/* AREA04 owners and two boot functions (see em_area04_port.h,
 * docs/AREA04_PORT.md).
 *
 * Each function below is a translation of the original code at the named
 * runtime address. The 17 overlay functions follow the decomp's
 * byte-identical C (src/overlays/AREA04/<name>.c, link name = runtime -
 * 0x40); 001BE5F0 follows the original instructions (the decomp holds it as
 * inline asm) and 001C1A80 follows the decomp's NEARMISS C checked against
 * the original instructions. Calls, their arguments and the memory
 * accesses between each two calls follow the original: the test compares
 * memory at every call entry and after the last store, and the memory
 * accesses between calls one for one, in order, by address and size
 * (docs/AREA04_PORT.md section 3). Where the original loads the operands of
 * one expression in a set order, the translation loads them in separate
 * statements in that order, because C leaves the order of evaluation of
 * arguments and operands open. A 16-byte load or store of the original is
 * two 8-byte accesses, low half first (the order the test's EE core
 * performs them). Float arithmetic is the EE model (em_ee_float.h) on bit
 * patterns; the operand order of each add/sub/mul/madd/compare is the
 * original's.
 */
#include "em_area04_port_internal.h"

/* Original globals the functions name. */
#define D_00275928 0x00275928u /* descriptor (0x825880 stores its address) */
#define D_00275660 0x00275660u /* descriptor (001C1A80 stores its address) */
#define D_00275B40 0x00275B40u /* word: a pointer to the object pointer */
#define D_0024F8F0 0x0024F8F0u
#define D_0028A508 0x0028A508u
#define D_0028A59C 0x0028A59Cu /* model table word */
#define D_0028A5D8 0x0028A5D8u
#define D_0028A6F8 0x0028A6F8u
#define D_008102B0 0x008102B0u /* the player block */
#define D_00810350 0x00810350u /* player x (= block + 0xA0) */
#define D_00810354 0x00810354u /* player y */
#define D_00810358 0x00810358u /* player z */
#define D_00810360 0x00810360u /* block + 0xB0 */
#define D_00810700 0x00810700u
#define D_00810701 0x00810701u
#define D_00810702 0x00810702u
#define D_00810764 0x00810764u
#define D_0081076A 0x0081076Au
#define D_0081076C 0x0081076Cu
#define D_008107E4 0x008107E4u /* AREA04 counter 0xC (the director) */
#define D_008107E5 0x008107E5u
#define D_008107E9 0x008107E9u
#define D_008107EA 0x008107EAu
#define D_00810834 0x00810834u
#define D_0081083B 0x0081083Bu
#define D_0081083D 0x0081083Du
#define D_00810841 0x00810841u /* lock bytes, indexed by D_00810700 */
#define D_00810845 0x00810845u /* D_00810841[4] */

/* Scratchpad. */
#define S_700038A0 0x700038A0u /* vector */
#define S_700038A4 0x700038A4u
#define S_700038A8 0x700038A8u
#define S_700038AC 0x700038ACu
#define S_700038B0 0x700038B0u /* colour words */
#define S_700038B4 0x700038B4u
#define S_700038B8 0x700038B8u
#define S_700038BC 0x700038BCu
#define S_70003B84 0x70003B84u /* halfword */
#define S_70003B8D 0x70003B8Du /* byte */

/* Overlay data (runtime addresses). */
#define A04_GROUP_826600 0x00826600u
#define A04_REC_826790 0x00826790u
#define A04_REC_8267F0 0x008267F0u
#define A04_GROUP_8268D0 0x008268D0u
#define A04_REC_827230 0x00827230u
#define A04_SCRIPT_8272A0 0x008272A0u
#define A04_KINDS_827530 0x00827530u /* 8-byte records: first kind */
#define A04_KINDS_827534 0x00827534u /* second kind */
#define A04_SCRIPT_8275A0 0x008275A0u
#define A04_SCRIPT_8278D0 0x008278D0u
#define A04_QUAD_827CD0 0x00827CD0u
#define A04_SCRIPT_827D10 0x00827D10u
#define A04_SCRIPT_827D90 0x00827D90u
#define A04_AREA_828220 0x00828220u /* 64 bytes, copied to 0x824490's frame */
#define A04_SCRIPT_828450 0x00828450u
#define A04_SCRIPT_8286D0 0x008286D0u
#define A04_POINT_828B90 0x00828B90u
#define A04_SCRIPT_828BE0 0x00828BE0u
#define A04_SCRIPT_82C1F0 0x0082C1F0u
#define A04_SCRIPT_82C3B0 0x0082C3B0u
#define A04_DATA_82C630 0x0082C630u
#define A04_DATA_82C640 0x0082C640u
#define A04_SCRIPT_82C650 0x0082C650u
#define A04_WORD_82C724 0x0082C724u

/* Float constants (bit patterns the original materialises). */
#define F_ZERO 0x00000000u
#define F_ONE 0x3F800000u
#define F_HALF 0x3F000000u
#define F_QUARTER 0x3E800000u
#define F_6 0x40C00000u
#define F_10 0x41200000u
#define F_16 0x41800000u
#define F_19 0x41980000u
#define F_20 0x41A00000u
#define F_300 0x43960000u
#define F_PI 0x40490FDBu
#define F_HALF_PI 0x3FC90FDBu
#define F_2POW_M31 0x30000000u     /* 2^-31 */
#define F_MINUS_0_7 0xBF333333u
#define F_0_7 0x3F333333u
#define F_0_1 0x3DCCCCCDu
#define F_0_2 0x3E4CCCCDu
#define F_0_04 0x3D23D70Au
#define F_0_34 0x3EAE147Bu
#define F_14_9 0x416E6666u
#define F_380 0x43BE0000u
#define F_440_1 0x43DC0CCDu
#define F_356_4 0x43B23333u
#define F_440_4 0x43DC3333u
#define F_114_4 0x42E4CCCDu
#define F_LIFT_STEP 0x3E44EC4Fu    /* 0.1923077 */
#define F_REEL_DX 0x3E372DC4u      /* 0.17888552 */
#define F_REEL_DY 0x3DB72DA9u      /* 0.08944256 */
#define F_REEL_ROLL 0x3CBDE8E2u    /* 0.023182336 */
#define F_499 0x43F98000u
#define F_497 0x43F88000u
#define F_495 0x43F78000u
#define F_428 0x43D60000u
#define F_426 0x43D50000u
#define F_424 0x43D40000u
#define F_395 0x43C58000u

static inline float fb(uint32_t bits) { return em_ee_float(bits); }

static int a04_begin(A04 *o, const EmArea04PortHooks *h, EmArea04PortFault *fault)
{
    if (!h || !fault) return -1;
    a04_open(o, h, fault);
    return a04_failed(o) ? -1 : 0;
}

static int a04_end(const A04 *o) { return a04_failed(o) ? -1 : 0; }

/* The scratchpad vector 0x700038A0..AC, stored in address order. */
static void a04_vec_a0(A04 *o, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    a04_w32(o, S_700038A0, x);
    a04_w32(o, S_700038A4, y);
    a04_w32(o, S_700038A8, z);
    a04_w32(o, S_700038AC, w);
}

