/*
 * Wii U data files shown as text (after quatric's nintoolbox: lib-byml.c, lib-xmb.c, lib-aamp.c /
 * lib-bglpbd.c, lib-bflyt.c, lib-sequence.c):
 *
 *   BYAML / BYML (v1-v4, big or little endian)   the node tree, YAML style
 *   XMB (Smash Bros.)                            the element tree, XML style
 *   AAMP parameter archives, BGLPBD among them   lists / objects / parameters (v1 and v2)
 *   BFLYT                                        textures, fonts, materials, the pane tree, texts
 *   BFLAN                                        the tag, its groups, the animated panes and their curves
 *   BFSEQ                                        the sequence labels and the data block
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <zlib.h>

#include "wiiufmt.h"

#define OUT_MAX (2u << 20)

typedef struct {
    char *p;
    uint32_t n, cap;
    bool full;
} sb_t;

static void sb_put(sb_t *s, const char *t, uint32_t n)
{
    if (s->full) return;
    if (s->n + n + 1 > OUT_MAX) {
        s->full = true;
        n = OUT_MAX - s->n - 1;
    }
    if (s->n + n + 1 > s->cap) {
        uint32_t nc = (s->n + n + 1) * 2;
        if (nc > OUT_MAX) nc = OUT_MAX;
        char *np = realloc(s->p, nc);
        if (!np) {
            s->full = true;
            return;
        }
        s->p = np;
        s->cap = nc;
    }
    memcpy(s->p + s->n, t, n);
    s->n += n;
    s->p[s->n] = 0;
}

static void pf(sb_t *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void pf(sb_t *s, const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n > 0) sb_put(s, line, n < (int)sizeof(line) ? (uint32_t)n : sizeof(line) - 1);
}

static void indent(sb_t *s, int d)
{
    for (int i = 0; i < d && i < 40; i++) sb_put(s, "  ", 2);
}

static uint32_t e32(const uint8_t *p, bool le)
{
    return le ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24
              : (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint16_t e16(const uint8_t *p, bool le) { return le ? (uint16_t)(p[0] | p[1] << 8) : (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t e24(const uint8_t *p, bool le)
{
    return le ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 : (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
}
static float ef(const uint8_t *p, bool le)
{
    uint32_t v = e32(p, le);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* A NUL-terminated string at `at`, or "" when it runs off the buffer. */
static const char *cstr(const uint8_t *b, uint32_t n, uint64_t at, char *tmp, size_t tn)
{
    tmp[0] = 0;
    if (at >= n) return tmp;
    size_t l = strnlen((const char *)b + at, n - at);
    if (l >= tn) l = tn - 1;
    memcpy(tmp, b + at, l);
    tmp[l] = 0;
    for (size_t i = 0; i < l; i++)
        if ((uint8_t)tmp[i] < 0x20) tmp[i] = '?';
    return tmp;
}

/* UTF-16 text as UTF-8. */
static void put_u16(sb_t *s, const uint8_t *p, uint32_t bytes, bool le)
{
    for (uint32_t i = 0; i + 1 < bytes; i += 2) {
        uint32_t c = e16(p + i, le);
        if (!c) break;
        char o[4];
        if (c == '\n') sb_put(s, "\\n", 2);
        else if (c < 0x20) pf(s, "{%02X}", (unsigned)c);
        else if (c < 0x80) o[0] = (char)c, sb_put(s, o, 1);
        else if (c < 0x800) o[0] = (char)(0xC0 | c >> 6), o[1] = (char)(0x80 | (c & 63)), sb_put(s, o, 2);
        else o[0] = (char)(0xE0 | c >> 12), o[1] = (char)(0x80 | ((c >> 6) & 63)), o[2] = (char)(0x80 | (c & 63)), sb_put(s, o, 3);
    }
}

/* ================================================================ BYML */

typedef struct {
    const uint8_t *b;
    uint32_t n;
    bool le;
    uint32_t keys, strs;    /* string table nodes */
    int ver;
} byml_t;

