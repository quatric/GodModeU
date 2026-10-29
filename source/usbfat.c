/*
 * FAT32 and exFAT USB drives. The Wii U's own FAT driver refuses to mount USB drives (permission
 * error, even through Mocha), but their raw sectors read fine, so devkitPro's libdvm / FatFs
 * (scripts/build_libdvm.sh) mounts them on top of Mocha's raw FSA access, as GodModeWii does on the Wii.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <dirent.h>
#include <mocha/fsa.h>
#include <dvm.h>
#include <ext2.h>

#include "usbfat.h"
#include "gmu_types.h"

#define SECTOR    512
#define DMA_ALIGN 0x40      /* FSA's raw I/O wants 64-byte-aligned buffers, as Dumpling uses */
#define MAX_IO    256       /* sectors per FSA request */

typedef struct {
    DvmDisc base;
    FSAClientHandle client;
    int32_t handle;
    u8 *bounce;             /* MAX_IO sectors, for buffers FSA can't use directly */
} RawDisc;

static bool raw_io(RawDisc *d, u8 *p, sec_t first, sec_t n, bool write)
{
    while (n) {
        sec_t chunk = n < MAX_IO ? n : MAX_IO;
        bool direct = ((uintptr_t)p & (DMA_ALIGN - 1)) == 0;
        u8 *io = direct ? p : d->bounce;
        if (write && !direct)
            memcpy(io, p, chunk * SECTOR);
        FSError e = write ? FSAEx_RawWriteEx(d->client, io, SECTOR, chunk, first, d->handle)
                          : FSAEx_RawReadEx(d->client, io, SECTOR, chunk, first, d->handle);
        if (e < 0)
            return false;
        if (!write && !direct)
            memcpy(p, io, chunk * SECTOR);
        p += chunk * SECTOR;
        first += chunk;
        n -= chunk;
    }
    return true;
}

static bool raw_read(DvmDisc *self, void *buf, sec_t first, sec_t n)
{
    return raw_io((RawDisc *)self, buf, first, n, false);
}

static bool raw_write(DvmDisc *self, const void *buf, sec_t first, sec_t n)
{
    return raw_io((RawDisc *)self, (u8 *)buf, first, n, true);
}

static void raw_flush(DvmDisc *self)
{
    (void)self;
}

static void raw_destroy(DvmDisc *self)
{
    RawDisc *d = (RawDisc *)self;
    FSAEx_RawCloseEx(d->client, d->handle);
    free(d->bounce);
    free(d);
}

static const DvmDiscIface raw_iface = { raw_destroy, raw_read, raw_write, raw_flush };

static bool is_fat_vbr(const u8 *s)
{
    return s[510] == 0x55 && s[511] == 0xAA && (!memcmp(s + 0x36, "FAT", 3) || !memcmp(s + 0x52, "FAT32", 5));
}

