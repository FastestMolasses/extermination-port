/* AREA11 pool-node bindings (step S10b). See em_area11_bindings.h and
 * docs/SCENE_COORDINATOR_DESIGN.md sections 4.3, 4.4 and 6 (S10b).
 *
 * Evidence for every spawn below (addresses only; Extermination/src):
 *   0018A880  byte-matched: alloc(1); +3 = a0; +0x10 = 0018A6B0; +0xD = a1.
 *   0015C310  byte-matched: (0,0) (stored at player+0x20), (1,0), (1,0x10)
 *             when a1 == 0; then D_00810CA4 == 2: (2,0xC); == 1: (2,0xB),
 *             (2,CA6); == 0: (2,0xA), (2,CA6); else (2,CA5), (2,CA6),
 *             (2,CA7); then D_00810CA6 == 4: (1,0x15).
 *   0015C420  0018A880(4,0) (player+0x18), 0015C310(player, 0), ...,
 *             001F0120(player, 0x3B); reached from 0015BA50 when the
 *             player's mode byte +4 is 0 (0015BA50 case 0), which the
 *             001AF5C0 wipe of state 0 leaves.
 *   001F0120  001E2290(b) (1 for 0x3B and 0x47) then 001EF9D0(0x80000010,
 *             0, 1.0); +0xD = b; +0x24 = owner +0x14. Since census L39 the
 *             one translation (em_head_sprite_original over em_effects_live,
 *             whose 001EF9D0 is em_effect_original's: the entity bytes come
 *             from the user's ELF, assets/effect_tables.emet).
 *   001EFD20  byte-matched: 001EF9D0(type), +0xB0 = the argument vector,
 *             +0xC0..+0xCC = 0, +0xBC = 1.0 (em_effect_original over
 *             em_effects_live since census L26).
 *   001C1EA0  byte-matched: D_008106C8 & 0x02000010 -> 001EFD20(0x80000017,
 *             &D_00250F00); & 0x04000020 -> (.., &D_00250F10); & 0x08000040
 *             -> (.., &D_00250F20). D_00250F00/10/20 are zero vectors in the
 *             ELF data.
 *   001C5570  byte-matched: alloc(0xC); +0x9A = +3 = +0x2E = 0; +0xD = a2;
 *             +0xE = 0xFFFF; +0x56 = +0x54 = 0; +0xA0 = a1 vector; +0xB0/+0xC0
 *             copied from the owner; +0xA = 0; a3 0 -> 001C5760, 1 ->
 *             001C5680, 2 -> +0xA = 1 and 001C5760.
 *   00219550  NEARMISS: state 0 (first call) spawns 001C5570(self, .., 0x73,
 *             1) after 001B0FD0 / 001B1020.
 *   00159210  state 0: model 0x2C -> 001C5570(p, .., 0x74, 1); otherwise,
 *             unless bit (+0x2E) of D_00810841[D_00810700] is set, 001C5570(p,
 *             .., 0x75, 1).
 *   001E55F0  NEARMISS (logic checked; translated in em_weather.c, oracle
 *             test_weather_reference): state 1 ends with D_008106B8 == 2 &&
 *             D_0028A9A0 == 2 -> +4 = 3; states 2/3 call 001AFC10(self).
 *   001C5930  NEARMISS; its lifecycle read from the .s: +4 == 3 or 2 ->
 *             001AFC10(self) (0x1C5954/0x1C5960 -> 0x1C5C24); +4 == 0 ->
 *             state 0, +4 = 1 (0x1C59C0..0x1C59CC); +4 == 1 -> with +5 == 1,
 *             or +5 == 0 after the card timer step, D_008106B8 != 0 -> +4 = 3
 *             (0x1C5AA8..0x1C5ABC). +5 only goes 0 -> 1 (0x1C5A9C..0x1C5AA4)
 *             and 001AFC10 clears it, so the B8 test runs on every state-1 call.
 *   Overlay owners (no static code): ORIGINAL_FRAME_ORDER.md section 6
 *   measured, on the first world frame, 0x825940 (deferred g0.7, the
 *   security gun) spawning its 001C5680 lamp child (+0xD 0x7A): its own
 *   lifecycle 0 does it since census L24 (em_security_gun, below); and
 *   0x827B10 (area11[19]) spawning a 001C5760 child at 0x827C20 (+0xD 0x10,
 *   +0xA 0), an INTERIM spawn.
 *   Roger 0x8237E0's 001BA8E0 -> 001F0120(Roger, 0x47) runs in its own
 *   lifecycle 0 since census L22 (em_area11_roger).
 *   The nodes 001EF9D0 allocates (the puffs 001EA240, the head sprites
 *   001E2560, the weather 001E55F0) are bound by their +0x10 through
 *   bind_spawned (em_effects_live's hook); the equipment nodes 0018A6B0 run
 *   em_equipment_live (census L28).
 */
#include "game/em_area11_bindings.h"

#include <stdio.h>
#include <string.h>

#include "game/em_area11_boxes.h"
#include "game/em_area11_door.h"
#include "game/em_area11_effect_runtime.h"
#include "game/em_area11_opening.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_area11_roger.h"
#include "game/em_area11_script_host.h"
#include "game/em_collision_world.h"
#include "game/em_director_original.h"
#include "game/em_sdk_math_original.h"
#include "game/em_effect_kinds.h"
#include "game/em_fan_original.h"
#include "game/em_effects_live.h"
#include "game/em_equipment_live.h"
#include "game/em_indicator_bind_live.h"
#include "game/em_indicator_child.h"
#include "game/em_pickup.h"
#include "game/em_player_closure_live.h"
#include "game/em_random.h"
#include "game/em_frame.h"
#include "game/em_hud.h"
#include "game/em_manager_008257A0.h"
#include "game/em_game_internal.h"
#include "game/em_opening_runtime.h"
#include "game/em_pickup_original.h"
#include "game/em_player.h"
#include "game/em_props.h"
#include "game/em_scene_bindings.h"
#include "game/em_scene_workers.h" /* EM_SCENE_D_008102B0 */
#include "game/em_script_door_fan.h"
#include "game/em_security_gun.h"
#include "game/em_security_gun_rest.h"
#include "game/em_sfx.h"
#include "game/em_snow_runtime.h"
#include "game/em_status_ui_leftovers.h"

static EmActorPool *s_pool;
static EmSceneState *s_scene;

/* ------------------------------------------------------------ node state */

typedef struct Node Node;
typedef int (*NodeTick)(EmActor *actor, Node *node, const EmArea11World *world);

typedef struct {
    uint32_t callback;
    const char *name;   /* binding name */
    NodeTick tick;      /* NULL: no port code; `note` is reported once */
    const char *note;
} Binding;

struct Node {
    const Binding *binding;
    char record[24];  /* original tracer tag, "" for runtime nodes */
    char name[112];   /* binding name recorded in the trace */
    uint8_t ticked;   /* first behaviour call done */
    EmActor *child;   /* an owner's indicator child (00219550 +0x2EC,
                       * 00827B10 +0x2E4) until the owner stops it */
    /* Indicator children (001C5680 / 001C5760): their owner, and their
     * +0xA0 colour vector (EmActor has no field for +0xA0), written by the
     * spawn and, for the terminal's child, by the owner's tail every frame. */
    EmActor *parent;
    float a0[4];
    /* 001E55F0 nodes: the actor's own weather state (its +4 byte and +0x1F0
     * block, em_weather.h); zeroed by bind_node, so a new actor seeds. */
    EmWeather weather;
    /* Record words EmActor has no field for, of the security gun 00825940,
     * its cable 00827490 and the fan pair 00827630: +0x28 (the gun's scan
     * timer, the cable's fall counter, the fan's phase timer), +0x34 (the
     * cable's) and +0x38 (the fan's spin). Zero at the bind; each owner
     * writes them before it reads them. */
    int16_t h28, h34;
    float f38;
    /* The opening script's 001BB0E0 records (001BAC00): +0x20, their
     * placement entry's address (the controller that spawned them is
     * `parent`, their +0x24). */
    uint32_t entry_20;
};

static Node s_nodes[EM_ACTOR_POOL_CAPACITY];
static EmActor *s_panel_child; /* 00159210's +0x20 */
static uint64_t s_reported; /* one bit per binding row */
static float s_walk_eye[3]; /* camera eye at the start of the walk */

static Node *node_of(const EmActor *actor)
{
    if (!s_pool || !actor || actor < s_pool->records || actor >= s_pool->records + EM_ACTOR_POOL_CAPACITY)
        return NULL;
    return &s_nodes[actor - s_pool->records];
}

/* Original address of `actor` (the +0x24 link value). */
static uint32_t address_of(const EmActor *actor)
{
    return em_actor_pool_address(s_pool, actor);
}

static int fault(uint32_t address, EmSceneFaultCode code, const char *why)
{
    fprintf(stderr, "em_area11: %08X: %s\n", (unsigned)address, why);
    return em_scene_fault(s_scene, address, code);
}

/* ------------------------------------------------------- spawn helpers */

static int bind_node(EmActor *actor, const char *record);

/* The fields every indicator-child spawn writes. 001C5570 (byte-matched):
 * alloc(0xC); +0x9A = +3 = +0x2E = 0; +0xD = a2; +0xE = 0xFFFF; +0x54 =
 * +0x56 = 0; +0xA0 = the a1 vector; +0xB0 / +0xC0 copied from the owner;
 * +0xA = 0 (a3 2: 1); +0x10 by a3. The AREA11 owners 00827B10
 * (0x827BD8..0x827C4C) and 0x825940 (0x825A74..0x825AE0) allocate their
 * child inline with the same stores except +0xA, which they leave as
 * 001AFC10 cleared it (0). */
static int spawn_child_record(EmActor *owner, const float a0[4], uint8_t param, uint32_t callback,
                              uint8_t alt, EmActor **out)
{
    if (out)
        *out = NULL;
    EmActor *p = em_actor_pool_alloc_001AFA90(s_pool, s_scene, 0x0C);
    if (!p)
        return 0;
    p->table_index = 0;
    p->model = 0;
    p->flags2 = 0;
    p->param = param;
    p->uid = 0xFFFF;
    p->link = 0;
    p->kind = 0;
    memcpy(p->pos, owner->pos, sizeof p->pos);
    memcpy(p->rot, owner->rot, sizeof p->rot);
    p->u0A[0] = alt; /* +0x0A */
    p->callback = callback;
    if (bind_node(p, NULL) < 0)
        return -1;
    Node *node = node_of(p);
    node->parent = owner;
    memcpy(node->a0, a0, sizeof node->a0);
    if (out)
        *out = p;
    return 0;
}

/* 001C5570(owner, a1 vector, a2, a3). Returns 0 (also when the class-0xC
 * reserve refuses the alloc: the original stores the 0 it returns) or -1;
 * *out is the child (NULL when refused). */
static int spawn_001C5570_child(EmActor *owner, const float a1[4], uint8_t a2, int a3, EmActor **out)
{
    switch (a3) {
    case 0:
        return spawn_child_record(owner, a1, a2, 0x001C5760u, 0, out);
    case 1:
        return spawn_child_record(owner, a1, a2, 0x001C5680u, 0, out);
    case 2:
        return spawn_child_record(owner, a1, a2, 0x001C5760u, 1, out);
    default:
        return fault(0x001C5570u, EM_SCENE_FAULT_BAD_INDEX, "001C5570 a3 outside 0..2");
    }
}

/* 0018A880(a0, a1). */
static int spawn_0018A880(uint8_t a0, uint8_t a1)
{
    EmActor *v = em_actor_pool_alloc_001AFA90(s_pool, s_scene, 1);
    if (!v)
        return 0;
    v->model = a0;
    v->callback = 0x0018A6B0u;
    v->param = a1;
    return bind_node(v, NULL);
}

/* ------------------------------------------------------- node adapters */

