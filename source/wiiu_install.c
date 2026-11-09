/* Installing Wii U titles: WUP folders through the system's MCP installer (as WUP Installer GX2 does),
 * and WUD / WUX disc images turned into WUP folders first (FIX94's wud2app, MIT). See wiiu_install.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
#include <ctype.h>
#include <stdarg.h>

#include <coreinit/mcp.h>
#include <coreinit/ios.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#include "wiiu_install.h"
#include "vfs.h"
#include "ui.h"
#include "input.h"
#include "ops.h"
#include "aes.h"
#include "sha1.h"

#define APP_DIR "sd:/wiiu/apps/GodModeU"

static u16 be16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u64 be64(const u8 *p) { return (u64)be32(p) << 32 | be32(p + 4); }
static u32 le32(const u8 *p) { return (u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0]; }
static u64 le64(const u8 *p) { return (u64)le32(p + 4) << 32 | le32(p); }

static void msg(int color, const char *title, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void msg(int color, const char *title, const char *fmt, ...)
{
    char t[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof(t), fmt, ap);
    va_end(ap);
    ui_message(title, color, t);
}

/* ================================================================ WUP folders */

bool wup_is_folder(const char *path)
{
    char p[VFS_PATH_MAX];
    if (vfs_is_archive(path)) return false;
    if (!vfs_join(p, sizeof(p), path, "title.tmd") || !vfs_exists(p)) return false;
    return vfs_join(p, sizeof(p), path, "title.tik") && vfs_exists(p);
}

#define MCP_COMMAND_INSTALL_ASYNC 0x81
#define MCP_PATH_MAX 0x27F

static volatile int g_inst_done;
static volatile u32 g_inst_err;

static void install_cb(IOSError err, void *ctx)
{
    (void)ctx;
    g_inst_err = (u32)err;
    g_inst_done = 1;
}

static const char *install_error_text(u32 e, bool usb)
{
    if (e == 0xFFFCFFE9 && usb) return "The USB drive couldn't be reached (is a Wii U formatted drive attached?).";
    if (e == 0xFFFBF446 || e == 0xFFFBF43F) return "title.tik is missing or bad.";
    if (e == 0xFFFBF441) return "The DLC title.tik belongs to another console.";
    if (e == 0xFFFCFFE4) return "Not enough space on the target drive.";
    if (e == 0xFFFFF825) return "The SD card couldn't be read. Format it as FAT32 with 32 KB clusters, or try another card.";
    if ((e & 0xFFFF0000) == 0xFFFB0000)
        return "Check that the WUP files are correct and complete. Titles not signed by Nintendo (DLC, "
               "eShop dumps, homebrew) need signature patches.";
    return "The system's installer refused the title.";
}

