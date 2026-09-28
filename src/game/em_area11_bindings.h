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
 *     001F0120(Roger, 0x47) (Roger's own lifecycle 0 since census L22,
 *     em_area11_bindings_spawn_001F0120), and the 001C1EA0 weather node;
 *   - the one `legacy_world` node of a scene without an original roster
 *     (office, drawbridge), whose behaviour is the S10a legacy block, so
 *     those scenes keep their exact legacy call order.
 *
 * INTERIM spawns (design 4.3 "interim_spawn"): a spawn whose original call
 * site sits in overlay code the port has no translation of (0x827B10).
 * (Since S12a 001C1EA0's input D_008106C8 is written by 001B0250
 * from the spawn table, so the weather node is no longer interim.) Their timing
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
#include "game/em_script_door_fan.h"

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

/* At the area build (001AFCA0, after the pool reset): the effect and
 * equipment binders over this pool (em_effects_live, em_equipment_live),
 * then 001D0660's 001F0310. 0, or -1. */
int em_area11_bindings_effects_attach(void);

/* 0015C310(player, arg1): the equipment children (arg1 = 0: (0,0), (1,0),
 * (1,0x10); both: the flavour-2 nodes of D_00810CA4..CA7 and, with
 * D_00810CA6 == 4, (1,0x15)). 0, or -1. */
int em_area11_spawn_player_equipment_0015C310(int32_t arg1);

/* 001F0120(owner, key) for the keys 001E2290 admits (0x3B, 0x47): the
 * head-bone sprite node linked to `owner_address` (+0x24 = owner +0x14).
 * 0 (also when the alloc is refused, as the original returns 0), or -1. */
int em_area11_bindings_spawn_001F0120(uint32_t owner_address, uint8_t key);

/* 001BAC00(owner, script, record) (script op14; the opening 0x828FC0):
 * em_sdf_001BAC00 over `owner`'s record and the list its record's +0x14
 * names in `image`, with the pool's 001AFA90; each spawned record takes
 * the entry's stores (+0x03, +0x0D, +0xB0.., +0xC0.., +0x10, +0x2E) and is
 * bound by its +0x10 (001BB0E0: em_area11_roger_opening_tick), its +0x20 /
 * +0x24 kept by the binder. *result = the original result (1). 0, or -1. */
int em_area11_bindings_001BAC00(EmActor *owner, const uint8_t *record, const EmSdfImage *image,
                                int32_t *result);

/* 001C1DC0 -> 001C1EA0 -> 001EFD20(0x80000017, D_00250F00/10/20): the
 * weather node chosen by the canonical D_008106C8 (see the .c). 0 or -1. */
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

/* The tick log's record of the security gun 00825940, its cable 00827490
 * and the fan pair 00827630 (tools/test_level_smoke.py check_gun_fan):
 * float fields are bit patterns. */
typedef struct {
    uint32_t address, callback;
    uint8_t b00, b04, b05, b09;  /* +0x00, +0x04, +0x05, +0x09 */
    int16_t h28, h34;            /* +0x28, +0x34 */
    uint16_t h36;                /* +0x36 */
    uint32_t f38;                /* +0x38 (the fan's spin) */
    uint32_t rot[4];             /* +0xC0..+0xCC (the fan's +0xC8) */
    uint32_t bone3_78;           /* the gun: bone 3 (slot 3) +0x78 */
    uint32_t w220;               /* the gun: +0x220, its lamp's address */
    uint32_t lamp_a0[4];         /* the gun: its lamp's +0xA0 colour */
} EmArea11GunFanLog;
/* 1 and *out for a bound gun, cable or fan node, else 0. */
int em_area11_bindings_gun_fan_log(const EmActor *actor, EmArea11GunFanLog *out);

/* Instrumentation for the frame trace: the original tracer's record tag
 * ("area11[i]", "deferred[gG.J]", or NULL for runtime nodes) and the
 * binding name. */
const char *em_area11_node_record(const EmActor *actor);
const char *em_area11_node_binding(const EmActor *actor);

/* 00159210 state 1 / sub 2 (the panel's completion, through the host's
 * panel program): when its child slot (+0x20, the 0x75 indicator node) is
 * non-zero, the child gets +4 = 3 and the slot is cleared; the original
 * checks the slot, so an empty slot does nothing. 1 (stopped) or 0 (empty
 * slot); never fails. */
int em_area11_bindings_panel_child_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA11_BINDINGS_H */
