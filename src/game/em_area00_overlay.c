/* AREA00 overlay owners (see em_area00_overlay.h, docs/AREA00_OVERLAY_PORT.md).
 *
 * Each function below is a translation of the original AREA00 overlay code
 * at the named runtime address. All thirteen follow the decomp's
 * byte-identical C (src/overlays/AREA00/<name>.c, link name = runtime -
 * 0x40). Calls, their arguments and the memory accesses between each two
 * calls follow the original: the test compares memory at every call entry
 * and after the last store, and the memory accesses between calls one for
 * one, in order, by address and size (docs/AREA00_OVERLAY_PORT.md section
 * 3). Where the original loads the operands of one expression in a set
 * order, the translation loads them in separate statements in that order,
 * because C leaves the order of evaluation of arguments and operands open.
 * Float arithmetic is the EE model (em_ee_float.h) on bit patterns; the
 * operand order of each add/sub/mul/div/compare is the original's.
 */
#include "em_area00_overlay_internal.h"

/* Original globals the functions name. */
#define D_00246F20 0x00246F20u /* a boot script (0x8263C0 starts it) */
#define D_00246FB4 0x00246FB4u /* word stored before that script starts */
#define D_00275B40 0x00275B40u /* current bone table (actor + 0x110 while a behaviour runs) */
#define D_002758A8 0x002758A8u
#define D_002758B0 0x002758B0u
#define D_002758B8 0x002758B8u
#define D_008102B0 0x008102B0u /* player actor */
#define D_00810354 0x00810354u /* float */
#define D_008104E6 0x008104E6u
#define D_00810701 0x00810701u /* area sub-state */
#define D_00810702 0x00810702u /* the player's spawn entry */
#define D_0081075A 0x0081075Au /* AREA00 flag 2 */
#define D_0081075B 0x0081075Bu /* AREA00 flag 3 */
#define D_0081075D 0x0081075Du /* AREA00 flag 5 */
#define D_0081075E 0x0081075Eu /* AREA00 flag 6 */
#define D_008107DB 0x008107DBu /* AREA00 counter 3 */
#define D_008107DC 0x008107DCu /* AREA00 counter 4 (ferry bits) */

/* Scratchpad. */
#define S_70003000 0x70003000u /* 4x4 matrix */
#define S_70003400 0x70003400u /* 4x4 matrix */
#define S_70003600 0x70003600u /* three colour words (0x823820) */
#define S_70003604 0x70003604u
#define S_70003608 0x70003608u
#define S_700036A0 0x700036A0u /* 4x4 matrix */
#define S_700036D0 0x700036D0u /* vector */
#define S_700038A0 0x700038A0u /* vector */
#define S_700038A4 0x700038A4u
#define S_700038A8 0x700038A8u
#define S_700038AC 0x700038ACu
#define S_700038B0 0x700038B0u /* four words */
#define S_700038B4 0x700038B4u
#define S_700038B8 0x700038B8u
#define S_700038BC 0x700038BCu
#define S_70003B84 0x70003B84u /* halfword frame counter */
#define S_70003B8D 0x70003B8Du

/* Overlay data (runtime addresses): script entry records, tables. */
#define A00_SCRIPT_8284E0 0x008284E0u
#define A00_SCRIPT_8286E0 0x008286E0u
#define A00_TABLE_8287E0 0x008287E0u
#define A00_TABLE_828870 0x00828870u
#define A00_SCRIPT_828D60 0x00828D60u
#define A00_SCRIPT_8294E0 0x008294E0u
#define A00_SCRIPT_8299A0 0x008299A0u
#define A00_SCRIPT_8299E0 0x008299E0u
#define A00_SCRIPT_829BE0 0x00829BE0u
#define A00_SCRIPT_82A0E0 0x0082A0E0u
#define A00_DATA_82A520 0x0082A520u
#define A00_SCRIPT_82A540 0x0082A540u
#define A00_DATA_82A700 0x0082A700u
#define A00_MATRIX_82A710 0x0082A710u
#define A00_SCRIPT_82A720 0x0082A720u
#define A00_SCRIPT_82B790 0x0082B790u
#define A00_SCRIPT_82B990 0x0082B990u
#define A00_RECORD_827A30 0x00827A30u
#define A00_POINT_82CCE0 0x0082CCE0u /* three floats 0x8263C0 rewrites every frame */
#define A00_POINTS_82D440 0x0082D440u /* four 16-byte vectors */

/* Float constants (bit patterns the original materialises). */
#define F_ZERO 0x00000000u
#define F_ONE 0x3F800000u
#define F_2_31 0x4F000000u     /* 2147483648.0 */
#define F_0_3 0x3E99999Au      /* 0.3 */
#define F_255 0x437F0000u
#define F_192 0x43400000u
#define F_3 0x40400000u
#define F_2 0x40000000u
#define F_0_09 0x3DB851ECu
#define F_0_01 0x3C23D70Au
#define F_1_5 0x3FC00000u
#define F_200 0x43480000u
#define F_4096 0x45800000u
#define F_MINUS_0_4 0xBECCCCCDu
#define F_155 0x431B0000u
#define F_MINUS_48 0xC2400000u
#define F_MINUS_1590 0xC4C6C000u
#define F_25 0x41C80000u
#define F_85 0x42AA0000u
#define F_5 0x40A00000u
#define F_MINUS_7_55 0xC0F1999Au
#define F_MINUS_5_69 0xC0B6147Bu
#define F_MINUS_1391_65 0xC4ADF4CDu
#define F_MINUS_16_1 0xC180CCCCu   /* -16.099998 */
#define F_12 0x41400000u
#define F_MINUS_40_4 0xC22199A0u   /* -40.400024 */
#define F_MINUS_11_017 0xC13045A2u
#define F_26_99 0x41D7EB85u
#define F_MINUS_41 0xC2240000u
#define F_MINUS_36_954 0xC213D0E5u

