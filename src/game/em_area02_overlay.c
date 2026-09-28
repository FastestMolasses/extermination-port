/* AREA02 overlay owners (see em_area02_overlay.h, docs/AREA02_OVERLAY_PORT.md).
 *
 * Each function below is a translation of the original AREA02 overlay code
 * at the named runtime address. All sixteen follow the decomp's
 * byte-identical C (src/overlays/AREA02/<name>.c, link name = runtime -
 * 0x40). Calls, their arguments and the memory accesses between each two
 * calls follow the original: the test compares memory at every call entry
 * and after the last store, and the memory accesses between calls one for
 * one, in order, by address and size (docs/AREA02_OVERLAY_PORT.md section
 * 3). Where the original loads the operands of one expression in a set
 * order, the translation loads them in separate statements in that order,
 * because C leaves the order of evaluation of arguments and operands open.
 * A 16-byte copy of the original is two 8-byte accesses, low half first
 * (the order the test's EE core performs them). Float arithmetic is the EE
 * model (em_ee_float.h) on bit patterns; the operand order of each
 * add/sub/mul/div/madd/compare is the original's.
 */
#include "em_area02_overlay_internal.h"

/* Original globals the functions name. */
#define D_00275BCC 0x00275BCCu /* halfword: the bone-slot cap */
#define D_00275C18 0x00275C18u
#define D_00275C1C 0x00275C1Cu
#define D_00275C24 0x00275C24u
#define D_00275C28 0x00275C28u
#define D_00275C2C 0x00275C2Cu
#define D_002758E0 0x002758E0u /* examine descriptor (0x824FA0 stores its address) */
#define D_00282154 0x00282154u /* signed byte */
#define D_0028A59C 0x0028A59Cu /* model table word */
#define D_0028A6E8 0x0028A6E8u
#define D_008102BA 0x008102BAu
#define D_00810350 0x00810350u /* player x (float) */
#define D_00810358 0x00810358u /* player z */
#define D_00810360 0x00810360u
#define D_00810368 0x00810368u
#define D_00810374 0x00810374u /* player yaw */
#define D_008104C4 0x008104C4u /* the object the player stands on */
#define D_0081050C 0x0081050Cu
#define D_00810761 0x00810761u /* AREA02 flag 9 */
#define D_008107E1 0x008107E1u /* AREA02 counter 9 (switch, car, gate bits) */
#define D_0081083F 0x0081083Fu

/* Scratchpad. */
#define S_700031F0 0x700031F0u
#define S_700038A0 0x700038A0u /* vector */
#define S_700038A4 0x700038A4u
#define S_700038A8 0x700038A8u
#define S_700038AC 0x700038ACu
#define S_700038B0 0x700038B0u /* vector (floats or words) */
#define S_700038B4 0x700038B4u
#define S_700038B8 0x700038B8u
#define S_700038BC 0x700038BCu
#define S_70003B68 0x70003B68u /* frame counter word */

/* Overlay data (runtime addresses). */
#define A02_SCRIPT_826780 0x00826780u
#define A02_SCRIPT_826980 0x00826980u
#define A02_SCRIPT_8269C0 0x008269C0u
#define A02_SCRIPT_826A00 0x00826A00u
#define A02_SCRIPT_826A40 0x00826A40u
#define A02_SCRIPT_826A80 0x00826A80u
#define A02_SCRIPT_826AC0 0x00826AC0u
#define A02_SCRIPT_826B40 0x00826B40u
#define A02_SCRIPT_826F40 0x00826F40u
#define A02_DATA_827340 0x00827340u
#define A02_TRIGGERS_827350 0x00827350u /* 18 records of 0x20 bytes */
#define A02_POINT_827590 0x00827590u
#define A02_POINT_8275A0 0x008275A0u
#define A02_QUAD_8275B0 0x008275B0u
#define A02_QUAD_8275F0 0x008275F0u
#define A02_POINTS_827630 0x00827630u /* four 16-byte points */
#define A02_SCRIPT_827670 0x00827670u
#define A02_SCRIPT_828C50 0x00828C50u
#define A02_SCRIPT_828E10 0x00828E10u
#define A02_DATA_829190 0x00829190u
#define A02_DATA_829280 0x00829280u
#define A02_GROUP_825C00 0x00825C00u
#define A02_DATA_969E80 0x00969E80u

/* Float constants (bit patterns the original materialises). */
#define F_ZERO 0x00000000u
#define F_ONE 0x3F800000u
#define F_3_5 0x40600000u
#define F_4 0x40800000u
#define F_5 0x40A00000u
#define F_10 0x41200000u
#define F_14 0x41600000u
#define F_15 0x41700000u
#define F_20 0x41A00000u
#define F_MINUS_20 0xC1A00000u
#define F_MINUS_25 0xC1C80000u
#define F_40 0x42200000u
#define F_MINUS_40 0xC2200000u
#define F_58 0x42680000u
#define F_MINUS_66 0xC2840000u
#define F_80 0x42A00000u
#define F_110 0x42DC0000u
#define F_140 0x430C0000u
#define F_170 0x432A0000u
#define F_175 0x432F0000u
#define F_180 0x43340000u
#define F_200 0x43480000u
#define F_MINUS_270 0xC3870000u
#define F_MINUS_280 0xC38C0000u
#define F_300 0x43960000u
#define F_900 0x44610000u
#define F_4900 0x45992000u
#define F_PI 0x40490FDBu
#define F_MINUS_PI 0xC0490FDBu
#define F_MINUS_HALF_PI 0xBFC90FDBu
#define F_TURN_RATE 0x3B751AA5u   /* 0.00373999146, 0x825100 / 0x825520 */
#define F_GOAL_1 0x3EA0D97Cu      /* 0.31415927 */
#define F_GOAL_2 0x3EDF66F3u      /* 0.43633232 */
#define F_GOAL_3 0x3EB2B8C3u      /* 0.34906587 */
#define F_GOAL_4 0x3E567750u      /* 0.20943952 */
#define F_GOAL_5 0xBEA387C6u      /* -0.31939524 */
#define F_GOAL_8 0x3F08B8DCu      /* 0.53407073 */
#define F_RATE 0x3B427301u        /* 0.0029670598 */
#define F_RATE_4 0x3BC27301u      /* 0.0059341195 */
#define F_RATE_5 0x3C11D641u      /* 0.00890117977 */

#define GIFTAG_824C40 UINT64_C(0x20045B0599421EF0)

static inline float fb(uint32_t bits) { return em_ee_float(bits); }

static int a02_begin(A02Ovl *o, const EmArea02OvlHooks *h, EmArea02OvlFault *fault)
{
    if (!h || !fault) return -1;
    a02_open(o, h, fault);
    return a02_failed(o) ? -1 : 0;
}

static int a02_end(const A02Ovl *o) { return a02_failed(o) ? -1 : 0; }

/* The scratchpad vector 0x700038A0..AC, stored in address order. */
static void a02_vec_a0(A02Ovl *o, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    a02_w32(o, S_700038A0, x);
    a02_w32(o, S_700038A4, y);
    a02_w32(o, S_700038A8, z);
    a02_w32(o, S_700038AC, w);
}


