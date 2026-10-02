/* The eleventh level's AREA13 overlay functions (OVERLAY/AREA13.BIN, id 10;
 * runtime addresses, linked 0x40 lower): [44]'s steps 2..4 and their area
 * test, chain scan and script callbacks; the watchers' effect spawn
 * 0x826610; [45]'s step functions behind the table 0x82D190; the thrown
 * piece 0x828500 with its collision method and the effects 0x828C60 /
 * 0x828E10 / 0x828F40. docs/LEVEL11_PORT.md section 2 (behaviour) and
 * section 3 (verification).
 *
 * Every function follows the original instructions (the decomp's
 * byte-identical C as a guide; 0x827F90 is NEARMISS there): the order of the
 * loads and stores between calls, the float operand order and every branch
 * are the original's, as tools/test_level11_port_reference.py checks.
 * Floats are bit patterns (em_ee_float.h). */
#include "em_level11_port_internal.h"

#define D_00810350 0x00810350u /* the player's position x, y, z */
#define D_00810354 0x00810354u
#define D_00810358 0x00810358u
#define D_00810360 0x00810360u
#define D_00810774 0x00810774u /* flag 0x1C */
#define D_008107F4 0x008107F4u /* counter 0x1C: [44]'s step in bits 0..3 */
#define D_00810833 0x00810833u
#define D_00275B40 0x00275B40u /* pointer: the running actor's bone block */
#define D_00275CA8 0x00275CA8u /* pointer: [45]'s published work block */
#define S_700031B0 0x700031B0u
#define S_700031D0 0x700031D0u
#define S_700036A0 0x700036A0u
#define S_700036D0 0x700036D0u
#define S_700038A0 0x700038A0u
#define S_700038B0 0x700038B0u
#define S_70003A20 0x70003A20u

#define F_2P31   0x4F000000u /* 2147483648.0 */
#define F_TWO_PI 0x40C90FDBu
#define F_PI     0x40490FDBu

/* [45]'s work block, through the pointer D_00275CA8: the original reads the
 * pointer again for every use (one read for a read-modify-write). */
static int32_t l11_d(L11 *o, int k) { return l11_s32(o, l11_u32(o, D_00275CA8) + 4u * (uint32_t)k); }
static void l11_wd(L11 *o, int k, uint32_t v) { l11_w32(o, l11_u32(o, D_00275CA8) + 4u * (uint32_t)k, v); }
static void l11_add_d(L11 *o, int k, int32_t add)
{
    uint32_t at = l11_u32(o, D_00275CA8) + 4u * (uint32_t)k;
    l11_w32(o, at, (uint32_t)(l11_s32(o, at) + add));
}

/* (float)00122BB8() / 2147483648.0 */
static uint32_t l11_unit_random(L11 *o)
{
    int32_t r = 0;
    l11_c_00122BB8(o, &r);
    return L11_DIV(L11_CVT_S_W(r), F_2P31);
}

/* ------------------------------------------------------------------------
 * 0x8240E0: the walkway test. 1 when flag 0x1C (D_00810774) is not 0xFF,
 * 001B1EA0(0, player position, the area 0x82E140, 4) is set and the
 * player's y is above 210; else 0. */
int32_t l11_008240E0(L11 *o)
{
    if (l11_u8(o, D_00810774) == 0xFFu) return 0;
    int32_t r = 0;
    if (l11_c_001B1EA0(o, 0, D_00810350, 0x0082E140u, 4, &r)) return 0;
    if (r == 0) return 0;
    if (L11_LE(l11_u32(o, D_00810354), 0x43520000u)) return 0; /* 210.0 */
    return 1;
}

