#ifndef WLAYOUT_H
#define WLAYOUT_H

#include <stdbool.h>
#include <stdint.h>

/* Wii U BFLYT layouts drawn in software (wlayout.c): picture, text, window and part panes with their
 * materials' first texture, vertex colours, transforms and alpha. Plain C: no console dependencies.
 * Text is drawn in the 8x16 console font (the layouts' own fonts are the system's). */

typedef struct wlayout wlayout_t;

/* Fetches a file the layout names (a texture "Name^t.bflim", a part "Name.bflyt"): malloc'd bytes, or
 * NULL when it isn't there. */
typedef uint8_t *(*wl_load_fn)(void *ctx, const char *name, uint32_t *len);

typedef struct {
    float w, h;                             /* the layout's screen */
    int panes, pictures, texts, windows, parts, hidden;
    int textures, missing;                  /* textures named, and how many of those weren't found */
} wlayout_info_t;

enum {
    WL_HIDDEN = 1,                          /* draw hidden panes too */
    WL_BOXES = 2,                           /* outline every pane */
};

/* The layout in b (which may be freed afterwards); its textures and parts come through load. NULL with
 * *err set. */
wlayout_t *wlayout_load(const uint8_t *b, uint32_t n, wl_load_fn load, void *ctx, const char **err);
void wlayout_free(wlayout_t *l);
void wlayout_info(const wlayout_t *l, wlayout_info_t *i);
/* Draws it into rgb (w * h * 3 bytes), fitted, then zoomed by zoom around the center moved by pan_x / pan_y
 * (layout units). font: the 8x16 font, 256 glyphs of 16 bytes. */
void wlayout_render(const wlayout_t *l, uint8_t *rgb, int w, int h, float zoom, float pan_x, float pan_y, int flags,
                    const uint8_t *font);

/* Layout animations (BFLAN): a file's animation of panes and materials by name (pane SRT, visibility, vertex
 * colours and alpha, material colours, the first texture's SRT and pattern). */
typedef struct wlanim wlanim_t;
wlanim_t *wlanim_load(const uint8_t *b, uint32_t n, const char **err);
void wlanim_free(wlanim_t *a);
const char *wlanim_name(const wlanim_t *a);
int wlanim_frames(const wlanim_t *a);
/* How many of the animation's panes and materials this layout has. */
int wlayout_anim_matches(const wlayout_t *l, const wlanim_t *a);
/* Picks the animation (its textures come through load); NULL goes back to the layout as it was. Returns how many
 * of its panes and materials were found. The animation must outlive its use. */
int wlayout_set_anim(wlayout_t *l, const wlanim_t *a, wl_load_fn load, void *ctx);
/* Poses the layout at `frame` (30 a second, looped) of the animation set. */
void wlayout_pose(wlayout_t *l, float frame);

#endif