/* ======================================================================
 * 0x823580 (overlay_AREA02_func_00823540). +0x04: 0 zeroes the counter
 * word +0x200 (block +0x1F0, +0x10), sets +0x04 = 1 and falls into 1;
 * 1: at counter 0 and 10 one 0x80000041 spawn (001EFD20) at the
 * scratchpad point 0x700038A0; at 60, 65, 70 and 75 one 0x80000074 spawn
 * each (001EFD90 with the point and the rotation 0x700038B0), the results
 * kept at +0x1F0, +0x1F4, +0x1F8, +0x1FC, and at 70 byte +4 of the first
 * result = 3 (when it is not 0); then the counter counts and past 0x50
 * +0x04 = 3. 2/3 free (001AFC10). Other values: nothing.
 * ====================================================================== */
static void a02_823580_spawn(A02Ovl *o, uint32_t x, uint32_t slot)
{
    int32_t r;
    a02_w32(o, S_700038B0, F_ZERO);
    a02_vec_a0(o, x, F_20, F_10, F_ONE);
    a02_w32(o, S_700038B4, F_MINUS_HALF_PI);
    a02_w32(o, S_700038B8, F_ZERO);
    a02_w32(o, S_700038BC, F_ONE);
    r = 0;
    if (a02_c_001EFD90(o, (int32_t)0x80000074u, S_700038A0, S_700038B0, &r) == 0) a02_w32(o, slot, (uint32_t)r);
}

static void a02_823580(A02Ovl *o, uint32_t self)
{
    uint32_t state = a02_u8(o, self + 4);
    uint32_t blk = self + 0x1F0;
    uint32_t e;

    if (state == 3 || state == 2) {
        (void)a02_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        a02_w32(o, blk + 0x10, 0);
        a02_w8(o, self + 4, 1);
    } else if (state != 1) {
        return;
    }
    switch (a02_s32(o, blk + 0x10)) {
    case 0:
        a02_vec_a0(o, F_175, F_10, F_ZERO, F_ONE);
        e = 0;
        (void)a02_c_001EFD20(o, (int32_t)0x80000041u, S_700038A0, &e);
        break;
    case 10:
        a02_vec_a0(o, F_175, F_20, F_10, F_ONE);
        e = 0;
        (void)a02_c_001EFD20(o, (int32_t)0x80000041u, S_700038A0, &e);
        break;
    case 60:
        a02_823580_spawn(o, F_170, blk + 0);
        break;
    case 65:
        a02_823580_spawn(o, F_140, blk + 4);
        break;
    case 70: {
        a02_823580_spawn(o, F_110, blk + 8);
        uint32_t first = a02_u32(o, blk + 0);
        if (first != 0) a02_w8(o, first + 4, 3);
        break;
    }
    case 75:
        a02_823580_spawn(o, F_80, blk + 0xC);
        break;
    default:
        break;
    }
    a02_w32(o, blk + 0x10, a02_u32(o, blk + 0x10) + 1);
    if (a02_s32(o, blk + 0x10) > 0x50) a02_w8(o, self + 4, 3);
}

int em_area02_ovl_00823580(const EmArea02OvlHooks *h, uint32_t self, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (a02_begin(&o, h, fault)) return -1;
    a02_823580(&o, self);
    return a02_end(&o);
}

/* ======================================================================
 * 0x823900 (func_overlay_AREA02_008238C0) — the area init: five boot
 * words (0x20, 0x20, the overlay address 0x829280, 0, 0x969E80).
 * ====================================================================== */
int em_area02_ovl_00823900(const EmArea02OvlHooks *h, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (a02_begin(&o, h, fault)) return -1;
    a02_w32(&o, D_00275C28, 0x20);
    a02_w32(&o, D_00275C24, 0x20);
    a02_w32(&o, D_00275C1C, A02_DATA_829280);
    a02_w32(&o, D_00275C2C, 0);
    a02_w32(&o, D_00275C18, A02_DATA_969E80);
    return a02_end(&o);
}

/* ======================================================================
 * 0x824D50 (func_overlay_AREA02_00824D10) — the zone test of the switch.
 * The owner is +0x1C. Its four local points 0x827630.. go through the
 * owner's bone (+0x110)[0] + 0x90 into +0x2B0.. (001026A0). When the
 * owner's (x, z) is within 70 of (D_00810360, D_00810368) (squared
 * distance < 4900): 2 when x = D_00810350 lies between point 0 and point
 * 1 x, 001B1EA0(0, 0x810350, 0x8275B0, 4) == 1, -pi/2 < yaw < 0 and
 * D_0081050C == 3; else 1 for points 3 / 2, quad 0x8275F0 and
 * -pi < yaw <= -pi/2 (same D_0081050C test); else 0.
 * ====================================================================== */
static int32_t a02_824D50(A02Ovl *o, uint32_t self)
{
    uint32_t owner = a02_u32(o, self + 0x1C);
    uint32_t v = self + 0x2B0;
    uint32_t i;
    int32_t r;

    for (i = 0; i < 4; i++) {
        uint32_t bone = a02_u32(o, owner + 0x110);
        (void)a02_c_001026A0(o, v + 0x10 * i, bone + 0x90, A02_POINTS_827630 + 0x10 * i);
    }
    uint32_t px = a02_u32(o, D_00810360);
    uint32_t ox = a02_u32(o, owner + 0xB0);
    uint32_t oz = a02_u32(o, owner + 0xB8);
    uint32_t pz = a02_u32(o, D_00810368);
    uint32_t dx = em_ee_sub_bits(ox, px);
    uint32_t acc = em_ee_mula_bits(dx, dx);
    uint32_t dz = em_ee_sub_bits(oz, pz);
    uint32_t d2 = em_ee_madd_bits(acc, dz, dz);
    if (!em_ee_c_lt_bits(d2, F_4900)) return 0;

    uint32_t x = a02_u32(o, D_00810350);
    if (em_ee_c_lt_bits(x, a02_u32(o, v + 0x10)) && !em_ee_c_le_bits(x, a02_u32(o, v + 0x00))) {
        r = 0;
        (void)a02_c_001B1EA0(o, 0, D_00810350, A02_QUAD_8275B0, 4, &r);
        if (r == 1) {
            uint32_t yaw = a02_u32(o, D_00810374);
            if (em_ee_c_lt_bits(F_MINUS_HALF_PI, yaw) && em_ee_c_lt_bits(yaw, F_ZERO)
                && a02_u8(o, D_0081050C) == 3)
                return 2;
        }
    }
    x = a02_u32(o, D_00810350);
    if (em_ee_c_lt_bits(x, a02_u32(o, v + 0x20)) && !em_ee_c_le_bits(x, a02_u32(o, v + 0x30))) {
        r = 0;
        (void)a02_c_001B1EA0(o, 0, D_00810350, A02_QUAD_8275F0, 4, &r);
        if (r == 1) {
            uint32_t yaw = a02_u32(o, D_00810374);
            if (em_ee_c_lt_bits(F_MINUS_PI, yaw) && em_ee_c_le_bits(yaw, F_MINUS_HALF_PI)
                && a02_u8(o, D_0081050C) == 3)
                return 1;
        }
    }
    return 0;
}

int em_area02_ovl_00824D50(const EmArea02OvlHooks *h, uint32_t self, int32_t *result, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (!result || a02_begin(&o, h, fault)) return -1;
    int32_t v = a02_824D50(&o, self);
    if (a02_end(&o)) return -1;
    *result = v;
    return 0;
}

