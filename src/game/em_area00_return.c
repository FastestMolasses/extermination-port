/* AREA00 return: AREA01 runtime entries, translated from the matching decomp C.
 * All workers remain explicit and this module is UNWIRED. */
#include "em_area00_return_internal.h"

#define R(a) a00r_u32(o, (a))
#define B(a) a00r_u8(o, (a))
#define W(a, v) a00r_w32(o, (a), (v))
#define WB(a, v) a00r_w8(o, (a), (v))
#define WH(a, v) a00r_w16(o, (a), (v))
#define C(fn, ...) do { if (a00r_c_##fn(o, __VA_ARGS__)) return; } while (0)
#define ONE 0x3F800000u

static int32_t range_00823A90(A00R *o, uint32_t self, uint32_t a1, uint32_t a2)
{
    uint32_t talk = self + 0x1F0, z = R(self + 0xB8), y, x, r = 0;
    if (A00R_LT(z, a00r_bits(-545.0f)) && A00R_LT(a00r_bits(-585.0f), z)) {
        y = R(self + 0xB4);
        if (A00R_LT(y, a00r_bits(50.0f)) && A00R_LT(a00r_bits(35.0f), y)) return 0;
    }
    if (a00r_c_001029C0(o, 0x700036A0)) return 0;
    W(0x700036C0, R(talk + 0x70)); W(0x700036C4, R(talk + 0x74)); W(0x700036C8, R(talk + 0x78));
    W(0x700036B0, R(talk + 0x80)); W(0x700036B4, R(talk + 0x84)); W(0x700036B8, R(talk + 0x88));
    W(0x700038B0, R(talk + 0x80)); W(0x700038B4, R(talk + 0x84)); W(0x700038B8, R(talk + 0x88));
    W(0x700038BC, ONE);
    W(0x700038C0, R(talk + 0x70)); W(0x700038C4, R(talk + 0x74)); W(0x700038C8, R(talk + 0x78));
    W(0x700038CC, ONE);
    if (a00r_c_00102718(o, 0x700038A0, 0x700038B0, 0x700038C0)) return 0;
    x = R(0x700038A0); y = R(0x700038A4); z = R(0x700038A8);
    W(0x700036A0, x); W(0x700036A4, y); W(0x700036A8, z);
    if (a00r_c_001027E0(o, 0x700036E0, 0x700036A0)) return 0;
    if (a00r_c_001028D0(o, 0x700038B0, a1, a2)) return 0;
    if (a00r_c_001026A0(o, 0x700038A0, 0x700036E0, 0x700038B0)) return 0;
    W(0x700038C0, 0); W(0x700038C4, 0); W(0x700038C8, 0);
    y = R(0x700038A0); z = R(0x700038A8);
    if (a00r_c_001B1240(o, 0x700038C0, A00R_NEG(y), A00R_NEG(z), &r)) return 0;
    W(talk + 0xE8, r);
    return 1;
}

