/* The fourteenth level: the AREA15 overlay functions the a19d / a15 census
 * found new (overlay id 12; runtime addresses, the overlay is linked 0x40
 * below where it runs): the overlay's init 0x823580, AREA15 sub 0's [0]
 * (0x8235A0 with its sub-handlers 0x8236B0 / 0x823780) and the swinging
 * objects 0x825320 / 0x825430 / 0x825D10, the placements that end at once
 * on this route (0x824070, 0x824510 -> 0x824560 / 0x8247B0, 0x824990,
 * 0x824E00 -> 0x824E50 / 0x825030), and AREA15 sub 1's placements (0x823850
 * with 0x8239F0 / 0x823E40, 0x8252D0, 0x826600, 0x826850).
 * docs/LEVEL14_PORT.md section 2. Ground truth: the original instructions
 * (the test runs every entry against them). */
#include "em_level14_port_internal.h"

#define F_ONE 0x3F800000u
#define F_ZERO 0x00000000u
#define F_20 0x41A00000u
#define F_300 0x43960000u
#define F_PI 0x40490FDBu
#define F_HALF_PI 0x3FC90FDBu

/* The common animation tail: 001C64F0(self, 1.0), 001B17A0(self),
 * 001C68C0(self), then the +0x4C method. */
static void a15_tail(L14 *o, uint32_t self)
{
    int32_t r = 0;
    if (l14_c_001C64F0(o, self, F_ONE, &r)) return;
    if (l14_c_001B17A0(o, self, &r)) return;
    if (l14_c_001C68C0(o, self)) return;
    l14_method(o, self);
}

/* 001BA1C0(self, bit) -> the story bit; 0 after a fault. */
static int32_t a15_bit(L14 *o, uint32_t self, int32_t bit)
{
    int32_t r = 0;
    if (l14_c_001BA1C0(o, self, bit, &r)) return 0;
    return r;
}

/* 001BA1F0(self) -> nonzero at the script's end; 0 after a fault. */
static int32_t a15_done(L14 *o, uint32_t self)
{
    int32_t r = 0;
    if (l14_c_001BA1F0(o, self, &r)) return 0;
    return r;
}

/* ------------------------------------------------------------------------
 * 0x823580: the overlay's init: D_00275C28 = 0x20, D_00275C2C = 0,
 * D_00275C24 = 0, D_00275C1C = 0x829F00. (The census row is 0x823540, the
 * 0x60 bytes from the overlay's entry pad to the end of this function; the
 * pad's words are not executed, the hit is this function.)
 * ---------------------------------------------------------------------- */