static inline float fb(uint32_t bits) { return em_ee_float(bits); }

static int a00_begin(A00Ovl *o, const EmArea00OvlHooks *h, EmArea00OvlFault *fault)
{
    if (!h || !fault) return -1;
    a00_open(o, h, fault);
    return a00_failed(o) ? -1 : 0;
}

static int a00_end(const A00Ovl *o) { return a00_failed(o) ? -1 : 0; }

/* ======================================================================
 * 0x823580 (overlay_AREA00_func_00823540) — the shaft door [52].
 * +0x04: 0 kickoff (001BBDA0; then +0x00 = 1, or, while D_0081075A != 0xFF,
 * +0x00 = 2, +0x05 = 5 and script 0x8284E0); 1 runs the step +0x05 (0..7,
 * other values run only the tail), then 001BC300; 2/3 free the node
 * (001AFC10); other values: nothing.
 * ====================================================================== */
static void a00_shaft_door(A00Ovl *o, uint32_t self)
{
    uint32_t state = a00_u8(o, self + 4);
    uint32_t block = self + 0x1F0;
    int32_t r;

    if (state == 3 || state == 2) {
        (void)a00_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        (void)a00_c_001BBDA0(o, self);
        if (a00_u8(o, D_0081075A) != 0xFF) {
            a00_w8(o, self + 0, 2);
            a00_w8(o, self + 5, 5);
            (void)a00_c_001BA1A0(o, block, A00_SCRIPT_8284E0);
        } else {
            a00_w8(o, self + 0, 1);
        }
        return;
    }
    if (state != 1) return;

    uint32_t step = a00_u8(o, self + 5);
    switch (step) {
    case 0:
        if (a00_u8(o, D_0081075E) == 0xFF) {
            a00_w8(o, self + 5, 1);
            r = 0;
            (void)a00_c_0019C6F0(o, 2, 0, &r);
            r = 0;
            (void)a00_c_0019C6F0(o, 0, 1, &r);
        } else if (a00_u8(o, D_0081075D) == 0xFF) {
            a00_w8(o, self + 5, 6);
            r = 0;
            (void)a00_c_0019C6F0(o, 2, 0, &r);
        } else {
            r = 0;
            (void)a00_c_001BBE40(o, self, block, 0, &r);
            if (r != 0) a00_w8(o, self + 5, 2);
        }
        break;
    case 1:
        r = 0;
        (void)a00_c_001BBE40(o, self, block, 0, &r);
        if (r != 0) a00_w8(o, self + 5, a00_u8(o, self + 5) + 1);
        break;
    case 2:
        r = 0;
        (void)a00_c_001BC0E0(o, self, block, &r);
        if (r != 0) a00_w8(o, self + 5, a00_u8(o, self + 5) + 1);
        break;
    case 3:
        (void)a00_c_001BC240(o, self, block);
        a00_w8(o, self + 5, a00_u8(o, self + 5) + 1);
        break;
    case 4:
        r = 0;
        (void)a00_c_001BC290(o, self, block, &r);
        if (r != 0) a00_w8(o, self + 5, 0);
        break;
    case 5: /* the arrival script ended: back to the plain door */
        r = 0;
        (void)a00_c_001BA1F0(o, self, &r);
        if (r != 0) {
            a00_w8(o, self + 0, 1);
            a00_w8(o, self + 0xB, 0);
            a00_w8(o, self + 5, 0);
            (void)a00_c_001C67E0(o, self, 0, fb(F_ZERO), fb(F_ZERO));
        }
        break;
    case 6: /* D_0081075D set: a Use starts the progression script */
        if (a00_u8(o, self + 0xB) & 4) {
            a00_w8(o, self + 5, step + 1); /* the step value read at the dispatch */
            (void)a00_c_001BA1A0(o, block, A00_SCRIPT_8286E0);
        }
        break;
    case 7: /* at its end: the area change */
        r = 0;
        (void)a00_c_001BA1F0(o, self, &r);
        if (r != 0) {
            (void)a00_c_001B0C60(o, 1, 0xFF, 0);
            a00_w8(o, self + 5, a00_u8(o, self + 5) + 1);
        }
        break;
    default:
        break;
    }
    (void)a00_c_001BC300(o, self);
}

int em_area00_ovl_00823580(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault)
{
    A00Ovl o;
    if (a00_begin(&o, h, fault)) return -1;
    a00_shaft_door(&o, self);
    return a00_end(&o);
}

/* ======================================================================
 * 0x823820 (func_overlay_AREA00_008237E0) — eight puffs in block +0x1F0
 * (+0x00 mode, +0x04 count, +0x08 eight timers, +0x28 eight {a, b, c, d}
 * floats). +0x04: 0 seeds them and falls into 1; 1 draws; 2/3 free.
 * ====================================================================== */
static uint32_t a00_rand_fraction(A00Ovl *o)
{
    int32_t r = 0;
    (void)a00_c_00122BB8(o, &r);
    return em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)r), F_2_31);
}

