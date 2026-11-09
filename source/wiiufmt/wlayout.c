/*
 * Wii U BFLYT layouts drawn in software. See wlayout.h.
 *
 * The sections follow kinnay's Nintendo-File-Formats bflyt.md and quatric's nintoolbox (lib-bflyt.c), for
 * the Wii U's layouts (versions below 8): a pane header of 0x54 bytes (flags, origin, alpha, a 24-byte
 * name, 8 bytes of user data, translation, rotation, scale, size), then each kind's own fields. "pas1" /
 * "pae1" open and close the children of the pane before them. Panes are drawn in file order, which is
 * parents before children, as the console does.
 *
 * What is drawn: a material's first texture through its black / white colours (texture * (white - black) +
 * black), its first texture SRT, times the pane's vertex colours and alpha (passed down when a parent asks
 * for it). Left out: TEV combiners, blend modes other than alpha, indirect and projected textures, X / Y
 * rotation (approximated by its cosine), animations (BFLAN) and the system fonts.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "wlayout.h"
#include "../wiiufmt.h"

#define MAX_PANES 4096
#define MAX_DEPTH 3                         /* parts in parts in parts */

typedef struct {
    char name[96];
    uint8_t *rgba;
    int w, h;
    bool tried;
} ltex_t;

typedef struct {
    int tex;                                /* in the layout's textures, -1: none */
    int wrap_s, wrap_t;
    bool srt;
    float tx, ty, rot, sx, sy;
    uint8_t black[4], white[4];
} lmat_t;

enum { K_PANE, K_PIC, K_TXT, K_WND, K_BND, K_PRT, K_OTHER };

typedef struct {
    int mat;
    uint8_t flip;
} lframe_t;

typedef struct {
    int kind, parent;
    char name[25];
    uint8_t flags, origin, alpha;
    float t[2], rot[3], s[2], size[2];
    /* picture, window content */
    uint8_t vc[4][4];                       /* TL TR BL BR */
    int mat;
    bool has_uv;
    float uv[4][2];
    /* text */
    uint16_t *text;
    int tlen, font_mat;
    uint8_t tpos, talign, tcol[2][4];
    float fsz[2], cspace, lspace;
    /* window */
    float infl[4];                          /* l r t b */
    int nframe;
    lframe_t frame[8];
    /* part */
    struct wlayout *part;
    float mag[2];
} lpane_t;

struct wlayout {
    float w, h;
    lpane_t *p;
    int np;
    lmat_t *m;
    int nm;
    ltex_t *t;
    int nt;
    int missing;
};

/* ---------------------------------------------------------------- reading */

typedef struct {
    const uint8_t *b;
    uint32_t n;
    bool le;
} rd_t;

