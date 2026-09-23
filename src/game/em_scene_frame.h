/* Scene frame core (WP-3 step S2, SCENE_COORDINATOR_DESIGN.md sections 2.3,
 * 2.4 and 3.1): translations of the original in-game frame machine 0x1AE040
 * (Extermination/src/anim_frame_top_b.c, NEARMISS; checked against its splat
 * .s) and its two world-frame variants 001AE5E0 (gameplay) and 001AE6B0
 * (cutscene) (NEARMISS; checked against their .s).
 *
 * The cores read and write only EmSceneState (em_scene_state.h) and the task
 * bytes +8..+0x1F in `user` (EmTask.user; original offset +k is user[k-8]).
 * Every original callee is reached through the EmSceneWorkers table
 * (em_scene_workers.h); a reached NULL worker or reader latches a fault and
 * the core stops (fail-stop), a negative worker result latches a fault.
 *
 * 0x1AE040 states 3 and 5 and the classifier r == 2 entry delegate to the
 * unchanged em_status_frame_enter/tick (design 3.1) through a view of +B, +C,
 * +0x11, C4 and EF. The view is published to the canonical storage before
 * every worker call and refreshed after it, so a worker sees exactly the bytes
 * the original would have stored by that point.
 *
 * Trace hook: every worker call is reported with the original caller/callee
 * and the a0..a3 values the original call site sets up. Registers the call
 * site does not set up are reported as 0; the cases are
 *   - 0x1AE040 state 2 (0022A650 == 3) -> 001AD140: a0 is 0022A650's leftover;
 *   - 001AE5E0's second 001CB590 call and both 001CB590 calls in 001AE6B0:
 *     a3 is a leftover (001CB590 stores only a0; anim_bone_array_setup reads
 *     none of a1..a3).
 * 0x1AE040's call to the classifier 001AE7E0 is also reported (a jal in the
 * original), although the classifier is em_sf_001AE7E0, not a worker.
 *
 * Verified by tools/test_scene_frame_reference.py, which executes the
 * original instructions at 0x1AE040 (with 0x1AE7E0, 001AE5E0 and 001AE6B0
 * executed) and at 0x1AE5E0 / 0x1AE6B0.
 *
 * Return value: 0 ok (including "nothing to do"), -1 when a fault is (or
 * already was) latched in s->fault.
 */
#ifndef EM_SCENE_FRAME_H
#define EM_SCENE_FRAME_H

#include <stdint.h>

#include "game/em_scene_state.h"
#include "game/em_scene_workers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 0x1AE040: one tick of the frame machine, dispatched on +B (0..6; any other
 * value does nothing, as the original's range guard). */
int em_sf_001AE040(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w);

/* The same tick with lead decision Q1 (design section 9; NOT original
 * behaviour): the classifier reads E74 with SELECT withheld
 * (em_scene_classify_q1). `select_withheld` (optional) receives 1 when the
 * classifier ran and SELECT was set in the canonical E74 (the caller logs
 * EM_SCENE_Q1_UNPORTED_MESSAGE), else 0. Everything else, including
 * 001AE6B0's E74 & 0x900 test, sees the canonical E74. */
int em_sf_001AE040_q1(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w,
                      int *select_withheld);

/* 001AE5E0: the gameplay world frame (0x1AE040 state 1, spad 3B8D == 0). */
int em_sf_001AE5E0(EmSceneState *s, const EmSceneWorkers *w);

/* 001AE6B0: the cutscene world frame (0x1AE040 state 1, spad 3B8D != 0). */
int em_sf_001AE6B0(EmSceneState *s, const EmSceneWorkers *w);

#ifdef __cplusplus
}
#endif

#endif /* EM_SCENE_FRAME_H */
