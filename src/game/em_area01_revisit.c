/* AREA01 revisit owners (see em_area01_revisit.h, docs/AREA01_REVISIT.md).
 *
 * Each function below is a translation of the original AREA01 overlay code
 * at the named runtime address. All seventeen follow the decomp's
 * byte-identical C (src/overlays/AREA01/<name>.c, link name = runtime -
 * 0x40). Calls, their arguments and the memory accesses between each two
 * calls follow the original: the test compares memory at every call entry
 * and after the last store, and the memory accesses between calls one for
 * one, in order, by address and size (docs/AREA01_REVISIT.md section 3).
 * Where the original loads the operands of one expression in a set order,
 * the translation loads them in separate statements in that order, because
 * C leaves the order of evaluation of arguments and operands open. Float
 * arithmetic is the EE model (em_ee_float.h) on bit patterns; the operand
 * order of each add/sub/mul/div/compare is the original's.
 */
#include "em_area01_revisit_internal.h"

/* Original globals the functions name. */
#define D_001F5040 0x001F5040u /* a boot function address, stored as a behaviour */
#define D_00275B40 0x00275B40u /* current bone table (actor + 0x110 while a behaviour runs) */
#define D_0028A5C4 0x0028A5C4u
#define D_00810350 0x00810350u /* point (the player actor's +0xA0) */
#define D_00810360 0x00810360u
#define D_008105E0 0x008105E0u
#define D_008106C0 0x008106C0u /* word: the 0x825950 node with +0x0D 0x47, while it runs */
#define D_008107DF 0x008107DFu /* AREA01 counter 7 (the revisit event's stage) */
#define D_00810845 0x00810845u

/* Scratchpad. */
#define S_3000 0x70003000u /* 4x4 matrix */
#define S_3030 0x70003030u /* its translation row */
#define S_3400 0x70003400u /* 4x4 matrix */
#define S_36A0 0x700036A0u /* 4x4 matrix */
#define S_36D0 0x700036D0u /* its translation row */
#define S_36D4 0x700036D4u
#define S_36D8 0x700036D8u
#define S_38A0 0x700038A0u
#define S_38A4 0x700038A4u
#define S_38A8 0x700038A8u
#define S_38AC 0x700038ACu
#define S_38B0 0x700038B0u
#define S_38B4 0x700038B4u
#define S_38B8 0x700038B8u
#define S_38BC 0x700038BCu
#define S_38C0 0x700038C0u
#define S_38C4 0x700038C4u
#define S_38C8 0x700038C8u
#define S_38CC 0x700038CCu
#define S_38D0 0x700038D0u
#define S_38D4 0x700038D4u
#define S_38D8 0x700038D8u
#define S_31B0 0x700031B0u
#define S_31D0 0x700031D0u
#define S_31D4 0x700031D4u
#define S_31D8 0x700031D8u
#define S_3A20 0x70003A20u

/* Overlay data (runtime addresses). */
#define RV_VEC_829DA0 0x00829DA0u    /* 16-byte vector (0x8237D0's a) */
#define RV_VEC_829DB0 0x00829DB0u    /* 16-byte vector (0x8237D0's b) */
#define RV_BONES_829DC0 0x00829DC0u  /* word per spot index: a bone-table slot */
#define RV_SPOTS_829DE0 0x00829DE0u  /* 0x18-byte records: three position, three angle floats */
#define RV_QUAD_82B090 0x0082B090u   /* four 16-byte points */
#define RV_SCRIPT_82AA90 0x0082AA90u
#define RV_SCRIPT_82AC10 0x0082AC10u
#define RV_SCRIPT_82AD10 0x0082AD10u
#define RV_SCRIPT_82AD90 0x0082AD90u
#define RV_GROUP_829220 0x00829220u
#define RV_GROUP_8291C0 0x008291C0u
#define RV_VEC_82CB20 0x0082CB20u    /* 16-byte vector (0x8282F0's tint) */

/* Float constants (bit patterns the original materialises). */
#define F_ZERO 0x00000000u
#define F_ONE 0x3F800000u
#define F_2 0x40000000u
#define F_MINUS_2 0xC0000000u
#define F_2_5 0x40200000u
#define F_MINUS_2_5 0xC0200000u
#define F_1_5 0x3FC00000u
#define F_3 0x40400000u
#define F_5 0x40A00000u
#define F_6 0x40C00000u
#define F_8 0x41000000u
#define F_60 0x42700000u
#define F_100 0x42C80000u
#define F_180 0x43340000u
#define F_505 0x43FC8000u
#define F_10000 0x461C4000u
#define F_56_25 0x42610000u
#define F_0_02 0x3CA3D70Au
#define F_0_04 0x3D23D70Au
#define F_0_1 0x3DCCCCCDu
#define F_0_2 0x3E4CCCCDu
#define F_0_6 0x3F19999Au
#define F_0_8 0x3F4CCCCDu
#define F_PI 0x40490FDBu
#define F_MINUS_HALF_PI 0xBFC90FDBu
#define F_MINUS_QUARTER_PI 0xBF490FDBu
#define F_TURN_4DEG 0x3D8EFA35u      /* 0.06981317 (4 degrees) */
#define F_TURN_2_5DEG 0x3D32B8C3u    /* 0.04363323 */
#define F_TURN_8DEG 0x3E0EFA35u      /* 0.13962634 */

#define GIFTAG_8282F0 UINT64_C(0x20045BA5154222DC)

static inline float fb(uint32_t bits) { return em_ee_float(bits); }
static inline uint32_t bf(float value) { return em_ee_bits(value); }

static int rv_begin(A01Rv *o, const EmArea01RevisitHooks *h, EmArea01RevisitFault *fault)
{
    if (!h || !fault) return -1;
    rv_open(o, h, fault);
    return rv_failed(o) ? -1 : 0;
}

static int rv_end(const A01Rv *o) { return rv_failed(o) ? -1 : 0; }

/* A quadword copy the original makes with one quadword load and one store:
 * both halves are read, then both are written. */
static void rv_copy16(A01Rv *o, uint32_t dst, uint32_t src)
{
    uint64_t lo = rv_u64(o, src);
    uint64_t hi = rv_u64(o, src + 8);
    rv_w64(o, dst, lo);
    rv_w64(o, dst + 8, hi);
}

/* ======================================================================
 * 0x8237D0 (func_overlay_AREA01_00823790). Stack locals (sp at entry):
 * a = sp - 0x20 and b = sp - 0x10 (copies of the vectors 0x829DA0 and
 * 0x829DB0, made on every call), m = sp - 0x60 (a 4x4 matrix).
 * +0x04 0: +0x28 = 0. 1: +0x28 += 1 (stored, read again); when it is a
 * multiple of 10: o = 001AFA90(0xC); if o: 00102948(o + 0xB0, +0x100),
 * 00102958(o + 0xD0, +0xD0), 001026A0(o + 0x100, o + 0xD0, a), o +0x10 =
 * 0x1F5040; then (o zero or not) 00102958(m, +0xD0), 001026A0(m + 0x30,
 * o + 0xD0, b), 001F4010(3, m). Other states: nothing.
 * ====================================================================== */
static void rv_8237d0(A01Rv *o, uint32_t self, uint32_t sp)
{
    uint32_t a = sp - 0x20, b = sp - 0x10, m = sp - 0x60;
    rv_copy16(o, a, RV_VEC_829DA0);
    rv_copy16(o, b, RV_VEC_829DB0);
    uint32_t state = rv_u8(o, self + 4);
    if (state == 0) {
        rv_w16(o, self + 0x28, 0);
        return;
    }
    if (state != 1) return;
    rv_w16(o, self + 0x28, (uint32_t)(rv_s16(o, self + 0x28) + 1));
    if (rv_s16(o, self + 0x28) % 10 != 0) return;
    uint32_t obj = 0;
    (void)rv_c_001AFA90(o, 0xC, &obj);
    if (obj != 0) {
        (void)rv_c_00102948(o, obj + 0xB0, self + 0x100);
        (void)rv_c_00102958(o, obj + 0xD0, self + 0xD0);
        (void)rv_c_001026A0(o, obj + 0x100, obj + 0xD0, a);
        rv_w32(o, obj + 0x10, D_001F5040);
    }
    (void)rv_c_00102958(o, m, self + 0xD0);
    (void)rv_c_001026A0(o, m + 0x30, obj + 0xD0, b);
    (void)rv_c_001F4010(o, 3, m);
}