static int free_self_001AFC10(EmActor *actor);

/* Item owners 00219550 x6 and 0015AFA0 (deferred g0.0..g0.6), one owner per
 * node in the AREA11 interaction host (WP-6). The first call is state 0:
 * 00219550 spawns its 001C5570 light child (both state-0 arms reach the
 * call when the bone slots are available; the child is kept as the
 * owner's +0x2EC), 0015AFA0 runs 0015AC00 (the aura's 001F1110). Later
 * calls are the owner update with its 001B17A0 publication; the take's
 * completion writes the child's +4 = 3 (the child then frees itself), and
 * the call after it frees the owner (00219550 state 3 / 0015AFA0 state 2:
 * 001AFC10). The record is bound to its host owner first (census L07: the
 * owner publishes it and 001A2370 moves its collision cell); 00219550's
 * state 0 runs 001C6380 and 001A2370 before the 001C5570 child spawn. */
static int tick_pickup(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)world;
    if (!node->ticked) {
        if (em_area11_interaction_host_bind_actor(actor->source_id, actor) < 0 ||
            em_area11_interaction_host_pickup_state0(actor->source_id, actor->model, actor->param) < 0)
            return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                         "item owner state 0: the interaction host failed");
        /* 00219550 state 0: 0x700038A0 = (0, 1.0, 0, 0.25), then
         * +0x2EC = 001C5570(self, 0x700038A0, 0x73, 1). */
        static const float k_light[4] = {0.0f, 1.0f, 0.0f, 0.25f};
        if (actor->callback == 0x00219550u &&
            spawn_001C5570_child(actor, k_light, 0x73, 1, &node->child) < 0)
            return -1;
        return 1;
    }
    int result = em_area11_interaction_host_pickup_tick(actor->source_id);
    if (result < 0)
        return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                     "item owner: the interaction host failed");
    if (node->child) {
        EmInteractionSceneOwner *record =
            em_interaction_scene_find(em_area11_interaction_host_scene(), actor->source_id);
        const EmPickupOwner *owner = record ? record->native_owner : NULL;
        if (!owner)
            return fault(actor->callback, EM_SCENE_FAULT_NULL_WORKER, "item owner without its binding");
        if (owner->child_status == 3) {
            node->child->u04[0] = 3; /* the child's +4 */
            node->child = NULL;
        }
    }
    return result ? 1 : free_self_001AFC10(actor);
}

/* Crates 001551B0 (area11[3..6]) and drums 00156620 (area11[14]/[15]):
 * their original owners over the node's own record (em_area11_boxes, census
 * L25), in both walk variants (class 4 is not skipped by 001AFD70 mode 1). */
static int tick_box(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)node;
    (void)world;
    if (em_area11_boxes_tick(actor, s_pool, s_scene) < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                                                 "box owner: a worker failed (em_area11_boxes)");
    return 1;
}

/* ------------------------------------------ the security gun pair (L24) */

/* The security gun 0x825940 (deferred g0.7) and its cable 0x827490
 * (deferred g0.8) on their original owners: em_gun_tick / em_gun_cable_tick
 * (em_security_gun.c, oracle tools/test_script_door_fan_reference.py) over
 * the node's own record (docs/SECURITY_GUN.md "Binding"). Both are world
 * model owners of the per-area bank (+0x0D: entries 8 and 6 of *D_0028A59C)
 * and draw through the object-unit path (001CAA00, em_owner_draw_live), in
 * every walk mode (class 4 is not skipped by 001AFD70 modes 0 and 1).
 *
 * Record storage: +0x00, +0x04, +0x9A, +0xB0, +0xC0 and +0x36 are EmActor's;
 * +0x28 / +0x34 are the node's (h28, h34); the gun's +0x1F4..+0x227 are its
 * +0x1F0 block (EmActor.scratch); its bone slots are the owner's
 * (em_area11_boxes_owner_slot). The gun's +0x220 holds its lamp child's
 * original address, as the original stores it.
 *
 * What the first level reaches: the gun's lifecycle 0 (001B0FD0, the flag
 * test, its one rand() draw for +0x28, bone 3 +0x78, 001C6380, 001A2370 and
 * the 001AFA90 lamp) and then its dormant 0x64 every tick (the flag test,
 * 001C6380, 001B17A0, the draw); the cable's lifecycle 0 (001B0FD0, +0x34,
 * 001B11E0 over the taken bit +0x9A) and then lifecycle 1 every tick (the
 * +0x36 test, 001C6380, 001B17A0, the draw). Bound but not reached (no live
 * code writes the cable's +0x36, as for the crates): the cable's hit (the gun
 * to lifecycle 2 with +0x21C = 90, 001EFE00(0x80000045), cue 0x426) and its
 * lifecycle 2 (cue 0x427 at 10, 001B1190 over the taken bit, 001B17A0, the
 * draw), and the gun's lifecycle 2 with the flag clear (the tail only).
 * Fail-stop: 001EFE00 (its 001EF9D0 node view is not bound, and the node
 * 0021AAC0 spawns strip nodes 0021A500 whose packet builder 001CE860 is
 * untranslated); the gun's lifecycles 4 and 1 (EM_GUN_FAULT_UNTRANSLATED at
 * 0x825B74 / 0x826190) and its lifecycle-2 swing, which only D_00810788 ==
 * 0xFF reaches (a return visit): its lamp view r_child_220, 00102958 and
 * 0x70003A20 are unbound. */
typedef struct {
    EmActor *actor;
    Node *node;
    EmOwnerBone *bone;     /* the slot the last r_00275B40 answered */
    uint32_t bone_address; /* its original address */
    EmGunLamp lamp;        /* the 001AFA90 view the gun writes */
    EmActor *lamp_actor;
    EmGunLinked linked;    /* the cable's view of the record at its +0x18 */
    EmActor *linked_actor;
    int freed;
} GunCall;

static int gun_report(const char *what)
{
    fprintf(stderr, "em_area11: security gun pair: %s\n", what);
    return -1;
}

/* +0x1F0 block words (the gun's +0x1F4..+0x227). */
static uint32_t gun_word(const EmActor *a, uint32_t offset)
{
    uint32_t v;
    memcpy(&v, a->scratch + (offset - 0x1F0u), 4);
    return v;
}

static void gun_put_word(EmActor *a, uint32_t offset, uint32_t v)
{
    memcpy(a->scratch + (offset - 0x1F0u), &v, 4);
}

static float gun_float(const EmActor *a, uint32_t offset)
{
    float f;
    memcpy(&f, a->scratch + (offset - 0x1F0u), 4);
    return f;
}

static void gun_put_float(EmActor *a, uint32_t offset, float f)
{
    memcpy(a->scratch + (offset - 0x1F0u), &f, 4);
}

static void gun_load(const GunCall *c, EmGun *g)
{
    const EmActor *a = c->actor;
    memset(g, 0, sizeof *g);
    g->b00 = a->status;
    g->lifecycle = a->u04[0];
    g->timer_28 = c->node->h28;
    memcpy(g->pos_B0, a->pos, sizeof g->pos_B0);
    memcpy(g->rot_C0, a->rot, sizeof g->rot_C0);
    uint32_t slot3 = 0;
    (void)em_area11_boxes_owner_slot(a, 3, &slot3); /* +0x11C; 0 before the bind */
    g->bone3_11C = slot3;
    g->f1F4 = gun_float(a, 0x1F4);
    g->f1F8 = gun_float(a, 0x1F8);
    g->f1FC = gun_float(a, 0x1FC);
    g->w200 = gun_word(a, 0x200);
    g->w204 = gun_word(a, 0x204);
    g->w208 = gun_word(a, 0x208);
    g->w20C = gun_word(a, 0x20C);
    g->f210 = gun_float(a, 0x210);
    g->f214 = gun_float(a, 0x214);
    g->f218 = gun_float(a, 0x218);
    g->w21C = (int32_t)gun_word(a, 0x21C);
    g->child_220 = gun_word(a, 0x220);
    g->w224 = (int32_t)gun_word(a, 0x224);
}

static void gun_store(GunCall *c, const EmGun *g)
{
    EmActor *a = c->actor;
    a->status = g->b00;
    a->u04[0] = g->lifecycle;
    c->node->h28 = g->timer_28;
    gun_put_float(a, 0x1F4, g->f1F4);
    gun_put_float(a, 0x1F8, g->f1F8);
    gun_put_float(a, 0x1FC, g->f1FC);
    gun_put_word(a, 0x200, g->w200);
    gun_put_word(a, 0x204, g->w204);
    gun_put_word(a, 0x208, g->w208);
    gun_put_word(a, 0x20C, g->w20C);
    gun_put_float(a, 0x210, g->f210);
    gun_put_float(a, 0x214, g->f214);
    gun_put_float(a, 0x218, g->f218);
    gun_put_word(a, 0x21C, (uint32_t)g->w21C);
    gun_put_word(a, 0x220, g->child_220);
    gun_put_word(a, 0x224, (uint32_t)g->w224);
}

static int gun_001C6380(void *ctx)
{
    GunCall *c = ctx;
    return em_area11_boxes_owner_001C6380(c->actor, NULL);
}

/* 001B17A0(self) through the interaction host's services (the view the
 * placed prop 001C4820 publishes; +0x01 = the 001B1630 byte). */
static int gun_001B17A0(void *ctx)
{
    GunCall *c = ctx;
    EmOwnerServicesOwner view;
    memset(&view, 0, sizeof view);
    view.cls = c->actor->cls;
    view.kind = c->actor->model;
    view.model_id = c->actor->param;
    view.flags2 = c->actor->flags2;
    memcpy(view.pos, c->actor->pos, sizeof view.pos);
    return em_area11_interaction_host_offer_001B17A0(c->actor, &view) < 0 ? -1 : 0;
}

/* +0x4C: 001CAA00 over the record (em_owner_draw_live). */
static int gun_draw(void *ctx)
{
    GunCall *c = ctx;
    return em_area11_boxes_owner_draw(c->actor);
}

static int gun_001AFC10(void *ctx)
{
    GunCall *c = ctx;
    if (em_actor_pool_free_001AFC10(s_pool, s_scene, c->actor) < 0)
        return -1;
    c->freed = 1;
    return 0;
}

/* 001B0FD0 over the per-area bank: *result 0 (bound) or 1 (the bone cap
 * refused); the module owns the +0x04 it writes. */
static int gun_001B0FD0(void *ctx, int32_t *result)
{
    GunCall *c = ctx;
    return em_area11_boxes_owner_001B0FD0(c->actor, s_pool, result);
}

static int gun_rand(void *ctx, int32_t *value)
{
    (void)ctx;
    *value = (int32_t)em_random_next();
    return 0;
}

/* 0011E2A8: the one bound SDK sine (the collision world's SDK context). */
static int gun_sin(void *ctx, float x, float *result)
{
    (void)ctx;
    EmSdkMathContext *sdk = em_collision_world_sdk();
    if (!sdk)
        return gun_report("0011E2A8 without the SDK context");
    return em_sdk_math_original_w_0011E2A8(sdk, x, result) < 0 || sdk->fault ? -1 : 0;
}

/* D_00275B40[index]: the gun's own slot (the walk set D_00275B40 = node +
 * 0x110). */
static int gun_slot(void *ctx, uint32_t index, uint32_t *bone)
{
    GunCall *c = ctx;
    c->bone = em_area11_boxes_owner_slot(c->actor, index, &c->bone_address);
    if (!c->bone)
        return gun_report("D_00275B40 slot outside the gun's bound slots");
    *bone = c->bone_address;
    return 0;
}

/* *(float *)(bone + 0x78) (bone 3's Z angle, EmOwnerBone.rot[2]). */
static int gun_bone_f32(void *ctx, uint32_t bone, uint32_t offset, float value)
{
    GunCall *c = ctx;
    if (!c->bone || bone != c->bone_address || offset != 0x78u)
        return gun_report("a bone store other than the answered slot's +0x78");
    c->bone->rot[2] = value;
    return 0;
}

