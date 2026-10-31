/* The attribute-0x39 ledge (TWELFTH_LEVEL_ROUTE.md section 2, a19b_01):
 * 0017F9E0 (the floor service's surface-0x39 entry) and the state 0016EF50
 * (+5 0x1B), em_level12_port's translations (test_level12_port_reference)
 * over the record and this closure's workers. 001751A0 is the state's stick
 * classifier (byte-matched C). */
#define LEDGE_HIT 0x7F100000u           /* the record 0x700031D0 names */
#define LEDGE_KIND 0x7F110000u          /* 0021BF90's hit record: only its +3 */
static struct {
    const EmPlayerProbeHit *hit;
    uint8_t rec[0x40];
    uint8_t s38B0[16], s3A24[8], s31B0[16], hit_kind;
    uint32_t word_31D0, word_30C, root_word, grid_word;
} LG;

static uint8_t *ledge_bytes(void *ctx, uint32_t a, uint32_t n)
{
    EmPlayerLiveActor *p = ctx;
    /* +0x30C keeps a grid node as 0x40000000 | node (the ladder entry's
     * record word): 0019F680 reads the node at its original address. */
    if (a == 0x008102B0u + 0x30Cu && n == 4) {
        uint32_t word = 0;
        memcpy(&word, p->bytes + 0x30C, 4);
        if ((word & 0xFF000000u) == 0x40000000u) {
            LG.word_30C = em_scene_bindings_grid_node_address(word & 0xFFFFFFu);
            return (uint8_t *)&LG.word_30C;
        }
    }
    if (a >= 0x008102B0u && n <= EM_PLAYER_ACTOR_SIZE && a - 0x008102B0u <= EM_PLAYER_ACTOR_SIZE - n)
        return p->bytes + (a - 0x008102B0u);
    if (a == 0x70003A20u && n == 4) return (uint8_t *)&L.land.s3A20;
    if (a >= 0x70003A24u && a + n <= 0x70003A2Cu) return LG.s3A24 + (a - 0x70003A24u);
    if (a >= 0x700038A0u && a + n <= 0x700038B0u) return (uint8_t *)L.land.s38A0 + (a - 0x700038A0u);
    if (a >= 0x700038B0u && a + n <= 0x700038C0u) return LG.s38B0 + (a - 0x700038B0u);
    if (a == 0x700031D0u && n == 4) return (uint8_t *)&LG.word_31D0;
    if (a == LEDGE_KIND + 3u && n == 1) return &LG.hit_kind;
    if (a >= 0x700031B0u && a + n <= 0x700031C0u) return LG.s31B0 + (a - 0x700031B0u);
    if (a == 0x70003B8Du && n == 1) return &scene()->spad3B8D;
    if (a >= LEDGE_HIT && a + n <= LEDGE_HIT + sizeof LG.rec) return LG.rec + (a - LEDGE_HIT);
    if (a == 0x00275B40u && n == 4) return (uint8_t *)&L.d275B40;
    /* the area bytes and D_008106F2, the world's (00169250 / 00194240) */
    if (a == 0x00810700u && n == 1) return &scene()->d810700;
    if (a == 0x00810701u && n == 1) return &scene()->d810701;
    if ((a == 0x008106F2u || a == 0x008106F1u || a == 0x008106BDu) && n == 1) return em_scene_req_at(scene(), a);
    /* 0x700031FC / 0x70003204: the collision file's vertex and index pools
     * (00199C50's staging of the D_0028A598 header), for 0019F680. */
    if ((a == 0x700031FCu || a == 0x70003204u) && n == 4) {
        EmModuleLoader *ml = em_module_loader_live();
        EmStatusSceneLoader *ld = ml ? em_module_loader_state(ml) : NULL;
        const uint32_t base = ld ? ld->d28A490[(0x0028A598u - 0x0028A490u) / 4u] : 0;
        const uint8_t *h = base ? em_module_loader_memory(ml, base, 12) : NULL;
        if (!h) return NULL;
        memcpy(&LG.grid_word, h + (a == 0x700031FCu ? 0u : 8u), 4);
        LG.grid_word += base;
        return (uint8_t *)&LG.grid_word;
    }
    /* the root node's clock word, **0x275B40 (the record's +0x110 names it) */
    uint32_t root = 0;
    memcpy(&root, p->bytes + 0x110, 4);
    if (a >= root && a < root + 12u && n == 4 && !((a - root) & 3u) && w_root_node(NULL, a - root, &LG.root_word) >= 0)
        return (uint8_t *)&LG.root_word;
    const uint8_t *q = em_effects_live_window(a, n);
    if (!q && em_module_loader_live()) q = em_module_loader_memory(em_module_loader_live(), a, n);
    return (uint8_t *)(uintptr_t)q;
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
static int lg_00175900(void *c, uint32_t a0, int32_t search, int32_t *result)
{
    (void)a0;
    int r = 0;
    if (w_floor(NULL, c, search, &r) < 0) return -1;
    *result = r;
    return 0;
}
/* The latch family's callees (their one translations over the record). */
static int lg_00102948(void *c, uint32_t a0, uint32_t a1) { return ledge_sdk(c, 0x00102948u, a0, a1, 0); }
static int lg_001028B8(void *c, uint32_t a0, uint32_t a1, uint32_t a2) { return ledge_sdk(c, 0x001028B8u, a0, a1, a2); }
static int lt_001FBD50(void *c, uint32_t a0, int32_t id, int32_t flags, float range)
{
    (void)a0;
    return w_sound_full(NULL, c, id, flags, range);
}
static int wa_0017C370(void *c, EmPlayerLiveActor *a);
static int lg_00174AC0(void *c, uint32_t a0, int32_t arg) { (void)a0; return w_heading(NULL, c, arg); }
static int lg_001749F0(void *c, uint32_t a0, int32_t clip, float blend, float frame)
{
    (void)a0;
    return w_arbiter(NULL, c, clip, blend, frame);
}
static int lg_001754E0(void *c, uint32_t a0, int32_t n, int32_t *result)
{
    (void)a0;
    int r = 0;
    if (em_player_reaction_001754E0(c, &L.reaction_scene, &L.land, n, &r) < 0) return -1;
    *result = r;
    return 0;
}
static int lg_00178B90(void *c, uint32_t a0, int32_t arg) { (void)a0; return w_translate(NULL, c, arg); }
static int lg_00179880(void *c, uint32_t a0, uint32_t a1) { (void)a0; (void)a1; return w_drop(NULL, c); }
static int lg_0017C370(void *c, uint32_t a0) { (void)a0; return wa_0017C370(NULL, c); }
static int lg_0017C540(void *c, uint32_t a0) { (void)a0; return w_handoff(NULL, c); }
static int lg_00182430(void *c, uint32_t a0, int32_t tier) { (void)a0; return x_surface_sound(NULL, c, tier); }
static int lg_001B61C0(void *c, int32_t a, int32_t b, int32_t d, int32_t e) { return w_rumble(NULL, a, b, d, e) + 0 * (c != NULL); }
static int lg_001C61D0(void *c, uint32_t bank, int32_t clip, int32_t *frames)
{
    (void)c;
    return w_clip_frames(NULL, bank, clip, frames);
}
static int lg_0021C120(void *c, uint32_t a0) { (void)a0; return w_0021C120(NULL, c); }
static int lg_0021C190(void *c, uint32_t a0, int32_t *r)
{
    (void)a0;
    int v = 0;
    if (w_0021C190(NULL, c, &v) < 0) return -1;
    *r = v;
    return 0;
}
static int lg_0021C200(void *c, uint32_t a0) { (void)a0; return w_0021C200(NULL, c); }
static int lg_0021C270(void *c, uint32_t a0) { (void)a0; return w_0021C270(NULL, c); }
static int lg_0021C350(void *c, uint32_t a0) { (void)a0; return w_0021C350(NULL, c); }
static int lg_0021D250(void *c, uint32_t a0, int32_t arg) { (void)a0; return w_surface5d(NULL, c, arg); }
static int lg_0021D2E0(void *c, uint32_t a0, int32_t frames, int32_t hold) { (void)a0; return w_teleport(NULL, c, frames, hold); }
static int lg_0021D490(void *c, uint32_t a0) { (void)a0; return w_0021D490(NULL, c); }
/* 0019AFE0(p, from, to, mask): the hit's point and node for 0x700031B0 / 0x700031D0. */
static int lg_0019AFE0(void *c, uint32_t a0, uint32_t a1, uint32_t a2, int32_t mask, int32_t *result)
{
    (void)a0;
    float from[4], to[4];
    const uint8_t *f = ledge_bytes(c, a1, 16), *t = ledge_bytes(c, a2, 16);
    if (!f || !t) return -1;
    memcpy(from, f, sizeof from);
    memcpy(to, t, sizeof to);
    EmPlayerProbeHit hit;
    memset(&hit, 0, sizeof hit);
    int r = 0;
    if (w_sweep_mask(c, from, to, (uint32_t)mask, &r, &hit) < 0) return -1;
    *result = r;
    if (r) {
        LG.word_31D0 = LEDGE_HIT;
        memcpy(LG.s31B0, hit.point, sizeof hit.point);
        memset(LG.rec, 0, sizeof LG.rec);
        memcpy(LG.rec + 0x24, hit.normal, sizeof hit.normal);
    }
    return 0;
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
    h->w_00102948 = lg_00102948;
    h->w_001028B8 = lg_001028B8;
    h->w_001FBD50 = lt_001FBD50;
    h->w_00174AC0 = lg_00174AC0;
    h->w_001749F0 = lg_001749F0;
    h->w_001754E0 = lg_001754E0;
    h->w_00178B90 = lg_00178B90;
    h->w_00179880 = lg_00179880;
    h->w_0017C370 = lg_0017C370;
    h->w_0017C540 = lg_0017C540;
    h->w_00182430 = lg_00182430;
    h->w_0019AFE0 = lg_0019AFE0;
    h->w_001B61C0 = lg_001B61C0;
    h->w_001C61D0 = lg_001C61D0;
    h->w_0021C120 = lg_0021C120;
    h->w_0021C190 = lg_0021C190;
    h->w_0021C200 = lg_0021C200;
    h->w_0021C270 = lg_0021C270;
    h->w_0021C350 = lg_0021C350;
    h->w_0021D250 = lg_0021D250;
    h->w_0021D2E0 = lg_0021D2E0;
    h->w_0021D490 = lg_0021D490;
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
/* +5 0x0F (00169250, em_area06_port's: the walk onto a bar) and 0x11
 * (0016AC50, em_level14_port's: the walk under a ceiling hang), over the same
 * record; 00194240 is em_level13_port's. */
static int lg_sqrt(void *c, float x, float *r) { return w_sqrt_r(c, x, r); }
static int lg_fabs(void *c, float x, float *r) { (void)c; *r = em_sdk_math_original_0011DF78(x); return 0; }
static int lg_001281C0(void *c, float x, int32_t *r)
{
    (void)c;
    *r = (int32_t)em_stream_lanes_001281C0(fbits(x));
    return 0;
}
static int lg_001B12B0(void *c, float target, float current, float rate, float *r)
{
    (void)c;
    uint32_t out = 0;
    if (em_script_host_001B12B0(NULL, fbits(target), fbits(current), fbits(rate), &out) < 0) return -1;
    *r = bfloat(out);
    return 0;
}
static int lg_001FBD50(void *c, uint32_t a0, int32_t id, int32_t flags, float range)
{
    (void)a0;
    return w_sound_full(NULL, c, id, flags, range);
}
static int lg14_001FBD50(void *c, uint32_t a0, int32_t id, int32_t flags, float range, int32_t *result)
{
    (void)a0;
    *result = 0;
    return w_sound_full(NULL, c, id, flags, range);
}
static int lg_00194240(void *c, uint32_t a0)
{
    EmLevel13PortHooks h;
    memset(&h, 0, sizeof h);
    h.ctx = c;
    h.bytes = ledge_bytes;
    h.w_0011DF78 = lg_fabs;
    EmLevel13PortFault f = {0, 0};
    int32_t r = 0;
    return em_level13_port_00194240(&h, a0, &r, &f) < 0 ? -1 : 0;
}
static int bar_state(void *c, EmPlayerLiveActor *a)
{
    (void)c;
    EmArea06PortHooks h;
    memset(&h, 0, sizeof h);
    h.ctx = a;
    h.bytes = ledge_bytes;
    h.w_0011E748 = lg_sqrt;
    h.w_001281C0 = lg_001281C0;
    h.w_001749A0 = lg_001749A0;
    h.w_00194240 = lg_00194240;
    h.w_001B12B0 = lg_001B12B0;
    h.w_001FBD50 = lg_001FBD50;
    EmArea06PortFault f = {0, 0};
    if (em_area06_port_00169250(&h, 0x008102B0u, &f) < 0) {
        fprintf(stderr, "player closure: 00169250 fault code %d at %08X\n", (int)f.code, (unsigned)f.address);
        return -1;
    }
    return 0;
}
static int lg_001885B0(void *c, uint32_t a0, int32_t *clip)
{
    (void)a0;
    return x_001885B0(NULL, c, clip);
}
static int ceiling_state(void *c, EmPlayerLiveActor *a)
{
    (void)c;
    EmLevel14PortHooks h;
    memset(&h, 0, sizeof h);
    h.ctx = a;
    h.bytes = ledge_bytes;
    h.w_001749A0 = lg_001749A0;
    h.w_001885B0 = lg_001885B0;
    h.w_001FBD50 = lg14_001FBD50;
    EmLevel14PortFault f = {0, 0};
    if (em_level14_port_0016AC50(&h, 0x008102B0u, &f) < 0) {
        fprintf(stderr, "player closure: 0016AC50 fault code %d at %08X\n", (int)f.code, (unsigned)f.address);
        return -1;
    }
    return 0;
}

/* 00175CF0's surface-0x39 handlers: 0017F9E0 (0); 0017FB90 (1) has no
 * translation and faults here. The hit is the floor service's. */
int em_player_closure_live_surface39(EmPlayerLiveActor *actor, int handler, const EmPlayerProbeHit *hit)
{
    if (!L.bound || actor != L.actor || !hit) return unbound("0017F9E0 (surface 0x39)");
    LG.hit = hit;
    LG.word_31D0 = LEDGE_HIT;
    memset(LG.rec, 0, sizeof LG.rec);
    memcpy(LG.rec + 0x34, hit->axis, sizeof hit->axis);
    EmLevel12PortHooks h;
    ledge_hooks(&h, actor);
    EmLevel12PortFault f = {0, 0};
    const int rc = handler == 0
        ? (em_level12_port_0017F9E0(&h, 0x008102B0u, &f) < 0 ? ledge_report("0017F9E0", &f) : 0)
        : (em_level12_port_0017FB90(&h, 0x008102B0u, &f) < 0 ? ledge_report("0017FB90", &f) : 0);
    LG.hit = NULL;
    return rc;
}

/* +4 2 / +5 0xD, 0xE (0015B770's switches on +0xD) and 0x15: the latch,
 * em_level12_port's translations. Kinds 2, 3 and 4 have none yet: their phase
 * slots stay empty, and the dispatch fail-stops. */
static int latch_run(EmPlayerLiveActor *a, int (*fn)(const EmLevel12PortHooks *, uint32_t, EmLevel12PortFault *),
                     const char *name)
{
    EmLevel12PortHooks h;
    ledge_hooks(&h, a);
    EmLevel12PortFault f = {0, 0};
    return fn(&h, 0x008102B0u, &f) < 0 ? ledge_report(name, &f) : 0;
}
static int latch_0(void *c, EmPlayerLiveActor *a) { (void)c; return latch_run(a, em_level12_port_0021FB40, "0021FB40"); }
static int latch_1(void *c, EmPlayerLiveActor *a) { (void)c; return latch_run(a, em_level12_port_002208C0, "002208C0"); }
static int throw_0(void *c, EmPlayerLiveActor *a) { (void)c; return latch_run(a, em_level12_port_0021FED0, "0021FED0"); }
static int throw_1(void *c, EmPlayerLiveActor *a) { (void)c; return latch_run(a, em_level12_port_00220B50, "00220B50"); }
static int knees_state(void *c, EmPlayerLiveActor *a)
{
    (void)c;
    return latch_run(a, em_level12_port_00224FE0, "00224FE0");
}
/* 0021BF90(the player, hit) from a creature: the hit's +3 byte reaches the
 * translation at a stand-in address. */
int em_player_closure_live_0021BF90(uint8_t kind)
{
    if (!L.bound || !L.actor) return unbound("0021BF90");
    EmLevel12PortHooks h;
    ledge_hooks(&h, L.actor);
    EmLevel12PortFault f = {0, 0};
    LG.hit_kind = kind;
    return em_level12_port_0021BF90(&h, 0x008102B0u, LEDGE_KIND, &f) < 0 ? ledge_report("0021BF90", &f) : 0;
}
