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
 *             0, 1.0); +0xD = b; +0x24 = owner +0x14.
 *   001EF9D0  NEARMISS (logic checked): the effect entity at
 *             *(D_00259C70) + (type & 0x7FFFFFFF) * 0x30; alloc(ent[0]);
 *             +3 = ent[4]; +0xD = ent[8]; +0x10 = ent[0xC]; +0x38 = 1. The
 *             entity bytes used here were read from the captured table
 *             (build/startup-reference/playable_ee.bin, table base
 *             0x257C90): type 0x10 = class 0x0C, +3 4, +0xD 0, 001E2560;
 *             type 0x17 = class 0x0C, +3 0x63, +0xD 0, 001E55F0.
 *   001EFD20  byte-matched: 001EF9D0(type), +0xB0 = the argument vector,
 *             +0xC0..+0xCC = 0, +0xBC = 1.0.
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
 *   child at 0x827C20 (+0xD 0x10, +0xA 0), and Roger 0x8237E0 calling
 *   001BA8E0 -> 001F0120(Roger, 0x47). These three are INTERIM spawns.
 */
#include "game/em_area11_bindings.h"

#include <stdio.h>
#include <string.h>

#include "game/em_area11_effect_runtime.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_director.h"
#include "game/em_frame.h"
#include "game/em_hud.h"
#include "game/em_manager_008257A0.h"
#include "game/em_game_internal.h"
#include "game/em_opening_runtime.h"
#include "game/em_props.h"
#include "game/em_scene_bindings.h"
#include "game/em_scene_workers.h" /* EM_SCENE_D_008102B0 */
#include "game/em_snow_runtime.h"
#include "game/em_truck.h"

static EmActorPool *s_pool;
static EmSceneState *s_scene;

/* ------------------------------------------------------------ node state */

enum { GROUP_NONE, GROUP_PICKUPS, GROUP_ENEMIES, GROUP_TRUCK, GROUP_INDICATORS, GROUP_COUNT };

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
    uint32_t link;    /* original +0x24 (the owner of an effect child) */
    /* 001E55F0 nodes: the actor's own weather state (its +4 byte and +0x1F0
     * block, em_weather.h); zeroed by bind_node, so a new actor seeds. */
    EmWeather weather;
};

static Node s_nodes[EM_ACTOR_POOL_CAPACITY];
static EmActor *s_heads[GROUP_COUNT];
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

/* Design 4.3 "interim_spawn": named in the trace's binding field. */
static void mark_interim(Node *node)
{
    size_t len = strlen(node->name);
    snprintf(node->name + len, sizeof node->name - len, " (interim spawn)");
}

/* 001C5570(owner, vector, a2, a3). Returns 0 (also when the class-0xC reserve
 * refuses the alloc: the original stores the 0 it returns) or -1. */
static int spawn_001C5570(EmActor *owner, uint8_t a2, int a3, int interim)
{
    EmActor *p = em_actor_pool_alloc_001AFA90(s_pool, s_scene, 0x0C);
    if (!p)
        return 0;
    p->table_index = 0;
    p->model = 0;
    p->flags2 = 0;
    p->param = a2;
    p->uid = 0xFFFF;
    p->link = 0;
    p->kind = 0;
    /* +0xA0 = the caller's vector: EmActor has no field for it (not stored). */
    memcpy(p->pos, owner->pos, sizeof p->pos);
    memcpy(p->rot, owner->rot, sizeof p->rot);
    p->u0A[0] = 0; /* +0x0A */
    switch (a3) {
    case 0:
        p->callback = 0x001C5760u;
        break;
    case 1:
        p->callback = 0x001C5680u;
        break;
    case 2:
        p->u0A[0] = 1;
        p->callback = 0x001C5760u;
        break;
    default:
        return fault(0x001C5570u, EM_SCENE_FAULT_BAD_INDEX, "001C5570 a3 outside 0..2");
    }
    if (bind_node(p, NULL) < 0)
        return -1;
    if (interim)
        mark_interim(node_of(p));
    return 0;
}

/* The 001EF9D0 entities the port spawns (bytes from the captured table,
 * see the file comment). */
typedef struct {
    uint32_t type;
    uint8_t cls, model, param;
    uint32_t callback;
} EffectEntity;
static const EffectEntity k_effect_0x10 = {0x80000010u, 0x0C, 0x04, 0x00, 0x001E2560u};
static const EffectEntity k_effect_0x17 = {0x80000017u, 0x0C, 0x63, 0x00, 0x001E55F0u};

