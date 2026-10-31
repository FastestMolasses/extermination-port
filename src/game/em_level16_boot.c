/* Boot workers of the sixteenth level, from the byte-matched decomp C. See
 * em_level16_boot.h. Callees stay explicit hooks. */
#include "em_level16_boot_internal.h"
#define B(a) l16b_u8(o,(a))
#define SH(a) l16b_s16(o,(a))
#define R(a) l16b_u32(o,(a))
#define W(a,v) l16b_w32(o,(a),(v))
#define WB(a,v) l16b_w8(o,(a),(v))
#define WH(a,v) l16b_w16(o,(a),(v))
#define F(v) l16b_bits(v)
#define NEG(v) em_ee_neg_bits(v)
#define C(fn,...) do { if (l16b_c_##fn(o, __VA_ARGS__)) return 0; } while (0)

/* The originals' struct copies move doublewords. */
static uint64_t l16b_u64(L16B *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, l16b_at(o, a, 8), 8);
    return v;
}

static void l16b_w64(L16B *o, uint32_t a, uint64_t v) { memcpy(l16b_at(o, a, 8), &v, 8); }

/* The turn toward a target direction: the angle between the actor's forward
 * and the flattened direction decides 001B1380 at the actor, or 001B39F0 from
 * a probe ahead of it. The two vectors live in the 0x90 frame. */
static int32_t e001B3670(L16B *o, uint32_t self, uint32_t target, uint32_t aim, uint32_t sp)
{
    const uint32_t fwd = sp - 0x20u, dir = sp - 0x10u;
    uint32_t v, angle, half;
    int32_t r = 0;
    W(fwd, 0); W(fwd + 4, 0); W(fwd + 8, F(1.0f)); W(fwd + 0xC, F(1.0f));
    C(001B2B10, self, fwd, fwd);
    C(00102948, dir, target);
    W(dir + 4, 0);
    C(00102760, dir, dir);
    C(00102738, fwd, dir, &v);
    C(0011E420, v, &angle);
    if (L16B_LE(angle, F(2.3561945f))) {
        C(001028B8, dir, dir, self + 0xB0);
        C(001B1380, dir, self + 0xB0, R(self + 0xC4), &r);
        return r;
    }
    W(fwd + 8, 0); W(fwd + 4, 0); W(fwd, 0); W(fwd + 0xC, F(1.0f));
    C(001B15D0, self + 0xB0, aim, &v);
    half = L16B_DIV(v, F(2.0f));
    W(fwd + 8, half);
    C(001B2B10, self, fwd, fwd);
    C(001028B8, fwd, fwd, self + 0xB0);
    C(001B39F0, self, fwd, dir, &r);
    return (int8_t)r;
}

/* 0017C370: the reaction's reset of the motion words; in the modes 6 and 7 its
 * clip and the half turn. */
static uint32_t e0017C370(L16B *o, uint32_t e)
{
    const uint32_t mode = B(e + 0x1F0);
    if (mode == 6 || mode == 7) {
        int32_t clip = 0;
        uint32_t yaw = 0;
        C(0017B490, e, B(e + 0x1F1) == 3 ? 2 : 4, (int32_t)B(e + 0x235), 0, &clip);
        C(001749A0, e, clip, 1, 0);
        C(001B1470, L16B_ADD(F(3.1415927f), R(e + 0xC4)), &yaw);
        W(e + 0xC4, yaw);
        W(e + 0x1FC, F(8.0f));
    }
    W(e + 0x38, 0);
    W(e + 0x240, 0);
    WB(e + 0x25C, 0);
    WB(e + 0x25E, 0);
    W(e + 0x260, 0);
    W(e + 0x264, 0);
    W(e + 0x268, 0);
    return 0;
}

/* The player's action-blocked test. */
static int32_t e0021BED0(L16B *o, uint32_t e)
{
    int32_t r = 0;
    if (B(0x70003B8D) || L16B_LE(R(e + 0x220), 0) || B(e) != 1 || B(e + 4) != 1 || B(e + 0x236))
        return 1;
    C(0021BB00, e, &r);
    if (r) return 1;
    C(0021BC40, e, &r);
    if (r) return 1;
    if (!B(e + 0xA) || SH(e + 0x20E)) return 1;
    return 0;
}

