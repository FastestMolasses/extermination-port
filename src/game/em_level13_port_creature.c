/* The thirteenth level: 001386E0's behaviours 2 (00139240), 3 (00139E00)
 * and 4 (0013A3B0) of the AREA19 creatures (ent = self + 0x1F0), and the
 * tendril pieces of the 001549C0 actors (001545B0, 00154F00).
 * docs/LEVEL13_PORT.md section 2. Ground truth: the original instructions
 * (the test runs every entry against them). */
#include "em_level13_port_internal.h"

#define F_ONE 0x3F800000u
#define F_PI 0x40490FDBu
#define F_HALF_PI 0x3FC90FDBu
#define F_300 0x43960000u
#define F_180 0x43340000u
#define F_255 0x437F0000u
#define F_0_4 0x3ECCCCCDu
#define P_POS 0x00810360u   /* the player's position (D_00810360) */
#define P_BLOCK 0x008102B0u /* the player block */

static uint32_t l13_rand(L13 *o, int *bad)
{
    int32_t r = 0;
    if (l13_c_00122BB8(o, &r)) *bad = 1;
    return (uint32_t)r;
}

/* The creature-to-player staging both 00139240 tests use: 0x700038A0 =
 * the creature's position, 0x700038B0 = the player's, both y words 0;
 * then the planar distance 001B15D0(0x700038A0, 0x700038B0). The player's
 * four words are read first, then the creature's word by word. */
static int l13_stage(L13 *o, uint32_t self, uint32_t *dist)
{
    uint32_t p0 = l13_u32(o, P_POS);
    uint32_t x = l13_u32(o, self + 0xB0);
    uint32_t p1 = l13_u32(o, P_POS + 4);
    uint32_t p2 = l13_u32(o, P_POS + 8);
    uint32_t p3 = l13_u32(o, P_POS + 12);
    l13_w32(o, 0x700038A0u, x);
    l13_w32(o, 0x700038A4u, l13_u32(o, self + 0xB4));
    l13_w32(o, 0x700038A8u, l13_u32(o, self + 0xB8));
    l13_w32(o, 0x700038ACu, l13_u32(o, self + 0xBC));
    l13_w32(o, 0x700038B0u, p0);
    l13_w32(o, 0x700038B4u, p1);
    l13_w32(o, 0x700038B8u, p2);
    l13_w32(o, 0x700038BCu, p3);
    l13_w32(o, 0x700038B4u, 0);
    l13_w32(o, 0x700038A4u, 0);
    return l13_c_001B15D0(o, 0x700038A0u, 0x700038B0u, dist);
}

/* ------------------------------------------------------------------------
 * 00139240 (self, ent): behaviour 2. d = 001B15D0(self +0xB0, the player);
 * 0013C8C0(self, ent). +6 0: +6 = 1, ent +0x24 / +0x22 / +0x2C / +0x30 = 0,
 * +0x4C = 0.65; beyond 50 a roll in the side's row of D_002451A0 (side =
 * bit 7 of +0xD, index (rand >> 11) & 0xF): 0 ends (flag), 1 sets the
 * target speed +0x40 = 0.4, others 0.8. +6 1: with ent +0x80 clear, d <=
 * 50, 0021BE40(0x8102B0, self) zero and 001B1560(self, the player,
 * 0.1745329) set: a roll in D_002451C0 ((rand >> 15) & 0xF): 5 -> the
 * staging and its distance: within 20 ends, else +5 = 5, +6 = 0; others
 * +5 = 4, +6 = 0. Otherwise beyond 100 the count ent +0x22 (241 ends);
 * within 100 it is cleared and, beyond 50 with +0xA bit 7, ent +0x32 zero
 * and the facing test set, +5 = 3, +6 = 0, self +0xC8 = 0, ent +0x48 = 0.
 * Then the speed ease (ent +0x80 set: above 0.4 accel -0.005, else 0.4 /
 * 0; clear: below +0x40 accel 0.001, else +0x40 / 0), the re-aim count
 * ent +0x24 (at 0 with ent +0x80 & 0xC clear: +0x58 = 001B1240 to the
 * player, the staging, within 15 and not facing (1.4835298) the count =
 * 60 + ((rand >> 6) & 0x7F)), the wander count ent +0x20 (120 while ent
 * +0x80 & 3; at 0 = 60 + ((rand >> 3) & 0x3F) and +0x5C = 3.1066861
 * ((rand >> 6) & 0xFF) / 255 - 1.5533431), the wander mirrored by the
 * floor (001B2F70 into 0x700038A0) or the height, the life count ent +0x2C
 * (flag or 301: +5 = 1, +6 = 0, +0x4C = +0x44 = 0.4, +0x48 = 0, +0x2C /
 * +0x30 = 0, +0x2E = 120 + ((rand >> 15) & 0x3F), +0x22 / +0x20 = 0), the
 * yaw ease (001B12B0, pi/120) and the wander turn (pi (1 + 1.8 r) / 180),
 * 0013BBB0 and 0013BA20.
 * ---------------------------------------------------------------------- */