/* Installs one WUP folder on the SD card. Shows its own errors; true when it worked. */
static bool install_one(const char *path, bool usb)
{
    const char *name = vfs_basename(path);
    if (vfs_dev_of(path) != DEV_SD || vfs_is_archive(path)) {
        msg(C_RED, "INSTALL", "%s\n\nThe system installer only reads WUP folders from the SD card. Copy the "
            "folder there (sd:/install/ for example) and install it from there.", name);
        return false;
    }
    /* "sd:/install/x" -> "/vol/app_sd/install/x": how MCP sees the SD card */
    const char *rel = strchr(path, '/');
    while (rel && *rel == '/') rel++;
    char mpath[MCP_PATH_MAX];
    snprintf(mpath, sizeof(mpath), "/vol/app_sd/%s", rel ? rel : "");
    size_t ml = strlen(mpath);
    while (ml > 1 && mpath[ml - 1] == '/') mpath[--ml] = 0;

    int h = MCP_Open();
    if (h < 0) {
        msg(C_RED, "INSTALL", "%s\n\nCouldn't open the system's installer (MCP).", name);
        return false;
    }
    u32 *info = memalign(0x40, 0x40);
    char *ipath = memalign(0x40, MCP_PATH_MAX + 1);
    IOSVec *vec = memalign(0x40, 0x40);
    bool ok = false;
    if (!info || !ipath || !vec) {
        msg(C_RED, "INSTALL", "Out of memory.");
        goto out;
    }
    memset(info, 0, 0x40);
    if (MCP_InstallGetInfo(h, mpath, (MCPInstallInfo *)info) != 0) {
        msg(C_RED, "INSTALL", "%s\n\nThe installer couldn't read this title. Check that every WUP file (title.tmd, "
            "title.tik, title.cert, the .app and .h3 files) is in the folder.", name);
        goto out;
    }
    u32 hi = info[0], lo = info[1];
    if (hi != 0x00050000 && hi != 0x0005000E && hi != 0x0005000C && hi != 0x00050002) {
        msg(C_RED, "INSTALL", "%s\n\nTitle %08X-%08X is not a game, update, DLC or demo. GodModeU doesn't "
            "install system titles.", name, (unsigned)hi, (unsigned)lo);
        goto out;
    }
    MCPInstallTarget tgt = usb ? MCP_INSTALL_TARGET_USB : MCP_INSTALL_TARGET_MLC;
    if (MCP_InstallSetTargetDevice(h, tgt) != 0 || MCP_InstallSetTargetUsb(h, (int)tgt) != 0) {
        msg(C_RED, "INSTALL", "%s\n\nCouldn't select the %s as the target.", name,
            usb ? "USB drive (is a Wii U formatted drive attached?)" : "internal storage");
        goto out;
    }

    memset(ipath, 0, MCP_PATH_MAX + 1);
    snprintf(ipath, MCP_PATH_MAX, "%s", mpath);
    memset(vec, 0, 0x40);
    vec->vaddr = ipath;
    vec->len = MCP_PATH_MAX;
    info[2] = MCP_COMMAND_INSTALL_ASYNC;
    info[3] = (u32)vec;
    info[4] = 1;
    info[5] = 0;
    g_inst_done = 0;
    g_inst_err = 0;
    if (IOS_IoctlvAsync(h, MCP_COMMAND_INSTALL_ASYNC, 1, 0, vec, install_cb, info) != 0) {
        msg(C_RED, "INSTALL", "%s\n\nThe installer didn't start.", name);
        goto out;
    }

    char label[96];
    snprintf(label, sizeof(label), "Installing %.40s to %s", name, usb ? "USB" : "internal storage");
    ui_busy_begin(label);
    u32 *prog = memalign(0x40, 0x40);
    while (!g_inst_done) {
        if (prog) {
            memset(prog, 0, 0x40);
            MCP_InstallGetProgress(h, (MCPInstallProgress *)prog);
            if (prog[0] == 1) {
                u64 total = (u64)prog[3] << 32 | prog[4];
                u64 done = (u64)prog[5] << 32 | prog[6];
                ui_busy_progress(done, total);
            } else {
                ui_busy_tick();
            }
        }
        OSSleepTicks(OSMillisecondsToTicks(50));
    }
    free(prog);
    ui_busy_end();
    if (g_inst_err) {
        msg(C_RED, "INSTALL FAILED", "%s\n\nError %08X: %s", name, (unsigned)g_inst_err,
            install_error_text(g_inst_err, usb));
        goto out;
    }
    ok = true;
out:
    free(info);
    free(ipath);
    free(vec);
    MCP_Close(h);
    return ok;
}

/* -1: cancelled, 0: internal, 1: USB */
static int pick_target(const char *what)
{
    const char *items[] = { "Internal storage (MLC)", "USB drive (Wii U formatted)" };
    char title[96];
    snprintf(title, sizeof(title), "INSTALL %.60s TO", what);
    int r = ui_menu(title, items, NULL, 2, 0);
    return r < 0 ? -1 : r;
}

void wup_install_paths(char **paths, int n)
{
    if (n <= 0) return;
    char what[64];
    if (n == 1) snprintf(what, sizeof(what), "%s", vfs_basename(paths[0]));
    else snprintf(what, sizeof(what), "%d TITLES", n);
    int t = pick_target(what);
    if (t < 0) return;
    int good = 0;
    for (int i = 0; i < n; i++)
        if (install_one(paths[i], t == 1)) good++;
    input_flush();
    if (good == n && n == 1)
        msg(C_GREEN, "INSTALL", "%s installed. It shows up on the Wii U Menu.", vfs_basename(paths[0]));
    else if (good == n)
        msg(C_GREEN, "INSTALL", "All %d titles installed. They show up on the Wii U Menu.", n);
    else if (n > 1)
        msg(C_YELLOW, "INSTALL", "%d of %d titles installed.", good, n);
}

/* ================================================================ disc images */

bool wud_is_name(const char *name)
{
    size_t l = strlen(name);
    return l > 4 && (!strcasecmp(name + l - 4, ".wud") || !strcasecmp(name + l - 4, ".wux"));
}

#define PART_SIZE 0x80000000ULL

enum { IMG_PLAIN, IMG_WUX, IMG_PARTS };

typedef struct {
    int kind;
    char dir[VFS_PATH_MAX];
    vfs_file f;
    bool open;
    int part;        /* the part open (IMG_PARTS) */
    int nparts;
    u64 size;
    u32 ssz;         /* WUX sector size */
    u32 *idx;
    u32 nidx;
    u64 dstart;
} wimg_t;

static bool parts_open(wimg_t *im, int p)
{
    if (im->open && im->part == p) return true;
    if (im->open) vfs_close(&im->f);
    im->open = false;
    char n[32], path[VFS_PATH_MAX];
    snprintf(n, sizeof(n), "game_part%d.wud", p + 1);
    if (!vfs_join(path, sizeof(path), im->dir, n) || !vfs_open_read(&im->f, path)) return false;
    vfs_unbuffered(&im->f);
    im->open = true;
    im->part = p;
    return true;
}

