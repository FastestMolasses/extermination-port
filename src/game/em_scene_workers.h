/* Worker table for the scene coordinator cores (WP-3, SCENE_COORDINATOR_DESIGN.md
 * sections 3.1 and 2.2-2.6).
 *
 * One typed pointer per original callee the cores (em_scene_frame.c,
 * em_scene_task.c) reach, named by its original address. The parameters are
 * the callee's own parameter list in the decomp C (Extermination/src); a
 * call-site register value the callee does not declare is not a worker
 * parameter, it is reported through the trace hook only. The known cases:
 *   - 0x1AE040 passes a0 = 0 to 001E0CC0 (declared void);
 *   - 0x1AE040 passes a0 = D_0028A9A0 to 001AD140 and 001AD010 (void);
 *   - 001AD1A0 passes a third argument to 001FF080 (declares two);
 *   - 001AD010's NEARMISS C passes (B7, B5) to 001FBC50 (declared void).
 * Addresses of original globals passed as arguments (D_008101E0, D_008102B0,
 * D_008101D0) are passed as the original address; the bindings map them.
 *
 * Return convention (design 3.1): every worker returns int. A negative value
 * is a worker failure, which the core latches as a fault. Workers of void
 * callees, and of callees whose result the coordinator ignores (0018D7B0,
 * 001FB9F0), return 0 on success. Workers of callees whose result the
 * coordinator tests (001AD1A0, 001AD230, 0020CDC0, 0021B550, 0022A650) return
 * the original value; an original value the port cannot represent as a
 * non-negative int is a failure. A reached NULL worker is a fault
 * (fail-stop); nothing is ever silently skipped.
 *
 * Readers return the ORIGINAL value of data the coordinator does not own.
 * Store workers write data the coordinator writes but another module owns.
 *
 * `trace` is instrumentation only: it may be NULL, and it never changes
 * behaviour. When set, the core calls it for every worker call with the
 * original caller and callee addresses and the a0..a3 values the original
 * call site sets up (floats as their IEEE-754 bits).
 *
 * stdint only (plus em_scene_state.h for the fault latch helpers).
 */
#ifndef EM_SCENE_WORKERS_H
#define EM_SCENE_WORKERS_H

#include <stdint.h>

#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Original data addresses passed as worker arguments. */
#define EM_SCENE_D_008101D0 0x008101D0u /* 001C1D00 argument in both variants */
#define EM_SCENE_D_008101E0 0x008101E0u /* 0018D7B0/0018C0D0/001CB590 argument */
#define EM_SCENE_D_008102B0 0x008102B0u /* 001CB590 argument in both variants */
/* Original function address passed to 001AB790 by 001ADF00. */
#define EM_SCENE_FN_001AC070 0x001AC070u

typedef void (*EmSceneTraceFn)(void *ctx, uint32_t caller, uint32_t callee, uint32_t a0,
                               uint32_t a1, uint32_t a2, uint32_t a3);