static const char *byml_str(const byml_t *y, uint32_t tab, uint32_t idx, char *tmp, size_t tn)
{
    tmp[0] = 0;
    if (!tab || tab + 8 > y->n || y->b[tab] != 0xC2) return tmp;
    uint32_t cnt = e24(y->b + tab + 1, y->le);
    if (idx >= cnt || tab + 8 + idx * 4ull > y->n) return tmp;
    return cstr(y->b, y->n, (uint64_t)tab + e32(y->b + tab + 4 + idx * 4, y->le), tmp, tn);
}

static void byml_value(sb_t *s, const byml_t *y, uint8_t type, uint32_t v, int depth);

static void byml_node(sb_t *s, const byml_t *y, uint32_t off, int depth)
{
    if (depth > 64 || off + 4 > y->n) {
        sb_put(s, "<bad>\n", 6);
        return;
    }
    uint8_t t = y->b[off];
    uint32_t cnt = e24(y->b + off + 1, y->le);
    char tmp[256];
    if (t == 0xC0) {                                     /* array */
        uint32_t vals = off + 4 + ((cnt + 3) & ~3u);
        if (!cnt) {
            sb_put(s, " []\n", 4);
            return;
        }
        sb_put(s, "\n", 1);
        if ((uint64_t)vals + cnt * 4ull > y->n) return;
        for (uint32_t i = 0; i < cnt && !s->full; i++) {
            indent(s, depth);
            sb_put(s, "-", 1);
            byml_value(s, y, y->b[off + 4 + i], e32(y->b + vals + i * 4, y->le), depth + 1);
        }
    } else if (t == 0xC1) {                              /* hash */
        if (!cnt) {
            sb_put(s, " {}\n", 4);
            return;
        }
        sb_put(s, "\n", 1);
        if (off + 4 + cnt * 8ull > y->n) return;
        for (uint32_t i = 0; i < cnt && !s->full; i++) {
            const uint8_t *e = y->b + off + 4 + i * 8;
            indent(s, depth);
            pf(s, "%s:", byml_str(y, y->keys, e24(e, y->le), tmp, sizeof(tmp)));
            byml_value(s, y, e[3], e32(e + 4, y->le), depth + 1);
        }
    } else {
        pf(s, " <node %02X>\n", t);
    }
}

static void byml_value(sb_t *s, const byml_t *y, uint8_t type, uint32_t v, int depth)
{
    char tmp[256];
    float f;
    switch (type) {
    case 0xA0: pf(s, " \"%s\"\n", byml_str(y, y->strs, v, tmp, sizeof(tmp))); break;
    case 0xA1: pf(s, " <binary at %X>\n", (unsigned)v); break;
    case 0xC0: case 0xC1: byml_node(s, y, v, depth); break;
    case 0xD0: pf(s, " %s\n", v ? "true" : "false"); break;
    case 0xD1: pf(s, " %d\n", (int)v); break;
    case 0xD2: memcpy(&f, &v, 4); pf(s, " %g\n", (double)f); break;
    case 0xD3: pf(s, " %uu\n", (unsigned)v); break;
    case 0xD4: case 0xD5: case 0xD6:
        if (v + 8ull <= y->n) {
            uint64_t q = (uint64_t)e32(y->b + v + (y->le ? 4 : 0), y->le) << 32 | e32(y->b + v + (y->le ? 0 : 4), y->le);
            if (type == 0xD4) pf(s, " %lld\n", (long long)q);
            else if (type == 0xD5) pf(s, " %lluu\n", (unsigned long long)q);
            else {
                double d;
                memcpy(&d, &q, 8);
                pf(s, " %g\n", d);
            }
        } else sb_put(s, " <bad>\n", 7);
        break;
    case 0xFF: sb_put(s, " null\n", 6); break;
    default: pf(s, " <type %02X: %08X>\n", type, (unsigned)v); break;
    }
}