static bool raw_read(vfs_file *f, u64 off, void *buf, u32 len)
{
    return vfs_seek(f, off) && vfs_read(f, buf, len) == (s32)len;
}

static bool img_read(wimg_t *im, u64 off, void *buf, u32 len)
{
    u8 *o = buf;
    if (im->kind == IMG_PLAIN) return raw_read(&im->f, off, buf, len);
    while (len) {
        u32 n;
        if (im->kind == IMG_PARTS) {
            int p = (int)(off / PART_SIZE);
            u64 within = off % PART_SIZE;
            n = (u32)((PART_SIZE - within) < len ? (PART_SIZE - within) : len);
            if (!parts_open(im, p) || !raw_read(&im->f, within, o, n)) return false;
        } else {
            u64 s = off / im->ssz;
            u32 within = (u32)(off % im->ssz);
            if (s >= im->nidx) return false;
            /* runs of sectors stored one after the other are read at once */
            u64 run = im->ssz - within;
            u64 next = s + 1;
            while (run < len && next < im->nidx && im->idx[next] == im->idx[next - 1] + 1) {
                run += im->ssz;
                next++;
            }
            n = run < len ? (u32)run : len;
            u64 real = im->dstart + (u64)im->idx[s] * im->ssz + within;
            if (!raw_read(&im->f, real, o, n)) return false;
        }
        o += n;
        off += n;
        len -= n;
    }
    return true;
}

static void img_close(wimg_t *im)
{
    if (im->open) vfs_close(&im->f);
    im->open = false;
    free(im->idx);
    im->idx = NULL;
}

static bool is_part_name(const char *name, int *num)
{
    int k;
    char tail[8];
    if (sscanf(name, "game_part%d.%4s", &k, tail) != 2 || strcasecmp(tail, "wud")) return false;
    *num = k;
    return true;
}

static bool img_open(wimg_t *im, const char *path, char *err, size_t errn)
{
    memset(im, 0, sizeof(*im));
    vfs_parent(path, im->dir, sizeof(im->dir));
    const char *base = vfs_basename(path);
    int pn;
    if (is_part_name(base, &pn)) {
        im->kind = IMG_PARTS;
        char p[VFS_PATH_MAX], nm[32];
        for (int i = 0; i < 64; i++) {
            u64 sz;
            bool d;
            snprintf(nm, sizeof(nm), "game_part%d.wud", i + 1);
            if (!vfs_join(p, sizeof(p), im->dir, nm) || !vfs_stat(p, &d, &sz)) break;
            im->nparts = i + 1;
            im->size = (u64)i * PART_SIZE + sz;
        }
        if (!im->nparts) {
            snprintf(err, errn, "game_part1.wud is missing.");
            return false;
        }
        if (!parts_open(im, 0)) {
            snprintf(err, errn, "Couldn't open game_part1.wud.");
            return false;
        }
        return true;
    }
    if (!vfs_open_read(&im->f, path)) {
        snprintf(err, errn, "Couldn't open the file.");
        return false;
    }
    vfs_unbuffered(&im->f);
    im->open = true;
    u8 h[0x20];
    if (!raw_read(&im->f, 0, h, sizeof(h))) {
        snprintf(err, errn, "Couldn't read the file.");
        img_close(im);
        return false;
    }
    if (le32(h) == 0x30585557 && le32(h + 4) == 0x1099D02E) {
        im->kind = IMG_WUX;
        im->ssz = le32(h + 8);
        im->size = le64(h + 0x10);
        if (im->ssz < 0x100 || im->ssz > 0x10000000 || (im->ssz & (im->ssz - 1)) || !im->size) {
            snprintf(err, errn, "Damaged WUX header.");
            img_close(im);
            return false;
        }
        im->nidx = (u32)((im->size + im->ssz - 1) / im->ssz);
        im->idx = malloc((size_t)im->nidx * 4);
        if (!im->idx || !raw_read(&im->f, 0x20, im->idx, im->nidx * 4)) {
            snprintf(err, errn, "Couldn't read the WUX sector table.");
            img_close(im);
            return false;
        }
        for (u32 i = 0; i < im->nidx; i++) im->idx[i] = le32((u8 *)&im->idx[i]);
        im->dstart = (0x20 + (u64)im->nidx * 4 + im->ssz - 1) / im->ssz * im->ssz;
    } else {
        im->kind = IMG_PLAIN;
        im->size = im->f.size;
    }
    return true;
}

/* ---------------------------------------------------------------- keys */

