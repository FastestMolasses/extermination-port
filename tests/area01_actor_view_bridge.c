#include "game/em_area01_actor_view.h"
#include "game/em_area01_math_actor.h"

#include <string.h>

static EmActorPool pool;
static EmSceneState scene;
static EmArea01ActorView view;
static uint8_t output[EM_ACTOR_POOL_CAPACITY * EM_ACTOR_RECORD_SIZE];
static uint8_t shared_words[16], shared_matrix[64], shared_slots[8], scratch[0x4000];
static EmActor *model_actor;
static uint32_t model_generation;
static int projection_bad, rebind_bad;

static int behavior(EmActor *a, void *world) { (void)a; (void)world; return 1; }
static int rebind(void *ctx, EmActor *a, uint32_t callback)
{
    (void)ctx;
    if (rebind_bad || callback < 0xA00000 || callback >= 0xA00100) return -1;
    a->callback = callback;
    a->behavior = behavior;
    return 0;
}

static int project(void *ctx, EmActor *a, EmArea01ActorSpan spans[EM_AREA01_ACTOR_SHARED_MAX])
{
    (void)ctx;
    if (a != model_actor || a->generation != model_generation) return 0;
    spans[0] = (EmArea01ActorSpan){0x40, 8, shared_words, 0};
    spans[1] = (EmArea01ActorSpan){0x4C, 4, shared_words + 12, 0};
    spans[2] = (EmArea01ActorSpan){0xD0, 64, shared_matrix, 1};
    spans[3] = (EmArea01ActorSpan){0x110, 8, shared_slots, 0};
    if (projection_bad == 1) spans[3].offset = 0xD0;
    if (projection_bad == 2) spans[3].offset = 0x30;
    if (projection_bad == 3) spans[3].bytes = NULL;
    return 4;
}

void av_reset(void)
{
    em_actor_pool_reset_001AF8E0(&pool);
    memset(&scene, 0, sizeof scene);
    model_actor = NULL; projection_bad = rebind_bad = 0;
    em_area01_actor_view_reset(&view, &pool, project, rebind, NULL);
}

uint32_t av_alloc(uint8_t cls)
{ return em_actor_pool_address(&pool, em_actor_pool_alloc_001AFA90(&pool, &scene, cls)); }
static EmActor *actor(uint32_t address)
{
    if (address < EM_ACTOR_POOL_BASE || (address - EM_ACTOR_POOL_BASE) % EM_ACTOR_RECORD_SIZE ||
        (address - EM_ACTOR_POOL_BASE) / EM_ACTOR_RECORD_SIZE >= EM_ACTOR_POOL_CAPACITY) return NULL;
    return &pool.records[(address - EM_ACTOR_POOL_BASE) / EM_ACTOR_RECORD_SIZE];
}
int av_free(uint32_t address) { return em_actor_pool_free_001AFC10(&pool, &scene, actor(address)); }
int av_begin(void) { return em_area01_actor_view_begin(&view); }
int av_commit(void) { return em_area01_actor_view_commit(&view); }
uint8_t *av_bytes(uint32_t address, uint32_t size, int write)
{ return em_area01_actor_view_bytes(&view, address, size, write); }
int av_fault(void) { return view.fault; }

uint8_t *av_image(void)
{
    /* Test-only observation of free records as well as live ones. */
    for (unsigned i = 0; i < EM_ACTOR_POOL_CAPACITY; ++i) {
        uint8_t *p = output + i * EM_ACTOR_RECORD_SIZE;
        uint8_t represented[EM_ACTOR_RECORD_SIZE];
        memcpy(p, view.record[i].image, EM_ACTOR_RECORD_SIZE);
        em_actor_pool_record_image(&pool, &pool.records[i], represented);
        const unsigned ranges[][2] = {{0,0x20},{0x2E,0x38},{0x52,0x9B},{0x9C,0x9F},{0xB0,0xD0},{0x1F0,0x2F0}};
        for (unsigned j = 0; j < sizeof ranges / sizeof ranges[0]; ++j)
            memcpy(p + ranges[j][0], represented + ranges[j][0], ranges[j][1] - ranges[j][0]);
    }
    return output;
}

void av_native_word(uint32_t address, uint32_t value) { actor(address)->w30 = value; }
void av_model(uint32_t address)
{
    model_actor = actor(address); model_generation = model_actor->generation;
    for (unsigned i = 0; i < sizeof shared_words; ++i) shared_words[i] = (uint8_t)(0x80+i);
    for (unsigned i = 0; i < sizeof shared_matrix; ++i) shared_matrix[i] = (uint8_t)(0x40+i);
    for (unsigned i = 0; i < sizeof shared_slots; ++i) shared_slots[i] = (uint8_t)(0x20+i);
}
uint8_t *av_matrix(void) { return shared_matrix; }
void av_bad_projection(int mode) { projection_bad = mode; }
void av_bad_rebind(void) { rebind_bad = 1; }

static uint8_t *math_view(void *ctx, uint32_t address, uint32_t size, int write)
{
    (void)ctx;
    if (size <= sizeof scratch && address >= 0x70000000u &&
        address - 0x70000000u <= sizeof scratch - size) return scratch + address - 0x70000000u;
    return em_area01_actor_view_bytes(&view, address, size, write);
}

static int math_call(void *ctx, uint32_t fn, const uint32_t *a, unsigned na,
                     const uint32_t *f, unsigned nf, uint32_t *v0, uint32_t *f0)
{
    (void)ctx; (void)f; (void)v0; (void)f0;
    if (fn != 0x1026A0 || na != 3 || nf || a[0] != 0x70003610 || a[2] != 0x70003600 ||
        !actor(a[1] - 0xD0) || av_commit() < 0) return -1;
    /* Explicit test boundary: both original and native helper receive
     * this same vector result. It is not a replacement game worker. */
    const uint32_t result[4] = {0x40000000, 0x40400000, 0x40800000, 0x3F800000};
    memcpy(scratch + 0x3610, result, sizeof result);
    return av_begin();
}

int av_math_move(uint32_t node)
{
    memset(scratch, 0, sizeof scratch);
    EmA01Math m = {0}; m.view = math_view; m.call = math_call;
    if (av_begin() < 0 || em_area01_math_001C39F0(&m, node, node + 0x1F0, 0x3F800000) < 0)
        return -1;
    return av_commit();
}

int av_math_alias(uint32_t node, uint32_t value)
{
    EmA01Math m = {0}; m.view = math_view;
    if (av_begin() < 0) return -1;
    em_a01m_sw(&m, node + 0x20000000u + 0x34u, value);
    if (m.fault_code || em_a01m_lw(&m, node + 0x30000000u + 0x34u) != value) return -1;
    return av_commit();
}