static void a00_puffs(A00Ovl *o, uint32_t self, uint32_t sp)
{
    uint32_t state = a00_u8(o, self + 4);
    uint32_t fx = self + 0x1F0;
    uint32_t packet = sp - 0xE0 + 0x80; /* the original's 0x60-byte local */
    int32_t r, i;

    if (state == 3 || state == 2) {
        (void)a00_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        for (i = 0; i < 8; i++) {
            uint32_t p = fx + 0x28 + 0x10 * (uint32_t)i;
            r = 0;
            (void)a00_c_00122BB8(o, &r);
            a00_w32(o, fx + 8 + 4 * (uint32_t)i, (uint32_t)(r % 120));
            a00_w32(o, p + 0, F_ZERO);
            a00_w32(o, p + 4, F_ZERO);
            a00_w32(o, p + 8, a00_rand_fraction(o));
            a00_w32(o, p + 12, a00_rand_fraction(o));
        }
        a00_w32(o, fx + 0, 0xFFFFFFFFu);
        a00_w32(o, fx + 4, 0);
        a00_w8(o, self + 0xC, 0);
        a00_w8(o, self + 9, 0);
        a00_w8(o, self + 4, 1);
        a00_w8(o, self + 5, 0);
    } else if (state != 1) {
        return;
    }

    (void)a00_c_001029C0(o, S_700036A0);
    (void)a00_c_00102948(o, S_700036D0, self + 0xB0);
    int32_t handle = 0;
    (void)a00_c_001CCF70(o, S_700036D0, &handle);
    for (i = 0; i < 8; i++) {
        uint32_t timer = fx + 8 + 4 * (uint32_t)i;
        uint32_t p = fx + 0x28 + 0x10 * (uint32_t)i;
        a00_w32(o, timer, a00_u32(o, timer) - 1);
        if (a00_s32(o, timer) >= 0) continue;

        uint32_t a = a00_u32(o, p + 0);
        if (em_ee_c_lt_bits(a, F_0_3)) {
            uint32_t t = em_ee_div_bits(em_ee_sub_bits(F_0_3, a), F_0_3);
            r = 0;
            (void)a00_c_001281C0(o, fb(em_ee_mul_bits(F_255, t)), &r);
            a00_w32(o, S_70003600, (uint32_t)r);
            t = em_ee_mul_bits(F_192, t);
            r = 0;
            (void)a00_c_001281C0(o, fb(t), &r);
            a00_w32(o, S_70003604, (uint32_t)r << 8);
            r = 0;
            (void)a00_c_001281C0(o, fb(t), &r);
            a00_w32(o, S_70003608, (uint32_t)r << 16);
            uint32_t a2 = a00_u32(o, p + 0);
            uint32_t c8 = a00_u32(o, S_70003608);
            uint32_t q = em_ee_div_bits(em_ee_mul_bits(F_3, a2), F_0_3);
            uint32_t c4 = a00_u32(o, S_70003604);
            uint32_t c0 = a00_u32(o, S_70003600);
            uint32_t w = em_ee_add_bits(F_ONE, q);
            r = 0;
            (void)a00_c_001CD520(o, 0, 2, S_700036D0, UINT64_C(0x20045B2599421E98),
                                 c0 | ((c8 | 0x80000000u) | c4), fb(w), fb(w), fb(F_2), &r);
        }
        uint32_t fa = a00_u32(o, p + 0);
        uint32_t fc = a00_u32(o, p + 8);
        (void)a00_c_001CFA60(o, packet, S_700036A0, fb(fa), fb(fc));
        (void)a00_c_001CFBE0(o, handle, 2, A00_TABLE_8287E0, packet, 0);
        uint32_t na = em_ee_add_bits(a00_u32(o, p + 0), F_0_09);
        a00_w32(o, p + 0, na);
        if (!em_ee_c_le_bits(na, F_2)) a00_w32(o, p + 0, F_2);

        uint32_t fbb = a00_u32(o, p + 4);
        uint32_t fd = a00_u32(o, p + 12);
        (void)a00_c_001CFA60(o, packet, S_700036A0, fb(fbb), fb(fd));
        (void)a00_c_001CFBE0(o, handle, 2, A00_TABLE_828870, packet, 0);
        uint32_t nb = em_ee_add_bits(a00_u32(o, p + 4), F_0_01);
        a00_w32(o, p + 4, nb);
        if (!em_ee_c_le_bits(nb, F_1_5)) {
            r = 0;
            (void)a00_c_00122BB8(o, &r);
            a00_w32(o, timer, (uint32_t)(r % 60 + 40));
            a00_w32(o, p + 0, F_ZERO);
            a00_w32(o, p + 4, F_ZERO);
            a00_w32(o, p + 8, a00_rand_fraction(o));
            a00_w32(o, p + 12, a00_rand_fraction(o));
        }
    }
    (void)a00_c_001FC3C0(o, self, fx, 0x41C, fb(F_200), fb(F_4096));
}

int em_area00_ovl_00823820(const EmArea00OvlHooks *h, uint32_t self, uint32_t sp, EmArea00OvlFault *fault)
{
    A00Ovl o;
    if (a00_begin(&o, h, fault)) return -1;
    a00_puffs(&o, self, sp);
    return a00_end(&o);
}

/* ======================================================================
 * 0x824EA0 (func_overlay_AREA00_00824E60) — the arrival owner.
 * +0x04 0: +0x05 0 waits (to state 3 when 001BA1C0(self, 2) or D_00810701
 * is set) for 001B10B0(self, 0xF, 0x11) == 0, then seeds block +0x1F0 and
 * the +0xD0 matrix; +0x05 1 starts script 0x828D60 once 00129780(self,
 * block, 3) is non-zero. 1: the scripted walk (counter 0x70003B84). 2:
 * 001C4760(3, 1), then 3. 3: free.
 * ====================================================================== */
