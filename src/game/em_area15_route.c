/* AREA15 continuation handlers, translated from matching decomp C. UNWIRED. */
#include "em_area15_route_internal.h"
#define B(a) a15r_u8(o, (a))
#define R(a) a15r_u32(o, (a))
#define W(a,v) a15r_w32(o, (a), (v))
#define WB(a,v) a15r_w8(o, (a), (v))
#define WH(a,v) a15r_w16(o, (a), (v))
#define C(fn,...) do { if (a15r_c_##fn(o, __VA_ARGS__)) return; } while (0)
#define CF0(fn) do { if (a15r_c_##fn(o)) return; } while (0)
#define ONE 0x3F800000u
#define HALF 0x3F000000u

static void countdown_00823B40(A15R *o, uint32_t self)
{
    int32_t r = 0, timer;
    uint32_t y, x;
    if (B(self + 0xD) == 0x54) {
        y = R(0x810354);
        if (A15R_LT(y, a15r_bits(291.0f))) WB(0x8107FB, 2);
        x = R(0x810350);
        if (A15R_LT(a15r_bits(893.0f), x) && A15R_LT(a15r_bits(329.0f), y) &&
            A15R_LT(a15r_bits(940.0f), R(0x810358))) WB(0x8107FB, 2);
        timer = (int16_t)(a15r_s16(o, self + 0x28) - 1);
        WH(self + 0x28, (uint32_t)timer);
        if (a15r_s16(o, self + 0x28) <= 0) { WB(0x8107FB, 2); WB(self + 5, 0); WH(self + 0x28, 0); }
    }
    C(001C64F0, self, ONE, &r); C(001C68C0, self); C(001B17A0, self);
    if (a15r_method(o, self)) return;
    C(00823E40, self);
}

static void transition_00823C80(A15R *o, uint32_t self)
{
    int32_t r = 0, timer;
    if (B(self + 0xD) != 0x54) { WB(self + 4, 3); return; }
    switch (B(self + 5)) {
    case 0: C(001BA1A0, self + 0x1F0, 0x8277C0); WB(self + 5, 1); WH(self + 0x2E, 0); break;
    case 1:
        C(001BA1F0, self, &r);
        if (r) {
            C(001C47A0, 6, 1); C(001C4760, 0xE, 1);
            WH(self + 0x2E, 0xFFFF); WB(self + 5, 2); WB(0x70003B8D, 3);
        }
        break;
    case 2:
        switch (B(self + 6)) {
        case 0:
            CF0(001FABB0); CF0(001FBC50); C(001AEDE0, 255, 0);
            WH(self + 0x28, 60); WB(self + 6, 1); break;
        case 1:
            timer = (int16_t)(a15r_s16(o, self + 0x28) - 1);
            WH(self + 0x28, (uint32_t)timer);
            if (!a15r_s16(o, self + 0x28)) WB(self + 5, 3);
            break;
        default: break;
        }
        break;
    case 3:
        WB(0x8107FB, 255); WB(0x8106B8, 1); WB(0x8106B5, 15);
        WB(0x8106B6, 0); WB(0x8106B7, 0); WB(self + 4, 3);
        WB(0x810730, 0x82); WB(0x810731, 0x81); WB(0x810732, 0x82);
        WB(0x810734, 0x81); WB(0x810736, 0x81); break;
    default: break;
    }
}

static int32_t camera_00823F60(A15R *o, uint32_t self, uint32_t a1, uint32_t a2)
{
    uint32_t obj = R(0x8106C0);
    (void)self;
    if (!obj) return 1;
    if (B(obj + 4) == 3) { W(0x8106C0, 0); return 1; }
    switch (B(a1 + 4)) {
    case 0:
        if (a15r_c_00102948(o, 0x8105D0, a2 + 0x20) ||
            a15r_c_00102948(o, 0x8105E0, obj + 0xB0) ||
            a15r_c_00102948(o, 0x8101F0, a2 + 0x20) ||
            a15r_c_00102948(o, 0x810200, obj + 0xB0)) return 0;
        WH(obj + 0x36, 100); WB(obj + 4, 2); WB(obj + 5, 0); WB(a1 + 4, 1); break;
    case 1:
        if (a15r_c_00102948(o, 0x8105E0, obj + 0xB0)) return 0;
        a15r_c_00102948(o, 0x810200, obj + 0xB0); break;
    default: break;
    }
    return 0;
}