static bool dump_byml(sb_t *s, const uint8_t *b, uint32_t n)
{
    if (n < 0x10) return false;
    byml_t y = { b, n, b[0] == 'Y', 0, 0, 0 };
    y.ver = e16(b + 2, y.le);
    if (y.ver < 1 || y.ver > 7) return false;
    y.keys = e32(b + 4, y.le);
    y.strs = e32(b + 8, y.le);
    uint32_t root = e32(b + 12, y.le);
    /* the oldest files have a path table before the root */
    if (root && root < n && b[root] == 0xC3 && n >= 0x14) root = e32(b + 16, y.le);
    pf(&*s, "BYML version %d, %s endian\n", y.ver, y.le ? "little" : "big");
    if (!root) {
        sb_put(s, "(empty)\n", 8);
        return true;
    }
    sb_put(s, "root:", 5);
    byml_node(s, &y, root, 1);
    return true;
}

/* ================================================================ XMB */

static bool dump_xmb(sb_t *s, const uint8_t *b, uint32_t n)
{
    if (n < 44) return false;
    uint32_t nodes = e32(b + 4, true), values = e32(b + 8, true), nt = e32(b + 0x18, true),
             pt = e32(b + 0x1C, true), names = e32(b + 0x24, true), vals = e32(b + 0x28, true);
    if (!nodes || nodes > 0x100000 || (uint64_t)nt + nodes * 16ull > n || (uint64_t)pt + values * 8ull > n) return false;
    int16_t *par = malloc(nodes * sizeof(int16_t));
    if (!par) return false;
    for (uint32_t i = 0; i < nodes; i++) par[i] = (int16_t)e16(b + nt + i * 16 + 12, true);
    /* depth by walking up (bounded) */
    char tmp[256], tv[512];
    pf(s, "XMB, %u nodes\n", (unsigned)nodes);
    /* print in file order with the depth from the parent chain: the files store children after parents */
    for (uint32_t i = 0; i < nodes && !s->full; i++) {
        const uint8_t *e = b + nt + i * 16;
        int d = 0;
        for (int p = par[i]; p >= 0 && p < (int)nodes && d < 64; p = par[p]) d++;
        indent(s, d);
        pf(s, "<%s", cstr(b, n, (uint64_t)names + e32(e, true), tmp, sizeof(tmp)));
        int np = (int16_t)e16(e + 4, true), fp = (int16_t)e16(e + 8, true);
        for (int k = 0; k < np && fp >= 0 && (uint32_t)(fp + k) < values; k++) {
            const uint8_t *pp = b + pt + (fp + k) * 8;
            pf(s, " %s=\"", cstr(b, n, (uint64_t)names + e32(pp, true), tmp, sizeof(tmp)));
            pf(s, "%s\"", cstr(b, n, (uint64_t)vals + e32(pp + 4, true), tv, sizeof(tv)));
        }
        sb_put(s, ">\n", 2);
    }
    free(par);
    return true;
}

/* ================================================================ AAMP (BGLPBD) */

static const char *const aamp_names[] = {
    "param_root", "root_grid", "aabb_min_pos", "aabb_max_pos", "voxel_step_pos", "param_obj",
    "dir_light_indirect", "point_light_indirect", "spot_light_indirect", "emission_scale", "grid",
    "sh_index_buffer", "sh_data_buffer", "index_buffer", "data_buffer", "max_sh_data_num",
    "max_index_num", "use_index", "data_num", "index_num", NULL,
};

static const char *aamp_name(uint32_t h, char *tmp, size_t tn)
{
    for (int i = 0; aamp_names[i]; i++)
        if (crc32(0, (const Bytef *)aamp_names[i], (uInt)strlen(aamp_names[i])) == h) return aamp_names[i];
    snprintf(tmp, tn, "0x%08X", (unsigned)h);
    return tmp;
}