/* ======================================================================
 * 0x823980 (func_overlay_AREA02_00823940) — the switch (kind 9 of
 * 0x823930). +0x04 0: with D_00810761 set, D_00810761 = D_008107E1 = 0xFF,
 * +0x04 = 3, 0019C6F0(0x1D, 1) and (0x1E, 1); else +0x30 = 0x827340,
 * +0x04 = +0x00 = 1, +0x28 = +0x2A = 0. 1: by D_008107E1: 0 marks the
 * switch (001F4BF0 at +0xB0 + (1, 15, -3.5) with the words (0, 0x80, 0,
 * 0x80)) and on a Use (+0x0B bit 2) sets both flags to 1 and starts script
 * 0x826780; 1: at the script's end 001FABB0, 001FA790(0, 0x13),
 * D_008107E1 |= 2; 0xFF: once (+0x06 == 0 and D_00282154 == 0) +0x06 + 1
 * and 001FAE70(0). Then 001B17A0 and, while D_008107E1 != 0, the +0x05
 * steps (0x824D50's zone: 1 -> step 1, 2 -> step 8; the scripts 0x826F40 /
 * 0x826B40; the clamps of D_00810350 at -25 and D_00810358 at -66).
 * 2/3 free.
 * ====================================================================== */
static void a02_clamp_x(A02Ovl *o, int both)
{
    if (!em_ee_c_le_bits(a02_u32(o, D_00810350), F_MINUS_25)) return;
    a02_w32(o, D_00810350, F_MINUS_25);
    if (!both) return;
    if (em_ee_c_le_bits(a02_u32(o, D_00810358), F_MINUS_66)) a02_w32(o, D_00810358, F_MINUS_66);
}

static void a02_823980(A02Ovl *o, uint32_t self)
{
    uint32_t state = a02_u8(o, self + 4);
    uint32_t talk = self + 0x1F0;
    int32_t r;

    if (state == 3 || state == 2) {
        (void)a02_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        if (a02_u8(o, D_00810761) != 0) {
            a02_w8(o, D_00810761, 0xFF);
            a02_w8(o, D_008107E1, 0xFF);
            a02_w8(o, self + 4, 3);
            (void)a02_c_0019C6F0(o, 0x1D, 1);
            (void)a02_c_0019C6F0(o, 0x1E, 1);
        } else {
            a02_w32(o, self + 0x30, A02_DATA_827340);
            a02_w8(o, self + 4, 1);
            a02_w8(o, self + 0, 1);
            a02_w16(o, self + 0x28, 0);
            a02_w16(o, self + 0x2A, 0);
        }
        return;
    }
    if (state != 1) return;

    switch (a02_u8(o, D_008107E1)) {
    case 0: {
        uint32_t x = a02_u32(o, self + 0xB0);
        a02_w32(o, S_700038A0, em_ee_add_bits(F_ONE, x));
        uint32_t y = a02_u32(o, self + 0xB4);
        a02_w32(o, S_700038A4, em_ee_add_bits(F_15, y));
        uint32_t z = a02_u32(o, self + 0xB8);
        a02_w32(o, S_700038B0, 0);
        a02_w32(o, S_700038B4, 0x80);
        a02_w32(o, S_700038B8, 0);
        a02_w32(o, S_700038A8, em_ee_sub_bits(z, F_3_5));
        a02_w32(o, S_700038AC, F_ONE);
        a02_w32(o, S_700038BC, 0x80);
        (void)a02_c_001F4BF0(o, S_700038A0, S_700038B0);
        if (a02_u8(o, self + 0xB) & 4) {
            a02_w8(o, D_00810761, 1);
            a02_w8(o, D_008107E1, 1);
            (void)a02_c_001BA1A0(o, talk, A02_SCRIPT_826780);
        }
        break;
    }
    case 1:
        r = 0;
        (void)a02_c_001BA1F0(o, self, &r);
        if (r != 0) {
            (void)a02_c_001FABB0(o);
            (void)a02_c_001FA790(o, 0, 0x13);
            a02_w8(o, D_008107E1, a02_u8(o, D_008107E1) | 2);
        }
        break;
    case 0xFF: {
        uint32_t six = a02_u8(o, self + 6);
        if (six == 0 && a02_u8(o, D_00282154) == 0) {
            a02_w8(o, self + 6, six + 1);
            (void)a02_c_001FAE70(o, 0);
        }
        break;
    }
    default:
        break;
    }
    (void)a02_c_001B17A0(o, self);
    if (a02_u8(o, D_008107E1) == 0) return;

    switch (a02_u8(o, self + 5)) {
    case 0: {
        int32_t zone = a02_824D50(o, self);
        if (a02_failed(o)) return;
        if (zone == 2) a02_w8(o, self + 5, 8);
        else if (zone == 1) a02_w8(o, self + 5, 1);
        break;
    }
    case 1:
        (void)a02_c_001BA1A0(o, talk, A02_SCRIPT_826F40);
        a02_w8(o, self + 5, 2);
        break;
    case 2:
        r = 0;
        (void)a02_c_001BA1F0(o, self, &r);
        if (r != 0) a02_w8(o, self + 5, 3);
        a02_clamp_x(o, 1);
        break;
    case 8:
        (void)a02_c_001BA1A0(o, talk, A02_SCRIPT_826B40);
        a02_w8(o, self + 5, 9);
        break;
    case 9:
        r = 0;
        (void)a02_c_001BA1F0(o, self, &r);
        if (r != 0) a02_w8(o, self + 5, 10);
        a02_clamp_x(o, 0);
        break;
    default: /* 3, 10 and other values: nothing */
        break;
    }
}

int em_area02_ovl_00823980(const EmArea02OvlHooks *h, uint32_t self, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (a02_begin(&o, h, fault)) return -1;
    a02_823980(&o, self);
    return a02_end(&o);
}

/* ======================================================================
 * 0x823D70 (func_overlay_AREA02_00823D30) — the gates, by kind +0x0D.
 * +0x04 0: with D_00810761 set, kind 14 frees itself (+0x04 = 3) and the
 * others start at +0x05 = 1; otherwise (and then) 001B0FD0, +0x04 = +0x00
 * = 1. 1: kind 14 with x < -270 frees itself once D_008107E1 bit 2 is set,
 * animating (001B1B70, 001C6380, callback) meanwhile; kind 14 with
 * x > 170: +0x05 0 animates and on bit 3 goes to +0x05 = 1 with the count
 * +0x28 = 5; +0x05 1 counts +0x28 down and at 0 frees itself with
 * 0019C6F0(0x1D, 1), (0x1E, 1), then 001B1B70. Kind 15: +0x05 0 waits for
 * bit 3, 1 animates. Kind 16: the same with bit 2. 2/3 free.
 * ====================================================================== */
static void a02_animate(A02Ovl *o, uint32_t self)
{
    (void)a02_c_001B1B70(o, self);
    (void)a02_c_001C6380(o, self);
    a02_callback(o, self);
}

