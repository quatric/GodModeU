/*
 * Wii U BFRES models (FMDL), drawn by a small software renderer into an RGB buffer: GodModeWii's model
 * viewer draws with GX, which a Cafe OS app doesn't have.
 *
 * The layout follows quatric's nintoolbox (lib-bfres.c, itself after KillzXGaming's NintenTools.Bfres and
 * mk8.tockdom.com's FMDL pages):
 *   FRES    index group of FMDLs at 0x20 (count at 0x50), of FTEXs at 0x24.
 *   FMDL    FSKL at 0x0C, the FVTX array at 0x10, index groups of FSHPs (0x14) and FMATs (0x18).
 *   FVTX    attributes (_p0 position, _n0 normal, _u0 texture coordinates, _i0 bone indices) in buffers.
 *   FSHP    its material, bone and FVTX; the vertex skin count (0: the shape follows its bone, 1: each
 *           vertex follows one bone, 2+: already in model space); LOD meshes (primitive type, index
 *           format and count, the index buffer).
 *   FMAT    texture references (name, FTEX); the first is drawn.
 *   FSKL    bones (scale, rotation as Euler XYZ or a quaternion, translation, parent) and the matrix to
 *           bone table that one-bone skinning indexes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "wmodel.h"
#include "../wiiufmt.h"

#define MAX_TEX_SIDE 256                    /* textures are kept at most this size (sampling is nearest) */

static uint32_t b32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t b16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static float bf(const uint8_t *p)
{
    union { uint32_t u; float f; } c = { b32(p) };
    return isfinite(c.f) ? c.f : 0;
}

typedef struct {
    const uint8_t *b;
    uint32_t n;
} buf_t;

/* a self-relative offset at `at`: where it points, or 0 when it doesn't fit */
static uint32_t rel(const buf_t *d, uint32_t at)
{
    if ((uint64_t)at + 4 > d->n) return 0;
    int32_t o = (int32_t)b32(d->b + at);
    if (!o) return 0;
    int64_t to = (int64_t)at + o;
    return to > 0 && to < d->n ? (uint32_t)to : 0;
}

static const char *rstr(const buf_t *d, uint32_t at)
{
    uint32_t p = rel(d, at);
    if (!p || !memchr(d->b + p, 0, d->n - p)) return "";
    return (const char *)d->b + p;
}

/* entry i of an index group (its data), or 0 */
static uint32_t group_item(const buf_t *d, uint32_t grp, uint32_t i)
{
    if (!grp || (uint64_t)grp + 8 > d->n) return 0;
    uint32_t cnt = b32(d->b + grp + 4);
    if (i >= cnt || (uint64_t)grp + 8 + (i + 2) * 16ull > d->n) return 0;
    return rel(d, grp + 8 + (i + 1) * 16 + 12);
}

static float half(uint16_t h)
{
    int s = h >> 15, e = h >> 10 & 31, m = h & 1023;
    float v = e == 0 ? m / 16777216.0f : e == 31 ? 0 : ldexpf(1 + m / 1024.0f, e - 15);
    return s ? -v : v;
}

/* One GX2 vertex attribute as up to four floats (integers as they are, normalized ones to 0..1 / -1..1). */
static bool attr(const uint8_t *p, uint32_t avail, uint32_t fmt, float o[4])
{
    o[0] = o[1] = o[2] = 0;
    o[3] = 1;
    int k;
    switch (fmt) {
    case 0x000: if (avail < 1) return false; o[0] = p[0] / 255.0f; return true;
    case 0x100: case 0x800: if (avail < 1) return false; o[0] = p[0]; return true;
    case 0x004: case 0x005: if (avail < 2) return false; o[0] = p[0] / 255.0f; o[1] = p[1] / 255.0f; return true;
    case 0x104: case 0x804: if (avail < 2) return false; o[0] = p[0]; o[1] = p[1]; return true;
    case 0x204: case 0x205: if (avail < 2) return false; o[0] = (int8_t)p[0] / 127.0f; o[1] = (int8_t)p[1] / 127.0f; return true;
    case 0x007: case 0x008:
        if (avail < 4) return false; o[0] = b16(p) / 65535.0f; o[1] = b16(p + 2) / 65535.0f; return true;
    case 0x207: case 0x208:
        if (avail < 4) return false; o[0] = (int16_t)b16(p) / 32767.0f; o[1] = (int16_t)b16(p + 2) / 32767.0f; return true;
    case 0x107: case 0x807: if (avail < 4) return false; o[0] = b16(p); o[1] = b16(p + 2); return true;
    case 0x307: case 0xA07: if (avail < 4) return false; o[0] = (int16_t)b16(p); o[1] = (int16_t)b16(p + 2); return true;
    case 0x809: if (avail < 4) return false; o[0] = half(b16(p)); o[1] = half(b16(p + 2)); return true;
    case 0x806: if (avail < 4) return false; o[0] = bf(p); return true;
    case 0x00A: if (avail < 4) return false; for (k = 0; k < 4; k++) o[k] = p[k] / 255.0f; return true;
    case 0x10A: case 0x80A: if (avail < 4) return false; for (k = 0; k < 4; k++) o[k] = p[k]; return true;
    case 0x20A: if (avail < 4) return false; for (k = 0; k < 4; k++) o[k] = (int8_t)p[k] / 127.0f; return true;
    case 0x30A: case 0xA0A: if (avail < 4) return false; for (k = 0; k < 4; k++) o[k] = (int8_t)p[k]; return true;
    case 0x00B: {
        if (avail < 4) return false;
        uint32_t v = b32(p);
        for (k = 0; k < 3; k++) o[k] = (v >> (k * 10) & 0x3FF) / 1023.0f;
        o[3] = (v >> 30) / 3.0f;
        return true;
    }
    case 0x20B: {
        if (avail < 4) return false;
        uint32_t v = b32(p);
        for (k = 0; k < 3; k++) {
            int c = v >> (k * 10) & 0x3FF;
            if (c & 0x200) c -= 0x400;
            o[k] = c / 511.0f;
        }
        return true;
    }
    case 0x00E: if (avail < 8) return false; for (k = 0; k < 4; k++) o[k] = b16(p + k * 2) / 65535.0f; return true;
    case 0x20E: if (avail < 8) return false; for (k = 0; k < 4; k++) o[k] = (int16_t)b16(p + k * 2) / 32767.0f; return true;
    case 0x10E: if (avail < 8) return false; for (k = 0; k < 4; k++) o[k] = b16(p + k * 2); return true;
    case 0x80F: if (avail < 8) return false; for (k = 0; k < 4; k++) o[k] = half(b16(p + k * 2)); return true;
    case 0x80D: if (avail < 8) return false; o[0] = bf(p); o[1] = bf(p + 4); return true;
    case 0x811: if (avail < 12) return false; for (k = 0; k < 3; k++) o[k] = bf(p + k * 4); return true;
    case 0x813: if (avail < 16) return false; for (k = 0; k < 4; k++) o[k] = bf(p + k * 4); return true;
    }
    return false;
}

