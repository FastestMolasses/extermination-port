/* Scene coordinator bindings, steps S8-S9 (legacy mode). See
 * em_scene_bindings.h and docs/SCENE_COORDINATOR_DESIGN.md sections 3.1,
 * 6 (S8, S9) and 10.1.
 *
 * What is live after S9:
 *   - the slot-0 task is em_scene_task_001ACEC0, which runs the S3 cores
 *     001ACEC0 -> 001AD250 and, through the w_001AD4D0 binding (the original
 *     001AD4D0 is a tail jump to 0x1AE040), the S2 frame core 0x1AE040 with
 *     the lead's Q1 entry point (em_sf_001AE040_q1);
 *   - S9: the state-0 tick returns. 0x1AE040 state 0 ends with
 *     `b .L001AE5CC` (0x1AE0DC, the epilogue), never falling into state 1,
 *     so the tick that rebuilds the area runs no world variant; the first
 *     world frame (001AE5E0/001AE6B0) is the next task tick;
 *   - spad 3B90 (001ACEC0 writes 2 every tick) and C4 are forwarded to the
 *     step-D letterbox gate at the end of every task tick (design 2.1);
 *   - the S6 trace contract (design 10.1) when EM_FRAME_TRACE is set.
 *
 * Worker bindings. Every worker the chain reaches in legacy mode is bound;
 * every other worker is NULL and faults when reached (fail-stop):
 *   w_001AD4D0            -> frame machine entry (legacy Continue hook,
 *                            trace frame state, 0x1AE040)
 *   w_001AE5E0/w_001AE6B0 -> ONE legacy world worker (em_game_legacy_world_frame)
 *   w_001AFCA0            -> spad 31F4 = 0 (001AFCA0 stores it) after the
 *                            port's native state-0 re-arm (em_game_legacy_state0)
 *   w_001AFCF0, w_001AD140, w_001AD010 -> the S3 cores (design 10.1)
 *   r_0028A9A0            -> em_frame_transition()->substate
 *   UNMIRRORED (see s_unmirrored below): the remaining 0x1AE040 state-0 callees
 *   and 001AFCF0's 001FC9B0 have no port code at their original position yet.
 *   Their bindings return without effect and are reported once on stderr, so
 *   a trace that shows the call is never mistaken for the port doing it.
 * Not reached in legacy mode, therefore NULL: the load arms (001AD1A0,
 * 001AD230, 001AD360's and 001ADF50's callees), the game-over arms, the
 * status/unported classifier arms (states 2, 3, 5, 6), state 4, the core
 * variants' stage workers and the 001AFD70 walk. The canonical request block
 * has no port writer until S11b/S12b, so B8/B9 stay 0 and 001AD140/001AD010
 * are never entered; if they were, their 001FC9B0/001FBC50/001FABB0 calls
 * would reach NULL 001FBC50/001FABB0 and fault.
 */
#include "game/em_scene_bindings.h"

#include <stdint.h>
#include <stdio.h>

#include "game/em_frame.h"
#include "game/em_frame_trace.h"
#include "game/em_scene_classify.h"
#include "game/em_scene_frame.h"
#include "game/em_scene_task.h"
#include "game/em_scene_workers.h"

/* ------------------------------------------------------------ storage */

static EmSceneState s_state;
static EmSceneWorkers s_workers;
static int s_ready;

/* Legacy-mode flag (bindings only; see em_scene_bindings.h). */
static const int s_classifier_shadow = 1; /* retired by S11a/S11b */

/* The slot-0 record's user bytes for the tick in progress. */
static uint8_t *s_user;
static int s_fault_reported;
static int s_q1_reported;

EmSceneState *em_scene_state(void)
{
    return &s_state;
}

/* ------------------------------------------------------ unmirrored workers */

/* Original callees the chain reaches in legacy mode with no port code at
 * their original position. Each entry says what the port does instead. */
enum {
    UM_001FC9B0,
    UM_001B07C0,
    UM_001B6990,
    UM_001D19E0,
    UM_001C1DC0,
    UM_00199C50,
    UM_001AEE40,
    UM_001FAE70,
    UM_001C5C50,
    UM_001D1EF0,
    UM_COUNT
};

static const struct {
    uint32_t address;
    const char *port; /* where the port does (or does not do) this today */
} s_unmirrored[UM_COUNT] = {
    [UM_001FC9B0] = {0x001FC9B0u, "001AFCF0 callee; no port counterpart"},
    [UM_001B07C0] = {0x001B07C0u, "spawn placement; the manifest spawn inside the legacy state-0 re-arm"},
    [UM_001B6990] = {0x001B6990u, "roster spawn; pool not live until S10b"},
    [UM_001D19E0] = {0x001D19E0u, "no port counterpart"},
    [UM_001C1DC0] = {0x001C1DC0u, "no port counterpart (weather node spawner, design 10.2 Q4)"},
    [UM_00199C50] = {0x00199C50u, "no port counterpart"},
    [UM_001AEE40] = {0x001AEE40u, "area-entry flash fade(4); not mirrored"},
    [UM_001FAE70] = {0x001FAE70u, "area music cue; not mirrored (game_load_task note)"},
    [UM_001C5C50] = {0x001C5C50u, "area-title actor; legacy em_hud area title"},
    [UM_001D1EF0] = {0x001D1EF0u, "no port counterpart"},
};

