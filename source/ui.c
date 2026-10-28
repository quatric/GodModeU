#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include <coreinit/cache.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <coreinit/screen.h>
#include <proc_ui/procui.h>

#include "ui.h"
#include "input.h"

/* The TV is a character grid, like the console's text mode was: everything is
 * written into a cell buffer, and ui_present() paints the grid with OSScreen
 * whenever the UI waits for input (see input.c). Text is always white;
 * colours only tint a cell's background, which is how highlight bars are drawn.
 *
 * OSScreenPutFontEx places 12 x 24 pixel cells from (50, 32) on the 1280x720 TV
 * (decaf-emu's numbers, confirmed on a Wii U), so the grid and bars follow it. */
#define FONT_X0   50
#define FONT_Y0   32
#define CELL_W    12
#define CELL_H    24
#define GRID_COLS ((1280 - 2 * FONT_X0) / CELL_W)
#define GRID_ROWS ((720 - 2 * FONT_Y0) / CELL_H)
#define BG_NONE   0xff
#define SCREEN_HEAP_TAG 0x474D5553
/* The GamePad mirrors the TV at 2/3 size; 1280 * 2/3 leaves its last column black. */
#define DRC_W     853
#define DRC_H     480

static char cells[GRID_ROWS][GRID_COLS];
static u8 cell_bg[GRID_ROWS][GRID_COLS];
static char shown_cells[GRID_ROWS][GRID_COLS];
static u8 shown_bg[GRID_ROWS][GRID_COLS];
static void *screen_mem[2];     /* indexed by SCREEN_TV / SCREEN_DRC */
static bool screen_ready;
static int g_cols = GRID_COLS, g_rows = GRID_ROWS;
static int cur_row, cur_col;
static u8 cur_bg = BG_NONE;

/* OSScreen's font is white only, so tinted text is recolored in the buffer. */
static struct { int row, col, len; u32 rgb; } tints[16];
static int ntints;

/* 0xRRGGBBXX, one per C_* colour (white is never used as a background). */
static const u32 BG_RGBA[8] = {
    0x000000FF, /* C_BLACK   */
    0xB02828FF, /* C_RED     */
    0x2E8B3AFF, /* C_GREEN   */
    0xB8961EFF, /* C_YELLOW  */
    0x2A4FB3FF, /* C_BLUE    */
    0x9A3AA8FF, /* C_MAGENTA */
    0x1C9CB8FF, /* C_CYAN    */
    0xE6E6E6FF, /* C_WHITE   */
};

/* The screen buffer lives in the MEM1 frame heap, which the system takes back
 * whenever the app loses the foreground. */
static u32 screen_acquired(void *ctx)
{
    (void)ctx;
    MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
    MEMRecordStateForFrmHeap(heap, SCREEN_HEAP_TAG);
    for (int id = SCREEN_TV; id <= SCREEN_DRC; id++) {
        screen_mem[id] = MEMAllocFromFrmHeapEx(heap, OSScreenGetBufferSizeEx(id), 0x100);
        if (!screen_mem[id]) {
            MEMFreeByStateToFrmHeap(heap, SCREEN_HEAP_TAG);
            return 0;
        }
        OSScreenSetBufferEx(id, screen_mem[id]);
        OSScreenEnableEx(id, TRUE);
    }
    screen_ready = true;
    memset(shown_cells, 0, sizeof(shown_cells));
    return 0;
}

static u32 screen_released(void *ctx)
{
    (void)ctx;
    if (!screen_ready)
        return 0;
    screen_ready = false;
    MEMFreeByStateToFrmHeap(MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1), SCREEN_HEAP_TAG);
    screen_mem[SCREEN_TV] = screen_mem[SCREEN_DRC] = NULL;
    return 0;
}

void ui_init(void)
{
    OSScreenInit();
    screen_acquired(NULL);
    ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, screen_acquired, NULL, 100);
    ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, screen_released, NULL, 100);
    if (!screen_ready)
        return;
    ui_clear();
    ui_present();
}

