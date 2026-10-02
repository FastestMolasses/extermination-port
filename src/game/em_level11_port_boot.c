/* The eleventh level's boot functions outside the 0x141D20 actor family
 * (em_level11_port_creature.c). docs/LEVEL11_PORT.md section 2 (behaviour)
 * and section 3 (verification). Every function follows the original
 * instructions; floats are bit patterns (em_ee_float.h). */
#include "em_level11_port_internal.h"

#define F_TWO_PI_B 0x40C90FDBu /* 6.28318548 */
#define F_PI_B     0x40490FDBu /* 3.14159274 */

/* ------------------------------------------------------------------------
 * 001F9140 (a0, a1, a2, a3, f12): the quadword D_0025DB00 copied to the
 * frame, then 001F8D30(a0, a1, a2, the copy, f12, f12, 0.0, a3). */
void l11_001F9140(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t f12, uint32_t sp)
{
    uint8_t q[16];
    uint32_t v = sp - 0x10;
    l11_q(o, 0x0025DB00u, q);
    l11_wq(o, v, q);
    l11_c_001F8D30(o, a0, a1, a2, v, f12, f12, F_ZERO, a3);
}

int em_level11_port_001F9140(const EmLevel11PortHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                             float f12, uint32_t sp, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    l11_001F9140(&o, a0, a1, a2, a3, l11_bits(f12), sp);
    return l11_end(&o);
}

/* ------------------------------------------------------------------------
 * 00100130 (two soft doubles): 1 when 001274B0(a0, a1), the soft-double
 * compare, returns a value >= 0 (as a 64-bit register), else 0. */
int em_level11_port_00100130(const EmLevel11PortHooks *h, uint64_t q0, uint64_t q1, int32_t *result,
                             EmLevel11PortFault *fault)
{
    L11 o;
    if (!result || l11_begin(&o, h, fault)) return -1;
    uint64_t v = 0;
    l11_c_001274B0(&o, q0, q1, &v);
    if (l11_end(&o)) return -1;
    *result = (int64_t)v >= 0;
    return 0;
}

/* ------------------------------------------------------------------------
 * 00118418 (a0; the sequencer's event handler): the event type a0 +0x34.
 * 1: every channel record of the 48 at 0x27CCC0 (0x6A bytes each) with +0
 * == 1, +0x1A == 2 and +0x22 == a0 +0x24, whose +0x3E / +2 equal the
 * command bytes q[4] / q[5] (q = a0 +8 + the stream base *(0x281AC0 +
 * 0x14)) and whose +6 equals the word a0 +0x18, is re-armed: +0x48 = 1,
 * +0x58 = q[3], +0x5A = +0x4C, +0x5C = +0x5E = (q[2] * 4) * the rate
 * *(0x27F77A) / 60; a0 +8 += 6. Otherwise: *(*(0x281AC0 + 0xC) + 4) =
 * q[2]; every channel i with +4 == (a0[0] & 0xF), +0x22 == a0 +0x24, +6 ==
 * a0 +0x18, +8 != 1, +0x1A == 1 and +0 == 1 gets +0x4C = q[2], +0x32 = the
 * halfword at 0x242630 + ((00117BA0(0, +0xE) >> 2) << 1), and v =
 * 001179E0(i, a0) goes to 001157F0(1, i, v >> 16, v & 0xFFFF); a0 +8 += 3.
 * Returns the new cursor a0 +8 (the decomp's NEARMISS text returns 0; the
 * original returns the stored cursor). */
