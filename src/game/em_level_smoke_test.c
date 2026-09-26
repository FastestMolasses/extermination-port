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
 * NOT-LIVE driven, never passed (none since WP-6 made the battery live).
 *
 * Each runner drives pad input only (as the route captures do) and asserts
 * the original values it can observe in process. The tick-by-tick comparison
 * against the original captures reads the scene tick log
 * (EM_AREA_CHANGE_LOG) in tools/test_level_smoke.py; the runner prints the
 * join key it needs (D_00810750 at first control). */
#include "game/em_level_smoke_test.h"
#include "em_input.h"
#include "game/em_area11_bindings.h"
#include "game/em_area11_boxes.h"
#include "game/em_area11_roger.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_pad_actuator.h"
#include "game/em_frame.h"
#include "game/em_game.h"
#include "game/em_game_internal.h"
#include "game/em_opening_runtime.h"
#include "game/em_pickup.h"
#include "game/em_scene_bindings.h"
#include "game/em_scene_state.h"
#include "game/em_status_background.h"
#include "game/em_status_models.h"
#include "game/em_status_runtime.h"
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
    /* 1: a side beat (FIRST_LEVEL_ROUTE.md section 3: beats 00 and 09 have
     * their own snapshots and are not on the main line). The main line
     * skips it and names it NOT-LIVE / side; EM_LEVEL_SMOKE_UNTIL=<it>
     * runs the main line up to it and then only it. */
    int side;
} Phase;

static void first_control_begin(void);
static int first_control_frame(void);
static void panel_no_battery_begin(void);
static int panel_no_battery_frame(void);
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
static void boxes_begin(void);
static int boxes_frame(void);
static void slide_begin(void);
static int slide_frame(void);
static void truck_preview_begin(void);
static int truck_preview_frame(void);
static void truck_crossing_begin(void);
static int truck_crossing_frame(void);
static void cage_ladders_begin(void);
static int cage_ladders_frame(void);
static void director_begin(void);
static int cage_roof_frame(void);
static void crevice_climbs_begin(void);
static int crevice_climbs_frame(void);
static int crevice_prompt_frame(void);
static void crevice_jump_begin(void);
static int crevice_jump_frame(void);
static void east_tower_climb_begin(void);
static int east_tower_climb_frame(void);
static int east_tower_frame(void);
static void roger_begin(void);
static int roger_frame(void);

static const Phase k_phases[] = {
    {"first_control", "01_battery (row f0 = slot 04)", 0,
     "0x1AE040 state 1 with 001AE5E0; the AREA11 pool of 49 nodes (ORIGINAL_FRAME_ORDER.md 2, 4)",
     "S12a", first_control_begin, first_control_frame, 0, 0},
    {"panel_no_battery", "00_panel_no_battery (side, slot 04)", 0x00159210u,
     "panel 00159210 (r18) without item 0x1B: script 0x246F20, message 0x80000018, letterbox",
     "WP-4 (the panel's original owner and its scripts)", panel_no_battery_begin, panel_no_battery_frame, 0,
     1},
    {"status", "01_battery (status exit); frame_trace2/status_04.json", 0,
     "001AE7E0 r==2 -> state 3 (0020E060, 0020CDC0) -> state 5 -> state 1 (ORIGINAL_FRAME_ORDER.md Q7)",
     "S11b", status_begin, status_frame, 0, 0},
    {"battery", "01_battery", 0x00219550u,
     "pickup 00219550 g0.0 (item 0x1B): take script 0x266620, B0=1/B1=0x1B, status ITEM page",
     "WP-6 (pickup owner and Use arbiter) with WP-5 (status ITEM page)", battery_begin, battery_frame,
     0, 0},
    {"elevator_refusal", "02_elevator_refusal", 0x00827B10u,
     "terminal 0x827B10 (r19): refusal script 0x82A990, message 0x8000001A, letterbox",
     "WP-4", refusal_begin, refusal_frame, 0, 0},
    {"panel", "03_panel_power", 0x00159210u,
     "panel 00159210 (r18): script 0x2477A0, 00157F60 B0=1/B1=0x82 (BATTERY page), discharge, "
     "script 0x247BE0, power bit 0x80",
     "WP-4", panel_begin, panel_frame, 0, 0},
    {"elevator", "04_elevator_ride", 0x00827B10u,
     "terminal 0x827B10: powered script 0x82A750, clip 0x47, carry 0x828050 down to y 190", "WP-4",
     elevator_begin, elevator_frame, 0, 0},
    {"boxes", "05_boxes", 0x001551B0u, "ledge climb (state 2, +1F0 8) onto crates r4 and r3 (001551B0)",
     "the Use chain and the crates' original owners (census L25)", boxes_begin, boxes_frame, 0, 0},
    {"slide", "06_hill_slide", 0, "slope slide 0016C6A0 (state 0x1C, +1F0 0x30)",
     "the slope slide on the live record (em_player_slide; census L03)", slide_begin, slide_frame, 0, 0},
    {"truck_preview", "07_truck_preview", 0x008251E0u,
     "trigger 0x8251E0 (r17): camera script 0x8292C0, letterbox, D_00810792=1",
     "the trigger and the AREA11 script host (census L23, L19)", truck_preview_begin,
     truck_preview_frame, 0, 0},
    {"truck_crossing", "08_truck_crossing", 0x00823FF0u,
     "truck 0x823FF0 (r16): stand-on arm, shake, fall, D_00810792=0xFF",
     "the truck's original owner (census L23)", truck_crossing_begin, truck_crossing_frame, 0, 0},
    {"fence_door", "09_fence_door (side, from 08)", 0x001BC350u,
     "door 001BC350 (r0): scripts 0x24DE40 / 0x24DC00, clip 0x45, room move B7=2/B8=2 to entry 2",
     "census L18 (the door's original owner; the room move has its own capture test, "
     "make test-room-move-reference)", NULL, NULL, 0, 1},
    {"cage_ladders", "10_cage_roof_roger", 0,
     "ladder column x 360: Use 0015D4C0 case 0x32, entry 00165B60 (state 0xB), climb 001662D0 (state 0xC)",
     "the ladder entry and climb on the live record (census L09, L10)", cage_ladders_begin,
     cage_ladders_frame, 0, 0},
    {"cage_roof", "10_cage_roof_roger", 0x008253F0u,
     "director 0x8253F0 beat 0 script 0x8294C0 (260 <= Y <= 280, quad 0x82ABE0); Roger 0x8237E0 script "
     "0x828990 (voiced line 0x7F, VOICE.DAT cues 143..148); D_00810813 0 -> 1 -> 0x10 -> 0x11",
     "census L21 with WP-8b (the director on its original scripts, the voice lanes)", director_begin,
     cage_roof_frame, 0, 0},
    {"crevice_climbs", "11_crevice_prompt", 0,
     "tank ledge climb (state 2, +1F0 8), the pipes (fall 5 / 0xB), pipe-end ledge climb",
     "the ledge climb and fall on the live record (census L04, L02)", crevice_climbs_begin,
     crevice_climbs_frame, 0, 0},
    {"crevice_prompt", "11_crevice_prompt", 0x008253F0u,
     "director beat 1 script 0x829A40 (Y >= 275, quad 0x82AC20; line 0x97, VOICE.DAT cue 150); D_00810813 -> 0x20",
     "census L21 with WP-8b", director_begin, crevice_prompt_frame, 0, 0},
    {"crevice_jump", "12_crevice_jump", 0,
     "running jump 0015EC50 / 001634A0 (+1F0 0x0C, state 6) onto the north block, landing 8 / 0xF",
     "the running jump on the live record (census L11)", crevice_jump_begin, crevice_jump_frame, 0, 0},
    {"east_tower_climb", "13_east_tower", 0, "high ledge climb (state 2, +1F0 8) onto the east tower top",
     "the ledge climb on the live record (census L04)", east_tower_climb_begin, east_tower_climb_frame, 0, 0},
    {"east_tower", "13_east_tower", 0x008253F0u,
     "director beat 2 script 0x829CC0 (Y >= 285, quad 0x82AC60; line 0x99, VOICE.DAT cue 149); D_00810813 -> 0xFF",
     "census L21 with WP-8b", director_begin, east_tower_frame, 0, 0},
    {"roger", "14_roger_encounter", 0x008237E0u,
     "running jump; Roger 0x8237E0 quad 0x82AB80, script 0x8283D0 (bank 96), 0x8107D8=1",
     "Roger's original owner and scripts (census L22)", roger_begin, roger_frame, 0, 0},
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
    /* the hub's draws: D_002655A0 steps and UI+0x20 against the hub
     * frames the page core ran at sub-state 1 */
    unsigned long hub_background_base;
    unsigned hub_draws;
    unsigned long hub_models_base;
    uint8_t hub_phase, hub_step;
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
        if (p->side && i != t.until)
            continue;
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
    /* Verification aid (LEVEL_SMOKE.md "Frame captures"):
     * EM_LEVEL_SMOKE_PHASE_CAPTURE=<phase>:<path.bmp> saves the frame that
     * ends <phase>, for a look beside the route beat's original.png. */
    const char *pc = getenv("EM_LEVEL_SMOKE_PHASE_CAPTURE");
    const char *colon = pc ? strchr(pc, ':') : NULL;
    if (colon && colon[1] && (size_t)(colon - pc) == strlen(done->name) &&
        strncmp(pc, done->name, (size_t)(colon - pc)) == 0)
        em_gfx_request_capture(em_frame_gfx(), colon + 1);
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
    /* The main line passes a side beat by: named, not run (it starts from
     * its own snapshot in the route). */
    while (t.current < t.until && k_phases[t.current].side) {
        const Phase *p = &k_phases[t.current];
        if (p->begin) {
            fprintf(stderr, "level smoke: %s: side beat, not on the main line (route beat %s; live, run on its "
                    "own with EM_LEVEL_SMOKE_UNTIL=%s)\n", p->name, p->beat, p->name);
        } else {
            const char *binding = em_scene_bindings_pool_binding(p->owner);
            fprintf(stderr, "level smoke: %s: side beat, not on the main line (route beat %s; NOT-LIVE: "
                    "original: %s; port binding of %08X: %s; lands with %s)\n", p->name, p->beat, p->original,
                    (unsigned)p->owner, binding ? binding : "no live node", p->lands);
        }
        ++t.current;
    }
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
    t.hub_background_base = em_status_background_live_steps();
    t.hub_models_base = em_status_models_drawn(em_area11_interaction_host_status_models());
    t.hub_draws = 0;
    t.hub_phase = t.hub_step = 0;
    pad_key(EM_KEY_RETURN, 1); /* START */
}

