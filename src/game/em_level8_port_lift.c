/* The lift and the area-change segment of the eighth level (see
 * em_level8_port.h, docs/LEVEL8_PORT.md): 001BBD20, 001BC560, 001BC6D0,
 * 001BC740, 001BC860, 001BD180, 001BD270, 001BD9F0, 001BDE60.
 *
 * Each function below is a translation of the original code at the named
 * address. Calls, their arguments and the memory accesses between each two
 * calls follow the original instructions: the test compares memory at every
 * call entry and after the last store, and the memory accesses between
 * calls one for one, in order, by address and size (docs/LEVEL8_PORT.md
 * section 3). Where the original loads the operands of one expression in a
 * set order, the translation loads them in separate statements in that
 * order, because C leaves the order of evaluation of operands open. Float
 * arithmetic is the EE model (em_ee_float.h) on bit patterns, with the
 * original's operand order for each add/sub/mul/div/compare; a float the
 * original only moves is carried as its bits.
 */
#include "em_level8_port_internal.h"

/* ------------------------------------------------------------------------
 * 001BBD20 (C linked from assembly; from the instructions): the sound of a
 * lift or door record. The row is the high byte of the signed halfword
 * self +0x56 (masked with 0xFF00 before the shift, so 0..255), two
 * halfwords a row from D_0024DB80; the sound is the unsigned halfword
 * index of that row. A tail call of 001FBD50(self, sound, 0, 300.0): the
 * third argument is 0 (the C text's pass-through of a2 is not what the
 * instructions do), and 001FBD50's result is the result.
 * ---------------------------------------------------------------------- */
int32_t l8_001BBD20(L8 *o, uint32_t self, int32_t index)
{
    int32_t hw = l8_s16(o, self + 0x56);
    uint32_t row = D_0024DB80 + (((((uint32_t)hw & 0xFF00u) >> 8)) << 2);
    uint32_t sound = l8_u16(o, ((uint32_t)index << 1) + row);
    int32_t r = 0;
    l8_c_001FBD50(o, self, (int32_t)sound, 0, fl(F_300), &r);
    return r;
}

