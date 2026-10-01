/* The creature at AREA04's lift (behaviour 0012E3A0, spawned by [3]'s first
 * stage 0x824830 with group 0x826790) and its state functions (see
 * em_level8_port.h, docs/LEVEL8_PORT.md): 0012E3A0, 0012E560, 0012E840,
 * 0012EB60, 0012F100, 0012FC10, 00131ED0, 00131F90, 00132490, 001328D0,
 * 00132FB0, 001333F0, 00133640, 00133A20, 00133DB0, 0011E0A8, 0021BE40,
 * 001B4810. `ent` is the creature's +0x1F0 block. The rules of
 * em_level8_port_lift.c apply; functions whose original (or a callee's)
 * frame holds locals take the stack pointer and pass each callee the
 * original's inner stack pointer.
 */
#include "em_level8_port_internal.h"

#define S_70003B8D 0x70003B8Du /* the pause / cinematic byte */
#define D_0081070A 0x0081070Au /* the difficulty byte of the hit points */
#define D_00810350 0x00810350u /* the player's position */
#define D_00810360 0x00810360u /* the player's position (the target) */
#define F_PI_4 0x3F490FDBu     /* pi / 4 */
#define F_10 0x41200000u

static void l8_clip(L8 *o, uint32_t self, int32_t clip, uint32_t blend)
{
    l8_c_001C67E0(o, self, clip, fl(blend), fl(F_ZERO));
}

static int l8_sound(L8 *o, uint32_t self, int32_t id)
{
    int32_t ignored = 0;
    return l8_c_001FBD50(o, self, id, 0, fl(F_300), &ignored);
}

static int32_t l8_rand(L8 *o)
{
    int32_t r = 0;
    l8_c_00122BB8(o, &r);
    return r;
}

/* ------------------------------------------------------------------------
 * 0011E0A8 (word asm; from the instructions): modff. *ptr = the integral
 * part of x (the sign alone below 1, x itself at exponent 23 or more, the
 * mantissa bits below the binary point cleared otherwise); the result is
 * the fraction x - *ptr (read back), the sign alone for an integral x or a
 * large one, and x itself below 1.
 * ---------------------------------------------------------------------- */
uint32_t l8_0011E0A8(L8 *o, uint32_t ptr, uint32_t x)
{
    int32_t e = (int32_t)((x >> 23) & 0xFFu) - 0x7F;
    if (!(e < 0x17)) {
        l8_w32(o, ptr, x);
        return x & 0x80000000u;
    }
    if (e < 0) {
        l8_w32(o, ptr, x & 0x80000000u);
        return x;
    }
    uint32_t mask = 0x007FFFFFu >> e;
    if ((x & mask) == 0) {
        l8_w32(o, ptr, x);
        return x & 0x80000000u;
    }
    l8_w32(o, ptr, x & ~mask);
    return L8_SUB(x, l8_u32(o, ptr));
}

int em_level8_port_0011E0A8(const EmLevel8PortHooks *h, uint32_t ptr, float x, float *result, EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    uint32_t r = l8_0011E0A8(&o, ptr, l8_bits(x));
    if (l8_failed(&o)) return -1;
    *result = fl(r);
    return 0;
}

/* ------------------------------------------------------------------------
 * 0021BE40 (inline asm; from the instructions): 1 unless every test
 * passes: the byte 0x70003B8D zero, self +0x220 above 0, self +0 == 1,
 * self +4 == 1, 0021BB00(self) zero and the halfword +0x20E zero (then 0).
 * ---------------------------------------------------------------------- */
int32_t l8_0021BE40(L8 *o, uint32_t self)
{
    if (l8_u8(o, S_70003B8D) != 0) return 1;
    if (L8_LE(l8_u32(o, self + 0x220), F_ZERO)) return 1;
    if (l8_u8(o, self) != 1) return 1;
    if (l8_u8(o, self + 4) != 1) return 1;
    int32_t r = 0;
    if (l8_c_0021BB00(o, self, &r)) return 0;
    if (r != 0) return 1;
    return l8_s16(o, self + 0x20E) == 0 ? 0 : 1;
}

