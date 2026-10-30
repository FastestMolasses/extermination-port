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