static void release_00824240(A15R *o, uint32_t self)
{
    int32_t r = 0;
    switch (B(self + 5)) {
    case 0:
        C(001BA1C0, self, 0x23, &r);
        if (r) {
            C(001BA1A0, self + 0x1F0, 0x827D70); C(001CA6F0, self, 2);
            CF0(001FABB0); CF0(001FBC50); WB(self + 5, 1);
        }
        break;
    case 1:
        C(001BA1F0, self, &r);
        if (r) {
            WH(self + 0x2E, 255); WB(self + 5, 0); W(self + 0x40, R(0x28A624));
            C(001C67E0, self, 0, a15r_bits(20.0f), 0);
            WB(0x8107FC, 1); C(001C47A0, 0x25, 1); C(001FAE70, 0);
        }
        C(001BA580, self, (int32_t)B(self + 0xD)); C(001C64F0, self, HALF, &r);
        WH(self + 0x1FE, (uint32_t)r); break;
    default: break;
    }
}

static void script_00824350(A15R *o, uint32_t self)
{
    int32_t r = 0;
    switch (B(self + 5)) {
    case 0: C(001BA1A0, self + 0x1F0, 0x8281F0); WB(self + 5, 1); break;
    case 1:
        C(001BA1F0, self, &r);
        if (r) { WB(0x8107FC, 2); C(001C4760, 15, 1); WB(self + 5, 0); }
        break;
    default: break;
    }
}

static void talk_008243E0(A15R *o, uint32_t self)
{
    int32_t r = 0;
    switch (B(self + 5)) {
    case 0:
        if (B(self + 0xB) & 4) {
            C(001BA1A0, self + 0x1F0, B(self + 0xD) == 0x50 ? 0x8287B0 : 0x828670);
            WB(self + 5, 1);
        }
        break;
    case 1:
        C(001BA1F0, self, &r);
        if (r) {
            C(001C67E0, self, B(self + 0xD) == 0x50 ? 9 : 0, a15r_bits(20.0f), 0);
            WB(self + 0xB, 0); WB(self + 5, 0);
        }
        break;
    default: break;
    }
    C(001BA580, self, (int32_t)B(self + 0xD)); C(001C64F0, self, ONE, &r);
    C(001B17A0, self); C(001C68C0, self); a15r_method(o, self);
}

int em_area15_route_00823B40(const EmArea15RouteHooks *h, uint32_t self, EmArea15RouteFault *fault)
{
    A15R o; if (a15r_begin(&o, h, fault)) return -1;
    countdown_00823B40(&o, self); return a15r_end(&o);
}

int em_area15_route_00823C80(const EmArea15RouteHooks *h, uint32_t self, EmArea15RouteFault *fault)
{
    A15R o; if (a15r_begin(&o, h, fault)) return -1;
    transition_00823C80(&o, self); return a15r_end(&o);
}

int em_area15_route_00824240(const EmArea15RouteHooks *h, uint32_t self, EmArea15RouteFault *fault)
{
    A15R o; if (a15r_begin(&o, h, fault)) return -1;
    release_00824240(&o, self); return a15r_end(&o);
}

int em_area15_route_00824350(const EmArea15RouteHooks *h, uint32_t self, EmArea15RouteFault *fault)
{
    A15R o; if (a15r_begin(&o, h, fault)) return -1;
    script_00824350(&o, self); return a15r_end(&o);
}

int em_area15_route_008243E0(const EmArea15RouteHooks *h, uint32_t self, EmArea15RouteFault *fault)
{
    A15R o; if (a15r_begin(&o, h, fault)) return -1;
    talk_008243E0(&o, self); return a15r_end(&o);
}

int em_area15_route_00823F60(const EmArea15RouteHooks *h, uint32_t self, uint32_t a1,
                            uint32_t a2, int32_t *result, EmArea15RouteFault *fault)
{
    A15R o; int32_t r;
    if (!result || a15r_begin(&o, h, fault)) return -1;
    r = camera_00823F60(&o, self, a1, a2);
    if (a15r_failed(&o)) return -1;
    *result = r; return 0;
}
