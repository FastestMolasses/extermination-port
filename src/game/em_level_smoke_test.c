/* The live first-level smoke (SCENE_COORDINATOR_DESIGN.md step S13). See
 * em_level_smoke_test.h and docs/LEVEL_SMOKE.md.
 *
 * The phases follow the original route (docs/FIRST_LEVEL_ROUTE.md section
 * 3, main line 01 -> 14). A phase is LIVE when this file has a runner for it;
 * a runner is added in the step that makes the phase's original owners live
 * in the port (the `lands` column), never before. Until then the phase
 * reports NOT-LIVE, naming the original owner, the binding the port runs
 * for it today and the step it waits on; the run stops there, because every
 * later beat starts from the state the earlier ones leave. A NOT-LIVE phase a
 * later live phase needs the state of is "driven" (Phase.driven): its runner
 * plays it through the owner's current port binding and it is reported
 * NOT-LIVE driven, never passed (the battery pickup, WP-6).
 *
 * Each runner drives pad input only (as the route captures do) and asserts
 * the original values it can observe in process. The tick-by-tick comparison
 * against the original captures reads the scene tick log
 * (EM_AREA_CHANGE_LOG) in tools/test_level_smoke.py; the runner prints the
 * join key it needs (D_00810750 at first control). */
#include "game/em_level_smoke_test.h"
#include "em_input.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_frame.h"
#include "game/em_game.h"
#include "game/em_game_internal.h"
#include "game/em_opening_runtime.h"
#include "game/em_pickup.h"
#include "game/em_scene_bindings.h"
#include "game/em_scene_state.h"
#include "game/em_task.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *beat;     /* route capture (../Extermination/build/s87/route/<beat>) */
    uint32_t owner;       /* original owner callback (0: the player's own states) */
    const char *original; /* original owners and scripts (FIRST_LEVEL_ROUTE.md section 3) */
    const char *lands;    /* the step that makes the phase live */
    void (*begin)(void);  /* NULL: NOT-LIVE */
    int (*frame)(void);   /* after each frame: 0 continue, 1 passed (failures call fail()) */
    /* 1: a NOT-LIVE phase a later live phase needs the state of (the
     * battery for the panel): the runner drives it through the owner's
     * current port binding and reports it NOT-LIVE ("driven"), never PASS;
     * the later phases' capture checks compare only their own windows. */
    int driven;
} Phase;

static void first_control_begin(void);
static int first_control_frame(void);
static void status_begin(void);
static int status_frame(void);
static void battery_begin(void);
static int battery_frame(void);
static void refusal_begin(void);
static int refusal_frame(void);
static void panel_begin(void);
static int panel_frame(void);
static void elevator_begin(void);
static int elevator_frame(void);

