#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>

#include "app.h"
#include "ui.h"
#include "input.h"
#include "pointer.h"
#include "pda.h"
#include "sfx.h"
#include "wiilight.h"
#include "colors.h"
#include "alarm.h"
#include "calc.h"
#include "clock.h"
#include "vfs.h"
#include "fat32fmt.h"
#include "filetools.h"
#include "zipwrite.h"
#include "ops.h"
#include "waddump.h"
#include "wadinstall.h"
#include "databin.h"
#include "editor.h"
#include "risk.h"
#include "irremote.h"
#include "mii.h"
#include "miiu.h"
#include "wfskeys.h"
#include "ftp.h"
#include "mii.h"
#include "arcfs.h"
#include "archive.h"
#include "saves.h"
#include "media.h"
#include "ftype.h"
#include "wiiload.h"
#include "wiiu_install.h"
#include "wiiufmt.h"
#include "usage.h"
#include "update.h"
#include "sweeper.h"

volatile bool g_quit_request;
volatile bool g_power_request;

/* ---------------------------------------------------------------- state */

static char cwd[VFS_PATH_MAX];       /* "" = drive list */
static vfs_entry *ents;
static bool *marks;
static int nents, nmarks;
static int cursor, scroll;
static int home_cursor, home_scroll;

static char **clip;
static int clip_n;
static bool clip_cut;

static char status_msg[96];
static int status_color = C_WHITE;
static int exit_req = EXIT_LOADER;

static void status(int color, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void status(int color, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(status_msg, sizeof(status_msg), fmt, ap);
    va_end(ap);
    status_color = color;
}

static int list_rows(void) { return ui_rows() - 8; }

/* Set for a plain cursor move: the screen is the same one, so rows are rewritten in place
 * instead of cleared first, which made the header bar blink. */
static bool cursor_only;

/* ---------------------------------------------------------------- clipboard */

static void clip_clear(void)
{
    for (int i = 0; i < clip_n; i++)
        free(clip[i]);
    free(clip);
    clip = NULL;
    clip_n = 0;
}

/* Paths of the marked entries, or the entry under the cursor. */
static char **targets(int *count)
{
    int off = clip_n ? 1 : 0;
    int n = nmarks ? nmarks : ((cursor - off >= 0 && cursor - off < nents) ? 1 : 0);
    *count = n;
    if (!n)
        return NULL;
    char **out = calloc(n, sizeof(char *));
    int k = 0;
    for (int i = 0; i < nents && k < n; i++) {
        if (nmarks ? marks[i] : (i == cursor - off)) {
            out[k] = malloc(VFS_PATH_MAX);
            vfs_join(out[k], VFS_PATH_MAX, cwd, ents[i].name);
            k++;
        }
    }
    return out;
}

static void free_paths(char **p, int n)
{
    for (int i = 0; i < n; i++)
        free(p[i]);
    free(p);
}

/* ---------------------------------------------------------------- directory */

static void free_dir(void)
{
    free(ents);
    free(marks);
    ents = NULL;
    marks = NULL;
    nents = nmarks = 0;
}

static bool load_dir(const char *path, const char *select_name)
{
    vfs_entry *list;
    int n;
    ui_busy_begin("Loading...");
    bool listed = vfs_list_visible(path, &list, &n);
    ui_busy_end();
    if (!listed) {
        if (vfs_is_archive(path)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "%s.", arcfs_error());
            ui_message("CAN'T OPEN", C_RED, msg);
        } else
            ui_message("ERROR", C_RED,
                       "Cannot open this location.\nThe drive may have been removed - try \"Remount drives\" in the HOME menu.");
        return false;
    }
    free_dir();
    ents = list;
    nents = n;
    marks = calloc(n ? n : 1, sizeof(bool));
    snprintf(cwd, sizeof(cwd), "%s", path);
    cursor = scroll = 0;
    int off = clip_n ? 1 : 0;
    if (select_name)
        for (int i = 0; i < n; i++)
            if (!strcmp(ents[i].name, select_name)) {
                cursor = i + off;
                break;
            }
    return true;
}

static void reload_keep(void)
{
    int c = cursor, s = scroll;
    char keep[VFS_PATH_MAX];
    snprintf(keep, sizeof(keep), "%s", cwd);
    if (load_dir(keep, NULL)) {
        int total = nents + (clip_n ? 1 : 0);
        cursor = c < total ? c : (total ? total - 1 : 0);
        scroll = s;
    }
}

/* ---------------------------------------------------------------- drawing */

/* The drive list warns while any drive is unlocked; inside a drive, only that drive counts. */
static bool here_risky(void)
{
    if (!cwd[0])
        return risk_any_unlocked();
    int dev = vfs_dev_of(cwd);
    return vfs_is_protected(dev) && risk_dev_unlocked(dev);
}

/* The console's clock, "HH:MM" (Moonshell puts one in its top corner too). */
static int clock_minute(char *out, size_t n)
{
    time_t t = time(NULL);
    struct tm tmv;
    if (t < 1000000000 || !localtime_r(&t, &tmv)) {     /* the clock isn't set */
        snprintf(out, n, "--:--");
        return -1;
    }
    snprintf(out, n, "%d:%02d %s", tmv.tm_hour % 12 ? tmv.tm_hour % 12 : 12, tmv.tm_min,
             tmv.tm_hour < 12 ? "AM" : "PM");
    return tmv.tm_hour * 60 + tmv.tm_min;
}

/* Write permission levels, the GodMode9 way, shown by the colour of the tag on the right (the bar keeps the
 * theme's colour):  green  SAFE: nothing internal can change;  yellow  UNLOCKED: an internal drive can;
 * magenta  CONSOLE DATA: unlocked, and here is data only this console has (SLC, SEEPROM, keys, the vWii NAND's
 * /sys and /shared2/sys);  red  FORCED: Risky Mode always on, nothing asks first. */
static void level_tag(char *out, size_t n, int *color)
{
    int dev = cwd[0] ? vfs_dev_of(cwd) : -1;
    bool unique = dev == DEV_SLC || dev == DEV_SEEPROM || dev == DEV_KEYS ||
                  (dev == DEV_NAND && (strstr(cwd, ":/shared2/sys") || strstr(cwd, ":/sys")));
    if (risk_forced())                     { snprintf(out, n, "[ FORCED ]");       *color = C_RED; }
    else if (!risk_any_unlocked())         { snprintf(out, n, "[ SAFE ]");         *color = C_GREEN; }
    else if (unique && here_risky())       { snprintf(out, n, "[ CONSOLE DATA ]"); *color = C_MAGENTA; }
    else                                   { snprintf(out, n, "[ UNLOCKED ]");     *color = C_YELLOW; }
}

static void draw_header(void)
{
    int cols = ui_cols();
    char left[64], tag[24], clk[12];
    int tc;
    clock_minute(clk, sizeof(clk));
    level_tag(tag, sizeof(tag), &tc);
    snprintf(left, sizeof(left), " GodModeU v%s", GMW_VERSION);
    int rlen = (int)strlen(tag) + 2 + (int)strlen(clk) + 1;
    int gap = cols - 1 - (int)strlen(left) - rlen;
    if (gap < 1) gap = 1;
    ui_bg(C_BLUE);
    ui_color(C_WHITE, true);
    ui_field(0, 0, cols - 1, "%s%*s%*s  %s ", left, gap, "", (int)strlen(tag), "", clk);
    ui_color(tc, true);
    ui_field(0, cols - 1 - rlen, (int)strlen(tag), "%s", tag);
    ui_reset();
}

static void draw_space_line(int row, int dev, bool tag)
{
    int cols = ui_cols();
    vfs_dev *d = &g_devs[dev];
    ui_reset();
    ui_field(row, 0, cols - 1, "");
    if (tag) {
        ui_color(C_CYAN, true);
        ui_field(row, 1, 6, "%s", d->tag);
    }
    if (!d->space_known) {
        ui_color(C_WHITE, false);
        ui_field(row, 8, cols - 10, "free space unknown");
        ui_reset();
        return;
    }
    char f[24], t[24];
    ui_fmt_size(f, sizeof(f), d->free);
    ui_fmt_size(t, sizeof(t), d->total);
    int used = d->total ? (int)((d->total - d->free) * 100 / d->total) : 0;
    int bw = 22;
    ui_bar(row, 8, bw, used, used < 75 ? C_GREEN : (used < 90 ? C_YELLOW : C_RED));
    ui_color(C_WHITE, true);
    ui_field(row, 8 + bw + 1, cols - 10 - bw, "%s free of %s", f, t);
    ui_reset();
}