/* 001EF9D0(type, 0, 1.0): none of the handled types is one of the
 * 0x80000026/2C/67 random-state ids, and a1 == 0 returns before the kind
 * dispatch. NULL when the alloc is refused (the original returns 0). */
static EmActor *spawn_001EF9D0(const EffectEntity *entity)
{
    EmActor *p = em_actor_pool_alloc_001AFA90(s_pool, s_scene, entity->cls);
    if (!p)
        return NULL;
    p->model = entity->model;
    p->param = entity->param;
    p->callback = entity->callback;
    /* +0x38 = 1: EmActor has no field for it (not stored). */
    return p;
}

/* 001F0120(owner, b) for b in {0x3B, 0x47} (001E2290 returns 1 for both). */
static int spawn_001F0120(uint32_t owner_address, uint8_t b, int interim)
{
    EmActor *e = spawn_001EF9D0(&k_effect_0x10);
    if (!e)
        return 0;
    e->param = b;
    if (bind_node(e, NULL) < 0)
        return -1;
    Node *n = node_of(e);
    n->link = owner_address; /* +0x24 = owner +0x14 (the owner's own address) */
    if (interim)
        mark_interim(n);
    return 0;
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

/* Pickup group: em_pickup is the port's aggregate of every item owner (its
 * light children tick at the indicator node, tick_indicators). */
static int tick_pickups(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)actor;
    if (!node->head)
        return 1;
    if (world->cutscene) {
        em_game_legacy_pickup_update(0);
    } else {
        em_game_legacy_pickup_update(1);
        em_game_legacy_pickup_collect();
    }
    return 1;
}

/* 00219550: state 0 spawns the indicator child (both state-0 arms reach the
 * 001C5570 call when the bone slots are available). */
static int tick_pickup_00219550(EmActor *actor, Node *node, const EmArea11World *world)
{
    if (!node->ticked && spawn_001C5570(actor, 0x73, 1, 0) < 0)
        return -1;
    return tick_pickups(actor, node, world);
}

/* Enemy group: em_enemy is the port's aggregate of the husk, crates and
 * drums. The legacy cutscene block never ran it, so it stays gameplay-only. */
static int tick_enemies(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)actor;
    if (node->head && !world->cutscene)
        em_game_legacy_enemy_tick();
    return 1;
}

/* 0x825940 (deferred g0.7): its first tick spawns the 001C5680 child that
 * the capture shows at its position (+0xD 0x7A). INTERIM. */
static int tick_enemy_00825940(EmActor *actor, Node *node, const EmArea11World *world)
{
    if (!node->ticked && spawn_001C5570(actor, 0x7A, 1, 1) < 0)
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

/* Roger: behaviour UNBOUND (WP-9). Its first tick's 001BA8E0 ->
 * 001F0120(Roger, 0x47) child is spawned (INTERIM). */
static int tick_roger(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)world;
    if (!node->ticked && spawn_001F0120(address_of(actor), 0x47, 1) < 0)
        return -1;
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

/* The legacy cutscene block never ran the director. */
static int tick_director(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)actor;
    (void)node;
    if (!world->cutscene)
        director_tick();
    return 1;
}

/* Truck (#24) with its trigger (#25). The legacy cutscene block never ran it. */
static int tick_truck(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)actor;
    if (node->head && !world->cutscene)
        em_truck_update(g.pos);
    return 1;
}

/* 00159210 panel (area11[18]). State 0 (the first call): the child (see the
 * file comment); the powered bit is D_00810841[0x0B] bit (+0x2E), the
 * canonical D_0081084C bit 7 (em_game_terminal_powered), so any other bit
 * faults. Later calls are state 1, the original owner in the AREA11
 * interaction host (WP-4): em_area11_interaction_host_panel_tick runs
 * 00159210/00157860 and its 001B17A0 publication tail, in both variants.
 * grate_update keeps the port's static panel pose and binds the original
 * cell-18 collision (em_props.c) on every call, as it did before. */
