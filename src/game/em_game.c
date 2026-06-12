/* em_game.c — slot-0 game task chain (PS2 -> native mapping).
 *
 * Engine chain (FINDINGS.md "ENGINE FRAME ANATOMY") and what stands in for
 * each stage here:
 *
 *   func_001AB7E0  boot/flow task        game_boot_task() — one frame:
 *                                        loads the player model + scene
 *                                        parts, then re-registers slot 0
 *                                        with the game task (exactly how
 *                                        the engine replaces it live).
 *   func_001ACEC0  game task machine     game_task() — switch on the slot
 *                  (state byte task+8)   record's user byte 0.
 *   func_001AD250  sub-machine           game_sub_machine() — user byte 1;
 *                  (6 states, jr-table   the live arm reaches the in-game
 *                  0x0026DCB0)           frame machine through the
 *                                        trampoline func_001AD4D0
 *                                        (= j func_001AE040).
 *   func_001AE040  in-game frame machine ingame_frame_machine() — user
 *                  (state byte task+0xB) byte 3 (task+0xB). Its live state
 *                                        runs per-frame services (flag
 *                                        reset, placement, camera, HUD
 *                                        context, end-of-level poll — all
 *                                        skeleton no-ops here), then the
 *                                        selector (scratchpad 0x70003B8D)
 *                                        picks the frame variant.
 *   func_001AE5E0  GAMEPLAY FRAME        gameplay_frame() — see below.
 *   func_001AE6B0  cutscene variant      cutscene_frame() — skeleton; the
 *                                        native selector never routes here
 *                                        yet.
 *
 * GAMEPLAY FRAME stages (func_001AE5E0) -> native:
 *   func_001CB590 actor-context begin    actor_context_begin() — marks the
 *                                        actor table the update writes
 *                                        (the engine sets the D_00275B40
 *                                        node-table base; one player actor
 *                                        natively, so it just selects it).
 *   func_0015BCF0 PLAYER ACTOR UPDATE    actor_update() — the port's anim
 *                                        advance: evaluate the bone palette
 *                                        at the current clip time and place
 *                                        the actor at its world position
 *                                        (the engine's anim-evaluator +
 *                                        physics spine, asset-side only
 *                                        for now).
 *   func_001CB5A0 actor-context end      actor_context_end().
 *   func_001D1C50 RENDER CHAIN BUILD     render_chain_build() — records
 *                                        this frame's draws (scene parts +
 *                                        player, or the test triangle) into
 *                                        a chain, the native form of the
 *                                        VIF packet chain. The PS2 kick
 *                                        streams packets whose camera
 *                                        matrix slot is filled afterwards;
 *                                        natively the recorded chain is
 *                                        flushed after camera apply, at
 *                                        close-out.
 *   func_001C1D00 render-env init        render_env_init() — once-per-
 *                  (flag block           area render-env setup (GS regs,
 *                  0x008101D0)           per-area specials). NOT camera
 *                                        math (corrected by FINDINGS.md
 *                                        "CAMERA SYSTEM"); skeleton no-op.
 *   func_001AFD70 / func_0015C160 /      world services — skeleton no-ops.
 *   func_001F0360
 *   func_001CB590(0x008101E0, 0xD0, 0)   camera_update() — THE CAMERA:
 *   + func_0018B9C0 camera machine       camera-context begin + the
 *                                        camera state machine (struct
 *                                        0x008101E0; see the CAMERA
 *                                        section below).
 *   func_001CB5A0 / func_001AAD00 /      frame_close_out() — flushes the
 *   func_001D1EA0(1) close-out           draw chain with the committed
 *                                        camera and advances clip time.
 *
 * INTERACTIVE MOVEMENT (first slice of the real actor spine): the frame
 * input block drives the player around the room. Left stick (WASD)
 * moves camera-relative on the XZ plane through the engine's ANALOG
 * GAIT quantizer (rings 48/88/122 -> turn-in-place / walk 6 u/s /
 * run 18 u/s; keyboard full push = run, Alt = the walk band); the
 * facing yaw seeks the movement direction (smooth turn). The placement
 * is composed onto the evaluated anim palette each frame (rotation
 * about Y by yaw, then translation — AFTER the animation pose; see
 * palette_apply_placement). Movement runs through the engine's
 * COLLISION WORLD (src/game/em_collision.[hc] over the id 0x44 EMCL
 * bake): the engine's own RADIAL WALL PROBES (five directions, the
 * 4.5-unit player radius — see PLAYER WALL RADIUS below) push the
 * player out of walls, and a vertical segment query sets the floor
 * height; with no collision asset the old room-bbox clamp remains.
 * The camera is the engine's own chase camera (clamped proportional
 * follow, FINDINGS.md "CAMERA SYSTEM" port contract) with its
 * desired-eye solver the DECODED func_0018DD20 (style 0, mask 6 —
 * cam_solver_0018DD20 below) against the same world; the player has
 * NO free camera control — R1/L1 orient the camera behind the player,
 * idle auto-orients slowly, and a blocking wall PULLS the eye in at
 * constant height (which reads as the camera rising over the player —
 * the CAMERA FIDELITY block below). While the status screen is open the world
 * simulation PAUSES (the gate in gameplay_frame). Esc still quits
 * (em_frame.c step C).
 *
 * SCENE MANIFEST: assets/scene/scene.txt (see scene_manifest_load) gives
 * each scene its own spawn, collision filename and optional bgm in TRUE
 * world coordinates — written by the decomp repo's exporters, read once
 * at boot. No manifest = the office defaults (historical behavior).
 *
 * Debug instrumentation (port-side): EM_CAPTURE=<path.bmp> requests a BMP
 * capture at gameplay frame 60 (override with EM_CAPTURE_FRAME=<n>) and
 * quits one frame later, preserving the pre-architecture shell's headless
 * regression behavior bit-for-bit at the default frame.
 * EM_MOVE_TEST=1 runs a deterministic movement self-test: a scripted key
 * sequence (60 frames forward, 30 frames right) is injected through the
 * real em_input event API, then the final position/yaw is printed and the
 * loop quits (see move_test_script). Combine with EM_CAPTURE to grab a
 * mid-walk frame (the move test suppresses the capture path's early quit).
 * EM_WEAPON_TEST=1 runs the scripted firing-loop self-test (draw, semi
 * fire, the empty-mag auto-reload, full-auto cadence, manual top-up,
 * holster — see weapon_test_script / em_weapon.h).
 */
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
#include "game/em_weapon.h"

#define MODEL_PATH     "assets/player.emdl"
#define SCENE_DIR      "assets/scene"   /* boot default; g.scene_dir is
                                         * the ACTIVE scene (runtime
                                         * switch: em_game_scene_switch) */
#define COLL_DEFAULT   "office.emcl"
#define SCENE_MAX      16

/* DEFAULT player spawn — the office room (chunk06.n1 level): the live
 * GS-dump capture has the character standing at ~(107.4, 0, -184); the
 * level floor there is y = 0 and the player EMDL is recentred at the
 * origin with its feet at y ~= 0. A scene manifest (scene.txt, below)
 * overrides this per scene; the office values stay as the no-manifest
 * defaults so an unmanifested assets/scene behaves exactly as before. */
static const float kPlayerPos[3] = { 107.4f, 0.0f, -184.0f };

/* Walkable-bounds FALLBACK: the office room's collision bbox (the id 0x44
 * file — FINDINGS.md "COLLISION WORLD", chunk06.n1). Used only when no
 * EMCL collision world is generated (assets/scene/office.emcl, from the
 * decomp repo's tools/export_collision.py); with the world loaded the
 * real engine queries run instead — em_collision_move_probe
 * (func_0019AD00 collide-and-slide) for movement and
 * em_collision_segment_query (func_0019A570) for the floor and the
 * camera solver. */
static const float kRoomMin[2] = { -3.6f,  -296.0f };  /* x, z */
static const float kRoomMax[2] = { 120.5f,    2.4f };

/* Vertical floor-probe window: the engine's actor spine resolves height
 * with separate vertical segment queries through the same hub (e.g. the
 * frozen scratchpad query in FINDINGS, a y+200..y-200 down-probe). The
 * port probes from step-height above the feet to a drop window below. */
#define FLOOR_PROBE_UP    8.0f
#define FLOOR_PROBE_DOWN  8.0f

/* (The old knee-height WALL_PROBE_LIFT and WALL_SKIN constants are
 * retired: wall response now runs the engine's own two-height radial
 * probes — ankle 0.05 / chest 4.01 with push-back, PLAYER WALL RADIUS
 * below — which never rest the probe START on a plane, so no skin is
 * needed, and the chest pass natively covers the snow-scene
 * foot-level sliver case the knee lift worked around.) */

/* Movement / camera tuning. The loop is vsync-locked at 60 Hz exactly
 * like the PS2 original, so a fixed dt keeps everything deterministic.
 *
 * ANALOG GAIT — TIER-RAMP CORRECTED (2026-06-11, user PCSX2 report
 * "full stick should RUN" + the func_0017BC40 re-decode; overturns the
 * s31 "gait = locIdx+1" reading AND the CURIOSITIES "unreachable
 * sprint"): the stick quantizer func_001B5CC0 (rings 48/88/122) picks
 * the gait byte 0..3, and func_00174AC0 maps it to a TARGET speed
 * +0x240 = {0, 0.1, 0.3, 0.8} u/tick. The locomotion tier locIdx
 * (+0x25C) only ENTERS at gait-1 (func_001612D0 state 2); from there
 * the ramp func_0017BC40 accelerates +0x38 by D_00248880[tier] per
 * frame and PROMOTES the tier (+0x25C += 1) each time the speed
 * crosses the next tier's D_00248870 value, until tier speed ==
 * target. anim_matrix_player re-requests the tier's clip every frame
 * (sub 1 blends TOWARD the next tier's clip mid-ramp). So sustained:
 *
 *   gait 1 = WALK  tier 1, anim id 1, 0.1 u/tick =  6 u/s
 *   gait 2 = JOG   tier 2, anim id 2, 0.3 u/tick = 18 u/s
 *   gait 3 = RUN   tier 3, anim id 3, 0.8 u/tick = 48 u/s
 *
 * Keyboard full push = RUN (the PCSX2 full-stick behavior); the input
 * layer's modifier caps land each hold one ring down — Cmd 0.8 (raw
 * ~102) = the gait-2 JOG band, Option 0.5 (raw 64) = the gait-1 WALK
 * band (em_input.h GAIT HOLD TIERS). WALK_SPEED stays as the
 * door-transit scripted MOVE-TO speed only (em_door's walk, not stick
 * locomotion — the historical port constant keeps the transit
 * timings). Stick release from tier 3 runs the engine's RUN-DOWN
 * (func_0017BC40 phase 2: 0.03125 u/tick decay to the tier-2 speed,
 * then stop; the carried-gear x2 decay and the mode-6 stop-skid anims
 * ids 4/5 are untranslated — flagged). */
#define FRAME_DT        (1.0f / 60.0f)
#define WALK_SPEED      15.0f   /* units/sec — scripted door MOVE-TO only */
#define GAIT_RING_1     48.0f   /* func_001B5CC0 rings, raw stick units */
#define GAIT_RING_2     88.0f
#define GAIT_RING_3     122.0f
#define GAIT_WALK_SPEED (0.1f * 60.0f)  /* D_00248870[1] u/tick @ 60 Hz */
#define GAIT_JOG_SPEED  (0.3f * 60.0f)  /* D_00248870[2] */
#define GAIT_RUN_SPEED  (0.8f * 60.0f)  /* D_00248870[3] — full stick */
/* Tier-ramp tables (ELF .data, re-dumped 2026-06-11): per-frame accel
 * D_00248880 while ramping OUT of tier i, decel D_00248890 while
 * ramping DOWN from tier i, and the phase-2 run-down decay. u/tick. */
#define GAIT_ACCEL_0    0.05f       /* D_00248880[0]: 0 -> 0.1   */
#define GAIT_ACCEL_1    0.05f       /* D_00248880[1]: 0.1 -> 0.3 */
#define GAIT_ACCEL_2    0.0625f     /* D_00248880[2]: 0.3 -> 0.8 */
#define GAIT_DECEL_2    0.025f      /* D_00248890[2]: 0.3 -> 0.1 */
#define GAIT_DECEL_3    0.0227273f  /* D_00248890[3]: 0.8 -> 0.3 */
#define GAIT_RUNDOWN    0.03125f    /* func_0017BC40 phase-2 decay
                                     * (0x3D000000; x2 with carried
                                     * gear — untranslated) */
#define TURN_SPEED      12.0f   /* rad/sec — facing seeks the move dir */
/* MANUAL AIM STEER — DECODED (2026-06-11, func_0017ABA0; retires the
 * old AIM_TURN_SPEED port stand-in). While aiming, the left stick
 * drives the aim BLEND PAIR player +0x27C (yaw) / +0x278 (pitch),
 * both 0.5-centered in [0,1] (stance entry resets them to 0.5):
 *
 *   - per-frame rate = a 4-band table indexed by the axis deflection
 *     band |raw - 0x80| through func_001B5DC0's rings 49/89/123:
 *     R1-family stances 0x31/0x34: {0, 0.0025, 0.005, 0.015};
 *     R2-family 0x32/0x35: {0, 0.0016667, 0.005, 0.015};
 *   - YAW: blend +- rate / sin(pi*(0.5 + 0.6*(pitch-0.5))) (faster
 *     when pitched off level; exactly 1.0 at center); overflow past
 *     [0,1] clamps the blend and TURNS THE BODY by the excess (rad) *
 *     1.0 (R1 family) / 1.5 (R2 family) — the pose pans up to its
 *     +-60 deg yaw ladder first, then the player turns;
 *   - PITCH: blend +- (rate * mult) / 2 per frame — INVERTED Y (the
 *     raw axis byte >= 0x80 = stick DOWN increments the blend = aim
 *     UP; stick UP aims DOWN — "W = down", the user-attested original
 *     behavior); mult = 1.5 outside [0.3, 0.7] for the R1 family (and
 *     x1.8 for sub-weapon 4, not in the port); clamp [0, 1.0] for
 *     stances 0x31/0x32, [0, 0.75] for 0x34/0x35.
 *
 * The blends select/blend the 9-step AIM POSE LADDER 0x112..0x11A
 * (FINDINGS "aim-ladder tables") — measured from the baked clips
 * themselves (hand-bone +X fire direction): 0x112 center (+1.3 deg),
 * 0x113 up +81.3, 0x114 down -78.7, 0x115/0x116 level yaw -+60 (model
 * -X = SCREEN RIGHT in the port's basis), 0x117/0x118 up/down right,
 * 0x119/0x11A up/down left. The fire/laser ray follows the posed hand
 * bone automatically (em_weapon's muzzle anchor). */
#define AIM_BAND_1      49.0f       /* func_001B5DC0 |raw-0x80| rings */
#define AIM_BAND_2      89.0f
#define AIM_BAND_3      123.0f
#define AIM_PITCH_MAX_R1 1.0f       /* +0x278 clamp, stances 0x31/0x32 */
#define AIM_POSE_UP_DEG   81.3f     /* measured ladder pose pitches */
#define AIM_POSE_DOWN_DEG 78.7f
#define AIM_POSE_CTR_DEG  1.3f
#define AIM_POSE_YAW_DEG  60.0f

/* PLAYER WALL RADIUS (decoded s38, FINDINGS "PLAYER WALL COLLISION
 * RADIUS"): the engine's wall response is NOT a zero-width move
 * segment — every frame the walk integrator func_001764E0 fires FIVE
 * radial probes of local vector (0, lift, 4.5) at yaw + D_00248950 =
 * {0, +45, -45, +90, -90} deg, at TWO heights: ankle y+0.05 (mask 6,
 * static world) and chest y+4.01 (mask 7, + movable hulls = doors),
 * and on a wall-class hit adds the hit-minus-end overshoot back into
 * actor x/z (the spad 0x700031C0 delta). The effective wall standoff
 * is 4.5 units — the old zero-radius pre-move probe is why the player
 * clipped halfway into walls. Sliding emerges exactly like the PS2:
 * only probes pointing into the wall push back, each along its own
 * direction, so motion parallel to the wall survives. */
#define PLAYER_WALL_RADIUS 4.5f   /* the (0, y, 4.5) local probe vector */
#define PROBE_ANKLE_LIFT   0.05f  /* loop-1 local y (sp+0x70 vector) */
#define PROBE_CHEST_LIFT   4.01f  /* loop-2 local y (D_002488C0) */

/* Animation clips + crossfade. Library clip ids (chunk28/f01_id3c —
 * for the player the anim id IS the container index, FINDINGS "ANIM ID
 * MAPPING"). The AUTHENTIC stick-locomotion ids — TIER-RAMP CORRECTED
 * 2026-06-11 (see ANALOG GAIT above; the s31 "full stick = id 2"
 * reading missed the func_0017BC40 tier promotion — the PCSX2 oracle
 * showed the port one tier slow everywhere):
 *
 *   stick deflection -> func_001B5CC0 quantizer (rings r=48/88/122) ->
 *   gait byte 0..3 (pad struct +0x17 = 0x810E57) -> player +0x23F ->
 *   target speed +0x240 (func_00174AC0) -> the tier ramp promotes
 *   locIdx +0x25C until D_00248870[locIdx] == target; anim id =
 *   D_00248AB0[mode 1][family*4 + locIdx] (func_0017B490/func_0017B460;
 *   unarmed family 0 row = {0, 1, 2, 3}).
 *
 * Sustained: gait 1 = WALK (id 1, 6 u/s), gait 2 = JOG (id 2,
 * 18 u/s), gait 3 full stick = RUN (id 3, 48 u/s). Tier 0 (id 0,
 * speed 0 — turn-in-place) is only the gait-1 ENTRY transient. All
 * three clips re-baked from the fixed-directory library 2026-06-11:
 * id 1 = 120-frame scissored stride (12.1 u root travel -> natural
 * 6.11 u/s; feet swing 7.3 u, arms 3.3 u); id 2 = 45-frame jog
 * (17.7 u -> 24.07 u/s; feet 8.4, arms 4.9) — the engine drives it at
 * 18 u/s = rate 0.75, exactly its hard-coded +0x204 = 0.75 at tier 2;
 * id 3 = 40-frame full arm-pump run (30.9 u -> 47.57 u/s; feet 10.1,
 * arms 6.8) at rate ~1.0 (0.8 u/tick = 48 u/s target). Playback rate
 * = move_speed / natural keeps the feet tracking the ground (stride
 * lock). Idle<->locomotion is a 0.15 s LINEAR palette blend — the
 * engine cross-fades clip transitions the same way (PROGRESS.md:
 * mid-blend live captures match no single clip); the mid-ramp
 * blend-toward-next-tier (anim_matrix_player sub 1) is approximated
 * by the same crossfade on the tier swap (flagged).
 *
 * IDLE CYCLE (decoded s38, FINDINGS "PLAYER IDLE CYCLE" — closes the
 * 2026-06-11 "true default-idle anim id" open item): the player
 * mode-0 top func_00161020 requests the BASE idle anim id 0 (the
 * 80-frame breathing idle; mode-0 family table D_00248A00[0] via
 * func_00174A50, blend arg 12.0 on entry), then runs a 300-frame
 * timer (+0x28 = 0x12C, 5 s). At zero it requests the IDLE FIDGET
 * id 0x15D = 349 — the 180-frame look-around (directory id; the
 * pre-fix exporter scan called this container "346") — with blend
 * arg 8.0, waits for the clip-end flag (+0x200 & 0x1000), then
 * re-requests the breathing idle (blend 8.0) and re-arms the timer:
 * the breathing/look-around loop you see when standing still. Stick
 * input, aim, melee or a scripted anim leaves mode 0 and resets the
 * cycle. Old assets without clip id 0 fall back to 349 alone (cycle
 * off). */
#define CLIP_ID_IDLE    0u      /* breathing idle (engine mode-0 base) */
#define CLIP_ID_FIDGET  349u    /* idle fidget 0x15D = look-around */
#define CLIP_ID_WALK    1u      /* tier 1 (gait 1 / Option hold) */
#define CLIP_ID_JOG     2u      /* tier 2 (gait 2 / Cmd hold) */
#define CLIP_ID_RUN     3u      /* tier 3 (gait 3 = full stick) */
#define WALK_CLIP_SPEED 6.11f   /* natural u/s at the baked 60 fps */
#define JOG_CLIP_SPEED  24.07f  /* jog clip natural speed (45 fr) */
#define RUN_CLIP_SPEED  47.57f  /* run clip natural speed (40 fr) */
#define IDLE_FIDGET_FRAMES 300  /* +0x28 timer re-arm value (0x12C) */
#define IDLE_BLEND_TIME (8.0f / 60.0f) /* the cycle's blend arg 8.0 */

/* STATUS-MENU UI SCENE (FINDINGS.md "STATUS-MENU UI SCENE DECODED" —
 * func_0020CDC0 state 0 + the menu-player behavior func_0020E6F0):
 * while the status screen is up, the engine's 3D frame IS the menu
 * scene — the camera matrix 0x810610 goes IDENTITY and a dedicated
 * static-array actor renders the player turntabling on black, under
 * the animated tile background and the panels. Decoded constants:
 *   placement  view-space (7.4, 2.4, 40) of the identity UI camera
 *              (pos = camCol3 + 40*colZ + 7.4*colX + 2.4*colY); the
 *              actor scale is never written (stays the alloc's 1.0)
 *   rotation   init (0, pi, 0) — facing the camera (the publisher
 *              func_0020EC80's diag(-1)*rotY(pi) flip makes it
 *              upright/front in the y-down view); yaw += 0.01
 *              rad/frame, wrapped > pi -> -2pi (one rev ~10.5 s)
 *   anim       displayed health > 35 -> clip 0x1C2 (450), a SINGLE-
 *              FRAME stance (static pose; all motion is the spin);
 *              <= 35 -> clip 0xA (10), the 90-frame low-health idle;
 *              advance 1.0/frame; recovery past 35 swaps back to
 *              0x1C2 (no swap TO 0xA mid-open — init only)
 *   tint       per-actor color delta (-0.8, -1.0, -0.3) * infection
 *              * ramp (G clamped >= -127), ramp breathing 1.0 <-> 1.3
 *              at +-0.01/frame — the 2 s infected-skin pulse
 * Port mapping: the native y-up view (em_mat4_lookat_gs remaps the
 * engine's y-down GS view) puts the engine's view-down offset at
 * negative y, and the engine's +x maps screen-LEFT (the remap's X
 * negation) — the model lands ON the ring gauge, the real screen's
 * placement. THE UI PROJECTION IS THE ENGINE'S WORLD P AT s = 480 —
 * READ LIVE s66: with the hub open, ctx+0x2468 = 480.37, UNCHANGED
 * from gameplay (the 0.37 is a stalled zoom-lerp tail); P at +0x2340
 * = the standard (0.8s, 0.5s) rows and V at +0x2380 = identity with
 * m11 = -1 (the UI camera y-flip). There is NO separate menu
 * projection; the s49 empirical tan(fovy/2) = 0.74 pin (which implied
 * a menu zoom s ~= 324) is DEAD. Whatever the 0.74 pin was
 * compensating for lives in the MODEL TRANSFORM/placement: the s49
 * actor-position decode (view-space (7.4, 2.4, 40), func_0020E6F0
 * state 0) cannot reproduce the observed screen framing under the
 * real s = 480 P (it would project the model tiny and near-centered),
 * so one link of that placement chain is misread — OPEN. The port
 * keeps the VALIDATED screen framing (the s49 anchors: ring-center
 * column canvas x 208 = NDC -0.1875, vertical anchor NDC +0.0811,
 * the same apparent size) by RE-DERIVING the view-space placement
 * under the engine projection — preserve the vertical per-unit scale
 * and both NDC anchors:
 *   UI_SCENE_Z = 40 * 0.74 / (224/480)     = 444/7 = 63.428571
 *   UI_SCENE_X = 0.1875 * (320/480) * Z    = 7.928571
 *   UI_SCENE_Y = 0.0811 * (224/480) * Z    = 2.4 (unchanged)
 * (FLAGGED port-derived placement; the engine's 10/7 pixel-aspect
 * anisotropy now applies to the menu model exactly as it does to the
 * world — the old square-pixel pin drew it ~7% too wide.) The
 * additive engine tint is approximated
 * multiplicatively over the GS 128 base: rgb_mul = (128 + delta)/128.
 * The rig is orbited to the gfx stand-in light's azimuth (relative
 * camera<->player transform unchanged) so the camera-facing side is
 * lit — also explained at ui_scene_render. */
#define UI_SCENE_X      7.928571f  /* re-derived under s=480 (above)  */
#define UI_SCENE_Y     -2.4f       /* engine view-down 2.4, native y-up */
#define UI_SCENE_Z      63.428571f /* re-derived under s=480 (above)  */
#define UI_SCENE_ZOOM   480.0f     /* ctx+0x2468 with the hub open —
                                    * the ORDINARY world zoom (s66)   */
#define UI_SPIN_RATE    0.01f   /* yaw rad per frame (engine 0x3C23D70A) */
#define UI_RAMP_RATE    0.01f   /* tint pulse step per frame */
#define UI_RAMP_MAX     1.3f    /* tint pulse ceiling (0x3FA66666) */
#define UI_LOW_HEALTH  35.0f    /* clip-select threshold (0x420C0000) */
/* The UI rig's orbit azimuth = the gfx stand-in light's horizontal
 * direction, normalized ((0.4, 0.45)/0.602 — see the camera note in
 * ui_scene_render). */
#define UI_CAM_DIR_X    0.6644f
#define UI_CAM_DIR_Z    0.7474f
#define CLIP_ID_MENU     450u   /* 0x1C2 — single-frame menu stance */
#define CLIP_ID_MENU_LOW 10u    /* 0xA — low-health menu idle (90 fr) */

/* FOOTSTEP TRIGGER FRAMES — the engine's per-anim-id property table
 * D_00248C90 (FINDINGS "ANIM ID MAPPING": frameA/frameB per row; the
 * per-frame func_00187350 fires the step sound + decal when the
 * committed clip time crosses them). Rows re-read from the user's
 * local boot ELF for the authentic ids: walk id 1 (120 fr) -> 72/21 —
 * the exact pair the s29 live capture metered at a partial-stick
 * gait; jog id 2 (45 fr) -> 26/3; run id 3 (40 fr) -> 21/2. Each
 * trigger plays the two-layer step — surface variant (material block
 * + tier sub-base) + gear/cloth variant, each with its own rand5 draw
 * (footstep_play below). */
#define WALK_STEP_FRAME_A 72.0f /* D_00248C90[1].frameA */
#define WALK_STEP_FRAME_B 21.0f /* D_00248C90[1].frameB */
#define JOG_STEP_FRAME_A  26.0f /* D_00248C90[2].frameA */
#define JOG_STEP_FRAME_B  3.0f  /* D_00248C90[2].frameB */
#define RUN_STEP_FRAME_A  21.0f /* D_00248C90[3].frameA */
#define RUN_STEP_FRAME_B  2.0f  /* D_00248C90[3].frameB */
#define ANIM_BLEND_TIME 0.15f   /* seconds, idle<->walk crossfade */
#define STICK_DEADZONE  0.25f
#define EM_PI           3.14159265f

/* PLAYER DAMAGE & DEATH (2026-06-11 damage-pipeline decode — the
 * engine's per-frame player damage processor func_0021C440, called at
 * the head of player states 1 and 2; the apply helpers func_0021C350
 * (health) / func_0021C270 (infection); the state-2 sub handlers
 * func_0021D800 (flinch), func_0021E240 (death), func_0021E830
 * (infected death); the terminal func_0021D2E0; the passive ticks
 * func_0015D100/func_0015D000; the kill-plane state-6 handler
 * func_0015D460).
 *
 * Engine model (player actor 0x008102B0):
 *   +0x224  pending HEALTH damage (float) — producers write it
 *           directly (leech latch 5.0 / leech lunge 15.0; s22b's
 *           "drain magnitude D_008104D4" IS this field); func_0021C350
 *           subtracts it from health +0x220, latches the low-health
 *           flag (+0x235 bit 0) at <= 35, floors at 0 with the event
 *           byte +0x00 = 2 (dying).
 *   +0x22C  pending INFECTION damage (float) — the breather-pad
 *           event-3 write (s33: pad sets +0x22C = 5.0); func_0021C270
 *           ADDS it to infection +0x228. At 100: infection clamps,
 *           health clamps to 60 and the display max swaps to 60
 *           (flag 0x8104E4 == player +0x234 — closes C14), the
 *           INFECTED latch +0x234 = 1 (with D_00810707/D_008106F1),
 *           sound 0x149 vol 300. This is the "Dennis Infected"
 *           consequence: NOT instant death — a reduced 60-HP cap plus
 *           a passive drain (func_0015D100: while +0x234 != 0, health
 *           -= 2.0 every 240 frames with effect 0x80000063; reaching
 *           0 sets event 2 with type 0x63 -> the INFECTED death).
 *   +0x0F   damage TYPE byte (D_008102BF) — typed/latch reactions
 *           1..0xB each pick their own state-2 sub + the marker anim
 *           ids 0x3B/0x3C/0x3E..0x40; only the GENERIC tail (type 0)
 *           is translated here — no typed producer exists natively
 *           yet (untranslated, flagged).
 *   +0x20E  post-hit invulnerability countdown — armed at flinch END
 *           (0x3C = 60 frames; 0x5A = 90 after a latch hit); the
 *           event byte returns to 1 (vulnerable) only at 0, and every
 *           producer requires event == 1, so the player is immune
 *           from hit-reaction start until the window expires.
 *
 * FLINCH (state 2 sub 0, func_0021D800): voice 0x152 (0x153 when the
 * hit was infection damage, +0x1F1 == 1) vol 300 + rumble; the REAL
 * clip is requested directly (the +0x1F0 = 0x3E write is a category
 * marker): RNG bit -> one of two flinch families, armed (+0x236)
 * 0x56/0x57, unarmed 0x1E/0x1F vs 0x20/0x21 (func_0021D1A0 picks the
 * side within the family — port: a second RNG bit, flagged),
 * infected 0x1C7. Clip end -> i-frames armed, exit to state 1.
 *
 * DEATH (state 2 sub 1, func_0021E240; health <= 0 in the processor):
 * phase 0 = rumble + voice 0x146 + body foley 0x151 (vol 300), clip
 * 0x2A (armed variant 0x5C via +0x236), root-motion mover; cues at
 * frames-remaining 80 (sound 0x156) and 16 (ground thud 0x14E,
 * infected 0x14F, + heavy rumble); clip end -> func_0021D2E0:
 * blood-pool effect 0x80000043 under the corpse (PORT: skipped, no
 * decal system — flagged), event = 2, corpse hold 0x78 = 120 frames,
 * then the standard fade-out func_001AEDE0(4,0). INFECTED death (sub
 * 3, func_0021E830) plays clip 0x1C4 (300 f succumb) with gore
 * effect 0x80000051 (PORT: skipped) and the same terminal.
 *
 * KILL PLANE (player spine, s15/s17): pos.y < -200 -> state 6
 * (func_0015D460): sub 0 zeroes health + event, sub 1 starts the same
 * fade-out, sub 2 parks. No anim, no sound.
 *
 * GAME OVER — THE DECODED CHAIN (s66 live + the s70 static decode of
 * func_001AD4E0 / func_001AC070 / func_001AC480; the old "screen id
 * D_008106CF via state 6" framing was the END-OF-LEVEL path —
 * D_008106CE/CF are never written on death):
 *
 *   vitals mirror (~0x0015CFB0)   health <= 0 latches D_008106B9 = 1
 *   func_001AE040 state-1 tail    latch && fade == 2 (hold-black) ->
 *                                 func_001AD140: game task 3/2
 *   GAME-OVER WAIT func_001AD4E0  sub 0: timer task+0x18 = 0xF0 = 240
 *                                 sub 1: busy gate; launch SCREEN
 *                                   MODULE 0x27 (func_001FF080(0,
 *                                   0x27)) — the GAME OVER art screen
 *                                 sub 2: module up -> FADE-IN
 *                                   (func_001AEE10) + audio cue
 *                                   func_001FA790(0, 0x1B) (the
 *                                   game-over jingle/stream — id
 *                                   identification flagged)
 *                                 sub 3: GS backdrop quad each frame
 *                                   (func_001ABF90); the 240 counts
 *                                   down THROUGH the fade-in; once
 *                                   the fade is idle, CROSS pressed-
 *                                   edge (D_00810E74 & 0x40 — the
 *                                   s-pad map pins 0x40 = CROSS) OR
 *                                   timer 0 -> FADE-OUT, sub 4
 *                                 sub 4: at hold-black func_001FAB50
 *                                   (audio stop) -> task state 9 = 4
 *   ARM func_001ADF00             clears, gp-0x7794 "from death" = 1,
 *                                 func_001AB790(func_001AC070): the
 *                                 game task fn is REPLACED WHOLESALE
 *                                 by the CONTINUE machine (the world
 *                                 stops existing; the title installs
 *                                 the same task with flag 0)
 *   CONTINUE func_001AC070        state 0: from-death -> state 2; the
 *                                 prompt sub-machine func_001AC480:
 *                                 launch SCREEN MODULE 1 (the title/
 *                                 continue screen), FADE-IN, then the
 *                                 interactive prompt — cursor task
 *                                 +0xF (from-death INIT = 1, title
 *                                 init = 0; 3 options 0..2), d-pad
 *                                 UP/DOWN = 0x1000/0x4000 moves it
 *                                 (sound 5), confirm = pressed &
 *                                 0x840 (START|CROSS) -> per-option
 *                                 confirm sound 0x5DD/0x5DE/0x5DF +
 *                                 FADE-OUT; idle timer task+0x16 =
 *                                 0x4B0 = 1200 (reset by any held
 *                                 button) -> timeout FADE-OUT to the
 *                                 title/attract cycle (held input
 *                                 mid-fade cancels back in). At
 *                                 hold-black the confirm dispatches:
 *                                 cursor 0 -> func_001AB790(
 *                                 func_001ACEC0) REINSTALLS the
 *                                 gameplay task (CONTINUE; D_00275BE0
 *                                 = 0), 1 -> the load-game screen
 *                                 (func_00225A00/func_00225AC0;
 *                                 success also reinstalls gameplay
 *                                 with D_00275BE0 = 1), 2 -> the
 *                                 func_00200A40 sub-screen, back to
 *                                 the prompt when done.
 *
 * The PORT runs that skeleton faithfully (game_over_tick + the frozen
 * world gate in gameplay_frame — the engine's task replacement is
 * modeled by halting the world sim like the menu pause): death fade ->
 * GAME-OVER screen fades IN (240-frame hold counted through the
 * fade, CROSS skips) -> fade out -> CONTINUE prompt fades in (cursor
 * init 1, d-pad moves, START/CROSS confirms, 1200-frame idle timeout)
 * -> fade out -> dispatch. FLAGGED stand-ins: the two screen modules'
 * art is unexported (em_hud_game_over / em_hud_continue draw the
 * skeleton presentation: black base + tall-font title / option
 * lines); option labels are port guesses (the module text is not
 * decoded); options 1/2 and the timeout have no native target (no
 * save system, no title screen) — all three return to the prompt,
 * documented at the dispatch; option 0 = the real continue (scene
 * reload + boot status restore, the engine's reinstalled-task area
 * re-entry).
 *
 * HEARTBEAT (func_0015D000): health <= 35 -> pad-rumble pulse
 * (func_001B61C0(0, 0xD0, 4, 0)) every 121 frames, <= 10 -> stronger
 * (0xE0) every 61. PURE RUMBLE — the port has no force-feedback
 * backend; documented not-applicable (no sound is involved).
 *
 * HAZARD-ROOM passive drain (func_0015D100 first arm: room attr bit
 * 4 + area flags & 0x60 + suit byte 0x810C7E == 0 -> health -= 1.0
 * every 360 frames) is untranslated — the port has no room-attribute
 * flags yet (flagged).
 *
 * The port's enemy producers post one mailbox int (em_enemy.h):
 * 0x4000 | 15 from the worm's lunge connect -> 15.0 pending HEALTH
 * damage (the DECODED lunge-latch magnitude D_008104D4 = 15.0,
 * func_00154120 sub 3 — replaces the invented 0x400A/10; whether
 * the engine's latch drains health or infection is undecoded,
 * flagged in em_enemy.h), and GEN_TRAP_HIT (5) from the open
 * breather pad -> 5.0 pending INFECTION (the engine pad writes
 * +0x22C = 5.0 — the old port consume burned it as health damage;
 * corrected). */
#define PD_CLIP_FLINCH_A0    0x1Eu /* unarmed flinch, family A side 0 */
#define PD_CLIP_FLINCH_A1    0x1Fu /* unarmed flinch, family A side 1 */
#define PD_CLIP_FLINCH_B0    0x20u /* unarmed flinch, family B side 0 */
#define PD_CLIP_FLINCH_B1    0x21u /* unarmed flinch, family B side 1 */
#define PD_CLIP_FLINCH_ARM_A 0x56u /* armed flinch, family A */
#define PD_CLIP_FLINCH_ARM_B 0x57u /* armed flinch, family B */
#define PD_CLIP_FLINCH_INF  0x1C7u /* infected flinch (90 f) */
#define PD_CLIP_DEATH        0x2Au /* normal death (130 f fall) */
#define PD_CLIP_DEATH_ARM    0x5Cu /* armed death variant (130 f) */
#define PD_CLIP_DEATH_INF   0x1C4u /* infected death (300 f succumb) */
#define PD_SFX_HURT         0x152u /* flinch grunt (health hit) */
#define PD_SFX_HURT_INF     0x153u /* flinch grunt (infection hit) */
#define PD_SFX_DEATH_VOICE  0x146u /* death voice (phase 0) */
#define PD_SFX_DEATH_BODY   0x151u /* death body foley (phase 0) */
#define PD_SFX_DEATH_FALL   0x156u /* mid-fall cue (T-80) */
#define PD_SFX_DEATH_THUD   0x14Eu /* body hits the ground (T-16) */
#define PD_SFX_DEATH_THUD_I 0x14Fu /* infected ground thud */
#define PD_SFX_INFECTED     0x149u /* infection-hits-100 sting */
#define PD_LOW_HEALTH       35.0f  /* +0x235 low-health latch (0x420C0000) */
#define PD_INFECTED_MAX     60.0f  /* infected health cap (0x42700000) */
#define PD_IFRAMES          60     /* +0x20E re-arm (0x3C; latch hits use
                                    * 0x5A = 90 — no latch producer yet) */
#define PD_CORPSE_HOLD      120    /* func_0021D2E0 wait (0x78) */
#define PD_DRAIN_PERIOD     240    /* infected drain period (0xF0) */
#define PD_DRAIN_AMOUNT     2.0f   /* infected drain per period */
#define PD_KILL_PLANE     -200.0f  /* spine death check (Y < -200 -> st 6) */
#define PD_DEATH_CUE_FALL   80     /* frames-remaining sound cue (0x156) */
#define PD_DEATH_CUE_THUD   16     /* frames-remaining ground thud cue */

/* GAME-OVER / CONTINUE machine (the decoded chain in the block doc
 * above — engine values from func_001AD4E0 / func_001AC480). */
#define GO_HOLD_FRAMES    240      /* task+0x18 = 0xF0: the GAME OVER
                                    * screen hold (counts through the
                                    * fade-in; CROSS skips once the
                                    * fade is idle) */
#define GO_PROMPT_FRAMES  1200     /* task+0x16 = 0x4B0: continue-prompt
                                    * idle timeout (reset by any held
                                    * button) */
#define GO_CURSOR_MAX     2        /* prompt options 0..2 (func_001AC480
                                    * clamps the d-pad walk to < 2 on
                                    * increment) */
#define GO_CURSOR_DEATH   1        /* from-death cursor INIT (title = 0
                                    * — func_001AC480 sub 0 on the
                                    * D_00275BDC flag) */
#define GO_SFX_MOVE       5u       /* cursor move blip (func_001FB9F0) */
#define GO_SFX_CONFIRM0   0x5DDu   /* option-0 confirm */
#define GO_SFX_CONFIRM1   0x5DEu   /* option-1 confirm */
#define GO_SFX_CONFIRM2   0x5DFu   /* option-2 confirm */

/* go_state values (the port's fold of task states 3-sub-2..4 + the
 * continue machine's state 2 — game_over_tick below). */
enum {
    GO_OFF = 0,
    GO_ARMED,          /* death fade-out running (pd_phase 3)         */
    GO_SCREEN,         /* GAME OVER screen: fade-in + 240 hold + skip */
    GO_SCREEN_OUT,     /* fading to black (engine wait sub 4)         */
    GO_PROMPT,         /* CONTINUE prompt: fade-in + cursor + timer   */
    GO_PROMPT_CONFIRM, /* confirmed: fading out; dispatch at black    */
    GO_PROMPT_TIMEOUT  /* idle timeout: fading out; engine -> title   */
};

/* Camera values — the AUTHENTIC engine numbers (FINDINGS.md "CAMERA
 * SYSTEM" + the s65 walk-camera decode): idle eye ~33 u behind the
 * player (the tether band [follow - slack, follow]) and 19 u above the
 * player's ground Y; the cut-table target follow seeks player.y + 11 +
 * cam[0x8C] — +17 idle (matching the live state-01 measurement), +8
 * while moving. Chase caps: 2.0 u/frame x/z + 4.0 y for the target
 * pre-step (func_001916C0), 4.0 for the eye solver chase (style 0). */
#define CAM_DIST        33.0f   /* IDLE desired-eye distance stand-in (the
                                   live-converged value; the engine idle tail
                                   runs the same TETHER as the walk camera —
                                   the port idle keeps the yaw-anchored
                                   shape, PORT_DIFFERENCES D13) */
#define CAM_EYE_HEIGHT  19.0f   /* IDLE eye height above player ground Y =
                                   11 + cam[0x5C] + cam[0x8C] (2 + 6 default;
                                   the -31.2 areas use 6 + 2 — same sum.
                                   func_00191390 + func_00230000, s65) */
#define CAM_TGT_HEIGHT  17.0f   /* IDLE target height = player.y + 11 +
                                   cam[0x8C](6) — func_001916C0 idle case
                                   (cut table, decoded s65). The old 15.0
                                   belonged to the SMOOTH-table inline
                                   follow, which gameplay does not use; the
                                   live capture measured +17. */
#define CAM_AIM_OFFSET  6.0f    /* struct +0x8C idle/default table value
                                   (func_00191390; walk states get -3.0) */
#define CAM_TGT_CAP_XZ  2.0f    /* desired-target x/z chase cap, u/frame
                                   (func_001916C0 walk+idle cases; the old
                                   0.8 was the smooth-table inline's cap) */
#define CAM_TGT_CAP_Y   4.0f    /* desired-target y seek cap (func_0018C4B0
                                   calls inside func_001916C0) */
#define CAM_EYE_CAP     4.0f    /* eye chase rate cap, units/frame */
#define CAM_NEAR_PUSH   4.0f    /* commit: view position = eye + 4*fwd */

/* WALK-STATE CAMERA — DECODED 2026-06-11 s65 (func_00230000 router +
 * func_0022FCA0 eye tether + func_00191390 per-state height table +
 * func_00191D40 eye-height seek + func_001916C0 target follow). While
 * the player MOVES (states 2/4/0xF) the camera has NO heading policy
 * at all — the desired eye hangs on a TOW-ROPE behind the target:
 *
 *  - dist = D_00810690 (horiz desired eye<->target, last commit),
 *    follow = fabs(cam+0x0C) (per-record camera distance, -46.8
 *    default), excess = dist - follow.
 *  - excess > 0 (player walking AWAY): the eye slides toward the
 *    target x/z by the FULL excess, instantly — the eye trails the
 *    player's PATH at exactly `follow`, so the camera settles behind
 *    the movement heading asymptotically (the visible heading LAG:
 *    arc the player and the bearing swings only as the path drags it).
 *  - -slack <= excess <= 0: NOTHING moves (the dead band) — walking
 *    toward the camera consumes up to 20 u before any response.
 *    slack = 20.0 when cam+0x64 == -46.8, else 10.0.
 *  - excess < -slack (still closing): actual horiz dist (D_0081069C)
 *    > 8.6 -> the eye backs straight out by (excess + slack), pinning
 *    dist at follow - slack; <= 8.6 (cramped, wall behind) -> SWING:
 *    latch a direction by sign(cam+0x90 wall heading - eye->target
 *    yaw), then rotate the bearing 0.3 deg per unit of over-closure
 *    per frame AWAY from the wall and re-place the eye outward along
 *    it (the spad 3A28 bearing accumulates across latched frames).
 *  - heights — s71 CORRECTION (PCSX2 ground truth: the camera does
 *    NOT dive while walking): states 2/4/0xF are NOT ordinary
 *    locomotion. The +0x230 writer func_0015CBA0 picks 1-vs-2 and
 *    3-vs-4 on the player flag +0x236 — the ELEVATED/hang latch
 *    (func_001764E0: player >= 13.8 u above the floor probe;
 *    func_00162A40 sets it with +0x235 |= 2; cleared by
 *    func_00179680) — so 2/4/0xF = the FLAGGED family (idle/walk
 *    while elevated + the fall family; the area-0 fall cam lives in
 *    the same handler). ORDINARY walking is state 3 -> the
 *    func_001921D0 DEFAULT branch (.L00192DDC), which runs the SAME
 *    tow-rope pull (excess > 0 -> dir*excess) and the SAME
 *    func_00191D40 eye-Y seek with the SAME 11+0x5C+0x8C sum — but
 *    func_00191390's -3.0/1.0 row only matches states 2/4/0xF, so a
 *    GROUND walk keeps the IDLE row: eye player.y + 19, target + 17,
 *    solver floor pad 17 / head-clear 17.5. The low ride (eye +9 /
 *    target +8, floor pad 6, head-clear 13) belongs to the elevated
 *    family only, which the port does not model (no +0x236). The s67
 *    "walk = low ride" binding was a misread of the state ids.
 *  - eye-Y seek (func_00191D40): proportional 1/10 capped 4.0/frame
 *    (1/5 snap inside 1.0 u), desired clamped to the y_hi bound;
 *    RISE vetoed by solver bit 0x80, DESCENT by bit 0x40 (and the
 *    untranslated pre-pass bit +0x5A & 1). Not modeled (no native
 *    reach, flagged): cam+0x98 (+23 when the pre-pass sets +0x6D),
 *    the area-0x10/0x03 clamps, the AREA11 region-1 height swap
 *    (func_00194D10 idx 1 -> constant 6.0 replaces 0x8C, i.e. the
 *    walking eye rides 9 u HIGHER inside that rect — this corrects
 *    the old s50 "+12 AIM target-height tweak" guess), and the
 *    area-0 fall cam (player.y < -83: eye (120, -1590), target y ->
 *    -67.5, style-5 solve, manual 4.0 chase).
 *  - tail (every walk frame): cam+0x44 = atan2 of the ACTUAL pair —
 *    the camera heading is an OUTPUT of the tether, never an input.
 *  - L1 arm: func_00230000 runs it for states 2/0xF only (not 4);
 *    the port arms it for every gait (state<->gait map unpinned). */
/* The follow distance is fabs(g.cam_dist_param) — the engine's
 * cam+0x0C (per-record, scene.txt `camdist`; default -46.8, office
 * records -31.2 [s66 live]); slack keys on the -46.8 default (the
 * engine tests cam+0x64 — the port folds record + area param into the
 * one scene knob). */
#define CAM_TETHER_SLACK  20.0f  /* dead band at the -46.8 default
                                    (engine: 20 when cam+0x64 == -46.8,
                                    else 10) */
#define CAM_TETHER_NEAR   8.6f   /* actual-dist gate (0x4109999A) below
                                    which the back-out becomes the SWING */
#define CAM_SWING_DPU     0.3f   /* swing rate, deg per unit of over-
                                    closure per frame (0x3E99999A) */
#define CAM_WALK_AIM_H   -3.0f   /* +0x8C ELEVATED-family table value
                                    (states 2/4/0xF — unused since s71:
                                    the port has no +0x236 latch; kept
                                    for the future climb/fall port) */
#define CAM_WALK_VAR5C    1.0f   /* +0x5C elevated-family table value
                                    (unused, same note) */
#define CAM_IDLE_VAR5C    2.0f   /* +0x5C idle/default table value */
#define CAM_BASE_H       11.0f   /* the 11.0 both heights build on:
                                    eye = 11 + 0x5C + 0x8C, tgt = 11 + 0x8C */

/* CAMERA FIDELITY (2026-06-11, observed-behavior notes against the
 * real game — the original gives the player NO free camera control;
 * the old d-pad orbit was a port invention and is REMOVED):
 *
 *  - WALL RESPONSE — DECODED (2026-06-11 s61, the full 6984-byte
 *    func_0018DD20 .s read; the old PORT-INVENTED "rise search"
 *    [step 2.0 / ceiling 40 / margin 0.5] is RETIRED): the engine
 *    NEVER lifts the eye to clear a wall. A square-on wall block
 *    PULLS the desired eye to 0.5 u in front of the hit ALONG THE
 *    SIGHT LINE, x/z ONLY — the eye keeps its absolute height while
 *    the horizontal distance collapses, so the view tilts down over
 *    the player's head: the user-observed "camera rises to show the
 *    player's top" is APPARENT rise, emergent from constant-height
 *    pull-in + the commit's +4 forward near-push. It returns when
 *    the sight line clears because the mode handler re-poses the
 *    desired eye every frame. The AIM camera (mode 1) never runs this
 *    solver — it solves with STYLE 2 -> func_0018F870, ALSO DECODED
 *    (2026-06-11 s64, cam_solver_0018F870 below): R1 KEEPS the
 *    constant-height pull-in (hit + 0.5 along the player->eye ray)
 *    but with NO head-clear waiver, NO wedge eject, a corner SLIDE
 *    along the blocking wall, and a floor+2 (not +17) eye-Y bound.
 *    Full decoded policy in cam_solver_0018DD20 / cam_solver_0018F870
 *    below.
 *  - R1 (tap or hold) orients the camera behind the player and keeps
 *    it tracking the aim direction until release; L1 is a one-shot
 *    reorient-behind-the-player. Engine side these are camera-mode
 *    swaps (the s23 live aim capture ran mode 1); the seek rate is a
 *    PORT CONSTANT.
 *  - IDLE AUTO-ORIENT: left alone a little while, the camera SLOWLY
 *    swings behind the player — apparently only at default height —
 *    and if a wall blocks the rotation path it STOPS rather than
 *    repositioning. Delay/rate are PORT CONSTANTS (flagged).
 *  - Per-room FIXED ANGLES (2026-06-11 director decode, FINDINGS
 *    "MODE-0 CAMERA DIRECTOR DECODED"): cut-table mode 0 is the
 *    per-area camera director func_00195130, and its fixed cameras
 *    are MAIN-ELF data — hardcoded per-area cases (areas 0/4/6/8/
 *    0xB/0xD/0xE/0xF/0x11/0x13) + the XZ-quad trigger-volume table
 *    D_0024A5F0 (func_00194D10: point-in-quad + |player.y - rec.y|
 *    < 4 gate). NOT a general overlay hook: the lone `jal 0x823FE0`
 *    is the area-13/entry>=8 gate and lands mid-function in the
 *    shipped AREA13.BIN (dead/drifted). EXPORTED-AREA VERDICT:
 *    the DIRECTOR defines none for AREA02/AREA01 sub 0; AREA06
 *    (snow) has ONE region: X[-370,-340] x Z[-620,-600], y gate 60,
 *    fixed eye (-367.7, 90, -598.9) — scene_snow's `camregion` line.
 *    SECOND MECHANISM (decoded + live-verified 2026-06-11, this
 *    session — the user-observed SUPPLY-ROOM corner camera): room-
 *    ENTRY spawn records (D_0024D650[area][room], +0x10 word) arm
 *    per-room FIXED cameras via func_001B0460 — bit 7 = fixed flag
 *    (cam+0x05), low 7 bits = camera mode (cam+0x06), word>>8 =
 *    index into the eye table D_0024A8D0; the eye HARD-PLACES at
 *    entry and stays pinned while the room is occupied (target =
 *    player + 15). AREA02 room 1 entry 3 = the supply room behind
 *    the office double doors -> eye (116, 33, -300); exported as
 *    the office scene's `camregion` line (the rect spans the room
 *    behind the doorway plane — behavior-identical for an enclosed
 *    room). The port machinery is live: scene.txt `camregion x0 z0
 *    x1 z1 ygate ex ey ez` lines drive camera_mode_dispatch's
 *    in-region branch (fixed eye, L1/auto-orient ignored, R1 aim
 *    still runs, release snaps back INSTANTLY — the observed
 *    behavior: "letting go INSTANTLY snaps back to the room's
 *    setting"). EM_CAMREGION_TEST=1 proves the machinery with a
 *    flagged SYNTHETIC region (it replaces the scene list for the
 *    run); EM_CAPTURE_SUPPLY=1 walks the real supply-room transit.
 *    Engine refinement noted: the snow case approaches its spec at
 *    0.7 u/frame via the chase primitives; the port uses hard
 *    placement — which IS the engine shape for the spawn-record
 *    cameras (func_001B0460 hard-copies desired AND actual). */
#define CAM_REGION_YGATE 4.0f   /* func_00194D10's |player.y - rec.y|
                                   region gate, engine constant */
/* func_0018DD20 solver constants — ALL engine immediates from the .s
 * (decoded 2026-06-11 s61; hex floats noted where non-obvious). */
#define SOLV_EXT          1.5f   /* primary probe extension past the eye   */
#define SOLV_GLANCE_COS   0.70710678f /* glancing gate (0x3F34FDF4, sin45):
                                   dot(horiz sight dir, horiz hit normal)
                                   below this = oblique wall              */
#define SOLV_DIST_PARAM   46.8f  /* fabsf(cam+0x0C) — per-area param (the
                                   -46.8 live in BOTH save states); the
                                   head-clear waiver runs only while the
                                   desired horiz eye<->target dist (the
                                   commit's D_00810690) <= this           */
#define SOLV_HEADCLR_H    17.5f  /* head-clear probe height over player.y
                                   (0x418C0000; 13.0 when cam+0x5C == 1)  */
#define SOLV_HEADCLR_H1   13.0f
#define SOLV_PULL_IN      0.5f   /* pull-in standoff along the sight line */
#define SOLV_WEDGE_DOT   -0.3f   /* reverse-probe "normals not opposing"
                                   gate (0xBE99999A)                      */
#define SOLV_WEDGE_EJECT  4.0f   /* wedge eject along the reverse normal  */
#define SOLV_SIDE         5.5f   /* side-probe lateral reach              */
#define SOLV_SIDE_BACK    3.0f   /* glancing variant: start offset to the
                                   far side                               */
#define SOLV_SIDE_FWD     1.5f   /* glancing variant: end pulled toward
                                   the target                             */
#define SOLV_SIDE_PAD     0.1f   /* candidate standoff along the side ray */
#define SOLV_OPPOSE_DOT  -0.998f /* same-wall-seen-from-behind reject
                                   (0xBF7F7CEE = -0.99800)                */
#define SOLV_CROSS_DOT   -0.08f  /* ceiling-case side reject (0xBDA3D70A) */
#define SOLV_GATE_HI      0.9f   /* non-glancing side response applies    */
#define SOLV_GATE_LO     -0.3f   /*   only for gate dot in (-0.3, 0.9)    */
#define SOLV_FLOOR_PAD    17.0f  /* eye-Y lower bound = floor-under-eye +
                                   17 (0x41880000; 6.0 when +0x5C == 1)   */
#define SOLV_FLOOR_PAD1   6.0f
#define SOLV_CEIL_PAD     1.0f   /* eye-Y upper bound = ceiling - 1       */
#define SOLV_BOUND_RANGE  200.0f /* bound probes reach 200 down/up        */
#define SOLV_BOUND_PULL   1.5f   /* bound probes cast from the eye pulled
                                   1.5 toward player + (0, 11, 0)         */
#define SOLV_PLAYER_UP    11.0f
/* func_0018F870 AIM/scope solver constants — engine immediates from the
 * full 0x16A4 .s read (DECODED 2026-06-11 s64; retires the old
 * CAM_WALL_MARGIN aim stand-in). Shares SOLV_EXT / SOLV_PULL_IN /
 * SOLV_SIDE / SOLV_SIDE_PAD / SOLV_CROSS_DOT / SOLV_OPPOSE_DOT /
 * SOLV_GATE_HI/LO / SOLV_BOUND_RANGE / SOLV_PLAYER_UP / SOLV_CEIL_PAD
 * with the follow solver above. */
#define AIMS_GLANCE_COS  0.99f   /* 0x3F7D70A4 — square-on gate:
                                    dot(horiz TARGET-ward sight dir,
                                    horiz hit normal) < 0.99 = glancing
                                    (much tighter than the follow
                                    solver's sin45)                      */
#define AIMS_CORNER_OFF  1.0f    /* corner lane runs 1 u off the wall   */
#define AIMS_CORNER_BACK 3.0f    /* lane sweep: -3 .. +5.5 along it     */
#define AIMS_WALL_DOT    0.9f    /* 0x3F666666 — corner lane accepts a
                                    DIFFERENT wall only (dot vs the
                                    first normal < 0.9)                  */
#define AIMS_FLOOR_PAD   2.0f    /* aim eye-Y lower bound = floor + 2
                                    (the follow solver keeps 17 — the
                                    aim camera may ride low)             */
#define AIMS_BOUND_PULL  1.0f    /* settle probes cast from the eye
                                    pulled 1.0 toward player + 11 up
                                    (follow: 1.5)                        */
#define AIMS_SETTLE_NY   0.17f   /* 0x3E2E147B — func_0018CE60 walkable
                                    gate on non-floor-class normals      */
#define CAM_L1_RATE     0.0349f /* rad/FRAME — the L1 orient-behind seek
                                   MOTION: the orient-to-heading family
                                   rate (func_001921D0 states 7/0x2C/0x2D,
                                   float 0x3D0EFA35 = 2 deg/frame). The
                                   sub-state-3 motion handler is still
                                   unread — rate flagged. The ARM is now
                                   decoded (func_00191000, s64): see the
                                   L1 block in camera_mode_dispatch. */
#define CAM_L1_MIN_RAD   7.0f   /* func_00191000 orbit-radius clamp:
                                   cam+0x4C = |D_0081069C| forced into
                                   [7.0, |cam+0x64| = 46.8]              */

/* IDLE AUTO-ORIENT — DECODED (2026-06-11, func_001921D0 idle path +
 * func_00193D90, the director sub-state-2 orbit; replaces the old
 * 120-frame / 0.4 rad/s PORT constants):
 *   - the camera struct's own timer (+0x08) increments each idle-path
 *     frame while the player state is 1/2 and the solver byte (+0x07)
 *     has no block bits (engine mask 9); any other state resets it;
 *   - at >= 0x1E1 = 481 frames (~8.0 s — EXACTLY the 300-frame fidget
 *     timer + the 180-frame look-around clip 0x15D: the engine waits
 *     out the END of the look-around idle, the user-observed anchor)
 *     it arms the slow orbit IF |player heading - cam yaw| > 0.052368
 *     rad (3 deg deadband) and the rotation direction's solver wall
 *     bit is clear (delta < 0 needs ~bit4, else ~bit2);
 *   - the orbit (func_00193D90): yaw steps 0.0034907 rad/frame
 *     (0x3B64C389, 0.2 deg/frame) toward the saved player heading,
 *     eye = target - (sin,cos)(yaw) * the horizontal eye<->target
 *     distance saved at arm time (D_0081069C -> cam+0x4C); it cancels
 *     when aligned, when the player leaves state 1/2, or when the
 *     solver reports a wall in the rotation path (bits 0xD/0xB by
 *     direction — the port maps these onto its rotation-path segment
 *     test, cam_yaw_blocked). */
#define CAM_IDLE_ORIENT_FRAMES 481        /* +0x08 >= 0x1E1 */
#define CAM_IDLE_ORIENT_DEADBAND 0.052368f /* 3 deg (0x3D567750) */
#define CAM_ORBIT_RATE  0.0034907f        /* rad/frame (0x3B64C389) */

/* AIM CAMERA MODE 1 — DECODED (2026-06-11, func_00197D20 dispatcher +
 * func_00197740 entry / func_00197870 steady; replaces the +0x8C
 * target-height stand-in):
 *   entry (sub 1, until the aim pose commits): TARGET = player +
 *     rotY(cam euler +0x30)*(0, 19, 6), EYE = player + rotY*(0,19,-30);
 *     actual target chases at 0.4/frame, eye at 4.0/frame;
 *   steady (sub 2): TARGET = base + aim_dir*16 + (0,19,0) (base =
 *     player pos; the R2 stance state 0x2A uses the position saved at
 *     aim entry, spad D_70003040), EYE = player + rotEuler(player
 *     rot)*(0,0,-30) — i.e. 30 u behind the FACING, tracking the
 *     turn-in-place — with EYE.y = player.y + 19 - 30*dir.y (the
 *     -30*dir.y term clamped to >= -25; 22 + that = f20 in [-3,0])
 *     clamped into [player.y + 2, player.y + 30] (flagged areas use
 *     +11 — port keeps +2); actual target chases at 0.6/frame, eye at
 *     4.0/frame; anti-close: horizontal eye<->player dist < 7 raises
 *     EYE.y to player.y + 18 + f20;
 *   dispatcher tail: if EYE.y > player.y + 23 and the horizontal
 *     eye<->player distance < 8, the eye is pushed out to EXACTLY 8
 *     along its own heading (the min-distance clamp);
 *   release (player states 0xC/0x29): the engine swaps to transition
 *     mode 2 (func_00198650, untranslated) — the port re-seeds the
 *     chase yaw from the actual eye->player heading and lets mode-0
 *     chase blend back (the engine's own .L001935EC reset shape). */
#define CAM_AIM_TGT_FWD   16.0f  /* target = base + dir*16 */
#define CAM_AIM_TGT_UP    19.0f  /* + 19 up (0x41980000) */
#define CAM_AIM_EYE_BACK  30.0f  /* eye 30 behind (0xC1F00000) */
#define CAM_AIM_ENTRY_FWD 6.0f   /* entry target (0,19,6) */
#define CAM_AIM_MIN_DIST  8.0f   /* dispatcher min horiz eye dist */

/* DOOR CAMERA CUES — DECODED (2026-06-11, op 0x0D sub 5 func_001B7B30 +
 * func_0018CBD0, and the locked-look native func_001BBBF0).
 * RE-DERIVED + LIVE-VERIFIED 2026-06-11 (this session, two PCSX2
 * transits through the office double doors — the s53 "+27/+25 along
 * the live camera heading" reading was wrong on both axes):
 *   OPEN transit (script D_0024DE40 record 2, op 0x0D sub 5): the cue
 *     fires AFTER the kickoff snapped the player to the STAGING POINT
 *     with the THROUGH-DOOR yaw, and func_001B07C0's pose snapshot
 *     (spad 3B40/3B50) was refreshed with that snapped pose — so the
 *     cut Euler is the DOOR AXIS, never the live camera heading.
 *     A HARD CUT (solver style 1 = copy desired -> actual):
 *       EYE    = staging - 20*(sin,cos)(through yaw), EYE.y =
 *                player.y + 19  (func_0018CBD0: 11 + f4 + f5; f4/f5 =
 *                2/6 default, 6/2 in the -46.8 param areas — 19 BOTH)
 *       TARGET = staging, TARGET.y = player.y + 13  (11 + f4, default
 *                params; -46.8 areas: +17. The +0.3*f20 term in the .s
 *                is a steep-pitch shave with f20 = 0 here, live-read 0)
 *     then cam+0xA0 = 0x78: the actual TARGET re-blends toward the
 *     walking player at <= 1.0 u/frame (func_001916C0's tail) while
 *     the EYE holds — live: eye pinned at (104, 19, -277.2) while the
 *     player walked the doorway.
 *   ROOM-BOUNDARY RE-SEAT (live): when the walk-through crosses the
 *     doorway plane (the engine's room move), the chase re-seats
 *     behind the player's through-door pose and the NORMAL solve runs
 *     — live the engine parked at (104, 29, -250.4) looking down at
 *     the walked-out player (the door wall behind the eye engages the
 *     decoded constant-height func_0018DD20 solve; the +10 over the
 *     default height comes from the walk-state camera handler
 *     func_00230000, unread — the old func_00191000 attribution was
 *     wrong: s64 decoded that one as the L1 re-orient ARM; it never
 *     places the eye).
 *   LOCKED try (script D_0024DEC0 record 2, op 0x09 -> func_001BBBF0):
 *     TARGET = door pos + 8 u toward the HANDLE side (the door-yaw
 *     left: (-8*cos(dyaw), +10, +8*sin(dyaw))) and EYE = TARGET -
 *     13*(sin,cos)(camera yaw D_00810374) with EYE.y = door.y + 12 —
 *     the camera parked at the handle while the try animation plays;
 *     the finish script (op 0x07 sub 4) restores the saved camera. */
#define DOORCAM_EYE_BACK   20.0f  /* op 0x0D sub 5 chase dist (-20.0) */
#define DOORCAM_EYE_UP     19.0f  /* 11 + f4 + f5 (live: eye y 19.0) */
#define DOORCAM_TGT_UP     13.0f  /* 11 + f4 (live: target y 13.0) */
#define DOORCAM_TGT_SOFT   120    /* cam+0xA0 = 0x78 target re-blend */
#define DOORCAM_PLANE      5.0f   /* staging -> doorway-center dist
                                   * (s45 staging math): past this the
                                   * player crossed the door plane */
#define LOCKCAM_HANDLE_OFF 8.0f   /* func_001BBBF0 door-left offset */
#define LOCKCAM_TGT_UP     10.0f
#define LOCKCAM_EYE_BACK   13.0f
#define LOCKCAM_EYE_UP     12.0f  /* door.y + 12 */

/* Engine projection — ADOPTED (2026-06-11; the old TODO(projection) is
 * closed). The world renders through em_mat4_perspective_gs (em_math.h
 * — the full derivation lives there): the engine's per-frame P from
 * zoom s (render-ctx +0x2468, FINDINGS.md "CAMERA SYSTEM" section 3),
 * GS rows (0.8s,0,0,0) (0,0.5s,0,0) (2048,2048,bz,1) (0,0,az,0) mapped
 * exactly to the native 4:3 frame: tan(hfov/2) = 320/s, tan(vfov/2) =
 * 224/s (67.38 x 50.03 deg at s = 480 — the 10/7 tan ratio is the
 * 512x448->4:3 pixel-aspect anisotropy, reproduced); near/far are the
 * bit-exact decode of the Z-row literals: NEAR 0.1, FAR 16711680
 * (0xFF0000 — az = 0.1*(2^24-1), bz = 1 - az/far; the same far the
 * engine passes its parameterized builder func_001D2D20). The gfx
 * backend letterboxes the window to 4:3 (em_gfx_begin_frame), so the
 * baked aspect is the displayed aspect at any window size.
 *
 * Zoom s lives on the camera (EmCamera.zoom, the native ctx+0x2468):
 * default 480 (func_001D25F0 — every static caller passes 0x43F00000);
 * the scope camera (top_mode 3, func_0022EEF0) sets s = 224/tan(half-
 * vfov) ("224.0/x") and scripted lerps animate it (func_001D2590) —
 * both write the same field when they land.
 *
 * Truth check (EM_PROJ_TEST=1, proj_test_run below): with the state01
 * savestate's live camera this chain reproduces the engine's own
 * K = P*V screen positions to < 0.01 px on the PCSX2 frame. */
#define ENGINE_CAM_ZOOM_S      480.0f      /* render-ctx +0x2468 default */
#define ENGINE_CAM_ZOOM_SCOPE  224.0f      /* scope cam: s = 224/tan(v/2) */

/* Task user-byte indices — mirror the live slot-0 record (state bytes
 * observed at record +8 / +9 / +0xB, i.e. user[0] / user[1] / user[3]). */
#define GAME_BYTE_MAIN  0
#define GAME_BYTE_SUB   1
#define GAME_BYTE_FRAME 3

typedef struct {
    EmModel    model;
    EmGfxMesh *mesh;
    float     *palette;   /* static pose (frame 0), bone_count * 16 */
} SceneItem;

/* One recorded draw — the native render-chain element. `tint` is the
 * optional per-draw RGBA modulation (the engine's actor RGB multiplier
 * — em_gfx_draw_skinned_tinted, e34bd29): NULL = the opaque untinted
 * draw. Same pointer contract as `palette`: recorded at chain-build
 * time, the values the flush reads are the owning module's
 * current-frame ones. Today only em_enemy publishes tints (tendril
 * spikes + the gib/corpse death fades — em_enemy_draw_tint). */
typedef struct {
    EmGfxMesh   *mesh;
    const float *palette;
    uint32_t     bone_count;
    const float *tint;
} ChainDraw;

/* CAMERA state — the native mirror of the engine's camera struct at
 * 0x008101E0 (0xD0 bytes) plus the global camera vector pool at
 * 0x008105D0.. (FINDINGS.md "CAMERA SYSTEM" section 1). Field comments
 * give the PS2 offsets/addresses; only what the mode-0 generic follow
 * consumes is live — the rest is carried so the per-room overlay
 * directors and the remaining mode handlers land in place. */
typedef struct {
    uint8_t  state;       /* +0x00: 0 = init one-shot, 1 = run */
    uint8_t  sub_state;   /* +0x01: 0->1 ramp on first run frame (zeroes
                             the mode timer) */
    uint8_t  top_mode;    /* +0x04: 0 = normal play, 1/2 = frozen (commit
                             only), 3 = scope/sniper func_0022EEF0 (TODO) */
    uint8_t  table_sel;   /* +0x05: 0 = cut jtbl_0026D950, 1 = smooth
                             jtbl_0026D910 */
    uint8_t  mode;        /* +0x06: camera mode 0..15 — only mode 0
                             (generic follow) implemented; see
                             camera_mode_dispatch for the TODO list */
    uint8_t  hit;         /* +0x07: follow-solver result byte */
    uint16_t timer;       /* +0x08: mode timer */
    float    zoom;        /* render-ctx +0x2468 zoom s — the projection
                             scale (em_mat4_perspective_gs): default 480
                             (ENGINE_CAM_ZOOM_S, set at camera init);
                             the scope camera writes 224/tan(half-vfov)
                             and scripted lerps animate it (engine
                             func_001D25F0 / func_001D2590) */
    float    eye_des[3];  /* +0x10: desired EYE (world) */
    float    tgt_des[3];  /* +0x20: desired TARGET (world) */
    float    yaw;         /* +0x44: eye->target heading; the R1/L1
                             orient and the idle auto-orient steer it */
    /* func_0018DD20 solver state (decoded s61) */
    float    y_lo;        /* +0x50: eye-Y lower bound (floor-under-eye +
                             17), re-probed every solve; init -200 */
    float    y_hi;        /* +0x54: eye-Y upper bound (ceiling-over-eye -
                             1, else eye + 200); init 1000 */
    uint16_t hit_attr;    /* +0x58: primary-hit surface-class halfword
                             (kept across clear frames, engine-true) */
    float    var_5c;      /* +0x5C: solver height variant, init 2.0.
                             WRITER FOUND (s65): the pre-step
                             func_00191390 per-state table — walk
                             states write 1.0 (head-clear 13 / floor
                             pad 6: the low walk ride), idle 2.0
                             (-31.2 areas: 6.0 — same pad rule) */
    float    wall_yaw;    /* +0x90: published heading of the blocking
                             surface normal, atan2(n.x, n.z) wrapped */
    uint8_t  swing;       /* +0x03: walk-tether swing latch (0 = none,
                             1/2 = direction picked vs wall_yaw —
                             func_0022FCA0, decoded s65) */
    float    swing_yaw;   /* spad 0x70003A28: the swing bearing
                             accumulator (persists while latched) */
    float    horiz_dist;  /* D_00810690: desired horiz eye<->target
                             dist, written by the commit (one frame
                             stale when the solver reads it — engine) */
    float    aim_h;       /* +0x8C: height offset above player Y, driven
                             per frame by the pre-step table
                             (camera_prestep_00191390, s65): idle 6.0,
                             walk -3.0. Feeds the target height (11 +
                             0x8C) and the walk eye base (11 + 0x5C +
                             0x8C); the aim camera proper is MODE 1 */
    uint8_t  aim_phase;   /* MODE-1 sub-machine (+0x01 in mode 1):
                             0 = off, 1 = entry blend (func_00197740),
                             2 = steady aim (func_00197870) */
    float    aim_entry[3];/* spad D_70003040: player pos saved at aim
                             entry (the R2 stance's target base) */
    /* IDLE AUTO-ORIENT orbit (director sub-state 2, func_00193D90) */
    uint8_t  orbit_on;    /* orbit running (cam+0x01 == 2) */
    float    orbit_tgt;   /* +0x48: saved player heading (goal yaw) */
    float    orbit_rad;   /* +0x4C: |horiz eye<->target| at arm time —
                             shared by the idle orbit (sub-state 2) and
                             the L1 seek (sub-state 3, func_00191000:
                             clamped into [7, 46.8]) */
    /* DOOR-CINEMATIC target re-blend (cam+0xA0, func_001916C0 tail) */
    int16_t  tgt_soft;    /* frames left: actual target chases desired
                             at <= 1.0 u/frame instead of hard-copying */
    /* global camera vector pool (the real per-frame camera output) */
    float    eye[3];      /* D_008105D0: actual eye — chased toward
                             eye_des, capped 4.0/frame */
    float    tgt[3];      /* D_008105E0: actual target — copy of tgt_des
                             (func_0018C0C0) */
    float    up[3];       /* D_008105F0: (0,-1,0) — the engine's Y-DOWN
                             view-up, set once at init */
    float    fwd[3];      /* D_00810600: normalized forward (commit) */
    float    view[16];    /* D_00810610: look-at view matrix */
} EmCamera;

/* CAMERA REGION — one mode-0 director fixed-camera trigger volume
 * (scene.txt `camregion`; the engine's D_0024A5F0 0x40-byte records =
 * 4 vec4 XZ-quad corners, axis-aligned rects in all shipped data, with
 * corner-0 y as the elevation gate; func_00194D10 tests point-in-quad
 * (func_001B1EA0 mode 0) + |player.y - y| < 4). Inside, the director
 * pins the desired EYE to the record's spec while the target keeps
 * tracking the player. */
/* LIGHTING — max placed lamps per scene (the largest decoded list,
 * func_001F6760 key 0x200 / office0, has 8 records). */
#define LAMP_MAX 16

#define CAM_REGION_MAX 8
typedef struct {
    float x0, z0, x1, z1;  /* XZ rect (min/max) */
    float ygate;           /* elevation gate center (+-CAM_REGION_YGATE) */
    float eye[3];          /* the room's fixed camera eye spec */
} EmCamRegion;

static struct {
    /* assets */
    EmModel    model;            /* player */
    EmGfxMesh *mesh;             /* player (NULL = not loaded) */
    SceneItem  scene[SCENE_MAX];
    int        n_scene;
    float      player_palette[1024 * 16]; /* bone_count <= 1024 (loader) */

    /* gameplay-frame state */
    int        frame_no;         /* gameplay frames run */
    uint8_t    frame_selector;   /* scratchpad 0x70003B8D: 0 = gameplay */

    /* animation clips + idle<->locomotion crossfade */
    int        clip_idle;        /* clip indices into model.clips */
    int        clip_fidget;      /* idle fidget 349; -1 = none (old asset,
                                  * cycle off) */
    int        clip_walk;        /* -1 = no walk clip (EMD2 asset) */
    int        clip_jog;         /* -1 = no jog clip (id 2) */
    int        clip_run;         /* -1 = no run clip (id 3) */
    int        loco_clip;        /* the ACTIVE locomotion clip index
                                  * (walk/jog/run, by tier) */
    float      loco_speed;       /* its natural ground speed, u/s */
    double     walk_t;           /* locomotion clip time, s (rate-scaled) */
    float      walk_w;           /* locomotion blend weight 0..1 */
    double     step_prev;        /* last frame's loco-cycle position in
                                  * clip FRAMES (footstep edge detect) */
    int        gait;             /* this frame's stick gait 0..3
                                  * (func_001B5CC0 quantizer) */
    int        loco_tier;        /* locomotion tier (+0x25C locIdx): the
                                  * speed ramp promotes/demotes it; the
                                  * sustained tier == gait */
    float      loco_upt;         /* ramped ground speed +0x38, u/tick */
    float      move_speed;       /* this frame's ground speed, units/sec */
    float      walk_palette[1024 * 16];  /* scratch for the blends */

    /* STATUS-MENU UI SCENE (the constants block above): the menu
     * player's private pose state — never touches the gameplay pose. */
    int        clip_menu;        /* clip index of id 450; -1 = old asset */
    int        clip_menu_low;    /* clip index of id 10; -1 = old asset */
    int        ui_prev;          /* scene rendered last frame (edge) */
    int        ui_low;           /* init picked the low-health clip */
    float      ui_yaw;           /* turntable yaw (init pi, +0.01/frame) */
    double     ui_t;             /* menu clip time, frames (1.0/frame) */
    float      ui_ramp;          /* tint pulse 1.0 <-> 1.3 */
    int        ui_ramp_dir;      /* 0 = rising, 1 = falling (+0x05) */
    EmGfxMesh *ui_backplate;     /* fullscreen black quad (lazy) */
    float      ui_palette[1024 * 16];    /* menu pose scratch */

    /* IDLE CYCLE state (func_00161020 — see the clip-id block above) */
    int        idle_phase;       /* 0 = breathing, 1 = fidget playing */
    int        idle_timer;       /* frames left to the next fidget (+0x28) */
    double     idle_t;           /* breathing clip time, seconds */
    double     fid_t;            /* fidget clip time, seconds */
    float      fid_w;            /* fidget blend weight 0..1 */

    /* scripted-anim mailbox (em_game.h em_game_anim_request): the
     * native player+0x1F2 request / +0x20C commit pair. sa_req/sa_cur
     * hold library clip IDS (0 = none); sa_clip is the committed
     * model clip-table index; sa_t the clip time in FRAMES. sa_hold
     * (riding the request as sa_req_hold) marks a HELD POSE: the clip
     * clamps at its last frame instead of returning to locomotion
     * (em_game_anim_hold — the weapon aim pose). */
    unsigned   sa_req;           /* requested clip id   (+0x1F2) */
    float      sa_rate;          /* requested rate      (+0x1F8) */
    int        sa_req_hold;      /* request is a hold (aim pose) */
    unsigned   sa_cur;           /* committed clip id   (+0x20C) */
    int        sa_clip;          /* committed clip index, -1 = none */
    int        sa_hold;          /* committed clip holds its end pose */
    double     sa_t;             /* scripted clip time, frames */

    /* player world placement (the actor's position + facing) */
    float      pos[3];           /* world position, feet on the floor */
    float      yaw;              /* facing about +Y, radians; 0 = +Z */

    /* collision world (id 0x44 -> EMCL; 0 polys = not loaded) */
    EmCollision coll;

    /* camera (struct 0x008101E0 + vector pool — see EmCamera above) */
    EmCamera   cam;
    int        cam_recenter;     /* R1/L1 orient-behind in progress (runs
                                  * until aligned — gives the R1 TAP its
                                  * full reorient) */
    int        cam_idle;         /* frames with no camera-relevant input
                                  * (drives the idle auto-orient) */
    EmCamRegion camregion[CAM_REGION_MAX]; /* scene.txt `camregion` lines
                                  * (the decoded D_0024A5F0 fixed-camera
                                  * trigger volumes for this scene) */
    int        n_camregion;
    int        cam_region_on;    /* this frame's dispatch ran the in-region
                                  * fixed placement (debug/test witness) */

    /* MANUAL AIM STEER state (func_0017ABA0 — the player aim blends) */
    float      aim_pitch;        /* +0x278: 0.5 center, 0 = full DOWN,
                                  * 1 = full UP (stick-down raises it:
                                  * the inverted-Y original behavior) */
    float      aim_yawb;         /* +0x27C: 0.5 center, 1 = pose yaw
                                  * SCREEN-LEFT limit, 0 = right (model
                                  * -X = screen right; overflow turns
                                  * the body) */
    int        aim_was;          /* aiming last frame (entry detect) */
    int        r2_aim;           /* R2-held armed stance 0x1E (code 0x32)
                                  * — the engine's second aim stance
                                  * (func_001607D0: held R2 -> mode
                                  * 0x1E), run by em_game when em_weapon
                                  * is not aiming */
    float      aim_palette[1024 * 16];  /* ladder-blend scratch */

    /* DOOR CINEMATIC camera (op 0x0D sub 5 + func_001BBBF0) */
    int        doorcam;          /* 0 off, 1 = transit armed (walking),
                                  * 2 = cinematic placed (cut done),
                                  * 3 = post-warp (chase re-seated),
                                  * 4 = LOCKED-LOOK hold (func_001BBBF0
                                  * cut pinned until the finish script
                                  * restores — em_door_locked_look) */
    float      doorcut_pos[3];   /* latched STAGING point (the walk-to
                                  * target = the engine's snap pose;
                                  * spad 3B40 equivalent) */
    float      doorcut_yaw;      /* latched THROUGH-DOOR yaw (the
                                  * kickoff snap; spad 3B50 equivalent
                                  * — the cut's Euler, live-verified) */

    /* player status (the engine globals em_hud.h documents; static demo
     * values until the weapon/health systems are translated) */
    EmPlayerStatus status;

    /* PLAYER DAMAGE & DEATH (see the PD_* constants block) — the
     * port of the engine's player damage state (player actor fields
     * in comments). pd_state mirrors the actor MAJOR state byte +0x04
     * restricted to the translated values: 0 = state 1 (gameplay),
     * 2 = state 2 (hit reaction / dying). */
    int        pd_state;       /* 0 gameplay, 2 = hit reaction/death */
    int        pd_sub;         /* +0x05: 0 flinch, 1 death, 3 infected
                                * death (the kill plane reuses 1 with
                                * no clip — engine state 6 has none) */
    int        pd_phase;       /* +0x06 sequence phase */
    int        pd_hold;        /* +0x28 corpse-hold countdown */
    int        pd_iframes;     /* +0x20E post-flinch invuln countdown */
    float      pd_pend_hp;     /* +0x224 pending health damage */
    float      pd_pend_inf;    /* +0x22C pending infection damage */
    int        pd_inf_hit;     /* +0x1F1 == 1: last applied hit was
                                * infection (flinch voice select) */
    int        pd_infected;    /* +0x234 INFECTED latch (infection 100) */
    int        pd_low;         /* +0x235 bit 0 low-health latch */
    int        pd_drain_t;     /* +0x2FC infected drain counter */
    unsigned   pd_clip;        /* committed reaction clip (test/report) */
    int        pd_cue_fall;    /* death-clip T-80 sound fired */
    int        pd_cue_thud;    /* death-clip T-16 sound fired */
    int        go_state;       /* GAME-OVER/CONTINUE machine (GO_*
                                * enum at the PD block) */
    int        go_frames;      /* frames in the current GO state */
    int        go_hold;        /* GAME OVER screen countdown (engine
                                * task+0x18 = 240; ticks through the
                                * fade-in, CROSS skips) */
    int        go_timer;       /* continue-prompt idle timeout (engine
                                * task+0x16 = 1200; reset by any held
                                * button) */
    int        go_cursor;      /* prompt cursor (engine task+0xF;
                                * from-death init = 1) */
    int        go_restart;     /* continue confirmed: scene reload
                                * latch, serviced by the frame machine
                                * (the engine's reinstalled gameplay
                                * task) */

    /* the camera block the recorded chain consumes (native K = P*V) */
    float      viewproj[16];

    /* render chain (this frame's recorded draws) */
    ChainDraw  chain[SCENE_MAX + 1 + EM_DOOR_MAX + EM_ENEMY_MAX
                     + EM_PICKUP_MAX];
    int        chain_len;
    int        chain_test_triangle;

    /* EM_BGM=<path.wav>: loop this cue WAV as level music (see boot task) */
    const char *bgm_path;

    /* SCENE MANIFEST (<scene_dir>/scene.txt) — per-scene boot config,
     * written by the exporters. Defaults = the office values, so a
     * missing manifest keeps the historical behavior bit-for-bit.
     * scene_dir is the ACTIVE scene (boot: SCENE_DIR; changed at
     * runtime by em_game_scene_switch — the goto-door area loader). */
    char        scene_dir[224];  /* active scene directory */
    float       spawn[3];        /* "spawn x y z yaw" — TRUE world coords */
    float       spawn_yaw;       /* facing about +Y, radians; 0 = +Z */
    char        coll_path[288];  /* "collision <file.emcl>" in scene_dir */
    char        bgm_file[256];   /* "bgm <file.wav>" in scene_dir; "" = none */
    float       cam_dist_param;  /* "camdist <f>" — the engine camera
                                  * distance cam+0x0C/+0x64 (signed;
                                  * default -46.8, office records -31.2).
                                  * Walk tether + door re-seat consume
                                  * fabs(); slack keys on == -46.8. */

    /* LIGHTING — the scene's CHARACTER LIGHT RIG (scene.txt light*
     * lines, export_level.py --lightrig: the decoded per-room rig
     * table D_00251C50 record for this scene's (area<<8)|sub key plus
     * the room's placed-lamp list). The engine selects the rig by the
     * area/sub bytes D_00810700/701 (func_001D7B30) — a sub-state
     * flip IS a scene switch in the port, so the rig is per-scene
     * constant by the engine's own mechanism (no spatial room bounds
     * exist or are invented; see char_rig_build). rig_on = 0 (no
     * manifest lines) keeps the historical shader stand-in. */
    int         rig_on;          /* lightamb+lightcam+2 lightdir parsed */
    float       rig_amb[3];      /* ambient row, engine 0..128 scale */
    float       rig_cam_dir[3];  /* slot 0 fill dir, CAMERA-SPACE */
    float       rig_cam_col[3];  /* slot 0 fill color */
    float       rig_cam_w;       /* slot 0 dir weight in the lamp fold */
    float       rig_dir[2][3];   /* slots 1/2 world-space directions */
    float       rig_col[2][3];   /* slots 1/2 colors */
    int         n_lamp;          /* placed lamps (func_001F6760 list) */
    struct {
        float pos[3];            /* world position */
        float col[3];            /* color, x128 registration scale */
        float inten;             /* intensity (slot +0x2C, x128) */
    }           lamp[LAMP_MAX];

    /* EM_CAPTURE / EM_MOVE_TEST / EM_DOOR_TEST debug instrumentation */
    const char *capture_path;
    int         capture_frame;
    int         capture_aim;     /* EM_CAPTURE_AIM=1 — hold R1 from frame
                                  * 0 so the capture shows the armed
                                  * stance (aim pose + laser + camera);
                                  * 3/4 = also hold stick down/up so the
                                  * capture shows the aim-UP / aim-DOWN
                                  * ladder pose (inverted Y) */
    int         capture_door;    /* EM_CAPTURE_DOOR=1 — door-test spawn +
                                  * approach + CROSS, no asserts: the
                                  * capture frame samples the door-transit
                                  * CINEMATIC camera (default frame 110) */
    int         capture_supply;  /* EM_CAPTURE_SUPPLY=1 — spawn at the
                                  * office double doors + CROSS: the
                                  * transit crosses into the SUPPLY ROOM
                                  * and the capture frame samples its
                                  * spawn-record FIXED corner camera
                                  * (default frame 360) */
    int         capture_rise;    /* EM_CAPTURE_RISE=1 — hold 's' (walk at
                                  * the camera) so the capture shows the
                                  * wall-blocked camera looking down at
                                  * the player (see gameplay_frame) */
    int         capture_examine; /* EM_CAPTURE_EXAMINE=1 — snow scene:
                                  * walk to the AREA11 switch + CROSS so
                                  * the capture samples the refusal line
                                  * ("Switch / No power...") presenting
                                  * (see gameplay_frame) */
    int         capture_orient;  /* EM_CAPTURE_ORIENT=1 — turn-in-place,
                                  * then idle: the slow auto-orient demo */
    int         move_test;
    int         move_legs[2];    /* EM_MOVE_LEGS=fwd,strafe frame counts */
    int         move_expect_set; /* EM_MOVE_EXPECT=x,y,z final-pos override */
    float       move_expect[3];
    int         transit_test;    /* EM_TRANSIT_TEST=1 — scene-switch test */
    int         slider_test;     /* EM_SLIDER_TEST=1 — sliding-door test
                                  * (drawbridge scene, slider brain) */
    int         locked_test;     /* EM_LOCKED_TEST=1 — locked-door test
                                  * (drawbridge scene, m15 security
                                  * door; refusal then unlock+retry) */
    int         tt_door;         /* test door index (the goto west door) */
    int         tt_ok_trigger;   /* X press put the goto door in OPENING */
    int         tt_ok_lock;      /* input locked mid-transit */
    int         tt_switch_frame; /* frame the active scene dir changed */
    float       tt_max_fade;     /* peak fade level (must reach 1.0) */
    int         door_test;       /* EM_DOOR_TEST=1 — door interaction test */
    int         dt_door;         /* test door index (the west doorway) */
    int         dt_ok_trigger;   /* X press put the door in OPENING */
    int         dt_ok_lock;      /* input locked during the transit */
    int         dt_ok_inputdead; /* held stick did NOT move the player */
    int         dt_ok_anim;      /* scripted door anim 0x43 committed
                                  * mid-open (back side of the test door) */
    float       dt_min_x;        /* min player x while the door not OPEN */
    float       dt_max_fade;     /* peak fade level seen (must reach 1.0) */
    int         weapon_test;     /* EM_WEAPON_TEST=1 — firing-loop test */
    int         wt_fail;         /* weapon test: failed checkpoints */
    int         enemy_test;      /* EM_ENEMY_TEST: 1 = shoot-the-crawler
                                  * run, 2 = let-it-reach-the-player run,
                                  * 3 = walk-at-the-crate burst run */
    int         enemy_test4;     /* EM_ENEMY_TEST=4 armed (the generator
                                  * run, owned by em_enemy.c) — the
                                  * manifest parser skips generator lines
                                  * so the run stays self-contained */
    int         sfx_test;        /* EM_SFX_TEST=1 — one-shot mixer test */
    int         pause_test;      /* EM_PAUSE_TEST=1 — status-pause test */
    int         camregion_test;  /* EM_CAMREGION_TEST=1 — fixed-camera
                                  * region test (synthetic office region) */
    int         aim_test;        /* EM_AIM_TEST=1 — manual aim-steer +
                                  * mode-1 aim-camera self-test */
    int         pickup_test;     /* EM_PICKUP_TEST=1 — pickup collect /
                                  * inventory / despawn / persistence-
                                  * across-reload self-test */
    int         pt_phase;        /* pickup test: script phase */
    int         pt_fail;         /* pickup test: failed checkpoints */
    int         pt_mark;         /* pickup test: phase anchor frame */
    int         pt_slot;         /* pickup test: injected pickup slot */
    int16_t     pt_r0;           /* pickup test: reserve before collect */
    int         examine_test;    /* EM_EXAMINE_TEST=1 — examine arm /
                                  * input pause / radio line / camera
                                  * cue / chain / re-arm self-test */
    int         ex_fail;         /* examine test: failed checkpoints */
    int         examcam;         /* EXAMINE camera-cue pin latch (op00
                                  * cut held while the sequence runs;
                                  * release = one-shot chase restore,
                                  * the op07-sub4 restore shape) */
    int         melee_test;      /* EM_MELEE_TEST=1 — knife-vs-crate run */
    int         mt_fail;         /* melee test: failed checkpoints */
    int         mt_phase;        /* melee test: script phase */
    int         mt_mark;         /* melee test: phase anchor frame */
    int         et_spawned;      /* test enemy placed at scene init */
    int         et_fail;         /* failed checkpoints */
    float       et_d0;           /* spawn distance to the test enemy */
    float       et_health0;      /* player health at frame 0 */
    int         et_fired;        /* frame the kill-run shot was injected */
    int         et_hit_frame;    /* frame the contact run lost health */
    int         et_burst_frame;  /* crate run: frame the crate burst */
    float       et_bd;           /* crate run: crate distance at burst */
    float       et_wd0;          /* crate run: bug-1 distance at burst */
    float       et_wd_min;       /* crate run: min bug-1 distance since */
    int         et_worm_atk;     /* crate run: bug 1 reached ATTACK
                                  * (field name predates the s68
                                  * worm->bug hatch rebinding) */
    int         death_test;      /* EM_DEATH_TEST=1 — flinch/death/
                                  * game-over/restart self-test */
    int         gt_phase;        /* death test: script phase */
    int         gt_fail;         /* death test: failed checkpoints */
    int         gt_mark;         /* death test: phase anchor frame */
    unsigned    gt_flinch;       /* death test: committed flinch clip */
    float       gt_health0;      /* death test: health before a hit */
} g;

/* SCENE MANIFEST — a plain-text scene.txt in the scene directory, written
 * there by the decomp repo's exporters (tools/export_level.py --spawn /
 * --bgm, tools/export_collision.py). Zero-dependency parser; "key value"
 * lines, '#' starts a comment, unknown keys are ignored:
 *
 *   spawn <x> <y> <z> <yaw>   player spawn, TRUE world coords + facing (rad)
 *   collision <file.emcl>     collision world filename inside the scene dir
 *   bgm <file.wav>            optional looping level-music cue WAV (scene
 *                             dir); the EM_BGM env override still wins
 *   door <file> <x> <y> <z> <yaw> <r> [goto <scene-dir> <sx> <sy> <sz> <syaw>]
 *                             one INTERACTIVE DOOR instance (file under
 *                             the scene dir, e.g. doors/door_m03.emdl;
 *                             r = use-scan trigger radius) — written by
 *                             export_props.py --doors, owned by em_door.c.
 *                             The OPTIONAL goto tail (export_level.py
 *                             --door-goto, the decoded dest+spawn
 *                             tables) makes the door's transition
 *                             commit SWITCH THE ACTIVE SCENE at full
 *                             black (em_game_scene_switch) and place
 *                             the player at the decoded arrival spawn —
 *                             see em_door.h. EM_DOOR_TEST ignores the
 *                             tail (it asserts same-scene re-place
 *                             geometry on the west door).
 *   enemy crawler <x> <y> <z> <yaw>
 *                             one WORM (the kind-0xD func_00153F10
 *                             brain — born attacking; the engine never
 *                             places one, port convenience); owned by
 *                             em_enemy.c.
 *   enemy crate <x> <y> <z> <yaw> [bugs <n>] [variant <v>]
 *                             one DISGUISED CRATE (the placed crawler
 *                             func_001551B0 — em_enemy.h "CRATE
 *                             KIND"): idles as the office crate mesh,
 *                             bursts into gibs + its NEST-GROUP BUGS
 *                             (s68 rebinding — never a worm) on
 *                             DAMAGE or at the end of its alarm-
 *                             driven suicide run (s62 — no proximity
 *                             trigger exists). `bugs <n>` = the
 *                             decoded registry group size (office:
 *                             2-3; 0 = gore-only link -1 crates);
 *                             bare lines default to 2 (flagged
 *                             fallback, em_enemy.h). `variant <v>` =
 *                             the placement MODEL byte (default 6 =
 *                             every exported scene's crates): keys
 *                             the burst's husk family exactly like
 *                             the engine's rebind (6 -> the brown
 *                             wooden husk 0x22 set, else the
 *                             grey-cyan 0x29 set — func_001551B0
 *                             @0x156380).
 *   enemy bug <x> <y> <z> <yaw>
 *                             one BUG hatchling placed directly (the
 *                             s68 15-node insectoid, em_enemy.h "BUG
 *                             KIND" — flagged-minimal brain; stands
 *                             in for the registry's ordinary room-
 *                             spawn records).
 *   pickup <type> <x> <y> <z> <yaw> <uid> [<model.emdl>] [prop]
 *                             one placed PICKUP (export_level.py
 *                             --pickups, the decoded deferred-spawn
 *                             registry D_0024D820 + the placement
 *                             tables' kind-0xB display props; owned by
 *                             em_pickup.c — see em_pickup.h for the
 *                             full decode ledger). <uid> = the
 *                             (area<<8)|puid taken-bit key (0 = never
 *                             persists); a taken uid is silently NOT
 *                             placed (the engine's cond-1 spawn
 *                             suppression). `prop` = render-only.
 *   examine <x> <y> <z> <yaw> <dist> <dy> [gline 0xNN] [delay N]
 *           [cooldown N] [cam <ex> <ey> <ez>]
 *                             one EXAMINE object (export_level.py
 *                             --examine, the decoded overlay examine
 *                             behaviors; owned by em_examine.c — full
 *                             ledger in em_examine.h). CROSS inside
 *                             the use-scan window pauses input and
 *                             presents the radio line: `gline` = a
 *                             GLOBAL slot-0x16 bank line through
 *                             em_hud_radio; otherwise the chained
 *                             AREA-bank records below. `cam` = the
 *                             script's op00 camera-cue eye (cut +
 *                             hold; restore at sequence end).
 *   examinetext <dur> <gap> <text...>
 *                             one chained AREA-bank record of the LAST
 *                             examine line: text (literal "\n" =
 *                             newline) for <dur> frames, then <gap>
 *                             blank frames (the engine's empty odd
 *                             bank lines).
 *   camregion <x0> <z0> <x1> <z1> <ygate> <ex> <ey> <ez>
 *                             one FIXED-CAMERA trigger volume (the mode-0
 *                             director func_00195130's decoded room-camera
 *                             bindings — the D_0024A5F0 XZ-quad records;
 *                             export_level.py --camregions). Player inside
 *                             the XZ rect with |player.y - ygate| < 4: the
 *                             camera eye is PINNED at (ex, ey, ez) (target
 *                             keeps tracking the player), L1 and the idle
 *                             auto-orient are ignored, R1 aim still works
 *                             and its release snaps back INSTANTLY.
 *   enemy generator <x> <y> <z> <yaw> [kind <k>] [link <n>]
 *                             one GENERATOR pad (engine class 0x0D /
 *                             func_0015A2C0 — em_enemy.h "GENERATOR");
 *                             kind/link are the decoded placement
 *                             fields (bare lines default to kind 1 /
 *                             link 2 — port defaults; engine placements
 *                             always carry both). EM_ENEMY_TEST=4 arms
 *                             its own pad inside em_enemy.c and skips
 *                             these lines so the run stays
 *                             self-contained.
 *                             Other enemy kinds are reported and
 *                             skipped.
 *
 * A missing file or missing key leaves the office defaults in place, so
 * the default scene needs no manifest to keep its exact behavior. */
static void scene_manifest_load(void)
{
    g.spawn[0]  = kPlayerPos[0];
    g.spawn[1]  = kPlayerPos[1];
    g.spawn[2]  = kPlayerPos[2];
    g.spawn_yaw = 0.0f;
    snprintf(g.coll_path, sizeof g.coll_path, "%s/%s", g.scene_dir,
             COLL_DEFAULT);
    g.bgm_file[0]  = '\0';
    g.cam_dist_param = -46.8f;  /* cam+0x0C/+0x64 default; `camdist` line */
    g.n_camregion  = 0;     /* camera regions are per-scene data */
    g.rig_on       = 0;     /* LIGHTING: rig + lamps are per-scene data */
    g.n_lamp       = 0;
    int n_ldir = 0, have_amb = 0, have_cam = 0;

    char mf[256 + 16];
    snprintf(mf, sizeof mf, "%s/scene.txt", g.scene_dir);
    FILE *f = fopen(mf, "r");
    if (!f) return;

    char line[512], name[256], gname[64];
    float x, y, z, yaw, r, gx, gy, gz, gyaw;
    int gn, gk, gl;
    int last_examine = -1;   /* examinetext chains onto this slot */
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (sscanf(line, "spawn %f %f %f %f", &x, &y, &z, &yaw) == 4) {
            g.spawn[0]  = x;
            g.spawn[1]  = y;
            g.spawn[2]  = z;
            g.spawn_yaw = yaw;
        } else if (sscanf(line, "collision %255s", name) == 1) {
            snprintf(g.coll_path, sizeof g.coll_path, "%s/%s", g.scene_dir,
                     name);
        } else if (sscanf(line, "bgm %255s", name) == 1) {
            snprintf(g.bgm_file, sizeof g.bgm_file, "%s", name);
        } else if (sscanf(line, "camdist %f", &x) == 1) {
            /* The engine's camera-distance param (cam+0x0C, signed —
             * spawn records carry it at +0x18; live: -46.8 default,
             * -31.2 in the office records, s66). Drives the s67 walk
             * tether follow distance, its slack rule (the engine keys
             * 20-vs-10 on cam+0x64 == -46.8; the port folds record and
             * area param into this one knob) and the door re-seat
             * distance. The exporter does not emit it yet — absent
             * line = the -46.8 default. */
            g.cam_dist_param = x;
        } else if (sscanf(line, "camregion %f %f %f %f %f %f %f %f",
                          &x, &z, &y, &yaw, &r, &gx, &gy, &gz) == 8) {
            /* Fixed-camera trigger volume (x0 z0 x1 z1 ygate ex ey ez) —
             * the decoded mode-0 director room cameras. Reusing the
             * scratch floats: x=x0 z=z0 y=x1 yaw=z1 r=ygate g*=eye. */
            if (g.n_camregion < CAM_REGION_MAX) {
                EmCamRegion *cr = &g.camregion[g.n_camregion++];
                cr->x0 = x < y ? x : y;
                cr->x1 = x < y ? y : x;
                cr->z0 = z < yaw ? z : yaw;
                cr->z1 = z < yaw ? yaw : z;
                cr->ygate  = r;
                cr->eye[0] = gx;
                cr->eye[1] = gy;
                cr->eye[2] = gz;
            } else {
                printf("manifest: camregion limit (%d) hit, skipped: %s",
                       CAM_REGION_MAX, line);
            }
        } else if (sscanf(line, "lightamb %f %f %f", &x, &y, &z) == 3) {
            /* LIGHTING — the scene rig's ambient row (engine 0..128
             * scale; D_00251C50 record +0x68). */
            g.rig_amb[0] = x; g.rig_amb[1] = y; g.rig_amb[2] = z;
            have_amb = 1;
        } else if (sscanf(line, "lightcam %f %f %f %f %f %f %f",
                          &x, &y, &z, &gx, &gy, &gz, &gyaw) == 7) {
            /* LIGHTING — rig slot 0, the CAMERA FILL: dir is
             * CAMERA-SPACE (rotated into world per frame by
             * char_rig_build), w = the lamp-fold dir weight. */
            g.rig_cam_dir[0] = x;  g.rig_cam_dir[1] = y;
            g.rig_cam_dir[2] = z;
            g.rig_cam_col[0] = gx; g.rig_cam_col[1] = gy;
            g.rig_cam_col[2] = gz;
            g.rig_cam_w      = gyaw;
            have_cam = 1;
        } else if (sscanf(line, "lightdir %f %f %f %f %f %f",
                          &x, &y, &z, &gx, &gy, &gz) == 6) {
            /* LIGHTING — rig slots 1/2, the static world-space
             * directionals (in manifest order). */
            if (n_ldir < 2) {
                g.rig_dir[n_ldir][0] = x;  g.rig_dir[n_ldir][1] = y;
                g.rig_dir[n_ldir][2] = z;
                g.rig_col[n_ldir][0] = gx; g.rig_col[n_ldir][1] = gy;
                g.rig_col[n_ldir][2] = gz;
                n_ldir++;
            }
        } else if (sscanf(line, "lamp %f %f %f %f %f %f %f",
                          &x, &y, &z, &gx, &gy, &gz, &gyaw) == 7) {
            /* LIGHTING — one placed lamp (the room's func_001F6760
             * list; color/intensity carry the engine's x128
             * registration scale). */
            if (g.n_lamp < LAMP_MAX) {
                g.lamp[g.n_lamp].pos[0] = x;
                g.lamp[g.n_lamp].pos[1] = y;
                g.lamp[g.n_lamp].pos[2] = z;
                g.lamp[g.n_lamp].col[0] = gx;
                g.lamp[g.n_lamp].col[1] = gy;
                g.lamp[g.n_lamp].col[2] = gz;
                g.lamp[g.n_lamp].inten  = gyaw;
                g.n_lamp++;
            } else {
                printf("manifest: lamp limit (%d) hit, skipped: %s",
                       LAMP_MAX, line);
            }
        } else if (sscanf(line, "door %255s %f %f %f %f %f", name,
                          &x, &y, &z, &yaw, &r) == 6) {
            /* Interactive door instance (em_door.c). The manifest is
             * parsed inside the boot task, so the gfx device exists.
             * Grammar (em_door.h):
             *   door <file> x y z yaw r [locked] [goto <dir> s...]
             * The optional `locked` token (export_level.py
             * --door-locked — the decoded D_00810841 lock gate) is
             * spliced out of a working copy so the goto sscanf below
             * keeps its fixed shape. */
            float p[3] = { x, y, z };
            char  dline[512];
            int   locked = 0;
            snprintf(dline, sizeof dline, "%s", line);
            {
                char *lt = strstr(dline, " locked");
                if (lt && (lt[7] == ' ' || lt[7] == '\0' ||
                           lt[7] == '\n' || lt[7] == '\r')) {
                    locked = 1;
                    memmove(lt, lt + 7, strlen(lt + 7) + 1);
                }
            }
            if (em_door_add(em_frame_gfx(), g.scene_dir, name, p, yaw, r)) {
                printf("manifest: door line failed to load: %s", line);
            } else if (((locked
                             ? (void)em_door_set_locked(em_door_count() - 1)
                             : (void)0),
                        sscanf(dline,
                              "door %*s %*f %*f %*f %*f %*f goto "
                              "%63s %f %f %f %f",
                              gname, &gx, &gy, &gz, &gyaw) == 5)) {
                /* Decoded destination tail (em_door.h): the commit
                 * scene-switches instead of re-placing. EM_DOOR_TEST
                 * asserts the same-scene re-place geometry on this
                 * very door, so it runs with goto tails ignored —
                 * EM_PAUSE_TEST's door leg (the mid-walk-out menu
                 * check) does the same. */
                if (g.door_test || g.pause_test) {
                    printf("manifest: %s — goto tail ignored "
                           "(same-scene transit asserted)\n",
                           g.door_test ? "EM_DOOR_TEST" : "EM_PAUSE_TEST");
                } else {
                    float gp[3] = { gx, gy, gz };
                    em_door_set_goto(em_door_count() - 1, gname, gp,
                                     gyaw);
                }
            }
        } else if ((gn = sscanf(line, "pickup %i %f %f %f %f %i "
                                "%255s %63s",
                                &gk, &x, &y, &z, &yaw, &gl,
                                name, gname)) >= 6) {
            /* Placed pickup (em_pickup.c — collectible item or
             * kind-0xB display prop; the line grammar is in the
             * manifest doc above). gk = item type, gl = taken-bit
             * uid; the optional 7th/8th tokens are the model file
             * and/or the `prop` marker. */
            const char *model = NULL;
            int prop = 0;
            if (gn >= 7) {
                if (strcmp(name, "prop") == 0) prop = 1;
                else model = name;
            }
            if (gn >= 8 && strcmp(gname, "prop") == 0) prop = 1;
            float p[3] = { x, y, z };
            int rc = em_pickup_add(em_frame_gfx(), g.scene_dir, gk, p,
                                   yaw, gl, model, prop);
            if (rc == -1)
                printf("manifest: pickup line failed to load: %s", line);
            /* rc == -2: taken uid — the engine's silent cond-1 skip */
        } else if (sscanf(line, "examine %f %f %f %f %f %f",
                          &x, &y, &z, &yaw, &gx, &gy) == 6) {
            /* EXAMINE object (em_examine.c — grammar in the manifest
             * doc above): optional tokens parsed by keyword so the
             * exporter can omit any of them. */
            float       p[3] = { x, y, z };
            float       cam[3];
            const float *camp = NULL;
            int         exline = -1, exdelay = 0, excool = 0;
            const char *t;
            if ((t = strstr(line, "gline ")))
                exline = (int)strtol(t + 6, NULL, 0);
            if ((t = strstr(line, "delay ")))
                exdelay = (int)strtol(t + 6, NULL, 0);
            if ((t = strstr(line, "cooldown ")))
                excool = (int)strtol(t + 9, NULL, 0);
            if ((t = strstr(line, "cam ")) &&
                sscanf(t + 4, "%f %f %f",
                       &cam[0], &cam[1], &cam[2]) == 3)
                camp = cam;
            last_examine = em_examine_add(p, yaw, gx, gy, exline,
                                          exdelay, excool, camp);
            if (last_examine < 0)
                printf("manifest: examine line failed to load: %s",
                       line);
        } else if ((gn = sscanf(line, "examinetext %d %d", &gk, &gl))
                   == 2) {
            /* chained AREA-bank record of the LAST examine line */
            const char *t = line + strlen("examinetext");
            for (int sk = 0; *t && sk < 2;) {       /* skip 2 numbers */
                while (*t == ' ') t++;
                while (*t && *t != ' ') t++;
                sk++;
            }
            while (*t == ' ') t++;
            char text[256];
            snprintf(text, sizeof text, "%s", t);
            text[strcspn(text, "\r\n")] = '\0';
            if (last_examine < 0 ||
                em_examine_text(last_examine, gk, gl, text) < 0)
                printf("manifest: examinetext line dropped: %s", line);
        } else if ((gn = sscanf(line, "enemy generator %f %f %f %f "
                                "kind %d link %d",
                                &x, &y, &z, &yaw, &gk, &gl)) >= 4) {
            /* Generator pad (em_enemy.c). This match must precede the
             * generic enemy match below, which would otherwise eat the
             * line as kind "generator". */
            if (gn < 5) gk = 1;  /* bare-line port defaults (engine
                                  * placements always carry both) */
            if (gn < 6) gl = 2;
            if (!g.enemy_test4) {
                float p[3] = { x, y, z };
                if (em_enemy_add_generator(em_frame_gfx(), p, yaw,
                                           gk, gl) < 0)
                    printf("manifest: generator line failed to load: %s",
                           line);
            }
        } else if (sscanf(line, "enemy %255s %f %f %f %f", name,
                          &x, &y, &z, &yaw) == 5) {
            /* Placed enemy instance (em_enemy.c). */
            int kind = strcmp(name, "crawler") == 0 ? EM_ENEMY_KIND_CRAWLER
                     : strcmp(name, "crate") == 0   ? EM_ENEMY_KIND_CRATE
                     : strcmp(name, "bug") == 0     ? EM_ENEMY_KIND_BUG
                                                    : -1;
            if (kind < 0) {
                printf("manifest: unknown enemy kind, skipped: %s", line);
            } else {
                float p[3] = { x, y, z };
                int   ok;
                if (kind == EM_ENEMY_KIND_CRATE) {
                    /* optional tails (-1 = absent -> em_enemy's
                     * defaults): `bugs <n>` = the s68 nest-group
                     * size; `variant <v>` = the placement model byte
                     * (the husk-family key — func_001551B0
                     * @0x156380). Order-free: each keyword is scanned
                     * from wherever it appears. */
                    int bn = -1, vn = -1;
                    const char *t;
                    if ((t = strstr(line, " bugs ")))
                        sscanf(t, " bugs %d", &bn);
                    if ((t = strstr(line, " variant ")))
                        sscanf(t, " variant %i", &vn);
                    ok = em_enemy_add_crate(em_frame_gfx(), p, yaw,
                                            bn, vn);
                } else {
                    ok = em_enemy_add_kind(em_frame_gfx(), kind, p, yaw);
                }
                if (ok < 0)
                    printf("manifest: enemy line failed to load: %s", line);
            }
        }
    }
    fclose(f);
    /* LIGHTING — the rig arms only complete (all four rig lines
     * present); partial blocks fall back to the shader stand-in so a
     * half-written manifest never half-lights the scene. */
    g.rig_on = have_amb && have_cam && n_ldir >= 2;
    if (g.rig_on)
        printf("manifest: light rig — amb (%g %g %g), camera fill "
               "(%g %g %g) w %g, %d lamp(s)\n",
               g.rig_amb[0], g.rig_amb[1], g.rig_amb[2],
               g.rig_cam_col[0], g.rig_cam_col[1], g.rig_cam_col[2],
               g.rig_cam_w, g.n_lamp);
    printf("manifest: %s — spawn (%.3f, %.3f, %.3f) yaw %.4f, "
           "collision %s%s%s, %d door(s), %d enem%s, %d pickup(s), "
           "%d examine(s)\n", mf,
           g.spawn[0], g.spawn[1], g.spawn[2], g.spawn_yaw, g.coll_path,
           g.bgm_file[0] ? ", bgm " : "", g.bgm_file, em_door_count(),
           em_enemy_count(), em_enemy_count() == 1 ? "y" : "ies",
           em_pickup_count(), em_examine_count());
    for (int i = 0; i < g.n_camregion; i++)
        printf("manifest: camregion %d — X[%.1f,%.1f] Z[%.1f,%.1f] "
               "y %.1f, fixed eye (%.1f, %.1f, %.1f)\n", i,
               g.camregion[i].x0, g.camregion[i].x1, g.camregion[i].z0,
               g.camregion[i].z1, g.camregion[i].ygate,
               g.camregion[i].eye[0], g.camregion[i].eye[1],
               g.camregion[i].eye[2]);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Load the active scene dir's EMDL files (alphabetical). Returns the
 * number loaded. */
static int scene_load(EmGfx *gfx, SceneItem *items, int max_items)
{
    DIR *dir = opendir(g.scene_dir);
    if (!dir) return 0;

    char *names[SCENE_MAX];
    int   n_names = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL && n_names < max_items) {
        const char *dot = strrchr(de->d_name, '.');
        if (!dot || strcmp(dot, ".emdl") != 0) continue;
        names[n_names] = malloc(strlen(de->d_name) + 1);
        if (!names[n_names]) break;
        strcpy(names[n_names], de->d_name);
        n_names++;
    }
    closedir(dir);
    qsort(names, n_names, sizeof(names[0]), cmp_str);

    int n = 0;
    for (int i = 0; i < n_names; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", g.scene_dir, names[i]);
        free(names[i]);
        SceneItem *it = &items[n];
        if (em_model_load(&it->model, path) != 0) continue;
        it->mesh = em_gfx_mesh_create(gfx, it->model.verts,
                                      it->model.vert_count, it->model.indices,
                                      it->model.index_count,
                                      (const EmGfxTexDesc *)it->model.texs,
                                      it->model.tex_count, it->model.texels,
                                      it->model.flags);
        if (!it->mesh) {
            em_model_free(&it->model);
            continue;
        }
        /* Static level parts pose once: bake the frame-0 palette now. */
        it->palette = malloc(it->model.bone_count * 16 * sizeof(float));
        if (!it->palette) {
            em_gfx_mesh_destroy(gfx, it->mesh);
            em_model_free(&it->model);
            continue;
        }
        em_model_palette_at(&it->model, 0, 0.0, it->palette);
        printf("scene: %s — %u verts, %u tris, %u textures\n", path,
               it->model.vert_count, it->model.index_count / 3,
               it->model.tex_count);
        n++;
    }
    return n;
}

/* Free the ACTIVE scene: level meshes, collision world, door + enemy
 * actors (the engine's actor-pool free at an area change). The player
 * model/mesh, the BGM stream and the sfx registry are NOT touched —
 * they persist across the switch (em_game.h em_game_scene_switch). */
static void scene_unload(EmGfx *gfx)
{
    for (int i = 0; i < g.n_scene; i++) {
        em_gfx_mesh_destroy(gfx, g.scene[i].mesh);
        em_model_free(&g.scene[i].model);
        free(g.scene[i].palette);
    }
    g.n_scene = 0;
    em_collision_free(&g.coll);
    em_door_scene_clear(gfx);   /* keeps the in-flight transit lock */
    em_enemy_shutdown(gfx);
    em_enemy_reset();
    em_pickup_scene_clear(gfx); /* instances only — the inventory and
                                 * the taken-bit set survive (engine
                                 * globals; that survival IS the pickup
                                 * persistence, em_pickup.h) */
    em_examine_reset();         /* examine objects are per-scene
                                 * placements (no persistent state —
                                 * the engine re-arms them anyway) */
}

int em_game_scene_switch(const char *dir)
{
    EmGfx *gfx = em_frame_gfx();
    char   path[sizeof g.scene_dir];

    if (strchr(dir, '/')) {
        snprintf(path, sizeof path, "%s", dir);
    } else {
        /* sibling of the current scene dir (manifest goto tails carry
         * sibling names: "scene_office0" next to "assets/scene") */
        const char *slash = strrchr(g.scene_dir, '/');
        int plen = slash ? (int)(slash - g.scene_dir) + 1 : 0;
        snprintf(path, sizeof path, "%.*s%s", plen, g.scene_dir, dir);
    }

    /* Validate BEFORE tearing the running scene down. */
    char mf[sizeof path + 16];
    snprintf(mf, sizeof mf, "%s/scene.txt", path);
    FILE *probe = fopen(mf, "r");
    if (!probe) {
        printf("scene switch: %s has no scene.txt — staying in %s\n",
               path, g.scene_dir);
        return -1;
    }
    fclose(probe);

    scene_unload(gfx);
    snprintf(g.scene_dir, sizeof g.scene_dir, "%s", path);
    scene_manifest_load();      /* doors + enemies of the new scene */
    g.n_scene = scene_load(gfx, g.scene, SCENE_MAX);
    if (em_collision_load(&g.coll, g.coll_path) == 0) {
        printf("collision: %s — %u polys (%u verts), grid %s\n",
               g.coll_path, g.coll.poly_count, g.coll.vert_count,
               (g.coll.flags & 1) ? "decoded" : "absent (flat-floor)");
    } else {
        printf("no %s — movement uses the room-bbox clamp\n",
               g.coll_path);
    }
    printf("scene switch: %s — %d part(s), %d door(s), %d enem%s\n",
           g.scene_dir, g.n_scene, em_door_count(), em_enemy_count(),
           em_enemy_count() == 1 ? "y" : "ies");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Gameplay frame stages (func_001AE5E0)                               */
/* ------------------------------------------------------------------ */

/* func_001CB590(actor table, size, flags) — actor-context begin. The
 * engine points the live node-table base at the actor block about to be
 * updated; the port has exactly one actor (the player), so the context is
 * implicit. Kept as a stage so multi-actor support lands here. */
static void actor_context_begin(void) {}

/* func_001CB5A0 — actor-context end. */
static void actor_context_end(void) {}

/* Compose the player's world placement onto an evaluated anim palette:
 * every bone matrix M becomes T(pos) * R_y(yaw) * M — rotation about Y by
 * the facing yaw, then translation, applied AFTER the animation pose. At
 * yaw = 0 this reduces bit-exactly to the old static translation bake
 * (cos 0 = 1, sin 0 = 0), keeping EM_CAPTURE output stable. */
static void palette_apply_placement(float *pal, uint32_t bone_count,
                                    const float pos[3], float yaw)
{
    const float c = cosf(yaw), s = sinf(yaw);
    for (uint32_t b = 0; b < bone_count; b++) {
        float *m = pal + b * 16;
        for (int col = 0; col < 4; col++) {
            float x = m[col * 4 + 0], z = m[col * 4 + 2];
            m[col * 4 + 0] =  c * x + s * z;
            m[col * 4 + 2] = -s * x + c * z;
        }
        m[12] += pos[0];
        m[13] += pos[1];
        m[14] += pos[2];
    }
}

/* WALL-class segment probe from an explicit start point. Outdoor
 * terrain (the snow scene's grid world) is near-flat but tilted, so a
 * low horizontal segment can clip the very ground the player stands
 * on — front-facing by a hair (n.z ~= -0.001) — and a naive block
 * turns into sideways drift along the terrain. The engine's result
 * block carries the surface class (SPR 0x700030CA) for exactly this
 * split: walkable ground (FLOOR/SLOPE) never blocks horizontal motion
 * (the floor query owns it; the engine's own filter is the
 * func_001764E0 surface-angle band) — walkable crossings are stepped
 * past and the probe re-runs for anything solid beyond them. When
 * `with_doors` is set the MOVABLE-HULL set joins in (em_door_probe —
 * the engine's mask-7 chest pass; a closed or moving door blocks, a
 * fully open one does not), nearest hit winning like the engine hub's
 * per-set segment clamping. Returns 1 with *hit staged on the first
 * wall-class hit, else 0. */
static int probe_wall_seg(const float start[3], const float target[3],
                          int with_doors, EmCollHit *hit)
{
    const unsigned mask = EM_COLL_SET_CELLS | EM_COLL_SET_GRID;
    float from[3] = { start[0], start[1], start[2] };
    int   sres    = 0;

    for (int i = 0; i < 8; i++) {
        if (!em_collision_move_probe(&g.coll, from, target, mask, hit))
            break;
        if (hit->surf_class != EM_SURF_FLOOR &&
            hit->surf_class != EM_SURF_SLOPE) {
            sres = 1;                       /* a real wall (or ceiling) */
            break;
        }
        /* Walkable ground — nudge the probe start just past the
         * crossing and look again for solid geometry beyond it. */
        float dx  = target[0] - hit->point[0];
        float dz  = target[2] - hit->point[2];
        float len = sqrtf(dx * dx + dz * dz);
        if (len <= 1e-3f) break;            /* crossing at the target */
        from[0] = hit->point[0] + dx / len * 1e-3f;
        from[2] = hit->point[2] + dz / len * 1e-3f;
    }
    if (!with_doors || !em_door_count())
        return sres;

    EmCollHit dhit;
    if (!em_door_probe(start, target, &dhit))
        return sres;
    if (sres) {
        float sd2 = 0.0f, dd2 = 0.0f;
        for (int k = 0; k < 3; k++) {
            float ds = hit->point[k] - start[k];
            float dd = dhit.point[k] - start[k];
            sd2 += ds * ds;
            dd2 += dd * dd;
        }
        if (sd2 <= dd2)
            return 1;          /* the static wall is nearer */
    }
    *hit = dhit;
    return 1;
}

/* func_001764E0 — the engine's RADIAL WALL PROBES (the real player
 * hitbox; see the PLAYER WALL RADIUS block above). Five directions
 * yaw + {0, +45, -45, +90, -90} deg (D_00248950), each probed twice —
 * ankle y+0.05 over the static sets and chest y+4.01 with the movable
 * hulls (doors) joined in — and every wall-class hit pushes the actor
 * back by the probe's overshoot (pos += hit - end, the spad
 * 0x700031C0 delta), exactly the engine's response. Each probe runs
 * from the ALREADY-corrected position, like the PS2 loop re-reading
 * actor +0xB0 per iteration. Runs every free/idle frame (the engine
 * fires it from both the idle top func_00161020 and the walk top
 * func_001612D0); scripted door transits skip it — the engine's
 * MOVE-TO crosses the sealed boundary planes deliberately. */
static void player_wall_probes(void)
{
    static const float kProbeAngle[5] = {
        0.0f, 0.7853982f, -0.7853982f, 1.5707964f, -1.5707964f
    };                                       /* D_00248950, radians */
    if (!g.coll.poly_count)
        return;
    for (int i = 0; i < 5; i++) {
        float ang = g.yaw + kProbeAngle[i];
        float dx  = sinf(ang) * PLAYER_WALL_RADIUS;
        float dz  = cosf(ang) * PLAYER_WALL_RADIUS;
        for (int pass = 0; pass < 2; pass++) {
            float lift = pass ? PROBE_CHEST_LIFT : PROBE_ANKLE_LIFT;
            float from[3] = { g.pos[0], g.pos[1] + lift, g.pos[2] };
            float end[3]  = { from[0] + dx, from[1], from[2] + dz };
            EmCollHit hit;
            if (probe_wall_seg(from, end, pass /* doors: chest only */,
                               &hit)) {
                static int trace = -1;
                if (trace < 0) trace = getenv("EM_PROBE_TRACE") != NULL;
                if (trace)
                    printf("probe: frame %d dir %d pass %d pos (%.2f, "
                           "%.2f) hit (%.2f, %.2f) push (%.3f, %.3f)\n",
                           g.frame_no, i, pass, g.pos[0], g.pos[2],
                           hit.point[0], hit.point[2],
                           hit.point[0] - end[0], hit.point[2] - end[2]);
                g.pos[0] += hit.point[0] - end[0];
                g.pos[2] += hit.point[2] - end[2];
            }
        }
    }
}

static void player_move_collide(float mx, float mz)
{
    EmCollHit hit;

    /* Integrate the move, then let the radial probes correct it (the
     * engine's order: the walk top writes +0xB0/B8, func_001764E0
     * pushes back). With the 4.5-unit radius far above the per-frame
     * step (0.3 u at run) the probes also own anti-tunneling. */
    g.pos[0] += mx;
    g.pos[2] += mz;
    player_wall_probes();

    /* Floor: vertical segment query through the same worlds (the grid
     * world owns the walkable floor — FINDINGS "COLLISION WORLD"). The
     * same class split applies downward: a leaning wall face (e.g. the
     * snow scene's gate posts, n.y slightly > 0) front-faces the probe
     * from above, and accepting it ratchets the player up the wall while
     * sliding along it — step past non-walkable crossings instead. */
    float from[3] = { g.pos[0], g.pos[1] + FLOOR_PROBE_UP,    g.pos[2] };
    float down[3] = { g.pos[0], g.pos[1] - FLOOR_PROBE_DOWN,  g.pos[2] };
    for (int i = 0; i < 8; i++) {
        if (!em_collision_segment_query(&g.coll, from, down,
                                        EM_COLL_SET_CELLS |
                                        EM_COLL_SET_GRID, 0, &hit))
            break;
        if (hit.surf_class == EM_SURF_FLOOR ||
            hit.surf_class == EM_SURF_SLOPE) {
            g.pos[1] = hit.point[1];
            break;
        }
        if (hit.point[1] - 1e-3f <= down[1])
            break;
        from[1] = hit.point[1] - 1e-3f;
    }
}

/* Player movement (the port's first slice of the actor spine's physics
 * side): left stick = camera-relative walk on the XZ plane; the facing
 * yaw seeks the movement direction at TURN_SPEED (smooth turn). With a
 * collision world loaded, movement goes through the engine's move probe
 * (walls stop/slide, the floor query sets the height); without one, the
 * old room-bbox clamp keeps the repo runnable standalone. */
static void player_move(void)
{
    g.gait = 0;          /* re-quantized below; scripted paths leave 0 */

    /* DOOR TRANSIT (the engine's gameplay-frame selector 3, spad
     * 0x70003B8D, armed by the use scan): a scripted MOVE-TO carries
     * the player to the door's far-side point with yaw snapped to the
     * door normal (func_001BBE40 -> func_00182F90). Runs collision-free
     * — the doorways are statically sealed by the grid room-boundary
     * planes, and this scripted move is exactly how the engine crosses
     * them. Stick input is ignored while it runs (the selector-3 frame
     * variant does not run the free-move spine). */
    {
        float tt[3], tyaw;
        if (em_door_transit_active(tt, &tyaw)) {
            float dx   = tt[0] - g.pos[0];
            float dz   = tt[2] - g.pos[2];
            float len  = sqrtf(dx * dx + dz * dz);
            float step = WALK_SPEED * FRAME_DT;
            g.move_speed = WALK_SPEED;     /* drive the walk clip */
            g.loco_tier  = 1;              /* scripted walk = tier-1 clip */
            g.loco_upt   = 0.0f;           /* free-move ramp re-arms */
            g.yaw        = tyaw;
            if (len <= step || len < 1e-6f) {
                g.pos[0] = tt[0];
                g.pos[2] = tt[2];
            } else {
                g.pos[0] += dx / len * step;
                g.pos[2] += dz / len * step;
            }
            return;
        }
    }

    /* ARRIVAL WALK-OUT (engine player state 5/1, func_00183250 —
     * decoded 2026-06-11, em_door.h step 4): after the re-place the
     * player UNINTERRUPTIBLY walks out through the door along the exit
     * yaw — 50 frames of clip-in-place (mostly under the fade-in), 30
     * frames at the locIdx-2 speed (0.3 u/tick), 30 frames decaying to
     * a stop (~12.8 u total). The stick is never read (state 5 has no
     * free-move spine); em_door owns the phases, this consumes the
     * per-frame command. Collision-free like the transit MOVE-TO (the
     * engine runs its own mover in the destination area's geometry). */
    {
        float wyaw, wspeed;
        if (em_door_walkout_active(&wyaw, &wspeed)) {
            g.yaw        = wyaw;
            /* Drive the locomotion clip at the engine's commanded tier:
             * the walk-out plays the locIdx-2 clip (family 0 -> id 2 =
             * JOG, 0.3 u/tick) even during the in-place phase. */
            g.move_speed = wspeed > 0.0f ? wspeed : GAIT_JOG_SPEED;
            g.loco_tier  = 2;
            g.loco_upt   = 0.0f;           /* free-move ramp re-arms */
            g.pos[0] += sinf(wyaw) * wspeed * FRAME_DT;
            g.pos[2] += cosf(wyaw) * wspeed * FRAME_DT;
            return;
        }
    }

    /* MOVEMENT LOCK (door transit — the decoded two-lock split,
     * em_door.h "THE TWO LOCKS"): kickoff -> walk-out end. Free
     * movement is ignored — the player walks the scripted MOVE-TO /
     * walk-out above or stands (at the staging point). The MENU lock
     * is separate (em_door_menu_locked, consumed by em_hud) and ends
     * earlier, at fade-in completion. */
    if (em_door_movement_locked()) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;          /* scripted mode exits locomotion:
                                    * re-entry re-arms the tier ramp */
        g.loco_upt   = 0.0f;
        return;
    }

    /* EXAMINE SEQUENCE LOCK (em_examine.h): the examine script's op07
     * sub2 enters scripted mode for the whole message presentation —
     * the player stands (no scripted walk on the examine path; the
     * engine's op01 walk-to in the AREA11 refusal is FLAGGED-omitted
     * there). Same lock shape as the door transit above. */
    if (em_examine_input_locked()) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;
        g.loco_upt   = 0.0f;
        return;
    }

    /* R2-HELD ARMED STANCE 0x1E (decoded 2026-06-11: the action machine
     * func_001607D0 dispatches HELD R2 -> player mode 0x1E, action code
     * 0x32 — the engine's SECOND aim stance, sharing the mode-1 aim
     * camera through player states 0x2A/0x29; its laser is the
     * DOT-only drawer func_001854E0 and it has no fire-counter recoil
     * — both stay with em_weapon, noted there as pending). em_game
     * runs its planted pose + steer + camera side: the held aim pose
     * through its own anim mailbox, R1 (em_weapon) taking priority. */
    {
        const EmFrameInput *rin = em_frame_input();
        int want = (rin->held & EM_PAD_R2) && !em_weapon_is_aiming() &&
                   !em_weapon_is_melee();
        if (want && !g.r2_aim)
            em_game_anim_hold(0x112, 1.0f);
        else if (!want && g.r2_aim && !em_weapon_is_aiming() &&
                 em_game_anim_active() == 0x112)
            em_game_anim_cancel();
        g.r2_aim = want;
    }

    /* PLANTED AIMING + MANUAL AIM STEER (func_0017ABA0 — the decoded
     * constants block above; retires the old AIM_TURN_SPEED turn-in-
     * place stand-in). The armed stance holds position (engine: the
     * armed modes 0x1D..0x20 replace the locomotion modes outright —
     * no aim-walk clips, zero footstep frames); the stick (and the
     * port's d-pad merge — arrows fold into the same axes, full
     * deflection) steers the aim BLENDS: pitch INVERTED-Y, yaw panning
     * the +-60 deg pose ladder first and turning the body only past
     * the blend limit. */
    {
        int aim_now = em_weapon_is_aiming() || g.r2_aim;
        if (aim_now && !g.aim_was) {
            /* STANCE ENTRY (func_0016F600 family): the aim blends reset
             * to center and the entry position is saved (D_70003040 —
             * the R2 state-0x2A camera target base), and the camera
             * arms its mode-1 entry phase. */
            g.aim_pitch = 0.5f;
            g.aim_yawb  = 0.5f;
            memcpy(g.cam.aim_entry, g.pos, sizeof g.cam.aim_entry);
            g.cam.aim_phase = 1;
        } else if (!aim_now && g.aim_was) {
            /* RELEASE (player states 0xC/0x29 -> camera mode 2): the
             * port re-seeds the chase yaw from the actual eye->player
             * heading (the engine's .L001935EC reset) and lets the
             * mode-0 chase blend back. */
            g.cam.aim_phase = 0;
            g.cam.yaw = atan2f(g.pos[0] - g.cam.eye[0],
                               g.pos[2] - g.cam.eye[2]);
        }
        g.aim_was = aim_now;
    }
    if (em_weapon_is_aiming() || g.r2_aim) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;          /* armed modes replace locomotion */
        g.loco_upt   = 0.0f;
        const EmFrameInput *ain = em_frame_input();
        int r1fam = !g.r2_aim || em_weapon_is_aiming(); /* stance 0x31 */

        /* d-pad merge (PORT, user-attested d-pad aim): arrows act as a
         * full-deflection axis when the stick is centered. */
        int rawx = ain->lx, rawy = ain->ly;
        if (rawx == 0x80 && (ain->held & EM_PAD_LEFT))  rawx = 0x00;
        if (rawx == 0x80 && (ain->held & EM_PAD_RIGHT)) rawx = 0xFF;
        if (rawx == 0x80 && (ain->held & EM_PAD_UP))    rawy = 0x00;
        if (rawy == 0x80 && (ain->held & EM_PAD_DOWN))  rawy = 0xFF;

        /* func_001B5DC0 deflection bands + the per-stance rate rows */
        static const float kRateR1[4] = { 0.0f, 0.0025f, 0.005f, 0.015f };
        static const float kRateR2[4] = { 0.0f, 0.0016667f, 0.005f,
                                          0.015f };
        const float *rate = r1fam ? kRateR1 : kRateR2;
        float body_mul = r1fam ? 1.0f : 1.5f;   /* f20 */
        int bx = abs(rawx - 0x80), by = abs(rawy - 0x80);
        int bandx = bx < AIM_BAND_1 ? 0 : bx < AIM_BAND_2 ? 1
                  : bx < AIM_BAND_3 ? 2 : 3;
        int bandy = by < AIM_BAND_1 ? 0 : by < AIM_BAND_2 ? 1
                  : by < AIM_BAND_3 ? 2 : 3;

        /* YAW (+0x27C): rate scaled by 1/sin(pi*(0.5+0.6*(p-0.5))) —
         * exactly 1.0 at pitch center (the engine special-cases the
         * equality), faster pitched off level. Overflow past [0,1]
         * turns the body by the excess (screen-right = yaw decreasing
         * in the port basis, matching the engine's heading -=). */
        if (bandx) {
            float p = g.aim_pitch;
            float s = (p == 0.5f) ? 1.0f
                    : sinf(EM_PI * (0.5f + 0.6f * (p - 0.5f)));
            float step = rate[bandx] / s;
            if (rawx >= 0x80) {            /* stick RIGHT */
                g.aim_yawb -= step;        /* toward the right poses */
                if (g.aim_yawb < 0.0f) {
                    g.yaw -= -g.aim_yawb * body_mul;
                    g.aim_yawb = 0.0f;
                }
            } else {                       /* stick LEFT */
                g.aim_yawb += step;
                if (g.aim_yawb > 1.0f) {
                    g.yaw += (g.aim_yawb - 1.0f) * body_mul;
                    g.aim_yawb = 1.0f;
                }
            }
            while (g.yaw >  EM_PI) g.yaw -= 2.0f * EM_PI;
            while (g.yaw < -EM_PI) g.yaw += 2.0f * EM_PI;
        }

        /* PITCH (+0x278): INVERTED Y — stick DOWN (raw >= 0x80) raises
         * the blend = aim UP; stick UP aims DOWN ("W = down"). The R1
         * family speeds up 1.5x outside [0.3, 0.7]. */
        if (bandy) {
            float mult = (r1fam && (g.aim_pitch <= 0.3f ||
                                    g.aim_pitch >= 0.7f)) ? 1.5f : 1.0f;
            float step = rate[bandy] * mult * 0.5f;
            if (rawy >= 0x80) {            /* stick DOWN -> aim UP */
                g.aim_pitch += step;
                if (g.aim_pitch > AIM_PITCH_MAX_R1)
                    g.aim_pitch = AIM_PITCH_MAX_R1;
            } else {                       /* stick UP -> aim DOWN */
                g.aim_pitch -= step;
                if (g.aim_pitch < 0.0f) g.aim_pitch = 0.0f;
            }
        }

        /* LOCK STEER (func_0017AF70 — em_weapon.h "TARGET LOCK"):
         * with the stick idle and a target in lock slot 0, em_weapon
         * creeps the blends toward the lock at <= 0.02/frame. The
         * stick-idle gate is the engine's manual-input lock drop (the
         * 0x1D stance clears D_008106E0 whenever func_0017ABA0 flags
         * manual steering, +0x302) — the player's hand always wins. */
        if (!bandx && !bandy) {
            float lp, ly;
            if (em_weapon_lock_steer(g.pos, g.yaw, g.aim_pitch,
                                     g.aim_yawb, &lp, &ly)) {
                g.aim_pitch = lp;
                g.aim_yawb  = ly;
            }
        }
        return;
    }

    /* KNIFE / MELEE plant: the engine's melee modes 0x21/0x22 replace
     * the locomotion modes outright (em_weapon.h "KNIFE / MELEE") —
     * the player stands for the swing + recover. The heavy's in-swing
     * yaw steer (func_00173DD0, D_002486F0 rates) is untranslated
     * (flagged in em_weapon.h), so no turn-in-place here either. */
    if (em_weapon_is_melee()) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;          /* melee modes replace locomotion */
        g.loco_upt   = 0.0f;
        return;
    }

    /* ANALOG GAIT — the engine's stick quantizer func_001B5CC0 on the
     * RAW 0x80-centered bytes: r = sqrt((x-128)^2 + (y-128)^2) through
     * rings 48/88/122 -> gait byte (pad +0x17 -> player +0x23F). */
    const EmFrameInput *in = em_frame_input();
    float rdx = (float)in->lx - 128.0f;
    float rdy = (float)in->ly - 128.0f;
    float r   = sqrtf(rdx * rdx + rdy * rdy);
    int gait  = r <= GAIT_RING_1 ? 0
              : r <= GAIT_RING_2 ? 1
              : r <= GAIT_RING_3 ? 2 : 3;
    g.gait       = gait;
    g.move_speed = 0.0f;

    /* THE TIER RAMP (func_0017BC40 — the 2026-06-11 re-decode; the
     * engine's +0x38/+0x25C pair): the target is the gait's
     * D_00248870 speed; the current speed accelerates/decelerates
     * toward it through the tier boundaries, promoting/demoting
     * loco_tier as each one is crossed. */
    static const float kTier[4] = { 0.0f, 0.1f, 0.3f, 0.8f };
    if (gait == 0) {                  /* dead ring: idle / run-down */
        if (g.loco_tier == 3 && g.loco_upt > 0.0f) {
            /* RUN-DOWN (phase 2): the run carries — speed decays
             * 0.03125 u/tick per frame to the tier-2 boundary, THEN
             * stops (the engine's phase-3 instant stop; the mode-6
             * stop-skid anims ids 4/5 are untranslated). */
            g.loco_upt -= GAIT_RUNDOWN;
            if (g.loco_upt <= kTier[2]) {
                g.loco_tier = 0;
                g.loco_upt  = 0.0f;
            } else {
                g.move_speed = g.loco_upt * 60.0f;
                float rx = sinf(g.yaw), rz = cosf(g.yaw);
                if (g.coll.poly_count)
                    player_move_collide(rx * g.move_speed * FRAME_DT,
                                        rz * g.move_speed * FRAME_DT);
                else {
                    g.pos[0] += rx * g.move_speed * FRAME_DT;
                    g.pos[2] += rz * g.move_speed * FRAME_DT;
                }
                return;
            }
        } else {                      /* tiers <= 2 stop instantly
                                       * (phase 3: +0x38 = 0) */
            g.loco_tier = 0;
            g.loco_upt  = 0.0f;
        }
        player_wall_probes();         /* the idle top probes too */
        return;
    }

    /* Locomotion ENTRY (func_001612D0 state 2): from idle the tier
     * starts at gait-1 with that tier's speed; the ramp then promotes
     * it to the gait. A gait change mid-run ramps from the current
     * speed instead (no re-entry). */
    if (g.loco_tier == 0 && g.loco_upt <= 0.0f) {
        g.loco_tier = gait - 1;
        g.loco_upt  = kTier[gait - 1];
    }
    {
        float target = kTier[gait];
        if (g.loco_upt < target) {            /* sub 1: accelerate */
            float acc = g.loco_tier <= 0 ? GAIT_ACCEL_0
                      : g.loco_tier == 1 ? GAIT_ACCEL_1 : GAIT_ACCEL_2;
            g.loco_upt += acc;
            if (g.loco_tier < 3 && g.loco_upt >= kTier[g.loco_tier + 1]) {
                g.loco_upt = kTier[g.loco_tier + 1];
                g.loco_tier++;                /* +0x25C += 1 */
            }
            if (g.loco_upt > target) g.loco_upt = target;
        } else if (g.loco_upt > target) {     /* sub 2: decelerate */
            float dec = g.loco_tier >= 3 ? GAIT_DECEL_3 : GAIT_DECEL_2;
            g.loco_upt -= dec;
            if (g.loco_tier > 0 && g.loco_upt <= kTier[g.loco_tier - 1]) {
                g.loco_upt = kTier[g.loco_tier - 1];
                g.loco_tier--;                /* +0x25C -= 1 */
            }
            if (g.loco_upt < target) g.loco_upt = target;
        } else {
            g.loco_tier = gait;               /* at tier: sustained */
        }
    }

    /* Stick direction (normalized) -> camera-relative move heading.
     * Camera basis on XZ: forward f points from the eye towards the
     * player, screen-right is f x up = (-fz, 0, fx). Stick up walks
     * away from the camera. Reads only the camera struct's yaw
     * (+0x44), so the EM_MOVE_TEST trajectory is independent of the
     * eye smoothing. */
    float sx = rdx / r, sy = rdy / r;
    float fx = sinf(g.cam.yaw), fz = cosf(g.cam.yaw);
    float mx = fx * -sy - fz * sx;
    float mz = fz * -sy + fx * sx;

    /* The ramped speed drives this frame (sustained: gait 1 = WALK
     * 6 u/s, gait 2 = JOG 18 u/s, gait 3 = RUN 48 u/s). */
    g.move_speed = g.loco_upt * 60.0f;

    if (g.coll.poly_count) {
        player_move_collide(mx * g.move_speed * FRAME_DT,
                            mz * g.move_speed * FRAME_DT);
    } else {
        g.pos[0] += mx * g.move_speed * FRAME_DT;
        g.pos[2] += mz * g.move_speed * FRAME_DT;
        if (g.pos[0] < kRoomMin[0]) g.pos[0] = kRoomMin[0];
        if (g.pos[0] > kRoomMax[0]) g.pos[0] = kRoomMax[0];
        if (g.pos[2] < kRoomMin[1]) g.pos[2] = kRoomMin[1];
        if (g.pos[2] > kRoomMax[1]) g.pos[2] = kRoomMax[1];
        g.pos[1] = 0.0f;  /* flat floor (no collision world loaded) */
    }

    /* Smooth-turn the facing towards the move direction (shortest arc). */
    float target = atan2f(mx, mz);
    float diff   = target - g.yaw;
    while (diff >  EM_PI) diff -= 2.0f * EM_PI;
    while (diff < -EM_PI) diff += 2.0f * EM_PI;
    float step = TURN_SPEED * FRAME_DT;
    if (diff >  step) diff =  step;
    if (diff < -step) diff = -step;
    g.yaw += diff;
    if (g.yaw >  EM_PI) g.yaw -= 2.0f * EM_PI;
    if (g.yaw < -EM_PI) g.yaw += 2.0f * EM_PI;
}

/* FOOTSTEPS — the native func_00187350 sound slice + its func_00182430
 * surface mapper (decomp FINDINGS.md "FOOTSTEP SURFACE TABLE", s37
 * static decode of the full call tree): when the locomotion clip's
 * cycle position crosses a trigger frame (frameA/frameB of its
 * D_00248C90 row), the engine submits TWO positional sounds
 * back-to-back, each with its OWN random variant draw:
 *
 *   surface_id = BLOCK(attr) + GAIT_SUB(gait) + rand5()
 *   gear_id    = EM_SFX_STEP_GEAR_BASE       + rand5()
 *
 * There is NO per-surface id table in the engine — the mapping is
 * compiled-in immediates inside func_00182430: a 17-id block per floor
 * material (footstep_block below) + the tier sub-base (a1==3 -> +0xA,
 * a1==2 -> +5, else +0; a1 = actor +0x25C, the ramped locomotion
 * tier — run +0xA, jog +5, walk +0) + rand5 =
 * func_00179B90 = (rand() & 7) with 5..7 folded to 0..2 (0..4, the low
 * three values twice as likely). This REPLACES the s29-era port guess
 * (fixed pairs 0x15/0x16 + 0x139/0x13A, both alternating L/R): the s29
 * "floor A vs floor B" capture was the SAME material at two tiers
 * (block 0x10 + 5 -> 0x15.. jog, + 0xA -> 0x1A.. run), and the
 * observed "pairs" were the rand bias toward 0..2 — neither layer
 * alternates. (The decal half of func_00187350 — step decals/FX via
 * func_00187EE0 — is still untranslated.) */

/* rand5 — func_00179B90. PORT NOTE: a private deterministic LCG (ANSI
 * minimal-standard constants, high bits) stands in for the EE libc
 * rand() the engine draws from, so the self-tests reproduce run to
 * run; the engine never seeds rand either. */
static unsigned footstep_rand5(void)
{
    static uint32_t s = 0x00187350u;   /* seed: the slice's own vaddr */
    s = s * 1103515245u + 12345u;
    unsigned v = (s >> 16) & 7u;
    return v >= 5u ? v - 5u : v;
}

/* Floor surface attr — the footing update func_00175900's attr copy:
 * after its own down-probe hits, the engine stores the collision
 * result record's surface-attr byte (+0x1A of the grid poly node) in
 * actor +0x23A every frame; a probe miss writes 0. The port probes at
 * step time instead (same result for a grounded player), reusing the
 * player height-resolve probe walk: step past non-walkable crossings,
 * take the first FLOOR/SLOPE hit's attr (EmCollHit.attr — the native
 * mirror of the poly node's +0x1A). No collision world -> attr 0,
 * which footstep_block maps to the default block 0x10 anyway. (The
 * movable-object override — standing on a crate forces attr 2/4 — and
 * the 0x5A/0x5B/0x5C first-contact one-shots are untranslated.) */
static uint8_t footstep_floor_attr(void)
{
    if (!g.coll.poly_count) return 0;
    float from[3] = { g.pos[0], g.pos[1] + FLOOR_PROBE_UP,   g.pos[2] };
    float down[3] = { g.pos[0], g.pos[1] - FLOOR_PROBE_DOWN, g.pos[2] };
    EmCollHit hit;
    for (int i = 0; i < 8; i++) {
        if (!em_collision_segment_query(&g.coll, from, down,
                                        EM_COLL_SET_CELLS |
                                        EM_COLL_SET_GRID, 0, &hit))
            return 0;                  /* probe miss: +0x23A = 0 */
        if (hit.surf_class == EM_SURF_FLOOR ||
            hit.surf_class == EM_SURF_SLOPE)
            return hit.attr;
        if (hit.point[1] - 1e-3f <= down[1])
            return 0;
        from[1] = hit.point[1] - 1e-3f;
    }
    return 0;
}

/* BLOCK(attr) — func_00182430's compiled-in material bases (FINDINGS
 * table; stride 0x11 = 17 ids per material: 3 tier sub-bases x 5
 * variants + 2 spare landing/scuff slots). */
static unsigned footstep_block(uint8_t attr)
{
    switch (attr) {
    case 1:    return 0x21u;
    case 2:    return 0x32u;
    case 3:    return 0x43u;
    case 4:    return 0x54u;
    case 5:    return 0x65u;
    case 6: case 7: return 0xA9u;
    case 8:    return 0x87u;
    case 0xD:  return 0xDCu;
    case 0xE:  return 0xEDu;
    case 0x5A: return 0x76u;  /* wet/puddle surface */
    case 0x5B: return 0xBAu;  /* water SHALLOW; DEEP (0xCB) needs the
                               * +0x23C depth state — untranslated */
    case 0x5C: return 0x98u;
    default:   return 0x10u;  /* attr 0 + any unmapped material — the
                               * office floor (grid attr 0, FINDINGS
                               * office cross-check) */
    }
}

/* One footstep at `tier` (the engine mapper's a1 = +0x25C; the locomotion paths
 * pass actor +0x25C, the melee impact gates a scripted 1..3).
 * EM_STEP_TRACE=1 prints each step's resolved attr/ids (debug). */
static void footstep_play(int tier)
{
    uint8_t  attr = footstep_floor_attr();
    unsigned sub  = tier == 3 ? 0xAu : tier == 2 ? 5u : 0u;
    unsigned surf = footstep_block(attr) + sub + footstep_rand5();
    unsigned gear = EM_SFX_STEP_GEAR_BASE + footstep_rand5();
    static int trace = -1;
    if (trace < 0) trace = getenv("EM_STEP_TRACE") != NULL;
    if (trace)
        printf("step: tier %d attr 0x%02X -> surface 0x%03X gear 0x%03X\n",
               tier, attr, surf, gear);
    /* engine: positional play_sound(actor, id, 0) radius 300 for both
     * layers (FINDINGS footstep decode) — source = the player, so the
     * gains come out center/full by the play_sound math itself */
    em_sfx_play_at(surf, g.pos, 300.0f);
    em_sfx_play_at(gear, g.pos, 300.0f);
}

/* Cyclic edge test: did the looping clip playhead cross `trig` going
 * prev -> cur (both in frames, cur may have wrapped past 0)? */
static int step_crossed(double prev, double cur, double trig)
{
    if (cur == prev) return 0;                    /* standing: frozen */
    if (cur > prev) return prev < trig && trig <= cur;
    return trig > prev || trig <= cur;            /* wrapped the loop */
}

/* AIM POSE LADDER blend (the dispatch half of the func_0017ABA0 steer
 * — anim_slot_index/anim_matrix_dispatch sampling the D_00248B70 sub-0
 * ladder 0x112..0x11A by the blends +0x278/+0x27C, two-buffer blend
 * func_00179CA0). The port evaluates the bilinear 3x3 pose grid at the
 * shared playhead and lerps the palettes (the same matrix-lerp the
 * port's idle/walk crossfades use — a documented stand-in for the
 * engine's bone-channel blend). Returns 1 when it produced the
 * palette (all needed ladder clips present), 0 to fall back to the
 * plain base-clip evaluation. */
static int aim_ladder_eval(double t)
{
    float p  = g.aim_pitch, yb = g.aim_yawb;
    int   up = p >= 0.5f;
    int   rt = yb < 0.5f;                  /* screen-right column */
    float wp = up ? (p - 0.5f) * 2.0f : (0.5f - p) * 2.0f;
    float wy = rt ? (0.5f - yb) * 2.0f : (yb - 0.5f) * 2.0f;

    unsigned id01 = rt ? 0x115 : 0x116;            /* level, yawed   */
    unsigned id10 = up ? 0x113 : 0x114;            /* pitched, ahead */
    unsigned id11 = up ? (rt ? 0x117 : 0x119)      /* corner          */
                       : (rt ? 0x118 : 0x11A);
    struct { unsigned id; float w; } s[4] = {
        { 0x112, (1.0f - wp) * (1.0f - wy) },
        { id01,  (1.0f - wp) * wy },
        { id10,  wp * (1.0f - wy) },
        { id11,  wp * wy },
    };
    uint32_t n = g.model.bone_count * 16;
    int      first = 1;
    for (int i = 0; i < 4; i++) {
        if (s[i].w <= 0.0f) continue;
        int ci = em_model_clip_index(&g.model, s[i].id);
        if (ci < 0) return 0;              /* old EMDL: no ladder bake */
        em_model_palette_at(&g.model, (uint32_t)ci, t, g.aim_palette);
        if (first) {
            for (uint32_t k = 0; k < n; k++)
                g.player_palette[k] = g.aim_palette[k] * s[i].w;
            first = 0;
        } else {
            for (uint32_t k = 0; k < n; k++)
                g.player_palette[k] += g.aim_palette[k] * s[i].w;
        }
    }
    return !first;
}

/* The analytic aim direction the camera (and the self-tests) consume —
 * the pose the ladder blend selects, expressed as a world ray (the
 * engine reads the equivalent from the posed hand matrix, gun+0xC0).
 * Pose pitches/yaws are the values MEASURED from the baked ladder
 * clips (constants block above). */
static void aim_dir_get(float out[3])
{
    float p  = g.aim_pitch, yb = g.aim_yawb;
    float pit_deg = p >= 0.5f
        ? AIM_POSE_CTR_DEG + (p - 0.5f) * 2.0f *
              (AIM_POSE_UP_DEG - AIM_POSE_CTR_DEG)
        : AIM_POSE_CTR_DEG - (0.5f - p) * 2.0f *
              (AIM_POSE_DOWN_DEG + AIM_POSE_CTR_DEG);
    /* yaw blend: 0 = screen right = yaw DECREASING in the port basis */
    float yaw = g.yaw + (yb - 0.5f) * 2.0f *
                (AIM_POSE_YAW_DEG * EM_PI / 180.0f);
    float pit = pit_deg * EM_PI / 180.0f;
    out[0] = sinf(yaw) * cosf(pit);
    out[1] = sinf(pit);
    out[2] = cosf(yaw) * cosf(pit);
}

/* ------------------------------------------------------------------ */
/* PLAYER DAMAGE & DEATH machine (the PD_* constants block above)      */
/* ------------------------------------------------------------------ */

/* Is the player in a hit reaction / dying / at the game-over screen?
 * (the movement + input + menu lock; also the producer-side immunity
 * gate together with pd_iframes — engine: every producer requires the
 * event byte == 1, which holds from reaction start until the +0x20E
 * window expires after the flinch). */
static int player_damage_locked(void)
{
    return g.pd_state != 0 || g.go_state != 0;
}

/* func_0021C270 — apply pending INFECTION damage. */
static void player_apply_infection(void)
{
    if (g.pd_pend_inf == 0.0f) return;
    g.status.infection += g.pd_pend_inf;
    g.pd_pend_inf = 0.0f;
    g.pd_inf_hit  = 1;                     /* +0x1F1 = 1: voice select */
    if (g.status.infection >= 100.0f) {
        g.status.infection = 100.0f;
        if (g.status.health > PD_INFECTED_MAX)
            g.status.health = PD_INFECTED_MAX;
        if (!g.pd_infected) {
            g.pd_infected = 1;             /* +0x234 / 0x8104E4 latch */
            /* the display max swap to 60 — C14: the same +0x234 flag
             * drives the HUD's "/60" max (em_hud.h) */
            g.status.health_max = PD_INFECTED_MAX;
            em_sfx_play_at(PD_SFX_INFECTED, g.pos, 300.0f); /* engine
                                              * play_sound radius 300 */
        }
    }
}

/* func_0021C350 — apply pending HEALTH damage. */
static void player_apply_health(void)
{
    if (g.pd_pend_hp == 0.0f) return;
    g.status.health -= g.pd_pend_hp;
    g.pd_pend_hp = 0.0f;
    g.pd_inf_hit = 0;                      /* +0x1F1 = 0 */
    if (g.status.health <= PD_LOW_HEALTH)
        g.pd_low = 1;                      /* +0x235 |= 1 */
    if (g.status.health <= 0.0f) {
        g.status.health = 0.0f;
        /* event byte = 2 — the death branch runs in the processor */
    }
}

/* state 2 sub 0 phase 0 — FLINCH entry (func_0021D800): voice + the
 * real reaction clip (family by RNG bit, side by a second draw —
 * func_0021D1A0's side test is a hit-direction check, untranslated;
 * flagged as a second RNG bit). */
static void player_enter_flinch(void)
{
    int      armed = em_weapon_state() != EM_WPN_HOLSTERED;  /* +0x236 */
    unsigned fam   = footstep_rand5() & 1u;   /* func_00122BB8 & 1 */
    unsigned side  = footstep_rand5() & 1u;   /* func_0021D1A0 stand-in */
    unsigned clip;
    if (g.pd_infected)
        clip = PD_CLIP_FLINCH_INF;
    else if (armed)
        clip = fam ? PD_CLIP_FLINCH_ARM_B : PD_CLIP_FLINCH_ARM_A;
    else
        clip = fam ? (side ? PD_CLIP_FLINCH_B1 : PD_CLIP_FLINCH_B0)
                   : (side ? PD_CLIP_FLINCH_A1 : PD_CLIP_FLINCH_A0);
    em_sfx_play_at(g.pd_inf_hit ? PD_SFX_HURT_INF : PD_SFX_HURT,
                   g.pos, 300.0f);            /* player-attached, r=300 */
    if (!em_game_anim_request(clip, 1.0f))
        clip = 0;                  /* clip-less asset: timed fallback */
    g.pd_state = 2;
    g.pd_sub   = 0;
    g.pd_phase = clip ? 5 : 2;     /* 5 = wait commit; 2 = clip-less
                                    * timed fallback */
    g.pd_hold  = 0;
    g.pd_clip  = clip;
    /* rumble func_001B61C0(0, 0xC0, 5, 0) — no force-feedback backend */
}

/* state 2 sub 1/3 phase 0 — DEATH entry (func_0021E240 /
 * func_0021E830). The +0x1F0 = 0x40/0x3F write is only the category
 * marker — the sub handler requests the REAL clip itself. */
static void player_enter_death(void)
{
    int      armed = em_weapon_state() != EM_WPN_HOLSTERED;  /* +0x236 */
    unsigned clip  = g.pd_infected ? PD_CLIP_DEATH_INF
                   : armed         ? PD_CLIP_DEATH_ARM
                                   : PD_CLIP_DEATH;
    em_sfx_play_at(PD_SFX_DEATH_VOICE, g.pos, 300.0f); /* 0x146, r=300 */
    em_sfx_play_at(PD_SFX_DEATH_BODY,  g.pos, 300.0f); /* 0x151, r=300 */
    if (!em_game_anim_hold(clip, 1.0f))
        clip = 0;                  /* clip-less asset: straight to hold */
    g.pd_state    = 2;
    g.pd_sub      = g.pd_infected ? 3 : 1;
    g.pd_phase    = clip ? 5 : 2;  /* 5 = wait commit */
    g.pd_hold     = PD_CORPSE_HOLD;
    g.pd_clip     = clip;
    g.pd_cue_fall = 0;
    g.pd_cue_thud = 0;
    /* gore effect 0x80000051 (infected) — no effect system: flagged */
    printf("player death: clip 0x%X (%s)%s\n", clip,
           g.pd_infected ? "infected" : armed ? "armed" : "unarmed",
           clip ? "" : " — EMDL carries no death clip (timed fallback)");
}

/* func_0021C440 (generic tail) — the per-frame damage processor:
 * consume the pending amounts, then route to flinch or death. Called
 * once per gameplay frame after the producers ran. */
static void player_damage_process(void)
{
    if (g.pd_pend_hp == 0.0f && g.pd_pend_inf == 0.0f) return;
    /* invulnerable: hit-reacting/dead, or inside the post-hit window
     * (engine: producers require event byte == 1) — drop the damage */
    if (player_damage_locked() || g.pd_iframes > 0) {
        g.pd_pend_hp = g.pd_pend_inf = 0.0f;
        return;
    }
    player_apply_health();           /* func_0021C350 */
    player_apply_infection();        /* func_0021C270 */
    if (g.status.health <= 0.0f)
        player_enter_death();
    else
        player_enter_flinch();
}

/* The state-2 per-frame tick (the port slice of func_0021D800 phase 1
 * / func_0021E240 / func_0021D2E0): watch the committed reaction clip,
 * fire the death-sequence sound cues, run the corpse hold, then the
 * fade-out into the game-over stand-in. Runs INSTEAD of player_move
 * (state 2 has no free-move spine). */
static void player_hurt_tick(void)
{
    if (g.pd_phase == 5) {
        /* wait for the scripted-anim COMMIT (1-frame latency — the
         * engine requests land one update after the write too) */
        if (em_game_anim_active() == g.pd_clip)
            g.pd_phase = 1;
        return;
    }
    if (g.pd_sub == 0) {
        /* FLINCH: wait for the one-shot clip to play out (sa auto-
         * clears at clip end), then arm the i-frames and exit to
         * state 1 (the engine exits to sub 7 recover — the port's
         * locomotion idle is that pose). */
        int over = g.pd_phase == 2
                 ? ++g.pd_hold >= 30      /* clip-less fallback: ~30 f */
                 : em_game_anim_active() != g.pd_clip;
        if (over) {
            g.pd_state   = 0;
            g.pd_phase   = 0;
            g.pd_hold    = 0;
            g.pd_iframes = PD_IFRAMES;       /* +0x20E = 0x3C */
        }
        return;
    }
    /* DEATH (sub 1 normal / 3 infected) */
    if (g.pd_phase == 1) {
        /* clip playing: frames-remaining sound cues (func_0021E240
         * phase 1 reads the anim clock +0x3C the same way) */
        int total = em_game_anim_frames(g.pd_clip);
        int cur   = em_game_anim_frame();
        int rem   = (cur >= 0 && total > 0) ? total - 1 - cur : -1;
        if (!g.pd_cue_fall && rem >= 0 && rem <= PD_DEATH_CUE_FALL) {
            g.pd_cue_fall = 1;
            em_sfx_play_at(PD_SFX_DEATH_FALL, g.pos, 300.0f); /* 0x156 */
        }
        if (!g.pd_cue_thud && rem >= 0 && rem <= PD_DEATH_CUE_THUD) {
            g.pd_cue_thud = 1;
            em_sfx_play_at(g.pd_infected ? PD_SFX_DEATH_THUD_I
                                         : PD_SFX_DEATH_THUD,
                           g.pos, 300.0f);
            /* + the heavy ground rumble func_001B61C0(1,0xEE,0x3C,1) */
        }
        if (rem <= 0) {
            /* clip done (held on the last frame — the corpse pose):
             * func_0021D2E0 phase 0. Blood-pool effect 0x80000043
             * under the corpse: skipped (no decal system), flagged. */
            g.pd_phase = 2;
        }
        return;
    }
    if (g.pd_phase == 2) {
        /* corpse hold (+0x28 = 0x78), then the standard fade-out —
         * func_001AEDE0(4,0), the door-transit machine. */
        if (--g.pd_hold <= 0) {
            g.pd_phase = 3;
            em_frame_fade_start(1, EM_FADE_SPEED_DOOR);
            g.go_state = 1;
        }
        return;
    }
    if (g.pd_phase == 3) {
        /* fading: at full black the engine parks the player machine
         * and func_001AE040's state-1 tail fires (D_008106B9 latch &&
         * fade == 2) -> the GAME-OVER WAIT (the decoded chain in the
         * PD block doc). Port: enter GO_SCREEN — module-0x27 screen
         * stand-in + FADE-IN + the 240 hold; from the next frame the
         * world is FROZEN (the gameplay_frame gate) and game_over_tick
         * owns the flow, modeling the engine's task replacement. */
        if (em_frame_fade_level() >= 1.0f) {
            g.pd_phase  = 4;
            g.go_state  = GO_SCREEN;
            g.go_frames = 0;
            g.go_hold   = GO_HOLD_FRAMES;          /* task+0x18 = 0xF0 */
            em_frame_fade_start(-1, EM_FADE_SPEED_DOOR); /* func_001AEE10 */
        }
    }
    /* phase 4: parked dead under the game-over/continue screens */
}

/* Passive per-frame vitals (func_0015D100 infected arm + the spine's
 * kill plane + the +0x20E i-frame countdown). The hazard-room drain
 * arm and the func_0015D000 heartbeat rumble are untranslated (see
 * the PD block doc). */
static void player_vitals_tick(void)
{
    if (g.pd_iframes > 0) g.pd_iframes--;
    if (player_damage_locked()) return;
    /* INFECTED passive drain: health -= 2.0 every 240 frames (the
     * green effect 0x80000063 per tick is skipped — no effect system,
     * flagged). Reaching 0 = the engine's event-2/type-0x63 death ->
     * the infected death sequence. */
    if (g.pd_infected && ++g.pd_drain_t >= PD_DRAIN_PERIOD) {
        g.pd_drain_t = 0;
        g.status.health -= PD_DRAIN_AMOUNT;
        if (g.status.health <= PD_LOW_HEALTH) g.pd_low = 1;
        if (g.status.health <= 0.0f) {
            g.status.health = 0.0f;
            player_enter_death();
            return;
        }
    }
    /* KILL PLANE (spine death check): Y < -200 -> engine state 6
     * (func_0015D460): zero health, fade, park — no anim, no sound. */
    if (g.pos[1] < PD_KILL_PLANE) {
        g.status.health = 0.0f;
        g.pd_state = 2;
        g.pd_sub   = 1;
        g.pd_phase = 3;            /* straight to the fade wait */
        g.pd_clip  = 0;
        em_frame_fade_start(1, EM_FADE_SPEED_DOOR);
        g.go_state = 1;
    }
}

/* GAME-OVER / CONTINUE machine — the decoded engine chain (PD block
 * doc: func_001AD4E0 wait subs 2..4 + func_001AC070/func_001AC480
 * continue states), folded into one port state machine. Runs every
 * frame from GO_SCREEN on (the frozen-world gate in gameplay_frame
 * calls it — the engine's game task is REPLACED here, so the world
 * does not simulate). Presentation: em_hud_game_over /
 * em_hud_continue, drawn UNDER the fade in frame_close_out. */
static void game_over_tick(void)
{
    const EmFrameInput *in = em_frame_input();
    if (g.go_state < GO_SCREEN) return;
    g.go_frames++;
    switch (g.go_state) {
    case GO_SCREEN:
        /* engine wait sub 3: the 240 counts down THROUGH the fade-in;
         * skip/expiry only fire once the fade machine is idle. */
        if (g.go_hold > 0) g.go_hold--;
        if (em_frame_fade_level() > 0.0f) break;     /* fade busy */
        if (g.go_hold == 0 || (in->pressed & EM_PAD_CROSS)) {
            /* CROSS skip (D_00810E74 & 0x40) or timer expiry ->
             * fade-out (func_001AEDE0(4,0)) */
            em_frame_fade_start(1, EM_FADE_SPEED_DOOR);
            g.go_state  = GO_SCREEN_OUT;
            g.go_frames = 0;
        }
        break;
    case GO_SCREEN_OUT:
        /* engine wait sub 4: at hold-black stop the cue + arm the
         * CONTINUE machine (func_001ADF00 task replacement). */
        if (em_frame_fade_level() >= 1.0f) {
            g.go_state  = GO_PROMPT;
            g.go_frames = 0;
            g.go_timer  = GO_PROMPT_FRAMES;        /* task+0x16       */
            g.go_cursor = GO_CURSOR_DEATH;         /* from-death = 1  */
            em_frame_fade_start(-1, EM_FADE_SPEED_DOOR);
        }
        break;
    case GO_PROMPT:
        /* func_001AC480 sub 2 — gated until the fade is idle. */
        if (em_frame_fade_level() > 0.0f) break;
        if (in->held == 0) {
            /* no button held: the idle timeout walks (engine: the
             * decrement runs only on input-free frames) */
            if (g.go_timer > 0 && --g.go_timer == 0) {
                em_frame_fade_start(1, EM_FADE_SPEED_DOOR);
                g.go_state  = GO_PROMPT_TIMEOUT;
                g.go_frames = 0;
            }
            break;
        }
        /* any held button re-arms the timer (engine tail) */
        g.go_timer = GO_PROMPT_FRAMES;
        if (in->pressed & (EM_PAD_START | EM_PAD_CROSS)) {
            /* confirm (engine mask 0x840) — per-option sound, then
             * fade out; dispatch happens at hold-black */
            em_sfx_play(g.go_cursor == 0 ? GO_SFX_CONFIRM0
                        : g.go_cursor == 1 ? GO_SFX_CONFIRM1
                                           : GO_SFX_CONFIRM2);
            em_frame_fade_start(1, EM_FADE_SPEED_DOOR);
            g.go_state  = GO_PROMPT_CONFIRM;
            g.go_frames = 0;
        } else if ((in->pressed & EM_PAD_DOWN) &&
                   g.go_cursor < GO_CURSOR_MAX) {
            g.go_cursor++;                          /* 0x4000 = DOWN  */
            em_sfx_play(GO_SFX_MOVE);
        } else if ((in->pressed & EM_PAD_UP) && g.go_cursor > 0) {
            g.go_cursor--;                          /* 0x1000 = UP    */
            em_sfx_play(GO_SFX_MOVE);
        }
        break;
    case GO_PROMPT_CONFIRM:
        if (em_frame_fade_level() < 1.0f) break;
        if (g.go_cursor == 0) {
            /* CONTINUE — the engine reinstalls the gameplay task
             * (func_001AB790(func_001ACEC0), D_00275BE0 = 0); the
             * port's equivalent area re-entry is the scene reload,
             * serviced by the frame machine (go_restart). */
            g.go_restart = 1;
        } else {
            /* options 1 (load game) / 2 (sub-screen): the engine's
             * targets (func_00225A00 memory-card flow / func_00200A40)
             * have no native counterpart — no save system. Both
             * RETURN TO THE PROMPT (the engine's own cancel/done path
             * for each); FLAGGED untranslated sub-screens. */
            g.go_state  = GO_PROMPT;
            g.go_frames = 0;
            g.go_timer  = GO_PROMPT_FRAMES;
            em_frame_fade_start(-1, EM_FADE_SPEED_DOOR);
        }
        break;
    case GO_PROMPT_TIMEOUT:
        /* engine sub 4: held input mid-fade cancels back to the
         * prompt; at hold-black the engine cycles to the TITLE/attract
         * screens — the port has no title scene, so it re-enters the
         * prompt (FLAGGED divergence, documented in the PD block). */
        if (in->held != 0 && em_frame_fade_level() < 1.0f) {
            em_frame_fade_start(-1, EM_FADE_SPEED_DOOR);
            g.go_state  = GO_PROMPT;
            g.go_frames = 0;
            g.go_timer  = GO_PROMPT_FRAMES;
            break;
        }
        if (em_frame_fade_level() >= 1.0f) {
            g.go_state  = GO_PROMPT;
            g.go_frames = 0;
            g.go_timer  = GO_PROMPT_FRAMES;
            em_frame_fade_start(-1, EM_FADE_SPEED_DOOR);
        }
        break;
    default:
        break;
    }
}

/* func_0015BCF0 — player actor update. The engine's per-actor spine
 * (state/AI, anim-evaluator selection, physics, sound triggers); the
 * port's slice of it is movement (frame input -> position/yaw) plus the
 * anim side: evaluate the bone palette at the current clip time, then
 * compose the world placement onto it.
 *
 * Idle<->walk crossfade (the anim-evaluator slice): the walk blend
 * weight seeks move_speed / WALK_SPEED linearly over ANIM_BLEND_TIME,
 * and the in-place walk clip advances at move_speed / WALK_CLIP_SPEED
 * so the stride tracks the ground (it freezes while standing). At
 * weight 0 the idle path is bit-exactly the old single-clip evaluation,
 * keeping EM_CAPTURE idle output stable.
 *
 * SCRIPTED ANIM (em_game.h em_game_anim_request): the commit slice of
 * the spine. If the request mailbox (sa_req, the native +0x1F2)
 * differs from the committed id (sa_cur, +0x20C), commit it — the
 * engine's func_00183090 "copy +0x1F2 -> +0x20C, anim_clip_init" path,
 * one frame after the request, exactly the original latency (requests
 * land from the world-services slot AFTER this update ran). While
 * committed, the scripted clip OWNS the palette: it plays once at
 * sa_rate frames/tick, holds its last frame, and ends back into
 * locomotion (clip over, or em_game_anim_cancel — the script
 * teardown's +0x1F2 = 0). The locomotion blend state is parked at
 * idle while suspended, so the return is the plain idle pose (the
 * door sequence re-places the player standing). */
static void actor_update(void)
{
    /* PLAYER STATE 2 (hit reaction / dying) replaces the free-move
     * spine entirely — the engine's state dispatch (func_0015BA50)
     * routes to the hurt machine instead of the action machine; the
     * committed reaction clip owns the palette through the scripted-
     * anim path below. */
    if (g.pd_state == 2) {
        player_hurt_tick();
        g.gait       = 0;
        g.move_speed = 0.0f;
        g.loco_tier  = 0;
    } else {
        player_move();
    }
    if (!g.mesh) return;

    /* scripted-anim COMMIT (func_00183090: +0x1F2 != +0x20C). */
    if (g.sa_req != g.sa_cur) {
        g.sa_cur  = g.sa_req;
        g.sa_clip = g.sa_req
                  ? em_model_clip_index(&g.model, (uint32_t)g.sa_req)
                  : -1;
        g.sa_hold = g.sa_req_hold;
        g.sa_t    = 0.0;
    }
    if (g.sa_cur && g.sa_clip >= 0) {
        const EmModelClip *cs = &g.model.clips[g.sa_clip];
        /* Evaluate, then advance; clamp at the LAST frame (one-shot —
         * em_model_palette_at would loop-blend back into frame 0). */
        double end = (double)(cs->frame_count - 1);
        double t   = g.sa_t < end ? g.sa_t : end;
        /* AIM POSE LADDER: while the held clip is the ladder BASE
         * (0x112 — the stance tops request the base code and the
         * DISPATCH picks the ladder step, exactly the engine split)
         * and the player is aiming, the bilinear pose-grid blend owns
         * the palette; the recoil restart (sa_t rewind) runs through
         * unchanged — every ladder step is the same 25 frames. */
        if (!(g.sa_cur == 0x112 &&
              (em_weapon_is_aiming() || g.r2_aim) &&
              aim_ladder_eval(t)))
            em_model_palette_at(&g.model, (uint32_t)g.sa_clip, t,
                                g.player_palette);
        palette_apply_placement(g.player_palette, g.model.bone_count,
                                g.pos, g.yaw);
        g.sa_t += (double)g.sa_rate;
        if (g.sa_t >= end + 1.0) {     /* played through: clip-end flag */
            if (g.sa_hold) {
                g.sa_t = end;          /* HELD POSE: clamp and keep the
                                        * palette (em_game_anim_hold) */
            } else {
                g.sa_req = g.sa_cur = 0;  /* (+0x200 & 0x1000 analog) */
                g.sa_clip = -1;
            }
        }
        g.walk_w = 0.0f;               /* locomotion parked at idle */
        g.idle_timer = IDLE_FIDGET_FRAMES;   /* scripted anim leaves
                                              * mode 0: cycle re-armed */
        g.idle_phase = 0;
        g.fid_w      = 0.0f;
        return;
    }

    /* Locomotion clip by TIER (the mode-1 id row {0,1,2,3} indexed by
     * the ramped locIdx +0x25C, NOT the raw gait — the 2026-06-11
     * tier-ramp correction): walk for tier 1, jog for tier 2, run for
     * tier 3, each falling back down-row when the asset lacks the
     * clip. On a clip swap the cycle restarts — the engine re-inits
     * the clip on an id change (anim_clip_init); the mid-ramp blend
     * toward the next tier's clip rides the same crossfade (flagged).
     * The DOOR TRANSIT scripted MOVE-TO (tier 1) keeps the walk clip;
     * the ARRIVAL WALK-OUT (tier 2) plays the engine's jog clip. */
    int loco = g.clip_walk;
    if (g.loco_tier >= 3 && g.clip_run >= 0)
        loco = g.clip_run;
    else if (g.loco_tier >= 2 && g.clip_jog >= 0)
        loco = g.clip_jog;
    if (loco != g.loco_clip) {
        g.loco_clip  = loco;
        g.loco_speed = (loco >= 0 && loco == g.clip_run) ? RUN_CLIP_SPEED
                     : (loco >= 0 && loco == g.clip_jog) ? JOG_CLIP_SPEED
                                                         : WALK_CLIP_SPEED;
        g.walk_t     = 0.0;
        g.step_prev  = 0.0;
    }

    float target = 0.0f;
    if (g.loco_clip >= 0) {
        target = g.move_speed > 0.0f ? 1.0f : 0.0f;
        float step = FRAME_DT / ANIM_BLEND_TIME;
        if      (g.walk_w < target - step) g.walk_w += step;
        else if (g.walk_w > target + step) g.walk_w -= step;
        else                               g.walk_w  = target;
        g.walk_t += (double)(FRAME_DT * g.move_speed / g.loco_speed);

        /* FOOTSTEP triggers (footstep_play above): the loco-cycle
         * playhead in clip FRAMES — wrapping exactly like the palette
         * evaluation — against the clip's D_00248C90 trigger frames
         * (walk id 1: 72/21; jog id 2: 26/3; run id 3: 21/2). The
         * playhead only advances while moving (rate-scaled to the
         * ground speed), so standing is silent and slower walks space
         * their steps out, exactly like the engine's clip-time test. */
        int run = g.loco_clip == g.clip_run && g.clip_run >= 0;
        int jog = g.loco_clip == g.clip_jog && g.clip_jog >= 0;
        double fa = run ? RUN_STEP_FRAME_A
                  : jog ? JOG_STEP_FRAME_A : WALK_STEP_FRAME_A;
        double fb = run ? RUN_STEP_FRAME_B
                  : jog ? JOG_STEP_FRAME_B : WALK_STEP_FRAME_B;
        const EmModelClip *cw = &g.model.clips[g.loco_clip];
        double cyc = fmod(g.walk_t * (double)cw->fps,
                          (double)cw->frame_count);
        if (step_crossed(g.step_prev, cyc, fa) ||
            step_crossed(g.step_prev, cyc, fb))
            footstep_play(run ? 3 : jog ? 2 : 1);
            /* a1 = the TIER (+0x25C): run +0xA, jog +5, walk +0
             * (func_00182430 sub-bases — the s30 "walk 0x15/run 0x1A"
             * captures were tiers 2/3 under the corrected labels) */
        g.step_prev = cyc;
    }

    /* IDLE CYCLE (func_00161020 — see the clip-id block): while truly
     * idle the 300-frame timer runs; at zero the fidget 349 plays once
     * (8-frame cross-fade each way, the engine's blend arg) and the
     * breathing idle restarts with the timer re-armed. Any movement,
     * gait input, aim, melee or the door input lock leaves mode 0 and
     * resets the cycle. */
    {
        int active = g.move_speed > 0.0f || g.gait != 0 ||
                     em_weapon_is_aiming() || em_weapon_is_melee() ||
                     em_door_movement_locked();
        float fstep = FRAME_DT / IDLE_BLEND_TIME;
        if (active || g.clip_fidget < 0) {
            g.idle_timer = IDLE_FIDGET_FRAMES;
            g.idle_phase = 0;
            g.fid_w     -= fstep;          /* fade an aborted fidget out */
            if (g.fid_w < 0.0f) g.fid_w = 0.0f;
        } else if (g.idle_phase == 0) {
            g.fid_w -= fstep;
            if (g.fid_w < 0.0f) g.fid_w = 0.0f;
            if (--g.idle_timer <= 0) {     /* +0x28 hit 0: fidget */
                g.idle_phase = 1;
                g.fid_t      = 0.0;
            }
        } else {
            g.fid_w += fstep;
            if (g.fid_w > 1.0f) g.fid_w = 1.0f;
            g.fid_t += FRAME_DT;
            const EmModelClip *cf = &g.model.clips[g.clip_fidget];
            if (g.fid_t * cf->fps >= (double)(cf->frame_count - 1)) {
                /* clip-end flag (+0x200 & 0x1000): back to breathing,
                 * re-init (clip restarts at 0), timer re-armed */
                g.idle_phase = 0;
                g.idle_timer = IDLE_FIDGET_FRAMES;
                g.idle_t     = -FRAME_DT;  /* restarts at 0 after the
                                            * post-eval advance below */
            }
        }
    }

    const EmModelClip *ci = &g.model.clips[g.clip_idle];
    em_model_palette_at(&g.model, (uint32_t)g.clip_idle,
                        g.idle_t * ci->fps, g.player_palette);
    if (g.fid_w > 0.0f && g.clip_fidget >= 0) {
        const EmModelClip *cf = &g.model.clips[g.clip_fidget];
        double ft  = g.fid_t * cf->fps;
        double end = (double)(cf->frame_count - 1);
        em_model_palette_at(&g.model, (uint32_t)g.clip_fidget,
                            ft < end ? ft : end, g.walk_palette);
        uint32_t n = g.model.bone_count * 16;
        float    w = g.fid_w;
        for (uint32_t i = 0; i < n; i++)
            g.player_palette[i] += (g.walk_palette[i] -
                                    g.player_palette[i]) * w;
    }
    if (g.walk_w > 0.0f && g.loco_clip >= 0) {
        const EmModelClip *cw = &g.model.clips[g.loco_clip];
        em_model_palette_at(&g.model, (uint32_t)g.loco_clip,
                            g.walk_t * cw->fps, g.walk_palette);
        uint32_t n = g.model.bone_count * 16;
        float    w = g.walk_w;
        for (uint32_t i = 0; i < n; i++)
            g.player_palette[i] += (g.walk_palette[i] -
                                    g.player_palette[i]) * w;
    }
    palette_apply_placement(g.player_palette, g.model.bone_count,
                            g.pos, g.yaw);
    g.idle_t += FRAME_DT;   /* post-eval, matching the old g.t cadence
                             * (frame n evaluates the idle at n/60) */
}

/* func_001D1C50 — render chain build. Records the frame's draws (the
 * native VIF chain): scene parts first, then the player, matching the
 * engine's draw order; with no assets at all, the gradient test triangle
 * keeps the repo runnable standalone. */
static void render_chain_build(void)
{
    g.chain_len           = 0;
    g.chain_test_triangle = 0;

    if (!g.mesh && !g.n_scene) {
        g.chain_test_triangle = 1;
        return;
    }
    for (int i = 0; i < g.n_scene; i++) {
        g.chain[g.chain_len++] = (ChainDraw){ g.scene[i].mesh,
                                              g.scene[i].palette,
                                              g.scene[i].model.bone_count,
                                              NULL };
    }
    /* Interactive doors (actor draws — func_001BC300's publish). The
     * chain records palette POINTERS; em_door_update (the world-services
     * slot, after this build) writes this frame's pose into them before
     * the close-out flush. */
    for (int i = 0; i < em_door_count(); i++) {
        ChainDraw *cd = &g.chain[g.chain_len++];
        em_door_draw(i, &cd->mesh, &cd->palette, &cd->bone_count);
        cd->tint = NULL;          /* doors draw untinted */
    }
    /* Enemies (the actor-pool draws). Same pointer contract as the
     * doors: em_enemy_update (after this build) writes this frame's
     * pose — and the per-draw tint for the tinted slots (tendril
     * spikes, death fades) — before the close-out flush. A dead slot
     * keeps drawing only while em_enemy's corpse fade runs, then
     * drops out. */
    for (int i = 0; i < em_enemy_count(); i++) {
        ChainDraw *cd = &g.chain[g.chain_len];
        if (em_enemy_draw(i, &cd->mesh, &cd->palette, &cd->bone_count)) {
            cd->tint = em_enemy_draw_tint(i);
            g.chain_len++;
        }
    }
    /* Pickups (the func_001C4820/func_0015AFA0 actor draws — rigid
     * props, pose baked at placement; func_001C6380). A collected slot
     * stops drawing the frame it frees (em_pickup_draw returns 0). */
    for (int i = 0; i < em_pickup_count(); i++) {
        ChainDraw *cd = &g.chain[g.chain_len];
        if (em_pickup_draw(i, &cd->mesh, &cd->palette, &cd->bone_count)) {
            cd->tint = NULL;       /* items draw untinted (the engine
                                    * AURA pass is unported — flagged
                                    * in em_pickup.h) */
            g.chain_len++;
        }
    }
    if (g.mesh) {
        g.chain[g.chain_len++] = (ChainDraw){ g.mesh, g.player_palette,
                                              g.model.bone_count, NULL };
    }
}

/* func_001C1D00(0x008101D0) — once-per-area render-env init (GS regs,
 * area specials via func_001E2260/func_001E0CF0/func_001D5370). NOT
 * camera math (FINDINGS.md "CAMERA SYSTEM" corrections); skeleton no-op
 * until the render-env table is translated. */
static void render_env_init(void) {}

/* ------------------------------------------------------------------ */
/* CAMERA — the engine system (FINDINGS.md "CAMERA SYSTEM"):           */
/*   func_0018B9C0 state machine top  -> camera_update()               */
/*   func_0018BC20 mode dispatch      -> camera_mode_dispatch()        */
/*   func_0018D7B0 desired-eye solver -> camera_solve()                */
/*   func_0018C0D0 commit             -> camera_commit()               */
/* ------------------------------------------------------------------ */

/* func_0018C6A0(src, dst, max) — the engine's HORIZONTAL chase
 * primitive, one axis: clamped proportional step. d = src - dst;
 * |d| <= 1.0 -> quarter-step snap (d/4); else move |d|/6 capped at max.
 * Speed-limited exponential follow — no splines. */
static float cam_chase_h(float dst, float src, float max)
{
    float d = src - dst;
    if (fabsf(d) <= 1.0f) return dst + d * 0.25f;
    float step = fabsf(d) / 6.0f;
    if (step > max) step = max;
    return dst + (d > 0.0f ? step : -step);
}

/* func_0018C4B0(vec, target_y, max) — the VERTICAL twin: divisor 8. */
static float cam_chase_v(float dst, float src, float max)
{
    float d = src - dst;
    if (fabsf(d) <= 1.0f) return dst + d * 0.25f;
    float step = fabsf(d) / 8.0f;
    if (step > max) step = max;
    return dst + (d > 0.0f ? step : -step);
}

static float cam_wrap_pi(float a);   /* defined with the solver kit */
static void  cam_norm3(float v[3]);

/* IDLE desired eye from the struct yaw: CAM_DIST behind the player
 * along the yaw heading, CAM_EYE_HEIGHT above the player's ground Y
 * (the live values: ~33 u back, ~19 u up). Port stand-in shape — the
 * engine idle tail (func_001921D0 .L00192DDC) runs the same tether as
 * the walk camera; the port's idle keeps the yaw-anchored placement so
 * the orbit/L1 yaw state stays authoritative (PORT_DIFFERENCES D13). */
static void camera_desired_eye(EmCamera *cam)
{
    cam->eye_des[0] = g.pos[0] - sinf(cam->yaw) * CAM_DIST;
    cam->eye_des[1] = g.pos[1] + CAM_EYE_HEIGHT;
    cam->eye_des[2] = g.pos[2] - cosf(cam->yaw) * CAM_DIST;
}

/* func_00191390 — the camera pre-step (DECODED s65; s71 STATE-ID
 * CORRECTION): zeroes the per-frame extras (+0x94/+0x98) and writes
 * the PER-STATE height params +0x8C/+0x5C every frame. The -3.0/1.0
 * row matches player states 2/4/0xF ONLY — the +0x236 ELEVATED/hang
 * family (the func_0015CBA0 state map picks 2-vs-1 and 4-vs-3 on
 * that latch), NOT ordinary locomotion: a ground walk is state 3 =
 * the idle/default row, so the camera KEEPS eye +19 / target +17
 * while the player moves (PCSX2-verified — the s67 low-ride binding
 * dived the camera toward the player's feet on every step and is
 * retired). The port models no +0x236, so the row is constant:
 * 6.0 / 2.0 (the -31.2 areas natively swap to 2.0 / 6.0 — same sums,
 * +0x64 not carried). The climb family (0/2.0) and state 0x13
 * (11.0/2.0) have no native states. +0x6D != 0 -> +0x98 = 23.0 is
 * unfed (the +0x6D writer is the solver pre-pass func_0018D330). */
static void camera_prestep_00191390(EmCamera *cam)
{
    cam->aim_h  = CAM_AIM_OFFSET;     /* +0x8C = 6.0 */
    cam->var_5c = CAM_IDLE_VAR5C;     /* +0x5C = 2.0 */
}

/* func_00191D40 — the walk camera's desired-EYE-HEIGHT seek (DECODED
 * s65): want = base (+ cam+0x98, unfed) clamped to the y_hi bound;
 * proportional |d|/10 capped at `rate` (4.0 from func_00230000), d/5
 * snap inside 1.0 u. A RISE is vetoed by solver bit 0x80, a DESCENT
 * by bit 0x40 (ceiling-/floor-clamped last solve) — the engine also
 * vetoes descent on the pre-pass sight bit +0x5A & 1 (untranslated).
 * Engine area specials omitted (no native reach): area 0x10 room 1
 * subs 2/4/6 cap the eye at y_lo + 7.5 when y_lo > 100; area 3 room 1
 * x < 356 caps at y_hi - 2 above 250. */
static void cam_eye_y_seek_00191D40(EmCamera *cam, float want, float rate)
{
    if (want > cam->y_hi) want = cam->y_hi;
    float d = want - cam->eye_des[1];
    if (d > 0.0f) {
        if (cam->hit & 0x80) return;
    } else {
        if (cam->hit & 0x40) return;
    }
    if (fabsf(d) <= 1.0f) {
        cam->eye_des[1] += d / 5.0f;
    } else {
        float step = fabsf(d) / 10.0f;
        if (step > rate) step = rate;
        cam->eye_des[1] += d > 0.0f ? step : -step;
    }
}

/* func_0022FCA0 — the walk camera's desired-eye x/z TETHER (DECODED
 * s65 — the full policy in the constants block above). Consumes the
 * commit's horiz_dist (D_00810690, one frame stale — engine) and the
 * ACTUAL pair's horizontal distance (D_0081069C). */
static void camera_walk_eye_0022FCA0(EmCamera *cam)
{
    float follow = fabsf(g.cam_dist_param);          /* fabs(cam+0x0C) */
    float slack  = (g.cam_dist_param == -46.8f) ? CAM_TETHER_SLACK
                                                : 10.0f; /* cam+0x64 rule */
    float excess = cam->horiz_dist - follow;

    if (excess > 0.0f) {
        /* too far: drag the eye toward the target by the FULL excess
         * (the tow-rope — the eye follows the player's path). */
        float d[3] = { cam->tgt_des[0] - cam->eye_des[0], 0.0f,
                       cam->tgt_des[2] - cam->eye_des[2] };
        cam_norm3(d);
        cam->eye_des[0] += d[0] * excess;
        cam->eye_des[2] += d[2] * excess;
        cam->swing = 0;                          /* cam+0x03 = 0 */
        return;
    }
    if (excess >= -slack) {
        cam->swing = 0;          /* dead band: nothing moves (engine
                                  * .L0022FFE4) */
        return;
    }
    /* over-closed past the slack */
    {
        float over = excess + slack;             /* < 0 (spad 3A24) */
        float ax = cam->eye[0] - cam->tgt[0];    /* D_0081069C: the */
        float az = cam->eye[2] - cam->tgt[2];    /* ACTUAL pair     */
        if (ax * ax + az * az > CAM_TETHER_NEAR * CAM_TETHER_NEAR) {
            /* back straight out to follow - slack (the engine leaves
             * the swing latch untouched here — verbatim). */
            float d[3] = { cam->tgt_des[0] - cam->eye_des[0], 0.0f,
                           cam->tgt_des[2] - cam->eye_des[2] };
            cam_norm3(d);
            cam->eye_des[0] += d[0] * over;      /* over < 0 -> away */
            cam->eye_des[2] += d[2] * over;
            return;
        }
        /* cramped (actual eye within 8.6 u of the target): SWING.
         * Latch the direction once, against the last blocking wall's
         * published heading (cam+0x90 — zeroed at init, written by
         * the follow solver); the bearing accumulates in swing_yaw
         * (spad 0x70003A28) across latched frames. */
        if (cam->swing == 0) {
            cam->swing_yaw = atan2f(cam->tgt_des[0] - cam->eye_des[0],
                                    cam->tgt_des[2] - cam->eye_des[2]);
            cam->swing = (cam->wall_yaw - cam->swing_yaw > 0.0f) ? 1 : 2;
        }
        {
            float step = EM_PI * (CAM_SWING_DPU * over) / 180.0f;
            cam->swing_yaw += (cam->swing == 1) ? step : -step;
            cam->eye_des[0] += sinf(cam->swing_yaw) * over;
            cam->eye_des[2] += cosf(cam->swing_yaw) * over;
        }
    }
}

/* Shortest-arc yaw seek at `rate` rad/s. Returns 1 once aligned. */
static int cam_yaw_seek(EmCamera *cam, float target, float rate)
{
    float diff = target - cam->yaw;
    while (diff >  EM_PI) diff -= 2.0f * EM_PI;
    while (diff < -EM_PI) diff += 2.0f * EM_PI;
    float step = rate * FRAME_DT;
    if (fabsf(diff) <= step) {
        cam->yaw = target;
        return 1;
    }
    cam->yaw += diff > 0.0f ? step : -step;
    if (cam->yaw >  EM_PI) cam->yaw -= 2.0f * EM_PI;
    if (cam->yaw < -EM_PI) cam->yaw += 2.0f * EM_PI;
    return 0;
}

/* func_00194D10 — the director's fixed-camera trigger-volume test:
 * player XZ inside the region quad (func_001B1EA0 mode 0; all shipped
 * records are axis-aligned rects) AND |player.y - record.y| < 4.0.
 * Returns the active region or NULL. */
static const EmCamRegion *camregion_find(void)
{
    for (int i = 0; i < g.n_camregion; i++) {
        const EmCamRegion *cr = &g.camregion[i];
        if (g.pos[0] >= cr->x0 && g.pos[0] <= cr->x1 &&
            g.pos[2] >= cr->z0 && g.pos[2] <= cr->z1 &&
            fabsf(g.pos[1] - cr->ygate) < CAM_REGION_YGATE)
            return cr;
    }
    return NULL;
}

/* Is the default-height eye position at `yaw` wall-blocked from the
 * current look target? (the idle auto-orient's rotation-path test) */
static int cam_yaw_blocked(const EmCamera *cam, float yaw)
{
    if (!g.coll.poly_count)
        return 0;
    float eye[3] = { g.pos[0] - sinf(yaw) * CAM_DIST,
                     g.pos[1] + CAM_EYE_HEIGHT,
                     g.pos[2] - cosf(yaw) * CAM_DIST };
    EmCollHit hit;
    return em_collision_segment_query(&g.coll, (float *)cam->tgt_des, eye,
                                      EM_COLL_SET_CELLS | EM_COLL_SET_GRID,
                                      EM_COLL_ID_NONE, &hit) != 0;
}

/* func_0018BC20 — mode dispatch (struct byte +0x06 over the cut/smooth
 * jump tables jtbl_0026D950/jtbl_0026D910). Natively only MODE 0 exists:
 * the generic player-relative follow (the smooth-table inline follow).
 * On the PS2, cut-table mode 0 is func_00195130 — the per-AREA camera
 * DIRECTOR, whose per-room logic lives in the area overlays (hardcoded
 * `jal 0x823FE0` hook): the survival-horror PER-ROOM FIXED/rail camera
 * angles. Those fixed angles are real and pending: translating the
 * overlay directors lands them HERE as cut-table mode 0.
 * TODO(camera-modes): translate the overlay directors and handlers 1..15
 * as the overlay code is decompiled — one-shot reposition (5 -> 7),
 * timed hold (6), init/fallback settle (8, func_001914A0), 9..15, and
 * the scope/sniper camera (top-mode 3, func_0022EEF0, zoom 224/x). */
/* MODE 1 — the over-shoulder AIM camera, DECODED (func_00197D20
 * dispatcher + func_00197740 entry / func_00197870 steady — the "AIM
 * CAMERA MODE 1" constants block above; replaces the old +0x8C
 * target-height stand-in AND the R1 cam_yaw_seek). The entry phase
 * frames the player from the current camera heading; the steady phase
 * looks from 30 u behind the FACING toward a point 16 u along the AIM
 * DIRECTION — the view down the barrel toward the laser dot. */
static void camera_mode1_aim(EmCamera *cam)
{
    /* sub 1 -> 2 when the aim pose commits (engine: player +0x1F1 == 1
     * gates the sub advance in func_00197D20) */
    if (cam->aim_phase == 0) cam->aim_phase = 1;
    if (cam->aim_phase == 1 && em_game_anim_active() == 0x112)
        cam->aim_phase = 2;

    if (cam->aim_phase == 1) {
        /* func_00197740: TARGET = player + rotY(cam euler)*(0,19,6),
         * EYE = player + rotY*(0,19,-30). cam+0x30's yaw here is the
         * chase camera's committed heading. */
        float ey = cam->yaw;
        cam->tgt_des[0] = g.pos[0] + sinf(ey) * CAM_AIM_ENTRY_FWD;
        cam->tgt_des[1] = g.pos[1] + CAM_AIM_TGT_UP;
        cam->tgt_des[2] = g.pos[2] + cosf(ey) * CAM_AIM_ENTRY_FWD;
        cam->eye_des[0] = g.pos[0] - sinf(ey) * CAM_AIM_EYE_BACK;
        cam->eye_des[1] = g.pos[1] + CAM_AIM_TGT_UP;
        cam->eye_des[2] = g.pos[2] - cosf(ey) * CAM_AIM_EYE_BACK;
        return;
    }

    /* func_00197870 steady. The R2 stance (player state 0x2A) bases
     * the target on the position SAVED AT ENTRY (spad D_70003040);
     * the R1 stance (0xD) tracks the live position. */
    float dir[3];
    aim_dir_get(dir);
    const float *base = (g.r2_aim && !em_weapon_is_aiming())
                      ? cam->aim_entry : g.pos;
    cam->tgt_des[0] = base[0] + dir[0] * CAM_AIM_TGT_FWD;
    cam->tgt_des[1] = base[1] + CAM_AIM_TGT_UP + dir[1] * CAM_AIM_TGT_FWD;
    cam->tgt_des[2] = base[2] + dir[2] * CAM_AIM_TGT_FWD;

    /* EYE: 30 u behind the player FACING (rotEuler(player rot) — it
     * tracks the turn-in-place), height countering the aim pitch. */
    float v = -CAM_AIM_EYE_BACK * dir[1];     /* -30*dir.y */
    float f20 = 0.0f;
    if (v < -22.0f) {
        if (v < -25.0f) v = -25.0f;
        f20 = 22.0f + v;                      /* in [-3, 0] */
    }
    cam->eye_des[0] = g.pos[0] - sinf(g.yaw) * CAM_AIM_EYE_BACK;
    cam->eye_des[2] = g.pos[2] - cosf(g.yaw) * CAM_AIM_EYE_BACK;
    float eyy = g.pos[1] + CAM_AIM_TGT_UP + v;
    if (eyy > g.pos[1] + 30.0f) eyy = g.pos[1] + 30.0f;
    if (eyy < g.pos[1] + 2.0f)  eyy = g.pos[1] + 2.0f;  /* flag areas: +11 */
    /* anti-close (the < 7 u raise arm) */
    {
        float dx = cam->eye_des[0] - g.pos[0];
        float dz = cam->eye_des[2] - g.pos[2];
        if (sqrtf(dx * dx + dz * dz) < 7.0f) {
            float floor_y = g.pos[1] + 18.0f + f20;
            if (eyy < floor_y) eyy = floor_y;
        }
    }
    cam->eye_des[1] = eyy;
    /* struct yaw (+0x44/+0x9C bookkeeping): the sight-line heading */
    cam->yaw = atan2f(cam->tgt_des[0] - cam->eye_des[0],
                      cam->tgt_des[2] - cam->eye_des[2]);
}

static void camera_mode_dispatch(EmCamera *cam)
{
    /* The original gives the player NO free camera control — the only
     * yaw inputs are the R1/R2 aim camera (mode 1), the L1 orient-
     * behind and the idle auto-orient. All of them steer the authentic
     * struct yaw (+0x44)/desired vectors; everything downstream
     * consumes only those, exactly like an engine mode handler. */
    const EmFrameInput *in = em_frame_input();
    int aim_on = em_weapon_is_aiming() || g.r2_aim;

    /* FIXED-CAMERA REGION (the mode-0 director's decoded room cameras —
     * scene.txt `camregion`): inside, the room OWNS the camera. L1 is a
     * NO-OP ("L1 won't reorient because the room has a specified camera
     * angle"), the idle auto-orient is off, and only the aim camera
     * takes control — releasing it falls back to the room spec the
     * SAME frame (the instant snap, applied below). */
    const EmCamRegion *rg = camregion_find();
    g.cam_region_on = (rg != NULL) && !aim_on;

    /* MODE 1 — aim camera (decoded above; it runs inside fixed-camera
     * regions too: "the R1 aim camera still runs"). */
    if (aim_on) {
        camera_mode1_aim(cam);
        g.cam_recenter = 0;
        cam->timer    = 0;
        cam->orbit_on = 0;
        return;
    }

    if (rg) {
        g.cam_recenter = 0;          /* L1/R1-tap reorients are ignored */
        cam->timer     = 0;          /* idle auto-orient disabled */
        cam->orbit_on  = 0;
    } else {

    /* L1: one-shot orient behind the player. The ARM is DECODED
     * (func_00191000, s64): the mode-0 router func_00193EB0 (player
     * states 1/0x21, cam mode 0, not fixed) and the walk camera
     * func_00230000 check the press edge of the config halfword at
     * spad 0x70003B80 (the L1 slot of the decoded config block; the
     * port's EM_PAD_L1 maps the same default binding), goal yaw = the
     * player heading +0xC4 (+pi when action code +0x1F0 == 6 — no
     * native producer, untranslated), a 3-deg deadband DROPS already-
     * aligned presses, and the orbit radius cam+0x4C = the actual
     * horiz eye<->target distance |D_0081069C| clamped into [7.0,
     * |cam+0x64| = 46.8]. It arms director sub-state 3 — whose MOTION
     * handler is still unread: the port seeks at the orient-family
     * rate (CAM_L1_RATE, flagged) around the armed radius (placement
     * shape inferred from the sub-state-2 orbit, flagged). R1 no
     * longer seeks here — the aim camera above owns the armed
     * stance. */
    if (in->pressed & EM_PAD_L1) {
        float diff = cam_wrap_pi(g.yaw - cam->yaw);
        if (fabsf(diff) > CAM_IDLE_ORIENT_DEADBAND) {  /* 3 deg */
            float rx = cam->eye[0] - cam->tgt[0];
            float rz = cam->eye[2] - cam->tgt[2];
            float rad = sqrtf(rx * rx + rz * rz);
            if (rad < CAM_L1_MIN_RAD)   rad = CAM_L1_MIN_RAD;
            if (rad > SOLV_DIST_PARAM)  rad = SOLV_DIST_PARAM;
            cam->orbit_rad = rad;                      /* +0x4C */
            g.cam_recenter = 1;                        /* sub-state 3 */
        }
    }
    if (g.cam_recenter) {
        if (cam_yaw_seek(cam, g.yaw, CAM_L1_RATE * 60.0f))
            g.cam_recenter = 0;
        cam->timer    = 0;
        cam->orbit_on = 0;
    }

    /* IDLE AUTO-ORIENT — DECODED (the constants block above): the
     * struct timer +0x08 runs while the player is idle/walking with a
     * clear solver byte; at 481 frames (= the fidget timer 300 + the
     * 180-frame look-around clip: the END of the look-around idle) it
     * arms the 0.2 deg/frame ORBIT (director sub-state 2,
     * func_00193D90) around the saved eye<->target radius, stopping at
     * walls and on any player action. */
    int idle_ok = g.gait == 0 && g.move_speed <= 0.0f &&
                  !g.cam_recenter && !(in->held & EM_PAD_L1) &&
                  !em_weapon_is_melee();
    if (!idle_ok) {
        cam->timer    = 0;
        cam->orbit_on = 0;
    } else if (cam->orbit_on) {
        float diff = cam->orbit_tgt - cam->yaw;
        while (diff >  EM_PI) diff -= 2.0f * EM_PI;
        while (diff < -EM_PI) diff += 2.0f * EM_PI;
        float step  = CAM_ORBIT_RATE;       /* rad/FRAME (0x3B64C389) */
        float trial = cam->yaw + (diff > 0.0f ? step : -step);
        int   blocked;
        if (fabsf(diff) <= step) {
            cam->yaw      = cam->orbit_tgt; /* aligned: back to follow */
            cam->orbit_on = 0;
            blocked       = 0;
        } else if (!(blocked = cam_yaw_blocked(cam, trial))) {
            cam->yaw = trial;
        } else {
            cam->orbit_on = 0;              /* wall in the path: cancel
                                             * (engine: solver bits
                                             * 0xD/0xB by direction) */
        }
        static int trace = -1;              /* EM_CAMERA_TRACE debug */
        if (trace < 0) trace = getenv("EM_CAMERA_TRACE") != NULL;
        if (trace && (g.frame_no % 30 == 0 || blocked))
            printf("camera: frame %d auto-orient yaw %.3f -> player "
                   "%.3f%s\n", g.frame_no, cam->yaw, g.yaw,
                   blocked ? " [WALL — stopped]" : "");
    } else {
        if (cam->hit & 9)                   /* engine mask 9 — the solver
                                             * bits are REAL now (bit 8 =
                                             * ceiling involvement, bit 1
                                             * = side other-class; a
                                             * plain wall pull-in is 0
                                             * and does NOT reset, s46/
                                             * s61 decode) */
            cam->timer = 0;
        else if (++cam->timer >= CAM_IDLE_ORIENT_FRAMES) {
            cam->timer = 0;
            float diff = g.yaw - cam->yaw;
            while (diff >  EM_PI) diff -= 2.0f * EM_PI;
            while (diff < -EM_PI) diff += 2.0f * EM_PI;
            if (fabsf(diff) > CAM_IDLE_ORIENT_DEADBAND &&
                !cam_yaw_blocked(cam, cam->yaw + (diff > 0.0f
                                                  ? CAM_ORBIT_RATE
                                                  : -CAM_ORBIT_RATE))) {
                cam->orbit_on  = 1;          /* cam+0x01 = 2 */
                cam->orbit_tgt = g.yaw;      /* cam+0x48 */
                float rx = cam->eye[0] - cam->tgt[0];
                float rz = cam->eye[2] - cam->tgt[2];
                cam->orbit_rad = sqrtf(rx * rx + rz * rz); /* +0x4C */
            }
        }
    }

    }   /* !rg — free-camera orient inputs */

    /* Mode 0 generic follow — the target side is func_001916C0
     * (decoded s65, cut-table cases): desired target chases the player
     * x/z at <= 2.0 u/frame and its height seeks player.y + 11 +
     * cam[0x8C] at <= 4.0 — i.e. +17 idle, +8 while moving (the walk
     * table writes 0x8C = -3). The idle case's extra 0.3 * shaped-
     * excess dip term is not modeled (a <= 6 u target sag while
     * over-close AND idle; the walking case has no such term). */
    cam->tgt_des[0] = cam_chase_h(cam->tgt_des[0], g.pos[0], CAM_TGT_CAP_XZ);
    cam->tgt_des[2] = cam_chase_h(cam->tgt_des[2], g.pos[2], CAM_TGT_CAP_XZ);
    cam->tgt_des[1] = cam_chase_v(cam->tgt_des[1],
                                  g.pos[1] + CAM_BASE_H + cam->aim_h,
                                  CAM_TGT_CAP_Y);

    /* AUTO-ORIENT ORBIT eye (func_00193D90): the desired eye circles
     * the target at the radius saved when the orbit armed. The L1
     * seek (sub-state 3) rides the same +0x4C radius — its decoded
     * arm (func_00191000) wrote it above; the placement shape is the
     * sub-state-2 one (motion handler unread, flagged). */
    if ((cam->orbit_on || g.cam_recenter) && !g.cam_region_on) {
        cam->eye_des[0] = cam->tgt_des[0] - sinf(cam->yaw) * cam->orbit_rad;
        cam->eye_des[1] = g.pos[1] + CAM_EYE_HEIGHT;
        cam->eye_des[2] = cam->tgt_des[2] - cosf(cam->yaw) * cam->orbit_rad;
        cam->swing = 0;
    } else if (g.cam_region_on) {
        /* ROOM SPEC: pin the desired eye to the region's fixed eye (the
         * director writes cam+0x10/14/18 = the record spec; the target
         * above keeps tracking the player, like the snow case). The
         * struct yaw becomes the fixed sight-line heading so the
         * camera-relative movement controls stay coherent, and an R1
         * release re-enters here the SAME frame — camera_solve's region
         * path hard-copies eye = spec (no chase): the observed INSTANT
         * snap-back. */
        cam->eye_des[0] = rg->eye[0];
        cam->eye_des[1] = rg->eye[1];
        cam->eye_des[2] = rg->eye[2];
        cam->yaw = atan2f(g.pos[0] - rg->eye[0], g.pos[2] - rg->eye[2]);
        cam->swing = 0;
    } else if (g.gait != 0 || g.move_speed > 0.0f) {
        /* MOVING-PLAYER CAMERA — s71: ordinary walking is player
         * state 3 = func_001921D0's DEFAULT branch (.L00192DDC),
         * which runs the same tow-rope excess pull and the same
         * func_00191D40 eye-Y seek as the elevated handler
         * (func_00230000) but with the IDLE height row (+19/+17 —
         * the constants block above): the TOW-ROPE tether owns the
         * eye x/z, the decoded seek owns the eye height (which now
         * STAYS at idle level on the ground — PCSX2-verified, no
         * dive), and the heading is an OUTPUT. */
        camera_walk_eye_0022FCA0(cam);
        cam_eye_y_seek_00191D40(cam,
                                g.pos[1] + CAM_BASE_H + cam->var_5c
                                         + cam->aim_h,
                                CAM_EYE_CAP);
        /* func_00230000 tail: cam+0x44 = atan2(actual eye -> actual
         * target) — one frame stale here exactly like the engine's
         * end-of-frame write feeding the next frame's consumers. */
        cam->yaw = atan2f(cam->tgt[0] - cam->eye[0],
                          cam->tgt[2] - cam->eye[2]);
    } else {
        camera_desired_eye(cam);
        cam->swing = 0;
    }

    /* EM_CAMERA_TRACE=1 — region enter/leave transitions (debug). */
    {
        static int trace = -1, was_on = 0;
        if (trace < 0) trace = getenv("EM_CAMERA_TRACE") != NULL;
        if (trace && g.cam_region_on != was_on)
            printf("camera: frame %d %s camregion%s\n", g.frame_no,
                   g.cam_region_on ? "ENTER" : "LEAVE",
                   g.cam_region_on ? " — eye pinned to the room spec"
                                   : "");
        was_on = g.cam_region_on;
    }
}

static float cam_dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cam_norm3(float v[3])               /* func_00102760 */
{
    float l = sqrtf(cam_dot3(v, v));
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static float cam_wrap_pi(float a)               /* func_001B1470 */
{
    while (a >  EM_PI) a -= 2.0f * EM_PI;
    while (a < -EM_PI) a += 2.0f * EM_PI;
    return a;
}

/* func_0018DD20 — THE blocked-eye solver, DECODED 2026-06-11 (s61)
 * from the full 6984-byte .s read; replaces the port-invented rise
 * model (PORT_DIFFERENCES D3). This is the STYLE-0 path — the one the
 * whole generic gameplay camera uses: the idle states (1/0x26/0x27,
 * func_001921D0 tail) and the locomotion states (2/4/0xF,
 * func_00230000) both call func_0018D7B0(cam, 0), which runs this
 * solver over collision mask 6 (static cells + grid, NO movable
 * hulls) and then smooth-chases the actual eye at cap 4.0/frame.
 * (Styles 3/4 — cinematic player states — take a reduced branch in
 * the same function; style 5 = director cams -> func_0018D910 [bounds
 * maintenance only, cam_solver_0018D910 below]; style 2/6 = aim/scope
 * -> func_0018F870 [decoded s64, cam_solver_0018F870 below]. The
 * dispatcher also runs a
 * pre-pass func_0018D330 that publishes sight-ray bits to cam+0x5A
 * and the overhead-ceiling Y to cam+0x60 — no decoded consumer yet,
 * untranslated.)
 *
 * Decoded policy, in order:
 *  1. PRIMARY PROBE: desired target -> desired eye EXTENDED 1.5 u.
 *     No hit -> no first-stage response. On a hit: publish the
 *     surface class (cam+0x58) and the wall heading (cam+0x90), and
 *     classify GLANCING = dot(horiz sight dir, horiz hit normal) <
 *     sin45 (the normal's RAW horizontal component — steep faces
 *     read as glancing, engine-verbatim).
 *  2. HEAD-CLEAR WAIVER (style != 3, dist <= |cam+0x0C| = 46.8):
 *     re-probe from (target.x, player.y + 17.5, target.z) to the
 *     extended eye. CLEAR -> the block is waived outright (low walls
 *     never move the camera — the raised line sees the eye over
 *     them); a ceiling-class hit within 1 u of the eye is forgiven
 *     too.
 *  3. FIRST-STAGE RESPONSE while still blocked:
 *      - ceiling class (0x8800): eye = hit, y -= 1 (duck under),
 *        x/z += 0.5 * sight dir; widen the carried Y bounds to admit
 *        it.   [result bit 8]
 *      - wall (0x2000): REVERSE probe eye -> target; a wall hit
 *        whose normal does NOT oppose the first (dot > -0.3) means
 *        the eye sits in a wedge/behind a corner: eject x/z to 4 u
 *        along the reverse-hit normal, height kept.
 *      - otherwise PULL-IN: eye x/z = hit + 0.5 * sight dir, HEIGHT
 *        KEPT. (The "apparent rise": constant absolute height +
 *        collapsing horizontal distance = the camera looks down on
 *        the player's head. No bits set — a plain pull-in returns 0.)
 *  4. SIDE STAGE (sight line clear OR glancing): probe 5.5 u to each
 *     side of the eye perpendicular to the sight line (glancing
 *     variant sweeps from 3 u on the far side to 1.5 u target-ward +
 *     5.5 on the near side, with same-wall/cross-normal rejects and a
 *     confirm probe from the first hit point at eye height). A valid
 *     side hit proposes a candidate at (hit -/+ side vector) + 0.1
 *     along the probe ray — i.e. the eye slides to keep ~5.4 u of
 *     lateral clearance [bits 2 (left) / 4 (right), 8 = ceiling-
 *     underside candidate keeps its own Y, 1 = other-class]. BOTH
 *     sides hitting centers the eye x/z on the midpoint of the two
 *     hit points (tight corridors). Engine quirk kept verbatim: the
 *     RIGHT side's ceiling-underside and other-class candidates set
 *     bits 8/1 but not 4, so the selection below never applies them.
 *  5. FINAL Y POLICY: if blocked by a NON-wall class: ceiling ->
 *     eye.y = min(eye.y, hit.y - 1); floor class -> eye.y =
 *     max(eye.y, hit.y + 1) — a LITERAL 1-u rise over floor-class
 *     blockers (banks/ledges clipping the sight line). Then clamp
 *     eye.y into the carried bounds, RE-PROBE the bounds (from the
 *     eye pulled 1.5 u toward player+11up: floor 200 down [class
 *     0x7000, engine-verbatim includes WALL] + 17 -> lower; ceiling
 *     200 up [0x8800] - 1 -> upper, else eye + 200; lower forced to
 *     upper - 3 if crossed) and clamp again [bits 0x40 floor-clamped
 *     / 0x80 ceiling-clamped]. The eye can NEVER sink below 17 u
 *     over the floor beneath it nor poke above the ceiling.
 *  (Engine per-area specials not reachable by the port's scenes are
 *  omitted, flagged: area 0x12 eye-z clamp [169.5, 230.6] + its
 *  upward bound probe variant, area 0x15 z>260 floor-bound 70.)
 *
 * Returns the result-bit byte (-> cam->hit, struct +0x07). Mutates
 * cam->eye_des in place exactly like the engine mutates cam+0x10 —
 * the mode handler re-poses it every frame, so nothing carries over.
 *
 * The actual eye (D_008105D0) then smooth-chases the solved desired
 * eye per axis, capped 4.0 u/frame (dispatcher style-0 tail) — every
 * response above inherits the engine's own smoothing; the actual
 * target is a straight copy of the desired target (func_0018C0C0). */
static int cam_solver_0018DD20(EmCamera *cam)
{
    const unsigned mask = EM_COLL_SET_CELLS | EM_COLL_SET_GRID; /* 6 */
    float *eye = cam->eye_des;                /* cam+0x10, in place */
    const float *tgt = cam->tgt_des;          /* cam+0x20 */
    EmCollHit hit;
    int   blocked, glancing = 0, bits = 0;
    int   side_l = 0, side_r = 0;     /* validated side hits (s1/s7) */
    float ext_eye[3];                 /* spad 38A0 (extended eye)    */
    float first_pt[3] = { 0, 0, 0 };  /* spad 38C0/3950 (1st hit)    */
    float first_n[3]  = { 0, 0, 0 };  /* spad 38E0 (1st hit normal)  */
    float hdir[3]     = { 0, 0, 0 };  /* spad 3960 (horiz sight dir) */
    float lpt[3] = { 0, 0, 0 }, rpt[3] = { 0, 0, 0 }; /* 3920/3930  */
    float cand_a[3], cand_b[3];       /* sp+0xA0 / sp+0xB0           */
    uint16_t attr = 0;

    /* 1. PRIMARY PROBE — target -> eye extended 1.5 u past the eye. */
    {
        float d[3] = { eye[0] - tgt[0], eye[1] - tgt[1], eye[2] - tgt[2] };
        cam_norm3(d);
        ext_eye[0] = eye[0] + SOLV_EXT * d[0];
        ext_eye[1] = eye[1] + SOLV_EXT * d[1];
        ext_eye[2] = eye[2] + SOLV_EXT * d[2];
    }
    blocked = em_collision_segment_query(&g.coll, tgt, ext_eye, mask,
                                         EM_COLL_ID_NONE, &hit) != 0;
    if (blocked) {
        attr = hit.surf_class;
        cam->hit_attr = attr;                          /* cam+0x58 */
        memcpy(first_pt, hit.point, sizeof first_pt);
        memcpy(first_n, hit.normal, sizeof first_n);
        hdir[0] = tgt[0] - ext_eye[0];
        hdir[1] = 0.0f;                                /* horizontal */
        hdir[2] = tgt[2] - ext_eye[2];
        cam_norm3(hdir);
        /* glancing gate: RAW horizontal normal component (verbatim) */
        if (hdir[0] * first_n[0] + hdir[2] * first_n[2] < SOLV_GLANCE_COS)
            glancing = 1;
        cam->wall_yaw = cam_wrap_pi(atan2f(first_n[0], first_n[2]));

        /* 2. HEAD-CLEAR WAIVER (style != 3; dist inside the param). */
        if (cam->horiz_dist - SOLV_DIST_PARAM <= 0.0f) {
            float h = (cam->var_5c == 1.0f) ? SOLV_HEADCLR_H1
                                            : SOLV_HEADCLR_H;
            float start[3] = { tgt[0], g.pos[1] + h, tgt[2] };
            EmCollHit h2;
            if (!em_collision_segment_query(&g.coll, start, ext_eye, mask,
                                            EM_COLL_ID_NONE, &h2)) {
                blocked = 0;            /* raised line sees the eye */
            } else if (h2.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {
                float dx = h2.point[0] - ext_eye[0];
                float dy = h2.point[1] - ext_eye[1];
                float dz = h2.point[2] - ext_eye[2];
                if (dx * dx + dy * dy + dz * dz < 1.0f)
                    blocked = 0;        /* ceiling graze AT the eye */
            }
        }

        /* 3. FIRST-STAGE RESPONSE. */
        if (blocked) {
            int handled = 0;
            float dir[3] = { ext_eye[0] - tgt[0], ext_eye[1] - tgt[1],
                             ext_eye[2] - tgt[2] };
            cam_norm3(dir);
            if (attr & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {  /* 0x8800 */
                eye[0] = first_pt[0] + SOLV_PULL_IN * dir[0];
                eye[1] = first_pt[1] - 1.0f;     /* duck under it */
                eye[2] = first_pt[2] + SOLV_PULL_IN * dir[2];
                if (eye[1] < cam->y_lo) cam->y_lo = eye[1];
                if (eye[1] > cam->y_hi) cam->y_hi = eye[1];
                bits |= 8;
                handled = 1;
            } else if (attr & EM_SURF_WALL) {               /* 0x2000 */
                EmCollHit rh;
                if (em_collision_segment_query(&g.coll, ext_eye, tgt,
                                               mask, EM_COLL_ID_NONE,
                                               &rh) &&
                    (rh.surf_class & EM_SURF_WALL) &&
                    cam_dot3(rh.normal, first_n) > SOLV_WEDGE_DOT) {
                    /* wedge: the eye is buried behind a corner */
                    cam->wall_yaw = cam_wrap_pi(atan2f(rh.normal[0],
                                                       rh.normal[2]));
                    eye[0] = rh.point[0] + SOLV_WEDGE_EJECT * rh.normal[0];
                    eye[2] = rh.point[2] + SOLV_WEDGE_EJECT * rh.normal[2];
                    handled = 1;
                }
            }
            if (!handled) {
                /* PULL-IN, height kept — the "apparent rise". */
                eye[0] = first_pt[0] + SOLV_PULL_IN * dir[0];
                eye[2] = first_pt[2] + SOLV_PULL_IN * dir[2];
            }
            /* EM_CAMERA_TRACE=1 — first-stage branch (debug). */
            {
                static int trace = -1;
                if (trace < 0)
                    trace = getenv("EM_CAMERA_TRACE") != NULL;
                if (trace)
                    printf("camera: frame %d 0018DD20 BLOCKED attr "
                           "0x%04x %s%s-> des eye %.2f %.2f %.2f "
                           "(tgt %.2f %.2f %.2f)\n", g.frame_no, attr,
                           handled ? (bits & 8 ? "ceiling-duck "
                                               : "wedge-eject ")
                                   : "pull-in ",
                           glancing ? "[glancing] " : "",
                           eye[0], eye[1], eye[2],
                           tgt[0], tgt[1], tgt[2]);
            }
        }
    }

    /* 4. SIDE STAGE — clear or glancing sight line only. */
    if (!blocked || glancing) {
        float yaw  = atan2f(tgt[0] - eye[0], tgt[2] - eye[2]);
        float syaw = cam_wrap_pi(yaw - EM_PI * 0.5f);
        float su[3] = { sinf(syaw), 0.0f, cosf(syaw) };  /* unit side */
        float sv[3] = { SOLV_SIDE * su[0], 0.0f, SOLV_SIDE * su[2] };
        float twd[3] = { 0, 0, 0 };       /* unit eye-ward (glancing) */
        float start[3], end[3], gate;
        EmCollHit sh;

        if (blocked) {
            twd[0] = eye[0] - tgt[0];
            twd[1] = eye[1] - tgt[1];
            twd[2] = eye[2] - tgt[2];
            cam_norm3(twd);
        }
        cand_a[0] = cand_b[0] = eye[0];   /* sp+0xA0 / sp+0xB0 init */
        cand_a[1] = cand_b[1] = eye[1];
        cand_a[2] = cand_b[2] = eye[2];

        for (int side = 0; side < 2; side++) {
            float sgn = side == 0 ? 1.0f : -1.0f;  /* left, then right */
            if (!blocked) {
                start[0] = eye[0];
                start[1] = eye[1];
                start[2] = eye[2];
                end[0] = eye[0] + sgn * sv[0];
                end[1] = eye[1];
                end[2] = eye[2] + sgn * sv[2];
            } else {
                /* glancing sweep: far side -> 1.5 target-ward + near */
                start[0] = eye[0] - sgn * SOLV_SIDE_BACK * su[0];
                start[1] = eye[1];
                start[2] = eye[2] - sgn * SOLV_SIDE_BACK * su[2];
                end[0] = eye[0] - SOLV_SIDE_FWD * twd[0] + sgn * sv[0];
                end[1] = eye[1] - SOLV_SIDE_FWD * twd[1];
                end[2] = eye[2] - SOLV_SIDE_FWD * twd[2] + sgn * sv[2];
            }
            int hit_ok = em_collision_segment_query(&g.coll, start, end,
                                                    mask, EM_COLL_ID_NONE,
                                                    &sh) != 0;
            gate = 0.0f;
            if (hit_ok && blocked) {
                /* validation (glancing case only reaches here) */
                if (bits & 8) {           /* 1st stage was the duck */
                    gate = cam_dot3(sh.normal, hdir);
                    if (gate < SOLV_CROSS_DOT) { hit_ok = 0; gate = -1.0f; }
                } else {
                    gate = cam_dot3(sh.normal, first_n);
                    if (gate < SOLV_OPPOSE_DOT) {
                        hit_ok = 0; gate = -1.0f;
                    } else {
                        float d[3] = { sh.point[0] - end[0],
                                       sh.point[1] - end[1],
                                       sh.point[2] - end[2] };
                        if (cam_dot3(d, d) < 1.0f) {
                            hit_ok = 0; gate = -1.0f; /* far-end graze */
                        } else {
                            /* confirm: 1st hit point at eye height */
                            float cs[3] = { first_pt[0], eye[1],
                                            first_pt[2] };
                            hit_ok = em_collision_segment_query(
                                         &g.coll, cs, end, mask,
                                         EM_COLL_ID_NONE, &sh) != 0;
                            if (hit_ok) {
                                gate = cam_dot3(sh.normal, first_n);
                                if (gate < SOLV_OPPOSE_DOT) {
                                    hit_ok = 0; gate = -1.0f;
                                }
                            } else {
                                gate = -1.0f;
                            }
                        }
                    }
                }
            }
            if (side == 0) side_l = hit_ok; else side_r = hit_ok;
            if (!hit_ok)
                continue;
            memcpy(side == 0 ? lpt : rpt, sh.point, 12);
            /* response window (non-glancing: gate dot in (-0.3, 0.9)) */
            if (!glancing &&
                (!(gate < SOLV_GATE_HI) || gate <= SOLV_GATE_LO))
                continue;
            {
                float rd[3] = { end[0] - start[0], end[1] - start[1],
                                end[2] - start[2] };
                float c[3]  = { sh.point[0] - sgn * sv[0],
                                sh.point[1],
                                sh.point[2] - sgn * sv[2] };
                float *cand = side == 0 ? cand_a : cand_b;
                cam_norm3(rd);
                if (sh.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {
                    if (-sh.normal[1] < SOLV_GATE_HI) {
                        /* tilted overhang: x/z only */
                        cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                        cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                        bits |= side == 0 ? 2 : 4;
                    } else {
                        /* flat ceiling underside: candidate keeps its
                         * own Y (engine quirk: the right side sets
                         * bit 8 only — never applied below) */
                        cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                        cand[1] = c[1];
                        cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                        bits |= side == 0 ? 0xA : 0x8;
                    }
                } else if (sh.surf_class & EM_SURF_WALL) {
                    cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                    cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                    bits |= side == 0 ? 2 : 4;
                } else {
                    /* other class (floor/slope beside the eye): full
                     * candidate (right side: bit 1 only, quirk) */
                    cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                    cand[1] = c[1];
                    cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                    bits |= side == 0 ? 3 : 1;
                }
            }
        }
        /* selection */
        if ((bits & 6) == 6 || (bits & 2 && side_r) ||
            (bits & 4 && side_l)) {
            eye[0] = 0.5f * (lpt[0] + rpt[0]);   /* corridor centering */
            eye[2] = 0.5f * (lpt[2] + rpt[2]);
        } else if (bits & 2) {
            eye[0] = cand_a[0];                  /* full copy, incl. Y */
            eye[1] = cand_a[1];
            eye[2] = cand_a[2];
        } else if (bits & 4) {
            eye[0] = cand_b[0];
            eye[1] = cand_b[1];
            eye[2] = cand_b[2];
        }
    }

    /* 5. FINAL Y POLICY + BOUNDS. */
    if (blocked && !(attr & EM_SURF_WALL)) {
        if (attr & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {
            float lim = first_pt[1] - 1.0f;      /* stay under it */
            if (eye[1] > lim) eye[1] = lim;
        } else {
            float lim = first_pt[1] + 1.0f;      /* literal rise over a
                                                    floor-class blocker */
            if (eye[1] < lim) eye[1] = lim;
        }
    }
    if (eye[1] <= cam->y_lo) eye[1] = cam->y_lo; /* carried bounds */
    if (eye[1] >= cam->y_hi) eye[1] = cam->y_hi;
    {
        float pb[3], lo, hi, probe[3];
        float d[3] = { eye[0] - g.pos[0],
                       eye[1] - (g.pos[1] + SOLV_PLAYER_UP),
                       eye[2] - g.pos[2] };
        cam_norm3(d);
        pb[0] = eye[0] - SOLV_BOUND_PULL * d[0];
        pb[1] = eye[1] - SOLV_BOUND_PULL * d[1];
        pb[2] = eye[2] - SOLV_BOUND_PULL * d[2];
        probe[0] = pb[0];
        probe[1] = pb[1] - SOLV_BOUND_RANGE;     /* floor, 200 down */
        probe[2] = pb[2];
        if (em_collision_segment_query(&g.coll, pb, probe, mask,
                                       EM_COLL_ID_NONE, &hit) &&
            (hit.surf_class & (EM_SURF_FLOOR | EM_SURF_SLOPE |
                               EM_SURF_WALL)))   /* 0x7000, verbatim */
            lo = hit.point[1] + (cam->var_5c == 1.0f ? SOLV_FLOOR_PAD1
                                                     : SOLV_FLOOR_PAD);
        else
            lo = cam->y_lo - SOLV_BOUND_RANGE;
        probe[1] = pb[1] + SOLV_BOUND_RANGE;     /* ceiling, 200 up */
        if (em_collision_segment_query(&g.coll, pb, probe, mask,
                                       EM_COLL_ID_NONE, &hit) &&
            (hit.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)))
            hi = hit.point[1] - SOLV_CEIL_PAD;
        else
            hi = eye[1] + SOLV_BOUND_RANGE;
        if (lo > hi) lo = hi - 3.0f;
        cam->y_lo = lo;
        cam->y_hi = hi;
    }
    if (eye[1] <= cam->y_lo) { eye[1] = cam->y_lo; bits |= 0x40; }
    if (eye[1] >= cam->y_hi) { eye[1] = cam->y_hi; bits |= 0x80; }
    return bits;
}

/* func_0018CE60 — the vertical-bounds SETTLE helper (decoded s64; the
 * s10 note "settle vs world: 2x func_0019A910 ray queries" was this).
 * Probes 200 down / 200 up from `pt` (mask 7 for style 2, else 6) and
 * derives the eye-Y bounds:
 *   lower = floor hit + pad (style 2: +2; cam+0x5C == 1: +6; else +17)
 *     — a NON-floor-class hit whose normal has n.y <= 0.17 (a wall
 *     face seen from inside, 0x3E2E147B) keeps the RAW hit Y;
 *     no hit -> carried y_lo - 200;
 *   upper = ceiling hit - 1 (a non-ceiling-class hit with -n.y <= 0.17
 *     keeps the raw Y); no hit -> pt.y + 200;
 *   lower forced to upper - 3 if crossed.
 * Writes cam+0x50/+0x54 and (style != 5) clamps the desired eye Y into
 * them. The aim solver calls it when the primary block came from the
 * GRID walker (engine query-hub return 4) instead of clamping into the
 * previous frame's bounds. */
static void cam_bounds_settle_0018CE60(EmCamera *cam, const float pt[3],
                                       int style)
{
    unsigned mask = EM_COLL_SET_CELLS | EM_COLL_SET_GRID;
    if (style == 2)
        mask |= EM_COLL_SET_HULLS;
    float pad = (style == 2) ? AIMS_FLOOR_PAD
              : (cam->var_5c == 1.0f ? SOLV_FLOOR_PAD1 : SOLV_FLOOR_PAD);
    EmCollHit hit;
    float probe[3] = { pt[0], pt[1] - SOLV_BOUND_RANGE, pt[2] };
    float lo, hi;

    if (em_collision_segment_query(&g.coll, pt, probe, mask,
                                   EM_COLL_ID_NONE, &hit)) {
        lo = hit.point[1];
        if ((hit.surf_class & (EM_SURF_FLOOR | EM_SURF_SLOPE)) ||
            hit.normal[1] > AIMS_SETTLE_NY)
            lo += pad;
    } else {
        lo = cam->y_lo - SOLV_BOUND_RANGE;
    }
    probe[1] = pt[1] + SOLV_BOUND_RANGE;
    if (em_collision_segment_query(&g.coll, pt, probe, mask,
                                   EM_COLL_ID_NONE, &hit)) {
        hi = hit.point[1];
        if ((hit.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)) ||
            -hit.normal[1] > AIMS_SETTLE_NY)
            hi -= SOLV_CEIL_PAD;
    } else {
        hi = pt[1] + SOLV_BOUND_RANGE;
    }
    if (lo > hi) lo = hi - 3.0f;
    cam->y_lo = lo;
    cam->y_hi = hi;
    if (style != 5) {
        if (cam->eye_des[1] < lo) cam->eye_des[1] = lo;
        if (cam->eye_des[1] > hi) cam->eye_des[1] = hi;
    }
}

/* func_0018F870 — the AIM/scope wall solver, DECODED 2026-06-11 (s64)
 * from the full 0x16A4 .s read; retires the flagged CAM_WALL_MARGIN
 * stand-in. Style 2 = the aim camera (mode 1, mask 7 — movable hulls
 * IN); style 6 = the scope camera (same first stage, NO lateral
 * stages — not reachable natively, noted inline). The user-observed
 * "R1 keeps the pull-in" is engine truth — the differences from the
 * follow solver are:
 *
 *  1. PRIMARY PROBE runs from the PLAYER POSITION (+0xB0) — not the
 *     desired target, which in aim rides the gun ray 16 u ahead — to
 *     the desired eye extended 1.5 u. A hit sets result bit 1 (the
 *     follow solver's plain pull-in returns 0) and publishes the
 *     class (cam+0x58). GLANCING = dot(horiz TARGET-ward sight dir,
 *     horiz normal) < 0.99 — a much tighter square-on gate than the
 *     follow solver's sin45.
 *  2. FIRST STAGE (every hit — NO head-clear waiver, NO wedge eject):
 *      - non-wall class: widen the carried Y bound to admit the hit
 *        (ceiling-class: y_lo down to hit.y, bit 8; floor-class: y_hi
 *        up to hit.y, bit 0x10), then eye = hit (ALL 3 AXES) + 0.5 *
 *        dir on x/z (dir = unit player->ext-eye) — the eye rides the
 *        blocking surface; the -1/+1 standoff is applied by the final
 *        Y policy below.
 *      - wall (or any other class): PULL-IN, eye x/z = hit + 0.5 *
 *        dir, HEIGHT KEPT — then eye.y clamps into the PREVIOUS
 *        frame's bounds (cam+0x50/+0x54)… UNLESS the hit came from
 *        the GRID walker (engine hub return 4): then the bounds are
 *        RE-DERIVED at (hit - 1*dir) via func_0018CE60 and the clamp
 *        uses the fresh pair.
 *  3. LATERAL STAGE:
 *      - blocked SQUARE-ON (not glancing): the CORNER SLIDE — a lane
 *        1 u off the wall through the eye, swept -3 .. +5.5 u along
 *        the wall face (along = wall-normal yaw - 90). A DIFFERENT
 *        wall in the lane (normal dot vs the first < 0.9) places the
 *        eye 5.5 u back from it along the wall [bits 2 / 4 by side;
 *        the second side only probes if the first found nothing] —
 *        the over-shoulder eye slides out of corner notches.
 *      - clear OR glancing: the 5.5-u SIDE stage, the follow solver's
 *        shape with the same constants (glancing sweep starts 3 u on
 *        the far side; rejects: ceiling-case cross dot < -0.08
 *        against the horiz PLAYER-ward dir, same-wall dot < -0.998;
 *        NO far-end-graze check and NO confirm re-probe — simpler
 *        than the follow solver), candidates at (hit -/+ side vec) +
 *        0.1 along the probe ray, both-sides -> midpoint corridor
 *        centering, right-side bit-8/bit-1 quirks kept verbatim.
 *  4. FINAL Y POLICY (blocked, non-wall class): ceiling-class ->
 *     eye.y = min(eye.y, hit.y - 1); floor-class -> eye.y =
 *     max(eye.y, hit.y + 1).
 *  5. CONFIRM RE-PROBE (any response bits 0x1F): player pos -> eye;
 *     still blocked -> eye x/z = the NEW hit point (no pad).
 *  6. NEW BOUNDS for the NEXT frame (written, NOT applied): from the
 *     eye pulled 1.0 toward player + 11 up, 200 down (class 0x7000)
 *     -> lower = floor + 2.0 (the follow solver's +17 becomes +2: the
 *     aim eye may ride 2 u over the floor — why aiming can look from
 *     ankle height); 200 up (0x8800) -> upper = ceiling - 1, else
 *     eye + 200; lower forced to upper - 3.
 *  (Engine per-area specials omitted, flagged: area 0x12 clamps eye.z
 *  to [169.5, 230.6] and swaps the no-ceiling upper bound for a
 *  player-up probe — with a stale-scratch quirk when THAT misses;
 *  no native area ids.)
 *
 * Returns the result-bit byte -> cam->hit. The mode-1 handler's own
 * eye-Y clamps ([player.y+2, +30]) and the dispatcher's 8-u min-
 * distance push (camera_solve tail) then run downstream, exactly like
 * the engine. */
static int cam_solver_0018F870(EmCamera *cam)
{
    const unsigned mask = EM_COLL_SET_CELLS | EM_COLL_SET_GRID |
                          EM_COLL_SET_HULLS;          /* style-2 mask 7 */
    float *eye = cam->eye_des;                /* cam+0x10, in place */
    const float *tgt = cam->tgt_des;          /* cam+0x20 */
    EmCollHit hit;
    int   bits = 0, glancing = 0, blocked;
    float ext_eye[3];                 /* spad 38A0 (extended eye)     */
    float first_pt[3] = { 0, 0, 0 };  /* spad 38C0/3950 (1st hit)     */
    float first_n[3]  = { 0, 0, 0 };  /* spad 38E0 (1st hit normal)   */
    float pdir[3]     = { 0, 0, 0 };  /* spad 3960 (horiz player-ward
                                         sight dir)                   */
    float dir[3]      = { 0, 0, 0 };  /* spad 38A0' (unit player->eye)*/
    uint16_t attr = 0;

    /* 1. PRIMARY PROBE — player position -> eye extended 1.5 u. */
    {
        float d[3] = { eye[0] - g.pos[0], eye[1] - g.pos[1],
                       eye[2] - g.pos[2] };
        cam_norm3(d);
        ext_eye[0] = eye[0] + SOLV_EXT * d[0];
        ext_eye[1] = eye[1] + SOLV_EXT * d[1];
        ext_eye[2] = eye[2] + SOLV_EXT * d[2];
    }
    blocked = em_collision_segment_query(&g.coll, g.pos, ext_eye, mask,
                                         EM_COLL_ID_NONE, &hit);
    if (blocked) {
        int from_grid = hit.kind == EM_COLL_SET_GRID; /* hub return 4 */
        bits = 1;                     /* a plain aim block returns 1  */
        attr = hit.surf_class;
        cam->hit_attr = attr;                          /* cam+0x58 */
        memcpy(first_pt, hit.point, sizeof first_pt);
        memcpy(first_n, hit.normal, sizeof first_n);
        {
            float tdir[3] = { tgt[0] - eye[0], 0.0f, tgt[2] - eye[2] };
            cam_norm3(tdir);
            if (tdir[0] * first_n[0] + tdir[2] * first_n[2]
                    < AIMS_GLANCE_COS)
                glancing = 1;
        }
        pdir[0] = g.pos[0] - eye[0];
        pdir[2] = g.pos[2] - eye[2];
        cam_norm3(pdir);
        dir[0] = ext_eye[0] - g.pos[0];
        dir[1] = ext_eye[1] - g.pos[1];
        dir[2] = ext_eye[2] - g.pos[2];
        cam_norm3(dir);

        /* 2. FIRST STAGE — no waiver, no wedge eject. */
        if (attr & (EM_SURF_CEIL | EM_SURF_STEEPDN |
                    EM_SURF_FLOOR | EM_SURF_SLOPE)) {     /* 0xD800 */
            if (attr & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {
                if (first_pt[1] < cam->y_lo) cam->y_lo = first_pt[1];
                bits |= 8;
            } else {
                if (first_pt[1] > cam->y_hi) cam->y_hi = first_pt[1];
                bits |= 0x10;
            }
            eye[0] = first_pt[0] + SOLV_PULL_IN * dir[0];
            eye[1] = first_pt[1];          /* full copy: ride the hit */
            eye[2] = first_pt[2] + SOLV_PULL_IN * dir[2];
        } else {
            /* wall/other: PULL-IN at constant height — "R1 keeps the
             * pull-in" (the user's note) is engine truth. */
            eye[0] = first_pt[0] + SOLV_PULL_IN * dir[0];
            eye[2] = first_pt[2] + SOLV_PULL_IN * dir[2];
            if (from_grid) {
                float pb[3] = { first_pt[0] - dir[0],
                                first_pt[1] - dir[1],
                                first_pt[2] - dir[2] };
                cam_bounds_settle_0018CE60(cam, pb, 2);
            } else {
                if (eye[1] <= cam->y_lo) eye[1] = cam->y_lo;
                if (eye[1] >= cam->y_hi) eye[1] = cam->y_hi;
            }
        }
        /* EM_CAMERA_TRACE=1 — first-stage branch (debug). */
        {
            static int trace = -1;
            if (trace < 0)
                trace = getenv("EM_CAMERA_TRACE") != NULL;
            if (trace)
                printf("camera: frame %d 0018F870 BLOCKED attr 0x%04x "
                       "%s-> des eye %.2f %.2f %.2f (player %.2f %.2f "
                       "%.2f)\n", g.frame_no, attr,
                       glancing ? "[glancing] " : "",
                       eye[0], eye[1], eye[2],
                       g.pos[0], g.pos[1], g.pos[2]);
        }
    }

    /* 3. LATERAL STAGE (style 6 scope would skip it entirely). */
    if (blocked && !glancing) {
        /* CORNER SLIDE — a lane 1 u off the wall, -3 .. +5.5 along it. */
        float ayaw = cam_wrap_pi(atan2f(first_n[0], first_n[2])
                                 - EM_PI * 0.5f);
        float ua[3] = { sinf(ayaw), 0.0f, cosf(ayaw) }; /* unit along */
        float av[3] = { SOLV_SIDE * ua[0], 0.0f, SOLV_SIDE * ua[2] };
        float base[3] = { eye[0] + AIMS_CORNER_OFF * first_n[0],
                          eye[1] + AIMS_CORNER_OFF * first_n[1],
                          eye[2] + AIMS_CORNER_OFF * first_n[2] };
        for (int side = 0; side < 2; side++) {
            float sgn = side == 0 ? 1.0f : -1.0f;
            float st[3] = { base[0] - sgn * AIMS_CORNER_BACK * ua[0],
                            base[1],
                            base[2] - sgn * AIMS_CORNER_BACK * ua[2] };
            float en[3] = { base[0] + sgn * av[0],
                            base[1],
                            base[2] + sgn * av[2] };
            if (em_collision_segment_query(&g.coll, st, en, mask,
                                           EM_COLL_ID_NONE, &hit) &&
                (hit.surf_class & EM_SURF_WALL) &&
                cam_dot3(hit.normal, first_n) < AIMS_WALL_DOT) {
                eye[0] = hit.point[0] - sgn * av[0];
                eye[2] = hit.point[2] - sgn * av[2];
                bits |= side == 0 ? 2 : 4;
                break;     /* the second lane probes only if the first
                              found nothing (engine: s2 & 2 gate) */
            }
        }
    } else if (!blocked || glancing) {
        /* SIDE STAGE — the follow solver's shape, aim variant: no
         * far-end-graze check, no confirm re-probe. */
        float syaw = cam_wrap_pi(atan2f(tgt[0] - eye[0],
                                        tgt[2] - eye[2]) - EM_PI * 0.5f);
        float su[3] = { sinf(syaw), 0.0f, cosf(syaw) };  /* unit side */
        float sv[3] = { SOLV_SIDE * su[0], 0.0f, SOLV_SIDE * su[2] };
        float lpt[3] = { 0, 0, 0 }, rpt[3] = { 0, 0, 0 };
        float cand_a[3], cand_b[3];
        int   raw_l = 0, raw_r = 0;   /* lane hit, not dot-rejected */
        memcpy(cand_a, eye, sizeof cand_a);
        memcpy(cand_b, eye, sizeof cand_b);
        for (int side = 0; side < 2; side++) {
            float sgn = side == 0 ? 1.0f : -1.0f;
            float st[3], en[3], gate;
            if (!blocked) {
                st[0] = eye[0]; st[1] = eye[1]; st[2] = eye[2];
            } else {
                st[0] = eye[0] - sgn * AIMS_CORNER_BACK * su[0];
                st[1] = eye[1];
                st[2] = eye[2] - sgn * AIMS_CORNER_BACK * su[2];
            }
            en[0] = eye[0] + sgn * sv[0];
            en[1] = eye[1];
            en[2] = eye[2] + sgn * sv[2];
            if (!em_collision_segment_query(&g.coll, st, en, mask,
                                            EM_COLL_ID_NONE, &hit))
                continue;
            gate = 0.0f;
            if (blocked) {            /* glancing-only validation */
                if (bits & 8) {       /* 1st stage was ceiling-class */
                    gate = cam_dot3(hit.normal, pdir);
                    if (gate < SOLV_CROSS_DOT) continue;
                } else {
                    gate = cam_dot3(hit.normal, first_n);
                    if (gate < SOLV_OPPOSE_DOT) continue;
                }
            }
            if (side == 0) { raw_l = 1; memcpy(lpt, hit.point, 12); }
            else           { raw_r = 1; memcpy(rpt, hit.point, 12); }
            /* response window (non-glancing: gate in (-0.3, 0.9)) */
            if (!glancing &&
                (!(gate < SOLV_GATE_HI) || gate <= SOLV_GATE_LO))
                continue;
            {
                float rd[3] = { en[0] - st[0], en[1] - st[1],
                                en[2] - st[2] };
                float c[3]  = { hit.point[0] - sgn * sv[0],
                                hit.point[1],
                                hit.point[2] - sgn * sv[2] };
                float *cand = side == 0 ? cand_a : cand_b;
                cam_norm3(rd);
                if (hit.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {
                    if (-hit.normal[1] < AIMS_WALL_DOT) {
                        cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                        cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                        bits |= side == 0 ? 2 : 4;
                    } else {
                        /* flat underside: keep the candidate's own Y
                         * (right side sets bit 8 only — never applied
                         * below, engine quirk kept) */
                        cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                        cand[1] = c[1];
                        cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                        bits |= side == 0 ? 0xA : 0x8;
                    }
                } else if (hit.surf_class & EM_SURF_WALL) {
                    cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                    cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                    bits |= side == 0 ? 2 : 4;
                } else {
                    /* other class (right side: bit 1 only, quirk) */
                    cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                    cand[1] = c[1];
                    cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                    bits |= side == 0 ? 3 : 1;
                }
            }
        }
        /* selection (engine: midpoint whenever the OTHER lane also hit
         * something it did not dot-reject, even window-failed) */
        if ((bits & 6) == 6 || ((bits & 2) && raw_r) ||
            ((bits & 4) && raw_l)) {
            eye[0] = 0.5f * (lpt[0] + rpt[0]);   /* corridor centering */
            eye[2] = 0.5f * (lpt[2] + rpt[2]);
        } else if (bits & 2) {
            memcpy(eye, cand_a, 12);             /* full copy, incl. Y */
        } else if (bits & 4) {
            memcpy(eye, cand_b, 12);
        }
    }

    /* 4. FINAL Y POLICY (blocked by a NON-wall class). */
    if (blocked && !(attr & EM_SURF_WALL)) {
        if (attr & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {
            float lim = first_pt[1] - 1.0f;      /* stay under it */
            if (eye[1] > lim) eye[1] = lim;
        } else {
            float lim = first_pt[1] + 1.0f;      /* ride over it */
            if (eye[1] < lim) eye[1] = lim;
        }
    }

    /* 5. CONFIRM RE-PROBE — any response moved the eye: re-test the
     * player->eye line; still blocked -> park ON the new hit (x/z,
     * height kept, no pad). */
    if ((bits & 0x1F) &&
        em_collision_segment_query(&g.coll, g.pos, eye, mask,
                                   EM_COLL_ID_NONE, &hit)) {
        eye[0] = hit.point[0];
        eye[2] = hit.point[2];
    }

    /* 6. NEW BOUNDS for the NEXT frame (written, not applied here). */
    {
        float anchor[3] = { g.pos[0], g.pos[1] + SOLV_PLAYER_UP,
                            g.pos[2] };
        float d[3] = { eye[0] - anchor[0], eye[1] - anchor[1],
                       eye[2] - anchor[2] };
        float pb[3], probe[3], lo, hi;
        cam_norm3(d);
        pb[0] = eye[0] - AIMS_BOUND_PULL * d[0];
        pb[1] = eye[1] - AIMS_BOUND_PULL * d[1];
        pb[2] = eye[2] - AIMS_BOUND_PULL * d[2];
        probe[0] = pb[0];
        probe[1] = pb[1] - SOLV_BOUND_RANGE;
        probe[2] = pb[2];
        if (em_collision_segment_query(&g.coll, pb, probe, mask,
                                       EM_COLL_ID_NONE, &hit) &&
            (hit.surf_class & (EM_SURF_FLOOR | EM_SURF_SLOPE |
                               EM_SURF_WALL)))   /* 0x7000, verbatim */
            lo = hit.point[1] + AIMS_FLOOR_PAD;
        else
            lo = cam->y_lo - SOLV_BOUND_RANGE;
        probe[1] = pb[1] + SOLV_BOUND_RANGE;
        if (em_collision_segment_query(&g.coll, pb, probe, mask,
                                       EM_COLL_ID_NONE, &hit) &&
            (hit.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)))
            hi = hit.point[1] - SOLV_CEIL_PAD;
        else
            hi = eye[1] + SOLV_BOUND_RANGE;      /* area 0x12 variant
                                                    omitted, flagged */
        if (lo > hi) lo = hi - 3.0f;
        cam->y_lo = lo;
        cam->y_hi = hi;
    }
    return bits;
}

/* func_0018D910 — the style-5 DIRECTOR solver (decoded s64; the s61
 * table's "returns 0" undersold it): it never moves the eye — it is
 * BOUNDS MAINTENANCE ONLY, the func_0018CE60 settle shape anchored at
 * the eye pulled 1.0 toward player + 11 up: floor probe 200 down
 * (class 0x7000) -> lower = floor + 17 (+6 when cam+0x5C == 1);
 * ceiling probe 200 up (0x8800) -> upper = ceiling - 1, else eye_des.y
 * + 200 (area-0x12 player-up variant omitted, flagged); lower forced
 * to upper - 3. Keeps cam+0x50/+0x54 fresh while a director/fixed
 * camera owns the eye, so the first follow solve after release clamps
 * against live bounds. */
static void cam_solver_0018D910(EmCamera *cam)
{
    const unsigned mask = EM_COLL_SET_CELLS | EM_COLL_SET_GRID;
    float anchor[3] = { g.pos[0], g.pos[1] + SOLV_PLAYER_UP, g.pos[2] };
    float d[3] = { cam->eye_des[0] - anchor[0],
                   cam->eye_des[1] - anchor[1],
                   cam->eye_des[2] - anchor[2] };
    float pb[3], probe[3], lo, hi;
    EmCollHit hit;

    cam_norm3(d);
    pb[0] = cam->eye_des[0] - AIMS_BOUND_PULL * d[0];
    pb[1] = cam->eye_des[1] - AIMS_BOUND_PULL * d[1];
    pb[2] = cam->eye_des[2] - AIMS_BOUND_PULL * d[2];
    probe[0] = pb[0];
    probe[1] = pb[1] - SOLV_BOUND_RANGE;
    probe[2] = pb[2];
    if (em_collision_segment_query(&g.coll, pb, probe, mask,
                                   EM_COLL_ID_NONE, &hit) &&
        (hit.surf_class & (EM_SURF_FLOOR | EM_SURF_SLOPE |
                           EM_SURF_WALL)))        /* 0x7000, verbatim */
        lo = hit.point[1] + (cam->var_5c == 1.0f ? SOLV_FLOOR_PAD1
                                                 : SOLV_FLOOR_PAD);
    else
        lo = cam->y_lo - SOLV_BOUND_RANGE;
    probe[1] = pb[1] + SOLV_BOUND_RANGE;
    if (em_collision_segment_query(&g.coll, pb, probe, mask,
                                   EM_COLL_ID_NONE, &hit) &&
        (hit.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)))
        hi = hit.point[1] - SOLV_CEIL_PAD;
    else
        hi = cam->eye_des[1] + SOLV_BOUND_RANGE;
    if (lo > hi) lo = hi - 3.0f;
    cam->y_lo = lo;
    cam->y_hi = hi;
}

/* func_0018D7B0 (style 0) — the desired-eye solver dispatcher.
 * Mask 6 (static cells + grid; style 2 would use 7 = + movable
 * hulls), the func_0018DD20 core above, result byte -> struct +0x07
 * (cam->hit), then the actual eye (D_008105D0) smooth-chases the
 * solved desired eye per axis capped 4.0 u/frame and the actual
 * target hard-copies the desired (func_0018C0C0). */
static void camera_solve(EmCamera *cam)
{
    float eye_des[3] = { cam->eye_des[0], cam->eye_des[1], cam->eye_des[2] };

    cam->hit = 0;

    /* FIXED-CAMERA REGION: the room spec is authoritative — no wall
     * solve (the designers placed the eye), CHASE DISABLED: the actual
     * eye is a hard copy of the spec. An R1-release frame lands here
     * with the spec already re-pinned by the dispatch, so the snap-back
     * is INSTANT (one frame, no lerp) — the observed behavior. The
     * engine still runs solver STYLE 5 for director cams =
     * func_0018D910 (decoded s64): BOUNDS MAINTENANCE ONLY — keep
     * cam+0x50/+0x54 fresh so the first follow solve after release
     * clamps against live bounds, not the pre-region pair. */
    if (g.cam_region_on) {
        if (g.coll.poly_count)
            cam_solver_0018D910(cam);
        cam->eye[0] = eye_des[0];
        cam->eye[1] = eye_des[1];
        cam->eye[2] = eye_des[2];
        cam->tgt[0] = cam->tgt_des[0];
        cam->tgt[1] = cam->tgt_des[1];
        cam->tgt[2] = cam->tgt_des[2];
        return;
    }
    if (g.coll.poly_count) {
        if (!cam->aim_phase) {
            /* THE FOLLOW SOLVE — the decoded func_0018DD20, style 0
             * (mask 6), result bits -> struct +0x07. It mutates
             * cam->eye_des in place (engine: cam+0x10); the chase
             * below consumes the mutated copy. */
            cam->hit = (uint8_t)cam_solver_0018DD20(cam);
            eye_des[0] = cam->eye_des[0];
            eye_des[1] = cam->eye_des[1];
            eye_des[2] = cam->eye_des[2];
        } else {
            /* AIM camera: the engine solves mode 1 with STYLE 2 ->
             * func_0018F870 over mask 7 (movable hulls in) — DECODED
             * s64 and translated verbatim (cam_solver_0018F870 above;
             * the CAM_WALL_MARGIN stand-in is RETIRED). R1 KEEPS the
             * constant-height pull-in; the follow solver's waiver/
             * wedge/rise-over-floor responses structurally never run
             * while aiming. */
            cam->hit = (uint8_t)cam_solver_0018F870(cam);
            eye_des[0] = cam->eye_des[0];
            eye_des[1] = cam->eye_des[1];
            eye_des[2] = cam->eye_des[2];
        }
    }
    cam->eye[0] = cam_chase_h(cam->eye[0], eye_des[0], CAM_EYE_CAP);
    cam->eye[2] = cam_chase_h(cam->eye[2], eye_des[2], CAM_EYE_CAP);
    cam->eye[1] = cam_chase_v(cam->eye[1], eye_des[1], CAM_EYE_CAP);

    /* MODE-1 dispatcher tail (func_00197D20): with the eye above
     * player.y + 23 and horizontally inside 8 u, push it out to
     * EXACTLY 8 along its own heading (the min-distance clamp). */
    if (cam->aim_phase && cam->eye[1] > g.pos[1] + 23.0f) {
        float dx = cam->eye[0] - g.pos[0];
        float dz = cam->eye[2] - g.pos[2];
        float d  = sqrtf(dx * dx + dz * dz);
        if (d < CAM_AIM_MIN_DIST) {
            float h = atan2f(dx, dz);
            cam->eye[0] = g.pos[0] + sinf(h) * CAM_AIM_MIN_DIST;
            cam->eye[2] = g.pos[2] + cosf(h) * CAM_AIM_MIN_DIST;
        }
    }

    /* ACTUAL TARGET: hard copy (func_0018C0C0) — except the mode-1
     * aim chases it (0.4 u/frame entry / 0.6 steady, func_00197740/
     * func_00197870) and a live door-cinematic re-blend window
     * (cam+0xA0) chases at <= 1.0 u/frame (func_001916C0's tail). */
    if (cam->aim_phase) {
        float cap = cam->aim_phase == 1 ? 0.4f : 0.6f;
        cam->tgt[0] = cam_chase_h(cam->tgt[0], cam->tgt_des[0], cap);
        cam->tgt[2] = cam_chase_h(cam->tgt[2], cam->tgt_des[2], cap);
        cam->tgt[1] = cam_chase_v(cam->tgt[1], cam->tgt_des[1], cap);
    } else if (cam->tgt_soft > 0) {
        cam->tgt_soft--;
        cam->tgt[0] = cam_chase_h(cam->tgt[0], cam->tgt_des[0], 1.0f);
        cam->tgt[2] = cam_chase_h(cam->tgt[2], cam->tgt_des[2], 1.0f);
        cam->tgt[1] = cam_chase_v(cam->tgt[1], cam->tgt_des[1], 1.0f);
    } else {
        cam->tgt[0] = cam->tgt_des[0];
        cam->tgt[1] = cam->tgt_des[1];
        cam->tgt[2] = cam->tgt_des[2];
    }

    /* EM_CAMERA_TRACE=1 — solve outcome (the per-branch trace lives in
     * cam_solver_0018DD20). */
    static int trace = -1;
    if (trace < 0) trace = getenv("EM_CAMERA_TRACE") != NULL;
    if (trace && cam->hit)
        printf("camera: frame %d solver bits 0x%02x (eye y %.2f -> des "
               "%.2f, bounds [%.1f, %.1f])\n", g.frame_no, cam->hit,
               cam->eye[1], eye_des[1], cam->y_lo, cam->y_hi);
}

/* func_0018C0D0(cam, 1) — the per-frame COMMIT. Engine steps:
 *   1. fwd = normalize(target - eye), degenerate-guarded;
 *   2. view position = eye + 4.0*fwd (near push; mode 0xA uses -1.0);
 *   3. func_00102CD0 look-at with up = D_008105F0 = (0,-1,0) — see
 *      em_mat4_lookat_gs for the Y-down/handedness reconciliation;
 *   4. P from zoom s, K = P*V -> every draw's matrix slot 0 (M = K*W).
 * Step 4 is the ENGINE projection (em_mat4_perspective_gs from the
 * camera's zoom field — see the "Engine projection" block above): the
 * old port 50-deg-at-window-aspect perspective with invented 0.5/500-800
 * clip planes is retired. The matrix bakes the 4:3 frame; the gfx
 * letterbox keeps that the displayed aspect at any window size. */
static void camera_commit(EmCamera *cam)
{
    float dx  = cam->tgt[0] - cam->eye[0];
    float dy  = cam->tgt[1] - cam->eye[1];
    float dz  = cam->tgt[2] - cam->eye[2];
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len > 1e-3f) {  /* degenerate guard: keep the last forward */
        cam->fwd[0] = dx / len;
        cam->fwd[1] = dy / len;
        cam->fwd[2] = dz / len;
    }

    float pos[3] = { cam->eye[0] + CAM_NEAR_PUSH * cam->fwd[0],
                     cam->eye[1] + CAM_NEAR_PUSH * cam->fwd[1],
                     cam->eye[2] + CAM_NEAR_PUSH * cam->fwd[2] };
    em_mat4_lookat_gs(cam->view, pos, cam->fwd, cam->up);

    /* Commit bookkeeping (engine step 4): D_00810690 = the DESIRED
     * pair's horizontal eye<->target distance — next frame's solver
     * reads it for the head-clear waiver gate (one frame stale,
     * engine-true). */
    {
        float hx = cam->tgt_des[0] - cam->eye_des[0];
        float hz = cam->tgt_des[2] - cam->eye_des[2];
        cam->horiz_dist = sqrtf(hx * hx + hz * hz);
    }

    float proj[16];
    em_mat4_perspective_gs(proj,
                           cam->zoom > 0.0f ? cam->zoom
                                            : ENGINE_CAM_ZOOM_S);
    em_mat4_mul(g.viewproj, proj, cam->view);

    /* EM_PROJ_TEST=1 — ENGINE-PROJECTION TRUTH TEST (one-shot, quits).
     * Ground truth = the state01 PCSX2 savestate (decomp repo
     * scratch/state01): its EE RAM carries the engine's own composed
     * camera matrix K = P*V (render-ctx +0x23C0) AND the camera inputs
     * (eye D_008105D0, target D_008105E0, zoom 480), and its screenshot
     * is the rendered frame for exactly that state. The expected pixels
     * below were produced by projecting world points through THAT K and
     * mapping GS->frame (x: [1792,2304]->640, y: [1936,2160] field
     * ->480); the player-root expectation (320.0, 441.1) was visually
     * confirmed to land between the player's boots in the screenshot.
     * The check: run the savestate's eye/target/zoom through the PORT
     * chain — engine commit semantics (fwd, eye + 4*fwd near push,
     * em_mat4_lookat_gs) + em_mat4_perspective_gs + the 4:3 viewport
     * mapping — and require the same pixels to 0.05 px. This pins the
     * whole native remap (axis flips included, via the off-center
     * points) to the engine's arithmetic, not to a formula re-derivation. */
    {
        static int pt = -1;
        if (pt < 0) pt = getenv("EM_PROJ_TEST") != NULL;
        if (pt == 1) {
            pt = 2;
            static const float t_eye[3] =
                { 251.2506561f, 248.8504944f, 170.2859192f };
            static const float t_tgt[3] =
                { 218.5923157f, 246.4232635f, 201.7888184f };
            static const float t_root[3] =
                { 218.5923004f, 229.8504486f, 201.7888641f };
            /* world point -> expected 640x480 frame pixel (engine K) */
            static const float t_pt[5][5] = {
                {   0.0f, 0.0f, 0.0f, 319.9994f, 441.0799f },  /* root  */
                {   5.0f, 0.0f, 0.0f, 276.9809f, 462.2875f },  /* +5x   */
                {   0.0f, 0.0f, 5.0f, 282.2786f, 423.7764f },  /* +5z   */
                {   0.0f, 8.0f, 0.0f, 319.9994f, 345.0756f },  /* +8y   */
                { -18.5923004f, 5.1495514f, 28.2111359f,
                    272.6450f, 306.1691f },                    /* off   */
            };
            float fw[3] = { t_tgt[0] - t_eye[0], t_tgt[1] - t_eye[1],
                            t_tgt[2] - t_eye[2] };
            float fl = sqrtf(fw[0]*fw[0] + fw[1]*fw[1] + fw[2]*fw[2]);
            fw[0] /= fl; fw[1] /= fl; fw[2] /= fl;
            float tp[3] = { t_eye[0] + CAM_NEAR_PUSH * fw[0],
                            t_eye[1] + CAM_NEAR_PUSH * fw[1],
                            t_eye[2] + CAM_NEAR_PUSH * fw[2] };
            float tup[3] = { 0.0f, -1.0f, 0.0f };
            float tv[16], tpr[16], tk[16];
            em_mat4_lookat_gs(tv, tp, fw, tup);
            em_mat4_perspective_gs(tpr, ENGINE_CAM_ZOOM_S);
            em_mat4_mul(tk, tpr, tv);
            int fails = 0;
            for (int i = 0; i < 5; i++) {
                float p[4] = { t_root[0] + t_pt[i][0],
                               t_root[1] + t_pt[i][1],
                               t_root[2] + t_pt[i][2], 1.0f };
                float c[4];
                for (int r = 0; r < 4; r++)
                    c[r] = tk[0+r]*p[0] + tk[4+r]*p[1] +
                           tk[8+r]*p[2] + tk[12+r];
                float px = (1.0f + c[0]/c[3]) * 0.5f * 640.0f;
                float py = (1.0f - c[1]/c[3]) * 0.5f * 480.0f;
                float d  = c[2] / c[3];
                int ok = fabsf(px - t_pt[i][3]) <= 0.05f &&
                         fabsf(py - t_pt[i][4]) <= 0.05f &&
                         d > 0.0f && d < 1.0f;
                if (!ok) fails++;
                printf("proj test: pt%d port (%8.4f, %8.4f) d %.6f — "
                       "engine (%8.4f, %8.4f): %s\n", i, px, py, d,
                       t_pt[i][3], t_pt[i][4], ok ? "ok" : "FAILED");
            }
            printf("proj test: engine s=480 projection vs state01 "
                   "K=P*V (5 pts, 0.05 px): %s\n",
                   fails ? "FAIL" : "PASS");
            fflush(stdout);
            em_frame_request_quit();
        }
    }
}

/* DOOR-TRANSIT CINEMATIC CAMERA — DECODED + LIVE-VERIFIED (the "DOOR
 * CAMERA CUES" constants block above; two PCSX2 transits). Once the
 * walk-to arrives and the door script begins, the OPEN script's op
 * 0x0D sub 5 cue fires — a HARD CUT to 20 u behind the STAGING POINT
 * along the THROUGH-DOOR axis at +19, looking at the staging point
 * (+13) — and the camera then HOLDS that eye while the actual target
 * re-blends toward the walking player at <= 1.0 u/frame (the
 * cam+0xA0 = 120 window): the cinematic angle the door walk-through
 * plays under. When the player crosses the doorway plane (the room
 * move), the chase RE-SEATS behind the through-door pose and the
 * normal solve owns the camera again (rising over the doorframe —
 * engine-observed). The LOCKED-TRY cut (func_001BBBF0, em_door subs
 * 1/2: target at the door HANDLE — 8 u to the door's left, +10 — eye
 * 13 u back along the live camera heading at door.y + 12) runs off
 * em_door_locked_look() and HOLDS both eye and target pinned until
 * the finish script restores (the old EM_DOORCAM_LOCKED env preview
 * is retired — the real sequence drives it now). */
static void camera_door_cinematic(EmCamera *cam)
{
    float tt[3], tyaw;
    if (g.doorcam == 3)             /* post-warp (script ended, op 0x18
                                     * restored the camera): hold the
                                     * re-seated chase placement until
                                     * the fade-in unlocks */
        return;
    if (g.doorcam == 4)             /* LOCKED-LOOK hold: the engine
                                     * hard-copied once and never moves
                                     * the camera again — the finish
                                     * script's restore is handled at
                                     * the dispatcher (camera_update) */
        return;
    if (g.doorcam < 2) {
        float dp[3], dyaw, snapyaw;
        if (em_door_transit_active(tt, &tyaw)) {
            g.doorcam = 1;          /* approach walk: hold the chase
                                     * camera still (commit only) */
            /* Latch the staging point + through-door yaw — the
             * engine's kickoff snap pose (spad 3B40/3B50), which the
             * cut below builds from. The walk-to target IS the
             * staging point and its yaw IS the front/back snap. */
            memcpy(g.doorcut_pos, tt, sizeof g.doorcut_pos);
            g.doorcut_yaw = tyaw;
            return;
        }
        /* walk-to arrived — the door script starts THIS frame: cut. */
        if (em_door_locked_look(dp, &dyaw, &snapyaw)) {
            /* func_001BBBF0 — the LOCKED-TRY cut: target = door + 8 u
             * toward the HANDLE side (the door-yaw left) + 10 up, eye
             * = target - 13 along D_00810374 with eye.y = door.y + 12.
             * Hard copy, then HOLD pinned (doorcam 4) until the finish
             * script restores.
             *
             * s71 YAW-SOURCE CORRECTION (PCSX2 ground truth: the
             * engine parks this shot in the SAME spot regardless of
             * how the camera was oriented on approach): D_00810374 is
             * NOT the live chase heading. Full writer census (every
             * main-ELF reference): only script/cutscene ops write it —
             * op01 walk-to sub (func_001B6F80 = rec[+0x34], a
             * script-authored yaw), op04 (rec angles), op15/op19
             * compounds, func_001B6F00 (actor yaw + offset, scripted-
             * actor cues), func_00183160 (+= delta, overlay-called),
             * and the room-entry mode-0xA fixed-cam armer
             * (func_001B0460 -> func_00102C58). The chase camera NEVER
             * writes it, so at the locked try it holds the last
             * SCRIPTED camera yaw — deterministic per route. The old
             * cam->yaw read only looked right before s67 (the chase
             * yaw was approach-independent after the kickoff walk);
             * the s67 tether made cam->yaw an output and broke it.
             * The port has no script-yaw global, so the deliberate,
             * orientation-independent stand-in is the THROUGH-DOOR
             * snap yaw from the kickoff (em_door's transit_yaw — the
             * same spad-3B50 anchor the open cut uses): the camera
             * parks 13 u on the player's side of the handle, facing
             * the door, from any approach (FLAGGED stand-in for the
             * last-scripted-yaw global). */
            cam->tgt_des[0] = dp[0] - LOCKCAM_HANDLE_OFF * cosf(dyaw);
            cam->tgt_des[1] = dp[1] + LOCKCAM_TGT_UP;
            cam->tgt_des[2] = dp[2] + LOCKCAM_HANDLE_OFF * sinf(dyaw);
            cam->eye_des[0] = cam->tgt_des[0]
                            - LOCKCAM_EYE_BACK * sinf(snapyaw);
            cam->eye_des[1] = dp[1] + LOCKCAM_EYE_UP;
            cam->eye_des[2] = cam->tgt_des[2]
                            - LOCKCAM_EYE_BACK * cosf(snapyaw);
            memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
            memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
            cam->tgt_soft = 0;
            g.doorcam     = 4;
            return;
        }
        /* op 0x0D sub 5 (func_0018CBD0, dist -20; live-verified
         * geometry — the constants block above): 20 u behind the
         * SNAPPED pose along the THROUGH-DOOR axis at head height,
         * looking at the staging point slightly below (+13). The
         * camera heading itself snaps to the door axis (the
         * engine's cam Euler +0x30 <- spad 3B50). */
        cam->yaw = g.doorcut_yaw;
        cam->tgt_des[0] = g.doorcut_pos[0];
        cam->tgt_des[1] = g.pos[1] + DOORCAM_TGT_UP;
        cam->tgt_des[2] = g.doorcut_pos[2];
        cam->eye_des[0] = g.doorcut_pos[0]
                        - sinf(g.doorcut_yaw) * DOORCAM_EYE_BACK;
        cam->eye_des[1] = g.pos[1] + DOORCAM_EYE_UP;
        cam->eye_des[2] = g.doorcut_pos[2]
                        - cosf(g.doorcut_yaw) * DOORCAM_EYE_BACK;
        /* solver style 1 = HARD COPY desired -> actual: the cut. */
        memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
        memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
        cam->tgt_soft = DOORCAM_TGT_SOFT;   /* cam+0xA0 = 0x78 */
        g.doorcam     = 2;
        return;
    }
    /* ROOM-BOUNDARY RE-SEAT — DECODED s65 (func_001B0460(1) ->
     * func_001B0080(cam, 2.0), the room-entry camera re-init's
     * unflagged-record arm, called on EVERY room entry): desired
     * TARGET = the player's ground position + 17 (the idle target
     * height), desired EYE = target + rotY(through-door yaw) *
     * (0, 0, cam+0x0C) + 2 up — i.e. fabs(cam+0x0C) BEHIND the
     * through-door pose at player.y + 19 — both HARD-COPIED to the
     * actuals. Then the NORMAL dispatch + solve own the camera: the
     * eye lands inside/behind the door wall, the solver pulls in at
     * kept height and the floor-bound probe landing on the doorframe
     * lintel raises it — the live park at (104, 29, -250.4) over the
     * 21-u frame is bounds + walk handler, fully attributed (the s56
     * "+29 from func_00191000" guess stays retracted). The engine
     * reads the PER-RECORD distance (office records: -31.2); the
     * port carries only the default. Goto doors never cross the
     * plane before the fade; their re-seat is the warp re-place
     * (doorcam = 3 there, same shape). */
    {
        float dx = g.pos[0] - g.doorcut_pos[0];
        float dz = g.pos[2] - g.doorcut_pos[2];
        if (dx * sinf(g.doorcut_yaw) + dz * cosf(g.doorcut_yaw)
                > DOORCAM_PLANE) {
            g.doorcam     = 3;
            cam->yaw      = g.doorcut_yaw;
            cam->tgt_soft = 0;
            cam->swing    = 0;
            cam->tgt_des[0] = g.pos[0];
            cam->tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
            cam->tgt_des[2] = g.pos[2];
            cam->eye_des[0] = cam->tgt_des[0]
                            - sinf(g.doorcut_yaw) * fabsf(g.cam_dist_param);
            cam->eye_des[1] = cam->tgt_des[1] + 2.0f;   /* player.y+19 */
            cam->eye_des[2] = cam->tgt_des[2]
                            - cosf(g.doorcut_yaw) * fabsf(g.cam_dist_param);
            memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
            memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
            return;
        }
    }
    /* held cinematic: the eye HOLDS the cut placement; the desired
     * target tracks the player and the actual one re-blends through
     * the tgt_soft window (camera_solve's chase arm runs only outside
     * the lock, so chase here directly). */
    cam->tgt_des[0] = g.pos[0];
    cam->tgt_des[1] = g.pos[1] + DOORCAM_TGT_UP;
    cam->tgt_des[2] = g.pos[2];
    if (cam->tgt_soft > 0) {
        cam->tgt_soft--;
        cam->tgt[0] = cam_chase_h(cam->tgt[0], cam->tgt_des[0], 1.0f);
        cam->tgt[2] = cam_chase_h(cam->tgt[2], cam->tgt_des[2], 1.0f);
        cam->tgt[1] = cam_chase_v(cam->tgt[1], cam->tgt_des[1], 1.0f);
    } else {
        memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
    }
}

/* func_001CB590(0x008101E0, 0xD0, 0) + func_0018B9C0 — camera-context
 * begin + the camera state machine top. Runs AFTER the render-chain
 * build, like the engine; on the PS2 the already-kicked chain consumes
 * the PREVIOUS frame's matrices, while the native chain is flushed at
 * close-out with this frame's — one frame less camera latency, same
 * 60 Hz math. */
static void camera_update(void)
{
    EmCamera *cam = &g.cam;

    if (cam->state == 0) {
        /* State 0 — one-shot init. Engine: vector pool setup with
         * up = (0,-1,0), then mode 8 (reposition/settle vs the world,
         * two func_0019A910 ray queries). The settle needs the collision
         * world (TODO above), so natively: snap actual = desired and
         * start the generic follow directly in mode 0. */
        cam->up[0]     = 0.0f;
        cam->up[1]     = -1.0f;  /* the engine's Y-DOWN view-up */
        cam->up[2]     = 0.0f;
        cam->top_mode  = 0;
        cam->table_sel = 1;      /* smooth dispatch table */
        cam->mode      = 0;      /* engine inits mode 8 — TODO(camera-modes) */
        cam->aim_h     = CAM_AIM_OFFSET;
        /* func_0018DD20 solver state (engine init values) */
        cam->y_lo       = -200.0f;     /* +0x50 */
        cam->y_hi       = 1000.0f;     /* +0x54 */
        cam->hit_attr   = 0;
        cam->var_5c     = 2.0f;        /* +0x5C init constant */
        cam->wall_yaw   = 0.0f;        /* +0x90 zeroed (func_0018B9C0) */
        cam->swing      = 0;           /* +0x03 walk-tether latch */
        cam->swing_yaw  = 0.0f;
        cam->horiz_dist = CAM_DIST;    /* D_00810690 pre-first-commit */
        cam->zoom      = ENGINE_CAM_ZOOM_S;  /* ctx+0x2468 default 480
                                              * (func_001D25F0; scope =
                                              * 224/tan(vfov/2) when the
                                              * top_mode-3 camera lands) */
        cam->tgt_des[0] = g.pos[0];
        cam->tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
        cam->tgt_des[2] = g.pos[2];
        camera_desired_eye(cam);
        memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
        memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
        cam->state     = 1;
        cam->sub_state = 0;
        /* fall through — the engine's init frame still commits */
    }
    if (cam->sub_state == 0) {   /* first run frame: 0->1 ramp */
        cam->sub_state = 1;
        cam->timer     = 0;
    }

    /* STATUS-SCREEN PAUSE: dispatch + solve are skipped and only the
     * commit runs — the engine's frozen top modes 1/2 shape. The DOOR
     * TRANSIT no longer freezes: the decoded script camera cue runs
     * instead (camera_door_cinematic — the walk-to approach holds
     * still, then the op 0x0D sub 5 cinematic cut + held angle; the
     * post-warp re-seat still happens while the screen is black,
     * gameplay_frame). */
    if (cam->top_mode == 0 && !em_hud_is_open()) {
        /* EXAMINE camera cue (em_examine.h): the script's op00 fixed
         * cut — eye from the decoded record, target framing the
         * examined object. Hard copy + hold pinned while the sequence
         * presents (the engine's cue records own the camera under
         * scripted mode); release = a one-shot chase restore behind
         * the unmoved player, the op07-sub4 restore shape exactly
         * like the locked-door finish below. Cue-less examines (the
         * snow refusal's op0D sub5 chase cue) never enter here — the
         * normal chase keeps the camera. */
        {
            float exe[3], ext[3];
            if (em_examine_camera(exe, ext)) {
                memcpy(cam->eye_des, exe, sizeof exe);
                memcpy(cam->tgt_des, ext, sizeof ext);
                memcpy(cam->eye, exe, sizeof exe);
                memcpy(cam->tgt, ext, sizeof ext);
                cam->tgt_soft = 0;
                g.examcam = 1;
                camera_commit(cam);   /* func_0018C0D0(cam, 1) */
                cam->timer++;
                return;
            }
            if (g.examcam) {
                g.examcam = 0;
                cam->tgt_soft = 0;
                cam->tgt_des[0] = g.pos[0];
                cam->tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
                cam->tgt_des[2] = g.pos[2];
                camera_desired_eye(cam);
                memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
                memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
            }
        }
        if (em_door_movement_locked() && g.doorcam != 3) {
            /* pre-warp transit: approach freeze, then the script's
             * cinematic cut + held angle */
            camera_door_cinematic(cam);
        } else {
            /* doorcam 3 = post-warp WALK-OUT: the script has ended
             * (op 0x18 restored the camera at the re-place) — the
             * normal chase resumes behind the re-seated player while
             * the movement lock finishes the walk-out. */
            if (g.doorcam == 4 && !em_door_movement_locked()) {
                /* LOCKED finish (op 0x07 sub 4 -> func_001CA770):
                 * RESTORE the saved camera — the chase placement
                 * behind the unmoved player (cam->yaw was never
                 * touched by the locked cut, so desired == the
                 * pre-script chase pose; an instant copy, not a
                 * blend — engine restore semantics). */
                cam->tgt_soft = 0;
                cam->tgt_des[0] = g.pos[0];
                cam->tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
                cam->tgt_des[2] = g.pos[2];
                camera_desired_eye(cam);
                memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
                memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
            }
            if (!em_door_movement_locked()) g.doorcam = 0;
            camera_prestep_00191390(cam);  /* per-state height params */
            camera_mode_dispatch(cam);   /* func_0018BC20 */
            camera_solve(cam);           /* func_0018D7B0, style 0 */
        }
    }
    /* top modes 1/2 (frozen) reach the commit only; top mode 3 is the
     * scope camera func_0022EEF0 — TODO(camera-modes). */
    camera_commit(cam);              /* func_0018C0D0(cam, 1) */
    cam->timer++;
}

/* --- STATUS-MENU UI SCENE (the constants block above) ----------------
 *
 * The engine's identity-UI-camera 3D pass: while the status screen is
 * up, the 3D frame is a black field with ONLY the player model
 * turntabling on it (func_0020E6F0 on the menu's private static-actor
 * stage — the world is not drawn at all; s44 verified there is no
 * other draw under the background tiles). The port mirrors that by
 * flushing this scene INSTEAD of the recorded world chain whenever the
 * screen is visible and the real animated background will draw
 * (em_hud_backdrop_ready — without the ui.emui asset the old
 * dim-over-scene fallback keeps the world flush, byte-identical to the
 * pre-scene builds). em_hud_scene_3d() then tells the background
 * drawer to skip its opaque base fill so the player shows between the
 * black frame and the translucent tile layers — the engine's order
 * (3D scene, background, panels). */

/* Lazy fullscreen black backplate: a quad at view depth 400 spanning
 * +-2000 units, drawn through em_gfx_draw_skinned_tinted with black —
 * every shading path multiplies to (0,0,0,1), giving the engine's
 * opaque black UI frame in the 3D pass (depth write on; the player at
 * z 40 passes the less-equal test). The renderer culls nothing
 * (MTLCullModeNone), so one winding suffices. */
static EmGfxMesh *ui_backplate_ensure(EmGfx *gfx)
{
    if (g.ui_backplate) return g.ui_backplate;
    /* pos3, nrm3, uv2, bone(u32), tex(u32 = none) per vertex */
    static const float kQuad[4][3] = {
        { -2000.0f, -2000.0f, 400.0f }, {  2000.0f, -2000.0f, 400.0f },
        {  2000.0f,  2000.0f, 400.0f }, { -2000.0f,  2000.0f, 400.0f }
    };
    float v[4 * 10];
    memset(v, 0, sizeof v);
    for (int i = 0; i < 4; i++) {
        float *p = v + i * 10;
        p[0] = kQuad[i][0]; p[1] = kQuad[i][1]; p[2] = kQuad[i][2];
        p[5] = -1.0f;                      /* normal: toward the camera */
        uint32_t bone = 0, tex = EM_MODEL_NO_TEX;
        memcpy(p + 8, &bone, 4);
        memcpy(p + 9, &tex, 4);
    }
    static const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    g.ui_backplate = em_gfx_mesh_create(gfx, v, 4, idx, 6, NULL, 0,
                                        NULL, 0);
    return g.ui_backplate;
}

/* Render one frame of the UI scene (call in place of the world flush).
 * State is private (ui_* fields) and re-initializes on every visible
 * edge — the engine re-allocates the menu actor at every open (state 0
 * init: yaw = pi, ramp = 1.0, clip by displayed health), so EM_HUD_FORCE
 * captures sample a FIXED spin phase: yaw = pi + 0.01 * (frames since
 * the screen appeared), deterministic in headless runs. */
/* LIGHTING — per-actor rig composer (defined with the close-out flush
 * below; the menu turntable consumes it too). */
static void char_rig_build(EmGfxCharRig *out, const float anchor[3],
                           int cam_fill);

static void ui_scene_render(EmGfx *gfx)
{
    if (!g.ui_prev) {                    /* open edge = engine state 0 */
        g.ui_yaw      = EM_PI;
        g.ui_t        = 0.0;
        g.ui_ramp     = 1.0f;
        g.ui_ramp_dir = 0;
        g.ui_low      = g.status.health <= UI_LOW_HEALTH;
    }

    /* Clip select (engine: init picks by displayed health; recovery
     * past 35 swaps back to the stance; never TO the low clip
     * mid-open). The port reads the status target — the engine's
     * display copy only lags it during the open count-up. */
    if (g.ui_low && g.status.health > UI_LOW_HEALTH) {
        g.ui_low = 0;
        g.ui_t   = 0.0;
    }
    int clip = g.ui_low ? g.clip_menu_low : g.clip_menu;
    if (clip < 0) clip = g.clip_menu >= 0 ? g.clip_menu : g.clip_idle;

    /* Menu pose (never touches the gameplay palette; under EM_HUD_FORCE
     * the world keeps simulating its own pose untouched underneath).
     * The player stands at the WORLD ORIGIN facing the camera, spinning. */
    em_model_palette_at(&g.model, (uint32_t)clip, g.ui_t, g.ui_palette);
    {
        const float pos[3] = { 0.0f, 0.0f, 0.0f };
        /* yaw 0 of the spin = facing the camera (the engine's pi init
         * under its identity camera + publisher flip). */
        float yaw0 = atan2f(UI_CAM_DIR_X, UI_CAM_DIR_Z);
        palette_apply_placement(g.ui_palette, g.model.bone_count,
                                pos, yaw0 + (g.ui_yaw - EM_PI));
    }

    /* The UI camera. The ENGINE uses the raw identity camera; the port
     * keeps the identical RELATIVE transform (the decoded view-space
     * (7.4, 2.4 down, 40)) but orbits the rig to the gfx layer's fixed
     * directional stand-in light azimuth (shader L = (0.4, 0.8, 0.45);
     * src/gfx is the renderer's lighting model, not the engine's) so
     * the model's camera-facing side is the LIT side — under the raw
     * identity camera the stand-in light leaves the menu player at the
     * 0.30 ambient floor, near-black on the black UI frame. Screen
     * framing is unchanged: eye = dir*Z - right*X + (0, -Y, 0), so the
     * player still projects at the decoded view-space offsets. */
    float view[16], proj[16], vp[16], vp_plate[16];
    float rig_eye[3], rig_fwd[3];        /* kept for the headlight below */
    {
        const float dir[3] = { UI_CAM_DIR_X, 0.0f, UI_CAM_DIR_Z };
        const float fwd[3] = { -dir[0], 0.0f, -dir[2] };
        const float up[3]  = { 0.0f, -1.0f, 0.0f };
        /* s = fwd x up (the lookat's view-right basis; up = (0,-1,0)
         * makes s = (fwd.z, 0, -fwd.x)) */
        const float s_[3]  = { fwd[2], 0.0f, -fwd[0] };
        const float eye[3] = {
            dir[0] * UI_SCENE_Z - s_[0] * UI_SCENE_X,
            -UI_SCENE_Y,
            dir[2] * UI_SCENE_Z - s_[2] * UI_SCENE_X
        };
        em_mat4_lookat_gs(view, eye, fwd, up);
        memcpy(rig_eye, eye, sizeof rig_eye);
        memcpy(rig_fwd, fwd, sizeof rig_fwd);
    }
    /* UI projection = THE ENGINE'S WORLD P AT s = 480 (s66 live read
     * closes the s49/s59 open item: ctx+0x2468 stays 480 with the hub
     * open; V is identity-with-y-flip; no separate menu projection
     * exists). em_mat4_perspective_gs carries the 10/7 pixel-aspect
     * anisotropy and the decoded near/far — the menu model is now
     * projected EXACTLY like the world. The screen framing the old
     * 0.74 pin validated is preserved by the re-derived UI_SCENE_*
     * placement (the constants block doc). Anchors stay registered to
     * the letterboxed 4:3 game frame at any window size (the gfx
     * backend letterboxes every draw — em_gfx_begin_frame — the same
     * region the overlay's 512x448 canvas maps). */
    em_mat4_perspective_gs(proj, UI_SCENE_ZOOM);
    em_mat4_mul(vp, proj, view);
    /* The black backplate keeps its own fixed straight-ahead camera (its
     * quad lives at world z 400): identical full-screen result, no need
     * to re-orient the quad with the rig. */
    {
        const float eye[3] = { 0.0f, 0.0f, 0.0f };
        const float fwd[3] = { 0.0f, 0.0f, 1.0f };
        const float up[3]  = { 0.0f, -1.0f, 0.0f };
        em_mat4_lookat_gs(view, eye, fwd, up);
        em_mat4_mul(vp_plate, proj, view);
    }

    /* Black backplate first (the engine's empty UI frame), then the
     * player over it. NOTE: this makes the UI player the renderer's
     * "last skinned palette" — em_weapon's muzzle anchor reads a menu
     * pose next frame, which only matters in the untestable
     * FORCE+aiming combination. */
    {
        static const float kBlack[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        static const float kIdent[16] = { 1, 0, 0, 0, 0, 1, 0, 0,
                                          0, 0, 1, 0, 0, 0, 0, 1 };
        EmGfxMesh *bp = ui_backplate_ensure(gfx);
        if (bp)
            em_gfx_draw_skinned_tinted(gfx, bp, vp_plate, kIdent, 1, kBlack);
    }

    /* MENU-SCENE LIGHT — ENGINE-TRUE since the room-rig decode
     * (2026-06-11 s57; the camera-anchored spot fill of the first
     * fix is retired when the scene carries a rig).
     *
     * Engine truth (boot-ELF re-read): the menu player's draw class
     * 0xB (func_001CA5F0 -> func_001CB480) sets lighting-override
     * mode 2 — and func_001D89D0 special-cases only modes 1/3/4/5/6,
     * so mode 2 runs the NORMAL character light path: the CURRENT
     * ROOM's rig from D_00251C50 with the slot-0 camera light ZEROED
     * (the static menu actor never gets flag +0x2 bit 0x20 —
     * func_001AFF10 zeroes it, func_0020CDC0 never sets it). The
     * port now feeds exactly that: char_rig_build with cam_fill = 0
     * and no lamp fold (anchor NULL — the engine's menu actor sits
     * far from any room lamp). Without a scene rig (rig-less
     * manifest) the previous camera-fill spot stand-in still runs so
     * the turntable never regresses to black-on-black. */
    if (g.rig_on) {
        EmGfxCharRig rig;
        char_rig_build(&rig, NULL, 0);
        em_gfx_char_rig(gfx, &rig);
    } else {
        static const float kFill[3] = { 0.62f, 0.62f, 0.62f };
        em_gfx_spot_light(gfx, rig_eye, rig_fwd, kFill,
                          4000.0f, -2.0f, -3.0f);
    }
    {
        /* Infection tint pulse (engine +0x80 color delta, GS 128 base,
         * approximated multiplicatively; infection 0 = opaque white =
         * bit-identical to the untinted draw per the em_gfx contract). */
        float inf = g.status.infection;
        float dr = -0.8f * inf * g.ui_ramp;
        float dg = -1.0f * inf * g.ui_ramp;
        float db = -0.3f * inf * g.ui_ramp;
        if (dg < -127.0f) dg = -127.0f;   /* engine clamp (0xC2FE0000) */
        float tint[4] = { (128.0f + dr) / 128.0f,
                          (128.0f + dg) / 128.0f,
                          (128.0f + db) / 128.0f, 1.0f };
        for (int i = 0; i < 3; i++) {
            if (tint[i] < 0.0f) tint[i] = 0.0f;
            if (tint[i] > 1.0f) tint[i] = 1.0f;
        }
        em_gfx_draw_skinned_tinted(gfx, g.mesh, vp, g.ui_palette,
                                   g.model.bone_count, tint);
    }
    {
        /* Lights off for anything after the menu player (rig cleared,
         * spot rgb 0 adds exactly 0.0 — later draws stay bit-exact). */
        static const float kOff[3] = { 0.0f, 0.0f, 0.0f };
        em_gfx_char_rig(gfx, NULL);
        em_gfx_spot_light(gfx, rig_eye, rig_fwd, kOff,
                          4000.0f, -2.0f, -3.0f);
    }

    /* Advance (engine state 1): spin, pulse ramp, clip time. */
    g.ui_yaw += UI_SPIN_RATE;
    if (g.ui_yaw > EM_PI) g.ui_yaw -= 2.0f * EM_PI;
    if (g.ui_ramp_dir == 0) {
        g.ui_ramp += UI_RAMP_RATE;
        if (g.ui_ramp >= UI_RAMP_MAX) g.ui_ramp_dir = 1;
    } else {
        g.ui_ramp -= UI_RAMP_RATE;
        if (g.ui_ramp <= 1.0f) g.ui_ramp_dir = 0;
    }
    g.ui_t += 1.0;                       /* anim_advance_time(1.0) */
}

/* LIGHTING — compose one actor draw's CHARACTER LIGHT RIG: the native
 * func_001D89D0 normal path (decomp FINDINGS "PER-ROOM LIGHT RIGS
 * DECODED", 2026-06-11). Slots 1/2 = the scene rig's static world
 * directionals (D_00251C50 record, manifest `lightdir`). Slot 0 = the
 * CAMERA FILL when `cam_fill` is set — the actor flag +0x2 bit 0x20
 * (func_001D8BF0): the PLAYER gets it once at init (func_001AF5C0)
 * and the decoded NPC spawners set it on humans; NO decoded enemy/
 * prop/door spawner sets it, so those draws run fill-less (flag clear
 * -> slot 0 dir AND color zeroed, func_001D8340). The fill direction
 * is the rig's CAMERA-SPACE vector rotated into world through the
 * TRANSPOSED view rotation (func_001D8340 i==0: copy the lookat
 * D_00810610, func_00102798 transpose, apply w=0) — the port rebuilds
 * the ENGINE view basis from cam fwd/up (em_mat4_lookat_gs negates
 * its rows for Metal NDC, so the basis is re-derived, not read back).
 *
 * Then the DYNAMIC POINT-LIGHT FOLD (func_001D8340 tail; gate
 * func_001D8270 excludes a fixed type list + large models — the port
 * applies the fold to every actor draw; `anchor` = the draw's bone-0
 * world translation, the engine's node light-reference column +0xC0):
 *   k    = 0.1 * I / max(|toLamp|^2, 1)    (UN-normalized offset —
 *                                           func_00102738 is a dot)
 *   dir0 = normalize(dir0*w0 + sum toLamp * 10k)
 *   col0 = col0 + sum lampcol * 2k         (x128 registration scale)
 * Omitted, documented: the engine's per-lamp +-1.8 deg random-walk
 * flicker rotation (func_001D7C30 type-1 path, slot +0x40) and the
 * story-flag lamp gates (func_001F68B0) — lamps register
 * unconditionally. The actor RGB multiplier the engine folds into the
 * color rows (func_001D8690, actor +0x80) rides the port's per-draw
 * tint instead (same modulate, applied post-clamp).
 *
 * ROOM SELECTION: the engine keys the rig on (D_00810700<<8)|
 * D_00810701 — the AREA/SUB-STATE bytes, not spatial bounds. A
 * sub-state flip is a scene switch in the port (each exported scene
 * is one (area, sub) pair), so the active scene's rig IS the engine's
 * room selection; no player-position mapping exists or is invented. */
static void char_rig_build(EmGfxCharRig *out, const float anchor[3],
                           int cam_fill)
{
    memset(out, 0, sizeof *out);
    for (int s = 0; s < 2; s++) {
        for (int c = 0; c < 3; c++) {
            out->dir[s + 1][c] = g.rig_dir[s][c];
            out->col[s + 1][c] = g.rig_col[s][c];
        }
    }
    for (int c = 0; c < 3; c++)
        out->amb[c] = g.rig_amb[c];

    float d0[3] = { 0.0f, 0.0f, 0.0f };
    float c0[3] = { 0.0f, 0.0f, 0.0f };
    if (cam_fill) {
        /* engine view basis (em_math.h em_mat4_lookat_gs, un-negated):
         * sv = fwd x up_gs (view +x), uv = sv x fwd (view +y, world-
         * down), fwd (view +z); world fill = basis * camera-space dir. */
        const float *fw = g.cam.fwd, *up = g.cam.up;
        float sv[3] = { fw[1] * up[2] - fw[2] * up[1],
                        fw[2] * up[0] - fw[0] * up[2],
                        fw[0] * up[1] - fw[1] * up[0] };
        float sl = sqrtf(sv[0] * sv[0] + sv[1] * sv[1] + sv[2] * sv[2]);
        if (sl > 1e-6f) { sv[0] /= sl; sv[1] /= sl; sv[2] /= sl; }
        float uv[3] = { sv[1] * fw[2] - sv[2] * fw[1],
                        sv[2] * fw[0] - sv[0] * fw[2],
                        sv[0] * fw[1] - sv[1] * fw[0] };
        for (int c = 0; c < 3; c++) {
            d0[c] = sv[c] * g.rig_cam_dir[0] + uv[c] * g.rig_cam_dir[1]
                  + fw[c] * g.rig_cam_dir[2];
            c0[c] = g.rig_cam_col[c];
        }
    }
    if (anchor && g.n_lamp) {
        float sd[3] = { d0[0] * g.rig_cam_w, d0[1] * g.rig_cam_w,
                        d0[2] * g.rig_cam_w };
        float sc[3] = { c0[0], c0[1], c0[2] };
        for (int i = 0; i < g.n_lamp; i++) {
            float toL[3] = { g.lamp[i].pos[0] - anchor[0],
                             g.lamp[i].pos[1] - anchor[1],
                             g.lamp[i].pos[2] - anchor[2] };
            float d2 = toL[0] * toL[0] + toL[1] * toL[1]
                     + toL[2] * toL[2];
            if (d2 < 1.0f) d2 = 1.0f;
            float k = 0.1f * g.lamp[i].inten / d2;
            for (int c = 0; c < 3; c++) {
                sd[c] += toL[c] * (10.0f * k);
                sc[c] += g.lamp[i].col[c] * (2.0f * k);
            }
        }
        float sl = sqrtf(sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2]);
        if (sl > 1e-6f) {
            d0[0] = sd[0] / sl; d0[1] = sd[1] / sl; d0[2] = sd[2] / sl;
        }
        c0[0] = sc[0]; c0[1] = sc[1]; c0[2] = sc[2];
    }
    for (int c = 0; c < 3; c++) {
        out->dir[0][c] = d0[c];
        out->col[0][c] = c0[c];
    }
}

/* func_001CB5A0 / func_001AAD00 / func_001D1EA0(1) — close-out: flush the
 * recorded chain with the camera block applied (the native "kick"), then
 * advance clip time. EM_CAPTURE instrumentation lives here so its frame
 * counting matches the rendered gameplay frames. */
static void frame_close_out(void)
{
    EmGfx *gfx = em_frame_gfx();

    /* MENU-RENDER GATE: the UI-camera 3D scene replaces the world
     * flush while the status screen shows (visible = the real toggle
     * OR the EM_HUD_FORCE capture hook) and the real background will
     * draw over it. Without the player asset or the ui.emui backdrop
     * the old path runs unchanged (asset-absent frames byte-identical). */
    int ui_scene = g.mesh && em_hud_visible() && em_hud_backdrop_ready(gfx);

    if (ui_scene) {
        ui_scene_render(gfx);
    } else if (g.chain_test_triangle) {
        em_gfx_draw_test_triangle(gfx);
    } else {
        for (int i = 0; i < g.chain_len; i++) {
            const ChainDraw *cd = &g.chain[i];
            /* LIGHTING — actor draws (everything after the scene
             * meshes: doors, enemies, player — the chain-build order)
             * take a freshly composed character rig, the engine's
             * per-actor light-matrix rebuild (func_001D89D0 per draw
             * publish). Anchor = the palette's bone-0 world
             * translation (column-major [12..14] — the engine's node
             * light-reference column +0xC0); the camera fill is the
             * PLAYER draw's alone (decoded gating, char_rig_build).
             * Scene meshes draw rig-less: the LEVEL path ignores the
             * rig anyway (baked vertex color — engine truth). */
            if (g.rig_on && i >= g.n_scene) {
                EmGfxCharRig rig;
                const float anchor[3] = { cd->palette[12],
                                          cd->palette[13],
                                          cd->palette[14] };
                char_rig_build(&rig, anchor,
                               cd->palette == g.player_palette);
                em_gfx_char_rig(gfx, &rig);
            } else {
                em_gfx_char_rig(gfx, NULL);
            }
            if (cd->tint)         /* per-draw RGBA modulate (ChainDraw) */
                em_gfx_draw_skinned_tinted(gfx, cd->mesh, g.viewproj,
                                           cd->palette, cd->bone_count,
                                           cd->tint);
            else
                em_gfx_draw_skinned(gfx, cd->mesh, g.viewproj,
                                    cd->palette, cd->bone_count);
        }
        em_gfx_char_rig(gfx, NULL);   /* LIGHTING — rig is per draw */
    }
    g.ui_prev = ui_scene;     /* edge tracking for the scene re-init */
    em_hud_scene_3d(ui_scene);  /* background skips its base fill */

    /* Weapon feedback overlays (crosshair / muzzle-flash placeholders —
     * em_weapon.h "VISUAL FEEDBACK"); queues nothing while holstered, so
     * the default frame stays byte-identical. */
    em_weapon_render(gfx);

    /* STATUS SCREEN over the flushed 3D frame (the engine's GS-sprite
     * status overlay; em_hud queues overlay rects, em_gfx_end_frame
     * draws them last). HIDDEN by default — the original shows no
     * persistent HUD — and toggled by a TRIANGLE/START press (edge);
     * shown, the 3D frame underneath is the UI scene above (or the
     * flagged dim fallback without assets) and the world simulation is
     * paused by the gameplay_frame gate. EM_HUD_FORCE=1 forces it
     * visible for overlay tests. The ammo readout is LIVE: mag/reserve
     * mirror the weapon state (D_00810C62 / D_00810CB4) every frame,
     * exactly like the engine UI re-reading the globals. */
    /* MENU INHIBIT (engine D_008106B3, written every frame by the
     * player spine: nonzero while hit-reacting/dying — and at the
     * game-over screen, where START means restart): the open press is
     * dropped inside em_hud_update. */
    em_hud_menu_inhibit(player_damage_locked() ||
                        em_examine_input_locked());  /* examine = the
                                  * op07 scripted-mode window — the
                                  * engine's menu poll never runs while
                                  * spad 3B8D owns the frame */
    em_hud_update(em_frame_input());
    g.status.mag     = em_weapon_mag();
    g.status.reserve = em_weapon_reserve();
    g.status.items   = em_pickup_items();   /* the D_00810C64 mirror —
                                             * the ITEM page's real
                                             * per-type counts */
    em_hud_render(gfx, &g.status);
    em_hud_found_render(gfx);   /* transient "Found: <name>" line over
                                 * gameplay (hidden while the menu is
                                 * open) — the pickup decode's flagged
                                 * status-auto-open stand-in */
    em_examine_render(gfx);     /* EXAMINE area-bank chain text — the
                                 * mode-2 presentation for lines the
                                 * global radio machine can't address
                                 * (GLOBAL lines drew inside em_hud
                                 * just above) */

    /* GAME-OVER / CONTINUE screens — drawn BEFORE the fade rect so
     * the fade machine owns them exactly like the engine (the screen
     * modules render under the GS fade; PD block doc). Their opaque
     * black base hides the frozen dead world underneath. Queues
     * nothing while the GO machine is off, so every other frame stays
     * byte-identical. Presentation = the FLAGGED module stand-ins
     * (em_hud.h): module 0x27 -> em_hud_game_over, module 1 ->
     * em_hud_continue (cursor highlight). */
    if (g.go_state == GO_SCREEN || g.go_state == GO_SCREEN_OUT)
        em_hud_game_over(gfx);
    else if (g.go_state >= GO_PROMPT)
        em_hud_continue(gfx, g.go_cursor);

    /* SCREEN FADE — the step-D machine, drawn last so it covers the
     * scene AND the status screen (the engine's fade owns the whole GS
     * frame). The engine blend is SUBTRACTIVE (out = max(0, pixel -
     * level), GS ALPHA_2 0xA1/FIX 0x80 — decoded 2026-06-11, see
     * em_frame.h): a GREY full-screen sprite with R=G=B=level is
     * subtracted from every frame pixel, so shadows crush to black
     * first and highlights survive longest. The overlay pass carries
     * that exact op (em_gfx_overlay_rect_sub: reverse-subtract,
     * ONE/ONE on RGB, flushed over the HUD) — the former black-quad
     * alpha approximation and its residual gap are gone. Level 0
     * queues nothing: the default frame stays byte-identical. */
    {
        float l = em_frame_fade_level();
        if (l > 0.0f) {
            const float grey[3] = { l, l, l };
            em_gfx_overlay_rect_sub(gfx, 0.0f, 0.0f, EM_GFX_OVERLAY_W,
                                    EM_GFX_OVERLAY_H, grey);
        }
    }

    if (g.capture_path && g.frame_no == g.capture_frame)
        em_gfx_request_capture(gfx, g.capture_path);

    g.frame_no++;
    /* A scripted self-test owns the quit when combined with a capture,
     * so a mid-script capture doesn't cut the script short. */
    if (g.capture_path && !g.move_test && !g.weapon_test && !g.door_test &&
        !g.transit_test && !g.slider_test &&
        g.frame_no > g.capture_frame + 1)
        em_frame_request_quit();
}

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
 * 4.5-unit standoff. The strafe leg then CURVES: under the s65
 * WALK-STATE CAMERA the heading (cam+0x44) is an OUTPUT of the eye
 * tether — strafing rotates the camera bearing as the dragged eye
 * trails the path, and camera-relative input curves with it (the
 * engine's emergent chase-camera spiral; the pre-s65 expectations
 * assumed the old fixed-bearing port camera and a straight slide).
 * Deterministic endpoints (no RNG in the camera or mover):
 *   collision world:  (84.334, 0.000, -177.513), yaw -1.9977
 *   bbox fallback:    (83.891, 0.000, -141.560), yaw -1.9586
 *                     (no probes, no wall — free motion, larger arc)
 * Those built-in expectations (and the 60/30-frame legs) are the OFFICE
 * scene's; for other scenes (manifest spawns) EM_MOVE_LEGS=fwd,strafe
 * resizes the two legs to reach that scene's wall and EM_MOVE_EXPECT=
 * x,y,z + EM_MOVE_YAW=<rad> override the expected final placement. */
static void move_test_inject(int key, int down)
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
        float ex   = g.coll.poly_count ?   84.334f :   83.891f;
        float ey   = 0.0f;
        float ez   = g.coll.poly_count ? -177.513f : -141.560f;
        float eyaw = g.coll.poly_count ?  -1.9977f :  -1.9586f;
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
        int ok = fabsf(g.pos[0] - ex)           <= tol &&
                 fabsf(g.pos[1] - ey)           <= tol &&
                 fabsf(g.pos[2] - ez)           <= tol &&
                 fabsf(g.yaw - eyaw)            <= 0.01f;
        printf("move test: pos (%.3f, %.3f, %.3f) yaw %.4f rad — "
               "expected (%.3f, %.3f, %.3f)%s: %s\n",
               g.pos[0], g.pos[1], g.pos[2], g.yaw, ex, ey, ez,
               g.coll.poly_count ? " [wall stop]" : " [bbox clamp]",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* EM_DOOR_TEST=1 — deterministic door-transit self-test. Spawns the
 * player on the z = -225 corridor line facing the WEST double door
 * (hinge/placement at (57, 0, -220.5), record [5] AREA02 state 1;
 * DOORWAY CENTER = hinge + 5 along the panel = (57, 0, -225.5) — the
 * decoded func_00183EF0 class-5 reference point, 2026-06-11) and
 * exercises the FULL s22 transit sequence end to end through the real
 * input API:
 *
 *   frames  1..24   run -X (full push = gait 3; the tier ramp tops
 *                   out at 0.8 u/frame) to the boundary-wall standoff
 *                   x ~= 64.5 (inside the 10 u use-scan radius measured
 *                   from the CENTER; no button — the door must stay
 *                   CLOSED. Engine-true twice over: the class-5 scan
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
        move_test_inject('w', 1);
    } else if (n == 24) {
        move_test_inject('w', 0);
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
        move_test_inject('w', 1);   /* push into the sealed doorway */
    } else if (n == 58) {
        move_test_inject('w', 0);   /* ~6 frames pinned on the boundary */
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
 * EM_SCENE=assets/scene_drawbridge). Exercises the decoded m17/m09
 * variant brain func_001BB860 (em_door.h "SLIDERS") on the drawbridge
 * room's intra-room slider door 4 — door_m09 at (128.6, 0, -610) yaw
 * pi/2 (AREA01 sub-0 placement; room-move entries 9/8 flank it at
 * x 143 / 115.5):
 *
 *   frame    0      spawn (112, 0, -610) facing +X, 16.6 u from the
 *                   door; hold 'w' — the player RUNS at the door.
 *   ~frame  11..13  the player crosses the 10-u scan radius still
 *                   pushing — and must stay UNARMED: the engine has NO
 *                   walk-into door trigger (s58 decode; the s56
 *                   no-button reading is OVERTURNED — the use scan
 *                   runs only on the USE press edge). Asserted at 13.
 *   frame   14      CROSS press (the D_00810E74-edge use scan, config
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
        move_test_inject('w', 1);
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
    } else if (n == 13) {
        /* inside the 10-u window, pushing, NO button: must be UNARMED
         * (the engine has no walk-into trigger — s58) */
        st_ok_noarm = st_door >= 0 &&
                      em_door_state(st_door) == EM_DOOR_CLOSED &&
                      !em_door_movement_locked();
        if (!st_ok_noarm)
            printf("slider test: frame 13 door state %d lock %d — armed "
                   "without the USE press\n",
                   st_door >= 0 ? em_door_state(st_door) : -1,
                   em_door_movement_locked());
    } else if (n == 14) {
        move_test_inject('k', 1);   /* CROSS — the use-scan press edge */
    } else if (n == 15) {
        move_test_inject('k', 0);
    } else if (n == 20) {
        /* armed by the CROSS edge inside the class-5 window */
        st_ok_arm = st_door >= 0 &&
                    em_door_state(st_door) == EM_DOOR_OPENING;
        move_test_inject('w', 0);   /* the script owns the player now */
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
        move_test_inject('w', 1);   /* run on, out of the scan radius */
    } else if (n == 170) {
        move_test_inject('w', 0);
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
 *     and attacking; player health untouched (the flagged-minimal
 *     bug brain deals no damage).
 *   EM_ENEMY_TEST=2 (contact run): ONE WORM 30 u ahead; weapon stays
 *     holstered; let the worm
 *     run its decoded sequence (approach 90 t -> stalk 120 t homing ->
 *     windup 45 t -> lunge). The lunge CONNECT posts the decoded latch
 *     15 (D_008104D4) and the worm bursts -> assert health dropped by
 *     exactly 15 and the worm despawned. (With the leech asset the
 *     connect is the decoded neck->head SEGMENT arm; rig-less runs
 *     fold into the radius-6 contact — em_enemy.c LATCH SEGMENT.)
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
            et_check(g.status.health == g.et_health0,
                     "player health untouched (the minimal bug brain "
                     "deals no damage — flagged)");
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
                g.et_worm_atk = 1;   /* bugs engaged (walk brain) */
            if (!et_flinch_sent) {
                if (n == g.et_burst_frame + 30) {
                    et_check(g.et_worm_atk,
                             "bugs engaged after the burst (the "
                             "minimal walk brain)");
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
 * EM_BGM / the manifest started one); then (frame 40) asserts FIVE
 * synthetic pan/attenuation vectors against the decoded func_001FBF50
 * math (center/full at the player, range cull, hard-left at 90 deg,
 * the behind-the-camera phase inversion, the 18-u proximity ramp —
 * em_sfx.h "POSITIONAL AUDIO"), exercises the play-path range cull
 * (frame 50), bursts 60 plays to prove the 48-voice-budget OLDEST
 * steal with zero drops (frame 60), and prints the audio thread's
 * counters + PASS/FAIL at frame 120. Needs the registry: without
 * sfx.txt the module is disabled by design and the test reports the
 * FAIL. */
/* EM_PAUSE_TEST=1 — STATUS-SCREEN PAUSE self-test (the 2026-06-11
 * fidelity note: the open status menu PAUSES the game — gameplay_frame
 * gates the whole world update on em_hud_is_open()) PLUS the decoded
 * MENU-LOCK gate (em_door.h "THE TWO LOCKS": the engine's open poll
 * func_001AE7E0 refuses while the fade machine runs; the lock ends at
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
        move_test_inject('w', 1);          /* held under the pause */
    } else if (n == 70) {
        ok_frozen = fabsf(g.pos[0] - pp[0]) < 1e-4f &&
                    fabsf(g.pos[2] - pp[1]) < 1e-4f;
        move_test_inject('w', 0);
        move_test_inject('i', 1);          /* TRIANGLE — close */
    } else if (n == 72) {
        move_test_inject('i', 0);
    } else if (n == 80) {
        ok_closed = !em_hud_is_open();
        move_test_inject('w', 1);          /* must move now */
    } else if (n == 140) {
        move_test_inject('w', 0);          /* parked on the boundary */
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
 *   frame 12   assert: count[0x10] == 1, pack counter == 1, taken bit
 *              0xF01 set, reserve == r0 + 30 (the +30/pack applied
 *              through the holstered weapon), count[0x20] == 0
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
        ok_place  = a >= 0 && b >= 0 &&
                    em_pickup_item_count(EM_PICKUP_TYPE_MAG) == 0 &&
                    !em_pickup_taken(0xF01);
        if (!ok_place) g.pt_fail++;
    } else if (n == 5) {
        move_test_inject('k', 1);          /* CROSS — the use press */
    } else if (n == 7) {
        move_test_inject('k', 0);
    } else if (n == 12) {
        ok_take = em_pickup_item_count(EM_PICKUP_TYPE_MAG) == 1 &&
                  em_pickup_mag_packs() == 1 &&
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
        ok_gate = em_pickup_item_count(EM_PICKUP_TYPE_MAG) == 1 &&
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
                         em_pickup_item_count(EM_PICKUP_TYPE_MAG) == 1;
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
 *   phase 0  hold 'w' (run +Z); the frame the region engages
 *            (g.cam_region_on — one frame after the crossing), release.
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
            if (n == 0) move_test_inject('w', 1);
            if (g.cam_region_on) {
                move_test_inject('w', 0);
                mark  = n;
                phase = 1;
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
 * DAMAGE ONLY (the engine has no proximity trigger), and a damage
 * kill BROADCASTS the group alarm (decoded: the WHOLE live list, no
 * radius — which is why far-away B waking is the assertion) — crate
 * B wakes, runs the blind suicide hop AS THE CRATE down the open
 * corridor, self-bursts on the 180-tick attack timer and hatches its
 * OWN bug pair back there (they approach from behind and park at the
 * standoff — outside the frontal melee cone, so the whiff leg stays
 * clean). Adaptive phase machine:
 *
 *   phase 0  frame 8: assert both crates IDLE in reach, then tap L
 *            (CIRCLE) — the LIGHT combo (engine mode 0x21). Impact =
 *            swing tick 26 (len 50 - gate 24); the acquire resolves
 *            the nearest crate (A, slot 0).
 *   phase 1  crate A dies: assert melee hit count 1, ZERO rifle shots
 *            (the kill is the +0x36 mailbox write), TWO BUGS hatched
 *            at the crate ring (the s68 burst contract), and crate B
 *            AWAKE (ATTACK) — the decoded group-alarm broadcast.
 *   phase 2  HIT-CONFIRM path: recover anim 0x10F must commit (a
 *            landed hit SKIPS the combo — engine states 0x50..0x52).
 *            Wait out the recover (and any flinch); the bugs WALK IN
 *            (the minimal approach brain); when one is inside 9 u,
 *            tap J (SQUARE) — the HEAVY stab (mode 0x22, damage 15,
 *            immediate gate).
 *   phase 3  heavy 1 lands: melee hits 2, EXACTLY ONE bug dead (the
 *            decoded 15 damage vs the variant-A HP 15 — a one-stab
 *            kill through the every-tick mailbox).
 *   phase 4  heavy 2 at the surviving bug: melee hits 3, both A-bugs
 *            dead — bugs ARE victims (s68), the inverse of the worm
 *            leg below.
 *   phase 5  the WORM WHIFF witness (J2 s66, kept): spawn ONE worm
 *            9 u dead ahead (em_enemy_add — the port-convenience
 *            direct spawn; generators are its only engine source),
 *            heavy stab THROUGH it mid-approach — melee hits STAY 3
 *            (func_00183AC0 rejects model 0xD), the worm is
 *            untouched (ATTACK, vestigial HP 10).
 *   phase 6  the worm clears itself: its OWN lunge resolve bursts it
 *            on the player (health reported, not asserted) — wait
 *            out the flinch.
 *   phase 7  WHIFF COMBO (frontal cone empty: A-bugs dead, worm
 *            gone, B's bugs parked behind): tap L, re-tap L during
 *            each swing — assert the committed clip chains 0x10B ->
 *            0x10C -> 0x10D (the buffered +0x2E chain), NO recover
 *            anim on a whiff (clip-end exit), the final counts (7
 *            swings, 3 hits), crate B self-burst (no melee hit on
 *            it) and its bug pair alive behind the player.
 *
 * PASS = all checks green; any phase timing out fails the run. */
static void melee_test_script(void)
{
    static int saw_recov_anim, saw_heavy, chain12, chain23, whiff_recov;
    static int mt_bug_left = -1;    /* the A-bug surviving heavy 1     */
    static int mt_j2, mt_j3;        /* heavy 2 / heavy 3 tap frames    */
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
    if (em_weapon_melee_heavy()) saw_heavy = 1;
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
                /* the decoded GROUP-ALARM BROADCAST: the damage kill
                 * walks the live list (no radius) and wakes crate B —
                 * it consumed the alarm the same update tick (bugs
                 * are NOT whitelisted — they stay on their walk) */
                if (em_enemy_state(1) != EM_ENEMY_ATTACK) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — crate B not "
                           "alarmed by the broadcast (state %d)\n",
                           em_enemy_state(1));
                }
                g.mt_phase = 2;
                g.mt_mark  = n;
            }
            break;
        case 2:
            /* recover (hit-confirm) must complete (and any flinch
             * clear) before heavy 1 — the A-bugs WALK IN from the
             * 11-u crate ring (the minimal approach brain); stab once
             * one is inside 9 u: bugs ARE melee victims (s68). */
            if (em_weapon_is_melee() || player_damage_locked()) break;
            if (!saw_recov_anim) {
                g.mt_fail++;
                printf("melee test: CHECK FAILED — recover anim 0x10F "
                       "never committed after the confirmed hit\n");
                saw_recov_anim = -1;            /* report once */
            }
            if ((em_enemy_state(2) == EM_ENEMY_ATTACK &&
                 et_dist_i(2) < 9.0f) ||
                (em_enemy_state(3) == EM_ENEMY_ATTACK &&
                 et_dist_i(3) < 9.0f)) {
                move_test_inject('j', 1);       /* SQUARE — heavy 1 */
                g.mt_phase = 3;
                g.mt_mark  = n;
            }
            break;
        case 3:
            if (n == g.mt_mark + 2) move_test_inject('j', 0);
            if (saw_heavy && !em_weapon_is_melee()) {
                /* heavy 1 one-stabs a bug: the decoded 15 vs the
                 * variant-A HP 15 through the every-tick mailbox */
                int dead2 = em_enemy_state(2) == EM_ENEMY_FREE;
                int dead3 = em_enemy_state(3) == EM_ENEMY_FREE;
                if (!(em_weapon_melee_hits() == 2 &&
                      (dead2 ^ dead3))) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — heavy 1 must "
                           "one-stab a bug (hits %d, states %d/%d)\n",
                           em_weapon_melee_hits(), em_enemy_state(2),
                           em_enemy_state(3));
                }
                mt_bug_left = dead2 ? 3 : 2;
                g.mt_phase  = 4;
                g.mt_mark   = n;
            } else if (n > g.mt_mark + 90) {
                g.mt_fail++;
                printf("melee test: CHECK FAILED — heavy 1 never "
                       "swung/finished (state %d)\n",
                       em_weapon_melee_state());
                g.mt_phase = 4;
                g.mt_mark  = n;
            }
            break;
        case 4:
            /* heavy 2 kills the surviving A-bug (parked inside the
             * standoff, still in the frontal cone). */
            if (!mt_j2) {
                if (em_weapon_is_melee() || player_damage_locked())
                    break;
                if (em_enemy_state(mt_bug_left) == EM_ENEMY_ATTACK &&
                    et_dist_i(mt_bug_left) < 11.0f) {
                    move_test_inject('j', 1);   /* SQUARE — heavy 2 */
                    mt_j2 = n;
                }
            } else {
                if (n == mt_j2 + 2) move_test_inject('j', 0);
                if (n > mt_j2 + 4 && !em_weapon_is_melee()) {
                    if (!(em_weapon_melee_hits() == 3 &&
                          em_enemy_state(mt_bug_left) ==
                              EM_ENEMY_FREE)) {
                        g.mt_fail++;
                        printf("melee test: CHECK FAILED — heavy 2 "
                               "must kill the surviving bug (hits %d, "
                               "state %d)\n", em_weapon_melee_hits(),
                               em_enemy_state(mt_bug_left));
                    }
                    g.mt_phase = 5;
                    g.mt_mark  = n;
                }
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
                    if (!(em_weapon_melee_hits() == 3 &&
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
                if (em_weapon_melee_swings() != 7 ||
                    em_weapon_melee_hits() != 3) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — counts (swings "
                           "%d expected 7: light + 2 bug heavies + "
                           "the worm whiff + 3 whiffs; hits %d "
                           "expected 3 — bugs ARE victims, worms are "
                           "not)\n", em_weapon_melee_swings(),
                           em_weapon_melee_hits());
                }
                /* crate B cleared ITSELF (the 180-tick timer burst —
                 * no melee hit on it) and hatched its bug pair back
                 * down the corridor; the pair walks in behind the
                 * player and parks at the standoff, OUTSIDE the
                 * frontal cone (which is why the whiffs whiffed). */
                if (em_enemy_state(1) != EM_ENEMY_FREE) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — crate B did "
                           "not self-burst (state %d)\n",
                           em_enemy_state(1));
                }
                int live_bugs = 0;
                for (int i = 0; i < 16; i++)
                    if (em_enemy_kind(i) == EM_ENEMY_KIND_BUG &&
                        em_enemy_state(i) == EM_ENEMY_ATTACK)
                        live_bugs++;
                if (em_enemy_alive() != 2 || live_bugs != 2) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — crate B's bug "
                           "pair should be the only survivors (alive "
                           "%d, live bugs %d)\n", em_enemy_alive(),
                           live_bugs);
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
 * to 20 (instrumentation — two lunges at the DECODED latch 15 =
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
 *   9  dispatch at hold-black -> restart: scene reloaded, boot status
 *      restored (health 75), damage machine cleared, death pose
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
                gt_check(g.status.health == 75.0f &&
                         g.status.health_max == 100.0f,
                         "boot status restored on restart");
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

/* func_001AE5E0 — THE GAMEPLAY FRAME (stage order is the engine's). */
static void gameplay_frame(void)
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
     * the player in the doorway (live-verified geometry; with
     * EM_DOORCAM_LOCKED=1: the locked-look handle placement). */
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
     * (cam_solver_0018DD20): the eye parks 0.5 u off the wall while
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
    if (g.enemy_test) enemy_test_script();  /* debug instrumentation only */
    if (g.melee_test) melee_test_script();  /* debug instrumentation only */
    if (g.death_test) death_test_script();  /* debug instrumentation only */
    if (g.sfx_test)  sfx_test_script();     /* debug instrumentation only */
    if (g.pause_test) pause_test_script();  /* debug instrumentation only */
    if (g.pickup_test) pickup_test_script();/* debug instrumentation only */
    if (g.examine_test) examine_test_script();          /* same */
    if (g.camregion_test) camregion_test_script(); /* debug instr. only  */
    if (g.aim_test)  aim_test_script();     /* debug instrumentation only */
    /* STATUS-SCREEN PAUSE GATE: while the status screen is OPEN (the
     * real Triangle/Start toggle — em_hud_is_open(); the EM_HUD_FORCE
     * capture hook deliberately does NOT pause, see em_hud.h) the
     * world simulation HALTS: no actor/door/enemy/weapon updates, no
     * camera dispatch — the player cannot move, exactly the original's
     * menu pause. The frame still renders: the chain re-records from
     * the modules' frozen palettes, the camera commits (window-resize
     * safe), and the close-out runs em_hud_update so the screen can be
     * closed (the toggle stays live) — idle/animation clocks freeze
     * because actor_update never runs. */
    if (em_hud_is_open()) {
        render_chain_build();    /* frozen poses, current scene */
        camera_update();         /* freeze path: commit only (above) */
        frame_close_out();       /* flush + hud toggle + fade + capture */
        return;
    }
    /* GAME-OVER / CONTINUE GATE: from GO_SCREEN on, the engine's
     * gameplay task is parked (wait state) and then REPLACED WHOLESALE
     * by the continue machine (s66/s70 — the PD block doc): the world
     * stops existing. The port models that the same way the menu
     * pause does — the world simulation halts, the frame still
     * renders (the screens' opaque base hides the dead scene), and
     * game_over_tick owns input/fades/dispatch. The restart latch is
     * serviced by the frame machine before this frame re-runs. */
    if (g.go_state >= GO_SCREEN) {
        game_over_tick();        /* the continue-machine slice */
        render_chain_build();    /* frozen world under the screens */
        camera_update();         /* commit only */
        frame_close_out();       /* overlays + fade + capture */
        return;
    }
    actor_context_begin();   /* func_001CB590(0x008102B0, 0x320, ...) */
    actor_update();          /* func_0015BCF0 — player actor update   */
    actor_context_end();     /* func_001CB5A0                         */
    render_chain_build();    /* func_001D1C50 — render chain build    */
    render_env_init();       /* func_001C1D00(0x008101D0)             */
    /* func_001AFD70(0) — the actor-pool tick (world services). The
     * port's first pooled actors are the DOORS: per-frame behavior
     * (func_001BC350 state machine), the player use scan
     * (func_00184BA0) and articulation live in em_door_update.
     * func_0015C160 / func_001F0360 — still untranslated. */
    em_door_update(&g.coll, g.pos, g.yaw, em_frame_input());
    /* GOTO-DOOR SCENE SWITCH (one-shot, at fade-out completion — screen
     * fully black): the runtime area/sub-state load. Free + reload the
     * scene (em_game_scene_switch), place the player at the decoded
     * arrival spawn with the exit yaw, re-seat the camera, and RE-RECORD
     * the render chain — the chain built earlier this frame points into
     * the freed scene/door/enemy tables (the close-out flush must see
     * the new scene's draws). All invisible: the fade is at full black
     * and the fade-in was armed by the door before posting. */
    {
        char  gdir[64];
        float gp[3], gyaw;
        if (em_door_goto_pending(gdir, sizeof gdir, gp, &gyaw)) {
            if (em_game_scene_switch(gdir) == 0) {
                if (g.transit_test && !g.tt_switch_frame)
                    g.tt_switch_frame = g.frame_no;
                g.pos[0] = gp[0];
                g.pos[1] = gp[1];
                g.pos[2] = gp[2];
                g.yaw    = gyaw;
                g.doorcam      = 3;   /* cinematic over (op 0x18) */
                g.cam.tgt_soft = 0;
                g.cam.yaw = gyaw;
                g.cam.tgt_des[0] = g.pos[0];
                g.cam.tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
                g.cam.tgt_des[2] = g.pos[2];
                camera_desired_eye(&g.cam);
                memcpy(g.cam.eye, g.cam.eye_des, sizeof g.cam.eye);
                memcpy(g.cam.tgt, g.cam.tgt_des, sizeof g.cam.tgt);
            }
            render_chain_build();   /* drop the freed-scene draw records */
        }
    }
    /* DOOR-TRANSIT RE-PLACE (one-shot, at fade-out completion — screen
     * fully black): set the player at the spawn point behind the door
     * with the exit yaw (the engine's spawn-table placement,
     * func_001B07C0 path), and re-seat the chase camera behind the new
     * pose (the placement rec's camera-init fields) — an invisible cut,
     * exactly like the engine doing it under the fade. */
    {
        float wp[3], wyaw;
        if (em_door_warp_pending(wp, &wyaw)) {
            g.pos[0] = wp[0];
            g.pos[1] = wp[1];
            g.pos[2] = wp[2];
            g.yaw    = wyaw;
            g.doorcam      = 3;   /* cinematic over (op 0x18 restore) */
            g.cam.tgt_soft = 0;
            g.cam.yaw = wyaw;
            g.cam.tgt_des[0] = g.pos[0];
            g.cam.tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
            g.cam.tgt_des[2] = g.pos[2];
            camera_desired_eye(&g.cam);
            memcpy(g.cam.eye, g.cam.eye_des, sizeof g.cam.eye);
            memcpy(g.cam.tgt, g.cam.tgt_des, sizeof g.cam.tgt);
        }
    }
    /* PICKUPS (em_pickup.h — the pool's item actors plus the player
     * use scan's archetype-3 ITEM branch). The scan rides the same
     * CROSS press edge as the door scan above; the engine's single
     * nearest-wins walk over one interactive list is approximated
     * DOORS-FIRST: a press that armed a door has engaged the movement
     * lock by now, which suppresses the item scan (the engine's
     * scripted-frame spad-3B8D gate shape). Damage lock likewise. */
    em_pickup_update(g.pos, g.yaw, em_frame_input(),
                     !em_door_movement_locked() &&
                     !player_damage_locked());
    {
        /* Collection events (one-shot takes — em_pickup.h):
         *  - FOUND: the engine posts D_008106B0/B1 and auto-opens the
         *    status screen at the item's record; the port stand-in is
         *    the em_hud Found line (flagged there).
         *  - AMMO (func_001C40B0 case 0x10, +30 reserve per pack):
         *    applied through em_weapon_reset — the module's only ammo
         *    writer — so only while the machine is quiescent
         *    (HOLSTERED, no melee); otherwise the rounds stay pending
         *    inside em_pickup until the stance settles. */
        int found = em_pickup_found_take();
        if (found >= 0)
            em_hud_found_show(found);
        if (em_weapon_state() == EM_WPN_HOLSTERED &&
            !em_weapon_is_melee()) {
            int rounds = em_pickup_ammo_take();
            if (rounds > 0) {
                em_weapon_reset(em_weapon_mag(),
                                (int16_t)(em_weapon_reserve() + rounds));
                printf("pickup: +%d reserve rounds (mag %u, reserve "
                       "%d)\n", rounds, em_weapon_mag(),
                       em_weapon_reserve());
            }
        }
    }
    /* EXAMINE objects (em_examine.h — the overlay examine behaviors:
     * archetype scan on the same CROSS press edge, then the scripted
     * sequence: input pause + the mode-2 radio line + the optional
     * op00 camera cue). Doors-first like the pickup scan above; the
     * running sequence suppresses its own scan internally. */
    em_examine_update(g.pos, g.yaw, em_frame_input(),
                      !em_door_movement_locked() &&
                      !player_damage_locked());
    /* ENEMIES: the enemy state machines (func_001551B0 crates +
     * func_00153F10 worms — also part of the actor-pool tick). Runs
     * BEFORE the weapon update so this frame's shot resolves against
     * current positions. */
    em_enemy_update(&g.coll, g.pos);
    /* PLAYER DAMAGE pipeline (the PD_* block above). The port's enemy
     * producers post one mailbox int (em_enemy.h, +0x36 code layout);
     * the engine's player producers instead write the pending-damage
     * floats directly — the bridge maps the two codes onto the decoded
     * fields: the worm's lunge connect (type bit 0x4000, amount 15 =
     * the decoded lunge latch D_008104D4) -> pending
     * HEALTH +0x224; the open breather pad (GEN_TRAP_HIT = 5, the s33
     * event-3 write) -> pending INFECTION +0x22C = 5.0 (the engine pad
     * infects, it does not wound — the old health consume here was the
     * P3/C12 gap). Then the processor (func_0021C440 generic tail)
     * applies and routes to flinch/death, and the passive vitals tick
     * (drain/kill plane/i-frames) runs. */
    {
        int hitcode = em_enemy_player_hit_take();
        if (hitcode & 0x4000)
            g.pd_pend_hp += (float)(hitcode & 0xFFF);
        else if (hitcode)
            g.pd_pend_inf += (float)hitcode;
        player_damage_process();
        player_vitals_tick();
        /* (the GO machine ticks from the frozen gate above once
         * GO_SCREEN is reached — the live path never runs it) */
    }
    /* WEAPON: the player-side armed-stance/fire state machine (engine:
     * part of the player actor update, modes 0x1D..0x20) plus the
     * gun-actor fire-event consumption (engine: pool tick, one-frame
     * latency) — both in em_weapon_update; see em_weapon.h. During the
     * door-transit INPUT LOCK the machine reads NEUTRAL input (actions
     * ignored: no draw/fire/reload; a held stance settles to holstered),
     * keeping its per-frame timers ticking. */
    {
        static const EmFrameInput kNeutral = { 0x80, 0x80, 0x80, 0x80,
                                               0, 0, 0 };
        /* the hit-reaction/death lock reads NEUTRAL like the transit
         * lock (engine state 2 clears the trigger latch +0x274/+0x276
         * every frame — func_0015BA50 tail) */
        em_weapon_update(&g.coll, g.pos, g.yaw,
                         (em_door_movement_locked() ||
                          player_damage_locked()) ? &kNeutral
                                                : em_frame_input());
    }
    camera_update();         /* func_001CB590(0x008101E0, 0xD0, 0) +
                              * func_0018B9C0 camera state machine    */
    em_sfx_listener(g.pos, g.cam.eye, g.cam.yaw);  /* positional-audio
                              * listeners: player = distance
                              * (D_00810360), camera eye/yaw = pan
                              * (D_008105D0 / cam+0x9C) — em_sfx.h    */
    frame_close_out();       /* func_001CB5A0/001AAD00/001D1EA0(1)    */
}

/* func_001AE6B0 — cutscene/scripted frame variant. Skeleton only: the
 * native selector never routes here yet. The original polls the frame
 * input block's button words for mask 0x0900 (the third halfword,
 * 0x00810E74) to allow skipping. */
static void cutscene_frame(void)
{
    const EmFrameInput *in = em_frame_input();
    if (in->pressed & 0x0900) {
        /* skip request — unhandled until cutscenes exist natively */
    }
}

/* ------------------------------------------------------------------ */
/* State machines (slot-0 task chain)                                  */
/* ------------------------------------------------------------------ */

/* func_001AE040 — in-game frame machine (state byte task+0xB). */
static void ingame_frame_machine(EmTask *self)
{
    switch (self->user[GAME_BYTE_FRAME]) {
        case 0:
            /* Scene-init arm: the engine resets per-frame flags, builds
             * the difficulty map, places the player against the area
             * spawn tables, and initializes camera + HUD/weapon contexts.
             * Natively the spawn-table stand-in is the scene manifest's
             * spawn (the office kPlayerPos default; origin with no scene
             * loaded), and the camera struct is zeroed back to its init
             * state (state 0 -> the one-shot setup arms it behind the
             * player on the next frame, along the spawn facing). */
            g.walk_t         = 0.0;
            g.walk_w         = 0.0f;
            g.step_prev      = 0.0;   /* footstep edge state re-armed */
            g.idle_t         = 0.0;   /* idle cycle re-armed (mode-0
                                       * entry: breathing + 300-frame
                                       * fidget timer) */
            g.idle_phase     = 0;
            g.idle_timer     = IDLE_FIDGET_FRAMES;
            g.fid_t          = 0.0;
            g.fid_w          = 0.0f;
            g.gait           = 0;
            g.loco_tier      = 0;     /* tier ramp re-armed (+0x25C/+0x38) */
            g.loco_upt       = 0.0f;
            g.cam_recenter   = 0;
            g.cam_idle       = 0;
            g.frame_no       = 0;
            g.frame_selector = 0;
            g.sa_req         = 0;     /* scripted-anim mailbox cleared */
            g.sa_cur         = 0;     /* (player anim re-init state)   */
            g.sa_clip        = -1;
            g.sa_req_hold    = 0;
            g.sa_hold        = 0;
            g.pos[0] = g.n_scene ? g.spawn[0] : 0.0f;
            g.pos[1] = g.n_scene ? g.spawn[1] : 0.0f;
            g.pos[2] = g.n_scene ? g.spawn[2] : 0.0f;
            g.yaw    = g.n_scene ? g.spawn_yaw : 0.0f;
            if (g.door_test || g.capture_door || g.pause_test) {
                /* EM_DOOR_TEST / EM_CAPTURE_DOOR / EM_PAUSE_TEST spawn:
                 * the z = -225 corridor line in front of the west
                 * double door, facing -X (see door_test_script;
                 * EM_PAUSE_TEST uses the same approach for its door leg
                 * — the mid-walk-out menu check). */
                g.pos[0] = 72.0f;
                g.pos[1] = 0.0f;
                g.pos[2] = -225.0f;
                g.yaw    = -EM_PI * 0.5f;
            }
            if (g.capture_supply) {
                /* EM_CAPTURE_SUPPLY spawn: on the x = 104 doorway-center
                 * column of the office DOUBLE DOORS (door id 2), facing
                 * them (south, yaw pi) — the approach + CROSS below
                 * carries the transit into the SUPPLY ROOM (area 2 room
                 * 1 entry 3), whose spawn-record FIXED camera the
                 * capture frame samples. */
                g.pos[0] = 104.0f;
                g.pos[1] = 0.0f;
                g.pos[2] = -238.0f;
                g.yaw    = EM_PI;
            }
            if (g.transit_test) {
                /* EM_TRANSIT_TEST spawn: on the west DOORWAY-CENTER z
                 * line (z = -225.5 — the placement pos is the HINGE
                 * corner; the decoded use scan measures from the
                 * center), 9 u east of it, facing it (see
                 * transit_test_script). */
                g.pos[0] = 66.0f;
                g.pos[1] = 0.0f;
                g.pos[2] = -225.5f;
                g.yaw    = -EM_PI * 0.5f;
            }
            if (g.slider_test) {
                /* EM_SLIDER_TEST spawn (scene_drawbridge): on the
                 * slider door 4's z = -610 line, 16.6 u west of it,
                 * facing +X (see slider_test_script). */
                g.pos[0] = 112.0f;
                g.pos[1] = 0.0f;
                g.pos[2] = -610.0f;
                g.yaw    = EM_PI * 0.5f;
            }
            if (g.locked_test) {
                /* EM_LOCKED_TEST spawn (scene_drawbridge): on the m15
                 * security door's doorway-center column (center
                 * (-25.5, -192)), BACK side, 9 u out, facing the door
                 * (+Z) — see locked_test_script. */
                g.pos[0] = -25.5f;
                g.pos[1] = 0.0f;
                g.pos[2] = -201.0f;
                g.yaw    = 0.0f;
            }
            memset(&g.cam, 0, sizeof g.cam);
            g.cam.yaw = g.yaw;   /* chase camera starts behind the spawn */
            g.cam_region_on = 0;
            /* EM_CAMREGION_TEST: SYNTHETIC test region (FLAGGED — it
             * REPLACES the scene's region list for the run, so the
             * test stays spawn-local and deterministic even now that
             * the office carries the REAL supply-room line, the
             * spawn-record camera decode in the CAMERA FIDELITY
             * block). A strip starting 5 u down +Z of the spawn (the
             * run-forward corridor; the wall radius stops the player
             * ~14 u in, well inside), with the fixed eye raised
             * BEHIND the spawn looking INTO the strip (real room
             * cameras watch the room — and camera-relative 'w' then
             * keeps pushing the player deeper, not back across the
             * boundary): entering it must pin the camera there.
             * scene_snow's REAL region (AREA06, D_0024A5F0[2]) and
             * the supply-room line exercise this same machinery. */
            if (g.camregion_test) {
                g.n_camregion  = 1;
                g.camregion[0] = (EmCamRegion){
                    .x0 = g.pos[0] - 25.0f, .z0 = g.pos[2] + 5.0f,
                    .x1 = g.pos[0] + 25.0f, .z1 = g.pos[2] + 34.0f,
                    .ygate  = g.pos[1],
                    .eye    = { g.pos[0] - 6.0f, g.pos[1] + 24.0f,
                                g.pos[2] - 12.0f } };
                printf("camregion test: SYNTHETIC region armed — "
                       "X[%.1f,%.1f] Z[%.1f,%.1f], eye (%.1f, %.1f, "
                       "%.1f)\n",
                       g.camregion[0].x0, g.camregion[0].x1,
                       g.camregion[0].z0, g.camregion[0].z1,
                       g.camregion[0].eye[0], g.camregion[0].eye[1],
                       g.camregion[0].eye[2]);
            }
            /* Weapon context init (the engine's HUD/weapon-context arm):
             * holstered, ammo from the demo status (live test save:
             * mag 4, reserve 120). The HUD mirrors the weapon live from
             * here on (frame_close_out). */
            em_weapon_reset(g.status.mag, g.status.reserve);
            /* EM_ENEMY_TEST spawn: test 2 places one WORM 30 units
             * ahead of the player spawn along the spawn facing (its
             * own INIT yaws it at the player — the decoded
             * func_00154040); tests 1/3 place a DISGUISED CRATE 12 /
             * 25 units ahead instead (see enemy_test_script). */
            /* EM_MELEE_TEST spawn: TWO DISGUISED CRATES — A 11.0 u dead
             * ahead (slot 0, inside the knife reach 12, inside the
             * office spawn's 14-u wall plane — the LIGHT-combo kill)
             * and B 25 u BEHIND the player down the open south
             * corridor (slot 1 — the group-alarm WITNESS: the decoded
             * broadcast walks the whole live list with NO radius, so
             * a crate clear across the room must wake too). B faces
             * south (away): its alarm-driven suicide hop runs down
             * the open corridor — never near the melee cone — and its
             * 180-tick timer bursts it there. (Facing it at the 14-u
             * wall boxed the steer in and the engine-true budget
             * returned it to dormancy — the run needs open ground.) */
            if (g.melee_test && !g.et_spawned) {
                g.et_spawned = 1;
                float fx = sinf(g.yaw), fz = cosf(g.yaw);
                float pa[3] = { g.pos[0] + fx * 11.0f, g.pos[1],
                                g.pos[2] + fz * 11.0f };
                float pb[3] = { g.pos[0] - fx * 25.0f, g.pos[1],
                                g.pos[2] - fz * 25.0f };
                if (em_enemy_add_kind(em_frame_gfx(), EM_ENEMY_KIND_CRATE,
                                      pa, g.yaw + EM_PI) < 0 ||
                    em_enemy_add_kind(em_frame_gfx(), EM_ENEMY_KIND_CRATE,
                                      pb, g.yaw + EM_PI) < 0)
                    printf("melee test: spawn failed\n");
            }
            /* EM_DEATH_TEST shares the contact-run placement: one
             * worm 30 u dead ahead (death_test_script). */
            if ((g.enemy_test || g.death_test) && !g.et_spawned) {
                g.et_spawned = 1;
                /* test 1 (kill run, restaged s66): the SHOOTABLE
                 * target is a CRATE 12 u dead ahead — inside the
                 * office wall plane 14 u out, so the world ray ranks
                 * behind the victim. Test 3: crate 25 u down the open
                 * corridor (about-face below). Test 2 / death test:
                 * one worm 30 u ahead. */
                int   ek = (g.enemy_test == 1 || g.enemy_test == 3)
                           ? EM_ENEMY_KIND_CRATE
                           : EM_ENEMY_KIND_CRAWLER;
                float ed = g.enemy_test == 1 ? 12.0f
                         : g.enemy_test == 3 ? 25.0f : 30.0f;
                /* Crate run only: the office spawn faces a wall plane
                 * 14 u ahead (see the kill-run wall note), so a walking
                 * player could never reach point-blank range at a
                 * crate 25 u beyond it. About-face the spawn pose
                 * (player + chase camera — the one-shot camera arm
                 * reads g.cam.yaw next frame) so the crate goes 25 u
                 * down the OPEN south corridor, still dead ahead. */
                if (g.enemy_test == 3) {
                    g.yaw    += EM_PI;
                    g.cam.yaw = g.yaw;
                }
                float ep[3] = { g.pos[0] + sinf(g.yaw) * ed,
                                g.pos[1],
                                g.pos[2] + cosf(g.yaw) * ed };
                if (em_enemy_add_kind(em_frame_gfx(), ek, ep,
                                      g.yaw + EM_PI) < 0)
                    printf("enemy test: spawn failed\n");
            }
            self->user[GAME_BYTE_FRAME] = 1;
            /* fall through — the engine's init frame still renders */
        case 1:
            /* CONTINUE RESTART (the decoded dispatch: prompt cursor 0
             * confirmed at hold-black — the engine reinstalls the
             * gameplay task func_001ACEC0 with the from-death flag
             * up and D_00275BE0 = 0; PD block doc): reload the active
             * scene — the manifest re-run rebuilds doors/enemies/
             * collision — restore the boot status, clear the damage
             * machine, re-arm the scene-init state (player re-place +
             * camera + weapon), and fade back in. */
            if (g.go_restart) {
                g.go_restart  = 0;
                g.go_state    = GO_OFF;
                g.go_frames   = 0;
                g.go_hold     = 0;
                g.go_timer    = 0;
                g.go_cursor   = 0;
                g.pd_state    = 0;
                g.pd_sub      = 0;
                g.pd_phase    = 0;
                g.pd_hold     = 0;
                g.pd_iframes  = 0;
                g.pd_pend_hp  = 0.0f;
                g.pd_pend_inf = 0.0f;
                g.pd_drain_t  = 0;
                g.pd_clip     = 0;
                /* infected latch persists across a continue? Unknown —
                 * the port restores the boot status wholesale
                 * (flagged). */
                g.pd_infected = 0;
                g.pd_low      = 0;
                g.status = (EmPlayerStatus){ .health = 75.0f,
                                             .health_max = 100.0f,
                                             .infection = 60.0f,
                                             .mag = 4, .mag_max = 30,
                                             .reserve = 120,
                                             .battery = 4,
                                             .battery_max = 6 };
                g.et_spawned = 0;     /* self-tests may re-spawn */
                em_game_scene_switch(g.scene_dir);
                em_frame_fade_start(-1, EM_FADE_SPEED_DOOR);
                self->user[GAME_BYTE_FRAME] = 0;
                ingame_frame_machine(self);   /* re-init this frame */
                break;
            }
            /* Live in-game arm: per-frame services (func_001AFCF0 flag
             * reset, func_001B07C0(1) placement check, func_001C1DC0
             * render-env updater (channel enables / fog / weather — NOT
             * camera math; FINDINGS.md "CAMERA SYSTEM" corrections),
             * func_001AE7E0 end-of-level poll — all pending translation),
             * then the frame-variant selector (scratchpad 0x70003B8D). */
            if (g.frame_selector)
                cutscene_frame();   /* func_001AE6B0 */
            else
                gameplay_frame();   /* func_001AE5E0 */
            break;
        default:
            /* Remaining jr-table 0x0026DD30 arms (pause/level-exit paths)
             * — pending translation. */
            break;
    }
}

/* func_001AD250 — game sub-machine (state byte task+9; 6 states in
 * jr-table 0x0026DCB0). The live arm reaches the in-game frame machine
 * through the trampoline func_001AD4D0 (= j func_001AE040). */
static void game_sub_machine(EmTask *self)
{
    switch (self->user[GAME_BYTE_SUB]) {
        case 0:
            /* Setup arm — natively nothing to stage yet. */
            self->user[GAME_BYTE_SUB] = 1;
            /* fall through */
        case 1:
            ingame_frame_machine(self);  /* via the func_001AD4D0 jump */
            break;
        default:
            /* States 2..5 (menu/loading/teardown arms) — pending. */
            break;
    }
}

/* func_001ACEC0 — game task machine (state byte task+8; live value 3). */
static void game_task(void)
{
    EmTask *self = em_task_current();
    switch (self->user[GAME_BYTE_MAIN]) {
        case 0:
            /* Entry arm. The original walks intermediate mode arms (the
             * live record sits at state 3); the skeleton jumps straight
             * to the in-game arm. */
            self->user[GAME_BYTE_MAIN] = 3;
            /* fall through */
        case 3:
            game_sub_machine(self);      /* func_001AD250 */
            break;
        default:
            /* Other mode arms (title/menu flows) — pending. */
            break;
    }
}

/* func_001AB7E0 — boot/flow task: first dispatch loads the assets, then
 * slot 0 is re-registered with the game task, exactly mirroring the live
 * engine (slot 0 = func_001ACEC0 after boot). */
static void game_boot_task(void)
{
    EmGfx *gfx = em_frame_gfx();

    /* Optional character asset (disc-derived, generated locally). */
    if (em_model_load(&g.model, MODEL_PATH) == 0) {
        g.mesh = em_gfx_mesh_create(gfx, g.model.verts, g.model.vert_count,
                                    g.model.indices, g.model.index_count,
                                    (const EmGfxTexDesc *)g.model.texs,
                                    g.model.tex_count, g.model.texels,
                                    g.model.flags);
        printf("loaded %s: %u bones, %u verts, %u tris, %u frames @ %.0f fps, "
               "%u clips, %u textures\n",
               MODEL_PATH, g.model.bone_count, g.model.vert_count,
               g.model.index_count / 3, g.model.frame_count, g.model.fps,
               g.model.clip_count, g.model.tex_count);
        /* Resolve the named clips. Idle = the engine's breathing idle
         * (id 0); the fidget 349 enables the idle CYCLE. Degradations:
         * an asset without clip id 0 idles on 349 alone (the pre-s38
         * single look-around — cycle off), and an old single-clip
         * (EMD2) asset keeps the original behavior: idle = clip 0 of
         * the table, no crossfade. */
        g.clip_idle   = em_model_clip_index(&g.model, CLIP_ID_IDLE);
        g.clip_fidget = g.clip_idle >= 0
                      ? em_model_clip_index(&g.model, CLIP_ID_FIDGET)
                      : -1;
        if (g.clip_idle < 0)
            g.clip_idle = em_model_clip_index(&g.model, CLIP_ID_FIDGET);
        if (g.clip_idle < 0) g.clip_idle = 0;
        g.clip_walk = em_model_clip_index(&g.model, CLIP_ID_WALK);
        if (g.clip_walk == g.clip_idle) g.clip_walk = -1;
        g.clip_jog  = em_model_clip_index(&g.model, CLIP_ID_JOG);
        if (g.clip_jog == g.clip_idle) g.clip_jog = -1;
        g.clip_run  = em_model_clip_index(&g.model, CLIP_ID_RUN);
        if (g.clip_run == g.clip_idle) g.clip_run = -1;
        g.loco_clip  = g.clip_walk;
        g.loco_speed = WALK_CLIP_SPEED;
        /* Status-menu UI scene clips (engine 0x1C2 stance / 0xA low-
         * health idle). An older asset without them falls back to the
         * breathing idle in ui_scene_render — flagged here once. */
        g.clip_menu     = em_model_clip_index(&g.model, CLIP_ID_MENU);
        g.clip_menu_low = em_model_clip_index(&g.model, CLIP_ID_MENU_LOW);
        if (g.clip_menu == g.clip_idle)     g.clip_menu = -1;
        if (g.clip_menu_low == g.clip_idle) g.clip_menu_low = -1;
        if (g.clip_menu < 0)
            printf("menu stance clip 450 missing — status screen uses "
                   "the breathing idle (re-export with --clips "
                   "...,0,450,10)\n");
        printf("clips: idle #%d (id %u)%s%s\n", g.clip_idle,
               g.model.clips[g.clip_idle].id,
               g.clip_fidget >= 0 ? ", idle cycle on (fidget 349)"
                                  : ", idle cycle off",
               g.clip_walk >= 0 ? ", walk found — locomotion crossfade on"
                                : " only — crossfade off (re-export with "
                                  "--clips 349,2,3,69,67,75,...,0)");
    } else {
        printf("no %s — showing the test triangle. Generate it with the "
               "decomp repo's tools/export_native.py\n", MODEL_PATH);
    }

    /* Optional scene (level parts, world-space) + its manifest (spawn /
     * collision filename / bgm / doors — office defaults when absent). */
    em_door_reset();
    em_enemy_reset();
    scene_manifest_load();
    g.n_scene = scene_load(gfx, g.scene, SCENE_MAX);

    /* Optional collision world (id 0x44 -> EMCL, disc-derived, generated
     * locally by the decomp repo's tools/export_collision.py; filename
     * from the scene manifest). Without it movement falls back to the
     * room-bbox clamp. */
    if (em_collision_load(&g.coll, g.coll_path) == 0) {
        printf("collision: %s — %u polys (%u verts), grid %s\n",
               g.coll_path, g.coll.poly_count, g.coll.vert_count,
               (g.coll.flags & 1) ? "decoded" : "absent (flat-floor)");
    } else {
        printf("no %s — movement uses the room-bbox clamp. Generate it "
               "with the decomp repo's tools/export_collision.py\n",
               g.coll_path);
    }

    /* SFX registry preload (assets/sfx/sfx.txt) — the native stand-in
     * for the area's SShd bank load. No registry = the module stays a
     * silent no-op (zero behavior change); see em_sfx.h. */
    em_sfx_init();

    /* BGM at the boot->game handoff — the native func_001FB0B0 moment:
     * on the PS2 the area flow writes the level's cue id to the
     * current-BGM global D_00810D38 and func_001FAE70 fades the stream
     * in (the in-level cues carry the loop flag in the D_0025DD30 table).
     * Natively EM_BGM=<path.wav> names a locally exported cue WAV and
     * stands in for the cue id until the native cue table lands; without
     * the env, the scene manifest's optional "bgm <file.wav>" (a cue WAV
     * in the scene dir) plays instead; neither = silent, exactly as
     * before (em_bgm never opens a device). */
    if (g.bgm_path) {
        em_bgm_play(g.bgm_path, 1);  /* func_001FB0B0(cue) — looping BGM */
    } else if (g.bgm_file[0]) {
        char path[560];
        snprintf(path, sizeof path, "%s/%s", SCENE_DIR, g.bgm_file);
        em_bgm_play(path, 1);
    }

    em_task_register(0, game_task);  /* func_001AB740(0, func_001ACEC0) */
}

void em_game_install(void)
{
    memset(&g, 0, sizeof g);
    snprintf(g.scene_dir, sizeof g.scene_dir, "%s", SCENE_DIR);
    /* Player status — static demo values matching the live test save
     * (FINDINGS.md "INVENTORY LOCATED": health 75/100, infection 60%,
     * mag 4/30, reserve 120, battery 04/06) until the weapon/health
     * systems are translated. */
    g.status = (EmPlayerStatus){ .health = 75.0f, .health_max = 100.0f,
                                 .infection = 60.0f, .mag = 4,
                                 .mag_max = 30, .reserve = 120,
                                 .battery = 4, .battery_max = 6 };
    g.bgm_path     = getenv("EM_BGM");
    g.capture_path = getenv("EM_CAPTURE");
    const char *cf = getenv("EM_CAPTURE_FRAME");
    g.capture_frame = cf ? atoi(cf) : 60;   /* default = the historical
                                               regression frame */
    const char *ca = getenv("EM_CAPTURE_AIM");
    g.capture_aim  = ca ? atoi(ca) : 0;     /* 1 = aim, 2 = aim + shot,
                                               3/4 = aim up/down pose,
                                               5 = aim near a wall (the
                                               func_0018F870 pull-in) */
    if (g.capture_aim == 5 && !cf)
        g.capture_frame = 420;              /* aim settled at the wall */
    const char *cd = getenv("EM_CAPTURE_DOOR");
    g.capture_door = cd && cd[0] == '1';    /* door cinematic capture */
    if (g.capture_door && !cf)
        g.capture_frame = 110;              /* mid-cinematic default */
    const char *cs = getenv("EM_CAPTURE_SUPPLY");
    g.capture_supply = cs && cs[0] == '1';  /* supply-room fixed camera */
    if (g.capture_supply && !cf)
        g.capture_frame = 360;              /* post-transit, in-room */
    const char *cr = getenv("EM_CAPTURE_RISE");
    g.capture_rise = cr && cr[0] == '1';    /* walk-at-camera rise demo */
    const char *cx = getenv("EM_CAPTURE_EXAMINE");
    g.capture_examine = cx ? atoi(cx) : 0;  /* N = examine slot N-1 (snow:
                                             * 1 = AREA06 switch message,
                                             * 2 = AREA11 refusal line) */
    if (g.capture_examine && !cf)
        g.capture_frame = 60;               /* message mid-presentation */
    const char *co = getenv("EM_CAPTURE_ORIENT");
    g.capture_orient = co && co[0] == '1';  /* idle auto-orient demo */
    const char *mt = getenv("EM_MOVE_TEST");
    g.move_test    = mt && mt[0] == '1';
    g.move_legs[0] = 60;   /* the historical office legs */
    g.move_legs[1] = 30;
    const char *ml = getenv("EM_MOVE_LEGS");
    if (ml) {
        int l0, l1;
        if (sscanf(ml, "%d,%d", &l0, &l1) == 2 && l0 > 0 && l1 > 0) {
            g.move_legs[0] = l0;
            g.move_legs[1] = l1;
        }
    }
    const char *me = getenv("EM_MOVE_EXPECT");
    if (me && sscanf(me, "%f,%f,%f", &g.move_expect[0], &g.move_expect[1],
                     &g.move_expect[2]) == 3)
        g.move_expect_set = 1;
    const char *dt = getenv("EM_DOOR_TEST");
    g.door_test    = dt && dt[0] == '1';
    const char *tt = getenv("EM_TRANSIT_TEST");
    g.transit_test = tt && tt[0] == '1';
    const char *sl = getenv("EM_SLIDER_TEST");
    g.slider_test  = sl && sl[0] == '1';
    const char *lk = getenv("EM_LOCKED_TEST");
    g.locked_test  = lk && lk[0] == '1';
    const char *wt = getenv("EM_WEAPON_TEST");
    g.weapon_test  = wt && wt[0] == '1';
    const char *et = getenv("EM_ENEMY_TEST");
    if (et && et[0] >= '1' && et[0] <= '3')
        g.enemy_test = et[0] - '0';
    g.enemy_test4 = et && et[0] == '4' && et[1] == '\0';
    const char *st = getenv("EM_SFX_TEST");
    g.sfx_test     = st && st[0] == '1';
    const char *pt = getenv("EM_PAUSE_TEST");
    g.pause_test   = pt && pt[0] == '1';
    const char *cg = getenv("EM_CAMREGION_TEST");
    g.camregion_test = cg && cg[0] == '1';
    const char *at = getenv("EM_AIM_TEST");
    g.aim_test     = at && at[0] == '1';
    const char *kt = getenv("EM_MELEE_TEST");
    g.melee_test   = kt && kt[0] == '1';
    const char *gt = getenv("EM_DEATH_TEST");
    g.death_test   = gt && gt[0] == '1';
    const char *ik = getenv("EM_PICKUP_TEST");
    g.pickup_test  = ik && ik[0] == '1';
    const char *xt = getenv("EM_EXAMINE_TEST");
    g.examine_test = xt && xt[0] == '1';
    em_pickup_reset();   /* new-game inventory/taken wipe — the engine's
                          * D_00810700-block memset (func_001AF2C0) */
    em_task_register(0, game_boot_task);  /* func_001AB740(0, 0x001AB7E0) */
}

void em_game_shutdown(void)
{
    EmGfx *gfx = em_frame_gfx();
    if (g.mesh) {
        em_gfx_mesh_destroy(gfx, g.mesh);
        em_model_free(&g.model);
        g.mesh = NULL;
    }
    for (int i = 0; i < g.n_scene; i++) {
        em_gfx_mesh_destroy(gfx, g.scene[i].mesh);
        em_model_free(&g.scene[i].model);
        free(g.scene[i].palette);
    }
    g.n_scene = 0;
    em_door_shutdown(gfx);
    em_enemy_shutdown(gfx);
    em_pickup_scene_clear(gfx);
    em_examine_reset();
    em_collision_free(&g.coll);
    em_bgm_shutdown();  /* blocks out the audio thread, then frees + prints */
    em_sfx_shutdown();  /* AFTER em_bgm_shutdown — the device-teardown
                         * guarantee makes the sample memory freeable */
}

/* ------------------------------------------------------------------ */
/* Scripted-anim mailbox (see em_game.h for the engine mapping)        */
/* ------------------------------------------------------------------ */

static int anim_request_common(unsigned clip_id, float rate, int hold)
{
    if (!g.mesh || clip_id == 0)
        return 0;
    if (em_model_clip_index(&g.model, (uint32_t)clip_id) < 0) {
        printf("anim request: player EMDL carries no clip id %u "
               "(0x%X) — re-export with it in --clips\n", clip_id,
               clip_id);
        return 0;
    }
    g.sa_req      = clip_id;        /* +0x1F2 */
    g.sa_rate     = rate;           /* +0x1F8 */
    g.sa_req_hold = hold;
    return 1;
}

int em_game_anim_request(unsigned clip_id, float rate)
{
    return anim_request_common(clip_id, rate, 0);
}

int em_game_anim_hold(unsigned clip_id, float rate)
{
    return anim_request_common(clip_id, rate, 1);
}

/* FIRE-RECOIL rewind (em_game.h): the native bone_matrix_publish
 * counter re-seed — the committed hold's playhead snaps back to frame
 * 0 (the engine's per-shot `+0x276 = 0` write) and replays the clip's
 * front-loaded recoil at `rate` frames/tick back into the clamped
 * hold. Not yet committed (or a different clip): plain hold request. */
int em_game_anim_hold_restart(unsigned clip_id, float rate)
{
    if (g.sa_cur == clip_id && g.sa_clip >= 0) {
        g.sa_t        = 0.0;        /* fire counter reset (+0x276 = 0) */
        g.sa_rate     = rate;
        g.sa_hold     = 1;
        g.sa_req      = clip_id;    /* keep request == commit: no re-init */
        g.sa_req_hold = 1;
        return 1;
    }
    return anim_request_common(clip_id, rate, 1);
}

int em_game_anim_frames(unsigned clip_id)
{
    int ci;
    if (!g.mesh || clip_id == 0)
        return 0;
    ci = em_model_clip_index(&g.model, (uint32_t)clip_id);
    return ci < 0 ? 0 : (int)g.model.clips[ci].frame_count;
}

void em_game_anim_cancel(void)
{
    g.sa_req      = 0;              /* op 0x18 teardown: +0x1F2 = 0 */
    g.sa_req_hold = 0;
}

unsigned em_game_anim_active(void)
{
    return g.sa_cur;                /* +0x20C */
}

int em_game_anim_frame(void)
{
    if (!g.sa_cur || g.sa_clip < 0)
        return -1;
    double end = (double)(g.model.clips[g.sa_clip].frame_count - 1);
    return (int)(g.sa_t < end ? g.sa_t : end);
}

/* Manual aim steer state (em_game.h) */
float em_game_aim_pitch(void)     { return g.aim_pitch; }
float em_game_aim_yaw_blend(void) { return g.aim_yawb; }
void  em_game_aim_dir(float out[3]) { aim_dir_get(out); }