/* A byte field + 1 (the original re-reads the byte). */
static void a04_inc8(A04 *o, uint32_t a)
{
    uint32_t v = a04_u8(o, a);
    a04_w8(o, a, v + 1);
}

/* ------------------------------------------------------------------------
 * 0x823580 (link 00823540): the talk turn. Only with +0x0B bit 2: turns the
 * player block's yaw to face this actor (or its back), sets +0x2E, puts the
 * player 6 units in front along the new yaw, starts script 0x8272A0 on
 * `who` and pumps it once; returns 1, else 0.
 * ---------------------------------------------------------------------- */
static int32_t a04_823580(A04 *o, uint32_t self, uint32_t who)
{
    float r = 0.0f;
    int32_t ignored = 0;
    if (!(a04_u8(o, self + 0x0B) & 4)) return 0;
    uint32_t px = a04_u32(o, D_008102B0 + 0xA0);
    uint32_t pz = a04_u32(o, D_008102B0 + 0xA8);
    if (a04_c_001B1240(o, self + 0xB0, fb(px), fb(pz), &r)) return 0;
    uint32_t angle = em_ee_bits(r);
    uint32_t yaw = a04_u32(o, self + 0xC4);
    if (a04_c_001B1470(o, fb(em_ee_sub_bits(angle, yaw)), &r)) return 0;
    if (a04_c_0011DF78(o, r, &r)) return 0;
    if (em_ee_c_le_bits(em_ee_bits(r), F_HALF_PI)) {
        yaw = a04_u32(o, self + 0xC4);
        if (a04_c_001B1470(o, fb(em_ee_add_bits(F_PI, yaw)), &r)) return 0;
        a04_w32(o, D_008102B0 + 0xC4, em_ee_bits(r));
        a04_w16(o, self + 0x2E, 0);
    } else {
        yaw = a04_u32(o, self + 0xC4);
        if (a04_c_001B1470(o, fb(yaw), &r)) return 0;
        a04_w32(o, D_008102B0 + 0xC4, em_ee_bits(r));
        a04_w16(o, self + 0x2E, 1);
    }
    if (a04_u8(o, self + 3) == 0x16) {
        uint32_t side = a04_u16(o, self + 0x2E);
        a04_w16(o, self + 0x2E, 1u - side);
    }
    uint32_t pyaw = a04_u32(o, D_008102B0 + 0xC4);
    if (a04_c_0011E2A8(o, fb(pyaw), &r)) return 0;
    uint32_t sx = a04_u32(o, self + 0xB0);
    a04_w32(o, S_700038A0, em_ee_sub_bits(sx, em_ee_mul_bits(F_6, em_ee_bits(r))));
    uint32_t py = a04_u32(o, D_008102B0 + 0xA4);
    a04_w32(o, S_700038A4, py);
    pyaw = a04_u32(o, D_008102B0 + 0xC4);
    if (a04_c_0011DE90(o, fb(pyaw), &r)) return 0;
    uint32_t sz = a04_u32(o, self + 0xB8);
    a04_w32(o, S_700038A8, em_ee_sub_bits(sz, em_ee_mul_bits(F_6, em_ee_bits(r))));
    a04_w32(o, S_700038AC, F_ONE);
    if (a04_c_00182F90(o, D_008102B0, S_700038A0)) return 0;
    if (a04_c_001BA1A0(o, who, A04_SCRIPT_8272A0)) return 0;
    if (a04_c_001BA1F0(o, self, &ignored)) return 0;
    return 1;
}

