/* The first boss's family (lane F1, docs/LEVEL3_BINDING.md): the 0012E3A0
 * creature's state handlers and the probes they call, translated by the
 * later levels' modules (each proven by its own original-instruction
 * oracle: test_level10/11/12/13/14_port_reference, test_level15_creatures_
 * reference; 0012F980, 0012FA50, 00131210, 00131510 and 00131940 are lane
 * F1's, in em_level14_port_creature.c) and bound here in AREA04 through the
 * generated hooks. As
 * dispatch_level8: each entry's frame is read from its first instruction,
 * nested hooks get the entry's sp less that frame and EIGHTH_NEST. Returns
 * 0 when the address is not one of them. */
static int dispatch_boss(EmArea01Runtime *r, EmArea01Call *c, int *out)
{
    if (c->function == 0x001CA1C0u) {
        /* 001CA1C0(m, q, t): the death effect's rotation (em_pose_host's). */
        const uint8_t *q = c->na >= 3 ? bytes(r, (uint32_t)c->a[1], 16, 0) : NULL;
        const uint8_t *t = c->na >= 3 ? bytes(r, (uint32_t)c->a[2], 12, 0) : NULL;
        uint8_t *to = c->na >= 3 ? bytes(r, (uint32_t)c->a[0], 64, 1) : NULL;
        uint8_t *products = bytes(r, 0x70003760u, 0x2Cu, 1);
        if (!q || !t || !to || !products) {
            *out = fail(r, c->function);
            return 1;
        }
        uint32_t qw[4], tw[3], m[16], p[11];
        memcpy(qw, q, sizeof qw);
        memcpy(tw, t, sizeof tw);
        em_pose_host_001CA1C0(m, qw, tw, p);
        memcpy(to, m, sizeof m);
        memcpy(products, p, sizeof p);
        *out = 0;
        return 1;
    }
    if (c->function == 0x001F3E30u) {
        /* The death effect's pieces: 001F3E30 through the aim / fire
         * composition (em_area00_fx_debris), the barrel particles' owner.
         * The matrix 0x700036A0 is its view's. */
        if (c->na < 5) {
            *out = fail(r, c->function);
            return 1;
        }
        const uint8_t *from = bytes(r, (uint32_t)c->a[0], 64, 0);
        uint8_t *to = em_aim_fire_binding_bytes((uint32_t)c->a[0], 64, 1);
        if (!from || !to) {
            *out = fail(r, c->function);
            return 1;
        }
        if (from != to) memcpy(to, from, 64);
        const uint32_t a[5] = {(uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2], (uint32_t)c->a[3],
                               (uint32_t)c->a[4]};
        *out = em_aim_fire_runtime_particle(c->function, a) < 0 ? fail(r, c->function) : 0;
        return 1;
    }
    if (c->function == 0x001CA3B0u || c->function == 0x001CA4D0u) {
        /* The quaternion leaves of the death effect's 001F4190 (the node
         * 001F4840): em_area00_world, their one translation. */
        const int three = c->function == 0x001CA4D0u;
        if (c->na < (three ? 3u : 1u) || c->nf < (three ? 0u : 3u)) {
            *out = fail(r, c->function);
            return 1;
        }
        EmArea00WorldRegion regions[4];
        const unsigned n = three ? 4u : 1u;
        for (unsigned k = 0; k < n; ++k) {
            const uint32_t at = k == 3 ? c->sp - 0x10u : (uint32_t)c->a[k];   /* 001CA4D0's frame */
            regions[k] = (EmArea00WorldRegion){at, 16, bytes(r, at, 16, 1)};
            if (!regions[k].bytes) {
                *out = fail(r, at);
                return 1;
            }
        }
        EmArea00World w = {.regions = regions, .region_count = n, .call = rv_world_worker, .ctx = r, .sp = c->sp};
        const int rc = three ? em_area00_world_001CA4D0(&w, (uint32_t)c->a[0], (uint32_t)c->a[1], (uint32_t)c->a[2])
                             : em_area00_world_001CA3B0(&w, (uint32_t)c->a[0], c->f[0], c->f[1], c->f[2]);
        *out = rc < 0 ? fail(r, c->function) : 0;
        return 1;
    }
    static const struct { uint32_t fn, frame; unsigned na, nf; } entries[] = {
        {0x0012F6C0u, 0x30u, 2, 0}, {0x0012F980u, 0x30u, 2, 0}, {0x0012FA50u, 0x30u, 2, 0},
        {0x001305B0u, 0x30u, 2, 0}, {0x00131210u, 0x30u, 2, 0}, {0x00131510u, 0x30u, 2, 0},
        {0x00131650u, 0x30u, 2, 0}, {0x00131940u, 0x40u, 2, 0},
        {0x00131740u, 0x30u, 2, 0}, {0x00131B10u, 0x30u, 2, 0}, {0x00131E80u, 0x20u, 1, 0},
        {0x00131F20u, 0x40u, 3, 0}, {0x001339E0u, 0x10u, 2, 0}, {0x001B2F70u, 0x80u, 2, 0},
        {0x001B3250u, 0x40u, 2, 1}, {0x001B3390u, 0x50u, 3, 1}, {0x001B37D0u, 0x60u, 1, 2},
        {0x001B3F10u, 0x30u, 1, 2}, {0x001B4CF0u, 0x40u, 1, 0},
        {0x001F4840u, 0x30u, 1, 0}};
    unsigned k = 0, n = sizeof entries / sizeof entries[0];
    while (k < n && entries[k].fn != c->function) ++k;
    if (k == n) return 0;
    const uint32_t frame = entries[k].frame;
    if (c->na < entries[k].na || c->nf < entries[k].nf || c->sp < frame + EIGHTH_NEST) {
        *out = fail(r, c->function);
        return 1;
    }
    r->sp = c->sp - frame - EIGHTH_NEST;
    const uint32_t a0 = (uint32_t)c->a[0], a1 = (uint32_t)c->a[1], a2 = (uint32_t)c->a[2];
    float f12, f13;
    memcpy(&f12, &c->f[0], 4);
    memcpy(&f13, &c->f[1], 4);
    int32_t i = 0;
    float x = 0;
    int rc, has_i = 0, has_f = 0;
    uint32_t where = 0;
    switch (c->function) {
    case 0x0012F6C0u: {
        EmLevel15CreaturesHooks h;
        EmLevel15CreaturesFault f = {0, 0};
        m15_hooks(r, &h);
        rc = em_level15_creatures_0012F6C0(&h, a0, a1, &f);
        where = f.address;
        break;
    }
    case 0x001305B0u: {
        EmLevel10PortHooks h;
        EmLevel10PortFault f = {0, 0};
        m10_hooks(r, &h);
        rc = em_level10_port_001305B0(&h, a0, a1, &f);
        where = f.address;
        break;
    }
    case 0x001B2F70u:
    case 0x001B3390u: {
        EmLevel12PortHooks h;
        EmLevel12PortFault f = {0, 0};
        m12_hooks(r, &h);
        rc = c->function == 0x001B2F70u ? em_level12_port_001B2F70(&h, a0, a1, &i, &f)
                                        : em_level12_port_001B3390(&h, a0, a1, a2, f12, c->sp, &i, &f);
        has_i = 1;
        where = f.address;
        break;
    }
    case 0x001B3250u:
    case 0x001B37D0u: {
        EmLevel13PortHooks h;
        EmLevel13PortFault f = {0, 0};
        m13_hooks(r, &h);
        if (c->function == 0x001B3250u) {
            rc = em_level13_port_001B3250(&h, a0, a1, f12, c->sp, &i, &f);
            has_i = 1;
        } else {
            rc = em_level13_port_001B37D0(&h, a0, f12, f13, c->sp, &x, &f);
            has_f = 1;
        }
        where = f.address;
        break;
    }
    case 0x001B3F10u:
    case 0x001F4840u: {
        EmLevel11PortHooks h;
        EmLevel11PortFault f = {0, 0};
        m11_hooks(r, &h);
        if (c->function == 0x001F4840u) {
            rc = em_level11_port_001F4840(&h, a0, &f);
        } else {
            rc = em_level11_port_001B3F10(&h, a0, f12, f13, c->sp, &i, &f);
            has_i = 1;
        }
        where = f.address;
        break;
    }
    default: {
        EmLevel14PortHooks h;
        EmLevel14PortFault f = {0, 0};
        m14_hooks(r, &h);
        switch (c->function) {
        case 0x0012F980u: rc = em_level14_port_0012F980(&h, a0, a1, &f); break;
        case 0x0012FA50u: rc = em_level14_port_0012FA50(&h, a0, a1, &f); break;
        case 0x00131210u: rc = em_level14_port_00131210(&h, a0, a1, &f); break;
        case 0x00131510u: rc = em_level14_port_00131510(&h, a0, a1, &f); break;
        case 0x00131650u: rc = em_level14_port_00131650(&h, a0, a1, &f); break;
        case 0x00131940u: rc = em_level14_port_00131940(&h, a0, a1, &f); break;
        case 0x00131740u: rc = em_level14_port_00131740(&h, a0, a1, &f); break;
        case 0x00131B10u: rc = em_level14_port_00131B10(&h, a0, a1, &f); break;
        case 0x00131E80u: rc = em_level14_port_00131E80(&h, a0, &f); break;
        case 0x00131F20u: rc = em_level14_port_00131F20(&h, a0, a1, a2, &f); break;
        case 0x001B4CF0u: rc = em_level14_port_001B4CF0(&h, a0, &f); break;
        default:          rc = em_level14_port_001339E0(&h, a0, a1, &f); break;
        }
        where = f.address;
        break;
    }
    }
    if (rc < 0) {
        fprintf(stderr, "em_area01: the boss family's %08X faulted at %08X\n", (unsigned)c->function,
                (unsigned)where);
        *out = fail(r, where ? where : c->function);
        return 1;
    }
    if (has_i) c->v0 = (uint64_t)(int64_t)i;
    if (has_f) c->f0 = em_ee_bits(x);
    *out = 0;
    return 1;
}
