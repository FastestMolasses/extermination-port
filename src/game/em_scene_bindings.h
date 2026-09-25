/* Scene coordinator bindings (WP-3, SCENE_COORDINATOR_DESIGN.md section 3.1).
 *
 * The only module that knows both the coordinator cores (em_scene_task.c,
 * em_scene_frame.c, em_scene_classify.c) and the port. It owns the static
 * canonical EmSceneState, fills the EmSceneWorkers table, and exports the
 * slot-0 game task em_scene_task_001ACEC0.
 *
 * STEPS S8-S11b. The slot-0 task runs the original chain
 * 001ACEC0 -> 001AD250 -> 001AD4D0 -> 0x1AE040 through the cores, once per
 * tick. Since S9 the state-0 tick returns without a world frame, as the
 * original does (0x1AE040 state 0 ends with a branch to its epilogue at
 * 0x1AE0DC); the port's former same-tick fall-through into state 1 is gone.
 * Since S11a the canonical input words D_00810E74/E70/E50 are written at the
 * start of every tick (em_frame_scene_input), so the core's 001AE7E0 acts on
 * real input. Since S11b the frame machine acts on the classifier: r == 2
 * opens the status screen (states 3/5, world frozen), and B9 leads to game
 * over (001AD140 -> 001AD4E0 -> 001ADF00 -> the interim 001AC070 task);
 * the unported arms (r == 1, r == 3, +9 = 3) fault at their NULL workers.
 * Since S10a the state-1 world frame runs the translated variants
 * em_sf_001AE5E0 / em_sf_001AE6B0, chosen by canonical 3B8D, which since
 * S11a is the port's only storage of that byte (and of 3B91). The variants'
 * stage workers are the stage functions of em_player_frame.c /
 * em_render_frame.c. Since S10b the
 * 001AFD70 positions walk the native actor pool (em_actor_pool.c) built at
 * state 0 from the AREA11 roster, with per-node behaviours from
 * em_area11_bindings.c; a scene without an original roster holds one
 * legacy_world node that runs the S10a legacy block.
 */
#ifndef EM_SCENE_BINDINGS_H
#define EM_SCENE_BINDINGS_H

#include "game/em_scene_state.h"
#include "game/em_task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The canonical coordinator state (design 3.2). Every module reads and
 * writes the original bytes it owns through this pointer; none keeps a copy. */
EmSceneState *em_scene_state(void);

/* The slot-0 game task (EmTaskFn): one tick of the original 001ACEC0. */
void em_scene_task_001ACEC0(void);

/* S12a: New Game and Continue register em_scene_task_001ACEC0 with a
 * cleared record and the chain runs the original load arms (001AD1A0,
 * 001AD230, 001AD360, 001ADF50) through the bindings. The EM_SKIP_STARTUP
 * fixture (em_game_install: not an original route) reads its scene at once
 * and calls this with the registered record, which writes the task bytes a
 * completed 001ADF50 leaves (001AD250 +9=5 arm: +9=1, +A=+B=0; +8=3), and
 * for an AREA11 scene 001AD360 step 4's area bytes. The first task tick then
 * enters 0x1AE040 state 0 (the rebuild, no world frame). */
void em_scene_bindings_fixture_loaded(EmTask *record);

/* 001B0C60(a, b, c) (byte-matched): the area-change request of the fan exit
 * and Roger: spad 3B8D = 3, 001B0C00(4) (001AEDE0(4, 0), the three 001FAD70
 * stream fades, reported), then D_008106B8 = 1, B5 = a, B7 = c, B6 = b. The
 * frame machine then calls 001AD010 at fade substate 2 (area a, room b,
 * entry c; b = 0xFF takes the room from D_00810730[a]). 0. */
int em_scene_request_area_change_001B0C60(int a, int b, int c);

/* The message service's stream workers (em_message_live.h, WP-8):
 * 001FD470(mask) and 001FA790(lane, cue). 1 ok, 0 fault. */
int em_scene_bindings_001FD470(void *ctx, int32_t mask);
int em_scene_bindings_001FA790(void *ctx, int lane, int32_t cue);