static void l13_00139240_tail(L13 *o, uint32_t self, uint32_t ent, int flag)
{
    int bad = 0;
    if (l13_s8(o, ent + 0x80) != 0) {
        if (L13_LE(l13_u32(o, ent + 0x44), F_0_4)) {
            l13_w32(o, ent + 0x44, F_0_4);
            l13_w32(o, ent + 0x48, 0);
        } else {
            l13_w32(o, ent + 0x48, 0xBBA3D70Au);
        }
    } else {
        uint32_t v = l13_u32(o, ent + 0x44);
        uint32_t t = l13_u32(o, ent + 0x40);
        if (L13_LT(v, t)) {
            l13_w32(o, ent + 0x48, 0x3A83126Fu);
        } else {
            l13_w32(o, ent + 0x44, t);
            l13_w32(o, ent + 0x48, 0);
        }
    }
    uint32_t c = l13_u16(o, ent + 0x24);
    if (c != 0) {
        l13_w16(o, ent + 0x24, c - 1u);
    } else if (!(l13_s8(o, ent + 0x80) & 0xC)) {
        uint32_t px = l13_u32(o, P_POS);
        uint32_t pz = l13_u32(o, P_POS + 8);
        uint32_t a = 0;
        if (l13_c_001B1240(o, self + 0xB0, px, pz, &a)) return;
        l13_w32(o, ent + 0x58, a);
        uint32_t d = 0;
        if (l13_stage(o, self, &d)) return;
        if (L13_LE(d, 0x41700000u)) {
            int32_t f = 0;
            if (l13_c_001B1560(o, self, P_POS, 0x3FBDE44Eu, &f)) return;
            if (f == 0) {
                uint32_t r = l13_rand(o, &bad);
                if (bad) return;
                l13_w16(o, ent + 0x24, ((uint32_t)l13_sra(r, 6) & 0x7Fu) + 0x3Cu);
            }
        }
    }
    if (l13_s8(o, ent + 0x80) & 3) {
        l13_w16(o, ent + 0x20, 0x78);
    } else {
        c = l13_u16(o, ent + 0x20);
        if (c != 0) {
            l13_w16(o, ent + 0x20, c - 1u);
        } else {
            uint32_t r = l13_rand(o, &bad);
            if (bad) return;
            l13_w16(o, ent + 0x20, ((uint32_t)l13_sra(r, 3) & 0x3Fu) + 0x3Cu);
            r = l13_rand(o, &bad);
            if (bad) return;
            uint32_t f = L13_DIV(L13_CVT_S_W((uint32_t)l13_sra(r, 6) & 0xFFu), F_255);
            l13_w32(o, ent + 0x5C, L13_SUB(L13_MUL(0x4046D3F2u, f), 0x3FC6D3F2u));
        }
        int hit = 0;
        if (L13_LT(l13_u32(o, ent + 0x5C), 0)) {
            int32_t r2f = 0;
            if (l13_c_001B2F70(o, self + 0xB0, 0x700038A0u, &r2f)) return;
            hit = r2f != 0;
            if (hit) {
                uint32_t hgt = l13_u32(o, 0x700038A0u);
                uint32_t y = l13_u32(o, self + 0xB4);
                int keep = 0;
                if (L13_LE(L13_ADD(0x41200000u, hgt), y)) keep = L13_LE(l13_u32(o, 0x00810364u), y);
                if (!keep) {
                    uint32_t w = 0;
                    if (l13_c_0011DF78(o, l13_u32(o, ent + 0x5C), &w)) return;
                    l13_w32(o, ent + 0x5C, w);
                }
            }
        }
        /* a miss of the floor probe joins the positive-wander test */
        if (!hit) {
            uint32_t w = l13_u32(o, ent + 0x5C);
            if (!L13_LE(w, 0)) {
                uint32_t py = l13_u32(o, 0x00810364u);
                uint32_t y = l13_u32(o, self + 0xB4);
                if (L13_LT(L13_ADD(0x41700000u, py), y)) l13_w32(o, ent + 0x5C, L13_MUL(w, 0xBF800000u));
            }
        }
    }
    l13_w16(o, ent + 0x2C, l13_u16(o, ent + 0x2C) + 1u);
    if (flag != 0 || l13_u16(o, ent + 0x2C) >= 0x12D) {
        l13_w8(o, self + 5, 1);
        l13_w8(o, self + 6, 0);
        l13_w32(o, ent + 0x4C, F_0_4);
        l13_w32(o, ent + 0x44, F_0_4);
        l13_w32(o, ent + 0x48, 0);
        l13_w16(o, ent + 0x2C, 0);
        l13_w16(o, ent + 0x30, 0);
        uint32_t r = l13_rand(o, &bad);
        if (bad) return;
        l13_w16(o, ent + 0x2E, ((uint32_t)l13_sra(r, 15) & 0x3Fu) + 0x78u);
        l13_w16(o, ent + 0x22, 0);
        l13_w16(o, ent + 0x20, 0);
    }
    uint32_t r = l13_rand(o, &bad);
    if (bad) return;
    uint32_t frac = L13_DIV(L13_CVT_S_W((uint32_t)l13_sra(r, 14) & 0xFFu), F_255);
    uint32_t aim = l13_u32(o, ent + 0x58);
    uint32_t yaw = l13_u32(o, self + 0xC4);
    uint32_t out = 0;
    if (l13_c_001B12B0(o, aim, yaw, 0x3CD67750u, &out)) return;
    l13_w32(o, self + 0xC4, out);
    uint32_t wt = l13_u32(o, ent + 0x5C);
    uint32_t wc = l13_u32(o, ent + 0x50);
    uint32_t rate = L13_DIV(L13_MUL(F_PI, L13_ADD(F_ONE, L13_MUL(0x3FE66666u, frac))), F_180);
    if (l13_c_001B12B0(o, wt, wc, rate, &out)) return;
    l13_w32(o, ent + 0x50, out);
    if (l13_c_0013BBB0(o, self, ent)) return;
    l13_c_0013BA20(o, self, ent);
}

static void l13_00139240(L13 *o, uint32_t self, uint32_t ent)
{
    int flag = 0, bad = 0;
    uint32_t dist = 0;
    if (l13_c_001B15D0(o, self + 0xB0, P_POS, &dist)) return;
    if (l13_c_0013C8C0(o, self, ent)) return;
    uint32_t st = l13_u8(o, self + 6);
    if (st == 0) {
        l13_w8(o, self + 6, st + 1u);
        l13_w16(o, ent + 0x24, 0);
        l13_w16(o, ent + 0x22, 0);
        l13_w32(o, ent + 0x4C, 0x3F266666u);
        l13_w16(o, ent + 0x2C, 0);
        l13_w16(o, ent + 0x30, 0);
        if (!L13_LE(dist, 0x42480000u)) {
            uint32_t tbl = 0x002451A0u + (((l13_u8(o, self + 0xD) & 0x80u) >> 7) << 4);
            uint32_t r = l13_rand(o, &bad);
            if (bad) return;
            int32_t c = l13_s8(o, tbl + ((uint32_t)l13_sra(r, 11) & 0xFu));
            if (c == 0) {
                flag = 1;
            } else {
                l13_w32(o, ent + 0x40, c == 1 ? F_0_4 : 0x3F4CCCCDu);
                flag = 0;
            }
        }
    } else if (st == 1) {
        int near = 0;
        if (l13_s8(o, ent + 0x80) == 0 && L13_LE(dist, 0x42480000u)) {
            int32_t r = 0;
            if (l13_c_0021BE40(o, P_BLOCK, self, &r)) return;
            if (r == 0) {
                if (l13_c_001B1560(o, self, P_POS, 0x3E32B8C3u, &r)) return;
                near = r != 0;
            }
        }
        if (near) {
            uint32_t tbl = 0x002451C0u + (((l13_u8(o, self + 0xD) & 0x80u) >> 7) << 4);
            uint32_t r = l13_rand(o, &bad);
            if (bad) return;
            if (l13_s8(o, tbl + ((uint32_t)l13_sra(r, 15) & 0xFu)) == 5) {
                uint32_t d = 0;
                if (l13_stage(o, self, &d)) return;
                if (L13_LE(d, 0x41A00000u)) {
                    flag = 1;
                } else {
                    l13_w8(o, self + 5, 5);
                    l13_w8(o, self + 6, 0);
                }
            } else {
                l13_w8(o, self + 5, 4);
                l13_w8(o, self + 6, 0);
            }
        } else if (!L13_LE(dist, 0x42C80000u)) {
            uint32_t c = (l13_u16(o, ent + 0x22) + 1u) & 0xFFFFu;
            l13_w16(o, ent + 0x22, c);
            if (c >= 0xF1) flag = 1;
        } else {
            l13_w16(o, ent + 0x22, 0);
            if (!L13_LE(dist, 0x42480000u) && (l13_u8(o, self + 0xA) & 0x80u) && l13_u16(o, ent + 0x32) == 0) {
                int32_t f = 0;
                if (l13_c_001B1560(o, self, P_POS, 0x3E32B8C3u, &f)) return;
                if (f != 0) {
                    l13_w8(o, self + 5, 3);
                    l13_w8(o, self + 6, 0);
                    l13_w32(o, self + 0xC8, 0);
                    l13_w32(o, ent + 0x48, 0);
                }
            }
        }
    }
    l13_00139240_tail(o, self, ent, flag);
}

int em_level13_port_00139240(const H13 *h, uint32_t self, uint32_t ent, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    l13_00139240(&o, self, ent);
    return l13_end(&o);
}