static int tick_panel(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)world;
    grate_update();
    if (!node->ticked) {
        if (actor->model == 0x2C) {
            if (spawn_001C5570(actor, 0x74, 1, 0) < 0)
                return -1;
        } else {
            if (s_scene->d810700 != 0x0B || actor->flags2 != 7)
                return fault(actor->callback, EM_SCENE_FAULT_BAD_INDEX,
                             "00159210 state 0 reads a D_00810841 bit the port does not store");
            if (!em_game_terminal_powered() && spawn_001C5570(actor, 0x75, 1, 0) < 0)
                return -1;
        }
        em_area11_interaction_host_set_panel_address(address_of(actor));
        return 1;
    }
    if (em_area11_interaction_host_panel_tick() < 0)
        return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED, "00159210: the interaction host failed");
    return 1;
}

/* 0x827B10 terminal and elevator (area11[19]). State 0 (the first call):
 * the floor placement (D_0081083A -> +0xB4 190/230, 001C6380; the host's
 * em_area11_interaction_host_elevator_state0), then the 001C5760 child (+0xD 0x10, +0xA 0) measured at 0x827C20 (INTERIM
 * spawn); the owner reads D_00810841[0x0B] bit (+0x2E), which only bit 7
 * of the canonical D_0081084C stores. Later calls are state 1, the
 * original owner in the AREA11 interaction host (WP-4): refusal 0x82A990
 * or powered 0x82A750 with the carry 00828050, and its 001B17A0
 * publication, in both variants. The legacy em_examine terminal and the
 * legacy ride it ran were retired in WP-4. */
static int tick_terminal(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)world;
    if (!node->ticked) {
        if (s_scene->d810700 != 0x0B || actor->flags2 != 7)
            return fault(actor->callback, EM_SCENE_FAULT_BAD_INDEX,
                         "00827B10 reads a D_00810841 bit the port does not store");
        /* 0x827B54..0x827BF0: +0xB4 from D_0081083A, then 001C6380,
         * before the child spawn at 0x827C18. */
        if (em_area11_interaction_host_elevator_state0() < 0)
            return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED,
                         "00827B10 state 0: the interaction host failed");
        if (spawn_001C5570(actor, 0x10, 0, 1) < 0)
            return -1;
        return 1;
    }
    if (em_area11_interaction_host_elevator_tick() < 0)
        return fault(actor->callback, EM_SCENE_FAULT_WORKER_FAILED, "00827B10: the interaction host failed");
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

/* Indicator children (001C5680 x7, 001C5760; ORIGINAL_FRAME_ORDER #39-#48).
 * The port ticks them as two aggregates at the first child's node: the
 * pickup light children (em_pickup's 001C5680 loop, #39-#44) and then the
 * em_props indicators (the panel/terminal children). Both draw a per-frame
 * LCG colour, so their place after the weather node keeps the original
 * order of those draws. */
