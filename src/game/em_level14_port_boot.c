/* The fourteenth level: the boot functions the a19d census found new that
 * are not the creature handlers: the player's ceiling-hang entry state
 * 0016AC50 (+5 0x11), the ledge-top test 001782A0, the running jump's
 * target helpers 001AA2A0 / 001AA410, the probe-height scan 001B2D00, the
 * hit-effect spawner 001B4CF0, the effect set-up 001EDE40 and the light
 * helper 001F6BA0. docs/LEVEL14_PORT.md section 2. Ground truth: the
 * original instructions (the test runs every entry against them). */
#include "em_level14_port_internal.h"

#define F_ONE 0x3F800000u
#define F_ZERO 0x00000000u
#define F_3 0x40400000u
#define F_300 0x43960000u
#define F_PI 0x40490FDBu
#define F_HALF_PI 0x3FC90FDBu

/* ------------------------------------------------------------------------
 * 0016AC50 (p): player state 0x11 (entered on surface 0x1E), by +6: 0:
 * +6 = 1, +7 = 0, +0x28 = 8, +0x2E4 = (+0x254 - +0xB4) / 8, 001749A0(p,
 * 0x70, 0, 1.0). 1: on +0x200 bit 0x1000 +6 = 2, 001749A0(p, 0xBA, 0, 1.0),
 * +0x25F = 3. 2: +0x28 zero -> +6 = 3, +0xB4 = +0x254, sound 0x110; else
 * +0xB4 += +0x2E4, +0x28 - 1. 3: on +0x200 bit 0x1000 +5 = 0x12, +6 = 0,
 * +0x1F0 = 0x22, +0x1F1 = +0x25D = +0x2F1 = 0, 001749A0(p, 001885B0(p), 0,
 * 1.0).
 * ---------------------------------------------------------------------- */
