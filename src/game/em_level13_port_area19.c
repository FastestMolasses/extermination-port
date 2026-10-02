/* The thirteenth level: the AREA19 overlay functions the a19c census found
 * new (overlay id 16, runtime addresses; the overlay is linked 0x40 below
 * where it runs). Sub 0: the effect 0x824A90 ([7]'s script) and the
 * callback 0x829840. Sub 1's placements: the flame columns 0x823780 ([40]),
 * the fire 0x823D10 ([39]), the valves 0x826570 ([38]) / 0x826840 ([37]),
 * the lift 0x826C10 ([36]) and its follower 0x827430 ([35]), the item giver
 * 0x827550 ([53]), the arrival script owner 0x8279E0 ([34]) with its
 * script callbacks 0x827B10 / 0x827B20, the 0x827B60 objects and 0x829A70
 * ([46]). docs/LEVEL13_PORT.md section 2. Ground truth: the original
 * instructions (the test runs every entry against them). */
#include "em_level13_port_internal.h"

#define F_ONE 0x3F800000u
#define F_PI 0x40490FDBu
#define F_HALF_PI 0x3FC90FDBu
#define F_300 0x43960000u
#define F_4096 0x45800000u
#define F_RAND_SCALE 0x477FFF00u /* 65535 */
#define F_RAND_BIAS 0x38D1B717u  /* 0.0001 */

/* The random fraction of a seed word: ((s >> 16) & 0xFFFF) / 65535, as the
 * originals compute it (cvt.s.w, div.s); the bias is added at the call. */
static uint32_t l13_seed_fraction(uint32_t seed)
{
    return L13_DIV(L13_CVT_S_W((uint32_t)l13_sra(seed, 16) & 0xFFFFu), F_RAND_SCALE);
}

/* The four words of a vector (x, y, z, 1.0) stored at a, in order. */
static void l13_vec4(L13 *o, uint32_t a, uint32_t x, uint32_t y, uint32_t z)
{
    l13_w32(o, a, x);
    l13_w32(o, a + 4, y);
    l13_w32(o, a + 8, z);
    l13_w32(o, a + 12, F_ONE);
}

/* ------------------------------------------------------------------------
 * 0x823780 (self, sp): [40], the two flame columns. +4 2 / 3:
 * 001FC520(blk +0x18), 001AFC10(self). 0: two random phases blk +8 / +0xC
 * (00122BB8() / 2^31) and seeds blk +0x10 / +0x14, the position (870, 370,
 * 910), +2 = 0xD, +3 = 3, +0xD = 0, +0x56 = 1, +0 = 2, +0x30 = 0x82AFE0,
 * +0x34 = 0x823770, blk +0x18 = -1, blk +0 / +4 = 0, +4 = 1, +5 = 0; with
 * D_0081077B and D_00810778 both 0xFF 001B6660(0x82AB00) and +4 = 3;
 * otherwise +5 = 1 when D_008107F8 != 0, then as 1. 1: D_00810702 8 / 5 /
 * 4 / 2 -> 001FC520(blk +0x18). +5 0: D_008107F8 != 0 -> +5 = 1,
 * 001F02C0(self +0xB0, 0x8E8, 300); +0 = 2. +5 1: per column k (z 938,
 * 880 at x 870, y 448) the matrix 0x700036A0 (identity, translated), the
 * packet model 0x82AF50 = (30, 0, 30, 1) scaled by blk +4, and four
 * 001CFA60 / 001CFBE0(0, 6, 0x82AF50, the packet at sp - 0x60, 1) with the
 * phase blk +8 + k/4 (wrapped above 2, kept in 0x70003A20) and the seed
 * fraction; then the phase += blk +0 (wrapped); blk +0 = 0.008, +4 = 1.0
 * (each clamped), 001FC3C0(self, blk +0x18, 0x8E9, 300, 4096), +0 = 1,
 * 001B17A0(self).
 * ---------------------------------------------------------------------- */
static void l13_823780_columns(L13 *o, uint32_t self, uint32_t sp)
{
    uint32_t blk = self + 0x1F0;
    for (uint32_t k = 0; k < 2; k++) {
        uint32_t q = blk + 4u * k;
        l13_vec4(o, 0x700038A0u, 0x44598000u, 0x43E00000u, (k & 1u) ? 0x445C0000u : 0x446A8000u);
        uint32_t seed = l13_u32(o, q + 0x10);
        if (l13_c_001029C0(o, 0x700036A0u)) return;
        if (l13_c_00102918(o, 0x700036A0u, 0x700036A0u, 0x700038A0u)) return;
        l13_w32(o, 0x0082AF50u, 0x41F00000u);
        l13_w32(o, 0x0082AF54u, 0);
        l13_w32(o, 0x0082AF58u, 0x41F00000u);
        l13_w32(o, 0x0082AF5Cu, F_ONE);
        if (l13_c_00103230(o, 0x0082AF50u, 0x0082AF50u, l13_u32(o, blk + 4))) return;
        for (int j = 0; j < 4; j++) {
            uint32_t step = L13_DIV(L13_CVT_S_W(k), 0x40800000u);
            uint32_t f = L13_ADD(l13_u32(o, q + 8), step);
            l13_w32(o, 0x70003A20u, f);
            if (!L13_LE(f, 0x40000000u)) f = L13_SUB(f, F_ONE);
            uint32_t r = l13_seed_fraction(seed);
            seed = seed * 37u + 11u;
            l13_w32(o, 0x70003A20u, f);
            if (l13_c_001CFA60(o, sp - 0x60u, 0x700036A0u, f, L13_ADD(r, F_RAND_BIAS))) return;
            if (l13_c_001CFBE0(o, 0, 6, 0x0082AF50u, sp - 0x60u, 1)) return;
        }
        uint32_t cur = l13_u32(o, q + 8);
        uint32_t ph = L13_ADD(cur, l13_u32(o, blk));
        l13_w32(o, q + 8, ph);
        if (!L13_LE(ph, 0x40000000u)) l13_w32(o, q + 8, L13_SUB(l13_u32(o, q + 8), F_ONE));
    }
    l13_w32(o, blk, 0x3C03126Fu);
    l13_w32(o, blk + 4, F_ONE);
    if (!L13_LT(l13_u32(o, blk), 0x3C03126Fu)) l13_w32(o, blk, 0x3C03126Fu);
    if (!L13_LE(l13_u32(o, blk + 4), F_ONE)) l13_w32(o, blk + 4, F_ONE);
    if (l13_c_001FC3C0(o, self, blk + 0x18, 0x8E9, F_300, F_4096)) return;
    l13_w8(o, self, 1);
    int32_t r;
    l13_c_001B17A0(o, self, &r);
}

static void l13_00823780(L13 *o, uint32_t self, uint32_t sp)
{
    uint32_t blk = self + 0x1F0;
    uint32_t st = l13_u8(o, self + 4);
    if (st == 3 || st == 2) {
        if (l13_c_001FC520(o, blk + 0x18)) return;
        l13_c_001AFC10(o, self);
        return;
    }
    if (st == 0) {
        for (uint32_t i = 0; i < 2; i++) {
            uint32_t p = blk + 4u * i;
            int32_t r = 0;
            if (l13_c_00122BB8(o, &r)) return;
            l13_w32(o, p + 8, L13_DIV(L13_CVT_S_W((uint32_t)r), 0x4F000000u));
            if (l13_c_00122BB8(o, &r)) return;
            l13_w32(o, p + 0x10, (uint32_t)r);
        }
        l13_vec4(o, self + 0xB0, 0x44598000u, 0x43B90000u, 0x44638000u);
        l13_w8(o, self + 2, 0xD);
        l13_w8(o, self + 3, 3);
        l13_w8(o, self + 0xD, 0);
        l13_w16(o, self + 0x56, 1);
        l13_w8(o, self, 2);
        l13_w32(o, self + 0x30, 0x0082AFE0u);
        l13_w32(o, self + 0x34, 0x00823770u);
        l13_w32(o, blk + 0x18, 0xFFFFFFFFu);
        l13_w32(o, blk, 0);
        l13_w32(o, blk + 4, 0);
        l13_w8(o, self + 4, 1);
        l13_w8(o, self + 5, 0);
        if (l13_u8(o, 0x0081077Bu) == 0xFFu && l13_u8(o, 0x00810778u) == 0xFFu) {
            if (l13_c_001B6660(o, 0x0082AB00u)) return;
            l13_w8(o, self + 4, 3);
            return;
        }
        if (l13_u8(o, 0x008107F8u) != 0) l13_w8(o, self + 5, 1);
    } else if (st != 1) {
        return;
    }
    uint32_t area = l13_u8(o, 0x00810702u);
    if (area == 8 || area == 5 || area == 4 || area == 2) {
        l13_c_001FC520(o, blk + 0x18);
        return;
    }
    uint32_t sub = l13_u8(o, self + 5);
    if (sub == 1) {
        l13_823780_columns(o, self, sp);
    } else if (sub == 0) {
        if (l13_u8(o, 0x008107F8u) != 0) {
            l13_w8(o, self + 5, 1);
            if (l13_c_001F02C0(o, self + 0xB0, 0x8E8, F_300)) return;
        }
        l13_w8(o, self, 2);
    }
}

