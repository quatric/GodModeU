/*
 * Wii U textures for the image viewer. The GX2 surface detiler (AMD addrlib's micro / macro tiling as
 * aboood40091's addrlib.py reimplements it) and the format expansion follow quatric's nintoolbox
 * (lib-gtx.c, lib-flim.c, lib-retro-txtr.c, wszst's FTEX extractor):
 *
 *   GTX (Gfx2)                 every texture block
 *   BFLIM                      the FLIM footer's image (GX2 tile mode / swizzle in its flags)
 *   BFFNT                      the TGLP glyph sheets
 *   BFRES (v3 / v4)            its FTEX textures
 *   Tropical Freeze TXTR       RFRM / TXTR: Retro LZSS or zlib buffers, then a GX2 surface
 *   NUT (NTWU / NTP3)          Smash Bros. for Wii U texture sets
 *   DDS                        DXT1 / 3 / 5, ATI1 / ATI2 (BC4 / BC5), DX10 BC1-5, 32 / 24 bit RGB(A)
 *
 * Textures are described when the file is opened and only expanded to RGBA when one is shown.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zlib.h>

#include "wiiufmt.h"

static uint32_t b32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t b16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t l32(const uint8_t *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }
static uint16_t l16(const uint8_t *p) { return (uint16_t)(p[1] << 8 | p[0]); }

#define MAX_DIM 8192

/* ================================================================ GX2 addressing */

static unsigned thickness(unsigned tm)
{
    if (tm == 3 || tm == 7 || tm == 11 || tm == 13 || tm == 15) return 4;
    if (tm == 16 || tm == 17) return 8;
    return 1;
}
static bool thick_macro(unsigned tm) { return tm == 7 || tm == 11 || tm == 13 || tm == 15; }
static bool bank_swapped(unsigned tm) { return tm == 8 || tm == 9 || tm == 10 || tm == 11 || tm == 14 || tm == 15; }
static unsigned macro_aspect(unsigned tm) { return (tm == 5 || tm == 9) ? 2 : (tm == 6 || tm == 10) ? 4 : 1; }
static const unsigned swap_order[8] = { 0, 1, 3, 2, 6, 7, 5, 4 };

static unsigned bank_swap_width(unsigned tm, unsigned bpp, unsigned pitch)
{
    if (!bank_swapped(tm)) return 0;
    unsigned bps = 8 * bpp, spt = 1;
    unsigned samples_per_tile = 2048 / bps;
    spt = samples_per_tile ? 1 / samples_per_tile : 0;
    if (!spt) spt = 1;
    unsigned es = thick_macro(tm) ? 4 : 1;
    unsigned bpts = es * bps / spt;
    unsigned factor = macro_aspect(tm);
    unsigned swap_tiles = 128 / bpp > 1 ? 128 / bpp : 1;
    unsigned swap_width = swap_tiles * 32;
    unsigned hb = es * factor * bpp * 2 / spt;
    unsigned smax = hb ? 0x4000 / hb : 0, smin = bpts ? 256 / bpts : 0;
    unsigned inner = swap_width > smin ? swap_width : smin;
    unsigned w = inner < smax ? inner : smax;
    while (pitch && w >= 2 * pitch) w >>= 1;
    return w;
}

static unsigned pixel_index(unsigned x, unsigned y, unsigned z, unsigned bpp, unsigned tm, bool depth)
{
    unsigned b0, b1, b2, b3, b4, b5;
    if (depth) {
        b0 = x & 1; b1 = y & 1; b2 = (x >> 1) & 1; b3 = (y >> 1) & 1; b4 = (x >> 2) & 1; b5 = (y >> 2) & 1;
    } else switch (bpp) {
    case 8:
        b0 = x & 1; b1 = (x >> 1) & 1; b2 = (x >> 2) & 1; b3 = (y >> 1) & 1; b4 = y & 1; b5 = (y >> 2) & 1;
        break;
    case 16:
        b0 = x & 1; b1 = (x >> 1) & 1; b2 = (x >> 2) & 1; b3 = y & 1; b4 = (y >> 1) & 1; b5 = (y >> 2) & 1;
        break;
    case 64:
        b0 = x & 1; b1 = y & 1; b2 = (x >> 1) & 1; b3 = (x >> 2) & 1; b4 = (y >> 1) & 1; b5 = (y >> 2) & 1;
        break;
    case 128:
        b0 = y & 1; b1 = x & 1; b2 = (x >> 1) & 1; b3 = (x >> 2) & 1; b4 = (y >> 1) & 1; b5 = (y >> 2) & 1;
        break;
    default:
        b0 = x & 1; b1 = (x >> 1) & 1; b2 = y & 1; b3 = (x >> 2) & 1; b4 = (y >> 1) & 1; b5 = (y >> 2) & 1;
        break;
    }
    unsigned r = 32 * b5 | 16 * b4 | 8 * b3 | 4 * b2 | b0 | 2 * b1;
    unsigned t = thickness(tm);
    if (t > 1) r |= (z & 3) << 6;
    if (t == 8) r |= (z & 4) << 6;
    return r;
}