static void aamp_value(sb_t *s, const uint8_t *b, uint32_t n, uint32_t at, uint32_t len, uint32_t type, bool le, bool v2)
{
    if (at >= n) {
        sb_put(s, "<bad>\n", 6);
        return;
    }
    if (at + len > n) len = n - at;
    switch (type) {
    case 0: pf(s, "%s\n", (v2 ? e32(b + at, le) : b[at]) ? "true" : "false"); break;
    case 1: pf(s, "%g\n", (double)ef(b + at, le)); break;
    case 2: pf(s, "%d\n", (int)e32(b + at, le)); break;
    case 17: pf(s, "%uu\n", (unsigned)e32(b + at, le)); break;
    case 3: case 4: case 5: case 6: case 16: {
        int k = type == 3 ? 2 : type == 4 ? 3 : 4;
        sb_put(s, "[", 1);
        for (int i = 0; i < k && at + i * 4 + 4 <= n; i++) pf(s, "%s%g", i ? ", " : "", (double)ef(b + at + i * 4, le));
        sb_put(s, "]\n", 2);
        break;
    }
    case 7: case 8: case 15: case 20: {
        char t[257];
        pf(s, "\"%s\"\n", cstr(b, n, at, t, sizeof(t)));
        break;
    }
    case 13: case 14: case 18: {
        uint32_t cnt = v2 ? (at >= 4 ? e32(b + at - 4, le) : 0) : len / 4;
        pf(s, "buffer of %u:", (unsigned)cnt);
        for (uint32_t i = 0; i < cnt && i < 16 && at + i * 4 + 4 <= n; i++) {
            if (type == 14) pf(s, " %g", (double)ef(b + at + i * 4, le));
            else pf(s, type == 13 ? " %d" : " %u", (int)e32(b + at + i * 4, le));
        }
        sb_put(s, cnt > 16 ? " ...\n" : "\n", cnt > 16 ? 5 : 1);
        break;
    }
    case 19: pf(s, "binary, %u bytes\n", (unsigned)(v2 ? (at >= 4 ? e32(b + at - 4, le) : 0) : len)); break;
    default: pf(s, "<type %u>\n", (unsigned)type); break;
    }
}

static void aamp_v2_list(sb_t *s, const uint8_t *b, uint32_t n, uint32_t off, bool le, int d)
{
    char tmp[16];
    if (d > 32 || off + 12 > n || s->full) return;
    indent(s, d);
    pf(s, "list %s:\n", aamp_name(e32(b + off, le), tmp, sizeof(tmp)));
    uint32_t lo = e16(b + off + 4, le), lc = e16(b + off + 6, le), oo = e16(b + off + 8, le), oc = e16(b + off + 10, le);
    for (uint32_t i = 0; i < oc && oo; i++) {
        uint32_t ob = off + oo * 4 + i * 8;
        if (ob + 8 > n) break;
        indent(s, d + 1);
        pf(s, "object %s:\n", aamp_name(e32(b + ob, le), tmp, sizeof(tmp)));
        uint32_t po = e16(b + ob + 4, le), pc = e16(b + ob + 6, le);
        for (uint32_t k = 0; k < pc && po; k++) {
            uint32_t pe = ob + po * 4 + k * 8;
            if (pe + 8 > n) break;
            uint32_t f4 = e32(b + pe + 4, le);
            indent(s, d + 2);
            pf(s, "%s: ", aamp_name(e32(b + pe, le), tmp, sizeof(tmp)));
            aamp_value(s, b, n, pe + (f4 & 0xFFFFFF) * 4, 1024, f4 >> 24, le, true);
        }
    }
    for (uint32_t i = 0; i < lc && lo; i++) aamp_v2_list(s, b, n, off + lo * 4 + i * 12, le, d + 1);
}

