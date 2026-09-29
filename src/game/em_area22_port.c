/* AREA22 boot functions and the AREA22 area init (see em_area22_port.h,
 * docs/AREA22_PORT.md).
 *
 * Each function below is a translation of the original code at the named
 * address. Calls, their arguments and the memory accesses between each two
 * calls follow the original instructions: the test compares memory at every
 * call entry and after the last store, and the memory accesses between
 * calls one for one, in order, by address and size (docs/AREA22_PORT.md
 * section 3). Where the original loads the operands of one expression in a
 * set order, the translation loads them in separate statements in that
 * order, because C leaves the order of evaluation of operands open. Float
 * arithmetic is the EE model (em_ee_float.h) on bit patterns, with the
 * original's operand order for each add/sub/mul/div/compare; a float that
 * the original only moves (MOV.S, LWC1/SWC1, MTC1) is carried as its bits.
 */
#include "em_area22_port_internal.h"

/* Original globals the functions name. */
#define D_00275C1C 0x00275C1Cu /* record base word (001E9580 / 001E9E60 index it) */
#define D_00275C24 0x00275C24u
#define D_00275C28 0x00275C28u
#define D_00275C2C 0x00275C2Cu /* record count of 001E7780's closing loop */
#define D_00810360 0x00810360u /* player block + 0xB0 */
#define D_00810364 0x00810364u
#define D_00810368 0x00810368u
#define D_00810374 0x00810374u /* player block + 0xC4 (yaw) */
#define D_008105D0 0x008105D0u /* camera eye (vec4) */
#define D_008105E0 0x008105E0u /* camera target (vec4) */
#define D_00810702 0x00810702u

/* The AREA22 overlay's .bss base: the first byte after the loaded file
 * (runtime 0x823500 + file size 0x900). */
#define D_OVL22_00823E00 0x00823E00u

/* Scratchpad. */
#define S_70003B40 0x70003B40u
#define S_70003B50 0x70003B50u
#define S_700038A0 0x700038A0u
#define S_700038A4 0x700038A4u
#define S_700038A8 0x700038A8u
#define S_700038AC 0x700038ACu

/* Float constants (bit patterns, as the original materializes them). */
#define F_ZERO   0x00000000u
#define F_HALF   0x3F000000u /* 0.5 */
#define F_ONE    0x3F800000u
#define F_FIVE   0x40A00000u
#define F_SIX    0x40C00000u
#define F_FIFTEEN 0x41700000u
#define F_PI     0x40490FDBu /* 3.1415927 */
#define F_180    0x43340000u
#define F_0_1    0x3DCCCCCDu
#define F_0_2    0x3E4CCCCDu
#define F_0_3    0x3E99999Au
#define F_0_4    0x3ECCCCCDu
#define F_42     0x42280000u
#define F_176_9  0x4330E666u /* the height test of 001963A0 */
#define F_266    0x43850000u
/* 001963A0's fixed poses (+0x10..+0x1C, and +0x20..+0x28 for action 9). */
#define F_115    0x42E60000u
#define F_114    0x42E40000u
#define F_173_7  0x432DB333u
#define F_193_4  0x43416666u
#define F_230_3  0x43664CCDu
#define F_246_3  0x43764CCDu
#define F_254    0x437E0000u

static int a22_begin(A22 *o, const EmArea22PortHooks *h, EmArea22PortFault *fault)
{
    if (!h || !fault) return -1;
    a22_open(o, h, fault);
    return a22_failed(o) ? -1 : 0;
}

static int a22_end(const A22 *o) { return a22_failed(o) ? -1 : 0; }

static inline float fb(uint32_t bits) { return a22_float(bits); }

/* ------------------------------------------------------------------------
 * 00100110 (boot, C byte-identical). Calls 001274B0 with the four argument
 * registers as it received them (the double compare) and returns 1 when
 * that function's whole 64-bit result is above 0 (a signed compare), else 0.
 * ---------------------------------------------------------------------- */
