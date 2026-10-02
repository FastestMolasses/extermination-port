/* The twelfth level: the AREA19 creature family run by 001386E0 (the
 * three 0x1383C0 actors of AREA19 from entry 10): its behaviours 0 / 1
 * (00138900, 00138C20), the shared post-step 0013BA20, the pose pick
 * 0013BBB0, the drift 0013BE60, the ground probe 0013BF20 and the steering
 * family 0013C8C0 / 0013CD50 / 0013C4C0 / 0013C1F0 / 0013D220.
 * docs/LEVEL12_PORT.md section 2. self = the pool node, ent = self + 0x1F0
 * (the behaviour block). Ground truth: the original instructions (the
 * test runs every entry against them); where the decomp's text differs the
 * original wins (docs/LEVEL12_PORT.md section 0). */
#include "em_level12_port_internal.h"

#define SP_38A0 0x700038A0u
#define SP_38B0 0x700038B0u
#define SP_38D0 0x700038D0u
#define SP_36A0 0x700036A0u
#define SP_31B0 0x700031B0u
#define SP_FRAME 0x70003B68u
#define SP_B8A 0x70003B8Au

#define F_PI_8 0x3EC90FDBu
#define F_MPI_8 0xBEC90FDBu
#define F_PI_2 0x3FC90FDBu
#define F_MPI_2 0xBFC90FDBu
#define F_PI 0x40490FDBu
#define F_25 0x41C80000u
#define F_20 0x41A00000u
#define F_300 0x43960000u

/* The four words (x, y, z, w) of a scratch vector. */
static void l12_vec4(L12 *o, uint32_t a, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    l12_w32(o, a + 0, x);
    l12_w32(o, a + 4, y);
    l12_w32(o, a + 8, z);
    l12_w32(o, a + 12, w);
}

/* ------------------------------------------------------------------------
 * 0013BA20 (self, ent): the lean. The halfword self +0x2C, sign-extended
 * and masked with 0xFFFF7FFF (so 0x8000 does not clear to 0), 0 or 4:
 * ent +0x60 = self +0xC4. d = 001B1470(self +0xC4 - ent +0x60), clamped to
 * +-0.034906585 when |d| (0011DF78) is above it; the rate by the sign of d
 * and of self +0xC8 (0x3D07D419 or 0x3D7A35DE); self +0xC8 =
 * 001B12B0(-(1.5533431 * (d / 0.034906585)), self +0xC8, rate); ent +0x60
 * = self +0xC4.
 * ---------------------------------------------------------------------- */
void l12_0013BA20(L12 *o, uint32_t self, uint32_t ent)
{
    uint32_t flag = (uint32_t)l12_s16(o, self + 0x2C) & 0xFFFF7FFFu;
    if (flag == 0 || flag == 4) l12_w32(o, ent + 0x60, l12_u32(o, self + 0xC4));
    uint32_t c4 = l12_u32(o, self + 0xC4);
    uint32_t e60 = l12_u32(o, ent + 0x60);
    uint32_t d, a, rate;
    if (l12_c_001B1470(o, L12_SUB(c4, e60), &d)) return;
    if (l12_c_0011DF78(o, d, &a)) return;
    if (!L12_LE(a, 0x3D0EFA35u)) d = L12_LT(d, 0) ? 0xBD0EFA35u : 0x3D0EFA35u;
    if (L12_LT(d, 0)) {
        uint32_t c8 = l12_u32(o, self + 0xC8);
        rate = L12_LT(c8, 0) ? 0x3D7A35DEu : 0x3D07D419u;
    } else {
        uint32_t c8 = l12_u32(o, self + 0xC8);
        rate = L12_LE(c8, 0) ? 0x3D07D419u : 0x3D7A35DEu;
    }
    uint32_t c8 = l12_u32(o, self + 0xC8);
    uint32_t goal = L12_NEG(L12_MUL(0x3FC6D3F2u, L12_DIV(d, 0x3D0EFA35u)));
    uint32_t r;
    if (l12_c_001B12B0(o, goal, c8, rate, &r)) return;
    l12_w32(o, self + 0xC8, r);
    l12_w32(o, ent + 0x60, l12_u32(o, self + 0xC4));
}

