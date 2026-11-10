/*
 * Wii U (Cafe NintendoWare) containers, read-only, for browsing. Layouts follow quatric's nintoolbox
 * (lib-sarc.c, lib-bars.c, lib-bfsar.c, lib-nus3audio.c):
 *
 *   SARC (.sarc, .pack, .szs once Yaz0 is undone)   SFAT nodes, SFNT names; big or little endian
 *   BARS (.bars)          AMTA metadata + FWAV / FSTP audio per asset; names from each AMTA's STRG
 *   BFSAR (.bfsar)        the INFO block's file table (internal files in the FILE block), named after
 *                         the sound / bank / wave archive / group that uses each file
 *   BFWAR (.bfwar)        a wave archive: its FWAV waves
 *   NUS3AUDIO             NUS3 / AUDIINDX: TNNM names, ADOF offsets into PACK (little endian)
 *
 * Every entry is read straight from the file (nothing is unpacked into memory).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "archive.h"

#define HDR_MAX (16u * 1024 * 1024)

static bool head(const src_t *f, uint8_t *h, uint32_t n) { return f->size >= n && src_read_full(f, 0, h, n); }

static uint32_t r32(const uint8_t *p, bool le)
{
    return le ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : be32(p);
}
static uint16_t r16(const uint8_t *p, bool le) { return le ? (uint16_t)(p[0] | p[1] << 8) : be16(p); }

static int32_t raw_read(archive_t *a, const tnode *n, uint64_t off, void *buf, uint32_t len)
{
    if (off >= n->size) return 0;
    if (len > n->size - off) len = (uint32_t)(n->size - off);
    return a->src.read(a->src.ctx, n->a + off, buf, len);
}

static void raw_destroy(archive_t *a) { (void)a; }

static archive_t *raw_new(const char *kind, const src_t *f)
{
    archive_t *a = archive_alloc(kind);
    if (!a) return NULL;
    a->read = raw_read;
    a->destroy = raw_destroy;
    a->base = *f;
    a->src = a->base;
    return a;
}

static archive_t *raw_fail(archive_t *a)
{
    if (a) {
        memset(&a->base, 0, sizeof(a->base));
        memset(&a->src, 0, sizeof(a->src));
        archive_close(a);
    }
    return NULL;
}

/* The first `n` bytes of the file (up to HDR_MAX), or NULL. */
static uint8_t *read_head(const src_t *f, uint32_t n)
{
    if (n > f->size) n = (uint32_t)f->size;
    if (!n || n > HDR_MAX) return NULL;
    uint8_t *b = malloc(n);
    if (b && !src_read_full(f, 0, b, n)) {
        free(b);
        return NULL;
    }
    return b;
}

/* An extension for a member, from its magic. */
static const char *ext_of(const uint8_t *m)
{
    static const struct { const char *magic, *ext; } t[] = {
        { "FWAV", "bfwav" }, { "FSTM", "bfstm" }, { "FSTP", "bfstp" }, { "FSEQ", "bfseq" },
        { "FBNK", "bfbnk" }, { "FWAR", "bfwar" }, { "FWSD", "bfwsd" }, { "FGRP", "bfgrp" },
        { "FSAR", "bfsar" }, { "FLYT", "bflyt" }, { "FLAN", "bflan" }, { "FLIM", "bflim" },
        { "FRES", "bfres" }, { "FFNT", "bffnt" }, { "Gfx2", "gtx" },   { "SARC", "sarc" },
        { "Yaz0", "szs" },   { "IDSP", "idsp" },  { "OPUS", "lopus" }, { "BNSF", "bnsf" },
        { "AMTA", "amta" },  { "BARS", "bars" },  { "BY\0\x01", "byml" }, { "BY\0\x02", "byml" },
        { "MsgS", "msbt" },  { "RFRM", "rfrm" },  { NULL, NULL },
    };
    for (int i = 0; t[i].magic; i++)
        if (!memcmp(m, t[i].magic, 4)) return t[i].ext;
    if (m[0] == 'Y' && m[1] == 'B') return "byml";
    return "bin";
}

/* ---------------------------------------------------------------- SARC */