static uint32_t r32(const rd_t *r, uint32_t at)
{
    if ((uint64_t)at + 4 > r->n) return 0;
    const uint8_t *p = r->b + at;
    return r->le ? (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]
                 : (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint16_t r16(const rd_t *r, uint32_t at)
{
    if ((uint64_t)at + 2 > r->n) return 0;
    const uint8_t *p = r->b + at;
    return r->le ? (uint16_t)(p[1] << 8 | p[0]) : (uint16_t)(p[0] << 8 | p[1]);
}
static float rf(const rd_t *r, uint32_t at)
{
    uint32_t v = r32(r, at);
    float f;
    memcpy(&f, &v, 4);
    return isfinite(f) ? f : 0;
}
static void rcol(const rd_t *r, uint32_t at, uint8_t *c)
{
    if ((uint64_t)at + 4 > r->n) {
        c[0] = c[1] = c[2] = c[3] = 255;
        return;
    }
    memcpy(c, r->b + at, 4);
}
static void rstr(const rd_t *r, uint32_t at, char *out, size_t max)
{
    size_t k = 0;
    while (k + 1 < max && at + k < r->n && r->b[at + k]) {
        out[k] = (char)r->b[at + k];
        k++;
    }
    out[k] = 0;
}

static int add_texture(wlayout_t *l, const char *name)
{
    for (int i = 0; i < l->nt; i++)
        if (!strcmp(l->t[i].name, name)) return i;
    ltex_t *nt = realloc(l->t, (l->nt + 1) * sizeof(ltex_t));
    if (!nt) return -1;
    l->t = nt;
    memset(&l->t[l->nt], 0, sizeof(ltex_t));
    snprintf(l->t[l->nt].name, sizeof(l->t[0].name), "%s", name);
    return l->nt++;
}

/* the first texture of a file, as RGBA */
typedef struct {
    ltex_t *t;
} grab_t;

static bool grab_tex(void *ctx, wtex_t *t, const char *label)
{
    (void)label;
    grab_t *g = ctx;
    if (!g->t->rgba && t->w && t->h && t->w <= 4096 && t->h <= 4096) {
        uint8_t *px = malloc((size_t)t->w * t->h * 4);
        if (px && wtex_decode(t, px)) {
            g->t->rgba = px;
            g->t->w = (int)t->w;
            g->t->h = (int)t->h;
        } else {
            free(px);
        }
    }
    return false;                           /* only the first (and the caller frees it) */
}

static void load_textures(wlayout_t *l, wl_load_fn load, void *ctx)
{
    for (int i = 0; i < l->nt; i++) {
        ltex_t *t = &l->t[i];
        if (t->tried) continue;
        t->tried = true;
        uint32_t len = 0;
        uint8_t *f = load ? load(ctx, t->name, &len) : NULL;
        if (f) {
            grab_t g = { t };
            wiiu_textures(f, len, t->name, grab_tex, &g);
            free(f);
        }
        if (!t->rgba) l->missing++;
    }
}

static void read_materials(wlayout_t *l, const rd_t *r, uint32_t sec, uint32_t end, const int *tex_of, int ntex_names)
{
    int cnt = r16(r, sec + 8);
    l->m = calloc(cnt ? cnt : 1, sizeof(lmat_t));
    if (!l->m) return;
    l->nm = cnt;
    for (int i = 0; i < cnt; i++) {
        lmat_t *m = &l->m[i];
        m->tex = -1;
        m->sx = m->sy = 1;
        memset(m->white, 255, 4);
        uint32_t at = sec + r32(r, sec + 0xC + i * 4);
        if (at + 0x28 > end) continue;
        rcol(r, at + 0x1C, m->black);
        rcol(r, at + 0x20, m->white);
        uint32_t fl = r32(r, at + 0x24), p = at + 0x28;
        int ntex = fl & 3, nsrt = (fl >> 2) & 3;
        for (int k = 0; k < ntex; k++, p += 4) {
            if (k) continue;
            int ti = r16(r, p);
            m->tex = tex_of && ti < ntex_names ? tex_of[ti] : -1;
            m->wrap_s = r->b[p + 2] & 3;
            m->wrap_t = r->b[p + 3] & 3;
        }
        if (nsrt && p + 20 <= end) {
            m->tx = rf(r, p);
            m->ty = rf(r, p + 4);
            m->rot = rf(r, p + 8);
            m->sx = rf(r, p + 12);
            m->sy = rf(r, p + 16);
            m->srt = m->tx || m->ty || m->rot || m->sx != 1 || m->sy != 1;
        }
    }
}

static void read_uv(const rd_t *r, uint32_t at, lpane_t *p)
{
    for (int c = 0; c < 4; c++) {
        p->uv[c][0] = rf(r, at + c * 8);
        p->uv[c][1] = rf(r, at + c * 8 + 4);
    }
    p->has_uv = true;
}

static wlayout_t *parse(const uint8_t *b, uint32_t n, wl_load_fn load, void *ctx, int depth, const char **err);

static void read_part(wlayout_t *l, lpane_t *p, const rd_t *r, uint32_t sec, uint32_t end, wl_load_fn load,
                      void *ctx, int depth)
{
    uint32_t props = r32(r, sec + 0x54);
    p->mag[0] = rf(r, sec + 0x58);
    p->mag[1] = rf(r, sec + 0x5C);
    if (!p->mag[0]) p->mag[0] = 1;
    if (!p->mag[1]) p->mag[1] = 1;
    uint32_t nat = sec + 0x60 + props * 0x28;
    if (props > 256 || nat >= end || depth >= MAX_DEPTH || !load) return;
    char name[64], file[80];
    rstr(r, nat, name, sizeof(name));
    if (!name[0]) return;
    snprintf(file, sizeof(file), "%s.bflyt", name);
    uint32_t len = 0;
    uint8_t *f = load(ctx, file, &len);
    if (!f) return;
    const char *e;
    p->part = parse(f, len, load, ctx, depth + 1, &e);
    free(f);
    (void)l;
}

static wlayout_t *parse(const uint8_t *b, uint32_t n, wl_load_fn load, void *ctx, int depth, const char **err)
{
    *err = "This isn't a Wii U layout (BFLYT)";
    if (n < 0x14 || memcmp(b, "FLYT", 4)) return NULL;
    rd_t r = { b, n, b[4] == 0xFF };
    int *tex_of = NULL, ntex = 0;
    wlayout_t *l = calloc(1, sizeof(*l));
    if (!l) goto oom;
    l->w = 1280;
    l->h = 720;
    uint32_t pos = r16(&r, 6), ns = r16(&r, 0x10);
    int stack[64], sp = 0, last = -1;
    l->p = calloc(MAX_PANES, sizeof(lpane_t));
    if (!l->p) goto oom;

    for (uint32_t k = 0; k < ns && pos + 8 <= n; k++) {
        const uint8_t *h = b + pos;
        uint32_t sz = r32(&r, pos + 4), end = pos + sz;
        if (sz < 8 || end > n) break;
        if (!memcmp(h, "lyt1", 4) && sz >= 0x14) {
            l->w = rf(&r, pos + 0xC);
            l->h = rf(&r, pos + 0x10);
            if (l->w < 1 || l->w > 8192) l->w = 1280;
            if (l->h < 1 || l->h > 8192) l->h = 720;
        } else if (!memcmp(h, "txl1", 4)) {
            ntex = r16(&r, pos + 8);
            free(tex_of);
            tex_of = calloc(ntex ? ntex : 1, sizeof(int));
            if (!tex_of) goto oom;
            for (int i = 0; i < ntex; i++) {
                char name[96];
                rstr(&r, pos + 0xC + r32(&r, pos + 0xC + i * 4), name, sizeof(name));
                tex_of[i] = add_texture(l, name);
            }
        } else if (!memcmp(h, "mat1", 4) && !l->m) {
            read_materials(l, &r, pos, end, tex_of, ntex);
        } else if (!memcmp(h, "pas1", 4)) {
            if (last >= 0 && sp < 64) stack[sp++] = last;
        } else if (!memcmp(h, "pae1", 4)) {
            if (sp > 0) sp--;
        } else if ((!memcmp(h, "pan1", 4) || !memcmp(h, "pic1", 4) || !memcmp(h, "txt1", 4) ||
                    !memcmp(h, "wnd1", 4) || !memcmp(h, "bnd1", 4) || !memcmp(h, "prt1", 4) ||
                    !memcmp(h, "scr1", 4) || !memcmp(h, "ali1", 4)) &&
                   sz >= 0x54 && l->np < MAX_PANES) {
            lpane_t *p = &l->p[l->np];
            p->kind = !memcmp(h, "pan1", 4) ? K_PANE : !memcmp(h, "pic1", 4) ? K_PIC : !memcmp(h, "txt1", 4) ? K_TXT
                    : !memcmp(h, "wnd1", 4) ? K_WND : !memcmp(h, "bnd1", 4) ? K_BND : !memcmp(h, "prt1", 4) ? K_PRT
                    : K_OTHER;
            p->parent = sp ? stack[sp - 1] : -1;
            p->flags = h[8];
            p->origin = h[9];
            p->alpha = h[0xA];
            memcpy(p->name, h + 0xC, 24);
            p->t[0] = rf(&r, pos + 0x2C);
            p->t[1] = rf(&r, pos + 0x30);
            for (int c = 0; c < 3; c++) p->rot[c] = rf(&r, pos + 0x38 + c * 4);
            p->s[0] = rf(&r, pos + 0x44);
            p->s[1] = rf(&r, pos + 0x48);
            p->size[0] = rf(&r, pos + 0x4C);
            p->size[1] = rf(&r, pos + 0x50);
            p->mat = -1;
            memset(p->vc, 255, sizeof(p->vc));

            if (p->kind == K_PIC && sz >= 0x68) {
                for (int c = 0; c < 4; c++) rcol(&r, pos + 0x54 + c * 4, p->vc[c]);
                p->mat = r16(&r, pos + 0x64);
                if (h[0x66] && pos + 0x68 + 32 <= end) read_uv(&r, pos + 0x68, p);
            } else if (p->kind == K_TXT && sz >= 0x80) {
                p->mat = r16(&r, pos + 0x58);
                p->tpos = h[0x5C];
                p->talign = h[0x5D];
                uint32_t to = r32(&r, pos + 0x64);
                rcol(&r, pos + 0x68, p->tcol[0]);
                rcol(&r, pos + 0x6C, p->tcol[1]);
                p->fsz[0] = rf(&r, pos + 0x70);
                p->fsz[1] = rf(&r, pos + 0x74);
                p->cspace = rf(&r, pos + 0x78);
                p->lspace = rf(&r, pos + 0x7C);
                uint32_t len = r16(&r, pos + 0x54);
                if (to && len && pos + (uint64_t)to + len <= end && len < 0x10000) {
                    p->text = malloc(len + 2);
                    if (p->text) {
                        for (uint32_t i = 0; i < len / 2; i++) p->text[i] = r16(&r, pos + to + i * 2);
                        p->tlen = (int)(len / 2);
                        while (p->tlen && !p->text[p->tlen - 1]) p->tlen--;
                    }
                }
            } else if (p->kind == K_WND && sz >= 0x70) {
                for (int c = 0; c < 4; c++) p->infl[c] = (float)(int16_t)r16(&r, pos + 0x54 + c * 2);
                int nf = h[0x64];
                uint32_t co = pos + r32(&r, pos + 0x68), fo = pos + r32(&r, pos + 0x6C);
                if (co + 0x14 <= end) {
                    for (int c = 0; c < 4; c++) rcol(&r, co + c * 4, p->vc[c]);
                    p->mat = r16(&r, co + 0x10);
                    if (b[co + 0x12] && co + 0x14 + 32 <= end) read_uv(&r, co + 0x14, p);
                }
                if (nf > 8) nf = 0;
                for (int i = 0; i < nf; i++) {
                    uint32_t fa = pos + r32(&r, fo + i * 4);
                    if (fo + i * 4 + 4 > end || fa + 4 > end) break;
                    p->frame[i].mat = r16(&r, fa);
                    p->frame[i].flip = b[fa + 2];
                    p->nframe = i + 1;
                }
            } else if (p->kind == K_PRT && sz >= 0x60) {
                read_part(l, p, &r, pos, end, load, ctx, depth);
            }
            if (p->mat >= l->nm) p->mat = -1;
            for (int i = 0; i < p->nframe; i++)
                if (p->frame[i].mat >= l->nm) p->frame[i].mat = -1;
            last = l->np++;
        }
        pos = end;
    }
    free(tex_of);
    if (!l->np) {
        *err = "This layout has no panes";
        wlayout_free(l);
        return NULL;
    }
    load_textures(l, load, ctx);
    *err = NULL;
    return l;
oom:
    free(tex_of);
    wlayout_free(l);
    *err = "Not enough memory for this layout";
    return NULL;
}

wlayout_t *wlayout_load(const uint8_t *b, uint32_t n, wl_load_fn load, void *ctx, const char **err)
{
    return parse(b, n, load, ctx, 0, err);
}

void wlayout_free(wlayout_t *l)
{
    if (!l) return;
    if (l->p)
        for (int i = 0; i < l->np; i++) {
            free(l->p[i].text);
            wlayout_free(l->p[i].part);
        }
    for (int i = 0; i < l->nt; i++) free(l->t[i].rgba);
    free(l->p);
    free(l->m);
    free(l->t);
    free(l);
}

void wlayout_info(const wlayout_t *l, wlayout_info_t *in)
{
    memset(in, 0, sizeof(*in));
    in->w = l->w;
    in->h = l->h;
    in->panes = l->np;
    in->textures = l->nt;
    in->missing = l->missing;
    for (int i = 0; i < l->np; i++) {
        const lpane_t *p = &l->p[i];
        in->pictures += p->kind == K_PIC;
        in->texts += p->kind == K_TXT;
        in->windows += p->kind == K_WND;
        in->parts += p->kind == K_PRT;
        in->hidden += !(p->flags & 1);
    }
}

/* ---------------------------------------------------------------- drawing */

typedef struct {
    float a, b, c, d, x, y;                 /* x' = a x + c y + x, y' = b x + d y + y */
} mtx_t;

static mtx_t mul(const mtx_t *p, const mtx_t *q)
{
    mtx_t o;
    o.a = p->a * q->a + p->c * q->b;
    o.b = p->b * q->a + p->d * q->b;
    o.c = p->a * q->c + p->c * q->d;
    o.d = p->b * q->c + p->d * q->d;
    o.x = p->a * q->x + p->c * q->y + p->x;
    o.y = p->b * q->x + p->d * q->y + p->y;
    return o;
}

typedef struct {
    uint8_t *rgb;
    int w, h;
    const uint8_t *font;
} canvas_t;

/* what a quad is filled with */
typedef struct {
    const ltex_t *tex;
    int wrap_s, wrap_t;
    const lmat_t *mat;
    const uint8_t *glyph;                   /* or an 8x16 glyph: white where set */
    float alpha;                            /* 0..1 */
} fill_t;

static float wrap(float v, int mode)
{
    switch (mode) {
    case 1: return v - floorf(v);
    case 2: {
        float f = v - 2 * floorf(v / 2);
        return f > 1 ? 2 - f : f;
    }
    case 3: {
        float f = fabsf(v);
        return f > 1 ? 1 : f;
    }
    default: return v < 0 ? 0 : v > 1 ? 1 : v;
    }
}

static void sample(const fill_t *f, float u, float v, float *o)
{
    if (f->glyph) {
        int gx = (int)(u * 8), gy = (int)(v * 16);
        if (gx < 0) gx = 0;
        if (gx > 7) gx = 7;
        if (gy < 0) gy = 0;
        if (gy > 15) gy = 15;
        float on = (f->glyph[gy] >> (7 - gx)) & 1 ? 1.f : 0.f;
        o[0] = o[1] = o[2] = 1;
        o[3] = on;
        return;
    }
    const ltex_t *t = f->tex;
    if (!t || !t->rgba) {
        o[0] = o[1] = o[2] = o[3] = 1;
    } else {
        float x = wrap(u, f->wrap_s) * t->w - 0.5f, y = wrap(v, f->wrap_t) * t->h - 0.5f;
        int x0 = (int)floorf(x), y0 = (int)floorf(y);
        float fx = x - x0, fy = y - y0;
        int x1 = x0 + 1, y1 = y0 + 1;
        if (f->wrap_s == 1) {
            x0 = (x0 % t->w + t->w) % t->w;
            x1 = (x1 % t->w + t->w) % t->w;
        }
        if (f->wrap_t == 1) {
            y0 = (y0 % t->h + t->h) % t->h;
            y1 = (y1 % t->h + t->h) % t->h;
        }
        if (x0 < 0) x0 = 0;
        if (x0 >= t->w) x0 = t->w - 1;
        if (x1 < 0) x1 = 0;
        if (x1 >= t->w) x1 = t->w - 1;
        if (y0 < 0) y0 = 0;
        if (y0 >= t->h) y0 = t->h - 1;
        if (y1 < 0) y1 = 0;
        if (y1 >= t->h) y1 = t->h - 1;
        const uint8_t *p00 = t->rgba + ((size_t)y0 * t->w + x0) * 4, *p10 = t->rgba + ((size_t)y0 * t->w + x1) * 4;
        const uint8_t *p01 = t->rgba + ((size_t)y1 * t->w + x0) * 4, *p11 = t->rgba + ((size_t)y1 * t->w + x1) * 4;
        for (int c = 0; c < 4; c++) {
            float top = p00[c] + (p10[c] - p00[c]) * fx, bot = p01[c] + (p11[c] - p01[c]) * fx;
            o[c] = (top + (bot - top) * fy) / 255.f;
        }
    }
    const lmat_t *m = f->mat;
    if (m) {
        if (t && t->rgba)
            for (int c = 0; c < 4; c++) o[c] = (m->black[c] + o[c] * (m->white[c] - m->black[c])) / 255.f;
        else
            for (int c = 0; c < 4; c++) o[c] = m->white[c] / 255.f;
    }
}

typedef struct {
    float x, y, u, v, c[4];
} vert_t;

static void tri(canvas_t *cv, const fill_t *f, const vert_t *p0, const vert_t *p1, const vert_t *p2)
{
    float area = (p1->x - p0->x) * (p2->y - p0->y) - (p2->x - p0->x) * (p1->y - p0->y);
    if (fabsf(area) < 1e-6f) return;
    float minx = fminf(p0->x, fminf(p1->x, p2->x)), maxx = fmaxf(p0->x, fmaxf(p1->x, p2->x));
    float miny = fminf(p0->y, fminf(p1->y, p2->y)), maxy = fmaxf(p0->y, fmaxf(p1->y, p2->y));
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > cv->w) x1 = cv->w;
    if (y1 > cv->h) y1 = cv->h;
    float inv = 1 / area;
    for (int y = y0; y < y1; y++) {
        float py = y + 0.5f;
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f;
            float w0 = ((p1->x - px) * (p2->y - py) - (p2->x - px) * (p1->y - py)) * inv;
            float w1 = ((p2->x - px) * (p0->y - py) - (p0->x - px) * (p2->y - py)) * inv;
            float w2 = 1 - w0 - w1;
            if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;
            float u = w0 * p0->u + w1 * p1->u + w2 * p2->u, v = w0 * p0->v + w1 * p1->v + w2 * p2->v;
            float s[4];
            sample(f, u, v, s);
            float a = s[3] * (w0 * p0->c[3] + w1 * p1->c[3] + w2 * p2->c[3]) * f->alpha;
            if (a <= 0.002f) continue;
            if (a > 1) a = 1;
            uint8_t *d = cv->rgb + ((size_t)y * cv->w + x) * 3;
            for (int c = 0; c < 3; c++) {
                float col = s[c] * (w0 * p0->c[c] + w1 * p1->c[c] + w2 * p2->c[c]) * 255;
                if (col > 255) col = 255;
                d[c] = (uint8_t)(d[c] + (col - d[c]) * a + 0.5f);
            }
        }
    }
}