static const Phase k_phases[] = {
    {"first_control", "01_battery (row f0 = slot 04)", 0,
     "0x1AE040 state 1 with 001AE5E0; the AREA11 pool of 49 nodes (ORIGINAL_FRAME_ORDER.md 2, 4)",
     "S12a", first_control_begin, first_control_frame, 0},
    {"status", "01_battery (status exit); frame_trace2/status_04.json", 0,
     "001AE7E0 r==2 -> state 3 (0020E060, 0020CDC0) -> state 5 -> state 1 (ORIGINAL_FRAME_ORDER.md Q7)",
     "S11b", status_begin, status_frame, 0},
    {"battery", "01_battery", 0x00219550u,
     "pickup 00219550 g0.0 (item 0x1B): take script 0x266620, B0=1/B1=0x1B, status ITEM page",
     "WP-6 (pickup owner and Use arbiter) with WP-5 (status ITEM page)", battery_begin, battery_frame,
     1},
    {"elevator_refusal", "02_elevator_refusal", 0x00827B10u,
     "terminal 0x827B10 (r19): refusal script 0x82A990, message 0x8000001A, letterbox",
     "WP-4", refusal_begin, refusal_frame, 0},
    {"panel", "03_panel_power", 0x00159210u,
     "panel 00159210 (r18): script 0x2477A0, 00157F60 B0=1/B1=0x82 (BATTERY page), discharge, "
     "script 0x247BE0, power bit 0x80",
     "WP-4", panel_begin, panel_frame, 0},
    {"elevator", "04_elevator_ride", 0x00827B10u,
     "terminal 0x827B10: powered script 0x82A750, clip 0x47, carry 0x828050 down to y 190", "WP-4",
     elevator_begin, elevator_frame, 0},
    {"boxes", "05_boxes", 0x001551B0u, "ledge climb (state 2, +1F0 8) onto crates r4 and r3 (001551B0)",
     "the ledge climb wired to the player (em_player_climb; WP-15) with WP-18 (crates)", NULL, NULL, 0},
    {"slide", "06_hill_slide", 0, "slope slide 0016C6A0 (state 0x1C, +1F0 0x30)",
     "the slope slide wired to the player (em_player_slide; WP-15)", NULL, NULL, 0},
    {"truck_preview", "07_truck_preview", 0x008251E0u,
     "trigger 0x8251E0 (r17): camera script 0x8292C0, letterbox, D_00810792=1",
     "WP-12 (trigger) with the WP-10 script host", NULL, NULL, 0},
    {"truck_crossing", "08_truck_crossing", 0x00823FF0u,
     "truck 0x823FF0 (r16): stand-on arm, shake, fall, D_00810792=0xFF", "WP-12", NULL, NULL, 0},
    {"cage_roof", "10_cage_roof_roger", 0x008253F0u,
     "ladder column x 360; director 0x8253F0 beat 0 script 0x8294C0; Roger 0x8237E0 script 0x828990",
     "the ladder (WP-15), WP-10 (director) and WP-9 (Roger)", NULL, NULL, 0},
    {"crevice_prompt", "11_crevice_prompt", 0x008253F0u,
     "tank ledge climb, pipes; director beat 1 script 0x829A40 (line 0x97)",
     "WP-10 with WP-8 and the ledge climb (WP-15)", NULL, NULL, 0},
    {"crevice_jump", "12_crevice_jump", 0, "running jump (+1F0 0x0C, state 6) onto the north block",
     "the running jump (no port module; WP-15)", NULL, NULL, 0},
    {"east_tower", "13_east_tower", 0x008253F0u,
     "high ledge climb; director beat 2 script 0x829CC0 (line 0x99)", "WP-10 and the ledge climb (WP-15)",
     NULL, NULL, 0},
    {"roger", "14_roger_encounter", 0x008237E0u,
     "running jump; Roger 0x8237E0 quad 0x82AB80, script 0x8283D0 (bank 96), 0x8107D8=1",
     "WP-9 and the running jump (WP-15)", NULL, NULL, 0},
};
enum { PHASE_COUNT = (int)(sizeof k_phases / sizeof k_phases[0]) };

/* The first-control census (ORIGINAL_FRAME_ORDER.md section 4: nodes #0..#48,
 * record 13 freed on the second world frame, Q3). */
enum { FIRST_CONTROL_CENSUS = 49 };
/* Test timeouts (not game behaviour). */
enum { FIRST_CONTROL_TIMEOUT = 3000, STATUS_HOLD_FRAMES = 30, STATUS_CLOSE_TIMEOUT = 120 };

static struct {
    int active, failed, until, current, passed;
    int last_live;    /* index of the last phase that passed (-1: none) */
    int stop_pending; /* the run passed; quit after one more frame (finish) */
    /* status */
    int step, frames, close_frames, resumed;
    float frozen_pos[3], frozen_eye[3];
    int32_t frozen_variants;
    /* the pad-input navigation of the route phases (nav_*) */
    int pad_on;
    EmPadState pad;
    int nav_frames, nav_hist_n;
    float nav_hist[64][2];
    int32_t scan_variants; /* D_00810750 at the Use scan's tick (scan_accepted) */
    uint8_t saw[8];        /* per-phase observations (see each runner) */
} t;

static void fail(const char *reason)
{
    fprintf(stderr, "level smoke: FAIL phase=%s frame=%d: %s\n",
            t.current < PHASE_COUNT ? k_phases[t.current].name : "-", g.frame_no, reason);
    t.failed = 1;
    em_frame_request_quit();
}

static void pad_key(int key, int down)
{
    EmEvent event = {0};
    event.type = down ? EM_EVENT_KEY_DOWN : EM_EVENT_KEY_UP;
    event.key = key;
    em_input_handle_event(&event);
}

static uint8_t task_byte(unsigned offset)
{
    EmTask *task = em_task_current(); /* the hook runs inside the slot-0 task */
    const uint8_t *b = task ? em_scene_task_byte(task->user, offset) : NULL;
    return b ? *b : 0xFF;
}