int em_area22_port_00100110(const EmArea22PortHooks *h, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                            int32_t *result, EmArea22PortFault *fault)
{
    A22 o;
    if (!result || a22_begin(&o, h, fault)) return -1;
    uint64_t v = 0;
    if (a22_c_001274B0(&o, a0, a1, a2, a3, &v)) return -1;
    *result = (int64_t)v > 0;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001028E8 (boot, inline asm in the decomp; from the instructions). One
 * VU0 macro multiply: out = a * b lane by lane (x, y, z, w). The three
 * quadword addresses are aligned down to 16 (the EE ignores their low four
 * bits). The four words of a are read, then the four of b, then the four
 * of out are written, each in lane order.
 * ---------------------------------------------------------------------- */
static void a22_1028E8(A22 *o, uint32_t out, uint32_t a, uint32_t b)
{
    uint32_t s[4], t[4], d[4] = { 0, 0, 0, 0 };
    a &= ~15u;
    b &= ~15u;
    out &= ~15u;
    for (unsigned k = 0; k < 4; k++) s[k] = a22_u32(o, a + 4 * k);
    for (unsigned k = 0; k < 4; k++) t[k] = a22_u32(o, b + 4 * k);
    if (a22_failed(o)) return;
    if (em_vu_vec_bits(EM_VU_MUL, 0xF, EM_VU_NO_BC, s, t, 0, NULL, d) != EM_EE_FLOAT_OK) {
        a22_latch(o, 0x001028E8u, EM_AREA22_PORT_FAULT_FLOAT_FORM);
        return;
    }
    for (unsigned k = 0; k < 4; k++) a22_w32(o, out + 4 * k, d[k]);
}

int em_area22_port_001028E8(const EmArea22PortHooks *h, uint32_t out, uint32_t a, uint32_t b,
                            EmArea22PortFault *fault)
{
    A22 o;
    if (a22_begin(&o, h, fault)) return -1;
    a22_1028E8(&o, out, a, b);
    return a22_end(&o);
}

/* ------------------------------------------------------------------------
 * 0012D940 (boot, C byte-identical): the step the 0012A5D0 bug nodes run
 * with +5 = 10 or 11 (self, sub = self + 0x1F0). v = 001C2770(self, sub, 6)
 * first; then by the state byte +6:
 *  0  heading +0xC4 = D_00810374, or pi + D_00810374 when (sub halfword
 *     +0xF6 & 7) >= 4 (stored before the draw); then +0xC4 (read again) +=
 *     pi ((rand & 0x1F) - 0x10) / 180; +0xC4 = 001B1470(+0xC4);
 *     0012E070(sub); sub +0xD8 = 0.5; 001287F0(self, sub, 0x1B, 0);
 *     sub +0xE4 = 0x400; +6 + 1; sub +0xF0, +0x70, +0x74, +0x80, +0x88 = 0,
 *     +0x78 = +0x84 = 1.0; +0 = 1.
 *  1  +0xB0 += +0x38 sin(+0xC4) (0011E2A8), +0xB8 += +0x38 cos(+0xC4)
 *     (0011DE90), 001B5360(self). sub +0xE4 == 0x100: +0xC0 = 0, sub
 *     +0xD8 = 0, sub halfword +0xF4 = 0, then by (rand & 0xC0) >> 6: 0 clip
 *     0x11 and +6 + 1; 1 clip 0x1E and +6 = 3; 2 clip 0x1A, +0 = +4 = 2,
 *     +5 = 4, +6 = 0; 3 clip 0x12 and +6 = 3 (clips through 001287F0 with
 *     0.0). Else, with sub +0xE4 & 0xF and 00128640(self) == 0: +5 = 1,
 *     +6 = +7 = 0.
 *  2  with sub halfword +0xF4 bit 0x1000: 00128830(self, 0, 0, 1.0),
 *     001287F0 clips 0 (0.0) and 1 (6.0), +0xC4 = 001B1470(pi + +0xC4),
 *     then 00128640(self) == 0: +5 = 1, +6 = +7 = 0.
 *  3  as 2 without the heading turn.
 * Other states do nothing. At the end, v == 0 (the whole register):
 * 001C3D60(self, sub).
 * ---------------------------------------------------------------------- */
static void a22_12D940_land(A22 *o, uint32_t self, uint32_t sub, int turn)
{
    if (!(a22_u16(o, sub + 0xF4) & 0x1000)) return;
    if (a22_c_00128830(o, self, fb(F_ZERO), fb(F_ZERO), fb(F_ONE))) return;
    if (a22_c_001287F0(o, self, sub, 0, fb(F_ZERO))) return;
    if (a22_c_001287F0(o, self, sub, 1, fb(F_SIX))) return;
    if (turn) {
        uint32_t heading = a22_u32(o, self + 0xC4);
        float wrapped = 0.0f;
        if (a22_c_001B1470(o, fb(em_ee_add_bits(F_PI, heading)), &wrapped)) return;
        a22_w32(o, self + 0xC4, a22_bits(wrapped));
    }
    int32_t r = 0;
    if (a22_c_00128640(o, self, &r)) return;
    if (r == 0) {
        a22_w8(o, self + 5, 1);
        a22_w8(o, self + 6, 0);
        a22_w8(o, self + 7, 0);
    }
}

/* One 001287F0 clip with 0.0, the case of a (rand & 0xC0) >> 6 pick. */
static int a22_clip(A22 *o, uint32_t self, uint32_t sub, int32_t clip)
{
    return a22_c_001287F0(o, self, sub, clip, fb(F_ZERO));
}

static void a22_12D940(A22 *o, uint32_t self, uint32_t sub)
{
    int32_t chk = 0, r = 0;
    if (a22_c_001C2770(o, self, sub, 6, &chk)) return;
    switch (a22_u8(o, self + 6)) {
    case 0: {
        float wrapped = 0.0f;
        if ((a22_u16(o, sub + 0xF6) & 7) >= 4) {
            uint32_t yaw = a22_u32(o, D_00810374);
            a22_w32(o, self + 0xC4, em_ee_add_bits(F_PI, yaw));
        } else {
            a22_w32(o, self + 0xC4, a22_u32(o, D_00810374));
        }
        if (a22_c_00122BB8(o, &r)) return;
        uint32_t heading = a22_u32(o, self + 0xC4);
        uint32_t turn = em_ee_mul_bits(F_PI, em_ee_cvt_s_w_bits((uint32_t)((r & 0x1F) - 0x10)));
        turn = em_ee_div_bits(turn, F_180);
        a22_w32(o, self + 0xC4, em_ee_add_bits(heading, turn));
        if (a22_c_001B1470(o, fb(a22_u32(o, self + 0xC4)), &wrapped)) return;
        a22_w32(o, self + 0xC4, a22_bits(wrapped));
        if (a22_c_0012E070(o, sub)) return;
        a22_w32(o, sub + 0xD8, F_HALF);
        if (a22_c_001287F0(o, self, sub, 0x1B, fb(F_ZERO))) return;
        a22_w32(o, sub + 0xE4, 0x400);
        a22_w8(o, self + 6, a22_u8(o, self + 6) + 1);
        a22_w32(o, sub + 0xF0, 0);
        a22_w32(o, sub + 0x70, 0);
        a22_w32(o, sub + 0x74, 0);
        a22_w32(o, sub + 0x78, F_ONE);
        a22_w32(o, sub + 0x80, 0);
        a22_w32(o, sub + 0x84, F_ONE);
        a22_w32(o, sub + 0x88, 0);
        a22_w8(o, self + 0, 1);
        break;
    }
    case 1: {
        float trig = 0.0f;
        if (a22_c_0011E2A8(o, fb(a22_u32(o, self + 0xC4)), &trig)) return;
        uint32_t speed = a22_u32(o, self + 0x38);
        uint32_t x = a22_u32(o, self + 0xB0);
        a22_w32(o, self + 0xB0, em_ee_add_bits(x, em_ee_mul_bits(speed, a22_bits(trig))));
        if (a22_c_0011DE90(o, fb(a22_u32(o, self + 0xC4)), &trig)) return;
        speed = a22_u32(o, self + 0x38);
        uint32_t z = a22_u32(o, self + 0xB8);
        a22_w32(o, self + 0xB8, em_ee_add_bits(z, em_ee_mul_bits(speed, a22_bits(trig))));
        if (a22_c_001B5360(o, self)) return;
        uint32_t e4 = a22_u32(o, sub + 0xE4);
        if (e4 == 0x100) {
            a22_w32(o, self + 0xC0, 0);
            a22_w32(o, sub + 0xD8, 0);
            a22_w16(o, sub + 0xF4, 0);
            if (a22_c_00122BB8(o, &r)) return;
            switch ((r & 0xC0) >> 6) {
            case 0:
                if (a22_clip(o, self, sub, 0x11)) return;
                a22_w8(o, self + 6, a22_u8(o, self + 6) + 1);
                break;
            case 1:
                if (a22_clip(o, self, sub, 0x1E)) return;
                a22_w8(o, self + 6, 3);
                break;
            case 2:
                if (a22_clip(o, self, sub, 0x1A)) return;
                a22_w8(o, self + 0, 2);
                a22_w8(o, self + 4, 2);
                a22_w8(o, self + 5, 4);
                a22_w8(o, self + 6, 0);
                break;
            default: /* 3 */
                if (a22_clip(o, self, sub, 0x12)) return;
                a22_w8(o, self + 6, 3);
                break;
            }
        } else if (e4 & 0xF) {
            if (a22_c_00128640(o, self, &r)) return;
            if (r == 0) {
                a22_w8(o, self + 5, 1);
                a22_w8(o, self + 6, 0);
                a22_w8(o, self + 7, 0);
            }
        }
        break;
    }
    case 2:
        a22_12D940_land(o, self, sub, 1);
        break;
    case 3:
        a22_12D940_land(o, self, sub, 0);
        break;
    default:
        break;
    }
    if (a22_failed(o)) return;
    if (chk == 0) a22_c_001C3D60(o, self, sub);
}

int em_area22_port_0012D940(const EmArea22PortHooks *h, uint32_t self, uint32_t sub, EmArea22PortFault *fault)
{
    A22 o;
    if (a22_begin(&o, h, fault)) return -1;
    a22_12D940(&o, self, sub);
    return a22_end(&o);
}

/* ------------------------------------------------------------------------
 * 00183010 (boot, C byte-identical): actor +0xA0 += d, +0xB0 += d and the
 * scratchpad vector 0x70003B40 += d (001028B8, each in place), then
 * 0x70003B50 = actor +0xC0 (00102948). No memory access of its own.
 * ---------------------------------------------------------------------- */
int em_area22_port_00183010(const EmArea22PortHooks *h, uint32_t actor, uint32_t delta, EmArea22PortFault *fault)
{
    A22 o;
    if (a22_begin(&o, h, fault)) return -1;
    if (a22_c_001028B8(&o, actor + 0xA0, actor + 0xA0, delta)) return -1;
    if (a22_c_001028B8(&o, actor + 0xB0, actor + 0xB0, delta)) return -1;
    if (a22_c_001028B8(&o, S_70003B40, S_70003B40, delta)) return -1;
    if (a22_c_00102948(&o, S_70003B50, actor + 0xC0)) return -1;
    return 0;
}

/* ------------------------------------------------------------------------
 * The chase step shared by 0018C850 (axis +4) and 0018C920 (axes +0 and
 * +8): d = goal - cur, a = 0011DF78(d) (fabs). a <= step: the axis takes
 * the goal (the caller stores it) and the step reports a snap. Else
 * v = a * (0.5 * step), v = step unless step < v, v negated when d < 0,
 * and the axis (read again) += v. Returns 1 on a snap (the caller stores
 * the goal: 0018C850 the target it holds, 0018C920 goal's axis read
 * again), 0 on a step, -1 on a fault.
 * ---------------------------------------------------------------------- */
static int a22_chase_step(A22 *o, uint32_t axis, uint32_t d, uint32_t step)
{
    float a = 0.0f;
    if (a22_c_0011DF78(o, fb(d), &a)) return -1;
    if (em_ee_c_le_bits(a22_bits(a), step)) return 1;
    uint32_t v = em_ee_mul_bits(a22_bits(a), em_ee_mul_bits(F_HALF, step));
    if (!em_ee_c_lt_bits(step, v)) v = step;
    if (em_ee_c_lt_bits(d, F_ZERO)) v = em_ee_neg_bits(v);
    uint32_t cur = a22_u32(o, axis);
    a22_w32(o, axis, em_ee_add_bits(cur, v));
    return 0;
}

/* ------------------------------------------------------------------------
 * 0018C850 (boot, word asm in the decomp; from the instructions): the y
 * chase. d = target - vec +4; a snap stores target at vec +4 and returns
 * 4, a step returns 0.
 * ---------------------------------------------------------------------- */
static int32_t a22_18C850(A22 *o, uint32_t vec, uint32_t target, uint32_t step)
{
    uint32_t cur = a22_u32(o, vec + 4);
    int snap = a22_chase_step(o, vec + 4, em_ee_sub_bits(target, cur), step);
    if (snap < 0) return -1;
    if (!snap) return 0;
    a22_w32(o, vec + 4, target);
    return 4;
}

int em_area22_port_0018C850(const EmArea22PortHooks *h, uint32_t vec, float target, float step, int32_t *result,
                            EmArea22PortFault *fault)
{
    A22 o;
    if (!result || a22_begin(&o, h, fault)) return -1;
    int32_t r = a22_18C850(&o, vec, a22_bits(target), a22_bits(step));
    if (a22_end(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0018C920 (boot, C byte-identical): the x / z chase of vec toward goal,
 * x (+0) then z (+8). A snap reads goal's axis again and stores it into
 * vec's; the result has bit 0 for an x snap and bit 1 for a z snap.
 * ---------------------------------------------------------------------- */
static int32_t a22_18C920(A22 *o, uint32_t goal, uint32_t vec, uint32_t step)
{
    int32_t flags = 0;
    for (uint32_t k = 0; k <= 8; k += 8) {
        uint32_t g = a22_u32(o, goal + k);
        uint32_t cur = a22_u32(o, vec + k);
        int snap = a22_chase_step(o, vec + k, em_ee_sub_bits(g, cur), step);
        if (snap < 0) return -1;
        if (snap) {
            a22_w32(o, vec + k, a22_u32(o, goal + k));
            flags |= k ? 2 : 1;
        }
    }
    return flags;
}

int em_area22_port_0018C920(const EmArea22PortHooks *h, uint32_t goal, uint32_t vec, float step, int32_t *result,
                            EmArea22PortFault *fault)
{
    A22 o;
    if (!result || a22_begin(&o, h, fault)) return -1;
    int32_t r = a22_18C920(&o, goal, vec, a22_bits(step));
    if (a22_end(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001963A0 (boot, NEARMISS C; from the instructions): camera action 12.
 * cam = the camera block, e = the player block.
 *
 * By cam +1 (the eye step):
 *  0  halfword +8 = 0, +1 (read) + 1, +2 = 0; then, by e +0xA4 <= 176.9:
 *     pose B (+0x10..+0x1C = 114, 173.7, 193.4, 1) and +1 = 1; else pose A
 *     (115, 266, 230.3, 1) and +1 = 3.
 *  1  0018C920(+0x10, eye, 0.2), 0018C850(eye, +0x14, 0.2); then above
 *     176.9: pose A, the raise (below), eye = +0x10 (00102948), +1 = 4.
 *  2  eye = +0x10 (00102948); then above 176.9 as in 1.
 *  3  the raise, 0018C920(+0x10, eye, 0.2), 0018C850(eye, +0x14, 0.2);
 *     at or below 176.9: pose B, +1 = 2.
 *  4  the raise, 0018C850(eye, +0x14, 0.2); at or below 176.9: pose B,
 *     +1 = 2.
 *  (the raise: +0x14 = +0x14 + ((42 + e +0xA4) - +0x14), and 266 when that
 *  is not <= 266.) Other values: nothing.
 * Then by e +0x230 (the player's action word):
 *  8  0018C920(e +0xA0, +0x20, 0.1);   6, 7  0018C6A0(e +0xB0, +0x20, 0.4);
 *  9  above 176.9: D_00810702 = 3, +0x20..+0x28 = (115, 254, 246.3),
 *     0018C6A0(+0x20, target, 0.3), 0018C4B0(target, +0x24, 0.3),
 *     0018D7B0(cam, 5) and return; else D_00810702 = 5 and
 *     0018C6A0(e +0xB0, +0x20, 0.4);
 *  other: +5 = 1 and halfword +0xA0 = 0x28 above 176.9, else +5 = 0; +6 =
 *     0, +1 = 0; 0018C920(e +0xA0, +0x20, 0.1).
 * and then 0018C4B0(+0x20, e +0xB4 + +0x8C, 0.3), 0018C0C0(cam) and
 * 0018D7B0(cam, 5). eye = D_008105D0, target = D_008105E0.
 * ---------------------------------------------------------------------- */
static void a22_pose(A22 *o, uint32_t cam, int high)
{
    a22_w32(o, cam + 0x10, high ? F_115 : F_114);
    a22_w32(o, cam + 0x14, high ? F_266 : F_173_7);
    a22_w32(o, cam + 0x18, high ? F_230_3 : F_193_4);
    a22_w32(o, cam + 0x1C, F_ONE);
}

/* e +0xA4 <= 176.9 (read here). */
static int a22_low(A22 *o, uint32_t e)
{
    return em_ee_c_le_bits(a22_u32(o, e + 0xA4), F_176_9);
}

static void a22_raise(A22 *o, uint32_t cam, uint32_t e)
{
    uint32_t y = a22_u32(o, e + 0xA4);
    uint32_t cur = a22_u32(o, cam + 0x14);
    uint32_t v = em_ee_add_bits(cur, em_ee_sub_bits(em_ee_add_bits(F_42, y), cur));
    a22_w32(o, cam + 0x14, v);
    if (!em_ee_c_le_bits(v, F_266)) a22_w32(o, cam + 0x14, F_266);
}

/* Steps 1 / 2 above 176.9: pose A, the raise, eye = +0x10, +1 = 4. */
static void a22_to_high(A22 *o, uint32_t cam, uint32_t e)
{
    a22_pose(o, cam, 1);
    a22_raise(o, cam, e);
    if (a22_c_00102948(o, D_008105D0, cam + 0x10)) return;
    a22_w8(o, cam + 1, 4);
}

static void a22_1963A0(A22 *o, uint32_t cam, uint32_t e)
{
    switch (a22_u8(o, cam + 1)) {
    case 0: {
        a22_w16(o, cam + 8, 0);
        a22_w8(o, cam + 1, a22_u8(o, cam + 1) + 1);
        a22_w8(o, cam + 2, 0);
        int low = a22_low(o, e);
        a22_pose(o, cam, !low);
        a22_w8(o, cam + 1, low ? 1 : 3);
        break;
    }
    case 1:
        a22_18C920(o, cam + 0x10, D_008105D0, F_0_2);
        if (a22_failed(o)) return;
        a22_18C850(o, D_008105D0, a22_u32(o, cam + 0x14), F_0_2);
        if (a22_failed(o)) return;
        if (!a22_low(o, e)) a22_to_high(o, cam, e);
        break;
    case 2:
        if (a22_c_00102948(o, D_008105D0, cam + 0x10)) return;
        if (!a22_low(o, e)) a22_to_high(o, cam, e);
        break;
    case 3:
        a22_raise(o, cam, e);
        a22_18C920(o, cam + 0x10, D_008105D0, F_0_2);
        if (a22_failed(o)) return;
        a22_18C850(o, D_008105D0, a22_u32(o, cam + 0x14), F_0_2);
        if (a22_failed(o)) return;
        if (a22_low(o, e)) {
            a22_pose(o, cam, 0);
            a22_w8(o, cam + 1, 2);
        }
        break;
    case 4:
        a22_raise(o, cam, e);
        a22_18C850(o, D_008105D0, a22_u32(o, cam + 0x14), F_0_2);
        if (a22_failed(o)) return;
        if (a22_low(o, e)) {
            a22_pose(o, cam, 0);
            a22_w8(o, cam + 1, 2);
        }
        break;
    default:
        break;
    }
    if (a22_failed(o)) return;

    switch (a22_u32(o, e + 0x230)) {
    case 8:
        a22_18C920(o, e + 0xA0, cam + 0x20, F_0_1);
        break;
    case 7:
    case 6:
        a22_c_0018C6A0(o, e + 0xB0, cam + 0x20, fb(F_0_4));
        break;
    case 9:
        if (!a22_low(o, e)) {
            a22_w8(o, D_00810702, 3);
            a22_w32(o, cam + 0x20, F_115);
            a22_w32(o, cam + 0x24, F_254);
            a22_w32(o, cam + 0x28, F_246_3);
            if (a22_c_0018C6A0(o, cam + 0x20, D_008105E0, fb(F_0_3))) return;
            if (a22_c_0018C4B0(o, D_008105E0, fb(a22_u32(o, cam + 0x24)), fb(F_0_3))) return;
            a22_c_0018D7B0(o, cam, 5);
            return;
        }
        a22_w8(o, D_00810702, 5);
        a22_c_0018C6A0(o, e + 0xB0, cam + 0x20, fb(F_0_4));
        break;
    default:
        if (!a22_low(o, e)) {
            a22_w8(o, cam + 5, 1);
            a22_w16(o, cam + 0xA0, 0x28);
        } else {
            a22_w8(o, cam + 5, 0);
        }
        a22_w8(o, cam + 6, 0);
        a22_w8(o, cam + 1, 0);
        a22_18C920(o, e + 0xA0, cam + 0x20, F_0_1);
        break;
    }
    if (a22_failed(o)) return;
    uint32_t band = a22_u32(o, e + 0xB4);
    uint32_t lift = a22_u32(o, cam + 0x8C);
    if (a22_c_0018C4B0(o, cam + 0x20, fb(em_ee_add_bits(band, lift)), fb(F_0_3))) return;
    if (a22_c_0018C0C0(o, cam)) return;
    a22_c_0018D7B0(o, cam, 5);
}

int em_area22_port_001963A0(const EmArea22PortHooks *h, uint32_t cam, uint32_t player, EmArea22PortFault *fault)
{
    A22 o;
    if (a22_begin(&o, h, fault)) return -1;
    a22_1963A0(&o, cam, player);
    return a22_end(&o);
}

/* ------------------------------------------------------------------------
 * 001BB310 (boot, C byte-identical): the op09 callback of the door program
 * 0x24DA40 (record 0x24DA80), called with the door. p = door +0x1C;
 * 0x700038A0 = (p +0xB0, p +0xB4, p +0xB8, 1.0); target = that (00102948);
 * 0x700038A0 = player x - 15 sin(yaw) (0011E2A8), then +0x38A4 = 5 + player
 * y, +0x38A8 = player z - 15 cos(yaw) (0011DE90); eye = that (00102948).
 * Returns 1. (yaw = D_00810374, the player block's +0xC4.)
 * ---------------------------------------------------------------------- */
static void a22_1BB310(A22 *o, uint32_t actor)
{
    uint32_t p = a22_u32(o, actor + 0x1C);
    a22_w32(o, S_700038A0, a22_u32(o, p + 0xB0));
    a22_w32(o, S_700038A4, a22_u32(o, p + 0xB4));
    a22_w32(o, S_700038A8, a22_u32(o, p + 0xB8));
    a22_w32(o, S_700038AC, F_ONE);
    if (a22_c_00102948(o, D_008105E0, S_700038A0)) return;
    float s = 0.0f, c = 0.0f;
    if (a22_c_0011E2A8(o, fb(a22_u32(o, D_00810374)), &s)) return;
    uint32_t x = a22_u32(o, D_00810360);
    uint32_t y = a22_u32(o, D_00810364);
    uint32_t yaw = a22_u32(o, D_00810374);
    a22_w32(o, S_700038A0, em_ee_sub_bits(x, em_ee_mul_bits(F_FIFTEEN, a22_bits(s))));
    a22_w32(o, S_700038A4, em_ee_add_bits(F_FIVE, y));
    if (a22_c_0011DE90(o, fb(yaw), &c)) return;
    uint32_t z = a22_u32(o, D_00810368);
    a22_w32(o, S_700038A8, em_ee_sub_bits(z, em_ee_mul_bits(F_FIFTEEN, a22_bits(c))));
    a22_c_00102948(o, D_008105D0, S_700038A0);
}

int em_area22_port_001BB310(const EmArea22PortHooks *h, uint32_t actor, int32_t *result, EmArea22PortFault *fault)
{
    A22 o;
    if (!result || a22_begin(&o, h, fault)) return -1;
    a22_1BB310(&o, actor);
    if (a22_end(&o)) return -1;
    *result = 1;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x823580 (AREA22 overlay, link overlay_AREA22_func_00823540, C
 * byte-identical): the AREA22 area init 001E7780 calls for key 0x1600
 * (its two arguments are not read). Stores, in this order: D_00275C28 =
 * 0x20, D_00275C2C = 0, D_00275C24 = 0, D_00275C1C = 0x823E00 (the
 * overlay's .bss base).
 * ---------------------------------------------------------------------- */
int em_area22_port_00823580(const EmArea22PortHooks *h, EmArea22PortFault *fault)
{
    A22 o;
    if (a22_begin(&o, h, fault)) return -1;
    a22_w32(&o, D_00275C28, 0x20);
    a22_w32(&o, D_00275C2C, 0);
    a22_w32(&o, D_00275C24, 0);
    a22_w32(&o, D_00275C1C, D_OVL22_00823E00);
    return a22_end(&o);
}
