/* Scene coordinator bindings (WP-3, SCENE_COORDINATOR_DESIGN.md section 3.1).
 *
 * The only module that knows both the coordinator cores (em_scene_task.c,
 * em_scene_frame.c, em_scene_classify.c) and the port. It owns the static
 * canonical EmSceneState, fills the EmSceneWorkers table, and exports the
 * slot-0 game task em_scene_task_001ACEC0.
 *
 * STEPS S8-S10a (LEGACY MODE). The slot-0 task runs the original chain
 * 001ACEC0 -> 001AD250 -> 001AD4D0 -> 0x1AE040 through the cores, once per
 * tick. Since S9 the state-0 tick returns without a world frame, as the
 * original does (0x1AE040 state 0 ends with a branch to its epilogue at
 * 0x1AE0DC); the port's former same-tick fall-through into state 1 is gone.
 * One bindings-only flag keeps today's port behaviour until the steps that
 * retire it:
 *   - classifier shadow (retired by S11a/S11b): the canonical input words
 *     D_00810E74/E70 are not written yet, so the core's 001AE7E0 cannot see a
 *     button press and returns 0; the trace records a SHADOW classifier result
 *     computed over the canonical state with the step-C pad block's original
 *     layout words (em_frame_pad_block). It is recorded, never acted on.
 * Since S10a the state-1 world frame runs the translated variants
 * em_sf_001AE5E0 / em_sf_001AE6B0, chosen by canonical 3B8D. Until S11a
 * makes 3B8D the port's only storage, the bindings publish the port's
 * g.frame_selector into it at the 0x1AE040 entry (the port's 3B8D writers
 * still write g.frame_selector). The variants' stage workers are the stage
 * functions of em_player_frame.c / em_render_frame.c; the 001AFD70
 * position is one legacy block per variant (retired by S10b).
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

/* The port's Continue restart (game-over option 0, g.go_restart), serviced
 * at the 0x1AE040 entry while +B == 1. Returns 0 when no restart is pending,
 * 1 when the restart ran and 0x1AE040 must rebuild from state 0 this tick,
 * -1 when the restart area could not be loaded (quit already requested; no
 * frame this tick). Retired by S11b (001AD140 -> 001AD4E0 -> 001ADF00). */
int em_game_legacy_continue_restart(void);

/* Head of a world-frame variant (cutscene != 0: 001AE6B0, else 001AE5E0):
 * test instrumentation, and in the gameplay variant the status/game-over
 * frozen frame (retired by S11b). Returns 1 when the frozen frame ran and
 * the variant's stages must not run this tick, else 0. */
int em_game_legacy_variant_head(int cutscene);

/* The 001AFD70 position of each variant (001AE5E0 walks mode 0, 001AE6B0
 * mode 1): one block holding the rest of the retired gameplay_frame /
 * cutscene_frame world updates in their old relative order. Retired by
 * S10b (the pool walk). */
void em_game_legacy_pool_gameplay(void);
void em_game_legacy_pool_cutscene(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_SCENE_BINDINGS_H */