static void a00_arrival(A00Ovl *o, uint32_t self)
{
    uint32_t state = a00_u8(o, self + 4);
    uint32_t sub = self + 0x1F0;
    int32_t r;

    if (state == 3) {
        (void)a00_c_001AFC10(o, self);
        return;
    }
    if (state == 2) {
        r = 0;
        (void)a00_c_001C4760(o, 3, 1, &r);
        a00_w8(o, self + 4, a00_u8(o, self + 4) + 1);
        return;
    }
    if (state == 0) {
        uint32_t step = a00_u8(o, self + 5);
        if (step == 1) {
            r = 0;
            (void)a00_c_00129780(o, self, sub, 3, &r);
            if (r != 0) (void)a00_c_001BA1A0(o, sub, A00_SCRIPT_828D60);
            return;
        }
        if (step != 0) return;
        r = 0;
        (void)a00_c_001BA1C0(o, self, 2, &r);
        if (r != 0) {
            a00_w8(o, self + 4, 3);
            return;
        }
        if (a00_u8(o, D_00810701) != 0) {
            a00_w8(o, self + 4, 3);
            return;
        }
        r = 0;
        (void)a00_c_001B10B0(o, self, 0xF, 0x11, &r);
        if (r != 0) return;
        (void)a00_c_001C63E0(o, self, 1);
        a00_w32(o, sub + 0xEC, F_ONE);
        a00_w32(o, sub + 0xD8, F_ZERO);
        a00_w32(o, sub + 0xE4, F_ZERO);
        a00_w32(o, sub + 0xDC, F_ZERO);
        a00_w32(o, sub + 0x60, F_ZERO);
        a00_w32(o, sub + 0x64, F_MINUS_0_4);
        a00_w32(o, sub + 0x68, F_ZERO);
        a00_w32(o, sub + 0x6C, F_ONE);
        a00_w32(o, sub + 0x70, F_ZERO);
        a00_w32(o, sub + 0x74, F_ZERO);
        a00_w32(o, sub + 0x78, F_ONE);
        a00_w32(o, sub + 0x7C, F_ONE);
        a00_w32(o, sub + 0x80, F_ZERO);
        a00_w32(o, sub + 0x84, F_ONE);
        a00_w32(o, sub + 0x88, F_ZERO);
        a00_w32(o, sub + 0x8C, F_ONE);
        (void)a00_c_001029C0(o, self + 0xD0);
        a00_w8(o, self + 5, a00_u8(o, self + 5) + 1);
        return;
    }
    if (state != 1) return;

    (void)a00_c_001029C0(o, S_70003000);
    uint32_t step = a00_u8(o, self + 5);
    if (step == 2) {
        r = 0;
        (void)a00_c_001BA1F0(o, self, &r);
        if (r != 0) {
            a00_w8(o, self + 4, a00_u8(o, self + 4) + 1);
            a00_w8(o, self + 5, 0);
        }
        if (a00_u8(o, S_70003B8D) != 0 && a00_u16(o, S_70003B84) == 0x4B0)
            (void)a00_c_001B1E20(o, 6, 0);
        return;
    }
    if (step == 0) {
        a00_w8(o, self + 5, step + 1);
        a00_w16(o, S_70003B84, 0);
    } else if (step != 1) {
        return;
    }
    r = 0;
    (void)a00_c_001C2770(o, self, sub, 0, &r);
    if (r == 0) (void)a00_c_001C3D60(o, self, sub);
    uint32_t dt = a00_u32(o, sub + 0xEC);
    r = 0;
    (void)a00_c_001C64F0(o, self, fb(dt), &r);
    (void)a00_c_00102958(o, S_70003400, S_70003000);
    (void)a00_c_001C69A0(o, self);
    a00_callback(o, self);
    if ((int32_t)a00_u16(o, S_70003B84) >= 0x14A) a00_w8(o, self + 5, a00_u8(o, self + 5) + 1);
    r = 0;
    (void)a00_c_001BA1F0(o, self, &r);
    if (r != 0) {
        a00_w8(o, self + 4, a00_u8(o, self + 4) + 1);
        a00_w8(o, self + 5, 0);
    }
}

int em_area00_ovl_00824EA0(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault)
{
    A00Ovl o;
    if (a00_begin(&o, h, fault)) return -1;
    a00_arrival(&o, self);
    return a00_end(&o);
}

/* ======================================================================
 * 0x825170 (func_overlay_AREA00_00825130) — door [51].
 * +0x04: 0 kickoff (001BBDA0, script 0x8294E0 unless D_0081075B == 0xFF,
 * +0x00 = 1); 1 runs the step +0x05 (0..6, other values only the tail),
 * then 001BC300; 2/3 free; other values: nothing.
 * ====================================================================== */
