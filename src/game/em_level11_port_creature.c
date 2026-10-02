/* The eleventh level's 0x141D20 actor family (boot code): the two actors
 * the group 0x82A230 spawns at [7]'s end (ELEVENTH_LEVEL_ROUTE.md section
 * 2) run 00141D20 as their behaviour. docs/LEVEL11_PORT.md section 2
 * (behaviour) and section 3 (verification).
 *
 * e = the actor (pool node), d = e + 0x1F0 (its behaviour block). Every
 * function follows the original instructions; the decomp's C (byte-matched
 * for 00141F00, 00142070, 00145880, 001464B0, 00146CE0, 001471E0; NEARMISS
 * or word assembly for the others) is a guide, and where it and the
 * original disagree the original wins (docs/LEVEL11_PORT.md section 0).
 * Floats are bit patterns (em_ee_float.h); no expression reads memory twice
 * in an unsequenced way (each read is its own statement or the only read
 * of its expression), so the access order is the source order. */
#include "em_level11_port_internal.h"

#define S_700038A0 0x700038A0u
#define S_700038A4 0x700038A4u
#define S_700038A8 0x700038A8u
#define S_700038AC 0x700038ACu
#define S_700038B0 0x700038B0u
#define S_700038B4 0x700038B4u
#define S_700038B8 0x700038B8u
#define S_700038BC 0x700038BCu
#define S_700038C0 0x700038C0u
#define S_700038D0 0x700038D0u
#define S_700036A0 0x700036A0u
#define S_70003A20 0x70003A20u
#define S_700031D0 0x700031D0u

#define F_PI      0x40490FDBu
#define F_TWO_PI  0x40C90FDBu
#define F_HALF_PI 0x3FC90FDBu
#define F_QPI     0x3F490FDBu /* pi / 4 */
#define F_255     0x437F0000u
#define F_300     0x43960000u

static uint32_t l11_rand(L11 *o, int *bad)
{
    int32_t r = 0;
    if (l11_c_00122BB8(o, &r)) *bad = 1;
    return (uint32_t)r;
}

/* ------------------------------------------------------------------------
 * 001471E0 (e, d): the zone byte d +0x7F. With D_00810700 == 0xD: the
 * player's z (D_00810368) >= 970 -> 1; else 001B1EA0(0, the player, the
 * area 0x245A60, 4) -> 2; else the area 0x245AA0 -> 3. Other areas: z >=
 * 1041.04 -> 4. Zone 0: d +0x10 = d (00102948). Else: d +0x64 = 0, d +0x10
 * = the record 0x245AE0 + (zone - 1) * 16, and when 001B13F0(d +0x10, e
 * +0xB0, 20) (both y zeroed, at 0x700038A0 / 0x700038B0) is set the zone
 * byte gets bit 7. */
void l11_001471E0(L11 *o, uint32_t e, uint32_t d)
{
    int32_t r = 0;
    l11_w8(o, d + 0x7F, 0);
    if (l11_u8(o, 0x00810700u) == 0xDu) {
        if (!L11_LT(l11_u32(o, 0x00810368u), 0x44728000u)) { /* 970.0 */
            l11_w8(o, d + 0x7F, 1);
        } else {
            if (l11_c_001B1EA0(o, 0, 0x00810360u, 0x00245A60u, 4, &r)) return;
            if (r != 0) {
                l11_w8(o, d + 0x7F, 2);
            } else {
                if (l11_c_001B1EA0(o, 0, 0x00810360u, 0x00245AA0u, 4, &r)) return;
                if (r != 0) l11_w8(o, d + 0x7F, 3);
            }
        }
    } else if (!L11_LT(l11_u32(o, 0x00810368u), 0x44822148u)) { /* 1041.04 */
        l11_w8(o, d + 0x7F, 4);
    }
    int32_t zone = (int8_t)l11_u8(o, d + 0x7F);
    if (zone == 0) {
        l11_c_00102948(o, d + 0x10, d);
        return;
    }
    l11_w16(o, d + 0x64, 0);
    zone = (int8_t)l11_u8(o, d + 0x7F);
    l11_c_00102948(o, d + 0x10, 0x00245AE0u + (uint32_t)((zone - 1) << 4));
    l11_c_00102948(o, S_700038A0, d + 0x10);
    l11_c_00102948(o, S_700038B0, e + 0xB0);
    l11_w32(o, S_700038B4, 0);
    l11_w32(o, S_700038A4, 0);
    if (l11_c_001B13F0(o, S_700038A0, S_700038B0, 0x41A00000u, &r)) return; /* 20.0 */
    if (r != 0) l11_w8(o, d + 0x7F, l11_u8(o, d + 0x7F) | 0x80u);
}

/* ------------------------------------------------------------------------
 * 00146CE0 (e, d): the hit counter. Returns 0 unless e +0x34 (the health)
 * and e +0x36 (the hit) are set; with d +0x72 or d +0x7B set the hit is
 * dropped (+0x36 = 0) and 0 returned. Else e +0 = 2, d +0x64 = 300, d +0x72
 * = 0xFF; the damage = (hit & 0xFFF), x5 with bit 0x8000, x2 with 0x4000;
 * d +0x7B (never set here, read twice) -> 0021C040(the player, e) and
 * D_008106BD 1 -> 0; d +0x66 += damage (d +0x73 set) or = damage; d +0x73
 * = 0x19; d +0x7D 0 with bit 0x4000: d +0x7D = 0x3C, 001EFE00(0x80000027,
 * e). Health <= damage: health 0, d +0x7C = 0, e +4 = 2, +5 = 2, +6 = 0,
 * 001B4CF0(e), e +5 = 1 unless d +0x74 bit 1 is set without hit bits
 * 0xA000; 1. Else health -= damage; bit 0x2000: e +4 = 2, +5 = 1, +6 = 0,
 * d +0x66 = 0, 1; e +4 1 with d +0x66 >= 0x19, or bit 0x8000: e +4 = 2,
 * +6 = 0, d +0x66 = 0, +5 = 0 (d +0x74 bit 1) or 1, 1; else d +0x72 = 0x1E
 * (bits 0x5000) or 0, e +0 = 1, +0x36 = 0, 0. */
int32_t l11_00146CE0(L11 *o, uint32_t e, uint32_t d)
{
    int32_t r = 0;
    if (l11_s16(o, e + 0x34) == 0 || l11_s16(o, e + 0x36) == 0) return 0;
    if (l11_u8(o, d + 0x72) != 0 || l11_u8(o, d + 0x7B) != 0) {
        l11_w16(o, e + 0x36, 0);
        return 0;
    }
    l11_w8(o, e, 2);
    l11_w16(o, d + 0x64, 0x12C);
    l11_w8(o, d + 0x72, 0xFF);
    int32_t hit = l11_s16(o, e + 0x36);
    int32_t dmg = (int16_t)(hit & 0xFFF);
    if (hit & 0x8000) dmg = (int16_t)(dmg * 5);
    if (hit & 0x4000) dmg = (int16_t)(dmg * 2);
    if (l11_u8(o, d + 0x7B) != 0) {
        l11_w8(o, d + 0x7B, 0);
        if (l11_c_0021C040(o, 0x008102B0u, e)) return 0;
        if (l11_u8(o, 0x008106BDu) == 1) l11_w8(o, 0x008106BDu, 0);
    }
    if (l11_u8(o, d + 0x73) != 0)
        l11_w16(o, d + 0x66, (uint32_t)(l11_s16(o, d + 0x66) + dmg));
    else
        l11_w16(o, d + 0x66, (uint32_t)dmg);
    l11_w8(o, d + 0x73, 0x19);
    if ((int8_t)l11_u8(o, d + 0x7D) == 0 && (l11_s16(o, e + 0x36) & 0x4000)) {
        l11_w8(o, d + 0x7D, 0x3C);
        if (l11_c_001EFE00(o, (int32_t)0x80000027u, e, &r)) return 0;
    }
    int32_t health = l11_s16(o, e + 0x34);
    if (health <= dmg) {
        l11_w16(o, e + 0x34, 0);
        l11_w8(o, d + 0x7C, 0);
        l11_w8(o, e + 4, 2);
        l11_w8(o, e + 5, 2);
        l11_w8(o, e + 6, 0);
        if (l11_c_001B4CF0(o, e)) return 0;
        if (l11_u8(o, d + 0x74) & 2u) {
            if (l11_s16(o, e + 0x36) & 0xA000) l11_w8(o, e + 5, 1);
        } else {
            l11_w8(o, e + 5, 1);
        }
        return 1;
    }
    l11_w16(o, e + 0x34, (uint32_t)(health - dmg));
    int32_t bits = l11_s16(o, e + 0x36);
    if (bits & 0x2000) {
        l11_w8(o, e + 4, 2);
        l11_w8(o, e + 5, 1);
        l11_w8(o, e + 6, 0);
        l11_w16(o, d + 0x66, 0);
        return 1;
    }
    int heavy = (bits & 0x8000) != 0;
    if (l11_u8(o, e + 4) == 1 && l11_s16(o, d + 0x66) >= 0x19) heavy = 1;
    if (heavy) {
        l11_w8(o, e + 4, 2);
        l11_w8(o, e + 6, 0);
        l11_w16(o, d + 0x66, 0);
        if (l11_u8(o, d + 0x74) & 2u)
            l11_w8(o, e + 5, 0);
        else
            l11_w8(o, e + 5, 1);
        return 1;
    }
    l11_w8(o, d + 0x72, (bits & 0x5000) ? 0x1Eu : 0u);
    l11_w8(o, e, 1);
    l11_w16(o, e + 0x36, 0);
    return 0;
}

