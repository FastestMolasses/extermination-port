/* AREA13's overlay owners in the shared runtime (area 0x0D; overlay id 10,
 * runtime addresses): em_area13_route's four (door [17], [3]'s scene, [4]
 * and [7]; test_area13_route_reference), em_level9_port's route owners
 * (test_level9_port_reference) and em_level11_port's later stages of [44],
 * [45]'s steps and the thrown piece (test_level11_port_reference). Each is
 * byte-identical overlay C compared with the original instructions; an
 * overlay address without a row here faults at the host's workers. The
 * entries that keep a frame take the caller's sp (the original's at entry;
 * [sp - 0x100, sp) is the composition's stack) and nested hooks run below
 * that frame, as dispatch_a06's. */
static int a13_done(EmArea01Runtime *r, EmArea01Call *c, int x, const char *port, uint32_t address, int code)
{
    if (x >= 0) return 0;
    fprintf(stderr, "em_area01: %s %08X fault code %d at %08X\n", port, (unsigned)c->function, code,
            (unsigned)address);
    return fail(r, address ? address : c->function);
}

static int dispatch_a13_route(EmArea01Runtime *r, EmArea01Call *c, int *rc)
{
    const uint32_t fn = c->function;
    if (fn != 0x00823580u && fn != 0x00823830u && fn != 0x00823A40u && fn != 0x00824A80u) return 0;
    if (c->na < 1 || c->nf) { *rc = fail(r, fn); return 1; }
    EmArea13RouteHooks h;
    a13_hooks(r, &h);
    EmArea13RouteFault f = {0, 0};
    const uint32_t a0 = (uint32_t)c->a[0];
    const int x = fn == 0x00823580u ? em_area13_route_00823580(&h, a0, &f)
                : fn == 0x00823830u ? em_area13_route_00823830(&h, a0, &f)
                : fn == 0x00823A40u ? em_area13_route_00823A40(&h, a0, &f)
                                    : em_area13_route_00824A80(&h, a0, &f);
    *rc = a13_done(r, c, x, "em_area13_route", f.address, f.code);
    return 1;
}

static int dispatch_a13_l9(EmArea01Runtime *r, EmArea01Call *c, int *rc)
{
    const uint32_t fn = c->function, a0 = (uint32_t)c->a[0], a1 = (uint32_t)c->a[1];
    unsigned need = 1;
    switch (fn) {
    case 0x00823700u: case 0x00823940u: case 0x00823BC0u: case 0x00823C10u: case 0x00823D50u:
    case 0x00823E90u: case 0x00824180u: case 0x00824BB0u: case 0x008266A0u: case 0x00826850u:
    case 0x00826FB0u: case 0x00826FC0u: case 0x00826FF0u: case 0x00827150u: case 0x008292A0u:
    case 0x008293A0u: case 0x008299E0u: case 0x00829AA0u: break;
    case 0x00823A10u: case 0x00823FE0u: need = 0; break;
    case 0x00826140u: need = 2; break;
    default: return 0;
    }
    if (c->na < need || c->nf || c->sp < 0x100u) { *rc = fail(r, fn); return 1; }
    EmLevel9PortHooks h;
    m09_hooks(r, &h);
    EmLevel9PortFault f = {0, 0};
    const uint32_t sp = c->sp, saved = r->sp;
    r->sp = sp - 0x100u;
    int32_t out = 0;
    int x;
    switch (fn) {
    case 0x00823700u: x = em_level9_port_00823700(&h, a0, &f); break;
    case 0x00823940u: x = em_level9_port_00823940(&h, a0, &f); break;
    case 0x00823A10u: x = em_level9_port_00823A10(&h, &out, &f); break;
    case 0x00823BC0u: x = em_level9_port_00823BC0(&h, a0, &f); break;
    case 0x00823C10u: x = em_level9_port_00823C10(&h, a0, &f); break;
    case 0x00823D50u: x = em_level9_port_00823D50(&h, a0, &f); break;
    case 0x00823E90u: x = em_level9_port_00823E90(&h, a0, &f); break;
    case 0x00823FE0u: x = em_level9_port_00823FE0(&h, &out, &f); break;
    case 0x00824180u: x = em_level9_port_00824180(&h, a0, &f); break;
    case 0x00824BB0u: x = em_level9_port_00824BB0(&h, a0, sp, &f); break;
    case 0x00826140u: x = em_level9_port_00826140(&h, a0, a1, sp, &out, &f); break;
    case 0x008266A0u: x = em_level9_port_008266A0(&h, a0, &f); break;
    case 0x00826850u: x = em_level9_port_00826850(&h, a0, &f); break;
    case 0x00826FB0u: x = em_level9_port_00826FB0(&h, a0, &out, &f); break;
    case 0x00826FC0u: x = em_level9_port_00826FC0(&h, a0, &out, &f); break;
    case 0x00826FF0u: x = em_level9_port_00826FF0(&h, a0, &f); break;
    case 0x00827150u: x = em_level9_port_00827150(&h, a0, &f); break;
    case 0x008292A0u: x = em_level9_port_008292A0(&h, a0, &f); break;
    case 0x008293A0u: x = em_level9_port_008293A0(&h, a0, &f); break;
    case 0x008299E0u: x = em_level9_port_008299E0(&h, a0, &f); break;
    default: x = em_level9_port_00829AA0(&h, a0, &f); break;
    }
    r->sp = saved;
    *rc = a13_done(r, c, x, "em_level9_port", f.address, f.code);
    if (*rc == 0) c->v0 = (uint64_t)(int64_t)out;
    return 1;
}