static void a00_door51(A00Ovl *o, uint32_t self)
{
    uint32_t state = a00_u8(o, self + 4);
    uint32_t block = self + 0x1F0;
    int32_t r;

    if (state == 3 || state == 2) {
        (void)a00_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        (void)a00_c_001BBDA0(o, self);
        if (a00_u8(o, D_0081075B) != 0xFF) (void)a00_c_001BA1A0(o, block, A00_SCRIPT_8294E0);
        a00_w8(o, self + 0, 1);
        return;
    }
    if (state != 1) return;

    switch (a00_u8(o, self + 5)) {
    case 0:
        if (a00_u8(o, D_00810702) == 5) { /* the player came in at entry 5 */
            a00_w8(o, D_0081075B, 0xFF);
            r = 0;
            (void)a00_c_001BBE40(o, self, block, 0, &r);
            if (r != 0) {
                a00_w8(o, D_008107DB, 0xFF);
                a00_w8(o, self + 5, 3);
            }
        } else if (a00_u8(o, D_0081075B) != 0xFF) {
            if (a00_u8(o, self + 0xB) & 4) a00_w8(o, self + 5, 6);
        } else {
            r = 0;
            (void)a00_c_001BBE40(o, self, block, 0, &r);
            if (r != 0) a00_w8(o, self + 5, 3);
        }
        break;
    case 1:
        r = 0;
        (void)a00_c_001BC0E0(o, self, block, &r);
        if (r != 0) {
            (void)a00_c_001BA1A0(o, block, A00_SCRIPT_8299A0);
            a00_w8(o, self + 5, a00_u8(o, self + 5) + 1);
        }
        break;
    case 2:
        r = 0;
        (void)a00_c_001BC0E0(o, self, block, &r);
        if (r != 0) {
            a00_w8(o, self + 0xB, 0);
            a00_w8(o, self + 5, 0);
        }
        break;
    case 3:
        r = 0;
        (void)a00_c_001BC0E0(o, self, block, &r);
        if (r != 0) a00_w8(o, self + 5, a00_u8(o, self + 5) + 1);
        break;
    case 4:
        (void)a00_c_001BC240(o, self, block);
        a00_w8(o, self + 5, a00_u8(o, self + 5) + 1);
        break;
    case 5:
        r = 0;
        (void)a00_c_001BC290(o, self, block, &r);
        if (r != 0) a00_w8(o, self + 5, 0);
        break;
    case 6: /* the locked script ended */
        r = 0;
        (void)a00_c_001BA1F0(o, self, &r);
        if (r != 0) {
            if (a00_u8(o, D_0081075B) == 0) {
                a00_w8(o, D_0081075B, 1);
                r = 0;
                (void)a00_c_001C4760(o, 4, 1, &r);
            }
            a00_w8(o, self + 5, 0);
            a00_w8(o, self + 0xB, 0);
            (void)a00_c_001BA1A0(o, block, A00_SCRIPT_8294E0);
        }
        break;
    default:
        break;
    }
    (void)a00_c_001BC300(o, self);
}

int em_area00_ovl_00825170(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault)
{
    A00Ovl o;
    if (a00_begin(&o, h, fault)) return -1;
    a00_door51(&o, self);
    return a00_end(&o);
}

/* ======================================================================
 * 0x8253E0 (func_overlay_AREA00_008253A0) — script op09 callback: steps
 * byte +0x05 of the block in a1 (reset when +0x04 is 0), plays sound
 * 0x19A every 16 steps, returns 1 once +0x05 exceeds 48. Other +0x04
 * values return 0 at once.
 * ====================================================================== */
static int32_t a00_op09_8253e0(A00Ovl *o, uint32_t s)
{
    uint32_t phase = a00_u8(o, s + 4);
    if (phase == 0) {
        a00_w8(o, s + 4, phase + 1);
        a00_w8(o, s + 5, 0);
    } else if (phase != 1) {
        return 0;
    }
    a00_w8(o, s + 5, a00_u8(o, s + 5) + 1);
    if ((a00_u8(o, s + 5) & 0xF) == 0) (void)a00_c_001FB9F0(o, 0x19A, 0x1000, 0x1000, 0x1000);
    return a00_u8(o, s + 5) > 0x30 ? 1 : 0;
}

int em_area00_ovl_008253E0(const EmArea00OvlHooks *h, uint32_t a0, uint32_t block, uint32_t a2,
                           int32_t *result, EmArea00OvlFault *fault)
{
    A00Ovl o;
    (void)a0;
    (void)a2;
    if (!result || a00_begin(&o, h, fault)) return -1;
    int32_t value = a00_op09_8253e0(&o, block);
    if (a00_failed(&o)) return -1;
    *result = value;
    return 0;
}

/* ======================================================================
 * 0x825480 (func_overlay_AREA00_00825440) — the cage-room terminal [40].
 * +0x04: 0 set-up once 001B0FD0 returns 0; 1: a Use (+0x0B bit 2) starts
 * script 0x8299E0, and at its end D_008107DC |= 1; then 001B17A0 and the
 * +0x4C callback; 2/3 free.
 * ====================================================================== */
static void a00_terminal(A00Ovl *o, uint32_t self)
{
    uint32_t state = a00_u8(o, self + 4);
    uint32_t block = self + 0x1F0;
    int32_t r;

    if (state == 3 || state == 2) {
        (void)a00_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        r = 0;
        (void)a00_c_001B0FD0(o, self, &r);
        if (r != 0) return;
        a00_w8(o, self + 0, 1);
        a00_w8(o, self + 8, 1);
        a00_w32(o, self + 0x30, A00_DATA_82A520);
        (void)a00_c_001C6380(o, self);
        a00_w32(o, S_700038A0, F_ZERO);
        a00_w32(o, S_700038A4, F_ONE);
        a00_w32(o, S_700038A8, F_ZERO);
        a00_w32(o, S_700038AC, F_ONE);
        (void)a00_c_001C5570(o, self, S_700038A0, 9, 0);
        return;
    }
    if (state != 1) return;

    uint32_t step = a00_u8(o, self + 5);
    if (step == 0) {
        if (a00_u8(o, self + 0xB) & 4) {
            a00_w8(o, self + 5, step + 1);
            (void)a00_c_001BA1A0(o, block, A00_SCRIPT_8299E0);
        }
    } else if (step == 1) {
        r = 0;
        (void)a00_c_001BA1F0(o, self, &r);
        if (r != 0) {
            a00_w8(o, D_008107DC, a00_u8(o, D_008107DC) | 1);
            a00_w8(o, self + 0xB, 0);
            a00_w8(o, self + 5, 0);
        }
    }
    r = 0;
    (void)a00_c_001B17A0(o, self, &r);
    a00_callback(o, self);
}

int em_area00_ovl_00825480(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault)
{
    A00Ovl o;
    if (a00_begin(&o, h, fault)) return -1;
    a00_terminal(&o, self);
    return a00_end(&o);
}