static uint32_t aamp_v1_list(sb_t *s, const uint8_t *b, uint32_t n, uint32_t off, bool le, int d)
{
    char tmp[16];
    if (d > 32 || off + 16 > n || s->full) return 0;
    uint32_t size = e32(b + off, le), lc = e32(b + off + 8, le), oc = e32(b + off + 12, le);
    if (size < 16 || off + size > n) return 0;
    indent(s, d);
    pf(s, "list %s:\n", aamp_name(e32(b + off + 4, le), tmp, sizeof(tmp)));
    uint32_t cur = off + 16, end = off + size;
    for (uint32_t i = 0; i < lc && cur < end; i++) {
        uint32_t c = aamp_v1_list(s, b, n, cur, le, d + 1);
        if (!c) return size;
        cur += c;
    }
    for (uint32_t i = 0; i < oc && cur + 16 <= end; i++) {
        uint32_t os = e32(b + cur, le), pc = e32(b + cur + 4, le);
        if (os < 16 || cur + os > end) break;
        indent(s, d + 1);
        pf(s, "object %s:\n", aamp_name(e32(b + cur + 8, le), tmp, sizeof(tmp)));
        uint32_t pp = cur + 16;
        for (uint32_t k = 0; k < pc && pp + 12 <= cur + os; k++) {
            uint32_t es = e32(b + pp, le);
            if (es < 12 || pp + es > cur + os) break;
            indent(s, d + 2);
            pf(s, "%s: ", aamp_name(e32(b + pp + 8, le), tmp, sizeof(tmp)));
            aamp_value(s, b, n, pp + 12, es - 12, e32(b + pp + 4, le), le, false);
            pp += es;
        }
        cur += os;
    }
    return size;
}

static bool dump_aamp(sb_t *s, const uint8_t *b, uint32_t n)
{
    if (n < 0x18) return false;
    uint32_t vl = e32(b + 4, true);
    bool le = vl == 1 || vl == 2;
    uint32_t ver = e32(b + 4, le);
    char t[64];
    if (ver == 1) {
        uint32_t nl = e32(b + 20, le);
        if (nl > n - 24) return false;
        pf(s, "AAMP v1 (%s endian), type \"%s\"\n", le ? "little" : "big", cstr(b, n, 24, t, sizeof(t)));
        aamp_v1_list(s, b, n, 24 + nl, le, 0);
        return true;
    }
    if (ver == 2 && n >= 0x30) {
        uint32_t po = e32(b + 20, le);
        pf(s, "AAMP v2 (%s endian), type \"%s\"\n", le ? "little" : "big", cstr(b, n, 0x30, t, sizeof(t)));
        aamp_v2_list(s, b, n, 0x30 + po, le, 0);
        return true;
    }
    return false;
}

/* ================================================================ BFLYT / BFLAN */

static const char *nm(const uint8_t *b, uint32_t n, uint32_t at, uint32_t max, char *tmp)
{
    tmp[0] = 0;
    if (at >= n) return tmp;
    uint32_t l = (uint32_t)strnlen((const char *)b + at, (n - at) < max ? (n - at) : max);
    memcpy(tmp, b + at, l);
    tmp[l] = 0;
    for (uint32_t i = 0; i < l; i++)
        if ((uint8_t)tmp[i] < 0x20) tmp[i] = '?';
    return tmp;
}

/* "txl1" / "fnl1": u16 count, pad, u32 offsets from +0xC */
static void name_list(sb_t *s, const uint8_t *b, uint32_t n, uint32_t sec, uint32_t end, bool le, const char *what)
{
    uint32_t cnt = e16(b + sec + 8, le);
    pf(s, "%s (%u):\n", what, (unsigned)cnt);
    char t[128];
    for (uint32_t i = 0; i < cnt && sec + 0xC + i * 4 + 4 <= end; i++)
        pf(s, "  %s\n", cstr(b, n, (uint64_t)sec + 0xC + e32(b + sec + 0xC + i * 4, le), t, sizeof(t)));
}