bool arc_sarc_probe(const src_t *f, const char *name)
{
    (void)name;
    uint8_t h[0x20];
    if (!head(f, h, 0x20) || memcmp(h, "SARC", 4)) return false;
    bool le = h[6] == 0xFF && h[7] == 0xFE;
    return r16(h + 4, le) == 0x14 && !memcmp(h + 0x14, "SFAT", 4);
}

archive_t *arc_sarc_open(const src_t *f, const char *name)
{
    (void)name;
    uint8_t h[0x20];
    if (!head(f, h, 0x20)) return NULL;
    bool le = h[6] == 0xFF && h[7] == 0xFE;
    uint32_t data = r32(h + 0xC, le);
    uint16_t nodes = r16(h + 0x1A, le);
    uint32_t sfat_len = r16(h + 0x18, le);
    if (data > f->size || data < 0x14 + sfat_len) return NULL;
    uint8_t *b = read_head(f, data);
    if (!b) return NULL;
    archive_t *a = NULL;
    uint32_t nodes_at = 0x14 + sfat_len, sfnt = nodes_at + nodes * 16u;
    if (sfnt + 8 > data || memcmp(b + sfnt, "SFNT", 4)) goto out;
    uint32_t names = sfnt + r16(b + sfnt + 4, le);
    a = raw_new("SARC", f);
    if (!a) goto out;
    for (uint32_t i = 0; i < nodes; i++) {
        const uint8_t *n = b + nodes_at + i * 16;
        uint32_t attr = r32(n + 4, le), s = r32(n + 8, le), e = r32(n + 12, le);
        if (e < s || (uint64_t)data + e > f->size) continue;
        char nm[300];
        if (attr >> 24) {
            uint32_t at = names + (attr & 0xFFFFFF) * 4;
            if (at >= data) continue;
            size_t l = strnlen((const char *)b + at, data - at);
            if (l >= sizeof(nm)) l = sizeof(nm) - 1;
            memcpy(nm, b + at, l);
            nm[l] = 0;
        } else {
            uint8_t m[4] = { 0 };
            if (e - s >= 4) src_read_full(f, data + s, m, 4);
            snprintf(nm, sizeof(nm), "%08X.%s", (unsigned)r32(n, le), ext_of(m));
        }
        while (nm[0] == '/') memmove(nm, nm + 1, strlen(nm));
        if (!nm[0]) continue;
        if (tree_add_path(&a->tree, nm, false, e - s, data + s, 0, 0) < 0) {
            a = raw_fail(a);
            break;
        }
    }
out:
    free(b);
    return a;
}

/* ---------------------------------------------------------------- BARS */

bool arc_bars_probe(const src_t *f, const char *name)
{
    (void)name;
    uint8_t h[0x10];
    return head(f, h, 0x10) && !memcmp(h, "BARS", 4) &&
           ((h[8] == 0xFE && h[9] == 0xFF) || (h[8] == 0xFF && h[9] == 0xFE));
}

/* The member's size from its own header: FWAV / FSTP keep it at +0xC, BWAV at +8 is not one. */
static uint32_t member_size(const src_t *f, uint64_t at, bool le, uint64_t limit)
{
    uint8_t m[0x10];
    if (at + 0x10 > f->size || !src_read_full(f, at, m, 0x10)) return 0;
    bool mle = m[4] == 0xFF && m[5] == 0xFE;
    uint32_t sz = r32(m + 0xC, mle);
    (void)le;
    if (!sz || at + sz > limit) sz = (uint32_t)(limit - at);
    return sz;
}

