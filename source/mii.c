/*
 * Mii export to the SD card or a USB drive, into /savemiis/:
 *  - Wii U Miis (Mii Maker's FFL_ODB.dat on the MLC) as <Name>.ffsd, the 96-byte form PC Mii tools read:
 *    the record in little-endian order plus a CRC16;
 *  - vWii Miis (RFL_DB.dat on the vWii NAND) as <Name>.miigx, the raw 74-byte record in SaveGame
 *    Manager GX's layout, so it can import them again.
 * Both databases are only read.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mii.h"
#include "ui.h"
#include "input.h"
#include "vfs.h"

#define MAX_REC 96

typedef struct {
    const char *title;
    int slots, rec_size, first;      /* record table: count, size, offset */
    int id_off, id_len;              /* CreateID: all zero in an empty slot */
    int name_off, creator_off;       /* 10 UTF-16BE characters each */
    const char *magic;
    long file_size;
    const char *ext;
} mii_db;

static const mii_db WIIU = { "Wii U", 3000, 0x5C, 0x8, 0x0C, 10, 0x1A, 0x48, "FFOC", 0x43840, ".ffsd" };
static const mii_db VWII = { "vWii", 100, 0x4A, 0x4, 0x18, 8, 0x02, 0x36, "RNOD", 0x1F1E0, ".miigx" };

/* Mii Maker's save, one per region. */
static const char *const WIIU_PATHS[] = {
    "mlc:/usr/save/00050010/1004a100/user/common/db/FFL_ODB.dat",
    "mlc:/usr/save/00050010/1004a200/user/common/db/FFL_ODB.dat",
    "mlc:/usr/save/00050010/1004a000/user/common/db/FFL_ODB.dat",
};
#define VWII_PATH "nand:/shared2/menu/FaceLib/RFL_DB.dat"

typedef struct {
    u8 data[MAX_REC];
    char name[32];       /* for the screen: ASCII */
    char file[64];       /* for the file name: UTF-8, with unsafe characters replaced */
    char ascii[32];      /* the same in plain ASCII, for the SD card (see export) */
    char creator[32];
    bool marked;
} mii_t;

static mii_t *miis;
static int nmiis;
static const mii_db *db;

enum { NAME_SCREEN, NAME_FILE, NAME_ASCII };

/* Nintendo's private-use glyphs (hearts, symbols) and anything a FAT name can't hold become '_';
 * the screen gets '?' for whatever OSScreen's font can't draw, NAME_ASCII '_' for any non-ASCII. */
static void utf16_name(const u8 *be, char *out, size_t sz, int mode)
{
    bool for_file = mode != NAME_SCREEN;
    size_t o = 0;
    for (int i = 0; i < 10; i++) {
        unsigned c = be[i * 2] << 8 | be[i * 2 + 1];
        if (!c)
            break;
        char buf[4];
        int n = 0;
        if (c < 0x20 || (c >= 0xE000 && c <= 0xF8FF) || (c < 0x80 && strchr("\\/:*?\"<>|", (int)c)))
            buf[n++] = for_file ? '_' : '?';
        else if (c < 0x80)
            buf[n++] = (char)c;
        else if (mode == NAME_SCREEN)
            buf[n++] = '?';
        else if (mode == NAME_ASCII)
            buf[n++] = '_';
        else if (c < 0x800) {
            buf[n++] = (char)(0xC0 | c >> 6);
            buf[n++] = (char)(0x80 | (c & 0x3F));
        } else {
            buf[n++] = (char)(0xE0 | c >> 12);
            buf[n++] = (char)(0x80 | (c >> 6 & 0x3F));
            buf[n++] = (char)(0x80 | (c & 0x3F));
        }
        if (o + n >= sz)
            break;
        memcpy(out + o, buf, n);
        o += n;
    }
    /* FAT drops trailing spaces and dots from names */
    while (for_file && o && (out[o - 1] == ' ' || out[o - 1] == '.'))
        o--;
    out[o] = '\0';
}

static void free_list(void)
{
    free(miis);
    miis = NULL;
    nmiis = 0;
}