/* 001A2370(self, bone 3 + 0x90): the gun's plate (its uid's cell) through
 * bone 3's world matrix, which 001C6380 has just written. */
static int gun_001A2370(void *ctx, uint32_t matrix)
{
    GunCall *c = ctx;
    if (!c->bone || matrix != c->bone_address + 0x90u)
        return gun_report("001A2370 with a matrix other than the answered slot's +0x90");
    return em_collision_world_retransform_001A2370(c->actor, c->bone->world) < 0 ? -1 : 0;
}

/* 001AFA90(0xC): the lamp child; the gun writes its bytes through the view
 * (applied to the record after the call, gun_lamp_apply). A refused alloc
 * answers 0, as the original. */
static int gun_001AFA90(void *ctx, uint8_t cls, uint32_t *node, EmGunLamp **view)
{
    GunCall *c = ctx;
    *node = 0;
    *view = NULL;
    EmActor *p = em_actor_pool_alloc_001AFA90(s_pool, s_scene, cls);
    if (!p)
        return em_scene_faulted(s_scene) ? -1 : 0;
    memset(&c->lamp, 0, sizeof c->lamp);
    c->lamp_actor = p;
    *node = address_of(p);
    *view = &c->lamp;
    return 0;
}

/* The lamp's stores (0x825AC8..0x825B2C) onto its record, then its binding
 * by +0x10 (001C5680: tick_indicator). */
static int gun_lamp_apply(GunCall *c)
{
    EmActor *p = c->lamp_actor;
    const EmGunLamp *v = &c->lamp;
    p->table_index = v->b9A;  /* +0x9A */
    p->model = v->b03;        /* +0x03 */
    p->flags2 = v->h2E;       /* +0x2E */
    p->param = v->b0D;        /* +0x0D */
    p->uid = v->h0E;          /* +0x0E */
    p->kind = v->h54;         /* +0x54 */
    p->link = v->h56;         /* +0x56 */
    memcpy(p->pos, v->pos_B0, sizeof p->pos);
    memcpy(p->rot, v->rot_C0, sizeof p->rot);
    p->callback = v->handler_10;
    if (bind_node(p, NULL) < 0)
        return -1;
    Node *node = node_of(p);
    node->parent = c->actor;
    memcpy(node->a0, v->fA0, sizeof node->a0); /* +0xA0 */
    return 0;
}

/* 001B11E0(+0x9A): the taken-bit test over the canonical D_00810860 rows. */
static int cable_001B11E0(void *ctx, uint8_t id, int32_t *result)
{
    (void)ctx;
    int r = em_actor_roster_001B11E0(s_scene, (EmActorRosterProgress *)em_scene_progress_spawn_view(s_scene), id);
    if (r < 0)
        return -1;
    *result = r;
    return 0;
}

/* 001B1190(+0x9A): em_gun_rest_001B1190 (the verified translation) over
 * D_00810700 and the canonical D_00810860 rows. */
static const uint8_t *cable_load(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    if (address == 0x00810700u && size == 1)
        return &s_scene->d810700;
    return em_scene_progress_at(s_scene, address, size);
}

static uint8_t *cable_store(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    return em_scene_progress_at(s_scene, address, size);
}

static int cable_001B1190(void *ctx, uint8_t id)
{
    (void)ctx;
    const EmGunRestMem mem = {NULL, cable_load, cable_store};
    EmGunFault f = {0, 0};
    if (em_gun_rest_001B1190(id, &mem, &f) < 0) {
        fprintf(stderr, "em_area11: 001B1190 faulted at %08X\n", (unsigned)f.address);
        return -1;
    }
    return 0;
}

/* 001EFE00(0x80000045, self): not bound (see above). */
static int cable_001EFE00(void *ctx, uint32_t fx, int32_t *result)
{
    (void)ctx;
    (void)result;
    fprintf(stderr, "em_area11: the gun cable's 001EFE00(%08X) is not bound: its 001EF9D0 node view, the node "
                    "0021AAC0 and its strip nodes' 001CE860 (fail-stop; docs/SECURITY_GUN.md)\n",
            (unsigned)fx);
    return -1;
}

/* 001FBD50(self, cue, 0, range). */
static int cable_001FBD50(void *ctx, int32_t cue, int32_t a2, float range)
{
    GunCall *c = ctx;
    if (a2 != 0)
        return gun_report("001FBD50 with a nonzero a2 (the flat cue)");
    em_sfx_play_at((unsigned)cue, c->actor->pos, range);
    return 0;
}

/* The record at the cable's +0x18 (the gun, the node spawned just before
 * it): its +0x04 and +0x21C, written back after the call. */
static int cable_link_18(void *ctx, EmGunLinked **linked)
{
    GunCall *c = ctx;
    EmActor *prev = c->actor->prev;
    *linked = NULL;
    if (!prev)
        return 0; /* the module faults: the original would store through 0 */
    c->linked_actor = prev;
    c->linked.lifecycle = prev->u04[0];
    c->linked.w21C = (int32_t)gun_word(prev, 0x21C);
    *linked = &c->linked;
    return 0;
}

static int gun_fault(const EmGunFault *f, const char *who)
{
    if (em_scene_faulted(s_scene))
        return -1;
    const EmSceneFaultCode code =
        f->code == EM_GUN_FAULT_WORKER_FAILED ? EM_SCENE_FAULT_WORKER_FAILED : EM_SCENE_FAULT_NULL_WORKER;
    const char *why = f->code == EM_GUN_FAULT_UNTRANSLATED
                          ? "a return-visit lifecycle (4 or 1) is not bound in the first level"
                      : f->code == EM_GUN_FAULT_WORKER_FAILED ? "a worker failed"
                                                               : "an unbound worker or view was reached";
    fprintf(stderr, "em_area11: %s: %s\n", who, why);
    return fault(f->address, code, who);
}

static void gun_workers(GunCall *c, EmGunWorkers *w)
{
    memset(w, 0, sizeof *w);
    w->ctx = c;
    w->w_001C6380 = gun_001C6380;
    w->w_001B17A0 = gun_001B17A0;
    w->w_draw_4C = gun_draw;
    w->w_001AFC10 = gun_001AFC10;
    w->w_001B0FD0 = gun_001B0FD0;
    w->w_00122BB8 = gun_rand;
    w->w_0011E2A8 = gun_sin;
    w->r_00275B40 = gun_slot;
    w->s_bone_f32 = gun_bone_f32;
    w->w_001A2370 = gun_001A2370;
    w->w_001AFA90 = gun_001AFA90;
    /* r_child_220 / w_00102958: the lifecycle-2 swing (return visit). */
    w->w_001B11E0 = cable_001B11E0;
    w->w_001B1190 = cable_001B1190;
    w->w_001EFE00 = cable_001EFE00;
    w->w_001FBD50 = cable_001FBD50;
    w->r_link_18 = cable_link_18;
}

static int tick_gun(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)world;
    GunCall c;
    memset(&c, 0, sizeof c);
    c.actor = actor;
    c.node = node;
    if (!em_scene_progress_at(s_scene, 0x00810788u, 1))
        return fault(0x00810788u, EM_SCENE_FAULT_BAD_INDEX, "D_00810788 is not canonical");
    /* D_00810758[256]: the gun reads [0x30] only (001BA1C0(self, 0x30)). */
    const EmGunWorld gw = {em_scene_progress_spawn_view(s_scene), NULL, NULL, NULL};
    EmGunWorkers w;
    gun_workers(&c, &w);
    EmGun g;
    gun_load(&c, &g);
    EmGunFault f = {0, 0};
    if (em_gun_tick(&g, &gw, &w, &f) < 0)
        return gun_fault(&f, "security gun 00825940");
    if (c.freed)
        return 1;
    gun_store(&c, &g);
    if (c.lamp_actor && gun_lamp_apply(&c) < 0)
        return -1;
    return 1;
}

static int tick_gun_cable(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)world;
    GunCall c;
    memset(&c, 0, sizeof c);
    c.actor = actor;
    c.node = node;
    EmGunWorkers w;
    gun_workers(&c, &w);
    EmGunCable cable = {actor->status, actor->u04[0], node->h28, node->h34, (int16_t)actor->h36,
                        actor->table_index, 0};
    EmGunFault f = {0, 0};
    if (em_gun_cable_tick(&cable, &w, &f) < 0)
        return gun_fault(&f, "gun cable 00827490");
    if (c.linked_actor) {
        c.linked_actor->u04[0] = c.linked.lifecycle;
        gun_put_word(c.linked_actor, 0x21C, (uint32_t)c.linked.w21C);
    }
    if (c.freed)
        return 1;
    actor->status = cable.b00;
    actor->u04[0] = cable.lifecycle;
    node->h28 = cable.timer_28;
    node->h34 = cable.h34;
    return 1;
}

/* ------------------------------------------------ the fan pair (L24) */

/* 0x827630, the fan pair (area11[1] and [2]), on its original owner:
 * em_fan_original_tick (oracle tools/test_fan_original_reference.py) over
 * the node's record (docs/FAN_ORIGINAL.md "Binding"): +0x04 / +0x05 /
 * +0x2E / +0xC8 are EmActor's (u04[0], u04[1], flags2, rot[2]), +0x28 and
 * +0x38 the node's. A world model owner (+0x0D 0x13 of *D_0028A59C) drawn
 * through the object-unit path; the legacy em_pickup prop instances the
 * manifest placed at its +0xB0 stop drawing once it is bound. Its globals:
 * D_00810788, D_00810758 and D_008107D8 are canonical progress bytes,
 * D_008106B8 the request byte B8; its player D_008102B0 is the live player
 * record (+0x00, +0x0F, +0x70..+0x7C, +0xA0..+0xA8, +0x224). The exit box
 * (the player at z < 156 inside its x/y box, record [2] only, fast arm or
 * slow) sets D_008107D8 |= 0x80 (Roger's departure; 001B0C60(1, 1, 4) when
 * D_00810758 == 0xFF); the hit box (156 <= z < 166.5, fast arm) writes the
 * player hit the player stage's 0021C440 consumes. Neither box is on the
 * level smoke's route (it ends at Roger's encounter). */
typedef struct {
    EmActor *actor;
    int freed;
} FanCall;

static int fan_001B0FD0(void *ctx, const EmFanOriginal *fan)
{
    (void)fan;
    FanCall *c = ctx;
    int32_t r = 0;
    if (em_area11_boxes_owner_001B0FD0(c->actor, s_pool, &r) < 0)
        return -1;
    if (r == 0)
        em_pickup_prop_retire(c->actor->pos);
    return r;
}

static int fan_001FBD50(void *ctx, int32_t cue, int32_t a2, float range)
{
    FanCall *c = ctx;
    if (a2 != 0)
        return gun_report("the fan's 001FBD50 with a nonzero a2 (the flat cue)");
    em_sfx_play_at((unsigned)cue, c->actor->pos, range);
    return 0;
}

/* 001C6380 after the module stored +0xC8 (rot.z, the Z leg of its TRS). */
static int fan_001C6380(void *ctx, const EmFanOriginal *fan)
{
    FanCall *c = ctx;
    c->actor->rot[2] = fan->rot_z;
    return em_area11_boxes_owner_001C6380(c->actor, NULL);
}

static int fan_001B17A0(void *ctx)
{
    FanCall *c = ctx;
    GunCall g;
    memset(&g, 0, sizeof g);
    g.actor = c->actor;
    return gun_001B17A0(&g);
}

static int fan_001B0C60(void *ctx, int32_t a0, int32_t a1, int32_t a2)
{
    (void)ctx;
    return em_scene_request_area_change_001B0C60(a0, a1, a2);
}