static void report_not_live(int from)
{
    for (int i = from; i <= t.until; ++i) {
        const Phase *p = &k_phases[i];
        const char *binding = p->owner ? em_scene_bindings_pool_binding(p->owner) : NULL;
        char owner[160] = "";
        if (p->owner)
            snprintf(owner, sizeof owner, "; port binding of %08X: %s", (unsigned)p->owner,
                     binding ? binding : "no live node");
        fprintf(stderr, "level smoke: %s: NOT-LIVE (route beat %s; original: %s%s; lands with %s)%s\n",
                p->name, p->beat, p->original, owner, p->lands,
                i > from && p->begin ? " [runner present, not reached]" : "");
    }
}

static void finish(void)
{
    fprintf(stderr, "level smoke: PASS %d live phase%s through %s", t.passed, t.passed == 1 ? "" : "s",
            k_phases[t.last_live].name);
    if (t.current <= t.until)
        fprintf(stderr, "; NOT-LIVE from %s through %s (not verified)\n", k_phases[t.current].name,
                k_phases[t.until].name);
    else
        fputc('\n', stderr);
    /* Hand the pad back to the keyboard map (the navigation below drives
     * it through the gamepad overlay, em_input_set_gamepad). */
    if (t.pad_on)
        em_input_set_gamepad(NULL);
    t.pad_on = 0;
    /* Quit after one more frame, not now. The route captures sample after
     * the original frame, and the original's 0x28A9A0 fade ticks after the
     * slot-0 task, so the port's post-frame fade is the next tick's start
     * sample in the scene tick log (em_scene_bindings.c log_tick_begin);
     * tools/test_level_smoke.py needs that tick after the last live phase.
     * The extra frame takes no input (test driver, not game behaviour). */
    t.stop_pending = 1;
}

/* Advance to the next phase, or stop at the first NOT-LIVE one. A driven
 * phase (Phase.driven) is reported NOT-LIVE, never counted as passed. */
static void next_phase(void)
{
    const Phase *done = &k_phases[t.current];
    if (done->driven) {
        const char *binding = done->owner ? em_scene_bindings_pool_binding(done->owner) : NULL;
        fprintf(stderr, "level smoke: %s: NOT-LIVE driven (route beat %s; original: %s; port binding of "
                "%08X: %s; lands with %s): driven through that binding only so the later phases start "
                "from its state; not verified\n", done->name, done->beat, done->original,
                (unsigned)done->owner, binding ? binding : "no live node", done->lands);
    } else {
        ++t.passed;
        t.last_live = t.current;
    }
    ++t.current;
    memset(t.saw, 0, sizeof t.saw);
    t.step = 0;
    if (t.current > t.until) {
        finish();
        return;
    }
    if (!k_phases[t.current].begin) {
        report_not_live(t.current);
        finish();
        return;
    }
    k_phases[t.current].begin();
}

/* ------------------------------------------------------- first_control */

static void first_control_begin(void) {}

/* The end of the first world frame with the opening runtime finished and the
 * fade clear (the newgame-control handoff). Original: gameplay runs 0x1AE040
 * state 1 (+B = 1, +C = 0) under 001ACEC0 +8 = 3 and 001AD250 +9 = 1 with
 * selector 3B8D = 0 (ORIGINAL_FRAME_ORDER.md section 2, status_04.json frame
 * 0), in area 0x0B room 0 (route 01 row f0), with 49 pool nodes. */
static int first_control_frame(void)
{
    if (g.frame_no > FIRST_CONTROL_TIMEOUT) {
        fail("no first control within the opening timeout");
        return 0;
    }
    if (em_opening_runtime_busy() || em_frame_transition()->substate != 0)
        return 0;
    const EmSceneState *s = em_scene_state();
    int census = em_scene_bindings_pool_census();
    uint8_t b8 = task_byte(EM_SCENE_TASK_08), b9 = task_byte(EM_SCENE_TASK_09);
    uint8_t bb = task_byte(EM_SCENE_TASK_0B), bc = task_byte(EM_SCENE_TASK_0C);
    if (b8 != 3 || b9 != 1 || bb != 1 || bc != 0 || s->spad3B8D != 0 || census != FIRST_CONTROL_CENSUS ||
        s->d810700 != 0x0B || s->d810701 != 0) {
        fprintf(stderr, "level smoke: first_control: task +8/+9/+B/+C=%u/%u/%u/%u 3B8D=%u census=%d area=%02X/%u\n",
                b8, b9, bb, bc, s->spad3B8D, census, s->d810700, s->d810701);
        fail("first control is not gameplay state 1 with selector 0 and the original 49 nodes");
        return 0;
    }
    fprintf(stderr, "level smoke: first_control: PASS frame=%d task=%u/%u/%u/%u selector=%u census=%d "
            "d810750=%d\n", g.frame_no, b8, b9, bb, bc, s->spad3B8D, census, (int)s->d810750);
    return 1;
}