static void a02_823D70(A02Ovl *o, uint32_t self)
{
    uint32_t state = a02_u8(o, self + 4);

    if (state == 3 || state == 2) {
        (void)a02_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        if (a02_u8(o, D_00810761) != 0) {
            if (a02_u8(o, self + 0xD) == 0xE) {
                a02_w8(o, self + 4, 3);
                return;
            }
            a02_w8(o, self + 5, 1);
        }
        int32_t r = 0;
        (void)a02_c_001B0FD0(o, self, &r);
        a02_w8(o, self + 4, 1);
        a02_w8(o, self + 0, 1);
        return;
    }
    if (state != 1) return;

    uint32_t kind = a02_u8(o, self + 0xD);
    if (kind == 0xE && em_ee_c_lt_bits(a02_u32(o, self + 0xB0), F_MINUS_270)) {
        if (a02_u8(o, D_008107E1) & 4) a02_w8(o, self + 4, 3);
        a02_animate(o, self);
    } else if (kind == 0xE && !em_ee_c_le_bits(a02_u32(o, self + 0xB0), F_170)) {
        uint32_t step = a02_u8(o, self + 5);
        if (step == 0) {
            if (a02_u8(o, D_008107E1) & 8) {
                a02_w8(o, self + 5, 1);
                a02_w16(o, self + 0x28, 5);
            }
            a02_animate(o, self);
        } else if (step == 1) {
            uint32_t n = (a02_u16(o, self + 0x28) - 1) & 0xFFFFu;
            a02_w16(o, self + 0x28, n);
            if (n == 0) {
                a02_w8(o, self + 4, 3);
                (void)a02_c_0019C6F0(o, 0x1D, 1);
                (void)a02_c_0019C6F0(o, 0x1E, 1);
            }
            (void)a02_c_001B1B70(o, self);
        }
    } else if (kind == 0xF || kind == 0x10) {
        uint32_t step = a02_u8(o, self + 5);
        if (step == 1) {
            a02_animate(o, self);
        } else if (step == 0) {
            if (a02_u8(o, D_008107E1) & (kind == 0xF ? 8u : 4u)) a02_w8(o, self + 5, 1);
        }
    }
}

int em_area02_ovl_00823D70(const EmArea02OvlHooks *h, uint32_t self, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (a02_begin(&o, h, fault)) return -1;
    a02_823D70(&o, self);
    return a02_end(&o);
}

/* ======================================================================
 * 0x824800 (func_overlay_AREA02_008247C0) — model setup: 001CA6E0(self,
 * 001C6120(D_0028A59C, +0x0D)), +0x40 = D_0028A6E8, +0x0C = 001C6150(+0x44)
 * (a byte); above the cap D_00275BCC (signed halfword) +0x04 = 3 and 0.
 * Otherwise one 001AF780 result per bone to +0x110 + 4i (the count
 * re-read every pass), +0x09 = the count, 001CB5B0(count), 001C63E0(self,
 * 0) for kind 7, (self, 1) for kind 8; returns 1.
 * ====================================================================== */
static int32_t a02_824800(A02Ovl *o, uint32_t self)
{
    uint32_t kind = a02_u8(o, self + 0xD);
    uint32_t table = a02_u32(o, D_0028A59C);
    uint32_t model = 0;
    int32_t count = 0;
    uint32_t n;
    int32_t i;

    (void)a02_c_001C6120(o, table, (int32_t)kind, &model);
    (void)a02_c_001CA6E0(o, self, model);
    a02_w32(o, self + 0x40, a02_u32(o, D_0028A6E8));
    (void)a02_c_001C6150(o, a02_u32(o, self + 0x44), &count);
    a02_w8(o, self + 0xC, (uint32_t)count);
    int32_t cap = (int16_t)a02_u16(o, D_00275BCC);
    if (cap < (int32_t)a02_u8(o, self + 0xC)) {
        a02_w8(o, self + 4, 3);
        return 0;
    }
    for (i = 0;; i++) {
        n = a02_u8(o, self + 0xC);
        if (a02_failed(o) || !(i < (int32_t)n)) break;
        uint32_t slot = 0;
        (void)a02_c_001AF780(o, &slot);
        a02_w32(o, self + 0x110 + 4 * (uint32_t)i, slot);
    }
    a02_w8(o, self + 9, n);
    (void)a02_c_001CB5B0(o, (int32_t)a02_u8(o, self + 0xC));
    kind = a02_u8(o, self + 0xD);
    if (kind == 7) (void)a02_c_001C63E0(o, self, 0);
    else if (kind == 8) (void)a02_c_001C63E0(o, self, 1);
    return 1;
}

int em_area02_ovl_00824800(const EmArea02OvlHooks *h, uint32_t self, int32_t *result, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (!result || a02_begin(&o, h, fault)) return -1;
    int32_t v = a02_824800(&o, self);
    if (a02_end(&o)) return -1;
    *result = v;
    return 0;
}

/* ======================================================================
 * 0x824910 (func_overlay_AREA02_008248D0) — the 18 trigger records at
 * 0x827350 (0x20 bytes: id +0x00, done +0x04, point +0x10). Each record not
 * yet done fires once the actor's x passes the point's x: id 0x80000001
 * first spawns 0x80000013 at the point + (0, 10, 0) (w 1), id 0x80000002
 * spawns 0x8000002F there; then 001EFD20(id, point), done = 1 and one of
 * the sounds 0x449 / 0x44A / 0x44B at 200 by rand % 3 (a negative
 * remainder plays none). The two spawn points are stack locals at sp -
 * 0x20 and sp - 0x10 (frame 0x60).
 * ====================================================================== */
static void a02_824910(A02Ovl *o, uint32_t self, uint32_t sp)
{
    uint32_t a = sp - 0x60 + 0x40, b = sp - 0x60 + 0x50;
    uint32_t i, e;

    for (i = 0; i < 18 && !a02_failed(o); i++) {
        uint32_t t = A02_TRIGGERS_827350 + 0x20 * i;
        if (a02_u32(o, t + 4) != 0) continue;
        uint32_t x = a02_u32(o, self + 0xB0);
        uint32_t p0 = a02_u32(o, t + 0x10);
        if (em_ee_c_le_bits(x, p0)) continue;
        if (a02_u32(o, t) == 0x80000001u) {
            a02_w32(o, a + 0, p0);
            a02_w32(o, a + 4, em_ee_add_bits(F_10, a02_u32(o, t + 0x14)));
            a02_w32(o, a + 8, a02_u32(o, t + 0x18));
            a02_w32(o, a + 0xC, F_ONE);
            e = 0;
            (void)a02_c_001EFD20(o, (int32_t)0x80000013u, a, &e);
        }
        if (a02_u32(o, t) == 0x80000002u) {
            a02_w32(o, b + 0, a02_u32(o, t + 0x10));
            a02_w32(o, b + 4, em_ee_add_bits(F_10, a02_u32(o, t + 0x14)));
            a02_w32(o, b + 8, a02_u32(o, t + 0x18));
            a02_w32(o, b + 0xC, F_ONE);
            e = 0;
            (void)a02_c_001EFD20(o, (int32_t)0x8000002Fu, b, &e);
        }
        uint32_t id = a02_u32(o, t);
        e = 0;
        (void)a02_c_001EFD20(o, (int32_t)id, t + 0x10, &e);
        a02_w32(o, t + 4, 1);
        int32_t r = 0;
        (void)a02_c_00122BB8(o, &r);
        switch (r % 3) {
        case 0:
            (void)a02_c_001F02C0(o, t + 0x10, 0x449, fb(F_200));
            break;
        case 1:
            (void)a02_c_001F02C0(o, t + 0x10, 0x44A, fb(F_200));
            break;
        case 2:
            (void)a02_c_001F02C0(o, t + 0x10, 0x44B, fb(F_200));
            break;
        default:
            break;
        }
    }
}

int em_area02_ovl_00824910(const EmArea02OvlHooks *h, uint32_t self, uint32_t sp, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (a02_begin(&o, h, fault)) return -1;
    a02_824910(&o, self, sp);
    return a02_end(&o);
}

