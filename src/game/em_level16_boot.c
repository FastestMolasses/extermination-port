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
#define C(fn,...) do { if (l16b_c_##fn(o, __VA_ARGS__)) return 0; } while (0)

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
    C(0017C370, p);
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

/* The seal (AREA03's [23]): the lock bit by the area byte, cut by any hit. */
static uint32_t e00158430(L16B *o, uint32_t self)
{
    const uint32_t bit = 0x810841u + B(0x810700);
    int32_t r = 0;
    switch (B(self + 4)) {
    case 0:
        if (B(bit) & ((1u << l16b_u16(o, self + 0x2E)) & 0xFF)) {
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
    case 1:
        if (SH(self + 0x36)) {
            WB(self, 2);
            WB(self + 4, 2);
            WB(0x810841u + B(0x810700), B(0x810841u + B(0x810700)) | (1u << B(self + 0x2E)));
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
            if (B(0x810781) == 0xFF) WB(self + 4, B(self + 4) + 1);
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
        W(v, R(0x8282C0)); W(v + 4, R(0x8282C4)); W(v + 8, R(0x8282C8)); W(v + 0xC, R(0x8282CC));
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
    for (uint32_t k = 0; k < 0x40; k += 4) W(area + k, R(0x829530 + k));
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
    if (L16B_LT(F(279.0f), R(0x810358))) return 1;
    if (L16B_LT(R(0x810358), F(235.0f))) return 1;
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
                if (B(room) == 0 && B(0x70003B8D) != 4 && !(B(seen) & 1)) {
                    C(001BA1A0, blk, 0x829570);
                    WB(self + 5, 1);
                    WB(seen, B(seen) | 1);
                    return 0;
                }
                if (B(room) == 2 && B(0x70003B8D) != 4 && !(B(seen) & 2)) {
                    C(001BA1A0, blk, 0x829930);
                    WB(self + 5, 1);
                    WB(seen, B(seen) | 2);
                    return 0;
                }
                if (B(room) == 7 && a04_825280(o) && !(B(seen) & 4)) {
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