int em_level13_port_00823780(const H13 *h, uint32_t self, uint32_t sp, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    l13_00823780(&o, self, sp);
    return l13_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x823D10 (self, sp): [39], the fire. The parent word +0x14 is read first.
 * +4 3: 001FC520(blk +0x20), 001AFC10(self). 0: the position (848, 377,
 * 920), +0x30 = parent +0x1F0, +0x34 = 0x823CA0, +0 = 1, blk +0x14 / +0x18
 * = 0, the seed blk +0x1C = 00122BB8(), the size blk +0x24 = 1, blk +0x28 /
 * +0x2C / +0x30 = 0, blk +0x20 = -1, the matrix self +0xD0 (identity,
 * translated), +0xC = +9 = 0, +4 = 1; D_008107FB == 0xFF or D_008107F8 !=
 * 0 -> +4 = 3. 1 / 2 (and 0 after its setup): nothing while D_00810702 is
 * 5, 4 or 2; D_008107F8 != 0 -> +4 = 2; h = 001CCF70(self +0xB0), the
 * model index 1 (h <= 1000000) or 6; three flame packets (x 848, y 370, z
 * 940 / 920 / 900: matrices 0x700036A0 / 0x700036E0 translated, their y
 * -4 / -6, 001CFB50(packet, 0, 0x700036E0, blk +0x28, fraction, 1, 0.2,
 * 20), 001CFBE0(h, 1, 0x82B120, packet, 0)); 0021B9A0(2 / 3, 1, 150);
 * with the size nonzero and D_008107F9 bit 7 clear three smoke columns (y
 * 377: y +2 / -6; per j 1..3 the model 0x82B000 sized by 4 j and the size,
 * 001CFB50 at 0x700036A0 with blk +0x2C, 001CFBE0(h, k, 0x82B000, packet,
 * 1); then 0x82B090 sized, 001CFB50 at 0x700036E0 with blk +0x30,
 * 001CFBE0(h, k, 0x82B090, packet, 1)); 0021B9A0(1, 0, 0); +0 = 1, or 2
 * while the countdown blk +0x14 runs (and in state 2); the phases blk +0x2C
 * += 0.015 / +0x30 += 0.0075 (wrapped above 2); in state 2 the frame count
 * blk +0x18, past 60 the size -1/240 a frame (floor 0) and blk +0x28 +=
 * 0.01, past 5 +4 = 3 (return); state 1 001FC3C0(self, blk +0x20, 0x413,
 * 300, 4096); blk +0 / +4 / +8 = (18, 45 size, 35); 001B1B70(self).
 * ---------------------------------------------------------------------- */
static const uint32_t L13_FIRE_Z[3] = {0x446B0000u, 0x44660000u, 0x44610000u};

static int l13_823D10_flame(L13 *o, uint32_t blk, uint32_t sp, int32_t h, uint32_t *seed)
{
    for (int i = 0; i < 3; i++) {
        l13_vec4(o, 0x700038A0u, 0x44540000u, 0x43B90000u, L13_FIRE_Z[i]);
        if (l13_c_001029C0(o, 0x700036A0u)) return -1;
        if (l13_c_001029C0(o, 0x700036E0u)) return -1;
        if (l13_c_00102918(o, 0x700036A0u, 0x700036A0u, 0x700038A0u)) return -1;
        if (l13_c_00102918(o, 0x700036E0u, 0x700036E0u, 0x700038A0u)) return -1;
        uint32_t ya = l13_u32(o, 0x700036D4u);
        uint32_t yb = l13_u32(o, 0x70003714u);
        uint32_t r = l13_seed_fraction(*seed);
        l13_w32(o, 0x700036D4u, L13_SUB(ya, 0x40800000u));
        l13_w32(o, 0x70003714u, L13_SUB(yb, 0x40C00000u));
        *seed = *seed * 37u + 11u;
        uint32_t ph = l13_u32(o, blk + 0x28);
        if (l13_c_001CFB50(o, sp - 0x60u, 0, 0x700036E0u, ph, L13_ADD(r, F_RAND_BIAS), F_ONE, 0x3E4CCCCDu,
                           0x41A00000u)) return -1;
        if (l13_c_001CFBE0(o, h, 1, 0x0082B120u, sp - 0x60u, 0)) return -1;
    }
    return 0;
}

static int l13_823D10_smoke(L13 *o, uint32_t blk, uint32_t sp, int32_t h, int32_t k, uint32_t *seed)
{
    for (int i = 0; i < 3; i++) {
        l13_vec4(o, 0x700038A0u, 0x44540000u, 0x43BC8000u, L13_FIRE_Z[i]);
        if (l13_c_001029C0(o, 0x700036A0u)) return -1;
        if (l13_c_001029C0(o, 0x700036E0u)) return -1;
        if (l13_c_00102918(o, 0x700036A0u, 0x700036A0u, 0x700038A0u)) return -1;
        if (l13_c_00102918(o, 0x700036E0u, 0x700036E0u, 0x700038A0u)) return -1;
        uint32_t ya = l13_u32(o, 0x700036D4u);
        uint32_t yb = l13_u32(o, 0x70003714u);
        l13_w32(o, 0x700036D4u, L13_ADD(ya, 0x40000000u));
        l13_w32(o, 0x70003714u, L13_SUB(yb, 0x40C00000u));
        for (uint32_t j = 1; j < 4; j++) {
            uint32_t f = L13_MUL(0x40800000u, L13_CVT_S_W(j));
            l13_w32(o, 0x0082B010u, f);
            l13_w32(o, 0x0082B018u, f);
            uint32_t r = l13_seed_fraction(*seed);
            *seed = *seed * 37u + 11u;
            l13_w32(o, 0x0082B014u, L13_MUL(0x41200000u, l13_u32(o, blk + 0x24)));
            l13_w32(o, 0x0082B004u, L13_MUL(0x41700000u, l13_u32(o, blk + 0x24)));
            l13_w32(o, 0x0082B040u, L13_MUL(0x41000000u, l13_u32(o, blk + 0x24)));
            l13_w32(o, 0x0082B044u, L13_MUL(0x41000000u, l13_u32(o, blk + 0x24)));
            l13_w32(o, 0x0082B050u, L13_MUL(0x41000000u, l13_u32(o, blk + 0x24)));
            l13_w32(o, 0x0082B054u, L13_MUL(0x41000000u, l13_u32(o, blk + 0x24)));
            uint32_t ph = l13_u32(o, blk + 0x2C);
            if (l13_c_001CFB50(o, sp - 0x60u, 0, 0x700036A0u, ph, L13_ADD(r, F_RAND_BIAS), F_ONE, 0x3E4CCCCDu,
                               0x41A00000u)) return -1;
            if (l13_c_001CFBE0(o, h, k, 0x0082B000u, sp - 0x60u, 1)) return -1;
        }
        l13_w32(o, 0x0082B0A4u, 0);
        l13_w32(o, 0x0082B094u, L13_MUL(0x42200000u, l13_u32(o, blk + 0x24)));
        uint32_t r = l13_seed_fraction(*seed);
        *seed = *seed * 37u + 11u;
        l13_w32(o, 0x0082B0D0u, L13_MUL(0x41A00000u, l13_u32(o, blk + 0x24)));
        l13_w32(o, 0x0082B0D4u, L13_MUL(0x41A00000u, l13_u32(o, blk + 0x24)));
        l13_w32(o, 0x0082B0E0u, L13_MUL(0x41A00000u, l13_u32(o, blk + 0x24)));
        l13_w32(o, 0x0082B0E4u, L13_MUL(0x41F00000u, l13_u32(o, blk + 0x24)));
        uint32_t ph = l13_u32(o, blk + 0x30);
        if (l13_c_001CFB50(o, sp - 0x60u, 0, 0x700036E0u, ph, L13_ADD(r, F_RAND_BIAS), F_ONE, 0x3E4CCCCDu,
                           0x41A00000u)) return -1;
        if (l13_c_001CFBE0(o, h, k, 0x0082B090u, sp - 0x60u, 1)) return -1;
    }
    return 0;
}

static void l13_00823D10(L13 *o, uint32_t self, uint32_t sp)
{
    uint32_t blk = self + 0x1F0;
    uint32_t parent = l13_u32(o, self + 0x14);
    uint32_t st = l13_u8(o, self + 4);
    if (st == 3) {
        if (l13_c_001FC520(o, blk + 0x20)) return;
        l13_c_001AFC10(o, self);
        return;
    }
    if (st == 0) {
        l13_vec4(o, self + 0xB0, 0x44540000u, 0x43BC8000u, 0x44660000u);
        l13_w32(o, self + 0x30, parent + 0x1F0);
        l13_w32(o, self + 0x34, 0x00823CA0u);
        l13_w8(o, self, 1);
        l13_w32(o, blk + 0x14, 0);
        l13_w32(o, blk + 0x18, 0);
        int32_t r = 0;
        if (l13_c_00122BB8(o, &r)) return;
        l13_w32(o, blk + 0x1C, (uint32_t)r);
        l13_w32(o, blk + 0x24, F_ONE);
        l13_w32(o, blk + 0x28, 0);
        l13_w32(o, blk + 0x2C, 0);
        l13_w32(o, blk + 0x30, 0);
        l13_w32(o, blk + 0x20, 0xFFFFFFFFu);
        if (l13_c_001029C0(o, self + 0xD0)) return;
        if (l13_c_00102918(o, self + 0xD0, self + 0xD0, self + 0xB0)) return;
        l13_w8(o, self + 0xC, 0);
        l13_w8(o, self + 9, 0);
        l13_w8(o, self + 4, 1);
        if (l13_u8(o, 0x008107FBu) == 0xFFu || l13_u8(o, 0x008107F8u) != 0) {
            l13_w8(o, self + 4, 3);
            return;
        }
    } else if (st != 1 && st != 2) {
        return;
    }
    uint32_t area = l13_u8(o, 0x00810702u);
    if (area == 5 || area == 4 || area == 2) return;
    if (l13_u8(o, 0x008107F8u) != 0) l13_w8(o, self + 4, 2);
    uint32_t seed = l13_u32(o, blk + 0x1C);
    int32_t h = 0;
    if (l13_c_001CCF70(o, self + 0xB0, &h)) return;
    int32_t k = h < 0xF4241 ? 1 : 6;
    if (l13_823D10_flame(o, blk, sp, h, &seed)) return;
    if (l13_c_0021B9A0(o, 2, F_ONE, 0x43160000u)) return;
    if (l13_c_0021B9A0(o, 3, F_ONE, 0x43160000u)) return;
    if (!L13_EQ(0, l13_u32(o, blk + 0x24)) && !(l13_u8(o, 0x008107F9u) & 0x80u)) {
        if (l13_823D10_smoke(o, blk, sp, h, k, &seed)) return;
    }
    if (l13_c_0021B9A0(o, 1, 0, 0)) return;
    uint32_t cd = l13_u32(o, blk + 0x14);
    if (cd != 0) {
        l13_w32(o, blk + 0x14, cd - 1u);
        l13_w8(o, self, 2);
    } else {
        l13_w8(o, self, 1);
    }
    if (l13_u8(o, self + 4) == 2) l13_w8(o, self, 2);
    l13_w32(o, blk + 0x2C, L13_ADD(l13_u32(o, blk + 0x2C), 0x3C75C28Fu));
    l13_w32(o, blk + 0x30, L13_ADD(l13_u32(o, blk + 0x30), 0x3BF5C28Fu));
    {
        uint32_t v = l13_u32(o, blk + 0x2C);
        if (!L13_LE(v, 0x40000000u)) l13_w32(o, blk + 0x2C, L13_SUB(v, F_ONE));
        v = l13_u32(o, blk + 0x30);
        if (!L13_LE(v, 0x40000000u)) l13_w32(o, blk + 0x30, L13_SUB(v, F_ONE));
    }
    if (l13_u8(o, self + 4) == 2) {
        l13_w32(o, blk + 0x18, l13_u32(o, blk + 0x18) + 1u);
        if (l13_s32(o, blk + 0x18) >= 0x3D) {
            uint32_t sz = L13_SUB(l13_u32(o, blk + 0x24), 0x3B888889u);
            l13_w32(o, blk + 0x24, sz);
            if (L13_LT(sz, 0)) l13_w32(o, blk + 0x24, 0);
            uint32_t t = L13_ADD(l13_u32(o, blk + 0x28), 0x3C23D70Au);
            l13_w32(o, blk + 0x28, t);
            if (!L13_LE(t, 0x40A00000u)) {
                l13_w8(o, self + 4, 3);
                return;
            }
        }
    }
    if (l13_u8(o, self + 4) == 1) {
        if (l13_c_001FC3C0(o, self, blk + 0x20, 0x413, F_300, F_4096)) return;
    }
    l13_w32(o, blk, 0x41900000u);
    l13_w32(o, blk + 4, L13_MUL(0x42340000u, l13_u32(o, blk + 0x24)));
    l13_w32(o, blk + 8, 0x420C0000u);
    l13_c_001B1B70(o, self);
}

int em_level13_port_00823D10(const H13 *h, uint32_t self, uint32_t sp, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    l13_00823D10(&o, self, sp);
    return l13_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x824A90 (self, sp): an effect started by [7]'s script. +4 2 / 3:
 * 001AFC10(self). 0: the matrix self +0xD0 (identity, rotated by +0xC0,
 * translated by +0xB0), the phase blk +0 = 0, blk +4 = 00122BB8() / 2^31,
 * +4 = 1, then as 1. 1: 001CFA60(the packet at sp - 0x60, self +0xD0, blk
 * +0, blk +4), 001CFBE0(001CCF70(self +0x100), 1, 0x82B2D0, packet, 0);
 * blk +0 += 0.02, past 1.5 +4 = 3.
 * ---------------------------------------------------------------------- */
int em_level13_port_00824A90(const H13 *h, uint32_t self, uint32_t sp, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    uint32_t blk = self + 0x1F0;
    uint32_t st = l13_u8(&o, self + 4);
    if (st == 3 || st == 2) {
        l13_c_001AFC10(&o, self);
        return l13_end(&o);
    }
    if (st == 0) {
        if (l13_c_001029C0(&o, self + 0xD0)) return -1;
        if (l13_c_00102C58(&o, self + 0xD0, self + 0xD0, self + 0xC0)) return -1;
        if (l13_c_00102918(&o, self + 0xD0, self + 0xD0, self + 0xB0)) return -1;
        l13_w32(&o, blk, 0);
        int32_t r = 0;
        if (l13_c_00122BB8(&o, &r)) return -1;
        l13_w32(&o, blk + 4, L13_DIV(L13_CVT_S_W((uint32_t)r), 0x4F000000u));
        l13_w8(&o, self + 4, 1);
    } else if (st != 1) {
        return l13_end(&o);
    }
    int32_t hd = 0;
    if (l13_c_001CCF70(&o, self + 0x100, &hd)) return -1;
    uint32_t t = l13_u32(&o, blk);
    uint32_t s = l13_u32(&o, blk + 4);
    if (l13_c_001CFA60(&o, sp - 0x60u, self + 0xD0, t, s)) return -1;
    if (l13_c_001CFBE0(&o, hd, 1, 0x0082B2D0u, sp - 0x60u, 0)) return -1;
    t = L13_ADD(l13_u32(&o, blk), 0x3CA3D70Au);
    l13_w32(&o, blk, t);
    if (!L13_LE(t, 0x3FC00000u)) l13_w8(&o, self + 4, 3);
    return l13_end(&o);
}

/* The light both valves place: 0x700038B0 = 0, the point (x, 384.9, z, 1)
 * at 0x700038A0, the colour words 0x700038B4 / B8 / BC = 0x80, 0, 0x80,
 * then 001F4BF0(0x700038A0, 0x700038B0). */
static int l13_valve_light(L13 *o, uint32_t x, uint32_t z)
{
    l13_w32(o, 0x700038B0u, 0);
    l13_vec4(o, 0x700038A0u, x, 0x43C07333u, z);
    l13_w32(o, 0x700038B4u, 0x80);
    l13_w32(o, 0x700038B8u, 0);
    l13_w32(o, 0x700038BCu, 0x80);
    return l13_c_001F4BF0(o, 0x700038A0u, 0x700038B0u);
}

/* The talk point both valves give 001B6F00: (0, 0, z, 1) at 0x700038A0,
 * the yaw pi. */
static int l13_valve_face(L13 *o, uint32_t self, uint32_t z)
{
    l13_vec4(o, 0x700038A0u, 0, 0, z);
    return l13_c_001B6F00(o, self, 0x700038A0u, F_PI);
}

/* +0x28 counts to 120 (halfword), 001FBD50(self, 0x19A, 0, 300) on
 * arriving; then the script's end (001BA1F0). Returns the 001BA1F0 result,
 * or -1 after a fault. */
static int32_t l13_valve_count(L13 *o, uint32_t self)
{
    int32_t t = l13_s16(o, self + 0x28);
    if (t < 0x78) {
        l13_w16(o, self + 0x28, (uint32_t)(t + 1));
        if (l13_s16(o, self + 0x28) == 0x78) {
            if (l13_c_001FBD50(o, self, 0x19A, 0, F_300)) return -1;
        }
    }
    int32_t done = 0;
    if (l13_c_001BA1F0(o, self, &done)) return -1;
    return done;
}

/* ------------------------------------------------------------------------
 * 0x826570 (self): [38], the valve that ends the fires. +4 3:
 * 001AFC10(self). 2: 001B17A0(self) set -> the +0x4C method. 0: when
 * 001B0FD0(self) returns 0: 001C6380(self); 001BA1C0(self, 0x20) or (0x23)
 * set -> +4 = 2; otherwise 0019C6F0(7, 1), +0x30 = 0x82CC60, +4 = 1, +0 =
 * 1. 1: +5 0 on +0xB bit 2: +0x28 = 0, 001B6F00(self, (0, 0, 5, 1), pi),
 * 001BA1A0(blk, script 0x82CA20), +5 = 1. +5 1: the count to 120 (sound
 * 0x19A); at the script's end 001F6BA0(), 0019C6F0(7, 0), D_00810778 =
 * D_008107F8 = 0xFF, 001B6660(0x82AB00) when D_0081077B == 0xFF, +2 = 4,
 * +4 = 2. Then 001B17A0 (set -> the +0x4C method) and the light at
 * (890.4, 836.37).
 * ---------------------------------------------------------------------- */
int em_level13_port_00826570(const H13 *h, uint32_t self, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    uint32_t blk = self + 0x1F0;
    uint32_t st = l13_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 3) {
        l13_c_001AFC10(&o, self);
    } else if (st == 2) {
        if (l13_c_001B17A0(&o, self, &r)) return -1;
        if (r != 0) l13_method(&o, self);
    } else if (st == 0) {
        if (l13_c_001B0FD0(&o, self, &r)) return -1;
        if (r != 0) return l13_end(&o);
        if (l13_c_001C6380(&o, self)) return -1;
        if (l13_c_001BA1C0(&o, self, 0x20, &r)) return -1;
        if (r == 0) {
            if (l13_c_001BA1C0(&o, self, 0x23, &r)) return -1;
        }
        if (r != 0) {
            l13_w8(&o, self + 4, 2);
        } else {
            if (l13_c_0019C6F0(&o, 7, 1)) return -1;
            l13_w32(&o, self + 0x30, 0x0082CC60u);
            l13_w8(&o, self + 4, 1);
            l13_w8(&o, self, 1);
        }
    } else if (st == 1) {
        uint32_t sub = l13_u8(&o, self + 5);
        if (sub == 1) {
            int32_t done = l13_valve_count(&o, self);
            if (done < 0) return -1;
            if (done != 0) {
                if (l13_c_001F6BA0(&o)) return -1;
                if (l13_c_0019C6F0(&o, 7, 0)) return -1;
                l13_w8(&o, 0x00810778u, 0xFF);
                l13_w8(&o, 0x008107F8u, 0xFF);
                if (l13_u8(&o, 0x0081077Bu) == 0xFFu) {
                    if (l13_c_001B6660(&o, 0x0082AB00u)) return -1;
                }
                l13_w8(&o, self + 2, 4);
                l13_w8(&o, self + 4, 2);
            }
        } else if (sub == 0) {
            if (l13_u8(&o, self + 0xB) & 4u) {
                l13_w16(&o, self + 0x28, 0);
                if (l13_valve_face(&o, self, 0x40A00000u)) return -1;
                if (l13_c_001BA1A0(&o, blk, 0x0082CA20u)) return -1;
                l13_w8(&o, self + 5, 1);
            }
        }
        if (l13_c_001B17A0(&o, self, &r)) return -1;
        if (r != 0) {
            if (l13_method(&o, self)) return -1;
        }
        l13_valve_light(&o, 0x445E999Au, 0x445117AEu);
    }
    return l13_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x826840 (self): [37], the valve that sends the lift. +4 3 and others
 * above 2: 001AFC10(self). 2: 001B17A0(self), the +0x4C method. 0: when
 * 001B0FD0(self) returns 0: 001C6380(self), +0 = 1; D_00810779 == 0xFF ->
 * +2 = 4, +4 = 2; otherwise +8 = 1, +0x30 = 0x82D270, +4 = 1. 1:
 * D_00810779 == 0xFF -> +2 = 4, +4 = 2 (nothing else). +5 0 on +0xB bit 2:
 * +0x28 = 0; script 0x82CFF0 when D_008107F9 & 0xF, else script 0x82CC70
 * and D_008107F9 |= 0x80 (001BA1A0(blk, script)); 001B6F00(self, (0, 0,
 * 6.2, 1), pi); the +0x18 object's +0x2EC = 0x8A; +5 += 1. +5 1: the
 * count to 120; at the script's end +5 = +0xB = 0 and D_008107F9 &= 0x7F.
 * Then 001B17A0(self), the +0x4C method and the light at (819.4, 836.35).
 * ---------------------------------------------------------------------- */
int em_level13_port_00826840(const H13 *h, uint32_t self, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    uint32_t blk = self + 0x1F0;
    uint32_t st = l13_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 2) {
        if (l13_c_001B17A0(&o, self, &r)) return -1;
        l13_method(&o, self);
    } else if (st == 1) {
        if (l13_u8(&o, 0x00810779u) == 0xFFu) {
            l13_w8(&o, self + 2, 4);
            l13_w8(&o, self + 4, 2);
            return l13_end(&o);
        }
        uint32_t sub = l13_u8(&o, self + 5);
        if (sub == 1) {
            int32_t done = l13_valve_count(&o, self);
            if (done < 0) return -1;
            if (done != 0) {
                l13_w8(&o, self + 5, 0);
                l13_w8(&o, self + 0xB, 0);
                l13_w8(&o, 0x008107F9u, l13_u8(&o, 0x008107F9u) & 0x7Fu);
            }
        } else if (sub == 0) {
            if (l13_u8(&o, self + 0xB) & 4u) {
                l13_w16(&o, self + 0x28, 0);
                if (l13_u8(&o, 0x008107F9u) & 0xFu) {
                    if (l13_c_001BA1A0(&o, blk, 0x0082CFF0u)) return -1;
                } else {
                    if (l13_c_001BA1A0(&o, blk, 0x0082CC70u)) return -1;
                    l13_w8(&o, 0x008107F9u, l13_u8(&o, 0x008107F9u) | 0x80u);
                }
                if (l13_valve_face(&o, self, 0x40C66666u)) return -1;
                l13_w32(&o, l13_u32(&o, self + 0x18) + 0x2EC, 0x8A);
                l13_w8(&o, self + 5, l13_u8(&o, self + 5) + 1u);
            }
        }
        if (l13_c_001B17A0(&o, self, &r)) return -1;
        if (l13_method(&o, self)) return -1;
        l13_valve_light(&o, 0x444CD99Au, 0x44511666u);
    } else if (st == 0) {
        if (l13_c_001B0FD0(&o, self, &r)) return -1;
        if (r != 0) return l13_end(&o);
        if (l13_c_001C6380(&o, self)) return -1;
        l13_w8(&o, self, 1);
        if (l13_u8(&o, 0x00810779u) == 0xFFu) {
            l13_w8(&o, self + 2, 4);
            l13_w8(&o, self + 4, 2);
        } else {
            l13_w8(&o, self + 8, 1);
            l13_w32(&o, self + 0x30, 0x0082D270u);
            l13_w8(&o, self + 4, 1);
        }
    } else {
        l13_c_001AFC10(&o, self);
    }
    return l13_end(&o);
}

/* The lift's four corner lights: 0x700038AC = 1, then for (+-28.5, +8,
 * +-16) around the position 001F5940(3, 0x700038A0, 0), in the order (+,
 * +), (+, -), (-, -), (-, +). */
static void l13_lift_lights(L13 *o, uint32_t self)
{
    l13_w32(o, 0x700038ACu, F_ONE);
    for (int c = 0; c < 4; c++) {
        uint32_t x = l13_u32(o, self + 0xB0);
        l13_w32(o, 0x700038A0u, c < 2 ? L13_ADD(0x41E40000u, x) : L13_SUB(x, 0x41E40000u));
        l13_w32(o, 0x700038A4u, L13_ADD(0x41000000u, l13_u32(o, self + 0xB4)));
        uint32_t z = l13_u32(o, self + 0xB8);
        l13_w32(o, 0x700038A8u, (c == 0 || c == 3) ? L13_ADD(0x41800000u, z) : L13_SUB(z, 0x41800000u));
        if (l13_c_001F5940(o, 3, 0x700038A0u, 0)) return;
    }
}

/* ------------------------------------------------------------------------
 * 0x826C10 (self): [36], the lift. +4 3 and others: 001AFC10(self). 2:
 * 001B1B70(self), the +0x4C method. 0: when 001B0FD0(self) returns 0: the
 * rest height +0x2E8 = +0xB4, +0x2EC = 0; D_00810779 == 0xFF ->
 * 001C6380(self), +4 = 2; otherwise (+0xB4 = 15 + +0x2E8 when D_008107F9
 * & 0xF) 001C6380(self), +4 = 4, 0019C6F0(0x15, 1); then 001A2370(self,
 * self +0xD0). 4: +5 0: with the countdown +0x2EC set: D_00810779 == 0xFF
 * -> +0x2EC = 0; otherwise it counts down, and at 0: the speed +0x2E4 =
 * -1/12 (D_008107F9 & 0xF, 001FBD50(self, 0x8EB, 0, 300)) or 1/12 (sound
 * 0x8EA), +5 += 1, +0x28 = 0. With it clear: the player (D_00810350 /
 * D_00810358) in 850 < x < 859.6, 850.5 < z < 855 -> +0x2E4 = +0x2E0 = 0,
 * +0x28 = 0, 001BA1A0(blk, script 0x82D290), +4 = 1. +5 1: +0x28 += 1;
 * below 180 +0xB4 += +0x2E4, 001C6380, 001A2370; at 180 the low nibble of
 * D_008107F9 toggles between 0 and 1 (the high nibble kept), +0xB4 = 15 +
 * +0x2E8 (nibble set) or +0x2E8, +5 = 0, 001C6380, 001A2370. Then
 * 001B1B70(self), the +0x4C method and the four lights. 1: at the
 * script's end D_00810779 = 0xFF, 0019C6F0(0x15, 0), +4 = 2. Otherwise
 * once +0x28 is set (by the script callback 0x827540) it counts to 70
 * (then 001FBD50(self, 0x8EC, 0, 300), 001B1E20(5, 120)); from 70 the drop:
 * above the rest height +0x2E4 += 0.2 and +0xB4 -= it (001C6380,
 * 001A2370), at or below it +0xB4 = +0x2E8 and, once (+0x2E0 clear),
 * 001B1E20(8, 20), +0x2E0 = 1. Then 001B1B70(self), the +0x4C method and,
 * above the rest height, the four lights.
 * ---------------------------------------------------------------------- */
static void l13_826C10_lift(L13 *o, uint32_t self)
{
    uint32_t blk = self + 0x1F0;
    uint32_t sub = l13_u8(o, self + 5);
    if (sub == 1) {
        l13_w16(o, self + 0x28, l13_u16(o, self + 0x28) + 1u);
        if (l13_s16(o, self + 0x28) < 0xB4) {
            uint32_t v = l13_u32(o, self + 0x2E4);
            l13_w32(o, self + 0xB4, L13_ADD(l13_u32(o, self + 0xB4), v));
            if (l13_c_001C6380(o, self)) return;
            if (l13_c_001A2370(o, self, self + 0xD0)) return;
        } else {
            uint32_t f = l13_u8(o, 0x008107F9u);
            l13_w8(o, 0x008107F9u, (f & 0xF0u) | ((f & 0xFu) != 0 ? 0u : 1u));
            if (l13_u8(o, 0x008107F9u) & 0xFu) {
                l13_w32(o, self + 0xB4, L13_ADD(0x41700000u, l13_u32(o, self + 0x2E8)));
            } else {
                l13_w32(o, self + 0xB4, l13_u32(o, self + 0x2E8));
            }
            l13_w8(o, self + 5, 0);
            if (l13_c_001C6380(o, self)) return;
            if (l13_c_001A2370(o, self, self + 0xD0)) return;
        }
    } else if (sub == 0) {
        if (l13_u32(o, self + 0x2EC) != 0) {
            if (l13_u8(o, 0x00810779u) == 0xFFu) {
                l13_w32(o, self + 0x2EC, 0);
            } else {
                l13_w32(o, self + 0x2EC, l13_u32(o, self + 0x2EC) - 1u);
                if (l13_u32(o, self + 0x2EC) == 0) {
                    if (l13_u8(o, 0x008107F9u) & 0xFu) {
                        l13_w32(o, self + 0x2E4, 0xBDAAAAABu);
                        if (l13_c_001FBD50(o, self, 0x8EB, 0, F_300)) return;
                    } else {
                        l13_w32(o, self + 0x2E4, 0x3DAAAAABu);
                        if (l13_c_001FBD50(o, self, 0x8EA, 0, F_300)) return;
                    }
                    l13_w8(o, self + 5, l13_u8(o, self + 5) + 1u);
                    l13_w16(o, self + 0x28, 0);
                }
            }
        } else {
            uint32_t x = l13_u32(o, 0x00810350u);
            if (!L13_LE(x, 0x44548000u) && L13_LT(x, 0x4456E666u)) {
                uint32_t z = l13_u32(o, 0x00810358u);
                if (!L13_LE(z, 0x4454A000u) && L13_LT(z, 0x4455C000u)) {
                    l13_w32(o, self + 0x2E4, 0);
                    l13_w32(o, self + 0x2E0, 0);
                    l13_w16(o, self + 0x28, 0);
                    if (l13_c_001BA1A0(o, blk, 0x0082D290u)) return;
                    l13_w8(o, self + 4, 1);
                }
            }
        }
    }
    if (l13_c_001B1B70(o, self)) return;
    if (l13_method(o, self)) return;
    l13_lift_lights(o, self);
}

static void l13_826C10_script(L13 *o, uint32_t self)
{
    int32_t done = 0;
    if (l13_c_001BA1F0(o, self, &done)) return;
    if (done != 0) {
        l13_w8(o, 0x00810779u, 0xFF);
        if (l13_c_0019C6F0(o, 0x15, 0)) return;
        l13_w8(o, self + 4, 2);
        return;
    }
    int32_t t = l13_s16(o, self + 0x28);
    if (t != 0) {
        if (t < 0x46) {
            l13_w16(o, self + 0x28, (uint32_t)(t + 1));
            if (l13_s16(o, self + 0x28) == 0x46) {
                if (l13_c_001FBD50(o, self, 0x8EC, 0, F_300)) return;
                if (l13_c_001B1E20(o, 5, 0x78)) return;
            }
        } else {
            uint32_t rest = l13_u32(o, self + 0x2E8);
            uint32_t y = l13_u32(o, self + 0xB4);
            if (L13_LE(y, rest)) {
                l13_w32(o, self + 0xB4, rest);
                if (l13_u32(o, self + 0x2E0) == 0) {
                    if (l13_c_001B1E20(o, 8, 0x14)) return;
                    l13_w32(o, self + 0x2E0, 1);
                }
            } else {
                l13_w32(o, self + 0x2E4, L13_ADD(l13_u32(o, self + 0x2E4), 0x3E4CCCCDu));
                uint32_t yy = l13_u32(o, self + 0xB4);
                l13_w32(o, self + 0xB4, L13_SUB(yy, l13_u32(o, self + 0x2E4)));
                if (l13_c_001C6380(o, self)) return;
                if (l13_c_001A2370(o, self, self + 0xD0)) return;
            }
        }
    }
    if (l13_c_001B1B70(o, self)) return;
    if (l13_method(o, self)) return;
    uint32_t rest = l13_u32(o, self + 0x2E8);
    if (!L13_LE(l13_u32(o, self + 0xB4), rest)) l13_lift_lights(o, self);
}

int em_level13_port_00826C10(const H13 *h, uint32_t self, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    uint32_t st = l13_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 2) {
        if (l13_c_001B1B70(&o, self)) return -1;
        l13_method(&o, self);
    } else if (st == 1) {
        l13_826C10_script(&o, self);
    } else if (st == 4) {
        l13_826C10_lift(&o, self);
    } else if (st == 0) {
        if (l13_c_001B0FD0(&o, self, &r)) return -1;
        if (r != 0) return l13_end(&o);
        l13_w32(&o, self + 0x2E8, l13_u32(&o, self + 0xB4));
        l13_w32(&o, self + 0x2EC, 0);
        if (l13_u8(&o, 0x00810779u) == 0xFFu) {
            if (l13_c_001C6380(&o, self)) return -1;
            l13_w8(&o, self + 4, 2);
        } else {
            if (l13_u8(&o, 0x008107F9u) & 0xFu)
                l13_w32(&o, self + 0xB4, L13_ADD(0x41700000u, l13_u32(&o, self + 0x2E8)));
            if (l13_c_001C6380(&o, self)) return -1;
            l13_w8(&o, self + 4, 4);
            if (l13_c_0019C6F0(&o, 0x15, 1)) return -1;
        }
        l13_c_001A2370(&o, self, self + 0xD0);
    } else {
        l13_c_001AFC10(&o, self);
    }
    return l13_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x827430 (self): [35], follows the linked object's (+0x1C) height. +4 3
 * and others above 1: 001AFC10(self). 0: when 001B0FD0(self) returns 0:
 * 001C6380(self), +0x2E4 = the linked +0xB4, 001A2370(self, self +0xD0).
 * 1: when +0x2E4 differs from the linked +0xB4: +0xB4 = 7.9 + it, +0x2E4 =
 * the linked +0xB4 (read again), 001C6380, 001A2370; then 001B1B70(self)
 * and the +0x4C method.
 * ---------------------------------------------------------------------- */
int em_level13_port_00827430(const H13 *h, uint32_t self, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    uint32_t st = l13_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 1) {
        uint32_t mine = l13_u32(&o, self + 0x2E4);
        uint32_t y = l13_u32(&o, l13_u32(&o, self + 0x1C) + 0xB4);
        if (!L13_EQ(mine, y)) {
            l13_w32(&o, self + 0xB4, L13_ADD(0x40FCCCC0u, y));
            l13_w32(&o, self + 0x2E4, l13_u32(&o, l13_u32(&o, self + 0x1C) + 0xB4));
            if (l13_c_001C6380(&o, self)) return -1;
            if (l13_c_001A2370(&o, self, self + 0xD0)) return -1;
        }
        if (l13_c_001B1B70(&o, self)) return -1;
        l13_method(&o, self);
    } else if (st == 0) {
        if (l13_c_001B0FD0(&o, self, &r)) return -1;
        if (r != 0) return l13_end(&o);
        if (l13_c_001C6380(&o, self)) return -1;
        l13_w32(&o, self + 0x2E4, l13_u32(&o, l13_u32(&o, self + 0x1C) + 0xB4));
        l13_c_001A2370(&o, self, self + 0xD0);
    } else {
        l13_c_001AFC10(&o, self);
    }
    return l13_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x827550 (self): [53], the item 0x24 giver in the tower. +4 2 / 3:
 * 001AFC10(self). 0: 001BA1C0(self, 0x26) set, or (0x25) clear -> +4 = 3;
 * otherwise 001B10B0(self, +0xD, 0x97), 001C63E0(self, 0), +0x58 =
 * D_0028A704, 001F1110(self, 3), +0 = +4 = 1, +0x28 = 0. 1: +5 0: with
 * 445 <= y <= 460 (D_00810354): 001B1EA0(0, 0x810350, area 0x82F820, 4)
 * set -> 001FB0B0(0), 001BA1A0(blk, script 0x82D590), +5 = 1; then
 * 001F1180(self). +5 1: +0x28 += 1, below 300 001F1180(self); at the
 * script's end 001C47A0(0x24, 1), +5 = 2. Then 001C64F0(self, 1.0),
 * 001C68C0, 001B17A0 and the +0x4C method.
 * ---------------------------------------------------------------------- */
int em_level13_port_00827550(const H13 *h, uint32_t self, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    uint32_t blk = self + 0x1F0;
    uint32_t st = l13_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 3 || st == 2) {
        l13_c_001AFC10(&o, self);
    } else if (st == 1) {
        uint32_t sub = l13_u8(&o, self + 5);
        if (sub == 1) {
            l13_w16(&o, self + 0x28, l13_u16(&o, self + 0x28) + 1u);
            if (l13_s16(&o, self + 0x28) < 0x12C) {
                if (l13_c_001F1180(&o, self)) return -1;
            }
            if (l13_c_001BA1F0(&o, self, &r)) return -1;
            if (r != 0) {
                if (l13_c_001C47A0(&o, 0x24, 1)) return -1;
                l13_w8(&o, self + 5, 2);
            }
        } else if (sub == 0) {
            uint32_t y = l13_u32(&o, 0x00810354u);
            if (!L13_LT(y, 0x43DE8000u) && L13_LE(y, 0x43E60000u)) {
                if (l13_c_001B1EA0(&o, 0, 0x00810350u, 0x0082F820u, 4, &r)) return -1;
                if (r != 0) {
                    if (l13_c_001FB0B0(&o, 0)) return -1;
                    if (l13_c_001BA1A0(&o, blk, 0x0082D590u)) return -1;
                    l13_w8(&o, self + 5, 1);
                }
                if (l13_c_001F1180(&o, self)) return -1;
            }
        }
        if (l13_c_001C64F0(&o, self, F_ONE)) return -1;
        if (l13_c_001C68C0(&o, self)) return -1;
        if (l13_c_001B17A0(&o, self, &r)) return -1;
        l13_method(&o, self);
    } else if (st == 0) {
        if (l13_c_001BA1C0(&o, self, 0x26, &r)) return -1;
        if (r != 0) {
            l13_w8(&o, self + 4, 3);
            return l13_end(&o);
        }
        if (l13_c_001BA1C0(&o, self, 0x25, &r)) return -1;
        if (r == 0) {
            l13_w8(&o, self + 4, 3);
            return l13_end(&o);
        }
        if (l13_c_001B10B0(&o, self, (int32_t)l13_u8(&o, self + 0xD), 0x97)) return -1;
        if (l13_c_001C63E0(&o, self, 0)) return -1;
        l13_w32(&o, self + 0x58, l13_u32(&o, 0x0028A704u));
        if (l13_c_001F1110(&o, self, 3)) return -1;
        l13_w8(&o, self, 1);
        l13_w8(&o, self + 4, 1);
        l13_w16(&o, self + 0x28, 0);
    }
    return l13_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8279E0 (self): [34], the sub-1 arrival script. +4 2 / 3:
 * 001AFC10(self). 0: 001BA1C0(self, 0x46) set -> 0019C6F0(8, 1),
 * 001B6660(0x82A9C0), +4 = 3; otherwise +4 = +0 = 1. 1: +5 0: with
 * D_00810702 == 1 and 0x70003B8D != 4: 001BA1A0(blk, script 0x82E090), +5
 * = 1. +5 1: at the script's end 001C4760(0xB, 1), D_0081081E = 0xFF, +4 =
 * 3.
 * ---------------------------------------------------------------------- */
int em_level13_port_008279E0(const H13 *h, uint32_t self, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    uint32_t blk = self + 0x1F0;
    uint32_t st = l13_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 3 || st == 2) {
        l13_c_001AFC10(&o, self);
    } else if (st == 1) {
        uint32_t sub = l13_u8(&o, self + 5);
        if (sub == 1) {
            if (l13_c_001BA1F0(&o, self, &r)) return -1;
            if (r != 0) {
                if (l13_c_001C4760(&o, 0xB, 1)) return -1;
                l13_w8(&o, 0x0081081Eu, 0xFF);
                l13_w8(&o, self + 4, 3);
            }
        } else if (sub == 0) {
            if (l13_u8(&o, 0x00810702u) == 1u && l13_u8(&o, 0x70003B8Du) != 4u) {
                if (l13_c_001BA1A0(&o, blk, 0x0082E090u)) return -1;
                l13_w8(&o, self + 5, 1);
            }
        }
    } else if (st == 0) {
        if (l13_c_001BA1C0(&o, self, 0x46, &r)) return -1;
        if (r != 0) {
            if (l13_c_0019C6F0(&o, 8, 1)) return -1;
            if (l13_c_001B6660(&o, 0x0082A9C0u)) return -1;
            l13_w8(&o, self + 4, 3);
        } else {
            l13_w8(&o, self + 4, 1);
            l13_w8(&o, self, 1);
        }
    }
    return l13_end(&o);
}