/* ======================================================================
 * 0x824AC0 (func_overlay_AREA02_00824A80) — when the frame counter
 * 0x70003B68 % 8 == 0 (C remainder): four 001EFD20(0x80000030, 0x700038A0)
 * spawns; each non-null one gets the point (10, 0, -20 + rand % 80, 1),
 * transformed in place by the actor's bone (+0x110)[0] + 0x90 (001026A0),
 * x += 40, +0xC0 = pi (rand % 150 + 15) / 180, +0xC4 = pi (rand % 360) / 180,
 * +0xC8 = 0.
 * ====================================================================== */
static void a02_824AC0(A02Ovl *o, uint32_t self)
{
    int32_t i, r;

    if (a02_s32(o, S_70003B68) % 8 != 0) return;
    for (i = 0; i < 4 && !a02_failed(o); i++) {
        uint32_t e = 0;
        (void)a02_c_001EFD20(o, (int32_t)0x80000030u, S_700038A0, &e);
        if (a02_failed(o) || e == 0) continue;
        a02_w32(o, e + 0xB0, F_10);
        a02_w32(o, e + 0xB4, 0);
        r = 0;
        (void)a02_c_00122BB8(o, &r);
        a02_w32(o, e + 0xB8, em_ee_add_bits(F_MINUS_20, em_ee_cvt_s_w_bits((uint32_t)(r % 80))));
        a02_w32(o, e + 0xBC, F_ONE);
        uint32_t bone = a02_u32(o, self + 0x110);
        (void)a02_c_001026A0(o, e + 0xB0, bone + 0x90, e + 0xB0);
        a02_w32(o, e + 0xB0, em_ee_add_bits(a02_u32(o, e + 0xB0), F_40));
        r = 0;
        (void)a02_c_00122BB8(o, &r);
        uint32_t v = em_ee_cvt_s_w_bits((uint32_t)(r % 150 + 15));
        a02_w32(o, e + 0xC0, em_ee_div_bits(em_ee_mul_bits(F_PI, v), F_180));
        r = 0;
        (void)a02_c_00122BB8(o, &r);
        v = em_ee_cvt_s_w_bits((uint32_t)(r % 360));
        a02_w32(o, e + 0xC4, em_ee_div_bits(em_ee_mul_bits(F_PI, v), F_180));
        a02_w32(o, e + 0xC8, 0);
    }
}

int em_area02_ovl_00824AC0(const EmArea02OvlHooks *h, uint32_t self, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (a02_begin(&o, h, fault)) return -1;
    a02_824AC0(&o, self);
    return a02_end(&o);
}

/* ======================================================================
 * 0x824C40 (func_overlay_AREA02_00824C00) — one sprite at `point`: grey
 * level k = 255 - (rand & 15) in all three channels, 001CD520(0, 2, point,
 * tag, 14, 14, 4, colour).
 * ====================================================================== */
static void a02_824C40(A02Ovl *o, uint32_t point)
{
    int32_t r = 0;
    (void)a02_c_00122BB8(o, &r);
    uint32_t k = 0xFFu - ((uint32_t)r & 0xFu);
    uint32_t v = k << 7;
    uint32_t c = (v >> 7) << 16;
    c |= (v >> 7) << 8;
    c |= v >> 7;
    (void)a02_c_001CD520(o, 0, 2, point, GIFTAG_824C40, fb(F_14), fb(F_14), fb(F_4), c);
}

int em_area02_ovl_00824C40(const EmArea02OvlHooks *h, uint32_t point, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (a02_begin(&o, h, fault)) return -1;
    a02_824C40(&o, point);
    return a02_end(&o);
}

/* ======================================================================
 * 0x824CD0 (func_overlay_AREA02_00824C90) — the points 0x827590 and
 * 0x8275A0 are copied (16 bytes each) into stack locals at sp - 0x40 and
 * sp - 0x30, transformed by the bone (+0x110)[0] + 0x90 into sp - 0x20 and
 * sp - 0x10 (001026A0, the bone pointer read before each), and 0x824C40
 * draws a sprite at each result (frame 0x60).
 * ====================================================================== */
static void a02_copy16(A02Ovl *o, uint32_t dst, uint32_t src)
{
    uint64_t lo = a02_u64(o, src);
    uint64_t hi = a02_u64(o, src + 8);
    a02_w64(o, dst, lo);
    a02_w64(o, dst + 8, hi);
}

static void a02_824CD0(A02Ovl *o, uint32_t self, uint32_t sp)
{
    uint32_t frame = sp - 0x60;
    uint32_t a = frame + 0x20, b = frame + 0x30, w0 = frame + 0x40, w1 = frame + 0x50;

    a02_copy16(o, a, A02_POINT_827590);
    a02_copy16(o, b, A02_POINT_8275A0);
    uint32_t bone = a02_u32(o, self + 0x110);
    (void)a02_c_001026A0(o, w0, bone + 0x90, a);
    bone = a02_u32(o, self + 0x110);
    (void)a02_c_001026A0(o, w1, bone + 0x90, b);
    a02_824C40(o, w0);
    a02_824C40(o, w1);
}

int em_area02_ovl_00824CD0(const EmArea02OvlHooks *h, uint32_t self, uint32_t sp, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (a02_begin(&o, h, fault)) return -1;
    a02_824CD0(&o, self, sp);
    return a02_end(&o);
}

/* ======================================================================
 * 0x8242F0 (func_overlay_AREA02_008242B0) — one step of kind 7 (the car)
 * or 8 (+0x0D). +0x28 + 1. Kinds 7/8: 001C64F0(self, 1.0), 001C68C0, the
 * callback; kind 7 also 001B1B70 and 001A2370(self, bone (+0x110)[0] +
 * 0x90). Kind 7 by +0x06 (a jump table in the original): 0 starts script
 * 0x826980, +0x06 = 1, counter +0x2E0 = 0 and returns 0 at once; 1..5 turn
 * +0xC4 (001B12B0(goal, +0xC4, rate)) and step on at each script's end
 * (0x8269C0, 0x826A00, 0x826A40, 0x826A80); 6: D_008107E1 |= 0x80, +0x06 =
 * 7; 7: +0x04 = 3, returns 1. Then +0x07: 0 once x > -280: D_008107E1 |=
 * 4, +0x07 = 1, sounds 0x8B3 and 0x8B4 (001F02C0 at +0xB0, 900); 1 once
 * x > 140: |= 8, +0x07 = 2, 001F6B30, sound 0x8B5 (300), 001EFD20(0,
 * +0xB0); 2: counter + 1. Then 001AA700, 0x824CD0, 0x824910, 0x824AC0,
 * +0x2A + 1, 0. Kind 8 by +0x06: 0 script 0x826AC0, +0x06 = 1, counter 0;
 * 1 counter + 1, +0x06 = 2 at the script's end, turn toward 0.534; 2:
 * D_008107E1 |= 0x40, +0x06 = 3; 3: 0019C6F0(1, 1), +0x04 = 3; then +0x2A
 * + 1, 0. Frame 0x40.
 * ====================================================================== */
static void a02_turn(A02Ovl *o, uint32_t self, uint32_t goal, uint32_t rate)
{
    float out = 0.0f;
    uint32_t cur = a02_u32(o, self + 0xC4);
    if (a02_c_001B12B0(o, fb(goal), fb(cur), fb(rate), &out) == 0) a02_w32(o, self + 0xC4, em_ee_bits(out));
}