/* ------------------------------------------------------------------------
 * 00145880 (e, d): the fall and the run. Nothing with d +0x75 bit 0. The
 * vertical speed d +0x48 -= 0.1 and e's y += it. With d +0x75 bit 1, bit 7
 * clear, d +0x5C <= 18 and 001B1560(e, the player, 1.1344640) set, e +0x52
 * |= 1 and done. Else the speed d +0x4C += d +0x50 and x / z += speed *
 * 0011E2A8 / 0011DE90 of the yaw e +0xC4. */
void l11_00145880(L11 *o, uint32_t e, uint32_t d)
{
    if ((int8_t)l11_u8(o, d + 0x75) & 1) return;
    uint32_t v = L11_ADD(l11_u32(o, d + 0x48), 0xBDCCCCCDu); /* -0.1 */
    l11_w32(o, d + 0x48, v);
    l11_w32(o, e + 0xB4, L11_ADD(l11_u32(o, e + 0xB4), v));
    int32_t f = (int8_t)l11_u8(o, d + 0x75);
    if ((f & 2) && !(f & 0x80) && L11_LE(l11_u32(o, d + 0x5C), 0x41900000u)) { /* 18.0 */
        int32_t r = 0;
        if (l11_c_001B1560(o, e, 0x00810360u, 0x3F91361Eu, &r)) return; /* 1.1344640 */
        if (r != 0) {
            l11_w16(o, e + 0x52, l11_u16(o, e + 0x52) | 1u);
            return;
        }
    }
    uint32_t accel = l11_u32(o, d + 0x50);
    uint32_t speed = l11_u32(o, d + 0x4C);
    l11_w32(o, d + 0x4C, L11_ADD(speed, accel));
    uint32_t s = 0;
    if (l11_c_0011E2A8(o, l11_u32(o, e + 0xC4), &s)) return;
    speed = l11_u32(o, d + 0x4C);
    l11_w32(o, e + 0xB0, L11_ADD(l11_u32(o, e + 0xB0), L11_MUL(speed, s)));
    if (l11_c_0011DE90(o, l11_u32(o, e + 0xC4), &s)) return;
    speed = l11_u32(o, d + 0x4C);
    l11_w32(o, e + 0xB8, L11_ADD(l11_u32(o, e + 0xB8), L11_MUL(speed, s)));
}

/* ------------------------------------------------------------------------
 * 001464B0 (e, d): the forward probe. (0, 3, 33.5, 1) through e's frame
 * (001B2B10) plus e +0xB0 at 0x700038A0; v = 001B2BF0(e, it, 0x700038D0,
 * pi/4). Bit 0 clear: bit 2 clear -> 3; else (d +0x5C <= 10 and 001B1560(e,
 * the player, pi/4) set -> 3) 00146740(e, d). Bit 0 set with bit 2: unless
 * e's y + 15 < the floor (0x700038D0) or the hit object's (0x700031D0)
 * +0x1A is 0x46, a wider probe (0, 15.5, 33.5, 1) through 0019AD00(e, it,
 * 7) == 0 -> 2; then as above. Bit 0 set without bit 2: e's y - 20 >= the
 * floor -> 3, else 0. */
int32_t l11_001464B0(L11 *o, uint32_t e, uint32_t d)
{
    int32_t r = 0;
    uint32_t f = 0;
    l11_w32(o, S_700038A0, 0);
    l11_w32(o, S_700038A4, 0x40400000u);
    l11_w32(o, S_700038A8, 0x42060000u);
    l11_w32(o, S_700038AC, 0x3F800000u);
    l11_c_001B2B10(o, e, S_700038A0, S_700038A0);
    l11_c_001028B8(o, S_700038A0, S_700038A0, e + 0xB0);
    int32_t v = 0;
    if (l11_c_001B2BF0(o, e, S_700038A0, S_700038D0, F_QPI, &v)) return 0;
    if (!(v & 1)) {
        if (!(v & 4)) return 3;
    } else if (v & 4) {
        f = L11_ADD(0x41700000u, l11_u32(o, e + 0xB4)); /* 15.0 */
        if (!L11_LT(f, l11_u32(o, S_700038D0)) && l11_u8(o, l11_u32(o, S_700031D0) + 0x1A) != 0x46u) {
            l11_w32(o, S_700038A0, 0);
            l11_w32(o, S_700038A4, 0x41780000u);
            l11_w32(o, S_700038A8, 0x42060000u);
            l11_w32(o, S_700038AC, 0x3F800000u);
            l11_c_001B2B10(o, e, S_700038A0, S_700038A0);
            l11_c_001028B8(o, S_700038A0, S_700038A0, e + 0xB0);
            if (l11_c_0019AD00(o, e, S_700038A0, 7, &r)) return 0;
            if (r == 0) return 2;
        }
    } else {
        f = L11_SUB(l11_u32(o, e + 0xB4), 0x41A00000u); /* 20.0 */
        if (!L11_LT(f, l11_u32(o, S_700038D0))) return 3;
        return 0;
    }
    if (L11_LE(l11_u32(o, d + 0x5C), 0x41200000u)) { /* 10.0 */
        if (l11_c_001B1560(o, e, 0x00810360u, F_QPI, &r)) return 0;
        if (r != 0) return 3;
    }
    if (l11_c_00146740(o, e, d, &r)) return 0;
    return r;
}

/* ------------------------------------------------------------------------
 * 00146110 (e, d): the step probe. (0, 3, 10, 1) through e's frame plus
 * +0xB0 at 0x700038A0; flags = 001B2BF0(e, it, 0x700038D0, pi/4). Bit 0
 * clear: bit 2 -> the speed 001B3580(e, the hit polygon's point (+0x24..,
 * 1) in the frame), else 001B37D0(e, 10, 20); d +0x44 = it, 1. Bit 0 with
 * bit 2: the point in the frame; unless e's y + 15 < the floor, with the
 * hit +0x1A != 0x46 and (00122BB8() >> 6) & 3: a vertical probe from e's
 * position (y + 5 .. y + 30, 0019A570(.., 6, 0)) that misses, then the
 * wide probe (0, 15.5, 10, 1) through 0019AD00(e, .., 7) == 0: e +5 = 3,
 * +6 = 0, 2; else d +0x44 = 001B3580(e, the point), 1. Bit 0 without bit
 * 2: e's y - 20 >= the floor -> d +0x44 = 001B37D0(e, 10, 20), 1; else
 * with d +0x7A 0 and d +0x74 bit 0 the same, 1; else 0. */
int32_t l11_00146110(L11 *o, uint32_t e, uint32_t d, uint32_t sp)
{
    uint32_t pt = sp - 0x10; /* the original's local vector */
    int32_t r = 0;
    uint32_t speed = 0;
    l11_w32(o, S_700038A0, 0);
    l11_w32(o, S_700038A4, 0x40400000u);
    l11_w32(o, S_700038A8, 0x41200000u);
    l11_w32(o, S_700038AC, 0x3F800000u);
    l11_c_001B2B10(o, e, S_700038A0, S_700038A0);
    l11_c_001028B8(o, S_700038A0, S_700038A0, e + 0xB0);
    int32_t flags = 0;
    if (l11_c_001B2BF0(o, e, S_700038A0, S_700038D0, F_QPI, &flags)) return 0;
    if (!(flags & 1)) {
        if (flags & 4) {
            uint32_t hit = l11_u32(o, S_700031D0);
            l11_w32(o, pt, l11_u32(o, hit + 0x24));
            l11_w32(o, pt + 4, l11_u32(o, hit + 0x28));
            l11_w32(o, pt + 8, l11_u32(o, hit + 0x2C));
            l11_w32(o, pt + 12, F_ONE);
            if (l11_c_001B3580(o, e, pt, &speed)) return 0;
        } else {
            if (l11_c_001B37D0(o, e, 0x41200000u, 0x41A00000u, &speed)) return 0;
        }
        l11_w32(o, d + 0x44, speed);
        return 1;
    }
    if (flags & 4) {
        uint32_t hit = l11_u32(o, S_700031D0);
        uint32_t x = l11_u32(o, hit + 0x24);
        uint32_t floor_ = l11_u32(o, S_700038D0);
        l11_w32(o, pt, x);
        l11_w32(o, pt + 4, l11_u32(o, hit + 0x28));
        l11_w32(o, pt + 8, l11_u32(o, hit + 0x2C));
        l11_w32(o, pt + 12, F_ONE);
        uint32_t lim = L11_ADD(0x41700000u, l11_u32(o, e + 0xB4)); /* 15.0 */
        if (!L11_LT(lim, floor_) && l11_u8(o, hit + 0x1A) != 0x46u) {
            int bad = 0;
            uint32_t rnd = l11_rand(o, &bad);
            if (bad) return 0;
            if (l11_sra(rnd, 6) & 3) {
                for (uint32_t k = 0; k < 4; k++) l11_w32(o, S_700038A0 + 4 * k, l11_u32(o, e + 0xB0 + 4 * k));
                uint32_t a4 = L11_ADD(l11_u32(o, S_700038A4), 0x40A00000u); /* 5.0 */
                for (uint32_t k = 0; k < 4; k++) l11_w32(o, S_700038B0 + 4 * k, l11_u32(o, e + 0xB0 + 4 * k));
                l11_w32(o, S_700038A4, a4);
                l11_w32(o, S_700038AC, F_ONE);
                l11_w32(o, S_700038B4, L11_ADD(l11_u32(o, S_700038B4), 0x41F00000u)); /* 30.0 */
                l11_w32(o, S_700038BC, F_ONE);
                if (l11_c_0019A570(o, S_700038A0, S_700038B0, 6, 0, &r)) return 0;
                if (r == 0) {
                    l11_w32(o, S_700038A0, 0);
                    l11_w32(o, S_700038A4, 0x41780000u);
                    l11_w32(o, S_700038A8, 0x41200000u);
                    l11_w32(o, S_700038AC, 0x3F800000u);
                    l11_c_001B2B10(o, e, S_700038A0, S_700038A0);
                    l11_c_001028B8(o, S_700038A0, S_700038A0, e + 0xB0);
                    if (l11_c_0019AD00(o, e, S_700038A0, 7, &r)) return 0;
                    if (r == 0) {
                        l11_w8(o, e + 5, 3);
                        l11_w8(o, e + 6, 0);
                        return 2;
                    }
                }
            }
        }
        if (l11_c_001B3580(o, e, pt, &speed)) return 0;
        l11_w32(o, d + 0x44, speed);
        return 1;
    }
    uint32_t low = L11_SUB(l11_u32(o, e + 0xB4), 0x41A00000u); /* 20.0 */
    if (L11_LT(low, l11_u32(o, S_700038D0))) {
        if ((int8_t)l11_u8(o, d + 0x7A) == 0 && ((int8_t)l11_u8(o, d + 0x74) & 1)) {
            if (l11_c_001B37D0(o, e, 0x41200000u, 0x41A00000u, &speed)) return 0;
            l11_w32(o, d + 0x44, speed);
            return 1;
        }
        return 0;
    }
    if (l11_c_001B37D0(o, e, 0x41200000u, 0x41A00000u, &speed)) return 0;
    l11_w32(o, d + 0x44, speed);
    return 1;
}

