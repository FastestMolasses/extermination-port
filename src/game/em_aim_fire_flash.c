/* em_aim_fire_flash.c - the muzzle node's record and model-node workers
 * (see em_aim_fire_flash.h, docs/AIM_FIRE.md section 9.1).
 *
 * Nothing here computes: every worker names the original callee it stands
 * for and calls that callee's one translation. */
#include "game/em_aim_fire_flash.h"

#include <stdio.h>
#include <string.h>

#include "game/em_aim_fire_binding.h"
#include "game/em_anim_runtime_rest.h"
#include "game/em_area11_boxes.h"
#include "game/em_area11_roger.h"
#include "game/em_effect_original.h"
#include "game/em_owner_draw_live.h"
#include "game/em_owner_services_original.h"
#include "game/em_point_light.h"
#include "game/em_render_context_live.h"
#include "game/em_roger_actor_original.h"

typedef uint32_t u32;

#define BONES_MAX 1u                 /* the library models 0x07..0x0F hold one bone */
#define FN_001CACB0 0x001CACB0u      /* 001CA5F0 kind 2 */
#define D_0028A56C_INDEX 0x37u

typedef struct {
    uint32_t generation;
    int live;
    uint8_t b28[2], w28[2];          /* +0x28 */
    u32 model;                       /* +0x44 */
    u32 method;                      /* +0x4C */
    uint8_t has_model;
    uint8_t mD0[64], wD0[64];        /* +0xD0..+0x10F */
    uint8_t s230[12], w230[12];      /* +0x230..+0x23B */
    EmOwnerBone bone[BONES_MAX];     /* the typed views of its slots */
    u32 word[BONES_MAX];             /* +0x110: their original slot addresses */
    unsigned held;
    const EmOwnerModel *view;        /* the model's owner-services view */
} Flash;

static struct {
    EmActorPool *pool;
    EmSceneState *scene;
    int attached;
    u32 fault;
    u32 d0028A56C;
    Flash rec[EM_ACTOR_POOL_CAPACITY];
    Flash *current;
    EmRogerActor stack;              /* 001AF780 / 001AF800 on the one stack */
    EmOwnerServices services;        /* 001C6150 / 001C62C0 / 001C9610 */
    EmOwnerServicesScratch scratch;
    EmWorldModels models;            /* the bound library models, by address */
    EmOwnerServicesOwner draw_view;
} S;

static int fail(u32 address, const char *what)
{
    if (!S.fault) {
        S.fault = address ? address : EM_AIM_FIRE_FLASH_CALLBACK;
        fprintf(stderr, "muzzle node: %s at %08X (stack %08X/%d, services %08X/%d)\n", what, (unsigned)S.fault,
                (unsigned)S.stack.fault.address, (int)S.stack.fault.code, (unsigned)S.services.fault.address,
                (int)S.services.fault.code);
    }
    return -1;
}

uint32_t em_aim_fire_flash_fault(void) { return S.fault; }

