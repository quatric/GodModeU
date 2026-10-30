/*
 * NTFS USB drives, read / write, through extremscorner's libntfs (NTFS-3G, scripts/build_libntfs.sh) on top
 * of Mocha's raw sector access, like usbfat.c does for FAT32 / exFAT. libntfs takes a libfat-style
 * DISC_INTERFACE, whose calls carry no context, so each USB slot gets its own set of functions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <mocha/fsa.h>
#include <mocha/disc_interface.h>
#include <ntfs.h>

#include "usbntfs.h"
#include "vfs.h"

#define SECTOR    512
#define DMA_ALIGN 0x40      /* FSA's raw I/O wants 64-byte-aligned buffers */
#define MAX_IO    256       /* sectors per FSA request */

static struct {
    FSAClientHandle client;
    int32_t handle;
    bool open;
} slot[USB_SLOTS];
static u8 *bounce;

static bool raw_io(int i, uint32_t first, uint32_t n, u8 *p, bool write)
{
    if (!slot[i].open)
        return false;
    while (n) {
        uint32_t chunk = n < MAX_IO ? n : MAX_IO;
        bool direct = ((uintptr_t)p & (DMA_ALIGN - 1)) == 0;
        u8 *io = direct ? p : bounce;
        if (write && !direct)
            memcpy(io, p, chunk * SECTOR);
        FSError e = write ? FSAEx_RawWriteEx(slot[i].client, io, SECTOR, chunk, first, slot[i].handle)
                          : FSAEx_RawReadEx(slot[i].client, io, SECTOR, chunk, first, slot[i].handle);
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

#define SLOT_IFACE(I)                                                                                 \
    static bool on_##I(void) { return slot[I].open; }                                                 \
    static bool rd_##I(uint32_t s, uint32_t n, void *b) { return raw_io(I, s, n, b, false); }          \
    static bool wr_##I(uint32_t s, uint32_t n, const void *b) { return raw_io(I, s, n, (u8 *)b, true); } \
    static bool ok_##I(void) { return true; }
SLOT_IFACE(0) SLOT_IFACE(1) SLOT_IFACE(2) SLOT_IFACE(3)
SLOT_IFACE(4) SLOT_IFACE(5) SLOT_IFACE(6) SLOT_IFACE(7)

#define SLOT_ENTRY(I) { DEVICE_TYPE_WII_U_USB, FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_WII_U_USB, \
                        on_##I, on_##I, rd_##I, wr_##I, ok_##I, ok_##I }
static DISC_INTERFACE iface[USB_SLOTS] = { SLOT_ENTRY(0), SLOT_ENTRY(1), SLOT_ENTRY(2), SLOT_ENTRY(3),
                                           SLOT_ENTRY(4), SLOT_ENTRY(5), SLOT_ENTRY(6), SLOT_ENTRY(7) };

static void close_slot(int i)
{
    if (slot[i].open)
        FSAEx_RawCloseEx(slot[i].client, slot[i].handle);
    slot[i].open = false;
}

int usbntfs_mount(FSAClientHandle client, int i, const char *name)
{
    char dev[24];
    snprintf(dev, sizeof(dev), "/dev/usb%02d", i + 1);
    if (!bounce)
        bounce = memalign(DMA_ALIGN, MAX_IO * SECTOR);
    if (!bounce || FSAEx_RawOpenEx(client, dev, &slot[i].handle) < 0)
        return USBNTFS_NONE;
    slot[i].client = client;
    slot[i].open = true;

    sec_t *parts = NULL;
    int n = ntfsFindPartitions(&iface[i], &parts);
    int result = USBNTFS_NONE;
    if (n > 0) {
        /* Never NTFS_RECOVER: resetting a dirty journal can lose what Windows hadn't written yet. */
        errno = 0;
        if (ntfsMount(name, &iface[i], parts[0], 8, 64, NTFS_DEFAULT | NTFS_SHOW_HIDDEN_FILES))
            result = USBNTFS_MOUNTED;
        else
            result = errno == EDIRTY ? USBNTFS_DIRTY : USBNTFS_FAILED;
    }
    free(parts);
    if (result != USBNTFS_MOUNTED)
        close_slot(i);
    return result;
}

void usbntfs_unmount(int i, const char *name)
{
    ntfsUnmount(name, true);
    close_slot(i);
}

int usbntfs_mount_part(FSAClientHandle client, int drive, int i, unsigned start, const char *name)
{
    char dev[24];
    snprintf(dev, sizeof(dev), "/dev/usb%02d", drive + 1);
    if (!bounce)
        bounce = memalign(DMA_ALIGN, MAX_IO * SECTOR);
    if (!bounce || slot[i].open || FSAEx_RawOpenEx(client, dev, &slot[i].handle) < 0)
        return USBNTFS_NONE;
    slot[i].client = client;
    slot[i].open = true;
    errno = 0;
    int result;
    if (ntfsMount(name, &iface[i], start, 8, 64, NTFS_DEFAULT | NTFS_SHOW_HIDDEN_FILES))
        result = USBNTFS_MOUNTED;
    else
        result = errno == EDIRTY ? USBNTFS_DIRTY : USBNTFS_FAILED;
    if (result != USBNTFS_MOUNTED)
        close_slot(i);
    return result;
}