/* ------------------------------------------------------------------------
 * 001469B0 (e, d): the alert. d +0x64 set: d +0x70 counts frames while d
 * +0x7F is set or d +0x5C > 100 (at 0xB4 d +0x64 = +0x70 = 0), else +0x70
 * = 0. d +0x64 clear and d +0x7F clear: e +0xA bit 0 -> d +0x64 = 300, +0x70
 * = 0; else when 001B3F10(e, wrap(atan2 of the player's +0x14C actor's
 * x / z (0011E620) - pi/2), 18) is set +0x70 counts to the threshold
 * 0x2753F0[D_0081050C & 3] and then d +0x64 = 300, +0x70 = 0; unset ->
 * +0x70 = 0. */
void l11_001469B0(L11 *o, uint32_t e, uint32_t d, uint32_t sp)
{
    if (l11_s16(o, d + 0x64) != 0) {
        if ((int8_t)l11_u8(o, d + 0x7F) == 0 && L11_LE(l11_u32(o, d + 0x5C), 0x42C80000u)) { /* 100.0 */
            l11_w8(o, d + 0x70, 0);
        } else {
            uint32_t c = (l11_u8(o, d + 0x70) + 1u) & 0xFFu;
            l11_w8(o, d + 0x70, c);
            if (c >= 0xB4) {
                l11_w16(o, d + 0x64, 0);
                l11_w8(o, d + 0x70, 0);
            }
        }
        return;
    }
    if ((int8_t)l11_u8(o, d + 0x7F) != 0) return;
    if (l11_u8(o, e + 0xA) & 1u) {
        l11_w16(o, d + 0x64, 0x12C);
        l11_w8(o, d + 0x70, 0);
        return;
    }
    uint32_t v0 = l11_u32(o, e + 0x14C);
    uint32_t z = l11_u32(o, v0 + 0xB8);
    uint32_t a = 0;
    if (l11_c_0011E620(o, l11_u32(o, v0 + 0xB0), z, &a)) return;
    if (l11_c_001B1470(o, L11_SUB(a, F_HALF_PI), &a)) return;
    int32_t hit = l11_001B3F10(o, e, a, 0x41900000u, sp - 0x30); /* 18.0 */
    if (l11_failed(o)) return;
    if (hit != 0) {
        uint32_t c = (l11_u8(o, d + 0x70) + 1u) & 0xFFu;
        l11_w8(o, d + 0x70, c);
        if ((int32_t)c < (int32_t)l11_u8(o, 0x002753F0u + (l11_u8(o, 0x0081050Cu) & 3u))) return;
        l11_w16(o, d + 0x64, 0x12C);
        l11_w8(o, d + 0x70, 0);
    } else {
        l11_w8(o, d + 0x70, 0);
    }
}

/* ------------------------------------------------------------------------
 * 00142330 (e, d): behaviour 0 (decomp word assembly). +6 0: +6 = 1, d
 * +0x50 = d +0x4C = 0, clip 1 or 2 (00122BB8() >> 19 & 7 zero or not;
 * 001C67E0(e, clip, 5.0, 0.0)). +6 1: when the clip word (e +0x2C &
 * 0xFFFF7FFF) is 1 and e +0x3C is exactly 80, 001FBD50(e, 0x833, 0, 300);
 * then 001469B0(e, d); then by d +0x7F (bit 7) and d +0x30 (bit 0x1000),
 * d +0x64, d +0x78 and d +0x60 the next behaviour: +6 = 0 with bit 7 set
 * and the clip ended; +5 = 1 / +6 = 0 for an ended clip, an alert or a
 * zone; +5 = 5 / +6 = 0 when d +0x78 is set and d +0x60 clear. */
void l11_00142330(L11 *o, uint32_t e, uint32_t d, uint32_t sp)
{
    int bad = 0;
    uint32_t st = l11_u8(o, e + 6);
    if (st == 1) {
        int32_t clip = l11_s16(o, e + 0x2C) & (int32_t)0xFFFF7FFF;
        if (clip == 1 && L11_EQ(0x42A00000u, l11_u32(o, e + 0x3C))) { /* 80.0 */
            int32_t r = 0;
            if (l11_c_001FBD50(o, e, 0x833, 0, F_300, &r)) return;
        }
        l11_001469B0(o, e, d, sp - 0x30);
        if (l11_failed(o)) return;
        int32_t zone = (int8_t)l11_u8(o, d + 0x7F);
        if (zone & 0x80) {
            if (l11_u32(o, d + 0x30) & 0x1000u) {
                l11_w8(o, e + 6, 0);
                return;
            }
        } else {
            int go = (l11_u32(o, d + 0x30) & 0x1000u) != 0;
            if (!go && l11_s16(o, d + 0x64) != 0) go = 1;
            if (!go && zone != 0) go = 1;
            if (go) {
                l11_w8(o, e + 5, 1);
                l11_w8(o, e + 6, 0);
                return;
            }
        }
        if ((int8_t)l11_u8(o, d + 0x78) == 0) return;
        if (l11_s16(o, d + 0x60) != 0) return;
        l11_w8(o, e + 5, 5);
        l11_w8(o, e + 6, 0);
        return;
    }
    if (st != 0) return;
    l11_w8(o, e + 6, st + 1);
    l11_w32(o, d + 0x50, 0);
    l11_w32(o, d + 0x4C, 0);
    uint32_t r = l11_rand(o, &bad);
    if (bad) return;
    if ((l11_sra(r, 19) & 7) == 0)
        l11_c_001C67E0(o, e, 1, 0x40A00000u, F_ZERO); /* 5.0 */
    else
        l11_c_001C67E0(o, e, 2, 0x40A00000u, F_ZERO);
}

/* ------------------------------------------------------------------------
 * 001424C0 (e, d): behaviour 1 (decomp NEARMISS). The turn rate is 2
 * degrees a frame (4 while d +0x7F is set and the move is re-picked); the
 * tail always turns e +0xC4 toward d +0x44 (001B12B0).
 * +6 0: +6 = 1, d +0x77 = 0, +0x75 = 2, +0x4C = 0.4, the timer d +0x20 =
 * ((rand >> 3) & 0x7F) + 0xB4, a random heading d +0x44 (001B1470 of 2pi *
 * ((rand >> 15) & 0xFF) / 255), clip 0 (001C67E0(e, 0, 5.0, 0.0)).
 * +6 1: r = 00146110(e, d): 2 -> the tail; nonzero -> +6 += 1 and, when
 * |wrap(d +0x44 - wrap(pi + e +0xC4))| is within pi/8 (00128350 /
 * 001000E0, the soft-double compare), d +0x77 counts and every third time a
 * new random heading ((rand >> 12) & 0xFF); then the timer. Zero ->
 * 001469B0(e, d) and: alerted (d +0x64): without d +0x78 / d +0x60 and
 * beyond 40 (d +0x5C), 5 of 8 draws ((rand >> 17) & 7 >= 3) set d +0x60 =
 * (rand >> 11) & 0x7F and continue below, else behaviour 5; d +0x71 0 and
 * within 19 -> behaviour 6; not within 50 -> behaviour 2 with d +0x62 =
 * ((rand >> 15) & 0xFF) + 300; else beyond 20 (the decomp's NEARMISS text
 * has "within") the heading toward the player (001B1240), and the timer. Not alerted: e +0xB0 / d +0x10 at
 * 0x700038A0 / 0x700038B0 (y zeroed); zone d +0x7F set: rate 4, negative ->
 * behaviour 0, else beyond 90 (001B13F0) -> behaviour 2; d +0x76 set: on
 * frames with (0x70003B68 + 0x70003B8A) & 0x3F zero, within 30 clears it,
 * and the heading toward d +0x10 / +0x18; else on those frames beyond 100
 * sets it. The timer: nonzero -> -1 and the tail; zero -> behaviour 0 when
 * the zone is clear or negative.
 * +6 2: once d +0x44 equals e +0xC4 exactly, +6 = 1. */
