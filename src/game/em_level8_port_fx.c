/* Packet / effect functions of the eighth level (see em_level8_port.h,
 * docs/LEVEL8_PORT.md): 001EAD70 and 001ED100 (packet pairs on
 * D_0081F8F0 with the view's random seed), 001F91C0 (the bone-part effect
 * of a creature by its type +3). The rules of em_level8_port_lift.c apply.
 */
#include "em_level8_port_internal.h"

#define D_00275C34 0x00275C34u /* pointer: the view block (+4 the random seed, +8, +0x54) */
#define D_0081F8F0 0x0081F8F0u /* the packet the pairs build */

/* The seed step of both packet functions: r = view +4; the float
 * ((r >> 16) & 0xFFFF) / 65535 + 0.0001 (the add after the store);
 * view +4 = r * 37 + 11 (32-bit wrapping); then the view pointer is read
 * again and its +0x54 is f12. Returns f13; *f12 receives the +0x54 bits. */
static uint32_t l8_seed_step(L8 *o, uint32_t *f12)
{
    uint32_t view = l8_u32(o, D_00275C34);
    uint32_t r = l8_u32(o, view + 4);
    uint32_t q = L8_DIV(L8_CVT((uint32_t)l8_sra(r, 16) & 0xFFFFu), 0x477FFF00u);
    l8_w32(o, view + 4, r * 37u + 11u);
    uint32_t again = l8_u32(o, D_00275C34);
    *f12 = l8_u32(o, again + 0x54);
    return L8_ADD(q, 0x38D1B717u);
}

/* ------------------------------------------------------------------------
 * 001EAD70 (C byte-identical): two 001CFB50(D_0081F8F0, 0, a0, view +0x54,
 * seed, 1.0, 1e-6, 5.0) / 001CFBE0(a1, mode, table, D_0081F8F0, 0) pairs:
 * mode 1 with the register table 0x2556B0, then mode 0 with 0x255740.
 * ---------------------------------------------------------------------- */