void ui_shutdown(void)
{
    if (!screen_ready)
        return;
    OSScreenShutdown();
    screen_released(NULL);
}

int ui_cols(void) { return g_cols; }
int ui_rows(void) { return g_rows; }

static void put(int row, int col, char ch)
{
    if (row < 0 || row >= g_rows || col < 0 || col >= g_cols)
        return;
    cells[row][col] = ch;
    cell_bg[row][col] = cur_bg;
}

/* Callers use 0-based cells. */
void ui_goto(int row, int col)
{
    cur_row = row;
    cur_col = col;
}

void ui_clear(void)
{
    cur_bg = BG_NONE;
    memset(cells, ' ', sizeof(cells));
    memset(cell_bg, BG_NONE, sizeof(cell_bg));
    ntints = 0;
}

static void tint(int row, int col, int len, u32 rgb)
{
    if (ntints >= (int)(sizeof(tints) / sizeof(tints[0])))
        return;
    tints[ntints].row = row;
    tints[ntints].col = col;
    tints[ntints].len = len;
    tints[ntints++].rgb = rgb;
}

/* There's no API for the back buffer, so a marker pixel finds it and its pitch. */
static u32 *back_buffer(OSScreenID id, int *pitch)
{
    const u32 mark = 0x01020300;
    u32 *halves[2] = { screen_mem[id], (u32 *)screen_mem[id] + OSScreenGetBufferSizeEx(id) / 8 };
    u32 *buf = NULL;
    OSScreenPutPixelEx(id, 0, 0, mark);
    OSScreenPutPixelEx(id, 0, 1, mark);
    for (int i = 0; i < 2 && !buf; i++)
        if (halves[i][0] == mark)
            buf = halves[i];
    *pitch = 0;
    for (int i = 1; buf && i < 4096 && !*pitch; i++)
        if (buf[i] == mark)
            *pitch = i;
    OSScreenPutPixelEx(id, 0, 0, BG_RGBA[C_BLACK]);
    OSScreenPutPixelEx(id, 0, 1, BG_RGBA[C_BLACK]);
    return *pitch ? buf : NULL;
}

static void apply_tints(void)
{
    int pitch;
    u32 *buf = ntints ? back_buffer(SCREEN_TV, &pitch) : NULL;
    if (!buf)
        return;
    for (int t = 0; t < ntints; t++) {
        u32 rgb = tints[t].rgb;
        int x0 = FONT_X0 + tints[t].col * CELL_W, x1 = x0 + tints[t].len * CELL_W;
        int y0 = FONT_Y0 + tints[t].row * CELL_H;
        for (int y = y0; y < y0 + CELL_H; y++) {
            u32 *line = buf + y * pitch;
            for (int x = x0; x < x1; x++) {
                /* Scale by the glyph's brightness so anti-aliased edges stay smooth. */
                u32 v = line[x] >> 24;
                if (!v)
                    continue;
                line[x] = (((rgb >> 16 & 0xff) * v / 255) << 24) |
                          (((rgb >> 8 & 0xff) * v / 255) << 16) |
                          (((rgb & 0xff) * v / 255) << 8) | 0xff;
            }
            DCFlushRange(line + x0, (x1 - x0) * 4);
        }
    }
}

static u32 blend(u32 a, u32 b, u32 c, u32 d, u32 wa, u32 wb, u32 wc, u32 wd)
{
    if (a == b && a == c && a == d)
        return a;
    u32 out = 0xff;
    for (int s = 8; s <= 24; s += 8)
        out |= (((a >> s & 0xff) * wa + (b >> s & 0xff) * wb +
                 (c >> s & 0xff) * wc + (d >> s & 0xff) * wd) / 9) << s;
    return out;
}