archive_t *arc_bars_open(const src_t *f, const char *name)
{
    (void)name;
    uint8_t h[0x10];
    if (!head(f, h, 0x10)) return NULL;
    bool le = h[8] == 0xFF;
    uint32_t n = r32(h + 0xC, le);
    if (!n || n > 100000 || 0x10 + n * 12ull > f->size) return NULL;
    uint8_t *b = read_head(f, 0x10 + n * 12);
    if (!b) return NULL;
    archive_t *a = raw_new("BARS", f);
    for (uint32_t i = 0; a && i < n; i++) {
        const uint8_t *p = b + 0x10 + n * 4 + i * 8;
        uint32_t amta = r32(p, le), audio = r32(p + 4, le);
        char nm[128] = "";
        uint8_t am[0x40];
        if (amta + 0x40ull <= f->size && src_read_full(f, amta, am, sizeof(am)) && !memcmp(am, "AMTA", 4)) {
            bool ale = am[4] == 0xFF;
            uint32_t asz = r32(am + 8, ale);
            /* the STRG section somewhere in the AMTA: a length, then the name */
            if (asz >= 0x20 && asz <= 0x10000 && amta + (uint64_t)asz <= f->size) {
                uint8_t *ab = malloc(asz);
                if (ab && src_read_full(f, amta, ab, asz))
                    for (uint32_t k = 0; k + 12 <= asz; k += 4)
                        if (!memcmp(ab + k, "STRG", 4)) {
                            uint32_t l = r32(ab + k + 4, ale);
                            if (l && l < sizeof(nm) && k + 8 + l <= asz) {
                                memcpy(nm, ab + k + 8, l);
                                nm[l] = 0;
                            }
                            break;
                        }
                free(ab);
            }
            if (!nm[0]) {
                /* Switch-style AMTA: a relative name pointer at +0x24 */
                uint64_t at = amta + 0x24ull + r32(am + 0x24, ale);
                if (at < f->size) {
                    uint8_t s[64] = { 0 };
                    src_read_full(f, at, s, at + sizeof(s) <= f->size ? sizeof(s) : (uint32_t)(f->size - at));
                    s[sizeof(s) - 1] = 0;
                    snprintf(nm, sizeof(nm), "%s", (char *)s);
                }
            }
        }
        tree_clean_name(nm);
        if (!nm[0]) snprintf(nm, sizeof(nm), "audio_%04u", (unsigned)i);
        if (audio != 0xFFFFFFFF && audio < f->size) {
            uint8_t m[4] = { 0 };
            src_read_full(f, audio, m, 4);
            uint32_t sz = member_size(f, audio, le, f->size);
            char full[160];
            snprintf(full, sizeof(full), "%s.%s", nm, ext_of(m));
            if (sz && tree_add(&a->tree, 0, full, false, sz, audio, 0, 0) < 0) a = raw_fail(a);
        }
    }
    free(b);
    return a;
}

/* ---------------------------------------------------------------- BFWAR */

bool arc_bfwar_probe(const src_t *f, const char *name)
{
    (void)name;
    uint8_t h[0x20];
    return head(f, h, 0x20) && !memcmp(h, "FWAR", 4) &&
           ((h[4] == 0xFE && h[5] == 0xFF) || (h[4] == 0xFF && h[5] == 0xFE));
}

archive_t *arc_bfwar_open(const src_t *f, const char *name)
{
    (void)name;
    uint8_t h[0x40];
    if (!head(f, h, 0x40)) return NULL;
    bool le = h[4] == 0xFF;
    uint16_t nb = r16(h + 0x10, le);
    uint32_t info = 0, file = 0;
    for (int i = 0; i < nb && i < 4; i++) {
        uint16_t id = r16(h + 0x14 + i * 12, le);
        uint32_t off = r32(h + 0x18 + i * 12, le);
        if (id == 0x6800) info = off;
        else if (id == 0x6801) file = off;
    }
    if (!info || !file || info + 12ull > f->size) return NULL;
    uint8_t ih[12];
    if (!src_read_full(f, info, ih, 12) || memcmp(ih, "INFO", 4)) return NULL;
    uint32_t n = r32(ih + 8, le);
    if (n > 100000 || info + 12 + n * 12ull > f->size) return NULL;
    uint8_t *t = malloc(n * 12 + 1);
    if (!t || !src_read_full(f, info + 12, t, n * 12)) {
        free(t);
        return NULL;
    }
    archive_t *a = raw_new("BFWAR", f);
    for (uint32_t i = 0; a && i < n; i++) {
        uint32_t off = r32(t + i * 12 + 4, le), sz = r32(t + i * 12 + 8, le);
        uint64_t at = (uint64_t)file + 8 + off;
        if (off == 0xFFFFFFFF || !sz || at + sz > f->size) continue;
        char nm[32];
        snprintf(nm, sizeof(nm), "wave_%04u.bfwav", (unsigned)i);
        if (tree_add(&a->tree, 0, nm, false, sz, at, 0, 0) < 0) a = raw_fail(a);
    }
    free(t);
    return a;
}

