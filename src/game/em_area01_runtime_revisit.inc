/* The AREA01 revisit owners (em_area01_revisit.c, docs/AREA01_REVISIT.md)
 * and the AREA01 routines that first run on the AREA00 return
 * (em_area00_return.c: 0x823CD0, 0x826010), bound into the AREA01 runtime
 * the way the first visit's overlay owners are (em_area01_runtime_overlay.inc):
 * every hook re-enters em_area01_runtime_call by its original address, so a
 * callee the composition has no worker for faults there (fail-stop). No game
 * state is owned here.
 *
 * Memory: both modules take a view without a write flag; it is served as a
 * read view (as em_area01_gun_aux serves 0x8282F0). Stores through it land in
 * the same storage; the actor view's commit still refuses a store into a
 * pool link or a read-only projected span. */
static uint8_t *revisit_view(void *ctx, uint32_t address, uint32_t size)
{
    return bytes(ctx, address, size, 0);
}

static int revisit_callback(void *ctx, uint32_t function, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = function, .sp = r->sp, .a = {actor}, .na = 1};
    return em_area01_runtime_call(r, &c);
}

/* Original frame sizes (the first instruction of each function). Nested
 * worker calls get the entry's sp less its frame and, where the entry calls
 * other revisit functions directly, less the deepest such callee's frame too
 * (0x825950 -> 0x825BE0 0x70; 0x824340 -> 0x825040 0x30), so a callee's own
 * stack use stays below every local the translation writes. */
static int revisit_frame(EmArea01Runtime *r, const EmArea01Call *c, unsigned na, uint32_t frame)
{
    if (c->na < na || c->nf || c->sp < frame) return -1;
    r->sp = c->sp - frame;
    return 0;
}

/* --- Boot routines the revisit reaches whose translations are view-based
 * modules (em_area00_world): their memory is the runtime's bytes, their
 * callees re-enter the runtime by address. */
static int rv_world_worker(void *ctx, EmArea00WorldCall *w)
{
    EmArea01Call c = {.function = w->fn, .sp = w->sp, .na = w->na, .nf = w->nf};
    if (w->na > 8 || w->nf > 4) return -1;
    memcpy(c.a, w->a, sizeof w->a);
    memcpy(c.f, w->f, sizeof w->f);
    int rc = em_area01_runtime_call(ctx, &c);
    w->v0 = c.v0; w->f0 = c.f0;
    return rc;
}

/* --- 001BAC00 (script op14's placement; the AREA01 revisit's 0x82AD90
 * places group 0x82A900 at a01r_01 f1139): em_sdf_001BAC00, the one
 * translation (the AREA11 opening binds it too), over this runtime. Its
 * 001AFA90 / 001C8140 allocate through the runtime (001C8140:
 * em_area02_math); the bytes em_sdf hands back per node (+0x03,
 * +0x0D, +0xB0.., +0xC0.., +0x10, +0x20, +0x24, +0x2E; the 0x270E path's
 * +0x24 / +0x2E) are stored after the walk, then the owner's +0x2E. */
enum { RV_PLACE_MAX = 16 };
typedef struct {
    EmArea01Runtime *r;
    uint32_t sp, node[RV_PLACE_MAX], fault;
    int alt[RV_PLACE_MAX];
    EmSdfSpawned view[RV_PLACE_MAX];
    unsigned count;
} RvPlace;

static int rv_place_word(void *ctx, int32_t index, uint32_t *value)
{
    RvPlace *pl = ctx;
    const uint8_t *p = index >= 0 ? bytes(pl->r, 0x0028A490u + 4u * (uint32_t)index, 4, 0) : NULL;
    if (!p) return -1;
    memcpy(value, p, 4);
    return 0;
}

static int rv_place_slot(RvPlace *pl, uint32_t node, int alt, EmSdfSpawned **view)
{
    *view = NULL;
    if (!node) return 0;
    if (pl->count >= RV_PLACE_MAX) return -1;
    memset(&pl->view[pl->count], 0, sizeof pl->view[pl->count]);
    pl->node[pl->count] = node;
    pl->alt[pl->count] = alt;
    *view = &pl->view[pl->count++];
    return 0;
}

static int rv_place_afa90(void *ctx, uint8_t type, uint32_t *node, EmSdfSpawned **view)
{
    RvPlace *pl = ctx;
    EmArea01Call c = {.function = 0x001AFA90u, .sp = pl->sp, .a = {type}, .na = 1};
    if (em_area01_runtime_call(pl->r, &c) < 0) return -1;
    *node = (uint32_t)c.v0;
    return rv_place_slot(pl, *node, 0, view);
}


static int rv_place_c8140(void *ctx, uint32_t bank, int16_t index, uint32_t callback, uint32_t *node,
                          EmSdfSpawned **view)
{
    RvPlace *pl = ctx;
    uint64_t v0 = 0;
    if (spawn02(pl->r, pl->sp, 0x001C8140u, bank, (uint32_t)(int32_t)index, callback, &v0, &pl->fault) < 0)
        return -1;
    *node = (uint32_t)v0;
    return rv_place_slot(pl, *node, 1, view);
}

static int rv_place_store(EmArea01Runtime *r, uint32_t a, const void *v, uint32_t n)
{
    uint8_t *p = bytes(r, a, n, 1);
    if (!p) return -1;
    memcpy(p, v, n);
    return 0;
}

