/* The fourteenth level: the 0012E3A0 creatures' state handlers the a19d
 * census found new (00131650 state 2 with its pieces 00131740 / 00131B10 /
 * 001339E0, 00131E80 state 3, the lunge point 00131F20) and the AREA15
 * enemy 00153950 with its states 00153A10 / 00153A90.
 * docs/LEVEL14_PORT.md section 2. Ground truth: the original instructions
 * (the test runs every entry against them). */
#include "em_level14_port_internal.h"

#define F_ONE 0x3F800000u
#define F_ZERO 0x00000000u
#define F_5 0x40A00000u
#define F_300 0x43960000u
#define F_1_3 0x3FA66666u
#define F_0_005 0x3BA3D70Au

/* ------------------------------------------------------------------------
 * The states of 0012E840's +5 the first boss adds (lane F1; byte-matched
 * decomp C, each checked against the original instructions by the test).
 *
 * 0012F980 (+5 3, the climb's start): +6 0: +6 + 1, ent +0x34 = 1.0, ent
 * +0x3C = 0, clip 0xB at 5, 0; then (and in +6 1) on ent +0x58 bit 0x1000:
 * +5 = 4, +6 = 0, ent +0x3C = 0.4, ent +0x38 = 2.1, sound 0x7D3. Always
 * 00132490(self, ent).
 * ---------------------------------------------------------------------- */
int em_level14_port_0012F980(const H14 *h, uint32_t self, uint32_t ent, F14 *fault)
{
    L14 state, *o = &state;
    if (l14_begin(o, h, fault)) return -1;
    uint32_t st = l14_u8(o, self + 6);
    if (st == 0) {
        l14_w8(o, self + 6, st + 1u);
        l14_w32(o, ent + 0x34, F_ONE);
        l14_w32(o, ent + 0x3C, F_ZERO);
        l14_c_001C67E0(o, self, 0xB, F_5, F_ZERO);
    }
    if (!l14_failed(o) && st <= 1 && (l14_u16(o, ent + 0x58) & 0x1000u)) {
        l14_w8(o, self + 5, 4);
        l14_w8(o, self + 6, 0);
        l14_w32(o, ent + 0x3C, 0x3ECCCCCDu);
        l14_w32(o, ent + 0x38, 0x40066666u);
        l14_c_001FBD50(o, self, 0x7D3, 0, F_300);
    }
    l14_c_00132490(o, self, ent);
    return l14_end(o);
}

/* ------------------------------------------------------------------------
 * 0012FA50 (+5 4, the fall): +6 0: +6 + 1, ent +0x61 = +0x63 = 0, ent +0x69
 * &= 1, ent +0x34 = 1.0, ent +0x6B = 0, clip 0xC at 5, 0, the +0x20 object's
 * +4 = 3 and +0x20 = 0 (when it has one). +6 0 / 1: ent +0x69 == 1: |= 2,
 * the splash at (+0xB0, ent +0x40, +0xB8, 1): 001EFD90(0x80000016,
 * 0x700038A0, self + 0xC0), 001E8B90(0x700038A0, 5.0). ent +0x38 <= 0 with
 * ent +0x60 bit 1: +6 + 1, ent +0x3C = 0, clip 0xD at 1, 0, sound 0x7D3. +6
 * 2: on ent +0x58 bit 0x1000 +5 = +6 = 0. Always 00132490(self, ent).
 * ---------------------------------------------------------------------- */
int em_level14_port_0012FA50(const H14 *h, uint32_t self, uint32_t ent, F14 *fault)
{
    L14 state, *o = &state;
    if (l14_begin(o, h, fault)) return -1;
    uint32_t st = l14_u8(o, self + 6);
    if (st == 0) {
        l14_w8(o, self + 6, st + 1u);
        l14_w8(o, ent + 0x61, 0);
        l14_w8(o, ent + 0x63, 0);
        l14_w8(o, ent + 0x69, l14_u8(o, ent + 0x69) & 1u);
        l14_w32(o, ent + 0x34, F_ONE);
        l14_w8(o, ent + 0x6B, 0);
        if (l14_c_001C67E0(o, self, 0xC, F_5, F_ZERO)) return l14_end(o);
        uint32_t clip = l14_u32(o, self + 0x20);
        if (clip != 0) {
            l14_w8(o, clip + 4, 3);
            l14_w32(o, self + 0x20, 0);
        }
    }
    if (st <= 1) {
        uint32_t f69 = l14_u8(o, ent + 0x69);
        if (f69 == 1) {
            l14_w8(o, ent + 0x69, f69 | 2u);
            l14_w32(o, 0x700038A0u, l14_u32(o, self + 0xB0));
            l14_w32(o, 0x700038A4u, l14_u32(o, self + 0xB4));
            l14_w32(o, 0x700038A8u, l14_u32(o, self + 0xB8));
            l14_w32(o, 0x700038ACu, F_ONE);
            l14_w32(o, 0x700038A4u, l14_u32(o, ent + 0x40));
            if (l14_c_001EFD90(o, (int32_t)0x80000016u, 0x700038A0u, self + 0xC0)) return l14_end(o);
            if (l14_c_001E8B90(o, 0x700038A0u, F_5)) return l14_end(o);
        }
        if (L14_LE(l14_u32(o, ent + 0x38), F_ZERO) && (l14_u8(o, ent + 0x60) & 2u)) {
            l14_w8(o, self + 6, l14_u8(o, self + 6) + 1u);
            l14_w32(o, ent + 0x3C, F_ZERO);
            if (l14_c_001C67E0(o, self, 0xD, F_ONE, F_ZERO)) return l14_end(o);
            l14_c_001FBD50(o, self, 0x7D3, 0, F_300);
        }
    } else if (st == 2) {
        if (l14_u16(o, ent + 0x58) & 0x1000u) {
            l14_w8(o, self + 5, 0);
            l14_w8(o, self + 6, 0);
        }
    }
    l14_c_00132490(o, self, ent);
    return l14_end(o);
}

