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

/* LEGACY LOAD (retired by S12a). The port's native loader (em_game.c
 * game_load_task) stands in for the original load arms 001AD1A0, 001AD230,
 * 001AD360 and 001ADF50. It installs em_scene_task_001ACEC0 and then calls
 * this with the replaced record, which writes the task bytes 001ADF50's
 * completion leaves in 001ACEC0 state 3 (001AD250 +9=5 arm: +9=1, +A=+B=0;
 * +8=3 from 001ACEC0 state 1/2). The first task tick then enters 0x1AE040
 * state 0 (the rebuild, no world frame); the second is the first world
 * frame. */
void em_scene_bindings_legacy_loaded(EmTask *record);

/* ---- Legacy port code the bindings call (implemented in em_game.c) ----
 * Each one is today's code, moved unchanged out of the retired
 * ingame_frame_machine (S8) and gameplay_frame/cutscene_frame (S10a)
 * bodies. */

/* The port's native state-0 re-arm (player/camera/spawn/test fixtures),
 * bound at the 0x1AE040 state-0 position of 001AFCA0. */
void em_game_legacy_state0(void);

/* The interim 001AC070 (EmTaskFn; S11b): the task 001ADF00's
 * 001AB790(0x1AC070) installs. It runs the port's legacy continue prompt
 * (FLAGGED stand-in until 001AC070/001AC480 are translated) and, on option
 * 0, the port's Continue restart, after which it reinstalls
 * em_scene_task_001ACEC0 through em_scene_bindings_legacy_loaded (the
 * 001AC070 state 4 counterpart). */
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
 * em_game_legacy_pickup_update is the item owners' half of em_pickup_update
 * (the light children are em_pickup_lights_tick), and (0) is the cutscene
 * block's scan-less call;
 * em_game_legacy_player_residue is the weapon update (WP-15), which has no
 * pool owner in the original (the damage/vitals tick moved to the player
 * stage in S11b). */
void em_game_legacy_collision_clears(void);
int em_game_legacy_door_tick(void);
void em_game_legacy_pickup_update(int gameplay);
void em_game_legacy_pickup_collect(void);
void em_game_legacy_examine_tick(void);
void em_game_legacy_enemy_tick(void);
void em_game_legacy_player_residue(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_SCENE_BINDINGS_H */
