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
 * desired-eye solver running func_0018D7B0-style segment queries
 * (mask 6) against the same world; the player has NO free camera
 * control — R1/L1 orient the camera behind the player, idle
 * auto-orients slowly, and walls make the camera RISE (the CAMERA
 * FIDELITY block below). While the status screen is open the world
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
#include "game/em_frame.h"
#include "game/em_hud.h"
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
 * ANALOG GAIT: free movement now runs the engine's REAL stick
 * quantizer + speed table (the s31 decode, constants below): the raw
 * stick magnitude picks gait 1/2/3 = turn-in-place/walk/run at
 * 0/6/18 u/s. Keyboard full push = RUN (matching the PCSX2 keyboard
 * feel); the input layer's modifier vector-magnitude caps land each
 * hold in ITS ring in every stick direction — Cmd 0.8 (raw ~102) =
 * the WALK band, Option 0.5 (raw 64) = the gait-1 TURN/creep band
 * (em_input.h GAIT HOLD TIERS). WALK_SPEED stays as the door-transit
 * scripted MOVE-TO speed only (em_door's walk, not stick locomotion —
 * the historical port constant keeps the transit timings). */
#define FRAME_DT        (1.0f / 60.0f)
#define WALK_SPEED      15.0f   /* units/sec — scripted door MOVE-TO only */
#define GAIT_RING_1     48.0f   /* func_001B5CC0 rings, raw stick units */
#define GAIT_RING_2     88.0f
#define GAIT_RING_3     122.0f
#define GAIT_WALK_SPEED (0.1f * 60.0f)  /* D_00248870[1] u/tick @ 60 Hz */
#define GAIT_RUN_SPEED  (0.3f * 60.0f)  /* D_00248870[2] */
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
 * MAPPING"). The AUTHENTIC stick-locomotion ids, decoded 2026-06-10
 * from the boot ELF's selection chain (replaces the s10-era stride-scan
 * guess of clips 2/3):
 *
 *   stick deflection -> func_001B5CC0 quantizer (rings r=48/88/122) ->
 *   gait byte 0..3 (pad struct +0x17 = 0x810E57) -> player +0x23F ->
 *   locomotion top func_001612D0: locIdx = gait-1, speed =
 *   D_00248870[locIdx] = {0.0, 0.1, 0.3, 0.8} u/tick, anim id =
 *   D_00248AB0[mode 1][family*4 + locIdx] (func_0017B490/func_0017B460;
 *   unarmed family 0 row = {0, 1, 2, 3}).
 *
 * So gait 1 = turn-in-place (id 0, speed 0), gait 2 = WALK (id 1,
 * 0.1 u/tick = 6 u/s), gait 3 (full stick) = RUN (id 2, 0.3 u/tick).
 * The quantizer never returns 4, so id 3 (0.8 u/tick row) is a sprint
 * data slot the stick cannot reach — not shipped. The walk/run clips
 * are baked IN PLACE; their natural ground speeds at 60 fps are
 * 6.11 / 24.07 u/s (exporter-printed; the engine's own walk 0.1 u/tick
 * = 6.0 u/s at 60 Hz cross-checks it), so playback rate =
 * move_speed / that keeps the feet tracking the ground (stride lock).
 * Idle<->locomotion is a 0.15 s LINEAR palette blend — the engine
 * cross-fades clip transitions the same way (PROGRESS.md: mid-blend
 * live captures match no single clip).
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
#define CLIP_ID_WALK    1u      /* engine walk (was library clip 2) */
#define CLIP_ID_RUN     2u      /* engine run (gait 3 = full stick) */
#define WALK_CLIP_SPEED 6.11f   /* units/sec at the baked 60 fps */
#define RUN_CLIP_SPEED  24.07f  /* run clip natural speed (45 fr) */
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
 * engine's y-down GS view) puts the engine's +2.4 view-down at
 * y = -2.4, and the engine's +x maps screen-LEFT (the remap's X
 * negation) — the model lands ON the ring gauge, the real screen's
 * placement. The UI projection is PINNED by the x-anchor: 7.4 units
 * at z 40 = the ring-center column (canvas 208) iff tan(fovy/2) =
 * 0.74 at the GS 4:3 frame (UI_PROJ_TANY; see ui_scene_render — the
 * projection stays 4:3-locked and stretches with the window exactly
 * like the overlay canvas). The additive engine tint is approximated
 * multiplicatively over the GS 128 base: rgb_mul = (128 + delta)/128.
 * The rig is orbited to the gfx stand-in light's azimuth (relative
 * camera<->player transform unchanged) so the camera-facing side is
 * lit — also explained at ui_scene_render. */
#define UI_SCENE_X      7.4f    /* engine view-space right offset */
#define UI_SCENE_Y     -2.4f    /* engine +2.4 view-down, native y-up */
#define UI_SCENE_Z     40.0f    /* engine view-space distance */
#define UI_SPIN_RATE    0.01f   /* yaw rad per frame (engine 0x3C23D70A) */
#define UI_RAMP_RATE    0.01f   /* tint pulse step per frame */
#define UI_RAMP_MAX     1.3f    /* tint pulse ceiling (0x3FA66666) */
#define UI_LOW_HEALTH  35.0f    /* clip-select threshold (0x420C0000) */
/* The UI rig's orbit azimuth = the gfx stand-in light's horizontal
 * direction, normalized ((0.4, 0.45)/0.602 — see the camera note in
 * ui_scene_render). */
#define UI_CAM_DIR_X    0.6644f
#define UI_CAM_DIR_Z    0.7474f
/* tan(fovy/2) of the engine's UI projection at its 4:3 frame — pinned
 * by the x-anchor consistency: view-space x 7.4 at z 40 projects to
 * the ring-gauge center column (canvas 208 of 512, NDC -0.1875), so
 * fx = 0.1875*40/7.4 = 1.01351 and tan_y = 1/(fx*4/3) = 0.74. */
#define UI_PROJ_TANY    0.74f
#define CLIP_ID_MENU     450u   /* 0x1C2 — single-frame menu stance */
#define CLIP_ID_MENU_LOW 10u    /* 0xA — low-health menu idle (90 fr) */

/* FOOTSTEP TRIGGER FRAMES — the engine's per-anim-id property table
 * D_00248C90 (FINDINGS "ANIM ID MAPPING": frameA/frameB per row; the
 * per-frame func_00187350 fires the step sound + decal when the
 * committed clip time crosses them). Rows re-read from the user's
 * local boot ELF for the authentic ids: walk id 1 (120 fr) -> 72/21 —
 * the exact pair the s29 live capture metered while stick-walking;
 * run id 2 (45 fr) -> 26/3. Each trigger plays the two-layer step —
 * surface variant (material block + gait sub-base) + gear/cloth
 * variant, each with its own rand5 draw (footstep_play below). */
#define WALK_STEP_FRAME_A 72.0f /* D_00248C90[1].frameA */
#define WALK_STEP_FRAME_B 21.0f /* D_00248C90[1].frameB */
#define RUN_STEP_FRAME_A  26.0f /* D_00248C90[2].frameA — for the run
                                 * clip when locomotion drives it */
#define RUN_STEP_FRAME_B  3.0f  /* D_00248C90[2].frameB */
#define ANIM_BLEND_TIME 0.15f   /* seconds, idle<->walk crossfade */
#define STICK_DEADZONE  0.25f
#define EM_PI           3.14159265f

/* Camera values — the AUTHENTIC engine numbers (FINDINGS.md "CAMERA
 * SYSTEM", live-verified in save states 01/03): eye ~33 u behind the
 * player along the camera yaw and ~19 u above the player's ground Y;
 * the generic follow seeks the target to player.y + 15 (the live
 * area-0x1100 director measured ground + 17; the documented smooth-table
 * follow constant is 15.0). Chase caps are the engine's: 0.8 u/frame for
 * the target follow, 4.0 u/frame for the eye solver (style 0). */
#define CAM_DIST        33.0f   /* desired eye distance behind the player */
#define CAM_EYE_HEIGHT  19.0f   /* desired eye height above player ground Y */
#define CAM_TGT_HEIGHT  15.0f   /* target height above player Y (follow) */
#define CAM_AIM_OFFSET  6.0f    /* struct +0x8C default — target height in
                                   player aim state 5 (TODO: player states) */