void l11_001424C0(L11 *o, uint32_t e, uint32_t d, uint32_t sp)
{
    int bad = 0;
    uint32_t rate = 0x3D0EFA35u; /* 0.034906585 */
    uint32_t st = l11_u8(o, e + 6);
    int32_t r = 0;
    uint32_t f = 0;
    switch (st) {
    case 0: {
        l11_w8(o, e + 6, st + 1);
        l11_w8(o, d + 0x77, 0);
        l11_w8(o, d + 0x75, 2);
        l11_w32(o, d + 0x4C, 0x3ECCCCCDu); /* 0.4 */
        uint32_t rnd = l11_rand(o, &bad);
        if (bad) return;
        l11_w32(o, d + 0x20, (((rnd >> 3) & 0x7Fu) + 0xB4u) & 0xFFFFu);
        rnd = l11_rand(o, &bad);
        if (bad) return;
        uint32_t a = L11_DIV(L11_MUL(F_TWO_PI, L11_CVT_S_W((uint32_t)l11_sra(rnd, 15) & 0xFFu)), F_255);
        if (l11_c_001B1470(o, a, &f)) return;
        l11_w32(o, d + 0x44, f);
        l11_c_001C67E0(o, e, 0, 0x40A00000u, F_ZERO); /* 5.0 */
        break;
    }
    case 1: {
        r = l11_00146110(o, e, d, sp - 0x40);
        if (l11_failed(o)) return;
        if (r == 2) break;
        if (r != 0) {
            l11_w8(o, e + 6, l11_u8(o, e + 6) + 1);
            uint32_t a = 0;
            if (l11_c_001B1470(o, L11_ADD(F_PI, l11_u32(o, e + 0xC4)), &a)) return;
            if (l11_c_001B1470(o, L11_SUB(l11_u32(o, d + 0x44), a), &a)) return;
            if (l11_c_0011DF78(o, a, &a)) return;
            uint64_t q = 0;
            if (l11_c_00128350(o, a, &q)) return;
            if (l11_c_001000E0(o, q, UINT64_C(0x3FD921FB60000000), &r)) return; /* (double)(float)(pi / 8) */
            if (r != 0) {
                int32_t cnt = (int8_t)(l11_u8(o, d + 0x77) + 1u);
                l11_w8(o, d + 0x77, (uint32_t)cnt);
                if (cnt >= 3) {
                    l11_w8(o, d + 0x77, 0);
                    uint32_t rnd = l11_rand(o, &bad);
                    if (bad) return;
                    uint32_t b = L11_DIV(L11_CVT_S_W((uint32_t)l11_sra(rnd, 12) & 0xFFu), F_255);
                    if (l11_c_001B1470(o, L11_MUL(F_TWO_PI, b), &f)) return;
                    l11_w32(o, d + 0x44, f);
                }
            }
            goto tick;
        }
        l11_001469B0(o, e, d, sp - 0x40);
        if (l11_failed(o)) return;
        if (l11_s16(o, d + 0x64) != 0) {
            if ((int8_t)l11_u8(o, d + 0x78) == 0 && l11_s16(o, d + 0x60) == 0) {
                if (!L11_LE(l11_u32(o, d + 0x5C), 0x42200000u)) { /* 40.0 */
                    uint32_t rnd = l11_rand(o, &bad);
                    if (bad) return;
                    if ((l11_sra(rnd, 0x11) & 7) >= 3) {
                        rnd = l11_rand(o, &bad);
                        if (bad) return;
                        l11_w16(o, d + 0x60, (uint32_t)(l11_sra(rnd, 0xB) & 0x7F));
                        goto mid;
                    }
                }
                l11_w8(o, e + 5, 5);
                l11_w8(o, e + 6, 0);
                break;
            }
        mid:;
            uint32_t calm = l11_u8(o, d + 0x71);
            uint32_t dist = calm == 0 ? l11_u32(o, d + 0x5C) : 0;
            if (calm == 0 && L11_LE(dist, 0x41980000u)) { /* 19.0 */
                l11_w8(o, e + 5, 6);
                l11_w8(o, e + 6, 0);
            } else if (dist = l11_u32(o, d + 0x5C), !L11_LT(dist, 0x42480000u)) { /* 50.0 */
                l11_w8(o, e + 5, 2);
                l11_w8(o, e + 6, 0);
                uint32_t rnd = l11_rand(o, &bad);
                if (bad) return;
                l11_w16(o, d + 0x62, (uint32_t)((l11_sra(rnd, 0xF) & 0xFF) + 0x12C));
            } else {
                if (!L11_LE(dist, 0x41A00000u)) { /* beyond 20 */
                    uint32_t px = l11_u32(o, 0x00810360u);
                    if (l11_c_001B1240(o, e + 0xB0, px, l11_u32(o, 0x00810368u), &f)) return;
                    l11_w32(o, d + 0x44, f);
                }
                goto tick;
            }
            break;
        }
        l11_c_00102948(o, S_700038A0, e + 0xB0);
        l11_c_00102948(o, S_700038B0, d + 0x10);
        l11_w32(o, S_700038B4, 0);
        l11_w32(o, S_700038A4, 0);
        {
            int32_t zone = (int8_t)l11_u8(o, d + 0x7F);
            if (zone != 0) {
                rate = 0x3D8EFA35u; /* 0.06981317 */
                if (zone & 0x80) {
                    l11_w8(o, e + 5, 0);
                    l11_w8(o, e + 6, 0);
                } else {
                    if (l11_c_001B13F0(o, S_700038A0, S_700038B0, 0x42B40000u, &r)) return; /* 90.0 */
                    if (r == 0) {
                        l11_w8(o, e + 5, 2);
                        l11_w8(o, e + 6, 0);
                    }
                }
            } else if ((int8_t)l11_u8(o, d + 0x76) != 0) {
                int32_t lag = l11_s16(o, 0x70003B8Au);
                if (!((l11_s32(o, 0x70003B68u) + lag) & 0x3F)) {
                    if (l11_c_001B13F0(o, S_700038A0, S_700038B0, 0x41F00000u, &r)) return; /* 30.0 */
                    if (r != 0) l11_w8(o, d + 0x76, 0);
                    uint32_t tx = l11_u32(o, d + 0x10);
                    if (l11_c_001B1240(o, e + 0xB0, tx, l11_u32(o, d + 0x18), &f)) return;
                    l11_w32(o, d + 0x44, f);
                }
            } else {
                int32_t lag = l11_s16(o, 0x70003B8Au);
                if (!((l11_s32(o, 0x70003B68u) + lag) & 0x3F)) {
                    if (l11_c_001B13F0(o, S_700038A0, S_700038B0, 0x42C80000u, &r)) return; /* 100.0 */
                    if (r == 0) l11_w8(o, d + 0x76, 1);
                }
            }
        }
    tick:;
        int32_t timer = l11_s32(o, d + 0x20);
        if (timer != 0) {
            l11_w32(o, d + 0x20, (uint32_t)(timer - 1));
        } else {
            int32_t zone = (int8_t)l11_u8(o, d + 0x7F);
            if (zone == 0 || (zone & 0x80)) {
                l11_w8(o, e + 5, 0);
                l11_w8(o, e + 6, 0);
            }
        }
        break;
    }
    case 2:
        f = l11_u32(o, d + 0x44);
        if (L11_EQ(f, l11_u32(o, e + 0xC4))) l11_w8(o, e + 6, 1);
        break;
    default:
        break;
    }
    if (l11_failed(o)) return;
    uint32_t want = l11_u32(o, d + 0x44);
    if (l11_c_001B12B0(o, want, l11_u32(o, e + 0xC4), rate, &f)) return;
    l11_w32(o, e + 0xC4, f);
}

/* The random turn step a + b * ((rand >> 14) & 0xFF) / 255 at 0x70003A20
 * and e +0xC4 turned toward d +0x44 by it (001B12B0). */
static void l11_turn_random(L11 *o, uint32_t e, uint32_t d, uint32_t base, int *bad)
{
    uint32_t rnd = l11_rand(o, bad);
    if (*bad) return;
    uint32_t t = L11_DIV(L11_CVT_S_W((uint32_t)l11_sra(rnd, 14) & 0xFFu), F_255);
    l11_w32(o, S_70003A20, L11_ADD(base, L11_MUL(0x3D8EFA35u, t))); /* 0.06981317 */
    uint32_t cur = l11_u32(o, e + 0xC4);
    uint32_t step = l11_u32(o, S_70003A20);
    uint32_t f = 0;
    if (l11_c_001B12B0(o, l11_u32(o, d + 0x44), cur, step, &f)) { *bad = 1; return; }
    l11_w32(o, e + 0xC4, f);
}

/* State 4 entry: +6 = 4, d +0x40 = 2.0, d +0x50 = -0.05, clip 6. */
static void l11_stop_run(L11 *o, uint32_t e, uint32_t d)
{
    l11_w8(o, e + 6, 4);
    l11_w32(o, d + 0x40, 0x40000000u);
    l11_w32(o, d + 0x50, 0xBD4CCCCDu);
    l11_c_001C67E0(o, e, 6, F_ZERO, F_ZERO);
}

