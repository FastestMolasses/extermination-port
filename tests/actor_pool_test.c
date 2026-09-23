/* WP-3 S4 unit test: the native actor pool (em_actor_pool.c). Restates the
 * original behaviour of 001AF8E0, 001AFA90, 001AFA50, 001AFBC0, 001AFC10 and
 * 001AFD70 (Extermination/src; the .s for the NEARMISS reset and walk) and
 * checks the fail-stop policies the header lists. The executed-original
 * comparison is tools/test_actor_pool_reference.py.
 * Build: cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc
 *        tests/actor_pool_test.c src/game/em_actor_pool.c */
#include "game/em_actor_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                     \
    do {                                                                             \
        if (!(x)) {                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x);    \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

static EmActorPool *pool;
static EmSceneState scene;

/* ------------------------------------------------------------ behaviours */

static EmActor *visits[64];
static int visit_count;
static uint8_t drawn_at_visit[64];

static void clear_visits(void) { visit_count = 0; }

static int b_none(EmActor *a, void *world)
{
    (void)world;
    drawn_at_visit[visit_count] = a->drawn;
    visits[visit_count++] = a;
    return 1;
}

static int b_append(EmActor *a, void *world)
{
    b_none(a, world);
    EmActor *child = em_actor_pool_alloc_001AFA90(pool, &scene, 3);
    CHECK(child);
    child->callback = 0x00C00000u;
    child->behavior = b_none;
    return 1;
}

static int b_free_self(EmActor *a, void *world)
{
    b_none(a, world);
    return em_actor_pool_free_001AFC10(pool, &scene, a) == 0 ? 1 : -1;
}

static int b_free_next(EmActor *a, void *world)
{
    b_none(a, world);
    return em_actor_pool_free_001AFC10(pool, &scene, a->next) == 0 ? 1 : -1;
}

static int b_free_second_next(EmActor *a, void *world)
{
    b_none(a, world);
    return em_actor_pool_free_001AFC10(pool, &scene, a->next->next) == 0 ? 1 : -1;
}

static int b_free_next_then_realloc(EmActor *a, void *world)
{
    EmActor *victim = a->next;
    b_free_next(a, world);
    /* LIFO: the freed record is the next alloc; it is live again but not the
     * node the walk captured. */
    EmActor *again = em_actor_pool_alloc_001AFA90(pool, &scene, 3);
    CHECK(again == victim);
    again->behavior = b_none;
    return 1;
}

static int b_fail(EmActor *a, void *world) { b_none(a, world); return -1; }
static int b_zero(EmActor *a, void *world) { b_none(a, world); return 0; }

/* ------------------------------------------------------------ helpers */

static EmActor *spawn(uint8_t cls, uint32_t callback, EmActorBehavior fn)
{
    EmActor *a = em_actor_pool_alloc_001AFA90(pool, &scene, cls);
    CHECK(a);
    a->callback = callback;
    a->behavior = fn;
    a->drawn = 0x5A;
    return a;
}

static void fresh(void)
{
    memset(&scene, 0, sizeof scene);
    em_actor_pool_reset_001AF8E0(pool);
    clear_visits();
}

static int list_length(void)
{
    int n = 0;
    for (EmActor *a = pool->head; a; a = a->next)
        ++n;
    return n;
}

static int releases;
static EmActor *released[4];
static void on_release(EmActor *a)
{
    if (releases < 4)
        released[releases] = a;
    ++releases;
}

static int bone_calls;
static int bone_worker(void *ctx, EmActor *a)
{
    CHECK(ctx == &bone_calls);
    CHECK(a->bones == 2);
    ++bone_calls;
    return 0;
}
static int bone_worker_fail(void *ctx, EmActor *a) { (void)ctx; (void)a; return -1; }

static uint32_t trace_log[16][2];
static int trace_count;
static void trace(void *ctx, uint32_t caller, uint32_t callee, uint32_t address, const EmActor *a)
{
    CHECK(ctx == &trace_count);
    CHECK(caller == EM_ACTOR_FN_001AFD70);
    CHECK(address == em_actor_pool_address(pool, a));
    if (trace_count < 16) {
        trace_log[trace_count][0] = callee;
        trace_log[trace_count][1] = address;
    }
    ++trace_count;
}

/* ------------------------------------------------------------ tests */

static void test_reset(void)
{
    fresh();
    CHECK(pool->head == NULL && pool->tail == NULL);
    CHECK(pool->free_head == &pool->records[0]);
    CHECK(pool->free_count == 0x100);
    for (int i = 0; i < EM_ACTOR_POOL_CAPACITY; ++i)
        CHECK(pool->records[i].next == (i < 0xFF ? &pool->records[i + 1] : NULL));
    CHECK(em_actor_pool_address(pool, &pool->records[1]) == 0x007A5640u + 0x2F0u);
    CHECK(em_actor_pool_address(pool, NULL) == 0);

    /* Native release hook: every allocated record, record order; the wipe
     * then leaves nothing live. */
    EmActor *a = spawn(1, 0x100, b_none), *b = spawn(2, 0x200, b_none), *c = spawn(3, 0x300, b_none);
    a->release = on_release;
    c->release = on_release;
    b->flags2 = 0x0042;
    CHECK(em_actor_pool_free_001AFC10(pool, &scene, b) == 0);
    CHECK(b->flags2 == 0x0042); /* free keeps +0x2E; the 001AF8E0 memset clears it */
    releases = 0;
    em_actor_pool_reset_001AF8E0(pool);
    CHECK(releases == 2 && released[0] == a && released[1] == c);
    CHECK(pool->free_count == 0x100 && pool->free_head == &pool->records[0]);
    CHECK(a->callback == 0 && a->status == 0 && a->self == NULL && b->flags2 == 0);
}

static void test_alloc(void)
{
    fresh();
    scene.d810701 = 0x21;
    scene.d810702 = 0x37;
    EmActor *a = em_actor_pool_alloc_001AFA90(pool, &scene, 0x42); /* class 2 + flag 0x40 */
    CHECK(a == &pool->records[0]);
    CHECK(a->status == 2 && a->cls == 0x42 && a->self == a);
    CHECK(a->b9D == 0x21 && a->b9E == 0x37);
    CHECK(a->f60[0] == 1.0f && a->f60[3] == 1.0f && a->f60[4] == 0.0f && a->f60[5] == 0.0f);
    CHECK(a->f60[6] == 1.0f && a->f60[7] == 1.0f && a->f80[2] == 1.0f);
    CHECK(a->pos[0] == 0.0f && a->pos[3] == 1.0f);
    CHECK(a->h94 == -1 && a->h96 == 0 && a->w5C == 0x00010101u);
    CHECK(a->behavior == NULL && a->allocated == 1);
    CHECK(pool->head == a && pool->tail == a && pool->free_count == 0xFF);

    EmActor *b = em_actor_pool_alloc_001AFA90(pool, &scene, 3);
    CHECK(b->b9D == 0 && b->b9E == 0); /* only class 2 latches */
    CHECK(b->prev == a && a->next == b && b->next == NULL && pool->tail == b);

    /* 0xC reserve: refused while fewer than 10 are free (flags ignored). */
    while (pool->free_count > 10)
        CHECK(em_actor_pool_alloc_001AFA90(pool, &scene, 1));
    int16_t before = pool->free_count;
    CHECK(em_actor_pool_alloc_001AFA90(pool, &scene, 0xEC) != NULL); /* 10 free: allowed */
    CHECK(pool->free_count == 9);
    CHECK(em_actor_pool_alloc_001AFA90(pool, &scene, 0x0C) == NULL);
    CHECK(em_actor_pool_alloc_001AFA90(pool, &scene, 0x2C) == NULL);
    CHECK(pool->free_count == 9 && before == 10);
    CHECK(em_actor_pool_alloc_001AFA90(pool, &scene, 0x0D) != NULL);
    while (pool->free_count > 0)
        CHECK(em_actor_pool_alloc_001AFA90(pool, &scene, 1));
    CHECK(pool->free_head == NULL);
    CHECK(em_actor_pool_alloc_001AFA90(pool, &scene, 1) == NULL); /* empty: 0, not a fault */
    CHECK(!em_scene_faulted(&scene));
}

static void test_free(void)
{
    fresh();
    EmActor *a = spawn(1, 0x100, b_none), *b = spawn(1, 0x200, b_none), *c = spawn(1, 0x300, b_none);
    b->rot[1] = 7.0f;
    b->scratch[0] = 1;
    b->scratch[0xFF] = 2;
    b->uid = 0x1234;
    b->w30 = 5;
    b->flags2 = 0x00AB; /* +0x2E: a spawner's halfword */
    CHECK(em_actor_pool_free_001AFC10(pool, &scene, b) == 0);
    CHECK(a->next == c && c->prev == a && pool->head == a && pool->tail == c);
    CHECK(pool->free_head == b && b->next == &pool->records[3] && pool->free_count == 0xFE);
    CHECK(b->self == NULL && b->uid == 0 && b->scratch[0] == 0 && b->scratch[0xFF] == 0);
    CHECK(b->callback == 0x200); /* +0x10 is not cleared */
    CHECK(b->rot[1] == 7.0f && b->w30 == 5 && b->flags2 == 0x00AB); /* not cleared by free */
    CHECK(b->behavior == NULL && b->allocated == 0);
    /* LIFO reuse; alloc rewrites w30 but not rot. */
    EmActor *again = em_actor_pool_alloc_001AFA90(pool, &scene, 1);
    CHECK(again == b && b->w30 == 0 && b->rot[1] == 7.0f && pool->tail == b);
    CHECK(b->flags2 == 0x00AB); /* 001AFA90 does not write +0x2E either */
    {
        uint8_t image[EM_ACTOR_RECORD_SIZE];
        em_actor_pool_record_image(pool, b, image);
        CHECK(image[0x2E] == 0xAB && image[0x2F] == 0x00);
    }

    /* Non-canonical handle: its +0x14 names the record to free. */
    EmActor *handle = &pool->records[10];
    CHECK(!handle->allocated);
    handle->self = c;
    CHECK(em_actor_pool_free_001AFC10(pool, &scene, handle) == 0);
    CHECK(handle->self == NULL && c->self == c && pool->free_head == c && pool->tail == b);

    /* Double free: +0x14 is already 0 (the original dereferences 0). */
    CHECK(em_actor_pool_free_001AFC10(pool, &scene, b) == 0);
    CHECK(em_actor_pool_free_001AFC10(pool, &scene, b) == -1);
    CHECK(scene.fault.code == EM_SCENE_FAULT_BAD_INDEX && scene.fault.address == 0x001AFC10u);

    /* Bones need the 001AF800 worker. */
    fresh();
    EmActor *boned = spawn(1, 0x100, b_none);
    boned->bones = 2;
    boned->u0A[2] = 9;
    CHECK(em_actor_pool_free_001AFC10(pool, &scene, boned) == -1);
    CHECK(scene.fault.code == EM_SCENE_FAULT_NULL_WORKER && scene.fault.address == 0x001AF800u);
    fresh();
    boned = spawn(1, 0x100, b_none);
    boned->bones = 2;
    pool->w_001AF800 = bone_worker;
    pool->worker_ctx = &bone_calls;
    CHECK(em_actor_pool_free_001AFC10(pool, &scene, boned) == 0);
    CHECK(bone_calls == 1 && boned->bones == 0 && boned->u0A[2] == 0);
    fresh();
    boned = spawn(1, 0x100, b_none);
    boned->bones = 2;
    pool->w_001AF800 = bone_worker_fail;
    CHECK(em_actor_pool_free_001AFC10(pool, &scene, boned) == -1);
    CHECK(scene.fault.code == EM_SCENE_FAULT_WORKER_FAILED && scene.fault.address == 0x001AF800u);
    pool->w_001AF800 = NULL;
    pool->worker_ctx = NULL;
}

static void test_walk_modes(void)
{
    static const uint8_t classes[] = {1, 0x21, 3, 0xE1, 0x0C, 1, 0x80};
    for (int mode = 0; mode < 4; ++mode) {
        fresh();
        EmActor *nodes[7];
        for (int i = 0; i < 7; ++i)
            nodes[i] = spawn(classes[i], 0x1000u + (uint32_t)i, b_none);
        scene.spad3B8A = 0x7777;
        CHECK(em_actor_pool_walk_001AFD70(pool, &scene, mode, NULL, NULL, NULL) == 0);
        CHECK(scene.spad3B8A == 7); /* skipped nodes count too */
        int expect = 0;
        for (int i = 0; i < 7; ++i) {
            int is1 = (classes[i] & 0x1F) == 1;
            int ticked = mode == 1 ? !is1 : mode == 2 ? is1 : 1;
            CHECK(nodes[i]->drawn == (ticked ? 0 : 0x5A));
            if (ticked) {
                CHECK(visits[expect] == nodes[i]);
                CHECK(drawn_at_visit[expect] == 0); /* cleared before the call */
                ++expect;
            }
        }
        CHECK(visit_count == expect);
    }
    /* Cutscene: mode 1 then mode 2 visit every node exactly once. */
    fresh();
    for (int i = 0; i < 7; ++i)
        spawn(classes[i], 0x1000u + (uint32_t)i, b_none);
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 1, NULL, NULL, NULL) == 0);
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 2, NULL, NULL, NULL) == 0);
    CHECK(visit_count == 7);

    /* Empty list. */
    fresh();
    scene.spad3B8A = 3;
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 0, NULL, NULL, NULL) == 0);
    CHECK(scene.spad3B8A == 0 && visit_count == 0);
}