static int a02_script_done(A02Ovl *o, uint32_t self)
{
    int32_t r = 0;
    (void)a02_c_001BA1F0(o, self, &r);
    return !a02_failed(o) && r != 0;
}

static int32_t a02_8242F0(A02Ovl *o, uint32_t self, uint32_t sp)
{
    uint32_t cnt = self + 0x2E0;
    uint32_t talk = self + 0x1F0;
    uint32_t dt = F_ZERO;
    uint32_t e;

    a02_w16(o, self + 0x28, a02_u16(o, self + 0x28) + 1);
    uint32_t kind = a02_u8(o, self + 0xD);
    if (kind == 7) dt = F_ONE;
    if (kind == 8) dt = F_ONE;
    if (kind == 7 || kind == 8) {
        (void)a02_c_001C64F0(o, self, fb(dt));
        (void)a02_c_001C68C0(o, self);
        a02_callback(o, self);
        if (a02_u8(o, self + 0xD) == 7) {
            (void)a02_c_001B1B70(o, self);
            uint32_t bone = a02_u32(o, self + 0x110);
            (void)a02_c_001A2370(o, self, bone + 0x90);
        }
    }
    kind = a02_u8(o, self + 0xD);
    if (kind == 7) {
        switch (a02_u8(o, self + 6)) {
        case 0:
            (void)a02_c_001BA1A0(o, talk, A02_SCRIPT_826980);
            a02_w8(o, self + 6, 1);
            a02_w32(o, cnt, 0);
            return 0;
        case 1:
            if (a02_script_done(o, self)) {
                a02_w8(o, self + 6, 2);
                (void)a02_c_001BA1A0(o, talk, A02_SCRIPT_8269C0);
            }
            a02_turn(o, self, F_GOAL_1, F_RATE);
            break;
        case 2:
            a02_turn(o, self, F_GOAL_2, F_RATE);
            if (a02_script_done(o, self)) {
                a02_w8(o, self + 6, 3);
                (void)a02_c_001BA1A0(o, talk, A02_SCRIPT_826A00);
            }
            break;
        case 3:
            a02_turn(o, self, F_GOAL_3, F_RATE);
            if (a02_script_done(o, self)) {
                a02_w8(o, self + 6, 4);
                (void)a02_c_001BA1A0(o, talk, A02_SCRIPT_826A40);
            }
            break;
        case 4:
            a02_turn(o, self, F_GOAL_4, F_RATE_4);
            if (a02_script_done(o, self)) {
                a02_w8(o, self + 6, 5);
                (void)a02_c_001BA1A0(o, talk, A02_SCRIPT_826A80);
            }
            break;
        case 5:
            a02_turn(o, self, F_GOAL_5, F_RATE_5);
            if (a02_script_done(o, self)) a02_w8(o, self + 6, 6);
            break;
        case 6:
            a02_w8(o, D_008107E1, a02_u8(o, D_008107E1) | 0x80);
            a02_w8(o, self + 6, 7);
            break;
        case 7:
            a02_w8(o, self + 4, 3);
            return 1;
        default:
            break;
        }
        switch (a02_u8(o, self + 7)) {
        case 0:
            if (!em_ee_c_le_bits(a02_u32(o, self + 0xB0), F_MINUS_280)) {
                a02_w8(o, D_008107E1, a02_u8(o, D_008107E1) | 4);
                a02_w8(o, self + 7, 1);
                (void)a02_c_001F02C0(o, self + 0xB0, 0x8B3, fb(F_900));
                (void)a02_c_001F02C0(o, self + 0xB0, 0x8B4, fb(F_900));
            }
            break;
        case 1:
            if (!em_ee_c_le_bits(a02_u32(o, self + 0xB0), F_140)) {
                a02_w8(o, D_008107E1, a02_u8(o, D_008107E1) | 8);
                a02_w8(o, self + 7, 2);
                (void)a02_c_001F6B30(o);
                (void)a02_c_001F02C0(o, self + 0xB0, 0x8B5, fb(F_300));
                e = 0;
                (void)a02_c_001EFD20(o, 0, self + 0xB0, &e);
            }
            break;
        case 2:
            a02_w32(o, cnt, a02_u32(o, cnt) + 1);
            break;
        default:
            break;
        }
        (void)a02_c_001AA700(o, self);
        a02_824CD0(o, self, sp - 0x40);
        a02_824910(o, self, sp - 0x40);
        a02_824AC0(o, self);
        a02_w16(o, self + 0x2A, a02_u16(o, self + 0x2A) + 1);
        return 0;
    }
    if (kind == 8) {
        switch (a02_u8(o, self + 6)) {
        case 0:
            (void)a02_c_001BA1A0(o, talk, A02_SCRIPT_826AC0);
            a02_w8(o, self + 6, 1);
            a02_w32(o, cnt, 0);
            break;
        case 1:
            a02_w32(o, cnt, a02_u32(o, cnt) + 1);
            if (a02_script_done(o, self)) a02_w8(o, self + 6, 2);
            a02_turn(o, self, F_GOAL_8, F_RATE);
            break;
        case 2:
            a02_w8(o, D_008107E1, a02_u8(o, D_008107E1) | 0x40);
            a02_w8(o, self + 6, 3);
            break;
        case 3:
            (void)a02_c_0019C6F0(o, 1, 1);
            a02_w8(o, self + 4, 3);
            break;
        default:
            break;
        }
        a02_w16(o, self + 0x2A, a02_u16(o, self + 0x2A) + 1);
        return 0;
    }
    return 0;
}

int em_area02_ovl_008242F0(const EmArea02OvlHooks *h, uint32_t self, uint32_t sp, int32_t *result,
                           EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (!result || a02_begin(&o, h, fault)) return -1;
    int32_t v = a02_8242F0(&o, self, sp);
    if (a02_end(&o)) return -1;
    *result = v;
    return 0;
}

/* ======================================================================
 * 0x824020 (func_overlay_AREA02_00823FE0) — kind-4 owners of 0x823930, by
 * +0x0D. +0x04 0: with D_00810761 set, kinds 10/11 set +0x05 = 1 and call
 * 001B0FD0 and 0019C6F0(1, 1), other kinds free themselves (+0x04 = 3).
 * Otherwise kinds 10/11 call 001B0FD0 and the others need 0x824800 to
 * return non-zero (else +0x04 = 3); then +0x1F0.. (0x40 words) = 0,
 * 00121A28(+0x2B0, 0, 0x40), +0x04 = +0x00 = 1, +0x28 = +0x2A = 0.
 * 1: kinds 7/8: +0x05 0 waits for D_008107E1 bit 1; 1 runs 0x8242F0 and
 * when it returns non-zero D_00810761 = D_008107E1 = 0xFF. Kind 10: +0x05 0
 * waits for bit 7 (001B6660(0x825C00), +0x05 = 1), 1 animates. Kind 11:
 * waits for bit 6, then animates. 2/3 free. Frame 0x20.
 * ====================================================================== */