/* ------------------------------------------------------------------------
 * 00139E00 (self, ent): behaviour 3. +6 0: +6 = 1, ent +0x84 = 1, ent
 * +0x20 = 300, 001FBD50(self, 0x817, 0, 300), then as 1. 1: ent +0x81 set,
 * or the count ent +0x20 reaching 0, ends (1). Otherwise the facing test
 * (1.4835298) failing ends after this frame; ent +0x48 = 0; the speed +0x44
 * above 0.8 is cut to 0.8, below 1 accel 0.005; +0x4C = 0.65 (+0x50 < 0)
 * or 0.4; +0x5C = -001B1270(self +0xB0, 7 + the player's y, 5 + self z);
 * +0x58 = 001B1240 to the player; with 0021BE40(0x8102B0, self) zero and
 * 001A7B80(self) set the grab: +6 += 1, self +0xC8 = 0, +0x4C = 0.4,
 * +0x48 / +0x44 / +0x50 = 0, D_008104D4 = 25 / 22 (bit 7 of +0xD, by
 * D_0081070A) or 20 / 18, D_008102B0 |= 2, D_00810320.. = the direction to
 * the player (y 0) normalized (00102760), 001C67E0(self, 5, 0, 0), the
 * matrix 0x700036A0 turned to the player's bearing (001B1240 from the
 * player into 0x700038A4), the point (0, y - py - 1.5, 3) through it plus
 * the player position, 001EFD90(0x80000006, 0x700038B0, 0x700038A0).
 * Without the grab the yaw ease (pi/360) and the turn ease (pi/100),
 * 0013BBB0, 0013BA20. 2: on ent +0x70 bit 0x1000 +6 = 3, ent +0x20 = 80,
 * +0x44 = -0.4, +0x48 = 0.01, +0x5C = pi/2, 001C67E0(self, 4, 0, 0). 3:
 * ent +0x81 set or the count ending ends (2); +0x44 not below 0.4 -> +0x44
 * = 0.4, +0x48 = 0; the turn ease. The end: +5 = 1, +6 = 0, +0x5C = 0,
 * +0x4C = 0.4, +0x48 = 0, +0x44 = 0.4, +0x20 / +0x2C / +0x30 = 0, +0x84 =
 * 0, +0x2E = 180 + ((rand >> 15) & 0x7F) (2) or (rand >> 15) & 0x3F (1).
 * ---------------------------------------------------------------------- */
static int l13_139E00_grab(L13 *o, uint32_t self, uint32_t ent)
{
    l13_w8(o, self + 6, l13_u8(o, self + 6) + 1u);
    l13_w32(o, self + 0xC8, 0);
    l13_w32(o, ent + 0x4C, F_0_4);
    l13_w32(o, ent + 0x48, 0);
    l13_w32(o, ent + 0x44, 0);
    l13_w32(o, ent + 0x50, 0);
    uint32_t hp;
    if (l13_u8(o, self + 0xD) & 0x80u)
        hp = l13_u8(o, 0x0081070Au) != 0 ? 0x41C80000u : 0x41B00000u;
    else
        hp = l13_u8(o, 0x0081070Au) != 0 ? 0x41A00000u : 0x41900000u;
    l13_w32(o, 0x008104D4u, hp);
    l13_w8(o, P_BLOCK, l13_u8(o, P_BLOCK) | 2u);
    uint32_t px = l13_u32(o, P_POS);
    l13_w32(o, 0x00810320u, L13_SUB(px, l13_u32(o, self + 0xB0)));
    l13_w32(o, 0x00810324u, 0);
    uint32_t pz = l13_u32(o, P_POS + 8);
    l13_w32(o, 0x00810328u, L13_SUB(pz, l13_u32(o, self + 0xB8)));
    if (l13_c_00102760(o, 0x00810320u, 0x00810320u)) return -1;
    if (l13_c_001C67E0(o, self, 5, 0, 0)) return -1;
    l13_w32(o, 0x700038A8u, 0);
    l13_w32(o, 0x700038A4u, 0);
    l13_w32(o, 0x700038A0u, 0);
    l13_w32(o, 0x700038ACu, F_ONE);
    if (l13_c_001029C0(o, 0x700036A0u)) return -1;
    uint32_t x = l13_u32(o, self + 0xB0);
    uint32_t z = l13_u32(o, self + 0xB8);
    uint32_t a = 0;
    if (l13_c_001B1240(o, P_POS, x, z, &a)) return -1;
    l13_w32(o, 0x700038A4u, a);
    if (l13_c_00102C58(o, 0x700036A0u, 0x700036A0u, 0x700038A0u)) return -1;
    l13_w32(o, 0x700038B0u, 0);
    uint32_t py = l13_u32(o, 0x00810364u);
    uint32_t y = l13_u32(o, self + 0xB4);
    l13_w32(o, 0x700038B4u, L13_SUB(L13_SUB(y, py), 0x3FC00000u));
    l13_w32(o, 0x700038B8u, 0x40400000u);
    l13_w32(o, 0x700038BCu, F_ONE);
    if (l13_c_001026A0(o, 0x700038B0u, 0x700036A0u, 0x700038B0u)) return -1;
    if (l13_c_001028B8(o, 0x700038B0u, P_POS, 0x700038B0u)) return -1;
    return l13_c_001EFD90(o, (int32_t)0x80000006u, 0x700038B0u, 0x700038A0u);
}