static bool dump_flyt(sb_t *s, const uint8_t *b, uint32_t n)
{
    bool le = b[4] == 0xFF;
    uint32_t ver = e32(b + 8, le), hs = e16(b + 6, le), ns = e16(b + 0x10, le);
    pf(s, "BFLYT version %u.%u.%u, %u sections\n", (unsigned)(ver >> 24), (unsigned)((ver >> 16) & 255),
       (unsigned)((ver >> 8) & 255), (unsigned)ns);
    int d = 0;
    char t[64];
    uint32_t pos = hs;
    for (uint32_t k = 0; k < ns && pos + 8 <= n && !s->full; k++) {
        const uint8_t *h = b + pos;
        uint32_t sz = e32(h + 4, le), end = pos + sz;
        if (sz < 8 || end > n) break;
        if (!memcmp(h, "lyt1", 4) && sz >= 0x14)
            pf(s, "layout %gx%g%s\n", (double)ef(h + 0xC, le), (double)ef(h + 0x10, le), h[8] ? ", drawn from the center" : "");
        else if (!memcmp(h, "txl1", 4)) name_list(s, b, n, pos, end, le, "textures");
        else if (!memcmp(h, "fnl1", 4)) name_list(s, b, n, pos, end, le, "fonts");
        else if (!memcmp(h, "mat1", 4)) {
            uint32_t cnt = e16(h + 8, le);
            pf(s, "materials (%u):\n", (unsigned)cnt);
            for (uint32_t i = 0; i < cnt && pos + 0xC + i * 4 + 4 <= end; i++)
                pf(s, "  %s\n", nm(b, n, pos + e32(h + 0xC + i * 4, le), 0x1C, t));
        } else if (!memcmp(h, "pas1", 4) || !memcmp(h, "grs1", 4)) d++;
        else if (!memcmp(h, "pae1", 4) || !memcmp(h, "gre1", 4)) d = d > 0 ? d - 1 : 0;
        else if (!memcmp(h, "pan1", 4) || !memcmp(h, "pic1", 4) || !memcmp(h, "txt1", 4) || !memcmp(h, "wnd1", 4) ||
                 !memcmp(h, "bnd1", 4) || !memcmp(h, "prt1", 4) || !memcmp(h, "scr1", 4) || !memcmp(h, "ali1", 4)) {
            if (sz < 0x54) goto next;
            indent(s, d + 1);
            pf(s, "%.3s \"%s\" at (%g, %g) %gx%g", (const char *)h, nm(b, n, pos + 0xC, 0x18, t),
               (double)ef(h + 0x2C, le), (double)ef(h + 0x30, le), (double)ef(h + 0x4C, le), (double)ef(h + 0x50, le));
            if (!(h[8] & 1)) sb_put(s, " (hidden)", 9);
            if (!memcmp(h, "txt1", 4) && sz >= 0x68) {
                uint32_t len = e16(h + 0x54, le), to = e32(h + 0x64, le);
                if (len && to && pos + (uint64_t)to + len <= end) {
                    sb_put(s, ": \"", 3);
                    put_u16(s, b + pos + to, len, le);
                    sb_put(s, "\"", 1);
                }
            }
            sb_put(s, "\n", 1);
        } else if (!memcmp(h, "grp1", 4) && sz >= 0x24) {
            indent(s, d);
            pf(s, "group \"%s\"\n", nm(b, n, pos + 8, 0x18, t));
        } else if (!memcmp(h, "usd1", 4)) {
            indent(s, d + 1);
            pf(s, "user data (%u entries)\n", (unsigned)e16(h + 8, le));
        } else {
            indent(s, d + 1);
            pf(s, "[%.4s] %u bytes\n", (const char *)h, (unsigned)sz);
        }
    next:
        pos = end;
    }
    return true;
}

