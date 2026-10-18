/* AREA19 00824690 flame owner from matching decomp C. UNWIRED. */
#include "em_area19_route_internal.h"
#define B(a) a19r_u8(o, (a))
#define R(a) a19r_u32(o, (a))
#define W(a,v) a19r_w32(o, (a), (v))
#define WB(a,v) a19r_w8(o, (a), (v))
#define F(v) a19r_bits(v)
#define V(v) a19r_float(v)
#define C(fn,...) do { if (a19r_c_##fn(o, __VA_ARGS__)) return; } while (0)

static void flame_00824690(A19R *o, uint32_t self, uint32_t sp)
{
    uint32_t work = self + 0x1F0, packet = sp - 0x70, vector = sp - 0x10;
    uint32_t y, phase, rnd, offset;
    int32_t r = 0, handle = 0;
    switch (B(self + 4)) {
    case 0:
        C(001029C0, self + 0xD0); C(00102C58, self + 0xD0, self + 0xD0, self + 0xC0);
        C(00102918, self + 0xD0, self + 0xD0, self + 0xB0);
        W(work + 0x10, 0xFFFFFFFF); W(work, 0); W(work + 4, 0);
        if (a19r_c_00122BB8(o, &r)) return;
        W(work + 8, A19R_DIV(A19R_CVT_S_W(r), F(2147483648.0f)));
        if (a19r_c_00122BB8(o, &r)) return;
        W(work + 0xC, A19R_DIV(A19R_CVT_S_W(r), F(2147483648.0f)));
        WB(self + 4, 1);
        /* fall through */
    case 1:
        y = B(0x810775) & 1 ? F(-120.0f) : F(-110.0f);
        W(0x700038A0, 0); W(0x700038A4, y); W(0x700038A8, F(30.0f)); W(0x700038AC, F(1.0f));
        C(001026A0, 0x700038A0, self + 0xD0, 0x700038A0);
        W(0x82B234, A19R_ADD(F(5.0f), y));
        phase = R(work); rnd = R(work + 8);
        C(001CFA60, packet, self + 0xD0, V(phase), V(rnd));
        C(001CFBE0, 0xFFB000, 1, 0x82B1B0, packet, 1);
        phase = A19R_ADD(R(work), F(0.015f)); W(work, phase);
        if (!A19R_LT(phase, F(2.0f))) W(work, A19R_SUB(R(work), F(1.0f)));
        C(001029C0, 0x700036A0); C(00102918, 0x700036A0, 0x700036A0, 0x700038A0);
        C(001CCF70, 0x700036D0, &handle);
        phase = R(work + 4); rnd = R(work + 0xC);
        C(001CFA60, packet, 0x700036A0, V(phase), V(rnd));
        C(001CFBE0, handle, 1, 0x82B240, packet, 0);
        phase = A19R_ADD(R(work + 4), F(0.02f)); W(work + 4, phase);
        if (!A19R_LT(phase, F(2.0f))) W(work + 4, A19R_SUB(R(work + 4), F(1.0f)));
        if (a19r_c_00122BB8(o, &r)) return;
        rnd = A19R_MUL(F(4.656613e-10f), A19R_CVT_S_W(r));
        if (!A19R_LE(rnd, F(0.5f))) {
            C(00102948, vector, 0x700036D0);
            if (a19r_c_00122BB8(o, &r)) return;
            rnd = A19R_MUL(F(4.656613e-10f), A19R_CVT_S_W(r));
            offset = A19R_ADD(F(-10.0f), A19R_MUL(F(20.0f), rnd));
            W(vector, A19R_ADD(R(vector), offset));
            if (a19r_c_00122BB8(o, &r)) return;
            rnd = A19R_MUL(F(4.656613e-10f), A19R_CVT_S_W(r));
            offset = A19R_ADD(F(-10.0f), A19R_MUL(F(20.0f), rnd));
            W(vector + 8, A19R_ADD(R(vector + 8), offset));
            if (a19r_c_00122BB8(o, &r)) return;
            rnd = A19R_MUL(F(4.656613e-10f), A19R_CVT_S_W(r));
            C(001E8B90, vector, V(A19R_ADD(0, A19R_MUL(F(0.8f), rnd))));
        }
        C(001FC3C0, self, work + 0x10, 0x419, 300.0f, 4096.0f);
        break;
    case 2: case 3: C(001FC520, work + 0x10); C(001AFC10, self); break;
    default: break;
    }
}

int em_area19_route_00824690(const EmArea19RouteHooks *h, uint32_t self, uint32_t sp, EmArea19RouteFault *fault)
{
    A19R o; if (a19r_begin(&o, h, fault)) return -1;
    flame_00824690(&o, self, sp); return a19r_end(&o);
}