int em_level12_port_0013BA20(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_0013BA20(&o, self, ent);
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 00138900 (self, ent): behaviour 0 by the sub-state self +6.
 *   0: ent +0x20 counts down; at 0 without bit 0 of self +0xD: +6 = 4;
 *      with it: +6 = 1, ent +0x44 <= 0 -> ent +0x44 = 0, else ent +0x48 =
 *      -0.01; ent +0x5C = pi/2; 001C67E0(self, 4, 0, 0).
 *   1: ent +0x44 <= 0 -> ent +0x48 = ent +0x44 = 0; on bit 2 of ent +0x81:
 *      +6 += 1, ent +0x20 = 0, +0x50 = +0x5C = +0x44 = +0x48 = 0, self
 *      +0xB4 -= 2.5, 001C67E0(self, 2, 0, 0).
 *   2: ent +0x20 + 1 while D_008106C7 is set and 001B15D0(self +0xB0,
 *      0x810360) <= 150, else 0; then on bit 0 of self +0xA or a nonzero
 *      count: +6 += 1, ent +0x20 = 0, 001C67E0(self, 3, 0, 0),
 *      001FBD50(self, 0x816, 0, 300).
 *   3: self +0x3C <= 25 -> ent +0x5C = -pi/2; on bit 0x1000 of ent +0x70:
 *      +5 = 2, +6 = 0, ent +0x5C = 0, +0x30 = 0, +0x44 = 0.4, +0x22 =
 *      +0x20 = 0, 001C67E0(self, 1, 0, 0).
 *   4: +5 += 1, +6 = 0, ent +0x5C = 0, +0x30 = 0, +0x44 = 0.4, +0x24 =
 *      +0x22 = +0x20 = 0, 001C67E0(self, 1, 0, 0).
 * Then ent +0x50 = 001B12B0(ent +0x5C, ent +0x50, pi/120) and 0013BA20.
 * ---------------------------------------------------------------------- */
void l12_00138900(L12 *o, uint32_t self, uint32_t ent)
{
    uint32_t st = l12_u8(o, self + 6);
    switch (st) {
    case 0: {
        uint32_t c = l12_u16(o, ent + 0x20);
        if (c != 0) {
            l12_w16(o, ent + 0x20, c - 1u);
            break;
        }
        if (!(l12_u8(o, self + 0xD) & 1u)) {
            l12_w8(o, self + 6, 4);
            break;
        }
        l12_w8(o, self + 6, st + 1u);
        if (L12_LE(l12_u32(o, ent + 0x44), 0)) l12_w32(o, ent + 0x44, 0);
        else l12_w32(o, ent + 0x48, 0xBC23D70Au);
        l12_w32(o, ent + 0x5C, F_PI_2);
        if (l12_c_001C67E0(o, self, 4, 0, 0)) return;
        break;
    }
    case 1:
        if (L12_LE(l12_u32(o, ent + 0x44), 0)) {
            l12_w32(o, ent + 0x48, 0);
            l12_w32(o, ent + 0x44, 0);
        }
        if (l12_s8(o, ent + 0x81) & 4) {
            l12_w8(o, self + 6, l12_u8(o, self + 6) + 1u);
            l12_w16(o, ent + 0x20, 0);
            l12_w32(o, ent + 0x50, 0);
            l12_w32(o, ent + 0x5C, 0);
            l12_w32(o, ent + 0x44, 0);
            l12_w32(o, ent + 0x48, 0);
            l12_w32(o, self + 0xB4, L12_SUB(l12_u32(o, self + 0xB4), 0x40200000u));
            if (l12_c_001C67E0(o, self, 2, 0, 0)) return;
        }
        break;
    case 2: {
        int near = 0;
        if (l12_u8(o, 0x8106C7u) != 0) {
            uint32_t d;
            if (l12_c_001B15D0(o, self + 0xB0, 0x810360u, &d)) return;
            near = L12_LE(d, 0x43160000u);
        }
        if (near) l12_w16(o, ent + 0x20, l12_u16(o, ent + 0x20) + 1u);
        else l12_w16(o, ent + 0x20, 0);
        if ((l12_u8(o, self + 0xA) & 1u) || l12_u16(o, ent + 0x20) != 0) {
            l12_w8(o, self + 6, l12_u8(o, self + 6) + 1u);
            l12_w16(o, ent + 0x20, 0);
            if (l12_c_001C67E0(o, self, 3, 0, 0)) return;
            if (l12_c_001FBD50(o, self, 0x816, 0, F_300)) return;
        }
        break;
    }
    case 3:
        if (L12_LE(l12_u32(o, self + 0x3C), F_25)) l12_w32(o, ent + 0x5C, F_MPI_2);
        if (l12_u32(o, ent + 0x70) & 0x1000u) {
            l12_w8(o, self + 5, 2);
            l12_w8(o, self + 6, 0);
            l12_w32(o, ent + 0x5C, 0);
            l12_w16(o, ent + 0x30, 0);
            l12_w32(o, ent + 0x44, 0x3ECCCCCDu);
            l12_w16(o, ent + 0x22, 0);
            l12_w16(o, ent + 0x20, 0);
            if (l12_c_001C67E0(o, self, 1, 0, 0)) return;
        }
        break;
    case 4:
        l12_w8(o, self + 5, l12_u8(o, self + 5) + 1u);
        l12_w8(o, self + 6, 0);
        l12_w32(o, ent + 0x5C, 0);
        l12_w16(o, ent + 0x30, 0);
        l12_w32(o, ent + 0x44, 0x3ECCCCCDu);
        l12_w16(o, ent + 0x24, 0);
        l12_w16(o, ent + 0x22, 0);
        l12_w16(o, ent + 0x20, 0);
        if (l12_c_001C67E0(o, self, 1, 0, 0)) return;
        break;
    default:
        break;
    }
    {
        uint32_t goal = l12_u32(o, ent + 0x5C);
        uint32_t cur = l12_u32(o, ent + 0x50);
        uint32_t r;
        if (l12_c_001B12B0(o, goal, cur, 0x3CD67750u, &r)) return;
        l12_w32(o, ent + 0x50, r);
    }
    l12_0013BA20(o, self, ent);
}

int em_level12_port_00138900(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_00138900(&o, self, ent);
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 0013BE60 (self, ent): the drift, skipped while ent +0x82 is set or self
 * +4 / +5 are 1 / 3: ent +0x44 += ent +0x48; self +0xB4 += ent +0x4C *
 * 0011E2A8(ent +0x50); self +0xB0 += ent +0x44 * 0011E2A8(self +0xC4);
 * self +0xB8 += ent +0x44 * 0011DE90(self +0xC4).
 * ---------------------------------------------------------------------- */
void l12_0013BE60(L12 *o, uint32_t self, uint32_t ent)
{
    uint32_t r;
    if (l12_s8(o, ent + 0x82) != 0) return;
    if (l12_u8(o, self + 4) == 1u && l12_u8(o, self + 5) == 3u) return;
    {
        uint32_t f48 = l12_u32(o, ent + 0x48);
        uint32_t f44 = l12_u32(o, ent + 0x44);
        l12_w32(o, ent + 0x44, L12_ADD(f44, f48));
    }
    if (l12_c_0011E2A8(o, l12_u32(o, ent + 0x50), &r)) return;
    {
        uint32_t k = l12_u32(o, ent + 0x4C);
        uint32_t y = l12_u32(o, self + 0xB4);
        l12_w32(o, self + 0xB4, L12_ADD(y, L12_MUL(k, r)));
    }
    if (l12_c_0011E2A8(o, l12_u32(o, self + 0xC4), &r)) return;
    {
        uint32_t k = l12_u32(o, ent + 0x44);
        uint32_t x = l12_u32(o, self + 0xB0);
        l12_w32(o, self + 0xB0, L12_ADD(x, L12_MUL(k, r)));
    }
    if (l12_c_0011DE90(o, l12_u32(o, self + 0xC4), &r)) return;
    {
        uint32_t k = l12_u32(o, ent + 0x44);
        uint32_t z = l12_u32(o, self + 0xB8);
        l12_w32(o, self + 0xB8, L12_ADD(z, L12_MUL(k, r)));
    }
}

int em_level12_port_0013BE60(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_0013BE60(&o, self, ent);
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 0013BBB0 (self, ent, sp): the pose by the lean. k = 1 when ent +0x5C <
 * ent +0x50; otherwise 4 unless ent +0x50 < pi/4, where with d = +0x5C -
 * +0x50: d == 0 -> (+0x5C <= 0 ? 1 : 0), else +0x5C <= 0 -> 1, d < pi/4
 * -> 0, else 4. Nothing when (self +0x2C & 0x7FFF) is k. Else t =
 * cvt(001C6160(self)) - self +0x3C, 0011E0A8(t, the frame word at sp -
 * 4); by the current pose: 0 -> k 1 ? (001281C0(that word) == 0x2C:
 * 001C67E0(self, k, 0, 0)) : 001C67E0(self, k, 0, min((1 + t) / 2, 23));
 * 4 -> k 1 ? (== 0x16: the same) : 001C67E0(self, k, 0, min(2 (1 + t),
 * 45)); any other -> 001C67E0(self, k, 5, 0).
 * ---------------------------------------------------------------------- */
void l12_0013BBB0(L12 *o, uint32_t self, uint32_t ent, uint32_t sp)
{
    uint32_t f5c = l12_u32(o, ent + 0x5C);
    uint32_t f50 = l12_u32(o, ent + 0x50);
    int32_t k;
    if (L12_LT(f5c, f50)) {
        k = 1;
    } else if (!L12_LT(f50, 0x3F490FDBu)) {
        k = 4;
    } else {
        uint32_t d = L12_SUB(f5c, f50);
        if (L12_EQ(0, d)) k = L12_LE(f5c, 0) ? 1 : 0;
        else if (L12_LE(f5c, 0)) k = 1;
        else k = L12_LT(d, 0x3F490FDBu) ? 0 : 4;
    }
    {
        int32_t cur = (int16_t)((uint32_t)l12_s16(o, self + 0x2C) & 0xFFFF7FFFu);
        if (cur == k) return;
    }
    uint32_t word = sp - 4u;
    int32_t v;
    if (l12_c_001C6160(o, self, &v)) return;
    uint32_t t = L12_SUB(L12_CVT_S_W(v), l12_u32(o, self + 0x3C));
    {
        uint32_t ignored;
        if (l12_c_0011E0A8(o, word, t, &ignored)) return;
    }
    int32_t cur = (int16_t)((uint32_t)l12_s16(o, self + 0x2C) & 0xFFFF7FFFu);
    if (cur == 0) {
        if (k == 1) {
            int32_t n;
            if (l12_c_001281C0(o, l12_u32(o, word), &n)) return;
            if (n != 0x2C) return;
            l12_c_001C67E0(o, self, k, 0, 0);
            return;
        }
        uint32_t f13 = L12_DIV(L12_ADD(F_ONE, t), 0x40000000u);
        if (!L12_LE(f13, 0x41B80000u)) f13 = 0x41B80000u;
        l12_c_001C67E0(o, self, k, 0, f13);
        return;
    }
    if (cur == 4) {
        if (k == 1) {
            int32_t n;
            if (l12_c_001281C0(o, l12_u32(o, word), &n)) return;
            if (n != 0x16) return;
            l12_c_001C67E0(o, self, k, 0, 0);
            return;
        }
        uint32_t f13 = L12_MUL(0x40000000u, L12_ADD(F_ONE, t));
        if (!L12_LE(f13, 0x42340000u)) f13 = 0x42340000u;
        l12_c_001C67E0(o, self, k, 0, f13);
        return;
    }
    l12_c_001C67E0(o, self, k, 0x40A00000u, 0);
}

int em_level12_port_0013BBB0(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                             EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_0013BBB0(&o, self, ent, sp);
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 0013BF20 (self, ent): the ground probes into ent +0x81. ent +0x81 = 0;
 * the point (0, 0, 3) through 001B2B10(self) and 001028B8(+ self +0xB0);
 * 0019AD00(self, it, 0x80000006) set -> bit 0; else the point (+-6, 0, 0)
 * by bit 0 of the frame counter 0x70003B68 the same way (bit 0 again).
 * Then by ent +0x50 <= 0: (0, -4, 0) / (0, -8, 0), state 2, or (0, 2, 0)
 * / (0, 4, 0), state 4 (its first y + 0.003 after the shift by self
 * +0xB0); 0019AB20(self, 0x700038A0, 0x700038B0, 0x80000007) set -> the
 * state's bit.
 * ---------------------------------------------------------------------- */
void l12_0013BF20(L12 *o, uint32_t self, uint32_t ent)
{
    int32_t r;
    l12_w8(o, ent + 0x81, 0);
    l12_vec4(o, SP_38A0, 0, 0, 0x40400000u, F_ONE);
    if (l12_c_001B2B10(o, self, SP_38A0, SP_38A0)) return;
    if (l12_c_001028B8(o, SP_38A0, self + 0xB0, SP_38A0)) return;
    if (l12_c_0019AD00(o, self, SP_38A0, (int32_t)0x80000006u, &r)) return;
    if (r != 0) {
        l12_w8(o, ent + 0x81, (uint32_t)l12_s8(o, ent + 0x81) | 1u);
    } else {
        if (l12_u32(o, SP_FRAME) & 1u) l12_vec4(o, SP_38A0, 0x40C00000u, 0, 0, F_ONE);
        else l12_vec4(o, SP_38A0, 0xC0C00000u, 0, 0, F_ONE);
        if (l12_c_001B2B10(o, self, SP_38A0, SP_38A0)) return;
        if (l12_c_001028B8(o, SP_38A0, self + 0xB0, SP_38A0)) return;
        if (l12_c_0019AD00(o, self, SP_38A0, (int32_t)0x80000006u, &r)) return;
        if (r != 0) l12_w8(o, ent + 0x81, (uint32_t)l12_s8(o, ent + 0x81) | 1u);
    }
    uint32_t st;
    if (L12_LE(l12_u32(o, ent + 0x50), 0)) {
        l12_w32(o, SP_38A0, 0);
        l12_w32(o, SP_38B0, 0);
        l12_w32(o, SP_38A0 + 4, 0xC0800000u);
        l12_w32(o, SP_38A0 + 8, 0);
        l12_w32(o, SP_38A0 + 12, F_ONE);
        l12_w32(o, SP_38B0 + 4, 0xC1000000u);
        l12_w32(o, SP_38B0 + 8, 0);
        st = 2;
        l12_w32(o, SP_38B0 + 12, F_ONE);
        if (l12_c_001028B8(o, SP_38A0, SP_38A0, self + 0xB0)) return;
    } else {
        l12_w32(o, SP_38A0, 0);
        l12_w32(o, SP_38B0, 0);
        l12_w32(o, SP_38A0 + 4, 0x40000000u);
        l12_w32(o, SP_38A0 + 8, 0);
        l12_w32(o, SP_38A0 + 12, F_ONE);
        l12_w32(o, SP_38B0 + 4, 0x40800000u);
        l12_w32(o, SP_38B0 + 8, 0);
        st = 4;
        l12_w32(o, SP_38B0 + 12, F_ONE);
        if (l12_c_001028B8(o, SP_38A0, SP_38A0, self + 0xB0)) return;
        l12_w32(o, SP_38A0 + 4, L12_ADD(l12_u32(o, SP_38A0 + 4), 0x3B449BA6u));
    }
    if (l12_c_0019AB20(o, self, SP_38A0, SP_38B0, (int32_t)0x80000007u, &r)) return;
    if (r != 0) l12_w8(o, ent + 0x81, (uint32_t)l12_s8(o, ent + 0x81) | st);
}

int em_level12_port_0013BF20(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_0013BF20(&o, self, ent);
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 0013D220 (self): the point self +0xB0 .. +0xB8 (w 1) at 0x700038E0 with
 * y - 10; 1 when 0019B6C0(self +0xB0, it) hits and the object at
 * 0x700031D0 has +0x1A == 0x5B, else 0.
 * ---------------------------------------------------------------------- */
int32_t l12_0013D220(L12 *o, uint32_t self)
{
    int32_t r;
    l12_w32(o, 0x700038E0u, l12_u32(o, self + 0xB0));
    l12_w32(o, 0x700038E4u, l12_u32(o, self + 0xB4));
    l12_w32(o, 0x700038E8u, l12_u32(o, self + 0xB8));
    l12_w32(o, 0x700038ECu, F_ONE);
    l12_w32(o, 0x700038E4u, L12_SUB(l12_u32(o, 0x700038E4u), 0x41200000u));
    if (l12_c_0019B6C0(o, self + 0xB0, 0x700038E0u, &r)) return 0;
    if (r == 0) return 0;
    {
        uint32_t hit = l12_u32(o, 0x700031D0u);
        return l12_u8(o, hit + 0x1A) == 0x5Bu ? 1 : 0;
    }
}

int em_level12_port_0013D220(const EmLevel12PortHooks *h, uint32_t self, int32_t *result, EmLevel12PortFault *fault)
{
    L12 o;
    if (!result || l12_begin(&o, h, fault)) return -1;
    int32_t v = l12_0013D220(&o, self);
    if (l12_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* The forward probe of the steering family: the point (0, 0, len, 1) at
 * 0x700038A0 through the matrix 0x700036A0 (001026A0) and shifted by self
 * +0xB0 (001028B8). */
static int l12_ahead(L12 *o, uint32_t self, uint32_t len)
{
    l12_vec4(o, SP_38A0, 0, 0, len, F_ONE);
    if (l12_c_001026A0(o, SP_38A0, SP_36A0, SP_38A0)) return -1;
    return l12_c_001028B8(o, SP_38A0, SP_38A0, self + 0xB0);
}

/* The yaw matrix: 001029C0(0x700036A0), then 00102C58 with (0, self
 * +0xC4, 0, 1) at 0x700038A0. */
static int l12_yaw_matrix(L12 *o, uint32_t self)
{
    if (l12_c_001029C0(o, SP_36A0)) return -1;
    l12_w32(o, SP_38A0, 0);
    l12_w32(o, SP_38A0 + 4, l12_u32(o, self + 0xC4));
    l12_w32(o, SP_38A0 + 8, 0);
    l12_w32(o, SP_38A0 + 12, F_ONE);
    return l12_c_00102C58(o, SP_36A0, SP_36A0, SP_38A0);
}

/* ent +0x80 = (ent +0x80 & 0xC) | (bit 2 of ent +0x81 ? 2 : 1). */
static void l12_latch_turn(L12 *o, uint32_t ent)
{
    l12_w8(o, ent + 0x80, (uint32_t)l12_s8(o, ent + 0x80) & 0xCu);
    if (l12_s8(o, ent + 0x81) & 4) l12_w8(o, ent + 0x80, (uint32_t)l12_s8(o, ent + 0x80) | 2u);
    else l12_w8(o, ent + 0x80, (uint32_t)l12_s8(o, ent + 0x80) | 1u);
}

/* ent +0x80 |= (001B39F0(self, 0x700038A0, 0x700038D0) + 1) * 4. */
static void l12_pick_side(L12 *o, uint32_t self, uint32_t ent)
{
    int32_t t = l12_001B39F0(o, self, SP_38A0, SP_38D0);
    if (l12_failed(o)) return;
    l12_w8(o, ent + 0x80, (uint32_t)l12_s8(o, ent + 0x80) | ((uint32_t)(t + 1) << 2));
}

/* ------------------------------------------------------------------------
 * 0013C4C0 (self, ent): the side step. By bit 0 of ent +0x80: angle pi/8,
 * mode 4, ent +0x5C = 1.5533431 (else -pi/8, 2, -1.5533431). The probe 25
 * ahead along self +0xC4 (0019AFE0 mask 6):
 *   hit: 0x700038A0 = (0, 0, 001B15D0(self +0xB0, 0x700031B0), 1); when
 *     that distance <= 5 or ent +0x81 & mode: without bits 0xC of +0x80
 *     the point (0, 0, distance) turned and shifted, then the side pick
 *     (001B39F0); with ent +0x81 & mode the turn latch; returns 1;
 *   miss with ent +0x81 & mode: the turn latch, then the side pick when
 *     +0x80 has no bit 0xC; returns 1;
 *   else the probe 25 ahead at the angle (00102B08, 00102BB0): a hit
 *     returns 1; a miss with bit 1 of +0x80 clear and 0013D220 set
 *     returns 1; else +0x80 &= 0xC, ent +0x5C = 0, returns 0.
 * ---------------------------------------------------------------------- */
int32_t l12_0013C4C0(L12 *o, uint32_t self, uint32_t ent)
{
    uint32_t ang;
    int32_t mode, r;
    if (l12_s8(o, ent + 0x80) & 1) {
        ang = F_PI_8;
        mode = 4;
        l12_w32(o, ent + 0x5C, 0x3FC6D3F2u);
    } else {
        ang = F_MPI_8;
        mode = 2;
        l12_w32(o, ent + 0x5C, 0xBFC6D3F2u);
    }
    if (l12_yaw_matrix(o, self)) return 0;
    if (l12_ahead(o, self, F_25)) return 0;
    if (l12_c_0019AFE0(o, self, self + 0xB0, SP_38A0, 6, &r)) return 0;
    if (r != 0) {
        uint32_t d;
        l12_w32(o, SP_38A0 + 8, 0);
        l12_w32(o, SP_38A0 + 4, 0);
        l12_w32(o, SP_38A0, 0);
        l12_w32(o, SP_38A0 + 12, F_ONE);
        if (l12_c_001B15D0(o, self + 0xB0, SP_31B0, &d)) return 0;
        l12_w32(o, SP_38A0 + 8, d);
        if (!L12_LE(d, 0x40A00000u) && !(l12_s8(o, ent + 0x81) & mode)) return 1;
        if (!(l12_s8(o, ent + 0x80) & 0xC)) {
            if (l12_c_001026A0(o, SP_38A0, SP_36A0, SP_38A0)) return 0;
            if (l12_c_001028B8(o, SP_38A0, SP_38A0, self + 0xB0)) return 0;
            l12_pick_side(o, self, ent);
            if (l12_failed(o)) return 0;
        }
        if (l12_s8(o, ent + 0x81) & mode) l12_latch_turn(o, ent);
        return 1;
    }
    if (l12_s8(o, ent + 0x81) & mode) {
        l12_latch_turn(o, ent);
        if (!(l12_s8(o, ent + 0x80) & 0xC)) l12_pick_side(o, self, ent);
        return 1;
    }
    if (l12_c_001029C0(o, SP_36A0)) return 0;
    if (l12_c_00102B08(o, SP_36A0, SP_36A0, ang)) return 0;
    if (l12_c_00102BB0(o, SP_36A0, SP_36A0, l12_u32(o, self + 0xC4))) return 0;
    if (l12_ahead(o, self, F_25)) return 0;
    if (l12_c_0019AFE0(o, self, self + 0xB0, SP_38A0, 6, &r)) return 0;
    if (r != 0) return 1;
    if (!(l12_s8(o, ent + 0x80) & 2)) {
        int32_t q = l12_0013D220(o, self);
        if (l12_failed(o)) return 0;
        if (q != 0) return 1;
    }
    l12_w8(o, ent + 0x80, (uint32_t)l12_s8(o, ent + 0x80) & 0xCu);
    l12_w32(o, ent + 0x5C, 0);
    return 0;
}

int em_level12_port_0013C4C0(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, int32_t *result,
                             EmLevel12PortFault *fault)
{
    L12 o;
    if (!result || l12_begin(&o, h, fault)) return -1;
    int32_t v = l12_0013C4C0(&o, self, ent);
    if (l12_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0013C1F0 (self, ent): the turn. ent +0x28 counts down; ent +0x2A counts
 * up; when it was >= 0xB4 or ent +0x81 & 6, and ent +0x80 & 3 is clear:
 * +0x80 |= (bit 2 of +0x81 ? 2 : 1) when +0x81 & 6, else 1 << ((00122BB8()
 * >> 14) & 1). Bit 3 of +0x80: self +0xC4 = 001B1470(0.047123894 + +0xC4),
 * angle -pi/8; else 001B1470(+0xC4 - 0.047123894), +pi/8. The probe 25
 * ahead (0019AD00 mask 6) and again at the angle (00102BB0): a hit returns
 * 1; ent +0x28 nonzero returns 1; else +0x80 &= 3, ent +0x58 = self +0xC4,
 * returns 0.
 * ---------------------------------------------------------------------- */
int32_t l12_0013C1F0(L12 *o, uint32_t self, uint32_t ent)
{
    int32_t r;
    uint32_t ang, v;
    {
        uint32_t t = l12_u16(o, ent + 0x28);
        if (t != 0) l12_w16(o, ent + 0x28, t - 1u);
    }
    {
        uint32_t c = l12_u16(o, ent + 0x2A);
        l12_w16(o, ent + 0x2A, c + 1u);
        int go = c >= 0xB4u;
        if (!go) go = (l12_s8(o, ent + 0x81) & 6) != 0;
        if (go) {
            int32_t a0 = l12_s8(o, ent + 0x80);
            if (!(a0 & 3)) {
                int32_t fl = l12_s8(o, ent + 0x81);
                if (fl & 6) {
                    l12_w8(o, ent + 0x80, (uint32_t)a0 | ((fl & 4) ? 2u : 1u));
                } else {
                    int32_t rnd;
                    if (l12_c_00122BB8(o, &rnd)) return 0;
                    uint32_t bit = (uint32_t)l12_sra((uint32_t)rnd, 14) & 1u;
                    uint32_t cur = (uint32_t)l12_s8(o, ent + 0x80);
                    l12_w8(o, ent + 0x80, cur | (uint32_t)(int32_t)(int8_t)(1u << bit));
                }
            }
        }
    }
    if (l12_s8(o, ent + 0x80) & 8) {
        uint32_t c4 = l12_u32(o, self + 0xC4);
        if (l12_c_001B1470(o, L12_ADD(0x3D4104FCu, c4), &v)) return 0;
        ang = F_MPI_8;
    } else {
        uint32_t c4 = l12_u32(o, self + 0xC4);
        if (l12_c_001B1470(o, L12_SUB(c4, 0x3D4104FCu), &v)) return 0;
        ang = F_PI_8;
    }
    l12_w32(o, self + 0xC4, v);
    if (l12_yaw_matrix(o, self)) return 0;
    if (l12_ahead(o, self, F_25)) return 0;
    if (l12_c_0019AD00(o, self, SP_38A0, 6, &r)) return 0;
    if (r != 0) return 1;
    if (l12_c_00102BB0(o, SP_36A0, SP_36A0, ang)) return 0;
    if (l12_ahead(o, self, F_25)) return 0;
    if (l12_c_0019AD00(o, self, SP_38A0, 6, &r)) return 0;
    if (r != 0) return 1;
    if (l12_u16(o, ent + 0x28) != 0) return 1;
    l12_w8(o, ent + 0x80, (uint32_t)l12_s8(o, ent + 0x80) & 3u);
    l12_w32(o, ent + 0x58, l12_u32(o, self + 0xC4));
    return 0;
}

int em_level12_port_0013C1F0(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, int32_t *result,
                             EmLevel12PortFault *fault)
{
    L12 o;
    if (!result || l12_begin(&o, h, fault)) return -1;
    int32_t v = l12_0013C1F0(&o, self, ent);
    if (l12_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0013CD50 (self, seg): the obstacle class of the segment self +0xB0 ->
 * seg. 0019AFE0(self, self +0xB0, seg, 6): 0 -> 0; bit 1 with no object
 * at 0x700031D4, or bit 0 -> 3. 0019BC40(seg) and the column count
 * 0x700031E0 (0 -> 3). lo / hi start at y -+ 1000 (y = seg +4); the
 * column table (heights 0x700030F0, flags 0x70003170, slopes 0x282250)
 * up to the first height above y: below, an unflagged entry with a slope
 * (0011DF78) <= pi/3 sets lo; the first above, flagged with such a slope,
 * sets hi. Then the column at the point 0.1 behind self (the yaw pi +
 * self +0xC4 through 00102C58 / 001026A0, plus 0x700031B0): mask bit 0
 * for a height <= y above lo - 5, bit 1 for one above y below hi + 5.
 * Mask 0: (|hi - y| <= 25 and |y - lo| <= 25 -> 1 when |hi - y| <= |y -
 * lo|, else 2), else 1 when |hi - y| <= 25, 2 when |y - lo| <= 25, else
 * 1; mask 1: |hi - y| <= 25 ? 1 : 3; mask 2: |y - lo| <= 25 ? 2 : 3; mask
 * 3: 3.
 * ---------------------------------------------------------------------- */
int32_t l12_0013CD50(L12 *o, uint32_t self, uint32_t seg)
{
    int32_t r;
    uint32_t g;
    if (l12_c_0019AFE0(o, self, self + 0xB0, seg, 6, &r)) return 0;
    if (r == 0) return 0;
    if (r & 2) {
        if (l12_u32(o, 0x700031D4u) == 0) return 3;
    }
    if (r & 1) return 3;
    if (l12_c_0019BC40(o, seg)) return 0;
    if (l12_u32(o, 0x700031E0u) == 0) return 3;
    uint32_t y0 = l12_u32(o, seg + 4);
    uint32_t lo = L12_SUB(y0, 0x447A0000u);
    uint32_t hi = L12_ADD(0x447A0000u, y0);
    for (int32_t i = 0; i < l12_s32(o, 0x700031E0u); i++) {
        uint32_t y = l12_u32(o, seg + 4);
        uint32_t h = l12_u32(o, 0x700030F0u + 4u * (uint32_t)i);
        if (L12_LT(y, h)) {
            if (l12_u16(o, 0x70003170u + 2u * (uint32_t)i) & 1u) {
                if (l12_c_0011DF78(o, l12_u32(o, 0x282250u + 4u * (uint32_t)i), &g)) return 0;
                if (L12_LE(g, 0x3F860A92u)) hi = l12_u32(o, 0x700030F0u + 4u * (uint32_t)i);
            }
            break;
        }
        if (l12_u16(o, 0x70003170u + 2u * (uint32_t)i) & 1u) continue;
        if (l12_c_0011DF78(o, l12_u32(o, 0x282250u + 4u * (uint32_t)i), &g)) return 0;
        if (L12_LE(g, 0x3F860A92u)) lo = l12_u32(o, 0x700030F0u + 4u * (uint32_t)i);
    }
    {
        uint32_t yaw;
        l12_w32(o, SP_38B0, 0);
        uint32_t c4 = l12_u32(o, self + 0xC4);
        if (l12_c_001B1470(o, L12_ADD(F_PI, c4), &yaw)) return 0;
        l12_w32(o, SP_38B0 + 4, yaw);
        l12_w32(o, SP_38B0 + 8, 0);
        l12_w32(o, SP_38B0 + 12, F_ONE);
    }
    if (l12_c_001029C0(o, SP_36A0)) return 0;
    if (l12_c_00102C58(o, SP_36A0, SP_36A0, SP_38B0)) return 0;
    l12_vec4(o, SP_38B0, 0, 0, 0x3DCCCCCDu, F_ONE);
    if (l12_c_001026A0(o, SP_38B0, SP_36A0, SP_38B0)) return 0;
    if (l12_c_001028B8(o, SP_38B0, SP_38B0, SP_31B0)) return 0;
    if (l12_c_0019BC40(o, SP_38B0)) return 0;
    uint32_t mask = 0;
    {
        int32_t n = l12_s32(o, 0x700031E0u);
        if (n != 0) {
            uint32_t up = L12_ADD(0x40A00000u, hi);
            uint32_t down = L12_SUB(lo, 0x40A00000u);
            for (int32_t i = 0; i < n; i++) {
                uint32_t h = l12_u32(o, 0x700030F0u + 4u * (uint32_t)i);
                uint32_t y = l12_u32(o, seg + 4);
                if (L12_LE(h, y)) {
                    if (!L12_LE(h, down)) mask |= 1u;
                } else if (L12_LT(h, up)) {
                    mask |= 2u;
                }
            }
        }
    }
    switch (mask) {
    case 3:
        return 3;
    case 2:
        if (l12_c_0011DF78(o, L12_SUB(l12_u32(o, seg + 4), lo), &g)) return 0;
        return L12_LE(g, F_25) ? 2 : 3;
    case 1:
        if (l12_c_0011DF78(o, L12_SUB(hi, l12_u32(o, seg + 4)), &g)) return 0;
        return L12_LE(g, F_25) ? 1 : 3;
    default: {
        uint32_t a, b;
        if (l12_c_0011DF78(o, L12_SUB(hi, l12_u32(o, seg + 4)), &g)) return 0;
        if (L12_LE(g, F_25)) {
            if (l12_c_0011DF78(o, L12_SUB(l12_u32(o, seg + 4), lo), &g)) return 0;
            if (L12_LE(g, F_25)) {
                if (l12_c_0011DF78(o, L12_SUB(hi, l12_u32(o, seg + 4)), &a)) return 0;
                if (l12_c_0011DF78(o, L12_SUB(l12_u32(o, seg + 4), lo), &b)) return 0;
                return L12_LE(a, b) ? 1 : 2;
            }
        }
        if (l12_c_0011DF78(o, L12_SUB(hi, l12_u32(o, seg + 4)), &g)) return 0;
        if (L12_LE(g, F_25)) return 1;
        if (l12_c_0011DF78(o, L12_SUB(l12_u32(o, seg + 4), lo), &g)) return 0;
        return L12_LE(g, F_25) ? 2 : 1;
    }
    }
}

int em_level12_port_0013CD50(const EmLevel12PortHooks *h, uint32_t self, uint32_t seg, int32_t *result,
                             EmLevel12PortFault *fault)
{
    L12 o;
    if (!result || l12_begin(&o, h, fault)) return -1;
    int32_t v = l12_0013CD50(&o, self, seg);
    if (l12_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0013C8C0 (self, ent, sp): the steering decision, only while ent +0x80 &
 * 0xF is clear. The point (0, 0, 25) through 001B2B10(self) and shifted
 * by self +0xB0; ent +0x80 = 0013CD50(self, it) (its low byte):
 *   0: the probe 20 ahead at +pi/8 (bit 0 of the frame counter plus
 *      0x70003B8A clear; bit 0 on a hit (0019AFE0) or on 0013D220) or at
 *      -pi/8 (bit 1 on a hit);
 *   3: ent +0x28 = 00128250(66.666664 * (((00122BB8() >> 7) & 0xFF) /
 *      255)), +0x2A = 0; the point (0, 0, 1) through 001B2B10(self); the
 *      hit object's x / z (0x700031D0 +0x24 / +0x2C) normalized
 *      (00102760); when 0011E420(00102738(the two)) <= 2.3561945 the turn
 *      001B1380 toward it, else half the distance to 0x700031B0 ahead
 *      and 001B39F0; +0x80 = (turn + 1) * 4.
 * Then 0013C4C0 on +0x80 & 3 and 0013C1F0 on +0x80 & 0xC.
 * ---------------------------------------------------------------------- */
void l12_0013C8C0(L12 *o, uint32_t self, uint32_t ent, uint32_t sp)
{
    int32_t r;
    if (!(l12_s8(o, ent + 0x80) & 0xF)) {
        l12_vec4(o, SP_38A0, 0, 0, F_25, F_ONE);
        if (l12_c_001B2B10(o, self, SP_38A0, SP_38A0)) return;
        if (l12_c_001028B8(o, SP_38A0, SP_38A0, self + 0xB0)) return;
        int32_t cls = l12_0013CD50(o, self, SP_38A0);
        if (l12_failed(o)) return;
        l12_w8(o, ent + 0x80, (uint32_t)cls);
        if ((int8_t)cls == 0) {
            if (l12_c_001029C0(o, SP_36A0)) return;
            l12_vec4(o, SP_38A0, 0, 0, F_20, F_ONE);
            int32_t b8a = l12_s16(o, SP_B8A);
            uint32_t coin = (l12_u32(o, SP_FRAME) + (uint32_t)b8a) & 1u;
            if (coin == 1) {
                if (l12_c_00102B08(o, SP_36A0, SP_36A0, F_MPI_8)) return;
                if (l12_c_00102BB0(o, SP_36A0, SP_36A0, l12_u32(o, self + 0xC4))) return;
                if (l12_c_001026A0(o, SP_38A0, SP_36A0, SP_38A0)) return;
                if (l12_c_001028B8(o, SP_38A0, SP_38A0, self + 0xB0)) return;
                if (l12_c_0019AFE0(o, self, self + 0xB0, SP_38A0, 6, &r)) return;
                if (r != 0) l12_w8(o, ent + 0x80, (uint32_t)l12_s8(o, ent + 0x80) | 2u);
            } else {
                if (l12_c_00102B08(o, SP_36A0, SP_36A0, F_PI_8)) return;
                if (l12_c_00102BB0(o, SP_36A0, SP_36A0, l12_u32(o, self + 0xC4))) return;
                if (l12_c_001026A0(o, SP_38A0, SP_36A0, SP_38A0)) return;
                if (l12_c_001028B8(o, SP_38A0, SP_38A0, self + 0xB0)) return;
                if (l12_c_0019AFE0(o, self, self + 0xB0, SP_38A0, 6, &r)) return;
                int hit = r != 0;
                if (!hit) {
                    int32_t q = l12_0013D220(o, self);
                    if (l12_failed(o)) return;
                    hit = q != 0;
                }
                if (hit) l12_w8(o, ent + 0x80, (uint32_t)l12_s8(o, ent + 0x80) | 1u);
            }
        } else if (l12_s8(o, ent + 0x80) == 3) {
            int32_t rnd, w;
            if (l12_c_00122BB8(o, &rnd)) return;
            {
                uint32_t n = (uint32_t)l12_sra((uint32_t)rnd, 7) & 0xFFu;
                uint32_t f = L12_MUL(0x42855555u, L12_DIV(L12_CVT_S_W(n), 0x437F0000u));
                if (l12_c_00128250(o, f, &w)) return;
            }
            l12_w16(o, ent + 0x28, (uint32_t)w);
            l12_w16(o, ent + 0x2A, 0);
            l12_vec4(o, SP_38A0, 0, 0, F_ONE, F_ONE);
            if (l12_c_001B2B10(o, self, SP_38A0, SP_38A0)) return;
            {
                uint32_t hit = l12_u32(o, 0x700031D0u);
                l12_w32(o, SP_38B0, l12_u32(o, hit + 0x24));
                l12_w32(o, SP_38B0 + 4, 0);
                l12_w32(o, SP_38B0 + 8, l12_u32(o, hit + 0x2C));
                l12_w32(o, SP_38B0 + 12, F_ONE);
            }
            if (l12_c_00102760(o, SP_38B0, SP_38B0)) return;
            uint32_t dot, ang;
            if (l12_c_00102738(o, SP_38A0, SP_38B0, &dot)) return;
            ang = l12_0011E420(o, dot, sp - 0x30u);
            if (l12_failed(o)) return;
            int32_t turn;
            if (L12_LE(ang, 0x4016CBE4u)) {
                if (l12_c_001028B8(o, SP_38B0, SP_38B0, self + 0xB0)) return;
                if (l12_c_001B1380(o, SP_38B0, self + 0xB0, l12_u32(o, self + 0xC4), &turn)) return;
            } else {
                uint32_t d;
                l12_w32(o, SP_38A0 + 8, 0);
                l12_w32(o, SP_38A0 + 4, 0);
                l12_w32(o, SP_38A0, 0);
                l12_w32(o, SP_38A0 + 12, F_ONE);
                if (l12_c_001B15D0(o, self + 0xB0, SP_31B0, &d)) return;
                l12_w32(o, SP_38A0 + 8, L12_DIV(d, 0x40000000u));
                if (l12_c_001B2B10(o, self, SP_38A0, SP_38A0)) return;
                if (l12_c_001028B8(o, SP_38A0, SP_38A0, self + 0xB0)) return;
                turn = l12_001B39F0(o, self, SP_38A0, SP_38D0);
                if (l12_failed(o)) return;
            }
            l12_w8(o, ent + 0x80, (uint32_t)(turn + 1) << 2);
        }
    }
    if (l12_s8(o, ent + 0x80) & 3) {
        l12_0013C4C0(o, self, ent);
        if (l12_failed(o)) return;
    }
    if (l12_s8(o, ent + 0x80) & 0xC) l12_0013C1F0(o, self, ent);
}

int em_level12_port_0013C8C0(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                             EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_0013C8C0(&o, self, ent, sp);
    return l12_end(&o);
}

/* The 70-unit (30-unit) check of 00138C20: self +0xB0..+0xBC to
 * 0x700038A0, ent +0..+0xC to 0x700038B0, then 001B13F0 with the
 * radius. */
static int l12_near(L12 *o, uint32_t self, uint32_t ent, uint32_t radius, int32_t *r)
{
    l12_w32(o, SP_38A0, l12_u32(o, self + 0xB0));
    l12_w32(o, SP_38A0 + 4, l12_u32(o, self + 0xB4));
    l12_w32(o, SP_38A0 + 8, l12_u32(o, self + 0xB8));
    l12_w32(o, SP_38A0 + 12, l12_u32(o, self + 0xBC));
    l12_w32(o, SP_38B0, l12_u32(o, ent + 0));
    l12_w32(o, SP_38B0 + 4, l12_u32(o, ent + 4));
    l12_w32(o, SP_38B0 + 8, l12_u32(o, ent + 8));
    l12_w32(o, SP_38B0 + 12, l12_u32(o, ent + 0xC));
    return l12_c_001B13F0(o, SP_38A0, SP_38B0, radius, r);
}

/* The every-64th-frame test of 00138C20: (0x70003B68 + 0x70003B8A) &
 * 0x3F is zero. */
static int l12_tick64(L12 *o)
{
    int32_t b8a = l12_s16(o, SP_B8A);
    return ((l12_u32(o, SP_FRAME) + (uint32_t)b8a) & 0x3Fu) == 0;
}

/* The pitch check of 00138C20's state 0: a negative ent +0x5C becomes
 * |ent +0x5C| (0011DF78) when 001B2F70(self +0xB0, 0x700038A0) finds a
 * floor and 20 + that floor is above self +0xB4. */
static void l12_pitch_floor(L12 *o, uint32_t self, uint32_t ent)
{
    int32_t found = l12_001B2F70(o, self + 0xB0, SP_38A0);
    if (l12_failed(o) || found == 0) return;
    uint32_t fl = l12_u32(o, SP_38A0);
    uint32_t y = l12_u32(o, self + 0xB4);
    if (L12_LE(L12_ADD(F_20, fl), y)) return;
    uint32_t a;
    if (l12_c_0011DF78(o, l12_u32(o, ent + 0x5C), &a)) return;
    l12_w32(o, ent + 0x5C, a);
}

/* ------------------------------------------------------------------------
 * 00138C20 (self, ent, sp): behaviour 1, the wander. 0013C8C0; ent +0x44
 * < 0.4 -> ent +0x48 = 0.05, else ent +0x48 = 0, +0x44 = 0.4. By self +6:
 *   0: every 64th frame, the target ent +0 outside 70 (001B13F0 0): +6 =
 *      1, ent +0x20 = 0, done. ent +0x80 & 0xC -> ent +0x20 = 0x3C, else
 *      the countdown, at 0 re-rolled ((r >> 17) & 0x7F) + 0x3C with ent
 *      +0x58 = 2 pi ((r' >> 9) & 0xFF) / 255 - pi. ent +0x80 & 3 -> ent
 *      +0x22 = 0x3C, else the countdown (then the floor check), at 0
 *      re-rolled ((r >> 12) & 0x7F) + 0x3C with ent +0x5C = 2.4434612
 *      ((r' >> 6) & 0xFF) / 255 - 1.2217306 (then the floor check when
 *      negative).
 *   1: every 64th frame within 30: +6 = 0, ent +0x20 = 0, done. Without
 *      ent +0x80 & 0xC: ent +0x58 = 001B1240(self +0xB0, ent +0, ent +8).
 *      ent +0x80 & 3 -> ent +0x20 = 0x78, else the countdown, at 0 ent
 *      +0x5C = -001B1270(self +0xB0, ent +4, ent +8).
 * Tail: bit 0 of self +0xD counts ent +0x30 up (at 0x97 +5 = +6 = 0); bit
 * 0 of self +0xA: +5 = 2, +6 = 0, ent +0x22 = +0x20 = 0, 001FBD50(self,
 * 0x816, 0, 300); else with ent +0x2E zero: the player 0x810360 within 150
 * (001B13F0), 0021BE40(0x8102B0, self) zero and 0019AFE0(self, self +0xB0,
 * 0x810360, 6) zero count ent +0x2C up (at 0x78 the same as bit 0 of
 * +0xA), else ent +0x2C = 0. ent +0x86 zero: = 00122BB8() >> 4,
 * 001FBD50(self, 0x826, 0, 300). self +0xC4 = 001B12B0(ent +0x58, +0xC4,
 * pi/120); ent +0x50 = 001B12B0(ent +0x5C, +0x50, 0.031415924); 0013BBB0,
 * 0013BA20.
 * ---------------------------------------------------------------------- */
static void l12_alert(L12 *o, uint32_t self, uint32_t ent)
{
    l12_w8(o, self + 5, 2);
    l12_w8(o, self + 6, 0);
    l12_w16(o, ent + 0x22, 0);
    l12_w16(o, ent + 0x20, 0);
    l12_c_001FBD50(o, self, 0x816, 0, F_300);
}

void l12_00138C20(L12 *o, uint32_t self, uint32_t ent, uint32_t sp)
{
    int32_t r;
    uint32_t v;
    l12_0013C8C0(o, self, ent, sp - 0x30u);
    if (l12_failed(o)) return;
    if (L12_LT(l12_u32(o, ent + 0x44), 0x3ECCCCCDu)) {
        l12_w32(o, ent + 0x48, 0x3D4CCCCDu);
    } else {
        l12_w32(o, ent + 0x48, 0);
        l12_w32(o, ent + 0x44, 0x3ECCCCCDu);
    }
    switch (l12_u8(o, self + 6)) {
    case 0:
        if (l12_tick64(o)) {
            if (l12_near(o, self, ent, 0x428C0000u, &r)) return;
            if (r == 0) {
                l12_w8(o, self + 6, l12_u8(o, self + 6) + 1u);
                l12_w16(o, ent + 0x20, 0);
                break;
            }
        }
        if (l12_s8(o, ent + 0x80) & 0xC) {
            l12_w16(o, ent + 0x20, 0x3C);
        } else {
            uint32_t t = l12_u16(o, ent + 0x20);
            if (t != 0) {
                l12_w16(o, ent + 0x20, t - 1u);
            } else {
                if (l12_c_00122BB8(o, &r)) return;
                l12_w16(o, ent + 0x20, ((uint32_t)l12_sra((uint32_t)r, 17) & 0x7Fu) + 0x3Cu);
                if (l12_c_00122BB8(o, &r)) return;
                v = L12_DIV(L12_CVT_S_W((uint32_t)l12_sra((uint32_t)r, 9) & 0xFFu), 0x437F0000u);
                l12_w32(o, ent + 0x58, L12_SUB(L12_MUL(0x40C90FDBu, v), F_PI));
            }
        }
        if (l12_s8(o, ent + 0x80) & 3) {
            l12_w16(o, ent + 0x22, 0x3C);
            break;
        }
        {
            uint32_t t = l12_u16(o, ent + 0x22);
            if (t != 0) {
                l12_w16(o, ent + 0x22, t - 1u);
                if (!L12_LT(l12_u32(o, ent + 0x5C), 0)) break;
                l12_pitch_floor(o, self, ent);
                if (l12_failed(o)) return;
            } else {
                if (l12_c_00122BB8(o, &r)) return;
                l12_w16(o, ent + 0x22, ((uint32_t)l12_sra((uint32_t)r, 12) & 0x7Fu) + 0x3Cu);
                if (l12_c_00122BB8(o, &r)) return;
                v = L12_DIV(L12_CVT_S_W((uint32_t)l12_sra((uint32_t)r, 6) & 0xFFu), 0x437F0000u);
                v = L12_SUB(L12_MUL(0x401C61ABu, v), 0x3F9C61ABu);
                l12_w32(o, ent + 0x5C, v);
                if (!L12_LT(v, 0)) break;
                l12_pitch_floor(o, self, ent);
                if (l12_failed(o)) return;
            }
        }
        break;
    case 1:
        if (l12_tick64(o)) {
            if (l12_near(o, self, ent, 0x41F00000u, &r)) return;
            if (r != 0) {
                l12_w8(o, self + 6, 0);
                l12_w16(o, ent + 0x20, 0);
                break;
            }
        }
        if (!(l12_s8(o, ent + 0x80) & 0xC)) {
            uint32_t x = l12_u32(o, ent + 0);
            uint32_t z = l12_u32(o, ent + 8);
            if (l12_c_001B1240(o, self + 0xB0, x, z, &v)) return;
            l12_w32(o, ent + 0x58, v);
        }
        if (l12_s8(o, ent + 0x80) & 3) {
            l12_w16(o, ent + 0x20, 0x78);
        } else {
            uint32_t t = l12_u16(o, ent + 0x20);
            if (t != 0) {
                l12_w16(o, ent + 0x20, t - 1u);
            } else {
                uint32_t y = l12_u32(o, ent + 4);
                uint32_t z = l12_u32(o, ent + 8);
                v = l12_001B1270(o, self + 0xB0, y, z);
                if (l12_failed(o)) return;
                l12_w32(o, ent + 0x5C, L12_MUL(0xBF800000u, v));
            }
        }
        break;
    default:
        break;
    }
    if (l12_u8(o, self + 0xD) & 1u) {
        uint32_t t = (l12_u16(o, ent + 0x30) + 1u) & 0xFFFFu;
        l12_w16(o, ent + 0x30, t);
        if (t >= 0x97u) {
            l12_w8(o, self + 5, 0);
            l12_w8(o, self + 6, 0);
        }
    }
    if (l12_u8(o, self + 0xA) & 1u) {
        l12_alert(o, self, ent);
        if (l12_failed(o)) return;
    } else if (l12_u16(o, ent + 0x2E) == 0) {
        int clear = 1;
        if (l12_c_001B13F0(o, 0x810360u, self + 0xB0, 0x43160000u, &r)) return;
        if (r != 0) {
            if (l12_c_0021BE40(o, 0x8102B0u, self, &r)) return;
            if (r == 0) {
                if (l12_c_0019AFE0(o, self, self + 0xB0, 0x810360u, 6, &r)) return;
                if (r == 0) {
                    uint32_t t = (l12_u16(o, ent + 0x2C) + 1u) & 0xFFFFu;
                    l12_w16(o, ent + 0x2C, t);
                    clear = 0;
                    if (t >= 0x78u) {
                        l12_alert(o, self, ent);
                        if (l12_failed(o)) return;
                    }
                }
            }
        }
        if (clear) l12_w16(o, ent + 0x2C, 0);
    }
    if (l12_s8(o, ent + 0x86) == 0) {
        if (l12_c_00122BB8(o, &r)) return;
        l12_w8(o, ent + 0x86, (uint32_t)l12_sra((uint32_t)r, 4));
        if (l12_c_001FBD50(o, self, 0x826, 0, F_300)) return;
    }
    {
        uint32_t cur = l12_u32(o, self + 0xC4);
        uint32_t goal = l12_u32(o, ent + 0x58);
        if (l12_c_001B12B0(o, goal, cur, 0x3CD67750u, &v)) return;
        l12_w32(o, self + 0xC4, v);
    }
    {
        uint32_t cur = l12_u32(o, ent + 0x50);
        uint32_t goal = l12_u32(o, ent + 0x5C);
        if (l12_c_001B12B0(o, goal, cur, 0x3D00ADFCu, &v)) return;
        l12_w32(o, ent + 0x50, v);
    }
    l12_0013BBB0(o, self, ent, sp - 0x30u);
    if (l12_failed(o)) return;
    l12_0013BA20(o, self, ent);
}

int em_level12_port_00138C20(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                             EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_00138C20(&o, self, ent, sp);
    return l12_end(&o);
}
