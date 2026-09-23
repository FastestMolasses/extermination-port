#include "game/em_scene_frame.h"

#include <stddef.h>

#include "game/em_scene_classify.h"
#include "game/em_status_frame.h"

/* Original addresses used as fault/trace identities. */
enum {
    SF_001AE040 = 0x001AE040u,
    SF_001AE5E0 = 0x001AE5E0u,
    SF_001AE6B0 = 0x001AE6B0u,
    SF_001AE7E0 = 0x001AE7E0u,
    SF_D_0028A9A0 = 0x0028A9A0u,
    SF_D_00282157 = 0x00282157u,
    SF_D_00275B44 = 0x00275B44u,
    SF_D_008102B9 = 0x008102B9u
};

#define TB(off) user[(off) - EM_SCENE_TASK_FIRST]
#define REQ(name) s->req[EM_SCENE_REQ_##name]

/* One worker call: fault/NULL check and trace (em_scene_worker_enter), the
 * call, then the result check (em_scene_worker_leave). The enclosing function
 * returns -1 on a fault. `t0..t3` are the traced a0..a3. */
#define SF_ENTER(caller, callee, fn, t0, t1, t2, t3)                                            \
    (em_scene_worker_enter(s, w, (caller), (callee), w != NULL && w->fn != NULL, (uint32_t)(t0), \
                           (uint32_t)(t1), (uint32_t)(t2), (uint32_t)(t3)) < 0)

#define SF_CALL0(caller, callee, fn)                                           \
    do {                                                                       \
        if (SF_ENTER(caller, callee, fn, 0, 0, 0, 0))                          \
            return -1;                                                         \
        if (em_scene_worker_leave(s, (callee), w->fn(w->ctx)) < 0)             \
            return -1;                                                         \
    } while (0)

#define SF_CALL(caller, callee, fn, t0, t1, t2, t3, ...)                       \
    do {                                                                       \
        if (SF_ENTER(caller, callee, fn, t0, t1, t2, t3))                      \
            return -1;                                                         \
        if (em_scene_worker_leave(s, (callee), w->fn(w->ctx, __VA_ARGS__)) < 0) \
            return -1;                                                         \
    } while (0)

/* A call that traces a0 but whose callee declares no parameter. */
#define SF_CALLV(caller, callee, fn, t0)                                       \
    do {                                                                       \
        if (SF_ENTER(caller, callee, fn, t0, 0, 0, 0))                         \
            return -1;                                                         \
        if (em_scene_worker_leave(s, (callee), w->fn(w->ctx)) < 0)             \
            return -1;                                                         \
    } while (0)

/* Calls whose original result the core tests. */
#define SF_CALLR0(out, caller, callee, fn)                                     \
    do {                                                                       \
        if (SF_ENTER(caller, callee, fn, 0, 0, 0, 0))                          \
            return -1;                                                         \
        (out) = em_scene_worker_leave(s, (callee), w->fn(w->ctx));             \
        if ((out) < 0)                                                         \
            return -1;                                                         \
    } while (0)

/* ------------------------------------------------------------- readers */

static int sf_fade(EmSceneState *s, const EmSceneWorkers *w, int16_t *out)
{
    if (em_scene_reader_ready(s, w, SF_D_0028A9A0, w != NULL && w->r_0028A9A0 != NULL) < 0)
        return -1;
    *out = w->r_0028A9A0(w->ctx);
    return 0;
}

static int sf_audio_busy(EmSceneState *s, const EmSceneWorkers *w, uint8_t *out)
{
    if (em_scene_reader_ready(s, w, SF_D_00282157, w != NULL && w->r_00282157 != NULL) < 0)
        return -1;
    *out = w->r_00282157(w->ctx);
    return 0;
}

static int sf_current_actor(EmSceneState *s, const EmSceneWorkers *w, uint32_t *out)
{
    if (em_scene_reader_ready(s, w, SF_D_00275B44, w != NULL && w->r_00275B44 != NULL) < 0)
        return -1;
    *out = w->r_00275B44(w->ctx);
    return 0;
}