typedef struct {
    const uint8_t *p;
    uint32_t fmt, stride, avail;
} stream_t;

static void find_attr(const buf_t *d, uint32_t fv, const char *want, stream_t *s)
{
    memset(s, 0, sizeof(*s));
    unsigned na = d->b[fv + 4], nb = d->b[fv + 5];
    uint32_t attrs = rel(d, fv + 0x10), bufs = rel(d, fv + 0x18);
    if (!attrs || !bufs || (uint64_t)attrs + na * 12 > d->n || (uint64_t)bufs + nb * 0x18 > d->n) return;
    for (unsigned i = 0; i < na; i++) {
        uint32_t a = attrs + i * 12;
        if (strcmp(rstr(d, a), want)) continue;
        unsigned bi = d->b[a + 4], off = b16(d->b + a + 6);
        if (bi >= nb) return;
        uint32_t bb = bufs + bi * 0x18, size = b32(d->b + bb + 4), stride = b16(d->b + bb + 0xC);
        uint32_t data = rel(d, bb + 0x14);
        if (!stride || !data || off >= stride || (uint64_t)data + size > d->n) return;
        s->p = d->b + data + off;
        s->fmt = b32(d->b + a + 8);
        s->stride = stride;
        s->avail = size > off ? size - off : 0;
        return;
    }
}

static bool get(const stream_t *s, uint32_t i, float o[4])
{
    if (!s->p || (uint64_t)i * s->stride >= s->avail) return false;
    return attr(s->p + (size_t)i * s->stride, s->avail - i * s->stride, s->fmt, o);
}

/* ---------------------------------------------------------------- matrices (3 x 4, row major) */

typedef float mtx_t[12];

static void m_ident(mtx_t m)
{
    memset(m, 0, sizeof(mtx_t));
    m[0] = m[5] = m[10] = 1;
}

static void m_mul(const mtx_t a, const mtx_t b, mtx_t o)
{
    mtx_t r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
        r[i * 4 + 3] += a[i * 4 + 3];
    }
    memcpy(o, r, sizeof(r));
}

static void m_point(const mtx_t m, const float v[3], float o[3])
{
    float x = v[0], y = v[1], z = v[2];
    for (int i = 0; i < 3; i++) o[i] = m[i * 4] * x + m[i * 4 + 1] * y + m[i * 4 + 2] * z + m[i * 4 + 3];
}

static void m_dir(const mtx_t m, const float v[3], float o[3])
{
    float x = v[0], y = v[1], z = v[2];
    for (int i = 0; i < 3; i++) o[i] = m[i * 4] * x + m[i * 4 + 1] * y + m[i * 4 + 2] * z;
}

/* scale, rotation (Euler XYZ, or the quaternion x y z w), translation */
static void m_srt(mtx_t m, const float s[3], const float r[4], bool euler, const float t[3])
{
    float xx, xy, xz, yx, yy, yz, zx, zy, zz;
    if (euler) {
        float cx = cosf(r[0]), sx = sinf(r[0]), cy = cosf(r[1]), sy = sinf(r[1]), cz = cosf(r[2]), sz = sinf(r[2]);
        /* Rz * Ry * Rx */
        xx = cy * cz; xy = sx * sy * cz - cx * sz; xz = cx * sy * cz + sx * sz;
        yx = cy * sz; yy = sx * sy * sz + cx * cz; yz = cx * sy * sz - sx * cz;
        zx = -sy;     zy = sx * cy;                zz = cx * cy;
    } else {
        float x = r[0], y = r[1], z = r[2], w = r[3], l = sqrtf(x * x + y * y + z * z + w * w);
        if (l < 1e-6f) x = y = z = 0, w = 1, l = 1;
        x /= l, y /= l, z /= l, w /= l;
        xx = 1 - 2 * (y * y + z * z); xy = 2 * (x * y - z * w);     xz = 2 * (x * z + y * w);
        yx = 2 * (x * y + z * w);     yy = 1 - 2 * (x * x + z * z); yz = 2 * (y * z - x * w);
        zx = 2 * (x * z - y * w);     zy = 2 * (y * z + x * w);     zz = 1 - 2 * (x * x + y * y);
    }
    m[0] = xx * s[0]; m[1] = xy * s[1]; m[2] = xz * s[2]; m[3] = t[0];
    m[4] = yx * s[0]; m[5] = yy * s[1]; m[6] = yz * s[2]; m[7] = t[1];
    m[8] = zx * s[0]; m[9] = zy * s[1]; m[10] = zz * s[2]; m[11] = t[2];
}

/* ---------------------------------------------------------------- the model */

typedef struct {
    float pos[3], nrm[3], uv[2];
} vtx_t;

typedef struct {
    char name[64];
    uint8_t *rgba;
    int w, h;
    bool alpha;                             /* has see-through texels (alpha tested) */
} tex_t;

typedef struct {
    uint32_t first, count;                  /* triangles in tri[] */
    int tex;                                /* texture, or -1 */
} part_t;

/* How a vertex follows the skeleton: mode 1 = the vertex is in the space of its bone (one bone: bi[0]), mode 2
 * = it is in the bind pose's model space and is blended over nb bones (bind-relative, through their inverse
 * bind matrices); mode 0 stays where it is. pos / nrm keep the vertex as stored. */
typedef struct {
    float pos[3], nrm[3];
    float bw[4];
    uint16_t bi[4];
    uint8_t mode, nb;
} skin_t;

typedef struct {
    char name[64];
    int parent;
    float s[3], r[4], t[3];                 /* the bind pose, local */
    bool euler;
    mtx_t bind, inv;                        /* model-space matrix and its inverse */
} bone_t;

/* an FSKA curve: keys at frames[], each with `elems` coefficients (cubic 4, linear 2, other 1) */
typedef struct {
    float *fr, *key;
    int n, elems, type;                     /* type: 0 cubic, 1 linear, 2 baked, 4 / 5 / 6 integer steps */
    bool raw;                               /* keys are 32-bit patterns (integer curves) */
    float scale, offset;
} acurve_t;

typedef struct {
    int bone;
    bool has[3];                            /* the base values present for scale, rotation, translation */
    float base[3][4];
    int curve[3][4];                        /* acurve_t index per component, or -1 */
} atrack_t;

typedef struct {
    char name[64];
    int frames;
    bool euler;
    acurve_t *c;
    int nc;
    atrack_t *tr;
    int ntr;
} anim_t;

struct wmodel {
    vtx_t *v;
    skin_t *sk;                             /* one per vertex */
    uint32_t nv, cv, csk;
    bone_t *bone;
    int nbone;
    anim_t *anim;
    int nanim;
    mtx_t *cur;                             /* scratch: the posed bones */
    uint32_t *tri;                          /* 3 vertex indices each */
    uint32_t ntri, ctri;
    part_t *part;
    int npart;
    tex_t *tex;
    int ntex;
    float center[3], radius;
    char name[64];
    int bones, textured;
};

