/* The attribute-0x39 ledge (TWELFTH_LEVEL_ROUTE.md section 2, a19b_01):
 * 0017F9E0 (the floor service's surface-0x39 entry) and the state 0016EF50
 * (+5 0x1B), em_level12_port's translations (test_level12_port_reference)
 * over the record and this closure's workers. 001751A0 is the state's stick
 * classifier (byte-matched C). */
#define LEDGE_HIT 0x7F100000u           /* the record 0x700031D0 names */
static struct {
    const EmPlayerProbeHit *hit;
    uint8_t rec[0x40];
    uint8_t s38B0[16], s3A24[4];
    uint32_t word_31D0, root_word;
} LG;

static uint8_t *ledge_bytes(void *ctx, uint32_t a, uint32_t n)
{
    EmPlayerLiveActor *p = ctx;
    if (a >= 0x008102B0u && n <= EM_PLAYER_ACTOR_SIZE && a - 0x008102B0u <= EM_PLAYER_ACTOR_SIZE - n)
        return p->bytes + (a - 0x008102B0u);
    if (a == 0x70003A20u && n == 4) return (uint8_t *)&L.land.s3A20;
    if (a == 0x70003A24u && n == 4) return LG.s3A24;
    if (a >= 0x700038A0u && a + n <= 0x700038B0u) return (uint8_t *)L.land.s38A0 + (a - 0x700038A0u);
    if (a >= 0x700038B0u && a + n <= 0x700038C0u) return LG.s38B0 + (a - 0x700038B0u);
    if (a == 0x700031D0u && n == 4) return (uint8_t *)&LG.word_31D0;
    if (a >= LEDGE_HIT && a + n <= LEDGE_HIT + sizeof LG.rec) return LG.rec + (a - LEDGE_HIT);
    if (a == 0x00275B40u && n == 4) return (uint8_t *)&L.d275B40;
    /* the root node's clock word, **0x275B40 (the record's +0x110 names it) */
    uint32_t root = 0;
    memcpy(&root, p->bytes + 0x110, 4);
    if (a == root && n == 4 && w_root_node(NULL, 0, &LG.root_word) >= 0) return (uint8_t *)&LG.root_word;
    return (uint8_t *)(uintptr_t)em_effects_live_window(a, n);
}
static void *ledge_map(void *ctx, uint32_t a, size_t n, int write)
{
    (void)write;
    return n <= UINT32_MAX ? ledge_bytes(ctx, a, (uint32_t)n) : NULL;
}
static int ledge_sdk(void *ctx, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2)
{
    EmAimFireTargetCall f = {fn, 0, {a0, a1, a2}, {0}, 3, 0, 0, 0};
    return em_aim_fire_sdk_memory_call(ctx, ledge_map, &f) == 0 ? 0 : -1;
}
static int lg_001026A0(void *c, uint32_t a0, uint32_t a1, uint32_t a2) { return ledge_sdk(c, 0x001026A0u, a0, a1, a2); }
static int lg_sin(void *c, float x, float *r) { return w_sin_r(c, x, r); }
static int lg_cos(void *c, float x, float *r) { return w_cos_r(c, x, r); }
static int lg_atan2(void *c, float y, float x, float *r) { return w_atan2_r(c, y, x, r); }
static int lg_wrap(void *c, float x, float *r) { (void)c; *r = em_player_sdk_wrap(x); return 0; }
static int lg_001749A0(void *c, uint32_t a0, int32_t clip, int32_t flags, float blend)
{
    (void)a0;
    return w_request(NULL, c, clip, flags, blend);
}
static int lg_00174AB0(void *c, uint32_t a0)
{
    (void)a0;
    return w_request(NULL, c, 0, 1, 0.0f);
}
static int lg_00174A50(void *c, uint32_t a0, float blend)
{
    (void)a0;
    return w_row_request(NULL, c, blend);
}
/* 001751A0(self) (byte-matched C): the stick classifier. Scratch 0x70003A20
 * and 0x70003A24 take the turn and its magnitude. */
