/* The fourteenth level: the 0012E3A0 creatures' state handlers the a19d
 * census found new (00131650 state 2 with its pieces 00131740 / 00131B10 /
 * 001339E0, 00131E80 state 3, the lunge point 00131F20) and the AREA15
 * enemy 00153950 with its states 00153A10 / 00153A90.
 * docs/LEVEL14_PORT.md section 2. Ground truth: the original instructions
 * (the test runs every entry against them). */
#include "em_level14_port_internal.h"

#define F_ONE 0x3F800000u
#define F_ZERO 0x00000000u
#define F_5 0x40A00000u
#define F_300 0x43960000u

/* ------------------------------------------------------------------------
 * 00131740 (self, ent): 00131650's +5 0. +6 0: +6 + 1, ent +0x63 = 0, ent
 * +0x3C = 0, ent +0x34 = 1.0, sound 0x7D4; with ent +0x61 set, the +0x2C
 * byte 4 and (float)001C6160(self) - +0x3C >= 65: ent +0x61 = 0; ent +0x61
 * still set -> clip 0x27 (001C67E0 at 0, 0); else +0 = 1 when +0x34 is
 * nonzero, then +0x36 bit 15 -> clip 0x28 and sound 0x7D6, else the clip
 * D_00275390[(rand >> 11) % 3]; +0x36 = 0. +6 1: ent +0x58 bit 0x1000 ->
 * with +0x34 nonzero the reset (+0 = 1, +0x36 = 0, +4 = 1, +5 = +6 = 0,
 * ent +0x62 = 0, ent +0x6A = 0x3C), else +5 = 2, +6 = 0; without the bit
 * and ent +0x61 0: 00133A20(self, ent).
 * ---------------------------------------------------------------------- */
void l14_00131740(L14 *o, uint32_t self, uint32_t ent)
{
    uint32_t st = l14_u8(o, self + 6);
    if (st == 0) {
        l14_w8(o, self + 6, st + 1u);
        l14_w8(o, ent + 0x63, 0);
        l14_w32(o, ent + 0x3C, 0);
        l14_w32(o, ent + 0x34, F_ONE);
        if (l14_c_001FBD50(o, self, 0x7D4, 0, F_300)) return;
        if (l14_u8(o, ent + 0x61) != 0 && l14_u8(o, self + 0x2C) == 4) {
            int32_t t = 0;
            if (l14_c_001C6160(o, self, &t)) return;
            if (!L14_LT(L14_SUB(L14_CVT_S_W(t), l14_u32(o, self + 0x3C)), 0x42820000u)) l14_w8(o, ent + 0x61, 0);
        }
        if (l14_u8(o, ent + 0x61) != 0) {
            l14_c_001C67E0(o, self, 0x27, F_ZERO, F_ZERO);
            return;
        }
        if (l14_s16(o, self + 0x34) != 0) l14_w8(o, self, 1);
        if (l14_u16(o, self + 0x36) & 0x8000u) {
            if (l14_c_001C67E0(o, self, 0x28, F_ZERO, F_ZERO)) return;
            if (l14_c_001FBD50(o, self, 0x7D6, 0, F_300)) return;
        } else {
            int32_t r = 0;
            if (l14_c_00122BB8(o, &r)) return;
            int32_t k = l14_sra((uint32_t)r, 11) % 3;
            if (l14_c_001C67E0(o, self, l14_s16(o, 0x00275390u + 2u * (uint32_t)k), F_ZERO, F_ZERO)) return;
        }
        l14_w16(o, self + 0x36, 0);
    } else if (st == 1) {
        if (l14_u16(o, ent + 0x58) & 0x1000u) {
            if (l14_s16(o, self + 0x34) != 0) {
                l14_w8(o, self, 1);
                l14_w16(o, self + 0x36, 0);
                l14_w8(o, self + 4, 1);
                l14_w8(o, self + 5, 0);
                l14_w8(o, self + 6, 0);
                l14_w8(o, ent + 0x62, 0);
                l14_w8(o, ent + 0x6A, 0x3C);
                return;
            }
            l14_w8(o, self + 5, 2);
            l14_w8(o, self + 6, 0);
            return;
        }
        if (l14_u8(o, ent + 0x61) == 0) l14_c_00133A20(o, self, ent);
    }
}