/* ---------------------------------------------------------------- BFSAR */

bool arc_bfsar_probe(const src_t *f, const char *name)
{
    (void)name;
    uint8_t h[0x20];
    return head(f, h, 0x20) && !memcmp(h, "FSAR", 4) &&
           ((h[4] == 0xFE && h[5] == 0xFF) || (h[4] == 0xFF && h[5] == 0xFE));
}

typedef struct {
    const uint8_t *b;     /* the STRG and INFO blocks, read whole */
    uint32_t len;
    bool le;
} blk_t;

static bool in(const blk_t *k, const uint8_t *p, uint32_t n) { return p >= k->b && p + n <= k->b + k->len; }

/* STRG: the name of an item id (type << 24 | index), through the lookup tree's leaves. */
static void strg_name(const blk_t *s, uint32_t id, char *out, size_t n)
{
    out[0] = 0;
    if (!s->b) return;
    const uint8_t *blk = s->b;
    int32_t st = (int32_t)r32(blk + 0xC, s->le), lk = (int32_t)r32(blk + 0x14, s->le);
    if (st == -1 || lk == -1) return;
    const uint8_t *tab = blk + st + 8, *look = blk + lk + 8;
    if (!in(s, tab, 4) || !in(s, look, 8)) return;
    uint32_t ns = r32(tab, s->le), nn = r32(look + 4, s->le);
    if (!in(s, look + 8, nn * 20)) return;
    for (uint32_t i = 0; i < nn; i++) {
        const uint8_t *r = look + 8 + i * 20;
        if (!r16(r, s->le) || r32(r + 0x10, s->le) != id) continue;
        uint32_t si = r32(r + 0xC, s->le);
        if (si >= ns || !in(s, tab + 4 + si * 12, 12)) return;
        int32_t off = (int32_t)r32(tab + 4 + si * 12 + 4, s->le);
        uint32_t sz = r32(tab + 4 + si * 12 + 8, s->le);
        const uint8_t *str = blk + off + 24;
        if (!sz || !in(s, str, sz)) return;
        if (sz > n) sz = (uint32_t)n;
        memcpy(out, str, sz - 1);
        out[sz - 1] = 0;
        return;
    }
}

/* Files the sound archive names but doesn't hold (streams in a folder beside it): read from there, one
 * open at a time. */
typedef struct {
    char dir[512];
    char (*path)[160];                      /* each one's path under dir */
    int n;
    int cur;
    src_t cache;
} bfsar_priv;

static void bfsar_destroy(archive_t *a)
{
    bfsar_priv *p = a->priv;
    if (!p) return;
    if (p->cache.read) src_close(&p->cache);
    free(p->path);
    free(p);
    a->priv = NULL;
}

static int32_t bfsar_read(archive_t *a, const tnode *n, uint64_t off, void *buf, uint32_t len)
{
    if (!n->b) return raw_read(a, n, off, buf, len);
    bfsar_priv *p = a->priv;
    int i = (int)n->a;
    if (!p || i < 0 || i >= p->n) return -1;
    if (off >= n->size) return 0;
    if (len > n->size - off) len = (uint32_t)(n->size - off);
    if (!p->cache.read || p->cur != i) {
        if (p->cache.read) src_close(&p->cache);
        memset(&p->cache, 0, sizeof(p->cache));
        char full[700];
        snprintf(full, sizeof(full), "%s/%s", p->dir, p->path[i]);
        if (!archive_file_open(full, &p->cache)) {
            memset(&p->cache, 0, sizeof(p->cache));
            return -1;
        }
        p->cur = i;
    }
    return p->cache.read(p->cache.ctx, off, buf, len);
}

