/* The creature's probes and helpers (see em_level8_port.h,
 * docs/LEVEL8_PORT.md): 001A7B80 / 001A7BA0 (the bone-box overlap test),
 * 001B1560, 001B2B10, 001B2BF0, 001B30E0, 001B32F0, 001B3440, 001B3580,
 * 001B55E0. The rules of em_level8_port_lift.c apply; stack locals (and
 * the register spills of 001A7BA0) are made at the original's frame
 * offsets below `sp`.
 */
#include "em_level8_port_internal.h"

#define S_700031C0 0x700031C0u /* the probe's shove vector */

#define S_700031E0 0x700031E0u /* the probe's hit count */
#define F_PI_3 0x3F860A92u     /* pi / 3 */

/* ------------------------------------------------------------------------
 * 001B1560 (C byte-identical): 1 when |001B1470(001B1240(self +0xB0,
 * pos.x, pos.z) - self yaw +0xC4)| (0011DF78) <= limit, else 0.
 * ---------------------------------------------------------------------- */
int32_t l8_001B1560(L8 *o, uint32_t self, uint32_t pos, uint32_t limit)
{
    uint32_t x = l8_u32(o, pos);
    uint32_t z = l8_u32(o, pos + 8);
    float bearing = 0.0f;
    if (l8_c_001B1240(o, self + 0xB0, fl(x), fl(z), &bearing)) return 0;
    uint32_t yaw = l8_u32(o, self + 0xC4);
    float turn = 0.0f;
    if (l8_angle(o, fl(L8_SUB(l8_bits(bearing), yaw)), &turn)) return 0;
    float mag = 0.0f;
    if (l8_c_0011DF78(o, turn, &mag)) return 0;
    return L8_LE(l8_bits(mag), limit) ? 1 : 0;
}