static u32 rd32(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

static int index_of(const EmActor *a)
{
    if (!S.pool || !a || a < S.pool->records || a >= S.pool->records + EM_ACTOR_POOL_CAPACITY) return -1;
    return (int)(a - S.pool->records);
}

static Flash *flash_of(const EmActor *a)
{
    const int i = index_of(a);
    if (i < 0 || !a->allocated) return NULL;
    Flash *f = &S.rec[i];
    return f->live && f->generation == a->generation ? f : NULL;
}

int em_aim_fire_flash_attach(EmActorPool *pool, EmSceneState *scene)
{
    S.attached = 0;
    u32 bank = 0;
    const EmRogerActorWorld *slots = em_area11_boxes_slot_world();
    if (!pool || !scene || !slots || !slots->d00275BCC ||
        em_area11_roger_table_word(0x0028A490u + 4u * D_0028A56C_INDEX, &bank) < 0) {
        fprintf(stderr, "muzzle node: the Roger export (D_0028A56C) or the bone-slot stack is missing\n");
        return -1;
    }
    memset(S.rec, 0, sizeof S.rec);
    memset(&S.models, 0, sizeof S.models);
    S.pool = pool;
    S.scene = scene;
    S.fault = 0;
    S.current = NULL;
    S.d0028A56C = bank;
    memset(&S.stack, 0, sizeof S.stack);
    S.stack.world = *slots;
    memset(&S.services, 0, sizeof S.services);
    S.services.world.scratch = &S.scratch;
    S.attached = 1;
    return 0;
}

int em_aim_fire_flash_claim(EmActor *actor)
{
    const int i = index_of(actor);
    if (!S.attached || S.fault || i < 0 || !actor->allocated) return fail(0x001F4F40u, "no claimable record");
    Flash *f = &S.rec[i];
    memset(f, 0, sizeof *f);
    f->generation = actor->generation;
    f->live = 1;
    return 0;
}

int em_aim_fire_flash_owns(const EmActor *actor) { return S.attached && flash_of(actor) != NULL; }

void em_aim_fire_flash_set_current(EmActor *actor) { S.current = actor ? flash_of(actor) : NULL; }

static void *span(u32 address, size_t size, u32 base, size_t count, void *bytes)
{
    return bytes && size && address >= base && size <= count && (uint64_t)address + size <= (uint64_t)base + count
               ? (uint8_t *)bytes + (address - base)
               : NULL;
}

/* A masked byte range: a write marks it, a read needs every byte marked. */
static void *masked(u32 address, size_t size, u32 base, size_t count, uint8_t *bytes, uint8_t *mask, int write)
{
    uint8_t *p = span(address, size, base, count, bytes);
    if (!p) return NULL;
    uint8_t *m = mask + (p - bytes);
    if (write) {
        memset(m, 1, size);
        return p;
    }
    for (size_t k = 0; k < size; ++k)
        if (!m[k]) return NULL;
    return p;
}

void *em_aim_fire_flash_field(uint32_t address, size_t size, int write)
{
    if (!S.attached || S.fault || !size || size > EM_ACTOR_RECORD_SIZE) return NULL;
    for (unsigned i = 0; i < EM_ACTOR_POOL_CAPACITY; ++i) {
        Flash *f = &S.rec[i];
        if (!f->live || !flash_of(&S.pool->records[i])) continue;
        for (unsigned k = 0; k < f->held; ++k) {
            void *p = span(address, size, f->word[k] + 0x90u, 64, f->bone[k].world);
            if (p) return p;
        }
    }
    if (address < EM_ACTOR_POOL_BASE) return NULL;
    const unsigned index = (address - EM_ACTOR_POOL_BASE) / EM_ACTOR_RECORD_SIZE;
    if (index >= EM_ACTOR_POOL_CAPACITY) return NULL;
    Flash *f = flash_of(&S.pool->records[index]);
    if (!f) return NULL;
    const u32 base = EM_ACTOR_POOL_BASE + index * EM_ACTOR_RECORD_SIZE;
    void *p;
    if ((p = masked(address, size, base + 0x28u, 2, f->b28, f->w28, write))) return p;
    if ((p = masked(address, size, base + 0xD0u, 64, f->mD0, f->wD0, write))) return p;
    if ((p = masked(address, size, base + 0x230u, 12, f->s230, f->w230, write))) return p;
    if (!write && f->has_model) {
        if ((p = span(address, size, base + 0x44u, 4, &f->model))) return p;
        if ((p = span(address, size, base + 0x4Cu, 4, &f->method))) return p;
    }
    if (!write && f->held && (p = span(address, size, base + 0x110u, 4u * f->held, f->word))) return p;
    return NULL;
}

/* The longest written runs of one masked range. */
static size_t runs(u32 base, uint8_t *bytes, const uint8_t *mask, size_t count, EmPoseRegion *out,
                   size_t capacity, size_t n, int writable)
{
    for (size_t k = 0; k < count;) {
        if (!mask[k]) {
            ++k;
            continue;
        }
        size_t e = k;
        while (e < count && mask[e]) ++e;
        if (out && n < capacity) out[n] = (EmPoseRegion){base + (u32)k, (u32)(e - k), bytes + k, writable};
        ++n;
        k = e;
    }
    return n;
}

size_t em_aim_fire_flash_regions(uint32_t address, EmPoseRegion *out, size_t capacity)
{
    if (!S.attached || address < EM_ACTOR_POOL_BASE || (address - EM_ACTOR_POOL_BASE) % EM_ACTOR_RECORD_SIZE)
        return 0;
    const unsigned index = (address - EM_ACTOR_POOL_BASE) / EM_ACTOR_RECORD_SIZE;
    if (index >= EM_ACTOR_POOL_CAPACITY) return 0;
    Flash *f = flash_of(&S.pool->records[index]);
    if (!f) return 0;
    /* A region-only owner (em_area00_fx_001F5040 through em_aim_fire_world_live)
     * reads and writes through these views without a per-byte check: the
     * written runs are readable, and the whole of each range is writable
     * as one view once the node's behaviour runs (its state 0 writes +0x28,
     * +0x230..+0x238 before any read; +0xD0..+0x10F is 00187CC0's). */
    size_t n = 0;
    if (S.current == f) {
        const struct { u32 at, size; uint8_t *bytes, *mask; } r[3] = {
            {0x28u, 2, f->b28, f->w28}, {0xD0u, 64, f->mD0, f->wD0}, {0x230u, 12, f->s230, f->w230}};
        for (unsigned k = 0; k < 3; ++k) {
            if (out && n < capacity) out[n] = (EmPoseRegion){address + r[k].at, r[k].size, r[k].bytes, 1};
            ++n;
        }
    } else {
        n = runs(address + 0x28u, f->b28, f->w28, 2, out, capacity, n, 1);
        n = runs(address + 0xD0u, f->mD0, f->wD0, 64, out, capacity, n, 1);
        n = runs(address + 0x230u, f->s230, f->w230, 12, out, capacity, n, 1);
    }
    if (f->has_model) {
        if (out && n < capacity) out[n] = (EmPoseRegion){address + 0x44u, 4, (uint8_t *)&f->model, 0};
        ++n;
        if (out && n < capacity) out[n] = (EmPoseRegion){address + 0x4Cu, 4, (uint8_t *)&f->method, 0};
        ++n;
    }
    if (f->held) {
        if (out && n < capacity) out[n] = (EmPoseRegion){address + 0x110u, 4u * f->held, (uint8_t *)f->word, 0};
        ++n;
        for (unsigned k = 0; k < f->held; ++k) {
            if (out && n < capacity)
                out[n] = (EmPoseRegion){f->word[k] + 0x90u, 64, (uint8_t *)f->bone[k].world, 1};
            ++n;
        }
    }
    return n;
}

/* ---- the workers -------------------------------------------------------- */

static Flash *node_arg(uint64_t a, EmActor **actor)
{
    const u32 address = (u32)a;
    if (address < EM_ACTOR_POOL_BASE || (address - EM_ACTOR_POOL_BASE) % EM_ACTOR_RECORD_SIZE) return NULL;
    const unsigned index = (address - EM_ACTOR_POOL_BASE) / EM_ACTOR_RECORD_SIZE;
    if (index >= EM_ACTOR_POOL_CAPACITY) return NULL;
    if (actor) *actor = &S.pool->records[index];
    return flash_of(&S.pool->records[index]);
}

/* The owner-services view of a node: its header bytes, model, slots and
 * +0xB0 / +0xD0 (each read only once written). */
static int owner_view(Flash *f, const EmActor *a, EmOwnerServicesOwner *o, int need_world)
{
    memset(o, 0, sizeof *o);
    o->drawn = a->drawn;
    o->cls = a->cls;
    o->kind = a->model;
    o->lifecycle = a->u04[0];
    o->bones_held = a->bones;
    o->bone_count = a->u0A[2];
    o->model_id = a->param;
    o->flags2 = a->flags2;
    o->model = f->has_model ? f->view : NULL;
    memcpy(o->scale, a->f60, sizeof o->scale);
    memcpy(o->pos, a->pos, sizeof o->pos);
    memcpy(o->rot, a->rot, sizeof o->rot);
    o->attachment = a->w90;
    o->collapsed_bone = (int16_t)a->h94;
    o->pose_bone = a->b98;
    if (need_world) {
        for (unsigned k = 0; k < 64; ++k)
            if (!f->wD0[k]) return -1;
        memcpy(o->world, f->mD0, sizeof o->world);
    }
    for (unsigned k = 0; k < f->held && k < EM_OWNER_SERVICES_MAX_BONES; ++k)
        o->bone[k] = f->word[k] ? &f->bone[k] : NULL;
    return 0;
}

static int w_001D7FA0(void *ctx, const float pos[4], const float color[4], int32_t type, float fa, float fb)
{
    (void)ctx;
    EmPointLightPool *pool = em_rcl_point_lights();   /* the context's +0x210.. */
    if (!pool) return -1;
    (void)em_point_light_register(pool, pos, color, type, fa, fb);
    return 0;
}

/* 001CABA0(node, model) from 001CACB0: em_owner_draw_live_001CABA0 over the
 * node's owner view (`ctx` the node's pool record). */
static int w_001CABA0(void *ctx, uint32_t owner, uint32_t model)
{
    EmActor *a = ctx;
    Flash *f = a ? flash_of(a) : NULL;
    if (!f || owner != em_actor_pool_address(S.pool, a) || model != f->model)
        return fail(0x001CABA0u, "001CABA0 of another record");
    EmOwnerServicesOwner *o = &S.draw_view;
    if (owner_view(f, a, o, 0) < 0) return fail(FN_001CACB0, "no owner view");
    u32 rgb[4];
    memcpy(rgb, a->f80, sizeof rgb);
    if (em_owner_draw_live_001CABA0(&S.models, o, rgb, owner) < 0)
        return fail(0x001CABA0u, "001CABA0 faulted (the muzzle node)");
    return 0;
}

static const uint8_t *bytes_of(uint64_t address, u32 size)
{
    return em_aim_fire_binding_bytes((u32)address, size, 0);
}

int em_aim_fire_flash_call(EmAimFireTargetCall *c)
{
    switch (c->function) {
    case 0x001C6120u: case 0x001CA5E0u: case 0x001C6150u: case 0x001AF780u: case 0x001CB5B0u:
    case 0x001C62C0u: case 0x001C9610u: case 0x001D80E0u: case FN_001CACB0:
        break;
    default:
        return 0;
    }
    /* Another owner's tick (the knife's trail node) pops its own slots. */
    if ((c->function == 0x001AF780u || c->function == 0x001CB5B0u) && !S.current) return 0;
    if (!S.attached || S.fault) return -1;
    EmActor *a = NULL;
    switch (c->function) {
    case 0x001C6120u: {
        /* 001C6120(bank, id): only the library D_0028A56C is exported. */
        u32 handle = 0;
        if ((u32)c->a[0] != S.d0028A56C) return fail(0x001C6120u, "a bank other than D_0028A56C");
        if (em_area11_roger_001C6120((u32)c->a[0], (u32)c->a[1], &handle) < 0)
            return fail(0x001C6120u, "the library model is not in the Roger export");
        c->v0 = handle;
        return 1;
    }
    case 0x001CA5E0u: {
        /* 001CA5E0(node, model, 2): +0x44 = model, 001CA5F0(node, 2): +0x4C
         * = 001CACB0. The model (header, blocks, skeleton records) joins
         * this module's bank at its address. */
        Flash *f = node_arg(c->a[0], &a);
        const u32 handle = (u32)c->a[1];
        if (!f || (u32)c->a[2] != 2u) return fail(0x001CA5E0u, "not a muzzle node, or a kind other than 2");
        const uint8_t *m = em_area11_roger_resource(handle, 0x40);
        if (!m) return fail(0x001CA5E0u, "the model is not in the Roger export");
        const u32 bones = rd32(m + 8), skeleton = rd32(m + 0xC);
        if (bones == 0 || bones > BONES_MAX || skeleton > 0x01000000u)
            return fail(0x001CA5E0u, "a model with more bones than a muzzle node holds");
        const u32 size = skeleton + 0x50u * bones;
        const uint8_t *all = em_area11_roger_resource(handle, size);
        const EmWorldModel *entry = NULL;
        if (!all || em_world_models_add(&S.models, handle, all, size, &entry) < 0)
            return fail(0x001CA5E0u, "the model does not parse");
        f->model = handle;
        f->method = FN_001CACB0;
        f->view = &entry->model;
        f->has_model = 1;
        return 1;
    }
    case 0x001C6150u: {
        const EmWorldModel *m = em_world_models_at(&S.models, (u32)c->a[0]);
        uint8_t count = 0;
        if (!m || em_owner_services_001C6150(&S.services, &m->model, &count) < 0)
            return fail(0x001C6150u, "001C6150 of a model no muzzle node bound");
        c->v0 = count;
        return 1;
    }
    case 0x001AF780u: {
        /* The slot at the stack cursor (0 while fewer than 31 are free),
         * popped for the node whose behaviour runs. */
        Flash *f = S.current;
        u32 word = 0;
        if (!f || em_roger_actor_001AF780(&S.stack, &word) < 0) return fail(0x001AF780u, "001AF780 outside a tick");
        c->v0 = word;
        if (!word) return 1;
        if (f->held >= BONES_MAX) return fail(0x001AF780u, "more slots than the model's bones");
        memset(&f->bone[f->held], 0, sizeof f->bone[f->held]);
        f->word[f->held++] = word;
        return 1;
    }
    case 0x001CB5B0u:
        /* anim_bone_array_setup: D_00275B40 = the node's own +0x110 slots,
         * which the tick already points it at. */
        if (!S.current) return fail(0x001CB5B0u, "001CB5B0 outside a tick");
        return 1;
    case 0x001C62C0u: {
        Flash *f = node_arg(c->a[0], &a);
        EmOwnerServicesOwner o;
        if (!f || owner_view(f, a, &o, 0) < 0) return fail(c->function, "not a muzzle node");
        return em_owner_services_001C62C0(&S.services, &o) < 0 ? fail(0x001C62C0u, "001C62C0 faulted") : 1;
    }
    case 0x001C9610u: {
        /* 001C9610(slots, count, matrix), from 001C63D0's tail call
         * (em_area00_world_001C63D0): the node's own +0x110 slots and its
         * +0xD0 matrix. */
        const u32 slots = (u32)c->a[0];
        Flash *f = node_arg(slots - 0x110u, &a);
        EmOwnerServicesOwner o;
        if (!f || (u32)c->a[2] != slots - 0x110u + 0xD0u) return fail(0x001C9610u, "not a muzzle node's slots");
        if (owner_view(f, a, &o, 1) < 0) return fail(0x001C9610u, "the node matrix +0xD0 was not written");
        const int32_t count = (int32_t)c->a[1];
        if (count < 0 || (unsigned)count > f->held) return fail(0x001C9610u, "a count past the node's slots");
        return em_owner_services_001C9610(&S.services, o.bone, count, o.world) < 0
                   ? fail(0x001C9610u, "001C9610 faulted")
                   : 1;
    }
    case 0x001D80E0u: {
        const uint8_t *p = bytes_of(c->a[0], 16), *q = bytes_of(c->a[1], 16);
        float pos[4], color[4];
        if (!p || !q) return fail(0x001D80E0u, "an unreadable vector");
        memcpy(pos, p, 16);
        memcpy(color, q, 16);
        const EmEffectOriginalWorkers w = {.w_001D7FA0 = w_001D7FA0};
        EmEffectOriginal e;
        memset(&e, 0, sizeof e);
        e.workers = &w;
        return em_effect_original_001D80E0(&e, pos, color) < 0 ? fail(0x001D7FA0u, "001D80E0 faulted") : 1;
    }
    case FN_001CACB0: {
        /* 001CACB0(node): em_anim_rest_001CACB0, its one translation (the
         * tail call 001CABA0(node, +0x44)), over the node's +0x44 word; its
         * 001CABA0 is em_owner_draw_live's (w_001CABA0 above). */
        Flash *f = node_arg(c->a[0], &a);
        if (!f || !f->has_model || f->method != FN_001CACB0) return fail(FN_001CACB0, "not a bound muzzle node");
        EmAnimRest r;
        memset(&r, 0, sizeof r);
        r.world.region[0] = (EmPoseRegion){(u32)c->a[0] + 0x44u, 4, (uint8_t *)&f->model, 0};
        r.world.region_count = 1;
        r.workers.ctx = a;
        r.workers.w_001CABA0 = w_001CABA0;
        if (em_anim_rest_001CACB0(&r, (u32)c->a[0]) < 0)
            return S.fault ? -1 : fail(r.fault.address ? r.fault.address : FN_001CACB0, "001CACB0 faulted");
        return 1;
    }
    default:
        return 0;
    }
}

int em_aim_fire_flash_h28(const EmActor *actor, uint16_t *value)
{
    const Flash *f = S.attached ? flash_of(actor) : NULL;
    if (!f || !value) return 0;
    *value = (uint16_t)(f->b28[0] | f->b28[1] << 8);
    return 1;
}

int em_aim_fire_flash_001AF800(EmActor *actor)
{
    Flash *f = S.attached ? flash_of(actor) : NULL;
    if (!f) return 0;
    if (S.fault) return -1;
    /* 001AF800 (em_roger_actor_001AF800, its own slot loop) over the node's
     * +0x09, +0x0C and the +0x110 words it popped. */
    if (actor->bones > f->held) return fail(0x001AF800u, "001AF800: +0x09 names a slot the node did not pop");
    EmRogerActorRecord v;
    memset(&v, 0, sizeof v);
    v.address = em_actor_pool_address(S.pool, actor);
    v.bones_held = actor->bones;
    v.bone_count = actor->u0A[2];
    for (unsigned k = 0; k < f->held; ++k) v.bone[k] = f->word[k];
    if (em_roger_actor_001AF800(&S.stack, &v) < 0) return fail(0x001AF800u, "001AF800 faulted");
    actor->bones = v.bones_held;
    actor->u0A[2] = v.bone_count;
    memset(f, 0, sizeof *f);
    return 1;
}