/* corners in pane space TL TR BL BR, with their texture coordinates and colours (0..255) */
static void quad(canvas_t *cv, const mtx_t *m, const fill_t *f, const float xy[4][2], const float uv[4][2],
                 const uint8_t col[4][4])
{
    vert_t v[4];
    for (int i = 0; i < 4; i++) {
        v[i].x = m->a * xy[i][0] + m->c * xy[i][1] + m->x;
        v[i].y = m->b * xy[i][0] + m->d * xy[i][1] + m->y;
        float u = uv[i][0], w = uv[i][1];
        const lmat_t *mt = f->mat;
        if (mt && mt->srt) {
            float cu = u - 0.5f, cw = w - 0.5f, r = mt->rot * 3.14159265f / 180, cs = cosf(r), sn = sinf(r);
            u = (cu * cs - cw * sn) * mt->sx + 0.5f + mt->tx;
            w = (cu * sn + cw * cs) * mt->sy + 0.5f - mt->ty;
        }
        v[i].u = u;
        v[i].v = w;
        for (int c = 0; c < 4; c++) v[i].c[c] = col ? col[i][c] / 255.f : 1;
    }
    tri(cv, f, &v[0], &v[1], &v[2]);
    tri(cv, f, &v[1], &v[3], &v[2]);
}

