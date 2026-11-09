/*
 * A text console in the 640 x 480 YUY2 framebuffer that behaves like libogc's: the same 8 x 16 font,
 * the same 16 pixel margin (76 x 28 cells), the same ANSI escapes and the same 24-entry colour table
 * (normal, bright and dim sets of the eight ANSI colours, as YUY2 pixel pairs), so GodModeWii's
 * interface code (and its colour themes, which rewrite that table) prints to it unchanged through
 * stdout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/iosupport.h>

#include "console.h"
#include "screen.h"

extern const uint8_t console_font_8x16[4096];

#define X0 16
#define Y0 16
#define CW 8
#define CH 16

static int cols = (SCREEN_W - 2 * X0) / CW, rows = (SCREEN_H - 2 * Y0) / CH;
static int cx, cy, saved_x, saved_y;
static int fg = 7, bg = 0;
static bool bold, dim;

static uint32_t table[24];

static uint32_t yuy2_of(int r, int g, int b)
{
    int y = 16 + ((66 * r + 129 * g + 25 * b + 128) >> 8);
    int u = 128 + ((-38 * r - 74 * g + 112 * b + 128) >> 8);
    int v = 128 + ((112 * r - 94 * g - 18 * b + 128) >> 8);
    return (uint32_t)y << 24 | (uint32_t)u << 16 | (uint32_t)y << 8 | (uint32_t)v;
}

/* libogc's colours: the ANSI eight at half, full and a quarter brightness, white a light gray. */
void con_reset_colors(void)
{
    static const uint8_t base[8][3] = {
        { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 0, 0, 1 }, { 1, 0, 1 }, { 0, 1, 1 }, { 1, 1, 1 },
    };
    for (int c = 0; c < 8; c++) {
        int n = c == 7 ? 192 : 128;
        table[c] = yuy2_of(base[c][0] * n, base[c][1] * n, base[c][2] * n);
        table[8 + c] = c == 0 ? yuy2_of(128, 128, 128) : yuy2_of(base[c][0] * 255, base[c][1] * 255, base[c][2] * 255);
        table[16 + c] = yuy2_of(base[c][0] * n / 2, base[c][1] * n / 2, base[c][2] * n / 2);
    }
}

uint32_t *con_color_table(void) { return table; }
int con_cols(void) { return cols; }
int con_rows(void) { return rows; }

static uint32_t fg_word(void) { return table[(dim ? 16 : bold ? 8 : 0) + fg]; }
static uint32_t bg_word(void) { return table[bg]; }

/* One pixel pair: each half's luma from its own colour, the chroma averaged. */
static inline uint32_t pair(uint32_t a, uint32_t b)
{
    if (a == b) return a;
    uint32_t u = ((a >> 16 & 0xff) + (b >> 16 & 0xff)) / 2, v = ((a & 0xff) + (b & 0xff)) / 2;
    return (a & 0xff000000) | u << 16 | (b & 0x0000ff00) | v;
}

static void draw_char(int col, int row, uint8_t ch)
{
    if (col < 0 || col >= cols || row < 0 || row >= rows) return;
    uint32_t *fb = (uint32_t *)screen_xfb();
    const uint8_t *g = console_font_8x16 + ch * 16;
    uint32_t f = fg_word(), b = bg_word();
    uint32_t pw[4] = { b, pair(b, f), pair(f, b), f };   /* 00, 01, 10, 11 */
    int x = (X0 + col * CW) / 2;
    for (int y = 0; y < CH; y++) {
        uint32_t *line = fb + (Y0 + row * CH + y) * (SCREEN_W / 2) + x;
        uint8_t bits = g[y];
        line[0] = pw[bits >> 6 & 3];
        line[1] = pw[bits >> 4 & 3];
        line[2] = pw[bits >> 2 & 3];
        line[3] = pw[bits & 3];
    }
}

static void clear_cells(int row, int c0, int c1)
{
    uint32_t *fb = (uint32_t *)screen_xfb();
    uint32_t b = bg_word();
    if (c0 < 0) c0 = 0;
    if (c1 > cols) c1 = cols;
    for (int y = 0; y < CH; y++) {
        uint32_t *line = fb + (Y0 + row * CH + y) * (SCREEN_W / 2) + (X0 + c0 * CW) / 2;
        for (int i = 0; i < (c1 - c0) * CW / 2; i++) line[i] = b;
    }
}

static void scroll_up(void)
{
    uint8_t *fb = screen_xfb();
    int pitch = SCREEN_W * 2;
    for (int y = Y0; y < Y0 + (rows - 1) * CH; y++)
        memmove(fb + y * pitch + X0 * 2, fb + (y + CH) * pitch + X0 * 2, cols * CW * 2);
    clear_cells(rows - 1, 0, cols);
}