int em_level11_port_008240E0(const EmLevel11PortHooks *h, int32_t *result, EmLevel11PortFault *fault)
{
    L11 o;
    if (!result || l11_begin(&o, h, fault)) return -1;
    int32_t r = l11_008240E0(&o);
    if (l11_end(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x8249F0: walks up to 12 objects of the +0x18 chain from self's +0x18 and
 * sets the halfword +0x36 = 1 on each one whose +2 & 0x1F is 4, +3 is 0xA
 * and +0xB4 is not below 215. Returns 1. */
int32_t l11_008249F0(L11 *o, uint32_t self)
{
    uint32_t obj = l11_u32(o, self + 0x18);
    int32_t i = 0;
    for (;;) {
        if (l11_failed(o)) return 1;
        if ((l11_u8(o, obj + 2) & 0x1Fu) == 4u && l11_u8(o, obj + 3) == 0xAu
            && !L11_LT(l11_u32(o, obj + 0xB4), 0x43570000u)) /* 215.0 */
            l11_w16(o, obj + 0x36, 1);
        obj = l11_u32(o, obj + 0x18);
        if (obj == 0) break;
        if (++i >= 12) break;
    }
    return 1;
}

int em_level11_port_008249F0(const EmLevel11PortHooks *h, uint32_t self, int32_t *result, EmLevel11PortFault *fault)
{
    L11 o;
    if (!result || l11_begin(&o, h, fault)) return -1;
    int32_t r = l11_008249F0(&o, self);
    if (l11_end(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x824390: [44]'s step 2. talk = self + 0x1F0; parent = self +0x1C.
 * +5 0: +5 = 1, the halfword +0x2A = 490. +5 1: +0x2A -= 1; at <= 0:
 * 0x8249F0(self), then script 0x82B3D0 (0x8240E0 set) or 0x82B810 (0x824060
 * set) with +5 = 2, +6 = 1, else script 0x82BC90, D_008107F4 += 1, the
 * parent's +0x2A += 1, +5 = +6 = 0; +0x2A = 0. +5 2: when 001BA1F0(self)
 * is set the parent's +0x2A += 1, +5 = 0, D_008107F4 += 1. Then 001C6380,
 * 001B17A0 and the +0x4C method. */
int em_level11_port_00824390(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    uint32_t talk = self + 0x1F0;
    uint32_t sub = l11_u8(&o, self + 5);
    uint32_t parent = l11_u32(&o, self + 0x1C);
    int32_t r = 0;
    switch (sub) {
    case 0:
        l11_w8(&o, self + 5, 1);
        l11_w16(&o, self + 0x2A, 0x1EA);
        break;
    case 1:
        l11_w16(&o, self + 0x2A, (uint32_t)(l11_s16(&o, self + 0x2A) - 1));
        if (l11_s16(&o, self + 0x2A) <= 0) {
            (void)l11_008249F0(&o, self);
            if (l11_008240E0(&o) != 0) {
                l11_c_001BA1A0(&o, talk, 0x0082B3D0u);
                l11_w8(&o, self + 5, 2);
                l11_w8(&o, self + 6, 1);
            } else {
                if (l11_failed(&o) || l11_c_00824060(&o, &r)) break;
                if (r != 0) {
                    l11_c_001BA1A0(&o, talk, 0x0082B810u);
                    l11_w8(&o, self + 5, 2);
                    l11_w8(&o, self + 6, 1);
                } else {
                    l11_c_001BA1A0(&o, talk, 0x0082BC90u);
                    l11_w8(&o, D_008107F4, l11_u8(&o, D_008107F4) + 1);
                    l11_w16(&o, parent + 0x2A, (uint32_t)(l11_s16(&o, parent + 0x2A) + 1));
                    l11_w8(&o, self + 5, 0);
                    l11_w8(&o, self + 6, 0);
                }
            }
            l11_w16(&o, self + 0x2A, 0);
        }
        break;
    case 2:
        if (l11_c_001BA1F0(&o, self, &r)) break;
        if (r != 0) {
            l11_w16(&o, parent + 0x2A, (uint32_t)(l11_s16(&o, parent + 0x2A) + 1));
            l11_w8(&o, self + 5, 0);
            l11_w8(&o, D_008107F4, l11_u8(&o, D_008107F4) + 1);
        }
        break;
    default:
        break;
    }
    l11_c_001C6380(&o, self);
    l11_c_001B17A0(&o, self, &r);
    l11_method(&o, self);
    return l11_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x824520: [44]'s step 3: three scripts released by the parent's (+0x1C)
 * halfword +0x28. +5 0 (count >= 2): script 0x82BD10 when 001BA1F0 is set
 * (+6 0) or at once (+6 1), +5 = 1; +5 1 (count >= 3): script 0x82BDD0,
 * +5 = 2, then 001BA1F0; +5 2 (count >= 4): script 0x82BE90, +5 = 3, then
 * 001BA1F0; +5 3: when 001BA1F0 is set 001FABB0(), 001FA790(0, 0x12),
 * D_008107F4 += 1, +5 = 0. Then 001C6380, 001B17A0 and the +0x4C method. */
int em_level11_port_00824520(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    uint32_t talk = self + 0x1F0;
    uint32_t sub = l11_u8(&o, self + 5);
    uint32_t parent = l11_u32(&o, self + 0x1C);
    int32_t r = 0;
    switch (sub) {
    case 0:
        if (l11_s16(&o, parent + 0x28) >= 2) {
            switch (l11_u8(&o, self + 6)) {
            case 0:
                if (l11_c_001BA1F0(&o, self, &r)) break;
                if (r != 0) {
                    l11_c_001BA1A0(&o, talk, 0x0082BD10u);
                    l11_w8(&o, self + 5, 1);
                }
                break;
            case 1:
                l11_c_001BA1A0(&o, talk, 0x0082BD10u);
                l11_w8(&o, self + 5, 1);
                break;
            default:
                break;
            }
        }
        break;
    case 1:
        if (l11_s16(&o, parent + 0x28) >= 3) {
            l11_c_001BA1A0(&o, talk, 0x0082BDD0u);
            l11_w8(&o, self + 5, 2);
        }
        l11_c_001BA1F0(&o, self, &r);
        break;
    case 2:
        if (l11_s16(&o, parent + 0x28) >= 4) {
            l11_c_001BA1A0(&o, talk, 0x0082BE90u);
            l11_w8(&o, self + 5, 3);
        }
        l11_c_001BA1F0(&o, self, &r);
        break;
    case 3:
        if (l11_c_001BA1F0(&o, self, &r)) break;
        if (r != 0) {
            l11_c_001FABB0(&o);
            l11_c_001FA790(&o, 0, 0x12);
            l11_w8(&o, D_008107F4, l11_u8(&o, D_008107F4) + 1);
            l11_w8(&o, self + 5, 0);
        }
        break;
    default:
        break;
    }
    l11_c_001C6380(&o, self);
    l11_c_001B17A0(&o, self, &r);
    l11_method(&o, self);
    return l11_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8246D0: script 0x82B3D0's callback (a0, self, blk). self +4 0: the
 * float blk +0x10 = 0, +4 = 1. +4 1: blk +0x10 += 1; the player's x -= 1;
 * the player's y += 0.2 * 0011DE90(0.034906585 * blk +0x10); past 50 the
 * count is reset and the result is 1. Else 0. */
int em_level11_port_008246D0(const EmLevel11PortHooks *h, uint32_t a0, uint32_t self, uint32_t blk,
                             int32_t *result, EmLevel11PortFault *fault)
{
    L11 o;
    (void)a0;
    if (!result || l11_begin(&o, h, fault)) return -1;
    int32_t ret = 0;
    switch (l11_u8(&o, self + 4)) {
    case 0:
        l11_w32(&o, blk + 0x10, F_ZERO);
        l11_w8(&o, self + 4, 1);
        break;
    case 1: {
        l11_w32(&o, blk + 0x10, L11_ADD(l11_u32(&o, blk + 0x10), F_ONE));
        l11_w32(&o, D_00810350, L11_SUB(l11_u32(&o, D_00810350), F_ONE));
        uint32_t s = 0;
        if (l11_c_0011DE90(&o, L11_MUL(0x3D0EFA35u, l11_u32(&o, blk + 0x10)), &s)) break; /* 0.034906585 */
        l11_w32(&o, D_00810354, L11_ADD(l11_u32(&o, D_00810354), L11_MUL(0x3E4CCCCDu, s))); /* 0.2 */
        if (!L11_LE(l11_u32(&o, blk + 0x10), 0x42480000u)) { /* 50.0 */
            l11_w32(&o, blk + 0x10, F_ZERO);
            ret = 1;
        }
        break;
    }
    default:
        break;
    }
    if (l11_end(&o)) return -1;
    *result = ret;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x8248C0: the callback of scripts 0x82B3D0 and 0x82B810 (a0, self, blk):
 * as 0x8246D0 with the player's z -= 0.7 per call and the reset past 45. */
int em_level11_port_008248C0(const EmLevel11PortHooks *h, uint32_t a0, uint32_t self, uint32_t blk,
                             int32_t *result, EmLevel11PortFault *fault)
{
    L11 o;
    (void)a0;
    if (!result || l11_begin(&o, h, fault)) return -1;
    int32_t ret = 0;
    switch (l11_u8(&o, self + 4)) {
    case 0:
        l11_w32(&o, blk + 0x10, F_ZERO);
        l11_w8(&o, self + 4, 1);
        break;
    case 1:
        l11_w32(&o, blk + 0x10, L11_ADD(l11_u32(&o, blk + 0x10), F_ONE));
        l11_w32(&o, D_00810358, L11_SUB(l11_u32(&o, D_00810358), 0x3F333333u)); /* 0.7 */
        if (!L11_LE(l11_u32(&o, blk + 0x10), 0x42340000u)) { /* 45.0 */
            l11_w32(&o, blk + 0x10, F_ZERO);
            ret = 1;
        }
        break;
    default:
        break;
    }
    if (l11_end(&o)) return -1;
    *result = ret;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x824960: [44]'s step 4. +5 0: once D_00810833 != 0, script 0x82C110 and
 * +5 = 1. +5 1: when 001BA1F0(self) is set, 001FAE70(0) and +4 = 3. */
int em_level11_port_00824960(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    uint32_t talk = self + 0x1F0;
    int32_t r = 0;
    switch (l11_u8(&o, self + 5)) {
    case 0:
        if (l11_u8(&o, D_00810833) != 0) {
            l11_c_001BA1A0(&o, talk, 0x0082C110u);
            l11_w8(&o, self + 5, 1);
        }
        break;
    case 1:
        if (l11_c_001BA1F0(&o, self, &r)) break;
        if (r != 0) {
            l11_c_001FAE70(&o, 0);
            l11_w8(&o, self + 4, 3);
        }
        break;
    default:
        break;
    }
    return l11_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x826610 (src; called by 0x824BB0 with its matrix): o = 001AFA90(0xC);
 * when set: the colour quadword 0x82C9E0 copied to the frame, 00102948(o +
 * 0xB0, src + 0x30), 00102958(o + 0xD0, src), 001026A0(o + 0x100, o +
 * 0xD0, the copy), o +0x10 = 0x1F5040 (the behaviour). */
int em_level11_port_00826610(const EmLevel11PortHooks *h, uint32_t src, uint32_t sp, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    uint32_t obj = 0;
    if (l11_c_001AFA90(&o, 0xC, &obj)) return l11_end(&o);
    if (obj != 0) {
        uint8_t q[16];
        uint32_t v = sp - 0x10;
        l11_q(&o, 0x0082C9E0u, q);
        l11_wq(&o, v, q);
        l11_c_00102948(&o, obj + 0xB0, src + 0x30);
        l11_c_00102958(&o, obj + 0xD0, src);
        l11_c_001026A0(&o, obj + 0x100, obj + 0xD0, v);
        l11_w32(&o, obj + 0x10, 0x001F5040u);
    }
    return l11_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x827C30: [45]'s step 0 (0x82D190[0]); the block is D_00275CA8's. At
 * count ([1]) 0x1DF: 001EFD20(0x80000054 / 6) at 0x82D1B0[0], self's
 * halfword +0x28 += 1, markers left ([3]) -= 1, 001F02C0(0x82D1B0, 0x8D3,
 * 500), 001B1E20(8, 100). At 0x1F3: D_008107F4 |= 0x20, the point (743,
 * 230, 1265, 1) in the frame, 001EFD20(0x80000041, it), 001F02C0(it, 0x44D,
 * 100). Counts 1 / 100 / 200 / 300 / 400 set [6] = 5 / 4 / 3 / 2 / 1. The
 * limit [2] = 0x30C. */
int em_level11_port_00827C30(const EmLevel11PortHooks *h, uint32_t self, uint32_t sp, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    if (l11_d(&o, 1) == 0x1DF) {
        l11_c_001EFD20(&o, (int32_t)0x80000054u, 0x0082D1B0u);
        l11_c_001EFD20(&o, 6, 0x0082D1B0u);
        l11_w16(&o, self + 0x28, (uint32_t)(l11_s16(&o, self + 0x28) + 1));
        l11_add_d(&o, 3, -1);
        l11_c_001F02C0(&o, 0x0082D1B0u, 0x8D3, 0x43FA0000u); /* 500.0 */
        l11_c_001B1E20(&o, 8, 0x64);
    }
    if (l11_d(&o, 1) == 0x1F3) {
        uint32_t v = sp - 0x10;
        uint32_t f4 = l11_u8(&o, D_008107F4);
        l11_w32(&o, v + 0, 0x4439C000u);  /* 743.0 */
        l11_w32(&o, v + 4, 0x43660000u);  /* 230.0 */
        l11_w8(&o, D_008107F4, f4 | 0x20u);
        l11_w32(&o, v + 8, 0x449E2000u);  /* 1265.0 */
        l11_w32(&o, v + 12, F_ONE);
        l11_c_001EFD20(&o, (int32_t)0x80000041u, v);
        l11_c_001F02C0(&o, v, 0x44D, 0x42C80000u); /* 100.0 */
    }
    uint32_t bp = l11_u32(&o, D_00275CA8);
    switch (l11_s32(&o, bp + 4)) {
    case 1: l11_w32(&o, bp + 24, 5); break;
    case 0x64: l11_w32(&o, bp + 24, 4); break;
    case 0xC8: l11_w32(&o, bp + 24, 3); break;
    case 0x12C: l11_w32(&o, bp + 24, 2); break;
    case 0x190: l11_w32(&o, bp + 24, 1); break;
    default: break;
    }
    l11_wd(&o, 2, 0x30C);
    return l11_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x827DD0: [45]'s step 1: the limit [2] = 0 when self's halfword +0x2A is
 * nonzero, else 0xFFFF. */
int em_level11_port_00827DD0(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    int32_t v = l11_s16(&o, self + 0x2A);
    l11_wd(&o, 2, v != 0 ? 0u : 0xFFFFu);
    return l11_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x827E00: [45]'s step 2: at counts 0 / 0x5A / 0x96 (n = 1 / 2 / 3) the
 * scratch angle (-pi/2, 0, 0, 1) at 0x700038A0, 001EFD20(0x8000004A /
 * 0x8000002F) at 0x82D1B0[n], self's +0x28 += 1, markers left -= 1,
 * 001F02C0(self + 0xB0, 0x44B, 500). The limit [2] = 0x12C. */
int em_level11_port_00827E00(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    int32_t n;
    switch (l11_d(&o, 1)) {
    case 0: n = 1; break;
    case 0x5A: n = 2; break;
    case 0x96: n = 3; break;
    default: n = -1; break;
    }
    if (n > 0) {
        uint32_t p = 0x0082D1B0u + (uint32_t)n * 16u;
        l11_w32(&o, S_700038A0, 0xBFC90FDBu); /* -1.5707964 */
        l11_w32(&o, S_700038A0 + 4, F_ZERO);
        l11_w32(&o, S_700038A0 + 8, F_ZERO);
        l11_w32(&o, S_700038A0 + 12, F_ONE);
        l11_c_001EFD20(&o, (int32_t)0x8000004Au, p);
        l11_c_001EFD20(&o, (int32_t)0x8000002Fu, p);
        l11_w16(&o, self + 0x28, (uint32_t)(l11_s16(&o, self + 0x28) + 1));
        l11_add_d(&o, 3, -1);
        l11_c_001F02C0(&o, self + 0xB0, 0x44B, 0x43FA0000u); /* 500.0 */
    }
    l11_wd(&o, 2, 0x12C);
    return l11_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x827F20: [45]'s step 3: 001C64F0(self, 1.0) while the count is above
 * 0xB4; 001F02C0(self + 0xB0, 0x8D4, 500) at count 0. The limit = 0x168. */
int em_level11_port_00827F20(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    int32_t r = 0;
    if (l11_d(&o, 1) > 0xB4) l11_c_001C64F0(&o, self, F_ONE, &r);
    if (l11_d(&o, 1) == 0) l11_c_001F02C0(&o, self + 0xB0, 0x8D4, 0x43FA0000u); /* 500.0 */
    l11_wd(&o, 2, 0x168);
    return l11_end(&o);
}

/* A random point up the matrix at (*(D_00275B40 + 4)) + 0x90:
 * 0x70003A20 = 00122BB8() / 2^31 = t; 0x700038A0 = (15 - 20t, 210t,
 * -15 - 20t, 1) transformed by 001026A0 in place. */
static void l11_random_point(L11 *o)
{
    uint32_t t = l11_unit_random(o);
    uint32_t bones = l11_u32(o, D_00275B40);
    l11_w32(o, S_70003A20, t);
    t = l11_u32(o, S_70003A20);
    uint32_t t20 = L11_MUL(0x41A00000u, t); /* 20.0 */
    l11_w32(o, S_700038A0, L11_SUB(0x41700000u, t20));           /* 15.0 */
    l11_w32(o, S_700038A0 + 4, L11_MUL(0x43520000u, t));          /* 210.0 */
    l11_w32(o, S_700038A0 + 8, L11_SUB(0xC1700000u, t20));       /* -15.0 */
    l11_w32(o, S_700038A0 + 12, F_ONE);
    uint32_t m = l11_u32(o, bones + 4) + 0x90;
    l11_c_001026A0(o, S_700038A0, m, S_700038A0);
}

/* ------------------------------------------------------------------------
 * 0x827F90: [45]'s step 4 (decomp NEARMISS; this follows the original).
 * Phase [4] 0: every 45 counts a random point (l11_random_point), the
 * angle (-pi/2, 0, 0, 1) at 0x700038B0, 001EFD20 of 00122BB8() % 3 (0, 1
 * or 2), then 2, 2, 0x8000004A, 0x8000002F (and 3 at count 0) at the
 * point, 001F02C0(self + 0xB0, 0x8D6, 200); every 20 counts a second
 * random point and the angle (pi/2, 0, 0, 1); with self +0x3C <= 10, the
 * player inside the area 0x82D330 (001B1EA0(0, .., 8)) and below y 210:
 * D_008102BF = 0xB, D_008102B0 |= 2, D_008104D4 = D_008104D0; with +0x3C
 * <= 4: [4] += 1, 001F02C0(self + 0xB0, 0x8D5, 500), D_00810833 = 0xFF.
 * Phase 1: at [5] 0 five 001EFD20(4, ..) at points stepped from (795, 180,
 * 1130) by a quarter of the way to (680, 170, 1015); [5] += 1 and past 30
 * self +5 += 1. Always 001C64F0(self, 1.0) and the limit [2] = 0xFFFF. */
int em_level11_port_00827F90(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    int32_t r = 0;
    uint32_t bp = l11_u32(&o, D_00275CA8);
    switch (l11_s32(&o, bp + 16)) {
    case 0:
        if (l11_s32(&o, bp + 4) % 45 == 0) {
            l11_random_point(&o);
            l11_w32(&o, S_700038B0, 0xBFC90FDBu); /* -1.5707964 */
            l11_w32(&o, S_700038B0 + 4, F_ZERO);
            l11_w32(&o, S_700038B0 + 8, F_ZERO);
            l11_w32(&o, S_700038B0 + 12, F_ONE);
            if (l11_c_00122BB8(&o, &r)) break;
            switch (r % 3) {
            case 0: l11_c_001EFD20(&o, 0, S_700038A0); break;
            case 1: l11_c_001EFD20(&o, 1, S_700038A0); break;
            case 2: l11_c_001EFD20(&o, 2, S_700038A0); break;
            default: break;
            }
            l11_c_001EFD20(&o, 2, S_700038A0);
            l11_c_001EFD20(&o, 2, S_700038A0);
            l11_c_001EFD20(&o, (int32_t)0x8000004Au, S_700038A0);
            l11_c_001EFD20(&o, (int32_t)0x8000002Fu, S_700038A0);
            if (l11_d(&o, 1) == 0) l11_c_001EFD20(&o, 3, S_700038A0);
            l11_c_001F02C0(&o, self + 0xB0, 0x8D6, 0x43480000u); /* 200.0 */
        }
        if (l11_d(&o, 1) % 20 == 0) {
            l11_random_point(&o);
            l11_w32(&o, S_700038B0, 0x3FC90FDBu); /* 1.5707964 */
            l11_w32(&o, S_700038B0 + 4, F_ZERO);
            l11_w32(&o, S_700038B0 + 8, F_ZERO);
            l11_w32(&o, S_700038B0 + 12, F_ONE);
        }
        if (L11_LE(l11_u32(&o, self + 0x3C), 0x41200000u)) { /* 10.0 */
            if (l11_c_001B1EA0(&o, 0, D_00810350, 0x0082D330u, 8, &r)) break;
            if (r != 0 && L11_LT(l11_u32(&o, D_00810354), 0x43520000u)) { /* 210.0 */
                l11_w8(&o, 0x008102BFu, 0xB);
                l11_w8(&o, 0x008102B0u, l11_u8(&o, 0x008102B0u) | 2u);
                l11_w32(&o, 0x008104D4u, l11_u32(&o, 0x008104D0u));
            }
        }
        if (L11_LE(l11_u32(&o, self + 0x3C), 0x40800000u)) { /* 4.0 */
            l11_add_d(&o, 4, 1);
            l11_c_001F02C0(&o, self + 0xB0, 0x8D5, 0x43FA0000u); /* 500.0 */
            l11_w8(&o, D_00810833, 0xFF);
        }
        break;
    case 1:
        if (l11_s32(&o, bp + 20) == 0) {
            l11_w32(&o, S_700038A0, 0x4446C000u);      /* 795.0 */
            l11_w32(&o, S_700038A0 + 4, 0x43340000u);  /* 180.0 */
            l11_w32(&o, S_700038A0 + 8, 0x448D4000u);  /* 1130.0 */
            l11_w32(&o, S_700038A0 + 12, F_ONE);
            l11_w32(&o, S_700038B0, 0x442A0000u);      /* 680.0 */
            l11_w32(&o, S_700038B0 + 4, 0x432A0000u);  /* 170.0 */
            l11_w32(&o, S_700038B0 + 8, 0x447DC000u);  /* 1015.0 */
            l11_w32(&o, S_700038B0 + 12, F_ONE);
            l11_c_001028D0(&o, S_700038B0, S_700038B0, S_700038A0);
            l11_c_00102850(&o, S_700038B0, S_700038B0, 0x40800000u); /* 4.0 */
            for (int i = 4; i >= 0; i--) {
                if (l11_failed(&o)) break;
                l11_c_001EFD20(&o, 4, S_700038A0);
                l11_c_001028B8(&o, S_700038A0, S_700038A0, S_700038B0);
            }
        }
        l11_add_d(&o, 5, 1);
        if (l11_d(&o, 5) > 0x1E) l11_w8(&o, self + 5, l11_u8(&o, self + 5) + 1);
        break;
    default:
        break;
    }
    l11_c_001C64F0(&o, self, F_ONE, &r);
    l11_wd(&o, 2, 0xFFFF);
    return l11_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8284E0 (self, other): the collision method 0x828500 stores in +0x34:
 * +4 = 2 unless bit 1 of the other object's +0 is set. */
int em_level11_port_008284E0(const EmLevel11PortHooks *h, uint32_t self, uint32_t other, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    if (!(l11_u8(&o, other) & 2u)) l11_w8(&o, self + 4, 2);
    return l11_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x828500: the thrown piece. blk = self + 0x1F0; the colour quadword
 * 0x82D3B0 is copied to the frame at entry.
 * +4 0: blk +0x20 = model 0x15 / 0x16 / 0x17 / 0x15 for +0xD 7..10; the
 * target blk +0x10: the player (+0xD 7, with scale +0x60..0x68 = 0.5; 8),
 * a random point at radius 50 around the player (9: 0x70003A20 = 2pi *
 * random, (50 sin, 0, 50 cos) + the player through 001028B8) or (735, 225,
 * 1250, 1) (10); blk = the start (00102948 from +0xB0); +0xC0 = 0, +0xC4 /
 * +0xC8 = 2pi * random - pi; blk +0x24 = 1200, +0x28 = +0x2C = 0; +4 = 1,
 * +0x30 = 0x2759C8, +0x34 = 0x8284E0, +0 = 1; then as +4 1.
 * +4 1: +4 = 3 and done when D_008106B8 == 2 and the halfword at 0x28A9A0
 * is 2; with D_00810833 0xFF +4 = 3 for +0xD 7 / 8 (and on); life (blk
 * +0x24) -= 1, below 0 +4 = 3; +0xC0 = pi * blk +0x28 / 180, blk +0x28 +=
 * 3 (-360 past 180); the point start + (target - start) * t (blk +0x2C)
 * at 0x700038A0, its y += 100 * 0011E2A8(pi * t), w = 1; past t 0.5 a
 * 0019A570(+0xB0, point, 6, 0) hit puts +0xB0 at the hit (0x700031B0) and
 * +4 = 2; else +0xB0 = the point, the marker matrix at 0x700036A0
 * (001029C0, 00102C58 by +0xC0, 00102918 by the point), scale (1, 1, 1, 1)
 * at 0x700038B0, 001CA7B0(0x700036D0, 10) >= 0 -> 001C7900(0x700036A0,
 * 0x700038B0, 0x3F5, 0) and 001CA940(h, 001C6120(*0x28A59C, model)); a
 * down probe from (+0xB0 x, -10000, +0xB8 z): on a hit the polygon's
 * normal (+0x24..0x2C) at 0x700038A0 and 001F9140(the player, the hit,
 * 0x700038A0, the colour, 7.0); t += 1/120, at t >= 1.5 +0xB0 = the point
 * and +4 = 2; 001B17A0(self).
 * +4 2: 001EFD20(0x8000002F (+0xD 7 / 10) or 0x80000013 (8 / 9), +0xB0)
 * and 001F02C0(+0xB0, 0x449, 100), +4 = 3. +4 3: 001AFC10(self). */
int em_level11_port_00828500(const EmLevel11PortHooks *h, uint32_t self, uint32_t sp, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    uint32_t blk = self + 0x1F0;
    uint32_t col = sp - 0x10;
    uint8_t q[16];
    l11_q(&o, 0x0082D3B0u, q);
    l11_wq(&o, col, q);
    int32_t r = 0;
    uint32_t f = 0;
    uint32_t state = l11_u8(&o, self + 4);
    if (state == 0) {
        switch (l11_u8(&o, self + 0xD)) {
        case 7: l11_w32(&o, blk + 0x20, 0x15); break;
        case 8: l11_w32(&o, blk + 0x20, 0x16); break;
        case 9: l11_w32(&o, blk + 0x20, 0x17); break;
        case 10: l11_w32(&o, blk + 0x20, 0x15); break;
        default: break;
        }
        switch (l11_u8(&o, self + 0xD)) {
        case 7:
            l11_w32(&o, self + 0x60, 0x3F000000u);
            l11_w32(&o, self + 0x64, 0x3F000000u);
            l11_w32(&o, self + 0x68, 0x3F000000u);
            l11_c_00102948(&o, blk + 0x10, D_00810360);
            break;
        case 8:
            l11_c_00102948(&o, blk + 0x10, D_00810360);
            break;
        case 9: {
            uint32_t a = L11_MUL(F_TWO_PI, l11_unit_random(&o));
            l11_w32(&o, S_70003A20, a);
            uint32_t s = 0;
            if (l11_c_0011E2A8(&o, a, &s)) break;
            a = l11_u32(&o, S_70003A20);
            l11_w32(&o, S_700038A0, L11_MUL(0x42480000u, s)); /* 50.0 */
            if (l11_c_0011DE90(&o, a, &s)) break;
            l11_w32(&o, S_700038A0 + 8, L11_MUL(0x42480000u, s));
            l11_w32(&o, S_700038A0 + 4, F_ZERO);
            l11_c_001028B8(&o, blk + 0x10, S_700038A0, D_00810360);
            break;
        }
        case 10:
            l11_w32(&o, blk + 0x10, 0x4437C000u); /* 735.0 */
            l11_w32(&o, blk + 0x14, 0x43610000u); /* 225.0 */
            l11_w32(&o, blk + 0x18, 0x449C4000u); /* 1250.0 */
            l11_w32(&o, blk + 0x1C, F_ONE);
            break;
        default:
            break;
        }
        l11_c_00102948(&o, blk, self + 0xB0);
        l11_w32(&o, self + 0xC0, F_ZERO);
        l11_w32(&o, self + 0xC4, L11_SUB(L11_MUL(F_TWO_PI, l11_unit_random(&o)), F_PI));
        l11_w32(&o, self + 0xC8, L11_SUB(L11_MUL(F_TWO_PI, l11_unit_random(&o)), F_PI));
        l11_w32(&o, blk + 0x24, 0x4B0);
        l11_w32(&o, blk + 0x28, F_ZERO);
        l11_w32(&o, blk + 0x2C, F_ZERO);
        l11_w8(&o, self + 4, 1);
        l11_w32(&o, self + 0x30, 0x002759C8u);
        l11_w32(&o, self + 0x34, 0x008284E0u);
        l11_w8(&o, self, 1);
        state = 1;
    }
    switch (state) {
    case 1:
        if (l11_u8(&o, 0x008106B8u) == 2u && l11_s16(&o, 0x0028A9A0u) == 2) {
            l11_w8(&o, self + 4, 3);
            break;
        }
        if (l11_u8(&o, D_00810833) == 0xFFu) {
            switch (l11_u8(&o, self + 0xD)) {
            case 7:
            case 8: l11_w8(&o, self + 4, 3); break;
            default: break;
            }
        }
        l11_w32(&o, blk + 0x24, (uint32_t)(l11_s32(&o, blk + 0x24) - 1));
        if (l11_s32(&o, blk + 0x24) < 0) l11_w8(&o, self + 4, 3);
        l11_w32(&o, self + 0xC0, L11_DIV(L11_MUL(F_PI, l11_u32(&o, blk + 0x28)), 0x43340000u)); /* 180.0 */
        uint32_t ang = L11_ADD(l11_u32(&o, blk + 0x28), 0x40400000u); /* 3.0 */
        l11_w32(&o, blk + 0x28, ang);
        if (!L11_LE(ang, 0x43340000u)) /* 180.0 */
            l11_w32(&o, blk + 0x28, L11_SUB(l11_u32(&o, blk + 0x28), 0x43B40000u)); /* 360.0 */
        l11_c_001028D0(&o, S_700038A0, blk + 0x10, blk);
        l11_c_00102900(&o, S_700038A0, S_700038A0, l11_u32(&o, blk + 0x2C));
        l11_c_001028B8(&o, S_700038A0, S_700038A0, blk);
        if (l11_c_0011E2A8(&o, L11_MUL(F_PI, l11_u32(&o, blk + 0x2C)), &f)) break;
        l11_w32(&o, S_700038A0 + 4, L11_ADD(l11_u32(&o, S_700038A0 + 4), L11_MUL(0x42C80000u, f))); /* 100.0 */
        l11_w32(&o, S_700038A0 + 12, F_ONE);
        if (!L11_LE(l11_u32(&o, blk + 0x2C), 0x3F000000u)) { /* 0.5 */
            if (l11_c_0019A570(&o, self + 0xB0, S_700038A0, 6, 0, &r)) break;
            if (r != 0) {
                l11_c_00102948(&o, self + 0xB0, S_700031B0);
                l11_w8(&o, self + 4, 2);
                break;
            }
        }
        l11_c_00102948(&o, self + 0xB0, S_700038A0);
        l11_c_001029C0(&o, S_700036A0);
        l11_c_00102C58(&o, S_700036A0, S_700036A0, self + 0xC0);
        l11_c_00102918(&o, S_700036A0, S_700036A0, S_700038A0);
        l11_w32(&o, S_700038B0, F_ONE);
        l11_w32(&o, S_700038B0 + 4, F_ONE);
        l11_w32(&o, S_700038B0 + 8, F_ONE);
        l11_w32(&o, S_700038B0 + 12, F_ONE);
        if (l11_c_001CA7B0(&o, S_700036D0, 0x41200000u, &r)) break; /* 10.0 */
        if (r >= 0) {
            int32_t hnd = r;
            l11_c_001C7900(&o, S_700036A0, S_700038B0, 0x3F5, 0);
            uint32_t model = 0;
            int32_t kind = l11_s32(&o, blk + 0x20);
            if (l11_c_001C6120(&o, l11_u32(&o, 0x0028A59Cu), kind, &model)) break;
            l11_c_001CA940(&o, hnd, (int32_t)model);
        }
        l11_w32(&o, S_700038A0, l11_u32(&o, self + 0xB0));
        l11_w32(&o, S_700038A0 + 4, 0xC61C4000u); /* -10000.0 */
        l11_w32(&o, S_700038A0 + 8, l11_u32(&o, self + 0xB8));
        l11_w32(&o, S_700038A0 + 12, F_ONE);
        if (l11_c_0019A570(&o, self + 0xB0, S_700038A0, 6, 0, &r)) break;
        if (r != 0) {
            uint32_t hit = l11_u32(&o, S_700031D0);
            l11_w32(&o, S_700038A0, l11_u32(&o, hit + 0x24));
            l11_w32(&o, S_700038A0 + 4, l11_u32(&o, hit + 0x28));
            l11_w32(&o, S_700038A0 + 8, l11_u32(&o, hit + 0x2C));
            l11_001F9140(&o, D_00810360, S_700031B0, S_700038A0, col, 0x40E00000u, sp - 0x50); /* 7.0 */
        }
        uint32_t t = L11_ADD(l11_u32(&o, blk + 0x2C), 0x3C088889u); /* 0.008333334 */
        l11_w32(&o, blk + 0x2C, t);
        if (!L11_LT(t, 0x3FC00000u)) { /* 1.5 */
            l11_c_00102948(&o, self + 0xB0, S_700038A0);
            l11_w8(&o, self + 4, 2);
        }
        l11_c_001B17A0(&o, self, &r);
        break;
    case 2:
        switch (l11_u8(&o, self + 0xD)) {
        case 7:
        case 10:
            l11_c_001EFD20(&o, (int32_t)0x8000002Fu, self + 0xB0);
            l11_c_001F02C0(&o, self + 0xB0, 0x449, 0x42C80000u);
            break;
        case 8:
        case 9:
            l11_c_001EFD20(&o, (int32_t)0x80000013u, self + 0xB0);
            l11_c_001F02C0(&o, self + 0xB0, 0x449, 0x42C80000u);
            break;
        default:
            break;
        }
        l11_w8(&o, self + 4, 3);
        break;
    case 3:
        l11_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l11_end(&o);
}

/* The effects' matrix: 001029C0(+0xD0), 00102C58(+0xD0, +0xD0, +0xC0),
 * 00102918(+0xD0, +0xD0, +0xB0). */
static void l11_effect_matrix(L11 *o, uint32_t self)
{
    l11_c_001029C0(o, self + 0xD0);
    l11_c_00102C58(o, self + 0xD0, self + 0xD0, self + 0xC0);
    l11_c_00102918(o, self + 0xD0, self + 0xD0, self + 0xB0);
}

/* step + (target - step) / 8, floored at `floor_`. */
static uint32_t l11_ease(L11 *o, uint32_t at, uint32_t floor_)
{
    uint32_t s = l11_u32(o, at);
    s = L11_ADD(s, L11_DIV(L11_SUB(floor_, s), 0x41000000u)); /* 8.0 */
    l11_w32(o, at, s);
    if (L11_LT(s, floor_)) s = floor_;
    l11_w32(o, at, s);
    return s;
}

/* ------------------------------------------------------------------------
 * 0x828C60: an effect. w = self + 0x1F0. +4 0: the matrix
 * (l11_effect_matrix), w[0] = 0, w[1] = 0.1, w[2] = random / 2^31, +4 = 1;
 * then as 1. +4 1: h = 001CCF70(+0x100), 001CFB50(the packet in the frame,
 * 0, +0xD0, w[0], w[2], 1, 1e-6, 20), 001CFBE0(h, 1, 0x82D3C0, the packet,
 * 0); w[1] eases toward 0.005 (floored there), w[0] += w[1]; at w[0] >=
 * 1.8 +4 = 3. +4 2 / 3: 001AFC10(self). */
int em_level11_port_00828C60(const EmLevel11PortHooks *h, uint32_t self, uint32_t sp, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    uint32_t w = self + 0x1F0;
    uint32_t pkt = sp - 0x60;
    uint32_t state = l11_u8(&o, self + 4);
    if (state == 0) {
        l11_effect_matrix(&o, self);
        l11_w32(&o, w, F_ZERO);
        l11_w32(&o, w + 4, 0x3DCCCCCDu); /* 0.1 */
        l11_w32(&o, w + 8, l11_unit_random(&o));
        l11_w8(&o, self + 4, 1);
        state = 1;
    }
    switch (state) {
    case 1: {
        int32_t hnd = 0;
        if (l11_c_001CCF70(&o, self + 0x100, &hnd)) break;
        uint32_t w0 = l11_u32(&o, w);
        uint32_t w2 = l11_u32(&o, w + 8);
        l11_c_001CFB50(&o, pkt, 0, self + 0xD0, w0, w2, F_ONE, 0x358637BDu, 0x41A00000u); /* 1e-6, 20.0 */
        l11_c_001CFBE0(&o, hnd, 1, 0x0082D3C0u, pkt, 0);
        uint32_t s = l11_ease(&o, w + 4, 0x3BA3D70Au); /* 0.005 */
        uint32_t t = L11_ADD(l11_u32(&o, w), s);
        l11_w32(&o, w, t);
        if (!L11_LT(t, 0x3FE66666u)) l11_w8(&o, self + 4, 3); /* 1.8 */
        break;
    }
    case 2:
    case 3:
        l11_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l11_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x828E10: an effect. w = self + 0x1F0. +4 0: the matrix, w[0] = 0, w[1] =
 * random / 2^31, +4 = 1; then as 1. +4 1: 001D04B0(+0xD0, 5, 0x82D450,
 * w[0], w[1]), w[0] += 0.008, past 1.5 +4 = 3. +4 2 / 3: 001AFC10. */
int em_level11_port_00828E10(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    uint32_t w = self + 0x1F0;
    uint32_t state = l11_u8(&o, self + 4);
    if (state == 0) {
        l11_effect_matrix(&o, self);
        l11_w32(&o, w, F_ZERO);
        l11_w32(&o, w + 4, l11_unit_random(&o));
        l11_w8(&o, self + 4, 1);
        state = 1;
    }
    switch (state) {
    case 1: {
        uint32_t w0 = l11_u32(&o, w);
        uint32_t w1 = l11_u32(&o, w + 4);
        l11_c_001D04B0(&o, self + 0xD0, 5, 0x0082D450u, w0, w1);
        uint32_t t = L11_ADD(l11_u32(&o, w), 0x3C03126Fu); /* 0.008 */
        l11_w32(&o, w, t);
        if (!L11_LE(t, 0x3FC00000u)) l11_w8(&o, self + 4, 3); /* 1.5 */
        break;
    }
    case 2:
    case 3:
        l11_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l11_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x828F40: an effect. blk = self + 0x1F0, pb = (+0x14) + 0x1F0. +4 0: the
 * matrix, blk +0x18 = 0, +0x1C = 0, +0x20 = 0.1, +0x14 = 00122BB8(), +4 =
 * 1, +0 = 1, +0x30 = pb, +0x34 = 0x828F30; then as 1. +4 1: eight packets
 * on a circle of radius 10 (0x70003A20 from -pi by pi/4: the matrix at
 * 0x700036A0 turned by it (00102BB0), (0, 0, 10, 1) through it plus +0xB0,
 * 00102918, h = 001CCF70(0x700036D0), r = ((seed >> 16) & 0xFFFF) / 65535
 * + 0.0001 with seed = seed * 37 + 11, 001CFA60(the packet, 0x700036A0,
 * blk +0x1C, r), 001CFBE0(h + 0x2000, 5, 0x82D4E0, the packet, 0)); +0x20
 * eases toward 0.01; +0x1C += it; at >= 1.5 +4 = 3, +0 = 2; else blk +0 /
 * +4 / +8 = 0.666666 * (100, 50, 100) * +0x1C, +0x18 += 1 (past 40 +0 =
 * 2), 001B1B70(self). +4 2 / 3: 001AFC10. */
int em_level11_port_00828F40(const EmLevel11PortHooks *h, uint32_t self, uint32_t sp, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    uint32_t blk = self + 0x1F0;
    uint32_t pb = l11_u32(&o, self + 0x14) + 0x1F0;
    uint32_t pkt = sp - 0x60;
    uint32_t state = l11_u8(&o, self + 4);
    if (state == 0) {
        l11_effect_matrix(&o, self);
        l11_w32(&o, blk + 0x18, 0);
        l11_w32(&o, blk + 0x1C, F_ZERO);
        l11_w32(&o, blk + 0x20, 0x3DCCCCCDu); /* 0.1 */
        int32_t seed0 = 0;
        if (!l11_c_00122BB8(&o, &seed0)) l11_w32(&o, blk + 0x14, (uint32_t)seed0);
        l11_w8(&o, self + 4, 1);
        l11_w8(&o, self, 1);
        l11_w32(&o, self + 0x30, pb);
        l11_w32(&o, self + 0x34, 0x00828F30u);
        state = 1;
    }
    switch (state) {
    case 1: {
        uint32_t seed = l11_u32(&o, blk + 0x14);
        l11_w32(&o, S_70003A20, 0xC0490FDBu); /* -pi */
        for (int i = 0; i < 8; i++) {
            if (l11_failed(&o)) break;
            l11_c_001029C0(&o, S_700036A0);
            l11_c_00102BB0(&o, S_700036A0, S_700036A0, l11_u32(&o, S_70003A20));
            l11_w32(&o, S_700038A0, F_ZERO);
            l11_w32(&o, S_700038A0 + 4, F_ZERO);
            l11_w32(&o, S_700038A0 + 8, 0x41200000u); /* 10.0 */
            l11_w32(&o, S_700038A0 + 12, F_ONE);
            l11_c_001026A0(&o, S_700038A0, S_700036A0, S_700038A0);
            l11_c_001028B8(&o, S_700038A0, S_700038A0, self + 0xB0);
            l11_c_00102918(&o, S_700036A0, S_700036A0, S_700038A0);
            int32_t hnd = 0;
            if (l11_c_001CCF70(&o, S_700036D0, &hnd)) break;
            uint32_t r = L11_CVT_S_W((uint32_t)l11_sra(seed, 16) & 0xFFFFu);
            r = L11_DIV(r, 0x477FFF00u);           /* 65535.0 */
            r = L11_ADD(r, 0x38D1B717u);           /* 0.0001 */
            seed = seed * 37u + 11u;
            l11_c_001CFA60(&o, pkt, S_700036A0, l11_u32(&o, blk + 0x1C), r);
            l11_c_001CFBE0(&o, hnd + 0x2000, 5, 0x0082D4E0u, pkt, 0);
            l11_w32(&o, S_70003A20, L11_ADD(l11_u32(&o, S_70003A20), 0x3F490FDBu)); /* pi / 4 */
        }
        if (l11_failed(&o)) break;
        uint32_t s = l11_ease(&o, blk + 0x20, 0x3C23D70Au); /* 0.01 */
        uint32_t t = L11_ADD(l11_u32(&o, blk + 0x1C), s);
        l11_w32(&o, blk + 0x1C, t);
        if (!L11_LT(t, 0x3FC00000u)) { /* 1.5 */
            l11_w8(&o, self + 4, 3);
            l11_w8(&o, self, 2);
            break;
        }
        l11_w32(&o, blk, L11_MUL(0x3F2AAA9Fu, L11_MUL(0x42C80000u, t)));
        l11_w32(&o, blk + 4, L11_MUL(0x3F2AAA9Fu, L11_MUL(0x42480000u, l11_u32(&o, blk + 0x1C))));
        l11_w32(&o, blk + 8, L11_MUL(0x3F2AAA9Fu, L11_MUL(0x42C80000u, l11_u32(&o, blk + 0x1C))));
        l11_w32(&o, blk + 0x18, (uint32_t)(l11_s32(&o, blk + 0x18) + 1));
        if (l11_s32(&o, blk + 0x18) > 0x28) l11_w8(&o, self, 2);
        l11_c_001B1B70(&o, self);
        break;
    }
    case 2:
    case 3:
        l11_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l11_end(&o);
}