static void l13_00139E00(L13 *o, uint32_t self, uint32_t ent)
{
    int flag = 0, bad = 0;
    uint32_t st = l13_u8(o, self + 6);
    uint32_t out = 0;
    if (st == 3) {
        if (l13_s8(o, ent + 0x81) != 0) {
            flag = 2;
        } else {
            uint32_t c = (l13_u16(o, ent + 0x20) - 1u) & 0xFFFFu;
            l13_w16(o, ent + 0x20, c);
            if (c == 0) flag = 2;
        }
        if (!L13_LT(l13_u32(o, ent + 0x44), F_0_4)) {
            l13_w32(o, ent + 0x44, F_0_4);
            l13_w32(o, ent + 0x48, 0);
        }
        uint32_t cur = l13_u32(o, ent + 0x50);
        uint32_t tgt = l13_u32(o, ent + 0x5C);
        if (l13_c_001B12B0(o, tgt, cur, 0x3D00ADFCu, &out)) return;
        l13_w32(o, ent + 0x50, out);
    } else if (st == 2) {
        if (l13_u32(o, ent + 0x70) & 0x1000u) {
            l13_w8(o, self + 6, st + 1u);
            l13_w16(o, ent + 0x20, 0x50);
            l13_w32(o, ent + 0x44, 0xBECCCCCDu);
            l13_w32(o, ent + 0x48, 0x3C23D70Au);
            l13_w32(o, ent + 0x5C, F_HALF_PI);
            if (l13_c_001C67E0(o, self, 4, 0, 0)) return;
        }
    } else if (st == 1 || st == 0) {
        if (st == 0) {
            l13_w8(o, self + 6, st + 1u);
            l13_w8(o, ent + 0x84, 1);
            l13_w16(o, ent + 0x20, 0x12C);
            if (l13_c_001FBD50(o, self, 0x817, 0, F_300)) return;
        }
        if (l13_s8(o, ent + 0x81) != 0) {
            flag = 1;
        } else {
            uint32_t c = (l13_u16(o, ent + 0x20) - 1u) & 0xFFFFu;
            l13_w16(o, ent + 0x20, c);
            if (c == 0) {
                flag = 1;
            } else {
                int32_t f = 0;
                if (l13_c_001B1560(o, self, P_POS, 0x3FBDE44Eu, &f)) return;
                if (f == 0) flag = 1;
                l13_w32(o, ent + 0x48, 0);
                uint32_t v = l13_u32(o, ent + 0x44);
                if (L13_LE(v, 0x3F4CCCCDu)) {
                    if (L13_LT(v, F_ONE)) l13_w32(o, ent + 0x48, 0x3BA3D70Au);
                } else {
                    l13_w32(o, ent + 0x44, 0x3F4CCCCDu);
                }
                l13_w32(o, ent + 0x4C, L13_LT(l13_u32(o, ent + 0x50), 0) ? 0x3F266666u : F_0_4);
                uint32_t z = l13_u32(o, self + 0xB8);
                uint32_t py = l13_u32(o, 0x00810364u);
                if (l13_c_001B1270(o, self + 0xB0, L13_ADD(0x40E00000u, py), L13_ADD(0x40A00000u, z), &out)) return;
                l13_w32(o, ent + 0x5C, L13_MUL(0xBF800000u, out));
                uint32_t px = l13_u32(o, P_POS);
                uint32_t pz = l13_u32(o, P_POS + 8);
                if (l13_c_001B1240(o, self + 0xB0, px, pz, &out)) return;
                l13_w32(o, ent + 0x58, out);
                int32_t r = 0;
                if (l13_c_0021BE40(o, P_BLOCK, self, &r)) return;
                if (r == 0) {
                    if (l13_c_001A7B80(o, self, &r)) return;
                    if (r != 0) {
                        l13_139E00_grab(o, self, ent);
                        goto end;
                    }
                }
                uint32_t yaw = l13_u32(o, self + 0xC4);
                uint32_t aim = l13_u32(o, ent + 0x58);
                if (l13_c_001B12B0(o, aim, yaw, 0x3C0EFA35u, &out)) return;
                l13_w32(o, self + 0xC4, out);
                uint32_t cur = l13_u32(o, ent + 0x50);
                uint32_t tgt = l13_u32(o, ent + 0x5C);
                if (l13_c_001B12B0(o, tgt, cur, 0x3D00ADFCu, &out)) return;
                l13_w32(o, ent + 0x50, out);
                if (l13_c_0013BBB0(o, self, ent)) return;
                if (l13_c_0013BA20(o, self, ent)) return;
            }
        }
    }
end:
    if (flag != 0) {
        l13_w8(o, self + 5, 1);
        l13_w8(o, self + 6, 0);
        l13_w32(o, ent + 0x5C, 0);
        l13_w32(o, ent + 0x4C, F_0_4);
        l13_w32(o, ent + 0x48, 0);
        l13_w32(o, ent + 0x44, F_0_4);
        l13_w16(o, ent + 0x20, 0);
        l13_w16(o, ent + 0x2C, 0);
        l13_w16(o, ent + 0x30, 0);
        l13_w8(o, ent + 0x84, 0);
        uint32_t r = l13_rand(o, &bad);
        if (bad) return;
        if (flag == 2)
            l13_w16(o, ent + 0x2E, ((uint32_t)l13_sra(r, 15) & 0x7Fu) + 0xB4u);
        else
            l13_w16(o, ent + 0x2E, (uint32_t)l13_sra(r, 15) & 0x3Fu);
    }
}

int em_level13_port_00139E00(const H13 *h, uint32_t self, uint32_t ent, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    l13_00139E00(&o, self, ent);
    return l13_end(&o);
}

/* The matrix 0x700036A0 = identity turned by `yaw` (00102BB0, the yaw word
 * read at `yaw_at` after the identity), the point (0, y, z, 1) at `vec`
 * through it (001026A0), then 001028B8(vec, b1, b2) (the sum's operands in
 * the original's argument order). */
static int l13_turned_point(L13 *o, uint32_t yaw_at, uint32_t vec, uint32_t y, uint32_t z, uint32_t b1, uint32_t b2)
{
    if (l13_c_001029C0(o, 0x700036A0u)) return -1;
    if (l13_c_00102BB0(o, 0x700036A0u, 0x700036A0u, l13_u32(o, yaw_at))) return -1;
    l13_w32(o, vec, 0);
    l13_w32(o, vec + 4, y);
    l13_w32(o, vec + 8, z);
    l13_w32(o, vec + 12, F_ONE);
    if (l13_c_001026A0(o, vec, 0x700036A0u, vec)) return -1;
    return l13_c_001028B8(o, vec, b1, b2);
}

/* The turn cancel both 0013A3B0 states 1 and 9 apply when ent +0x5C != 0:
 * ent +0x81 bit 2 -> +0x5C = +0x50 = 0; otherwise +0x5C = 0 unless y < 15
 * + the player's y. */
static void l13_13A3B0_cancel(L13 *o, uint32_t self, uint32_t ent)
{
    if (l13_s8(o, ent + 0x81) & 4) {
        l13_w32(o, ent + 0x5C, 0);
        l13_w32(o, ent + 0x50, 0);
    } else {
        uint32_t py = l13_u32(o, 0x00810364u);
        uint32_t y = l13_u32(o, self + 0xB4);
        if (!L13_LT(y, L13_ADD(0x41700000u, py))) l13_w32(o, ent + 0x5C, 0);
    }
}