static void test_walk_mutation(void)
{
    /* Appended by a non-tail node: ticked in the same walk. Appended by the
     * tail: not (its saved next was 0). */
    fresh();
    EmActor *a = spawn(3, 0x100, b_append), *b = spawn(3, 0x200, b_none);
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 0, NULL, NULL, NULL) == 0);
    CHECK(visit_count == 3 && visits[0] == a && visits[1] == b && visits[2] == pool->tail);
    fresh();
    a = spawn(3, 0x100, b_none);
    b = spawn(3, 0x200, b_append);
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 0, NULL, NULL, NULL) == 0);
    CHECK(visit_count == 2 && list_length() == 3 && scene.spad3B8A == 2);

    /* Self-free continues with the saved next. */
    fresh();
    a = spawn(3, 0x100, b_free_self);
    b = spawn(3, 0x200, b_none);
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 0, NULL, NULL, NULL) == 0);
    CHECK(visit_count == 2 && visits[1] == b && pool->head == b && pool->free_head == a);

    /* Freeing a node that is not the saved next: it is simply not reached. */
    fresh();
    a = spawn(3, 0x100, b_free_second_next);
    b = spawn(3, 0x200, b_none);
    EmActor *c = spawn(3, 0x300, b_none);
    EmActor *d = spawn(3, 0x400, b_none);
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 0, NULL, NULL, NULL) == 0);
    CHECK(visit_count == 3 && visits[0] == a && visits[1] == b && visits[2] == d);
    CHECK(c->allocated == 0 && b->next == d && scene.spad3B8A == 3);

    /* Freeing the saved next faults with the owner's callback. */
    fresh();
    a = spawn(3, 0x100, b_none);
    b = spawn(3, 0x200, b_free_next);
    c = spawn(3, 0x300, b_none);
    d = spawn(3, 0x400, b_none);
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 0, NULL, NULL, NULL) == -1);
    CHECK(scene.fault.code == EM_SCENE_FAULT_FREED_NEXT && scene.fault.address == 0x200);
    CHECK(visit_count == 2 && c->allocated == 0);
    /* A latched fault stops everything. */
    clear_visits();
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 0, NULL, NULL, NULL) == -1);
    CHECK(visit_count == 0);

    /* Freed and immediately re-allocated next: still a fault. */
    fresh();
    a = spawn(3, 0x100, b_free_next_then_realloc);
    b = spawn(3, 0x200, b_none);
    c = spawn(3, 0x300, b_none);
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 0, NULL, NULL, NULL) == -1);
    CHECK(scene.fault.code == EM_SCENE_FAULT_FREED_NEXT && scene.fault.address == 0x100);
    CHECK(b->allocated == 1 && pool->tail == b);

    /* Unbound, failing and out-of-range behaviours. */
    fresh();
    a = spawn(3, 0x100, b_none);
    b = spawn(3, 0x00827490u, NULL);
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 0, NULL, NULL, NULL) == -1);
    CHECK(scene.fault.code == EM_SCENE_FAULT_NULL_WORKER && scene.fault.address == 0x00827490u);
    CHECK(b->drawn == 0 && visit_count == 1);
    fresh();
    spawn(3, 0x500, b_fail);
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 0, NULL, NULL, NULL) == -1);
    CHECK(scene.fault.code == EM_SCENE_FAULT_WORKER_FAILED && scene.fault.address == 0x500);
    fresh();
    spawn(3, 0x600, b_zero);
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 0, NULL, NULL, NULL) == -1);
    CHECK(scene.fault.code == EM_SCENE_FAULT_BAD_RESULT && scene.fault.address == 0x600);
}