#define CAM_TGT_CAP     0.8f    /* target chase rate cap, units/frame */
#define CAM_EYE_CAP     4.0f    /* eye chase rate cap, units/frame */
#define CAM_NEAR_PUSH   4.0f    /* commit: view position = eye + 4*fwd */

/* CAMERA FIDELITY (2026-06-11, observed-behavior notes against the
 * real game — the original gives the player NO free camera control;
 * the old d-pad orbit was a port invention and is REMOVED):
 *
 *  - WALL RISE: when a wall blocks the desired eye, the engine does
 *    NOT pull the camera in toward the player — the camera RISES
 *    (you see the top of the player) until the sight line clears,
 *    and returns to its height when there is room again. NOT applied
 *    while aiming (the aim camera keeps the close solve). The PS2
 *    implementation lives in the unread 6984-byte solver
 *    func_0018DD20 (FINDINGS "CAMERA SYSTEM", confidence medium);
 *    the step/ceiling constants below are PORT CONSTANTS (flagged).
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
 *    AREA02 (office, both subs) and AREA01 sub 0 define NO fixed
 *    cameras — all chase (the AREA02 overlay never touches the
 *    camera; AREA01's only touch is the drawbridge-cutscene target
 *    retarget). AREA06 (snow) has ONE real region: X[-370,-340] x
 *    Z[-620,-600], y gate 60, fixed eye (-367.7, 90, -598.9) —
 *    exported as scene_snow's `camregion` line. The port machinery
 *    is live: scene.txt `camregion x0 z0 x1 z1 ygate ex ey ez`
 *    lines drive camera_mode_dispatch's in-region branch (fixed
 *    eye, L1/auto-orient ignored, R1 aim still runs, release snaps
 *    back INSTANTLY — the observed behavior: "letting go INSTANTLY
 *    snaps back to the room's setting"). EM_CAMREGION_TEST=1 proves
 *    it with a flagged SYNTHETIC office region (the office has no
 *    real ones). Engine refinement noted: the snow case approaches
 *    its spec at 0.7 u/frame via the chase primitives; the port
 *    uses hard placement ("chase disabled") per the observed snap. */
#define CAM_REGION_YGATE 4.0f   /* func_00194D10's |player.y - rec.y|
                                   region gate, engine constant */
#define CAM_RISE_STEP   2.0f    /* rise search step, units (PORT) */
#define CAM_RISE_MAX    40.0f   /* rise search ceiling above the default
                                   eye height, units (PORT) */