static int fan_draw(void *ctx)
{
    FanCall *c = ctx;
    return em_area11_boxes_owner_draw(c->actor);
}

static int fan_001AFC10(void *ctx)
{
    FanCall *c = ctx;
    if (em_actor_pool_free_001AFC10(s_pool, s_scene, c->actor) < 0)
        return -1;
    c->freed = 1;
    return 0;
}

static int tick_fan(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)world;
    FanCall c = {actor, 0};
    uint8_t *e788 = em_scene_progress_at(s_scene, 0x00810788u, 1);
    uint8_t *e758 = em_scene_progress_at(s_scene, 0x00810758u, 1);
    uint8_t *e7D8 = em_scene_progress_at(s_scene, 0x008107D8u, 1);
    if (!e788 || !e758 || !e7D8)
        return fault(EM_FAN_ORIGINAL_CALLBACK, EM_SCENE_FAULT_BAD_INDEX, "the fan's progress bytes are not canonical");
    EmFanOriginalGlobals globals = {*e788, s_scene->req[EM_SCENE_REQ_B8], *e758, *e7D8};
    EmPlayerLiveActor *pl = player_states_actor_mut();
    EmFanOriginalPlayer player;
    memset(&player, 0, sizeof player);
    if (pl) {
        player.b00 = pl->bytes[0x00];
        player.b0F = pl->bytes[0x0F];
        memcpy(player.f70, pl->bytes + 0x70, sizeof player.f70);
        memcpy(player.pos, pl->bytes + 0xA0, sizeof player.pos);
        memcpy(&player.f224, pl->bytes + 0x224, sizeof player.f224);
    }
    EmFanOriginal fan = {actor->u04[0], actor->u04[1], node->h28, actor->flags2, node->f38, actor->rot[2], 0};
    const EmFanOriginalWorkers w = {&c, fan_001B0FD0, fan_001FBD50, fan_001C6380, fan_001B17A0,
                                    fan_001B0C60, fan_draw, fan_001AFC10};
    EmFanOriginalFault f = {0, 0};
    /* `player` is read only by record [2]'s box (flags2 1, B8 0); without a
     * player record the module faults there. */
    if (em_fan_original_tick(&fan, pl ? &player : NULL, &globals, &w, &f) < 0) {
        if (em_scene_faulted(s_scene))
            return -1;
        return fault(f.address, (EmSceneFaultCode)f.code, "fan 00827630: a worker failed (em_fan_original)");
    }
    *e7D8 = globals.d8107D8;
    if (pl) {
        pl->bytes[0x00] = player.b00;
        pl->bytes[0x0F] = player.b0F;
        memcpy(pl->bytes + 0x70, player.f70, sizeof player.f70);
        memcpy(pl->bytes + 0x224, &player.f224, sizeof player.f224);
    }
    if (c.freed)
        return 1;
    actor->u04[0] = fan.lifecycle;
    actor->u04[1] = fan.phase;
    node->h28 = fan.timer;
    node->f38 = fan.spin;
    actor->rot[2] = fan.rot_z;
    return 1;
}

int em_area11_bindings_gun_fan_log(const EmActor *actor, EmArea11GunFanLog *out)
{
    const Node *node = node_of(actor);
    if (!node || !node->binding || !out)
        return 0;
    const uint32_t cb = actor->callback;
    if (cb != 0x00825940u && cb != 0x00827490u && cb != 0x00827630u)
        return 0;
    memset(out, 0, sizeof *out);
    out->address = address_of(actor);
    out->callback = cb;
    out->b00 = actor->status;
    out->b04 = actor->u04[0];
    out->b05 = actor->u04[1];
    out->b09 = actor->bones;
    out->h28 = node->h28;
    out->h34 = node->h34;
    out->h36 = actor->h36;
    memcpy(&out->f38, &node->f38, 4);
    memcpy(out->rot, actor->rot, sizeof out->rot);
    if (cb == 0x00825940u) {
        const EmOwnerBone *bone3 = em_area11_boxes_owner_slot(actor, 3, NULL);
        if (bone3)
            memcpy(&out->bone3_78, &bone3->rot[2], 4);
        out->w220 = gun_word(actor, 0x220);
        for (EmActor *p = s_pool->head; p; p = p->next)
            if (address_of(p) == out->w220 && out->w220) {
                const Node *lamp = node_of(p);
                if (lamp)
                    memcpy(out->lamp_a0, lamp->a0, sizeof out->lamp_a0);
            }
    }
    return 1;
}

/* 001BC350, the fence door (census L18): its original owner
 * (em_area11_door: 001BC350 with 001BBE40 and the ELF program 0x24DE40 on
 * the AREA11 script host, 001BC150's room move through B8) in both walk
 * variants (class 5 is walked by 001AFD70 modes 0 and 1). The side-1
 * arrival's walk-out (001B07C0(1) writes 5/1/0 at entry 1) is the player's
 * own stage: 0015B610 / 00183250 (em_player_floor.c). */
static int tick_door(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)node;
    (void)world;
    if (em_area11_door_tick(actor) < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                                                 "fence door owner: a worker failed (em_area11_door)");
    return 1;
}

/* 008235F0, the flame (area11[7]): em_area11_effect's controller; its
 * DRAW is 001D04B0 on em_effects_live (the packets go into the chain page,
 * docs/AREA11_EFFECT.md "Binding"). */
static int tick_effect(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)node;
    (void)world;
    if (em_area11_effect_runtime_tick() < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(em_effects_live_fault() ? em_effects_live_fault() : actor->callback,
                                                 EM_SCENE_FAULT_WORKER_FAILED,
                                                 "flame owner: 001D04B0 faulted (em_effects_live)");
    return 1;
}

/* Roger 008237E0 (area11[8]) and the equipment node 001C5C90 (area11[9])
 * on their original owners (em_area11_roger, census L22;
 * ROGER_ACTOR_ORIGINAL.md "Binding"), in both walk variants (class 0x0A
 * and the equipment's class are walked by 001AFD70 modes 0 and 1). Roger's
 * lifecycle 0 runs 001BA8E0, whose 001F0120(Roger, 0x47) spawns the head
 * sprite node (em_area11_bindings_spawn_001F0120). */
static int tick_roger(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)node;
    (void)world;
    int r = actor->callback == 0x008237E0u ? em_area11_roger_tick(actor, s_pool, s_scene)
                                           : em_area11_roger_equipment_tick(actor, s_pool, s_scene);
    if (r < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                                                 "Roger owner: a worker failed (em_area11_roger)");
    return 1;
}

/* 008257A0 (area11[13]): em_manager_008257A0 (S12a). The node's +0 and +4
 * are EmActor.status and u04[0]; D_00810794 and D_00810788 are canonical D2
 * progress bytes. States 2 and 3 free the node itself (001AFC10); the pool
 * walk continues with the next node it saved. State 1 (event 0x30 set) is
 * the untranslated script arm and faults. */
typedef struct {
    EmActor *self;
    int freed;
} ManagerFree;

static int manager_free_001AFC10(void *ctx)
{
    ManagerFree *f = ctx;
    if (em_actor_pool_free_001AFC10(s_pool, s_scene, f->self) < 0)
        return -1;
    f->freed = 1;
    return 0;
}

static int tick_manager_8257A0(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)node;
    (void)world;
    const uint8_t *e794 = em_scene_progress_at(s_scene, 0x00810794u, 1);
    const uint8_t *e788 = em_scene_progress_at(s_scene, 0x00810788u, 1);
    if (!e794 || !e788)
        return fault(EM_MANAGER_008257A0, EM_SCENE_FAULT_BAD_INDEX, "event bytes not canonical");
    EmManager8257A0 m = {actor->status, actor->u04[0], *e794, *e788};
    ManagerFree f = {actor, 0};
    EmManager8257A0Workers w = {&f, manager_free_001AFC10};
    uint32_t at = 0;
    if (em_manager_008257A0_tick(&m, &w, &at) < 0) {
        if (em_scene_faulted(s_scene))
            return -1;
        return fault(at, at == EM_MANAGER_008257A0 ? EM_SCENE_FAULT_NULL_WORKER : EM_SCENE_FAULT_WORKER_FAILED,
                     at == EM_MANAGER_008257A0 ? "008257A0 state 1 (script arm) is not translated (WP-10)"
                                               : "008257A0: 001AFC10 failed");
    }
    if (!f.freed) {
        actor->status = m.b00;
        actor->u04[0] = m.b04;
    }
    return 1;
}

/* 00823E80, the opening controller (area11[10]; overlay AREA11, runtime
 * 0x823E80..0x823FE8), by its +0x04. State 0: 001B0FD0 (0x823ECC,
 * em_area11_boxes_owner_001B0FD0: the world bank's model 0x11, the
 * parachute canopy, and its slot; a refusal returns), 001C6380 (0x823EDC),
 * +0x04 = 1 and +0x00 = 1. State 1: the script machine
 * (em_area11_opening_state1: 001BA1C0(self, 0x39); +0x05 0 starts the
 * script 0x828FC0 on the AREA11 script host and stops the streams; +0x05 1
 * polls it and, when it ends, runs the completion: +0x2E = 0xFFFF,
 * D_00810811 = 0xFF, 001C4760(0, 1), 001FAE70(0), +0x05 = 2, 001AEE10(4,
 * 0)), then on every path 001B1B70(self) (0x823FAC: the record onto the
 * collision world's class lists, its cell uid 3) and its +0x4C (0x823FB8),
 * 001CAA00 over its record (em_area11_boxes_owner_draw): the canopy keeps
 * drawing after the script ends. States 2 / 3 free the record (0x823FCC):
 * not reached in the first level, a fault. The script's actors are pool
 * records (001BAC00 spawns them; tick_opening_actor), the player's its own
 * stage's (the script host's takeover). */
typedef struct {
    EmActor *self;
} OpeningCall;

static int opening_001BA1C0(void *ctx, uint32_t a1, int32_t *result)
{
    (void)ctx;
    const uint8_t *flag = em_scene_progress_at(s_scene, 0x00810758u + a1, 1);
    if (!flag)
        return -1;
    *result = *flag == 0xFF;
    return 0;
}

static int opening_001BA1A0(void *ctx, uint32_t entry)
{
    OpeningCall *c = ctx;
    return em_area11_script_host_start(c->self, entry);
}

static int opening_001BA1F0(void *ctx, int32_t *result)
{
    OpeningCall *c = ctx;
    return em_area11_script_host_tick(c->self, result);
}

static int opening_001FABB0(void *ctx)
{
    (void)ctx;
    return em_scene_bindings_001FABB0();
}

static int opening_00810811(void *ctx, uint8_t value)
{
    (void)ctx;
    g.opening_complete = value;   /* D_00810811 (em_game.h) */
    return 0;
}

static int opening_001C4760(void *ctx, int32_t a0, int32_t a1)
{
    (void)ctx;
    /* The canonical key byte D_00810CC3[a0]. */
    return em_director_original_001C4760_scene(s_scene, a0, a1);
}

static int opening_001FAE70(void *ctx, int32_t a0)
{
    (void)ctx;
    return em_scene_bindings_001FAE70(a0);
}

static int opening_001AEE10(void *ctx, int16_t a0, uint8_t a1)
{
    (void)ctx;
    em_frame_fade_start_colour(-1, a0, a1);
    return 0;
}