static bool grow(void **p, uint32_t *cap, uint32_t need, size_t sz)
{
    if (need <= *cap) return true;
    uint32_t c = *cap ? *cap : 1024;
    while (c < need) c *= 2;
    void *q = realloc(*p, (size_t)c * sz);
    if (!q) return false;
    *p = q;
    *cap = c;
    return true;
}

int wmodel_count(const uint8_t *b, uint32_t n)
{
    if (n < 0x60 || memcmp(b, "FRES", 4) || b16(b + 8) != 0xFEFF || (b[4] != 3 && b[4] != 4)) return 0;
    buf_t d = { b, n };
    int c = 0, nm = b16(b + 0x50);
    uint32_t grp = rel(&d, 0x20);
    for (int i = 0; i < nm; i++) {
        uint32_t m = group_item(&d, grp, i);
        if (m && (uint64_t)m + 0x2C <= n && !memcmp(b + m, "FMDL", 4) && b16(b + m + 0x22)) c++;
    }
    return c;
}

/* the FMDL of the idx'th model with shapes */
static uint32_t nth_model(const buf_t *d, int idx)
{
    uint32_t grp = rel(d, 0x20);
    int nm = b16(d->b + 0x50), c = 0;
    for (int i = 0; i < nm; i++) {
        uint32_t m = group_item(d, grp, i);
        if (!m || (uint64_t)m + 0x2C > d->n || memcmp(d->b + m, "FMDL", 4) || !b16(d->b + m + 0x22)) continue;
        if (c++ == idx) return m;
    }
    return 0;
}

typedef struct {
    struct wmodel *m;
    char (*want)[64];
    int nwant;
} texctx_t;

static bool take_tex(void *ctx, wtex_t *t, const char *label)
{
    texctx_t *c = ctx;
    int slot = -1;
    for (int i = 0; i < c->nwant; i++)
        if (!strcmp(c->want[i], label) && !c->m->tex[i].rgba) slot = i;
    if (slot < 0 || !t->w || !t->h || t->w > 8192 || t->h > 8192) {
        wtex_free(t);
        return true;
    }
    uint8_t *full = malloc((size_t)t->w * t->h * 4);
    if (full && wtex_decode(t, full)) {
        int step = 1;
        while (t->w / step > MAX_TEX_SIDE || t->h / step > MAX_TEX_SIDE) step *= 2;
        int w = t->w / step, h = t->h / step;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        uint8_t *small = malloc((size_t)w * h * 4);
        tex_t *x = &c->m->tex[slot];
        if (small) {
            uint32_t clear = 0, solid = 0;
            for (int y = 0; y < h; y++)
                for (int xx = 0; xx < w; xx++) {
                    const uint8_t *s = full + ((size_t)y * step * t->w + (size_t)xx * step) * 4;
                    memcpy(small + ((size_t)y * w + xx) * 4, s, 4);
                    if (s[3] < 128) clear++;
                    else solid++;
                }
            x->rgba = small;
            x->w = w;
            x->h = h;
            /* cut-outs (leaves, fences) are alpha tested; a texture that is mostly see-through is a mask
             * for a shader (its alpha means something else), drawn solid */
            x->alpha = clear && solid >= (uint32_t)(w * h) / 4;
        }
    }
    free(full);
    wtex_free(t);
    return true;
}

/* ---------------------------------------------------------------- skeletal animation (FSKA) */

/* the 32 bits as a float, without bf()'s clean-up (integer curves keep bit patterns here) */
static float rawf(const uint8_t *p)
{
    union { uint32_t u; float f; } c = { b32(p) };
    return c.f;
}

static void curve_free(acurve_t *c)
{
    free(c->fr);
    free(c->key);
}

static void anim_free(anim_t *a)
{
    for (int i = 0; i < a->nc; i++) curve_free(&a->c[i]);
    free(a->c);
    free(a->tr);
}

/* One AnimCurve record (0x24 bytes from FRES 3.4, 0x20 before): flags (frame type, key type, curve type),
 * key count, the animated field, scale, offset, the frame array and the key array. */
static bool curve_read(const buf_t *d, uint32_t c, bool newl, acurve_t *o)
{
    memset(o, 0, sizeof(*o));
    uint32_t rec = newl ? 0x24 : 0x20;
    if ((uint64_t)c + rec > d->n) return false;
    unsigned flags = b16(d->b + c), nk = b16(d->b + c + 2);
    int ft = flags & 3, kt = flags >> 2 & 3;
    if (!nk || ft > 2 || kt > 2) return false;
    o->type = flags >> 4 & 7;
    o->scale = bf(d->b + c + 0x10);
    o->offset = rawf(d->b + c + 0x14);
    o->raw = o->type >= 4 && o->type <= 6;
    o->elems = o->type == 0 ? 4 : o->type == 1 ? 2 : 1;
    o->n = nk;
    uint32_t fa = rel(d, c + (newl ? 0x1C : 0x18)), ka = rel(d, c + (newl ? 0x20 : 0x1C));
    unsigned fw = ft == 0 ? 4 : ft == 1 ? 2 : 1, kw = kt == 0 ? 4 : kt == 1 ? 2 : 1;
    if (!fa || !ka || (uint64_t)fa + (uint64_t)nk * fw > d->n || (uint64_t)ka + (uint64_t)nk * o->elems * kw > d->n)
        return false;
    o->fr = malloc(nk * sizeof(float));
    o->key = malloc((size_t)nk * o->elems * sizeof(float));
    if (!o->fr || !o->key) {
        curve_free(o);
        memset(o, 0, sizeof(*o));
        return false;
    }
    for (unsigned i = 0; i < nk; i++) {
        const uint8_t *p = d->b + fa + i * fw;
        o->fr[i] = ft == 0 ? bf(p) : ft == 1 ? (int16_t)b16(p) / 32.0f : (float)p[0];
    }
    for (unsigned i = 0; i < nk * o->elems; i++) {
        const uint8_t *p = d->b + ka + i * kw;
        o->key[i] = kt == 0 ? (o->raw ? rawf(p) : bf(p)) : kt == 1 ? (float)(int16_t)b16(p) : (float)(int8_t)p[0];
    }
    return true;
}

static int32_t curve_int(const acurve_t *c, float stored)
{
    if (!c->raw) return (int32_t)stored;
    union { float f; uint32_t u; } k = { stored };
    return (int32_t)k.u;
}

static float curve_value(const acurve_t *c, float stored)
{
    if (c->raw) {
        union { float f; uint32_t u; } o = { c->offset };
        return (float)(curve_int(c, stored) + (int32_t)o.u);
    }
    return stored * c->scale + c->offset;
}

