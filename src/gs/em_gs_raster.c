/* em_gs_raster - CPU model of the GS drawing path (em_gs_raster.h,
 * docs/GS_EXACT.md). Clean room: public GS documentation plus the decomp's
 * conformance measurements; no emulator source. Section numbers in the
 * comments refer to docs/GS_EXACT.md. */
#include "gs/em_gs_raster.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) && defined(__GNUC__) && !defined(__clang__)
__extension__ typedef __int128 i128;
#else
typedef __int128 i128;
#endif

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
/* The same quotients through 64-bit division when the dividend fits (the
 * values are those of the 128-bit division; only the instruction differs). */
static inline int fits64(i128 a) { return a >= -(i128)INT64_MAX && a <= (i128)INT64_MAX; }
static inline i128 floordiv_fast(i128 a, int64_t b)
{
    return fits64(a) ? (i128)floordiv64((int64_t)a, b) : floordiv128(a, (i128)b);
}
static inline i128 truncdiv_fast(i128 a, int64_t b)       /* C division: toward zero */
{
    return fits64(a) ? (i128)((int64_t)a / b) : a / (i128)b;
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
    /* 16 * 2^size_log2 (TW / TH 0..10) exactly, as ldexp gives it */
    double r = floor((double)(float)((float)S / (float)Q) * (double)(UINT64_C(16) << (size_log2 & 15)));
    /* coordinates beyond 2^40 (1/16 texels) are clamped (not measured) */
    if (!(r > -0x1p40)) return -(INT64_C(1) << 40);
    if (!(r < 0x1p40)) return INT64_C(1) << 40;
    return (int64_t)r;
}

static void span_end(EmGs *gs, int measured);
/* section 4b, the decoded texture cache: memory written by a transfer or
 * em_gs_write_pixel, the CLUT buffer changed */
static void tc_written_rect(EmGs *gs, unsigned psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h);
static void tc_clut_changed(EmGs *gs);
static int list_clut_drawn(const EmGs *gs, unsigned cpsm, uint32_t cbp, int t8);

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
static inline uint32_t addr32_(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, int z)
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

static inline uint32_t addr8_(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    x &= 2047; y &= 2047;
    uint32_t ppr = bw >> 1 ? bw >> 1 : 1;
    uint32_t page = (y >> 6) * ppr + (x >> 7);
    uint32_t blk = (bp + page * 32 + blk32[(y >> 4) & 3][(x >> 4) & 7]) & 16383;
    return blk * 256 + in_block8(x & 15, y & 15);
}

static inline uint32_t addr4_(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    x &= 2047; y &= 2047;
    uint32_t ppr = bw >> 1 ? bw >> 1 : 1;
    uint32_t page = (y >> 7) * ppr + (x >> 7);
    uint32_t blk = (bp + page * 32 + blk16[(y >> 4) & 7][(x >> 5) & 3]) & 16383;
    return blk * 512 + in_block4(x & 31, y & 15);   /* nibble address */
}

/* The exported addressing functions are the inline ones above (the
 * fragment pipeline calls those directly). */
uint32_t em_gs_addr32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, int z) { return addr32_(bp, bw, x, y, z); }
uint32_t em_gs_addr8(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) { return addr8_(bp, bw, x, y); }
uint32_t em_gs_addr4(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) { return addr4_(bp, bw, x, y); }

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

/* ------------------------------------------------------------------------
 * 1b. The separable address tables (docs/PERFORMANCE.md M1). The layouts
 * the first level draws and reads every pixel (PSMCT32 / CT24 frames and
 * textures, PSMZ32 / Z24 Z buffers, PSMT8 and PSMT4 textures, and the CT32
 * CLUT loads; Progress P11 has the counts) split into a row part and a
 * column part: their address is
 *
 *     (bp * unit + row[bw][y & 2047] + col[s][x & 2047])  wrapped by the 4 MiB mask
 *
 * (unit: 64 words, 256 bytes or 512 nibbles a block). The page row, the
 * block row and the in-block row come only from y and the width; the page
 * column, the block column and the in-block column only from x. In PSMT8 /
 * PSMT4 one column bit is flipped on alternate pairs of rows (the 4-pixel
 * halves of in_block8 / in_block4): those rows read the second column table,
 * s = ((y >> 1) ^ (y >> 2)) & 1. No part carries into another, and the
 * block number's wrap at 16384 blocks is the 4 MiB wrap of the sum.
 *
 * The tables are the functions above sampled along one column (row[bw][y] =
 * the address of (0, y) at block 0) and one row (col[s][x] = the address of
 * (x, y_s) less that of (0, y_s), y_0 = 0 and y_1 = 2), so they define no
 * layout of their own; tests/gs_swizzle_test.c proves that the sum equals
 * em_gs_addr32 / em_gs_addr8 / em_gs_addr4 at every x, y and width (with
 * every base block), and test_gs_raster_reference.py part A proves the
 * CT32 and Z32 tables against the measured swizzle maps. Each EmGs builds
 * its own (EmGs.swz, heap): the column tables on its first draw, a width's
 * row table on its first use. Unsigned arithmetic: a column entry may wrap
 * below zero; the sum is taken modulo 2^32, a multiple of the 4 MiB. */
enum { SWZ_CT32, SWZ_Z32, SWZ_T8, SWZ_T4, SWZ_N };
typedef struct {
    uint32_t col[SWZ_N][2][2048];          /* [layout][s][x]                 */
    uint32_t *row[SWZ_N][64];              /* [layout][bw][y], 2048 entries  */
} Swz;

static uint32_t swz_ref(unsigned layout, uint32_t bw, uint32_t x, uint32_t y)
{
    switch (layout) {
    case SWZ_CT32: return addr32_(0, bw, x, y, 0);
    case SWZ_Z32: return addr32_(0, bw, x, y, 1);
    case SWZ_T8: return addr8_(0, bw, x, y);
    default: return addr4_(0, bw, x, y);
    }
}

static Swz *swz_get(EmGs *gs)
{
    if (gs->swz)
        return gs->swz;
    Swz *t = malloc(sizeof *t);
    if (!t)
        return NULL;
    for (unsigned l = 0; l < SWZ_N; l++) {
        for (unsigned s = 0; s < 2; s++) {
            const uint32_t y = s ? 2 : 0, o = swz_ref(l, 0, 0, y);
            for (uint32_t x = 0; x < 2048; x++)
                t->col[l][s][x] = swz_ref(l, 0, x, y) - o;
        }
        for (unsigned bw = 0; bw < 64; bw++)
            t->row[l][bw] = NULL;
    }
    gs->swz = t;
    return t;
}

/* The row table of a layout and width (bw: 6 bits), or NULL without memory. */
static const uint32_t *swz_row(EmGs *gs, unsigned layout, uint32_t bw)
{
    Swz *t = swz_get(gs);
    if (!t)
        return NULL;
    uint32_t **slot = &t->row[layout][bw & 63];
    if (!*slot) {
        uint32_t *r = malloc(2048 * sizeof *r);
        if (!r)
            return NULL;
        for (uint32_t y = 0; y < 2048; y++)
            r[y] = swz_ref(layout, bw & 63, 0, y);
        *slot = r;
    }
    return *slot;
}

static void swz_free(EmGs *gs)
{
    Swz *t = gs->swz;
    if (!t)
        return;
    for (unsigned l = 0; l < SWZ_N; l++)
        for (unsigned bw = 0; bw < 64; bw++)
            free(t->row[l][bw]);
    free(t);
    gs->swz = NULL;
}

/* CT32 / Z32 layouts (word address) and T8 / T4 (byte / nibble address). */
static inline uint32_t swz32(const uint32_t *row, const uint32_t *col, uint32_t base, uint32_t x, uint32_t y)
{
    return base + row[y & 2047] + col[x & 2047];
}
static inline uint32_t swzt(const uint32_t *row, const uint32_t *col, uint32_t base, uint32_t x, uint32_t y)
{
    y &= 2047;
    return base + row[y] + col[(((y >> 1) ^ (y >> 2)) & 1) * 2048 + (x & 2047)];
}

uint32_t em_gs_swz_addr(EmGs *gs, unsigned psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    unsigned l;
    uint32_t unit, mask;
    switch (psm) {
    case EM_GS_PSMCT32: case EM_GS_PSMCT24: l = SWZ_CT32; unit = 64; mask = (EM_GS_MEM_BYTES / 4) - 1; break;
    case EM_GS_PSMZ32: case EM_GS_PSMZ24: l = SWZ_Z32; unit = 64; mask = (EM_GS_MEM_BYTES / 4) - 1; break;
    case EM_GS_PSMT8: l = SWZ_T8; unit = 256; mask = EM_GS_MEM_BYTES - 1; break;
    case EM_GS_PSMT4: l = SWZ_T4; unit = 512; mask = EM_GS_MEM_BYTES * 2 - 1; break;
    default: return UINT32_MAX;
    }
    const uint32_t *row = swz_row(gs, l, bw);
    if (!row)
        return UINT32_MAX;
    const uint32_t *col = ((Swz *)gs->swz)->col[l][0];
    const uint32_t a = l <= SWZ_Z32 ? swz32(row, col, bp * unit, x, y) : swzt(row, col, bp * unit, x, y);
    return a & mask;
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

static void write_pixel(EmGs *gs, uint32_t bp, uint32_t bw, unsigned psm, uint32_t x, uint32_t y, uint32_t v)
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

void em_gs_write_pixel(EmGs *gs, uint32_t bp, uint32_t bw, unsigned psm, uint32_t x, uint32_t y, uint32_t v)
{
    write_pixel(gs, bp, bw, psm, x, y, v);
    tc_written_rect(gs, psm, bp, bw, x & 2047, y & 2047, 1, 1);
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
    if (gs->list && list_clut_drawn(gs, cpsm, cbp, t8)) {
        /* parse once (section 12): memory a span of the list draws, and no
         * drain (em_gs_list_drain) that draws the list so far first: the
         * drawing would decide what the load reads */
        refuse(gs, EM_GS_REFUSE_ORDER, "a CLUT load from memory the parsed list draws");
        return;
    }
    unsigned n = t8 ? 256 : 16;
    const uint32_t *crow = NULL;           /* CT32, width 1 (section 1b) */
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
            if (!crow && !(crow = swz_row(gs, SWZ_CT32, 1))) {
                refuse(gs, EM_GS_REFUSE_MEMORY, "no memory for the address tables");
                return;
            }
            uint32_t c = rd32(gs, swz32(crow, ((const Swz *)gs->swz)->col[SWZ_CT32][0], cbp * 64, x, y));
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
    if (changed) {
        gs->clut_changes++;            /* section 12: the list's CLUT snapshots */
        tc_clut_changed(gs);           /* section 4b: the palettes are compared again */
    }
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
    if (gs->list) {
        /* parse once (section 12): the parsing EmGs writes no memory */
        refuse(gs, EM_GS_REFUSE_ORDER, "a transfer in a parsed list");
        return;
    }
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
                write_pixel(gs, dbp, dbw, dpsm, dx + x, dy + y, em_gs_read_pixel(gs, sbp, sbw, spsm, sx + x, sy + y));
        tc_written_rect(gs, dpsm, dbp, dbw, dx, dy, w, h);
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
    write_pixel(gs, dbp, dbw, dpsm, dx + gs->trx_x, dy + gs->trx_y, v);
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
        case 32: if (i + 4 > bytes) goto done; memcpy(&v, p + i, 4); i += 4; break;
        case 24: if (i + 3 > bytes) goto done; v = p[i] | (uint32_t)p[i + 1] << 8 | (uint32_t)p[i + 2] << 16; i += 3; break;
        case 16: if (i + 2 > bytes) goto done; v = p[i] | (uint32_t)p[i + 1] << 8; i += 2; break;
        case 8: v = p[i++]; break;
        default:
            v = gs->trx_nibble ? (p[i] >> 4) : (p[i] & 15);
            if (gs->trx_nibble) i++;
            gs->trx_nibble ^= 1;
            break;
        }
        trx_pixel(gs, v);
    }
done:
    /* the decoded textures over the transfer's rectangle (section 4b) */
    tc_written_rect(gs, dpsm, (uint32_t)bits(gs->bitbltbuf, 32, 14), (uint32_t)bits(gs->bitbltbuf, 48, 6),
                    (uint32_t)bits(gs->trxpos, 32, 11), (uint32_t)bits(gs->trxpos, 48, 11),
                    (uint32_t)bits(gs->trxreg, 0, 12), (uint32_t)bits(gs->trxreg, 32, 12));
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
    if (reg != EM_GS_RGBAQ && reg != EM_GS_ST && reg != EM_GS_UV && reg != EM_GS_FOG && reg != EM_GS_XYZF2 &&
        reg != EM_GS_XYZ2 && reg != EM_GS_XYZF3 && reg != EM_GS_XYZ3 &&
        !(reg == EM_GS_PRIM && (v & 0x7FF) == gs->prim))
        gs->setup_valid = 0;               /* draw_setup's inputs may change */
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
    if (gs->tee) em_gs_write(gs->tee, reg, v);
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
            for (EmGs *t = gs->tee; t; t = t->tee) trx_data(t, p + off, need);
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
    /* the address tables (section 1b) of the frame (CT32 / CT24), Z (Z32 /
     * Z24) and texture (CT32, T8, T4); draw_one binds them, NULL otherwise */
    const uint32_t *frow, *fcol, *zrow, *zcol, *trow, *tcol;
    uint32_t fbase, zbase, tbase;
    /* the decoded copy of the texture (section 4b) draw_one binds; NULL:
     * the texels are read from memory */
    struct TcEntry *tc;
} Draw;

/* Wrap (section 4.3), on the integer texel index of each axis. */
static inline int wrap_u(const Draw *d, int u)
{
    switch (d->wms) {
    case 0: return u & ((1 << d->tw) - 1);
    case 1: return clampi(u, 0, (1 << d->tw) - 1);
    case 2: return clampi(u, d->minu, d->maxu);
    default: return (u & d->minu) | d->maxu;
    }
}
static inline int wrap_v(const Draw *d, int v)
{
    switch (d->wmt) {
    case 0: return v & ((1 << d->th) - 1);
    case 1: return clampi(v, 0, (1 << d->th) - 1);
    case 2: return clampi(v, d->minv, d->maxv);
    default: return (v & d->minv) | d->maxv;
    }
}