static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* A key file: 16 raw bytes, or 32 hex digits (spaces and line ends ignored). */
static bool read_key(const char *path, u8 key[16])
{
    vfs_file f;
    if (!vfs_open_read(&f, path)) return false;
    u8 buf[128];
    s32 n = f.size <= sizeof(buf) ? vfs_read(&f, buf, (u32)f.size) : -1;
    vfs_close(&f);
    if (n == 16) {
        memcpy(key, buf, 16);
        return true;
    }
    if (n <= 0) return false;
    int d = 0;
    u8 k[16];
    for (s32 i = 0; i < n; i++) {
        int v = hexv(buf[i]);
        if (v < 0) {
            if (isspace(buf[i]) || buf[i] == '-') continue;
            return false;
        }
        if (d >= 32) return false;
        if (d & 1) k[d / 2] |= v;
        else k[d / 2] = (u8)(v << 4);
        d++;
    }
    if (d != 32) return false;
    memcpy(key, k, 16);
    return true;
}

/* The Wii U common key sits at 0xE0 in a nanddumper / Mocha otp.bin (0x400 bytes). */
static bool read_otp_key(const char *path, u8 key[16])
{
    vfs_file f;
    if (!vfs_open_read(&f, path)) return false;
    bool ok = f.size == 0x400 && raw_read(&f, 0xE0, key, 16);
    vfs_close(&f);
    return ok;
}

/* Every common key we can find, in order; the caller tries each against the title. */
static int common_keys(const char *imgdir, u8 keys[][16], char where[][VFS_PATH_MAX], int max)
{
    const char *dirs[] = { imgdir, APP_DIR, "sd:" };
    int n = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int d = 0; d < 3 && n < max; d++) {
            char p[VFS_PATH_MAX];
            if (!vfs_join(p, sizeof(p), dirs[d], pass ? "otp.bin" : "common.key")) continue;
            if (pass ? read_otp_key(p, keys[n]) : read_key(p, keys[n])) {
                snprintf(where[n], VFS_PATH_MAX, "%s", p);
                n++;
            }
        }
    return n;
}

/* The disc key: game.key next to the image (wudump), or "<image name>.key". */
static bool disc_key(const char *path, const char *dir, u8 key[16])
{
    char p[VFS_PATH_MAX], stem[VFS_NAME_MAX];
    snprintf(stem, sizeof(stem), "%s", vfs_basename(path));
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    if (vfs_join(p, sizeof(p), dir, "game.key") && read_key(p, key)) return true;
    strncat(stem, ".key", sizeof(stem) - strlen(stem) - 1);
    if (vfs_join(p, sizeof(p), dir, stem) && read_key(p, key)) return true;
    return vfs_join(p, sizeof(p), APP_DIR, stem) && read_key(p, key);
}

/* ---------------------------------------------------------------- the disc's layout */

#define TMD_CONTENTS 0xB04
#define TMD_ENTRY    0x30

typedef struct {
    char volname[11];
    u8 *tbl;           /* decrypted partition table, 0x8000 */
    int nparts;
    u8 *cert, *tik, *tmd;
    u32 certlen, tiklen, tmdlen;
    u64 tid;
    u16 ncontents;
    u64 gm_off;        /* GM partition */
    u8 hdr[0x8000];    /* its header (h3 hashes) */
    u8 tkey[16];       /* decrypted title key */
    u8 *fst;           /* content 0, decrypted */
    u64 fstsize;
    u64 total;         /* everything the WUP folder will hold */
    u64 biggest;
} disc_t;

static void disc_free(disc_t *d)
{
    free(d->tbl);
    free(d->cert);
    free(d->tik);
    free(d->tmd);
    free(d->fst);
}

#define ALIGN16(x) (((x) + 15) & ~(u64)15)

static const u8 *tmd_content(const disc_t *d, int i) { return d->tmd + TMD_CONTENTS + i * TMD_ENTRY; }

static bool read_dec(wimg_t *im, u64 off, u32 len, const u8 key[16], const u8 iv[16], u8 **out)
{
    u8 *b = memalign(0x40, (size_t)ALIGN16(len));
    if (!b) return false;
    if (!img_read(im, off, b, (u32)ALIGN16(len))) {
        free(b);
        return false;
    }
    aes128_t a;
    aes128_init_dec(&a, key);
    aes128_cbc_decrypt(&a, iv, b, b, (size_t)ALIGN16(len));
    *out = b;
    return true;
}

static int find_part(const disc_t *d, const char *name, size_t n)
{
    for (int i = 0; i < d->nparts; i++)
        if (!strncasecmp((const char *)d->tbl + 0x800 + i * 0x80, name, n)) return i;
    return -1;
}

static u64 part_offset(const disc_t *d, int i) { return (u64)be32(d->tbl + 0x800 + i * 0x80 + 0x20) * 0x8000; }