/* Each GamePad pixel covers 1.5 x 1.5 TV pixels: two per axis, weighted 2:1 or 1:2. */
static void mirror_to_drc(void)
{
    int tv_pitch, drc_pitch;
    u32 *tv = back_buffer(SCREEN_TV, &tv_pitch);
    OSScreenClearBufferEx(SCREEN_DRC, BG_RGBA[C_BLACK]);
    u32 *drc = back_buffer(SCREEN_DRC, &drc_pitch);
    if (!tv || !drc)
        return;
    /* Read what OSScreen drew, not stale cache lines from an earlier frame. */
    DCFlushRange(tv, tv_pitch * 720 * 4);
    for (int dy = 0; dy < DRC_H; dy++) {
        const u32 *r0 = tv + (dy / 2 * 3 + (dy & 1)) * tv_pitch, *r1 = r0 + tv_pitch;
        u32 y0 = (dy & 1) ? 1 : 2, y1 = 3 - y0;
        u32 *out = drc + dy * drc_pitch;
        for (int dx = 0; dx < DRC_W; dx++) {
            int sx = dx / 2 * 3 + (dx & 1);
            u32 x0 = (dx & 1) ? 1 : 2, x1 = 3 - x0;
            out[dx] = blend(r0[sx], r0[sx + 1], r1[sx], r1[sx + 1], x0 * y0, x1 * y0, x0 * y1, x1 * y1);
        }
        DCFlushRange(out, DRC_W * 4);
    }
}

void ui_bg(int bg)
{
    cur_bg = (u8)bg;
}

/* Text colours don't exist in OSScreen's font; kept so the callers read the same. */
void ui_color(int fg, bool bold)
{
    (void)fg;
    (void)bold;
}

void ui_reset(void)
{
    cur_bg = BG_NONE;
}

void ui_putc(int c)
{
    put(cur_row, cur_col, (char)c);
    cur_col++;
}

void ui_puts(const char *s)
{
    while (*s)
        ui_putc(*s++);
}

void ui_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ui_puts(buf);
}

void ui_field(int row, int col, int width, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (width >= (int)sizeof(buf))
        width = sizeof(buf) - 1;
    int len = strlen(buf);
    for (; len < width; len++)
        buf[len] = ' ';
    for (int i = 0; i < width; i++)
        put(row, col + i, buf[i]);
}

void ui_fill(int row, int col, int width, char c)
{
    for (int i = 0; i < width; i++)
        put(row, col + i, c);
}

void ui_bar(int row, int col, int width, int pct, int color)
{
    (void)color;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int inner = width - 2;
    if (inner < 0) inner = 0;
    int fill = inner * pct / 100;
    ui_goto(row, col);
    ui_putc('[');
    for (int i = 0; i < inner; i++)
        ui_putc(i < fill ? '#' : '-');
    ui_putc(']');
}

void ui_fmt_size(char *out, size_t n, u64 bytes)
{
    static const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        u++;
    }
    if (u == 0)
        snprintf(out, n, "%u B", (unsigned)bytes);
    else
        snprintf(out, n, "%.*f %s", v >= 100 ? 0 : (v >= 10 ? 1 : 2), v, units[u]);
}

/* Paints the grid if it changed since the last frame. Called by the input layer
 * right before it waits for a button, so every drawing call shows up without
 * the screens having to remember to flip. */