/* -------------------------------------------------------------- status */

static int world_unchanged(void)
{
    for (unsigned axis = 0; axis < 3; ++axis)
        if (g.pos[axis] != t.frozen_pos[axis] || g.cam.eye[axis] != t.frozen_eye[axis])
            return 0;
    return em_scene_state()->d810750 == t.frozen_variants;
}

/* START at the end of the first-control frame. It reaches D_00810E74 at the
 * next tick's step C, so 001AE7E0 returns 2 before another world frame. */
static void status_begin(void)
{
    memcpy(t.frozen_pos, g.pos, sizeof t.frozen_pos);
    memcpy(t.frozen_eye, g.cam.eye, sizeof t.frozen_eye);
    t.frozen_variants = em_scene_state()->d810750;
    t.step = 1;
    pad_key(EM_KEY_RETURN, 1); /* START */
}

/* Original (ORIGINAL_FRAME_ORDER.md section 2 and Q7; status_04.json): the
 * r == 2 tick and state 3 sub-step 0 draw no frame; every later state-3
 * frame runs 0020CDC0 and 001D1EA0(0) with the world frozen (no variant: no
 * D_00810750 increment, no player or camera update) and 3B8D stays 0; the
 * hub's close leads to +B = 5, one state-5 tick without a frame, then state
 * 1 with one 001AE5E0 per tick, under the 001AEE40(0x20) fade-in. The request
 * bytes B0 and C5 are clear after the close. The tick-exact sequence and the
 * exit fade are compared with the captures by tools/test_level_smoke.py. */
static int status_frame(void)
{
    const EmSceneState *s = em_scene_state();
    uint8_t state = task_byte(EM_SCENE_TASK_0B);
    if (s->spad3B8D != 0) {
        fail("selector 3B8D left 0 during a status screen opened from gameplay");
        return 0;
    }
    if (t.step == 1 || t.step == 2) {
        if (t.step == 1)
            pad_key(EM_KEY_RETURN, 0);
        if (state != 3 || task_byte(EM_SCENE_TASK_0C) != 1 || !world_unchanged()) {
            fail(t.step == 1 ? "START did not open the status screen with the world frozen"
                             : "the world advanced while the status screen showed");
            return 0;
        }
        t.step = 2;
        if (++t.frames == STATUS_HOLD_FRAMES) {
            pad_key('i', 1); /* TRIANGLE */
            t.step = 3;
        }
        return 0;
    }
    if (t.step == 3) {
        if (t.close_frames++ == 0)
            pad_key('i', 0);
        if (s->d810750 == t.frozen_variants) {
            if ((state != 3 && state != 5) || !world_unchanged()) {
                fail("the world advanced before the status screen closed");
                return 0;
            }
            if (t.close_frames > STATUS_CLOSE_TIMEOUT)
                fail("TRIANGLE did not close the status screen");
            return 0;
        }
        t.step = 4;
    }
    /* Resumed: state 1, one gameplay variant per frame. */
    if (state != 1 || s->d810750 != t.frozen_variants + 1 + t.resumed) {
        fail("the frames after the close are not one gameplay variant per frame in state 1");
        return 0;
    }
    if (s->req[EM_SCENE_REQ_B0] != 0 || s->req[EM_SCENE_REQ_C5] != 0) {
        fail("status request bytes B0/C5 still set after the close");
        return 0;
    }
    ++t.resumed;
    if (em_frame_transition()->substate != 0)
        return 0;
    fprintf(stderr, "level smoke: status: PASS status_frames=%d close_frames=%d resumed_frames=%d "
            "variants_frozen_at=%d\n", t.frames, t.close_frames, t.resumed, (int)t.frozen_variants);
    return 1;
}

/* ------------------------------------------------ route navigation (pad only)
 *
 * The closed loop of route_capture.py (FIRST_LEVEL_ROUTE.md section 1):
 * the left stick points at a world (x, z) target relative to the camera
 * forward D_00810600 (g.cam.fwd): stick up = forward, stick right =
 * (-fz, fx). It drives the analog stick through the gamepad overlay
 * (em_input_set_gamepad), the pad path a DualShock takes; every value here
 * is test input, not game behaviour. */