static void draw_browser(void)
{
    int cols = ui_cols(), rows = ui_rows();
    int L = list_rows();
    int off = clip_n ? 1 : 0;
    int total = nents + off;

    if (!cursor_only)
        ui_clear();
    cursor_only = false;
    draw_header();

    ui_color(C_CYAN, true);
    /* the path as folders: "a.bin|/x" (the '|' marks where an archive is entered) shows as "a.bin/x" */
    char shown[VFS_PATH_MAX];
    vfs_shown_path(cwd, shown, sizeof(shown));
    int pl = strlen(shown);
    if (pl > cols - 3)
        ui_field(1, 1, cols - 2, "...%s", shown + pl - (cols - 6));
    else
        ui_field(1, 1, cols - 2, "%s", shown);
    ui_reset();
    ui_color(C_WHITE, false);
    ui_fill(2, 0, cols - 1, '-');
    ui_reset();

    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + L) scroll = cursor - L + 1;

    int nw = cols - 1 - 2 - 12;
    for (int i = 0; i < L; i++) {
        int idx = scroll + i;
        int row = 3 + i;
        bool flag = ui_pride_stripe(i, L);       /* the Pride theme paints the flag behind the rows */
        if (idx >= total) {
            ui_field(row, 0, cols - 1, "");
            if (total == 0 && i == 0) {
                if (!flag) ui_color(C_WHITE, false);
                ui_field(row, 2, cols - 3, "(empty)");
            }
            ui_reset();
            continue;
        }
        bool sel = idx == cursor;
        if (sel) {
            ui_bg(flag ? C_WHITE : C_CYAN);
            ui_color(C_BLACK, false);
        }

        if (off && idx == 0) {
            if (!sel && !flag) ui_color(C_GREEN, true);
            ui_field(row, 0, cols - 1, "> Paste %d item(s) here  [%s]", clip_n,
                     clip_cut ? "MOVE" : "COPY");
            ui_reset();
            continue;
        }
        int e = idx - off;
        vfs_entry *en = &ents[e];
        if (!sel && !flag) {
            if (marks[e]) ui_color(C_GREEN, true);
            else if (en->is_dir) ui_color(C_YELLOW, true);
            else ui_color(C_WHITE, true);
        }
        char sz[24];
        if (en->is_dir) snprintf(sz, sizeof(sz), "<DIR>");
        else ui_fmt_size(sz, sizeof(sz), en->size);
        char shown[256];
        ui_ascii(shown, sizeof(shown), en->name);
        ui_field(row, 0, cols - 1, "%c %-*.*s %11s", marks[e] ? '*' : ' ', nw, nw, shown, sz);
        ui_reset();
    }

    ui_color(C_WHITE, false);
    ui_fill(rows - 5, 0, cols - 1, '-');
    ui_reset();

    int dev = vfs_dev_of(cwd);
    if (dev >= 0 && vfs_is_card(dev)) {
        ui_reset();
        ui_field(rows - 4, 0, cols - 1, "");
        ui_color(C_CYAN, true);
        ui_field(rows - 4, 1, 6, "%s", g_devs[dev].tag);
        ui_color(C_WHITE, false);
        ui_field(rows - 4, 8, cols - 10, "memory card - paste .gci saves here to restore them");
        ui_reset();
    } else if (dev >= 0 && vfs_is_archive(cwd)) {
        ui_reset();
        ui_field(rows - 4, 0, cols - 1, "");
        ui_color(C_CYAN, true);
        bool wfs = arcfs_dev_pinned(dev);
        bool wiiu = wfs && !strcmp(arcfs_pin_kind(dev), "WFS");
        ui_field(rows - 4, 1, 6, "%s", wiiu ? "WFS" : wfs ? "DISK" : "ARC");
        ui_color(C_WHITE, false);
        ui_field(rows - 4, 8, cols - 10, wiiu ? "Wii U WFS volume (read-only) - copy files out to SD/USB"
                                              : wfs ? "drive read as a disk (read-only) - copy files out to SD/USB"
                                                    : "inside an archive (read-only) - copy files out to SD/USB");
        ui_reset();
    } else if (dev >= 0 && vfs_is_virtual(dev)) {
        ui_reset();
        ui_field(rows - 4, 0, cols - 1, "");
        ui_color(C_CYAN, true);
        ui_field(rows - 4, 1, 6, "%s", g_devs[dev].tag);
        ui_color(C_WHITE, false);
        ui_field(rows - 4, 8, cols - 10, "read-only virtual drive - copy files out to SD/USB");
        ui_reset();
    } else if (dev >= 0) {
        draw_space_line(rows - 4, dev, true);
    }

    /* status line: marks / clipboard / last message */
    ui_reset();
    ui_field(rows - 3, 0, cols - 1, "");
    int col = 1;
    if (nmarks) {
        ui_color(C_GREEN, true);
        ui_field(rows - 3, col, 14, "%d marked", nmarks);
        col += 14;
    }
    if (clip_n) {
        ui_color(clip_cut ? C_RED : C_GREEN, true);
        ui_field(rows - 3, col, 20, "Clip: %s %d", clip_cut ? "CUT" : "COPY", clip_n);
        col += 20;
    }
    if (status_msg[0]) {
        ui_color(status_color, true);
        ui_field(rows - 3, col, cols - 1 - col, "%s", status_msg);
    }
    ui_reset();

    ui_color(C_CYAN, true);
    if (off && cursor == 0)
        ui_field(rows - 2, 1, cols - 2, "A Paste  B Back  Y/1 Clear clipboard  X/2 Menu  R/+ Copy  L/- Cut");
    else
        ui_field(rows - 2, 1, cols - 2, "A Open  B Back  Y/1 Mark  X/2 Menu  R/+ Copy  L/- Cut");
    ui_color(C_WHITE, false);
    ui_field(rows - 1, 1, cols - 2, "HOME System menu    D-Pad L/R Page");
    ui_reset();
}

/* Drive rows that fit on the home screen. */
static int home_rows(void)
{
    int vis = ui_rows() - 7;
    return vis < 1 ? 1 : vis;
}

/* Drives shown on the home screen. */
static int home_list(int *out)
{
    int n = 0;
    for (int i = 0; i < DEV_COUNT; i++)
        if (g_devs[i].always_show || g_devs[i].mounted)
            out[n++] = i;
    return n;
}

static void draw_home(void)
{
    int cols = ui_cols(), rows = ui_rows();
    int devs[DEV_COUNT];
    int n = home_list(devs);
    int vis = home_rows();
    if (home_cursor >= n) home_cursor = n - 1;
    if (home_cursor < home_scroll) home_scroll = home_cursor;
    if (home_cursor >= home_scroll + vis) home_scroll = home_cursor - vis + 1;

    if (!cursor_only)
        ui_clear();
    cursor_only = false;
    draw_header();
    ui_color(C_CYAN, true);
    ui_field(1, 1, cols - 2, "Select a drive");
    ui_color(C_WHITE, false);
    ui_fill(2, 0, cols - 1, '-');
    ui_reset();

    for (int i = 0; i < vis; i++) {
        int idx = home_scroll + i;
        int row = 3 + i;
        bool flag = ui_pride_stripe(i, vis);     /* the Pride theme paints the flag behind the rows */
        if (idx >= n) {
            if (flag) ui_field(row, 0, cols - 1, "");
            ui_reset();
            continue;
        }
        vfs_dev *d = &g_devs[devs[idx]];
        bool sel = idx == home_cursor;
        if (sel) {
            ui_bg(flag ? C_WHITE : C_CYAN);
            ui_color(C_BLACK, false);
        } else if (!flag)
            ui_color(d->mounted ? C_WHITE : C_RED, d->mounted);
        char note[56] = "";
        if (d->mounted && vfs_is_protected(devs[idx]))
            snprintf(note, sizeof(note), "%s",
                     risk_dev_unlocked(devs[idx]) ? "UNLOCKED - BRICK RISK" : "protected (Konami code to edit)");
        else if (d->note[0])
            snprintf(note, sizeof(note), "%s", d->note);
        else if (!d->mounted)
            snprintf(note, sizeof(note), "not detected");
        else if (vfs_is_virtual(devs[idx]))
            snprintf(note, sizeof(note), "%s", d->note);
        else if (d->exfat)
            snprintf(note, sizeof(note), "exFAT");
        else if (d->ntfs)
            snprintf(note, sizeof(note), "NTFS");
        else if (d->ext)
            snprintf(note, sizeof(note), "ext%d  read-only", d->ext);
        ui_field(row, 0, cols - 1, " [%-4s]  %-20s %-10s %s", d->tag, d->name, d->root, note);
        ui_reset();
    }

    if (n > vis) {
        ui_color(C_WHITE, false);
        ui_field(rows - 4, 1, cols - 2, "%d/%d", home_cursor + 1, n);
    }
    ui_color(C_CYAN, true);
    ui_field(rows - 2, 1, cols - 2, n > vis ? "A Open drive  D-Pad L/R Page  HOME System menu"
                                            : "A Open drive  HOME System menu");
    ui_color(status_color, true);
    ui_field(rows - 3, 1, cols - 2, "%s", status_msg);
    ui_reset();
}

/* ---------------------------------------------------------------- viewers */