static uint64_t addr_macro(unsigned x, unsigned y, unsigned bpp, unsigned pitch, unsigned height, unsigned tm,
                           bool depth, unsigned pipe_sw, unsigned bank_sw)
{
    unsigned th = thickness(tm);
    unsigned pidx = pixel_index(x, y, 0, bpp, tm, depth);
    uint64_t elem_bits = (uint64_t)bpp * pidx;
    unsigned elem_off = (unsigned)((elem_bits + 7) / 8);
    unsigned pipe = ((y >> 3) ^ (x >> 3)) & 1;
    unsigned bank = (((y >> 5) ^ (x >> 3)) & 1) | 2 * (((y >> 4) ^ (x >> 4)) & 1);
    unsigned sw = pipe_sw + 2 * bank_sw;
    unsigned bp = ((pipe + 2 * bank) ^ sw) % 8;
    pipe = bp % 2;
    bank = bp / 2;
    unsigned mtp = 32, mth = 16;
    if (tm == 5 || tm == 9) mtp >>= 1, mth <<= 1;
    else if (tm == 6 || tm == 10) mtp >>= 2, mth <<= 2;
    unsigned per_row = pitch / mtp;
    unsigned mt_bytes = (th * bpp * mth * mtp + 7) / 8;
    unsigned mx = x / mtp, my = y / mth;
    uint64_t moff = (uint64_t)(mx + per_row * my) * mt_bytes;
    if (bank_swapped(tm)) {
        unsigned bsw = bank_swap_width(tm, bpp, pitch);
        if (bsw) bank ^= swap_order[(mtp * mx / bsw) & 3];
    }
    (void)height;
    uint64_t total = elem_off + (moff >> 3);
    return (uint64_t)bank << 9 | pipe << 8 | (total & 255) | ((total & ~255ull) << 3);
}

/* Byte offset of element (x, y) in a single-sample 2D surface, ~0 if the mode isn't handled. */
static uint64_t surf_offset(unsigned x, unsigned y, unsigned bpp, unsigned height, unsigned tm, unsigned pitch,
                            unsigned swizzle, bool depth)
{
    if (tm <= 1) return ((uint64_t)y * pitch + x) * bpp / 8;
    if (tm == 2 || tm == 3) {
        unsigned th = thickness(tm);
        uint64_t mb = ((uint64_t)64 * th * bpp + 7) / 8;
        uint64_t moff = mb * ((x >> 3) + (uint64_t)(y >> 3) * (pitch >> 3));
        return moff + (uint64_t)bpp * pixel_index(x, y, 0, bpp, tm, depth) / 8;
    }
    if (tm >= 4 && tm <= 15)
        return addr_macro(x, y, bpp, pitch, height, tm, depth, (swizzle >> 8) & 1, (swizzle >> 9) & 3);
    return ~(uint64_t)0;
}

static bool fmt_bpp(unsigned f, unsigned *bpp, int *bc)
{
    *bc = 0;
    switch (f & 0x3F) {
    case 0x01: case 0x02: *bpp = 8; return true;
    case 0x05: case 0x06: case 0x07: case 0x08: case 0x0A: case 0x0B: case 0x0C: *bpp = 16; return true;
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x16: case 0x19: case 0x1A: case 0x1B:
        *bpp = 32; return true;
    case 0x1C: case 0x1D: case 0x1E: case 0x1F: case 0x20: *bpp = 64; return true;
    case 0x22: case 0x23: *bpp = 128; return true;
    case 0x31: *bpp = 64; *bc = 1; return true;
    case 0x32: *bpp = 128; *bc = 2; return true;
    case 0x33: *bpp = 128; *bc = 3; return true;
    case 0x34: *bpp = 64; *bc = 4; return true;
    case 0x35: *bpp = 128; *bc = 5; return true;
    }
    return false;
}

static unsigned min_pitch(unsigned ew, unsigned tm)
{
    unsigned a = 1;
    if (tm == 2 || tm == 3) a = 8;
    else if (tm >= 4 && tm <= 15) a = (tm == 5 || tm == 9) ? 16 : (tm == 6 || tm == 10) ? 8 : 32;
    return (ew + a - 1) / a * a;
}

/* ================================================================ BCn (DXT) blocks, little endian */

