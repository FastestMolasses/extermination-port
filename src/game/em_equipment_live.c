/* em_equipment_live.c - the player's equipment nodes bound live (see
 * em_equipment_live.h, docs/PLAYER_EQUIPMENT.md section 8).
 *
 * Nothing here computes: every worker names the original callee it stands
 * for and calls that callee's one translation. */
#include "game/em_equipment_live.h"

#include "game/em_area11_boxes.h"
#include "game/em_area11_roger.h"
#include "game/em_ee_float.h"
#include "game/em_effect_original.h"
#include "game/em_effects_live.h"
#include "game/em_locomotion_display.h"
#include "game/em_owner_services_original.h"
#include "game/em_player.h"
#include "game/em_player_equipment.h"
#include "game/em_player_hang.h"
#include "game/em_pose_host_workers.h"
#include "game/em_roger_actor_original.h"

#include <stdio.h>
#include <string.h>

typedef uint32_t u32;

#define PLAYER 0x008102B0u
#define PLAYER_NODES 21u
#define TABLE_WORDS 0xA4u            /* D_0024A220..D_0024A4AF */
#define MODEL_BONES_MAX 4u           /* the equipment models hold 1..3 bones */
#define METHOD_001CAA00 0x001CAA00u

typedef struct {
    uint32_t generation;
    int live;
    EmPlayerEquipmentNode n;
    EmOwnerBone bone[MODEL_BONES_MAX];     /* the typed views of its slots */
    uint32_t word[MODEL_BONES_MAX];        /* their original slot addresses */
    unsigned held;
    uint32_t model_address;                /* +0x44 as the original holds it */
    EmOwnerModel model;
    EmOwnerSkeletonRecord skeleton[MODEL_BONES_MAX];
    uint8_t drew, at_node;
} Slot;

static struct {
    EmActorPool *pool;
    EmSceneState *scene;
    EmEquipmentLiveSpawn spawn;
    int attached;
    uint32_t fault;
    Slot slot[EM_ACTOR_POOL_CAPACITY];
    Slot *current;
    /* views */
    EmOwnerBone player_bone[PLAYER_NODES];
    EmOwnerBone *player_bone_ptr[PLAYER_NODES];
    uint32_t d0028A56C;
    int16_t d00248B98, d00248C78;
    uint32_t table[TABLE_WORDS];
    uint32_t spad3600[EM_PLAYER_EQUIPMENT_SPAD3600_WORDS];
    uint32_t spad38A0[EM_PLAYER_EQUIPMENT_SPAD38A0_WORDS];
    EmPlayerEquipmentHitState hit;
    EmPlayerEquipmentWorld world;
    EmPlayerEquipmentWorkers workers;
    EmPlayerEquipmentFault efault;
    EmPlayerEquipment e;
    /* 001AF780 / 001AF890 / 001CA6E0 on the one slot stack */
    EmRogerActor stack;
    /* 001C62C0 / 001C9610 */
    EmOwnerServices services;
    EmOwnerServicesScratch scratch;
    unsigned reported;
} S;

static int fail(u32 address, const char *what)
{
    if (!S.fault) {
        S.fault = address ? address : 0x0018A6B0u;
        fprintf(stderr, "player equipment: %s at %08X (equipment %08X/%d, stack %08X/%d, services %08X/%d)\n",
                what, (unsigned)S.fault, (unsigned)S.efault.address, (int)S.efault.code,
                (unsigned)S.stack.fault.address, (int)S.stack.fault.code,
                (unsigned)S.services.fault.address, (int)S.services.fault.code);
    }
    return -1;
}

uint32_t em_equipment_live_fault(void) { return S.fault; }
void em_equipment_live_set_spawn(EmEquipmentLiveSpawn spawn) { S.spawn = spawn; }