/* ======================================================================
 * 0x825600 (func_overlay_AREA00_008255C0) — the ferry group, by kind byte
 * +0x0D: 2 the ferry, 0x15 a rider that keeps its offset from the node at
 * +0x18, 7 the node that writes 85 + (y - 25) of the node two links up,
 * other kinds follow the node at +0x18. +0x04: 0 set-up once 001B0FD0
 * returns 0; 1 runs; 2/3 free.
 * ====================================================================== */
static void a00_ferry(A00Ovl *o, uint32_t self, uint32_t sp)
{
    uint32_t state = a00_u8(o, self + 4);
    uint32_t block = self + 0x1F0;
    uint32_t vector = sp - 0x50 + 0x40; /* the original's 16-byte local */
    int32_t r;

    if (state == 3 || state == 2) {
        (void)a00_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        r = 0;
        (void)a00_c_001B0FD0(o, self, &r);
        if (r != 0) return;
        uint32_t kind = a00_u8(o, self + 0xD);
        if (kind == 2) {
            (void)a00_c_00102948(o, self + 0xA0, self + 0xB0);
            if (a00_u8(o, D_008107DC) & 2) { /* the ferry was left at the east end */
                a00_w32(o, self + 0xB0, F_155);
                a00_w32(o, self + 0xB4, F_MINUS_48);
                a00_w32(o, self + 0xB8, F_MINUS_1590);
                a00_w32(o, self + 0xBC, F_ONE);
            }
        } else if (kind == 0x15) {
            uint32_t other = a00_u32(o, self + 0x18);
            (void)a00_c_001028D0(o, self + 0xA0, self + 0xB0, other + 0xA0);
        }
        (void)a00_c_001C6380(o, self);
        (void)a00_c_001A2370(o, self, self + 0xD0);
        return;
    }
    if (state != 1) return;

    uint32_t kind = a00_u8(o, self + 0xD);
    if (kind == 2) {
        uint32_t step = a00_u8(o, self + 5);
        if (step == 1) {
            r = 0;
            (void)a00_c_001BA1F0(o, self, &r);
            if (r != 0) a00_w8(o, self + 5, 0);
            (void)a00_c_001A2370(o, self, self + 0xD0);
        } else if (step == 0) {
            uint32_t flags = a00_u8(o, D_008107DC);
            if (flags & 1) {
                a00_w8(o, self + 5, step + 1);
                (void)a00_c_001BA1A0(o, block, (flags & 2) ? A00_SCRIPT_82A0E0 : A00_SCRIPT_829BE0);
            }
        }
        (void)a00_c_001C6380(o, self);
        a00_w8(o, self + 1, 1);
        (void)a00_c_001B1B70(o, self);
        a00_callback(o, self);
        for (uint32_t i = 0; i < 4; i++) {
            (void)a00_c_001026A0(o, vector, self + 0xD0, A00_POINTS_82D440 + 0x10 * i);
            (void)a00_c_001F5940(o, 3, vector, 0);
        }
    } else if (kind == 7) {
        uint32_t link = a00_u32(o, self + 0x18);
        uint32_t other = a00_u32(o, link + 0x18);
        uint32_t d = em_ee_sub_bits(a00_u32(o, other + 0xB4), F_25);
        if (!em_ee_c_eq_bits(F_85, d)) {
            uint32_t bones = a00_u32(o, D_00275B40);
            uint32_t sum = em_ee_add_bits(F_85, d);
            uint32_t target = a00_u32(o, bones + 4);
            a00_w32(o, target + 0x80, sum);
        }
        (void)a00_c_001C6380(o, self);
        a00_w8(o, self + 1, 1);
        (void)a00_c_001B1B70(o, self);
        (void)a00_c_001A2370(o, self, self + 0xD0);
        a00_callback(o, self);
    } else {
        uint32_t other = a00_u32(o, self + 0x18);
        (void)a00_c_001028B8(o, self + 0xB0, other + 0xB0, self + 0xA0);
        a00_w32(o, self + 0xBC, F_ONE);
        (void)a00_c_001C6380(o, self);
        if (a00_u8(o, other + 1) != 0) {
            (void)a00_c_001A2370(o, self, self + 0xD0);
            a00_w8(o, self + 1, 1);
            (void)a00_c_001B1B70(o, self);
        }
        a00_callback(o, self);
    }
}

int em_area00_ovl_00825600(const EmArea00OvlHooks *h, uint32_t self, uint32_t sp, EmArea00OvlFault *fault)
{
    A00Ovl o;
    if (a00_begin(&o, h, fault)) return -1;
    a00_ferry(&o, self, sp);
    return a00_end(&o);
}

/* ======================================================================
 * 0x825920 (func_overlay_AREA00_008258E0) — [43] (header +0x02 bit 7) and
 * two group nodes. +0x04: 0 waits on 001B0F60(self, 6 or 7); 1 runs the
 * step +0x05 and, while D_00810702 is 5 or 6, animates; 2: nothing; 3 frees.
 * ====================================================================== */
static void a00_store_effect_point(A00Ovl *o)
{
    a00_w32(o, S_700038A0, F_MINUS_7_55);
    a00_w32(o, S_700038A4, F_MINUS_5_69);
    a00_w32(o, S_700038A8, F_MINUS_1391_65);
    a00_w32(o, S_700038AC, F_ONE);
    (void)a00_c_001EFD20(o, 0, S_700038A0);
}

