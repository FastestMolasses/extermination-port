/* AREA13 route owners, translated from the matching decomp C. */
#include "em_area13_route_internal.h"
#define B(a) a13r_u8(o, (a))
#define R(a) a13r_u32(o, (a))
#define WB(a,v) a13r_w8(o, (a), (v))
#define WH(a,v) a13r_w16(o, (a), (v))
#define C(fn,...) do { if (a13r_c_##fn(o, __VA_ARGS__)) return; } while (0)

static void door_00823580(A13R *o, uint32_t self)
{
    uint32_t state = B(self + 4), owner = R(self + 0x1C), talk = self + 0x1F0;
    int32_t r = 0;
    switch (state) {
    case 0: C(001BBDA0, self, talk); WB(owner + 0xB, 1); break;
    case 1:
        switch (B(self + 5)) {
        case 0:
            if (B(0x810774) == 1) { WB(self, 2); WB(owner + 0xB, 0); }
            else { WB(self, 1); WB(owner + 0xB, 1); }
            C(001BBE40, self, talk, 0, &r);
            if (r) WB(self + 5, B(self + 5) + 1);
            break;
        case 1:
            C(001BC0E0, self, talk, &r);
            if (r) WB(self + 5, B(self + 5) + 1);
            break;
        case 2: C(001BC240, self, talk); WB(self + 5, B(self + 5) + 1); break;
        case 3: C(001BC290, self, talk, &r); if (r) WB(self + 5, 0); break;
        default: break;
        }
        C(001BC300, self); break;
    case 2: case 3: C(001AFC10, self); break;
    default: break;
    }
}

static void cutscene_00823830(A13R *o, uint32_t self)
{
    int32_t r = 0;
    switch (B(self + 5)) {
    case 0:
        if (A13R_LE(R(0x810354), a13r_bits(170.0f)) && B(0x810702) == 2) {
            C(001AEDB0, 0); C(001EFD20, 7, 0x700038A0);
            C(001BA1A0, self + 0x1F0, 0x82A360);
            WB(self + 5, 1); WB(0x810771, 1);
        }
        break;
    case 1:
        C(001BA1F0, self, &r);
        if (r) {
            C(001C4760, 9, 1); C(001C4760, 0x4F, 1);
            WH(self + 0x2E, 0xFFFF); WB(0x8107F1, 1); WB(self + 5, 0); C(001FAE70, 0);
        }
        break;
    default: break;
    }
}

static void interaction_00823A40(A13R *o, uint32_t self)
{
    int32_t r = 0;
    switch (B(self + 4)) {
    case 0:
        C(001BA1C0, self, 0x1A, &r);
        if (r) { WB(self + 4, 3); break; }
        C(001B1020, self, (int32_t)B(self + 0xD), -1, 0); WB(self, 1); break;
    case 1:
        switch (B(self + 5)) {
        case 0:
            if (A13R_LE(R(0x810354), a13r_bits(170.0f))) {
                C(001B1EA0, 0, 0x810350, 0x82E100, 4, &r);
                if (r) { C(001BA1A0, self + 0x1F0, 0x82A770); WB(self + 5, 1); WH(self + 0x28, 0); }
            }
            break;
        case 1: C(001BA1F0, self, &r); if (r) WB(self + 4, 3); break;
        default: break;
        }
        WB(self + 1, 1); C(001C6380, self); a13r_method(o, self); break;
    case 2: case 3: C(001AFC10, self); break;
    default: break;
    }
}

static void switch_00824A80(A13R *o, uint32_t self)
{
    int32_t r = 0;
    switch (B(self + 4)) {
    case 0:
        C(001BA1C0, self, 0x42, &r);
        if (r) WB(self + 4, 3);
        else { WB(self + 4, 1); WB(self, 1); }
        break;
    case 1:
        switch (B(self + 5)) {
        case 0:
            C(001B1EA0, 0, 0x810350, 0x82E220, 4, &r);
            if (r) { C(001BA1A0, self + 0x1F0, 0x82C510); WB(self + 5, 1); WB(0x81081A, 255); }
            break;
        case 1:
            C(001BA1F0, self, &r);
            if (r) { C(001B6660, 0x82A230); WB(self + 4, 3); }
            break;
        default: break;
        }
        break;
    case 2: case 3: C(001AFC10, self); break;
    default: break;
    }
}

int em_area13_route_00823580(const EmArea13RouteHooks *h, uint32_t self, EmArea13RouteFault *fault)
{
    A13R o; if (a13r_begin(&o, h, fault)) return -1;
    door_00823580(&o, self); return a13r_end(&o);
}
int em_area13_route_00823830(const EmArea13RouteHooks *h, uint32_t self, EmArea13RouteFault *fault)
{
    A13R o; if (a13r_begin(&o, h, fault)) return -1;
    cutscene_00823830(&o, self); return a13r_end(&o);
}
int em_area13_route_00823A40(const EmArea13RouteHooks *h, uint32_t self, EmArea13RouteFault *fault)
{
    A13R o; if (a13r_begin(&o, h, fault)) return -1;
    interaction_00823A40(&o, self); return a13r_end(&o);
}
int em_area13_route_00824A80(const EmArea13RouteHooks *h, uint32_t self, EmArea13RouteFault *fault)
{
    A13R o; if (a13r_begin(&o, h, fault)) return -1;
    switch_00824A80(&o, self); return a13r_end(&o);
}
