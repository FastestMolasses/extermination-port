/* AREA06 and AREA01 overlay functions of level 7 (see em_area06_port.h,
 * docs/AREA06_PORT.md). Runtime addresses; the decomp's C for each is
 * byte-identical (lane A06C for AREA06, overlay_AREA06_func_00823540.c,
 * func_overlay_AREA06_00824280.c / _00824520.c; AREA01's
 * func_overlay_AREA01_00826910.c). The conventions are those of
 * em_area06_port.c: calls, arguments and the memory accesses between calls
 * follow the original instructions, floats are the EE model.
 */
#include "em_area06_port_internal.h"

/* Overlay data (runtime addresses). */
#define D_OVL06_00826A70 0x00826A70u /* AREA06: the point [9] spawns around */
#define D_OVL06_00827180 0x00827180u /* AREA06: the beam's script */
#define D_OVL06_00827640 0x00827640u /* AREA06: polygon 1 (64 bytes) */
#define D_OVL06_00827680 0x00827680u /* AREA06: polygon 2 (64 bytes) */

#define D_0024D7C0 0x0024D7C0u /* placement tables [area][sub] */
#define D_008102B0 0x008102B0u /* the player block */
#define D_008102B5 0x008102B5u
#define D_00810350 0x00810350u /* player position (vec4) */
#define D_00810354 0x00810354u
#define D_00810358 0x00810358u
#define D_00810374 0x00810374u /* player yaw */
#define D_008106B9 0x008106B9u
#define D_00810768 0x00810768u
#define D_00810845 0x00810845u
#define D_008106C5 0x008106C5u
#define S_70003600 0x70003600u
#define S_70003680 0x70003680u
#define S_70003B68 0x70003B68u

#define F_PI      0x40490FDBu
#define F_M_PI    0xC0490FDBu
#define F_2_31    0x4F000000u /* 2^31 */
#define F_200     0x43480000u
#define F_20      0x41A00000u
#define F_40      0x42200000u
#define F_55      0x425C0000u
#define F_0_4     0x3ECCCCCDu
#define F_0_06981 0x3D8EFA35u /* 0.0698131695 (4 degrees) */

/* ------------------------------------------------------------------------
 * 0x823580 (AREA06 placement [9]). w = self +0x1F0 (three words: the phase
 * countdown, the strip period, the spark period). By +4:
 *  0  w0 = 240, w1 = rand % 2 + 2, w2 = rand % 5 + 15, +4 = 1, +5 = 0; as 1.
 *  1  by +5 (read again): 0 --w2 < 0: w2 = rand % 50 + 50, the matrix
 *     0x700036A0 = identity rotated by pi rand / 2^31 (001029C0, 00102BB0)
 *     placed at 0x826A70 (00102918), a class 0x80000042 spawn there
 *     (001EFEB0) and 001F02C0(0x826A70, 0x41D, 200.0); --w0 < 0: w0 = 90,
 *     +5 = 1, 0019C6F0(0xD, 1). 1 --w2 < 0: w2 = rand % 5 + 15 and the same
 *     spawn without the sound; --w1 < 0: w1 = rand % 2 + 2, the point
 *     (-225 - 30 r, 20, -595 - 170 r', 1) at 0x700038A0, its offset from
 *     0x826A70 (0x700038B0), the length at 0x70003A20, the direction matrix
 *     placed at the point, a class 0x8000003B spawn given +5 = 1, +0x1F0 =
 *     12, +0x1F4 = the length, +0x1F8 = 0.4, and when D_70003B68 % 100 == 0
 *     the sound 0x41D / 0x41E / 0x41F for rand % 4 = 0 / 1 / 2; then with
 *     rand even and w0 % 3 == 0, 001EA210(2.0); --w0 < 0: w0 = 240, +5 = 0,
 *     0019C6F0(0xD, 0).
 *  2, 3  001AFC10(self).
 * ---------------------------------------------------------------------- */
static int a6_rand(A6 *o, int32_t *r) { return a6_c_00122BB8(o, r); }

/* --w[k] (the word is stored, then read again); nonzero when below 0. */
static int a6_count_down(A6 *o, uint32_t at)
{
    a6_w32(o, at, a6_u32(o, at) - 1u);
    return (int32_t)a6_u32(o, at) < 0;
}

/* The class 0x80000042 spawn of 0x823580 (the rotated matrix at 0x826A70). */
static int a6_823580_spawn(A6 *o)
{
    int32_t r = 0;
    if (a6_c_001029C0(o, S_700036A0)) return -1;
    if (a6_rand(o, &r)) return -1;
    uint32_t turn = A6_MUL(F_PI, A6_DIV(A6_CVT(r), F_2_31));
    if (a6_c_00102BB0(o, S_700036A0, S_700036A0, fl(turn))) return -1;
    if (a6_c_00102918(o, S_700036A0, S_700036A0, D_OVL06_00826A70)) return -1;
    uint32_t p = 0;
    return a6_c_001EFEB0(o, (int32_t)0x80000042, S_700036A0, &p);
}

