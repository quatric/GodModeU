/*
 * The layout viewer on the Wii U: BFLYT layouts drawn in software (wiiufmt/wlayout.c) into the console
 * framebuffer, with the textures and parts found beside them (the archive's timg/ next to its blyt/). The
 * text dump of the panes is one button away.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "media.h"
#include "ui.h"
#include "input.h"
#include "pointer.h"
#include "vfs.h"
#include "wiiufmt.h"
#include "wiiufmt/wlayout.h"

#define TOP    48                           /* the two text lines above */
#define BOTTOM 32                           /* and the key strip below */
#define MAX_FILE (32u << 20)

extern const uint8_t console_font_8x16[4096];

static u8 *load_all(const char *path, u32 *len, u32 min)
{
    vfs_file f;
    if (!vfs_open_read(&f, path)) return NULL;
    if (f.size > MAX_FILE || f.size < min) {
        vfs_close(&f);
        return NULL;
    }
    u32 n = (u32)f.size, got = 0;
    u8 *b = memalign(32, (n + 31) & ~31u);
    while (b && got < n) {
        s32 r = vfs_read(&f, b + got, n - got);
        if (r <= 0) break;
        got += r;
    }
    vfs_close(&f);
    if (b && got != n) {
        free(b);
        return NULL;
    }
    *len = n;
    return b;
}

/* where the layout is: its folder, and the folder above when that is blyt/ */
typedef struct {
    char dir[512], up[512];
} where_t;

static u8 *fetch(void *ctx, const char *name, uint32_t *len)
{
    where_t *w = ctx;
    const char *tries[] = { "%s/timg/%s", "%s/%s", "%s/timg/%s", "%s/blyt/%s", "%s/%s" };
    const char *base[] = { w->up, w->dir, w->dir, w->up, w->up };
    char p[1024];
    for (int i = 0; i < 5; i++) {
        if (!base[i][0]) continue;
        snprintf(p, sizeof(p), tries[i], base[i], name);
        u32 n;
        u8 *b = load_all(p, &n, 8);
        if (b) {
            *len = n;
            return b;
        }
    }
    return NULL;
}

/* ITU-R BT.601, TV ranges, as the image viewer converts */
static u32 yuy2_pair(const u8 *a, const u8 *b)
{
    int y0 = 16 + ((66 * a[0] + 129 * a[1] + 25 * a[2] + 128) >> 8);
    int y1 = 16 + ((66 * b[0] + 129 * b[1] + 25 * b[2] + 128) >> 8);
    int r = (a[0] + b[0]) / 2, g = (a[1] + b[1]) / 2, bl = (a[2] + b[2]) / 2;
    int u = 128 + ((-38 * r - 74 * g + 112 * bl + 128) >> 8);
    int v = 128 + ((112 * r - 94 * g - 18 * bl + 128) >> 8);
    return (u32)y0 << 24 | (u32)u << 16 | (u32)y1 << 8 | (u32)v;
}

static void show(const u8 *rgb, int w, int h)
{
    int fw, fh;
    u8 *fb = ui_framebuffer(&fw, &fh);
    if (!fb) return;
    for (int y = 0; y < h && TOP + y < fh; y++) {
        /* uncached: whole words only (see image.c) */
        u32 *row = (u32 *)(fb + (TOP + y) * fw * 2);
        const u8 *s = rgb + (size_t)y * w * 3;
        for (int x = 0; x + 1 < w && x + 1 < fw; x += 2) row[x / 2] = yuy2_pair(s + x * 3, s + x * 3 + 3);
    }
}