void ui_present(void)
{
    if (!screen_ready)
        return;
    if (!memcmp(cells, shown_cells, sizeof(cells)) && !memcmp(cell_bg, shown_bg, sizeof(cell_bg)))
        return;
    memcpy(shown_cells, cells, sizeof(cells));
    memcpy(shown_bg, cell_bg, sizeof(cell_bg));

    OSScreenClearBufferEx(SCREEN_TV, BG_RGBA[C_BLACK]);

    /* Background: one rectangle per run of equal cells on a row. */
    for (int r = 0; r < g_rows; r++) {
        int c = 0;
        while (c < g_cols) {
            u8 bg = cell_bg[r][c];
            int end = c + 1;
            while (end < g_cols && cell_bg[r][end] == bg)
                end++;
            if (bg != BG_NONE && bg < 8) {
                for (int y = FONT_Y0 + r * CELL_H; y < FONT_Y0 + (r + 1) * CELL_H; y++)
                    for (int x = FONT_X0 + c * CELL_W; x < FONT_X0 + end * CELL_W; x++)
                        OSScreenPutPixelEx(SCREEN_TV, x, y, BG_RGBA[bg]);
            }
            c = end;
        }
    }

    /* Text: runs of non-blank characters, so blank cells never cover a background. */
    char run[GRID_COLS + 1];
    for (int r = 0; r < g_rows; r++) {
        int c = 0;
        while (c < g_cols) {
            while (c < g_cols && cells[r][c] == ' ')
                c++;
            int k = 0, start = c;
            while (c < g_cols && cells[r][c] != ' ')
                run[k++] = cells[r][c++];
            if (k) {
                run[k] = '\0';
                OSScreenPutFontEx(SCREEN_TV, start, r, run);
            }
        }
    }

    apply_tints();
    mirror_to_drc();
    OSScreenFlipBuffersEx(SCREEN_TV);
    OSScreenFlipBuffersEx(SCREEN_DRC);
}

/* ---------------------------------------------------------------- boxes */

#define BOX_W_MAX 70

static int wrap_lines(const char *text, int width, char lines[][BOX_W_MAX + 2], int max)
{
    int n = 0;
    const char *p = text;
    while (*p && n < max) {
        /* explicit newline ends the line */
        int len = 0, last_space = -1;
        while (p[len] && p[len] != '\n' && len < width) {
            if (p[len] == ' ')
                last_space = len;
            len++;
        }
        int take = len;
        if (p[len] && p[len] != '\n' && last_space > 0)
            take = last_space;
        memcpy(lines[n], p, take);
        lines[n][take] = '\0';
        n++;
        p += take;
        if (*p == '\n' || *p == ' ')
            p++;
    }
    return n;
}

/* Dialogs are drawn as solid colour windows (no ASCII frames): a coloured
 * title bar, a blue body and a dark button strip at the bottom. */
#define WIN_BG C_BLUE

static int title_fg(int bar)
{
    return (bar == C_RED || bar == C_BLUE || bar == C_MAGENTA) ? C_WHITE : C_BLACK;
}

static void win_blank(int row, int left, int w, int bg)
{
    ui_bg(bg);
    ui_goto(row, left);
    for (int i = 0; i < w; i++)
        ui_putc(' ');
}

/* Body text inside the window, padded to the window width. */
static void win_text(int row, int left, int w, int fg, bool bold, const char *s)
{
    ui_bg(WIN_BG);
    ui_color(fg, bold);
    ui_field(row, left + 2, w - 4, "%s", s);
}

static void win_strip(int row, int left, int w, const char *text)
{
    win_blank(row, left, w, C_CYAN);
    ui_color(C_BLACK, false);
    ui_field(row, left + 2, w - 4, "%s", text);
    ui_reset();
}

/* Draws title bar + body + button strip; returns the first content row. */
static int draw_window(const char *title, int color, int content_rows, int w,
                       const char *strip, int *out_left, int *out_strip_row)
{
    int h = content_rows + 4;                 /* title, pad, content, pad, strip */
    int top = (g_rows - h) / 2;
    int left = (g_cols - w) / 2;
    if (top < 0) top = 0;
    if (left < 0) left = 0;

    win_blank(top, left, w, color);
    ui_color(title_fg(color), true);
    ui_field(top, left + 2, w - 4, "%s", title ? title : "");
    for (int r = 1; r < h - 1; r++)
        win_blank(top + r, left, w, WIN_BG);
    win_strip(top + h - 1, left, w, strip);
    ui_reset();
    *out_left = left;
    if (out_strip_row)
        *out_strip_row = top + h - 1;
    return top + 2;
}