enum { NAV_LIMIT = 900, NAV_STUCK_WINDOW = 30 };

static void pad_apply(uint16_t buttons, float lx, float ly)
{
    t.pad = (EmPadState){buttons, lx, ly, 0, 0};
    t.pad_on = 1;
    em_input_set_gamepad(&t.pad);
}

static float nav_stick_toward(float x, float z, float magnitude)
{
    float dx = x - g.pos[0], dz = z - g.pos[2];
    float dist = sqrtf(dx * dx + dz * dz);
    float fx = g.cam.fwd[0], fz = g.cam.fwd[2];
    float norm = sqrtf(fx * fx + fz * fz);
    if (norm <= 0)
        norm = 1;
    fx /= norm;
    fz /= norm;
    float d = dist > 0 ? dist : 1;
    float up = (dx * fx + dz * fz) / d;
    float right = (-dx * fz + dz * fx) / d;
    pad_apply(0, magnitude * right, -magnitude * up);
    return dist;
}

static void nav_reset(void)
{
    t.nav_frames = 0;
    t.nav_hist_n = 0;
}

/* 1 reached (or stuck with stuck_ok), 0 continue, -1 failed (reported). */
static int nav_goto(float x, float z, float tol, float magnitude, int stuck_ok)
{
    if (nav_stick_toward(x, z, magnitude) <= tol) {
        pad_apply(0, 0, 0);
        nav_reset();
        return 1;
    }
    if (++t.nav_frames > NAV_LIMIT) {
        fail("navigation did not reach its target");
        return -1;
    }
    int slot = t.nav_hist_n % 64;
    t.nav_hist[slot][0] = g.pos[0];
    t.nav_hist[slot][1] = g.pos[2];
    ++t.nav_hist_n;
    if (t.nav_hist_n > NAV_STUCK_WINDOW) {
        int old = (t.nav_hist_n - 1 - NAV_STUCK_WINDOW) % 64;
        float mx = g.pos[0] - t.nav_hist[old][0], mz = g.pos[2] - t.nav_hist[old][1];
        if (sqrtf(mx * mx + mz * mz) < 0.05f) {
            pad_apply(0, 0, 0);
            nav_reset();
            if (stuck_ok)
                return 1;
            fail("navigation stuck");
            return -1;
        }
    }
    return 0;
}

static int in_control(void)
{
    return em_scene_state()->spad3B8D == 0 && !player_pose_owned() &&
           !em_game_player_interact_busy() && task_byte(EM_SCENE_TASK_0B) == 1;
}

static int idle_clip(void)
{
    unsigned clip;
    return player_pose_source(&clip, NULL, NULL, NULL) && clip == 0;
}

/* route_capture settle(): idle `frames`, then wait for control and the idle
 * clip. 1 done, 0 continue, -1 failed. */
static int nav_settle(int frames)
{
    pad_apply(0, 0, 0);
    ++t.nav_frames;
    if (t.nav_frames <= frames)
        return 0;
    if (in_control() && idle_clip()) {
        nav_reset();
        return 1;
    }
    if (t.nav_frames > frames + 600) {
        fail("control did not return (settle)");
        return -1;
    }
    return 0;
}

/* route_capture face(): short stick taps toward body yaw `yaw` (X = sin,
 * Z = cos) until within 0.12, at most 40 frames. */
static int nav_face(float yaw)
{
    float diff = fmodf(yaw - g.yaw + 3.14159265f, 6.28318531f);
    if (diff < 0)
        diff += 6.28318531f;
    diff -= 3.14159265f;
    if (fabsf(diff) <= 0.12f || ++t.nav_frames > 40) {
        pad_apply(0, 0, 0);
        nav_reset();
        return 1;
    }
    nav_stick_toward(g.pos[0] + 100 * sinf(yaw), g.pos[2] + 100 * cosf(yaw), 0.6f);
    return 0;
}

/* Hold `buttons` for `frames` frames, then release. */
static int nav_press(uint16_t buttons, int frames)
{
    if (t.nav_frames++ < frames) {
        pad_apply(buttons, 0, 0);
        return 0;
    }
    pad_apply(0, 0, 0);
    nav_reset();
    return 1;
}