int em_level14_port_0016AC50(const H14 *h, uint32_t p, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t st = l14_u8(&o, p + 6);
    if (st == 0) {
        l14_w8(&o, p + 6, st + 1u);
        l14_w8(&o, p + 7, 0);
        l14_w16(&o, p + 0x28, 8);
        l14_w32(&o, p + 0x2E4, L14_DIV(L14_SUB(l14_u32(&o, p + 0x254), l14_u32(&o, p + 0xB4)), 0x41000000u));
        l14_c_001749A0(&o, p, 0x70, 0, F_ONE);
    } else if (st == 1) {
        if (l14_u32(&o, p + 0x200) & 0x1000u) {
            l14_w8(&o, p + 6, st + 1u);
            if (l14_c_001749A0(&o, p, 0xBA, 0, F_ONE)) return -1;
            l14_w8(&o, p + 0x25F, 3);
        }
    } else if (st == 2) {
        if (l14_s16(&o, p + 0x28) == 0) {
            l14_w8(&o, p + 6, st + 1u);
            l14_w32(&o, p + 0xB4, l14_u32(&o, p + 0x254));
            l14_c_001FBD50(&o, p, 0x110, 0, F_300);
        } else {
            uint32_t d = l14_u32(&o, p + 0x2E4);
            l14_w32(&o, p + 0xB4, L14_ADD(l14_u32(&o, p + 0xB4), d));
            l14_w16(&o, p + 0x28, l14_u16(&o, p + 0x28) - 1u);
        }
    } else if (st == 3) {
        if (l14_u32(&o, p + 0x200) & 0x1000u) {
            int32_t clip = 0;
            l14_w8(&o, p + 5, 0x12);
            l14_w8(&o, p + 6, 0);
            l14_w8(&o, p + 0x1F0, 0x22);
            l14_w8(&o, p + 0x1F1, 0);
            l14_w8(&o, p + 0x25D, 0);
            l14_w8(&o, p + 0x2F1, 0);
            if (l14_c_001885B0(&o, p, &clip)) return -1;
            l14_c_001749A0(&o, p, clip, 0, F_ONE);
        }
    }
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 001782A0 (p, sp) -> 1 / 0: the ledge top. 00199FA0(sp - 0x20, sp - 0x10)
 * (two frame vectors) zero -> 0. Else t = (3 + p +0xB4 - the first vector's
 * y) / 3 into 0x70003A20; t below 0 -> 0; else 0x70003A20 += 0.6, p +0x2E4
 * = y + 3 * (float)001281C0(that), 00199DB0(0x700038A0), p +0x2E0 =
 * 0x700038A0, p +0x2E8 = 0x700038A8, p +0x218 = p +0xC4; 1.
 * ---------------------------------------------------------------------- */
int em_level14_port_001782A0(const H14 *h, uint32_t p, uint32_t sp, int32_t *result, F14 *fault)
{
    L14 o;
    if (!result || l14_begin(&o, h, fault)) return -1;
    int32_t r = 0;
    uint32_t buf0 = sp - 0x20u, buf1 = sp - 0x10u;
    if (l14_c_00199FA0(&o, buf0, buf1, &r)) return -1;
    int32_t out = 0;
    if (r != 0) {
        uint32_t py = l14_u32(&o, p + 0xB4);
        uint32_t y = l14_u32(&o, buf0 + 4);
        uint32_t t = L14_DIV(L14_SUB(L14_ADD(F_3, py), y), F_3);
        l14_w32(&o, 0x70003A20u, t);
        if (!L14_LT(t, F_ZERO)) {
            int32_t k = 0;
            uint32_t s = L14_ADD(l14_u32(&o, 0x70003A20u), 0x3F19999Au);
            l14_w32(&o, 0x70003A20u, s);
            if (l14_c_001281C0(&o, s, &k)) return -1;
            l14_w32(&o, p + 0x2E4, L14_ADD(y, L14_MUL(F_3, L14_CVT_S_W(k))));
            if (l14_c_00199DB0(&o, 0x700038A0u)) return -1;
            l14_w32(&o, p + 0x2E0, l14_u32(&o, 0x700038A0u));
            l14_w32(&o, p + 0x2E8, l14_u32(&o, 0x700038A8u));
            l14_w32(&o, p + 0x218, l14_u32(&o, p + 0xC4));
            out = 1;
        }
    }
    if (l14_failed(&o)) return -1;
    *result = out;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001AA2A0 (p, object, radius) -> 1 / 0: the running jump's target test
 * (word assembly; behaviour read from the original's runs,
 * docs/LEVEL14_PORT.md section 2). d = 0011E748(dx * dx + dz * dz) with dx
 * = object +0xB0 - p +0xA0, dz = object +0xB8 - p +0xA8, stored at
 * 0x70003A20 (001AA4E0 reads it back); d above radius -> 0. Then dy =
 * object +0xB4 - p +0xA4 against the kind's band (object +3: 4 or 8 ->
 * [-8, 30], others [-13, 10], both ends inside): below 0 only the lower
 * end is tested, otherwise only the upper one.
 * ---------------------------------------------------------------------- */
int em_level14_port_001AA2A0(const H14 *h, uint32_t p, uint32_t object, float radius, int32_t *result,
                             F14 *fault)
{
    L14 o;
    if (!result || l14_begin(&o, h, fault)) return -1;
    uint32_t ox = l14_u32(&o, object + 0xB0);
    uint32_t dx = L14_SUB(ox, l14_u32(&o, p + 0xA0));
    uint32_t oz = l14_u32(&o, object + 0xB8);
    uint32_t dz = L14_SUB(oz, l14_u32(&o, p + 0xA8));
    uint32_t d = 0;
    if (l14_c_0011E748(&o, L14_ADD(L14_MUL(dx, dx), L14_MUL(dz, dz)), &d)) return -1;
    l14_w32(&o, 0x70003A20u, d);
    int32_t out = 0;
    if (!L14_LT(l14_bits(radius), d)) {
        uint32_t py = l14_u32(&o, p + 0xA4);
        uint32_t dy = L14_SUB(l14_u32(&o, object + 0xB4), py);
        uint32_t kind = l14_u8(&o, object + 3);
        int wide = kind == 4 || kind == 8;
        if (L14_LT(dy, F_ZERO)) out = !L14_LT(dy, wide ? 0xC1000000u : 0xC1500000u);
        else out = !L14_LT(wide ? 0x41F00000u : 0x41200000u, dy);
    }
    if (l14_failed(&o)) return -1;
    *result = out;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001AA410 (object) -> the target's reach by its kind +3 (word assembly;
 * behaviour read from the original's runs over every kind): 1, 2, 4, 5,
 * 7, 0xC -> 20.0; 6 -> 35.0; 8 -> 30.0; others 0.0.
 * ---------------------------------------------------------------------- */
int em_level14_port_001AA410(const H14 *h, uint32_t object, float *result, F14 *fault)
{
    L14 o;
    if (!result || l14_begin(&o, h, fault)) return -1;
    uint32_t kind = l14_u8(&o, object + 3), v = F_ZERO;
    if (kind == 1 || kind == 2 || kind == 4 || kind == 5 || kind == 7 || kind == 0xC) v = 0x41A00000u;
    else if (kind == 6) v = 0x420C0000u;
    else if (kind == 8) v = 0x41F00000u;
    if (l14_failed(&o)) return -1;
    *result = l14_float(v);
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B2D00 (a0, a1) -> 1 / 0: 0019BC40(a0), then over the column table's n
 * = *0x700031E0 heights (0x700030F0, flags 0x70003170): the first height
 * not below a0 +4: flag bit 0 -> *a1 = it, 1; else 0. None: the last one's
 * flag set -> 0; else (not the first) the one before it with its flag ->
 * *a1 = it, 1; else 0.
 * ---------------------------------------------------------------------- */
int em_level14_port_001B2D00(const H14 *h, uint32_t a0, uint32_t a1, int32_t *result, F14 *fault)
{
    L14 o;
    if (!result || l14_begin(&o, h, fault)) return -1;
    int32_t out = 0;
    if (l14_c_0019BC40(&o, a0)) return -1;
    int32_t n = l14_s32(&o, 0x700031E0u);
    if (n != 0) {
        int32_t i = 0;
        int found = 0;
        for (; i < n; i++) {
            if (!L14_LT(l14_u32(&o, 0x700030F0u + 4u * (uint32_t)i), l14_u32(&o, a0 + 4))) {
                found = 1;
                break;
            }
        }
        if (found) {
            if (l14_u16(&o, 0x70003170u + 2u * (uint32_t)i) & 1u) {
                l14_w32(&o, a1, l14_u32(&o, 0x700030F0u + 4u * (uint32_t)i));
                out = 1;
            }
        } else {
            int32_t j = i - 1;
            if (!(l14_u16(&o, 0x70003170u + 2u * (uint32_t)j) & 1u) && j != 0) {
                if (l14_u16(&o, 0x7000316Eu + 2u * (uint32_t)j) & 1u) {
                    l14_w32(&o, a1, l14_u32(&o, 0x700030ECu + 4u * (uint32_t)j));
                    out = 1;
                }
            }
        }
    }
    if (l14_failed(&o)) return -1;
    *result = out;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B4CF0 (p): the hit effects of a creature by its kind +3 (1, 3, 4, 5,
 * 6, 7, 9, 10, 11; others nothing): an offset (x, y, z, 1) into 0x70003600
 * (by +0xD bits 0 / 7), a bone index and an effect id (bit 7), the angles
 * p +0xC0 copied to 0x70003610 (00102948; kinds 6 / 9 / 10 turn its y by pi
 * / pi/2 / pi through 001B1470); the offset through bone idx's matrix
 * (*D_00275B40)[idx] +0x90 (001026A0), 001EFD90(0x80000036 / id /
 * 0x80000025, 0x70003600, 0x70003610), sounds 0x15C / 0x15D.
 * ---------------------------------------------------------------------- */
static void b_offset(L14 *o, uint32_t x, uint32_t y, uint32_t z)
{
    l14_w32(o, 0x70003600u, x);
    l14_w32(o, 0x70003604u, y);
    l14_w32(o, 0x70003608u, z);
    l14_w32(o, 0x7000360Cu, F_ONE);
}

static int b_turn(L14 *o, uint32_t by)
{
    uint32_t a = 0;
    if (l14_c_001B1470(o, L14_ADD(by, l14_u32(o, 0x70003614u)), &a)) return -1;
    l14_w32(o, 0x70003614u, a);
    return 0;
}

int em_level14_port_001B4CF0(const H14 *h, uint32_t p, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t kind = l14_u8(&o, p + 3), idx = 0, sid = 0, turn = 0;
    switch (kind) {
    case 1:
        idx = 4;
        {
            uint32_t d = l14_u8(&o, p + 0xD);
            if (d & 1u) {
                if (d & 0x80u) b_offset(&o, 0x40091687u, 0x3FFBE76Du, 0xBD71A9FCu);
                else b_offset(&o, 0x4009BA5Eu, 0x3FFA1CACu, 0xBD48B439u);
            } else {
                if (d & 0x80u) b_offset(&o, 0x400B53F8u, 0x3FF2B021u, F_ZERO);
                else b_offset(&o, 0x4003D70Au, 0x4006F9DBu, F_ZERO);
            }
        }
        sid = (l14_u8(&o, p + 0xD) & 0x80u) ? 0x8000006Fu : 0x80000070u;
        break;
    case 3:
        sid = 0x80000070u;
        b_offset(&o, 0x3F8B645Au, 0x3F50E560u, F_ZERO);
        idx = 2;
        break;
    case 4:
        sid = (l14_u8(&o, p + 0xD) & 0x80u) ? 0x80000071u : 0x80000070u;
        b_offset(&o, 0x3EAB851Fu, 0x3F11A9FCu, F_ZERO);
        idx = 2;
        break;
    case 5:
        idx = 0xD;
        sid = (l14_u8(&o, p + 0xD) & 0x80u) ? 0x80000071u : 0x80000070u;
        b_offset(&o, 0x4018E560u, 0xBF27AE14u, F_ZERO);
        break;
    case 6:
        idx = 5;
        if (l14_u8(&o, p + 0xD) & 0x80u) {
            sid = 0x80000071u;
            b_offset(&o, 0xBEA2D0E5u, 0xC02ED917u, F_ZERO);
        } else {
            sid = 0x80000070u;
            b_offset(&o, F_ZERO, 0xC001999Au, F_ZERO);
        }
        turn = F_PI;
        break;
    case 7:
        idx = 0xB;
        if (l14_u8(&o, p + 0xD) & 0x80u) {
            sid = 0x80000071u;
            b_offset(&o, 0x404147AEu, 0x3EE147AEu, F_ZERO);
        } else {
            sid = 0x80000070u;
            b_offset(&o, 0x4001BA5Eu, F_ZERO, F_ZERO);
        }
        break;
    case 9:
        sid = 0x8000006Fu;
        b_offset(&o, 0x401B79A7u, 0xBFF367A1u, 0xBE30A3D7u);
        idx = 0x1C;
        turn = F_HALF_PI;
        break;
    case 10:
        sid = 0x80000058u;
        b_offset(&o, 0x41EE7EFAu, 0xBF6F5C29u, F_ZERO);
        idx = 0x10;
        turn = F_PI;
        break;
    case 11:
        sid = 0x80000071u;
        b_offset(&o, 0x4138C8B4u, 0x4185E148u, 0xBC03126Fu);
        idx = 2;
        break;
    default:
        return l14_end(&o);
    }
    if (l14_c_00102948(&o, 0x70003610u, p + 0xC0)) return -1;
    if (turn && b_turn(&o, turn)) return -1;
    uint32_t bones = l14_u32(&o, 0x00275B40u);
    if (l14_c_001026A0(&o, 0x70003600u, l14_u32(&o, bones + 4u * idx) + 0x90u, 0x70003600u)) return -1;
    if (l14_c_001EFD90(&o, (int32_t)0x80000036u, 0x70003600u, 0x70003610u)) return -1;
    if (l14_c_001EFD90(&o, (int32_t)sid, 0x70003600u, 0x70003610u)) return -1;
    if (l14_c_001EFD90(&o, (int32_t)0x80000025u, 0x70003600u, 0x70003610u)) return -1;
    if (l14_c_001FBD50(&o, p, 0x15C, 0, F_300)) return -1;
    l14_c_001FBD50(&o, p, 0x15D, 0, F_300);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 001EDE40 (a0, a1): the three effect records' parameter slots (+0x20 ..
 * +0x3C of 0x257510 / 0x2575A0 / 0x257630), then per record: the
 * generator state *D_00275C34: r = +4, phase = (float)(r >> 16 & 0xFFFF) /
 * 65535 + 1e-4, +4 = r * 37 + 11, 001CFB50(0x81F8F0, 0, a0, +0x54, phase,
 * 1.0, 1e-6, 10.0), 001CFBE0(a1, 1, record, 0x81F8F0, 1) (the original
 * passes 1 as the fifth argument; the NEARMISS text has 0); then +8 eases
 * toward 0.02 by a tenth, floored at 0.02.
 * ---------------------------------------------------------------------- */
static const uint32_t EDE40_SLOTS[24][2] = {
    {0x00257530u, 0x41800000u}, {0x00257534u, 0x41400000u}, {0x00257538u, 0x41800000u}, {0x0025753Cu, 0},
    {0x00257540u, 0}, {0x00257544u, 0}, {0x00257548u, 0}, {0x0025754Cu, 0},
    {0x002575C0u, 0x43000000u}, {0x002575C4u, 0}, {0x002575C8u, 0x43000000u}, {0x002575CCu, 0x42C00000u},
    {0x002575D0u, 0x42C00000u}, {0x002575D4u, 0}, {0x002575D8u, 0x42C00000u}, {0x002575DCu, 0},
    {0x00257650u, 0x43000000u}, {0x00257654u, 0}, {0x00257658u, 0x43000000u}, {0x0025765Cu, 0x42C00000u},
    {0x00257660u, 0x42C00000u}, {0x00257664u, 0}, {0x00257668u, 0x42C00000u}, {0x0025766Cu, 0},
};

int em_level14_port_001EDE40(const H14 *h, uint32_t a0, uint32_t a1, F14 *fault)
{
    static const uint32_t records[3] = {0x00257510u, 0x002575A0u, 0x00257630u};
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t st = 0;
    for (int i = 0; i < 24; i++) {
        if (i == 23) st = l14_u32(&o, 0x00275C34u);   /* the state pointer is read before the last slot */
        l14_w32(&o, EDE40_SLOTS[i][0], EDE40_SLOTS[i][1]);
    }
    for (int k = 0; k < 3; k++) {
        if (k) st = l14_u32(&o, 0x00275C34u);
        uint32_t r = l14_u32(&o, st + 4);
        uint32_t f = L14_ADD(L14_DIV(L14_CVT_S_W((r >> 16) & 0xFFFFu), 0x477FFF00u), 0x38D1B717u);
        l14_w32(&o, st + 4, r * 0x25u + 0xBu);
        uint32_t s54 = l14_u32(&o, l14_u32(&o, 0x00275C34u) + 0x54);
        if (l14_c_001CFB50(&o, 0x0081F8F0u, 0, a0, s54, f, F_ONE, 0x358637BDu, 0x41200000u)) return -1;
        if (l14_c_001CFBE0(&o, (int32_t)a1, 1, records[k], 0x0081F8F0u, 1)) return -1;
    }
    st = l14_u32(&o, 0x00275C34u);
    uint32_t t = l14_u32(&o, st + 8);
    l14_w32(&o, st + 8, L14_ADD(t, L14_DIV(L14_SUB(0x3CA3D70Au, t), 0x41200000u)));
    st = l14_u32(&o, 0x00275C34u);
    uint32_t v = l14_u32(&o, st + 8);
    if (L14_LT(v, 0x3CA3D70Au)) v = 0x3CA3D70Au;
    l14_w32(&o, st + 8, v);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 001F6BA0: 001F66F0(0x25D270) (a tail jump).
 * ---------------------------------------------------------------------- */
int em_level14_port_001F6BA0(const H14 *h, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    l14_c_001F66F0(&o, 0x0025D270u);
    return l14_end(&o);
}