/* Reads the disc's tables, the SI partition's ticket / TMD / certificates and the GM partition's FST. */
static bool disc_open(wimg_t *im, const char *path, disc_t *d, char *err, size_t errn)
{
    memset(d, 0, sizeof(*d));
    u8 dkey[16], zero[16] = { 0 };
    if (!disc_key(path, im->dir, dkey)) {
        snprintf(err, errn, "The disc key wasn't found. Put the game.key from wudump next to the image (or name "
                 "it like the image, with .key).");
        return false;
    }
    if (!img_read(im, 0, d->volname, 10)) {
        snprintf(err, errn, "Couldn't read the image.");
        return false;
    }
    for (int i = 0; i < 10; i++)
        if (!isalnum((unsigned char)d->volname[i]) && d->volname[i] != '-') d->volname[i] = '_';
    if (!read_dec(im, 0x18000, 0x8000, dkey, zero, &d->tbl)) {
        snprintf(err, errn, "Couldn't read the partition table.");
        return false;
    }
    u8 hash[20];
    sha1_t sc;
    sha1_init(&sc);
    sha1_update(&sc, d->tbl + 0x800, 0x7800);
    sha1_final(&sc, hash);
    if (be32(d->tbl) != 0xCCA6E67B || memcmp(hash, d->tbl + 8, 20)) {
        snprintf(err, errn, "The partition table didn't decrypt: the disc key doesn't belong to this image, or it "
                 "isn't a Wii U disc.");
        return false;
    }
    d->nparts = (int)be32(d->tbl + 0x1C);
    if (d->nparts < 0 || d->nparts > 0xF0) d->nparts = 0xF0;

    int si = find_part(d, "SI", 3);
    if (si < 0) {
        snprintf(err, errn, "The disc has no SI partition.");
        return false;
    }
    u64 off = part_offset(d, si) + 0x8000;
    u8 *fst = NULL;
    if (!read_dec(im, off, 0x8000, dkey, zero, &fst)) {
        snprintf(err, errn, "Couldn't read the SI partition.");
        return false;
    }
    u32 ecount = be32(fst + 8) << 5;
    if (0x20 + ecount + 16 > 0x8000) goto bad_si;
    u32 entries = be32(fst + 0x20 + ecount + 8);
    u32 nameoff = 0x20 + ecount + (entries << 4);
    if (nameoff >= 0x8000) goto bad_si;
    off += 0x8000;
    for (u32 e = 1; e < entries && !(d->cert && d->tik && d->tmd); e++) {
        const u8 *fe = fst + 0x20 + ecount + e * 16;
        u32 noff = be32(fe) & 0xFFFFFF;
        if (nameoff + noff >= 0x8000) continue;
        const char *name = (const char *)fst + nameoff + noff;
        if (strncasecmp(name, "title.", 6)) continue;
        u32 len = be32(fe + 8);
        u64 coff = (u64)be32(fe + 4) << 5;
        if (!len || len > 0x100000) continue;
        u8 iv[16] = { 0 };
        u64 civ = coff >> 16;
        for (int k = 0; k < 8; k++) iv[8 + k] = (u8)(civ >> (56 - 8 * k));
        u8 *b;
        if (!read_dec(im, off + coff, len, dkey, iv, &b)) continue;
        if (!strcasecmp(name, "title.cert") && !d->cert) {
            d->cert = b;
            d->certlen = len;
        } else if (!strcasecmp(name, "title.tik") && !d->tik && len >= 0x1E4 && be32(b + 0x1DC) == 0x00050000) {
            d->tik = b;
            d->tiklen = len;
        } else if (!strcasecmp(name, "title.tmd") && !d->tmd && len >= TMD_CONTENTS && be32(b + 0x18C) == 0x00050000) {
            d->tmd = b;
            d->tmdlen = len;
        } else {
            free(b);
        }
    }
    free(fst);
    fst = NULL;
    if (!d->tik || !d->tmd) {
        snprintf(err, errn, "The game's ticket or TMD wasn't found on the disc.");
        return false;
    }
    d->tid = be64(d->tmd + 0x18C);
    d->ncontents = be16(d->tmd + 0x1DE);
    if (!d->ncontents || TMD_CONTENTS + (u32)d->ncontents * TMD_ENTRY > d->tmdlen) {
        snprintf(err, errn, "The game's TMD is damaged.");
        return false;
    }

    char gm[20];
    snprintf(gm, sizeof(gm), "GM%016llX", (unsigned long long)d->tid);
    int gp = find_part(d, gm, 18);
    if (gp < 0) {
        snprintf(err, errn, "The disc has no %s partition.", gm);
        return false;
    }
    d->gm_off = part_offset(d, gp);
    if (!img_read(im, d->gm_off, d->hdr, sizeof(d->hdr))) {
        snprintf(err, errn, "Couldn't read the game partition.");
        return false;
    }
    d->fstsize = be64(tmd_content(d, 0) + 8);
    if (!d->fstsize || d->fstsize > 0x4000000) {
        snprintf(err, errn, "The game's file table is damaged.");
        return false;
    }

    /* the title key, with each common key found until the FST decrypts */
    u8 keys[6][16];
    char where[6][VFS_PATH_MAX];
    int nk = common_keys(im->dir, keys, where, 6);
    if (!nk) {
        snprintf(err, errn, "The Wii U common key wasn't found. Put common.key (16 bytes or 32 hex digits) or the "
                 "otp.bin from nanddumper in " APP_DIR "/.");
        return false;
    }
    u8 *fstenc = memalign(0x40, (size_t)ALIGN16(d->fstsize));
    if (!fstenc || !img_read(im, d->gm_off + 0x8000, fstenc, (u32)ALIGN16(d->fstsize))) {
        free(fstenc);
        snprintf(err, errn, "Couldn't read the game's file table.");
        return false;
    }
    d->fst = memalign(0x40, (size_t)ALIGN16(d->fstsize));
    bool found = false;
    for (int k = 0; k < nk && d->fst && !found; k++) {
        u8 iv[16] = { 0 };
        memcpy(iv, d->tik + 0x1DC, 8);
        aes128_t a;
        aes128_init_dec(&a, keys[k]);
        aes128_cbc_decrypt(&a, iv, d->tik + 0x1BF, d->tkey, 16);
        u8 civ[16] = { 0 };
        memcpy(civ, tmd_content(d, 0) + 4, 2);
        aes128_init_dec(&a, d->tkey);
        aes128_cbc_decrypt(&a, civ, fstenc, d->fst, (size_t)ALIGN16(d->fstsize));
        found = !memcmp(d->fst, "FST", 3);
    }
    free(fstenc);
    if (!found) {
        snprintf(err, errn, "None of the common keys found (%s%s) decrypts this game. The Wii U common key is "
                 "needed (not the Wii's).", where[0], nk > 1 ? " and others" : "");
        return false;
    }
    if (0x20 + (u64)d->ncontents * 0x20 > d->fstsize) {
        snprintf(err, errn, "The game's file table is too short.");
        return false;
    }
    d->total = d->certlen + d->tiklen + d->tmdlen + ALIGN16(d->fstsize);
    for (int i = 1; i < d->ncontents; i++) {
        u64 s = be64(tmd_content(d, i) + 8);
        d->total += s;
        if (s > d->biggest) d->biggest = s;
    }
    return true;
bad_si:
    free(fst);
    snprintf(err, errn, "The SI partition's file table is damaged.");
    return false;
}