int em_level8_port_001BBD20(const EmLevel8PortHooks *h, uint32_t self, int32_t index, int32_t *result,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001BBD20(&o, self, index);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001BC560 (NEARMISS; from the instructions): a camera step (the lift's
 * ride). 00102948(D_00810210, 0x70003B50); the angle D_00810214 -=
 * 1.1693707, stored, then normalized by 001B1470 and stored again;
 * 00102948(D_00810200, D_00810350); D_00810204 += 13.8; the matrix
 * 0x70003400 = identity (001029C0) turned by 00102C58 with D_00810210; the
 * point 0x70003600 = (0, 0, -20, 1) through it into D_008101F0 (001026A0);
 * then D_008101F0 += D_00810200, D_008101F4 += D_00810204 + D_0081023C,
 * D_008101F8 += D_00810208, each loaded, added and stored in turn;
 * 0018D7B0(D_008101E0, 1). Returns 1.
 * ---------------------------------------------------------------------- */
static int32_t l8_001BC560(L8 *o)
{
    if (l8_c_00102948(o, 0x00810210u, S_70003B50)) return 0;
    uint32_t a = l8_u32(o, 0x00810214u);
    uint32_t f12 = L8_SUB(a, F_1_1693707);
    l8_w32(o, 0x00810214u, f12);
    float r = 0.0f;
    if (l8_angle(o, fl(f12), &r)) return 0;
    l8_w32(o, 0x00810214u, l8_bits(r));
    if (l8_c_00102948(o, 0x00810200u, 0x00810350u)) return 0;
    uint32_t b = l8_u32(o, 0x00810204u);
    l8_w32(o, 0x00810204u, L8_ADD(b, F_13_8));
    if (l8_c_001029C0(o, S_70003400)) return 0;
    if (l8_c_00102C58(o, S_70003400, S_70003400, 0x00810210u)) return 0;
    l8_w32(o, S_70003600, 0);
    l8_w32(o, S_70003600 + 4u, 0);
    l8_w32(o, S_70003600 + 8u, F_M20);
    l8_w32(o, S_70003600 + 12u, F_ONE);
    if (l8_c_001026A0(o, 0x008101F0u, S_70003400, S_70003600)) return 0;
    uint32_t x = l8_u32(o, 0x008101F0u);
    uint32_t dx = l8_u32(o, 0x00810200u);
    l8_w32(o, 0x008101F0u, L8_ADD(x, dx));
    uint32_t y = l8_u32(o, 0x008101F4u);
    uint32_t dy = l8_u32(o, 0x00810204u);
    uint32_t ey = l8_u32(o, 0x0081023Cu);
    l8_w32(o, 0x008101F4u, L8_ADD(y, L8_ADD(dy, ey)));
    uint32_t z = l8_u32(o, 0x008101F8u);
    uint32_t dz = l8_u32(o, 0x00810208u);
    l8_w32(o, 0x008101F8u, L8_ADD(z, dz));
    l8_c_0018D7B0(o, D_008101E0, 1);
    return 1;
}

int em_level8_port_001BC560(const EmLevel8PortHooks *h, int32_t *result, EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001BC560(&o);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001BC6D0 (C byte-identical): a script step on the record three links
 * down (self +0x18 -> +0x18 -> +0x18; all three loads are made, the
 * second between the load of st +4 and the third). st +4 == 0: when the
 * record's +0x0B is 0 it becomes 2 and st +4 counts on; returns 0. st +4 ==
 * 1: returns 1 when the record's +0x0B is 3, else 0. Other: 0.
 * ---------------------------------------------------------------------- */
static int32_t l8_001BC6D0(L8 *o, uint32_t self, uint32_t st)
{
    uint32_t p = l8_u32(o, self + 0x18);
    uint32_t step = l8_u8(o, st + 4);
    p = l8_u32(o, p + 0x18);
    p = l8_u32(o, p + 0x18);
    if (step == 1) return l8_u8(o, p + 0x0B) == 3 ? 1 : 0;
    if (step == 0 && l8_u8(o, p + 0x0B) == 0) {
        l8_w8(o, p + 0x0B, 2);
        l8_w8(o, st + 4, l8_u8(o, st + 4) + 1u);
    }
    return 0;
}

int em_level8_port_001BC6D0(const EmLevel8PortHooks *h, uint32_t self, uint32_t st, int32_t *result,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001BC6D0(&o, self, st);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001BC740 / 001BC860 (C byte-identical): the Use offer of a lift button.
 * When self +0x0B bit 2 (the Use) is set: the player's yaw = 001B1470(pi +
 * self yaw +0xC4); the point (0.3, 0, 5, 1) at 0x700038A0 through the
 * button's matrix self +0xD0 (001026A0 in place), its y replaced by the
 * player's +0xA4; 00182F90(player, 0x700038A0) (the player placed there);
 * then the script (001BA1A0 on blk): 001BC740 takes 0x24E3A0 when self +3
 * is 0, else 0x24E560; 001BC860 always 0x24E7E0; 001BA1F0(self), self +0 =
 * 2, returns 1. Without the bit: returns 0.
 * ---------------------------------------------------------------------- */
static int32_t l8_use_offer(L8 *o, uint32_t self, uint32_t blk, int two_scripts)
{
    if (!(l8_u8(o, self + 0x0B) & 4u)) return 0;
    uint32_t yaw = l8_u32(o, self + 0xC4);
    float r = 0.0f;
    if (l8_angle(o, fl(L8_ADD(F_PI, yaw)), &r)) return 0;
    l8_w32(o, D_008102B0 + 0xC4u, l8_bits(r));
    l8_w32(o, S_700038A0, F_0_3);
    l8_w32(o, S_700038A0 + 4u, 0);
    l8_w32(o, S_700038A0 + 8u, F_5);
    l8_w32(o, S_700038A0 + 12u, F_ONE);
    if (l8_c_001026A0(o, S_700038A0, self + 0xD0u, S_700038A0)) return 0;
    l8_w32(o, S_700038A0 + 4u, l8_u32(o, D_008102B0 + 0xA4u));
    if (l8_c_00182F90(o, D_008102B0, S_700038A0)) return 0;
    uint32_t script = D_0024E7E0;
    if (two_scripts) script = l8_u8(o, self + 3) == 0 ? D_0024E3A0 : D_0024E560;
    if (l8_c_001BA1A0(o, blk, script)) return 0;
    int32_t done = 0;
    if (l8_c_001BA1F0(o, self, &done)) return 0;
    l8_w8(o, self, 2);
    return 1;
}

int32_t l8_001BC860(L8 *o, uint32_t self, uint32_t blk) { return l8_use_offer(o, self, blk, 0); }

int em_level8_port_001BC740(const EmLevel8PortHooks *h, uint32_t self, uint32_t blk, int32_t *result,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_use_offer(&o, self, blk, 1);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

int em_level8_port_001BC860(const EmLevel8PortHooks *h, uint32_t self, uint32_t blk, int32_t *result,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001BC860(&o, self, blk);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* The four +0x80 floats of the pose block *D_00275B40 (its records +4,
 * +8, +0xC, +0x10): the first two get rec +0x10 + rec +0x14, the last two
 * rec +0x14; the pointer and the rec fields are loaded again for each. */
static void l8_pose_floats(L8 *o, uint32_t rec)
{
    for (uint32_t k = 4; k <= 8; k += 4) {
        uint32_t g = l8_u32(o, D_00275B40);
        uint32_t a = l8_u32(o, rec + 0x10);
        uint32_t b = l8_u32(o, rec + 0x14);
        uint32_t p = l8_u32(o, g + k);
        l8_w32(o, p + 0x80, L8_ADD(a, b));
    }
    for (uint32_t k = 0xC; k <= 0x10; k += 4) {
        uint32_t g = l8_u32(o, D_00275B40);
        uint32_t b = l8_u32(o, rec + 0x14);
        uint32_t p = l8_u32(o, g + k);
        l8_w32(o, p + 0x80, b);
    }
}

/* ------------------------------------------------------------------------
 * 001BD180 (NEARMISS C; the instructions agree): the doors opening. rec
 * +0x10 += 0.2 (stored); at 6 or more: clamped to 14 when not below 14,
 * then rec +0x14 += 0.2 (stored), and above 16 it is set to 16 and the
 * function returns 1 without touching the pose. Otherwise the pose floats
 * (l8_pose_floats) and 0.
 * ---------------------------------------------------------------------- */
static int32_t l8_001BD180(L8 *o, uint32_t rec)
{
    uint32_t a = L8_ADD(l8_u32(o, rec + 0x10), F_0_2);
    int below = L8_LT(a, F_6);
    l8_w32(o, rec + 0x10, a);
    if (!below) {
        if (!L8_LT(a, F_14)) l8_w32(o, rec + 0x10, F_14);
        uint32_t b = L8_ADD(l8_u32(o, rec + 0x14), F_0_2);
        int within = L8_LE(b, F_16);
        l8_w32(o, rec + 0x14, b);
        if (!within) {
            l8_w32(o, rec + 0x14, F_16);
            return 1;
        }
    }
    l8_pose_floats(o, rec);
    return 0;
}

int em_level8_port_001BD180(const EmLevel8PortHooks *h, uint32_t rec, int32_t *result, EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001BD180(&o, rec);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001BD270 (C byte-identical): the doors closing. rec +0x14 -= 0.2
 * (stored); at 8 or less: clamped to 0 at 0 or less, and rec +0x10 -= 0.2
 * (stored, clamped to 0 the same way). The pose floats, then 1 when rec
 * +0x10 (loaded again) equals 0, else 0.
 * ---------------------------------------------------------------------- */
static int32_t l8_001BD270(L8 *o, uint32_t rec)
{
    uint32_t a = L8_SUB(l8_u32(o, rec + 0x14), F_0_2);
    int low = L8_LE(a, F_8);
    l8_w32(o, rec + 0x14, a);
    if (low) {
        if (L8_LE(a, F_ZERO)) l8_w32(o, rec + 0x14, 0);
        uint32_t b = L8_SUB(l8_u32(o, rec + 0x10), F_0_2);
        int shut = L8_LE(b, F_ZERO);
        l8_w32(o, rec + 0x10, b);
        if (shut) l8_w32(o, rec + 0x10, 0);
    }
    l8_pose_floats(o, rec);
    return L8_EQ(F_ZERO, l8_u32(o, rec + 0x10)) ? 1 : 0;
}

int em_level8_port_001BD270(const EmLevel8PortHooks *h, uint32_t rec, int32_t *result, EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001BD270(&o, rec);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001BD9F0 (NEARMISS; from the instructions): an area-change door (the
 * AREA13 room moves). Entry loads self +4 and the link self +0x18.
 *   3, 2: 001AFC10(self).
 *   0: 001B0FD0(self); +0x34 (halfword) = +0x2E (byte); +0x2E (halfword) =
 *      1 when +3 is 3, else 0; +0x30 = D_00275610; 001C6380(self); +0 = 1.
 *   1: by +5, each first calling 00158590(self, 1, 1) unless noted:
 *      0  when the halfword +0x2E is 1: D_00810774 == 1 gives +0 = 2 and
 *         00158590(self, 0, 1), else +0 = 1 (and the usual call); then
 *         001BC860(self, self +0x1F0) (the Use offer) nonzero: +5 + 1.
 *      1  001BA1F0(self) nonzero: the target = the link (its own +0x18 when
 *         +3 is 4); D_008105E0.. = its +0xB0, 16 + its +0xB4, its +0xB8;
 *         its +0x0B = 1; +0x28 = 60; +5 + 1.
 *      2  +0x28 counts down; at 0: +5 + 1 and 001BC150(self) (the area
 *         request).
 *      3  +5 + 1, 001AEBA0(4).
 *      4  D_008106B8 == 0: +5 = 0, +0 = 1, +0x0B = 0.
 *      then (every +5) 001B17A0(self) and the +0x4C method.
 *   other: nothing.
 * The NEARMISS text's arguments to 00158590 / 001AFC10 / 001B0FD0 are not
 * the instructions'.
 * ---------------------------------------------------------------------- */
static void l8_001BD9F0(L8 *o, uint32_t self)
{
    uint32_t state = l8_u8(o, self + 4);
    uint32_t link = l8_u32(o, self + 0x18);
    if (state == 3 || state == 2) {
        l8_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        int32_t ignored = 0;
        if (l8_c_001B0FD0(o, self, &ignored)) return;
        l8_w16(o, self + 0x34, l8_u8(o, self + 0x2E));
        l8_w16(o, self + 0x2E, l8_u8(o, self + 3) == 3 ? 1u : 0u);
        l8_w32(o, self + 0x30, D_00275610);
        if (l8_c_001C6380(o, self)) return;
        l8_w8(o, self, 1);
        return;
    }
    if (state != 1) return;
    uint32_t sub = l8_u8(o, self + 5);
    switch (sub) {
    case 0:
        if (l8_u16(o, self + 0x2E) == 1) {
            if (l8_u8(o, D_00810774) == 1) {
                l8_w8(o, self, 2);
                if (l8_c_00158590(o, self, 0, 1)) return;
            } else {
                l8_w8(o, self, 1);
                if (l8_c_00158590(o, self, 1, 1)) return;
            }
        } else if (l8_c_00158590(o, self, 1, 1)) {
            return;
        }
        if (l8_001BC860(o, self, self + 0x1F0)) l8_w8(o, self + 5, l8_u8(o, self + 5) + 1u);
        break;
    case 1: {
        if (l8_c_00158590(o, self, 1, 1)) return;
        int32_t done = 0;
        if (l8_c_001BA1F0(o, self, &done)) return;
        if (done) {
            if (l8_u8(o, self + 3) == 4) link = l8_u32(o, link + 0x18);
            l8_w32(o, D_008105E0, l8_u32(o, link + 0xB0));
            uint32_t y = l8_u32(o, link + 0xB4);
            l8_w32(o, D_008105E0 + 4u, L8_ADD(F_16, y));
            l8_w32(o, D_008105E0 + 8u, l8_u32(o, link + 0xB8));
            l8_w8(o, link + 0x0B, 1);
            l8_w16(o, self + 0x28, 0x3C);
            l8_w8(o, self + 5, l8_u8(o, self + 5) + 1u);
        }
        break;
    }
    case 2: {
        if (l8_c_00158590(o, self, 1, 1)) return;
        uint32_t t = (uint32_t)l8_s16(o, self + 0x28) - 1u;
        l8_w16(o, self + 0x28, t);
        if ((int16_t)t == 0) {
            l8_w8(o, self + 5, l8_u8(o, self + 5) + 1u);
            if (l8_c_001BC150(o, self)) return;
        }
        break;
    }
    case 3:
        if (l8_c_00158590(o, self, 1, 1)) return;
        l8_w8(o, self + 5, l8_u8(o, self + 5) + 1u);
        if (l8_c_001AEBA0(o, 4)) return;
        break;
    case 4:
        if (l8_c_00158590(o, self, 1, 1)) return;
        if (l8_u8(o, D_008106B8) == 0) {
            l8_w8(o, self + 5, 0);
            l8_w8(o, self, 1);
            l8_w8(o, self + 0x0B, 0);
        }
        break;
    default:
        break;
    }
    if (l8_c_001B17A0(o, self)) return;
    l8_callback(o, l8_u32(o, self + 0x4C), self);
}

int em_level8_port_001BD9F0(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_001BD9F0(&o, self);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 001BDE60 (word asm; from the instructions): a door with the wobbling
 * pose (the AREA13 arrival's lift door). By self +4:
 *   3, 2: 001AFC10(self).
 *   0: 001B0FD0(self); +0 = 1; +0x34 (halfword) = +0x2E (byte); +0x2E
 *      (halfword) = 0; self +0x200 (blk +0x10) = 0.
 *   1: by +5 (blk = self +0x1F0):
 *      0  when +0x0B is nonzero: +5 + 1 and 001BBD20(self, 0).
 *      1  001BDCA0(blk) nonzero: +5 + 1, +0x0B = 0, 001BBD20(self, 1).
 *      2  001BDD70(blk) nonzero: +5 = 0.
 *      then 001C6380(self), +1 = 1, 001B1D20(self), the +0x4C method (for
 *      every +5; an unlisted +5 goes straight there).
 *   other: nothing.
 * ---------------------------------------------------------------------- */
static void l8_001BDE60(L8 *o, uint32_t self)
{
    uint32_t state = l8_u8(o, self + 4);
    uint32_t blk = self + 0x1F0;
    if (state == 3 || state == 2) {
        l8_c_001AFC10(o, self);
        return;
    }
    if (state == 0) {
        int32_t ignored = 0;
        if (l8_c_001B0FD0(o, self, &ignored)) return;
        l8_w8(o, self, 1);
        l8_w16(o, self + 0x34, l8_u8(o, self + 0x2E));
        l8_w16(o, self + 0x2E, 0);
        l8_w32(o, blk + 0x10, 0);
        return;
    }
    if (state != 1) return;
    uint32_t sub = l8_u8(o, self + 5);
    if (sub == 2) {
        int32_t r = 0;
        if (l8_c_001BDD70(o, blk, &r)) return;
        if (r) l8_w8(o, self + 5, 0);
    } else if (sub == 1) {
        int32_t r = 0;
        if (l8_c_001BDCA0(o, blk, &r)) return;
        if (r) {
            l8_w8(o, self + 5, l8_u8(o, self + 5) + 1u);
            l8_w8(o, self + 0x0B, 0);
            l8_001BBD20(o, self, 1);
        }
    } else if (sub == 0) {
        if (l8_u8(o, self + 0x0B) != 0) {
            l8_w8(o, self + 5, sub + 1u);
            l8_001BBD20(o, self, 0);
        }
    }
    if (l8_c_001C6380(o, self)) return;
    l8_w8(o, self + 1, 1);
    if (l8_c_001B1D20(o, self)) return;
    l8_callback(o, l8_u32(o, self + 0x4C), self);
}

int em_level8_port_001BDE60(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_001BDE60(&o, self);
    return l8_end(&o);
}
