/* Scene coordinator bindings (WP-3, SCENE_COORDINATOR_DESIGN.md section 3.1).
 *
 * The only module that knows both the coordinator cores (em_scene_task.c,
 * em_scene_frame.c, em_scene_classify.c) and the port. It owns the static
 * canonical EmSceneState, fills the EmSceneWorkers table, and exports the
 * slot-0 game task em_scene_task_001ACEC0.
 *
 * STEP S8 (LEGACY MODE). The slot-0 task runs the original chain
 * 001ACEC0 -> 001AD250 -> 001AD4D0 -> 0x1AE040 through the cores. Two
 * bindings-only flags keep today's port behaviour until the steps that
 * retire them:
 *   - legacy_state0_frame (retired by S9): after 0x1AE040 state 0 returns,
 *     the bindings run state 1 in the same tick (the port's historical
 *     fall-through; the original draws no world frame on the state-0 tick).
 *   - classifier shadow (retired by S11a/S11b): the canonical input words
 *     D_00810E74/E70 are not written yet, so the core's 001AE7E0 cannot see a
 *     button press and returns 0; the trace records a SHADOW classifier result
 *     computed over the canonical state with the step-C pad block's original
 *     layout words (em_frame_pad_block). It is recorded, never acted on.
 * The state-1 world frame is ONE legacy worker, bound to both 001AE5E0 and
 * 001AE6B0, that runs today's gameplay_frame/cutscene_frame (chosen by the
 * port's g.frame_selector, which S11a replaces with canonical 3B8D).
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
 * state 0, as the port's frame machine did before S8. */
void em_scene_bindings_legacy_loaded(EmTask *record);

/* ---- Legacy port code the S8 bindings call (implemented in em_game.c) ----
 * Each one is today's code, moved unchanged out of the retired
 * ingame_frame_machine body. */

/* The port's native state-0 re-arm (player/camera/spawn/test fixtures),
 * bound at the 0x1AE040 state-0 position of 001AFCA0. */
void em_game_legacy_state0(void);

/* The port's Continue restart (game-over option 0, g.go_restart), serviced
 * at the 0x1AE040 entry while +B == 1. Returns 0 when no restart is pending,
 * 1 when the restart ran and 0x1AE040 must rebuild from state 0 this tick,
 * -1 when the restart area could not be loaded (quit already requested; no
 * frame this tick). Retired by S11b (001AD140 -> 001AD4E0 -> 001ADF00). */
int em_game_legacy_continue_restart(void);

/* Today's world frame: em_opening_control_test_before_frame, then
 * cutscene_frame when g.frame_selector != 0, else gameplay_frame. */
void em_game_legacy_world_frame(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_SCENE_BINDINGS_H */