static int tick_opening(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)node;
    (void)world;
    if (actor->u04[0] == 0) {
        int32_t refused = 0;
        if (em_area11_boxes_owner_001B0FD0(actor, s_pool, &refused) < 0)
            return fault(0x001B0FD0u, EM_SCENE_FAULT_WORKER_FAILED, "00823E80 state 0: 001B0FD0 faulted");
        if (!refused) {
            if (em_area11_boxes_owner_001C6380(actor, NULL) < 0)
                return fault(0x001C6380u, EM_SCENE_FAULT_WORKER_FAILED, "00823E80 state 0: 001C6380 faulted");
            actor->u04[0] = 1;   /* +0x04 */
            actor->status = 1;   /* +0x00 */
            em_pickup_prop_retire(actor->pos);
        }
        return 1;
    }
    if (actor->u04[0] != 1)
        return fault(actor->callback, EM_SCENE_FAULT_BAD_INDEX, "00823E80: the free of states 2 / 3 is not bound");
    OpeningCall call = {actor};
    const EmArea11OpeningWorkers w = {&call,           opening_001BA1C0, opening_001BA1A0, opening_001BA1F0,
                                      opening_001FABB0, opening_00810811, opening_001C4760, opening_001FAE70,
                                      opening_001AEE10};
    EmArea11Opening op = {actor->u04[1], actor->flags2};
    const uint8_t before = op.b05;
    uint32_t at = 0;
    if (em_area11_opening_state1(&op, &w, &at) < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(at, EM_SCENE_FAULT_WORKER_FAILED, "00823E80 state 1: a callee faulted");
    actor->u04[1] = op.b05;   /* +0x05 */
    actor->flags2 = op.h2E;   /* +0x2E */
    if (before == 1 && op.b05 == 2)
        em_opening_runtime_complete();
    if (em_collision_world_publish_001B1B70(actor) < 0)
        return fault(0x001B1B70u, EM_SCENE_FAULT_WORKER_FAILED, "00823E80: 001B1B70 faulted");
    if (em_area11_boxes_owner_draw(actor) < 0)
        return fault(0x001CAA00u, EM_SCENE_FAULT_WORKER_FAILED, "00823E80: its +0x4C 001CAA00 faulted");
    return 1;
}

/* The opening script's actors (callback 001BB0E0, spawned by 001BAC00 in
 * the controller's script; em_area11_roger_opening_tick): the entry their
 * +0x20 names from the opening's image, their +0x24 the controller. They
 * tick in both walk variants (classes 9 and 8: 001AFD70 modes 0 and 1). */
static int tick_opening_actor(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)world;
    const uint8_t *entry = em_area11_script_host_opening_bytes(node->entry_20, EM_SDF_ENTRY_SIZE);
    if (!entry || !node->parent)
        return fault(actor->callback, EM_SCENE_FAULT_BAD_INDEX, "001BB0E0: no placement entry or controller");
    int r = em_area11_roger_opening_tick(actor, s_pool, s_scene, entry, node->entry_20, node->parent);
    if (r < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                                                 "opening actor: a worker failed (em_area11_roger)");
    return 1;
}

/* 008253F0 (area11[12], #21): the director manager, live since WP-8b.
 *
 * The original adapter: em_director_original (census L21). Its +0 / +4 / +5 are
 * EmActor.status and u04[0..1]; D_00810813, D_00810793 and D_00810CC3[] are
 * canonical D2 bytes, D_008106B0 / B1 the request block, D_00810350 the
 * player's +0xA0 (g.pos, as for Roger's trigger); the quads come from the
 * visit's director_quads.emsc and 001B1EA0's 0011E620 is the one bound SDK
 * atan2f (em_sdk_math_original over the collision world's SDK context).
 * Its scripts 0x8294C0 / 0x829A40 / 0x829CC0 run on em_area11_script_host
 * (001BA1A0 / 001BA1F0 on the node's +0x1F0 block); 001AFC10 frees the
 * node (states 2 / 3; the pool walk continues with the node it saved). It
 * ticks in both walk variants, as class 9 does in 001AFD70(0) and (1). */
typedef struct {
    EmActor *self;
    uint32_t address;
    int freed;
} Director;

static int director_atan2(void *ctx, float y, float x, float *result)
{
    EmSdkMathContext *sdk = ctx;
    *result = em_sdk_math_original_float_0011E620(sdk, y, x);
    return sdk->fault ? -1 : 0;
}

static int director_001BA1A0(void *ctx, uint32_t block, uint32_t entry)
{
    Director *d = ctx;
    if (block != d->address + EM_DIRECTOR_ORIGINAL_SCRIPT_BLOCK) return -1;
    return em_area11_script_host_start(d->self, entry) < 0 ? -1 : 0;
}

static int director_001BA1F0(void *ctx, uint32_t self, int32_t *result)
{
    Director *d = ctx;
    if (self != d->address) return -1;
    return em_area11_script_host_tick(d->self, result) < 0 ? -1 : 0;
}

static int director_001AFC10(void *ctx, uint32_t self)
{
    Director *d = ctx;
    if (self != d->address || em_actor_pool_free_001AFC10(s_pool, s_scene, d->self) < 0) return -1;
    d->freed = 1;
    return 0;
}

static int tick_director_original(EmActor *actor)
{
    EmSdkMathContext *sdk = em_collision_world_sdk();
    if (!sdk)
        return fault(EM_DIRECTOR_ORIGINAL_OWNER, EM_SCENE_FAULT_NULL_WORKER,
                     "008253F0: 001B1EA0's 0011E620 needs the collision world's SDK context");
    EmDirectorOriginalWorld w;
    memset(&w, 0, sizeof w);
    w.d810813 = em_scene_progress_at(s_scene, 0x00810813u, 1);
    w.d810793 = em_scene_progress_at(s_scene, 0x00810793u, 1);
    w.d810350 = g.pos;
    w.d810CC3 = em_scene_progress_at(s_scene, 0x00810CC3u, 2);
    w.d8106B0 = em_scene_req_at(s_scene, 0x008106B0u);
    w.d8106B1 = em_scene_req_at(s_scene, 0x008106B1u);
    if (!w.d810813 || !w.d810793 || !w.d810CC3 || !w.d8106B0 || !w.d8106B1)
        return fault(EM_DIRECTOR_ORIGINAL_OWNER, EM_SCENE_FAULT_BAD_INDEX,
                     "008253F0: D_00810813 / D_00810793 / D_00810CC3 / D_008106B0 not canonical");
    /* The quads are read only by a beat's gate (+5 = 0 past the height
     * test); a missing export faults there, at the quad's address. */
    const float (*quad[3])[4] = {NULL, NULL, NULL};
    if (em_area11_script_host_director_quads(quad) == 0)
        for (int i = 0; i < 3; ++i) w.quad[i] = quad[i];
    w.atan2 = director_atan2;
    w.atan2_ctx = sdk;
    Director d = {actor, em_actor_pool_address(s_pool, actor), 0};
    EmDirectorOriginalWorkers k = {&d, director_001BA1A0, director_001BA1F0, director_001AFC10};
    EmDirectorOriginalNode n = {&actor->status, &actor->u04[0], &actor->u04[1], d.address};
    uint32_t at = 0;
    if (em_director_original_tick(&n, &w, &k, &at) < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(at, at == 0x001BA1A0u || at == 0x001BA1F0u || at == 0x001AFC10u
                                                         ? EM_SCENE_FAULT_WORKER_FAILED
                                                         : EM_SCENE_FAULT_NULL_WORKER,
                                                 "008253F0 (em_director_original) faulted");
    return 1;
}

static int tick_director(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)node;
    (void)world;
    return tick_director_original(actor);
}

/* The truck 00823FF0 (#24) and its camera trigger 008251E0 (#25) on their
 * original owners (em_area11_boxes, census L23; TRUCK_ORIGINAL.md
 * "Binding"), in both walk variants (the original walks their class in
 * both). A node that frees itself (state 3) returns through 001AFC10. */
static int tick_truck(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)node;
    (void)world;
    int r = actor->callback == 0x00823FF0u ? em_area11_boxes_truck_tick(actor, s_pool, s_scene)
                                           : em_area11_boxes_trigger_tick(actor, s_pool, s_scene);
    if (r < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                                                 "truck owner: a worker failed (em_area11_boxes)");
    return 1;
}

/* 00159210 panel (area11[18]), by its +0x04 (decomp src/func_00159210.c).
 * State 0: 001B0FD0 (em_area11_boxes_owner_001B0FD0: the world bank's model
 * 0x04 and its bone slot; a refusal leaves the record in state 0), then
 * 001C6380 over the record and its slot, then the child (see the file
 * comment); the powered bit is D_00810841[0x0B] bit (+0x2E), the canonical
 * D_0081084C bit 7 (em_game_terminal_powered), so any other bit faults.
 * State 1 is the original owner in the AREA11 interaction host (WP-4):
 * em_area11_interaction_host_panel_tick runs 00159210/00157860 and its tail,
 * 001B17A0 (census L07: the record bound at state 0 goes onto the collision
 * world's lists, its cell uid 18) and the +0x4C 001CAA00 (the host's draw
 * hook, em_area11_boxes_owner_draw), in both variants. grate_update binds
 * cell 18 into the port's own collision world (em_props.c) for the port's
 * own queries (player movement, follow camera) that still use it. The
 * panel's states 2 and 3 (+0x04 += 1, then 001AFC10) are not reached: the
 * owner never leaves state 1 in the first level, and a record in another
 * state faults. */
static int tick_panel(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)world;
    grate_update();
    if (actor->u04[0] == 0) {
        if (!node->ticked) {
            em_area11_interaction_host_set_panel_address(address_of(actor));
            /* Census L07: the record its 001B17A0 publishes (cell uid 18). */
            if (em_area11_interaction_host_bind_actor(actor->source_id, actor) < 0)
                return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                             "00159210 state 0: the interaction host failed");
        }
        int32_t refused = 0;
        if (em_area11_boxes_owner_001B0FD0(actor, s_pool, &refused) < 0)
            return fault(0x001B0FD0u, EM_SCENE_FAULT_WORKER_FAILED, "00159210 state 0: 001B0FD0 faulted");
        if (refused)
            return 1;
        if (em_area11_boxes_owner_001C6380(actor, NULL) < 0)
            return fault(0x001C6380u, EM_SCENE_FAULT_WORKER_FAILED, "00159210 state 0: 001C6380 faulted");
        /* 00159210 state 0: model 0x2C takes 0x700038A0 = (0, 1.0, 0, 1.0)
         * and 001C5570(p, .., 0x74, 1); otherwise, unless the power bit is
         * set, (1.0, 0, 0, 1.0) and 001C5570(p, .., 0x75, 1); +0x20 = the
         * child. */
        static const float k_green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
        static const float k_red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        if (actor->model == 0x2C) {
            if (spawn_001C5570_child(actor, k_green, 0x74, 1, &s_panel_child) < 0)
                return -1;
        } else {
            if (s_scene->d810700 != 0x0B || actor->flags2 != 7)
                return fault(actor->callback, EM_SCENE_FAULT_BAD_INDEX,
                             "00159210 state 0 reads a D_00810841 bit the port does not store");
            if (!em_game_terminal_powered() &&
                spawn_001C5570_child(actor, k_red, 0x75, 1, &s_panel_child) < 0)
                return -1;
        }
        return 1;
    }
    if (actor->u04[0] != 1)
        return fault(actor->callback, EM_SCENE_FAULT_BAD_INDEX, "00159210 states 2 / 3 are not bound");
    if (em_area11_interaction_host_panel_tick() < 0)
        return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED, "00159210: the interaction host failed");
    return 1;
}