/* 0x827B10 (): a callback of [34]'s script: D_0081081E = 1; returns 1. */
int em_level13_port_00827B10(const H13 *h, int32_t *result, F13 *fault)
{
    L13 o;
    if (!result || l13_begin(&o, h, fault)) return -1;
    l13_w8(&o, 0x0081081Eu, 1);
    if (l13_failed(&o)) return -1;
    *result = 1;
    return 0;
}

/* 0x827B20 (): a callback of [34]'s script: 001B6660(0x82A9C0), 001F6B90
 * (translated here), 0019C6F0(8, 1); returns 1. */
int em_level13_port_00827B20(const H13 *h, int32_t *result, F13 *fault)
{
    L13 o;
    if (!result || l13_begin(&o, h, fault)) return -1;
    if (!l13_c_001B6660(&o, 0x0082A9C0u)) {
        l13_001F6B90(&o);
        l13_c_0019C6F0(&o, 8, 1);
    }
    if (l13_failed(&o)) return -1;
    *result = 1;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x827B60 (self): sub 1's 0x827B60 objects (11 placements). +4 3:
 * 001AFC10(self). 2: +0x10 = 0x156620. 0: D_0081079E == 0xFF -> +4 = 3;
 * otherwise when 001B0FD0(self) returns 0: +0x34 = 1 (halfword), +0 = 1,
 * the pose +0xB0 / +0xC0 copied to blk +0x10 / +0x20 (00102948),
 * 001C6380(self). 1: +5 0: D_0081081E == 1 -> +5 += 1, the countdown +0x28
 * = +0x9A. +5 1: +0x28 -= 1; at 0: +0 = +4 = 2, +5 = 0, 0x700038A0 = the
 * position, 0x700038B0 = it, their y +4 / -4, and 0019A570(0x700038A0,
 * 0x700038B0, 4, 0) set -> the matrix 0x700036A0 (identity, rotated by
 * +0xC0, turned by pi/2 (00102B08), translated by +0xB0), its y += 0.2 and
 * 001F0460(4, 0x700036A0). Then 001B17A0(self) and the +0x4C method.
 * ---------------------------------------------------------------------- */
int em_level13_port_00827B60(const H13 *h, uint32_t self, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    uint32_t blk = self + 0x1F0;
    uint32_t st = l13_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 3) {
        l13_c_001AFC10(&o, self);
    } else if (st == 2) {
        l13_w32(&o, self + 0x10, 0x00156620u);
    } else if (st == 1) {
        uint32_t sub = l13_u8(&o, self + 5);
        if (sub == 1) {
            uint32_t t = (l13_u16(&o, self + 0x28) - 1u) & 0xFFFFu;
            l13_w16(&o, self + 0x28, t);
            if (t == 0) {
                l13_w8(&o, self, 2);
                l13_w8(&o, self + 4, 2);
                l13_w8(&o, self + 5, 0);
                if (l13_c_00102948(&o, 0x700038A0u, self + 0xB0)) return -1;
                if (l13_c_00102948(&o, 0x700038B0u, 0x700038A0u)) return -1;
                uint32_t ya = l13_u32(&o, 0x700038A4u);
                uint32_t yb = l13_u32(&o, 0x700038B4u);
                l13_w32(&o, 0x700038A4u, L13_ADD(ya, 0x40800000u));
                l13_w32(&o, 0x700038B4u, L13_SUB(yb, 0x40800000u));
                if (l13_c_0019A570(&o, 0x700038A0u, 0x700038B0u, 4, 0, &r)) return -1;
                if (r != 0) {
                    if (l13_c_001029C0(&o, 0x700036A0u)) return -1;
                    if (l13_c_00102C58(&o, 0x700036A0u, 0x700036A0u, self + 0xC0)) return -1;
                    if (l13_c_00102B08(&o, 0x700036A0u, 0x700036A0u, F_HALF_PI)) return -1;
                    if (l13_c_00102918(&o, 0x700036A0u, 0x700036A0u, self + 0xB0)) return -1;
                    l13_w32(&o, 0x700036D4u, L13_ADD(l13_u32(&o, 0x700036D4u), 0x3E4CCCCDu));
                    if (l13_c_001F0460(&o, 4, 0x700036A0u)) return -1;
                }
            }
        } else if (sub == 0) {
            if (l13_u8(&o, 0x0081081Eu) == 1u) {
                l13_w8(&o, self + 5, sub + 1u);
                l13_w16(&o, self + 0x28, l13_u8(&o, self + 0x9A));
            }
        }
        if (l13_c_001B17A0(&o, self, &r)) return -1;
        l13_method(&o, self);
    } else if (st == 0) {
        if (l13_u8(&o, 0x0081079Eu) == 0xFFu) {
            l13_w8(&o, self + 4, 3);
            return l13_end(&o);
        }
        if (l13_c_001B0FD0(&o, self, &r)) return -1;
        if (r != 0) return l13_end(&o);
        l13_w16(&o, self + 0x34, 1);
        l13_w8(&o, self, 1);
        if (l13_c_00102948(&o, blk + 0x10, self + 0xB0)) return -1;
        if (l13_c_00102948(&o, blk + 0x20, self + 0xC0)) return -1;
        l13_c_001C6380(&o, self);
    }
    return l13_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x829840 (src, sp): an object spawner called from 0x827DD0. o =
 * 001AFA90(0xC); when set: the colour quadword 0x82F680 copied to the
 * frame (sp - 0x10), 00102948(o +0xB0, src +0x30), 00102958(o +0xD0, src),
 * 001026A0(o +0x100, o +0xD0, sp - 0x10), o +0x10 = 0x1F5040.
 * ---------------------------------------------------------------------- */
int em_level13_port_00829840(const H13 *h, uint32_t src, uint32_t sp, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    uint32_t obj = 0;
    if (l13_c_001AFA90(&o, 0xC, &obj)) return -1;
    if (obj != 0) {
        uint8_t v[16];
        l13_q(&o, 0x0082F680u, v);
        l13_wq(&o, sp - 0x10u, v);
        if (l13_c_00102948(&o, obj + 0xB0, src + 0x30)) return -1;
        if (l13_c_00102958(&o, obj + 0xD0, src)) return -1;
        if (l13_c_001026A0(&o, obj + 0x100, obj + 0xD0, sp - 0x10u)) return -1;
        l13_w32(&o, obj + 0x10, 0x001F5040u);
    }
    return l13_end(&o);
}

/* The two turns 0x829A70 applies: 0019C6F0(5, 0), then the +0x74 floats
 * of the objects *(D_00275B40 + 8) / *(D_00275B40 + 0xC) = -pi/2 / pi/2
 * (the word D_00275B40 read for each). */
static int l13_829A70_turn(L13 *o)
{
    if (l13_c_0019C6F0(o, 5, 0)) return -1;
    l13_w32(o, l13_u32(o, l13_u32(o, 0x00275B40u) + 8) + 0x74, 0xBFC90FDBu);
    l13_w32(o, l13_u32(o, l13_u32(o, 0x00275B40u) + 0xC) + 0x74, F_HALF_PI);
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x829A70 (self): [46]. +4 3 and others: 001AFC10(self). 2: 001B1B70,
 * the +0x4C method. 0: when 001B0FD0(self) returns 0: +0 = 1, +0x2E8 = 0;
 * D_00810838 != 0 -> the turns, 001C6380(self), +2 = 4, +4 = 2; otherwise
 * +8 = 1, +0x2EC = the halfword +0xE, +0xE |= 0xFF00, +0x30 = 0x82F790,
 * 001C6380(self), and (D_00810838 still 0) a child o = 001AFA90(0xC): o
 * +0x9A = o +3 = 0, o +0x2E = 0, o +0xD = 0x18, o +0xE = 0xFFFF, o +0x54 =
 * o +0x56 = 0, o +0xA0.. = (1, 0, 0, 0.25), its pose from +0xB0 / +0xC0,
 * o +0x10 = 0x1C5760, +0x2E8 = o. 1: +5 0 on +0xB bit 2: with bit 0:
 * D_00810838 = 1, 001C47E0(0x25, 1), +0xE = the low half of +0x2EC, the
 * turns, the child's +4 = 3, 001C6380(self), +2 = 4, +4 = 2, 0x70003B8D /
 * B91 / B92 = 0 and, with 0x70003B8F == 2, 001CA770(0x8102B0) and
 * 0x70003B8F = 1; without bit 0 001BA1A0(blk, script 0x82F690), +5 += 1.
 * +5 1: at the script's end +5 = +0xB = 0. Then 001B1B70 and the +0x4C
 * method.
 * ---------------------------------------------------------------------- */
int em_level13_port_00829A70(const H13 *h, uint32_t self, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    uint32_t blk = self + 0x1F0;
    uint32_t st = l13_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 2) {
        if (l13_c_001B1B70(&o, self)) return -1;
        l13_method(&o, self);
    } else if (st == 1) {
        uint32_t sub = l13_u8(&o, self + 5);
        if (sub == 1) {
            if (l13_c_001BA1F0(&o, self, &r)) return -1;
            if (r != 0) {
                l13_w8(&o, self + 5, 0);
                l13_w8(&o, self + 0xB, 0);
            }
        } else if (sub == 0) {
            uint32_t b = l13_u8(&o, self + 0xB);
            if (b & 4u) {
                if (b & 1u) {
                    l13_w8(&o, 0x00810838u, 1);
                    if (l13_c_001C47E0(&o, 0x25, 1)) return -1;
                    l13_w16(&o, self + 0xE, l13_u16(&o, self + 0x2EC));
                    if (l13_829A70_turn(&o)) return -1;
                    uint32_t child = l13_u32(&o, self + 0x2E8);
                    if (child != 0) l13_w8(&o, child + 4, 3);
                    if (l13_c_001C6380(&o, self)) return -1;
                    l13_w8(&o, self + 2, 4);
                    l13_w8(&o, self + 4, 2);
                    l13_w8(&o, 0x70003B8Du, 0);
                    l13_w8(&o, 0x70003B91u, 0);
                    l13_w8(&o, 0x70003B92u, 0);
                    if (l13_u8(&o, 0x70003B8Fu) == 2u) {
                        if (l13_c_001CA770(&o, 0x008102B0u)) return -1;
                        l13_w8(&o, 0x70003B8Fu, 1);
                    }
                } else {
                    if (l13_c_001BA1A0(&o, blk, 0x0082F690u)) return -1;
                    l13_w8(&o, self + 5, l13_u8(&o, self + 5) + 1u);
                }
            }
        }
        if (l13_c_001B1B70(&o, self)) return -1;
        l13_method(&o, self);
    } else if (st == 0) {
        if (l13_c_001B0FD0(&o, self, &r)) return -1;
        if (r != 0) return l13_end(&o);
        l13_w8(&o, self, 1);
        l13_w32(&o, self + 0x2E8, 0);
        if (l13_u8(&o, 0x00810838u) != 0) {
            if (l13_829A70_turn(&o)) return -1;
            if (l13_c_001C6380(&o, self)) return -1;
            l13_w8(&o, self + 2, 4);
            l13_w8(&o, self + 4, 2);
            return l13_end(&o);
        }
        l13_w8(&o, self + 8, 1);
        l13_w32(&o, self + 0x2EC, l13_u16(&o, self + 0xE));
        l13_w16(&o, self + 0xE, l13_u16(&o, self + 0xE) | 0xFF00u);
        l13_w32(&o, self + 0x30, 0x0082F790u);
        if (l13_c_001C6380(&o, self)) return -1;
        if (l13_u8(&o, 0x00810838u) != 0) return l13_end(&o);
        uint32_t c = 0;
        if (l13_c_001AFA90(&o, 0xC, &c)) return -1;
        if (c == 0) return l13_end(&o);
        l13_w8(&o, c + 0x9A, 0);
        l13_w8(&o, c + 3, 0);
        l13_w16(&o, c + 0x2E, 0);
        l13_w8(&o, c + 0xD, 0x18);
        l13_w16(&o, c + 0xE, 0xFFFF);
        l13_w16(&o, c + 0x54, 0);
        l13_w16(&o, c + 0x56, 0);
        l13_w32(&o, c + 0xA0, F_ONE);
        l13_w32(&o, c + 0xA4, 0);
        l13_w32(&o, c + 0xA8, 0);
        l13_w32(&o, c + 0xAC, 0x3E800000u);
        if (l13_c_00102948(&o, c + 0xB0, self + 0xB0)) return -1;
        if (l13_c_00102948(&o, c + 0xC0, self + 0xC0)) return -1;
        l13_w32(&o, c + 0x10, 0x001C5760u);
        l13_w32(&o, self + 0x2E8, c);
    } else {
        l13_c_001AFC10(&o, self);
    }
    return l13_end(&o);
}
