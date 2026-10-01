/* Effects of the ninth level (see em_level9_port.h, docs/LEVEL9_PORT.md):
 * 001DE920 and 001E5AC0 (run from the outdoor entry-8 load: a VU0 packet
 * builder and a weather strip pass) and 001E7050 (an emitter that follows
 * a source actor). All three are NEARMISS in the decomp: the translation
 * follows the instructions (read locally, never reproduced); the decomp's
 * C was a guide only.
 *
 * Stack frames: 001DE920 (sp - 0x140: the 001D6C90 stack arguments at
 * +0x00..+0x30, the saved packet pointer at +0xF0, the vector at +0x100,
 * the basis copy at +0x110), 001E5AC0 (sp - 0xF0: the strip descriptor at
 * +0x90) and 001E7050 (sp - 0xC0: the descriptor at +0x60) take `sp`, the
 * original's stack pointer at entry.
 */
#include "em_level9_port_internal.h"

#define D_00275670 0x00275670u /* pointer: the packet context */
#define D_0027568C 0x0027568Cu
#define D_00275698 0x00275698u
#define D_00253490 0x00253490u
#define S_70003AC0 0x70003AC0u
#define D_008106BF 0x008106BFu
#define D_007635C0 0x007635C0u
#define D_008105D0 0x008105D0u
#define D_008105D4 0x008105D4u
#define D_008105D8 0x008105D8u
#define D_00810610 0x00810610u
#define D_00255050 0x00255050u
#define D_002550E0 0x002550E0u
#define D_00255320 0x00255320u
#define F_2PI_L9 0x40C90FDBu /* 2 pi */
#define F_0_2_L9 0x3E4CCCCDu /* 0.2 */

/* ------------------------------------------------------------------------
 * 001DE920 (sp): the context c = D_00275670. 00102948(c +0x2450,
 * D_00810360); c +0x2460 = 2^28 / D_00275698; c +0x2464 = 1.0;
 * 00102948(v, c +0x2450) and 001026A0(v, 0x70003AC0, v) for the frame
 * vector v = sp - 0x40; v.z = (16 v.z) / v.w, then v.z / 4 (stored each
 * time); spread = ((float)D_008106BF / 127)^4 * 4; the packet start c
 * +0x1C saved at sp - 0x50. Two batches: 001D6B10(3, D_0027568C, 8, 8),
 * 001D2040(3, 2), 001D6BA0(3, D_0027568C, 8, 8, 0, 0), 001D1FF0(3, 0),
 * 001D6C90(3, 0, 1, 0, 0, 1, 0, 0; stack 1, 2, 0, 1, 0, 1, 0 as
 * doublewords); 16 groups each: the DMA tag at c +0x1C (byte +3 = 0x10,
 * word +4 = 0, halfword +0 = 0x14), c +0x1C += 0x150, a zero quadword at
 * +0x10, the GIF tag words +0x1C / +0x20 / +0x28; five randoms (u = r
 * 512, v = r 224, the base 253 << 20 r + 0x300000, two floats) give the
 * group's u / v; six particles (0x30 bytes each from +0x30) with the basis
 * D_00253490 copied to sp - 0x30 each time. Then 001D1F20(3), 001D1FF0(3,
 * 1), the closing tag (byte +3 = 0x60, +4 = 0, +0 = 0, c +0x1C += 0x10) and
 * 001CB760(0x7635C0, 0xFFF000, the saved start, 0x60).
 * ---------------------------------------------------------------------- */
static int32_t rand16(L9 *o)
{
    int32_t r = 0;
    l9_c_00122BB8(o, &r);
    return l9_sra((uint32_t)r, 16);
}

static void copy_quad(L9 *o, uint32_t dst, const uint8_t q[16]) { l9_wq(o, dst, q); }