static int tick_indicators(EmActor *actor, Node *node, const EmArea11World *world)
{
    (void)actor;
    (void)world;
    if (node->head) {
        em_pickup_lights_tick();
        em_props_indicators_tick();
    }
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
    {0x00219550u, "pickups: legacy em_pickup_update + collection (group head)", "group: pickups",
     GROUP_PICKUPS, tick_pickup_00219550, NULL},
    {0x0015AFA0u, "pickups: legacy em_pickup_update + collection (group head)", "group: pickups",
     GROUP_PICKUPS, tick_pickups, NULL},
    /* deferred g0.7/g0.8, crates area11[3..6], drums area11[14..15] */
    {0x00825940u, "enemies: legacy em_enemy_update (group head)", "group: enemies", GROUP_ENEMIES,
     tick_enemy_00825940, NULL},
    {0x00827490u, "enemies: legacy em_enemy_update (group head)", "group: enemies", GROUP_ENEMIES,
     tick_enemies, NULL},
    {0x001551B0u, "enemies: legacy em_enemy_update (group head)", "group: enemies", GROUP_ENEMIES,
     tick_enemies, NULL},
    {0x00156620u, "enemies: legacy em_enemy_update (group head)", "group: enemies", GROUP_ENEMIES,
     tick_enemies, NULL},
    {0x001BC350u, "door: legacy em_door_update", NULL, GROUP_NONE, tick_door, NULL},
    {0x00827630u, "fan: static", NULL, GROUP_NONE, NULL,
     "fan (area11[1]/[2]): no port behaviour; em_pickup draws it static (WP-11)"},
    {0x008235F0u, "flame: em_area11_effect_runtime_tick", NULL, GROUP_NONE, tick_effect, NULL},
    {0x008237E0u, "Roger: UNBOUND", NULL, GROUP_NONE, tick_roger,
     "Roger (area11[8]): behaviour UNBOUND, drawn statically (WP-9); only its 001F0120(0x47) "
     "child is spawned"},
    {0x001C5C90u, "equipment: UNBOUND", NULL, GROUP_NONE, NULL,
     "001C5C90 (area11[9]): UNBOUND (WP-9)"},
    {0x00823E80u, "opening controller: em_opening_runtime_tick", NULL, GROUP_NONE, tick_opening, NULL},
    {0x00823CE0u, "manager: dormant", NULL, GROUP_NONE, NULL,
     "manager 00823CE0 (area11[11]): dormant (waits on D_00810788); no port code"},
    {0x008253F0u, "manager: legacy director_tick", NULL, GROUP_NONE, tick_director, NULL},
    {0x008257A0u, "record 13: em_manager_008257A0", NULL, GROUP_NONE, tick_manager_8257A0, NULL},
    {0x00823FF0u, "truck: legacy em_truck_update (group head)", "group: truck", GROUP_TRUCK, tick_truck,
     NULL},
    {0x008251E0u, "truck: legacy em_truck_update (group head)", "group: truck", GROUP_TRUCK, tick_truck,
     NULL},
    {0x00159210u, "panel: em_area11_interaction_host_panel_tick", NULL, GROUP_NONE, tick_panel, NULL},
    {0x00827B10u, "terminal: em_area11_interaction_host_elevator_tick", NULL, GROUP_NONE, tick_terminal,
     NULL},
    {0x001C4820u, "prop: render-only", NULL, GROUP_NONE, NULL,
     "001C4820 (area11[20]): render-only; the port draws it from the scene props"},
    {0x001E55F0u, "weather: em_weather over the node's state (em_snow_runtime)", NULL, GROUP_NONE,
     tick_weather, NULL},
    {0x001C5930u, "area title: lifecycle; the legacy em_hud card draws at the close-out", NULL,
     GROUP_NONE, tick_area_title, NULL},
    {0x0018A6B0u, "player equipment: no port draw", NULL, GROUP_NONE, NULL,
     "0018A6B0 x7 player attached equipment (D3, Q5): the port has no equipment-model draw"},
    {0x001E2560u, "head-bone sprite effect: UNBOUND", NULL, GROUP_NONE, NULL,
     "001E2560 head-bone sprite effect (Q5): UNBOUND"},
    {0x001C5680u, "indicators: pickup lights + em_props indicators (group head)", "group: indicators",
     GROUP_INDICATORS, tick_indicators, NULL},
    {0x001C5760u, "indicators: pickup lights + em_props indicators (group head)", "group: indicators",
     GROUP_INDICATORS, tick_indicators, NULL},
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
    if (node)
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
}

void em_area11_bindings_reset(void)
{
    memset(s_nodes, 0, sizeof s_nodes);
    memset(s_heads, 0, sizeof s_heads);
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

int em_area11_spawn_player_children_0015C420(void)
{
    if (em_scene_faulted(s_scene))
        return -1;
    /* 0015C420: 0018A880(4, 0) -> player+0x18 (the player is not a pool
     * record in the port; the handle is not kept). */
    if (spawn_0018A880(4, 0) < 0)
        return -1;
    /* 0015C310(player, 0). */
    const uint8_t *ca = em_scene_progress_at(s_scene, 0x00810CA4u, 4);
    if (!ca)
        return fault(0x0015C310u, EM_SCENE_FAULT_BAD_INDEX, "D_00810CA4..CA7 not canonical");
    uint8_t ca4 = ca[0], ca5 = ca[1], ca6 = ca[2], ca7 = ca[3];
    if (spawn_0018A880(0, 0) < 0 || spawn_0018A880(1, 0) < 0 || spawn_0018A880(1, 0x10) < 0)
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
    /* 0015C420: 001F0120(player, 0x3B); +0x24 = player +0x14 = 0x008102B0. */
    return spawn_001F0120(EM_SCENE_D_008102B0, 0x3B, 0);
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
    EmActor *p = spawn_001EF9D0(&k_effect_0x17);
    if (!p)
        return 0;
    p->pos[0] = p->pos[1] = p->pos[2] = 0.0f; /* D_00250F00 */
    p->rot[0] = p->rot[1] = p->rot[2] = p->rot[3] = 0.0f;
    p->pos[3] = 1.0f;
    return bind_node(p, NULL);
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