int em_level14_port_00823580(const H14 *h, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    l14_w32(&o, 0x00275C28u, 0x20u);
    l14_w32(&o, 0x00275C2Cu, 0);
    l14_w32(&o, 0x00275C24u, 0);
    l14_w32(&o, 0x00275C1Cu, 0x00829F00u);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8236B0 (self): [0]'s first scene. +5 0: script 0x826E70 on self +0x1F0,
 * +5 = 1, D_00810702 = 1. +5 1: at the script's end 001C4760(0xD, 1), +0x2E
 * = 0xFFFF, D_008107FA = 1, +5 = 0, 001FAE70(0). Then the animation tail.
 * ---------------------------------------------------------------------- */
static void a15_008236B0(L14 *o, uint32_t self)
{
    uint32_t sub = l14_u8(o, self + 5);
    if (sub == 0) {
        if (l14_c_001BA1A0(o, self + 0x1F0, 0x00826E70u)) return;
        l14_w8(o, self + 5, 1);
        l14_w8(o, 0x00810702u, 1);
    } else if (sub == 1) {
        if (a15_done(o, self)) {
            if (l14_c_001C4760(o, 0xD, 1)) return;
            l14_w16(o, self + 0x2E, 0xFFFF);
            l14_w8(o, 0x008107FAu, 1);
            l14_w8(o, self + 5, 0);
            if (l14_c_001FAE70(o, 0)) return;
        }
    }
    if (l14_failed(o)) return;
    a15_tail(o, self);
}

int em_level14_port_008236B0(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    a15_008236B0(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x823780 (self): [0] after its scene. +5 0: with +0xB bit 2 script
 * 0x827130, +5 = 1. +5 1: at the script's end +0xB = +5 = 0 and
 * 001C67E0(self, 0, 20.0, 0.0). Then the animation tail.
 * ---------------------------------------------------------------------- */
static void a15_00823780(L14 *o, uint32_t self)
{
    uint32_t sub = l14_u8(o, self + 5);
    if (sub == 0) {
        if (l14_u8(o, self + 0xB) & 4u) {
            if (l14_c_001BA1A0(o, self + 0x1F0, 0x00827130u)) return;
            l14_w8(o, self + 5, 1);
        }
    } else if (sub == 1) {
        if (a15_done(o, self)) {
            l14_w8(o, self + 0xB, 0);
            l14_w8(o, self + 5, 0);
            if (l14_c_001C67E0(o, self, 0, F_20, F_ZERO)) return;
        }
    }
    if (l14_failed(o)) return;
    a15_tail(o, self);
}

int em_level14_port_00823780(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    a15_00823780(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8235A0 (self): AREA15 sub 0's [0]. State 0: story bit 0x23 -> state 3;
 * else 001B10B0(self, +0xD, 0x65), 001C63E0(self, 0), 001CA6F0(self, 2),
 * +0 = 1, +0x30 = 0x8272B0, +0x58 = D_0028A6E8, state 1. State 1:
 * D_008107FA 0 -> 0x8236B0, 1 -> 0x823780. States 2 / 3: 001AFC10.
 * ---------------------------------------------------------------------- */
int em_level14_port_008235A0(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t st = l14_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 0) {
        if (a15_bit(&o, self, 0x23)) {
            l14_w8(&o, self + 4, 3);
            return l14_end(&o);
        }
        if (l14_failed(&o)) return -1;
        if (l14_c_001B10B0(&o, self, (int32_t)l14_u8(&o, self + 0xD), 0x65, &r)) return -1;
        if (l14_c_001C63E0(&o, self, 0)) return -1;
        if (l14_c_001CA6F0(&o, self, 2)) return -1;
        l14_w8(&o, self, 1);
        l14_w32(&o, self + 0x30, 0x008272B0u);
        l14_w32(&o, self + 0x58, l14_u32(&o, 0x0028A6E8u));
        l14_w8(&o, self + 4, 1);
    } else if (st == 1) {
        uint32_t k = l14_u8(&o, 0x008107FAu);
        if (k == 0) a15_008236B0(&o, self);
        else if (k == 1) a15_00823780(&o, self);
    } else if (st == 2 || st == 3) {
        l14_c_001AFC10(&o, self);
    }
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x823E40 (self): the +0x2A counter: +1; every 8th frame: past 240 +0x240 =
 * 2 + (rand >> 16) * 4 >> 15 and +0x2A = 0; then +0x240 counts down, or (at
 * 0) the point 300 along the +0x1C object's +0xC0 angles (00103230 into
 * 0x700038A0) plus its +0xB0 position, 001FBD50(self, 0x164, 0, 800.0) and
 * 001B0CD0(object, 0).
 * ---------------------------------------------------------------------- */
static void a15_00823E40(L14 *o, uint32_t self)
{
    uint32_t c0 = l14_u16(o, self + 0x2A);
    uint32_t obj = l14_u32(o, self + 0x1C);
    l14_w16(o, self + 0x2A, c0 + 1u);
    int32_t c = l14_s16(o, self + 0x2A);
    if (c % 8 != 0) return;
    if (c > 240) {
        int32_t r = 0;
        if (l14_c_00122BB8(o, &r)) return;
        l14_w8(o, self + 0x240, (uint32_t)((l14_sra((uint32_t)r, 16) * 4) >> 15) + 2u);
        l14_w16(o, self + 0x2A, 0);
    }
    uint32_t n = l14_u8(o, self + 0x240);
    if (n > 0) {
        l14_w8(o, self + 0x240, n - 1u);
        return;
    }
    if (l14_c_00103230(o, 0x700038A0u, obj + 0xC0, 0x43960000u)) return;
    l14_w32(o, 0x700038A0u, L14_ADD(l14_u32(o, 0x700038A0u), l14_u32(o, obj + 0xB0)));
    l14_w32(o, 0x700038A4u, L14_ADD(l14_u32(o, 0x700038A4u), l14_u32(o, obj + 0xB4)));
    l14_w32(o, 0x700038A8u, L14_ADD(l14_u32(o, 0x700038A8u), l14_u32(o, obj + 0xB8)));
    if (l14_c_001FBD50(o, self, 0x164, 0, 0x44480000u)) return;
    l14_c_001B0CD0(o, obj, 0);
}

int em_level14_port_00823E40(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    a15_00823E40(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8239F0 (self): sub 1's [?] (+0xD 0x54) before its scene (D_008107FB 0).
 * For +0xD 0x54: +5 0: with the player's y (D_00810354) not below 310 and
 * 001B1EA0(0, 0x810350, 0x827C80, 6) == 1: +5 = 1, D_0081077B = 1, script
 * 0x827400; +5 1: at the script's end 001FAE70(0), D_008107FB = 1, +0x2E =
 * 0xFFFF, +5 = 0, +0x28 = 900. Then, while D_70003B92 is 0, the animation
 * tail and 0x823E40.
 * ---------------------------------------------------------------------- */
static void a15_008239F0(L14 *o, uint32_t self)
{
    if (l14_u8(o, self + 0xD) == 0x54) {
        uint32_t sub = l14_u8(o, self + 5);
        if (sub == 0) {
            if (!L14_LT(l14_u32(o, 0x00810354u), 0x439B0000u)) {
                int32_t r = 0;
                if (l14_c_001B1EA0(o, 0, 0x00810350u, 0x00827C80u, 6, &r)) return;
                if (r == 1) {
                    l14_w8(o, self + 5, 1);
                    l14_w8(o, 0x0081077Bu, 1);
                    if (l14_c_001BA1A0(o, self + 0x1F0, 0x00827400u)) return;
                }
            }
        } else if (sub == 1) {
            if (a15_done(o, self)) {
                if (l14_c_001FAE70(o, 0)) return;
                l14_w8(o, 0x008107FBu, 1);
                l14_w16(o, self + 0x2E, 0xFFFF);
                l14_w8(o, self + 5, 0);
                l14_w16(o, self + 0x28, 900);
            }
        }
    }
    if (l14_failed(o)) return;
    if (l14_u8(o, 0x70003B92u) == 0) {
        a15_tail(o, self);
        if (l14_failed(o)) return;
        a15_00823E40(o, self);
    }
}

int em_level14_port_008239F0(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    a15_008239F0(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x823850 (self): sub 1's two placements of this behaviour (+0xD 0x54 /
 * 0x5D). State 0: story bit 0x23 set or 0x22 clear -> state 3; +0xD 0x54:
 * 001B10B0(self, 0x54, 0x56), 001C63E0(self, 8), D_008106C0 =
 * 001B6660(0x826CD0); +0xD 0x5D: 001B10B0(self, 0x5D, 0x5F),
 * 001C63E0(self, 0), +0x58 = D_0028A610; +0x28 = +0x2A = 0, state 1, +0 = 1.
 * State 1 by D_008107FB: 0 -> 0x8239F0, 1 -> 0x823B40, 2..4 -> 0x823C80.
 * States 2 / 3: 001AFC10.
 * ---------------------------------------------------------------------- */
int em_level14_port_00823850(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t st = l14_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 0) {
        if (a15_bit(&o, self, 0x23)) {
            l14_w8(&o, self + 4, 3);
            return l14_end(&o);
        }
        if (l14_failed(&o)) return -1;
        if (!a15_bit(&o, self, 0x22)) {
            if (l14_failed(&o)) return -1;
            l14_w8(&o, self + 4, 3);
            return l14_end(&o);
        }
        uint32_t id = l14_u8(&o, self + 0xD);
        if (id == 0x54) {
            uint32_t p = 0;
            if (l14_c_001B10B0(&o, self, (int32_t)id, 0x56, &r)) return -1;
            if (l14_c_001C63E0(&o, self, 8)) return -1;
            if (l14_c_001B6660(&o, 0x00826CD0u, &p)) return -1;
            l14_w32(&o, 0x008106C0u, p);
        }
        id = l14_u8(&o, self + 0xD);
        if (id == 0x5D) {
            if (l14_c_001B10B0(&o, self, (int32_t)id, 0x5F, &r)) return -1;
            if (l14_c_001C63E0(&o, self, 0)) return -1;
            l14_w32(&o, self + 0x58, l14_u32(&o, 0x0028A610u));
        }
        l14_w16(&o, self + 0x28, 0);
        l14_w16(&o, self + 0x2A, 0);
        l14_w8(&o, self + 4, 1);
        l14_w8(&o, self, 1);
    } else if (st == 1) {
        uint32_t k = l14_u8(&o, 0x008107FBu);
        if (k == 0) a15_008239F0(&o, self);
        else if (k == 1) l14_c_00823B40(&o, self);
        else if (k >= 2 && k <= 4) l14_c_00823C80(&o, self);
    } else if (st == 2 || st == 3) {
        l14_c_001AFC10(&o, self);
    }
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x824070 (self). State 0: state 3 unless story bit 0x23 is set and 0x28
 * and 0x29 are clear; +0xD 0x50: 001B10B0(self, 0x50, 0x52),
 * 001C63E0(self, 9), +0x58 = D_0028A5DC; +0xD 0x64: 001B10B0(self, 0x64,
 * 0x65), 001C63E0(self, 0), +0x58 = D_0028A6E8; 001BA8E0(self, +0xD),
 * state 1, +0 = 1, +0x30 = 0x8288F0. State 1 by D_008107FC: 0 -> 0x824240
 * (only for +0xD 0x64; otherwise return at once), 1 -> 0x824350 for +0xD
 * 0x50, other -> 0x8243E0; then 001C68C0, +1 = 1, the +0x4C method. States
 * 2 / 3: 001BA540, 001AFC10.
 * ---------------------------------------------------------------------- */
int em_level14_port_00824070(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t st = l14_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 0) {
        if (!a15_bit(&o, self, 0x23)) {
            if (l14_failed(&o)) return -1;
            l14_w8(&o, self + 4, 3);
            return l14_end(&o);
        }
        if (a15_bit(&o, self, 0x28) || (!l14_failed(&o) && a15_bit(&o, self, 0x29))) {
            l14_w8(&o, self + 4, 3);
            return l14_end(&o);
        }
        if (l14_failed(&o)) return -1;
        uint32_t id = l14_u8(&o, self + 0xD);
        if (id == 0x50) {
            if (l14_c_001B10B0(&o, self, (int32_t)id, 0x52, &r)) return -1;
            if (l14_c_001C63E0(&o, self, 9)) return -1;
            l14_w32(&o, self + 0x58, l14_u32(&o, 0x0028A5DCu));
        }
        id = l14_u8(&o, self + 0xD);
        if (id == 0x64) {
            if (l14_c_001B10B0(&o, self, (int32_t)id, 0x65, &r)) return -1;
            if (l14_c_001C63E0(&o, self, 0)) return -1;
            l14_w32(&o, self + 0x58, l14_u32(&o, 0x0028A6E8u));
        }
        if (l14_c_001BA8E0(&o, self, (int32_t)l14_u8(&o, self + 0xD))) return -1;
        l14_w8(&o, self + 4, 1);
        l14_w8(&o, self, 1);
        l14_w32(&o, self + 0x30, 0x008288F0u);
    } else if (st == 1) {
        uint32_t k = l14_u8(&o, 0x008107FCu);
        if (k == 0) {
            if (l14_u8(&o, self + 0xD) != 0x64) return l14_end(&o);
            if (l14_c_00824240(&o, self)) return -1;
        } else if (k == 1) {
            if (l14_u8(&o, self + 0xD) == 0x50 && l14_c_00824350(&o, self)) return -1;
        } else {
            if (l14_c_008243E0(&o, self)) return -1;
        }
        if (l14_failed(&o)) return -1;
        if (l14_c_001C68C0(&o, self)) return -1;
        l14_w8(&o, self + 1, 1);
        l14_method(&o, self);
    } else if (st == 2 || st == 3) {
        if (l14_c_001BA540(&o, self)) return -1;
        l14_c_001AFC10(&o, self);
    }
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * The tail shared by 0x824560 / 0x8247B0 in state 1: 001BA580(self, +0xD),
 * then the animation tail.
 * ---------------------------------------------------------------------- */
static void a15_tail2(L14 *o, uint32_t self)
{
    if (l14_c_001BA580(o, self, (int32_t)l14_u8(o, self + 0xD))) return;
    a15_tail(o, self);
}

/* State 0 of 0x824560 / 0x8247B0: story bit 0x25 set, or 0x28 and 0x29 both
 * clear -> state 3 (returns 1); 0 to go on, -1 after a fault. */
static int a15_gate25(L14 *o, uint32_t self)
{
    if (a15_bit(o, self, 0x25)) {
        l14_w8(o, self + 4, 3);
        return 1;
    }
    if (l14_failed(o)) return -1;
    if (!a15_bit(o, self, 0x28)) {
        if (l14_failed(o)) return -1;
        if (!a15_bit(o, self, 0x29)) {
            if (l14_failed(o)) return -1;
            l14_w8(o, self + 4, 3);
            return 1;
        }
    }
    return l14_failed(o) ? -1 : 0;
}

/* ------------------------------------------------------------------------
 * 0x824560 (self, +0xD 0x64). State 0 (a15_gate25): 001B10B0(self, +0xD,
 * 0x65), 001C63E0(self, 3), 001CA6F0(self, 2), 001BA8E0(self, +0xD), +0x58 =
 * D_0028A6E8, state 1, +0x30 = 0x828BA0, +0 = 1. State 1: +5 0: with +0xB
 * bit 2 script 0x828900, +5 = 1, +0x28 = 0; +5 1: +0x28 + 1, at 0x208 the
 * vectors 0x828B80 / 0x828B90 copied to D_008105D0 / D_008105E0 and
 * D_008101F0 / D_00810200 (00102948); at the script's end 001C67E0(self, 3,
 * 20.0, 0.0), +0xB = +5 = 0; then a15_tail2. States 2 / 3: 001BA540,
 * 001AFC10.
 * ---------------------------------------------------------------------- */
static void a15_00824560(L14 *o, uint32_t self)
{
    uint32_t st = l14_u8(o, self + 4);
    int32_t r = 0;
    if (st == 0) {
        if (a15_gate25(o, self)) return;
        if (l14_c_001B10B0(o, self, (int32_t)l14_u8(o, self + 0xD), 0x65, &r)) return;
        if (l14_c_001C63E0(o, self, 3)) return;
        if (l14_c_001CA6F0(o, self, 2)) return;
        if (l14_c_001BA8E0(o, self, (int32_t)l14_u8(o, self + 0xD))) return;
        l14_w32(o, self + 0x58, l14_u32(o, 0x0028A6E8u));
        l14_w8(o, self + 4, 1);
        l14_w32(o, self + 0x30, 0x00828BA0u);
        l14_w8(o, self, 1);
    } else if (st == 1) {
        uint32_t sub = l14_u8(o, self + 5);
        if (sub == 0) {
            if (l14_u8(o, self + 0xB) & 4u) {
                if (l14_c_001BA1A0(o, self + 0x1F0, 0x00828900u)) return;
                l14_w8(o, self + 5, 1);
                l14_w16(o, self + 0x28, 0);
            }
        } else if (sub == 1) {
            l14_w16(o, self + 0x28, l14_u16(o, self + 0x28) + 1u);
            if (l14_s16(o, self + 0x28) == 0x208) {
                if (l14_c_00102948(o, 0x008105D0u, 0x00828B80u)) return;
                if (l14_c_00102948(o, 0x008105E0u, 0x00828B90u)) return;
                if (l14_c_00102948(o, 0x008101F0u, 0x00828B80u)) return;
                if (l14_c_00102948(o, 0x00810200u, 0x00828B90u)) return;
            }
            if (a15_done(o, self)) {
                if (l14_c_001C67E0(o, self, 3, F_20, F_ZERO)) return;
                l14_w8(o, self + 0xB, 0);
                l14_w8(o, self + 5, 0);
            }
        }
        if (l14_failed(o)) return;
        a15_tail2(o, self);
    } else if (st == 2 || st == 3) {
        if (l14_c_001BA540(o, self)) return;
        l14_c_001AFC10(o, self);
    }
}

int em_level14_port_00824560(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    a15_00824560(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8247B0 (self, +0xD 0x50). State 0 (a15_gate25): 001B10B0(self, +0xD,
 * 0x52), 001C63E0(self, 4), +0x58 = D_0028A5DC, 001CA6F0(self, 2),
 * 001BA8E0(self, +0xD), +0x30 = 0x828BB0, state 1, +0 = 1. State 1: +5 0:
 * with +0xB bit 2 script 0x828A40, +5 = 1; +5 1: at the script's end
 * 001C67E0(self, 4, 20.0, 0.0), +0xB = +5 = 0; then a15_tail2. States 2 /
 * 3: 001BA540, 001AFC10.
 * ---------------------------------------------------------------------- */
static void a15_008247B0(L14 *o, uint32_t self)
{
    uint32_t st = l14_u8(o, self + 4);
    int32_t r = 0;
    if (st == 0) {
        if (a15_gate25(o, self)) return;
        if (l14_c_001B10B0(o, self, (int32_t)l14_u8(o, self + 0xD), 0x52, &r)) return;
        if (l14_c_001C63E0(o, self, 4)) return;
        l14_w32(o, self + 0x58, l14_u32(o, 0x0028A5DCu));
        if (l14_c_001CA6F0(o, self, 2)) return;
        if (l14_c_001BA8E0(o, self, (int32_t)l14_u8(o, self + 0xD))) return;
        l14_w32(o, self + 0x30, 0x00828BB0u);
        l14_w8(o, self + 4, 1);
        l14_w8(o, self, 1);
    } else if (st == 1) {
        uint32_t sub = l14_u8(o, self + 5);
        if (sub == 0) {
            if (l14_u8(o, self + 0xB) & 4u) {
                if (l14_c_001BA1A0(o, self + 0x1F0, 0x00828A40u)) return;
                l14_w8(o, self + 5, 1);
            }
        } else if (sub == 1) {
            if (a15_done(o, self)) {
                if (l14_c_001C67E0(o, self, 4, F_20, F_ZERO)) return;
                l14_w8(o, self + 0xB, 0);
                l14_w8(o, self + 5, 0);
            }
        }
        if (l14_failed(o)) return;
        a15_tail2(o, self);
    } else if (st == 2 || st == 3) {
        if (l14_c_001BA540(o, self)) return;
        l14_c_001AFC10(o, self);
    }
}

int em_level14_port_008247B0(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    a15_008247B0(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x824510 (self): +0xD 0x64 -> 0x824560; then +0xD 0x50 -> 0x8247B0.
 * ---------------------------------------------------------------------- */
int em_level14_port_00824510(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    if (l14_u8(&o, self + 0xD) == 0x64) a15_00824560(&o, self);
    if (l14_failed(&o)) return -1;
    if (l14_u8(&o, self + 0xD) == 0x50) a15_008247B0(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x824990 (self). State 0: story bit 0x30 clear -> state 3; +0xD 0x50:
 * 001B10B0(self, 0x50, 0x52), 001C63E0(self, 5), 001CA6F0(self, 2), +0x58 =
 * D_0028A5DC, +0x30 = 0x829260; +0xD 0x5A: 001B10B0(self, 0x5A, 0x5B),
 * 001C63E0(self, 2), 001CA6F0(self, 2), +0x58 = D_0028A600, +0x30 =
 * 0x829270; 001BA8E0(self, +0xD), state 1, +0 = 1. State 1: story bit 0x2A
 * -> state 3; else D_008107FF 0 -> 0x824B40, 1 -> 0x824C90. States 2 / 3:
 * 001BA540, 001AFC10.
 * ---------------------------------------------------------------------- */
int em_level14_port_00824990(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t st = l14_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 0) {
        if (!a15_bit(&o, self, 0x30)) {
            if (l14_failed(&o)) return -1;
            l14_w8(&o, self + 4, 3);
            return l14_end(&o);
        }
        uint32_t id = l14_u8(&o, self + 0xD);
        if (id == 0x50) {
            if (l14_c_001B10B0(&o, self, (int32_t)id, 0x52, &r)) return -1;
            if (l14_c_001C63E0(&o, self, 5)) return -1;
            if (l14_c_001CA6F0(&o, self, 2)) return -1;
            l14_w32(&o, self + 0x58, l14_u32(&o, 0x0028A5DCu));
            l14_w32(&o, self + 0x30, 0x00829260u);
        }
        id = l14_u8(&o, self + 0xD);
        if (id == 0x5A) {
            if (l14_c_001B10B0(&o, self, (int32_t)id, 0x5B, &r)) return -1;
            if (l14_c_001C63E0(&o, self, 2)) return -1;
            if (l14_c_001CA6F0(&o, self, 2)) return -1;
            l14_w32(&o, self + 0x58, l14_u32(&o, 0x0028A600u));
            l14_w32(&o, self + 0x30, 0x00829270u);
        }
        if (l14_c_001BA8E0(&o, self, (int32_t)l14_u8(&o, self + 0xD))) return -1;
        l14_w8(&o, self + 4, 1);
        l14_w8(&o, self, 1);
    } else if (st == 1) {
        if (a15_bit(&o, self, 0x2A)) {
            l14_w8(&o, self + 4, 3);
            return l14_end(&o);
        }
        if (l14_failed(&o)) return -1;
        uint32_t k = l14_u8(&o, 0x008107FFu);
        if (k == 0) l14_c_00824B40(&o, self);
        else if (k == 1) l14_c_00824C90(&o, self);
    } else if (st == 2 || st == 3) {
        if (l14_c_001BA540(&o, self)) return -1;
        l14_c_001AFC10(&o, self);
    }
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x824E50 (self, +0xD 0x59). State 0: story bit 0x2C set or 0x26 clear ->
 * state 3; else 001B10B0(self, +0xD, 0x5B), 001C63E0(self, 2),
 * 001CA6F0(self, 2), +0x58 = D_0028A600, state 1, +0 = 1. State 1: +5 0:
 * with D_00810702 1 and D_00810784 0 script 0x829330, +5 = 1; +5 1: at the
 * script's end 001C4760(0x15, 1), 001AEE10(4, 0), D_00810804 = 1, +0x2E =
 * 0xFFFF, +5 = 2, 001FAE70(0). Then while D_00810804 is set:
 * 001C64F0(1.0), 001B17A0, 001C68C0, +1 = 1, the +0x4C method. States 2 /
 * 3: 001AFC10.
 * ---------------------------------------------------------------------- */
static void a15_00824E50(L14 *o, uint32_t self)
{
    uint32_t st = l14_u8(o, self + 4);
    int32_t r = 0;
    if (st == 0) {
        if (a15_bit(o, self, 0x2C)) {
            l14_w8(o, self + 4, 3);
            return;
        }
        if (l14_failed(o)) return;
        if (!a15_bit(o, self, 0x26)) {
            if (l14_failed(o)) return;
            l14_w8(o, self + 4, 3);
            return;
        }
        if (l14_c_001B10B0(o, self, (int32_t)l14_u8(o, self + 0xD), 0x5B, &r)) return;
        if (l14_c_001C63E0(o, self, 2)) return;
        if (l14_c_001CA6F0(o, self, 2)) return;
        l14_w32(o, self + 0x58, l14_u32(o, 0x0028A600u));
        l14_w8(o, self + 4, 1);
        l14_w8(o, self, 1);
    } else if (st == 1) {
        uint32_t sub = l14_u8(o, self + 5);
        if (sub == 0) {
            if (l14_u8(o, 0x00810702u) == 1 && l14_u8(o, 0x00810784u) == 0) {
                if (l14_c_001BA1A0(o, self + 0x1F0, 0x00829330u)) return;
                l14_w8(o, self + 5, 1);
            }
        } else if (sub == 1) {
            if (a15_done(o, self)) {
                if (l14_c_001C4760(o, 0x15, 1)) return;
                if (l14_c_001AEE10(o, 4, 0)) return;
                l14_w8(o, 0x00810804u, 1);
                l14_w16(o, self + 0x2E, 0xFFFF);
                l14_w8(o, self + 5, 2);
                if (l14_c_001FAE70(o, 0)) return;
            }
        }
        if (l14_failed(o)) return;
        if (l14_u8(o, 0x00810804u) != 0) {
            if (l14_c_001C64F0(o, self, F_ONE, &r)) return;
            if (l14_c_001B17A0(o, self, &r)) return;
            if (l14_c_001C68C0(o, self)) return;
            l14_w8(o, self + 1, 1);
            l14_method(o, self);
        }
    } else if (st == 2 || st == 3) {
        l14_c_001AFC10(o, self);
    }
}

int em_level14_port_00824E50(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    a15_00824E50(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x825030 (self, +0xD 0x50). State 0: story bit 0x2C set or 0x26 clear ->
 * state 3; else 001B10B0(self, +0xD, 0x52), 001C63E0(self, 7 with
 * D_00810804 set, else 4), 001CA6F0(self, 2), +0 = 1, +0x30 = 0x8297F0,
 * +0x58 = D_0028A5DC, state 1. State 1: +5 0: with D_00810804 set the
 * position (975.5, 240, 902.5), +0xC4 = 0, 001C67E0(self, 7, 0, 0), +5 =
 * 1; then the animation tail only while D_00810784 is 0. +5 1: with +0xB bit
 * 2 script 0x8296F0, +5 = 2; the tail. +5 2: at the script's end
 * 001C67E0(self, 7, 20.0, 0.0), +0xB = 0, +5 = 1; the tail. States 2 / 3:
 * 001AFC10.
 * ---------------------------------------------------------------------- */
static void a15_00825030(L14 *o, uint32_t self)
{
    uint32_t st = l14_u8(o, self + 4);
    int32_t r = 0;
    if (st == 0) {
        if (a15_bit(o, self, 0x2C)) {
            l14_w8(o, self + 4, 3);
            return;
        }
        if (l14_failed(o)) return;
        if (!a15_bit(o, self, 0x26)) {
            if (l14_failed(o)) return;
            l14_w8(o, self + 4, 3);
            return;
        }
        if (l14_c_001B10B0(o, self, (int32_t)l14_u8(o, self + 0xD), 0x52, &r)) return;
        if (l14_c_001C63E0(o, self, l14_u8(o, 0x00810804u) != 0 ? 7 : 4)) return;
        if (l14_c_001CA6F0(o, self, 2)) return;
        l14_w8(o, self, 1);
        l14_w32(o, self + 0x30, 0x008297F0u);
        l14_w32(o, self + 0x58, l14_u32(o, 0x0028A5DCu));
        l14_w8(o, self + 4, 1);
    } else if (st == 1) {
        uint32_t sub = l14_u8(o, self + 5);
        if (sub == 0) {
            if (l14_u8(o, 0x00810804u) != 0) {
                l14_w32(o, self + 0xB0, 0x4473E000u);
                l14_w32(o, self + 0xB4, 0x43700000u);
                l14_w32(o, self + 0xB8, 0x4461A000u);
                l14_w32(o, self + 0xC4, F_ZERO);
                if (l14_c_001C67E0(o, self, 7, F_ZERO, F_ZERO)) return;
                l14_w8(o, self + 5, 1);
            }
            if (l14_u8(o, 0x00810784u) == 0) a15_tail(o, self);
        } else if (sub == 1) {
            if (l14_u8(o, self + 0xB) & 4u) {
                if (l14_c_001BA1A0(o, self + 0x1F0, 0x008296F0u)) return;
                l14_w8(o, self + 5, 2);
            }
            a15_tail(o, self);
        } else if (sub == 2) {
            if (a15_done(o, self)) {
                if (l14_c_001C67E0(o, self, 7, F_20, F_ZERO)) return;
                l14_w8(o, self + 0xB, 0);
                l14_w8(o, self + 5, 1);
            }
            if (l14_failed(o)) return;
            a15_tail(o, self);
        }
    } else if (st == 2 || st == 3) {
        l14_c_001AFC10(o, self);
    }
}

int em_level14_port_00825030(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    a15_00825030(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x824E00 (self): +0xD 0x59 -> 0x824E50; then +0xD 0x50 -> 0x825030.
 * ---------------------------------------------------------------------- */
int em_level14_port_00824E00(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    if (l14_u8(&o, self + 0xD) == 0x59) a15_00824E50(&o, self);
    if (l14_failed(&o)) return -1;
    if (l14_u8(&o, self + 0xD) == 0x50) a15_00825030(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8252D0 (self): +3 == 1 -> 001C5C90(self), else 001C4820(self).
 * ---------------------------------------------------------------------- */
int em_level14_port_008252D0(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    if (l14_u8(&o, self + 3) == 1) l14_c_001C5C90(&o, self);
    else l14_c_001C4820(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x825320 (self): a spinning object. State 0: 001B0FD0, 001C6380, +0x1F0 =
 * 0.01 + (-0.002 + 0.004 * (2^-31 * (float)rand)). State 1: +0xC4 =
 * 001B1470(+0xC4 + +0x1F0), 001C6380, 001A2370(self, self + 0xD0),
 * 001B17A0, the +0x4C method. States 2 / 3: 001AFC10.
 * ---------------------------------------------------------------------- */
int em_level14_port_00825320(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t st = l14_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 0) {
        if (l14_c_001B0FD0(&o, self, &r)) return -1;
        if (l14_c_001C6380(&o, self)) return -1;
        if (l14_c_00122BB8(&o, &r)) return -1;
        uint32_t v = L14_MUL(0x30000000u, L14_CVT_S_W(r));
        v = L14_MUL(0x3B83126Fu, v);
        v = L14_ADD(0xBB03126Fu, v);
        l14_w32(&o, self + 0x1F0, L14_ADD(0x3C23D70Au, v));
    } else if (st == 1) {
        uint32_t a = 0;
        if (l14_c_001B1470(&o, L14_ADD(l14_u32(&o, self + 0xC4), l14_u32(&o, self + 0x1F0)), &a)) return -1;
        l14_w32(&o, self + 0xC4, a);
        if (l14_c_001C6380(&o, self)) return -1;
        if (l14_c_001A2370(&o, self, self + 0xD0)) return -1;
        if (l14_c_001B17A0(&o, self, &r)) return -1;
        l14_method(&o, self);
    } else if (st == 2 || st == 3) {
        l14_c_001AFC10(&o, self);
    }
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x826600 (self): [6], sub 1's lit machine. State 0: +0x1F0 = -1,
 * 001B0FD0; with story bit 0x22 the +0x120 object's +0x84 -= 20;
 * 0019C6F0(0x22, 1), 001C6380. State 1: story bit 0x23 -> state 3; with
 * D_70003B92 0: when bit 0x22 is set the +0x114 object's +0x74 += 0.6108653
 * and the +0x118 object's +0x70 += 0.69813174 (each wrapped by 001B1470)
 * and 001FC3C0(self, self + 0x1F0, 0x44F, 600, 4096); the light point
 * (-2.575, 45.472, -102.748, 1) through the matrix self + 0xD0 (001026A0
 * into 0x700038B0), 001F5940(2, 0x700038B0, 0), 001C6380,
 * 001A2370(self, self + 0xD0), 001B1B70, the +0x4C method; with D_70003B92
 * set 001FC520(self + 0x1F0). States 2 / 3: 0019C6F0(0x22, 0),
 * 001FC520(self + 0x1F0), 001AFC10.
 * ---------------------------------------------------------------------- */
int em_level14_port_00826600(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t st = l14_u8(&o, self + 4);
    uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    if (st == 0) {
        l14_w32(&o, blk, 0xFFFFFFFFu);
        if (l14_c_001B0FD0(&o, self, &r)) return -1;
        if (a15_bit(&o, self, 0x22)) {
            uint32_t p = l14_u32(&o, self + 0x120);
            l14_w32(&o, p + 0x84, L14_SUB(l14_u32(&o, p + 0x84), F_20));
        }
        if (l14_failed(&o)) return -1;
        if (l14_c_0019C6F0(&o, 0x22, 1)) return -1;
        l14_c_001C6380(&o, self);
    } else if (st == 1) {
        if (a15_bit(&o, self, 0x23)) {
            l14_w8(&o, self + 4, 3);
            return l14_end(&o);
        }
        if (l14_failed(&o)) return -1;
        if (l14_u8(&o, 0x70003B92u) == 0) {
            if (a15_bit(&o, self, 0x22)) {
                uint32_t a = 0;
                uint32_t p = l14_u32(&o, self + 0x114);
                l14_w32(&o, p + 0x74, L14_ADD(l14_u32(&o, p + 0x74), 0x3F1C61ABu));
                p = l14_u32(&o, self + 0x114);
                if (l14_c_001B1470(&o, l14_u32(&o, p + 0x74), &a)) return -1;
                l14_w32(&o, l14_u32(&o, self + 0x114) + 0x74, a);
                p = l14_u32(&o, self + 0x118);
                l14_w32(&o, p + 0x70, L14_ADD(l14_u32(&o, p + 0x70), 0x3F32B8C3u));
                p = l14_u32(&o, self + 0x118);
                if (l14_c_001B1470(&o, l14_u32(&o, p + 0x70), &a)) return -1;
                l14_w32(&o, l14_u32(&o, self + 0x118) + 0x70, a);
                if (l14_c_001FC3C0(&o, self, blk, 0x44F, 0x44160000u, L14_CVT_S_W(4096))) return -1;
            }
            if (l14_failed(&o)) return -1;
            l14_w32(&o, 0x700038ACu, F_ONE);
            l14_w32(&o, 0x700038A0u, 0xC024CCCDu);
            l14_w32(&o, 0x700038A4u, 0x4235E354u);
            l14_w32(&o, 0x700038A8u, 0xC2CD7EFAu);
            if (l14_c_001026A0(&o, 0x700038B0u, self + 0xD0, 0x700038A0u)) return -1;
            if (l14_c_001F5940(&o, 2, 0x700038B0u, 0)) return -1;
            if (l14_c_001C6380(&o, self)) return -1;
            if (l14_c_001A2370(&o, self, self + 0xD0)) return -1;
            if (l14_c_001B1B70(&o, self)) return -1;
            l14_method(&o, self);
        } else {
            l14_c_001FC520(&o, blk);
        }
    } else if (st == 2 || st == 3) {
        if (l14_c_0019C6F0(&o, 0x22, 0)) return -1;
        if (l14_c_001FC520(&o, blk)) return -1;
        l14_c_001AFC10(&o, self);
    }
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x826850 (self): sub 1's three objects of this behaviour. State 0:
 * 001B0FD0, 001CA5F0(self, 0xC) for +0xD 0xD, 001C6380. State 1: +0xD 0xC:
 * D_008107FB >= 3 -> state 3, else 001C6380, 001B17A0, the +0x4C method;
 * other +0xD: with D_008107FB >= 3 001C6380, 001B1B70, the +0x4C method.
 * States 2 / 3: 001AFC10.
 * ---------------------------------------------------------------------- */
int em_level14_port_00826850(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t st = l14_u8(&o, self + 4);
    int32_t r = 0;
    if (st == 0) {
        if (l14_c_001B0FD0(&o, self, &r)) return -1;
        if (l14_u8(&o, self + 0xD) == 0xD && l14_c_001CA5F0(&o, self, 0xC)) return -1;
        if (l14_failed(&o)) return -1;
        l14_c_001C6380(&o, self);
    } else if (st == 1) {
        if (l14_u8(&o, self + 0xD) == 0xC) {
            if (l14_u8(&o, 0x008107FBu) >= 3) {
                l14_w8(&o, self + 4, 3);
            } else {
                if (l14_c_001C6380(&o, self)) return -1;
                if (l14_c_001B17A0(&o, self, &r)) return -1;
                l14_method(&o, self);
            }
        } else if (l14_u8(&o, 0x008107FBu) >= 3) {
            if (l14_c_001C6380(&o, self)) return -1;
            if (l14_c_001B1B70(&o, self)) return -1;
            l14_method(&o, self);
        }
    } else if (st == 2 || st == 3) {
        l14_c_001AFC10(&o, self);
    }
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x825430 / 0x825D10 (self): the two swinging gates of AREA15 sub 0 (the
 * second mirrored: x offset -(x - 869), z offset 966.5 - z, the pinned
 * angles and the two amplitude branches swapped). Bone 0 = *D_00275B40;
 * its +0x74 is the swing angle. Scratch 0x70003A20 / A24 / A2C / A3C.
 * State 0: 001B0FD0, 001C6380, +0x1F0 = 0, +0x1FC = 0, +0x200 = 0.08,
 * +0x204 = 0. State 1: the offsets from the player (D_00810350 / 358);
 * inside |x| < 7.7 (0011DF78), -2 < z < 9.5: z below 0 pins the angle to
 * +-1.3788102 by the side +0x1F0 and |x| < 2 (phase pi/2, amplitude =
 * angle); else (side 0 takes the player's D_00810374 sign and negates the
 * amplitude) the push: sound 0x404 once when |angle| > 0.5235988, the
 * phase += step wrapped at +-pi, the target t = 2.0799727 * (9.5 - z) *
 * (x + 4 clamped at 0) (or x - 4 clamped, by side) clamped to 79 degrees,
 * in radians; beyond the amplitude it eases (0.01) toward it, or follows
 * amplitude * sin(phase) when that is further; within it the amplitude
 * becomes t (phase pi/2); the angle = t. Outside: side 0, while the
 * amplitude is above 0.02 the free swing (phase += step; sound 0x403 at
 * each zero crossing and at the +-pi wrap, at 300 * amplitude), angle =
 * amplitude * sin(phase), amplitude *= 0.985; else angle = amplitude = 0;
 * +0x204 = 0. Then 001C6380, 001A2370(self, bone 0 + 0x90), +1 =
 * 001B1630(+0xB0, +0xB4, +0xB8), with +1 set and |angle| > 1.3613569
 * 001B1B70, the +0x4C method. States 2 / 3: 001AFC10.
 * ---------------------------------------------------------------------- */
#define A20 0x70003A20u
#define A24 0x70003A24u
#define A2C 0x70003A2Cu
#define A3C 0x70003A3Cu

static uint32_t a15_bone0(L14 *o) { return l14_u32(o, l14_u32(o, 0x00275B40u)); }

/* bone 0 +0x74 = v (the pointer chain read before the store). */
static void a15_angle(L14 *o, uint32_t v)
{
    uint32_t b0 = a15_bone0(o);
    l14_w32(o, b0 + 0x74, v);
}

/* The push toward the target t with the amplitude p1 (self +0x1F4) and
 * phase p3 (+0x1FC): positive (pos != 0: x + 4 clamped below at 0, t in
 * [0, 79] degrees) or negative (x - 4 clamped above at 0, t in [-79, 0]).
 * The target lives in 0x70003A2C, re-read where the original re-reads it. */
static void a15_push(L14 *o, uint32_t self, int pos)
{
    uint32_t p1 = self + 0x1F4, p3 = self + 0x1FC;
    uint32_t r = pos ? L14_ADD(0x40800000u, l14_u32(o, A20)) : L14_SUB(l14_u32(o, A20), 0x40800000u);
    l14_w32(o, A2C, r);
    if (pos ? L14_LT(r, F_ZERO) : L14_LT(F_ZERO, r)) l14_w32(o, A2C, F_ZERO);
    uint32_t zz = l14_u32(o, A24);
    r = L14_MUL(0x40051E46u, L14_MUL(L14_SUB(0x41180000u, zz), l14_u32(o, A2C)));
    l14_w32(o, A2C, r);
    if (pos ? L14_LT(0x429E0000u, r) : L14_LT(r, 0xC29E0000u)) l14_w32(o, A2C, pos ? 0x429E0000u : 0xC29E0000u);
    if (pos ? L14_LT(l14_u32(o, A2C), F_ZERO) : L14_LT(F_ZERO, l14_u32(o, A2C))) l14_w32(o, A2C, F_ZERO);
    l14_w32(o, A2C, L14_DIV(L14_MUL(F_PI, l14_u32(o, A2C)), 0x43340000u));
    uint32_t amp = l14_u32(o, p1);
    uint32_t t = l14_u32(o, A2C);
    if (pos ? L14_LT(t, amp) : L14_LT(amp, t)) {
        uint32_t sn = 0;
        if (l14_c_0011E2A8(o, l14_u32(o, p3), &sn)) return;
        uint32_t a = L14_MUL(l14_u32(o, p1), sn);
        uint32_t b = l14_u32(o, A2C);
        l14_w32(o, A24, a);
        if (pos ? L14_LT(b, a) : L14_LT(a, b)) {
            l14_w32(o, A2C, a);
            l14_w32(o, p1, a);
        } else {
            uint32_t v = l14_u32(o, p1);
            l14_w32(o, p1, pos ? L14_SUB(v, L14_MUL(0x3C23D70Au, L14_SUB(v, b)))
                               : L14_ADD(v, L14_MUL(0x3C23D70Au, L14_SUB(b, v))));
        }
    } else {
        l14_w32(o, p1, t);
        l14_w32(o, p3, F_HALF_PI);
    }
}

static void a15_swing(L14 *o, uint32_t self, int mirror)
{
    uint32_t st = l14_u8(o, self + 4);
    int32_t r = 0;
    if (st == 0) {
        if (l14_c_001B0FD0(o, self, &r)) return;
        if (l14_c_001C6380(o, self)) return;
        l14_w32(o, self + 0x1F0, 0);
        l14_w32(o, self + 0x1FC, F_ZERO);
        l14_w32(o, self + 0x200, 0x3DA3D70Au);
        l14_w32(o, self + 0x204, F_ZERO);
        return;
    }
    if (st == 2 || st == 3) {
        l14_c_001AFC10(o, self);
        return;
    }
    if (st != 1) return;
    uint32_t p1 = self + 0x1F4, p3 = self + 0x1FC, abs = 0, z = 0;
    uint32_t px = l14_u32(o, 0x00810350u), pz = l14_u32(o, 0x00810358u);
    l14_w32(o, A20, mirror ? L14_NEG(L14_SUB(px, 0x44594000u)) : L14_SUB(px, 0x44594000u));
    px = l14_u32(o, A20);
    l14_w32(o, A24, mirror ? L14_SUB(0x4471A000u, pz) : L14_SUB(pz, 0x446DE000u));
    if (l14_c_0011DF78(o, px, &abs)) return;
    int inside = 0;
    if (L14_LT(abs, 0x40F66666u)) {
        z = l14_u32(o, A24);
        inside = L14_LT(0xC0000000u, z) && L14_LT(z, 0x41180000u);
    }
    if (inside && L14_LT(z, F_ZERO)) {
        /* the pinned angle */
        int32_t side = l14_s32(o, self + 0x1F0);
        if (side > 0) {
            if (mirror ? L14_LT(l14_u32(o, A20), 0x40000000u) : L14_LT(0xC0000000u, l14_u32(o, A20)))
                a15_angle(o, mirror ? 0xBFB07CDAu : 0x3FB07CDAu);
        } else if (side < 0) {
            if (mirror ? L14_LT(0xC0000000u, l14_u32(o, A20)) : L14_LT(l14_u32(o, A20), 0x40000000u))
                a15_angle(o, mirror ? 0x3FB07CDAu : 0xBFB07CDAu);
        }
        l14_w32(o, p3, F_HALF_PI);
        l14_w32(o, p1, l14_u32(o, a15_bone0(o) + 0x74));
    } else if (inside) {
        /* the push */
        if (l14_s32(o, self + 0x1F0) == 0) {
            l14_w32(o, p1, L14_NEG(l14_u32(o, p1)));
            l14_w32(o, self + 0x1F0, L14_LT(F_ZERO, l14_u32(o, 0x00810374u)) ? 1u : 0xFFFFFFFFu);
        }
        if (L14_EQ(l14_u32(o, self + 0x204), F_ZERO)) {
            uint32_t m = 0;
            if (l14_c_0011DF78(o, l14_u32(o, a15_bone0(o) + 0x74), &m)) return;
            if (L14_LT(0x3F060A92u, m)) {
                if (l14_c_001FBD50(o, self, 0x404, 0, F_300)) return;
                l14_w32(o, self + 0x204, F_ONE);
            }
        }
        uint32_t step = l14_u32(o, self + 0x200);
        uint32_t v = L14_ADD(l14_u32(o, p3), step);
        l14_w32(o, p3, v);
        if (L14_LT(F_PI, v)) l14_w32(o, p3, 0xC0490FDBu);
        else if (L14_LT(v, 0xC0490FDBu)) l14_w32(o, p3, F_PI);
        int pos = l14_s32(o, self + 0x1F0) >= 0;
        a15_push(o, self, mirror ? !pos : pos);
        if (l14_failed(o)) return;
        uint32_t bp = l14_u32(o, 0x00275B40u);
        uint32_t t = l14_u32(o, A2C);
        l14_w32(o, l14_u32(o, bp) + 0x74, t);
    } else {
        /* the free swing */
        l14_w32(o, self + 0x1F0, 0);
        uint32_t m = 0;
        if (l14_c_0011DF78(o, l14_u32(o, p1), &m)) return;
        if (L14_LT(0x3CA3D70Au, m)) {
            l14_w32(o, A3C, l14_u32(o, p3));
            uint32_t step = l14_u32(o, self + 0x200);
            uint32_t v = L14_ADD(l14_u32(o, p3), step);
            l14_w32(o, p3, v);
            if (L14_LT(v, F_ZERO) && L14_LT(F_ZERO, l14_u32(o, A3C))) {
                if (l14_c_001FBD50(o, self, 0x403, 0, L14_MUL(F_300, l14_u32(o, p1)))) return;
            }
            if (L14_LT(F_ZERO, l14_u32(o, p3)) && L14_LT(l14_u32(o, A3C), F_ZERO)) {
                if (l14_c_001FBD50(o, self, 0x403, 0, L14_MUL(F_300, l14_u32(o, p1)))) return;
            }
            uint32_t w = l14_u32(o, p3);
            if (L14_LT(F_PI, w)) {
                l14_w32(o, p3, 0xC0490FDBu);
                if (l14_c_001FBD50(o, self, 0x403, 0, L14_MUL(F_300, l14_u32(o, p1)))) return;
            } else if (L14_LT(w, 0xC0490FDBu)) {
                l14_w32(o, p3, F_PI);
                if (l14_c_001FBD50(o, self, 0x403, 0, L14_MUL(F_300, l14_u32(o, p1)))) return;
            }
            uint32_t sn = 0;
            if (l14_c_0011E2A8(o, l14_u32(o, p3), &sn)) return;
            uint32_t a = L14_MUL(l14_u32(o, p1), sn);
            uint32_t bp = l14_u32(o, 0x00275B40u);
            l14_w32(o, A24, a);
            l14_w32(o, l14_u32(o, bp) + 0x74, a);
            l14_w32(o, p1, L14_MUL(l14_u32(o, p1), 0x3F7C28F6u));
        } else {
            a15_angle(o, F_ZERO);
            l14_w32(o, p1, F_ZERO);
            l14_w32(o, self + 0x1F0, 0);
        }
        l14_w32(o, self + 0x204, F_ZERO);
    }
    if (l14_failed(o)) return;
    if (l14_c_001C6380(o, self)) return;
    if (l14_c_001A2370(o, self, a15_bone0(o) + 0x90)) return;
    uint32_t y = l14_u32(o, self + 0xB4), zp = l14_u32(o, self + 0xB8);
    if (l14_c_001B1630(o, l14_u32(o, self + 0xB0), y, zp, &r)) return;
    l14_w8(o, self + 1, (uint32_t)r);
    if (l14_u8(o, self + 1) != 0) {
        uint32_t a = l14_u32(o, a15_bone0(o) + 0x74);
        if (L14_LT(a, 0xBFAE40F1u) || L14_LT(0x3FAE40F1u, a)) {
            if (l14_c_001B1B70(o, self)) return;
        }
    }
    l14_method(o, self);
}

int em_level14_port_00825430(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    a15_swing(&o, self, 0);
    return l14_end(&o);
}

int em_level14_port_00825D10(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    a15_swing(&o, self, 1);
    return l14_end(&o);
}