/* Hit reaction 0x3C: the direction from the hit record's kind. */
static uint32_t e0021BF90(L16B *o, uint32_t p, uint32_t hit)
{
    WB(p, B(p) | 2);
    WB(p + 4, 2); WB(p + 5, 0xD); WB(p + 6, 0);
    WB(p + 0x1F0, 0x3C); WB(p + 0x1F1, 1);
    switch (B(hit + 3)) {
    case 1: WB(p + 0xD, 0); WB(p + 0x25F, 1); break;
    case 4: WB(p + 0xD, 1); break;
    case 7: WB(p + 0xD, 2); WB(p + 0x25F, 1); break;
    case 6: WB(p + 0xD, 3); WB(p + 0x25F, 1); break;
    case 5: WB(p + 0xD, 4); break;
    default: WB(p + 0xD, 0); break;
    }
    (void)e0017C370(o, p);
    return 0;
}

/* The follow-up of reaction 0x3C. */
static uint32_t e0021C040(L16B *o, uint32_t p, uint32_t hit)
{
    if (B(p + 0x1F0) != 0x3C) return 0;
    if (B(hit + 3) == 5) {
        WB(p + 4, 1); WB(p + 5, 0); WB(p + 6, 0); WB(p + 0x1F0, 0);
        WH(p + 0x20E, 0x3C);
        return 0;
    }
    WB(p + 4, 2); WB(p + 5, 0xE); WB(p + 6, 0); WB(p + 0x1F0, 0x3D); WB(p + 0x1F1, 1);
    switch (B(hit + 3)) {
    case 4: WB(p + 0xD, 1); WB(p + 0xF, 4); break;
    case 7: WB(p + 0xD, 2); WB(p + 0xF, 8); break;
    case 6: WB(p + 0xD, 3); WB(p + 0xF, 9); break;
    default: WB(p + 0xD, 0); break;
    }
    return 0;
}

/* 001BD460: the plain doors closing, the twin of 001BD370: rec +0x14 falls by
 * 0.2 to 0, under 2 rec +0x10 too; the four pose floats +0x7C; 1 when closed. */
static int32_t e001BD460(L16B *o, uint32_t rec)
{
    uint32_t b = L16B_SUB(R(rec + 0x14), F(0.2f)), a, table, f10, f14;
    W(rec + 0x14, b);
    if (L16B_LE(b, F(2.0f))) {
        if (L16B_LE(b, 0)) W(rec + 0x14, 0);
        a = L16B_SUB(R(rec + 0x10), F(0.2f));
        W(rec + 0x10, a);
        if (L16B_LE(a, 0)) W(rec + 0x10, 0);
    }
    table = R(0x275B40);
    f14 = R(rec + 0x14);
    W(R(table + 4) + 0x7C, NEG(f14));
    table = R(0x275B40);
    f10 = R(rec + 0x10);
    f14 = R(rec + 0x14);
    W(R(table + 8) + 0x7C, L16B_SUB(NEG(f10), f14));
    table = R(0x275B40);
    f10 = R(rec + 0x10);
    f14 = R(rec + 0x14);
    W(R(table + 0xC) + 0x7C, L16B_ADD(f10, f14));
    table = R(0x275B40);
    f14 = R(rec + 0x14);
    W(R(table + 0x10) + 0x7C, f14);
    return L16B_EQ(R(rec + 0x10), 0) ? 1 : 0;
}

int em_level16_boot_001BD460(const EmLevel16BootHooks *h, uint32_t a0, int32_t *result, EmLevel16BootFault *fault)
{
    L16B o;
    if (!result || l16b_begin(&o, h, fault)) return -1;
    const int32_t r = e001BD460(&o, a0);
    if (l16b_end(&o)) return -1;
    *result = r;
    return 0;
}