static void turn_008240E0(A00R *o, uint32_t self, uint32_t ev)
{
    int32_t res = 0, random = 0, n;
    uint32_t r = 0, goal, cur, state;
    C(001C2770, self, ev, 1, &res);
    state = B(self + 6);
    switch (state) {
    case 0:
        WH(self + 0x28, 0); W(ev + 0xD8, 0);
        if (a00r_c_00122BB8(o, &random)) return;
        WH(ev + 0xD0, (random & 1) ? 30 : 60);
        if (a00r_c_00122BB8(o, &random)) return;
        r = A00R_MUL(a00r_bits(6.2831855f), A00R_CVT_S_W(random & 0xF0));
        r = A00R_DIV(r, a00r_bits(256.0f));
        C(001B1470, r, &r);
        W(ev + 0xE8, r); WB(self + 6, B(self + 6) + 1);
        C(001287F0, self, ev, 5, a00r_bits(8.0f));
        break;
    case 1: case 3:
        cur = R(self + 0xC4); goal = R(ev + 0xE8);
        C(001B12B0, goal, cur, a00r_bits(0.0698131695f), &r);
        W(self + 0xC4, r);
        if (A00R_EQ(r, R(ev + 0xE8))) {
            WB(self + 6, B(self + 6) + 1); W(ev + 0xD8, a00r_bits(0.4f));
            if (state == 3) WH(ev + 0xD0, 20);
        }
        break;
    case 2:
        if (range_00823A90(o, self, self + 0xB0, ev + 0x50)) {
            WB(self + 6, 3); W(ev + 0xD8, 0);
        } else {
            n = (int16_t)(a00r_s16(o, ev + 0xD0) - 1);
            WH(ev + 0xD0, (uint32_t)n);
            if (!n) { WB(self + 6, 0); W(ev + 0xD8, 0); }
        }
        break;
    case 4:
        n = (int16_t)(a00r_s16(o, ev + 0xD0) - 1);
        WH(ev + 0xD0, (uint32_t)n);
        if (!n) WB(self + 6, 2);
        break;
    default: break;
    }
    if (res == 0 || res == 8) C(001C3D60, self, ev);
}

static void owner_00823CD0(A00R *o, uint32_t self)
{
    uint32_t talk = self + 0x1F0, n, p;
    int32_t r = 0;
    switch (B(self + 4)) {
    case 0:
        switch (B(self + 5)) {
        case 0:
            C(001BA1C0, self, 7, &r);
            if (r) { WB(self + 4, 3); break; }
            n = B(self + 0xD); WB(talk + 0xE2, n >> 4);
            WB(self + 0xD, B(self + 0xD) & 15);
            n = B(talk + 0xE2); WB(talk + 0xE3, B(0x2758C8 + n));
            C(00128AB0, self, talk, &r);
            if (r) {
                WB(self + 5, B(self + 5) + 1); WH(talk + 0xF8, 1);
                C(001C63E0, self, 1);
            }
            break;
        case 1:
            n = B(self + 0xD); C(00129780, self, talk, (int32_t)n, &r);
            if (r) WB(self, 2);
            break;
        default: break;
        }
        break;
    case 1:
        C(001029C0, 0x70003000); WB(talk + 0xFA, 0); C(001B17A0, self);
        switch (B(self + 5)) {
        case 0: WB(self + 5, B(self + 5) + 1); WB(self + 6, 0); WB(self + 7, 0); break;
        case 1:
            WB(talk + 0xFA, 1); turn_008240E0(o, self, talk);
            if (B(0x8107DF) == 2) {
                n = B(talk + 0xE3);
                if (!n) { WB(self + 5, 5); WB(self + 6, 0); WB(self + 7, 0); }
                else WB(talk + 0xE3, n - 1);
            }
            break;
        case 2: case 3: case 4: case 5: WB(talk + 0xFA, 1); C(00824340, self, talk); break;
        case 6: WB(talk + 0xFA, 1); C(00824770, self, talk); break;
        case 7: C(00824D50, self, talk); break;
        default: break;
        }
        W(talk + 0xEC, A00R_EQ(R(talk + 0xD8), 0) ? ONE : a00r_bits(2.0f));
        n = R(talk + 0xEC); C(001C64F0, self, n, &r); WH(talk + 0xF4, (uint32_t)r);
        if (B(self + 5) != 7) { C(00102958, 0x70003400, 0x70003000); C(001C69A0, self); }
        if (B(self + 1)) {
            if (a00r_s8(o, talk + 0xFA)) C(001288D0, self, talk);
            if (a00r_method(o, self)) return;
        }
        WB(self + 0xB, 0);
        break;
    case 2: case 3: C(001AFC10, self); break;
    case 4:
        C(001CA6F0, self, 1); WB(self + 4, B(self + 4) + 1);
        W(self + 0x40, R(0x28A6E8)); WH(talk + 0xF8, 1); C(001C63E0, self, 1);
        /* fall through */
    case 5:
        C(001288D0, self, talk); C(001C64F0, self, ONE, &r); WH(talk + 0xF4, (uint32_t)r);
        if (A00R_EQ(R(self + 0x3C), a00r_bits(509.0f))) WB(self + 4, 3);
        C(001C6960, self);
        if (A00R_EQ(R(self + 0x3C), a00r_bits(510.0f))) {
            W(0x700038A0, 0); W(0x700038B0, 0); W(0x700038B4, 0); W(0x700038B8, 0);
            W(0x700038A4, ONE); W(0x700038A8, ONE); W(0x700038AC, ONE); W(0x700038BC, ONE);
            p = R(0x275B40); p = R(p + 0xC);
            C(001026A0, 0x700038A0, p + 0x90, 0x700038A0);
            C(001FB9F0, 0x1B2, 0x1000, 0x1000, 0x1000);
            C(001EFD90, (int32_t)0x80000009, 0x700038A0, 0x700038B0);
        }
        a00r_method(o, self);
        break;
    default: break;
    }
}

