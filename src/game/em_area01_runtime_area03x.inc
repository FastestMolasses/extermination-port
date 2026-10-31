/* The sixteenth level's addresses in the shared composition (lane N1;
 * docs/AREA03X_PORT.md): what the later dispatch (generated from the level
 * ports' headers) cannot reach. Returns 0 for any other address. */
static int dispatch_n1(EmArea01Runtime *r, EmArea01Call *c, int *out)
{
    (void)r;
    switch (c->function) {
    case 0x0011CB90u: /* the SDK's sqrtf kernel, 0011BCF8's and 0011BDD8's */
        if (c->nf < 1) { *out = fail(r, c->function); return 1; }
        c->f0 = em_ee_bits(em_sdk_math_original_0011CB90(em_ee_float(c->f[0])));
        *out = 0;
        return 1;
    case 0x0011E080u: /* the SDK's isnanf, 0011E420's */
        if (c->nf < 1) { *out = fail(r, c->function); return 1; }
        c->v0 = (uint64_t)(int64_t)em_sdk_math_original_0011E080(em_ee_float(c->f[0]));
        *out = 0;
        return 1;
    case 0x001C94B0u: { /* build_trs_matrix (em_owner_services_original) */
        if (c->na < 4) { *out = fail(r, c->function); return 1; }
        uint8_t *m = bytes(r, (uint32_t)c->a[0], 64, 1);
        const uint8_t *p = bytes(r, (uint32_t)c->a[1], 16, 0), *q = bytes(r, (uint32_t)c->a[2], 16, 0),
                      *t = bytes(r, (uint32_t)c->a[3], 16, 0);
        if (!m || !p || !q || !t) { *out = fail(r, c->function); return 1; }
        float world[16], pos[4], rot[4], scale[4];
        memcpy(pos, p, 16); memcpy(rot, q, 16); memcpy(scale, t, 16);
        *out = em_owner_services_build_trs_matrix(world, pos, rot, scale) < 0 ? fail(r, c->function) : 0;
        if (*out == 0) memcpy(m, world, 64);
        return 1;
    }
    default:
        return 0;
    }
}

/* AREA03's owners no earlier module has (em_level16_boot). Frames from each
 * entry's first instruction. */
static int dispatch_a03x(EmArea01Runtime *r, EmArea01Call *c, int *out)
{
    const uint32_t fn = c->function;
    uint32_t frame;
    switch (fn) {
    case 0x008235A0u: frame = 0x30u; break;
    case 0x00823810u: frame = 0x20u; break;
    case 0x00825200u: frame = 0x40u; break;
    default: return 0;
    }
    if (area_frame(r, c, 1, frame, out) < 0) return 1;
    EmLevel16BootHooks h;
    m16x_hooks(r, &h);
    EmLevel16BootFault f = {0, 0};
    const uint32_t a0 = (uint32_t)c->a[0];
    const int x = fn == 0x008235A0u ? em_level16_boot_a03_008235A0(&h, a0, &f)
                : fn == 0x00823810u ? em_level16_boot_a03_00823810(&h, a0, &f)
                                    : em_level16_boot_a03_00825200(&h, a0, c->sp, &f);
    if (x < 0) {
        fprintf(stderr, "em_area01: AREA03 %08X fault code %d at %08X\n", (unsigned)fn, (int)f.code, (unsigned)f.address);
        *out = fail(r, f.address ? f.address : fn);
    } else {
        *out = 0;
    }
    return 1;
}

/* AREA04 sub 1's owners no earlier module has (em_level16_boot). */
static int dispatch_a04x(EmArea01Runtime *r, EmArea01Call *c, int *out)
{
    const uint32_t fn = c->function;
    if (fn != 0x00824EF0u && fn != 0x00825310u) return 0;
    if (area_frame(r, c, 1, fn == 0x00824EF0u ? 0x20u : 0x30u, out) < 0) return 1;
    EmLevel16BootHooks h;
    m16x_hooks(r, &h);
    EmLevel16BootFault f = {0, 0};
    const uint32_t a0 = (uint32_t)c->a[0];
    const int x = fn == 0x00824EF0u ? em_level16_boot_a04_00824EF0(&h, a0, c->sp, &f)
                                    : em_level16_boot_a04_00825310(&h, a0, &f);
    if (x < 0) {
        fprintf(stderr, "em_area01: AREA04 %08X fault code %d at %08X\n", (unsigned)fn, (int)f.code, (unsigned)f.address);
        *out = fail(r, f.address ? f.address : fn);
    } else {
        *out = 0;
    }
    return 1;
}