static void a00_switch43(A00Ovl *o, uint32_t self)
{
    uint32_t state = a00_u8(o, self + 4);
    uint32_t talk = self + 0x1F0;
    int32_t r;

    if (state == 3) {
        (void)a00_c_001AFC10(o, self);
        return;
    }
    if (state == 2) return;
    if (state == 0) {
        if (a00_u8(o, D_0081075D) == 0xFF) {
            r = 0;
            (void)a00_c_001B0F60(o, self, 6, &r);
            if (r != 0) return;
            a00_w8(o, self + 5, 2);
            if ((a00_u8(o, self + 2) & 0x80) && a00_u8(o, D_0081075E) != 0xFF) a00_store_effect_point(o);
        } else if (a00_u8(o, self + 2) & 0x80) {
            r = 0;
            (void)a00_c_001B0F60(o, self, 7, &r);
            if (r != 0) return;
            (void)a00_c_001BA1A0(o, talk, A00_SCRIPT_82A540);
            a00_w32(o, self + 0x30, A00_DATA_82A700);
            a00_w8(o, self + 0, 1);
        } else {
            r = 0;
            (void)a00_c_001B0F60(o, self, 6, &r);
            if (r != 0) return;
            a00_w8(o, self + 5, 2);
        }
        return;
    }
    if (state != 1) return;

    uint32_t step = a00_u8(o, self + 5);
    if (step == 0) {
        a00_w8(o, self + 0, a00_u8(o, D_008104E6) != 0 ? 2 : 1);
        if (a00_u8(o, self + 0xB) & 4) a00_w8(o, self + 5, 1);
    } else if (step == 1) {
        r = 0;
        (void)a00_c_001BA1F0(o, self, &r);
        if (r != 0) {
            r = 0;
            (void)a00_c_001C64F0(o, self, fb(F_ONE), &r);
            a00_w8(o, self + 5, 2);
            uint32_t y = a00_u32(o, D_00810354);
            uint32_t x = a00_u32(o, self + 0xB0);
            a00_w32(o, S_700038A0, x);
            a00_w32(o, S_700038A4, y);
            uint32_t z = em_ee_sub_bits(a00_u32(o, self + 0xB8), F_5);
            a00_w32(o, S_700038A8, z);
            a00_w32(o, S_700038AC, F_ONE);
            (void)a00_c_00182F90(o, D_008102B0, S_700038A0);
            a00_store_effect_point(o);
        }
    }
    uint32_t entry = a00_u8(o, D_00810702);
    if (entry != 5 && entry != 6) return;
    (void)a00_c_001C68C0(o, self);
    r = 0;
    (void)a00_c_001B17A0(o, self, &r);
    a00_callback(o, self);
    if (!(a00_u8(o, self + 2) & 0x80)) return;
    (void)a00_c_001026A0(o, S_700038A0, self + 0xD0, A00_MATRIX_82A710);
    if (a00_u8(o, D_0081075D) == 0xFF) return;
    a00_w32(o, S_700038B0, 0);
    a00_w32(o, S_700038B4, 0x80);
    a00_w32(o, S_700038B8, 0);
    a00_w32(o, S_700038BC, 0x80);
    (void)a00_c_001F4E20(o, S_700038A0, S_700038B0, fb(F_2));
}

int em_area00_ovl_00825920(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault)
{
    A00Ovl o;
    if (a00_begin(&o, h, fault)) return -1;
    a00_switch43(&o, self);
    return a00_end(&o);
}

/* ======================================================================
 * 0x825C80 / 0x8261E0 / 0x8262D0 (func_overlay_AREA00_00825C40 / 008261A0 /
 * 00826290) — one code, three data sets: +0x04 0 sets +0x30 to a boot word
 * address, +0x08 = 3, +0x00 = 1, +0x04 = 1; 1: a Use (+0x0B bit 2) starts
 * the script, its end clears +0x0B and +0x05, then 001B17A0; 2: nothing;
 * 3 frees.
 * ====================================================================== */
static void a00_examine(A00Ovl *o, uint32_t self, uint32_t word, uint32_t script)
{
    uint32_t state = a00_u8(o, self + 4);
    int32_t r;

    if (state == 3) {
        (void)a00_c_001AFC10(o, self);
        return;
    }
    if (state == 2) return;
    if (state == 0) {
        a00_w32(o, self + 0x30, word);
        a00_w8(o, self + 8, 3);
        a00_w8(o, self + 0, 1);
        a00_w8(o, self + 4, 1);
        return;
    }
    if (state != 1) return;
    uint32_t step = a00_u8(o, self + 5);
    if (step == 0) {
        if (a00_u8(o, self + 0xB) & 4) {
            (void)a00_c_001BA1A0(o, self + 0x1F0, script);
            a00_w8(o, self + 5, 1);
        }
    } else if (step == 1) {
        r = 0;
        (void)a00_c_001BA1F0(o, self, &r);
        if (r != 0) {
            a00_w8(o, self + 0xB, 0);
            a00_w8(o, self + 5, 0);
        }
    }
    r = 0;
    (void)a00_c_001B17A0(o, self, &r);
}

int em_area00_ovl_00825C80(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault)
{
    A00Ovl o;
    if (a00_begin(&o, h, fault)) return -1;
    a00_examine(&o, self, D_002758A8, A00_SCRIPT_82A720);
    return a00_end(&o);
}

int em_area00_ovl_008261E0(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault)
{
    A00Ovl o;
    if (a00_begin(&o, h, fault)) return -1;
    a00_examine(&o, self, D_002758B0, A00_SCRIPT_82B790);
    return a00_end(&o);
}

int em_area00_ovl_008262D0(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault)
{
    A00Ovl o;
    if (a00_begin(&o, h, fault)) return -1;
    a00_examine(&o, self, D_002758B8, A00_SCRIPT_82B990);
    return a00_end(&o);
}

/* ======================================================================
 * 0x8263C0 (func_overlay_AREA00_00826380) — the ferry cab [47]. +0x04: 0
 * set-up once 001B0FD0 returns 0 (spawns 001B6660(0x827A30) offset by its
 * own position); 1: a Use starts boot script 0x246F20, then every frame the
 * point 0x82CCE0 follows the cab, the cab follows the node at +0x1C (y + 1),
 * and two points are emitted (001F5940(9, ...)); 2/3 free.
 * ====================================================================== */