static int sf_player_byte(EmSceneState *s, const EmSceneWorkers *w, uint8_t *out)
{
    if (em_scene_reader_ready(s, w, SF_D_008102B9, w != NULL && w->r_008102B9 != NULL) < 0)
        return -1;
    *out = w->r_008102B9(w->ctx);
    return 0;
}

/* --------------------------------------------- world-frame variants */

/* 001AE5E0 (NEARMISS C + .s): counters, then 14 calls in this order. */
int em_sf_001AE5E0(EmSceneState *s, const EmSceneWorkers *w)
{
    if (!s || em_scene_faulted(s))
        return -1;
    uint8_t b9;
    uint32_t actor;
    /* 0x1AE5EC..0x1AE624: n = D_00810750 and D_008102B9 are loaded, then
     * D_00810750 = n + 1 and spad 3B68 += 1 are stored before the first jal. */
    int32_t n = s->d810750;
    if (sf_player_byte(s, w, &b9) < 0)
        return -1;
    s->d810750 = (int32_t)((uint32_t)n + 1u);
    s->spad3B68 = (int32_t)((uint32_t)s->spad3B68 + 1u);
    /* 0x1AE620: 001CB590(&D_008102B0, 0x320, D_008102B9, n). */
    SF_CALL(SF_001AE5E0, 0x1CB590u, w_001CB590, EM_SCENE_D_008102B0, 0x320, b9, (uint32_t)n,
            EM_SCENE_D_008102B0, 0x320, b9, n);
    /* 0x1AE628: 0015BCF0(D_00275B44), loaded after 001CB590 returned. */
    if (sf_current_actor(s, w, &actor) < 0)
        return -1;
    SF_CALL(SF_001AE5E0, 0x15BCF0u, w_0015BCF0, actor, 0, 0, 0, actor);
    SF_CALL0(SF_001AE5E0, 0x1CB5A0u, w_001CB5A0);
    SF_CALL0(SF_001AE5E0, 0x1D1C50u, w_001D1C50);
    SF_CALL(SF_001AE5E0, 0x1C1D00u, w_001C1D00, EM_SCENE_D_008101D0, 0, 0, 0, EM_SCENE_D_008101D0);
    SF_CALL(SF_001AE5E0, 0x1AFD70u, walk_001AFD70, 0, 0, 0, 0, 0);
    SF_CALL0(SF_001AE5E0, 0x15C160u, w_0015C160);
    SF_CALL0(SF_001AE5E0, 0x1F0360u, w_001F0360);
    /* 0x1AE670: 001CB590(&D_008101E0, 0xD0, 0); a3 is not set up. */
    SF_CALL(SF_001AE5E0, 0x1CB590u, w_001CB590, EM_SCENE_D_008101E0, 0xD0, 0, 0,
            EM_SCENE_D_008101E0, 0xD0, 0, 0);
    if (sf_current_actor(s, w, &actor) < 0)
        return -1;
    SF_CALL(SF_001AE5E0, 0x18B9C0u, w_0018B9C0, actor, 0, 0, 0, actor);
    SF_CALL0(SF_001AE5E0, 0x1CB5A0u, w_001CB5A0);
    SF_CALL0(SF_001AE5E0, 0x1AAD00u, w_001AAD00);
    SF_CALL(SF_001AE5E0, 0x1D1EA0u, w_001D1EA0, 1, 0, 0, 0, 1);
    return 0;
}