/* ------------------------------------------------------------------------
 * 0013A3B0 (self, ent): behaviour 4, the grab. The yaw rate 1.5 and turn
 * rate 1.0 (degrees a frame) are latched; +6 (a 10-entry switch, 8 and
 * above 9 nothing): 0: +6 += 1, +0x50 = 0, +0x4C = 0.4, +0x84 = 1, +0x5C
 * = pi/2, 001C67E0(self, 6, 5, 0), then as 1. 1: the speed decays (+0x44
 * <= 0 -> +0x48 = +0x44 = 0, else accel -0.005); +0x5C 0 -> turn rate 2.5,
 * else the cancel; with +0x50, +0x5C, +0x44 all 0 and the pose word +0x2C
 * (sign-extended, bit 15 masked) 7: +6 += 1, +0x20 = 300, +0x48 = +0x44 =
 * 0, 001FBD50(self, 0x818, 0, 300), 001C67E0(self, 8, 5, 0); +0x58 =
 * 001B1240 to the player. 2: ent +0x81 clear, the count +0x20 not ending
 * and the facing test (1.4835298) set: the approach (+0x48 = 0; +0x44
 * below 0.8 accel 0.009, else 0.8; +0x4C 0.65 / 0.4 by +0x50; +0x5C =
 * -001B1270(self +0xB0, 6 + py, 3 + z); the point (0, 0, 3.5) turned by
 * the yaw plus the position at 0x700038B0, the box (4, 9) at 0x700038A0;
 * 001B34F0(the player, box, point) set and 0021BED0(0x8102B0) clear: +6
 * += 1, +0x50 = 0, +0x5C = pi/2, +0x48 = +0x44 = 0, 0021BF90(0x8102B0,
 * self), +0x83 = 1, D_00810374 = 001B1470(pi + yaw), the position = the
 * player + (0, 0, 3.7) turned by it (x, z), 001C67E0(self, 9, 1, 0),
 * 001FBD50(self, 0x818, 0, 300)); otherwise +6 = 9, +0x82 = 1, +0x4C =
 * 0.4, +0x5C = pi/2, +0x60 = the yaw, +0x40 = -0.005, +0x58 =
 * 001B1470(pi + yaw), 001C67E0(self, 0xE, 1, 0). 3: D_008106BD == 1 ->
 * +6 = 6, +0x83 = 0, D_008106BD = 0; else y clamped to 8 + py (then +0x5C
 * = +0x50 = 0) and on ent +0x70 bit 0x1000 +6 += 1, +0x20 = 240, +0x22 =
 * 60, y = 8 + py, +0x5C = +0x50 = 0, 001C67E0(self, 0xB, 1, 0). 4: the
 * same cancel; else the position = the player + (0, 8, 3.7) turned by
 * D_00810374; the count +0x20: running -> +0x22 counts, at 0 = 60,
 * 001FBD50(self, 0x819, 0, 300), the floor 15 / 12 (bit 7 of +0xD) or 15
 * / 10 by D_0081070A: health D_008104D0 at or below it -> +6 = 6, +0x83 =
 * 0, D_008104D4 = the health; else D_008104D4 = the floor; D_008102B0 |=
 * 2; 001EFD90(0x80000006, the player + (0, 3, 3) turned, 0x810370); ended
 * -> +6 += 1, 001C67E0(self, 0xC, 1, 0). 5: ent +0x70 bit 0x1000 -> +6 =
 * 6; with +0x20 0 and self +0x3C <= 17: +0x20 = 1, 001FBD50(self, 0x81A,
 * 0, 300), the matrix turned by the yaw, the point (0, -5.0768, 3.7116)
 * through it plus the position into the matrix's translation,
 * 001EFEB0(0x80000029, 0x700036A0), D_008104DC = 60 / 40 (bit 7) or 48 /
 * 40 by D_0081070A, D_008102B0 |= 2, +0x83 = 0, 0021C040(0x8102B0, self),
 * D_008106BD 1 -> 0. 6: +6 = 7, +0x50 = 0, +0x5C = pi/4, +0x44 = -0.4,
 * +0x48 = 0.01, +0x83 = 0, 001C67E0(self, 0xD, 1, 0). 7: ent +0x70 bit
 * 0x1000 -> +0x44 = 0.4, +0x48 = 0, reset, +0x5C = 0; otherwise +0x44
 * capped at 0.4 (+0x48 = 0) and, below 0, the push (0, 0, -3) through
 * 001B2B10 plus the position, 0019AD00(self, it, 0x80000006). 9: bit
 * 0x1000 -> reset; yaw rate 2.5; the cancel when +0x5C != 0; +0x48 = 0 /
 * +0x44 = 0 at or below 0, else +0x48 = +0x40; +0x44 += +0x48; y += +0x4C
 * 0011E2A8(+0x50), x += +0x44 0011E2A8(+0x60), z += +0x44 0011DE90(+0x60);
 * the probe point (0, 0, 3) turned by +0x60 (00102C58 of (0, +0x60, 0))
 * plus the position, 0019AD00(self, it, 0x80000006). Reset: +5 = 1, +6 =
 * 0, +0x20 = 60, +0x22 / +0x2C / +0x30 = 0, +0x82 = +0x84 = 0, +0x5C = 0,
 * +0x4C = 0.4, +0x48 = 0, +0x60 = the yaw, +0x2E = 120 + ((rand >> 15) &
 * 0x7F), +0x44 clamped to 0..0.4. Last the eases: +0x50 toward +0x5C (pi
 * turn / 180), self +0xC8 toward 0 (0.0349066), the yaw toward +0x58 (pi
 * yaw / 180).
 * ---------------------------------------------------------------------- */