static int rv_001BAC00(EmArea01Runtime *r, EmArea01Call *c)
{
    if (c->na < 3 || c->nf || c->sp < 0x40u) return fail(r, c->function);
    const uint32_t owner = (uint32_t)c->a[0], record = (uint32_t)c->a[2];
    RvPlace pl;
    memset(&pl, 0, sizeof pl);
    pl.r = r;
    pl.sp = c->sp - 0x40u;
    const uint8_t *rec = bytes(r, record, 0x18, 0);
    const uint8_t *self14 = bytes(r, owner + 0x14u, 4, 0);
    /* The placement lists and their records lie in the overlay's data
     * (AREA01's 0x828A00..0x82CD00; AREA04's 0x826600..0x82C900, its
     * arrival script 0x8275A0's op14; AREA13's 0x829D00..0x82E280). */
    uint32_t lo = r->area == 4 ? 0x00826600u : 0x00828A00u, hi = r->area == 4 ? 0x0082C900u : 0x0082CD00u;
    if (r->area == 0x0D) { lo = 0x00829D00u; hi = 0x0082E280u; }
    if (r->area == 0x0F) { lo = 0x00826980u; hi = 0x00829F00u; }   /* AREA15's data */
    if (r->area == 3) { lo = 0x00826900u; hi = 0x00829100u; }      /* AREA03's */
    const uint8_t *data = bytes(r, lo, hi - lo, 0);
    if (!rec || !self14 || !data) return fail(r, c->function);
    uint8_t record_bytes[0x18];
    memcpy(record_bytes, rec, sizeof record_bytes);
    EmSdfSpawnOwner view_owner;
    memcpy(&view_owner.self_14, self14, 4);
    view_owner.s2E = 0;
    const EmSdfImage image = {data, lo, hi - lo};
    EmSdfWorkers w;
    memset(&w, 0, sizeof w);
    w.ctx = &pl;
    w.r_0028A490 = rv_place_word;
    w.w_001AFA90 = rv_place_afa90;
    w.w_001C8140 = rv_place_c8140;
    EmSdfFault f = {0, 0};
    const int result = em_sdf_001BAC00(&view_owner, record_bytes, &image, &w, &f);
    if (result < 0) return fail(r, pl.fault ? pl.fault : f.address ? f.address : c->function);
    for (unsigned k = 0; k < pl.count; ++k) {
        const uint32_t n = pl.node[k];
        const EmSdfSpawned *v = &pl.view[k];
        const uint16_t s2E = (uint16_t)v->s2E;
        int bad = 0;
        if (!pl.alt[k]) {
            bad |= rv_place_store(r, n + 0x03u, &v->b03, 1) | rv_place_store(r, n + 0x0Du, &v->b0D, 1);
            bad |= rv_place_store(r, n + 0xB0u, v->pos_B0, 12) | rv_place_store(r, n + 0xC0u, v->rot_C0, 12);
            bad |= rv_place_store(r, n + 0x10u, &v->handler_10, 4) | rv_place_store(r, n + 0x20u, &v->entry_20, 4);
        }
        bad |= rv_place_store(r, n + 0x24u, &v->owner_24, 4) | rv_place_store(r, n + 0x2Eu, &s2E, 2);
        if (bad) return fail(r, n);
    }
    const uint16_t zero = 0;
    if (rv_place_store(r, owner + 0x2Eu, &zero, 2) < 0) return fail(r, owner + 0x2Eu);
    c->v0 = (uint64_t)(int64_t)result;
    return 0;
}

/* --- 001BB0E0 (the default behaviour 001BAC00 gives the group 0x82A900's
 * five script actors) with its 001BAD40: em_slg_001BB0E0 and
 * em_sdf_001BAD40, the one translations (the AREA11 opening binds them over
 * its Roger pair), over this runtime. The node's bytes are a local image:
 * before each callee the bytes the translations changed go back to the
 * runtime, after it the image is read again, so every callee sees and
 * leaves the node as in the original. Frames: 001BB0E0 0x40, 001BAD40 0x60.
 * 001BAD40's message (0x270D / 0x270C) and camera (command 3) paths have no
 * world here: they fault if reached (the group's commands are 0, 5, 6, 2
 * and 2). */
enum { RV_NODE = 0x2F0 };
typedef struct {
    EmArea01Runtime *r;
    uint32_t node, sp, fault;
    int freed;
    uint8_t image[RV_NODE], base[RV_NODE];
} RvActor;

static int rv_actor_flush(RvActor *a)
{
    if (a->freed) return 0;
    for (uint32_t i = 0; i < RV_NODE;) {
        if (a->image[i] == a->base[i]) { ++i; continue; }
        uint32_t j = i;
        while (j < RV_NODE && a->image[j] != a->base[j]) ++j;
        uint8_t *p = bytes(a->r, a->node + i, j - i, 1);
        if (!p) { if (!a->fault) a->fault = a->node + i; return -1; }
        memcpy(p, a->image + i, j - i);
        i = j;
    }
    memcpy(a->base, a->image, RV_NODE);
    return 0;
}

static int rv_actor_load(RvActor *a)
{
    if (a->freed) return 0;
    const uint8_t *p = bytes(a->r, a->node, RV_NODE, 0);
    if (!p) { if (!a->fault) a->fault = a->node; return -1; }
    memcpy(a->image, p, RV_NODE);
    memcpy(a->base, p, RV_NODE);
    return 0;
}

/* One callee by address with the node as a0 (when `node`), then the
 * integer arguments, at most two floats; the node is synced around it. */
static int rv_actor_call(RvActor *a, uint32_t fn, uint32_t sp, int node, unsigned na, const uint64_t *args,
                         unsigned nf, const uint32_t *fs, uint64_t *v0)
{
    if (a->fault) return -1;
    EmArea01Call c = {.function = fn, .sp = sp};
    if (node) c.a[c.na++] = (uint64_t)(int64_t)(int32_t)a->node;
    for (unsigned k = 0; k < na; ++k) c.a[c.na++] = args[k];
    for (unsigned k = 0; k < nf; ++k) c.f[c.nf++] = fs[k];
    if (rv_actor_flush(a) < 0) return -1;
    if (em_area01_runtime_call(a->r, &c) < 0) { if (!a->fault) a->fault = fn; return -1; }
    if (fn == 0x001AFC10u) a->freed = 1;
    if (rv_actor_load(a) < 0) return -1;
    if (v0) *v0 = c.v0;
    return 0;
}

static uint64_t rv_reg(int32_t v) { return (uint64_t)(int64_t)v; }

/* em_sdf's view of the node <-> the image. */
static void rv_event_load(const RvActor *a, EmSdfEventActor *v)
{
    const uint8_t *n = a->image;
    v->lifecycle = n[0x04];
    v->b09 = n[0x09];
    v->b0C = n[0x0C];
    memcpy(&v->w18, n + 0x18, 4);
    memcpy(&v->bank_40, n + 0x40, 4);
    memcpy(&v->w44, n + 0x44, 4);
    memcpy(v->bones_110, n + 0x110, sizeof v->bones_110);
}

static void rv_event_store(RvActor *a, const EmSdfEventActor *v)
{
    uint8_t *n = a->image;
    n[0x04] = v->lifecycle;
    n[0x09] = v->b09;
    n[0x0C] = v->b0C;
    memcpy(n + 0x40, &v->bank_40, 4);
    memcpy(n + 0x44, &v->w44, 4);
    memcpy(n + 0x110, v->bones_110, sizeof v->bones_110);
}