/* 0020CDC0 phase 1 draws only at sub-state 1: 0020A7A0 (one D_002655A0
 * step) and 00209DF0 (00208AD0 advances UI+0x20 once). A frame whose tick
 * started at sub-state 1 drew the hub, the close-edge frame included;
 * the cold entry, sub-state 0 and the 0020E0C0 exit frames draw nothing. */
static int hub_draws_checked(void)
{
    const EmStatusRuntime *status = em_area11_interaction_host_status();
    const EmStatusPage *page = status ? em_status_runtime_page(status) : NULL;
    if (!page) {
        fail("the AREA11 status runtime is missing");
        return 0;
    }
    if (t.hub_phase == 1 && t.hub_step == 1)
        ++t.hub_draws;
    t.hub_phase = page->phase;
    t.hub_step = page->step;
    if (em_status_background_live_steps() - t.hub_background_base != t.hub_draws ||
        (page->phase == 1 && em_status_runtime_ui_clock(status) != t.hub_draws)) {
        fail("the hub did not step 0020A7A0 and draw 00209DF0 exactly once per sub-state-1 frame");
        return 0;
    }
    /* The models (em_status_models): 0020CDC0 sub-state 0 fills the pool
     * D_0028B020 as in the status-hub capture (record 0 the menu player
     * 0020E6F0, records 1..6 the letters 0020E460 with the glyphs 0020E250
     * derives from CA4..CA7 = FF 05 00 07: '/', '@', '0', '1', '2', '8').
     * The first sub-state-1 walk only initialises them (0020E6F0 and
     * 0020E460 state 0 draw nothing); every later hub frame draws each
     * record once (001CB580). */
    const EmStatusModels *models = em_area11_interaction_host_status_models();
    const EmStatusScenePool *pool = em_status_models_pool(models);
    static const uint8_t glyphs[6] = {0x2F, 0x40, 0x30, 0x31, 0x32, 0x38};
    unsigned used = 0;
    for (unsigned i = 0; pool && i < EM_STATUS_SCENE_POOL_RECORDS; ++i)
        used += pool->record[i].b00 != 0;
    unsigned long drawn = em_status_models_drawn(models) - t.hub_models_base;
    if (!pool || (t.hub_draws && page->phase == 1 &&
                  (used != 7 || pool->record[0].w10 != 0x0020E6F0u ||
                   drawn != 7ul * (t.hub_draws - 1)))) {
        fail("the hub models are not the capture's seven records drawn once per hub frame");
        return 0;
    }
    for (unsigned i = 0; t.hub_draws && page->phase == 1 && i < 6; ++i)
        if (pool->record[1 + i].w10 != 0x0020E460u || pool->record[1 + i].b0D != glyphs[i]) {
            fail("the hub's letter models are not the status-hub capture's");
            return 0;
        }
    /* Verification aid: EM_LEVEL_SMOKE_HUB_CAPTURE=<path.bmp> writes the
     * hub frame whose walk equals the status-hub capture (walk 10, the
     * menu player's captured breathe/yaw; tests/status_models_test.c). */
    const char *capture = getenv("EM_LEVEL_SMOKE_HUB_CAPTURE");
    if (capture && *capture && t.hub_draws == 9 && page->phase == 1 && page->step == 1)
        em_gfx_request_capture(em_frame_gfx(), capture);
    return 1;
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
        if (!hub_draws_checked())
            return 0;
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
            if (state == 3 && !hub_draws_checked())
                return 0;
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
    if (em_status_background_live_steps() - t.hub_background_base != t.hub_draws ||
        t.hub_draws < 2) {
        fail("0020A7A0 stepped outside the hub's sub-state-1 frames");
        return 0;
    }
    fprintf(stderr, "level smoke: status: PASS status_frames=%d close_frames=%d resumed_frames=%d "
            "variants_frozen_at=%d hub_draws=%u\n", t.frames, t.close_frames, t.resumed,
            (int)t.frozen_variants, t.hub_draws);
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

/* ---------------------------------------------------- panel_no_battery
 *
 * Route beat 00 (a side beat from slot 04, the first-control state):
 * without item 0x1B, Cross at the panel 00159210 starts 0x246F20 (the scan
 * and the script's op07/2 in the same frame, 3B8D 0 -> 2, f75; the player
 * placed at (239.7, y, 223.8) facing 0), message 0x80000018 in mode 2
 * (f79..f229), the bars, then the release at f230. The runner walks from
 * first control to the route's press stance (242.605, 226.742; f71) and
 * faces its heading 0.69894 (navigation input, as the battery's), then
 * presses Cross as route_capture's beat_panel_no_battery does. In process:
 * the scan, the message, the letterbox and camera byte 1 were seen, no
 * item and no power, and control returns. tools/test_level_smoke.py
 * check_panel_no_battery compares the capture row for row. Run on its
 * own: EM_LEVEL_SMOKE_UNTIL=panel_no_battery (make test-level-smoke-full). */
static void panel_no_battery_begin(void)
{
    nav_reset();
    if (em_pickup_item_count(0x1B) != 0 || power_bit())
        fail("route beat 00 starts without item 0x1B and without power");
}

static int panel_no_battery_frame(void)
{
    const EmMessageBlock *message = em_message_live_block();
    if (message && message->phase && message->line == 0x80000018u)
        t.saw[0] = 1;
    if (em_frame_screen_fade()->state == 3 || em_frame_screen_fade()->state == 1)
        t.saw[1] = 1;
    if (g.cam.top_mode == 1)
        t.saw[2] = 1;
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(242.605f, 226.742f, 1.0f, 1.0f, 1));
    case 1: NAV_STEP(nav_goto(242.605f, 226.742f, 0.1f, 0.4f, 1));
    case 2: NAV_STEP(nav_settle(20));
    case 3: NAV_STEP(nav_face(0.69894f));
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
        if (!in_control()) {
            if (++t.nav_frames > 1000)
                fail("the panel's 0x246F20 did not release the player");
            return 0;
        }
        if (!t.saw[0] || !t.saw[1] || !t.saw[2]) {
            fprintf(stderr, "level smoke: panel_no_battery: message 0x80000018 %s, letterbox %s, camera byte 1 "
                    "%s\n", t.saw[0] ? "seen" : "missing", t.saw[1] ? "seen" : "missing",
                    t.saw[2] ? "seen" : "missing");
            fail("the panel without the battery did not run 0x246F20's message, letterbox and camera");
            return 0;
        }
        if (em_pickup_item_count(0x1B) != 0 || power_bit()) {
            fail("the panel without the battery changed the item or the power");
            return 0;
        }
        ++t.step;
        nav_reset();
        return 0;
    case 8: {
        int r = nav_settle(30);
        if (r <= 0)
            return 0;
        fprintf(stderr, "level smoke: panel_no_battery: PASS scan_d810750=%d player=(%.3f,%.5f,%.3f) "
                "yaw=%.5f\n", (int)t.scan_variants, g.pos[0], g.pos[1], g.pos[2], g.yaw);
        return 1;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------- battery
 *
 * Route beat 01 (live since WP-6): the battery pickup g0.0 (00219550, item
 * 0x1B) at (211.6, 229.9, 227.2). The runner walks to the route's stance
 * before its press (218.212, 222.373, facing -0.9588; f118..f124) and
 * presses Cross as route_capture's beat_battery does. 00184BA0 arms the
 * original owner (3B8D = 3, the scan tick is printed); its take program
 * 0x266620 turns the player, plays the grab clip, settles the camera target
 * and consumes the item (001C47A0: 001C40B0, B0 = 1, B1 = 0x1B), and the
 * status screen pops up on it (route f189 post, f192 open). In process:
 * the screen opens (+B = 3) with item 0x1B taken, the page is the ITEM root
 * in its BATTERY child (0020EE50 state 5 after module 0x21) showing
 * 002149F0's acquisition notice (sub-state 3) with charge and capacity 12,
 * the request consumed (B0 = 0); the notice hands over to the list
 * (sub-state 1) after route 01's 239 frames; TRIANGLE 20 frames later
 * (route f459 -> f479) closes the screen, control returns and the owner has
 * set its taken bit. tools/test_level_smoke.py check_battery compares the
 * take with route 01 row for row (LEVEL_SMOKE.md). */
static void battery_begin(void)
{
    nav_reset();
}

static int battery_frame(void)
{
    const EmStatusRuntime *status = em_area11_interaction_host_status();
    const EmStatusPage *page = status ? em_status_runtime_page(status) : NULL;
    const EmSceneState *s = em_scene_state();
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(218.212f, 222.373f, 1.0f, 1.0f, 1));
    case 1: NAV_STEP(nav_goto(218.212f, 222.373f, 0.1f, 0.4f, 1));
    case 2: NAV_STEP(nav_settle(20));
    case 3: NAV_STEP(nav_face(-0.9588f));
    case 4: NAV_STEP(nav_settle(10));
    case 5:
        (void)scan_accepted();
        NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 6:
        (void)scan_accepted();
        if (task_byte(EM_SCENE_TASK_0B) == 3) {
            if (em_pickup_item_count(0x1B) != 1 || s->req[EM_SCENE_REQ_B1] != 0x1B) {
                fail("the status screen opened without the item 0x1B take's request");
                return 0;
            }
            ++t.step;
            nav_reset();
            return 0;
        }
        if (++t.nav_frames > 600)
            fail("the battery take did not open the status screen");
        return 0;
    case 7:
        if (page && page->phase == 3 && page->item.screen == 0 && page->item.state == 5 &&
            page->item.step == 3) {
            if (em_pickup_battery_charge() != 12 || em_pickup_battery_capacity() != 12 ||
                s->req[EM_SCENE_REQ_B0] != 0) {
                fail("the BATTERY acquisition notice did not set charge/capacity 12 or keep B0");
                return 0;
            }
            ++t.step;
            nav_reset();
            return 0;
        }
        if (++t.nav_frames > 120)
            fail("the status screen did not show the BATTERY acquisition notice");
        return 0;
    case 8:
        if (page && page->item.state == 5 && page->item.step == 1) {
            fprintf(stderr, "level smoke: battery: pop-up notice %d frames, then the list\n",
                    t.nav_frames);
            /* Route 01_battery: the ITEM root is at state 5 step 3 from
             * f219 (its countdown 0xF0) to step 1 at f459; counted from
             * the frame after step 3 is seen, as here, that is 239
             * frames. The notice is the BATTERY page's own timer. */
            if (t.nav_frames != 239) {
                fail("the BATTERY acquisition notice did not last route 01's 239 frames");
                return 0;
            }
            ++t.step;
            nav_reset();
            return 0;
        }
        if (++t.nav_frames > 400)
            fail("the BATTERY acquisition notice did not hand over to the list");
        return 0;
    case 9:
        if (++t.nav_frames < 20)
            return 0;
        ++t.step;
        nav_reset();
        return 0;
    case 10: NAV_STEP(nav_press(EM_PAD_TRIANGLE, 1));
    case 11:
        if (task_byte(EM_SCENE_TASK_0B) == 1 && s->req[EM_SCENE_REQ_B0] == 0 &&
            em_frame_transition()->substate == 0) {
            ++t.step;
            nav_reset();
            return 0;
        }
        if (++t.nav_frames > STATUS_CLOSE_TIMEOUT)
            fail("TRIANGLE did not close the battery pop-up");
        return 0;
    case 12: {
        int r = nav_settle(30);
        if (r <= 0)
            return 0;
        /* 00219550's completion: the taken bit (001B1190) and the owner
         * freed with its light child. */
        if (!t.saw[7] || !em_pickup_taken(0x0B01) || em_pickup_item_count(0x1B) != 1) {
            fail("the battery owner did not complete its take (taken bit, count 1)");
            return 0;
        }
        fprintf(stderr, "level smoke: battery: PASS scan_d810750=%d player=(%.3f,%.3f,%.3f) yaw=%.5f "
                "charge=%d\n", (int)t.scan_variants, g.pos[0], g.pos[1], g.pos[2], g.yaw,
                em_pickup_battery_charge());
        return 1;
    }
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
    const EmMessageBlock *message = em_message_live_block();
    if (message && message->phase && message->line == 0x8000001Au)
        t.saw[0] = 1;
    /* Verification aid (tools/test_message_capture.py):
     * EM_LEVEL_SMOKE_MESSAGE_CAPTURE=<path.bmp> writes the frame whose step
     * F presents 0x8000001A for the fifth time (+0x68 = 5), the state of the
     * elevator/refusal capture; this hook runs before that frame's step F. */
    const char *capture = getenv("EM_LEVEL_SMOKE_MESSAGE_CAPTURE");
    if (capture && *capture && message && message->phase == 1 &&
        message->line == 0x8000001Au && message->frames == 4)
        em_gfx_request_capture(em_frame_gfx(), capture);
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

/* --------------------------------------------------------------- boxes
 *
 * Route beat 05: from the elevator's release the player walks to crate r4
 * (001551B0, 0x7A7C70, top y 203.8) and stands before it at the route's
 * stance (228.787, 281.266, facing 0.0265; route f160..f172). Cross: the Use
 * dispatcher 00160220 runs its ledge probes (0015DF10) and enters the ledge
 * climb (+5 = 2, +1F0 = 8; route f175), clips 0x70, 0x78 and 0x8C, and the
 * player stands on r4 (y 203.776, ground 0x7A7C70; f253/f254). Then west on
 * the crate top to (226.237, 288.309), facing -x (-1.5708; f380..f390), Cross
 * again: the climb onto the raised crate r3 (0x7A7980, y 217.786; f393 ..
 * f472), and north onto the upper ledge (f640: y 219.26 on the grid). In
 * process: both climbs are entered and end on their crates; the tick-by-tick
 * comparison of the climbs with the capture is tools/test_level_smoke.py's
 * check_boxes. */
static void boxes_begin(void)
{
    nav_reset();
}

/* The climb after a Cross: 1 once the stage has entered +5 = 2 and handed
 * back to control with the idle clip, 0 continue, -1 failed. */
static int boxes_climb(int index)
{
    uint8_t state = em_live_u8(player_states_actor(), 5);
    if (state == 2)
        t.saw[index] = 1;
    /* After the hand-back the pad stays neutral through the idle return
     * (route f254..f265: the clip 0 countdown from 12), as in the route. */
    if (t.saw[index] && state != 2 && in_control() && idle_clip() && ++t.saw[index + 2] > 12) {
        nav_reset();
        return 1;
    }
    if (++t.nav_frames > 200) {
        fail(t.saw[index] ? "the ledge climb did not hand back to control"
                          : "Cross did not enter the ledge climb (+5 = 2)");
        return -1;
    }
    return 0;
}

static int boxes_frame(void)
{
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(228.787f, 275.0f, 1.0f, 1.0f, 1));
    case 1: NAV_STEP(nav_goto(228.787f, 281.266f, 0.1f, 0.4f, 1));
    case 2: NAV_STEP(nav_settle(20));
    case 3: NAV_STEP(nav_face(0.0265f));
    case 4: NAV_STEP(nav_settle(30));
    case 5: NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 6: NAV_STEP(boxes_climb(0));
    case 7:
        if (fabsf(g.pos[1] - 203.776f) > 0.001f) {
            fprintf(stderr, "level smoke: boxes: after the first climb y %.5f\n", g.pos[1]);
            fail("the first climb did not end on crate r4 (y 203.776, route f253)");
            return 0;
        }
        ++t.step;
        return 0;
    case 8: NAV_STEP(nav_goto(226.237f, 288.309f, 0.1f, 0.4f, 1));
    case 9: NAV_STEP(nav_settle(20));
    case 10: NAV_STEP(nav_face(-1.5708f));
    case 11: NAV_STEP(nav_settle(30));
    case 12: NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 13: NAV_STEP(boxes_climb(1));
    case 14:
        if (fabsf(g.pos[1] - 217.78619f) > 0.001f) {
            fprintf(stderr, "level smoke: boxes: after the second climb y %.5f\n", g.pos[1]);
            fail("the second climb did not end on crate r3 (y 217.786, route f471)");
            return 0;
        }
        ++t.step;
        return 0;
    case 15: NAV_STEP(nav_goto(219.6f, 305.2f, 0.3f, 1.0f, 1));
    case 16: {
        int r = nav_settle(30);
        if (r <= 0)
            return 0;
        if (g.pos[1] < 219.0f) {
            fprintf(stderr, "level smoke: boxes: on the ledge y %.5f\n", g.pos[1]);
            fail("the player did not step onto the upper ledge (route f640: y 219.26)");
            return 0;
        }
        fprintf(stderr, "level smoke: boxes: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", g.pos[0], g.pos[1],
                g.pos[2], g.yaw);
        return 1;
    }
    default:
        return 0;
    }
}

