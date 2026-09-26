/* em_indicator_bind_live.c - see em_indicator_bind_live.h.
 *
 * Nothing here computes: every worker names the original callee it stands
 * for and calls that callee's one translation. */
#include "game/em_indicator_bind_live.h"

#include <stdio.h>
#include <string.h>

#include "game/em_area11_boxes.h"
#include "game/em_area11_roger.h"
#include "game/em_indicator_child.h"
#include "game/em_owner_services_original.h"
#include "game/em_render_verify_rest.h"
#include "game/em_roger_actor_original.h"

typedef uint32_t u32;

#define FN_2360 0x001C2360u        /* 001C5680's bind: D_0028A56C */
#define FN_22A0 0x001C22A0u        /* 001C5760's bind: D_0028A59C */
#define BONES_MAX 4u               /* models 0x73 (3), 0x74 / 0x75 / 0x10 (1), 0x7A (4) */
#define SELF_SIZE (0x110u + 4u * BONES_MAX)

typedef struct {
    uint32_t generation;
    int live;                      /* the bind ran (slots may be held) */
    u32 fn;
    u32 model_address;             /* +0x44 */
    u32 method;                    /* +0x4C */
    EmOwnerModel model;
    EmOwnerSkeletonRecord skeleton[BONES_MAX];
    EmOwnerBone bone[BONES_MAX];   /* the typed views of its slots */
    u32 word[BONES_MAX];           /* their original slot addresses (+0x110..) */
    unsigned held;
    float world[16];               /* +0xD0 */
} Child;

static struct {
    EmActorPool *pool;
    int attached;
    u32 fault;
    u32 d0028A56C;                 /* D_0028A490[0x37] */
    Child child[EM_ACTOR_POOL_CAPACITY];
    Child *current;
    EmRogerActor stack;            /* 001AF780 / 001AF890 on the one stack */
    EmOwnerServices services;      /* 001C62C0 / 001C6380 */
    EmOwnerServicesScratch scratch;
} S;

static int fail(u32 address, const char *what)
{
    if (!S.fault) {
        S.fault = address ? address : 0x001C5680u;
        fprintf(stderr, "indicator bind: %s at %08X (stack %08X/%d, services %08X/%d)\n", what,
                (unsigned)S.fault, (unsigned)S.stack.fault.address, (int)S.stack.fault.code,
                (unsigned)S.services.fault.address, (int)S.services.fault.code);
    }
    return -1;
}

uint32_t em_indicator_bind_live_fault(void) { return S.fault; }