static void a02_824020(A02Ovl *o, uint32_t self, uint32_t sp)
{
    uint32_t state = a02_u8(o, self + 4);
    int32_t r;
    uint32_t i;

    if (state == 3 || state == 2) {
        (void)a02_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        if (a02_u8(o, D_00810761) != 0) {
            uint32_t kind = a02_u8(o, self + 0xD);
            if (kind == 0xA || kind == 0xB) {
                a02_w8(o, self + 5, 1);
                r = 0;
                (void)a02_c_001B0FD0(o, self, &r);
                (void)a02_c_0019C6F0(o, 1, 1);
            } else {
                a02_w8(o, self + 4, 3);
            }
            return;
        }
        uint32_t kind = a02_u8(o, self + 0xD);
        if (kind == 0xA || kind == 0xB) {
            r = 0;
            (void)a02_c_001B0FD0(o, self, &r);
        } else {
            int32_t ok = a02_824800(o, self);
            if (a02_failed(o)) return;
            if (ok == 0) {
                a02_w8(o, self + 4, 3);
                return;
            }
        }
        for (i = 0; i < 0x40; i++) a02_w32(o, self + 0x1F0 + 4 * i, 0);
        (void)a02_c_00121A28(o, self + 0x2B0, 0, 0x40);
        a02_w8(o, self + 4, 1);
        a02_w8(o, self + 0, 1);
        a02_w16(o, self + 0x28, 0);
        a02_w16(o, self + 0x2A, 0);
        return;
    }
    if (state != 1) return;

    uint32_t kind = a02_u8(o, self + 0xD);
    if (kind == 7 || kind == 8) {
        uint32_t step = a02_u8(o, self + 5);
        if (step == 0) {
            if (a02_u8(o, D_008107E1) & 2) a02_w8(o, self + 5, 1);
        } else if (step == 1) {
            int32_t done = a02_8242F0(o, self, sp - 0x20);
            if (!a02_failed(o) && done != 0) {
                a02_w8(o, D_00810761, 0xFF);
                a02_w8(o, D_008107E1, 0xFF);
            }
        }
    }
    if (a02_u8(o, self + 0xD) == 0xA) {
        uint32_t step = a02_u8(o, self + 5);
        if (step == 0) {
            if (a02_u8(o, D_008107E1) & 0x80) {
                (void)a02_c_001B6660(o, A02_GROUP_825C00);
                a02_w8(o, self + 5, 1);
            }
        } else if (step == 1) {
            a02_animate(o, self);
        }
    }
    if (a02_u8(o, self + 0xD) == 0xB) {
        uint32_t step = a02_u8(o, self + 5);
        if (step == 0) {
            if (a02_u8(o, D_008107E1) & 0x40) a02_w8(o, self + 5, 1);
        } else if (step == 1) {
            a02_animate(o, self);
        }
    }
}

int em_area02_ovl_00824020(const EmArea02OvlHooks *h, uint32_t self, uint32_t sp, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (a02_begin(&o, h, fault)) return -1;
    a02_824020(&o, self, sp);
    return a02_end(&o);
}

/* ======================================================================
 * 0x823930 (func_overlay_AREA02_008238F0) — dispatch by +0x02 & 0x1F:
 * 9 -> 0x823980, 4 -> 0x824020, others nothing. (The original leaves the
 * masked and the raw byte in a1 / a2 for the callees, which do not read
 * them.) Frame 0x10.
 * ====================================================================== */
int em_area02_ovl_00823930(const EmArea02OvlHooks *h, uint32_t self, uint32_t sp, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (a02_begin(&o, h, fault)) return -1;
    uint32_t kind = a02_u8(&o, self + 2) & 0x1Fu;
    if (!a02_failed(&o)) {
        if (kind == 9) a02_823980(&o, self);
        else if (kind == 4) a02_824020(&o, self, sp - 0x10);
    }
    return a02_end(&o);
}

/* ======================================================================
 * 0x824FA0 (func_overlay_AREA02_00824F60) — an examine owner. +0x04 0:
 * once 001B0FD0 returns 0: 001C6380, +0x08 = 3, +0x30 = 0x2758E0, +0x04 =
 * 1. 1: +0x00 = 2 when 001BA1C0(self, 9) is non-zero, else 1; +0x05 0 on a
 * Use (+0x0B bit 2) goes to (the value read) + 1 and starts script
 * 0x827670; +0x05 1 clears +0x0B and +0x05 at the script's end; then
 * 001B17A0 and the callback. 2/3 free.
 * ====================================================================== */
static void a02_824FA0(A02Ovl *o, uint32_t self)
{
    uint32_t state = a02_u8(o, self + 4);
    int32_t r;

    if (state == 3 || state == 2) {
        (void)a02_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        r = 0;
        (void)a02_c_001B0FD0(o, self, &r);
        if (a02_failed(o) || r != 0) return;
        (void)a02_c_001C6380(o, self);
        a02_w8(o, self + 8, 3);
        a02_w32(o, self + 0x30, D_002758E0);
        a02_w8(o, self + 4, 1);
        return;
    }
    if (state != 1) return;
    r = 0;
    (void)a02_c_001BA1C0(o, self, 9, &r);
    a02_w8(o, self + 0, r != 0 ? 2 : 1);
    uint32_t step = a02_u8(o, self + 5);
    if (step == 0) {
        if (a02_u8(o, self + 0xB) & 4) {
            a02_w8(o, self + 5, step + 1);
            (void)a02_c_001BA1A0(o, self + 0x1F0, A02_SCRIPT_827670);
        }
    } else if (step == 1) {
        if (a02_script_done(o, self)) {
            a02_w8(o, self + 0xB, 0);
            a02_w8(o, self + 5, 0);
        }
    }
    (void)a02_c_001B17A0(o, self);
    a02_callback(o, self);
}

int em_area02_ovl_00824FA0(const EmArea02OvlHooks *h, uint32_t self, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (a02_begin(&o, h, fault)) return -1;
    a02_824FA0(&o, self);
    return a02_end(&o);
}

/* ======================================================================
 * 0x825520 (func_overlay_AREA02_008254E0) — when D_008104C4 (an object)
 * is set, D_008102BA is non-zero and that object's +0x0D is 9:
 * 0x700031F0 = 1; the player's (x, z) = (D_00810350, D_00810358) turns
 * about the object's (+0xB0, +0xB8) by 0.00374 rad (cos 0011DE90, sin
 * 0011E2A8: x' = ox + (dx cos + dz sin), z' = oz + (dx (-sin) + dz cos)) and
 * the yaw D_00810374 = 001B1470(0.00374 + yaw). Frame 0x30.
 * ====================================================================== */
static uint32_t a02_trig(A02Ovl *o, int sine)
{
    float out = 0.0f;
    if (sine) (void)a02_c_0011E2A8(o, fb(F_TURN_RATE), &out);
    else (void)a02_c_0011DE90(o, fb(F_TURN_RATE), &out);
    return em_ee_bits(out);
}

static void a02_825520(A02Ovl *o)
{
    uint32_t obj = a02_u32(o, D_008104C4);
    if (obj == 0 || a02_u8(o, D_008102BA) == 0 || a02_u8(o, obj + 0xD) != 9) return;

    uint32_t px = a02_u32(o, D_00810350);
    a02_w32(o, S_700031F0, 1);
    uint32_t ox = a02_u32(o, obj + 0xB0);
    uint32_t oz = a02_u32(o, obj + 0xB8);
    uint32_t pz = a02_u32(o, D_00810358);
    uint32_t dx = em_ee_sub_bits(px, ox);
    uint32_t dz = em_ee_sub_bits(pz, oz);

    uint32_t t = em_ee_mul_bits(dx, a02_trig(o, 0));
    uint32_t u = em_ee_mul_bits(dz, a02_trig(o, 1));
    ox = a02_u32(o, obj + 0xB0);
    a02_w32(o, D_00810350, em_ee_add_bits(ox, em_ee_add_bits(t, u)));

    t = em_ee_mul_bits(dx, em_ee_neg_bits(a02_trig(o, 1)));
    u = em_ee_mul_bits(dz, a02_trig(o, 0));
    oz = a02_u32(o, obj + 0xB8);
    a02_w32(o, D_00810358, em_ee_add_bits(oz, em_ee_add_bits(t, u)));

    uint32_t yaw = a02_u32(o, D_00810374);
    float out = 0.0f;
    if (a02_c_001B1470(o, fb(em_ee_add_bits(F_TURN_RATE, yaw)), &out) == 0)
        a02_w32(o, D_00810374, em_ee_bits(out));
}