static void c565(uint16_t v, uint8_t *o)
{
    o[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
    o[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
    o[2] = (uint8_t)((v & 31) * 255 / 31);
    o[3] = 255;
}

static void bc1(const uint8_t *b, uint8_t *px, bool alpha_ok)
{
    uint16_t c0 = l16(b), c1 = l16(b + 2);
    uint8_t c[4][4];
    c565(c0, c[0]);
    c565(c1, c[1]);
    if (c0 > c1 || !alpha_ok) {
        for (int k = 0; k < 3; k++) {
            c[2][k] = (uint8_t)((2 * c[0][k] + c[1][k]) / 3);
            c[3][k] = (uint8_t)((c[0][k] + 2 * c[1][k]) / 3);
        }
        c[2][3] = c[3][3] = 255;
    } else {
        for (int k = 0; k < 3; k++) c[2][k] = (uint8_t)((c[0][k] + c[1][k]) / 2), c[3][k] = 0;
        c[2][3] = 255;
        c[3][3] = 0;
    }
    uint32_t idx = l32(b + 4);
    for (int i = 0; i < 16; i++) memcpy(px + i * 4, c[(idx >> (2 * i)) & 3], 4);
}

/* An interpolated 8-bit channel block (BC3 alpha, BC4, BC5). */
static void bc_chan(const uint8_t *b, uint8_t *out, int stride, bool sgn)
{
    int a0 = sgn ? (int8_t)b[0] + 128 : b[0], a1 = sgn ? (int8_t)b[1] + 128 : b[1];
    int v[8] = { a0, a1 };
    if (a0 > a1)
        for (int i = 1; i < 7; i++) v[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    else {
        for (int i = 1; i < 5; i++) v[i + 1] = ((5 - i) * a0 + i * a1) / 5;
        v[6] = 0;
        v[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++) out[i * stride] = (uint8_t)v[(bits >> (3 * i)) & 7];
}

static void bc_block(int kind, const uint8_t *b, uint8_t *px, bool sgn)
{
    switch (kind) {
    case 1: bc1(b, px, true); break;
    case 2:
        bc1(b + 8, px, false);
        for (int i = 0; i < 16; i++) {
            int n = (b[i / 2] >> (4 * (i & 1))) & 15;
            px[i * 4 + 3] = (uint8_t)(n * 17);
        }
        break;
    case 3:
        bc1(b + 8, px, false);
        bc_chan(b, px + 3, 4, false);
        break;
    case 4:
        bc_chan(b, px, 4, sgn);
        for (int i = 0; i < 16; i++) px[i * 4 + 1] = px[i * 4 + 2] = px[i * 4], px[i * 4 + 3] = 255;
        break;
    case 5:
        bc_chan(b, px, 4, sgn);
        bc_chan(b + 8, px + 1, 4, sgn);
        for (int i = 0; i < 16; i++) {          /* a normal map: rebuild Z for a readable picture */
            float x = px[i * 4] / 127.5f - 1, y = px[i * 4 + 1] / 127.5f - 1, z = 1 - x * x - y * y;
            px[i * 4 + 2] = (uint8_t)((z > 0 ? sqrtf(z) : 0) * 127.5f + 127.5f);
            px[i * 4 + 3] = 255;
        }
        break;
    }
}

/* ================================================================ format expansion */

static float half(uint16_t h)
{
    unsigned s = h >> 15, e = (h >> 10) & 31, m = h & 1023;
    float v = !e ? m / 16777216.0f : e == 31 ? 1.0f : ldexpf(1.0f + m / 1024.0f, (int)e - 15);
    return s ? -v : v;
}
static uint8_t fbyte(float v) { return v <= 0 ? 0 : v >= 1 ? 255 : (uint8_t)(v * 255 + .5f); }
static float uflt(unsigned raw, unsigned mb)
{
    unsigned m = raw & ((1u << mb) - 1), e = raw >> mb;
    if (e == 31) return 1.0f;
    return e ? ldexpf(1.0f + m / (float)(1u << mb), (int)e - 15) : ldexpf(m / (float)(1u << mb), -14);
}
static float f32(uint32_t v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static void expand(unsigned f, const uint8_t *s, uint8_t *d)
{
    unsigned k = f & 0x3F;
    bool flt = ((f >> 8) & 15) == 8;
    switch (k) {
    case 0x01: d[0] = d[1] = d[2] = s[0]; d[3] = 255; break;
    case 0x02: d[0] = (uint8_t)((s[0] >> 4) * 17); d[1] = (uint8_t)((s[0] & 15) * 17); d[2] = 0; d[3] = 255; break;
    case 0x05: case 0x06: {
        uint16_t v = b16(s);
        d[0] = d[1] = d[2] = (k == 6 || flt) ? fbyte(half(v)) : (uint8_t)(v >> 8);
        d[3] = 255;
        break;
    }
    case 0x07: d[0] = s[0]; d[1] = s[1]; d[2] = 0; d[3] = 255; break;
    case 0x08: c565(b16(s), d); break;
    case 0x0A: {
        uint16_t v = b16(s);
        d[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
        d[1] = (uint8_t)(((v >> 6) & 31) * 255 / 31);
        d[2] = (uint8_t)(((v >> 1) & 31) * 255 / 31);
        d[3] = (v & 1) ? 255 : 0;
        break;
    }
    case 0x0B: {
        uint16_t v = b16(s);
        d[0] = (uint8_t)((v >> 12) * 17); d[1] = (uint8_t)(((v >> 8) & 15) * 17);
        d[2] = (uint8_t)(((v >> 4) & 15) * 17); d[3] = (uint8_t)((v & 15) * 17);
        break;
    }
    case 0x0C: {
        uint16_t v = b16(s);
        d[0] = (uint8_t)((v & 31) * 255 / 31); d[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31);
        d[2] = (uint8_t)(((v >> 10) & 31) * 255 / 31); d[3] = (v & 0x8000) ? 255 : 0;
        break;
    }
    case 0x0D: case 0x0E:
        d[0] = d[1] = d[2] = (k == 0x0E || flt) ? fbyte(f32(b32(s))) : s[0];
        d[3] = 255;
        break;
    case 0x0F: case 0x10:
        for (int c = 0; c < 2; c++) d[c] = (k == 0x10 || flt) ? fbyte(half(b16(s + 2 * c))) : s[2 * c];
        d[2] = 0; d[3] = 255;
        break;
    case 0x11: {
        uint32_t v = b32(s);
        d[0] = d[1] = d[2] = (uint8_t)(((v >> 8) & 0xFFFFFF) * 255.0 / 0xFFFFFF);
        d[3] = 255;
        break;
    }
    case 0x16: {
        uint32_t v = b32(s);
        d[0] = fbyte(uflt(v >> 21, 6)); d[1] = fbyte(uflt((v >> 10) & 0x7FF, 6)); d[2] = fbyte(uflt(v & 0x3FF, 5));
        d[3] = 255;
        break;
    }
    case 0x19: {
        uint32_t v = b32(s);
        d[0] = (uint8_t)(((v >> 20) & 1023) * 255 / 1023); d[1] = (uint8_t)(((v >> 10) & 1023) * 255 / 1023);
        d[2] = (uint8_t)((v & 1023) * 255 / 1023); d[3] = (uint8_t)(((v >> 30) & 3) * 85);
        break;
    }
    case 0x1A: memcpy(d, s, 4); break;
    case 0x1B: {
        uint32_t v = b32(s);
        d[0] = (uint8_t)((v & 1023) * 255 / 1023); d[1] = (uint8_t)(((v >> 10) & 1023) * 255 / 1023);
        d[2] = (uint8_t)(((v >> 20) & 1023) * 255 / 1023); d[3] = (uint8_t)((v >> 30) * 85);
        break;
    }
    case 0x1C: d[0] = d[1] = d[2] = fbyte(f32(b32(s))); d[3] = 255; break;
    case 0x1D: case 0x1E:
        for (int c = 0; c < 2; c++) d[c] = (k == 0x1E || flt) ? fbyte(f32(b32(s + 4 * c))) : s[4 * c];
        d[2] = 0; d[3] = 255;
        break;
    case 0x1F: case 0x20:
        for (int c = 0; c < 4; c++) d[c] = (k == 0x20 || flt) ? fbyte(half(b16(s + 2 * c))) : s[2 * c];
        break;
    case 0x22: case 0x23:
        for (int c = 0; c < 4; c++) d[c] = (k == 0x23 || flt) ? fbyte(f32(b32(s + 4 * c))) : s[4 * c];
        break;
    default: memset(d, 0, 4); break;
    }
}

/* ================================================================ decoding a described texture */

static bool gx2_decode(const wtex_t *t, uint8_t *out)
{
    unsigned bpp;
    int bc;
    if (!fmt_bpp(t->fmt, &bpp, &bc)) return false;
    unsigned w = t->w, h = t->h, bw = bc ? (w + 3) / 4 : w, bh = bc ? (h + 3) / 4 : h, bytes = bpp / 8;
    unsigned pitch = t->pitch ? t->pitch : min_pitch(bw, t->tile);
    if (pitch < bw) pitch = min_pitch(bw, t->tile);
    bool depth = (t->fmt & 0x3F) == 0x11, sgn = (t->fmt & 0x200) != 0;
    uint8_t el[16], px[64];
    memset(out, 0, (size_t)w * h * 4);
    for (unsigned y = 0; y < bh; y++)
        for (unsigned x = 0; x < bw; x++) {
            uint64_t a = surf_offset(x, y, bpp, bh, t->tile, pitch, t->swizzle, depth);
            if (a == ~(uint64_t)0) return false;
            if (a + bytes > t->size) continue;
            memcpy(el, t->data + a, bytes);
            if (!bc) {
                expand(t->fmt, el, out + ((size_t)y * w + x) * 4);
                continue;
            }
            bc_block(bc, el, px, sgn);
            for (unsigned py = 0; py < 4 && y * 4 + py < h; py++)
                for (unsigned q = 0; q < 4 && x * 4 + q < w; q++)
                    memcpy(out + ((size_t)(y * 4 + py) * w + x * 4 + q) * 4, px + (py * 4 + q) * 4, 4);
        }
    return true;
}

/* Linear (DDS, NTP3) data: BCn blocks or 8-bit channels at given byte positions. */
static bool linear_decode(const wtex_t *t, uint8_t *out)
{
    unsigned w = t->w, h = t->h;
    if (t->bc) {
        unsigned bw = (w + 3) / 4, bh = (h + 3) / 4, bs = (t->bc == 1 || t->bc == 4) ? 8 : 16;
        if ((uint64_t)bw * bh * bs > t->size) return false;
        uint8_t px[64];
        for (unsigned y = 0; y < bh; y++)
            for (unsigned x = 0; x < bw; x++) {
                bc_block(t->bc, t->data + ((size_t)y * bw + x) * bs, px, false);
                for (unsigned py = 0; py < 4 && y * 4 + py < h; py++)
                    for (unsigned q = 0; q < 4 && x * 4 + q < w; q++)
                        memcpy(out + ((size_t)(y * 4 + py) * w + x * 4 + q) * 4, px + (py * 4 + q) * 4, 4);
            }
        return true;
    }
    unsigned bpp = t->lin_bpp;
    if (!bpp || (uint64_t)w * h * bpp > t->size) return false;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        const uint8_t *s = t->data + i * bpp;
        uint8_t *d = out + i * 4;
        if (t->rgb565) {
            c565(t->rgb565 == 2 ? b16(s) : l16(s), d);
            continue;
        }
        for (int c = 0; c < 4; c++) d[c] = t->chan[c] < 0 ? 255 : s[t->chan[c]];
    }
    return true;
}

bool wtex_decode(const wtex_t *t, uint8_t *out)
{
    if (!t || !t->data || !t->w || !t->h) return false;
    bool ok = t->linear ? linear_decode(t, out) : gx2_decode(t, out);
    if (!ok) return false;
    if (t->has_sel) {
        for (size_t i = 0; i < (size_t)t->w * t->h; i++) {
            uint8_t in[4], *p = out + i * 4;
            memcpy(in, p, 4);
            for (int c = 0; c < 4; c++) {
                unsigned s = t->sel[c];
                p[c] = s < 4 ? in[s] : s == 4 ? 0 : 255;
            }
        }
    }
    return true;
}

void wtex_free(wtex_t *t)
{
    if (t) free(t->own);
    free(t);
}

const char *wtex_fmt_name(const wtex_t *t)
{
    if (t->linear) return t->bc ? (t->bc == 1 ? "BC1" : t->bc == 2 ? "BC2" : t->bc == 3 ? "BC3" : t->bc == 4 ? "BC4" : "BC5")
                                : t->rgb565 ? "RGB565" : "RGBA8";
    switch (t->fmt & 0x3F) {
    case 0x01: return "R8";
    case 0x02: return "R4G4";
    case 0x05: return "R16";
    case 0x06: return "R16F";
    case 0x07: return "R8G8";
    case 0x08: return "RGB565";
    case 0x0A: return "RGB5A1";
    case 0x0B: return "RGBA4";
    case 0x0C: return "A1BGR5";
    case 0x0D: return "R32";
    case 0x0E: return "R32F";
    case 0x0F: return "RG16";
    case 0x10: return "RG16F";
    case 0x11: return "D24S8";
    case 0x16: return "R11G11B10F";
    case 0x19: return "RGB10A2";
    case 0x1A: return (t->fmt & 0x400) ? "RGBA8 sRGB" : "RGBA8";
    case 0x1B: return "A2BGR10";
    case 0x1F: return "RGBA16";
    case 0x20: return "RGBA16F";
    case 0x22: return "RGBA32";
    case 0x23: return "RGBA32F";
    case 0x31: return "BC1";
    case 0x32: return "BC2";
    case 0x33: return "BC3";
    case 0x34: return "BC4";
    case 0x35: return "BC5";
    }
    return "GX2";
}

/* ================================================================ containers */

static wtex_t *new_gx2(unsigned fmt, unsigned w, unsigned h, unsigned tile, unsigned swizzle, unsigned pitch,
                       const uint8_t *data, uint32_t size)
{
    unsigned bpp;
    int bc;
    if (!w || !h || w > MAX_DIM || h > MAX_DIM || !data || !size || tile > 15 || !fmt_bpp(fmt, &bpp, &bc))
        return NULL;
    wtex_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->fmt = fmt;
    t->w = w;
    t->h = h;
    t->tile = tile;
    t->swizzle = swizzle;
    t->pitch = pitch;
    t->data = data;
    t->size = size;
    return t;
}

static void set_sel(wtex_t *t, const uint8_t *sel)
{
    for (int c = 0; c < 4; c++)
        if (sel[c] > 5) return;
    if (sel[0] == 0 && sel[1] == 1 && sel[2] == 2 && sel[3] == 3) return;
    memcpy(t->sel, sel, 4);
    t->has_sel = true;
}

static int emit(wtex_add_fn add, void *ctx, wtex_t *t, const char *label)
{
    if (!t) return 0;
    if (!add(ctx, t, label)) {
        wtex_free(t);
        return -1;
    }
    return 1;
}

static int parse_gtx(const uint8_t *b, uint32_t n, wtex_add_fn add, void *ctx)
{
    if (n < 32 || b32(b + 4) != 32) return 0;
    unsigned maj = b32(b + 8), min = b32(b + 12);
    unsigned surf = (maj == 6 && min == 0) ? 0x0A : 0x0B;
    int count = 0;
    const uint8_t *hdr = NULL;
    for (uint32_t pos = 32; pos + 32 <= n;) {
        if (memcmp(b + pos, "BLK{", 4)) break;
        uint32_t hs = b32(b + pos + 4), type = b32(b + pos + 16), ds = b32(b + pos + 20);
        if (hs < 32 || (uint64_t)pos + hs + ds > n) break;
        const uint8_t *d = b + pos + hs;
        if (type == surf && ds >= 136) hdr = d;
        else if (type == surf + 1 && hdr) {
            wtex_t *t = new_gx2(b32(hdr + 20), b32(hdr + 4), b32(hdr + 8), b32(hdr + 48), b32(hdr + 52),
                                b32(hdr + 60), d, ds);
            if (t) set_sel(t, hdr + 132);
            char lab[32];
            snprintf(lab, sizeof(lab), "texture %d", count + 1);
            int r = emit(add, ctx, t, lab);
            if (r < 0) break;
            count += r;
            hdr = NULL;
        }
        if (type == 1) break;
        pos += hs + ds;
    }
    return count;
}

static int parse_flim(const uint8_t *b, uint32_t n, const char *name, wtex_add_fn add, void *ctx)
{
    if (n < 0x28) return 0;
    const uint8_t *f = b + n - 0x28;
    if (memcmp(f, "FLIM", 4) || f[4] != 0xFE || f[5] != 0xFF || memcmp(f + 0x14, "imag", 4)) return 0;
    unsigned w = b16(f + 0x1C), h = b16(f + 0x1E), fm = f[0x22], fl = f[0x23];
    uint32_t ds = b32(b + n - 4);
    if (ds > n - 0x28) ds = n - 0x28;
    static const uint16_t map[24] = { 0x01, 0x01, 0x02, 0x07, 0x07, 0x08, 0x1A, 0x0A, 0x0B, 0x1A, 0, 0,
                                      0x31, 0x32, 0x33, 0x34, 0x34, 0x35, 0x02, 0x02, 0x41A, 0x431, 0x432, 0x433 };
    if (fm >= 24 || !map[fm]) return 0;
    wtex_t *t = new_gx2(map[fm], w, h, fl & 31, ((fl >> 5) & 7) << 8, 0, b, ds);
    /* NW4F's formats say what the channels mean (selectors: 0-3 R G B A, 4 zero, 5 one) */
    static const uint8_t alpha[4] = { 5, 5, 5, 0 }, lum[4] = { 0, 0, 0, 5 }, lum_alpha[4] = { 0, 0, 0, 1 };
    if (t) {
        if (fm == 1 || fm == 16 || fm == 19) set_sel(t, alpha);           /* A8, BC4 alpha, A4 */
        else if (fm == 0 || fm == 15 || fm == 18) set_sel(t, lum);        /* L8, BC4 luminance, L4 */
        else if (fm == 2 || fm == 3 || fm == 17) set_sel(t, lum_alpha);   /* LA4, LA8, BC5 (LA) */
    }
    return emit(add, ctx, t, name);
}

static int parse_ffnt(const uint8_t *b, uint32_t n, wtex_add_fn add, void *ctx)
{
    if (n < 0x30 || b[4] != 0xFE || b[5] != 0xFF) return 0;
    /* TGLP: the first one in the file */
    for (uint32_t p = b16(b + 6); p + 0x20 <= n; p += 4) {
        if (memcmp(b + p, "TGLP", 4)) continue;
        unsigned sheets = b[p + 0xA], fmt = b16(b + p + 0x12), w = b16(b + p + 0x18), h = b16(b + p + 0x1A);
        uint32_t ssz = b32(b + p + 0xC), doff = b32(b + p + 0x1C);
        static const uint16_t map[16] = { 0x1A, 0, 0x0A, 0x08, 0x0B, 0x07, 0x02, 0, 0x01, 0x31, 0x32, 0x33,
                                          0x34, 0x35, 0x41A, 0x431 };
        if (fmt >= 16 || !map[fmt] || !ssz || !sheets) return 0;
        int count = 0;
        for (unsigned i = 0; i < sheets; i++) {
            uint64_t at = doff + (uint64_t)i * ssz;
            if (at + ssz > n) break;
            wtex_t *t = new_gx2(map[fmt], w, h, 4, ((i * 4) % 8) << 8, 0, b + at, ssz);
            if (t && (fmt == 8 || fmt == 12)) {
                static const uint8_t a[4] = { 5, 5, 5, 0 };
                set_sel(t, a);
            }
            char lab[32];
            snprintf(lab, sizeof(lab), "sheet %u", i + 1);
            int r = emit(add, ctx, t, lab);
            if (r < 0) break;
            count += r;
        }
        return count;
    }
    return 0;
}

static uint32_t rel(const uint8_t *b, uint32_t at) { return at + (uint32_t)(int32_t)b32(b + at); }

static int parse_bfres(const uint8_t *b, uint32_t n, wtex_add_fn add, void *ctx)
{
    if (n < 0x6C || b16(b + 8) != 0xFEFF || (b[4] != 3 && b[4] != 4)) return 0;
    unsigned nt = b16(b + 0x52);
    if (!nt) return 0;
    uint32_t grp = rel(b, 0x24);
    if ((uint64_t)grp + 8 > n) return 0;
    uint32_t ents = b32(b + grp + 4);
    if (ents > 0x10000 || grp + 8 + (uint64_t)(ents + 1) * 16 > n) return 0;
    int count = 0;
    for (uint32_t i = 0; i < ents && i < nt; i++) {
        uint32_t te = grp + 8 + (i + 1) * 16;
        uint32_t ft = rel(b, te + 12);
        if ((uint64_t)ft + 0xC0 > n || memcmp(b + ft, "FTEX", 4)) continue;
        uint32_t no = rel(b, ft + 0xA8);
        char lab[64] = "texture";
        if (no < n) snprintf(lab, sizeof(lab), "%.*s", (int)strnlen((const char *)b + no, n - no), b + no);
        uint32_t isz = b32(b + ft + 0x24), dof = rel(b, ft + 0xB0);
        if (!isz || (uint64_t)dof + isz > n) continue;
        wtex_t *t = new_gx2(b32(b + ft + 0x18), b32(b + ft + 8), b32(b + ft + 0xC), b32(b + ft + 0x34),
                            b32(b + ft + 0x38), b32(b + ft + 0x40), b + dof, isz);
        if (t) set_sel(t, b + ft + 0x88);
        int r = emit(add, ctx, t, lab);
        if (r < 0) break;
        count += r;
    }
    return count;
}

/* ---------------------------------------------------------------- Tropical Freeze TXTR */

static bool tropical_gx2(unsigned id, unsigned *f)
{
    static const int16_t m[0x22] = {
        0x01, 0x01, 0x01, 0x01, 0x05, 0x05, 0x05, 0x05, 0x06, 0x0D, 0x0D, -1,   0x1A, 0x1A, 0x1F, 0x23,
        0x05, 0x05, 0x11, 0x0E, 0x31, 0x31, 0x32, 0x32, 0x33, 0x33, 0x34, 0x34, 0x35, 0x35, 0x16, 0x0E,
        0x10, 0x07,
    };
    if (id >= 0x22 || m[id] < 0) return false;
    *f = (unsigned)m[id];
    return true;
}

static uint8_t *retro_lzss(const uint8_t *src, uint32_t n, uint32_t out_n)
{
    if (n < 4 || src[0] > 3 || src[1] || src[2] || src[3]) return NULL;
    unsigned mode = src[0];
    uint8_t *o = malloc(out_n ? out_n : 1);
    if (!o) return NULL;
    if (!mode) {
        if (n - 4 < out_n) goto bad;
        memcpy(o, src + 4, out_n);
        return o;
    }
    unsigned g = mode == 1 ? 1 : mode == 2 ? 2 : 4;
    uint32_t sp = 4, dp = 0, hdr = 0, left = 0;
    while (dp < out_n) {
        if (!left) {
            if (sp >= n) goto bad;
            hdr = src[sp++];
            left = 8;
        }
        bool ref = hdr & 0x80;
        hdr = (hdr << 1) & 0xFF;
        left--;
        if (!ref) {
            if (sp + g > n || dp + g > out_n) goto bad;
            memcpy(o + dp, src + sp, g);
            sp += g;
            dp += g;
        } else {
            if (sp + 2 > n) goto bad;
            unsigned b0 = src[sp], b1 = src[sp + 1], cnt, len;
            sp += 2;
            if (mode == 1) cnt = (b0 >> 4) + 3, len = (b0 & 15) << 8 | b1;
            else if (mode == 2) cnt = (b0 >> 4) + 2, len = ((b0 & 15) << 8 | b1) << 1;
            else cnt = (b0 >> 4) + 1, len = ((b0 & 15) << 8 | b1) << 2;
            if (!len || len > dp || dp + (uint64_t)cnt * g > out_n) goto bad;
            uint32_t s = dp - len;
            for (unsigned c = 0; c < cnt * g; c++) o[dp++] = o[s++];
        }
    }
    return o;
bad:
    free(o);
    return NULL;
}

static uint64_t b64(const uint8_t *p) { return (uint64_t)b32(p) << 32 | b32(p + 4); }

static int parse_tropical(const uint8_t *b, uint32_t n, const char *name, wtex_add_fn add, void *ctx)
{
    if (n < 0x80 || memcmp(b, "RFRM", 4) || memcmp(b + 0x14, "TXTR", 4)) return 0;
    uint32_t pos = 0x20;
    if (memcmp(b + pos, "HEAD", 4)) return 0;
    uint64_t hsz = b64(b + pos + 4);
    if (hsz < 0x24 || pos + 0x18 + hsz > n) return 0;
    const uint8_t *hb = b + pos + 0x18;
    unsigned type = b32(hb), tf = b32(hb + 4), w = b32(hb + 8), h = b32(hb + 12), depth = b32(hb + 16),
             tile = b32(hb + 20), swr = b32(hb + 24);
    if (type != 1 || depth != 1 || tile > 15 || swr > 7) return 0;
    unsigned gf;
    if (!tropical_gx2(tf, &gf)) return 0;
    pos += 0x18 + (uint32_t)hsz;
    if (pos + 0x18 > n || memcmp(b + pos, "GPU", 3)) return 0;
    uint64_t gsz = b64(b + pos + 4);
    if (pos + 0x18 + gsz > n) return 0;
    pos += 0x18 + (uint32_t)gsz;
    if (pos + 0x40 > n || memcmp(b + pos, "META", 4)) return 0;
    const uint8_t *mb = b + pos;
    uint32_t align = b32(mb + 0x24), gstart = b32(mb + 0x28), nbuf = b32(mb + 0x30);
    if (!nbuf || nbuf > 256 || pos + 0x34 + (uint64_t)nbuf * 12 > n) return 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i < nbuf; i++) total += b32(mb + 0x34 + i * 12);
    if (!total || total > (64u << 20)) return 0;
    uint8_t *raw = malloc((size_t)total);
    if (!raw) return 0;
    uint32_t wp = 0;
    for (uint32_t i = 0; i < nbuf; i++) {
        const uint8_t *e = mb + 0x34 + i * 12;
        uint32_t ds = b32(e), cs = b32(e + 4), off = b32(e + 8);
        if ((uint64_t)gstart + off + cs > n) goto bad;
        const uint8_t *c = b + gstart + off;
        uint8_t *d = NULL;
        if (cs >= 4 && c[0] <= 3 && !c[1] && !c[2] && !c[3]) d = retro_lzss(c, cs, ds);
        else {
            uLongf dl = ds;
            d = malloc(ds ? ds : 1);
            if (d && (uncompress(d, &dl, c, cs) != Z_OK || dl != ds)) free(d), d = NULL;
        }
        if (!d) goto bad;
        memcpy(raw + wp, d, ds);
        wp += ds;
        free(d);
    }
    {
        static const unsigned sw[8] = { 0, 500, 600, 900, 1200, 1400, 1600, 4000 };
        unsigned bpp;
        int bc;
        fmt_bpp(gf, &bpp, &bc);
        unsigned ew = bc ? (w + 3) / 4 : w, eh = bc ? (h + 3) / 4 : h;
        unsigned pitch = align / 2, mp = min_pitch(ew, tile);
        if (pitch < mp || (uint64_t)pitch * eh * bpp / 8 > wp) pitch = mp;
        wtex_t *t = new_gx2(gf, w, h, tile, sw[swr], pitch, raw, wp);
        if (!t) goto bad;
        t->own = raw;
        return emit(add, ctx, t, name);
    }
bad:
    free(raw);
    return 0;
}

/* ---------------------------------------------------------------- NUT */

static int parse_nut(const uint8_t *b, uint32_t n, wtex_add_fn add, void *ctx)
{
    bool wu = !memcmp(b, "NTWU", 4);
    if (n < 0x10 || (!wu && memcmp(b, "NTP3", 4))) return 0;
    unsigned ver = b16(b + 4), cnt = b16(b + 6);
    if (!cnt || cnt > 1024) return 0;
    /* headers back to back from 0x10; in version 2 the data follows all of them */
    uint32_t hp = 0x10, dp = 0x10;
    for (unsigned i = 0; i < cnt && hp + 0x20 <= n; i++) dp += b16(b + hp + 0xC), hp += b16(b + hp + 0xC);
    int count = 0;
    hp = 0x10;
    for (unsigned i = 0; i < cnt; i++) {
        if (hp + 0x30 > n) break;
        const uint8_t *h = b + hp;
        uint32_t total = b32(h), dsz = b32(h + 8);
        unsigned hsz = b16(h + 0xC), fmt = h[0x13], w = b16(h + 0x14), hh = b16(h + 0x16);
        if (hsz < 0x30 || !w || !hh) break;
        uint32_t at = ver >= 0x200 ? dp : hp + hsz;
        if (ver < 0x200 && !dsz) dsz = total > hsz ? total - hsz : 0;
        if ((uint64_t)at + dsz > n) break;
        wtex_t *t = NULL;
        static const int16_t gx[0x17] = { 0x31, 0x32, 0x33, -1, -1, -1, -1, -1, 0x08, -1, -1, 0x1A, -1, -1,
                                          0x1A, -1, -1, 0x1A, -1, -1, -1, 0x34, 0x35 };
        if (fmt < 0x17 && gx[fmt] >= 0) {
            if (wu) {
                /* the GX2 surface info sits after the common header */
                uint32_t sw = hsz >= 0x60 ? b32(h + 0x58) : 0;
                t = new_gx2((unsigned)gx[fmt], w, hh, 4, sw & 0x700, 0, b + at, dsz);
            } else {
                t = calloc(1, sizeof(*t));
                if (t) {
                    t->linear = true;
                    t->w = w;
                    t->h = hh;
                    t->data = b + at;
                    t->size = dsz;
                    if (fmt <= 2) t->bc = fmt + 1;
                    else if (fmt == 0x15) t->bc = 4;
                    else if (fmt == 0x16) t->bc = 5;
                    else if (fmt == 8) t->lin_bpp = 2, t->rgb565 = 2;
                    else {
                        t->lin_bpp = 4;
                        if (fmt == 0x0E) t->chan[0] = 1, t->chan[1] = 2, t->chan[2] = 3, t->chan[3] = 0;  /* ARGB */
                        else t->chan[0] = 0, t->chan[1] = 1, t->chan[2] = 2, t->chan[3] = 3;
                    }
                }
            }
        }
        char lab[32];
        snprintf(lab, sizeof(lab), "texture %u", i + 1);
        int r = emit(add, ctx, t, lab);
        if (r < 0) break;
        count += r;
        dp += dsz;
        hp += hsz;
    }
    return count;
}

/* ---------------------------------------------------------------- DDS */

static int mask_shift(uint32_t m)
{
    if (!m) return -1;
    int s = 0;
    while (!(m & 1)) m >>= 1, s++;
    return s;
}

static int parse_dds(const uint8_t *b, uint32_t n, const char *name, wtex_add_fn add, void *ctx)
{
    if (n < 128 || memcmp(b, "DDS ", 4) || l32(b + 4) != 124) return 0;
    unsigned h = l32(b + 12), w = l32(b + 16);
    uint32_t pff = l32(b + 80), four = l32(b + 84), bits = l32(b + 88);
    uint32_t data = 128;
    wtex_t *t = calloc(1, sizeof(*t));
    if (!t) return 0;
    t->linear = true;
    t->w = w;
    t->h = h;
    if (pff & 4) {
        char fc[5] = { 0 };
        memcpy(fc, b + 84, 4);
        if (!strcmp(fc, "DXT1")) t->bc = 1;
        else if (!strcmp(fc, "DXT2") || !strcmp(fc, "DXT3")) t->bc = 2;
        else if (!strcmp(fc, "DXT4") || !strcmp(fc, "DXT5")) t->bc = 3;
        else if (!strcmp(fc, "ATI1") || !strcmp(fc, "BC4U") || !strcmp(fc, "BC4S")) t->bc = 4;
        else if (!strcmp(fc, "ATI2") || !strcmp(fc, "BC5U") || !strcmp(fc, "BC5S")) t->bc = 5;
        else if (!strcmp(fc, "DX10") && n >= 148) {
            uint32_t dx = l32(b + 128);
            data = 148;
            if (dx >= 70 && dx <= 72) t->bc = 1;
            else if (dx >= 73 && dx <= 75) t->bc = 2;
            else if (dx >= 76 && dx <= 78) t->bc = 3;
            else if (dx >= 79 && dx <= 81) t->bc = 4;
            else if (dx >= 82 && dx <= 84) t->bc = 5;
            else if (dx >= 27 && dx <= 32) t->lin_bpp = 4, t->chan[0] = 0, t->chan[1] = 1, t->chan[2] = 2, t->chan[3] = 3;
            else if (dx >= 87 && dx <= 93) t->lin_bpp = 4, t->chan[0] = 2, t->chan[1] = 1, t->chan[2] = 0,
                                                t->chan[3] = (dx == 88 || dx == 92 || dx == 93) ? -1 : 3;
        }
        (void)four;
    } else if ((pff & 0x40) && (bits == 32 || bits == 24)) {
        int rs = mask_shift(l32(b + 92)), gs = mask_shift(l32(b + 96)), bs = mask_shift(l32(b + 100)),
            as = (pff & 1) ? mask_shift(l32(b + 104)) : -1;
        if (rs >= 0 && gs >= 0 && bs >= 0 && !(rs % 8) && !(gs % 8) && !(bs % 8)) {
            t->lin_bpp = bits / 8;
            t->chan[0] = rs / 8;
            t->chan[1] = gs / 8;
            t->chan[2] = bs / 8;
            t->chan[3] = as >= 0 && !(as % 8) ? as / 8 : -1;
        }
    } else if ((pff & 0x40) && bits == 16 && l32(b + 92) == 0xF800) {
        t->lin_bpp = 2;
        t->rgb565 = 1;
    }
    if (!t->bc && !t->lin_bpp) {
        free(t);
        return 0;
    }
    t->data = b + data;
    t->size = n - data;
    return emit(add, ctx, t, name);
}

/* ================================================================ entry points */

bool wiiu_image_sniff(const uint8_t *h, uint32_t n, uint64_t size, const char *name)
{
    if (n < 8) return false;
    if (!memcmp(h, "Gfx2", 4) || !memcmp(h, "FFNT", 4) || !memcmp(h, "NTWU", 4) || !memcmp(h, "NTP3", 4) ||
        !memcmp(h, "DDS ", 4))
        return true;
    if (!memcmp(h, "FRES", 4) && n >= 10 && h[8] == 0xFE && h[9] == 0xFF) return true;
    if (n >= 0x18 && !memcmp(h, "RFRM", 4) && !memcmp(h + 0x14, "TXTR", 4)) return true;
    (void)size;
    if (name) {
        size_t l = strlen(name);
        if (l > 6 && !strcasecmp(name + l - 6, ".bflim")) return true;
    }
    return false;
}

const char *wiiu_textures(const uint8_t *b, uint32_t n, const char *name, wtex_add_fn add, void *ctx)
{
    if (n < 8) return NULL;
    if (!memcmp(b, "Gfx2", 4)) return parse_gtx(b, n, add, ctx) > 0 ? "GTX" : NULL;
    if (!memcmp(b, "FFNT", 4)) return parse_ffnt(b, n, add, ctx) > 0 ? "BFFNT" : NULL;
    if (!memcmp(b, "FRES", 4)) return parse_bfres(b, n, add, ctx) > 0 ? "BFRES" : NULL;
    if (!memcmp(b, "RFRM", 4)) return parse_tropical(b, n, name, add, ctx) > 0 ? "Tropical Freeze TXTR" : NULL;
    if (!memcmp(b, "NTWU", 4) || !memcmp(b, "NTP3", 4)) return parse_nut(b, n, add, ctx) > 0 ? "NUT" : NULL;
    if (!memcmp(b, "DDS ", 4)) return parse_dds(b, n, name, add, ctx) > 0 ? "DDS" : NULL;
    if (parse_flim(b, n, name, add, ctx) > 0) return "BFLIM";
    return NULL;
}