/* The texel at wrapped indices (u, v), as RGBA. */
static uint32_t fetch_rgba(const EmGs *gs, const Draw *d, int u, int v)
{
    uint32_t c, idx;
    switch (d->tpsm) {
    case EM_GS_PSMCT32: return rd32(gs, swz32(d->trow, d->tcol, d->tbase, (uint32_t)u, (uint32_t)v));
    case EM_GS_PSMT8:
        /* em_gs_read_pixel's T8 and T4 reads, inline, through the tables */
        idx = gs->mem[swzt(d->trow, d->tcol, d->tbase, (uint32_t)u, (uint32_t)v) & (EM_GS_MEM_BYTES - 1)];
        goto clut;
    case EM_GS_PSMT4: {
        const uint32_t n = swzt(d->trow, d->tcol, d->tbase, (uint32_t)u, (uint32_t)v);
        idx = (gs->mem[(n >> 1) & (EM_GS_MEM_BYTES - 1)] >> ((n & 1) * 4)) & 15;
        goto clut;
    }
    case EM_GS_PSMCT24:
        c = rd32(gs, em_gs_addr32(d->tbp, d->tbw, (uint32_t)u, (uint32_t)v, 0)) & 0xFFFFFF;
        return c | ((d->aem && c == 0) ? 0 : d->ta0 << 24);
    case EM_GS_PSMCT16: case EM_GS_PSMCT16S:
        c = rd16(gs, em_gs_addr16(d->tbp, d->tbw, (uint32_t)u, (uint32_t)v, d->tpsm));
        goto expand16;
    case EM_GS_PSMT8H: case EM_GS_PSMT4HL: case EM_GS_PSMT4HH:
        idx = em_gs_read_pixel(gs, d->tbp, d->tbw, d->tpsm, (uint32_t)u, (uint32_t)v);
    clut:
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

/* ------------------------------------------------------------------------
 * 4b. The decoded texture cache (docs/PERFORMANCE.md M3; GS_EXACT.md 4.8).
 *
 * A texture the first level samples (PSMCT32, and PSMT8 / PSMT4 with a CT32
 * CLUT) is kept as a linear 32-bit copy: texel (u, v) of the rectangle
 * [ulo, uhi] x [vlo, vhi] that its wrap modes can reach is fetch_rgba's
 * value for (u, v), palette applied. sample() and texel_rgba() then read the
 * copy at the indices they wrapped, instead of the swizzled memory and the
 * CLUT. A row is decoded on its first read, by fetch_rgba's reads for these
 * formats (tc_decode_row), so a decoded texel is the texel memory gives as
 * long as memory and the CLUT still hold what they held when its row was
 * decoded. The cache keeps that true:
 *
 *  - The key is the texture (TBP0, TBW, PSM; CSA for T4), the rectangle and
 *    the palette the copy applied (T8: the 256 colours, T4: the CSA's 16,
 *    compared in full, not only by hash). A CLUT load that changes the CLUT
 *    selects another copy; TEXA, TCC and TFX are not read by these formats.
 *  - Writes to memory: every copy lists the 8 KiB pages its texels lie in.
 *    A span (section 3.7) that draws into a page (its frame, and its Z
 *    buffer when Z is written) stamps the page with the span's number before
 *    any of its primitives draws, and a copy whose pages were stamped at or
 *    after the span its rows date from is out of date: its rows are decoded
 *    again. In the span that draws into its pages a texture is read from
 *    memory (EmGs.tcs.uncached_written), so a primitive that samples its own
 *    frame or Z buffer, and one that samples what an earlier primitive of
 *    its span drew, read memory as before. Rows decoded in one span stay
 *    good in a later one only if no span between wrote their pages, so the
 *    row bands (EmGs.band_*: each EmGs draws its rows of every primitive)
 *    agree: a band ahead of another by the current span has drawn nothing
 *    into a copy the other still uses. em_gs_world's barriers order its
 *    threads the same way (a read of a drawn block waits for every band's
 *    draws, and a draw into a block read since the last barrier waits for
 *    the reads); the copy reads only texels of the reach the recorder
 *    orders (em_gs_world.c texture_entry: the rectangle is that reach).
 *  - Transfers (HOST -> LOCAL data, LOCAL -> LOCAL), em_gs_write_pixel and
 *    em_gs_memory_written (another model's writes) drop the copies that
 *    read the pages they wrote.
 *  - A texture whose rectangle the wrap can leave (REGION_CLAMP with MINU >
 *    MAXU, or bounds past the texture; REGION_REPEAT past it) or whose copy
 *    would not fit is read from memory.
 *
 * Each EmGs holds its own (EmGs.tc, heap): up to TC_MAX copies (a full
 * table is emptied) and TC_BUDGET bytes of texels, the least recently used
 * copy's texels dropped first. No global state. The first level holds at
 * most 6.25 MiB per EmGs in AREA11 and 10.2 MiB on the route through
 * AREA01, in at most 570 copies, and never fills either limit
 * (PERFORMANCE.md P13). */
#define TC_MAX 1024u
#define TC_SLOTS 4096u                 /* the copies' hash (a power of two, 4 x TC_MAX) */
#define TC_BUDGET (16u << 20)          /* decoded bytes per EmGs (P13 sizes it)          */
#define TC_PAGES 512u                  /* 8 KiB pages of local memory                    */
#define TC_SPAN_W 8u                   /* page ranges one span may write, kept exactly   */

typedef struct { uint32_t pg0, npg; } PgSpan;     /* pages pg0 .. pg0 + npg - 1, wrapping */

struct Tc;
typedef struct TcEntry {
    struct Tc *own;
    uint32_t tbp, tbw, psm, csa;       /* the texture (csa: T4 only, else 0)         */
    int ulo, uhi, vlo, vhi;            /* the rectangle                               */
    uint64_t pal_hash;                 /* T8 / T4: the palette's hash; 0 for CT32     */
    uint32_t pal[256];                 /* the palette applied (npal colours)          */
    uint32_t npal, pal_ver;            /* Tc.clut_ver at which pal was last equal     */
    PgSpan pages;                      /* the pages its texels lie in                 */
    uint64_t valid_from;               /* the span its rows date from                 */
    uint64_t checked;                  /* the span in which it was last found good    */
    int valid;
    uint32_t w, h;
    uint32_t *dec;                     /* w * h texels, then h row flags; NULL: none  */
    uint8_t *rowok;
    uint64_t last_use;
} TcEntry;

typedef struct Tc {
    TcEntry *e;                        /* TC_MAX                                      */
    uint32_t n;
    uint16_t slot[TC_SLOTS];           /* index + 1, 0 empty (linear probing)         */
    uint64_t sigma;                    /* the span being drawn, from 1                */
    uint64_t pstamp[TC_PAGES];         /* the last span that wrote a page a copy reads */
    uint16_t pref[TC_PAGES];           /* copies that read a page                     */
    uint64_t refmap[TC_PAGES / 64];    /* the pages pref counts (a bit each)          */
    PgSpan w[TC_SPAN_W];               /* the pages the current span writes           */
    uint32_t nw;
    int w_all;                         /* ... more ranges than TC_SPAN_W: all memory  */
    uint32_t clut_ver;                 /* bumped when the CLUT buffer changes         */
    uint32_t pal[256], pal_at;         /* the CLUT as CT32 colours at clut_ver pal_at */
    int pal_have;
    uint64_t hash8, hash4[16];
    uint32_t hash8_at, hash4_at[16];
    int hash8_have;
    uint16_t hash4_have;
    uint64_t m_tex0, m_clamp;          /* the last bind: its TEX0 / CLAMP fields read, */
    uint32_t m_clut, m_ok;             /* the CLUT version and whether it is held     */
    TcEntry *m_e;
    uint64_t use;
    uint64_t rows, texels;             /* decoded (EmGs.tcs)                          */
    /* the queued span's FRAME, ZBUF, TEST and SCISSOR (queue_prim), and
     * whether its primitives differ in them (tc_span then takes each) */
    uint64_t q_reg[4];
    int q_valid, q_mixed;
} Tc;

static int pg_overlap(PgSpan a, PgSpan b)
{
    return ((b.pg0 - a.pg0) & (TC_PAGES - 1)) < a.npg || ((a.pg0 - b.pg0) & (TC_PAGES - 1)) < b.npg;
}

/* The pages the pixels (x, y), x0 <= x <= x1, y0 <= y <= y1 (0..2047), of a
 * buffer or texture lie in: every layout's page index is (y / page height) *
 * pages a row + x / page width, rising in x and in y, and its blocks are bp +
 * page * 32 + 0..31 (section 2, the address functions above). 0: none. */
static int pg_span(unsigned psm, uint32_t bp, uint32_t bw, int x0, int x1, int y0, int y1, PgSpan *o)
{
    if (x0 > x1 || y0 > y1)
        return 0;
    uint32_t pw, ph, ppr = bw;
    switch (psm) {
    case EM_GS_PSMCT32: case EM_GS_PSMCT24: case EM_GS_PSMZ32: case EM_GS_PSMZ24:
    case EM_GS_PSMT8H: case EM_GS_PSMT4HL: case EM_GS_PSMT4HH: pw = 64; ph = 32; break;
    case EM_GS_PSMCT16: case EM_GS_PSMCT16S: case EM_GS_PSMZ16: case EM_GS_PSMZ16S: pw = 64; ph = 64; break;
    case EM_GS_PSMT8: pw = 128; ph = 64; ppr = bw >> 1 ? bw >> 1 : 1; break;
    case EM_GS_PSMT4: pw = 128; ph = 128; ppr = bw >> 1 ? bw >> 1 : 1; break;
    default: o->pg0 = 0; o->npg = TC_PAGES; return 1;
    }
    if (x0 < 0 || y0 < 0 || x1 > 2047 || y1 > 2047) {       /* the address wraps: all of memory */
        o->pg0 = 0;
        o->npg = TC_PAGES;
        return 1;
    }
    const uint64_t pmin = (uint64_t)((uint32_t)y0 / ph) * ppr + (uint32_t)x0 / pw;
    const uint64_t pmax = (uint64_t)((uint32_t)y1 / ph) * ppr + (uint32_t)x1 / pw;
    const uint64_t b0 = bp + pmin * 32, b1 = bp + pmax * 32 + 31;
    const uint64_t n = (b1 >> 5) - (b0 >> 5) + 1;
    o->pg0 = (uint32_t)(b0 >> 5) & (TC_PAGES - 1);
    o->npg = n >= TC_PAGES ? TC_PAGES : (uint32_t)n;
    return 1;
}

static void tc_drop_storage(EmGs *gs, Tc *t, TcEntry *e)
{
    (void)t;
    if (!e->dec)
        return;
    gs->tcs.bytes -= (uint64_t)e->w * e->h * 4u + e->h;
    free(e->dec);
    e->dec = NULL;
    e->rowok = NULL;
    e->valid = 0;
}

/* Every copy dropped (the table full, or released). */
static void tc_reset(EmGs *gs, Tc *t)
{
    for (uint32_t i = 0; i < t->n; i++)
        tc_drop_storage(gs, t, &t->e[i]);
    t->n = 0;
    memset(t->slot, 0, sizeof t->slot);
    memset(t->pref, 0, sizeof t->pref);
    memset(t->refmap, 0, sizeof t->refmap);
    t->m_ok = 0;
    t->m_e = NULL;
    gs->tcs.resets++;
}

static Tc *tc_get(EmGs *gs)
{
    if (gs->tc || gs->tc_off)
        return gs->tc;
    Tc *t = calloc(1, sizeof *t);
    TcEntry *e = t ? calloc(TC_MAX, sizeof *e) : NULL;
    if (!e) {
        free(t);
        gs->tc_off = 1;                /* no memory: every texel from memory */
        return NULL;
    }
    t->e = e;
    gs->tc = t;
    return t;
}

static void tc_free(EmGs *gs)
{
    Tc *t = gs->tc;
    if (!t)
        return;
    for (uint32_t i = 0; i < t->n; i++)
        free(t->e[i].dec);
    free(t->e);
    free(t);
    gs->tc = NULL;
    gs->tcs.bytes = 0;
}

/* The CLUT buffer changed (clut_load). */
static void tc_clut_changed(EmGs *gs)
{
    if (gs->tc)
        ((Tc *)gs->tc)->clut_ver++;
}

/* The copies that read pages of `s` are out of date. */
static void tc_written(EmGs *gs, PgSpan s)
{
    Tc *t = gs->tc;
    if (!t)
        return;
    for (uint32_t i = 0; i < t->n; i++)
        if (t->e[i].valid && pg_overlap(t->e[i].pages, s))
            t->e[i].valid = 0;
}

static void tc_written_rect(EmGs *gs, unsigned psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h)
{
    PgSpan s;
    if (!gs->tc || w == 0 || h == 0)
        return;
    if (pg_span(psm, bp, bw, (int)x, (int)(x + w - 1), (int)y, (int)(y + h - 1), &s))
        tc_written(gs, s);
}

void em_gs_memory_written(EmGs *gs, uint32_t first_block, uint32_t blocks)
{
    if (!gs || !gs->tc || blocks == 0)
        return;
    PgSpan s;
    const uint64_t b0 = first_block, b1 = (uint64_t)first_block + blocks - 1;
    const uint64_t n = (b1 >> 5) - (b0 >> 5) + 1;
    s.pg0 = (uint32_t)(b0 >> 5) & (TC_PAGES - 1);
    s.npg = blocks >= 16384u || n >= TC_PAGES ? TC_PAGES : (uint32_t)n;
    tc_written(gs, s);
}

/* A page range a span writes (dropping repeats): w[0 .. *nw), or *w_all
 * once more than TC_SPAN_W ranges were given. */
static int pg_add(PgSpan *w, uint32_t *nw, int *w_all, PgSpan s)
{
    for (uint32_t i = 0; i < *nw; i++)
        if (w[i].pg0 == s.pg0 && w[i].npg == s.npg)
            return 0;
    if (*nw == TC_SPAN_W) {
        *w_all = 1;
        return 0;
    }
    w[(*nw)++] = s;
    return 1;
}

/* The copy still holds what memory holds for the texels it decoded. */
static int tc_fresh(Tc *t, TcEntry *e)
{
    if (!e->valid)
        return 0;
    if (e->checked == t->sigma)
        return 1;
    for (uint32_t k = 0; k < e->pages.npg; k++)
        if (t->pstamp[(e->pages.pg0 + k) & (TC_PAGES - 1)] >= e->valid_from) {
            e->valid = 0;
            return 0;
        }
    for (uint32_t i = 0; i < t->nw; i++)
        if (pg_overlap(t->w[i], e->pages)) {
            e->valid = 0;
            return 0;
        }
    if (t->w_all) {
        e->valid = 0;
        return 0;
    }
    e->checked = t->sigma;
    return 1;
}

static uint64_t tc_mix(uint64_t h, uint64_t v)
{
    h ^= v + UINT64_C(0x9E3779B97F4A7C15) + (h << 6) + (h >> 2);
    return h * UINT64_C(0xBF58476D1CE4E5B9);
}

/* The CLUT as CT32 colours (fetch_rgba's CT32 CLUT read: low halfword at
 * clut[e], high at clut[e + 256]), at the current CLUT version. */
static void tc_palette(const EmGs *gs, Tc *t)
{
    if (t->pal_have && t->pal_at == t->clut_ver)
        return;
    for (unsigned i = 0; i < 256; i++)
        t->pal[i] = gs->clut[i] | (uint32_t)gs->clut[i + 256] << 16;
    t->pal_at = t->clut_ver;
    t->pal_have = 1;
    t->hash8_have = 0;
    t->hash4_have = 0;
}

static uint64_t tc_pal_hash(const uint32_t *pal, unsigned n)
{
    uint64_t h = n;
    for (unsigned i = 0; i < n; i++)
        h = tc_mix(h, pal[i]);
    return h;
}

/* The rectangle a wrap mode lets an axis reach (wrap_u / wrap_v's values),
 * inside the texture: 0 when it can leave it. */
static int tc_axis(int mode, uint32_t size_log2, int mn, int mx, int *lo, int *hi)
{
    const int size = 1 << (size_log2 > 10 ? 10 : size_log2);
    if (size_log2 > 10)
        return 0;
    switch (mode) {
    case 0: case 1: *lo = 0; *hi = size - 1; return 1;
    case 2: if (mn > mx || mx > size - 1) return 0; *lo = mn; *hi = mx; return 1;
    default: if ((mn | mx) > size - 1) return 0; *lo = mx; *hi = mn | mx; return 1;
    }
}

static TcEntry *tc_bind(EmGs *gs, const Draw *d)
{
    Tc *t = gs->tc;
    if (!t)
        return NULL;
    const int t8 = d->tpsm == EM_GS_PSMT8, t4 = d->tpsm == EM_GS_PSMT4;
    if (!(d->tpsm == EM_GS_PSMCT32 || ((t8 || t4) && d->cpsm == EM_GS_PSMCT32)))
        return NULL;
    /* the last bind's texture again (the fields read below, the CLUT) */
    const uint64_t tex0 = d->c->tex0 & (UINT64_C(0x3FFFFFFFF) | UINT64_C(0xF) << 51 | UINT64_C(0x1F) << 56);
    const uint64_t clamp = d->c->clamp & ((UINT64_C(1) << 44) - 1);
    if (t->m_ok && t->m_tex0 == tex0 && t->m_clamp == clamp && (!(t8 || t4) || t->m_clut == t->clut_ver)) {
        TcEntry *e = t->m_e;
        if (!e)
            return NULL;               /* (the rectangle can leave the texture) */
        if (e->dec && tc_fresh(t, e)) {
            e->last_use = ++t->use;
            return e;
        }
    }
    t->m_ok = 1;
    t->m_tex0 = tex0;
    t->m_clamp = clamp;
    t->m_clut = t->clut_ver;
    t->m_e = NULL;
    int ulo, uhi, vlo, vhi;
    if (!tc_axis(d->wms, d->tw, d->minu, d->maxu, &ulo, &uhi) || !tc_axis(d->wmt, d->th, d->minv, d->maxv, &vlo, &vhi))
        return NULL;
    const uint32_t csa = t4 ? (d->csa & 15) : 0;
    const uint32_t *pal = NULL;
    uint32_t npal = 0;
    uint64_t ph = 0;
    if (t8 || t4) {
        tc_palette(gs, t);
        if (t8) {
            if (!t->hash8_have || t->hash8_at != t->pal_at) {
                t->hash8 = tc_pal_hash(t->pal, 256);
                t->hash8_at = t->pal_at;
                t->hash8_have = 1;
            }
            pal = t->pal;
            npal = 256;
            ph = t->hash8;
        } else {
            if (!((t->hash4_have >> csa) & 1) || t->hash4_at[csa] != t->pal_at) {
                t->hash4[csa] = tc_pal_hash(t->pal + csa * 16, 16);
                t->hash4_at[csa] = t->pal_at;
                t->hash4_have |= (uint16_t)(1u << csa);
            }
            pal = t->pal + csa * 16;
            npal = 16;
            ph = t->hash4[csa];
        }
    }
    uint64_t h = tc_mix(tc_mix(tc_mix(d->tbp, d->tbw), d->tpsm | (uint64_t)csa << 8), ph);
    h = tc_mix(h, (uint64_t)ulo | (uint64_t)uhi << 16 | (uint64_t)vlo << 32 | (uint64_t)vhi << 48);
    uint32_t s = (uint32_t)(h >> 52) & (TC_SLOTS - 1);
    TcEntry *e = NULL;
    for (;; s = (s + 1) & (TC_SLOTS - 1)) {
        const uint32_t k = t->slot[s];
        if (!k)
            break;
        TcEntry *c = &t->e[k - 1];
        if (c->tbp != d->tbp || c->tbw != d->tbw || c->psm != d->tpsm || c->csa != csa || c->ulo != ulo ||
            c->uhi != uhi || c->vlo != vlo || c->vhi != vhi || c->pal_hash != ph)
            continue;
        if (npal && c->pal_ver != t->pal_at) {
            if (memcmp(c->pal, pal, npal * sizeof *pal) != 0)
                continue;              /* the same hash, another palette */
            c->pal_ver = t->pal_at;
        }
        e = c;
        break;
    }
    const uint32_t w = (uint32_t)(uhi - ulo + 1), hh = (uint32_t)(vhi - vlo + 1);
    const uint64_t need = (uint64_t)w * hh * 4u + hh;
    if (need > TC_BUDGET / 2)
        return NULL;                   /* (m_e NULL: from memory while this texture lasts) */
    if (!e) {
        if (t->n == TC_MAX) {
            tc_reset(gs, t);
            t->m_ok = 1;
            t->m_tex0 = tex0;
            t->m_clamp = clamp;
            t->m_clut = t->clut_ver;
            for (s = (uint32_t)(h >> 52) & (TC_SLOTS - 1); t->slot[s]; s = (s + 1) & (TC_SLOTS - 1)) {}
        }
        e = &t->e[t->n++];
        memset(e, 0, sizeof *e);
        e->own = t;
        e->tbp = d->tbp; e->tbw = d->tbw; e->psm = d->tpsm; e->csa = csa;
        e->ulo = ulo; e->uhi = uhi; e->vlo = vlo; e->vhi = vhi;
        e->pal_hash = ph;
        e->npal = npal;
        if (npal) {
            memcpy(e->pal, pal, npal * sizeof *pal);
            e->pal_ver = t->pal_at;
        }
        e->w = w;
        e->h = hh;
        if (!pg_span(d->tpsm, d->tbp, d->tbw, ulo, uhi, vlo, vhi, &e->pages)) {
            e->pages.pg0 = 0;
            e->pages.npg = TC_PAGES;
        }
        for (uint32_t k = 0; k < e->pages.npg; k++) {
            const uint32_t pg = (e->pages.pg0 + k) & (TC_PAGES - 1);
            t->pref[pg]++;
            t->refmap[pg >> 6] |= UINT64_C(1) << (pg & 63);
        }
        t->slot[s] = (uint16_t)t->n;
        gs->tcs.created++;
    } else if (e->dec && tc_fresh(t, e)) {
        t->m_e = e;
        e->last_use = ++t->use;
        return e;
    } else if (e->dec || e->valid_from) {
        gs->tcs.renewed++;
    }
    t->m_e = e;
    e->last_use = ++t->use;
    /* rows from this span on (none decoded yet) */
    if (!e->dec) {
        while (gs->tcs.bytes + need > TC_BUDGET) {
            TcEntry *old = NULL;
            for (uint32_t i = 0; i < t->n; i++)
                if (t->e[i].dec && &t->e[i] != e && (!old || t->e[i].last_use < old->last_use))
                    old = &t->e[i];
            if (!old)
                break;
            tc_drop_storage(gs, t, old);
            gs->tcs.evicted++;
        }
        if (gs->tcs.bytes + need > TC_BUDGET)
            return NULL;
        e->dec = malloc((size_t)need);
        if (!e->dec)
            return NULL;
        e->rowok = (uint8_t *)(e->dec + (size_t)w * hh);
        gs->tcs.bytes += need;
    }
    memset(e->rowok, 0, hh);
    e->valid = 1;
    e->valid_from = t->sigma;
    e->checked = 0;
    if (!tc_fresh(t, e)) {
        gs->tcs.uncached_written++;
        return NULL;                   /* drawn in this span: from memory */
    }
    return e;
}

/* Row v of the bound copy, decoded on its first read: fetch_rgba's texels
 * for these formats, its CT32, T8 and T4 reads written out per format (the
 * same address tables, the same arithmetic; the palette is the CLUT's CT32
 * colours, tc_bind found it equal). */
__attribute__((noinline)) static void tc_decode_row(const EmGs *gs, const Draw *d, TcEntry *e, int v)
{
    uint32_t *out = e->dec + (size_t)(v - e->vlo) * e->w;
    const uint32_t y = (uint32_t)v & 2047, n = e->w, u0 = (uint32_t)e->ulo;
    const uint32_t rb = d->tbase + d->trow[y];
    switch (d->tpsm) {
    case EM_GS_PSMCT32:
        for (uint32_t i = 0; i < n; i++)
            out[i] = rd32(gs, rb + d->tcol[(u0 + i) & 2047]);
        break;
    case EM_GS_PSMT8: {
        const uint32_t *col = d->tcol + (((y >> 1) ^ (y >> 2)) & 1) * 2048;
        for (uint32_t i = 0; i < n; i++)
            out[i] = e->pal[gs->mem[(rb + col[(u0 + i) & 2047]) & (EM_GS_MEM_BYTES - 1)]];
        break;
    }
    default: {                         /* PSMT4 */
        const uint32_t *col = d->tcol + (((y >> 1) ^ (y >> 2)) & 1) * 2048;
        for (uint32_t i = 0; i < n; i++) {
            const uint32_t a = rb + col[(u0 + i) & 2047];
            out[i] = e->pal[(gs->mem[(a >> 1) & (EM_GS_MEM_BYTES - 1)] >> ((a & 1) * 4)) & 15];
        }
        break;
    }
    }
    e->rowok[v - e->vlo] = 1;
    e->own->rows++;
    e->own->texels += e->w;
}

__attribute__((always_inline)) static inline const uint32_t *tc_row(const EmGs *gs, const Draw *d, int v)
{
    TcEntry *e = d->tc;
    const uint32_t r = (uint32_t)(v - e->vlo);
    if (!e->rowok[r])
        tc_decode_row(gs, d, e, v);
    return e->dec + (size_t)r * e->w;
}

static uint32_t texel_rgba(const EmGs *gs, const Draw *d, int u, int v)
{
    const int uw = wrap_u(d, u), vw = wrap_v(d, v);
    if (d->tc)
        return tc_row(gs, d, vw)[uw - d->tc->ulo];
    return fetch_rgba(gs, d, uw, vw);
}

/* Bilinear / nearest from fixed-point texel coordinates in 1/16 texel
 * (U16 = floor(u * 16)); section 4.2. */
static uint32_t sample(const EmGs *gs, const Draw *d, int64_t U16, int64_t V16, int bilinear)
{
    if (!bilinear)
        return texel_rgba(gs, d, (int)(U16 >> 4), (int)(V16 >> 4));
    int64_t Ub = U16 - 8, Vb = V16 - 8;
    int iu = (int)(Ub >> 4), iv = (int)(Vb >> 4), fu = (int)(Ub & 15), fv = (int)(Vb & 15);
    /* each axis wrapped once (the same indices texel_rgba wraps) */
    const int u0 = wrap_u(d, iu), u1 = wrap_u(d, iu + 1), v0 = wrap_v(d, iv), v1 = wrap_v(d, iv + 1);
    uint32_t t00, t10, t01, t11;
    if (d->tc) {
        /* the decoded copy (section 4b): the same texels at the same indices */
        const uint32_t *r0 = tc_row(gs, d, v0), *r1 = tc_row(gs, d, v1);
        const int a = u0 - d->tc->ulo, b = u1 - d->tc->ulo;
        t00 = r0[a]; t10 = r0[b]; t01 = r1[a]; t11 = r1[b];
    } else {
        t00 = fetch_rgba(gs, d, u0, v0); t10 = fetch_rgba(gs, d, u1, v0);
        t01 = fetch_rgba(gs, d, u0, v1); t11 = fetch_rgba(gs, d, u1, v1);
    }
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

/* The row is this EmGs's band's (EmGs.band_rows: a row range; else
 * EmGs.band_count: interleaved bands). */
static inline int own_row(const EmGs *gs, int y)
{
    if (gs->band_rows)
        return y >= gs->band_y0 && y <= gs->band_y1;
    return gs->band_count <= 1 || (((uint32_t)y >> gs->band_shift) % gs->band_count) == gs->band_index;
}

/* No row y0 <= y <= y1 is this EmGs's band's (a primitive drawing only those
 * rows draws nothing here). */
static int band_misses(const EmGs *gs, int y0, int y1)
{
    if (gs->band_rows)
        return y0 > y1 || y1 < gs->band_y0 || y0 > gs->band_y1;
    if (gs->band_count > 1) {
        int any = 0;
        for (int y = y0; y <= y1 && !any; y = ((y >> gs->band_shift) + 1) << gs->band_shift)
            any = own_row(gs, y);
        return !any;
    }
    return 0;
}

/* plot() in two parts (docs/PERFORMANCE.md P12). A pixel is written only
 * when it passes the scissor, SCANMSK, the Z test and DATE (and the alpha
 * test, unless AFAIL lets it through), and every test that fails before the
 * writes returns without writing or counting anything. The Z test and DATE
 * read only Z, the frame word and the pixel's Z, never the texture, and
 * nothing before the writes stores to memory, so testing them first, before
 * the texture is sampled, draws what testing them after the sample draws:
 * the same tests pass, the same words are read and the same values written.
 * The alpha test needs the sampled alpha and stays after the sample; its
 * AFAIL modes change only what a passing pixel writes. plot_dst runs the
 * tests that do not need the source colour; plot_src computes the colour
 * and writes. */
typedef struct {
    uint32_t faddr, fword;   /* the frame word's address and its value */
    uint32_t zaddr, zval;    /* the Z word's address, the pixel's Z clamped to the format */
} Dst;

static inline int plot_dst(EmGs *gs, const Draw *d, int x, int y, const Frag *fr, Dst *o)
{
    if (x < d->sx0 || x > d->sx1 || y < d->sy0 || y > d->sy1)
        return 0;
    /* (rows of other bands never reach plot: triangles and sprites skip
     * them per row, lines and points before the call) */
    if ((d->scanmsk == 2 && !(y & 1)) || (d->scanmsk == 3 && (y & 1)))
        return 0;
    /* Z test */
    uint32_t zaddr = 0;
    uint32_t zval;
    int zmx = zmax_of(d->zpsm);
    if (d->zpsm == EM_GS_PSMZ32)
        zval = (uint32_t)(fr->z > 0xFFFFFFFFull ? 0xFFFFFFFFull : fr->z);
    else
        zval = (uint32_t)(fr->z > (uint64_t)zmx ? (uint64_t)zmx : fr->z);
    int z32 = d->zpsm == EM_GS_PSMZ32 || d->zpsm == EM_GS_PSMZ24;
    /* the address when the test reads Z or the pixel may write it (ZMSK 0;
     * the alpha test can still withdraw the write) */
    if (d->zte && (d->ztst >= 2 || !d->zmsk)) {
        if (z32)
            zaddr = swz32(d->zrow, d->zcol, d->zbase, (uint32_t)x, (uint32_t)y);
        else
            zaddr = em_gs_addr16(d->zbp, d->fbw, (uint32_t)x, (uint32_t)y, d->zpsm);
    }
    if (d->zte) {
        if (d->ztst == 0)
            goto zfail;
        if (d->ztst >= 2) {
            uint32_t zb = z32 ? rd32(gs, zaddr) : rd16(gs, zaddr);
            if (d->zpsm == EM_GS_PSMZ24) zb &= 0xFFFFFF;
            if (d->ztst == 2 ? zval < zb : zval <= zb)
                goto zfail;
        }
    }
    /* frame word */
    uint32_t faddr = 0, fword = 0;
    int f32 = d->fpsm == EM_GS_PSMCT32 || d->fpsm == EM_GS_PSMCT24;
    if (f32) {
        faddr = swz32(d->frow, d->fcol, d->fbase, (uint32_t)x, (uint32_t)y);
        fword = rd32(gs, faddr);
    } else {
        faddr = em_gs_addr16(d->fbp, d->fbw, (uint32_t)x, (uint32_t)y, d->fpsm);
        fword = rd16(gs, faddr);
    }
    if (d->date) {
        int dbit = f32 ? (int)(fword >> 31) : (int)((fword >> 15) & 1);
        if (d->fpsm == EM_GS_PSMCT24)
            dbit = 0;
        if (dbit != d->datm) {
            gs->pix.datefail_tex += (uint64_t)(d->tme && fr->has_tex);
            return 0;
        }
    }
    o->faddr = faddr; o->fword = fword; o->zaddr = zaddr; o->zval = zval;
    return 1;
zfail:
    gs->pix.zfail++;
    if (d->tme && fr->has_tex) {
        gs->pix.zfail_tex++;
        gs->pix.zfail_bil += (uint64_t)d->mmag;
    }
    return 0;
}

static inline void plot_src(EmGs *gs, const Draw *d, int x, int y, const Frag *fr, const Dst *o)
{
    /* The colour and fog weight arrive in 8.7 fixed point; the texture
     * function and fog see that precision (section 5.1). */
    int r, g, b, a;
    if (d->tme && fr->has_tex) {
        int bil = d->mmag;   /* MXL 0: LOD 0 is magnification (section 4.2) */
        uint32_t t = sample(gs, d, fr->U16, fr->V16, bil);
        gs->pix.samples++;
        gs->pix.samples_bil += (uint64_t)bil;
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
    const uint32_t faddr = o->faddr, fword = o->fword, zaddr = o->zaddr, zval = o->zval;
    int f32 = d->fpsm == EM_GS_PSMCT32 || d->fpsm == EM_GS_PSMCT24;
    int z32 = d->zpsm == EM_GS_PSMZ32 || d->zpsm == EM_GS_PSMZ24;
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

static void plot(EmGs *gs, const Draw *d, int x, int y, const Frag *fr)
{
    Dst o;
    if (plot_dst(gs, d, x, y, fr, &o))
        plot_src(gs, d, x, y, fr, &o);
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
 * 6. Sprites (section 3.4)
 *
 * The row coordinate V of a UV sprite (FST 1) is accumulated in binary32,
 * one step a row, from the TOP corner (the one with the lower Y, with its
 * V, whatever the order the corners are given in), starting at the first
 * row drawn (the first covered row, or the scissor's top row when the
 * sprite starts above it); for bilinear sampling the value accumulated is
 * the coordinate the filter reads, V - 0.5 texel (h = 0.5; nearest: h =
 * 0), as the column rule's W. In texels and pixels (V / 16, Y / 16: exact),
 * each operation rounded to binary32:
 *   d   = (vB - vT) / (yB - yT)
 *   v   = (vT - h) + (ys - yT) * d          at ys, the first row drawn
 *   v  += d                                 every next row
 * and the coordinate in 1/16 texel is floor(16 v) (+ 8 for bilinear, which
 * the filter takes off again). Measured (2026-10-20): the depth-of-field
 * captures (decomp build/dof_capture: 256 texel rows over 224 rows, the
 * corners top to bottom, the first row drawn; section 3.4), the status
 * pages' captures, and the designed fork probe probe_rows.py p14_rows
 * (decomp build/status_field_capture: 448 UV sprites, every drawn row equal;
 * the corners given bottom to top, starts above the scissor, the BATTERY
 * page background's live sprites that the rule before left open) and the
 * held-out probe_rows2.py p15_rows_holdout (captured after the rule was
 * written down: random sprites of every kind, bilinear and nearest, both
 * axes at once, and 64 sprites whose start's sum order changes a drawn row:
 * every pixel equal). The rule before (from the corner given first, at the
 * first covered row, V itself accumulated) fits 226 of p14_rows' 448
 * sprites; from the top corner and the first drawn row with V itself 388;
 * a fused start at most 340; fixed point (the column rule's shape, 16- to
 * 24-bit steps, lanes of 1 to 8 rows) at most 223; the start's sum taken as
 * vT + ((ys - yT) d - h) leaves 512 pixels of p15_rows_holdout off.
 *
 * The column coordinate U (measured 2026-10-20: the status pages' fork
 * captures, decomp build/status_field_capture, and four designed probes in
 * the fork, probe_cols.py p10_cols, probe_open.py p11_open, probe_open2.py
 * p12_open and the held-out probe_holdout.py p13_holdout: 2,048 UV sprites
 * of height 1, about 360,000 decoded columns, every one equal;
 * docs/GS_EXACT.md section 3.4) is 16.16 fixed point, W in 2^-16 texel,
 * set up in binary32 from the LEFT corner (the one with the lower X, with
 * its U) whatever the order the corners are given in; for bilinear sampling
 * W is the coordinate the filter reads, U - 0.5 texel (h = 0.5; nearest:
 * h = 0). In texels and pixels (U / 16, X / 16: exact), each operation
 * rounded to binary32:
 *   d   = (uR - uL) / (xR - xL)
 *   W0  = trunc(((uL - h) + (xs - xL) * d) * 2^16)      toward zero, so a
 *         start below half a texel rounds UP
 *   G   = trunc(d * 2^18)                               four columns
 *   L_k = trunc((k * d) * 2^16), k = -3 .. 3            a lane's offset
 * at xs, the first column drawn (the first covered column, or the
 * scissor's left column when the sprite starts left of it). The columns go
 * four at a time, in groups aligned to window x mod 4 (whatever XYOFFSET's
 * X: p13_holdout phase1 at 1025 px and phase_half at 1024.5 px): lane L of
 * the group holding xs (r = xs mod 4) is W0 + L_(L - r), each next group adds
 * G. The 1/16-texel coordinate is floor(W / 2^12) (+ 8 for bilinear, which
 * the filter takes off again). Over many groups this is close to an 18-bit
 * step truncated toward zero (the rule before; it left 19 probe columns
 * open, all within 79 columns of the start); the start, the lanes inside a
 * group and the binary32 step are where it differs (a start in double
 * precision leaves 152 columns of p11_open off, an exact-integer step and
 * lanes 7 of p13_holdout's, a start as (uL + p d) - h 1 and one fused
 * multiply-add 67). The start's sum as uL + (p d - h), which fits those
 * four probes too, leaves 72 pixels of p15_rows_holdout's cols_assoc off
 * (64 sprites found by search where the two orders differ at a drawn
 * column; 2026-10-20): the sum is (uL - h) + p d. */
static inline float uv_texels(uint32_t q4) { return (float)q4 * 0.0625f; }   /* 1/16 texel -> texel: exact */

/* The row rule above for one sprite: the start value v at ys (the first
 * row drawn) and the step d, for corners (q0, Y0), (q1, Y1) as given (V in
 * 1/16 texel, Y in 12.4 window), bilinear or not. Each binary32 operation
 * is its own statement: never contracted into a fused multiply-add. */
static float uv_row_start(uint32_t q0, uint32_t q1, int Y0, int Y1, int ys, int bil, float *step)
{
    const int top = Y0 <= Y1;
    const uint32_t qT = top ? q0 : q1, qB = top ? q1 : q0;
    const int YT = top ? Y0 : Y1, YB = top ? Y1 : Y0;
    const float vT = uv_texels(qT), vB = uv_texels(qB);
    const float yT = (float)YT * 0.0625f, yB = (float)YB * 0.0625f;
    const float span = yB - yT, rise = vB - vT;
    const float d = rise / span;
    const float h = bil ? 0.5f : 0.0f;
    const float pre = (float)ys - yT;            /* exact */
    const float prod = pre * d;
    const float base = vT - h;
    *step = d;
    return base + prod;
}

/* The 1/16-texel V of the accumulated value v. */
static inline int64_t uv_row_v16(float v, int bil)
{
    const float v16 = v * 16.0f;                 /* exact: a power of two */
    return (int64_t)floorf(v16) + (bil ? 8 : 0);
}

/* The column rule above for one sprite. */
typedef struct {
    int64_t w0, step4;
    int64_t lane[7];                 /* L_k, k = -3 .. 3 */
    int xb, r, bil, flat;
} UvCols;

/* The rule's state from xs (the first column drawn) for corners (q0, X0),
 * (q1, X1) as given (U in 1/16 texel, X in 12.4 window), bilinear or not.
 * Each binary32 operation is its own statement: never contracted into a
 * fused multiply-add. */
static void uv_cols_init(UvCols *c, uint32_t q0, uint32_t q1, int X0, int X1, int xs, int bil)
{
    const int left = X0 <= X1;
    const uint32_t qL = left ? q0 : q1, qR = left ? q1 : q0;
    const int XL = left ? X0 : X1, XR = left ? X1 : X0;
    c->bil = bil;
    c->flat = XL == XR;
    c->r = xs & 3;
    c->xb = xs - c->r;
    if (c->flat) {
        c->w0 = (int64_t)qL << 12;
        c->step4 = 0;
        memset(c->lane, 0, sizeof c->lane);
        return;
    }
    const float uL = uv_texels(qL), uR = uv_texels(qR);
    const float xL = (float)XL * 0.0625f, xR = (float)XR * 0.0625f;
    const float span = xR - xL, rise = uR - uL;
    const float d = rise / span;
    const float h = bil ? 0.5f : 0.0f;
    const float pre = (float)xs - xL;            /* exact */
    const float prod = pre * d;
    const float base = uL - h;
    const float s = base + prod;
    const float w0 = s * 65536.0f;               /* exact: a power of two */
    c->w0 = (int64_t)w0;                         /* C: toward zero */
    const float g4 = d * 262144.0f;              /* exact */
    c->step4 = (int64_t)g4;
    for (int k = -3; k <= 3; ++k) {
        const float kd = (float)k * d;
        const float lk = kd * 65536.0f;          /* exact */
        c->lane[k + 3] = (int64_t)lk;
    }
}


/* The 1/16-texel U of window column x (x >= the group start of xs). */
static inline int64_t uv_cols_u16(const UvCols *c, int x)
{
    if (c->flat) return c->w0 >> 12;
    const int off = x - c->xb;
    const int64_t w = c->w0 + c->lane[(off & 3) - c->r + 3] + c->step4 * (int64_t)(off >> 2);
    return floordiv64(w, (int64_t)1 << 12) + (c->bil ? 8 : 0);
}

/* The alpha test's comparison (TEST.ATST against AREF) for a fragment alpha
 * a: 1 when it passes. plot() makes the same comparison inline (kept there
 * as written, so its code is unchanged); the fast path below makes it once
 * per sprite. */
static inline int alpha_pass(const Draw *d, int a)
{
    switch (d->atst) {
    case 0: return 0;
    case 1: return 1;
    case 2: return a < d->aref;
    case 3: return a <= d->aref;
    case 4: return a == d->aref;
    case 5: return a >= d->aref;
    case 6: return a > d->aref;
    default: return a != d->aref;
    }
}

/* A sprite of trivial state (docs/PERFORMANCE.md Q4): no texture, no blend,
 * no DATE, and no Z test that compares (ZTE 0, or ZTST ALWAYS). Its pixels
 * then all take plot()'s path with the same values: the colour (vertex
 * colour, fog with the sprite's one fog weight), the alpha test's outcome
 * (the alpha is one value), the source word and its write mask, and the Z
 * value. This fills the rectangle with those values once computed, pixel by
 * pixel in plot()'s order (rows top to bottom, each left to right; the
 * frame word before the Z word, so frame and Z buffers that share memory
 * are written exactly as plot() writes them) and counts the pixels plot()
 * counts. Every rule here is plot()'s, restated for a value that does not
 * change across the sprite; the conformance tests and a generic-against-
 * fast differential (PERFORMANCE.md, Progress P7) show the same bytes.
 * Returns 0, drawing nothing, for any other state: plot() draws those. The
 * rectangle px0..px1 x py0..py1 is inside the scissor (draw_sprite clips it),
 * so plot()'s scissor test always passes. */
static int sprite_fill(EmGs *gs, const Draw *d, const Frag *fr, int px0, int px1, int py0, int py1)
{
    if (d->tme || d->date || (d->zte && d->ztst != 1))
        return 0;
    /* plot(): the colour without a texture, then fog */
    int r = fr->r >> 7, g = fr->g >> 7, b = fr->b >> 7, a = fr->a >> 7;
    if (d->fog) {
        const int f = fr->f;
        r = d->fogcol[0] + (((r - d->fogcol[0]) * f) >> 15);
        g = d->fogcol[1] + (((g - d->fogcol[1]) * f) >> 15);
        b = d->fogcol[2] + (((b - d->fogcol[2]) * f) >> 15);
    }
    const int f32 = d->fpsm == EM_GS_PSMCT32 || d->fpsm == EM_GS_PSMCT24;
    /* a blend reads the frame; dither (never for sprites) the position */
    if ((d->abe && (!d->pabe || (a & 0x80))) || (d->dthe && !f32 && d->dither))
        return 0;
    /* the alpha test: one outcome for every pixel */
    int write_rgb = 1, write_a = 1, write_z = !d->zmsk;
    if (d->ate && !alpha_pass(d, a)) {
        switch (d->afail) {
        case 0: return 1;                  /* every pixel ends here: nothing drawn, nothing counted */
        case 1: write_z = 0; break;
        case 2: write_rgb = write_a = 0; break;
        default: write_a = 0; write_z = 0; break;
        }
    }
    if (d->colclamp) {
        r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
    } else {
        r &= 255; g &= 255; b &= 255;
    }
    int oa = a & 255;
    if (d->fba)
        oa |= 0x80;
    /* the frame word: nw = (old & keep) | (src & ~keep), as in plot() */
    const int wframe = write_rgb || write_a;
    uint32_t keep, src;
    if (f32) {
        src = (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | (uint32_t)oa << 24;
        keep = (uint32_t)d->fbmsk;
        if (!write_a) keep |= 0xFF000000u;
        if (!write_rgb) keep |= 0x00FFFFFFu;
        if (d->fpsm == EM_GS_PSMCT24) keep |= 0xFF000000u;
    } else {
        src = (uint32_t)(r >> 3) | (uint32_t)(g >> 3) << 5 | (uint32_t)(b >> 3) << 10 | (uint32_t)(oa >> 7) << 15;
        const uint32_t m = (uint32_t)d->fbmsk;
        keep = ((m >> 3) & 0x1F) | ((m >> 6) & 0x3E0) | ((m >> 9) & 0x7C00) | ((m >> 16) & 0x8000);
        if (!write_a) keep |= 0x8000;
        if (!write_rgb) keep |= 0x7FFF;
    }
    const uint32_t put = src & ~keep;
    /* the Z word: plot()'s clamp to the Z format, written when ZTE and the
     * write is not masked */
    const int wz = d->zte && write_z;
    const int z32 = d->zpsm == EM_GS_PSMZ32 || d->zpsm == EM_GS_PSMZ24;
    uint32_t zval;
    if (d->zpsm == EM_GS_PSMZ32)
        zval = (uint32_t)(fr->z > 0xFFFFFFFFull ? 0xFFFFFFFFull : fr->z);
    else {
        const int zmx = zmax_of(d->zpsm);
        zval = (uint32_t)(fr->z > (uint64_t)zmx ? (uint64_t)zmx : fr->z);
    }
    if (px0 > px1)
        return 1;
    const uint64_t per_row = (uint64_t)(px1 - px0 + 1);
    for (int y = py0; y <= py1; y++) {
        if (!own_row(gs, y))
            continue;
        if ((d->scanmsk == 2 && !(y & 1)) || (d->scanmsk == 3 && (y & 1)))
            continue;
        for (int x = px0; x <= px1; x++) {
            if (wframe) {
                if (f32) {
                    const uint32_t fa = swz32(d->frow, d->fcol, d->fbase, (uint32_t)x, (uint32_t)y);
                    wr32(gs, fa, keep ? (rd32(gs, fa) & keep) | put : put);
                } else {
                    const uint32_t fa = em_gs_addr16(d->fbp, d->fbw, (uint32_t)x, (uint32_t)y, d->fpsm);
                    wr16(gs, fa, keep ? (rd16(gs, fa) & keep) | put : put);
                }
            }
            if (wz) {
                if (z32) {
                    const uint32_t za = swz32(d->zrow, d->zcol, d->zbase, (uint32_t)x, (uint32_t)y);
                    wr32(gs, za, d->zpsm == EM_GS_PSMZ24 ? (rd32(gs, za) & 0xFF000000u) | zval : zval);
                } else {
                    wr16(gs, em_gs_addr16(d->zbp, d->fbw, (uint32_t)x, (uint32_t)y, d->zpsm), zval);
                }
            }
        }
        gs->drawn_pixels += per_row;
    }
    return 1;
}

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
    if (sprite_fill(gs, d, &fr, px0, px1, py0, py1))
        return;
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
    /* UV: the row coordinate accumulated in binary32 from the first row
     * drawn (above); every band takes every step, so the bands draw what
     * one EmGs draws */
    float vrow = 0.0f, vstep = 0.0f;
    const int vacc = d->tme && d->fst && Y1 != Y0;
    if (vacc)
        vrow = uv_row_start(v0->v, v1->v, Y0, Y1, py0, d->mmag, &vstep);
    /* The column coordinate depends on x and the primitive only, so it is
     * computed once per column, by the same expression as per pixel (the
     * same bits), on the first row this band draws (a band that draws no
     * row of the sprite computes none). px0..px1 lies inside the scissor
     * (0..2047). */
    int64_t ucol[2048];
    int ucol_ready = 0;
    UvCols ucols;
    uv_cols_init(&ucols, v0->u, v1->u, X0, X1, px0, d->mmag);   /* px0: xs, the first column drawn; FST 1 only */
    for (int y = py0; y <= py1; y++, vrow += vstep) {
        if (!own_row(gs, y))
            continue;
        if (d->tme) {
            if (d->fst) {
                fr.V16 = vacc ? uv_row_v16(vrow, d->mmag) : (int64_t)v0->v;
            } else {
                double t = (Y1 == Y0) ? 0.0 : (double)(16 * y - Y0) / (double)(Y1 - Y0);
                fr.V16 = stq_div(t0 + (t1 - t0) * t, qq, (int)d->th);
            }
            if (!ucol_ready) {
                for (int x = px0; x <= px1; x++) {
                    int64_t U16;
                    if (d->fst) {
                        U16 = (X1 == X0) ? (int64_t)v0->u : uv_cols_u16(&ucols, x);
                    } else {
                        double t = (X1 == X0) ? 0.0 : (double)(16 * x - X0) / (double)(X1 - X0);
                        U16 = stq_div(s0 + (s1 - s0) * t, qq, (int)d->tw);
                    }
                    ucol[x - px0] = U16;
                }
                ucol_ready = 1;
            }
            for (int x = px0; x <= px1; x++) {
                fr.U16 = ucol[x - px0];
                plot(gs, d, x, y, &fr);
            }
        } else {
            for (int x = px0; x <= px1; x++)
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
    if (y >= 0 && own_row(gs, y))
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
                const int ly = xmajor ? cand : ab;
                if (ly >= 0 && own_row(gs, ly))
                    plot(gs, d, xmajor ? ab : cand, ly, &fr);
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
        const int ly = xmajor ? (int)B : A;
        if (ly >= 0 && own_row(gs, ly))
            plot(gs, d, xmajor ? A : (int)B, ly, &fr);
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
    return (int64_t)(128 * p->a0 + floordiv_fast(num * 128, A));
}
static inline int64_t step512(const Plane *p, int64_t A)
{
    i128 v = truncdiv_fast((i128)p->nx * 16 * 512, A);   /* C division truncates toward zero */
    return (int64_t)v;
}
static inline int64_t lane128(const Plane *p, int64_t A, int d)
{
    i128 v = truncdiv_fast((i128)p->nx * 16 * 128 * d, A);
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
    /* a triangle with no row in this EmGs's band draws nothing here */
    if ((gs->band_rows || gs->band_count > 1) && band_misses(gs, py0, py1))
        return;
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
    /* the in-block lane offsets L(d) depend on the triangle only */
    int64_t cl[4][7], fl[7];
    for (int dd = 0; dd <= 3; dd++) {
        /* L(-d) = -L(d): the quotient truncates toward zero */
        for (int k = 0; k < 4; k++) {
            cl[k][dd + 3] = lane128(&pc[k], A, dd);
            cl[k][3 - dd] = -cl[k][dd + 3];
        }
        fl[dd + 3] = lane128(&pf, A, dd);
        fl[3 - dd] = -fl[dd + 3];
    }
    Frag fr;
    memset(&fr, 0, sizeof fr);
    fr.has_tex = d->tme;
    for (int y = py0; y <= py1; y++) {
        if (!own_row(gs, y))
            continue;
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
        int64_t cacc[4], cst[4], facc, fst;
        for (int k = 0; k < 4; k++) {
            cacc[k] = rowstart128(&pc[k], A, X[0], Y[0], xd, y);
            cst[k] = cstep[k];
        }
        facc = rowstart128(&pf, A, X[0], Y[0], xd, y);
        double zrow = (double)V[ztop]->z + ((double)xd - X[ztop] / 16.0) * zgx + ((double)y - Y[ztop] / 16.0) * zgy;
        fst = fstep;
        for (int x = (int)lo; x <= (int)hi; x++) {
            /* Z first: a pixel the Z test or DATE rejects needs none of
             * the other attributes (each is a function of x and y alone,
             * set again for every pixel), so they are computed only for
             * the pixels that pass (plot_dst, P12) */
            {
                double zz = zrow + (double)(x - xd) * zstep;
                fr.z = zz <= 0.0 ? 0 : (uint64_t)floor(zz);
            }
            Dst o;
            if (!plot_dst(gs, d, x, y, &fr, &o))
                continue;
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
            if (d->tme) {
                if (d->fst) {
                    i128 nu = (i128)pu.nx * (16 * x - X[0]) + (i128)pu.ny * (16 * y - Y[0]);
                    i128 nv = (i128)pv.nx * (16 * x - X[0]) + (i128)pv.ny * (16 * y - Y[0]);
                    fr.U16 = (int64_t)((i128)pu.a0 + floordiv_fast(nu, A));
                    fr.V16 = (int64_t)((i128)pv.a0 + floordiv_fast(nv, A));
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
            plot_src(gs, d, x, y, &fr, &o);
        }
    }
}

/* ------------------------------------------------------------------------
 * 9b. Specialized span loops (docs/PERFORMANCE.md M2; GS_EXACT.md 5.4).
 *
 * Most of the first level's pixels are drawn in a few states (PERFORMANCE.md
 * P14's census): the depth-of-field pass's bilinear CT32 sprites (the copy
 * with Z ALWAYS, and the blend over the Z plane with Z GEQUAL), the snow and
 * fire STQ sprites (T8 / T4 through their decoded copies, blended, Z
 * GEQUAL), the textured triangles (T4 / T8 / CT32, bilinear, Z GEQUAL or
 * ALWAYS) and gsw_alpha_clear's blended untextured strip in state block
 * (2,9) (section 9). The loops below draw those: they read the primitive's
 * state once into locals, look the frame's and the Z buffer's row up once a
 * row, a sprite's texel columns and bilinear weights once a column and its
 * texel rows once a row, and run plot()'s tests and arithmetic inline, with
 * the Z test's mode and whether the primitive blends fixed per loop. Every
 * rule is plot()'s (plot_dst, plot_src, sample), in plot()'s order: the
 * scissor (each loop's pixels lie inside it, as in draw_sprite and
 * draw_triangle), the Z test, the frame read, the sample, the texture
 * function, fog, the alpha test, the blend, the clamp, FBA, FBMSK, the frame
 * word written before the Z word; the counters are plot()'s; a texel row is
 * decoded at the row's first pixel that passes the Z test, as sample()
 * decodes it; the coordinates and attributes are draw_sprite's and
 * draw_triangle's expressions, unchanged (-ffp-contract=off, P1). Two
 * rewrites are used. The texture function's four TFX cases are one
 * expression (SpK, below). The bilinear step a + (((b - a) * f) >> 4) is
 * computed as (a * (16 - f) + b * f) >> 4. Both are floor((16 a + (b - a) f) / 16):
 * 16 a is a multiple of 16, so it moves inside the floor exactly, and the
 * sum is a * (16 - f) + b * f, never negative. That lets the four 8-bit
 * channels share one 64-bit multiply in 16-bit lanes (each lane at most
 * 255 * 16, so no lane carries into the next). spec_select names the states
 * the loops take; every other state, and every primitive with
 * EmGs.spec_off, draws through the generic path, whose source is unchanged.
 * EmGs.spec_check draws each primitive a loop takes both ways and compares
 * (spec_check, section 10). */
enum { SP_ZNONE = 0, SP_ZGE = 1, SP_ZGT = 2 };

/* A texture axis's wrap (section 4.3, wrap_u / wrap_v) as two forms:
 * REPEAT and REGION_REPEAT as (u & a) | b, CLAMP and REGION_CLAMP as
 * clampi(u, a, b). */
typedef struct { int clamp, a, b; } SpWrap;

static SpWrap sp_wrap_of(int mode, uint32_t size_log2, int mn, int mx)
{
    SpWrap w;
    switch (mode) {
    case 0: w.clamp = 0; w.a = (1 << size_log2) - 1; w.b = 0; break;
    case 1: w.clamp = 1; w.a = 0; w.b = (1 << size_log2) - 1; break;
    case 2: w.clamp = 1; w.a = mn; w.b = mx; break;
    default: w.clamp = 0; w.a = mn; w.b = mx; break;
    }
    return w;
}
static inline int sp_wrap(SpWrap w, int u) { return w.clamp ? clampi(u, w.a, w.b) : (u & w.a) | w.b; }

/* sample()'s bilinear step for four texels (above): R and B in lanes 0 and 1,
 * G and A in lanes 2 and 3 of 16 bits each. */
static inline uint64_t sp_lanes(uint32_t t) { return (uint64_t)(t & 0x00FF00FFu) | (uint64_t)((t >> 8) & 0x00FF00FFu) << 32; }
static inline uint32_t sp_bilinear(uint32_t t00, uint32_t t10, uint32_t t01, uint32_t t11, int fu, int fv)
{
    const uint64_t m = UINT64_C(0x00FF00FF00FF00FF), gu = (uint64_t)(16 - fu), hu = (uint64_t)fu;
    const uint64_t top = ((sp_lanes(t00) * gu + sp_lanes(t10) * hu) >> 4) & m;
    const uint64_t bot = ((sp_lanes(t01) * gu + sp_lanes(t11) * hu) >> 4) & m;
    const uint64_t v = ((top * (uint64_t)(16 - fv) + bot * (uint64_t)fv) >> 4) & m;
    return (uint32_t)(v & 0x00FF00FFu) | (uint32_t)((v >> 32) & 0x00FF00FFu) << 8;
}

/* rd32 / wr32 on a local copy of the memory pointer */
static inline uint32_t sp_rd32(const uint8_t *mem, uint32_t word)
{
    uint32_t v;
    memcpy(&v, mem + ((word * 4u) & (EM_GS_MEM_BYTES - 1)), 4);
    return v;
}
static inline void sp_wr32(uint8_t *mem, uint32_t word, uint32_t v)
{
    memcpy(mem + ((word * 4u) & (EM_GS_MEM_BYTES - 1)), &v, 4);
}

/* The bound decoded copy (d->tc) as locals: its storage does not move while a
 * primitive draws (tc_decode_row fills rows in place), so only a row's flag
 * is read again (tc_row's rule: a row is decoded on its first read). */
typedef struct {
    TcEntry *e;
    uint32_t *dec;
    const uint8_t *rowok;
    uint32_t w;
    int vlo, ulo;
} SpTex;

static inline void sp_tex_of(const Draw *d, SpTex *x)
{
    x->e = d->tc;
    x->dec = x->e->dec;
    x->rowok = x->e->rowok;
    x->w = x->e->w;
    x->vlo = x->e->vlo;
    x->ulo = x->e->ulo;
}

static inline __attribute__((always_inline)) const uint32_t *sp_row(const EmGs *gs, const Draw *d, const SpTex *x, int v)
{
    const uint32_t r = (uint32_t)(v - x->vlo);
    if (!x->rowok[r])
        tc_decode_row(gs, d, x->e, v);
    return x->dec + (size_t)r * x->w;
}

/* The primitive's state the pixel stage reads, as locals, with two of
 * plot_src's per-pixel choices made per primitive:
 *  - The texture function (section 5.1) in one form for every TFX: C =
 *    min(255, (Ct * m >> 14) + add), m = 16384 for DECAL (Ct * 2^14 >> 14 =
 *    Ct, exact) and the colour C7 otherwise, add = A7 >> 7 for HIGHLIGHT and
 *    HIGHLIGHT2, else 0; with TCC the alpha is min(255, (At * m >> 14) +
 *    add), m = A7 for MODULATE, else 16384, add = A7 >> 7 for HIGHLIGHT
 *    only; without TCC it is A7 >> 7. These are plot_src's four cases.
 *  - What a pixel that fails the alpha test writes (section 5.3, AFAIL):
 *    nothing (KEEP), the frame without Z (FB_ONLY), only Z (ZB_ONLY), RGB
 *    (RGB_ONLY): whether it writes the frame and Z, and the frame bits it
 *    keeps. */
typedef struct {
    uint32_t fbmsk;
    int zacc, zw, z24;                 /* the Z word's address needed; ZTE and ZMSK 0; PSMZ24 */
    int tfx, tcc;
    int fog, fc0, fc1, fc2;
    int ate, atst, aref;
    int f_keep_all;                    /* a failing pixel writes nothing (AFAIL KEEP) */
    int f_wframe, f_wz;                /* ... else whether it writes the frame, the Z word */
    uint32_t f_keep;                   /* ... and the frame bits it keeps */
    int pabe, ba, bb, bc, bd, fix, colclamp, fba;
} SpK;

static void sp_k(const Draw *d, SpK *k)
{
    k->fbmsk = d->fbmsk;
    k->zacc = d->zte && (d->ztst >= 2 || !d->zmsk);
    k->zw = d->zte && !d->zmsk;
    k->z24 = d->zpsm == EM_GS_PSMZ24;
    k->tfx = (int)d->tfx;
    k->tcc = (int)d->tcc;
    k->fog = d->fog; k->fc0 = d->fogcol[0]; k->fc1 = d->fogcol[1]; k->fc2 = d->fogcol[2];
    k->ate = d->ate; k->atst = d->atst; k->aref = d->aref;
    /* plot_src's AFAIL cases: KEEP returns; FB_ONLY drops the Z write;
     * ZB_ONLY the frame write; RGB_ONLY the alpha and the Z write */
    const int wrgb = d->afail != 2, wa = d->afail == 1, wz = d->afail == 2 ? k->zw : 0;
    k->f_keep_all = d->afail == 0;
    k->f_wframe = wrgb || wa;
    k->f_wz = wz;
    k->f_keep = d->fbmsk | (wa ? 0u : 0xFF000000u) | (wrgb ? 0u : 0x00FFFFFFu);
    k->pabe = d->pabe; k->ba = d->ba; k->bb = d->bb; k->bc = d->bc; k->bd = d->bd; k->fix = d->fix;
    k->colclamp = d->colclamp; k->fba = d->fba;
}

/* plot_src for a CT32 frame and a Z32 / Z24 Z buffer: t the texel sampled
 * (TEX), cr..ca the 8.7 colour, f the 8.7 fog weight, the frame word's
 * address and value and the Z word's address and the pixel's Z as plot_dst
 * gives them. 1 when the pixel is drawn (and counted). */
static inline __attribute__((always_inline)) int sp_src(uint8_t *mem, const SpK *k, const int TEX, const int BLEND,
                                                        uint32_t t, int cr, int cg, int cb, int ca, int f,
                                                        uint32_t faddr, uint32_t fword, uint32_t zaddr, uint32_t zval)
{
    int r, g, b, a;
    if (TEX) {
        const int tr = (int)(t & 255), tg = (int)((t >> 8) & 255), tb = (int)((t >> 16) & 255), ta = (int)(t >> 24);
        /* the texture function, one form for every TFX (SpK) */
        const int af = ca >> 7, add = k->tfx >= 2 ? af : 0;
        const int dc = k->tfx == 1;
        const int mr = dc ? 16384 : cr, mg = dc ? 16384 : cg, mb = dc ? 16384 : cb;
        r = ((tr * mr) >> 14) + add; g = ((tg * mg) >> 14) + add; b = ((tb * mb) >> 14) + add;
        a = k->tcc ? ((ta * (k->tfx == 0 ? ca : 16384)) >> 14) + (k->tfx == 2 ? af : 0) : af;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        if (a > 255) a = 255;
    } else {
        r = cr >> 7; g = cg >> 7; b = cb >> 7; a = ca >> 7;
    }
    if (k->fog) {
        r = k->fc0 + (((r - k->fc0) * f) >> 15);
        g = k->fc1 + (((g - k->fc1) * f) >> 15);
        b = k->fc2 + (((b - k->fc2) * f) >> 15);
    }
    /* the alpha test; a failing pixel's writes are AFAIL's (SpK) */
    int wframe = 1, write_z = k->zw;
    uint32_t keep = k->fbmsk;
    if (k->ate) {
        int pass;
        switch (k->atst) {
        case 0: pass = 0; break;
        case 1: pass = 1; break;
        case 2: pass = a < k->aref; break;
        case 3: pass = a <= k->aref; break;
        case 4: pass = a == k->aref; break;
        case 5: pass = a >= k->aref; break;
        case 6: pass = a > k->aref; break;
        default: pass = a != k->aref; break;
        }
        if (!pass) {
            if (k->f_keep_all)
                return 0;
            wframe = k->f_wframe;
            write_z = k->f_wz;
            keep = k->f_keep;
        }
    }
    if (BLEND && (!k->pabe || (a & 0x80))) {
        const int cs[3] = {r, g, b};
        const int cd[3] = {(int)(fword & 255), (int)((fword >> 8) & 255), (int)((fword >> 16) & 255)};
        const int da = (int)(fword >> 24);
        int out[3];
        const int cc = k->bc == 0 ? a : k->bc == 1 ? da : k->fix;
        for (int i = 0; i < 3; i++) {
            const int A = k->ba == 0 ? cs[i] : k->ba == 1 ? cd[i] : 0;
            const int B = k->bb == 0 ? cs[i] : k->bb == 1 ? cd[i] : 0;
            const int D = k->bd == 0 ? cs[i] : k->bd == 1 ? cd[i] : 0;
            out[i] = (((A - B) * cc) >> 7) + D;
        }
        r = out[0]; g = out[1]; b = out[2];
    }
    if (k->colclamp) {
        r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
    } else {
        r &= 255; g &= 255; b &= 255;
    }
    int oa = a & 255;
    if (k->fba)
        oa |= 0x80;
    if (wframe) {
        const uint32_t src = (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | (uint32_t)oa << 24;
        sp_wr32(mem, faddr, (fword & keep) | (src & ~keep));
    }
    if (write_z) {
        if (k->z24)
            sp_wr32(mem, zaddr, (sp_rd32(mem, zaddr) & 0xFF000000u) | zval);
        else
            sp_wr32(mem, zaddr, zval);
    }
    return 1;
}

/* plot()'s counters for a loop's pixels (and EmGs.spc's) */
static void sp_count(EmGs *gs, int tex, unsigned loop, uint64_t zfail, uint64_t samples, uint64_t drawn)
{
    gs->pix.zfail += zfail;
    if (tex) {                         /* bilinear (MMAG 1) */
        gs->pix.zfail_tex += zfail;
        gs->pix.zfail_bil += zfail;
        gs->pix.samples += samples;
        gs->pix.samples_bil += samples;
    }
    gs->drawn_pixels += drawn;
    gs->spc.pixels[loop] += drawn;
}

/* A textured sprite from its decoded copy, bilinear (draw_sprite). */
static inline __attribute__((always_inline)) void sp_sprite(EmGs *gs, const Draw *d, const EmGsVertex *v0,
                                                            const EmGsVertex *v1, const int ZM, const int BLEND)
{
    /* draw_sprite's rectangle, colour, Z and texel coordinates */
    int X0 = wx(d->c, v0->x), Y0 = wy(d->c, v0->y), X1 = wx(d->c, v1->x), Y1 = wy(d->c, v1->y);
    int xa = X0 < X1 ? X0 : X1, xb = X0 < X1 ? X1 : X0, ya = Y0 < Y1 ? Y0 : Y1, yb = Y0 < Y1 ? Y1 : Y0;
    int px0 = (int)floordiv64(xa + 15, 16), px1 = (int)floordiv64(xb + 15, 16) - 1;
    int py0 = (int)floordiv64(ya + 15, 16), py1 = (int)floordiv64(yb + 15, 16) - 1;
    if (px0 < d->sx0) px0 = d->sx0;
    if (px1 > d->sx1) px1 = d->sx1;
    if (py0 < d->sy0) py0 = d->sy0;
    if (py1 > d->sy1) py1 = d->sy1;
    const int cr = v1->rgba[0] << 7, cg = v1->rgba[1] << 7, cb = v1->rgba[2] << 7, ca = v1->rgba[3] << 7;
    const int f = (int)v1->f << 7;
    const uint64_t z = v1->z;
    double s0 = 0, s1 = 0, t0 = 0, t1 = 0, qq = 1;
    if (!d->fst) {
        qq = f32_of(v1->q);
        s0 = f32_of(v0->s); s1 = f32_of(v1->s); t0 = f32_of(v0->t); t1 = f32_of(v1->t);
        if (d->stq_grid && qq > 0.0 && isfinite(qq)) {
            int E = q_exponent(qq);
            s0 = stq_trunc_st(s0, E); s1 = stq_trunc_st(s1, E);
            t0 = stq_trunc_st(t0, E); t1 = stq_trunc_st(t1, E);
            qq = stq_trunc_q(qq, E);
        }
    }
    float vrow = 0.0f, vstep = 0.0f;
    const int vacc = d->fst && Y1 != Y0;
    if (vacc)
        vrow = uv_row_start(v0->v, v1->v, Y0, Y1, py0, d->mmag, &vstep);   /* draw_sprite's */
    SpK k;
    sp_k(d, &k);
    /* plot_dst's Z clamped to the format (Z32 or Z24 where Z is read or written) */
    const uint32_t zval = (uint32_t)(k.z24 ? (z > 0xFFFFFFull ? 0xFFFFFFull : z) : (z > 0xFFFFFFFFull ? 0xFFFFFFFFull : z));
    uint8_t *const mem = gs->mem;
    const uint32_t *const ftab = d->frow, *const fcol = d->fcol, *const ztab = d->zrow, *const zcol = d->zcol;
    const uint32_t fbase = d->fbase, zbase = d->zbase;
    const SpWrap wu = sp_wrap_of(d->wms, d->tw, d->minu, d->maxu), wv = sp_wrap_of(d->wmt, d->th, d->minv, d->maxv);
    SpTex tx;
    sp_tex_of(d, &tx);
    const int ulo = tx.ulo;
    /* per column (sample()'s wrapped indices into the copy's row and the weight),
     * on the first row this band draws, as draw_sprite computes its U */
    int32_t col_a[2048], col_b[2048];
    uint8_t col_f[2048];
    UvCols ucols;
    uv_cols_init(&ucols, v0->u, v1->u, X0, X1, px0, d->mmag);   /* px0: xs, the first column drawn; FST 1 only */
    int ready = 0;
    uint64_t zfail = 0, samples = 0, drawn = 0;
    for (int y = py0; y <= py1; y++, vrow += vstep) {
        if (!own_row(gs, y))
            continue;
        int64_t V16;
        if (d->fst) {
            V16 = vacc ? uv_row_v16(vrow, d->mmag) : (int64_t)v0->v;
        } else {
            double t = (Y1 == Y0) ? 0.0 : (double)(16 * y - Y0) / (double)(Y1 - Y0);
            V16 = stq_div(t0 + (t1 - t0) * t, qq, (int)d->th);
        }
        if (!ready) {
            for (int x = px0; x <= px1; x++) {
                int64_t U16;
                if (d->fst) {
                    U16 = (X1 == X0) ? (int64_t)v0->u : uv_cols_u16(&ucols, x);
                } else {
                    double t = (X1 == X0) ? 0.0 : (double)(16 * x - X0) / (double)(X1 - X0);
                    U16 = stq_div(s0 + (s1 - s0) * t, qq, (int)d->tw);
                }
                const int64_t Ub = U16 - 8;
                const int iu = (int)(Ub >> 4);
                col_a[x - px0] = sp_wrap(wu, iu) - ulo;
                col_b[x - px0] = sp_wrap(wu, iu + 1) - ulo;
                col_f[x - px0] = (uint8_t)(Ub & 15);
            }
            ready = 1;
        }
        const int64_t Vb = V16 - 8;
        const int iv = (int)(Vb >> 4), fv = (int)(Vb & 15);
        const int vw0 = sp_wrap(wv, iv), vw1 = sp_wrap(wv, iv + 1);
        const uint32_t *r0 = NULL, *r1 = NULL;
        const uint32_t frb = fbase + ftab[y & 2047];
        const uint32_t zrb = k.zacc ? zbase + ztab[y & 2047] : 0;
        for (int x = px0; x <= px1; x++) {
            const uint32_t zaddr = k.zacc ? zrb + zcol[x & 2047] : 0;
            if (ZM != SP_ZNONE) {
                uint32_t zb = sp_rd32(mem, zaddr);
                if (k.z24) zb &= 0xFFFFFF;
                if (ZM == SP_ZGE ? zval < zb : zval <= zb) {
                    zfail++;
                    continue;
                }
            }
            const uint32_t faddr = frb + fcol[x & 2047], fword = sp_rd32(mem, faddr);
            if (!r0) {
                r0 = sp_row(gs, d, &tx, vw0);
                r1 = sp_row(gs, d, &tx, vw1);
            }
            const int i = x - px0, a = col_a[i], b = col_b[i];
            const uint32_t t = sp_bilinear(r0[a], r0[b], r1[a], r1[b], col_f[i], fv);
            samples++;
            drawn += (uint64_t)sp_src(mem, &k, 1, BLEND, t, cr, cg, cb, ca, f, faddr, fword, zaddr, zval);
        }
    }
    sp_count(gs, 1, 0, zfail, samples, drawn);
}

/* A triangle: textured (TEX: from its decoded copy, bilinear) or not
 * (draw_triangle). */
static inline __attribute__((always_inline)) void sp_tri(EmGs *gs, const Draw *d, const EmGsVertex *v0,
                                                         const EmGsVertex *v1, const EmGsVertex *v2, const int TEX,
                                                         const int ZM, const int BLEND)
{
    /* draw_triangle's edges, rows, planes and attributes */
    int X[3] = {wx(d->c, v0->x), wx(d->c, v1->x), wx(d->c, v2->x)};
    int Y[3] = {wy(d->c, v0->y), wy(d->c, v1->y), wy(d->c, v2->y)};
    int64_t A = (int64_t)(X[1] - X[0]) * (Y[2] - Y[0]) - (int64_t)(Y[1] - Y[0]) * (X[2] - X[0]);
    if (A == 0)
        return;
    int s = A > 0 ? 1 : -1;
    const EmGsVertex *V[3] = {v0, v1, v2};
    int iip = (int)((d->prim >> 3) & 1);
    struct { int64_t kx, ky, c; int tie; } e[3];
    for (int i = 0; i < 3; i++) {
        int ax = X[i], ay = Y[i], bx = X[(i + 1) % 3], by = Y[(i + 1) % 3];
        e[i].kx = -(int64_t)s * (by - ay);
        e[i].ky = (int64_t)s * (bx - ax);
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
    if ((gs->band_rows || gs->band_count > 1) && band_misses(gs, py0, py1))
        return;
    Plane pc[4], pf, pz, pu, pv;
    const EmGsVertex *flat = v2;
    for (int q = 0; q < 4; q++)
        pc[q] = plane(V[0]->rgba[q], V[1]->rgba[q], V[2]->rgba[q], X[0], Y[0], X[1], Y[1], X[2], Y[2]);
    pf = plane(V[0]->f, V[1]->f, V[2]->f, X[0], Y[0], X[1], Y[1], X[2], Y[2]);
    pz = plane(V[0]->z, V[1]->z, V[2]->z, X[0], Y[0], X[1], Y[1], X[2], Y[2]);
    pu = plane(V[0]->u, V[1]->u, V[2]->u, X[0], Y[0], X[1], Y[1], X[2], Y[2]);
    pv = plane(V[0]->v, V[1]->v, V[2]->v, X[0], Y[0], X[1], Y[1], X[2], Y[2]);
    int64_t cstep[4], fstep = step512(&pf, A);
    float inv_area = 1.0f / (float)A;
    double zgx = (double)pz.nx * 16.0 * (double)inv_area, zgy = (double)pz.ny * 16.0 * (double)inv_area;
    double zstep = (double)pz.nx * 16.0 / (double)A;
    int ztop = 0;
    for (int i = 1; i < 3; i++)
        if (Y[i] < Y[ztop] || (Y[i] == Y[ztop] && X[i] < X[ztop]))
            ztop = i;
    for (int q = 0; q < 4; q++)
        cstep[q] = step512(&pc[q], A);
    double sv[3], tv[3], qv[3];
    for (int i = 0; i < 3; i++) {
        sv[i] = f32_of(V[i]->s); tv[i] = f32_of(V[i]->t); qv[i] = f32_of(V[i]->q);
        if (d->stq_grid && qv[i] > 0.0 && isfinite(qv[i])) {
            int E = q_exponent(qv[i]);
            sv[i] = stq_trunc_st(sv[i], E); tv[i] = stq_trunc_st(tv[i], E); qv[i] = stq_trunc_q(qv[i], E);
        }
    }
    int64_t cl[4][7], fl[7];
    for (int dd = 0; dd <= 3; dd++) {
        for (int q = 0; q < 4; q++) {
            cl[q][dd + 3] = lane128(&pc[q], A, dd);
            cl[q][3 - dd] = -cl[q][dd + 3];
        }
        fl[dd + 3] = lane128(&pf, A, dd);
        fl[3 - dd] = -fl[dd + 3];
    }
    SpK k;
    sp_k(d, &k);
    const uint64_t zmx = k.z24 ? 0xFFFFFFull : 0xFFFFFFFFull;
    uint8_t *const mem = gs->mem;
    const uint32_t *const ftab = d->frow, *const fcol = d->fcol, *const ztab = d->zrow, *const zcol = d->zcol;
    const uint32_t fbase = d->fbase, zbase = d->zbase;
    SpWrap wu = {0, 0, 0}, wv = {0, 0, 0};
    SpTex tx = {NULL, NULL, NULL, 0, 0, 0};
    if (TEX) {
        wu = sp_wrap_of(d->wms, d->tw, d->minu, d->maxu);
        wv = sp_wrap_of(d->wmt, d->th, d->minv, d->maxv);
        sp_tex_of(d, &tx);
    }
    const int ulo = tx.ulo;
    const int fst_uv = d->fst, tw = (int)d->tw, th = (int)d->th;
    uint64_t zfail = 0, samples = 0, drawn = 0;
    for (int y = py0; y <= py1; y++) {
        if (!own_row(gs, y))
            continue;
        int64_t py = 16 * (int64_t)y;
        int64_t lo = INT32_MIN, hi = INT32_MAX;
        int empty = 0;
        for (int i = 0; i < 3 && !empty; i++) {
            int64_t K = e[i].ky * py + e[i].c;
            if (e[i].kx > 0) {
                int64_t xm = -floordiv64(K, 16 * e[i].kx);
                if (xm > lo) lo = xm;
            } else if (e[i].kx < 0) {
                int64_t kk = -e[i].kx;
                int64_t xm = floordiv64(K - 1, 16 * kk);
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
        int xd = (int)lo;
        int xr = xd & ~3, jd = xd - xr;
        int64_t cacc[4], cst[4], facc, fst;
        for (int q = 0; q < 4; q++) {
            cacc[q] = rowstart128(&pc[q], A, X[0], Y[0], xd, y);
            cst[q] = cstep[q];
        }
        facc = rowstart128(&pf, A, X[0], Y[0], xd, y);
        double zrow = (double)V[ztop]->z + ((double)xd - X[ztop] / 16.0) * zgx + ((double)y - Y[ztop] / 16.0) * zgy;
        fst = fstep;
        const uint32_t frb = fbase + ftab[y & 2047];
        const uint32_t zrb = k.zacc ? zbase + ztab[y & 2047] : 0;
        for (int x = (int)lo; x <= (int)hi; x++) {
            uint64_t z;
            {
                double zz = zrow + (double)(x - xd) * zstep;
                z = zz <= 0.0 ? 0 : (uint64_t)floor(zz);
            }
            const uint32_t zval = (uint32_t)(z > zmx ? zmx : z);
            const uint32_t zaddr = k.zacc ? zrb + zcol[x & 2047] : 0;
            if (ZM != SP_ZNONE) {
                uint32_t zb = sp_rd32(mem, zaddr);
                if (k.z24) zb &= 0xFFFFFF;
                if (ZM == SP_ZGE ? zval < zb : zval <= zb) {
                    zfail++;
                    continue;
                }
            }
            const uint32_t faddr = frb + fcol[x & 2047], fword = sp_rd32(mem, faddr);
            int64_t kb = (x - xr) >> 2;
            int j = (x - xr) & 3;
            int cr, cg, cb, ca;
            if (iip) {
                cr = clampi((int)(cacc[0] + kb * cst[0] + cl[0][j - jd + 3]), 0, 0x7FFF);
                cg = clampi((int)(cacc[1] + kb * cst[1] + cl[1][j - jd + 3]), 0, 0x7FFF);
                cb = clampi((int)(cacc[2] + kb * cst[2] + cl[2][j - jd + 3]), 0, 0x7FFF);
                ca = clampi((int)(cacc[3] + kb * cst[3] + cl[3][j - jd + 3]), 0, 0x7FFF);
            } else {
                cr = flat->rgba[0] << 7; cg = flat->rgba[1] << 7; cb = flat->rgba[2] << 7; ca = flat->rgba[3] << 7;
            }
            /* the fog weight feeds only fog */
            const int f = k.fog ? clampi((int)(facc + kb * fst + fl[j - jd + 3]), 0, 0x7FFF) : 0;
            uint32_t t = 0;
            if (TEX) {
                int64_t U16, V16;
                if (fst_uv) {
                    i128 nu = (i128)pu.nx * (16 * x - X[0]) + (i128)pu.ny * (16 * y - Y[0]);
                    i128 nv = (i128)pv.nx * (16 * x - X[0]) + (i128)pv.ny * (16 * y - Y[0]);
                    U16 = (int64_t)((i128)pu.a0 + floordiv_fast(nu, A));
                    V16 = (int64_t)((i128)pv.a0 + floordiv_fast(nv, A));
                } else {
                    double px = 16.0 * x, pyy = 16.0 * y;
                    double w1 = ((px - X[0]) * (Y[2] - Y[0]) - (pyy - Y[0]) * (X[2] - X[0])) / (double)A;
                    double w2 = ((X[1] - X[0]) * (pyy - Y[0]) - (Y[1] - Y[0]) * (px - X[0])) / (double)A;
                    double w0 = 1.0 - w1 - w2;
                    double S = (float)(w0 * sv[0] + w1 * sv[1] + w2 * sv[2]);
                    double T = (float)(w0 * tv[0] + w1 * tv[1] + w2 * tv[2]);
                    double Q = (float)(w0 * qv[0] + w1 * qv[1] + w2 * qv[2]);
                    U16 = stq_div(S, Q, tw);
                    V16 = stq_div(T, Q, th);
                }
                /* sample(), bilinear, from the decoded copy */
                const int64_t Ub = U16 - 8, Vb = V16 - 8;
                const int iu = (int)(Ub >> 4), iv = (int)(Vb >> 4), fu = (int)(Ub & 15), fv = (int)(Vb & 15);
                const int u0 = sp_wrap(wu, iu) - ulo, u1 = sp_wrap(wu, iu + 1) - ulo;
                const int vw0 = sp_wrap(wv, iv), vw1 = sp_wrap(wv, iv + 1);
                const uint32_t *r0 = sp_row(gs, d, &tx, vw0), *r1 = sp_row(gs, d, &tx, vw1);
                t = sp_bilinear(r0[u0], r0[u1], r1[u0], r1[u1], fu, fv);
                samples++;
            }
            drawn += (uint64_t)sp_src(mem, &k, TEX, BLEND, t, cr, cg, cb, ca, f, faddr, fword, zaddr, zval);
        }
    }
    sp_count(gs, TEX, TEX ? 1 : 2, zfail, samples, drawn);
}

/* The loops, one per (kind, Z mode, blend): spec_select's number less 1. */
#define SP_LOOPS(ZM, BL)                                                                               \
    static void sp_s##ZM##BL(EmGs *gs, const Draw *d, const EmGsVertex *v) { sp_sprite(gs, d, &v[0], &v[1], ZM, BL); } \
    static void sp_t##ZM##BL(EmGs *gs, const Draw *d, const EmGsVertex *v) { sp_tri(gs, d, &v[0], &v[1], &v[2], 1, ZM, BL); } \
    static void sp_u##ZM##BL(EmGs *gs, const Draw *d, const EmGsVertex *v) { sp_tri(gs, d, &v[0], &v[1], &v[2], 0, ZM, BL); }
SP_LOOPS(0, 0) SP_LOOPS(0, 1) SP_LOOPS(1, 0) SP_LOOPS(1, 1) SP_LOOPS(2, 0) SP_LOOPS(2, 1)
#undef SP_LOOPS
static void (*const sp_loop[18])(EmGs *, const Draw *, const EmGsVertex *) = {
    sp_s00, sp_s01, sp_s10, sp_s11, sp_s20, sp_s21,
    sp_t00, sp_t01, sp_t10, sp_t11, sp_t20, sp_t21,
    sp_u00, sp_u01, sp_u10, sp_u11, sp_u20, sp_u21,
};

/* The loop a primitive takes: 1 + (loop * 6 + Z mode * 2 + blend), loop 0 a
 * sprite, 1 a textured triangle, 2 an untextured one; 0: the generic path.
 * The loops draw a CT32 frame without DATE or SCANMSK; Z off, or ZTST
 * ALWAYS / GEQUAL / GREATER with a Z32 / Z24 buffer where Z is read or
 * written; textures only from their decoded copy, bilinear; untextured
 * sprites stay with sprite_fill and plot(). */
static int spec_select(const Draw *d, unsigned kind)
{
    if (kind != 3 && kind != 6)
        return 0;
    if (d->fpsm != EM_GS_PSMCT32 || d->date || d->scanmsk)
        return 0;
    if (d->tme) {
        if (!d->tc || !d->mmag)
            return 0;
    } else if (kind == 6) {
        return 0;
    }
    int zm = SP_ZNONE;
    if (d->zte) {
        if (d->ztst == 0)
            return 0;
        if ((d->ztst >= 2 || !d->zmsk) && d->zpsm != EM_GS_PSMZ32 && d->zpsm != EM_GS_PSMZ24)
            return 0;
        zm = d->ztst == 1 ? SP_ZNONE : d->ztst == 2 ? SP_ZGE : SP_ZGT;
    }
    const int loop = kind == 6 ? 0 : d->tme ? 1 : 2;
    return 1 + loop * 6 + zm * 2 + (d->abe ? 1 : 0);
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

/* The span about to draw (span_end): its number, the pages its primitives
 * write (the frame; the Z buffer when Z is written), and the stamps of the
 * pages a decoded copy reads (section 4b). Within a span FRAME, ZBUF, TEST
 * and SCISSOR do not change (a write of one ends it, section 3.7), but every
 * primitive is taken. */
/* The pages a span's primitive d writes, added to w[0 .. *nw) (or *w_all);
 * *q is the span's previous primitive (NULL before the first). */
static void span_writes_add(const Draw *d, const Draw **q, PgSpan *w, uint32_t *nw, int *w_all)
{
    const Draw *p = *q;
    if (p && d->fbp == p->fbp && d->fbw == p->fbw && d->fpsm == p->fpsm && d->sx0 == p->sx0 && d->sx1 == p->sx1
        && d->sy0 == p->sy0 && d->sy1 == p->sy1 && d->zte == p->zte && d->zmsk == p->zmsk && d->zbp == p->zbp
        && d->zpsm == p->zpsm)
        return;                        /* the same pages as the previous primitive */
    *q = d;
    PgSpan s;
    if (pg_span(d->fpsm, d->fbp, d->fbw, d->sx0, d->sx1, d->sy0, d->sy1, &s))
        pg_add(w, nw, w_all, s);
    if (d->zte && !d->zmsk && pg_span(d->zpsm, d->zbp, d->fbw, d->sx0, d->sx1, d->sy0, d->sy1, &s))
        pg_add(w, nw, w_all, s);
}

/* The bitmap m of the pages in w[0 .. nw) (all of them with w_all). */
static void span_writes_mask(const PgSpan *w, uint32_t nw, int w_all, uint64_t m[TC_PAGES / 64])
{
    memset(m, 0, TC_PAGES / 64 * sizeof *m);
    if (w_all) {
        memset(m, 0xFF, TC_PAGES / 64 * sizeof *m);
    } else {
        for (uint32_t i = 0; i < nw; i++) {
            const PgSpan r = w[i];
            if (r.npg >= TC_PAGES) {
                memset(m, 0xFF, TC_PAGES / 64 * sizeof *m);
                break;
            }
            for (uint32_t k = 0; k < r.npg;) {           /* the range, a word at a time */
                const uint32_t pg = (r.pg0 + k) & (TC_PAGES - 1), b = pg & 63;
                const uint32_t run = 64 - b < r.npg - k ? 64 - b : r.npg - k;
                m[pg >> 6] |= (run == 64 ? ~UINT64_C(0) : ((UINT64_C(1) << run) - 1)) << b;
                k += run;
            }
        }
    }
}

/* The pages primitives p[0 .. n) of a span write: the ranges w[0 .. *nw)
 * (or *w_all) and the bitmap m of their pages. */
static void span_writes(const struct EmGsPending *p, uint32_t n, PgSpan *w, uint32_t *nw, int *w_all,
                        uint64_t m[TC_PAGES / 64])
{
    *nw = 0;
    *w_all = 0;
    const Draw *q = NULL;
    for (uint32_t i = 0; i < n; i++)
        span_writes_add(&p[i].d, &q, w, nw, w_all);
    span_writes_mask(w, *nw, *w_all, m);
}

/* The pages in m that a copy reads (refmap: pref nonzero), stamped with the
 * current span. */
static void tc_stamp(Tc *t, const uint64_t m[TC_PAGES / 64])
{
    for (uint32_t j = 0; j < TC_PAGES / 64; j++)
        for (uint64_t r = m[j] & t->refmap[j]; r; r &= r - 1)
            t->pstamp[j * 64 + (uint32_t)__builtin_ctzll(r)] = t->sigma;
}

static void tc_span(EmGs *gs, const struct EmGsPending *p, uint32_t n)
{
    Tc *t = tc_get(gs);
    if (!t)
        return;
    t->sigma++;
    /* one primitive stands for the span when queue_prim saw them all with the
     * same registers (a write of one ends the span, so that is the rule) */
    if (t->q_valid && !t->q_mixed && n > 1)
        n = 1;
    t->q_valid = 0;
    uint64_t m[TC_PAGES / 64];
    span_writes(p, n, t->w, &t->nw, &t->w_all, m);
    tc_stamp(t, m);
}


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

/* Binds the Draw's address tables (section 1b): 0 without memory for them. */
static int swz_bind(EmGs *gs, Draw *d)
{
    if (d->fpsm == EM_GS_PSMCT32 || d->fpsm == EM_GS_PSMCT24) {
        if (!(d->frow = swz_row(gs, SWZ_CT32, d->fbw)))
            return 0;
        d->fcol = ((Swz *)gs->swz)->col[SWZ_CT32][0];
        d->fbase = d->fbp * 64;
    }
    if (d->zte && (d->zpsm == EM_GS_PSMZ32 || d->zpsm == EM_GS_PSMZ24)) {
        if (!(d->zrow = swz_row(gs, SWZ_Z32, d->fbw)))
            return 0;
        d->zcol = ((Swz *)gs->swz)->col[SWZ_Z32][0];
        d->zbase = d->zbp * 64;
    }
    if (d->tme) {
        const unsigned l = d->tpsm == EM_GS_PSMCT32 ? SWZ_CT32 : d->tpsm == EM_GS_PSMT8 ? SWZ_T8
                         : d->tpsm == EM_GS_PSMT4 ? SWZ_T4 : SWZ_N;
        if (l != SWZ_N) {
            if (!(d->trow = swz_row(gs, l, d->tbw)))
                return 0;
            d->tcol = ((Swz *)gs->swz)->col[l][0];
            d->tbase = d->tbp * (l == SWZ_CT32 ? 64u : l == SWZ_T8 ? 256u : 512u);
        }
    }
    return 1;
}

/* The generic path's draw of a primitive (the kinds of draw_one). */
static void draw_generic(EmGs *gs, struct EmGsPending *p)
{
    switch (p->kind) {
    case 0: draw_point(gs, &p->d, &p->v[0]); break;
    case 1: draw_line(gs, &p->d, &p->v[0], &p->v[1]); break;
    case 6: draw_sprite(gs, &p->d, &p->v[0], &p->v[1]); break;
    default: draw_triangle(gs, &p->d, &p->v[0], &p->v[1], &p->v[2]); break;
    }
}

/* EmGs.spec_check (a developer check, EM_GS_SPEC_CHECK=1): the primitive is
 * drawn by the generic path, the words it can write are put back as they
 * were, and it is drawn again by its loop (section 9b); the words and the
 * counters must be equal, or the check aborts. The words are the frame word
 * and, where the Z word is read or written, the Z word of every pixel of
 * this band's rows in the rectangle the primitive can cover (a sprite's
 * pixel rectangle; a triangle's vertex box, a pixel wider on each side),
 * inside the scissor: the only words plot() and the loops write. Only this
 * EmGs's rows are touched, so the bands of em_gs_world can check at once.
 * The loop's draw is the one kept. */
typedef struct { uint32_t word, before, after; } SpLog;

static void spec_check(EmGs *gs, struct EmGsPending *p, int spec)
{
    const Draw *d = &p->d;
    int x0, x1, y0, y1;
    if (p->kind == 6) {
        const int X0 = wx(d->c, p->v[0].x), Y0 = wy(d->c, p->v[0].y), X1 = wx(d->c, p->v[1].x), Y1 = wy(d->c, p->v[1].y);
        x0 = (int)floordiv64((X0 < X1 ? X0 : X1) + 15, 16); x1 = (int)floordiv64((X0 < X1 ? X1 : X0) + 15, 16) - 1;
        y0 = (int)floordiv64((Y0 < Y1 ? Y0 : Y1) + 15, 16); y1 = (int)floordiv64((Y0 < Y1 ? Y1 : Y0) + 15, 16) - 1;
    } else {
        int xmin = INT32_MAX, xmax = INT32_MIN, ymin = INT32_MAX, ymax = INT32_MIN;
        for (int i = 0; i < 3; i++) {
            const int X = wx(d->c, p->v[i].x), Y = wy(d->c, p->v[i].y);
            if (X < xmin) xmin = X;
            if (X > xmax) xmax = X;
            if (Y < ymin) ymin = Y;
            if (Y > ymax) ymax = Y;
        }
        x0 = (int)floordiv64(xmin, 16) - 1; x1 = (int)floordiv64(xmax, 16) + 1;
        y0 = (int)floordiv64(ymin + 15, 16); y1 = (int)floordiv64(ymax, 16);
    }
    if (x0 < d->sx0) x0 = d->sx0;
    if (x1 > d->sx1) x1 = d->sx1;
    if (y0 < d->sy0) y0 = d->sy0;
    if (y1 > d->sy1) y1 = d->sy1;
    const int zacc = d->zte && (d->ztst >= 2 || !d->zmsk);
    const size_t need = x0 <= x1 && y0 <= y1 ? (size_t)(x1 - x0 + 1) * (size_t)(y1 - y0 + 1) * (zacc ? 2u : 1u) : 0;
    if (need > gs->spec_log_cap) {
        SpLog *nl = realloc(gs->spec_log, need * sizeof *nl);
        if (!nl) {
            fprintf(stderr, "em_gs_raster: EM_GS_SPEC_CHECK: no memory for %zu words\n", need);
            abort();
        }
        gs->spec_log = nl;
        gs->spec_log_cap = need;
    }
    SpLog *lg = gs->spec_log;
    size_t n = 0;
    for (int y = y0; y <= y1; y++) {
        if (!own_row(gs, y))
            continue;
        for (int x = x0; x <= x1; x++) {
            lg[n].word = swz32(d->frow, d->fcol, d->fbase, (uint32_t)x, (uint32_t)y);
            lg[n].before = rd32(gs, lg[n].word);
            n++;
            if (zacc) {
                lg[n].word = swz32(d->zrow, d->zcol, d->zbase, (uint32_t)x, (uint32_t)y);
                lg[n].before = rd32(gs, lg[n].word);
                n++;
            }
        }
    }
    const uint64_t drawn0 = gs->drawn_pixels;
    const __typeof__(gs->pix) pix0 = gs->pix;
    draw_generic(gs, p);
    const uint64_t drawn_g = gs->drawn_pixels;
    const __typeof__(gs->pix) pix_g = gs->pix;
    for (size_t i = 0; i < n; i++)
        lg[i].after = rd32(gs, lg[i].word);
    for (size_t i = n; i-- > 0;)
        wr32(gs, lg[i].word, lg[i].before);
    gs->drawn_pixels = drawn0;
    gs->pix = pix0;
    sp_loop[spec - 1](gs, d, p->v);
    size_t bad = n;
    for (size_t i = 0; i < n && bad == n; i++)
        if (rd32(gs, lg[i].word) != lg[i].after)
            bad = i;
    if (bad < n || gs->drawn_pixels != drawn_g || memcmp(&gs->pix, &pix_g, sizeof pix_g) != 0) {
        fprintf(stderr, "em_gs_raster: EM_GS_SPEC_CHECK: loop %d (kind %u, TEST 0x%llx, ALPHA 0x%llx, TEX0 0x%llx) "
                "differs from the generic path: ", spec, p->kind, (unsigned long long)d->c->test,
                (unsigned long long)d->c->alpha, (unsigned long long)d->c->tex0);
        if (bad < n)
            fprintf(stderr, "word 0x%x is 0x%08x, the generic path wrote 0x%08x\n", lg[bad].word,
                    rd32(gs, lg[bad].word), lg[bad].after);
        else
            fprintf(stderr, "drawn %llu, the generic path %llu (or the pixel counters)\n",
                    (unsigned long long)(gs->drawn_pixels - drawn0), (unsigned long long)(drawn_g - drawn0));
        abort();
    }
    gs->spc.checked++;
}

static void draw_one(EmGs *gs, struct EmGsPending *p)
{
    p->d.c = &p->c;
    if (!swz_bind(gs, &p->d)) {
        /* fail-stop: nothing drawn, the primitive counted as refused */
        refuse(gs, EM_GS_REFUSE_MEMORY, "no memory for the address tables");
        gs->refused_prims++;
        return;
    }
    /* the decoded copy of its texture (section 4b), or none */
    p->d.tc = NULL;
    if (p->d.tme) {
        if (!gs->tc_off)
            p->d.tc = tc_bind(gs, &p->d);
        if (p->d.tc) gs->tcs.binds++; else gs->tcs.uncached++;
    }
    const uint64_t s0 = gs->pix.samples, b0 = gs->pix.samples_bil;
    /* a specialized loop (section 9b) or the generic path */
    const int spec = gs->spec_off ? 0 : spec_select(&p->d, p->kind);
    if (!spec) {
        draw_generic(gs, p);
    } else {
        gs->spc.prims[(spec - 1) / 6]++;
        if (gs->spec_check)
            spec_check(gs, p, spec);
        else
            sp_loop[spec - 1](gs, &p->d, p->v);
    }
    if (p->d.tc) {
        const Tc *t = gs->tc;
        gs->tcs.samples += gs->pix.samples - s0;
        gs->tcs.samples_bil += gs->pix.samples_bil - b0;
        gs->tcs.rows = t->rows;
        gs->tcs.texels = t->texels;
    }
}

/* The span ends: draw the queue. `measured` says whether a capture showed
 * that this kind of boundary ends the reference's span (GS_EXACT.md 3.7).
 * Where it did not, the model still ends the span here, but it keeps a
 * chain of the spans joined by unmeasured boundaries: if merging them would
 * change the grid decision of any of them (one span has one Z and uses STQ,
 * and the chain's Z are not all equal), the result depends on an
 * unmeasured rule and strict mode faults (EM_GS_REFUSE_UNMEASURED). */
static struct EmGsPending *list_slot(struct EmGsList *l, uint32_t i);
static void list_close(EmGs *gs, uint32_t n, int stq_grid);

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
    if (gs->list) {
        /* parse once (section 12): appended, not drawn */
        if (n)
            list_close(gs, n, (int)gs->pend_zconst);
        return;
    }
    if (n && !gs->tc_off)
        tc_span(gs, gs->pend, n);      /* section 4b: the pages the span writes */
    for (uint32_t i = 0; i < n; i++) {
        gs->pend[i].d.stq_grid = (int)gs->pend_zconst;
        draw_one(gs, &gs->pend[i]);
    }
}

void em_gs_flush(EmGs *gs)
{
    span_end(gs, 1);
    if (gs->tee) em_gs_flush(gs->tee);
}

void em_gs_release(EmGs *gs)
{
    em_gs_flush(gs);
    free(gs->pend);
    gs->pend = NULL;
    gs->pend_cap = 0;
    free(gs->setup_cache);
    gs->setup_cache = NULL;
    gs->setup_valid = 0;
    swz_free(gs);
    tc_free(gs);
    free(gs->spec_log);
    gs->spec_log = NULL;
    gs->spec_log_cap = 0;
}

static void queue_prim(EmGs *gs, const Draw *d, unsigned kind, const EmGsVertex *a, const EmGsVertex *b,
                       const EmGsVertex *c)
{
    unsigned attr = d->prim & 0x7F8, cls = kind;
    unsigned nv = kind == 0 ? 1 : kind == 3 ? 3 : 2;
    const EmGsVertex *vs[3] = {a, b, c};
    if (gs->pend_n && (attr != gs->pend_attr || cls != gs->pend_class))
        span_end(gs, 1);               /* measured: p7_flush prim_iip, p8_span prim_* / rev_*, p8_class */
    /* parse once (section 12): the span is queued in the list's storage */
    struct EmGsPending *p = gs->list ? list_slot(gs->list, gs->pend_n) : NULL;
    if (gs->list && !p)
        return;                        /* (no memory: the list is marked failed) */
    if (!gs->list && gs->pend_n == gs->pend_cap) {
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
            if (!gs->tc_off)
                tc_span(gs, &one, 1);          /* a span of its own (section 4b) */
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
    if (gs->tc) {
        /* section 4b: the pages the span writes come from these registers */
        Tc *t = gs->tc;
        const uint64_t r[4] = { d->c->frame, d->c->zbuf, d->c->test, d->c->scissor };
        if (gs->pend_n == 0) {
            memcpy(t->q_reg, r, sizeof r);
            t->q_valid = 1;
            t->q_mixed = 0;
        } else if (memcmp(t->q_reg, r, sizeof r) != 0) {
            t->q_mixed = 1;
        }
    }
    if (!p)
        p = &gs->pend[gs->pend_n];
    gs->pend_n++;
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
    /* the memory the span uses decides only whether a transfer ends it
     * (trx_start); a parsing EmGs refuses transfers (section 12) */
    if (gs->list)
        return;
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
    /* draw_setup reads only registers: with none written since the last
     * accepted setup of this kind, it would give the same Draw */
    if (gs->setup_valid && gs->setup_kind == kind) {
        memcpy(d, gs->setup_cache, sizeof *d);
        gs->drawn_prims++;
        return 1;
    }
    if (draw_setup(gs, d, kind)) {
        if (!gs->setup_cache) gs->setup_cache = malloc(sizeof *d);
        if (gs->setup_cache) {
            memcpy(gs->setup_cache, d, sizeof *d);
            gs->setup_valid = 1;
            gs->setup_kind = kind;
        }
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

/* ------------------------------------------------------------------------
 * 12. Parse once (docs/PERFORMANCE.md M4; em_gs_raster.h EmGsList).
 *
 * A parsing EmGs (EmGs.list set) runs the stream as any EmGs does (the
 * registers, the vertex queue, draw_setup and its refusals, the span and
 * its chain, the CLUT loads) and, where span_end would draw the span, hands
 * it here. A span is kept as the primitives exactly as span_end would have
 * drawn them (EmGsPending: the Draw with its grid decision, the context
 * copied at the kick, the vertices), plus the two pieces of EmGs state the
 * drawing reads besides those (the CLUT buffer, through fetch_rgba and the
 * decoded palettes, and DIMX, through the dither), as they are when the span
 * ends, and the pages it writes (span_writes, as tc_span computes them).
 * em_gs_list_draw gives a drawing EmGs that state and calls draw_one on each
 * primitive: the same calls the parsing EmGs would have made, in the same
 * order, so it writes what they write in its rows. A primitive whose rows
 * (list_rows: its vertices' rows two rows wider on each side, inside the
 * scissor, which every draw function stays within) miss the band is not
 * called: it would draw nothing there (a textured line is called in every
 * band: draw_line flags it whatever its rows).
 *
 * The decoded texture cache (section 4b) of a drawing EmGs must see every
 * span that writes memory, also those it draws nothing of: a span with no
 * primitive in its band is not drawn, but its pages are stamped (together
 * with the other such spans before the next span it draws: one stamp marks
 * the same copies out of date as one stamp each, as nothing is bound in
 * between). em_gs_list_pass stamps the pages of spans an EmGs drew none of.
 *
 * The storage is chunked and never moves: an appended span's data stay
 * where they are until em_gs_list_clear, so spans already appended can be
 * drawn while later ones are appended (em_gs_world publishes the count).
 *
 * The parsing EmGs reads memory for CLUT loads only. One EmGs drawing
 * directly reads it there with every span that ended before the load drawn
 * and the span still queued not (clut_load reads, then ends that span);
 * appended, those are the spans of the list so far and the span appended
 * next. A load from a page a span appended since the last drain writes
 * therefore calls the drain (em_gs_list_drain), which returns once every
 * span appended so far is drawn: the load then reads what the direct EmGs
 * reads. */
#define LS_PCHUNK 4096u                /* primitives a chunk               */
#define LS_PCHUNKS 4096u
#define LS_SCHUNK 1024u                /* spans a chunk                    */
#define LS_SCHUNKS 4096u
#define LS_CCHUNK 64u                  /* CLUT snapshots a chunk           */
#define LS_CCHUNKS 1024u

typedef struct { int32_t lo, hi; } LsRows;
typedef struct {
    uint32_t first, n;                 /* primitives first .. first + n - 1 */
    int32_t ylo, yhi;                  /* their rows (union)                */
    uint32_t clut;                     /* the CLUT snapshot it is drawn with */
    uint64_t clut_serial;              /*   and its key (EmGs.list_clut): the parsing EmGs's
                                          clut_changes + 1, one CLUT content per key, in
                                          every list that EmGs fills */
    uint64_t dimx;
    uint32_t nw;                       /* the pages it writes (tc_span)     */
    int w_all;
    PgSpan w[TC_SPAN_W];
    uint64_t m[TC_PAGES / 64];
} LsSpan;

struct EmGsList {
    struct EmGsPending *prim[LS_PCHUNKS];
    LsRows *rows[LS_PCHUNKS];
    LsSpan *span[LS_SCHUNKS];
    uint16_t (*clut[LS_CCHUNKS])[512];
    uint32_t nprims, nspans, ncluts;
    uint64_t snap_changes;             /* the parsing EmGs's clut_changes at the last snapshot */
    int failed;
    uint64_t written[TC_PAGES / 64];   /* the pages the spans so far write */
    uint64_t undrawn[TC_PAGES / 64];   /* ... those appended since the last drain */
    /* em_gs_list_drain: draws the list so far, for a CLUT load from it */
    int (*drain)(void *);
    void *drain_ctx;
    /* em_gs_list_prior: a list still being drawn, and the wait for it */
    const struct EmGsList *prior;
    void (*prior_wait)(void *);
    void *prior_ctx;
};

EmGsList *em_gs_list_create(void)
{
    return calloc(1, sizeof(EmGsList));
}

void em_gs_list_destroy(EmGsList *l)
{
    if (!l)
        return;
    for (uint32_t i = 0; i < LS_PCHUNKS; i++) {
        free(l->prim[i]);
        free(l->rows[i]);
    }
    for (uint32_t i = 0; i < LS_SCHUNKS; i++)
        free(l->span[i]);
    for (uint32_t i = 0; i < LS_CCHUNKS; i++)
        free(l->clut[i]);
    free(l);
}

void em_gs_list_clear(EmGsList *l)
{
    if (!l)
        return;
    l->nprims = l->nspans = l->ncluts = 0;
    l->failed = 0;
    memset(l->written, 0, sizeof l->written);
    memset(l->undrawn, 0, sizeof l->undrawn);
}

uint32_t em_gs_list_spans(const EmGsList *l) { return l ? l->nspans : 0; }
int em_gs_list_failed(const EmGsList *l) { return !l || l->failed; }

static inline struct EmGsPending *ls_prim(const EmGsList *l, uint32_t i)
{
    return &l->prim[i / LS_PCHUNK][i % LS_PCHUNK];
}
static inline const LsRows *ls_rows(const EmGsList *l, uint32_t i) { return &l->rows[i / LS_PCHUNK][i % LS_PCHUNK]; }
static inline const LsSpan *ls_span(const EmGsList *l, uint32_t s) { return &l->span[s / LS_SCHUNK][s % LS_SCHUNK]; }

/* The rows a queued primitive can draw: its vertices' rows (window Y in
 * 1/16 pixel, floor), two rows wider each side (points round to the nearest
 * row, lines take a row next to the rounded one, sprites and triangles the
 * rows whose centres they cover), inside the scissor. */
static LsRows list_rows(const struct EmGsPending *p)
{
    if (p->kind == 1 && p->d.tme) {
        /* a textured line flags its refusal in every band that takes it
         * (draw_line; strict mode refuses it at the setup) */
        LsRows all = { INT32_MIN, INT32_MAX };
        return all;
    }
    const unsigned nv = p->kind == 0 ? 1 : p->kind == 3 ? 3 : 2;
    int64_t ymin = INT64_MAX, ymax = INT64_MIN;
    for (unsigned i = 0; i < nv; i++) {
        const int64_t Y = wy(&p->c, p->v[i].y);
        if (Y < ymin) ymin = Y;
        if (Y > ymax) ymax = Y;
    }
    int64_t lo = floordiv64(ymin, 16) - 2, hi = floordiv64(ymax, 16) + 2;
    if (lo < p->d.sy0) lo = p->d.sy0;
    if (hi > p->d.sy1) hi = p->d.sy1;
    LsRows r = { (int32_t)lo, (int32_t)hi };
    return r;
}

/* A CLUT load (clut_load) reads a page a span of the list appended since
 * the last drain writes, and no drain draws them first (em_gs_list_drain:
 * none, or it could not). One that reads such a page with a drain, or a
 * page the prior list writes (em_gs_list_prior), first waits for it. The
 * pages are those of the CLUT's texels (clut_load's reads: 16 x 16 entries
 * for T8, 8 x 2 for T4, at CBP with width 1), exactly. */
static int list_clut_drawn(const EmGs *gs, unsigned cpsm, uint32_t cbp, int t8)
{
    EmGsList *l = gs->list;
    uint64_t pages[TC_PAGES / 64] = {0};
    const unsigned w = t8 ? 16 : 8, h = t8 ? 16 : 2;
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++) {
            const uint32_t byte = cpsm == EM_GS_PSMCT32 || cpsm == EM_GS_PSMCT24 ? addr32_(cbp, 1, x, y, 0) * 4u
                                                                                 : em_gs_addr16(cbp, 1, x, y, cpsm) * 2u;
            const uint32_t pg = (byte / 8192u) & (TC_PAGES - 1);
            pages[pg >> 6] |= UINT64_C(1) << (pg & 63);
        }
    for (uint32_t j = 0; j < TC_PAGES / 64; j++)
        if (pages[j] & l->undrawn[j]) {
            if (!l->drain || !l->drain(l->drain_ctx))
                return 1;
            memset(l->undrawn, 0, sizeof l->undrawn);   /* every span so far is drawn */
            break;
        }
    for (uint32_t j = 0; l->prior && j < TC_PAGES / 64; j++)
        if (pages[j] & l->prior->written[j]) {
            l->prior_wait(l->prior_ctx);
            l->prior = NULL;
        }
    return 0;
}

void em_gs_list_prior(EmGsList *l, const EmGsList *prior, void (*wait)(void *), void *ctx)
{
    if (!l)
        return;
    l->prior = wait ? prior : NULL;
    l->prior_wait = wait;
    l->prior_ctx = ctx;
}

void em_gs_list_drain(EmGsList *l, int (*drain)(void *), void *ctx)
{
    if (!l)
        return;
    l->drain = drain;
    l->drain_ctx = ctx;
}

/* Slot i of the span being queued (the primitive nprims + i), its chunk
 * allocated: NULL (the list marked failed) without memory or room. The
 * slots are not drawable until list_close appends the span. */
static struct EmGsPending *list_slot(EmGsList *l, uint32_t i)
{
    if (l->failed)
        return NULL;
    const uint64_t k = (uint64_t)l->nprims + i;
    if (k >= (uint64_t)LS_PCHUNK * LS_PCHUNKS) {
        l->failed = 1;
        return NULL;
    }
    const uint32_t c = (uint32_t)(k / LS_PCHUNK);
    if (!l->prim[c]) {
        l->prim[c] = malloc(LS_PCHUNK * sizeof(struct EmGsPending));
        l->rows[c] = malloc(LS_PCHUNK * sizeof(LsRows));
        if (!l->prim[c] || !l->rows[c]) {
            free(l->prim[c]);
            free(l->rows[c]);
            l->prim[c] = NULL;
            l->rows[c] = NULL;
            l->failed = 1;
            return NULL;
        }
    }
    return ls_prim(l, (uint32_t)k);
}

/* The span queued in slots 0 .. n - 1 ends (span_end): its grid decision,
 * the CLUT and DIMX it is drawn with, its rows and pages, appended. */
static void list_close(EmGs *gs, uint32_t n, int stq_grid)
{
    EmGsList *l = gs->list;
    if (l->failed)
        return;
    if (l->nspans == LS_SCHUNK * LS_SCHUNKS) {
        l->failed = 1;
        return;
    }
    /* the CLUT the span is drawn with: a new snapshot when the parsing EmGs's
     * CLUT changed since the last one (or this list has none yet) */
    if (!l->ncluts || gs->clut_changes != l->snap_changes) {
        const uint32_t c = l->ncluts;
        if (c == LS_CCHUNK * LS_CCHUNKS) {
            l->failed = 1;
            return;
        }
        if (!l->clut[c / LS_CCHUNK] && !(l->clut[c / LS_CCHUNK] = malloc(LS_CCHUNK * sizeof *l->clut[0]))) {
            l->failed = 1;
            return;
        }
        memcpy(l->clut[c / LS_CCHUNK][c % LS_CCHUNK], gs->clut, sizeof gs->clut);
        l->ncluts++;
        l->snap_changes = gs->clut_changes;
    }
    const uint32_t s = l->nspans;
    if (!l->span[s / LS_SCHUNK] && !(l->span[s / LS_SCHUNK] = malloc(LS_SCHUNK * sizeof(LsSpan)))) {
        l->failed = 1;
        return;
    }
    LsSpan *sp = &l->span[s / LS_SCHUNK][s % LS_SCHUNK];
    sp->first = l->nprims;
    sp->n = n;
    sp->ylo = INT32_MAX;
    sp->yhi = INT32_MIN;
    sp->nw = 0;
    sp->w_all = 0;
    const Draw *q = NULL;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t k = l->nprims + i;
        struct EmGsPending *p = ls_prim(l, k);
        p->d.stq_grid = stq_grid;
        const LsRows r = list_rows(p);
        l->rows[k / LS_PCHUNK][k % LS_PCHUNK] = r;
        if (r.lo <= r.hi) {
            if (r.lo < sp->ylo) sp->ylo = r.lo;
            if (r.hi > sp->yhi) sp->yhi = r.hi;
        }
        span_writes_add(&p->d, &q, sp->w, &sp->nw, &sp->w_all);
    }
    span_writes_mask(sp->w, sp->nw, sp->w_all, sp->m);
    sp->clut = l->ncluts - 1;
    sp->clut_serial = l->snap_changes + 1;
    sp->dimx = gs->dimx;
    for (uint32_t j = 0; j < TC_PAGES / 64; j++) {
        l->written[j] |= sp->m[j];
        l->undrawn[j] |= sp->m[j];
    }
    l->nprims += n;
    l->nspans = s + 1;
}

void em_gs_list_rows(const EmGsList *l, uint32_t s0, uint32_t s1, int32_t *ylo, int32_t *yhi)
{
    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (uint32_t s = s0; l && s < s1 && s < l->nspans; s++) {
        const LsSpan *sp = ls_span(l, s);
        if (sp->ylo > sp->yhi)
            continue;
        if (sp->ylo < lo) lo = sp->ylo;
        if (sp->yhi > hi) hi = sp->yhi;
    }
    *ylo = lo;
    *yhi = hi;
}

/* The rows lo..hi miss this EmGs's band (conservative for interleaved bands:
 * those are drawn). */
static int list_misses(const EmGs *gs, int32_t lo, int32_t hi)
{
    if (lo > hi)
        return 1;
    return gs->band_rows && (hi < gs->band_y0 || lo > gs->band_y1);
}

/* The decoded texture cache's stamp of pages m (a span of their own). */
static void list_stamp(EmGs *gs, const uint64_t m[TC_PAGES / 64], const PgSpan *w, uint32_t nw, int w_all)
{
    Tc *t = tc_get(gs);
    if (!t)
        return;
    t->sigma++;
    t->nw = nw;
    t->w_all = w_all;
    if (nw)
        memcpy(t->w, w, nw * sizeof *w);
    t->q_valid = 0;
    tc_stamp(t, m);
}

uint32_t em_gs_list_draw(EmGs *gs, const EmGsList *l, uint32_t s0, uint32_t s1, int (*before)(void *, uint32_t),
                         void *ctx)
{
    if (!gs || !l)
        return s0;
    uint64_t pass[TC_PAGES / 64] = {0};
    int passed = 0;
    uint32_t s = s0;
    for (; s < s1; s++) {
        if (before && !before(ctx, s))
            break;
        gs->lsc.spans++;
        const LsSpan *sp = ls_span(l, s);
        if (list_misses(gs, sp->ylo, sp->yhi)) {
            if (!gs->tc_off) {
                for (uint32_t j = 0; j < TC_PAGES / 64; j++)
                    pass[j] |= sp->m[j];
                passed = 1;
            }
            continue;
        }
        if (!gs->tc_off) {
            if (passed) {
                list_stamp(gs, pass, NULL, 0, 0);
                memset(pass, 0, sizeof pass);
                passed = 0;
            }
            list_stamp(gs, sp->m, sp->w, sp->nw, sp->w_all);
        }
        if (gs->list_clut != sp->clut_serial) {
            memcpy(gs->clut, l->clut[sp->clut / LS_CCHUNK][sp->clut % LS_CCHUNK], sizeof gs->clut);
            gs->list_clut = sp->clut_serial;
            tc_clut_changed(gs);
        }
        gs->dimx = sp->dimx;
        gs->lsc.spans_drawn++;
        for (uint32_t i = sp->first; i < sp->first + sp->n; i++) {
            const LsRows *r = ls_rows(l, i);
            if (list_misses(gs, r->lo, r->hi))
                continue;
            struct EmGsPending q = *ls_prim(l, i);
            gs->lsc.prims++;
            draw_one(gs, &q);
        }
    }
    if (passed)
        list_stamp(gs, pass, NULL, 0, 0);
    return s;
}

void em_gs_list_pass(EmGs *gs, const EmGsList *l, uint32_t s0, uint32_t s1)
{
    if (!gs || !l || gs->tc_off)
        return;
    uint64_t pass[TC_PAGES / 64] = {0};
    int passed = 0;
    for (uint32_t s = s0; s < s1; s++) {
        const LsSpan *sp = ls_span(l, s);
        for (uint32_t j = 0; j < TC_PAGES / 64; j++)
            pass[j] |= sp->m[j];
        passed = 1;
    }
    if (passed)
        list_stamp(gs, pass, NULL, 0, 0);
}