/* --------------------------------------------------------------- slide
 *
 * Route beat 06 (route_capture.py beat_hill_slide): from the upper ledge
 * the stick points at (240, 312) at full deflection until within 1.5, then
 * at (262, 356) without a stop. The floor service's apply 00175CF0 meets
 * the hill's authored class-0x1000 grid nodes and 001796C0 enters the
 * slope slide (+5 = 0x1C, +1F0 = 0x30; route f72 at (251.1, 207.1,
 * 328.1)); 0016C6A0 runs it with clips 0x5E / 0x61 while the stick stays
 * on (262, 356). When +1F0 leaves 0x30 (route f138, on the low ground at
 * y 185.0) the stick is released; the skid-out (clips 0x60 / 0x65) hands
 * back to state 0 (f181) and the player idles at (265.7, 185.3, 373.7).
 * In process: the slide is entered, its action ends on the low ground and
 * control returns there; tools/test_level_smoke.py check_slide compares
 * the slide with the capture row for row. */
enum { SLIDE_ENTRY_LIMIT = 300, SLIDE_LIMIT = 400 };

static void slide_begin(void)
{
    nav_reset();
}

static int slide_frame(void)
{
    const EmPlayerLiveActor *a = player_states_actor();
    uint8_t state = em_live_u8(a, 5), mode = em_live_u8(a, 0x1F0);
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(219.594f, 305.214f, 0.1f, 0.4f, 1));
    case 1: NAV_STEP(nav_settle(20));
    case 2: NAV_STEP(nav_face(-0.04141f));
    case 3: NAV_STEP(nav_settle(30));
    case 4:
        /* route_capture goto(240, 312, tol 1.5, stop=False). */
        if (nav_stick_toward(240.0f, 312.0f, 1.0f) > 1.5f) {
            if (++t.nav_frames > NAV_LIMIT)
                fail("navigation did not reach the top of the hill");
            return 0;
        }
        nav_reset();
        ++t.step;
        /* fall through: the stick turns to (262, 356) on this frame */
    case 5:
        nav_stick_toward(262.0f, 356.0f, 1.0f);
        if (state == 0x1C && mode == 0x30) {
            t.saw[0] = 1;
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > SLIDE_ENTRY_LIMIT)
            fail("walking down the hill did not enter the slope slide (+5 = 0x1C, +1F0 = 0x30)");
        return 0;
    case 6:
        if (mode == 0x30) {
            nav_stick_toward(262.0f, 356.0f, 1.0f);
            if (++t.nav_frames > SLIDE_LIMIT)
                fail("the slide action did not end");
            return 0;
        }
        pad_apply(0, 0, 0);
        if (state != 0x1C || g.pos[1] > 186.0f) {
            fprintf(stderr, "level smoke: slide: action ended at +5 %u y %.5f\n", state, g.pos[1]);
            fail("the slide action did not end in state 0x1C on the low ground (route f138: y 185.0)");
            return 0;
        }
        nav_reset();
        ++t.step;
        return 0;
    case 7: {
        /* Neutral through the skid-out, the hand-back (route f181) and past
         * the idle return the capture check compares (12 rows). */
        int r = nav_settle(60);
        if (r <= 0)
            return 0;
        if (g.pos[1] > 186.0f) {
            fprintf(stderr, "level smoke: slide: settled at y %.5f\n", g.pos[1]);
            fail("the player did not settle on the low ground (route f181: y 185.28)");
            return 0;
        }
        fprintf(stderr, "level smoke: slide: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", g.pos[0], g.pos[1],
                g.pos[2], g.yaw);
        return 1;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------- truck_preview
 *
 * Route beat 07: route_capture.py's beat_truck_preview. From the slide's
 * end the stick walks the waypoints (280, 392), (300, 400), (328, 412) at
 * full deflection (walk_path: each until within 2.0, or blocked) and is
 * released once the trigger 008251E0 (state 4) has started 0x8292C0: the
 * first frame with 3B8D != 0 (the trigger's +0x04 = 1 is one frame
 * earlier). The script runs 364 frames: 3B8D = 2 with camera byte 1 and the
 * letterbox, the player placed at (327.4, y, 396.7) and turned to 1.97222,
 * the camera shots, then 07/4; the trigger stores D_00810792 = 1 and frees
 * itself. In process: the script's frame, bars and camera byte were seen,
 * the placement and heading are the record's, D_00810792 = 1 and the
 * trigger node is gone. tools/test_level_smoke.py check_truck_preview
 * compares the script window with the capture row for row. */
enum { TRUCK_PREVIEW_LIMIT = 900 };
static const float k_truck_path[3][2] = {{280.0f, 392.0f}, {300.0f, 400.0f}, {328.0f, 412.0f}};

static uint8_t story_792(void)
{
    const uint8_t *b = em_scene_progress_at(em_scene_state(), 0x00810792u, 1);
    return b ? *b : 0xEE;
}

static void truck_preview_begin(void)
{
    nav_reset();
    if (story_792() != 0 || em_scene_bindings_pool_count(0x008251E0u) != 1)
        fail("the truck preview needs D_00810792 = 0 and the trigger node (route beat 07 starts so)");
}

/* walk_path: one waypoint at a time until within 2.0, or blocked (45 frames
 * moving less than 0.3). 1 when the path ends, 0 continue. */
static int truck_walk(void)
{
    if (t.step >= 3)
        return 1;
    const float *wp = k_truck_path[t.step];
    int slot = t.nav_hist_n % 64;
    t.nav_hist[slot][0] = g.pos[0];
    t.nav_hist[slot][1] = g.pos[2];
    ++t.nav_hist_n;
    int blocked = 0;
    if (t.nav_hist_n > 45) {
        int old = (t.nav_hist_n - 1 - 45) % 64;
        blocked = hypotf(g.pos[0] - t.nav_hist[old][0], g.pos[2] - t.nav_hist[old][1]) < 0.3f;
    }
    if (nav_stick_toward(wp[0], wp[1], 1.0f) <= 2.0f || blocked) {
        nav_reset();
        ++t.step;
    }
    return 0;
}

static int truck_preview_frame(void)
{
    const EmSceneState *s = em_scene_state();
    if (s->spad3B8D == 2)
        t.saw[0] = 1;
    if (em_frame_screen_fade()->state == 3 || em_frame_screen_fade()->state == 1)
        t.saw[1] = 1;
    if (g.cam.top_mode == 1)
        t.saw[2] = 1;
    if (t.step < 4) {
        if (s->spad3B8D != 0) {
            pad_apply(0, 0, 0);
            nav_reset();
            t.step = 4;
            return 0;
        }
        if (++t.nav_frames > TRUCK_PREVIEW_LIMIT) {
            fail("the walk did not reach the truck trigger's band (3B8D stayed 0)");
            return 0;
        }
        int n = t.nav_frames;
        (void)truck_walk();
        t.nav_frames = n;
        if (t.step >= 3)
            fail("the walk ended without the trigger starting its script");
        return 0;
    }
    if (t.step == 4) {
        pad_apply(0, 0, 0);
        if (story_792() != 1 || !in_control()) {
            if (++t.nav_frames > TRUCK_PREVIEW_LIMIT)
                fail("the truck preview script did not end with D_00810792 = 1 and control");
            return 0;
        }
        if (!t.saw[0] || !t.saw[1] || !t.saw[2]) {
            fail("the truck preview did not open the scripted frame (3B8D 2) with the letterbox and camera "
                 "byte 1");
            return 0;
        }
        ++t.step;
        nav_reset();
        return 0;
    }
    int r = nav_settle(30);
    if (r <= 0)
        return 0;
    /* 0x8292C0's 01/1 placement and 04/8 heading (FIRST_LEVEL_ROUTE.md 07). */
    if (fabsf(g.pos[0] - 327.4f) > 1e-3f || fabsf(g.pos[2] - 396.7f) > 1e-3f || fabsf(g.yaw - 1.97222f) > 1e-4f ||
        em_scene_bindings_pool_count(0x008251E0u) != 0) {
        fprintf(stderr, "level smoke: truck_preview: player (%.4f, %.4f) yaw %.5f trigger nodes %d\n", g.pos[0],
                g.pos[2], g.yaw, em_scene_bindings_pool_count(0x008251E0u));
        fail("the preview did not leave the player at the script's placement, or the trigger did not free itself");
        return 0;
    }
    fprintf(stderr, "level smoke: truck_preview: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f story792=%u\n", g.pos[0],
            g.pos[1], g.pos[2], g.yaw, story_792());
    return 1;
}

/* ------------------------------------------------------ truck_crossing
 *
 * Route beat 08: route_capture.py's beat_truck_crossing. The stick walks
 * (345, 390) then (365, 368) (walk_path, tolerance 2.0, a blocked waypoint
 * is skipped after 45 frames), is released, and the run waits for
 * D_00810792 = 0xFF and settles. Standing on the truck (the player's
 * +0x214 = the truck record, its +0x0D = 9, player +0x0A != 0) arms it
 * (00823FF0 state 4): 001B1E20(0, 0), the 47-tick shake, then the 119-beat
 * fall (state 1) and state 2 with D_00810792 = 0xFF. In process: the player
 * stood on the truck record, the truck armed, fell and stored 0xFF, the pad
 * block's rumble ran, and the player is back on the low ground north of the
 * pit (route f172: y 184.84). tools/test_level_smoke.py check_truck_crossing
 * compares the truck record with the capture row for row from the arm. */
enum { TRUCK_CROSSING_LIMIT = 900 };
static const float k_crossing_path[2][2] = {{345.0f, 390.0f}, {365.0f, 368.0f}};

static void truck_crossing_begin(void)
{
    nav_reset();
    if (story_792() != 1 || em_scene_bindings_pool_count(0x00823FF0u) != 1)
        fail("the truck crossing needs D_00810792 = 1 and the truck node (route beat 08 starts so)");
}

static int truck_crossing_frame(void)
{
    const EmPlayerLiveActor *a = player_states_actor();
    uint32_t record;
    uint8_t head[16], t2dc[20];
    float truck_pos[3];
    int truck = em_area11_boxes_truck_state(&record, head, truck_pos, t2dc);
    if (truck && a->link_owner && em_scene_bindings_pool_address(a->link_owner) == record)
        t.saw[0] = 1;                                    /* stood on the truck */
    if (truck && t2dc[16] != 0)
        t.saw[1] = 1;                                    /* +0x2EC: armed */
    if (truck && head[4] == 1)
        t.saw[2] = 1;                                    /* state 1: falling */
    if (em_pad_actuator_block()[0x16])
        t.saw[3] = 1;                                    /* 001B61C0 ran */
    if (t.step < 2) {
        const float *wp = k_crossing_path[t.step];
        int slot = t.nav_hist_n % 64;
        t.nav_hist[slot][0] = g.pos[0];
        t.nav_hist[slot][1] = g.pos[2];
        ++t.nav_hist_n;
        int blocked = 0;
        if (t.nav_hist_n > 45) {
            int old = (t.nav_hist_n - 1 - 45) % 64;
            blocked = hypotf(g.pos[0] - t.nav_hist[old][0], g.pos[2] - t.nav_hist[old][1]) < 0.3f;
        }
        if (nav_stick_toward(wp[0], wp[1], 1.0f) <= 2.0f || blocked) {
            t.nav_hist_n = 0;
            ++t.step;
        }
        if (++t.nav_frames > TRUCK_CROSSING_LIMIT)
            fail("the walk across the truck did not end");
        return 0;
    }
    if (t.step == 2) {
        pad_apply(0, 0, 0);
        if (story_792() != 0xFF) {
            if (++t.nav_frames > TRUCK_CROSSING_LIMIT)
                fail("the truck did not fall (D_00810792 never 0xFF)");
            return 0;
        }
        ++t.step;
        nav_reset();
        return 0;
    }
    int r = nav_settle(30);
    if (r <= 0)
        return 0;
    /* TRUCK_ORIGINAL.md: a whole set piece spawns 32 effects (12 in the
     * shake, 20 in the fall), counted at the gap (census L26). */
    if (em_area11_boxes_effect_spawns() != 32) {
        fprintf(stderr, "level smoke: truck_crossing: %u effect spawns\n", em_area11_boxes_effect_spawns());
        fail("the truck did not spawn the set piece's 32 effects");
        return 0;
    }
    if (!t.saw[0] || !t.saw[1] || !t.saw[2] || !t.saw[3] || !truck || head[4] != 2) {
        fprintf(stderr, "level smoke: truck_crossing: stood %u armed %u fell %u rumble %u state %d\n", t.saw[0],
                t.saw[1], t.saw[2], t.saw[3], truck ? head[4] : -1);
        fail("the player did not arm the truck from its top, or the truck did not shake, fall and rest in state 2");
        return 0;
    }
    /* Step I's countdown 001B5B70 stopped the rumbles through 001B6250: the
     * pad block's active byte +0x16 and duration +0x28 are 0 again, as in
     * route 08's end snapshot (eeMemory at f239). */
    const uint8_t *pad = em_pad_actuator_block();
    if (pad[0x16] != 0 || pad[0x28] != 0 || pad[0x29] != 0) {
        fail("the truck's rumble was not stopped by the step-I countdown (pad block +0x16 / +0x28)");
        return 0;
    }
    if (g.pos[1] > 186.0f || g.pos[1] < 184.0f || g.pos[2] > 385.0f) {
        fprintf(stderr, "level smoke: truck_crossing: player (%.3f, %.5f, %.3f)\n", g.pos[0], g.pos[1], g.pos[2]);
        fail("the player is not on the low ground north of the pit (route f172: y 184.84)");
        return 0;
    }
    fprintf(stderr, "level smoke: truck_crossing: PASS player=(%.3f,%.5f,%.3f) truck_y=%.5f story792=%u\n",
            g.pos[0], g.pos[1], g.pos[2], truck_pos[1], story_792());
    return 1;
}

/* -------------------------------------------------------- cage_ladders
 *
 * Route beat 10's two climbs (route_capture.py beat_cage_roof_roger up to
 * the roof): from the truck crossing's end the stick walks (360, 320) then
 * (360, 296) (walk_path, tolerance 1.0), settles, goes to (360, 293.5) at
 * 0.4 stick, settles, faces pi (face() settles 10 frames after the turn)
 * and presses Cross: 00160220 -> 0015D4C0 case 0x32 on the column's
 * authored node enters the ladder (+5 = 0xB, +1F0 = 0x15, clip 0xE3; route
 * f268); once 001662D0 has taken over (+1F0 = 0x17) the stick is held up
 * (the climb, clips 0xE8 / 0xEA, +3 y per cycle; the dismount +1F0 = 0x18
 * with clip 0xF0) until +1F0 leaves the ladder's actions; the player stands
 * on the cage floor (route f577: y 225.374). Then (359.8, 262) at 0.5
 * stick, face pi, Cross and the same climb to the roof (route f780..f1089:
 * y 264.912). In process: both presses entered state 0xB, the climbs
 * reached 0x17 and 0x18 and ended at the route's heights.
 * tools/test_level_smoke.py check_cage_ladders compares both climbs with
 * the capture row for row. */
enum { LADDER_LIMIT = 900 };
static const float k_cage_path[2][2] = {{360.0f, 320.0f}, {360.0f, 296.0f}};

/* walk_path over `path` (count waypoints): each until within `tol`, or
 * blocked (45 frames moving less than 0.3). 1 when the path ends, 0
 * continue, -1 failed. The waypoint index is t.saw[6]. */
static int walk_path(const float (*path)[2], int count, float tol)
{
    if (t.saw[6] >= count) {
        pad_apply(0, 0, 0);
        t.saw[6] = 0;
        nav_reset();
        return 1;
    }
    const float *wp = path[t.saw[6]];
    int slot = t.nav_hist_n % 64;
    t.nav_hist[slot][0] = g.pos[0];
    t.nav_hist[slot][1] = g.pos[2];
    ++t.nav_hist_n;
    int blocked = 0;
    if (t.nav_hist_n > 45) {
        int old = (t.nav_hist_n - 1 - 45) % 64;
        blocked = hypotf(g.pos[0] - t.nav_hist[old][0], g.pos[2] - t.nav_hist[old][1]) < 0.3f;
    }
    if (nav_stick_toward(wp[0], wp[1], 1.0f) <= tol || blocked) {
        t.nav_hist_n = 0;
        ++t.saw[6];
    }
    if (++t.nav_frames > NAV_LIMIT) {
        fail("the walk did not end");
        return -1;
    }
    return 0;
}

/* route_capture ladder(): after the press, wait for 001662D0 (+1F0 0x17),
 * then hold the stick up until +1F0 leaves 0x15 / 0x17 / 0x18. t.saw[slot]
 * records 0xB seen (bit 0), 0x17 (bit 1) and 0x18 (bit 2). */
static int ladder_climb(int slot)
{
    const EmPlayerLiveActor *a = player_states_actor();
    uint8_t state = em_live_u8(a, 5), mode = em_live_u8(a, 0x1F0);
    if (state == 0xB && mode == 0x15)
        t.saw[slot] |= 1;
    if (mode == 0x17)
        t.saw[slot] |= 2;
    if (mode == 0x18)
        t.saw[slot] |= 4;
    if (++t.nav_frames > LADDER_LIMIT) {
        fail(t.saw[slot] & 1 ? "the ladder climb did not end" : "Cross at the ladder did not enter state 0xB");
        return -1;
    }
    if (!(t.saw[slot] & 2)) {
        pad_apply(0, 0, 0);
        if (t.nav_frames > 90 && !(t.saw[slot] & 1)) {
            fail("Cross at the ladder did not enter state 0xB (+1F0 0x15)");
            return -1;
        }
        return 0;
    }
    /* The capture's stick reached the game two frames after the row that
     * showed 0x17 (the pad latency, FIRST_LEVEL_ROUTE.md section 6: route
     * 10 clip 0xE6 through f330, the climb clip from f331); the overlay's
     * reaches it on the next frame, so the stick waits two frames more. */
    if (t.saw[slot + 2] < 2) {
        ++t.saw[slot + 2];
        pad_apply(0, 0, 0);
        return 0;
    }
    if (mode == 0x15 || mode == 0x17 || mode == 0x18) {
        pad_apply(0, 0, -1.0f);
        return 0;
    }
    pad_apply(0, 0, 0);
    nav_reset();
    if (t.saw[slot] != 7) {
        fail("the ladder did not run entry 0xB, climb 0x17 and dismount 0x18");
        return -1;
    }
    return 1;
}

static void cage_ladders_begin(void)
{
    nav_reset();
}

static int cage_ladders_frame(void)
{
    switch (t.step) {
    case 0: NAV_STEP(walk_path(k_cage_path, 2, 1.0f));
    case 1: NAV_STEP(nav_settle(5));
    case 2: NAV_STEP(nav_goto(360.0f, 293.5f, 0.5f, 0.4f, 1));
    case 3: NAV_STEP(nav_settle(5));
    case 4: NAV_STEP(nav_face(3.14159265f));
    case 5: NAV_STEP(nav_settle(10));
    case 6: NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 7: NAV_STEP(ladder_climb(0));
    case 8:
        if (fabsf(g.pos[1] - 225.374f) > 0.001f) {
            fprintf(stderr, "level smoke: cage_ladders: after ladder A y %.5f\n", g.pos[1]);
            fail("ladder A did not end on the cage floor (route f577: y 225.374)");
            return 0;
        }
        ++t.step;
        return 0;
    case 9: NAV_STEP(nav_goto(359.8f, 262.0f, 0.6f, 0.5f, 1));
    case 10: NAV_STEP(nav_settle(5));
    case 11: NAV_STEP(nav_face(3.14159265f));
    case 12: NAV_STEP(nav_settle(10));
    case 13: NAV_STEP(nav_press(EM_PAD_CROSS, 2));
    case 14: NAV_STEP(ladder_climb(1));
    case 15:
        if (fabsf(g.pos[1] - 264.912f) > 0.001f) {
            fprintf(stderr, "level smoke: cage_ladders: after ladder B y %.5f\n", g.pos[1]);
            fail("ladder B did not end on the cage roof (route f1089: y 264.912)");
            return 0;
        }
        fprintf(stderr, "level smoke: cage_ladders: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", g.pos[0],
                g.pos[1], g.pos[2], g.yaw);
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------- the director's beats
 *
 * cage_roof, crevice_prompt and east_tower are the director 008253F0's
 * beats 0, 1 and 2 (FIRST_LEVEL_ROUTE.md section 5): scripts 0x8294C0,
 * 0x829A40 and 0x829CC0 on the original owner (em_director_original over
 * em_area11_script_host, live since WP-8b), and in beat 10 Roger's
 * alternate script 0x828990 with its voiced line 0x7F; beats 1 and 2 show
 * the voiced lines 0x97 and 0x99. Each beat starts on its own when the
 * previous phase leaves the player inside its quad. The pad stays neutral
 * until the beat has stored its step byte D_00810813 (beat 0: the
 * director's 0x10, then Roger's ordinary branch's 0x11 on the next frame,
 * route 10 f3508 / f3509; beats 1 and 2: 0x20 and 0xFF) and control is
 * back, then settles 90 frames (route_capture settle(); each of the three
 * captures ends 60 rows after its release, and a port whose voiced line
 * tore down early releases early: tools/test_level_smoke.py
 * check_director_beat), so the capture checks compare the release and the
 * follow camera to the capture's end. None of the three
 * scripts moves the player (routes 10 f1089..f3508, 11 f706..f1200 and 13
 * f531..f749 keep +B0..+B8). */
enum { DIRECTOR_LIMIT = 6000, DIRECTOR_SETTLE = 90 };

static uint8_t director_step(void)
{
    const uint8_t *b = em_scene_progress_at(em_scene_state(), 0x00810813u, 1);
    return b ? *b : 0xEE;
}

static int director_beat(uint8_t want)
{
    pad_apply(0, 0, 0);
    if (!t.saw[0]) {
        if (director_step() != want || !in_control()) {
            if (++t.nav_frames > DIRECTOR_LIMIT)
                fail("the director's beat did not end with its step byte and control");
            return 0;
        }
        t.saw[0] = 1;
        nav_reset();
    }
    int r = nav_settle(DIRECTOR_SETTLE);
    if (r <= 0)
        return 0;
    fprintf(stderr, "level smoke: %s: PASS D_00810813 = 0x%02X player=(%.3f,%.5f,%.3f)\n",
            k_phases[t.current].name, director_step(), g.pos[0], g.pos[1], g.pos[2]);
    return 1;
}

static void director_begin(void) { nav_reset(); }
static int cage_roof_frame(void) { return director_beat(0x11); }
static int crevice_prompt_frame(void) { return director_beat(0x20); }
static int east_tower_frame(void) { return director_beat(0xFF); }

/* The ledge climbs of beats 11 and 13: with `fine` the stick first walks
 * to the route's stance before the press at 0.4 stick (navigation input,
 * within 0.1, as the boxes do); without it the preceding walk's stop is
 * the stance, as in route_capture.py (the pipe end: a walk into the pipe's
 * end face stops beside it, and a second approach there slides along the
 * face). Then the player faces the route's heading at the press and
 * settles 30 frames. `land`: the climb ends where the director's beat
 * takes over (routes 11 f706 and 13 f531: 3B8D = 3 on the landing row),
 * so the step passes when +5 leaves 2 instead of waiting for control. */
static int climb_landed(int index)
{
    uint8_t state = em_live_u8(player_states_actor(), 5);
    if (state == 2)
        t.saw[index] = 1;
    if (t.saw[index] && state != 2) {
        nav_reset();
        return 1;
    }
    if (++t.nav_frames > 200) {
        fail(t.saw[index] ? "the ledge climb did not land" : "Cross did not enter the ledge climb (+5 = 2)");
        return -1;
    }
    return 0;
}

static int stance_climb(int step, const float stance[3], int fine, int slot, int land, float top,
                        const char *what)
{
    switch (step) {
    case 0: return fine ? nav_goto(stance[0], stance[1], 0.1f, 0.4f, 1) : 1;
    case 1: return nav_settle(20);
    case 2: return nav_face(stance[2]);
    case 3: return nav_settle(30);
    case 4: return nav_press(EM_PAD_CROSS, 2);
    case 5: return land ? climb_landed(slot) : boxes_climb(slot);
    case 6:
        if (fabsf(g.pos[1] - top) > 0.001f) {
            fprintf(stderr, "level smoke: %s: after the climb y %.5f\n", k_phases[t.current].name, g.pos[1]);
            fail(what);
            return -1;
        }
        return 1;
    default:
        return 1;
    }
}

/* ------------------------------------------------------ crevice_climbs
 *
 * Route beat 11 up to director beat 1 (route_capture.py beat_crevice_prompt):
 * the stick walks (385, 238), (407, 240) east over the bridge (tolerance
 * 1.0); the tank climb from the route's stance (405.283, 240.018), facing
 * 1.5637 (f169): the ledge climb (state 2, +1F0 8, clips 0x70 / 0x79 /
 * 0x8C) onto the tank top at y 286.09 (f264). Then the pipes: the
 * waypoints (420, 262) .. (470, 292) (tolerance 1.5; the short drop at f498
 * is the fall 5 / 0xB and its landing 8 / 0xF; the extra waypoint
 * (470, 300) makes the port's last leg straight down -z, as the
 * original's was), and the pipe-end climb from where that walk stops
 * (route (470.039, 289.876)), facing -3.0327 (f639; clips
 * 0x70 / 0x77 / 0x8C) onto y 279.9 (f705). In process: both presses entered
 * the ledge climb and ended at the route's heights. tools/test_level_smoke.py
 * check_crevice_climbs compares both climbs with the capture. */
static const float k_bridge_path[2][2] = {{385.0f, 238.0f}, {407.0f, 240.0f}};
static const float k_pipe_path[12][2] = {{420.0f, 262.0f}, {416.0f, 270.0f}, {412.0f, 276.0f}, {405.0f, 285.0f},
                                         {401.0f, 300.0f}, {410.0f, 312.0f}, {430.0f, 330.0f}, {450.0f, 347.0f},
                                         {462.0f, 355.0f}, {470.0f, 340.0f}, {470.0f, 300.0f}, {470.0f, 292.0f}};
static const float k_tank_stance[3] = {405.283f, 240.018f, 1.5637f};
static const float k_pipe_stance[3] = {470.039f, 289.876f, -3.0327f};

static void crevice_climbs_begin(void) { nav_reset(); }

static int crevice_climbs_frame(void)
{
    if (t.step == 0) NAV_STEP(walk_path(k_bridge_path, 2, 1.0f));
    if (t.step == 1) NAV_STEP(nav_settle(5));
    if (t.step >= 2 && t.step <= 8)
        NAV_STEP(stance_climb(t.step - 2, k_tank_stance, 1, 0, 0, 286.09f,
                              "the tank climb did not end on the tank top (route f264: y 286.09)"));
    if (t.step == 9) NAV_STEP(nav_settle(10));
    if (t.step == 10) NAV_STEP(walk_path(k_pipe_path, 12, 1.5f));
    if (t.step == 11) NAV_STEP(nav_settle(5));
    if (t.step >= 12 && t.step <= 18)
        NAV_STEP(stance_climb(t.step - 12, k_pipe_stance, 0, 1, 1, 279.9f,
                              "the pipe-end climb did not end on the pipe (route f705: y 279.9)"));
    fprintf(stderr, "level smoke: crevice_climbs: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", g.pos[0], g.pos[1],
            g.pos[2], g.yaw);
    return 1;
}

/* -------------------------------------------------------- crevice_jump
 *
 * Route beat 12 (route_capture.py beat_crevice_jump): the stick walks
 * (485, 275), (477, 262) (tolerance 1.0; the drop off the pipe at f42 is
 * the fall 5 / 0xB), settles 5, faces pi, then runs toward (477, 150) until
 * z <= 249.5, where Cross is held for two frames with the stick kept
 * (f227). 00160220's running-jump probe 0015EC50 enters state 6 (+1F0
 * 0x0C, clips 0x69 / 0x6B; f230) and 001634A0 carries the player across
 * the crevice onto the north block (8 / 0xF, clip 0x6E; landing f277 at
 * (476.4, 269.84, 188.1)). The stick stays on until +1F0 leaves 0x0C and
 * 0x0F, then the pad is released and the player settles. In process: the
 * jump was entered and landed on the north block (z below 210, y
 * 269.84). tools/test_level_smoke.py check_crevice_jump compares the jump
 * with the capture. */
static const float k_jump_path[2][2] = {{485.0f, 275.0f}, {477.0f, 262.0f}};
enum { JUMP_LIMIT = 200 };

static void crevice_jump_begin(void) { nav_reset(); }

static int crevice_jump_frame(void)
{
    const EmPlayerLiveActor *a = player_states_actor();
    uint8_t mode = em_live_u8(a, 0x1F0);
    switch (t.step) {
    case 0: NAV_STEP(walk_path(k_jump_path, 2, 1.0f));
    case 1: NAV_STEP(nav_settle(5));
    case 2: NAV_STEP(nav_face(3.14159265f));
    case 3: NAV_STEP(nav_settle(10));
    case 4:
        if (g.pos[2] > 249.5f) {
            nav_stick_toward(477.0f, 150.0f, 1.0f);
            if (++t.nav_frames > JUMP_LIMIT)
                fail("the run-up did not reach the plateau's edge");
            return 0;
        }
        nav_reset();
        ++t.step;
        /* fall through: Cross with the stick kept */
    case 5:
        pad_apply(EM_PAD_CROSS, t.pad.lx, t.pad.ly);
        if (++t.nav_frames >= 2) {
            nav_reset();
            ++t.step;
        }
        return 0;
    case 6:
        pad_apply(0, t.pad.lx, t.pad.ly);
        if (mode == 0x0C) {
            t.saw[0] = 1;
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > 10)
            fail("Cross at the edge did not enter the running jump (+1F0 0x0C)");
        return 0;
    case 7:
        pad_apply(0, t.pad.lx, t.pad.ly);
        if (mode == 0x0F)
            t.saw[1] = 1;
        if (mode == 0x0C || mode == 0x0F) {
            if (++t.nav_frames > JUMP_LIMIT)
                fail("the running jump did not end");
            return 0;
        }
        pad_apply(0, 0, 0);
        nav_reset();
        ++t.step;
        return 0;
    case 8: {
        int r = nav_settle(30);
        if (r <= 0)
            return 0;
        if (!t.saw[1] || g.pos[2] > 210.0f || fabsf(g.pos[1] - 269.84f) > 0.01f) {
            fprintf(stderr, "level smoke: crevice_jump: landed %u at (%.3f, %.5f, %.3f)\n", t.saw[1], g.pos[0],
                    g.pos[1], g.pos[2]);
            fail("the running jump did not land on the north block (route f277: y 269.84, z 188.1)");
            return 0;
        }
        fprintf(stderr, "level smoke: crevice_jump: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", g.pos[0],
                g.pos[1], g.pos[2], g.yaw);
        return 1;
    }
    default:
        return 0;
    }
}

/* --------------------------------------------------- east_tower_climb
 *
 * Route beat 13 up to director beat 2 (route_capture.py beat_east_tower):
 * the high ledge climb from the route's stance (444.246, 179.776) at the
 * north end of the east tower's east face, facing -1.6104 (f435; clips
 * 0x70 / 0x79 / 0x8C) onto the tower top at y 289.75 (f530). The approach
 * is route_capture's goto(445, 178) at 0.6 stick. tools/test_level_smoke.py
 * check_east_tower_climb compares the climb with the capture. */
static const float k_tower_stance[3] = {444.246f, 179.776f, -1.6104f};

static void east_tower_climb_begin(void) { nav_reset(); }

static int east_tower_climb_frame(void)
{
    if (t.step == 0) NAV_STEP(nav_goto(445.0f, 178.0f, 0.7f, 0.6f, 1));
    if (t.step == 1) NAV_STEP(nav_settle(5));
    if (t.step >= 2 && t.step <= 8)
        NAV_STEP(stance_climb(t.step - 2, k_tower_stance, 1, 0, 1, 289.75f,
                              "the high ledge climb did not end on the east tower top (route f530: y 289.75)"));
    fprintf(stderr, "level smoke: east_tower_climb: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f\n", g.pos[0],
            g.pos[1], g.pos[2], g.yaw);
    return 1;
}

/* ---------------------------------------------------------------- roger
 *
 * Route beat 14 (route_capture.py beat_roger_encounter): goto(436, 190) at
 * 0.6 stick (tolerance 0.8, a stop counts), settle 5, face -pi/2 (and settle
 * 10, as face() does), then run toward (300, 190) at full stick until
 * x <= 411.5 (f238), Cross for two frames with the stick kept; the stick
 * stays on until +1F0 = 0x0C (the running jump, f241) and on until Roger's
 * script block names 0x8283D0 (Roger 008237E0's ordinary branch: the
 * player crossed into his quad 0x82AB80 in mid-air, f283), then neutral
 * until the scripted frame opens (3B8D != 0) and until control is back
 * (3B8D = 0, +1F0 = 0; f1758), then settle 60. In process: the script ran,
 * the counter D_008107D8 holds bit 0 (0x823AB0) and the player stands at
 * the script's 01/9 placement (338, 289.75, 192), heading -2.531.
 * tools/test_level_smoke.py check_roger compares the encounter with the
 * capture. */
enum { ROGER_RUN_LIMIT = 200, ROGER_ENCOUNTER_LIMIT = 6000 };

static uint32_t roger_script_pc(void)
{
    uint32_t record;
    uint8_t header[16], block[16];
    float position[3];
    if (!em_area11_roger_state(&record, header, position, block))
        return 0;
    uint32_t pc;
    memcpy(&pc, block + 8, 4);
    return pc;
}

static void roger_begin(void) { nav_reset(); }

static int roger_frame(void)
{
    const EmPlayerLiveActor *a = player_states_actor();
    uint8_t mode = em_live_u8(a, 0x1F0);
    switch (t.step) {
    case 0: NAV_STEP(nav_goto(436.0f, 190.0f, 0.8f, 0.6f, 1));
    case 1: NAV_STEP(nav_settle(5));
    case 2: NAV_STEP(nav_face(-1.5707963f));
    case 3: NAV_STEP(nav_settle(10));
    case 4:
        if (g.pos[0] > 411.5f) {
            nav_stick_toward(300.0f, 190.0f, 1.0f);
            if (++t.nav_frames > ROGER_RUN_LIMIT)
                fail("the run-up did not reach x 411.5");
            return 0;
        }
        nav_reset();
        ++t.step;
        /* fall through: Cross with the stick kept */
    case 5:
        pad_apply(EM_PAD_CROSS, t.pad.lx, t.pad.ly);
        if (++t.nav_frames >= 2) {
            nav_reset();
            ++t.step;
        }
        return 0;
    case 6:
        pad_apply(0, t.pad.lx, t.pad.ly);
        if (mode == 0x0C) {
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > 10)
            fail("Cross at the edge did not enter the running jump (+1F0 0x0C)");
        return 0;
    case 7:
        pad_apply(0, t.pad.lx, t.pad.ly);
        if (roger_script_pc() == 0x008283D0u) {
            pad_apply(0, 0, 0);
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > 120)
            fail("Roger's quad 0x82AB80 did not start script 0x8283D0");
        return 0;
    case 8:
        pad_apply(0, 0, 0);
        if (em_scene_state()->spad3B8D != 0) {
            t.saw[0] = 1;
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > 60)
            fail("the encounter did not open its scripted frame (3B8D)");
        return 0;
    case 9:
        pad_apply(0, 0, 0);
        if (em_scene_state()->spad3B8D == 0 && mode == 0 && in_control()) {
            nav_reset();
            ++t.step;
            return 0;
        }
        if (++t.nav_frames > ROGER_ENCOUNTER_LIMIT)
            fail("control did not return after the encounter");
        return 0;
    case 10: {
        int r = nav_settle(60);
        if (r <= 0)
            return 0;
        const uint8_t *story = em_scene_progress_at(em_scene_state(), 0x008107D8u, 1);
        if (!story || !(*story & 1) || fabsf(g.pos[0] - 338.0f) > 0.01f || fabsf(g.pos[2] - 192.0f) > 0.01f ||
            fabsf(g.pos[1] - 289.75f) > 0.01f || fabsf(g.yaw - -2.5307274f) > 0.001f) {
            fprintf(stderr, "level smoke: roger: story %u at (%.3f, %.5f, %.3f) yaw %.5f\n", story ? *story : 0xEEu,
                    g.pos[0], g.pos[1], g.pos[2], g.yaw);
            fail("the encounter did not end at the script's placement with D_008107D8 bit 0 (route f1758)");
            return 0;
        }
        fprintf(stderr, "level smoke: roger: PASS player=(%.3f,%.5f,%.3f) yaw=%.5f story=%u\n", g.pos[0],
                g.pos[1], g.pos[2], g.yaw, *story);
        return 1;
    }
    default:
        return 0;
    }
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

void em_level_smoke_test_scene_stopped(void)
{
    if (!t.active || t.failed)
        return;
    const EmSceneFault *fault = &em_scene_state()->fault;
    char reason[96];
    snprintf(reason, sizeof reason, "the scene coordinator faulted at %08X (code %d); the game task is stopped",
             (unsigned)fault->address, (int)fault->code);
    fail(reason);
}

int em_level_smoke_test_active(void) {return t.active;}
int em_level_smoke_test_failed(void) {return t.failed;}
