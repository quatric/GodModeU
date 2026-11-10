/*
 * The 3D model viewer on the Wii U: BFRES models drawn in software (wiiufmt/wmodel.c) into the console
 * framebuffer, between the title lines and the key strip. GodModeWii's GX viewer (BRRES, BMD, ...) has no
 * Cafe OS equivalent; a BFRES without a model goes on to the image viewer (its textures).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <coreinit/time.h>

#include "media.h"
#include "ui.h"
#include "input.h"
#include "pointer.h"
#include "vfs.h"
#include "wiiufmt/wmodel.h"

#define TOP    48                           /* the two text lines above */
#define BOTTOM 32                           /* and the key strip below */
#define MAX_FILE (48u << 20)

static u8 *load_all(const char *path, u32 *len)
{
    vfs_file f;
    if (!vfs_open_read(&f, path)) return NULL;
    if (f.size > MAX_FILE || f.size < 0x60) {
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

bool model_view(const char *path)
{
    u32 len;
    ui_splash("Loading model...");
    u8 *b = load_all(path, &len);
    if (!b) return false;
    int count = wmodel_count(b, len);
    if (!count) {
        free(b);
        return false;                       /* textures only: the image viewer */
    }

    int cur = 0;
    const char *err = NULL;
    wmodel_t *m = wmodel_load(b, len, cur, &err);
    if (!m) {
        free(b);
        ui_message("MODEL", C_YELLOW, err ? err : "This model couldn't be read.");
        return true;
    }

    int fw, fh;
    ui_framebuffer(&fw, &fh);
    int w = fw & ~1, h = fh - TOP - BOTTOM;
    u8 *rgb = malloc((size_t)w * h * 3);
    if (!rgb) {
        wmodel_free(m);
        free(b);
        ui_message("MODEL", C_RED, "Not enough memory to draw this model.");
        return true;
    }

    float yaw = 30, pitch = 15, dist = 2.6f;
    int shade = WM_LIT;
    int anim = -1;                          /* the bind pose, or the animation playing (30 frames a second) */
    OSTime t0 = OSGetSystemTime();
    float frame = 0;
    bool redraw = true;
    ptr_drag drag = { 0 };
    ptr_begin();
    ui_clear();

    for (;;) {
        if (anim >= 0) {
            frame = (float)OSTicksToMilliseconds(OSGetSystemTime() - t0) * 0.03f;
            wmodel_pose(m, anim, frame);
            redraw = true;
        }
        if (redraw) {
            ptr_hide();
            wmodel_render(m, rgb, w, h, yaw, pitch, dist, shade);
            show(rgb, w, h);
            wmodel_info_t in;
            wmodel_info(m, &in);
            ui_color(C_WHITE, true);
            ui_field(0, 0, ui_cols() - 1, " MODEL  %s  [%d/%d] %s", vfs_basename(path), cur + 1, count, in.name);
            const char *sh = shade == WM_LIT ? "lit" : shade == WM_FLAT ? "unlit" : "wireframe";
            if (anim >= 0)
                ui_field(1, 0, ui_cols() - 1, " BFRES  animation %d/%d  %s  frame %d/%d  %d bones  %s", anim + 1,
                         wmodel_anim_count(m), wmodel_anim_name(m, anim),
                         (int)fmodf(frame, (float)(wmodel_anim_frames(m, anim) > 1 ? wmodel_anim_frames(m, anim) - 1 : 1)),
                         wmodel_anim_frames(m, anim), in.bones, sh);
            else
                ui_field(1, 0, ui_cols() - 1, " BFRES  %u triangles  %d shapes (%d textured, %d textures)  %d bones  %s%s",
                         (unsigned)in.triangles, in.shapes, in.textured, in.textures, in.bones, sh,
                         wmodel_anim_count(m) ? "  (bind pose)" : "");
            ui_color(C_CYAN, true);
            ui_field(ui_rows() - 1, 0, ui_cols() - 1,
                     wmodel_anim_count(m) ? " D-Pad/Point+A drag Rotate  +/- Zoom  A Shading  1 Animation  2 Textures  B Close"
                     : count > 1          ? " D-Pad/Point+A drag Rotate  +/- Zoom  A Shading  1 Next  2 Textures  B Close"
                                          : " D-Pad/Point+A drag Rotate  +/- Zoom  A Shading  2 Textures  B Close");
            ui_reset();
            redraw = false;
        }

        ptr_show();
        u32 p = DetectInput(IN_DOWN), held = INPUT_ButtonsHeld();
        if (p & (WPAD_BUTTON_B | WPAD_BUTTON_HOME)) break;

        int pdx, pdy, pev = ptr_drag_update(&drag, held, &pdx, &pdy);
        if (pev == PD_MOVE) {
            yaw += pdx * 0.5f;
            pitch += pdy * 0.5f;
            redraw = true;
        }
        if (pev == PD_TAP) {
            shade = (shade + 1) % WM_SHADES;
            redraw = true;
        }
        int wheel = ptr_wheel();
        if (wheel) {
            dist *= wheel < 0 ? 0.9f : 1.1f;
            redraw = true;
        }
        if (held & (WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT | WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_PLUS |
                    WPAD_BUTTON_MINUS)) {
            if (held & WPAD_BUTTON_LEFT) yaw -= 4;
            if (held & WPAD_BUTTON_RIGHT) yaw += 4;
            if (held & WPAD_BUTTON_UP) pitch -= 4;
            if (held & WPAD_BUTTON_DOWN) pitch += 4;
            if (held & WPAD_BUTTON_PLUS) dist *= 0.96f;
            if (held & WPAD_BUTTON_MINUS) dist *= 1.04f;
            redraw = true;
        }
        if (dist < 0.3f) dist = 0.3f;
        if (dist > 40) dist = 40;

        if ((p & WPAD_BUTTON_A) && !ptr_drag_owns_a(&drag)) {
            shade = (shade + 1) % WM_SHADES;
            redraw = true;
        }
        if ((p & WPAD_BUTTON_1) && (count > 1 || wmodel_anim_count(m))) {
            /* bind pose -> each animation of the model -> the next model's bind pose */
            if (anim + 1 < wmodel_anim_count(m)) {
                anim++;
                t0 = OSGetSystemTime();
            } else {
                anim = -1;
                wmodel_pose(m, -1, 0);
                if (count > 1) {
                    ptr_hide();
                    ui_splash("Loading model...");
                    for (int tries = 0; tries < count; tries++) {
                        cur = (cur + 1) % count;
                        wmodel_t *nm = wmodel_load(b, len, cur, &err);
                        if (nm) {
                            wmodel_free(m);
                            m = nm;
                            break;
                        }
                    }
                    yaw = 30, pitch = 15, dist = 2.6f;
                    ui_clear();
                }
            }
            redraw = true;
        }
        if (p & WPAD_BUTTON_2) {
            ptr_hide();
            image_view(path);
            ptr_begin();
            ui_clear();
            redraw = true;
        }
    }

    ptr_hide();
    free(rgb);
    wmodel_free(m);
    free(b);
    ui_clear();
    return true;
}
