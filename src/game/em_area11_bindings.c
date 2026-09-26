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
 *   measured, on the first world frame, 0x825940 (deferred g0.7) spawning a
 *   001C5680 child (+0xD 0x7A), 0x827B10 (area11[19]) spawning a 001C5760
 *   child at 0x827C20 (+0xD 0x10, +0xA 0). These two are INTERIM spawns.
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
#include "game/em_area11_effect_runtime.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_area11_roger.h"
#include "game/em_area11_script_host.h"
#include "game/em_collision_world.h"
#include "game/em_director_original.h"
#include "game/em_sdk_math_original.h"
#include "game/em_effect_kinds.h"
#include "game/em_effects_live.h"
#include "game/em_equipment_live.h"
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
#include "game/em_props.h"
#include "game/em_scene_bindings.h"
#include "game/em_scene_workers.h" /* EM_SCENE_D_008102B0 */
#include "game/em_snow_runtime.h"

static EmActorPool *s_pool;
static EmSceneState *s_scene;

/* ------------------------------------------------------------ node state */

enum { GROUP_NONE, GROUP_ENEMIES, GROUP_COUNT };

typedef struct Node Node;
typedef int (*NodeTick)(EmActor *actor, Node *node, const EmArea11World *world);

typedef struct {
    uint32_t callback;
    const char *name;   /* binding name (the group head's, for a group) */
    const char *member; /* binding name of a non-head group member */
    uint8_t group;
    NodeTick tick;      /* NULL: no port code; `note` is reported once */
    const char *note;
} Binding;

struct Node {
    const Binding *binding;
    char record[24];  /* original tracer tag, "" for runtime nodes */
    char name[112];   /* binding name recorded in the trace */
    uint8_t ticked;   /* first behaviour call done */
    uint8_t head;     /* runs its group's port code */
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
};

static Node s_nodes[EM_ACTOR_POOL_CAPACITY];
static EmActor *s_heads[GROUP_COUNT];
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

/* Enemy group: em_enemy is the port's aggregate of the husk pair (0x825940,
 * 0x827490). The legacy cutscene block never ran it, so it stays
 * gameplay-only. (The crates and drums left the group in census L25: they
 * run their original owners, tick_box.) */
static int tick_enemies(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)actor;
    if (node->head && !world->cutscene)
        em_game_legacy_enemy_tick();
    return 1;
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

/* 0x825940 (deferred g0.7): its state 0 allocates its 001C5680 child inline
 * (overlay 0x825A74..0x825AE0: +0xD 0x7A, +0xA0 = (0, 0, 0, 0.25), stored at
 * its +0x220). Its later states rewrite that colour (0x825D18.., 0x825D6C..,
 * 0x825DE4.., 0x826334.., 0x826388.., 0x826420..); the husk owner itself is
 * still the legacy em_enemy aggregate (census L24), so on the route the
 * child keeps the spawn colour, as every captured beat shows. */
static int tick_enemy_00825940(EmActor *actor, Node *node, const EmArea11World *world)
{
    static const float k_husk_child[4] = {0.0f, 0.0f, 0.0f, 0.25f};
    if (!node->ticked &&
        spawn_child_record(actor, k_husk_child, 0x7A, 0x001C5680u, 0, &node->child) < 0)
        return -1;
    return tick_enemies(actor, node, world);
}

/* 001BC350, the room-move door. AREA11's only door is a room-move door
 * (assets/scene_snow/scene.txt), so a goto switch here would be a scene
 * change the original routes through B5..B8 and 001AD010 (S12a/S12b):
 * fail-stop. The legacy cutscene block never ran the door. */
static int tick_door(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)node;
    if (world->cutscene)
        return 1;
    if (em_game_legacy_door_tick())
        return fault(actor->callback, EM_SCENE_FAULT_BAD_RESULT,
                     "legacy goto scene switch from the AREA11 roster pool (S12b routes it through B8)");
    return 1;
}

static int tick_effect(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)actor;
    (void)node;
    (void)world;
    em_area11_effect_runtime_tick();
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

static int tick_opening(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)actor;
    (void)node;
    (void)world;
    em_opening_runtime_tick();
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

/* 00159210 panel (area11[18]). State 0 (the first call): the child (see the
 * file comment); the powered bit is D_00810841[0x0B] bit (+0x2E), the
 * canonical D_0081084C bit 7 (em_game_terminal_powered), so any other bit
 * faults. Later calls are state 1, the original owner in the AREA11
 * interaction host (WP-4): em_area11_interaction_host_panel_tick runs
 * 00159210/00157860 and its 001B17A0 publication tail (census L07: the
 * record bound at state 0 goes onto the collision world's lists, its cell
 * uid 18), in both variants. grate_update keeps the port's static panel pose
 * and binds cell 18 into the port's own collision world (em_props.c) for
 * the port's own queries (player movement, follow camera) that still use it. */