/* 001C82A0(e, out): the actor's world matrix, then per bone a fresh matrix in
 * 0x70003440 (identity, the bone's translation +0x70, its rotation +0x7C..84,
 * the Euler halfwords +0x88..8C at 1/4096 over the rows), multiplied with the
 * pose matrix `out` and the parent's +0x90 (or the world). */
static uint32_t e001C82A0(L16B *o, uint32_t e, uint32_t out)
{
    const uint32_t m = 0x70003440u;
    C(001C94B0, e + 0xD0, e + 0xB0, e + 0xC0, e + 0x60);
    for (uint32_t i = 0; i < B(e + 0xC); ++i) {
        C(001029C0, m);
        C(00102C58, m, m, R(e + 0x110 + i * 4) + 0x70);
        W(0x70003470, R(R(e + 0x110 + i * 4) + 0x7C));
        W(0x70003474, R(R(e + 0x110 + i * 4) + 0x80));
        W(0x70003478, R(R(e + 0x110 + i * 4) + 0x84));
        for (uint32_t k = 0; k < 3; ++k) {
            const uint32_t turn = L16B_MUL(F(0.00024414062f), L16B_CVT_S_W((uint32_t)SH(R(e + 0x110 + i * 4) + 0x88 + k * 2)));
            C(00103230, m + k * 0x10, m + k * 0x10, turn);
        }
        C(001026D0, m, out + i * 0x40, m);
        const uint32_t p = R(e + 0x110 + i * 4);
        const int32_t parent = SH(p + 0x64);
        if (parent != -1) C(001026D0, p + 0x90, R(e + (uint32_t)parent * 4 + 0x110) + 0x90, m);
        else C(001026D0, p + 0x90, e + 0xD0, m);
    }
    return 0;
}

/* 001B3C30(a, m, colour; reach, damage): the beam from the matrix's position
 * along its x: 0019AA80 at the player's hull unless 0021BE40 frees it, 0019A570
 * at the world; the hit's effects and the bolt 001E2BA0, and the flash node. */
static uint32_t e001B3C30(L16B *o, uint32_t a, uint32_t m, uint32_t colour, uint32_t reach, uint32_t damage, uint32_t sp)
{
    const uint32_t from = sp - 0x40u, to = sp - 0x30u, normal = sp - 0x20u, dir = sp - 0x10u;
    uint32_t hit = 0, e;
    int32_t r = 0;
    C(00102948, from, m + 0x30);
    W(m + 0x38, 0); W(m + 0x34, 0); W(m + 0x30, 0);
    W(to, reach); W(to + 4, 0); W(to + 8, 0); W(to + 0xC, F(1.0f));
    C(001026A0, to, m, to);
    C(001028B8, to, from, to);
    C(00102948, m + 0x30, from);
    C(001028D0, dir, to, from);
    C(00102760, dir, dir);
    C(0021BE40, 0x8102B0, a, &r);
    if (r == 0) {
        C(0019AA80, from, to, 0x20, &r);
        if (r) {
            hit = 1;
            C(00102948, to, 0x700031B0);
            e = R(0x700031D0);
            W(normal, R(e + 0x24)); W(normal + 4, R(e + 0x28)); W(normal + 8, R(e + 0x2C));
            W(normal + 0xC, F(1.0f));
            W(to + 0xC, F(1.0f));
        }
    }
    C(0019A570, from, to, 7, 0x20, &r);
    if (r) {
        C(00102948, to, 0x700031B0);
        e = R(0x700031D0);
        W(0x70003600, R(e + 0x24)); W(0x70003604, R(e + 0x28)); W(0x70003608, R(e + 0x2C));
        W(0x7000360C, F(1.0f));
        W(to + 0xC, F(1.0f));
        if (R(0x700031D8) != 1) {
            C(001EFD90, (int32_t)0x80000003u, to, 0x70003600);
            if (R(0x700031D8) == 2) {
                const uint32_t t = R(0x700031D4);
                if (t && B(t + 2) == 4) {
                    const uint32_t st = B(t + 3);
                    if (st != 6 && st != 0x1C && st != 0x1E && st != 0x1F) {
                        int32_t v = 0;
                        C(001281C0, damage, &v);
                        WH(t + 0x36, (uint32_t)v);
                    }
                }
            }
        }
    } else if (hit) {
        C(001EFD90, (int32_t)0x80000006u, to, normal);
        W(0x8104D4, damage);
        WB(0x8102B0, B(0x8102B0) | 2);
        C(00102948, 0x810320, dir);
    }
    C(001E2BA0, from, to, colour, reach);
    uint32_t n = 0;
    C(001F4F40, 0, &n);
    if (n) {
        C(00102958, n + 0xD0, m);
        C(00102948, n + 0xB0, m + 0x30);
    }
    return 0;
}

