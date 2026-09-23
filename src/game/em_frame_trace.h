/* Native frame-order trace (WP-3 step S6, SCENE_COORDINATOR_DESIGN.md
 * section 3.3). Instrumentation only: nothing in here changes game behaviour.
 *
 * Output is JSONL, one line per task tick:
 *
 *   {"counter":N,"task":[+8,+9,+A,+B,+C]|null,"classifier":R|null,
 *    "selector":S|null,"fade":F|null,"events":[...]}
 *
 * - task/selector/fade are sampled where the original tracer sampled them:
 *   at the entry of 0x1AE040 (the task bytes +8..+0xC of *(0x70003B6C), the
 *   scratchpad selector 0x70003B8D; fade is D_0028A9A0, which the original
 *   trace did not record). null when 0x1AE040 was not entered that tick.
 * - classifier is the 001AE7E0 result (null when it was not called).
 * - A worker call is {"fn":"<caller>","target":"<callee>","args":[a0,a1,a2,a3]}
 *   (addresses as 8 upper-case hex digits, args as the unsigned 32-bit values
 *   the original call site sets up).
 * - A walk node is {"fn":"001AFD70","op":"jalr","callback":"<+0x10>",
 *   "class":<+2>,"record":"<tag>"|null,"binding":"<name>"|null}. `record` uses
 *   the original tracer's vocabulary: "area11[i]" for placement table
 *   0x82A3C0 record i, "deferred[gG.J]" for D_0024D820 group G record J;
 *   null for nodes no table record spawned. Nodes are matched on callback +
 *   record, never on node address (LIFO slot reuse, design risk 8).
 *
 * Errors are never silent: a protocol violation (an event outside a tick, a
 * tick begun twice, a second classifier or frame state in one tick) or an
 * allocation/write failure latches the recorder, writes one
 * {"error":"..."} line (tools/compare_frame_order.py fails any trace that
 * contains one), reports on stderr, and stops recording.
 *
 * libc only; no dependency on any port subsystem.
 */
#ifndef EM_FRAME_TRACE_H
#define EM_FRAME_TRACE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EmFrameTrace {
    FILE *fp;
    int error;             /* latched; nothing is recorded after it */
    int in_tick;
    uint32_t counter;
    int have_state;
    uint8_t task[5];       /* task bytes +8, +9, +A, +B, +C */
    uint8_t selector;      /* spad 0x70003B8D */
    int32_t fade;          /* D_0028A9A0 */
    int have_classifier;
    int32_t classifier;
    char *events;          /* JSON text of the current tick's events */
    size_t len, cap;
    size_t nevents;
    uint32_t ticks;        /* lines written */
} EmFrameTrace;

/* Open `path` for writing (truncates). 0 on success, -1 on failure. */
int em_frame_trace_open(EmFrameTrace *t, const char *path);
/* Close; a tick still open at close is a protocol error (error line). Returns
 * 0 when no error was latched during the recorder's life, else -1. */
int em_frame_trace_close(EmFrameTrace *t);
int em_frame_trace_failed(const EmFrameTrace *t);

void em_frame_trace_tick_begin(EmFrameTrace *t, uint32_t counter);
/* Sampled at the entry of 0x1AE040 (see above). */
void em_frame_trace_frame_state(EmFrameTrace *t, const uint8_t task[5], uint8_t selector,
                                int32_t fade);
void em_frame_trace_classifier(EmFrameTrace *t, int32_t result);
void em_frame_trace_call(EmFrameTrace *t, uint32_t caller, uint32_t callee, uint32_t a0,
                         uint32_t a1, uint32_t a2, uint32_t a3);
void em_frame_trace_node(EmFrameTrace *t, uint32_t callback, uint8_t cls, const char *record,
                         const char *binding);
/* Writes the tick's line and flushes. 0 on success, -1 when latched. */
int em_frame_trace_tick_end(EmFrameTrace *t);

/* ---- process recorder gated on EM_FRAME_TRACE=<path> ----
 * em_frame_trace_env() opens the file named by EM_FRAME_TRACE on first use
 * (the close is registered with atexit) and returns the recorder, or NULL when
 * the variable is unset or empty. A set variable whose file cannot be opened
 * is reported on stderr and yields NULL (no file: the comparator fails). */
EmFrameTrace *em_frame_trace_env(void);
/* Signature-compatible with EmSceneTraceFn (em_scene_workers.h), so the
 * bindings can install it as the worker table's trace hook. `ctx` is the
 * worker table's context and is not used; the call is recorded into
 * em_frame_trace_env() when tracing is enabled, and ignored otherwise. */
void em_frame_trace_env_hook(void *ctx, uint32_t caller, uint32_t callee, uint32_t a0,
                             uint32_t a1, uint32_t a2, uint32_t a3);

#ifdef __cplusplus
}
#endif

#endif /* EM_FRAME_TRACE_H */
