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
 * em_area01_revisit_spawn); the bytes em_sdf hands back per node (+0x03,
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

static int rv_place_worker(void *ctx, EmArea01Call *c) { return em_area01_runtime_call(ctx, c); }

static int rv_place_c8140(void *ctx, uint32_t bank, int16_t index, uint32_t callback, uint32_t *node,
                          EmSdfSpawned **view)
{
    RvPlace *pl = ctx;
    const EmArea01RuntimeHost host = {pl->r, bytes, rv_place_worker};
    uint64_t v0 = 0;
    if (em_area01_revisit_spawn_001C8140(&host, pl->sp, bank, index, callback, &v0, &pl->fault) < 0) return -1;
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
    /* The placement lists and their records lie in the overlay's data. */
    const uint8_t *data = bytes(r, 0x00828A00u, 0x0082CD00u - 0x00828A00u, 0);
    if (!rec || !self14 || !data) return fail(r, c->function);
    uint8_t record_bytes[0x18];
    memcpy(record_bytes, rec, sizeof record_bytes);
    EmSdfSpawnOwner view_owner;
    memcpy(&view_owner.self_14, self14, 4);
    view_owner.s2E = 0;
    const EmSdfImage image = {data, 0x00828A00u, 0x0082CD00u - 0x00828A00u};
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

/* Returns 1 when `function` is one of these entries (*rc set), else 0. */
static int dispatch_revisit(EmArea01Runtime *r, EmArea01Call *c, int *rc)
{
    if (r->area != 1) return 0;
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
    case 0x001C8140u: { /* a 0x270E placement entry's node (em_area01_revisit_spawn) */
        if (c->na < 3 || c->nf) { *rc = fail(r, c->function); return 1; }
        const EmArea01RuntimeHost host = {r, bytes, rv_place_worker};
        uint32_t f = 0;
        *rc = em_area01_revisit_spawn_001C8140(&host, c->sp, a0, (int16_t)c->a[1], (uint32_t)c->a[2], &c->v0, &f);
        if (*rc < 0) *rc = fail(r, f ? f : c->function);
        return 1;
    }
    case 0x001C7EB0u: { /* that node's behaviour (em_area01_revisit_spawn) */
        if (c->na < 1 || c->nf) { *rc = fail(r, c->function); return 1; }
        const EmArea01RuntimeHost host = {r, bytes, rv_place_worker};
        uint32_t f = 0;
        *rc = em_area01_revisit_spawn_001C7EB0(&host, c->sp, a0, &f);
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
