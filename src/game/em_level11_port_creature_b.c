/* The 0x141D20 actor family, second part (boot code): the behaviours 3, 4, 6,
 * 7 and 8 of the live frame (00142070), the dying frame 001450B0 with its
 * three stages and the last frame 00145850, which AREA13's two beasts need to
 * run live. docs/LEVEL11_PORT.md: section 0 (the rows), section 3 (the
 * oracle) and "Binding" (how the hooks of em_level11_port_creature.c reach
 * these entries).
 *
 * e = the actor (pool node), d = e + 0x1F0 (its behaviour block). Every
 * function follows the original instructions; the decomp's C (byte-matched
 * for all but 00144040 and 00144C20, NEARMISS) is a guide, and the field
 * names its comments give are labels. Floats are bit patterns (em_ee_float.h);
 * every read the original orders is its own statement or the only read of
 * its expression, so the access order is the source order. */
#include "em_level11_port_internal.h"

#define S_700038A0 0x700038A0u
#define S_700038A4 0x700038A4u
#define S_700038A8 0x700038A8u
#define S_700038AC 0x700038ACu
#define S_700038B0 0x700038B0u
#define S_700038B4 0x700038B4u
#define S_700038B8 0x700038B8u
#define S_700038BC 0x700038BCu
#define S_70003A20 0x70003A20u

#define PLAYER_BLOCK 0x008102B0u /* its first byte too: bit 1 is set by every grab */
#define PLAYER_XZ    0x00810360u
#define PLAYER_Z     0x00810368u
#define PLAYER_YAW   0x00810374u
#define GRAB_POINT   0x00810320u
#define GRAB_TIME    0x008104D4u
#define HARD_MODE    0x0081070Au
#define CARRY_POINT  0x00245950u

#define F_PI  0x40490FDBu
#define F_QPI 0x3F490FDBu /* pi / 4 */
#define F_300 0x43960000u
#define F_2   0x40000000u
#define F_5   0x40A00000u
#define F_20  0x41A00000u

#define L11_D_END 0x1000u /* d +0x30: the clip reached its end */

static uint32_t l11_rand(L11 *o, int *bad)
{
    int32_t r = 0;
    if (l11_c_00122BB8(o, &r)) *bad = 1;
    return (uint32_t)r;
}

/* The bone record the frame loop points D_00275B40 at: word +4 of it. */
static uint32_t l11_bone(L11 *o)
{
    return l11_u32(o, l11_u32(o, 0x00275B40u) + 4);
}

/* d +0x7B set: cleared, 0021C040(the player, e), and D_008106BD 1 -> 0. */
static void l11_drop_held(L11 *o, uint32_t e, uint32_t d)
{
    if ((int8_t)l11_u8(o, d + 0x7B) == 0) return;
    l11_w8(o, d + 0x7B, 0);
    if (l11_c_0021C040(o, PLAYER_BLOCK, e)) return;
    if (l11_u8(o, 0x008106BDu) == 1) l11_w8(o, 0x008106BDu, 0);
}

/* The next stage from an ended clip: e +4 = 1, e +5 = 2, e +6 = 0, e +0 = 1. */
static void l11_back_alive(L11 *o, uint32_t e)
{
    l11_w8(o, e + 4, 1);
    l11_w8(o, e + 5, 2);
    l11_w8(o, e + 6, 0);
    l11_w8(o, e, 1);
}

/* d +0x62 = ((rand >> 15) & 0xFF) + 300. */
static void l11_rearm_chase(L11 *o, uint32_t d)
{
    int bad = 0;
    uint32_t rnd = l11_rand(o, &bad);
    if (bad) return;
    l11_w16(o, d + 0x62, (uint32_t)((l11_sra(rnd, 15) & 0xFF) + 0x12C));
}

/* (0, y, z, 1) through e's frame (001B2B10) at 0x700038A0, then moved to e
 * +0xB0 (001028B8). */
static void l11_ahead(L11 *o, uint32_t e, uint32_t y, uint32_t z)
{
    l11_w32(o, S_700038A0, 0);
    l11_w32(o, S_700038A4, y);
    l11_w32(o, S_700038A8, z);
    l11_w32(o, S_700038AC, F_ONE);
    l11_c_001B2B10(o, e, S_700038A0, S_700038A0);
    l11_c_001028B8(o, S_700038A0, e + 0xB0, S_700038A0);
}

/* The held player's place: the point 0x245950 through e's frame plus e
 * +0xB0, given to 00182F90(the player, it). */
static void l11_carry(L11 *o, uint32_t e)
{
    l11_c_00102948(o, S_700038A0, CARRY_POINT);
    l11_c_001B2B10(o, e, S_700038A0, S_700038A0);
    l11_c_001028B8(o, S_700038A0, e + 0xB0, S_700038A0);
    l11_c_00182F90(o, PLAYER_BLOCK, S_700038A0);
}

/* e +0xC4 turned toward the player (d +0x44 = 001B1240) by `rate`. */
static void l11_face_player(L11 *o, uint32_t e, uint32_t d, uint32_t rate)
{
    uint32_t f = 0;
    uint32_t px = l11_u32(o, PLAYER_XZ);
    if (l11_c_001B1240(o, e + 0xB0, px, l11_u32(o, PLAYER_Z), &f)) return;
    l11_w32(o, d + 0x44, f);
    if (l11_c_001B12B0(o, f, l11_u32(o, e + 0xC4), rate, &f)) return;
    l11_w32(o, e + 0xC4, f);
}

/* ------------------------------------------------------------------------
 * 001434C0 (e, d): behaviour 3. +6 0: +6 = 1, d +0x20 = +0x75 = +0x4C =
 * +0x50 = 0, clip 7 (1, 0). +6 1: with d +0x20 clear and e +0x3C <= 25: +6 =
 * 2, d +0x20 = 1, d +0x4C = D_002753E0, d +0x48 = D_002753E4, 001FBD50(e,
 * 0x831, 0, 300). +6 2: clip 8 (1, 0) at the clip's end; d +0x48 <= 0: e +5
 * = 4, +6 = 0, clip 9 (1, 0). */