int em_area02_ovl_00825520(const EmArea02OvlHooks *h, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (a02_begin(&o, h, fault)) return -1;
    a02_825520(&o);
    return a02_end(&o);
}

/* ======================================================================
 * 0x825100 (func_overlay_AREA02_008250C0) — by +0x03. +0x04 0: 001B0FD0,
 * +0x30 = 0x829190, +0x00 = 1, +0x38 = 0, +0x04 = 1, D_0081083F = 0.
 * 1 with +0x03 7 (the turntable): +0x05 0 waits for D_0081083F (+0x05 = 1,
 * script 0x828E10); +0x05 1 by D_0081083F: 2 turns the player (0x825520)
 * and continues as 1: +0x38 = 0.00374 when D_0081083F (read again) is 2
 * else 0; at the script's end D_0081083F, +0x05 and +0x38 = 0; 001C6380,
 * 001A2370(self, +0xD0). Then 001C6380 and the two 001B18F0 tests with the
 * scratchpad pairs (58,0,0,1)/(0,0,58,1) and (40,0,40,1)/(-40,0,40,1); the
 * callback runs when either is non-zero. 1 with +0x03 4: +0x05 0 on a Use
 * 001B6F00(self, (0, 0, 5, 1), pi), script 0x828C50, +0x05 = 1; 1 at the
 * script's end D_0081083F = 1, +0x05 = 2; 2 once D_0081083F is 0 clears it,
 * +0x0B and +0x05. Then, while D_00810761 == 0, 001F4BF0 at +0xB0 + (1, 15,
 * 0) with the words (0, 0x80, 0, 0x80); 001C6380, 001B17A0, the callback.
 * 2/3 free.
 * ====================================================================== */
static void a02_825100(A02Ovl *o, uint32_t self)
{
    uint32_t state = a02_u8(o, self + 4);
    uint32_t talk = self + 0x1F0;
    int32_t r;

    if (state == 3 || state == 2) {
        (void)a02_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        r = 0;
        (void)a02_c_001B0FD0(o, self, &r);
        a02_w32(o, self + 0x30, A02_DATA_829190);
        a02_w8(o, self + 0, 1);
        a02_w32(o, self + 0x38, 0);
        a02_w8(o, self + 4, 1);
        a02_w8(o, D_0081083F, 0);
        return;
    }
    if (state != 1) return;

    uint32_t mode = a02_u8(o, self + 3);
    if (mode == 7) {
        uint32_t step = a02_u8(o, self + 5);
        if (step == 0) {
            if (a02_u8(o, D_0081083F) != 0) {
                a02_w8(o, self + 5, 1);
                (void)a02_c_001BA1A0(o, talk, A02_SCRIPT_828E10);
            }
        } else if (step == 1) {
            uint32_t turn = a02_u8(o, D_0081083F);
            if (turn == 1 || turn == 2) {
                if (turn == 2) a02_825520(o);
                if (a02_u8(o, D_0081083F) == 2) a02_w32(o, self + 0x38, F_TURN_RATE);
                else a02_w32(o, self + 0x38, 0);
                if (a02_script_done(o, self)) {
                    a02_w8(o, D_0081083F, 0);
                    a02_w8(o, self + 5, 0);
                    a02_w32(o, self + 0x38, 0);
                }
                (void)a02_c_001C6380(o, self);
                (void)a02_c_001A2370(o, self, self + 0xD0);
            }
        }
        (void)a02_c_001C6380(o, self);
        a02_w32(o, S_700038B0, 0);
        a02_w32(o, S_700038B4, 0);
        a02_w32(o, S_700038A0, F_58);
        a02_w32(o, S_700038B8, F_58);
        a02_w32(o, S_700038A4, 0);
        a02_w32(o, S_700038A8, 0);
        a02_w32(o, S_700038AC, F_ONE);
        a02_w32(o, S_700038BC, F_ONE);
        r = 0;
        (void)a02_c_001B18F0(o, self, S_700038A0, S_700038B0, &r);
        if (a02_failed(o)) return;
        if (r == 0) {
            a02_vec_a0(o, F_40, 0, F_40, F_ONE);
            a02_w32(o, S_700038B0, F_MINUS_40);
            a02_w32(o, S_700038B4, 0);
            a02_w32(o, S_700038B8, F_40);
            a02_w32(o, S_700038BC, F_ONE);
            r = 0;
            (void)a02_c_001B18F0(o, self, S_700038A0, S_700038B0, &r);
            if (a02_failed(o) || r == 0) return;
        }
        a02_callback(o, self);
        return;
    }
    if (mode != 4) return;

    uint32_t step = a02_u8(o, self + 5);
    if (step == 0) {
        if (a02_u8(o, self + 0xB) & 4) {
            a02_vec_a0(o, 0, 0, F_5, F_ONE);
            (void)a02_c_001B6F00(o, self, S_700038A0, fb(F_PI));
            (void)a02_c_001BA1A0(o, talk, A02_SCRIPT_828C50);
            a02_w8(o, self + 5, 1);
        }
    } else if (step == 1) {
        if (a02_script_done(o, self)) {
            a02_w8(o, D_0081083F, 1);
            a02_w8(o, self + 5, 2);
        }
    } else if (step == 2) {
        if (a02_u8(o, D_0081083F) == 0) {
            a02_w8(o, D_0081083F, 0);
            a02_w8(o, self + 0xB, 0);
            a02_w8(o, self + 5, 0);
        }
    }
    if (a02_u8(o, D_00810761) == 0) {
        uint32_t x = a02_u32(o, self + 0xB0);
        a02_w32(o, S_700038A0, em_ee_add_bits(F_ONE, x));
        uint32_t y = a02_u32(o, self + 0xB4);
        a02_w32(o, S_700038A4, em_ee_add_bits(F_15, y));
        a02_w32(o, S_700038A8, a02_u32(o, self + 0xB8));
        a02_w32(o, S_700038AC, F_ONE);
        a02_w32(o, S_700038B0, 0);
        a02_w32(o, S_700038B4, 0x80);
        a02_w32(o, S_700038B8, 0);
        a02_w32(o, S_700038BC, 0x80);
        (void)a02_c_001F4BF0(o, S_700038A0, S_700038B0);
    }
    (void)a02_c_001C6380(o, self);
    (void)a02_c_001B17A0(o, self);
    a02_callback(o, self);
}

int em_area02_ovl_00825100(const EmArea02OvlHooks *h, uint32_t self, EmArea02OvlFault *fault)
{
    A02Ovl o;
    if (a02_begin(&o, h, fault)) return -1;
    a02_825100(&o, self);
    return a02_end(&o);
}