static void a00_cab(A00Ovl *o, uint32_t self)
{
    uint32_t state = a00_u8(o, self + 4);
    uint32_t block = self + 0x1F0;
    int32_t r;

    if (state == 3 || state == 2) {
        (void)a00_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        r = 0;
        (void)a00_c_001B0FD0(o, self, &r);
        if (r != 0) return;
        uint32_t e = 0;
        (void)a00_c_001B6660(o, A00_RECORD_827A30, &e);
        if (e != 0) {
            (void)a00_c_001028D0(o, e + 0xA0, e + 0xB0, self + 0xB0);
            a00_w32(o, e + 0x20, a00_u32(o, self + 0x14));
        }
        a00_w8(o, self + 0, 1);
        a00_w8(o, self + 8, 1);
        a00_w32(o, self + 0x30, A00_POINT_82CCE0);
        (void)a00_c_001C6380(o, self);
        return;
    }
    if (state != 1) return;

    uint32_t step = a00_u8(o, self + 5);
    if (step == 0) {
        if (a00_u8(o, self + 0xB) & 4) {
            a00_w32(o, D_00246FB4, 0x8000000Au);
            (void)a00_c_001BA1A0(o, block, D_00246F20);
            a00_w8(o, self + 5, a00_u8(o, self + 5) + 1);
        }
    } else if (step == 1) {
        r = 0;
        (void)a00_c_001BA1F0(o, self, &r);
        if (r != 0) {
            a00_w8(o, self + 5, 0);
            a00_w8(o, self + 0xB, 0);
        }
    }
    a00_w32(o, A00_POINT_82CCE0 + 0, em_ee_add_bits(F_MINUS_16_1, a00_u32(o, self + 0xB0)));
    a00_w32(o, A00_POINT_82CCE0 + 4, em_ee_add_bits(F_12, a00_u32(o, self + 0xB4)));
    a00_w32(o, A00_POINT_82CCE0 + 8, em_ee_add_bits(F_MINUS_40_4, a00_u32(o, self + 0xB8)));

    uint32_t other = a00_u32(o, self + 0x1C);
    uint32_t x = a00_u32(o, self + 0xB0);
    uint32_t ox = a00_u32(o, other + 0xB0);
    int same = 0;
    if (em_ee_c_eq_bits(x, ox)) {
        uint32_t oy = em_ee_add_bits(F_ONE, a00_u32(o, other + 0xB4));
        same = em_ee_c_eq_bits(a00_u32(o, self + 0xB4), oy);
    }
    if (!same) {
        a00_w32(o, self + 0xB0, ox);
        a00_w32(o, self + 0xB4, em_ee_add_bits(F_ONE, a00_u32(o, other + 0xB4)));
        (void)a00_c_001C6380(o, self);
        (void)a00_c_001A2370(o, self, self + 0xD0);
    }
    (void)a00_c_001B1B70(o, self);
    a00_w8(o, self + 1, 1);
    a00_callback(o, self);

    a00_w32(o, S_700038AC, F_ONE);
    a00_w32(o, S_700038A0, em_ee_add_bits(F_MINUS_11_017, a00_u32(o, self + 0xB0)));
    a00_w32(o, S_700038A4, em_ee_add_bits(F_26_99, a00_u32(o, self + 0xB4)));
    a00_w32(o, S_700038A8, em_ee_add_bits(F_MINUS_41, a00_u32(o, self + 0xB8)));
    (void)a00_c_001F5940(o, 9, S_700038A0, 0);
    a00_w32(o, S_700038A8, em_ee_add_bits(F_MINUS_36_954, a00_u32(o, self + 0xB8)));
    (void)a00_c_001F5940(o, 9, S_700038A0, 0);
}

int em_area00_ovl_008263C0(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault)
{
    A00Ovl o;
    if (a00_begin(&o, h, fault)) return -1;
    a00_cab(&o, self);
    return a00_end(&o);
}

/* ======================================================================
 * 0x8266A0 (func_overlay_AREA00_00826660) — [60]. +0x04: 0 calls 001B0FD0
 * and 001C6380; 1: once D_0081075E is 0xFF and +0x0D is 0x19, it switches
 * +0x0D to 0x1A, calls 001AF800, 001CB5B0(+0x09), 001B0FD0, 001C6380 and
 * sets +0x04 = 1; then 001B17A0 and the +0x4C callback; any other value
 * frees the node.
 * ====================================================================== */
static void a00_beam(A00Ovl *o, uint32_t self)
{
    uint32_t state = a00_u8(o, self + 4);
    int32_t r;

    if (state == 0) {
        r = 0;
        (void)a00_c_001B0FD0(o, self, &r);
        (void)a00_c_001C6380(o, self);
        return;
    }
    if (state != 1) {
        (void)a00_c_001AFC10(o, self);
        return;
    }
    if (a00_u8(o, D_0081075E) == 0xFF && a00_u8(o, self + 0xD) == 0x19) {
        a00_w8(o, self + 0xD, 0x1A);
        (void)a00_c_001AF800(o, self);
        (void)a00_c_001CB5B0(o, (int32_t)a00_u8(o, self + 9));
        r = 0;
        (void)a00_c_001B0FD0(o, self, &r);
        (void)a00_c_001C6380(o, self);
        a00_w8(o, self + 4, 1);
    }
    r = 0;
    (void)a00_c_001B17A0(o, self, &r);
    a00_callback(o, self);
}

int em_area00_ovl_008266A0(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault)
{
    A00Ovl o;
    if (a00_begin(&o, h, fault)) return -1;
    a00_beam(&o, self);
    return a00_end(&o);
}