#define RV_SDF_SP(a) ((a)->sp - 0x60u)
static int rv_sdf_obj(RvActor *a, EmSdfEventActor *obj, uint32_t fn, unsigned na, const uint64_t *args)
{
    rv_event_store(a, obj);
    int rc = rv_actor_call(a, fn, RV_SDF_SP(a), 1, na, args, 0, NULL, NULL);
    if (rc == 0 && !a->freed) rv_event_load(a, obj);
    return rc;
}

static int rv_sdf_value(RvActor *a, uint32_t fn, unsigned na, const uint64_t *args, uint64_t *v0)
{
    return rv_actor_call(a, fn, RV_SDF_SP(a), 0, na, args, 0, NULL, v0);
}

static int rv_sdf_r_0028A490(void *ctx, int32_t index, uint32_t *value)
{
    RvActor *a = ctx;
    const uint8_t *p = index >= 0 ? bytes(a->r, 0x0028A490u + 4u * (uint32_t)index, 4, 0) : NULL;
    if (!p) return -1;
    memcpy(value, p, 4);
    return 0;
}
static int rv_sdf_001CA6E0(void *ctx, EmSdfEventActor *obj, uint32_t bank)
{
    const uint64_t args[1] = {rv_reg((int32_t)bank)};
    return rv_sdf_obj(ctx, obj, 0x001CA6E0u, 1, args);
}
static int rv_sdf_001C5C90(void *ctx, EmSdfEventActor *obj) { return rv_sdf_obj(ctx, obj, 0x001C5C90u, 0, NULL); }
static int rv_sdf_001BA8E0(void *ctx, EmSdfEventActor *obj, int16_t type)
{
    const uint64_t args[1] = {rv_reg(type)};
    return rv_sdf_obj(ctx, obj, 0x001BA8E0u, 1, args);
}
static int rv_sdf_001D8BF0(void *ctx, EmSdfEventActor *obj, int32_t a1)
{
    const uint64_t args[1] = {rv_reg(a1)};
    return rv_sdf_obj(ctx, obj, 0x001D8BF0u, 1, args);
}
static int rv_sdf_001CA6F0(void *ctx, EmSdfEventActor *obj, uint8_t mode)
{
    const uint64_t args[1] = {mode};
    return rv_sdf_obj(ctx, obj, 0x001CA6F0u, 1, args);
}
static int rv_sdf_001C63E0(void *ctx, EmSdfEventActor *obj, int16_t clip)
{
    const uint64_t args[1] = {rv_reg(clip)};
    return rv_sdf_obj(ctx, obj, 0x001C63E0u, 1, args);
}
static int rv_sdf_001C6150(void *ctx, uint32_t model, int32_t *result)
{
    const uint64_t args[1] = {rv_reg((int32_t)model)};
    uint64_t v0 = 0;
    if (rv_sdf_value(ctx, 0x001C6150u, 1, args, &v0) < 0) return -1;
    *result = (int32_t)v0;
    return 0;
}
static int rv_sdf_001AF780(void *ctx, uint32_t *handle)
{
    uint64_t v0 = 0;
    if (rv_sdf_value(ctx, 0x001AF780u, 0, NULL, &v0) < 0) return -1;
    *handle = (uint32_t)v0;
    return 0;
}
static int rv_sdf_001CB5B0(void *ctx, uint8_t count)
{
    const uint64_t args[1] = {count};
    return rv_sdf_value(ctx, 0x001CB5B0u, 1, args, NULL);
}
static int rv_sdf_001C6120(void *ctx, uint32_t table, int32_t index, uint32_t *result)
{
    const uint64_t args[2] = {rv_reg((int32_t)table), rv_reg(index)};
    uint64_t v0 = 0;
    if (rv_sdf_value(ctx, 0x001C6120u, 2, args, &v0) < 0) return -1;
    *result = (uint32_t)v0;
    return 0;
}
static int rv_sdf_001C61D0(void *ctx, uint32_t bank, int16_t clip, int32_t *result)
{
    const uint64_t args[2] = {rv_reg((int32_t)bank), rv_reg(clip)};
    uint64_t v0 = 0;
    if (rv_sdf_value(ctx, 0x001C61D0u, 2, args, &v0) < 0) return -1;
    *result = (int32_t)v0;
    return 0;
}
static int rv_sdf_001C67E0(void *ctx, int16_t clip, float start, float length)
{
    RvActor *a = ctx;
    const uint64_t args[1] = {rv_reg(clip)};
    uint32_t fs[2];
    memcpy(&fs[0], &start, 4);
    memcpy(&fs[1], &length, 4);
    return rv_actor_call(a, 0x001C67E0u, RV_SDF_SP(a), 1, 1, args, 2, fs, NULL);
}

/* 001BAD40's command 3 (AREA04's event script 0x8278D0, a04_00): the
 * camera track's head word *(float *)track and 0022EC30(D_008101E0), the
 * timeline's bind, through the runtime (em_area01_timeline). */
static int rv_sdf_track_head(void *ctx, uint32_t address, float *value)
{
    RvActor *a = ctx;
    const uint8_t *p = bytes(a->r, address, 4, 0);
    if (!p) return -1;
    memcpy(value, p, 4);
    return 0;
}
static int rv_sdf_0022EC30(void *ctx, uint32_t camera)
{
    const uint64_t args[1] = {rv_reg((int32_t)camera)};
    return rv_sdf_value(ctx, 0x0022EC30u, 1, args, NULL);
}