static float curve_eval(const acurve_t *c, float t)
{
    int hi = c->n - 1, e = c->elems;
    if (t <= c->fr[0]) return curve_value(c, c->key[0]);
    if (t >= c->fr[hi]) return curve_value(c, c->key[hi * e]);
    int i = 0;
    while (i < hi && t >= c->fr[i + 1]) i++;
    float f0 = c->fr[i], dt = c->fr[i + 1] - f0;
    const float *k = c->key + i * e;
    if (dt <= 0) return curve_value(c, k[0]);
    switch (c->type) {
    case 0: {                               /* cubic: c0 + c1 u + c2 u^2 + c3 u^3 over the segment */
        float u = (t - f0) / dt;
        return k[0] * c->scale + c->offset + u * (k[1] * c->scale + u * (k[2] * c->scale + u * k[3] * c->scale));
    }
    case 1:                                 /* linear */
        return k[0] * c->scale + c->offset + k[1] * c->scale * (t - f0);
    default:
        return curve_value(c, k[0]);
    }
}

static int find_bone(const wmodel_t *m, const char *name)
{
    for (int i = 0; i < m->nbone; i++)
        if (!strcmp(m->bone[i].name, name)) return i;
    return -1;
}

/* One FSKA into the model's animations (those that move at least one of its bones). */
static void fska_load(wmodel_t *m, const buf_t *d, uint32_t fs, uint32_t ver, const char *name)
{
    bool newl = ver >= 0x03040000;
    if ((uint64_t)fs + (newl ? 0x30 : 0x24) > d->n) return;
    uint32_t flags = b32(d->b + fs + 0x0C);
    int frames = newl ? (int32_t)b32(d->b + fs + 0x10) : (int)b16(d->b + fs + 0x10);
    unsigned nba = newl ? b16(d->b + fs + 0x14) : b16(d->b + fs + 0x12);
    if (flags & 2) frames++;
    uint32_t bas = rel(d, fs + 0x20);
    if (!nba || frames <= 0 || frames > 100000 || !bas || (uint64_t)bas + nba * 0x18ull > d->n) return;

    anim_t an;
    memset(&an, 0, sizeof(an));
    snprintf(an.name, sizeof(an.name), "%s", name && *name ? name : "animation");
    an.frames = frames;
    an.euler = (flags & 0x1000) != 0;
    an.c = calloc(nba * 9, sizeof(acurve_t));
    an.tr = calloc(nba, sizeof(atrack_t));
    if (!an.c || !an.tr) goto bad;

    unsigned crec = newl ? 0x24 : 0x20;
    for (unsigned bi = 0; bi < nba; bi++) {
        uint32_t ba = bas + bi * 0x18;
        unsigned ncurve = d->b[ba + 0xA];
        int bone = ncurve ? find_bone(m, rstr(d, ba + 4)) : -1;
        uint32_t cb = rel(d, ba + 0x10);
        if (bone < 0 || !cb || (uint64_t)cb + ncurve * crec > d->n) continue;
        atrack_t *tr = &an.tr[an.ntr];
        memset(tr, -1, sizeof(*tr));
        tr->bone = bone;
        memset(tr->has, 0, sizeof(tr->has));
        for (int g = 0; g < 3; g++) {
            tr->base[g][0] = tr->base[g][1] = tr->base[g][2] = g == 0 ? 1.0f : 0.0f;
            tr->base[g][3] = 1;
        }
        unsigned bflags = b32(d->b + ba) & 0x38;
        uint32_t bo = rel(d, ba + 0x14);
        if (bo && (uint64_t)bo + 0x28 <= d->n) {
            tr->has[0] = bflags & 0x08, tr->has[1] = bflags & 0x10, tr->has[2] = bflags & 0x20;
            if (tr->has[0]) for (int e = 0; e < 3; e++) tr->base[0][e] = bf(d->b + bo + e * 4);
            if (tr->has[1]) for (int e = 0; e < 4; e++) tr->base[1][e] = bf(d->b + bo + 12 + e * 4);
            if (tr->has[2]) for (int e = 0; e < 3; e++) tr->base[2][e] = bf(d->b + bo + 28 + e * 4);
        }
        int any = 0;
        for (unsigned k = 0; k < ncurve && k < 9; k++) {
            acurve_t *c = &an.c[an.nc];
            if (!curve_read(d, cb + k * crec, newl, c)) continue;
            uint32_t tg = b32(d->b + cb + k * crec + 4) & 0xFFFFFFFCu;
            int g = -1, e = 0;
            if (tg >= 0x04 && tg <= 0x0C) g = 0, e = (tg - 4) / 4;              /* scale */
            else if (tg >= 0x10 && tg <= 0x18) g = 2, e = (tg - 0x10) / 4;      /* translation */
            else if (tg >= 0x20 && tg <= 0x2C) g = 1, e = (tg - 0x20) / 4;      /* rotation */
            if (g < 0) {
                curve_free(c);
                continue;
            }
            tr->curve[g][e] = an.nc++;
            any = 1;
        }
        if (any) an.ntr++;
    }
    if (!an.ntr) goto bad;
    anim_t *na = realloc(m->anim, (m->nanim + 1) * sizeof(anim_t));
    if (!na) goto bad;
    m->anim = na;
    m->anim[m->nanim++] = an;
    return;
bad:
    anim_free(&an);
}

/* every FSKA of the file, from the FRES's index group at 0x28 */
static void fska_all(wmodel_t *m, const buf_t *d)
{
    uint32_t grp = rel(d, 0x28);
    if (!grp || (uint64_t)grp + 8 > d->n || m->nbone <= 0) return;
    uint32_t cnt = b32(d->b + grp + 4), ver = b32(d->b + 4);
    if (cnt > 0x4000 || (uint64_t)grp + 8 + (cnt + 1) * 16ull > d->n) return;
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t fs = group_item(d, grp, i);
        if (!fs || (uint64_t)fs + 0x24 > d->n || memcmp(d->b + fs, "FSKA", 4)) continue;
        fska_load(m, d, fs, ver, rstr(d, grp + 8 + (i + 1) * 16 + 8));
    }
}

static bool m_inv(const mtx_t a, mtx_t o)
{
    float c0 = a[5] * a[10] - a[6] * a[9], c1 = a[6] * a[8] - a[4] * a[10], c2 = a[4] * a[9] - a[5] * a[8];
    float det = a[0] * c0 + a[1] * c1 + a[2] * c2;
    if (fabsf(det) < 1e-12f) return false;
    float id = 1.0f / det;
    float r[9] = { c0 * id, (a[2] * a[9] - a[1] * a[10]) * id, (a[1] * a[6] - a[2] * a[5]) * id,
                   c1 * id, (a[0] * a[10] - a[2] * a[8]) * id, (a[2] * a[4] - a[0] * a[6]) * id,
                   c2 * id, (a[1] * a[8] - a[0] * a[9]) * id,  (a[0] * a[5] - a[1] * a[4]) * id };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) o[i * 4 + j] = r[i * 3 + j];
        o[i * 4 + 3] = -(o[i * 4] * a[3] + o[i * 4 + 1] * a[7] + o[i * 4 + 2] * a[11]);
    }
    return true;
}

int wmodel_anim_count(const wmodel_t *m) { return m->nanim; }
const char *wmodel_anim_name(const wmodel_t *m, int i) { return i >= 0 && i < m->nanim ? m->anim[i].name : ""; }
int wmodel_anim_frames(const wmodel_t *m, int i) { return i >= 0 && i < m->nanim ? m->anim[i].frames : 0; }