static u32 rd32(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

static int slot_index(const EmActor *a)
{
    if (!S.pool || !a || a < S.pool->records || a >= S.pool->records + EM_ACTOR_POOL_CAPACITY) return -1;
    return (int)(a - S.pool->records);
}

static Slot *slot_of(const EmPEN *n)
{
    for (unsigned i = 0; i < EM_ACTOR_POOL_CAPACITY; ++i)
        if (&S.slot[i].n == n) return &S.slot[i];
    return NULL;
}

static EmActor *actor_of(const Slot *s) { return &S.pool->records[s - S.slot]; }

/* ------------------------------------------------------------ SDK leaves */

static void to_f(float *out, const u32 *in, unsigned n) { memcpy(out, in, 4 * n); }
static void to_u(u32 *out, const float *in, unsigned n) { memcpy(out, in, 4 * n); }

static int w_001026A0(void *ctx, const u32 m[16], const u32 v[4], u32 out[4])
{
    (void)ctx;
    float mf[16], vf[4], of[4];
    to_f(mf, m, 16);
    to_f(vf, v, 4);
    em_effect_original_001026A0(of, mf, vf);
    to_u(out, of, 4);
    return 0;
}
static int w_001026D0(void *ctx, const u32 a[16], const u32 b[16], u32 out[16])
{
    (void)ctx;
    return em_loco_001026D0(out, a, b) < 0 ? -1 : 0;
}
static int w_001028B8(void *ctx, const u32 a[4], const u32 b[4], u32 out[4])
{
    float af[4], bf[4], of[4];
    to_f(af, a, 4);
    to_f(bf, b, 4);
    if (em_player_hang_vadd(ctx, of, af, bf) < 0) return -1;
    to_u(out, of, 4);
    return 0;
}
/* 001028D0: out = a - b over all four lanes, one VSUB.xyzw (the asm-word
 * leaf; the same form em_camera_leftovers and em_roger_actor_original
 * execute). */
static int w_001028D0(void *ctx, const u32 a[4], const u32 b[4], u32 out[4])
{
    (void)ctx;
    u32 x[4], y[4], r[4] = {0, 0, 0, 0};
    memcpy(x, a, sizeof x);
    memcpy(y, b, sizeof y);
    if (em_vu_vec_bits(EM_VU_SUB, 15, EM_VU_NO_BC, x, y, 0, NULL, r) != EM_EE_FLOAT_OK) return -1;
    memcpy(out, r, sizeof r);
    return 0;
}
static int w_00102760(void *ctx, const u32 v[4], u32 out[4])
{
    (void)ctx;
    float vf[4], of[4];
    to_f(vf, v, 4);
    em_effect_original_00102760(of, vf);
    to_u(out, of, 4);
    return 0;
}
static int w_001029C0(void *ctx, u32 out[16])
{
    (void)ctx;
    float m[16];
    if (em_owner_services_identity_001029C0(m) != 0) return -1;
    to_u(out, m, 16);
    return 0;
}
static int w_00102BB0(void *ctx, const u32 m[16], u32 angle, u32 out[16])
{
    (void)ctx;
    float in[16], o[16];
    to_f(in, m, 16);
    if (em_owner_services_rotate_y_00102BB0(o, in, angle) != 0) return -1;
    to_u(out, o, 16);
    return 0;
}

/* ------------------------------------------------------------ the node */

static int w_001AFC10(void *ctx, EmPEN *node)
{
    (void)ctx;
    Slot *s = slot_of(node);
    if (!s || !s->live) return -1;
    EmActor *a = actor_of(s);
    a->status = s->n.status;
    a->drawn = s->n.drawn;
    a->u04[0] = s->n.state;
    a->bones = s->n.bones_held;      /* +0x09: 001AFC10's 001AF800 loop count */
    a->u0A[2] = s->n.bone_count;
    if (em_actor_pool_free_001AFC10(S.pool, S.scene, a) < 0) return -1;
    s->live = 0;
    return 0;
}

/* +0x4C (001CAA00): the port renderer's boundary. The player's mesh draws
 * the equipment models at its node slots 4 and 14 of the current frame;
 * the method records the draw and whether the node's first bone is that
 * matrix. A node whose behaviour did not place its bones this tick (the
 * flavour-2 nodes in the tick D_008106CC frees them, which still draw) draws
 * at its previous matrix in the original; the player's mesh cannot, so the
 * first such frame is reported. */
static int w_method(void *ctx, EmPEN *node, u32 method)
{
    (void)ctx;
    Slot *s = slot_of(node);
    if (!s || method != METHOD_001CAA00) return -1;
    s->drew = 1;
    const unsigned at = node->flavour == 4 ? 14u : 4u;
    s->at_node = node->bone_count && node->bone[0] &&
                 memcmp(node->bone[0]->world, S.player_bone[at].world, sizeof node->bone[0]->world) == 0;
    if (!s->at_node && !(S.reported & 1u)) {
        S.reported |= 1u;
        fprintf(stderr, "player equipment: node (%u, %#x) draws at a matrix other than the player's node %u "
                "this frame; the player's mesh draws it at the node (001CAA00 is the renderer's)\n",
                (unsigned)node->flavour, (unsigned)node->variant, at);
    }
    return 0;
}

static int w_001C6120(void *ctx, u32 bank, u32 id, u32 *handle)
{
    (void)ctx;
    return em_area11_roger_001C6120(bank, id, handle);
}

/* 001CA6E0 = 001CA5E0(node, handle, 0): +0x44 and +0x4C
 * (em_roger_actor_001CA6E0); the model view is the handle's header and
 * skeleton records in the Roger export. */
static int w_001CA6E0(void *ctx, EmPEN *node, u32 handle)
{
    (void)ctx;
    Slot *s = slot_of(node);
    if (!s) return -1;
    EmRogerActorRecord r;
    memset(&r, 0, sizeof r);
    if (em_roger_actor_001CA6E0(&S.stack, &r, handle) < 0) return -1;
    const uint8_t *m = em_area11_roger_resource(handle, 0x40);
    if (!m) return -1;
    const u32 bones = rd32(m + 8), skeleton = rd32(m + 0xC);
    if (bones == 0 || bones > MODEL_BONES_MAX) return -1;
    const uint8_t *k = em_area11_roger_resource(handle + skeleton, 0x50u * bones);
    if (!k) return -1;
    memset(&s->model, 0, sizeof s->model);
    s->model.bone_count = (uint8_t)bones;
    memcpy(&s->model.radius, m + 0x20, 4);
    for (u32 i = 0; i < bones; ++i) {
        s->skeleton[i].parent = (int16_t)(uint16_t)(k[0x50 * i + 4] | k[0x50 * i + 5] << 8);
        memcpy(s->skeleton[i].bind, k + 0x50 * i + 0x10, 64);
    }
    s->model.skeleton = s->skeleton;
    s->model.skeleton_records = bones;
    s->model_address = r.model;
    node->model = &s->model;       /* +0x44 */
    node->method = r.draw;         /* +0x4C */
    return 0;
}

static int w_001C6150(void *ctx, const void *model, uint8_t *count)
{
    (void)ctx;
    if (!model) return -1;
    *count = ((const EmOwnerModel *)model)->bone_count;   /* model +0x08 */
    return 0;
}

/* 001AF780: the slot at the stack cursor (em_roger_actor_001AF780 on the
 * one stack), or the original's 0 while fewer than 31 are free. */
static int w_001AF780(void *ctx, EmOwnerBone **slot)
{
    (void)ctx;
    *slot = NULL;
    Slot *s = S.current;
    u32 word = 0;
    if (!s || em_roger_actor_001AF780(&S.stack, &word) < 0) return -1;
    if (!word) return 0;
    if (s->held >= MODEL_BONES_MAX) return -1;
    memset(&s->bone[s->held], 0, sizeof s->bone[s->held]);
    s->word[s->held] = word;
    *slot = &s->bone[s->held++];
    return 0;
}

/* anim_bone_array_setup (001CB5B0): D_00275B40 = the node's own +0x110
 * slots, which the world view hands over at every call. */
static int w_anim_bone_array_setup(void *ctx, uint8_t count)
{
    (void)ctx;
    (void)count;
    return 0;
}

/* bone_init_default_1 (001C62C0) over the node's model and slots. */
static int w_bone_init_default_1(void *ctx, EmPEN *node)
{
    (void)ctx;
    EmOwnerServicesOwner o;
    memset(&o, 0, sizeof o);
    o.model = (const EmOwnerModel *)node->model;
    o.bone_count = node->bone_count;
    for (unsigned i = 0; i < node->bone_count && i < EM_OWNER_SERVICES_MAX_BONES; ++i) o.bone[i] = node->bone[i];
    return em_owner_services_001C62C0(&S.services, &o) < 0 ? -1 : 0;
}

static int w_001C9610(void *ctx, EmOwnerBone *const *bones, u32 bones_count, int32_t count, const u32 world[16])
{
    (void)ctx;
    if (count < 0 || (u32)count > bones_count) return -1;
    float root[16];
    to_f(root, world, 16);
    return em_owner_services_001C9610(&S.services, bones, count, root) < 0 ? -1 : 0;
}

static int w_001B0070(void *ctx, u32 *value)
{
    (void)ctx;
    *value = em_scene_req_u32(S.scene, EM_SCENE_REQ_C8);
    return 0;
}

static int w_0015C310(void *ctx, int32_t arg1)
{
    (void)ctx;
    return S.spawn ? S.spawn(arg1) : -1;
}

/* The callees no first-level route reaches (docs/PLAYER_EQUIPMENT.md 4.2):
 * reaching one is a fault. */
static int x_node(void *ctx, EmPEN *node) { (void)ctx; (void)node; return -1; }
static int x_001B61C0(void *ctx, int32_t a, int32_t b, int32_t c, int32_t d)
{ (void)ctx; (void)a; (void)b; (void)c; (void)d; return -1; }
static int x_001EFEB0(void *ctx, u32 id, const u32 *m) { (void)ctx; (void)id; (void)m; return -1; }
static int x_001F4010(void *ctx, int32_t i, const u32 *at) { (void)ctx; (void)i; (void)at; return -1; }
static int x_00187780(void *ctx, EmPEN *n, int32_t a1, int32_t a2) { (void)ctx; (void)n; (void)a1; (void)a2; return -1; }
static int x_0019B2C0(void *ctx, const u32 a[4], const u32 b[4], int32_t m, int32_t *r)
{ (void)ctx; (void)a; (void)b; (void)m; (void)r; return -1; }
static int x_00189EC0(void *ctx, const void *e, int32_t *r) { (void)ctx; (void)e; (void)r; return -1; }
static int x_face_record(void *ctx, const void *rec, uint16_t *h, u32 n[3])
{ (void)ctx; (void)rec; (void)h; (void)n; return -1; }
static int x_001F00A0(void *ctx, u32 id, const u32 *a, const u32 *b, int32_t a3)
{ (void)ctx; (void)id; (void)a; (void)b; (void)a3; return -1; }
static int x_0019A570(void *ctx, const u32 f[4], const u32 t[4], int32_t m, int32_t id, int32_t *r)
{ (void)ctx; (void)f; (void)t; (void)m; (void)id; (void)r; return -1; }
static int x_00189FE0(void *ctx, EmPEN *n, const u32 a[4], const u32 b[4])
{ (void)ctx; (void)n; (void)a; (void)b; return -1; }
static int x_001EFF10(void *ctx, u32 id, const EmOwnerBone *b, const u32 *a0, const u32 *a1, const u32 *a2,
                      const u32 *a3, u32 f12, uint8_t **e)
{ (void)ctx; (void)id; (void)b; (void)a0; (void)a1; (void)a2; (void)a3; (void)f12; (void)e; return -1; }

static void wire(void)
{
    EmPlayerEquipmentWorkers *w = &S.workers;
    memset(w, 0, sizeof *w);
    w->w_001026A0 = w_001026A0;
    w->w_001026D0 = w_001026D0;
    w->w_001028B8 = w_001028B8;
    w->w_001028D0 = w_001028D0;
    w->w_00102760 = w_00102760;
    w->w_001029C0 = w_001029C0;
    w->w_00102BB0 = w_00102BB0;
    w->w_001AFC10 = w_001AFC10;
    w->w_method = w_method;
    w->w_001C6120 = w_001C6120;
    w->w_001CA6E0 = w_001CA6E0;
    w->w_001C6150 = w_001C6150;
    w->w_001AF780 = w_001AF780;
    w->w_anim_bone_array_setup = w_anim_bone_array_setup;
    w->w_bone_init_default_1 = w_bone_init_default_1;
    w->w_001854E0 = x_node;
    w->w_00185760 = x_node;
    w->w_001861C0 = x_node;
    w->w_001869A0 = x_node;
    w->w_00186A60 = x_node;
    w->w_001872C0 = x_node;
    w->w_00187CC0 = x_node;
    w->w_001B61C0 = x_001B61C0;
    w->w_001EFEB0 = x_001EFEB0;
    w->w_001F4010 = x_001F4010;
    w->w_00188C70 = x_node;
    w->w_0015C310 = w_0015C310;
    w->w_00189090 = x_node;
    w->w_00189330 = x_node;
    w->w_001899C0 = x_node;
    w->w_00189A20 = x_node;
    w->w_001C9610 = w_001C9610;
    w->w_001B0070 = w_001B0070;
    w->w_00187780 = x_00187780;
    w->w_001AA840 = x_node;
    w->w_0019B2C0 = x_0019B2C0;
    w->w_00189EC0 = x_00189EC0;
    w->w_face_record = x_face_record;
    w->w_001F00A0 = x_001F00A0;
    w->w_0018A180 = x_node;
    w->w_0019A570 = x_0019A570;
    w->w_00189FE0 = x_00189FE0;
    w->w_001EFF10 = x_001EFF10;
    S.e = (EmPlayerEquipment){w, &S.world, &S.efault};
}

/* ------------------------------------------------------------ attach */

int em_equipment_live_attach(EmActorPool *pool, EmSceneState *scene)
{
    S.attached = 0;
    if (!pool || !scene) return -1;
    const uint8_t *t = em_effects_live_elf(0x0024A220u, 4 * TABLE_WORDS);
    const uint8_t *b98 = em_effects_live_elf(0x00248B98u, 2), *c78 = em_effects_live_elf(0x00248C78u, 2);
    u32 bank = 0;
    if (!t || !b98 || !c78 || em_area11_roger_table_word(0x0028A490u + 4u * 0x37u, &bank) < 0) {
        fprintf(stderr, "player equipment: the effect tables or the Roger export are not loaded\n");
        return -1;
    }
    const EmRogerActorWorld *slots = em_area11_boxes_slot_world();
    if (!slots || !slots->d00275BCC) return -1;
    memset(S.slot, 0, sizeof S.slot);
    S.pool = pool;
    S.scene = scene;
    S.fault = 0;
    S.reported = 0;
    memcpy(S.table, t, sizeof S.table);
    S.d00248B98 = (int16_t)(uint16_t)(b98[0] | b98[1] << 8);
    S.d00248C78 = (int16_t)(uint16_t)(c78[0] | c78[1] << 8);
    S.d0028A56C = bank;
    memset(&S.stack, 0, sizeof S.stack);
    S.stack.world = *slots;
    memset(&S.services, 0, sizeof S.services);
    S.services.world.scratch = &S.scratch;
    memset(&S.efault, 0, sizeof S.efault);
    for (unsigned i = 0; i < PLAYER_NODES; ++i) S.player_bone_ptr[i] = &S.player_bone[i];
    EmPlayerEquipmentWorld *d = &S.world;
    memset(d, 0, sizeof *d);
    d->player_bones = S.player_bone_ptr;
    d->player_bone_count = PLAYER_NODES;
    d->d008106C6 = em_scene_req_at(scene, 0x008106C6u);
    d->d008106C7 = em_scene_req_at(scene, 0x008106C7u);
    d->d008106CC = em_scene_req_at(scene, 0x008106CCu);
    d->d00810CA4 = em_scene_progress_at(scene, 0x00810CA4u, 1);
    d->d00810CA6 = em_scene_progress_at(scene, 0x00810CA6u, 1);
    d->d00275BCC = slots->d00275BCC;
    d->d0028A56C = &S.d0028A56C;
    d->d00248B98 = &S.d00248B98;
    d->d00248C78 = &S.d00248C78;
    d->table = S.table;
    d->table_words = TABLE_WORDS;
    d->spad3600 = S.spad3600;
    d->spad38A0 = S.spad38A0;
    d->hit = &S.hit;
    if (!d->d00810CA4 || !d->d00810CA6) return -1;
    wire();
    S.attached = 1;
    return 0;
}

/* ------------------------------------------------------------ the tick */

/* The player's bone world matrices (+0x90 of the record pose's node
 * records the player's +0x110 words name). */
static int player_bones(const uint8_t *player)
{
    const EmPoseHost *h = player_pose_record_host();
    if (!h) return -1;
    for (unsigned i = 0; i < PLAYER_NODES; ++i) {
        const u32 node = rd32(player + 0x110 + 4 * i) + 0x90u;
        const uint8_t *m = NULL;
        for (unsigned k = 0; k < h->region_count && !m; ++k) {
            const EmPoseRegion *r = &h->region[k];
            if (r->bytes && node >= r->address && 0x40u <= r->size && node - r->address <= r->size - 0x40u)
                m = r->bytes + (node - r->address);
        }
        if (!m) return -1;
        memcpy(S.player_bone[i].world, m, 0x40);
    }
    return 0;
}

int em_equipment_live_tick(EmActor *actor)
{
    if (!S.attached || S.fault) return -1;
    const int i = slot_index(actor);
    if (i < 0 || actor->callback != EM_PLAYER_EQUIPMENT_CALLBACK) return fail(0x0018A6B0u, "a node outside the pool");
    Slot *s = &S.slot[i];
    if (!s->live || s->generation != actor->generation) {
        /* 0018A880's record: +0x03, +0x0D, +0x10; +0x14 = self (001AFA90). */
        memset(s, 0, sizeof *s);
        s->generation = actor->generation;
        s->live = 1;
        s->n.flavour = actor->model;
        s->n.variant = actor->param;
        s->n.self = &s->n;
    }
    const EmPlayerLiveActor *p = player_states_actor_mut();
    if (!p) return fail(PLAYER, "no player record");
    if (player_bones(p->bytes) < 0) return fail(0x008103C0u, "the player's node records are not mapped");
    S.world.player = p->bytes;
    /* D_00275B40: this node's own +0x110 slots (the walk's 001CB590). */
    S.world.d00275B40 = s->n.bone;
    S.world.d00275B40_count = EM_PLAYER_EQUIPMENT_MAX_BONES;
    s->n.status = actor->status;
    s->n.drawn = actor->drawn;
    s->n.state = actor->u04[0];
    s->drew = 0;
    s->at_node = 0;
    S.current = s;
    int r = em_player_equipment_tick(&S.e, &s->n);
    S.current = NULL;
    if (r < 0 || S.efault.code || S.stack.fault.code || S.services.fault.code)
        return fail(S.efault.code ? S.efault.address : S.stack.fault.code ? S.stack.fault.address
                  : S.services.fault.code ? S.services.fault.address : 0x0018A6B0u, "fault");
    if (!s->live) return 0;   /* freed (lifecycle 3) */
    actor->status = s->n.status;
    actor->drawn = s->n.drawn;
    actor->u04[0] = s->n.state;
    actor->u04[1] = s->n.b05;
    actor->u04[3] = s->n.b07;
    actor->bones = s->n.bones_held;
    actor->u0A[2] = s->n.bone_count;
    return 1;
}

int em_equipment_live_001AF800(EmActor *actor)
{
    const int i = slot_index(actor);
    if (i < 0 || !S.attached || actor->callback != EM_PLAYER_EQUIPMENT_CALLBACK) return 0;
    Slot *s = &S.slot[i];
    if (!s->live || s->generation != actor->generation) return 0;
    for (unsigned k = 0; k < actor->bones && k < s->held; ++k)
        if (em_roger_actor_001AF890(&S.stack, s->word[k]) < 0) return fail(0x001AF890u, "001AF890 faulted");
    return 1;
}

int em_equipment_live_nodes(EmEquipmentLiveNode *out, int max)
{
    if (!S.attached || !S.pool || !out) return 0;
    int n = 0;
    for (const EmActor *a = S.pool->head; a && n < max; a = a->next) {
        const int i = slot_index(a);
        if (i < 0 || a->callback != EM_PLAYER_EQUIPMENT_CALLBACK) continue;
        const Slot *s = &S.slot[i];
        if (!s->live || s->generation != a->generation) continue;
        EmEquipmentLiveNode *o = &out[n++];
        memset(o, 0, sizeof *o);
        o->address = em_actor_pool_address(S.pool, a);
        o->head[0x00] = a->status;
        o->head[0x01] = a->drawn;
        o->head[0x02] = a->cls;
        o->head[0x03] = a->model;
        memcpy(o->head + 0x04, a->u04, sizeof a->u04);
        o->head[0x09] = a->bones;
        memcpy(o->head + 0x0A, a->u0A, sizeof a->u0A);
        o->head[0x0D] = a->param;
        o->head[0x0E] = (uint8_t)a->uid;
        o->head[0x0F] = (uint8_t)(a->uid >> 8);
        o->model = s->model_address;
        o->method = s->n.method;
        if (s->n.bone[0]) memcpy(o->bone0, s->n.bone[0]->world, sizeof o->bone0);
        o->drew = s->drew;
        o->at_node = s->at_node;
    }
    return n;
}
