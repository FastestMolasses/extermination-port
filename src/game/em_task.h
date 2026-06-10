/* em_task.h — the engine's 3-slot frame-task system, translated to native C.
 *
 * Faithful structural translation of the PS2 engine's task table (decomp
 * repo, docs/FINDINGS.md "ENGINE FRAME ANATOMY"):
 *
 *   - The original keeps a 3 x 0x20-byte task table at 0x0028A750.
 *     Each record: byte +0 = state, word +4 = function pointer, and the
 *     remaining bytes are task-private state (the live game task keeps its
 *     state-machine bytes at record +8 / +9 / +0xB).
 *   - func_001AB650  zeroes the table            -> em_task_init()
 *   - func_001AB740  installs (slot, fn)         -> em_task_register()
 *   - func_001AB6A0  dispatches all slots        -> em_task_dispatch()
 *
 * STATE BYTE semantics (verified live in PCSX2):
 *   0        = FREE  — slot skipped
 *   1 or 4   = START/WAKE — promoted to 2 by the dispatcher
 *   2        = RUN   — fn called once per dispatch (i.e. once per frame)
 *
 * The task fn takes no arguments; while it runs, a pointer to its own slot
 * record is "parked" for it (the original parks it in scratchpad 0x70003B6C)
 * — retrieve it with em_task_current(). A running task may re-register its
 * own slot to replace itself: that is exactly how the engine's boot/flow
 * task (func_001AB7E0) hands slot 0 over to the game task (func_001ACEC0).
 *
 * Clean-room: structure and semantics from our own live-debug notes; all
 * code here is original.
 */
#ifndef EM_TASK_H
#define EM_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_TASK_SLOTS      3
#define EM_TASK_USER_BYTES 24   /* record bytes +8..+0x1F in the original */

/* State byte values (record byte +0). */
enum {
    EM_TASK_STATE_FREE  = 0,  /* skip */
    EM_TASK_STATE_START = 1,  /* promote to RUN at next dispatch */
    EM_TASK_STATE_RUN   = 2,  /* run every dispatch */
    EM_TASK_STATE_WAKE  = 4   /* alternate promote-to-RUN entry state */
};

typedef void (*EmTaskFn)(void);

/* One task record — mirrors the original 0x20-byte slot layout. */
typedef struct {
    uint8_t  state;                     /* byte +0 */
    EmTaskFn fn;                        /* word +4 */
    uint8_t  user[EM_TASK_USER_BYTES];  /* +8.. : task-private state bytes */
} EmTask;

/* Zero the whole table (func_001AB650). */
void em_task_init(void);

/* Install `fn` into `slot` (0..2) with state START and cleared user bytes;
 * it will first run on the next dispatch (func_001AB740). Returns the slot
 * record, or NULL if slot is out of range. */
EmTask *em_task_register(int slot, EmTaskFn fn);

/* Run one frame's worth of tasks (func_001AB6A0): for each slot in order,
 * promote START/WAKE to RUN, then call fn if the state is RUN. */
void em_task_dispatch(void);

/* The slot record of the currently running task (the scratchpad-0x70003B6C
 * mirror). NULL outside of a dispatch. */
EmTask *em_task_current(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_TASK_H */