static int l13_13A3B0_state(L13 *o, uint32_t self, uint32_t ent, uint32_t st, uint32_t *yaw_rate,
                            uint32_t *turn_rate, int *reset)
{
    uint32_t out = 0;
    int32_t r = 0;
    switch (st) {
    case 0:
        l13_w8(o, self + 6, l13_u8(o, self + 6) + 1u);
        l13_w32(o, ent + 0x50, 0);
        l13_w32(o, ent + 0x4C, F_0_4);
        l13_w8(o, ent + 0x84, 1);
        l13_w32(o, ent + 0x5C, F_HALF_PI);
        if (l13_c_001C67E0(o, self, 6, 0x40A00000u, 0)) return -1;
        /* fall through */
    case 1: {
        if (L13_LE(l13_u32(o, ent + 0x44), 0)) {
            l13_w32(o, ent + 0x48, 0);
            l13_w32(o, ent + 0x44, 0);
        } else {
            l13_w32(o, ent + 0x48, 0xBBA3D70Au);
        }
        if (L13_EQ(0, l13_u32(o, ent + 0x5C)))
            *turn_rate = 0x40200000u;
        else
            l13_13A3B0_cancel(o, self, ent);
        if (L13_EQ(l13_u32(o, ent + 0x50), 0) && L13_EQ(l13_u32(o, ent + 0x5C), 0) &&
            L13_EQ(l13_u32(o, ent + 0x44), 0) && ((uint32_t)l13_s16(o, self + 0x2C) & 0xFFFF7FFFu) == 7u) {
            l13_w8(o, self + 6, l13_u8(o, self + 6) + 1u);
            l13_w16(o, ent + 0x20, 300);
            l13_w32(o, ent + 0x48, 0);
            l13_w32(o, ent + 0x44, 0);
            if (l13_c_001FBD50(o, self, 0x818, 0, F_300)) return -1;
            if (l13_c_001C67E0(o, self, 8, 0x40A00000u, 0)) return -1;
        }
        uint32_t px = l13_u32(o, P_POS);
        uint32_t pz = l13_u32(o, P_POS + 8);
        if (l13_c_001B1240(o, self + 0xB0, px, pz, &out)) return -1;
        l13_w32(o, ent + 0x58, out);
        return 0;
    }
    case 2: {
        int track = 0;
        if (l13_s8(o, ent + 0x81) == 0) {
            uint32_t c = (l13_u16(o, ent + 0x20) - 1u) & 0xFFFFu;
            l13_w16(o, ent + 0x20, c);
            if (c != 0) {
                if (l13_c_001B1560(o, self, P_POS, 0x3FBDE44Eu, &r)) return -1;
                track = r != 0;
            }
        }
        if (!track) {
            l13_w8(o, self + 6, 9);
            l13_w8(o, ent + 0x82, 1);
            l13_w32(o, ent + 0x4C, F_0_4);
            l13_w32(o, ent + 0x5C, F_HALF_PI);
            l13_w32(o, ent + 0x60, l13_u32(o, self + 0xC4));
            l13_w32(o, ent + 0x40, 0xBBA3D70Au);
            if (l13_c_001B1470(o, L13_ADD(F_PI, l13_u32(o, self + 0xC4)), &out)) return -1;
            l13_w32(o, ent + 0x58, out);
            return l13_c_001C67E0(o, self, 0xE, F_ONE, 0);
        }
        l13_w32(o, ent + 0x48, 0);
        if (L13_LT(l13_u32(o, ent + 0x44), 0x3F4CCCCDu))
            l13_w32(o, ent + 0x48, 0x3C1374BCu);
        else
            l13_w32(o, ent + 0x44, 0x3F4CCCCDu);
        l13_w32(o, ent + 0x4C, L13_LT(l13_u32(o, ent + 0x50), 0) ? 0x3F266666u : F_0_4);
        uint32_t py = l13_u32(o, 0x00810364u);
        uint32_t z = l13_u32(o, self + 0xB8);
        if (l13_c_001B1270(o, self + 0xB0, L13_ADD(0x40C00000u, py), L13_ADD(0x40400000u, z), &out)) return -1;
        l13_w32(o, ent + 0x5C, L13_MUL(0xBF800000u, out));
        if (l13_turned_point(o, self + 0xC4, 0x700038B0u, 0, 0x40600000u, 0x700038B0u, self + 0xB0)) return -1;
        l13_w32(o, 0x700038A0u, 0x40800000u);
        l13_w32(o, 0x700038A4u, 0x41100000u);
        l13_w32(o, 0x700038A8u, 0);
        l13_w32(o, 0x700038ACu, F_ONE);
        if (l13_001B34F0(o, P_POS, 0x700038A0u, 0x700038B0u) == 0) return 0;
        if (l13_failed(o)) return -1;
        if (l13_c_0021BED0(o, P_BLOCK, &r)) return -1;
        if (r != 0) return 0;
        l13_w8(o, self + 6, l13_u8(o, self + 6) + 1u);
        l13_w32(o, ent + 0x50, 0);
        l13_w32(o, ent + 0x5C, F_HALF_PI);
        l13_w32(o, ent + 0x48, 0);
        l13_w32(o, ent + 0x44, 0);
        if (l13_c_0021BF90(o, P_BLOCK, self)) return -1;
        l13_w8(o, ent + 0x83, 1);
        if (l13_c_001B1470(o, L13_ADD(F_PI, l13_u32(o, self + 0xC4)), &out)) return -1;
        l13_w32(o, 0x00810374u, out);
        if (l13_turned_point(o, 0x00810374u, 0x700038A0u, 0, 0x406CCCCDu, P_POS, 0x700038A0u)) return -1;
        l13_w32(o, self + 0xB0, l13_u32(o, 0x700038A0u));
        l13_w32(o, self + 0xB8, l13_u32(o, 0x700038A8u));
        if (l13_c_001C67E0(o, self, 9, F_ONE, 0)) return -1;
        return l13_c_001FBD50(o, self, 0x818, 0, F_300);
    }
    case 3:
        if (l13_u8(o, 0x008106BDu) == 1u) {
            l13_w8(o, self + 6, 6);
            l13_w8(o, ent + 0x83, 0);
            l13_w8(o, 0x008106BDu, 0);
            return 0;
        } else {
            uint32_t py = l13_u32(o, 0x00810364u);
            uint32_t y = l13_u32(o, self + 0xB4);
            uint32_t t0 = L13_ADD(0x41000000u, py);
            if (L13_LE(t0, y)) {
                l13_w32(o, self + 0xB4, t0);
                l13_w32(o, ent + 0x5C, 0);
                l13_w32(o, ent + 0x50, 0);
            }
            if (l13_u32(o, ent + 0x70) & 0x1000u) {
                l13_w8(o, self + 6, l13_u8(o, self + 6) + 1u);
                l13_w16(o, ent + 0x20, 0xF0);
                l13_w16(o, ent + 0x22, 0x3C);
                l13_w32(o, self + 0xB4, L13_ADD(0x41000000u, l13_u32(o, 0x00810364u)));
                l13_w32(o, ent + 0x5C, 0);
                l13_w32(o, ent + 0x50, 0);
                return l13_c_001C67E0(o, self, 0xB, F_ONE, 0);
            }
            return 0;
        }
    case 4: {
        if (l13_u8(o, 0x008106BDu) == 1u) {
            l13_w8(o, self + 6, 6);
            l13_w8(o, ent + 0x83, 0);
            l13_w8(o, 0x008106BDu, 0);
            return 0;
        }
        if (l13_turned_point(o, 0x00810374u, 0x700038A0u, 0x41000000u, 0x406CCCCDu, P_POS, 0x700038A0u)) return -1;
        l13_w32(o, self + 0xB0, l13_u32(o, 0x700038A0u));
        l13_w32(o, self + 0xB4, l13_u32(o, 0x700038A4u));
        l13_w32(o, self + 0xB8, l13_u32(o, 0x700038A8u));
        uint32_t c = l13_u16(o, ent + 0x20);
        if (c == 0) {
            l13_w8(o, self + 6, l13_u8(o, self + 6) + 1u);
            return l13_c_001C67E0(o, self, 0xC, F_ONE, 0);
        }
        l13_w16(o, ent + 0x20, c - 1u);
        uint32_t c2 = l13_u16(o, ent + 0x22);
        if (c2 != 0) {
            l13_w16(o, ent + 0x22, c2 - 1u);
            return 0;
        }
        l13_w16(o, ent + 0x22, 0x3C);
        if (l13_c_001FBD50(o, self, 0x819, 0, F_300)) return -1;
        uint32_t lim;
        if (l13_u8(o, self + 0xD) & 0x80u)
            lim = l13_u8(o, 0x0081070Au) != 0 ? 0x41700000u : 0x41400000u;
        else
            lim = l13_u8(o, 0x0081070Au) != 0 ? 0x41700000u : 0x41200000u;
        if (L13_LE(l13_u32(o, 0x008104D0u), lim)) {
            l13_w8(o, self + 6, 6);
            l13_w8(o, ent + 0x83, 0);
            l13_w32(o, 0x008104D4u, l13_u32(o, 0x008104D0u));
        } else {
            l13_w32(o, 0x008104D4u, lim);
        }
        l13_w8(o, P_BLOCK, l13_u8(o, P_BLOCK) | 2u);
        if (l13_turned_point(o, 0x00810374u, 0x700038A0u, 0x40400000u, 0x40400000u, P_POS, 0x700038A0u)) return -1;
        return l13_c_001EFD90(o, (int32_t)0x80000006u, 0x700038A0u, 0x00810370u);
    }
    case 5: {
        if (l13_u32(o, ent + 0x70) & 0x1000u) l13_w8(o, self + 6, st + 1u);
        if (l13_u16(o, ent + 0x20) != 0) return 0;
        if (!L13_LE(l13_u32(o, self + 0x3C), 0x41880000u)) return 0;
        l13_w16(o, ent + 0x20, 1);
        if (l13_c_001FBD50(o, self, 0x81A, 0, F_300)) return -1;
        if (l13_turned_point(o, self + 0xC4, 0x700038A0u, 0xC0A27525u, 0x406D8ADBu, self + 0xB0, 0x700038A0u)) return -1;
        uint32_t hx = l13_u32(o, 0x700038A0u);
        uint32_t hy = l13_u32(o, 0x700038A4u);
        uint32_t hz = l13_u32(o, 0x700038A8u);
        l13_w32(o, 0x700036D0u, hx);
        l13_w32(o, 0x700036D4u, hy);
        l13_w32(o, 0x700036D8u, hz);
        if (l13_c_001EFEB0(o, (int32_t)0x80000029u, 0x700036A0u)) return -1;
        uint32_t lim;
        if (l13_u8(o, self + 0xD) & 0x80u)
            lim = l13_u8(o, 0x0081070Au) != 0 ? 0x42700000u : 0x42200000u;
        else
            lim = l13_u8(o, 0x0081070Au) != 0 ? 0x42400000u : 0x42200000u;
        l13_w32(o, 0x008104DCu, lim);
        l13_w8(o, P_BLOCK, l13_u8(o, P_BLOCK) | 2u);
        l13_w8(o, ent + 0x83, 0);
        if (l13_c_0021C040(o, P_BLOCK, self)) return -1;
        if (l13_u8(o, 0x008106BDu) == 1u) l13_w8(o, 0x008106BDu, 0);
        return 0;
    }
    case 6:
        l13_w8(o, self + 6, st + 1u);
        l13_w32(o, ent + 0x50, 0);
        l13_w32(o, ent + 0x5C, 0x3F490FDBu);
        l13_w32(o, ent + 0x44, 0xBECCCCCDu);
        l13_w32(o, ent + 0x48, 0x3C23D70Au);
        l13_w8(o, ent + 0x83, 0);
        return l13_c_001C67E0(o, self, 0xD, *turn_rate, 0);
    case 7:
        if (l13_u32(o, ent + 0x70) & 0x1000u) {
            l13_w32(o, ent + 0x44, F_0_4);
            l13_w32(o, ent + 0x48, 0);
            *reset = 1;
            l13_w32(o, ent + 0x5C, 0);
            return 0;
        }
        if (!L13_LE(l13_u32(o, ent + 0x44), F_0_4)) {
            l13_w32(o, ent + 0x44, F_0_4);
            l13_w32(o, ent + 0x48, 0);
        }
        if (L13_LT(l13_u32(o, ent + 0x44), 0)) {
            l13_w32(o, 0x700038A0u, 0);
            l13_w32(o, 0x700038A4u, 0);
            l13_w32(o, 0x700038A8u, 0xC0400000u);
            l13_w32(o, 0x700038ACu, F_ONE);
            if (l13_c_001B2B10(o, self, 0x700038A0u, 0x700038A0u)) return -1;
            if (l13_c_001028B8(o, 0x700038A0u, 0x700038A0u, self + 0xB0)) return -1;
            return l13_c_0019AD00(o, self, 0x700038A0u, (int32_t)0x80000006u, &r);
        }
        return 0;
    case 9: {
        if (l13_u32(o, ent + 0x70) & 0x1000u) *reset = 1;
        *yaw_rate = 0x40200000u;
        if (!L13_EQ(0, l13_u32(o, ent + 0x5C))) l13_13A3B0_cancel(o, self, ent);
        if (L13_LE(l13_u32(o, ent + 0x44), 0)) {
            l13_w32(o, ent + 0x48, 0);
            l13_w32(o, ent + 0x44, 0);
        } else {
            l13_w32(o, ent + 0x48, l13_u32(o, ent + 0x40));
        }
        uint32_t acc = l13_u32(o, ent + 0x48);
        l13_w32(o, ent + 0x44, L13_ADD(l13_u32(o, ent + 0x44), acc));
        uint32_t s = 0;
        if (l13_c_0011E2A8(o, l13_u32(o, ent + 0x50), &s)) return -1;
        uint32_t k = l13_u32(o, ent + 0x4C);
        l13_w32(o, self + 0xB4, L13_ADD(l13_u32(o, self + 0xB4), L13_MUL(k, s)));
        if (l13_c_0011E2A8(o, l13_u32(o, ent + 0x60), &s)) return -1;
        k = l13_u32(o, ent + 0x44);
        l13_w32(o, self + 0xB0, L13_ADD(l13_u32(o, self + 0xB0), L13_MUL(k, s)));
        if (l13_c_0011DE90(o, l13_u32(o, ent + 0x60), &s)) return -1;
        k = l13_u32(o, ent + 0x44);
        l13_w32(o, self + 0xB8, L13_ADD(l13_u32(o, self + 0xB8), L13_MUL(k, s)));
        l13_w32(o, 0x700038A0u, 0);
        l13_w32(o, 0x700038A4u, l13_u32(o, ent + 0x60));
        l13_w32(o, 0x700038A8u, 0);
        l13_w32(o, 0x700038ACu, F_ONE);
        if (l13_c_001029C0(o, 0x700036A0u)) return -1;
        if (l13_c_00102C58(o, 0x700036A0u, 0x700036A0u, 0x700038A0u)) return -1;
        l13_w32(o, 0x700038A0u, 0);
        l13_w32(o, 0x700038A4u, 0);
        l13_w32(o, 0x700038A8u, 0x40400000u);
        l13_w32(o, 0x700038ACu, F_ONE);
        if (l13_c_001026A0(o, 0x700038A0u, 0x700036A0u, 0x700038A0u)) return -1;
        if (l13_c_001028B8(o, 0x700038A0u, 0x700038A0u, self + 0xB0)) return -1;
        return l13_c_0019AD00(o, self, 0x700038A0u, (int32_t)0x80000006u, &r);
    }
    default:
        return 0;
    }
}