/* 001AE6B0 (NEARMISS C + .s): scratchpad bookkeeping, counters, 14 calls. */
int em_sf_001AE6B0(EmSceneState *s, const EmSceneWorkers *w)
{
    if (!s || em_scene_faulted(s))
        return -1;
    uint8_t b9;
    uint32_t actor;
    /* 0x1AE6BC: 3B92 != 0 -> 3B84 (u16) += 1. */
    if (s->spad3B92 != 0)
        s->spad3B84 = (uint16_t)(s->spad3B84 + 1u);
    /* 0x1AE6E0: 3B91 == 1 && D_0028A9A0 == 0 && (E74 & 0x900) -> 3B91 = 2.
     * D_0028A9A0 is loaded only when 3B91 == 1. */
    if (s->spad3B91 == 1) {
        int16_t fade;
        if (sf_fade(s, w, &fade) < 0)
            return -1;
        if (fade == 0 && (s->d810E74 & 0x900) != 0)
            s->spad3B91 = 2;
    }
    /* 0x1AE720..0x1AE748: D_00810750 += 1, spad 3B68 += 1. */
    s->d810750 = (int32_t)((uint32_t)s->d810750 + 1u);
    s->spad3B68 = (int32_t)((uint32_t)s->spad3B68 + 1u);
    SF_CALL0(SF_001AE6B0, 0x1D1C50u, w_001D1C50);
    SF_CALL(SF_001AE6B0, 0x1C1D00u, w_001C1D00, EM_SCENE_D_008101D0, 0, 0, 0, EM_SCENE_D_008101D0);
    SF_CALL(SF_001AE6B0, 0x1AFD70u, walk_001AFD70, 1, 0, 0, 0, 1);
    SF_CALL0(SF_001AE6B0, 0x1F0360u, w_001F0360);
    /* 0x1AE768: D_008102B9 is loaded here, after 001F0360; a3 is not set up. */
    if (sf_player_byte(s, w, &b9) < 0)
        return -1;
    SF_CALL(SF_001AE6B0, 0x1CB590u, w_001CB590, EM_SCENE_D_008102B0, 0x320, b9, 0,
            EM_SCENE_D_008102B0, 0x320, b9, 0);
    if (sf_current_actor(s, w, &actor) < 0)
        return -1;
    SF_CALL(SF_001AE6B0, 0x15BCF0u, w_0015BCF0, actor, 0, 0, 0, actor);
    SF_CALL0(SF_001AE6B0, 0x1CB5A0u, w_001CB5A0);
    SF_CALL(SF_001AE6B0, 0x1AFD70u, walk_001AFD70, 2, 0, 0, 0, 2);
    SF_CALL0(SF_001AE6B0, 0x15C160u, w_0015C160);
    SF_CALL(SF_001AE6B0, 0x1CB590u, w_001CB590, EM_SCENE_D_008101E0, 0xD0, 0, 0,
            EM_SCENE_D_008101E0, 0xD0, 0, 0);
    if (sf_current_actor(s, w, &actor) < 0)
        return -1;
    SF_CALL(SF_001AE6B0, 0x18B9C0u, w_0018B9C0, actor, 0, 0, 0, actor);
    SF_CALL0(SF_001AE6B0, 0x1CB5A0u, w_001CB5A0);
    SF_CALL0(SF_001AE6B0, 0x1AAD00u, w_001AAD00);
    SF_CALL(SF_001AE6B0, 0x1D1EA0u, w_001D1EA0, 1, 0, 0, 0, 1);
    return 0;
}

/* ------------------------------------ states 3/5: em_status_frame view */

typedef struct {
    EmSceneState *s;
    uint8_t *user;
    const EmSceneWorkers *w;
    EmStatusFrame *view;
} SfStatus;

/* view -> canonical (+B, +C, +0x11, C4, EF). D_00282157 is not owned by the
 * coordinator and 0x1AE040 never stores it, so audio_busy is not published. */
static void sf_status_publish(const SfStatus *c)
{
    EmSceneState *s = c->s;
    uint8_t *user = c->user;
    TB(0x0B) = c->view->phase;
    TB(0x0C) = c->view->step;
    TB(0x11) = c->view->task_flag11;
    REQ(C4) = c->view->control_mode;
    REQ(EF) = c->view->recovery_lock;
}