static bool load(const mii_db *which)
{
    free_list();
    db = which;
    const char *path = NULL;
    if (which == &WIIU) {
        for (int i = 0; i < 3 && !path; i++)
            if (vfs_exists(WIIU_PATHS[i]))
                path = WIIU_PATHS[i];
    } else {
        path = VWII_PATH;
    }
    vfs_file f;
    if (!path || !vfs_open_read(&f, path))
        return false;
    u8 *buf = malloc(which->file_size);
    s32 got = buf ? vfs_read(&f, buf, which->file_size) : -1;
    vfs_close(&f);
    if (got != which->file_size || memcmp(buf, which->magic, 4)) {
        free(buf);
        return false;
    }
    int used = 0;
    for (int i = 0; i < which->slots; i++) {
        const u8 *r = buf + which->first + i * which->rec_size;
        for (int k = 0; k < which->id_len; k++)
            if (r[which->id_off + k]) { used++; break; }
    }
    miis = calloc(used ? used : 1, sizeof(*miis));
    for (int i = 0; miis && i < which->slots; i++) {
        const u8 *r = buf + which->first + i * which->rec_size;
        bool empty = true;
        for (int k = 0; k < which->id_len; k++)
            if (r[which->id_off + k]) empty = false;
        if (empty)
            continue;
        mii_t *m = &miis[nmiis++];
        memcpy(m->data, r, which->rec_size);
        utf16_name(r + which->name_off, m->name, sizeof(m->name), NAME_SCREEN);
        utf16_name(r + which->name_off, m->file, sizeof(m->file), NAME_FILE);
        utf16_name(r + which->name_off, m->ascii, sizeof(m->ascii), NAME_ASCII);
        utf16_name(r + which->creator_off, m->creator, sizeof(m->creator), NAME_SCREEN);
        if (!m->file[0])
            snprintf(m->file, sizeof(m->file), "Mii %03d", i + 1);
        if (!strpbrk(m->ascii, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"))
            snprintf(m->ascii, sizeof(m->ascii), "Mii %03d", i + 1);
        if (!m->name[0])
            snprintf(m->name, sizeof(m->name), "(no name)");
    }
    free(buf);
    return miis != NULL;
}

static u16 crc16_xmodem(const u8 *p, int n)
{
    u16 crc = 0;
    while (n--) {
        crc ^= (u16)(*p++ << 8);
        for (int b = 0; b < 8; b++)
            crc = crc & 0x8000 ? (u16)(crc << 1 ^ 0x1021) : (u16)(crc << 1);
    }
    return crc;
}

static void swap16(u8 *p, int words)
{
    for (int i = 0; i < words; i++, p += 2) {
        u8 t = p[0];
        p[0] = p[1];
        p[1] = t;
    }
}

/* FFL keeps the record big-endian; .ffsd files are the 3DS / Wii U little-endian layout, as Mii Studio
 * loaders and mii-js read them (the same swaps jamesrhg/mii-transfer makes). */
static int to_ffsd(const u8 *rec, u8 out[96])
{
    memset(out, 0, 96);
    memcpy(out, rec, 0x5C);
    u8 t = out[0];
    out[0] = out[3];
    out[3] = t;
    t = out[1];
    out[1] = out[2];
    out[2] = t;
    swap16(out + 0x18, 11);
    swap16(out + 0x30, 12);
    swap16(out + 0x48, 10);
    u16 crc = crc16_xmodem(out, 0x5E);
    out[0x5E] = (u8)(crc >> 8);
    out[0x5F] = (u8)crc;
    return 96;
}

static int marked_count(void)
{
    int n = 0;
    for (int i = 0; i < nmiis; i++)
        n += miis[i].marked;
    return n;
}

static void draw(int cursor, int top, int rows, bool loaded)
{
    int cols = ui_cols();
    ui_clear();
    ui_bg(C_BLUE);
    ui_color(C_WHITE, true);
    ui_field(0, 0, cols - 1, " EXPORT MIIS  -  %s", db->title);
    ui_reset();
    ui_color(C_CYAN, true);
    if (loaded)
        ui_field(1, 1, cols - 2, "%s Miis: %d    Marked: %d", db->title, nmiis, marked_count());
    else
        ui_field(1, 1, cols - 2, "No %s Mii database found.", db->title);
    ui_reset();
    for (int i = 0; i < rows && top + i < nmiis; i++) {
        const mii_t *m = &miis[top + i];
        if (top + i == cursor) { ui_bg(C_CYAN); ui_color(C_BLACK, false); }
        else ui_color(C_WHITE, m->marked);
        ui_field(3 + i, 0, cols - 1, " %s %-12s  by %s", m->marked ? "[x]" : "[ ]", m->name, m->creator);
        ui_reset();
    }
    ui_color(C_CYAN, true);
    ui_field(ui_rows() - 2, 1, cols - 2, "A Mark   Y / 1 Mark all   R / + Export   L / - %s Miis   B Back",
             db == &WIIU ? "vWii" : "Wii U");
    ui_reset();
}

/* Mounted SD and USB drives; never the protected internal storage. */
static int pick_drive(void)
{
    const char *items[1 + USB_SLOTS];
    char labels[1 + USB_SLOTS][48];
    int devs[1 + USB_SLOTS], n = 0;
    for (int d = DEV_SD; d < DEV_USB0 + USB_SLOTS; d++) {
        if (!g_devs[d].mounted)
            continue;
        snprintf(labels[n], sizeof(labels[n]), "%s (%s)", g_devs[d].name, g_devs[d].root);
        items[n] = labels[n];
        devs[n++] = d;
    }
    if (!n) {
        ui_message("EXPORT MIIS", C_YELLOW, "No SD card or USB drive to save to.");
        return -1;
    }
    int r = ui_menu("SAVE MIIS TO", items, NULL, n, 0);
    return r < 0 ? -1 : devs[r];
}

/* "Name.ext", or "Name (2).ext" when that exists already. */
static bool unique_path(char *out, size_t sz, const char *dir, const char *base)
{
    for (int i = 1; i < 1000; i++) {
        char name[96];
        if (i == 1) snprintf(name, sizeof(name), "%s%s", base, db->ext);
        else        snprintf(name, sizeof(name), "%s (%d)%s", base, i, db->ext);
        if (!vfs_join(out, sz, dir, name))
            return false;
        if (!vfs_exists(out))
            return true;
    }
    return false;
}

static void export(int dev, int only)
{
    char dir[VFS_PATH_MAX], path[VFS_PATH_MAX], last[VFS_PATH_MAX] = "", failed[VFS_PATH_MAX] = "";
    vfs_join(dir, sizeof(dir), vfs_dev_root(dev), "savemiis");
    if (!vfs_exists(dir))
        vfs_mkdir(dir);
    int ok = 0, total = 0;
    for (int i = 0; i < nmiis; i++) {
        if (only >= 0 ? i != only : !miis[i].marked)
            continue;
        total++;
        u8 out[96];
        int len = db == &WIIU ? to_ffsd(miis[i].data, out) : db->rec_size;
        const u8 *src = db == &WIIU ? out : miis[i].data;
        vfs_file f;
        path[0] = '\0';
        bool good = unique_path(path, sizeof(path), dir, miis[i].file) && vfs_open_write(&f, path);
        /* The Wii U's own SD driver refuses some names FatFs takes; plain ASCII always works. */
        if (!good && strcmp(miis[i].file, miis[i].ascii))
            good = unique_path(path, sizeof(path), dir, miis[i].ascii) && vfs_open_write(&f, path);
        if (good) {
            good = vfs_write(&f, src, len) == len;
            good = vfs_close_checked(&f) && good;
            if (!good)
                vfs_remove(path, false);
        }
        if (good) {
            ok++;
            snprintf(last, sizeof(last), "%s", path);
        } else if (!failed[0]) {
            snprintf(failed, sizeof(failed), "%s", path[0] ? path : dir);
        }
    }
    char msg[VFS_PATH_MAX * 2 + 96];
    if (ok == total && total == 1)
        snprintf(msg, sizeof(msg), "Saved as %s", last);
    else if (ok == total)
        snprintf(msg, sizeof(msg), "Saved %d Miis to %s/", ok, dir);
    else
        snprintf(msg, sizeof(msg), "Saved %d of %d Miis to %s/\n\nCould not write:\n%s", ok, total, dir, failed);
    if (ok == total) {
        ui_message("DONE", C_GREEN, msg);
    } else {
        ui_alert_window(true);
        ui_message("FINISHED WITH ERRORS", C_RED, msg);
        ui_alert_window(false);
    }
}

void mii_export(void)
{
    ui_splash("Reading the Mii database...");
    bool loaded = load(&WIIU);
    int cursor = 0, top = 0, rows = ui_rows() - 6;
    for (;;) {
        if (cursor < top) top = cursor;
        if (cursor >= top + rows) top = cursor - rows + 1;
        draw(cursor, top, rows, loaded);
        u32 p = input_pressed();
        if (nmiis) {
            if (p & BTN_UP)    cursor = cursor ? cursor - 1 : nmiis - 1;
            if (p & BTN_DOWN)  cursor = (cursor + 1) % nmiis;
            if (p & BTN_LEFT)  cursor = cursor > rows ? cursor - rows : 0;
            if (p & BTN_RIGHT) cursor = cursor + rows < nmiis ? cursor + rows : nmiis - 1;
            if (p & BTN_A)     miis[cursor].marked = !miis[cursor].marked;
            if (p & BTN_1) {
                bool all = marked_count() == nmiis;
                for (int i = 0; i < nmiis; i++)
                    miis[i].marked = !all;
            }
            if (p & BTN_PLUS) {
                int dev = pick_drive();
                if (dev >= 0)
                    export(dev, marked_count() ? -1 : cursor);
            }
        }
        if (p & BTN_MINUS) {
            ui_splash("Reading the Mii database...");
            loaded = load(db == &WIIU ? &VWII : &WIIU);
            cursor = top = 0;
        }
        if (p & (BTN_B | BTN_HOME))
            break;
    }
    free_list();
}