int em_level8_port_0021BE40(const EmLevel8PortHooks *h, uint32_t self, int32_t *result, EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_0021BE40(&o, self);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 00131ED0 (C byte-identical): unless self +5 is 0x63, the pose block's
 * first record (*(*D_00275B40)) gets 0 at +0, +8, +0xC and +0x14 (both
 * pointers read again for each).
 * ---------------------------------------------------------------------- */
void l8_00131ED0(L8 *o, uint32_t self)
{
    if (l8_u8(o, self + 5) == 0x63) return;
    static const uint32_t offs[4] = {0, 8, 0xC, 0x14};
    for (int k = 0; k < 4; k++) {
        uint32_t g = l8_u32(o, D_00275B40);
        uint32_t rec = l8_u32(o, g);
        l8_w32(o, rec + offs[k], 0);
    }
}

int em_level8_port_00131ED0(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_00131ED0(&o, self);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 00133DB0 (word asm; from the instructions): unless self +0x0D bit 7 or
 * ent's halfword +0x5C or byte +0x61 is set or ent's halfword +0x56 is 0:
 * with self +4 == 1, the halfword +0x54 nonzero and self +5 below 3 (signed
 * byte compare of the unsigned byte), self +5 = 8 and +6 = 0.
 * ---------------------------------------------------------------------- */
void l8_00133DB0(L8 *o, uint32_t self, uint32_t ent)
{
    if (l8_u8(o, self + 0x0D) & 0x80u) return;
    if (l8_u16(o, ent + 0x5C) != 0) return;
    if (l8_u8(o, ent + 0x61) != 0) return;
    if (l8_u16(o, ent + 0x56) == 0) return;
    if (l8_u8(o, self + 4) != 1) return;
    if (l8_s16(o, self + 0x54) == 0) return;
    if (!((int32_t)l8_u8(o, self + 5) < 3)) return;
    l8_w8(o, self + 5, 8);
    l8_w8(o, self + 6, 0);
}

int em_level8_port_00133DB0(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_00133DB0(&o, self, ent);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 001B4810 (C byte-identical): the bite light of a creature by its type
 * +3: a point (w 1.0) at 0x70003600 and a bone index per type (and +0x0D
 * bits 0 / 7), the point through that bone of the pose block
 * (*D_00275B40)[index] +0x90 (001026A0 in place), the colour 0x70003610 =
 * (0x80, 0x50, 0x30, 0x80) with +0x0D bit 7, else (0x30, 0x80, 0x30,
 * 0x80), and 001F4E20(0x70003600, 0x70003610, radius). Type 3 first copies
 * self +0xC0 to 0x70003610 (00102948). Types 0, 2, 8 and 12 and up set
 * neither the index nor the radius: the original then uses the caller's
 * s0 and f20; the translation faults 7 after reading the type.
 * ---------------------------------------------------------------------- */
static void l8_point(L8 *o, uint32_t x, uint32_t y, uint32_t z)
{
    l8_w32(o, S_70003600, x);
    l8_w32(o, S_70003600 + 4u, y);
    l8_w32(o, S_70003600 + 8u, z);
    l8_w32(o, S_70003600 + 12u, F_ONE);
}

void l8_001B4810(L8 *o, uint32_t self)
{
    uint32_t type = l8_u8(o, self + 3);
    uint32_t radius = F_5, index = 0;
    switch (type) {
    case 1: {
        uint32_t flags = l8_u8(o, self + 0x0D);
        index = 4;
        if (flags & 1u) {
            if (flags & 0x80u) l8_point(o, 0x40091687u, 0x3FFBE76Du, 0xBD71A9FCu);
            else l8_point(o, 0x4009BA5Eu, 0x3FFA1CACu, 0xBD48B439u);
        } else if (flags & 0x80u) {
            l8_point(o, 0x400B53F8u, 0x3FF2B021u, 0);
        } else {
            l8_point(o, 0x4003D70Au, 0x4006F9DBu, 0);
        }
        break;
    }
    case 3:
        radius = F_8;
        l8_point(o, 0x3F8B645Au, 0x3F50E560u, 0);
        if (l8_c_00102948(o, 0x70003610u, self + 0xC0)) return;
        index = 2;
        break;
    case 4:
        l8_point(o, 0x3EAB851Fu, 0x3F11A9FCu, 0);
        index = 2;
        break;
    case 5:
        l8_point(o, 0x4018E560u, 0xBF27AE14u, 0);
        index = 0x0D;
        break;
    case 6:
        index = 5;
        if (l8_u8(o, self + 0x0D) & 0x80u) l8_point(o, 0xBEA2D0E5u, 0xC02ED917u, 0);
        else l8_point(o, 0, 0xC001999Au, 0);
        break;
    case 7:
        index = 0x0B;
        if (l8_u8(o, self + 0x0D) & 0x80u) l8_point(o, 0x404147AEu, 0x3EE147AEu, 0);
        else l8_point(o, 0x4001BA5Eu, 0, 0);
        break;
    case 9:
        l8_point(o, 0x401B79A7u, 0xBFF367A1u, 0xBE30A3D7u);
        index = 0x1C;
        break;
    case 10:
        radius = F_20;
        l8_point(o, 0x41EE7EFAu, 0xBF6F5C29u, 0);
        index = 0x10;
        break;
    case 11:
        radius = F_20;
        l8_point(o, 0x4138C8B4u, 0x4185E148u, 0xBC03126Fu);
        index = 2;
        break;
    default:
        l8_latch(o, 0x001B4810u, EM_LEVEL8_PORT_FAULT_REGISTER);
        return;
    }
    uint32_t g = l8_u32(o, D_00275B40);
    uint32_t bone = l8_u32(o, g + (index << 2));
    if (l8_c_001026A0(o, S_70003600, bone + 0x90, S_70003600)) return;
    if (l8_u8(o, self + 0x0D) & 0x80u) {
        l8_w32(o, 0x70003610u, 0x80);
        l8_w32(o, 0x70003614u, 0x50);
        l8_w32(o, 0x70003618u, 0x30);
        l8_w32(o, 0x7000361Cu, 0x80);
    } else {
        l8_w32(o, 0x70003610u, 0x30);
        l8_w32(o, 0x70003614u, 0x80);
        l8_w32(o, 0x70003618u, 0x30);
        l8_w32(o, 0x7000361Cu, 0x80);
    }
    l8_c_001F4E20(o, S_70003600, 0x70003610u, fl(radius));
}

int em_level8_port_001B4810(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_001B4810(&o, self);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 001333F0 (C byte-identical): the creature's approach test; returns 1
 * when it should go for the player (see the decomp's header). Flags +0x0D
 * bit 1: self +0xB4 below 50 resets the pose (ent +0x6A = 0, self +0x36 =
 * +0x34 + 1, +0xB0.. = the canned point) and returns 0; else with
 * D_00810354 at 47 or more and 001B1EA0(0, D_00810360, 0x245020, 0x10)
 * nonzero: ent +0x56 = 600, returns 1; otherwise ent +0x56 = 0, returns 0.
 * With ent +0x56 running: range ent +0x44 within 60 clears the counter
 * +0x6F, else it counts to 120 and clears both; returns 0. Self +0x0A bit
 * 0: ent +0x56 = 120, counter 0, returns 1. Otherwise the bearing to the
 * target (self +0x13C): 001B3F10(self, 001B1470(0011E620(t x, t z) -
 * pi/2), 20) nonzero counts +0x6F up to the difficulty limit
 * 0x275398[D_0081050C & 3] (then +0x56 = 120, counter 0, returns 1); zero
 * clears the counter.
 * ---------------------------------------------------------------------- */
int32_t l8_001333F0(L8 *o, uint32_t self, uint32_t ent)
{
    uint32_t flags = l8_u8(o, self + 0x0D);
    if (flags & 2u) {
        if (L8_LT(l8_u32(o, self + 0xB4), 0x42480000u)) {
            l8_w8(o, ent + 0x6A, 0);
            l8_w16(o, self + 0x36, (uint32_t)l8_s16(o, self + 0x34) + 1u);
            l8_w32(o, self + 0xB0, 0x44079333u);
            l8_w32(o, self + 0xB4, 0x425C0000u);
            l8_w32(o, self + 0xB8, 0x438A3333u);
            l8_w32(o, self + 0xBC, F_ONE);
        } else if (!L8_LT(l8_u32(o, 0x00810354u), 0x423C0000u)) {
            int32_t r = 0;
            if (l8_c_001B1EA0(o, 0, D_00810360, 0x00245020u, 0x10, &r)) return 0;
            if (r) {
                l8_w16(o, ent + 0x56, 0x258);
                return 1;
            }
        }
        l8_w16(o, ent + 0x56, 0);
        return 0;
    }
    if (l8_u16(o, ent + 0x56) != 0) {
        if (L8_LE(l8_u32(o, ent + 0x44), 0x42700000u)) {
            l8_w8(o, ent + 0x6F, 0);
            return 0;
        }
        uint32_t c = l8_u8(o, ent + 0x6F) + 1u;
        l8_w8(o, ent + 0x6F, c);
        if ((int32_t)(c & 0xFFu) < 0x78) return 0;
        l8_w16(o, ent + 0x56, 0);
        l8_w8(o, ent + 0x6F, 0);
        return 0;
    }
    if (l8_u8(o, self + 0x0A) & 1u) {
        l8_w16(o, ent + 0x56, 0x78);
        l8_w8(o, ent + 0x6F, 0);
        return 1;
    }
    uint32_t target = l8_u32(o, self + 0x13C);
    uint32_t tz = l8_u32(o, target + 0xB8);
    uint32_t tx = l8_u32(o, target + 0xB0);
    float atan = 0.0f;
    if (l8_c_0011E620(o, fl(tx), fl(tz), &atan)) return 0;
    float bearing = 0.0f;
    if (l8_angle(o, fl(L8_SUB(l8_bits(atan), 0x3FC90FDBu)), &bearing)) return 0;
    int32_t facing = 0;
    if (l8_c_001B3F10(o, self, bearing, fl(F_20), &facing)) return 0;
    if (!facing) {
        l8_w8(o, ent + 0x6F, 0);
        return 0;
    }
    uint32_t c = l8_u8(o, ent + 0x6F) + 1u;
    l8_w8(o, ent + 0x6F, c);
    uint32_t level = l8_u8(o, 0x0081050Cu) & 3u;
    uint32_t limit = l8_u8(o, 0x00275398u + level);
    if ((int32_t)(c & 0xFFu) < (int32_t)limit) return 0;
    l8_w16(o, ent + 0x56, 0x78);
    l8_w8(o, ent + 0x6F, 0);
    return 1;
}

int em_level8_port_001333F0(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, int32_t *result,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001333F0(&o, self, ent);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 00133A20 (C byte-identical): the creature takes a hit (self halfword
 * +0x36): returns 1 when it dies or changes state, 0 otherwise (see the
 * decomp's header for the fields). With ent +0x6A or +0x65 set the hit is
 * dropped. Otherwise +0 = 2, ent +0x56 = 120, ent +0x5A = 0, the spark
 * 0x80000027 (001EFE00) once per 60 frames for bit 0x4000; the burst test
 * (ent +0x61 with +0x2C == 4 and 001C6160(self) - +0x3C below 65); the
 * hit accumulates into ent +0x67 (the byte of +0x36, & 0xFFF) for 20
 * frames; the damage (& 0xFFF, x5 with bit 0x8000) against the hit points
 * +0x34: dead -> +0 = 2, +0x34 = 0, +4 = 2, +5 = +6 = 0, 001B4CF0(self),
 * +5 = 1 unless (ent +0x60 bit 1 and not bit 0x2000); alive -> bit 0x2000
 * or 25 accumulated or bit 0x8000 knock it into state 2; bit 0x5000 alone
 * sets ent +0x6A = 30; +0 = 1, +0x36 = 0.
 * ---------------------------------------------------------------------- */
int32_t l8_00133A20(L8 *o, uint32_t self, uint32_t ent)
{
    if (l8_s16(o, self + 0x36) == 0) return 0;
    if (l8_u8(o, ent + 0x6A) != 0 || l8_u8(o, ent + 0x65) != 0) {
        l8_w16(o, self + 0x36, 0);
        return 0;
    }
    l8_w8(o, self, 2);
    l8_w16(o, ent + 0x56, 0x78);
    l8_w16(o, ent + 0x5A, 0);
    if (l8_u8(o, ent + 0x6C) == 0 && (l8_s16(o, self + 0x36) & 0x4000)) {
        l8_w8(o, ent + 0x6C, 0x3C);
        if (l8_c_001EFE00(o, (int32_t)0x80000027u, self)) return 0;
    }
    uint32_t burst = l8_u8(o, ent + 0x61);
    if (burst != 0 && l8_u8(o, self + 0x2C) == 4) {
        int32_t n = 0;
        if (l8_c_001C6160(o, self, &n)) return 0;
        uint32_t left = L8_SUB(L8_CVT((uint32_t)n), l8_u32(o, self + 0x3C));
        if (!L8_LT(left, 0x42820000u)) burst = 0;
    }
    if (l8_u8(o, ent + 0x66) != 0) {
        uint32_t add = l8_u8(o, self + 0x36) & 0xFFFu;
        uint32_t sum = l8_u8(o, ent + 0x67) + add;
        l8_w8(o, ent + 0x67, sum);
    } else {
        l8_w8(o, ent + 0x67, l8_u8(o, self + 0x36) & 0xFFFu);
    }
    l8_w8(o, ent + 0x66, 0x14);
    int32_t hit = l8_s16(o, self + 0x36);
    int32_t damage = (int16_t)(hit & 0xFFF);
    if (hit & 0x8000) damage = (int16_t)(uint16_t)((uint32_t)damage * 5u);
    int32_t hp = l8_s16(o, self + 0x34);
    if (!(damage < hp)) {
        l8_w8(o, self, 2);
        l8_w16(o, self + 0x34, 0);
        l8_w8(o, self + 4, 2);
        l8_w8(o, self + 5, 0);
        l8_w8(o, self + 6, 0);
        if (l8_c_001B4CF0(o, self)) return 0;
        if (burst) return 1;
        if ((l8_u8(o, ent + 0x60) & 2u) && !(l8_s16(o, self + 0x36) & 0x2000)) return 1;
        l8_w8(o, self + 5, 1);
        return 1;
    }
    l8_w16(o, self + 0x34, (uint32_t)(hp - damage));
    int32_t again = l8_s16(o, self + 0x36);
    if (again & 0x2000) {
        l8_w8(o, self + 4, 2);
        l8_w8(o, self + 6, 0);
        l8_w8(o, ent + 0x6B, 0);
        l8_w8(o, ent + 0x67, 0);
        l8_w8(o, self + 5, burst ? 0u : 1u);
        return 1;
    }
    if (!((int32_t)l8_u8(o, ent + 0x67) < 0x19) || (again & 0x8000)) {
        l8_w8(o, self + 4, 2);
        l8_w8(o, self + 6, 0);
        l8_w8(o, ent + 0x67, 0);
        l8_w8(o, ent + 0x6B, 0);
        l8_w8(o, self + 5, (l8_u8(o, ent + 0x60) & 2u) ? 0u : 1u);
        return 1;
    }
    if (again & 0x5000) l8_w8(o, ent + 0x6A, 0x1E);
    l8_w8(o, self, 1);
    l8_w16(o, self + 0x36, 0);
    return 0;
}

int em_level8_port_00133A20(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, int32_t *result,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_00133A20(&o, self, ent);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 00133640 (NEARMISS; from the instructions): the lunge test; returns 1
 * when it starts a lunge state (+5 = 5, 6 or 7), else 0. Nothing with ent
 * +0x5A or +0x61 set, a clip +0x2C of 3 or 4, or 0021BE40(player) nonzero.
 * The vertical probe self -> player (both +10 up, 0019A570(.., 6, 0))
 * must miss; |dy| (0011DF78) under 13 unless +0x0D bit 1; the flat
 * distance (001B15D0, stored at 0x70003A20) at most 50; facing within
 * pi/4 (001B1560). Within 24: a table 0x245120 roll ((rand >> 4) & 0xF)
 * with neither bit 1 nor 0021BED0(player) gives +5 = 7; else |dy| under 13
 * gives +5 = 5 with ent +0x64 = (rand >> 11) & 1, else +5 = 5 with +0x64
 * = 2. Beyond 24: the roll of table 0x245130 ((rand >> 17) & 0xF): 0 ->
 * nothing; 2 with bit 0 -> +5 = 6; otherwise +5 = 5, +0x64 = 2. Each
 * start clears ent +0x63 and +6.
 * ---------------------------------------------------------------------- */
static int32_t l8_lunge(L8 *o, uint32_t self, uint32_t ent, uint32_t state, int32_t kind)
{
    l8_w8(o, ent + 0x63, 0);
    l8_w8(o, self + 5, state);
    l8_w8(o, self + 6, 0);
    if (kind >= 0) l8_w8(o, ent + 0x64, (uint32_t)kind);
    return 1;
}

int32_t l8_00133640(L8 *o, uint32_t self, uint32_t ent)
{
    if (l8_u16(o, ent + 0x5A) != 0) return 0;
    if (l8_u8(o, ent + 0x61) != 0) return 0;
    int32_t clip = l8_s16(o, self + 0x2C);
    if (clip == 3 || clip == 4) return 0;
    if (l8_0021BE40(o, D_008102B0)) return 0;
    if (l8_failed(o)) return 0;
    uint32_t px = l8_u32(o, D_00810350);
    uint32_t sx = l8_u32(o, self + 0xB0);
    uint32_t py = l8_u32(o, D_00810350 + 4u);
    uint32_t pz = l8_u32(o, D_00810350 + 8u);
    l8_w32(o, 0x700038A0u, sx);
    l8_w32(o, 0x700038A4u, l8_u32(o, self + 0xB4));
    l8_w32(o, 0x700038A8u, l8_u32(o, self + 0xB8));
    l8_w32(o, 0x700038B0u, px);
    l8_w32(o, 0x700038B4u, py);
    l8_w32(o, 0x700038B8u, pz);
    l8_w32(o, 0x700038ACu, F_ONE);
    l8_w32(o, 0x700038BCu, F_ONE);
    uint32_t ay = l8_u32(o, 0x700038A4u);
    uint32_t by = l8_u32(o, 0x700038B4u);
    l8_w32(o, 0x700038A4u, L8_ADD(ay, F_10));
    l8_w32(o, 0x700038B4u, L8_ADD(by, F_10));
    int32_t hit = 0;
    if (l8_c_0019A570(o, 0x700038A0u, 0x700038B0u, 6, 0, &hit)) return 0;
    if (hit) return 0;
    uint32_t dy = L8_SUB(l8_u32(o, 0x700038A4u), l8_u32(o, 0x700038B4u));
    float mag = 0.0f;
    if (l8_c_0011DF78(o, fl(dy), &mag)) return 0;
    uint32_t f20 = l8_bits(mag);
    if (!(l8_u8(o, self + 0x0D) & 2u) && !L8_LT(f20, 0x41500000u)) return 0;
    l8_w32(o, 0x700038B4u, 0);
    l8_w32(o, 0x700038A4u, 0);
    float dist = 0.0f;
    if (l8_c_001B15D0(o, 0x700038A0u, 0x700038B0u, &dist)) return 0;
    int near = L8_LE(l8_bits(dist), 0x42480000u);
    l8_w32(o, 0x70003A20u, l8_bits(dist));
    if (!near) return 0;
    if (!l8_001B1560(o, self, D_00810360, F_PI_4)) return 0;
    if (L8_LE(l8_u32(o, 0x70003A20u), 0x41C00000u)) {
        int32_t r = l8_rand(o);
        uint32_t roll = l8_u8(o, 0x00245120u + ((uint32_t)l8_sra((uint32_t)r, 4) & 0xFu));
        if (roll != 0 && !(l8_u8(o, self + 0x0D) & 2u)) {
            int32_t busy = 0;
            if (l8_c_0021BED0(o, D_008102B0, &busy)) return 0;
            if (!busy) return l8_lunge(o, self, ent, 7, -1);
        }
        if (!L8_LT(f20, 0x41500000u)) {
            l8_w8(o, ent + 0x63, 0);
            l8_w8(o, self + 5, 5);
            l8_w8(o, self + 6, 0);
            l8_w8(o, ent + 0x64, 2);
            return 1;
        }
        int32_t r2 = l8_rand(o);
        l8_w8(o, ent + 0x64, (uint32_t)l8_sra((uint32_t)r2, 11) & 1u);
        return l8_lunge(o, self, ent, 5, -1);
    }
    int32_t r = l8_rand(o);
    uint32_t roll = l8_u8(o, 0x00245130u + ((uint32_t)l8_sra((uint32_t)r, 17) & 0xFu));
    if (roll == 0) return 0;
    if (roll == 2 && (l8_u8(o, self + 0x0D) & 1u)) return l8_lunge(o, self, ent, 6, -1);
    l8_w8(o, ent + 0x63, 0);
    l8_w8(o, self + 5, 5);
    l8_w8(o, self + 6, 0);
    l8_w8(o, ent + 0x64, 2);
    return 1;
}

int em_level8_port_00133640(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, int32_t *result,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_00133640(&o, self, ent);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 00131F90 (NEARMISS; from the instructions): the turn toward ent +0x30
 * (frame 0x60, the local at frame +0x5C). The turn rate w (degrees a
 * frame) is the word 0x244FD8 with ent +0x62 set, 4 + 4 (rand >> 14 & 0xFF)
 * / 255 with ent +0x56 running, else 0x244FD0. Already facing: ent +0x63
 * = 0. With ent +0x56 or +0x62 set, a clip other than 7, or ent +0x60 bit 2:
 * ent +0x63 = 0 and yaw = 001B12B0(target, yaw, pi w / 180). Otherwise the
 * frames of the clip left (001C6160(self) - (int) modf(+0x3C)) pick the side
 * step: d = 001B1470(target - yaw); d > 0 needs 00128250(left) in 23..47,
 * d <= 0 below 11 or 56 and up (unsigned; outside: ent +0x63 =
 * 0); the yaw steps by pi w / 180 toward the target (or lands on it), is
 * normalised, the matrix +0xD0 rebuilt (001029C0, 00102C58); the side
 * (ent +0x63 = 1 / 2, set once) places ent +0x10 = self +0xB0 + the table
 * row 0x242F50 / 0x243180 [00128250(left)] turned by +0xD0, and
 * self +0xB0 = self +0xB0 - (the negated row turned by +0xD0) + ent +0x10.
 * ---------------------------------------------------------------------- */
static void l8_side_step(L8 *o, uint32_t self, uint32_t ent, uint32_t frame_local, uint32_t row_x, uint32_t side)
{
    if (l8_u8(o, ent + 0x63) != side) {
        l8_w8(o, ent + 0x63, side);
        int32_t n = 0;
        if (l8_c_00128250(o, fl(l8_u32(o, frame_local)), &n)) return;
        uint32_t at = (uint32_t)n << 3;
        l8_w32(o, 0x700038A0u, l8_u32(o, row_x + at));
        l8_w32(o, 0x700038A4u, 0);
        l8_w32(o, 0x700038A8u, l8_u32(o, row_x + 4u + at));
        if (l8_c_001026A0(o, ent + 0x10, self + 0xD0, 0x700038A0u)) return;
        if (l8_c_001028B8(o, ent + 0x10, self + 0xB0, ent + 0x10)) return;
    }
    int32_t n = 0;
    if (l8_c_00128250(o, fl(l8_u32(o, frame_local)), &n)) return;
    uint32_t at = (uint32_t)n << 3;
    l8_w32(o, 0x700038A0u, L8_NEG(l8_u32(o, row_x + at)));
    l8_w32(o, 0x700038A4u, 0);
    l8_w32(o, 0x700038A8u, L8_NEG(l8_u32(o, row_x + 4u + at)));
    if (l8_c_001026A0(o, 0x700038B0u, self + 0xD0, 0x700038A0u)) return;
    l8_c_001028B8(o, self + 0xB0, ent + 0x10, 0x700038B0u);
}

static void l8_turn_to(L8 *o, uint32_t self, uint32_t ent, uint32_t rate, int toward_plus, uint32_t d)
{
    uint32_t limit = L8_DIV(L8_MUL(F_PI, rate), 0x43340000u);
    uint32_t mag = toward_plus ? d : L8_NEG(d);
    if (L8_LT(mag, limit)) {
        l8_w32(o, self + 0xC4, l8_u32(o, ent + 0x30));
    } else {
        uint32_t yaw = l8_u32(o, self + 0xC4);
        l8_w32(o, self + 0xC4, toward_plus ? L8_ADD(yaw, limit) : L8_SUB(yaw, limit));
    }
    float r = 0.0f;
    if (l8_angle(o, fl(l8_u32(o, self + 0xC4)), &r)) return;
    l8_w32(o, self + 0xC4, l8_bits(r));
    if (l8_c_001029C0(o, self + 0xD0)) return;
    l8_c_00102C58(o, self + 0xD0, self + 0xD0, self + 0xC0);
}

void l8_00131F90(L8 *o, uint32_t self, uint32_t ent, uint32_t sp)
{
    uint32_t local = sp - 0x60u + 0x5Cu;
    uint32_t rate;
    if (l8_u8(o, ent + 0x62) != 0) {
        rate = l8_u32(o, 0x00244FD8u);
    } else if (l8_u16(o, ent + 0x56) != 0) {
        int32_t r = l8_rand(o);
        uint32_t b = L8_CVT((uint32_t)l8_sra((uint32_t)r, 14) & 0xFFu);
        rate = L8_ADD(0x40800000u, L8_MUL(0x40800000u, L8_DIV(b, 0x437F0000u)));
    } else {
        rate = l8_u32(o, 0x00244FD0u);
    }
    if (l8_failed(o)) return;
    uint32_t yaw = l8_u32(o, self + 0xC4);
    uint32_t goal = l8_u32(o, ent + 0x30);
    if (L8_EQ(yaw, goal)) {
        l8_w8(o, ent + 0x63, 0);
        return;
    }
    int quick = l8_u16(o, ent + 0x56) != 0;
    if (!quick) quick = l8_u8(o, ent + 0x62) != 0;
    if (!quick) quick = l8_s16(o, self + 0x2C) != 7 || (l8_u8(o, ent + 0x60) & 4u) != 0;
    if (quick) {
        l8_w8(o, ent + 0x63, 0);
        uint32_t lim = L8_MUL(F_PI, rate);
        uint32_t t = l8_u32(o, ent + 0x30);
        uint32_t y = l8_u32(o, self + 0xC4);
        float r = 0.0f;
        if (l8_c_001B12B0(o, fl(t), fl(y), fl(L8_DIV(lim, 0x43340000u)), &r)) return;
        l8_w32(o, self + 0xC4, l8_bits(r));
        return;
    }
    l8_0011E0A8(o, local, l8_u32(o, self + 0x3C));
    int32_t frames = 0;
    if (l8_c_001C6160(o, self, &frames)) return;
    int32_t whole = 0;
    if (l8_c_001281C0(o, fl(l8_u32(o, local)), &whole)) return;
    /* The frames left, stored in the frame word; the original reads it back
     * into the callee-saved f22 (a frame access the reference harness
     * files with the register saves), so the translation keeps it in C
     * for the two range tests and reads the word where the original loads
     * it into an argument register. */
    const uint32_t left = L8_CVT((uint32_t)(frames - whole));
    l8_w32(o, local, left);
    uint32_t t = l8_u32(o, ent + 0x30);
    uint32_t y = l8_u32(o, self + 0xC4);
    float dr = 0.0f;
    if (l8_angle(o, fl(L8_SUB(t, y)), &dr)) return;
    uint32_t d = l8_bits(dr);
    if (!L8_LE(d, F_ZERO)) {
        int32_t n = 0;
        if (l8_c_00128250(o, fl(left), &n)) return;
        if ((uint32_t)n < 0x17u) goto stop;
        if (l8_c_00128250(o, fl(left), &n)) return;
        if (!((uint32_t)n < 0x30u)) goto stop;
        l8_turn_to(o, self, ent, rate, 1, d);
        if (l8_failed(o)) return;
        l8_side_step(o, self, ent, local, 0x00242F50u, 1);
        return;
    }
    {
        int32_t n = 0;
        if (l8_c_00128250(o, fl(left), &n)) return;
        if (!((uint32_t)n < 0x0Bu)) {
            if (l8_c_00128250(o, fl(left), &n)) return;
            if ((uint32_t)n < 0x38u) goto stop;
        }
        l8_turn_to(o, self, ent, rate, 0, d);
        if (l8_failed(o)) return;
        l8_side_step(o, self, ent, local, 0x00243180u, 2);
        return;
    }
stop:
    l8_w8(o, ent + 0x63, 0);
}

int em_level8_port_00131F90(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_00131F90(&o, self, ent, sp);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 00132490 (NEARMISS; from the instructions): the creature's motion step
 * (frame 0x60, locals at frame +0x58 / +0x5C). ent +0x38 -= 0.1, self
 * +0xB4 += ent +0x38. When the range ent +0x44 (as a double, 00128350 /
 * 001000E0) is at most 11.5 and the facing is within 65 degrees (001B1560):
 * self +0x52 |= 1, done. Otherwise the clip +0x2C picks a speed table (22
 * clips) or none: none moves self +0xB0 / +0xB8 by ent +0x3C along the
 * yaw (0011E2A8 / 0011DE90); a table walks the frames from the clip's
 * position (modf of left = 001C6160(self) - +0x3C, and of left + 1 -
 * ent +0x34) setting ent +0x3C = table[(int)left] and stepping the same
 * way each frame.
 * ---------------------------------------------------------------------- */
static uint32_t l8_speed_table(int32_t clip)
{
    switch (clip) {
    case 0x28: return 0x00244C30u;
    case 0x21: return 0x00244AF0u;
    case 0x20: return 0x00244A50u;
    case 0x1D: return 0x00244800u;
    case 0x1C: return 0x00244580u;
    case 0x1B: return 0x00244490u;
    case 0x1A: return 0x002442D0u;
    case 0x19: return 0x002441B0u;
    case 0x18: return 0x00244130u;
    case 0x17: return 0x00243FF0u;
    case 0x14: return 0x00243E60u;
    case 0x13: return 0x00243DA0u;
    case 0x12: return 0x00243CB0u;
    case 0x11: return 0x00243B90u;
    case 0x10: return 0x00243A60u;
    case 0x0F: return 0x002439A0u;
    case 0x0E: return 0x002438E0u;
    case 0x0A: return 0x002437D0u;
    case 0x09: return 0x00243710u;
    case 0x08: return 0x00243660u;
    case 0x07: return 0x00243540u;
    case 0x02: return 0x002433B0u;
    default: return 0;
    }
}

static void l8_step_along(L8 *o, uint32_t self, uint32_t ent)
{
    float s = 0.0f;
    if (l8_c_0011E2A8(o, fl(l8_u32(o, self + 0xC4)), &s)) return;
    uint32_t v = l8_u32(o, ent + 0x3C);
    uint32_t x = l8_u32(o, self + 0xB0);
    l8_w32(o, self + 0xB0, L8_ADD(x, L8_MUL(v, l8_bits(s))));
    float c = 0.0f;
    if (l8_c_0011DE90(o, fl(l8_u32(o, self + 0xC4)), &c)) return;
    uint32_t v2 = l8_u32(o, ent + 0x3C);
    uint32_t z = l8_u32(o, self + 0xB8);
    l8_w32(o, self + 0xB8, L8_ADD(z, L8_MUL(v2, l8_bits(c))));
}

void l8_00132490(L8 *o, uint32_t self, uint32_t ent, uint32_t sp)
{
    uint32_t fr = sp - 0x60u;
    uint32_t fall = L8_ADD(l8_u32(o, ent + 0x38), 0xBDCCCCCDu);
    l8_w32(o, ent + 0x38, fall);
    l8_w32(o, self + 0xB4, L8_ADD(l8_u32(o, self + 0xB4), fall));
    uint64_t range = 0;
    if (l8_c_00128350(o, fl(l8_u32(o, ent + 0x44)), &range)) return;
    int32_t close = 0;
    if (l8_c_001000E0(o, range, (uint64_t)0x40270000u << 32, &close)) return;
    if (close) {
        if (l8_001B1560(o, self, D_00810360, 0x3F91361Eu)) {
            l8_w16(o, self + 0x52, l8_u16(o, self + 0x52) | 1u);
            return;
        }
        if (l8_failed(o)) return;
    }
    uint32_t table = l8_speed_table(l8_s16(o, self + 0x2C));
    if (!table) {
        if (L8_EQ(F_ZERO, l8_u32(o, ent + 0x3C))) return;
        l8_step_along(o, self, ent);
        return;
    }
    int32_t frames = 0;
    if (l8_c_001C6160(o, self, &frames)) return;
    uint32_t left = L8_SUB(L8_CVT((uint32_t)frames), l8_u32(o, self + 0x3C));
    l8_0011E0A8(o, fr + 0x58, left);
    uint32_t pace = l8_u32(o, ent + 0x34);
    l8_0011E0A8(o, fr + 0x5C, L8_ADD(F_ONE, L8_SUB(left, pace)));
    uint32_t a = l8_u32(o, fr + 0x58);
    uint32_t b = l8_u32(o, fr + 0x5C);
    uint32_t n = L8_SUB(a, b);
    int done = L8_LT(n, F_ZERO);
    l8_w32(o, fr + 0x58, n);
    if (done) return;
    for (;;) {
        int32_t k = 0;
        if (l8_c_001281C0(o, fl(left), &k)) return;
        l8_w32(o, ent + 0x3C, l8_u32(o, table + ((uint32_t)k << 2)));
        float s = 0.0f;
        if (l8_c_0011E2A8(o, fl(l8_u32(o, self + 0xC4)), &s)) return;
        uint32_t v = l8_u32(o, ent + 0x3C);
        uint32_t x = l8_u32(o, self + 0xB0);
        l8_w32(o, self + 0xB0, L8_ADD(x, L8_MUL(v, l8_bits(s))));
        float c = 0.0f;
        if (l8_c_0011DE90(o, fl(l8_u32(o, self + 0xC4)), &c)) return;
        uint32_t v2 = l8_u32(o, ent + 0x3C);
        uint32_t z = l8_u32(o, self + 0xB8);
        left = L8_SUB(left, F_ONE);
        uint32_t nz = L8_ADD(z, L8_MUL(v2, l8_bits(c)));
        int wrap = L8_LT(left, F_ZERO);
        l8_w32(o, self + 0xB8, nz);
        if (wrap) {
            int32_t f2 = 0;
            if (l8_c_001C6160(o, self, &f2)) return;
            left = L8_CVT((uint32_t)(f2 - 1));
        }
        uint32_t m = L8_SUB(l8_u32(o, fr + 0x58), F_ONE);
        int end = L8_LE(m, F_ZERO);
        l8_w32(o, fr + 0x58, m);
        if (end) return;
    }
}

int em_level8_port_00132490(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_00132490(&o, self, ent, sp);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 001328D0 (NEARMISS; from the instructions): the creature's collision
 * probes (frame 0x50). ent +0x60 = 0. A point 3 up and 3 ahead (-3 when
 * ent +0x3C is negative) turned by the yaw (001B2B10) plus the position:
 * a wall hit within pi/4 (001B32F0) sets ent +0x60 bit 2. Otherwise, when
 * rising (ent +0x38 < -0.1 is the check), four corner points (+-3 in x
 * and z) turned by the yaw and placed: the pairs (a, b), (b, a), then
 * (c, d), (d, c) through 001B3390 (the second pair only while +0x60 is
 * still 0); a hit sets bit 2. Bit 2 sets self +0x52 bit 0. Falling (ent
 * +0x38 <= 0): four rows of 0x244FE0 turned by self +0xC0 (00102C58) and
 * placed, each against the point 15 below (001B3440 within pi/4): a hit
 * sets ent +0x38 = 0 and +0x60 bit 1. Without bit 1, in state 1 (+5 not 3
 * or 4): the floor under self (001B2F70, or self y - 10) more than 10
 * below and ent +0x38 < -0.5 start the fall (+5 = 4, +6 = 0, 001339E0).
 * Then the ledge probe 18 up (0019B6C0 from self +0xB0, ent +0x69 bit 0
 * cleared first): a hit on a polygon with attribute 0x5B sets bit 0 of
 * +0x69 and ent +0x40 = 0x700031B4. Rising instead: 15 up (001028B8), y +
 * 0.002, 0019AB20(self, .., .., 0x80000007) nonzero: ent +0x38 = 0, +0x60
 * bit 0.
 * ---------------------------------------------------------------------- */
static void l8_v4(L8 *o, uint32_t at, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    l8_w32(o, at, x);
    l8_w32(o, at + 4u, y);
    l8_w32(o, at + 8u, z);
    l8_w32(o, at + 12u, w);
}

static void l8_or8(L8 *o, uint32_t at, uint32_t bits) { l8_w8(o, at, l8_u8(o, at) | bits); }

void l8_001328D0(L8 *o, uint32_t self, uint32_t ent, uint32_t sp)
{
    uint32_t inner = sp - 0x50u;
    l8_w8(o, ent + 0x60, 0);
    l8_w32(o, 0x700038A0u, 0);
    l8_w32(o, 0x700038A4u, 0x40400000u);
    l8_w32(o, 0x700038A8u, 0x40400000u);
    l8_w32(o, 0x700038ACu, F_ONE);
    if (L8_LT(l8_u32(o, ent + 0x3C), F_ZERO)) l8_w32(o, 0x700038A8u, 0xC0400000u);
    l8_001B2B10(o, self, 0x700038A0u, 0x700038A0u);
    if (l8_c_001028B8(o, 0x700038A0u, self + 0xB0, 0x700038A0u)) return;
    if (l8_001B32F0(o, self, 0x700038A0u, F_PI_4, inner)) {
        l8_or8(o, ent + 0x60, 4);
    } else if (!l8_failed(o) && L8_LT(l8_u32(o, ent + 0x38), 0xBDCCCCCDu)) {
        l8_w32(o, 0x700038A0u, 0);
        l8_w32(o, 0x700038A4u, 0);
        l8_w32(o, 0x700038B0u, 0);
        l8_w32(o, 0x700038B4u, 0);
        l8_w32(o, 0x700038A8u, 0xC0400000u);
        l8_w32(o, 0x700038C0u, 0xC0400000u);
        l8_w32(o, 0x700038C4u, 0);
        l8_w32(o, 0x700038C8u, 0);
        l8_w32(o, 0x700038ACu, F_ONE);
        l8_w32(o, 0x700038CCu, F_ONE);
        l8_w32(o, 0x700038B8u, 0x40400000u);
        l8_w32(o, 0x700038D0u, 0x40400000u);
        l8_w32(o, 0x700038BCu, F_ONE);
        l8_w32(o, 0x700038D4u, 0);
        l8_w32(o, 0x700038D8u, 0);
        l8_w32(o, 0x700038DCu, F_ONE);
        if (l8_c_001029C0(o, 0x700036A0u)) return;
        if (l8_c_00102BB0(o, 0x700036A0u, 0x700036A0u, fl(l8_u32(o, self + 0xC4)))) return;
        for (uint32_t k = 0; k < 4; k++)
            if (l8_c_001026A0(o, 0x700038A0u + 0x10u * k, 0x700036A0u, 0x700038A0u + 0x10u * k)) return;
        for (uint32_t k = 0; k < 4; k++)
            if (l8_c_001028B8(o, 0x700038A0u + 0x10u * k, self + 0xB0, 0x700038A0u + 0x10u * k)) return;
        static const uint32_t pairs[2][2] = {{0x700038A0u, 0x700038B0u}, {0x700038C0u, 0x700038D0u}};
        for (int k = 0; k < 2; k++) {
            if (k == 1 && l8_u8(o, ent + 0x60) != 0) break;
            int32_t r = 0;
            if (l8_c_001B3390(o, self, pairs[k][0], pairs[k][1], fl(F_PI_4), &r)) return;
            if (r) {
                l8_or8(o, ent + 0x60, 4);
                continue;
            }
            if (l8_c_001B3390(o, self, pairs[k][1], pairs[k][0], fl(F_PI_4), &r)) return;
            if (r) l8_or8(o, ent + 0x60, 4);
        }
    }
    if (l8_failed(o)) return;
    if (l8_u8(o, ent + 0x60) & 4u) l8_w16(o, self + 0x52, l8_u16(o, self + 0x52) | 1u);
    if (!L8_LE(l8_u32(o, ent + 0x38), F_ZERO)) {
        l8_v4(o, 0x700038A0u, 0, 0x41700000u, 0, F_ONE);
        if (l8_c_001028B8(o, 0x700038B0u, 0x700038A0u, self + 0xB0)) return;
        uint32_t y = l8_u32(o, 0x700038B4u);
        l8_w32(o, 0x700038B4u, L8_ADD(y, 0x3B03126Fu));
        int32_t r = 0;
        if (l8_c_0019AB20(o, self, 0x700038B0u, 0x700038A0u, (int32_t)0x80000007u, &r)) return;
        if (r) {
            l8_w32(o, ent + 0x38, 0);
            l8_or8(o, ent + 0x60, 1);
        }
        return;
    }
    uint32_t row = 0x00244FE0u;
    for (int k = 0; k < 4; k++, row += 0x10u) {
        for (uint32_t j = 0; j < 16; j += 4) l8_w32(o, 0x700038B0u + j, l8_u32(o, row + j));
        if (l8_c_001029C0(o, 0x700036A0u)) return;
        if (l8_c_00102C58(o, 0x700036A0u, 0x700036A0u, self + 0xC0)) return;
        if (l8_c_001026A0(o, 0x700038B0u, 0x700036A0u, 0x700038B0u)) return;
        if (l8_c_001028B8(o, 0x700038B0u, 0x700038B0u, self + 0xB0)) return;
        l8_v4(o, 0x700038A0u, 0, 0xC1700000u, 0, F_ONE);
        if (l8_001B3440(o, self, 0x700038B0u, 0x700038A0u, F_PI_4, inner)) {
            l8_w32(o, ent + 0x38, 0);
            l8_or8(o, ent + 0x60, 2);
            break;
        }
        if (l8_failed(o)) return;
    }
    if (!(l8_u8(o, ent + 0x60) & 2u) && l8_u8(o, self + 4) == 1) {
        uint32_t st = l8_u8(o, self + 5);
        if (st != 3 && st != 4) {
            int32_t r = 0;
            if (l8_c_001B2F70(o, self + 0xB0, 0x700038A0u, &r)) return;
            if (r == 0) l8_w32(o, 0x700038A0u, L8_SUB(l8_u32(o, self + 0xB4), F_10));
            uint32_t drop = L8_SUB(l8_u32(o, self + 0xB4), l8_u32(o, 0x700038A0u));
            if (!L8_LT(drop, F_10) && L8_LT(l8_u32(o, ent + 0x38), 0xBF000000u)) {
                l8_w8(o, self + 5, 4);
                l8_w8(o, self + 6, 0);
                if (l8_c_001339E0(o, self, ent)) return;
            }
        }
    }
    l8_w32(o, 0x700038A0u, l8_u32(o, self + 0xB0));
    l8_w32(o, 0x700038A4u, l8_u32(o, self + 0xB4));
    l8_w32(o, 0x700038A8u, l8_u32(o, self + 0xB8));
    l8_w32(o, 0x700038ACu, F_ONE);
    l8_w32(o, 0x700038A4u, L8_ADD(l8_u32(o, 0x700038A4u), 0x41900000u));
    l8_w8(o, ent + 0x69, l8_u8(o, ent + 0x69) & 0xFEu);
    int32_t hit = 0;
    if (l8_c_0019B6C0(o, 0x700038A0u, self + 0xB0, &hit)) return;
    if (!hit) return;
    uint32_t poly = l8_u32(o, S_700031D0);
    if (l8_u8(o, poly + 0x1A) != 0x5B) return;
    l8_or8(o, ent + 0x69, 1);
    l8_w32(o, ent + 0x40, l8_u32(o, 0x700031B4u));
}

int em_level8_port_001328D0(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_001328D0(&o, self, ent, sp);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 00132FB0 (NEARMISS; from the instructions): the path test ahead (frame
 * 0x50, a point at frame +0x40). The point 3 up and 10 ahead, placed;
 * 001B2BF0(self, it, 0x700038D0, pi/4) -> bits. Bit 0 without bit 2: the
 * floor 0x700038D0 at or above y - 15 -> r = 0; else ent +0x62 =
 * (rand >> 9) & 7, ent +0x30 = 001B37D0(self, 10, 15), r = 1. No bit 0,
 * no bit 2: ent +0x62 = (rand >> 5) & 7, ent +0x30 = 001B37D0(self, 10,
 * 15), r = the v0 001B37D0 leaves. Bit 2 only: ent +0x63 = 0, ent +0x62 =
 * ((rand >> 8) & 0x1F) + 1, ent +0x30 = 001B3580(self, the hit polygon's
 * normal (+0x24..), w 1), r = the v0 001B3580 leaves. Bits 0 and 2: the
 * same frame point; outside AREA 7, 8, 16, 18 and 19, with the floor at or
 * below y + 15, ent +0x61 and +0x0D bit 1 clear, a polygon attribute not
 * 0x46 and a roll (rand >> 14) & (3 with ent +0x56 running, else 1): the
 * climb test (0019A570 from self to 40 up must miss, then the point 15.5
 * up and 10 ahead, placed, 0019AD00(.., 7) must miss): +5 = 3, +6 = 0,
 * r = 2; otherwise the bit-2 turn (ent +0x63 = 0, +0x62, 001B3580).
 * ---------------------------------------------------------------------- */
static void l8_turn_away(L8 *o, uint32_t self, uint32_t ent, uint32_t frame_point)
{
    l8_w8(o, ent + 0x63, 0);
    int32_t r = l8_rand(o);
    if (l8_failed(o)) return;
    uint32_t n = ((uint32_t)l8_sra((uint32_t)r, 8) & 0x1Fu) + 1u;
    l8_w8(o, ent + 0x62, n);
    uint32_t f = l8_001B3580(o, self, frame_point);
    if (l8_failed(o)) return;
    l8_w32(o, ent + 0x30, f);
}

static void l8_wander(L8 *o, uint32_t self, uint32_t ent, unsigned shift)
{
    int32_t r = l8_rand(o);
    if (l8_failed(o)) return;
    l8_w8(o, ent + 0x62, (uint32_t)l8_sra((uint32_t)r, shift) & 7u);
    float f = 0.0f;
    if (l8_c_001B37D0(o, self, fl(F_10), fl(0x41700000u), &f)) return;
    l8_w32(o, ent + 0x30, l8_bits(f));
}

int32_t l8_00132FB0(L8 *o, uint32_t self, uint32_t ent, uint32_t sp)
{
    uint32_t point = sp - 0x50u + 0x40u;
    l8_v4(o, 0x700038A0u, 0, 0x40400000u, F_10, F_ONE);
    l8_001B2B10(o, self, 0x700038A0u, 0x700038A0u);
    if (l8_c_001028B8(o, 0x700038A0u, 0x700038A0u, self + 0xB0)) return 0;
    int32_t bits = l8_001B2BF0(o, self, 0x700038A0u, 0x700038D0u, F_PI_4, sp - 0x50u);
    if (l8_failed(o)) return 0;
    if (bits & 1) {
        if (!(bits & 4)) {
            uint32_t y = L8_SUB(l8_u32(o, self + 0xB4), 0x41700000u);
            if (L8_LT(y, l8_u32(o, 0x700038D0u))) return 0;
            l8_wander(o, self, ent, 9);
            return 1;
        }
        uint32_t poly = l8_u32(o, S_700031D0);
        uint32_t nx = l8_u32(o, poly + 0x24);
        uint32_t area = l8_u8(o, 0x00810700u);
        l8_w32(o, point, nx);
        l8_w32(o, point + 4u, l8_u32(o, poly + 0x28));
        l8_w32(o, point + 8u, l8_u32(o, poly + 0x2C));
        l8_w32(o, point + 12u, F_ONE);
        int climb = !(area == 7 || area == 8 || area == 0x10 || area == 0x12 || area == 0x13);
        if (climb) climb = !L8_LT(L8_ADD(0x41700000u, l8_u32(o, self + 0xB4)), l8_u32(o, 0x700038D0u));
        if (climb) climb = l8_u8(o, ent + 0x61) == 0;
        if (climb) climb = !(l8_u8(o, self + 0x0D) & 2u);
        if (climb) climb = l8_u8(o, poly + 0x1A) != 0x46;
        if (climb) {
            uint32_t mask = l8_u16(o, ent + 0x56) != 0 ? 3u : 1u;
            int32_t r = l8_rand(o);
            if (l8_failed(o)) return 0;
            climb = ((uint32_t)l8_sra((uint32_t)r, 14) & mask) != 0;
        }
        if (climb) {
            uint32_t x = l8_u32(o, self + 0xB0);
            l8_w32(o, 0x700038A0u, x);
            l8_w32(o, 0x700038A4u, l8_u32(o, self + 0xB4));
            l8_w32(o, 0x700038A8u, l8_u32(o, self + 0xB8));
            l8_w32(o, 0x700038ACu, l8_u32(o, self + 0xBC));
            uint32_t ay = l8_u32(o, 0x700038A4u);
            uint32_t bx = l8_u32(o, self + 0xB0);
            uint32_t up = L8_ADD(ay, F_10);
            l8_w32(o, 0x700038B0u, bx);
            l8_w32(o, 0x700038B4u, l8_u32(o, self + 0xB4));
            l8_w32(o, 0x700038B8u, l8_u32(o, self + 0xB8));
            l8_w32(o, 0x700038BCu, l8_u32(o, self + 0xBC));
            l8_w32(o, 0x700038A4u, up);
            l8_w32(o, 0x700038ACu, F_ONE);
            uint32_t by = l8_u32(o, 0x700038B4u);
            l8_w32(o, 0x700038B4u, L8_ADD(by, 0x42200000u));
            l8_w32(o, 0x700038BCu, F_ONE);
            int32_t hit = 0;
            if (l8_c_0019A570(o, 0x700038A0u, 0x700038B0u, 6, 0, &hit)) return 0;
            if (!hit) {
                l8_v4(o, 0x700038A0u, 0, 0x41780000u, F_10, F_ONE);
                l8_001B2B10(o, self, 0x700038A0u, 0x700038A0u);
                if (l8_c_001028B8(o, 0x700038A0u, 0x700038A0u, self + 0xB0)) return 0;
                if (l8_c_0019AD00(o, self, 0x700038A0u, 7, &hit)) return 0;
                if (!hit) {
                    l8_w8(o, self + 5, 3);
                    l8_w8(o, self + 6, 0);
                    return 2;
                }
            }
        }
        l8_turn_away(o, self, ent, point);
        return 1;
    }
    if (!(bits & 4)) {
        l8_wander(o, self, ent, 5);
        return 1;
    }
    l8_w8(o, ent + 0x63, 0);
    int32_t r = l8_rand(o);
    if (l8_failed(o)) return 0;
    l8_w8(o, ent + 0x62, ((uint32_t)l8_sra((uint32_t)r, 8) & 0x1Fu) + 1u);
    uint32_t poly = l8_u32(o, S_700031D0);
    l8_w32(o, point, l8_u32(o, poly + 0x24));
    l8_w32(o, point + 4u, l8_u32(o, poly + 0x28));
    l8_w32(o, point + 8u, l8_u32(o, poly + 0x2C));
    l8_w32(o, point + 12u, F_ONE);
    uint32_t f = l8_001B3580(o, self, point);
    if (l8_failed(o)) return 0;
    l8_w32(o, ent + 0x30, f);
    return 1;
}

int em_level8_port_00132FB0(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp, int32_t *result,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_00132FB0(&o, self, ent, sp);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0012EB60 (C byte-identical): the creature's idle state (+5 = 0), by the
 * sub-state +6 (the table 0x26D100; see the decomp). ent +0x3C = 0 and
 * 001333F0 first; after every sub-state 00132490 and, unless the byte
 * 0x70003B8D is set, 00133640.
 * ---------------------------------------------------------------------- */
static void l8_idle_clip(L8 *o, uint32_t self, uint32_t ent, int32_t clip)
{
    if (l8_u8(o, ent + 0x61) != 0) l8_clip(o, self, clip, F_10);
}

void l8_0012EB60(L8 *o, uint32_t self, uint32_t ent, uint32_t sp)
{
    uint32_t inner = sp - 0x30u;
    l8_w32(o, ent + 0x3C, 0);
    l8_001333F0(o, self, ent);
    if (l8_failed(o)) return;
    uint32_t sub = l8_u8(o, self + 6);
    switch (sub) {
    case 0:
        l8_w16(o, ent + 0x58, 0);
        if (l8_u16(o, ent + 0x56) != 0) {
            l8_w8(o, self + 6, 6);
            if (l8_u8(o, ent + 0x61) != 0) {
                l8_w32(o, ent + 0x34, F_2);
                l8_clip(o, self, 4, F_ZERO);
            }
            break;
        }
        if ((l8_u8(o, self + 0x0D) & 3u) == 3 && l8_u8(o, ent + 0x61) == 0) {
            l8_w8(o, self + 5, 9);
            l8_w8(o, self + 6, 0);
            break;
        }
        {
            int32_t r = l8_rand(o);
            uint32_t row = l8_u8(o, ent + 0x68);
            uint32_t pick = l8_u8(o, 0x00244F10u + (row << 4) + ((uint32_t)l8_sra((uint32_t)r, 8) & 0xFu));
            l8_w8(o, self + 6, pick);
            if (pick != 0) {
                int32_t r2 = l8_rand(o);
                uint32_t now = l8_u8(o, self + 6);
                uint32_t t = l8_u16(o, 0x00244E90u + (now << 5) + (((uint32_t)l8_sra((uint32_t)r2, 9) & 0xFu) << 1));
                l8_w16(o, ent + 0x50, t);
                if (l8_u8(o, self + 6) != 3) {
                    l8_idle_clip(o, self, ent, 4);
                    break;
                }
                int32_t seen = 0;
                if (l8_c_001B13F0(o, D_00810350, self + 0xB0, fl(0x428C0000u), &seen)) return;
                if (seen || (l8_u8(o, ent + 0x69) & 1u)) {
                    l8_w8(o, self + 6, 1);
                    l8_idle_clip(o, self, ent, 4);
                    break;
                }
                if (l8_u8(o, ent + 0x61) == 0) l8_clip(o, self, 3, F_10);
                break;
            }
            l8_w8(o, self + 6, 6);
            l8_idle_clip(o, self, ent, 4);
        }
        break;
    case 1:
    case 2: {
        if (l8_u8(o, ent + 0x61) != 0) {
            if (!(l8_u16(o, ent + 0x58) & 0x5000u)) break;
            l8_w8(o, ent + 0x61, 0);
        }
        l8_w8(o, self + 6, 4);
        l8_clip(o, self, sub == 1 ? 5 : 6, F_10);
        break;
    }
    case 3:
        if (l8_u8(o, ent + 0x61) == 0) {
            uint32_t frame = l8_u32(o, self + 0x3C);
            if (L8_EQ(0x42380000u, frame) || L8_EQ(0x41B00000u, frame)) {
                if (l8_sound(o, self, 0x7DD)) return;
            }
            if (!(l8_u16(o, ent + 0x58) & 0x5000u)) break;
            l8_w8(o, ent + 0x61, 1);
        }
        {
            int32_t r = l8_rand(o);
            if (!((uint32_t)l8_sra((uint32_t)r, 8) & 1u)) {
                l8_w8(o, self + 5, 1);
                l8_w8(o, self + 6, 0);
                break;
            }
            l8_w8(o, self + 6, 5);
            int32_t r2 = l8_rand(o);
            l8_clip(o, self, (r2 & 1) ? 0 : 1, F_10);
        }
        break;
    case 4:
        if ((l8_u8(o, self + 0x0D) & 2u) && l8_u8(o, S_70003B8D) != 0) break;
        {
            uint32_t t = l8_u16(o, ent + 0x50) - 1u;
            l8_w16(o, ent + 0x50, t);
            if ((t & 0xFFFFu) != 0 && l8_u16(o, ent + 0x56) == 0) break;
            l8_w8(o, self + 6, 0);
        }
        break;
    case 5: {
        int32_t clip = l8_s16(o, self + 0x2C);
        if (((uint32_t)clip & 0xFFFF7FFFu) == 1 && L8_EQ(0x42BE0000u, l8_u32(o, self + 0x3C))) {
            if (l8_sound(o, self, 0x7DF)) return;
        }
        uint32_t t = l8_u16(o, ent + 0x50) - 1u;
        l8_w16(o, ent + 0x50, t);
        if ((t & 0xFFFFu) == 0 || l8_u16(o, ent + 0x56) != 0) {
            l8_w8(o, self + 6, 0);
            break;
        }
        if (!(l8_u16(o, ent + 0x58) & 0x3000u)) break;
        int32_t r = l8_rand(o);
        l8_clip(o, self, (r & 1) ? 0 : 1, F_10);
        break;
    }
    case 6:
        if (l8_u8(o, ent + 0x61) != 0) {
            if (!(l8_u16(o, ent + 0x58) & 0x5000u)) break;
            l8_w32(o, ent + 0x34, F_ONE);
        }
        l8_w8(o, self + 5, 1);
        l8_w8(o, self + 6, 0);
        l8_w8(o, ent + 0x61, 0);
        break;
    case 7: {
        int32_t hold = l8_s16(o, self + 0x56);
        uint32_t range = l8_u32(o, ent + 0x44);
        if (L8_LE(range, L8_CVT((uint32_t)hold)) || (l8_u8(o, self + 0x0A) & 1u)) {
            l8_w16(o, self + 0x56, 0);
            l8_w8(o, self + 5, 1);
            l8_w8(o, self + 6, 0);
        }
        break;
    }
    default:
        break;
    }
    if (l8_failed(o)) return;
    l8_00132490(o, self, ent, inner);
    if (l8_failed(o)) return;
    if (l8_u8(o, S_70003B8D) == 0) l8_00133640(o, self, ent);
}

int em_level8_port_0012EB60(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_0012EB60(&o, self, ent, sp);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 0012F100 (NEARMISS; from the instructions): the creature's hunt state
 * (+5 = 1). By +6: 0 starts it (ent +0x63 / +0x54 / +0x3C cleared, a
 * random heading ent +0x30 = 001B1470(2 pi ((rand >> 13) & 0xFF) / 255),
 * the timer ent +0x50 from the table 0x244F30 [+0x61][(rand >> 7) & 0xF],
 * the clip: with ent +0x61, clip 2 current -> +6 = 2, ent +0x3C = 0.1,
 * else +6 + 1 and 001C67E0(self, 2, 10, 0); without, clip 7 -> +6 = 2,
 * else +6 + 1 and clip 7); 1 waits for the clip (ent +0x58 bit 15 clear)
 * then +6 + 1, ent +0x3C = 0.1 and on as 2. 2: the step sounds (frames 95
 * / 43 with ent +0x61), 001333F0 zero with +0x0D & 3 == 3 and no +0x61 ->
 * +5 = 9; ent +0x56 with +0x61 -> +5 = +6 = 0; the turn ent +0x62 count or
 * the path test 00132FB0 zero: the chase (within 15 of the player: the
 * heading to the player; facing within pi/90 (001B1560) and not within 40:
 * the lunge point 70 ahead / 3 up (00131F20) and, clear (001B3250 zero),
 * +5 = 2) or, every 64th frame, the wander check (ent +0x68 toggles by
 * reach 15 / 30, the heading to ent +0..); the timer ent +0x50: at 0 +5 =
 * +6 = 0; then 00133640 in state 1 and 00131F90. Every +6 ends with the
 * breath (ent +0x69 bit 0: a countdown ent +0x54, reloaded 7 + (rand >> 9
 * & 0xF), spark 0x8000001D at the mouth, y = ent +0x40) and 00132490.
 * ---------------------------------------------------------------------- */
static void l8_breath(L8 *o, uint32_t self, uint32_t ent, uint32_t inner)
{
    if (l8_u8(o, ent + 0x69) & 1u) {
        uint32_t t = l8_u16(o, ent + 0x54);
        if (t != 0) {
            l8_w16(o, ent + 0x54, t - 1u);
        } else {
            int32_t r = l8_rand(o);
            if (l8_failed(o)) return;
            l8_w16(o, ent + 0x54, ((uint32_t)l8_sra((uint32_t)r, 9) & 0xFu) + 7u);
            l8_w32(o, 0x700038A0u, l8_u32(o, self + 0xB0));
            l8_w32(o, 0x700038A4u, l8_u32(o, self + 0xB4));
            l8_w32(o, 0x700038A8u, l8_u32(o, self + 0xB8));
            l8_w32(o, 0x700038ACu, F_ONE);
            l8_w32(o, 0x700038A4u, l8_u32(o, ent + 0x40));
            if (l8_c_001EFD90(o, (int32_t)0x8000001Du, 0x700038A0u, self + 0xC0)) return;
        }
    }
    l8_00132490(o, self, ent, inner);
}

void l8_0012F100(L8 *o, uint32_t self, uint32_t ent, uint32_t sp)
{
    uint32_t inner = sp - 0x30u;
    uint32_t sub = l8_u8(o, self + 6);
    if (sub == 0) {
        l8_w8(o, ent + 0x63, 0);
        l8_w16(o, ent + 0x54, 0);
        l8_w32(o, ent + 0x3C, 0);
        int32_t r = l8_rand(o);
        uint32_t b = L8_CVT((uint32_t)l8_sra((uint32_t)r, 13) & 0xFFu);
        float heading = 0.0f;
        if (l8_angle(o, fl(L8_DIV(L8_MUL(0x40C90FDBu, b), 0x437F0000u)), &heading)) return;
        l8_w32(o, ent + 0x30, l8_bits(heading));
        int32_t r2 = l8_rand(o);
        if (l8_failed(o)) return;
        uint32_t row = l8_u8(o, ent + 0x61);
        uint32_t t = l8_u16(o, 0x00244F30u + (row << 5) + (((uint32_t)l8_sra((uint32_t)r2, 7) & 0xFu) << 1));
        l8_w16(o, ent + 0x50, t);
        int32_t want = l8_u8(o, ent + 0x61) != 0 ? 2 : 7;
        if (l8_s16(o, self + 0x2C) == want) {
            l8_w8(o, self + 6, 2);
            if (want == 2) l8_w32(o, ent + 0x3C, 0x3DCCCCCDu);
        } else {
            uint32_t s = l8_u8(o, self + 6);
            l8_w8(o, self + 6, s + 1u);
            l8_clip(o, self, want, F_10);
        }
        if (l8_failed(o)) return;
        l8_breath(o, self, ent, inner);
        return;
    }
    if (sub == 1) {
        if (l8_u16(o, ent + 0x58) & 0x8000u) {
            l8_breath(o, self, ent, inner);
            return;
        }
        l8_w8(o, self + 6, sub + 1u);
        l8_w32(o, ent + 0x3C, 0x3DCCCCCDu);
    } else if (sub != 2) {
        l8_breath(o, self, ent, inner);
        return;
    }
    if (l8_u8(o, ent + 0x61) != 0) {
        uint32_t frame = l8_u32(o, self + 0x3C);
        if (L8_EQ(0x42BE0000u, frame) || L8_EQ(0x422C0000u, frame)) {
            if (l8_sound(o, self, 0x7DE)) return;
        }
    }
    int32_t go = l8_001333F0(o, self, ent);
    if (l8_failed(o)) return;
    if (!go && (l8_u8(o, self + 0x0D) & 3u) == 3 && l8_u8(o, ent + 0x61) == 0) {
        l8_w8(o, self + 5, 9);
        l8_w8(o, self + 6, 0);
        l8_breath(o, self, ent, inner);
        return;
    }
    if (l8_u16(o, ent + 0x56) != 0 && l8_u8(o, ent + 0x61) != 0) {
        l8_w8(o, self + 5, 0);
        l8_w8(o, self + 6, 0);
    }
    uint32_t turning = l8_u8(o, ent + 0x62);
    if (turning != 0) {
        l8_w8(o, ent + 0x62, turning - 1u);
        if (L8_EQ(l8_u32(o, ent + 0x30), l8_u32(o, self + 0xC4))) l8_w8(o, ent + 0x62, 0);
    } else if (!l8_00132FB0(o, self, ent, inner)) {
        if (l8_failed(o)) return;
        if (l8_u16(o, ent + 0x56) != 0 && l8_u8(o, ent + 0x61) == 0) {
            int32_t near = 0;
            if (l8_c_001B13F0(o, D_00810360, self + 0xB0, fl(0x41700000u), &near)) return;
            if (!near) {
                uint32_t x = l8_u32(o, D_00810360);
                uint32_t z = l8_u32(o, D_00810360 + 8u);
                float to = 0.0f;
                if (l8_c_001B1240(o, self + 0xB0, fl(x), fl(z), &to)) return;
                l8_w32(o, ent + 0x30, l8_bits(to));
            }
            if (l8_001B1560(o, self, D_00810360, 0x3D0EFA35u)) {
                if (l8_c_001B13F0(o, D_00810360, self + 0xB0, fl(0x42200000u), &near)) return;
                if (!near) {
                    l8_v4(o, 0x700038A0u, 0, 0x40400000u, 0x428C0000u, F_ONE);
                    if (l8_c_00131F20(o, self, 0x700038A0u, 0x700038A0u)) return;
                    if (l8_c_001028B8(o, 0x700038A0u, 0x700038A0u, self + 0xB0)) return;
                    int32_t blocked = 0;
                    if (l8_c_001B3250(o, self, 0x700038A0u, fl(0x41700000u), &blocked)) return;
                    if (!blocked) {
                        l8_w8(o, self + 5, 2);
                        l8_w8(o, self + 6, 0);
                    }
                }
            }
        } else {
            int32_t shift = l8_s16(o, 0x70003B8Au);
            uint32_t frame = l8_u32(o, S_70003B68);
            if (((frame + (uint32_t)shift) & 0x3Fu) == 0) {
                for (uint32_t k = 0; k < 16; k += 4) l8_w32(o, 0x700038A0u + k, l8_u32(o, self + 0xB0 + k));
                for (uint32_t k = 0; k < 16; k += 4) l8_w32(o, 0x700038B0u + k, l8_u32(o, ent + k));
                l8_w32(o, 0x700038B4u, 0);
                l8_w32(o, 0x700038A4u, 0);
                int32_t in = 0;
                if (l8_u8(o, ent + 0x68) != 0) {
                    if (l8_c_001B13F0(o, 0x700038A0u, 0x700038B0u, fl(0x41700000u), &in)) return;
                    if (in) l8_w8(o, ent + 0x68, 0);
                    uint32_t x = l8_u32(o, ent);
                    uint32_t z = l8_u32(o, ent + 8);
                    float to = 0.0f;
                    if (l8_c_001B1240(o, self + 0xB0, fl(x), fl(z), &to)) return;
                    l8_w32(o, ent + 0x30, l8_bits(to));
                } else {
                    if (l8_c_001B13F0(o, 0x700038A0u, 0x700038B0u, fl(0x41F00000u), &in)) return;
                    if (!in) l8_w8(o, ent + 0x68, 1);
                }
            }
        }
    }
    if (l8_failed(o)) return;
    uint32_t t = l8_u16(o, ent + 0x50) - 1u;
    l8_w16(o, ent + 0x50, t);
    if ((t & 0xFFFFu) == 0) {
        l8_w8(o, self + 5, 0);
        l8_w8(o, self + 6, 0);
    }
    if (l8_u8(o, self + 5) == 1) l8_00133640(o, self, ent);
    if (l8_failed(o)) return;
    l8_00131F90(o, self, ent, inner);
    if (l8_failed(o)) return;
    l8_breath(o, self, ent, inner);
}

int em_level8_port_0012F100(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_0012F100(&o, self, ent, sp);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 0012FC10 (NEARMISS; from the instructions): the creature's bite state
 * (+5 = 5), by +6 (see docs/LEVEL8_PORT.md): 0 the wind-up (flags
 * cleared, ent +0x34 = 2 with +0x0D bit 1 else 1.5, the clip of the table
 * 0x244F70 row ent +0x64); 1 the turn (ent +0x30 to the player, the yaw
 * by 001B12B0 at pi/45), on the clip's end (ent +0x58 bit 12): ent +0x34 =
 * 1, the clip of 0x244F72, +6 + 1, ent +0x54 = 20 for the bite kind 2, else
 * 30 and the trail 0x8000000D (001EFF10, stored at self +0x20); 2 the bite:
 * at the countdown's end the sound 0x7D1 (kind 2) or 0x7D0; unless the
 * player is out (0021BE40) or the bite is spent (ent +0x5A) or kind 2: the
 * box test 001A7B80 hits -> ent +0x5A = 0xFFFF, the push (D_00810320 =
 * the flat direction to the player, normalised), 001B55E0(self, 1), the
 * damage D_008104D4 by kind and type (+0x0D & 0x83) and difficulty
 * (D_0081070A), the player flag D_008102B0 bit 1; kind 2 at frame 24 the
 * spit (the point turned into place, the angle to the player, the spit
 * 0x80000008 by +0x0D bits); the clip's end: the clip of 0x244F74, +6 + 1,
 * the trail ended (+4 = 3 of self +0x20); 3 at the clip's end: +5 = +6 =
 * 0, ent +0x6B = 0, ent +0x5A = 0 (bit 1 of +0x0D) or (rand >> 6) & 0x1F.
 * Every +6 ends with 00132490.
 * ---------------------------------------------------------------------- */
static void l8_bite_clip(L8 *o, uint32_t self, uint32_t ent, uint32_t table)
{
    uint32_t kind = l8_u8(o, ent + 0x64);
    int32_t clip = l8_s16(o, table + kind * 6u);
    l8_clip(o, self, clip, F_5);
}

static void l8_bite_damage(L8 *o, uint32_t self, uint32_t ent)
{
    uint32_t kind = l8_u8(o, ent + 0x64);
    uint32_t type = l8_u8(o, self + 0x0D) & 0x83u;
    uint32_t value;
    if (kind != 0) {
        if (type == 0x81) value = l8_u8(o, D_0081070A) ? 0x42200000u : 0x421C0000u;
        else if (type == 0x80 || type == 3) value = 0x41C80000u;
        else if (type == 1) value = l8_u8(o, D_0081070A) ? 0x41F00000u : 0x41C80000u;
        else value = l8_u8(o, D_0081070A) ? 0x41A00000u : 0x41700000u;
    } else {
        if (type == 0x81) value = l8_u8(o, D_0081070A) ? 0x41F00000u : 0x41E00000u;
        else if (type == 0x80) value = 0x41C80000u;
        else if (type == 3 || type == 1) value = l8_u8(o, D_0081070A) ? 0x41C80000u : 0x41A00000u;
        else value = l8_u8(o, D_0081070A) ? 0x41A00000u : 0x41700000u;
    }
    l8_w32(o, 0x008104D4u, value);
    l8_or8(o, D_008102B0, 2);
}

static void l8_bite_spit(L8 *o, uint32_t self)
{
    l8_v4(o, 0x700038A0u, 0xBF9703B0u, 0x41318ADBu, 0x40F14539u, F_ONE);
    l8_001B2B10(o, self, 0x700038A0u, 0x700038A0u);
    if (l8_c_001028B8(o, 0x700038A0u, 0x700038A0u, self + 0xB0)) return;
    if (l8_c_00102948(o, 0x700038B0u, 0x700038A0u)) return;
    if (l8_c_00102948(o, 0x700038C0u, D_00810350)) return;
    l8_w32(o, 0x700038C4u, 0);
    l8_w32(o, 0x700038B4u, 0);
    float flat = 0.0f;
    if (l8_c_001B15D0(o, 0x700038B0u, 0x700038C0u, &flat)) return;
    l8_w32(o, 0x70003A20u, l8_bits(flat));
    uint32_t py = l8_u32(o, 0x00810364u);
    uint32_t sy = l8_u32(o, 0x700038A4u);
    float dy = 0.0f;
    if (l8_c_0011DF78(o, fl(L8_SUB(py, sy)), &dy)) return;
    l8_w32(o, 0x70003A24u, l8_bits(dy));
    float pitch = 0.0f;
    if (l8_c_0011E620(o, dy, fl(l8_u32(o, 0x70003A20u)), &pitch)) return;
    l8_w32(o, 0x70003A20u, L8_MUL(0xBF800000u, l8_bits(pitch)));
    if (l8_c_001029C0(o, 0x700036A0u)) return;
    if (l8_c_00102B08(o, 0x700036A0u, 0x700036A0u, fl(l8_u32(o, 0x70003A20u)))) return;
    if (l8_c_00102BB0(o, 0x700036A0u, 0x700036A0u, fl(l8_u32(o, self + 0xC4)))) return;
    l8_v4(o, 0x700038B0u, 0, 0, F_ONE, F_ONE);
    if (l8_c_001026A0(o, 0x700038B0u, 0x700036A0u, 0x700038B0u)) return;
    uint32_t flags = l8_u8(o, self + 0x0D);
    uint32_t low = flags & 3u;
    int32_t kind;
    uint32_t speed = 0x3F333333u;
    if (low == 3) {
        int32_t r = l8_rand(o);
        if (l8_failed(o)) return;
        uint32_t b = L8_CVT((uint32_t)l8_sra((uint32_t)r, 14) & 0xFFu);
        speed = L8_ADD(0x3F0CCCCDu, L8_MUL(0x3E19999Au, L8_DIV(b, 0x437F0000u)));
        l8_w32(o, 0x70003A20u, speed);
        kind = 0x12;
    } else if (low == 1) {
        kind = (flags & 0x80u) ? 0x0D : 0x0C;
    } else if (low == 0) {
        kind = (flags & 0x80u) ? 0x0F : 0x0E;
    } else {
        return;
    }
    l8_c_001EFFD0(o, (int32_t)0x80000008u, 0x700038A0u, 0x700038B0u, kind, fl(speed));
}

void l8_0012FC10(L8 *o, uint32_t self, uint32_t ent, uint32_t sp)
{
    uint32_t inner = sp - 0x30u;
    uint32_t sub = l8_u8(o, self + 6);
    if (sub == 0) {
        l8_w16(o, ent + 0x56, 0);
        l8_w16(o, ent + 0x52, 0);
        l8_w8(o, ent + 0x63, 0);
        l8_w16(o, ent + 0x5A, 0);
        l8_w32(o, ent + 0x3C, 0);
        l8_w8(o, ent + 0x6B, 1);
        l8_w8(o, self + 0x5C, 1);
        l8_w32(o, ent + 0x34, (l8_u8(o, self + 0x0D) & 2u) ? F_2 : 0x3FC00000u);
        l8_bite_clip(o, self, ent, 0x00244F70u);
        if (l8_failed(o)) return;
        l8_w8(o, self + 6, l8_u8(o, self + 6) + 1u);
    } else if (sub == 1) {
        uint32_t x = l8_u32(o, D_00810360);
        uint32_t z = l8_u32(o, D_00810360 + 8u);
        float to = 0.0f;
        if (l8_c_001B1240(o, self + 0xB0, fl(x), fl(z), &to)) return;
        l8_w32(o, ent + 0x30, l8_bits(to));
        uint32_t yaw = l8_u32(o, self + 0xC4);
        uint32_t goal = l8_u32(o, ent + 0x30);
        float r = 0.0f;
        if (l8_c_001B12B0(o, fl(goal), fl(yaw), fl(0x3D8EFA35u), &r)) return;
        l8_w32(o, self + 0xC4, l8_bits(r));
        if (l8_u16(o, ent + 0x58) & 0x1000u) {
            l8_w32(o, ent + 0x34, F_ONE);
            l8_bite_clip(o, self, ent, 0x00244F72u);
            if (l8_failed(o)) return;
            l8_w8(o, self + 6, l8_u8(o, self + 6) + 1u);
            if (l8_u8(o, ent + 0x64) == 2) {
                l8_w16(o, ent + 0x54, 0x14);
            } else {
                l8_w16(o, ent + 0x54, 0x1E);
                l8_w32(o, 0x700038A0u, 0);
                l8_w32(o, 0x700038A4u, 0);
                l8_w32(o, 0x700038A8u, 0);
                l8_w32(o, 0x700038D0u, 0);
                l8_w32(o, 0x700038D4u, 0);
                l8_w32(o, 0x700038D8u, 0);
                l8_w32(o, 0x700038DCu, 0);
                l8_w32(o, 0x700038ACu, F_ONE);
                l8_w32(o, 0x700038B0u, 0x40E00000u);
                l8_w32(o, 0x700038B4u, 0);
                l8_w32(o, 0x700038B8u, 0);
                l8_w32(o, 0x700038BCu, F_ONE);
                l8_w32(o, 0x700038C0u, 0x43000000u);
                l8_w32(o, 0x700038C4u, 0x42200000u);
                l8_w32(o, 0x700038C8u, 0x42A00000u);
                l8_w32(o, 0x700038CCu, 0x43000000u);
                uint32_t bone = l8_u32(o, self + 0x170);
                uint32_t trail = 0;
                if (l8_c_001EFF10(o, (int32_t)0x8000000Du, bone + 0x90, 0x700038A0u, 0x700038B0u, 0x700038C0u,
                                  0x700038D0u, fl(F_10), &trail))
                    return;
                l8_w32(o, self + 0x20, trail);
            }
        }
    } else if (sub == 2) {
        uint32_t t = l8_u16(o, ent + 0x54) - 1u;
        l8_w16(o, ent + 0x54, t);
        if ((t & 0xFFFFu) == 0) {
            if (l8_sound(o, self, l8_u8(o, ent + 0x64) == 2 ? 0x7D1 : 0x7D0)) return;
        }
        int bite = !l8_0021BE40(o, D_008102B0);
        if (l8_failed(o)) return;
        if (bite) bite = l8_u16(o, ent + 0x5A) == 0;
        if (bite) bite = l8_u8(o, ent + 0x64) != 2;
        if (bite) {
            bite = l8_001A7B80(o, self, inner);
            if (l8_failed(o)) return;
        }
        if (bite) {
            l8_w16(o, ent + 0x5A, 0xFFFF);
            uint32_t px = l8_u32(o, D_00810360);
            uint32_t sx = l8_u32(o, self + 0xB0);
            l8_w32(o, 0x00810320u, L8_SUB(px, sx));
            l8_w32(o, 0x00810324u, 0);
            uint32_t pz = l8_u32(o, D_00810360 + 8u);
            uint32_t sz = l8_u32(o, self + 0xB8);
            l8_w32(o, 0x00810328u, L8_SUB(pz, sz));
            if (l8_c_00102760(o, 0x00810320u, 0x00810320u)) return;
            l8_001B55E0(o, self, 1);
            if (l8_failed(o)) return;
            l8_bite_damage(o, self, ent);
        } else if (l8_u8(o, ent + 0x64) == 2 && L8_EQ(0x41C00000u, l8_u32(o, self + 0x3C))) {
            l8_bite_spit(o, self);
        }
        if (l8_failed(o)) return;
        if (l8_u16(o, ent + 0x58) & 0x1000u) {
            l8_bite_clip(o, self, ent, 0x00244F74u);
            if (l8_failed(o)) return;
            l8_w8(o, self + 6, l8_u8(o, self + 6) + 1u);
            uint32_t trail = l8_u32(o, self + 0x20);
            if (trail != 0) {
                l8_w8(o, trail + 4, 3);
                l8_w32(o, self + 0x20, 0);
            }
        }
    } else if (sub == 3) {
        if (l8_u16(o, ent + 0x58) & 0x1000u) {
            l8_w8(o, self + 5, 0);
            l8_w8(o, self + 6, 0);
            l8_w8(o, ent + 0x6B, 0);
            if (l8_u8(o, self + 0x0D) & 2u) {
                l8_w16(o, ent + 0x5A, 0);
            } else {
                int32_t r = l8_rand(o);
                if (l8_failed(o)) return;
                l8_w16(o, ent + 0x5A, (uint32_t)l8_sra((uint32_t)r, 6) & 0x1Fu);
            }
        }
    }
    if (l8_failed(o)) return;
    l8_00132490(o, self, ent, inner);
}

int em_level8_port_0012FC10(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_0012FC10(&o, self, ent, sp);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 0012E840 (C byte-identical): the creature's live state (+4 = 1; the
 * decomp's header describes it). ent +0x6D = 0; 001B2140(self) zero ends
 * it. ent +0x6D = 1; ent +0x44 = the flat distance to the player
 * (001B15D0 of 0x700038A0 / 0x700038B0 with their y cleared). Then the
 * behaviour +5 (the table 0x26D0D0): 0 0012EB60, 1 0012F100, 5 0012FC10
 * (translated here), 2 / 3 / 4 / 6 / 7 / 8 / 9 the hooks 0012F6C0,
 * 0012F980, 0012FA50, 001305B0, 00130AB0, 00131210, 00131510. With the
 * byte 0x70003B8D set and +5 not 0, 3, 4 or 9: +5 = +6 = 0. +0x5D = 0x81
 * and 001B4810(self) with ent +0x6B, else 1; 00133A20, 00133DB0; the hit
 * flags +0x5E / +0x52 bit 0 from ent +0x63 / +0x65; +0 by +0x0D, ent +0x6A
 * and +0x34; ent +0x56 / +0x5A count down; +0x0A = 0, 001328D0; ent +0x58
 * = 001C64F0(self, ent +0x34) and the step sound 0x7D3 on its low nibble
 * 9; 00131ED0, 001C68C0, 001B17A0 and the +0x4C method.
 * ---------------------------------------------------------------------- */
void l8_0012E840(L8 *o, uint32_t self, uint32_t ent, uint32_t sp)
{
    uint32_t inner = sp - 0x30u;
    l8_w8(o, ent + 0x6D, 0);
    int32_t here = 0;
    if (l8_c_001B2140(o, self, &here)) return;
    if (!here) return;
    l8_w8(o, ent + 0x6D, 1);
    if (l8_c_00102948(o, 0x700038A0u, self + 0xB0)) return;
    if (l8_c_00102948(o, 0x700038B0u, D_00810360)) return;
    l8_w32(o, 0x700038B4u, 0);
    l8_w32(o, 0x700038A4u, 0);
    float d = 0.0f;
    if (l8_c_001B15D0(o, 0x700038A0u, 0x700038B0u, &d)) return;
    l8_w32(o, ent + 0x44, l8_bits(d));
    switch (l8_u8(o, self + 5)) {
    case 0: l8_0012EB60(o, self, ent, inner); break;
    case 1: l8_0012F100(o, self, ent, inner); break;
    case 2: l8_c_0012F6C0(o, self, ent); break;
    case 3: l8_c_0012F980(o, self, ent); break;
    case 4: l8_c_0012FA50(o, self, ent); break;
    case 5: l8_0012FC10(o, self, ent, inner); break;
    case 6: l8_c_001305B0(o, self, ent); break;
    case 7: l8_c_00130AB0(o, self, ent); break;
    case 8: l8_c_00131210(o, self, ent); break;
    case 9: l8_c_00131510(o, self, ent); break;
    default: break;
    }
    if (l8_failed(o)) return;
    if (l8_u8(o, S_70003B8D) != 0) {
        uint32_t st = l8_u8(o, self + 5);
        if (st != 0 && st != 3 && st != 4 && st != 9) {
            l8_w8(o, self + 5, 0);
            l8_w8(o, self + 6, 0);
        }
    }
    if (l8_u8(o, ent + 0x6B) != 0) {
        l8_w8(o, self + 0x5D, 0x81);
        l8_001B4810(o, self);
    } else {
        l8_w8(o, self + 0x5D, 1);
    }
    if (l8_failed(o)) return;
    l8_00133A20(o, self, ent);
    if (l8_failed(o)) return;
    l8_00133DB0(o, self, ent);
    if (l8_u8(o, ent + 0x63) != 0 || l8_u8(o, ent + 0x65) != 0) {
        l8_w8(o, self + 0x5E, 0);
        l8_w16(o, self + 0x52, l8_u16(o, self + 0x52) | 1u);
    } else {
        l8_w8(o, self + 0x5E, 1);
    }
    if (l8_u8(o, self + 0x0D) == 3) l8_w8(o, self, 1);
    else if (l8_u8(o, ent + 0x6A) != 0) l8_w8(o, self, 3);
    else l8_w8(o, self, l8_s16(o, self + 0x34) != 0 ? 1u : 3u);
    uint32_t t = l8_u16(o, ent + 0x56);
    if (t) l8_w16(o, ent + 0x56, t - 1u);
    t = l8_u16(o, ent + 0x5A);
    if (t) l8_w16(o, ent + 0x5A, t - 1u);
    l8_w8(o, self + 0x0A, 0);
    l8_001328D0(o, self, ent, inner);
    if (l8_failed(o)) return;
    int32_t frame = 0;
    if (l8_c_001C64F0(o, self, fl(l8_u32(o, ent + 0x34)), &frame)) return;
    l8_w16(o, ent + 0x58, (uint32_t)frame);
    if ((l8_u16(o, ent + 0x58) & 0xFu) == 9) {
        if (l8_sound(o, self, 0x7D3)) return;
    }
    l8_00131ED0(o, self);
    if (l8_c_001C68C0(o, self)) return;
    if (l8_c_001B17A0(o, self)) return;
    l8_callback(o, l8_u32(o, self + 0x4C), self);
}

int em_level8_port_0012E840(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_0012E840(&o, self, ent, sp);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 0012E560 (NEARMISS; from the instructions): the creature's spawn (+4 =
 * 0): +4 + 1, +0 = 1, ent +0x6D = 1, ent +0x6B = 0, ent +0x34 = 1.0, ent
 * +0x6E = +0x0D, ent +0x61 = 0, ent +0x58 = 0, +0x30 = 0x275388, +0x20 =
 * 0; ent +0 = self +0xB0, ent +0x20 = self +0xC0 (00102948); with D_00810808
 * at 0xFF, +0x0D bit 7 is set. The model by +0x0D bits 0 / 7 (001B10B0
 * with 0x6E / 0x70 or 0x72 / 0x74 and 0x71; a nonzero result ends here),
 * +0x58 = D_0028A64C (bit 0) or D_0028A65C; 001C63E0(self, 5); the hit
 * points +0x34 by +0x0D & 0x83 and D_0081070A (80 / 120, 100 / 150, 150 /
 * 250, 180 / 300, 200 / 350; the 0x80 and 0x81 kinds also scale +0x60..
 * +0x68 by 1.3 and +0x6C = 1); with the halfword +0x56 set: +5 = 0, +6 =
 * 7 and 001C67E0(self, 5, 10, 0).
 * ---------------------------------------------------------------------- */
void l8_0012E560(L8 *o, uint32_t self, uint32_t ent)
{
    l8_w8(o, self + 4, l8_u8(o, self + 4) + 1u);
    l8_w8(o, self, 1);
    l8_w8(o, ent + 0x6D, 1);
    l8_w8(o, ent + 0x6B, 0);
    l8_w32(o, ent + 0x34, F_ONE);
    l8_w8(o, ent + 0x6E, l8_u8(o, self + 0x0D));
    l8_w8(o, ent + 0x61, 0);
    l8_w16(o, ent + 0x58, 0);
    l8_w32(o, self + 0x30, 0x00275388u);
    l8_w32(o, self + 0x20, 0);
    if (l8_c_00102948(o, ent, self + 0xB0)) return;
    if (l8_c_00102948(o, ent + 0x20, self + 0xC0)) return;
    if (l8_u8(o, 0x00810808u) == 0xFF) l8_w8(o, self + 0x0D, l8_u8(o, self + 0x0D) | 0x80u);
    uint32_t flags = l8_u8(o, self + 0x0D);
    int32_t r = 0;
    uint32_t model;
    if (flags & 1u) {
        if (l8_c_001B10B0(o, self, (flags & 0x80u) ? 0x70 : 0x6E, 0x71, &r)) return;
        model = 0x0028A64Cu;
    } else {
        if (l8_c_001B10B0(o, self, (flags & 0x80u) ? 0x74 : 0x72, 0x71, &r)) return;
        model = 0x0028A65Cu;
    }
    if (r != 0) return;
    l8_w32(o, self + 0x58, l8_u32(o, model));
    if (l8_c_001C63E0(o, self, 5)) return;
    uint32_t kind = l8_u8(o, self + 0x0D) & 0x83u;
    int big = 0;
    uint32_t hp;
    switch (kind) {
    case 3: hp = l8_u8(o, D_0081070A) ? 0x15E : 0xC8; break;
    case 0x81: hp = l8_u8(o, D_0081070A) ? 0x12C : 0xB4; big = 1; break;
    case 1: hp = l8_u8(o, D_0081070A) ? 0x96 : 0x64; break;
    case 0x80: hp = l8_u8(o, D_0081070A) ? 0xFA : 0x96; big = 1; break;
    case 0: hp = l8_u8(o, D_0081070A) ? 0x78 : 0x50; break;
    default: hp = 0x10000; break;
    }
    if (hp != 0x10000) l8_w16(o, self + 0x34, hp);
    if (big) {
        l8_w32(o, self + 0x60, 0x3FA66666u);
        l8_w32(o, self + 0x64, 0x3FA66666u);
        l8_w32(o, self + 0x68, 0x3FA66666u);
        l8_w32(o, self + 0x6C, F_ONE);
    }
    if (l8_s16(o, self + 0x56) == 0) return;
    l8_w8(o, self + 5, 0);
    l8_w8(o, self + 6, 7);
    l8_clip(o, self, 5, F_10);
}

int em_level8_port_0012E560(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_0012E560(&o, self, ent);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 0012E3A0 (word asm; from the instructions): the creature's behaviour.
 * By the byte 0x70003B8D: 4 or 0 -> the dispatch; 3 or 2 -> the dispatch
 * only with +0x0D == 3 and +4 != 1; 1 -> with +0x0D == 3 or 001B2140(self)
 * zero the dispatch, else (+4 nonzero) only the +0x4C method; other ->
 * the dispatch. The dispatch: +0x52 = 0; by +4: 0 0012E560, 1 0012E840, 2
 * 00131650, 3 00131E80 (then nothing more); then (not 3) the countdowns
 * ent +0x66, +0x5C (halfword), +0x6C, +0x6A, self +0x54 = 0, 001B5360(self)
 * with ent +0x6D and 001B0D80(self).
 * ---------------------------------------------------------------------- */
void l8_0012E3A0(L8 *o, uint32_t self, uint32_t sp)
{
    uint32_t ent = self + 0x1F0;
    uint32_t mode = l8_u8(o, S_70003B8D);
    if (mode == 3 || mode == 2) {
        if (l8_u8(o, self + 0x0D) != 3) return;
        if (l8_u8(o, self + 4) == 1) return;
    } else if (mode == 1) {
        if (l8_u8(o, self + 0x0D) != 3) {
            int32_t here = 0;
            if (l8_c_001B2140(o, self, &here)) return;
            if (here) {
                if (l8_u8(o, self + 4) == 0) return;
                l8_callback(o, l8_u32(o, self + 0x4C), self);
                return;
            }
        }
    }
    l8_w16(o, self + 0x52, 0);
    uint32_t state = l8_u8(o, self + 4);
    if (state == 3) {
        l8_c_00131E80(o, self);
        return;
    }
    if (state == 2) l8_c_00131650(o, self, ent);
    else if (state == 1) l8_0012E840(o, self, ent, sp - 0x30u);
    else if (state == 0) l8_0012E560(o, self, ent);
    if (l8_failed(o)) return;
    uint32_t t = l8_u8(o, ent + 0x66);
    if (t) l8_w8(o, ent + 0x66, t - 1u);
    t = l8_u16(o, ent + 0x5C);
    if (t) l8_w16(o, ent + 0x5C, t - 1u);
    t = l8_u8(o, ent + 0x6C);
    if (t) l8_w8(o, ent + 0x6C, t - 1u);
    t = l8_u8(o, ent + 0x6A);
    if (t) l8_w8(o, ent + 0x6A, t - 1u);
    l8_w16(o, self + 0x54, 0);
    if (l8_u8(o, ent + 0x6D) != 0) {
        if (l8_c_001B5360(o, self)) return;
    }
    l8_c_001B0D80(o, self);
}

int em_level8_port_0012E3A0(const EmLevel8PortHooks *h, uint32_t self, uint32_t sp, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_0012E3A0(&o, self, sp);
    return l8_end(&o);
}
