/* The tenth level's boot rows (00118790, 001305B0, 001833F0, 001885F0,
 * 001E6F60) and AREA13's load-time 0x8236E0. See em_level10_port.h and
 * docs/LEVEL10_PORT.md. 001833F0, 001885F0, 001E6F60 and AREA13
 * 0x8236E0 are byte-identical decomp C; 00118790 and 001305B0 are NEARMISS
 * and are translated from the instructions (the differences from the C
 * text are noted at each). Roles are what the code does.
 */
#include "em_level10_port_internal.h"

/* ------------------------------------------------------------------------
 * 00118790 (p): a step of the sequencer's event reader (001152D8 calls it
 * for controller 0x60). base = D_00281AD4, c = p +8 (read in that order);
 * p +0x3C = 1, p +0x38 = 1; op = base + c. With op[4] nonzero: k = p
 * +0x36 (halfword); k == op[4] -> p +0x38 = p +0x36 = p +0x3C = 0; else
 * v = (op[3] << 8) + op[2], p +0x14 = v, b = base[v], p +0x36 = k + 1, p
 * +0x3A = b (the decomp's C stores +0x36 before reading base[v]; the
 * instructions read it first). With op[4] zero: p +0x14 = op[3] << 8, then
 * p +0x14 = that | op[2] (op[2] read between the two stores), p +0x3A =
 * base[that]. Always p +8 = c + 5, the result.
 * ---------------------------------------------------------------------- */
int em_level10_port_00118790(const EmLevel10PortHooks *h, uint32_t p, int32_t *result, EmLevel10PortFault *fault)
{
    L10 o;
    if (!result || l10_begin(&o, h, fault)) return -1;
    uint32_t base = l10_u32(&o, 0x00281AD4u);
    uint32_t c = l10_u32(&o, p + 8);
    l10_w16(&o, p + 0x3C, 1);
    l10_w16(&o, p + 0x38, 1);
    uint32_t op = c + base;
    uint32_t b4 = l10_u8(&o, op + 4);
    if (b4 != 0) {
        uint32_t k = l10_u16(&o, p + 0x36);
        if (k == b4) {
            l10_w16(&o, p + 0x38, 0);
            l10_w16(&o, p + 0x36, 0);
            l10_w16(&o, p + 0x3C, 0);
        } else {
            uint32_t b3 = l10_u8(&o, op + 3);
            uint32_t b2 = l10_u8(&o, op + 2);
            uint32_t v = (b3 << 8) + b2;
            l10_w32(&o, p + 0x14, v);
            uint32_t b = l10_u8(&o, base + v);
            l10_w16(&o, p + 0x36, k + 1);
            l10_w16(&o, p + 0x3A, b);
        }
    } else {
        uint32_t b3 = l10_u8(&o, op + 3);
        uint32_t v = b3 << 8;
        l10_w32(&o, p + 0x14, v);
        uint32_t b2 = l10_u8(&o, op + 2);
        v |= b2;
        l10_w32(&o, p + 0x14, v);
        uint32_t b = l10_u8(&o, base + v);
        l10_w16(&o, p + 0x3A, b);
    }
    l10_w32(&o, p + 8, c + 5);
    if (l10_failed(&o)) return -1;
    *result = (int32_t)(c + 5);
    return 0;
}