int em_level9_port_001DE920(const EmLevel9PortHooks *h, uint32_t sp, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (l9_begin(&o, h, fault)) return -1;
    uint32_t frame = sp - 0x140;
    uint32_t v = frame + 0x100, basis = frame + 0x110, saved = frame + 0xF0;

    if (l9_c_00102948(&o, l9_u32(&o, D_00275670) + 0x2450, D_00810360)) return -1;
    {
        uint32_t d = l9_u32(&o, D_00275698);
        uint32_t c = l9_u32(&o, D_00275670);
        l9_w32(&o, c + 0x2460, L9_DIV(0x4D800000u /* 2^28 */, d));
        l9_w32(&o, l9_u32(&o, D_00275670) + 0x2464, F_ONE);
    }
    if (l9_c_00102948(&o, v, l9_u32(&o, D_00275670) + 0x2450)) return -1;
    if (l9_c_001026A0(&o, v, S_70003AC0, v)) return -1;
    {
        uint32_t w = l9_u32(&o, v + 0xC);
        uint32_t z = l9_u32(&o, v + 0x8);
        uint32_t q = L9_DIV(L9_MUL(F_16, z), w);
        l9_w32(&o, v + 0x8, q);
        l9_w32(&o, v + 0x8, L9_DIV(q, F_4));
    }
    uint32_t spread = L9_DIV(L9_CVT(l9_u8(&o, D_008106BF)), 0x42FE0000u /* 127 */);
    l9_w32(&o, saved, l9_u32(&o, l9_u32(&o, D_00275670) + 0x1C));
    spread = L9_MUL(spread, spread);
    spread = L9_MUL(spread, spread);
    spread = L9_MUL(spread, F_4);

    for (int batch = 0; batch < 2 && !l9_failed(&o); batch++) {
        if (l9_c_001D6B10(&o, 3, l9_s32(&o, D_0027568C), 8, 8)) return -1;
        if (l9_c_001D2040(&o, 3, 2)) return -1;
        if (l9_c_001D6BA0(&o, 3, l9_s32(&o, D_0027568C), 8, 8, 0, 0)) return -1;
        if (l9_c_001D1FF0(&o, 3, 0)) return -1;
        l9_w64(&o, frame + 0x00, 1);
        l9_w64(&o, frame + 0x08, 2);
        l9_w64(&o, frame + 0x10, 0);
        l9_w64(&o, frame + 0x18, 1);
        l9_w64(&o, frame + 0x20, 0);
        l9_w64(&o, frame + 0x28, 1);
        l9_w64(&o, frame + 0x30, 0);
        if (l9_c_001D6C90(&o, 3, 0, 1, 0, 0, 1, 0, 0)) return -1;
        for (int group = 0; group < 16 && !l9_failed(&o); group++) {
            uint32_t c = l9_u32(&o, D_00275670);
            l9_w8(&o, l9_u32(&o, c + 0x1C) + 3, 0x10);
            l9_w32(&o, l9_u32(&o, c + 0x1C) + 4, 0);
            l9_w16(&o, l9_u32(&o, c + 0x1C), 0x14);
            uint32_t rec = l9_u32(&o, c + 0x1C);
            l9_w32(&o, c + 0x1C, rec + 0x150);
            {
                static const uint8_t zero[16] = {0};
                copy_quad(&o, rec + 0x10, zero);
            }
            l9_w32(&o, rec + 0x1C, 0x50000013u);
            l9_w64(&o, rec + 0x20, 0x8006u | ((uint64_t)0x302EC000u << 32));
            l9_w64(&o, rec + 0x28, 0x421);
            uint32_t particle = rec + 0x30;

            int32_t ju = l9_sra((uint32_t)rand16(&o) << 9, 15);
            int32_t jv = l9_sra((uint32_t)rand16(&o) * 224u, 15);
            int32_t jb = l9_sra((uint32_t)rand16(&o) * (253u << 20), 15) + 0x300000;
            if (l9_c_00122BB8(&o, &r)) return -1;
            uint32_t f20 = L9_MUL(F_2PM31, L9_CVT(r));
            if (l9_c_00122BB8(&o, &r)) return -1;
            uint32_t f1 = L9_MUL(F_2PM31, L9_CVT(r));
            uint32_t f2 = L9_MUL(F_2, f20);
            uint32_t f0 = L9_MUL(F_2, f1);
            f2 = L9_ADD(0xBF800000u /* -1 */, f2);
            f2 = L9_MUL(spread, f2);
            f0 = L9_ADD(0xBF800000u /* -1 */, f0);
            uint32_t base_u = L9_ADD(L9_CVT(ju), f2);
            uint32_t base_v = L9_ADD(L9_CVT(jv), L9_MUL(spread, f0));

            for (int i = 0; i < 6 && !l9_failed(&o); i++, particle += 0x30) {
                uint8_t q0[16], q1[16], q2[16];
                uint32_t b = basis + 8u * (uint32_t)i;
                l9_q(&o, D_00253490 + 0x00, q0);
                l9_q(&o, D_00253490 + 0x10, q1);
                l9_q(&o, D_00253490 + 0x20, q2);
                copy_quad(&o, basis + 0x00, q0);
                copy_quad(&o, basis + 0x10, q1);
                copy_quad(&o, basis + 0x20, q2);
                l9_w32(&o, particle + 0x8, 0x80);
                l9_w32(&o, particle + 0x4, 0x80);
                l9_w32(&o, particle + 0x0, 0x80);
                l9_w32(&o, particle + 0xC, i == 0 ? 0x5Cu : 0u);
                l9_w32(&o, particle + 0x10, L9_DIV(L9_ADD(base_u, l9_u32(&o, b)), 0x44000000u /* 512 */));
                l9_w32(&o, particle + 0x14, L9_DIV(L9_ADD(base_v, l9_u32(&o, b + 4)), 0x43600000u /* 224 */));
                l9_w32(&o, particle + 0x18, F_ONE);
                if (l9_c_001281C0(&o, l9_u32(&o, b), &r)) return -1;
                l9_w32(&o, particle + 0x20, ((uint32_t)ju + 0x700u + (uint32_t)r) << 4);
                if (l9_c_001281C0(&o, l9_u32(&o, b + 4), &r)) return -1;
                l9_w32(&o, particle + 0x24, ((uint32_t)jv + 0x790u + (uint32_t)r) << 4);
                l9_w32(&o, particle + 0x28, (uint32_t)jb);
                l9_w32(&o, particle + 0x2C, 0);
            }
        }
    }
    if (l9_failed(&o)) return -1;
    if (l9_c_001D1F20(&o, 3)) return -1;
    if (l9_c_001D1FF0(&o, 3, 1)) return -1;
    {
        uint32_t c = l9_u32(&o, D_00275670);
        uint32_t start = l9_u32(&o, saved);
        l9_w8(&o, l9_u32(&o, c + 0x1C) + 3, 0x60);
        l9_w32(&o, l9_u32(&o, c + 0x1C) + 4, 0);
        l9_w16(&o, l9_u32(&o, c + 0x1C), 0);
        l9_w32(&o, c + 0x1C, l9_u32(&o, c + 0x1C) + 0x10);
        l9_c_001CB760(&o, D_007635C0, 0xFFF000u, start, 0x60);
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 001E5AC0 (self, flags, seed, dt; sp): two banks of three billboard
 * strips (e = self +0x1F0: [i] the strip's position along its texture,
 * [i + 6] its wave phase; +0x220 the fade timer). The camera point
 * D_008105D0 / D4 / D8 gives the texture offset 0x700038B0..B8: ((int(c) +
 * 100000) mod 160) + (c - int(c)) per axis (bank 2: mod 80, then / 2), with
 * int = 001281C0. Each strip i: 0x70003A20 = row[0] + 0.5 (row[0]
 * sin(2 pi e[i + 6])) (rows D_00255050 / D_002550E0, 0x30 bytes each); the
 * strip origin x = -160 (i / 3) (bank 2: -40 ((i - 3) / 3)) brought into
 * [0, 140) (bank 2: [0, 40)) then -70 (-20), y 0, z -130 (-30); the matrix
 * 0x700036A0 (identity, 00102B08 by pi 0x70003A20 / 180), 0x700036D0 =
 * that point + D_008105D0, plus (0, 0, 30) through 00102798(0x700036E0,
 * 0x810610) with its last row (0, 0, 0, 1); the row's tangents 00102900(
 * 0x254F70 / 0x255000, row +0x10, 1) and 00102900(0x254F50 / 0x254FE0, row
 * +0x20, 1.3 dt) copied to 0x254F80 / 0x255010 and 0x254F60 / 0x254FF0;
 * uv = ((seed >> 16) & 0xFFFF) / 65535, seed = 37 seed + 11;
 * 001CFAE0(desc, 0, 0x700036A0, e[i], uv + 0.0001, 1, 1e-6) and
 * 001CFFE0(3, 3, 0x254F30 / 0x254FC0, desc, 1); then with flags bit 1 the
 * phase -= 0.004 dt and e[i] -= 1.5 row[2] dt (below 1: += 1), else the
 * phase += 0.004 dt and e[i] += 1.5 row[2] dt (above 2: -= 1). After bank
 * 1: 0021B9A0(2, 0, 0), 0021B9A0(3, 0, 70); after bank 2: 0021B9A0(1, 0,
 * 0). Then 0x70003A20 = e +0x30 - 75; above 0: / 52, the colour 56 that
 * (x3, alpha 0) through 00103230(0x700038A0, 0x700038A0, dt), four
 * 00128250 bytes, 001E6F60(3, 0x7000, 0x7900, 0x9000, 0x8700, 0xFFFFFF,
 * colour).
 * ---------------------------------------------------------------------- */
static uint32_t fti(L9 *o, uint32_t f)
{
    int32_t r = 0;
    l9_c_001281C0(o, f, &r);
    return (uint32_t)r;
}

/* The texture offset of one bank: ((int(c) + 100000) mod m) + frac(c). */
static void texture_offset(L9 *o, int32_t m)
{
    uint32_t fx = l9_u32(o, D_008105D0);
    int32_t n = (int32_t)fti(o, fx);
    uint32_t fy = l9_u32(o, D_008105D4);
    uint32_t t0 = L9_CVT(l9_rem(n + 0x186A0, m));
    l9_w32(o, S_700038B0, t0);
    n = (int32_t)fti(o, fy);
    uint32_t fz = l9_u32(o, D_008105D8);
    uint32_t t1 = L9_CVT(l9_rem(n + 0x186A0, m));
    l9_w32(o, S_700038B4, t1);
    n = (int32_t)fti(o, fz);
    uint32_t t2 = L9_CVT(l9_rem(n + 0x186A0, m));
    l9_w32(o, S_700038B8, t2);
    n = (int32_t)fti(o, fx);
    l9_w32(o, S_700038B0, L9_ADD(l9_u32(o, S_700038B0), L9_SUB(fx, L9_CVT(n))));
    n = (int32_t)fti(o, fy);
    l9_w32(o, S_700038B4, L9_ADD(l9_u32(o, S_700038B4), L9_SUB(fy, L9_CVT(n))));
    n = (int32_t)fti(o, fz);
    l9_w32(o, S_700038B8, L9_ADD(l9_u32(o, S_700038B8), L9_SUB(fz, L9_CVT(n))));
}

static void strip(L9 *o, uint32_t p, uint32_t row, int32_t index, int bank2, uint32_t *seed, int mode, uint32_t dt,
                  uint32_t s13, uint32_t st, uint32_t desc)
{
    uint32_t s = 0;
    l9_w32(o, S_70003A20, l9_u32(o, row));
    if (l9_c_0011E2A8(o, L9_MUL(F_2PI_L9, l9_u32(o, p + 0x18)), &s)) return;
    {
        uint32_t r0 = l9_u32(o, row);
        uint32_t cur = l9_u32(o, S_70003A20);
        l9_w32(o, S_70003A20, L9_ADD(cur, L9_MUL(F_HALF, L9_MUL(r0, s))));
    }
    {
        uint32_t k = L9_DIV(L9_CVT(bank2 ? index - 3 : index), F_3);
        l9_w32(o, S_700038A0, L9_MUL(bank2 ? 0xC2200000u /* -40 */ : 0xC3200000u /* -160 */, k));
    }
    l9_w32(o, S_700038A4, 0);
    l9_w32(o, S_700038A8, bank2 ? 0xC1F00000u /* -30 */ : 0xC3020000u /* -130 */);
    l9_w32(o, S_700038AC, F_ONE);
    {
        uint32_t wrap = bank2 ? 0x42200000u /* 40 */ : 0x430C0000u /* 140 */;
        uint32_t x = l9_u32(o, S_700038A0);
        while (L9_LT(x, F_ZERO) && !l9_failed(o)) {
            l9_w32(o, S_700038A0, L9_ADD(l9_u32(o, S_700038A0), wrap));
            x = l9_u32(o, S_700038A0);
        }
        l9_w32(o, S_700038A0, L9_SUB(x, bank2 ? F_20 : 0x428C0000u /* 70 */));
    }
    if (l9_c_001029C0(o, S_700036A0)) return;
    {
        uint32_t a = L9_DIV(L9_MUL(F_PI, l9_u32(o, S_70003A20)), 0x43340000u /* 180 */);
        if (l9_c_00102B08(o, S_700036A0, S_700036A0, a)) return;
    }
    if (l9_c_001026A0(o, S_700036D0, S_700036A0, S_700038A0)) return;
    if (l9_c_001028B8(o, S_700036D0, S_700036D0, D_008105D0)) return;
    l9_w32(o, S_700038B0, 0);
    l9_w32(o, S_700038B4, 0);
    l9_w32(o, S_700038B8, 0x41F00000u /* 30 */);
    l9_w32(o, S_700038BC, F_ONE);
    if (l9_c_00102798(o, S_700036E0, D_00810610)) return;
    l9_w32(o, S_70003710, 0);
    l9_w32(o, S_70003710 + 4, 0);
    l9_w32(o, S_70003710 + 8, 0);
    l9_w32(o, S_70003710 + 12, F_ONE);
    if (l9_c_001026A0(o, S_700038B0, S_700036E0, S_700038B0)) return;
    if (l9_c_001028B8(o, S_700036D0, S_700036D0, S_700038B0)) return;
    l9_w32(o, S_700036DC, F_ONE);
    {
        uint32_t a = bank2 ? 0x00255000u : 0x00254F70u, b = bank2 ? 0x00254FE0u : 0x00254F50u;
        uint32_t ac = bank2 ? 0x00255010u : 0x00254F80u, bc = bank2 ? 0x00254FF0u : 0x00254F60u;
        if (l9_c_00102900(o, a, row + 0x10, F_ONE)) return;
        if (l9_c_00102900(o, b, row + 0x20, s13)) return;
        if (l9_c_00102948(o, ac, a)) return;
        if (l9_c_00102948(o, bc, b)) return;
    }
    {
        uint32_t uv = L9_DIV(L9_CVT((uint32_t)l9_sra(*seed, 16) & 0xFFFFu), 0x477FFF00u /* 65535 */);
        *seed = *seed * 0x25u + 0xBu;
        uint32_t pos = l9_u32(o, p);
        if (l9_c_001CFAE0(o, desc, 0, S_700036A0, pos, L9_ADD(uv, 0x38D1B717u /* 0.0001 */), F_ONE,
                          0x358637BDu /* 1e-6 */))
            return;
    }
    if (l9_c_001CFFE0(o, 3, 3, bank2 ? 0x00254FC0u : 0x00254F30u, desc, 1)) return;
    if (mode) {
        l9_w32(o, p + 0x18, L9_SUB(l9_u32(o, p + 0x18), st));
        uint32_t r2 = l9_u32(o, row + 8);
        uint32_t y = L9_SUB(l9_u32(o, p), L9_MUL(0x3FC00000u /* 1.5 */, L9_MUL(r2, dt)));
        l9_w32(o, p, y);
        if (L9_LT(y, F_ONE)) l9_w32(o, p, L9_ADD(l9_u32(o, p), F_ONE));
    } else {
        l9_w32(o, p + 0x18, L9_ADD(l9_u32(o, p + 0x18), st));
        uint32_t r2 = l9_u32(o, row + 8);
        uint32_t y = L9_ADD(l9_u32(o, p), L9_MUL(0x3FC00000u /* 1.5 */, L9_MUL(r2, dt)));
        l9_w32(o, p, y);
        if (!L9_LE(y, F_2)) l9_w32(o, p, L9_SUB(l9_u32(o, p), F_ONE));
    }
}

int em_level9_port_001E5AC0(const EmLevel9PortHooks *h, uint32_t self, int32_t flags, int32_t seed, float dt_f,
                            uint32_t sp, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    uint32_t dt = l9_bits(dt_f);
    uint32_t e = self + 0x1F0;
    uint32_t desc = sp - 0xF0 + 0x90;
    uint32_t s = (uint32_t)seed;
    int mode = (flags & 2) != 0;

    texture_offset(&o, 160);
    uint32_t s13 = L9_MUL(0x3FA66666u /* 1.3 */, dt);
    uint32_t st = L9_MUL(0x3B83126Fu /* 0.004 */, dt);
    for (int32_t i = 0; i < 3 && !l9_failed(&o); i++)
        strip(&o, e + 4u * (uint32_t)i, D_00255050 + 0x30u * (uint32_t)i, i, 0, &s, mode, dt, s13, st, desc);
    if (l9_failed(&o)) return -1;
    if (l9_c_0021B9A0(&o, 2, F_ZERO, F_ZERO)) return -1;
    if (l9_c_0021B9A0(&o, 3, F_ZERO, 0x428C0000u /* 70 */)) return -1;
    texture_offset(&o, 80);
    l9_w32(&o, S_700038B0, L9_DIV(l9_u32(&o, S_700038B0), F_2));
    l9_w32(&o, S_700038B4, L9_DIV(l9_u32(&o, S_700038B4), F_2));
    l9_w32(&o, S_700038B8, L9_DIV(l9_u32(&o, S_700038B8), F_2));
    for (int32_t i = 3; i < 6 && !l9_failed(&o); i++)
        strip(&o, e + 0xC + 4u * (uint32_t)(i - 3), D_002550E0 + 0x30u * (uint32_t)(i - 3), i, 1, &s, mode, dt, s13,
              st, desc);
    if (l9_failed(&o)) return -1;
    if (l9_c_0021B9A0(&o, 1, F_ZERO, F_ZERO)) return -1;
    {
        uint32_t y = L9_SUB(l9_u32(&o, e + 0x30), 0x42960000u /* 75 */);
        l9_w32(&o, S_70003A20, y);
        if (!L9_LE(y, F_ZERO)) {
            int32_t c0 = 0, c1 = 0, c2 = 0, c3 = 0;
            uint32_t k = L9_DIV(l9_u32(&o, S_70003A20), 0x42500000u /* 52 */);
            l9_w32(&o, S_70003A20, k);
            uint32_t v = L9_MUL(0x42600000u /* 56 */, k);
            l9_w32(&o, S_700038A0, v);
            l9_w32(&o, S_700038A4, v);
            l9_w32(&o, S_700038A8, v);
            l9_w32(&o, S_700038AC, 0);
            if (l9_c_00103230(&o, S_700038A0, S_700038A0, dt)) return -1;
            if (l9_c_00128250(&o, l9_u32(&o, S_700038A0), &c0)) return -1;
            if (l9_c_00128250(&o, l9_u32(&o, S_700038A4), &c1)) return -1;
            if (l9_c_00128250(&o, l9_u32(&o, S_700038A8), &c2)) return -1;
            if (l9_c_00128250(&o, l9_u32(&o, S_700038AC), &c3)) return -1;
            uint32_t col = (uint32_t)c0 | (uint32_t)c1 << 8 | (uint32_t)c2 << 16 | (uint32_t)c3 << 24;
            l9_c_001E6F60(&o, 3, 0x7000, 0x7900, 0x9000, 0x8700, 0xFFFFFF, (int32_t)col);
        }
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 001E7050 (self; sp): src = +0x24 (read first), tbl = self +0x1F0. By +4:
 *   0: for 8 slots j: tbl[j] = 00122BB8() mod src[0x0C] (signed; a zero
 *      divisor faults 7), tbl[j + 8] = 00122BB8() / 2^31, tbl[j + 16] the
 *      same; tbl +0x60 = 0; +4 = 1.
 *   1: src +4 == 3: +4 = 3. Else 00102948(self +0xB0, src +0x100), +0xB4
 *      += 8, h = 001CD070(self +0xB0, 0x30); h == 0xFFFFFF: stop;
 *      0x70003A20 = 001CD2B0(8, 16, 384, 128); 0 (equal): stop. For 8 slots:
 *      001029C0(0x700036A0); 00102948(0x700036D0, (src +0x110 + 4 tbl[j])
 *      +0xC0); 001CFAE0(desc, 0, 0x700036A0, tbl[j + 16], tbl[j + 8],
 *      0x70003A20, 1e-6); 001CFBE0(h, 0, 0x255320, desc, 0); tbl[j + 16] +=
 *      0.2, above 1.4: -= 1.4 and tbl[j] re-rolled.
 *   2, 3: 001AFC10(self). Others: nothing.
 * desc = sp - 0x60.
 * ---------------------------------------------------------------------- */
static int32_t roll_slot(L9 *o, uint32_t src)
{
    int32_t r = 0;
    l9_c_00122BB8(o, &r);
    int32_t m = (int32_t)l9_u8(o, src + 0x0C);
    if (m == 0) {
        l9_latch(o, 0x001E7050u, EM_LEVEL9_PORT_FAULT_REGISTER);
        return 0;
    }
    return l9_rem(r, m);
}

int em_level9_port_001E7050(const EmLevel9PortHooks *h, uint32_t self, uint32_t sp, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (l9_begin(&o, h, fault)) return -1;
    uint32_t src = l9_u32(&o, self + 0x24);
    uint32_t state = l9_u8(&o, self + 4);
    uint32_t tbl = self + 0x1F0;
    uint32_t desc = sp - 0x60;
    switch (state) {
    case 0:
        for (uint32_t j = 0; j < 8 && !l9_failed(&o); j++) {
            uint32_t p = tbl + 4u * j;
            int32_t idx = roll_slot(&o, src);
            if (l9_failed(&o)) break;
            l9_w32(&o, p, (uint32_t)idx);
            if (l9_c_00122BB8(&o, &r)) break;
            l9_w32(&o, p + 0x20, L9_DIV(L9_CVT(r), F_2P31));
            if (l9_c_00122BB8(&o, &r)) break;
            l9_w32(&o, p + 0x40, L9_DIV(L9_CVT(r), F_2P31));
        }
        if (l9_failed(&o)) break;
        l9_w32(&o, tbl + 0x60, 0);
        l9_w8(&o, self + 4, 1);
        break;
    case 1: {
        if (l9_u8(&o, src + 4) == 3u) {
            l9_w8(&o, self + 4, 3);
            break;
        }
        uint32_t f = 0;
        if (l9_c_00102948(&o, self + 0xB0, src + 0x100)) break;
        l9_w32(&o, self + 0xB4, L9_ADD(l9_u32(&o, self + 0xB4), F_8));
        if (l9_c_001CD070(&o, self + 0xB0, 0x30, &r)) break;
        int32_t handle = r;
        if ((uint32_t)handle == 0xFFFFFFu) break;
        if (l9_c_001CD2B0(&o, F_8, F_16, 0x43C00000u /* 384 */, F_128, &f)) break;
        l9_w32(&o, S_70003A20, f);
        if (L9_EQ(F_ZERO, l9_u32(&o, S_70003A20))) break;
        for (uint32_t j = 0; j < 8 && !l9_failed(&o); j++) {
            uint32_t p = tbl + 4u * j;
            if (l9_c_001029C0(&o, S_700036A0)) break;
            uint32_t bone = l9_u32(&o, src + 0x110 + 4u * l9_u32(&o, p));
            if (l9_c_00102948(&o, S_700036D0, bone + 0xC0)) break;
            uint32_t a = l9_u32(&o, p + 0x40), b = l9_u32(&o, p + 0x20), c = l9_u32(&o, S_70003A20);
            if (l9_c_001CFAE0(&o, desc, 0, S_700036A0, a, b, c, 0x358637BDu /* 1e-6 */)) break;
            if (l9_c_001CFBE0(&o, handle, 0, D_00255320, desc, 0)) break;
            uint32_t ph = L9_ADD(l9_u32(&o, p + 0x40), F_0_2_L9);
            l9_w32(&o, p + 0x40, ph);
            if (!L9_LE(ph, 0x3FB33333u /* 1.4 */)) {
                l9_w32(&o, p + 0x40, L9_SUB(l9_u32(&o, p + 0x40), 0x3FB33333u));
                int32_t idx = roll_slot(&o, src);
                if (l9_failed(&o)) break;
                l9_w32(&o, p, (uint32_t)idx);
            }
        }
        break;
    }
    case 2:
    case 3:
        l9_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l9_end(&o);
}