int em_level8_port_001EAD70(const EmLevel8PortHooks *h, int32_t a0, int32_t a1, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    static const uint32_t tables[2] = {0x002556B0u, 0x00255740u};
    for (int k = 0; k < 2; k++) {
        uint32_t f12 = 0;
        uint32_t f13 = l8_seed_step(&o, &f12);
        if (l8_c_001CFB50(&o, D_0081F8F0, 0, a0, fl(f12), fl(f13), fl(F_ONE), fl(0x358637BDu), fl(F_5))) return -1;
        if (l8_c_001CFBE0(&o, a1, 1 - k, tables[k], D_0081F8F0, 0)) return -1;
    }
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 001ED100 (NEARMISS; from the instructions): first fills three GIF
 * register tables: 0x257380 = (16, 4, 4, 0, 0, 0, 0, 0), 0x257410 = (96, 0,
 * 0, 96, 96, 0, 0, 0), 0x2574A0 = (96, 0, 0, 96, 96, 0, 0, 0) (the last
 * word of that table is stored after the first read of the view pointer);
 * then three 001CFB50(D_0081F8F0, 0, a0, view +0x54, seed, 1.0, 1e-6,
 * 10.0) / 001CFBE0(a1, 1, table, D_0081F8F0, 1) pairs with the tables
 * 0x257360, 0x2573F0, 0x257480. Last, view +8 += (0.02 - view +8) / 10
 * (stored), then view +8 read again (the view pointer too) is stored back,
 * raised to 0.02 when below it.
 * ---------------------------------------------------------------------- */
int em_level8_port_001ED100(const EmLevel8PortHooks *h, int32_t a0, int32_t a1, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    static const uint32_t words[] = {0x41800000u, 0x40800000u, 0x40800000u, 0, 0, 0, 0, 0};
    static const uint32_t wide[] = {0x42C00000u, 0, 0, 0x42C00000u, 0x42C00000u, 0, 0, 0};
    for (uint32_t k = 0; k < 8; k++) l8_w32(&o, 0x00257380u + 4u * k, words[k]);
    for (uint32_t k = 0; k < 8; k++) l8_w32(&o, 0x00257410u + 4u * k, wide[k]);
    for (uint32_t k = 0; k < 7; k++) l8_w32(&o, 0x002574A0u + 4u * k, wide[k]);
    /* The first seed step, with the table's last store after the pointer read. */
    uint32_t view = l8_u32(&o, D_00275C34);
    l8_w32(&o, 0x002574BCu, wide[7]);
    uint32_t r = l8_u32(&o, view + 4);
    uint32_t q = L8_DIV(L8_CVT((uint32_t)l8_sra(r, 16) & 0xFFFFu), 0x477FFF00u);
    l8_w32(&o, view + 4, r * 37u + 11u);
    uint32_t again = l8_u32(&o, D_00275C34);
    uint32_t f12 = l8_u32(&o, again + 0x54);
    uint32_t f13 = L8_ADD(q, 0x38D1B717u);
    static const uint32_t tables[3] = {0x00257360u, 0x002573F0u, 0x00257480u};
    for (int k = 0; k < 3; k++) {
        if (k) f13 = l8_seed_step(&o, &f12);
        if (l8_c_001CFB50(&o, D_0081F8F0, 0, a0, fl(f12), fl(f13), fl(F_ONE), fl(0x358637BDu), fl(0x41200000u)))
            return -1;
        if (l8_c_001CFBE0(&o, a1, 1, tables[k], D_0081F8F0, 1)) return -1;
    }
    uint32_t v = l8_u32(&o, D_00275C34);
    uint32_t cur = l8_u32(&o, v + 8);
    uint32_t step = L8_DIV(L8_SUB(0x3CA3D70Au, cur), 0x41200000u);
    l8_w32(&o, v + 8, L8_ADD(cur, step));
    uint32_t v2 = l8_u32(&o, D_00275C34);
    uint32_t now = l8_u32(&o, v2 + 8);
    if (L8_LT(now, 0x3CA3D70Au)) now = 0x3CA3D70Au;
    l8_w32(&o, v2 + 8, now);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 001F91C0 (NEARMISS; from the instructions): the per-part effects of a
 * creature by its type +3. The frame is 0xD0 bytes; its locals at sp -0x40
 * .. -0x04 (frame +0x90 .. +0xCC) are the vectors the callees get.
 *   type 9 / 10 / 11 / 2: the part table 0x25DC00 / 0x25DC90 / 0x25DCF0 /
 *   0x25DB20 (16-byte rows: a part index, two floats; -1 ends it) and the
 *   radius 100 / 500 / 100 / 40; any other type: 00102948(self +0xB0,
 *   (self +0x114) +0xC0) and 001B5360(self), and nothing more.
 * For each row: p = self +0x110[index]; v = 0.4 p (+0x90..), w 1, through
 * p +0x90 (001026A0 in place); +0xB0 = v (00102948); +0xA4 += 1 (read
 * before any store of this row: the previous row's or the stack's word);
 * +0xB4 = v.y - 500. Then by type (read again): 9 / 11 / 2 place the
 * segment (+0xA0 = (v.x, -60 / 35 / 220, v.z, 1), +0xB0 = (0, 1, 0, 1))
 * with the widths 1.3 row +4 / +8; 10 first probes 0019A570(+0x90, +0xB0,
 * 6, 0) and skips the row when it hits or when p +0xC4 <= 10, else the
 * same with 10 and 1.5; any other type ends the whole function. Then (type
 * 2, read again) the widths scale by the length of p's (+0x90, +0x98)
 * (0011E748) and the larger is kept as both limits; +0xC0 = p's point
 * through p +0x90, minus p +0xC0, normalised (001026A0, 001028D0,
 * 00102760); 001F8D30(+0x90, +0xA0, +0xB0, +0xC0, w22, w21, radius,
 * 0x25DAE0).
 * ---------------------------------------------------------------------- */
int em_level8_port_001F91C0(const EmLevel8PortHooks *h, uint32_t self, uint32_t sp, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    uint32_t fr = sp - 0xD0u;
    uint32_t type = l8_u8(&o, self + 3);
    uint32_t table, radius;
    if (type == 2) {
        table = 0x0025DB20u;
        radius = 0x42200000u;
    } else if (type == 0x0B) {
        table = 0x0025DCF0u;
        radius = 0x42C80000u;
    } else if (type == 0x0A) {
        table = 0x0025DC90u;
        radius = 0x43FA0000u;
    } else if (type == 9) {
        table = 0x0025DC00u;
        radius = 0x42C80000u;
    } else {
        uint32_t other = l8_u32(&o, self + 0x114);
        if (l8_c_00102948(&o, self + 0xB0, other + 0xC0)) return -1;
        l8_c_001B5360(&o, self);
        return l8_end(&o);
    }
    uint32_t w21 = 0, w22 = 0;
    for (;;) {
        int32_t index = (int32_t)l8_u32(&o, table);
        if (index == -1) break;
        uint32_t p = l8_u32(&o, self + ((uint32_t)index << 2) + 0x110u);
        l8_w32(&o, fr + 0x90, L8_MUL(0x3ECCCCCDu, l8_u32(&o, p)));
        l8_w32(&o, fr + 0x94, L8_MUL(0x3ECCCCCDu, l8_u32(&o, p + 4)));
        l8_w32(&o, fr + 0x98, L8_MUL(0x3ECCCCCDu, l8_u32(&o, p + 8)));
        l8_w32(&o, fr + 0x9C, F_ONE);
        if (l8_c_001026A0(&o, fr + 0x90, p + 0x90, fr + 0x90)) return -1;
        if (l8_c_00102948(&o, fr + 0xB0, fr + 0x90)) return -1;
        l8_w32(&o, fr + 0xA4, L8_ADD(l8_u32(&o, fr + 0xA4), F_ONE));
        l8_w32(&o, fr + 0xB4, L8_SUB(l8_u32(&o, fr + 0x94), 0x43FA0000u));
        uint32_t t = l8_u8(&o, self + 3);
        int placed = 0;
        uint32_t scale = 0, height = 0;
        if (t == 2) {
            scale = 0x3FA66666u;
            height = 0x435C0000u;
            placed = 1;
        } else if (t == 0x0B) {
            scale = 0x3FA66666u;
            height = 0x420C0000u;
            placed = 1;
        } else if (t == 0x0A) {
            int32_t hit = 0;
            if (l8_c_0019A570(&o, fr + 0x90, fr + 0xB0, 6, 0, &hit)) return -1;
            if (!hit && !L8_LE(l8_u32(&o, p + 0xC4), 0x41200000u)) {
                scale = 0x3FC00000u;
                height = 0x41200000u;
                placed = 1;
            }
        } else if (t == 9) {
            scale = 0x3FA66666u;
            height = 0xC2700000u;
            placed = 1;
        } else {
            return l8_end(&o);
        }
        if (placed) {
            uint32_t a = l8_u32(&o, table + 4);
            uint32_t b = l8_u32(&o, table + 8);
            uint32_t x = l8_u32(&o, fr + 0x90);
            w21 = L8_MUL(scale, a);
            l8_w32(&o, fr + 0xA0, x);
            l8_w32(&o, fr + 0xA4, height);
            uint32_t z = l8_u32(&o, fr + 0x98);
            w22 = L8_MUL(scale, b);
            l8_w32(&o, fr + 0xA8, z);
            l8_w32(&o, fr + 0xAC, F_ONE);
            l8_w32(&o, fr + 0xB0, 0);
            l8_w32(&o, fr + 0xB4, F_ONE);
            l8_w32(&o, fr + 0xB8, 0);
            l8_w32(&o, fr + 0xBC, F_ONE);
            if (l8_u8(&o, self + 3) == 2) {
                uint32_t px = l8_u32(&o, p + 0x90);
                uint32_t pz = l8_u32(&o, p + 0x98);
                float len = 0.0f;
                if (l8_c_0011E748(&o, fl(L8_MADD(L8_MUL(px, px), pz, pz)), &len)) return -1;
                if (L8_LE(w21, w22)) {
                    w22 = L8_MUL(w22, l8_bits(len));
                    if (L8_LT(w22, w21)) w22 = w21;
                } else {
                    w21 = L8_MUL(w21, l8_bits(len));
                    if (L8_LT(w21, w22)) w21 = w22;
                }
            }
            l8_w32(&o, fr + 0xC0, l8_u32(&o, p));
            l8_w32(&o, fr + 0xC4, l8_u32(&o, p + 4));
            l8_w32(&o, fr + 0xC8, l8_u32(&o, p + 8));
            l8_w32(&o, fr + 0xCC, F_ONE);
            if (l8_c_001026A0(&o, fr + 0xC0, p + 0x90, fr + 0xC0)) return -1;
            if (l8_c_001028D0(&o, fr + 0xC0, fr + 0xC0, p + 0xC0)) return -1;
            if (l8_c_00102760(&o, fr + 0xC0, fr + 0xC0)) return -1;
            if (l8_c_001F8D30(&o, fr + 0x90, fr + 0xA0, fr + 0xB0, fr + 0xC0, fl(w22), fl(w21), fl(radius),
                              0x0025DAE0u))
                return -1;
        }
        table += 0x10u;
    }
    return l8_end(&o);
}