int em_area04_port_00823580(const EmArea04PortHooks *h, uint32_t self, uint32_t who, int32_t *result,
                            EmArea04PortFault *fault)
{
    A04 o;
    if (!result || a04_begin(&o, h, fault)) return -1;
    int32_t r = a04_823580(&o, self, who);
    if (a04_end(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x823700 (link 008236C0): door [45]. State 0: 001BB520, +0 = 1. State 1
 * by +5: 0 the lock bit D_00810841[D_00810700] & (1 << +0x34) -> 001BB560
 * (step 2) else the talk turn (step + 1); 1 / 2 001BB7C0; 3 001BC150; 4
 * 001BB7F0. Then 001C6380, the callback, and within 20 of the player
 * +1 = 1 (001B1DE0 when +2 bit 7). States 2 / 3: 001AFC10.
 * ---------------------------------------------------------------------- */
static void a04_823700(A04 *o, uint32_t self)
{
    uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    float d = 0.0f;
    switch (a04_u8(o, self + 4)) {
    case 0:
        if (a04_c_001BB520(o, self, blk)) return;
        a04_w8(o, self + 0, 1);
        break;
    case 1:
        switch (a04_u8(o, self + 5)) {
        case 0: {
            uint32_t index = a04_u8(o, D_00810700);
            uint32_t shift = a04_u16(o, self + 0x34);
            uint32_t lock = a04_u8(o, D_00810841 + index);
            if (lock & (1u << (shift & 31))) {
                if (a04_c_001BB560(o, self, blk, 0, &r)) return;
                if (r != 0) a04_w8(o, self + 5, 2);
            } else {
                r = a04_823580(o, self, blk);
                if (a04_failed(o)) return;
                if (r != 0) a04_inc8(o, self + 5);
            }
            break;
        }
        case 1:
            if (a04_c_001BB7C0(o, self, blk, &r)) return;
            if (r != 0) {
                a04_w8(o, self + 0x0B, 0);
                a04_w8(o, self + 5, 0);
            }
            break;
        case 2:
            if (a04_c_001BB7C0(o, self, blk, &r)) return;
            if (r != 0) a04_inc8(o, self + 5);
            break;
        case 3:
            if (a04_c_001BC150(o, self, blk)) return;
            a04_inc8(o, self + 5);
            break;
        case 4:
            if (a04_c_001BB7F0(o, self, blk, &r)) return;
            if (r != 0) a04_w8(o, self + 5, 0);
            break;
        default:
            break;
        }
        if (a04_c_001C6380(o, self)) return;
        a04_callback(o, self);
        if (a04_failed(o)) return;
        {
            uint32_t px = a04_u32(o, D_00810350);
            uint32_t sx = a04_u32(o, self + 0xB0);
            uint32_t sy = a04_u32(o, self + 0xB4);
            uint32_t sz = a04_u32(o, self + 0xB8);
            uint32_t py = a04_u32(o, D_00810354);
            uint32_t dx = em_ee_sub_bits(px, sx);
            uint32_t pz = a04_u32(o, D_00810358);
            uint32_t dy = em_ee_sub_bits(py, sy);
            uint32_t dz = em_ee_sub_bits(pz, sz);
            uint32_t acc = em_ee_adda_bits(em_ee_mul_bits(dx, dx), em_ee_mul_bits(dy, dy));
            if (a04_c_0011E748(o, fb(em_ee_madd_bits(acc, dz, dz)), &d)) return;
        }
        if (em_ee_c_le_bits(em_ee_bits(d), F_20)) {
            a04_w8(o, self + 1, 1);
            if (a04_u8(o, self + 2) & 0x80) {
                if (a04_c_001B1DE0(o, self)) return;
            }
        }
        break;
    case 2:
    case 3:
        a04_c_001AFC10(o, self);
        break;
    default:
        break;
    }
}

int em_area04_port_00823700(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_823700(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x823B40 (link 00823B00): the script callback. When D_00810845 bit 3 is
 * clear it sets it and calls 001FB9F0(0x3EE, 0x1000, 0x1000, 0x1000).
 * Returns 1.
 * ---------------------------------------------------------------------- */
int em_area04_port_00823B40(const EmArea04PortHooks *h, int32_t *result, EmArea04PortFault *fault)
{
    A04 o;
    if (!result || a04_begin(&o, h, fault)) return -1;
    uint32_t v = a04_u8(&o, D_00810845);
    if (!(v & 8)) {
        a04_w8(&o, D_00810845, v | 8);
        a04_c_001FB9F0(&o, 0x3EE, 0x1000, 0x1000, 0x1000);
    }
    if (a04_end(&o)) return -1;
    *result = 1;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x823B90 (link 00823B50): the director [1].
 * ---------------------------------------------------------------------- */
static void a04_823B90(A04 *o, uint32_t self)
{
    uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    switch (a04_u8(o, self + 4)) {
    case 0:
        if (!(a04_u8(o, D_00810845) & 0x20)) {
            if (a04_c_001BA1C0(o, self, 0xC, &r)) return;
            if (r == 0) {
                if (a04_u8(o, D_008107E4) == 0) {
                    if (a04_c_001BA1A0(o, blk, A04_SCRIPT_8275A0)) return;
                    if (a04_c_001FABB0(o)) return;
                } else {
                    if (a04_c_001BA1A0(o, blk, A04_SCRIPT_8278D0)) return;
                }
                a04_inc8(o, self + 4);
                break;
            }
        }
        a04_w8(o, D_00810764, 0xFF);
        a04_w8(o, self + 4, 3);
        break;
    case 1: {
        uint32_t e4 = a04_u8(o, D_008107E4);
        if (e4 == 2 || e4 == 1) {
            uint32_t step = a04_u8(o, self + 5);
            if (step == 1) {
                if (a04_c_001BA1F0(o, self, &r)) return;
                if (r == 0) break;
                a04_vec_a0(o, F_440_4, F_14_9, F_114_4, F_ONE);
                if (a04_c_001B6F80(o, S_700038A0, fb(F_ZERO))) return;
                a04_w16(o, self + 0x2E, 0xFFFF);
                a04_w8(o, self + 4, 3);
                if (a04_c_001C4760(o, 6, 1)) return;
                a04_c_001FAE70(o, 0);
            } else if (step == 0) {
                if (a04_u8(o, D_00810845) & 0x20) {
                    a04_w8(o, self + 0, 2);
                    a04_w8(o, D_00810764, 0xFF);
                    a04_w8(o, self + 4, 3);
                    break;
                }
                if (!em_ee_c_le_bits(a04_u32(o, D_00810354), F_16)) break;
                if (a04_c_001B1EA0(o, 0, D_00810360, A04_QUAD_827CD0, 4, &r)) return;
                if (r == 0) break;
                a04_inc8(o, self + 5);
                a04_w8(o, D_008107E4, 2);
                if (a04_c_001FABB0(o)) return;
                a04_w16(o, S_70003B84, 0);
            }
        } else if (e4 == 0) {
            if (a04_u8(o, S_70003B8D) != 0) {
                if ((int8_t)a04_u8(o, blk + 0xC) == 1 && a04_u16(o, S_70003B84) == 0xAA) {
                    if (a04_c_001B1E20(o, 2, 0xF0)) return;
                }
            }
            if (a04_c_001BA1F0(o, self, &r)) return;
            if (r == 0) break;
            a04_w16(o, self + 0x2E, 0xFFFF);
            a04_vec_a0(o, F_440_1, F_14_9, F_356_4, F_ONE);
            if (a04_c_001B6F80(o, S_700038A0, fb(F_PI))) return;
            if (a04_c_001BA1A0(o, blk, A04_SCRIPT_8278D0)) return;
            if (a04_c_001B6660(o, A04_GROUP_8268D0, (uint32_t *)&r)) return;
            a04_c_001FB0B0(o, 0xF);
        }
        break;
    }
    case 3:
        a04_c_001AFC10(o, self);
        break;
    default:
        break;
    }
}

int em_area04_port_00823B90(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_823B90(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x823EE0 (link 00823EA0): [47]. The model kind +0x0D comes from the
 * 8-byte records at 0x827530 indexed by D_00810701 (first kind at +0,
 * second at +4). The link (+0x18)->+0x18 is read at entry, in every state.
 * ---------------------------------------------------------------------- */
static void a04_823EE0(A04 *o, uint32_t self)
{
    int32_t r = 0;
    uint32_t model = 0;
    uint32_t first = a04_u32(o, self + 0x18);
    uint32_t state = a04_u8(o, self + 4);
    uint32_t link = a04_u32(o, first + 0x18);
    switch (state) {
    case 0:
        if (a04_u8(o, D_00810764) != 0xFF) {
            uint32_t i = a04_u8(o, D_00810701);
            a04_w8(o, self + 0x0D, a04_u8(o, A04_KINDS_827530 + i * 8));
        }
        if (a04_c_001B0FD0(o, self, &r)) return;
        if (a04_c_001CA5E0(o, self, a04_u32(o, self + 0x44), 1)) return;
        if (a04_u8(o, D_00810764) == 0xFF) a04_w8(o, self + 4, 2);
        break;
    case 1: {
        uint32_t step = a04_u8(o, self + 5);
        if (step == 1) {
            int go = 0;
            if (a04_u16(o, S_70003B84) >= 0x55D) {
                go = 1;
            } else if (a04_u8(o, D_00810764) == 0xFF) {
                uint32_t i = a04_u8(o, D_00810701);
                uint32_t kind = a04_u8(o, self + 0x0D);
                if (kind == a04_u8(o, A04_KINDS_827530 + i * 8)) go = 1;
            }
            if (go) {
                a04_w8(o, self + 4, 2);
                uint32_t i = a04_u8(o, D_00810701);
                a04_w8(o, self + 0x0D, a04_u8(o, A04_KINDS_827534 + i * 8));
                uint32_t table = a04_u32(o, D_0028A59C);
                uint32_t kind = a04_u8(o, self + 0x0D);
                if (a04_c_001C6120(o, table, (int32_t)kind, &model)) return;
                if (a04_c_001CA6E0(o, self, model)) return;
                if (a04_c_001C62C0(o, self)) return;
                if (a04_c_001CA5E0(o, self, a04_u32(o, self + 0x44), 1)) return;
            }
        } else if (step == 0) {
            if (a04_u8(o, D_00810764) == 0xFF) {
                a04_w8(o, self + 4, 2);
            } else if (a04_u8(o, D_008107E4) == 2) {
                a04_w8(o, self + 5, step + 1);
            }
        }
        if (a04_c_001C6380(o, self)) return;
        a04_callback(o, self);
        break;
    }
    case 2: {
        uint32_t bone = a04_u32(o, link + 0x110);
        uint32_t holder = a04_u32(o, D_00275B40);
        uint32_t value = a04_u32(o, bone + 0x7C);
        uint32_t object = a04_u32(o, holder);
        a04_w32(o, object + 0x7C, value);
        if (a04_c_001C6380(o, self)) return;
        a04_callback(o, self);
        break;
    }
    case 3:
        a04_c_001AFC10(o, self);
        break;
    default:
        break;
    }
}

int em_area04_port_00823EE0(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_823EE0(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x824100 (link 008240C0): the group 0x8268D0 node.
 * ---------------------------------------------------------------------- */
static void a04_824100(A04 *o, uint32_t self)
{
    int32_t r = 0;
    switch (a04_u8(o, self + 4)) {
    case 0:
        if (a04_c_001B10B0(o, self, 0x4E, 0x52, &r)) return;
        if (r == 0) {
            if (a04_c_001C63E0(o, self, 0)) return;
            a04_inc8(o, self + 4);
        }
        break;
    case 1:
        if (a04_c_001C64F0(o, self, fb(F_ONE), &r)) return;
        if (a04_c_001C68C0(o, self)) return;
        if (a04_c_001B17A0(o, self, &r)) return;
        if (r != 0) {
            a04_callback(o, self);
            if (a04_failed(o)) return;
        }
        if (a04_u8(o, D_008107E4) == 2) a04_w8(o, self + 4, 3);
        break;
    case 2:
    case 3:
        a04_c_001AFC10(o, self);
        break;
    default:
        break;
    }
}

int em_area04_port_00824100(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_824100(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8241F0 (link 008241B0): [61]. Script 0x827D10 once D_0081083D is set.
 * ---------------------------------------------------------------------- */
static void a04_8241F0(A04 *o, uint32_t self)
{
    uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    uint32_t spawned = 0;
    switch (a04_u8(o, self + 4)) {
    case 0:
        if (a04_c_001BA1C0(o, self, 0xD, &r)) return;
        if (r == 0) {
            if (a04_c_001BA1C0(o, self, 0x65, &r)) return;
        }
        if (r != 0) {
            a04_w8(o, self + 4, 3);
        } else {
            a04_w8(o, self + 0, 1);
            a04_w8(o, self + 4, 1);
        }
        break;
    case 1:
        if (a04_u8(o, D_0081083D) == 0) break;
        switch (a04_u8(o, self + 5)) {
        case 0:
            if (a04_c_001BA1A0(o, blk, A04_SCRIPT_827D10)) return;
            a04_w8(o, self + 5, 1);
            break;
        case 1:
            if (a04_c_001BA1F0(o, self, &r)) return;
            if (r != 0) {
                if (a04_c_001B6660(o, A04_GROUP_826600, &spawned)) return;
                a04_w8(o, D_008107E5, 0xFF);
                a04_w8(o, self + 4, 3);
            }
            break;
        default:
            break;
        }
        break;
    case 2:
    case 3:
        a04_c_001AFC10(o, self);
        break;
    default:
        break;
    }
}

int em_area04_port_008241F0(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_8241F0(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x824490 (link 00824450): the NPC [2]'s first sub-state (0x824320 calls
 * it while D_008107E9 is 0). Copies the area 0x828220 to its frame, then by
 * +5: 0 starts script 0x827D90 when 001B1EA0(0, player, area, 4) is 1; 1 at
 * the script's end: +0x2E = 0xFFFF, +5 = 0, +0x40 = D_0028A5D8, 001C67E0,
 * 001C47A0(0x23, 1), 001C4760(8, 1), D_008107E9 = 1, 001FAE70(0); then
 * 001BA580(self, +0x0D) and the clip step (0.5 on step 1, stored at
 * +0x1FE; 1.0 otherwise).
 * ---------------------------------------------------------------------- */
static void a04_824490(A04 *o, uint32_t self, uint32_t sp)
{
    uint32_t blk = self + 0x1F0;
    uint32_t area = sp - 0x40;
    uint64_t lo[4], hi[4];
    int32_t r = 0;
    for (int i = 0; i < 4; i++) {
        lo[i] = a04_u64(o, A04_AREA_828220 + 16u * (uint32_t)i);
        hi[i] = a04_u64(o, A04_AREA_828220 + 16u * (uint32_t)i + 8u);
    }
    for (int i = 0; i < 4; i++) {
        a04_w64(o, area + 16u * (uint32_t)i, lo[i]);
        a04_w64(o, area + 16u * (uint32_t)i + 8u, hi[i]);
    }
    uint32_t step = a04_u8(o, self + 5);
    if (step == 1) {
        if (a04_c_001BA1F0(o, self, &r)) return;
        if (r != 0) {
            a04_w16(o, self + 0x2E, 0xFFFF);
            a04_w8(o, self + 5, 0);
            a04_w32(o, self + 0x40, a04_u32(o, D_0028A5D8));
            if (a04_c_001C67E0(o, self, 0, fb(F_ZERO), fb(F_ZERO))) return;
            if (a04_c_001C47A0(o, 0x23, 1)) return;
            if (a04_c_001C4760(o, 8, 1)) return;
            a04_w8(o, D_008107E9, 1);
            if (a04_c_001FAE70(o, 0)) return;
        }
        if (a04_c_001BA580(o, self, (int32_t)a04_u8(o, self + 0x0D))) return;
        if (a04_c_001C64F0(o, self, fb(F_HALF), &r)) return;
        a04_w16(o, blk + 0xE, (uint32_t)r);
        return;
    }
    if (step == 0) {
        if (a04_c_001B1EA0(o, 0, D_00810350, area, 4, &r)) return;
        if (r == 1) {
            if (a04_c_001BA1A0(o, blk, A04_SCRIPT_827D90)) return;
            a04_w8(o, self + 5, 1);
        }
    }
    if (a04_c_001BA580(o, self, (int32_t)a04_u8(o, self + 0x0D))) return;
    a04_c_001C64F0(o, self, fb(F_ONE), &r);
}

int em_area04_port_00824490(const EmArea04PortHooks *h, uint32_t self, uint32_t sp, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_824490(&o, self, sp);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x824830 (link 008247F0): [3]'s sub-state +5 (0x8246B0 calls it while
 * D_008107EA is 0 or 0x10).
 * ---------------------------------------------------------------------- */
static void a04_824830(A04 *o, uint32_t self)
{
    uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    uint32_t spawned = 0;
    switch (a04_u8(o, self + 5)) {
    case 0:
        if (a04_u8(o, D_008107EA) == 0) {
            a04_w8(o, self + 5, 1);
            a04_c_001BA1A0(o, blk, A04_SCRIPT_828450);
        } else {
            a04_w8(o, self + 5, 2);
            if (a04_c_001B6660(o, A04_REC_826790, &spawned)) return;
            a04_w32(o, self + 0x240, spawned);
            a04_c_001B6660(o, A04_REC_8267F0, &spawned);
        }
        break;
    case 1:
        if (a04_c_001BA1F0(o, self, &r)) return;
        if (r != 0) {
            a04_w16(o, self + 0x2E, 0xFFFF);
            if (a04_c_001B6660(o, A04_REC_826790, &spawned)) return;
            a04_w32(o, self + 0x240, spawned);
            if (a04_c_001B6660(o, A04_REC_8267F0, &spawned)) return;
            a04_w8(o, D_008107EA, 0x10);
            if (a04_c_001FAE70(o, 0)) return;
            a04_w8(o, self + 5, 2);
        }
        break;
    default:
        break;
    }
}

int em_area04_port_00824830(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_824830(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x824930 (link 008248F0): [3]'s sub-state +6 (0x8246B0 calls it while
 * D_008107EA is 1, 2, 0x20 or 0xFF).
 * ---------------------------------------------------------------------- */
static void a04_824930(A04 *o, uint32_t self)
{
    uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    uint32_t spawned = 0;
    switch (a04_u8(o, self + 6)) {
    case 0: {
        uint32_t object = a04_u32(o, self + 0x240);
        if (object != 0) {
            if (a04_c_00121870(o, self + 0x2E0, object + 0xB0, 0x10)) return;
            if (a04_c_001BA1A0(o, blk, A04_SCRIPT_8286D0)) return;
            a04_w8(o, self + 6, 1);
        } else {
            if (a04_c_001B6660(o, A04_REC_827230, &spawned)) return;
            if (a04_c_001EFD20(o, (int32_t)0x8000005Bu, A04_POINT_828B90, &spawned)) return;
            a04_w8(o, D_0081076A, 0xFF);
            a04_w8(o, D_008107EA, 0xFF);
            a04_w8(o, self + 6, 2);
        }
        break;
    }
    case 1:
        if (a04_c_001BA1F0(o, self, &r)) return;
        if (r != 0) a04_w8(o, self + 6, 2);
        break;
    case 2:
        a04_w8(o, D_008107EA, 0xFF);
        a04_w8(o, self + 6, 3);
        break;
    default:
        break;
    }
}

int em_area04_port_00824930(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_824930(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8246B0 (link 00824670): [3]. State 0: 001BA1C0(self, 0x12) -> state 3,
 * else model setup and state 1. State 1 while D_0081076A: by D_008107EA
 * 0 / 0x10 -> 0x824830, 1 / 2 / 0x20 / 0xFF -> 0x824930; then, with
 * D_008107EA (re-read) non-zero, the clip step, 001B17A0, 001C68C0 and the
 * callback.
 * ---------------------------------------------------------------------- */
static void a04_8246B0(A04 *o, uint32_t self)
{
    int32_t r = 0;
    switch (a04_u8(o, self + 4)) {
    case 0:
        if (a04_c_001BA1C0(o, self, 0x12, &r)) return;
        if (r != 0) {
            a04_w8(o, self + 4, 3);
            break;
        }
        if (a04_c_001B10B0(o, self, (int32_t)a04_u8(o, self + 0x0D), 0x6D, &r)) return;
        if (a04_c_001C63E0(o, self, 0)) return;
        a04_w32(o, self + 0x58, a04_u32(o, D_0028A6F8));
        a04_w8(o, self + 4, 1);
        a04_w8(o, self + 0, 1);
        break;
    case 1: {
        if (a04_u8(o, D_0081076A) == 0) break;
        uint32_t ea = a04_u8(o, D_008107EA);
        if (ea == 0xFF || ea == 0x20 || ea == 2 || ea == 1) {
            a04_824930(o, self);
        } else if (ea == 0x10 || ea == 0) {
            a04_824830(o, self);
        }
        if (a04_failed(o)) return;
        if (a04_u8(o, D_008107EA) != 0) {
            if (a04_c_001C64F0(o, self, fb(F_ONE), &r)) return;
            if (a04_c_001B17A0(o, self, &r)) return;
            if (a04_c_001C68C0(o, self)) return;
            a04_callback(o, self);
        }
        break;
    }
    case 2:
    case 3:
        a04_c_001AFC10(o, self);
        break;
    default:
        break;
    }
}

int em_area04_port_008246B0(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_8246B0(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x824DC0 (link 00824D80): [4]. Script 0x828BE0 while D_0081076C is 1.
 * ---------------------------------------------------------------------- */
static void a04_824DC0(A04 *o, uint32_t self)
{
    uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    switch (a04_u8(o, self + 4)) {
    case 0:
        if (a04_c_001BA1C0(o, self, 0x14, &r)) return;
        if (r != 0) {
            a04_w8(o, self + 4, 3);
        } else {
            a04_w8(o, self + 0, 1);
            a04_w8(o, self + 4, 1);
        }
        break;
    case 1:
        if (a04_u8(o, D_0081076C) != 1) break;
        switch (a04_u8(o, self + 5)) {
        case 0:
            if (a04_c_001BA1A0(o, blk, A04_SCRIPT_828BE0)) return;
            a04_w8(o, self + 5, 1);
            break;
        case 1:
            if (a04_c_001BA1F0(o, self, &r)) return;
            if (r != 0) {
                a04_w8(o, S_70003B8D, 3);
                a04_w8(o, D_0081076C, 0xFF);
                a04_w8(o, self + 4, 3);
            }
            break;
        default:
            break;
        }
        break;
    case 2:
    case 3:
        a04_c_001AFC10(o, self);
        break;
    default:
        break;
    }
}

int em_area04_port_00824DC0(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_824DC0(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x825880 (link 00825840): [66]. A Use script (0x82C1F0) with a 120-frame
 * count that plays 001FBD50(self, 0x19A, 0, 300), and a marker drawn by
 * 001F4BF0 at (+0xB0, +0xB4 - 0.04, 0.34 + +0xB8) with the words (0, 0x80,
 * 0, 0x80).
 * ---------------------------------------------------------------------- */
static void a04_825880_marker(A04 *o, uint32_t self)
{
    uint32_t x = a04_u32(o, self + 0xB0);
    a04_w32(o, S_700038A0, x);
    uint32_t y = a04_u32(o, self + 0xB4);
    a04_w32(o, S_700038A4, em_ee_sub_bits(y, F_0_04));
    uint32_t z = a04_u32(o, self + 0xB8);
    a04_w32(o, S_700038B0, 0);
    a04_w32(o, S_700038B4, 0x80);
    a04_w32(o, S_700038B8, 0);
    a04_w32(o, S_700038A8, em_ee_add_bits(F_0_34, z));
    a04_w32(o, S_700038AC, F_ONE);
    a04_w32(o, S_700038BC, 0x80);
    a04_c_001F4BF0(o, S_700038A0, S_700038B0);
}

static void a04_825880(A04 *o, uint32_t self)
{
    uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    switch (a04_u8(o, self + 4)) {
    case 0:
        if (a04_c_001B0FD0(o, self, &r)) return;
        if (r == 0) {
            a04_w8(o, self + 4, 1);
            a04_w8(o, self + 0, 1);
            a04_w32(o, self + 0x30, D_00275928);
            a04_c_001C6380(o, self);
        }
        break;
    case 1:
        if (a04_c_001B17A0(o, self, &r)) return;
        if (a04_u8(o, self + 0x0B) & 4) {
            if (a04_c_001BA1A0(o, blk, A04_SCRIPT_82C1F0)) return;
            a04_w8(o, self + 4, 4);
            if (a04_c_001BA1F0(o, self, &r)) return;
            a04_w16(o, a04_u32(o, self + 0x18) + 0x28, 0x78);
            a04_w16(o, self + 0x28, 0);
        }
        a04_callback(o, self);
        if (a04_failed(o)) return;
        a04_825880_marker(o, self);
        break;
    case 4: {
        int32_t count = (int16_t)a04_u16(o, self + 0x28);
        if (count < 0x78) {
            a04_w16(o, self + 0x28, (uint32_t)(count + 1));
            if ((int16_t)a04_u16(o, self + 0x28) == 0x78) {
                if (a04_c_001FBD50(o, self, 0x19A, 0, fb(F_300))) return;
            }
        }
        if (a04_c_001BA1F0(o, self, &r)) return;
        if (r != 0) {
            a04_w8(o, self + 4, 1);
            a04_w8(o, self + 0x0B, 0);
        }
        a04_825880_marker(o, self);
        if (a04_failed(o)) return;
        a04_callback(o, self);
        break;
    }
    default:
        a04_c_001AFC10(o, self);
        break;
    }
}

int em_area04_port_00825880(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_825880(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x825B00 (link 00825AC0): [65]. Words at +0x1F0 (moving), +0x1F4
 * (direction), float +0x1F8 (base height), +0x1FC (offset): the offset
 * moves by 0.1923077 per frame between 0 and 10 and +0xB4 = base + offset;
 * D_00810834 records the end reached.
 * ---------------------------------------------------------------------- */
static void a04_825B00(A04 *o, uint32_t self)
{
    int32_t r = 0;
    switch (a04_u8(o, self + 4)) {
    case 0:
        if (a04_c_001B0FD0(o, self, &r)) return;
        if (r != 0) break;
        a04_w16(o, self + 0x28, 0);
        a04_w32(o, self + 0x1F0, 0);
        a04_w32(o, self + 0x1F8, a04_u32(o, self + 0xB4));
        if (a04_u8(o, D_00810834) != 0) {
            a04_w32(o, self + 0x1F4, 1);
            a04_w32(o, self + 0x1FC, F_10);
            uint32_t y = a04_u32(o, self + 0xB4);
            uint32_t off = a04_u32(o, self + 0x1FC);
            a04_w32(o, self + 0xB4, em_ee_add_bits(y, off));
        } else {
            a04_w32(o, self + 0x1F4, 0);
            a04_w32(o, self + 0x1FC, F_ZERO);
        }
        if (a04_c_001C6380(o, self)) return;
        if (a04_c_001A2370(o, self, self + 0xD0)) return;
        a04_w8(o, self + 4, 1);
        a04_w8(o, self + 0, 1);
        break;
    case 1: {
        int32_t count = (int16_t)a04_u16(o, self + 0x28);
        if (count != 0) {
            a04_w16(o, self + 0x28, (uint32_t)(count - 1));
            if ((int16_t)a04_u16(o, self + 0x28) == 0) {
                a04_w32(o, self + 0x1F0, 1);
                if (a04_u32(o, self + 0x1F4) == 0) {
                    if (a04_c_001FBD50(o, self, 0x451, 0, fb(F_300))) return;
                } else {
                    if (a04_c_001FBD50(o, self, 0x452, 0, fb(F_300))) return;
                }
            }
        }
        if (a04_u32(o, self + 0x1F0) != 0) {
            uint32_t dir = a04_u32(o, self + 0x1F4);
            if (dir == 1) {
                uint32_t off = em_ee_sub_bits(a04_u32(o, self + 0x1FC), F_LIFT_STEP);
                a04_w32(o, self + 0x1FC, off);
                if (em_ee_c_lt_bits(off, F_ZERO)) {
                    a04_w32(o, self + 0x1FC, 0);
                    a04_w32(o, self + 0x1F0, 0);
                    a04_w32(o, self + 0x1F4, 0);
                    a04_w8(o, D_00810834, 0);
                }
                off = a04_u32(o, self + 0x1FC);
                uint32_t base = a04_u32(o, self + 0x1F8);
                a04_w32(o, self + 0xB4, em_ee_add_bits(base, off));
            } else if (dir == 0) {
                uint32_t off = em_ee_add_bits(a04_u32(o, self + 0x1FC), F_LIFT_STEP);
                a04_w32(o, self + 0x1FC, off);
                if (!em_ee_c_le_bits(off, F_10)) {
                    a04_w32(o, self + 0x1FC, F_10);
                    a04_w32(o, self + 0x1F0, 0);
                    a04_w32(o, self + 0x1F4, 1);
                    a04_w8(o, D_00810834, 1);
                }
                off = a04_u32(o, self + 0x1FC);
                uint32_t base = a04_u32(o, self + 0x1F8);
                a04_w32(o, self + 0xB4, em_ee_add_bits(base, off));
            }
            if (a04_c_001C6380(o, self)) return;
            if (a04_c_001A2370(o, self, self + 0xD0)) return;
        }
        if (a04_c_001B1B70(o, self)) return;
        a04_callback(o, self);
        break;
    }
    default:
        a04_c_001AFC10(o, self);
        break;
    }
}

int em_area04_port_00825B00(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_825B00(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x825D60 (link 00825D20): [68]. State 0 001B0FD0 == 0 -> 001C6380,
 * state 1, +0 = 1; state 1 the callback; others (2 included) 001AFC10.
 * ---------------------------------------------------------------------- */
static void a04_825D60(A04 *o, uint32_t self)
{
    int32_t r = 0;
    switch (a04_u8(o, self + 4)) {
    case 0:
        if (a04_c_001B0FD0(o, self, &r)) return;
        if (r == 0) {
            if (a04_c_001C6380(o, self)) return;
            a04_w8(o, self + 4, 1);
            a04_w8(o, self + 0, 1);
        }
        break;
    case 1:
        a04_callback(o, self);
        break;
    default:
        a04_c_001AFC10(o, self);
        break;
    }
}

int em_area04_port_00825D60(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_825D60(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x825DF0 (link 00825DB0): the console [69]. The object it spawns at load
 * (001C5570) is kept at +0x2EC; the reel is the node at +0x1C.
 * ---------------------------------------------------------------------- */
static void a04_825DF0(A04 *o, uint32_t self)
{
    uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    uint32_t spawned = 0;
    switch (a04_u8(o, self + 4)) {
    case 0: {
        if (a04_c_001B0FD0(o, self, &r)) return;
        if (r != 0) break;
        if (a04_u8(o, D_0081083B) != 0) {
            a04_w8(o, self + 4, 4);
            a04_w16(o, self + 0x2A, 0);
            a04_w16(o, self + 0x28, 0);
        } else {
            a04_w8(o, self + 4, 1);
        }
        a04_w8(o, self + 0, 2);
        a04_w32(o, self + 0x30, A04_DATA_82C630);
        if (a04_c_001C6380(o, self)) return;
        a04_vec_a0(o, F_ZERO, F_ONE, F_ZERO, F_QUARTER);
        if (a04_c_001C5570(o, self, S_700038A0, 0xC, 0, &spawned)) return;
        a04_w32(o, self + 0x2EC, spawned);
        uint32_t e = a04_u32(o, self + 0x2EC);
        uint32_t x = a04_u32(o, e + 0xB0);
        a04_w32(o, e + 0xB0, em_ee_add_bits(x, F_0_7));
        e = a04_u32(o, self + 0x2EC);
        uint32_t y = a04_u32(o, e + 0xB4);
        a04_w32(o, e + 0xB4, em_ee_sub_bits(y, F_0_1));
        break;
    }
    case 1:
        a04_w8(o, self + 0, a04_u8(o, D_00810702) == 4 ? 1 : 2);
        if (a04_c_001B17A0(o, self, &r)) return;
        if (a04_u8(o, self + 0x0B) & 4) {
            if (a04_c_001BA1A0(o, blk, A04_SCRIPT_82C3B0)) return;
            a04_w8(o, self + 4, 4);
            if (a04_c_001BA1F0(o, self, &r)) return;
            a04_w16(o, a04_u32(o, self + 0x18) + 0x28, 0x78);
            a04_w16(o, self + 0x28, 0x78);
            a04_w16(o, self + 0x2A, 0x3C);
        }
        a04_callback(o, self);
        break;
    case 4:
        if (a04_u8(o, self + 0x0B) != 0) {
            if (a04_c_001BA1F0(o, self, &r)) return;
            if (r != 0) {
                a04_w8(o, self + 0x0B, 0);
                a04_w16(o, self + 0x28, 1);
                a04_w16(o, self + 0x2A, 0);
                a04_w8(o, a04_u32(o, self + 0x1C) + 4, 0x64);
                a04_w8(o, D_0081083B, 0xFF);
            }
        }
        {
            int32_t count = (int16_t)a04_u16(o, self + 0x28);
            if (count != 0) {
                a04_w16(o, self + 0x28, (uint32_t)(count - 1));
                if ((int16_t)a04_u16(o, self + 0x28) == 0 && a04_u32(o, self + 0x2EC) != 0) {
                    if (a04_c_001FBD50(o, self, 0x19A, 0, fb(F_300))) return;
                    a04_w8(o, a04_u32(o, self + 0x2EC) + 4, 3);
                    a04_w32(o, self + 0x2EC, 0);
                }
            } else {
                count = (int16_t)a04_u16(o, self + 0x2A);
                if (count != 0) {
                    a04_w16(o, self + 0x2A, (uint32_t)(count - 1));
                    if ((int16_t)a04_u16(o, self + 0x2A) == 0) {
                        if (a04_c_001FBD50(o, self, 0x455, 0, fb(F_300))) return;
                        a04_w8(o, D_0081083B, 0xFF);
                        a04_w8(o, a04_u32(o, self + 0x1C) + 4, 1);
                    }
                }
            }
        }
        a04_callback(o, self);
        break;
    default:
        a04_c_001AFC10(o, self);
        break;
    }
}

int em_area04_port_00825DF0(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_825DF0(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8260C0 (link 00826080): the cable reel [70]. State 1 rolls it along
 * +0xB0 in bands (each test is "not x <= limit", so a NaN x takes the first
 * band) for 700 frames; 0x64 puts it at (380, 14.9) with +0xC8 = 0.
 * ---------------------------------------------------------------------- */
static void a04_8260C0_place(A04 *o, uint32_t self)
{
    a04_w32(o, self + 0xB0, F_380);
    a04_w32(o, self + 0xB4, F_14_9);
    a04_w32(o, self + 0xC8, 0);
}

/* One band: x - dx; then optionally y - F_REEL_DY; then optionally
 * +0xC8 +/- F_REEL_ROLL (roll > 0 adds, roll < 0 subtracts). */
static void a04_8260C0_band(A04 *o, uint32_t self, uint32_t x, uint32_t dx, int drop, int roll)
{
    a04_w32(o, self + 0xB0, em_ee_sub_bits(x, dx));
    if (drop) {
        uint32_t y = a04_u32(o, self + 0xB4);
        a04_w32(o, self + 0xB4, em_ee_sub_bits(y, F_REEL_DY));
    }
    if (roll) {
        uint32_t c = a04_u32(o, self + 0xC8);
        a04_w32(o, self + 0xC8, roll > 0 ? em_ee_add_bits(c, F_REEL_ROLL) : em_ee_sub_bits(c, F_REEL_ROLL));
    }
}

static void a04_8260C0(A04 *o, uint32_t self)
{
    uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    switch (a04_u8(o, self + 4)) {
    case 0:
        if (a04_c_001B0FD0(o, self, &r)) return;
        if (r != 0) break;
        if (a04_u8(o, D_0081083B) != 0) {
            a04_8260C0_place(o, self);
        } else {
            a04_w32(o, self + 0x30, A04_DATA_82C640);
        }
        if (a04_c_001C6380(o, self)) return;
        if (a04_c_001A2370(o, self, self + 0xD0)) return;
        a04_w8(o, self + 4, 4);
        a04_w8(o, self + 5, 0);
        a04_w8(o, self + 0, 1);
        break;
    case 4:
        switch (a04_u8(o, self + 5)) {
        case 0:
            a04_w8(o, self + 2, a04_u8(o, D_0081083B) != 0 ? 4 : 0x84);
            if (a04_u8(o, self + 0x0B) & 4) {
                a04_w32(o, A04_WORD_82C724, 0xF3);
                if (a04_c_001BA1A0(o, blk, A04_SCRIPT_82C650)) return;
                a04_inc8(o, self + 5);
            }
            break;
        case 1:
            if (a04_c_001BA1F0(o, self, &r)) return;
            if (r != 0) {
                a04_w8(o, self + 5, 0);
                a04_w8(o, self + 0x0B, 0);
            }
            break;
        default:
            break;
        }
        if (a04_c_001B1B70(o, self)) return;
        a04_callback(o, self);
        break;
    case 1:
        switch (a04_u8(o, self + 5)) {
        case 0:
            a04_w8(o, self + 5, 1);
            a04_w16(o, self + 0x28, 0x2BC);
            break;
        case 1: {
            uint32_t count = a04_u16(o, self + 0x28);
            a04_w16(o, self + 0x28, count - 1);
            if ((int16_t)a04_u16(o, self + 0x28) <= 0) {
                a04_w8(o, self + 4, 0x64);
                break;
            }
            uint32_t x = a04_u32(o, self + 0xB0);
            if (!em_ee_c_le_bits(x, F_499)) {
                a04_8260C0_band(o, self, x, F_0_2, 0, 0);
            } else if (!em_ee_c_le_bits(x, F_497)) {
                a04_8260C0_band(o, self, x, F_0_2, 1, 1);
            } else if (!em_ee_c_le_bits(x, F_495)) {
                a04_8260C0_band(o, self, x, F_REEL_DX, 1, 1);
            } else if (!em_ee_c_le_bits(x, F_428)) {
                a04_8260C0_band(o, self, x, F_REEL_DX, 1, 0);
            } else if (!em_ee_c_le_bits(x, F_426)) {
                a04_8260C0_band(o, self, x, F_REEL_DX, 1, -1);
            } else if (!em_ee_c_le_bits(x, F_424)) {
                a04_8260C0_band(o, self, x, F_0_2, 0, -1);
            } else if (!em_ee_c_le_bits(x, F_395)) {
                a04_8260C0_band(o, self, x, F_0_2, 0, 0);
            } else {
                a04_8260C0_place(o, self);
            }
            if (a04_c_001C6380(o, self)) return;
            if (a04_c_001A2370(o, self, self + 0xD0)) return;
            break;
        }
        default:
            break;
        }
        if (a04_c_001B17A0(o, self, &r)) return;
        a04_callback(o, self);
        break;
    case 0x64:
        a04_8260C0_place(o, self);
        a04_w8(o, self + 4, 4);
        a04_w8(o, self + 5, 0);
        if (a04_c_001C6380(o, self)) return;
        if (a04_c_001A2370(o, self, self + 0xD0)) return;
        if (a04_c_001B17A0(o, self, &r)) return;
        a04_callback(o, self);
        break;
    default:
        a04_c_001AFC10(o, self);
        break;
    }
}

int em_area04_port_008260C0(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_8260C0(&o, self);
    return a04_end(&o);
}

/* ------------------------------------------------------------------------
 * 001BE5F0 (boot): with the scratchpad byte 0x70003B8D clear, 1 when the
 * horizontal distance from `player` (+0xA0 / +0xA8) to `actor` (+0xB0 /
 * +0xB8) is <= the first float of the record at block +0x18 and |player
 * +0xA4 - actor +0xB4| is <= its second float; else 0.
 * ---------------------------------------------------------------------- */
static int32_t a04_1BE5F0(A04 *o, uint32_t player, uint32_t actor, uint32_t block)
{
    float r = 0.0f;
    if (a04_u8(o, S_70003B8D) != 0) return 0;
    uint32_t gx = a04_u32(o, player + 0xA0);
    uint32_t sx = a04_u32(o, actor + 0xB0);
    uint32_t gz = a04_u32(o, player + 0xA8);
    uint32_t sz = a04_u32(o, actor + 0xB8);
    uint32_t dx = em_ee_sub_bits(gx, sx);
    uint32_t dz = em_ee_sub_bits(gz, sz);
    if (a04_c_0011E748(o, fb(em_ee_madd_bits(em_ee_mula_bits(dx, dx), dz, dz)), &r)) return 0;
    uint32_t limits = a04_u32(o, block + 0x18);
    if (!em_ee_c_le_bits(em_ee_bits(r), a04_u32(o, limits + 0))) return 0;
    uint32_t gy = a04_u32(o, player + 0xA4);
    uint32_t sy = a04_u32(o, actor + 0xB4);
    if (a04_c_0011DF78(o, fb(em_ee_sub_bits(gy, sy)), &r)) return 0;
    limits = a04_u32(o, block + 0x18);
    return em_ee_c_le_bits(em_ee_bits(r), a04_u32(o, limits + 4)) ? 1 : 0;
}

int em_area04_port_001BE5F0(const EmArea04PortHooks *h, uint32_t player, uint32_t actor, uint32_t block,
                            int32_t *result, EmArea04PortFault *fault)
{
    A04 o;
    if (!result || a04_begin(&o, h, fault)) return -1;
    int32_t r = a04_1BE5F0(&o, player, actor, block);
    if (a04_end(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001C1A80 (boot). State 0: bone record setup (001D0C80, 001D0D40,
 * 001C62C0), +0x30 = +0x208 = &D_00275660, state 1, +0x0A = 0, a timer
 * 240 + (240 (rand >> 16) >> 15) at +0x28, and 001D0D60(+0x90, 1 + 19
 * (2^-31 rand)). State 1: with 001B17A0 non-zero the timer counts down (at
 * 0: 001FBD50(self, 0x441, 0, 300) and 300 + (180 (rand >> 16) >> 15)),
 * then 001BE5F0(player block, self, self + 0x1F0): on contact 00187EC0(8,
 * 0) and +0x0A = 1, the first contact copying the player's +0xA0 vector
 * to +0xA0, a repeat pushing the player back by -0.7 x (player - self)
 * (00183010) first. Then 001C6380, 001D0D60(+0x90, 1.0) and the callback.
 * States 2 / 3: 001AF890(+0x90), 001AFC10.
 * ---------------------------------------------------------------------- */
static void a04_1C1A80(A04 *o, uint32_t self)
{
    int32_t r = 0;
    switch (a04_u8(o, self + 4)) {
    case 0: {
        if (a04_c_001D0C80(o, self, a04_u32(o, D_0028A508))) return;
        if (a04_c_001D0D40(o, self, D_0024F8F0, 0x28, 1)) return;
        if (a04_c_001C62C0(o, self)) return;
        a04_w32(o, self + 0x30, D_00275660);
        a04_w32(o, self + 0x1F0 + 0x18, D_00275660);
        a04_w8(o, self + 4, 1);
        a04_w8(o, self + 0x0A, 0);
        if (a04_c_00122BB8(o, &r)) return;
        int32_t x = r >> 16;
        a04_w16(o, self + 0x28, (uint32_t)(((x * 240) >> 15) + 0xF0));
        if (a04_c_00122BB8(o, &r)) return;
        uint32_t handle = a04_u32(o, self + 0x90);
        uint32_t f = em_ee_mul_bits(F_2POW_M31, em_ee_cvt_s_w_bits((uint32_t)r));
        f = em_ee_mul_bits(F_19, f);
        a04_c_001D0D60(o, handle, fb(em_ee_add_bits(F_ONE, f)));
        break;
    }
    case 1:
        if (a04_c_001B17A0(o, self, &r)) return;
        if (r == 0) {
            a04_w8(o, self + 0x0A, 0);
        } else {
            int32_t count = (int16_t)a04_u16(o, self + 0x28);
            if (count != 0) {
                a04_w16(o, self + 0x28, (uint32_t)(count - 1));
            } else {
                if (a04_c_001FBD50(o, self, 0x441, 0, fb(F_300))) return;
                if (a04_c_00122BB8(o, &r)) return;
                int32_t x = r >> 16;
                a04_w16(o, self + 0x28, (uint32_t)(((x * 180) >> 15) + 0x12C));
            }
            r = a04_1BE5F0(o, D_008102B0, self, self + 0x1F0);
            if (a04_failed(o)) return;
            if (r == 0) {
                a04_w8(o, self + 0x0A, 0);
            } else {
                if (a04_c_00187EC0(o, 8, 0)) return;
                if (a04_u8(o, self + 0x0A) == 0) {
                    a04_w8(o, self + 0x0A, 1);
                    if (a04_c_00102948(o, self + 0xA0, D_008102B0 + 0xA0)) return;
                } else {
                    a04_w8(o, self + 0x0A, 1);
                    if (a04_c_001028D0(o, S_700038A0, D_008102B0 + 0xA0, self + 0xA0)) return;
                    if (a04_c_00102900(o, S_700038A0, S_700038A0, fb(F_MINUS_0_7))) return;
                    if (a04_c_00183010(o, D_008102B0, S_700038A0)) return;
                    if (a04_c_00102948(o, self + 0xA0, D_008102B0 + 0xA0)) return;
                }
            }
        }
        if (a04_c_001C6380(o, self)) return;
        if (a04_c_001D0D60(o, a04_u32(o, self + 0x90), fb(F_ONE))) return;
        a04_callback(o, self);
        break;
    case 2:
    case 3:
        if (a04_c_001AF890(o, a04_u32(o, self + 0x90))) return;
        a04_c_001AFC10(o, self);
        break;
    default:
        break;
    }
}

int em_area04_port_001C1A80(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault)
{
    A04 o;
    if (a04_begin(&o, h, fault)) return -1;
    a04_1C1A80(&o, self);
    return a04_end(&o);
}