/* Poses the model: anim < 0 is the bind pose, otherwise the animation at `frame` (looped). */
void wmodel_pose(wmodel_t *m, int anim, float frame)
{
    if (!m->sk || !m->nbone || !m->cur) return;
    const anim_t *an = anim >= 0 && anim < m->nanim ? &m->anim[anim] : NULL;
    if (an) {
        if (an->frames > 1) {
            frame = fmodf(frame, (float)(an->frames - 1));
            if (frame < 0) frame += an->frames - 1;
        } else frame = 0;
    }
    int *track = NULL;
    if (an) {
        track = malloc(m->nbone * sizeof(int));
        if (!track) an = NULL;
        else {
            for (int i = 0; i < m->nbone; i++) track[i] = -1;
            for (int i = 0; i < an->ntr; i++) track[an->tr[i].bone] = i;
        }
    }
    for (int i = 0; i < m->nbone; i++) {
        const bone_t *b = &m->bone[i];
        float s[3], r[4], t[3];
        memcpy(s, b->s, sizeof(s)), memcpy(r, b->r, sizeof(r)), memcpy(t, b->t, sizeof(t));
        bool euler = b->euler;
        if (an && track[i] >= 0) {
            const atrack_t *tr = &an->tr[track[i]];
            float *dst[3] = { s, r, t };
            euler = an->euler;
            for (int g = 0; g < 3; g++) {
                int ne = g == 1 ? 4 : 3;
                if (g == 1 && !tr->has[1] && tr->curve[1][0] < 0 && tr->curve[1][1] < 0 && tr->curve[1][2] < 0 &&
                    tr->curve[1][3] < 0)
                    euler = b->euler;       /* not animated: the bone's own rotation */
                for (int e = 0; e < ne; e++) {
                    if (tr->curve[g][e] >= 0) dst[g][e] = curve_eval(&an->c[tr->curve[g][e]], frame);
                    else if (tr->has[g]) dst[g][e] = tr->base[g][e];
                }
            }
        }
        mtx_t local;
        m_srt(local, s, r, euler, t);
        if (b->parent >= 0 && b->parent < i) m_mul(m->cur[b->parent], local, m->cur[i]);
        else memcpy(m->cur[i], local, sizeof(mtx_t));
    }
    free(track);

    /* smooth skinning's matrices: posed bone * inverse bind */
    mtx_t *sm = malloc(m->nbone * sizeof(mtx_t));
    if (sm)
        for (int i = 0; i < m->nbone; i++) m_mul(m->cur[i], m->bone[i].inv, sm[i]);
    for (uint32_t i = 0; i < m->nv; i++) {
        const skin_t *k = &m->sk[i];
        vtx_t *v = &m->v[i];
        if (k->mode == 1 && k->bi[0] < m->nbone) {
            m_point(m->cur[k->bi[0]], k->pos, v->pos);
            m_dir(m->cur[k->bi[0]], k->nrm, v->nrm);
        } else if (k->mode == 2 && sm) {
            float p[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 };
            for (int j = 0; j < k->nb; j++) {
                if (k->bi[j] >= m->nbone || k->bw[j] <= 0) continue;
                float q[3], u[3];
                m_point(sm[k->bi[j]], k->pos, q);
                m_dir(sm[k->bi[j]], k->nrm, u);
                for (int c = 0; c < 3; c++) p[c] += q[c] * k->bw[j], n[c] += u[c] * k->bw[j];
            }
            memcpy(v->pos, p, sizeof(p));
            memcpy(v->nrm, n, sizeof(n));
        } else continue;
        float l = sqrtf(v->nrm[0] * v->nrm[0] + v->nrm[1] * v->nrm[1] + v->nrm[2] * v->nrm[2]);
        if (l > 1e-6f) for (int c = 0; c < 3; c++) v->nrm[c] /= l;
    }
    free(sm);
    /* the camera follows the posed model (one that travels would leave the picture) */
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (uint32_t i = 0; i < m->ntri * 3; i++) {
        const float *p = m->v[m->tri[i]].pos;
        for (int c = 0; c < 3; c++) {
            if (p[c] < lo[c]) lo[c] = p[c];
            if (p[c] > hi[c]) hi[c] = p[c];
        }
    }
    if (m->ntri)
        for (int c = 0; c < 3; c++) m->center[c] = (lo[c] + hi[c]) / 2;
}

void wmodel_free(wmodel_t *m)
{
    if (!m) return;
    for (int i = 0; i < m->nanim; i++) anim_free(&m->anim[i]);
    free(m->anim);
    free(m->bone);
    free(m->cur);
    free(m->sk);
    for (int i = 0; i < m->ntex; i++) free(m->tex[i].rgba);
    free(m->tex);
    free(m->v);
    free(m->tri);
    free(m->part);
    free(m);
}