/* canonical -> view, after every worker call. */
static void sf_status_refresh(const SfStatus *c)
{
    EmSceneState *s = c->s;
    uint8_t *user = c->user;
    c->view->phase = TB(0x0B);
    c->view->step = TB(0x0C);
    c->view->task_flag11 = TB(0x11);
    c->view->control_mode = REQ(C4);
    c->view->recovery_lock = REQ(EF);
}

static int sf_status_call(SfStatus *c, EmStatusFrameEvent event)
{
    EmSceneState *s = c->s;
    const EmSceneWorkers *w = c->w;
    /* Event -> original call (em_status_frame.h names each one). */
    switch (event) {
    case EM_STATUS_RESET_UI:
        SF_CALL0(SF_001AE040, 0x20E060u, w_0020E060);
        return 0;
    case EM_STATUS_RESET_SOUNDS:
        SF_CALL0(SF_001AE040, 0x1FBC50u, w_001FBC50);
        return 0;
    case EM_STATUS_STOP_STREAMS:
        SF_CALL0(SF_001AE040, 0x1FABB0u, w_001FABB0);
        return 0;
    case EM_STATUS_CHANNEL_ZERO:
        SF_CALL(SF_001AE040, 0x119828u, w_00119828, 0, 0x3FFF, 0x3FFF, 0, 0, 0x3FFF, 0x3FFF);
        return 0;
    case EM_STATUS_CHANNEL_ONE:
        SF_CALL(SF_001AE040, 0x119828u, w_00119828, 1, 0x3FFF, 0x3FFF, 0, 1, 0x3FFF, 0x3FFF);
        return 0;
    case EM_STATUS_BEGIN_FRAME:
        SF_CALL0(SF_001AE040, 0x1D1C50u, w_001D1C50);
        return 0;
    case EM_STATUS_DRAW_CONTEXT:
        SF_CALL(SF_001AE040, 0x1D2830u, w_001D2830, 3, 1, 0, 0, 3, 1);
        return 0;
    case EM_STATUS_MODE_ZERO:
        /* 001E0CC0 declares no parameter; the call site sets a0 = 0. */
        SF_CALLV(SF_001AE040, 0x1E0CC0u, w_001E0CC0, 0);
        return 0;
    case EM_STATUS_BLACK_HOLD:
        SF_CALL(SF_001AE040, 0x1AEDB0u, w_001AEDB0, 0, 0, 0, 0, 0);
        return 0;
    case EM_STATUS_END_FRAME:
        SF_CALL(SF_001AE040, 0x1D1EA0u, w_001D1EA0, 0, 0, 0, 0, 0);
        return 0;
    case EM_STATUS_RESET_FRAME:
        SF_CALL0(SF_001AE040, 0x1D1EF0u, w_001D1EF0);
        return 0;
    case EM_STATUS_CAMERA_COMMIT:
        SF_CALL(SF_001AE040, 0x18C0D0u, w_0018C0D0, EM_SCENE_D_008101E0, 1, 0, 0,
                EM_SCENE_D_008101E0, 1);
        return 0;
    case EM_STATUS_RESUME_MUSIC:
        SF_CALL(SF_001AE040, 0x1FAE70u, w_001FAE70, 1, 0, 0, 0, 1);
        return 0;
    case EM_STATUS_FLASH:
        SF_CALL(SF_001AE040, 0x1AEE40u, w_001AEE40, 0x20, 0, 0, 0, 0x20);
        return 0;
    }
    return em_scene_fault(s, SF_001AE040, EM_SCENE_FAULT_BAD_RESULT);
}

static int sf_status_emit(void *context, EmStatusFrameEvent event)
{
    SfStatus *c = context;
    sf_status_publish(c);
    int result = sf_status_call(c, event);
    sf_status_refresh(c);
    return result < 0 ? 0 : 1; /* em_status_frame: 1 = side effect done */
}

