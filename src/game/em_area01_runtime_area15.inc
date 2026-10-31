/* AREA15's and AREA03's overlay owners in the shared composition (lane K1;
 * docs/AREA15_PORT.md): em_level14_port's and em_area15_route's AREA15
 * entries, em_area03_route's and em_area03_arrival's AREA03 entries, each
 * proven by its own original-instruction oracle (test_level14_port_reference,
 * test_area15_route_reference, test_area03_route_reference,
 * test_area03_arrival_reference), through the generated hooks. As
 * dispatch_level8: each entry's frame is read from its first instruction and
 * nested hooks get the entry's sp less that frame. Returns 0 when the address
 * is not one of the area's entries (a call of any other overlay address then
 * reaches the host's fail-stop). */
static int area_frame(EmArea01Runtime *r, EmArea01Call *c, unsigned na, uint32_t frame, int *out)
{
    if (c->na < na || c->nf || c->sp < frame + EIGHTH_NEST) {
        *out = fail(r, c->function);
        return -1;
    }
    r->sp = c->sp - frame - EIGHTH_NEST;
    return 0;
}

static int dispatch_a15(EmArea01Runtime *r, EmArea01Call *c, int *out)
{
    static const struct { uint32_t fn, frame; } owners[] = {
        {0x008235A0u, 0x20u}, {0x008236B0u, 0x20u}, {0x00823780u, 0x20u}, {0x00823850u, 0x20u},
        {0x008239F0u, 0x30u}, {0x00823E40u, 0x30u}, {0x00824070u, 0x20u}, {0x00824510u, 0x20u},
        {0x00824560u, 0x20u}, {0x008247B0u, 0x20u}, {0x00824990u, 0x20u}, {0x00824E00u, 0x20u},
        {0x00824E50u, 0x20u}, {0x00825030u, 0x20u}, {0x008252D0u, 0x10u}, {0x00825320u, 0x20u},
        {0x00825430u, 0x40u}, {0x00825D10u, 0x40u}, {0x00826600u, 0x30u}, {0x00826850u, 0x20u},
        /* em_area15_route's */
        {0x00823B40u, 0x20u}, {0x00823C80u, 0x20u}, {0x00824240u, 0x30u}, {0x00824350u, 0x20u},
        {0x008243E0u, 0x20u}, {0x00823F60u, 0x40u}};
    const uint32_t fn = c->function;
    unsigned k = 0;
    const unsigned n = sizeof owners / sizeof owners[0];
    while (k < n && owners[k].fn != fn) ++k;
    if (k == n) return 0;
    const unsigned route = k >= 20;
    if (area_frame(r, c, fn == 0x00823F60u ? 3 : 1, owners[k].frame, out) < 0) return 1;
    const uint32_t a0 = (uint32_t)c->a[0];
    int x;
    uint32_t where;
    int32_t code;
    if (!route) {
        EmLevel14PortHooks h;
        m14_hooks(r, &h);
        EmLevel14PortFault f = {0, 0};
        switch (fn) {
        case 0x008235A0u: x = em_level14_port_008235A0(&h, a0, &f); break;
        case 0x008236B0u: x = em_level14_port_008236B0(&h, a0, &f); break;
        case 0x00823780u: x = em_level14_port_00823780(&h, a0, &f); break;
        case 0x00823850u: x = em_level14_port_00823850(&h, a0, &f); break;
        case 0x008239F0u: x = em_level14_port_008239F0(&h, a0, &f); break;
        case 0x00823E40u: x = em_level14_port_00823E40(&h, a0, &f); break;
        case 0x00824070u: x = em_level14_port_00824070(&h, a0, &f); break;
        case 0x00824510u: x = em_level14_port_00824510(&h, a0, &f); break;
        case 0x00824560u: x = em_level14_port_00824560(&h, a0, &f); break;
        case 0x008247B0u: x = em_level14_port_008247B0(&h, a0, &f); break;
        case 0x00824990u: x = em_level14_port_00824990(&h, a0, &f); break;
        case 0x00824E00u: x = em_level14_port_00824E00(&h, a0, &f); break;
        case 0x00824E50u: x = em_level14_port_00824E50(&h, a0, &f); break;
        case 0x00825030u: x = em_level14_port_00825030(&h, a0, &f); break;
        case 0x008252D0u: x = em_level14_port_008252D0(&h, a0, &f); break;
        case 0x00825320u: x = em_level14_port_00825320(&h, a0, &f); break;
        case 0x00825430u: x = em_level14_port_00825430(&h, a0, &f); break;
        case 0x00825D10u: x = em_level14_port_00825D10(&h, a0, &f); break;
        case 0x00826600u: x = em_level14_port_00826600(&h, a0, &f); break;
        default:          x = em_level14_port_00826850(&h, a0, &f); break;
        }
        where = f.address;
        code = f.code;
    } else {
        EmArea15RouteHooks h;
        r15_hooks(r, &h);
        EmArea15RouteFault f = {0, 0};
        int32_t result = 0;
        switch (fn) {
        case 0x00823B40u: x = em_area15_route_00823B40(&h, a0, &f); break;
        case 0x00823C80u: x = em_area15_route_00823C80(&h, a0, &f); break;
        case 0x00824240u: x = em_area15_route_00824240(&h, a0, &f); break;
        case 0x00824350u: x = em_area15_route_00824350(&h, a0, &f); break;
        case 0x008243E0u: x = em_area15_route_008243E0(&h, a0, &f); break;
        default:
            x = em_area15_route_00823F60(&h, a0, (uint32_t)c->a[1], (uint32_t)c->a[2], &result, &f);
            if (x >= 0) c->v0 = (uint64_t)(int64_t)result;
            break;
        }
        where = f.address;
        code = f.code;
    }
    if (x < 0) {
        fprintf(stderr, "em_area01: AREA15 %08X fault code %d at %08X\n", (unsigned)fn, (int)code, (unsigned)where);
        *out = fail(r, where ? where : fn);
    } else {
        *out = 0;
    }
    return 1;
}