static void line(canvas_t *cv, float x0, float y0, float x1, float y1, const uint8_t *c)
{
    int n = (int)(fmaxf(fabsf(x1 - x0), fabsf(y1 - y0))) + 1;
    if (n > 8192) n = 8192;
    for (int i = 0; i <= n; i++) {
        int x = (int)(x0 + (x1 - x0) * i / n), y = (int)(y0 + (y1 - y0) * i / n);
        if (x < 0 || y < 0 || x >= cv->w || y >= cv->h) continue;
        memcpy(cv->rgb + ((size_t)y * cv->w + x) * 3, c, 3);
    }
}

/* the pane's rectangle in its own space: left, top, right, bottom (y up) */
static void pane_rect(const lpane_t *p, float *r)
{
    int ox = p->origin & 3, oy = (p->origin >> 2) & 3;
    float w = p->size[0], h = p->size[1];
    r[0] = ox == 1 ? 0 : ox == 2 ? -w : -w / 2;
    r[2] = r[0] + w;
    r[1] = oy == 1 ? 0 : oy == 2 ? h : h / 2;
    r[3] = r[1] - h;
}

static const ltex_t *mat_tex(const wlayout_t *l, int mat)
{
    if (mat < 0 || mat >= l->nm || l->m[mat].tex < 0 || l->m[mat].tex >= l->nt) return NULL;
    const ltex_t *t = &l->t[l->m[mat].tex];
    return t->rgba ? t : NULL;
}