int em_level16_boot_001C82A0(const EmLevel16BootHooks *h, uint32_t a0, uint32_t a1, EmLevel16BootFault *fault)
{
    L16B o;
    if (l16b_begin(&o, h, fault)) return -1;
    (void)e001C82A0(&o, a0, a1);
    return l16b_end(&o);
}

int em_level16_boot_001B3C30(const EmLevel16BootHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, float a3, float a4, uint32_t sp, EmLevel16BootFault *fault)
{
    L16B o;
    if (l16b_begin(&o, h, fault)) return -1;
    (void)e001B3C30(&o, a0, a1, a2, l16b_bits(a3), l16b_bits(a4), sp);
    return l16b_end(&o);
}

/* The seal (AREA03's [23]): the lock bit by the area byte, cut by any hit. */
static uint32_t e00158430(L16B *o, uint32_t self)
{
    int32_t r = 0;
    switch (B(self + 4)) {
    case 0: {
        const uint32_t area = B(0x810700), bit = l16b_u16(o, self + 0x2E);
        if (B(0x810841u + area) & ((1u << bit) & 0xFF)) {
            WB(self + 4, 3);
            break;
        }
        C(001B0FD0, self, &r);
        if (!r) {
            C(001C6380, self);
            WB(self, 1);
            C(001F1110, self, 2);
        }
        break;
    }
    case 1:
        if (SH(self + 0x36)) {
            WB(self, 2);
            WB(self + 4, 2);
            const uint32_t at = 0x810841u + B(0x810700), bit = B(self + 0x2E);
            WB(at, B(at) | (1u << bit));
            WB(0x810845, B(0x810845) | 4);
            C(001FB9F0, 0x3F1, 0x1000, 0x1000, 0x1000, &r);
        }
        C(001F1180, self);
        C(001B17A0, self, &r);
        if (l16b_method(o, self)) return 0;
        break;
    case 2: case 3:
        C(001AFC10, self);
        break;
    default:
        break;
    }
    return 0;
}

int em_level16_boot_00158430(const EmLevel16BootHooks *h, uint32_t a0, EmLevel16BootFault *fault)
{
    L16B o;
    if (l16b_begin(&o, h, fault)) return -1;
    (void)e00158430(&o, a0);
    return l16b_end(&o);
}

/* AREA03 0x8235A0 (sub 1's placement [22], the terminal of flag 0x29). */
static uint32_t a03_8235A0(L16B *o, uint32_t self)
{
    const uint32_t talk = self + 0x1F0, flag = 0x810781, count = 0x810801;
    int32_t r = 0;
    switch (B(self + 4)) {
    case 0:
        C(001B0FD0, self, &r);
        if (r) break;
        C(001C6380, self);
        WB(self + 4, 1); WB(self, 1); WB(self + 8, 3);
        W(self + 0x30, 0x275900);
        if (B(flag) == 0xFF) WB(self + 2, 0x84);
        break;
    case 1:
        switch (B(self + 5)) {
        case 0:
            if (B(flag) == 1) {
                C(001BA1A0, talk, 0x827170);
                WB(self + 5, 1);
            }
            if ((B(self + 0xB) & 4) && B(flag) == 0xFF) {
                C(001BA1A0, talk, 0x827630);
                WB(self + 5, 1);
            } else {
                WB(self + 0xB, 0);
            }
            break;
        case 1:
            C(001BA1F0, self, &r);
            if (r) {
                if (B(count) != 0xFF) {
                    C(001C47A0, 0x2A, 1);
                    C(001C4760, 0x12, 1);
                    WB(count, 0xFF);
                    WB(self + 2, 0x84);
                }
                WB(self + 0xB, 0);
                WB(self + 5, 0);
            }
            break;
        default:
            break;
        }
        C(001B17A0, self, &r);
        if (l16b_method(o, self)) return 0;
        break;
    case 2: case 3:
        C(001AFC10, self);
        break;
    default:
        break;
    }
    return 0;
}