static int box_width(void)
{
    int w = g_cols - 8;
    return w > BOX_W_MAX ? BOX_W_MAX : w;
}

static void draw_text_box(const char *title, int color, const char *text,
                          const char *strip, int extra, int *content_row, int *left)
{
    int w = box_width();
    char lines[16][BOX_W_MAX + 2];
    int n = wrap_lines(text, w - 4, lines, 16);
    int row = draw_window(title, color, n + extra, w, strip, left, NULL);
    for (int i = 0; i < n; i++)
        win_text(row + i, *left, w, C_WHITE, true, lines[i]);
    ui_reset();
    if (content_row)
        *content_row = row + n;
}

void ui_message(const char *title, int color, const char *text)
{
    int left;
    draw_text_box(title, color, text, "A  OK", 0, NULL, &left);
    for (;;) {
        u32 p = DetectInput(IN_DOWN);
        if (p & (BTN_A | BTN_B | BTN_HOME))
            return;
    }
}

void ui_message_pages(const char *title, int color, const char *const *pages, int n)
{
    int i = 0;
    for (;;) {
        char head[96];
        int left;
        snprintf(head, sizeof(head), "%s  (%d/%d)", title, i + 1, n);
        draw_text_box(head, color, pages[i], "L / R  Prev / Next    A  OK", 0, NULL, &left);
        for (;;) {
            u32 p = DetectInput(IN_DOWN);
            if (p & (BTN_A | BTN_B | BTN_HOME))
                return;
            if (p & BTN_PLUS) { i = (i + 1) % n; break; }
            if (p & BTN_MINUS) { i = (i + n - 1) % n; break; }
        }
    }
}

bool ui_confirm(const char *title, int color, const char *text)
{
    int left;
    draw_text_box(title, color, text, "A  Yes        B  No", 0, NULL, &left);
    for (;;) {
        u32 p = DetectInput(IN_DOWN);
        if (p & BTN_A) return true;
        if (p & (BTN_B | BTN_HOME)) return false;
    }
}

int ui_menu(const char *title, const char **items, const bool *enabled, int n, int start)
{
    return ui_menu_ex(title, items, enabled, n, start, "A  Select        B  Back");
}

int ui_menu_ex(const char *title, const char **items, const bool *enabled, int n, int start, const char *strip)
{
    int w = 0;
    for (int i = 0; i < n; i++)
        if ((int)strlen(items[i]) > w) w = strlen(items[i]);
    w += 10;
    if (w < 34) w = 34;
    if (w > g_cols - 4) w = g_cols - 4;
    int sel = start;
    if (sel < 0 || sel >= n) sel = 0;
    int max_rows = g_rows - 8;
    int view = n < max_rows ? n : max_rows;
    int scroll = 0;
    int left, row;
    bool first = true;

    for (;;) {
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + view) scroll = sel - view + 1;
        if (first) {
            row = draw_window(title, C_CYAN, view, w, strip, &left, NULL);
            first = false;
        }
        for (int i = 0; i < view; i++) {
            int idx = scroll + i;
            bool on = idx == sel;
            ui_bg(on ? C_CYAN : WIN_BG);
            if (on)                          ui_color(C_BLACK, false);
            else if (enabled && !enabled[idx]) ui_color(C_WHITE, false);
            else                             ui_color(C_WHITE, true);
            ui_field(row + i, left + 1, w - 2, "  %s", items[idx]);
        }
        ui_reset();

        u32 p = input_pressed();
        if (p & BTN_UP)   sel = (sel + n - 1) % n;
        if (p & BTN_DOWN) sel = (sel + 1) % n;
        if (p & BTN_A)    return sel;
        if (p & BTN_B)    return -1;
        if (p & BTN_HOME) return UI_MENU_HOME;
    }
}

/* Character roller: the Wii has no keyboard, so names are spun like a
 * slot machine. Up/Down changes the char, Left/Right moves, 1 deletes. */
static const char CHARSET[] =
    " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    ".-_()[]+,!#&'@~";