wmodel_t *wmodel_load(const uint8_t *b, uint32_t n, int idx, const char **err)
{
    buf_t D = { b, n }, *d = &D;
    *err = "This BFRES has no model in it";
    uint32_t fm = wmodel_count(b, n) ? nth_model(d, idx) : 0;
    if (!fm) return NULL;
    *err = "Not enough memory for this model";
    wmodel_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    snprintf(m->name, sizeof(m->name), "%s", rstr(d, fm + 4));

    /* the skeleton: each bone's model-space matrix */
    uint32_t sk = rel(d, fm + 0x0C);
    int nbone = 0, nmtb = 0;
    mtx_t *world = NULL;
    uint32_t mtb = 0;                       /* matrix -> bone table (u16 each) */
    if (sk && (uint64_t)sk + 0x20 <= n && !memcmp(b + sk, "FSKL", 4)) {
        nbone = b16(b + sk + 8);
        nmtb = b16(b + sk + 10) + b16(b + sk + 12);
        uint32_t arr = rel(d, sk + 0x14);
        mtb = rel(d, sk + 0x18);
        if (!mtb || (uint64_t)mtb + nmtb * 2 > n) nmtb = 0;
        if (nbone > 4096 || !arr || (uint64_t)arr + nbone * 0x40ull > n) nbone = 0;
        world = nbone ? calloc(nbone, sizeof(mtx_t)) : NULL;
        if (!world) nbone = 0;
        for (int i = 0; i < nbone; i++) {
            const uint8_t *e = b + arr + i * 0x40;
            float s[3] = { bf(e + 0x14), bf(e + 0x18), bf(e + 0x1C) };
            float r[4] = { bf(e + 0x20), bf(e + 0x24), bf(e + 0x28), bf(e + 0x2C) };
            float t[3] = { bf(e + 0x30), bf(e + 0x34), bf(e + 0x38) };
            mtx_t local;
            bool euler = (b32(e + 0x10) & 0x7000) == 0x1000;
            m_srt(local, s, r, euler, t);
            int par = (int16_t)b16(e + 6);
            if (par >= 0 && par < i) m_mul(world[par], local, world[i]);   /* parents come first */
            else memcpy(world[i], local, sizeof(mtx_t));
            if (m->bone || (m->bone = calloc(nbone, sizeof(bone_t)))) {
                bone_t *bn = &m->bone[i];
                snprintf(bn->name, sizeof(bn->name), "%s", rstr(d, arr + i * 0x40));
                bn->parent = par >= 0 && par < i ? par : -1;
                memcpy(bn->s, s, sizeof(s)), memcpy(bn->r, r, sizeof(r)), memcpy(bn->t, t, sizeof(t));
                bn->euler = euler;
                memcpy(bn->bind, world[i], sizeof(mtx_t));
                if (!m_inv(world[i], bn->inv)) m_ident(bn->inv);
            }
        }
    }
    m->bones = nbone;
    m->nbone = m->bone ? nbone : 0;
    m->cur = m->nbone ? calloc(nbone, sizeof(mtx_t)) : NULL;

    /* materials: the first texture each, as wanted names */
    int nmat = b16(b + fm + 0x24);
    uint32_t mgrp = rel(d, fm + 0x18);
    char (*want)[64] = calloc(nmat ? nmat : 1, 64);
    int *mat_tex = calloc(nmat ? nmat : 1, sizeof(int));
    if (!want || !mat_tex) goto oom;
    int nwant = 0;
    for (int i = 0; i < nmat; i++) {
        mat_tex[i] = -1;
        uint32_t ma = group_item(d, mgrp, i);
        if (!ma || (uint64_t)ma + 0x4C > n || memcmp(b + ma, "FMAT", 4) || !b[ma + 0x11]) continue;
        uint32_t refs = rel(d, ma + 0x28);
        if (!refs) continue;
        const char *tn = rstr(d, refs);
        if (!tn[0]) continue;
        int k;
        for (k = 0; k < nwant && strcmp(want[k], tn); k++) {}
        if (k == nwant) snprintf(want[nwant++], 64, "%s", tn);
        mat_tex[i] = k;
    }
    m->tex = calloc(nwant ? nwant : 1, sizeof(tex_t));
    if (!m->tex) goto oom;
    m->ntex = nwant;
    for (int i = 0; i < nwant; i++) snprintf(m->tex[i].name, sizeof(m->tex[i].name), "%s", want[i]);

    /* shapes */
    int nvtx = b16(b + fm + 0x20), nshp = b16(b + fm + 0x22);
    uint32_t varr = rel(d, fm + 0x10), sgrp = rel(d, fm + 0x14);
    for (int si = 0; si < nshp; si++) {
        uint32_t sh = group_item(d, sgrp, si);
        if (!sh || (uint64_t)sh + 0x30 > n || memcmp(b + sh, "FSHP", 4)) continue;
        int mat = b16(b + sh + 0x0E), bone = b16(b + sh + 0x10), vi = b16(b + sh + 0x12);
        int skin = b[sh + 0x16];
        if (vi >= nvtx || !varr) continue;
        uint32_t fv = varr + vi * 0x20;
        if ((uint64_t)fv + 0x20 > n || memcmp(b + fv, "FVTX", 4)) continue;
        uint32_t count = b32(b + fv + 8);
        stream_t P, N, U, I;
        find_attr(d, fv, "_p0", &P);
        if (!P.p || !count || count > 0x400000) continue;
        find_attr(d, fv, "_n0", &N);
        find_attr(d, fv, "_u0", &U);
        find_attr(d, fv, "_i0", &I);
        stream_t W;
        find_attr(d, fv, "_w0", &W);
        /* the bones a smooth vertex's indices name: the shape's own table, else the skeleton's */
        unsigned nsb = b16(b + sh + 0x14);
        uint32_t sbt = nsb && nsb < 4096 && (uint64_t)sh + 0x2C <= n ? rel(d, sh + 0x28) : 0;
        if (sbt && (uint64_t)sbt + nsb * 2 > n) sbt = 0;

        uint32_t lod = rel(d, sh + 0x24);
        if (!lod || (uint64_t)lod + 0x1C > n) continue;
        uint32_t prim = b32(b + lod), ifmt = b32(b + lod + 4), icount = b32(b + lod + 8);
        uint32_t ibuf = rel(d, lod + 0x14), first = b32(b + lod + 0x18);
        if (first >= count) first = 0;
        if ((prim != 4 && prim != 6) || !icount || icount > 0x1000000 || !ibuf || (uint64_t)ibuf + 0x18 > n) continue;
        uint32_t idata = rel(d, ibuf + 0x14);
        unsigned isz = ifmt == 9 || ifmt == 1 ? 4 : 2;
        bool ile = ifmt == 0 || ifmt == 1;
        if (!idata || (uint64_t)idata + (uint64_t)icount * isz > n) continue;

        /* the vertices, in model space */
        uint32_t base = m->nv;
        if (!grow((void **)&m->v, &m->cv, m->nv + count, sizeof(vtx_t))) goto oom;
        if (!grow((void **)&m->sk, &m->csk, m->nv + count, sizeof(skin_t))) goto oom;
        const float *shape_bone = skin == 0 && bone < nbone ? world[bone] : NULL;
        for (uint32_t k = 0; k < count; k++) {
            vtx_t *v = &m->v[m->nv + k];
            skin_t *sk = &m->sk[m->nv + k];
            float a[4];
            memset(v, 0, sizeof(*v));
            memset(sk, 0, sizeof(*sk));
            if (get(&P, k, a)) memcpy(v->pos, a, 12);
            if (get(&N, k, a)) memcpy(v->nrm, a, 12);
            else v->nrm[1] = 1;
            if (get(&U, k, a)) v->uv[0] = a[0], v->uv[1] = a[1];
            memcpy(sk->pos, v->pos, 12);
            memcpy(sk->nrm, v->nrm, 12);
            const float *bm = shape_bone;
            if (shape_bone) sk->mode = 1, sk->nb = 1, sk->bi[0] = bone;
            if (skin == 1 && get(&I, k, a)) {
                int mi = (int)a[0];
                if (mi >= 0 && mi < nmtb) {
                    int bi = b16(b + mtb + mi * 2);
                    if (bi < nbone) bm = world[bi], sk->mode = 1, sk->nb = 1, sk->bi[0] = bi;
                }
            }
            if (skin >= 2 && nbone && get(&I, k, a)) {
                float w[4] = { 1, 0, 0, 0 }, sum = 0;
                if (!get(&W, k, w)) w[0] = 1, w[1] = w[2] = w[3] = 0;
                int nb = skin < 4 ? skin : 4;
                for (int j = 0; j < nb; j++) {
                    int mi = (int)a[j], bi = -1;
                    if (sbt && mi >= 0 && mi < (int)nsb) bi = b16(b + sbt + mi * 2);
                    else if (!sbt && mi >= 0 && mi < nmtb) bi = b16(b + mtb + mi * 2);
                    if (bi < 0 || bi >= nbone || w[j] <= 0) continue;
                    sk->bi[sk->nb] = bi;
                    sk->bw[sk->nb++] = w[j];
                    sum += w[j];
                }
                if (sum > 0) {
                    for (int j = 0; j < sk->nb; j++) sk->bw[j] /= sum;
                    sk->mode = 2;
                }
            }
            if (bm) {
                float o[3];
                m_point(bm, v->pos, o);
                memcpy(v->pos, o, 12);
                m_dir(bm, v->nrm, o);
                memcpy(v->nrm, o, 12);
            }
            float l = sqrtf(v->nrm[0] * v->nrm[0] + v->nrm[1] * v->nrm[1] + v->nrm[2] * v->nrm[2]);
            if (l > 1e-6f) for (int c = 0; c < 3; c++) v->nrm[c] /= l;
        }
        m->nv += count;

        /* the triangles */
        uint32_t ntri = prim == 4 ? icount / 3 : icount >= 3 ? icount - 2 : 0;
        if (!grow((void **)&m->tri, &m->ctri, (m->ntri + ntri) * 3, sizeof(uint32_t))) goto oom;
        uint32_t t0 = m->ntri;
        for (uint32_t k = 0; k < ntri; k++) {
            uint32_t ix[3];
            for (int c = 0; c < 3; c++) {
                uint32_t at = prim == 4 ? k * 3 + c : k + c;
                const uint8_t *p = b + idata + (size_t)at * isz;
                uint32_t x = isz == 4 ? (ile ? (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24 : b32(p))
                                      : (ile ? (uint32_t)(p[0] | p[1] << 8) : b16(p));
                ix[c] = x + first;
            }
            if (prim == 6 && (k & 1)) { uint32_t t = ix[1]; ix[1] = ix[2]; ix[2] = t; }
            if (ix[0] >= count || ix[1] >= count || ix[2] >= count) continue;
            if (ix[0] == ix[1] || ix[1] == ix[2] || ix[0] == ix[2]) continue;
            uint32_t *o = m->tri + m->ntri * 3;
            o[0] = base + ix[0], o[1] = base + ix[1], o[2] = base + ix[2];
            m->ntri++;
        }
        part_t *np = realloc(m->part, (m->npart + 1) * sizeof(part_t));
        if (!np) goto oom;
        m->part = np;
        m->part[m->npart].first = t0;
        m->part[m->npart].count = m->ntri - t0;
        m->part[m->npart].tex = mat < nmat ? mat_tex[mat] : -1;
        m->npart++;
    }
    free(world);
    world = NULL;
    *err = "This model has no triangles to draw";
    if (!m->ntri) goto fail;

    /* textures: only those the materials use */
    if (nwant) {
        texctx_t tc = { m, want, nwant };
        wiiu_textures(b, n, "x.bfres", take_tex, &tc);
    }
    for (int i = 0; i < m->npart; i++)
        if (m->part[i].tex >= 0 && m->tex[m->part[i].tex].rgba) m->textured++;
    free(want);
    free(mat_tex);
    fska_all(m, d);

    /* the bounds, for the camera */
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (uint32_t i = 0; i < m->ntri * 3; i++) {
        const float *p = m->v[m->tri[i]].pos;
        for (int c = 0; c < 3; c++) {
            if (p[c] < lo[c]) lo[c] = p[c];
            if (p[c] > hi[c]) hi[c] = p[c];
        }
    }
    float r2 = 0;
    for (int c = 0; c < 3; c++) m->center[c] = (lo[c] + hi[c]) / 2;
    for (uint32_t i = 0; i < m->ntri * 3; i++) {
        const float *p = m->v[m->tri[i]].pos;
        float dx = p[0] - m->center[0], dy = p[1] - m->center[1], dz = p[2] - m->center[2];
        float q = dx * dx + dy * dy + dz * dz;
        if (q > r2) r2 = q;
    }
    m->radius = r2 > 0 ? sqrtf(r2) : 1;
    *err = NULL;
    return m;
oom:
    *err = "Not enough memory for this model";
fail:
    free(world);
    free(want);
    free(mat_tex);
    wmodel_free(m);
    return NULL;
}

void wmodel_info(const wmodel_t *m, wmodel_info_t *i)
{
    i->name = m->name;
    i->triangles = m->ntri;
    i->vertices = m->nv;
    i->shapes = m->npart;
    i->textured = m->textured;
    i->textures = m->ntex;
    i->bones = m->bones;
}

/* ---------------------------------------------------------------- drawing */

typedef struct {
    float x, y, z, w;                       /* clip space */
    float u, v, light;
} cv_t;

typedef struct {
    uint8_t *rgb;
    float *zb;                              /* 1 / w; larger is nearer; 0 is empty */
    int w, h;
    int shade;
} target_t;

static void line(target_t *t, float x0, float y0, float x1, float y1, const uint8_t c[3])
{
    float dx = x1 - x0, dy = y1 - y0;
    int steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)) + 1;
    if (steps > 4096) return;
    for (int i = 0; i <= steps; i++) {
        int x = (int)(x0 + dx * i / steps), y = (int)(y0 + dy * i / steps);
        if (x >= 0 && y >= 0 && x < t->w && y < t->h) memcpy(t->rgb + ((size_t)y * t->w + x) * 3, c, 3);
    }
}