/* ------------------------------------------------------------------------
 * 00131210 (+5 8, the change): +6 0: +6 + 1, ent +0x3C = 0, ent +0x34 = 1.0,
 * clip 0x2C at 5, 0. 1: on ent +0x58 bit 0x1000: +6 + 1, sound 0x7D9, clip
 * 0x2D at 1, 0. 2: ent +0x6A = 100; on the bit: +6 + 1, 001EFE00(0x8000002B,
 * self), clip 0x2E at 1, 0, sound 0x7DA. 3: ent +0x6A = 100; on the bit with
 * +0x60 == 1.3: +6 + 1, clip 0x2F at 1, 0, sound 0x7DB; then always +0x60
 * grows by (0.005, 0.005, 0.005, 0) (001028B8), past 1.3 the scale (1.3, 1.3,
 * 1.3, 1). 4: ent +0x6A = 100; on the bit: +5 = +6 = 0; +0x34 = 300 / 180
 * (+0xD bit 0) or 250 / 150 by D_0081070A, +0xD |= 0x80, ent +0x6A = 0, sound
 * 0x7DC, 001C63E0(self, 5). Always 00132490(self, ent).
 * ---------------------------------------------------------------------- */
int em_level14_port_00131210(const H14 *h, uint32_t self, uint32_t ent, F14 *fault)
{
    L14 state, *o = &state;
    if (l14_begin(o, h, fault)) return -1;
    uint32_t st = l14_u8(o, self + 6), p = 0;
    switch (st) {
    case 0:
        l14_w8(o, self + 6, st + 1u);
        l14_w32(o, ent + 0x3C, F_ZERO);
        l14_w32(o, ent + 0x34, F_ONE);
        l14_c_001C67E0(o, self, 0x2C, F_5, F_ZERO);
        break;
    case 1:
        if (l14_u16(o, ent + 0x58) & 0x1000u) {
            l14_w8(o, self + 6, st + 1u);
            if (l14_c_001FBD50(o, self, 0x7D9, 0, F_300)) break;
            l14_c_001C67E0(o, self, 0x2D, F_ONE, F_ZERO);
        }
        break;
    case 2:
        l14_w8(o, ent + 0x6A, 0x64);
        if (l14_u16(o, ent + 0x58) & 0x1000u) {
            l14_w8(o, self + 6, l14_u8(o, self + 6) + 1u);
            if (l14_c_001EFE00(o, (int32_t)0x8000002Bu, self, &p)) break;
            if (l14_c_001C67E0(o, self, 0x2E, F_ONE, F_ZERO)) break;
            l14_c_001FBD50(o, self, 0x7DA, 0, F_300);
        }
        break;
    case 3:
        l14_w8(o, ent + 0x6A, 0x64);
        if (l14_u16(o, ent + 0x58) & 0x1000u) {
            if (L14_EQ(l14_u32(o, self + 0x60), F_1_3)) {
                l14_w8(o, self + 6, l14_u8(o, self + 6) + 1u);
                if (l14_c_001C67E0(o, self, 0x2F, F_ONE, F_ZERO)) break;
                if (l14_c_001FBD50(o, self, 0x7DB, 0, F_300)) break;
            }
        }
        l14_w32(o, 0x700038A0u, F_0_005);
        l14_w32(o, 0x700038A4u, F_0_005);
        l14_w32(o, 0x700038A8u, F_0_005);
        l14_w32(o, 0x700038ACu, 0);
        if (l14_c_001028B8(o, self + 0x60, 0x700038A0u, self + 0x60)) break;
        if (!L14_LE(l14_u32(o, self + 0x60), F_1_3)) {
            l14_w32(o, self + 0x60, F_1_3);
            l14_w32(o, self + 0x64, F_1_3);
            l14_w32(o, self + 0x68, F_1_3);
            l14_w32(o, self + 0x6C, F_ONE);
        }
        break;
    case 4:
        l14_w8(o, ent + 0x6A, 0x64);
        if (l14_u16(o, ent + 0x58) & 0x1000u) {
            l14_w8(o, self + 5, 0);
            l14_w8(o, self + 6, 0);
            if (l14_u8(o, self + 0xD) & 1u)
                l14_w16(o, self + 0x34, l14_u8(o, 0x0081070Au) != 0 ? 0x12C : 0xB4);
            else
                l14_w16(o, self + 0x34, l14_u8(o, 0x0081070Au) != 0 ? 0xFA : 0x96);
            l14_w8(o, self + 0xD, l14_u8(o, self + 0xD) | 0x80u);
            l14_w8(o, ent + 0x6A, 0);
            if (l14_c_001FBD50(o, self, 0x7DC, 0, F_300)) break;
            l14_c_001C63E0(o, self, 5);
        }
        break;
    default:
        break;
    }
    l14_c_00132490(o, self, ent);
    return l14_end(o);
}

/* ------------------------------------------------------------------------
 * 00131510 (+5 9, the boss hides while the player is out of its area): +6 0:
 * +6 + 1, ent +0x58 = 0, ent +0x3C = 0, ent +0x34 = 1.0, clip 0x30 at 1, 0;
 * then (and in +6 1) on ent +0x58 bit 0x1000: +6 + 1, clip 0x31. 2: ent +0x6A
 * = 100; 001333F0(self, ent) nonzero: +6 + 1, ent +0x52 = 0, ent +0x56 = 120,
 * clip 0x32. 3: on the bit: +5 = 1, +6 = 0, ent +0x6A = 90.
 * ---------------------------------------------------------------------- */
int em_level14_port_00131510(const H14 *h, uint32_t self, uint32_t ent, F14 *fault)
{
    L14 state, *o = &state;
    if (l14_begin(o, h, fault)) return -1;
    uint32_t st = l14_u8(o, self + 6);
    int32_t r = 0;
    switch (st) {
    case 0:
        l14_w8(o, self + 6, st + 1u);
        l14_w16(o, ent + 0x58, 0);
        l14_w32(o, ent + 0x3C, F_ZERO);
        l14_w32(o, ent + 0x34, F_ONE);
        if (l14_c_001C67E0(o, self, 0x30, F_ONE, F_ZERO)) break;
        /* fall through */
    case 1:
        if (l14_u16(o, ent + 0x58) & 0x1000u) {
            l14_w8(o, self + 6, l14_u8(o, self + 6) + 1u);
            l14_c_001C67E0(o, self, 0x31, F_ONE, F_ZERO);
        }
        break;
    case 2:
        l14_w8(o, ent + 0x6A, 100);
        if (l14_c_001333F0(o, self, ent, &r)) break;
        if (r != 0) {
            l14_w8(o, self + 6, l14_u8(o, self + 6) + 1u);
            l14_w16(o, ent + 0x52, 0);
            l14_w16(o, ent + 0x56, 120);
            l14_c_001C67E0(o, self, 0x32, F_ONE, F_ZERO);
        }
        break;
    case 3:
        if (l14_u16(o, ent + 0x58) & 0x1000u) {
            l14_w8(o, self + 5, 1);
            l14_w8(o, self + 6, 0);
            l14_w8(o, ent + 0x6A, 90);
        }
        break;
    default:
        break;
    }
    return l14_end(o);
}