static void hexview(const char *path)
{
    vfs_file f;
    if (!vfs_open_read(&f, path)) {
        ui_message("ERROR", C_RED, "Cannot open the file for reading.");
        return;
    }
    int cols = ui_cols(), rows = ui_rows();
    int L = rows - 5;
    int per = 16;
    u8 *buf = memalign(32, (size_t)L * per + 32);
    u64 off = 0;
    bool redraw = true;
    int got = 0;

    for (;;) {
        if (redraw) {
            vfs_seek(&f, off);
            got = vfs_read(&f, buf, L * per);
            if (got < 0) got = 0;

            ui_clear();
            ui_bg(C_BLUE);
            ui_color(C_WHITE, true);
            ui_field(0, 0, cols - 1, " HEX  %s", vfs_basename(path));

            ui_reset();
            ui_color(C_WHITE, false);
            ui_fill(1, 0, cols - 1, '-');

            for (int r = 0; r < L; r++) {
                int base = r * per;
                if (base >= got) break;
                ui_goto(2 + r, 1);
                ui_color(C_CYAN, true);
                printf("%08X ", (unsigned)(off + base));
                ui_color(C_WHITE, false);
                for (int c = 0; c < per; c++) {
                    if (base + c < got) printf("%02X ", buf[base + c]);
                    else printf("   ");
                }
                ui_color(C_YELLOW, false);
                for (int c = 0; c < per && base + c < got; c++) {
                    u8 b = buf[base + c];
                    putchar(b >= 32 && b < 127 ? b : '.');
                }
            }
            ui_reset();
            char tsz[24];
            ui_fmt_size(tsz, sizeof(tsz), f.size);
            ui_color(C_CYAN, true);
            ui_field(rows - 3, 1, cols - 2, "Offset %08X / %s", (unsigned)off, tsz);
            ui_field(rows - 2, 1, cols - 2, "U/D Line  L/R Page  +/- x16 pages  B Close");
            ui_reset();
            redraw = false;
        }

        u32 p = input_pressed();
        u64 page = (u64)L * per;
        u64 last = f.size > page ? ((f.size - 1) / per * per) - (page - per) : 0;
        if (f.size <= page) last = 0;

        u64 o = off;
        if (p & WPAD_BUTTON_DOWN) o += per;
        if (p & WPAD_BUTTON_UP)   o = o >= per ? o - per : 0;
        if (p & WPAD_BUTTON_RIGHT) o += page;
        if (p & WPAD_BUTTON_LEFT)  o = o >= page ? o - page : 0;
        if (p & WPAD_BUTTON_PLUS)  o += page * 16;
        if (p & WPAD_BUTTON_MINUS) o = o >= page * 16 ? o - page * 16 : 0;

        if (o > last) o = last;
        if (o != off) {
            off = o;
            redraw = true;
        }

        if (p & (WPAD_BUTTON_B | WPAD_BUTTON_HOME | WPAD_BUTTON_A))
            break;
    }
    free(buf);
    vfs_close(&f);
}

/* Plain text viewer: loads the first MiB, wraps it to the screen width and
 * scrolls by line. Tabs are expanded; ui_field folds UTF-8 to ASCII. */
#define TEXT_MAX_BYTES (1024 * 1024)
#define TEXT_MAX_LINES 200000

void text_show(const char *title, u8 *buf, u32 got, u64 total, bool truncated);

static void textview(const char *path)
{
    vfs_file f;
    if (!vfs_open_read(&f, path)) {
        ui_message("ERROR", C_RED, "Cannot open the file for reading.");
        return;
    }
    u32 want = f.size < TEXT_MAX_BYTES ? (u32)f.size : TEXT_MAX_BYTES;
    u8 *buf = memalign(32, want + 32);
    u32 got = 0;
    while (buf && got < want) {
        u32 n = want - got > 32768 ? 32768 : want - got;
        s32 r = vfs_read(&f, buf + got, n);
        if (r <= 0)
            break;
        got += r;
    }
    bool truncated = f.size > got;
    vfs_close(&f);
    if (!buf) {
        ui_message("ERROR", C_RED, "Out of memory.");
        return;
    }

    /* Warn before dumping a binary file on the screen. */
    u32 probe = got < 1024 ? got : 1024, odd = 0;
    for (u32 i = 0; i < probe; i++)
        if (buf[i] == 0 || (buf[i] < 32 && buf[i] != '\n' && buf[i] != '\r' && buf[i] != '\t'))
            odd++;
    if (probe && odd * 10 > probe &&
        !ui_confirm("BINARY FILE", C_YELLOW,
                    "This doesn't look like text. Open it as text anyway?\n\nThe hex viewer suits it better.")) {
        free(buf);
        return;
    }

    text_show(vfs_basename(path), buf, got, f.size, truncated);
}

/* Shows (and frees) a text buffer: also used by the BMG viewer. */
void text_show(const char *title, u8 *buf, u32 got, u64 total, bool truncated)
{
    int cols = ui_cols(), rows = ui_rows();
    int width = cols - 2;
    int L = rows - 5;

    u32 cap = 1024, nl = 0;
    u32 *lines = malloc(cap * sizeof(u32));
    if (!lines) {
        free(buf);
        return;
    }
    lines[nl++] = 0;

    int col = 0;

    for (u32 i = 0; i < got && nl < TEXT_MAX_LINES; i++) {
        u8 c = buf[i];
        if (nl + 2 >= cap) {
            cap *= 2;
            u32 *nlines = realloc(lines, cap * sizeof(u32));
            if (!nlines) break;
            lines = nlines;
        }
        if (c == '\n') {
            lines[nl++] = i + 1;
            col = 0;
        } else if (c != '\r') {
            int w = c == '\t' ? 4 - (col % 4) : 1;
            if (col + w > width) {
                lines[nl++] = i;
                col = 0;
                w = c == '\t' ? 4 : 1;
            }
            col += w;
        }
    }
    lines[nl] = got;       /* sentinel (cap always leaves room) */

    u32 top = 0;
    bool redraw = true;
    char row_text[256];
    for (;;) {
        if (redraw) {

            ui_clear();
            ui_bg(C_BLUE);
            ui_color(C_WHITE, true);
            ui_field(0, 0, cols - 1, " TEXT  %s", title);
            ui_reset();
            ui_color(C_WHITE, false);
            ui_fill(1, 0, cols - 1, '-');
            ui_color(C_WHITE, true);

            for (int r = 0; r < L; r++) {
                u32 li = top + r;
                if (li >= nl) break;
                u32 s0 = lines[li], e0 = (li + 1 < nl) ? lines[li + 1] : got;
                int k = 0, cc = 0;
                for (u32 i = s0; i < e0 && k < (int)sizeof(row_text) - 5; i++) {
                    u8 c = buf[i];
                    if (c == '\n' || c == '\r') continue;
                    if (c == '\t') {
                        int w = 4 - (cc % 4);
                        while (w-- && k < (int)sizeof(row_text) - 5) {
                            row_text[k++] = ' ';
                            cc++;
                        }
                    } else {
                        row_text[k++] = (c >= 32 && c != 127) ? (char)c : '.';
                        if ((c & 0xC0) != 0x80)
                            cc++;
                    }
                }
                row_text[k] = '\0';
                ui_field(2 + r, 1, width, "%s", row_text);
            }
            ui_reset();

            int pct = nl > 1 ? (int)((u64)(top + 1) * 100 / nl) : 100;
            char tsz[24];
            ui_fmt_size(tsz, sizeof(tsz), total);
            ui_color(C_CYAN, true);
            ui_field(rows - 3, 1, cols - 2, "Line %u / %u  (%d%%)   %s%s", (unsigned)(top + 1),
                     (unsigned)nl, pct, tsz, truncated ? "  - first 1 MiB shown" : "");
            ui_field(rows - 2, 1, cols - 2, "U/D Line  L/R Page  +/- x10 pages  B Close");
            ui_reset();
            redraw = false;
            UI_TEST_SHOT();
        }

        u32 p = input_pressed();
        u32 t = top, last = nl > (u32)L ? nl - L : 0;
        if (p & WPAD_BUTTON_DOWN)  t += 1;
        if (p & WPAD_BUTTON_UP)    t = t ? t - 1 : 0;
        if (p & WPAD_BUTTON_RIGHT) t += L - 1;
        if (p & WPAD_BUTTON_LEFT)  t = t > (u32)(L - 1) ? t - (L - 1) : 0;
        if (p & WPAD_BUTTON_PLUS)  t += (L - 1) * 10;
        if (p & WPAD_BUTTON_MINUS) t = t > (u32)((L - 1) * 10) ? t - (L - 1) * 10 : 0;

        if (t > last) t = last;
        if (t != top) {
            top = t;
            redraw = true;
        }
        if (p & (WPAD_BUTTON_B | WPAD_BUTTON_HOME | WPAD_BUTTON_A))
            break;
    }

    free(lines);
    free(buf);
}

static void show_info(char **paths, int n)
{
    u64 bytes = 0;
    u32 files = 0, dirs = 0;
    for (int i = 0; i < n; i++)
        ops_tree_info(paths[i], &bytes, &files, &dirs);
    char sz[32], msg[512];
    ui_fmt_size(sz, sizeof(sz), bytes);
    if (n == 1)
        snprintf(msg, sizeof(msg), "%s\n\n%u file(s), %u folder(s)\nTotal: %s (%llu bytes)",
                 paths[0], (unsigned)files, (unsigned)dirs, sz, (unsigned long long)bytes);
    else
        snprintf(msg, sizeof(msg),
                 "%d items selected\n\n%u file(s), %u folder(s)\nTotal: %s (%llu bytes)", n,
                 (unsigned)files, (unsigned)dirs, sz, (unsigned long long)bytes);
    ui_message("INFO", C_CYAN, msg);
}