static fill_t mat_fill(const wlayout_t *l, int mat, float alpha)
{
    fill_t f = { 0 };
    f.alpha = alpha;
    if (mat >= 0 && mat < l->nm) {
        f.mat = &l->m[mat];
        f.tex = mat_tex(l, mat);
        f.wrap_s = f.mat->wrap_s;
        f.wrap_t = f.mat->wrap_t;
    }
    return f;
}

static void rect_xy(float l, float t, float r, float b, float xy[4][2])
{
    xy[0][0] = l, xy[0][1] = t;
    xy[1][0] = r, xy[1][1] = t;
    xy[2][0] = l, xy[2][1] = b;
    xy[3][0] = r, xy[3][1] = b;
}

static void rect_uv(float u0, float v0, float u1, float v1, float uv[4][2])
{
    uv[0][0] = u0, uv[0][1] = v0;
    uv[1][0] = u1, uv[1][1] = v0;
    uv[2][0] = u0, uv[2][1] = v1;
    uv[3][0] = u1, uv[3][1] = v1;
}

/* a window frame's texture placement: flips (0 none, 1 H, 2 V, 3/4/5 turned 90 / 180 / 270) */
static void flip_uv(float uv[4][2], int flip)
{
    for (int i = 0; i < 4; i++) {
        float u = uv[i][0], v = uv[i][1];
        switch (flip) {
        case 1: u = 1 - u; break;
        case 2: v = 1 - v; break;
        case 3: { float t = u; u = v; v = 1 - t; break; }
        case 4: u = 1 - u, v = 1 - v; break;
        case 5: { float t = u; u = 1 - v; v = t; break; }
        default: break;
        }
        uv[i][0] = u, uv[i][1] = v;
    }
}