static int lg_001751A0(void *c, uint32_t a0)
{
    (void)a0;
    EmPlayerLiveActor *a = c;
    refresh_pad();
    if (P.spad3B8D != 0) return 0;
    em_live_set_u8(a, 0x23F, P.gait);
    if (P.gait == 0) {
        em_live_set_u32(a, 0x24C, UINT32_C(0xFFFFFFFF));
        return 0;
    }
    /* 3.14159265f * ((float)byte / 256.0f) */
    const uint32_t F_PI_BITS = 0x40490FDBu, F_256 = 0x43800000u;
    const uint32_t a65 = em_ee_mul_bits(F_PI_BITS, em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)P.ly), F_256));
    const uint32_t a64 = em_ee_mul_bits(F_PI_BITS, em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)P.lx), F_256));
    float cos64, cos65, r;
    if (w_cos_r(NULL, bfloat(a64), &cos64) < 0) return -1;
    em_live_set_f32(a, 0x244, cos64);
    if (w_cos_r(NULL, bfloat(a65), &cos65) < 0) return -1;
    em_live_set_f32(a, 0x248, cos65);
    if (w_atan2_r(NULL, bfloat(em_ee_neg_bits(fbits(cos65))), cos64, &r) < 0) return -1;
    uint32_t bits = em_ee_add_bits(em_ee_add_bits(F_PI_BITS, fbits(r)), P.d8106A0);
    bits = fbits(em_player_sdk_wrap(bfloat(bits)));
    bits = fbits(em_player_sdk_wrap(bfloat(em_ee_sub_bits(bits, em_live_u32(a, 0xC4)))));
    L.land.s3A20 = bfloat(bits);
    const uint32_t mag = fbits(em_sdk_math_original_0011DF78(bfloat(bits)));
    memcpy(LG.s3A24, &mag, 4);
    uint32_t kind;
    if (em_ee_c_le_bits(mag, 0x3F490FDBu)) kind = 0;
    else if (em_ee_c_le_bits(0x4016CBE4u, mag)) kind = 1;
    else kind = em_ee_c_lt_bits(bits, 0) ? 2 : 3;
    em_live_set_u32(a, 0x24C, kind);
    return 0;
}
static int lg_00175900(void *c, uint32_t a0, int32_t search)
{
    (void)a0;
    int ignored;
    return w_floor(NULL, c, search, &ignored);
}
static int lg_001764E0(void *c, uint32_t a0) { (void)a0; return w_probes(NULL, c); }
static int lg_001796C0(void *c, uint32_t a0) { (void)a0; return w_fall_check(NULL, c); }
static int lg_00188610(void *c, uint32_t a0, int32_t *clip) { (void)a0; return x_00188610(NULL, c, clip); }
static int lg_001C6DA0(void *c, uint32_t a0) { (void)a0; return w_eval_skeleton(NULL, c); }
static int lg_0019AD00(void *c, uint32_t a0, uint32_t a1, int32_t mask, int32_t *result)
{
    (void)a0;
    float target[4];
    const uint8_t *p = ledge_bytes(c, a1, 16);
    if (!p) return -1;
    memcpy(target, p, sizeof target);
    return w_move_mask(c, target, (uint32_t)mask, result, NULL);
}
static int lg_001C94B0(void *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t out[16], pos[3], rot[3], sc[3];
    const uint8_t *pp = ledge_bytes(c, a1, 12), *rp = ledge_bytes(c, a2, 12), *sp = ledge_bytes(c, a3, 12);
    uint8_t *to = ledge_bytes(c, a0, 64);
    if (!pp || !rp || !sp || !to) return -1;
    memcpy(pos, pp, 12);
    memcpy(rot, rp, 12);
    memcpy(sc, sp, 12);
    memcpy(out, to, 64);
    if (em_pose_host_build_trs_matrix(L.pose, out, pos, rot, sc) < 0) return -1;
    memcpy(to, out, 64);
    return 0;
}
static int lg_0019F680(void *c, uint32_t out, uint32_t rec, int32_t index)
{
    if (rec != LEDGE_HIT || !LG.hit || index < 0 || index >= LG.hit->ring) return -1;
    uint8_t *to = ledge_bytes(c, out, 12);
    if (!to) return -1;
    memcpy(to, LG.hit->corner[index], 12);
    return 0;
}
static void ledge_hooks(EmLevel12PortHooks *h, EmPlayerLiveActor *a)
{
    memset(h, 0, sizeof *h);
    h->ctx = a;
    h->bytes = ledge_bytes;
    h->w_001026A0 = lg_001026A0;
    h->w_0011DE90 = lg_cos;
    h->w_0011E2A8 = lg_sin;
    h->w_0011E620 = lg_atan2;
    h->w_001B1470 = lg_wrap;
    h->w_001749A0 = lg_001749A0;
    h->w_00174A50 = lg_00174A50;
    h->w_00174AB0 = lg_00174AB0;
    h->w_001751A0 = lg_001751A0;
    h->w_00175900 = lg_00175900;
    h->w_001764E0 = lg_001764E0;
    h->w_001796C0 = lg_001796C0;
    h->w_00188610 = lg_00188610;
    h->w_001C6DA0 = lg_001C6DA0;
    h->w_0019AD00 = lg_0019AD00;
    h->w_001C94B0 = lg_001C94B0;
    h->w_0019F680 = lg_0019F680;
}
static int ledge_report(const char *name, const EmLevel12PortFault *f)
{
    fprintf(stderr, "player closure: %s fault code %d at %08X\n", name, (int)f->code, (unsigned)f->address);
    return -1;
}
/* +5 0x1B: 0016EF50 (the slot's frame: the land's 0x70003A20). */
static int ledge_state(void *c, EmPlayerLiveActor *a)
{
    (void)c;
    EmLevel12PortHooks h;
    ledge_hooks(&h, a);
    EmLevel12PortFault f = {0, 0};
    return em_level12_port_0016EF50(&h, 0x008102B0u, &f) < 0 ? ledge_report("0016EF50", &f) : 0;
}
/* 00175CF0's surface-0x39 handlers: 0017F9E0 (0); 0017FB90 (1) has no
 * translation and faults here. The hit is the floor service's. */
int em_player_closure_live_surface39(EmPlayerLiveActor *actor, int handler, const EmPlayerProbeHit *hit)
{
    if (!L.bound || actor != L.actor || !hit) return unbound("0017F9E0 (surface 0x39)");
    if (handler != 0) return unbound("0017FB90 (surface 0x39, the second handler)");
    LG.hit = hit;
    LG.word_31D0 = LEDGE_HIT;
    memset(LG.rec, 0, sizeof LG.rec);
    memcpy(LG.rec + 0x34, hit->axis, sizeof hit->axis);
    EmLevel12PortHooks h;
    ledge_hooks(&h, actor);
    EmLevel12PortFault f = {0, 0};
    const int rc = em_level12_port_0017F9E0(&h, 0x008102B0u, &f) < 0 ? ledge_report("0017F9E0", &f) : 0;
    LG.hit = NULL;
    return rc;
}
