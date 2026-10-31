/* AREA19 (area byte 0x13, overlay id 0x10; lane J1, docs/AREA19_ASSETS.md,
 * docs/LEVEL3_BINDING.md): the overlay's behaviours and script callbacks,
 * translated by the later levels' modules (each proven by its own
 * original-instruction oracle: test_level10/12/13/14_port_reference,
 * test_area19_route_reference) and bound here through the generated hooks.
 * As dispatch_level8: each entry's frame is read from its first instruction
 * and nested hooks run EIGHTH_NEST below it. Any other overlay address goes
 * to the host's workers (an unbound one is the fail-stop), never to another
 * area's overlay at the same address. */
static int dispatch_area19(EmArea01Runtime *r, EmArea01Call *c)
{
    static const struct { uint32_t fn, frame; unsigned na; } entries[] = {
        {0x00823580u, 0x30u, 1}, {0x00823780u, 0xD0u, 1}, {0x00823D10u, 0xE0u, 1}, {0x00824690u, 0xC0u, 1},
        {0x00824A90u, 0xA0u, 1}, {0x00824BE0u, 0xB0u, 1}, {0x00824EF0u, 0x20u, 1}, {0x00824F70u, 0x10u, 1},
        {0x00825030u, 0x10u, 1}, {0x008250F0u, 0x20u, 1}, {0x00825240u, 0x70u, 1}, {0x00825420u, 0x70u, 1},
        {0x008255D0u, 0x20u, 1}, {0x008257C0u, 0x20u, 1}, {0x00825930u, 0x20u, 1}, {0x00825AB0u, 0x30u, 1},
        {0x00825C70u, 0x20u, 1}, {0x00825EE0u, 0x20u, 1}, {0x00826100u, 0x20u, 1}, {0x00826570u, 0x30u, 1},
        {0x00826840u, 0x20u, 1}, {0x00826B30u, 0x20u, 1}, {0x00826C10u, 0x30u, 1}, {0x00827430u, 0x20u, 1},
        {0x00827540u, 0x00u, 1}, {0x00827550u, 0x30u, 1}, {0x00827790u, 0x70u, 1}, {0x008279E0u, 0x20u, 1},
        {0x00827B10u, 0x00u, 0}, {0x00827B20u, 0x10u, 0}, {0x00827B60u, 0x30u, 1}, {0x00827DD0u, 0x60u, 1},
        {0x00829370u, 0x80u, 2}, {0x00829840u, 0x40u, 1}, {0x008298D0u, 0x20u, 1}, {0x00829A70u, 0x40u, 1}};
    unsigned k = 0, n = sizeof entries / sizeof entries[0];
    while (k < n && entries[k].fn != c->function) ++k;
    if (k == n) return r->host.worker(r->host.ctx, c);
    const uint32_t frame = entries[k].frame;
    if (c->na < entries[k].na || c->nf || c->sp < frame + EIGHTH_NEST) return fail(r, c->function);
    r->sp = c->sp - frame - EIGHTH_NEST;
    const uint32_t a0 = (uint32_t)c->a[0], a1 = (uint32_t)c->a[1], sp = c->sp;
    int32_t i = 0;
    int rc = -1, has_i = 0;
    uint32_t where = 0;
    int32_t code = 0;
    EmLevel10PortHooks h10;
    EmLevel12PortHooks h12;
    EmLevel13PortHooks h13;
    EmLevel14PortHooks h14;
    EmArea19RouteHooks h19;
    EmLevel10PortFault f10 = {0, 0};
    EmLevel12PortFault f12 = {0, 0};
    EmLevel13PortFault f13 = {0, 0};
    EmLevel14PortFault f14 = {0, 0};
    EmArea19RouteFault f19 = {0, 0};
    switch (c->function) {
    case 0x00823580u: case 0x008250F0u: case 0x00825240u: case 0x008255D0u: case 0x008257C0u:
    case 0x00825AB0u: case 0x00825C70u: case 0x00825EE0u: case 0x00826100u: case 0x00827790u:
    case 0x00829370u:
        m10_hooks(r, &h10);
        switch (c->function) {
        case 0x00823580u: rc = em_level10_port_00823580(&h10, a0, &f10); break;
        case 0x008250F0u: rc = em_level10_port_008250F0(&h10, a0, sp, &f10); break;
        case 0x00825240u: rc = em_level10_port_00825240(&h10, a0, sp, &f10); break;
        case 0x008255D0u: rc = em_level10_port_008255D0(&h10, a0, &f10); break;
        case 0x008257C0u: rc = em_level10_port_008257C0(&h10, a0, &f10); break;
        case 0x00825AB0u: rc = em_level10_port_00825AB0(&h10, a0, &f10); break;
        case 0x00825C70u: rc = em_level10_port_00825C70(&h10, a0, &f10); break;
        case 0x00825EE0u: rc = em_level10_port_00825EE0(&h10, a0, &f10); break;
        case 0x00826100u: rc = em_level10_port_00826100(&h10, a0, &f10); break;
        case 0x00827790u: rc = em_level10_port_00827790(&h10, a0, sp, &f10); break;
        default:          rc = em_level10_port_00829370(&h10, a0, a1, sp, &i, &f10); has_i = 1; break;
        }
        where = f10.address; code = f10.code;
        break;
    case 0x00824BE0u: case 0x00824EF0u: case 0x00824F70u: case 0x00825030u: case 0x00825420u:
    case 0x00825930u:
        m12_hooks(r, &h12);
        switch (c->function) {
        case 0x00824BE0u: rc = em_level12_port_00824BE0(&h12, a0, sp, &f12); break;
        case 0x00824EF0u: rc = em_level12_port_00824EF0(&h12, a0, &f12); break;
        case 0x00824F70u: rc = em_level12_port_00824F70(&h12, a0, &f12); break;
        case 0x00825030u: rc = em_level12_port_00825030(&h12, a0, &f12); break;
        case 0x00825420u: rc = em_level12_port_00825420(&h12, a0, sp, &f12); break;
        default:          rc = em_level12_port_00825930(&h12, a0, &f12); break;
        }
        where = f12.address; code = f12.code;
        break;
    case 0x00823780u: case 0x00823D10u: case 0x00824A90u: case 0x00826570u: case 0x00826840u:
    case 0x00826C10u: case 0x00827430u: case 0x00827550u: case 0x008279E0u: case 0x00827B10u:
    case 0x00827B20u: case 0x00827B60u: case 0x00829840u: case 0x00829A70u:
        m13_hooks(r, &h13);
        switch (c->function) {
        case 0x00823780u: rc = em_level13_port_00823780(&h13, a0, sp, &f13); break;
        case 0x00823D10u: rc = em_level13_port_00823D10(&h13, a0, sp, &f13); break;
        case 0x00824A90u: rc = em_level13_port_00824A90(&h13, a0, sp, &f13); break;
        case 0x00826570u: rc = em_level13_port_00826570(&h13, a0, &f13); break;
        case 0x00826840u: rc = em_level13_port_00826840(&h13, a0, &f13); break;
        case 0x00826C10u: rc = em_level13_port_00826C10(&h13, a0, &f13); break;
        case 0x00827430u: rc = em_level13_port_00827430(&h13, a0, &f13); break;
        case 0x00827550u: rc = em_level13_port_00827550(&h13, a0, &f13); break;
        case 0x008279E0u: rc = em_level13_port_008279E0(&h13, a0, &f13); break;
        case 0x00827B10u: rc = em_level13_port_00827B10(&h13, &i, &f13); has_i = 1; break;
        case 0x00827B20u: rc = em_level13_port_00827B20(&h13, &i, &f13); has_i = 1; break;
        case 0x00827B60u: rc = em_level13_port_00827B60(&h13, a0, &f13); break;
        case 0x00829840u: rc = em_level13_port_00829840(&h13, a0, sp, &f13); break;
        default:          rc = em_level13_port_00829A70(&h13, a0, &f13); break;
        }
        where = f13.address; code = f13.code;
        break;
    case 0x00826B30u: case 0x00827540u:
        m14_hooks(r, &h14);
        rc = c->function == 0x00826B30u ? em_level14_port_00826B30(&h14, a0, &i, &f14)
                                        : em_level14_port_00827540(&h14, a0, &i, &f14);
        has_i = 1;
        where = f14.address; code = f14.code;
        break;
    case 0x00824690u: case 0x00827DD0u:
        m19_hooks(r, &h19);
        rc = c->function == 0x00824690u ? em_area19_route_00824690(&h19, a0, sp, &f19)
                                        : em_area19_route_00827DD0(&h19, a0, &f19);
        where = f19.address; code = f19.code;
        break;
    default:
        /* 0x8298D0: AREA01's 0x828850, word for word
         * (test_level10_port_reference checks the two overlays). */
        rc = em_area01_ovl_00828850(&r->overlay, a0, &r->overlay_fault);
        where = r->overlay_fault.address; code = r->overlay_fault.code;
        break;
    }
    if (rc < 0) {
        fprintf(stderr, "em_area01: AREA19 %08X fault code %d at %08X\n", (unsigned)c->function, (int)code,
                (unsigned)where);
        return fail(r, where ? where : c->function);
    }
    if (has_i) c->v0 = (uint64_t)(int64_t)i;
    return 0;
}