static void l13_0013A3B0(L13 *o, uint32_t self, uint32_t ent)
{
    uint32_t yaw_rate = 0x3FC00000u, turn_rate = F_ONE;
    int reset = 0, bad = 0;
    uint32_t st = l13_u8(o, self + 6);
    if (l13_13A3B0_state(o, self, ent, st, &yaw_rate, &turn_rate, &reset)) return;
    if (reset) {
        l13_w8(o, self + 5, 1);
        l13_w8(o, self + 6, 0);
        l13_w16(o, ent + 0x20, 0x3C);
        l13_w16(o, ent + 0x22, 0);
        l13_w16(o, ent + 0x2C, 0);
        l13_w16(o, ent + 0x30, 0);
        l13_w8(o, ent + 0x82, 0);
        l13_w8(o, ent + 0x84, 0);
        l13_w32(o, ent + 0x5C, 0);
        l13_w32(o, ent + 0x4C, F_0_4);
        l13_w32(o, ent + 0x48, 0);
        l13_w32(o, ent + 0x60, l13_u32(o, self + 0xC4));
        uint32_t r = l13_rand(o, &bad);
        if (bad) return;
        l13_w16(o, ent + 0x2E, ((uint32_t)l13_sra(r, 15) & 0x7Fu) + 0x78u);
        uint32_t v = l13_u32(o, ent + 0x44);
        if (L13_LT(v, 0))
            l13_w32(o, ent + 0x44, 0);
        else if (!L13_LE(v, F_0_4))
            l13_w32(o, ent + 0x44, F_0_4);
    }
    uint32_t out = 0;
    uint32_t tgt = l13_u32(o, ent + 0x5C);
    uint32_t cur = l13_u32(o, ent + 0x50);
    if (l13_c_001B12B0(o, tgt, cur, L13_DIV(L13_MUL(F_PI, turn_rate), F_180), &out)) return;
    l13_w32(o, ent + 0x50, out);
    if (l13_c_001B12B0(o, 0, l13_u32(o, self + 0xC8), 0x3D0EFA35u, &out)) return;
    l13_w32(o, self + 0xC8, out);
    tgt = l13_u32(o, ent + 0x58);
    cur = l13_u32(o, self + 0xC4);
    if (l13_c_001B12B0(o, tgt, cur, L13_DIV(L13_MUL(F_PI, yaw_rate), F_180), &out)) return;
    l13_w32(o, self + 0xC4, out);
}

int em_level13_port_0013A3B0(const H13 *h, uint32_t self, uint32_t ent, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    l13_0013A3B0(&o, self, ent);
    return l13_end(&o);
}