/* ---------------------------------------------------------------- writing the WUP folder */

static bool write_file(const char *dir, const char *name, const void *buf, u32 len)
{
    char p[VFS_PATH_MAX];
    vfs_file f;
    if (!vfs_join(p, sizeof(p), dir, name) || !vfs_open_write(&f, p)) return false;
    bool ok = vfs_write(&f, buf, len) == (s32)len;
    return vfs_close_checked(&f) && ok;
}

#define COPY_CHUNK (4u << 20)

/* 1 done, 0 failed (err set), -1 cancelled */
static int extract(wimg_t *im, disc_t *d, const char *out, char *err, size_t errn)
{
    if (!vfs_exists(out) && !vfs_mkdir(out)) {
        snprintf(err, errn, "Couldn't create %s.", out);
        return 0;
    }
    if ((d->cert && !write_file(out, "title.cert", d->cert, d->certlen)) ||
        !write_file(out, "title.tik", d->tik, d->tiklen) || !write_file(out, "title.tmd", d->tmd, d->tmdlen)) {
        snprintf(err, errn, "Couldn't write the title files to %s.", out);
        return 0;
    }
    u8 *buf = memalign(0x40, COPY_CHUNK);
    if (!buf) {
        snprintf(err, errn, "Out of memory.");
        return 0;
    }
    u32 hcount = be32(d->hdr + 0x10);
    u32 hashpos = 0x40 + hcount * 4;
    u64 done = 0;
    int r = 1;
    ui_busy_begin("Writing the WUP folder");
    for (int i = 0; i < d->ncontents && r == 1; i++) {
        const u8 *c = tmd_content(d, i);
        u32 cid = be32(c);
        u16 type = be16(c + 6);
        u64 size = i ? be64(c + 8) : ALIGN16(d->fstsize);
        u64 src = i ? d->gm_off + (u64)be32(d->fst + 0x20 + i * 0x20) * 0x8000 : d->gm_off + 0x8000;
        char name[24], p[VFS_PATH_MAX];
        snprintf(name, sizeof(name), "%08x.app", (unsigned)cid);
        vfs_file f;
        if (!vfs_join(p, sizeof(p), out, name) || !vfs_open_write(&f, p)) {
            snprintf(err, errn, "Couldn't create %s.", name);
            r = 0;
            break;
        }
        vfs_unbuffered(&f);
        for (u64 left = size; left && r == 1;) {
            u32 n = left > COPY_CHUNK ? COPY_CHUNK : (u32)left;
            if (!img_read(im, src, buf, n)) {
                snprintf(err, errn, "Couldn't read the image at %llX.", (unsigned long long)src);
                r = 0;
            } else if (vfs_write(&f, buf, n) != (s32)n) {
                snprintf(err, errn, "Couldn't write %s%s%s.", name, g_write_err ? ": " : "",
                         g_write_err ? g_write_err : "");
                r = 0;
            } else if (input_cancel_held()) {
                r = -1;
            }
            src += n;
            left -= n;
            done += n;
            ui_busy_progress(done, d->total);
        }
        if (!vfs_close_checked(&f) && r == 1) {
            snprintf(err, errn, "Couldn't finish %s.", name);
            r = 0;
        }
        if (r == 1 && i && (type & 2)) {
            u32 hn = (u32)(size / 0x10000000ULL + 1) * 0x14;
            snprintf(name, sizeof(name), "%08x.h3", (unsigned)cid);
            if (hashpos + hn > sizeof(d->hdr) || !write_file(out, name, d->hdr + hashpos, hn)) {
                snprintf(err, errn, "Couldn't write %s.", name);
                r = 0;
            }
            hashpos += hn;
        }
    }
    ui_busy_end();
    free(buf);
    return r;
}