/* AREA03 0x823780: state 0 of 0x823810. */
static int32_t a03_823780(L16B *o, uint32_t self)
{
    int32_t r = 0;
    W(self + 0x68, F(1.0f)); W(self + 0x64, F(1.0f)); W(self + 0x60, F(1.0f));
    C(001B1020, self, (int32_t)B(self + 0xD), -1, 0, &r);
    if (r) return 1;
    C(001C6380, self);
    WB(self, 1); WB(self + 8, 3);
    W(self + 0x30, 0x275908);
    C(001F1110, self, 0);
    return 0;
}

/* AREA03 0x823810 (sub 1's deferred group 0x826BC0[3], g[3]: the panel that
 * starts flag 0x29). */
static uint32_t a03_823810(L16B *o, uint32_t self)
{
    int32_t r = 0;
    const uint32_t state = B(self + 4);
    if (state == 0) {
        (void)a03_823780(o, self);
        return 0;
    }
    if (state == 1) {
        switch (B(self + 5)) {
        case 0:
            if (B(self + 0xB) & 4) {
                WB(0x810781, 1);
                WB(self + 5, B(self + 5) + 1);
            }
            break;
        case 1:
            if (B(0x810781) == 0xFF) WB(self + 4, state + 1);
            break;
        default:
            break;
        }
        if (B(0x810801) == 0) {
            C(001F1180, self);
            C(001B17A0, self, &r);
            if (r && l16b_method(o, self)) return 0;
        }
        return 0;
    }
    C(001B1190, (int32_t)B(self + 0x9A));
    C(001AFC10, self);
    return 0;
}

/* AREA03 0x825200 (0x823930's muzzle flash, the same code as AREA01 0x8287C0):
 * class 0xC node at the matrix, behaviour 001F5040. The colour vector
 * 0x8282C0 is copied into the frame. */
static uint32_t a03_825200(L16B *o, uint32_t src, uint32_t sp)
{
    uint32_t n = 0;
    const uint32_t v = sp - 0x10u;
    C(001AFA90, 0xC, &n);
    if (n) {
        const uint64_t lo = l16b_u64(o, 0x8282C0), hi = l16b_u64(o, 0x8282C8);
        l16b_w64(o, v, lo);
        l16b_w64(o, v + 8, hi);
        C(00102948, n + 0xB0, src + 0x30);
        C(00102958, n + 0xD0, src);
        C(001026A0, n + 0x100, n + 0xD0, v);
        W(n + 0x10, 0x1F5040);
    }
    return 0;
}

/* AREA04 0x825040: [1]'s step while counter 0x28 is 0: inside the polygon read
 * from 0x829530 below y 25 it starts script 0x829020 (flag 0x28 = 1); at the
 * script's end it gives items 0x29 and 0x10 and sets counter 0x28. */
static uint32_t a04_825040(L16B *o, uint32_t self, uint32_t sp)
{
    const uint32_t area = sp - 0x40u, blk = self + 0x1F0;
    int32_t r = 0;
    uint64_t polygon[8];
    for (uint32_t k = 0; k < 8; ++k) polygon[k] = l16b_u64(o, 0x829530 + k * 8);
    for (uint32_t k = 0; k < 8; ++k) l16b_w64(o, area + k * 8, polygon[k]);
    switch (B(self + 5)) {
    case 0:
        C(001B1EA0, 0, 0x810350, area, 4, &r);
        if (r == 1 && L16B_LT(R(0x810354), F(25.0f))) {
            C(001BA1A0, blk, 0x829020);
            WB(self + 5, 1);
            WB(0x810780, 1);
        }
        break;
    case 1:
        C(001BA1F0, self, &r);
        if (r) {
            WB(self + 5, 0);
            W(self + 0x40, R(0x28A5FC));
            C(001C67E0, self, 0, 0, 0);
            WB(0x810800, 1);
            C(001C47A0, 0x29, 1);
            C(001C4760, 0x10, 1);
            C(001FAE70, 0);
        }
        C(001BA580, self, (int32_t)B(self + 0xD));
        C(001C64F0, self, F(0.5f), &r);
        WH(blk + 0xE, (uint32_t)r);
        return 0;
    default:
        break;
    }
    C(001BA580, self, (int32_t)B(self + 0xD));
    C(001C64F0, self, F(1.0f), &r);
    return 0;
}