static void probe_00826010(A00R *o, uint32_t self, uint32_t sp)
{
    uint32_t pl, n, x, y, rot = sp - 0x10;
    int32_t random = 0, hit = 0, count;
    count = a00r_s16(o, self + 0x2A); pl = R(self + 0x1C);
    WH(self + 0x2A, (uint32_t)(count + 1));
    count = a00r_s16(o, self + 0x2A);
    if (count % 6) return;
    if (count > 180) {
        if (a00r_c_00122BB8(o, &random)) return;
        x = (uint32_t)a00r_sra((uint32_t)random, 16) * 4;
        x = (uint32_t)a00r_sra(x, 15);
        WB(self + 0x240, x + 2); WH(self + 0x2A, 0);
    }
    n = B(self + 0x240);
    if (n) { WB(self + 0x240, n - 1); return; }
    C(00103230, 0x700038A0, pl + 0xC0, a00r_bits(300.0f));
    for (uint32_t i = 0; i < 12; i += 4) {
        x = R(0x700038A0 + i); y = R(pl + 0xB0 + i);
        W(0x700038A0 + i, A00R_ADD(x, y));
    }
    C(001FBD50, self, 0x164, 0, a00r_bits(800.0f)); C(001B0CD0, pl, 0);
    C(0019A570, pl + 0xB0, 0x700038A0, 4, 0x20, &hit);
    if (hit) {
        C(001031E0, 0x700038A0, 0x700031B0);
        W(rot, 0); x = A00R_ADD(a00r_bits(3.14159265f), R(self + 0xC4));
        C(001B1470, x, &x); W(rot + 4, x); W(rot + 8, 0);
        C(001EFD90, (int32_t)0x80000003, 0x700038A0, rot);
    }
}

int em_area00_return_00823A90(const EmArea00ReturnHooks *h, uint32_t self, uint32_t a1,
                             uint32_t a2, int32_t *result, EmArea00ReturnFault *fault)
{
    A00R o; int32_t r;
    if (!result || a00r_begin(&o, h, fault)) return -1;
    r = range_00823A90(&o, self, a1, a2);
    if (a00r_failed(&o)) return -1;
    *result = r; return 0;
}
int em_area00_return_00823CD0(const EmArea00ReturnHooks *h, uint32_t self, EmArea00ReturnFault *fault)
{
    A00R o; if (a00r_begin(&o, h, fault)) return -1;
    owner_00823CD0(&o, self); return a00r_end(&o);
}
int em_area00_return_008240E0(const EmArea00ReturnHooks *h, uint32_t self, uint32_t ev, EmArea00ReturnFault *fault)
{
    A00R o; if (a00r_begin(&o, h, fault)) return -1;
    turn_008240E0(&o, self, ev); return a00r_end(&o);
}
int em_area00_return_00826010(const EmArea00ReturnHooks *h, uint32_t self, uint32_t sp, EmArea00ReturnFault *fault)
{
    A00R o; if (a00r_begin(&o, h, fault)) return -1;
    probe_00826010(&o, self, sp); return a00r_end(&o);
}