static void newline(void)
{
    cx = 0;
    if (++cy >= rows) {
        scroll_up();
        cy = rows - 1;
    }
}

void con_clear(void)
{
    for (int r = 0; r < rows; r++) clear_cells(r, 0, cols);
    cx = cy = 0;
}

/* ---------------------------------------------------------------- escapes */

static void sgr(const int *p, int n)
{
    if (!n) { fg = 7; bg = 0; bold = dim = false; return; }
    for (int i = 0; i < n; i++) {
        int v = p[i];
        if (v == 0) { fg = 7; bg = 0; bold = dim = false; }
        else if (v == 1) { bold = true; dim = false; }
        else if (v == 2) { dim = true; bold = false; }
        else if (v == 22) bold = dim = false;
        else if (v >= 30 && v <= 37) fg = v - 30;
        else if (v == 39) fg = 7;
        else if (v >= 40 && v <= 47) bg = v - 40;
        else if (v == 49) bg = 0;
        else if (v >= 90 && v <= 97) { fg = v - 90; bold = true; }
    }
}

static void escape(char cmd, const int *p, int n)
{
    int a = n > 0 ? p[0] : 0, b = n > 1 ? p[1] : 0;
    switch (cmd) {
    case 'H': case 'f':
        cy = (a ? a : 1) - 1;
        cx = (b ? b : 1) - 1;
        if (cy < 0) cy = 0;
        if (cy >= rows) cy = rows - 1;
        if (cx < 0) cx = 0;
        if (cx > cols) cx = cols;
        break;
    case 'A': cy -= a ? a : 1; if (cy < 0) cy = 0; break;
    case 'B': cy += a ? a : 1; if (cy >= rows) cy = rows - 1; break;
    case 'C': cx += a ? a : 1; if (cx > cols) cx = cols; break;
    case 'D': cx -= a ? a : 1; if (cx < 0) cx = 0; break;
    case 'J':
        if (a == 2) con_clear();
        else if (a == 0) {
            clear_cells(cy, cx, cols);
            for (int r = cy + 1; r < rows; r++) clear_cells(r, 0, cols);
        }
        break;
    case 'K':
        if (a == 0) clear_cells(cy, cx, cols);
        else if (a == 1) clear_cells(cy, 0, cx + 1);
        else clear_cells(cy, 0, cols);
        break;
    case 'm': sgr(p, n); break;
    case 's': saved_x = cx; saved_y = cy; break;
    case 'u': cx = saved_x; cy = saved_y; break;
    }
}

static enum { S_TEXT, S_ESC, S_CSI } state;
static int params[8], nparams;
static bool have_digit;

void con_write(const char *s, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)s[i];
        if (state == S_ESC) {
            if (c == '[') {
                state = S_CSI;
                nparams = 0;
                have_digit = false;
                memset(params, 0, sizeof(params));
            } else {
                state = S_TEXT;
            }
            continue;
        }
        if (state == S_CSI) {
            if (c >= '0' && c <= '9') {
                if (nparams < 8) {
                    params[nparams] = params[nparams] * 10 + (c - '0');
                    have_digit = true;
                }
            } else if (c == ';') {
                if (nparams < 8) nparams++;
                have_digit = false;
            } else {
                int n = nparams + (have_digit || nparams ? 1 : 0);
                if (n > 8) n = 8;
                escape((char)c, params, n);
                state = S_TEXT;
            }
            continue;
        }
        switch (c) {
        case 0x1b: state = S_ESC; break;
        case '\n': newline(); break;
        case '\r': cx = 0; break;
        case '\b': if (cx > 0) cx--; break;
        case '\t': cx = (cx + 4) & ~3; if (cx >= cols) newline(); break;
        default:
            if (cx >= cols) newline();
            draw_char(cx++, cy, c);
            break;
        }
    }
}

/* ---------------------------------------------------------------- stdout */

static ssize_t con_write_r(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    (void)r;
    (void)fd;
    con_write(ptr, len);
    return (ssize_t)len;
}

static ssize_t discard_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    (void)r;
    (void)fd;
    (void)ptr;
    return (ssize_t)len;
}

static const devoptab_t con_tab = { .name = "stdout", .write_r = con_write_r };
static const devoptab_t discard_tab = { .name = "stderr", .write_r = discard_write };

void con_init(void)
{
    con_reset_colors();
    devoptab_list[STD_OUT] = &con_tab;
    /* libraries (unarr, libpng, libjpeg) print warnings to stderr, which would land on the screen */
    devoptab_list[STD_ERR] = &discard_tab;
    setvbuf(stdout, NULL, _IONBF, 0);
    con_clear();
}