static const char *curve_name(const uint8_t *m)
{
    static const struct { const char *tag, *what; } t[] = {
        { "FLPA", "pane SRT" },     { "FLVI", "visibility" }, { "FLTS", "texture SRT" }, { "FLVC", "vertex colour" },
        { "FLMC", "material colour" }, { "FLTP", "texture pattern" }, { "FLIM", "indirect texture" },
        { "FLAC", "alpha compare" }, { "FLFS", "font shadow" }, { "FLCT", "per-character transform" },
        { "FLWN", "window" }, { "FLEU", "extended user data" }, { "FLMI", "material ID" }, { NULL, NULL },
    };
    for (int i = 0; t[i].tag; i++)
        if (!memcmp(m, t[i].tag, 4)) return t[i].what;
    return "?";
}

static bool dump_flan(sb_t *s, const uint8_t *b, uint32_t n)
{
    bool le = b[4] == 0xFF;
    uint32_t hs = e16(b + 6, le), ns = e16(b + 0x10, le);
    pf(s, "BFLAN, %u sections\n", (unsigned)ns);
    char t[128];
    uint32_t pos = hs;
    for (uint32_t k = 0; k < ns && pos + 8 <= n && !s->full; k++) {
        const uint8_t *h = b + pos;
        uint32_t sz = e32(h + 4, le), end = pos + sz;
        if (sz < 8 || end > n) break;
        if (!memcmp(h, "pat1", 4) && sz >= 0x1C) {
            uint32_t gc = e16(h + 0xA, le), no = e32(h + 0xC, le), go = e32(h + 0x10, le);
            pf(s, "tag \"%s\", frames %d to %d\n", cstr(b, n, (uint64_t)pos + no, t, sizeof(t)),
               (int)(int16_t)e16(h + 0x14, le), (int)(int16_t)e16(h + 0x16, le));
            for (uint32_t i = 0; i < gc && pos + go + (i + 1) * 0x24ull <= end; i++)
                pf(s, "  group %s\n", nm(b, n, pos + go + i * 0x24, 0x24, t));
        } else if (!memcmp(h, "pai1", 4) && sz >= 0x14) {
            uint32_t frames = e16(h + 8, le), tc = e16(h + 0xC, le), ec = e16(h + 0xE, le), eo = e32(h + 0x10, le);
            pf(s, "%u frames%s, %u textures, %u animated panes / materials\n", (unsigned)frames, h[0xA] ? ", looped" : "",
               (unsigned)tc, (unsigned)ec);
            for (uint32_t i = 0; i < tc && pos + 0x14 + i * 4 + 4 <= end; i++)
                pf(s, "  texture %s\n", cstr(b, n, (uint64_t)pos + 0x14 + e32(h + 0x14 + i * 4, le), t, sizeof(t)));
            for (uint32_t i = 0; i < ec && pos + eo + i * 4 + 4 <= end && !s->full; i++) {
                uint32_t ent = pos + e32(h + eo + i * 4, le);
                if (ent + 0x24 > end) continue;
                uint32_t tags = b[ent + 0x1C];
                pf(s, "  %s %s:", b[ent + 0x1D] ? "material" : "pane", nm(b, n, ent, 0x1C, t));
                for (uint32_t j = 0; j < tags && ent + 0x20 + j * 4 + 4 <= end; j++) {
                    uint32_t tg = ent + e32(b + ent + 0x20 + j * 4, le);
                    if (tg + 4 <= end) pf(s, "%s %s", j ? "," : "", curve_name(b + tg));
                }
                sb_put(s, "\n", 1);
            }
        } else {
            pf(s, "[%.4s] %u bytes\n", (const char *)h, (unsigned)sz);
        }
        pos = end;
    }
    return true;
}

/* ================================================================ BFSEQ */