void wud_menu(const char *path)
{
    wimg_t im;
    char err[384];
    ui_busy_begin("Reading the disc image");
    bool ok = img_open(&im, path, err, sizeof(err));
    disc_t *d = ok ? calloc(1, sizeof(disc_t)) : NULL;
    if (ok && !d) {
        snprintf(err, sizeof(err), "Out of memory.");
        ok = false;
    }
    if (ok) ok = disc_open(&im, path, d, err, sizeof(err));
    ui_busy_end();
    if (!ok) {
        msg(C_RED, "WII U DISC IMAGE", "%s\n\n%s", vfs_basename(path), err);
        goto done;
    }

    char out[VFS_PATH_MAX], sz[24], big[24];
    snprintf(out, sizeof(out), "sd:/install/%s", d->volname[0] ? d->volname : "disc");
    ui_fmt_size(sz, sizeof(sz), d->total);
    ui_fmt_size(big, sizeof(big), d->biggest);
    for (;;) {
        char title[96], info[160];
        snprintf(info, sizeof(info), "%s  %016llX  %d contents, %s", d->volname, (unsigned long long)d->tid,
                 d->ncontents, sz);
        snprintf(title, sizeof(title), "%.70s", info);
        const char *items[] = {
            "Install to the internal storage (MLC)",
            "Install to a USB drive",
            "Only write the WUP folder to sd:/install/",
            "Show the partitions",
        };
        int c = ui_menu(title, items, NULL, 4, 0);
        if (c < 0) break;
        if (c == 3) {
            char t[1536];
            size_t n = 0;
            for (int i = 0; i < d->nparts && n < sizeof(t) - 80; i++) {
                char pn[0x20];
                memcpy(pn, d->tbl + 0x800 + i * 0x80, 0x1F);
                pn[0x1F] = 0;
                n += snprintf(t + n, sizeof(t) - n, "%-20s at %011llX\n", pn, (unsigned long long)part_offset(d, i));
            }
            ui_message("PARTITIONS", C_CYAN, t);
            continue;
        }
        vfs_update_space(DEV_SD);
        if (!g_devs[DEV_SD].mounted) {
            msg(C_RED, "WII U DISC IMAGE", "Insert an SD card: the WUP folder is written there.");
            continue;
        }
        if (g_devs[DEV_SD].space_known && g_devs[DEV_SD].free < d->total + (1 << 20)) {
            char fr[24];
            ui_fmt_size(fr, sizeof(fr), g_devs[DEV_SD].free);
            msg(C_RED, "WII U DISC IMAGE", "The WUP folder needs %s; the SD card has %s free.", sz, fr);
            continue;
        }
        if (!g_devs[DEV_SD].exfat && d->biggest >= 0x100000000ULL) {
            msg(C_RED, "WII U DISC IMAGE", "One of the game's contents is %s, more than a FAT32 SD card can hold "
                "in one file.", big);
            continue;
        }
        if (vfs_exists(out) &&
            !ui_confirm("WII U DISC IMAGE", C_YELLOW, "The folder already exists. Write over the files in it?"))
            continue;
        vfs_mkdir("sd:/install");
        int r = extract(&im, d, out, err, sizeof(err));
        input_flush();
        if (r == 0) {
            msg(C_RED, "WII U DISC IMAGE", "%s\n\nThe folder %s is incomplete.", err, out);
            continue;
        }
        if (r < 0) {
            msg(C_YELLOW, "WII U DISC IMAGE", "Cancelled. The folder %s is incomplete.", out);
            continue;
        }
        if (c == 2) {
            msg(C_GREEN, "WII U DISC IMAGE", "Written to %s. Press A on its title.tmd to install it.", out);
            break;
        }
        bool inst = install_one(out, c == 1);
        input_flush();
        if (inst) {
            char *p = out;
            if (ui_confirm("INSTALLED", C_GREEN, "The game is installed. Delete the WUP folder from the SD card?"))
                ops_delete(&p, 1);
        }
        break;
    }
done:
    if (d) disc_free(d);
    free(d);
    img_close(&im);
}