bool ui_text_input(const char *title, char *buf, int maxlen)
{
    int w = box_width();
    int len = strlen(buf);
    int pos = len ? len - 1 : 0;
    int left, strip_row;
    int row = draw_window(title, C_CYAN, 4, w, "A  OK     B  Cancel", &left, &strip_row);
    win_text(row + 2, left, w, C_CYAN, true, "U/D char  L/R move  +/- jump  1 del  2 space");

    for (;;) {
        int vis = w - 6;
        int off = pos >= vis ? pos - vis + 1 : 0;
        ui_goto(row, left + 2);
        for (int i = 0; i < vis; i++) {
            int idx = off + i;
            char c = idx < len ? buf[idx] : ' ';
            if (idx == pos) { ui_bg(C_CYAN); ui_color(C_BLACK, false); }
            else            { ui_bg(C_BLACK); ui_color(C_YELLOW, true); }
            ui_putc(c);
        }
        ui_bg(WIN_BG);
        ui_color(C_WHITE, false);
        ui_field(row + 1, left + 2, w - 4, "%d / %d characters", len, maxlen);
        ui_reset();

        u32 p = input_pressed();
        int cs = sizeof(CHARSET) - 1;
        int step = 0;
        if (p & BTN_UP) step = 1;
        if (p & BTN_DOWN) step = -1;
        if (p & BTN_PLUS) step = 10;
        if (p & BTN_MINUS) step = -10;
        if (step) {
            if (pos >= len) {
                if (len >= maxlen) continue;
                buf[len++] = 'a';
                buf[len] = '\0';
            }
            const char *f = strchr(CHARSET, buf[pos]);
            int idx = f ? (int)(f - CHARSET) : 0;
            idx = ((idx + step) % cs + cs) % cs;
            buf[pos] = CHARSET[idx];
        }
        if (p & BTN_RIGHT) {
            if (pos < len - 1) pos++;
            else if (len < maxlen && pos == len - 1) { buf[len++] = 'a'; buf[len] = '\0'; pos++; }
        }
        if (p & BTN_LEFT) { if (pos > 0) pos--; }
        if (p & BTN_2) {
            if (len < maxlen) {
                memmove(buf + pos + 1, buf + pos, len - pos + 1);
                buf[pos] = ' ';
                len++;
            }
        }
        if (p & BTN_1) {
            if (pos < len) {
                memmove(buf + pos, buf + pos + 1, len - pos);
                len--;
                if (pos >= len && pos > 0) pos--;
            }
        }
        if (p & BTN_A) {
            while (len > 0 && buf[len - 1] == ' ') buf[--len] = '\0';
            return len > 0;
        }
        if (p & (BTN_B | BTN_HOME))
            return false;
    }
}

/* ---------------------------------------------------------------- splash */

void ui_splash(const char *status)
{
    ui_clear();
    int r = g_rows / 2 - 6;
    static const char *art[] = {
        "  ____           _ __  __           _        _   _",
        " / ___| ___   __| |  \\/  | ___   __| | ___  | | | |",
        "| |  _ / _ \\ / _` | |\\/| |/ _ \\ / _` |/ _ \\ | | | |",
        "| |_| | (_) | (_| | |  | | (_) | (_| |  __/ | |_| |",
        " \\____|\\___/ \\__,_|_|  |_|\\___/ \\__,_|\\___|  \\___/",
    };
    int col = (g_cols - 51) / 2;
    for (int i = 0; i < 5; i++) {
        ui_goto(r + i, col);
        ui_puts(art[i]);
        tint(r + i, col, 44, 0xC0C0C0);      /* "GodMode" in the Wii U logo's silver */
        tint(r + i, col + 44, 7, 0x009AC7);  /* and the "U" in its blue */
    }
    ui_reset();
    ui_field(r + 7, 0, g_cols - 1, "%*s%s", (g_cols - (int)strlen(status)) / 2, "", status);
    ui_present();
}