/* Run the navigation step `r` of a phase: advance on 1, stop on -1. */
#define NAV_STEP(expr)                                                                             \
    do {                                                                                           \
        int r_ = (expr);                                                                           \
        if (r_ < 0)                                                                                \
            return 0;                                                                              \
        if (r_ > 0)                                                                                \
            ++t.step;                                                                              \
        return 0;                                                                                  \
    } while (0)

static int power_bit(void)
{
    return em_game_terminal_powered();
}

/* The scan tick: 00184BA0 accepted the Use (3B8D = 3; the terminal's
 * script turns it into 2 on the next frame, the panel's in the same frame,
 * as in route beats 02 f200 and 03 f231), latched on every frame from the
 * press on as the first frame with 3B8D != 0. The capture checks align on
 * it (tools/test_level_smoke.py). */
static int scan_accepted(void)
{
    if (!t.saw[7] && em_scene_state()->spad3B8D != 0) {
        t.saw[7] = 1;
        t.scan_variants = em_scene_state()->d810750;
    }
    return t.saw[7];
}

/* ------------------------------------------------------------- battery
 *
 * NOT-LIVE, driven (route beat 01): the battery pickup g0.0 (00219550,
 * item 0x1B) at (211.6, 229.9, 227.2) still runs the legacy em_pickup take
 * until WP-6 binds its original owner (take script 0x266620, the status
 * ITEM page). The panel phase needs the item, so the runner walks to it and
 * presses Cross as route_capture's beat_battery does and waits for the
 * item count; nothing here is checked against the original. */
static void battery_begin(void)
{
    nav_reset();
}

static int battery_frame(void)
{
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(211.6f, 227.2f, 5.0f, 1.0f, 1));
    case 1: NAV_STEP(nav_settle(20));
    case 2: NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 3:
        if (em_pickup_item_count(0x1B) == 1) {
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > 600)
            fail("the legacy pickup take did not add item 0x1B");
        return 0;
    case 4: NAV_STEP(nav_settle(30));
    default:
        return 1;
    }
}

/* Route beats 02 and 04: the terminal 00827B10 at (224, 230, 250.7). Its
 * script aligns the player to (222, y, 250) facing -1.3037; approach along
 * -x so 00183EF0's facing gate passes (route_capture use_elevator_terminal). */
static int use_terminal(void)
{
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(229.0f, 250.4f, 1.0f, 1.0f, 1));
    case 1: NAV_STEP(nav_goto(223.5f, 250.4f, 0.6f, 0.5f, 1));
    case 2: NAV_STEP(nav_settle(20));
    case 3: NAV_STEP(nav_face(-1.3037f));
    case 4: NAV_STEP(nav_settle(10));
    case 5:
        (void)scan_accepted();
        NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 6:
        /* 00160220 -> 00184BA0: the terminal wins the scan (3B8D = 3). */
        if (scan_accepted()) {
            ++t.step;
            nav_reset();
            return 0;
        }
        if (++t.nav_frames > 60)
            fail("Cross at the terminal did not win the use scan (3B8D stayed 0)");
        return 0;
    default:
        return 1;
    }
}

/* ---------------------------------------------------- elevator_refusal
 *
 * Route beat 02: with the power bit D_0081084C & 0x80 clear the terminal
 * 00827B10 runs the refusal 0x82A990: the scripted frame (3B8D 3 -> 2,
 * camera byte 1, the letterbox fading in), message 0x8000001A, then the
 * release (3B8D 0) with the player placed at (222, y, 250). In process:
 * the scan, the letterbox and the message were seen, the power bit stays
 * clear and control returns. The tick-by-tick comparison with the capture
 * is tools/test_level_smoke.py's. */
static void refusal_begin(void)
{
    nav_reset();
    if (power_bit())
        fail("the power bit is set before the refusal (route beat 02 has it clear)");
}