/* AREA04 0x8251C0: the same node once counter 0x28 is 1: Use plays 0x8293A0. */
static uint32_t a04_8251C0(L16B *o, uint32_t self)
{
    const uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    switch (B(self + 5)) {
    case 0:
        if (B(self + 0xB) & 4) {
            C(001BA1A0, blk, 0x8293A0);
            WB(self + 5, 1);
        }
        break;
    case 1:
        C(001BA1F0, self, &r);
        if (r) {
            C(001C67E0, self, 0, F(20.0f), 0);
            WB(self + 0xB, 0);
            WB(self + 5, 0);
        }
        break;
    default:
        break;
    }
    C(001BA580, self, (int32_t)B(self + 0xD));
    C(001C64F0, self, F(1.0f), &r);
    return 0;
}

/* AREA04 0x824EF0 (sub 1's placement [1]). */
static uint32_t a04_824EF0(L16B *o, uint32_t self, uint32_t sp)
{
    int32_t r = 0;
    switch (B(self + 4)) {
    case 0:
        C(001BA1C0, self, 0x28, &r);
        if (r) {
            WB(self + 4, 3);
            break;
        }
        C(001B10B0, self, (int32_t)B(self + 0xD), 0x5B, &r);
        C(001C63E0, self, 0);
        C(001CA6F0, self, 2);
        C(001BA8E0, self, (int32_t)B(self + 0xD));
        W(self + 0x58, R(0x28A600));
        W(self + 0x30, 0x829520);
        WB(self + 4, 1);
        WB(self, 1);
        break;
    case 1:
        switch (B(0x810800)) {
        case 0: (void)a04_825040(o, self, sp - 0x20u); break;
        case 1: (void)a04_8251C0(o, self); break;
        default: break;
        }
        if (l16b_failed(o)) return 0;
        C(001C68C0, self);
        C(001B17A0, self, &r);
        WB(self + 1, 1);
        if (l16b_method(o, self)) return 0;
        break;
    case 2: case 3:
        C(001BA540, self);
        C(001AFC10, self);
        break;
    default:
        break;
    }
    return 0;
}

/* AREA04 0x825280: 1 outside the box x >= 552, z 235..279. */
static int32_t a04_825280(L16B *o)
{
    if (L16B_LT(R(0x810350), F(552.0f))) return 1;
    const uint32_t z = R(0x810358);
    if (L16B_LT(F(279.0f), z)) return 1;
    if (L16B_LT(z, F(235.0f))) return 1;
    return 0;
}

/* AREA04 0x825310 (sub 1's placement [3]): the three arrival scripts of flag
 * 0x31, one each by entry (bits of D_00810821). */