static void a6_00823580(A6 *o, uint32_t self)
{
    uint32_t w = self + 0x1F0;
    uint32_t state = a6_u8(o, self + 4);
    int32_t r = 0;
    if (state == 3 || state == 2) {
        a6_c_001AFC10(o, self);
        return;
    }
    if (state != 1) {
        if (state != 0) return;
        a6_w32(o, w, 0xF0);
        if (a6_rand(o, &r)) return;
        a6_w32(o, w + 4, (uint32_t)(r % 2 + 2));
        if (a6_rand(o, &r)) return;
        a6_w32(o, w + 8, (uint32_t)(r % 5 + 15));
        a6_w8(o, self + 4, 1);
        a6_w8(o, self + 5, 0);
    }
    uint32_t sub = a6_u8(o, self + 5);
    if (sub == 1) {
        if (a6_count_down(o, w + 8)) {
            if (a6_rand(o, &r)) return;
            a6_w32(o, w + 8, (uint32_t)(r % 5 + 15));
            if (a6_823580_spawn(o)) return;
        }
        if (a6_count_down(o, w + 4)) {
            if (a6_rand(o, &r)) return;
            a6_w32(o, w + 4, (uint32_t)(r % 2 + 2));
            if (a6_rand(o, &r)) return;
            uint32_t fx = A6_ADD(0xC3610000u, A6_MUL(F_M30, A6_DIV(A6_CVT(r), F_2_31)));
            a6_w32(o, S_700038A0, fx);
            if (a6_rand(o, &r)) return;
            uint32_t fz = A6_ADD(0xC414C000u, A6_MUL(0xC32A0000u, A6_DIV(A6_CVT(r), F_2_31)));
            a6_w32(o, S_700038A0 + 8, fz);
            a6_w32(o, S_700038A0 + 4, F_20);
            a6_w32(o, S_700038A0 + 0xC, F_ONE);
            if (a6_c_001028D0(o, S_700038B0, D_OVL06_00826A70, S_700038A0)) return;
            uint32_t x = a6_u32(o, S_700038B0), y = a6_u32(o, S_700038B0 + 4), z = a6_u32(o, S_700038B0 + 8);
            float len = 0.0f;
            if (a6_c_0011E748(o, fl(a6_sumsq3(x, y, z)), &len)) return;
            a6_w32(o, S_70003A20, a6_bits(len));
            if (a6_c_00102760(o, S_700038B0, S_700038B0)) return;
            if (a6_c_001CD390(o, S_700036A0, S_700038B0)) return;
            if (a6_c_00102918(o, S_700036A0, S_700036A0, S_700038A0)) return;
            uint32_t p = 0;
            if (a6_c_001EFEB0(o, (int32_t)0x8000003B, S_700036A0, &p)) return;
            if (p != 0) {
                a6_w8(o, p + 5, 1);
                a6_w32(o, p + 0x1F0, 0xC);
                a6_w32(o, p + 0x1F4, a6_u32(o, S_70003A20));
                a6_w32(o, p + 0x1F8, F_0_4);
            }
            if ((int32_t)a6_u32(o, S_70003B68) % 100 == 0) {
                if (a6_rand(o, &r)) return;
                int32_t pick = r % 4;
                if (pick == 0) {
                    if (a6_c_001FB9F0(o, 0x41D, 0x1000, 0x1000, 0x1000)) return;
                } else if (pick == 1) {
                    if (a6_c_001FB9F0(o, 0x41E, 0x1000, 0x1000, 0x1000)) return;
                } else if (pick == 2) {
                    if (a6_c_001FB9F0(o, 0x41F, 0x1000, 0x1000, 0x1000)) return;
                }
            }
        }
        if (a6_rand(o, &r)) return;
        if (r % 2 == 0 && (int32_t)a6_u32(o, w) % 3 == 0) {
            a6_001EA210(o, 0x40000000u);
            if (a6_failed(o)) return;
        }
        if (a6_count_down(o, w)) {
            a6_w32(o, w, 0xF0);
            a6_w8(o, self + 5, 0);
            a6_c_0019C6F0(o, 0xD, 0);
        }
    } else if (sub == 0) {
        if (a6_count_down(o, w + 8)) {
            if (a6_rand(o, &r)) return;
            a6_w32(o, w + 8, (uint32_t)(r % 50 + 50));
            if (a6_823580_spawn(o)) return;
            if (a6_c_001F02C0(o, D_OVL06_00826A70, 0x41D, fl(F_200))) return;
        }
        if (a6_count_down(o, w)) {
            a6_w32(o, w, 0x5A);
            a6_w8(o, self + 5, 1);
            a6_c_0019C6F0(o, 0xD, 1);
        }
    }
}