/* 0020CDC0. 0x1AE040 tests the result only against zero (beqz v0). */
static int sf_status_page(void *context)
{
    SfStatus *c = context;
    EmSceneState *s = c->s;
    const EmSceneWorkers *w = c->w;
    int result = 0;
    sf_status_publish(c);
    if (SF_ENTER(SF_001AE040, 0x20CDC0u, w_0020CDC0, 0, 0, 0, 0)) {
        result = -1;
    } else {
        result = em_scene_worker_leave(s, 0x20CDC0u, w->w_0020CDC0(w->ctx));
        if (result > 0)
            result = 1;
    }
    sf_status_refresh(c);
    return result;
}

/* enter != 0: the classifier r == 2 arm (em_status_frame_enter);
 * otherwise states 3 and 5 (em_status_frame_tick). */
static int sf_status(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w, int enter)
{
    EmStatusFrame view = {0};
    SfStatus c = {s, user, w, &view};
    sf_status_refresh(&c);
    /* D_00282157 is loaded only by state 3 sub-step 0. */
    if (!enter && TB(0x0B) == 3 && TB(0x0C) == 0 && sf_audio_busy(s, w, &view.audio_busy) < 0)
        return -1;
    int result = enter ? em_status_frame_enter(&view, sf_status_emit, &c)
                       : em_status_frame_tick(&view, sf_status_emit, sf_status_page, &c);
    sf_status_publish(&c);
    if (result < 0)
        return em_scene_faulted(s) ? -1 : em_scene_fault(s, SF_001AE040, EM_SCENE_FAULT_BAD_RESULT);
    return 0;
}

/* ------------------------------------------------------ 0x1AE040 */

/* State 1 (also the tail of state 4, which falls through). */
static int sf_state1(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w, int q1,
                     int *select_withheld)
{
    int16_t fade;
    if (sf_fade(s, w, &fade) < 0)
        return -1;
    /* 0x1AE154: jal 001AE7E0 (traced; the classifier is not a worker). */
    if (em_scene_worker_enter(s, w, SF_001AE040, SF_001AE7E0, 1, 0, 0, 0, 0) < 0)
        return -1;
    int r = q1 ? em_scene_classify_q1(s, fade, select_withheld) : em_sf_001AE7E0(s, fade);
    if (r == 1) {
        /* 0x1AE15C..0x1AE1C8 (001FBC50 at 0x1AE170, C4 in its delay slot). */
        REQ(C4) = 2;
        SF_CALL0(SF_001AE040, 0x1FBC50u, w_001FBC50);
        SF_CALL(SF_001AE040, 0x1FB9F0u, w_001FB9F0, 0xC, 0x1000, 0x1000, 0x1000, 0xC, 0x1000,
                0x1000, 0x1000);
        TB(0x0B) = (uint8_t)(TB(0x0B) + 1u);
        TB(0x11) = 0;
        TB(0x0C) = 0;
        TB(0x0D) = 0;
    } else if (r == 2) {
        /* 0x1AE1CC..0x1AE240: 0020E060, C4 = 1, +B = 3, +11 = +C = 0,
         * 001FBC50, 001FABB0, 00119828(0/1, 0x3FFF, 0x3FFF). */
        return sf_status(s, user, w, 1);
    } else if (r == 3) {
        /* 0x1AE248..0x1AE280 (001FBC50, 001FABB0, 001AEDB0 at 0x1AE268..0x1AE278). */
        TB(0x0B) = 6;
        TB(0x0C) = 0;
        SF_CALL0(SF_001AE040, 0x1FBC50u, w_001FBC50);
        SF_CALL0(SF_001AE040, 0x1FABB0u, w_001FABB0);
        SF_CALL(SF_001AE040, 0x1AEDB0u, w_001AEDB0, 0, 0, 0, 0, 0);
    } else if (s->d275BD8 == 0) {
        /* 0x1AE284..0x1AE31C: the world frame (0x1AE2A4 / 0x1AE2B4), then the request consumers at
         * D_0028A9A0 == 2 (re-read after the variant; a0 = that halfword). */
        if (s->spad3B8D == 0)
            SF_CALL0(SF_001AE040, SF_001AE5E0, w_001AE5E0);
        else
            SF_CALL0(SF_001AE040, SF_001AE6B0, w_001AE6B0);
        if (REQ(B9) != 0) {
            if (sf_fade(s, w, &fade) < 0)
                return -1;
            if (fade == 2)
                SF_CALLV(SF_001AE040, 0x1AD140u, w_001AD140, fade);
        } else if (REQ(B8) != 0) {
            if (sf_fade(s, w, &fade) < 0)
                return -1;
            if (fade == 2)
                SF_CALLV(SF_001AE040, 0x1AD010u, w_001AD010, fade);
        }
    }
    return 0;
}