static void frame_size(const wlayout_t *l, const lpane_t *p, int i, float *fw, float *fh)
{
    const ltex_t *t = i < p->nframe ? mat_tex(l, p->frame[i].mat) : NULL;
    *fw = t ? t->w : 0;
    *fh = t ? t->h : 0;
}

static void draw_frame_piece(canvas_t *cv, const wlayout_t *l, const mtx_t *m, const lpane_t *p, int i, float alpha,
                             float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1)
{
    if (i >= p->nframe || x1 - x0 <= 0 || y0 - y1 <= 0) return;
    fill_t f = mat_fill(l, p->frame[i].mat, alpha);
    if (!f.tex) return;
    f.wrap_s = f.wrap_t = 0;
    float xy[4][2], uv[4][2];
    rect_xy(x0, y0, x1, y1, xy);
    rect_uv(u0, v0, u1, v1, uv);
    flip_uv(uv, p->frame[i].flip);
    quad(cv, m, &f, xy, uv, NULL);
}

static void draw_window(canvas_t *cv, const wlayout_t *l, const mtx_t *m, const lpane_t *p, float alpha)
{
    float r[4];
    pane_rect(p, r);
    float L = r[0], T = r[1], R = r[2], B = r[3], W = R - L, H = T - B;

    /* the content, grown by the inflation */
    float xy[4][2], uv[4][2];
    rect_xy(L - p->infl[0], T + p->infl[2], R + p->infl[1], B - p->infl[3], xy);
    if (p->has_uv) memcpy(uv, p->uv, sizeof(uv));
    else rect_uv(0, 0, 1, 1, uv);
    fill_t f = mat_fill(l, p->mat, alpha);
    quad(cv, m, &f, xy, uv, (const uint8_t (*)[4])p->vc);

    if (p->nframe == 1 || p->nframe == 4) {
        /* the corners turn around the pane, each stretching its last row / column along a side */
        int tl = 0, tr = p->nframe == 4 ? 1 : 0, bl = p->nframe == 4 ? 2 : 0, br = p->nframe == 4 ? 3 : 0;
        float w0, h0, w1, h1, w2, h2, w3, h3;
        frame_size(l, p, tl, &w0, &h0);
        frame_size(l, p, tr, &w1, &h1);
        frame_size(l, p, bl, &w2, &h2);
        frame_size(l, p, br, &w3, &h3);
        if (!w0 || !h0) return;
        /* one frame: the top left corner, flipped for the others */
        lpane_t q = *p;
        if (p->nframe == 1) {
            q.nframe = 4;
            q.frame[1] = q.frame[0];
            q.frame[1].flip = 1;
            q.frame[2] = q.frame[0];
            q.frame[2].flip = 2;
            q.frame[3] = q.frame[0];
            q.frame[3].flip = 4;
            q.frame[0].flip = 0;
        }
        /* top left: along the top */
        draw_frame_piece(cv, l, m, &q, 0, alpha, L, T, R - w1, T - h0, 0, 0, w0 ? (W - w1) / w0 : 1, 1);
        /* top right: down the right */
        draw_frame_piece(cv, l, m, &q, 1, alpha, R - w1, T, R, B + h3, 0, 0, 1, h1 ? (H - h3) / h1 : 1);
        /* bottom right: along the bottom, right to left */
        draw_frame_piece(cv, l, m, &q, 3, alpha, L + w2, B + h3, R, B, w3 ? 1 - (W - w2) / w3 : 0, 0, 1, 1);
        /* bottom left: up the left */
        draw_frame_piece(cv, l, m, &q, 2, alpha, L, T - h0, L + w2, B, 0, h2 ? 1 - (H - h0) / h2 : 0, 1, 1);
    } else if (p->nframe == 8) {
        /* corners TL TR BL BR, then the sides T B L R stretched between them */
        float cw[4], ch[4];
        for (int i = 0; i < 4; i++) frame_size(l, p, i, &cw[i], &ch[i]);
        draw_frame_piece(cv, l, m, p, 0, alpha, L, T, L + cw[0], T - ch[0], 0, 0, 1, 1);
        draw_frame_piece(cv, l, m, p, 1, alpha, R - cw[1], T, R, T - ch[1], 0, 0, 1, 1);
        draw_frame_piece(cv, l, m, p, 2, alpha, L, B + ch[2], L + cw[2], B, 0, 0, 1, 1);
        draw_frame_piece(cv, l, m, p, 3, alpha, R - cw[3], B + ch[3], R, B, 0, 0, 1, 1);
        float tw, th;
        frame_size(l, p, 4, &tw, &th);
        draw_frame_piece(cv, l, m, p, 4, alpha, L + cw[0], T, R - cw[1], T - th, 0, 0, 1, 1);
        frame_size(l, p, 5, &tw, &th);
        draw_frame_piece(cv, l, m, p, 5, alpha, L + cw[2], B + th, R - cw[3], B, 0, 0, 1, 1);
        frame_size(l, p, 6, &tw, &th);
        draw_frame_piece(cv, l, m, p, 6, alpha, L, T - ch[0], L + tw, B + ch[2], 0, 0, 1, 1);
        frame_size(l, p, 7, &tw, &th);
        draw_frame_piece(cv, l, m, p, 7, alpha, R - tw, T - ch[1], R, B + ch[3], 0, 0, 1, 1);
    }
}