static void l11_001434C0(L11 *o, uint32_t e, uint32_t d)
{
    int32_t r = 0;
    uint32_t st = l11_u8(o, e + 6);
    switch (st) {
    case 0:
        l11_w8(o, e + 6, st + 1);
        l11_w32(o, d + 0x20, 0);
        l11_w8(o, d + 0x75, 0);
        l11_w32(o, d + 0x4C, 0);
        l11_w32(o, d + 0x50, 0);
        l11_c_001C67E0(o, e, 7, F_ONE, F_ZERO);
        break;
    case 1:
        if (l11_s32(o, d + 0x20) != 0) break;
        if (!L11_LE(l11_u32(o, e + 0x3C), 0x41C80000u)) break; /* 25.0 */
        l11_w8(o, e + 6, st + 1);
        l11_w32(o, d + 0x20, 1);
        l11_w32(o, d + 0x4C, l11_u32(o, 0x002753E0u));
        l11_w32(o, d + 0x48, l11_u32(o, 0x002753E4u));
        l11_c_001FBD50(o, e, 0x831, 0, F_300, &r);
        break;
    case 2:
        if (l11_u32(o, d + 0x30) & L11_D_END) l11_c_001C67E0(o, e, 8, F_ONE, F_ZERO);
        if (L11_LE(l11_u32(o, d + 0x48), F_ZERO)) {
            l11_w8(o, e + 5, 4);
            l11_w8(o, e + 6, 0);
            l11_c_001C67E0(o, e, 9, F_ONE, F_ZERO);
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------
 * 00143610 (e, d): behaviour 4. +6 0: +6 = 1, d +0x75 = 0, +0x40 = 1, +0x50
 * = 0, the bone record's +0x74 = 0, clip 9 (1, 6) unless clip 9 already runs
 * (e +0x2C without bit 15), then the held player's drop. +6 1: e +0x3C <= 26,
 * or d +0x48 <= 0 with d +0x74 bit 1: +6 += 1, d +0x40 = 0. +6 2: d +0x48 <=
 * 0 with d +0x74 bit 1: +6 += 1, d +0x40 = 1, +0x48 = +0x4C = 0, 001FBD50(e,
 * 0x832, 0, 300). +6 3: e +5 = +6 = 0 at the clip's end. */
static void l11_00143610(L11 *o, uint32_t e, uint32_t d)
{
    int32_t r = 0;
    uint32_t st = l11_u8(o, e + 6);
    switch (st) {
    case 0:
        l11_w8(o, e + 6, st + 1);
        l11_w8(o, d + 0x75, 0);
        l11_w32(o, d + 0x40, F_ONE);
        l11_w32(o, d + 0x50, 0);
        l11_w32(o, l11_bone(o) + 0x74, 0);
        if ((l11_s16(o, e + 0x2C) & (int32_t)0xFFFF7FFF) != 9) l11_c_001C67E0(o, e, 9, F_ONE, 0x40C00000u); /* 6.0 */
        l11_drop_held(o, e, d);
        break;
    case 1:
        if (!L11_LE(l11_u32(o, e + 0x3C), 0x41D00000u)) { /* 26.0 */
            if (!L11_LE(l11_u32(o, d + 0x48), F_ZERO)) break;
            if (!((int8_t)l11_u8(o, d + 0x74) & 2)) break;
        }
        l11_w8(o, e + 6, l11_u8(o, e + 6) + 1);
        l11_w32(o, d + 0x40, 0);
        break;
    case 2:
        if (!L11_LE(l11_u32(o, d + 0x48), F_ZERO)) break;
        if (!((int8_t)l11_u8(o, d + 0x74) & 2)) break;
        l11_w8(o, e + 6, st + 1);
        l11_w32(o, d + 0x40, F_ONE);
        l11_w32(o, d + 0x48, 0);
        l11_w32(o, d + 0x4C, 0);
        l11_c_001FBD50(o, e, 0x832, 0, F_300, &r);
        break;
    case 3:
        if (l11_u32(o, d + 0x30) & L11_D_END) {
            l11_w8(o, e + 5, 0);
            l11_w8(o, e + 6, 0);
        }
        break;
    default:
        break;
    }
}

/* The grab of behaviour 6 (states 2 and 3): within 8 (e +0x3C) with d +0x20
 * clear, 0021BE40(the player, e) clear and 001A7B80(e) set: d +0x20 = 1, the
 * player's first byte gets bit 1, D_008104D4 = 25 / 20 by D_0081070A with e
 * +0xD bit 7, else 20 / 15, D_00810320 = the player's offset from e (x, 0,
 * z), normalized (00102760), 001B55E0(e, 0). */
static void l11_grab(L11 *o, uint32_t e, uint32_t d)
{
    int32_t r = 0;
    if (!L11_LE(l11_u32(o, e + 0x3C), 0x41000000u)) return; /* 8.0 */
    if (l11_s32(o, d + 0x20) != 0) return;
    if (l11_c_0021BE40(o, PLAYER_BLOCK, e, &r)) return;
    if (r != 0) return;
    if (l11_c_001A7B80(o, e, &r)) return;
    if (r == 0) return;
    l11_w32(o, d + 0x20, 1);
    l11_w8(o, PLAYER_BLOCK, l11_u8(o, PLAYER_BLOCK) | 2u);
    uint32_t heavy = l11_u8(o, e + 0xD) & 0x80u;
    uint32_t hard = l11_u8(o, HARD_MODE);
    if (heavy)
        l11_w32(o, GRAB_TIME, hard ? 0x41C80000u : F_20); /* 25.0 */
    else
        l11_w32(o, GRAB_TIME, hard ? F_20 : 0x41700000u); /* 15.0 */
    uint32_t a = l11_u32(o, PLAYER_XZ);
    l11_w32(o, GRAB_POINT, L11_SUB(a, l11_u32(o, e + 0xB0)));
    l11_w32(o, GRAB_POINT + 4, 0);
    a = l11_u32(o, PLAYER_Z);
    l11_w32(o, GRAB_POINT + 8, L11_SUB(a, l11_u32(o, e + 0xB8)));
    l11_c_00102760(o, GRAB_POINT, GRAB_POINT);
    l11_c_001B55E0(o, e, 0);
}

/* ------------------------------------------------------------------------
 * 00143AF0 (e, d): behaviour 6 (the bite). e +0x5E = 0 first. +6 0: +6 = 1,
 * d +0x20 = +0x24 = +0x75 = 0, +0x40 = 2.3, +0x4C = 0, clip 0xA (5, 0). +6 1:
 * d +0x24 clear and e +0x3C <= 35: d +0x24 = 1, 001FBD50(e, 0x835, 0, 300);
 * at the clip's end +6 += 1, d +0x24 = 0, +0x40 = 1, clip 0xB (0, 0). +6 2:
 * d +0x24 clear and e +0x3C <= 8: d +0x24 = 1, 001FBD50(e, 0x836, 0, 300); d
 * +0x4C = 0 within 18 (d +0x5C), else 0.1; the grab; at the clip's end +6 +=
 * 1, d +0x20 = +0x24 = +0x4C = 0, clip 0xC (0, 0). +6 3: as +6 2 without the
 * speed; at the clip's end +6 += 1, clip 0xE (0, 0). +6 4: at the clip's end
 * e +5 = +6 = 0, d +0x70 = 0, d +0x71 = (rand >> 19) & 0x1F. Every state then
 * turns e +0xC4 toward the player at 4 degrees a frame. */
static void l11_00143AF0(L11 *o, uint32_t e, uint32_t d)
{
    int bad = 0;
    int32_t r = 0;
    l11_w8(o, e + 0x5E, 0);
    uint32_t st = l11_u8(o, e + 6);
    switch (st) {
    case 0:
        l11_w8(o, e + 6, st + 1);
        l11_w32(o, d + 0x20, 0);
        l11_w32(o, d + 0x24, 0);
        l11_w8(o, d + 0x75, 0);
        l11_w32(o, d + 0x40, 0x40133333u); /* 2.3 */
        l11_w32(o, d + 0x4C, 0);
        l11_c_001C67E0(o, e, 0xA, F_5, F_ZERO);
        break;
    case 1:
        if (l11_s32(o, d + 0x24) == 0 && L11_LE(l11_u32(o, e + 0x3C), 0x420C0000u)) { /* 35.0 */
            l11_w32(o, d + 0x24, 1);
            if (l11_c_001FBD50(o, e, 0x835, 0, F_300, &r)) return;
        }
        if (l11_u32(o, d + 0x30) & L11_D_END) {
            l11_w8(o, e + 6, l11_u8(o, e + 6) + 1);
            l11_w32(o, d + 0x24, 0);
            l11_w32(o, d + 0x40, F_ONE);
            l11_c_001C67E0(o, e, 0xB, F_ZERO, F_ZERO);
        }
        break;
    case 2:
        if (l11_s32(o, d + 0x24) == 0 && L11_LE(l11_u32(o, e + 0x3C), 0x41000000u)) { /* 8.0 */
            l11_w32(o, d + 0x24, 1);
            if (l11_c_001FBD50(o, e, 0x836, 0, F_300, &r)) return;
        }
        if (L11_LE(l11_u32(o, d + 0x5C), 0x41900000u)) /* 18.0 */
            l11_w32(o, d + 0x4C, 0);
        else
            l11_w32(o, d + 0x4C, 0x3DCCCCCDu); /* 0.1 */
        l11_grab(o, e, d);
        if (l11_u32(o, d + 0x30) & L11_D_END) {
            l11_w8(o, e + 6, l11_u8(o, e + 6) + 1);
            l11_w32(o, d + 0x20, 0);
            l11_w32(o, d + 0x24, 0);
            l11_w32(o, d + 0x4C, 0);
            l11_c_001C67E0(o, e, 0xC, F_ZERO, F_ZERO);
        }
        break;
    case 3:
        if (l11_s32(o, d + 0x24) == 0 && L11_LE(l11_u32(o, e + 0x3C), 0x41000000u)) { /* 8.0 */
            l11_w32(o, d + 0x24, 1);
            if (l11_c_001FBD50(o, e, 0x836, 0, F_300, &r)) return;
        }
        l11_grab(o, e, d);
        if (l11_u32(o, d + 0x30) & L11_D_END) {
            l11_w8(o, e + 6, l11_u8(o, e + 6) + 1);
            l11_c_001C67E0(o, e, 0xE, F_ZERO, F_ZERO);
        }
        break;
    case 4:
        if (l11_u32(o, d + 0x30) & L11_D_END) {
            l11_w8(o, e + 5, 0);
            l11_w8(o, e + 6, 0);
            l11_w8(o, d + 0x70, 0);
            uint32_t rnd = l11_rand(o, &bad);
            if (bad) return;
            l11_w8(o, d + 0x71, (uint32_t)(l11_sra(rnd, 19) & 0x1F));
        }
        break;
    default:
        break;
    }
    if (l11_failed(o)) return;
    l11_face_player(o, e, d, 0x3D8EFA35u); /* 0.06981317 */
}

/* The latch of behaviour 8: the point (0, 0, 1, 1) through e's frame, whole,
 * at D_00810320; the player's first byte gets bit 1, its +0xF = 2 and
 * D_008104D4 = 25. */
static void l11_latch_point(L11 *o, uint32_t e)
{
    l11_w32(o, S_700038A0, 0);
    l11_w32(o, S_700038A4, 0);
    uint32_t flags = l11_u8(o, PLAYER_BLOCK);
    l11_w32(o, S_700038A8, F_ONE);
    l11_w32(o, S_700038AC, F_ONE);
    l11_w8(o, PLAYER_BLOCK, flags | 2u);
    l11_w8(o, 0x008102BFu, 2);
    l11_c_001B2B10(o, e, S_700038A0, S_700038A0);
    uint32_t x = l11_u32(o, S_700038A0);
    uint32_t y = l11_u32(o, S_700038A4);
    uint32_t z = l11_u32(o, S_700038A8);
    uint32_t w = l11_u32(o, S_700038AC);
    l11_w32(o, GRAB_POINT, x);
    l11_w32(o, GRAB_POINT + 4, y);
    l11_w32(o, GRAB_POINT + 8, z);
    l11_w32(o, GRAB_POINT + 12, w);
    l11_w32(o, GRAB_TIME, 0x41C80000u); /* 25.0 */
}

/* Back to the chase: e +5 = +6 = 2, d +0x40 = 1, clip 5 (5, 0). */
static void l11_to_chase(L11 *o, uint32_t e, uint32_t d)
{
    l11_w8(o, e + 5, 2);
    l11_w8(o, e + 6, 2);
    l11_w32(o, d + 0x40, F_ONE);
    l11_c_001C67E0(o, e, 5, F_5, F_ZERO);
}

/* ------------------------------------------------------------------------
 * 00144040 (e, d): behaviour 8 (the leap; decomp NEARMISS). +6 0: +6 = 1, d
 * +0x20 = +0x24 = +0x75 = 0, +0x40 = 1, +0x50 = 0, +0x4C = 1.15, clip 5 (0,
 * 0) unless clip 5 runs. +6 1: within 40 (d +0x5C) and 001B1560(e, the
 * player, 10 degrees) set: +6 += 1, d +0x40 = 2, clip 0x10 (5, 20), and with
 * e +0xD bit 7 and 001B3250(e, (0, 3, 50) ahead, 20) clear: +6 = 3, clip 0x18
 * (5, 23). Else (0, 3, 18) ahead: d +0x62 clear, 001B3250 set or d +0x74 bit
 * 0 -> +6 = 0x63; else 001B1560(e, the player, 80 degrees) clear -> e +5 =
 * +6 = 2; then the turn toward the player at 1 degree a frame. +6 2: at e
 * +0x3C <= 84 once (d +0x24) 001FBD50(e, 0x837, 0, 300). Unless clip bit 15
 * is set: within 75 d +0x40 = 1, and within 71 with d +0x20 clear,
 * 0021BE40(the player, e) clear and 001A7B80(e) set: d +0x20 = 1, the latch,
 * 001B55E0(e, 1). Within 47 or at the clip's end: d +0x20 set -> +6 = 0x63,
 * else back to the chase. +6 3: at 17 once 001FBD50(e, 0x837, 0, 300). With
 * d +0x20 clear, clip bit 15 clear and within 8: d +0x40 = 1, and within 4
 * with 0021BED0(the player) clear and 001A7B80(e) set: d +0x20 = 1 and (0, 3,
 * 50) ahead through 001B3250(.., 20): set -> +6 = 2, the latch, clip 0x10 (5,
 * 48); clear -> +6 += 1, d +0x20 = 100, 0021BF90(the player, e), d +0x7B = 1,
 * D_00810374 = 001B1470(pi + e +0xC4), clip 0x19 (0, 0), the carry. Anything
 * else: back to the chase at the clip's end. +6 4: the carry; 00146F80(e)
 * set or d +0x74 bit 0: d +0x20 = 0; d +0x20 counts down, at 0: +6 += 1, d
 * +0x50 = -0.05, clip 0x16 (0, 0). +6 5: with d +0x4C nonzero a lift probe
 * from e +0xB0 (y + 10) to (0, 0, 40) ahead (0019AFE0(e, .., 6)): a hit or a
 * negative d +0x4C zeroes d +0x4C and +0x50. Held (d +0x7B), clip bit 15
 * clear and within 84: d +0x7B = 0, the player's first byte gets bit 1,
 * D_008104D4 = 35 / 30 by D_0081070A, 0021C040(the player, e), 001B55E0(e, 1).
 * Still held: the carry; else within 74: +6 += 1, d +0x48 = 1.5, +0x4C =
 * -0.7, +0x50 = 0, +0x7A = 1. +6 6: d +0x74 bit 1: d +0x4C = 0, and at the
 * clip's end e +5 = +6 = 0, d +0x64 = 0, +0x7A = 0. +6 0x63: e +5 = 2, +6 =
 * 4, d +0x40 = 2, +0x50 = -0.05, clip 6 (0, 0). */
static void l11_00144040(L11 *o, uint32_t e, uint32_t d)
{
    int32_t r = 0;
    uint32_t st = l11_u8(o, e + 6);
    switch (st) {
    case 0:
        l11_w8(o, e + 6, st + 1);
        l11_w32(o, d + 0x20, 0);
        l11_w32(o, d + 0x24, 0);
        l11_w8(o, d + 0x75, 0);
        l11_w32(o, d + 0x40, F_ONE);
        l11_w32(o, d + 0x50, 0);
        l11_w32(o, d + 0x4C, 0x3F933333u); /* 1.15 */
        if ((l11_s16(o, e + 0x2C) & (int32_t)0xFFFF7FFF) != 5) l11_c_001C67E0(o, e, 5, F_ZERO, F_ZERO);
        break;
    case 1:
        if (L11_LE(l11_u32(o, d + 0x5C), 0x42200000u)) { /* 40.0 */
            if (l11_c_001B1560(o, e, PLAYER_XZ, 0x3E32B8C3u, &r)) return; /* 10 degrees */
            if (r != 0) {
                l11_w8(o, e + 6, l11_u8(o, e + 6) + 1);
                l11_w32(o, d + 0x40, F_2);
                if (l11_c_001C67E0(o, e, 0x10, F_5, F_20)) return;
                if (!(l11_u8(o, e + 0xD) & 0x80u)) break;
                l11_ahead(o, e, 0x40400000u, 0x42480000u); /* 3, 50 */
                if (l11_c_001B3250(o, e, S_700038A0, F_20, &r)) return;
                if (r == 0) {
                    l11_w8(o, e + 6, 3);
                    l11_c_001C67E0(o, e, 0x18, F_5, 0x41B80000u); /* 23.0 */
                }
                break;
            }
        }
        l11_ahead(o, e, 0x40400000u, 0x41900000u); /* 3, 18 */
        if (l11_failed(o)) return;
        {
            int leave = l11_s16(o, d + 0x62) == 0;
            if (!leave) {
                if (l11_c_001B3250(o, e, S_700038A0, F_20, &r)) return;
                leave = r != 0 || ((int8_t)l11_u8(o, d + 0x74) & 1);
            }
            if (leave) {
                l11_w8(o, e + 6, 0x63);
            } else {
                if (l11_c_001B1560(o, e, PLAYER_XZ, 0x3FB2B8C3u, &r)) return; /* 80 degrees */
                if (r == 0) {
                    l11_w8(o, e + 5, 2);
                    l11_w8(o, e + 6, 2);
                }
            }
        }
        l11_face_player(o, e, d, 0x3C8EFA35u); /* 0.017453292 */
        break;
    case 2:
        if (l11_s32(o, d + 0x24) == 0 && L11_LE(l11_u32(o, e + 0x3C), 0x42A80000u)) { /* 84.0 */
            l11_w32(o, d + 0x24, 1);
            if (l11_c_001FBD50(o, e, 0x837, 0, F_300, &r)) return;
        }
        if (l11_s16(o, e + 0x2C) & 0x8000) break;
        if (L11_LE(l11_u32(o, e + 0x3C), 0x42960000u)) { /* 75.0 */
            l11_w32(o, d + 0x40, F_ONE);
            if (l11_s32(o, d + 0x20) == 0 && L11_LE(l11_u32(o, e + 0x3C), 0x428E0000u)) { /* 71.0 */
                if (l11_c_0021BE40(o, PLAYER_BLOCK, e, &r)) return;
                if (r == 0) {
                    if (l11_c_001A7B80(o, e, &r)) return;
                    if (r != 0) {
                        l11_w32(o, d + 0x20, 1);
                        l11_latch_point(o, e);
                        if (l11_c_001B55E0(o, e, 1)) return;
                    }
                }
            }
        }
        if (L11_LE(l11_u32(o, e + 0x3C), 0x423C0000u) || (l11_u32(o, d + 0x30) & L11_D_END)) { /* 47.0 */
            if (l11_s32(o, d + 0x20) != 0)
                l11_w8(o, e + 6, 0x63);
            else
                l11_to_chase(o, e, d);
        }
        break;
    case 3:
        if (l11_s32(o, d + 0x24) == 0 && L11_LE(l11_u32(o, e + 0x3C), 0x41880000u)) { /* 17.0 */
            l11_w32(o, d + 0x24, 1);
            if (l11_c_001FBD50(o, e, 0x837, 0, F_300, &r)) return;
        }
        if (l11_s32(o, d + 0x20) == 0 && !(l11_s16(o, e + 0x2C) & 0x8000) &&
            L11_LE(l11_u32(o, e + 0x3C), 0x41000000u)) { /* 8.0 */
            l11_w32(o, d + 0x40, F_ONE);
            if (L11_LE(l11_u32(o, e + 0x3C), 0x40800000u) && l11_s32(o, d + 0x20) == 0) { /* 4.0 */
                if (l11_c_0021BED0(o, PLAYER_BLOCK, &r)) return;
                if (r == 0) {
                    if (l11_c_001A7B80(o, e, &r)) return;
                    if (r != 0) {
                        l11_w32(o, d + 0x20, 1);
                        l11_ahead(o, e, 0x40400000u, 0x42480000u); /* 3, 50 */
                        if (l11_c_001B3250(o, e, S_700038A0, F_20, &r)) return;
                        if (r != 0) {
                            l11_w8(o, e + 6, 2);
                            l11_latch_point(o, e);
                            l11_c_001C67E0(o, e, 0x10, F_5, 0x42400000u); /* 48.0 */
                        } else {
                            uint32_t f = 0;
                            l11_w8(o, e + 6, l11_u8(o, e + 6) + 1);
                            l11_w32(o, d + 0x20, 100);
                            if (l11_c_0021BF90(o, PLAYER_BLOCK, e)) return;
                            l11_w8(o, d + 0x7B, 1);
                            if (l11_c_001B1470(o, L11_ADD(F_PI, l11_u32(o, e + 0xC4)), &f)) return;
                            l11_w32(o, PLAYER_YAW, f);
                            if (l11_c_001C67E0(o, e, 0x19, F_ZERO, F_ZERO)) return;
                            l11_carry(o, e);
                        }
                        break;
                    }
                }
            }
        }
        if (l11_u32(o, d + 0x30) & L11_D_END) l11_to_chase(o, e, d);
        break;
    case 4: {
        l11_carry(o, e);
        if (l11_failed(o)) return;
        if (l11_c_00146F80(o, e, &r)) return;
        if (r != 0 || ((int8_t)l11_u8(o, d + 0x74) & 1)) l11_w32(o, d + 0x20, 0);
        int32_t t = l11_s32(o, d + 0x20);
        if (t != 0) {
            l11_w32(o, d + 0x20, (uint32_t)(t - 1));
        } else {
            l11_w8(o, e + 6, l11_u8(o, e + 6) + 1);
            l11_w32(o, d + 0x50, 0xBD4CCCCDu); /* -0.05 */
            l11_c_001C67E0(o, e, 0x16, F_ZERO, F_ZERO);
        }
        break;
    }
    case 5:
        if (!L11_EQ(l11_u32(o, d + 0x4C), F_ZERO)) {
            l11_c_00102948(o, S_700038A0, e + 0xB0);
            l11_w32(o, S_700038B0, 0);
            l11_w32(o, S_700038B4, 0);
            uint32_t lift = l11_u32(o, S_700038A4);
            l11_w32(o, S_700038B8, 0x42200000u); /* 40.0 */
            l11_w32(o, S_700038BC, F_ONE);
            l11_w32(o, S_700038A4, L11_ADD(lift, 0x41200000u)); /* 10.0 */
            l11_c_001B2B10(o, e, S_700038B0, S_700038B0);
            l11_c_001028B8(o, S_700038B0, S_700038A0, S_700038B0);
            if (l11_c_0019AFE0(o, e, S_700038A0, S_700038B0, 6, &r)) return;
            if (r != 0 || L11_LT(l11_u32(o, d + 0x4C), F_ZERO)) {
                l11_w32(o, d + 0x4C, 0);
                l11_w32(o, d + 0x50, 0);
            }
        }
        if ((int8_t)l11_u8(o, d + 0x7B) != 0 && !(l11_s16(o, e + 0x2C) & 0x8000) &&
            L11_LE(l11_u32(o, e + 0x3C), 0x42A80000u)) { /* 84.0 */
            l11_w8(o, d + 0x7B, 0);
            uint32_t flags = l11_u8(o, PLAYER_BLOCK);
            uint32_t hard = l11_u8(o, HARD_MODE);
            l11_w8(o, PLAYER_BLOCK, flags | 2u);
            l11_w32(o, GRAB_TIME, hard ? 0x420C0000u : 0x41F00000u); /* 35.0, 30.0 */
            if (l11_c_0021C040(o, PLAYER_BLOCK, e)) return;
            if (l11_c_001B55E0(o, e, 1)) return;
        }
        if ((int8_t)l11_u8(o, d + 0x7B) != 0) {
            l11_carry(o, e);
        } else if (L11_LE(l11_u32(o, e + 0x3C), 0x42940000u)) { /* 74.0 */
            l11_w8(o, e + 6, l11_u8(o, e + 6) + 1);
            l11_w32(o, d + 0x48, 0x3FC00000u); /* 1.5 */
            l11_w32(o, d + 0x4C, 0xBF333333u); /* -0.7 */
            l11_w32(o, d + 0x50, 0);
            l11_w8(o, d + 0x7A, 1);
        }
        break;
    case 6:
        if ((int8_t)l11_u8(o, d + 0x74) & 2) {
            l11_w32(o, d + 0x4C, 0);
            if (l11_u32(o, d + 0x30) & L11_D_END) {
                l11_w8(o, e + 5, 0);
                l11_w8(o, e + 6, 0);
                l11_w16(o, d + 0x64, 0);
                l11_w8(o, d + 0x7A, 0);
            }
        }
        break;
    case 0x63:
        l11_w8(o, e + 5, 2);
        l11_w8(o, e + 6, 4);
        l11_w32(o, d + 0x40, F_2);
        l11_w32(o, d + 0x50, 0xBD4CCCCDu); /* -0.05 */
        l11_c_001C67E0(o, e, 6, F_ZERO, F_ZERO);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------
 * 00144C20 (e, d): behaviour 7 (the jump aside; decomp NEARMISS). +6 0: +6 =
 * 1, d +0x75 = 0, +0x40 = 1, +0x48 = 2, +0x4C = 1.3, 001FBD50(e, 0x831, 0,
 * 300), clip 7 (0, 17); the turn wrap(D_00810374 - e +0xC4) at 0x70003A20,
 * clamped to +-pi/4, is added to e +0xC4 (wrapped); d +0x44 = that yaw +-
 * 3.1241393 (minus for a negative turn, wrapped). +6 1: d +0x48 < 0: +6 =
 * 2, clip 9 (0, 0); else clip 8 (0, 0) at the end of another clip. +6 2: d
 * +0x40 = 0 once e +0x3C <= 31; d +0x74 bit 1: +6 += 1, d +0x40 = 1, +0x50 =
 * -0.05, +0x54 = 10, +0x58 = -0.275, 001FBD50(e, 0x832, 0, 300); 001B1560(e,
 * the player, 80 degrees) clear -> clip 6 (5, 12); set -> e +5 = +6 = 2, +7
 * = 0, d +0x4C = 1.15, +0x50 = 0, clip 5 (5, 20). +6 3: d +0x7A = 0; a
 * negative d +0x4C zeroes +0x50 and +0x4C; a negative d +0x54 zeroes +0x58
 * and +0x54; d +0x54 += d +0x58; the bone record's +0x74 turns toward pi at
 * pi * d +0x54 / 180 a frame (001B12B0). It reached pi, or d +0x54 is zero:
 * e +0xC4 = wrap(e +0xC4 + it), it = 0, d +0x50 = +0x4C = 0, and e +5 = 6
 * within 20 (d +0x5C) with 001B1560(e, the player, pi/4) set, else e +5 =
 * 2; e +6 = 0. */
static void l11_00144C20(L11 *o, uint32_t e, uint32_t d)
{
    int32_t r = 0;
    uint32_t f = 0;
    uint32_t st = l11_u8(o, e + 6);
    switch (st) {
    case 0: {
        l11_w8(o, e + 6, st + 1);
        l11_w8(o, d + 0x75, 0);
        l11_w32(o, d + 0x40, F_ONE);
        l11_w32(o, d + 0x48, F_2);
        l11_w32(o, d + 0x4C, 0x3FA66666u); /* 1.3 */
        if (l11_c_001FBD50(o, e, 0x831, 0, F_300, &r)) return;
        if (l11_c_001C67E0(o, e, 7, F_ZERO, 0x41880000u)) return; /* 17.0 */
        uint32_t mine = l11_u32(o, e + 0xC4);
        if (l11_c_001B1470(o, L11_SUB(l11_u32(o, PLAYER_YAW), mine), &f)) return;
        l11_w32(o, S_70003A20, f);
        if (L11_LT(f, F_ZERO)) {
            if (L11_LT(f, 0xBF490FDBu)) l11_w32(o, S_70003A20, 0xBF490FDBu); /* -pi/4 */
            mine = l11_u32(o, e + 0xC4);
            if (l11_c_001B1470(o, L11_ADD(mine, l11_u32(o, S_70003A20)), &f)) return;
            l11_w32(o, e + 0xC4, f);
            if (l11_c_001B1470(o, L11_SUB(f, 0x4047F1E6u), &f)) return;
        } else {
            if (!L11_LE(f, F_QPI)) l11_w32(o, S_70003A20, F_QPI);
            mine = l11_u32(o, e + 0xC4);
            if (l11_c_001B1470(o, L11_ADD(mine, l11_u32(o, S_70003A20)), &f)) return;
            l11_w32(o, e + 0xC4, f);
            if (l11_c_001B1470(o, L11_ADD(0x4047F1E6u, f), &f)) return;
        }
        l11_w32(o, d + 0x44, f);
        break;
    }
    case 1:
        if (L11_LT(l11_u32(o, d + 0x48), F_ZERO)) {
            l11_w8(o, e + 6, st + 1);
            l11_c_001C67E0(o, e, 9, F_ZERO, F_ZERO);
        } else if ((l11_s16(o, e + 0x2C) & (int32_t)0xFFFF7FFF) != 8 && (l11_u32(o, d + 0x30) & L11_D_END)) {
            l11_c_001C67E0(o, e, 8, F_ZERO, F_ZERO);
        }
        break;
    case 2:
        if (L11_LE(l11_u32(o, e + 0x3C), 0x41F80000u)) l11_w32(o, d + 0x40, 0); /* 31.0 */
        if ((int8_t)l11_u8(o, d + 0x74) & 2) {
            l11_w8(o, e + 6, l11_u8(o, e + 6) + 1);
            l11_w32(o, d + 0x40, F_ONE);
            l11_w32(o, d + 0x50, 0xBD4CCCCDu); /* -0.05 */
            l11_w32(o, d + 0x54, 0x41200000u); /* 10.0 */
            l11_w32(o, d + 0x58, 0xBE8CCCCDu); /* -0.275 */
            if (l11_c_001FBD50(o, e, 0x832, 0, F_300, &r)) return;
            if (l11_c_001B1560(o, e, PLAYER_XZ, 0x3FB2B8C3u, &r)) return; /* 80 degrees */
            if (r == 0) {
                l11_c_001C67E0(o, e, 6, F_5, 0x41400000u); /* 12.0 */
            } else {
                l11_w8(o, e + 5, 2);
                l11_w8(o, e + 6, 2);
                l11_w8(o, e + 7, 0);
                l11_w32(o, d + 0x4C, 0x3F933333u); /* 1.15 */
                l11_w32(o, d + 0x50, 0);
                l11_c_001C67E0(o, e, 5, F_5, F_20);
            }
        }
        break;
    case 3: {
        l11_w8(o, d + 0x7A, 0);
        if (L11_LT(l11_u32(o, d + 0x4C), F_ZERO)) {
            l11_w32(o, d + 0x50, 0);
            l11_w32(o, d + 0x4C, 0);
        }
        if (L11_LT(l11_u32(o, d + 0x54), F_ZERO)) {
            l11_w32(o, d + 0x58, 0);
            l11_w32(o, d + 0x54, 0);
        }
        uint32_t t = l11_u32(o, d + 0x54);
        t = L11_ADD(t, l11_u32(o, d + 0x58));
        l11_w32(o, d + 0x54, t);
        uint32_t rate = L11_DIV(L11_MUL(F_PI, t), 0x43340000u); /* 180.0 */
        if (l11_c_001B12B0(o, F_PI, l11_u32(o, l11_bone(o) + 0x74), rate, &f)) return;
        l11_w32(o, l11_bone(o) + 0x74, f);
        uint32_t ang = l11_u32(o, l11_bone(o) + 0x74);
        if (!L11_EQ(F_PI, ang) && !L11_EQ(l11_u32(o, d + 0x54), F_ZERO)) break;
        if (l11_c_001B1470(o, L11_ADD(l11_u32(o, e + 0xC4), ang), &f)) return;
        l11_w32(o, e + 0xC4, f);
        l11_w32(o, l11_bone(o) + 0x74, 0);
        l11_w32(o, d + 0x50, 0);
        l11_w32(o, d + 0x4C, 0);
        if (L11_LE(l11_u32(o, d + 0x5C), F_20)) {
            if (l11_c_001B1560(o, e, PLAYER_XZ, F_QPI, &r)) return;
            if (r != 0) {
                l11_w8(o, e + 5, 6);
                l11_w8(o, e + 6, 0);
                break;
            }
        }
        l11_w8(o, e + 5, 2);
        l11_w8(o, e + 6, 0);
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------------
 * 00145210 (e, d): the hit stage. +6 0: +6 = 1, d +0x40 = 1, +0x50 = +0x4C =
 * 0. Bit 0x8000 of e +0x36: clip 0x1F (1, 0), 001FBD50(e, 0x839, 0, 300).
 * Else d +0x72 = 10 (bit 0x1000) or 0, e +0 = 1, e +0x36 = 0, the clip
 * 0x2753E8[(rand >> 17) % 3] (1, 0), 001FBD50(e, 0x838, 0, 300). Then the
 * held player's drop. +6 1: at the clip's end e +4 = 1, +5 = 2, +6 = 0, +0 =
 * 1, d +0x64 = 300, e +0x36 = 0, d +0x72 = 0x5A and d +0x62 is re-armed. */
static void l11_00145210(L11 *o, uint32_t e, uint32_t d)
{
    int bad = 0;
    int32_t r = 0;
    uint32_t st = l11_u8(o, e + 6);
    if (st == 0) {
        l11_w8(o, e + 6, st + 1);
        l11_w32(o, d + 0x40, F_ONE);
        l11_w32(o, d + 0x50, 0);
        l11_w32(o, d + 0x4C, 0);
        int32_t hit = l11_s16(o, e + 0x36);
        if (hit & 0x8000) {
            if (l11_c_001C67E0(o, e, 0x1F, F_ONE, F_ZERO)) return;
            if (l11_c_001FBD50(o, e, 0x839, 0, F_300, &r)) return;
        } else {
            l11_w8(o, d + 0x72, (hit & 0x1000) ? 0xAu : 0u);
            l11_w8(o, e, 1);
            l11_w16(o, e + 0x36, 0);
            uint32_t rnd = l11_rand(o, &bad);
            if (bad) return;
            int32_t pick = l11_sra(rnd, 17) % 3;
            int32_t clip = l11_s16(o, 0x002753E8u + (uint32_t)(pick * 2));
            if (l11_c_001C67E0(o, e, clip, F_ONE, F_ZERO)) return;
            if (l11_c_001FBD50(o, e, 0x838, 0, F_300, &r)) return;
        }
        l11_drop_held(o, e, d);
    } else if (st == 1) {
        if (!(l11_u32(o, d + 0x30) & L11_D_END)) return;
        l11_back_alive(o, e);
        l11_w16(o, d + 0x64, 0x12C);
        l11_w16(o, e + 0x36, 0);
        l11_w8(o, d + 0x72, 0x5A);
        l11_rearm_chase(o, d);
    }
}

/* ------------------------------------------------------------------------
 * 001453C0 (e, d): the knock back. +6 0: +6 = 1, d +0x75 = 0, +0x40 = 1,
 * +0x48 = 1.5, +0x4C = -1, +0x50 = 0, clip 0x1D (0, 0), 001FBD50(e, 0x838, 0,
 * 300), e +0xC4 = wrap(pi + atan2(e +0x70, e +0x78)), the held player's drop.
 * +6 1: d +0x74 bit 1: d +0x4C = 0; at the clip's end +6 += 1, e +0xC4 =
 * wrap(pi + e +0xC4), clip 0x1E (1, 0), and no health left: e +5 = 2, +6 =
 * 0, d +0x7C = 1. +6 2: at the clip's end back alive, e +0x36 = 0, d +0x72 =
 * 0x5A, d +0x64 = 300 and d +0x62 is re-armed. */
static void l11_001453C0(L11 *o, uint32_t e, uint32_t d)
{
    int32_t r = 0;
    uint32_t f = 0;
    uint32_t st = l11_u8(o, e + 6);
    switch (st) {
    case 0: {
        l11_w8(o, e + 6, st + 1);
        l11_w8(o, d + 0x75, 0);
        l11_w32(o, d + 0x40, F_ONE);
        l11_w32(o, d + 0x48, 0x3FC00000u); /* 1.5 */
        l11_w32(o, d + 0x4C, 0xBF800000u); /* -1.0 */
        l11_w32(o, d + 0x50, 0);
        if (l11_c_001C67E0(o, e, 0x1D, F_ZERO, F_ZERO)) return;
        if (l11_c_001FBD50(o, e, 0x838, 0, F_300, &r)) return;
        uint32_t z = l11_u32(o, e + 0x78);
        if (l11_c_0011E620(o, l11_u32(o, e + 0x70), z, &f)) return;
        if (l11_c_001B1470(o, L11_ADD(F_PI, f), &f)) return;
        l11_w32(o, e + 0xC4, f);
        l11_drop_held(o, e, d);
        break;
    }
    case 1:
        if ((int8_t)l11_u8(o, d + 0x74) & 2) l11_w32(o, d + 0x4C, 0);
        if (!(l11_u32(o, d + 0x30) & L11_D_END)) break;
        l11_w8(o, e + 6, l11_u8(o, e + 6) + 1);
        if (l11_c_001B1470(o, L11_ADD(F_PI, l11_u32(o, e + 0xC4)), &f)) return;
        l11_w32(o, e + 0xC4, f);
        if (l11_c_001C67E0(o, e, 0x1E, F_ONE, F_ZERO)) return;
        if (l11_s16(o, e + 0x34) == 0) {
            l11_w8(o, e + 5, 2);
            l11_w8(o, e + 6, 0);
            l11_w8(o, d + 0x7C, 1);
        }
        break;
    case 2:
        if (!(l11_u32(o, d + 0x30) & L11_D_END)) break;
        l11_back_alive(o, e);
        l11_w16(o, e + 0x36, 0);
        l11_w8(o, d + 0x72, 0x5A);
        l11_w16(o, d + 0x64, 0x12C);
        l11_rearm_chase(o, d);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------
 * 001455A0 (e, d): the death. +6 0: e +0 = 2, d +0x75 = +0x4C = +0x50 = 0,
 * +0x40 = 1, +0x54 = 0.08, 001B1190(e +0x9A), d +0x7C set: +6 = 2, clip 0x21
 * (1, 0), else +6 = 1, clip 0x20 (1, 0); 001FBD50(e, 0x83B, 0, 300), the held
 * player's drop. +6 1: at the clip's end +6 = 2, d +0x4C = 0, clip 0x21 (1,
 * 0). +6 2: at the clip's end with d +0x78 clear: +6 = 3, d +0x7E = 0,
 * 001FBD50(e, 0x83A, 0, 300), 001EFE00(0x8000001E, e) zero -> e +4 = 3.
 * Every state, with d +0x78 set: e +0x8C += d +0x54; rising and at 4 or
 * more: e +0x8C = 4, d +0x54 = -0.08, 001CA5E0(e, D_0028A490[0x7E with e
 * +0xD bit 7, else 0x7D], 0), e +0x80..0x88 = 1; falling and at 1 or less: e
 * +0x8C = 1, d +0x54 = 0, d +0x78 = 0. */
static void l11_001455A0(L11 *o, uint32_t e, uint32_t d)
{
    int32_t r = 0;
    uint32_t st = l11_u8(o, e + 6);
    switch (st) {
    case 0:
        l11_w8(o, e, 2);
        l11_w8(o, d + 0x75, 0);
        l11_w32(o, d + 0x4C, 0);
        l11_w32(o, d + 0x50, 0);
        l11_w32(o, d + 0x40, F_ONE);
        l11_w32(o, d + 0x54, 0x3DA3D70Au); /* 0.08 */
        if (l11_c_001B1190(o, (int32_t)l11_u8(o, e + 0x9A))) return;
        if ((int8_t)l11_u8(o, d + 0x7C) != 0) {
            l11_w8(o, e + 6, 2);
            if (l11_c_001C67E0(o, e, 0x21, F_ONE, F_ZERO)) return;
        } else {
            l11_w8(o, e + 6, 1);
            if (l11_c_001C67E0(o, e, 0x20, F_ONE, F_ZERO)) return;
        }
        if (l11_c_001FBD50(o, e, 0x83B, 0, F_300, &r)) return;
        l11_drop_held(o, e, d);
        break;
    case 1:
        if (l11_u32(o, d + 0x30) & L11_D_END) {
            l11_w8(o, e + 6, st + 1);
            l11_w32(o, d + 0x4C, 0);
            if (l11_c_001C67E0(o, e, 0x21, F_ONE, F_ZERO)) return;
        }
        break;
    case 2:
        if ((l11_u32(o, d + 0x30) & L11_D_END) && (int8_t)l11_u8(o, d + 0x78) == 0) {
            l11_w8(o, e + 6, st + 1);
            l11_w8(o, d + 0x7E, 0);
            if (l11_c_001FBD50(o, e, 0x83A, 0, F_300, &r)) return;
            if (l11_c_001EFE00(o, (int32_t)0x8000001Eu, e, &r)) return;
            if (r == 0) l11_w8(o, e + 4, 3);
        }
        break;
    default:
        break;
    }
    if (l11_failed(o)) return;
    if ((int8_t)l11_u8(o, d + 0x78) == 0) return;
    uint32_t step = l11_u32(o, d + 0x54);
    uint32_t glow = l11_u32(o, e + 0x8C);
    l11_w32(o, e + 0x8C, L11_ADD(glow, step));
    if (!L11_LE(l11_u32(o, d + 0x54), F_ZERO)) {
        if (L11_LT(l11_u32(o, e + 0x8C), 0x40800000u)) return; /* 4.0 */
        l11_w32(o, e + 0x8C, 0x40800000u);
        l11_w32(o, d + 0x54, 0xBDA3D70Au); /* -0.08 */
        uint32_t index = (l11_u8(o, e + 0xD) & 0x80u) ? 0x7Eu : 0x7Du;
        if (l11_c_001CA5E0(o, e, l11_u32(o, 0x0028A490u + index * 4u), 0)) return;
        l11_w32(o, e + 0x80, F_ONE);
        l11_w32(o, e + 0x84, F_ONE);
        l11_w32(o, e + 0x88, F_ONE);
    } else if (L11_LE(l11_u32(o, e + 0x8C), F_ONE)) {
        l11_w32(o, e + 0x8C, F_ONE);
        l11_w32(o, d + 0x54, 0);
        l11_w8(o, d + 0x78, 0);
    }
}

/* ------------------------------------------------------------------------
 * 001450B0 (e, d): the stage frame (e +4 == 2). By e +5: 0 00145210, 1
 * 001453C0, 2 001455A0, others nothing. Clips 9 / 8 (the low byte of d +0x30)
 * call 001FBD50(e, 0x827 / 0x82C + ((rand >> 13) % 5), 0, 300). Then 00146CE0,
 * 00145880, 001459A0; d +0x30 = 001C64F0(e, 1.0); 00131ED0, 001C68C0, 001B17A0
 * and the +0x4C method. */
static void l11_001450B0(L11 *o, uint32_t e, uint32_t d)
{
    int bad = 0;
    int32_t r = 0;
    switch (l11_u8(o, e + 5)) {
    case 0: l11_00145210(o, e, d); break;
    case 1: l11_001453C0(o, e, d); break;
    case 2: l11_001455A0(o, e, d); break;
    default: break;
    }
    if (l11_failed(o)) return;
    uint32_t clip = l11_u8(o, d + 0x30);
    if (clip == 9 || clip == 8) {
        uint32_t rnd = l11_rand(o, &bad);
        if (bad) return;
        int32_t pick = l11_sra(rnd, 13) % 5 + (clip == 9 ? 0x827 : 0x82C);
        if (l11_c_001FBD50(o, e, pick, 0, F_300, &r)) return;
    }
    (void)l11_00146CE0(o, e, d);
    l11_00145880(o, e, d);
    l11_001459A0(o, e, d);
    if (l11_failed(o)) return;
    if (l11_c_001C64F0(o, e, F_ONE, &r)) return;
    l11_w32(o, d + 0x30, (uint32_t)r);
    l11_c_00131ED0(o, e);
    l11_c_001C68C0(o, e);
    l11_c_001B17A0(o, e, &r);
    l11_method(o, e);
}

/* ------------------------------------------------------------------------
 * 00145850 (e): the last frame (e +4 == 3): 001B1190(e +0x9A), 001AFC10(e). */
static void l11_00145850(L11 *o, uint32_t e)
{
    if (l11_c_001B1190(o, (int32_t)l11_u8(o, e + 0x9A))) return;
    l11_c_001AFC10(o, e);
}

#define L11_ENTRY_ED(name, call)                                                                     \
    int name(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, EmLevel11PortFault *fault)         \
    {                                                                                                \
        L11 o;                                                                                       \
        if (l11_begin(&o, h, fault)) return -1;                                                      \
        call;                                                                                        \
        return l11_end(&o);                                                                          \
    }
L11_ENTRY_ED(em_level11_port_001434C0, l11_001434C0(&o, e, d))
L11_ENTRY_ED(em_level11_port_00143610, l11_00143610(&o, e, d))
L11_ENTRY_ED(em_level11_port_00143AF0, l11_00143AF0(&o, e, d))
L11_ENTRY_ED(em_level11_port_00144040, l11_00144040(&o, e, d))
L11_ENTRY_ED(em_level11_port_00144C20, l11_00144C20(&o, e, d))
L11_ENTRY_ED(em_level11_port_001450B0, l11_001450B0(&o, e, d))
L11_ENTRY_ED(em_level11_port_00145210, l11_00145210(&o, e, d))
L11_ENTRY_ED(em_level11_port_001453C0, l11_001453C0(&o, e, d))
L11_ENTRY_ED(em_level11_port_001455A0, l11_001455A0(&o, e, d))
L11_ENTRY_ED(em_level11_port_00145850, ((void)d, l11_00145850(&o, e)))
