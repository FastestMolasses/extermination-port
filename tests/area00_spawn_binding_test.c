/* The runner extracts the production scene functions into the include.
 * Pool/roster/view are real; loader and outer player/collision brackets
 * are controlled boundaries. Original instructions are checked separately. */
#include "game/em_actor_roster.h"
#include "game/em_area01_live.h"
#include "game/em_module_loader.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static EmActorPool s_pool;
static EmSceneState s_state;
static EmArea01Live s_area01_live;
static uint8_t module[0x9F80], original[0x9F80];
static uint32_t missing;
static unsigned suspends, resumes;

EmModuleLoader *em_module_loader_live(void)
{ return (EmModuleLoader *)(void *)module; }
const uint8_t *em_module_loader_memory(const EmModuleLoader *loader, uint32_t at, uint32_t n)
{
    assert(loader == em_module_loader_live());
    if (at < 0x00823500u || n > sizeof module || at - 0x00823500u > sizeof module - n ||
        (missing && at <= missing && missing - at < n)) return NULL;
    return module + (at - 0x00823500u);
}
int em_area01_live_suspend(EmArea01Live *live)
{
    assert(live == &s_area01_live && live->active);
    ++suspends;
    if (em_area01_actor_view_commit(&live->actors) < 0) return -1;
    live->active = 0;
    return 0;
}
int em_area01_live_resume(EmArea01Live *live)
{
    assert(live == &s_area01_live && !live->active);
    ++resumes;
    if (em_area01_actor_view_begin(&live->actors) < 0) return -1;
    live->active = 1;
    return 0;
}
static int area01_behavior(EmActor *actor, void *ctx)
{ (void)actor; (void)ctx; return 1; }
static int area01_rebind(void *ctx, EmActor *actor, uint32_t callback)
{ (void)ctx; (void)actor; (void)callback; return -1; }

#include "scene_spawn_under_test.inc"

static EmActor *prepare(void)
{
    memset(&s_pool, 0, sizeof s_pool);
    memset(&s_state, 0, sizeof s_state);
    memset(&s_area01_live, 0, sizeof s_area01_live);
    memcpy(module, original, sizeof module);
    missing = 0; suspends = resumes = 0;
    em_actor_pool_reset_001AF8E0(&s_pool);
    EmActor *parent = em_actor_pool_alloc_001AFA90(&s_pool, &s_state, 7);
    assert(parent);
    em_area01_actor_view_reset(&s_area01_live.actors, &s_pool, NULL, NULL, NULL);
    assert(em_area01_actor_view_begin(&s_area01_live.actors) == 0);
    s_area01_live.active = 1;
    uint32_t node = em_actor_pool_address(&s_pool, parent);
    float pending = 42.0f;
    uint8_t *view = em_area01_actor_view_bytes(&s_area01_live.actors, node + 0xB0, 4, 1);
    assert(view);
    memcpy(view, &pending, 4);
    return parent;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file && fread(original, 1, sizeof original, file) == sizeof original);
    assert(fgetc(file) == EOF);
    fclose(file);
    EmActor *parent = prepare();
    float changed = -19.25f;
    memcpy(module + (0x00827A30u - 0x00823500u) + 0x10, &changed, 4);
    EmArea01Call call = {.function=0x001B6660u, .na=1, .a={0x00827A30u}, .v0=0xCAFE};
    assert(area01_missing_worker(NULL, &call) == 0);
    assert(suspends == 1 && resumes == 1 && s_area01_live.active);
    assert(parent->pos[0] == 42.0f && parent->next == s_pool.tail);
    EmActor *child = s_pool.tail;
    assert(child != parent && child->callback == 0x0015B030u && child->behavior == area01_behavior);
    assert(child->source_id == 0x00827A30u && child->pos[0] == changed);
    assert(call.v0 == em_actor_pool_address(&s_pool, child));
    uint32_t node = (uint32_t)call.v0, parent_address = em_actor_pool_address(&s_pool, parent);
    uint8_t *p = em_area01_actor_view_bytes(&s_area01_live.actors, node + 0x20, 4, 1);
    assert(p); memcpy(p, &parent_address, 4);
    assert(em_area01_actor_view_touch(&s_area01_live.actors, parent_address) == 0);
    assert(em_area01_live_suspend(&s_area01_live) == 0);

    parent = prepare();
    assert(em_area01_live_suspend(&s_area01_live) == 0);
    while (em_actor_pool_alloc_001AFA90(&s_pool, &s_state, 7)) {}
    assert(em_area01_live_resume(&s_area01_live) == 0);
    call.v0 = 0xCAFE;
    assert(area01_missing_worker(NULL, &call) == 0 && call.v0 == 0);
    assert(!s_pool.free_count && parent->pos[0] == 42.0f);

    for (unsigned bad = 0; bad < 6; ++bad) {
        prepare(); call.v0 = 0xCAFE; call.na = 1; call.nf = 0; call.a[0] = 0x00827A30u;
        if (bad == 0) module[4] = 2;
        if (bad == 1) missing = 0x00827A5Cu; /* actual terminator */
        if (bad == 2) call.a[0] = 0x00826F7Fu;
        if (bad == 3) call.na = 2;
        if (bad == 4) call.nf = 1;
        if (bad == 5) s_state.d810700 = 1;
        assert(area01_missing_worker(NULL, &call) < 0 && call.v0 == 0xCAFE);
        assert(!suspends && !resumes && s_pool.free_count == EM_ACTOR_POOL_CAPACITY - 1);
    }
    puts("PASS: AREA00 direct spawner borrows delivered bytes, brackets active actor links, binds shared child, returns allocation failure and rejects invalid module/table/calls");
    return 0;
}