static int tick_panel(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)world;
    grate_update();
    if (!node->ticked) {
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
        em_area11_interaction_host_set_panel_address(address_of(actor));
        /* Census L07: the record its 001B17A0 publishes (cell uid 18). */
        if (em_area11_interaction_host_bind_actor(actor->source_id, actor) < 0)
            return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                         "00159210 state 0: the interaction host failed");
        return 1;
    }
    if (em_area11_interaction_host_panel_tick() < 0)
        return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED, "00159210: the interaction host failed");
    return 1;
}

/* 0x827B10 terminal and elevator (area11[19]). State 0 (the first call):
 * the floor placement (D_0081083A -> +0xB4 190/230, 001C6380; the host's
 * em_area11_interaction_host_elevator_state0), then its 001C5760 child,
 * allocated inline (0x827BD8..0x827C4C: +0xD 0x10, +0xA0 = (1.0, 0, 0,
 * 0.25), stored at its +0x2E4); the owner reads D_00810841[0x0B] bit
 * (+0x2E), which only bit 7 of the canonical D_0081084C stores. Later
 * calls are state 1, the original owner in the AREA11 interaction host
 * (WP-4): refusal 0x82A990 or powered 0x82A750 with the carry 00828050, and
 * its 001B17A0 publication and the +0x28 level step (em_elevator_tick), in
 * both variants; then the tail 0x827EAC writes the child's colour
 * (em_indicator_00827B10_colour).
 * The legacy em_examine terminal and the legacy ride it ran were retired in
 * WP-4. */
static int tick_terminal(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)world;
    if (!node->ticked) {
        if (s_scene->d810700 != 0x0B || actor->flags2 != 7)
            return fault(actor->callback, EM_SCENE_FAULT_BAD_INDEX,
                         "00827B10 reads a D_00810841 bit the port does not store");
        /* 0x827B54..0x827BF0: +0xB4 from D_0081083A, then 001C6380 and
         * 001A2370 (0x827C04, over the record bound here: census L07),
         * before the child spawn at 0x827C18. */
        if (em_area11_interaction_host_bind_actor(actor->source_id, actor) < 0 ||
            em_area11_interaction_host_elevator_state0() < 0)
            return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                         "00827B10 state 0: the interaction host failed");
        static const float k_unpowered[4] = {1.0f, 0.0f, 0.0f, 0.25f};
        if (spawn_child_record(actor, k_unpowered, 0x10, 0x001C5760u, 0, &node->child) < 0)
            return -1;
        return 1;
    }
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

/* 001E55F0 weather node: em_weather's translation over the node's own state,
 * drawing through the snow runtime. The cutscene block passed the camera eye
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
        return fault(0x0021B9A0u, EM_SCENE_FAULT_WORKER_FAILED, "001E67C0's fog programmer faulted");
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

/* 001C2360 / 001C22A0: the model and bone-slot bind. The port keeps no bone
 * slots for these children (their draw takes the owner's palette, below),
 * so the bind always succeeds; +0x4C becomes 001CACB0 (001CA5F0 mode 2). */
static int indicator_init(void *ctx, uint32_t fn, int32_t *result)
{
    (void)ctx;
    (void)fn;
    *result = 0;
    return 0;
}

/* 001C6380: the child's matrix from its +0xB0 / +0xC0, which the spawn
 * copied from the owner. The port's +0x4C draw places the child with the
 * owner's current palette (em_pickup / em_props), which is that matrix for
 * the standing owners; the moving terminal copies its node matrix to the
 * child in the original too. */
static int indicator_place(void *ctx)
{
    (void)ctx;
    return 0;
}

static int indicator_rand(void *ctx, int32_t *v0)
{
    (void)ctx;
    *v0 = (int32_t)em_random_next();
    return 0;
}