bool flyt_view(const char *path)
{
    u32 len;
    u8 *b = load_all(path, &len, 0x14);
    if (!b) return false;
    if (memcmp(b, "FLYT", 4)) {
        free(b);
        return false;
    }
    ui_splash("Loading layout...");
    where_t w;
    snprintf(w.dir, sizeof(w.dir), "%s", path);
    char *slash = strrchr(w.dir, '/');
    if (slash) *slash = 0;
    else w.dir[0] = 0;
    snprintf(w.up, sizeof(w.up), "%s", w.dir);
    slash = strrchr(w.up, '/');
    if (slash && !strcmp(slash, "/blyt")) *slash = 0;
    else w.up[0] = 0;

    const char *err = NULL;
    wlayout_t *l = wlayout_load(b, len, fetch, &w, &err);
    free(b);
    if (!l) return false;                   /* the text dump instead */

    int fw, fh;
    ui_framebuffer(&fw, &fh);
    int vw = fw & ~1, vh = fh - TOP - BOTTOM;
    u8 *rgb = malloc((size_t)vw * vh * 3);
    if (!rgb) {
        wlayout_free(l);
        ui_message("LAYOUT", C_RED, "Not enough memory to draw this layout.");
        return true;
    }

    wlayout_info_t in;
    wlayout_info(l, &in);
    float zoom = 1, px = 0, py = 0;
    int flags = 0;
    bool redraw = true;
    ptr_drag drag = { 0 };
    ptr_begin();
    ui_clear();

    for (;;) {
        if (redraw) {
            ptr_hide();
            wlayout_render(l, rgb, vw, vh, zoom, px, py, flags, console_font_8x16);
            show(rgb, vw, vh);
            ui_color(C_WHITE, true);
            ui_field(0, 0, ui_cols() - 1, " LAYOUT  %s  %gx%g  x%.2g%s%s", vfs_basename(path), (double)in.w,
                     (double)in.h, (double)zoom, flags & WL_HIDDEN ? "  +hidden" : "", flags & WL_BOXES ? "  +boxes" : "");
            ui_field(1, 0, ui_cols() - 1, " BFLYT  %d panes: %d pictures, %d texts, %d windows, %d parts (%d hidden)  %d/%d textures",
                     in.panes, in.pictures, in.texts, in.windows, in.parts, in.hidden, in.textures - in.missing,
                     in.textures);
            ui_color(C_CYAN, true);
            ui_field(ui_rows() - 1, 0, ui_cols() - 1,
                     " D-Pad/Point+A drag Move  +/- Zoom  A Outlines  1 Hidden panes  2 Pane list  B Close");
            ui_reset();
            redraw = false;
        }

        ptr_show();
        u32 p = DetectInput(IN_DOWN), held = INPUT_ButtonsHeld();
        if (p & (WPAD_BUTTON_B | WPAD_BUTTON_HOME)) break;

        float fit = (vw / in.w < vh / in.h ? vw / in.w : vh / in.h) * zoom;
        int pdx, pdy, pev = ptr_drag_update(&drag, held, &pdx, &pdy);
        if (pev == PD_MOVE) {
            px -= pdx / fit;
            py += pdy / fit;
            redraw = true;
        }
        if (pev == PD_TAP) {
            flags ^= WL_BOXES;
            redraw = true;
        }
        int wheel = ptr_wheel();
        if (wheel) {
            zoom *= wheel < 0 ? 1.1f : 0.9f;
            redraw = true;
        }
        if (held & (WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT | WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_PLUS |
                    WPAD_BUTTON_MINUS)) {
            float step = 12 / fit;
            if (held & WPAD_BUTTON_LEFT) px -= step;
            if (held & WPAD_BUTTON_RIGHT) px += step;
            if (held & WPAD_BUTTON_UP) py += step;
            if (held & WPAD_BUTTON_DOWN) py -= step;
            if (held & WPAD_BUTTON_PLUS) zoom *= 1.05f;
            if (held & WPAD_BUTTON_MINUS) zoom /= 1.05f;
            redraw = true;
        }
        if (zoom < 0.25f) zoom = 0.25f;
        if (zoom > 16) zoom = 16;

        if ((p & WPAD_BUTTON_A) && !ptr_drag_owns_a(&drag)) {
            flags ^= WL_BOXES;
            redraw = true;
        }
        if (p & WPAD_BUTTON_1) {
            flags ^= WL_HIDDEN;
            redraw = true;
        }
        if (p & WPAD_BUTTON_2) {
            ptr_hide();
            wiiu_text_view(path);
            ptr_begin();
            ui_clear();
            redraw = true;
        }
    }

    ptr_hide();
    free(rgb);
    wlayout_free(l);
    ui_clear();
    return true;
}
