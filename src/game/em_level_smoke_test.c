/* The live first-level smoke (SCENE_COORDINATOR_DESIGN.md step S13). See
 * em_level_smoke_test.h and docs/LEVEL_SMOKE.md.
 *
 * The phases follow the original route (docs/FIRST_LEVEL_ROUTE.md section
 * 3, main line 01 -> 14). A phase is LIVE when this file has a runner for it;
 * a runner is added in the step that makes the phase's original owners live
 * in the port (the `lands` column), never before. Until then the phase
 * reports NOT-LIVE, naming the original owner, the binding the port runs
 * for it today and the step it waits on; the run stops there, because every
 * later beat starts from the state the earlier ones leave.
 *
 * Each runner drives pad input only (as the route captures do) and asserts
 * the original values it can observe in process. The tick-by-tick comparison
 * against the original captures reads the scene tick log
 * (EM_AREA_CHANGE_LOG) in tools/test_level_smoke.py; the runner prints the
 * join key it needs (D_00810750 at first control). */
#include "game/em_level_smoke_test.h"
#include "em_input.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_opening_runtime.h"
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
} Phase;

static void first_control_begin(void);
static int first_control_frame(void);
static void status_begin(void);
static int status_frame(void);

static const Phase k_phases[] = {
    {"first_control", "01_battery (row f0 = slot 04)", 0,
     "0x1AE040 state 1 with 001AE5E0; the AREA11 pool of 49 nodes (ORIGINAL_FRAME_ORDER.md 2, 4)",
     "S12a", first_control_begin, first_control_frame},
    {"status", "01_battery (status exit); frame_trace2/status_04.json", 0,
     "001AE7E0 r==2 -> state 3 (0020E060, 0020CDC0) -> state 5 -> state 1 (ORIGINAL_FRAME_ORDER.md Q7)",
     "S11b", status_begin, status_frame},
    {"battery", "01_battery", 0x00219550u,
     "pickup 00219550 g0.0 (item 0x1B): take script 0x266620, B0=1/B1=0x1B, status ITEM page",
     "WP-6 (pickup owner and Use arbiter) with WP-5 (status ITEM page)", NULL, NULL},
    {"elevator_refusal", "02_elevator_refusal", 0x00827B10u,
     "terminal 0x827B10 (r19): refusal script 0x82A990, message 0x8000001A, letterbox",
     "WP-4 (terminal) with WP-8 (message)", NULL, NULL},
    {"panel", "03_panel_power", 0x00159210u,
     "panel 00159210 (r18): script 0x2477A0, 00157F60 B0=1/B1=0x82 (BATTERY page), discharge, "
     "script 0x247BE0, power bit 0x80",
     "WP-4 (panel) with WP-5 (BATTERY page)", NULL, NULL},
    {"elevator", "04_elevator_ride", 0x00827B10u,
     "terminal 0x827B10: powered script 0x82A750, clip 0x47, carry 0x828050 down to y 190", "WP-4", NULL,
     NULL},
    {"boxes", "05_boxes", 0x001551B0u, "ledge climb (state 2, +1F0 8) onto crates r4 and r3 (001551B0)",
     "the ledge climb wired to the player (em_player_climb; WP-15) with WP-18 (crates)", NULL, NULL},
    {"slide", "06_hill_slide", 0, "slope slide 0016C6A0 (state 0x1C, +1F0 0x30)",
     "the slope slide wired to the player (em_player_slide; WP-15)", NULL, NULL},
    {"truck_preview", "07_truck_preview", 0x008251E0u,
     "trigger 0x8251E0 (r17): camera script 0x8292C0, letterbox, D_00810792=1",
     "WP-12 (trigger) with the WP-10 script host", NULL, NULL},
    {"truck_crossing", "08_truck_crossing", 0x00823FF0u,
     "truck 0x823FF0 (r16): stand-on arm, shake, fall, D_00810792=0xFF", "WP-12", NULL, NULL},
    {"cage_roof", "10_cage_roof_roger", 0x008253F0u,
     "ladder column x 360; director 0x8253F0 beat 0 script 0x8294C0; Roger 0x8237E0 script 0x828990",
     "the ladder (WP-15), WP-10 (director) and WP-9 (Roger)", NULL, NULL},
    {"crevice_prompt", "11_crevice_prompt", 0x008253F0u,
     "tank ledge climb, pipes; director beat 1 script 0x829A40 (line 0x97)",
     "WP-10 with WP-8 and the ledge climb (WP-15)", NULL, NULL},
    {"crevice_jump", "12_crevice_jump", 0, "running jump (+1F0 0x0C, state 6) onto the north block",
     "the running jump (no port module; WP-15)", NULL, NULL},
    {"east_tower", "13_east_tower", 0x008253F0u,
     "high ledge climb; director beat 2 script 0x829CC0 (line 0x99)", "WP-10 and the ledge climb (WP-15)",
     NULL, NULL},
    {"roger", "14_roger_encounter", 0x008237E0u,
     "running jump; Roger 0x8237E0 quad 0x82AB80, script 0x8283D0 (bank 96), 0x8107D8=1",
     "WP-9 and the running jump (WP-15)", NULL, NULL},
};
enum { PHASE_COUNT = (int)(sizeof k_phases / sizeof k_phases[0]) };

/* The first-control census (ORIGINAL_FRAME_ORDER.md section 4: nodes #0..#48,
 * record 13 freed on the second world frame, Q3). */
enum { FIRST_CONTROL_CENSUS = 49 };
/* Test timeouts (not game behaviour). */
enum { FIRST_CONTROL_TIMEOUT = 3000, STATUS_HOLD_FRAMES = 30, STATUS_CLOSE_TIMEOUT = 120 };

static struct {
    int active, failed, until, current, passed;
    int stop_pending; /* the run passed; quit after one more frame (finish) */
    /* status */
    int step, frames, close_frames, resumed;
    float frozen_pos[3], frozen_eye[3];
    int32_t frozen_variants;
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
            k_phases[t.passed - 1].name);
    if (t.current <= t.until)
        fprintf(stderr, "; NOT-LIVE from %s through %s (not verified)\n", k_phases[t.current].name,
                k_phases[t.until].name);
    else
        fputc('\n', stderr);
    /* Quit after one more frame, not now. The route captures sample after
     * the original frame, and the original's 0x28A9A0 fade ticks after the
     * slot-0 task, so the port's post-frame fade is the next tick's start
     * sample in the scene tick log (em_scene_bindings.c log_tick_begin);
     * tools/test_level_smoke.py needs that tick after the last live phase.
     * The extra frame takes no input (test driver, not game behaviour). */
    t.stop_pending = 1;
}

/* Advance to the next phase, or stop at the first NOT-LIVE one. */
static void next_phase(void)
{
    ++t.passed;
    t.current = t.passed;
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
