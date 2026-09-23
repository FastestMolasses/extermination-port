/* Scene coordinator task chain (WP-3 step S3, SCENE_COORDINATOR_DESIGN.md
 * sections 2.2 and 2.6): translations of the original game task installed in
 * frame-task slot 0 and its sub-machines.
 *
 *   001ACEC0  task entry, dispatch on +8           (NEARMISS C; .s checked)
 *   001AD250  sub-machine, dispatch on +9          (byte-matched)
 *   001AD360  New Game bring-up, steps on +A       (NEARMISS C; .s checked)
 *   001ADF50  area load, steps on +A               (byte-matched)
 *   001AD4E0  game-over screen, steps on +A        (byte-matched)
 *   001ADF00  game-over exit, replaces the task    (byte-matched)
 *   001AD010  fade-complete request consumer       (NEARMISS C; .s checked)
 *   001AD140  death request consumer               (byte-matched)
 *   001AFCF0  request/scratchpad reset             (byte-matched)
 *
 * Verified by tools/test_scene_task_reference.py, which executes the original
 * ELF instructions of every function above (and the memset 00121A28 that
 * 001AFCF0 calls) and compares state bytes, the state at every worker call,
 * and the ordered worker calls.
 *
 * Arguments: `s` is the canonical EmSceneState; `user` is EmTask.user of the
 * slot-0 task, where original record offset +k is user[k - 8] (the original
 * reaches the record through the scratchpad word 0x70003B6C); `w` is the
 * worker table. Every call to an original function that is not one of these
 * cores goes through `w`; a NULL table or entry is a fault (fail-stop).
 *
 * Return: -1 when a fault is latched (on entry, or by this call: NULL worker,
 * negative worker result, NULL `user`, or an original table index outside the
 * owned storage). Otherwise 0, or for 001AD360 and 001ADF50 the original
 * return value (0 = still working, 4 = done).
 *
 * Trace: besides the worker calls (em_scene_worker_enter), the cores report
 * their calls to other cores in this file and 001AFCF0's call to 00121A28
 * (memset of the owned request block, done in place) to w->trace, so the
 * trace carries the original jal order. The trace's a0..a3 are the low 32
 * bits of the argument registers (001ABF90 takes four 64-bit values; its
 * worker receives them whole).
 */
#ifndef EM_SCENE_TASK_H
#define EM_SCENE_TASK_H

#include <stdint.h>

#include "game/em_scene_state.h"
#include "game/em_scene_workers.h"

#ifdef __cplusplus
extern "C" {
#endif

int em_sf_001ACEC0(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w);
int em_sf_001AD250(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w);
int em_sf_001AD360(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w);
int em_sf_001ADF50(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w);
int em_sf_001AD4E0(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w);
int em_sf_001AD010(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w);
int em_sf_001AD140(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w);
/* 001ADF00 and 001AFCF0 do not touch the task record. */
int em_sf_001ADF00(EmSceneState *s, const EmSceneWorkers *w);
int em_sf_001AFCF0(EmSceneState *s, const EmSceneWorkers *w);
/* 0018AB00 (byte-matched), called by 0x1AE040 state 4 (S12b): writes
 * D_008106C6 from the equipment bytes D_00810CA4/CA7 (canonical D2 progress).
 * 0, or -1 with the fault latched (at 0x0018AB00) when the bytes are not
 * canonical. No callees. */
int em_sf_0018AB00(EmSceneState *s);

#ifdef __cplusplus
}
#endif

#endif /* EM_SCENE_TASK_H */