/* 0x827B10 terminal and elevator (area11[19]), by its +0x04 (overlay AREA11,
 * runtime 0x827B10..0x828040). State 0: 001B0FD0 (0x827BC8,
 * em_area11_boxes_owner_001B0FD0: the world bank's model 0x0F and its bone
 * slot; a refusal leaves +0x04 = 3 and returns), +0x00 = 1, +0x08 = 1,
 * +0x30 = 0x0082AB10 (its use descriptor), then the floor placement
 * (D_0081083A -> +0xB4 190/230, 001C6380, 001A2370; the host's
 * em_area11_interaction_host_elevator_state0), then its 001C5760 child,
 * allocated inline (0x827C18..0x827C8C: +0xD 0x10, +0xA0 = (1.0, 0, 0,
 * 0.25), stored at its +0x2E4); the owner reads D_00810841[0x0B] bit
 * (+0x2E), which only bit 7 of the canonical D_0081084C stores. State 1 is
 * the original owner in the AREA11 interaction host (WP-4): refusal 0x82A990
 * or powered 0x82A750 with the carry 00828050 (its 001C6380 per tick), the
 * phase-1 copy of its node matrix into the child's slot (0x827E6C), its
 * 001B17A0 publication and its +0x4C 001CAA00 and the +0x28 level step
 * (em_elevator_tick), in both variants; then the tail 0x827EAC writes the
 * child's colour (em_indicator_00827B10_colour). +0x04 == 3 (and any state
 * other than 0 / 1) frees the record in the original (0x827B30 / 0x828024):
 * not reached in the first level (the host keeps the record), so it faults.
 * The legacy em_examine terminal and the legacy ride it ran were retired in
 * WP-4. */
static int tick_terminal(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)world;
    if (actor->u04[0] == 0) {
        if (s_scene->d810700 != 0x0B || actor->flags2 != 7)
            return fault(actor->callback, EM_SCENE_FAULT_BAD_INDEX,
                         "00827B10 reads a D_00810841 bit the port does not store");
        if (!node->ticked && em_area11_interaction_host_bind_actor(actor->source_id, actor) < 0)
            return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                         "00827B10 state 0: the interaction host failed");
        int32_t refused = 0;
        if (em_area11_boxes_owner_001B0FD0(actor, s_pool, &refused) < 0)
            return fault(0x001B0FD0u, EM_SCENE_FAULT_WORKER_FAILED, "00827B10 state 0: 001B0FD0 faulted");
        if (refused)
            return 1;
        actor->status = 1;          /* +0x00 */
        actor->u04[4] = 1;          /* +0x08 */
        actor->w30 = 0x0082AB10u;   /* +0x30 */
        /* 0x827B54..0x827BF0 (+0xB4 from D_0081083A), 0x827BF0 001C6380 and
         * 0x827C04 001A2370 (over the record bound here: census L07), before
         * the child spawn at 0x827C18. */
        if (em_area11_interaction_host_elevator_state0() < 0)
            return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                         "00827B10 state 0: the interaction host failed");
        static const float k_unpowered[4] = {1.0f, 0.0f, 0.0f, 0.25f};
        if (spawn_child_record(actor, k_unpowered, 0x10, 0x001C5760u, 0, &node->child) < 0)
            return -1;
        return 1;
    }
    if (actor->u04[0] != 1)
        return fault(actor->callback, EM_SCENE_FAULT_BAD_INDEX,
                     "00827B10: the free of a record in state 3 (or another state) is not bound");
    if (em_area11_interaction_host_elevator_tick() < 0)
        return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED, "00827B10: the interaction host failed");
    /* 0x827EAC: the child's colour from the +0x28 level, which the host's
     * em_elevator_tick stepped after the publication. A refused child
     * alloc leaves +0x2E4 = 0, through which the original would store. */
    const EmElevatorRuntime *elevator = em_area11_interaction_host_elevator();
    Node *child = node->child ? node_of(node->child) : NULL;
    uint32_t spad3A20 = 0;
    if (!elevator || !child ||
        em_indicator_00827B10_colour(elevator->owner.indicator_level, child->a0, &spad3A20) < 0)
        return fault(actor->callback, EM_SCENE_FAULT_NULL_WORKER, "00827B10 tail: no child at +0x2E4");
    /* 0x70003A20 = level / 128 goes into the player closure's copy of the
     * word only (em_player_closure_live.h). */
    if (elevator->owner.indicator_level != 0)
        em_player_closure_live_store_3A20(spad3A20);
    return 1;
}

/* 0x827E60..0x827E70 (the host's copy_child worker): 00102958(child
 * +0x110[0] + 0x90, self +0x110[0] + 0x90), the terminal's node 0 matrix
 * into its +0x2E4 child's slot 0. The child's bind (001C22A0) ran in its
 * own first call, long before any phase-1 call; a child without a slot
 * faults (the original would read address 0 + 0x110). */
static int terminal_copy_child(EmActor *terminal)
{
    Node *node = node_of(terminal);
    float matrix[16];
    if (!node || !node->child)
        return fault(0x00827B10u, EM_SCENE_FAULT_NULL_WORKER, "0x827E6C: no child at +0x2E4");
    if (em_area11_boxes_owner_node(terminal, 0, matrix) < 0 ||
        em_indicator_bind_live_set_node(node->child, 0, matrix) < 0)
        return fault(0x00102958u, EM_SCENE_FAULT_WORKER_FAILED, "0x827E6C: a slot +0x90 is missing");
    return 0;
}

/* 001C4820, the placed prop (area11[20], per-area model 0x04): the one
 * translation em_sul_001C4820 (byte-matched decomp src/func_001C4820.c) over
 * the record's +0x04 and +0x4C. State 0: 001B0FD0 (the world bank's model
 * and its slot; em_area11_boxes_owner_001B0FD0), then 001C6380 unless it
 * refused; state 1: 001B17A0 (the interaction host's services: 001B1630 on
 * the camera, 001B1B70 onto the collision world's class lists), then the
 * +0x4C, 001CAA00 (em_area11_boxes_owner_draw) whatever 001B17A0 found;
 * states 2 / 3: 001AFC10. The legacy prop instance the manifest placed at
 * its +0xB0 stops drawing once its owner is bound. */
typedef struct {
    EmActor *actor;
    int freed;
} PropCall;

static int prop_model_bind(void *context, uint8_t *image, int32_t *result)
{
    PropCall *c = context;
    if (em_area11_boxes_owner_001B0FD0(c->actor, s_pool, result) < 0) return -1;
    image[4] = c->actor->u04[0];
    if (*result == 0) em_pickup_prop_retire(c->actor->pos);
    return 0;
}

static int prop_place(void *context, uint8_t *image)
{
    (void)image;
    PropCall *c = context;
    return em_area11_boxes_owner_001C6380(c->actor, NULL);
}

static int prop_publish(void *context, uint8_t *image)
{
    (void)image;
    PropCall *c = context;
    EmOwnerServicesOwner view;
    memset(&view, 0, sizeof view);
    view.cls = c->actor->cls;
    view.kind = c->actor->model;
    view.model_id = c->actor->param;
    view.flags2 = c->actor->flags2;
    memcpy(view.pos, c->actor->pos, sizeof view.pos);
    return em_area11_interaction_host_offer_001B17A0(c->actor, &view) < 0 ? -1 : 0;
}

static int prop_method(void *context, uint8_t *image, uint32_t method)
{
    (void)image;
    PropCall *c = context;
    return method == 0x001CAA00u ? em_area11_boxes_owner_draw(c->actor) : -1;
}

static int prop_free(void *context, uint8_t *image)
{
    (void)image;
    PropCall *c = context;
    if (em_actor_pool_free_001AFC10(s_pool, s_scene, c->actor) < 0) return -1;
    c->freed = 1;
    return 0;
}

static int tick_prop_001C4820(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)node;
    (void)world;
    PropCall call = {actor, 0};
    EmSulWorkers w;
    memset(&w, 0, sizeof w);
    w.context = &call;
    w.model_bind = prop_model_bind;
    w.place = prop_place;
    w.publish = prop_publish;
    w.method = prop_method;
    w.free_actor = prop_free;
    uint8_t image[0x50];
    memset(image, 0, sizeof image);
    image[4] = actor->u04[0];
    uint32_t model = 0, method = 0;
    if (em_area11_boxes_owner_state(actor, &model, &method))
        memcpy(image + 0x4C, &method, 4);
    if (em_sul_001C4820(&w, image, sizeof image) < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                                                 "001C4820: a worker failed (em_area11_boxes / the host)");
    return 1;
}

/* The host owners' record services (em_area11_interaction_host.h). */
static int owner_place(EmActor *actor, float world[16])
{
    return em_area11_boxes_owner_001C6380(actor, world) < 0
               ? fault(0x001C6380u, EM_SCENE_FAULT_WORKER_FAILED, "001C6380 faulted (a host owner)")
               : 0;
}

static int owner_bind_001B0FD0(EmActor *actor, int32_t *ret)
{
    return em_area11_boxes_owner_001B0FD0(actor, s_pool, ret) < 0
               ? fault(0x001B0FD0u, EM_SCENE_FAULT_WORKER_FAILED, "001B0FD0 faulted (a host owner)")
               : 0;
}

static int owner_bind_001B1020(EmActor *actor, uint32_t a1, int32_t a2, int32_t a3, int32_t *ret)
{
    return em_area11_boxes_owner_001B1020(actor, s_pool, a1, a2, a3, ret) < 0
               ? fault(0x001B1020u, EM_SCENE_FAULT_WORKER_FAILED, "001B1020 faulted (a host owner)")
               : 0;
}

static int owner_draw(EmActor *actor)
{
    return em_area11_boxes_owner_draw(actor) < 0
               ? fault(0x001CAA00u, EM_SCENE_FAULT_WORKER_FAILED, "001CAA00 faulted (a host owner)")
               : 0;
}

/* 001EA240 (the puff driver) and 001E2560 (the head-bone sprite): the
 * effect originals over the node's own record (em_effects_live, census L26 /
 * L27 / L39), in every walk mode (class 0xC). A node that frees itself
 * (state 2 / 3) returns through 001AFC10. */
static int tick_effect_node(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)node;
    (void)world;
    if (em_effects_live_tick(actor) < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(em_effects_live_fault() ? em_effects_live_fault() : actor->callback,
                                                 EM_SCENE_FAULT_WORKER_FAILED,
                                                 "effect node: a translation faulted (em_effects_live)");
    return 1;
}

/* 0018A6B0 x7, the player's equipment nodes (em_equipment_live, census
 * L28), walked in mode 0 and, as class 1, in mode 2 of the cutscene
 * variant. A node that frees itself (lifecycle 3) returns through
 * 001AFC10. */
static int tick_equipment(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)node;
    (void)world;
    if (em_equipment_live_tick(actor) < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(em_equipment_live_fault() ? em_equipment_live_fault()
                                                                           : actor->callback,
                                                 EM_SCENE_FAULT_WORKER_FAILED,
                                                 "player equipment: a translation faulted (em_equipment_live)");
    return 1;
}

/* Free the node's own actor from inside its behaviour (001AFC10(self)); the
 * pool walk continues with the next node it saved. */
static int free_self_001AFC10(EmActor *actor)
{
    if (em_actor_pool_free_001AFC10(s_pool, s_scene, actor) < 0)
        return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED, "001AFC10 failed");
    return 1;
}

/* 001E55F0 weather node: em_weather's translation over the node's own state;
 * the snow runtime writes 001E67C0's tile requests (001CFFE0) into the
 * render context's channel 3 and closes the list at context +0x2520, which
 * the frame close's 001E0D70 CALLs into the chain page. The cutscene block passed the camera eye
 * from before this frame's camera stage (copied at its start); the gameplay
 * block passed the live eye. The room move (S12b): at the end of state 1,
 * D_008106B8 == 2 with D_0028A9A0 == 2 selects state 3; the next call frees
 * the actor (the 001C1DC0 of 0x1AE040 state 4 has spawned its successor). */
static int tick_weather(EmActor *actor, Node *node, const EmArea11World *world)
{
    int released = em_snow_runtime_tick_actor(
        &node->weather, world->cutscene ? s_walk_eye : g.cam.eye, world->cutscene ? 1u : 0u,
        s_scene->req[EM_SCENE_REQ_B8], (unsigned)(int)em_frame_transition()->substate);
    if (released < 0)
        return fault(0x001E55F0u, EM_SCENE_FAULT_WORKER_FAILED,
                     "weather: a render-context call or packet write of 001E55F0 / 001E67C0 faulted");
    return released ? free_self_001AFC10(actor) : 1;
}

