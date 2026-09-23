/* em_game_selftest.c — env-gated gameplay self-test scripts.
 *
 * Moved unchanged out of em_game.c in step S5 of
 * docs/SCENE_COORDINATOR_DESIGN.md (tools/split_module.py): every
 * EM_*_TEST script of the gameplay frame, and (in
 * em_game_selftest_pre_frame) the head of gameplay_frame that ran them and
 * the EM_CAPTURE_* input injections. Port-only instrumentation; no
 * original function corresponds to anything here. Behaviour is unchanged
 * by the move — only the file boundary is new.
 *
 * Every function here reads the shared gameplay state (EmGameState g), so
 * this module takes the subsystem's internal header rather than owning
 * private state — the same single state block the engine keeps in its
 * gameplay globals, now viewed from one more file. */

#include "game/em_game.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "em_gfx.h"
#include "em_input.h"
#include "em_math.h"
#include "em_model.h"
#include "game/em_bgm.h"
#include "game/em_collision.h"
#include "game/em_door.h"
#include "game/em_enemy.h"
#include "game/em_examine.h"
#include "game/em_frame.h"
#include "game/em_hud.h"
#include "game/em_pickup.h"
#include "game/em_sfx.h"
#include "game/em_task.h"
#include "game/em_truck.h"
#include "game/em_weapon.h"
#include "game/em_game_internal.h"
#include "game/em_effect_color.h"
#include "game/em_random.h"
#include "game/em_director.h"
#include "game/em_player.h"
#include "game/em_player_damage.h"
#include "game/em_camera.h"
#include "game/em_scene.h"
#include "game/em_props.h"
#include "game/em_opening_runtime.h"
#include "game/em_opening_actor.h"
#include "game/em_snow_runtime.h"
#include "game/em_area11_effect_runtime.h"
#include "game/em_opening_control_test.h"

/* EM_MOVE_TEST=1 — deterministic movement self-test. Injects a scripted
 * key sequence through the real em_input event API (an injected event
 * lands in the NEXT frame's input snapshot, exactly like a real key, so
 * the test exercises the full step-C unpack path): 'w' held for frames
 * 1..60 (walk forward, +Z at cam_yaw 0), 'd' for frames 61..90 (walk
 * screen-right, -X), then assert the final placement and quit.
 *
 * Keyboard full push = GAIT 3 (RUN, 48 u/s = 0.8 u/frame sustained —
 * the tier-ramp-corrected mapping, ANALOG GAIT above; the entry ramps
 * 0.3 -> 0.8 over 8 frames = 4.65 u, then 0.8/frame). The office
 * collision world has a wall n-gon at z = -170 (grid poly, plane
 * n = (0,0,-1), d = 170 — 14 u ahead of the spawn), so with collision
 * loaded the RADIAL WALL PROBES rest the player near the engine's
 * 4.5-unit standoff. The strafe leg then CURVES on TWO coupled counts:
 *   (1) the BODY-HEADING EASE (func_00174AC0/func_001B12B0): the leg-1
 *       -> leg-2 stick flip is a +90 deg desired-heading jump, but the
 *       body heading g.yaw turns into it at the engine's banded rate
 *       (run far-band 10.5 deg/f) and VELOCITY IS EMITTED ALONG g.yaw,
 *       so the path arcs over ~9 frames instead of sliding instantly
 *       off the raw stick — this is the user-reported fix;
 *   (2) the s65 WALK-STATE CAMERA: the heading basis (cam+0x44) is an
 *       OUTPUT of the eye tether — strafing rotates the camera bearing
 *       as the dragged eye trails the path, so the camera-relative
 *       DESIRED heading itself keeps swinging (the engine's emergent
 *       chase-camera spiral). The two compound: the final yaw overshoots
 *       -pi/2 to ~-1.93 because the rotating camera bearing pushes the
 *       desired heading further around while the body chases it.
 * Deterministic endpoints (no RNG in the camera or mover).
 * RE-PINNED + RE-ARMED 2026-06-17 (BYTE-FAITHFUL movement restore): the
 * desired heading is now the disasm-exact closed form
 * atan2(sx,-sy) + player_move_cam_yaw() + pi/2 (= wrap(stickAngle + pi +
 * D_008106A0); INVESTIGATION_movement_exact.md "FORWARD-DIRECTION
 * RESOLUTION (definitive, static)"). That is +90 deg off the prior
 * X-mirrored door-regression form, so both legs walk a new heading and
 * the default office scene (spawn 107.4,0,-184 yaw 0, office.emcl, legs
 * 60/30) settles at a NEW deterministic wall-stop endpoint pos
 * (107.404, 0.000, -205.266) yaw -3.1416 [wall stop] — captured from a
 * clean post-restore run and verified IDENTICAL across two consecutive
 * runs. This is the pinned collision-world baseline; the assertion is
 * ARMED (the default office run asserts strictly — a movement regression
 * FAILS it). Those built-in expectations (and the 60/30-frame legs) are
 * the OFFICE scene's; for other scenes (manifest spawns) EM_MOVE_LEGS=
 * fwd,strafe resizes the two legs to reach that scene's wall and
 * EM_MOVE_EXPECT=x,y,z + EM_MOVE_YAW=<rad> override the expected final
 * placement. */
void move_test_inject(int key, int down)
{
    EmEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.type = down ? EM_EVENT_KEY_DOWN : EM_EVENT_KEY_UP;
    ev.key  = key;
    em_input_handle_event(&ev);
}