int em_area01_revisit_008237D0(const EmArea01RevisitHooks *h, uint32_t self, uint32_t sp, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (rv_begin(&o, h, fault)) return -1;
    rv_8237d0(&o, self, sp);
    return rv_end(&o);
}

/* ======================================================================
 * 0x823900 (func_overlay_AREA01_008238C0). Stack local v = sp - 0x10.
 * +0x04 1: v = (0, 0, 1, 1); 001026A0(v, +0xD0, v); 001028D0(v, v, +0x100);
 * 00102760(v, v); 001EFD90(0x80000003, +0x100, v); 001EFD20(0x80000024,
 * +0x100). Other states: nothing.
 * ====================================================================== */
static void rv_823900(A01Rv *o, uint32_t self, uint32_t sp)
{
    uint32_t v = sp - 0x10;
    if (rv_u8(o, self + 4) != 1) return;
    rv_w32(o, v + 0x0, F_ZERO);
    rv_w32(o, v + 0x4, F_ZERO);
    rv_w32(o, v + 0x8, F_ONE);
    rv_w32(o, v + 0xC, F_ONE);
    (void)rv_c_001026A0(o, v, self + 0xD0, v);
    (void)rv_c_001028D0(o, v, v, self + 0x100);
    (void)rv_c_00102760(o, v, v);
    (void)rv_c_001EFD90(o, (int32_t)0x80000003u, self + 0x100, v);
    (void)rv_c_001EFD20(o, (int32_t)0x80000024u, self + 0x100);
}

int em_area01_revisit_00823900(const EmArea01RevisitHooks *h, uint32_t self, uint32_t sp, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (rv_begin(&o, h, fault)) return -1;
    rv_823900(&o, self, sp);
    return rv_end(&o);
}

/* ======================================================================
 * 0x8239C0 (func_overlay_AREA01_00823980).
 * +0x04 1: the scratchpad words 0x700038B0..BC = 0x30, 0x80, 0x30, 0x80,
 * then 001F4E20(+0x100, 0x700038B0, 5.0). Other states: nothing.
 * ====================================================================== */
static void rv_8239c0(A01Rv *o, uint32_t self)
{
    if (rv_u8(o, self + 4) != 1) return;
    rv_w32(o, S_38B0, 0x30);
    rv_w32(o, S_38B4, 0x80);
    rv_w32(o, S_38B8, 0x30);
    rv_w32(o, S_38BC, 0x80);
    (void)rv_c_001F4E20(o, self + 0x100, S_38B0, fb(F_5));
}

int em_area01_revisit_008239C0(const EmArea01RevisitHooks *h, uint32_t self, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (rv_begin(&o, h, fault)) return -1;
    rv_8239c0(&o, self);
    return rv_end(&o);
}

/* ======================================================================
 * 0x824F70 (func_overlay_AREA01_00824F30). p = (D_008106C0) +0x118.
 * Returns 1 when 0011E748((+0xB0 - p+0xC0)^2 + (+0xB8 - p+0xC8)^2) <= 8.0
 * (the sum by the EE's MULA/MADD), else 0.
 * ====================================================================== */
static int32_t rv_824f70(A01Rv *o, uint32_t self)
{
    uint32_t owner = rv_u32(o, D_008106C0);
    uint32_t sx = rv_u32(o, self + 0xB0);
    uint32_t sz = rv_u32(o, self + 0xB8);
    uint32_t p = rv_u32(o, owner + 0x118);
    uint32_t px = rv_u32(o, p + 0xC0);
    uint32_t pz = rv_u32(o, p + 0xC8);
    uint32_t dx = em_ee_sub_bits(sx, px);
    uint32_t dz = em_ee_sub_bits(sz, pz);
    uint32_t sum = em_ee_madd_bits(em_ee_mula_bits(dx, dx), dz, dz);
    float d = 0.0f;
    (void)rv_c_0011E748(o, fb(sum), &d);
    return em_ee_c_le_bits(bf(d), F_8) ? 1 : 0;
}