/* ------------------------------------------------------------------------
 * 001305B0 (self, ent = self + 0x1F0 at its caller 0012E840, behaviour 6):
 * by the byte +6 (read once, st).
 *  0: +6 = st + 1; ent +0x63 = 0, +0x50 = 0, +0x54 = 1 (halfwords), +0x6B
 *     = 1, +0x3C = 0, +0x34 = 2.0; self +0x5C = 2; 001C67E0(self, 0x1E,
 *     5, 0); then on as 1.
 *  1: when ent +0x54 is nonzero and self +0x3C <= 136: ent +0x54 = 0,
 *     001FBD50(self, 0x7E1, 0, 300). When ent +0x58 has bit 0x1000: +6 =
 *     +6 + 1 (read again), ent +0x34 = 1.0, 001C67E0(self, 0x1F, 1, 0);
 *     else ent +0x30 = 001B1240(self + 0xB0, D_00810360, D_00810368) and
 *     self +0xC4 = 001B12B0(ent +0x30, self +0xC4, 0.0174533) (self +0xC4
 *     is read before ent +0x30; the decomp's C text reads them the other
 *     way round).
 *  2: bit 0x1000 of ent +0x58 -> +6 = st + 1, 001C67E0(self, 0x20, 1, 0).
 *  3: 0021BE40(0x8102B0) zero and 001A7B80(self) nonzero: +6 += 1,
 *     D_008102BF = 2, 0x700038A0 = (0, 0, 1, 1), 001B2B10(self,
 *     0x700038A0, 0x700038A0), D_00810320..2C = 0x700038A0..AC (read, then
 *     stored), D_008104D4 = 25 / 20 (+0xD == 3, by D_0081070A), 30 / 28
 *     (+0xD bit 7, by D_0081070A) or 20, D_008102B0 |= 2, 001B55E0(self,
 *     1), 001C67E0(self, 0x21, 1, 0). Otherwise 0x700038A0 = (0, 3, 10,
 *     1), 00131F20(self, 0x700038A0, 0x700038A0), 001028B8(0x700038A0,
 *     0x700038A0, self + 0xB0); 001B3250(self, 0x700038A0, 15) nonzero ->
 *     +6 += 1, 001C67E0(self, 0xA, 3, 0); else 001B1560(self, 0x810360,
 *     1.4835298) zero -> +6 += 1, 001C67E0(self, 0x21, 1, 0).
 *  4: bit 0x1000 of ent +0x58 -> +5 = +6 = 0, ent +0x62 = 0, +0x34 = 1.0,
 *     +0x5A = 0 (halfword), +0x6B = 0.
 * Then +6 is read again: from 2 on, with ent +0x69 bit 0: the halfword
 * ent +0x50 counts down; at 0 it is set to ((rand >> 9) & 7) + 5 and
 * 001EFD90(0x8000001D, 0x700038A0, self + 0xC0) with 0x700038A0 =
 * (self +0xB0, +0xB4, +0xB8, 1) whose y is then replaced by ent +0x40.
 * Last 00132490(self, ent).
 * ---------------------------------------------------------------------- */
static void anim(L10 *o, uint32_t self, int32_t clip, uint32_t a)
{
    l10_c_001C67E0(o, self, clip, a, F_ZERO);
}

static void bump6(L10 *o, uint32_t self)
{
    uint32_t s = l10_u8(o, self + 6);
    l10_w8(o, self + 6, s + 1);
}

static void state1(L10 *o, uint32_t self, uint32_t ent)
{
    if (l10_u16(o, ent + 0x54) != 0) {
        uint32_t x = l10_u32(o, self + 0x3C);
        if (L10_LE(x, 0x43080000u)) { /* 136.0 */
            int32_t r = 0;
            l10_w16(o, ent + 0x54, 0);
            l10_c_001FBD50(o, self, 0x7E1, 0, F_300, &r);
        }
    }
    if (l10_u16(o, ent + 0x58) & 0x1000) {
        bump6(o, self);
        l10_w32(o, ent + 0x34, F_ONE);
        anim(o, self, 0x1F, F_ONE);
    } else {
        uint32_t px = l10_u32(o, 0x00810360u);
        uint32_t pz = l10_u32(o, 0x00810368u);
        uint32_t aim = 0;
        l10_c_001B1240(o, self + 0xB0, px, pz, &aim);
        l10_w32(o, ent + 0x30, aim);
        uint32_t cur = l10_u32(o, self + 0xC4);
        uint32_t target = l10_u32(o, ent + 0x30);
        uint32_t turned = 0;
        l10_c_001B12B0(o, target, cur, 0x3C8EFA35u, &turned); /* 0.0174533 */
        l10_w32(o, self + 0xC4, turned);
    }
}