/* The path an external file record names (the record's reference points at a NUL-ended path), or NULL. */
static const char *bfsar_ext_path(const blk_t *k, const uint8_t *rec)
{
    int32_t off = (int32_t)r32(rec + 4, k->le);
    const uint8_t *s = rec + off;
    if (off <= 0 || !in(k, s, 2)) return NULL;
    size_t max = (size_t)(k->b + k->len - s), i;
    for (i = 0; i < max && s[i]; i++)
        if (s[i] < 0x20 || s[i] > 0x7E) return NULL;
    return i && i < max && i < 150 ? (const char *)s : NULL;
}

archive_t *arc_bfsar_open(const src_t *f, const char *name)
{
    (void)name;
    uint8_t h[0x60];
    if (!head(f, h, 0x60)) return NULL;
    bool le = h[4] == 0xFF;
    uint16_t nb = r16(h + 0x10, le);
    uint32_t so = 0, ss = 0, io = 0, is = 0, fo = 0;
    for (int i = 0; i < nb && i < 6; i++) {
        uint32_t off = r32(h + 0x18 + i * 12, le), sz = r32(h + 0x1C + i * 12, le);
        switch (r16(h + 0x14 + i * 12, le)) {
        case 0x2000: so = off, ss = sz; break;
        case 0x2001: io = off, is = sz; break;
        case 0x2002: fo = off; break;
        }
    }
    if (!io || !fo || is < 0x48 || is > HDR_MAX || io + (uint64_t)is > f->size) return NULL;
    blk_t S = { 0 }, I = { 0 };
    I.le = S.le = le;
    I.len = is;
    I.b = malloc(is);
    if (!I.b || !src_read_full(f, io, (void *)I.b, is) || memcmp(I.b, "INFO", 4)) goto fail;
    if (so && ss >= 0x18 && ss <= HDR_MAX && so + (uint64_t)ss <= f->size) {
        S.len = ss;
        S.b = malloc(ss);
        if (S.b && (!src_read_full(f, so, (void *)S.b, ss) || memcmp(S.b, "STRG", 4))) {
            free((void *)S.b);
            S.b = NULL;
        }
    }

    /* the items that own a file: sounds (0x01), banks (0x03), wave archives (0x05), groups (0x06) */
    const uint8_t *begin = I.b + 8;
    uint32_t nfiles = 0;
    const uint8_t *ftab = NULL;
    {
        int32_t off = (int32_t)r32(begin + 6 * 8 + 4, le);
        if (off != -1 && in(&I, begin + off, 4)) {
            ftab = begin + off;
            nfiles = r32(ftab, le);
            if (nfiles > 100000 || !in(&I, ftab + 4, nfiles * 8)) nfiles = 0;
        }
    }
    char (*names)[96] = nfiles ? calloc(nfiles, 96) : NULL;
    static const struct { int sect; uint32_t type; } owners[] = { { 3, 0x05 }, { 2, 0x03 }, { 4, 0x06 }, { 0, 0x01 } };
    for (int o = 0; names && o < 4; o++) {
        int32_t off = (int32_t)r32(begin + owners[o].sect * 8 + 4, le);
        if (off == -1 || !in(&I, begin + off, 4)) continue;
        const uint8_t *t = begin + off;
        uint32_t cnt = r32(t, le);
        if (cnt > 200000 || !in(&I, t + 4, cnt * 8)) continue;
        for (uint32_t i = 0; i < cnt; i++) {
            int32_t eo = (int32_t)r32(t + 4 + i * 8 + 4, le);
            if (eo == -1 || !in(&I, t + eo, 4)) continue;
            uint32_t fid = r32(t + eo, le);
            if (fid >= nfiles || names[fid][0]) continue;
            strg_name(&S, owners[o].type << 24 | i, names[fid], 96);
        }
    }

    archive_t *a = raw_new("BFSAR", f);
    for (uint32_t i = 0; a && i < nfiles; i++) {
        int32_t eo = (int32_t)r32(ftab + 4 + i * 8 + 4, le);
        if (eo == -1 || !in(&I, ftab + eo, 24)) continue;
        const uint8_t *rec = ftab + eo;
        if (r16(rec, le) != 0x220C) continue;                  /* external files live elsewhere */
        uint32_t off = r32(rec + 16, le), sz = r32(rec + 20, le);
        uint64_t at = (uint64_t)fo + 8 + off;
        if (off == 0xFFFFFFFF || !sz || at + sz > f->size) continue;
        uint8_t m[4] = { 0 };
        src_read_full(f, at, m, 4);
        char nm[128];
        char base[96];
        snprintf(base, sizeof(base), "%s", names ? names[i] : "");
        tree_clean_name(base);
        if (base[0]) snprintf(nm, sizeof(nm), "%s.%s", base, ext_of(m));
        else snprintf(nm, sizeof(nm), "file_%04u.%s", (unsigned)i, ext_of(m));
        if (tree_find_child(&a->tree, 0, nm) >= 0)
            snprintf(nm, sizeof(nm), "%s_%04u.%s", base[0] ? base : "file", (unsigned)i, ext_of(m));
        if (tree_add(&a->tree, 0, nm, false, sz, at, 0, 0) < 0) a = raw_fail(a);
    }

    /* Sequences: every sequence sound whose FSEQ is inside, playable with its bank (A in the browser).
     * Each entry is its FSEQ file; the player finds the sound again by name. */
    int seqdir = -1;
    int32_t soff = (int32_t)r32(begin + 4, le);
    const uint8_t *st = soff != -1 && r16(begin, le) == 0x2100 && in(&I, begin + soff, 4) ? begin + soff : NULL;
    uint32_t nsnd = st ? r32(st, le) : 0;
    if (nsnd > 200000 || (st && !in(&I, st + 4, nsnd * 8))) nsnd = 0;
    for (uint32_t i = 0; a && i < nsnd; i++) {
        int32_t eo = (int32_t)r32(st + 4 + i * 8 + 4, le);
        if (eo == -1 || !in(&I, st + eo, 0x14) || r16(st + eo + 0xC, le) != 0x2203) continue;
        uint32_t fid = r32(st + eo, le);
        if (fid >= nfiles) continue;
        int32_t fe = (int32_t)r32(ftab + 4 + fid * 8 + 4, le);
        if (fe == -1 || !in(&I, ftab + fe, 24) || r16(ftab + fe, le) != 0x220C) continue;
        uint32_t off = r32(ftab + fe + 16, le), sz = r32(ftab + fe + 20, le);
        uint64_t at = (uint64_t)fo + 8 + off;
        if (off == 0xFFFFFFFF || !sz || at + sz > f->size) continue;
        char sn[96], nm[128];
        strg_name(&S, 0x01000000u | i, sn, sizeof(sn));
        if (sn[0]) snprintf(nm, sizeof(nm), "%s.seq", sn);
        else snprintf(nm, sizeof(nm), "sound%04u.seq", (unsigned)i);
        tree_clean_name(nm);
        if (seqdir < 0 && (seqdir = tree_add(&a->tree, 0, "Sequences", true, 0, 0, 0, 0)) < 0) break;
        tree_add(&a->tree, seqdir, nm, false, sz, at, 0, 0);
    }
    /* External files: those of the records that name a path, found beside the archive */
    char dir[512];
    if (a && archive_opening_dir(dir, sizeof(dir))) {
        bfsar_priv *pr = NULL;
        int extdir = -1;
        for (uint32_t i = 0; i < nfiles; i++) {
            int32_t eo = (int32_t)r32(ftab + 4 + i * 8 + 4, le);
            if (eo == -1 || !in(&I, ftab + eo, 8) || r16(ftab + eo, le) != 0x220D) continue;
            const char *path = bfsar_ext_path(&I, ftab + eo);
            if (!path) continue;
            while (*path == '/') path++;
            /* its path under the archive's folder, else just its name there */
            const char *base = strrchr(path, '/');
            const char *tries[2] = { path, base ? base + 1 : NULL };
            const char *use = NULL;
            uint64_t sz = 0;
            for (int t = 0; t < 2 && !use; t++) {
                char full[700];
                if (!tries[t]) continue;
                snprintf(full, sizeof(full), "%s/%s", dir, tries[t]);
                if (archive_file_size(full, &sz) && sz) use = tries[t];
            }
            if (!use) continue;
            if (!pr) {
                pr = calloc(1, sizeof(*pr));
                if (!pr) break;
                snprintf(pr->dir, sizeof(pr->dir), "%s", dir);
                pr->cur = -1;
                pr->path = calloc(nfiles, sizeof(*pr->path));
                if (!pr->path) {
                    free(pr);
                    pr = NULL;
                    break;
                }
                a->priv = pr;
                a->read = bfsar_read;
                a->destroy = bfsar_destroy;
            }
            snprintf(pr->path[i], sizeof(pr->path[i]), "%s", use);
            pr->n = (int)nfiles;
            char rel[200], clean[160];
            snprintf(clean, sizeof(clean), "%s", path);
            if (extdir < 0 && (extdir = tree_add(&a->tree, 0, "External", true, 0, 0, 0, 0)) < 0) break;
            snprintf(rel, sizeof(rel), "External/%s", clean);
            if (tree_find(&a->tree, rel) < 0) tree_add_path(&a->tree, rel, false, sz, i, 1, 0);
        }
    }
    free(names);
    free((void *)I.b);
    free((void *)S.b);
    return a;
fail:
    free((void *)I.b);
    return NULL;
}