typedef struct EmSceneWorkers {
    void *ctx;            /* passed as the first argument of every entry */
    EmSceneTraceFn trace; /* optional */

    /* ---- readers (not owned by EmSceneState, design 3.2) ---- */
    int16_t (*r_0028A9A0)(void *ctx); /* transition substate (lh) */
    uint8_t (*r_00282157)(void *ctx); /* audio busy byte */
    uint32_t (*r_00275B44)(void *ctx); /* current-actor word set by 001CB590; handle value */
    uint8_t (*r_008102B9)(void *ctx);  /* byte 001AE5E0/001AE6B0 pass to 001CB590 */

    /* ---- stores to data owned outside the coordinator ---- */
    int (*s_00821058)(void *ctx, uint8_t value); /* 001AD360 step 1 writes 1 */
    int (*s_00275C78)(void *ctx, uint8_t value); /* 001AD360 step 1 writes 0 */
    int (*s_00810D38)(void *ctx, int32_t value); /* 001ADF00 writes 0 */

    /* ---- 0x1AE040 state 0 (design 2.3) ---- */
    int (*w_001AFCA0)(void *ctx);
    int (*w_001AFCF0)(void *ctx); /* bound to em_scene_task's core */
    int (*w_001B07C0)(void *ctx, int a0);
    int (*w_001B6990)(void *ctx);
    int (*w_001D19E0)(void *ctx);
    int (*w_001C1DC0)(void *ctx);
    int (*w_00199C50)(void *ctx);
    int (*w_001AEE40)(void *ctx, int16_t a0);
    int (*w_001FAE70)(void *ctx, int a0);
    int (*w_001C5C50)(void *ctx);
    int (*w_001D1EF0)(void *ctx);

    /* ---- 0x1AE040 state 4 ---- */
    int (*w_0018AB00)(void *ctx);
    int (*w_0018D7B0)(void *ctx, uint32_t a0, int a1);
    int (*w_0018C0D0)(void *ctx, uint32_t a0, int a1);
    int (*w_001AEE10)(void *ctx, int16_t a0, uint8_t a1);

    /* ---- 0x1AE040 state 1 ---- */
    int (*w_001FBC50)(void *ctx);
    int (*w_001FB9F0)(void *ctx, int a0, int a1, int a2, int a3);
    int (*w_0020E060)(void *ctx);
    int (*w_001FABB0)(void *ctx);
    int (*w_00119828)(void *ctx, int a0, int a1, int a2);
    int (*w_001AEDB0)(void *ctx, uint8_t a0);
    int (*w_001AE5E0)(void *ctx); /* world-frame variant, 3B8D == 0 */
    int (*w_001AE6B0)(void *ctx); /* world-frame variant, 3B8D != 0 */
    int (*w_001AD140)(void *ctx); /* bound to em_scene_task's core */
    int (*w_001AD010)(void *ctx); /* bound to em_scene_task's core */

    /* ---- 0x1AE040 state 2 ---- */
    int (*w_0022A650)(void *ctx); /* original result; 0x1AE040 acts on 1, 2, 3 */
    int (*w_001AF1C0)(void *ctx);
    int (*w_001AEBA0)(void *ctx, int16_t a0);
    int (*w_001AF150)(void *ctx);
    int (*w_001D2610)(void *ctx, float a0);

    /* ---- 0x1AE040 states 3, 5, 6 ---- */
    int (*w_001D1C50)(void *ctx);
    int (*w_001D2830)(void *ctx, int a0, int a1);
    int (*w_0020CDC0)(void *ctx); /* original result; state 3 tests != 0 */
    int (*w_001E0CC0)(void *ctx);
    int (*w_001D1EA0)(void *ctx, int a0);
    int (*w_001FF030)(void *ctx, uint8_t a0);
    int (*w_001FEFE0)(void *ctx, uint8_t a0); /* declared char; caller passes lbu CF */

    /* ---- world-frame variants 001AE5E0 / 001AE6B0 (design 2.4) ---- */
    int (*w_001CB590)(void *ctx, uint32_t a0, int a1, int a2, int a3);
    int (*w_0015BCF0)(void *ctx, uint32_t actor);
    int (*w_001CB5A0)(void *ctx);
    int (*w_001C1D00)(void *ctx, uint32_t a0);
    int (*walk_001AFD70)(void *ctx, int mode);
    int (*w_0015C160)(void *ctx);
    int (*w_001F0360)(void *ctx);
    int (*w_0018B9C0)(void *ctx, uint32_t actor);
    int (*w_001AAD00)(void *ctx);

    /* ---- task chain (design 2.2, 2.6) ---- */
    int (*w_001AD1A0)(void *ctx); /* original returns 0 or 4 */
    int (*w_001AD230)(void *ctx); /* original: 001AF2C0(), return 4 */
    int (*w_001AD4D0)(void *ctx); /* tail-jumps to 0x1AE040 */
    int (*w_001AD740)(void *ctx);
    int (*w_001AED80)(void *ctx, uint8_t a0);
    int (*w_001FF080)(void *ctx, int a0, int a1);
    int (*w_0021B180)(void *ctx);
    int (*w_0021B550)(void *ctx); /* original result; 001ADF50 step 2 tests != 0 */
    int (*w_0021B840)(void *ctx);
    int (*w_001FC9B0)(void *ctx);
    int (*w_001D2880)(void *ctx);
    int (*w_001FA790)(void *ctx, int a0, int a1);
    int (*w_001ABF90)(void *ctx, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);
    int (*w_001AEDE0)(void *ctx, int16_t a0, uint8_t a1);
    int (*w_001FAB50)(void *ctx);
    int (*w_001AB790)(void *ctx, uint32_t fn); /* task replace; 001ADF00 passes 0x1AC070 */
} EmSceneWorkers;

/* Call protocol for the cores. Before calling worker `present` (non-zero
 * when the pointer is set) at original `callee` from original `caller`:
 *   - a latched fault stops everything (returns -1, no trace);
 *   - a NULL table or worker latches EM_SCENE_FAULT_NULL_WORKER at `callee`;
 *   - otherwise the trace hook (if any) sees the call and 0 is returned.
 * After the call, em_scene_worker_leave() latches a negative result as
 * EM_SCENE_FAULT_WORKER_FAILED at `callee` and passes other values through. */
static inline int em_scene_worker_enter(EmSceneState *s, const EmSceneWorkers *w,
                                        uint32_t caller, uint32_t callee, int present,
                                        uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (em_scene_faulted(s))
        return -1;
    if (!w || !present)
        return em_scene_fault(s, callee, EM_SCENE_FAULT_NULL_WORKER);
    if (w->trace)
        w->trace(w->ctx, caller, callee, a0, a1, a2, a3);
    return 0;
}

/* Before using reader or store `present` for original data `address`:
 * -1 when a fault is latched or the entry is NULL (latching
 * EM_SCENE_FAULT_NULL_WORKER at `address`), else 0. Not traced. */
static inline int em_scene_reader_ready(EmSceneState *s, const EmSceneWorkers *w,
                                        uint32_t address, int present)
{
    if (em_scene_faulted(s))
        return -1;
    if (!w || !present)
        return em_scene_fault(s, address, EM_SCENE_FAULT_NULL_WORKER);
    return 0;
}

static inline int em_scene_worker_leave(EmSceneState *s, uint32_t callee, int result)
{
    if (result < 0)
        return em_scene_fault(s, callee, EM_SCENE_FAULT_WORKER_FAILED);
    return result;
}

#ifdef __cplusplus
}
#endif

#endif /* EM_SCENE_WORKERS_H */