/* one triangle after clipping: screen x, y, 1/w and the varyings over w */
static void raster(target_t *t, const cv_t *a, const cv_t *b, const cv_t *c, const tex_t *tx)
{
    float sx[3], sy[3], iw[3], uw[3], vw[3], lw[3];
    const cv_t *p[3] = { a, b, c };
    for (int i = 0; i < 3; i++) {
        iw[i] = 1.0f / p[i]->w;
        sx[i] = (p[i]->x * iw[i] * 0.5f + 0.5f) * t->w;
        sy[i] = (0.5f - p[i]->y * iw[i] * 0.5f) * t->h;
        uw[i] = p[i]->u * iw[i];
        vw[i] = p[i]->v * iw[i];
        lw[i] = p[i]->light * iw[i];
    }
    if (t->shade == WM_WIRE) {
        static const uint8_t white[3] = { 230, 230, 230 };
        line(t, sx[0], sy[0], sx[1], sy[1], white);
        line(t, sx[1], sy[1], sx[2], sy[2], white);
        line(t, sx[2], sy[2], sx[0], sy[0], white);
        return;
    }
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (fabsf(area) < 1e-6f) return;
    float minx = fminf(sx[0], fminf(sx[1], sx[2])), maxx = fmaxf(sx[0], fmaxf(sx[1], sx[2]));
    float miny = fminf(sy[0], fminf(sy[1], sy[2])), maxy = fmaxf(sy[0], fmaxf(sy[1], sy[2]));
    int x0 = (int)fmaxf(0, floorf(minx)), x1 = (int)fminf(t->w - 1, ceilf(maxx));
    int y0 = (int)fmaxf(0, floorf(miny)), y1 = (int)fminf(t->h - 1, ceilf(maxy));
    if (x0 > x1 || y0 > y1) return;
    float inv = 1.0f / area;
    /* edge functions, stepped across each row */
    for (int y = y0; y <= y1; y++) {
        float py = y + 0.5f;
        float px = x0 + 0.5f;
        float e0 = ((sx[2] - sx[1]) * (py - sy[1]) - (sy[2] - sy[1]) * (px - sx[1])) * inv;
        float e1 = ((sx[0] - sx[2]) * (py - sy[2]) - (sy[0] - sy[2]) * (px - sx[2])) * inv;
        float d0 = -(sy[2] - sy[1]) * inv, d1 = -(sy[0] - sy[2]) * inv;
        for (int x = x0; x <= x1; x++, e0 += d0, e1 += d1) {
            float e2 = 1 - e0 - e1;
            if (e0 < 0 || e1 < 0 || e2 < 0) continue;
            float z = e0 * iw[0] + e1 * iw[1] + e2 * iw[2];
            size_t at = (size_t)y * t->w + x;
            if (z <= t->zb[at]) continue;
            float wv = 1.0f / z;
            float l = (e0 * lw[0] + e1 * lw[1] + e2 * lw[2]) * wv;
            int r = 200, g = 200, bl = 200;
            if (tx && tx->rgba) {
                float u = (e0 * uw[0] + e1 * uw[1] + e2 * uw[2]) * wv;
                float v = (e0 * vw[0] + e1 * vw[1] + e2 * vw[2]) * wv;
                u -= floorf(u);
                v -= floorf(v);
                int ix = (int)(u * tx->w), iy = (int)(v * tx->h);
                if (ix >= tx->w) ix = tx->w - 1;
                if (iy >= tx->h) iy = tx->h - 1;
                const uint8_t *s = tx->rgba + ((size_t)iy * tx->w + ix) * 4;
                if (tx->alpha && s[3] < 128) continue;
                r = s[0], g = s[1], bl = s[2];
            }
            if (t->shade == WM_LIT) {
                r = (int)(r * l), g = (int)(g * l), bl = (int)(bl * l);
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (bl > 255) bl = 255;
            }
            t->zb[at] = z;
            uint8_t *o = t->rgb + at * 3;
            o[0] = (uint8_t)r, o[1] = (uint8_t)g, o[2] = (uint8_t)bl;
        }
    }
}