static int dispatch_a13_l11(EmArea01Runtime *r, EmArea01Call *c, int *rc)
{
    const uint32_t fn = c->function, a0 = (uint32_t)c->a[0], a1 = (uint32_t)c->a[1], a2 = (uint32_t)c->a[2];
    unsigned need = 1;
    switch (fn) {
    case 0x00824390u: case 0x00824520u: case 0x00824960u: case 0x008249F0u: case 0x00826610u:
    case 0x00827C30u: case 0x00827DD0u: case 0x00827E00u: case 0x00827F20u: case 0x00827F90u:
    case 0x00828500u: case 0x00828C60u: case 0x00828E10u: case 0x00828F40u: break;
    case 0x00824060u: case 0x008240E0u: case 0x00824160u: need = 0; break;
    case 0x008284E0u: need = 2; break;
    case 0x008246D0u: case 0x008247B0u: case 0x008248C0u: need = 3; break;
    default: return 0;
    }
    if (c->na < need || c->nf || c->sp < 0x100u) { *rc = fail(r, fn); return 1; }
    EmLevel11PortHooks h;
    m11_hooks(r, &h);
    EmLevel11PortFault f = {0, 0};
    const uint32_t sp = c->sp, saved = r->sp;
    r->sp = sp - 0x100u;
    int32_t out = 0;
    int x;
    switch (fn) {
    case 0x00824060u: x = em_level11_port_00824060(&h, &out, &f); break;
    case 0x008240E0u: x = em_level11_port_008240E0(&h, &out, &f); break;
    case 0x00824160u: x = em_level11_port_00824160(&h, &f); break;
    case 0x008247B0u: x = em_level11_port_008247B0(&h, a0, a1, a2, &out, &f); break;
    case 0x00824390u: x = em_level11_port_00824390(&h, a0, &f); break;
    case 0x00824520u: x = em_level11_port_00824520(&h, a0, &f); break;
    case 0x008246D0u: x = em_level11_port_008246D0(&h, a0, a1, a2, &out, &f); break;
    case 0x008248C0u: x = em_level11_port_008248C0(&h, a0, a1, a2, &out, &f); break;
    case 0x00824960u: x = em_level11_port_00824960(&h, a0, &f); break;
    case 0x008249F0u: x = em_level11_port_008249F0(&h, a0, &out, &f); break;
    case 0x00826610u: x = em_level11_port_00826610(&h, a0, sp, &f); break;
    case 0x00827C30u: x = em_level11_port_00827C30(&h, a0, sp, &f); break;
    case 0x00827DD0u: x = em_level11_port_00827DD0(&h, a0, &f); break;
    case 0x00827E00u: x = em_level11_port_00827E00(&h, a0, &f); break;
    case 0x00827F20u: x = em_level11_port_00827F20(&h, a0, &f); break;
    case 0x00827F90u: x = em_level11_port_00827F90(&h, a0, &f); break;
    case 0x008284E0u: x = em_level11_port_008284E0(&h, a0, a1, &f); break;
    case 0x00828500u: x = em_level11_port_00828500(&h, a0, sp, &f); break;
    case 0x00828C60u: x = em_level11_port_00828C60(&h, a0, sp, &f); break;
    case 0x00828E10u: x = em_level11_port_00828E10(&h, a0, &f); break;
    default: x = em_level11_port_00828F40(&h, a0, sp, &f); break;
    }
    r->sp = saved;
    *rc = a13_done(r, c, x, "em_level11_port", f.address, f.code);
    if (*rc == 0) c->v0 = (uint64_t)(int64_t)out;
    return 1;
}

/* AREA13's boot entries em_level9_port translates: 001BA7F0 (the scene's
 * body 0x61, 001BA580's branch), the lift door's wobble 001BDCA0 / 001BDD70,
 * the emitter node 001E7050 (the player's breath, effect 0x80000018). */
static int dispatch_a13_boot(EmArea01Runtime *r, EmArea01Call *c, int *rc)
{
    const uint32_t fn = c->function, a0 = (uint32_t)c->a[0];
    if (fn != 0x001BA7F0u && fn != 0x001BDCA0u && fn != 0x001BDD70u && fn != 0x001E7050u)
        return 0;
    if (c->na < 1 || c->nf || c->sp < 0x100u) { *rc = fail(r, fn); return 1; }
    EmLevel9PortHooks h;
    m09_hooks(r, &h);
    EmLevel9PortFault f = {0, 0};
    const uint32_t sp = c->sp, saved = r->sp;
    r->sp = sp - 0x100u;
    int32_t out = 0;
    const int x = fn == 0x001BA7F0u ? em_level9_port_001BA7F0(&h, a0, &f)
                : fn == 0x001BDCA0u ? em_level9_port_001BDCA0(&h, a0, &out, &f)
                : fn == 0x001BDD70u ? em_level9_port_001BDD70(&h, a0, &out, &f)
                                    : em_level9_port_001E7050(&h, a0, sp, &f);
    r->sp = saved;
    *rc = a13_done(r, c, x, "em_level9_port", f.address, f.code);
    if (*rc == 0) c->v0 = (uint64_t)(int64_t)out;
    return 1;
}

static int dispatch_a13(EmArea01Runtime *r, EmArea01Call *c, int *rc)
{
    /* 0x828F30: the empty method [the thrown piece's] +0x34 names. */
    if (c->function == 0x00828F30u) { *rc = 0; return 1; }
    return dispatch_a13_route(r, c, rc) || dispatch_a13_l9(r, c, rc) || dispatch_a13_l11(r, c, rc) ||
           dispatch_a13_boot(r, c, rc);
}