static int rv_slg_001BAD40(void *ctx, const EmSlgScriptActor *s, int32_t *ret)
{
    RvActor *a = ctx;
    EmSdfEventActor v;
    rv_event_load(a, &v);
    const uint8_t *bcc = bytes(a->r, 0x00275BCCu, 2, 0);
    if (!bcc) return -1;
    int16_t budget;
    memcpy(&budget, bcc, 2);
    EmSdfWorld world;
    memset(&world, 0, sizeof world);
    world.d275BCC = &budget;
    /* The camera block's words command 3 writes (D_008101E0 +0x04, +0x6E,
     * +0x70..+0x78), each from its one storage. */
    world.d810250 = (uint32_t *)(void *)bytes(a->r, 0x00810250u, 4, 1);
    world.d810254 = (float *)(void *)bytes(a->r, 0x00810254u, 4, 1);
    world.d810258 = (float *)(void *)bytes(a->r, 0x00810258u, 4, 1);
    world.d8101E4 = bytes(a->r, 0x008101E4u, 1, 1);
    world.d81024E = (int16_t *)(void *)bytes(a->r, 0x0081024Eu, 2, 1);
    EmSdfWorkers w;
    memset(&w, 0, sizeof w);
    w.ctx = a;
    w.r_0028A490 = rv_sdf_r_0028A490;
    w.w_001CA6E0 = rv_sdf_001CA6E0;
    w.w_001C6120 = rv_sdf_001C6120;
    w.w_001C5C90 = rv_sdf_001C5C90;
    w.w_001C6150 = rv_sdf_001C6150;
    w.w_001AF780 = rv_sdf_001AF780;
    w.w_001BA8E0 = rv_sdf_001BA8E0;
    w.w_001D8BF0 = rv_sdf_001D8BF0;
    w.w_001CA6F0 = rv_sdf_001CA6F0;
    w.w_001CB5B0 = rv_sdf_001CB5B0;
    w.w_001C63E0 = rv_sdf_001C63E0;
    w.w_001C61D0 = rv_sdf_001C61D0;
    w.w_001C67E0 = rv_sdf_001C67E0;
    w.r_track_head = rv_sdf_track_head;
    w.w_0022EC30 = rv_sdf_0022EC30;
    EmSdfFault f = {0, 0};
    const int r = em_sdf_001BAD40(&v, s->entry, &world, &w, &f);
    if (r < 0) {
        if (!a->fault) a->fault = f.address ? f.address : 0x001BAD40u;
        return -1;
    }
    if (!a->freed) rv_event_store(a, &v);
    *ret = r;
    return 0;
}

static int rv_slg_node(void *ctx, uint32_t fn)
{
    RvActor *a = ctx;
    return rv_actor_call(a, fn, a->sp, 1, 0, NULL, 0, NULL, NULL);
}
static int rv_slg_001C5C90(void *ctx, const EmSlgScriptActor *s) { (void)s; return rv_slg_node(ctx, 0x001C5C90u); }
static int rv_slg_001C68C0(void *ctx, const EmSlgScriptActor *s) { (void)s; return rv_slg_node(ctx, 0x001C68C0u); }
static int rv_slg_001BA540(void *ctx, const EmSlgScriptActor *s) { (void)s; return rv_slg_node(ctx, 0x001BA540u); }
static int rv_slg_001AFC10(void *ctx, const EmSlgScriptActor *s) { (void)s; return rv_slg_node(ctx, 0x001AFC10u); }
static int rv_slg_method_4C(void *ctx, const EmSlgScriptActor *s)
{
    RvActor *a = ctx;
    uint32_t fn;
    memcpy(&fn, s->node + 0x4C, 4);
    return rv_slg_node(a, fn);
}
static int rv_slg_001BA580(void *ctx, const EmSlgScriptActor *s, int32_t key)
{
    RvActor *a = ctx;
    (void)s;
    const uint64_t args[1] = {rv_reg(key)};
    return rv_actor_call(a, 0x001BA580u, a->sp, 1, 1, args, 0, NULL, NULL);
}
static int rv_slg_001F9660(void *ctx, const EmSlgScriptActor *s, int32_t key)
{
    RvActor *a = ctx;
    (void)s;
    const uint64_t args[1] = {rv_reg(key)};
    return rv_actor_call(a, 0x001F9660u, a->sp, 1, 1, args, 0, NULL, NULL);
}
static int rv_slg_001C64F0(void *ctx, const EmSlgScriptActor *s, uint32_t dt_bits)
{
    RvActor *a = ctx;
    (void)s;
    return rv_actor_call(a, 0x001C64F0u, a->sp, 1, 0, NULL, 1, &dt_bits, NULL);
}

static int rv_001BB0E0(EmArea01Runtime *r, EmArea01Call *c)
{
    if (c->na < 1 || c->nf || c->sp < 0x100u) return fail(r, c->function);
    static RvActor a;   /* one call at a time: the runtime does not re-enter 001BB0E0 */
    memset(&a, 0, sizeof a);
    a.r = r;
    a.node = (uint32_t)c->a[0];
    a.sp = c->sp - 0x40u;
    if (rv_actor_load(&a) < 0) return fail(r, a.node);
    uint32_t entry_address, owner_address;
    memcpy(&entry_address, a.image + 0x20, 4);
    memcpy(&owner_address, a.image + 0x24, 4);
    const uint8_t *entry = bytes(r, entry_address, EM_SDF_ENTRY_SIZE, 0);
    if (!entry) return fail(r, entry_address);
    /* The owner's +0x2E (its done mask) is read in states 0 / 1 only: in
     * states 2 / 3 the owner may already be freed (the group 0x82A900's
     * owner at a01r_01 row 8103 frees itself in the tick its actors run
     * state 2, the fork pool probe), and the original reads nothing of it. */
    uint8_t entry_copy[EM_SDF_ENTRY_SIZE], owner_copy[0x30];
    memset(owner_copy, 0, sizeof owner_copy);
    if (a.image[4] <= 1u) {
        const uint8_t *owner_bytes = bytes(r, owner_address, 0x30, 0);
        if (!owner_bytes) return fail(r, owner_address);
        memcpy(owner_copy, owner_bytes, sizeof owner_copy);
    }
    memcpy(entry_copy, entry, sizeof entry_copy);
    const EmSlgScriptActorWorkers w = {&a,          rv_slg_001BAD40, rv_slg_001C5C90, rv_slg_001C68C0,
                                       rv_slg_001BA580, rv_slg_001C64F0, rv_slg_001F9660, rv_slg_001BA540,
                                       rv_slg_001AFC10, rv_slg_method_4C};
    const EmSlgScriptActor s = {a.image, entry_copy, owner_copy};
    if (em_slg_001BB0E0(&w, &s) < 0 || a.fault) return fail(r, a.fault ? a.fault : c->function);
    if (rv_actor_flush(&a) < 0) return fail(r, a.fault);
    return 0;
}

/* 001F9660(node, key), frame 0x310 (the node's 0x2F0-byte copy at sp -
 * 0x2F0 of its frame): em_area02_misc's translation over the node, the area
 * bytes D_00810700 / D_00810701 and its frame; block_copy 00121870 and
 * 001F91C0 re-enter the runtime. */
static int rv_misc_call(void *ctx, EmArea02MiscCall *in)
{
    EmArea01Call c = {.function = in->fn, .sp = in->sp, .na = in->na, .nf = in->nf};
    if (in->na > 8 || in->nf > 8) return -1;
    memcpy(c.a, in->a, sizeof c.a);
    memcpy(c.f, in->f, sizeof c.f);
    int rc = em_area01_runtime_call(ctx, &c);
    in->v0 = c.v0;
    in->f0 = c.f0;
    return rc;
}

