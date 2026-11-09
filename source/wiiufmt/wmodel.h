#ifndef WMODEL_H
#define WMODEL_H

#include <stdbool.h>
#include <stdint.h>

/* Wii U BFRES models drawn in software (wmodel.c). Plain C: no console dependencies. */

typedef struct wmodel wmodel_t;

enum { WM_LIT, WM_FLAT, WM_WIRE, WM_SHADES };

typedef struct {
    const char *name;
    uint32_t triangles, vertices;
    int shapes, textured, textures, bones;
} wmodel_info_t;

/* How many models (FMDLs with shapes) the BFRES has; 0 when it isn't a Wii U BFRES or has none. */
int wmodel_count(const uint8_t *b, uint32_t n);
/* Model idx (0-based) with the textures its materials use; NULL with *err set. The buffer may be freed
 * afterwards. */
wmodel_t *wmodel_load(const uint8_t *b, uint32_t n, int idx, const char **err);
void wmodel_free(wmodel_t *m);
void wmodel_info(const wmodel_t *m, wmodel_info_t *i);
/* Draws it into rgb (w * h * 3 bytes), turned by yaw / pitch (degrees), dist model radii away. */
void wmodel_render(const wmodel_t *m, uint8_t *rgb, int w, int h, float yaw, float pitch, float dist, int shade);

#endif