/* ------------------------------------------------------------------------
 * 001545B0 (self, x, z) -> 1 when (x, z) lies inside the ellipse of the
 * actor's size record D_00248120 + 20 * +0xD (semi-axes 0.92 * rec +8 and
 * 0.92 * rec +0) in the bearing 001B1240(self +0xB0, x, z): with a = (0.92
 * rec8)^2, b = (0.92 rec0)^2, c = cos^2 (0011E2A8), s = sin^2 (0011DE90),
 * r^2 = a b / (a c + b s) (MULA / MADD) and d^2 = dx^2 + dz^2 (MULA /
 * MADD): d^2 <= r^2.
 * ---------------------------------------------------------------------- */
static int32_t l13_001545B0(L13 *o, uint32_t self, uint32_t x, uint32_t z)
{
    uint32_t rec = 0x00248120u + l13_u8(o, self + 0xD) * 20u;
    uint32_t ang = 0;
    if (l13_c_001B1240(o, self + 0xB0, x, z, &ang)) return 0;
    uint32_t r8 = l13_u32(o, rec + 8);
    uint32_t r0 = l13_u32(o, rec);
    uint32_t a = L13_MUL(0x3F6B851Fu, r8);
    uint32_t b = L13_MUL(0x3F6B851Fu, r0);
    a = L13_MUL(a, a);
    b = L13_MUL(b, b);
    uint32_t c = 0, s = 0;
    if (l13_c_0011E2A8(o, ang, &c)) return 0;
    c = L13_MUL(c, c);
    if (l13_c_0011DE90(o, ang, &s)) return 0;
    s = L13_MUL(s, s);
    uint32_t x0 = l13_u32(o, self + 0xB0);
    uint32_t z0 = l13_u32(o, self + 0xB8);
    uint32_t r2 = L13_DIV(L13_MUL(a, b), L13_MADD(L13_MULA(a, c), b, s));
    uint32_t dx = L13_SUB(x, x0);
    uint32_t dz = L13_SUB(z, z0);
    uint32_t d2 = L13_MADD(L13_MULA(dx, dx), dz, dz);
    return L13_LE(d2, r2) ? 1 : 0;
}

int em_level13_port_001545B0(const H13 *h, uint32_t self, float x, float z, int32_t *result, F13 *fault)
{
    L13 o;
    if (!result || l13_begin(&o, h, fault)) return -1;
    int32_t r = l13_001545B0(&o, self, l13_bits(x), l13_bits(z));
    if (l13_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 00154F00 (self, sp): the tendril pieces. The position is saved to the
 * frame (sp - 0x10, 00102948). The fast flag: the target object +0x20 set
 * and its +0x80 above 0.5. For each of the 12 pieces i (halfwords at blk
 * +0x7C + 10 i: pos, vel, +0x80, +0x82, +0x84; floats blk +0x1C / +0x20 +
 * 8 i) with +0x84 set: pos += vel; vel -= 8 (fast) or 1; pos below 100 ->
 * pos = 100, vel = ((5 (rand >> 16)) >> 15) + 3; pos below 0x80 -> vel +=
 * ((14 (rand >> 16)) >> 15) + 0x1C (fast) or ((5 (rand >> 16)) >> 15) + 3;
 * not fast with vel >= 8: vel = 001281C0(vel * 0.5). Then the draw: +0x8C
 * = (+0x80 / 16) blk +0xC below 16, else blk +0xC; +0x60 = +0x68 = +0x82 /
 * 256, +0x64 = pos +0x80 / 65536; x / z = the piece's floats, 00102948
 * (self +0xB0 onto itself), 001C6380(self), the +0x4C method. Last the
 * position restored from the frame and 001C6380(self).
 * ---------------------------------------------------------------------- */
static void l13_00154F00(L13 *o, uint32_t self, uint32_t sp)
{
    uint32_t blk = self + 0x1F0;
    int bad = 0;
    if (l13_c_00102948(o, sp - 0x10u, self + 0xB0)) return;
    int fast = 0;
    uint32_t target = l13_u32(o, self + 0x20);
    if (target != 0) fast = !L13_LE(l13_u32(o, target + 0x80), 0x3F000000u);
    for (uint32_t i = 0; i < 12; i++) {
        uint32_t s3 = blk + 10u * i, s4 = blk + 8u * i;
        if (l13_s16(o, s3 + 0x84) == 0) continue;
        int32_t pos = l13_s16(o, s3 + 0x7C);
        int32_t vel = l13_s16(o, s3 + 0x7E);
        l13_w16(o, s3 + 0x7C, (uint32_t)(pos + vel));
        l13_w16(o, s3 + 0x7E, (uint32_t)(l13_s16(o, s3 + 0x7E) - (fast ? 8 : 1)));
        int32_t v = l13_s16(o, s3 + 0x7C);
        if (v < 0x64) {
            l13_w16(o, s3 + 0x7C, 0x64);
            uint32_t r = l13_rand(o, &bad);
            if (bad) return;
            uint32_t hi = (uint32_t)l13_sra(r, 16);
            l13_w16(o, s3 + 0x7E, (uint32_t)l13_sra(hi * 5u, 15) + 3u);
        } else if (v < 0x80) {
            uint32_t r = l13_rand(o, &bad);
            if (bad) return;
            uint32_t hi = (uint32_t)l13_sra(r, 16);
            int32_t cur = l13_s16(o, s3 + 0x7E);
            uint32_t add = fast ? (uint32_t)l13_sra(hi * 14u, 15) + 0x1Cu : (uint32_t)l13_sra(hi * 5u, 15) + 3u;
            l13_w16(o, s3 + 0x7E, (uint32_t)cur + add);
        }
        if (!fast) {
            int32_t vv = l13_s16(o, s3 + 0x7E);
            if (vv >= 8) {
                int32_t iv = 0;
                if (l13_c_001281C0(o, L13_MUL(L13_CVT_S_W((uint32_t)vv), 0x3F000000u), &iv)) return;
                l13_w16(o, s3 + 0x7E, (uint32_t)iv);
            }
        }
        int32_t c80 = l13_s16(o, s3 + 0x80);
        int32_t c7c = l13_s16(o, s3 + 0x7C);
        uint32_t f3 = L13_MUL(L13_CVT_S_W((uint32_t)c7c * (uint32_t)c80), 0x37800000u);
        if (c80 < 0x10) {
            uint32_t q = L13_DIV(L13_CVT_S_W((uint32_t)c80), 0x41800000u);
            l13_w32(o, self + 0x8C, L13_MUL(q, l13_u32(o, blk + 0xC)));
        } else {
            l13_w32(o, self + 0x8C, l13_u32(o, blk + 0xC));
        }
        uint32_t f0 = L13_MUL(0x3B800000u, L13_CVT_S_W((uint32_t)l13_s16(o, s3 + 0x82)));
        l13_w32(o, self + 0x68, f0);
        l13_w32(o, self + 0x60, f0);
        l13_w32(o, self + 0x64, f3);
        l13_w32(o, self + 0xB0, l13_u32(o, s4 + 0x1C));
        l13_w32(o, self + 0xB8, l13_u32(o, s4 + 0x20));
        if (l13_c_00102948(o, self + 0xB0, self + 0xB0)) return;
        if (l13_c_001C6380(o, self)) return;
        if (l13_method(o, self)) return;
    }
    if (l13_c_00102948(o, self + 0xB0, sp - 0x10u)) return;
    l13_c_001C6380(o, self);
}

int em_level13_port_00154F00(const H13 *h, uint32_t self, uint32_t sp, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    l13_00154F00(&o, self, sp);
    return l13_end(&o);
}