static void show_credits(void)
{
    static const char *const pages[] = {
        "quatric & cmyksoda\n\n"
        "GodModeU " GMW_VERSION " - a GodMode9-style file manager for the Wii U, the Wii U port of GodModeWii.\n"
        "Inspired by GodMode9 (d0k3), GodMode9i (DS-Homebrew), ftpii and WiiXplorer (Dimok, niuus).\n"
        "Built with devkitPPC and wut (devkitPro). GPL-3.0-or-later. See README.",

        "Controllers: libwiiuinput (quatric), the Wii U port of libwiiinput, after snes9xgx's Wii U driver "
        "(Daryl Borth) and RiftWii's GameCube adapter code.\n\n"
        "Wii U titles: WUP installing as WUP Installer GX2 (Dimok, FIX94, Yardape8000) does it through MCP; WUD / WUX "
        "discs read as wud2app (FIX94) and JWUDTool (Maschell) lay them out.",

        "Audio and texture formats match quatric's nintoolbox and mobipeg: GX2 textures (GTX, BFLIM, BFFNT, BFRES) "
        "deswizzled after AddrLib's tiling, as decaf-emu and nintoolbox read them.\n\n"
        "Mobiclip, THP and Flipnote playback: quatric's wii-mobiclip-player (MIT), whose Mobiclip decoder is "
        "ported from FFmpeg (Florian Nouwt, Adib Surani, Paul B Mahol; LGPL 2.1+). JPEG frames: NanoJPEG (Martin J. Fiedler). Vorbis: Tremor.",

        "RSEQ sequences: layouts from VGMTrans. WAV, AIFF, MP3 and FLAC: dr_libs (David Reid). zip, 7z and RAR 1.5 - 4: unarr (selmf, LZMA SDK by "
        "Igor Pavlov); RAR5 from the public format description.\n\n"
        "RVZ / WIA: written from Dolphin's format description; Zstandard (Meta), bzip2. LZO1X and Retro Studios PAK layouts match nintoolbox; "
        "lzop by Markus Oberhumer. WFS: wfslib and wfs-tools (koolkdev).",

        "IR remote: WiiIR, SingIR and OptimizeIR (Dakota Thorpe) through libwiiuir, out of the GamePad's IR LED; "
        "Flipper-IRDB's remotes. Raw drive and disc access: libmocha (Maschell). WBFS: libwbfs (Kwiirk, Waninkoko). "
        "Development console WADs: the dev common key, after RVT2RVL. Title info: after ShowMiiWads (Leathl) and NUSD. "
        "Write permission colours: GodMode9. Zip: zlib.\n\n"
        "Full list with licenses: README.",

        "The interface sounds are MoonShell 2.10's (Moonlight, GPL v2 or later), and the clock, calendar, "
        "calculator and alarm follow its own.\n\n"
        "Thanks for using GodModeU!\n\n"
        "Psst... feeling nostalgic? Right here, or on the drive list, enter + A - Y X - (on a Wii Remote, + A - 1 2 -).",
    };
    /* the easter egg: + A - 1 2 - on the last page switches TiVo mode
     * (on the GamePad and Pro Controller, 1 is Y and 2 is X) */
    static const u32 code[] = { WPAD_BUTTON_PLUS, WPAD_BUTTON_A, WPAD_BUTTON_MINUS, WPAD_BUTTON_1,
                                WPAD_BUTTON_2, WPAD_BUTTON_MINUS };
    int n = sizeof(pages) / sizeof(pages[0]);
    if (ui_message_pages_code("ABOUT / CREDITS", C_CYAN, pages, n, code, sizeof(code) / sizeof(code[0]))) {
        bool saved = sfx_tivo_toggle();
        ui_tivo_dialog(sfx_tivo(), saved, sfx_enabled());
    }
}

/* TiVo mode, as a TiVo does: the cursor stops at the top and bottom of a list (and paging stops at the
 * ends) with a bonk instead of the tick, where MoonShell mode wraps round. True when it bonked. */
static bool tivo_bonk(u32 p, int cursor, int total)
{
    if (!sfx_tivo() || !(p & (WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT))) return false;
    bool top = cursor <= 0, end = cursor >= total - 1;
    if (!(((p & (WPAD_BUTTON_UP | WPAD_BUTTON_LEFT)) && top) || ((p & (WPAD_BUTTON_DOWN | WPAD_BUTTON_RIGHT)) && end)))
        return false;
    sfx_cancel();                                   /* not the tick or page sound that went with the press */
    sfx_play(SFX_BONK);
    return true;
}

/* ---------------------------------------------------------------- actions */

static void do_copy_cut(bool cut)
{
    int n;
    char **t = targets(&n);
    if (!n) return;
    /* Cutting from protected storage removes files there: ask for that drive's code now. */
    int src_dev = vfs_dev_of(t[0]);
    if (cut && vfs_is_protected(src_dev) && !risk_ensure_unlocked(src_dev)) {
        free_paths(t, n);
        return;
    }
    clip_clear();
    clip = t;
    clip_n = n;
    clip_cut = cut;
    for (int i = 0; i < nents; i++) marks[i] = false;
    nmarks = 0;
    if (cursor == 0) cursor = 0;
    status(cut ? C_RED : C_GREEN,
           "%d item(s) in clipboard - open the target and press A on the Paste row", n);
}

static void do_clip_clear(void)
{
    clip_clear();
    if (cursor > 0) cursor--;
    if (scroll > 0) scroll--;
    status(C_GREEN, "Clipboard cleared.");
}

static void do_paste(void)
{
    if (!clip_n || !cwd[0]) return;
    if (vfs_is_readonly(cwd)) {
        ui_message("READ-ONLY", C_YELLOW, "You can't paste here: this location is read-only.");
        return;
    }
    bool ok = ops_paste(clip, clip_n, clip_cut, cwd);
    if (ok)
        clip_clear();
    reload_keep();
    status_msg[0] = 0;
}

/* ---------------------------------------------------------------- easter eggs */

/* A file or folder named "pride" (in any case) unlocks the Pride theme for good. */
static void pride_check(const char *name)
{
    if (strcasecmp(name, "pride")) return;
    bool first = !colors_pride_unlocked();
    bool saved = colors_unlock_pride();
    if (!first && colors_current() == colors_pride()) return;
    char msg[400];
    snprintf(msg, sizeof(msg), "%s%s\n\nSwitch to it now?",
             first ? "Happy Pride! You've unlocked the Pride theme: the colours of the rainbow flag (with the trans flag's "
                     "pink and light blue), in the HOME menu's Theme list from now on."
                   : "Happy Pride! The Pride theme is waiting in the HOME menu's Theme list.",
             saved ? "" : "\n\n(It couldn't be saved on the SD card, so it's here until GodModeU exits.)");
    if (ui_confirm("HAPPY PRIDE!", C_MAGENTA, msg))
        colors_apply(colors_pride(), true);
}