int em_level11_port_00118418(const EmLevel11PortHooks *h, uint32_t a0, int32_t *result, EmLevel11PortFault *fault)
{
    L11 o;
    if (!result || l11_begin(&o, h, fault)) return -1;
    const uint32_t chan = 0x0027CCC0u, base = 0x00281AC0u;
    uint32_t cur;
    if (l11_u16(&o, a0 + 0x34) == 1) {
        cur = l11_u32(&o, a0 + 8);
        for (uint32_t p = chan; p < chan + 0x13E0u; p += 0x6A) {
            if (l11_failed(&o)) break;
            if (l11_u16(&o, p) != 1 || l11_u16(&o, p + 0x1A) != 2) continue;
            uint32_t ch = l11_u16(&o, p + 0x22);
            if (ch != l11_u16(&o, a0 + 0x24)) continue;
            uint32_t q = cur + l11_u32(&o, base + 0x14);
            uint32_t v = l11_u16(&o, p + 0x3E);
            if (v != l11_u8(&o, q + 4)) continue;
            v = l11_u16(&o, p + 2);
            if (v != l11_u8(&o, q + 5)) continue;
            v = l11_u16(&o, p + 6);
            if (v != l11_u32(&o, a0 + 0x18)) continue;
            l11_w16(&o, p + 0x48, 1);
            uint32_t b3 = l11_u8(&o, q + 3);
            int32_t rate = (int32_t)l11_u16(&o, 0x0027F77Au);
            l11_w16(&o, p + 0x58, b3);
            l11_w16(&o, p + 0x5A, l11_u16(&o, p + 0x4C));
            int32_t b2 = (int32_t)l11_u8(&o, q + 2);
            int32_t rate2 = (int32_t)l11_u16(&o, 0x0027F77Au);
            l11_w16(&o, p + 0x5C, (uint32_t)((b2 << 2) * rate2 / 60));
            b2 = (int32_t)l11_u8(&o, q + 2);
            l11_w16(&o, p + 0x5E, (uint32_t)((b2 << 2) * rate / 60));
        }
        cur += 6;
    } else {
        cur = l11_u32(&o, a0 + 8);
        uint32_t q = cur + l11_u32(&o, base + 0x14);
        uint32_t mix = l11_u32(&o, base + 0xC);
        l11_w8(&o, mix + 4, l11_u8(&o, q + 2));
        cur = l11_u32(&o, a0 + 8);
        for (int32_t i = 0; i < 0x30; i++) {
            if (l11_failed(&o)) break;
            uint32_t p = chan + (uint32_t)i * 0x6A;
            uint32_t want = l11_u8(&o, a0) & 0xFu;
            if (l11_u16(&o, p + 4) != want) continue;
            want = l11_u16(&o, a0 + 0x24);
            if (l11_u16(&o, p + 0x22) != want) continue;
            want = l11_u32(&o, a0 + 0x18);
            if (l11_u16(&o, p + 6) != want) continue;
            if (l11_u16(&o, p + 8) == 1 || l11_u16(&o, p + 0x1A) != 1 || l11_u16(&o, p) != 1) continue;
            q = cur + l11_u32(&o, base + 0x14);
            l11_w16(&o, p + 0x4C, l11_u8(&o, q + 2));
            int32_t r = 0;
            if (l11_c_00117BA0(&o, 0, (int32_t)l11_u16(&o, p + 0xE), &r)) break;
            l11_w16(&o, p + 0x32, l11_u16(&o, 0x00242630u + (uint32_t)((r >> 2) << 1)));
            int32_t v = 0;
            if (l11_c_001179E0(&o, i, a0, &v)) break;
            l11_c_001157F0(&o, 1, i, v >> 16, v & 0xFFFF, &r);
            cur = l11_u32(&o, a0 + 8);
        }
        cur += 3;
    }
    l11_w32(&o, a0 + 8, cur);
    if (l11_end(&o)) return -1;
    *result = (int32_t)cur;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0019A6F0 (a0, a1, a2, a3, t0): the segment a1 -> a2 copied to 0x70003190
 * / 0x700031A0 and a2 to the frame; 0x700031AC = 0x7000319C = 1.0, the
 * frame's w = 1.0, 0x700031D4 = 0. flags = a3 & 0xFF. With flags bit 0 and
 * a0 +0 bit 0: when (a0 +2 & 0x1F) == 0, 001A6440(t0 & 0xFFFF), refused
 * when the hit object's (0x700031D4) +0x52 has bit 1; else 001A7280(0x40)
 * when (t0 & 0xFFFF) == 0x40, refused when a0 +0x52 lacks bit 1; a hit with
 * a0 +0x52 bit 0 clear copies 0x700031B0.. to 0x700031A0.. (mode 1).
 * 0x7000324E = a0 +2 & 0x1F. Flags bit 1: 0x70003254 = a0 +0x14,
 * 001A0B10() -> mode 2. Flags bit 2: 0019D330() -> mode 4. A mode copies
 * the frame's a2 back to 0x700031A0; none clears 0x700031D0.
 * 0x700031D8 = the mode, returned. */
int32_t l11_0019A6F0(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t t0, uint32_t sp)
{
    uint32_t local = sp - 0x10;
    int32_t mode = 0;
    for (int i = 0; i < 3; i++) {
        l11_w32(o, 0x70003190u + 4u * (uint32_t)i, l11_u32(o, a1 + 4u * (uint32_t)i));
        uint32_t f = l11_u32(o, a2 + 4u * (uint32_t)i);
        l11_w32(o, local + 4u * (uint32_t)i, f);
        l11_w32(o, 0x700031A0u + 4u * (uint32_t)i, f);
    }
    l11_w32(o, 0x700031ACu, F_ONE);
    l11_w32(o, 0x7000319Cu, F_ONE);
    uint32_t flags = (uint32_t)a3 & 0xFFu;
    l11_w32(o, local + 12, F_ONE);
    l11_w32(o, 0x700031D4u, 0);
    if ((flags & 1u) && (l11_u8(o, a0) & 1u)) {
        int32_t ok = 1;
        if (!(l11_u8(o, a0 + 2) & 0x1Fu)) {
            if (l11_c_001A6440(o, t0 & 0xFFFF, &ok)) return 0;
            if (ok != 0 && (l11_u16(o, l11_u32(o, 0x700031D4u) + 0x52) & 2u)) ok = 0;
        } else {
            /* (t0 & 0xFFFF) != 0x40 skips the call; the result register
             * then holds a value the function set earlier, nonzero on
             * every input measured (docs/LEVEL11_PORT.md section 2). */
            if ((t0 & 0xFFFF) == 0x40 && l11_c_001A7280(o, 0x40, &ok)) return 0;
            if (ok != 0 && !(l11_u16(o, a0 + 0x52) & 2u)) ok = 0;
        }
        if (ok != 0 && !(l11_u16(o, a0 + 0x52) & 1u)) {
            for (int i = 0; i < 3; i++)
                l11_w32(o, 0x700031A0u + 4u * (uint32_t)i, l11_u32(o, 0x700031B0u + 4u * (uint32_t)i));
            mode = 1;
        }
    }
    l11_w16(o, 0x7000324Eu, l11_u8(o, a0 + 2) & 0x1Fu);
    if (flags & 2u) {
        l11_w32(o, 0x70003254u, l11_u32(o, a0 + 0x14));
        int32_t r = 0;
        if (l11_c_001A0B10(o, &r)) return 0;
        if (r != 0) mode = 2;
    }
    if (flags & 4u) {
        int32_t r = 0;
        if (l11_c_0019D330(o, &r)) return 0;
        if (r != 0) mode = 4;
    }
    if (mode != 0) {
        for (int i = 0; i < 3; i++)
            l11_w32(o, 0x700031A0u + 4u * (uint32_t)i, l11_u32(o, local + 4u * (uint32_t)i));
    } else {
        l11_w32(o, 0x700031D0u, 0);
    }
    l11_w32(o, 0x700031D8u, (uint32_t)mode);
    return mode;
}

int em_level11_port_0019A6F0(const EmLevel11PortHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, int32_t a3,
                             int32_t t0, uint32_t sp, int32_t *result, EmLevel11PortFault *fault)
{
    L11 o;
    if (!result || l11_begin(&o, h, fault)) return -1;
    int32_t r = l11_0019A6F0(&o, a0, a1, a2, a3, t0, sp);
    if (l11_end(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B2E50 (a0, out): the sorted table 0x700030F0 (count *0x700031E0, per
 * entry halfword flags at 0x70003170): count 0 -> 0. The first entry i not
 * below a0 +4: its flag bit 0 -> *out = entry i, 1; else 0. Every entry
 * below it: with last = count - 1, bit 0 of flag[last] -> 0; last 0 -> 0;
 * bit 0 of flag[last - 1] -> *out = entry last - 1, 1; else 0. A negative
 * count skips the walk (last = -1: the flags just below the table). */
int32_t l11_001B2E50(L11 *o, uint32_t a0, uint32_t out)
{
    int32_t count = l11_s32(o, 0x700031E0u);
    if (count == 0) return 0;
    int32_t i;
    for (i = 0; i < count; i++) {
        if (l11_failed(o)) return 0;
        uint32_t entry = l11_u32(o, 0x700030F0u + 4u * (uint32_t)i);
        if (!L11_LT(entry, l11_u32(o, a0 + 4))) {
            if (l11_u16(o, 0x70003170u + 2u * (uint32_t)i) & 1u) {
                l11_w32(o, out, l11_u32(o, 0x700030F0u + 4u * (uint32_t)i));
                return 1;
            }
            return 0;
        }
    }
    int32_t last = i - 1;
    if (l11_u16(o, 0x70003170u + 2u * (uint32_t)last) & 1u) return 0;
    if (last == 0) return 0;
    if (l11_u16(o, 0x7000316Eu + 2u * (uint32_t)last) & 1u) {
        l11_w32(o, out, l11_u32(o, 0x700030ECu + 4u * (uint32_t)last));
        return 1;
    }
    return 0;
}

int em_level11_port_001B2E50(const EmLevel11PortHooks *h, uint32_t a0, uint32_t out, int32_t *result,
                             EmLevel11PortFault *fault)
{
    L11 o;
    if (!result || l11_begin(&o, h, fault)) return -1;
    int32_t r = l11_001B2E50(&o, a0, out);
    if (l11_end(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B3F10 (self, bearing f12, height f13): the yaw error |wrap(001B1240(
 * self +0xB0, the player's x, z) - bearing)| picks a radius table by self
 * +3 (1 / 7 / 6 / 5 / other: 0x24D720 / 0x24D780 / 0x24D760 / 0x24D740 /
 * 0x24D7A0 within pi/4, else the next table up); 0 unless D_008102B4 == 1
 * and 001B13F0(the player, self +0xB0, table[D_0081050C & 3]) is set; self
 * +0xB0.. and the player 0x810350.. are copied to 0x70003600 / 0x70003610;
 * 0 unless |y0 - y1| < height; both y += 15, then 0019A6F0(self,
 * 0x70003600, 0x70003610, 7, 0x40): no hit -> 1; a hit whose object
 * (0x700031D0) +0x1A has bit 0x2000 -> 0, else 1. */
int32_t l11_001B3F10(L11 *o, uint32_t self, uint32_t bearing, uint32_t height, uint32_t sp)
{
    uint32_t f = 0;
    uint32_t px = l11_u32(o, 0x00810360u);
    if (l11_c_001B1240(o, self + 0xB0, px, l11_u32(o, 0x00810368u), &f)) return 0;
    if (l11_c_001B1470(o, L11_SUB(f, bearing), &f)) return 0;
    if (l11_c_0011DF78(o, f, &f)) return 0;
    uint32_t tbl;
    int near = L11_LE(f, 0x3F490FDBu); /* pi / 4 */
    switch (l11_u8(o, self + 3)) {
    case 1: tbl = near ? 0x0024D720u : 0x0024D730u; break;
    case 7: tbl = near ? 0x0024D780u : 0x0024D790u; break;
    case 6: tbl = near ? 0x0024D760u : 0x0024D770u; break;
    case 5: tbl = near ? 0x0024D740u : 0x0024D750u; break;
    default: tbl = near ? 0x0024D7A0u : 0x0024D7B0u; break;
    }
    if (l11_u8(o, 0x008102B4u) != 1) return 0;
    int32_t r = 0;
    if (l11_c_001B13F0(o, 0x00810360u, self + 0xB0, l11_u32(o, tbl + 4u * (l11_u8(o, 0x0081050Cu) & 3u)), &r)) return 0;
    if (r == 0) return 0;
    uint32_t t0 = l11_u32(o, 0x00810350u);
    uint32_t p0 = l11_u32(o, self + 0xB0);
    uint32_t t1 = l11_u32(o, 0x00810354u);
    uint32_t t2 = l11_u32(o, 0x00810358u);
    uint32_t t3 = l11_u32(o, 0x0081035Cu);
    l11_w32(o, 0x70003600u, p0);
    l11_w32(o, 0x70003604u, l11_u32(o, self + 0xB4));
    l11_w32(o, 0x70003608u, l11_u32(o, self + 0xB8));
    l11_w32(o, 0x7000360Cu, l11_u32(o, self + 0xBC));
    l11_w32(o, 0x70003610u, t0);
    l11_w32(o, 0x70003614u, t1);
    l11_w32(o, 0x70003618u, t2);
    l11_w32(o, 0x7000361Cu, t3);
    uint32_t y0 = l11_u32(o, 0x70003604u);
    if (l11_c_0011DF78(o, L11_SUB(y0, l11_u32(o, 0x70003614u)), &f)) return 0;
    if (!L11_LT(f, height)) return 0;
    uint32_t a = L11_ADD(l11_u32(o, 0x70003604u), 0x41700000u); /* 15.0 */
    uint32_t b = L11_ADD(l11_u32(o, 0x70003614u), 0x41700000u);
    l11_w32(o, 0x70003604u, a);
    l11_w32(o, 0x70003614u, b);
    if (l11_0019A6F0(o, self, 0x70003600u, 0x70003610u, 7, 0x40, sp - 0x30) == 0) return 1;
    if (!(l11_u16(o, l11_u32(o, 0x700031D0u) + 0x1A) & 0x2000u)) return 1;
    return 0;
}

int em_level11_port_001B3F10(const EmLevel11PortHooks *h, uint32_t self, float bearing, float height, uint32_t sp,
                             int32_t *result, EmLevel11PortFault *fault)
{
    L11 o;
    if (!result || l11_begin(&o, h, fault)) return -1;
    int32_t r = l11_001B3F10(&o, self, l11_bits(bearing), l11_bits(height), sp);
    if (l11_end(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001F4190 (self, timing, cfg): one burst of cfg +0x4C pieces (re-read
 * every pass) around self (+0x100.. the position, +0xD0 the basis, +0xD
 * the kind). age = timing +8, life = timing +4 - age, rnd = timing +0,
 * 0x70003A20 = (float)age. Each piece takes eight draws r = ((rnd >> 16) &
 * 0xFFFF) / 65535 + 0.0001, rnd = rnd * 37 + 11: the position
 * 0x700038A0 = self +0x100 + cfg[0..2] * (0011DE90(a), 0011DE90(2pi r1 -
 * pi), 0011E2A8(a)) with a = 2pi r0 - pi; the velocity 0x700038B0 = cfg
 * +0x10.. * r - cfg +0x20.. through the basis (001026A0) plus +0x100
 * (001028D0); the spin quaternion 0x700038C0 (001CA3B0 of pi * 360 r5 /
 * 180); the scale 0x700038D0 = cfg +0x40 + cfg +0x44 * r6; the spin rate
 * 0x70003A24 by kind (12 / 8 / 9 / 6 / 4; other kinds zero 0x70003A20) and
 * the turn pi * (age * (rate * r7)) / 180 (001CA3B0 into 0x700036E0,
 * 001CA4D0 into the spin); the fade 0x70003A24 = 1 or life / 20 below 20;
 * the drift (velocity y += cfg +0x48 * age, position += velocity * age);
 * 001CA1C0(0x700036A0, the spin, the position), the three rows scaled by
 * scale * fade (00102900) and 001F3E30(0x700036A0, cfg +0x30, cfg +0x50,
 * cfg +0x54 / +0x58 by the piece's parity, cfg +0x5C). */
static uint32_t l11_draw(uint32_t *rnd)
{
    uint32_t r = L11_CVT_S_W((uint32_t)l11_sra(*rnd, 16) & 0xFFFFu);
    r = L11_ADD(L11_DIV(r, 0x477FFF00u), 0x38D1B717u); /* 65535.0, 0.0001 */
    *rnd = *rnd * 37u + 11u;
    return r;
}

void l11_001F4190(L11 *o, uint32_t self, uint32_t timing, uint32_t cfg)
{
    int32_t age = l11_s32(o, timing + 8);
    int32_t life = l11_s32(o, timing + 4) - age;
    uint32_t rnd = l11_u32(o, timing);
    l11_w32(o, 0x70003A20u, L11_CVT_S_W((uint32_t)age));
    for (int32_t i = 0; i < l11_s32(o, cfg + 0x4C); i++) {
        if (l11_failed(o)) return;
        uint32_t r0 = l11_draw(&rnd), r1 = l11_draw(&rnd);
        uint32_t ang = L11_SUB(L11_MUL(F_TWO_PI_B, r0), F_PI_B);
        uint32_t c = 0;
        if (l11_c_0011DE90(o, ang, &c)) return;
        uint32_t k = l11_u32(o, cfg);
        l11_w32(o, 0x700038A0u, L11_ADD(l11_u32(o, self + 0x100), L11_MUL(k, c)));
        if (l11_c_0011DE90(o, L11_SUB(L11_MUL(F_TWO_PI_B, r1), F_PI_B), &c)) return;
        k = l11_u32(o, cfg + 4);
        l11_w32(o, 0x700038A4u, L11_ADD(l11_u32(o, self + 0x104), L11_MUL(k, c)));
        if (l11_c_0011E2A8(o, ang, &c)) return;
        k = l11_u32(o, cfg + 8);
        l11_w32(o, 0x700038A8u, L11_ADD(l11_u32(o, self + 0x108), L11_MUL(k, c)));
        l11_w32(o, 0x700038ACu, F_ONE);
        uint32_t r2 = l11_draw(&rnd), r3 = l11_draw(&rnd), r4 = l11_draw(&rnd);
        const uint32_t rv[3] = {r2, r3, r4};
        for (uint32_t k = 0; k < 3; k++) {
            uint32_t sc = L11_MUL(l11_u32(o, cfg + 0x10 + 4 * k), rv[k]);
            l11_w32(o, 0x700038B0u + 4 * k, L11_SUB(sc, l11_u32(o, cfg + 0x20 + 4 * k)));
        }
        l11_w32(o, 0x700038BCu, F_ONE);
        l11_c_001026A0(o, 0x700038B0u, self + 0xD0, 0x700038B0u);
        l11_c_001028D0(o, 0x700038B0u, 0x700038B0u, self + 0x100);
        uint32_t r5 = l11_draw(&rnd);
        uint32_t t = L11_DIV(L11_MUL(F_PI_B, L11_MUL(0x43B40000u, r5)), 0x43340000u); /* 360, 180 */
        l11_c_001CA3B0(o, 0x700038C0u, t, t, t);
        uint32_t r6 = l11_draw(&rnd);
        uint32_t a = l11_u32(o, cfg + 0x40);
        a = L11_ADD(a, L11_MUL(l11_u32(o, cfg + 0x44), r6));
        l11_w32(o, 0x700038D0u, a);
        l11_w32(o, 0x700038D4u, a);
        l11_w32(o, 0x700038D8u, a);
        l11_w32(o, 0x700038DCu, F_ONE);
        switch (l11_u8(o, self + 0xD)) {
        case 21: case 24: case 25: case 26: l11_w32(o, 0x70003A24u, 0x41400000u); break; /* 12.0 */
        case 15: case 16: case 19: l11_w32(o, 0x70003A24u, 0x41000000u); break;          /* 8.0 */
        case 11: l11_w32(o, 0x70003A24u, 0x41100000u); break;                            /* 9.0 */
        case 12: l11_w32(o, 0x70003A24u, 0x40C00000u); break;                            /* 6.0 */
        case 13: case 14: case 20: l11_w32(o, 0x70003A24u, 0x40800000u); break;          /* 4.0 */
        default: l11_w32(o, 0x70003A20u, F_ZERO); break;
        }
        uint32_t r7 = l11_draw(&rnd);
        uint32_t rate = L11_MUL(l11_u32(o, 0x70003A24u), r7);
        uint32_t t2 = L11_DIV(L11_MUL(F_PI_B, L11_MUL(l11_u32(o, 0x70003A20u), rate)), 0x43340000u);
        l11_w32(o, 0x70003A24u, t2);
        l11_c_001CA3B0(o, 0x700036E0u, t2, t2, t2);
        l11_c_001CA4D0(o, 0x700038C0u, 0x700038C0u, 0x700036E0u);
        l11_w32(o, 0x70003A24u, F_ONE);
        if (life < 20) l11_w32(o, 0x70003A24u, L11_DIV(L11_CVT_S_W((uint32_t)life), 0x41A00000u)); /* 20.0 */
        uint32_t sc = l11_u32(o, 0x70003A20u);
        uint32_t k48 = l11_u32(o, cfg + 0x48);
        uint32_t vy = l11_u32(o, 0x700038B4u);
        uint32_t px = l11_u32(o, 0x700038A0u);
        l11_w32(o, 0x700038B4u, L11_ADD(vy, L11_MUL(k48, sc)));
        uint32_t vx = l11_u32(o, 0x700038B0u);
        vy = l11_u32(o, 0x700038B4u);
        uint32_t vz = l11_u32(o, 0x700038B8u);
        l11_w32(o, 0x700038A0u, L11_ADD(px, L11_MUL(vx, sc)));
        uint32_t p = l11_u32(o, 0x700038A4u);
        l11_w32(o, 0x700038A4u, L11_ADD(p, L11_MUL(vy, sc)));
        p = l11_u32(o, 0x700038A8u);
        l11_w32(o, 0x700038A8u, L11_ADD(p, L11_MUL(vz, sc)));
        l11_c_001CA1C0(o, 0x700036A0u, 0x700038C0u, 0x700038A0u);
        for (uint32_t row = 0; row < 3; row++) {
            uint32_t sca = l11_u32(o, 0x700038D0u + 4u * row);
            uint32_t fade = l11_u32(o, 0x70003A24u);
            l11_c_00102900(o, 0x700036A0u + 0x10u * row, 0x700036A0u + 0x10u * row, L11_MUL(sca, fade));
        }
        int32_t id0 = l11_s32(o, cfg + 0x50);
        int32_t id1 = l11_s32(o, cfg + (uint32_t)(i % 2) * 4u + 0x54);
        int32_t id2 = l11_s32(o, cfg + 0x5C);
        l11_c_001F3E30(o, 0x700036A0u, cfg + 0x30, id0, id1, id2);
    }
}

int em_level11_port_001F4190(const EmLevel11PortHooks *h, uint32_t self, uint32_t timing, uint32_t cfg,
                             EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    l11_001F4190(&o, self, timing, cfg);
    return l11_end(&o);
}

/* ------------------------------------------------------------------------
 * 001F4840 (self; a spawned effect). st = self + 0x1F0. +4 0: unless the
 * kind +0xD is 0x10 / 0x13, the matrix +0xD0 (001029C0, 00102C58 by +0xC0,
 * 00102918 by +0xB0); kind 0x14 instead +0x104 += 30; the lifetime st +4
 * by kind (0x1E for 0x10 / 0x19 / 0x1A, 0x3C for 0xB..0xE / 0x13 / 0x15 /
 * 0x18, 0x78 for 0xF, 0xB4 for 0x14, else 1), st +0 = 00122BB8(), st +8 =
 * 0, +4 = 1. +4 1: 001F4190(self, st, 0x25A350 + kind * 0x60), st +8 += 1,
 * past st +4 +4 = 3. +4 2 / 3: 001AFC10(self). */
int em_level11_port_001F4840(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    uint32_t st = self + 0x1F0;
    switch (l11_u8(&o, self + 4)) {
    case 0: {
        switch (l11_u8(&o, self + 0xD)) {
        case 0x10:
        case 0x13:
            break;
        case 0x14:
            l11_w32(&o, self + 0x104, L11_ADD(l11_u32(&o, self + 0x104), 0x41F00000u)); /* 30.0 */
            break;
        default:
            l11_c_001029C0(&o, self + 0xD0);
            l11_c_00102C58(&o, self + 0xD0, self + 0xD0, self + 0xC0);
            l11_c_00102918(&o, self + 0xD0, self + 0xD0, self + 0xB0);
            break;
        }
        uint32_t life;
        switch (l11_u8(&o, self + 0xD)) {
        case 0x10: case 0x19: case 0x1A: life = 0x1E; break;
        case 0xB: case 0xC: case 0xD: case 0xE: case 0x13: case 0x15: case 0x18: life = 0x3C; break;
        case 0xF: life = 0x78; break;
        case 0x14: life = 0xB4; break;
        default: life = 1; break;
        }
        l11_w32(&o, st + 4, life);
        int32_t r = 0;
        if (l11_c_00122BB8(&o, &r)) break;
        l11_w32(&o, st, (uint32_t)r);
        l11_w32(&o, st + 8, 0);
        l11_w8(&o, self + 4, 1);
        break;
    }
    case 1:
        l11_001F4190(&o, self, st, 0x0025A350u + l11_u8(&o, self + 0xD) * 0x60u);
        l11_w32(&o, st + 8, (uint32_t)(l11_s32(&o, st + 8) + 1));
        int32_t ticks = l11_s32(&o, st + 8);
        if (ticks > l11_s32(&o, st + 4)) l11_w8(&o, self + 4, 3);
        break;
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
 * 001E4610 (self; the ring emitter). other = self +0x14, s = self + 0x1F0.
 * +4 0: by self +0xD: 0 -> s +0x14 = 50, 1 -> 30, each with s +0x28 = 5,
 * s +0x44 = s +0x50 = 0 (other kinds keep them); s +0x14 /= 2; s +0x18 = 0; the doubleword s +0x20 =
 * 0x20041A85553220D0; s +0x30 = 0x14, +0x34 = 0, +0x38 = 360.0; the matrix
 * +0xD0; +0x30 = other + 0x1F0, +0x34 = 0x1E4600, +0xC = +9 = 0, +0 = 1, +4
 * = 1; then as 1. +4 1: t = s +0x18 (0x70003A24); past 1: +0 = 2 and the
 * alpha 0x70003A20 = 24 * max(0, 1 - (t - 1) / 0.7); else +0 = 1 and 24;
 * s +0x3C = s +0x40 = t * s +0x14, s +0x48 = s +0x4C = s +0x28 + t * s
 * +0x14; (s +0x48, 1, s +0x4C, 1) through the matrix (001026A0) plus +0x100
 * (001028D0) into s; the colour s +0x1C from 00128250 of (alpha, alpha,
 * alpha, 0) bytes; 001CE660(0, 2, +0xD0, s + 0x30, the doubleword s +0x20,
 * s +0x1C); s +0x18 += (2 - t) / 25 then + 0.001; past 2 +4 = 3, +0 = 2;
 * 001B17A0(self). +4 2 / 3: 001AFC10(self). */
int em_level11_port_001E4610(const EmLevel11PortHooks *h, uint32_t self, uint32_t sp, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    uint32_t other = l11_u32(&o, self + 0x14);
    uint32_t s = self + 0x1F0;
    uint32_t state = l11_u8(&o, self + 4);
    if (state == 2 || state == 3) {
        l11_c_001AFC10(&o, self);
        return l11_end(&o);
    }
    if (state > 1) return l11_end(&o);
    if (state == 0) {
        switch (l11_u8(&o, self + 0xD)) {
        case 0:
            l11_w32(&o, s + 0x14, 0x42480000u); /* 50.0 */
            l11_w32(&o, s + 0x28, 0x40A00000u); /* 5.0 */
            l11_w32(&o, s + 0x44, 0);
            l11_w32(&o, s + 0x50, 0);
            break;
        case 1:
            l11_w32(&o, s + 0x14, 0x41F00000u); /* 30.0 */
            l11_w32(&o, s + 0x28, 0x40A00000u);
            l11_w32(&o, s + 0x44, 0);
            l11_w32(&o, s + 0x50, 0);
            break;
        default:
            break;
        }
        l11_w32(&o, s + 0x14, L11_DIV(l11_u32(&o, s + 0x14), 0x40000000u)); /* 2.0 */
        l11_w32(&o, s + 0x18, F_ZERO);
        l11_w64(&o, s + 0x20, UINT64_C(0x20041A85553220D0));
        l11_w32(&o, s + 0x30, 0x14);
        l11_w32(&o, s + 0x34, 0);
        l11_w32(&o, s + 0x38, 0x43B40000u);
        l11_c_001029C0(&o, self + 0xD0);
        l11_c_00102C58(&o, self + 0xD0, self + 0xD0, self + 0xC0);
        l11_c_00102918(&o, self + 0xD0, self + 0xD0, self + 0xB0);
        l11_w32(&o, self + 0x30, other + 0x1F0);
        l11_w32(&o, self + 0x34, 0x001E4600u);
        l11_w8(&o, self + 0xC, 0);
        l11_w8(&o, self + 9, 0);
        l11_w8(&o, self, 1);
        l11_w8(&o, self + 4, 1);
    }
    uint32_t t = l11_u32(&o, s + 0x18);
    l11_w32(&o, 0x70003A24u, t);
    if (!L11_LE(t, F_ONE)) {
        l11_w8(&o, self, 2);
        uint32_t a = L11_SUB(l11_u32(&o, 0x70003A24u), F_ONE);
        l11_w32(&o, 0x70003A20u, a);
        a = L11_DIV(a, 0x3F333333u); /* 0.7 */
        a = L11_SUB(F_ONE, a);
        l11_w32(&o, 0x70003A20u, a);
        if (L11_LT(a, F_ZERO)) a = F_ZERO;
        l11_w32(&o, 0x70003A20u, a);
        l11_w32(&o, 0x70003A20u, L11_MUL(l11_u32(&o, 0x70003A20u), 0x41C00000u)); /* 24.0 */
    } else {
        l11_w8(&o, self, 1);
        l11_w32(&o, 0x70003A20u, 0x41C00000u);
    }
    for (uint32_t k = 0; k < 4; k++) {
        /* s +0x3C, +0x40 = t * s +0x14; s +0x48, +0x4C = s +0x28 + t * s +0x14 */
        uint32_t tt = l11_u32(&o, 0x70003A24u);
        uint32_t m = L11_MUL(tt, l11_u32(&o, s + 0x14));
        if (k >= 2) m = L11_ADD(l11_u32(&o, s + 0x28), m);
        l11_w32(&o, s + (k < 2 ? 0x3C + 4 * k : 0x48 + 4 * (k - 2)), m);
    }
    l11_w32(&o, 0x700038B0u, l11_u32(&o, s + 0x48));
    l11_w32(&o, 0x700038B4u, F_ONE);
    l11_w32(&o, 0x700038B8u, l11_u32(&o, s + 0x4C));
    l11_w32(&o, 0x700038BCu, F_ONE);
    l11_c_001026A0(&o, s, self + 0xD0, 0x700038B0u);
    l11_c_001028D0(&o, s, s, self + 0x100);
    uint32_t v20 = l11_u32(&o, 0x70003A20u);
    l11_w32(&o, 0x700038A0u, v20);
    l11_w32(&o, 0x700038A4u, v20);
    l11_w32(&o, 0x700038A8u, v20);
    l11_w32(&o, 0x700038ACu, 0);
    int32_t b = 0;
    if (l11_c_00128250(&o, l11_u32(&o, 0x700038A0u), &b)) return l11_end(&o);
    l11_w32(&o, s + 0x1C, (uint32_t)b);
    if (l11_c_00128250(&o, l11_u32(&o, 0x700038A4u), &b)) return l11_end(&o);
    l11_w32(&o, s + 0x1C, l11_u32(&o, s + 0x1C) | ((uint32_t)b << 8));
    if (l11_c_00128250(&o, l11_u32(&o, 0x700038A8u), &b)) return l11_end(&o);
    l11_w32(&o, s + 0x1C, l11_u32(&o, s + 0x1C) | ((uint32_t)b << 16));
    if (l11_c_00128250(&o, l11_u32(&o, 0x700038ACu), &b)) return l11_end(&o);
    l11_w32(&o, s + 0x1C, l11_u32(&o, s + 0x1C) | ((uint32_t)b << 24));
    uint64_t tex;
    memcpy(&tex, l11_at(&o, s + 0x20, 8), 8);
    l11_001CE660(&o, 0, 2, self + 0xD0, s + 0x30, tex, l11_u32(&o, s + 0x1C), sp - 0x40);
    uint32_t v = l11_u32(&o, s + 0x18);
    v = L11_ADD(v, L11_DIV(L11_SUB(0x40000000u, v), 0x41C80000u)); /* 2.0, 25.0 */
    l11_w32(&o, s + 0x18, v);
    v = L11_ADD(v, 0x3A83126Fu); /* 0.001 */
    l11_w32(&o, s + 0x18, v);
    if (L11_LT(0x40000000u, v)) {
        l11_w8(&o, self + 4, 3);
        l11_w8(&o, self, 2);
    }
    l11_c_001B17A0(&o, self, &b);
    return l11_end(&o);
}

/* A VU0 register load (lqc2) / store (sqc2): four words. */
static void l11_vload(L11 *o, uint32_t a, uint32_t v[4])
{
    a &= ~15u;
    for (unsigned k = 0; k < 4; k++) v[k] = l11_u32(o, a + 4u * k);
}

static void l11_vstore(L11 *o, uint32_t a, const uint32_t v[4])
{
    a &= ~15u;
    for (unsigned k = 0; k < 4; k++) l11_w32(o, a + 4u * k, v[k]);
}

/* One VU0 macro instruction of a form the original executes; an unmeasured
 * form latches fault 7 (never expected: the forms used here are in the
 * table). */
static void l11_vu(L11 *o, em_vu_op op, int bc, const uint32_t fs[4], const uint32_t ft[4], const uint32_t acc[4],
                   uint32_t dst[4])
{
    if (em_vu_vec_bits(op, 15, bc, fs, ft, 0, acc, dst) != EM_EE_FLOAT_OK)
        l11_latch(o, 0, EM_LEVEL11_PORT_FAULT_REGISTER);
}

/* The matrix rows m (4 x 4 words) times p (x, y, z) plus row 3 times
 * p.w-lane broadcast: VMULAx, VMADDAy, VMADDAz, VMADDw (dest xyzw). */
static void l11_vu_transform(L11 *o, const uint32_t m[4][4], const uint32_t p[4], uint32_t out[4])
{
    uint32_t acc[4] = {0, 0, 0, 0};
    l11_vu(o, EM_VU_MULABC, 0, m[0], p, NULL, acc);
    l11_vu(o, EM_VU_MADDABC, 1, m[1], p, acc, acc);
    l11_vu(o, EM_VU_MADDABC, 2, m[2], p, acc, acc);
    l11_vu(o, EM_VU_MADDBC, 3, m[3], p, acc, out);
}

/* ------------------------------------------------------------------------
 * 001CE660 (layer, tag, matrix, band, tex0 (t0, 64-bit), rgba (t1)): a
 * ring of band +0 samples from band +4 to band +8 degrees: per sample the
 * angle's 0011E2A8 / 0011DE90 (kept at 0x70003680 / 0x70003684), the inner
 * point (band +0xC * s, band +0x14, band +0x10 * c, 1) and the outer point
 * (band +0x18 * s, band +0x20, band +0x1C * c, 1) in the scratchpad quad
 * 0x70003400 (odd samples the first pair, even the second), each moved by
 * the matrix on VU0; from the second sample on 001CDDC0(layer, tag, the
 * quad, tex0, rgba). */
void l11_001CE660(L11 *o, int32_t layer, int32_t tag, uint32_t mtx, uint32_t band, uint64_t tex0, uint32_t rgba,
                  uint32_t sp)
{
    uint32_t angle = L11_DIV(L11_MUL(l11_u32(o, band + 4), F_PI_B), 0x43340000u);
    uint32_t end = L11_DIV(L11_MUL(l11_u32(o, band + 8), F_PI_B), 0x43340000u);
    uint32_t step = L11_DIV(L11_SUB(end, angle), L11_CVT_S_W((uint32_t)(l11_s32(o, band) - 1)));
    for (int32_t i = 0; i < l11_s32(o, band); i++) {
        if (l11_failed(o)) return;
        uint32_t v = (i & 1) ? 0x70003400u : 0x70003420u;
        uint32_t sn = 0, cs = 0, k;
        if (l11_c_0011E2A8(o, angle, &sn)) return;
        l11_w32(o, 0x70003680u, sn);
        if (l11_c_0011DE90(o, angle, &cs)) return;
        l11_w32(o, 0x70003684u, cs);
        sn = l11_u32(o, 0x70003680u);
        l11_w32(o, v + 0x00, L11_MUL(l11_u32(o, band + 0xC), sn));
        l11_w32(o, v + 0x04, l11_u32(o, band + 0x14));
        cs = l11_u32(o, 0x70003684u);
        l11_w32(o, v + 0x08, L11_MUL(l11_u32(o, band + 0x10), cs));
        l11_w32(o, v + 0x0C, F_ONE);
        sn = l11_u32(o, 0x70003680u);
        l11_w32(o, v + 0x10, L11_MUL(l11_u32(o, band + 0x18), sn));
        l11_w32(o, v + 0x14, l11_u32(o, band + 0x20));
        k = l11_u32(o, band + 0x1C);
        l11_w32(o, v + 0x18, L11_MUL(k, l11_u32(o, 0x70003684u)));
        l11_w32(o, v + 0x1C, F_ONE);
        uint32_t m[4][4], p[4], out[4];
        for (unsigned r = 0; r < 4; r++) l11_vload(o, mtx + 0x10u * r, m[r]);
        for (unsigned j = 0; j < 2; j++) {
            l11_vload(o, v + 0x10u * j, p);
            l11_vu_transform(o, (const uint32_t (*)[4])m, p, out);
            l11_vstore(o, v + 0x10u * j, out);
        }
        if (i != 0) l11_001CDDC0(o, layer, tag, 0x70003400u, tex0, rgba, sp - 0x90);
        angle = L11_ADD(angle, step);
    }
}

int em_level11_port_001CE660(const EmLevel11PortHooks *h, int32_t layer, int32_t tag, uint32_t mtx, uint32_t band,
                             uint64_t tex0, uint32_t rgba, uint32_t sp, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    l11_001CE660(&o, layer, tag, mtx, band, tex0, rgba, sp);
    return l11_end(&o);
}

/* VU0 lane helpers for 001CDDC0 (forms the original executes). */
static void l11_vu_dest(L11 *o, em_vu_op op, unsigned dest, int bc, const uint32_t fs[4], const uint32_t ft[4],
                        uint32_t q, const uint32_t acc[4], uint32_t dst[4])
{
    if (em_vu_vec_bits(op, dest, bc, fs, ft, q, acc, dst) != EM_EE_FLOAT_OK)
        l11_latch(o, 0, EM_LEVEL11_PORT_FAULT_REGISTER);
}

/* Q = 1.0 / v.w: the VU0 division of the constant register's w lane by
 * v's w lane. */
static uint32_t l11_vu_recip_w(L11 *o, const uint32_t v[4])
{
    uint32_t q = 0;
    if (em_vu_div_bits(F_ONE, v[3], 3, 3, &q) != EM_EE_FLOAT_OK) l11_latch(o, 0, EM_LEVEL11_PORT_FAULT_REGISTER);
    return q;
}

/* The fog lane: w = clamp(fog.z + w * fog.w, 0, fog.x) (VMULAz.w with vf0,
 * VMADDw.w, VMINIx.w by fog.x, VMAXx.w by vf0.x), then VFTOI4 of the
 * whole vector. */
static void l11_vu_fog_ftoi4(L11 *o, uint32_t v[4], const uint32_t fog[4], uint32_t out[4])
{
    static const uint32_t vf0[4] = {0, 0, 0, F_ONE};
    uint32_t acc[4] = {0, 0, 0, 0};
    l11_vu_dest(o, EM_VU_MULABC, 1, 2, vf0, fog, 0, NULL, acc);
    l11_vu_dest(o, EM_VU_MADDBC, 1, 3, v, fog, 0, acc, v);
    v[3] = em_vu_min_bits(v[3], fog[0]);
    v[3] = em_vu_max_bits(v[3], vf0[0]);
    for (unsigned k = 0; k < 4; k++) out[k] = em_vu_ftoi4_bits(v[k]);
}

/* ------------------------------------------------------------------------
 * 001CDDC0 (layer, mode, corners, tex0 (a3, 64-bit), rgba (t0)): one
 * textured quad. The sort key is the largest 001CCF70 of the four corners;
 * corner 0 projected by the camera rows 0x70003AC0 (VU0) gives the fog
 * (D_00275670 +0xA0: max, -, bias, scale) at 0x70003600 (VFTOI4); modes
 * 1..4 scale the colour by the fog (alpha for 1, r / g / b for 2..4), and
 * the colour's four bytes go to 0x70003600 as words. Two triangles (0, 1,
 * 2) and (1, 2, 3): the corners with their (u, v) into the clipper input
 * 0x8112C0 (0x50 bytes each), n = 001CF470(it, 001CD370(0)); with n set a
 * block 001CB5F0(page, key, 3n + 2) (page = 0x7635C0 + (layer << 15))
 * gets a VIF / GIF header and per clipped vertex (0x8117C0 + 0x50 i) the
 * ST / RGBAQ / XYZF2 quadwords: xy / w, z / (w - 1), the fog of w - 1, the
 * colour, s = u / w, t = v / w, q = 1 / w. Then 001CB950(page, key, tex0)
 * and 001CB900(page, key, mode). */
void l11_001CDDC0(L11 *o, int32_t layer, int32_t mode, uint32_t corners, uint64_t tex0, uint32_t rgba, uint32_t sp)
{
    static const uint32_t one_x[4] = {F_ONE, 0, 0, 0};
    static const uint32_t uv[4][2] = {{F_ZERO, F_ZERO}, {F_ZERO, F_ONE}, {F_ONE, F_ZERO}, {F_ONE, F_ONE}};
    uint32_t page = 0x007635C0u + ((uint32_t)layer << 15);
    int32_t key = 0, z = 0;
    if (l11_c_001CCF70(o, corners, &key)) return;
    for (uint32_t i = 1; i < 4; i++) {
        if (l11_c_001CCF70(o, corners + 0x10u * i, &z)) return;
        if (key < z) key = z;
    }
    uint32_t fog[4], m[4][4], v[4], clip[4], xf[4];
    l11_vload(o, l11_u32(o, 0x00275670u) + 0xA0, fog);
    for (unsigned r = 0; r < 4; r++) l11_vload(o, 0x70003AC0u + 0x10u * r, m[r]);
    l11_vload(o, corners, v);
    l11_vu_transform(o, (const uint32_t (*)[4])m, v, clip);
    l11_vstore(o, sp - 0x20, clip);
    uint32_t q = l11_vu_recip_w(o, clip);
    memcpy(xf, clip, sizeof xf);
    l11_vu_dest(o, EM_VU_MULQ, 14, -1, clip, NULL, q, NULL, xf);
    l11_vu_fog_ftoi4(o, xf, fog, v);
    l11_vstore(o, 0x70003600u, v);
    if (mode != 0) {
        int32_t f = l11_sra(l11_u32(o, 0x7000360Cu), 4);
        if (f >= 0x100) f = 0xFF;
        if (f < 0) f = 0;
        if (mode == 1) {
            uint32_t a = ((rgba >> 24) & 0xFFu) * (uint32_t)f >> 8;
            rgba = (rgba & 0x00FFFFFFu) | (a << 24);
        } else if (mode == 2 || mode == 3 || mode == 4) {
            uint32_t rr = (rgba & 0xFFu) * (uint32_t)f >> 8;
            uint32_t gg = ((rgba >> 8) & 0xFFu) * (uint32_t)f >> 8;
            uint32_t bb = ((rgba >> 16) & 0xFFu) * (uint32_t)f >> 8;
            l11_w32(o, 0x7000360Cu, 0xFF0); /* overwritten below */
            rgba = (rgba & 0xFF000000u) | (bb << 16) | (gg << 8) | rr;
        }
    }
    l11_w32(o, 0x70003600u, rgba & 0xFFu);
    l11_w32(o, 0x70003604u, (rgba >> 8) & 0xFFu);
    l11_w32(o, 0x70003608u, (rgba >> 16) & 0xFFu);
    l11_w32(o, 0x7000360Cu, (rgba >> 24) & 0xFFu);
    for (uint32_t tri = 0; tri < 2; tri++) {
        if (l11_failed(o)) return;
        for (uint32_t i = 0; i < 3; i++) {
            uint32_t ci = tri + i, in = 0x008112C0u + 0x50u * i;
            uint8_t qw[16];
            l11_q(o, corners + 0x10u * ci, qw);
            l11_wq(o, in, qw);
            l11_w32(o, in + 0x30, uv[ci][0]);
            l11_w32(o, in + 0x34, uv[ci][1]);
        }
        uint32_t ctx = 0;
        if (l11_c_001CD370(o, 0, &ctx)) return;
        int32_t n = 0;
        if (l11_c_001CF470(o, 0x008112C0u, ctx, &n)) return;
        if (n == 0) continue;
        uint32_t blk = 0;
        if (l11_c_001CB5F0(o, page, key, 3 * n + 2, &blk)) return;
        static const uint8_t zero16[16] = {0};
        l11_wq(o, blk, zero16);
        l11_w32(o, blk + 0x0C, 0x50000000u | (uint32_t)(3 * n + 1));
        l11_w64(o, blk + 0x10, UINT64_C(0x302EC00000008000) | (uint32_t)n);
        l11_w64(o, blk + 0x18, 0x412);
        l11_vload(o, l11_u32(o, 0x00275670u) + 0xA0, fog);
        for (unsigned r = 0; r < 4; r++) l11_vload(o, 0x70003AC0u + 0x10u * r, m[r]);
        for (int32_t i = 0; i < n; i++) {
            if (l11_failed(o)) return;
            uint32_t src = 0x008117C0u + 0x50u * (uint32_t)i, out = blk + 0x20 + 0x30u * (uint32_t)i;
            l11_vload(o, src, v);
            l11_vu_transform(o, (const uint32_t (*)[4])m, v, clip);
            l11_vstore(o, sp - 0x10, clip);
            q = l11_vu_recip_w(o, clip);
            memcpy(xf, clip, sizeof xf);
            l11_vu_dest(o, EM_VU_MULQ, 12, -1, clip, NULL, q, NULL, xf);
            l11_vu_dest(o, EM_VU_SUBBC, 1, 0, clip, one_x, 0, NULL, xf);
            q = l11_vu_recip_w(o, xf);
            l11_vu_dest(o, EM_VU_MULQ, 2, -1, clip, NULL, q, NULL, xf);
            l11_vu_fog_ftoi4(o, xf, fog, v);
            l11_vstore(o, out + 0x20, v);
            uint32_t invw = L11_DIV(F_ONE, l11_u32(o, sp - 0x10 + 12));
            uint8_t col[16];
            l11_q(o, 0x70003600u, col);
            l11_wq(o, out + 0x10, col);
            l11_w32(o, out + 0x00, L11_MUL(l11_u32(o, src + 0x30), invw));
            l11_w32(o, out + 0x04, L11_MUL(l11_u32(o, src + 0x34), invw));
            l11_w32(o, out + 0x08, invw);
            l11_w32(o, out + 0x0C, F_ONE);
        }
    }
    uint32_t r = 0;
    if (l11_c_001CB950(o, page, key, tex0, &r)) return;
    l11_c_001CB900(o, page, key, mode);
}

int em_level11_port_001CDDC0(const EmLevel11PortHooks *h, int32_t layer, int32_t mode, uint32_t corners, uint64_t tex0,
                             uint32_t rgba, uint32_t sp, EmLevel11PortFault *fault)
{
    L11 o;
    if (l11_begin(&o, h, fault)) return -1;
    l11_001CDDC0(&o, layer, mode, corners, tex0, rgba, sp);
    return l11_end(&o);
}