int em_area06_port_00823580(const EmArea06PortHooks *h, uint32_t self, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_00823580(&o, self);
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8242C0 (AREA06, a script op09 callback (self, st)): by st +4: 0
 * D_008106C5 = 1, st +4 + 1, returns 0; 1 with D_00810845 bit 5 set
 * 001FABB0() and 001FB0B0(0), returns 1; other values return 0.
 * ---------------------------------------------------------------------- */
int em_area06_port_008242C0(const EmArea06PortHooks *h, uint32_t self, uint32_t st, int32_t *result,
                            EmArea06PortFault *fault)
{
    A6 o;
    int32_t r = 0;
    (void)self;
    if (!result || a6_begin(&o, h, fault)) return -1;
    uint32_t s = a6_u8(&o, st + 4);
    if (s == 1) {
        if (a6_u8(&o, D_00810845) & 0x20) {
            if (!a6_c_001FABB0(&o)) a6_c_001FB0B0(&o, 0);
        }
        r = 1;
    } else if (s == 0) {
        a6_w8(&o, D_008106C5, 1);
        a6_w8(&o, st + 4, a6_u8(&o, st + 4) + 1);
    }
    if (a6_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x826950 (AREA01, the op09 callback of script 0x82B590 (self, st, prm);
 * the player block D_008102B0): by st +4:
 *  0  prm +0x20 = player +0xA0 (00102948), st +4 + 1, prm +0x10 = 0,
 *     player halfword +0x1F2 = 2, +0x25C = 2, +0x1F8 = 4.0; returns 0.
 *  1  goal = 001B1240(player +0xA0, prm +0x30, prm +0x38), player +0xC4 =
 *     001B12B0(goal, player +0xC4, 4 degrees); unless it equals goal
 *     return 0, else st +4 + 1 and on as 2.
 *  2  prm +0x10 >= prm +0xC (not <): player +0x1F2 = 0, +0x25C = 0,
 *     +0x1F8 = 4.0, returns 1. Otherwise the sound 0x4A at +0x10 == 20,
 *     40 and 55, +0x10 += 1.0, t = +0x10 / +0xC, st +0x10 = prm +0x30 -
 *     prm +0x20 (001028D0), 0x70003600 = prm +0x20 + t st +0x10 (xyz),
 *     00182F90(player, 0x70003600); returns 0.
 * ---------------------------------------------------------------------- */
int em_area06_port_00826950(const EmArea06PortHooks *h, uint32_t self, uint32_t st, uint32_t prm, int32_t *result,
                            EmArea06PortFault *fault)
{
    A6 o;
    int32_t r = 0;
    const uint32_t pl = D_008102B0;
    (void)self;
    if (!result || a6_begin(&o, h, fault)) return -1;
    uint32_t s = a6_u8(&o, st + 4);
    if (s == 0) {
        if (!a6_c_00102948(&o, prm + 0x20, pl + 0xA0)) {
            a6_w8(&o, st + 4, a6_u8(&o, st + 4) + 1);
            a6_w32(&o, prm + 0x10, 0);
            a6_w16(&o, pl + 0x1F2, 2);
            a6_w8(&o, pl + 0x25C, 2);
            a6_w32(&o, pl + 0x1F8, 0x40800000u);
        }
    } else if (s == 1 || s == 2) {
        int go = 1;
        if (s == 1) {
            float goal = 0.0f, turned = 0.0f;
            uint32_t x = a6_u32(&o, prm + 0x30);
            uint32_t z = a6_u32(&o, prm + 0x38);
            if (a6_c_001B1240(&o, pl + 0xA0, fl(x), fl(z), &goal)) return -1;
            uint32_t cur = a6_u32(&o, pl + 0xC4);
            if (a6_c_001B12B0(&o, goal, fl(cur), fl(F_0_06981), &turned)) return -1;
            int same = A6_EQ(a6_bits(turned), a6_bits(goal));
            a6_w32(&o, pl + 0xC4, a6_bits(turned));
            if (!same) {
                go = 0;
            } else {
                a6_w8(&o, st + 4, a6_u8(&o, st + 4) + 1);
            }
        }
        if (go) {
            uint32_t n = a6_u32(&o, prm + 0x10);
            uint32_t len = a6_u32(&o, prm + 0xC);
            if (!A6_LT(n, len)) {
                a6_w16(&o, pl + 0x1F2, 0);
                a6_w8(&o, pl + 0x25C, 0);
                a6_w32(&o, pl + 0x1F8, 0x40800000u);
                r = 1;
            } else {
                if (A6_EQ(F_20, n) && a6_c_001FB9F0(&o, 0x4A, 0x1000, 0x1000, 0x1000)) return -1;
                if (A6_EQ(F_40, a6_u32(&o, prm + 0x10)) && a6_c_001FB9F0(&o, 0x4A, 0x1000, 0x1000, 0x1000))
                    return -1;
                if (A6_EQ(F_55, a6_u32(&o, prm + 0x10)) && a6_c_001FB9F0(&o, 0x4A, 0x1000, 0x1000, 0x1000))
                    return -1;
                uint32_t v = A6_ADD(a6_u32(&o, prm + 0x10), F_ONE);
                a6_w32(&o, prm + 0x10, v);
                uint32_t t = A6_DIV(v, a6_u32(&o, prm + 0xC));
                if (a6_c_001028D0(&o, st + 0x10, prm + 0x30, prm + 0x20)) return -1;
                for (uint32_t k = 0; k < 12; k += 4) {
                    uint32_t d = a6_u32(&o, st + 0x10 + k);
                    uint32_t b = a6_u32(&o, prm + 0x20 + k);
                    a6_w32(&o, S_70003600 + k, A6_ADD(b, A6_MUL(d, t)));
                }
                if (a6_c_00182F90(&o, pl, S_70003600)) return -1;
            }
        }
    }
    if (a6_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x824560 (AREA06, the beam [11]). REC is its placement record,
 * (*(D_0024D7C0[D_00810700]))[D_00810701] + 0x28 +0x9A, read afresh at
 * every use (the order of those loads varies by site and follows the
 * original). Polygons 1 and 2 (0x827640 / 0x827680, 64 bytes each) are
 * copied into the frame (sp - 0x80 / sp - 0x40; frame 0xD0) and tested
 * against the player position D_00810350 with 001B1EA0.
 *  0  +0 = 1, +0x2EC = +0x2E4 = 0, +0x2E0 = 0.002; 001BA1C0(self, 0x10)
 *     set: +0xD = 12, +0x2D0 = 0, the pose from REC (+0xE = +0x2E, +0xC0..
 *     = +0x40.., +0xB0.. = +0x34..), 001B0FD0, +4 = 2, +5 = 1; else
 *     001B0FD0, +4 = 1 with D_00810845 bit 5 else 4, +0x2D0 = 1; then
 *     001C6380 and 001A2370(self, self +0xD0).
 *  4  the idle sway while +0x2EC is set (phase +0x2DC + 0.08, wrapped to
 *     -pi past pi; +0xC8 = REC +0x20 + +0x2E0 sin(phase), +0xB4 = REC
 *     +0x10 - 45 sin(+0xC8)); else the player at y >= 55 inside polygon
 *     1 starts a run (+0x2EC = 1, +0x2DC = 0, +0x2E4 = 240). During a run
 *     --+0x2E4 <= 0 ends it (pose from REC) and every 64th frame a random
 *     class 0x8000001F spawn near the beam, then 001C6380 / 001A2370. Bit
 *     5 of D_00810845 moves it to 1. Then 001B1B70 and the +0x4C method.
 *  1  sub 0: the player at y >= 55 inside polygon 2, D_008106B9 == 0 and
 *     D_008102B5 < 2 or in 29..34: script 0x827180 (001BA1A0 with self +
 *     0x1F0), +0x2EC = 30, +0x2E8 = 6, +0x2E4 = 18, +5 = 1, D_00810768 =
 *     1, +0x2D8 / +0x2D4 = the player y / z, +0x2E0 = 0.006, the player
 *     placed at (-270, 55, -583) facing +pi/2 (yaw > 0) or -pi/2, sound
 *     0x8CC. sub 1: see docs/AREA06_PORT.md section 2 (the shake, the
 *     lift, the fall with its spawns, the reset to state 2). Then (sub 1)
 *     001BA1F0 / 001C6380 / 001A2370, and 001B1B70 and the +0x4C method.
 *  2  sub 0: D_00810768 = 0xFF, +5 = 1, six class 0x80000015 spawns
 *     stepped from +0xB0; then while +0x2D0 is set 001BA1F0; 001C6380,
 *     001B1B70 and the +0x4C method.
 *  3 and other values: 001AFC10(self).
 * ---------------------------------------------------------------------- */
#define F_0_002   0x3B03126Fu
#define F_0_006   0x3BC49BA6u
#define F_0_0002  0x3951B717u
#define F_0_08    0x3DA3D70Au
#define F_0_04    0x3D23D70Au
#define F_0_6     0x3F19999Au
#define F_1_5     0x3FC00000u
#define F_2       0x40000000u
#define F_4       0x40800000u
#define F_5       0x40A00000u
#define F_10      0x41200000u
#define F_35      0x420C0000u
#define F_45      0x42340000u
#define F_46      0x42380000u
#define F_90      0x42B40000u
#define F_1_255   0x3C808080u /* 0.0156862735748291 */
#define F_PI_2_OV 0x3FC90FDBu
#define F_M_PI_2  0xBFC90FDBu
#define F_30      0x41F00000u

static uint32_t a6_rec_row(A6 *o, uint32_t area, uint32_t sub)
{
    uint32_t t = a6_u32(o, D_0024D7C0 + (area << 2));
    return a6_u32(o, t + (sub << 2));
}

/* The usual order: D_00810700, +0x9A, D_00810701, the table, the row. */
static uint32_t a6_rec(A6 *o, uint32_t self)
{
    uint32_t area = a6_u8(o, D_00810700);
    uint32_t uid = a6_u8(o, self + 0x9A);
    uint32_t sub = a6_u8(o, D_00810701);
    return uid * 0x28u + a6_rec_row(o, area, sub);
}

/* The order of the two phase sites: D_00810700, the phase word (returned
 * in *phase), D_00810701, +0x9A, the table, the row. */
static uint32_t a6_rec_phase(A6 *o, uint32_t self, uint32_t *phase)
{
    uint32_t area = a6_u8(o, D_00810700);
    *phase = a6_u32(o, self + 0x2DC);
    uint32_t sub = a6_u8(o, D_00810701);
    uint32_t uid = a6_u8(o, self + 0x9A);
    return uid * 0x28u + a6_rec_row(o, area, sub);
}

/* The pose loads from REC into self (+0xC4, +0xC8, +0xB4, +0xB8 from REC
 * +0x1C, +0x20, +0x10, +0x14): the end of a run / a shake. */
static void a6_rec_rest(A6 *o, uint32_t self)
{
    static const uint32_t to[4] = { 0xC4, 0xC8, 0xB4, 0xB8 }, from[4] = { 0x1C, 0x20, 0x10, 0x14 };
    for (unsigned k = 0; k < 4; k++) a6_w32(o, self + to[k], a6_u32(o, a6_rec(o, self) + from[k]));
}

/* The placement pose (+0xC0.. = REC +0x40.., +0xB0.. = REC +0x34..). */
static void a6_rec_pose(A6 *o, uint32_t self)
{
    static const uint32_t to[6] = { 0xC0, 0xC4, 0xC8, 0xB0, 0xB4, 0xB8 };
    static const uint32_t from[6] = { 0x40, 0x44, 0x48, 0x34, 0x38, 0x3C };
    for (unsigned k = 0; k < 6; k++) a6_w32(o, self + to[k], a6_u32(o, a6_rec(o, self) + from[k]));
}

static int a6_spawn(A6 *o, uint32_t cls) { return a6_c_001EFD20(o, (int32_t)cls, S_700038A0); }

/* 0x700038A0.. = (x, y, z) as word stores, in that order. */
static void a6_point(A6 *o, uint32_t x, uint32_t y, uint32_t z)
{
    a6_w32(o, S_700038A0, x);
    a6_w32(o, S_700038A0 + 4, y);
    a6_w32(o, S_700038A0 + 8, z);
}

/* 0x700038A0 lane k += v (or -= v): load, operate, store. */
static void a6_bump(A6 *o, uint32_t k, uint32_t v, int sub)
{
    uint32_t cur = a6_u32(o, S_700038A0 + 4 * k);
    a6_w32(o, S_700038A0 + 4 * k, sub ? A6_SUB(cur, v) : A6_ADD(cur, v));
}

/* The player's y while the beam tilts: D_00810354 = +0x2D8 - (45 + (+0xB0
 * - D_00810360)) sin(+0xC8). */
static int a6_carry(A6 *o, uint32_t self)
{
    float si = 0.0f;
    if (a6_c_0011E2A8(o, fl(a6_u32(o, self + 0xC8)), &si)) return -1;
    uint32_t px = a6_u32(o, D_00810360);
    uint32_t bx = a6_u32(o, self + 0xB0);
    uint32_t y0 = a6_u32(o, self + 0x2D8);
    a6_w32(o, D_00810354, A6_SUB(y0, A6_MUL(A6_ADD(F_45, A6_SUB(bx, px)), a6_bits(si))));
    return 0;
}

/* +0xB4 = REC +0x10 - 45 sin(+0xC8), REC read before +0xC8. */
static int a6_tilt_y(A6 *o, uint32_t self)
{
    uint32_t base = a6_u32(o, a6_rec(o, self) + 0x10);
    float si = 0.0f;
    if (a6_c_0011E2A8(o, fl(a6_u32(o, self + 0xC8)), &si)) return -1;
    a6_w32(o, self + 0xB4, A6_SUB(base, A6_MUL(F_45, a6_bits(si))));
    return 0;
}

/* The phase step: +0x2DC += step, stored; past pi the word -pi. */
static void a6_phase(A6 *o, uint32_t self, uint32_t step)
{
    uint32_t p = A6_ADD(a6_u32(o, self + 0x2DC), step);
    int in = A6_LE(p, F_PI);
    a6_w32(o, self + 0x2DC, p);
    if (!in) a6_w32(o, self + 0x2DC, F_M_PI);
}

static void a6_824560_state0(A6 *o, uint32_t self)
{
    int32_t r = 0;
    a6_w8(o, self, 1);
    a6_w32(o, self + 0x2EC, 0);
    a6_w32(o, self + 0x2E4, 0);
    a6_w32(o, self + 0x2E0, F_0_002);
    if (a6_c_001BA1C0(o, self, 0x10, &r)) return;
    if (r != 0) {
        a6_w8(o, self + 0xD, 0xC);
        a6_w32(o, self + 0x2D0, 0);
        a6_w16(o, self + 0xE, a6_u16(o, a6_rec(o, self) + 0x2E));
        a6_rec_pose(o, self);
        if (a6_c_001B0FD0(o, self, &r)) return;
        a6_w8(o, self + 4, 2);
        a6_w8(o, self + 5, 1);
    } else {
        if (a6_c_001B0FD0(o, self, &r)) return;
        a6_w8(o, self + 4, (a6_u8(o, D_00810845) & 0x20) ? 1 : 4);
        a6_w32(o, self + 0x2D0, 1);
    }
    if (a6_c_001C6380(o, self)) return;
    a6_c_001A2370(o, self, self + 0xD0);
}

static void a6_824560_state4(A6 *o, uint32_t self, uint32_t poly1)
{
    const uint32_t cnt = self + 0x2EC, tmr = self + 0x2E4;
    if (a6_u32(o, cnt) != 0) {
        a6_phase(o, self, F_0_08);
        uint32_t phase = 0;
        uint32_t rec = a6_rec_phase(o, self, &phase);
        uint32_t base = a6_u32(o, rec + 0x20);
        float si = 0.0f;
        if (a6_c_0011E2A8(o, fl(phase), &si)) return;
        uint32_t amp = a6_u32(o, self + 0x2E0);
        a6_w32(o, self + 0xC8, A6_ADD(base, A6_MUL(amp, a6_bits(si))));
        if (a6_tilt_y(o, self)) return;
    } else if (!A6_LT(a6_u32(o, D_00810354), F_55)) {
        int32_t in = 0;
        if (a6_c_001B1EA0(o, 0, D_00810350, poly1, 4, &in)) return;
        if (in == 1) {
            a6_w32(o, cnt, 1);
            a6_w32(o, self + 0x2DC, 0);
            a6_w32(o, tmr, 0xF0);
        }
    }
    if (a6_u32(o, cnt) != 0) {
        a6_w32(o, tmr, a6_u32(o, tmr) - 1u);
        if (!((int32_t)a6_u32(o, tmr) > 0)) {
            a6_w32(o, cnt, 0);
            a6_rec_rest(o, self);
        }
        if ((a6_u32(o, tmr) & 0x3F) == 0) {
            int32_t r = 0;
            if (a6_c_00122BB8(o, &r)) return;
            int32_t x = (int32_t)((uint32_t)(r >> 16) << 1) >> 15;
            if (x != 0) {
                if (a6_c_00102948(o, S_700038A0, self + 0xB0)) return;
                uint32_t px = A6_SUB(a6_u32(o, S_700038A0), F_46);
                uint32_t area = a6_u8(o, D_00810700);
                uint32_t sub = a6_u8(o, D_00810701);
                a6_w32(o, S_700038A0, px);
                uint32_t uid = a6_u8(o, self + 0x9A);
                uint32_t base = a6_u32(o, uid * 0x28u + a6_rec_row(o, area, sub) + 0x10);
                float si = 0.0f;
                if (a6_c_0011E2A8(o, fl(a6_u32(o, self + 0xC8)), &si)) return;
                a6_w32(o, S_700038A0 + 4, A6_ADD(F_1_5, A6_SUB(base, A6_MUL(F_90, a6_bits(si)))));
                if (a6_c_00122BB8(o, &r)) return;
                int32_t y = (int32_t)((uint32_t)(r >> 16) << 8) >> 15;
                uint32_t z = a6_u32(o, S_700038A0 + 8);
                uint32_t dz = A6_SUB(A6_MUL(F_1_255, A6_CVT(y)), F_ONE);
                a6_w32(o, S_700038A0 + 8, A6_ADD(z, dz));
                if (a6_spawn(o, 0x8000001Fu)) return;
            }
        }
        if (a6_c_001C6380(o, self)) return;
        if (a6_c_001A2370(o, self, self + 0xD0)) return;
    }
    if (a6_u8(o, D_00810845) & 0x20) a6_w8(o, self + 4, 1);
    if (a6_c_001B1B70(o, self)) return;
    a6_callback(o, a6_u32(o, self + 0x4C), self);
}

/* State 1, sub 1, the shake (+0x2E8 set). */
static int a6_824560_shake(A6 *o, uint32_t self)
{
    const uint32_t cnt = self + 0x2E8, tmr = self + 0x2E4;
    a6_phase(o, self, F_0_1);
    uint32_t phase = 0;
    uint32_t rec = a6_rec_phase(o, self, &phase);
    uint32_t base = a6_u32(o, rec + 0x20);
    float si = 0.0f;
    if (a6_c_0011E2A8(o, fl(phase), &si)) return -1;
    uint32_t amp = a6_u32(o, self + 0x2E0);
    a6_w32(o, self + 0xC8, A6_ADD(base, A6_MUL(amp, A6_ADD(F_ONE, a6_bits(si)))));
    if (a6_tilt_y(o, self)) return -1;
    if (a6_carry(o, self)) return -1;
    a6_w32(o, tmr, a6_u32(o, tmr) - 1u);
    if ((a6_u32(o, tmr) & 0x3F) == 0 && (int32_t)a6_u32(o, cnt) > 1) {
        int32_t r = 0;
        if (a6_c_00102948(o, S_700038A0, self + 0xB0)) return -1;
        if (a6_c_00122BB8(o, &r)) return -1;
        int32_t x = (int32_t)((uint32_t)(r >> 16) * 90u) >> 15;
        uint32_t cx = A6_CVT(x);
        uint32_t px = a6_u32(o, S_700038A0);
        a6_w32(o, S_700038A0, A6_ADD(px, A6_SUB(cx, F_45)));
        uint32_t py = a6_u32(o, S_700038A0 + 4);
        a6_w32(o, S_700038A0 + 4, A6_ADD(py, F_1_5));
        if (a6_spawn(o, 0x8000001Fu)) return -1;
    }
    if (!((int32_t)a6_u32(o, tmr) > 0)) {
        a6_w32(o, cnt, a6_u32(o, cnt) - 1u);
        int32_t left = (int32_t)a6_u32(o, cnt);
        if (!(left > 0)) {
            a6_rec_rest(o, self);
            if (a6_c_00102948(o, S_700038A0, self + 0xB0)) return -1;
            a6_bump(o, 0, F_30, 1);
            if (a6_spawn(o, 0x80000021u)) return -1;
        } else {
            if (left == 1) {
                if (a6_c_00102948(o, S_700038A0, self + 0xB0)) return -1;
                a6_bump(o, 0, F_40, 0);
                if (a6_spawn(o, 0x80000015u)) return -1;
                uint32_t bx = a6_u32(o, self + 0xB0);
                a6_w32(o, S_700038A0, A6_SUB(bx, F_10));
                uint32_t by = a6_u32(o, self + 0xB4);
                a6_w32(o, S_700038A0 + 4, A6_SUB(by, F_10));
                uint32_t bz = a6_u32(o, self + 0xB8);
                a6_w32(o, S_700038A0 + 8, A6_ADD(F_4, bz));
                if (a6_spawn(o, 0x80000021u)) return -1;
            }
            a6_w32(o, tmr, 0x12);
        }
    }
    return 0;
}

/* State 1, sub 1, +0x2E8 clear: the lift (+0x2EC > 0) and the fall
 * (+0x2EC < 0). */
static int a6_824560_lift(A6 *o, uint32_t self)
{
    const uint32_t cnt = self + 0x2EC;
    int32_t v = (int32_t)a6_u32(o, cnt);
    if (v > 0) {
        uint32_t area = a6_u8(o, D_00810700);
        uint32_t k = A6_CVT(31 - v);
        uint32_t sub = a6_u8(o, D_00810701);
        a6_w32(o, S_70003680, k);
        uint32_t table = a6_u32(o, D_0024D7C0 + (area << 2));
        uint32_t kk = a6_u32(o, S_70003680);
        uint32_t uid = a6_u8(o, self + 0x9A);
        uint32_t row = a6_u32(o, table + (sub << 2));
        uint32_t base = a6_u32(o, uid * 0x28u + row + 0x20);
        a6_w32(o, self + 0xC8, A6_ADD(A6_MUL(F_0_0002, A6_MUL(kk, kk)), base));
        if (a6_tilt_y(o, self)) return -1;
        if (a6_carry(o, self)) return -1;
        a6_w32(o, cnt, a6_u32(o, cnt) - 1u);
        if (a6_u32(o, cnt) == 0) {
            a6_w32(o, cnt, (uint32_t)-50);
            a6_w32(o, self + 0x2E0, F_0_6);
            a6_point(o, 0xC38A8000u, 0x422C0000u, 0xC4118000u);
            if (a6_spawn(o, 0x80000015u)) return -1;
            if (a6_spawn(o, 0x80000021u)) return -1;
        }
        return 0;
    }
    if (v >= 0) return 0;
    a6_w32(o, self + 0x2E0, A6_SUB(a6_u32(o, self + 0x2E0), F_0_04));
    a6_w32(o, self + 0xC8, A6_SUB(a6_u32(o, self + 0xC8), F_0_006));
    uint32_t y = a6_u32(o, self + 0xB4);
    uint32_t vy = a6_u32(o, self + 0x2E0);
    a6_w32(o, self + 0xB4, A6_ADD(y, vy));
    if ((int32_t)a6_u32(o, cnt) < -25)
        a6_w32(o, D_00810354, A6_ADD(F_5, a6_u32(o, self + 0xB4)));
    else
        a6_w32(o, D_00810354, A6_ADD(F_8, a6_u32(o, self + 0xB4)));
    a6_w32(o, cnt, a6_u32(o, cnt) + 1u);
    int32_t n = (int32_t)a6_u32(o, cnt);
    if (n == 0) {
        a6_w8(o, self + 0xD, a6_u8(o, a6_rec(o, self) + 0x2C));
        a6_w16(o, self + 0xE, a6_u16(o, a6_rec(o, self) + 0x2E));
        a6_rec_pose(o, self);
        if (a6_c_001AF800(o, self)) return -1;
        if (a6_c_001CB5B0(o, (int32_t)a6_u8(o, self + 9))) return -1;
        int32_t r = 0;
        if (a6_c_001B0FD0(o, self, &r)) return -1;
        a6_w8(o, self + 4, 2);
        a6_w8(o, self + 5, 0);
    } else if (n == -30) {
        a6_point(o, 0xC3500000u, 0x41E00000u, 0xC4148000u);
        if (a6_spawn(o, 0x80000015u) || a6_spawn(o, 0x80000021u)) return -1;
    } else if (n == -25) {
        a6_point(o, 0xC3810000u, 0x41E00000u, 0xC4120000u);
        if (a6_spawn(o, 0x80000021u)) return -1;
        a6_bump(o, 0, F_45, 0);
        if (a6_spawn(o, 0x80000021u)) return -1;
        a6_w32(o, S_700038A0, a6_u32(o, self + 0xB0));
        a6_w32(o, S_700038A0 + 4, a6_u32(o, self + 0xB4));
        a6_w32(o, S_700038A0 + 8, a6_u32(o, self + 0xB8));
        if (a6_spawn(o, 0x80000015u)) return -1;
    } else if (n == -20) {
        a6_point(o, 0xC3640000u, 0x41E00000u, 0xC4148000u);
        if (a6_spawn(o, 0x80000015u) || a6_spawn(o, 0x80000021u)) return -1;
        a6_point(o, 0xC3860000u, 0x41E00000u, 0xC4148000u);
        if (a6_spawn(o, 0x80000015u)) return -1;
    } else if (n == -14) {
        a6_point(o, 0xC3810000u, 0x41E00000u, 0xC4148000u);
        if (a6_spawn(o, 0x80000015u) || a6_spawn(o, 0x80000021u)) return -1;
    } else if (n == -3) {
        a6_point(o, 0xC38A8000u, 0x41E00000u, 0xC4148000u);
        if (a6_spawn(o, 0x80000015u)) return -1;
        if (a6_c_00102948(o, S_700038A0, self + 0xB0)) return -1;
        a6_bump(o, 0, F_35, 1);
        a6_bump(o, 1, F_3, 0);
        a6_bump(o, 2, F_20, 1);
        if (a6_spawn(o, 0x80000015u)) return -1;
        a6_bump(o, 0, F_10, 0);
        a6_bump(o, 1, F_2, 1);
        if (a6_spawn(o, 0x80000015u)) return -1;
        a6_bump(o, 0, F_10, 0);
        if (a6_spawn(o, 0x80000015u)) return -1;
    }
    return 0;
}

static void a6_824560_state1(A6 *o, uint32_t self, uint32_t poly2)
{
    uint32_t sub = a6_u8(o, self + 5);
    if (sub == 1) {
        int bad = a6_u32(o, self + 0x2E8) != 0 ? a6_824560_shake(o, self) : a6_824560_lift(o, self);
        if (bad || a6_failed(o)) return;
        if (a6_c_001BA1F0(o, self)) return;
        if (a6_c_001C6380(o, self)) return;
        if (a6_c_001A2370(o, self, self + 0xD0)) return;
    } else if (sub == 0) {
        int32_t in = 0;
        if (!A6_LT(a6_u32(o, D_00810354), F_55)) {
            if (a6_c_001B1EA0(o, 0, D_00810350, poly2, 4, &in)) return;
            if (in == 1 && a6_u8(o, D_008106B9) == 0) {
                uint32_t mode = a6_u8(o, D_008102B5);
                if ((int32_t)mode < 2 || ((int32_t)mode >= 0x1D && (int32_t)mode < 0x23)) {
                    if (a6_c_001BA1A0(o, self + 0x1F0, D_OVL06_00827180)) return;
                    a6_w32(o, self + 0x2EC, 0x1E);
                    a6_w32(o, self + 0x2E8, 6);
                    a6_w32(o, self + 0x2E4, 0x12);
                    a6_w8(o, self + 5, 1);
                    a6_w8(o, D_00810768, 1);
                    a6_w32(o, self + 0x2D8, a6_u32(o, D_00810354));
                    a6_w32(o, self + 0x2D4, a6_u32(o, D_00810358));
                    a6_w32(o, self + 0x2E0, F_0_006);
                    a6_w32(o, D_00810350, 0xC3870000u);
                    a6_w32(o, D_00810354, F_55);
                    a6_w32(o, D_00810358, 0xC411C000u);
                    a6_w32(o, D_00810374, A6_LE(a6_u32(o, D_00810374), F_ZERO) ? F_M_PI_2 : F_PI_2_OV);
                    if (a6_c_001FBD50(o, self, 0x8CC, 0, fl(F_300))) return;
                }
            }
        }
    }
    if (a6_c_001B1B70(o, self)) return;
    a6_callback(o, a6_u32(o, self + 0x4C), self);
}

static void a6_824560_state2(A6 *o, uint32_t self)
{
    uint32_t sub = a6_u8(o, self + 5);
    if (sub == 0 || sub == 1) {
        if (sub == 0) {
            a6_w8(o, D_00810768, 0xFF);
            a6_w8(o, self + 5, 1);
            if (a6_c_00102948(o, S_700038A0, self + 0xB0)) return;
            a6_bump(o, 0, F_45, 1);
            a6_bump(o, 1, F_4, 1);
            a6_bump(o, 2, F_10, 1);
            if (a6_spawn(o, 0x80000015u)) return;
            static const uint32_t dy[5] = { F_ONE, F_2, 0, F_2, F_2 };
            static const int down[5] = { 0, 1, 0, 0, 0 };
            for (unsigned k = 0; k < 5; k++) {
                a6_bump(o, 0, F_10, 0);
                if (dy[k]) a6_bump(o, 1, dy[k], down[k]);
                if (a6_spawn(o, 0x80000015u)) return;
            }
        }
        if (a6_u32(o, self + 0x2D0) != 0 && a6_c_001BA1F0(o, self)) return;
    }
    if (a6_c_001C6380(o, self)) return;
    if (a6_c_001B1B70(o, self)) return;
    a6_callback(o, a6_u32(o, self + 0x4C), self);
}

static void a6_00824560(A6 *o, uint32_t self, uint32_t sp)
{
    uint32_t frame = sp - 0xD0, poly1 = frame + 0x50, poly2 = frame + 0x90;
    uint8_t q[4][16];
    for (unsigned k = 0; k < 4; k++) a6_q(o, D_OVL06_00827640 + 16 * k, q[k]);
    for (unsigned k = 0; k < 4; k++) a6_wq(o, poly1 + 16 * k, q[k]);
    for (unsigned k = 0; k < 4; k++) a6_q(o, D_OVL06_00827680 + 16 * k, q[k]);
    for (unsigned k = 0; k < 4; k++) a6_wq(o, poly2 + 16 * k, q[k]);
    switch (a6_u8(o, self + 4)) {
    case 0: a6_824560_state0(o, self); break;
    case 1: a6_824560_state1(o, self, poly2); break;
    case 2: a6_824560_state2(o, self); break;
    case 4: a6_824560_state4(o, self, poly1); break;
    default: a6_c_001AFC10(o, self); break;
    }
}

int em_area06_port_00824560(const EmArea06PortHooks *h, uint32_t self, uint32_t sp, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_00824560(&o, self, sp);
    return a6_end(&o);
}