/* ================================================================ help */

void nand_backup_help(void)
{
    static const char *const pages[] = {
        "Make a NAND backup before installing anything to the\n"
        "system. It's the only way back from most bricks, and it\n"
        "holds your console's keys.\n\n"
        "Cafe OS doesn't let programs read the NAND chips raw, so\n"
        "GodModeU can't dump them itself (it can copy files off\n"
        "mlc:/, slc:/ and nand:/). Use nanddumper under Aroma:\n\n"
        "1. Get nanddumper (the .rpx / .wuhb from its GitHub\n"
        "   releases) and put it in sd:/wiiu/apps/.\n"
        "2. Check the SD card's free space: the MLC dump needs\n"
        "   8 GB (Basic) or 32 GB (Deluxe) more than the rest.",

        "3. Launch nanddumper from the Wii U Menu or HBL.\n"
        "4. Set SLC: Yes, SLCCMPT: Yes, OTP: Yes, SEEPROM: Yes.\n"
        "   MLC: Yes if the card has room (recommended), else No.\n"
        "5. Press A and wait. The MLC takes a long time; don't\n"
        "   turn the console off.\n"
        "6. The dump is written to the root of the SD card:\n"
        "   slc.bin, slccmpt.bin, otp.bin, seeprom.bin and\n"
        "   mlc.bin.part01, mlc.bin.part02, ...\n"
        "7. Copy them to a computer (two places is better), then\n"
        "   delete them from the SD card.",

        "Keep otp.bin and seeprom.bin safe: the backup only works\n"
        "on the console it came from, and those hold its keys.\n"
        "Restoring a NAND needs ISFShax / recovery tools or a\n"
        "hardmod: see wiiu.hacks.guide.\n\n"
        "GodModeU uses otp.bin too: copy it to\n"
        "  " APP_DIR "/otp.bin\n"
        "and it has the Wii U common key for turning WUD / WUX\n"
        "disc images into WUP folders.\n\n"
        "vWii: nand:/ here is the vWii NAND (files). For a raw\n"
        "vWii NAND image, use GodModeWii in vWii.",
    };
    ui_message_pages("NAND BACKUP", C_CYAN, pages, 3);
}

void wup_help(void)
{
    static const char *const pages[] = {
        "A WUP folder holds title.tmd, title.tik, title.cert\n"
        "and the contents (00000000.app ... and .h3 files).\n\n"
        "1. Copy the folder to the SD card, sd:/install/<name>.\n"
        "2. Open the folder and press A on title.tmd, or pick the\n"
        "   folder (several can be marked) and choose \"Install\n"
        "   Wii U title (WUP)\" in the action menu.\n"
        "3. Choose the internal storage or a USB drive. The USB\n"
        "   drive must already be formatted by the Wii U.\n\n"
        "Games, updates, DLC and demos can be installed; system\n"
        "titles can't.",

        "Requirements:\n"
        "- Aroma with signature patches (the sigpatches module /\n"
        "  Mocha), as WUP Installer GX2 needs, for anything not\n"
        "  signed by Nintendo for this console.\n"
        "- An SD card formatted FAT32 with 32 KB clusters.\n\n"
        "Errors:\n"
        "FFFCFFE9  no USB drive        FFFCFFE4  not enough space\n"
        "FFFBF446  bad title.tik       FFFBF441  DLC ticket from\n"
        "FFFFF825  SD card problem               another console\n"
        "FFFBxxxx  incomplete files or no signature patches",

        "Disc images (.wud, .wux, wudump's game_part1.wud):\n"
        "press A on one to install it. GodModeU writes its game\n"
        "partition out as a WUP folder in sd:/install/ (as\n"
        "wud2app does) and installs that.\n\n"
        "Keys:\n"
        "- the disc key: wudump's game.key next to the image, or\n"
        "  <image name>.key\n"
        "- the Wii U common key: common.key (16 bytes or 32 hex\n"
        "  digits) or nanddumper's otp.bin, in " APP_DIR "/,\n"
        "  sd:/ or the image's folder.\n\n"
        "The SD card needs room for the whole game.",
    };
    ui_message_pages("INSTALLING WII U TITLES", C_CYAN, pages, 3);
}