static void move_test_script(void)
{
    int n = g.frame_no;
    if (n == 0) {
        move_test_inject('w', 1);
    } else if (n == g.move_legs[0]) {
        move_test_inject('w', 0);
        move_test_inject('d', 1);
    } else if (n == g.move_legs[0] + g.move_legs[1]) {
        move_test_inject('d', 0);
    } else if (n == g.move_legs[0] + g.move_legs[1] + 1) {
        /* RE-PINNED + RE-ARMED 2026-06-17 (BYTE-FAITHFUL movement restore,
         * INVESTIGATION_movement_exact.md "FORWARD-DIRECTION RESOLUTION
         * (definitive, static)"). The desired-heading basis was restored to
         * the disasm-exact closed form
         *     desired = atan2(sx,-sy) + player_move_cam_yaw() + pi/2
         * (= wrap(stickAngle + pi + D_008106A0), D_008106A0 =
         * atan2(-fwd.z,fwd.x)). This is +90 deg off the prior X-mirrored
         * "door-regression" form (player_move_cam_yaw() + atan2(-sx,-sy)),
         * so BOTH move-test legs walk a new heading: the office spawn
         * (107.4,0,-184, yaw 0, office.emcl, legs 60/30) now walks 'w' -X-
         * relative-to-the-+90 basis then 'd', settling at the deterministic
         * wall-stop endpoint pinned below — captured from a clean post-
         * restore run and verified IDENTICAL across two consecutive runs
         * (107.404, 0.000, -205.266) yaw -3.1416. FIX-2 (tier-0 ramp) and
         * FIX-3 (multi-frame turn-in-place) are untouched; only the
         * desired-heading basis changed, which is what moved this endpoint.
         * The collision-world default ASSERTS strictly against this pin (a
         * movement regression FAILS loudly — it is NOT rebaseline-always-
         * pass). The bbox (no-collision) branch is not reached by any
         * default scene (the office always loads office.emcl), so its
         * literals stay a soft report-only reference. */
        float ex   = g.coll.poly_count ?  107.404f :   86.025f;
        float ey   = g.coll.poly_count ?    0.000f :    0.000f;
        float ez   = g.coll.poly_count ? -205.266f : -135.887f;
        float eyaw = g.coll.poly_count ?  -3.1416f :  -1.9032f;
        if (g.move_expect_set) {
            ex = g.move_expect[0];
            ey = g.move_expect[1];
            ez = g.move_expect[2];
        }
        {
            const char *my = getenv("EM_MOVE_YAW");
            if (my) eyaw = (float)atof(my);
        }
        const float tol  = 0.05f;  /* +- slide/fp drift allowance */
        /* RE-ARMED 2026-06-17: the collision-world default (the office
         * scene, which always loads office.emcl -> poly_count > 0) and any
         * explicit EM_MOVE_EXPECT override ASSERT strictly against the
         * pinned endpoint above — a movement regression FAILS the test
         * loudly. Only the bbox (no-collision) branch, which no default
         * scene reaches, stays report-only (its literals are stale). */
        int report_only = (g.coll.poly_count == 0) && !g.move_expect_set;
        int ok = report_only ? 1 :
                 (fabsf(g.pos[0] - ex)           <= tol &&
                  fabsf(g.pos[1] - ey)           <= tol &&
                  fabsf(g.pos[2] - ez)           <= tol &&
                  fabsf(g.yaw - eyaw)            <= 0.01f);
        printf("move test: pos (%.3f, %.3f, %.3f) yaw %.4f rad — "
               "expected (%.3f, %.3f, %.3f)%s%s: %s\n",
               g.pos[0], g.pos[1], g.pos[2], g.yaw, ex, ey, ez,
               g.coll.poly_count ? " [wall stop]" : " [bbox clamp]",
               report_only ? " [bbox report-only baseline]" : "",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* EM_DOOR_TEST=1 — deterministic door-transit self-test. Spawns the
 * player on the z = -225 corridor line facing the WEST double door
 * (hinge/placement at (57, 0, -220.5), record [5] AREA02 state 1;
 * DOORWAY CENTER = hinge + 5 along the panel = (57, 0, -225.5) — the
 * decoded func_00183EF0 kind-5 reference point, 2026-06-11).
 * CITATION TIGHTENED (audit 2026-07-31): that 5-unit offset is real
 * but it is NOT the `+8 == 5` arm. src/func_00183EF0.c (NEARMISS)
 * puts it in the `+8 == 0` arm, under `kind == 5 && (sub == 3 ||
 * sub == 0x15)`, as `ref = (obj.x - 5*cos(yaw), obj.z + 5*sin(yaw))`,
 * range-tested against rec[0] with |dy| <= rec[1] from the +0x30
 * tuning record. The `+8 == 5` arm is a plain 14-u radius test
 * (`dx*dx + dz*dz <= 196.0f`) with |dy| <= 4.0f and NO panel offset.)
 * Exercises the FULL s22 transit sequence end to end through the real
 * input API:
 *
 *   frames  1..24   run -X (full push = gait 3; the tier ramp tops
 *                   out at 0.8 u/frame) to the boundary-wall standoff
 *                   x ~= 64.5 (inside the 10 u use-scan radius measured
 *                   from the CENTER; no button — the door must stay
 *                   CLOSED. Engine-true twice over: the kind-5 scan
 *                   has no auto ring, and the scan itself only runs on
 *                   the USE press edge — there is NO walk-into door
 *                   trigger at all, s58)
 *   frames 29..58   keep running -X. The doorway is statically SEALED by
 *                   the grid room-boundary plane at x = 60 (the engine's
 *                   sealed-room-box world): the radial wall probes must
 *                   BLOCK free movement at the player's 4.5-unit radius,
 *                   x ~= 64.5 (tracked as dt_min_x while CLOSED) — the
 *                   "previously blocked plane".
 *   frame   60      CROSS press — the engine trigger (the use scan runs
 *                   on the press edge D_00810E74 & spad-3B76 = 0x0040,
 *                   s58) -> arms the door (dist to
 *                   the center ~7.5; facing -X = the back-side pi/4 yaw
 *                   gate passes at 0); assert state == OPENING soon
 *                   after. The kickoff LOCKS input, latches the side
 *                   (the test approach is the BACK side: bearing(player
 *                   - door) is pi off the door yaw) and WALKS the player
 *                   to the staging point (62, 0, -225.5) = CENTER + 5*n
 *                   on his own side — the s22 live capture's EXACT point
 *                   (arrival ~frame 80; the walk keeps the locomotion
 *                   walk anim).
 *   ~frame  80      OPEN phase: the door script chain fires on arrival
 *                   — scripted player anim 0x43 (door-open BACK, rate
 *                   1.0; after the 2026-06-11 clip-directory fix this IS
 *                   the open clip — reach, push, walk-through — the old
 *                   asset shipped the LOCKED try under this id) replaces
 *                   locomotion, door sound + clip start; the script
 *                   waits 70 frames (back-side op 0x02).
 *   frame  100      assert em_door_movement_locked() == 1 (mid-transit)
 *                   AND em_game_anim_active() == 0x43;
 *   100..140        hold forward ('w') — locked input must NOT move the
 *                   player off the staging point (checked at 145).
 *   ~frame 150      commit (arrival ~80 + 70-frame wait) -> 64-frame
 *                   fade-out (peak level tracked); at black (~205):
 *                   re-place at the spawn point (52, 0, -225.5) =
 *                   CENTER - 5*n, exit yaw -pi/2, scripted anim
 *                   CANCELLED (script teardown), and the ARRIVAL
 *                   WALK-OUT starts (player state 5/1: 50 frames
 *                   clip-in-place + 30 at 0.3 u/tick + 30 decaying,
 *                   ~13.1 u along -X); door closes; 64-frame fade-in
 *                   (done ~269). MENU lock ends at fade-in completion;
 *                   MOVEMENT lock at walk-out end (~317) — the decoded
 *                   two-lock split (em_door.h "THE TWO LOCKS").
 *   frame  290      assert MID-WALK-OUT: menu lock OFF, movement lock
 *                   ON, player mid-flight (39.3 < x < 51.5) — the split
 *                   witnessed live.
 *   frame  340      assert: trigger OK, blocked min x >= 64.4 (the
 *                   boundary plane + the 4.5 player radius), lock
 *                   seen, locked input dead, scripted anim seen at 100
 *                   and idle (0) again at the end, fade reached 1.0 and
 *                   is back at 0, MOVEMENT unlocked (walk-out over),
 *                   final pos = spawn - walk-out travel (38.89,
 *                   -225.5) +- tol with yaw -pi/2 (walked out of the
 *                   door, exit pose), door re-armed CLOSED.
 *
 * (Geometry verified against the office EMCL: corridor floor along the
 * whole approach, boundary wall at x = 60, no other static blocker.) */
static void door_test_script(void)
{
    static int s_dt_ok_split;        /* frame-290 two-lock witness */
    int n = g.frame_no;
    if (n == 0) {
        s_dt_ok_split     = 0;
        g.dt_door         = -1;
        g.dt_min_x        = 1e9f;
        g.dt_max_fade     = 0.0f;
        g.dt_ok_trigger   = 0;
        g.dt_ok_lock      = 0;
        g.dt_ok_inputdead = 0;
        g.dt_ok_anim      = 0;
        /* WALK-UP under the BYTE-FAITHFUL movement basis (restored 2026-06-17,
         * INVESTIGATION_movement_exact.md "FORWARD-DIRECTION RESOLUTION").
         * Spawn is (72,0,-225) facing -X (yaw -pi/2); the door is straight
         * -X. The chase camera sits behind the player looking -X, so the
         * move basis player_move_cam_yaw() = atan2(fwd.x,fwd.z) ~= -pi/2.
         * The faithful desired = atan2(sx,-sy) + cam_yaw + pi/2, so to head
         * -X (desired = -pi/2) we need atan2(sx,-sy) = -pi/2, i.e. the LEFT
         * stick (sx=-1, sy=0) = the 'a' key — NOT 'w'. (Under the faithful
         * +90 deg rotation, forward-press 'w' walks 90 deg off the camera
         * look; 'a' = "screen-into-the-look", which is straight at the door
         * here.) The camera stays looking -X as the player walks straight
         * down the corridor, so 'a' holds desired = -pi/2 self-consistently
         * and the player reaches the doorway boundary, exactly as the old
         * 'w' did under the (wrong) pre-faithful basis. The scripted door
         * CROSSING itself ignores the stick (selector-3 0x70003B8D gate),
         * so only this walk-up input needed the basis correction. */
        move_test_inject('a', 1);
    } else if (n == 24) {
        move_test_inject('a', 0);
        /* the test door = nearest instance to the west doorway */
        float best = 1e30f;
        for (int i = 0; i < em_door_count(); i++) {
            float p[3];
            em_door_pos(i, p);
            float dx = p[0] - 57.0f, dz = p[2] + 220.5f;
            float d2 = dx * dx + dz * dz;
            if (d2 < best) { best = d2; g.dt_door = i; }
        }
    } else if (n == 28) {
        move_test_inject('a', 1);   /* push -X into the sealed doorway
                                     * (faithful basis: 'a' = at the door) */
    } else if (n == 58) {
        move_test_inject('a', 0);   /* ~6 frames pinned on the boundary */
    } else if (n == 60) {
        move_test_inject('k', 1);   /* CROSS — the use-scan trigger */
    } else if (n == 61) {
        move_test_inject('k', 0);
    } else if (n == 64) {
        g.dt_ok_trigger = g.dt_door >= 0 &&
                          em_door_state(g.dt_door) == EM_DOOR_OPENING;
    } else if (n == 100) {
        g.dt_ok_lock = em_door_movement_locked();
        /* Mid-open: the scripted door anim must own the player — the
         * BACK-side clip 0x43 committed (+0x20C analog), not the walk. */
        g.dt_ok_anim = em_game_anim_active() == 0x43;
        move_test_inject('w', 1);   /* locked: must be ignored */
    } else if (n == 140) {
        move_test_inject('w', 0);
    } else if (n == 145) {
        /* 40 frames of held forward under the lock: still parked on the
         * staging point (62, 0, -225.5) — CENTER + 5*n, the s22
         * live-captured value. */
        g.dt_ok_inputdead = fabsf(g.pos[0] - 62.0f)   <= 0.35f &&
                            fabsf(g.pos[2] + 225.5f) <= 0.35f;
        if (!g.dt_ok_inputdead)
            printf("door test: frame 145 pos (%.3f, %.3f, %.3f) — off "
                   "the staging point\n", g.pos[0], g.pos[1], g.pos[2]);
    } else if (n == 290) {
        /* MID-WALK-OUT (re-place ~205, fade-in done ~269, walk-out ends
         * ~317): the decoded TWO-LOCK SPLIT — the menu gate is already
         * open while movement is still script-owned and the player is
         * mid-flight between the spawn (52) and the stop (~38.9). */
        s_dt_ok_split = !em_door_menu_locked() &&
                        em_door_movement_locked() &&
                        g.pos[0] < 51.5f && g.pos[0] > 39.3f;
        if (!s_dt_ok_split)
            printf("door test: frame 290 menu_locked %d movement_locked "
                   "%d x %.3f — two-lock split wrong\n",
                   em_door_menu_locked(), em_door_movement_locked(),
                   g.pos[0]);
    } else if (n == 340) {
        int ok_closed = g.dt_door >= 0 &&
                        em_door_state(g.dt_door) == EM_DOOR_CLOSED;
        int ok_block  = g.dt_min_x >= 64.4f && g.dt_min_x < 65.2f;
        int ok_fade   = g.dt_max_fade >= 0.999f &&
                        em_frame_fade_level() <= 0.001f;
        int ok_unlock = !em_door_movement_locked();
        int ok_animend = em_game_anim_active() == 0;  /* teardown reset */
        /* Final pos = the spawn point (52, -225.5) MINUS the arrival
         * WALK-OUT travel (~13.1 u along the exit yaw -X — the decoded
         * player state 5/1 walk-out, em_door.h "ARRIVAL WALK-OUT"). */
        int ok_pass   = fabsf(g.pos[0] - 38.89f)  <= 0.35f &&
                        fabsf(g.pos[2] + 225.5f)  <= 0.6f &&
                        fabsf(g.yaw + EM_PI * 0.5f) <= 0.01f;
        int ok = g.dt_ok_trigger && g.dt_ok_lock && g.dt_ok_inputdead &&
                 g.dt_ok_anim && ok_animend && s_dt_ok_split &&
                 ok_fade && ok_unlock && ok_pass && ok_block && ok_closed;
        printf("door test: trigger->OPENING %s, blocked min x %.3f while "
               "closed (boundary 60 + radius 4.5: %s), input locked "
               "mid-transit %s, "
               "locked stick ignored %s, scripted anim 0x43 mid-open %s / "
               "reset at end %s, two-lock split mid-walk-out %s, fade "
               "peak %.3f / final %.3f: %s, "
               "movement unlocked at end %s, final pos (%.3f, %.3f, %.3f) "
               "yaw %.4f walked out of the door: %s, door state %d "
               "(CLOSED %d, re-armed): %s — %s\n",
               g.dt_ok_trigger ? "ok" : "FAILED", g.dt_min_x,
               ok_block ? "ok" : "FAILED",
               g.dt_ok_lock ? "ok" : "FAILED",
               g.dt_ok_inputdead ? "ok" : "FAILED",
               g.dt_ok_anim ? "ok" : "FAILED",
               ok_animend ? "ok" : "FAILED",
               s_dt_ok_split ? "ok" : "FAILED",
               g.dt_max_fade, em_frame_fade_level(),
               ok_fade ? "ok" : "FAILED",
               ok_unlock ? "ok" : "FAILED",
               g.pos[0], g.pos[1], g.pos[2], g.yaw,
               ok_pass ? "ok" : "FAILED",
               g.dt_door >= 0 ? em_door_state(g.dt_door) : -1,
               EM_DOOR_CLOSED, ok_closed ? "ok" : "FAILED",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
    /* Track how far -X free movement reaches while the door is CLOSED
     * (the boundary plane must hold the player at ~60.01), and the fade
     * peak (the fade-out must reach full black before the re-place). */
    if (n > 0 && n <= 60 && g.dt_door >= 0 &&
        em_door_state(g.dt_door) == EM_DOOR_CLOSED &&
        g.pos[0] < g.dt_min_x)
        g.dt_min_x = g.pos[0];
    if (em_frame_fade_level() > g.dt_max_fade)
        g.dt_max_fade = em_frame_fade_level();
}

/* EM_TRANSIT_TEST=1 — deterministic SCENE-SWITCH transit self-test.
 * Exercises the goto-door path end to end: the office scene's west door
 * (hinge (57, 0, -220.5), doorway CENTER (57, 0, -225.5)) carries the
 * manifest goto tail written by the decomp repo's export_level.py
 * --door-goto — since 2026-06-11 the REAL decoded destination: door id
 * 1|0x80 -> AREA01 sub 0 entry 5 = the DRAWBRIDGE ROOM (chunk05.n0,
 * exported as assets/scene_drawbridge; the old flagged synthetic
 * west<->m15 link is gone). Arrival spawn = the real AREA01 spawn
 * record (39, 0, -225) yaw -pi/2 — the engine's own west-door arrival.
 *
 *   frame    0      spawn (66, 0, -225.5) facing -X — on the doorway
 *                   center line, dist 9 of the 10-u scan radius
 *   frame    5      CROSS -> use scan arms the goto door; kickoff locks
 *                   input, walks to staging (62, -225.5) = CENTER + 5*n
 *                   (the s22 live-captured point), open script (back
 *                   side: anim 0x43, 70-frame wait), commit, 64-frame
 *                   fade-out
 *   at black        em_door posts the GOTO; em_game_scene_switch frees
 *                   the office sub-1 scene and loads scene_drawbridge
 *                   (11 zone parts, drawbridge.emcl, its 6 doors, its
 *                   enemies); player placed at (39, 0, -225) yaw -pi/2;
 *                   fade-in
 *   frame  380      assert: trigger OK, locked mid-transit, the ACTIVE
 *                   SCENE changed to assets/scene_drawbridge (switch
 *                   frame recorded), player at the arrival spawn, fade
 *                   peaked 1.0 and back at 0, input unlocked, the NEW
 *                   scene's 6 doors present + CLOSED, collision =
 *                   drawbridge.emcl (loaded), enemies of the new scene
 *                   populated, and the player model still owns its
 *                   palette (persists).
 */
static void transit_test_script(void)
{
    int n = g.frame_no;
    if (n == 0) {
        g.tt_door         = -1;
        g.tt_ok_trigger   = 0;
        g.tt_ok_lock      = 0;
        g.tt_switch_frame = 0;
        g.tt_max_fade     = 0.0f;
    } else if (n == 3) {
        /* the goto door = nearest instance to the west doorway */
        float best = 1e30f;
        for (int i = 0; i < em_door_count(); i++) {
            float p[3];
            em_door_pos(i, p);
            float dx = p[0] - 57.0f, dz = p[2] + 220.5f;
            float d2 = dx * dx + dz * dz;
            if (d2 < best) { best = d2; g.tt_door = i; }
        }
    } else if (n == 5) {
        move_test_inject('k', 1);   /* CROSS — the use-scan trigger */
    } else if (n == 7) {
        move_test_inject('k', 0);
    } else if (n == 12) {
        g.tt_ok_trigger = g.tt_door >= 0 &&
                          em_door_state(g.tt_door) == EM_DOOR_OPENING;
    } else if (n == 150) {
        g.tt_ok_lock = em_door_movement_locked();   /* mid fade-out */
    } else if (n == 380) {
        int ok_scene  = g.tt_switch_frame > 0 &&
                        strcmp(g.scene_dir, "assets/scene_drawbridge") == 0;
        /* Arrival spawn (39, -225) MINUS the ~13.1 u WALK-OUT travel
         * along the exit yaw -X (the walk-out state survives the goto
         * scene switch, like the engine's player state — em_door.h). */
        int ok_pos    = fabsf(g.pos[0] - 25.89f) <= 0.35f &&
                        fabsf(g.pos[1])          <= 0.6f &&
                        fabsf(g.pos[2] + 225.0f) <= 0.1f &&
                        fabsf(g.yaw + EM_PI * 0.5f) <= 0.01f;
        int ok_fade   = g.tt_max_fade >= 0.999f &&
                        em_frame_fade_level() <= 0.001f;
        int ok_unlock = !em_door_movement_locked();
        int closed    = 0;
        for (int i = 0; i < em_door_count(); i++)
            closed += em_door_state(i) == EM_DOOR_CLOSED;
        int ok_doors  = em_door_count() == 6 && closed == 6;
        int ok_coll   = g.coll.poly_count > 0 &&
                        strstr(g.coll_path, "drawbridge.emcl") != NULL;
        int ok_enemy  = em_enemy_count() >= 1;
        int ok_player = g.mesh != NULL;
        int ok = g.tt_ok_trigger && g.tt_ok_lock && ok_scene && ok_pos &&
                 ok_fade && ok_unlock && ok_doors && ok_coll && ok_enemy &&
                 ok_player;
        printf("transit test: trigger->OPENING %s, locked mid-transit %s, "
               "scene switched at frame %d -> %s: %s, player (%.3f, %.3f, "
               "%.3f) yaw %.4f at the arrival spawn: %s, fade peak %.3f / "
               "final %.3f: %s, unlocked %s, new scene doors %d (closed "
               "%d): %s, collision %s (%u polys): %s, enemies %d: %s, "
               "player model kept: %s — %s\n",
               g.tt_ok_trigger ? "ok" : "FAILED",
               g.tt_ok_lock ? "ok" : "FAILED",
               g.tt_switch_frame, g.scene_dir,
               ok_scene ? "ok" : "FAILED",
               g.pos[0], g.pos[1], g.pos[2], g.yaw,
               ok_pos ? "ok" : "FAILED",
               g.tt_max_fade, em_frame_fade_level(),
               ok_fade ? "ok" : "FAILED",
               ok_unlock ? "ok" : "FAILED",
               em_door_count(), closed, ok_doors ? "ok" : "FAILED",
               g.coll_path, g.coll.poly_count, ok_coll ? "ok" : "FAILED",
               em_enemy_count(), ok_enemy ? "ok" : "FAILED",
               ok_player ? "ok" : "FAILED",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
    if (em_frame_fade_level() > g.tt_max_fade)
        g.tt_max_fade = em_frame_fade_level();
}

/* EM_SLIDER_TEST=1 — deterministic SLIDING-DOOR self-test (run with
 * EM_SCENE=assets/scene_drawbridge). Exercises the m17/m09
 * variant brain func_001BB860 (em_door.h "SLIDERS") on the drawbridge
 * room's intra-room slider door 4 — door_m09 at (128.6, 0, -610) yaw
 * pi/2 (AREA01 sub-0 placement; room-move entries 9/8 flank it at
 * x 143 / 115.5):
 *
 *   frame    0      spawn (112, 0, -610) facing +X, 16.6 u from the
 *                   door; hold 'a' — the player RUNS at the door. (Under
 *                   the BYTE-FAITHFUL move basis, with the chase camera
 *                   directly behind the spawn, 'a' is the "forward into
 *                   the scene" key — desired = atan2(sx,-sy) + cam_yaw +
 *                   pi/2, so 'a' (sx=-1) -> cam_yaw + pi/2 -> facing dir;
 *                   same remap the door test took 2026-06-17. 'w' here
 *                   walks -Z, NOT at the door.)
 *   ~frame  16..17  the player crosses the 10-u scan radius still
 *                   pushing — and must stay UNARMED: the engine has NO
 *                   walk-into door trigger (s58 decode; the s56
 *                   no-button reading is OVERTURNED — the use scan
 *                   runs only on the USE press edge). Asserted at 17.
 *                   PROVENANCE CHECKED (audit 2026-07-31), and NOT
 *                   fully settled: src/func_001BB860.c (NEARMISS) is a
 *                   generic actor state machine — states 0..3 on the
 *                   byte at self+4, sub-states 0..4 on self+5, a
 *                   story-flag gate `D_00810841[D_00810700] &
 *                   (1 << self->s16(+0x34))` for kinds 0x16/0x17/0x3E,
 *                   a virtual tick through self+0x4C, and states 2/3
 *                   handed to func_001AFC10. Nothing in it is
 *                   door- or slider-specific, so it does not by
 *                   itself support "no walk-into trigger". It DOES
 *                   carry a player-PROXIMITY arm the old note never
 *                   mentioned: at the tail it takes the distance from
 *                   the player mirror (D_00810350/54/58) to self+0xB0
 *                   and, `if (d <= 20.0f)`, sets self+1 = 1 and — when
 *                   self+2 & 0x80 — queues the actor via
 *                   func_001B1DE0 (a 32-deep push of self[5] onto the
 *                   D_00275B60 stack). What that latch feeds is NOT
 *                   recovered. The "no walk-into trigger" reading
 *                   still rests on the s58 .s read, not on this file;
 *                   treat it as OBSERVED and re-check it before any
 *                   door-arming change.
 *   frame   18      CROSS press (the D_00810E74-edge use scan, config
 *                   mask spad 3B76 = 0x0040) inside the window ->
 *                   kickoff: back side (bearing -pi/2 vs door yaw
 *                   +pi/2), yaw snap +X,
 *                   walk to the staging point (122.6, -610) =
 *                   door - 6.0 * fwd (the func_001BB560 6.0 constant)
 *   ~frame  30..75  the NATIVE SLIDE pumps (46-frame clip, panels
 *                   part 0.2 u/frame to +-9 u) while the player
 *                   STANDS at staging — no player anim (the slider
 *                   script has none), no fade (op07 sub0)
 *   ~frame  76..    op01-sub8 walk-through: scripted MOVE-TO to the
 *                   mirrored point (134.6, -610); at arrival the
 *                   script ends — locks release, door stays OPEN
 *   frame  150..170 hold 'w' — the player runs on +X out of the scan
 *                   radius (+ the 2-u hysteresis)
 *   frame  240      assert: stick-push alone did NOT arm + the CROSS
 *                   edge DID, mid-slide parked at staging with NO
 *                   scripted player anim and fade 0, walk-through
 *                   landed at (134.6, -610) unlocked, door reclosed
 *                   (reverse clip) after the leave.
 */
static void slider_test_script(void)
{
    static int   st_door, st_ok_noarm, st_ok_arm, st_ok_noanim, st_ok_park;
    static int   st_ok_through, st_ok_unlock;
    static float st_max_fade;
    int n = g.frame_no;
    if (n == 0) {
        st_door = -1;
        st_ok_noarm = st_ok_arm = st_ok_noanim = st_ok_park = 0;
        st_ok_through = st_ok_unlock = 0;
        st_max_fade = 0.0f;
        move_test_inject('a', 1);   /* 'a' = forward at the door (faithful basis) */
    } else if (n == 3) {
        /* the test door = nearest instance to the slider placement */
        float best = 1e30f;
        for (int i = 0; i < em_door_count(); i++) {
            float p[3];
            em_door_pos(i, p);
            float dx = p[0] - 128.6f, dz = p[2] + 610.0f;
            float d2 = dx * dx + dz * dz;
            if (d2 < best) { best = d2; st_door = i; }
        }
    } else if (n == 17) {
        /* NOW inside the 10-u scan window (dist ~9.3), pushing, NO button:
         * must be UNARMED — the engine has no walk-into trigger (s58).
         * CROSS frame RETUNED 2026-06-17 (14 -> 18): under the corrected
         * run-ramp (func_0017BC40, 2026-06-11) and the faithful move basis
         * the player only crosses the 10-u radius at frame ~16, so the old
         * frame-14 press fired ~11 u out and never armed. This assert now
         * proves no-walk-into-trigger from INSIDE the window, where it
         * actually bites. */
        st_ok_noarm = st_door >= 0 &&
                      em_door_state(st_door) == EM_DOOR_CLOSED &&
                      !em_door_movement_locked();
        if (!st_ok_noarm)
            printf("slider test: frame 17 door state %d lock %d — armed "
                   "without the USE press\n",
                   st_door >= 0 ? em_door_state(st_door) : -1,
                   em_door_movement_locked());
    } else if (n == 18) {
        move_test_inject('k', 1);   /* CROSS — the use-scan press edge */
    } else if (n == 19) {
        move_test_inject('k', 0);
    } else if (n == 22) {
        /* armed by the CROSS edge inside the class-5 window */
        st_ok_arm = st_door >= 0 &&
                    em_door_state(st_door) == EM_DOOR_OPENING;
        move_test_inject('a', 0);   /* the script owns the player now */
    } else if (n == 60) {
        /* mid-slide: the player is PARKED at the staging point
         * (122.6, -610) with NO scripted anim — the slider script has
         * no player door gesture (the PCSX2-verified look). */
        st_ok_noanim = em_game_anim_active() == 0 &&
                       em_door_movement_locked();
        st_ok_park   = fabsf(g.pos[0] - 122.6f) <= 0.35f &&
                       fabsf(g.pos[2] + 610.0f) <= 0.35f;
        if (!st_ok_park)
            printf("slider test: frame 60 pos (%.3f, %.3f, %.3f) — off "
                   "the staging point\n", g.pos[0], g.pos[1], g.pos[2]);
    } else if (n == 140) {
        /* walk-through done: player on the far side, free, door OPEN */
        st_ok_through = fabsf(g.pos[0] - 134.6f) <= 0.35f &&
                        fabsf(g.pos[2] + 610.0f) <= 0.35f &&
                        st_door >= 0 &&
                        em_door_state(st_door) == EM_DOOR_OPEN;
        st_ok_unlock  = !em_door_movement_locked() &&
                        !em_door_menu_locked();
        if (!st_ok_through)
            printf("slider test: frame 140 pos (%.3f, %.3f, %.3f) door "
                   "state %d\n", g.pos[0], g.pos[1], g.pos[2],
                   st_door >= 0 ? em_door_state(st_door) : -1);
    } else if (n == 150) {
        move_test_inject('a', 1);   /* run on +X, out of the scan radius */
    } else if (n == 170) {
        move_test_inject('a', 0);
    } else if (n == 240) {
        int ok_closed = st_door >= 0 &&
                        em_door_state(st_door) == EM_DOOR_CLOSED;
        int ok_nofade = st_max_fade <= 0.001f;   /* op07 sub0: NO fade */
        int ok = st_ok_noarm && st_ok_arm && st_ok_noanim && st_ok_park &&
                 st_ok_through && st_ok_unlock && ok_closed && ok_nofade;
        printf("slider test: stick-push alone left it CLOSED %s, CROSS "
               "edge armed %s, mid-slide "
               "parked at staging w/ no player anim %s/%s, walk-through "
               "landed far-side + unlocked %s/%s, fade stayed 0 (%.3f): "
               "%s, door reclosed after leave (state %d): %s — %s\n",
               st_ok_noarm ? "ok" : "FAILED",
               st_ok_arm ? "ok" : "FAILED",
               st_ok_park ? "ok" : "FAILED",
               st_ok_noanim ? "ok" : "FAILED",
               st_ok_through ? "ok" : "FAILED",
               st_ok_unlock ? "ok" : "FAILED",
               st_max_fade, ok_nofade ? "ok" : "FAILED",
               st_door >= 0 ? em_door_state(st_door) : -1,
               ok_closed ? "ok" : "FAILED",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
    if (em_frame_fade_level() > st_max_fade)
        st_max_fade = em_frame_fade_level();
}

/* EM_LOCKED_TEST=1 — deterministic LOCKED-DOOR self-test (run with
 * EM_SCENE=assets/scene_drawbridge). Exercises the full locked
 * sequence (em_door.h "THE LOCKED SEQUENCE") on the drawbridge room's
 * m15 SECURITY DOOR — the REAL lock-gated placement (AREA01 sub-0
 * record [14], model 0x15, door id 1, manifest `locked` token from
 * export_level.py --door-locked) at (-20.5, 0, -192) yaw 0, goto
 * scene_office0:
 *
 *   frame    0      spawn (-25.5, 0, -201) — on the doorway-center
 *                   column (center = hinge + 5 = (-25.5, -192)), BACK
 *                   side, 9 u out, facing the door (+Z)
 *   frame    5      CROSS -> use scan arms; the LOCK GATE refuses
 *                   (unlock bit clear): kickoff mode 1 -> sub 1,
 *                   locks engage, walk-to staging (-25.5, -197)
 *   ~frame  23      arrival: locked-look camera CUT (func_001BBBF0:
 *                   target (-28.5, 10, -192) = door + 8 to the handle
 *                   side + 10 up; eye = target - 13 along the live
 *                   camera yaw 0 -> (-28.5, 12, -205)), player try
 *                   anim 0x44 (back), lock-fixture jiggle clip
 *   ~frame  83      the op-0x02 60-frame wait elapses: rattle 0x3F2 +
 *                   the RADIO/EXAMINE message machine starts on GLOBAL
 *                   line 6 "It's locked and won't open." (op09
 *                   func_001BBAE0 — jtbl sel 0; em_hud_radio)
 *   ~frame 202      the message's 118 display frames + the terminal
 *                   record elapse — the machine reports done
 *   ~frame 223      jiggle clip (200 f) ends (the locked script's op0B
 *                   sub1 gate, AFTER the pumped VO native): finish
 *                   script restores camera + control; door re-arms
 *                   CLOSED — no fade, no warp, player still on the
 *                   near side
 *   frame  255      em_door_unlock() — the panel/keycard event
 *   frame  260      CROSS again -> the gate passes: state OPENING,
 *                   open anim 0x43, 70-frame wait, commit, fade,
 *                   GOTO scene switch to office0, arrival walk-out
 *   frame  560      assert the whole ledger (PASS/FAIL) and quit.
 */
static void locked_test_script(void)
{
    static int   lt_door, lt_ok_refuse, lt_ok_lock, lt_ok_anim;
    static int   lt_ok_cam, lt_ok_rattle, lt_ok_shut, lt_ok_stay;
    static int   lt_ok_restore, lt_ok_retry, lt_ok_openanim, lt_ok_switch;
    static int   lt_ok_radio_run, lt_ok_radio_done;
    static float lt_refuse_fade;
    int n = g.frame_no;
    if (n == 0) {
        lt_door = -1;
        lt_ok_refuse = lt_ok_lock = lt_ok_anim = lt_ok_cam = 0;
        lt_ok_rattle = lt_ok_shut = lt_ok_stay = lt_ok_restore = 0;
        lt_ok_retry = lt_ok_openanim = lt_ok_switch = 0;
        lt_ok_radio_run = lt_ok_radio_done = 0;
        lt_refuse_fade = 0.0f;
    } else if (n == 3) {
        /* the test door = nearest instance to the m15 placement */
        float best = 1e30f;
        for (int i = 0; i < em_door_count(); i++) {
            float p[3];
            em_door_pos(i, p);
            float dx = p[0] + 20.5f, dz = p[2] + 192.0f;
            float d2 = dx * dx + dz * dz;
            if (d2 < best) { best = d2; lt_door = i; }
        }
        if (lt_door < 0 || !em_door_is_locked(lt_door))
            printf("locked test: door %d is not lock-gated — manifest "
                   "`locked` token missing?\n", lt_door);
    } else if (n == 5) {
        move_test_inject('k', 1);   /* CROSS — the use-scan trigger */
    } else if (n == 6) {
        move_test_inject('k', 0);
    } else if (n == 8) {
        /* the gate REFUSED: locked-try state, both locks engaged */
        lt_ok_refuse = lt_door >= 0 &&
                       em_door_state(lt_door) == EM_DOOR_LOCKED_TRY;
        lt_ok_lock   = em_door_movement_locked() && em_door_menu_locked();
    } else if (n == 45) {
        /* mid-try: the scripted TRY anim owns the player and the
         * locked-look camera is pinned at the handle (the func_001BBBF0
         * geometry — camera cut observed via state). */
        float dp[3], dyaw;
        lt_ok_anim = em_game_anim_active() == 0x44;
        lt_ok_cam  = em_door_locked_look(dp, &dyaw, NULL) &&
                     fabsf(g.cam.tgt[0] + 28.5f)  <= 0.75f &&
                     fabsf(g.cam.tgt[1] - 10.0f)  <= 0.75f &&
                     fabsf(g.cam.tgt[2] + 192.0f) <= 0.75f &&
                     fabsf(g.cam.eye[1] - 12.0f)  <= 0.75f &&
                     fabsf(g.cam.eye[2] + 205.0f) <= 1.5f;
        if (!lt_ok_cam)
            printf("locked test: frame 45 cam eye (%.2f, %.2f, %.2f) "
                   "tgt (%.2f, %.2f, %.2f) — off the locked-look\n",
                   g.cam.eye[0], g.cam.eye[1], g.cam.eye[2],
                   g.cam.tgt[0], g.cam.tgt[1], g.cam.tgt[2]);
    } else if (n == 95) {
        /* the 60-frame mark passed: exactly one rattle 0x3F2, and the
         * radio/examine message machine is PRESENTING line 6 (the op09
         * native started it on the same tick as the rattle) */
        lt_ok_rattle    = em_door_rattles() == 1;
        lt_ok_radio_run = em_hud_radio_active();
    } else if (n == 250) {
        /* refusal over: control restored, door CLOSED and re-armed,
         * NO fade ever ran, NO warp/switch — the player stands at the
         * staging point on his own side, anim torn down. */
        lt_ok_shut    = lt_door >= 0 &&
                        em_door_state(lt_door) == EM_DOOR_CLOSED &&
                        lt_refuse_fade <= 0.001f;
        /* Near side held: parked at the staging point (-197.25 — the
         * MOVE-TO 0.3-u arrival window), then the control restore lets
         * the free-move wall separation push the 4.5-u player radius
         * off the closed door hull (~ -198.5). Never past the door
         * plane (-192). */
        lt_ok_stay    = fabsf(g.pos[0] + 25.5f) <= 0.35f &&
                        g.pos[2] <= -196.0f && g.pos[2] >= -199.5f;
        lt_ok_restore = !em_door_movement_locked() &&
                        !em_door_menu_locked() &&
                        em_game_anim_active() == 0;
        /* the message machine completed its full presentation: no
         * longer active, and the text record showed exactly 118
         * frames (the GLOBAL table's line-6 duration) */
        lt_ok_radio_done = !em_hud_radio_active() &&
                           em_hud_radio_frames() == 118;
        if (!lt_ok_radio_done)
            printf("locked test: frame 250 radio active %d frames %d — "
                   "expected done after 118\n",
                   em_hud_radio_active(), em_hud_radio_frames());
        if (!lt_ok_stay)
            printf("locked test: frame 250 pos (%.3f, %.3f, %.3f) — "
                   "moved through a locked door\n",
                   g.pos[0], g.pos[1], g.pos[2]);
    } else if (n == 255) {
        em_door_unlock(lt_door);    /* the panel/keycard unlock event */
    } else if (n == 260) {
        move_test_inject('k', 1);   /* retry */
    } else if (n == 261) {
        move_test_inject('k', 0);
    } else if (n == 264) {
        lt_ok_retry = lt_door >= 0 &&
                      em_door_state(lt_door) == EM_DOOR_OPENING;
    } else if (n == 300) {
        /* the OPEN script owns the player now (back-side clip 0x43) */
        lt_ok_openanim = em_game_anim_active() == 0x43 &&
                         em_door_movement_locked();
    } else if (n == 560) {
        int ok_unlocked = !em_door_movement_locked() &&
                          !em_door_menu_locked();
        int ok_fade     = em_frame_fade_level() <= 0.001f;
        int ok = lt_ok_refuse && lt_ok_lock && lt_ok_anim && lt_ok_cam &&
                 lt_ok_rattle && lt_ok_radio_run && lt_ok_radio_done &&
                 lt_ok_shut && lt_ok_stay &&
                 lt_ok_restore && lt_ok_retry && lt_ok_openanim &&
                 lt_ok_switch && ok_unlocked && ok_fade;
        printf("locked test: refusal->LOCKED_TRY %s, locks engaged %s, "
               "try anim 0x44 %s, locked-look cam %s, rattle 0x3F2 x1 "
               "%s, radio line 6 presenting %s, radio done @118 f %s, "
               "door shut + no fade %s, pos held near side %s, "
               "control restored %s, unlock+retry->OPENING %s, open "
               "anim 0x43 %s, goto switch to office0 %s, final "
               "unlocked %s fade %.3f %s — %s\n",
               lt_ok_refuse ? "ok" : "FAILED",
               lt_ok_lock ? "ok" : "FAILED",
               lt_ok_anim ? "ok" : "FAILED",
               lt_ok_cam ? "ok" : "FAILED",
               lt_ok_rattle ? "ok" : "FAILED",
               lt_ok_radio_run ? "ok" : "FAILED",
               lt_ok_radio_done ? "ok" : "FAILED",
               lt_ok_shut ? "ok" : "FAILED",
               lt_ok_stay ? "ok" : "FAILED",
               lt_ok_restore ? "ok" : "FAILED",
               lt_ok_retry ? "ok" : "FAILED",
               lt_ok_openanim ? "ok" : "FAILED",
               lt_ok_switch ? "ok" : "FAILED",
               ok_unlocked ? "ok" : "FAILED",
               em_frame_fade_level(), ok_fade ? "ok" : "FAILED",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
    if (getenv("EM_LOCKED_TRACE") && n % 5 == 0 && n < 260)
        printf("LT f%d pos (%.3f, %.3f) door %d state %d transit_lock %d\n",
               n, g.pos[0], g.pos[2], lt_door,
               lt_door >= 0 ? em_door_state(lt_door) : -1,
               em_door_movement_locked());
    /* The refusal must never fade (the locked script has no commit);
     * track the fade only until the unlock. */
    if (n > 0 && n < 255 && em_frame_fade_level() > lt_refuse_fade)
        lt_refuse_fade = em_frame_fade_level();
    /* The goto switch lands when the active scene dir changes. */
    if (!lt_ok_switch && strstr(g.scene_dir, "office0"))
        lt_ok_switch = 1;
}

/* EM_WEAPON_TEST=1 — deterministic firing-loop self-test (em_weapon.c).
 * Exercises the whole weapon contract through the real input API from
 * the demo ammo state mag 4 / reserve 120 (the live test save).
 *
 * The schedule is computed at frame 0 from the weapon's HONEST state
 * windows (em_weapon_draw_ticks/reload_ticks/holster_ticks — the real
 * anim-clip lengths when the player EMDL carries them, the flagged
 * fallbacks otherwise), anchored at A/B/C/D:
 *
 *   frame    0       hold E (R1) — draw (anim 0x110 @1.4)
 *   A = draw+5       AIM reached; four SEMI presses of L (CIRCLE — the
 *   A/A+14/A+28/A+42 engine's default-config fire button, s29) at
 *                    14-frame spacing — PAST the engine's 25-frame
 *                    ladder cadence (+0x2F4 = the 0x112 clip length,
 *                    func_0017A8B0; shots space >= 13 ticks), so each
 *                    press fires 1:1: mag AND reserve decrement
 *                    (TOTAL-pool rule) -> 4/120 .. 0/116
 *   A+55             the 4th shot's cadence EXPIRY auto-reloads the
 *                    empty mag (mode 1 — no extra press needed:
 *                    mag = min(30, 116) = 30, reserve UNTOUCHED at
 *                    116; anim 0x33 holds the RELOAD state)
 *   B = A+55+rld+8   press after the reload window: 5th real shot
 *                    -> 29/115
 *   B+16..B+47       FULL-AUTO stretch (after the semi cadence
 *                    expires): mode 2, L held 31 frames -> shots at
 *                    the 6-frame in-burst cadence (the 0x1E state's
 *                    +0x2F4 = 12.0 store) = 6 rounds -> 23/109
 *   C = B+66         manual top-up (R = L3, the engine's raw reload
 *                    bit; honored only in WAIT) -> 30/109, reserve
 *                    again untouched
 *   D = C+rld+24     RECOIL-SETTLE check: by D the post-reload aim hold
 *                    has been quiet for > ceil(25/2) ticks, so the 0x112
 *                    playhead must be CLAMPED at the last frame (the
 *                    +24 window exists for this — the recoil replay
 *                    needs 12.5 ticks to settle); then release E ->
 *                    holster (anim 0x111) -> HOLSTERED checked one
 *                    holster window later
 *
 * RECOIL checks (s25): a shot rewinds the held 0x112 clip to frame 0
 * at 2 frames/tick (em_game_anim_hold_restart — the engine's fire
 * counter re-seed, FINDINGS "FIRE ANIM MECHANISM"); the A+6 and B+6
 * checkpoints additionally assert the playhead is MID-REPLAY (0 <
 * frame < last) while the committed clip stays 0x112, and D asserts
 * the clamp. Skipped when the player EMDL carries no clip 0x112 (the
 * clip-less fallback build).
 *
 * Checkpoints sample a few frames after each action so the input-inject
 * latency (events land in the NEXT frame's snapshot) and the fire-event
 * one-frame latency are absorbed. */

/* Recoil introspection helper: 1 = the committed scripted clip is the
 * aim pose 0x112 with its playhead mid-replay (a shot rewound it and
 * it has not yet clamped); 2 = committed 0x112 clamped at the last
 * frame (the settled hold); 0 = anything else. -1 = the EMDL carries
 * no 0x112 (recoil checks skipped honestly). */
static int wt_recoil_state(void)
{
    int frames = em_game_anim_frames(0x112);
    if (frames <= 0) return -1;
    if (em_game_anim_active() != 0x112) return 0;
    int f = em_game_anim_frame();
    if (f <= 0) return 0;              /* not yet evaluated / at start */
    return f < frames - 1 ? 1 : 2;
}

static void wt_check(int cond, const char *what)
{
    if (cond) return;
    g.wt_fail++;
    printf("weapon test: CHECK FAILED — %s (state %d, mag %u, reserve %d, "
           "shots %d, reloads %d)\n", what, em_weapon_state(),
           em_weapon_mag(), em_weapon_reserve(), em_weapon_shots(),
           em_weapon_reloads());
}

static void weapon_test_script(void)
{
    static int A, B, C, D;        /* checkpoint anchors (see the doc) */
    int n = g.frame_no;
    if (n == 0) {
        A = em_weapon_draw_ticks() + 5;
        B = A + 55 + em_weapon_reload_ticks() + 8;
        C = B + 66;     /* past the auto leg + its final cadence */
        D = C + em_weapon_reload_ticks() + 24;  /* +24: recoil-settle
                                                 * window (12.5 ticks
                                                 * to clamp) */
        printf("weapon test: windows draw %d / reload %d / holster %d "
               "ticks -> anchors A=%d B=%d C=%d D=%d\n",
               em_weapon_draw_ticks(), em_weapon_reload_ticks(),
               em_weapon_holster_ticks(), A, B, C, D);
        move_test_inject('e', 1);                           /* R1 hold  */
        return;
    }
    if (n == A) {
        wt_check(em_weapon_state() == EM_WPN_AIM, "drawn by A");
        move_test_inject('l', 1);                           /* semi #1  */
    } else if (n == A + 2 || n == A + 16 || n == A + 30 || n == A + 44) {
        move_test_inject('l', 0);
    } else if (n == A + 6) {
        wt_check(em_weapon_mag() == 3 &&
                 em_weapon_reserve() == 119 &&
                 em_weapon_shots() == 1,
                 "shot 1: 4/120 -> 3/119");
        int rs = wt_recoil_state();
        wt_check(rs == 1 || rs == -1,
                 "recoil replay mid-flight after shot 1 (0x112 rewound)");
    } else if (n == A + 14 || n == A + 28 || n == A + 42) {
        move_test_inject('l', 1);                           /* semi 2..4 */
    } else if (n == A + 48) {
        wt_check(em_weapon_mag() == 0 &&
                 em_weapon_reserve() == 116 &&
                 em_weapon_shots() == 4,
                 "shots 2-4 at the 14-frame spacing: mag empty at "
                 "0/116");
    } else if (n == A + 60) {
        /* the 4th shot's cadence expiry (shot ~A+43, +12 ticks)
         * auto-reloaded the dry mag — mode 1, NO press required */
        wt_check(em_weapon_state() == EM_WPN_RELOAD &&
                 em_weapon_mag() == 30 &&
                 em_weapon_reserve() == 116 &&
                 em_weapon_shots() == 4 &&
                 em_weapon_reloads() == 1,
                 "dry mag auto-reloads at the cadence expiry: mag = "
                 "min(30, reserve) = 30, reserve untouched (116), no "
                 "round fired");
    } else if (n == B) {
        wt_check(em_weapon_state() == EM_WPN_AIM,
                 "reload anim over by B");
        move_test_inject('l', 1);                           /* semi #5  */
    } else if (n == B + 2) {
        move_test_inject('l', 0);
    } else if (n == B + 6) {
        wt_check(em_weapon_mag() == 29 &&
                 em_weapon_reserve() == 115 &&
                 em_weapon_shots() == 5,
                 "5th shot after reload: 29/115");
        int rs = wt_recoil_state();
        wt_check(rs == 1 || rs == -1,
                 "recoil replay mid-flight after shot 5");
    } else if (n == B + 16) {
        /* the 5th (semi) shot's 25-frame cadence has expired by here
         * (shot ~B+1, expiry ~B+13) — the auto hold starts from WAIT */
        em_weapon_set_fire_mode(EM_WPN_MODE_AUTO);
        move_test_inject('l', 1);                           /* auto hold */
    } else if (n == B + 47) {
        move_test_inject('l', 0);
    } else if (n == B + 54) {
        wt_check(em_weapon_mag() == 23 &&
                 em_weapon_reserve() == 109 &&
                 em_weapon_shots() == 11,
                 "auto 31-frame hold = 6 rounds: 23/109");
    } else if (n == C) {
        move_test_inject('2', 1);                           /* manual L3 */
    } else if (n == C + 2) {
        move_test_inject('2', 0);
    } else if (n == C + 8) {
        wt_check(em_weapon_state() == EM_WPN_RELOAD &&
                 em_weapon_mag() == 30 &&
                 em_weapon_reserve() == 109 &&
                 em_weapon_reloads() == 2,
                 "manual top-up: 30/109, reserve untouched");
    } else if (n == D) {
        int rs = wt_recoil_state();
        wt_check(rs == 2 || rs == -1,
                 "0x112 settled back into the clamped hold by D");
        move_test_inject('e', 0);                           /* holster   */
    } else if (n == D + em_weapon_holster_ticks() + 6) {
        wt_check(em_weapon_state() == EM_WPN_HOLSTERED,
                 "holstered after R1 release");
        printf("weapon test: %d shots, %d reloads, mag %u, reserve "
               "%d, last ray %s — %s\n", em_weapon_shots(),
               em_weapon_reloads(), em_weapon_mag(),
               em_weapon_reserve(),
               em_weapon_last_hit() < 0 ? "none"
                   : em_weapon_last_hit() ? "hit" : "miss",
               g.wt_fail == 0 ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* EM_ENEMY_TEST — deterministic enemy-vs-player self-tests (em_enemy.c;
 * s62 condition decode + the s66 shootability verdict — the worm is
 * BORN ATTACKING, the crate bursts on DAMAGE ONLY, and the WORM IS
 * NOT SHOOTABLE (both victim filters reject model 0x0D; nothing
 * consumes its mailbox). Scene-init arm (et_spawned) places the
 * target; the scripts adapt to actual pace instead of fixed frames:
 *
 *   EM_ENEMY_TEST=1 (kill run — RESTAGED s68: the burst hatches the
 *     nest-group BUGS now, and bugs ARE victims): spawn a DISGUISED
 *     CRATE 12 u dead ahead (inside the office's wall plane 14 u out,
 *     so the world ray cannot outrank the victim test). Hold R1 from
 *     frame 0 (draw), pitch the aim down until the screen-cone lock
 *     fills on the crate, fire ONE semi shot -> +0x36 mailbox vs the
 *     crate's HP 1 -> assert the ray resolved HIT, the crate burst on
 *     its IDLE poll, and TWO BUGS (the default nest group) hatched at
 *     the crate ring with the variant-A HP 15. Then the SHOOTABLE-BUG
 *     witness (s68 — replaces the old worm pass-through leg; the worm
 *     victim-filter rejection stays witnessed by EM_MELEE_TEST's
 *     whiff leg and EM_ENEMY_TEST=4's mailbox witness): keep R1,
 *     assert the lock RE-FILLS on a hatched bug (func_00183B80 passes
 *     the bug models 0x0F/0x10), fire ONE more shot and assert it
 *     resolved HIT and the locked bug FLINCHED — HP 15 -> 10 (the
 *     every-tick mailbox consumption, bullet amount 5), still alive
 *     and attacking. (s76: the bug brain now bites, so player health is
 *     reported by et_finish, not asserted untouched — see the leg.)
 *   EM_ENEMY_TEST=2 (contact run): ONE WORM 30 u ahead; weapon stays
 *     holstered; let the worm
 *     run its sequence (approach 90 t -> stalk 120 t homing ->
 *     windup 45 t -> lunge). CITATION TIGHTENED (audit 2026-07-31)
 *     against src/func_00154120.c (NEARMISS — the worm ATTACK
 *     sub-machine on the byte at self+5, states 0..3): only the
 *     120-tick homing leg is in the recovered C — state 0 arms
 *     `self->s16(+0x28) = 0x78` on the clip-end bit 0x1000 and state 1
 *     counts it down while easing the heading toward the player mirror
 *     (`+0xC4 = func_001B12B0(aim, +0xC4, 0.0698131695f)` = 4 deg per
 *     frame). The 90 and 45 counts are NOT in it; they rest on
 *     FINDINGS' read of func_00153F10, which is only an all-word stub.
 *     The lunge CONTACT LATCH *is* source-derived: state 3 writes
 *     `D_008102BF = 2; D_008104D4 = 0x41700000` — literally 15.0f —
 *     versus the state-0 brush-contact latch 0x40A00000 = 5.0f. The
 *     worm bursts -> assert health dropped by exactly 15 and the worm
 *     despawned. (With the leech asset the
 *     connect is the decoded neck->head SEGMENT arm — func_0019AA80 on
 *     the two +0xC0 columns — which is the ONLY arm that writes the
 *     latch; rig-less runs fold into the radius-6 contact, and that
 *     fold is a PORT STAND-IN, not source-derived: state 3's
 *     `func_0019A570(..., 6, 0)` else-branch only resets the worm to
 *     +4 = 2 / +5 = 0 and deals NO damage. See em_enemy.c LATCH
 *     SEGMENT — the fold lives there, not here.)
 *   EM_ENEMY_TEST=3 (crate run — RESTAGED s68): spawn a DISGUISED
 *     CRATE 25 units ahead instead (em_enemy.h "CRATE KIND"); weapon
 *     stays holstered, walk forward (W) INTO it. Assert the DISGUISE
 *     HOLDS at point-blank range (the engine has no proximity trigger
 *     — the old ~10-u burst was port-invented), then post a
 *     knife-sized 5 mailbox hit (em_enemy_damage — the same write a
 *     melee impact does; EM_MELEE_TEST covers the real knife path)
 *     and assert the burst hatched TWO BUGS at the crate's hatch ring
 *     (s68 — never a worm) with the variant-A HP 15, the bugs
 *     ENGAGED (the minimal walk brain), then the bug MAILBOX LADDER
 *     on bug slot 1: a nonlethal 5 FLINCHES it (HP 15 -> 10, alive,
 *     the every-tick func_00128B80 consumption), a following 15
 *     KILLS it (handler func_00129FC0; slot freed, corpse fade) while
 *     the other bug lives on. Closing distances are reported, not
 *     asserted (the hatch ring can land inside the approach
 *     standoff). */
static void et_check(int cond, const char *what)
{
    if (cond) return;
    g.et_fail++;
    printf("enemy test: CHECK FAILED — %s\n", what);
}

static float et_dist_i(int i)
{
    float p[3] = { 0.0f, 0.0f, 0.0f };
    em_enemy_pos(i, p);
    float dx = p[0] - g.pos[0], dz = p[2] - g.pos[2];
    return sqrtf(dx * dx + dz * dz);
}

static float et_dist(void)
{
    return et_dist_i(0);
}

static void et_finish(const char *run)
{
    printf("enemy test (%s): spawn dist %.1f, final dist %.1f, shots %d, "
           "crawler alive %d (state %d), health %.0f (start %.0f) — %s\n",
           run, g.et_d0, et_dist(), em_weapon_shots(), em_enemy_alive(),
           em_enemy_state(0), g.status.health, g.et_health0,
           g.et_fail == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    em_frame_request_quit();
}

static void enemy_test_script(void)
{
    int n = g.frame_no;
    if (n == 0) {
        g.et_d0      = et_dist();
        g.et_health0 = g.status.health;
        if (g.enemy_test == 1)
            move_test_inject('e', 1);   /* R1 hold — draw the rifle */
        if (g.enemy_test == 3)
            move_test_inject('w', 1);   /* walk at the crate */
        return;
    }
    if (n == 20 && g.enemy_test == 2)   /* the disguised crates (tests
                                         * 1/3) stay IDLE */
        et_check(em_enemy_state(0) == EM_ENEMY_ATTACK,
                 "worm attacking by frame 20 (born attacking — "
                 "func_00153F10 has no idle state)");

    if (g.enemy_test == 1) {
        /* RESTAGED s68: shot 1 kills the CRATE (HP 1); the burst
         * hatches the nest-group BUGS — which ARE victims, so shot 2
         * is the SHOOTABLE-BUG witness (lock re-fill + flinch). */
        static int second_fired, bug_lock = -1;
        if (!g.et_fired) {
            /* ENGINE-TRUE ACQUISITION (2026-06-11 func_00199220
             * translation): the bullet only bends to a target inside
             * the SCREEN-center cone — the low crate under a level
             * aim sits below it. Do what the player does: pitch the
             * aim DOWN (inverted-Y stick up, 'w') until lock slot 0
             * fills, then fire the locked shot. */
            int locked = em_weapon_lock_target() >= 0;
            if (em_weapon_state() == EM_WPN_AIM)
                move_test_inject('w', !locked);
            if (n > 20 && locked) {
                et_check(em_enemy_state(0) == EM_ENEMY_IDLE,
                         "crate disguise holding at lock time");
                et_check(em_weapon_state() == EM_WPN_AIM,
                         "rifle drawn (AIM) at fire time");
                move_test_inject('w', 0);
                move_test_inject('l', 1);   /* CIRCLE — semi shot 1 */
                g.et_fired = n;
            } else if (n >= 400) {
                et_check(0, "screen-cone lock on the crate by frame "
                            "400");
                et_finish("kill run");
            }
        } else if (n == g.et_fired + 2) {
            move_test_inject('l', 0);
        } else if (n == g.et_fired + 12 && !second_fired) {
            et_check(em_weapon_shots() == 1, "one round after shot 1");
            et_check(em_weapon_last_hit() == 1, "shot 1 resolved HIT");
            et_check(em_enemy_state(0) == EM_ENEMY_FREE,
                     "crate burst on its IDLE mailbox poll (HP 1)");
            et_check(em_enemy_alive() == 2 &&
                     em_enemy_kind(1) == EM_ENEMY_KIND_BUG &&
                     em_enemy_kind(2) == EM_ENEMY_KIND_BUG,
                     "nest-group bugs (2) hatched by the burst (s68)");
            et_check(em_enemy_hp(1) == 15 && em_enemy_hp(2) == 15,
                     "variant-A HP 15 (func_00128390)");
            second_fired = -1;          /* arm the re-lock stage */
        } else if (second_fired == -1) {
            /* the SHOOTABLE-BUG witness (s68): the lock must RE-FILL
             * on a hatched bug (func_00183B80 passes models
             * 0x0F/0x10) — keep pitching like the player does until
             * it does, then fire the locked shot. */
            bug_lock = em_weapon_lock_target();
            if (em_weapon_state() == EM_WPN_AIM)
                move_test_inject('w', bug_lock < 0);
            if (bug_lock >= 0) {
                et_check(em_enemy_kind(bug_lock) == EM_ENEMY_KIND_BUG,
                         "auto-aim lock re-fills on a hatched bug");
                move_test_inject('w', 0);
                move_test_inject('l', 1);   /* CIRCLE — semi shot 2 */
                second_fired = n;
            } else if (n >= g.et_fired + 400) {
                et_check(0, "screen-cone lock on a bug after the "
                            "burst");
                et_finish("kill run");
            }
        } else if (n == second_fired + 2) {
            move_test_inject('l', 0);
        } else if (n == second_fired + 12) {
            et_check(em_weapon_shots() == 2, "exactly two rounds fired");
            et_check(em_weapon_last_hit() == 1, "shot 2 resolved HIT");
            et_check(em_enemy_state(bug_lock) == EM_ENEMY_ATTACK &&
                     em_enemy_hp(bug_lock) == 10,
                     "locked bug FLINCHED — HP 15 -> 10 (every-tick "
                     "mailbox func_00128B80, bullet amount 5)");
            et_check(em_enemy_alive() == 2, "both bugs still alive");
            /* s76: the bug brain now has a real bite lunge, so player
             * health is NO LONGER asserted untouched here — a bug that
             * closes the ~12 u and completes a lunge inside this short
             * witness window CAN bite (BUG_BITE_DMG). The health delta
             * is reported by et_finish; this leg only witnesses
             * shootability + flinch. */
            et_finish("kill run");
        }
    } else if (g.enemy_test == 3) {
        /* crate run, engine-true: NO proximity burst exists — walk to
         * point-blank, assert the disguise HOLDS, then damage it (the
         * decoded only-trigger) and witness the s68 bug hatch + the
         * bug mailbox ladder (flinch below lethal, death at it). */
        static int et_stop_frame, et_hit_sent, et_flinch_sent,
                   et_kill_sent;
        if (!et_stop_frame) {
            if (et_dist() <= 6.0f) {
                et_stop_frame = n;
                move_test_inject('w', 0);    /* stop at the crate */
            } else if (n >= 600) {
                et_check(0, "player reached the crate by frame 600");
                et_finish("crate run");
            }
        } else if (!et_hit_sent) {
            if (n == et_stop_frame + 30) {
                /* 30 frames at point-blank: the decoded state 4 has no
                 * player-distance test — the disguise must hold */
                et_check(em_enemy_alive() == 1 &&
                         em_enemy_state(0) == EM_ENEMY_IDLE,
                         "disguise holds at point-blank range (no "
                         "proximity trigger in the engine)");
                et_check(em_weapon_shots() == 0, "no shot fired");
                /* the decoded trigger: a damage mailbox write (the
                 * same +0x36 write a knife impact does — the real
                 * knife path is EM_MELEE_TEST's job) */
                em_enemy_damage(0, 5);
                et_hit_sent = n;
            }
        } else if (!g.et_burst_frame) {
            if (em_enemy_state(0) == EM_ENEMY_FREE) {
                /* the crate burst (slot 0 freed) — the next IDLE
                 * mailbox poll after the write */
                g.et_burst_frame = n;
                g.et_bd          = et_dist();
                et_check(n <= et_hit_sent + 2,
                         "burst on the IDLE mailbox poll");
                et_check(em_enemy_alive() == 2 &&
                         em_enemy_kind(1) == EM_ENEMY_KIND_BUG &&
                         em_enemy_kind(2) == EM_ENEMY_KIND_BUG,
                         "nest-group bugs (2) hatched by the burst "
                         "(s68 — never a worm)");
                float cp[3] = { 0, 0, 0 }, bp[3] = { 0, 0, 0 };
                em_enemy_pos(0, cp);
                for (int k = 1; k <= 2; k++) {
                    em_enemy_pos(k, bp);
                    et_check(fabsf(bp[0] - cp[0]) +
                             fabsf(bp[1] - cp[1]) +
                             fabsf(bp[2] - cp[2]) < 2.5f,
                             "bug hatched at the crate (the "
                             "record-offset ring stand-in)");
                }
                et_check(em_enemy_hp(1) == 15 && em_enemy_hp(2) == 15,
                         "variant-A HP 15 (func_00128390)");
                g.et_wd0 = g.et_wd_min = et_dist_i(1);
            } else if (n >= et_hit_sent + 60) {
                et_check(0, "crate burst after the damage write");
                et_finish("crate run");
            }
        } else {
            float wd = et_dist_i(1);
            if (wd < g.et_wd_min) g.et_wd_min = wd;
            if (em_enemy_state(1) == EM_ENEMY_ATTACK)
                g.et_worm_atk = 1;   /* bugs engaged (s76 approach brain) */
            if (!et_flinch_sent) {
                if (n == g.et_burst_frame + 30) {
                    et_check(g.et_worm_atk,
                             "bugs engaged after the burst (the "
                             "s76 approach brain)");
                    /* the bug mailbox ladder, step 1: a knife-light-
                     * sized 5 must FLINCH, not kill (HP 15) */
                    em_enemy_damage(1, 5);
                    et_flinch_sent = n;
                }
            } else if (!et_kill_sent) {
                if (n == et_flinch_sent + 5) {
                    et_check(em_enemy_state(1) == EM_ENEMY_ATTACK &&
                             em_enemy_hp(1) == 10,
                             "nonlethal hit FLINCHES the bug — HP 15 "
                             "-> 10 (every-tick mailbox "
                             "func_00128B80)");
                    /* step 2: a heavy-stab-sized 15 out-kills the
                     * remaining 10 */
                    em_enemy_damage(1, 15);
                    et_kill_sent = n;
                }
            } else if (n == et_kill_sent + 5) {
                et_check(em_enemy_state(1) == EM_ENEMY_FREE,
                         "lethal mailbox kills the bug (handler "
                         "func_00129FC0; corpse fade)");
                et_check(em_enemy_alive() == 1 &&
                         em_enemy_state(2) == EM_ENEMY_ATTACK,
                         "the other bug lives on");
                printf("enemy test (crate run): burst dist %.1f, bug "
                       "dist %.1f -> min %.1f, health %.0f, draw "
                       "slots %d\n", g.et_bd, g.et_wd0, g.et_wd_min,
                       g.status.health, em_enemy_count());
                et_finish("crate run");
            }
        }
    } else {
        if (!g.et_hit_frame) {
            if (g.status.health < g.et_health0) {
                g.et_hit_frame = n;
            } else if (n >= 600) {
                et_check(0, "worm reached the player by frame 600 "
                            "(approach 90 t + stalk + windup + lunge)");
                et_finish("contact run");
            }
        } else if (n == g.et_hit_frame + 5) {
            et_check(g.status.health == g.et_health0 - 15.0f,
                     "lunge connect dealt the decoded latch (15)");
            et_check(em_enemy_alive() == 0,
                     "worm burst on the lunge (suicide path)");
            et_finish("contact run");
        }
    }
}

/* EM_SFX_TEST=1 — one-shot SFX mixer self-test (em_sfx.c). Fires THREE
 * one-shots 10 frames (1/6 s) apart — weapon draw 0x162, fire 0x164,
 * enemy death 0x7D8, all mapped by the user's assets/sfx/sfx.txt — so
 * their voices OVERLAP in the shared render callback (over the BGM when
 * EM_BGM started one); then (frame 40) asserts FIVE
 * synthetic pan/attenuation vectors against the decoded func_001FBF50
 * math (center/full at the player, range cull, hard-left at 90 deg,
 * the behind-the-camera phase inversion, the 18-u proximity ramp —
 * em_sfx.h "POSITIONAL AUDIO"). CONFIRMED by audit 2026-07-31 against
 * src/func_001FBF50.c (BYTE-MATCHED — authoritative): the range cull is
 * `if (!(dist < range)) return 0;`, the magnitude is
 * `vol * sin(pi/2 * (range - dist) / range)`, the near-field weight is
 * `dist <= 18.0f ? 0.055555556f * dist : 1.0f` (1/18 exactly), and the
 * pan scalar is `t^5 * w` pushed to the far channel by `± (1 - w)`,
 * with func_001B1380's side test choosing which of the two out-params
 * gets the full magnitude — the port's l/r split. Exercises the cull
 * (frame 50), bursts 60 plays to prove the 48-voice-budget OLDEST
 * steal with zero drops (frame 60), and prints the audio thread's
 * counters + PASS/FAIL at frame 120. Needs the registry: without
 * sfx.txt the module is disabled by design and the test reports the
 * FAIL. */
/* EM_PAUSE_TEST=1 — STATUS-SCREEN PAUSE self-test (the 2026-06-11
 * fidelity note: the open status menu PAUSES the game — gameplay_frame
 * gates the whole world update on em_hud_is_open()) PLUS the decoded
 * MENU-LOCK gate (em_door.h "THE TWO LOCKS": the engine's open poll
 * func_001AE7E0 refuses while the fade machine runs — RE-VERIFIED in
 * src/func_001AE7E0.c (NEARMISS 99.97%, logic authoritative; the old
 * "99.10%" here was a stale number): the
 * classifier returns 0 (= blocked) on `if (D_0028A9A0 != 0) return 0;`,
 * D_0028A9A0 being the same fade-machine state the game-over chain
 * polls for idle/hold-black. TIGHTENED (audit 2026-07-31): that test is
 * SIXTH in the chain, so it is not an unconditional veto — the earlier
 * arms `if (D_008106CE) return 3;` and
 * `if (D_008106C5 || D_008106B0) return 2;` still report their own
 * nonzero mode DURING a fade. The port models only the blocked/allowed
 * split, which is faithful for the door-transit case this gate serves
 * (no native counterpart to those two mode globals exists). The lock
 * ends at
 * fade-in completion, BEFORE the arrival walk-out finishes — so the
 * menu opens mid-walk-out while movement is still locked). Spawned at
 * the door-test corridor position (72, 0, -225 facing -X, the west
 * double door ahead) and scripted through the real input API:
 *
 *   frame    5      TRIANGLE -> the status screen opens (no transit:
 *                   the menu lock is clear)
 *   frames  10..70  hold 'w' (full stick = run) — the world is halted:
 *                   the player must NOT move (assert at 70)
 *   frame   70      TRIANGLE -> close; release 'w'
 *   frame   80      assert closed; hold 'w' again
 *   frame  140      assert the player MOVED (the held key that was dead
 *                   under the pause now runs him onto the x = 60
 *                   boundary-wall radius stop, ~7.5 u); release
 *   frame  150      CROSS -> west-door transit (goto tail ignored under
 *                   EM_PAUSE_TEST: same-scene re-place). Timeline:
 *                   staging ~161, commit ~231, black + re-place ~295,
 *                   fade-in done ~359, walk-out end ~405
 *   frame  156      assert the MENU LOCK engaged with the kickoff
 *   frame  320      TRIANGLE press MID-FADE-IN -> must be DROPPED (the
 *                   func_001AE7E0 fade gate); assert closed at 326
 *   frame  380      MID-WALK-OUT: assert menu lock OFF + movement lock
 *                   ON, then TRIANGLE -> the menu MUST OPEN over the
 *                   running walk-out (assert open at 386; record pos)
 *   frame  400      assert the open menu FROZE the walk-out (pos
 *                   unchanged 386..400); TRIANGLE -> close
 *   frame  410      assert closed (walk-out resumes)
 *   frame  470      assert movement unlocked (walk-out completed after
 *                   the resume) and the player kept walking past the
 *                   frozen pos -> PASS/FAIL, quit
 */
static void pause_test_script(void)
{
    static float pp[2], wp_x;
    static int ok_open, ok_frozen, ok_closed;
    static int ok_lock_on, ok_drop, ok_split, ok_midopen, ok_wfrozen,
               ok_reclosed;
    int n = g.frame_no;
    if (n == 5) {
        move_test_inject('i', 1);          /* TRIANGLE — open */
    } else if (n == 7) {
        move_test_inject('i', 0);
    } else if (n == 10) {
        ok_open = em_hud_is_open();
        pp[0] = g.pos[0];
        pp[1] = g.pos[2];
        /* Door approach uses 'a' (LEFT), not 'w': under the BYTE-FAITHFUL
         * move basis (atan2(sx,-sy) + cam_yaw + pi/2) the spawn's -X-facing
         * chase camera makes LEFT head straight -X at the door, while 'w'
         * walks 90 deg off (see door_test_script's WALK-UP note). Here the
         * world is PAUSED so the key is dead anyway, but keeping it 'a'
         * matches the un-paused approach that re-runs at n=80. */
        move_test_inject('a', 1);          /* held under the pause */
    } else if (n == 70) {
        ok_frozen = fabsf(g.pos[0] - pp[0]) < 1e-4f &&
                    fabsf(g.pos[2] - pp[1]) < 1e-4f;
        move_test_inject('a', 0);
        move_test_inject('i', 1);          /* TRIANGLE — close */
    } else if (n == 72) {
        move_test_inject('i', 0);
    } else if (n == 80) {
        ok_closed = !em_hud_is_open();
        move_test_inject('a', 1);          /* must move now (-X to the door) */
    } else if (n == 140) {
        move_test_inject('a', 0);          /* parked on the boundary */
    } else if (n == 150) {
        move_test_inject('k', 1);          /* CROSS — door transit */
    } else if (n == 152) {
        move_test_inject('k', 0);
    } else if (n == 156) {
        ok_lock_on = em_door_menu_locked();
    } else if (n == 320) {
        move_test_inject('i', 1);          /* TRIANGLE mid-fade-in */
    } else if (n == 322) {
        move_test_inject('i', 0);
    } else if (n == 326) {
        ok_drop = !em_hud_is_open() && em_door_menu_locked();
    } else if (n == 380) {
        /* fade-in done (~359), walk-out still running (~405) */
        ok_split = !em_door_menu_locked() && em_door_movement_locked();
        move_test_inject('i', 1);          /* TRIANGLE mid-walk-out */
    } else if (n == 382) {
        move_test_inject('i', 0);
    } else if (n == 386) {
        ok_midopen = em_hud_is_open() && em_door_movement_locked();
        wp_x = g.pos[0];
    } else if (n == 400) {
        ok_wfrozen = fabsf(g.pos[0] - wp_x) < 1e-4f;
        move_test_inject('i', 1);          /* TRIANGLE — close */
    } else if (n == 402) {
        move_test_inject('i', 0);
    } else if (n == 410) {
        ok_reclosed = !em_hud_is_open();
    } else if (n == 470) {
        float dx = g.pos[0] - pp[0], dz = g.pos[2] - pp[1];
        int moved   = dx * dx + dz * dz > 25.0f;   /* > 5 u of motion */
        int ok_done = !em_door_movement_locked() &&
                      g.pos[0] < wp_x - 0.5f;   /* kept walking out */
        int ok = ok_open && ok_frozen && ok_closed && moved &&
                 ok_lock_on && ok_drop && ok_split && ok_midopen &&
                 ok_wfrozen && ok_reclosed && ok_done;
        printf("pause test: opened %s, frozen under held stick %s, "
               "closed %s, resumed (moved %.1f u) %s, menu lock at "
               "kickoff %s, mid-fade press dropped %s, two-lock split "
               "mid-walk-out %s, menu OPEN over the walk-out %s, "
               "walk-out frozen under the menu %s, re-closed %s, "
               "walk-out completed after resume (x %.2f < %.2f) %s — "
               "%s\n",
               ok_open ? "ok" : "FAILED", ok_frozen ? "ok" : "FAILED",
               ok_closed ? "ok" : "FAILED",
               sqrtf(dx * dx + dz * dz), moved ? "ok" : "FAILED",
               ok_lock_on ? "ok" : "FAILED", ok_drop ? "ok" : "FAILED",
               ok_split ? "ok" : "FAILED", ok_midopen ? "ok" : "FAILED",
               ok_wfrozen ? "ok" : "FAILED",
               ok_reclosed ? "ok" : "FAILED",
               g.pos[0], wp_x, ok_done ? "ok" : "FAILED",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* EM_PICKUP_TEST=1 — pickup collect / inventory / despawn / persistence
 * self-test (the 2026-06-11 pickup decode, em_pickup.h). Runs on the
 * default scene (assets/scene — needs the exported assets tree for the
 * reload leg, like EM_DOOR_TEST): two SYNTHETIC pickups are injected
 * by the script itself so the assertions are placement-independent:
 *
 *   A: type 0x10 (SPR4 MAGAZINE — the func_001C40B0 case-0x10 ammo
 *      path), 4 u IN FRONT of the spawn (inside the 7-u facing
 *      auto-pass ring), uid 0xF01
 *   B: type 0x20 (MTS VACCINE), 9 u BEHIND the spawn — inside the
 *      10-u ring but outside the auto ring with the player facing
 *      AWAY: the pi/4 facing gate must hold, uid 0xF02
 *
 *   frame  1   inject A + B; record the reserve
 *   frame  5   CROSS -> the scan arms A (nearest passer; B fails the
 *              facing gate); the take fires after the 2 scripted
 *              frames
 *   frame 12   assert: count[0x10] and the pack counter each +1 over
 *              their frame-1 values (em_pickup_reset seeds both to 2,
 *              as 001AF2C0 does), taken bit 0xF01 set, reserve ==
 *              r0 + 30 (the +30/pack applied through the holstered
 *              weapon), count[0x20] == 0
 *   frame 20   CROSS again -> assert at 28: counts unchanged (A
 *              DESPAWNED — no double collect; B's facing gate held)
 *   frame 40   scene RELOAD (em_game_scene_switch into the same dir —
 *              the engine's area re-entry); re-inject A and B: A must
 *              be SUPPRESSED (rc -2, the cond-1 taken check), B
 *              re-places
 *   frame 45   assert + PASS/FAIL + quit. */
static void pickup_test_script(void)
{
    static int ok_place, ok_take, ok_gate, ok_reload, ok_suppress;
    static float pa[3], pb[3];
    static int mag0, packs0;        /* frame-1 counts (001AF2C0 seeds) */
    int n = g.frame_no;
    if (n == 1) {
        float fy = g.spawn_yaw;
        pa[0] = g.spawn[0] + 4.0f * sinf(fy);
        pa[1] = g.spawn[1];
        pa[2] = g.spawn[2] + 4.0f * cosf(fy);
        pb[0] = g.spawn[0] - 9.0f * sinf(fy);
        pb[1] = g.spawn[1];
        pb[2] = g.spawn[2] - 9.0f * cosf(fy);
        int a = em_pickup_add(em_frame_gfx(), g.scene_dir,
                              EM_PICKUP_TYPE_MAG, pa, 0.0f, 0xF01,
                              NULL, 0);
        int b = em_pickup_add(em_frame_gfx(), g.scene_dir, 0x20, pb,
                              0.0f, 0xF02, NULL, 0);
        g.pt_slot = a;
        g.pt_r0   = em_weapon_reserve();
        mag0      = em_pickup_item_count(EM_PICKUP_TYPE_MAG);
        packs0    = em_pickup_mag_packs();
        /* 001AF2C0 seeds count[0x10] = 2 and 2 packs (001C40B0(0x10,2));
         * the absolute check catches a boot path that skipped the reset */
        ok_place  = a >= 0 && b >= 0 &&
                    mag0 == 2 && packs0 == 2 &&
                    em_pickup_item_count(0x20) == 0 &&
                    !em_pickup_taken(0xF01);
        if (!ok_place) g.pt_fail++;
    } else if (n == 5) {
        move_test_inject('k', 1);          /* CROSS — the use press */
    } else if (n == 7) {
        move_test_inject('k', 0);
    } else if (n == 12) {
        ok_take = em_pickup_item_count(EM_PICKUP_TYPE_MAG) == mag0 + 1 &&
                  em_pickup_mag_packs() == packs0 + 1 &&
                  em_pickup_taken(0xF01) &&
                  em_weapon_reserve() == (int16_t)(g.pt_r0 + 30) &&
                  em_pickup_item_count(0x20) == 0;
        if (!ok_take) {
            g.pt_fail++;
            printf("pickup test: CHECK FAILED — take (count %u packs "
                   "%u taken %d reserve %d/%d count20 %u)\n",
                   em_pickup_item_count(EM_PICKUP_TYPE_MAG),
                   em_pickup_mag_packs(), em_pickup_taken(0xF01),
                   em_weapon_reserve(), g.pt_r0 + 30,
                   em_pickup_item_count(0x20));
        }
    } else if (n == 20) {
        move_test_inject('k', 1);
    } else if (n == 22) {
        move_test_inject('k', 0);
    } else if (n == 28) {
        ok_gate = em_pickup_item_count(EM_PICKUP_TYPE_MAG) == mag0 + 1 &&
                  em_pickup_item_count(0x20) == 0;
        if (!ok_gate) g.pt_fail++;
    } else if (n == 40) {
        ok_reload = em_game_scene_switch(g.scene_dir) == 0;
        if (!ok_reload) g.pt_fail++;
        int a = em_pickup_add(em_frame_gfx(), g.scene_dir,
                              EM_PICKUP_TYPE_MAG, pa, 0.0f, 0xF01,
                              NULL, 0);
        int b = em_pickup_add(em_frame_gfx(), g.scene_dir, 0x20, pb,
                              0.0f, 0xF02, NULL, 0);
        ok_suppress = a == -2 && b >= 0;
        if (!ok_suppress) g.pt_fail++;
    } else if (n == 45) {
        int ok_persist = em_pickup_taken(0xF01) &&
                         !em_pickup_taken(0xF02) &&
                         em_pickup_item_count(EM_PICKUP_TYPE_MAG) == mag0 + 1;
        int ok = ok_place && ok_take && ok_gate && ok_reload &&
                 ok_suppress && ok_persist && g.pt_fail == 0;
        printf("pickup test: placed %s, collected (+count/+packs/+30 "
               "reserve/taken bit) %s, despawn + facing gate %s, "
               "scene reload %s, taken-uid respawn suppressed %s, "
               "inventory persisted %s — %s\n",
               ok_place ? "ok" : "FAILED", ok_take ? "ok" : "FAILED",
               ok_gate ? "ok" : "FAILED", ok_reload ? "ok" : "FAILED",
               ok_suppress ? "ok" : "FAILED",
               ok_persist ? "ok" : "FAILED", ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* EM_EXAMINE_TEST=1 — examine arm / input pause / radio line / camera
 * cue / chain presenter / cooldown / re-arm self-test (the 2026-06-11
 * examine decode, em_examine.h). Runs on the default scene; THREE
 * SYNTHETIC examine objects are injected so the assertions are
 * placement-independent (the real records ship in the scene manifests
 * — export_level.py --examine):
 *
 *   A: GLOBAL line 0x1A ("Switch / No power...", 148 frames — the
 *      AREA11 snow-switch refusal shape) 4 u IN FRONT of the spawn
 *      (the 7-u facing auto-pass ring), with an op00-style camera cue
 *      and the decoded 300-frame cooldown
 *   B: AREA-bank chain (the office/drawbridge shape: text/gap/text +
 *      terminal, pre-delay 5) 12 u in front — inside the 20-u ring,
 *      a passer, but A is nearer (nearest-wins leg)
 *   C: chain 9 u BEHIND — the pi/4 facing gate must exclude it
 *
 *   frame   1   inject A + B + C
 *   frame   5   CROSS -> the scan arms A (nearest passer)
 *   frame 100   assert: sequence active on A, input locked, the radio
 *               machine presenting, the camera pinned at A's cue
 *   frame 170   assert: sequence over (148 + terminal), input free,
 *               camera restored (eye off the cue), A cooling down
 *   frame 172   CROSS -> A refused (cooldown) + C refused (facing):
 *               B arms; chain = delay 5 + 20 text + 10 gap + 15 text
 *               + 1 terminal -> done ~frame 225
 *   frame 190   assert: active on B, locked, radio machine IDLE (the
 *               chain rides em_examine's own presenter)
 *   frame 240   assert: unlocked again
 *   frame 245   CROSS -> B RE-ARMS (no cooldown — the engine re-arm)
 *   frame 250   assert + PASS/FAIL + quit. */
static void examine_test_script(void)
{
    static int   ok_arm, ok_lock, ok_cam, ok_done, ok_chain, ok_gate,
                 ok_rearm;
    static int   slot_a = -1, slot_b = -1;
    static float cue[3];
    int n = g.frame_no;
    if (n == 1) {
        float fy = g.spawn_yaw;
        float pa[3] = { g.spawn[0] + 4.0f * sinf(fy), g.spawn[1],
                        g.spawn[2] + 4.0f * cosf(fy) };
        float pb[3] = { g.spawn[0] + 12.0f * sinf(fy), g.spawn[1],
                        g.spawn[2] + 12.0f * cosf(fy) };
        float pc[3] = { g.spawn[0] - 9.0f * sinf(fy), g.spawn[1],
                        g.spawn[2] - 9.0f * cosf(fy) };
        cue[0] = pa[0];
        cue[1] = pa[1] + 9.0f;   /* an office-cue-shaped raised eye */
        cue[2] = pa[2] + 6.0f;
        slot_a = em_examine_add(pa, 0.0f, EM_EXAMINE_RADIUS,
                                EM_EXAMINE_DY, 0x1A, 0, 300, cue);
        slot_b = em_examine_add(pb, 0.0f, EM_EXAMINE_RADIUS,
                                EM_EXAMINE_DY, -1, 5, 0, NULL);
        int c  = em_examine_add(pc, 0.0f, EM_EXAMINE_RADIUS,
                                EM_EXAMINE_DY, -1, 0, 0, NULL);
        em_examine_text(slot_b, 20, 10, "EXAMINE TEST ONE");
        em_examine_text(slot_b, 15, 0, "EXAMINE\\nTEST TWO");
        em_examine_text(c, 20, 0, "WRONG OBJECT");
        if (slot_a < 0 || slot_b < 0 || c < 0) g.ex_fail++;
    } else if (n == 5 || n == 172 || n == 245) {
        move_test_inject('k', 1);          /* CROSS — the use press */
    } else if (n == 7 || n == 174 || n == 247) {
        move_test_inject('k', 0);
    } else if (n == 100) {
        float ce[3], ct[3];
        ok_arm  = em_examine_active() == slot_a;
        ok_lock = em_examine_input_locked() && em_hud_radio_active();
        ok_cam  = em_examine_camera(ce, ct) &&
                  fabsf(g.cam.eye[0] - cue[0]) < 1e-3f &&
                  fabsf(g.cam.eye[1] - cue[1]) < 1e-3f &&
                  fabsf(g.cam.eye[2] - cue[2]) < 1e-3f;
        if (!ok_arm || !ok_lock || !ok_cam) {
            g.ex_fail++;
            printf("examine test: CHECK FAILED — present (active %d/%d "
                   "lock %d radio %d cam %d eye %.2f %.2f %.2f)\n",
                   em_examine_active(), slot_a,
                   em_examine_input_locked(), em_hud_radio_active(),
                   ok_cam, g.cam.eye[0], g.cam.eye[1], g.cam.eye[2]);
        }
    } else if (n == 170) {
        ok_done = !em_examine_input_locked() && !em_hud_radio_active() &&
                  em_examine_active() < 0 &&
                  fabsf(g.cam.eye[1] - cue[1]) > 1e-3f;
        if (!ok_done) g.ex_fail++;
    } else if (n == 190) {
        ok_chain = em_examine_active() == slot_b &&
                   em_examine_input_locked() && !em_hud_radio_active();
        ok_gate  = 1;   /* C armed would have made active == c != b */
        if (!ok_chain) {
            g.ex_fail++;
            printf("examine test: CHECK FAILED — chain (active %d/%d "
                   "lock %d radio %d)\n", em_examine_active(), slot_b,
                   em_examine_input_locked(), em_hud_radio_active());
        }
    } else if (n == 240) {
        if (em_examine_input_locked()) g.ex_fail++;
    } else if (n == 250) {
        ok_rearm = em_examine_active() == slot_b;
        if (!ok_rearm) g.ex_fail++;
        int ok = ok_arm && ok_lock && ok_cam && ok_done && ok_chain &&
                 ok_gate && ok_rearm && g.ex_fail == 0;
        printf("examine test: armed (nearest, CROSS edge) %s, input "
               "paused + radio line %s, camera cue pinned %s, timed "
               "dismiss + restore + cooldown %s, area chain (facing "
               "gate held) %s, re-arm %s — %s\n",
               ok_arm ? "ok" : "FAILED", ok_lock ? "ok" : "FAILED",
               ok_cam ? "ok" : "FAILED", ok_done ? "ok" : "FAILED",
               ok_chain ? "ok" : "FAILED", ok_rearm ? "ok" : "FAILED",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

static void sfx_test_script(void)
{
    /* Decoded-math assertion state (frame 40/50/60 below). The synthetic
     * listener is overwritten by the SAME frame's em_sfx_listener call
     * at the gameplay-frame tail, so gameplay audio is untouched. */
    static int ok_gain = -1;
    static int live_at_burst;
    int n = g.frame_no;
    if (n == 10 || n == 20 || n == 30) {
        static const unsigned ids[3] = { EM_SFX_WPN_DRAW, EM_SFX_WPN_FIRE,
                                         EM_SFX_ENEMY_DEATH };
        em_sfx_play(ids[n / 10 - 1]);
    } else if (n == 40) {
        /* PAN/ATTENUATION assertions against the func_001FBF50 decode
         * (em_sfx.h "POSITIONAL AUDIO"): synthetic listener at the
         * origin (player == camera eye, yaw 0 = facing +Z), expected
         * gains precomputed by hand from the formulas. */
        const float o[3] = { 0, 0, 0 };
        em_sfx_listener(o, o, 0.0f);
        float l, r;
        int   okc = 1;
        /* 1. at the player: d=0 -> vol=1, k=0 -> t=+1 -> center/full */
        okc &= em_sfx_compute_gains(o, 300.0f, &l, &r) == 1 &&
               fabsf(l - 1.0f) < 1e-4f && fabsf(r - 1.0f) < 1e-4f;
        /* 2. beyond the radius: engine play_sound -1 (not submitted) */
        { const float p[3] = { 0, 0, 400 };
          okc &= em_sfx_compute_gains(p, 300.0f, &l, &r) == 0; }
        /* 3. 90 deg LEFT (+X = bearing +pi/2 = the engine's near-LEFT
         * side test) at d=150, r=300: vol = sin(pi/4) = 0.70711,
         * c = cos(pi/2) = 0, k = 1 -> t = 0: hard left, far silent */
        { const float p[3] = { 150, 0, 0 };
          okc &= em_sfx_compute_gains(p, 300.0f, &l, &r) == 1 &&
                 fabsf(l - 0.70711f) < 1e-3f && fabsf(r) < 1e-3f; }
        /* 4. BEHIND at d=100: vol = sin(pi/2 * 200/300) = 0.86603,
         * c = -1 -> t = -1: far channel PHASE-INVERTED at full */
        { const float p[3] = { 0, 0, -100 };
          okc &= em_sfx_compute_gains(p, 300.0f, &l, &r) == 1 &&
                 fabsf(l - 0.86603f) < 1e-3f &&
                 fabsf(r + 0.86603f) < 1e-3f; }
        /* 5. proximity pan ramp: 45 deg left at d=9 (= 18/2): k=0.5,
         * c = cos(pi/4) -> t = c^5*k + (1-k) = 0.17678*0.5 + 0.5 =
         * 0.58839; vol = sin(pi/2 * 291/300) = 0.99889. (Not the 90 deg
         * axis: there cosf lands on +-4e-8 and the engine's own sign
         * branch — and ours — flips on the noise bit.) */
        { const float p[3] = { 6.3640f, 0, 6.3640f };
          okc &= em_sfx_compute_gains(p, 300.0f, &l, &r) == 1 &&
                 fabsf(l - 0.99889f) < 1e-3f &&
                 fabsf(r - 0.58774f) < 1e-3f; }
        ok_gain = okc;
    } else if (n == 50) {
        /* RANGE CULL through the play path: a mapped id beyond its
         * radius must not submit (engine -1) — counted, not played. */
        const float o[3] = { 0, 0, 0 }, far_p[3] = { 0, 0, 400 };
        em_sfx_listener(o, o, 0.0f);
        em_sfx_play_at(EM_SFX_WPN_FIRE, far_p, 300.0f);
    } else if (n == 60) {
        /* VOICE STEALING at the engine's 48 budget (em_sfx.h "VOICE
         * STEALING"): burst 60 center plays in one frame — every play
         * past 48 live voices kills the then-oldest; the 16 spare
         * physical slots absorb the one-callback kill latency, so
         * NOTHING drops. Expected steals = 12 + (voices still ringing
         * from frames 10..30: WAV-length dependent, 0..3). */
        live_at_burst = em_sfx_plays();   /* accepted so far (3) */
        for (int i = 0; i < 60; i++)
            em_sfx_play(EM_SFX_WPN_FIRE);
    } else if (n == 120) {
        long mixed  = em_sfx_frames_mixed();
        int  peak   = em_sfx_max_concurrent();
        int  steals = em_sfx_steals();
        /* steals = max(0, live_at_burst_voices + 60 - 48); the three
         * early voices may have ended -> accept the 12..15 band. */
        int  ok = em_sfx_sound_count() > 0 && em_sfx_plays() == 63 &&
                  em_sfx_drops() == 0 && em_sfx_culls() == 1 &&
                  steals >= 12 && steals <= 15 &&
                  ok_gain == 1 && mixed > 0 && peak >= 2;
        printf("sfx test: %d sound(s) loaded, %d play(s) (%d dropped), "
               "pan/attenuation vectors %s, range cull %d, steals %d "
               "(48-voice budget, 60-play burst over %d ringing), %ld "
               "voice frames mixed, peak %d concurrent voice(s) — %s\n",
               em_sfx_sound_count(), em_sfx_plays(), em_sfx_drops(),
               ok_gain == 1 ? "ok" : "FAILED", em_sfx_culls(), steals,
               live_at_burst, mixed, peak, ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* EM_CAMREGION_TEST=1 — FIXED-CAMERA REGION self-test (the mode-0
 * director decode, FINDINGS "MODE-0 CAMERA DIRECTOR DECODED"). Scene
 * init REPLACES the scene's region list with one SYNTHETIC,
 * CLEARLY-FLAGGED test region 5 u down +Z of the spawn (see
 * ingame_frame_machine case 0) so the run is deterministic and
 * spawn-local — the machinery under test is exactly what scene_snow's
 * REAL exported region (AREA06, D_0024A5F0[2]) and the office scene's
 * REAL supply-room line (the spawn-record camera, this session's
 * decode; EM_CAPTURE_SUPPLY samples it in place) drive. Adaptive
 * phase machine:
 *
 *   phase 0  hold 'a' (run +Z — faithful-basis forward, camera behind);
 *            the frame the region engages (g.cam_region_on — one frame
 *            after the crossing), release. (Safety: FAIL+quit by frame
 *            180 if it never engages, so a movement-basis change can
 *            never re-hang this test.)
 *   phase 1  mark+2: assert the camera eye sits EXACTLY at the room
 *            spec (enter -> camera at spec).
 *   phase 2  mark+10..12 tap 'q' (L1); mark+30: assert the eye is
 *            STILL at the spec and no recenter armed (L1 is a NO-OP
 *            in a specified-camera room).
 *   phase 3  mark+40 hold 'e' (R1 aim): the aim camera takes over.
 *            mark+130: assert the eye LEFT the spec (> 6 u — aim still
 *            works in fixed rooms) and release.
 *   phase 4  mark+132: assert the eye is back at the spec EXACTLY,
 *            two frames after release — the chase cap (4 u/frame)
 *            could never cover the aim distance that fast, so equality
 *            here proves the snap-back is INSTANT (no lerp): the
 *            user-observed behavior. PASS/FAIL, quit.
 */
static void camregion_test_script(void)
{
    static int phase, mark, ok_enter, ok_l1, ok_aim;
    static float d_aim;
    const EmCamRegion *spec = &g.camregion[0];
    float de[3] = { g.cam.eye[0] - spec->eye[0],
                    g.cam.eye[1] - spec->eye[1],
                    g.cam.eye[2] - spec->eye[2] };
    float d   = sqrtf(de[0] * de[0] + de[1] * de[1] + de[2] * de[2]);
    int   at  = d < 1e-5f;
    int   n   = g.frame_no;

    switch (phase) {
        case 0:
            /* 'a' = forward (into the +Z synthetic region) under the
             * BYTE-FAITHFUL move basis with the chase camera directly
             * behind the spawn — same remap the slider/door tests took
             * 2026-06-17. 'w' here drives +X (cam_yaw 0 -> 'w' heads
             * cam_yaw + pi/2), NEVER crossing the +Z region, which used to
             * HANG phase 0 forever. */
            if (n == 0) move_test_inject('a', 1);
            if (g.cam_region_on) {
                move_test_inject('a', 0);
                mark  = n;
                phase = 1;
            } else if (n >= 180) {
                /* SAFETY: a full run +Z reaches the 5-u region in well
                 * under 180 frames. If it has not engaged, FAIL loudly
                 * rather than loop forever (the old 'w' bug). */
                printf("camregion test: CHECK FAILED — region never "
                       "engaged by frame %d (player never reached the "
                       "synthetic region)\n", n);
                fflush(stdout);
                em_frame_request_quit();
            }
            break;
        case 1:
            if (n == mark + 2) {
                ok_enter = g.cam_region_on && at;
                if (!ok_enter)
                    printf("camregion test: CHECK FAILED — enter (on %d, "
                           "eye off spec by %.3f)\n", g.cam_region_on, d);
                phase = 2;
            }
            break;
        case 2:
            if      (n == mark + 10) move_test_inject('q', 1);  /* L1 */
            else if (n == mark + 12) move_test_inject('q', 0);
            else if (n == mark + 30) {
                ok_l1 = g.cam_region_on && at && !g.cam_recenter;
                if (!ok_l1)
                    printf("camregion test: CHECK FAILED — L1 not a "
                           "no-op (on %d, recenter %d, eye off spec by "
                           "%.3f)\n", g.cam_region_on, g.cam_recenter, d);
                phase = 3;
            }
            break;
        case 3:
            if      (n == mark + 40) move_test_inject('e', 1);  /* R1 */
            else if (n == mark + 130) {
                d_aim  = d;
                ok_aim = !g.cam_region_on && d > 6.0f;
                if (!ok_aim)
                    printf("camregion test: CHECK FAILED — aim camera "
                           "(on %d, eye moved %.3f u, need > 6)\n",
                           g.cam_region_on, d);
                move_test_inject('e', 0);   /* release: must snap NOW */
                phase = 4;
            }
            break;
        case 4:
            if (n == mark + 132) {
                int ok_snap = g.cam_region_on && at;
                int ok = ok_enter && ok_l1 && ok_aim && ok_snap;
                printf("camregion test: enter->spec %s, L1 no-op %s, "
                       "aim moved %.1f u %s, release snapped to spec in "
                       "<= 2 frames (off by %.6f) %s — %s\n",
                       ok_enter ? "ok" : "FAILED",
                       ok_l1 ? "ok" : "FAILED", d_aim,
                       ok_aim ? "ok" : "FAILED", d,
                       ok_snap ? "ok" : "FAILED", ok ? "PASS" : "FAIL");
                fflush(stdout);
                em_frame_request_quit();
            }
            break;
    }
}

/* EM_AIM_TEST=1 — MANUAL AIM STEER + MODE-1 AIM CAMERA self-test (the
 * func_0017ABA0 / func_00197D20 decodes). Scripted run through the
 * real input API:
 *
 *   frame 0      hold 'e' (R1): draw -> held aim pose 0x112.
 *   frame 30     assert the stance entry: blends centered (0.5/0.5),
 *                pose 0x112 committed, camera in mode-1 steady.
 *   31..240      hold 'w' (stick UP): INVERTED Y — the pitch blend
 *                must FALL (aim DOWN, "W = down") and clamp at 0.0;
 *                checked mid-travel (90) and at the clamp (240).
 *   240          camera steady-state geometry (emergent, not the
 *                formula): the eye ~30 u horizontally behind the
 *                facing, eye.y at the player.y+30 ceiling (full-down
 *                aim counter-raises the eye), the target BELOW the
 *                +19 rest height (it rides the aim line down).
 *   250..400     hold 's' (stick DOWN): pitch must RISE through
 *                center and clamp at the stance-0x31 limit 1.0.
 *   410+         hold 'a' (stick LEFT): the yaw blend pans the POSE
 *                first (the body yaw must NOT move while the blend is
 *                unsaturated), then past the limit the BODY turns —
 *                checked at 415 (pose-only) and 460 (body turning,
 *                blend pinned at 1.0). PASS/FAIL, quit.
 */
static void aim_test_script(void)
{
    static int   fail;
    static float yaw0;
    int n = g.frame_no;

    switch (n) {
        case 0:
            move_test_inject('e', 1);          /* R1: draw + aim */
            break;
        case 30: {
            int ok = g.aim_pitch == 0.5f && g.aim_yawb == 0.5f &&
                     em_game_anim_active() == 0x112 &&
                     g.cam.aim_phase == 2;
            if (!ok) {
                fail++;
                printf("aim test: CHECK FAILED — entry (pitch %.3f yaw "
                       "%.3f anim 0x%x phase %d)\n", g.aim_pitch,
                       g.aim_yawb, em_game_anim_active(),
                       g.cam.aim_phase);
            }
            move_test_inject('w', 1);          /* stick UP */
            break;
        }
        case 90:
            if (!(g.aim_pitch < 0.5f)) {
                fail++;
                printf("aim test: CHECK FAILED — stick UP did not aim "
                       "DOWN (pitch %.3f, inverted-Y broken)\n",
                       g.aim_pitch);
            }
            break;
        case 240: {
            int ok_clamp = g.aim_pitch == 0.0f;
            float fx = sinf(g.yaw), fz = cosf(g.yaw);
            float ex = g.pos[0] - fx * CAM_AIM_EYE_BACK;
            float ez = g.pos[2] - fz * CAM_AIM_EYE_BACK;
            float dx = g.cam.eye[0] - ex, dz = g.cam.eye[2] - ez;
            int ok_eye  = sqrtf(dx * dx + dz * dz) < 1.5f &&
                          fabsf(g.cam.eye[1] - (g.pos[1] + 30.0f)) < 1.5f;
            int ok_tgt  = g.cam.tgt[1] < g.pos[1] + CAM_AIM_TGT_UP - 5.0f;
            if (!ok_clamp || !ok_eye || !ok_tgt) {
                fail++;
                printf("aim test: CHECK FAILED — full-down state "
                       "(pitch %.3f; eye off (%.2f, y %.2f vs %.2f); "
                       "tgt y %.2f)\n", g.aim_pitch,
                       sqrtf(dx * dx + dz * dz), g.cam.eye[1],
                       g.pos[1] + 30.0f, g.cam.tgt[1]);
            }
            move_test_inject('w', 0);
            break;
        }
        case 250:
            move_test_inject('s', 1);          /* stick DOWN */
            break;
        case 400:
            if (g.aim_pitch != AIM_PITCH_MAX_R1) {
                fail++;
                printf("aim test: CHECK FAILED — stick DOWN did not "
                       "clamp at the up limit (pitch %.3f)\n",
                       g.aim_pitch);
            }
            move_test_inject('s', 0);
            break;
        case 410:
            yaw0 = g.yaw;
            move_test_inject('a', 1);          /* stick LEFT */
            break;
        case 415:
            if (g.yaw != yaw0 || !(g.aim_yawb > 0.5f)) {
                fail++;
                printf("aim test: CHECK FAILED — pose-pan phase "
                       "(yaw %.4f vs %.4f, blend %.3f)\n", g.yaw,
                       yaw0, g.aim_yawb);
            }
            break;
        case 460: {
            int ok = g.aim_yawb == 1.0f && g.yaw != yaw0;
            if (!ok) {
                fail++;
                printf("aim test: CHECK FAILED — body-turn overflow "
                       "(blend %.3f, yaw %.4f vs %.4f)\n", g.aim_yawb,
                       g.yaw, yaw0);
            }
            printf("aim test: inverted-Y pitch, clamps, full-down "
                   "camera, pose-pan-then-body-turn — %s\n",
                   fail == 0 ? "PASS" : "FAIL");
            fflush(stdout);
            em_frame_request_quit();
            break;
        }
    }
}

/* EM_MELEE_TEST=1 — deterministic knife self-test (the s36 melee
 * decode, em_weapon.h "KNIFE / MELEE"; restaged for the s68
 * creature-identity correction: crate bursts hatch BUGS, which ARE
 * melee victims; the worm whiff witness rides a direct spawn now).
 * Scene init spawns TWO DISGUISED CRATES (see the spawn block): A in
 * the knife reach (12), B 25 u away across the room; crates burst on
 * DAMAGE ONLY (the engine has no proximity trigger). A damage kill
 * still BROADCASTS the group alarm (the decoded list-wide, no-radius
 * walk), but the broadcast is INERT for placed crates — the engine
 * wakes a recipient only when its +0x52 (on-surface) flag is set, and
 * that is 0 on every placed crate (live-read s76), so crate B does NOT
 * wake (user-reported 2026-06-12: breaking one crate must not move the
 * other; the J1 "match" was wrong). Crate B therefore stays an inert
 * IDLE crate for the whole run — which also keeps the frontal cone
 * clear for the whiff legs. Adaptive phase machine:
 *
 *   phase 0  frame 8: assert both crates IDLE in reach, then tap L
 *            (CIRCLE) — the LIGHT combo (engine mode 0x21). Impact =
 *            swing tick 26 (len 50 - gate 24); the acquire resolves
 *            the nearest crate (A, slot 0).
 *   phase 1  crate A dies: assert melee hit count 1, ZERO rifle shots
 *            (the kill is the +0x36 mailbox write), TWO BUGS hatched
 *            at the crate ring (the s68 burst contract), and crate B
 *            STAYS IDLE — the broadcast does NOT wake it (s76).
 *   phase 2  recover anim 0x10F commits (a landed hit SKIPS the combo).
 *            Then the s76 LATCH witness: the A-bugs WALK IN and LATCH
 *            onto the player (you do NOT stab a clinging bug — the new
 *            bug combat is the shake-off). Wait for em_enemy_latched_
 *            count() >= 1.
 *   phase 3  SHAKE-OFF: the player AUTO-shakes the latched bug off (clip
 *            0x36 must play) which DETACHES it (latched -> 0); then clear
 *            both A-bugs (mailbox) so the frontal cone is empty for the
 *            whiff legs.
 *   phase 5  the WORM WHIFF witness (J2 s66, kept): spawn ONE worm
 *            9 u dead ahead (em_enemy_add), heavy stab THROUGH it —
 *            melee hits STAY 1 (func_00183AC0 rejects model 0xD), the
 *            worm untouched (ATTACK, vestigial HP 10).
 *   phase 6  the worm clears itself: its OWN lunge resolve bursts it
 *            on the player (health reported, not asserted) — wait
 *            out the flinch.
 *   phase 7  WHIFF COMBO (frontal cone empty: A-bugs cleared, worm gone,
 *            crate B inert behind): tap L, re-tap L during each swing —
 *            assert the committed clip chains 0x10B -> 0x10C -> 0x10D
 *            (the buffered +0x2E chain), NO recover anim on a whiff
 *            (clip-end exit), the final counts (5 swings, 1 hit — the
 *            bugs latch + are shaken off, not stabbed), and that crate B
 *            is still the lone IDLE survivor.
 *
 * PASS = all checks green; any phase timing out fails the run. */
static void melee_test_script(void)
{
    static int saw_recov_anim, chain12, chain23, whiff_recov;
    /* (mash-struggle witness uses live state, no clip flag) */
    static int mt_cleared;          /* frame the A-bugs were cleared   */
    static int mt_j3;               /* heavy 3 (worm whiff) tap frame   */
    static int mt_worm = -1;        /* the worm-whiff witness slot     */
    static int dbg = -1;
    static unsigned prev_anim;
    int n = g.frame_no;
    unsigned anim = em_game_anim_active();

    if (dbg < 0) dbg = getenv("EM_MELEE_DEBUG") != NULL;
    if (dbg)
        printf("mt f%d ph%d melee(st%d cb%d hv%d sw%d hit%d) anim 0x%X "
               "alive %d st0 %d st1 %d d0 %.1f d1 %.1f\n", n, g.mt_phase,
               em_weapon_melee_state(), em_weapon_melee_combo(),
               em_weapon_melee_heavy(), em_weapon_melee_swings(),
               em_weapon_melee_hits(), anim, em_enemy_alive(),
               em_enemy_state(0), em_enemy_state(1), et_dist(),
               et_dist_i(1));
    /* transition trackers (sampled every frame) */
    if (anim == 0x10F && g.mt_phase <= 2)
        saw_recov_anim = 1;
    if (g.mt_phase == 7) {
        if (prev_anim == 0x10B && anim == 0x10C) chain12 = 1;
        if (prev_anim == 0x10C && anim == 0x10D) chain23 = 1;
        if (anim == 0x10F) whiff_recov = 1;
    }
    prev_anim = anim;

    if (n >= 1500) {
        g.mt_fail++;
        printf("melee test: CHECK FAILED — timed out in phase %d\n",
               g.mt_phase);
        goto finish;
    }

    switch (g.mt_phase) {
        case 0:
            if (n == 8) {
                /* both crates IDLE (A in reach, B across the room) —
                 * IDLE is engine-true at ANY range (no proximity
                 * trigger exists) */
                if (!(em_enemy_alive() == 2 &&
                      em_enemy_state(0) == EM_ENEMY_IDLE &&
                      em_enemy_state(1) == EM_ENEMY_IDLE &&
                      et_dist() < 12.0f)) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — crates not idle "
                           "(A in reach) (alive %d states %d/%d dist "
                           "%.1f/%.1f)\n", em_enemy_alive(),
                           em_enemy_state(0), em_enemy_state(1),
                           et_dist(), et_dist_i(1));
                }
                move_test_inject('l', 1);       /* CIRCLE — light combo */
            } else if (n == 10) {
                move_test_inject('l', 0);
                g.mt_phase = 1;
                g.mt_mark  = n;
            }
            break;
        case 1:
            if (em_enemy_state(0) == EM_ENEMY_FREE) {
                /* slot 0 freed = the burst frame; the bug pair
                 * hatches the same tick (s68) */
                if (!(em_weapon_melee_hits() == 1 &&
                      em_weapon_shots() == 0)) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — crate kill not "
                           "the knife mailbox (melee hits %d, shots "
                           "%d)\n", em_weapon_melee_hits(),
                           em_weapon_shots());
                }
                /* the A-bugs (slots 2/3) hatch at crate A's ring */
                float cp[3] = { 0, 0, 0 }, bp[3] = { 0, 0, 0 };
                em_enemy_pos(0, cp);
                int hatch_ok = em_enemy_alive() == 3 &&
                               em_enemy_kind(2) == EM_ENEMY_KIND_BUG &&
                               em_enemy_kind(3) == EM_ENEMY_KIND_BUG;
                for (int k = 2; k <= 3 && hatch_ok; k++) {
                    em_enemy_pos(k, bp);
                    hatch_ok = fabsf(bp[0] - cp[0]) +
                               fabsf(bp[2] - cp[2]) < 2.5f;
                }
                if (!hatch_ok) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — bug pair not at "
                           "the burst crate (alive %d kinds %d/%d)\n",
                           em_enemy_alive(), em_enemy_kind(2),
                           em_enemy_kind(3));
                }
                /* s76: the GROUP-ALARM BROADCAST is engine-true but
                 * INERT for placed crates — the engine reaches its wake
                 * write only for a recipient with +0x52 (on-surface)
                 * set, and that is 0 on every placed crate, so killing
                 * crate A does NOT wake crate B (user-reported; the J1
                 * "match" was wrong). Assert crate B STAYS IDLE. */
                if (em_enemy_state(1) != EM_ENEMY_IDLE) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — crate B should "
                           "stay IDLE (the broadcast must not wake it) "
                           "(state %d)\n", em_enemy_state(1));
                }
                g.mt_phase = 2;
                g.mt_mark  = n;
            }
            break;
        case 2:
            /* s76 LATCH + SHAKE-OFF witness (the new bug combat — you do
             * NOT stab a clinging bug, you shake it off): the A-bugs walk
             * in from the 11-u ring and LATCH onto the player. The light-
             * combo recover (0x10F) is tracked globally; wait for a latch
             * (the player then auto-shakes — player_shake_tick). */
            if (!saw_recov_anim && n > g.mt_mark + 90) {
                g.mt_fail++;
                printf("melee test: CHECK FAILED — recover anim 0x10F "
                       "never committed after the confirmed hit\n");
                saw_recov_anim = -1;            /* report once */
            }
            if (em_enemy_latched_count() >= 1) {
                g.mt_phase = 3;                 /* a bug latched */
                g.mt_mark  = n;
            } else if (n > g.mt_mark + 600) {
                g.mt_fail++;
                printf("melee test: CHECK FAILED — no bug latched onto "
                       "the player within budget\n");
                g.mt_phase = 3;
                g.mt_mark  = n;
            }
            break;
        case 3:
            /* s76 LIVE: the bugs LATCH and the player MASHES CROSS to
             * shake them off — which KILLS them (em_enemy_shake_off ->
             * EM_ENEMY_DEATH), not detach. Mash CROSS (alternate down/up =
             * press edges, ~1 per 2 frames) until the struggle wins; then
             * witness both A-bugs DIED. */
            move_test_inject('k', (n & 1) == 0);     /* CROSS press edges */
            if (!mt_cleared) {
                if (em_enemy_latched_count() == 0 &&
                    !player_damage_locked()) {        /* struggle won */
                    int dead = (em_enemy_state(2) == EM_ENEMY_FREE ||
                                em_enemy_state(2) == EM_ENEMY_DEATH) +
                               (em_enemy_state(3) == EM_ENEMY_FREE ||
                                em_enemy_state(3) == EM_ENEMY_DEATH);
                    if (dead < 2) {
                        g.mt_fail++;
                        printf("melee test: CHECK FAILED — the shake-off "
                               "must KILL the clinging bugs (states %d/%d)\n",
                               em_enemy_state(2), em_enemy_state(3));
                    }
                    move_test_inject('k', 0);
                    mt_cleared = n > 0 ? n : 1;
                } else if (n > g.mt_mark + 600) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — CROSS-mash did not "
                           "shake the bugs off (latched %d)\n",
                           em_enemy_latched_count());
                    move_test_inject('k', 0);
                    for (int k = 2; k <= 3; k++)
                        if (em_enemy_state(k) != EM_ENEMY_FREE)
                            em_enemy_damage(k, 15);
                    mt_cleared = n;
                }
            } else if (n > mt_cleared + 12 && !player_damage_locked()) {
                int live_a = (em_enemy_state(2) != EM_ENEMY_FREE) +
                             (em_enemy_state(3) != EM_ENEMY_FREE);
                if (live_a) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — A-bugs not gone "
                           "(%d live)\n", live_a);
                }
                g.mt_phase = 5;
                g.mt_mark  = n;
            }
            break;
        case 5:
            /* the WORM WHIFF witness (J2 s66, kept through the s68
             * restage): a directly-spawned worm 9 u dead ahead;
             * heavy 3 must pass straight through it (func_00183AC0
             * rejects model 0xD — worms are NOT melee victims). */
            if (mt_worm < 0) {
                float fx = sinf(g.yaw), fz = cosf(g.yaw);
                float wp[3] = { g.pos[0] + fx * 9.0f, g.pos[1],
                                g.pos[2] + fz * 9.0f };
                mt_worm = em_enemy_add(em_frame_gfx(), wp,
                                       g.yaw + EM_PI);
                if (mt_worm < 0) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — worm witness "
                           "spawn failed\n");
                    goto finish;
                }
                g.mt_mark = n;
            } else if (!mt_j3) {
                if (n < g.mt_mark + 5) break;   /* let its INIT run */
                if (em_weapon_is_melee() || player_damage_locked())
                    break;
                move_test_inject('j', 1);       /* SQUARE — heavy 3 */
                mt_j3 = n;
            } else {
                if (n == mt_j3 + 2) move_test_inject('j', 0);
                if (n > mt_j3 + 4 && !em_weapon_is_melee()) {
                    if (!(em_weapon_melee_hits() == 1 &&
                          em_enemy_state(mt_worm) == EM_ENEMY_ATTACK &&
                          em_enemy_hp(mt_worm) == 10)) {
                        g.mt_fail++;
                        printf("melee test: CHECK FAILED — heavy 3 "
                               "must WHIFF through the worm (hits %d, "
                               "worm state %d hp %d)\n",
                               em_weapon_melee_hits(),
                               em_enemy_state(mt_worm),
                               em_enemy_hp(mt_worm));
                    }
                    g.mt_phase = 6;
                    g.mt_mark  = n;
                }
            }
            break;
        case 6:
            /* the worm clears itself: its OWN lunge resolve bursts it
             * on the player (latch 15 — health reported, not
             * asserted); wait out the flinch before the whiff combo. */
            if (em_enemy_state(mt_worm) == EM_ENEMY_FREE &&
                !em_weapon_is_melee() && !player_damage_locked()) {
                g.mt_phase = 7;
                g.mt_mark  = n;
            }
            break;
        case 7:
            /* whiff combo: tap L now and during each swing (the chain
             * windows are forgiving — any in-swing press buffers). */
            if (n == g.mt_mark + 2 || n == g.mt_mark + 14 ||
                n == g.mt_mark + 44)
                move_test_inject('l', 1);
            else if (n == g.mt_mark + 4 || n == g.mt_mark + 16 ||
                     n == g.mt_mark + 46)
                move_test_inject('l', 0);
            else if (n > g.mt_mark + 50 && !em_weapon_is_melee()) {
                if (!(chain12 && chain23)) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — whiff combo did "
                           "not chain (0x10B->0x10C %d, 0x10C->0x10D "
                           "%d)\n", chain12, chain23);
                }
                if (whiff_recov) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — recover anim "
                           "played on a whiff (clip-end exit "
                           "expected)\n");
                }
                if (em_weapon_melee_swings() != 5 ||
                    em_weapon_melee_hits() != 1) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — counts (swings "
                           "%d expected 5: light + the worm whiff + 3 "
                           "whiffs; hits %d expected 1 — the bugs LATCH "
                           "and are shaken off, not stabbed; the worm is "
                           "not a melee victim)\n",
                           em_weapon_melee_swings(),
                           em_weapon_melee_hits());
                }
                /* s76: crate B never woke (the broadcast is inert — see
                 * phase 1), so it is STILL an IDLE crate and hatched no
                 * bugs; the only survivor is crate B itself, and the
                 * frontal cone is empty (which is why the whiffs
                 * whiffed). */
                if (em_enemy_state(1) != EM_ENEMY_IDLE) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — crate B should "
                           "still be IDLE (it never woke) (state %d)\n",
                           em_enemy_state(1));
                }
                int live_bugs = 0;
                for (int i = 0; i < 16; i++)
                    if (em_enemy_kind(i) == EM_ENEMY_KIND_BUG &&
                        em_enemy_state(i) == EM_ENEMY_ATTACK)
                        live_bugs++;
                if (em_enemy_alive() != 1 || live_bugs != 0) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — only the IDLE "
                           "crate B should survive (alive %d, live bugs "
                           "%d)\n", em_enemy_alive(), live_bugs);
                }
                goto finish;
            }
            break;
    }
    return;
finish:
    printf("melee test: %d swing(s), %d hit(s), %d shot(s), enemies "
           "alive %d, health %.0f — %s\n", em_weapon_melee_swings(),
           em_weapon_melee_hits(), em_weapon_shots(), em_enemy_alive(),
           g.status.health, g.mt_fail == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    em_frame_request_quit();
}

/* EM_DEATH_TEST=1 — player flinch/death/game-over/continue self-test
 * (the PLAYER DAMAGE & DEATH machine + the s66/s70 GO machine above).
 * Scene init spawns ONE
 * worm 30 u ahead (the contact-run placement); frame 0 sets health
 * to 20 (instrumentation — two lunges at the DECODED latch 15;
 * src/func_00154120.c state 3 writes D_008104D4 = 0x41700000 = 15.0f —
 * flinch then death; a worm suicide-bursts on its lunge connect, so
 * phase 2 spawns the second killer). Adaptive phases:
 *
 *   0  first lunge lands (health 20 -> 5): assert the FLINCH — state
 *      2 sub 0, an unarmed flinch clip (0x1E..0x21) committed, the
 *      worm burst.
 *   1  flinch plays out: assert the exit armed the i-frames and
 *      control returned (pd_state 0).
 *   2  i-frames expire: spawn worm B 30 u ahead.
 *   3  second lunge (health 5 -> 0): assert the DEATH — state 2 sub
 *      1, the death clip 0x2A (unarmed) committed, movement locked.
 *   4  death sequence: clip end -> corpse hold -> fade-out; assert
 *      full black, the machine parked (pd_phase 4) and the GAME-OVER
 *      screen armed (GO_SCREEN, hold = 240, fade-in started).
 *   5  GAME OVER shown (fade settled): assert the hold survived the
 *      fade-in (engine: the 240 counts THROUGH it), then SKIP with
 *      CROSS (the decoded D_00810E74 & 0x40) well before expiry.
 *   6  assert the skip was honored (GO_SCREEN_OUT with hold frames
 *      remaining — skippable witnessed, not a timeout).
 *   7  CONTINUE prompt up (fade settled): assert the from-death
 *      cursor INIT = 1 (func_001AC480 sub 0 on D_00275BDC), then
 *      press d-pad UP (engine 0x1000).
 *   8  assert the cursor walked to 0 (option move + sound), then
 *      CONFIRM with START (engine mask 0x840 = START|CROSS).
 *   9  dispatch at hold-black -> restart: the 001AF2C0 reset
 *      (health 100, mag 30, reserve 60, battery 0), D_00810700-block
 *      progress bytes cleared, scene switched to AREA11 (001AD360
 *      step 4), opening re-armed, damage machine cleared, death pose
 *      released.
 *  10  fade-in running after the restart.
 *
 * PASS = all checks green; any phase timing out fails the run. */
static void gt_check(int cond, const char *what)
{
    if (cond) return;
    g.gt_fail++;
    printf("death test: CHECK FAILED — %s (pd %d/%d/%d hp %.0f clip "
           "0x%X go %d)\n", what, g.pd_state, g.pd_sub, g.pd_phase,
           g.status.health, g.pd_clip, g.go_state);
}

static void death_test_script(void)
{
    int n = g.frame_no;
    /* the restart re-arms frame 0 — only the FIRST frame 0 seeds */
    if (n == 0 && g.gt_phase == 0 && !g.gt_mark) {
        g.gt_mark = 1;
        g.status.health = 20.0f;    /* instrumentation: 2 lunges (15
                                     * each, the decoded latch) kill  */
        g.gt_health0 = g.status.health;
        printf("death test: health set to %.0f, worm 30 u ahead\n",
               g.status.health);
        return;
    }
    if (g.gt_phase < 5 && n >= 3000 && !g.go_restart) {
        g.gt_fail++;
        printf("death test: CHECK FAILED — timed out in phase %d "
               "(frame %d)\n", g.gt_phase, n);
        goto finish;
    }
    switch (g.gt_phase) {
        case 0:
            if (g.status.health < g.gt_health0) {
                /* lunge landed THIS frame; the flinch entered the same
                 * frame (processor), the clip commits next actor tick
                 * — sample at +3 */
                g.gt_phase = 1;
                g.gt_mark  = n + 3;
                gt_check(g.status.health == g.gt_health0 - 15.0f,
                         "first lunge dealt the decoded latch (15)");
            }
            break;
        case 1:
            if (n == g.gt_mark) {
                unsigned c = em_game_anim_active();
                g.gt_flinch = c;
                gt_check(g.pd_state == 2 && g.pd_sub == 0,
                         "flinch state entered (state 2 sub 0)");
                gt_check(c >= PD_CLIP_FLINCH_A0 && c <= PD_CLIP_FLINCH_B1,
                         "an unarmed flinch clip (0x1E..0x21) committed");
                gt_check(em_enemy_alive() == 0,
                         "worm burst on its lunge connect");
                g.gt_phase = 2;
            }
            break;
        case 2:
            if (g.pd_state == 0) {
                gt_check(g.pd_iframes > 0,
                         "flinch exit armed the i-frames (+0x20E)");
                g.gt_phase = 3;
            }
            break;
        case 3:
            if (g.pd_iframes == 0) {
                float ep[3] = { g.pos[0] + sinf(g.yaw) * 30.0f, g.pos[1],
                                g.pos[2] + cosf(g.yaw) * 30.0f };
                if (em_enemy_add_kind(em_frame_gfx(),
                                      EM_ENEMY_KIND_CRAWLER, ep,
                                      g.yaw + EM_PI) < 0) {
                    g.gt_fail++;
                    printf("death test: worm B spawn failed\n");
                    goto finish;
                }
                g.gt_health0 = g.status.health;
                g.gt_phase   = 4;
            }
            break;
        case 4:
            if (g.status.health <= 0.0f) {
                g.gt_phase = 5;
                g.gt_mark  = n + 3;
            }
            break;
        case 5:
            if (n == g.gt_mark) {
                gt_check(g.pd_state == 2 && g.pd_sub == 1,
                         "death state entered (state 2 sub 1)");
                gt_check(em_game_anim_active() == PD_CLIP_DEATH,
                         "the death clip 0x2A committed");
                g.gt_phase = 6;
                g.gt_mark  = n;
            }
            break;
        case 6:
            /* ride the sequence out: clip (130) + hold (120) + fade
             * (64) — the GAME-OVER screen must arm */
            if (g.go_state == GO_SCREEN) {
                gt_check(g.pd_phase == 4, "death machine parked");
                gt_check(g.go_hold > GO_HOLD_FRAMES - 4 &&
                         g.go_hold <= GO_HOLD_FRAMES,
                         "GAME OVER hold armed at 240 (task+0x18)");
                g.gt_phase = 7;
                g.gt_mark  = n;
            } else if (n > g.gt_mark + 500) {
                g.gt_fail++;
                printf("death test: CHECK FAILED — game over never "
                       "armed (pd %d/%d/%d fade %.2f)\n", g.pd_state,
                       g.pd_sub, g.pd_phase, em_frame_fade_level());
                goto finish;
            }
            break;
        case 7:
            /* the screen fades IN (engine wait sub 2/3); once the
             * fade settles, SKIP with CROSS well before the 240
             * expires (the hold keeps counting through the fade) */
            if (g.go_state == GO_SCREEN &&
                em_frame_fade_level() <= 0.0f) {
                gt_check(g.go_hold > 0 && g.go_hold < GO_HOLD_FRAMES,
                         "hold counting through the fade-in, screen "
                         "shown before expiry");
                move_test_inject('k', 1);            /* CROSS (0x40) */
                g.gt_phase = 8;
                g.gt_mark  = n + 2;
            } else if (n > g.gt_mark + 400) {
                gt_check(0, "GAME OVER screen faded in");
                goto finish;
            }
            break;
        case 8:
            if (n == g.gt_mark)
                move_test_inject('k', 0);
            if (g.go_state == GO_SCREEN_OUT) {
                gt_check(g.go_hold > 0,
                         "CROSS skip honored with hold frames left "
                         "(skippable, not a timeout)");
                g.gt_phase = 9;
                g.gt_mark  = n;
            } else if (n > g.gt_mark + 100) {
                gt_check(0, "CROSS skipped the GAME OVER hold");
                goto finish;
            }
            break;
        case 9:
            /* the CONTINUE prompt fades in (the task-replacement
             * point); assert the decoded from-death cursor init then
             * walk it UP to option 0 */
            if (g.go_state == GO_PROMPT &&
                em_frame_fade_level() <= 0.0f) {
                gt_check(g.go_cursor == GO_CURSOR_DEATH,
                         "from-death cursor init = 1 (func_001AC480 "
                         "sub 0)");
                move_test_inject(EM_KEY_UP, 1);      /* d-pad 0x1000 */
                g.gt_phase = 10;
                g.gt_mark  = n + 2;
            } else if (n > g.gt_mark + 400) {
                gt_check(0, "CONTINUE prompt faded in");
                goto finish;
            }
            break;
        case 10:
            if (n == g.gt_mark)
                move_test_inject(EM_KEY_UP, 0);
            if (n == g.gt_mark + 4) {
                gt_check(g.go_cursor == 0,
                         "d-pad UP walked the cursor to CONTINUE (0)");
                move_test_inject(EM_KEY_RETURN, 1);  /* START (0x800) */
                g.gt_mark = n + 2;
                g.gt_phase = 11;
            }
            break;
        case 11:
            if (n == g.gt_mark)
                move_test_inject(EM_KEY_RETURN, 0);
            if (g.go_state == GO_PROMPT_CONFIRM && g.gt_mark > 0) {
                g.gt_mark = 0;       /* confirmed; wait for the reload */
            } else if (g.frame_no <= 2 && g.go_state == GO_OFF) {
                /* the restart re-armed the frame machine: frame_no
                 * restarted at 0 (scene-init) — sample the fresh
                 * state on its first frames */
                /* Option 0 = the 001AF2C0 new-game reset + the
                 * 001AD360 area 0x0B/0/0 commit (was: the port's old
                 * 75/60/4/120/4-6 demo status, an invention). */
                gt_check(g.status.health == 100.0f &&
                         g.status.infection == 0.0f &&
                         g.status.mag == 30 && g.status.reserve == 60 &&
                         g.status.battery == 0 &&
                         g.status.battery_max == 0,
                         "001AF2C0 new-game status on restart");
                gt_check(g.opening_complete == 0 &&
                         g.opening_event_39 == 0 &&
                         g.terminal_powered == 0,
                         "D_00810700-block progress bytes cleared");
                gt_check(strcmp(g.scene_dir, AREA11_SCENE_DIR) == 0,
                         "restart area is AREA11 (001AD360)");
                gt_check(em_opening_runtime_busy(),
                         "AREA11 opening controller re-armed");
                gt_check(g.pd_state == 0 && g.pd_iframes == 0,
                         "damage machine cleared on restart");
                gt_check(em_game_anim_active() == 0,
                         "death pose released on restart");
                g.gt_phase = 12;
                g.gt_mark  = 0;
            }
            break;
        case 12:
            /* one more frame so the fade-in is observable */
            gt_check(em_frame_fade_level() < 1.0f,
                     "fade-in running after restart");
            goto finish;
    }
    return;
finish:
    printf("death test: flinch clip 0x%X, death clip 0x%X, health "
           "%.0f, game-over screen %s, cross-skip %s, prompt cursor "
           "%s, continue %s — %s\n", g.gt_flinch,
           g.pd_clip ? g.pd_clip : PD_CLIP_DEATH, g.status.health,
           g.gt_phase >= 8 ? "shown" : "NOT shown",
           g.gt_phase >= 9 ? "ok" : "NOT taken",
           g.gt_phase >= 10 ? "ok" : "NOT seen",
           g.gt_phase >= 12 ? "ok" : "NOT reached",
           g.gt_fail == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    em_frame_request_quit();
}

/* The env-gated head of the gameplay frame (moved verbatim out of
 * gameplay_frame, S5 of docs/SCENE_COORDINATOR_DESIGN.md): each
 * EM_*_TEST script and each EM_CAPTURE_* input injection, in the order
 * they ran there. Port-only instrumentation: no original function
 * corresponds to it. gameplay_frame calls it first, before the status and
 * game-over gates, exactly where the block used to sit. */
void em_game_selftest_pre_frame(void)
{
    if (g.move_test) move_test_script();    /* debug instrumentation only */
    if (g.door_test) door_test_script();    /* debug instrumentation only */
    if (g.transit_test) transit_test_script(); /* debug instrumentation  */
    if (g.slider_test) slider_test_script();   /* debug instrumentation  */
    if (g.locked_test) locked_test_script();   /* debug instrumentation  */
    if (g.weapon_test) weapon_test_script();/* debug instrumentation only */
    /* EM_CAPTURE_AIM=1: hold R1 (key E) from frame 0 — by the default
     * capture frame (60) the draw has finished and the capture shows the
     * held aim pose, the laser pass and the lowered camera target. A
     * short turn-in-place (frames 28..44, ~27 deg) angles the laser off
     * the camera axis so the beam and hit dot are not occluded by the
     * player's own torso in the capture.
     * EM_CAPTURE_AIM=2: same, plus ONE semi shot (L = CIRCLE) at frame
     * 57 — it lands at 58, so the default capture frame 60 samples the
     * 0x112 recoil replay ~2 frames in (the clip's max-delta snap zone,
     * 4 deg/frame) with the muzzle flash still up: the aim+fire
     * reference against the =1 static hold. */
    /* EM_CAPTURE_AIM=3: aim + hold stick DOWN ('s') from frame 20 —
     * the inverted-Y steer pitches the aim UP: the capture samples the
     * up-ladder pose, the raised laser and the counter-lowered mode-1
     * eye. =4: hold stick UP ('w') — aim DOWN ("W = down").
     * EM_CAPTURE_AIM=5: aim NEAR A WALL — run at the camera (the RISE
     * approach) until the player reaches the office south wall, turn
     * to face back INTO the room, then hold R1: the desired aim eye
     * (30 u behind the facing) lands inside the wall and the DECODED
     * func_0018F870 PULLS IT IN at constant height (hit + 0.5 along
     * the player->eye ray) — the over-shoulder view framed from the
     * wall plane, no rise, no slide-away ("R1 keeps the pull-in").
     * RE-VERIFIED in src/func_0018F870.c (NEARMISS, logic
     * authoritative): the step-1 probe runs focus -> normalize(eye -
     * focus) * 1.5 + eye, and on a hit the scratch vector is re-derived
     * as the UNIT focus->eye direction and the eye is written
     * `self+0x10 += 0.5f * dir.x` / `self+0x18 += 0.5f * dir.z` off the
     * hit point — X and Z only, self+0x14 (Y) untouched. So "0.5 units
     * off the surface, height held" is literal, not an approximation.
     * SCOPED (audit 2026-07-31): that is the WALL arm, which is the one
     * this capture exercises. The sibling floor/ceiling arm (surface
     * flags +0x1A & 0xD800) copies the whole hit quad into self+0x10
     * with func_00102948 first, so it DOES move the eye Y before
     * applying the same 0.5 X/Z push — "height held" is the wall case
     * only, not the function as a whole.
     * Default capture frame 420; EM_CAMERA_TRACE=1 prints the solve. */
    if (g.capture_aim == 5) {
        if      (g.frame_no == 0)   move_test_inject('s', 1);
        else if (g.frame_no == 300) move_test_inject('s', 0);
        else if (g.frame_no == 310) move_test_inject('w', 1);
        else if (g.frame_no == 322) move_test_inject('w', 0);
        else if (g.frame_no == 330) move_test_inject('e', 1);
    } else if (g.capture_aim) {
        if      (g.frame_no == 0)  move_test_inject('e', 1);
        if (g.capture_aim < 3) {
            if      (g.frame_no == 28) move_test_inject('a', 1);
            else if (g.frame_no == 44) move_test_inject('a', 0);
        } else if (g.frame_no == 20) {
            move_test_inject(g.capture_aim == 3 ? 's' : 'w', 1);
        }
        if (g.capture_aim == 2) {
            if      (g.frame_no == 57) move_test_inject('l', 1);
            else if (g.frame_no == 59) move_test_inject('l', 0);
        }
    }                                       /* debug instrumentation only */
    /* EM_CAPTURE_DOOR=1: the door-test approach + CROSS with NO asserts
     * — by the default door capture frame (110) the transit walk has
     * arrived and the op 0x0D sub 5 CINEMATIC CUT holds: 20 u behind
     * the staging point along the through-door axis at +19, looking at
     * the player in the doorway (live-verified geometry). */
    if (g.capture_door) {
        if      (g.frame_no == 0)  move_test_inject('w', 1);
        else if (g.frame_no == 24) move_test_inject('w', 0);
        else if (g.frame_no == 28) move_test_inject('w', 1);
        else if (g.frame_no == 58) move_test_inject('w', 0);
        else if (g.frame_no == 60) move_test_inject('k', 1);
        else if (g.frame_no == 61) move_test_inject('k', 0);
    }                                       /* debug instrumentation only */
    /* EM_CAPTURE_SUPPLY=1: approach the office DOUBLE DOORS + CROSS —
     * the same-scene transit walks the player into the SUPPLY ROOM and
     * its spawn-record FIXED camera (camregion line, decoded eye
     * (116, 33, -300)) pins the view from the room corner by the
     * default capture frame (360). */
    if (g.capture_supply) {
        if      (g.frame_no == 0)  move_test_inject('w', 1);
        else if (g.frame_no == 40) move_test_inject('w', 0);
        else if (g.frame_no == 45) move_test_inject('k', 1);
        else if (g.frame_no == 46) move_test_inject('k', 0);
    }                                       /* debug instrumentation only */
    /* EM_CAPTURE_EXAMINE=1: place the player 6 u south of the scene's
     * FIRST examine object (inside the facing auto-pass ring; the snow
     * scene's = the AREA11 switch — the spawn-side approach corridor
     * is wall-blocked in the exported collision, so the capture
     * teleports to the ring instead of walking) and CROSS at frame 20
     * — the capture (default frame 60) samples the input-paused
     * refusal line "Switch / No power..." (GLOBAL line 0x1A)
     * presenting over the scene. */
    if (g.capture_examine) {
        float xp[3];
        if (g.frame_no == 1 &&
            em_examine_pos(g.capture_examine - 1, xp)) {
            g.pos[0] = xp[0] - 2.0f;   /* a step aside so a fixed
                                        * camera cue is not filled by
                                        * the player model; dist 4.0
                                        * fits the tightest (5-u
                                        * archetype-1) ring */
            g.pos[1] = xp[1];
            g.pos[2] = xp[2] - 3.5f;
            g.yaw    = 0.0f;               /* facing +Z = the object */
            g.cam.state = 0;               /* re-seat the chase camera */
            printf("capture-examine: placed at (%.1f, %.1f, %.1f)\n",
                   g.pos[0], g.pos[1], g.pos[2]);
        }
        else if (g.frame_no == 20) move_test_inject('k', 1);
        else if (g.frame_no == 22) move_test_inject('k', 0);
    }                                       /* debug instrumentation only */
    /* EM_CAPTURE_RISE=1: hold 's' (stick down) from frame 0 — the
     * player about-faces and runs TOWARD the camera; the chase camera
     * backs away until the room's far wall blocks its desired eye and
     * the decoded func_0018DD20 solve PULLS IT IN at constant height
     * (cam_solver_0018DD20, em_camera.c; CONFIRMED against
     * src/func_0018DD20.c [NEARMISS] — SCOPED (audit 2026-07-31): the
     * style-0 plain-WALL arm, i.e. the `resolved == 0` tail, adds
     * 0.5f * push to pos[0]/pos[2] only and never pos[1]. The
     * floor/ceiling sibling (+0x1A & 0x8800) does `pos[1] -= 1.0f`
     * first, and the style-3 arm pushes all three components, so
     * "constant height" is this capture's arm, not the function):
     * the eye parks 0.5 u off the wall while
     * the player keeps closing, so the view tilts down over the
     * player's head — the user-observed "rise to show the player's
     * top", emergent. Use with EM_CAPTURE_FRAME around 280+ (office
     * scene: the eye meets the south wall after ~9 s of running).
     * EM_CAMERA_TRACE=1 prints the per-frame solve for verification. */
    if (g.capture_rise) {                   /* debug instrumentation only */
        if (g.frame_no == 0)
            move_test_inject('s', 1);
        /* release phase (after the default capture window): walk back
         * AWAY from the wall — once the desired eye has room again the
         * solver stops responding and the chase returns the camera to
         * its full distance/height ("returns when clear", emergent). */
        else if (g.frame_no == 360) move_test_inject('s', 0);
        else if (g.frame_no == 370) move_test_inject('w', 1);
        else if (g.frame_no == 600) move_test_inject('w', 0);
    }
    /* EM_CAPTURE_ORIENT=1: Cmd (WALK-band hold) + 'a' for 30 frames —
     * the facing swings ~90 deg (the same facing-seek runs for every
     * moving gait) while gait-2 WALK drifts ~3 u left — then idle.
     * (2026-06-11 GAIT HOLD TIERS, em_input.h: the WALK hold moved from
     * Option to COMMAND; Option is now the gait-1 TURN/creep hold —
     * deliberately reachable again from the keyboard.) After
     * CAM_AUTO_DELAY the camera slowly auto-orients behind the new
     * facing (EM_CAMERA_TRACE prints the seek); capture late (~frame
     * 450) to see it settled. */
    if (g.capture_orient) {
        if (g.frame_no == 0) {
            move_test_inject(EM_KEY_CMD, 1);
            move_test_inject('a', 1);
        } else if (g.frame_no == 30) {
            move_test_inject('a', 0);
            move_test_inject(EM_KEY_CMD, 0);
        }
    }                                       /* debug instrumentation only */
    /* EM_CAPTURE_WALK=1: hold 'w' (run AWAY from the camera) — by the
     * capture frame the chase rides the tow-rope at the MOVING-PLAYER
     * heights (eye +19 / target +17, the s71 idle-row correction): a
     * level over-the-shoulder framing, NOT the old dive toward the
     * feet. The print at the capture frame is the height witness. */
    if (g.capture_walk) {
        if (g.frame_no == 0)
            move_test_inject('w', 1);
        else if (g.frame_no == g.capture_frame)
            printf("capture-walk: frame %d player y %.2f cam eye y %.2f "
                   "tgt y %.2f (moving heights: eye +%.1f tgt +%.1f)\n",
                   g.frame_no, g.pos[1], g.cam.eye[1], g.cam.tgt[1],
                   g.cam.eye[1] - g.pos[1], g.cam.tgt[1] - g.pos[1]);
    }                                       /* debug instrumentation only */
    /* EM_CAPTURE_LOCKED=N (run with EM_SCENE=assets/scene_drawbridge):
     * the m15 LOCKED security door (placement (-20.5, 0, -192) yaw 0,
     * doorway center (-25.5, -192)) tried from two different APPROACH
     * ORIENTATIONS — variant 1 stands square south of the center
     * facing +Z, variant 2 stands oblique to the SW facing the center;
     * the re-seated chase camera therefore arrives at the CROSS with
     * two different live yaws. The locked-look cut (func_001BBBF0)
     * must park the SAME camera both runs (the engine's D_00810374 =
     * last-SCRIPTED yaw, ported as the kickoff snap yaw — never the
     * live chase heading): the print at the capture frame is the
     * determinism witness, the BMPs the visual one. */
    if (g.capture_locked) {
        if (g.frame_no == 1) {
            if (g.capture_locked == 1) {
                g.pos[0] = -25.5f; g.pos[2] = -201.0f;
                g.yaw     = 0.0f;           /* square on, facing +Z */
                g.cam.yaw = 0.0f;           /* prior camera: behind */
            } else {
                g.pos[0] = -21.0f; g.pos[2] = -200.0f;
                g.yaw     = -0.512f;        /* oblique SE, facing the
                                             * doorway center */
                g.cam.yaw = -2.2f;          /* prior camera: swung way
                                             * around — the live-yaw
                                             * input the OLD locked
                                             * look wrongly consumed */
            }
            g.pos[1] = 0.0f;
            g.cam.state = 0;                /* re-seat the chase camera
                                             * behind the variant yaw */
            printf("capture-locked: variant %d at (%.1f, %.1f, %.1f) "
                   "yaw %.3f cam yaw %.3f\n", g.capture_locked,
                   g.pos[0], g.pos[1], g.pos[2], g.yaw, g.cam.yaw);
        } else if (g.frame_no == 50) {
            move_test_inject('k', 1);       /* CROSS — the locked try */
        } else if (g.frame_no == 52) {
            move_test_inject('k', 0);
        } else if (g.frame_no == g.capture_frame) {
            printf("capture-locked: variant %d frame %d cam eye "
                   "(%.3f, %.3f, %.3f) tgt (%.3f, %.3f, %.3f)\n",
                   g.capture_locked, g.frame_no,
                   g.cam.eye[0], g.cam.eye[1], g.cam.eye[2],
                   g.cam.tgt[0], g.cam.tgt[1], g.cam.tgt[2]);
        }
    }                                       /* debug instrumentation only */
    if (g.enemy_test) enemy_test_script();  /* debug instrumentation only */
    if (g.melee_test) melee_test_script();  /* debug instrumentation only */
    if (g.death_test) death_test_script();  /* debug instrumentation only */
    if (g.sfx_test)  sfx_test_script();     /* debug instrumentation only */
    if (g.pause_test) pause_test_script();  /* debug instrumentation only */
    if (g.pickup_test) pickup_test_script();/* debug instrumentation only */
    if (g.examine_test) examine_test_script();          /* same */
    if (g.camregion_test) camregion_test_script(); /* debug instr. only  */
    if (g.cine_test) cine_test_script();    /* debug instrumentation only */
    if (g.aim_test)  aim_test_script();     /* debug instrumentation only */
}