static uint32_t a04_825310(L16B *o, uint32_t self)
{
    const uint32_t blk = self + 0x1F0, seen = 0x810821, room = 0x810702;
    int32_t r = 0;
    switch (B(self + 4)) {
    case 0:
        WB(self + 4, 1);
        WB(self, 1);
        break;
    case 1:
        switch (B(self + 5)) {
        case 0:
            C(001BA1C0, self, 0x31, &r);
            if (r) {
                const uint32_t where = B(room);
                if (where == 0 && B(0x70003B8D) != 4 && !(B(seen) & 1)) {
                    C(001BA1A0, blk, 0x829570);
                    WB(self + 5, 1);
                    WB(seen, B(seen) | 1);
                    return 0;
                }
                if (where == 2 && B(0x70003B8D) != 4 && !(B(seen) & 2)) {
                    C(001BA1A0, blk, 0x829930);
                    WB(self + 5, 1);
                    WB(seen, B(seen) | 2);
                    return 0;
                }
                if (where == 7 && a04_825280(o) && !(B(seen) & 4)) {
                    C(001BA1A0, blk, 0x829CF0);
                    WB(self + 5, 1);
                    WB(seen, B(seen) | 4);
                    return 0;
                }
            }
            break;
        case 1:
            C(001BA1F0, self, &r);
            if (r) WB(self + 4, 3);
            break;
        default:
            break;
        }
        C(001B17A0, self, &r);
        break;
    case 2: case 3:
        C(001AFC10, self);
        break;
    default:
        break;
    }
    return 0;
}

int em_level16_boot_a04_00824EF0(const EmLevel16BootHooks *h, uint32_t a0, uint32_t sp, EmLevel16BootFault *fault)
{
    L16B o;
    if (l16b_begin(&o, h, fault)) return -1;
    (void)a04_824EF0(&o, a0, sp);
    return l16b_end(&o);
}

int em_level16_boot_a04_00825310(const EmLevel16BootHooks *h, uint32_t a0, EmLevel16BootFault *fault)
{
    L16B o;
    if (l16b_begin(&o, h, fault)) return -1;
    (void)a04_825310(&o, a0);
    return l16b_end(&o);
}

int em_level16_boot_a03_008235A0(const EmLevel16BootHooks *h, uint32_t a0, EmLevel16BootFault *fault)
{
    L16B o;
    if (l16b_begin(&o, h, fault)) return -1;
    (void)a03_8235A0(&o, a0);
    return l16b_end(&o);
}

int em_level16_boot_a03_00823810(const EmLevel16BootHooks *h, uint32_t a0, EmLevel16BootFault *fault)
{
    L16B o;
    if (l16b_begin(&o, h, fault)) return -1;
    (void)a03_823810(&o, a0);
    return l16b_end(&o);
}

int em_level16_boot_a03_00825200(const EmLevel16BootHooks *h, uint32_t a0, uint32_t sp, EmLevel16BootFault *fault)
{
    L16B o;
    if (l16b_begin(&o, h, fault)) return -1;
    (void)a03_825200(&o, a0, sp);
    return l16b_end(&o);
}

int em_level16_boot_001B3670(const EmLevel16BootHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t sp, int32_t *result, EmLevel16BootFault *fault)
{
    L16B o;
    if (!result || l16b_begin(&o, h, fault)) return -1;
    const int32_t r = e001B3670(&o, a0, a1, a2, sp);
    if (l16b_end(&o)) return -1;
    *result = r;
    return 0;
}

int em_level16_boot_0017C370(const EmLevel16BootHooks *h, uint32_t a0, EmLevel16BootFault *fault)
{
    L16B o;
    if (l16b_begin(&o, h, fault)) return -1;
    (void)e0017C370(&o, a0);
    return l16b_end(&o);
}

int em_level16_boot_0021BED0(const EmLevel16BootHooks *h, uint32_t a0, int32_t *result, EmLevel16BootFault *fault)
{
    L16B o;
    if (!result || l16b_begin(&o, h, fault)) return -1;
    const int32_t r = e0021BED0(&o, a0);
    if (l16b_end(&o)) return -1;
    *result = r;
    return 0;
}

int em_level16_boot_0021BF90(const EmLevel16BootHooks *h, uint32_t a0, uint32_t a1, EmLevel16BootFault *fault)
{
    L16B o;
    if (l16b_begin(&o, h, fault)) return -1;
    (void)e0021BF90(&o, a0, a1);
    return l16b_end(&o);
}

int em_level16_boot_0021C040(const EmLevel16BootHooks *h, uint32_t a0, uint32_t a1, EmLevel16BootFault *fault)
{
    L16B o;
    if (l16b_begin(&o, h, fault)) return -1;
    (void)e0021C040(&o, a0, a1);
    return l16b_end(&o);
}