static bool dump_fseq(sb_t *s, const uint8_t *b, uint32_t n)
{
    bool le = b[4] == 0xFF;
    uint32_t nb = e16(b + 0x10, le), data = 0, dsz = 0, labl = 0;
    for (uint32_t i = 0; i < nb && 0x14 + i * 12 + 12 <= n; i++) {
        uint32_t id = e16(b + 0x14 + i * 12, le), off = e32(b + 0x18 + i * 12, le), sz = e32(b + 0x1C + i * 12, le);
        if (id == 0x5000) data = off, dsz = sz;
        else if (id == 0x5001) labl = off;
    }
    uint32_t ver = e32(b + 8, le);
    pf(s, "BFSEQ version %u.%u.%u: a NintendoWare sequence (MIDI-like events). It needs its sound bank to play,\n"
          "so it is shown as its labels here.\n\n", (unsigned)((ver >> 16) & 255), (unsigned)((ver >> 8) & 255),
       (unsigned)(ver & 255));
    if (data) pf(s, "DATA block: %u bytes at %X\n", (unsigned)dsz, (unsigned)data);
    if (labl && labl + 12 <= n && !memcmp(b + labl, "LABL", 4)) {
        uint32_t cnt = e32(b + labl + 8, le);
        pf(s, "%u labels:\n", (unsigned)cnt);
        char t[128];
        for (uint32_t i = 0; i < cnt && labl + 12 + i * 8 + 8 <= n && !s->full; i++) {
            uint32_t lo = labl + 8 + e32(b + labl + 12 + i * 8 + 4, le);
            if (lo + 16 > n) continue;
            uint32_t at = e32(b + lo + 4, le), len = e32(b + lo + 8, le);
            if (len > sizeof(t) - 1) len = sizeof(t) - 1;
            pf(s, "  %-40s at %06X\n", nm(b, n, lo + 12, len, t), (unsigned)at);
        }
    }
    return true;
}

/* ================================================================ entry points */

int wiiu_text_kind(const uint8_t *h, uint32_t n, const char *name)
{
    if (n < 8) return 0;
    if (((h[0] == 'B' && h[1] == 'Y') || (h[0] == 'Y' && h[1] == 'B'))) {
        uint16_t v = h[0] == 'Y' ? (uint16_t)(h[2] | h[3] << 8) : (uint16_t)(h[2] << 8 | h[3]);
        if (v >= 1 && v <= 7) return WT_BYML;
    }
    if (!memcmp(h, "XMB ", 4)) return WT_XMB;
    if (!memcmp(h, "AAMP", 4)) return WT_AAMP;
    if (!memcmp(h, "FLYT", 4)) return WT_FLYT;
    if (!memcmp(h, "FLAN", 4)) return WT_FLAN;
    if (!memcmp(h, "FSEQ", 4)) return WT_FSEQ;
    (void)name;
    return 0;
}

char *wiiu_text_dump(const uint8_t *b, uint32_t n, const char *name, const char **kind, uint32_t *len)
{
    sb_t s = { 0 };
    bool ok = false;
    *kind = "Wii U";
    switch (wiiu_text_kind(b, n, name)) {
    case WT_BYML: *kind = "BYML"; ok = dump_byml(&s, b, n); break;
    case WT_XMB: *kind = "XMB"; ok = dump_xmb(&s, b, n); break;
    case WT_AAMP: {
        bool lp = false;
        for (uint32_t i = 0; i + 5 <= n && i < 0x40 && !lp; i++) lp = !memcmp(b + i, "glpbd", 5);
        *kind = lp ? "BGLPBD" : "AAMP";
        ok = dump_aamp(&s, b, n);
        break;
    }
    case WT_FLYT: *kind = "BFLYT"; ok = n >= 0x14 && dump_flyt(&s, b, n); break;
    case WT_FLAN: *kind = "BFLAN"; ok = n >= 0x14 && dump_flan(&s, b, n); break;
    case WT_FSEQ: *kind = "BFSEQ"; ok = n >= 0x20 && dump_fseq(&s, b, n); break;
    }
    if (!ok || !s.p) {
        free(s.p);
        return NULL;
    }
    if (s.full && s.n > 64) {
        s.n -= 64;
        s.full = false;
        sb_put(&s, "\n(cut off: too long to show)\n", 29);
    }
    *len = s.n;
    return s.p;
}