/* UTF-16 to the font's code page 437, near enough */
static uint8_t cp437(uint16_t c)
{
    if (c >= 0x20 && c < 0x7F) return (uint8_t)c;
    switch (c) {
    case 0xE9: return 0x82;
    case 0xE8: return 0x8A;
    case 0xE0: return 0x85;
    case 0xE7: return 0x87;
    case 0xFC: return 0x81;
    case 0xF6: return 0x94;
    case 0xE4: return 0x84;
    case 0xF1: return 0xA4;
    case 0xB0: return 0xF8;
    case 0x2022: case 0x30FB: return 0xF9;
    case 0x3000: return ' ';
    default: break;
    }
    if (c >= 0xFF01 && c <= 0xFF5E) return (uint8_t)(c - 0xFF01 + 0x21);   /* full width ASCII */
    return c < 0x20 ? ' ' : 0xFE;           /* a block for the rest (CJK, symbols) */
}

/* the text, with "\x0E" tags taken out; returns its length */
static int clean_text(const lpane_t *p, uint16_t *out, int max)
{
    int k = 0;
    for (int i = 0; i < p->tlen && k < max; i++) {
        uint16_t c = p->text[i];
        if (c == 0x0E) {                    /* tag: group, type, byte length, data */
            if (i + 3 < p->tlen) i += 3 + p->text[i + 3] / 2;
            continue;
        }
        if (c == 0x0F) continue;
        out[k++] = c;
    }
    return k;
}

static void draw_text(canvas_t *cv, const wlayout_t *l, const mtx_t *m, const lpane_t *p, float alpha)
{
    if (!p->tlen || !cv->font) return;
    uint16_t txt[1024];
    int n = clean_text(p, txt, 1024);
    float gh = p->fsz[1] > 0 ? p->fsz[1] : 24, gw = (p->fsz[0] > 0 ? p->fsz[0] : gh) * 0.55f;
    float adv = gw + p->cspace, lh = gh + p->lspace;
    if (adv <= 0) adv = gw;
    if (lh <= 0) lh = gh;

    /* lines and the block's size */
    int starts[64], lens[64], nl = 0;
    for (int i = 0, s = 0; i <= n && nl < 64; i++)
        if (i == n || txt[i] == '\n') {
            starts[nl] = s;
            lens[nl++] = i - s;
            s = i + 1;
        }
    float bw = 0;
    for (int i = 0; i < nl; i++)
        if (lens[i] * adv > bw) bw = lens[i] * adv;
    float bh = nl * lh;

    float r[4];
    pane_rect(p, r);
    /* the console's fonts are proportional and narrower: squeeze what wouldn't fit the pane */
    float pw = r[2] - r[0];
    if (pw > 0 && bw > pw) {
        float k = pw / bw;
        gw *= k;
        adv *= k;
        bw = pw;
    }
    int px = p->tpos & 3, py = (p->tpos >> 2) & 3;
    float left = px == 1 ? r[0] : px == 2 ? r[2] - bw : (r[0] + r[2]) / 2 - bw / 2;
    float top = py == 1 ? r[1] : py == 2 ? r[3] + bh : (r[1] + r[3]) / 2 + bh / 2;
    int la = p->talign ? p->talign : (px == 1 ? 1 : px == 2 ? 3 : 2);

    fill_t f = { 0 };
    f.alpha = alpha;
    const lmat_t *mt = p->mat >= 0 && p->mat < l->nm ? &l->m[p->mat] : NULL;
    for (int li = 0; li < nl; li++) {
        float lw = lens[li] * adv;
        float x = la == 1 ? left : la == 3 ? left + bw - lw : left + (bw - lw) / 2;
        float y = top - li * lh;
        for (int i = 0; i < lens[li]; i++, x += adv) {
            uint8_t ch = cp437(txt[starts[li] + i]);
            if (ch == ' ') continue;
            f.glyph = cv->font + ch * 16;
            float xy[4][2], uv[4][2];
            rect_xy(x, y, x + gw, y - gh, xy);
            rect_uv(0, 0, 1, 1, uv);
            /* the gradient top to bottom, through the material's white */
            uint8_t col[4][4];
            for (int c = 0; c < 4; c++) {
                int top_c = p->tcol[0][c], bot_c = p->tcol[1][c];
                if (mt) {
                    top_c = top_c * mt->white[c] / 255;
                    bot_c = bot_c * mt->white[c] / 255;
                }
                col[0][c] = col[1][c] = (uint8_t)top_c;
                col[2][c] = col[3][c] = (uint8_t)bot_c;
            }
            quad(cv, m, &f, xy, uv, (const uint8_t (*)[4])col);
        }
    }
}

