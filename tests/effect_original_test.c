/* Unit test for em_effect_original (001EFD90 / 001EFD20 / 001EF9D0 /
 * 001EF940 / 001F0460 / 001EA240 / 001CCF70 / 001CD390). Equality with the
 * original instructions is established by
 * tools/test_effect_original_reference.py; this test pins the native
 * contract: fail-stop faults, the worker protocol and the driver's life
 * cycle. It needs no disc data: the tables are built in memory. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game/em_effect_original.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static uint32_t bits(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }

typedef struct {
    EmEffectOriginalNode nodes[4];
    int next, refuse, fail; /* fail: which worker returns -1 (1 alloc .. 8 free) */
    int allocs, rands, lights, sfxpos, sfx, rumbles, handlers, frees;
    int32_t rand_value, sfx_result;
    int32_t last_type, last_depth;
    uint32_t last_handler;
} Log;

static int w_alloc(void *c, uint8_t cls, EmEffectOriginalNode **n)
{
    Log *l = c; (void)cls; l->allocs++;
    if (l->fail == 1) return -1;
    *n = l->refuse ? NULL : &l->nodes[l->next++ & 3];
    return 0;
}
static int w_rand(void *c, int32_t *v) { Log *l = c; l->rands++; *v = l->rand_value; return l->fail == 2 ? -1 : 0; }
static int w_light(void *c, const float p[4], const float col[4], int32_t type, float fa, float fb)
{
    Log *l = c; (void)p; (void)col; (void)fa; (void)fb; l->lights++; l->last_type = type;
    return l->fail == 3 ? -1 : 0;
}
static int w_sfxpos(void *c, const float p[4], float f12, float f13, int32_t *a, int32_t *b, int32_t *r)
{
    Log *l = c; (void)p; (void)f12; (void)f13; l->sfxpos++; *a = 7; *b = 9; *r = l->sfx_result;
    return l->fail == 4 ? -1 : 0;
}
static int w_sfx(void *c, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    Log *l = c; (void)id; (void)a1; (void)a2; (void)a3; l->sfx++; return l->fail == 5 ? -1 : 0;
}
static int w_rumble(void *c, int32_t ch, float f12, float f13)
{
    Log *l = c; (void)ch; (void)f12; (void)f13; l->rumbles++; return l->fail == 6 ? -1 : 0;
}
static int w_handler(void *c, uint32_t h, EmEffectOriginalNode *n, int32_t depth, EmEffectOriginalWork *w)
{
    Log *l = c; l->handlers++; l->last_handler = h; l->last_depth = depth;
    CHECK(w == &n->work);
    return l->fail == 7 ? -1 : 0;
}
static int w_free(void *c, EmEffectOriginalNode *n) { Log *l = c; (void)n; l->frees++; return l->fail == 8 ? -1 : 0; }

static EmEffectOriginalWorkers workers(Log *l)
{
    EmEffectOriginalWorkers w = { l, w_alloc, w_rand, w_light, w_sfxpos, w_sfx, w_rumble, w_handler, w_free };
    return w;
}

static EmEffectOriginalTables tables;
static EmEffectOriginalGlobals globals;
static EmEffectOriginalDecals decals;
static EmEffectOriginalView view;