int em_level14_port_00131740(const H14 *h, uint32_t self, uint32_t ent, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    l14_00131740(&o, self, ent);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00131B10 (self, ent): 00131650's +5 2. +6 0: +6 + 1, 001B1190(+0x9A),
 * sound 0x7D8; +0xD bit 1 -> +6 = 3, ent +0x34 = 1.0, D_008107EA = 1, clip
 * 4 / 5 by ent +0x61; else ent +0x34 = 1.0, clip 0x29 with ent +0x61, else
 * 0x2A / 0x2B by rand bit 9 (clips at 5.0, 0). +6 1: the clip (+0x2C
 * without bit 15) 0x2A with +0x3C == 34 or 0x2B with 21 -> sound 0x7D5; on
 * ent +0x58 bit 0x1000 +6 + 1 and, without +0xD bit 1, ent +0x6D = 0, sound
 * 0x7D7, 001EFE00(0x8000001E, self) zero -> +4 = 3. +6 2: +0xD bit 1,
 * D_0028A9A2 set and D_0028A9A0 == 2 -> +4 = 3 and the +0x24 object's +4 =
 * 3, +0x24 = 0. +6 3: D_008107EA == 2 -> +6 = 1, ent +0x34 = 0.35, +0x24 =
 * 001EFE00(0x80000046, self), clip 0x2A / 0x2B by rand bit 9.
 * ---------------------------------------------------------------------- */
void l14_00131B10(L14 *o, uint32_t self, uint32_t ent)
{
    uint32_t st = l14_u8(o, self + 6);
    int32_t r = 0;
    if (st == 0) {
        l14_w8(o, self + 6, st + 1u);
        if (l14_c_001B1190(o, (int32_t)l14_u8(o, self + 0x9A))) return;
        if (l14_c_001FBD50(o, self, 0x7D8, 0, F_300)) return;
        if (l14_u8(o, self + 0xD) & 2u) {
            l14_w8(o, self + 6, 3);
            l14_w32(o, ent + 0x34, F_ONE);
            l14_w8(o, 0x008107EAu, 1);
            l14_c_001C67E0(o, self, l14_u8(o, ent + 0x61) != 0 ? 4 : 5, F_5, F_ZERO);
            return;
        }
        l14_w32(o, ent + 0x34, F_ONE);
        if (l14_u8(o, ent + 0x61) != 0) {
            l14_c_001C67E0(o, self, 0x29, F_5, F_ZERO);
            return;
        }
        if (l14_c_00122BB8(o, &r)) return;
        l14_c_001C67E0(o, self, (l14_sra((uint32_t)r, 9) & 1) ? 0x2A : 0x2B, F_5, F_ZERO);
    } else if (st == 1) {
        uint32_t clip = (uint32_t)l14_s16(o, self + 0x2C) & 0xFFFF7FFFu;
        if (clip == 0x2A && L14_EQ(l14_u32(o, self + 0x3C), 0x42080000u)) {
            if (l14_c_001FBD50(o, self, 0x7D5, 0, F_300)) return;
        } else if (clip == 0x2B && L14_EQ(l14_u32(o, self + 0x3C), 0x41A80000u)) {
            if (l14_c_001FBD50(o, self, 0x7D5, 0, F_300)) return;
        }
        if (l14_failed(o)) return;
        if (l14_u16(o, ent + 0x58) & 0x1000u) {
            l14_w8(o, self + 6, l14_u8(o, self + 6) + 1u);
            if (!(l14_u8(o, self + 0xD) & 2u)) {
                uint32_t p = 0;
                l14_w8(o, ent + 0x6D, 0);
                if (l14_c_001FBD50(o, self, 0x7D7, 0, F_300)) return;
                if (l14_c_001EFE00(o, (int32_t)0x8000001Eu, self, &p)) return;
                if (p == 0) l14_w8(o, self + 4, 3);
            }
        }
    } else if (st == 2) {
        if ((l14_u8(o, self + 0xD) & 2u) && l14_u8(o, 0x0028A9A2u) != 0 && l14_s16(o, 0x0028A9A0u) == 2) {
            l14_w8(o, self + 4, 3);
            uint32_t link = l14_u32(o, self + 0x24);
            if (link != 0) {
                l14_w8(o, link + 4, 3);
                l14_w32(o, self + 0x24, 0);
            }
        }
    } else if (st == 3) {
        if (l14_u8(o, 0x008107EAu) == 2) {
            uint32_t p = 0;
            l14_w8(o, self + 6, 1);
            l14_w32(o, ent + 0x34, 0x3EB33333u);
            if (l14_c_001EFE00(o, (int32_t)0x80000046u, self, &p)) return;
            l14_w32(o, self + 0x24, p);
            if (l14_c_00122BB8(o, &r)) return;
            l14_c_001C67E0(o, self, (l14_sra((uint32_t)r, 9) & 1) ? 0x2A : 0x2B, F_5, F_ZERO);
        }
    }
}

int em_level14_port_00131B10(const H14 *h, uint32_t self, uint32_t ent, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    l14_00131B10(&o, self, ent);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 001339E0 (a0, ent): with ent +0x65 set: ent +0x65 = 0, 0021C040(0x8102B0,
 * a0).
 * ---------------------------------------------------------------------- */
void l14_001339E0(L14 *o, uint32_t a0, uint32_t ent)
{
    if (l14_u8(o, ent + 0x65) == 0) return;
    l14_w8(o, ent + 0x65, 0);
    l14_c_0021C040(o, 0x008102B0u, a0);
}

int em_level14_port_001339E0(const H14 *h, uint32_t a0, uint32_t ent, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    l14_001339E0(&o, a0, ent);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00131650 (self, ent): state 2 (word assembly; behaviour read from the
 * original's runs over every sub-state, docs/LEVEL14_PORT.md section 2).
 * By +5: 0 00131740(self, ent), 1 00131940(self, ent), 2 00131B10(self,
 * ent), others nothing. Then the +0x20 object (if any) gets +4 = 3 and
 * +0x20 = 0; 001339E0(self, ent); 00132490(self, ent), 001328D0(self, ent);
 * ent +0x58 (halfword) = 001C64F0(self, ent +0x34); 00131ED0, 001C68C0,
 * 001B17A0, the +0x4C method.
 * ---------------------------------------------------------------------- */
int em_level14_port_00131650(const H14 *h, uint32_t self, uint32_t ent, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t sub = l14_u8(&o, self + 5);
    if (sub == 0) l14_00131740(&o, self, ent);
    else if (sub == 1) l14_c_00131940(&o, self, ent);
    else if (sub == 2) l14_00131B10(&o, self, ent);
    if (l14_failed(&o)) return -1;
    uint32_t pending = l14_u32(&o, self + 0x20);
    if (pending) {
        l14_w8(&o, pending + 4, 3);
        l14_w32(&o, self + 0x20, 0);
    }
    l14_001339E0(&o, self, ent);
    if (l14_failed(&o)) return -1;
    if (l14_c_00132490(&o, self, ent)) return -1;
    if (l14_c_001328D0(&o, self, ent)) return -1;
    int32_t r = 0;
    if (l14_c_001C64F0(&o, self, l14_u32(&o, ent + 0x34), &r)) return -1;
    l14_w16(&o, ent + 0x58, (uint32_t)r);
    if (l14_c_00131ED0(&o, self)) return -1;
    if (l14_c_001C68C0(&o, self)) return -1;
    if (l14_c_001B17A0(&o, self, &r)) return -1;
    l14_method(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00131E80 (self): state 3: the +0x20 object (if any) gets +4 = 3 and +0x20
 * = 0; 001B1190(+0x9A), 001AFC10(self).
 * ---------------------------------------------------------------------- */
int em_level14_port_00131E80(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t pending = l14_u32(&o, self + 0x20);
    if (pending) {
        l14_w8(&o, pending + 4, 3);
        l14_w32(&o, self + 0x20, 0);
    }
    if (l14_c_001B1190(&o, (int32_t)l14_u8(&o, self + 0x9A))) return -1;
    l14_c_001AFC10(&o, self);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00131F20 (self, a1, a2): 001029C0(0x700036A0), 00102C58(0x700036A0,
 * 0x700036A0, self + 0xC0), 001026A0(a2, 0x700036A0, a1): a1 through the
 * matrix of self's +0xC0 angles into a2.
 * ---------------------------------------------------------------------- */
int em_level14_port_00131F20(const H14 *h, uint32_t self, uint32_t a1, uint32_t a2, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    if (l14_c_001029C0(&o, 0x700036A0u)) return -1;
    if (l14_c_00102C58(&o, 0x700036A0u, 0x700036A0u, self + 0xC0)) return -1;
    l14_c_001026A0(&o, a2, 0x700036A0u, a1);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00153A10 (self, ent): +4 + 1, +0 = 1, ent +0x10 = 1, +0x34 = 0x3C, +0x30 =
 * 0x275440; 001B10B0(self, 0x72, 0x71) zero -> 001C63E0(self, 0x33), +0x58
 * = D_0028A65C.
 * ---------------------------------------------------------------------- */
void l14_00153A10(L14 *o, uint32_t self, uint32_t ent)
{
    int32_t r = 0;
    l14_w8(o, self + 4, l14_u8(o, self + 4) + 1u);
    l14_w8(o, self, 1);
    l14_w8(o, ent + 0x10, 1);
    l14_w16(o, self + 0x34, 0x3C);
    l14_w32(o, self + 0x30, 0x00275440u);
    if (l14_c_001B10B0(o, self, 0x72, 0x71, &r)) return;
    if (r == 0) {
        if (l14_c_001C63E0(o, self, 0x33)) return;
        l14_w32(o, self + 0x58, l14_u32(o, 0x0028A65Cu));
    }
}

int em_level14_port_00153A10(const H14 *h, uint32_t self, uint32_t ent, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    l14_00153A10(&o, self, ent);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00153A90 (self, out): the hit mailbox +0x36: set with D_0081077B 0 ->
 * +0 = 1, +0x36 = 0; set otherwise -> +4 = 2, +5 = 0. Then unless
 * D_0081077B is 1 with D_008107FB 0: *out = 001C64F0(self, 1.0),
 * 00131ED0, 001C68C0, 001B17A0, the +0x4C method.
 * ---------------------------------------------------------------------- */
void l14_00153A90(L14 *o, uint32_t self, uint32_t out)
{
    if (l14_s16(o, self + 0x36) != 0) {
        if (l14_u8(o, 0x0081077Bu) == 0) {
            l14_w8(o, self, 1);
            l14_w16(o, self + 0x36, 0);
        } else {
            l14_w8(o, self + 4, 2);
            l14_w8(o, self + 5, 0);
        }
    }
    if (l14_u8(o, 0x0081077Bu) != 1 || l14_u8(o, 0x008107FBu) != 0) {
        int32_t r = 0;
        if (l14_c_001C64F0(o, self, F_ONE, &r)) return;
        l14_w32(o, out, (uint32_t)r);
        if (l14_c_00131ED0(o, self)) return;
        if (l14_c_001C68C0(o, self)) return;
        if (l14_c_001B17A0(o, self, &r)) return;
        l14_method(o, self);
    }
}

int em_level14_port_00153A90(const H14 *h, uint32_t self, uint32_t out, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    l14_00153A90(&o, self, out);
    return l14_end(&o);
}

/* ------------------------------------------------------------------------
 * 00153950 (self): the AREA15 enemy's behaviour (word assembly; behaviour
 * read from the original's runs over every state, docs/LEVEL14_PORT.md
 * section 2). ent = self + 0x1F0. By +4: 0 00153A10(self, ent), 1
 * 00153A90(self, ent), 2 00153B50(self, ent), 3 00153EA0(self, ent) and
 * nothing more; then (not 3) with ent +0x10 set 001B5360(self).
 * ---------------------------------------------------------------------- */
int em_level14_port_00153950(const H14 *h, uint32_t self, F14 *fault)
{
    L14 o;
    if (l14_begin(&o, h, fault)) return -1;
    uint32_t ent = self + 0x1F0;
    uint32_t st = l14_u8(&o, self + 4);
    if (st == 3) {
        l14_c_00153EA0(&o, self, ent);
        return l14_end(&o);
    }
    if (st == 0) l14_00153A10(&o, self, ent);
    else if (st == 1) l14_00153A90(&o, self, ent);
    else if (st == 2) l14_c_00153B50(&o, self, ent);
    if (l14_failed(&o)) return -1;
    if (l14_u8(&o, ent + 0x10) != 0) l14_c_001B5360(&o, self);
    return l14_end(&o);
}