static int rv_001F9660(EmArea01Runtime *r, EmArea01Call *c)
{
    if (c->na < 2 || c->nf || c->sp < 0x310u) return fail(r, c->function);
    const uint32_t node = (uint32_t)c->a[0];
    EmArea02MiscRegion regions[3] = {
        {node, RV_NODE, bytes(r, node, RV_NODE, 0)},
        {0x00810700u, 2, bytes(r, 0x00810700u, 2, 0)},
        {c->sp - 0x310u, 0x310u, bytes(r, c->sp - 0x310u, 0x310u, 1)}};
    for (unsigned k = 0; k < 3; ++k)
        if (!regions[k].bytes) return fail(r, regions[k].base);
    EmArea02Misc m;
    memset(&m, 0, sizeof m);
    m.regions = regions;
    m.region_count = 3;
    m.call = rv_misc_call;
    m.ctx = r;
    m.sp = c->sp;
    if (em_area02_misc_001F9660(&m, node, c->a[1]) < 0) return fail(r, m.fault_address ? m.fault_address : c->function);
    return 0;
}

/* 001F4010(index, frame), 0x8237D0's ring slot: em_area02_misc's
 * translation (the aim/fire world binds it for the shell casing) over
 * D_00275C40; its 001F2F90 / 001F3340 re-enter the runtime. */
static int rv_ring_001F4010(EmArea01Runtime *r, EmArea01Call *c)
{
    if (c->na < 2 || c->nf) return fail(r, c->function);
    EmArea02MiscRegion regions[1] = {{0x00275C40u, 4, bytes(r, 0x00275C40u, 4, 1)}};
    if (!regions[0].bytes) return fail(r, 0x00275C40u);
    EmArea02Misc m;
    memset(&m, 0, sizeof m);
    m.regions = regions;
    m.region_count = 1;
    m.call = rv_misc_call;
    m.ctx = r;
    m.sp = c->sp;
    if (em_area02_misc_001F4010(&m, (uint32_t)c->a[0], (uint32_t)c->a[1]) < 0)
        return fail(r, m.fault_address ? m.fault_address : c->function);
    return 0;
}

/* Returns 1 when `function` is one of these entries (*rc set), else 0. */
static int dispatch_revisit(EmArea01Runtime *r, EmArea01Call *c, int *rc)
{
    /* The boot workers below serve the later areas too (AREA15's sub 1 event
     * and creature); the overlay entries are AREA01's alone. */
    if (r->area != 1 && !((r->area == 0x0F || r->area == 3) && c->function < 0x00823500u)) return 0;
    const uint32_t a0 = (uint32_t)c->a[0], a1 = (uint32_t)c->a[1];
    int32_t result = 0;
    switch (c->function) {
    case 0x00825950u: /* behaviour of 0x828A00[5] / [7] */
        *rc = revisit_frame(r, c, 1, 0x20u + 0x70u) < 0 ? fail(r, c->function)
            : em_area01_revisit_00825950(&r->revisit, a0, c->sp, &r->revisit_fault);
        return 1;
    case 0x008237D0u: /* group 0x82A900 behaviours */
        *rc = revisit_frame(r, c, 1, 0x90u) < 0 ? fail(r, c->function)
            : em_area01_revisit_008237D0(&r->revisit, a0, c->sp, &r->revisit_fault);
        return 1;
    case 0x00823900u:
        *rc = revisit_frame(r, c, 1, 0x30u) < 0 ? fail(r, c->function)
            : em_area01_revisit_00823900(&r->revisit, a0, c->sp, &r->revisit_fault);
        return 1;
    case 0x008239C0u:
        *rc = revisit_frame(r, c, 1, 0x10u) < 0 ? fail(r, c->function)
            : em_area01_revisit_008239C0(&r->revisit, a0, &r->revisit_fault);
        return 1;
    case 0x00824340u: /* 0x823CD0's creature steps (self, self + 0x1F0) */
        *rc = revisit_frame(r, c, 2, 0x40u + 0x30u) < 0 ? fail(r, c->function)
            : em_area01_revisit_00824340(&r->revisit, a0, a1, &r->revisit_fault);
        return 1;
    case 0x00824770u:
        *rc = revisit_frame(r, c, 2, 0x60u) < 0 ? fail(r, c->function)
            : em_area01_revisit_00824770(&r->revisit, a0, a1, &r->revisit_fault);
        return 1;
    case 0x00824D50u:
        *rc = revisit_frame(r, c, 2, 0x50u) < 0 ? fail(r, c->function)
            : em_area01_revisit_00824D50(&r->revisit, a0, a1, &r->revisit_fault);
        return 1;
    case 0x00825910u: /* script op09 callback (record 0x82ACD0); reads a0 only */
        if (c->na < 1 || c->nf) { *rc = fail(r, c->function); return 1; }
        *rc = em_area01_revisit_00825910(&r->revisit, a0, c->na > 1 ? a1 : 0u,
                                         c->na > 2 ? (uint32_t)c->a[2] : 0u, &result, &r->revisit_fault);
        if (*rc == 0) c->v0 = (uint64_t)(int64_t)result;
        return 1;
    case 0x00823CD0u: /* behaviour of 0x828A00[0..4] */
        *rc = revisit_frame(r, c, 1, 0x30u) < 0 ? fail(r, c->function)
            : em_area00_return_00823CD0(&r->ret, a0, &r->ret_fault);
        return 1;
    case 0x00826010u: /* 0x825BE0 / 0x825D30's probe */
        *rc = revisit_frame(r, c, 1, 0x40u) < 0 ? fail(r, c->function)
            : em_area00_return_00826010(&r->ret, a0, c->sp, &r->ret_fault);
        return 1;
    case 0x001B0CD0u: { /* 0x826010's probe (byte-matched decomp C; em_area00_world) */
        if (c->na < 2 || c->nf) { *rc = fail(r, c->function); return 1; }
        EmArea00World w = {.call = rv_world_worker, .ctx = r, .sp = c->sp};
        *rc = em_area00_world_view_001B0CD0(&w, bytes, r, a0, a1);
        if (*rc < 0) *rc = fail(r, w.fault_address ? w.fault_address : c->function);
        return 1;
    }
    case 0x001BAC00u: /* script op14's placement (script 0x82AD90, group 0x82A900) */
        *rc = rv_001BAC00(r, c);
        return 1;
    case 0x001BB0E0u: /* the group's script actors (em_slg_001BB0E0 with em_sdf_001BAD40) */
        *rc = rv_001BB0E0(r, c);
        return 1;
    case 0x001F4010u: /* 0x8237D0's ring slot (em_area02_misc) */
        *rc = rv_ring_001F4010(r, c);
        return 1;
    case 0x001F9660u: /* 001BB0E0 command 6's sound copy (em_area02_misc) */
        *rc = rv_001F9660(r, c);
        return 1;
    case 0x001C8140u: /* a 0x270E placement entry's node (em_area02_math) */
    case 0x001C7EB0u: { /* that node's behaviour (em_area02_math) */
        if (c->na < (c->function == 0x001C8140u ? 3u : 1u) || c->nf) { *rc = fail(r, c->function); return 1; }
        uint32_t f = 0;
        *rc = spawn02(r, c->sp, c->function, a0, (uint32_t)c->a[1], (uint32_t)c->a[2], &c->v0, &f);
        if (*rc < 0) *rc = fail(r, f ? f : c->function);
        return 1;
    }
    default:
        return 0;
    }
}

