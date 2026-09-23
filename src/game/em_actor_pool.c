/* Native actor pool; see em_actor_pool.h for the original functions and the
 * deliberate (fail-stop) differences. */
#include "game/em_actor_pool.h"

#include <string.h>

static int pool_index(const EmActorPool *pool, const EmActor *actor)
{
    if (!pool || !actor || actor < pool->records || actor >= pool->records + EM_ACTOR_POOL_CAPACITY)
        return -1;
    return (int)(actor - pool->records);
}

uint32_t em_actor_pool_address(const EmActorPool *pool, const EmActor *actor)
{
    int index = pool_index(pool, actor);
    return index < 0 ? 0u : EM_ACTOR_POOL_BASE + (uint32_t)index * EM_ACTOR_RECORD_SIZE;
}

void em_actor_pool_globals(const EmActorPool *pool, EmActorPoolGlobals *out)
{
    out->head = em_actor_pool_address(pool, pool->head);
    out->tail = em_actor_pool_address(pool, pool->tail);
    out->free_head = em_actor_pool_address(pool, pool->free_head);
    out->free_count = pool->free_count;
    out->current = em_actor_pool_address(pool, pool->current);
}

static void put8(uint8_t *out, unsigned offset, uint8_t value) { out[offset] = value; }

static void put16(uint8_t *out, unsigned offset, uint16_t value)
{
    out[offset] = (uint8_t)value;
    out[offset + 1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *out, unsigned offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        out[offset + i] = (uint8_t)(value >> (8 * i));
}

static void putf(uint8_t *out, unsigned offset, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    put32(out, offset, bits);
}

void em_actor_pool_record_image(const EmActorPool *pool, const EmActor *a,
                                uint8_t out[EM_ACTOR_RECORD_SIZE])
{
    memset(out, 0, EM_ACTOR_RECORD_SIZE);
    put8(out, 0x00, a->status);
    put8(out, 0x01, a->drawn);
    put8(out, 0x02, a->cls);
    put8(out, 0x03, a->model);
    memcpy(out + 0x04, a->u04, sizeof a->u04);
    put8(out, 0x09, a->bones);
    memcpy(out + 0x0A, a->u0A, sizeof a->u0A);
    put8(out, 0x0D, a->param);
    put16(out, 0x0E, a->uid);
    put32(out, 0x10, a->callback);
    put32(out, 0x14, em_actor_pool_address(pool, a->self));
    put32(out, 0x18, em_actor_pool_address(pool, a->prev));
    put32(out, 0x1C, em_actor_pool_address(pool, a->next));
    put32(out, 0x30, a->w30);
    put16(out, 0x36, a->h36);
    put16(out, 0x52, a->h52);
    put16(out, 0x54, a->kind);
    put16(out, 0x56, a->link);
    put32(out, 0x58, a->w58);
    put32(out, 0x5C, a->w5C);
    for (unsigned i = 0; i < 8; ++i)
        putf(out, 0x60 + 4 * i, a->f60[i]);
    for (unsigned i = 0; i < 4; ++i)
        putf(out, 0x80 + 4 * i, a->f80[i]);
    put32(out, 0x90, a->w90);
    put16(out, 0x94, (uint16_t)a->h94);
    put16(out, 0x96, (uint16_t)a->h96);
    put8(out, 0x98, a->b98);
    put8(out, 0x99, a->b99);
    put8(out, 0x9A, a->table_index);
    put8(out, 0x9C, a->b9C);
    put8(out, 0x9D, a->b9D);
    put8(out, 0x9E, a->b9E);
    for (unsigned i = 0; i < 4; ++i) {
        putf(out, 0xB0 + 4 * i, a->pos[i]);
        putf(out, 0xC0 + 4 * i, a->rot[i]);
    }
    memcpy(out + EM_ACTOR_SCRATCH_OFFSET, a->scratch, EM_ACTOR_SCRATCH_SIZE);
}

void em_actor_pool_reset_001AF8E0(EmActorPool *pool)
{
    /* Native hook first: the original memset below discards live records. */
    for (int i = 0; i < EM_ACTOR_POOL_CAPACITY; ++i) {
        EmActor *a = &pool->records[i];
        if (a->allocated && a->release)
            a->release(a);
    }
    /* 001AF8E0: D_00275BC0 = D_00275BBC = 0; each record memset(0, 0x2F0)
     * and +0x1C = next record; the last +0x1C = 0 (D_007A536C + 0x100*0x2F0). */
    pool->head = NULL;
    pool->tail = NULL;
    for (int i = 0; i < EM_ACTOR_POOL_CAPACITY; ++i) {
        EmActor *a = &pool->records[i];
        uint32_t generation = a->generation + 1u;
        memset(a, 0, sizeof *a);
        a->generation = generation;
        a->next = i + 1 < EM_ACTOR_POOL_CAPACITY ? &pool->records[i + 1] : NULL;
    }
    /* D_00275BC4 = D_007A5640, D_00275BC8 = 0x100. */
    pool->free_head = &pool->records[0];
    pool->free_count = EM_ACTOR_POOL_CAPACITY;
}

void em_actor_pool_link_001AFA50(EmActorPool *pool, EmActor *actor)
{
    /* 001AFA50: prev = tail, next = 0; tail ? tail->next : head = actor; tail = actor. */
    actor->prev = pool->tail;
    actor->next = NULL;
    if (!pool->tail)
        pool->head = actor;
    else
        pool->tail->next = actor;
    pool->tail = actor;
}

void em_actor_pool_unlink_001AFBC0(EmActorPool *pool, EmActor *actor)
{
    /* 001AFBC0 */
    if (!actor->prev)
        pool->head = actor->next;
    else
        actor->prev->next = actor->next;
    if (actor->next)
        actor->next->prev = actor->prev;
    else
        pool->tail = actor->prev;
}

EmActor *em_actor_pool_alloc_001AFA90(EmActorPool *pool, const EmSceneState *scene, uint8_t cls)
{
    uint8_t c = (uint8_t)(cls & EM_ACTOR_CLASS_MASK); /* 001AFA90: cls & ~0xE0 */
    if (c == EM_ACTOR_CLASS_RESERVED && pool->free_count < EM_ACTOR_CLASS_RESERVE)
        return NULL;
    EmActor *self = pool->free_head;
    if (!self)
        return NULL;
    pool->free_count = (int16_t)(pool->free_count - 1);
    pool->free_head = self->next;
    self->self = self;
    self->status = 2;
    self->cls = cls;
    for (int i = 0; i < 4; ++i) {
        self->f60[i] = 1.0f; /* +0x60..+0x6C = 0x3F800000 */
        self->f80[i] = 1.0f; /* +0x80..+0x8C = 0x3F800000 */
        self->pos[i] = 0.0f; /* +0xB0..+0xB8 = 0 */
    }
    self->pos[3] = 1.0f;  /* +0xBC */
    self->f60[4] = 0.0f;  /* +0x70 */
    self->f60[5] = 0.0f;  /* +0x74 */
    self->f60[6] = 1.0f;  /* +0x78 */
    self->f60[7] = 1.0f;  /* +0x7C */
    self->table_index = 0; /* +0x9A */
    self->b99 = 0;
    self->h94 = -1;
    self->h96 = 0;
    em_actor_pool_link_001AFA50(pool, self);
    self->w30 = 0;
    self->kind = 0;  /* +0x54 */
    self->link = 0;  /* +0x56 */
    self->h52 = 0;
    self->w58 = 0;
    self->w5C = 0x00010101u;
    self->b9C = 0;
    if (c == 2) {
        self->b9D = scene->d810701;
        self->b9E = scene->d810702;
    }
    /* Native: unbound until the spawner binds it. */
    self->behavior = NULL;
    self->release = NULL;
    self->owner = NULL;
    self->source_id = 0;
    self->allocated = 1;
    return self;
}

int em_actor_pool_free_001AFC10(EmActorPool *pool, EmSceneState *scene, EmActor *handle)
{
    if (em_scene_faulted(scene))
        return -1;
    if (pool_index(pool, handle) < 0)
        return em_scene_fault(scene, EM_ACTOR_FN_001AFC10, EM_SCENE_FAULT_BAD_INDEX);
    /* 001AFC10: the canonical record is +0x14; the handle's +0x14 is cleared. */
    EmActor *self = handle->self;
    handle->self = NULL;
    if (pool_index(pool, self) < 0) /* original: +0x14 == 0 dereferences address 0 */
        return em_scene_fault(scene, EM_ACTOR_FN_001AFC10, EM_SCENE_FAULT_BAD_INDEX);
    /* 001AF800: bone slots. With +9 == 0 its loop is empty and its own writes
     * (+9 = 0, +0xC = 0, D_00275BCC += 0) are covered by the clears below. */
    if (self->bones != 0) {
        if (!pool->w_001AF800)
            return em_scene_fault(scene, EM_ACTOR_FN_001AF800, EM_SCENE_FAULT_NULL_WORKER);
        if (pool->w_001AF800(pool->worker_ctx, self) < 0)
            return em_scene_fault(scene, EM_ACTOR_FN_001AF800, EM_SCENE_FAULT_WORKER_FAILED);
        self->bones = 0;
        self->u0A[2] = 0; /* +0x0C */
    }
    em_actor_pool_unlink_001AFBC0(pool, self);
    pool->free_count = (int16_t)(pool->free_count + 1);
    self->next = pool->free_head;
    pool->free_head = self;
    /* +0x00..+0x0F words, +0x36 h, +0x98 b, +0x90 w, +0x1F0..+0x2EF. */
    self->status = self->drawn = self->cls = self->model = 0;
    memset(self->u04, 0, sizeof self->u04);
    self->bones = 0;
    memset(self->u0A, 0, sizeof self->u0A);
    self->param = 0;
    self->uid = 0;
    self->h36 = 0;
    self->b98 = 0;
    self->w90 = 0;
    memset(self->scratch, 0, sizeof self->scratch);
    /* Native bookkeeping. */
    self->behavior = NULL;
    self->release = NULL;
    self->owner = NULL;
    self->allocated = 0;
    self->generation++;
    return 0;
}

int em_actor_pool_walk_001AFD70(EmActorPool *pool, EmSceneState *scene, int mode, void *world,
                                EmActorTraceFn trace, void *trace_ctx)
{
    if (em_scene_faulted(scene))
        return -1;
    scene->spad3B8A = 0;
    EmActor *cur = pool->head;
    while (cur) {
        /* The saved `next` is read before the counter and the class test. */
        EmActor *next = cur->next;
        uint32_t next_generation = next ? next->generation : 0;
        scene->spad3B8A = (uint16_t)(scene->spad3B8A + 1u);
        uint8_t c = (uint8_t)(cur->cls & EM_ACTOR_CLASS_MASK);
        if ((mode == 1 && c == 1) || (mode == 2 && c != 1)) {
            cur = next;
            continue;
        }
        /* 001CB590(cur, 0x2F0, +9): D_00275B44 = D_00275B48 = cur. The walk
         * loads D_00275B44 before the jalr, so the original link copy after
         * the call is a self-copy and is omitted. */
        pool->current = cur;
        uint32_t address = em_actor_pool_address(pool, cur);
        if (trace)
            trace(trace_ctx, EM_ACTOR_FN_001AFD70, EM_ACTOR_FN_001CB590, address, cur);
        cur->drawn = 0;
        if (!cur->behavior)
            return em_scene_fault(scene, cur->callback, EM_SCENE_FAULT_NULL_WORKER);
        if (trace)
            trace(trace_ctx, EM_ACTOR_FN_001AFD70, cur->callback, address, cur);
        int result = cur->behavior(cur, world);
        if (em_scene_faulted(scene))
            return -1;
        if (result < 0)
            return em_scene_fault(scene, cur->callback, EM_SCENE_FAULT_WORKER_FAILED);
        if (result != 1)
            return em_scene_fault(scene, cur->callback, EM_SCENE_FAULT_BAD_RESULT);
        /* Policy (design 4.1): the original would continue into a freed next. */
        if (next && next->generation != next_generation)
            return em_scene_fault(scene, cur->callback, EM_SCENE_FAULT_FREED_NEXT);
        cur = next;
    }
    return 0;
}