/* 00119828(ch, l, r), the IOP command 0x16 packer, for callers outside the
 * frame machine (the opening's 001B82D0 ops 9..12 phase 0). The port has no
 * 001157F0 sink yet (docs/STREAM_LANES.md "Still missing"): (0/1, 0x3FFF,
 * 0x3FFF) changes nothing, any other value is a reported no-effect binding
 * (UM_00119828). 0 always. */
int em_scene_bindings_00119828(void *ctx, int32_t ch, int32_t l, int32_t r);

/* The number of live nodes in the actor pool (D_00275BC0 list length) when
 * the pool holds the AREA11 roster, else -1 (test instrumentation). */
int em_scene_bindings_pool_census(void);
/* The number of live AREA11 pool nodes with this original callback, or -1
 * without a roster pool (test instrumentation, S12b). */
int em_scene_bindings_pool_count(uint32_t callback);
/* The binding name the frame trace records for the first live AREA11 pool
 * node with this original callback, or NULL (no roster pool, or no such
 * node). Test instrumentation (the S13 level smoke's NOT-LIVE lines). */
const char *em_scene_bindings_pool_binding(uint32_t callback);
/* The original record address of a pool record (0 outside the pool; test
 * instrumentation: the level smoke's player-ground check). */
uint32_t em_scene_bindings_pool_address(const void *actor);

/* ---- Legacy port code the bindings call (implemented in em_game.c) ----
 * Each one is today's code, moved unchanged out of the retired
 * ingame_frame_machine (S8) and gameplay_frame/cutscene_frame (S10a)
 * bodies. */

/* The port's native state-0 re-arm (player state), bound at the 0x1AE040
 * state-0 position of 001AFCA0; for a scene without an original roster the
 * bindings follow it with the manifest spawn, the camera re-arm and the
 * fixtures (the pre-S12a order). */
void em_game_legacy_state0(void);
void em_game_legacy_manifest_spawn(void);
/* The legacy chase-camera re-arm; also the reported 001B0460 stand-in at the
 * end of 001B07C0 (AREA11). */
void em_game_legacy_camera_rearm(void);
/* Port fixtures that read the placed pose (weapon context, self-test enemy
 * spawns) and the opening runtime's asset bind. */
void em_game_legacy_state0_fixtures(void);

/* The native area read (the bindings' 001FF080(1, 0), and the
 * EM_SKIP_STARTUP fixture): 0, or -1 when the scene cannot be loaded. */
int em_game_legacy_area_load(const char *dir);

/* 001AD230's 001AF2C0 (New Game reset), as the port mirrors it. */
void em_game_new_game_reset_001AF2C0(void);

/* The interim 001AC070 (EmTaskFn; S11b): the task 001ADF00's
 * 001AB790(0x1AC070) installs. It runs the port's legacy continue prompt
 * (FLAGGED stand-in until 001AC070/001AC480 are translated) and, on option
 * 0, the port's Continue restart, after which it reinstalls
 * em_scene_task_001ACEC0 with a cleared record (001AC070 state 4:
 * D_00275BE0 = 0, 001AB790(001ACEC0)), so the New Game route runs again. */
void em_game_legacy_continue_task_001AC070(void);

/* Head of a world-frame variant (cutscene != 0: 001AE6B0, else 001AE5E0):
 * test instrumentation only (its status/game-over frozen frame was retired
 * by S11b). */
void em_game_legacy_variant_head(int cutscene);

/* The S10a legacy blocks (001AE5E0 walks mode 0, 001AE6B0 mode 1): the
 * rest of the retired gameplay_frame / cutscene_frame world updates in their
 * old relative order. Since S10b they are only the behaviour of the one
 * `legacy_world` pool node of a scene without an original roster. */
void em_game_legacy_pool_gameplay(void);
void em_game_legacy_pool_cutscene(void);

/* The pieces em_game_legacy_pool_gameplay is made of (S10b), which the
 * AREA11 node adapters (em_area11_bindings.c) call at their owners' nodes.
 * em_game_legacy_door_tick returns 1 when it consumed a goto scene switch;
 * em_game_legacy_player_residue is the weapon update (WP-15), which has no
 * pool owner in the original (the damage/vitals tick moved to the player
 * stage in S11b). */
void em_game_legacy_collision_clears(void);
int em_game_legacy_door_tick(void);
void em_game_legacy_examine_tick(void);
void em_game_legacy_enemy_tick(void);
void em_game_legacy_player_residue(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_SCENE_BINDINGS_H */