static void state3(L10 *o, uint32_t self)
{
    int32_t r = 0;
    l10_c_0021BE40(o, D_008102B0, &r);
    int commit = 0;
    if (r == 0) {
        r = 0;
        l10_c_001A7B80(o, self, &r);
        commit = r != 0;
    }
    if (commit) {
        bump6(o, self);
        l10_w8(o, 0x008102BFu, 2);
        l10_w32(o, S_700038A0, F_ZERO);
        l10_w32(o, S_700038A4, F_ZERO);
        l10_w32(o, S_700038A8, F_ONE);
        l10_w32(o, S_700038AC, F_ONE);
        l10_c_001B2B10(o, self, S_700038A0, S_700038A0);
        uint32_t a = l10_u32(o, S_700038A0);
        uint32_t b = l10_u32(o, S_700038A4);
        uint32_t c = l10_u32(o, S_700038A8);
        uint32_t d = l10_u32(o, S_700038AC);
        l10_w32(o, 0x00810320u, a);
        l10_w32(o, 0x00810324u, b);
        l10_w32(o, 0x00810328u, c);
        l10_w32(o, 0x0081032Cu, d);
        uint32_t fl = l10_u8(o, self + 0xD);
        if (fl == 3)
            l10_w32(o, 0x008104D4u, l10_u8(o, 0x0081070Au) ? 0x41C80000u : 0x41A00000u);
        else if (fl & 0x80)
            l10_w32(o, 0x008104D4u, l10_u8(o, 0x0081070Au) ? 0x41F00000u : 0x41E00000u);
        else
            l10_w32(o, 0x008104D4u, 0x41A00000u);
        uint32_t pb = l10_u8(o, D_008102B0);
        l10_w8(o, D_008102B0, pb | 2);
        l10_c_001B55E0(o, self, 1);
        anim(o, self, 0x21, F_ONE);
        return;
    }
    l10_w32(o, S_700038A0, F_ZERO);
    l10_w32(o, S_700038A4, F_3);
    l10_w32(o, S_700038A8, F_10);
    l10_w32(o, S_700038AC, F_ONE);
    l10_c_00131F20(o, self, S_700038A0, S_700038A0);
    l10_c_001028B8(o, S_700038A0, S_700038A0, self + 0xB0);
    r = 0;
    l10_c_001B3250(o, self, S_700038A0, F_15, &r);
    if (r) {
        bump6(o, self);
        anim(o, self, 0xA, F_3);
        return;
    }
    r = 0;
    l10_c_001B1560(o, self, D_00810360, 0x3FBDE44Eu, &r); /* 1.4835298 */
    if (r == 0) {
        bump6(o, self);
        anim(o, self, 0x21, F_ONE);
    }
}

int em_level10_port_001305B0(const EmLevel10PortHooks *h, uint32_t self, uint32_t ent, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    uint32_t st = l10_u8(&o, self + 6);
    switch (st) {
    case 0:
        l10_w8(&o, self + 6, st + 1);
        l10_w8(&o, ent + 0x63, 0);
        l10_w16(&o, ent + 0x50, 0);
        l10_w16(&o, ent + 0x54, 1);
        l10_w8(&o, ent + 0x6B, 1);
        l10_w32(&o, ent + 0x3C, 0);
        l10_w32(&o, ent + 0x34, F_2);
        l10_w8(&o, self + 0x5C, 2);
        anim(&o, self, 0x1E, F_5);
        state1(&o, self, ent);
        break;
    case 1:
        state1(&o, self, ent);
        break;
    case 2:
        if (l10_u16(&o, ent + 0x58) & 0x1000) {
            l10_w8(&o, self + 6, st + 1);
            anim(&o, self, 0x20, F_ONE);
        }
        break;
    case 3:
        state3(&o, self);
        break;
    case 4:
        if (l10_u16(&o, ent + 0x58) & 0x1000) {
            l10_w8(&o, self + 5, 0);
            l10_w8(&o, self + 6, 0);
            l10_w8(&o, ent + 0x62, 0);
            l10_w32(&o, ent + 0x34, F_ONE);
            l10_w16(&o, ent + 0x5A, 0);
            l10_w8(&o, ent + 0x6B, 0);
        }
        break;
    default:
        break;
    }
    if (l10_u8(&o, self + 6) >= 2u && (l10_u8(&o, ent + 0x69) & 1)) {
        uint32_t c = l10_u16(&o, ent + 0x50);
        if (c != 0) {
            l10_w16(&o, ent + 0x50, c - 1);
        } else {
            int32_t r = 0;
            l10_c_00122BB8(&o, &r);
            l10_w16(&o, ent + 0x50, (uint32_t)((l10_sra((uint32_t)r, 9) & 7) + 5));
            uint32_t x = l10_u32(&o, self + 0xB0);
            l10_w32(&o, S_700038A0, x);
            uint32_t y = l10_u32(&o, self + 0xB4);
            l10_w32(&o, S_700038A4, y);
            uint32_t z = l10_u32(&o, self + 0xB8);
            l10_w32(&o, S_700038A8, z);
            l10_w32(&o, S_700038AC, F_ONE);
            uint32_t ey = l10_u32(&o, ent + 0x40);
            l10_w32(&o, S_700038A4, ey);
            l10_c_001EFD90(&o, (int32_t)0x8000001Du, S_700038A0, self + 0xC0);
        }
    }
    l10_c_00132490(&o, self, ent);
    return l10_end(&o);
}

/* ------------------------------------------------------------------------
 * 001833F0 (self; the player stage routine 0015B610 +5 = 2): +0x23F = 2,
 * +0x24C = 0, 001662D0(self); then, when +4 is 1, the byte 0x70003B8D = 0.
 * ---------------------------------------------------------------------- */
int em_level10_port_001833F0(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    l10_w8(&o, self + 0x23F, 2);
    l10_w32(&o, self + 0x24C, 0);
    l10_c_001662D0(&o, self);
    if (l10_u8(&o, self + 4) == 1) l10_w8(&o, S_70003B8D, 0);
    return l10_end(&o);
}