/* ------------------------------------------------------------------------
 * 00131940 (00131650's +5 1, the thrown reaction): +6 0: +6 + 1, ent +0x63 =
 * 0, ent +0x34 = 1.0, ent +0x38 = 1.5, ent +0x54 = 1, sound 0x7D4; a =
 * 001B1470(0011E620(+0x70, +0x78) - +0xC4); fabs(a) not below pi/2: ent +0x3C
 * = -1, +0xC4 = 001B1470(pi + (+0xC4 + a)), clip 0x25; else ent +0x3C = 1,
 * +0xC4 = 001B1470(+0xC4 + a), clip 0x26 (both at 0, 0). +6 1, with ent +0x60
 * bit 1 (on the ground): ent +0x54 nonzero: 0 and sound 0x7D5; ent +0x3C = 0;
 * on ent +0x58 bit 0x1000: with +0x34 nonzero ent +0x61 = 1, +4 = 1, +5 = +6
 * = 0, +0x36 = 0, +0 = 1, ent +0x6A = 0x3C; else +5 = 2, +6 = 0.
 * ---------------------------------------------------------------------- */
void l14_00131940(L14 *o, uint32_t self, uint32_t ent)
{
    uint32_t st = l14_u8(o, self + 6);
    if (st == 0) {
        l14_w8(o, self + 6, st + 1u);
        l14_w8(o, ent + 0x63, 0);
        l14_w32(o, ent + 0x34, F_ONE);
        l14_w32(o, ent + 0x38, 0x3FC00000u);
        l14_w16(o, ent + 0x54, 1);
        if (l14_c_001FBD50(o, self, 0x7D4, 0, F_300)) return;
        uint32_t f13 = l14_u32(o, self + 0x78);
        uint32_t f12 = l14_u32(o, self + 0x70);
        uint32_t ang = 0, a = 0, m = 0, turned = 0;
        if (l14_c_0011E620(o, f12, f13, &ang)) return;
        if (l14_c_001B1470(o, L14_SUB(ang, l14_u32(o, self + 0xC4)), &a)) return;
        if (l14_c_0011DF78(o, a, &m)) return;
        if (!L14_LT(m, 0x3FC90FDBu)) {
            l14_w32(o, ent + 0x3C, 0xBF800000u);
            if (l14_c_001B1470(o, L14_ADD(0x40490FDBu, L14_ADD(l14_u32(o, self + 0xC4), a)), &turned)) return;
            l14_w32(o, self + 0xC4, turned);
            l14_c_001C67E0(o, self, 0x25, F_ZERO, F_ZERO);
        } else {
            l14_w32(o, ent + 0x3C, F_ONE);
            if (l14_c_001B1470(o, L14_ADD(l14_u32(o, self + 0xC4), a), &turned)) return;
            l14_w32(o, self + 0xC4, turned);
            l14_c_001C67E0(o, self, 0x26, F_ZERO, F_ZERO);
        }
    } else if (st == 1) {
        if (l14_u8(o, ent + 0x60) & 2u) {
            if (l14_u16(o, ent + 0x54) != 0) {
                l14_w16(o, ent + 0x54, 0);
                if (l14_c_001FBD50(o, self, 0x7D5, 0, F_300)) return;
            }
            l14_w32(o, ent + 0x3C, F_ZERO);
            if (l14_u16(o, ent + 0x58) & 0x1000u) {
                if (l14_s16(o, self + 0x34) != 0) {
                    l14_w8(o, ent + 0x61, 1);
                    l14_w8(o, self + 4, 1);
                    l14_w8(o, self + 5, 0);
                    l14_w8(o, self + 6, 0);
                    l14_w16(o, self + 0x36, 0);
                    l14_w8(o, self, 1);
                    l14_w8(o, ent + 0x6A, 0x3C);
                } else {
                    l14_w8(o, self + 5, 2);
                    l14_w8(o, self + 6, 0);
                }
            }
        }
    }
}