int em_level8_port_001B1560(const EmLevel8PortHooks *h, uint32_t self, uint32_t pos, float limit, int32_t *result,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001B1560(&o, self, pos, l8_bits(limit));
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B2B10 (C byte-identical): dst = src turned by self's yaw: the matrix
 * 0x70003400 = identity (001029C0) turned about y by self +0xC4 (00102BB0),
 * then 001026A0(dst, 0x70003400, src).
 * ---------------------------------------------------------------------- */
void l8_001B2B10(L8 *o, uint32_t self, uint32_t dst, uint32_t src)
{
    if (l8_c_001029C0(o, S_70003400)) return;
    if (l8_c_00102BB0(o, S_70003400, S_70003400, fl(l8_u32(o, self + 0xC4)))) return;
    l8_c_001026A0(o, dst, S_70003400, src);
}

int em_level8_port_001B2B10(const EmLevel8PortHooks *h, uint32_t self, uint32_t dst, uint32_t src,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_001B2B10(&o, self, dst, src);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 001B30E0 (NEARMISS; from the instructions): the floor under a point
 * from the probe's hit list (count 0x700031E0; per hit the halfword flags
 * 0x70003170, the slope 0x282250 and the height 0x700030F0). Hits with
 * flag bit 0 and |slope| <= pi/3 (0011DF78) are floors: the first one
 * sets out = its height; a later one that is above out: when pos.y is
 * below it the function returns 1 at once, else out = it. A hit without
 * the flag, once a floor is found, returns 1 when pos.y <= its height.
 * Returns whether a floor was found.
 * ---------------------------------------------------------------------- */
int32_t l8_001B30E0(L8 *o, uint32_t pos, uint32_t out)
{
    if (l8_u32(o, S_700031E0) == 0) return 0;
    uint32_t flags = 0x70003170u, slope = 0x00282250u, height = 0x700030F0u;
    int32_t found = 0;
    for (int32_t i = 0; i < (int32_t)l8_u32(o, S_700031E0); i++, flags += 2, slope += 4, height += 4) {
        if (l8_u16(o, flags) & 1u) {
            float mag = 0.0f;
            if (l8_c_0011DF78(o, fl(l8_u32(o, slope)), &mag)) return 0;
            if (!L8_LE(l8_bits(mag), F_PI_3)) continue;
            if (!found) {
                uint32_t hgt = l8_u32(o, height);
                found = 1;
                l8_w32(o, out, hgt);
                continue;
            }
            uint32_t cur = l8_u32(o, out);
            uint32_t hgt = l8_u32(o, height);
            if (!L8_LT(cur, hgt)) continue;
            if (L8_LT(l8_u32(o, pos + 4), hgt)) return 1;
            l8_w32(o, out, hgt);
        } else if (found) {
            uint32_t y = l8_u32(o, pos + 4);
            uint32_t hgt = l8_u32(o, height);
            if (L8_LE(y, hgt)) return 1;
        }
    }
    return found;
}

int em_level8_port_001B30E0(const EmLevel8PortHooks *h, uint32_t pos, uint32_t out, int32_t *result,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001B30E0(&o, pos, out);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B2BF0 (word asm; from the instructions): a ground probe (frame 0x60,
 * one local at frame +0x5C). pos +0xC = 1.0; 0019BC40(pos); hit =
 * 0019AD00(self, pos, 7): none -> 001B30E0(pos, out). Bit 1 of hit with the
 * word 0x700031D4 zero -> 4. The hit polygon's (*0x700031D0) halfword +0x1A:
 * bits 0x1800 -> 4; else 0019A310(&local), r = 001B2E50(pos, out); bit 0x2000
 * -> r | 4; local <= limit -> 001B30E0(pos, out); else r | 4.
 * ---------------------------------------------------------------------- */
int32_t l8_001B2BF0(L8 *o, uint32_t self, uint32_t pos, uint32_t out, uint32_t limit, uint32_t sp)
{
    uint32_t local = sp - 0x60u + 0x5Cu;
    l8_w32(o, pos + 0xC, F_ONE);
    if (l8_c_0019BC40(o, pos)) return 0;
    int32_t hit = 0;
    if (l8_c_0019AD00(o, self, pos, 7, &hit)) return 0;
    if (hit == 0) return l8_001B30E0(o, pos, out);
    if ((hit & 2) && l8_u32(o, 0x700031D4u) == 0) return 4;
    uint32_t poly = l8_u32(o, S_700031D0);
    int32_t kind = l8_s16(o, poly + 0x1A);
    if (kind & 0x1800) return 4;
    int32_t ignored = 0;
    if (l8_c_0019A310(o, local, &ignored)) return 0;
    int32_t r = 0;
    if (l8_c_001B2E50(o, pos, out, &r)) return 0;
    if (kind & 0x2000) return r | 4;
    if (L8_LE(l8_u32(o, local), limit)) return l8_001B30E0(o, pos, out);
    return r | 4;
}

int em_level8_port_001B2BF0(const EmLevel8PortHooks *h, uint32_t self, uint32_t pos, uint32_t out, float limit,
                            uint32_t sp, int32_t *result, EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001B2BF0(&o, self, pos, out, l8_bits(limit), sp);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B32F0 / 001B3440 (NEARMISS; from the instructions): a wall probe
 * (frame 0x50, one local at frame +0x4C). 001B32F0(self, pos, limit): pos
 * +0xC = 1.0, hit = 0019AD00(self, pos, 7); 001B3440(self, a, b, limit): b
 * +0xC = a +0xC = 1.0, hit = 0019AB20(self, a, b, 7). No hit -> 0;
 * 0019A310(&local) zero -> 0. 001B32F0 shoves when the local is above the
 * limit, 001B3440 when it is at or below it: self +0xB0 += the shove
 * 0x700031C0 (001028B8) and the result is hit; otherwise 0.
 * ---------------------------------------------------------------------- */
static int32_t l8_wall(L8 *o, uint32_t self, uint32_t local, int32_t hit, uint32_t limit, int shove_above)
{
    if (!hit) return 0;
    int32_t got = 0;
    if (l8_c_0019A310(o, local, &got)) return 0;
    if (!got) return 0;
    int within = L8_LE(l8_u32(o, local), limit);
    if (shove_above ? within : !within) return 0;
    if (l8_c_001028B8(o, self + 0xB0, self + 0xB0, S_700031C0)) return 0;
    return hit;
}

int32_t l8_001B32F0(L8 *o, uint32_t self, uint32_t pos, uint32_t limit, uint32_t sp)
{
    l8_w32(o, pos + 0xC, F_ONE);
    int32_t hit = 0;
    if (l8_c_0019AD00(o, self, pos, 7, &hit)) return 0;
    return l8_wall(o, self, sp - 0x50u + 0x4Cu, hit, limit, 1);
}

int32_t l8_001B3440(L8 *o, uint32_t self, uint32_t a, uint32_t b, uint32_t limit, uint32_t sp)
{
    l8_w32(o, b + 0xC, F_ONE);
    l8_w32(o, a + 0xC, F_ONE);
    int32_t hit = 0;
    if (l8_c_0019AB20(o, self, a, b, 7, &hit)) return 0;
    return l8_wall(o, self, sp - 0x50u + 0x4Cu, hit, limit, 0);
}

int em_level8_port_001B32F0(const EmLevel8PortHooks *h, uint32_t self, uint32_t pos, float limit, uint32_t sp,
                            int32_t *result, EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001B32F0(&o, self, pos, l8_bits(limit), sp);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

int em_level8_port_001B3440(const EmLevel8PortHooks *h, uint32_t self, uint32_t a, uint32_t b, float limit,
                            uint32_t sp, int32_t *result, EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001B3440(&o, self, a, b, l8_bits(limit), sp);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B3580 (C byte-identical): a heading toward a point with a random
 * spread: 0x70003600 = (pos, 1.0) - self +0xB0 (001028B8 in place);
 * 001B1470(001B1240(self +0xB0, x, z) + (pi/3)(2 (b / 255) - 1)), b =
 * (00122BB8() >> 7) & 0xFF; returns that (f0; the original sets no v0).
 * ---------------------------------------------------------------------- */
uint32_t l8_001B3580(L8 *o, uint32_t self, uint32_t pos)
{
    for (uint32_t k = 0; k < 12; k += 4) l8_w32(o, S_70003600 + k, l8_u32(o, pos + k));
    l8_w32(o, S_70003600 + 12u, F_ONE);
    if (l8_c_001028B8(o, S_70003600, S_70003600, self + 0xB0)) return 0;
    uint32_t x = l8_u32(o, S_70003600);
    uint32_t z = l8_u32(o, S_70003600 + 8u);
    float bearing = 0.0f;
    if (l8_c_001B1240(o, self + 0xB0, fl(x), fl(z), &bearing)) return 0;
    int32_t r = 0;
    if (l8_c_00122BB8(o, &r)) return 0;
    uint32_t b = L8_CVT((uint32_t)l8_sra((uint32_t)r, 7) & 0xFFu);
    uint32_t u = L8_MUL(F_2, L8_DIV(b, 0x437F0000u));
    uint32_t spread = L8_MUL(0x3F860A92u, L8_SUB(u, F_ONE));
    float out = 0.0f;
    if (l8_c_001B1470(o, fl(L8_ADD(l8_bits(bearing), spread)), &out)) return 0;
    return l8_bits(out);
}

int em_level8_port_001B3580(const EmLevel8PortHooks *h, uint32_t self, uint32_t pos, float *result,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    uint32_t f = l8_001B3580(&o, self, pos);
    if (l8_failed(&o)) return -1;
    *result = fl(f);
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B55E0 (NEARMISS; from the instructions): a spark at the point 2
 * ahead of the player facing the creature: 0x70003610 = (0, 0, 0, 1), its
 * y = 001B1240(player +0xB0 of D_00810360, self x, self z) (stored before
 * the matrix call); 0x70003400 = that turn (001029C0, 00102BB0);
 * 0x70003600 = (0, 0, 2, 1) through it plus D_00810360 (001026A0,
 * 001028B8); its y += (0x70003680 =) 5 b / 255, b = (00122BB8() >> 5) &
 * 0xFF; 001EFD90(class, 0x70003600, 0x70003610) with the class by the
 * signed byte kind: 0 -> 0x80000006, 1 -> 0x80000022, 2 -> 0x80000023.
 * Another kind leaves the caller's register as the class in the original;
 * the translation faults 7 there (before 001EFD90) instead.
 * ---------------------------------------------------------------------- */
void l8_001B55E0(L8 *o, uint32_t self, int32_t kind)
{
    l8_w32(o, 0x70003618u, 0);
    l8_w32(o, 0x70003614u, 0);
    l8_w32(o, 0x70003610u, 0);
    l8_w32(o, 0x7000361Cu, F_ONE);
    uint32_t x = l8_u32(o, self + 0xB0);
    uint32_t z = l8_u32(o, self + 0xB8);
    float yaw = 0.0f;
    if (l8_c_001B1240(o, 0x00810360u, fl(x), fl(z), &yaw)) return;
    l8_w32(o, 0x70003614u, l8_bits(yaw));
    if (l8_c_001029C0(o, S_70003400)) return;
    if (l8_c_00102BB0(o, S_70003400, S_70003400, fl(l8_u32(o, 0x70003614u)))) return;
    l8_w32(o, S_70003600, 0);
    l8_w32(o, S_70003600 + 4u, 0);
    l8_w32(o, S_70003600 + 8u, F_2);
    l8_w32(o, S_70003600 + 12u, F_ONE);
    if (l8_c_001026A0(o, S_70003600, S_70003400, S_70003600)) return;
    if (l8_c_001028B8(o, S_70003600, 0x00810360u, S_70003600)) return;
    int32_t r = 0;
    if (l8_c_00122BB8(o, &r)) return;
    uint32_t b = L8_CVT((uint32_t)l8_sra((uint32_t)r, 5) & 0xFFu);
    uint32_t y = l8_u32(o, S_70003600 + 4u);
    uint32_t rise = L8_MUL(F_5, L8_DIV(b, 0x437F0000u));
    l8_w32(o, 0x70003680u, rise);
    l8_w32(o, S_70003600 + 4u, L8_ADD(y, rise));
    int32_t k = (int8_t)(uint8_t)kind;
    uint32_t cls;
    if (k == 2) cls = 0x80000023u;
    else if (k == 1) cls = 0x80000022u;
    else if (k == 0) cls = 0x80000006u;
    else {
        l8_latch(o, 0x001B55E0u, EM_LEVEL8_PORT_FAULT_REGISTER);
        return;
    }
    l8_c_001EFD90(o, (int32_t)cls, S_70003600, 0x70003610u);
}

int em_level8_port_001B55E0(const EmLevel8PortHooks *h, uint32_t self, int32_t kind, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_001B55E0(&o, self, kind);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 001A7BA0 (NEARMISS; from the instructions): the overlap test of two
 * objects' bone boxes (a = the creature, b = the player; 001A7B80 passes
 * b = D_008102B0, masks 0x10 and 0x20). Returns 1 on an overlap, else 0.
 * The frame is 0x290 bytes; the original keeps a, the group counts and
 * pointers, the last bone indices, the mask bits and the outer counter in
 * frame words (+0x18C, +0xE0, +0x100, +0x110, +0xF0, +0xD0, +0x140 ..
 * +0x170, +0x120) and reloads them from there; the translation makes the
 * same frame accesses (b's first group, saved from s1 at +0x13C, is frame
 * bookkeeping of a callee-saved register and is kept in C). The difference points live at frame
 * +0x190 (16 bytes each, up to 16).
 *   For each group g1 of a's list (a +0x58: count, then groups of stride
 *   halfword +4; a leading group with halfword +0xA == -2 is skipped and
 *   not counted) that passes the mask test (bit 0x10: its byte 0 & a +0x5C,
 *   or bit 0x20: its byte 1 & a +0x5D): n1 = halfword +6 (0x70003B86, at
 *   most 4); the bone (byte +3) matrix into 0x70003480 when it changed
 *   (00102958 of (a +0x110)[bone] +0x90); its points (12 bytes each from
 *   +0x18) through it into 0x70003400.. (w 0; the box min/max kept in
 *   f21/f20, f23/f22, f25/f24). Then each group g2 of b's list alike (masks
 *   a3, b +0x5C / +0x5D, 0x70003B88, 0x700034C0, 0x70003440..): its box is
 *   kept from g2's points, but each coordinate compare reads g1's point
 *   (0x70003400 + 16 k) and takes g2's (the original's order; kept).
 *   With n1 n2 >= 4 and the boxes overlapping on all three axes (strict
 *   compares): the differences p1 - p2 normalised into the frame array;
 *   the triangle of the first three, its normal (00102718 cross,
 *   00102760), turned toward the origin (0x70003680 = |dot|); then for
 *   each further difference: a support test by dots (0x70003684), the
 *   replaced corner (index by two self-compares that are always true on
 *   the EE, so corner 0), the new normal and, when the origin is behind
 *   it, the result 1.
 * ---------------------------------------------------------------------- */
#define A7_A     0x18Cu
#define A7_N1    0xE0u
#define A7_G1    0x100u
#define A7_N2    0x110u
#define A7_BONE1 0xF0u
#define A7_I     0xD0u
#define A7_M1    0x140u
#define A7_M2    0x150u
#define A7_M3    0x160u
#define A7_M4    0x170u
#define A7_BONE2 0x120u
#define A7_DIFF  0x190u

static void l8_box_point(L8 *o, uint32_t base, int32_t s, uint32_t out, uint32_t matrix)
{
    uint32_t x = l8_u32(o, base + ((uint32_t)s << 2));
    l8_w32(o, S_70003600, x);
    uint32_t y = l8_u32(o, base + ((uint32_t)(s + 1) << 2));
    l8_w32(o, S_70003600 + 4u, y);
    uint32_t z = l8_u32(o, base + ((uint32_t)(s + 2) << 2));
    l8_w32(o, S_70003600 + 8u, z);
    l8_w32(o, S_70003600 + 12u, F_ONE);
    l8_c_001026A0(o, out, matrix, S_70003600);
}

/* The simplex stage of 001A7BA0 (from its box overlap on): returns 1 when
 * the origin is enclosed (the function then returns 1), else 0. */
static void l8_neg_store(L8 *o, uint32_t at) { l8_w32(o, at, L8_NEG(l8_u32(o, at))); }

static int l8_a7_simplex(L8 *o, uint32_t fr, int32_t count)
{
    uint32_t k = 0;
    uint32_t p1 = S_70003400;
    for (int32_t i = 0; i < l8_s16(o, 0x70003B86u); i++, p1 += 0x10u) {
        uint32_t p2 = 0x70003440u;
        for (int32_t j = 0; j < l8_s16(o, 0x70003B88u); j++, p2 += 0x10u) {
            if (l8_c_001028D0(o, S_70003600, p1, p2)) return 0;
            uint32_t slot = fr + A7_DIFF + (k << 4);
            k++;
            if (l8_c_00102760(o, slot, S_70003600)) return 0;
        }
    }
    if (l8_c_00102948(o, 0x70003440u, fr + A7_DIFF)) return 0;
    if (l8_c_00102948(o, 0x70003450u, fr + A7_DIFF + 0x10u)) return 0;
    if (l8_c_00102948(o, 0x70003460u, fr + A7_DIFF + 0x20u)) return 0;
    if (l8_c_001028D0(o, 0x70003610u, 0x70003450u, 0x70003440u)) return 0;
    if (l8_c_001028D0(o, 0x70003620u, 0x70003460u, 0x70003440u)) return 0;
    if (l8_c_00102718(o, 0x70003630u, 0x70003610u, 0x70003620u)) return 0;
    if (l8_c_00102760(o, 0x70003630u, 0x70003630u)) return 0;
    float d = 0.0f;
    if (l8_c_00102738(o, 0x70003440u, 0x70003630u, &d)) return 0;
    l8_w32(o, 0x70003680u, l8_bits(d));
    if (L8_LT(l8_bits(d), F_ZERO)) {
        l8_w32(o, 0x70003680u, L8_NEG(l8_bits(d)));
        l8_neg_store(o, 0x70003630u);
        l8_neg_store(o, 0x70003634u);
        l8_neg_store(o, 0x70003638u);
    }
    if (count < 4) return 0;
    uint32_t s0 = fr + A7_DIFF + 0x30u;
    for (int32_t s3 = 3; s3 < count; s3++, s0 += 0x10u) {
        float e = 0.0f;
        if (l8_c_00102738(o, 0x70003630u, s0, &e)) return 0;
        l8_w32(o, 0x70003684u, l8_bits(e));
        uint32_t f1 = l8_u32(o, 0x70003680u);
        uint32_t f0 = l8_u32(o, 0x70003684u);
        if (L8_LT(f1, f0)) continue;
        if (l8_c_001028D0(o, S_70003600, 0x70003440u, s0)) return 0;
        uint32_t corner = 0;
        float g = 0.0f;
        if (l8_c_00102738(o, S_70003600, S_70003600, &g)) return 0;
        l8_w32(o, 0x70003684u, l8_bits(g));
        if (l8_c_001028D0(o, S_70003600, 0x70003450u, s0)) return 0;
        if (l8_c_00102738(o, S_70003600, S_70003600, &g)) return 0;
        l8_w32(o, 0x70003684u, l8_bits(g));
        if (!L8_LE(l8_bits(g), l8_bits(g))) {
            corner = 1;
            l8_w32(o, 0x70003684u, l8_bits(g));
        }
        if (l8_c_001028D0(o, S_70003600, 0x70003460u, s0)) return 0;
        if (l8_c_00102738(o, S_70003600, S_70003600, &g)) return 0;
        l8_w32(o, 0x70003684u, l8_bits(g));
        if (!L8_LE(l8_bits(g), l8_bits(g))) corner = 2;
        if (l8_c_00102948(o, 0x70003440u + (corner << 4), s0)) return 0;
        if (l8_c_001028D0(o, 0x70003610u, 0x70003450u, 0x70003440u)) return 0;
        if (l8_c_001028D0(o, 0x70003620u, 0x70003460u, 0x70003440u)) return 0;
        if (l8_c_00102718(o, S_70003600, 0x70003610u, 0x70003620u)) return 0;
        if (l8_c_00102760(o, 0x70003630u, S_70003600)) return 0;
        if (l8_c_00102738(o, 0x70003630u, S_70003600, &g)) return 0;
        l8_w32(o, 0x70003684u, l8_bits(g));
        if (L8_LT(l8_bits(g), F_ZERO)) {
            uint32_t x = l8_u32(o, S_70003600);
            uint32_t y = l8_u32(o, S_70003600 + 4u);
            uint32_t z = l8_u32(o, S_70003600 + 8u);
            l8_w32(o, 0x70003630u, L8_NEG(x));
            l8_w32(o, 0x70003634u, L8_NEG(y));
            l8_w32(o, 0x70003638u, L8_NEG(z));
        } else if (l8_c_00102948(o, 0x70003630u, S_70003600)) {
            return 0;
        }
        if (l8_c_00102738(o, 0x70003630u, 0x70003440u, &g)) return 0;
        if (L8_LT(l8_bits(g), F_ZERO)) return 1;
    }
    return 0;
}

int32_t l8_001A7BA0(L8 *o, uint32_t a, uint32_t b, uint32_t m1, uint32_t m2, uint32_t sp)
{
    uint32_t fr = sp - 0x290u;
    uint32_t la = l8_u32(o, a + 0x58);
    l8_w32(o, fr + A7_A, a);
    if (!la) return 0;
    uint32_t lb = l8_u32(o, b + 0x58);
    if (!lb) return 0;
    l8_w32(o, fr + A7_N1, l8_u32(o, la));
    int32_t lead = l8_s16(o, la + 0xA);
    l8_w32(o, fr + A7_G1, la + 4u);
    if (lead == -2) {
        uint32_t g = l8_u32(o, fr + A7_G1);
        uint32_t step = l8_u16(o, g + 4);
        l8_w32(o, fr + A7_N1, l8_u32(o, fr + A7_N1) - 1u);
        l8_w32(o, fr + A7_G1, l8_u32(o, fr + A7_G1) + step);
    }
    uint32_t nb = l8_u32(o, lb);
    int32_t leadb = l8_s16(o, lb + 0xA);
    l8_w32(o, fr + A7_N2, nb);
    uint32_t s1 = lb + 4u;
    if (leadb == -2) {
        uint32_t n = l8_u32(o, fr + A7_N2);
        uint32_t step = l8_u16(o, s1 + 4);
        s1 += step;
        l8_w32(o, fr + A7_N2, n - 1u);
    }
    l8_w32(o, fr + A7_BONE1, 0x3E7);
    uint32_t n1 = l8_u32(o, fr + A7_N1);
    /* The original saves s1 at frame +0x13C and reloads it there: a callee-
     * saved register's frame slot, which the translation keeps in C. */
    const uint32_t g2_start = s1;
    l8_w32(o, fr + A7_I, 0);
    if (!(0 < (int32_t)n1)) return 0;
    l8_w32(o, fr + A7_M1, m1 & 0x10u);
    l8_w32(o, fr + A7_M2, m1 & 0x20u);
    l8_w32(o, fr + A7_M3, m2 & 0x10u);
    l8_w32(o, fr + A7_M4, m2 & 0x20u);
    uint32_t f20 = 0, f21 = 0, f22 = 0, f23 = 0, f24 = 0, f25 = 0;
    uint32_t f26 = 0, f27 = 0, f28 = 0, f29 = 0, f30 = 0, f31 = 0;
    for (;;) {
        int pass = 0;
        if (l8_u32(o, fr + A7_M1)) {
            uint32_t g = l8_u32(o, fr + A7_G1);
            uint32_t bits = l8_u8(o, g);
            uint32_t aa = l8_u32(o, fr + A7_A);
            if (bits & l8_u8(o, aa + 0x5C)) pass = 1;
        }
        if (!pass && l8_u32(o, fr + A7_M2)) {
            uint32_t g = l8_u32(o, fr + A7_G1);
            uint32_t bits = l8_u8(o, g + 1);
            uint32_t aa = l8_u32(o, fr + A7_A);
            if (bits & l8_u8(o, aa + 0x5D)) pass = 1;
        }
        if (pass) {
            uint32_t g = l8_u32(o, fr + A7_G1);
            l8_w16(o, 0x70003B86u, (uint32_t)l8_s16(o, g + 6));
            g = l8_u32(o, fr + A7_G1);
            uint32_t bone = l8_u8(o, g + 3);
            if (l8_u32(o, fr + A7_BONE1) != bone) {
                uint32_t aa = l8_u32(o, fr + A7_A);
                l8_w32(o, fr + A7_BONE1, bone);
                uint32_t node = l8_u32(o, aa + (bone << 2) + 0x110u);
                if (l8_c_00102958(o, 0x70003480u, node + 0x90)) return 0;
            }
            uint32_t s3 = l8_u32(o, fr + A7_G1) + 8u;
            if (!(l8_s16(o, 0x70003B86u) < 5)) l8_w16(o, 0x70003B86u, 4);
            int32_t s2 = 4;
            uint32_t s5 = S_70003400;
            for (int32_t s0 = 0; s0 < l8_s16(o, 0x70003B86u); s0++, s5 += 0x10u, s2 += 3) {
                l8_box_point(o, s3, s2, s5, 0x70003480u);
                if (l8_failed(o)) return 0;
                l8_w32(o, s5 + 0xC, 0);
                if (s0 == 0) {
                    f20 = f21 = l8_u32(o, S_70003400);
                    f22 = f23 = l8_u32(o, S_70003400 + 4u);
                    f24 = f25 = l8_u32(o, S_70003400 + 8u);
                    continue;
                }
                uint32_t v = l8_u32(o, s5);
                if (!L8_LE(f21, v)) f21 = v;
                if (L8_LT(f20, v)) f20 = v;
                v = l8_u32(o, s5 + 4);
                if (!L8_LE(f23, v)) f23 = v;
                if (L8_LT(f22, v)) f22 = v;
                v = l8_u32(o, s5 + 8);
                if (!L8_LE(f25, v)) f25 = v;
                if (L8_LT(f24, v)) f24 = v;
            }
            l8_w32(o, fr + A7_BONE2, 0x3E7);
            int32_t fp = 0;
            if (0 < (int32_t)l8_u32(o, fr + A7_N2)) {
                for (;;) {
                    int pass2 = 0;
                    if (l8_u32(o, fr + A7_M3)) {
                        uint32_t bits = l8_u8(o, s1);
                        if (bits & l8_u8(o, b + 0x5C)) pass2 = 1;
                    }
                    if (!pass2 && l8_u32(o, fr + A7_M4)) {
                        uint32_t bits = l8_u8(o, s1 + 1);
                        if (bits & l8_u8(o, b + 0x5D)) pass2 = 1;
                    }
                    if (pass2) {
                        l8_w16(o, 0x70003B88u, (uint32_t)l8_s16(o, s1 + 6));
                        uint32_t bone2 = l8_u8(o, s1 + 3);
                        if (l8_u32(o, fr + A7_BONE2) != bone2) {
                            l8_w32(o, fr + A7_BONE2, bone2);
                            uint32_t node = l8_u32(o, b + (bone2 << 2) + 0x110u);
                            if (l8_c_00102958(o, 0x700034C0u, node + 0x90)) return 0;
                        }
                        uint32_t base2 = s1 + 8u;
                        if (!(l8_s16(o, 0x70003B88u) < 5)) l8_w16(o, 0x70003B88u, 4);
                        uint32_t q3 = 0x70003440u, q4 = S_70003400;
                        int32_t q6 = 4;
                        int32_t n2 = 0;
                        for (int32_t q5 = 0;; q5++, q3 += 0x10u, q4 += 0x10u, q6 += 3) {
                            n2 = l8_s16(o, 0x70003B88u);
                            if (!(q5 < n2)) break;
                            l8_box_point(o, base2, q6, q3, 0x700034C0u);
                            if (l8_failed(o)) return 0;
                            l8_w32(o, q3 + 0xC, 0);
                            if (q5 == 0) {
                                f26 = f27 = l8_u32(o, 0x70003440u);
                                f28 = f29 = l8_u32(o, 0x70003444u);
                                f30 = f31 = l8_u32(o, 0x70003448u);
                                continue;
                            }
                            uint32_t v = l8_u32(o, q4);
                            if (!L8_LE(f27, v)) f27 = l8_u32(o, q3);
                            if (L8_LT(f26, v)) f26 = l8_u32(o, q3);
                            v = l8_u32(o, q4 + 4);
                            if (!L8_LE(f29, v)) f29 = l8_u32(o, q3 + 4);
                            if (L8_LT(f28, v)) f28 = l8_u32(o, q3 + 4);
                            v = l8_u32(o, q4 + 8);
                            if (!L8_LE(f31, v)) f31 = l8_u32(o, q3 + 8);
                            if (L8_LT(f30, v)) f30 = l8_u32(o, q3 + 8);
                        }
                        int32_t cnt1 = l8_s16(o, 0x70003B86u);
                        int32_t s2n = (int32_t)((uint32_t)cnt1 * (uint32_t)n2);
                        if (s2n >= 4 && L8_LT(f21, f26) && L8_LT(f23, f28) && L8_LT(f25, f30) &&
                            !L8_LE(f20, f27) && !L8_LE(f22, f29) && !L8_LE(f24, f31)) {
                            if (l8_a7_simplex(o, fr, s2n)) return 1;
                            if (l8_failed(o)) return 0;
                        }
                    }
                    uint32_t step = l8_u16(o, s1 + 4);
                    fp++;
                    int more = fp < (int32_t)l8_u32(o, fr + A7_N2);
                    s1 += step;
                    if (!more) break;
                }
            }
        }
        uint32_t g = l8_u32(o, fr + A7_G1);
        s1 = g2_start;
        uint32_t step = l8_u16(o, g + 4);
        l8_w32(o, fr + A7_I, l8_u32(o, fr + A7_I) + 1u);
        uint32_t i = l8_u32(o, fr + A7_I);
        uint32_t n = l8_u32(o, fr + A7_N1);
        int more = (int32_t)i < (int32_t)n;
        uint32_t next = l8_u32(o, fr + A7_G1) + step;
        l8_w32(o, fr + A7_G1, next);
        if (!more) break;
    }
    return 0;
}

int em_level8_port_001A7BA0(const EmLevel8PortHooks *h, uint32_t a, uint32_t b, uint32_t m1, uint32_t m2,
                            uint32_t sp, int32_t *result, EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001A7BA0(&o, a, b, m1, m2, sp);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001A7B80 (C byte-identical): a tail call of 001A7BA0(self, D_008102B0,
 * 0x10, 0x20) (same stack pointer).
 * ---------------------------------------------------------------------- */
int32_t l8_001A7B80(L8 *o, uint32_t self, uint32_t sp) { return l8_001A7BA0(o, self, D_008102B0, 0x10, 0x20, sp); }

int em_level8_port_001A7B80(const EmLevel8PortHooks *h, uint32_t self, uint32_t sp, int32_t *result,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001A7B80(&o, self, sp);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}