/* ------------------------------------------------------------------------
 * 001885F0 (self): the signed halfword D_002754D4[+0x235 & 1] (the gp
 * table 0x2754D4). em_player_ladder_climb.c already folds this lookup
 * into its translation of the ladder climb; this is the function on its
 * own.
 * ---------------------------------------------------------------------- */
int em_level10_port_001885F0(const EmLevel10PortHooks *h, uint32_t self, int32_t *result, EmLevel10PortFault *fault)
{
    L10 o;
    if (!result || l10_begin(&o, h, fault)) return -1;
    uint32_t bit = l10_u8(&o, self + 0x235) & 1u;
    int32_t v = l10_s16(&o, 0x002754D4u + 2u * bit);
    if (l10_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001E6F60 (n0..n6, the seven argument registers a0..a3, t0..t2): a
 * 0x70-byte GS packet record. pool = D_00275670 + 4 * n0; with rec the
 * word pool +0x10 (read again before each of the first three stores):
 * rec +3 = 0x10, rec +4 = 0, rec +0 = 6 (halfword); t = pool +0x10; pool
 * +0x10 = t + 0x70; the quadword at t + 0x10 = 0; t +0x1C = 0x50000005;
 * the doublewords t +0x20.. = 0x1000000000008001, 0xE, 0x8000000068,
 * 0x42, 0x4400000000008001, 0x4410, 0x46, n6 (zero-extended), then
 * (n5 << 32) | sext(n1 | n2 << 16) and (n5 << 32) | sext(n3 | n4 << 16)
 * (the low word sign-extended before the OR, as the instructions do).
 * ---------------------------------------------------------------------- */
int em_level10_port_001E6F60(const EmLevel10PortHooks *h, int32_t n0, int32_t n1, int32_t n2, int32_t n3,
                             int32_t n4, int32_t n5, int32_t n6, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    uint64_t high = (uint64_t)(uint32_t)n5 << 32;
    uint32_t lo1 = (uint32_t)n1 | ((uint32_t)n2 << 16);
    uint32_t lo2 = (uint32_t)n3 | ((uint32_t)n4 << 16);
    uint64_t w60 = high | (uint64_t)(int64_t)(int32_t)lo1;
    uint64_t w68 = high | (uint64_t)(int64_t)(int32_t)lo2;
    uint32_t pool = l10_u32(&o, 0x00275670u) + ((uint32_t)n0 << 2);
    uint32_t rec = l10_u32(&o, pool + 0x10);
    l10_w8(&o, rec + 3, 0x10);
    rec = l10_u32(&o, pool + 0x10);
    l10_w32(&o, rec + 4, 0);
    rec = l10_u32(&o, pool + 0x10);
    l10_w16(&o, rec, 6);
    uint32_t t = l10_u32(&o, pool + 0x10);
    l10_w32(&o, pool + 0x10, t + 0x70);
    static const uint8_t zero[16] = {0};
    l10_wq(&o, t + 0x10, zero);
    l10_w32(&o, t + 0x1C, 0x50000005u);
    l10_w64(&o, t + 0x20, UINT64_C(0x1000000000008001));
    l10_w64(&o, t + 0x28, UINT64_C(0xE));
    l10_w64(&o, t + 0x30, UINT64_C(0x8000000068));
    l10_w64(&o, t + 0x38, UINT64_C(0x42));
    l10_w64(&o, t + 0x40, UINT64_C(0x4400000000008001));
    l10_w64(&o, t + 0x48, UINT64_C(0x4410));
    l10_w64(&o, t + 0x50, UINT64_C(0x46));
    l10_w64(&o, t + 0x58, (uint64_t)(uint32_t)n6);
    l10_w64(&o, t + 0x60, w60);
    l10_w64(&o, t + 0x68, w68);
    return l10_end(&o);
}

/* ------------------------------------------------------------------------
 * AREA13 0x8236E0 (no argument; AREA13's load-time function): D_00275C28
 * = 0x20, D_00275C2C = 0, D_00275C24 = 0, D_00275C1C = 0x82E280 (the
 * overlay's data address), in that order.
 * ---------------------------------------------------------------------- */
int em_level10_port_008236E0(const EmLevel10PortHooks *h, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    l10_w32(&o, 0x00275C28u, 0x20);
    l10_w32(&o, 0x00275C2Cu, 0);
    l10_w32(&o, 0x00275C24u, 0);
    l10_w32(&o, 0x00275C1Cu, 0x0082E280u);
    return l10_end(&o);
}