/* 001C5930 area-title node: its lifecycle (evidence above); the card itself
 * is the legacy em_hud title (armed by the manifest `areatitle` at the area
 * read, and by 0x1AE040 state 4's 001C5C50 adapter). State 0's +0x28/+0x2A/
 * +0x1F0 stores and the 001C5860 sub-location line have no port storage. */
static int tick_area_title(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)node;
    (void)world;
    switch (actor->u04[0]) {
    case 0:
        actor->u04[0] = 1;
        return 1;
    case 1:
        if (s_scene->req[EM_SCENE_REQ_B8] != 0)
            actor->u04[0] = 3;
        return 1;
    case 2:
    case 3:
        em_hud_area_title_stop();
        return free_self_001AFC10(actor);
    default:
        return 1;
    }
}

/* Indicator children (001C5680 x8, 001C5760; ORIGINAL_FRAME_ORDER #39-#48):
 * each node runs its own behaviour (em_indicator_child_step) in walk order,
 * so each draws its one 001F54E0 (one 00122BB8 value) where the original
 * does. The workers: */
typedef struct {
    EmActor *actor;
    Node *node;
} IndicatorCall;

/* 001C2360 / 001C22A0: the model and bone-slot bind, the translations
 * em_rvr_001C2360 / em_rvr_001C22A0 over the child's record, its model bank
 * (D_0028A56C / *D_0028A59C) and the one bone-slot stack
 * (em_indicator_bind_live); +0x4C becomes 001CACB0 (001CA5F0 mode 2). A
 * nonzero *result (the bone cap) keeps the child in state 0. */
static int indicator_init(void *ctx, uint32_t fn, int32_t *result)
{
    IndicatorCall *c = ctx;
    return em_indicator_bind_live_bind(c->actor, fn, result);
}

/* 001C6380 over the child's +0xB0 / +0xC0 / +0x60 (the spawn copied the
 * owner's) and its bound slots (em_indicator_bind_live). The port's +0x4C
 * draw is still a stand-in: the child's model mesh, additive, at the
 * child's own node 0 (indicator_draw below; OWNER_DRAW.md section 11). */
static int indicator_place(void *ctx)
{
    IndicatorCall *c = ctx;
    return em_indicator_bind_live_place(c->actor);
}

static int indicator_rand(void *ctx, int32_t *v0)
{
    (void)ctx;
    *v0 = (int32_t)em_random_next();
    return 0;
}

/* The +0x4C method 001CACB0, called by 001F54E0 with the child's new +0x80.
 * 001CABA0 (its packet builder: channel 3, lighting mode 1, 001D3990 /
 * 001D3D90 and the 001CAAC0 depth sort into page D_007635C0) is not
 * translated: the stand-in draws the child's model mesh additively at the
 * child's own node 0 (its slot +0x90). */
static int indicator_draw(void *ctx, uint32_t fn, void *obj)
{
    IndicatorCall *c = ctx;
    const EmActor *parent = c->node->parent;
    if (fn != EM_INDICATOR_CHILD_DRAW_001CACB0 || obj != c->actor || !parent)
        return -1;
    const float *c80 = c->actor->f80;
    float node[16];
    if (parent->callback != 0x00825940u && em_indicator_bind_live_node(c->actor, 0, node) < 0)
        return -1;
    switch (parent->callback) {
    case 0x00219550u:
        return em_pickup_light_submit(parent->source_id, c80, node);
    case 0x00159210u:
        return em_props_indicator_submit(0, c80, node);
    case 0x00827B10u:
        return em_props_indicator_submit(1, c80, node);
    case 0x00825940u: {
        /* The security gun's lamp (model 0x7A, bank D_0028A56C) has no
         * port mesh yet: its draw is the object-unit draw of
         * docs/OWNER_DRAW.md (P1). Its colour is (0, 0, 0, 0.25) in the
         * first level (the gun stays dormant: the lamp is dark), which
         * 001D8C30 mode 1 turns into 1 / 128 of the texel: the RNG draw
         * above is the part that shows. */
        static int reported;
        if (!reported++)
            fprintf(stderr, "em_area11: 001C5680 lamp 0x7A of the security gun 0x825940: 001CACB0 not drawn "
                            "(no model 0x7A mesh; OWNER_DRAW.md P1)\n");
        return 0;
    }
    default:
        return -1;
    }
}

static int indicator_color(void *ctx, float c80[4])
{
    IndicatorCall *c = ctx;
    const EmEffectKindsWorkers workers = {.ctx = ctx, .w_00122BB8 = indicator_rand,
                                          .w_indirect = indicator_draw};
    EmEffectKinds kinds = {.workers = &workers};
    return em_effect_kinds_001F54E0(&kinds, c->actor, c80, EM_INDICATOR_CHILD_DRAW_001CACB0, c80);
}

static int indicator_free(void *ctx)
{
    IndicatorCall *c = ctx;
    if (s_panel_child == c->actor)
        s_panel_child = NULL;
    return em_actor_pool_free_001AFC10(s_pool, s_scene, c->actor);
}

static int tick_indicator(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)world;
    IndicatorCall call = {actor, node};
    const EmIndicatorChildWorkers workers = {&call, indicator_init, indicator_place, indicator_color,
                                             indicator_free};
    EmIndicatorChildRecord record = {&actor->u04[0], actor->u0A[0], node->a0, actor->f80};
    if (em_indicator_child_step(actor->callback, &record, &workers) < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                                                 "indicator child: a worker failed (no draw target)");
    return 1;
}

/* 00159210 state 1 / sub 2: r = +0x20; only when r != 0 does it write
 * r[4] = 3 and clear the slot (the slot holds 0 when 001C5570's class-0xC
 * alloc was refused, or when the powered terminal spawned no child). */
int em_area11_bindings_panel_child_stop(void)
{
    if (!s_panel_child)
        return 0;
    s_panel_child->u04[0] = 3;
    s_panel_child = NULL;
    return 1;
}

static int tick_legacy_world(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)actor;
    (void)node;
    if (world->cutscene)
        em_game_legacy_pool_cutscene();
    else
        em_game_legacy_pool_gameplay();
    return 1;
}

/* ------------------------------------------------------------ the table */

#define LEGACY_WORLD_CALLBACK 0u

static const Binding k_bindings[] = {
    /* deferred g0.0-g0.6: item owners */
    {0x00219550u, "pickup: em_area11_interaction_host_pickup_tick", tick_pickup, NULL},
    {0x0015AFA0u, "pickup: em_area11_interaction_host_pickup_tick", tick_pickup, NULL},
    /* deferred g0.7/g0.8 (the security gun and its cable); crates area11[3..6], drums area11[14..15] */
    {0x00825940u, "security gun: em_gun_tick (em_security_gun)", tick_gun, NULL},
    {0x00827490u, "gun cable: em_gun_cable_tick (em_security_gun)", tick_gun_cable, NULL},
    {0x001551B0u, "crate: em_crate_original (em_area11_boxes)", tick_box, NULL},
    {0x00156620u, "drum: em_drum_original (em_area11_boxes)", tick_box, NULL},
    {0x001BC350u, "door: 001BC350 (em_area11_door)", tick_door, NULL},
    {0x00827630u, "fan: em_fan_original_tick (area11[1]/[2])", tick_fan, NULL},
    {0x008235F0u, "flame: em_area11_effect_runtime_tick, 001D04B0 on em_effects_live", tick_effect, NULL},
    {0x008237E0u, "Roger: em_roger_tick / em_roger_actor_original (em_area11_roger)",
     tick_roger, NULL},
    {0x001C5C90u, "equipment: em_roger_actor_001C5C90 (em_area11_roger)", tick_roger,
     NULL},
    {0x00823E80u, "opening controller: em_area11_opening over em_area11_script_host", tick_opening, NULL},
    {0x001BB0E0u, "opening actor: em_slg_001BB0E0 (em_area11_roger)", tick_opening_actor, NULL},
    {0x00823CE0u, "manager: dormant", NULL,
     "manager 00823CE0 (area11[11]): dormant (waits on D_00810788); no port code"},
    {0x008253F0u, "director: em_director_original over em_area11_script_host",
     tick_director, NULL},
    {0x008257A0u, "record 13: em_manager_008257A0", tick_manager_8257A0, NULL},
    {0x00823FF0u, "truck: em_truck_original (em_area11_boxes)", tick_truck, NULL},
    {0x008251E0u, "truck trigger: em_truck_trigger_tick, script 0x8292C0 (em_area11_script_host)", tick_truck, NULL},
    {0x00159210u, "panel: em_area11_interaction_host_panel_tick", tick_panel, NULL},
    {0x00827B10u, "terminal: em_area11_interaction_host_elevator_tick", tick_terminal,
     NULL},
    {0x001C4820u, "prop: em_sul_001C4820 (em_area11_boxes world owner)", tick_prop_001C4820,
     NULL},
    {0x001E55F0u, "weather: em_weather over the node's state (em_snow_runtime)",
     tick_weather, NULL},
    {0x001C5930u, "area title: lifecycle; the legacy em_hud card draws at the close-out", tick_area_title, NULL},
    {0x0018A6B0u, "player equipment: em_player_equipment (em_equipment_live)",
     tick_equipment, NULL},
    {0x001E2560u, "head-bone sprite: em_head_sprite_original (em_effects_live)",
     tick_effect_node, NULL},
    {0x001EA240u, "effect: em_effect_original 001EA240 (em_effects_live)",
     tick_effect_node, NULL},
    {0x001C5680u, "indicator child: 001C5680 (em_indicator_child)", tick_indicator, NULL},
    {0x001C5760u, "indicator child: 001C5760 (em_indicator_child)", tick_indicator, NULL},
    {LEGACY_WORLD_CALLBACK, "legacy_world: S10a legacy block", tick_legacy_world, NULL},
};
#define BINDING_COUNT (sizeof k_bindings / sizeof k_bindings[0])
_Static_assert(BINDING_COUNT <= 64, "s_reported has one bit per row");

static const Binding *binding_for(uint32_t callback)
{
    for (size_t i = 0; i < BINDING_COUNT; ++i)
        if (k_bindings[i].callback == callback)
            return &k_bindings[i];
    return NULL;
}

/* The one behaviour every bound node runs: first-call bookkeeping around
 * the row's adapter; a row without port code is reported once. */
static int node_behavior(EmActor *actor, void *world_arg)
{
    Node *node = node_of(actor);
    const EmArea11World *world = world_arg;
    if (!node || !node->binding || !world)
        return fault(actor ? actor->callback : 0, EM_SCENE_FAULT_NULL_WORKER, "unbound pool node");
    const Binding *b = node->binding;
    if (b->note) {
        uint64_t bit = 1ull << (unsigned)(b - k_bindings);
        if (!(s_reported & bit)) {
            s_reported |= bit;
            fprintf(stderr, "em_area11: node without port code: %s\n", b->note);
        }
    }
    int rc = b->tick ? b->tick(actor, node, world) : 1;
    node->ticked = 1;
    return rc;
}

static void node_release(EmActor *actor)
{
    Node *node = node_of(actor);
    if (!node)
        return;
    memset(node, 0, sizeof *node);
}

static int bind_node(EmActor *actor, const char *record)
{
    Node *node = node_of(actor);
    const Binding *b = binding_for(actor->callback);
    if (!node)
        return fault(actor->callback, EM_SCENE_FAULT_BAD_INDEX, "node outside the pool");
    if (!b)
        return fault(actor->callback, EM_SCENE_FAULT_NULL_WORKER, "callback with no AREA11 binding row");
    memset(node, 0, sizeof *node);
    node->binding = b;
    if (record)
        snprintf(node->record, sizeof node->record, "%s", record);
    snprintf(node->name, sizeof node->name, "%s", b->name);
    actor->behavior = node_behavior;
    actor->release = node_release;
    actor->owner = node;
    return 0;
}