static int dispatch_a03(EmArea01Runtime *r, EmArea01Call *c, int *out)
{
    static const struct { uint32_t fn, frame; uint8_t na; } owners[] = {
        {0x00823930u, 0x50u, 1}, {0x00825290u, 0x20u, 1}, {0x00826270u, 0u, 0}, {0x00826330u, 0u, 3},
        {0x00826860u, 0u, 0}, {0x00826880u, 0x10u, 1}, {0x008268B0u, 0x10u, 1},
        /* em_area03_arrival's */
        {0x00824D30u, 0x80u, 2}, {0x00825430u, 0x20u, 1}, {0x008256E0u, 0x20u, 1}, {0x00825980u, 0x20u, 1},
        {0x00825DF0u, 0x20u, 1}, {0x00826340u, 0x20u, 1}, {0x008265D0u, 0x20u, 1}};
    const uint32_t fn = c->function;
    unsigned k = 0;
    const unsigned n = sizeof owners / sizeof owners[0];
    while (k < n && owners[k].fn != fn) ++k;
    if (k == n) return 0;
    if (area_frame(r, c, owners[k].na, owners[k].frame, out) < 0) return 1;
    const uint32_t a0 = (uint32_t)c->a[0], a1 = (uint32_t)c->a[1], a2 = (uint32_t)c->a[2];
    int32_t result = 0;
    int x;
    uint32_t where;
    int32_t code;
    if (k < 7) {
        EmArea03RouteHooks h;
        r03_hooks(r, &h);
        EmArea03RouteFault f = {0, 0};
        switch (fn) {
        case 0x00823930u: x = em_area03_route_00823930(&h, a0, &f); break;
        case 0x00825290u: x = em_area03_route_00825290(&h, a0, &f); break;
        case 0x00826270u: x = em_area03_route_00826270(&h, &result, &f); break;
        case 0x00826330u: x = em_area03_route_00826330(&h, a0, a1, a2, &result, &f); break;
        case 0x00826860u: x = em_area03_route_00826860(&h, &result, &f); break;
        case 0x00826880u: x = em_area03_route_00826880(&h, a0, &result, &f); break;
        default:          x = em_area03_route_008268B0(&h, a0, &result, &f); break;
        }
        where = f.address;
        code = f.code;
    } else {
        EmArea03ArrivalHooks h;
        a03_hooks(r, &h);
        EmArea03ArrivalFault f = {0, 0};
        switch (fn) {
        case 0x00824D30u: x = em_area03_arrival_00824D30(&h, a0, a1, c->sp, &result, &f); break;
        case 0x00825430u: x = em_area03_arrival_00825430(&h, a0, &f); break;
        case 0x008256E0u: x = em_area03_arrival_008256E0(&h, a0, &f); break;
        case 0x00825980u: x = em_area03_arrival_00825980(&h, a0, &f); break;
        case 0x00825DF0u: x = em_area03_arrival_00825DF0(&h, a0, &f); break;
        case 0x00826340u: x = em_area03_arrival_00826340(&h, a0, &f); break;
        default:          x = em_area03_arrival_008265D0(&h, a0, &f); break;
        }
        where = f.address;
        code = f.code;
    }
    if (x < 0) {
        fprintf(stderr, "em_area01: AREA03 %08X fault code %d at %08X\n", (unsigned)fn, (int)code, (unsigned)where);
        *out = fail(r, where ? where : fn);
    } else {
        c->v0 = (uint64_t)(int64_t)result;
        *out = 0;
    }
    return 1;
}