static int sf_001AE040(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w, int q1,
                       int *select_withheld)
{
    if (select_withheld)
        *select_withheld = 0;
    if (!s || em_scene_faulted(s))
        return -1;
    if (!user)
        return em_scene_fault(s, SF_001AE040, EM_SCENE_FAULT_BAD_INDEX);
    switch (TB(0x0B)) {
    case 0:
        /* 0x1AE078..0x1AE0D4: +B += 1 (stored in 001AFCA0's delay slot) before the
         * bring-up chain; no world frame this tick. */
        TB(0x0B) = (uint8_t)(TB(0x0B) + 1u);
        SF_CALL0(SF_001AE040, 0x1AFCA0u, w_001AFCA0);
        SF_CALL0(SF_001AE040, 0x1AFCF0u, w_001AFCF0);
        SF_CALL(SF_001AE040, 0x1B07C0u, w_001B07C0, 0, 0, 0, 0, 0);
        SF_CALL0(SF_001AE040, 0x1B6990u, w_001B6990);
        SF_CALL0(SF_001AE040, 0x1D19E0u, w_001D19E0);
        SF_CALL0(SF_001AE040, 0x1C1DC0u, w_001C1DC0);
        SF_CALL0(SF_001AE040, 0x199C50u, w_00199C50);
        SF_CALL(SF_001AE040, 0x1AEE40u, w_001AEE40, 4, 0, 0, 0, 4);
        SF_CALL(SF_001AE040, 0x1FAE70u, w_001FAE70, 1, 0, 0, 0, 1);
        SF_CALL0(SF_001AE040, 0x1C5C50u, w_001C5C50);
        SF_CALL0(SF_001AE040, 0x1D1EF0u, w_001D1EF0);
        return 0;
    case 4:
        /* 0x1AE0E4..0x1AE14C, then falls into state 1 (0x1AE154) in the same tick. */
        SF_CALL0(SF_001AE040, 0x1AFCF0u, w_001AFCF0);
        SF_CALL0(SF_001AE040, 0x18AB00u, w_0018AB00);
        SF_CALL(SF_001AE040, 0x1B07C0u, w_001B07C0, 1, 0, 0, 0, 1);
        SF_CALL0(SF_001AE040, 0x1C1DC0u, w_001C1DC0);
        s->d8101E4 = 0;
        TB(0x0B) = 1; /* stored in 0018D7B0's delay slot, before the call */
        SF_CALL(SF_001AE040, 0x18D7B0u, w_0018D7B0, EM_SCENE_D_008101E0, 1, 0, 0,
                EM_SCENE_D_008101E0, 1);
        SF_CALL(SF_001AE040, 0x18C0D0u, w_0018C0D0, EM_SCENE_D_008101E0, 1, 0, 0,
                EM_SCENE_D_008101E0, 1);
        SF_CALL(SF_001AE040, 0x1AEE10u, w_001AEE10, 4, 0, 0, 0, 4, 0);
        SF_CALL(SF_001AE040, 0x1FAE70u, w_001FAE70, 0, 0, 0, 0, 0);
        SF_CALL0(SF_001AE040, 0x1C5C50u, w_001C5C50);
        return sf_state1(s, user, w, q1, select_withheld);
    case 1:
        return sf_state1(s, user, w, q1, select_withheld);
    case 2: {
        /* 0x1AE324..0x1AE410: acts on 0022A650 == 1, 2, 3 only. */
        int r;
        SF_CALLR0(r, SF_001AE040, 0x22A650u, w_0022A650);
        if (r == 1) {
            SF_CALL0(SF_001AE040, 0x1AF1C0u, w_001AF1C0);
            REQ(C4) = 0;
            TB(0x0B) = (uint8_t)(TB(0x0B) - 1u); /* in 001FB9F0's delay slot */
            SF_CALL(SF_001AE040, 0x1FB9F0u, w_001FB9F0, 0xD, 0x1000, 0x1000, 0x1000, 0xD, 0x1000,
                    0x1000, 0x1000);
            SF_CALL(SF_001AE040, 0x1FAE70u, w_001FAE70, 0, 0, 0, 0, 0);
        } else if (r == 2) {
            SF_CALL(SF_001AE040, 0x1AEBA0u, w_001AEBA0, 0xFF, 0, 0, 0, 0xFF);
            SF_CALL0(SF_001AE040, 0x1AF150u, w_001AF150);
            /* f12 = 0.0f (mtc1 zero); traced as its IEEE-754 bits. */
            SF_CALL(SF_001AE040, 0x1D2610u, w_001D2610, 0, 0, 0, 0, 0.0f);
            s->d275BE0 = 1;
            TB(0x08) = 3;
            TB(0x09) = 5;
            TB(0x0A) = 0;
            TB(0x0B) = 0;
            TB(0x0C) = 0;
            TB(0x0D) = 0;
        } else if (r == 3) {
            /* a0 is 0022A650's leftover here: traced as 0. */
            SF_CALLV(SF_001AE040, 0x1AD140u, w_001AD140, 0);
        }
        return 0;
    }
    case 3:
    case 5:
        return sf_status(s, user, w, 0);
    case 6:
        /* 0x1AE51C..0x1AE5C0 (001FF030 0x1AE564, 001FEFE0 0x1AE578). */
        if (TB(0x0C) == 0) {
            uint8_t busy;
            if (sf_audio_busy(s, w, &busy) < 0)
                return -1;
            if (busy == 0) {
                if (REQ(CE) == 2)
                    SF_CALL(SF_001AE040, 0x1FF030u, w_001FF030, REQ(CF), 0, 0, 0, REQ(CF));
                else
                    SF_CALL(SF_001AE040, 0x1FEFE0u, w_001FEFE0, REQ(CF), 0, 0, 0, REQ(CF));
                TB(0x0C) = (uint8_t)(TB(0x0C) + 1u);
            }
        } else if (TB(0x0C) == 1) {
            if (s->d275BD8 == 0) {
                REQ(CE) = 0;
                TB(0x0B) = 1;
                TB(0x0C) = 0;
                SF_CALL0(SF_001AE040, 0x1C1DC0u, w_001C1DC0);
                SF_CALL(SF_001AE040, 0x1FAE70u, w_001FAE70, 1, 0, 0, 0, 1);
            }
        }
        return 0;
    default:
        /* 0x1AE054: +B >= 7 fails the sltiu range guard; nothing happens. */
        return 0;
    }
}

int em_sf_001AE040(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w)
{
    return sf_001AE040(s, user, w, 0, NULL);
}

int em_sf_001AE040_q1(EmSceneState *s, uint8_t *user, const EmSceneWorkers *w,
                      int *select_withheld)
{
    return sf_001AE040(s, user, w, 1, select_withheld);
}