/* The +0x4C method 001CACB0, called by 001F54E0 with the child's new +0x80. */
static int indicator_draw(void *ctx, uint32_t fn, void *obj)
{
    IndicatorCall *c = ctx;
    const EmActor *parent = c->node->parent;
    if (fn != EM_INDICATOR_CHILD_DRAW_001CACB0 || obj != c->actor || !parent)
        return -1;
    const float *c80 = c->actor->f80;
    switch (parent->callback) {
    case 0x00219550u:
        return em_pickup_light_submit(parent->source_id, c80);
    case 0x00159210u:
        return em_props_indicator_submit(0, c80);
    case 0x00827B10u:
        return em_props_indicator_submit(1, c80);
    case 0x00825940u: {
        /* Model 0x7A (bank D_0028A56C) has no port mesh yet: its draw is the
         * object-unit draw of docs/OWNER_DRAW.md (P1). Its colour is
         * (0, 0, 0, 0.25) on the route, which 001D8C30 mode 1 turns into
         * 1 / 128 of the texel: the RNG draw above is the part that shows. */
        static int reported;
        if (!reported++)
            fprintf(stderr, "em_area11: 001C5680 child 0x7A of 0x825940: 001CACB0 not drawn (no model 0x7A "
                            "mesh; OWNER_DRAW.md P1)\n");
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
    {0x00219550u, "pickup: em_area11_interaction_host_pickup_tick", NULL, GROUP_NONE, tick_pickup, NULL},
    {0x0015AFA0u, "pickup: em_area11_interaction_host_pickup_tick", NULL, GROUP_NONE, tick_pickup, NULL},
    /* deferred g0.7/g0.8 (the husk pair); crates area11[3..6], drums area11[14..15] */
    {0x00825940u, "enemies: legacy em_enemy_update (group head)", "group: enemies", GROUP_ENEMIES,
     tick_enemy_00825940, NULL},
    {0x00827490u, "enemies: legacy em_enemy_update (group head)", "group: enemies", GROUP_ENEMIES,
     tick_enemies, NULL},
    {0x001551B0u, "crate: em_crate_original (em_area11_boxes)", NULL, GROUP_NONE, tick_box, NULL},
    {0x00156620u, "drum: em_drum_original (em_area11_boxes)", NULL, GROUP_NONE, tick_box, NULL},
    {0x001BC350u, "door: legacy em_door_update", NULL, GROUP_NONE, tick_door, NULL},
    {0x00827630u, "fan: static", NULL, GROUP_NONE, NULL,
     "fan (area11[1]/[2]): no port behaviour; em_pickup draws it static (WP-11)"},
    {0x008235F0u, "flame: em_area11_effect_runtime_tick", NULL, GROUP_NONE, tick_effect, NULL},
    {0x008237E0u, "Roger: em_roger_tick / em_roger_actor_original (em_area11_roger)", NULL, GROUP_NONE,
     tick_roger, NULL},
    {0x001C5C90u, "equipment: em_roger_actor_001C5C90 (em_area11_roger)", NULL, GROUP_NONE, tick_roger,
     NULL},
    {0x00823E80u, "opening controller: em_opening_runtime_tick", NULL, GROUP_NONE, tick_opening, NULL},
    {0x00823CE0u, "manager: dormant", NULL, GROUP_NONE, NULL,
     "manager 00823CE0 (area11[11]): dormant (waits on D_00810788); no port code"},
    {0x008253F0u, "director: em_director_original over em_area11_script_host", NULL, GROUP_NONE,
     tick_director, NULL},
    {0x008257A0u, "record 13: em_manager_008257A0", NULL, GROUP_NONE, tick_manager_8257A0, NULL},
    {0x00823FF0u, "truck: em_truck_original (em_area11_boxes)", NULL, GROUP_NONE, tick_truck, NULL},
    {0x008251E0u, "truck trigger: em_truck_trigger_tick, script 0x8292C0 (em_area11_script_host)", NULL,
     GROUP_NONE, tick_truck, NULL},
    {0x00159210u, "panel: em_area11_interaction_host_panel_tick", NULL, GROUP_NONE, tick_panel, NULL},
    {0x00827B10u, "terminal: em_area11_interaction_host_elevator_tick", NULL, GROUP_NONE, tick_terminal,
     NULL},
    {0x001C4820u, "prop: render-only", NULL, GROUP_NONE, NULL,
     "001C4820 (area11[20]): render-only; the port draws it from the scene props"},
    {0x001E55F0u, "weather: em_weather over the node's state (em_snow_runtime)", NULL, GROUP_NONE,
     tick_weather, NULL},
    {0x001C5930u, "area title: lifecycle; the legacy em_hud card draws at the close-out", NULL,
     GROUP_NONE, tick_area_title, NULL},
    {0x0018A6B0u, "player equipment: em_player_equipment (em_equipment_live)", NULL, GROUP_NONE,
     tick_equipment, NULL},
    {0x001E2560u, "head-bone sprite: em_head_sprite_original (em_effects_live)", NULL, GROUP_NONE,
     tick_effect_node, NULL},
    {0x001EA240u, "effect: em_effect_original 001EA240 (em_effects_live)", NULL, GROUP_NONE,
     tick_effect_node, NULL},
    {0x001C5680u, "indicator child: 001C5680 (em_indicator_child)", NULL, GROUP_NONE, tick_indicator, NULL},
    {0x001C5760u, "indicator child: 001C5760 (em_indicator_child)", NULL, GROUP_NONE, tick_indicator, NULL},
    {LEGACY_WORLD_CALLBACK, "legacy_world: S10a legacy block", NULL, GROUP_NONE, tick_legacy_world, NULL},
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
    if (node->head && node->binding && s_heads[node->binding->group] == actor)
        s_heads[node->binding->group] = NULL;
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
    if (b->group != GROUP_NONE && !s_heads[b->group]) {
        s_heads[b->group] = actor;
        node->head = 1;
    }
    snprintf(node->name, sizeof node->name, "%s",
             (b->group != GROUP_NONE && !node->head) ? b->member : b->name);
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
    memset(s_heads, 0, sizeof s_heads);
    s_panel_child = NULL;
    em_area11_boxes_reset(); /* 001AFCA0's 001AF710 and the boxes' state */
    em_area11_roger_reset();
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
        em_equipment_live_attach(s_pool, s_scene) < 0)
        return -1;
    em_equipment_live_set_spawn(equipment_spawn);
    em_area11_interaction_host_set_aura_draw(em_effects_live_aura_draw);
    /* 001AFCA0's 001D0660: 001F0310, the effect pools' reset. */
    return em_effects_live_001F0310();
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