#define CAM_L1_RATE     0.0349f /* rad/FRAME — the L1 orient-behind seek:
                                   the engine's orient-to-heading family
                                   rate (func_001921D0 states 7/0x2C/0x2D,
                                   float 0x3D0EFA35 = 2 deg/frame) */

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
 * func_0018CBD0, and the locked-look native func_001BBBF0):
 *   OPEN transit (script D_0024DE40 record 2, op 0x0D sub 5): a HARD
 *     CUT (solver style 1 = copy desired -> actual): TARGET = player
 *     pos + (19 + 6) up, EYE = player + rotY(cam euler)*(0, 0, -20)
 *     with EYE.y = player.y + 19 + 6 + 2 = +27 (f4 + f5 = 8 in BOTH
 *     per-area parameter sets), then cam+0xA0 = 0x78: the actual
 *     TARGET re-blends toward the desired one at <= 1.0 u/frame for
 *     120 frames (func_001916C0's tail) — the cinematic angle that
 *     "teleports behind the player" and then pans as he walks through.
 *   LOCKED try (script D_0024DEC0 record 2, op 0x09 -> func_001BBBF0):
 *     TARGET = door pos + 8 u toward the HANDLE side (the door-yaw
 *     left: (-8*cos(dyaw), +10, +8*sin(dyaw))) and EYE = TARGET -
 *     13*(sin,cos)(camera yaw D_00810374) with EYE.y = door.y + 12 —
 *     the camera parked at the handle while the try animation plays;
 *     the finish script (op 0x07 sub 4) restores the saved camera. */
#define DOORCAM_EYE_BACK   20.0f  /* op 0x0D sub 5 chase dist (-20.0) */
#define DOORCAM_EYE_UP     27.0f  /* 19 + f4 + f5 (= 6+2 or 2+6) */
#define DOORCAM_TGT_UP     25.0f  /* 19 + f4 (6, the -46.8 param set) */
#define DOORCAM_TGT_SOFT   120    /* cam+0xA0 = 0x78 target re-blend */
#define LOCKCAM_HANDLE_OFF 8.0f   /* func_001BBBF0 door-left offset */
#define LOCKCAM_TGT_UP     10.0f
#define LOCKCAM_EYE_BACK   13.0f
#define LOCKCAM_EYE_UP     12.0f  /* door.y + 12 */

/* Engine projection (FINDINGS.md "CAMERA SYSTEM" section 3) — recorded
 * for eventual native adoption; rendering still goes through
 * em_mat4_perspective below. TODO(projection): adopt the s = 480 zoom
 * model. P rows: (0.8s,0,0,0) (0,0.5s,0,0) (2048,2048,0.8996,1)
 * (0,0,1677721.5,0) -> screen x = 0.8s*x/z + 2048, y = 0.5s*y/z + 2048
 * (GS center 2048, screen y down), z = 0.8996*z + 1677721.5 (24-bit GS
 * Z), w_clip = z_view. Native remap: tan(half-hfov) = half_w_gs/(0.8s).
 * Zoom is fixed 480 except the scope camera (s = 224/x, func_001D25F0)
 * and scripted zoom lerps (func_001D2590). */
#define ENGINE_CAM_ZOOM_S      480.0f      /* render-ctx +0x2468 default */
#define ENGINE_CAM_PROJ_ZSCALE 0.8996f     /* GS-Z row scale */
#define ENGINE_CAM_PROJ_ZOFFS  1677721.5f  /* GS-Z row offset */
#define ENGINE_CAM_GS_CENTER   2048.0f     /* GS screen-center offset */

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
    float    eye_des[3];  /* +0x10: desired EYE (world) */
    float    tgt_des[3];  /* +0x20: desired TARGET (world) */
    float    yaw;         /* +0x44: eye->target heading; the R1/L1
                             orient and the idle auto-orient steer it */
    float    rise;        /* PORT: this frame's wall-rise height above
                             the default desired eye (0 = at height;
                             gates the idle auto-orient) */
    float    aim_h;       /* +0x8C: target height offset above player Y
                             (func_00191390's per-state table; default 6.0
                             — kept for the generic follow; the aim camera
                             proper is MODE 1 below) */
    uint8_t  aim_phase;   /* MODE-1 sub-machine (+0x01 in mode 1):
                             0 = off, 1 = entry blend (func_00197740),
                             2 = steady aim (func_00197870) */
    float    aim_entry[3];/* spad D_70003040: player pos saved at aim
                             entry (the R2 stance's target base) */
    /* IDLE AUTO-ORIENT orbit (director sub-state 2, func_00193D90) */
    uint8_t  orbit_on;    /* orbit running (cam+0x01 == 2) */
    float    orbit_tgt;   /* +0x48: saved player heading (goal yaw) */
    float    orbit_rad;   /* +0x4C: |horiz eye<->target| at arm time */
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
    int        clip_run;         /* -1 = no run clip */
    int        loco_clip;        /* the ACTIVE locomotion clip index
                                  * (walk or run, by gait) */
    float      loco_speed;       /* its natural ground speed, u/s */
    double     walk_t;           /* locomotion clip time, s (rate-scaled) */
    float      walk_w;           /* locomotion blend weight 0..1 */
    double     step_prev;        /* last frame's loco-cycle position in
                                  * clip FRAMES (footstep edge detect) */
    int        gait;             /* this frame's stick gait 0..3
                                  * (func_001B5CC0 quantizer) */
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
                                  * 2 = cinematic placed (cut done) */
    float      door_yaw[EM_DOOR_MAX]; /* manifest door yaws (the locked-
                                  * look handle-side math needs them) */
    int        n_door_yaw;

    /* player status (the engine globals em_hud.h documents; static demo
     * values until the weapon/health systems are translated) */
    EmPlayerStatus status;

    /* the camera block the recorded chain consumes (native K = P*V) */
    float      viewproj[16];

    /* render chain (this frame's recorded draws) */
    ChainDraw  chain[SCENE_MAX + 1 + EM_DOOR_MAX + EM_ENEMY_MAX];
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
    int         capture_rise;    /* EM_CAPTURE_RISE=1 — hold 's' (walk at
                                  * the camera) so the capture shows the
                                  * wall-rise camera (see gameplay_frame) */
    int         capture_orient;  /* EM_CAPTURE_ORIENT=1 — turn-in-place,
                                  * then idle: the slow auto-orient demo */
    int         move_test;
    int         move_legs[2];    /* EM_MOVE_LEGS=fwd,strafe frame counts */
    int         move_expect_set; /* EM_MOVE_EXPECT=x,y,z final-pos override */
    float       move_expect[3];
    int         transit_test;    /* EM_TRANSIT_TEST=1 — scene-switch test */
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
    float       et_wd0;          /* crate run: worm distance at burst */
    float       et_wd_min;       /* crate run: min worm distance since */
    int         et_worm_atk;     /* crate run: worm reached ATTACK */
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
 *                             one placed CRAWLER (the func_001551B0
 *                             placement records); owned by em_enemy.c.
 *   enemy crate <x> <y> <z> <yaw>
 *                             one DISGUISED CRATE (the crawler's IDLE
 *                             disguise as its own kind — em_enemy.h
 *                             "CRATE KIND"): idles as the office crate
 *                             mesh, bursts into gibs + a crawler on a
 *                             bullet hit or ~10-u player proximity.
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
    g.n_camregion  = 0;     /* camera regions are per-scene data */

    char mf[256 + 16];
    snprintf(mf, sizeof mf, "%s/scene.txt", g.scene_dir);
    FILE *f = fopen(mf, "r");
    if (!f) return;

    char line[512], name[256], gname[64];
    float x, y, z, yaw, r, gx, gy, gz, gyaw;
    int gn, gk, gl;
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
        } else if (sscanf(line, "door %255s %f %f %f %f %f", name,
                          &x, &y, &z, &yaw, &r) == 6) {
            /* Interactive door instance (em_door.c). The manifest is
             * parsed inside the boot task, so the gfx device exists. */
            float p[3] = { x, y, z };
            if (em_door_add(em_frame_gfx(), g.scene_dir, name, p, yaw, r)) {
                printf("manifest: door line failed to load: %s", line);
            } else if (((g.n_door_yaw < EM_DOOR_MAX
                             ? (void)(g.door_yaw[g.n_door_yaw++] = yaw)
                             : (void)0),   /* yaw for the locked-look
                                            * handle-side math */
                        sscanf(line,
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
                                                    : -1;
            if (kind < 0) {
                printf("manifest: unknown enemy kind, skipped: %s", line);
            } else {
                float p[3] = { x, y, z };
                if (em_enemy_add_kind(em_frame_gfx(), kind, p, yaw) < 0)
                    printf("manifest: enemy line failed to load: %s", line);
            }
        }
    }
    fclose(f);
    printf("manifest: %s — spawn (%.3f, %.3f, %.3f) yaw %.4f, "
           "collision %s%s%s, %d door(s), %d enem%s\n", mf,
           g.spawn[0], g.spawn[1], g.spawn[2], g.spawn_yaw, g.coll_path,
           g.bgm_file[0] ? ", bgm " : "", g.bgm_file, em_door_count(),
           em_enemy_count(), em_enemy_count() == 1 ? "y" : "ies");
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
    g.n_door_yaw = 0;           /* re-recorded by the new manifest */
    em_enemy_shutdown(gfx);
    em_enemy_reset();
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
            /* Drive the locomotion clip at the engine's commanded gait:
             * the walk-out plays the locIdx-2 clip (family 0 -> id 2 =
             * RUN) even during the in-place phase. */
            g.move_speed = wspeed > 0.0f ? wspeed : GAIT_RUN_SPEED;
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
        return;
    }

    /* KNIFE / MELEE plant: the engine's melee modes 0x21/0x22 replace
     * the locomotion modes outright (em_weapon.h "KNIFE / MELEE") —
     * the player stands for the swing + recover. The heavy's in-swing
     * yaw steer (func_00173DD0, D_002486F0 rates) is untranslated
     * (flagged in em_weapon.h), so no turn-in-place here either. */
    if (em_weapon_is_melee()) {
        g.move_speed = 0.0f;
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
    if (gait == 0) {                  /* dead ring: idle */
        player_wall_probes();         /* the idle top probes too */
        return;
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

    /* D_00248870[gait-1]: gait 1 = TURN-IN-PLACE (speed 0 — the facing
     * seeks the stick heading below, the breathing-idle clip plays),
     * gait 2 = WALK 6 u/s, gait 3 = RUN 18 u/s. */
    g.move_speed = gait == 3 ? GAIT_RUN_SPEED
                 : gait == 2 ? GAIT_WALK_SPEED : 0.0f;

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
 * material (footstep_block below) + the gait sub-base (a1==3 -> +0xA,
 * a1==2 -> +5, else +0; a1 = actor +0x25C, the gait byte) + rand5 =
 * func_00179B90 = (rand() & 7) with 5..7 folded to 0..2 (0..4, the low
 * three values twice as likely). This REPLACES the s29-era port guess
 * (fixed pairs 0x15/0x16 + 0x139/0x13A, both alternating L/R): the s29
 * "floor A vs floor B" capture was the SAME material at walk vs run
 * gait (block 0x10 + 5 -> 0x15.. walk, + 0xA -> 0x1A.. run), and the
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
 * table; stride 0x11 = 17 ids per material: 3 gait sub-bases x 5
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

/* One footstep at `gait` (the engine mapper's a1; the locomotion paths
 * pass actor +0x25C, the melee impact gates a scripted 1..3).
 * EM_STEP_TRACE=1 prints each step's resolved attr/ids (debug). */
static void footstep_play(int gait)
{
    uint8_t  attr = footstep_floor_attr();
    unsigned sub  = gait == 3 ? 0xAu : gait == 2 ? 5u : 0u;
    unsigned surf = footstep_block(attr) + sub + footstep_rand5();
    unsigned gear = EM_SFX_STEP_GEAR_BASE + footstep_rand5();
    static int trace = -1;
    if (trace < 0) trace = getenv("EM_STEP_TRACE") != NULL;
    if (trace)
        printf("step: gait %d attr 0x%02X -> surface 0x%03X gear 0x%03X\n",
               gait, attr, surf, gear);
    em_sfx_play(surf);
    em_sfx_play(gear);
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
    player_move();
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

    /* Locomotion clip by GAIT (the mode-1 id row {0,1,2,3}): walk for
     * gait 2, run for gait 3 (falling back to walk when the asset has
     * no run clip). On a clip swap the cycle restarts — the engine
     * re-inits the clip on an id change (anim_clip_init). The DOOR
     * TRANSIT scripted MOVE-TO (move_speed = WALK_SPEED, gait 0)
     * keeps the walk clip, the historical behavior. */
    int loco = g.clip_walk;
    if (g.gait == 3 && g.clip_run >= 0)
        loco = g.clip_run;
    if (loco != g.loco_clip) {
        g.loco_clip  = loco;
        g.loco_speed = (loco >= 0 && loco == g.clip_run) ? RUN_CLIP_SPEED
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
         * (walk id 1: 72/21; run id 2: 26/3). The playhead only
         * advances while moving (rate-scaled to the ground speed), so
         * standing is silent and slower walks space their steps out,
         * exactly like the engine's clip-time test. */
        int run = g.loco_clip == g.clip_run && g.clip_run >= 0;
        double fa = run ? RUN_STEP_FRAME_A : WALK_STEP_FRAME_A;
        double fb = run ? RUN_STEP_FRAME_B : WALK_STEP_FRAME_B;
        const EmModelClip *cw = &g.model.clips[g.loco_clip];
        double cyc = fmod(g.walk_t * (double)cw->fps,
                          (double)cw->frame_count);
        if (step_crossed(g.step_prev, cyc, fa) ||
            step_crossed(g.step_prev, cyc, fb))
            footstep_play(run ? 3 : 2);  /* gait sub-base: walk +5,
                                          * run +0xA (func_00182430) */
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

/* Desired eye from the struct yaw: CAM_DIST behind the player along the
 * yaw heading, CAM_EYE_HEIGHT above the player's ground Y (the live
 * values: ~33 u back, ~19 u up). */
static void camera_desired_eye(EmCamera *cam)
{
    cam->eye_des[0] = g.pos[0] - sinf(cam->yaw) * CAM_DIST;
    cam->eye_des[1] = g.pos[1] + CAM_EYE_HEIGHT;
    cam->eye_des[2] = g.pos[2] - cosf(cam->yaw) * CAM_DIST;
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

    /* L1: one-shot orient behind the player, at the engine's orient-
     * to-heading family rate (2 deg/frame — func_001921D0's 0x3D0EFA35
     * states; which player state L1 routes through is unpinned, the
     * rate family is). R1 no longer seeks here — the aim camera above
     * owns the armed stance. */
    if (in->pressed & EM_PAD_L1)
        g.cam_recenter = 1;
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
        if (cam->hit)                       /* engine: +0x07 & 9 */
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

    /* Mode 0 generic follow: desired target chases the player on x/z at
     * <= 0.8 u/frame; y seeks player.y + 15.0. (The old armed-stance
     * +0x8C target-height swap is RETIRED — the aim camera is the real
     * MODE 1 above.) Settles to exactly player x/z when idle, matching
     * the live capture. */
    cam->tgt_des[0] = cam_chase_h(cam->tgt_des[0], g.pos[0], CAM_TGT_CAP);
    cam->tgt_des[2] = cam_chase_h(cam->tgt_des[2], g.pos[2], CAM_TGT_CAP);
    cam->tgt_des[1] = cam_chase_v(cam->tgt_des[1],
                                  g.pos[1] + CAM_TGT_HEIGHT,
                                  CAM_TGT_CAP);

    /* AUTO-ORIENT ORBIT eye (func_00193D90): the desired eye circles
     * the target at the radius saved when the orbit armed. */
    if (cam->orbit_on && !g.cam_region_on) {
        cam->eye_des[0] = cam->tgt_des[0] - sinf(cam->yaw) * cam->orbit_rad;
        cam->eye_des[1] = g.pos[1] + CAM_EYE_HEIGHT;
        cam->eye_des[2] = cam->tgt_des[2] - cosf(cam->yaw) * cam->orbit_rad;
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
    } else {
        camera_desired_eye(cam);
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

/* Eye pull-in margin: how far in front of the hit plane the solved eye
 * sits, along the blocked sight line. The PS2 solver's exact inset is
 * inside func_0018DD20 (6984 B, unread — FINDINGS confidence "medium");
 * the commit's view position adds another 4.0 * forward away from the
 * wall (CAM_NEAR_PUSH), so a small margin suffices. */
#define CAM_WALL_MARGIN 0.5f

/* func_0018D7B0 (style 0) — the desired-eye solver. Collision-resolves
 * the desired eye against the world: segment queries (the func_0019A910
 * hub — same walkers/eps as the documented func_0019A570 family) from
 * the look target toward the desired eye over collision-set mask 6
 * (static cells + grid; 7 would add movable hulls, which the port has
 * none of yet). The result byte lands in struct +0x07 (cam->hit).
 *
 * WALL RESPONSE (the CAMERA FIDELITY block above): when the sight line
 * is blocked the real game's camera RISES — the desired eye keeps its
 * x/z and climbs (CAM_RISE_STEP search) until the look line clears
 * over the wall, descending again once the default height has room;
 * you briefly look down at the top of the player. The old pull-in
 * (eye dragged in front of the hit) remains in exactly two roles: the
 * AIM camera (the rise is not applied while aiming) and the fallback
 * when no rise inside CAM_RISE_MAX clears (full-height walls). Then
 * the actual eye (D_008105D0) smooth-chases the solved desired eye
 * per axis, capped at 4.0 u/frame — the rise/descent inherit the
 * engine's own smoothing; the actual target is a straight copy of
 * the desired target (func_0018C0C0). */
static void camera_solve(EmCamera *cam)
{
    float eye_des[3] = { cam->eye_des[0], cam->eye_des[1], cam->eye_des[2] };

    cam->hit  = 0;
    cam->rise = 0.0f;

    /* FIXED-CAMERA REGION: the room spec is authoritative — no wall
     * solve (the designers placed the eye), CHASE DISABLED: the actual
     * eye is a hard copy of the spec. An R1-release frame lands here
     * with the spec already re-pinned by the dispatch, so the snap-back
     * is INSTANT (one frame, no lerp) — the observed behavior. */
    if (g.cam_region_on) {
        cam->eye[0] = eye_des[0];
        cam->eye[1] = eye_des[1];
        cam->eye[2] = eye_des[2];
        cam->tgt[0] = cam->tgt_des[0];
        cam->tgt[1] = cam->tgt_des[1];
        cam->tgt[2] = cam->tgt_des[2];
        return;
    }
    if (g.coll.poly_count) {
        EmCollHit hit;
        int kind = em_collision_segment_query(
            &g.coll, cam->tgt_des, eye_des,
            EM_COLL_SET_CELLS | EM_COLL_SET_GRID,  /* solver mask 6 */
            EM_COLL_ID_NONE, &hit);
        if (kind) {
            cam->hit = (uint8_t)kind;              /* struct +0x07 */
            int risen = 0;
            if (!cam->aim_phase) {     /* the aim camera keeps the
                                        * close pull-in solve (engine
                                        * mode-1 style 2) */
                /* RISE: lowest clear height above the default eye. */
                for (float up = CAM_RISE_STEP; up <= CAM_RISE_MAX;
                     up += CAM_RISE_STEP) {
                    float try_eye[3] = { eye_des[0], eye_des[1] + up,
                                         eye_des[2] };
                    if (!em_collision_segment_query(
                            &g.coll, cam->tgt_des, try_eye,
                            EM_COLL_SET_CELLS | EM_COLL_SET_GRID,
                            EM_COLL_ID_NONE, &hit)) {
                        eye_des[1] += up;
                        cam->rise   = up;
                        risen       = 1;
                        break;
                    }
                }
                if (!risen) {
                    /* re-stage the original hit for the pull-in */
                    em_collision_segment_query(
                        &g.coll, cam->tgt_des, eye_des,
                        EM_COLL_SET_CELLS | EM_COLL_SET_GRID,
                        EM_COLL_ID_NONE, &hit);
                }
            }
            if (!risen) {
                /* Pull the eye in front of the wall, back toward the
                 * target along the blocked sight line (aim camera /
                 * no-clearance fallback). */
                float dx = cam->tgt_des[0] - hit.point[0];
                float dy = cam->tgt_des[1] - hit.point[1];
                float dz = cam->tgt_des[2] - hit.point[2];
                float dl = sqrtf(dx * dx + dy * dy + dz * dz);
                if (dl > 1e-3f) {
                    float s = CAM_WALL_MARGIN / dl;
                    if (s > 1.0f) s = 1.0f;
                    dx *= s; dy *= s; dz *= s;
                } else {
                    dx = dy = dz = 0.0f;
                }
                eye_des[0] = hit.point[0] + dx;
                eye_des[1] = hit.point[1] + dy;
                eye_des[2] = hit.point[2] + dz;
            }
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

    /* EM_CAMERA_TRACE=1 — wall-solve introspection (debug). */
    static int trace = -1;
    if (trace < 0) trace = getenv("EM_CAMERA_TRACE") != NULL;
    if (trace && cam->hit)
        printf("camera: frame %d blocked -> %s %.1f (eye y %.2f -> des "
               "%.2f)\n", g.frame_no,
               cam->rise > 0.0f ? "RISE" : "pull-in", cam->rise,
               cam->eye[1], eye_des[1]);
}

/* func_0018C0D0(cam, 1) — the per-frame COMMIT. Engine steps:
 *   1. fwd = normalize(target - eye), degenerate-guarded;
 *   2. view position = eye + 4.0*fwd (near push; mode 0xA uses -1.0);
 *   3. func_00102CD0 look-at with up = D_008105F0 = (0,-1,0) — see
 *      em_mat4_lookat_gs for the Y-down/handedness reconciliation;
 *   4. P from zoom s, K = P*V -> every draw's matrix slot 0 (M = K*W).
 * Step 4's projection here is still the port's em_mat4_perspective; the
 * engine's GS values are pinned in the ENGINE_CAM_* constants above. */
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

    int dw, dh;
    em_window_drawable_size(em_frame_window(), &dw, &dh);
    float aspect   = (dh > 0) ? (float)dw / (float)dh : 4.0f / 3.0f;
    float far_clip = g.n_scene ? 800.0f : 500.0f;

    float proj[16];
    em_mat4_perspective(proj, 50.0f * EM_PI / 180.0f, aspect,
                        0.5f, far_clip);
    em_mat4_mul(g.viewproj, proj, cam->view);
}

/* DOOR-TRANSIT CINEMATIC CAMERA — DECODED (the "DOOR CAMERA CUES"
 * constants block above). Replaces the old full camera freeze during
 * the door input lock: once the walk-to arrives and the door script
 * begins, the OPEN script's op 0x0D sub 5 cue fires — a HARD CUT to
 * 20 u behind the player along the current camera heading at +27,
 * looking at the player (+25) — and the camera then HOLDS that eye
 * while the actual target re-blends toward the walking player at
 * <= 1.0 u/frame (the cam+0xA0 = 120 window): the cinematic angle the
 * door walk-through plays under. EM_DOORCAM_LOCKED=1 swaps in the
 * LOCKED-TRY placement (func_001BBBF0: target at the door HANDLE — 8 u
 * to the door's left, +10 — eye 13 u back along the camera heading at
 * door.y + 12) as a visual preview: the real trigger is the locked
 * sequence (door subs 1/2), which em_door does not run yet. */
static void camera_door_cinematic(EmCamera *cam)
{
    float tt[3], tyaw;
    if (g.doorcam == 3)             /* post-warp (script ended, op 0x18
                                     * restored the camera): hold the
                                     * re-seated chase placement until
                                     * the fade-in unlocks */
        return;
    if (g.doorcam < 2) {
        if (em_door_transit_active(tt, &tyaw)) {
            g.doorcam = 1;          /* approach walk: hold the chase
                                     * camera still (commit only) */
            return;
        }
        /* walk-to arrived — the door script starts THIS frame: cut. */
        static int locked_prev = -1;
        if (locked_prev < 0)
            locked_prev = getenv("EM_DOORCAM_LOCKED") != NULL;
        if (locked_prev) {
            /* func_001BBBF0 — locked-look (preview wiring, above) */
            int   di = -1;
            float best = 1e30f, dp[3] = { 0, 0, 0 };
            for (int i = 0; i < em_door_count(); i++) {
                float p[3];
                em_door_pos(i, p);
                float dx = p[0] - g.pos[0], dz = p[2] - g.pos[2];
                if (dx * dx + dz * dz < best) {
                    best = dx * dx + dz * dz;
                    di   = i;
                    memcpy(dp, p, sizeof dp);
                }
            }
            float dyaw = (di >= 0 && di < g.n_door_yaw)
                       ? g.door_yaw[di] : g.yaw;
            cam->tgt_des[0] = dp[0] - LOCKCAM_HANDLE_OFF * cosf(dyaw);
            cam->tgt_des[1] = dp[1] + LOCKCAM_TGT_UP;
            cam->tgt_des[2] = dp[2] + LOCKCAM_HANDLE_OFF * sinf(dyaw);
            cam->eye_des[0] = cam->tgt_des[0]
                            - LOCKCAM_EYE_BACK * sinf(cam->yaw);
            cam->eye_des[1] = dp[1] + LOCKCAM_EYE_UP;
            cam->eye_des[2] = cam->tgt_des[2]
                            - LOCKCAM_EYE_BACK * cosf(cam->yaw);
        } else {
            /* op 0x0D sub 5 (func_0018CBD0, dist -20): behind the
             * player along the camera heading, high look-down. */
            cam->tgt_des[0] = g.pos[0];
            cam->tgt_des[1] = g.pos[1] + DOORCAM_TGT_UP;
            cam->tgt_des[2] = g.pos[2];
            cam->eye_des[0] = g.pos[0] - sinf(cam->yaw) * DOORCAM_EYE_BACK;
            cam->eye_des[1] = g.pos[1] + DOORCAM_EYE_UP;
            cam->eye_des[2] = g.pos[2] - cosf(cam->yaw) * DOORCAM_EYE_BACK;
        }
        /* solver style 1 = HARD COPY desired -> actual: the cut. */
        memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
        memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
        cam->tgt_soft = DOORCAM_TGT_SOFT;   /* cam+0xA0 = 0x78 */
        g.doorcam     = 2;
        return;
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
        if (em_door_movement_locked() && g.doorcam != 3) {
            /* pre-warp transit: approach freeze, then the script's
             * cinematic cut + held angle */
            camera_door_cinematic(cam);
        } else {
            /* doorcam 3 = post-warp WALK-OUT: the script has ended
             * (op 0x18 restored the camera at the re-place) — the
             * normal chase resumes behind the re-seated player while
             * the movement lock finishes the walk-out. */
            if (!em_door_movement_locked()) g.doorcam = 0;
            /* func_00191390 leaf pre-step — no native work yet. */
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
    }
    /* UI projection — PINNED by the decoded x-anchor: the engine's
     * 7.4-unit offset at z 40 lands the model exactly on the ring
     * center (canvas x 208, NDC -0.1875) iff the projection's x scale
     * is 0.1875*40/7.4 = 1.01351, i.e. tan(fovy/2) = 0.74 at the GS
     * 4:3 frame (~73 deg vertical). LOCKED at 4:3 regardless of the
     * drawable: the engine renders a 4:3 frame and the whole output
     * stretches to the window — exactly how the overlay's 512x448
     * canvas already maps, keeping the 3D scene and the panel anchors
     * registered at any window size. */
    em_mat4_perspective(proj, 2.0f * atanf(UI_PROJ_TANY), 4.0f / 3.0f,
                        0.5f, 500.0f);
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
            if (cd->tint)         /* per-draw RGBA modulate (ChainDraw) */
                em_gfx_draw_skinned_tinted(gfx, cd->mesh, g.viewproj,
                                           cd->palette, cd->bone_count,
                                           cd->tint);
            else
                em_gfx_draw_skinned(gfx, cd->mesh, g.viewproj,
                                    cd->palette, cd->bone_count);
        }
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
    em_hud_update(em_frame_input());
    g.status.mag     = em_weapon_mag();
    g.status.reserve = em_weapon_reserve();
    em_hud_render(gfx, &g.status);

    /* SCREEN FADE — the step-D machine, drawn last so it covers the
     * scene AND the status screen (the engine's fade owns the whole GS
     * frame). The engine blend is SUBTRACTIVE (out = max(0, pixel -
     * level), GS ALPHA_2 0xA1/FIX 0x80 — decoded 2026-06-11, see
     * em_frame.h): pixels darken UNEVENLY, shadows crushing to black
     * first. The overlay pass only has standard alpha blending, so the
     * stand-in is a black quad with em_frame_fade_alpha() = 1-(1-l)^2
     * (the mean-luminance match; residual gap documented in em_frame.h).
     * Alpha 0 queues nothing: the default frame stays byte-identical. */
    {
        float a = em_frame_fade_alpha();
        if (a > 0.0f) {
            const float black[4] = { 0.0f, 0.0f, 0.0f, a };
            em_gfx_overlay_rect(gfx, 0.0f, 0.0f, EM_GFX_OVERLAY_W,
                                EM_GFX_OVERLAY_H, black);
        }
    }

    if (g.capture_path && g.frame_no == g.capture_frame)
        em_gfx_request_capture(gfx, g.capture_path);

    g.frame_no++;
    /* A scripted self-test owns the quit when combined with a capture,
     * so a mid-script capture doesn't cut the script short. */
    if (g.capture_path && !g.move_test && !g.weapon_test && !g.door_test &&
        !g.transit_test && g.frame_no > g.capture_frame + 1)
        em_frame_request_quit();
}

/* EM_MOVE_TEST=1 — deterministic movement self-test. Injects a scripted
 * key sequence through the real em_input event API (an injected event
 * lands in the NEXT frame's input snapshot, exactly like a real key, so
 * the test exercises the full step-C unpack path): 'w' held for frames
 * 1..60 (walk forward, +Z at cam_yaw 0), 'd' for frames 61..90 (walk
 * screen-right, -X), then assert the final placement and quit.
 *
 * Keyboard full push = GAIT 3 (RUN, 18 u/s = 0.3 u/frame — the engine
 * quantizer; see ANALOG GAIT above), so the forward leg covers
 * 60 * 0.3 = 18 u of motion. The office collision world has a wall
 * n-gon at z = -170 (grid poly, plane n = (0,0,-1), d = 170 — 14 u
 * ahead of the spawn), so with collision loaded the RADIAL WALL PROBES
 * must rest the player at the engine's 4.5-unit standoff: z = -174.5.
 * The right leg then slides free along that wall (motion parallel to
 * the plane; only wall-facing probes push, along their own direction,
 * so no lateral drift):
 *   collision world:  (98.400, 0.000, -174.500), yaw -pi/2
 *   bbox fallback:    (98.400, 0.000, -166.000), yaw -pi/2  (no probes,
 *                     no wall — 18 u of free motion)
 * Those built-in expectations (and the 60/30-frame legs) are the OFFICE
 * scene's; for other scenes (manifest spawns) EM_MOVE_LEGS=fwd,strafe
 * resizes the two legs to reach that scene's wall and EM_MOVE_EXPECT=
 * x,y,z overrides the expected final position (the yaw expectation,
 * -pi/2, is scene-independent). */
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
        float ex = 98.4f, ey = 0.0f;
        float ez = g.coll.poly_count ? -174.5f : -166.0f;
        if (g.move_expect_set) {
            ex = g.move_expect[0];
            ey = g.move_expect[1];
            ez = g.move_expect[2];
        }
        const float tol  = 0.05f;  /* +- slide/fp drift allowance */
        int ok = fabsf(g.pos[0] - ex)           <= tol &&
                 fabsf(g.pos[1] - ey)           <= tol &&
                 fabsf(g.pos[2] - ez)           <= tol &&
                 fabsf(g.yaw + EM_PI * 0.5f)    <= 0.01f;
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
 *   frames  1..24   run -X (full push = gait 3, 0.3 u/frame) to
 *                   x ~= 64.8 (inside the 10 u use-scan radius measured
 *                   from the CENTER; no button — the door must stay
 *                   CLOSED. The engine's class-5 scan has no auto ring)
 *   frames 29..58   keep running -X. The doorway is statically SEALED by
 *                   the grid room-boundary plane at x = 60 (the engine's
 *                   sealed-room-box world): the radial wall probes must
 *                   BLOCK free movement at the player's 4.5-unit radius,
 *                   x ~= 64.5 (tracked as dt_min_x while CLOSED) — the
 *                   "previously blocked plane".
 *   frame   60      CROSS press -> the use scan arms the door (dist to
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
 *   A/A+8/A+16/A+24  engine's default-config fire button, s29) at
 *                    8-frame spacing: each shot decrements mag AND
 *                    reserve (TOTAL-pool rule) -> 4/120 .. 0/116
 *   A+32             5th press on the EMPTY mag: must NOT fire — the
 *                    mode-0 auto-reload triggers instead (func_0017B300:
 *                    mag = min(30, 116) = 30, reserve UNTOUCHED at 116;
 *                    anim 0x33 holds the RELOAD state for its length)
 *   B = A+34+rld+8   press after the reload window: 5th real shot
 *                    -> 29/115
 *   B+10..B+41       FULL-AUTO stretch: mode 2, L held 31 frames ->
 *                    shots at the 6-frame cadence (+2/frame vs interval
 *                    12) = 6 rounds -> 23/109
 *   C = B+50         manual top-up (R = L3, the engine's raw reload
 *                    bit) -> 30/109, reserve again untouched
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
        B = A + 34 + em_weapon_reload_ticks() + 8;
        C = B + 50;
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
    } else if (n == A + 2 || n == A + 10 || n == A + 18 || n == A + 26) {
        move_test_inject('l', 0);
    } else if (n == A + 6) {
        wt_check(em_weapon_mag() == 3 &&
                 em_weapon_reserve() == 119 &&
                 em_weapon_shots() == 1,
                 "shot 1: 4/120 -> 3/119");
        int rs = wt_recoil_state();
        wt_check(rs == 1 || rs == -1,
                 "recoil replay mid-flight after shot 1 (0x112 rewound)");
    } else if (n == A + 8 || n == A + 16 || n == A + 24) {
        move_test_inject('l', 1);                           /* semi 2..4 */
    } else if (n == A + 30) {
        wt_check(em_weapon_mag() == 0 &&
                 em_weapon_reserve() == 116 &&
                 em_weapon_shots() == 4,
                 "shots 2-4: mag empty at 0/116");
    } else if (n == A + 32) {
        move_test_inject('l', 1);                           /* dry press */
    } else if (n == A + 34) {
        move_test_inject('l', 0);
    } else if (n == A + 38) {
        wt_check(em_weapon_state() == EM_WPN_RELOAD &&
                 em_weapon_mag() == 30 &&
                 em_weapon_reserve() == 116 &&
                 em_weapon_shots() == 4 &&
                 em_weapon_reloads() == 1,
                 "empty-mag press auto-reloads: mag = min(30, "
                 "reserve) = 30, reserve untouched (116), no "
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
    } else if (n == B + 10) {
        em_weapon_set_fire_mode(EM_WPN_MODE_AUTO);
        move_test_inject('l', 1);                           /* auto hold */
    } else if (n == B + 41) {
        move_test_inject('l', 0);
    } else if (n == B + 46) {
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

/* EM_ENEMY_TEST — deterministic crawler-vs-player self-tests (em_enemy.c).
 * Both runs spawn ONE crawler 30 units ahead of the player spawn along
 * the spawn facing (scene-init arm, et_spawned) and adapt to the
 * crawler's actual pace (steer pauses vary with the scene's collision
 * world) instead of fixed frame numbers:
 *
 *   EM_ENEMY_TEST=1 (kill run): hold R1 from frame 0 (draw). Assert the
 *     crawler WAKES (the 32-u distance sense covers the 30-u spawn) and
 *     CLOSES distance; once it is within 12 u (inside the office's wall
 *     plane 14 u ahead of the spawn, so the world ray cannot outrank
 *     the victim test) fire ONE semi shot -> +0x36 mailbox, HP 1 =
 *     one-shot kill -> assert death/despawn, exactly one round spent,
 *     ray resolved HIT, and the player's health untouched.
 *   EM_ENEMY_TEST=2 (contact run): weapon stays holstered; let the
 *     crawler reach the player. The radius-6 lunge writes the player
 *     mailbox (0x400A -> amount 10) and the crawler bursts (the
 *     suicide-attack path) -> assert health dropped by exactly 10 and
 *     the crawler despawned.
 *   EM_ENEMY_TEST=3 (crate run): spawn a DISGUISED CRATE 25 units ahead
 *     instead (em_enemy.h "CRATE KIND"); weapon stays holstered, walk
 *     forward (W) at it. Assert the crate BURSTS at the ~10-u proximity
 *     trigger with no shot fired, that the WORM spawned at the crate
 *     position (the burst's gibs occupy virtual draw slots), and that
 *     the worm wakes (ATTACK) and CLOSES distance on the now-standing
 *     player. (The worm may finish its suicide lunge inside the
 *     window — health/alive are reported, not asserted.) */
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
    if (n == 20 && g.enemy_test != 3)   /* the disguised crate stays IDLE */
        et_check(em_enemy_state(0) == EM_ENEMY_ATTACK,
                 "crawler awake (ATTACK) by frame 20");

    if (g.enemy_test == 1) {
        if (!g.et_fired) {
            if (n > 20 && et_dist() <= 12.0f) {
                et_check(et_dist() < g.et_d0 - 5.0f,
                         "closed distance before the shot");
                et_check(em_weapon_state() == EM_WPN_AIM,
                         "rifle drawn (AIM) at fire time");
                move_test_inject('l', 1);   /* CIRCLE — one semi shot */
                g.et_fired = n;
            } else if (n >= 400) {
                et_check(0, "crawler closed to 12 u by frame 400");
                et_finish("kill run");
            }
        } else if (n == g.et_fired + 2) {
            move_test_inject('l', 0);
        } else if (n == g.et_fired + 15) {
            et_check(em_weapon_shots() == 1, "exactly one round fired");
            et_check(em_weapon_last_hit() == 1, "the shot resolved HIT");
            et_check(em_enemy_alive() == 0 &&
                     em_enemy_state(0) == EM_ENEMY_FREE,
                     "crawler dead + despawned (mailbox one-shot kill)");
            et_check(g.status.health == g.et_health0,
                     "player health untouched");
            et_finish("kill run");
        }
    } else if (g.enemy_test == 3) {
        if (!g.et_burst_frame) {
            if (em_enemy_state(0) == EM_ENEMY_FREE) {
                /* the crate burst this frame (slot 0 freed) */
                g.et_burst_frame = n;
                g.et_bd          = et_dist();
                move_test_inject('w', 0);    /* stop: the worm comes */
                et_check(g.et_bd <= 10.5f && g.et_bd >= 8.0f,
                         "burst at the ~10-u proximity trigger");
                et_check(em_weapon_shots() == 0,
                         "no shot fired (proximity, not damage)");
                et_check(em_enemy_alive() == 1 &&
                         em_enemy_kind(1) == EM_ENEMY_KIND_CRAWLER,
                         "worm spawned by the burst");
                float cp[3] = { 0, 0, 0 }, wp[3] = { 0, 0, 0 };
                em_enemy_pos(0, cp);
                em_enemy_pos(1, wp);
                et_check(fabsf(wp[0] - cp[0]) + fabsf(wp[1] - cp[1]) +
                         fabsf(wp[2] - cp[2]) < 0.01f,
                         "worm emerged at the crate position");
                g.et_wd0 = g.et_wd_min = et_dist_i(1);
            } else if (n >= 600) {
                et_check(0, "crate burst by frame 600");
                et_finish("crate run");
            }
        } else {
            float wd = et_dist_i(1);
            if (wd < g.et_wd_min) g.et_wd_min = wd;
            if (em_enemy_state(1) == EM_ENEMY_ATTACK)
                g.et_worm_atk = 1;   /* sampled: it may lunge-burst
                                      * before a fixed checkpoint */
            if (n == g.et_burst_frame + 120) {
                et_check(g.et_worm_atk,
                         "worm woke (ATTACK) after the burst");
                et_check(g.et_wd_min <= g.et_wd0 - 3.0f,
                         "worm closed distance on the player");
                printf("enemy test (crate run): burst dist %.1f, worm "
                       "dist %.1f -> min %.1f, draw slots %d\n",
                       g.et_bd, g.et_wd0, g.et_wd_min, em_enemy_count());
                et_finish("crate run");
            }
        }
    } else {
        if (!g.et_hit_frame) {
            if (g.status.health < g.et_health0) {
                g.et_hit_frame = n;
            } else if (n >= 600) {
                et_check(0, "crawler reached the player by frame 600");
                et_finish("contact run");
            }
        } else if (n == g.et_hit_frame + 5) {
            et_check(g.status.health == g.et_health0 - 10.0f,
                     "lunge dealt the 0x400A amount (10)");
            et_check(em_enemy_alive() == 0,
                     "crawler burst on the lunge (suicide path)");
            et_finish("contact run");
        }
    }
}

/* EM_SFX_TEST=1 — one-shot SFX mixer self-test (em_sfx.c). Fires THREE
 * one-shots 10 frames (1/6 s) apart — weapon draw 0x162, fire 0x164,
 * enemy death 0x7D8, all mapped by the user's assets/sfx/sfx.txt — so
 * their voices OVERLAP in the shared render callback (over the BGM when
 * EM_BGM / the manifest started one), then prints the audio thread's
 * mixed-voice counters and quits. Needs the registry: without sfx.txt
 * the module is disabled by design and the test reports the FAIL. */
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

static void sfx_test_script(void)
{
    int n = g.frame_no;
    if (n == 10 || n == 20 || n == 30) {
        static const unsigned ids[3] = { EM_SFX_WPN_DRAW, EM_SFX_WPN_FIRE,
                                         EM_SFX_ENEMY_DEATH };
        em_sfx_play(ids[n / 10 - 1]);
    } else if (n == 120) {
        long mixed = em_sfx_frames_mixed();
        int  peak  = em_sfx_max_concurrent();
        int  ok    = em_sfx_sound_count() > 0 && em_sfx_plays() == 3 &&
                     em_sfx_drops() == 0 && mixed > 0 && peak >= 2;
        printf("sfx test: %d sound(s) loaded, %d play(s) (%d dropped), "
               "%ld voice frames mixed, peak %d concurrent voice(s) — "
               "%s\n", em_sfx_sound_count(), em_sfx_plays(),
               em_sfx_drops(), mixed, peak, ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* EM_CAMREGION_TEST=1 — FIXED-CAMERA REGION self-test (the mode-0
 * director decode, FINDINGS "MODE-0 CAMERA DIRECTOR DECODED"). The
 * OFFICE DEFINES NO REAL FIXED CAMERAS (the honest decode verdict:
 * AREA02 has no director case and its overlay never touches the camera
 * struct), so scene init injects a SYNTHETIC, CLEARLY-FLAGGED test
 * region 5 u down +Z of the spawn (see ingame_frame_machine case 0) —
 * the machinery under test is exactly what scene_snow's REAL exported
 * region (AREA06, D_0024A5F0[2]) drives. Adaptive phase machine:
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

/* EM_MELEE_TEST=1 — deterministic knife-vs-crate self-test (the s36
 * melee decode, em_weapon.h "KNIFE / MELEE"). Scene init spawns TWO
 * DISGUISED CRATES (slots 0/1, see the spawn block): inside the knife
 * reach (12) but outside the crate's ~10-u proximity-burst trigger, so
 * only the melee damage mailbox can pop them. Adaptive phase machine:
 *
 *   phase 0  frame 8: assert both crates still IDLE (no proximity
 *            burst), then tap L (CIRCLE) — the LIGHT combo (engine
 *            mode 0x21). Impact = swing tick 26 (len 50 - gate 24);
 *            the acquire resolves the nearest crate (A, slot 0).
 *   phase 1  crate A dies: assert melee hit count 1, ZERO rifle shots
 *            (the kill is the +0x36 mailbox write) and the worm
 *            spawned at the crate position (the burst contract).
 *   phase 2  HIT-CONFIRM path: recover anim 0x10F must commit (a
 *            landed hit SKIPS the combo — engine states 0x50..0x52).
 *            Wait out the recover AND worm A's suicide run (it closes
 *            ~0.3 u/tick and lunge-bursts on the player — health is
 *            reported, not asserted), then tap J (SQUARE) — the HEAVY
 *            stab (mode 0x22, damage 15, immediate gate) at crate B.
 *   phase 3  crate B dies: melee hits 2, heavy seen, worm B spawned.
 *   phase 4  wait for worm B + the heavy recover to clear.
 *   phase 5  WHIFF COMBO (nothing left in reach): tap L, re-tap L
 *            during each swing — assert the committed clip chains
 *            0x10B -> 0x10C -> 0x10D (the buffered +0x2E chain), NO
 *            recover anim on a whiff (clip-end exit), and the final
 *            counts (5 swings, 2 hits).
 *
 * PASS = all checks green; any phase timing out fails the run. */
static void melee_test_script(void)
{
    static int saw_recov_anim, saw_heavy, chain12, chain23, whiff_recov;
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
    if (g.mt_phase == 5) {
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
                if (!(em_enemy_alive() == 2 &&
                      em_enemy_state(0) == EM_ENEMY_IDLE &&
                      em_enemy_state(1) == EM_ENEMY_IDLE &&
                      et_dist() > 10.0f && et_dist() < 12.0f)) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — crates not idle "
                           "in reach (alive %d states %d/%d dist %.1f/"
                           "%.1f)\n", em_enemy_alive(), em_enemy_state(0),
                           em_enemy_state(1), et_dist(), et_dist_i(1));
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
                /* slot 0 freed = the burst frame (enemy test 3's
                 * trigger); the worm spawns the same tick */
                if (!(em_weapon_melee_hits() == 1 &&
                      em_weapon_shots() == 0)) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — crate kill not "
                           "the knife mailbox (melee hits %d, shots "
                           "%d)\n", em_weapon_melee_hits(),
                           em_weapon_shots());
                }
                /* worm A (slot 2) emerges at crate A's position */
                float cp[3] = { 0, 0, 0 }, wp[3] = { 0, 0, 0 };
                em_enemy_pos(0, cp);
                em_enemy_pos(2, wp);
                if (!(em_enemy_alive() == 2 &&
                      em_enemy_kind(2) == EM_ENEMY_KIND_CRAWLER &&
                      fabsf(wp[0] - cp[0]) + fabsf(wp[2] - cp[2])
                          < 0.01f)) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — worm not at the "
                           "burst crate (alive %d kind %d)\n",
                           em_enemy_alive(), em_enemy_kind(2));
                }
                g.mt_phase = 2;
                g.mt_mark  = n;
            }
            break;
        case 2:
            /* recover (hit-confirm) must complete; worm A suicide-
             * bursts on the player meanwhile. Strike crate B once
             * melee is idle and the worm is gone. */
            if (em_weapon_is_melee()) break;
            if (!saw_recov_anim) {
                g.mt_fail++;
                printf("melee test: CHECK FAILED — recover anim 0x10F "
                       "never committed after the confirmed hit\n");
                saw_recov_anim = -1;            /* report once */
            }
            if (em_enemy_alive() == 1 &&
                em_enemy_state(1) == EM_ENEMY_IDLE) {
                move_test_inject('j', 1);       /* SQUARE — heavy stab */
                g.mt_phase = 3;
                g.mt_mark  = n;
            }
            break;
        case 3:
            if (n == g.mt_mark + 2) move_test_inject('j', 0);
            if (em_enemy_state(1) == EM_ENEMY_FREE) {
                if (!(em_weapon_melee_hits() == 2 && saw_heavy &&
                      em_weapon_shots() == 0)) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — heavy crate kill "
                           "(hits %d, heavy seen %d, shots %d)\n",
                           em_weapon_melee_hits(), saw_heavy,
                           em_weapon_shots());
                }
                g.mt_phase = 4;
                g.mt_mark  = n;
            }
            break;
        case 4:
            /* worm B's run + the heavy recover clear the field. */
            if (em_enemy_alive() == 0 && !em_weapon_is_melee()) {
                g.mt_phase = 5;
                g.mt_mark  = n;
            }
            break;
        case 5:
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
                    em_weapon_melee_hits() != 2) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — counts (swings "
                           "%d expected 5: light + heavy + 3 whiffs; "
                           "hits %d expected 2)\n",
                           em_weapon_melee_swings(),
                           em_weapon_melee_hits());
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

/* func_001AE5E0 — THE GAMEPLAY FRAME (stage order is the engine's). */
static void gameplay_frame(void)
{
    if (g.move_test) move_test_script();    /* debug instrumentation only */
    if (g.door_test) door_test_script();    /* debug instrumentation only */
    if (g.transit_test) transit_test_script(); /* debug instrumentation  */
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
     * eye. =4: hold stick UP ('w') — aim DOWN ("W = down"). */
    if (g.capture_aim) {
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
     * the player at +27 looking down at him in the doorway (with
     * EM_DOORCAM_LOCKED=1: the locked-look handle placement). */
    if (g.capture_door) {
        if      (g.frame_no == 0)  move_test_inject('w', 1);
        else if (g.frame_no == 24) move_test_inject('w', 0);
        else if (g.frame_no == 28) move_test_inject('w', 1);
        else if (g.frame_no == 58) move_test_inject('w', 0);
        else if (g.frame_no == 60) move_test_inject('k', 1);
        else if (g.frame_no == 61) move_test_inject('k', 0);
    }                                       /* debug instrumentation only */
    /* EM_CAPTURE_RISE=1: hold 's' (stick down) from frame 0 — the
     * player about-faces and runs TOWARD the camera; the chase camera
     * backs away until the room's far wall blocks its desired eye and
     * the WALL-RISE solve lifts it (camera_solve). Use with
     * EM_CAPTURE_FRAME around 280+ (office scene: the eye meets the
     * south wall after ~9 s of running). EM_CAMERA_TRACE=1 prints the
     * per-frame rise for verification. */
    if (g.capture_rise && g.frame_no == 0)
        move_test_inject('s', 1);           /* debug instrumentation only */
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
    if (g.sfx_test)  sfx_test_script();     /* debug instrumentation only */
    if (g.pause_test) pause_test_script();  /* debug instrumentation only */
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
    /* ENEMIES: the crawler state machines (func_001551B0 — also part of
     * the actor-pool tick). Runs BEFORE the weapon update so this
     * frame's shot resolves against current positions. */
    em_enemy_update(&g.coll, g.pos);
    /* Player-side damage mailbox (the crawler lunge writes it with the
     * actor +0x36 code layout: low bits = amount, high = type flags).
     * The engine's own player-damage path (latch byte D_008102BF +
     * drain magnitude D_008104D4) is untranslated; consuming the
     * mailbox into the status health is the port stand-in. */
    {
        int hitcode = em_enemy_player_hit_take();
        if (hitcode) {
            g.status.health -= (float)(hitcode & 0x1FFF);
            if (g.status.health < 0.0f) g.status.health = 0.0f;
        }
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
        em_weapon_update(&g.coll, g.pos, g.yaw,
                         em_door_movement_locked() ? &kNeutral
                                                : em_frame_input());
    }
    camera_update();         /* func_001CB590(0x008101E0, 0xD0, 0) +
                              * func_0018B9C0 camera state machine    */
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
            memset(&g.cam, 0, sizeof g.cam);
            g.cam.yaw = g.yaw;   /* chase camera starts behind the spawn */
            g.cam_region_on = 0;
            /* EM_CAMREGION_TEST: SYNTHETIC test region (FLAGGED — the
             * office defines NO real fixed cameras; the decode verdict
             * in the CAMERA FIDELITY block). A strip starting 5 u down
             * +Z of the spawn (the run-forward corridor; the wall
             * radius stops the player ~14 u in, well inside), with the
             * fixed eye raised BEHIND the spawn looking INTO the strip
             * (real room cameras watch the room — and camera-relative
             * 'w' then keeps pushing the player deeper, not back across
             * the boundary): entering it must pin the camera there.
             * scene_snow's REAL region (AREA06, D_0024A5F0[2])
             * exercises this same machinery. */
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
            /* EM_ENEMY_TEST spawn: tests 1/2 place one crawler 30
             * units ahead of the player spawn along the spawn facing,
             * turned to face the player; test 3 places a DISGUISED
             * CRATE 25 units ahead instead (see enemy_test_script). */
            /* EM_MELEE_TEST spawn: TWO DISGUISED CRATES — A 11.0 u dead
             * ahead (slot 0, the LIGHT-combo kill) and B 11.3 u ahead
             * + 3 u right (slot 1, dist ~11.7, bearing ~15 deg — the
             * HEAVY kill). Both sit INSIDE the knife reach (12,
             * em_weapon.h) but OUTSIDE the crate's ~10-u proximity-
             * burst trigger, so only the melee mailbox can pop them;
             * both also sit inside the office spawn's 14-u wall
             * plane, so no about-face is needed. The acquire picks
             * the NEAREST in the cone, so the light strike resolves
             * crate A first. */
            if (g.melee_test && !g.et_spawned) {
                g.et_spawned = 1;
                float fx = sinf(g.yaw), fz = cosf(g.yaw);
                float pa[3] = { g.pos[0] + fx * 11.0f, g.pos[1],
                                g.pos[2] + fz * 11.0f };
                float pb[3] = { g.pos[0] + fx * 11.3f + fz * 3.0f,
                                g.pos[1],
                                g.pos[2] + fz * 11.3f - fx * 3.0f };
                if (em_enemy_add_kind(em_frame_gfx(), EM_ENEMY_KIND_CRATE,
                                      pa, g.yaw + EM_PI) < 0 ||
                    em_enemy_add_kind(em_frame_gfx(), EM_ENEMY_KIND_CRATE,
                                      pb, g.yaw + EM_PI) < 0)
                    printf("melee test: spawn failed\n");
            }
            if (g.enemy_test && !g.et_spawned) {
                g.et_spawned = 1;
                int   ek = g.enemy_test == 3 ? EM_ENEMY_KIND_CRATE
                                             : EM_ENEMY_KIND_CRAWLER;
                float ed = g.enemy_test == 3 ? 25.0f : 30.0f;
                /* Crate run only: the office spawn faces a wall plane
                 * 14 u ahead (see the kill-run wall note), so a walking
                 * player could never reach the 10-u proximity trigger
                 * of a crate 25 u beyond it. About-face the spawn pose
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
                                               3/4 = aim up/down pose */
    const char *cd = getenv("EM_CAPTURE_DOOR");
    g.capture_door = cd && cd[0] == '1';    /* door cinematic capture */
    if (g.capture_door && !cf)
        g.capture_frame = 110;              /* mid-cinematic default */
    const char *cr = getenv("EM_CAPTURE_RISE");
    g.capture_rise = cr && cr[0] == '1';    /* walk-at-camera rise demo */
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
