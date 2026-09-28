/* em_gs_raster - CPU model of the GS drawing path (em_gs_raster.h,
 * docs/GS_EXACT.md). Clean room: public GS documentation plus the decomp's
 * conformance measurements; no emulator source. Section numbers in the
 * comments refer to docs/GS_EXACT.md. */
#include "gs/em_gs_raster.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef __int128 i128;

/* ------------------------------------------------------------------------
 * Small helpers */
static inline uint64_t bits(uint64_t v, unsigned lo, unsigned n) { return (v >> lo) & ((n >= 64) ? ~0ull : ((1ull << n) - 1)); }
static inline int64_t floordiv64(int64_t a, int64_t b)
{
    int64_t q = a / b, r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}
static inline i128 floordiv128(i128 a, i128 b)
{
    i128 q = a / b, r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float f32_of(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }

/* STQ (section 4.5, measured). When every vertex of the primitive's span
 * (section 3.7) has the same Z, or the span is a sprite span, every
 * vertex's S and T are floored to
 * 2^-(14 - E) and its Q to 16 significant bits (2^-(15 - E)), E being the
 * binary exponent of that vertex's Q (for a sprite: of the second vertex's
 * Q, which both corners use); otherwise they are used as given. Triangles
 * interpolate those values to the pixel, rounded to binary32; the texel
 * coordinate is the quotient. */
static int q_exponent(double Q)
{
    int E;
    (void)frexp(Q, &E);           /* Q = m * 2^E, m in [0.5, 1) */
    return E - 1;                 /* Q in [2^E, 2^(E+1)) */
}
static double stq_trunc_st(double S, int E) { return ldexp(floor(ldexp(S, 14 - E)), E - 14); }
static double stq_trunc_q(double Q, int E) { return ldexp(floor(ldexp(Q, 15 - E)), E - 15); }

/* floor(f32(S / Q) * 2^size * 16): the texel coordinate in 1/16 texel. The
 * quotient is rounded to binary32 (p8_class: sprites whose exact S/Q lies
 * just below a 1/16-texel step read the step; p5_q drops from 1,597 to
 * fewer values off). */
static int64_t stq_div(double S, double Q, int size_log2)
{
    if (!(Q > 0.0) || !isfinite(Q) || !isfinite(S))
        return 0;
    double r = floor((double)(float)((float)S / (float)Q) * ldexp(16.0, size_log2));
    /* coordinates beyond 2^40 (1/16 texels) are clamped (not measured) */
    if (!(r > -0x1p40)) return -(INT64_C(1) << 40);
    if (!(r < 0x1p40)) return INT64_C(1) << 40;
    return (int64_t)r;
}

static void span_end(EmGs *gs, int measured);

static void refuse(EmGs *gs, uint32_t why, const char *what)
{
    gs->refusals |= why;
    if (!gs->reason[0])
        snprintf(gs->reason, sizeof gs->reason, "%s", what);
}

/* ------------------------------------------------------------------------
 * 1. Local memory layout (section 2: the documented page / block / column
 * tables; PSMCT32, PSMZ32, PSMCT16 and PSMZ16 equal the maps the layout
 * batch measured, PSMT8 / PSMT4 equal the uploads' raw pages). */
static const uint8_t blk32[4][8] = {
    {0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23},
    {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}};
static const uint8_t blkz32[4][8] = {
    {24, 25, 28, 29, 8, 9, 12, 13}, {26, 27, 30, 31, 10, 11, 14, 15},
    {16, 17, 20, 21, 0, 1, 4, 5}, {18, 19, 22, 23, 2, 3, 6, 7}};
static const uint8_t col32[2][8] = {{0, 1, 4, 5, 8, 9, 12, 13}, {2, 3, 6, 7, 10, 11, 14, 15}};
static const uint8_t blk16[8][4] = {
    {0, 2, 8, 10}, {1, 3, 9, 11}, {4, 6, 12, 14}, {5, 7, 13, 15},
    {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
static const uint8_t blk16s[8][4] = {
    {0, 2, 16, 18}, {1, 3, 17, 19}, {8, 10, 24, 26}, {9, 11, 25, 27},
    {4, 6, 20, 22}, {5, 7, 21, 23}, {12, 14, 28, 30}, {13, 15, 29, 31}};
static const uint8_t blkz16[8][4] = {
    {24, 26, 16, 18}, {25, 27, 17, 19}, {28, 30, 20, 22}, {29, 31, 21, 23},
    {8, 10, 0, 2}, {9, 11, 1, 3}, {12, 14, 4, 6}, {13, 15, 5, 7}};
static const uint8_t blkz16s[8][4] = {
    {24, 26, 8, 10}, {25, 27, 9, 11}, {16, 18, 0, 2}, {17, 19, 1, 3},
    {28, 30, 12, 14}, {29, 31, 13, 15}, {20, 22, 4, 6}, {21, 23, 5, 7}};
static const uint8_t col16[2][16] = {
    {0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27},
    {4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31}};
/* PSMT8 / PSMT4 byte / nibble order inside a block (measured: the
 * texture batch's identity and random T8 / T4 uploads read back from
 * gs.bin; section 2). Column k (4 rows) of a block; in each column the
 * pixel pairs interleave as j = x & 7 with the 4-pixel halves swapped on
 * rows 2..3 of even columns and rows 0..1 of odd columns. */
static inline uint32_t in_block8(uint32_t x, uint32_t y)
{
    uint32_t r = y & 3, j = (x & 7) ^ ((((r >> 1) ^ (y >> 2)) & 1) * 4);
    return ((y >> 2) & 3) * 64 + (j & 1) * 4 + (j >> 1) * 16 + ((x >> 3) & 1) * 2 + (r & 1) * 8 + (r >> 1);
}
static inline uint32_t in_block4(uint32_t x, uint32_t y)
{
    uint32_t r = y & 3, j = (x & 7) ^ ((((r >> 1) ^ (y >> 2)) & 1) * 4);
    return ((y >> 2) & 3) * 128 + (j & 1) * 8 + (j >> 1) * 32 + ((x >> 3) & 3) * 2 + (r & 1) * 16 + (r >> 1);
}

/* Block numbers add to the base (bp) and wrap at the 4 MiB (16384 blocks). */
uint32_t em_gs_addr32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, int z)
{
    x &= 2047; y &= 2047;
    uint32_t page = (y >> 5) * bw + (x >> 6);
    uint32_t blk = (bp + page * 32 + (z ? blkz32 : blk32)[(y >> 3) & 3][(x >> 3) & 7]) & 16383;
    return blk * 64 + ((y >> 1) & 3) * 16 + col32[y & 1][x & 7];
}

uint32_t em_gs_addr16(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, unsigned psm)
{
    const uint8_t (*t)[4] = psm == EM_GS_PSMCT16S ? blk16s : psm == EM_GS_PSMZ16 ? blkz16
                          : psm == EM_GS_PSMZ16S ? blkz16s : blk16;
    x &= 2047; y &= 2047;
    uint32_t page = (y >> 6) * bw + (x >> 6);
    uint32_t blk = (bp + page * 32 + t[(y >> 3) & 7][(x >> 4) & 3]) & 16383;
    return blk * 128 + ((y >> 1) & 3) * 32 + col16[y & 1][x & 15];
}

uint32_t em_gs_addr8(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    x &= 2047; y &= 2047;
    uint32_t ppr = bw >> 1 ? bw >> 1 : 1;
    uint32_t page = (y >> 6) * ppr + (x >> 7);
    uint32_t blk = (bp + page * 32 + blk32[(y >> 4) & 3][(x >> 4) & 7]) & 16383;
    return blk * 256 + in_block8(x & 15, y & 15);
}

uint32_t em_gs_addr4(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    x &= 2047; y &= 2047;
    uint32_t ppr = bw >> 1 ? bw >> 1 : 1;
    uint32_t page = (y >> 7) * ppr + (x >> 7);
    uint32_t blk = (bp + page * 32 + blk16[(y >> 4) & 7][(x >> 5) & 3]) & 16383;
    return blk * 512 + in_block4(x & 31, y & 15);   /* nibble address */
}

static inline uint32_t rd32(const EmGs *gs, uint32_t word)
{
    uint32_t v;
    memcpy(&v, gs->mem + ((word * 4u) & (EM_GS_MEM_BYTES - 1)), 4);
    return v;
}
static inline void wr32(EmGs *gs, uint32_t word, uint32_t v)
{
    memcpy(gs->mem + ((word * 4u) & (EM_GS_MEM_BYTES - 1)), &v, 4);
}
static inline uint32_t rd16(const EmGs *gs, uint32_t half)
{
    uint16_t v;
    memcpy(&v, gs->mem + ((half * 2u) & (EM_GS_MEM_BYTES - 1)), 2);
    return v;
}
static inline void wr16(EmGs *gs, uint32_t half, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(gs->mem + ((half * 2u) & (EM_GS_MEM_BYTES - 1)), &h, 2);
}

uint32_t em_gs_read_pixel(const EmGs *gs, uint32_t bp, uint32_t bw, unsigned psm, uint32_t x, uint32_t y)
{
    switch (psm) {
    case EM_GS_PSMCT32: return rd32(gs, em_gs_addr32(bp, bw, x, y, 0));
    case EM_GS_PSMCT24: return rd32(gs, em_gs_addr32(bp, bw, x, y, 0)) & 0xFFFFFF;
    case EM_GS_PSMZ32: return rd32(gs, em_gs_addr32(bp, bw, x, y, 1));
    case EM_GS_PSMZ24: return rd32(gs, em_gs_addr32(bp, bw, x, y, 1)) & 0xFFFFFF;
    case EM_GS_PSMCT16: case EM_GS_PSMCT16S: case EM_GS_PSMZ16: case EM_GS_PSMZ16S:
        return rd16(gs, em_gs_addr16(bp, bw, x, y, psm));
    case EM_GS_PSMT8: return gs->mem[em_gs_addr8(bp, bw, x, y) & (EM_GS_MEM_BYTES - 1)];
    case EM_GS_PSMT4: {
        uint32_t n = em_gs_addr4(bp, bw, x, y);
        return (gs->mem[(n >> 1) & (EM_GS_MEM_BYTES - 1)] >> ((n & 1) * 4)) & 15;
    }
    case EM_GS_PSMT8H: return rd32(gs, em_gs_addr32(bp, bw, x, y, 0)) >> 24;
    case EM_GS_PSMT4HL: return (rd32(gs, em_gs_addr32(bp, bw, x, y, 0)) >> 24) & 15;
    case EM_GS_PSMT4HH: return rd32(gs, em_gs_addr32(bp, bw, x, y, 0)) >> 28;
    }
    return 0;
}

void em_gs_write_pixel(EmGs *gs, uint32_t bp, uint32_t bw, unsigned psm, uint32_t x, uint32_t y, uint32_t v)
{
    switch (psm) {
    case EM_GS_PSMCT32: wr32(gs, em_gs_addr32(bp, bw, x, y, 0), v); return;
    case EM_GS_PSMZ32: wr32(gs, em_gs_addr32(bp, bw, x, y, 1), v); return;
    case EM_GS_PSMCT24: case EM_GS_PSMZ24: {
        uint32_t a = em_gs_addr32(bp, bw, x, y, psm == EM_GS_PSMZ24);
        wr32(gs, a, (rd32(gs, a) & 0xFF000000u) | (v & 0xFFFFFF));
        return;
    }
    case EM_GS_PSMCT16: case EM_GS_PSMCT16S: case EM_GS_PSMZ16: case EM_GS_PSMZ16S:
        wr16(gs, em_gs_addr16(bp, bw, x, y, psm), v);
        return;
    case EM_GS_PSMT8: gs->mem[em_gs_addr8(bp, bw, x, y) & (EM_GS_MEM_BYTES - 1)] = (uint8_t)v; return;
    case EM_GS_PSMT4: {
        uint32_t n = em_gs_addr4(bp, bw, x, y);
        uint8_t *b = &gs->mem[(n >> 1) & (EM_GS_MEM_BYTES - 1)];
        *b = (n & 1) ? (uint8_t)((*b & 0x0F) | ((v & 15) << 4)) : (uint8_t)((*b & 0xF0) | (v & 15));
        return;
    }
    case EM_GS_PSMT8H: case EM_GS_PSMT4HL: case EM_GS_PSMT4HH: {
        uint32_t a = em_gs_addr32(bp, bw, x, y, 0), w = rd32(gs, a);
        if (psm == EM_GS_PSMT8H) w = (w & 0x00FFFFFFu) | (v << 24);
        else if (psm == EM_GS_PSMT4HL) w = (w & 0xF0FFFFFFu) | ((v & 15) << 24);
        else w = (w & 0x0FFFFFFFu) | ((v & 15) << 28);
        wr32(gs, a, w);
        return;
    }
    }
}

/* ------------------------------------------------------------------------
 * 2. Registers */
void em_gs_init(EmGs *gs, uint8_t *mem)
{
    memset(gs, 0, sizeof *gs);
    gs->mem = mem;
    gs->prmodecont = 1;
}

static int bits_per_pixel(unsigned psm)
{
    switch (psm) {
    case EM_GS_PSMCT32: case EM_GS_PSMZ32: case EM_GS_PSMT8H: case EM_GS_PSMT4HL: case EM_GS_PSMT4HH: return 32;
    case EM_GS_PSMCT24: case EM_GS_PSMZ24: return 24;
    case EM_GS_PSMCT16: case EM_GS_PSMCT16S: case EM_GS_PSMZ16: case EM_GS_PSMZ16S: return 16;
    case EM_GS_PSMT8: return 8;
    case EM_GS_PSMT4: return 4;
    }
    return 0;
}

/* CLUT buffer load (section 4.6): CSM1 layouts; a CT32 entry keeps its low
 * halfword at buf[i] and its high halfword at buf[i + 256]. */
static void clut_load(EmGs *gs, uint64_t tex0)
{
    unsigned psm = (unsigned)bits(tex0, 20, 6), cpsm = (unsigned)bits(tex0, 51, 4);
    unsigned csm = (unsigned)bits(tex0, 55, 1), csa = (unsigned)bits(tex0, 56, 5);
    unsigned cld = (unsigned)bits(tex0, 61, 3);
    uint32_t cbp = (uint32_t)bits(tex0, 37, 14);
    int t8 = psm == EM_GS_PSMT8 || psm == EM_GS_PSMT8H;
    int t4 = psm == EM_GS_PSMT4 || psm == EM_GS_PSMT4HL || psm == EM_GS_PSMT4HH;
    if (!t8 && !t4)
        return;
    switch (cld) {
    case 0: return;
    case 1: break;
    case 2: gs->cbp0 = cbp; break;
    case 3: gs->cbp1 = cbp; break;
    case 4: if (gs->cbp0 == cbp) return; gs->cbp0 = cbp; break;
    case 5: if (gs->cbp1 == cbp) return; gs->cbp1 = cbp; break;
    default: return;
    }
    if (csm) {
        refuse(gs, EM_GS_REFUSE_FORMAT, "CSM2 CLUT storage");
        return;
    }
    unsigned n = t8 ? 256 : 16;
    uint16_t next[512];
    memcpy(next, gs->clut, sizeof next);
    for (unsigned i = 0; i < n; i++) {
        uint32_t x, y;
        if (t8) {
            unsigned p = (i & ~0x18u) | ((i & 0x08u) << 1) | ((i & 0x10u) >> 1);
            x = p & 15; y = p >> 4;
        } else {
            x = i & 7; y = i >> 3;
        }
        if (cpsm == EM_GS_PSMCT32 || cpsm == EM_GS_PSMCT24) {
            uint32_t c = rd32(gs, em_gs_addr32(cbp, 1, x, y, 0));
            unsigned e = t8 ? i : (csa & 15) * 16 + i;
            next[e & 255] = (uint16_t)c;
            next[(e & 255) + 256] = (uint16_t)(c >> 16);
        } else if (cpsm == EM_GS_PSMCT16 || cpsm == EM_GS_PSMCT16S) {
            uint32_t c = rd16(gs, em_gs_addr16(cbp, 1, x, y, cpsm));
            unsigned e = t8 ? i : csa * 16 + i;
            next[e & 511] = (uint16_t)c;
        } else {
            refuse(gs, EM_GS_REFUSE_FORMAT, "CLUT CPSM");
            return;
        }
    }
    /* A load ends a textured span when it changes the CLUT buffer (the
     * queued primitives still read the old one: p8_misc
     * clut_cld1_same_value; p8_more t8_clut_reload_change) or when it loads
     * from another CBP, even with the same contents (p8_more
     * t8_tex0_cbp_reload / t8_tex2_cbp_reload). A reload of the same CBP
     * with the same contents does not (t8_tex0_same_reload). Another CPSM,
     * CSM or CSA alone, with the same contents, is not measured. */
    uint64_t src = (uint64_t)cbp | (uint64_t)cpsm << 14 | (uint64_t)csm << 18 | (uint64_t)(csa & 31) << 19;
    int changed = memcmp(next, gs->clut, sizeof next) != 0;
    int textured = (gs->pend_n && ((gs->pend_attr >> 4) & 1))
                   || (!gs->pend_n && gs->chain_open && ((gs->chain_attr >> 4) & 1));
    if (textured) {
        if (changed || !gs->clut_src_valid || (src & 0x3FFF) != (gs->clut_src & 0x3FFF))
            span_end(gs, changed || (gs->clut_src_valid && (src & 0x3FFF) != (gs->clut_src & 0x3FFF)));
        else if (src != gs->clut_src)
            span_end(gs, 0);
    }
    gs->clut_src = src;
    gs->clut_src_valid = 1;
    memcpy(gs->clut, next, sizeof next);
}

/* HOST -> LOCAL transfer (section 2.2): pixels in raster order over the
 * TRXREG rectangle from (DSAX, DSAY). */
static void mem_range(uint32_t bp, uint32_t bw, unsigned psm, uint32_t rows, uint64_t *lo, uint64_t *hi);

static void trx_start(EmGs *gs)
{
    unsigned dir = (unsigned)bits(gs->trxdir, 0, 2);
    if (gs->pend_n) {
        /* a HOST -> LOCAL transfer into the frame, Z or texture memory the
         * queued primitives use ends the span (p8_span trx_*); one elsewhere
         * does not (p7_flush). LOCAL -> LOCAL / HOST are not measured. */
        uint64_t lo, hi;
        mem_range((uint32_t)bits(gs->bitbltbuf, 32, 14), (uint32_t)bits(gs->bitbltbuf, 48, 6),
                  (unsigned)bits(gs->bitbltbuf, 56, 6),
                  (uint32_t)bits(gs->trxpos, 48, 11) + (uint32_t)bits(gs->trxreg, 32, 12), &lo, &hi);
        int hit = 0;
        for (int k = 0; k < 3; k++)
            if (gs->pend_hi[k] > gs->pend_lo[k] && lo < gs->pend_hi[k] && gs->pend_lo[k] < hi)
                hit = 1;
        if (dir == 1 || dir == 2)
            span_end(gs, 0);
        else if (hit)
            span_end(gs, 1);
    }
    gs->trx_x = gs->trx_y = 0;
    gs->trx_nibble = 0;
    gs->trx_active = 0;
    if (dir == 0) {
        gs->trx_active = 1;
    } else if (dir == 1) {
        /* LOCAL -> HOST: the model returns no data (documented; unmeasured) */
        if (gs->strict)
            refuse(gs, EM_GS_REFUSE_UNMEASURED, "LOCAL->HOST transfer");
    } else if (dir == 2) {
        /* LOCAL -> LOCAL (documented; no capture uses it). */
        if (gs->strict) {
            refuse(gs, EM_GS_REFUSE_UNMEASURED, "LOCAL->LOCAL transfer");
            return;
        }
        uint32_t sbp = (uint32_t)bits(gs->bitbltbuf, 0, 14), sbw = (uint32_t)bits(gs->bitbltbuf, 16, 6);
        unsigned spsm = (unsigned)bits(gs->bitbltbuf, 24, 6);
        uint32_t dbp = (uint32_t)bits(gs->bitbltbuf, 32, 14), dbw = (uint32_t)bits(gs->bitbltbuf, 48, 6);
        unsigned dpsm = (unsigned)bits(gs->bitbltbuf, 56, 6);
        uint32_t sx = (uint32_t)bits(gs->trxpos, 0, 11), sy = (uint32_t)bits(gs->trxpos, 16, 11);
        uint32_t dx = (uint32_t)bits(gs->trxpos, 32, 11), dy = (uint32_t)bits(gs->trxpos, 48, 11);
        uint32_t w = (uint32_t)bits(gs->trxreg, 0, 12), h = (uint32_t)bits(gs->trxreg, 32, 12);
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++)
                em_gs_write_pixel(gs, dbp, dbw, dpsm, dx + x, dy + y,
                                  em_gs_read_pixel(gs, sbp, sbw, spsm, sx + x, sy + y));
    }
}

static void trx_pixel(EmGs *gs, uint32_t v)
{
    uint32_t dbp = (uint32_t)bits(gs->bitbltbuf, 32, 14), dbw = (uint32_t)bits(gs->bitbltbuf, 48, 6);
    unsigned dpsm = (unsigned)bits(gs->bitbltbuf, 56, 6);
    uint32_t w = (uint32_t)bits(gs->trxreg, 0, 12), h = (uint32_t)bits(gs->trxreg, 32, 12);
    uint32_t dx = (uint32_t)bits(gs->trxpos, 32, 11), dy = (uint32_t)bits(gs->trxpos, 48, 11);
    if (!gs->trx_active || gs->trx_y >= h || w == 0)
        return;
    em_gs_write_pixel(gs, dbp, dbw, dpsm, dx + gs->trx_x, dy + gs->trx_y, v);
    if (++gs->trx_x >= w) {
        gs->trx_x = 0;
        if (++gs->trx_y >= h)
            gs->trx_active = 0;
    }
}

/* Image data for the running transfer: `bytes` bytes of pixels. */
static void trx_data(EmGs *gs, const uint8_t *p, size_t bytes)
{
    unsigned dpsm = (unsigned)bits(gs->bitbltbuf, 56, 6);
    int bpp = bits_per_pixel(dpsm);
    if (!gs->trx_active)
        return;
    if (bpp == 0) {
        refuse(gs, EM_GS_REFUSE_FORMAT, "transfer PSM");
        return;
    }
    if (dpsm == EM_GS_PSMT4HL || dpsm == EM_GS_PSMT4HH)
        bpp = 4;
    else if (dpsm == EM_GS_PSMT8H)
        bpp = 8;
    for (size_t i = 0; i < bytes && gs->trx_active;) {
        uint32_t v;
        switch (bpp) {
        case 32: if (i + 4 > bytes) return; memcpy(&v, p + i, 4); i += 4; break;
        case 24: if (i + 3 > bytes) return; v = p[i] | (uint32_t)p[i + 1] << 8 | (uint32_t)p[i + 2] << 16; i += 3; break;
        case 16: if (i + 2 > bytes) return; v = p[i] | (uint32_t)p[i + 1] << 8; i += 2; break;
        case 8: v = p[i++]; break;
        default:
            v = gs->trx_nibble ? (p[i] >> 4) : (p[i] & 15);
            if (gs->trx_nibble) i++;
            gs->trx_nibble ^= 1;
            break;
        }
        trx_pixel(gs, v);
    }
}

static void vertex_kick(EmGs *gs, int draw);

/* Section 3.7: a register write ends the span when it changes a value the
 * span uses. Measured to end it (p7_flush, p8_span, p8_more): TEX0 / TEX2
 * texture fields (below), TEX1, CLAMP and TEXA while TME is 1; ALPHA and
 * PABE while ABE is 1; FOGCOL while FGE is 1; DIMX while DTHE is 1; TEST,
 * COLCLAMP, DTHE, XYOFFSET, SCISSOR, FRAME, ZBUF and FBA always. Measured
 * not to end it: the same value again, those registers while their bit is
 * 0, TEXFLUSH, MIPTBP1 (MXL 0), TEXCLUT (CSM1) and TEX0 / TEX2 CLUT fields.
 * SCANMSK and the unmeasured TEX0 fields end it as unmeasured boundaries
 * (span_end). */
static void span_write(EmGs *gs, unsigned reg, uint64_t v)
{
    unsigned a;
    if (gs->pend_n)
        a = gs->pend_attr;
    else if (gs->chain_open)
        a = gs->chain_attr;
    else
        return;
    unsigned n = (a >> 9) & 1;
    int tme = (a >> 4) & 1, fge = (a >> 5) & 1, abe = (a >> 6) & 1;
    EmGsContext *c = NULL;
    const uint64_t *cur = NULL;
    uint64_t next = v;
    int rel = 0, measured = 1;
    switch (reg) {
    case EM_GS_TEX0_1: case EM_GS_TEX0_2: case EM_GS_TEX2_1: case EM_GS_TEX2_2: {
        /* Only the texture fields count (TBP0 .. TFX, bits 0-36): a change of
         * TBP0, TCC or TFX ends the span (p7_flush tex0_tfx, p8_more tex0_tbp /
         * tex0_tcc); a change of the CLUT fields alone does not, whatever the
         * texture format (p8_span tex2_cbp, p8_more tex0_cbp_ct32 /
         * tex0_cld_ct32 / tex2_* / t8_tex0_cbp_noload / t8_tex2_cbp_noload);
         * the CLUT load itself decides (clut_load). TBW, PSM, TW or TH alone
         * are not measured. TEX2 writes PSM and the CLUT fields. */
        const uint64_t tex_mask = (1ull << 37) - 1, measured_mask = 0x3FFFull | (7ull << 34);
        int two = reg == EM_GS_TEX2_1 || reg == EM_GS_TEX2_2;
        c = &gs->ctx[reg - (two ? EM_GS_TEX2_1 : EM_GS_TEX0_1)];
        uint64_t keep = ~((0x3Full << 20) | (~0ull << 37));
        uint64_t nv = two ? (c->tex0 & keep) | (v & ~keep) : v;
        uint64_t diff = (c->tex0 ^ nv) & tex_mask;
        if (c != &gs->ctx[n] || !tme || !diff)
            return;
        span_end(gs, (diff & measured_mask) != 0);
        return;
    }
    case EM_GS_CLAMP_1: case EM_GS_CLAMP_2: c = &gs->ctx[reg - EM_GS_CLAMP_1]; cur = &c->clamp; rel = tme; break;
    case EM_GS_TEX1_1: case EM_GS_TEX1_2: c = &gs->ctx[reg - EM_GS_TEX1_1]; cur = &c->tex1; rel = tme; break;
    case EM_GS_XYOFFSET_1: case EM_GS_XYOFFSET_2:
        c = &gs->ctx[reg - EM_GS_XYOFFSET_1]; cur = &c->xyoffset; rel = 1; break;
    case EM_GS_SCISSOR_1: case EM_GS_SCISSOR_2: c = &gs->ctx[reg - EM_GS_SCISSOR_1]; cur = &c->scissor; rel = 1; break;
    case EM_GS_ALPHA_1: case EM_GS_ALPHA_2: c = &gs->ctx[reg - EM_GS_ALPHA_1]; cur = &c->alpha; rel = abe; break;
    case EM_GS_TEST_1: case EM_GS_TEST_2: c = &gs->ctx[reg - EM_GS_TEST_1]; cur = &c->test; rel = 1; break;
    case EM_GS_FBA_1: case EM_GS_FBA_2: c = &gs->ctx[reg - EM_GS_FBA_1]; cur = &c->fba; rel = 1; break;
    case EM_GS_FRAME_1: case EM_GS_FRAME_2: c = &gs->ctx[reg - EM_GS_FRAME_1]; cur = &c->frame; rel = 1; break;
    case EM_GS_ZBUF_1: case EM_GS_ZBUF_2: c = &gs->ctx[reg - EM_GS_ZBUF_1]; cur = &c->zbuf; rel = 1; break;
    case EM_GS_TEXA: cur = &gs->texa; rel = tme; break;
    case EM_GS_PABE: cur = &gs->pabe; rel = abe; break;
    case EM_GS_FOGCOL: cur = &gs->fogcol; rel = fge; break;
    case EM_GS_DIMX: cur = &gs->dimx; rel = (int)(gs->dthe & 1); break;
    case EM_GS_DTHE: cur = &gs->dthe; rel = 1; break;
    case EM_GS_COLCLAMP: cur = &gs->colclamp; rel = 1; break;
    case EM_GS_SCANMSK: cur = &gs->scanmsk; rel = 1; measured = 0; break;
    default: return;                   /* MIPTBP1/2, TEXCLUT, TEXFLUSH, PRMODE, ... */
    }
    if (c && c != &gs->ctx[n])
        rel = 0;                       /* the other context's register */
    if (rel && *cur != next)
        span_end(gs, measured);
}

void em_gs_write(EmGs *gs, unsigned reg, uint64_t v)
{
    span_write(gs, reg, v);
    switch (reg) {
    case EM_GS_PRIM:
        gs->prim = v & 0x7FF;
        gs->queued = 0;
        gs->fan_count = 0;
        break;
    case EM_GS_RGBAQ:
        gs->cur.rgba[0] = (uint8_t)v; gs->cur.rgba[1] = (uint8_t)(v >> 8);
        gs->cur.rgba[2] = (uint8_t)(v >> 16); gs->cur.rgba[3] = (uint8_t)(v >> 24);
        gs->cur.q = (uint32_t)(v >> 32);
        break;
    case EM_GS_ST: gs->cur.s = (uint32_t)v; gs->cur.t = (uint32_t)(v >> 32); break;
    case EM_GS_UV: gs->cur.u = (uint32_t)bits(v, 0, 14); gs->cur.v = (uint32_t)bits(v, 16, 14); break;
    case EM_GS_FOG: gs->cur.f = (uint32_t)bits(v, 56, 8); break;
    case EM_GS_XYZF2: case EM_GS_XYZF3:
        gs->cur.x = (int32_t)bits(v, 0, 16); gs->cur.y = (int32_t)bits(v, 16, 16);
        gs->cur.z = (uint32_t)bits(v, 32, 24); gs->cur.f = (uint32_t)bits(v, 56, 8);
        vertex_kick(gs, reg == EM_GS_XYZF2);
        break;
    case EM_GS_XYZ2: case EM_GS_XYZ3:
        gs->cur.x = (int32_t)bits(v, 0, 16); gs->cur.y = (int32_t)bits(v, 16, 16);
        gs->cur.z = (uint32_t)(v >> 32);
        vertex_kick(gs, reg == EM_GS_XYZ2);
        break;
    case EM_GS_TEX0_1: case EM_GS_TEX0_2:
        gs->ctx[reg - EM_GS_TEX0_1].tex0 = v;
        clut_load(gs, v);
        break;
    case EM_GS_CLAMP_1: case EM_GS_CLAMP_2: gs->ctx[reg - EM_GS_CLAMP_1].clamp = v; break;
    case EM_GS_TEX1_1: case EM_GS_TEX1_2: gs->ctx[reg - EM_GS_TEX1_1].tex1 = v; break;
    case EM_GS_TEX2_1: case EM_GS_TEX2_2: {
        /* TEX2 writes PSM and the CLUT fields of TEX0 (documented). */
        EmGsContext *c = &gs->ctx[reg - EM_GS_TEX2_1];
        uint64_t keep = ~((0x3Full << 20) | (~0ull << 37));
        c->tex0 = (c->tex0 & keep) | (v & ~keep);
        c->tex2 = v;
        clut_load(gs, c->tex0);
        break;
    }
    case EM_GS_XYOFFSET_1: case EM_GS_XYOFFSET_2: gs->ctx[reg - EM_GS_XYOFFSET_1].xyoffset = v; break;
    case EM_GS_PRMODECONT: gs->prmodecont = v; break;
    case EM_GS_PRMODE: gs->prmode = v; break;
    case EM_GS_TEXCLUT: gs->texclut = v; break;
    case EM_GS_SCANMSK: gs->scanmsk = v; break;
    case EM_GS_MIPTBP1_1: case EM_GS_MIPTBP1_2: gs->ctx[reg - EM_GS_MIPTBP1_1].miptbp1 = v; break;
    case EM_GS_MIPTBP2_1: case EM_GS_MIPTBP2_2: gs->ctx[reg - EM_GS_MIPTBP2_1].miptbp2 = v; break;
    case EM_GS_TEXA: gs->texa = v; break;
    case EM_GS_FOGCOL: gs->fogcol = v; break;
    case EM_GS_TEXFLUSH: break;
    case EM_GS_SCISSOR_1: case EM_GS_SCISSOR_2: gs->ctx[reg - EM_GS_SCISSOR_1].scissor = v; break;
    case EM_GS_ALPHA_1: case EM_GS_ALPHA_2: gs->ctx[reg - EM_GS_ALPHA_1].alpha = v; break;
    case EM_GS_DIMX: gs->dimx = v; break;
    case EM_GS_DTHE: gs->dthe = v; break;
    case EM_GS_COLCLAMP: gs->colclamp = v; break;
    case EM_GS_TEST_1: case EM_GS_TEST_2: gs->ctx[reg - EM_GS_TEST_1].test = v; break;
    case EM_GS_PABE: gs->pabe = v; break;
    case EM_GS_FBA_1: case EM_GS_FBA_2: gs->ctx[reg - EM_GS_FBA_1].fba = v; break;
    case EM_GS_FRAME_1: case EM_GS_FRAME_2: gs->ctx[reg - EM_GS_FRAME_1].frame = v; break;
    case EM_GS_ZBUF_1: case EM_GS_ZBUF_2: gs->ctx[reg - EM_GS_ZBUF_1].zbuf = v; break;
    case EM_GS_BITBLTBUF: gs->bitbltbuf = v; break;
    case EM_GS_TRXPOS: gs->trxpos = v; break;
    case EM_GS_TRXREG: gs->trxreg = v; break;
    case EM_GS_TRXDIR: gs->trxdir = v; trx_start(gs); break;
    case EM_GS_HWREG: {
        uint8_t b[8];
        memcpy(b, &v, 8);
        trx_data(gs, b, 8);
        break;
    }
    case EM_GS_SIGNAL: case EM_GS_FINISH: case EM_GS_LABEL: break;
    default:
        refuse(gs, EM_GS_REFUSE_REGISTER, "unknown GS register");
        break;
    }
}

/* ------------------------------------------------------------------------
 * 3. GIF packets (section 2.1: the GIF tag, PACKED register descriptors,
 * REGLIST and IMAGE). */
size_t em_gs_gif(EmGs *gs, const void *data, size_t bytes)
{
    const uint8_t *p = data;
    size_t off = 0;
    while (off + 16 <= bytes) {
        uint64_t lo, hi;
        memcpy(&lo, p + off, 8);
        memcpy(&hi, p + off + 8, 8);
        off += 16;
        /* the Q a PACKED RGBAQ takes: 1.0 at every tag, then the last PACKED
         * ST's Q in that tag; A+D RGBAQ / ST writes leave it (p8_gif pk_q_*) */
        uint32_t packed_q = 0x3F800000u;
        unsigned nloop = (unsigned)bits(lo, 0, 15), pre = (unsigned)bits(lo, 46, 1);
        unsigned flg = (unsigned)bits(lo, 58, 2), nreg = (unsigned)bits(lo, 60, 4);
        if (nreg == 0)
            nreg = 16;
        if (flg == 0) {
            if (pre)
                em_gs_write(gs, EM_GS_PRIM, bits(lo, 47, 11));
            size_t need = (size_t)nloop * nreg * 16;
            if (off + need > bytes) {
                refuse(gs, EM_GS_REFUSE_GIF, "PACKED data past the end");
                return off;
            }
            for (unsigned l = 0; l < nloop; l++) {
                for (unsigned r = 0; r < nreg; r++) {
                    uint64_t a, b;
                    memcpy(&a, p + off, 8);
                    memcpy(&b, p + off + 8, 8);
                    off += 16;
                    unsigned d = (unsigned)bits(hi, 4 * r, 4);
                    switch (d) {
                    case 0x0: em_gs_write(gs, EM_GS_PRIM, a & 0x7FF); break;
                    case 0x1:
                        em_gs_write(gs, EM_GS_RGBAQ, bits(a, 0, 8) | bits(a, 32, 8) << 8 | bits(b, 0, 8) << 16
                                    | bits(b, 32, 8) << 24 | (uint64_t)packed_q << 32);
                        break;
                    case 0x2:
                        packed_q = (uint32_t)b;
                        em_gs_write(gs, EM_GS_ST, a);
                        break;
                    case 0x3: em_gs_write(gs, EM_GS_UV, bits(a, 0, 14) | bits(a, 32, 14) << 16); break;
                    case 0x4: {
                        uint64_t v = bits(a, 0, 16) | bits(a, 32, 16) << 16 | bits(b, 4, 24) << 32
                                   | bits(b, 36, 8) << 56;
                        em_gs_write(gs, bits(b, 47, 1) ? EM_GS_XYZF3 : EM_GS_XYZF2, v);
                        break;
                    }
                    case 0x5: {
                        uint64_t v = bits(a, 0, 16) | bits(a, 32, 16) << 16 | bits(b, 0, 32) << 32;
                        em_gs_write(gs, bits(b, 47, 1) ? EM_GS_XYZ3 : EM_GS_XYZ2, v);
                        break;
                    }
                    case 0x6: case 0x7: case 0x8: case 0x9: em_gs_write(gs, d, a); break;
                    case 0xA: em_gs_write(gs, EM_GS_FOG, bits(b, 36, 8) << 56); break;
                    case 0xC: {    /* XYZF3: the XYZF2 PACKED layout, no drawing kick (p8_gif2) */
                        uint64_t v = bits(a, 0, 16) | bits(a, 32, 16) << 16 | bits(b, 4, 24) << 32
                                   | bits(b, 36, 8) << 56;
                        em_gs_write(gs, EM_GS_XYZF3, v);
                        break;
                    }
                    case 0xD: {    /* XYZ3: the XYZ2 PACKED layout, no drawing kick (p8_gif2) */
                        uint64_t v = bits(a, 0, 16) | bits(a, 32, 16) << 16 | bits(b, 0, 32) << 32;
                        em_gs_write(gs, EM_GS_XYZ3, v);
                        break;
                    }
                    case 0xE: em_gs_write(gs, (unsigned)bits(b, 0, 8), a); break;
                    default: break;   /* 0xB reserved, 0xF NOP */
                    }
                }
            }
        } else if (flg == 1) {
            size_t count = (size_t)nloop * nreg, need = ((count + 1) / 2) * 16;
            if (off + need > bytes) {
                refuse(gs, EM_GS_REFUSE_GIF, "REGLIST data past the end");
                return off;
            }
            for (size_t i = 0; i < count; i++) {
                uint64_t a;
                memcpy(&a, p + off + i * 8, 8);
                unsigned d = (unsigned)bits(hi, 4 * (unsigned)(i % nreg), 4);
                if (d == 0xE || d == 0xF)
                    continue;
                em_gs_write(gs, d, a);
            }
            off += need;
        } else {
            size_t need = (size_t)nloop * 16;
            if (off + need > bytes) {
                refuse(gs, EM_GS_REFUSE_GIF, "IMAGE data past the end");
                return off;
            }
            trx_data(gs, p + off, need);
            off += need;
        }
    }
    return off;
}

/* ------------------------------------------------------------------------
 * 4. The fragment pipeline (section 5): texture, texture function, fog,
 * alpha test, DATE, Z test, blend, dither, clamp, format, FBA, masks. */
typedef struct {
    const EmGsContext *c;
    unsigned prim;          /* the attribute bits in effect (IIP, TME, FGE, ABE, AA1, FST, CTXT, FIX) */
    unsigned kind;          /* primitive type 0..6                                  */
    int dither;             /* this primitive kind dithers (triangles, lines)      */
    /* frame / Z */
    uint32_t fbp, fbw, fpsm, fbmsk, zbp, zpsm, zmsk;
    int zte, ztst;
    int ate, atst, aref, afail, date, datm;
    int abe, pabe, fba, colclamp, dthe;
    int ba, bb, bc, bd, fix;
    int fog;
    int fogcol[3];
    int sx0, sx1, sy0, sy1;
    int scanmsk;
    /* texture */
    int tme, fst;
    uint32_t tbp, tbw, tpsm, tw, th, tcc, tfx, cpsm;
    unsigned csa;
    int mmag, mmin;
    int wms, wmt, minu, maxu, minv, maxv;
    uint32_t ta0, ta1, aem;
    int stq_grid;           /* the span's vertices share one Z: STQ on the grid (4.5) */
} Draw;

static uint32_t texel_rgba(const EmGs *gs, const Draw *d, int u, int v)
{
    /* wrap (section 4.3), on the integer texel index */
    int W = 1 << d->tw, H = 1 << d->th;
    switch (d->wms) {
    case 0: u &= W - 1; break;
    case 1: u = clampi(u, 0, W - 1); break;
    case 2: u = clampi(u, d->minu, d->maxu); break;
    default: u = (u & d->minu) | d->maxu; break;
    }
    switch (d->wmt) {
    case 0: v &= H - 1; break;
    case 1: v = clampi(v, 0, H - 1); break;
    case 2: v = clampi(v, d->minv, d->maxv); break;
    default: v = (v & d->minv) | d->maxv; break;
    }
    uint32_t c, idx;
    switch (d->tpsm) {
    case EM_GS_PSMCT32: return rd32(gs, em_gs_addr32(d->tbp, d->tbw, (uint32_t)u, (uint32_t)v, 0));
    case EM_GS_PSMCT24:
        c = rd32(gs, em_gs_addr32(d->tbp, d->tbw, (uint32_t)u, (uint32_t)v, 0)) & 0xFFFFFF;
        return c | ((d->aem && c == 0) ? 0 : d->ta0 << 24);
    case EM_GS_PSMCT16: case EM_GS_PSMCT16S:
        c = rd16(gs, em_gs_addr16(d->tbp, d->tbw, (uint32_t)u, (uint32_t)v, d->tpsm));
        goto expand16;
    case EM_GS_PSMT8: case EM_GS_PSMT8H: case EM_GS_PSMT4: case EM_GS_PSMT4HL: case EM_GS_PSMT4HH:
        idx = em_gs_read_pixel(gs, d->tbp, d->tbw, d->tpsm, (uint32_t)u, (uint32_t)v);
        if (d->cpsm == EM_GS_PSMCT32 || d->cpsm == EM_GS_PSMCT24) {
            unsigned e = (d->tpsm == EM_GS_PSMT8 || d->tpsm == EM_GS_PSMT8H) ? idx : (d->csa & 15) * 16 + idx;
            c = gs->clut[e & 255] | (uint32_t)gs->clut[(e & 255) + 256] << 16;
            if (d->cpsm == EM_GS_PSMCT24)
                c = (c & 0xFFFFFF) | (((d->aem && (c & 0xFFFFFF) == 0) ? 0 : d->ta0) << 24);
            return c;
        }
        c = gs->clut[((d->tpsm == EM_GS_PSMT8 || d->tpsm == EM_GS_PSMT8H) ? idx : d->csa * 16 + idx) & 511];
        goto expand16;
    }
    return 0;
expand16: {
        uint32_t r = (c & 31) << 3, g = ((c >> 5) & 31) << 3, b = ((c >> 10) & 31) << 3;
        uint32_t a = (c & 0x8000) ? d->ta1 : ((d->aem && (c & 0x7FFF) == 0) ? 0 : d->ta0);
        return r | g << 8 | b << 16 | a << 24;
    }
}

/* Bilinear / nearest from fixed-point texel coordinates in 1/16 texel
 * (U16 = floor(u * 16)); section 4.2. */
static uint32_t sample(const EmGs *gs, const Draw *d, int64_t U16, int64_t V16, int bilinear)
{
    if (!bilinear)
        return texel_rgba(gs, d, (int)(U16 >> 4), (int)(V16 >> 4));
    int64_t Ub = U16 - 8, Vb = V16 - 8;
    int iu = (int)(Ub >> 4), iv = (int)(Vb >> 4), fu = (int)(Ub & 15), fv = (int)(Vb & 15);
    uint32_t t00 = texel_rgba(gs, d, iu, iv), t10 = texel_rgba(gs, d, iu + 1, iv);
    uint32_t t01 = texel_rgba(gs, d, iu, iv + 1), t11 = texel_rgba(gs, d, iu + 1, iv + 1);
    uint32_t out = 0;
    for (int k = 0; k < 32; k += 8) {
        int a = (int)((t00 >> k) & 255), b = (int)((t10 >> k) & 255);
        int c = (int)((t01 >> k) & 255), e = (int)((t11 >> k) & 255);
        int top = a + (((b - a) * fu) >> 4);
        int bot = c + (((e - c) * fu) >> 4);
        int val = top + (((bot - top) * fv) >> 4);
        out |= (uint32_t)(val & 255) << k;
    }
    return out;
}

typedef struct {
    int r, g, b, a;          /* vertex colour, 8.7 fixed (value << 7 when flat) */
    int f;                   /* fog weight, 8.7 fixed                           */
    uint64_t z;              /* Z before the format clamp           */
    int has_tex;             /* texel coordinates present           */
    int64_t U16, V16;        /* texel coordinates in 1/16 texel     */
} Frag;

static int zmax_of(uint32_t zpsm)
{
    return zpsm == EM_GS_PSMZ32 ? 0 : zpsm == EM_GS_PSMZ24 ? 0xFFFFFF : 0xFFFF;
}

static void plot(EmGs *gs, const Draw *d, int x, int y, const Frag *fr)
{
    if (x < d->sx0 || x > d->sx1 || y < d->sy0 || y > d->sy1)
        return;
    if ((d->scanmsk == 2 && !(y & 1)) || (d->scanmsk == 3 && (y & 1)))
        return;
    /* The colour and fog weight arrive in 8.7 fixed point; the texture
     * function and fog see that precision (section 5.1). */
    int r, g, b, a;
    if (d->tme && fr->has_tex) {
        int bil = d->mmag;   /* MXL 0: LOD 0 is magnification (section 4.2) */
        uint32_t t = sample(gs, d, fr->U16, fr->V16, bil);
        int tr = (int)(t & 255), tg = (int)((t >> 8) & 255), tb = (int)((t >> 16) & 255), ta = (int)(t >> 24);
        int af = fr->a >> 7;
        switch (d->tfx) {
        case 0:
            r = (tr * fr->r) >> 14; g = (tg * fr->g) >> 14; b = (tb * fr->b) >> 14;
            a = d->tcc ? (ta * fr->a) >> 14 : af;
            break;
        case 1:
            r = tr; g = tg; b = tb;
            a = d->tcc ? ta : af;
            break;
        case 2:
            r = ((tr * fr->r) >> 14) + af; g = ((tg * fr->g) >> 14) + af; b = ((tb * fr->b) >> 14) + af;
            a = d->tcc ? ta + af : af;
            break;
        default:
            r = ((tr * fr->r) >> 14) + af; g = ((tg * fr->g) >> 14) + af; b = ((tb * fr->b) >> 14) + af;
            a = d->tcc ? ta : af;
            break;
        }
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        if (a > 255) a = 255;
    } else {
        r = fr->r >> 7; g = fr->g >> 7; b = fr->b >> 7; a = fr->a >> 7;
    }
    if (d->fog) {
        int f = fr->f;
        r = d->fogcol[0] + (((r - d->fogcol[0]) * f) >> 15);
        g = d->fogcol[1] + (((g - d->fogcol[1]) * f) >> 15);
        b = d->fogcol[2] + (((b - d->fogcol[2]) * f) >> 15);
    }
    /* alpha test */
    int write_rgb = 1, write_a = 1, write_z = !d->zmsk;
    if (d->ate) {
        int pass;
        switch (d->atst) {
        case 0: pass = 0; break;
        case 1: pass = 1; break;
        case 2: pass = a < d->aref; break;
        case 3: pass = a <= d->aref; break;
        case 4: pass = a == d->aref; break;
        case 5: pass = a >= d->aref; break;
        case 6: pass = a > d->aref; break;
        default: pass = a != d->aref; break;
        }
        if (!pass) {
            switch (d->afail) {
            case 0: return;
            case 1: write_z = 0; break;
            case 2: write_rgb = write_a = 0; break;
            default: write_a = 0; write_z = 0; break;
            }
        }
    }
    /* frame word */
    uint32_t faddr = 0, fword = 0;
    int f32 = d->fpsm == EM_GS_PSMCT32 || d->fpsm == EM_GS_PSMCT24;
    if (f32) {
        faddr = em_gs_addr32(d->fbp, d->fbw, (uint32_t)x, (uint32_t)y, 0);
        fword = rd32(gs, faddr);
    } else {
        faddr = em_gs_addr16(d->fbp, d->fbw, (uint32_t)x, (uint32_t)y, d->fpsm);
        fword = rd16(gs, faddr);
    }
    if (d->date) {
        int dbit = f32 ? (int)(fword >> 31) : (int)((fword >> 15) & 1);
        if (d->fpsm == EM_GS_PSMCT24)
            dbit = 0;
        if (dbit != d->datm)
            return;
    }
    /* Z test */
    uint32_t zaddr = 0;
    uint32_t zval;
    int zmx = zmax_of(d->zpsm);
    if (d->zpsm == EM_GS_PSMZ32)
        zval = (uint32_t)(fr->z > 0xFFFFFFFFull ? 0xFFFFFFFFull : fr->z);
    else
        zval = (uint32_t)(fr->z > (uint64_t)zmx ? (uint64_t)zmx : fr->z);
    int z32 = d->zpsm == EM_GS_PSMZ32 || d->zpsm == EM_GS_PSMZ24;
    if (d->zte && (d->ztst >= 2 || write_z)) {
        if (z32)
            zaddr = em_gs_addr32(d->zbp, d->fbw, (uint32_t)x, (uint32_t)y, 1);
        else
            zaddr = em_gs_addr16(d->zbp, d->fbw, (uint32_t)x, (uint32_t)y, d->zpsm);
    }
    if (d->zte) {
        if (d->ztst == 0)
            return;
        if (d->ztst >= 2) {
            uint32_t zb = z32 ? rd32(gs, zaddr) : rd16(gs, zaddr);
            if (d->zpsm == EM_GS_PSMZ24) zb &= 0xFFFFFF;
            if (d->ztst == 2 ? zval < zb : zval <= zb)
                return;
        }
    }
    /* colour */
    int dr, dg, db, da;
    if (f32) {
        dr = (int)(fword & 255); dg = (int)((fword >> 8) & 255); db = (int)((fword >> 16) & 255);
        da = d->fpsm == EM_GS_PSMCT24 ? 0x80 : (int)(fword >> 24);
    } else {
        dr = (int)((fword & 31) << 3); dg = (int)(((fword >> 5) & 31) << 3); db = (int)(((fword >> 10) & 31) << 3);
        da = (fword & 0x8000) ? 0x80 : 0;
    }
    if (d->abe && (!d->pabe || (a & 0x80))) {
        int cs[3] = {r, g, b}, cd[3] = {dr, dg, db}, out[3];
        int cc = d->bc == 0 ? a : d->bc == 1 ? da : d->fix;
        for (int k = 0; k < 3; k++) {
            int A = d->ba == 0 ? cs[k] : d->ba == 1 ? cd[k] : 0;
            int B = d->bb == 0 ? cs[k] : d->bb == 1 ? cd[k] : 0;
            int D = d->bd == 0 ? cs[k] : d->bd == 1 ? cd[k] : 0;
            out[k] = (((A - B) * cc) >> 7) + D;
        }
        r = out[0]; g = out[1]; b = out[2];
    }
    if (d->dthe && !f32 && d->dither) {
        int dm = (int)((gs->dimx >> (16 * (y & 3) + 4 * (x & 3))) & 7);
        if (dm & 4) dm -= 8;
        r += dm; g += dm; b += dm;
    }
    if (d->colclamp) {
        r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
    } else {
        r &= 255; g &= 255; b &= 255;
    }
    int oa = a & 255;
    if (d->fba)
        oa |= 0x80;
    if (write_rgb || write_a) {
        uint32_t nw;
        if (f32) {
            uint32_t src = (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | (uint32_t)oa << 24;
            uint32_t keep = (uint32_t)d->fbmsk;
            if (!write_a) keep |= 0xFF000000u;
            if (!write_rgb) keep |= 0x00FFFFFFu;
            if (d->fpsm == EM_GS_PSMCT24) keep |= 0xFF000000u;
            nw = (fword & keep) | (src & ~keep);
            wr32(gs, faddr, nw);
        } else {
            uint32_t src = (uint32_t)(r >> 3) | (uint32_t)(g >> 3) << 5 | (uint32_t)(b >> 3) << 10
                         | (uint32_t)(oa >> 7) << 15;
            uint32_t m = (uint32_t)d->fbmsk;
            uint32_t keep = ((m >> 3) & 0x1F) | ((m >> 6) & 0x3E0) | ((m >> 9) & 0x7C00) | ((m >> 16) & 0x8000);
            if (!write_a) keep |= 0x8000;
            if (!write_rgb) keep |= 0x7FFF;
            nw = (fword & keep) | (src & ~keep);
            wr16(gs, faddr, nw);
        }
    }
    if (d->zte && write_z) {
        if (d->zpsm == EM_GS_PSMZ24)
            wr32(gs, zaddr, (rd32(gs, zaddr) & 0xFF000000u) | zval);
        else if (z32)
            wr32(gs, zaddr, zval);
        else
            wr16(gs, zaddr, zval);
    }
    gs->drawn_pixels++;
}

/* ------------------------------------------------------------------------
 * 5. Primitive setup */
static int draw_setup(EmGs *gs, Draw *d, unsigned kind)
{
    unsigned attr = (gs->prmodecont & 1) ? (unsigned)gs->prim : (unsigned)gs->prmode;
    unsigned ctxt = (attr >> 9) & 1;
    const EmGsContext *c = &gs->ctx[ctxt];
    memset(d, 0, sizeof *d);
    d->c = c;
    d->prim = attr;
    d->kind = kind;
    d->dither = kind != 6;             /* points dither too (p8_misc), sprites do not */
    d->fbp = (uint32_t)bits(c->frame, 0, 9) * 32;
    d->fbw = (uint32_t)bits(c->frame, 16, 6);
    d->fpsm = (uint32_t)bits(c->frame, 24, 6);
    d->fbmsk = (uint32_t)bits(c->frame, 32, 32);
    d->zbp = (uint32_t)bits(c->zbuf, 0, 9) * 32;
    d->zpsm = (uint32_t)bits(c->zbuf, 24, 4) | 0x30;
    d->zmsk = (uint32_t)bits(c->zbuf, 32, 1);
    d->ate = (int)bits(c->test, 0, 1);
    d->atst = (int)bits(c->test, 1, 3);
    d->aref = (int)bits(c->test, 4, 8);
    d->afail = (int)bits(c->test, 12, 2);
    d->date = (int)bits(c->test, 14, 1);
    d->datm = (int)bits(c->test, 15, 1);
    d->zte = (int)bits(c->test, 16, 1);
    d->ztst = (int)bits(c->test, 17, 2);
    d->abe = (int)((attr >> 6) & 1);
    d->pabe = (int)bits(gs->pabe, 0, 1);
    d->fba = (int)bits(c->fba, 0, 1);
    d->colclamp = (int)bits(gs->colclamp, 0, 1);
    d->dthe = (int)bits(gs->dthe, 0, 1);
    d->ba = (int)bits(c->alpha, 0, 2); d->bb = (int)bits(c->alpha, 2, 2);
    d->bc = (int)bits(c->alpha, 4, 2); d->bd = (int)bits(c->alpha, 6, 2);
    d->fix = (int)bits(c->alpha, 32, 8);
    d->fog = (int)((attr >> 5) & 1);
    d->fogcol[0] = (int)bits(gs->fogcol, 0, 8);
    d->fogcol[1] = (int)bits(gs->fogcol, 8, 8);
    d->fogcol[2] = (int)bits(gs->fogcol, 16, 8);
    d->sx0 = (int)bits(c->scissor, 0, 11); d->sx1 = (int)bits(c->scissor, 16, 11);
    d->sy0 = (int)bits(c->scissor, 32, 11); d->sy1 = (int)bits(c->scissor, 48, 11);
    d->scanmsk = (int)bits(gs->scanmsk, 0, 2);
    d->tme = (int)((attr >> 4) & 1);
    d->fst = (int)((attr >> 8) & 1);
    d->tbp = (uint32_t)bits(c->tex0, 0, 14);
    d->tbw = (uint32_t)bits(c->tex0, 14, 6);
    d->tpsm = (uint32_t)bits(c->tex0, 20, 6);
    d->tw = (uint32_t)bits(c->tex0, 26, 4);
    d->th = (uint32_t)bits(c->tex0, 30, 4);
    d->tcc = (uint32_t)bits(c->tex0, 34, 1);
    d->tfx = (uint32_t)bits(c->tex0, 35, 2);
    d->cpsm = (uint32_t)bits(c->tex0, 51, 4);
    d->csa = (unsigned)bits(c->tex0, 56, 5);
    d->mmag = (int)bits(c->tex1, 5, 1);
    d->mmin = (int)bits(c->tex1, 6, 3);
    d->wms = (int)bits(c->clamp, 0, 2); d->wmt = (int)bits(c->clamp, 2, 2);
    d->minu = (int)bits(c->clamp, 4, 10); d->maxu = (int)bits(c->clamp, 14, 10);
    d->minv = (int)bits(c->clamp, 24, 10); d->maxv = (int)bits(c->clamp, 34, 10);
    d->ta0 = (uint32_t)bits(gs->texa, 0, 8);
    d->aem = (uint32_t)bits(gs->texa, 15, 1);
    d->ta1 = (uint32_t)bits(gs->texa, 32, 8);

    /* refusals */
    if (d->fpsm != EM_GS_PSMCT32 && d->fpsm != EM_GS_PSMCT24 && d->fpsm != EM_GS_PSMCT16
        && d->fpsm != EM_GS_PSMCT16S) {
        refuse(gs, EM_GS_REFUSE_FORMAT, "frame PSM");
        return 0;
    }
    if (d->zte && d->zpsm != EM_GS_PSMZ32 && d->zpsm != EM_GS_PSMZ24 && d->zpsm != EM_GS_PSMZ16
        && d->zpsm != EM_GS_PSMZ16S) {
        refuse(gs, EM_GS_REFUSE_FORMAT, "Z PSM");
        return 0;
    }
    if (d->tme && bits_per_pixel(d->tpsm) == 0) {
        refuse(gs, EM_GS_REFUSE_FORMAT, "texture PSM");
        return 0;
    }
    if (gs->strict) {
        const char *why = NULL;
        if (attr & 0x80) why = "AA1";
        else if (attr & 0x400) why = "PRIM FIX";
        else if (ctxt) why = "context 2";
        else if (d->tme && bits(c->tex1, 2, 3)) why = "mipmapping (MXL > 0)";
        else if (d->tme && d->mmag != (d->mmin & 1)) why = "MMAG != MMIN";
        else if (d->tme && d->mmin > 1) why = "mipmap MMIN";
        else if (d->tme && (d->tpsm == EM_GS_PSMT8H || d->tpsm == EM_GS_PSMT4HL || d->tpsm == EM_GS_PSMT4HH))
            why = "PSMT8H / T4HL / T4HH";
        else if (d->tme && (d->tpsm == EM_GS_PSMCT24 || d->tpsm == EM_GS_PSMCT16 || d->tpsm == EM_GS_PSMCT16S))
            why = "CT24 / CT16 texture (TEXA)";
        else if (d->tme && (d->tpsm == EM_GS_PSMT8 || d->tpsm == EM_GS_PSMT4) && d->cpsm != EM_GS_PSMCT32)
            why = "CT16 CLUT";
        else if (d->scanmsk) why = "SCANMSK";
        else if (d->fpsm == EM_GS_PSMCT16S || d->fpsm == EM_GS_PSMCT24) why = "CT16S / CT24 frame";
        else if (d->zte && d->zpsm == EM_GS_PSMZ16S) why = "Z16S";
        else if (d->date && d->fpsm != EM_GS_PSMCT32) why = "DATE on a 16-bit frame";
        else if (!d->zte) why = "ZTE 0";
        else if (!(gs->prmodecont & 1)) why = "PRMODECONT.AC 0 (attributes from PRMODE)";
        else if (kind == 1 && d->tme) why = "textured line";
        else if (kind == 6 && d->tme && !d->fst) {
            /* sprites with Q other than 1 are handled per corner; allowed */
        }
        if (why) {
            char msg[64];
            snprintf(msg, sizeof msg, "unmeasured: %s", why);
            refuse(gs, EM_GS_REFUSE_UNMEASURED, msg);
            return 0;
        }
    }
    return 1;
}

static inline int wx(const EmGsContext *c, int32_t x) { return x - (int)bits(c->xyoffset, 0, 16); }
static inline int wy(const EmGsContext *c, int32_t y) { return y - (int)bits(c->xyoffset, 32, 16); }

/* ------------------------------------------------------------------------
 * 6. Sprites (section 3.4) */
static void draw_sprite(EmGs *gs, const Draw *d, const EmGsVertex *v0, const EmGsVertex *v1)
{
    int X0 = wx(d->c, v0->x), Y0 = wy(d->c, v0->y), X1 = wx(d->c, v1->x), Y1 = wy(d->c, v1->y);
    int xa = X0 < X1 ? X0 : X1, xb = X0 < X1 ? X1 : X0, ya = Y0 < Y1 ? Y0 : Y1, yb = Y0 < Y1 ? Y1 : Y0;
    int px0 = (int)floordiv64(xa + 15, 16), px1 = (int)floordiv64(xb + 15, 16) - 1;
    int py0 = (int)floordiv64(ya + 15, 16), py1 = (int)floordiv64(yb + 15, 16) - 1;
    if (px0 < d->sx0) px0 = d->sx0;
    if (px1 > d->sx1) px1 = d->sx1;
    if (py0 < d->sy0) py0 = d->sy0;
    if (py1 > d->sy1) py1 = d->sy1;
    Frag fr;
    memset(&fr, 0, sizeof fr);
    fr.r = v1->rgba[0] << 7; fr.g = v1->rgba[1] << 7; fr.b = v1->rgba[2] << 7; fr.a = v1->rgba[3] << 7;
    fr.f = (int)v1->f << 7;
    fr.z = v1->z;
    fr.has_tex = d->tme;
    /* texel coordinates: UV affine between the corners as given; STQ: S and
     * T affine between the corners, both divided by the SECOND vertex's Q
     * (section 4.4) */
    double s0 = 0, s1 = 0, t0 = 0, t1 = 0, qq = 1;
    if (d->tme && !d->fst) {
        qq = f32_of(v1->q);
        s0 = f32_of(v0->s); s1 = f32_of(v1->s); t0 = f32_of(v0->t); t1 = f32_of(v1->t);
        if (d->stq_grid && qq > 0.0 && isfinite(qq)) {
            int E = q_exponent(qq);
            s0 = stq_trunc_st(s0, E); s1 = stq_trunc_st(s1, E);
            t0 = stq_trunc_st(t0, E); t1 = stq_trunc_st(t1, E);
            qq = stq_trunc_q(qq, E);
        }
    }
    for (int y = py0; y <= py1; y++) {
        if (d->tme) {
            if (d->fst) {
                /* V16 = floor(V0 + (V1 - V0) * (16y - Y0) / (Y1 - Y0)) */
                fr.V16 = (Y1 == Y0) ? (int64_t)v0->v
                       : (int64_t)v0->v + floordiv64(((int64_t)v1->v - (int64_t)v0->v) * (16 * y - Y0), Y1 - Y0);
            } else {
                double t = (Y1 == Y0) ? 0.0 : (double)(16 * y - Y0) / (double)(Y1 - Y0);
                fr.V16 = stq_div(t0 + (t1 - t0) * t, qq, (int)d->th);
            }
        }
        for (int x = px0; x <= px1; x++) {
            if (d->tme) {
                if (d->fst) {
                    fr.U16 = (X1 == X0) ? (int64_t)v0->u
                           : (int64_t)v0->u + floordiv64(((int64_t)v1->u - (int64_t)v0->u) * (16 * x - X0), X1 - X0);
                } else {
                    double t = (X1 == X0) ? 0.0 : (double)(16 * x - X0) / (double)(X1 - X0);
                    fr.U16 = stq_div(s0 + (s1 - s0) * t, qq, (int)d->tw);
                }
            }
            plot(gs, d, x, y, &fr);
        }
    }
}

/* ------------------------------------------------------------------------
 * 7. Points (section 3.5) */
static void draw_point(EmGs *gs, const Draw *d, const EmGsVertex *v)
{
    int X = wx(d->c, v->x), Y = wy(d->c, v->y);
    int x = (int)floordiv64(X + 8, 16), y = (int)floordiv64(Y + 8, 16);
    Frag fr;
    memset(&fr, 0, sizeof fr);
    fr.r = v->rgba[0] << 7; fr.g = v->rgba[1] << 7; fr.b = v->rgba[2] << 7; fr.a = v->rgba[3] << 7;
    fr.f = (int)v->f << 7; fr.z = v->z;
    fr.has_tex = d->tme;
    if (d->tme) {
        if (d->fst) { fr.U16 = v->u; fr.V16 = v->v; }
        else {
            double q = f32_of(v->q), ss = f32_of(v->s), tt = f32_of(v->t);
            if (d->stq_grid && q > 0.0 && isfinite(q)) {
                int E = q_exponent(q);
                ss = stq_trunc_st(ss, E); tt = stq_trunc_st(tt, E); q = stq_trunc_q(q, E);
            }
            fr.U16 = stq_div(ss, q, (int)d->tw);
            fr.V16 = stq_div(tt, q, (int)d->th);
        }
    }
    plot(gs, d, x, y, &fr);
}

/* ------------------------------------------------------------------------
 * 8. Lines (section 3.6): the measured diamond rule. */
static int in_diamond(int px, int py, int cx, int cy, int xmajor)
{
    int ax = px - cx, ay = py - cy;
    int s = (ax < 0 ? -ax : ax) + (ay < 0 ? -ay : ay);
    if (s < 8)
        return 1;
    if (s > 8)
        return 0;
    return xmajor ? ay < 0 : ax < 0;
}

static void line_attr(const EmGsVertex *v0, const EmGsVertex *v1, int iip, int64_t num, int64_t den, Frag *fr)
{
    /* position t = num / den along the major axis (0 at v0, 1 at v1);
     * 8.7 values floored (section 3.6; Gouraud lines: open item 8.5) */
    if (!iip || den == 0) {
        fr->r = v1->rgba[0] << 7; fr->g = v1->rgba[1] << 7; fr->b = v1->rgba[2] << 7; fr->a = v1->rgba[3] << 7;
    } else {
        int c0[4] = {v0->rgba[0], v0->rgba[1], v0->rgba[2], v0->rgba[3]};
        int c1[4] = {v1->rgba[0], v1->rgba[1], v1->rgba[2], v1->rgba[3]};
        int out[4];
        for (int k = 0; k < 4; k++)
            out[k] = clampi((int)(c0[k] * 128 + floordiv64((int64_t)(c1[k] - c0[k]) * 128 * num, den)), 0, 255 * 128 + 127);
        fr->r = out[0]; fr->g = out[1]; fr->b = out[2]; fr->a = out[3];
    }
    if (den == 0) {
        fr->f = (int)v1->f << 7; fr->z = v1->z;
    } else {
        fr->f = clampi((int)((int64_t)v0->f * 128 + floordiv64(((int64_t)v1->f - (int64_t)v0->f) * 128 * num, den)), 0, 255 * 128 + 127);
        i128 z = (i128)v0->z + floordiv128(((i128)v1->z - (i128)v0->z) * num, den);
        fr->z = z < 0 ? 0 : (uint64_t)z;
    }
}

static void draw_line(EmGs *gs, const Draw *d, const EmGsVertex *v0, const EmGsVertex *v1)
{
    int X0 = wx(d->c, v0->x), Y0 = wy(d->c, v0->y), X1 = wx(d->c, v1->x), Y1 = wy(d->c, v1->y);
    int dx = X1 - X0, dy = Y1 - Y0;
    if (dx == 0 && dy == 0)
        return;
    int iip = (int)((d->prim >> 3) & 1);
    int xmajor = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
    /* work in (major, minor) coordinates */
    int a0 = xmajor ? X0 : Y0, b0 = xmajor ? Y0 : X0, a1 = xmajor ? X1 : Y1, b1 = xmajor ? Y1 : X1;
    int da = a1 - a0, db = b1 - b0, dir = da > 0 ? 1 : -1;
    Frag fr;
    memset(&fr, 0, sizeof fr);
    fr.has_tex = 0;
    if (d->tme)                        /* default mode: drawn untextured, flagged */
        refuse(gs, EM_GS_REFUSE_UNMEASURED, "textured line");
    /* the pixel before the start */
    if (a0 % 16 != 0) {
        int ab = dir > 0 ? (int)floordiv64(a0, 16) : (int)floordiv64(a0 + 15, 16);
        int bb = (int)floordiv64(b0 + 8, 16);
        for (int cand = bb - 1; cand <= bb + 1; cand++) {
            int px = xmajor ? a0 : b0, py = xmajor ? b0 : a0;
            int cx = xmajor ? 16 * ab : 16 * cand, cy = xmajor ? 16 * cand : 16 * ab;
            if (in_diamond(px, py, cx, cy, xmajor)) {
                line_attr(v0, v1, iip, (int64_t)(16 * ab - a0) * dir, (int64_t)da * dir, &fr);
                plot(gs, d, xmajor ? ab : cand, xmajor ? cand : ab, &fr);
                break;
            }
        }
    }
    /* centre lines from the start (inclusive) to the end (exclusive) */
    int first = dir > 0 ? (int)floordiv64(a0 + 15, 16) : (int)floordiv64(a0, 16);
    for (int A = first;; A += dir) {
        int64_t pa = 16 * (int64_t)A;
        if (dir > 0 ? pa >= a1 : pa <= a1)
            break;
        /* minor coordinate at this centre line, rounded half up */
        int64_t num = (int64_t)db * (pa - a0);
        int64_t bnum = (int64_t)b0 * da + num;   /* minor * da */
        /* B = floor((minor + 8) / 16) with minor = bnum / da */
        int64_t B = floordiv64(bnum + 8 * (int64_t)da, 16 * (int64_t)da);
        if (da < 0)
            B = floordiv64(-bnum - 8 * (int64_t)da, -16 * (int64_t)da);
        int ex = xmajor ? X1 : Y1, ey = xmajor ? Y1 : X1;   /* end in (major, minor) */
        int px = xmajor ? ex : ey, py = xmajor ? ey : ex;
        int cx = xmajor ? (int)pa : (int)(16 * B), cy = xmajor ? (int)(16 * B) : (int)pa;
        if (in_diamond(px, py, cx, cy, xmajor))
            continue;
        line_attr(v0, v1, iip, (pa - a0) * dir, (int64_t)da * dir, &fr);
        plot(gs, d, xmajor ? A : (int)B, xmajor ? (int)B : A, &fr);
    }
}

/* ------------------------------------------------------------------------
 * 9. Triangles (section 3.1 coverage, 3.2 colour and F, 3.3 Z) */
typedef struct {
    int64_t nx, ny;        /* gradient numerators: value = a0 + (nx*(16x-X0) + ny*(16y-Y0)) / A */
    int64_t a0;
} Plane;

static Plane plane(int64_t a0, int64_t a1, int64_t a2, int X0, int Y0, int X1, int Y1, int X2, int Y2)
{
    Plane p;
    p.a0 = a0;
    p.nx = (a1 - a0) * (int64_t)(Y2 - Y0) - (a2 - a0) * (int64_t)(Y1 - Y0);
    p.ny = (a2 - a0) * (int64_t)(X1 - X0) - (a1 - a0) * (int64_t)(X2 - X0);
    (void)X0; (void)Y0;
    return p;
}

/* Colour-class attributes (section 3.2). The row is drawn in 4-pixel
 * blocks aligned to x = 0 mod 4, starting with the block that holds the
 * row's first drawn pixel xd (the first covered pixel, or the scissor's
 * left edge when that is further right). With E the exact plane value:
 *   V7   = floor(128 * E(xd, y))                     (the start, 1/128 units)
 *   s    = trunc(512 * dC/dx)                        (the block step, 1/128)
 *   L(d) = trunc(128 * d * dC/dx), d = lane - start lane (-3..3)
 * pixel x (block k after the start block, lane j) holds
 *   V7 + k * s + L(j - jd), and the 8-bit value is that >> 7 (floor). */
static inline int64_t rowstart128(const Plane *p, int64_t A, int X0, int Y0, int x, int y)
{
    i128 num = (i128)p->nx * (16 * x - X0) + (i128)p->ny * (16 * y - Y0);
    return (int64_t)(128 * p->a0 + floordiv128(num * 128, A));
}
static inline int64_t step512(const Plane *p, int64_t A)
{
    i128 v = ((i128)p->nx * 16 * 512) / A;   /* C division truncates toward zero */
    return (int64_t)v;
}
static inline int64_t lane128(const Plane *p, int64_t A, int d)
{
    i128 v = ((i128)p->nx * 16 * 128 * d) / A;
    return (int64_t)v;
}

static void draw_triangle(EmGs *gs, const Draw *d, const EmGsVertex *v0, const EmGsVertex *v1,
                          const EmGsVertex *v2)
{
    int X[3] = {wx(d->c, v0->x), wx(d->c, v1->x), wx(d->c, v2->x)};
    int Y[3] = {wy(d->c, v0->y), wy(d->c, v1->y), wy(d->c, v2->y)};
    int64_t A = (int64_t)(X[1] - X[0]) * (Y[2] - Y[0]) - (int64_t)(Y[1] - Y[0]) * (X[2] - X[0]);
    if (A == 0)
        return;
    int s = A > 0 ? 1 : -1;
    const EmGsVertex *V[3] = {v0, v1, v2};
    int iip = (int)((d->prim >> 3) & 1);
    /* edges: E(px, py) = s * ((bx-ax)*(py-ay) - (by-ay)*(px-ax)) */
    struct { int64_t kx, ky, c; int tie; } e[3];
    for (int i = 0; i < 3; i++) {
        int ax = X[i], ay = Y[i], bx = X[(i + 1) % 3], by = Y[(i + 1) % 3];
        e[i].kx = -(int64_t)s * (by - ay);          /* dE/dpx */
        e[i].ky = (int64_t)s * (bx - ax);           /* dE/dpy */
        e[i].c = -(e[i].kx * ax + e[i].ky * ay);
        e[i].tie = e[i].kx > 0 || (e[i].kx == 0 && e[i].ky > 0);
    }
    int ymin = Y[0], ymax = Y[0];
    for (int i = 1; i < 3; i++) {
        if (Y[i] < ymin) ymin = Y[i];
        if (Y[i] > ymax) ymax = Y[i];
    }
    int py0 = (int)floordiv64(ymin + 15, 16), py1 = (int)floordiv64(ymax, 16);
    if (py0 < d->sy0) py0 = d->sy0;
    if (py1 > d->sy1) py1 = d->sy1;
    /* planes */
    Plane pc[4], pf, pz, pu, pv;
    const EmGsVertex *flat = v2;
    for (int k = 0; k < 4; k++)
        pc[k] = plane(V[0]->rgba[k], V[1]->rgba[k], V[2]->rgba[k], X[0], Y[0], X[1], Y[1], X[2], Y[2]);
    pf = plane(V[0]->f, V[1]->f, V[2]->f, X[0], Y[0], X[1], Y[1], X[2], Y[2]);
    pz = plane(V[0]->z, V[1]->z, V[2]->z, X[0], Y[0], X[1], Y[1], X[2], Y[2]);
    pu = plane(V[0]->u, V[1]->u, V[2]->u, X[0], Y[0], X[1], Y[1], X[2], Y[2]);
    pv = plane(V[0]->v, V[1]->v, V[2]->v, X[0], Y[0], X[1], Y[1], X[2], Y[2]);
    int64_t cstep[4], fstep = step512(&pf, A);
    /* Z (section 3.3): the row start is taken from the top vertex with the
     * gradients' numerators scaled by the binary32 reciprocal of the area;
     * the in-row step is the exact dZ/dx; double arithmetic, then floor */
    float inv_area = 1.0f / (float)A;
    double zgx = (double)pz.nx * 16.0 * (double)inv_area, zgy = (double)pz.ny * 16.0 * (double)inv_area;
    double zstep = (double)pz.nx * 16.0 / (double)A;
    /* the top vertex: least Y, then least X */
    int ztop = 0;
    for (int i = 1; i < 3; i++)
        if (Y[i] < Y[ztop] || (Y[i] == Y[ztop] && X[i] < X[ztop]))
            ztop = i;
    for (int k = 0; k < 4; k++)
        cstep[k] = step512(&pc[k], A);
    /* STQ (section 4.5): the vertex values on the STQ grid, S, T, Q
     * screen-linear to the pixel (rounded to binary32), then the quotient */
    double sv[3], tv[3], qv[3];
    for (int i = 0; i < 3; i++) {
        sv[i] = f32_of(V[i]->s); tv[i] = f32_of(V[i]->t); qv[i] = f32_of(V[i]->q);
        if (d->stq_grid && qv[i] > 0.0 && isfinite(qv[i])) {
            int E = q_exponent(qv[i]);
            sv[i] = stq_trunc_st(sv[i], E); tv[i] = stq_trunc_st(tv[i], E); qv[i] = stq_trunc_q(qv[i], E);
        }
    }
    Frag fr;
    memset(&fr, 0, sizeof fr);
    fr.has_tex = d->tme;
    for (int y = py0; y <= py1; y++) {
        int64_t py = 16 * (int64_t)y;
        int64_t lo = INT32_MIN, hi = INT32_MAX;
        int empty = 0;
        for (int i = 0; i < 3 && !empty; i++) {
            int64_t K = e[i].ky * py + e[i].c;      /* E = kx*px + K, px = 16x */
            if (e[i].kx > 0) {
                /* a left edge (always a tie edge): kx*16x + K >= 0,
                 * x >= ceil(-K / (16 kx)) */
                int64_t xm = -floordiv64(K, 16 * e[i].kx);
                if (xm > lo) lo = xm;
            } else if (e[i].kx < 0) {
                int64_t k = -e[i].kx;
                /* 16x*k < K (strict; kx < 0 is never a tie edge) -> x < K/(16k) */
                int64_t xm = floordiv64(K - 1, 16 * k);
                if (xm < hi) hi = xm;
            } else {
                if (!(K > 0 || (K == 0 && e[i].tie)))
                    empty = 1;
            }
        }
        if (empty)
            continue;
        if (lo < d->sx0) lo = d->sx0;
        if (hi > d->sx1) hi = d->sx1;
        if (lo > hi)
            continue;
        /* the row's first drawn pixel (section 3.2) */
        int xd = (int)lo;
        int xr = xd & ~3, jd = xd - xr;
        int64_t cacc[4], cst[4], cl[4][7], facc, fst, fl[7];
        for (int k = 0; k < 4; k++) {
            cacc[k] = rowstart128(&pc[k], A, X[0], Y[0], xd, y);
            cst[k] = cstep[k];
            for (int dd = -3; dd <= 3; dd++)
                cl[k][dd + 3] = lane128(&pc[k], A, dd);
        }
        facc = rowstart128(&pf, A, X[0], Y[0], xd, y);
        double zrow = (double)V[ztop]->z + ((double)xd - X[ztop] / 16.0) * zgx + ((double)y - Y[ztop] / 16.0) * zgy;
        fst = fstep;
        for (int dd = -3; dd <= 3; dd++)
            fl[dd + 3] = lane128(&pf, A, dd);
        for (int x = (int)lo; x <= (int)hi; x++) {
            int64_t kb = (x - xr) >> 2;
            int j = (x - xr) & 3;
            if (iip) {
                fr.r = clampi((int)(cacc[0] + kb * cst[0] + cl[0][j - jd + 3]), 0, 0x7FFF);
                fr.g = clampi((int)(cacc[1] + kb * cst[1] + cl[1][j - jd + 3]), 0, 0x7FFF);
                fr.b = clampi((int)(cacc[2] + kb * cst[2] + cl[2][j - jd + 3]), 0, 0x7FFF);
                fr.a = clampi((int)(cacc[3] + kb * cst[3] + cl[3][j - jd + 3]), 0, 0x7FFF);
            } else {
                fr.r = flat->rgba[0] << 7; fr.g = flat->rgba[1] << 7; fr.b = flat->rgba[2] << 7; fr.a = flat->rgba[3] << 7;
            }
            fr.f = clampi((int)(facc + kb * fst + fl[j - jd + 3]), 0, 0x7FFF);
            {
                double zz = zrow + (double)(x - xd) * zstep;
                fr.z = zz <= 0.0 ? 0 : (uint64_t)floor(zz);
            }
            if (d->tme) {
                if (d->fst) {
                    i128 nu = (i128)pu.nx * (16 * x - X[0]) + (i128)pu.ny * (16 * y - Y[0]);
                    i128 nv = (i128)pv.nx * (16 * x - X[0]) + (i128)pv.ny * (16 * y - Y[0]);
                    fr.U16 = (int64_t)((i128)pu.a0 + floordiv128(nu, A));
                    fr.V16 = (int64_t)((i128)pv.a0 + floordiv128(nv, A));
                } else {
                    /* barycentric weights at the pixel */
                    double px = 16.0 * x, pyy = 16.0 * y;
                    double w1 = ((px - X[0]) * (Y[2] - Y[0]) - (pyy - Y[0]) * (X[2] - X[0])) / (double)A;
                    double w2 = ((X[1] - X[0]) * (pyy - Y[0]) - (Y[1] - Y[0]) * (px - X[0])) / (double)A;
                    double w0 = 1.0 - w1 - w2;
                    double S = (float)(w0 * sv[0] + w1 * sv[1] + w2 * sv[2]);
                    double T = (float)(w0 * tv[0] + w1 * tv[1] + w2 * tv[2]);
                    double Q = (float)(w0 * qv[0] + w1 * qv[1] + w2 * qv[2]);
                    fr.U16 = stq_div(S, Q, (int)d->tw);
                    fr.V16 = stq_div(T, Q, (int)d->th);
                }
            }
            plot(gs, d, x, y, &fr);
        }
    }
}

/* ------------------------------------------------------------------------
 * 10. The span (section 3.7). Primitives are queued with their state and
 * drawn in order when the span ends (span_end): at a primitive whose
 * attribute bits or class (point, line, triangle, sprite) differ from the
 * span's, a write that changes state the span uses (span_write), a CLUT
 * load that changes the CLUT or comes from another CBP (clut_load), a
 * transfer into memory the span uses (trx_start), or em_gs_flush. A span
 * of points or triangles whose vertices share one Z, and any sprite span,
 * puts STQ on the vertex grid (section 4.5). Boundaries no capture has
 * measured are tracked as a chain and fault in strict mode when they could
 * change a grid decision. */
struct EmGsPending {
    Draw d;
    EmGsContext c;                     /* the context as it was at the kick */
    EmGsVertex v[3];
    unsigned kind;                     /* 0 point, 1 line, 3 triangle, 6 sprite */
};

/* A conservative byte range of a buffer: base block bp, width bw * 64,
 * `rows` rows, whole pages, plus one page when the base is inside a page. */
static void mem_range(uint32_t bp, uint32_t bw, unsigned psm, uint32_t rows, uint64_t *lo, uint64_t *hi)
{
    int bpp = bits_per_pixel(psm);
    uint32_t pw = bpp >= 16 ? 64 : 128, ph = bpp >= 24 ? 32 : bpp == 16 ? 64 : bpp == 8 ? 64 : 128;
    uint64_t wp = ((uint64_t)(bw ? bw : 1) * 64 + pw - 1) / pw, hp = ((uint64_t)rows + ph - 1) / ph;
    *lo = (uint64_t)bp * 256;
    *hi = *lo + (wp * (hp ? hp : 1) + (bp % 32 ? 1 : 0)) * 8192;
}

static void range_add(EmGs *gs, int k, uint64_t lo, uint64_t hi)
{
    if (gs->pend_hi[k] <= gs->pend_lo[k]) {
        gs->pend_lo[k] = lo;
        gs->pend_hi[k] = hi;
    } else {
        if (lo < gs->pend_lo[k]) gs->pend_lo[k] = lo;
        if (hi > gs->pend_hi[k]) gs->pend_hi[k] = hi;
    }
}

static void draw_one(EmGs *gs, struct EmGsPending *p)
{
    p->d.c = &p->c;
    switch (p->kind) {
    case 0: draw_point(gs, &p->d, &p->v[0]); break;
    case 1: draw_line(gs, &p->d, &p->v[0], &p->v[1]); break;
    case 6: draw_sprite(gs, &p->d, &p->v[0], &p->v[1]); break;
    default: draw_triangle(gs, &p->d, &p->v[0], &p->v[1], &p->v[2]); break;
    }
}

/* The span ends: draw the queue. `measured` says whether a capture showed
 * that this kind of boundary ends the reference's span (GS_EXACT.md 3.7).
 * Where it did not, the model still ends the span here, but it keeps a
 * chain of the spans joined by unmeasured boundaries: if merging them would
 * change the grid decision of any of them (one span has one Z and uses STQ,
 * and the chain's Z are not all equal), the result depends on an
 * unmeasured rule and strict mode faults (EM_GS_REFUSE_UNMEASURED). */
static void span_end(EmGs *gs, int measured)
{
    uint32_t n = gs->pend_n;
    if (n) {
        int uniform = (int)gs->pend_zconst, stq = (int)gs->pend_stq;
        if (gs->chain_open && gs->chain_attr == gs->pend_attr && gs->chain_class == gs->pend_class) {
            gs->chain_uniform = gs->chain_uniform && uniform && gs->chain_z == gs->pend_z;
            gs->chain_stq |= (uint32_t)(uniform && stq);
            if (gs->chain_stq && !gs->chain_uniform) {
                gs->span_faults++;
                if (gs->strict)
                    refuse(gs, EM_GS_REFUSE_UNMEASURED, "unmeasured: a span boundary the grid decision depends on");
            }
        } else {
            gs->chain_uniform = (uint32_t)uniform;
            gs->chain_z = gs->pend_z;
            gs->chain_stq = (uint32_t)(uniform && stq);
        }
        gs->chain_attr = gs->pend_attr;
        gs->chain_class = gs->pend_class;
        gs->chain_open = !measured;
    } else if (measured) {
        gs->chain_open = 0;
    }
    gs->pend_n = 0;
    for (int k = 0; k < 3; k++)
        gs->pend_lo[k] = gs->pend_hi[k] = 0;
    for (uint32_t i = 0; i < n; i++) {
        gs->pend[i].d.stq_grid = (int)gs->pend_zconst;
        draw_one(gs, &gs->pend[i]);
    }
}

void em_gs_flush(EmGs *gs)
{
    span_end(gs, 1);
}

void em_gs_release(EmGs *gs)
{
    em_gs_flush(gs);
    free(gs->pend);
    gs->pend = NULL;
    gs->pend_cap = 0;
}

static void queue_prim(EmGs *gs, const Draw *d, unsigned kind, const EmGsVertex *a, const EmGsVertex *b,
                       const EmGsVertex *c)
{
    unsigned attr = d->prim & 0x7F8, cls = kind;
    unsigned nv = kind == 0 ? 1 : kind == 3 ? 3 : 2;
    const EmGsVertex *vs[3] = {a, b, c};
    if (gs->pend_n && (attr != gs->pend_attr || cls != gs->pend_class))
        span_end(gs, 1);               /* measured: p7_flush prim_iip, p8_span prim_* / rev_*, p8_class */
    if (gs->pend_n == gs->pend_cap) {
        uint32_t cap = gs->pend_cap ? gs->pend_cap * 2 : 256;
        struct EmGsPending *np = realloc(gs->pend, (size_t)cap * sizeof *np);
        if (!np) {
            /* no memory: end the span here and draw this primitive alone */
            span_end(gs, 1);
            struct EmGsPending one;
            one.d = *d;
            one.c = *d->c;
            one.kind = kind;
            one.d.stq_grid = 1;
            for (unsigned i = 0; i < nv; i++) {
                one.v[i] = *vs[i];
                if (kind != 6 && vs[i]->z != vs[0]->z)
                    one.d.stq_grid = 0;
            }
            draw_one(gs, &one);
            return;
        }
        gs->pend = np;
        gs->pend_cap = cap;
    }
    if (gs->pend_n == 0) {
        gs->pend_attr = attr;
        gs->pend_class = cls;
        gs->pend_z = a->z;
        gs->pend_zconst = 1;
        gs->pend_stq = 0;
        /* a span of other attributes or class would have been ended by a
         * measured boundary anyway: the chain of unmeasured ones is closed */
        if (gs->chain_open && (attr != gs->chain_attr || cls != gs->chain_class))
            gs->chain_open = 0;
    }
    struct EmGsPending *p = &gs->pend[gs->pend_n++];
    p->d = *d;
    p->c = *d->c;
    p->kind = kind;
    if (d->tme && !d->fst)
        gs->pend_stq = 1;
    for (unsigned i = 0; i < nv; i++) {
        p->v[i] = *vs[i];
        /* a sprite span always uses the grid: its Z plays no part (p8_class
         * sprite_corner_*, c*_sprite_tri); points and triangles need one Z
         * over the whole span (p7_zc, p8_class c*_rev_tri_point) */
        if (kind != 6 && vs[i]->z != gs->pend_z)
            gs->pend_zconst = 0;
    }
    uint64_t lo, hi;
    mem_range(d->fbp, d->fbw, d->fpsm, (uint32_t)d->sy1 + 1, &lo, &hi);
    range_add(gs, 0, lo, hi);
    if (d->zte) {
        mem_range(d->zbp, d->fbw, d->zpsm, (uint32_t)d->sy1 + 1, &lo, &hi);
        range_add(gs, 1, lo, hi);
    }
    if (d->tme) {
        uint32_t rows = 1u << d->th;
        if ((uint32_t)d->maxv + 1 > rows) rows = (uint32_t)d->maxv + 1;
        mem_range(d->tbp, d->tbw, d->tpsm, rows, &lo, &hi);
        range_add(gs, 2, lo, hi);
    }
}

/* ------------------------------------------------------------------------
 * 11. The vertex queue (section 3.0) */
static int setup_counted(EmGs *gs, Draw *d, unsigned kind)
{
    if (draw_setup(gs, d, kind)) {
        gs->drawn_prims++;
        return 1;
    }
    gs->refused_prims++;
    return 0;
}

static void vertex_kick(EmGs *gs, int draw)
{
    unsigned kind = (unsigned)(gs->prim & 7);
    EmGsVertex v = gs->cur;
    Draw d;
    switch (kind) {
    case 0:
        if (draw && setup_counted(gs, &d, 0)) queue_prim(gs, &d, 0, &v, NULL, NULL);
        break;
    case 1: case 6:
        gs->queue[gs->queued++] = v;
        if (gs->queued == 2) {
            if (draw && setup_counted(gs, &d, kind)) {
                queue_prim(gs, &d, kind, &gs->queue[0], &gs->queue[1], NULL);
            }
            gs->queued = 0;
        }
        break;
    case 2:
        if (gs->queued == 0) {
            gs->queue[0] = v;
            gs->queued = 1;
        } else {
            if (draw && setup_counted(gs, &d, 1)) queue_prim(gs, &d, 1, &gs->queue[0], &v, NULL);
            gs->queue[0] = v;
        }
        break;
    case 3:
        gs->queue[gs->queued++] = v;
        if (gs->queued == 3) {
            if (draw && setup_counted(gs, &d, 3)) queue_prim(gs, &d, 3, &gs->queue[0], &gs->queue[1], &gs->queue[2]);
            gs->queued = 0;
        }
        break;
    case 4:
        if (gs->queued < 2) {
            gs->queue[gs->queued++] = v;
        } else {
            if (draw && setup_counted(gs, &d, 3)) queue_prim(gs, &d, 3, &gs->queue[0], &gs->queue[1], &v);
            gs->queue[0] = gs->queue[1];
            gs->queue[1] = v;
        }
        break;
    case 5:
        if (gs->queued < 2) {
            gs->queue[gs->queued++] = v;
        } else {
            if (draw && setup_counted(gs, &d, 3)) queue_prim(gs, &d, 3, &gs->queue[0], &gs->queue[1], &v);
            gs->queue[1] = v;
        }
        break;
    default:
        refuse(gs, EM_GS_REFUSE_REGISTER, "PRIM type 7");
        break;
    }
}
