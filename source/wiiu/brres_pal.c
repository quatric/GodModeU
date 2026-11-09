/*
 * The parsing helper of GodModeWii's model viewer (model.c) that the image viewer uses: which PLT0 a
 * BRRES draws a CI texture with. Taken from model.c unchanged, without the GX viewer around it.
 */
#include <string.h>
#include <gccore.h>
#include "archive.h"
#include "media.h"

typedef struct {
    const u8 *b;
    u32 len, ver;
    /* the BRRES around it: names and textures live outside the MDL0 */
    const u8 *file;
    u32 flen;
    float (*mtx)[12];           /* by node id */
    u8 *has;
    int nmtx;
} mdl_t;

/* A BRRES resource by folder and name ("Textures(NW4R)", "Palettes(NW4R)"): its offset in the
 * file, or 0. */
static u32 brres_find(const u8 *f, u32 flen, const char *folder, const char *name, u32 nlen)
{
    if (flen < 0x10 || memcmp(f, "bres", 4)) return 0;
    u32 root = be16(f + 0x0C);
    if (root + 16 > flen || memcmp(f + root, "root", 4)) return 0;
    u32 g = root + 8, n = be32(f + g + 4);
    for (u32 i = 1; i <= n && g + 8 + (i + 1) * 16 <= flen; i++) {
        const u8 *e = f + g + 8 + i * 16;
        u32 fn = g + be32(e + 8), sub = g + be32(e + 12);
        if (fn >= flen || sub + 8 > flen || strncmp((const char *)f + fn, folder, flen - fn))
            continue;
        u32 sn = be32(f + sub + 4);
        for (u32 k = 1; k <= sn && sub + 8 + (k + 1) * 16 <= flen; k++) {
            const u8 *se = f + sub + 8 + k * 16;
            u32 no = sub + be32(se + 8), at = sub + be32(se + 12);
            if (no + nlen < flen && !memcmp(f + no, name, nlen) && !f[no + nlen] &&
                at + 0x40 <= flen)
                return at;
        }
    }
    return 0;
}

/* A PLT0 in a BRRES: the one the material names (Animal Crossing's CI textures share palettes such as
 * "skin", and swap them for color variants), else the one called like the texture. 0 if neither. */
static u32 brres_palette(const u8 *f, u32 flen, const char *pname, u32 plen, const char *tname, u32 tlen)
{
    u32 pl = pname && plen ? brres_find(f, flen, "Palettes(NW4R)", pname, plen) : 0;
    if (!pl || memcmp(f + pl, "PLT0", 4)) pl = brres_find(f, flen, "Palettes(NW4R)", tname, tlen);
    return pl && !memcmp(f + pl, "PLT0", 4) ? pl : 0;
}

/* The name a layer reference points at (self-relative offset at `at` in the reference, to the
 * characters; the length is the u32 before them), or NULL. The names are in the BRRES's string table,
 * after the MDL0, so they are looked for in the whole file. */
static const char *mdl_ref_name(const mdl_t *d, const u8 *ref, u32 at, u32 *len)
{
    s32 rel = (s32)be32(ref + at);
    *len = 0;
    if (!rel) return NULL;
    s64 o = (s64)(ref - d->file) + rel;
    if (o < 4 || o >= (s64)d->flen) return NULL;
    const u8 *nm = d->file + o;
    u32 n = be32(nm - 4);
    if (!n || n > 255 || (u64)o + n > d->flen) return NULL;
    *len = n;
    return (const char *)nm;
}

/* The PLT0 that the BRRES's models draw the texture `tex` with: the palette named by the first
 * material layer that uses it, else one called like the texture, else the only palette there.
 * Offset into b, or 0. For the image viewer, which shows one TEX0 at a time. */
u32 model_brres_texture_palette(const u8 *b, u32 len, const char *tex)
{
    u32 tlen = (u32)strlen(tex);
    if (len < 0x10 || memcmp(b, "bres", 4) || !tlen) return 0;
    u32 root = be16(b + 0x0C);
    if (root + 16 > len || memcmp(b + root, "root", 4)) return 0;
    u32 g = root + 8, n = be32(b + g + 4), pals = 0, models = 0;
    for (u32 i = 1; i <= n && g + 8 + (i + 1) * 16 <= len; i++) {
        const u8 *e = b + g + 8 + i * 16;
        u32 fn = g + be32(e + 8), sub = g + be32(e + 12);
        if (fn >= len || sub + 8 > len) continue;
        if (!strncmp((const char *)b + fn, "Palettes(NW4R)", len - fn)) pals = sub;
        if (!strncmp((const char *)b + fn, "3DModels(NW4R)", len - fn)) models = sub;
    }
    if (!pals) return 0;
    for (u32 k = 1; models && k <= be32(b + models + 4) && models + 8 + (k + 1) * 16 <= len; k++) {
        u32 mo = models + be32(b + models + 8 + k * 16 + 12);
        if (mo + 0x40 > len || memcmp(b + mo, "MDL0", 4)) continue;
        u32 ver = be32(b + mo + 8), msize = be32(b + mo + 4);
        if (msize > len - mo) msize = len - mo;
        int si = ver >= 10 ? 8 : 6;
        s32 mg = (s32)be32(b + mo + 0x10 + si * 4);
        if (mg <= 0 || (u32)mg + 8 > msize) continue;
        mdl_t d = { 0 };
        d.b = b + mo;
        d.len = msize;
        d.file = b;
        d.flen = len;
        u32 grp = (u32)mg, cnt = be32(d.b + grp + 4);
        for (u32 j = 1; j <= cnt && grp + 8 + (j + 1) * 16 <= msize; j++) {
            u32 mat = grp + be32(d.b + grp + 8 + j * 16 + 12);
            if (mat + 0x34 > msize) continue;
            u32 nl = be32(d.b + mat + 0x2C);
            s32 refs = (s32)be32(d.b + mat + 0x30);
            if (nl > 8 || refs <= 0) continue;
            for (u32 l = 0; l < nl && mat + refs + (l + 1) * 0x34 <= msize; l++) {
                const u8 *ref = d.b + mat + refs + l * 0x34;
                u32 a, pl;
                const char *tn = mdl_ref_name(&d, ref, 0, &a);
                if (!tn || a != tlen || memcmp(tn, tex, tlen)) continue;
                const char *pn = mdl_ref_name(&d, ref, 4, &pl);
                u32 at = pn ? brres_palette(b, len, pn, pl, tex, tlen) : 0;
                if (at) return at;
            }
        }
    }
    u32 at = brres_palette(b, len, NULL, 0, tex, tlen);
    if (at) return at;
    if (be32(b + pals + 4) == 1 && pals + 8 + 32 <= len) {
        at = pals + be32(b + pals + 8 + 16 + 12);
        if (at + 0x20 <= len && !memcmp(b + at, "PLT0", 4)) return at;
    }
    return 0;
}