/* On the game disc, or inside a disc image: nothing there can be deleted, as the Wii Menu would put it. */
static bool on_game_disc(const char *path)
{
    if (vfs_dev_of(path) == DEV_DISC) return true;
    if (!vfs_is_archive(path)) return false;
    char outer[VFS_PATH_MAX];
    snprintf(outer, sizeof(outer), "%s", path);
    char *bar = strstr(outer, "|/");
    if (bar) *bar = '\0';
    static const char *const exts[] = { ".iso", ".rvz", ".wbfs", ".wia", ".gcm", ".ciso", ".gcz", ".wdf", ".wud", ".wux" };
    for (unsigned i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
        if (arc_name_has_ext(outer, exts[i])) return true;
    return false;
}

static void game_disc_message(void)
{
    ui_message("", C_CYAN, "The Game Disc could not be deleted. Please refer to the Wii U Operations Manual for more information.");
}

static void do_delete(void)
{
    int n;
    char **t = targets(&n);
    if (!n) return;
    char msg[300];
    if (n == 1)
        snprintf(msg, sizeof(msg), "Delete \"%s\"?\n\nThis cannot be undone.", vfs_basename(t[0]));
    else
        snprintf(msg, sizeof(msg), "Delete %d items?\n\nThis cannot be undone.", n);
    if (ui_confirm("DELETE", C_RED, msg)) {
        ops_delete(t, n);
        reload_keep();
    }
    free_paths(t, n);
}

static void do_rename(void)
{
    int off = clip_n ? 1 : 0;
    int e = cursor - off;
    if (e < 0 || e >= nents) return;
    char old[VFS_PATH_MAX];
    vfs_join(old, sizeof(old), cwd, ents[e].name);
    if (!risk_authorize(old, "rename"))
        return;
    int dev = vfs_dev_of(cwd);
    char name[VFS_NAME_MAX];
    snprintf(name, sizeof(name), "%s", ents[e].name);
    if (!ui_text_input("RENAME", name, dev == DEV_NAND ? NAND_NAME_MAX : 120))
        return;
    char dst[VFS_PATH_MAX];
    vfs_join(dst, sizeof(dst), cwd, name);
    if (vfs_exists(dst))
        ui_message("ERROR", C_RED, "Something with that name already exists.");
    else if (!vfs_rename(old, dst))
        ui_message("ERROR", C_RED, g_write_err ? g_write_err : "Rename failed.");
    else {
        status(C_GREEN, "Renamed.");
        pride_check(name);
    }
    load_dir(cwd, name);
}

static void do_mkdir(void)
{
    if (!risk_authorize(cwd, "create a folder"))
        return;
    char name[VFS_NAME_MAX] = "";
    int dev = vfs_dev_of(cwd);
    if (!ui_text_input("NEW FOLDER", name, dev == DEV_NAND ? NAND_NAME_MAX : 120))
        return;
    char dst[VFS_PATH_MAX];
    vfs_join(dst, sizeof(dst), cwd, name);
    if (vfs_exists(dst))
        ui_message("ERROR", C_RED, "Something with that name already exists.");
    else if (!vfs_mkdir(dst))
        ui_message("ERROR", C_RED, g_write_err ? g_write_err : "Could not create the folder.");
    else {
        vfs_update_space(dev);
        pride_check(name);
    }
    load_dir(cwd, name);
}

static void do_mkfile(void)
{
    if (!risk_authorize(cwd, "create a file"))
        return;
    char name[VFS_NAME_MAX] = "";
    int dev = vfs_dev_of(cwd);
    if (!ui_text_input("NEW FILE", name, dev == DEV_NAND ? NAND_NAME_MAX : 120))
        return;
    char dst[VFS_PATH_MAX];
    vfs_join(dst, sizeof(dst), cwd, name);
    vfs_file f;
    if (vfs_exists(dst))
        ui_message("ERROR", C_RED, "Something with that name already exists.");
    else if (!vfs_open_write(&f, dst) || !vfs_close_checked(&f))
        ui_message("ERROR", C_RED, g_write_err ? g_write_err : "Could not create the file.");
    else {
        vfs_update_space(dev);
        pride_check(name);
    }
    load_dir(cwd, name);
}

static void select_all(bool all)
{
    nmarks = 0;
    for (int i = 0; i < nents; i++) {
        marks[i] = all ? true : false;
        nmarks += marks[i];
    }
}

static void invert_marks(void)
{
    nmarks = 0;
    for (int i = 0; i < nents; i++) {
        marks[i] = !marks[i];
        nmarks += marks[i];
    }
}

enum {
    A_OPEN,
    A_PASTE,
    A_CLIP_CLEAR,
    A_COPY,
    A_CUT,
    A_DELETE,
    A_RENAME,
    A_MKDIR,
    A_MKFILE,
    A_WADDUMP,
    A_WADSPLIT,
    A_WUP,
    A_WUD,
    A_INFO,
    A_USAGE,
    A_HEX,
    A_TEXT,
    A_EDIT_HEX,
    A_EDIT_TEXT,
    A_ALL,
    A_NONE,
    A_INVERT,
    A_REFRESH,
    A_COMPARE,
    A_REPLACE,
    A_INJECT,
    A_TITLEINFO,
    A_ZIP,
    A_COUNT
};

static void actions_menu(void);

/* The items as a .zip: next to them when that folder can be written, else at the SD card's top. */
static void do_zip(char **paths, int n)
{
    char dir[VFS_PATH_MAX], base[VFS_NAME_MAX], zp[VFS_PATH_MAX], err[VFS_PATH_MAX + 96], msg[VFS_PATH_MAX + 64];
    int dev = vfs_dev_of(cwd);
    bool here = (dev == DEV_SD || (dev >= DEV_USB0 && dev < DEV_USB0 + USB_SLOTS)) && !vfs_is_archive(cwd) &&
                !vfs_is_readonly(cwd);                  /* never onto the internal storage */
    if (here) snprintf(dir, sizeof(dir), "%s", cwd);
    else if (g_devs[DEV_SD].mounted) snprintf(dir, sizeof(dir), "%s", g_devs[DEV_SD].root);
    else { ui_message("ZIP", C_YELLOW, "This folder can't be written, and there's no SD card to put the zip on."); return; }
    if (n == 1) {
        bool is_dir = false;
        vfs_stat(paths[0], &is_dir, NULL);
        snprintf(base, sizeof(base), "%s", vfs_basename(paths[0]));
        char *dot = strrchr(base, '.');
        if (!is_dir && dot && dot != base) *dot = '\0';
    } else {
        const char *b = vfs_basename(cwd);
        snprintf(base, sizeof(base), "%s", b[0] && !strchr(b, ':') ? b : "Archive");
    }
    for (int i = 1; i < 1000; i++) {
        char name[VFS_NAME_MAX + 16];
        if (i == 1) snprintf(name, sizeof(name), "%s.zip", base);
        else snprintf(name, sizeof(name), "%s (%d).zip", base, i);
        if (!vfs_join(zp, sizeof(zp), dir, name)) return;
        if (!vfs_exists(zp)) break;
    }
    if (zip_create(paths, n, zp, err, sizeof(err))) {
        snprintf(msg, sizeof(msg), "Saved as %s", zp);
        ui_message("ZIPPED", C_GREEN, msg);
        if (here) reload_keep();
    } else {
        ui_message("ZIP", C_RED, err);
    }
}

static bool open_archive(const vfs_entry *en)
{
    char file[VFS_PATH_MAX], top[VFS_PATH_MAX + 4];
    if (!vfs_join(file, sizeof(file), cwd, en->name))
        return false;
    snprintf(top, sizeof(top), "%s|/", file);
    if (load_dir(top, NULL))
        return true;
    return false;
}

/* What a listed file is, from its first bytes. */
static ftype_t entry_type(const vfs_entry *en)
{
    char path[VFS_PATH_MAX];
    return vfs_join(path, sizeof(path), cwd, en->name) ? ftype_detect(path) : FT_NONE;
}

static void actions_menu(void)
{
    int off = clip_n ? 1 : 0;
    int e = cursor - off;
    bool on_entry = e >= 0 && e < nents;
    int dev = vfs_dev_of(cwd);
    bool ro = vfs_is_readonly(cwd);
    bool card = dev >= 0 && vfs_is_card(dev);

    const char *items[A_COUNT];
    bool en[A_COUNT];
    int code[A_COUNT];
    int n = 0;
    char paste_label[48];
    const char *del_label = "Delete", *ren_label = "Rename";

    items[n] = "Open as archive";
    en[n] = on_entry && !nmarks && !ents[e].is_dir;
    code[n++] = A_OPEN;

    if (clip_n) {
        snprintf(paste_label, sizeof(paste_label), "Paste %d item(s) here", clip_n);
        items[n] = paste_label;
        en[n] = !ro;
        code[n++] = A_PASTE;

        items[n] = "Clear clipboard";
        en[n] = true;
        code[n++] = A_CLIP_CLEAR;
    }

    items[n] = "Copy";
    en[n] = on_entry || nmarks;
    code[n++] = A_COPY;

    items[n] = "Cut";
    en[n] = (on_entry || nmarks) && !ro;
    code[n++] = A_CUT;

    items[n] = del_label;
    en[n] = (on_entry || nmarks) && !ro;
    code[n++] = A_DELETE;

    items[n] = ren_label;
    en[n] = on_entry && !nmarks && !ro && !card;
    code[n++] = A_RENAME;

    items[n] = "New folder";
    en[n] = !ro && !card;
    code[n++] = A_MKDIR;

    items[n] = "New file";
    en[n] = !ro && !card;
    code[n++] = A_MKFILE;

    /* an installed title's folder (or a content.bin) -> WAD, as YABDM makes them */
    bool dumpable = false;
    if (nmarks) {
        for (int i = 0; i < nents && !dumpable; i++) {
            char p[VFS_PATH_MAX];
            dumpable = marks[i] && vfs_join(p, sizeof(p), cwd, ents[i].name) && waddump_is_source(p, NULL);
        }
    } else if (on_entry) {
        char p[VFS_PATH_MAX];
        dumpable = vfs_join(p, sizeof(p), cwd, ents[e].name) && waddump_is_source(p, NULL);
    }
    if (!dumpable && !nmarks) dumpable = waddump_is_source(cwd, NULL);
    if (dumpable) {
        items[n] = "Back up as WAD (YABDM style)";
        en[n] = true;
        code[n++] = A_WADDUMP;
    }

    /* a DLC WAD -> small WADs (dlc-wad-splitter) */
    bool splittable = false;
    for (int i = 0; i < nents && !splittable; i++) {
        char p[VFS_PATH_MAX];
        if ((nmarks ? marks[i] : (on_entry && i == e)) && !ents[i].is_dir && entry_type(&ents[i]) == FT_WAD &&
            vfs_join(p, sizeof(p), cwd, ents[i].name))
            splittable = wad_is_dlc(p);
    }
    if (splittable) {
        items[n] = "Split DLC into small WADs (dlc-wad-splitter)";
        en[n] = true;
        code[n++] = A_WADSPLIT;
    }

    /* Wii U titles: a WUP / NUS folder (title.tmd, title.tik, the .app contents), or a disc image */
    char ep[VFS_PATH_MAX] = "";
    if (on_entry) vfs_join(ep, sizeof(ep), cwd, ents[e].name);
    bool wup_here = !nmarks && !ro && wup_is_folder(cwd);
    bool wup_entry = on_entry && ents[e].is_dir && wup_is_folder(ep);
    bool wup_marked = false;
    for (int i = 0; i < nents && nmarks && !wup_marked; i++) {
        char mp[VFS_PATH_MAX];
        wup_marked = marks[i] && ents[i].is_dir && vfs_join(mp, sizeof(mp), cwd, ents[i].name) && wup_is_folder(mp);
    }
    if (wup_here || wup_entry || wup_marked) {
        items[n] = "Install Wii U title (WUP)";
        en[n] = !vfs_is_archive(cwd);
        code[n++] = A_WUP;
    }
    if (on_entry && !nmarks && !ents[e].is_dir && wud_is_name(ents[e].name)) {
        items[n] = "Wii U disc image (WUD / WUX)...";
        en[n] = true;
        code[n++] = A_WUD;
    }

    items[n] = "Info / folder size";
    en[n] = on_entry || nmarks;
    code[n++] = A_INFO;

    items[n] = "Disk usage";
    en[n] = cwd[0] != '\0';
    code[n++] = A_USAGE;

    items[n] = "Text viewer";
    en[n] = on_entry && !nmarks && !ents[e].is_dir;
    code[n++] = A_TEXT;

    items[n] = "Hex viewer";
    en[n] = on_entry && !nmarks && !ents[e].is_dir;
    code[n++] = A_HEX;

    /* with one copied file on the clipboard: compare / replace / inject against this one */
    bool clip_file = false;
    if (clip_n == 1) { bool d; clip_file = vfs_stat(clip[0], &d, NULL) && !d; }
    if (clip_file && on_entry && !nmarks && !ents[e].is_dir) {
        items[n] = "Compare with the copied file (hex)";
        en[n] = true;
        code[n++] = A_COMPARE;
        items[n] = "Replace this with the copied file";
        en[n] = !ro && !card;
        code[n++] = A_REPLACE;
        items[n] = "Inject the copied file at an offset";
        en[n] = !ro && !card;
        code[n++] = A_INJECT;
    }
    char here_path[VFS_PATH_MAX] = "";
    if (on_entry && !nmarks) vfs_join(here_path, sizeof(here_path), cwd, ents[e].name);
    if ((here_path[0] && titleinfo_can(here_path)) || titleinfo_can(cwd)) {
        items[n] = "Title info (TMD / ticket)";
        en[n] = true;
        code[n++] = A_TITLEINFO;
    }

    items[n] = nmarks > 1 ? "Zip the marked items" : "Zip this (a .zip next to it)";
    en[n] = on_entry || nmarks;
    code[n++] = A_ZIP;

    items[n] = "Edit as text";
    en[n] = on_entry && !nmarks && !ro && !card && !ents[e].is_dir;
    code[n++] = A_EDIT_TEXT;

    items[n] = "Edit as hex";
    en[n] = on_entry && !nmarks && !ro && !card && !ents[e].is_dir;
    code[n++] = A_EDIT_HEX;

    items[n] = "Select all";
    en[n] = nents > 0;
    code[n++] = A_ALL;

    items[n] = "Deselect all";
    en[n] = nmarks > 0;
    code[n++] = A_NONE;

    items[n] = "Invert selection";
    en[n] = nents > 0;
    code[n++] = A_INVERT;

    items[n] = "Refresh";
    en[n] = true;
    code[n++] = A_REFRESH;

    int r = ui_menu("ACTIONS", items, en, n, 0);
    if (r < 0)
        return;
    if (!en[r]) {
        if (code[r] == A_DELETE && on_entry && on_game_disc(cwd))
            game_disc_message();
        else if (ro)
            ui_message("READ-ONLY", C_YELLOW,
                       "This is read-only. Copy its files to the SD card or a USB drive instead.");
        return;
    }

    int t;
    char **tp;

    switch (code[r]) {
    case A_OPEN:    open_archive(&ents[e]); break;
    case A_PASTE:   do_paste(); break;
    case A_CLIP_CLEAR: do_clip_clear(); break;
    case A_COPY:    do_copy_cut(false); break;
    case A_CUT:     do_copy_cut(true); break;
    case A_DELETE:  do_delete(); break;
    case A_RENAME:  do_rename(); break;
    case A_MKDIR:   do_mkdir(); break;
    case A_MKFILE:  do_mkfile(); break;
    case A_INFO:
        tp = targets(&t);
        if (t) {
            show_info(tp, t);
            free_paths(tp, t);
        }
        break;
    case A_WADDUMP: {
        tp = targets(&t);
        bool any = false;
        for (int i = 0; i < t; i++) any = any || waddump_is_source(tp[i], NULL);
        if (any) {
            waddump_paths(tp, t);
        } else {
            char *here = cwd;
            waddump_paths(&here, 1);
        }
        if (tp) free_paths(tp, t);
        reload_keep();
        break;
    }
    case A_WADSPLIT:
        tp = targets(&t);
        if (t) {
            wad_split_paths(tp, t);
            free_paths(tp, t);
        }
        reload_keep();
        break;
    case A_WUP: {
        tp = NULL;
        t = 0;
        if (wup_marked || wup_entry) tp = targets(&t);
        if (t) {
            wup_install_paths(tp, t);
            free_paths(tp, t);
        } else {
            char *here = cwd;
            wup_install_paths(&here, 1);
        }
        reload_keep();
        break;
    }
    case A_WUD:
        if (wud_menu(ep))
            open_archive(&ents[e]);
        else
            reload_keep();
        break;
    case A_USAGE:
        usage_browse(cwd);
        reload_keep();
        break;
    case A_COMPARE:
    case A_REPLACE:
    case A_INJECT: {
        char p[VFS_PATH_MAX];
        if (!vfs_join(p, sizeof(p), cwd, ents[e].name)) break;
        if (code[r] == A_COMPARE) hexcompare(p, clip[0]);
        else if (code[r] == A_REPLACE ? file_replace(p, clip[0]) : file_inject(p, clip[0])) reload_keep();
        break;
    }
    case A_ZIP: {
        int t;
        char **tp = targets(&t);
        if (t) do_zip(tp, t);
        free_paths(tp, t);
        break;
    }
    case A_TITLEINFO:
        titleinfo_show(here_path[0] && titleinfo_can(here_path) ? here_path : cwd);
        break;
    case A_HEX:
        tp = targets(&t);
        if (t) {
            hexview(tp[0]);
            free_paths(tp, t);
        }
        break;
    case A_TEXT:
        tp = targets(&t);
        if (t) {
            textview(tp[0]);
            free_paths(tp, t);
        }
        break;
    case A_EDIT_TEXT:
        tp = targets(&t);
        if (t) {
            editor_text(tp[0]);
            free_paths(tp, t);
            reload_keep();
        }
        break;
    case A_EDIT_HEX:
        tp = targets(&t);
        if (t) {
            editor_hex(tp[0]);
            free_paths(tp, t);
            reload_keep();
        }
        break;
    case A_ALL:     select_all(true); break;
    case A_NONE:    select_all(false); break;
    case A_INVERT:  invert_marks(); break;
    case A_REFRESH: vfs_update_space(dev); reload_keep(); break;
    }
}

/* The FTP server may have changed anything: refresh what is on screen. */
static void reload_keep_safe(void)
{
    for (int i = 0; i < DEV_COUNT; i++)
        vfs_update_space(i);
    if (cwd[0])
        reload_keep();
}

/* ---------------------------------------------------------------- format a USB drive as FAT32 */

static void fmt_progress(u64 done, u64 total, void *ctx)
{
    (void)ctx;
    ui_busy_progress(done, total);
}

/* Only a USB drive in a format the Wii's Homebrew Channel and USB loaders can't use (exFAT, NTFS, ext, WBFS,
 * unreadable) is offered: a FAT drive never is, and a Wii U formatted drive is mounted by the Wii U and never listed. */
static void format_menu(void)
{
    const char *T = "FORMAT AS FAT32";
    const char *names[USB_SLOTS];
    char labels[USB_SLOTS][64];
    int devs[USB_SLOTS], n = 0;
    for (int d = DEV_USB0; d < DEV_USB0 + USB_SLOTS; d++) {
        const char *fs = vfs_foreign_fs(d);
        if (!fs) continue;
        snprintf(labels[n], sizeof(labels[n]), "%s  (%s)", g_devs[d].name, fs);
        names[n] = labels[n];
        devs[n++] = d;
    }
    if (!n) {
        ui_message(T, C_GREEN, "Every USB drive here is FAT already, Wii U formatted, or not connected. Nothing needs "
                   "formatting.\n\nOnly a drive in another format (exFAT, NTFS, ext, WBFS, unreadable...) is offered here.");
        return;
    }
    int r = ui_menu("FORMAT WHICH DRIVE?", names, NULL, n, 0);
    if (r < 0) return;
    int dev = devs[r];
    u64 secs = vfs_drive_sectors(dev);
    const u32 ss = 512;
    fat32_plan p;
    char sz[24], q[700];
    if (!secs) { ui_message(T, C_RED, "The drive can't be reached."); return; }
    ui_fmt_size(sz, sizeof(sz), secs * ss);
    if (!fat32_layout(secs, ss, &p)) { ui_message(T, C_RED, "This drive is too small or too big for FAT32."); return; }
    snprintf(q, sizeof(q), "%s is %s.\n\nFormat it as FAT32 (%s, one partition, %u KB clusters)%s?\n\nEVERYTHING ON THE "
             "DRIVE IS ERASED: every partition and every file. Copy off what you want to keep first.", g_devs[dev].name,
             vfs_foreign_fs(dev), sz, (unsigned)(p.spc * ss / 1024),
             (u64)p.vol + p.start < secs ? " (FAT32 tops out at 2 TB: the rest stays unused)" : "");
    if (!ui_confirm(T, C_RED, q)) return;
    snprintf(q, sizeof(q), "Last chance: erase all of %s (%s) and format it?", g_devs[dev].name, sz);
    if (!ui_confirm(T, C_RED, q)) return;
    char err[160] = "";
    ui_busy_begin("Formatting...");
    vfs_unmount_all();
    bool ok = fat32_format(vfs_drive_write, (void *)(intptr_t)(dev - DEV_USB0), secs, ss, "WII USB", fmt_progress, NULL,
                           err, sizeof(err));
    ui_busy_end();
    ui_splash("Looking for drives...");
    vfs_remount();
    free_dir();
    cwd[0] = '\0';
    ui_message(ok ? "FORMATTED" : T, ok ? C_GREEN : C_RED, ok ? "The drive is FAT32 now, empty and ready for apps/ and wbfs/." : err);
}

/* Returns true if the user wants to leave the program. */
static bool system_menu(void)
{
    enum {
        M_LOCK,
        M_REMOUNT,
        M_FORMAT,
        M_FTP,
        M_WIILOAD,
        M_MIIS,
        M_MIIU,
        M_KEYS,
        M_DRAW,
        M_MEMOS,
        M_IR,
        M_SFX,
        M_ALARM,
        M_CALC,
        M_CLOCK,
        M_THEME,
        M_SAVES,
        M_NAND,
        M_WUPHELP,
        M_ABOUT,
        M_LOADER,
        M_WIIMENU,
        M_RESTART,
        M_POWER
    };

    for (;;) {
        if (cwd[0])
            draw_browser();
        else
            draw_home();

        /* The HOME menu in groups, as GodModeWii's; B in a group goes back to the groups, B there closes the menu. */
        static const char *const groups[] = {
            "Drives and network", "Titles and saves", "Console and Miis", "Tools", "Settings", "Exit and power",
        };
        enum { G_DRIVES, G_TITLES, G_CONSOLE, G_TOOLS, G_SETTINGS, G_EXIT, G_COUNT };
        const char *gitems[G_COUNT + 1];
        int gn = 0, glock = -1;
        if (risk_any_unlocked() && !risk_forced()) { glock = gn; gitems[gn++] = "Lock risky options again"; }
        for (int g = 0; g < G_COUNT; g++) gitems[gn++] = groups[g];
        int gr = ui_menu_ex("HOME", gitems, NULL, gn, 0, "A Open   B / HOME Back");
        if (gr < 0)
            return false;
        int g = gr == glock ? -1 : gr - (glock >= 0 ? 1 : 0);

        char theme_item[48];
        const char *items[16];
        int code[16];
        int n = 0;
#define ITEM(text, c) do { items[n] = (text); code[n++] = (c); } while (0)
        switch (g) {
        case -1:
            ITEM("Lock risky options again", M_LOCK);
            break;
        case G_DRIVES:
            ITEM("Remount drives (SD / USB / internal)", M_REMOUNT);
            ITEM("FTP server", M_FTP);
            ITEM("Wiiload (receive apps and files)", M_WIILOAD);
            ITEM("Format a USB drive as FAT32", M_FORMAT);
            break;
        case G_TITLES:
            ITEM("Install Wii U titles (WUP / WUD help)", M_WUPHELP);
            ITEM("Export all vWii saves", M_SAVES);
            ITEM("Back up the NAND / MLC (how to)", M_NAND);
            break;
        case G_CONSOLE:
            ITEM("Miis", M_MIIS);
            ITEM("Export Wii U Miis (.ffsd)", M_MIIU);
            ITEM("Save Wii U keys for GodModeWii", M_KEYS);
            break;
        case G_TOOLS:
            ITEM("IR remote (TV / AV control, from the GamePad)", M_IR);
            ITEM("Sketchpad (draw)", M_DRAW);
            ITEM("Voice memos (record / play)", M_MEMOS);
            ITEM("Alarm clock", M_ALARM);
            ITEM("Calculator", M_CALC);
            ITEM("Clock and calendar", M_CLOCK);
            break;
        case G_SETTINGS:
            snprintf(theme_item, sizeof(theme_item), "Theme: %s", colors_name(colors_current()));
            ITEM(theme_item, M_THEME);
            ITEM(sfx_enabled() ? "Sound effects: ON (select to turn off)" : "Sound effects: OFF (select to turn on)", M_SFX);
            ITEM("About / credits", M_ABOUT);
            break;
        default:
            if (launched_from_hbl()) ITEM("Exit to Homebrew Launcher", M_LOADER);
            ITEM("Exit to Wii U Menu", M_WIIMENU);
            ITEM("Restart console", M_RESTART);
            ITEM("Power off", M_POWER);
            break;
        }
#undef ITEM
        /* HOME closes the menu like the Wii's own HOME Menu; leaving is always an explicit choice. */
        int r = g < 0 ? 0 : ui_menu_ex(groups[g], items, NULL, n, 0, "A Select   B Back");
        if (r < 0)
            continue;
        switch (code[r]) {
        case M_LOCK:
            risk_lock();
            status(C_GREEN, "Risky options locked.");
            return false;
        case M_FORMAT:
            format_menu();
            return false;
        case M_REMOUNT:
            ui_splash("Looking for drives...");
            vfs_remount();
            input_flush();
            clip_clear();
            free_dir();
            cwd[0] = '\0';
            status(C_GREEN, "Drives re-scanned.");
            return false;
        case M_FTP: {
            const char *modes[] = {
                "Read-only (browse and download)",
                "Read/write (upload, rename, delete)",
                "Install server: archives unpacked onto the SD card",
                "Install server: archives unpacked onto a USB drive",
            };
            int m = ui_menu("FTP SERVER", modes, NULL, 4, 0);
            if (m < 0)
                break;                  /* back to this menu */
            sfx_cancel();               /* the click comes once the network is up */
            ftp_run(m == 1, m == 2 ? FTP_INSTALL_SD : m == 3 ? FTP_INSTALL_USB : FTP_INSTALL_OFF);
            reload_keep_safe();
            return false;
        }
        case M_WIILOAD: {
            sfx_cancel();
            char prog[VFS_PATH_MAX];
            wiiload_run(prog, sizeof(prog));
            reload_keep_safe();
            return false;
        }
        case M_MIIS:
            mii_export();
            return false;
        case M_MIIU:
            miiu_export();
            return false;
        /* Only shows a box, so B comes back to the SYSTEM menu. */
        case M_KEYS: wfskeys_save(); break;
        case M_DRAW:
            draw_run();
            reload_keep_safe();
            return false;
        case M_MEMOS:
            memo_run();
            reload_keep_safe();
            return false;
        case M_IR:
            irremote_run(false);
            reload_keep_safe();
            return false;
        case M_SFX:
            sfx_set_enabled(!sfx_enabled());      /* saved on the card (sd:/apps/GodModeWii/sfx_off) */
            sfx_play(SFX_CLICK);
            status(C_GREEN, sfx_enabled() ? "Sound effects on." : "Sound effects off (saved on the card).");
            return false;
        case M_SAVES:
            saves_export_all();
            reload_keep_safe();
            return false;
        case M_NAND:
            nand_backup_help();
            return false;
        case M_WUPHELP: wup_help(); break;
        /* Only shows a box, so B comes back to the SYSTEM menu, redrawn over the browser. */
        case M_ABOUT: show_credits(); break;
        case M_ALARM:
            alarm_menu();
            return false;
        case M_CLOCK:
            clock_run();
            return false;
        case M_CALC:
            calc_run();
            return false;
        case M_THEME: {
            const char *names[16];
            int nt = colors_count();
            for (int t = 0; t < nt && t < 16; t++) names[t] = colors_name(t);
            int pick = ui_menu("THEME", names, NULL, nt, colors_current());
            if (pick >= 0) colors_apply(pick, true);
            break;
        }
        case M_LOADER: exit_req = EXIT_LOADER; return true;
        case M_WIIMENU: exit_req = EXIT_MENU; return true;
        case M_RESTART: sfx_play(SFX_POWEROFF); sfx_wait(); exit_req = EXIT_RESTART; return true;
        default: sfx_play(SFX_POWEROFF); sfx_wait(); exit_req = EXIT_POWEROFF; return true;
        }
    }
}

/* ---------------------------------------------------------------- main loop */

static void open_drive(int dev)
{
    vfs_dev *d = &g_devs[dev];
    if (!d->mounted) {
        char why[160];
        snprintf(why, sizeof(why), "%s%s%s", d->name, d->note[0] ? ": " : " is not available.",
                 d->note);
        if (!d->note[0])
            snprintf(why, sizeof(why),
                     "Nothing detected here. Insert a card or drive and use \"Remount drives\" in the HOME menu.");
        ui_message("NOT AVAILABLE", C_YELLOW, why);
        return;
    }

    vfs_update_space(dev);
    load_dir(d->root, NULL);
}

int app_run(void)
{
    bool dirty = true;
    int last_hover = -1, shown_minute = -2;

    while (!g_quit_request) {
        if (dirty) {
            if (cwd[0])
                draw_browser();
            else
                draw_home();
            dirty = false;
        }
        u32 p = input_pressed() | ptr_ui_buttons();
        {
            char clk[12];
            int minute = clock_minute(clk, sizeof(clk));
            if (minute != shown_minute) {                  /* keep the clock in the header current */
                shown_minute = minute;
                draw_header();
            }
        }
        {
            /* the pointer / mouse: hovering a row selects it, A (or a click) on it acts on it */
            int devs_tmp[DEV_COUNT];
            bool in_dir = cwd[0] != '\0';
            int vis = in_dir ? list_rows() : home_rows();
            int count = in_dir ? nents + (clip_n ? 1 : 0) : home_list(devs_tmp);
            int *cur = in_dir ? &cursor : &home_cursor;
            int base = in_dir ? scroll : home_scroll;
            int hov = ptr_hover_item(3, vis, 0, ui_cols(), &last_hover);
            int hit = ptr_item_at(3, vis, 0, ui_cols());
            if (hov >= 0 && base + hov < count && base + hov != *cur) {
                *cur = base + hov;
                if (!p) {
                    dirty = true;
                    cursor_only = true;
                    continue;
                }
            }
            if ((p & WPAD_BUTTON_A) && hit >= 0 && base + hit < count)
                *cur = base + hit;
        }
        {
            int up = update_poll();
            if (up == 2) return EXIT_MENU;
            if (up) {
                dirty = true;
                continue;
            }
        }
        if (alarm_ringing()) {
            const char *opts[] = { "Stop", "Snooze (5 minutes)" };
            int r = ui_menu("ALARM", opts, NULL, 2, 0);
            if (r == 1) alarm_snooze(5);
            else alarm_stop();
            dirty = true;
            continue;
        }
        if (!p)
            continue;
        status_msg[0] = '\0';
        dirty = true;
        cursor_only =
            !(p & ~(WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT));

        if (p & WPAD_BUTTON_HOME) {
            sfx_play(SFX_MENU);                 /* TiVo mode only */
            if (system_menu())
                break;
            cursor_only = false;            /* the menu (and a splash) drew over the screen: redraw it all */
            continue;
        }

        if (!cwd[0]) {
            int devs[DEV_COUNT];
            int n = home_list(devs);
            /* several drives at a time, like a page of files */
            int jump = home_rows() > 2 ? home_rows() - 1 : 1;
            if (!tivo_bonk(p, home_cursor, n)) {
                if (p & WPAD_BUTTON_UP)   home_cursor = (home_cursor + n - 1) % n;
                if (p & WPAD_BUTTON_DOWN) home_cursor = (home_cursor + 1) % n;
                if (p & WPAD_BUTTON_LEFT)  { home_cursor -= jump; if (home_cursor < 0) home_cursor = 0; }
                if (p & WPAD_BUTTON_RIGHT) { home_cursor += jump; if (home_cursor >= n) home_cursor = n - 1; }
            }
            /* the easter egg: the Konami code (no START) here opens Bad Block Sweeper instead of the drive */
            static const u32 konami[] = { WPAD_BUTTON_UP, WPAD_BUTTON_UP, WPAD_BUTTON_DOWN, WPAD_BUTTON_DOWN,
                                          WPAD_BUTTON_LEFT, WPAD_BUTTON_RIGHT, WPAD_BUTTON_LEFT, WPAD_BUTTON_RIGHT,
                                          WPAD_BUTTON_B, WPAD_BUTTON_A };
            static int konami_got;
            konami_got = p == konami[konami_got] ? konami_got + 1 : p == konami[0] ? 1 : 0;
            if (konami_got == 10) {
                konami_got = 0;
                sweeper_run();
                cursor_only = false;
                continue;
            }
            /* and the TiVo code (+ A - 1 2 -, or - A + 1 2 -) switches TiVo mode, as on the last
             * credits page; while it is under way its A doesn't open the drive */
            static const u32 tivo[2][6] = {
                { WPAD_BUTTON_PLUS, WPAD_BUTTON_A, WPAD_BUTTON_MINUS, WPAD_BUTTON_1, WPAD_BUTTON_2, WPAD_BUTTON_MINUS },
                { WPAD_BUTTON_MINUS, WPAD_BUTTON_A, WPAD_BUTTON_PLUS, WPAD_BUTTON_1, WPAD_BUTTON_2, WPAD_BUTTON_MINUS },
            };
            static int tivo_got[2];
            bool tivo_kept = false, tivo_done = false;
            for (int c = 0; p && c < 2; c++) {
                if (p == tivo[c][tivo_got[c]]) tivo_got[c]++, tivo_kept = true;
                else tivo_got[c] = p == tivo[c][0] ? 1 : 0;
                if (tivo_got[c] == 6) tivo_done = true;
            }
            if (tivo_done) {
                tivo_got[0] = tivo_got[1] = 0;
                bool saved = sfx_tivo_toggle();
                ui_tivo_dialog(sfx_tivo(), saved, sfx_enabled());
                cursor_only = false;
                continue;
            }
            if ((p & WPAD_BUTTON_A) && !tivo_kept) open_drive(devs[home_cursor]);
            continue;
        }

        int off = clip_n ? 1 : 0;
        int total = nents + off;
        int L = list_rows();

        if (tivo_bonk(p, cursor, total)) {
        } else {
            if (p & WPAD_BUTTON_UP)    { if (total) cursor = (cursor + total - 1) % total; }
            if (p & WPAD_BUTTON_DOWN)  { if (total) cursor = (cursor + 1) % total; }
            if (p & WPAD_BUTTON_LEFT) {
                cursor -= L - 1;
                if (cursor < 0) cursor = 0;
            }
            if (p & WPAD_BUTTON_RIGHT) {
                cursor += L - 1;
                if (cursor >= total) cursor = total ? total - 1 : 0;
            }
        }

        if (p & WPAD_BUTTON_B) {
            char parent[VFS_PATH_MAX];
            char from[VFS_NAME_MAX];
            snprintf(from, sizeof(from), "%s", vfs_basename(cwd));
            char container[VFS_PATH_MAX];
            if (vfs_is_archive(cwd) && arcfs_container(cwd, container, sizeof(container)) &&
                cwd[strlen(container) + 2] == '\0')
                snprintf(from, sizeof(from), "%s", vfs_basename(container));
            if (vfs_parent(cwd, parent, sizeof(parent))) {
                load_dir(parent, from);
            } else {
                free_dir();
                cwd[0] = '\0';
            }
        }

        if (p & WPAD_BUTTON_A) {
            if (off && cursor == 0) {
                do_paste();
            } else if (cursor - off >= 0 && cursor - off < nents) {
                vfs_entry *en = &ents[cursor - off];
                if (en->is_dir) {
                    char path[VFS_PATH_MAX];
                    if (vfs_join(path, sizeof(path), cwd, en->name))
                        load_dir(path, NULL);
                } else {
                    char path[VFS_PATH_MAX];
                    bool joined = vfs_join(path, sizeof(path), cwd, en->name);
                    /* by its magic, not its name */
                    ftype_t ft = joined ? ftype_detect(path) : FT_NONE;
                    if (joined && !vfs_is_archive(cwd) && wud_is_name(en->name)) {
                        if (wud_menu(path))
                            open_archive(en);
                    }
                    else if (joined && !vfs_is_archive(cwd) && !strcasecmp(en->name, "title.tmd") && wup_is_folder(cwd)) {
                        char *here = cwd;
                        wup_install_paths(&here, 1);
                        reload_keep();
                    } else if (ftype_is_media(ft) && media_open_type(path, ft))
                        ;
                    else if (joined && arcfs_can_open(path))
                        open_archive(en);
                    else if (joined && arc_name_has_ext(en->name, ".txt"))
                        textview(path);         /* PDF pages, logs, readmes */
                    else
                        actions_menu();
                }
            }
        }

        if (p & WPAD_BUTTON_1) {
            int e = cursor - off;
            if (e >= 0 && e < nents) {
                marks[e] = !marks[e];
                nmarks += marks[e] ? 1 : -1;
                if (marks[e] && !strcasecmp(ents[e].name, "mark"))
                    ui_message("MARK", C_CYAN, "Oh hi Mark.");
                if (cursor + 1 < total) cursor++;
            } else if (off && cursor == 0) {
                do_clip_clear();
            }
        }
        if (p & WPAD_BUTTON_2)     actions_menu();
        if (p & WPAD_BUTTON_PLUS)  do_copy_cut(false);
        if (p & WPAD_BUTTON_MINUS) {
            if (on_game_disc(cwd))
                game_disc_message();            /* a cut deletes the originals */
            else if (vfs_is_readonly(cwd))
                ui_message("READ-ONLY", C_YELLOW,
                           "Nothing can be cut from a read-only location. Use + to copy instead.");
            else
                do_copy_cut(true);
        }
    }

    /* Reset / power buttons end up here through the quit flags. */
    if (g_power_request)
        return EXIT_POWEROFF;
    return exit_req;
}
