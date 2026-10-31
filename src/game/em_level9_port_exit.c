/* The ninth level's exit segment (a13_exit: the AREA19 load at entry 9)
 * and the boot functions it runs (see em_level9_port.h,
 * docs/LEVEL9_PORT.md): 001383C0, 00138540, 001386E0 (AREA19's creature
 * behaviour, three live at the arrival), 00154460, 001546C0, 00154740,
 * 001549C0 (the kind-0xE field, six live), 0015A200 (its spawn), 00183440
 * and 001838B0 (two player stage workers), 00196970 and 00196CE0 (AREA19's
 * camera at the ladder).
 *
 * Ground truth: the original instructions; the decomp's C where it is
 * byte-identical (00138540, 001386E0, 00154460, 001546C0, 00154740,
 * 001549C0, 0015A200, 001838B0, 00196970) was the guide, and for
 * 001383C0 / 00196CE0 (NEARMISS) and 00183440 (inline asm) the
 * instructions alone. Where the NEARMISS text and the instructions differ
 * the instructions win:
 *   - 00196CE0 calls 001916C0 with a0 = its own record (unchanged from its
 *     entry), a1 = the player record and a2 = 0 (the text passes 0 in a0);
 *     its "outside both circles and region 5" test is !(d < 225), and its
 *     state-2 step-back test is !(y < table[idx]) (NaN-exact).
 */
#include "em_level9_port_internal.h"

#define D_0081070A 0x0081070Au
#define D_00810808 0x00810808u
#define D_0028A678 0x0028A678u
#define D_002753B0 0x002753B0u
#define D_0028A4E4 0x0028A4E4u
#define D_00275BCC 0x00275BCCu
#define D_00275450 0x00275450u
#define D_00246800 0x00246800u
#define D_00248120 0x00248120u
#define D_00248124 0x00248124u
#define D_00248128 0x00248128u
#define D_0026D320 0x0026D320u
#define D_008105D0 0x008105D0u
#define D_008105E0 0x008105E0u
#define D_008105E8 0x008105E8u
#define D_0024A6B0 0x0024A6B0u
#define D_0024A6B4 0x0024A6B4u
#define D_0024A6B8 0x0024A6B8u
#define D_0024A6BC 0x0024A6BCu
#define PLAYER D_008102B0

/* ------------------------------------------------------------------------
 * 001383C0 (AREA19's creature behaviour; NEARMISS, from the instructions).
 * ent = self +0x1F0. By the byte 0x70003B8D (the pause / cinematic mode):
 *   2, 3: return.
 *   1: 001B2140(self) nonzero and +4 nonzero: the +0x4C method. Return.
 *   0, 4 and others: by +4: 0 -> 00138540(self, ent), 1 -> 001386E0,
 *   2 -> 0013B350, 3 -> 0013B9A0 and return; others none. Then ent +0x85
 *   (signed byte) and ent +0x34 (halfword) count down to 0, 001B0D80(self),
 *   and with ent +0x87 nonzero and not (D_00810700 == 0x13 and D_00810701
 *   == 0): 001B5360(self).
 * ---------------------------------------------------------------------- */
