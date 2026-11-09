#ifndef WIIUFMT_H
#define WIIUFMT_H

#include <stdbool.h>
#include <stdint.h>

/* Wii U formats (wiiufmt/), after quatric's nintoolbox:
 *   textures (gx2tex.c)     GTX, BFLIM, BFFNT, BFRES, Tropical Freeze TXTR, NUT, DDS
 *   archives (arc_wiiu.c)   SARC, BARS, BFSAR, BFWAR, NUS3AUDIO (declared in archive.h)
 *   text (text_wiiu.c)      BYAML / BYML, XMB, AAMP / BGLPBD, BFLYT, BFLAN, BFSEQ
 * BFSTM / BFWAV / BTSND play through the audio player like the Wii's streams. */

/* ---------------------------------------------------------------- textures */

/* One texture, described (pointing into the file's buffer) until it is shown. */
typedef struct {
    unsigned fmt, w, h, tile, swizzle, pitch;  /* GX2 surface */
    const uint8_t *data;
    uint32_t size;
    uint8_t *own;                              /* data this texture owns (decompressed), or NULL */
    bool has_sel;
    uint8_t sel[4];                            /* GX2 component selectors */
    /* linear data (DDS, NTP3) instead of a GX2 surface */
    bool linear;
    int bc;                                    /* 1-5: BCn blocks */
    unsigned lin_bpp;                          /* bytes per pixel otherwise */
    int rgb565;                                /* 1: little endian RGB565, 2: big endian */
    int chan[4];                               /* byte of R, G, B, A in a pixel (-1: 255) */
} wtex_t;

/* Takes the texture (frees it with wtex_free when done); false: no room, stop. */
typedef bool (*wtex_add_fn)(void *ctx, wtex_t *t, const char *label);

/* A Wii U texture file by its first bytes (and the name, for BFLIM's footer). */
bool wiiu_image_sniff(const uint8_t *head, uint32_t n, uint64_t size, const char *name);
/* Describes every texture in the file; returns its kind ("GTX", ...) or NULL when there are none. */
const char *wiiu_textures(const uint8_t *b, uint32_t n, const char *name, wtex_add_fn add, void *ctx);
/* Expands one to RGBA (w * h * 4 bytes). */
bool wtex_decode(const wtex_t *t, uint8_t *rgba);
const char *wtex_fmt_name(const wtex_t *t);
void wtex_free(wtex_t *t);

/* ---------------------------------------------------------------- text */

enum { WT_NONE, WT_BYML, WT_XMB, WT_AAMP, WT_FLYT, WT_FLAN, WT_FSEQ };

/* Which of the text formats a file is (0: none), by its first bytes. */
int wiiu_text_kind(const uint8_t *head, uint32_t n, const char *name);
/* The file as text (malloc'd, *len bytes); NULL when it can't be read. */
char *wiiu_text_dump(const uint8_t *b, uint32_t n, const char *name, const char **kind, uint32_t *len);
/* Loads the file and shows it in the text viewer; false when it isn't one of these. */
bool wiiu_text_view(const char *path);

#endif