/* ------------------------------------------------------------ public */

void em_area11_bindings_attach(EmActorPool *pool, EmSceneState *scene)
{
    s_pool = pool;
    s_scene = scene;
    /* 001AF800: the records that hold bone slots (+0x09): the boxes' and
     * (census L22) Roger's and the equipment node's, which the boxes' worker
     * hands to em_area11_roger. */
    if (pool) {
        pool->w_001AF800 = em_area11_boxes_001AF800;
        pool->worker_ctx = NULL;
    }
}

void em_area11_bindings_reset(void)
{
    memset(s_nodes, 0, sizeof s_nodes);
    s_panel_child = NULL;
    em_area11_boxes_reset(); /* 001AFCA0's 001AF710 and the boxes' state */
    em_area11_roger_reset();
    em_area11_door_reset(s_pool, s_scene);
    /* The overlay scripts are mutated in place: fresh images per visit. */
    em_area11_script_host_reset(s_pool, s_scene);
}

int em_area11_bind_roster(void *ctx, EmActor *actor, const EmActorRosterSpawned *spawned)
{
    (void)ctx;
    char record[24] = "";
    if (spawned->source == EM_ROSTER_SOURCE_DEFERRED)
        snprintf(record, sizeof record, "deferred[g%u.%u]", (unsigned)spawned->group,
                 (unsigned)spawned->index);
    else if (spawned->source == EM_ROSTER_SOURCE_PLACEMENT)
        snprintf(record, sizeof record, "area11[%u]", (unsigned)spawned->index);
    return bind_node(actor, record[0] ? record : NULL);
}

static int equipment_spawn(int32_t arg1);

/* A node the effect binder's 001EF9D0 allocated: its behaviour by +0x10. */
static int bind_spawned(EmActor *actor)
{
    return bind_node(actor, NULL);
}

int em_area11_bindings_effects_attach(void)
{
    if (em_effects_live_attach(s_pool, s_scene, bind_spawned) < 0 ||
        em_equipment_live_attach(s_pool, s_scene) < 0 || em_indicator_bind_live_attach(s_pool) < 0)
        return -1;
    em_equipment_live_set_spawn(equipment_spawn);
    em_area11_interaction_host_set_aura_draw(em_effects_live_aura_draw);
    static const EmArea11HostOwnerHooks k_owner = {owner_bind_001B0FD0, owner_bind_001B1020, owner_place,
                                                   owner_draw, terminal_copy_child};
    em_area11_interaction_host_set_owner_hooks(&k_owner);
    /* 001AFCA0's 001D0660: 001F0310, the effect pools' reset. */
    return em_effects_live_001F0310();
}

/* 001BAC00's 001AFA90: the record and em_sdf's view of its stores, which
 * em_area11_bindings_001BAC00 applies once the walk of the list returns. */
enum { SPAWN_LIST_MAX = 8 };
typedef struct {
    EmActor *actor[SPAWN_LIST_MAX];
    EmSdfSpawned view[SPAWN_LIST_MAX];
    unsigned count;
} SpawnList;

static int spawn_001AFA90(void *ctx, uint8_t type, uint32_t *node, EmSdfSpawned **view)
{
    SpawnList *l = ctx;
    *node = 0;
    *view = NULL;
    if (l->count >= SPAWN_LIST_MAX)
        return fault(0x001BAC00u, EM_SCENE_FAULT_BAD_INDEX, "001BAC00: more entries than the binder holds");
    EmActor *p = em_actor_pool_alloc_001AFA90(s_pool, s_scene, type);
    if (!p)
        return em_scene_faulted(s_scene) ? -1 : 0;   /* a refused alloc answers 0, as the original */
    memset(&l->view[l->count], 0, sizeof l->view[l->count]);
    l->actor[l->count] = p;
    *node = address_of(p);
    *view = &l->view[l->count++];
    return 0;
}

int em_area11_bindings_001BAC00(EmActor *owner, const uint8_t *record, const EmSdfImage *image, int32_t *result)
{
    if (!owner || !record || !image || !result || em_scene_faulted(s_scene))
        return -1;
    SpawnList list;
    memset(&list, 0, sizeof list);
    EmSdfWorkers w;
    memset(&w, 0, sizeof w);
    w.ctx = &list;
    w.w_001AFA90 = spawn_001AFA90;
    /* The 0x270E path (001C8140) is not in the opening's list: NULL. */
    EmSdfSpawnOwner view = {address_of(owner), (int16_t)owner->flags2};
    EmSdfFault f = {0, 0};
    int r = em_sdf_001BAC00(&view, record, image, &w, &f);
    if (r < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(f.address ? f.address : 0x001BAC00u, EM_SCENE_FAULT_WORKER_FAILED,
                                                 "001BAC00 faulted (em_sdf_001BAC00)");
    owner->flags2 = (uint16_t)view.s2E;   /* owner +0x2E = 0 */
    for (unsigned i = 0; i < list.count; ++i) {
        EmActor *p = list.actor[i];
        const EmSdfSpawned *v = &list.view[i];
        if (v->owner_24 != address_of(owner))
            return fault(0x001BAC00u, EM_SCENE_FAULT_BAD_RESULT, "001BAC00: a record's +0x24 is not its owner");
        p->model = v->b03;                          /* +0x03 */
        p->param = v->b0D;                          /* +0x0D */
        memcpy(p->pos, v->pos_B0, sizeof v->pos_B0); /* +0xB0..+0xB8 */
        memcpy(p->rot, v->rot_C0, sizeof v->rot_C0); /* +0xC0..+0xC8 */
        p->callback = v->handler_10;                /* +0x10 */
        p->flags2 = (uint16_t)v->s2E;               /* +0x2E */
        if (bind_node(p, NULL) < 0)
            return -1;
        Node *node = node_of(p);
        node->parent = owner;                       /* +0x24 */
        node->entry_20 = v->entry_20;               /* +0x20 */
    }
    *result = r;
    return 0;
}

int em_area11_bindings_spawn_001F0120(uint32_t owner_address, uint8_t key)
{
    if (em_effects_live_001F0120(owner_address, key) < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(em_effects_live_fault() ? em_effects_live_fault() : 0x001F0120u,
                                                 EM_SCENE_FAULT_WORKER_FAILED, "001F0120 faulted (em_effects_live)");
    return 0;
}

int em_area11_spawn_player_equipment_0015C310(int32_t arg1)
{
    if (em_scene_faulted(s_scene))
        return -1;
    const uint8_t *ca = em_scene_progress_at(s_scene, 0x00810CA4u, 4);
    if (!ca)
        return fault(0x0015C310u, EM_SCENE_FAULT_BAD_INDEX, "D_00810CA4..CA7 not canonical");
    uint8_t ca4 = ca[0], ca5 = ca[1], ca6 = ca[2], ca7 = ca[3];
    /* 0015C310 (byte-matched): with a1 == 0 the (0,0) (player +0x20),
     * (1,0) and (1,0x10) nodes first. */
    if (arg1 == 0 && (spawn_0018A880(0, 0) < 0 || spawn_0018A880(1, 0) < 0 || spawn_0018A880(1, 0x10) < 0))
        return -1;
    int rc;
    if (ca4 == 2)
        rc = spawn_0018A880(2, 0x0C);
    else if (ca4 == 1)
        rc = spawn_0018A880(2, 0x0B) < 0 ? -1 : spawn_0018A880(2, ca6);
    else if (ca4 == 0)
        rc = spawn_0018A880(2, 0x0A) < 0 ? -1 : spawn_0018A880(2, ca6);
    else
        rc = (spawn_0018A880(2, ca5) < 0 || spawn_0018A880(2, ca6) < 0) ? -1 : spawn_0018A880(2, ca7);
    if (rc < 0)
        return -1;
    if (ca6 == 4 && spawn_0018A880(1, 0x15) < 0)
        return -1;
    return 0;
}

static int equipment_spawn(int32_t arg1)
{
    return em_area11_spawn_player_equipment_0015C310(arg1);
}

int em_area11_spawn_player_children_0015C420(void)
{
    if (em_scene_faulted(s_scene))
        return -1;
    /* 0015C420: 0018A880(4, 0) -> player+0x18 (the player is not a pool
     * record in the port; the handle is not kept). */
    if (spawn_0018A880(4, 0) < 0)
        return -1;
    /* 0015C310(player, 0). */
    if (em_area11_spawn_player_equipment_0015C310(0) < 0)
        return -1;
    /* 0015C420: 001F0120(player, 0x3B); +0x24 = player +0x14 = 0x008102B0. */
    return em_area11_bindings_spawn_001F0120(EM_SCENE_D_008102B0, 0x3B);
}

/* 001C1EA0 (byte-matched): v0 = 001B0070() = D_008106C8, the canonical
 * request word C8 that 001B07C0 -> 001B0250 wrote earlier in the same
 * state-0 tick (S12a; before S12a the captured 0x20081910 was used). The
 * first of & 0x02000010, & 0x04000020, & 0x08000040 that is set spawns one
 * 001EFD20(0x80000017, &D_00250F00 / &D_00250F10 / &D_00250F20) and
 * returns; none set spawns nothing. The three vectors are zero in the ELF
 * data, so the node's +0xB0 is (0, 0, 0) with +0xBC = 1.0 and +0xC0..+0xCC
 * = 0 (001EFD20). Called only for the AREA11 roster pool. */
int em_area11_spawn_weather_001C1EA0(void)
{
    if (em_scene_faulted(s_scene))
        return -1;
    uint32_t flags = em_scene_req_u32(s_scene, EM_SCENE_REQ_C8);
    if (!(flags & 0x02000010u) && !(flags & 0x04000020u) && !(flags & 0x08000040u))
        return 0;
    /* D_00250F00 / 10 / 20 (zero in the ELF data): em_effect_original's
     * 001EFD20 over the effect table (entity 0x17: class 0x0C, +3 0x63,
     * 001E55F0); the binder binds the node by its +0x10. */
    static const float k_zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (em_effects_live_001EFD20(0x80000017u, k_zero) < 0)
        return em_scene_faulted(s_scene) ? -1
                                         : fault(em_effects_live_fault() ? em_effects_live_fault() : 0x001EFD20u,
                                                 EM_SCENE_FAULT_WORKER_FAILED, "001C1EA0's 001EFD20 faulted");
    return 0;
}

int em_area11_spawn_legacy_world(void)
{
    if (em_scene_faulted(s_scene))
        return -1;
    EmActor *p = em_actor_pool_alloc_001AFA90(s_pool, s_scene, 0);
    if (!p)
        return fault(0x001B6990u, EM_SCENE_FAULT_BAD_RESULT, "legacy_world node: pool full");
    p->callback = LEGACY_WORLD_CALLBACK;
    return bind_node(p, NULL);
}

void em_area11_walk_begin(int mode)
{
    memcpy(s_walk_eye, g.cam.eye, sizeof s_walk_eye);
    if (mode != 2)
        em_game_legacy_collision_clears();
}

void em_area11_walk_end(int mode)
{
    if (mode == 2)
        return;
    /* The port-native draw-list collector (no original counterpart; the
     * original owners submit their draws from inside their behaviours). It
     * runs after every owner so this frame's owner state decides what is
     * drawn; the legacy cutscene block also ran it last. */
    render_chain_build();
    if (mode == 0)
        em_game_legacy_player_residue();
}

const char *em_area11_node_record(const EmActor *actor)
{
    const Node *node = node_of(actor);
    return node && node->record[0] ? node->record : NULL;
}

const char *em_area11_node_binding(const EmActor *actor)
{
    const Node *node = node_of(actor);
    return node && node->binding ? node->name : NULL;
}
