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
 *   - func_001AB790  replaces the running task  -> em_task_replace_current()
 *   - func_001AB6A0  dispatches all slots        -> em_task_dispatch()
 *
 * STATE BYTE semantics (verified live in PCSX2):
 *   0        = FREE  — slot skipped
 *   1 or 4   = START/WAKE — promoted to 2 by the dispatcher
 *   2        = RUN   — fn called once per dispatch (i.e. once per frame)
 *
 * The task fn takes no arguments; while it runs, a pointer to its own slot
 * record is "parked" for it (the original parks it in scratchpad 0x70003B6C)
 * — retrieve it with em_task_current(). A running task can replace itself;
 * the new function first runs when dispatch next reaches that slot. The
 * boot task (func_001AB7E0) replaces itself with the title/screen-flow task
 * (func_001AC070), which later hands off to the game task (func_001ACEC0).
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
#define EM_TASK_RESET_BYTES 16  /* registration clears only +8..+0x17 */

/* State byte values (record byte +0). */
enum {
    EM_TASK_STATE_FREE  = 0,  /* skip */
    EM_TASK_STATE_START = 1,  /* promote to RUN at next dispatch */
    EM_TASK_STATE_RUN   = 2,  /* run every dispatch */
    EM_TASK_STATE_WAKE  = 4   /* alternate promote-to-RUN entry state */
};

typedef void (*EmTaskFn)(void);

/* Native representation, NOT a binary overlay of the PS2's 0x20-byte
 * record. The host function pointer has native alignment and width;
 * the offsets below identify the corresponding original fields only. */
typedef struct {
    uint8_t  state;                     /* byte +0 */
    EmTaskFn fn;                        /* word +4 */
    uint8_t  user[EM_TASK_USER_BYTES];  /* +8.. : task-private state bytes */
} EmTask;

/* Zero the whole table (func_001AB650). */
void em_task_init(void);

/* Install `fn` into `slot` (0..2) with state START, clearing user[0..15]
 * and preserving user[16..23] (func_001AB740). Dispatch promotes it and
 * calls it when it next reaches this slot, including a later slot in the
 * current dispatch. Returns the slot record, or NULL for an invalid slot. */
EmTask *em_task_register(int slot, EmTaskFn fn);

/* Replace the running task with the same reset/preservation rules
 * (func_001AB790). This does not immediately call the replacement.
 * Returns the current record, or NULL outside a task callback. */
EmTask *em_task_replace_current(EmTaskFn fn);

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
