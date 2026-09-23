/* AREA11 pool-node bindings (WP-3 step S10b; SCENE_COORDINATOR_DESIGN.md
 * sections 4.3, 4.4 and 10.2/10.3).
 *
 * Since S10b the 001AFD70 position of both world-frame variants is the real
 * pool walk (em_actor_pool_walk_001AFD70). This module gives every node the
 * walk can reach a native behaviour, keyed on the original callback (+0x10):
 *   - the per-callback table of design 4.4: each AREA11 owner either runs
 *     the port's legacy code for it (a piece of the retired S10a block, a
 *     group adapter, or an existing runtime), or is an explicit no-port-code
 *     node (UNBOUND, dormant, static, render-only, drawn at the close-out).
 *     No-port-code nodes return 1 and are reported once on stderr, so a
 *     trace that shows the node is never mistaken for the port running it.
 *     A node with no table entry is left unbound and the walk faults at its
 *     callback (fail-stop);
 *   - the spawns of the dynamic tail (design 4.3, 10.2 Q4/Q5; measured in
 *     ORIGINAL_FRAME_ORDER.md section 6), each named with its original
 *     spawner: 0018A880 (x7 from 0015C420/0015C310) and 001F0120 (0x3B) at the
 *     first player stage, 001C5570 children from the owners' first ticks,
 *     001F0120(Roger, 0x47), and the 001C1EA0 weather node;
 *   - the one `legacy_world` node of a scene without an original roster
 *     (office, drawbridge), whose behaviour is the S10a legacy block, so
 *     those scenes keep their exact legacy call order.
 *
 * INTERIM spawns (design 4.3 "interim_spawn"): a spawn whose original call
 * site sits in overlay code the port cannot read (0x825940, 0x8237E0,
 * 0x827B10) or whose selecting input has no port writer yet (001C1EA0 reads
 * D_008106C8, written by 001B0250 from the S12a spawn table). Their timing
 * and argument bytes are the measured ones; they are flagged `interim` in
 * the binding name the trace records.
 *
 * Not modelled (fail-stop or documented at the adapter):
 * - bone-slot exhaustion (001B0EA0 via 001B0FD0): the port has no
 *   D_00275BCC stack; every spawn behaves as if the slots were available,
 *   which is what the captured first frame shows;
 * - child record bytes EmActor has no field for (+0x24 owner, +0x38, +0xA0):
 *   the owner link is kept here (em_area11_node_link), the others are not
 *   stored.
 */
#ifndef EM_AREA11_BINDINGS_H
#define EM_AREA11_BINDINGS_H

#include <stdint.h>

#include "game/em_actor_pool.h"
#include "game/em_actor_roster.h"
#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The walk's `world` argument. */
typedef struct {
    int cutscene; /* 0: 001AE5E0 walks mode 0; 1: 001AE6B0 walks modes 1 and 2 */
} EmArea11World;

/* The pool and canonical state the spawns allocate in (the bindings' own). */
void em_area11_bindings_attach(EmActorPool *pool, EmSceneState *scene);

/* After 001AF8E0: forget every node's native state and the group heads. */
void em_area11_bindings_reset(void);

/* EmActorRosterBindFn for em_actor_roster_spawn_001B6990/_001C5C50: binds
 * the node's behaviour by callback and records its trace tag. Returns 0, or
 * -1 for a callback with no binding row (the roster then faults). */
int em_area11_bind_roster(void *ctx, EmActor *actor, const EmActorRosterSpawned *spawned);

/* 0015C420's pool children (the player-init path 0015BA50 -> 0015C420 of
 * the first 0015BCF0 after the 001AF5C0 player wipe): 0018A880(4,0), then
 * 0015C310(player, 0) (0018A880 x3 plus the D_00810CA4 branch and the
 * D_00810CA6 == 4 extra), then 001F0120(player, 0x3B). Returns 0 or -1. */
int em_area11_spawn_player_children_0015C420(void);

/* 001C1DC0 -> 001C1EA0 -> 001EFD20(0x80000017, D_00250F00): the weather
 * node (INTERIM: D_008106C8 has no port writer; see the .c). 0 or -1. */
int em_area11_spawn_weather_001C1EA0(void);

/* The `legacy_world` node of a scene without a roster (callback 0,
 * class 0, so both cutscene walks treat it as a non-class-1 node).
 * 0 or -1. */
int em_area11_spawn_legacy_world(void);

/* Around a walk of the AREA11 roster pool. begin: the collision-registry
 * clears the legacy blocks ran first (modes 0 and 1). end: the port-native
 * draw-list collector and, in gameplay, the legacy player residue. */
void em_area11_walk_begin(int mode);
void em_area11_walk_end(int mode);

/* Instrumentation for the frame trace: the original tracer's record tag
 * ("area11[i]", "deferred[gG.J]", or NULL for runtime nodes) and the
 * binding name. */
const char *em_area11_node_record(const EmActor *actor);
const char *em_area11_node_binding(const EmActor *actor);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA11_BINDINGS_H */
