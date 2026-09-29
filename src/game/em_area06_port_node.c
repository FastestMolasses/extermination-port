/* AREA01 upper floor / AREA06 boot functions, part 2: the node behaviour
 * 00219870 (AREA06's deferred g[11]) and its helpers, the debris spawner
 * 0021AE90, 001A8F40, 00176180, the player state 00169250 and the camera
 * states 001944B0 (see em_area06_port.h, docs/AREA06_PORT.md). The
 * conventions are those of em_area06_port.c.
 */
#include "em_area06_port_internal.h"

/* ------------------------------------------------------------------------
 * 00176180 (NEARMISS; the instructions agree with its C): unless actor
 * +0x1F0 is 0x3C: 0011E748(00102738(D_00281B50, D_00281B50)) (result
 * unused), n = the normal of D_00281B50 (00102760), p = actor +0xB0 +
 * D_00281B50 (00102948, 001028B8), probe = (p.x + 4.5 n.x, target +4, p.z
 * + 4.5 n.z); 0019AFE0(actor, target, probe, 6) nonzero adds the
 * scratchpad offsets 0x700031C0 / 0x700031C8 to p.x / p.z; actor +0xB0 /
 * +0xB8 = p.x / p.z. With actor +4 == 1 and +5 == 0x1D, 0x700031F0 = 1.
 * Frame locals at sp - 0x40 / - 0x30 / - 0x20 / - 0x10 (frame 0x80).
 * ---------------------------------------------------------------------- */