int em_area01_revisit_00824F70(const EmArea01RevisitHooks *h, uint32_t self, int32_t *result,
                               EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (!result || rv_begin(&o, h, fault)) return -1;
    int32_t r = rv_824f70(&o, self);
    if (rv_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ======================================================================
 * 0x824FE0 (func_overlay_AREA01_00824FA0). p = (D_008106C0) +0x118.
 * +0xC4 = 001B12B0(001B1240(+0xB0, p+0xC0, p+0xC8), +0xC4, 0.04363323).
 * ====================================================================== */
static void rv_824fe0(A01Rv *o, uint32_t self)
{
    uint32_t owner = rv_u32(o, D_008106C0);
    uint32_t p = rv_u32(o, owner + 0x118);
    uint32_t px = rv_u32(o, p + 0xC0);
    uint32_t pz = rv_u32(o, p + 0xC8);
    float goal = 0.0f, yaw = 0.0f;
    (void)rv_c_001B1240(o, self + 0xB0, fb(px), fb(pz), &goal);
    uint32_t current = rv_u32(o, self + 0xC4);
    (void)rv_c_001B12B0(o, goal, fb(current), fb(F_TURN_2_5DEG), &yaw);
    rv_w32(o, self + 0xC4, bf(yaw));
}

int em_area01_revisit_00824FE0(const EmArea01RevisitHooks *h, uint32_t self, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (rv_begin(&o, h, fault)) return -1;
    rv_824fe0(&o, self);
    return rv_end(&o);
}

/* ======================================================================
 * 0x825040 (func_overlay_AREA01_00825000), (self, block). D_008106C0 is
 * read on entry in every case. +0x07 0: block+0xE8 = 001B1240(+0xB0,
 * p+0xC0, p+0xC8) with p = (D_008106C0)+0x118; +0x07 += 1; block+0xD2 =
 * 0x78; returns 0. +0x07 1: +0xC4 = 001B12B0(block+0xE8, +0xC4,
 * 0.13962634); block+0xD2 -= 1; returns 1 when that halfword is 0 or when
 * +0xC4 == block+0xE8 (both read again), else 0. Other: 0.
 * ====================================================================== */
static int32_t rv_825040(A01Rv *o, uint32_t self, uint32_t block)
{
    uint32_t step = rv_u8(o, self + 7);
    uint32_t owner = rv_u32(o, D_008106C0);
    if (step == 1) {
        uint32_t current = rv_u32(o, self + 0xC4);
        uint32_t goal = rv_u32(o, block + 0xE8);
        float yaw = 0.0f;
        (void)rv_c_001B12B0(o, fb(goal), fb(current), fb(F_TURN_8DEG), &yaw);
        rv_w32(o, self + 0xC4, bf(yaw));
        uint32_t n = (uint32_t)(rv_s16(o, block + 0xD2) - 1);
        rv_w16(o, block + 0xD2, n);
        if ((n & 0xFFFFu) == 0) return 1;
        uint32_t a = rv_u32(o, self + 0xC4);
        uint32_t b = rv_u32(o, block + 0xE8);
        return em_ee_c_eq_bits(a, b) ? 1 : 0;
    }
    if (step != 0) return 0;
    uint32_t p = rv_u32(o, owner + 0x118);
    uint32_t px = rv_u32(o, p + 0xC0);
    uint32_t pz = rv_u32(o, p + 0xC8);
    float goal = 0.0f;
    (void)rv_c_001B1240(o, self + 0xB0, fb(px), fb(pz), &goal);
    rv_w32(o, block + 0xE8, bf(goal));
    rv_w8(o, self + 7, rv_u8(o, self + 7) + 1);
    rv_w16(o, block + 0xD2, 0x78);
    return 0;
}

int em_area01_revisit_00825040(const EmArea01RevisitHooks *h, uint32_t self, uint32_t block, int32_t *result,
                               EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (!result || rv_begin(&o, h, fault)) return -1;
    int32_t r = rv_825040(&o, self, block);
    if (rv_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ======================================================================
 * 0x824340 (func_overlay_AREA01_00824300), (self, block = self + 0x1F0 in
 * its caller 0x823CD0). held = 001C2770(self, block, 2) first; then the
 * step +0x06 (0..8; other values run only the tail):
 *  0: block+0xD8 = 0, +0x07 = 0, and +0x06 = 1 when held is 0.
 *  1: 0012D580(self, block, held).
 *  2: 00128830(self, 0, 0, -2.5), 001287F0(self, block, 0x13, 0); +0x06 += 1,
 *     +0x07 = 0, block+0xD8 = 0.
 *  3: block+0xF4 bit 0x1000: +0x06 = step + 1, +0x07 = 0, 00128830(self, 0,
 *     2.5, 0), 001287F0(self, block, 0x14, 0); otherwise 0x825040.
 *  4: block+0xF4 & 0x5000: +0x06 = step + 1, 00128830(self, 0, 1.5, 3),
 *     001287F0(self, block, 0x15, 0), block+0xD0 = 0x78, block+0xE4 = 0,
 *     block+0xF0 = 0.8, +0xC0 = -pi/4, block+0xD8 = 0.6.
 *  5: 0x824FE0; block+0xE4 & 0xF: +0x06 = +0x07 = 0. Otherwise +0xC0 =
 *     pi * (56.25 * -block+0xF0) / 180; block+0xF0 -= 0.04 (read again,
 *     stored); below 0: +0xC0 = 0, +0x06 += 1, block+0xD0 = 0; then +0xB4
 *     += block+0xF0 (read again); 0x824F70 non-zero: +0xC0 = 0, +0x05 = 6,
 *     +0x06 = 0.
 *  6: 0x824FE0; +0xB4 += 0.1 * 0011E2A8(001B1470((float)block+0xD0));
 *     block+0xD0 += 0x18; block+0xE4 & 0xF: +0x06 = +0x07 = 0; else
 *     0x824F70 non-zero: +0xC0 = 0, +0x05 = 6, +0x06 = 0; else block+0xD0
 *     > 0x870: block+0xE4 = 0x400, +0x06 += 1.
 *  7: block+0xE4 == 0x100: 001287F0(self, block, 0x11, 0), +0xC0 = 0,
 *     +0x06 += 1, block+0xD8 = 0, block+0xF4 = 0; else its & 0xF: +0x06 =
 *     +0x07 = 0.
 *  8: block+0xF4 bit 0x1000: 00128830(self, 0, 0, 1), 001287F0(self,
 *     block, 0, 0), 001287F0(self, block, 1, 6), +0xC4 = 001B1470(pi +
 *     +0xC4), +0x06 = +0x07 = 0.
 * Tail: 001C3D60(self, block) when held is 0.
 * ====================================================================== */
static void rv_824340_step5(A01Rv *o, uint32_t self, uint32_t block)
{
    rv_824fe0(o, self);
    if (rv_u32(o, block + 0xE4) & 0xFu) {
        rv_w8(o, self + 6, 0);
        rv_w8(o, self + 7, 0);
        return;
    }
    uint32_t t = em_ee_neg_bits(rv_u32(o, block + 0xF0));
    t = em_ee_mul_bits(F_56_25, t);
    t = em_ee_mul_bits(F_PI, t);
    rv_w32(o, self + 0xC0, em_ee_div_bits(t, F_180));
    uint32_t f = em_ee_sub_bits(rv_u32(o, block + 0xF0), F_0_04);
    rv_w32(o, block + 0xF0, f);
    if (em_ee_c_lt_bits(f, F_ZERO)) {
        rv_w32(o, self + 0xC0, F_ZERO);
        rv_w8(o, self + 6, rv_u8(o, self + 6) + 1);
        rv_w16(o, block + 0xD0, 0);
    }
    uint32_t step = rv_u32(o, block + 0xF0);
    uint32_t y = rv_u32(o, self + 0xB4);
    rv_w32(o, self + 0xB4, em_ee_add_bits(y, step));
    if (rv_824f70(o, self)) {
        rv_w32(o, self + 0xC0, F_ZERO);
        rv_w8(o, self + 5, 6);
        rv_w8(o, self + 6, 0);
    }
}

static void rv_824340_step6(A01Rv *o, uint32_t self, uint32_t block)
{
    rv_824fe0(o, self);
    int32_t phase = rv_s16(o, block + 0xD0);
    float wrapped = 0.0f, wave = 0.0f;
    (void)rv_c_001B1470(o, fb(em_ee_cvt_s_w_bits((uint32_t)phase)), &wrapped);
    (void)rv_c_0011E2A8(o, wrapped, &wave);
    uint32_t y = rv_u32(o, self + 0xB4);
    rv_w32(o, self + 0xB4, em_ee_add_bits(y, em_ee_mul_bits(F_0_1, bf(wave))));
    rv_w16(o, block + 0xD0, (uint32_t)(rv_s16(o, block + 0xD0) + 0x18));
    if (rv_u32(o, block + 0xE4) & 0xFu) {
        rv_w8(o, self + 6, 0);
        rv_w8(o, self + 7, 0);
    } else if (rv_824f70(o, self)) {
        rv_w32(o, self + 0xC0, F_ZERO);
        rv_w8(o, self + 5, 6);
        rv_w8(o, self + 6, 0);
    } else if (rv_s16(o, block + 0xD0) > 0x870) {
        rv_w32(o, block + 0xE4, 0x400);
        rv_w8(o, self + 6, rv_u8(o, self + 6) + 1);
    }
}

static void rv_824340(A01Rv *o, uint32_t self, uint32_t block)
{
    int32_t held = 0;
    (void)rv_c_001C2770(o, self, block, 2, &held);
    uint32_t step = rv_u8(o, self + 6);
    switch (step) {
    case 0:
        rv_w32(o, block + 0xD8, F_ZERO);
        rv_w8(o, self + 7, 0);
        if (held == 0) rv_w8(o, self + 6, 1);
        break;
    case 1:
        (void)rv_c_0012D580(o, self, block, held);
        break;
    case 2:
        (void)rv_c_00128830(o, self, fb(F_ZERO), fb(F_ZERO), fb(F_MINUS_2_5));
        (void)rv_c_001287F0(o, self, block, 0x13, fb(F_ZERO));
        rv_w8(o, self + 6, rv_u8(o, self + 6) + 1);
        rv_w8(o, self + 7, 0);
        rv_w32(o, block + 0xD8, F_ZERO);
        break;
    case 3:
        if (rv_u16(o, block + 0xF4) & 0x1000u) {
            rv_w8(o, self + 6, step + 1);
            rv_w8(o, self + 7, 0);
            (void)rv_c_00128830(o, self, fb(F_ZERO), fb(F_2_5), fb(F_ZERO));
            (void)rv_c_001287F0(o, self, block, 0x14, fb(F_ZERO));
        } else {
            (void)rv_825040(o, self, block);
        }
        break;
    case 4:
        if (rv_u16(o, block + 0xF4) & 0x5000u) {
            rv_w8(o, self + 6, step + 1);
            (void)rv_c_00128830(o, self, fb(F_ZERO), fb(F_1_5), fb(F_3));
            (void)rv_c_001287F0(o, self, block, 0x15, fb(F_ZERO));
            rv_w16(o, block + 0xD0, 0x78);
            rv_w32(o, block + 0xE4, 0);
            rv_w32(o, block + 0xF0, F_0_8);
            rv_w32(o, self + 0xC0, F_MINUS_QUARTER_PI);
            rv_w32(o, block + 0xD8, F_0_6);
        }
        break;
    case 5:
        rv_824340_step5(o, self, block);
        break;
    case 6:
        rv_824340_step6(o, self, block);
        break;
    case 7: {
        uint32_t e4 = rv_u32(o, block + 0xE4);
        if (e4 == 0x100) {
            (void)rv_c_001287F0(o, self, block, 0x11, fb(F_ZERO));
            rv_w32(o, self + 0xC0, F_ZERO);
            rv_w8(o, self + 6, rv_u8(o, self + 6) + 1);
            rv_w32(o, block + 0xD8, F_ZERO);
            rv_w16(o, block + 0xF4, 0);
        } else if (e4 & 0xFu) {
            rv_w8(o, self + 6, 0);
            rv_w8(o, self + 7, 0);
        }
        break;
    }
    case 8:
        if (rv_u16(o, block + 0xF4) & 0x1000u) {
            float yaw = 0.0f;
            (void)rv_c_00128830(o, self, fb(F_ZERO), fb(F_ZERO), fb(F_ONE));
            (void)rv_c_001287F0(o, self, block, 0, fb(F_ZERO));
            (void)rv_c_001287F0(o, self, block, 1, fb(F_6));
            (void)rv_c_001B1470(o, fb(em_ee_add_bits(F_PI, rv_u32(o, self + 0xC4))), &yaw);
            rv_w32(o, self + 0xC4, bf(yaw));
            rv_w8(o, self + 6, 0);
            rv_w8(o, self + 7, 0);
        }
        break;
    default:
        break;
    }
    if (held == 0) (void)rv_c_001C3D60(o, self, block);
}

int em_area01_revisit_00824340(const EmArea01RevisitHooks *h, uint32_t self, uint32_t block, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (rv_begin(&o, h, fault)) return -1;
    rv_824340(&o, self, block);
    return rv_end(&o);
}

/* ======================================================================
 * Shared by 0x824770 and 0x824D50: the matrix `m` (a scratchpad matrix)
 * from the angles at `angles` (+0, +4, +8: 00102B08, 00102BB0, 00102A60
 * after 001029C0), its translation row from `position` (001031E0), then
 * 001026D0(m, bone + 0x90, m) with bone = the word at owner + 0x110 +
 * 4 * (0x829DC0[index]) (32-bit address arithmetic).
 * ====================================================================== */
static void rv_spot_matrix(A01Rv *o, uint32_t m, uint32_t row, uint32_t angles, uint32_t position,
                           uint32_t owner, uint32_t index)
{
    (void)rv_c_001029C0(o, m);
    (void)rv_c_00102B08(o, m, m, fb(rv_u32(o, angles + 0)));
    (void)rv_c_00102BB0(o, m, m, fb(rv_u32(o, angles + 4)));
    (void)rv_c_00102A60(o, m, m, fb(rv_u32(o, angles + 8)));
    (void)rv_c_001031E0(o, row, position);
    uint32_t slot = rv_u32(o, RV_BONES_829DC0 + 4 * index);
    uint32_t bone = rv_u32(o, (slot << 2) + owner + 0x110);
    (void)rv_c_001026D0(o, m, bone + 0x90, m);
}

/* ======================================================================
 * 0x824770 (func_overlay_AREA01_00824730), (self, ev = self + 0x1F0 in its
 * caller 0x823CD0). Reads, in order, +0x06, ev+0xE2 (the spot index) and
 * D_008106C0 (the owner, pl). Spot = 0x829DE0 + 0x18 * index.
 *  0: +0x06 += 1; ev+0x30..0x38 = spot position, ev+0x40..0x48 = spot
 *     angles; the matrix 0x700036A0 from ev+0x40.. and ev+0x30 on pl's
 *     bone (rv_spot_matrix); ev+0x10..0x18 = its row 0x700036D0.. - +0xB0..
 *     (the z term loads +0xB8 first); ev+0x1C = 1; ev+0xE8 = pl+0xC4;
 *     ev+0xD4 = 1; then as 1.
 *  1: +0xC4 = 001B12B0(pi + pl+0xC4, +0xC4, 4 degrees); +0xC0 =
 *     001B12B0(-pi/2, +0xC0, 4 degrees); ev+0xD4 -= 0.02; 00103230(ev+0x10,
 *     ev+0x10, that); yaw = 001B1470(pl+0xC4 - ev+0xE8); 0x700036A0 =
 *     identity turned (00102BB0) by 001B1470(pi + yaw); 001026A0(0x700038A0,
 *     0x700036A0, ev+0x10); the spot matrix again; +0xB0 = 36D0 + 38A0,
 *     +0xB4 = 36D4 - 38A4, +0xB8 = 36D8 + 38A8; the actor matrix
 *     0x70003000 from +0xC0..; 00102958(+0xD0, 0x70003000); 001031E0(
 *     0x70003030, +0xB0). When ev+0xD4 <= 0.2: ev+0x30..0x38 = spot
 *     position, +0xC0..0xC8 = ev+0x40..0x48, 001287F0(self, ev, 0x16, 0),
 *     ev+0xD0 = 0x1C2; ev+0xE2 (read again) 0: D_008107DF = 0x10, +0x04 =
 *     4, +0x05 = +0x06 = 0; else +0x06 += 1, +0x07 = 0.
 *  2: the matrix 0x70003000 from +0xC0.. and ev+0x30 on pl's bone;
 *     001031E0(+0xB0, 0x70003030); ev+0xD0 -= 1; at 0: +0x05 = 7, +0x06 =
 *     +0x07 = 0.
 *  Other steps: nothing.
 * ====================================================================== */
static void rv_824770_spot(A01Rv *o, uint32_t ev, uint32_t spot)
{
    rv_w32(o, ev + 0x30, rv_u32(o, spot + 0x00));
    rv_w32(o, ev + 0x34, rv_u32(o, spot + 0x04));
    rv_w32(o, ev + 0x38, rv_u32(o, spot + 0x08));
}

static void rv_824770(A01Rv *o, uint32_t self, uint32_t ev)
{
    uint32_t step = rv_u8(o, self + 6);
    uint32_t index = rv_u8(o, ev + 0xE2);
    uint32_t pl = rv_u32(o, D_008106C0);
    uint32_t spot = RV_SPOTS_829DE0 + 0x18 * index;
    uint32_t a, b;

    if (step == 2) {
        rv_spot_matrix(o, S_3000, S_3030, self + 0xC0, ev + 0x30, pl, index);
        (void)rv_c_001031E0(o, self + 0xB0, S_3030);
        uint32_t n = (uint32_t)(rv_s16(o, ev + 0xD0) - 1);
        rv_w16(o, ev + 0xD0, n);
        if ((n & 0xFFFFu) == 0) {
            rv_w8(o, self + 5, 7);
            rv_w8(o, self + 6, 0);
            rv_w8(o, self + 7, 0);
        }
        return;
    }
    if (step == 0) {
        rv_w8(o, self + 6, step + 1);
        rv_824770_spot(o, ev, spot);
        rv_w32(o, ev + 0x40, rv_u32(o, spot + 0x0C));
        rv_w32(o, ev + 0x44, rv_u32(o, spot + 0x10));
        rv_w32(o, ev + 0x48, rv_u32(o, spot + 0x14));
        rv_spot_matrix(o, S_36A0, S_36D0, ev + 0x40, ev + 0x30, pl, index);
        a = rv_u32(o, S_36D0);
        b = rv_u32(o, self + 0xB0);
        rv_w32(o, ev + 0x10, em_ee_sub_bits(a, b));
        a = rv_u32(o, S_36D4);
        b = rv_u32(o, self + 0xB4);
        rv_w32(o, ev + 0x14, em_ee_sub_bits(a, b));
        b = rv_u32(o, self + 0xB8);
        a = rv_u32(o, S_36D8);
        rv_w32(o, ev + 0x18, em_ee_sub_bits(a, b));
        rv_w32(o, ev + 0x1C, F_ONE);
        rv_w32(o, ev + 0xE8, rv_u32(o, pl + 0xC4));
        rv_w32(o, ev + 0xD4, F_ONE);
    } else if (step != 1) {
        return;
    }

    /* step 1 (and the rest of step 0) */
    float r1 = 0.0f, r2 = 0.0f, yaw = 0.0f, turn = 0.0f;
    uint32_t heading = rv_u32(o, pl + 0xC4);
    uint32_t current = rv_u32(o, self + 0xC4);
    (void)rv_c_001B12B0(o, fb(em_ee_add_bits(F_PI, heading)), fb(current), fb(F_TURN_4DEG), &r1);
    rv_w32(o, self + 0xC4, bf(r1));
    (void)rv_c_001B12B0(o, fb(F_MINUS_HALF_PI), fb(rv_u32(o, self + 0xC0)), fb(F_TURN_4DEG), &r2);
    rv_w32(o, self + 0xC0, bf(r2));
    uint32_t scale = em_ee_sub_bits(rv_u32(o, ev + 0xD4), F_0_02);
    rv_w32(o, ev + 0xD4, scale);
    (void)rv_c_00103230(o, ev + 0x10, ev + 0x10, fb(scale));
    a = rv_u32(o, pl + 0xC4);
    b = rv_u32(o, ev + 0xE8);
    (void)rv_c_001B1470(o, fb(em_ee_sub_bits(a, b)), &yaw);
    (void)rv_c_001029C0(o, S_36A0);
    (void)rv_c_001B1470(o, fb(em_ee_add_bits(F_PI, bf(yaw))), &turn);
    (void)rv_c_00102BB0(o, S_36A0, S_36A0, turn);
    (void)rv_c_001026A0(o, S_38A0, S_36A0, ev + 0x10);
    rv_spot_matrix(o, S_36A0, S_36D0, ev + 0x40, ev + 0x30, pl, index);
    a = rv_u32(o, S_36D0);
    b = rv_u32(o, S_38A0);
    rv_w32(o, self + 0xB0, em_ee_add_bits(a, b));
    a = rv_u32(o, S_36D4);
    b = rv_u32(o, S_38A4);
    rv_w32(o, self + 0xB4, em_ee_sub_bits(a, b));
    a = rv_u32(o, S_36D8);
    b = rv_u32(o, S_38A8);
    rv_w32(o, self + 0xB8, em_ee_add_bits(a, b));
    (void)rv_c_001029C0(o, S_3000);
    (void)rv_c_00102B08(o, S_3000, S_3000, fb(rv_u32(o, self + 0xC0)));
    (void)rv_c_00102BB0(o, S_3000, S_3000, fb(rv_u32(o, self + 0xC4)));
    (void)rv_c_00102A60(o, S_3000, S_3000, fb(rv_u32(o, self + 0xC8)));
    (void)rv_c_00102958(o, self + 0xD0, S_3000);
    (void)rv_c_001031E0(o, S_3030, self + 0xB0);
    if (!em_ee_c_le_bits(rv_u32(o, ev + 0xD4), F_0_2)) return;
    rv_824770_spot(o, ev, spot);
    rv_w32(o, self + 0xC0, rv_u32(o, ev + 0x40));
    rv_w32(o, self + 0xC4, rv_u32(o, ev + 0x44));
    rv_w32(o, self + 0xC8, rv_u32(o, ev + 0x48));
    (void)rv_c_001287F0(o, self, ev, 0x16, fb(F_ZERO));
    rv_w16(o, ev + 0xD0, 0x1C2);
    if (rv_u8(o, ev + 0xE2) == 0) {
        rv_w8(o, D_008107DF, 0x10);
        rv_w8(o, self + 4, 4);
        rv_w8(o, self + 5, 0);
        rv_w8(o, self + 6, 0);
    } else {
        rv_w8(o, self + 6, rv_u8(o, self + 6) + 1);
        rv_w8(o, self + 7, 0);
    }
}

int em_area01_revisit_00824770(const EmArea01RevisitHooks *h, uint32_t self, uint32_t block, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (rv_begin(&o, h, fault)) return -1;
    rv_824770(&o, self, block);
    return rv_end(&o);
}

/* ======================================================================
 * 0x824D50 (func_overlay_AREA01_00824D10), (self, ev = self + 0x1F0 in its
 * caller 0x823CD0). Reads, in order, +0x06, D_008106C0 (pl) and ev+0xE2.
 *  0: +0x06 = step + 1, 001287F0(self, ev, 0x19, 0), ev+0xD0 = 0xF0,
 *     ev+0xD4 = 1.
 *  1: the matrix 0x70003000 from +0xC0.. and ev+0x30 on pl's bone;
 *     001031E0(+0xB0, 0x70003030); 00102958(0x70003400, 0x70003000);
 *     001C69A0(self); 0012DE90(ev); unless ev+0xD4 < 2.0: 0x700038A0 =
 *     (0, 1, 1, 1) and 0x700038B0 = (0, 0, 0, 1) (stored A0, B0, B4, B8,
 *     A4, A8, AC, BC), 001026A0(0x700038A0, ((D_00275B40)+0xC)+0x90,
 *     0x700038A0), 001FB9F0(0x1B2, 0x1000, 0x1000, 0x1000),
 *     001EFD90(0x80000009, 0x700038A0, 0x700038B0), +0x04 = 3.
 *  Other steps: nothing.
 * ====================================================================== */
static void rv_824d50(A01Rv *o, uint32_t self, uint32_t ev)
{
    uint32_t step = rv_u8(o, self + 6);
    uint32_t pl = rv_u32(o, D_008106C0);
    uint32_t index = rv_u8(o, ev + 0xE2);
    if (step == 0) {
        rv_w8(o, self + 6, step + 1);
        (void)rv_c_001287F0(o, self, ev, 0x19, fb(F_ZERO));
        rv_w16(o, ev + 0xD0, 0xF0);
        rv_w32(o, ev + 0xD4, F_ONE);
        return;
    }
    if (step != 1) return;
    rv_spot_matrix(o, S_3000, S_3030, self + 0xC0, ev + 0x30, pl, index);
    (void)rv_c_001031E0(o, self + 0xB0, S_3030);
    (void)rv_c_00102958(o, S_3400, S_3000);
    (void)rv_c_001C69A0(o, self);
    (void)rv_c_0012DE90(o, ev);
    if (em_ee_c_lt_bits(rv_u32(o, ev + 0xD4), F_2)) return;
    rv_w32(o, S_38A0, F_ZERO);
    rv_w32(o, S_38B0, F_ZERO);
    rv_w32(o, S_38B4, F_ZERO);
    rv_w32(o, S_38B8, F_ZERO);
    rv_w32(o, S_38A4, F_ONE);
    rv_w32(o, S_38A8, F_ONE);
    rv_w32(o, S_38AC, F_ONE);
    rv_w32(o, S_38BC, F_ONE);
    uint32_t table = rv_u32(o, D_00275B40);
    uint32_t record = rv_u32(o, table + 0xC);
    (void)rv_c_001026A0(o, S_38A0, record + 0x90, S_38A0);
    (void)rv_c_001FB9F0(o, 0x1B2, 0x1000, 0x1000, 0x1000);
    (void)rv_c_001EFD90(o, (int32_t)0x80000009u, S_38A0, S_38B0);
    rv_w8(o, self + 4, 3);
}

int em_area01_revisit_00824D50(const EmArea01RevisitHooks *h, uint32_t self, uint32_t block, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (rv_begin(&o, h, fault)) return -1;
    rv_824d50(&o, self, block);
    return rv_end(&o);
}

/* ======================================================================
 * 0x825910 (func_overlay_AREA01_008258D0): 1 when a0+0x3C <= 505.0, else 0.
 * ====================================================================== */
int em_area01_revisit_00825910(const EmArea01RevisitHooks *h, uint32_t a0, uint32_t a1, uint32_t a2,
                               int32_t *result, EmArea01RevisitFault *fault)
{
    A01Rv o;
    (void)a1;
    (void)a2;
    if (!result || rv_begin(&o, h, fault)) return -1;
    int32_t r = em_ee_c_le_bits(rv_u32(&o, a0 + 0x3C), F_505) ? 1 : 0;
    if (rv_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ======================================================================
 * The tail shared by 0x825BE0 / 0x825D30 / 0x825EA0: 001C68C0(self),
 * +0x01 = 1, then the +0x4C callback.
 * ====================================================================== */
static void rv_draw_tail(A01Rv *o, uint32_t self)
{
    (void)rv_c_001C68C0(o, self);
    rv_w8(o, self + 1, 1);
    (void)rv_callback(o, self);
}

/* ======================================================================
 * 0x825BE0 (func_overlay_AREA01_00825BA0). Stack local quad = sp - 0x40, a
 * copy of the four points 0x82B090 made on every call (all four read, then
 * all four written). +0x05 0: 001C63E0(self, 7), +0x05 = 1. 1: when
 * 001B1EA0(0, D_00810350, quad, 4) is non-zero: +0x05 = 2, D_008107DF = 1,
 * 001BA1A0(self + 0x1F0, 0x82AA90). 2: when 001BA1F0(self) is non-zero:
 * +0x05 = 0, D_008107DF = 2, 00102948(D_008105E0, (+0x18)+0xB0). Other
 * steps: nothing. Then 0x826010(self), 001C64F0(self, 1.0), 001C68C0(self),
 * +0x01 = 1 and the +0x4C callback.
 * ====================================================================== */
static void rv_825be0(A01Rv *o, uint32_t self, uint32_t sp)
{
    uint32_t quad = sp - 0x40;
    uint64_t q[8];
    int32_t r = 0;
    for (int i = 0; i < 8; i++) q[i] = rv_u64(o, RV_QUAD_82B090 + 8u * (uint32_t)i);
    for (int i = 0; i < 8; i++) rv_w64(o, quad + 8u * (uint32_t)i, q[i]);
    uint32_t step = rv_u8(o, self + 5);
    if (step == 2) {
        (void)rv_c_001BA1F0(o, self, &r);
        if (r != 0) {
            rv_w8(o, self + 5, 0);
            rv_w8(o, D_008107DF, 2);
            (void)rv_c_00102948(o, D_008105E0, rv_u32(o, self + 0x18) + 0xB0);
        }
    } else if (step == 1) {
        (void)rv_c_001B1EA0(o, 0, D_00810350, quad, 4, &r);
        if (r != 0) {
            rv_w8(o, self + 5, 2);
            rv_w8(o, D_008107DF, 1);
            (void)rv_c_001BA1A0(o, self + 0x1F0, RV_SCRIPT_82AA90);
        }
    } else if (step == 0) {
        (void)rv_c_001C63E0(o, self, 7);
        rv_w8(o, self + 5, 1);
    }
    (void)rv_c_00826010(o, self);
    r = 0;
    (void)rv_c_001C64F0(o, self, fb(F_ONE), &r);
    rv_draw_tail(o, self);
}

int em_area01_revisit_00825BE0(const EmArea01RevisitHooks *h, uint32_t self, uint32_t sp, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (rv_begin(&o, h, fault)) return -1;
    rv_825be0(&o, self, sp);
    return rv_end(&o);
}

/* ======================================================================
 * 0x825D30 (func_overlay_AREA01_00825CF0). block = self + 0x1F0.
 * +0x05 0: 00102948(D_008105E0, (+0x18)+0xB0), 0x826010(self),
 * 001C64F0(self, 1.0); when D_008107DF == 0x10: 001BA1A0(block, 0x82AC10),
 * 001CA6F0(self, 2), +0x05 += 1, +0xB0..+0xCC = 0 (eight words).
 * 1: when 001BA1F0(self) is non-zero: +0x05 = 0, D_008107DF = 0x40,
 * byte +0x04 of the node at +0x1C = 3, 001BA1A0(block, 0x82AD10) (the
 * original also leaves 3 in a2; 001BA1A0 does not read it); then block+0x0E
 * = 001C64F0(self, 1.0) (halfword); +0x28 += 1 (stored, read again); at
 * >= 0x26C block+0x0E = 0x1000. Other steps: nothing. Then 001C68C0(self),
 * +0x01 = 1 and the +0x4C callback.
 * ====================================================================== */
static void rv_825d30(A01Rv *o, uint32_t self)
{
    uint32_t block = self + 0x1F0;
    uint32_t step = rv_u8(o, self + 5);
    int32_t r = 0;
    if (step == 1) {
        (void)rv_c_001BA1F0(o, self, &r);
        if (r != 0) {
            rv_w8(o, self + 5, 0);
            rv_w8(o, D_008107DF, 0x40);
            rv_w8(o, rv_u32(o, self + 0x1C) + 4, 3);
            (void)rv_c_001BA1A0(o, block, RV_SCRIPT_82AD10);
        }
        r = 0;
        (void)rv_c_001C64F0(o, self, fb(F_ONE), &r);
        rv_w16(o, block + 0xE, (uint32_t)r);
        rv_w16(o, self + 0x28, (uint32_t)(rv_s16(o, self + 0x28) + 1));
        if (rv_s16(o, self + 0x28) >= 0x26C) rv_w16(o, block + 0xE, 0x1000);
    } else if (step == 0) {
        (void)rv_c_00102948(o, D_008105E0, rv_u32(o, self + 0x18) + 0xB0);
        (void)rv_c_00826010(o, self);
        (void)rv_c_001C64F0(o, self, fb(F_ONE), &r);
        if (rv_u8(o, D_008107DF) == 0x10) {
            (void)rv_c_001BA1A0(o, block, RV_SCRIPT_82AC10);
            (void)rv_c_001CA6F0(o, self, 2);
            rv_w8(o, self + 5, rv_u8(o, self + 5) + 1);
            for (uint32_t at = 0xB0; at <= 0xCC; at += 4) rv_w32(o, self + at, F_ZERO);
        }
    }
    rv_draw_tail(o, self);
}

int em_area01_revisit_00825D30(const EmArea01RevisitHooks *h, uint32_t self, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (rv_begin(&o, h, fault)) return -1;
    rv_825d30(&o, self);
    return rv_end(&o);
}

/* ======================================================================
 * 0x825EA0 (func_overlay_AREA01_00825E60): when 001BA1F0(self) is non-zero,
 * +0x05 = 0 and D_008107DF = 0x80; then 001C68C0(self), +0x01 = 1 and the
 * +0x4C callback.
 * ====================================================================== */
static void rv_825ea0(A01Rv *o, uint32_t self)
{
    int32_t r = 0;
    (void)rv_c_001BA1F0(o, self, &r);
    if (r != 0) {
        rv_w8(o, self + 5, 0);
        rv_w8(o, D_008107DF, 0x80);
    }
    rv_draw_tail(o, self);
}

int em_area01_revisit_00825EA0(const EmArea01RevisitHooks *h, uint32_t self, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (rv_begin(&o, h, fault)) return -1;
    rv_825ea0(&o, self);
    return rv_end(&o);
}

/* ======================================================================
 * 0x825F00 (func_overlay_AREA01_00825EC0). +0x05 0: 001BA1A0(self + 0x1F0,
 * 0x82AD90), +0x05 = 1, +0x28 = 0. 1: when 001BA1F0(self) is non-zero:
 * +0x2E = 0xFFFF, D_008107DF = 0xFF, 001C47A0(0x20, 1), 001C4760(5, 1),
 * 001B6660(0x829220), 001B6660(0x8291C0), 001FAE70(0), +0x04 = 3. Other
 * steps: nothing.
 * ====================================================================== */
static void rv_825f00(A01Rv *o, uint32_t self)
{
    uint32_t step = rv_u8(o, self + 5);
    int32_t r = 0;
    uint32_t node = 0;
    if (step == 1) {
        (void)rv_c_001BA1F0(o, self, &r);
        if (r == 0) return;
        rv_w16(o, self + 0x2E, 0xFFFF);
        rv_w8(o, D_008107DF, 0xFF);
        (void)rv_c_001C47A0(o, 0x20, 1, &r);
        (void)rv_c_001C4760(o, 5, 1, &r);
        (void)rv_c_001B6660(o, RV_GROUP_829220, &node);
        (void)rv_c_001B6660(o, RV_GROUP_8291C0, &node);
        (void)rv_c_001FAE70(o, 0);
        rv_w8(o, self + 4, 3);
    } else if (step == 0) {
        (void)rv_c_001BA1A0(o, self + 0x1F0, RV_SCRIPT_82AD90);
        rv_w8(o, self + 5, 1);
        rv_w16(o, self + 0x28, 0);
    }
}

int em_area01_revisit_00825F00(const EmArea01RevisitHooks *h, uint32_t self, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (rv_begin(&o, h, fault)) return -1;
    rv_825f00(&o, self);
    return rv_end(&o);
}

/* ======================================================================
 * 0x825FC0 (func_overlay_AREA01_00825F80): 001C64F0(self, 1.0),
 * 001C68C0(self), 001B17A0(self), the +0x4C callback.
 * ====================================================================== */
static void rv_825fc0(A01Rv *o, uint32_t self)
{
    int32_t r = 0;
    (void)rv_c_001C64F0(o, self, fb(F_ONE), &r);
    (void)rv_c_001C68C0(o, self);
    (void)rv_c_001B17A0(o, self, &r);
    (void)rv_callback(o, self);
}

int em_area01_revisit_00825FC0(const EmArea01RevisitHooks *h, uint32_t self, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (rv_begin(&o, h, fault)) return -1;
    rv_825fc0(&o, self);
    return rv_end(&o);
}

/* ======================================================================
 * 0x825950 (func_overlay_AREA01_00825910), the behaviour of the 0x828A00
 * records with +0x0D 0x47 and 0x4B (sp: its own frame is 0x20 bytes, so
 * 0x825BE0 runs with sp - 0x20).
 * +0x04 0: +0x0D 0x47 (read again after the store): D_008106C0 = self,
 *   001B10B0(self, +0x0D, 0x4A), +0x04 = 1, +0x38 = 1.0, +0x00 = 1, +0x28 =
 *   +0x2A = 0, +0x58 = D_0028A5C4, 0019C6F0(0x29, 0), +0x240 = 0. Then
 *   +0x0D (read again) 0x4B: D_00810845 bit 5 set: +0x04 = 3; else
 *   001B10B0(self, 0x4B, 0x4C), 001C63E0(self, 0), +0x00 = 1,
 *   001CA6F0(self, 0), +0x58 = 0, 0019C6F0(0x29, 1), +0x04 = 1.
 * 1: +0x0D 0x47: D_008106C0 = self; D_008107DF 0/1 -> 0x825BE0, 2/0x10 ->
 *   0x825D30, 0x40 -> 0x825EA0, 0x80 -> 0x825F00, other values nothing.
 *   Then +0x0D (read again) 0x4B: D_008107DF 0xFF -> 0x825FC0.
 * 2, 3: +0x0D 0x47: D_008106C0 = 0; then 001AFC10(self).
 * Other states: nothing.
 * ====================================================================== */
static void rv_825950(A01Rv *o, uint32_t self, uint32_t sp)
{
    uint32_t state = rv_u8(o, self + 4);
    int32_t r = 0;
    if (state == 3 || state == 2) {
        if (rv_u8(o, self + 0xD) == 0x47) rv_w32(o, D_008106C0, 0);
        (void)rv_c_001AFC10(o, self);
        return;
    }
    if (state == 1) {
        if (rv_u8(o, self + 0xD) == 0x47) {
            rv_w32(o, D_008106C0, self);
            switch (rv_u8(o, D_008107DF)) {
            case 0:
            case 1:
                rv_825be0(o, self, sp - 0x20);
                break;
            case 2:
            case 0x10:
                rv_825d30(o, self);
                break;
            case 0x40:
                rv_825ea0(o, self);
                break;
            case 0x80:
                rv_825f00(o, self);
                break;
            default:
                break;
            }
        }
        if (rv_u8(o, self + 0xD) == 0x4B && rv_u8(o, D_008107DF) == 0xFF) rv_825fc0(o, self);
        return;
    }
    if (state != 0) return;
    if (rv_u8(o, self + 0xD) == 0x47) {
        rv_w32(o, D_008106C0, self);
        (void)rv_c_001B10B0(o, self, (int32_t)rv_u8(o, self + 0xD), 0x4A, &r);
        rv_w8(o, self + 4, 1);
        rv_w32(o, self + 0x38, F_ONE);
        rv_w8(o, self + 0, 1);
        rv_w16(o, self + 0x28, 0);
        rv_w16(o, self + 0x2A, 0);
        rv_w32(o, self + 0x58, rv_u32(o, D_0028A5C4));
        (void)rv_c_0019C6F0(o, 0x29, 0, &r);
        rv_w8(o, self + 0x240, 0);
    }
    uint32_t id = rv_u8(o, self + 0xD);
    if (id != 0x4B) return;
    if (rv_u8(o, D_00810845) & 0x20u) {
        rv_w8(o, self + 4, 3);
        return;
    }
    (void)rv_c_001B10B0(o, self, (int32_t)id, 0x4C, &r);
    (void)rv_c_001C63E0(o, self, 0);
    rv_w8(o, self + 0, 1);
    (void)rv_c_001CA6F0(o, self, 0);
    rv_w32(o, self + 0x58, 0);
    (void)rv_c_0019C6F0(o, 0x29, 1, &r);
    rv_w8(o, self + 4, 1);
}

int em_area01_revisit_00825950(const EmArea01RevisitHooks *h, uint32_t self, uint32_t sp, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (rv_begin(&o, h, fault)) return -1;
    rv_825950(&o, self, sp);
    return rv_end(&o);
}

/* ======================================================================
 * 0x8282F0 (func_overlay_AREA01_008282B0), (self, matrix). Stack locals:
 * dir = sp - 0x20, tint = sp - 0x10. Called by 0x826D40 with the matrix of
 * its record B (+0x90).
 *  - 0x700038A0 = (60, 0, 0, 0) (stored A0, then dir = (3, -2, 0, 1), then
 *    A8, A4, AC); 001026A0(0x700038A0, matrix, 0x700038A0); 001026A0(dir,
 *    matrix, dir); 001028B8(0x700038A0, 0x700038A0, dir); 38AC = 1.
 *  - hit = 2 when 0019AA80(dir, 0x700038A0, 0x20) is non-zero (then
 *    00102948(0x700038A0, 0x700031B0)), else 0.
 *  - The words 0x700031D8, D4, D0 are read (in that order); when
 *    0019A570(dir, 0x700038A0, 7, 0x20) is non-zero: 001028D0(0x700038A0,
 *    0x700031B0, D_00810360), 38AC = 0, d = 00102738(0x700038A0,
 *    0x700038A0), 0x70003A20 = d; hit = 1 when d > 10000, else 1 when
 *    31D8 == 1 and byte +3 of the record at 31D4 is 0x10..0x13, else 2.
 *    When it is zero the three words are stored back (D8, D4, D0).
 *  - hit 2: when self+0x204 is 0 and 31D8 == 1, self+0x204 = 31D4. Then
 *    +0x04 == 4: 001031E0(0x700038A0, 0x700031B0), 38AC = 1, x =
 *    00122BB8() >> 16, x = (x * 0xFFFF) >> 15 (32-bit, arithmetic), 38B0 =
 *    ((x >> 15) & 0x1F) + 0x40, 38B4 = 38B8 = 0, 38BC = 0x80; the four
 *    words read back (BC, B8, B4, B0) into rgba = BC << 24 | B8 << 16 |
 *    B4 << 8 | B0; 001CD520(0, 2, 0x700038A0, tag 0x20045BA5154222DC,
 *    rgba, 3, 3, 2); 38C0 = 0.8, 38C8 = 38C4 = 0; 001E2BA0(dir, 0x700038A0,
 *    0x700038C0, 100). Otherwise, when self+0x200 >= 13: tint = the vector
 *    0x82CB20; 001031E0(0x700038A0, 0x700031B0); D_00275B40 read, 38AC = 1;
 *    001026A0(0x700038C0, ((D_00275B40)+0xC)+0x90, tint); 38CC = 1;
 *    38D8 = 38D4 = 38D0 = 0.8; 001E2BA0(0x700038A0, 0x700038C0,
 *    0x700038D0, 100).
 *  - hit 1: 001031E0(0x700038B0, 0x700031B0), 38BC = 1, 38C0 = 0.8,
 *    38C8 = 38C4 = 0, 001E2BA0(dir, 0x700038B0, 0x700038C0, 100).
 *  Returns 0 unless hit is 2; then 2 when self+0x204 == 31D4 (both read
 *  again), else 1.
 * ====================================================================== */
static int32_t rv_8282f0(A01Rv *o, uint32_t self, uint32_t matrix, uint32_t sp)
{
    uint32_t dir = sp - 0x20, tint = sp - 0x10;
    int32_t r = 0;
    int hit;

    rv_w32(o, S_38A0, F_60);
    rv_w32(o, dir + 0x0, F_3);
    rv_w32(o, dir + 0x4, F_MINUS_2);
    rv_w32(o, dir + 0x8, F_ZERO);
    rv_w32(o, dir + 0xC, F_ONE);
    rv_w32(o, S_38A8, F_ZERO);
    rv_w32(o, S_38A4, F_ZERO);
    rv_w32(o, S_38AC, F_ZERO);
    (void)rv_c_001026A0(o, S_38A0, matrix, S_38A0);
    (void)rv_c_001026A0(o, dir, matrix, dir);
    (void)rv_c_001028B8(o, S_38A0, S_38A0, dir);
    rv_w32(o, S_38AC, F_ONE);
    (void)rv_c_0019AA80(o, dir, S_38A0, 0x20, &r);
    if (r != 0) {
        (void)rv_c_00102948(o, S_38A0, S_31B0);
        hit = 2;
    } else {
        hit = 0;
    }
    uint32_t save_d8 = rv_u32(o, S_31D8);
    uint32_t save_d4 = rv_u32(o, S_31D4);
    uint32_t save_d0 = rv_u32(o, S_31D0);
    r = 0;
    (void)rv_c_0019A570(o, dir, S_38A0, 7, 0x20, &r);
    if (r != 0) {
        float d = 0.0f;
        (void)rv_c_001028D0(o, S_38A0, S_31B0, D_00810360);
        rv_w32(o, S_38AC, F_ZERO);
        (void)rv_c_00102738(o, S_38A0, S_38A0, &d);
        rv_w32(o, S_3A20, bf(d));
        if (!em_ee_c_le_bits(bf(d), F_10000)) {
            hit = 1;
        } else if (rv_u32(o, S_31D8) != 1) {
            hit = 2;
        } else {
            uint32_t kind = rv_u8(o, rv_u32(o, S_31D4) + 3);
            hit = (kind >= 0x10 && kind < 0x14) ? 1 : 2;
        }
    } else {
        rv_w32(o, S_31D8, save_d8);
        rv_w32(o, S_31D4, save_d4);
        rv_w32(o, S_31D0, save_d0);
    }

    if (hit == 2) {
        if (rv_u32(o, self + 0x204) == 0 && rv_u32(o, S_31D8) == 1) rv_w32(o, self + 0x204, rv_u32(o, S_31D4));
        if (rv_u8(o, self + 4) == 4) {
            (void)rv_c_001031E0(o, S_38A0, S_31B0);
            rv_w32(o, S_38AC, F_ONE);
            r = 0;
            (void)rv_c_00122BB8(o, &r);
            int32_t x = r >> 16;
            x = (int32_t)(((uint32_t)x << 16) - (uint32_t)x) >> 15;
            rv_w32(o, S_38B0, (uint32_t)(((x >> 15) & 0x1F) + 0x40));
            rv_w32(o, S_38B4, 0);
            rv_w32(o, S_38B8, 0);
            rv_w32(o, S_38BC, 0x80);
            uint32_t bc = rv_u32(o, S_38BC);
            uint32_t b8 = rv_u32(o, S_38B8);
            uint32_t b4 = rv_u32(o, S_38B4);
            uint32_t b0 = rv_u32(o, S_38B0);
            uint32_t rgba = b0 | (b4 << 8 | (bc << 24 | b8 << 16));
            r = 0;
            (void)rv_c_001CD520(o, 0, 2, S_38A0, GIFTAG_8282F0, rgba, fb(F_3), fb(F_3), fb(F_2), &r);
            rv_w32(o, S_38C0, F_0_8);
            rv_w32(o, S_38C8, F_ZERO);
            rv_w32(o, S_38C4, F_ZERO);
            (void)rv_c_001E2BA0(o, dir, S_38A0, S_38C0, fb(F_100));
        } else if ((int32_t)rv_u32(o, self + 0x200) >= 0xD) {
            rv_copy16(o, tint, RV_VEC_82CB20);
            (void)rv_c_001031E0(o, S_38A0, S_31B0);
            uint32_t table = rv_u32(o, D_00275B40);
            rv_w32(o, S_38AC, F_ONE);
            uint32_t record = rv_u32(o, table + 0xC);
            (void)rv_c_001026A0(o, S_38C0, record + 0x90, tint);
            rv_w32(o, S_38CC, F_ONE);
            rv_w32(o, S_38D8, F_0_8);
            rv_w32(o, S_38D4, F_0_8);
            rv_w32(o, S_38D0, F_0_8);
            (void)rv_c_001E2BA0(o, S_38A0, S_38C0, S_38D0, fb(F_100));
        }
    } else if (hit == 1) {
        (void)rv_c_001031E0(o, S_38B0, S_31B0);
        rv_w32(o, S_38BC, F_ONE);
        rv_w32(o, S_38C0, F_0_8);
        rv_w32(o, S_38C8, F_ZERO);
        rv_w32(o, S_38C4, F_ZERO);
        (void)rv_c_001E2BA0(o, dir, S_38B0, S_38C0, fb(F_100));
    }
    if (hit != 2) return 0;
    uint32_t mine = rv_u32(o, self + 0x204);
    uint32_t other = rv_u32(o, S_31D4);
    return mine == other ? 2 : 1;
}

int em_area01_revisit_008282F0(const EmArea01RevisitHooks *h, uint32_t self, uint32_t matrix, uint32_t sp,
                               int32_t *result, EmArea01RevisitFault *fault)
{
    A01Rv o;
    if (!result || rv_begin(&o, h, fault)) return -1;
    int32_t r = rv_8282f0(&o, self, matrix, sp);
    if (rv_failed(&o)) return -1;
    *result = r;
    return 0;
}