static uint32_t s_unmirrored_seen;     /* reached at least once */
static uint32_t s_unmirrored_reported; /* already on stderr */

static int unmirrored(int index)
{
    s_unmirrored_seen |= 1u << index;
    return 0;
}

/* One stderr line per newly reached set, after the tick. */
static void report_unmirrored(void)
{
    uint32_t fresh = s_unmirrored_seen & ~s_unmirrored_reported;
    if (!fresh)
        return;
    fprintf(stderr, "em_scene: legacy mode: reached without port code:");
    for (int i = 0; i < UM_COUNT; ++i)
        if (fresh & (1u << i))
            fprintf(stderr, " %08X (%s);", (unsigned)s_unmirrored[i].address,
                    s_unmirrored[i].port);
    fputc('\n', stderr);
    s_unmirrored_reported |= fresh;
}

static int um_001FC9B0(void *ctx) { (void)ctx; return unmirrored(UM_001FC9B0); }
static int um_001B07C0(void *ctx, int a0) { (void)ctx; (void)a0; return unmirrored(UM_001B07C0); }
static int um_001B6990(void *ctx) { (void)ctx; return unmirrored(UM_001B6990); }
static int um_001D19E0(void *ctx) { (void)ctx; return unmirrored(UM_001D19E0); }
static int um_001C1DC0(void *ctx) { (void)ctx; return unmirrored(UM_001C1DC0); }
static int um_00199C50(void *ctx) { (void)ctx; return unmirrored(UM_00199C50); }
static int um_001AEE40(void *ctx, int16_t a0) { (void)ctx; (void)a0; return unmirrored(UM_001AEE40); }
static int um_001FAE70(void *ctx, int a0) { (void)ctx; (void)a0; return unmirrored(UM_001FAE70); }
static int um_001C5C50(void *ctx) { (void)ctx; return unmirrored(UM_001C5C50); }
static int um_001D1EF0(void *ctx) { (void)ctx; return unmirrored(UM_001D1EF0); }

/* ------------------------------------------------------------ readers */

static int16_t r_0028A9A0(void *ctx)
{
    (void)ctx;
    return em_frame_transition()->substate;
}

/* ------------------------------------------------------------ trace */

/* The classifier result recorded for this tick (design 10.1: once per
 * classifier run). Shadow mode: the canonical state with the input words
 * taken from the step-C pad block in the original layout, through the same
 * Q1 entry the core uses. Recorded only; the core acts on the canonical state. */
static int32_t shadow_classifier(void)
{
    EmSceneState view = s_state;
    if (s_classifier_shadow) {
        const EmPadUnpack *pad = em_frame_pad_block();
        view.d810E74 = pad->pressed;
        view.d810E70 = pad->held;
    }
    return em_scene_classify_q1(&view, em_frame_transition()->substate, NULL);
}

static void bindings_trace(void *ctx, uint32_t caller, uint32_t callee, uint32_t a0,
                           uint32_t a1, uint32_t a2, uint32_t a3)
{
    em_frame_trace_env_hook(ctx, caller, callee, a0, a1, a2, a3);
    if (caller == 0x001AE040u && callee == 0x001AE7E0u) {
        EmFrameTrace *t = em_frame_trace_env();
        if (t)
            em_frame_trace_classifier(t, shadow_classifier());
    }
}

/* -------------------------------------------------- core-backed workers */

static int w_001AFCF0(void *ctx)
{
    (void)ctx;
    return em_sf_001AFCF0(&s_state, &s_workers);
}

static int w_001AD140(void *ctx)
{
    (void)ctx;
    return em_sf_001AD140(&s_state, s_user, &s_workers);
}

static int w_001AD010(void *ctx)
{
    (void)ctx;
    return em_sf_001AD010(&s_state, s_user, &s_workers);
}

/* -------------------------------------------------------- legacy workers */

/* 0x1AE040 state 0, first callee. 001AFCA0 ends with spad 31F4 = 0 (design
 * 2.3); the port's native re-arm stands in for the rest of it and for the
 * unmirrored state-0 callees (spawn, camera init, self-test fixtures). */
static int w_001AFCA0(void *ctx)
{
    (void)ctx;
    em_game_legacy_state0();
    s_state.spad31F4 = 0;
    return 0;
}

/* 0x1AE040 state 1 world frame, both variants. */
static int w_world_legacy(void *ctx)
{
    (void)ctx;
    em_game_legacy_world_frame();
    return 0;
}

