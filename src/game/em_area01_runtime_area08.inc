/* AREA08's and AREA07 sub 0's overlay owners in the shared composition (lane
 * P1; docs/AREA08_PORT.md): em_area08_route's entries, proven by
 * tools/test_area08_route_reference.py, through the generated hooks. As
 * dispatch_a15: each entry's frame is read from its first instruction and
 * nested hooks get the entry's sp less that frame. Returns 0 when the address
 * is not one of the area's entries. */
static int dispatch_a08(EmArea01Runtime *r, EmArea01Call *c, int *out)
{
    static const struct { uint32_t fn, frame; uint8_t na; } owners08[] = {
        {0x00823580u, 0u, 1}, {0x008235E0u, 0u, 1}, {0x00823870u, 0x30u, 1}, {0x00823AC0u, 0x40u, 1},
        {0x00824210u, 0x20u, 1}, {0x00824460u, 0x10u, 0}, {0x008245E0u, 0x20u, 1}, {0x008249F0u, 0x30u, 1},
        {0x00824BC0u, 0xC0u, 1}};
    static const struct { uint32_t fn, frame; uint8_t na; } owners07[] = {{0x00823DA0u, 0x30u, 1}};
    const uint32_t fn = c->function;
    const int a07 = r->area == 7;
    uint32_t frame = 0;
    unsigned na = 0, k, found = 0;
    if (a07) {
        for (k = 0; k < sizeof owners07 / sizeof owners07[0]; ++k)
            if (owners07[k].fn == fn) { frame = owners07[k].frame; na = owners07[k].na; found = 1; }
    } else {
        for (k = 0; k < sizeof owners08 / sizeof owners08[0]; ++k)
            if (owners08[k].fn == fn) { frame = owners08[k].frame; na = owners08[k].na; found = 1; }
    }
    if (!found) return 0;
    if (area_frame(r, c, na, frame, out) < 0) return 1;
    const uint32_t a0 = (uint32_t)c->a[0];
    EmArea08RouteHooks h;
    r08_hooks(r, &h);
    EmArea08RouteFault f = {0, 0};
    int32_t result = 0;
    int x;
    if (a07) {
        x = em_area08_route_a07_00823DA0(&h, a0, &f);
    } else {
        switch (fn) {
        case 0x00823580u: x = em_area08_route_00823580(&h, a0, &result, &f); break;
        case 0x008235E0u: x = em_area08_route_008235E0(&h, a0, &result, &f); break;
        case 0x00823870u: x = em_area08_route_00823870(&h, a0, &f); break;
        case 0x00823AC0u: x = em_area08_route_00823AC0(&h, a0, &f); break;
        case 0x00824210u: x = em_area08_route_00824210(&h, a0, &f); break;
        case 0x00824460u: x = em_area08_route_00824460(&h, &result, &f); break;
        case 0x008245E0u: x = em_area08_route_008245E0(&h, a0, &f); break;
        case 0x008249F0u: x = em_area08_route_008249F0(&h, a0, &f); break;
        default:          x = em_area08_route_00824BC0(&h, a0, c->sp, &f); break;
        }
    }
    if (x < 0) {
        fprintf(stderr, "em_area01: AREA%02u %08X fault code %d at %08X\n", r->area, (unsigned)fn, (int)f.code,
                (unsigned)f.address);
        *out = fail(r, f.address ? f.address : fn);
    } else {
        c->v0 = (uint64_t)(int64_t)result;
        *out = 0;
    }
    return 1;
}
