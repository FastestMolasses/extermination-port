/* AREA03 partner and script callbacks. UNWIRED; matching decomp C plus
 * independent original-instruction comparisons. */
#include "em_area03_route_internal.h"

int em_area03_route_00825290(const EmArea03RouteHooks *h, uint32_t self, EmArea03RouteFault *fault)
{
    A03R o;
    int32_t r = 0;
    if (a03r_begin(&o, h, fault)) return -1;
    switch (a03r_u8(&o, self + 4)) {
    case 0:
        (void)a03r_c_001B0FD0(&o, self, &r);
        if (r == 0) {
            a03r_w16(&o, self + 0x34, 1);
            a03r_w8(&o, self + 0, 1);
            r = 0;
            (void)a03r_c_001B11E0(&o, (int32_t)a03r_u8(&o, self + 0x9A), &r);
            if (r != 0) {
                a03r_w8(&o, a03r_u32(&o, self + 0x18) + 4, 2);
                a03r_w8(&o, self + 4, 3);
            }
        }
        break;
    case 1:
        if (a03r_s16(&o, self + 0x36) != 0) {
            a03r_w8(&o, self + 0, 2);
            a03r_w8(&o, a03r_u32(&o, self + 0x18) + 4, 2);
            a03r_w32(&o, a03r_u32(&o, self + 0x18) + 0x21C, 0x5A);
            r = 0;
            (void)a03r_c_001EFE00(&o, (int32_t)0x80000045u, self, &r);
            if (r != 0) {
                (void)a03r_c_001FBD50(&o, self, 0x426, 0, 300.0f, &r);
                a03r_w16(&o, self + 0x28, 0);
                a03r_w8(&o, self + 4, 2);
            } else {
                a03r_w8(&o, self + 4, 3);
            }
        } else {
            (void)a03r_c_001C6380(&o, self);
            (void)a03r_c_001B17A0(&o, self, &r);
            a03r_method(&o, self);
        }
        break;
    case 2: {
        /* one load for the test and the increment, then the stored value
         * is loaded again for the == 10 test (the original's loads) */
        int32_t count = a03r_s16(&o, self + 0x28);
        if (count < 0xA) {
            a03r_w16(&o, self + 0x28, (uint32_t)(count + 1));
            if (a03r_s16(&o, self + 0x28) == 0xA)
                (void)a03r_c_001FBD50(&o, self, 0x427, 0, 300.0f, &r);
        }
        (void)a03r_c_001B1190(&o, (int32_t)a03r_u8(&o, self + 0x9A));
        (void)a03r_c_001B17A0(&o, self, &r);
        a03r_method(&o, self);
        break;
    }
    default: /* 3 and anything else */
        (void)a03r_c_001AFC10(&o, self);
        break;
    }
    return a03r_end(&o);
}

/* The census pad starts at 00823540; the executed init begins at 00823580. */
int em_area03_route_00823580(const EmArea03RouteHooks *h, EmArea03RouteFault *fault)
{
    A03R o; if (a03r_begin(&o, h, fault)) return -1;
    a03r_w32(&o, 0x275C28, 0x20);
    a03r_w32(&o, 0x275C2C, 0);
    a03r_w32(&o, 0x275C24, 0);
    a03r_w32(&o, 0x275C1C, 0x829100);
    return a03r_end(&o);
}

int em_area03_route_00826270(const EmArea03RouteHooks *h, int32_t *result, EmArea03RouteFault *fault)
{
    A03R o; uint32_t flag, y, camera, target;
    if (!result || a03r_begin(&o, h, fault)) return -1;
    flag = a03r_u32(&o, 0x275CA0);
    y = a03r_u32(&o, 0x810354);
    camera = a03r_u32(&o, 0x8105D4);
    a03r_w8(&o, 0x810702, flag ? 3 : 2);
    a03r_w32(&o, 0x810354, flag ? A03R_ADD(y, a03r_bits(104.5f)) : A03R_SUB(y, a03r_bits(104.5f)));
    a03r_w32(&o, 0x8105D4, flag ? A03R_ADD(camera, a03r_bits(104.5f)) : A03R_SUB(camera, a03r_bits(104.5f)));
    target = a03r_u32(&o, 0x8105E4);
    a03r_w32(&o, 0x8105E4, flag ? A03R_ADD(target, a03r_bits(104.5f)) : A03R_SUB(target, a03r_bits(104.5f)));
    if (a03r_failed(&o)) return -1;
    *result = 1; return 0;
}

int em_area03_route_00826330(const EmArea03RouteHooks *h, uint32_t a0, uint32_t a1, uint32_t a2,
                            int32_t *result, EmArea03RouteFault *fault)
{
    A03R o; (void)a0; (void)a1;
    if (!result || a03r_begin(&o, h, fault)) return -1;
    a03r_w32(&o, 0x275CA4, a03r_u32(&o, a2 + 8));
    if (a03r_failed(&o)) return -1;
    *result = 1; return 0;
}

int em_area03_route_00826860(const EmArea03RouteHooks *h, int32_t *result, EmArea03RouteFault *fault)
{
    A03R o;
    if (!result || a03r_begin(&o, h, fault)) return -1;
    a03r_w32(&o, 0x275CA0, !a03r_u32(&o, 0x275CA0));
    if (a03r_failed(&o)) return -1;
    *result = 1; return 0;
}

static int sound_callback(const EmArea03RouteHooks *h, uint32_t self, int32_t cue,
                          int32_t *result, EmArea03RouteFault *fault)
{
    A03R o; int32_t unused = 0;
    if (!result || a03r_begin(&o, h, fault)) return -1;
    if (a03r_c_001FBD50(&o, self, cue, 0, 300.0f, &unused)) return -1;
    *result = 1; return 0;
}
int em_area03_route_00826880(const EmArea03RouteHooks *h, uint32_t self,
                            int32_t *result, EmArea03RouteFault *fault)
{
    return sound_callback(h, self, 0x40B, result, fault);
}
int em_area03_route_008268B0(const EmArea03RouteHooks *h, uint32_t self,
                            int32_t *result, EmArea03RouteFault *fault)
{
    return sound_callback(h, self, 0x3F7, result, fault);
}