/* ---------------------------------------------------------------- NUS3AUDIO */

bool arc_nus3_probe(const src_t *f, const char *name)
{
    (void)name;
    uint8_t h[0x14];
    return head(f, h, 0x14) && !memcmp(h, "NUS3", 4) && !memcmp(h + 8, "AUDIINDX", 8);
}

archive_t *arc_nus3_open(const src_t *f, const char *name)
{
    (void)name;
    uint8_t *b = read_head(f, f->size < HDR_MAX ? (uint32_t)f->size : 0x100000);
    if (!b) return NULL;
    uint32_t len = f->size < HDR_MAX ? (uint32_t)f->size : 0x100000;
    const uint8_t *nmof = NULL, *adof = NULL, *tnnm = NULL;
    uint32_t nt = 0, nmofn = 0, adofn = 0, tnnmn = 0;
    for (uint32_t pos = 8; pos + 8 <= len;) {
        uint32_t tl = pos + 12 <= len && !memcmp(b + pos, "AUDIINDX", 8) ? 8 : 4;
        uint32_t cs = r32(b + pos + tl, true), pl = pos + tl + 4;
        if (!memcmp(b + pos, "PACK", 4)) break;                /* the payloads: not needed here */
        if (cs > len - pl) break;
        if (tl == 8 && cs >= 4) nt = r32(b + pl, true);
        else if (!memcmp(b + pos, "NMOF", 4)) nmof = b + pl, nmofn = cs;
        else if (!memcmp(b + pos, "ADOF", 4)) adof = b + pl, adofn = cs;
        else if (!memcmp(b + pos, "TNNM", 4)) tnnm = b + pl, tnnmn = cs;
        pos = pl + cs;
    }
    archive_t *a = NULL;
    if (!nt || nt > 100000 || !adof || adofn < nt * 8) goto out;
    a = raw_new("NUS3AUDIO", f);
    for (uint32_t i = 0; a && i < nt; i++) {
        uint32_t off = r32(adof + i * 8, true), sz = r32(adof + i * 8 + 4, true);
        if (!sz || (uint64_t)off + sz > f->size) continue;
        char nm[128] = "";
        if (tnnm && nmof && nmofn >= (i + 1) * 4) {
            uint32_t no = r32(nmof + i * 4, true);
            if (no < tnnmn) {
                size_t l = strnlen((const char *)tnnm + no, tnnmn - no);
                if (l >= sizeof(nm)) l = sizeof(nm) - 1;
                memcpy(nm, tnnm + no, l);
                nm[l] = 0;
            }
        }
        tree_clean_name(nm);
        if (!nm[0]) snprintf(nm, sizeof(nm), "track_%04u", (unsigned)i);
        uint8_t m[4] = { 0 };
        src_read_full(f, off, m, 4);
        char full[160];
        snprintf(full, sizeof(full), "%s.%s", nm, ext_of(m));
        if (tree_add(&a->tree, 0, full, false, sz, off, 0, 0) < 0) a = raw_fail(a);
    }
out:
    free(b);
    return a;
}