#define NEAR 0.01f

static cv_t lerp(const cv_t *a, const cv_t *b, float k)
{
    cv_t o;
    o.x = a->x + (b->x - a->x) * k;
    o.y = a->y + (b->y - a->y) * k;
    o.z = a->z + (b->z - a->z) * k;
    o.w = a->w + (b->w - a->w) * k;
    o.u = a->u + (b->u - a->u) * k;
    o.v = a->v + (b->v - a->v) * k;
    o.light = a->light + (b->light - a->light) * k;
    return o;
}

/* clips against w = NEAR (behind the camera), then draws the fan */
static void tri(target_t *t, const cv_t *a, const cv_t *b, const cv_t *c, const tex_t *tx)
{
    const cv_t *in[3] = { a, b, c };
    cv_t out[4];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const cv_t *p = in[i], *q = in[(i + 1) % 3];
        bool pin = p->w >= NEAR, qin = q->w >= NEAR;
        if (pin) out[n++] = *p;
        if (pin != qin) out[n++] = lerp(p, q, (NEAR - p->w) / (q->w - p->w));
    }
    for (int i = 1; i + 1 < n; i++) raster(t, &out[0], &out[i], &out[i + 1], tx);
}

void wmodel_render(const wmodel_t *m, uint8_t *rgb, int w, int h, float yaw, float pitch, float dist, int shade)
{
    static const uint8_t bg_top[3] = { 40, 44, 60 }, bg_bot[3] = { 12, 12, 18 };
    for (int y = 0; y < h; y++) {
        uint8_t c[3];
        for (int k = 0; k < 3; k++) c[k] = (uint8_t)(bg_top[k] + (bg_bot[k] - bg_top[k]) * y / (h > 1 ? h - 1 : 1));
        for (int x = 0; x < w; x++) memcpy(rgb + ((size_t)y * w + x) * 3, c, 3);
    }
    float *zb = calloc((size_t)w * h, sizeof(float));
    cv_t *cv = malloc((size_t)m->nv * sizeof(cv_t));
    if (!zb || !cv) {
        free(zb);
        free(cv);
        return;
    }

    /* view: around the model's center, then back by dist radii */
    float cy = cosf(yaw * 0.0174533f), sy = sinf(yaw * 0.0174533f);
    float cp = cosf(pitch * 0.0174533f), sp = sinf(pitch * 0.0174533f);
    float f = 1.0f / tanf(0.0174533f * 22.5f);                      /* 45 degrees high */
    float aspect = (float)w / h, back = dist * m->radius, s = 1.0f;
    static const float light[3] = { 0.35f, 0.6f, 0.72f };            /* towards the viewer, from above */
    for (uint32_t i = 0; i < m->nv; i++) {
        const vtx_t *v = &m->v[i];
        float x = v->pos[0] - m->center[0], y = v->pos[1] - m->center[1], z = v->pos[2] - m->center[2];
        float x1 = x * cy + z * sy, z1 = -x * sy + z * cy;           /* yaw about Y */
        float y2 = y * cp - z1 * sp, z2 = y * sp + z1 * cp;          /* pitch about X */
        float nx = v->nrm[0], ny = v->nrm[1], nz = v->nrm[2];
        float nx1 = nx * cy + nz * sy, nz1 = -nx * sy + nz * cy;
        float ny2 = ny * cp - nz1 * sp, nz2 = ny * sp + nz1 * cp;
        float l = nx1 * light[0] + ny2 * light[1] + nz2 * light[2];
        cv_t *o = &cv[i];
        float vz = back - z2;                                         /* distance in front of the camera */
        o->x = x1 * f / aspect * s;
        o->y = y2 * f * s;
        o->w = vz;
        o->z = vz;
        o->u = v->uv[0];
        o->v = v->uv[1];
        o->light = 0.35f + 0.75f * (l > 0 ? l : 0);
    }

    target_t t = { rgb, zb, w, h, shade };
    for (int p = 0; p < m->npart; p++) {
        const part_t *pt = &m->part[p];
        const tex_t *tx = shade != WM_WIRE && pt->tex >= 0 ? &m->tex[pt->tex] : NULL;
        for (uint32_t k = pt->first; k < pt->first + pt->count; k++) {
            const uint32_t *ix = m->tri + k * 3;
            const cv_t *a = &cv[ix[0]], *b = &cv[ix[1]], *c = &cv[ix[2]];
            if (a->w < NEAR && b->w < NEAR && c->w < NEAR) continue;
            /* entirely off one side */
            if ((a->x > a->w && b->x > b->w && c->x > c->w) || (a->x < -a->w && b->x < -b->w && c->x < -c->w) ||
                (a->y > a->w && b->y > b->w && c->y > c->w) || (a->y < -a->w && b->y < -b->w && c->y < -c->w))
                continue;
            tri(&t, a, b, c, tx);
        }
    }
    free(zb);
    free(cv);
}