int em_level9_port_001383C0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (l9_begin(&o, h, fault)) return -1;
    uint32_t ent = self + 0x1F0;
    uint32_t mode = l9_u8(&o, S_70003B8D);
    if (mode == 3 || mode == 2) return l9_end(&o);
    if (mode == 1) {
        if (l9_c_001B2140(&o, self, &r)) return -1;
        if (r != 0 && l9_u8(&o, self + 4) != 0) l9_method(&o, self);
        return l9_end(&o);
    }
    switch (l9_u8(&o, self + 4)) {
    case 0: l9_00138540(&o, self, ent); break;
    case 1: l9_001386E0(&o, self, ent); break;
    case 2: l9_c_0013B350(&o, self, ent); break;
    case 3: l9_c_0013B9A0(&o, self, ent); return l9_end(&o);
    default: break;
    }
    if (l9_failed(&o)) return -1;
    {
        int32_t c = l9_s8(&o, ent + 0x85);
        if (c != 0) l9_w8(&o, ent + 0x85, (uint32_t)(c - 1));
    }
    {
        uint32_t v = l9_u16(&o, ent + 0x34);
        if (v != 0) l9_w16(&o, ent + 0x34, v - 1u);
    }
    {
        int32_t ignored = 0;
        if (l9_c_001B0D80(&o, self, &ignored)) return -1;
    }
    if (l9_s8(&o, ent + 0x87) != 0) {
        if (l9_u8(&o, D_00810700) != 0x13u || l9_u8(&o, D_00810701) != 0) l9_c_001B5360(&o, self);
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 00138540 (p, q = p +0x1F0): the creature's spawn. p +4 += 1; q +0x88 =
 * p +0x0D; q +0x87 = 1; p +0x20 = p +0xC0 = p +0xC8 = 0; D_00810808 ==
 * 0xFF: p +0x0D |= 0x80. With bit 7 of +0x0D: +0x34 = 0x96 (D_0081070A
 * set) or 0x50, the scale +0x60..+0x6C = (1.3, 1.3, 1.3, 1.0),
 * 001B10B0(p, 0x7B, 0x7C); else +0x34 = 0x3C or 0x28, 001B10B0(p, 0x79,
 * 0x7C). A nonzero result stops here. Then 001C63E0(p, 1), +0 = 1, +0x30
 * = 0x2753B0, +0x5E = 0, +0x58 = D_0028A678, 00102948(q, p +0xB0),
 * 00102948(q +0x10, p +0xC0), q +0x82 = 0, q +0x4C = 0.4, q +0x54 =
 * 0.0437, and q +0x20 = (short)0x70003B8A + ((00122BB8() >> 11) & 0xF).
 * ---------------------------------------------------------------------- */
void l9_00138540(L9 *o, uint32_t p, uint32_t q)
{
    int32_t r = 0;
    l9_w8(o, p + 4, l9_u8(o, p + 4) + 1u);
    l9_w8(o, q + 0x88, l9_u8(o, p + 0x0D));
    l9_w8(o, q + 0x87, 1);
    l9_w32(o, p + 0x20, 0);
    l9_w32(o, p + 0xC0, 0);
    l9_w32(o, p + 0xC8, 0);
    if (l9_u8(o, D_00810808) == 0xFFu) l9_w8(o, p + 0x0D, l9_u8(o, p + 0x0D) | 0x80u);
    if (l9_u8(o, p + 0x0D) & 0x80u) {
        l9_w16(o, p + 0x34, l9_u8(o, D_0081070A) != 0 ? 0x96u : 0x50u);
        l9_w32(o, p + 0x60, 0x3FA66666u);
        l9_w32(o, p + 0x64, 0x3FA66666u);
        l9_w32(o, p + 0x68, 0x3FA66666u);
        l9_w32(o, p + 0x6C, F_ONE);
        if (l9_c_001B10B0(o, p, 0x7B, 0x7C, &r) || r != 0) return;
    } else {
        l9_w16(o, p + 0x34, l9_u8(o, D_0081070A) != 0 ? 0x3Cu : 0x28u);
        if (l9_c_001B10B0(o, p, 0x79, 0x7C, &r) || r != 0) return;
    }
    if (l9_c_001C63E0(o, p, 1)) return;
    l9_w8(o, p + 0, 1);
    l9_w32(o, p + 0x30, D_002753B0);
    l9_w8(o, p + 0x5E, 0);
    l9_w32(o, p + 0x58, l9_u32(o, D_0028A678));
    if (l9_c_00102948(o, q, p + 0xB0)) return;
    if (l9_c_00102948(o, q + 0x10, p + 0xC0)) return;
    l9_w8(o, q + 0x82, 0);
    l9_w32(o, q + 0x4C, 0x3ECCCCCDu);
    l9_w32(o, q + 0x54, 0x3D32B8C3u);
    if (l9_c_00122BB8(o, &r)) return;
    {
        uint32_t nib = (uint32_t)l9_sra((uint32_t)r, 11) & 0xFu;
        l9_w16(o, q + 0x20, (uint32_t)l9_s16(o, S_70003B8A) + nib);
    }
}

int em_level9_port_00138540(const EmLevel9PortHooks *h, uint32_t p, uint32_t q, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    l9_00138540(&o, p, q);
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 001386E0 (self, ent): the creature's live frame, only while
 * 001B2140(self) is nonzero. By +5 (a jump table at 0x26D1C0, 6 entries):
 * 00138900 / 00138C20 / 00139240 / 001399F0 / 00139E00 / 0013A3B0 (self,
 * ent). Then ent +0x86 (byte), +0x32 and +0x2E (halfwords) count down to
 * 0; ent +0x84 nonzero: +0x5D = 0x81 and 001B4810(self), else +0x5D = 1;
 * +0x36 nonzero: ent +0x34 or ent +0x83 nonzero: +0 = 1, +0x36 = 0; else
 * +4 = 2, +5 = 0, +6 = 0. ent +0x70 (byte) == 8: 001FBD50(self, 0x821 +
 * (00122BB8() >> 17) % 5, 0, 300). Then +0x0A = 0, 0013BF20(self, ent),
 * 0013BE60(self, ent), ent +0x70 = 001C64F0(self, 1.0), 00131ED0(self),
 * 001C6910(self), 001B17A0(self), the +0x4C method.
 * ---------------------------------------------------------------------- */
void l9_001386E0(L9 *o, uint32_t self, uint32_t ent)
{
    int32_t r = 0;
    if (l9_c_001B2140(o, self, &r) || r == 0) return;
    switch (l9_u8(o, self + 5)) {
    case 0: if (l9_c_00138900(o, self, ent)) return; break;
    case 1: if (l9_c_00138C20(o, self, ent)) return; break;
    case 2: if (l9_c_00139240(o, self, ent)) return; break;
    case 3: if (l9_c_001399F0(o, self, ent)) return; break;
    case 4: if (l9_c_00139E00(o, self, ent)) return; break;
    case 5: if (l9_c_0013A3B0(o, self, ent)) return; break;
    default: break;
    }
    {
        int32_t c = l9_s8(o, ent + 0x86);
        if (c != 0) l9_w8(o, ent + 0x86, (uint32_t)(c - 1));
    }
    {
        uint32_t v = l9_u16(o, ent + 0x32);
        if (v != 0) l9_w16(o, ent + 0x32, v - 1u);
    }
    {
        uint32_t v = l9_u16(o, ent + 0x2E);
        if (v != 0) l9_w16(o, ent + 0x2E, v - 1u);
    }
    if (l9_s8(o, ent + 0x84) != 0) {
        l9_w8(o, self + 0x5D, 0x81);
        if (l9_c_001B4810(o, self)) return;
    } else {
        l9_w8(o, self + 0x5D, 1);
    }
    if (l9_s16(o, self + 0x36) != 0) {
        if (l9_u16(o, ent + 0x34) != 0 || l9_s8(o, ent + 0x83) != 0) {
            l9_w8(o, self + 0, 1);
            l9_w16(o, self + 0x36, 0);
        } else {
            l9_w8(o, self + 4, 2);
            l9_w8(o, self + 5, 0);
            l9_w8(o, self + 6, 0);
        }
    }
    if (l9_u8(o, ent + 0x70) == 8u) {
        if (l9_c_00122BB8(o, &r)) return;
        int32_t t = l9_sra((uint32_t)r, 17);
        int32_t ignored = 0;
        if (l9_c_001FBD50(o, self, l9_rem(t, 5) + 0x821, 0, F_300, &ignored)) return;
    }
    l9_w8(o, self + 0x0A, 0);
    if (l9_c_0013BF20(o, self, ent)) return;
    if (l9_c_0013BE60(o, self, ent)) return;
    if (l9_c_001C64F0(o, self, F_ONE, &r)) return;
    l9_w32(o, ent + 0x70, (uint32_t)r);
    if (l9_c_00131ED0(o, self)) return;
    if (l9_c_001C6910(o, self)) return;
    if (l9_c_001B17A0(o, self, &r)) return;
    l9_method(o, self);
}

int em_level9_port_001386E0(const EmLevel9PortHooks *h, uint32_t self, uint32_t ent, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    l9_001386E0(&o, self, ent);
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 00154460 (self): the field's trigger gate. 0 unless the byte 0x70003B8D
 * is 0; then, with row = self +0x0D * 20: |player x - +0xB0| (0011DF78) <=
 * 3 * D_00248120[row], |player z - +0xB8| <= 3 * D_00248128[row] and
 * |player y - +0xB4| <= 3 + D_00248124[row]: 1; else 0.
 * ---------------------------------------------------------------------- */
static int32_t gate_axis(L9 *o, uint32_t self, uint32_t player_at, uint32_t self_at, uint32_t table, int add)
{
    uint32_t d = 0;
    uint32_t a = l9_u32(o, PLAYER + player_at);
    uint32_t b = l9_u32(o, self + self_at);
    if (l9_c_0011DF78(o, L9_SUB(a, b), &d)) return -1;
    uint32_t row = l9_u8(o, self + 0x0D) * 20u;
    uint32_t limit = l9_u32(o, table + row);
    limit = add ? L9_ADD(F_3, limit) : L9_MUL(F_3, limit);
    return L9_LE(d, limit) ? 1 : 0;
}

int32_t l9_00154460(L9 *o, uint32_t self)
{
    if (l9_u8(o, S_70003B8D) != 0) return 0;
    if (gate_axis(o, self, 0xA0, 0xB0, D_00248120, 0) != 1) return 0;
    if (gate_axis(o, self, 0xA8, 0xB8, D_00248128, 0) != 1) return 0;
    return gate_axis(o, self, 0xA4, 0xB4, D_00248124, 1) == 1 ? 1 : 0;
}

int em_level9_port_00154460(const EmLevel9PortHooks *h, uint32_t self, int32_t *result, EmLevel9PortFault *fault)
{
    L9 o;
    if (!result || l9_begin(&o, h, fault)) return -1;
    int32_t v = l9_00154460(&o, self);
    if (l9_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001546C0 (the kind-0xE field's behaviour). scr = self +0x1F0. By +4:
 * 0 -> 00154740(self, scr); 1 -> 001549C0(self, scr); 2 -> +4 = 3; 3 ->
 * 001AFC10(self) (a1 = scr, unread). Others: nothing.
 * ---------------------------------------------------------------------- */
int em_level9_port_001546C0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    uint32_t scr = self + 0x1F0;
    switch (l9_u8(&o, self + 4)) {
    case 0: l9_00154740(&o, self, scr); break;
    case 1: l9_001549C0(&o, self, scr); break;
    case 2: l9_w8(&o, self + 4, 3); break;
    case 3: l9_c_001AFC10(&o, self); break;
    default: break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 00154740 (self, scr): the field's spawn. 001CA5E0(self, D_0028A4E4, 10);
 * +0x0C = 001C6150(+0x44); when the halfword D_00275BCC is below +0x0C: +4
 * = 3, stop. Else +0x110 + 4 i = 001AF780() for i below +0x0C (re-read
 * each time); +9 = +0x0C; 001CB5B0(+0x0C); 001C62C0(self); +4 = 1, +0 =
 * 2, +0x30 = 0x275450, +0x80..+0x88 = 1.0; the room tint: the first of 22
 * records of D_00246800 whose word is (D_00810700 << 8) + D_00810701 gives
 * scr [0..2] = its bytes 4..6 as floats, scr [3] = byte 7 / 128, and +0x80
 * / +0x84 / +0x88 = scr [0..2] / 128, +0x8C = scr [3]. Then +0x38 = 1.0 and
 * the 12 halfwords scr +0x84 + 10 j = 0.
 * ---------------------------------------------------------------------- */
static uint32_t byte_float(uint32_t b) { return L9_CVT(b); } /* bytes are below 2^31: the plain conversion */

void l9_00154740(L9 *o, uint32_t self, uint32_t scr)
{
    int32_t r = 0;
    uint32_t addr = 0;
    if (l9_c_001CA5E0(o, self, l9_u32(o, D_0028A4E4), 10)) return;
    if (l9_c_001C6150(o, l9_u32(o, self + 0x44), &r)) return;
    l9_w8(o, self + 0x0C, (uint32_t)r);
    int32_t limit = l9_s16(o, D_00275BCC);
    if (limit < (int32_t)l9_u8(o, self + 0x0C)) {
        l9_w8(o, self + 4, 3);
        return;
    }
    uint32_t count;
    for (uint32_t i = 0;; i++) {
        count = l9_u8(o, self + 0x0C);
        if (!((int32_t)i < (int32_t)count) || l9_failed(o)) break;
        if (l9_c_001AF780(o, &addr)) return;
        l9_w32(o, self + 0x110 + 4u * i, addr);
    }
    l9_w8(o, self + 9, count);
    if (l9_c_001CB5B0(o, (int32_t)l9_u8(o, self + 0x0C))) return;
    if (l9_c_001C62C0(o, self)) return;
    l9_w8(o, self + 4, 1);
    l9_w8(o, self + 0, 2);
    l9_w32(o, self + 0x30, D_00275450);
    l9_w32(o, self + 0x80, F_ONE);
    l9_w32(o, self + 0x84, F_ONE);
    l9_w32(o, self + 0x88, F_ONE);
    {
        uint32_t hi = l9_u8(o, D_00810700) << 8;
        uint32_t key = hi + l9_u8(o, D_00810701);
        uint32_t rec = D_00246800;
        for (uint32_t idx = 0; idx < 0x16u && !l9_failed(o); idx++, rec += 8) {
            if (l9_u32(o, rec) != key) continue;
            l9_w32(o, scr + 0x0, byte_float(l9_u8(o, rec + 4)));
            l9_w32(o, scr + 0x4, byte_float(l9_u8(o, rec + 5)));
            l9_w32(o, scr + 0x8, byte_float(l9_u8(o, rec + 6)));
            l9_w32(o, scr + 0xC, L9_DIV(byte_float(l9_u8(o, rec + 7)), F_128));
            l9_w32(o, self + 0x80, L9_DIV(l9_u32(o, scr + 0x0), F_128));
            l9_w32(o, self + 0x84, L9_DIV(l9_u32(o, scr + 0x4), F_128));
            l9_w32(o, self + 0x88, L9_DIV(l9_u32(o, scr + 0x8), F_128));
            l9_w32(o, self + 0x8C, l9_u32(o, scr + 0xC));
            break;
        }
    }
    l9_w32(o, self + 0x38, F_ONE);
    for (uint32_t j = 0; j < 12; j++) l9_w16(o, scr + 0x84 + 10u * j, 0);
}

int em_level9_port_00154740(const EmLevel9PortHooks *h, uint32_t self, uint32_t scr, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    l9_00154740(&o, self, scr);
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 001549C0 (s7 = self, s6 = scr): the field's live frame. By +5:
 *   0 (SCAN): 00154460(self) zero: nothing more. Else scr +0x10 / +0x14 /
 *     +0x18 = (player x, +0xB4, player z), +0x28 = 8, +5 += 1 (stored
 *     before the random), k = ((00122BB8() >> 16) << 2) >> 15; then 12
 *     tendrils i: the radius 5.5 + 2 (r 2^-31) (+0x2E zero) or 7 + 2.5 (r
 *     2^-31); the angle -pi + 2 pi (r 2^-31); scr +0x1C + 8 i = player x
 *     + radius cos(angle), +0x20 + 8 i = player z + radius sin(angle);
 *     001545B0(self, x, z) nonzero: scr +0x84 + 10 i = 1 (hit), else 0;
 *     +0x7C + 10 i = ((00122BB8() >> 16) * 80 >> 15) + 0x30, +0x7E = +0x80
 *     = 0, +0x82 = D_0026D320[2 k]; k = (k + 1) & 3. Any hit:
 *     001FBD50(self, 0x42D, 0, 300). And on into 1.
 *   1 (DEPLOY): the 12 halfwords scr +0x80 + 10 i += 0x25 (clamped to
 *     0x12C, stored, read back, stored); +0x28 -= 1, at 0 or below +5 += 1.
 *   2 (HOLD): 0x70003B8D nonzero, |player y - +0xB4| > 3 + D_00248124[row]
 *     (not <=), or the player's distance squared from (scr +0x10, scr
 *     +0x18) not below 4 (+0x2E zero) or 16: +0x28 = 8, +5 += 1.
 *   3 (RETRACT): as 1 with -= 0x25 clamped at 0.
 *   4: +5 = 0.
 * Then with the parent +0x20: k = 1 - parent +0x80; +0x80 = (6 + k (scr
 * [0] - 6)) / 128, +0x84 = (92 + k (scr [1] - 92)) / 128, +0x88 = (1 + k
 * (scr [2] - 1)) / 128. 001C6380(self); +5 nonzero: 00154F00(self).
 * ---------------------------------------------------------------------- */
#define F_5_5 0x40B00000u
#define F_2_5 0x40200000u
#define F_7 0x40E00000u
#define F_2PI 0x40C90FDBu
#define F_92 0x42B80000u
#define F_6 0x40C00000u

static uint32_t rand_unit(L9 *o, uint32_t scale, uint32_t base)
{
    int32_t r = 0;
    l9_c_00122BB8(o, &r);
    uint32_t f = L9_MUL(F_2PM31, L9_CVT(r));
    f = L9_MUL(scale, f);
    return L9_ADD(base, f);
}

static void tendril_step(L9 *o, uint32_t self, uint32_t scr, int32_t delta)
{
    uint32_t b = scr;
    for (int i = 0; i < 12; i++, b += 10) {
        l9_w16(o, b + 0x80, (uint32_t)(l9_s16(o, b + 0x80) + delta));
        int32_t t = l9_s16(o, b + 0x80);
        if (delta > 0) {
            if (!(t < 0x12D)) t = 0x12C;
        } else {
            if (t < 0) t = 0;
        }
        l9_w16(o, b + 0x80, (uint32_t)t);
    }
    int32_t n = (int16_t)(l9_s16(o, self + 0x28) - 1);
    l9_w16(o, self + 0x28, (uint32_t)n);
    if (!(n > 0)) l9_w8(o, self + 5, l9_u8(o, self + 5) + 1u);
}

static void advance(L9 *o, uint32_t self)
{
    l9_w16(o, self + 0x28, 8);
    l9_w8(o, self + 5, l9_u8(o, self + 5) + 1u);
}

void l9_001549C0(L9 *o, uint32_t self, uint32_t scr)
{
    int32_t r = 0;
    switch (l9_u8(o, self + 5)) {
    case 0: {
        if (l9_00154460(o, self) == 0) break;
        if (l9_failed(o)) return;
        l9_w32(o, scr + 0x10, l9_u32(o, PLAYER + 0xA0));
        l9_w32(o, scr + 0x14, l9_u32(o, self + 0xB4));
        l9_w32(o, scr + 0x18, l9_u32(o, PLAYER + 0xA8));
        l9_w16(o, self + 0x28, 8);
        l9_w8(o, self + 5, l9_u8(o, self + 5) + 1u);
        if (l9_c_00122BB8(o, &r)) return;
        int32_t k = l9_sra((uint32_t)l9_sra((uint32_t)r, 16) << 2, 15);
        int hit = 0;
        uint32_t s4 = scr, s5 = scr;
        for (int i = 0; i < 12; i++, s4 += 8, s5 += 10) {
            uint32_t radius, angle, c = 0, s = 0;
            if (l9_u16(o, self + 0x2E) == 0) radius = rand_unit(o, F_2, F_5_5);
            else radius = rand_unit(o, F_2_5, F_7);
            angle = rand_unit(o, F_2PI, F_MPI);
            if (l9_c_0011DE90(o, angle, &c)) return;
            uint32_t t = L9_MUL(radius, c);
            if (l9_c_0011E2A8(o, angle, &s)) return;
            uint32_t u = L9_MUL(radius, s);
            l9_w32(o, s4 + 0x1C, L9_ADD(l9_u32(o, PLAYER + 0xA0), t));
            l9_w32(o, s4 + 0x20, L9_ADD(l9_u32(o, PLAYER + 0xA8), u));
            uint32_t x = l9_u32(o, s4 + 0x1C), z = l9_u32(o, s4 + 0x20);
            if (l9_c_001545B0(o, self, x, z, &r)) return;
            if (r != 0) {
                hit = 1;
                l9_w16(o, s5 + 0x84, 1);
            } else {
                l9_w16(o, s5 + 0x84, 0);
            }
            if (l9_c_00122BB8(o, &r)) return;
            int32_t v = l9_sra((uint32_t)l9_sra((uint32_t)r, 16) * 0x50u, 15);
            l9_w16(o, s5 + 0x7C, (uint32_t)(v + 0x30));
            l9_w16(o, s5 + 0x7E, 0);
            l9_w16(o, s5 + 0x80, 0);
            l9_w16(o, s5 + 0x82, l9_u16(o, D_0026D320 + (uint32_t)k * 4u));
            k = (k + 1) & 3;
        }
        if (hit) {
            int32_t ignored = 0;
            if (l9_c_001FBD50(o, self, 0x42D, 0, F_300, &ignored)) return;
        }
    }
        /* fall through */
    case 1:
        tendril_step(o, self, scr, 0x25);
        break;
    case 2:
        if (l9_u8(o, S_70003B8D) != 0) {
            advance(o, self);
            break;
        }
        {
            uint32_t d = 0;
            uint32_t a = l9_u32(o, PLAYER + 0xA4), b = l9_u32(o, self + 0xB4);
            if (l9_c_0011DF78(o, L9_SUB(a, b), &d)) return;
            uint32_t row = l9_u8(o, self + 0x0D) * 20u;
            uint32_t limit = L9_ADD(F_3, l9_u32(o, D_00248124 + row));
            if (!L9_LE(d, limit)) {
                advance(o, self);
                break;
            }
        }
        {
            uint32_t px = l9_u32(o, PLAYER + 0xA0), sx = l9_u32(o, scr + 0x10);
            uint32_t pz = l9_u32(o, PLAYER + 0xA8), sz = l9_u32(o, scr + 0x18);
            uint32_t wide = l9_u16(o, self + 0x2E);
            uint32_t dx = L9_SUB(px, sx), dz = L9_SUB(pz, sz);
            uint32_t sq = L9_MADD(L9_MULA(dx, dx), dz, dz);
            if (L9_LT(sq, wide == 0 ? F_4 : F_16)) break;
            advance(o, self);
        }
        break;
    case 3:
        tendril_step(o, self, scr, -0x25);
        break;
    case 4:
        l9_w8(o, self + 5, 0);
        break;
    default:
        break;
    }
    if (l9_failed(o)) return;
    {
        uint32_t parent = l9_u32(o, self + 0x20);
        if (parent != 0) {
            uint32_t k = L9_SUB(F_ONE, l9_u32(o, parent + 0x80));
            uint32_t v = l9_u32(o, scr + 0);
            l9_w32(o, self + 0x80, L9_DIV(L9_ADD(F_6, L9_MUL(k, L9_SUB(v, F_6))), F_128));
            v = l9_u32(o, scr + 4);
            l9_w32(o, self + 0x84, L9_DIV(L9_ADD(F_92, L9_MUL(k, L9_SUB(v, F_92))), F_128));
            v = l9_u32(o, scr + 8);
            l9_w32(o, self + 0x88, L9_DIV(L9_ADD(F_ONE, L9_MUL(k, L9_SUB(v, F_ONE))), F_128));
        }
    }
    if (l9_c_001C6380(o, self)) return;
    if (l9_u8(o, self + 5) != 0) l9_c_00154F00(o, self);
}

int em_level9_port_001549C0(const EmLevel9PortHooks *h, uint32_t self, uint32_t scr, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    l9_001549C0(&o, self, scr);
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0015A200 (parent, kind, pair): e = 001AFA90(2); none: 0. Else e +3 =
 * kind, +0x0D = parent +0x54, +0x9A = 0, +0x2E = pair, 00102948(e +0xB0,
 * parent +0xB0), +0xC0..+0xC8 = 0, +0x10 = 00153F10 (kind 0xD) or
 * 001546C0, +0x20 = parent +0x14; 1.
 * ---------------------------------------------------------------------- */
int em_level9_port_0015A200(const EmLevel9PortHooks *h, uint32_t parent, int32_t kind, int32_t pair,
                            int32_t *result, EmLevel9PortFault *fault)
{
    L9 o;
    uint32_t e = 0;
    int32_t v = 0;
    if (!result || l9_begin(&o, h, fault)) return -1;
    if (l9_c_001AFA90(&o, 2, &e)) return -1;
    if (e != 0) {
        l9_w8(&o, e + 3, (uint32_t)kind);
        l9_w8(&o, e + 0x0D, l9_u8(&o, parent + 0x54));
        l9_w8(&o, e + 0x9A, 0);
        l9_w16(&o, e + 0x2E, (uint32_t)pair);
        if (l9_c_00102948(&o, e + 0xB0, parent + 0xB0)) return -1;
        l9_w32(&o, e + 0xC0, 0);
        l9_w32(&o, e + 0xC4, 0);
        l9_w32(&o, e + 0xC8, 0);
        l9_w32(&o, e + 0x10, kind == 0xD ? 0x00153F10u : 0x001546C0u);
        l9_w32(&o, e + 0x20, l9_u32(&o, parent + 0x14));
        v = 1;
    }
    if (l9_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 00183440 (inline asm: the decomp's asm body uses mnemonics with .word
 * for its branches; translated from the instructions): a player stage worker.
 * +0x23F = 2, +0x24C = 1, 001662D0(self). With +0x200 bit 12: +0x268 +=
 * 1.0 (stored); when it is not below 4.0: 0x70003B8D = 0, +4 = 1, +5 =
 * 0x0C, +6 = 0, +0x1F0 = 0x17.
 * ---------------------------------------------------------------------- */
int em_level9_port_00183440(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    l9_w8(&o, self + 0x23F, 2);
    l9_w32(&o, self + 0x24C, 1);
    if (l9_c_001662D0(&o, self)) return -1;
    if (l9_u32(&o, self + 0x200) & 0x1000u) {
        uint32_t t = L9_ADD(l9_u32(&o, self + 0x268), F_ONE);
        l9_w32(&o, self + 0x268, t);
        if (!L9_LT(t, F_4)) {
            l9_w8(&o, S_70003B8D, 0);
            l9_w8(&o, self + 4, 1);
            l9_w8(&o, self + 5, 0x0C);
            l9_w8(&o, self + 6, 0);
            l9_w8(&o, self + 0x1F0, 0x17);
        }
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 001838B0 (a player stage worker): 001662D0(e); with +4 == 1: +4 = 4, +5
 * = 0, +6 = 0, 00174A50(e, 8.0), +0x1F2 = +0x20C (halfword).
 * ---------------------------------------------------------------------- */
int em_level9_port_001838B0(const EmLevel9PortHooks *h, uint32_t e, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    if (l9_c_001662D0(&o, e)) return -1;
    if (l9_u8(&o, e + 4) == 1u) {
        l9_w8(&o, e + 4, 4);
        l9_w8(&o, e + 5, 0);
        l9_w8(&o, e + 6, 0);
        if (l9_c_00174A50(&o, e, F_8)) return -1;
        l9_w16(&o, e + 0x1F2, l9_u16(&o, e + 0x20C));
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 00196970 (p, q): AREA19's ladder regions. Nothing while D_008106B8 is
 * set. With x = q +0xA0, z = q +0xA8 (read once), the first of five
 * circles (radius 8: (x - cx)^2 + (z - cz)^2 < 64, as mula / madd) holding
 * the player sets p +2 = its index, clamps p +0x14 to the region's ceiling
 * (stored only when above it), and when q +0xA4 is not below the region's
 * threshold calls 001B0C60(area, sub, entry) and p +6 = 7. Outside all
 * five: p +2 = 5.
 * ---------------------------------------------------------------------- */
static const struct {
    uint32_t cx, cz, ceiling, threshold;
    int32_t area, sub, entry;
} REGIONS[5] = {
    {0x44318000u /* 710 */, 0x449E4333u /* 1266.1 */, 0x43860CCDu /* 268.1 */, 0x438E4000u /* 284.5 */, 0xD, 0, 6},
    {0x4462C000u /* 907 */, 0x44700CCDu /* 960.2 */, 0x43990CCDu /* 306.1 */, 0x439D0000u /* 314 */, 0x13, 1, 8},
    {0x44674666u /* 925.1 */, 0x4456ECCDu /* 859.7 */, 0x439A0000u /* 308 */, 0x439D0000u /* 314 */, 0x13, 1, 7},
    {0x4486A666u /* 1077.2 */, 0x44534000u /* 845 */, 0x43728000u /* 242.5 */, 0x437C0000u /* 252 */, 0xD, 0, 7},
    {0x4455C666u /* 855.1 */, 0x4454D333u /* 851.3 */, 0x439A8000u /* 309 */, 0x439D0000u /* 314 */, 0x13, 1, 6},
};

void l9_00196970(L9 *o, uint32_t p, uint32_t q)
{
    if (l9_u8(o, D_008106B8) != 0) return;
    uint32_t x = l9_u32(o, q + 0xA0);
    uint32_t z = l9_u32(o, q + 0xA8);
    for (int i = 0; i < 5; i++) {
        uint32_t dx = L9_SUB(x, REGIONS[i].cx);
        uint32_t acc = L9_MULA(dx, dx);
        uint32_t dz = L9_SUB(z, REGIONS[i].cz);
        if (!L9_LT(L9_MADD(acc, dz, dz), 0x42800000u /* 64 */)) continue;
        l9_w8(o, p + 2, (uint32_t)i);
        if (!L9_LE(l9_u32(o, p + 0x14), REGIONS[i].ceiling)) l9_w32(o, p + 0x14, REGIONS[i].ceiling);
        if (!L9_LT(l9_u32(o, q + 0xA4), REGIONS[i].threshold)) {
            if (l9_c_001B0C60(o, REGIONS[i].area, REGIONS[i].sub, REGIONS[i].entry)) return;
            l9_w8(o, p + 6, 7);
        }
        return;
    }
    l9_w8(o, p + 2, 5);
}

int em_level9_port_00196970(const EmLevel9PortHooks *h, uint32_t p, uint32_t q, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    l9_00196970(&o, p, q);
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 00196CE0 (self = the camera record, other = the player; NEARMISS, from
 * the instructions): AREA19's ladder camera. 001916C0(self, other, 0);
 * then by self +1:
 *   0: +8 = 0, +1 += 1, +2 = 0, +0x44 = 001B1240(0x8105D0, D_008105E0,
 *      D_008105E8), 00196970(self, other); other +0x230 == 0x2D: +0x10..
 *      +0x18 = the region's point (D_0024A6B0 / B4 / B8 [16 +2]) and
 *      00102948(0x8105D0, self +0x10); else regions 3 / 4 the same point
 *      without the copy. Then the two windows: the player within 15 of
 *      (848.6, 882.1) with 241 < y < 294: +1 = 4; within 15 of (915,
 *      939.1) with 229 < y < 266: +1 = 3; not within 15 of the second
 *      and region 5: +1 = 2. Otherwise on into 1.
 *   1: 00196970(self, other); unless other +0x230 == 0x2D: other +0xB4
 *      below D_0024A6BC[16 +2]: +1 += 1 when +2 < 3, 0018D7B0(self, 3);
 *      then 0018C6A0(self +0x10, 0x8105D0, 2), 0018C4B0(0x8105D0, +0x14,
 *      2).
 *   2: other +0x230 == 7: +0x44 = 001B12B0(pi + other +0xC4, +0x44,
 *      0.0349), +0x6C = 0, +0x10 = E0 + +0xC sin(+0x44), +0x18 = E8 + +0xC
 *      cos(+0x44), 0018D7B0(self, 3), C6A0(.., 1), C4B0(.., 1). Else
 *      +0x44 = 001B12B0(other +0xC4, +0x44, 0.0349), the same point,
 *      00192010(self, +0x8C + (+0x5C + other +0xB4), 20, 15),
 *      00196970(self, other), region 5 or other +0xB4 below the region's
 *      height: stay, else +1 -= 1; 0018D7B0(self, 3), C6A0(.., 2), C4B0(..,
 *      2).
 *   3 / 4: +0x10 / +0x18 = (915.8, 900) / (801.8, 882.1), 00192010(self,
 *      .., 15, 10), 0018D7B0(self, 5), C6A0(.., 1), C4B0(.., 2).
 * Then other +0x230 not one of 6, 7, 8, 9, 0x2C, 0x2D: +5 = +6 = +1 = 0.
 * 0018C0C0(self).
 * ---------------------------------------------------------------------- */
#define K_225 0x43610000u
#define K_TURN 0x3D0EFA35u /* 0.034906585 */

static void follow(L9 *o, uint32_t self, uint32_t near_rate, uint32_t far_rate)
{
    if (l9_c_0018C6A0(o, self + 0x10, D_008105D0, near_rate)) return;
    l9_c_0018C4B0(o, D_008105D0, l9_u32(o, self + 0x14), far_rate);
}

/* The region's point; `first` is the index byte when the caller has just
 * read it (the first load then reuses it), else -1. */
static void region_point(L9 *o, uint32_t self, int32_t first)
{
    uint32_t idx = first >= 0 ? (uint32_t)first : l9_u8(o, self + 2);
    l9_w32(o, self + 0x10, l9_u32(o, D_0024A6B0 + idx * 16u));
    l9_w32(o, self + 0x14, l9_u32(o, D_0024A6B4 + l9_u8(o, self + 2) * 16u));
    l9_w32(o, self + 0x18, l9_u32(o, D_0024A6B8 + l9_u8(o, self + 2) * 16u));
}

static void orbit_point(L9 *o, uint32_t self, uint32_t s)
{
    uint32_t c = 0;
    uint32_t r = l9_u32(o, self + 0xC);
    uint32_t e0 = l9_u32(o, D_008105E0);
    l9_w32(o, self + 0x10, L9_ADD(e0, L9_MUL(r, s)));
    if (l9_c_0011DE90(o, l9_u32(o, self + 0x44), &c)) return;
    r = l9_u32(o, self + 0xC);
    uint32_t e8 = l9_u32(o, D_008105E8);
    l9_w32(o, self + 0x18, L9_ADD(e8, L9_MUL(r, c)));
}

static uint32_t climb_height(L9 *o, uint32_t self, uint32_t other)
{
    uint32_t a = l9_u32(o, self + 0x5C);
    uint32_t b = l9_u32(o, other + 0xB4);
    uint32_t c = l9_u32(o, self + 0x8C);
    return L9_ADD(c, L9_ADD(a, b));
}

static int window(uint32_t x, uint32_t z, uint32_t cx, uint32_t cz)
{
    uint32_t dx = L9_SUB(x, cx);
    uint32_t acc = L9_MULA(dx, dx);
    uint32_t dz = L9_SUB(z, cz);
    return L9_LT(L9_MADD(acc, dz, dz), K_225);
}

int em_level9_port_00196CE0(const EmLevel9PortHooks *h, uint32_t self, uint32_t other, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    if (l9_c_001916C0(&o, self, other, 0)) return -1;
    uint32_t state = l9_u8(&o, self + 1);
    int idle = 0;
    switch (state) {
    case 0: {
        uint32_t f = 0;
        l9_w16(&o, self + 8, 0);
        l9_w8(&o, self + 1, l9_u8(&o, self + 1) + 1u);
        l9_w8(&o, self + 2, 0);
        uint32_t e0 = l9_u32(&o, D_008105E0);
        uint32_t e8 = l9_u32(&o, D_008105E8);
        if (l9_c_001B1240(&o, D_008105D0, e0, e8, &f)) return -1;
        l9_w32(&o, self + 0x44, f);
        l9_00196970(&o, self, other);
        if (l9_u32(&o, other + 0x230) == 0x2D) {
            region_point(&o, self, -1);
            if (l9_c_00102948(&o, D_008105D0, self + 0x10)) return -1;
        } else {
            uint32_t idx = l9_u8(&o, self + 2);
            if (idx == 3 || idx == 4) region_point(&o, self, (int32_t)idx);
        }
        uint32_t x = l9_u32(&o, other + 0xA0);
        uint32_t z = l9_u32(&o, other + 0xA8);
        if (window(x, z, 0x44542666u /* 848.6 */, 0x445C8666u /* 882.1 */)) {
            uint32_t y = l9_u32(&o, other + 0xA4);
            if (L9_LT(y, 0x43930000u /* 294 */) && !L9_LE(y, 0x43710000u /* 241 */)) {
                l9_w8(&o, self + 1, 4);
                break;
            }
        }
        if (window(x, z, 0x4464C000u /* 915 */, 0x446AC666u /* 939.1 */)) {
            uint32_t y = l9_u32(&o, other + 0xA4);
            if (L9_LT(y, 0x43850000u /* 266 */) && !L9_LE(y, 0x43650000u /* 229 */)) {
                l9_w8(&o, self + 1, 3);
                break;
            }
        } else if (l9_u8(&o, self + 2) == 5u) {
            l9_w8(&o, self + 1, 2);
            break;
        }
        idle = 1;
        break;
    }
    case 1:
        idle = 1;
        break;
    case 2:
        if (l9_u32(&o, other + 0x230) == 7) {
            uint32_t f = 0, s = 0;
            uint32_t yaw = l9_u32(&o, other + 0xC4);
            uint32_t cur = l9_u32(&o, self + 0x44);
            if (l9_c_001B12B0(&o, L9_ADD(F_PI, yaw), cur, K_TURN, &f)) return -1;
            l9_w32(&o, self + 0x44, f);
            l9_w8(&o, self + 0x6C, 0);
            if (l9_c_0011E2A8(&o, l9_u32(&o, self + 0x44), &s)) return -1;
            orbit_point(&o, self, s);
            if (l9_c_0018D7B0(&o, self, 3)) return -1;
            follow(&o, self, F_ONE, F_ONE);
        } else {
            uint32_t f = 0, s = 0;
            uint32_t cur = l9_u32(&o, self + 0x44);
            if (l9_c_001B12B0(&o, l9_u32(&o, other + 0xC4), cur, K_TURN, &f)) return -1;
            l9_w32(&o, self + 0x44, f);
            if (l9_c_0011E2A8(&o, f, &s)) return -1;
            orbit_point(&o, self, s);
            if (l9_c_00192010(&o, self, climb_height(&o, self, other), F_20, 0x41700000u /* 15 */)) return -1;
            l9_00196970(&o, self, other);
            uint32_t idx = l9_u8(&o, self + 2);
            if (idx != 5u) {
                uint32_t y = l9_u32(&o, other + 0xB4);
                if (!L9_LT(y, l9_u32(&o, D_0024A6BC + idx * 16u))) l9_w8(&o, self + 1, l9_u8(&o, self + 1) - 1u);
            }
            if (l9_c_0018D7B0(&o, self, 3)) return -1;
            follow(&o, self, F_2, F_2);
        }
        break;
    case 3:
        l9_w32(&o, self + 0x10, 0x4464F333u /* 915.8 */);
        l9_w32(&o, self + 0x18, 0x44610000u /* 900 */);
        if (l9_c_00192010(&o, self, climb_height(&o, self, other), 0x41700000u /* 15 */, F_10)) return -1;
        if (l9_c_0018D7B0(&o, self, 5)) return -1;
        follow(&o, self, F_ONE, F_2);
        break;
    case 4:
        l9_w32(&o, self + 0x10, 0x44487333u /* 801.8 */);
        l9_w32(&o, self + 0x18, 0x445C8666u /* 882.1 */);
        if (l9_c_00192010(&o, self, climb_height(&o, self, other), 0x41700000u /* 15 */, F_10)) return -1;
        if (l9_c_0018D7B0(&o, self, 5)) return -1;
        follow(&o, self, F_ONE, F_2);
        break;
    default:
        break;
    }
    if (idle) {
        if (l9_u32(&o, other + 0x230) == 0x2D) {
            l9_00196970(&o, self, other);
        } else {
            l9_00196970(&o, self, other);
            uint32_t idx = l9_u8(&o, self + 2);
            uint32_t y = l9_u32(&o, other + 0xB4);
            if (L9_LT(y, l9_u32(&o, D_0024A6BC + idx * 16u))) {
                if ((int32_t)idx < 3) l9_w8(&o, self + 1, l9_u8(&o, self + 1) + 1u);
                if (l9_c_0018D7B0(&o, self, 3)) return -1;
            }
            follow(&o, self, F_2, F_2);
        }
    }
    if (l9_failed(&o)) return -1;
    switch (l9_u32(&o, other + 0x230)) {
    case 6: case 7: case 8: case 9: case 0x2C: case 0x2D:
        break;
    default:
        l9_w8(&o, self + 5, 0);
        l9_w8(&o, self + 6, 0);
        l9_w8(&o, self + 1, 0);
        break;
    }
    l9_c_0018C0C0(&o, self);
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 00153F10 (self): the generators' kind-0xD worm (0015A200's other brain;
 * byte-matched C). ent = self +0x1F0. By self +4:
 *   0: 00154040: 001B10B0(self, 0x14, 0x13) == 0: 001C63E0(self, 1), +0x4C =
 *      001CB130 (001CA5F0 kind 9), +0x58 = spec D_0028A4D8, the halfword ent
 *      = 1, +4 = +0 = 1, +0x30 = D_00275448, +0x34 = 10, +0xC4 =
 *      001B1240(self +0xB0, the player's x, z), +0x80.. = 0.5, 0.875, 1, 1,
 *      sound 0x430.
 *   1: with 0x70003B8D 0 or 4, 00154120: ent +2 = 001C64F0(self, 1.0); by
 *      self +5: 0: the two-node hit 0019AA80(mask 0x20) on a player in state
 *      1: mode 2, timer 5, state | 2, the direction into the record's +0x70; on the
 *      clip's end 00153ED0(0), +5 += 1, +0x28 = 0x78. 1: +0x28 counts down,
 *      at zero +5 += 1 and 00153ED0(2); +0xC4 turns toward the player at
 *      0.0698. 2: on the clip's end 00153ED0(3), +5 += 1, +0xC4 faces the
 *      player, sound 0x431. 3: the hit (a state of exactly 1 takes the
 *      timer 15 and the direction): +4 = 2, +5 = 0; else 0019A570 (6, 0):
 *      +4 = 2, +5 = 0; else on the clip's end +4 = 3, +5 = 0. Then
 *      001C68C0(self), 001B17A0(self), the +0x4C method.
 *   2: by +5: 0: sound 0x434, 001EFE00(0x80000052, self), the +0x4C method,
 *      +5 += 1; 1: +5 += 1; 2: +4 = 3, +5 = 0.
 *   3: 001AFC10(self).
 * ---------------------------------------------------------------------- */

static void l9_00153ED0(L9 *o, uint32_t self, uint32_t ent, int32_t clip)
{
    if (l9_s16(o, ent) != (int16_t)clip) {
        l9_w16(o, ent, (uint32_t)clip);
        l9_c_001C67E0(o, self, clip, 0, 0);
    }
}

static void worm_rings(L9 *o, uint32_t *a, uint32_t *b)
{
    const uint32_t table = l9_u32(o, 0x00275B40u);
    *a = l9_u32(o, table + 0x34) + 0xC0;
    *b = l9_u32(o, table + 0x40) + 0xC0;
}

static void worm_hit(L9 *o, uint32_t timer, int table_first)
{
    uint32_t table = table_first ? l9_u32(o, 0x00275B40u) : 0;
    l9_w8(o, PLAYER + 0x0F, 2);
    l9_w32(o, PLAYER + 0x224, timer);
    const uint32_t state = l9_u8(o, PLAYER) | 2u;
    if (!table_first) table = l9_u32(o, 0x00275B40u);
    l9_w8(o, PLAYER, state);
    const uint32_t a = l9_u32(o, table + 0x34) + 0xC0;
    const uint32_t b = l9_u32(o, table + 0x40) + 0xC0;
    if (l9_c_001028D0(o, 0x700038A0u, b, a)) return;
    l9_c_00102760(o, PLAYER + 0x70, 0x700038A0u);
}

static void l9_00154120(L9 *o, uint32_t self, uint32_t ent)
{
    int32_t v = 0, hit = 0;
    uint32_t a = 0, b = 0;
    if (l9_c_001C64F0(o, self, 0x3F800000u, &v)) return;
    l9_w16(o, ent + 2, (uint32_t)v);
    const uint32_t st = l9_u8(o, self + 5);
    if (st == 0) {
        worm_rings(o, &a, &b);
        if (l9_c_0019AA80(o, a, b, 0x20, &hit)) return;
        if (hit != 0 && l9_u8(o, PLAYER) == 1u) worm_hit(o, 0x40A00000u, 1);
        if (l9_failed(o)) return;
        if (l9_u16(o, ent + 2) & 0x1000u) {
            l9_00153ED0(o, self, ent, 0);
            l9_w8(o, self + 5, l9_u8(o, self + 5) + 1u);
            l9_w16(o, self + 0x28, 0x78);
        }
    } else if (st == 1) {
        const int32_t t = l9_s16(o, self + 0x28) - 1;
        l9_w16(o, self + 0x28, (uint32_t)t);
        if ((int16_t)t == 0) {
            l9_w8(o, self + 5, l9_u8(o, self + 5) + 1u);
            l9_00153ED0(o, self, ent, 2);
        }
        if (l9_c_001B1240(o, self + 0xB0, l9_u32(o, PLAYER + 0xA0), l9_u32(o, PLAYER + 0xA8), &a)) return;
        if (l9_c_001B12B0(o, a, l9_u32(o, self + 0xC4), 0x3D8EFA35u, &b)) return;
        l9_w32(o, self + 0xC4, b);
    } else if (st == 2) {
        if (l9_u16(o, ent + 2) & 0x1000u) {
            l9_00153ED0(o, self, ent, 3);
            l9_w8(o, self + 5, l9_u8(o, self + 5) + 1u);
            if (l9_c_001B1240(o, self + 0xB0, l9_u32(o, PLAYER + 0xA0), l9_u32(o, PLAYER + 0xA8), &a)) return;
            l9_w32(o, self + 0xC4, a);
            if (l9_c_001FBD50(o, self, 0x431, 0, F_300, &v)) return;
        }
    } else if (st == 3) {
        worm_rings(o, &a, &b);
        if (l9_c_0019AA80(o, a, b, 0x20, &hit)) return;
        if (hit != 0) {
            l9_w8(o, self + 4, 2);
            l9_w8(o, self + 5, 0);
            if (l9_u8(o, PLAYER) == 1u) worm_hit(o, 0x41700000u, 0);
        } else {
            worm_rings(o, &a, &b);
            if (l9_c_0019A570(o, a, b, 6, 0, &hit)) return;
            if (hit != 0) {
                l9_w8(o, self + 4, 2);
                l9_w8(o, self + 5, 0);
            } else if (l9_u16(o, ent + 2) & 0x1000u) {
                l9_w8(o, self + 4, 3);
                l9_w8(o, self + 5, 0);
            }
        }
    }
    if (l9_failed(o)) return;
    if (l9_c_001C68C0(o, self)) return;
    if (l9_c_001B17A0(o, self, &v)) return;
    l9_callback(o, l9_u32(o, self + 0x4C), self);
}

int em_level9_port_00154120(const EmLevel9PortHooks *h, uint32_t self, uint32_t ent, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    l9_00154120(&o, self, ent);
    return l9_end(&o);
}

static void l9_00154040(L9 *o, uint32_t self, uint32_t ent)
{
    int32_t r = 0;
    uint32_t y = 0;
    if (l9_c_001B10B0(o, self, 0x14, 0x13, &r) || r != 0) return;
    if (l9_c_001C63E0(o, self, 1)) return;
    (void)l9_u32(o, 0x0026E334u); /* 001CA5F0's jump table, kind 9 */
    l9_w32(o, self + 0x4C, 0x001CB130u);
    l9_w32(o, self + 0x58, l9_u32(o, 0x0028A4D8u));
    l9_w16(o, ent, 1);
    l9_w8(o, self + 4, 1);
    l9_w8(o, self + 0, 1);
    l9_w32(o, self + 0x30, 0x00275448u);
    l9_w16(o, self + 0x34, 0xA);
    if (l9_c_001B1240(o, self + 0xB0, l9_u32(o, PLAYER + 0xA0), l9_u32(o, PLAYER + 0xA8), &y)) return;
    l9_w32(o, self + 0xC4, y);
    l9_w32(o, self + 0x80, 0x3F000000u);
    l9_w32(o, self + 0x84, 0x3F600000u);
    l9_w32(o, self + 0x88, 0x3F800000u);
    l9_w32(o, self + 0x8C, 0x3F800000u);
    l9_c_001FBD50(o, self, 0x430, 0, F_300, &r);
}

int em_level9_port_00154040(const EmLevel9PortHooks *h, uint32_t self, uint32_t ent, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    l9_00154040(&o, self, ent);
    return l9_end(&o);
}

int em_level9_port_00153F10(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    const uint32_t ent = self + 0x1F0;
    int32_t r = 0;
    switch (l9_u8(&o, self + 4)) {
    case 0:
        l9_00154040(&o, self, ent);
        break;
    case 1: {
        const uint32_t m = l9_u8(&o, 0x70003B8Du);
        if (m == 0 || m == 4) l9_00154120(&o, self, ent);
        break;
    }
    case 2: {
        const uint32_t step = l9_u8(&o, self + 5);
        switch (step) {
        case 0:
            if (l9_c_001FBD50(&o, self, 0x434, 0, F_300, &r)) break;
            if (l9_c_001EFE00(&o, (int32_t)0x80000052u, self, &r)) break;
            if (l9_callback(&o, l9_u32(&o, self + 0x4C), self)) break;
            l9_w8(&o, self + 5, l9_u8(&o, self + 5) + 1u);
            break;
        case 1:
            l9_w8(&o, self + 5, step + 1u);
            break;
        case 2:
            l9_w8(&o, self + 4, 3);
            l9_w8(&o, self + 5, 0);
            break;
        default:
            break;
        }
        break;
    }
    case 3:
        l9_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 00156620 (self): the drum (byte-matched C, the AREA11 drums' owner too;
 * here spawned by the AREA19 pieces of em_level13_port_area19.c). ent = self
 * +0x1F0.
 *   0: 001B0FD0(self) == 0: +0x34 = 1, +0 = 1, ent +0x10 / +0x20 = the
 *      position / rotation, 001C6380(self).
 *   1: with +0x36 set: +0 = +4 = 2, a probe 4 above to 4 below; a 0019A570
 *      hit (mask 4) raises the matrix at 0x700036A0 for 001F0460(4). Then
 *      the +0x4C method, the squared distance to the player against 50^2:
 *      within, +1 = 1 and 001B1D20(self); else 001B17A0(self).
 *   2: by +5, 0: the burst effects at +7 height (kinds 0x18, 0x2A: 0x8000001C
 *      and sound 0x1A1; else 0x8000002E and 0x19F), +0x28 = 2, the angle of
 *      kinds 10 and 12, speed and lift from the rand tables 00246A00 /
 *      00246A10. 1: +0x28 down; at zero the angle is taken: kinds 0x18, 0x2A
 *      go to +4 = 3; else the next step, +0x28 = 8. 2: +0x28 down, at zero
 *      the next step with +0 = 1, +0x36 = 0, +0x34 = 1. 2 and 3: the flight:
 *      spin, step, wall probe (the speed stops), gravity 0.06 down to -4; on
 *      step 3 a +0x36 hit without bit 0x2000 lands (sound 0x1A0); falling,
 *      a 0019AB20 floor lands; in AREA21 (0x15) below 5.0 with the frame's
 *      test, 0x8000005F and sound 0xDB. Then 001C6380, 001A2370 (self +0xD0),
 *      001B17A0, the +0x4C method.
 *   3: 001AFC10(self).
 * ---------------------------------------------------------------------- */
#define DRUM_A0 0x700038A0u
#define DRUM_B0 0x700038B0u
#define DRUM_FX 0x700036A0u

static void drum_land(L9 *o, uint32_t self)
{
    l9_w32(o, DRUM_A0 + 0, l9_u32(o, self + 0xB0));
    l9_w32(o, DRUM_A0 + 4, L9_ADD(0x40800000u, l9_u32(o, self + 0xB4)));
    l9_w32(o, DRUM_A0 + 8, l9_u32(o, self + 0xB8));
    l9_w32(o, DRUM_A0 + 0xC, 0x3F800000u);
    if (l9_c_001EFD20(o, (int32_t)0x80000013u, DRUM_A0)) return;
    if (l9_c_001EFD20(o, (int32_t)0x8000002Eu, DRUM_A0)) return;
    l9_c_001FC580(o, self, 0x1A0);
}

static void drum_burst(L9 *o, uint32_t self, uint32_t ent)
{
    int32_t r = 0;
    uint32_t v = 0;
    l9_w32(o, DRUM_A0 + 0, l9_u32(o, self + 0xB0));
    l9_w32(o, DRUM_A0 + 4, L9_ADD(0x40E00000u, l9_u32(o, self + 0xB4)));
    l9_w32(o, DRUM_A0 + 8, l9_u32(o, self + 0xB8));
    l9_w32(o, DRUM_A0 + 0xC, 0x3F800000u);
    if (l9_c_001EFD20(o, (int32_t)0x80000013u, DRUM_A0)) return;
    uint32_t k = l9_u8(o, self + 3);
    if (k == 0x18 || k == 0x2A) {
        if (l9_c_001EFD20(o, (int32_t)0x8000001Cu, DRUM_A0)) return;
        if (l9_c_001FC580(o, self, 0x1A1)) return;
    } else {
        if (l9_c_001EFD20(o, (int32_t)0x8000002Eu, DRUM_A0)) return;
        if (l9_c_001FC580(o, self, 0x19F)) return;
    }
    l9_w16(o, self + 0x28, 2);
    l9_w8(o, self + 5, l9_u8(o, self + 5) + 1u);
    k = l9_u8(o, self + 3);
    if (k == 0xA) {
        if (l9_c_00122BB8(o, &r)) return;
        const uint32_t x = L9_DIV(L9_MUL(0x40C90FDBu, L9_CVT((uint32_t)r & 0xF0u)), 0x43800000u);
        if (l9_c_001B1470(o, x, &v)) return;
        l9_w32(o, ent + 0x74, v);
    } else if (k == 0xC) {
        if (l9_c_00122BB8(o, &r)) return;
        const uint32_t x = L9_CVT((uint32_t)r & 0x1Fu);
        const uint32_t y = L9_ADD(l9_u32(o, self + 0xC4), L9_DIV(L9_MUL(F_PI, x), 0x43340000u));
        if (l9_c_001B1470(o, y, &v)) return;
        l9_w32(o, ent + 0x74, v);
    }
    if (l9_c_00122BB8(o, &r)) return;
    l9_w32(o, self + 0x38, l9_u32(o, 0x00246A00u + (((uint32_t)r & 0x300u) >> 8) * 4));
    if (l9_c_00122BB8(o, &r)) return;
    l9_w32(o, ent + 0x78, l9_u32(o, 0x00246A10u + (((uint32_t)r & 0x300u) >> 8) * 4));
}

/* The flight; 1: a landing left the switch without the tail. */
static int drum_flight(L9 *o, uint32_t self, uint32_t ent)
{
    int32_t r = 0;
    uint32_t v = 0, s = 0, c = 0;
    const uint32_t step = L9_LT(l9_u32(o, ent + 0x78), 0) ? 0x3C8EFA35u : 0x3C0EFA35u;
    if (l9_c_001B1470(o, L9_ADD(step, l9_u32(o, self + 0xC0)), &v)) return -1;
    l9_w32(o, self + 0xC0, v);
    if (l9_c_0011E2A8(o, l9_u32(o, ent + 0x74), &s)) return -1;
    {
        const uint32_t speed = l9_u32(o, self + 0x38);
        l9_w32(o, self + 0xB0, L9_ADD(l9_u32(o, self + 0xB0), L9_MUL(speed, s)));
    }
    if (l9_c_0011DE90(o, l9_u32(o, ent + 0x74), &c)) return -1;
    {
        const uint32_t speed = l9_u32(o, self + 0x38);
        l9_w32(o, self + 0xB8, L9_ADD(l9_u32(o, self + 0xB8), L9_MUL(speed, c)));
    }
    if (l9_c_0011E2A8(o, l9_u32(o, ent + 0x74), &s)) return -1;
    l9_w32(o, DRUM_A0 + 0, L9_ADD(l9_u32(o, self + 0xB0), L9_MUL(0x40E00000u, s)));
    l9_w32(o, DRUM_A0 + 4, L9_ADD(0x40E00000u, l9_u32(o, self + 0xB4)));
    if (l9_c_0011DE90(o, l9_u32(o, ent + 0x74), &c)) return -1;
    l9_w32(o, DRUM_A0 + 8, L9_ADD(l9_u32(o, self + 0xB8), L9_MUL(0x40E00000u, c)));
    if (!L9_EQ(l9_u32(o, self + 0x38), 0)) {
        if (l9_c_0019AD00(o, self, DRUM_A0, (int32_t)0x80000007u, &r)) return -1;
        if (r != 0) l9_w32(o, self + 0x38, 0);
    }
    uint32_t lift = L9_SUB(l9_u32(o, ent + 0x78), 0x3D75C28Fu);
    l9_w32(o, ent + 0x78, lift);
    if (L9_LT(lift, 0xC0800000u)) l9_w32(o, ent + 0x78, 0xC0800000u);
    lift = l9_u32(o, ent + 0x78);
    l9_w32(o, self + 0xB4, L9_ADD(l9_u32(o, self + 0xB4), lift));
    const uint32_t hit = l9_u8(o, self + 5) == 3u ? l9_u16(o, self + 0x36) : 0;
    if (hit != 0) {
        if (!(hit & 0x2000u)) {
            drum_land(o, self);
            if (l9_failed(o)) return -1;
            l9_w16(o, self + 0x36, 0);
            l9_w8(o, self + 4, 3);
            l9_w8(o, self + 5, 0);
            l9_w8(o, self + 0, 2);
            return 0;
        }
        l9_w16(o, self + 0x36, 0);
    }
    if (L9_LT(l9_u32(o, ent + 0x78), 0)) {
        l9_w32(o, DRUM_A0 + 0, 0);
        l9_w32(o, DRUM_A0 + 4, 0xC1200000u);
        l9_w32(o, DRUM_A0 + 8, 0);
        l9_w32(o, DRUM_A0 + 0xC, 0x3F800000u);
        if (l9_c_0019AB20(o, self, self + 0xB0, DRUM_A0, (int32_t)0x80000007u, &r)) return -1;
        if (r != 0) {
            drum_land(o, self);
            if (l9_failed(o)) return -1;
            l9_w16(o, self + 0x36, 0);
            l9_w8(o, self + 4, 3);
            l9_w8(o, self + 5, 0);
        } else {
            int go = 0;
            const uint32_t index = (uint32_t)l9_s16(o, 0x70003B8Au);
            if ((l9_u32(o, 0x70003B68u) + index) & 0x3Fu) go = 1;
            else {
                if (l9_c_001B0D80(o, self, &r)) return -1;
                go = r == 0;
            }
            if (go && l9_u8(o, 0x00810700u) == 0x15u && L9_LT(l9_u32(o, self + 0xB4), 0x40A00000u)) {
                l9_w32(o, DRUM_A0 + 0, l9_u32(o, self + 0xB0));
                l9_w32(o, DRUM_A0 + 4, F_10);
                l9_w32(o, DRUM_A0 + 8, l9_u32(o, self + 0xB8));
                l9_w32(o, DRUM_A0 + 0xC, 0x3F800000u);
                if (l9_c_001EFD20(o, (int32_t)0x8000005Fu, DRUM_A0)) return -1;
                if (l9_c_001FBD50(o, self, 0xDB, 0, 0x44480000u, &r)) return -1;
                l9_w16(o, self + 0x36, 0);
                l9_w8(o, self + 4, 3);
                l9_w8(o, self + 5, 0);
            }
        }
    }
    return 0;
}

int em_level9_port_00156620(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    const uint32_t ent = self + 0x1F0;
    int32_t r = 0;
    uint32_t v = 0;
    switch (l9_u8(&o, self + 4)) {
    case 0:
        if (l9_c_001B0FD0(&o, self, &r) || r != 0) break;
        l9_w16(&o, self + 0x34, 1);
        l9_w8(&o, self + 0, 1);
        if (l9_c_00102948(&o, ent + 0x10, self + 0xB0)) break;
        if (l9_c_00102948(&o, ent + 0x20, self + 0xC0)) break;
        l9_c_001C6380(&o, self);
        break;
    case 1:
        if (l9_s16(&o, self + 0x36) != 0) {
            l9_w8(&o, self + 0, 2);
            l9_w8(&o, self + 4, 2);
            if (l9_c_00102948(&o, DRUM_A0, self + 0xB0)) break;
            if (l9_c_00102948(&o, DRUM_B0, DRUM_A0)) break;
            {
                const uint32_t up = l9_u32(&o, DRUM_A0 + 4), down = l9_u32(&o, DRUM_B0 + 4);
                l9_w32(&o, DRUM_A0 + 4, L9_ADD(up, 0x40800000u));
                l9_w32(&o, DRUM_B0 + 4, L9_SUB(down, 0x40800000u));
            }
            if (l9_c_0019A570(&o, DRUM_A0, DRUM_B0, 4, 0, &r)) break;
            if (r != 0) {
                if (l9_c_001029C0(&o, DRUM_FX)) break;
                if (l9_c_00102B08(&o, DRUM_FX, DRUM_FX, 0x3FC90FDBu)) break;
                if (l9_c_00102918(&o, DRUM_FX, DRUM_FX, self + 0xB0)) break;
                l9_w32(&o, 0x700036D4u, L9_ADD(l9_u32(&o, 0x700036D4u), 0x3E4CCCCDu));
                if (l9_c_001F0460(&o, 4, DRUM_FX)) break;
            }
        }
        if (l9_callback(&o, l9_u32(&o, self + 0x4C), self)) break;
        if (l9_c_001028D0(&o, DRUM_A0, 0x00810350u, self + 0xB0)) break;
        l9_w32(&o, 0x700038ACu, 0);
        if (l9_c_00102738(&o, DRUM_A0, DRUM_A0, &v)) break;
        l9_w32(&o, 0x70003A20u, v);
        l9_w32(&o, 0x70003A24u, 0x42480000u);
        {
            const uint32_t limit = L9_MUL(l9_u32(&o, 0x70003A24u), 0x42480000u);
            v = l9_u32(&o, 0x70003A20u);
            l9_w32(&o, 0x70003A24u, limit);
            r = L9_LT(limit, v);
        }
        if (r) {
            l9_c_001B17A0(&o, self, &r);
        } else {
            l9_w8(&o, self + 1, 1);
            l9_c_001B1D20(&o, self);
        }
        break;
    case 2: {
        const uint32_t s5 = l9_u8(&o, self + 5);
        int landed = 0;
        if (s5 == 0) drum_burst(&o, self, ent);
        else if (s5 == 1) {
            const int32_t t = l9_s16(&o, self + 0x28) - 1;
            l9_w16(&o, self + 0x28, (uint32_t)t);
            if ((int16_t)t == 0) {
                l9_w32(&o, self + 0xC4, l9_u32(&o, ent + 0x74));
                const uint32_t k = l9_u8(&o, self + 3);
                if (k == 0x18 || k == 0x2A) {
                    l9_w8(&o, self + 4, 3);
                    l9_w8(&o, self + 5, 0);
                } else {
                    l9_w8(&o, self + 5, l9_u8(&o, self + 5) + 1u);
                    l9_w16(&o, self + 0x28, 8);
                }
            }
        } else if (s5 == 2 || s5 == 3) {
            if (s5 == 2) {
                const int32_t t = l9_s16(&o, self + 0x28);
                if (t == 0) {
                    l9_w8(&o, self + 5, s5 + 1u);
                    l9_w8(&o, self + 0, 1);
                    l9_w16(&o, self + 0x36, 0);
                    l9_w16(&o, self + 0x34, 1);
                } else l9_w16(&o, self + 0x28, (uint32_t)(t - 1));
            }
            landed = drum_flight(&o, self, ent);
        }
        if (l9_failed(&o) || landed < 0) break;
        if (l9_c_001C6380(&o, self)) break;
        if (l9_c_001A2370(&o, self, self + 0xD0)) break;
        if (l9_c_001B17A0(&o, self, &r)) break;
        l9_callback(&o, l9_u32(&o, self + 0x4C), self);
        break;
    }
    case 3:
        l9_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0013B350 (self, ctl): the 001383C0 creature's +4 2 (byte-matched C): by
 * +5: 0 0013B420 (the hit), 1 0013B5B0 (the flinch), 2 0013B790 (the fall
 * and the burst); then 0013BF20, 0013BE60, ctl +0x70 = 001C64F0(self, 1.0),
 * 00131ED0, 001C6910, 001B17A0, the +0x4C method.
 *   0013B420: +0 = 2, the turn clears, +0xC4 and ctl +0x58 = the wrapped
 *   angle opposite the hit vector +0x70, +0x78; the damage n = +0x36 & 0xFFF
 *   (five times with bit 0x8000); the link +0x20 freed; ctl +0x83 set runs
 *   0021C040 and clears D_008106BD == 1; bit 0x4000 once in 60 ticks effect
 *   0x80000027; +0x34 <= n dies (+5 = 2, 001B4CF0); else +5 = 1, +0x34 -= n.
 *   0013B5B0: step 0 arms the -1.8 / 0.05 rise, sound 0x838, clip 0xF / 0x10
 *   by bits 17, 18 of rand; the rise clamps at 0; while negative the push
 *   (0, 0, -3, 1) through 001B2B10, 001028B8 and 0019AD00; clip end: back to
 *   +4 1 / +5 2 with the timers cleared.
 *   0013B790: step 0 the 420-tick fall, sounds 0x81B, clip 0x11; step 1 on
 *   the floor bit: sounds 0x81C, 0x81E, clip 0x13; the count's end +4 = 3;
 *   step 2 clip end: sound 0x81D, effect 0x8000001E (none: +4 = 3); always
 *   the gravity ctl +0x40 / +0x4C to -3.
 * 0013B9A0 (self, ctl): +4 3: the link freed, ctl +0x83 as above, 001B1190
 * (+0x9A), 001AFC10.
 * ---------------------------------------------------------------------- */
static void hit_unlink(L9 *o, uint32_t self)
{
    const uint32_t link = l9_u32(o, self + 0x20);
    if (link) {
        l9_w8(o, link + 4, 3);
        l9_w32(o, self + 0x20, 0);
    }
}

static void hit_release(L9 *o, uint32_t self, uint32_t ctl)
{
    if (l9_s8(o, ctl + 0x83) != 0) {
        l9_w8(o, ctl + 0x83, 0);
        if (l9_c_0021C040(o, PLAYER, self)) return;
        if (l9_u8(o, 0x008106BDu) == 1u) l9_w8(o, 0x008106BDu, 0);
    }
}

static void l9_0013B420(L9 *o, uint32_t self, uint32_t ctl)
{
    uint32_t a = 0, b = 0;
    int32_t r = 0;
    l9_w8(o, self, 2);
    l9_w8(o, ctl + 0x82, 0);
    l9_w8(o, ctl + 0x84, 0);
    l9_w32(o, ctl + 0x4C, 0x3ECCCCCDu);
    l9_w32(o, ctl + 0x48, 0);
    l9_w32(o, ctl + 0x44, 0);
    l9_w32(o, ctl + 0x5C, 0);
    l9_w32(o, ctl + 0x50, 0);
    l9_w32(o, self + 0xC8, 0);
    if (l9_c_0011E620(o, l9_u32(o, self + 0x70), l9_u32(o, self + 0x78), &a)) return;
    if (l9_c_001B1470(o, L9_ADD(F_PI, a), &b)) return;
    l9_w32(o, self + 0xC4, b);
    l9_w32(o, ctl + 0x58, b);
    const int32_t t = l9_s16(o, self + 0x36);
    int16_t n = (int16_t)(t & 0xFFF);
    if (t & 0x8000) n = (int16_t)(n * 5);
    hit_unlink(o, self);
    hit_release(o, self, ctl);
    if (l9_failed(o)) return;
    if (l9_s8(o, ctl + 0x85) == 0 && (l9_s16(o, self + 0x36) & 0x4000)) {
        l9_w8(o, ctl + 0x85, 0x3C);
        if (l9_c_001EFE00(o, (int32_t)0x80000027u, self, &r)) return;
    }
    if (l9_s16(o, self + 0x34) <= n) {
        l9_w8(o, self + 5, 2);
        l9_w16(o, self + 0x34, 0);
        l9_c_001B4CF0(o, self);
        return;
    }
    l9_w8(o, self + 5, l9_u8(o, self + 5) + 1u);
    l9_w16(o, self + 0x34, (uint32_t)(l9_s16(o, self + 0x34) - n));
}

static void l9_0013B5B0(L9 *o, uint32_t self, uint32_t ctl)
{
    const uint32_t st = l9_u8(o, self + 6);
    int32_t r = 0;
    if (st > 1) return;
    if (st == 0) {
        l9_w8(o, self + 6, st + 1u);
        l9_w32(o, ctl + 0x44, 0xBFE66666u);
        l9_w32(o, ctl + 0x48, 0x3D4CCCCDu);
        if (l9_c_001FBD50(o, self, 0x838, 0, F_300, &r)) return;
        if (l9_c_00122BB8(o, &r)) return;
        if (l9_c_001C67E0(o, self, (l9_sra((uint32_t)r, 0x11) & 3) ? 0xF : 0x10, 0x3F800000u, 0)) return;
    }
    if (!L9_LE(l9_u32(o, ctl + 0x44), 0)) {
        l9_w32(o, ctl + 0x48, 0);
        l9_w32(o, ctl + 0x44, 0);
    }
    if (L9_LT(l9_u32(o, ctl + 0x44), 0)) {
        l9_w32(o, 0x700038A0u, 0);
        l9_w32(o, 0x700038A4u, 0);
        l9_w32(o, 0x700038A8u, 0xC0400000u);
        l9_w32(o, 0x700038ACu, 0x3F800000u);
        if (l9_c_001B2B10(o, self, 0x700038A0u, 0x700038A0u)) return;
        if (l9_c_001028B8(o, 0x700038A0u, 0x700038A0u, self + 0xB0)) return;
        if (l9_c_0019AD00(o, self, 0x700038A0u, (int32_t)0x80000006u, &r)) return;
    }
    if (l9_u32(o, ctl + 0x70) & 0x1000u) {
        l9_w8(o, self, 1);
        l9_w8(o, self + 4, 1);
        l9_w8(o, self + 5, 2);
        l9_w8(o, self + 6, 0);
        l9_w32(o, ctl + 0x48, 0);
        l9_w32(o, ctl + 0x44, 0);
        l9_w32(o, ctl + 0x50, 0xBFC6D3F2u);
        l9_w16(o, ctl + 0x22, 0);
        l9_w16(o, ctl + 0x20, 0);
        if (l9_u16(o, self + 0x36) & 0x5000u) l9_w16(o, ctl + 0x34, 0xA);
        l9_w16(o, self + 0x36, 0);
    }
}

static void l9_0013B790(L9 *o, uint32_t self, uint32_t ctl)
{
    const uint32_t tmp = l9_u8(o, self + 6);
    int32_t r = 0;
    if (tmp == 0) {
        l9_w8(o, self + 6, tmp + 1u);
        l9_w16(o, ctl + 0x20, 0x1A4);
        l9_w32(o, ctl + 0x50, 0);
        l9_w32(o, ctl + 0x4C, 0);
        l9_w32(o, ctl + 0x40, 0xBCA3D70Au);
        if (l9_c_001B1190(o, (int32_t)l9_u8(o, self + 0x9A))) return;
        if (l9_c_001FBD50(o, self, 0x81B, 0, F_300, &r)) return;
        if (l9_c_001C67E0(o, self, 0x11, 0x3F800000u, 0)) return;
    }
    if (tmp <= 1) {
        if (l9_u8(o, ctl + 0x81) & 2u) {
            l9_w8(o, self + 6, l9_u8(o, self + 6) + 1u);
            if (l9_c_001FBD50(o, self, 0x81C, 0, F_300, &r)) return;
            if (l9_c_001FBD50(o, self, 0x81E, 0, F_300, &r)) return;
            if (l9_c_001C67E0(o, self, 0x13, 0x3F800000u, 0)) return;
        }
        const uint32_t cnt = (l9_u16(o, ctl + 0x20) - 1u) & 0xFFFFu;
        l9_w16(o, ctl + 0x20, cnt);
        if (cnt == 0) l9_w8(o, self + 4, 3);
    } else if (tmp == 2) {
        if (l9_u32(o, ctl + 0x70) & 0x1000u) {
            l9_w8(o, self + 6, tmp + 1u);
            l9_w8(o, ctl + 0x87, 0);
            if (l9_c_001FBD50(o, self, 0x81D, 0, F_300, &r)) return;
            if (l9_c_001EFE00(o, (int32_t)0x8000001Eu, self, &r)) return;
            if (r == 0) l9_w8(o, self + 4, 3);
        }
    }
    if (l9_u8(o, ctl + 0x81) & 2u) {
        l9_w32(o, ctl + 0x40, 0);
        l9_w32(o, ctl + 0x4C, 0);
    } else if (L9_LT(l9_u32(o, ctl + 0x4C), 0xC0400000u)) {
        l9_w32(o, ctl + 0x4C, 0xC0400000u);
        l9_w32(o, ctl + 0x40, 0);
    } else {
        l9_w32(o, ctl + 0x40, 0xBCA3D70Au);
    }
    const uint32_t rate = l9_u32(o, ctl + 0x40);
    const uint32_t dv = L9_ADD(l9_u32(o, ctl + 0x4C), rate);
    l9_w32(o, ctl + 0x4C, dv);
    l9_w32(o, self + 0xB4, L9_ADD(l9_u32(o, self + 0xB4), dv));
}

#define HIT_ENTRY(name, body)                                                                           \
    int em_level9_port_##name(const EmLevel9PortHooks *h, uint32_t self, uint32_t ctl, EmLevel9PortFault *fault) \
    {                                                                                                   \
        L9 o;                                                                                           \
        if (l9_begin(&o, h, fault)) return -1;                                                          \
        body;                                                                                           \
        return l9_end(&o);                                                                              \
    }
HIT_ENTRY(0013B420, l9_0013B420(&o, self, ctl))
HIT_ENTRY(0013B5B0, l9_0013B5B0(&o, self, ctl))
HIT_ENTRY(0013B790, l9_0013B790(&o, self, ctl))

int em_level9_port_0013B350(const EmLevel9PortHooks *h, uint32_t self, uint32_t ctl, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t v = 0;
    if (l9_begin(&o, h, fault)) return -1;
    switch (l9_u8(&o, self + 5)) {
    case 0: l9_0013B420(&o, self, ctl); break;
    case 1: l9_0013B5B0(&o, self, ctl); break;
    case 2: l9_0013B790(&o, self, ctl); break;
    default: break;
    }
    if (l9_failed(&o)) return -1;
    if (l9_c_0013BF20(&o, self, ctl)) return -1;
    if (l9_c_0013BE60(&o, self, ctl)) return -1;
    if (l9_c_001C64F0(&o, self, 0x3F800000u, &v)) return -1;
    l9_w32(&o, ctl + 0x70, (uint32_t)v);
    if (l9_c_00131ED0(&o, self)) return -1;
    if (l9_c_001C6910(&o, self)) return -1;
    if (l9_c_001B17A0(&o, self, &v)) return -1;
    l9_callback(&o, l9_u32(&o, self + 0x4C), self);
    return l9_end(&o);
}

int em_level9_port_0013B9A0(const EmLevel9PortHooks *h, uint32_t self, uint32_t ctl, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    hit_unlink(&o, self);
    hit_release(&o, self, ctl);
    if (l9_failed(&o)) return -1;
    if (l9_c_001B1190(&o, (int32_t)l9_u8(&o, self + 0x9A))) return -1;
    l9_c_001AFC10(&o, self);
    return l9_end(&o);
}