int em_area06_port_00176180(const EmArea06PortHooks *h, uint32_t actor, uint32_t a1, uint32_t target, uint32_t sp,
                            EmArea06PortFault *fault)
{
    A6 o;
    (void)a1; /* the original passes its a1 through to nothing */
    if (a6_begin(&o, h, fault)) return -1;
    if (a6_u8(&o, actor + 0x1F0) == 0x3C) return a6_end(&o);
    uint32_t fr = sp - 0x80, n = fr + 0x40, a50 = fr + 0x50, p = fr + 0x60, probe = fr + 0x70;
    float d = 0.0f, s = 0.0f;
    if (a6_c_00102738(&o, D_00281B50, D_00281B50, &d)) return -1;
    if (a6_c_0011E748(&o, d, &s)) return -1;
    if (a6_c_00102760(&o, n, D_00281B50)) return -1;
    if (a6_c_00102948(&o, a50, actor + 0xB0)) return -1;
    if (a6_c_001028B8(&o, p, a50, D_00281B50)) return -1;
    uint32_t nx = a6_u32(&o, n);
    uint32_t nz = a6_u32(&o, n + 8);
    uint32_t px = a6_u32(&o, p);
    a6_w32(&o, probe, A6_ADD(px, A6_MUL(F_4_5, nx)));
    uint32_t pz = a6_u32(&o, p + 8);
    a6_w32(&o, probe + 8, A6_ADD(pz, A6_MUL(F_4_5, nz)));
    a6_w32(&o, probe + 4, a6_u32(&o, target + 4));
    int32_t hit = 0;
    if (a6_c_0019AFE0(&o, actor, target, probe, 6, &hit)) return -1;
    if (hit != 0) {
        uint32_t ox = a6_u32(&o, S_700031C0);
        uint32_t x = a6_u32(&o, p);
        uint32_t nxs = A6_ADD(x, ox);
        uint32_t oz = a6_u32(&o, S_700031C8);
        a6_w32(&o, p, nxs);
        uint32_t z = a6_u32(&o, p + 8);
        a6_w32(&o, p + 8, A6_ADD(z, oz));
    }
    a6_w32(&o, actor + 0xB0, a6_u32(&o, p));
    a6_w32(&o, actor + 0xB8, a6_u32(&o, p + 8));
    if (a6_u8(&o, actor + 4) == 1 && a6_u8(&o, actor + 5) == 0x1D) a6_w32(&o, S_700031F0, 1);
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 001A8F40 (NEARMISS; the instructions agree with its C): the contact test
 * of a pair (a, b): d = b +0xB0 - a +0xB0 into 0x700038A0 (w cleared),
 * 0x70003A20 = d . d; v = 3.0 + *(a +0x30); 0x70003A24 = v, then v v;
 * when 0x70003A20 (read again) <= v v: b halfword +0x36 = 0x2014 and the
 * halfword 0x70003B88 = 0.
 * ---------------------------------------------------------------------- */
int em_area06_port_001A8F40(const EmArea06PortHooks *h, uint32_t a, uint32_t b, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    if (a6_c_001028D0(&o, S_700038A0, b + 0xB0, a + 0xB0)) return -1;
    a6_w32(&o, 0x700038ACu, 0);
    float d = 0.0f;
    if (a6_c_00102738(&o, S_700038A0, S_700038A0, &d)) return -1;
    a6_w32(&o, S_70003A20, a6_bits(d));
    uint32_t r = a6_u32(&o, a + 0x30);
    uint32_t dd = a6_u32(&o, S_70003A20);
    uint32_t v = A6_ADD(F_3, a6_u32(&o, r));
    uint32_t vv = A6_MUL(v, v);
    a6_w32(&o, S_70003A24, v);
    a6_w32(&o, S_70003A24, vv);
    if (A6_LE(dd, vv)) {
        a6_w16(&o, b + 0x36, 0x2014);
        a6_w16(&o, S_70003B88, 0);
    }
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 0021A440 (C byte-identical; the original passes its a1 on, which the
 * decomp's C also states since 2026-09-28): sub = self + 0x1F0; sub +0x20 = src
 * (00102948); sub +0x30 = |sub - sub +0x20|, sub +0x34 = |sub +0x10 - sub
 * +0x20| (001028D0 into 0x700038A0, 0011E748 of the sum of squares);
 * sub +0x3C = 1.
 * ---------------------------------------------------------------------- */
void a6_0021A440(A6 *o, uint32_t self, uint32_t src)
{
    uint32_t s = self + 0x1F0;
    float r = 0.0f;
    if (a6_c_00102948(o, s + 0x20, src)) return;
    if (a6_c_001028D0(o, S_700038A0, s + 0x20, s)) return;
    uint32_t x = a6_u32(o, S_700038A0), y = a6_u32(o, S_700038A0 + 4), z = a6_u32(o, S_700038A0 + 8);
    if (a6_c_0011E748(o, fl(a6_sumsq3(x, y, z)), &r)) return;
    a6_w32(o, s + 0x30, a6_bits(r));
    if (a6_c_001028D0(o, S_700038A0, s + 0x20, s + 0x10)) return;
    x = a6_u32(o, S_700038A0);
    y = a6_u32(o, S_700038A0 + 4);
    z = a6_u32(o, S_700038A0 + 8);
    if (a6_c_0011E748(o, fl(a6_sumsq3(x, y, z)), &r)) return;
    a6_w32(o, s + 0x34, a6_bits(r));
    a6_w32(o, s + 0x3C, 1);
}

int em_area06_port_0021A440(const EmArea06PortHooks *h, uint32_t self, uint32_t src, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_0021A440(&o, self, src);
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 00219F50 (C byte-identical): sub = self + 0x1F0. sub = self +0xB0; the
 * matrix 0x700036A0 = self +0xD0 (00102958); 0x700038A0 = (0, 0, 100, 1)
 * through it (001026A0); dir = that - self +0xB0, w cleared, normalized
 * (0x700038B0); self +0x2D4 = 6 dir.x, +0x2D0 = 6 dir.z; sub +0x10 =
 * 0x700031B0 when 0019A570(self +0xB0, 0x700038A0, 4, 0) hits, else
 * 0x700038A0; len = |sub +0x10 - sub| (w cleared) at 0x70003A20 and the
 * matrix row 0x700036C0 scaled by it; 001A2370(self, 0x700036A0); sub
 * +0x30 = self +0x2C8 = |sub - sub +0x10|, sub +0x3C = 0, +0x38 = 1.0.
 * ---------------------------------------------------------------------- */
void a6_00219F50(A6 *o, uint32_t self)
{
    uint32_t s = self + 0x1F0;
    float r = 0.0f;
    int32_t hit = 0;
    if (a6_c_00102948(o, s, self + 0xB0)) return;
    if (a6_c_00102958(o, S_700036A0, self + 0xD0)) return;
    a6_w32(o, S_700038A0 + 4, 0);
    a6_w32(o, S_700038A0, 0);
    a6_w32(o, S_700038A0 + 8, F_100);
    a6_w32(o, S_700038A0 + 0xC, F_ONE);
    if (a6_c_001026A0(o, S_700038A0, S_700036A0, S_700038A0)) return;
    if (a6_c_001028D0(o, S_700038B0, S_700038A0, self + 0xB0)) return;
    a6_w32(o, S_700038B0 + 0xC, 0);
    if (a6_c_00102760(o, S_700038B0, S_700038B0)) return;
    uint32_t x = a6_u32(o, S_700038B0);
    a6_w32(o, self + 0x2D4, A6_MUL(F_6, x));
    uint32_t z = a6_u32(o, S_700038B0 + 8);
    a6_w32(o, self + 0x2D0, A6_MUL(F_6, z));
    if (a6_c_0019A570(o, self + 0xB0, S_700038A0, 4, 0, &hit)) return;
    if (a6_c_00102948(o, s + 0x10, hit != 0 ? S_700031B0 : S_700038A0)) return;
    if (a6_c_001028D0(o, S_700038A0, s, s + 0x10)) return;
    a6_w32(o, S_700038A0 + 0xC, 0);
    if (a6_c_00102738(o, S_700038A0, S_700038A0, &r)) return;
    float len = 0.0f;
    if (a6_c_0011E748(o, r, &len)) return;
    uint32_t lb = a6_bits(len);
    a6_w32(o, S_70003A20, lb);
    for (uint32_t k = 0; k < 16; k += 4) {
        uint32_t v = a6_u32(o, S_700036C0 + k);
        a6_w32(o, S_700036C0 + k, A6_MUL(v, lb));
    }
    if (a6_c_001A2370(o, self, S_700036A0)) return;
    if (a6_c_001028D0(o, S_700038A0, s + 0x10, s)) return;
    x = a6_u32(o, S_700038A0);
    uint32_t y = a6_u32(o, S_700038A0 + 4);
    z = a6_u32(o, S_700038A0 + 8);
    if (a6_c_0011E748(o, fl(a6_sumsq3(x, y, z)), &r)) return;
    a6_w32(o, s + 0x30, a6_bits(r));
    a6_w32(o, self + 0x2C8, a6_bits(r));
    a6_w32(o, s + 0x3C, 0);
    a6_w32(o, s + 0x38, F_ONE);
}

int em_area06_port_00219F50(const EmArea06PortHooks *h, uint32_t self, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_00219F50(&o, self);
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 0021A180 (C byte-identical): the strip drawing of the node, by the word
 * self +0x22C: 0 one strip, 1 two strips and the fade, 2 returns 0, other
 * values return 1 without drawing. A strip: dir = normalize(end - sub)
 * (0x700038A0 / 0x700038B0), the matrix 0x700036A0 / 0x700036E0 from it
 * (001CD390) with the translation row set (00102918), h = 001CCF70(row
 * block 0x700036D0 / 0x70003710), D_00266920 = 0x40, D_002668A8 = sub
 * +0x30 / +0x34, 001CFA60(frame local, matrix, sub +0x38, 0.0) and
 * 001CFBE0(h, 4, D_002668A0, frame local, 1). The fade: sub +0x38 -= 1.0 /
 * (float)self +0x2C4 (an int), then sub +0x3C = 2 when it is below 0.
 * Frame local at sp - 0x60 (frame 0xA0). Returns 1 (0 for state 2).
 * ---------------------------------------------------------------------- */
static int a6_strip(A6 *o, uint32_t s, uint32_t matrix, uint32_t rows, uint32_t len_at, uint32_t local)
{
    int32_t handle = 0;
    if (a6_c_001CCF70(o, rows, &handle)) return -1;
    a6_w32(o, 0x00266920u, 0x40);
    a6_w32(o, 0x002668A8u, a6_u32(o, s + len_at));
    uint32_t f12 = a6_u32(o, s + 0x38);
    if (a6_c_001CFA60(o, local, matrix, fl(f12), fl(F_ZERO))) return -1;
    return a6_c_001CFBE0(o, handle, 4, 0x002668A0u, local, 1);
}

static int a6_strip_matrix(A6 *o, uint32_t vec, uint32_t end, uint32_t from, uint32_t matrix)
{
    if (a6_c_001028D0(o, vec, end, from)) return -1;
    if (a6_c_00102760(o, vec, vec)) return -1;
    if (a6_c_001CD390(o, matrix, vec)) return -1;
    return a6_c_00102918(o, matrix, matrix, from);
}

int32_t a6_0021A180(A6 *o, uint32_t self, uint32_t sp)
{
    uint32_t state = a6_u32(o, self + 0x22C);
    uint32_t s = self + 0x1F0, local = sp - 0xA0 + 0x40;
    if (state == 2) return 0;
    if (state == 1) {
        if (a6_strip_matrix(o, S_700038A0, s + 0x20, s, S_700036A0)) return 1;
        if (a6_strip_matrix(o, S_700038B0, s + 0x20, s + 0x10, S_700036E0)) return 1;
        if (a6_strip(o, s, S_700036A0, S_700036D0, 0x30, local)) return 1;
        if (a6_strip(o, s, S_700036E0, S_70003710, 0x34, local)) return 1;
        uint32_t n = a6_u32(o, self + 0x2C4);
        uint32_t v = a6_u32(o, s + 0x38);
        v = A6_SUB(v, A6_DIV(F_ONE, A6_CVT(n)));
        int below = A6_LT(v, F_ZERO);
        a6_w32(o, s + 0x38, v);
        if (below) a6_w32(o, s + 0x3C, 2);
        return 1;
    }
    if (state == 0) {
        if (a6_strip_matrix(o, S_700038A0, s + 0x10, s, S_700036A0)) return 1;
        a6_strip(o, s, S_700036A0, S_700036D0, 0x30, local);
    }
    return 1;
}

int em_area06_port_0021A180(const EmArea06PortHooks *h, uint32_t self, uint32_t sp, int32_t *result,
                            EmArea06PortFault *fault)
{
    A6 o;
    if (!result || a6_begin(&o, h, fault)) return -1;
    int32_t r = a6_0021A180(&o, self, sp);
    if (a6_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 00219870 (NEARMISS; the translation follows the instructions): the node
 * behaviour of AREA06's deferred g[11] (model 0x30). sub = self + 0x1F0.
 * By the state byte +4:
 *  0  001B0FD0(self) == 0: halfword +0x34 = 1, +0 = 1, +0x2D8 = 0,
 *     001C6380, 00219F50, +0x2C4 = 20, 0x700038A0 = (1, 0, 0, 0.25),
 *     +0x2CC = 001C5570(self, 0x700038A0, 0x7C, 1); with 001B11E0((+0x9A
 *     + 1) & 0xFF) nonzero: halfword +0x28 = 0, +5 = 2, 0021A440(self,
 *     sub).
 *  1  halfword +0x2A + 1; by +5: 0 with *D_008102C8 != 1 and
 *     0019AA80(sub, sub +0x10, 0x40) nonzero: sound 0x429, +0 = 2, +0x28 =
 *     0, +5 + 1, 0021A440(self, sub), +0x2E8 = 0, +0x2D8 = 1, the drift
 *     (0, 0, 0.2) through the matrix **D_00275B40 +0x90 into +0x2E4 /
 *     +0x2E0; then with halfword +0x36 set: 001B1190((+0x9A + 1) & 0xFF),
 *     sound 0x42A, +0x28 = 0, +5 = 2, sub's end point moved onto the line
 *     toward the player (D_00810360) and 0021A440(self, 0x700038A0); then
 *     the pulse: the halfword 0x70003B88 = +0x2A & 0x7F folded at 0x40,
 *     2 v / 128 at 0x70003A20 and (+0x2CC)->+0xA0, +0xA4 = +0xA8 = 0,
 *     +0xAC = 0.25. 1 +0x28 + 1; at +0x28 >= (+0x2C4 + 10) >> 2 sound
 *     0x42B and, with +0 == 2, +0x2A = +0x28 = 0, +0x2C4 >>= 2, +4 = 2;
 *     (+0x2CC)->+0xA0 = 1.0 or 0 by +0x2A bit 3, +0xA4 = +0xA8 = 0,
 *     +0xAC = 0.25. Then with +0x2D8 set: +0x2E8 -= 0.015 and the drift
 *     added to (*(D_00275B40 +4)) +0xC0 / +0xC4 / +0xC8; +1 =
 *     001B1630(+0xB0, +0xB4, +0xB8); when +1 is set: 001B1B70 when +5 < 2
 *     and *D_008102C8 == 1, then the +0x4C method; then 0021A180.
 *  2  +0x28 + 1; from 4 (sound 0x42C at 4) a spark 0x8000006D at +0xB0 +
 *     (+0x2D4, 0, +0x2D0) * +0x2A and +0x2A + 1; then when +0x2C8 < 6
 *     +0x2A or +0x2A == 8: +4 = 3 and, with +0x2CC set, 001B1190(+0x9A)
 *     and (+0x2CC)->+4 = 3.
 *  3  001AFC10(self).
 * The frame is 0x30; 0021A180 runs with sp - 0x30.
 * ---------------------------------------------------------------------- */
static void a6_pulse_words(A6 *o, uint32_t self, uint32_t a0_bits)
{
    uint32_t p = a6_u32(o, self + 0x2CC);
    a6_w32(o, p + 0xA0, a0_bits);
    p = a6_u32(o, self + 0x2CC);
    a6_w32(o, p + 0xA4, 0);
    p = a6_u32(o, self + 0x2CC);
    a6_w32(o, p + 0xA8, 0);
    p = a6_u32(o, self + 0x2CC);
    a6_w32(o, p + 0xAC, F_QUARTER);
}

static void a6_219870_state1(A6 *o, uint32_t self, uint32_t sp)
{
    uint32_t s = self + 0x1F0;
    a6_w16(o, self + 0x2A, (uint32_t)a6_s16(o, self + 0x2A) + 1);
    uint32_t sub = a6_u8(o, self + 5);
    if (sub == 1) {
        a6_w16(o, self + 0x28, (uint32_t)a6_s16(o, self + 0x28) + 1);
        int32_t lim = (int32_t)a6_u32(o, self + 0x2C4);
        int32_t c = a6_s16(o, self + 0x28);
        if (!(c < ((lim + 0xA) >> 2))) {
            if (a6_c_001FBD50(o, self, 0x42B, 0, fl(F_300))) return;
            if (a6_u8(o, self) == 2) {
                a6_w16(o, self + 0x2A, 0);
                a6_w16(o, self + 0x28, 0);
                int32_t q = (int32_t)a6_u32(o, s + 0xD4);
                a6_w32(o, s + 0xD4, (uint32_t)(q >> 2));
                a6_w8(o, self + 4, 2);
            }
        }
        a6_pulse_words(o, self, (a6_s16(o, self + 0x2A) & 8) ? F_ONE : F_ZERO);
    } else if (sub == 0) {
        uint32_t pp = a6_u32(o, D_008102C8);
        if (a6_u8(o, pp) != 1) {
            int32_t r = 0;
            if (a6_c_0019AA80(o, s, s + 0x10, 0x40, &r)) return;
            if (r != 0) {
                if (a6_c_001FBD50(o, self, 0x429, 0, fl(F_300))) return;
                a6_w8(o, self, 2);
                a6_w16(o, self + 0x28, 0);
                a6_w8(o, self + 5, a6_u8(o, self + 5) + 1);
                a6_0021A440(o, self, s);
                if (a6_failed(o)) return;
                a6_w32(o, self + 0x2E8, 0);
                a6_w32(o, self + 0x2D8, 1);
                a6_w32(o, S_700038A0 + 0xC, 0);
                a6_w32(o, S_700038A0 + 4, 0);
                a6_w32(o, S_700038A0, 0);
                a6_w32(o, S_700038A0 + 8, F_0_2);
                uint32_t g = a6_u32(o, D_00275B40);
                uint32_t m = a6_u32(o, g);
                if (a6_c_001026A0(o, S_700038A0, m + 0x90, S_700038A0)) return;
                a6_w32(o, self + 0x2E4, a6_u32(o, S_700038A0));
                a6_w32(o, self + 0x2E0, a6_u32(o, S_700038A0 + 8));
            }
        }
        if (a6_s16(o, self + 0x36) != 0) {
            float d = 0.0f, len = 0.0f;
            if (a6_c_001B1190(o, (int32_t)((a6_u8(o, self + 0x9A) + 1) & 0xFF))) return;
            if (a6_c_001FBD50(o, self, 0x42A, 0, fl(F_300))) return;
            a6_w16(o, self + 0x28, 0);
            a6_w8(o, self + 5, 2);
            if (a6_c_001028D0(o, S_700038A0, s + 0x10, s)) return;
            a6_w32(o, S_700038A0 + 0xC, 0);
            if (a6_c_00102760(o, S_700038A0, S_700038A0)) return;
            if (a6_c_001028D0(o, S_700038B0, D_00810360, s)) return;
            a6_w32(o, S_700038B0 + 0xC, 0);
            if (a6_c_00102738(o, S_700038B0, S_700038B0, &d)) return;
            if (a6_c_0011E748(o, d, &len)) return;
            a6_w32(o, S_70003A20, a6_bits(len));
            if (a6_c_00102760(o, S_700038B0, S_700038B0)) return;
            if (a6_c_00102738(o, S_700038A0, S_700038B0, &d)) return;
            uint32_t m = A6_MUL(a6_u32(o, S_70003A20), a6_bits(d));
            a6_w32(o, S_70003A24, m);
            if (a6_c_00102900(o, S_700038A0, S_700038A0, fl(m))) return;
            if (a6_c_001028B8(o, S_700038A0, s, S_700038A0)) return;
            a6_0021A440(o, self, S_700038A0);
            if (a6_failed(o)) return;
        }
        a6_w16(o, S_70003B88, (uint32_t)a6_s16(o, self + 0x2A) & 0x7Fu);
        int32_t t = a6_s16(o, S_70003B88);
        if (!(t < 0x40)) a6_w16(o, S_70003B88, (uint32_t)(0x7F - t));
        int32_t v = a6_s16(o, S_70003B88);
        uint32_t f = A6_DIV(A6_CVT(v << 1), F_128);
        a6_w32(o, S_70003A20, f);
        a6_pulse_words(o, self, f);
    }
    if (a6_failed(o)) return;
    if (a6_u32(o, self + 0x2D8) != 0) {
        a6_w32(o, self + 0x2E8, A6_ADD(a6_u32(o, self + 0x2E8), F_M0_015));
        static const uint32_t drift[3] = { 0x2E4, 0x2E8, 0x2E0 };
        for (uint32_t k = 0; k < 3; k++) {
            uint32_t g = a6_u32(o, D_00275B40);
            uint32_t dv = a6_u32(o, self + drift[k]);
            uint32_t p = a6_u32(o, g + 4);
            uint32_t cur = a6_u32(o, p + 0xC0 + 4 * k);
            a6_w32(o, p + 0xC0 + 4 * k, A6_ADD(cur, dv));
        }
    }
    uint32_t y = a6_u32(o, self + 0xB4);
    uint32_t z = a6_u32(o, self + 0xB8);
    uint32_t x = a6_u32(o, self + 0xB0);
    int32_t vis = 0;
    if (a6_c_001B1630(o, fl(x), fl(y), fl(z), &vis)) return;
    a6_w8(o, self + 1, (uint32_t)vis);
    if (a6_u8(o, self + 1) != 0) {
        if ((int32_t)a6_u8(o, self + 5) < 2) {
            uint32_t pp = a6_u32(o, D_008102C8);
            if (a6_u8(o, pp) == 1 && a6_c_001B1B70(o, self)) return;
        }
        if (a6_callback(o, a6_u32(o, self + 0x4C), self)) return;
    }
    a6_0021A180(o, self, sp - 0x30);
}

static void a6_219870_state2(A6 *o, uint32_t self)
{
    a6_w16(o, self + 0x28, (uint32_t)a6_s16(o, self + 0x28) + 1);
    int32_t c = a6_s16(o, self + 0x28);
    if (c >= 4) {
        if (c == 4 && a6_c_001FBD50(o, self, 0x42C, 0, fl(F_300))) return;
        a6_w32(o, S_700038A0, a6_u32(o, self + 0xB0));
        a6_w32(o, S_700038A0 + 4, a6_u32(o, self + 0xB4));
        a6_w32(o, S_700038A0 + 8, a6_u32(o, self + 0xB8));
        a6_w32(o, S_700038A0 + 0xC, F_ONE);
        int32_t k = a6_s16(o, self + 0x2A);
        uint32_t step = a6_u32(o, self + 0x2D4);
        uint32_t base = a6_u32(o, self + 0xB0);
        a6_w32(o, S_700038A0, A6_ADD(base, A6_MUL(step, A6_CVT(k))));
        a6_w32(o, S_700038A0 + 4, a6_u32(o, self + 0xB4));
        k = a6_s16(o, self + 0x2A);
        step = a6_u32(o, self + 0x2D0);
        base = a6_u32(o, self + 0xB8);
        a6_w32(o, S_700038A0 + 8, A6_ADD(base, A6_MUL(step, A6_CVT(k))));
        a6_w32(o, S_700038A0 + 0xC, F_ONE);
        if (a6_c_001EFD20(o, (int32_t)0x8000006D, S_700038A0)) return;
        a6_w16(o, self + 0x2A, (uint32_t)a6_s16(o, self + 0x2A) + 1);
    }
    int32_t k = a6_s16(o, self + 0x2A);
    uint32_t lim = a6_u32(o, self + 0x2C8);
    if (A6_LT(lim, A6_MUL(F_6, A6_CVT(k))) || k == 8) {
        a6_w8(o, self + 4, 3);
        if (a6_u32(o, self + 0x2CC) != 0) {
            if (a6_c_001B1190(o, (int32_t)a6_u8(o, self + 0x9A))) return;
            a6_w8(o, a6_u32(o, self + 0x2CC) + 4, 3);
        }
    }
}

static void a6_00219870(A6 *o, uint32_t self, uint32_t sp)
{
    uint32_t state = a6_u8(o, self + 4);
    if (state == 3) {
        a6_c_001AFC10(o, self);
    } else if (state == 2) {
        a6_219870_state2(o, self);
    } else if (state == 1) {
        a6_219870_state1(o, self, sp);
    } else if (state == 0) {
        int32_t r = 0, ok = 0;
        uint32_t p = 0;
        if (a6_c_001B0FD0(o, self, &r) || r != 0) return;
        a6_w16(o, self + 0x34, 1);
        a6_w8(o, self, 1);
        a6_w32(o, self + 0x2D8, 0);
        if (a6_c_001C6380(o, self)) return;
        a6_00219F50(o, self);
        if (a6_failed(o)) return;
        a6_w32(o, self + 0x2C4, 0x14);
        a6_w32(o, S_700038A0, F_ONE);
        a6_w32(o, S_700038A0 + 4, 0);
        a6_w32(o, S_700038A0 + 8, 0);
        a6_w32(o, S_700038A0 + 0xC, F_QUARTER);
        if (a6_c_001C5570(o, self, S_700038A0, 0x7C, 1, &p)) return;
        a6_w32(o, self + 0x2CC, p);
        if (a6_c_001B11E0(o, (int32_t)((a6_u8(o, self + 0x9A) + 1) & 0xFF), &ok)) return;
        if (ok != 0) {
            a6_w16(o, self + 0x28, 0);
            a6_w8(o, self + 5, 2);
            a6_0021A440(o, self, self + 0x1F0);
        }
    }
}

int em_area06_port_00219870(const EmArea06PortHooks *h, uint32_t self, uint32_t sp, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_00219870(&o, self, sp);
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 0021AE90 (NEARMISS; the translation follows the instructions): the
 * debris spawner. owner = self +0x24, sub = self + 0x1F0. By +4: 2 / 3
 * 001AFC10(self). 0 fills sixteen (a, b) word pairs at sub from
 * D_00266AE0 / D_00266B00 at ((rand >> 16) % 6) each, sub +0x84 = +0x80
 * = 0, +4 = 1, and falls into 1. 1: owner +4 == 3 sets +4 = 3 and
 * returns; otherwise by the counter sub +0x80: 16 sets +4 = 3; 0, 4, 8, 12
 * spawn two strips (pair sub +0x84, then +0x84 + 1): the points
 * (owner +0x110)[a] +0xC0 and [b] +0xC0 (0x700038A0 / 0x700038B0), length
 * 1.1 |b - a| at 0x70003A20, the direction matrix at a, class 0x8000003B
 * (001EFEB0) given +5 = 1, +0x1F0 = 6, +0x1F4 = the length, +0x1F8 = 0.2;
 * then the counter + 1.
 * ---------------------------------------------------------------------- */
static void a6_0021AE90(A6 *o, uint32_t self)
{
    uint32_t owner = a6_u32(o, self + 0x24);
    uint32_t state = a6_u8(o, self + 4);
    uint32_t s = self + 0x1F0;
    if (state == 3 || state == 2) {
        a6_c_001AFC10(o, self);
        return;
    }
    if (state != 1) {
        if (state != 0) return;
        uint32_t e = s;
        for (int i = 0; i < 0x10; i++, e += 8) {
            int32_t r = 0;
            if (a6_c_00122BB8(o, &r)) return;
            uint32_t a = a6_u32(o, D_00266AE0 + (uint32_t)((r >> 16) % 6) * 4u);
            a6_w32(o, e, a);
            if (a6_c_00122BB8(o, &r)) return;
            uint32_t b = a6_u32(o, D_00266B00 + (uint32_t)((r >> 16) % 6) * 4u);
            a6_w32(o, e + 4, b);
        }
        a6_w32(o, s + 0x84, 0);
        a6_w32(o, s + 0x80, 0);
        a6_w8(o, self + 4, 1);
    }
    if (a6_u8(o, owner + 4) == 3) {
        a6_w8(o, self + 4, 3);
        return;
    }
    uint32_t k = a6_u32(o, s + 0x80);
    if (k == 0x10) {
        a6_w8(o, self + 4, 3);
    } else if (k == 0xC || k == 8 || k == 4 || k == 0) {
        for (int i = 0; i < 2; i++) {
            uint32_t c = a6_u32(o, s + 0x84);
            uint32_t e = s + c * 8u;
            uint32_t b = a6_u32(o, e + 4);
            uint32_t a = a6_u32(o, e);
            uint32_t pa = a6_u32(o, owner + a * 4u + 0x110);
            if (a6_c_00102948(o, S_700038A0, pa + 0xC0)) return;
            uint32_t pb = a6_u32(o, owner + b * 4u + 0x110);
            if (a6_c_00102948(o, S_700038B0, pb + 0xC0)) return;
            if (a6_c_001028D0(o, S_700038B0, S_700038B0, S_700038A0)) return;
            uint32_t x = a6_u32(o, S_700038B0), y = a6_u32(o, S_700038B0 + 4), z = a6_u32(o, S_700038B0 + 8);
            float len = 0.0f;
            if (a6_c_0011E748(o, fl(a6_sumsq3(x, y, z)), &len)) return;
            a6_w32(o, S_70003A20, a6_bits(len));
            a6_w32(o, S_70003A20, A6_MUL(a6_u32(o, S_70003A20), F_1_1));
            if (a6_c_00102760(o, S_700038B0, S_700038B0)) return;
            if (a6_c_001CD390(o, S_700036A0, S_700038B0)) return;
            if (a6_c_00102918(o, S_700036A0, S_700036A0, S_700038A0)) return;
            uint32_t p = 0;
            if (a6_c_001EFEB0(o, (int32_t)0x8000003B, S_700036A0, &p)) return;
            if (p != 0) {
                a6_w8(o, p + 5, 1);
                a6_w32(o, p + 0x1F0, 6);
                a6_w32(o, p + 0x1F4, a6_u32(o, S_70003A20));
                a6_w32(o, p + 0x1F8, F_0_2);
            }
            a6_w32(o, s + 0x84, a6_u32(o, s + 0x84) + 1);
        }
    }
    a6_w32(o, s + 0x80, a6_u32(o, s + 0x80) + 1);
}

int em_area06_port_0021AE90(const EmArea06PortHooks *h, uint32_t self, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_0021AE90(&o, self);
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 00169250 (NEARMISS; the translation follows the instructions, which
 * differed from the decomp's C in the odd-count branch until its 2026-09-28
 * correction: the half step is 0.5 (4.5 +0x2E0), not 0.5 +0x2E0): player
 * +5 state, by the sub-state
 * +6:
 *  0  +6 + 1, +7 = 0, 00194240(self) when D_00810700 == 0x13 and
 *     D_00810701 == 0; then as 1.
 *  1  +0xC4 = 001B12B0(+0x218, +0xC4, pi/8); when it equals +0x218 (read
 *     again): +6 + 1, 001749A0(self, 0x70, 0, 1.0), halfword +0x28 = 8,
 *     +0x2E4 = (+0x254 - +0xB4) / 8, the two bar ends through 0019F680
 *     (self +0x30C, 0 / 1) into 0x700038A0 / 0x700038B0, n =
 *     001281C0((|end0 - end1|xz + 0.1) / 4.5), d = |(+0x290, +0x298) -
 *     (+0xB0, +0xB8)|, t = 001281C0(d / 4.5) (n even) or 001281C0((d -
 *     2.25) / 4.5) (n odd): +0x2F4 = +0x290 - 4.5 +0x2E0 t (odd: - 0.5
 *     (4.5 +0x2E0)), +0x2F8 likewise from +0x298 / +0x2E8; +0x2E0 =
 *     (+0x2F4 - +0xB0) / 8, +0x2E8 = (+0x2F8 - +0xB8) / 8.
 *  2  with +0x200 & 0x1000: +6 = 3, 001749A0(self, 0xBA, 0, 1.0), +0x25F
 *     = 3.
 *  3  halfword +0x28 == 0: +6 = 4, +0xB4 = +0x254, +0xB0 = +0x2F4, +0xB8
 *     = +0x2F8, sound 0x123; else +0xB4 += +0x2E4, +0xB0 += +0x2E0, +0xB8
 *     += +0x2E8 and +0x28 - 1.
 *  4  with +0x200 & 0x1000: +5 = 0x10, +6 = 0, +0x1F0 = 0x21, +0x1F1 =
 *     +0x2F1 = 0, 001749A0(self, 001885B0(self), 0, 1.0).
 * ---------------------------------------------------------------------- */
static void a6_00169250(A6 *o, uint32_t self)
{
    uint32_t st = a6_u8(o, self + 6);
    if (st == 4) {
        if (!(a6_u32(o, self + 0x200) & 0x1000)) return;
        int32_t clip = 0;
        a6_w8(o, self + 5, 0x10);
        a6_w8(o, self + 6, 0);
        a6_w8(o, self + 0x1F0, 0x21);
        a6_w8(o, self + 0x1F1, 0);
        a6_w8(o, self + 0x2F1, 0);
        a6_001885B0(o, self, &clip);
        if (a6_failed(o)) return;
        a6_c_001749A0(o, self, clip, 0, fl(F_ONE));
        return;
    }
    if (st == 3) {
        if (a6_s16(o, self + 0x28) == 0) {
            a6_w8(o, self + 6, st + 1);
            a6_w32(o, self + 0xB4, a6_u32(o, self + 0x254));
            a6_w32(o, self + 0xB0, a6_u32(o, self + 0x2F4));
            a6_w32(o, self + 0xB8, a6_u32(o, self + 0x2F8));
            a6_c_001FBD50(o, self, 0x123, 0, fl(F_300));
            return;
        }
        static const uint32_t pos[3] = { 0xB4, 0xB0, 0xB8 }, vel[3] = { 0x2E4, 0x2E0, 0x2E8 };
        for (unsigned k = 0; k < 3; k++) {
            uint32_t v = a6_u32(o, self + vel[k]);
            uint32_t p = a6_u32(o, self + pos[k]);
            a6_w32(o, self + pos[k], A6_ADD(p, v));
        }
        a6_w16(o, self + 0x28, (uint32_t)a6_s16(o, self + 0x28) - 1);
        return;
    }
    if (st == 2) {
        if (!(a6_u32(o, self + 0x200) & 0x1000)) return;
        a6_w8(o, self + 6, st + 1);
        if (a6_c_001749A0(o, self, 0xBA, 0, fl(F_ONE))) return;
        a6_w8(o, self + 0x25F, 3);
        return;
    }
    if (st != 1) {
        if (st != 0) return;
        a6_w8(o, self + 6, st + 1);
        a6_w8(o, self + 7, 0);
        if (a6_u8(o, D_00810700) == 0x13 && a6_u8(o, D_00810701) == 0) {
            if (a6_c_00194240(o, self)) return;
        }
    }
    /* state 1 */
    uint32_t cur = a6_u32(o, self + 0xC4);
    uint32_t goal = a6_u32(o, self + 0x218);
    float r = 0.0f;
    if (a6_c_001B12B0(o, fl(goal), fl(cur), fl(F_PI_8), &r)) return;
    uint32_t rb = a6_bits(r);
    a6_w32(o, self + 0xC4, rb);
    if (!A6_EQ(rb, a6_u32(o, self + 0x218))) return;
    a6_w8(o, self + 6, a6_u8(o, self + 6) + 1);
    if (a6_c_001749A0(o, self, 0x70, 0, fl(F_ONE))) return;
    a6_w16(o, self + 0x28, 8);
    uint32_t top = a6_u32(o, self + 0x254);
    uint32_t y = a6_u32(o, self + 0xB4);
    a6_w32(o, self + 0x2E4, A6_DIV(A6_SUB(top, y), F_8));
    int32_t ignored = 0;
    a6_0019F680(o, S_700038A0, a6_u32(o, self + 0x30C), 0, &ignored);
    a6_0019F680(o, S_700038B0, a6_u32(o, self + 0x30C), 1, &ignored);
    if (a6_failed(o)) return;
    uint32_t ax = a6_u32(o, S_700038A0);
    uint32_t bx = a6_u32(o, S_700038B0);
    uint32_t az = a6_u32(o, S_700038A0 + 8);
    uint32_t dx = A6_SUB(ax, bx);
    uint32_t bz = a6_u32(o, S_700038B0 + 8);
    uint32_t dz = A6_SUB(az, bz);
    a6_w32(o, S_70003A20, dx);
    uint32_t dx2 = a6_u32(o, S_70003A20);
    a6_w32(o, S_70003A28, dz);
    float len = 0.0f;
    if (a6_c_0011E748(o, fl(A6_MADD(A6_MUL(dx2, dx2), dz, dz)), &len)) return;
    a6_w32(o, S_70003A24, A6_ADD(F_0_1, a6_bits(len)));
    int32_t n = 0;
    if (a6_c_001281C0(o, fl(A6_DIV(a6_u32(o, S_70003A24), F_4_5)), &n)) return;
    uint32_t tx = a6_u32(o, self + 0x290);
    uint32_t px = a6_u32(o, self + 0xB0);
    a6_w32(o, S_70003A20, A6_SUB(tx, px));
    uint32_t tz = a6_u32(o, self + 0x298);
    uint32_t pz = a6_u32(o, self + 0xB8);
    uint32_t ex = a6_u32(o, S_70003A20);
    uint32_t ez = A6_SUB(tz, pz);
    a6_w32(o, S_70003A28, ez);
    float d = 0.0f;
    if (a6_c_0011E748(o, fl(A6_MADD(A6_MUL(ex, ex), ez, ez)), &d)) return;
    int32_t t = 0;
    if (!(n & 1)) {
        if (a6_c_001281C0(o, fl(A6_DIV(a6_bits(d), F_4_5)), &t)) return;
        uint32_t ct = A6_CVT(t);
        uint32_t vx = a6_u32(o, self + 0x2E0);
        uint32_t gx = a6_u32(o, self + 0x290);
        a6_w32(o, self + 0x2F4, A6_SUB(gx, A6_MUL(A6_MUL(F_4_5, vx), ct)));
        uint32_t vz = a6_u32(o, self + 0x2E8);
        uint32_t gz = a6_u32(o, self + 0x298);
        a6_w32(o, self + 0x2F8, A6_SUB(gz, A6_MUL(A6_MUL(F_4_5, vz), ct)));
    } else {
        if (a6_c_001281C0(o, fl(A6_DIV(A6_SUB(a6_bits(d), F_2_25), F_4_5)), &t)) return;
        uint32_t ct = A6_CVT(t);
        uint32_t vx = a6_u32(o, self + 0x2E0);
        uint32_t gx = a6_u32(o, self + 0x290);
        uint32_t sx = A6_MUL(F_4_5, vx);
        a6_w32(o, self + 0x2F4, A6_MSUB(A6_SUB(gx, A6_MUL(sx, ct)), F_HALF, sx));
        uint32_t vz = a6_u32(o, self + 0x2E8);
        uint32_t gz = a6_u32(o, self + 0x298);
        uint32_t sz = A6_MUL(F_4_5, vz);
        a6_w32(o, self + 0x2F8, A6_MSUB(A6_SUB(gz, A6_MUL(sz, ct)), F_HALF, sz));
    }
    uint32_t fx = a6_u32(o, self + 0x2F4);
    uint32_t cx = a6_u32(o, self + 0xB0);
    a6_w32(o, self + 0x2E0, A6_DIV(A6_SUB(fx, cx), F_8));
    uint32_t fz = a6_u32(o, self + 0x2F8);
    uint32_t cz = a6_u32(o, self + 0xB8);
    a6_w32(o, self + 0x2E8, A6_DIV(A6_SUB(fz, cz), F_8));
}

int em_area06_port_00169250(const EmArea06PortHooks *h, uint32_t self, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_00169250(&o, self);
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 001944B0 (C byte-identical): camera states 29..37 (the entity word
 * ent +0x230), with the preset row D_0024A530 + 20 idx (x, y, z, base yaw,
 * reference facing). Returns 1 for 29..37, else 0. See docs/AREA06_PORT.md
 * section 2 for each state.
 * ---------------------------------------------------------------------- */
static uint32_t a6_row(int32_t idx, uint32_t field) { return D_0024A530 + field + (uint32_t)idx * 20u; }

/* The approach step of states 32 / 33 (and 36 / 37 in areas 8 / 0x13 sub
 * 1): t = D_00810690 - |cam +0xC| at 0x70003A20; t > 0 moves the camera
 * along the normalized xz of cam +0x20 - cam +0x10 by t; below the limit
 * (-30 when cam +0x64 is -46.8, else -20) by t - limit (0x70003A24). Then
 * 0018D7B0(cam, 4), cam +0x14 = the row's y and the two eye chases. */
static void a6_approach(A6 *o, uint32_t cam, int32_t idx)
{
    float fa = 0.0f;
    if (a6_c_0011DF78(o, fl(a6_u32(o, cam + 0xC)), &fa)) return;
    uint32_t t = A6_SUB(a6_u32(o, D_00810690), a6_bits(fa));
    int le = A6_LE(t, F_ZERO);
    a6_w32(o, S_70003A20, t);
    uint32_t by = S_70003A20;
    if (le) {
        uint32_t lim = A6_EQ(F_M46_8, a6_u32(o, cam + 0x64)) ? F_M30 : F_M20;
        by = 0;
        if (A6_LT(t, lim)) {
            a6_w32(o, S_70003A24, A6_SUB(t, lim));
            by = S_70003A24;
        }
    }
    if (by) {
        if (a6_c_001028D0(o, S_700038A0, cam + 0x20, cam + 0x10)) return;
        a6_w32(o, S_700038A0 + 4, 0);
        a6_w32(o, S_700038A0 + 0xC, 0);
        if (a6_c_00102760(o, S_700038A0, S_700038A0)) return;
        uint32_t dx = a6_u32(o, S_700038A0);
        uint32_t px = a6_u32(o, cam + 0x10);
        uint32_t k = a6_u32(o, by);
        a6_w32(o, cam + 0x10, A6_ADD(px, A6_MUL(dx, k)));
        uint32_t dz = a6_u32(o, S_700038A0 + 8);
        uint32_t pz = a6_u32(o, cam + 0x18);
        k = a6_u32(o, by);
        a6_w32(o, cam + 0x18, A6_ADD(pz, A6_MUL(dz, k)));
    }
    if (a6_c_0018D7B0(o, cam, 4)) return;
    uint32_t y = a6_u32(o, a6_row(idx, 4));
    a6_w32(o, cam + 0x14, y);
    if (a6_c_0018C4B0(o, D_008105D0, fl(y), fl(F_0_8))) return;
    a6_c_0018C6A0(o, cam + 0x10, D_008105D0, fl(F_0_8));
}

/* cam +0x10 / +0x18 on the orbit of D_008105E0 / D_008105E8 at the yaw
 * cam +0x44 and the distance cam +0xC + cam +0x94; the sine of the yaw is
 * `sine` (the caller's), the cosine is taken of cam +0x44 read again. */
static void a6_orbit(A6 *o, uint32_t cam, uint32_t sine)
{
    uint32_t c = a6_u32(o, cam + 0xC);
    uint32_t d = a6_u32(o, cam + 0x94);
    uint32_t e = a6_u32(o, D_008105E0);
    a6_w32(o, cam + 0x10, A6_ADD(e, A6_MUL(A6_ADD(c, d), sine)));
    float co = 0.0f;
    if (a6_c_0011DE90(o, fl(a6_u32(o, cam + 0x44)), &co)) return;
    c = a6_u32(o, cam + 0xC);
    d = a6_u32(o, cam + 0x94);
    e = a6_u32(o, D_008105E8);
    a6_w32(o, cam + 0x18, A6_ADD(e, A6_MUL(A6_ADD(c, d), a6_bits(co))));
}

static int32_t a6_001944B0(A6 *o, uint32_t cam, uint32_t ent, int32_t idx)
{
    int32_t state = (int32_t)a6_u32(o, ent + 0x230);
    if ((uint32_t)(state - 29) >= 9u) return 0;
    if (state == 29) {
        float yaw = 0.0f;
        if (!(idx < 7)) {
            a6_w32(o, cam + 0x14, a6_u32(o, a6_row(idx, 4)));
            uint32_t x = a6_u32(o, ent + 0xA0);
            uint32_t z = a6_u32(o, ent + 0xA8);
            if (a6_c_001B1240(o, cam + 0x10, fl(x), fl(z), &yaw)) return 1;
            a6_w32(o, cam + 0x44, a6_bits(yaw));
            if (a6_c_0018C6A0(o, cam + 0x10, D_008105D0, fl(F_0_8))) return 1;
            if (a6_c_0018C4B0(o, D_008105D0, fl(a6_u32(o, cam + 0x14)), fl(F_0_8))) return 1;
            a6_c_0018D7B0(o, cam, 5);
        } else {
            a6_w32(o, cam + 0x10, a6_u32(o, a6_row(idx, 0)));
            a6_w32(o, cam + 0x14, a6_u32(o, a6_row(idx, 4)));
            a6_w32(o, cam + 0x18, a6_u32(o, a6_row(idx, 8)));
            uint32_t x = a6_u32(o, ent + 0xA0);
            uint32_t z = a6_u32(o, ent + 0xA8);
            if (a6_c_001B1240(o, cam + 0x10, fl(x), fl(z), &yaw)) return 1;
            a6_w32(o, cam + 0x44, a6_bits(yaw));
            if (a6_c_0018D7B0(o, cam, 5)) return 1;
            a6_c_00102948(o, D_008105D0, cam + 0x10);
        }
        return 1;
    }
    if (state == 32 || state == 33) {
        a6_approach(o, cam, idx);
        return 1;
    }
    if (state == 36 || state == 37) {
        uint32_t mode = a6_u8(o, D_00810700);
        if (mode == 8 || (mode == 0x13 && a6_u8(o, D_00810701) == 1)) {
            a6_approach(o, cam, idx);
            return 1;
        }
    }
    /* 30, 31, 34, 35, and 36 / 37 outside those areas */
    if (state == 30) {
        uint32_t facing = a6_u32(o, ent + 0xC4);
        uint32_t ref = a6_u32(o, a6_row(idx, 0x10));
        float w = 0.0f, a = 0.0f, r = 0.0f;
        if (a6_c_001B1470(o, fl(A6_SUB(facing, ref)), &w)) return 1;
        if (a6_c_0011DF78(o, w, &a)) return 1;
        uint32_t base = a6_u32(o, a6_row(idx, 0xC));
        uint32_t goal = A6_LE(a6_bits(a), F_PI_2) ? A6_SUB(base, F_PI_6) : A6_ADD(F_PI_6, base);
        if (a6_c_001B1470(o, fl(goal), &w)) return 1;
        uint32_t cur = a6_u32(o, cam + 0x44);
        if (a6_c_001B12B0(o, w, fl(cur), fl(F_PI_600), &r)) return 1;
        a6_w32(o, cam + 0x44, a6_bits(r));
        float si = 0.0f;
        if (a6_c_0011E2A8(o, fl(a6_u32(o, cam + 0x44)), &si)) return 1;
        a6_orbit(o, cam, a6_bits(si));
    } else {
        uint32_t base = a6_u32(o, a6_row(idx, 0xC));
        uint32_t cur = a6_u32(o, cam + 0x44);
        float a = 0.0f;
        if (a6_c_00191120(o, state, fl(base), fl(cur), fl(F_PI_90), fl(F_PI_6), &a)) return 1;
        if (!A6_EQ(a6_bits(a), a6_u32(o, cam + 0x44))) {
            a6_w32(o, cam + 0x44, a6_bits(a));
            float si = 0.0f;
            if (a6_c_0011E2A8(o, a, &si)) return 1;
            a6_orbit(o, cam, a6_bits(si));
        }
    }
    if (a6_failed(o)) return 1;
    if (idx == 0) {
        if (A6_LT(a6_u32(o, cam + 0x10), F_M365)) a6_w32(o, cam + 0x10, F_M365);
    } else if (idx == 3) {
        if (A6_LT(a6_u32(o, cam + 0x10), F_749_5)) a6_w32(o, cam + 0x10, F_749_5);
    } else if (idx == 6) {
        if (A6_LT(a6_u32(o, cam + 0x18), F_1010)) a6_w32(o, cam + 0x18, F_1010);
    }
    uint32_t y = a6_u32(o, a6_row(idx, 4));
    a6_w32(o, cam + 0x14, y);
    if (a6_c_0018C4B0(o, D_008105D0, fl(y), fl(F_0_8))) return 1;
    if (a6_c_0018C6A0(o, cam + 0x10, D_008105D0, fl(F_0_8))) return 1;
    a6_c_0018D7B0(o, cam, 5);
    return 1;
}

int em_area06_port_001944B0(const EmArea06PortHooks *h, uint32_t cam, uint32_t ent, int32_t idx, int32_t *result,
                            EmArea06PortFault *fault)
{
    A6 o;
    if (!result || a6_begin(&o, h, fault)) return -1;
    int32_t r = a6_001944B0(&o, cam, ent, idx);
    if (a6_failed(&o)) return -1;
    *result = r;
    return 0;
}