static void put32(uint32_t address, uint32_t v)
{
    uint8_t *p = tables.bytes + (address - EM_EFFECT_ORIGINAL_TABLE_BASE);
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Record i: class 0xC, +4 0, +8 subtype, callback 001EA240, kind, sound. */
static void record(uint32_t i, uint8_t subtype, uint32_t kind, uint32_t sound)
{
    uint32_t a = EM_EFFECT_ORIGINAL_TABLE_BASE + i * EM_EFFECT_ORIGINAL_RECORD;
    tables.bytes[a - EM_EFFECT_ORIGINAL_TABLE_BASE] = 0x0C;
    tables.bytes[a + 8 - EM_EFFECT_ORIGINAL_TABLE_BASE] = subtype;
    put32(a + 0x0C, EM_EFFECT_ORIGINAL_DRIVER);
    for (int k = 0; k < 4; ++k) put32(a + 0x10 + 4u * (uint32_t)k, bits(1.0f));
    put32(a + 0x20, kind);
    put32(a + 0x24, sound);
    put32(a + 0x28, bits(300.0f));
    put32(a + 0x2C, bits(4096.0f));
}

static void reset(EmEffectOriginal *e, Log *l, EmEffectOriginalWorkers *w)
{
    memset(&tables, 0, sizeof tables);
    memset(&globals, 0, sizeof globals);
    memset(&decals, 0, sizeof decals);
    memset(&view, 0, sizeof view);
    tables.global = EM_EFFECT_ORIGINAL_TABLE_BASE;
    tables.area[11] = EM_EFFECT_ORIGINAL_TABLE_BASE + 0x40u * EM_EFFECT_ORIGINAL_RECORD;
    for (int i = 0; i < EM_EFFECT_ORIGINAL_SUBTYPES; ++i) {
        tables.step[i] = bits(0.03f);
        tables.handler[i] = 0x001EC000u + (uint32_t)i;
    }
    record(0x28, 5, 0, 0xFFFFFFFFu);
    record(0x26, 0x18, 0, 0xFFFFFFFFu);
    record(0x27, 0x20, 1, 0x14A);
    record(0x39, 0x20, 4, 0xFFFFFFFFu);
    /* identity clip and camera: points with |x|,|y|,|z| <= w are inside */
    for (int i = 0; i < 4; ++i) {
        view.clip[i * 5] = 1.0f;
        view.camera[i * 5] = 1.0f;
    }
    view.fog[0] = 255.0f;
    memset(l, 0, sizeof *l);
    *w = workers(l);
    memset(e, 0, sizeof *e);
    e->tables = &tables;
    e->globals = &globals;
    e->decals = &decals;
    e->view = &view;
    e->workers = w;
}

static void test_spawn(void)
{
    EmEffectOriginal e; Log l; EmEffectOriginalWorkers w;
    reset(&e, &l, &w);
    const float pos[4] = {0.25f, 0.5f, 0.125f, 7.0f}, rot[4] = {0.1f, 0.2f, 0.3f, 0.0f};
    EmEffectOriginalNode *n = NULL;
    CHECK(em_effect_original_001EFD90(&e, 0x80000028u, pos, rot, &n) == 0);
    CHECK(n == &l.nodes[0] && n->subtype == 5 && n->callback == EM_EFFECT_ORIGINAL_DRIVER);
    CHECK(n->live38 == 1 && n->pos[3] == 1.0f && n->rot[2] == 0.3f && l.lights == 0 && l.sfxpos == 0);

    /* a record whose callback word is 0: no node, no alloc */
    CHECK(em_effect_original_001EFD20(&e, 0x80000001u, pos, &n) == 0 && n == NULL && l.allocs == 1);

    /* the pool refuses: 0 without a fault */
    l.refuse = 1;
    CHECK(em_effect_original_001EFD20(&e, 0x80000028u, pos, &n) == 0 && n == NULL);
    CHECK(e.fault.code == EM_EFFECT_FAULT_NONE);
    l.refuse = 0;

    /* 001EFD20 zeroes the rotation */
    CHECK(em_effect_original_001EFD20(&e, 0x80000028u, pos, &n) == 0 && n);
    CHECK(n->rot[0] == 0.0f && n->rot[3] == 0.0f && n->pos[3] == 1.0f);

    /* kind 1 point light and the live sound (record 0x27) */
    l.sfx_result = 1;
    CHECK(em_effect_original_001EFD90(&e, 0x80000027u, pos, rot, &n) == 0);
    CHECK(l.lights == 1 && l.last_type == 0 && l.sfxpos == 1 && l.sfx == 1);
    globals.d8101E4 = 3; /* 001EF940 gate */
    CHECK(em_effect_original_001EFD90(&e, 0x80000027u, pos, rot, &n) == 0 && l.sfxpos == 1);
    globals.d8101E4 = 0;

    /* kind 4 throttle: fires once per 13-tick distance, then stamps */
    int lights = l.lights;
    globals.spad3B68 = 100; globals.d275C38 = 88;
    CHECK(em_effect_original_001EF9D0(&e, 0x80000039u, pos, 1.0f, &n) == 0 && l.lights == lights);
    globals.d275C38 = 87;
    CHECK(em_effect_original_001EF9D0(&e, 0x80000039u, pos, 1.0f, &n) == 0 && l.lights == lights + 1);
    CHECK(l.last_type == 1);
    CHECK(globals.d275C38 == 100);

    /* ids 0x80000026: rand % 4 -> the table's FIRST record +0x24 */
    l.rand_value = 6;
    CHECK(em_effect_original_001EF9D0(&e, 0x80000026u, NULL, 1.0f, &n) == 0);
    CHECK(tables.bytes[0x24] == 0x90 && tables.bytes[0x25] == 0x01); /* 0x18E + 2 */
    l.rand_value = -3; /* C remainder -3: no store */
    tables.bytes[0x24] = 0x55;
    CHECK(em_effect_original_001EF9D0(&e, 0x80000026u, NULL, 1.0f, &n) == 0 && tables.bytes[0x24] == 0x55);
    CHECK(em_effect_original_001EF9D0(&e, 0x80000026u, NULL, 0.5f, &n) == 0 && tables.bytes[0x24] == 0xFF);
    CHECK(e.fault.code == EM_EFFECT_FAULT_NONE);

    /* an id beyond the loaded tables faults instead of reading past them */
    CHECK(em_effect_original_001EF9D0(&e, 0x80000200u, NULL, 1.0f, &n) == -1);
    CHECK(e.fault.code == EM_EFFECT_FAULT_BAD_INDEX);
    CHECK(em_effect_original_001EFD20(&e, 0x80000028u, pos, &n) == -1); /* latched */

    reset(&e, &l, &w);
    globals.d810700 = EM_EFFECT_ORIGINAL_AREAS;
    CHECK(em_effect_original_001EF9D0(&e, 5, NULL, 1.0f, &n) == -1 && e.fault.code == EM_EFFECT_FAULT_BAD_INDEX);
    reset(&e, &l, &w);
    globals.d810700 = 3; /* D_00259C74[3] == 0 here: address 0 is outside */
    CHECK(em_effect_original_001EF9D0(&e, 5, NULL, 1.0f, &n) == -1 && e.fault.code == EM_EFFECT_FAULT_BAD_INDEX);
}

static void test_worker_faults(void)
{
    const float pos[4] = {0}, rot[4] = {0};
    for (int fail = 1; fail <= 5; ++fail) {
        EmEffectOriginal e; Log l; EmEffectOriginalWorkers w;
        reset(&e, &l, &w);
        l.fail = fail;
        l.sfx_result = 1;
        EmEffectOriginalNode *n = NULL;
        uint32_t id = fail == 2 ? 0x80000026u : 0x80000027u;
        const float r1[4] = {0, 0, 0, 1.0f};
        CHECK(em_effect_original_001EFD90(&e, id, pos, fail == 2 ? r1 : rot, &n) == -1);
        CHECK(e.fault.code == EM_EFFECT_FAULT_WORKER_FAILED);
    }
    EmEffectOriginal e; Log l; EmEffectOriginalWorkers w;
    reset(&e, &l, &w);
    w.w_001AFA90 = NULL;
    EmEffectOriginalNode *n = NULL;
    CHECK(em_effect_original_001EFD20(&e, 0x80000028u, pos, &n) == -1);
    CHECK(e.fault.code == EM_EFFECT_FAULT_NULL_WORKER && e.fault.address == 0x001AFA90u);
}

static void test_driver(void)
{
    EmEffectOriginal e; Log l; EmEffectOriginalWorkers w;
    reset(&e, &l, &w);
    const float pos[4] = {0.0f, 0.0f, 0.5f, 1.0f}, rot[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    EmEffectOriginalNode *n = NULL;
    CHECK(em_effect_original_001EFD90(&e, 0x80000028u, pos, rot, &n) == 0 && n);
    n->state = 0; /* 001AFA90 hands out a record in state 0 */
    int ticks = 0, r;
    float before = 0.0f;
    while ((r = em_effect_original_001EA240(&e, n)) == 1) {
        if (n->state == 1) before = n->work.accumulator;
        ++ticks;
        CHECK(globals.d275C30 == n && globals.d275C34 == &n->work);
        if (ticks > 100) break;
    }
    CHECK(r == 0 && n->freed == 1 && l.frees == 1);
    CHECK(before <= 1.5f && n->work.accumulator > 1.5f);
    CHECK(ticks == l.handlers && l.rands == 2 && l.rumbles == 0);
    CHECK(l.last_handler == 0x001EC005u && l.last_depth != EM_EFFECT_ORIGINAL_CLIPPED);
    /* rotation (0,0,0) and the 00102BB0(pi) turn: row 0 ~ (-1, 0, 0) */
    CHECK(n->matrix[0] < -0.99f && n->matrix[15] == 1.0f && n->matrix[13] == 0.5f);
    /* a freed node faults instead of running again */
    CHECK(em_effect_original_001EA240(&e, n) == -1 && e.fault.code == EM_EFFECT_FAULT_BAD_INDEX);

    /* endless limit: the decay clamp keeps the accumulator at or below 2.0 + step */
    reset(&e, &l, &w);
    EmEffectOriginalNode m;
    memset(&m, 0, sizeof m);
    m.state = 1; m.subtype = 5; m.work.limit = 0.0f; m.work.step = 0.5f; m.work.accumulator = 1.75f;
    m.matrix[15] = 1.0f;
    CHECK(em_effect_original_001EA240(&e, &m) == 1 && m.work.accumulator == 1.25f && m.state == 1);

    /* a subtype past D_00255430 faults */
    reset(&e, &l, &w);
    memset(&m, 0, sizeof m);
    m.subtype = EM_EFFECT_ORIGINAL_SUBTYPES;
    CHECK(em_effect_original_001EA240(&e, &m) == -1 && e.fault.code == EM_EFFECT_FAULT_BAD_INDEX);

    /* a NaN translation: vmaddw.xyzw (form (15,3)) clamps ACC, so VCLIP only
     * ever sees finite lanes (here x = w = MAX): no fault */
    reset(&e, &l, &w);
    memset(&m, 0, sizeof m);
    m.state = 1; m.subtype = 5; m.work.limit = 1.5f; m.matrix[12] = NAN;
    CHECK(em_effect_original_001EA240(&e, &m) == 1 && e.fault.code == EM_EFFECT_FAULT_NONE && l.handlers == 1);

    /* a jitter subtype whose rotation 001B1470 can never wrap: fault */
    reset(&e, &l, &w);
    memset(&m, 0, sizeof m);
    m.state = 0; m.subtype = 0x28; m.live38 = 1; m.rot[0] = 1.0e30f;
    r = em_effect_original_001EA240(&e, &m);
    CHECK(r == -1 && e.fault.code == EM_EFFECT_FAULT_UNMEASURED && e.fault.address == 0x001B1470u);

    /* rumble subtypes: 0x28 kicks channels 2 and 3 and re-arms channel 1 */
    reset(&e, &l, &w);
    memset(&m, 0, sizeof m);
    m.state = 1; m.subtype = 0x28; m.work.limit = 1.5f;
    CHECK(em_effect_original_001EA240(&e, &m) == 1 && l.rumbles == 3);

    /* handler failure latches */
    reset(&e, &l, &w);
    memset(&m, 0, sizeof m);
    m.state = 1; m.subtype = 5; m.work.limit = 1.5f;
    l.fail = 7;
    CHECK(em_effect_original_001EA240(&e, &m) == -1 && e.fault.code == EM_EFFECT_FAULT_WORKER_FAILED);
    CHECK(e.fault.address == 0x001EC005u);
}

static void test_decals_and_leaves(void)
{
    EmEffectOriginal e; Log l; EmEffectOriginalWorkers w;
    reset(&e, &l, &w);
    float src[16];
    for (int i = 0; i < 16; ++i) src[i] = (float)i;
    CHECK(em_effect_original_001F0460(&e, 7, src) == 0 && em_effect_original_001F0460(&e, -1, src) == 0);
    decals.index[1] = 0x13; /* limit 0x14: wraps to 0 */
    CHECK(em_effect_original_001F0460(&e, 1, src) == 0 && decals.index[1] == 0);
    CHECK(decals.slot[1][0].life == 180 && decals.slot[1][0].tag == UINT64_C(0x200418851532218C));
    CHECK(decals.slot[1][0].params[0] == bits(48.0f) && decals.slot[1][0].source[5] == bits(5.0f));
    CHECK(em_effect_original_001F0460(&e, 4, src) == 0 && decals.slot[4][1].life == 1200);
    /* preset 0 also spawns 0x8000000E at the source's row 3 */
    record(0x0E, 7, 0, 0xFFFFFFFFu);
    CHECK(em_effect_original_001F0460(&e, 0, src) == 0 && l.allocs == 1 && l.nodes[0].pos[0] == 12.0f);
    decals.index[3] = -5;
    CHECK(em_effect_original_001F0460(&e, 3, src) == -1 && e.fault.code == EM_EFFECT_FAULT_BAD_INDEX);

    float out;
    CHECK(em_effect_original_001B1470(7.0f, &out) == 0 && out > 0.71f && out < 0.72f);
    CHECK(em_effect_original_001B1470(1.0e30f, &out) == -1);
    CHECK(em_effect_original_001B1470(NAN, &out) == -1);
    CHECK(em_effect_original_float_to_int(-2.9f) == -2 && em_effect_original_float_to_int(3.0e9f) == INT32_MAX);
    CHECK(em_effect_original_float_to_int(NAN) == 0 && em_effect_original_float_to_int(-INFINITY) == INT32_MIN);

    /* the recorded EE add.s example of the pre-trim (docs/EE_FLOAT_MODEL.md) */
    CHECK(em_effect_original_fp(EM_EFFECT_FP_EE_ADD, 0, 0x40555555u, 0x40000001u, 0) == 0x40AAAAABu);
    CHECK(em_effect_original_fp(EM_EFFECT_FP_EE_DIV, 0, bits(1.0f), 0, 0) == 0x7F7FFFFFu);

    /* 001CD390 with x == z == 0: the +5 bias on Z, row 2 = the input */
    reset(&e, &l, &w);
    const float up[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    float m[16];
    CHECK(em_effect_original_001CD390(&e, m, up) == 0);
    CHECK(globals.spad3600[6] == bits(5.0f) && m[9] == 1.0f && m[15] == 1.0f && m[3] == 0.0f);

    /* 001CCF70 outside the clip volume leaves the scratchpad alone */
    const float far[4] = {5.0f, 0.0f, 0.0f, 1.0f};
    int32_t key = 0;
    globals.spad3600[0] = 0x1234;
    CHECK(em_effect_original_001CCF70(&e, far, &key) == 0 && key == EM_EFFECT_ORIGINAL_CLIPPED);
    CHECK(globals.spad3600[0] == 0x1234);
    const float near[4] = {0.5f, 0.25f, 0.5f, 1.0f};
    CHECK(em_effect_original_001CCF70(&e, near, &key) == 0 && key == 8 && globals.d275C04 == 1);

    EmEffectOriginalTables t;
    uint8_t small[16] = {0x7F, 'E', 'L', 'F'};
    CHECK(em_effect_original_load_tables(small, sizeof small, &t) == -1);
}

int main(void)
{
    test_spawn();
    test_worker_faults();
    test_driver();
    test_decals_and_leaves();
    if (failures) {
        fprintf(stderr, "effect_original_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("effect_original_test: OK\n");
    return 0;
}