/* ------------------------------------------------------------------------
 * 001429D0 (e, d): behaviour 2 (the chase; decomp NEARMISS 99.94%). +6 0:
 * +6 = 1, d +0x75 = 0, d +0x4C = d +0x50 = 0, d +0x44 = e +0xC4, clip 4. +6
 * 1: speed 1 once e +0x3C <= 14; at the clip's end +6 = 2, d +0x24 = d
 * +0x28 = 0, speed 1.15, clip 5. +6 2: 001464B0(e, d): 0 -> with the zone
 * d +0x7F: d +0x28 counts down or re-picks ((rand >> 9) & 3, the heading to
 * d +0x10 / +0x18 and a random turn 2.5 .. 6.5 degrees); within 45 of d
 * +0x10 (y zeroed) or a negative zone -> the stop (state 4). Else, near the
 * player (within 40 and 001B1560(e, the player, pi/4)): within 15, on
 * D_0081050C 3 facing within pi/4 of the player's yaw (0x810374), a free
 * forward probe (0, 15, 35.5, 1; 0019AD00(.., 7) == 0) -> behaviour 7;
 * 35 or more and facing away -> behaviour 8; 27 or less on D_0081050C 0
 * clears d +0x62. d +0x62 clear -> the stop. Zone clear, beyond 23,
 * facing exactly the player (001B1240) and 00146AF0(e, d, 13) set: d +0x24
 * = ((rand >> 17) & 0xF) + 10, d +0x28 = 0 and a quarter turn by
 * 001B39F0(e, (0, 0, 25, 1) through the frame, 0x700038B0). d +0x24 counts
 * down or re-aims at the player; d +0x28 counts down, or within 20 re-picks
 * (rand >> 9) & 0x7F, else a random turn 1 .. 5 degrees. 1 -> +6 = 3, d
 * +0x20 = 0. 2 -> +6 = 5, +7 = 0, d +0x7A = 1, d +0x48 = 2.1, 001FBD50(e,
 * 0x831, 0, 300), clip 7 (0, 17). 3 -> the stop. +6 3: d +0x20 counts; at
 * 300 the stop with d +0x50 = -0.04; else a 4-degree turn by d +0x79
 * (0x70003A20 = -/+ pi/8 kept), two side probes (0, 3, 33.5, 1) through
 * the turned frames (0019AD00(.., 6)); both free -> +6 = 2, d +0x20 = 0.
 * +6 4: a negative speed is zeroed; at the clip's end d +0x40 = 1 and
 * behaviour 6 when 001B1560(e, the player's position 0x810350, pi/4) and
 * 001B13F0(the player, e +0xB0, 20) are set, else behaviour 0 with speed
 * zero. +6 5: d +0x7A = 1; +7 0: when d +0x48 <= 0, +7 += 1, d +0x40 = 1,
 * clip 9; else clip 8 at the end of another clip. +7 1: d +0x40 = 0 once e
 * +0x3C <= 32; a hit (d +0x74 bit 1) -> +6 = 2, +7 = 0, d +0x40 = 1,
 * 001FBD50(e, 0x832, 0, 300), clip 5 (5, 20). */