static int refusal_frame(void)
{
    if (t.step <= 6)
        return use_terminal(), 0;
    uint32_t message[3];
    em_area11_interaction_host_message_block(message);
    if (message[1] && message[2] == 0x8000001Au)
        t.saw[0] = 1;
    if (em_frame_screen_fade()->state == 3 || em_frame_screen_fade()->state == 1)
        t.saw[1] = 1;
    if (g.cam.top_mode == 1)
        t.saw[2] = 1;
    if (t.step == 7) {
        if (power_bit()) {
            fail("the refusal set the power bit");
            return 0;
        }
        if (!in_control()) {
            if (++t.nav_frames > 1500)
                fail("the refusal script did not release the player");
            return 0;
        }
        if (!t.saw[0] || !t.saw[1] || !t.saw[2]) {
            fprintf(stderr, "level smoke: elevator_refusal: message 0x8000001A %s, letterbox %s, camera "
                    "byte 1 %s\n", t.saw[0] ? "seen" : "missing", t.saw[1] ? "seen" : "missing",
                    t.saw[2] ? "seen" : "missing");
            fail("the refusal did not run the original script's message, letterbox and camera");
            return 0;
        }
        ++t.step;
        nav_reset();
        return 0;
    }
    if (t.step == 8) {
        int r = nav_settle(30);
        if (r <= 0)
            return 0;
        fprintf(stderr, "level smoke: elevator_refusal: PASS scan_d810750=%d player=(%.3f,%.3f,%.3f) "
                "yaw=%.5f power=%d\n", (int)t.scan_variants, g.pos[0], g.pos[1], g.pos[2], g.yaw,
                power_bit());
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------------- panel
 *
 * Route beat 03: Cross at the panel 00159210 with item 0x1B starts
 * 0x2477A0 (message 0x80000018); its 00157F60 posts B0 = 1 / B1 = 0x82 and
 * the status screen opens on the BATTERY page's "consume 2 units" prompt
 * (default No); LEFT then Cross select Yes; the discharge runs 12 -> 10 ->
 * 8 half-units; 0x247BE0 plays clip 0x15C and 001580C0 sets the power bit
 * 0x80 before control returns. */
static void panel_begin(void)
{
    nav_reset();
    if (em_pickup_item_count(0x1B) != 1 || em_pickup_battery_charge() != 12)
        fail("the panel phase needs item 0x1B with charge 12 (route beat 03 starts so)");
}

static int prompt_open(void)
{
    const EmStatusRuntime *status = em_area11_interaction_host_status();
    const EmStatusPage *page = status ? em_status_runtime_page(status) : NULL;
    return task_byte(EM_SCENE_TASK_0B) == 3 && page && page->phase == 3 && page->step == 2 &&
           page->item.step == 4;
}

static int panel_frame(void)
{
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(239.7f, 216.0f, 1.0f, 1.0f, 1));
    /* route_capture aims at (239.7, 222.0); the original's walk carried the
     * player on to (241.4, 225.3) before the press (beat 03 f228). The
     * port's walk stops shorter, so the runner aims at that press point:
     * 00183EF0's panel radius is 9.5 around (240, 232.8). */
    case 1: NAV_STEP(nav_goto(240.5f, 225.0f, 0.8f, 0.5f, 1));
    case 2: NAV_STEP(nav_settle(20));
    case 3: NAV_STEP(nav_face(0.0f));
    case 4: NAV_STEP(nav_settle(10));
    case 5:
        (void)scan_accepted();
        NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 6:
        if (scan_accepted()) {
            ++t.step;
            nav_reset();
        } else if (++t.nav_frames > 60) {
            fail("Cross at the panel did not win the use scan (3B8D stayed 0)");
        }
        return 0;
    case 7:
        /* 00157F60's request and the BATTERY prompt on its status page. */
        if (em_scene_state()->req[EM_SCENE_REQ_B0] == 1 && em_scene_state()->req[EM_SCENE_REQ_B1] == 0x82)
            t.saw[0] = 1;
        if (prompt_open()) {
            if (!t.saw[0]) {
                fail("the BATTERY page opened without the 00157F60 request B0 = 1 / B1 = 0x82");
                return 0;
            }
            ++t.step;
            nav_reset();
        } else if (++t.nav_frames > 900) {
            fail("the BATTERY prompt did not open");
        }
        return 0;
    case 8:
        /* route_capture: idle 30, LEFT 2 (+10), Cross 2. */
        if (++t.nav_frames > 30) {
            ++t.step;
            nav_reset();
        }
        pad_apply(0, 0, 0);
        return 0;
    case 9: NAV_STEP(nav_press(EM_PAD_LEFT, 2));
    case 10:
        pad_apply(0, 0, 0);
        if (++t.nav_frames > 10) {
            ++t.step;
            nav_reset();
        }
        return 0;
    case 11: NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 12:
        if (em_pickup_battery_charge() == 8)
            t.saw[1] = 1;
        if (power_bit()) {
            ++t.step;
            nav_reset();
        } else if (++t.nav_frames > 900) {
            fail("the power bit 0x80 was not set");
        }
        return 0;
    case 13:
        if (in_control()) {
            ++t.step;
            nav_reset();
        } else if (++t.nav_frames > 900) {
            fail("the panel script did not release the player");
        }
        return 0;
    case 14: {
        int r = nav_settle(30);
        if (r <= 0)
            return 0;
        if (!t.saw[1] || em_pickup_battery_charge() != 8 || em_pickup_item_count(0x1B) != 1) {
            fail("the discharge did not leave item 0x1B with charge 8 (route beat 03)");
            return 0;
        }
        fprintf(stderr, "level smoke: panel: PASS scan_d810750=%d power=%d charge=%d player=(%.3f,%.3f,%.3f)"
                " yaw=%.5f\n", (int)t.scan_variants, power_bit(), em_pickup_battery_charge(), g.pos[0],
                g.pos[1], g.pos[2], g.yaw);
        return 1;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------ elevator
 *
 * Route beat 04: with the power bit set the terminal runs 0x82A750: the
 * scripted frame, clip 0x47, the 150-call carry 00828050, and the release
 * at (222, 190, 250) with the elevator's floor byte D_0081083A toggled to
 * the lower floor. */
static void elevator_begin(void)
{
    nav_reset();
    if (!power_bit())
        fail("the elevator phase needs the power bit (route beat 04 starts powered)");
}

static int elevator_frame(void)
{
    if (t.step <= 6)
        return use_terminal(), 0;
    if (t.step == 7) {
        if (!in_control()) {
            if (++t.nav_frames > 1500)
                fail("the powered terminal script did not release the player");
            return 0;
        }
        const uint8_t *floor = em_scene_progress_at(em_scene_state(), 0x0081083Au, 1);
        if (!floor || *floor != 1 || fabsf(g.pos[1] - 190.0f) > 0.01f) {
            fprintf(stderr, "level smoke: elevator: floor byte %d, player y %.5f\n", floor ? *floor : -1,
                    g.pos[1]);
            fail("the ride did not carry the player down to the lower floor (y 190, D_0081083A = 1)");
            return 0;
        }
        ++t.step;
        nav_reset();
        return 0;
    }
    int r = nav_settle(30);
    if (r <= 0)
        return 0;
    fprintf(stderr, "level smoke: elevator: PASS scan_d810750=%d player=(%.3f,%.5f,%.3f) yaw=%.5f\n",
            (int)t.scan_variants, g.pos[0], g.pos[1], g.pos[2], g.yaw);
    return 1;
}

/* ------------------------------------------------------------- driver */

void em_level_smoke_test_begin(void)
{
    memset(&t, 0, sizeof t);
    const char *value = getenv("EM_STARTUP_TEST");
    t.active = value && strcmp(value, "newgame-level") == 0;
    if (!t.active)
        return;
    t.until = PHASE_COUNT - 1;
    const char *until = getenv("EM_LEVEL_SMOKE_UNTIL");
    if (until && until[0]) {
        t.until = -1;
        for (int i = 0; i < PHASE_COUNT; ++i)
            if (strcmp(until, k_phases[i].name) == 0)
                t.until = i;
        if (t.until < 0) {
            fprintf(stderr, "level smoke: EM_LEVEL_SMOKE_UNTIL=%s is not a phase; phases:", until);
            for (int i = 0; i < PHASE_COUNT; ++i)
                fprintf(stderr, " %s", k_phases[i].name);
            fputc('\n', stderr);
            t.current = PHASE_COUNT;
            fail("unknown phase");
            return;
        }
    }
    fprintf(stderr, "level smoke: New Game through %s\n", k_phases[t.until].name);
    t.last_live = -1;
    k_phases[0].begin();
}

void em_level_smoke_test_after_frame(void)
{
    if (!t.active || t.failed)
        return;
    if (t.stop_pending) {
        t.stop_pending = 0;
        if (em_scene_faulted(em_scene_state()))
            fail("the scene coordinator faulted on the frame after the last phase");
        else
            em_frame_request_quit();
        return;
    }
    if (t.current > t.until || t.current >= PHASE_COUNT)
        return;
    if (em_scene_faulted(em_scene_state())) {
        fail("the scene coordinator faulted");
        return;
    }
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!isfinite(g.pos[axis]) || !isfinite(g.cam.eye[axis])) {
            fail("nonfinite player or camera");
            return;
        }
    if (k_phases[t.current].frame() == 1 && !t.failed)
        next_phase();
}

int em_level_smoke_test_active(void) {return t.active;}
int em_level_smoke_test_failed(void) {return t.failed;}