int em_level14_port_00131940(const H14 *h, uint32_t self, uint32_t ent, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    l14_00131940(&o, self, ent);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00131740 (self, ent): 00131650's +5 0. +6 0: +6 + 1, ent +0x63 = 0, ent
 * +0x3C = 0, ent +0x34 = 1.0, sound 0x7D4; with ent +0x61 set, the +0x2C
 * byte 4 and (float)001C6160(self) - +0x3C >= 65: ent +0x61 = 0; ent +0x61
 * still set -> clip 0x27 (001C67E0 at 0, 0); else +0 = 1 when +0x34 is
 * nonzero, then +0x36 bit 15 -> clip 0x28 and sound 0x7D6, else the clip
 * D_00275390[(rand >> 11) % 3]; +0x36 = 0. +6 1: ent +0x58 bit 0x1000 ->
 * with +0x34 nonzero the reset (+0 = 1, +0x36 = 0, +4 = 1, +5 = +6 = 0,
 * ent +0x62 = 0, ent +0x6A = 0x3C), else +5 = 2, +6 = 0; without the bit
 * and ent +0x61 0: 00133A20(self, ent).
 * ---------------------------------------------------------------------- */
void l14_00131740(L14 *o, uint32_t self, uint32_t ent)
{
    uint32_t st = l14_u8(o, self + 6);
    if (st == 0) {
        l14_w8(o, self + 6, st + 1u);
        l14_w8(o, ent + 0x63, 0);
        l14_w32(o, ent + 0x3C, 0);
        l14_w32(o, ent + 0x34, F_ONE);
        if (l14_c_001FBD50(o, self, 0x7D4, 0, F_300)) return;
        if (l14_u8(o, ent + 0x61) != 0 && l14_u8(o, self + 0x2C) == 4) {
            int32_t t = 0;
            if (l14_c_001C6160(o, self, &t)) return;
            if (!L14_LT(L14_SUB(L14_CVT_S_W(t), l14_u32(o, self + 0x3C)), 0x42820000u)) l14_w8(o, ent + 0x61, 0);
        }
        if (l14_u8(o, ent + 0x61) != 0) {
            l14_c_001C67E0(o, self, 0x27, F_ZERO, F_ZERO);
            return;
        }
        if (l14_s16(o, self + 0x34) != 0) l14_w8(o, self, 1);
        if (l14_u16(o, self + 0x36) & 0x8000u) {
            if (l14_c_001C67E0(o, self, 0x28, F_ZERO, F_ZERO)) return;
            if (l14_c_001FBD50(o, self, 0x7D6, 0, F_300)) return;
        } else {
            int32_t r = 0;
            if (l14_c_00122BB8(o, &r)) return;
            int32_t k = l14_sra((uint32_t)r, 11) % 3;
            if (l14_c_001C67E0(o, self, l14_s16(o, 0x00275390u + 2u * (uint32_t)k), F_ZERO, F_ZERO)) return;
        }
        l14_w16(o, self + 0x36, 0);
    } else if (st == 1) {
        if (l14_u16(o, ent + 0x58) & 0x1000u) {
            if (l14_s16(o, self + 0x34) != 0) {
                l14_w8(o, self, 1);
                l14_w16(o, self + 0x36, 0);
                l14_w8(o, self + 4, 1);
                l14_w8(o, self + 5, 0);
                l14_w8(o, self + 6, 0);
                l14_w8(o, ent + 0x62, 0);
                l14_w8(o, ent + 0x6A, 0x3C);
                return;
            }
            l14_w8(o, self + 5, 2);
            l14_w8(o, self + 6, 0);
            return;
        }
        if (l14_u8(o, ent + 0x61) == 0) l14_c_00133A20(o, self, ent);
    }
}

int em_level14_port_00131740(const H14 *h, uint32_t self, uint32_t ent, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    l14_00131740(&o, self, ent);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00131B10 (self, ent): 00131650's +5 2. +6 0: +6 + 1, 001B1190(+0x9A),
 * sound 0x7D8; +0xD bit 1 -> +6 = 3, ent +0x34 = 1.0, D_008107EA = 1, clip
 * 4 / 5 by ent +0x61; else ent +0x34 = 1.0, clip 0x29 with ent +0x61, else
 * 0x2A / 0x2B by rand bit 9 (clips at 5.0, 0). +6 1: the clip (+0x2C
 * without bit 15) 0x2A with +0x3C == 34 or 0x2B with 21 -> sound 0x7D5; on
 * ent +0x58 bit 0x1000 +6 + 1 and, without +0xD bit 1, ent +0x6D = 0, sound
 * 0x7D7, 001EFE00(0x8000001E, self) zero -> +4 = 3. +6 2: +0xD bit 1,
 * D_0028A9A2 set and D_0028A9A0 == 2 -> +4 = 3 and the +0x24 object's +4 =
 * 3, +0x24 = 0. +6 3: D_008107EA == 2 -> +6 = 1, ent +0x34 = 0.35, +0x24 =
 * 001EFE00(0x80000046, self), clip 0x2A / 0x2B by rand bit 9.
 * ---------------------------------------------------------------------- */
void l14_00131B10(L14 *o, uint32_t self, uint32_t ent)
{
    uint32_t st = l14_u8(o, self + 6);
    int32_t r = 0;
    if (st == 0) {
        l14_w8(o, self + 6, st + 1u);
        if (l14_c_001B1190(o, (int32_t)l14_u8(o, self + 0x9A))) return;
        if (l14_c_001FBD50(o, self, 0x7D8, 0, F_300)) return;
        if (l14_u8(o, self + 0xD) & 2u) {
            l14_w8(o, self + 6, 3);
            l14_w32(o, ent + 0x34, F_ONE);
            l14_w8(o, 0x008107EAu, 1);
            l14_c_001C67E0(o, self, l14_u8(o, ent + 0x61) != 0 ? 4 : 5, F_5, F_ZERO);
            return;
        }
        l14_w32(o, ent + 0x34, F_ONE);
        if (l14_u8(o, ent + 0x61) != 0) {
            l14_c_001C67E0(o, self, 0x29, F_5, F_ZERO);
            return;
        }
        if (l14_c_00122BB8(o, &r)) return;
        l14_c_001C67E0(o, self, (l14_sra((uint32_t)r, 9) & 1) ? 0x2A : 0x2B, F_5, F_ZERO);
    } else if (st == 1) {
        uint32_t clip = (uint32_t)l14_s16(o, self + 0x2C) & 0xFFFF7FFFu;
        if (clip == 0x2A && L14_EQ(l14_u32(o, self + 0x3C), 0x42080000u)) {
            if (l14_c_001FBD50(o, self, 0x7D5, 0, F_300)) return;
        } else if (clip == 0x2B && L14_EQ(l14_u32(o, self + 0x3C), 0x41A80000u)) {
            if (l14_c_001FBD50(o, self, 0x7D5, 0, F_300)) return;
        }
        if (l14_failed(o)) return;
        if (l14_u16(o, ent + 0x58) & 0x1000u) {
            l14_w8(o, self + 6, l14_u8(o, self + 6) + 1u);
            if (!(l14_u8(o, self + 0xD) & 2u)) {
                uint32_t p = 0;
                l14_w8(o, ent + 0x6D, 0);
                if (l14_c_001FBD50(o, self, 0x7D7, 0, F_300)) return;
                if (l14_c_001EFE00(o, (int32_t)0x8000001Eu, self, &p)) return;
                if (p == 0) l14_w8(o, self + 4, 3);
            }
        }
    } else if (st == 2) {
        if ((l14_u8(o, self + 0xD) & 2u) && l14_u8(o, 0x0028A9A2u) != 0 && l14_s16(o, 0x0028A9A0u) == 2) {
            l14_w8(o, self + 4, 3);
            uint32_t link = l14_u32(o, self + 0x24);
            if (link != 0) {
                l14_w8(o, link + 4, 3);
                l14_w32(o, self + 0x24, 0);
            }
        }
    } else if (st == 3) {
        if (l14_u8(o, 0x008107EAu) == 2) {
            uint32_t p = 0;
            l14_w8(o, self + 6, 1);
            l14_w32(o, ent + 0x34, 0x3EB33333u);
            if (l14_c_001EFE00(o, (int32_t)0x80000046u, self, &p)) return;
            l14_w32(o, self + 0x24, p);
            if (l14_c_00122BB8(o, &r)) return;
            l14_c_001C67E0(o, self, (l14_sra((uint32_t)r, 9) & 1) ? 0x2A : 0x2B, F_5, F_ZERO);
        }
    }
}

int em_level14_port_00131B10(const H14 *h, uint32_t self, uint32_t ent, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    l14_00131B10(&o, self, ent);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 001339E0 (a0, ent): with ent +0x65 set: ent +0x65 = 0, 0021C040(0x8102B0,
 * a0).
 * ---------------------------------------------------------------------- */
void l14_001339E0(L14 *o, uint32_t a0, uint32_t ent)
{
    if (l14_u8(o, ent + 0x65) == 0) return;
    l14_w8(o, ent + 0x65, 0);
    l14_c_0021C040(o, 0x008102B0u, a0);
}

int em_level14_port_001339E0(const H14 *h, uint32_t a0, uint32_t ent, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    l14_001339E0(&o, a0, ent);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00131650 (self, ent): state 2 (word assembly; behaviour read from the
 * original's runs over every sub-state, docs/LEVEL14_PORT.md section 2).
 * By +5: 0 00131740(self, ent), 1 00131940(self, ent), 2 00131B10(self,
 * ent), others nothing. Then the +0x20 object (if any) gets +4 = 3 and
 * +0x20 = 0; 001339E0(self, ent); 00132490(self, ent), 001328D0(self, ent);
 * ent +0x58 (halfword) = 001C64F0(self, ent +0x34); 00131ED0, 001C68C0,
 * 001B17A0, the +0x4C method.
 * ---------------------------------------------------------------------- */
int em_level14_port_00131650(const H14 *h, uint32_t self, uint32_t ent, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t sub = l14_u8(&o, self + 5);
    if (sub == 0) l14_00131740(&o, self, ent);
    else if (sub == 1) l14_00131940(&o, self, ent);
    else if (sub == 2) l14_00131B10(&o, self, ent);
    if (l14_failed(&o)) return -1;
    uint32_t pending = l14_u32(&o, self + 0x20);
    if (pending) {
        l14_w8(&o, pending + 4, 3);
        l14_w32(&o, self + 0x20, 0);
    }
    l14_001339E0(&o, self, ent);
    if (l14_failed(&o)) return -1;
    if (l14_c_00132490(&o, self, ent)) return -1;
    if (l14_c_001328D0(&o, self, ent)) return -1;
    int32_t r = 0;
    if (l14_c_001C64F0(&o, self, l14_u32(&o, ent + 0x34), &r)) return -1;
    l14_w16(&o, ent + 0x58, (uint32_t)r);
    if (l14_c_00131ED0(&o, self)) return -1;
    if (l14_c_001C68C0(&o, self)) return -1;
    if (l14_c_001B17A0(&o, self, &r)) return -1;
    l14_method(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00131E80 (self): state 3: the +0x20 object (if any) gets +4 = 3 and +0x20
 * = 0; 001B1190(+0x9A), 001AFC10(self).
 * ---------------------------------------------------------------------- */
int em_level14_port_00131E80(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t pending = l14_u32(&o, self + 0x20);
    if (pending) {
        l14_w8(&o, pending + 4, 3);
        l14_w32(&o, self + 0x20, 0);
    }
    if (l14_c_001B1190(&o, (int32_t)l14_u8(&o, self + 0x9A))) return -1;
    l14_c_001AFC10(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00131F20 (self, a1, a2): 001029C0(0x700036A0), 00102C58(0x700036A0,
 * 0x700036A0, self + 0xC0), 001026A0(a2, 0x700036A0, a1): a1 through the
 * matrix of self's +0xC0 angles into a2.
 * ---------------------------------------------------------------------- */
int em_level14_port_00131F20(const H14 *h, uint32_t self, uint32_t a1, uint32_t a2, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    if (l14_c_001029C0(&o, 0x700036A0u)) return -1;
    if (l14_c_00102C58(&o, 0x700036A0u, 0x700036A0u, self + 0xC0)) return -1;
    l14_c_001026A0(&o, a2, 0x700036A0u, a1);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00153A10 (self, ent): +4 + 1, +0 = 1, ent +0x10 = 1, +0x34 = 0x3C, +0x30 =
 * 0x275440; 001B10B0(self, 0x72, 0x71) zero -> 001C63E0(self, 0x33), +0x58
 * = D_0028A65C.
 * ---------------------------------------------------------------------- */
void l14_00153A10(L14 *o, uint32_t self, uint32_t ent)
{
    int32_t r = 0;
    l14_w8(o, self + 4, l14_u8(o, self + 4) + 1u);
    l14_w8(o, self, 1);
    l14_w8(o, ent + 0x10, 1);
    l14_w16(o, self + 0x34, 0x3C);
    l14_w32(o, self + 0x30, 0x00275440u);
    if (l14_c_001B10B0(o, self, 0x72, 0x71, &r)) return;
    if (r == 0) {
        if (l14_c_001C63E0(o, self, 0x33)) return;
        l14_w32(o, self + 0x58, l14_u32(o, 0x0028A65Cu));
    }
}

int em_level14_port_00153A10(const H14 *h, uint32_t self, uint32_t ent, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    l14_00153A10(&o, self, ent);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00153A90 (self, out): the hit mailbox +0x36: set with D_0081077B 0 ->
 * +0 = 1, +0x36 = 0; set otherwise -> +4 = 2, +5 = 0. Then unless
 * D_0081077B is 1 with D_008107FB 0: *out = 001C64F0(self, 1.0),
 * 00131ED0, 001C68C0, 001B17A0, the +0x4C method.
 * ---------------------------------------------------------------------- */
void l14_00153A90(L14 *o, uint32_t self, uint32_t out)
{
    if (l14_s16(o, self + 0x36) != 0) {
        if (l14_u8(o, 0x0081077Bu) == 0) {
            l14_w8(o, self, 1);
            l14_w16(o, self + 0x36, 0);
        } else {
            l14_w8(o, self + 4, 2);
            l14_w8(o, self + 5, 0);
        }
    }
    if (l14_u8(o, 0x0081077Bu) != 1 || l14_u8(o, 0x008107FBu) != 0) {
        int32_t r = 0;
        if (l14_c_001C64F0(o, self, F_ONE, &r)) return;
        l14_w32(o, out, (uint32_t)r);
        if (l14_c_00131ED0(o, self)) return;
        if (l14_c_001C68C0(o, self)) return;
        if (l14_c_001B17A0(o, self, &r)) return;
        l14_method(o, self);
    }
}

int em_level14_port_00153A90(const H14 *h, uint32_t self, uint32_t out, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    l14_00153A90(&o, self, out);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00153950 (self): the AREA15 enemy's behaviour (word assembly; behaviour
 * read from the original's runs over every state, docs/LEVEL14_PORT.md
 * section 2). ent = self + 0x1F0. By +4: 0 00153A10(self, ent), 1
 * 00153A90(self, ent), 2 00153B50(self, ent), 3 00153EA0(self, ent) and
 * nothing more; then (not 3) with ent +0x10 set 001B5360(self).
 * ---------------------------------------------------------------------- */
int em_level14_port_00153950(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t ent = self + 0x1F0;
    uint32_t st = l14_u8(&o, self + 4);
    if (st == 3) {
        l14_c_00153EA0(&o, self, ent);
        return l14_end(&o);
    }
    if (st == 0) l14_00153A10(&o, self, ent);
    else if (st == 1) l14_00153A90(&o, self, ent);
    else if (st == 2) l14_c_00153B50(&o, self, ent);
    if (l14_failed(&o)) return -1;
    if (l14_u8(&o, ent + 0x10) != 0) l14_c_001B5360(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 001F88C0 (NEARMISS; the translation follows the instructions), the effect
 * node on a creature's bones (lane F1: the first boss's spores, spawned by
 * 00131650 through 001EFE00). obj = self +0x24, q = self +0x1F0 (q[0] the
 * LCG seed, q[1] the variant, always 0 once set up, q[2] the ring cursor,
 * q[3] the bone count, q[4..] the bones). By +4:
 *  0  q[1] = obj +3 == 9; the bone list by obj +3 (1, 2, 3, 9: the -1
 *     terminated tables D_0025DA40 / 60 / C0 / A0), else 2, 3.. in steps of
 *     rand % 2 + 1 below obj +0xC, at most 12; 001F87E0(self, 0x38), 0 ends
 *     it here; the 0x150 ring slots' +0x1C = -1; q[2] = q[1] = 0; q[0] =
 *     rand; then 1.
 *  1  obj +4 == 3: +4 = 3. Every fourth frame each listed bone gets a ring
 *     slot: unit matrix at 0x700036A0, rotated by a random angle within
 *     +-90 degrees (00102BB0), times the bone's matrix (001026D0), its
 *     quaternion (001C9E40) and position at the slot, age 0. Then for all
 *     0x150 slots, the LCG word w (q[0], never stored back; w * 37 + 11 per
 *     slot) gives 0x70003A20 = ((w >> 16) & 0xFFFF) / 65535 + 1e-4, and a
 *     slot of age >= 0 draws: 001CA1C0, 001D04B0(m, 1, D_0025D920 + q[1] *
 *     0x90, age, phase), age + 0.05, above 1.5 it ends (-1).
 *  2, 3  001AFC10(self); others return.
 * 001F8880(k) is the ring slot: word (D_00275B40)[k / 6] + (k % 6) * 32.
 * 001F87E0(self, n): +0xC = n; above the halfword D_00275BCC: +4 = 3, 0;
 * else +0x110 + 4 i = 001AF780() below +0xC (re-read), 001CB5B0(the last
 * count), +9 = +0xC, +4 = 1, 1.
 * ---------------------------------------------------------------------- */
#define D_0025D920 0x0025D920u
#define D_00275B40 0x00275B40u
#define D_00275BCC 0x00275BCCu
#define S_700036A0 0x700036A0u
#define S_700036D0 0x700036D0u
#define S_70003A20 0x70003A20u
#define S_70003B68 0x70003B68u
#define F_M1 0xBF800000u
#define F_PI 0x40490FDBu
#define F_90 0x42B40000u
#define F_180 0x43340000u

static uint32_t l14_001F8880(L14 *o, uint32_t k)
{
    const uint32_t table = l14_u32(o, D_00275B40);
    return l14_u32(o, table + 4u * (k / 6u)) + ((k % 6u) << 5);
}

static int l14_001F87E0(L14 *o, uint32_t self, uint32_t n)
{
    l14_w8(o, self + 0x0C, n);
    uint32_t count = l14_u8(o, self + 0x0C);
    if (l14_s16(o, D_00275BCC) < (int32_t)count) {
        l14_w8(o, self + 4, 3);
        return 0;
    }
    uint32_t i = 0;
    for (count = l14_u8(o, self + 0x0C); i < count; count = l14_u8(o, self + 0x0C)) {
        uint32_t slot = 0;
        if (l14_c_001AF780(o, &slot)) return 0;
        l14_w32(o, self + 0x110 + 4u * i, slot);
        i++;
    }
    if (l14_c_001CB5B0(o, (int32_t)count)) return 0;
    l14_w8(o, self + 9, l14_u8(o, self + 0x0C));
    l14_w8(o, self + 4, 1);
    return 1;
}

int em_level14_port_001F87E0(const H14 *h, uint32_t self, uint32_t n, int32_t *result, F14 *fault)
{
    L14 state, *o = &state;
    if (!result || l14_begin(o, h, fault)) return -1;
    const int32_t r = l14_001F87E0(o, self, n & 0xFFu);
    if (l14_end(o)) return -1;
    *result = r;
    return 0;
}

int em_level14_port_001F8880(const H14 *h, int32_t k, uint32_t *result, F14 *fault)
{
    L14 state, *o = &state;
    if (!result || l14_begin(o, h, fault)) return -1;
    const uint32_t r = l14_001F8880(o, (uint32_t)k);
    if (l14_end(o)) return -1;
    *result = r;
    return 0;
}

int em_level14_port_001F88C0(const H14 *h, uint32_t self, F14 *fault)
{
    L14 state, *o = &state;
    if (l14_begin(o, h, fault)) return -1;
    const uint32_t st = l14_u8(o, self + 4);
    const uint32_t obj = l14_u32(o, self + 0x24);
    const uint32_t q = self + 0x1F0;
    int32_t r = 0;
    if (st == 2 || st == 3) {
        l14_c_001AFC10(o, self);
        return l14_end(o);
    }
    if (st > 1) return l14_end(o);
    if (st == 0) {
        const uint32_t kind = l14_u8(o, obj + 3);
        l14_w32(o, q + 4, kind == 9);
        uint32_t list = 0;
        switch (l14_u8(o, obj + 3)) {
        case 1: list = 0x0025DA40u; break;
        case 2: list = 0x0025DA60u; break;
        case 3: list = 0x0025DAC0u; break;
        case 9: list = 0x0025DAA0u; break;
        default: break;
        }
        if (!l14_001F87E0(o, self, 0x38)) return l14_end(o);
        l14_w32(o, q + 0xC, 0);
        if (list) {
            for (uint32_t b = l14_u32(o, list); b != 0xFFFFFFFFu && !l14_failed(o);) {
                list += 4;
                l14_w32(o, q + 0x10 + 4u * l14_u32(o, q + 0xC), b);
                l14_w32(o, q + 0xC, l14_u32(o, q + 0xC) + 1u);
                b = l14_u32(o, list);
            }
        } else {
            for (int32_t b = 2; b < (int32_t)l14_u8(o, obj + 0xC) && !l14_failed(o);) {
                l14_w32(o, q + 0x10 + 4u * l14_u32(o, q + 0xC), (uint32_t)b);
                l14_w32(o, q + 0xC, l14_u32(o, q + 0xC) + 1u);
                if ((int32_t)l14_u32(o, q + 0xC) >= 0xC) break;
                if (l14_c_00122BB8(o, &r)) return l14_end(o);
                b += r % 2 + 1;
            }
        }
        for (uint32_t k = 0; k < 0x150u; k++) l14_w32(o, l14_001F8880(o, k) + 0x1C, F_M1);
        l14_w32(o, q + 8, 0);
        l14_w32(o, q + 4, 0);
        if (l14_c_00122BB8(o, &r)) return l14_end(o);
        l14_w32(o, q, (uint32_t)r);
    }
    if (l14_u8(o, obj + 4) == 3) {
        l14_w8(o, self + 4, 3);
        return l14_end(o);
    }
    if ((int32_t)l14_u32(o, S_70003B68) % 4 == 0) {
        for (uint32_t i = 0; (int32_t)i < (int32_t)l14_u32(o, q + 0xC) && !l14_failed(o); i++) {
            if (l14_c_001029C0(o, S_700036A0) || l14_c_00122BB8(o, &r)) return l14_end(o);
            const uint32_t u = L14_DIV(L14_CVT_S_W(r), 0x4F000000u);
            const uint32_t d = L14_SUB(L14_MUL(F_180, u), F_90);
            const uint32_t angle = L14_DIV(L14_MUL(F_PI, d), F_180);
            if (l14_c_00102BB0(o, S_700036A0, S_700036A0, angle)) return l14_end(o);
            const uint32_t bone = l14_u32(o, q + 0x10 + 4u * i);
            const uint32_t node = l14_u32(o, obj + 0x110 + 4u * bone);
            if (l14_c_001026D0(o, S_700036A0, node + 0x90, S_700036A0)) return l14_end(o);
            const uint32_t ent = l14_001F8880(o, l14_u32(o, q + 8));
            if (l14_c_001C9E40(o, ent, S_700036A0)) return l14_end(o);
            l14_w32(o, ent + 0x10, l14_u32(o, S_700036D0));
            l14_w32(o, ent + 0x14, l14_u32(o, S_700036D0 + 4));
            l14_w32(o, ent + 0x18, l14_u32(o, S_700036D0 + 8));
            l14_w32(o, ent + 0x1C, 0);
            l14_w32(o, q + 8, l14_u32(o, q + 8) + 1u);
            if ((int32_t)l14_u32(o, q + 8) >= 0x150) l14_w32(o, q + 8, 0);
        }
    }
    uint32_t w = l14_u32(o, q);
    for (uint32_t k = 0; k < 0x150u && !l14_failed(o); k++) {
        const uint32_t ent = l14_001F8880(o, k);
        const uint32_t phase = L14_ADD(L14_DIV(L14_CVT_S_W((w >> 16) & 0xFFFFu), 0x477FFF00u), 0x38D1B717u);
        l14_w32(o, S_70003A20, phase);
        w = w * 37u + 11u;
        if (L14_LT(l14_u32(o, ent + 0x1C), F_ZERO)) continue;
        if (l14_c_001CA1C0(o, S_700036A0, ent, ent + 0x10)) return l14_end(o);
        const uint32_t table = D_0025D920 + l14_u32(o, q + 4) * 0x90u;
        const uint32_t age = l14_u32(o, ent + 0x1C);
        if (l14_c_001D04B0(o, S_700036A0, 1, table, age, l14_u32(o, S_70003A20))) return l14_end(o);
        const uint32_t grown = L14_ADD(l14_u32(o, ent + 0x1C), 0x3D4CCCCDu);
        l14_w32(o, ent + 0x1C, grown);
        if (!L14_LE(grown, 0x3FC00000u)) l14_w32(o, ent + 0x1C, F_M1);
    }
    return l14_end(o);
}

/* ------------------------------------------------------------------------
 * 001F7E40 (NEARMISS; the translation follows the instructions), the node
 * of effect 0x8000005B: the grey veil over the screen when the first boss
 * has fallen (0x824A40). q = self +0x1F0: q[0] the LCG seed (rand at state
 * 0, stored; the frame's own word w starts from it each frame and is not
 * stored back), q[1] the hold (0x3C), q[2] the fade (0). State 1 draws 32
 * sprites: x = int(512 u) + 0x700, y = int(224 u) + 0x790 (the words
 * 0x700038A0 / A4), width int(320 + 128 u), height int(160 + 64 u) (B0 /
 * B4), a sign from a draw's low bit (+-192), x = int(x + sign * fade *
 * (0.7 + 0.3 u)), centred on the size, the colour (0x80, 0x80, 0x80, int(
 * int(32 + 16 u) * (1 - fade))) at C0..CC; u = ((w >> 16) & 0xFFFF) / 65535
 * + 1e-4, w = w * 37 + 11 per draw; 001F7CF0(A0, B0, C0). Then q[1] - 1;
 * below zero the fade grows by 1/180, and above 1.0 +4 = 3. States 2, 3:
 * 001AFC10(self). 001F7CF0 is one sprite's GS packet at depth 0xFFF000.
 * ---------------------------------------------------------------------- */
#define S_700038A0 0x700038A0u
#define S_700038A4 0x700038A4u
#define S_700038B0 0x700038B0u
#define S_700038B4 0x700038B4u
#define S_700038C0 0x700038C0u
#define S_700038CC 0x700038CCu
#define F_65535 0x477FFF00u
#define F_1EM4 0x38D1B717u

static uint32_t veil_u(uint32_t w)
{
    return L14_ADD(L14_DIV(L14_CVT_S_W((w >> 16) & 0xFFFFu), F_65535), F_1EM4);
}

static void l14_001F7CF0(L14 *o, uint32_t pos, uint32_t end, uint32_t colour)
{
    static const uint8_t zero[16] = {0};
    uint32_t p = 0;
    if (l14_c_001CB5F0(o, 0x007635C0u, 0xFFF000, 8, &p)) return;
    l14_wq(o, p, zero);
    l14_w32(o, p + 0x0C, 0x50000007u);
    l14_w64(o, p + 0x10, UINT64_C(0x602B400000008001));
    l14_w64(o, p + 0x18, UINT64_C(0x0000000000424216));
    l14_w64(o, p + 0x20, UINT64_C(0x2004128555322090));
    l14_w64(o, p + 0x28, UINT64_C(0x2004128555322090));
    for (uint32_t k = 0; k < 16; k += 4) l14_w32(o, p + 0x30 + k, l14_u32(o, colour + k));
    l14_w32(o, p + 0x40, 0);
    l14_w32(o, p + 0x44, 0);
    l14_w32(o, p + 0x48, F_ONE);
    l14_w32(o, p + 0x4C, 0);
    l14_w32(o, p + 0x50, l14_u32(o, pos) << 4);
    l14_w32(o, p + 0x54, l14_u32(o, pos + 4) << 4);
    l14_w32(o, p + 0x58, 0xFFFFFFu);
    l14_w32(o, p + 0x5C, 0);
    l14_w32(o, p + 0x60, F_ONE);
    l14_w32(o, p + 0x64, F_ONE);
    l14_w32(o, p + 0x68, F_ONE);
    l14_w32(o, p + 0x6C, 0);
    l14_w32(o, p + 0x70, l14_u32(o, end) << 4);
    l14_w32(o, p + 0x74, l14_u32(o, end + 4) << 4);
    l14_w32(o, p + 0x78, 0xFFFFFFu);
    l14_w32(o, p + 0x7C, 0);
    l14_c_001CB900(o, 0x007635C0u, 0xFFF000, 1);
}

int em_level14_port_001F7CF0(const H14 *h, uint32_t a0, uint32_t a1, uint32_t a2, F14 *fault)
{
    L14 state, *o = &state;
    if (l14_begin(o, h, fault)) return -1;
    l14_001F7CF0(o, a0, a1, a2);
    return l14_end(o);
}

int em_level14_port_001F7E40(const H14 *h, uint32_t self, F14 *fault)
{
    L14 state, *o = &state;
    if (l14_begin(o, h, fault)) return -1;
    const uint32_t st = l14_u8(o, self + 4);
    const uint32_t q = self + 0x1F0;
    int32_t r = 0;
    if (st == 2 || st == 3) {
        l14_c_001AFC10(o, self);
        return l14_end(o);
    }
    if (st > 1) return l14_end(o);
    if (st == 0) {
        if (l14_c_00122BB8(o, &r)) return l14_end(o);
        l14_w32(o, q, (uint32_t)r);
        l14_w32(o, q + 4, 0x3C);
        l14_w32(o, q + 8, 0);
        l14_w8(o, self + 4, 1);
    }
#define NEXT(w) ((w) * 37u + 11u)
    uint32_t w = l14_u32(o, q);
    for (uint32_t i = 0; i < 0x20u && !l14_failed(o); i++) {
        uint32_t f = L14_MUL(0x44000000u, veil_u(w));       /* 512 */
        w = NEXT(w);
        if (l14_c_001281C0(o, f, &r)) return l14_end(o);
        l14_w32(o, S_700038A0, (uint32_t)r);
        f = L14_MUL(0x43600000u, veil_u(w));                /* 224 */
        w = NEXT(w);
        if (l14_c_001281C0(o, f, &r)) return l14_end(o);
        l14_w32(o, S_700038A4, (uint32_t)r);
        f = L14_ADD(0x43A00000u, L14_MUL(0x43000000u, veil_u(w)));   /* 320 + 128 u */
        w = NEXT(w);
        l14_w32(o, S_700038A0, l14_u32(o, S_700038A0) + 0x700u);
        l14_w32(o, S_700038A4, l14_u32(o, S_700038A4) + 0x790u);
        if (l14_c_001281C0(o, f, &r)) return l14_end(o);
        l14_w32(o, S_700038B0, (uint32_t)r);
        f = L14_ADD(0x43200000u, L14_MUL(0x42800000u, veil_u(w)));   /* 160 + 64 u */
        w = NEXT(w);
        if (l14_c_001281C0(o, f, &r)) return l14_end(o);
        l14_w32(o, S_700038B4, (uint32_t)r);
        l14_w32(o, S_70003A20, ((w >> 16) & 1u) ? 0xC3400000u : 0x43400000u);
        w = NEXT(w);
        const uint32_t sign = l14_u32(o, S_70003A20);
        const uint32_t u = veil_u(w);
        const uint32_t fade = l14_u32(o, q + 8);
        w = NEXT(w);
        const uint32_t x = L14_CVT_S_W(l14_u32(o, S_700038A0));
        const uint32_t shift = L14_MUL(sign, L14_MUL(fade, L14_ADD(0x3F333333u, L14_MUL(0x3E99999Au, u))));
        l14_w32(o, S_70003A20, shift);
        if (l14_c_001281C0(o, L14_ADD(x, shift), &r)) return l14_end(o);
        l14_w32(o, S_700038A0, (uint32_t)r);
        const uint32_t width = l14_u32(o, S_700038B0);
        const uint32_t left = l14_u32(o, S_700038A0);
        const uint32_t height = l14_u32(o, S_700038B4);
        l14_w32(o, S_700038C0, 0x80);
        l14_w32(o, S_700038C0 + 4, 0x80);
        l14_w32(o, S_700038C0 + 8, 0x80);
        l14_w32(o, S_700038A0, left - (uint32_t)((int32_t)width >> 1));
        const uint32_t alpha_u = veil_u(w);
        w = NEXT(w);
        l14_w32(o, S_700038A4, l14_u32(o, S_700038A4) - (uint32_t)((int32_t)height >> 1));
        const uint32_t x0 = l14_u32(o, S_700038A0), y0 = l14_u32(o, S_700038A4);
        l14_w32(o, S_700038B0, width + x0);
        l14_w32(o, S_700038B4, l14_u32(o, S_700038B4) + y0);
        if (l14_c_001281C0(o, L14_ADD(0x42000000u, L14_MUL(0x41800000u, alpha_u)), &r)) return l14_end(o);
        l14_w32(o, S_700038CC, (uint32_t)r);
        const uint32_t a = L14_CVT_S_W(l14_u32(o, S_700038CC));
        const uint32_t faded = L14_MUL(a, L14_SUB(F_ONE, l14_u32(o, q + 8)));
        if (l14_c_001281C0(o, faded, &r)) return l14_end(o);
        l14_w32(o, S_700038CC, (uint32_t)r);
        l14_001F7CF0(o, S_700038A0, S_700038B0, S_700038C0);
    }
#undef NEXT
    l14_w32(o, q + 4, l14_u32(o, q + 4) - 1u);
    if (l14_s32(o, q + 4) < 0) {
        const uint32_t fade = L14_ADD(l14_u32(o, q + 8), 0x3BB60B61u);
        l14_w32(o, q + 8, fade);
        if (!L14_LE(fade, F_ONE)) l14_w8(o, self + 4, 3);
    }
    return l14_end(o);
}