void l11_001429D0(L11 *o, uint32_t e, uint32_t d, uint32_t sp)
{
    int bad = 0;
    int32_t r = 0;
    uint32_t f = 0;
    (void)sp;
    switch (l11_u8(o, e + 6)) {
    case 0:
        l11_w8(o, e + 6, l11_u8(o, e + 6) + 1);
        l11_w8(o, d + 0x75, 0);
        l11_w32(o, d + 0x4C, 0);
        l11_w32(o, d + 0x50, 0);
        l11_w32(o, d + 0x44, l11_u32(o, e + 0xC4));
        l11_c_001C67E0(o, e, 4, F_ZERO, F_ZERO);
        break;
    case 1:
        if (L11_LE(l11_u32(o, e + 0x3C), 0x41600000u)) l11_w32(o, d + 0x4C, F_ONE); /* 14.0 */
        if (l11_u32(o, d + 0x30) & 0x1000u) {
            l11_w8(o, e + 6, l11_u8(o, e + 6) + 1);
            l11_w32(o, d + 0x24, 0);
            l11_w32(o, d + 0x28, 0);
            l11_w32(o, d + 0x4C, 0x3F933333u); /* 1.15 */
            l11_c_001C67E0(o, e, 5, F_ZERO, F_ZERO);
        }
        break;
    case 2: {
        int32_t v = l11_001464B0(o, e, d);
        if (l11_failed(o)) return;
        switch (v) {
        case 0:
            if ((int8_t)l11_u8(o, d + 0x7F) != 0) {
                int32_t t = l11_s32(o, d + 0x28);
                if (t != 0) {
                    l11_w32(o, d + 0x28, (uint32_t)(t - 1));
                } else {
                    uint32_t rnd = l11_rand(o, &bad);
                    if (bad) return;
                    l11_w32(o, d + 0x28, (uint32_t)(l11_sra(rnd, 9) & 3));
                    uint32_t tx = l11_u32(o, d + 0x10);
                    if (l11_c_001B1240(o, e + 0xB0, tx, l11_u32(o, d + 0x18), &f)) return;
                    l11_w32(o, d + 0x44, f);
                    l11_turn_random(o, e, d, 0x3D32B8C3u, &bad); /* 0.043633234 */
                    if (bad) return;
                }
                l11_c_00102948(o, S_700038A0, e + 0xB0);
                l11_c_00102948(o, S_700038B0, d + 0x10);
                l11_w32(o, S_700038B4, 0);
                l11_w32(o, S_700038A4, 0);
                if (l11_c_001B13F0(o, S_700038A0, S_700038B0, 0x42340000u, &r)) return; /* 45.0 */
                if (r != 0 || ((int8_t)l11_u8(o, d + 0x7F) & 0x80)) {
                    l11_w8(o, e + 6, 4);
                    l11_w32(o, d + 0x40, 0x40000000u);
                    l11_w32(o, d + 0x50, 0xBD4CCCCDu);
                    l11_c_001C67E0(o, e, 6, F_ZERO, F_ZERO);
                }
                break;
            }
            if (L11_LE(l11_u32(o, d + 0x5C), 0x42200000u)) { /* 40.0 */
                if (l11_c_001B1560(o, e, 0x00810360u, F_QPI, &r)) return;
                if (r != 0) {
                    uint32_t dist = l11_u32(o, d + 0x5C);
                    if (L11_LE(dist, 0x41700000u)) { /* 15.0 */
                        if (l11_u8(o, 0x0081050Cu) == 3) {
                            uint32_t mine = l11_u32(o, e + 0xC4);
                            if (l11_c_001B1470(o, L11_SUB(l11_u32(o, 0x00810374u), mine), &f)) return;
                            if (l11_c_0011DF78(o, f, &f)) return;
                            if (L11_LE(f, F_QPI)) {
                                l11_w32(o, S_700038A0, 0);
                                l11_w32(o, S_700038A4, 0x41700000u);
                                l11_w32(o, S_700038A8, 0x420E0000u);
                                l11_w32(o, S_700038AC, 0x3F800000u);
                                l11_c_001B2B10(o, e, S_700038A0, S_700038A0);
                                l11_c_001028B8(o, S_700038A0, e + 0xB0, S_700038A0);
                                if (l11_c_0019AD00(o, e, S_700038A0, 7, &r)) return;
                                if (r == 0) {
                                    l11_w8(o, e + 5, 7);
                                    l11_w8(o, e + 6, 0);
                                    break;
                                }
                            }
                        }
                    } else if (!L11_LT(dist, 0x420C0000u)) { /* 35.0 */
                        uint32_t mine = l11_u32(o, e + 0xC4);
                        if (l11_c_001B1470(o, L11_SUB(l11_u32(o, 0x00810374u), mine), &f)) return;
                        if (l11_c_0011DF78(o, f, &f)) return;
                        if (!L11_LE(f, F_QPI)) {
                            l11_w8(o, e + 5, 8);
                            l11_w8(o, e + 6, 0);
                            break;
                        }
                    } else if (l11_u8(o, 0x0081050Cu) == 0 && L11_LE(dist, 0x41D80000u)) { /* 27 */
                        l11_w16(o, d + 0x62, 0);
                    }
                }
            }
            if (l11_s16(o, d + 0x62) == 0) {
                l11_stop_run(o, e, d);
                break;
            }
            if ((int8_t)l11_u8(o, d + 0x7F) == 0 && !L11_LE(l11_u32(o, d + 0x5C), 0x41B80000u)) { /* 23.0 */
                uint32_t px = l11_u32(o, 0x00810360u);
                if (l11_c_001B1240(o, e + 0xB0, px, l11_u32(o, 0x00810368u), &f)) return;
                if (L11_EQ(l11_u32(o, e + 0xC4), f)) {
                    if (l11_c_00146AF0(o, e, d, 0x41500000u, &r)) return; /* 13.0 */
                    if (r != 0) {
                        uint32_t rnd = l11_rand(o, &bad);
                        if (bad) return;
                        l11_w32(o, d + 0x24, (uint32_t)((l11_sra(rnd, 17) & 0xF) + 10));
                        l11_w32(o, d + 0x28, 0);
                        l11_w32(o, S_700038A0, 0);
                        l11_w32(o, S_700038A4, 0);
                        l11_w32(o, S_700038A8, 0x41C80000u);
                        l11_w32(o, S_700038AC, 0x3F800000u);
                        l11_c_001B2B10(o, e, S_700038A0, S_700038A0);
                        if (l11_c_001B39F0(o, e, S_700038A0, S_700038B0, &r)) return;
                        uint32_t h = l11_u32(o, d + 0x44);
                        h = r != 0 ? L11_ADD(F_HALF_PI, h) : L11_SUB(h, F_HALF_PI);
                        if (l11_c_001B1470(o, h, &f)) return;
                        l11_w32(o, d + 0x44, f);
                    }
                }
            }
            {
                int32_t t = l11_s32(o, d + 0x24);
                if (t != 0) {
                    l11_w32(o, d + 0x24, (uint32_t)(t - 1));
                } else {
                    uint32_t px = l11_u32(o, 0x00810360u);
                    if (l11_c_001B1240(o, e + 0xB0, px, l11_u32(o, 0x00810368u), &f)) return;
                    l11_w32(o, d + 0x44, f);
                }
                t = l11_s32(o, d + 0x28);
                if (t != 0) {
                    l11_w32(o, d + 0x28, (uint32_t)(t - 1));
                } else if (L11_LT(l11_u32(o, d + 0x5C), 0x41A00000u)) { /* 20.0 */
                    uint32_t rnd = l11_rand(o, &bad);
                    if (bad) return;
                    l11_w32(o, d + 0x28, (uint32_t)(l11_sra(rnd, 9) & 0x7F));
                } else {
                    l11_turn_random(o, e, d, 0x3C8EFA35u, &bad); /* 0.017453292 */
                    if (bad) return;
                }
            }
            break;
        case 1:
            l11_w8(o, e + 6, 3);
            l11_w32(o, d + 0x20, 0);
            break;
        case 2:
            l11_w8(o, e + 6, 5);
            l11_w8(o, e + 7, 0);
            l11_w8(o, d + 0x7A, 1);
            l11_w32(o, d + 0x48, 0x40066666u); /* 2.1 */
            if (l11_c_001FBD50(o, e, 0x831, 0, F_300, &r)) return;
            l11_c_001C67E0(o, e, 7, F_ZERO, 0x41880000u); /* 17.0 */
            break;
        case 3:
            l11_stop_run(o, e, d);
            break;
        default:
            break;
        }
        break;
    }
    case 3: {
        uint32_t t = l11_u32(o, d + 0x20) + 1u;
        l11_w32(o, d + 0x20, t);
        if (t >= 300u) {
            l11_w8(o, e + 6, 4);
            l11_w32(o, d + 0x50, 0xBD23D70Au); /* -0.04 */
            l11_c_001C67E0(o, e, 6, F_ZERO, F_ZERO);
            break;
        }
        if ((int8_t)l11_u8(o, d + 0x79) != 0) {
            l11_w32(o, S_70003A20, 0xBEC90FDBu);
            if (l11_c_001B1470(o, L11_ADD(0x3D8EFA35u, l11_u32(o, e + 0xC4)), &f)) return;
        } else {
            l11_w32(o, S_70003A20, 0x3EC90FDBu);
            if (l11_c_001B1470(o, L11_SUB(l11_u32(o, e + 0xC4), 0x3D8EFA35u), &f)) return;
        }
        l11_w32(o, e + 0xC4, f);
        l11_c_001029C0(o, S_700036A0);
        l11_c_00102BB0(o, S_700036A0, S_700036A0, l11_u32(o, e + 0xC4));
        l11_w32(o, S_700038A0, 0);
        l11_w32(o, S_700038A4, 0x40400000u);
        l11_w32(o, S_700038A8, 0x42060000u);
        l11_w32(o, S_700038AC, 0x3F800000u);
        l11_c_001026A0(o, S_700038B0, S_700036A0, S_700038A0);
        l11_c_001028B8(o, S_700038B0, S_700038B0, e + 0xB0);
        if (l11_c_0019AD00(o, e, S_700038B0, 6, &r)) return;
        if (r == 0) {
            l11_c_00102BB0(o, S_700036A0, S_700036A0, l11_u32(o, S_70003A20));
            l11_c_001026A0(o, S_700038B0, S_700036A0, S_700038A0);
            l11_c_001028B8(o, S_700038B0, S_700038B0, e + 0xB0);
            if (l11_c_0019AD00(o, e, S_700038B0, 6, &r)) return;
            if (r == 0) {
                l11_w8(o, e + 6, 2);
                l11_w32(o, d + 0x20, 0);
            }
        }
        break;
    }
    case 4:
        if (L11_LT(l11_u32(o, d + 0x4C), F_ZERO)) {
            l11_w32(o, d + 0x4C, 0);
            l11_w32(o, d + 0x50, 0);
        }
        if (l11_u32(o, d + 0x30) & 0x1000u) {
            l11_w32(o, d + 0x40, F_ONE);
            if (l11_c_001B1560(o, e, 0x00810350u, F_QPI, &r)) return;
            if (r != 0) {
                if (l11_c_001B13F0(o, 0x00810360u, e + 0xB0, 0x41A00000u, &r)) return; /* 20.0 */
                if (r != 0) {
                    l11_w8(o, e + 5, 6);
                    l11_w8(o, e + 6, 0);
                    break;
                }
            }
            l11_w8(o, e + 5, 0);
            l11_w8(o, e + 6, 0);
            l11_w32(o, d + 0x4C, 0);
            l11_w32(o, d + 0x50, 0);
        }
        break;
    case 5:
        l11_w8(o, d + 0x7A, 1);
        switch (l11_u8(o, e + 7)) {
        case 0:
            if (L11_LE(l11_u32(o, d + 0x48), F_ZERO)) {
                l11_w8(o, e + 7, 1); /* +7 was 0 here */
                l11_w32(o, d + 0x40, F_ONE);
                l11_c_001C67E0(o, e, 9, F_ZERO, F_ZERO);
                break;
            }
            if ((l11_s16(o, e + 0x2C) & (int32_t)0xFFFF7FFF) != 8 && (l11_u32(o, d + 0x30) & 0x1000u))
                l11_c_001C67E0(o, e, 8, F_ZERO, F_ZERO);
            break;
        case 1:
            if (L11_LE(l11_u32(o, e + 0x3C), 0x42000000u)) l11_w32(o, d + 0x40, 0); /* 32.0 */
            if ((int8_t)l11_u8(o, d + 0x74) & 2) {
                l11_w8(o, e + 6, 2);
                l11_w8(o, e + 7, 0);
                l11_w32(o, d + 0x40, F_ONE);
                if (l11_c_001FBD50(o, e, 0x832, 0, F_300, &r)) return;
                l11_c_001C67E0(o, e, 5, 0x40A00000u, 0x41A00000u); /* 5.0, 20.0 */
            }
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------
 * 001459A0 (e, d): the collision probes (decomp NEARMISS). d +0x74 = 0. A
 * side probe (0, 3, +-3 by the sign of d +0x4C, 1) through e's frame plus
 * +0xB0 (001B32F0(e, it, pi/4)) sets bit 0 and skips to the tail. Else by
 * the vertical speed d +0x48: rising: up to four joint points (0x245960 or
 * 0x2459B0 by D_00810700 == 0xD, +16 per joint) turned by e's yaw
 * (001029C0 / 00102BB0 / 001026A0) plus +0xB0 through 001B32F0 -> bit 0;
 * falling below -0.1: two segments (0, 0, -3) -> (0, 0, 3) and (-3, 0, 0)
 * -> (3, 0, 0) turned and moved, 001B3390 both ways on the first, and
 * (bit 0 still clear) both ways on the second -> bit 0. Tail: bit 0 sets e
 * +0x52 bit 0. Then falling: five joint points against the floor segment
 * (0, -15, 0) (001B3440) -> d +0x48 = 0 and bit 1; none, with e +4 == 1 and
 * d +0x7A clear: the ground below (001B2F70, else y - 10) more than 10
 * down and d +0x48 < -0.5 -> behaviour 4. Rising: the head probe from e's
 * position (y + 20.002) up 15 (0019AB20(e, .., 0x80000007)) -> d +0x48 = 0
 * and bit 2. */
void l11_001459A0(L11 *o, uint32_t e, uint32_t d)
{
    int32_t r = 0;
    l11_w8(o, d + 0x74, 0);
    l11_w32(o, S_700038A0, 0);
    l11_w32(o, S_700038A4, 0x40400000u);
    l11_w32(o, S_700038A8, 0x40400000u);
    l11_w32(o, S_700038AC, 0x3F800000u);
    if (L11_LT(l11_u32(o, d + 0x4C), F_ZERO)) l11_w32(o, S_700038A8, 0xC0400000u);
    l11_c_001B2B10(o, e, S_700038A0, S_700038A0);
    l11_c_001028B8(o, S_700038A0, e + 0xB0, S_700038A0);
    if (l11_c_001B32F0(o, e, S_700038A0, F_QPI, &r)) return;
    if (r != 0) {
        l11_w8(o, d + 0x74, l11_u8(o, d + 0x74) | 1u);
        goto tail;
    }
    {
        uint32_t lean = l11_u32(o, d + 0x48);
        if (L11_LT(F_ZERO, lean)) {
            l11_c_001029C0(o, S_700036A0);
            l11_c_00102BB0(o, S_700036A0, S_700036A0, l11_u32(o, e + 0xC4));
            for (uint32_t i = 0; i < 4; i++) {
                if (l11_failed(o)) return;
                if (l11_u8(o, 0x00810700u) == 0xDu)
                    l11_c_001026A0(o, S_700038A0, S_700036A0, 0x00245960u + (i + 1u) * 0x10u);
                else
                    l11_c_001026A0(o, S_700038A0, S_700036A0, 0x002459B0u + (i + 1u) * 0x10u);
                l11_c_001028B8(o, S_700038A0, e + 0xB0, S_700038A0);
                if (l11_c_001B32F0(o, e, S_700038A0, F_QPI, &r)) return;
                if (r != 0) {
                    l11_w8(o, d + 0x74, l11_u8(o, d + 0x74) | 1u);
                    goto tail;
                }
            }
        } else if (L11_LT(lean, 0xBDCCCCCDu)) { /* -0.1 */
            l11_w32(o, S_700038A0, 0);
            l11_w32(o, S_700038A4, 0);
            l11_w32(o, S_700038B0, 0);
            l11_w32(o, S_700038B4, 0);
            l11_w32(o, S_700038A8, 0xC0400000u);
            l11_w32(o, S_700038C0, 0xC0400000u);
            l11_w32(o, S_700038C0 + 4, 0);
            l11_w32(o, S_700038C0 + 8, 0);
            l11_w32(o, S_700038AC, 0x3F800000u);
            l11_w32(o, S_700038C0 + 12, 0x3F800000u);
            l11_w32(o, S_700038B8, 0x40400000u);
            l11_w32(o, S_700038D0, 0x40400000u);
            l11_w32(o, S_700038BC, 0x3F800000u);
            l11_w32(o, S_700038D0 + 4, 0);
            l11_w32(o, S_700038D0 + 8, 0);
            l11_w32(o, S_700038D0 + 12, 0x3F800000u);
            l11_c_001029C0(o, S_700036A0);
            l11_c_00102BB0(o, S_700036A0, S_700036A0, l11_u32(o, e + 0xC4));
            for (uint32_t k = 0; k < 4; k++) l11_c_001026A0(o, S_700038A0 + 0x10u * k, S_700036A0, S_700038A0 + 0x10u * k);
            for (uint32_t k = 0; k < 4; k++) l11_c_001028B8(o, S_700038A0 + 0x10u * k, e + 0xB0, S_700038A0 + 0x10u * k);
            if (l11_c_001B3390(o, e, S_700038A0, S_700038B0, F_QPI, &r)) return;
            if (r != 0) {
                l11_w8(o, d + 0x74, l11_u8(o, d + 0x74) | 1u);
            } else {
                if (l11_c_001B3390(o, e, S_700038B0, S_700038A0, F_QPI, &r)) return;
                if (r != 0) l11_w8(o, d + 0x74, l11_u8(o, d + 0x74) | 1u);
            }
            if ((int8_t)l11_u8(o, d + 0x74) == 0) {
                if (l11_c_001B3390(o, e, S_700038C0, S_700038D0, F_QPI, &r)) return;
                if (r != 0) {
                    l11_w8(o, d + 0x74, l11_u8(o, d + 0x74) | 1u);
                    goto tail;
                }
                if (l11_c_001B3390(o, e, S_700038D0, S_700038C0, F_QPI, &r)) return;
                if (r != 0) l11_w8(o, d + 0x74, l11_u8(o, d + 0x74) | 1u);
            }
        }
    }
tail:;
    if ((int8_t)l11_u8(o, d + 0x74) & 1) l11_w16(o, e + 0x52, l11_u16(o, e + 0x52) | 1u);
    uint32_t lean = l11_u32(o, d + 0x48);
    if (L11_LT(lean, F_ZERO)) {
        l11_c_001029C0(o, S_700036A0);
        l11_c_00102BB0(o, S_700036A0, S_700036A0, l11_u32(o, e + 0xC4));
        l11_w32(o, S_700038B0, 0);
        l11_w32(o, S_700038B4, 0xC1700000u);
        l11_w32(o, S_700038B8, 0);
        l11_w32(o, S_700038BC, 0x3F800000u);
        for (uint32_t i = 0; i < 5; i++) {
            if (l11_failed(o)) return;
            if (l11_u8(o, 0x00810700u) == 0xDu)
                l11_c_001026A0(o, S_700038A0, S_700036A0, 0x00245960u + i * 0x10u);
            else
                l11_c_001026A0(o, S_700038A0, S_700036A0, 0x002459B0u + i * 0x10u);
            l11_c_001028B8(o, S_700038A0, e + 0xB0, S_700038A0);
            if (l11_c_001B3440(o, e, S_700038A0, S_700038B0, F_QPI, &r)) return;
            if (r != 0) {
                l11_w32(o, d + 0x48, F_ZERO);
                l11_w8(o, d + 0x74, l11_u8(o, d + 0x74) | 2u);
                break;
            }
        }
        if (!((int8_t)l11_u8(o, d + 0x74) & 2) && l11_u8(o, e + 4) == 1 && (int8_t)l11_u8(o, d + 0x7A) == 0) {
            if (l11_c_001B2F70(o, e + 0xB0, S_700038A0, &r)) return;
            if (r == 0) l11_w32(o, S_700038A0, L11_SUB(l11_u32(o, e + 0xB4), 0x41200000u)); /* 10.0 */
            uint32_t y = l11_u32(o, e + 0xB4);
            uint32_t drop = L11_SUB(y, l11_u32(o, S_700038A0));
            if (!L11_LT(drop, 0x41200000u) && L11_LT(l11_u32(o, d + 0x48), 0xBF000000u)) { /* 10.0, -0.5 */
                l11_w8(o, e + 5, 4);
                l11_w8(o, e + 6, 0);
            }
        }
    } else if (L11_LT(F_ZERO, lean)) {
        l11_w32(o, S_700038A0, l11_u32(o, e + 0xB0));
        l11_w32(o, S_700038A4, l11_u32(o, e + 0xB4));
        l11_w32(o, S_700038A8, l11_u32(o, e + 0xB8));
        l11_w32(o, S_700038AC, l11_u32(o, e + 0xBC));
        l11_w32(o, S_700038B0, 0);
        l11_w32(o, S_700038B4, 0x41700000u);
        l11_w32(o, S_700038B8, 0);
        l11_w32(o, S_700038BC, 0x3F800000u);
        l11_w32(o, S_700038A4, L11_ADD(l11_u32(o, S_700038A4), 0x41A00419u)); /* 20.002 */
        if (l11_c_0019AB20(o, e, S_700038A0, S_700038B0, (int32_t)0x80000007u, &r)) return;
        if (r != 0) {
            l11_w32(o, d + 0x48, F_ZERO);
            l11_w8(o, d + 0x74, l11_u8(o, d + 0x74) | 4u);
        }
    }
}

/* ------------------------------------------------------------------------
 * 00141F00 (e, d): the spawn step (byte-matched). e +4 += 1, e +0 = 1, d
 * +0x40 = 1.0, e +0x30 = 0x2753D0; d and d +0x10 = e +0xB0 (00102948); e
 * +0x58 = D_0028A690; D_00810808 0xFF sets e +0xD bit 7. Bit 7: the
 * health e +0x34 = 500 (D_0081070A set) or 300, the scale +0x60..0x68 =
 * D_002753D8, +0x6C = 1.0, 001B10B0(e, 0x7E, 0x7F); else 350 / 200, scale
 * 1.5, 001B10B0(e, 0x7D, 0x7F); a zero result -> 001C63E0(e, 0). */
void l11_00141F00(L11 *o, uint32_t e, uint32_t d)
{
    int32_t r = 0;
    uint32_t n = (l11_u8(o, e + 4) + 1u) & 0xFFu;
    l11_w8(o, e + 4, n);
    l11_w8(o, e, 1);
    l11_w32(o, d + 0x40, F_ONE);
    l11_w32(o, e + 0x30, 0x002753D0u);
    l11_c_00102948(o, d, e + 0xB0);
    l11_c_00102948(o, d + 0x10, e + 0xB0);
    l11_w32(o, e + 0x58, l11_u32(o, 0x0028A690u));
    if (l11_u8(o, 0x00810808u) == 0xFFu) l11_w8(o, e + 0xD, l11_u8(o, e + 0xD) | 0x80u);
    if (l11_u8(o, e + 0xD) & 0x80u) {
        l11_w16(o, e + 0x34, l11_u8(o, 0x0081070Au) != 0 ? 0x1F4u : 0x12Cu);
        for (uint32_t k = 0; k < 3; k++) l11_w32(o, e + 0x60 + 4 * k, l11_u32(o, 0x002753D8u));
        l11_w32(o, e + 0x6C, F_ONE);
        if (l11_c_001B10B0(o, e, 0x7E, 0x7F, &r)) return;
    } else {
        l11_w16(o, e + 0x34, l11_u8(o, 0x0081070Au) != 0 ? 0x15Eu : 0xC8u);
        l11_w32(o, e + 0x60, 0x3FC00000u);
        l11_w32(o, e + 0x64, 0x3FC00000u);
        l11_w32(o, e + 0x68, 0x3FC00000u);
        l11_w32(o, e + 0x6C, F_ONE);
        if (l11_c_001B10B0(o, e, 0x7D, 0x7F, &r)) return;
    }
    if (r == 0) l11_c_001C63E0(o, e, 0);
}

/* ------------------------------------------------------------------------
 * 00142070 (e, d): the per-frame update (byte-matched). Only when
 * 001B2140(e) is set: d +0x7A = 0, e +0x5E = 1; the flat distance to the
 * player (001B15D0 of e +0xB0 and 0x810350 at 0x700038A0 / 0x700038B0, y
 * zeroed) in d +0x5C; e +0x9F = d +0x78; 001471E0; the behaviour e +5: 0
 * 00142330, 1 001424C0, 2 001429D0, 3 001434C0, 4 00143610, 5 001437E0, 6
 * 00143AF0, 7 00144C20, 8 00144040 (3, 4, 7 set d +0x7A first); e +0 = 3
 * (d +0x72) or 1; e +0x5D = 0x81 with 001B4810(e) when d +0x78 is clear,
 * else 1; clips 9 / 8 (the low byte of d +0x30) call 001FBD50(e, 0x827 /
 * 0x82C + ((rand >> 13) % 5), 0, 300); e +0xA = 0; 00146CE0, 00145880,
 * 001459A0; d +0x30 = 001C64F0(e, d +0x40); 00131ED0, 001C68C0, 001B17A0
 * and the +0x4C method. */
void l11_00142070(L11 *o, uint32_t e, uint32_t d, uint32_t sp)
{
    int bad = 0;
    int32_t r = 0;
    if (l11_c_001B2140(o, e, &r)) return;
    if (r == 0) return;
    l11_w8(o, d + 0x7A, 0);
    l11_w8(o, e + 0x5E, 1);
    l11_c_00102948(o, S_700038A0, e + 0xB0);
    l11_c_00102948(o, S_700038B0, 0x00810350u);
    l11_w32(o, S_700038B4, 0);
    l11_w32(o, S_700038A4, 0);
    uint32_t f = 0;
    if (l11_c_001B15D0(o, S_700038A0, S_700038B0, &f)) return;
    l11_w32(o, d + 0x5C, f);
    l11_w8(o, e + 0x9F, l11_u8(o, d + 0x78));
    l11_001471E0(o, e, d);
    switch (l11_u8(o, e + 5)) {
    case 0: l11_00142330(o, e, d, sp - 0x30); break;
    case 1: l11_001424C0(o, e, d, sp - 0x30); break;
    case 2: l11_001429D0(o, e, d, sp - 0x30); break;
    case 3: l11_w8(o, d + 0x7A, 1); l11_c_001434C0(o, e, d); break;
    case 4: l11_w8(o, d + 0x7A, 1); l11_c_00143610(o, e, d); break;
    case 5: l11_c_001437E0(o, e, d); break;
    case 6: l11_c_00143AF0(o, e, d); break;
    case 7: l11_w8(o, d + 0x7A, 1); l11_c_00144C20(o, e, d); break;
    case 8: l11_c_00144040(o, e, d); break;
    default: break;
    }
    if (l11_failed(o)) return;
    l11_w8(o, e, l11_u8(o, d + 0x72) != 0 ? 3u : 1u);
    if ((int8_t)l11_u8(o, d + 0x78) == 0) {
        l11_w8(o, e + 0x5D, 0x81);
        l11_c_001B4810(o, e);
    } else {
        l11_w8(o, e + 0x5D, 1);
    }
    uint32_t clip = l11_u8(o, d + 0x30);
    if (clip == 9 || clip == 8) {
        uint32_t rnd = l11_rand(o, &bad);
        if (bad) return;
        int32_t pick = l11_sra(rnd, 13) % 5 + (clip == 9 ? 0x827 : 0x82C);
        if (l11_c_001FBD50(o, e, pick, 0, F_300, &r)) return;
    }
    l11_w8(o, e + 0xA, 0);
    (void)l11_00146CE0(o, e, d);
    l11_00145880(o, e, d);
    l11_001459A0(o, e, d);
    if (l11_failed(o)) return;
    if (l11_c_001C64F0(o, e, l11_u32(o, d + 0x40), &r)) return;
    l11_w32(o, d + 0x30, (uint32_t)r);
    l11_c_00131ED0(o, e);
    l11_c_001C68C0(o, e);
    l11_c_001B17A0(o, e, &r);
    l11_method(o, e);
}

/* ------------------------------------------------------------------------
 * 00141D20 (e): the behaviour (decomp NEARMISS). d = e + 0x1F0. By the
 * pause byte 0x70003B8D: 1 -> when 001B2140(e) is set and e +4 nonzero,
 * the +0x4C method; 2 / 3 -> nothing; else e +0x52 = 0 and by e +4: 0
 * 00141F00, 1 00142070, 2 001450B0, 3 00145850 (and done); then the
 * countdowns d +0x64, +0x62 (halfwords), +0x73, +0x7D, +0x72, +0x71
 * (bytes), +0x60 (halfword) each -1 when nonzero; d +0x7E set ->
 * 001B5360(e) unless D_00810700 is 0xD; 001B0D80(e). */
int em_level11_port_00141D20(const EmLevel11PortHooks *h, uint32_t e, uint32_t sp, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    uint32_t d = e + 0x1F0;
    uint32_t st = l11_u8(&o, 0x70003B8Du);
    int32_t r = 0;
    if (st == 1) {
        if (l11_c_001B2140(&o, e, &r)) return l11_end(&o);
        if (r != 0 && l11_u8(&o, e + 4) != 0) l11_method(&o, e);
        return l11_end(&o);
    }
    if (st == 2 || st == 3) return l11_end(&o);
    l11_w16(&o, e + 0x52, 0);
    switch (l11_u8(&o, e + 4)) {
    case 3:
        l11_c_00145850(&o, e, d);
        return l11_end(&o);
    case 2: l11_c_001450B0(&o, e, d); break;
    case 1: l11_00142070(&o, e, d, sp - 0x30); break;
    case 0: l11_00141F00(&o, e, d); break;
    default: break;
    }
    static const struct { uint16_t at; uint8_t size; } counters[] = {
        {0x64, 2}, {0x62, 2}, {0x73, 1}, {0x7D, 1}, {0x72, 1}, {0x71, 1}, {0x60, 2}};
    for (unsigned k = 0; k < sizeof counters / sizeof counters[0]; k++) {
        uint32_t at = d + counters[k].at;
        if (counters[k].size == 2) {
            int32_t v = l11_s16(&o, at);
            if (v != 0) l11_w16(&o, at, (uint32_t)(v - 1));
        } else {
            uint32_t v = l11_u8(&o, at);
            if (v != 0) l11_w8(&o, at, v - 1);
        }
    }
    if ((int8_t)l11_u8(&o, d + 0x7E) != 0 && l11_u8(&o, 0x00810700u) != 0xDu) l11_c_001B5360(&o, e);
    l11_c_001B0D80(&o, e);
    return l11_end(&o);
}

/* ---- entries of the functions the actor family calls directly ---- */
#define L11_ENTRY_ED(name, call)                                                                     \
    int name(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, EmLevel11PortFault *fault)         \
    {                                                                                                \
        L11 o;                                                                                       \
        if (l11_begin(&o, h, fault)) return -1;                                                      \
        call;                                                                                        \
        return l11_end(&o);                                                                          \
    }
#define L11_ENTRY_EDS(name, call)                                                                    \
    int name(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, uint32_t sp, EmLevel11PortFault *fault) \
    {                                                                                                \
        L11 o;                                                                                       \
        if (l11_begin(&o, h, fault)) return -1;                                                      \
        call;                                                                                        \
        return l11_end(&o);                                                                          \
    }
L11_ENTRY_ED(em_level11_port_00141F00, l11_00141F00(&o, e, d))
L11_ENTRY_EDS(em_level11_port_00142070, l11_00142070(&o, e, d, sp))
L11_ENTRY_EDS(em_level11_port_00142330, l11_00142330(&o, e, d, sp))
L11_ENTRY_EDS(em_level11_port_001424C0, l11_001424C0(&o, e, d, sp))
L11_ENTRY_EDS(em_level11_port_001429D0, l11_001429D0(&o, e, d, sp))
L11_ENTRY_ED(em_level11_port_00145880, l11_00145880(&o, e, d))
L11_ENTRY_ED(em_level11_port_001459A0, l11_001459A0(&o, e, d))
L11_ENTRY_EDS(em_level11_port_001469B0, l11_001469B0(&o, e, d, sp))
L11_ENTRY_ED(em_level11_port_001471E0, l11_001471E0(&o, e, d))

#define L11_ENTRY_EDR(name, call)                                                                    \
    int name(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, int32_t *result, EmLevel11PortFault *fault) \
    {                                                                                                \
        L11 o;                                                                                       \
        if (!result || l11_begin(&o, h, fault)) return -1;                                           \
        int32_t r = call;                                                                            \
        if (l11_end(&o)) return -1;                                                                  \
        *result = r;                                                                                 \
        return 0;                                                                                    \
    }
L11_ENTRY_EDR(em_level11_port_001464B0, l11_001464B0(&o, e, d))
L11_ENTRY_EDR(em_level11_port_00146CE0, l11_00146CE0(&o, e, d))

int em_level11_port_00146110(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, uint32_t sp, int32_t *result,
                             EmLevel11PortFault *fault)
{
    L11 o;
    if (!result || l11_begin(&o, h, fault)) return -1;
    int32_t r = l11_00146110(&o, e, d, sp);
    if (l11_end(&o)) return -1;
    *result = r;
    return 0;
}