static u32 rd32(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

static Child *child_of(const EmActor *a)
{
    if (!S.pool || !a || a < S.pool->records || a >= S.pool->records + EM_ACTOR_POOL_CAPACITY) return NULL;
    return &S.child[a - S.pool->records];
}

int em_indicator_bind_live_attach(EmActorPool *pool)
{
    S.attached = 0;
    u32 bank = 0;
    const EmRogerActorWorld *slots = em_area11_boxes_slot_world();
    if (!pool || !slots || !slots->d00275BCC ||
        em_area11_roger_table_word(0x0028A490u + 4u * 0x37u, &bank) < 0) {
        fprintf(stderr, "indicator bind: the Roger export (D_0028A56C) or the bone-slot stack is missing\n");
        return -1;
    }
    memset(S.child, 0, sizeof S.child);
    S.pool = pool;
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

/* ---- 001C2360 / 001C22A0's workers ------------------------------------- */

/* 001C6120(bank, code): D_0028A56C over the Roger export, *D_0028A59C over
 * the world model bank. */
static int w_001C6120(void *ctx, u32 bank, u32 code, u32 *handle)
{
    (void)ctx;
    const Child *c = S.current;
    if (!c) return -1;
    return c->fn == FN_2360 ? em_area11_roger_001C6120(bank, code, handle)
                            : em_area11_boxes_world_001C6120(bank, code, handle);
}

/* The model view of a handle: the block model's +0x08 bones, +0x0C the
 * skeleton offset, +0x20 the radius, and its 0x50-byte skeleton records. */
static int model_view(Child *c, u32 handle)
{
    memset(&c->model, 0, sizeof c->model);
    if (c->fn == FN_22A0) {
        const EmOwnerModel *m = em_area11_boxes_world_model(handle);
        if (!m || m->bone_count > BONES_MAX || m->skeleton_records < m->bone_count) return -1;
        c->model = *m;
        for (u32 i = 0; i < m->bone_count; ++i) c->skeleton[i] = m->skeleton[i];
        c->model.skeleton = c->skeleton;
        return 0;
    }
    const uint8_t *m = em_area11_roger_resource(handle, 0x40);
    if (!m) return -1;
    const u32 bones = rd32(m + 8), skeleton = rd32(m + 0xC);
    if (bones > BONES_MAX) return -1;
    const uint8_t *k = bones ? em_area11_roger_resource(handle + skeleton, 0x50u * bones) : NULL;
    if (bones && !k) return -1;
    c->model.bone_count = (uint8_t)bones;
    memcpy(&c->model.radius, m + 0x20, 4);
    for (u32 i = 0; i < bones; ++i) {
        c->skeleton[i].parent = (int16_t)(uint16_t)(k[0x50 * i + 4] | k[0x50 * i + 5] << 8);
        memcpy(c->skeleton[i].bind, k + 0x50 * i + 0x10, 64);
    }
    c->model.skeleton = c->skeleton;
    c->model.skeleton_records = bones;
    return 0;
}

/* 001CA5E0(self, model, 2): +0x44 = model, then 001CA5F0(self, 2): +0x4C =
 * 001CACB0 (decomp src/func_001CA5E0.c, src/func_001CA5F0.c). */
static int w_001CA5E0(void *ctx, uint8_t *self, u32 self_address, u32 model, int32_t mode)
{
    (void)ctx;
    (void)self_address;
    Child *c = S.current;
    if (!c || mode != 2) return -1;
    self[0x44] = (uint8_t)model;
    self[0x45] = (uint8_t)(model >> 8);
    self[0x46] = (uint8_t)(model >> 16);
    self[0x47] = (uint8_t)(model >> 24);
    if (model_view(c, model) < 0) return -1;
    c->model_address = model;
    c->method = EM_INDICATOR_CHILD_DRAW_001CACB0;
    return 0;
}

/* 001C6150(model): the byte at model +0x08. */
static int w_001C6150(void *ctx, u32 value, u32 *result)
{
    (void)ctx;
    const Child *c = S.current;
    if (!c || value != c->model_address) return -1;
    *result = c->model.bone_count;
    return 0;
}

/* 001AF780: the slot at the stack cursor (0 while fewer than 31 are free). */
static int w_001AF780(void *ctx, u32 *result)
{
    (void)ctx;
    Child *c = S.current;
    u32 word = 0;
    if (!c || em_roger_actor_001AF780(&S.stack, &word) < 0) return -1;
    *result = word;
    if (!word) return 0;
    if (c->held >= BONES_MAX) return -1;
    memset(&c->bone[c->held], 0, sizeof c->bone[c->held]);
    c->word[c->held++] = word;
    return 0;
}

/* anim_bone_array_setup (001CB5B0): D_00275B40 = the node's own +0x110
 * slots, which the owner view below hands over. */
static int w_001CB5B0(void *ctx, int32_t count)
{
    (void)ctx;
    (void)count;
    return 0;
}

static void owner_view(Child *c, const EmActor *a, EmOwnerServicesOwner *o)
{
    memset(o, 0, sizeof *o);
    o->bones_held = a->bones;
    o->bone_count = a->u0A[2];
    o->model_id = a->param;
    o->model = c->model_address ? &c->model : NULL;
    memcpy(o->scale, a->f60, sizeof o->scale);
    memcpy(o->pos, a->pos, sizeof o->pos);
    memcpy(o->rot, a->rot, sizeof o->rot);
    memcpy(o->world, c->world, sizeof o->world);
    for (unsigned i = 0; i < c->held && i < BONES_MAX; ++i)
        o->bone[i] = c->word[i] ? &c->bone[i] : NULL;
}

/* bone_init_default_1 (001C62C0) over the child's model and slots. */
static int w_001C62C0(void *ctx, uint8_t *self, u32 self_address)
{
    (void)ctx;
    (void)self_address;
    Child *c = S.current;
    if (!c) return -1;
    EmOwnerServicesOwner o;
    EmActor view;
    memset(&view, 0, sizeof view);
    view.bones = self[9];
    view.u0A[2] = self[0xC];
    owner_view(c, &view, &o);
    return em_owner_services_001C62C0(&S.services, &o) < 0 ? -1 : 0;
}

int em_indicator_bind_live_bind(EmActor *child, uint32_t fn, int32_t *result)
{
    if (S.fault) return -1;
    Child *c = child_of(child);
    if (!S.attached || !c || !result || (fn != FN_2360 && fn != FN_22A0))
        return fail(fn ? fn : FN_2360, "no bindable indicator child");
    const u32 bank = fn == FN_2360 ? S.d0028A56C : em_area11_boxes_world_bank_word();
    if (!bank) return fail(fn, "the model bank is not loaded");
    if (!c->live || c->generation != child->generation) {
        memset(c, 0, sizeof *c);
        c->generation = child->generation;
    }
    c->fn = fn;
    c->held = 0;
    /* The record bytes the bind reads and writes, by original offset. */
    uint8_t self[SELF_SIZE];
    memset(self, 0, sizeof self);
    self[0x09] = child->bones;
    self[0x0C] = child->u0A[2];
    self[0x0D] = child->param;
    const EmRvrModelWorkers w = {NULL, w_001C6120, w_001CA5E0, w_001C6150, w_001AF780, w_001CB5B0,
                                 w_001C62C0};
    EmRvrFault fault = {0, 0};
    S.current = c;
    const u32 address = em_actor_pool_address(S.pool, child);
    int rc = fn == FN_2360
                 ? em_rvr_001C2360(self, SELF_SIZE, address, bank, *S.stack.world.d00275BCC, &w, result,
                                   &fault)
                 : em_rvr_001C22A0(self, SELF_SIZE, address, bank, *S.stack.world.d00275BCC, &w, result,
                                   &fault);
    S.current = NULL;
    child->bones = self[0x09];
    child->u0A[2] = self[0x0C];
    c->live = 1;
    if (rc < 0) return fail(fault.address, "001C2360 / 001C22A0 faulted");
    for (unsigned i = 0; i < c->held; ++i)
        if (rd32(self + 0x110 + 4 * i) != c->word[i]) return fail(fn, "a slot word differs from its pop");
    return 0;
}

int em_indicator_bind_live_place(EmActor *child)
{
    if (S.fault) return -1;
    Child *c = child_of(child);
    if (!c || !c->live || c->generation != child->generation || !c->model_address)
        return fail(0x001C6380u, "001C6380 on an indicator child that is not bound");
    EmOwnerServicesOwner o;
    owner_view(c, child, &o);
    if (em_owner_services_001C6380(&S.services, &o) < 0) return fail(0x001C6380u, "001C6380 faulted");
    memcpy(c->world, o.world, sizeof c->world);
    return 0;
}

int em_indicator_bind_live_001AF800(EmActor *child)
{
    Child *c = child_of(child);
    if (!S.attached || !c || !c->live || c->generation != child->generation) return 0;
    if (S.fault) return -1;
    for (unsigned k = 0; k < child->bones && k < c->held; ++k)
        if (em_roger_actor_001AF890(&S.stack, c->word[k]) < 0) return fail(0x001AF890u, "001AF890 faulted");
    memset(c, 0, sizeof *c);
    return 1;
}

int em_indicator_bind_live_record(const EmActor *child, EmIndicatorBindRecord *out)
{
    const Child *c = child_of(child);
    if (!c || !out || !c->live || c->generation != child->generation) return 0;
    memset(out, 0, sizeof *out);
    out->address = em_actor_pool_address(S.pool, child);
    out->bones = child->bones;
    out->count = child->u0A[2];
    out->model = c->model_address;
    out->method = c->method;
    for (unsigned i = 0; i < c->held && i < child->bones && i < 4; ++i) out->slot[i] = c->word[i];
    if (c->held) memcpy(out->world, c->bone[0].world, sizeof out->world);
    return 1;
}