static void test_trace_and_current(void)
{
    fresh();
    EmActor *a = spawn(1, 0x100, b_none), *b = spawn(3, 0x200, b_none);
    trace_count = 0;
    CHECK(em_actor_pool_walk_001AFD70(pool, &scene, 1, NULL, trace, &trace_count) == 0);
    CHECK(trace_count == 2);
    CHECK(trace_log[0][0] == 0x001CB590u && trace_log[0][1] == em_actor_pool_address(pool, b));
    CHECK(trace_log[1][0] == 0x200 && trace_log[1][1] == em_actor_pool_address(pool, b));
    CHECK(pool->current == b);
    EmActorPoolGlobals g;
    em_actor_pool_globals(pool, &g);
    CHECK(g.head == 0x007A5640u && g.tail == 0x007A5640u + 0x2F0u && g.current == g.tail);
    CHECK(g.free_head == 0x007A5640u + 2u * 0x2F0u && g.free_count == 0xFE);
    uint8_t image[EM_ACTOR_RECORD_SIZE];
    em_actor_pool_record_image(pool, a, image);
    CHECK(image[0] == 2 && image[2] == 1 && image[0x10] == 0x00 && image[0x11] == 0x01);
    CHECK(image[0x14] == 0x40 && image[0x15] == 0x56 && image[0x16] == 0x7A); /* +0x14 = self */
    CHECK(image[0x1C] == 0x30 && image[0x1D] == 0x59); /* +0x1C = 0x7A5930 */
    CHECK(image[0x63] == 0x3F && image[0x62] == 0x80 && image[0x94] == 0xFF && image[0x95] == 0xFF);
}

int main(void)
{
    pool = calloc(1, sizeof *pool);
    CHECK(pool);
    test_reset();
    test_alloc();
    test_free();
    test_walk_modes();
    test_walk_mutation();
    test_trace_and_current();
    free(pool);
    printf("actor_pool_test: PASS\n");
    return 0;
}