static void draw_layout(canvas_t *cv, const wlayout_t *l, const mtx_t *base, float base_alpha, int flags, int depth)
{
    mtx_t *world = malloc(l->np * sizeof(mtx_t));
    float *galpha = malloc(l->np * sizeof(float));
    bool *shown = malloc(l->np * sizeof(bool));
    if (!world || !galpha || !shown) {
        free(world);
        free(galpha);
        free(shown);
        return;
    }
    for (int i = 0; i < l->np; i++) {
        const lpane_t *p = &l->p[i];
        const lpane_t *par = p->parent >= 0 && p->parent < i ? &l->p[p->parent] : NULL;
        const mtx_t *pm = par ? &world[p->parent] : base;

        /* placed from the parent's anchor (its parent origin), Y up */
        float ax = 0, ay = 0;
        if (par) {
            float r[4];
            pane_rect(par, r);
            int hx = (p->origin >> 4) & 3, hy = (p->origin >> 6) & 3;
            ax = hx == 1 ? r[0] : hx == 2 ? r[2] : (r[0] + r[2]) / 2;
            ay = hy == 1 ? r[1] : hy == 2 ? r[3] : (r[1] + r[3]) / 2;
        }
        float rz = p->rot[2] * 3.14159265f / 180;
        float sx = p->s[0] * cosf(p->rot[1] * 3.14159265f / 180), sy = p->s[1] * cosf(p->rot[0] * 3.14159265f / 180);
        mtx_t loc = { cosf(rz) * sx, sinf(rz) * sx, -sinf(rz) * sy, cosf(rz) * sy, p->t[0] + ax, p->t[1] + ay };
        world[i] = mul(pm, &loc);

        bool vis = (p->flags & 1) || (flags & WL_HIDDEN);
        shown[i] = vis && (!par || shown[p->parent]);
        float pa = par ? ((par->flags & 2) ? galpha[p->parent] : 1) : base_alpha;
        galpha[i] = pa * p->alpha / 255.f;
        if (!shown[i] || galpha[i] <= 0.002f) continue;

        float r[4];
        pane_rect(p, r);
        if (p->kind == K_PIC) {
            float xy[4][2], uv[4][2];
            rect_xy(r[0], r[1], r[2], r[3], xy);
            if (p->has_uv) memcpy(uv, p->uv, sizeof(uv));
            else rect_uv(0, 0, 1, 1, uv);
            fill_t f = mat_fill(l, p->mat, galpha[i]);
            quad(cv, &world[i], &f, xy, uv, (const uint8_t (*)[4])p->vc);
        } else if (p->kind == K_WND) {
            draw_window(cv, l, &world[i], p, galpha[i]);
        } else if (p->kind == K_TXT) {
            draw_text(cv, l, &world[i], p, galpha[i]);
        } else if (p->kind == K_PRT && p->part && depth < MAX_DEPTH) {
            mtx_t mg = { p->mag[0], 0, 0, p->mag[1], 0, 0 };
            mtx_t pm2 = mul(&world[i], &mg);
            draw_layout(cv, p->part, &pm2, galpha[i], flags & ~WL_BOXES, depth + 1);
        }
        if (flags & WL_BOXES) {
            static const uint8_t col[][3] = { { 128, 128, 128 }, { 0, 255, 255 }, { 255, 255, 0 },
                                              { 255, 0, 255 },   { 255, 128, 0 }, { 0, 255, 0 }, { 128, 128, 255 } };
            const uint8_t *c = col[p->kind];
            float pts[4][2] = { { r[0], r[1] }, { r[2], r[1] }, { r[2], r[3] }, { r[0], r[3] } };
            float sxy[4][2];
            for (int k = 0; k < 4; k++) {
                sxy[k][0] = world[i].a * pts[k][0] + world[i].c * pts[k][1] + world[i].x;
                sxy[k][1] = world[i].b * pts[k][0] + world[i].d * pts[k][1] + world[i].y;
            }
            for (int k = 0; k < 4; k++) line(cv, sxy[k][0], sxy[k][1], sxy[(k + 1) & 3][0], sxy[(k + 1) & 3][1], c);
        }
    }
    free(world);
    free(galpha);
    free(shown);
}

void wlayout_render(const wlayout_t *l, uint8_t *rgb, int w, int h, float zoom, float pan_x, float pan_y, int flags,
                    const uint8_t *font)
{
    /* a dark checkerboard: what the layout leaves see-through */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t v = ((x >> 4) ^ (y >> 4)) & 1 ? 40 : 28;
            uint8_t *d = rgb + ((size_t)y * w + x) * 3;
            d[0] = d[1] = d[2] = v;
        }
    float fit = fminf(w / l->w, h / l->h) * (zoom > 0 ? zoom : 1);
    /* layout space (Y up, origin in the middle) to pixels */
    mtx_t base = { fit, 0, 0, -fit, w / 2.f - pan_x * fit, h / 2.f + pan_y * fit };
    canvas_t cv = { rgb, w, h, font };
    draw_layout(&cv, l, &base, 1, flags, 0);
}