static u32 rd32le(const u8 *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

enum { VOL_NONE, VOL_FAT, VOL_EXFAT, VOL_EXT };

/* An ext2/3/4 superblock 1024 bytes into the volume. */
static bool ext_at(RawDisc *d, u8 *buf, u32 start)
{
    return raw_io(d, buf, start + 2, 1, false) && buf[0x38] == 0x53 && buf[0x39] == 0xEF;
}

/* The first sector of an ext volume: at sector 0, an MBR type 0x83 partition, or a GPT "Linux filesystem" one. */
static bool find_ext(RawDisc *d, u8 *buf, u32 *start)
{
    static const u8 gpt_linux[16] = { 0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
                                      0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 };
    u32 cand[128];
    int n = 0;
    if (ext_at(d, buf, 0)) {
        *start = 0;
        return true;
    }
    if (!raw_io(d, buf, 0, 1, false) || buf[510] != 0x55 || buf[511] != 0xAA)
        return false;
    if (buf[0x1C2] == 0xEE) {
        if (!raw_io(d, buf, 1, 1, false) || memcmp(buf, "EFI PART", 8))
            return false;
        u32 table = rd32le(buf + 72), count = rd32le(buf + 80);
        for (u32 i = 0; i < count && i < 128; i++) {
            if (i % 4 == 0 && !raw_io(d, buf, table + i / 4, 1, false))
                break;
            const u8 *e = buf + i % 4 * 128;
            if (!memcmp(e, gpt_linux, 16))
                cand[n++] = rd32le(e + 32);
        }
    } else {
        for (int i = 0; i < 4; i++)
            if (buf[0x1C2 + i * 16] == 0x83)
                cand[n++] = rd32le(buf + 0x1C6 + i * 16);
    }
    for (int i = 0; i < n; i++)
        if (cand[i] && ext_at(d, buf, cand[i])) {
            *start = cand[i];
            return true;
        }
    return false;
}

/* Like blkid: ext4 if it uses anything ext3 can't read, ext3 if it has a journal. */
static int ext_version(const u8 *sb)
{
    if ((rd32le(sb + 0x60) & ~0x16u) || (rd32le(sb + 0x64) & ~0x7u))
        return 4;
    return rd32le(sb + 0x5C) & 0x4 ? 3 : 2;
}

/* Finds the volume the way FatFs does (VBR at sector 0, else GPT basic data, else MBR), from GodModeWii. */
static int volume_type(RawDisc *d, u8 *buf)
{
    static const u8 gpt_basic_data[16] = { 0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
                                           0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 };
    u32 start[128];
    int n = 0;
    if (!raw_io(d, buf, 0, 1, false))
        return VOL_NONE;
    if (!memcmp(buf + 3, "EXFAT   ", 8))
        return VOL_EXFAT;
    if (is_fat_vbr(buf))
        return VOL_FAT;
    if (buf[510] != 0x55 || buf[511] != 0xAA)
        return VOL_NONE;
    if (buf[0x1C2] == 0xEE) {
        if (!raw_io(d, buf, 1, 1, false) || memcmp(buf, "EFI PART", 8))
            return VOL_NONE;
        u32 table = rd32le(buf + 72), count = rd32le(buf + 80);
        for (u32 i = 0; i < count && i < 128; i++) {
            if (i % 4 == 0 && !raw_io(d, buf, table + i / 4, 1, false))
                break;
            const u8 *e = buf + i % 4 * 128;
            if (!memcmp(e, gpt_basic_data, 16))
                start[n++] = rd32le(e + 32);
        }
    } else {
        for (int i = 0; i < 4; i++)
            start[n++] = rd32le(buf + 0x1C6 + i * 16);
    }
    for (int i = 0; i < n; i++) {
        if (!start[i] || !raw_io(d, buf, start[i], 1, false))
            continue;
        if (!memcmp(buf + 3, "EXFAT   ", 8))
            return VOL_EXFAT;
        if (is_fat_vbr(buf))
            return VOL_FAT;
    }
    return VOL_NONE;
}

int usbfat_mount(FSAClientHandle client, int slot, const char *name)
{
    char dev[24];
    snprintf(dev, sizeof(dev), "/dev/usb%02d", slot + 1);
    RawDisc *d = calloc(1, sizeof(*d));
    if (!d)
        return false;
    d->bounce = memalign(DMA_ALIGN, MAX_IO * SECTOR);
    if (!d->bounce || FSAEx_RawOpenEx(client, dev, &d->handle) < 0) {
        free(d->bounce);
        free(d);
        return USBFAT_NONE;
    }
    d->client = client;
    d->base.vt = &raw_iface;
    d->base.features = FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE;
    d->base.sector_shift = 9;
    d->base.num_sectors = ~(sec_t)0;
    /* FatFs mounts lazily and would accept anything, so only drives with a FAT or exFAT volume go on. */
    u32 start = 0;
    int type = volume_type(d, d->bounce), ext = 0;
    if (type == VOL_NONE && find_ext(d, d->bounce, &start)) {
        type = VOL_EXT;
        ext = ext_version(d->bounce);
    }
    if (type == VOL_NONE) {
        raw_destroy(&d->base);
        return USBFAT_NONE;
    }
    static bool ext_registered;
    if (type == VOL_EXT && !ext_registered)
        ext_registered = dvmRegisterFsDriver(&g_ext2FsDriver);
    /* ext is read-only: the disc refuses writes, and the driver mounts without journal replay. */
    if (type == VOL_EXT)
        d->base.features &= ~FEATURE_MEDIUM_CANWRITE;

    /* The cache takes over the raw disc and destroys it with the volume. */
    DvmDisc *disc = dvmDiscCacheCreate(&d->base, 8, 64);
    if (!dvmMountVolume(name, disc, start, type == VOL_EXT ? "ext2" : "vfat")) {
        disc->vt->destroy(disc);
        return USBFAT_NONE;
    }
    char root[24];
    snprintf(root, sizeof(root), "%s:/", name);
    DIR *dir = opendir(root);
    if (dir) {
        closedir(dir);
        return type == VOL_EXT ? USBFAT_EXT2 + ext - 2 : type == VOL_EXFAT ? USBFAT_EXFAT : USBFAT_FAT;
    }
    dvmUnmountVolume(name);
    return USBFAT_NONE;
}

void usbfat_unmount(const char *name)
{
    dvmUnmountVolume(name);
}