static int frame_machine(void)
{
    int select_withheld = 0;
    int rc = em_sf_001AE040_q1(&s_state, s_user, &s_workers, &select_withheld);
    if (select_withheld && !s_q1_reported) {
        s_q1_reported = 1;
        fprintf(stderr, "em_scene: %s\n", EM_SCENE_Q1_UNPORTED_MESSAGE);
    }
    return rc;
}

/* 001AD4D0: the jump into 0x1AE040. */
static int w_001AD4D0(void *ctx)
{
    (void)ctx;
    uint8_t *frame_state = em_scene_task_byte(s_user, EM_SCENE_TASK_0B);
    if (!frame_state)
        return em_scene_fault(&s_state, 0x001AD4D0u, EM_SCENE_FAULT_BAD_INDEX);
    /* Legacy Continue (retired by S11b): the port services the game-over
     * restart here, where its frame machine did, and rebuilds from state 0. */
    if (*frame_state == 1) {
        int restart = em_game_legacy_continue_restart();
        if (restart < 0)
            return 0; /* quit already requested; no frame, as before S8 */
        if (restart > 0)
            *frame_state = 0;
    }
    EmFrameTrace *t = em_frame_trace_env();
    if (t)
        em_frame_trace_frame_state(t, s_user, s_state.spad3B8D,
                                   em_frame_transition()->substate);
    /* One 0x1AE040 run per tick. State 0 returns after its eleven calls
     * (0x1AE0DC b .L001AE5CC), so a rebuild tick draws no world frame. */
    if (frame_machine() < 0)
        return -1;
    /* Shadow mode: nothing may act on the classifier. With no writer of the
     * canonical request block or input words, 001AE7E0 returns 0 and state 1
     * stays 1; anything else is a bindings defect. */
    if (s_classifier_shadow && *frame_state != 1)
        return em_scene_fault(&s_state, 0x001AE7E0u, EM_SCENE_FAULT_BAD_RESULT);
    return 0;
}

/* ------------------------------------------------------------ setup */

static void bindings_init(void)
{
    if (s_ready)
        return;
    s_ready = 1;
    /* D_00810E50 = 4 in every original capture (design 10.2 Q8); written
     * here until S11a's input translation owns it. 001AE7E0 returns 1
     * (unported 0022A650) for any other value. */
    s_state.d810E50 = 4;

    EmSceneWorkers *w = &s_workers;
    w->ctx = NULL;
    w->trace = em_frame_trace_env() ? bindings_trace : NULL;
    w->r_0028A9A0 = r_0028A9A0;

    w->w_001AD4D0 = w_001AD4D0;
    w->w_001AFCA0 = w_001AFCA0;
    w->w_001AFCF0 = w_001AFCF0;
    w->w_001AD140 = w_001AD140;
    w->w_001AD010 = w_001AD010;
    w->w_001AE5E0 = w_world_legacy;
    w->w_001AE6B0 = w_world_legacy;

    w->w_001FC9B0 = um_001FC9B0;
    w->w_001B07C0 = um_001B07C0;
    w->w_001B6990 = um_001B6990;
    w->w_001D19E0 = um_001D19E0;
    w->w_001C1DC0 = um_001C1DC0;
    w->w_00199C50 = um_00199C50;
    w->w_001AEE40 = um_001AEE40;
    w->w_001FAE70 = um_001FAE70;
    w->w_001C5C50 = um_001C5C50;
    w->w_001D1EF0 = um_001D1EF0;
}

void em_scene_bindings_legacy_loaded(EmTask *record)
{
    if (!record)
        return;
    uint8_t *user = record->user;
    *em_scene_task_byte(user, EM_SCENE_TASK_08) = 3;
    *em_scene_task_byte(user, EM_SCENE_TASK_09) = 1;
    *em_scene_task_byte(user, EM_SCENE_TASK_0A) = 0;
    *em_scene_task_byte(user, EM_SCENE_TASK_0B) = 0;
}

/* ------------------------------------------------------------ the task */

void em_scene_task_001ACEC0(void)
{
    bindings_init();
    if (em_scene_faulted(&s_state))
        return; /* fail-stop: the task does nothing after a fault */
    EmTask *self = em_task_current();
    s_user = self ? self->user : NULL;
    EmFrameTrace *t = em_frame_trace_env();
    if (t)
        em_frame_trace_tick_begin(t, em_frame_counter());
    int rc = em_sf_001ACEC0(&s_state, s_user, &s_workers);
    /* Step D of the next main-loop iteration reads 3B90 and C4 (design 2.1). */
    em_frame_screen_fade_gate(s_state.spad3B90, s_state.req[EM_SCENE_REQ_C4]);
    if (t)
        em_frame_trace_tick_end(t);
    report_unmirrored();
    s_user = NULL;
    if (rc < 0 && !s_fault_reported) {
        s_fault_reported = 1;
        fprintf(stderr,
                "em_scene: FAULT at %08X (code %d); the game task is stopped (fail-stop)\n",
                (unsigned)s_state.fault.address, (int)s_state.fault.code);
    }
}
