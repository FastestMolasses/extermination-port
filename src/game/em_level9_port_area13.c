/* AREA13 overlay functions of the ninth level (see em_level9_port.h,
 * docs/LEVEL9_PORT.md): runtime 0x823700, 0x823940, 0x823A10, 0x823BC0,
 * 0x823C10, 0x823D50, 0x823E90, 0x823FE0, 0x824180, 0x8266A0, 0x826850,
 * 0x826FB0, 0x826FC0, 0x826FF0, 0x827150, 0x8292A0, 0x8293A0, 0x8299E0 and
 * 0x829AA0 (OVERLAY/AREA13.BIN, id 10; the decomp's splat/link names are
 * 0x40 lower, e.g. 0x823700 is func_overlay_AREA13_008236C0).
 *
 * Ground truth: the original instructions. The decomp's C of these
 * functions is byte-identical (lane A13C) except 0x829AA0 (NEARMISS), and
 * was used as the guide; the order of loads and stores between calls, the
 * float operand order and every branch follow the instructions (the test
 * compares them access for access).
 */
#include "em_level9_port_internal.h"

#define D_008107F1 0x008107F1u
#define D_0028A61C 0x0028A61Cu
#define D_0028A59C 0x0028A59Cu
#define D_0028A6EC 0x0028A6ECu
#define D_0024D7C0 0x0024D7C0u /* placement record tables by area / sub */
#define D_002759B8 0x002759B8u
#define D_002759C0 0x002759C0u
#define D_00275CA8 0x00275CA8u /* pointer: 0x827150's block (self +0x1F0) */
#define D_007635C0 0x007635C0u
#define D_008106B0 0x008106B0u
#define D_008106B1 0x008106B1u
#define D_008106D0 0x008106D0u

static int32_t sound(L9 *o, uint32_t self, int32_t id, uint32_t range)
{
    int32_t r = 0;
    l9_c_001FBD50(o, self, id, 0, range, &r);
    return r;
}

/* ------------------------------------------------------------------------
 * 0x823700 ([3], the lobby's scene owner). By +4:
 *   0: 001B10B0(self, +0x0D, 0x62); 001C63E0(self, 0); 001CA6F0(self, 2);
 *      001BA8E0(self, +0x0D); +0x30 = 0x82A760; +0x58 = D_0028A61C; +4 = 1;
 *      +0 = 1.
 *   1: 001BA1C0(self, 0x1A) nonzero: +4 = 3. Else by D_008107F1 (counter
 *      0x19): 0 -> 0x823830(self), 1 -> 0x823940(self), others nothing.
 *   2, 3: 001BA540(self), 001AFC10(self). Others: nothing.
 * ---------------------------------------------------------------------- */