/* Typed hooks (one per callee address, in the headers' order). */
static int rv_001026A0(void *ctx, uint32_t dst, uint32_t matrix, uint32_t vector)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001026A0u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)matrix;
    c.a[2] = (uint64_t)(int64_t)(int32_t)vector;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001026D0(void *ctx, uint32_t dst, uint32_t a, uint32_t b)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001026D0u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a;
    c.a[2] = (uint64_t)(int64_t)(int32_t)b;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001028B8(void *ctx, uint32_t dst, uint32_t a, uint32_t b)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001028B8u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a;
    c.a[2] = (uint64_t)(int64_t)(int32_t)b;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001028D0(void *ctx, uint32_t dst, uint32_t a, uint32_t b)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001028D0u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a;
    c.a[2] = (uint64_t)(int64_t)(int32_t)b;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_00102738(void *ctx, uint32_t a, uint32_t b, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102738u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a;
    c.a[1] = (uint64_t)(int64_t)(int32_t)b;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int rv_00102760(void *ctx, uint32_t dst, uint32_t src)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102760u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)src;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_00102948(void *ctx, uint32_t dst, uint32_t src)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102948u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)src;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_00102958(void *ctx, uint32_t dst, uint32_t src)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102958u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)src;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001029C0(void *ctx, uint32_t matrix)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001029C0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)matrix;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_00102A60(void *ctx, uint32_t dst, uint32_t src, float angle)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102A60u, .sp = r->sp, .na = 2, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)src;
    c.f[0] = em_ee_bits(angle);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_00102B08(void *ctx, uint32_t dst, uint32_t src, float angle)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102B08u, .sp = r->sp, .na = 2, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)src;
    c.f[0] = em_ee_bits(angle);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_00102BB0(void *ctx, uint32_t dst, uint32_t src, float angle)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102BB0u, .sp = r->sp, .na = 2, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)src;
    c.f[0] = em_ee_bits(angle);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001031E0(void *ctx, uint32_t dst, uint32_t src)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001031E0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)src;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_00103230(void *ctx, uint32_t dst, uint32_t src, float scale)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00103230u, .sp = r->sp, .na = 2, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)dst;
    c.a[1] = (uint64_t)(int64_t)(int32_t)src;
    c.f[0] = em_ee_bits(scale);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_0011E2A8(void *ctx, float x, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0011E2A8u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(x);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int rv_0011E748(void *ctx, float x, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0011E748u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(x);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int rv_00122BB8(void *ctx, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00122BB8u, .sp = r->sp, .na = 0, .nf = 0};
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rv_001287F0(void *ctx, uint32_t actor, uint32_t block, int32_t clip, float f12)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001287F0u, .sp = r->sp, .na = 3, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)block;
    c.a[2] = (uint64_t)(int64_t)clip;
    c.f[0] = em_ee_bits(f12);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_00128830(void *ctx, uint32_t actor, float f12, float f13, float f14)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00128830u, .sp = r->sp, .na = 1, .nf = 3};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.f[0] = em_ee_bits(f12);
    c.f[1] = em_ee_bits(f13);
    c.f[2] = em_ee_bits(f14);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_0012D580(void *ctx, uint32_t actor, uint32_t block, int32_t a2)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0012D580u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)block;
    c.a[2] = (uint64_t)(int64_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_0012DE90(void *ctx, uint32_t block)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0012DE90u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)block;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_0019A570(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0019A570u, .sp = r->sp, .na = 4, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)a2;
    c.a[3] = (uint64_t)(int64_t)a3;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rv_0019AA80(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0019AA80u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rv_0019C6F0(void *ctx, int32_t a0, int32_t a1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0019C6F0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rv_001AFA90(void *ctx, int32_t cls, uint32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001AFA90u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)cls;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (uint32_t)c.v0;
    return rc;
}
static int rv_001AFC10(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001AFC10u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001B10B0(void *ctx, uint32_t actor, int32_t a1, int32_t a2, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B10B0u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)a1;
    c.a[2] = (uint64_t)(int64_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rv_001B1240(void *ctx, uint32_t origin, float x, float z, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1240u, .sp = r->sp, .na = 1, .nf = 2};
    c.a[0] = (uint64_t)(int64_t)(int32_t)origin;
    c.f[0] = em_ee_bits(x);
    c.f[1] = em_ee_bits(z);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int rv_001B12B0(void *ctx, float goal, float current, float rate, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B12B0u, .sp = r->sp, .na = 0, .nf = 3};
    c.f[0] = em_ee_bits(goal);
    c.f[1] = em_ee_bits(current);
    c.f[2] = em_ee_bits(rate);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int rv_001B1470(void *ctx, float angle, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1470u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(angle);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int rv_001B17A0(void *ctx, uint32_t actor, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B17A0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rv_001B1EA0(void *ctx, int32_t a0, uint32_t point, uint32_t quad, int32_t a3, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1EA0u, .sp = r->sp, .na = 4, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)point;
    c.a[2] = (uint64_t)(int64_t)(int32_t)quad;
    c.a[3] = (uint64_t)(int64_t)a3;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rv_001B6660(void *ctx, uint32_t record, uint32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B6660u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)record;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (uint32_t)c.v0;
    return rc;
}
static int rv_001BA1A0(void *ctx, uint32_t block, uint32_t script)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA1A0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)block;
    c.a[1] = (uint64_t)(int64_t)(int32_t)script;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001BA1F0(void *ctx, uint32_t actor, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA1F0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rv_001C2770(void *ctx, uint32_t actor, uint32_t block, int32_t a2, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C2770u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)block;
    c.a[2] = (uint64_t)(int64_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rv_001C3D60(void *ctx, uint32_t actor, uint32_t block)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C3D60u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)(int32_t)block;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001C4760(void *ctx, int32_t a0, int32_t a1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C4760u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rv_001C47A0(void *ctx, int32_t a0, int32_t a1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C47A0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rv_001C63E0(void *ctx, uint32_t actor, int32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C63E0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001C64F0(void *ctx, uint32_t actor, float dt, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C64F0u, .sp = r->sp, .na = 1, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.f[0] = em_ee_bits(dt);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rv_001C68C0(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C68C0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001C69A0(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C69A0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001CA6F0(void *ctx, uint32_t actor, int32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001CA6F0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001CD520(void *ctx, int32_t bucket, int32_t mode, uint32_t point, uint64_t giftag, uint32_t rgba, float w, float h, float zbias, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001CD520u, .sp = r->sp, .na = 5, .nf = 3};
    c.a[0] = (uint64_t)(int64_t)bucket;
    c.a[1] = (uint64_t)(int64_t)mode;
    c.a[2] = (uint64_t)(int64_t)(int32_t)point;
    c.a[3] = giftag;
    c.a[4] = (uint64_t)(int64_t)(int32_t)rgba;
    c.f[0] = em_ee_bits(w);
    c.f[1] = em_ee_bits(h);
    c.f[2] = em_ee_bits(zbias);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rv_001E2BA0(void *ctx, uint32_t start, uint32_t end, uint32_t colour, float length)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001E2BA0u, .sp = r->sp, .na = 3, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)start;
    c.a[1] = (uint64_t)(int64_t)(int32_t)end;
    c.a[2] = (uint64_t)(int64_t)(int32_t)colour;
    c.f[0] = em_ee_bits(length);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001EFD20(void *ctx, int32_t id, uint32_t point)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001EFD20u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)id;
    c.a[1] = (uint64_t)(int64_t)(int32_t)point;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001EFD90(void *ctx, int32_t id, uint32_t a, uint32_t b)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001EFD90u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)id;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a;
    c.a[2] = (uint64_t)(int64_t)(int32_t)b;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001F4010(void *ctx, int32_t a0, uint32_t matrix)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001F4010u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)matrix;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001F4E20(void *ctx, uint32_t point, uint32_t colour, float f12)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001F4E20u, .sp = r->sp, .na = 2, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)point;
    c.a[1] = (uint64_t)(int64_t)(int32_t)colour;
    c.f[0] = em_ee_bits(f12);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001FAE70(void *ctx, int32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001FAE70u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_001FB9F0(void *ctx, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001FB9F0u, .sp = r->sp, .na = 4, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)id;
    c.a[1] = (uint64_t)(int64_t)a1;
    c.a[2] = (uint64_t)(int64_t)a2;
    c.a[3] = (uint64_t)(int64_t)a3;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rv_00826010(void *ctx, uint32_t actor)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00826010u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)actor;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001026A0(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001026A0u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)(int32_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_00102718(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102718u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)(int32_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001027E0(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001027E0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001028D0(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001028D0u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)(int32_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_00102958(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00102958u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001029C0(void *ctx, uint32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001029C0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001031E0(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001031E0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_00103230(void *ctx, uint32_t a0, uint32_t a1, float a2)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00103230u, .sp = r->sp, .na = 2, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.f[0] = em_ee_bits(a2);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_00122BB8(void *ctx, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00122BB8u, .sp = r->sp, .na = 0, .nf = 0};
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rt_001287F0(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, float a3)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001287F0u, .sp = r->sp, .na = 3, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)a2;
    c.f[0] = em_ee_bits(a3);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001288D0(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001288D0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_00128AB0(void *ctx, uint32_t a0, uint32_t a1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00128AB0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rt_00129780(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00129780u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rt_0019A570(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x0019A570u, .sp = r->sp, .na = 4, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)a2;
    c.a[3] = (uint64_t)(int64_t)a3;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rt_001AFC10(void *ctx, uint32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001AFC10u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001B0CD0(void *ctx, uint32_t a0, int32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B0CD0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001B1240(void *ctx, uint32_t a0, float a1, float a2, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1240u, .sp = r->sp, .na = 1, .nf = 2};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.f[0] = em_ee_bits(a1);
    c.f[1] = em_ee_bits(a2);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int rt_001B12B0(void *ctx, float a0, float a1, float a2, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B12B0u, .sp = r->sp, .na = 0, .nf = 3};
    c.f[0] = em_ee_bits(a0);
    c.f[1] = em_ee_bits(a1);
    c.f[2] = em_ee_bits(a2);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int rt_001B1470(void *ctx, float a0, float *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B1470u, .sp = r->sp, .na = 0, .nf = 1};
    c.f[0] = em_ee_bits(a0);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = em_ee_float(c.f0);
    return rc;
}
static int rt_001B17A0(void *ctx, uint32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001B17A0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001BA1C0(void *ctx, uint32_t a0, int32_t a1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001BA1C0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rt_001C2770(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C2770u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rt_001C3D60(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C3D60u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001C63E0(void *ctx, uint32_t a0, int32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C63E0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001C64F0(void *ctx, uint32_t a0, float a1, int32_t *result)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C64F0u, .sp = r->sp, .na = 1, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.f[0] = em_ee_bits(a1);
    int rc = em_area01_runtime_call(r, &c);
    if (rc == 0 && result) *result = (int32_t)c.v0;
    return rc;
}
static int rt_001C6960(void *ctx, uint32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C6960u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001C69A0(void *ctx, uint32_t a0)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001C69A0u, .sp = r->sp, .na = 1, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001CA6F0(void *ctx, uint32_t a0, int32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001CA6F0u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001EFD90(void *ctx, int32_t a0, uint32_t a1, uint32_t a2)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001EFD90u, .sp = r->sp, .na = 3, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    c.a[2] = (uint64_t)(int64_t)(int32_t)a2;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001FB9F0(void *ctx, int32_t a0, int32_t a1, int32_t a2, int32_t a3)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001FB9F0u, .sp = r->sp, .na = 4, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    c.a[2] = (uint64_t)(int64_t)a2;
    c.a[3] = (uint64_t)(int64_t)a3;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_001FBD50(void *ctx, uint32_t a0, int32_t a1, int32_t a2, float a3)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x001FBD50u, .sp = r->sp, .na = 3, .nf = 1};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)a1;
    c.a[2] = (uint64_t)(int64_t)a2;
    c.f[0] = em_ee_bits(a3);
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_00824340(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00824340u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_00824770(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00824770u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}
static int rt_00824D50(void *ctx, uint32_t a0, uint32_t a1)
{
    EmArea01Runtime *r = ctx;
    EmArea01Call c = {.function = 0x00824D50u, .sp = r->sp, .na = 2, .nf = 0};
    c.a[0] = (uint64_t)(int64_t)(int32_t)a0;
    c.a[1] = (uint64_t)(int64_t)(int32_t)a1;
    int rc = em_area01_runtime_call(r, &c);
    return rc;
}

static void revisit_workers(EmArea01Runtime *r)
{
    r->revisit.ctx = r; r->revisit.bytes = revisit_view; r->revisit.w_callback = revisit_callback;
    r->revisit.w_001026A0 = rv_001026A0;
    r->revisit.w_001026D0 = rv_001026D0;
    r->revisit.w_001028B8 = rv_001028B8;
    r->revisit.w_001028D0 = rv_001028D0;
    r->revisit.w_00102738 = rv_00102738;
    r->revisit.w_00102760 = rv_00102760;
    r->revisit.w_00102948 = rv_00102948;
    r->revisit.w_00102958 = rv_00102958;
    r->revisit.w_001029C0 = rv_001029C0;
    r->revisit.w_00102A60 = rv_00102A60;
    r->revisit.w_00102B08 = rv_00102B08;
    r->revisit.w_00102BB0 = rv_00102BB0;
    r->revisit.w_001031E0 = rv_001031E0;
    r->revisit.w_00103230 = rv_00103230;
    r->revisit.w_0011E2A8 = rv_0011E2A8;
    r->revisit.w_0011E748 = rv_0011E748;
    r->revisit.w_00122BB8 = rv_00122BB8;
    r->revisit.w_001287F0 = rv_001287F0;
    r->revisit.w_00128830 = rv_00128830;
    r->revisit.w_0012D580 = rv_0012D580;
    r->revisit.w_0012DE90 = rv_0012DE90;
    r->revisit.w_0019A570 = rv_0019A570;
    r->revisit.w_0019AA80 = rv_0019AA80;
    r->revisit.w_0019C6F0 = rv_0019C6F0;
    r->revisit.w_001AFA90 = rv_001AFA90;
    r->revisit.w_001AFC10 = rv_001AFC10;
    r->revisit.w_001B10B0 = rv_001B10B0;
    r->revisit.w_001B1240 = rv_001B1240;
    r->revisit.w_001B12B0 = rv_001B12B0;
    r->revisit.w_001B1470 = rv_001B1470;
    r->revisit.w_001B17A0 = rv_001B17A0;
    r->revisit.w_001B1EA0 = rv_001B1EA0;
    r->revisit.w_001B6660 = rv_001B6660;
    r->revisit.w_001BA1A0 = rv_001BA1A0;
    r->revisit.w_001BA1F0 = rv_001BA1F0;
    r->revisit.w_001C2770 = rv_001C2770;
    r->revisit.w_001C3D60 = rv_001C3D60;
    r->revisit.w_001C4760 = rv_001C4760;
    r->revisit.w_001C47A0 = rv_001C47A0;
    r->revisit.w_001C63E0 = rv_001C63E0;
    r->revisit.w_001C64F0 = rv_001C64F0;
    r->revisit.w_001C68C0 = rv_001C68C0;
    r->revisit.w_001C69A0 = rv_001C69A0;
    r->revisit.w_001CA6F0 = rv_001CA6F0;
    r->revisit.w_001CD520 = rv_001CD520;
    r->revisit.w_001E2BA0 = rv_001E2BA0;
    r->revisit.w_001EFD20 = rv_001EFD20;
    r->revisit.w_001EFD90 = rv_001EFD90;
    r->revisit.w_001F4010 = rv_001F4010;
    r->revisit.w_001F4E20 = rv_001F4E20;
    r->revisit.w_001FAE70 = rv_001FAE70;
    r->revisit.w_001FB9F0 = rv_001FB9F0;
    r->revisit.w_00826010 = rv_00826010;
    r->ret.ctx = r; r->ret.bytes = revisit_view; r->ret.w_callback = revisit_callback;
    r->ret.w_001026A0 = rt_001026A0;
    r->ret.w_00102718 = rt_00102718;
    r->ret.w_001027E0 = rt_001027E0;
    r->ret.w_001028D0 = rt_001028D0;
    r->ret.w_00102958 = rt_00102958;
    r->ret.w_001029C0 = rt_001029C0;
    r->ret.w_001031E0 = rt_001031E0;
    r->ret.w_00103230 = rt_00103230;
    r->ret.w_00122BB8 = rt_00122BB8;
    r->ret.w_001287F0 = rt_001287F0;
    r->ret.w_001288D0 = rt_001288D0;
    r->ret.w_00128AB0 = rt_00128AB0;
    r->ret.w_00129780 = rt_00129780;
    r->ret.w_0019A570 = rt_0019A570;
    r->ret.w_001AFC10 = rt_001AFC10;
    r->ret.w_001B0CD0 = rt_001B0CD0;
    r->ret.w_001B1240 = rt_001B1240;
    r->ret.w_001B12B0 = rt_001B12B0;
    r->ret.w_001B1470 = rt_001B1470;
    r->ret.w_001B17A0 = rt_001B17A0;
    r->ret.w_001BA1C0 = rt_001BA1C0;
    r->ret.w_001C2770 = rt_001C2770;
    r->ret.w_001C3D60 = rt_001C3D60;
    r->ret.w_001C63E0 = rt_001C63E0;
    r->ret.w_001C64F0 = rt_001C64F0;
    r->ret.w_001C6960 = rt_001C6960;
    r->ret.w_001C69A0 = rt_001C69A0;
    r->ret.w_001CA6F0 = rt_001CA6F0;
    r->ret.w_001EFD90 = rt_001EFD90;
    r->ret.w_001FB9F0 = rt_001FB9F0;
    r->ret.w_001FBD50 = rt_001FBD50;
    r->ret.w_00824340 = rt_00824340;
    r->ret.w_00824770 = rt_00824770;
    r->ret.w_00824D50 = rt_00824D50;
}