int em_level9_port_00823700(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (l9_begin(&o, h, fault)) return -1;
    switch (l9_u8(&o, self + 4)) {
    case 0:
        if (l9_c_001B10B0(&o, self, (int32_t)l9_u8(&o, self + 0x0D), 0x62, &r)) break;
        if (l9_c_001C63E0(&o, self, 0)) break;
        if (l9_c_001CA6F0(&o, self, 2)) break;
        if (l9_c_001BA8E0(&o, self, (int32_t)l9_u8(&o, self + 0x0D))) break;
        l9_w32(&o, self + 0x30, 0x0082A760u);
        l9_w32(&o, self + 0x58, l9_u32(&o, D_0028A61C));
        l9_w8(&o, self + 4, 1);
        l9_w8(&o, self + 0, 1);
        break;
    case 1:
        if (l9_c_001BA1C0(&o, self, 0x1A, &r)) break;
        if (r != 0) {
            l9_w8(&o, self + 4, 3);
            break;
        }
        switch (l9_u8(&o, D_008107F1)) {
        case 0: l9_c_00823830(&o, self); break;
        case 1: l9_00823940(&o, self); break;
        default: break;
        }
        break;
    case 2:
    case 3:
        if (l9_c_001BA540(&o, self)) break;
        l9_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x823940 ([3] once counter 0x19 is 1). blk = self +0x1F0. By +5:
 *   0: with +0x0B bit 2 (the Use): 001BA1A0(blk, 0x82A620), +5 = 1.
 *   1: 001BA1F0(self) nonzero: +0x0B = 0, +5 = 0, 001C67E0(self, 0, 20, 0).
 * Then 001BA580(self, +0x0D), 001C64F0(self, 1.0), 001B17A0(self),
 * 001C68C0(self) and the +0x4C method.
 * ---------------------------------------------------------------------- */
void l9_00823940(L9 *o, uint32_t self)
{
    int32_t r = 0;
    uint32_t sub = l9_u8(o, self + 5);
    if (sub == 1) {
        if (l9_c_001BA1F0(o, self, &r)) return;
        if (r != 0) {
            l9_w8(o, self + 0x0B, 0);
            l9_w8(o, self + 5, 0);
            if (l9_c_001C67E0(o, self, 0, F_20, F_ZERO)) return;
        }
    } else if (sub == 0) {
        if (l9_u8(o, self + 0x0B) & 4u) {
            if (l9_c_001BA1A0(o, self + 0x1F0, 0x0082A620u)) return;
            l9_w8(o, self + 5, 1);
        }
    }
    if (l9_c_001BA580(o, self, (int32_t)l9_u8(o, self + 0x0D))) return;
    if (l9_c_001C64F0(o, self, F_ONE, &r)) return;
    if (l9_c_001B17A0(o, self, &r)) return;
    if (l9_c_001C68C0(o, self)) return;
    l9_method(o, self);
}

int em_level9_port_00823940(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    l9_00823940(&o, self);
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x823A10 (an op09 record of [4]'s script 0x82A770): 001C47A0(0x1A, 1)
 * (item 0x1A); returns 1.
 * ---------------------------------------------------------------------- */
int em_level9_port_00823A10(const EmLevel9PortHooks *h, int32_t *result, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (!result || l9_begin(&o, h, fault)) return -1;
    if (l9_c_001C47A0(&o, 0x1A, 1, &r)) return -1;
    *result = 1;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x823C10 / 0x823D50 (the holes [5] / [6]; the same code with the Use
 * descriptor 0x82B070 / 0x82B080 and the script 0x82AB70 / 0x82ADF0).
 * blk = self +0x1F0. By +4:
 *   0: 001BA1C0(self, 0x1B) nonzero: +4 = 3. Else +0x30 = descriptor,
 *      +0 = 1, +4 = 1.
 *   1: D_00810C8B (item 0x27) nonzero: +4 = 3. Else by +5: 0 with +0x0B
 *      bit 2: 001BA1A0(blk, script), +5 = 1; 1 when 001BA1F0(self) is
 *      nonzero: 001AEE10(4, 0), +0x0B = 0, +5 = 0. Then 001B17A0(self).
 *   2, 3: 001AFC10(self). Others: nothing.
 * ---------------------------------------------------------------------- */
static void hole(L9 *o, uint32_t self, uint32_t descriptor, uint32_t script)
{
    int32_t r = 0;
    switch (l9_u8(o, self + 4)) {
    case 0:
        if (l9_c_001BA1C0(o, self, 0x1B, &r)) return;
        if (r != 0) {
            l9_w8(o, self + 4, 3);
            return;
        }
        l9_w32(o, self + 0x30, descriptor);
        l9_w8(o, self + 0, 1);
        l9_w8(o, self + 4, 1);
        return;
    case 1:
        if (l9_u8(o, D_00810C8B) != 0) {
            l9_w8(o, self + 4, 3);
            return;
        }
        switch (l9_u8(o, self + 5)) {
        case 0:
            if (l9_u8(o, self + 0x0B) & 4u) {
                if (l9_c_001BA1A0(o, self + 0x1F0, script)) return;
                l9_w8(o, self + 5, 1);
            }
            break;
        case 1:
            if (l9_c_001BA1F0(o, self, &r)) return;
            if (r != 0) {
                if (l9_c_001AEE10(o, 4, 0)) return;
                l9_w8(o, self + 0x0B, 0);
                l9_w8(o, self + 5, 0);
            }
            break;
        default:
            break;
        }
        l9_c_001B17A0(o, self, &r);
        return;
    case 2:
    case 3:
        l9_c_001AFC10(o, self);
        return;
    default:
        return;
    }
}

void l9_00823C10(L9 *o, uint32_t self) { hole(o, self, 0x0082B070u, 0x0082AB70u); }
void l9_00823D50(L9 *o, uint32_t self) { hole(o, self, 0x0082B080u, 0x0082ADF0u); }

int em_level9_port_00823C10(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    l9_00823C10(&o, self);
    return l9_end(&o);
}

int em_level9_port_00823D50(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    l9_00823D50(&o, self);
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x823BC0 (the holes' behaviour): +0x0D 0 -> 0x823C10(self), 1 ->
 * 0x823D50(self); others nothing.
 * ---------------------------------------------------------------------- */
int em_level9_port_00823BC0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    switch (l9_u8(&o, self + 0x0D)) {
    case 0: l9_00823C10(&o, self); break;
    case 1: l9_00823D50(&o, self); break;
    default: break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x823E90 ([44], the machine). The step D_008107F4 & 0xF is read first.
 * By +4:
 *   0: 001BA1C0(self, 0x1C) nonzero: +4 = 3. Else 001B0FD0(self), +0x30 =
 *      0x2759B8, +8 = 3, +4 = 1, +0 = 1, +0x0A = 0, +0x34 (halfword) = 4.
 *   1: by the step: 0 -> 0x824160, 1 -> 0x824180, 2 -> 0x824390, 3 ->
 *      0x824520, 4 -> 0x824960 (each (self)); others nothing.
 *   2, 3: 001AFC10(self). Others: nothing.
 * ---------------------------------------------------------------------- */
int em_level9_port_00823E90(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (l9_begin(&o, h, fault)) return -1;
    uint32_t step = l9_u8(&o, D_008107F4) & 0xFu;
    switch (l9_u8(&o, self + 4)) {
    case 0:
        if (l9_c_001BA1C0(&o, self, 0x1C, &r)) break;
        if (r != 0) {
            l9_w8(&o, self + 4, 3);
            break;
        }
        if (l9_c_001B0FD0(&o, self, &r)) break;
        l9_w32(&o, self + 0x30, D_002759B8);
        l9_w8(&o, self + 8, 3);
        l9_w8(&o, self + 4, 1);
        l9_w8(&o, self + 0, 1);
        l9_w8(&o, self + 0x0A, 0);
        l9_w16(&o, self + 0x34, 4);
        break;
    case 1:
        switch (step) {
        case 0: l9_c_00824160(&o, self); break;
        case 1: l9_00824180(&o, self); break;
        case 2: l9_c_00824390(&o, self); break;
        case 3: l9_c_00824520(&o, self); break;
        case 4: l9_c_00824960(&o, self); break;
        default: break;
        }
        break;
    case 2:
    case 3:
        l9_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x823FE0 (called by boot 00195130 at 0x195D18, in its area-0xD case
 * while D_00810702 >= 8; the port's em_camera_area11_specials.c calls it
 * through its hook w_00823FE0): 1 when D_00810774 (flag 0x1C) is not 0xFF,
 * 001B1EA0(0, 0x810350, 0x82E1C0, 4) is nonzero and the player's y
 * (D_00810354) is above 210 (not <= 210); else 0.
 * ---------------------------------------------------------------------- */
int em_level9_port_00823FE0(const EmLevel9PortHooks *h, int32_t *result, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0, v = 0;
    if (!result || l9_begin(&o, h, fault)) return -1;
    if (l9_u8(&o, D_00810774) != 0xFFu) {
        if (l9_c_001B1EA0(&o, 0, D_00810350, 0x0082E1C0u, 4, &r)) return -1;
        if (r != 0 && !L9_LE(l9_u32(&o, 0x00810354u), 0x43520000u /* 210 */)) v = 1;
    }
    if (l9_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x824180 ([44]'s step 1). blk = self +0x1F0. By +5 (a jump table at
 * 0x82E200, 7 entries):
 *   0: b = +0x0B; with bit 2: with bit 0 +5 += 1, +0x0A = 1, +0 = 2; else
 *      001BA1A0(blk, 0x82B090), +5 = 2.
 *   1: 001BA1A0(blk, 0x82B2D0); +0x0A == 0: +5 = 3; else +5 = 4 and
 *      D_00810774 = 1.
 *   2: 001BA1F0(self) nonzero: D_008106B1 = +0x34 + 0x80, D_008106B0 = 1,
 *      D_008106D0 = +0x14, +0x0A = 0, +0x0B = 0, +0 = 1, +5 = 1.
 *   3: 001BA1F0(self) nonzero: +0x0B = 0, +0 = 1, +5 = 0.
 *   4: 001BA1F0(self) nonzero: +5 += 1.
 *   5: D_008107F4 += 1, +5 = 0.
 *   6 and above: nothing.
 * Then 0x700038A0 = (-1.333, 1.9, -2.6, 1.0) (stored w, x, y, z),
 * 001026A0(0x700038B0, self +0xD0, 0x700038A0), 001F5940(4, 0x700038B0,
 * 0), 001C6380(self), 001B17A0(self) and the +0x4C method.
 * ---------------------------------------------------------------------- */
void l9_00824180(L9 *o, uint32_t self)
{
    int32_t r = 0;
    uint32_t blk = self + 0x1F0;
    switch (l9_u8(o, self + 5)) {
    case 0: {
        uint32_t b = l9_u8(o, self + 0x0B);
        if (b & 4u) {
            if (b & 1u) {
                l9_w8(o, self + 5, l9_u8(o, self + 5) + 1u);
                l9_w8(o, self + 0x0A, 1);
                l9_w8(o, self + 0, 2);
            } else {
                if (l9_c_001BA1A0(o, blk, 0x0082B090u)) return;
                l9_w8(o, self + 5, 2);
            }
        }
        break;
    }
    case 1:
        if (l9_c_001BA1A0(o, blk, 0x0082B2D0u)) return;
        if (l9_u8(o, self + 0x0A) == 0) {
            l9_w8(o, self + 5, 3);
        } else {
            l9_w8(o, self + 5, 4);
            l9_w8(o, D_00810774, 1);
        }
        break;
    case 2:
        if (l9_c_001BA1F0(o, self, &r)) return;
        if (r != 0) {
            l9_w8(o, D_008106B1, l9_u8(o, self + 0x34) + 0x80u);
            l9_w8(o, D_008106B0, 1);
            l9_w32(o, D_008106D0, l9_u32(o, self + 0x14));
            l9_w8(o, self + 0x0A, 0);
            l9_w8(o, self + 0x0B, 0);
            l9_w8(o, self + 0, 1);
            l9_w8(o, self + 5, 1);
        }
        break;
    case 3:
        if (l9_c_001BA1F0(o, self, &r)) return;
        if (r != 0) {
            l9_w8(o, self + 0x0B, 0);
            l9_w8(o, self + 0, 1);
            l9_w8(o, self + 5, 0);
        }
        break;
    case 4:
        if (l9_c_001BA1F0(o, self, &r)) return;
        if (r != 0) l9_w8(o, self + 5, l9_u8(o, self + 5) + 1u);
        break;
    case 5:
        l9_w8(o, D_008107F4, l9_u8(o, D_008107F4) + 1u);
        l9_w8(o, self + 5, 0);
        break;
    default:
        break;
    }
    l9_w32(o, S_700038AC, F_ONE);
    l9_w32(o, S_700038A0, 0xBFAA9FBEu /* -1.333 */);
    l9_w32(o, S_700038A4, 0x3FF33333u /* 1.9 */);
    l9_w32(o, S_700038A8, 0xC0266666u /* -2.6 */);
    if (l9_c_001026A0(o, S_700038B0, self + 0xD0, S_700038A0)) return;
    if (l9_c_001F5940(o, 4, S_700038B0, 0)) return;
    if (l9_c_001C6380(o, self)) return;
    if (l9_c_001B17A0(o, self, &r)) return;
    l9_method(o, self);
}

int em_level9_port_00824180(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    l9_00824180(&o, self);
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8266A0 (the outdoor creatures' partner, four in the deferred group
 * 0x829D00; the AREA01 0x828850 twin plus one test). By +4:
 *   0: 001B0FD0(self) zero: +0x34 (halfword) = 1, +0 = 1, and when
 *      001B11E0(+0x9A) is nonzero: (+0x18) +4 = 2, +4 = 3.
 *   1: nothing while D_00810702 < 8. +0x36 nonzero: +0 = 2, (+0x18) +4 =
 *      2, (+0x18) +0x21C = 0x5A; 001EFE00(0x80000045, self) nonzero:
 *      001FBD50(self, 0x426, 0, 300), +0x28 = 0, +4 = 2; else +4 = 3.
 *      +0x36 zero: 001C6380(self), 001B17A0(self), the +0x4C method.
 *   2: +0x28 below 10: +0x28 += 1, at 10 001FBD50(self, 0x427, 0, 300).
 *      Then 001B1190(+0x9A), 001B17A0(self), the +0x4C method.
 *   3 and others: 001AFC10(self).
 * ---------------------------------------------------------------------- */
int em_level9_port_008266A0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (l9_begin(&o, h, fault)) return -1;
    switch (l9_u8(&o, self + 4)) {
    case 0:
        if (l9_c_001B0FD0(&o, self, &r)) break;
        if (r == 0) {
            l9_w16(&o, self + 0x34, 1);
            l9_w8(&o, self + 0, 1);
            if (l9_c_001B11E0(&o, (int32_t)l9_u8(&o, self + 0x9A), &r)) break;
            if (r != 0) {
                l9_w8(&o, l9_u32(&o, self + 0x18) + 4, 2);
                l9_w8(&o, self + 4, 3);
            }
        }
        break;
    case 1:
        if (l9_u8(&o, D_00810702) < 8u) break;
        if (l9_s16(&o, self + 0x36) != 0) {
            l9_w8(&o, self + 0, 2);
            l9_w8(&o, l9_u32(&o, self + 0x18) + 4, 2);
            l9_w32(&o, l9_u32(&o, self + 0x18) + 0x21C, 0x5A);
            if (l9_c_001EFE00(&o, (int32_t)0x80000045u, self, &r)) break;
            if (r != 0) {
                sound(&o, self, 0x426, F_300);
                l9_w16(&o, self + 0x28, 0);
                l9_w8(&o, self + 4, 2);
            } else {
                l9_w8(&o, self + 4, 3);
            }
        } else {
            if (l9_c_001C6380(&o, self)) break;
            if (l9_c_001B17A0(&o, self, &r)) break;
            l9_method(&o, self);
        }
        break;
    case 2:
        {
            int32_t count = l9_s16(&o, self + 0x28);
            if (count < 10) {
                l9_w16(&o, self + 0x28, (uint32_t)(count + 1));
                if (l9_s16(&o, self + 0x28) == 10) sound(&o, self, 0x427, F_300);
            }
        }
        if (l9_c_001B1190(&o, (int32_t)l9_u8(&o, self + 0x9A))) break;
        if (l9_c_001B17A0(&o, self, &r)) break;
        l9_method(&o, self);
        break;
    default:
        l9_c_001AFC10(&o, self);
        break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x826850 (the hatches [62] / [63]). blk = self +0x1F0. "North" means
 * +0xB8 (z) above 1000 (not <= 1000): hatch [62]. By +4:
 *   0: +0 = 1, +8 = 1, +2 = 0x84 while D_00810C8B (item 0x27) is set,
 *      else 4. North: +0x30 = 0x82CDD0 and bit 0 of D_00810839 (counter
 *      0x61); south: 0x82CDF0 and bit 1. With the bit: +0x0D = 0x0D, +2 =
 *      4, 001B0FD0(self) (nonzero: stop), 001C6380(self), +4 = 2. Without:
 *      001B0F60(self, 9) (nonzero: stop), 001C68C0(self). Then +0x2EC =
 *      0.152 + +0xB4 and the four corners: 0x700038A0 = (-5.95, 0,
 *      -5.688, 1); 001026A0(0x700038B0, self +0xD0, 0x700038A0) -> +0x2E8
 *      / +0x2E4 (x / z); x = 5.832 -> +0x2E0 / +0x2DC; z = 5.536 -> +0x2D8
 *      / +0x2D4; x = -5.95 -> +0x2D0 / +0x2CC.
 *   1: +2 = 0x84 / 4 by item 0x27. By +5: 0 with +0x0B bit 2: the three
 *      ladder points 0x82CAB0 / 0x82CAC0 / 0x82CB00 (north or south set),
 *      +0x28 = 0, 001BA1A0(blk, 0x82CA50), +5 += 1. 1: blk +0x0E =
 *      001C64F0(self, 1.0); +0x28 += 1; 001BA1F0(self) nonzero:
 *      001CA6E0(self, 001C6120(D_0028A59C, 0x0D)), 001C62C0(self),
 *      001C6380(self), +4 = 2, +2 = 4, D_00810839 |= 1 (north) or 2, and
 *      return; else at +0x28 == 4 the record 0x82CA00 [5], [3], [1], then
 *      [0], [2], [4] by the side. Then 001C68C0(self), the +0x4C method
 *      when 001B17A0(self) is nonzero, and the four corner lights:
 *      0x700038B0 = (0x80, 0, 0, 0x80), 0x700038A4 = +0x2EC, 0x700038AC =
 *      1.0, each corner's x / z into 0x700038A0 / 0x700038A8 and
 *      001F4BF0(0x700038A0, 0x700038B0).
 *   2: +1 = 001B1630(+0xB0, +0xB4, +0xB8); the +0x4C method when +1 != 0.
 *   3 and others: 001AFC10(self).
 * ---------------------------------------------------------------------- */
#define F_1000 0x447A0000u

static void hatch_corner(L9 *o, uint32_t self, uint32_t x_at, uint32_t z_at)
{
    l9_c_001026A0(o, S_700038B0, self + 0xD0, S_700038A0);
    l9_w32(o, self + x_at, l9_u32(o, S_700038B0));
    l9_w32(o, self + z_at, l9_u32(o, S_700038B8));
}

int em_level9_port_00826850(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    uint32_t c = 0;
    if (l9_begin(&o, h, fault)) return -1;
    uint32_t blk = self + 0x1F0;
    switch (l9_u8(&o, self + 4)) {
    case 0: {
        l9_w8(&o, self + 0, 1);
        l9_w8(&o, self + 8, 1);
        l9_w8(&o, self + 2, l9_u8(&o, D_00810C8B) != 0 ? 0x84u : 4u);
        int north = !L9_LE(l9_u32(&o, self + 0xB8), F_1000);
        l9_w32(&o, self + 0x30, north ? 0x0082CDD0u : 0x0082CDF0u);
        if (l9_u8(&o, D_00810839) & (north ? 1u : 2u)) {
            l9_w8(&o, self + 0x0D, 0x0D);
            l9_w8(&o, self + 2, 4);
            if (l9_c_001B0FD0(&o, self, &r) || r != 0) break;
            if (l9_c_001C6380(&o, self)) break;
            l9_w8(&o, self + 4, 2);
        } else {
            if (l9_c_001B0F60(&o, self, 9, &r) || r != 0) break;
            if (l9_c_001C68C0(&o, self)) break;
        }
        l9_w32(&o, self + 0x2EC, L9_ADD(0x3E1BA5E3u /* 0.152 */, l9_u32(&o, self + 0xB4)));
        l9_w32(&o, S_700038A0, 0xC0BE6666u /* -5.95 */);
        l9_w32(&o, S_700038A4, F_ZERO);
        l9_w32(&o, S_700038A8, 0xC0B60419u /* -5.688 */);
        l9_w32(&o, S_700038AC, F_ONE);
        hatch_corner(&o, self, 0x2E8, 0x2E4);
        l9_w32(&o, S_700038A0, 0x40BA9FBEu /* 5.832 */);
        hatch_corner(&o, self, 0x2E0, 0x2DC);
        l9_w32(&o, S_700038A8, 0x40B126E9u /* 5.536 */);
        hatch_corner(&o, self, 0x2D8, 0x2D4);
        l9_w32(&o, S_700038A0, 0xC0BE6666u /* -5.95 */);
        hatch_corner(&o, self, 0x2D0, 0x2CC);
        break;
    }
    case 1:
        l9_w8(&o, self + 2, l9_u8(&o, D_00810C8B) != 0 ? 0x84u : 4u);
        switch (l9_u8(&o, self + 5)) {
        case 0:
            if (l9_u8(&o, self + 0x0B) & 4u) {
                if (!L9_LE(l9_u32(&o, self + 0xB8), F_1000)) {
                    l9_w32(&o, 0x0082CAB0u, 0x44368CCDu /* 730.2 */);
                    l9_w32(&o, 0x0082CAB4u, 0x4340E666u /* 192.9 */);
                    l9_w32(&o, 0x0082CAB8u, 0x449F8CCDu /* 1276.4 */);
                    l9_w32(&o, 0x0082CAC0u, 0x44356CCDu /* 725.7 */);
                    l9_w32(&o, 0x0082CAC4u, 0x432E999Au /* 174.6 */);
                    l9_w32(&o, 0x0082CAC8u, 0x449DD333u /* 1262.6 */);
                    l9_w32(&o, 0x0082CB00u, 0x4433F333u /* 719.8 */);
                    l9_w32(&o, 0x0082CB04u, 0x43208000u /* 160.5 */);
                    l9_w32(&o, 0x0082CB08u, 0x449C899Au /* 1252.3 */);
                } else {
                    l9_w32(&o, 0x0082CAB0u, 0x44895000u /* 1098.5 */);
                    l9_w32(&o, 0x0082CAB4u, 0x433CE666u /* 188.9 */);
                    l9_w32(&o, 0x0082CAB8u, 0x44558000u /* 854.0 */);
                    l9_w32(&o, 0x0082CAC0u, 0x44873000u /* 1081.5 */);
                    l9_w32(&o, 0x0082CAC4u, 0x432D3333u /* 173.2 */);
                    l9_w32(&o, 0x0082CAC8u, 0x4454B99Au /* 850.9 */);
                    l9_w32(&o, 0x0082CB00u, 0x4485F000u /* 1071.5 */);
                    l9_w32(&o, 0x0082CB04u, 0x43208000u /* 160.5 */);
                    l9_w32(&o, 0x0082CB08u, 0x44534CCDu /* 845.2 */);
                }
                l9_w16(&o, self + 0x28, 0);
                if (l9_c_001BA1A0(&o, blk, 0x0082CA50u)) break;
                l9_w8(&o, self + 5, l9_u8(&o, self + 5) + 1u);
            }
            break;
        case 1:
            if (l9_c_001C64F0(&o, self, F_ONE, &r)) break;
            l9_w16(&o, blk + 0x0E, (uint32_t)r);
            l9_w16(&o, self + 0x28, (uint32_t)(l9_s16(&o, self + 0x28) + 1));
            if (l9_c_001BA1F0(&o, self, &r)) break;
            if (r != 0) {
                if (l9_c_001C6120(&o, l9_u32(&o, D_0028A59C), 0x0D, &c)) break;
                if (l9_c_001CA6E0(&o, self, c)) break;
                if (l9_c_001C62C0(&o, self)) break;
                if (l9_c_001C6380(&o, self)) break;
                l9_w8(&o, self + 4, 2);
                l9_w8(&o, self + 2, 4);
                if (!L9_LE(l9_u32(&o, self + 0xB8), F_1000))
                    l9_w8(&o, D_00810839, l9_u8(&o, D_00810839) | 1u);
                else
                    l9_w8(&o, D_00810839, l9_u8(&o, D_00810839) | 2u);
                return l9_end(&o);
            }
            if (l9_s16(&o, self + 0x28) == 4) {
                l9_w32(&o, 0x0082CA14u, F_ZERO);
                l9_w32(&o, 0x0082CA0Cu, F_ZERO);
                l9_w32(&o, 0x0082CA04u, 0x43208000u /* 160.5 */);
                if (!L9_LE(l9_u32(&o, self + 0xB8), F_1000)) {
                    l9_w32(&o, 0x0082CA00u, 0x4433F333u /* 719.8 */);
                    l9_w32(&o, 0x0082CA08u, 0x449C899Au /* 1252.3 */);
                    l9_w32(&o, 0x0082CA10u, 0x3D00ADFCu /* 0.031415924 */);
                } else {
                    l9_w32(&o, 0x0082CA00u, 0x4485F000u /* 1071.5 */);
                    l9_w32(&o, 0x0082CA08u, 0x44534CCDu /* 845.2 */);
                    l9_w32(&o, 0x0082CA10u, 0x3FCB1292u /* 1.5865042 */);
                }
            }
            break;
        default:
            break;
        }
        if (l9_c_001C68C0(&o, self)) break;
        if (l9_c_001B17A0(&o, self, &r)) break;
        if (r != 0 && l9_method(&o, self)) break;
        l9_w32(&o, S_700038B0, 0x80);
        l9_w32(&o, S_700038B4, 0);
        l9_w32(&o, S_700038B8, 0);
        l9_w32(&o, S_700038BC, 0x80);
        l9_w32(&o, S_700038A4, l9_u32(&o, self + 0x2EC));
        l9_w32(&o, S_700038AC, F_ONE);
        l9_w32(&o, S_700038A0, l9_u32(&o, self + 0x2E8));
        l9_w32(&o, S_700038A8, l9_u32(&o, self + 0x2E4));
        if (l9_c_001F4BF0(&o, S_700038A0, S_700038B0)) break;
        l9_w32(&o, S_700038A0, l9_u32(&o, self + 0x2E0));
        l9_w32(&o, S_700038A8, l9_u32(&o, self + 0x2DC));
        if (l9_c_001F4BF0(&o, S_700038A0, S_700038B0)) break;
        l9_w32(&o, S_700038A0, l9_u32(&o, self + 0x2D8));
        l9_w32(&o, S_700038A8, l9_u32(&o, self + 0x2D4));
        if (l9_c_001F4BF0(&o, S_700038A0, S_700038B0)) break;
        l9_w32(&o, S_700038A0, l9_u32(&o, self + 0x2D0));
        l9_w32(&o, S_700038A8, l9_u32(&o, self + 0x2CC));
        l9_c_001F4BF0(&o, S_700038A0, S_700038B0);
        break;
    case 2:
        {
            uint32_t y = l9_u32(&o, self + 0xB4), z = l9_u32(&o, self + 0xB8), x = l9_u32(&o, self + 0xB0);
            if (l9_c_001B1630(&o, x, y, z, &r)) break;
        }
        l9_w8(&o, self + 1, (uint32_t)r);
        if (l9_u8(&o, self + 1) != 0) l9_method(&o, self);
        break;
    default:
        l9_c_001AFC10(&o, self);
        break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x826FB0 (an op09 record of the hatch's script 0x82CA50): a0 +0x2E
 * (halfword) = 0xFF; returns 1.
 * ---------------------------------------------------------------------- */
int em_level9_port_00826FB0(const EmLevel9PortHooks *h, uint32_t a0, int32_t *result, EmLevel9PortFault *fault)
{
    L9 o;
    if (!result || l9_begin(&o, h, fault)) return -1;
    l9_w16(&o, a0 + 0x2E, 0xFF);
    if (l9_failed(&o)) return -1;
    *result = 1;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x826FC0 (an op09 record of the hatch's script): 001FBD50(self, 0x40D,
 * 0, 300); returns 1.
 * ---------------------------------------------------------------------- */
int em_level9_port_00826FC0(const EmLevel9PortHooks *h, uint32_t self, int32_t *result, EmLevel9PortFault *fault)
{
    L9 o;
    if (!result || l9_begin(&o, h, fault)) return -1;
    sound(&o, self, 0x40D, F_300);
    if (l9_failed(&o)) return -1;
    *result = 1;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x826FF0 ([58] / [59]). blk = self +0x1F0. By +4:
 *   0: 001B0FD0(self) zero: 001C6380(self), +0x30 = 0x2759C0, +8 = 3,
 *      +0 = 1, +4 = 1.
 *   1: by +5: 0 with +0x0B bit 2: 001BA1A0(blk, 0x82CE10 when the player's
 *      x D_00810350 < 800, else 0x82CFD0), +5 = 1; 1 when 001BA1F0(self)
 *      is nonzero: +0x0B = 0, +5 = 0. Then 001B17A0(self), the +0x4C
 *      method.
 *   2, 3: 001AFC10(self). Others: nothing.
 * ---------------------------------------------------------------------- */
int em_level9_port_00826FF0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (l9_begin(&o, h, fault)) return -1;
    switch (l9_u8(&o, self + 4)) {
    case 0:
        if (l9_c_001B0FD0(&o, self, &r) || r != 0) break;
        if (l9_c_001C6380(&o, self)) break;
        l9_w32(&o, self + 0x30, D_002759C0);
        l9_w8(&o, self + 8, 3);
        l9_w8(&o, self + 0, 1);
        l9_w8(&o, self + 4, 1);
        break;
    case 1:
        switch (l9_u8(&o, self + 5)) {
        case 0:
            if (l9_u8(&o, self + 0x0B) & 4u) {
                uint32_t script = L9_LT(l9_u32(&o, D_00810350), 0x44480000u /* 800 */) ? 0x0082CE10u : 0x0082CFD0u;
                if (l9_c_001BA1A0(&o, self + 0x1F0, script)) break;
                l9_w8(&o, self + 5, 1);
            }
            break;
        case 1:
            if (l9_c_001BA1F0(&o, self, &r)) break;
            if (r != 0) {
                l9_w8(&o, self + 0x0B, 0);
                l9_w8(&o, self + 5, 0);
            }
            break;
        default:
            break;
        }
        if (l9_c_001B17A0(&o, self, &r)) break;
        l9_method(&o, self);
        break;
    case 2:
    case 3:
        l9_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8292A0 ([49]..[54]). By +4:
 *   0: D_008107F4 bit 5: +4 = 3. Else 001B0FD0(self) zero: 001C6380(self),
 *      +4 = 0x64, +0x28 = 0.
 *   0x64: 001B1B70(self), the +0x4C method; with D_008107F4 bit 5 +0x28 -=
 *      1 and below 0 +4 = 1.
 *   1: +4 = 3.
 *   3 and others: 001AFC10(self).
 * ---------------------------------------------------------------------- */
int em_level9_port_008292A0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (l9_begin(&o, h, fault)) return -1;
    switch (l9_u8(&o, self + 4)) {
    case 0:
        if (l9_u8(&o, D_008107F4) & 0x20u) {
            l9_w8(&o, self + 4, 3);
            break;
        }
        if (l9_c_001B0FD0(&o, self, &r) || r != 0) break;
        if (l9_c_001C6380(&o, self)) break;
        l9_w8(&o, self + 4, 0x64);
        l9_w16(&o, self + 0x28, 0);
        break;
    case 0x64:
        if (l9_c_001B1B70(&o, self)) break;
        if (l9_method(&o, self)) break;
        if (l9_u8(&o, D_008107F4) & 0x20u) {
            l9_w16(&o, self + 0x28, (uint32_t)(l9_s16(&o, self + 0x28) - 1));
            if (l9_s16(&o, self + 0x28) < 0) l9_w8(&o, self + 4, 1);
        }
        break;
    case 1:
        l9_w8(&o, self + 4, 3);
        break;
    default:
        l9_c_001AFC10(&o, self);
        break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8293A0 ([47] / [48]). REC = the placement record +0x9A * 0x28 of
 * the table D_0024D7C0[D_00810700][D_00810701]. By +4:
 *   0: D_008107F4 bit 6 clear: +0x0D = REC[0x2C], +0x0E = REC +0x2E, the
 *      rotation +0xC0..C8 = REC +0x40..48 and the position +0xB0..B8 = REC
 *      +0x34..3C, 0019C6F0(0x1F, 1), 0019C6F0(0x20, 0); set:
 *      0019C6F0(0x1F, 0), 0019C6F0(0x20, 1). Then 001B0FD0(self) zero:
 *      001C6380(self), +0x28 = 0x6E0.
 *   1: bit 6 set and +0x28 nonzero: +0x28 -= 1. +0x0D == 0x13 with bit 6
 *      set and +0x28 zero: the record's first pose (+4 model, +6, +0x18..
 *      rotation, +0x0C.. position), 001AF800(self), 001CB5B0(+9),
 *      001B0FD0(self), 001C6380(self), +4 = 1, 0019C6F0(0x1F, 0),
 *      0019C6F0(0x20, 1). Then 001B1B70(self), the +0x4C method.
 *   3 and others: 001AFC10(self).
 * ---------------------------------------------------------------------- */
static uint32_t place_record(L9 *o, uint32_t self)
{
    uint32_t area = l9_u8(o, D_00810700);
    uint32_t index = l9_u8(o, self + 0x9A) * 0x28u;
    uint32_t sub = l9_u8(o, D_00810701);
    uint32_t table = l9_u32(o, D_0024D7C0 + area * 4u);
    return index + l9_u32(o, table + sub * 4u);
}

static void second_pose(L9 *o, uint32_t self)
{
    l9_w8(o, self + 0x0D, l9_u8(o, place_record(o, self) + 0x2C));
    l9_w16(o, self + 0x0E, l9_u16(o, place_record(o, self) + 0x2E));
    l9_w32(o, self + 0xC0, l9_u32(o, place_record(o, self) + 0x40));
    l9_w32(o, self + 0xC4, l9_u32(o, place_record(o, self) + 0x44));
    l9_w32(o, self + 0xC8, l9_u32(o, place_record(o, self) + 0x48));
    l9_w32(o, self + 0xB0, l9_u32(o, place_record(o, self) + 0x34));
    l9_w32(o, self + 0xB4, l9_u32(o, place_record(o, self) + 0x38));
    l9_w32(o, self + 0xB8, l9_u32(o, place_record(o, self) + 0x3C));
}

int em_level9_port_008293A0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (l9_begin(&o, h, fault)) return -1;
    switch (l9_u8(&o, self + 4)) {
    case 0:
        if (!(l9_u8(&o, D_008107F4) & 0x40u)) {
            second_pose(&o, self);
            if (l9_c_0019C6F0(&o, 0x1F, 1, &r)) break;
            if (l9_c_0019C6F0(&o, 0x20, 0, &r)) break;
        } else {
            if (l9_c_0019C6F0(&o, 0x1F, 0, &r)) break;
            if (l9_c_0019C6F0(&o, 0x20, 1, &r)) break;
        }
        if (l9_c_001B0FD0(&o, self, &r) || r != 0) break;
        if (l9_c_001C6380(&o, self)) break;
        l9_w16(&o, self + 0x28, 0x6E0);
        break;
    case 1:
        if (l9_u8(&o, D_008107F4) & 0x40u) {
            int32_t count = l9_s16(&o, self + 0x28);
            if (count != 0) l9_w16(&o, self + 0x28, (uint32_t)(count - 1));
        }
        if (l9_u8(&o, self + 0x0D) == 0x13u && (l9_u8(&o, D_008107F4) & 0x40u) && l9_s16(&o, self + 0x28) == 0) {
            l9_w8(&o, self + 0x0D, l9_u8(&o, place_record(&o, self) + 4));
            l9_w16(&o, self + 0x0E, l9_u16(&o, place_record(&o, self) + 6));
            l9_w32(&o, self + 0xC0, l9_u32(&o, place_record(&o, self) + 0x18));
            l9_w32(&o, self + 0xC4, l9_u32(&o, place_record(&o, self) + 0x1C));
            l9_w32(&o, self + 0xC8, l9_u32(&o, place_record(&o, self) + 0x20));
            l9_w32(&o, self + 0xB0, l9_u32(&o, place_record(&o, self) + 0x0C));
            l9_w32(&o, self + 0xB4, l9_u32(&o, place_record(&o, self) + 0x10));
            l9_w32(&o, self + 0xB8, l9_u32(&o, place_record(&o, self) + 0x14));
            if (l9_c_001AF800(&o, self)) break;
            if (l9_c_001CB5B0(&o, (int32_t)l9_u8(&o, self + 9))) break;
            if (l9_c_001B0FD0(&o, self, &r)) break;
            if (l9_c_001C6380(&o, self)) break;
            l9_w8(&o, self + 4, 1);
            if (l9_c_0019C6F0(&o, 0x1F, 0, &r)) break;
            if (l9_c_0019C6F0(&o, 0x20, 1, &r)) break;
        }
        if (l9_c_001B1B70(&o, self)) break;
        l9_method(&o, self);
        break;
    default:
        l9_c_001AFC10(&o, self);
        break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8299E0 ([11], a deferred group member). blk = self +0x1F0. By +4:
 *   0: 0015AC00(self, blk) zero: +0x2EC = 1, +0x2E8 = +0xB4, +0x28 =
 *      0x71C.
 *   1: D_00810774 == 0: 0015AE20(self, blk).
 *   2, 3 and others: 001B1190(+0x9A), 001AFC10(self).
 * ---------------------------------------------------------------------- */
int em_level9_port_008299E0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (l9_begin(&o, h, fault)) return -1;
    uint32_t blk = self + 0x1F0;
    switch (l9_u8(&o, self + 4)) {
    case 0:
        if (l9_c_0015AC00(&o, self, blk, &r) || r != 0) break;
        l9_w32(&o, self + 0x2EC, 1);
        l9_w32(&o, self + 0x2E8, l9_u32(&o, self + 0xB4));
        l9_w16(&o, self + 0x28, 0x71C);
        break;
    case 1:
        if (l9_u8(&o, D_00810774) == 0) l9_c_0015AE20(&o, self, blk);
        break;
    default:
        if (l9_c_001B1190(&o, (int32_t)l9_u8(&o, self + 0x9A))) break;
        l9_c_001AFC10(&o, self);
        break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x829AA0 (NEARMISS; from the instructions; no jal caller; its address
 * is a behaviour pointer in a boot .data table of 0x30-byte records, the
 * word at 0x25973C; ran live at a13_00 frame 561 per the census): a fading
 * full-screen GS packet. By +4:
 *   0: +4 = 1, +5 = 24, and on into 1.
 *   1: 0x70003A20 = (float)+5 / 24; f = 224 * that; the colour word c =
 *      001281C0(f) | 001281C0(f) << 8 | 001281C0(f) << 16 |
 *      001281C0(128) << 24; p = 001CB5F0(0x7635C0, 0xFFE000, 6); the
 *      0x60-byte packet at p (a zero quadword with the word 0x50000005 at
 *      +0xC, then eleven doublewords: the GIF tag and register writes with
 *      c as the colour); +5 -= 1, at 0 +4 = 3.
 *   2, 3: 001AFC10(self). Others: nothing.
 * ---------------------------------------------------------------------- */
int em_level9_port_00829AA0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    uint32_t p = 0;
    if (l9_begin(&o, h, fault)) return -1;
    uint32_t state = l9_u8(&o, self + 4);
    if (state == 2 || state == 3) {
        l9_c_001AFC10(&o, self);
        return l9_end(&o);
    }
    if (state == 0) {
        l9_w8(&o, self + 4, 1);
        l9_w8(&o, self + 5, 0x18);
    } else if (state != 1) {
        return l9_end(&o);
    }
    l9_w32(&o, S_70003A20, L9_DIV(L9_CVT(l9_u8(&o, self + 5)), 0x41C00000u /* 24 */));
    uint32_t f = L9_MUL(0x43600000u /* 224 */, l9_u32(&o, S_70003A20));
    uint32_t c;
    if (l9_c_001281C0(&o, f, &r)) return -1;
    c = (uint32_t)r;
    if (l9_c_001281C0(&o, f, &r)) return -1;
    c |= (uint32_t)r << 8;
    if (l9_c_001281C0(&o, f, &r)) return -1;
    c |= (uint32_t)r << 16;
    if (l9_c_001281C0(&o, F_128, &r)) return -1;
    c |= (uint32_t)r << 24;
    if (l9_c_001CB5F0(&o, D_007635C0, 0xFFE000u, 6, &p)) return -1;
    l9_w64(&o, p + 0x0, 0);
    l9_w64(&o, p + 0x8, 0);
    l9_w32(&o, p + 0xC, 0x50000005u);
    l9_w64(&o, p + 0x10, 0x8001u | ((uint64_t)0x10000000u << 32));
    l9_w64(&o, p + 0x18, 0xE);
    l9_w64(&o, p + 0x20, 0x68u | ((uint64_t)0x80u << 32));
    l9_w64(&o, p + 0x28, 0x42);
    l9_w64(&o, p + 0x30, 0x8001u | ((uint64_t)0x44000000u << 32));
    l9_w64(&o, p + 0x38, 0x4410);
    l9_w64(&o, p + 0x40, 0x46);
    l9_w64(&o, p + 0x48, (uint64_t)(int64_t)(int32_t)c);
    l9_w64(&o, p + 0x50, 0x79007000u | ((uint64_t)0xFFFFFFu << 32));
    l9_w64(&o, p + 0x58, (uint64_t)(int64_t)(int32_t)0x87009000u);
    l9_w8(&o, self + 5, l9_u8(&o, self + 5) - 1u);
    if ((l9_u8(&o, self + 5) & 0xFFu) == 0) l9_w8(&o, self + 4, 3);
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x827150 ([45] / [46], the machine's panel). blk = D_00275CA8 = self
 * +0x1F0, stored first every frame (the words [0] step, [1] count, [2]
 * count limit, [3] markers shown, [4], [5], [6] blink bit, [7] latch).
 * REC as 0x8293A0. By +4:
 *   0: 001B0FD0(self); +0x28 = +0x2A = 0; blk [0] = [1] = 0, [3] = 4, [4]
 *      = [5] = 0, [6] = -1, [7] = 0; when D_00810774 == 0xFF: +0x0D =
 *      0x12, the record's second pose, 001CA6E0(self, 001C6120(D_0028A59C,
 *      0x12)), 001C62C0(self), 001C6380(self), +4 = 1.
 *   1: by +0x0D: 0x11 (see the sub-states in the code), 0x12: with
 *      D_00810774 == 0xFF 001C6380(self), 001B1B70(self), the +0x4C method.
 *   2, 3: 001AFC10(self). Others: nothing.
 * ---------------------------------------------------------------------- */
#define T_82D190 0x0082D190u /* step functions */
#define T_82D1B0 0x0082D1B0u /* four marker points (16 bytes each) */
#define T_82D1F0 0x0082D1F0u /* twelve sprite offsets (16 bytes each) */
#define T_82D2F0 0x0082D2F0u /* four marker rotations (16 bytes each) */

static uint32_t panel_block(L9 *o) { return l9_u32(o, D_00275CA8); }

static void panel_pose(L9 *o, uint32_t self)
{
    uint32_t c = 0;
    l9_w16(o, self + 0x0E, l9_u16(o, place_record(o, self) + 0x2E));
    l9_w32(o, self + 0xC0, l9_u32(o, place_record(o, self) + 0x40));
    l9_w32(o, self + 0xC4, l9_u32(o, place_record(o, self) + 0x44));
    l9_w32(o, self + 0xC8, l9_u32(o, place_record(o, self) + 0x48));
    l9_w32(o, self + 0xB0, l9_u32(o, place_record(o, self) + 0x34));
    l9_w32(o, self + 0xB4, l9_u32(o, place_record(o, self) + 0x38));
    l9_w32(o, self + 0xB8, l9_u32(o, place_record(o, self) + 0x3C));
    if (l9_c_001C6120(o, l9_u32(o, D_0028A59C), 0x12, &c)) return;
    if (l9_c_001CA6E0(o, self, c)) return;
    if (l9_c_001C62C0(o, self)) return;
    l9_c_001C6380(o, self);
}

static void panel_markers(L9 *o, uint32_t self)
{
    int32_t r = 0;
    uint32_t c = 0;
    (void)self;
    l9_w32(o, S_700038A0, F_ONE);
    l9_w32(o, S_700038A4, F_ONE);
    l9_w32(o, S_700038A8, F_ONE);
    l9_w32(o, S_700038AC, F_ONE);
    for (int32_t i = 4 - l9_s32(o, panel_block(o) + 12); i < 4 && !l9_failed(o); i++) {
        uint32_t rot = T_82D2F0 + (uint32_t)i * 16u;
        if (l9_c_001029C0(o, S_700036A0)) return;
        if (l9_c_00102B08(o, S_700036A0, S_700036A0, l9_u32(o, rot))) return;
        if (l9_c_00102BB0(o, S_700036A0, S_700036A0, l9_u32(o, rot + 4))) return;
        if (l9_c_00102A60(o, S_700036A0, S_700036A0, l9_u32(o, rot + 8))) return;
        if (l9_c_00102918(o, S_700036A0, S_700036A0, T_82D1B0 + (uint32_t)i * 16u)) return;
        if (l9_c_001CA7B0(o, S_700036D0, F_10, &r)) return;
        if (r >= 0) {
            int32_t handle = r;
            if (l9_c_001C7900(o, S_700036A0, S_700038A0, 0x3F5, 0)) return;
            if (l9_c_001C6120(o, l9_u32(o, D_0028A59C), 0x1B, &c)) return;
            if (l9_c_001CA940(o, handle, (int32_t)c)) return;
        }
        l9_w32(o, S_700038C0, F_ZERO);
        l9_w32(o, S_700038B0, 0xFF);
        l9_w32(o, S_700038B4, 0);
        l9_w32(o, S_700038B8, 0);
        l9_w32(o, S_700038BC, 0x80);
        l9_w32(o, S_700038C4, 0x40600000u /* 3.5 */);
        l9_w32(o, S_700038C8, 0x3F66C8B4u /* 0.9015 */);
        l9_w32(o, S_700038CC, F_ONE);
        if (l9_c_001026A0(o, S_700038C0, S_700036A0, S_700038C0)) return;
        if (i == 0) {
            uint32_t b = panel_block(o);
            int32_t blink = l9_s32(o, b + 24);
            if (blink == -1) {
                if (l9_c_001F4E20(o, S_700038C0, S_700038B0, F_5)) return;
            } else if ((l9_sra(l9_u32(o, S_70003B68), (unsigned)blink & 31u) & 1) != 0) {
                if (l9_c_001F4E20(o, S_700038C0, S_700038B0, 0x40E00000u /* 7 */)) return;
                uint32_t b2 = panel_block(o);
                if (l9_u32(o, b2 + 28) == 0) {
                    l9_w32(o, b2 + 28, 1);
                    if (l9_c_001F02C0(o, S_700038C0, 0x8D2, 0x43FA0000u /* 500 */)) return;
                }
            } else {
                l9_w32(o, b + 28, 0);
            }
        } else {
            if (l9_c_001F4E20(o, S_700038C0, S_700038B0, F_5)) return;
        }
    }
    if (l9_s32(o, panel_block(o)) < 4) {
        for (int32_t i = 0; i < 4 - l9_s32(o, panel_block(o) + 12) && !l9_failed(o); i++) {
            if (!(l9_u32(o, S_70003B68) & 0xFu)) {
                if (l9_c_001EFD20(o, 5, T_82D1B0 + (uint32_t)i * 16u)) return;
            }
        }
        l9_w32(o, S_700038B0, 0xFF);
        l9_w32(o, S_700038B4, 0);
        l9_w32(o, S_700038B8, 0);
        l9_w32(o, S_700038BC, 0x80);
        for (uint32_t i = 0; i < 12; i++) {
            if (l9_c_001028B8(o, S_700038A0, self + 0xB0, T_82D1F0 + i * 16u)) return;
            if (l9_c_001F4E20(o, S_700038A0, S_700038B0, 0x40E00000u /* 7 */)) return;
        }
    }
}

int em_level9_port_00827150(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (l9_begin(&o, h, fault)) return -1;
    l9_w32(&o, D_00275CA8, self + 0x1F0);
    switch (l9_u8(&o, self + 4)) {
    case 0:
        if (l9_c_001B0FD0(&o, self, &r)) break;
        l9_w16(&o, self + 0x28, 0);
        l9_w16(&o, self + 0x2A, 0);
        l9_w32(&o, panel_block(&o) + 0, 0);
        l9_w32(&o, panel_block(&o) + 4, 0);
        l9_w32(&o, panel_block(&o) + 12, 4);
        l9_w32(&o, panel_block(&o) + 16, 0);
        l9_w32(&o, panel_block(&o) + 20, 0);
        l9_w32(&o, panel_block(&o) + 24, 0xFFFFFFFFu);
        l9_w32(&o, panel_block(&o) + 28, 0);
        if (l9_u8(&o, D_00810774) == 0xFFu) {
            l9_w8(&o, self + 0x0D, 0x12);
            panel_pose(&o, self);
            l9_w8(&o, self + 4, 1);
        }
        break;
    case 1:
        switch (l9_u8(&o, self + 0x0D)) {
        case 0x11:
            if (l9_u8(&o, D_008107F4) == 0xFFu) l9_w8(&o, self + 5, 3);
            uint32_t sub = l9_u8(&o, self + 5);
            switch (sub) {
            case 0:
                l9_w8(&o, self + 5, sub + 1u);
                l9_w32(&o, self + 0x40, l9_u32(&o, D_0028A6EC));
                if (l9_c_001C63E0(&o, self, 0)) break;
                l9_c_001C68C0(&o, self);
                break;
            case 1: {
                uint32_t f = l9_u8(&o, D_008107F4);
                if (f & 2u) {
                    l9_w8(&o, D_008107F4, f | 0x10u);
                    l9_w8(&o, self + 5, l9_u8(&o, self + 5) + 1u);
                }
                l9_c_001C68C0(&o, self);
                break;
            }
            case 2: {
                uint32_t f = l9_u8(&o, D_008107F4);
                uint32_t b = panel_block(&o);
                l9_w8(&o, D_008107F4, f | 0x40u);
                uint32_t fn = l9_u32(&o, T_82D190 + (uint32_t)l9_s32(&o, b) * 4u);
                if (l9_callback(&o, fn, self)) break;
                b = panel_block(&o);
                l9_w32(&o, b + 4, l9_u32(&o, b + 4) + 1u);
                b = panel_block(&o);
                if (!(l9_s32(&o, b + 4) < l9_s32(&o, b + 8))) {
                    l9_w32(&o, b, l9_u32(&o, b) + 1u);
                    l9_w32(&o, panel_block(&o) + 4, 0);
                }
                l9_c_001C68C0(&o, self);
                break;
            }
            case 3:
                l9_w8(&o, self + 5, sub + 1u);
                panel_pose(&o, self);
                break;
            case 4:
                l9_c_001C6380(&o, self);
                break;
            default:
                break;
            }
            if (l9_c_001B1B70(&o, self)) break;
            if (l9_method(&o, self)) break;
            panel_markers(&o, self);
            break;
        case 0x12:
            if (l9_u8(&o, D_00810774) == 0xFFu) {
                if (l9_c_001C6380(&o, self)) break;
                if (l9_c_001B1B70(&o, self)) break;
                l9_method(&o, self);
            }
            break;
        default:
            break;
        }
        break;
    case 2:
    case 3:
        l9_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l9_end(&o);
}
