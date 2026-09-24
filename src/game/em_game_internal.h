/* em_game_internal.h — shared interior of the slot-0 game task chain.
 *
 * em_game.c grew to ~11.8k lines holding the whole gameplay frame: scene
 * load, player movement, damage, camera, director, lighting and the
 * area set pieces. Splitting those into modules requires one thing first —
 * every section reads the same state object, so the tuning constants, the
 * shared types and the state struct itself have to live somewhere all of
 * them can see. That is this header.
 *
 * It is INTERNAL to src/game/: the rest of the port talks to this subsystem
 * through game/em_game.h. Nothing here is part of the public contract.
 *
 * The state object is `g`, defined once in em_game.c. It is the native
 * mirror of the engine's gameplay globals, so the module split does not
 * change the engine's own shape — the same single state block, viewed
 * from several files. */
#ifndef EM_GAME_INTERNAL_H
#define EM_GAME_INTERNAL_H

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
#include "game/em_player_motor.h"
#include "game/em_player_pose.h"
#include "game/em_point_light.h"
#include "game/em_sfx.h"
#include "game/em_task.h"
#include "game/em_truck.h"
#include "game/em_weapon.h"


#define MODEL_PATH     "assets/player.emdl"
#define SCENE_DIR      "assets/scene"   /* boot default; g.scene_dir is
                                         * the ACTIVE scene (runtime
                                         * switch: em_game_scene_switch) */
#define COLL_DEFAULT   "office.emcl"
#define PLAYER_CHANNELS_PATH "assets/player_channels.empc"
/* Area 0x0B / sub 0 / entry 0, committed by func_001AD360 step 4 on every
 * new-game route (title New Game and game-over option 0). */
#define AREA11_SCENE_DIR "assets/scene_snow"
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
 * timings). Stick release runs the engine's RUN-DOWN (func_0017BC40
 * phase 2, C2): 0.03125 u/tick decay that CASCADES through every tier
 * (run -> jog -> walk -> stop), demoting at each tier's floor — not
 * stopping at the first boundary. The carried-gear x2 decay (actor
 * +0x314 & 0x1F) is OMITTED (the port has no gear-carry state) and the
 * mode-6 stop-skid anims ids 4/5 are untranslated — both flagged. */
#define FRAME_DT        (1.0f / 60.0f)
#define WALK_SPEED      15.0f   /* units/sec — scripted door MOVE-TO only */

/* AREA-11 ELEVATOR DESCENT — DOWNGRADED by audit: OBSERVED, not
 * source-derived. The cited body lives in the OVERLAY (ov 0x00828050);
 * no overlay code is in the decomp (there is no src/func_0082*.c), and
 * the cited INVESTIGATION_area11_elevator.md does not exist in either
 * repo, so nothing here is re-checkable against recovered C. The
 * numbers below come from a live PCSX2 read and should be treated as
 * a port stand-in until the overlay is disassembled.
 * Observed first-use (0x81083A == 0) DOWN run: add the rate to the
 * player ground-Y, the camera target-Y and the platform mesh-Y each
 * frame for 150 frames = 40.0 units (Y~230 -> Y~190). rate const
 * 0xBE888889. */
#define ELEV_RATE_DOWN  (-0.266667f) /* +0x2E8: 0xBE888889 u/frame, DOWN */
#define ELEV_FRAMES     150          /* +0x2EC < 0x96 */
#define ELEV_SFX_DOWN   0x453u       /* func_001FBD50(300, 0x453) — down */

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
/* BODY-HEADING TURN RATES — the engine's facing ease (func_00174AC0
 * rate select + func_001B12B0 turn-toward; decomp FINDINGS "GROUND
 * LOCOMOTION"). The invented TURN_SPEED 12 rad/s slide is RETIRED: the
 * body heading (g.yaw) eases toward the camera-relative DESIRED heading
 * at a per-frame rate banded by the gait/speed state, and velocity is
 * emitted along the EASED g.yaw, NOT the raw stick vector — so motion
 * CURVES into turns (the body lags the stick). rad/frame @ 60 Hz.
 *
 * TURN-IN-PLACE (move_speed == 0), banded by the gait byte +0x23F.
 * CORRECTED (audit) — gaits 1 and 2 were SWAPPED here, and the swap had
 * reached the constants below, so the port turned in place twice too
 * fast on a gait-1 stick and half as fast on a gait-2 stick.
 * func_00174AC0 [NEARMISS] reads, literally, in that order:
 *   +0x23F == 1 -> func_001B12B0(ang, yaw, 0.06981317f)   =  4.0 deg/f
 *   +0x23F == 2 -> func_001B12B0(ang, yaw, 0.13962634f)   =  8.0 deg/f
 *   else (0/3)  -> func_001B12B0(ang, yaw, 0.39269909f)   = 22.5 deg/f
 * TURNING WHILE MOVING (move_speed > 0), banded by |delta| then by the
 * current ramped speed loco_upt (u/tick: walk 0.1, jog 0.3, run 0.8):
 *   |delta| <= 54 deg:  spd<=0.1 -> 4 ; spd<=0.3 -> 6 ; spd>0.3 -> 7
 *   |delta| >  54 deg:  spd<=0.1 -> 6 ; spd<=0.3 -> 9 ; spd>0.3 -> 10.5
 * The turn-toward SNAPS when |delta| <= the chosen rate (no overshoot /
 * jitter), else steps by sign(delta)*rate. */
/* CORRECTED (audit): values swapped to match func_00174AC0's own
 * gait-byte order — gait 1 is the SLOW in-place turn, gait 2 the fast
 * one. Engine literals 0.06981317f / 0.13962634f / 0.39269909f. */
#define TURN_IP_GAIT1   0.06981317f /* 4.0 deg/f, turn-in-place gait 1 */
#define TURN_IP_GAIT2   0.13962634f /* 8.0 deg/f, turn-in-place gait 2 */
#define TURN_IP_GAIT03  0.39269909f /* 22.5 deg/f, turn-in-place 0/3   */
/* The moving-turn set is unchanged in MEANING but tightened to the exact
 * func_00174AC0 literals (the old values were hand-rounded). */
#define TURN_DELTA_BAND 0.9424779f   /* 54 deg — the |delta| split     */
#define TURN_MV_NEAR_W  0.06981317f  /* near band: walk 4 deg/f        */
#define TURN_MV_NEAR_J  0.10471976f  /*           jog  6 deg/f         */
#define TURN_MV_NEAR_R  0.122173056f /*           run  7 deg/f         */
#define TURN_MV_FAR_W   0.10471976f  /* far  band: walk 6 deg/f        */
#define TURN_MV_FAR_J   0.15707964f  /*           jog  9 deg/f         */
#define TURN_MV_FAR_R   0.18325958f  /*           run  10.5 deg/f      */
/* MANUAL AIM STEER — DECODED, re-verified against the recovered C
 * (func_0017ABA0 [NEARMISS] + func_001B5DC0 [byte-matched]; retires the
 * old AIM_TURN_SPEED port stand-in). While aiming, the left stick
 * drives the aim BLEND PAIR player +0x27C (yaw) / +0x278 (pitch),
 * both 0.5-centered in [0,1] (stance entry resets them to 0.5):
 *
 *   - per-frame rate = a 4-band table indexed by the axis deflection
 *     band |raw - 0x80| through func_001B5DC0's rings 49/89/123
 *     (func_001B5DC0 tests < 0x31 / < 0x59 / < 0x7B — byte-matched):
 *     R1-family stances 0x31/0x34: {0, 0.0025, 0.005, 0.015};
 *     EVERY OTHER stance (the R2 family 0x32/0x35):
 *                                  {0, 0.0016666666, 0.005, 0.01}.
 *     CORRECTED (audit): the R2 top band is 0.01, NOT 0.015 — the
 *     recovered func_0017ABA0 else-arm reads rate[3] = 0.01f.
 *     STALE POINTER FIXED 2026-07-31: this used to say "the live
 *     table in em_game.c (kRateR2[3]) still carries 0.015 ... one
 *     third too fast". All three parts were out of date — the table
 *     now lives in em_player.c, it has since been corrected to 0.01f,
 *     and 0.015 vs 0.01 is 50% too fast, not one third.
 *   - YAW: blend +- rate / sin(pi*(0.5 + 0.6*(pitch-0.5))) (faster
 *     when pitched off level; exactly 1.0 at center); overflow past
 *     [0,1] clamps the blend and TURNS THE BODY by the excess (rad) *
 *     1.0 (R1 family) / 1.5 (R2 family) — the pose pans up to its
 *     +-60 deg yaw ladder first, then the player turns;
 *   - PITCH: blend +- (rate * mult) / 2 per frame — INVERTED Y (the
 *     raw axis byte >= 0x80 = stick DOWN increments the blend = aim
 *     UP; stick UP aims DOWN — "W = down", the user-attested original
 *     behavior); mult = 1.5 when pitch <= 0.3 or pitch >= 0.7 for the
 *     R1 family (and x1.8 for sub-weapon 4, not in the port); clamp
 *     [0, 1.0] for stances 0x31/0x32, [0, 0.75] for 0x34/0x35
 *     (func_0017ABA0: lim = (stance - 0x31 < 2) ? 1.0 : 0.75).
 *     The yaw axis has its OWN sub-weapon-4 multiplier (x1.5, not
 *     x1.8) — also not in the port, no sub-weapon state exists.
 *
 * The blends select/blend the 9-step AIM POSE LADDER 0x112..0x11A
 * (FINDINGS "aim-ladder tables") — measured from the baked clips
 * themselves (hand-bone +X fire direction): 0x112 center (+1.3 deg),
 * 0x113 up +81.3, 0x114 down -78.7, 0x115/0x116 level yaw -+60 (model
 * -X = SCREEN RIGHT in the port's basis), 0x117/0x118 up/down right,
 * 0x119/0x11A up/down left. The fire/laser ray follows the posed hand
 * bone automatically (em_weapon's muzzle anchor).
 * AUDIT 2026-07-31 - HOLDS. Both rate tables, the 49/89/123 bands, the two sub-weapon-4
 * multipliers (x1.5 on yaw, x1.8 on pitch) and the (stance - 0x31 < 2) clamp split were re-read
 * in func_0017ABA0 and func_001B5DC0.
 * */
#define AIM_BAND_1      49.0f       /* func_001B5DC0 |raw-0x80| rings */
#define AIM_BAND_2      89.0f
#define AIM_BAND_3      123.0f
#define AIM_PITCH_MAX_R1 1.0f       /* +0x278 clamp, stances 0x31/0x32 */
#define AIM_PITCH_MAX_R2 0.75f      /* +0x278 clamp, stances 0x34/0x35 —
                                     * func_0017ABA0's `lim` else-arm.
                                     * NOTE the clamp families are NOT the
                                     * rate families: the rate table splits
                                     * 0x31/0x34 vs the rest, the clamp
                                     * splits (stance - 0x31 < 2) i.e.
                                     * 0x31/0x32 vs the rest. The port
                                     * models only the R1 stance, so this
                                     * is carried unused until the 0x34/
                                     * 0x35 sub-weapon stances land. */
#define AIM_POSE_UP_DEG   81.3f     /* measured ladder pose pitches */
#define AIM_POSE_DOWN_DEG 78.7f
#define AIM_POSE_CTR_DEG  1.3f
#define AIM_POSE_YAW_DEG  60.0f

/* PLAYER WALL RADIUS — re-verified against func_001764E0 [NEARMISS]:
 * the engine's wall response is NOT a zero-width move segment. Both
 * passes rotate a local probe vector by yaw + D_00248950[i], but they
 * do NOT run the same number of probes:
 *   ANKLE pass (loop 1): FIVE probes, i = 0..4 (`do {...} while (i <
 *     5)`). Local vector literally (0, 0.05, 4.5, 1) — the recovered C
 *     builds that quad on the stack — queried with mask 6 (static
 *     world). GATED: it runs only while the actor is in state 1 sub 1
 *     (arg0+4 == 1 && arg0+5 == 1).
 *   CHEST pass (loop 2): CORRECTED (audit) — EIGHT probes, j = 0..7
 *     (`do {...} while (j < 8)`), each hit setting bit (1 << j) of the
 *     actor's contact mask +0x314, so the byte carries eight lanes.
 *     The old "FIVE radial probes per pass" reading was wrong for this
 *     loop. Vector = the rotated D_002488C0 quad plus a scratch
 *     (0, 4.01, 0, 1) offset (4.01 is an INSTRUCTION immediate in the
 *     loop, not a field of D_002488C0), queried with mask 7 (+ movable
 *     hulls = doors).
 * ANKLE hit response: the engine adds the spad 0x700031C0 overshoot
 * back into the actor position — all THREE components x/y/z, not just
 * x/z — for wall-class hits (surface halfword & 0xFF00 == 0x1000 or
 * 0x800; the 0x2000 movable class needs its owner's bytes +2/+3 to be
 * 4/2, else a normal-angle window rejects it).
 * CHEST hit response is NOT that add: loop 2 routes every hit through
 * func_00176390 [NEARMISS] -> func_00176180 (still an unread stub), so
 * the port's "pos += hit - end" on the chest pass is an APPROXIMATION,
 * flagged. The effective wall standoff is 4.5 units; the old
 * zero-radius pre-move probe is why the player clipped into walls.
 * Sliding emerges exactly like the PS2: only probes pointing into the
 * wall push back, each along its own direction, so motion parallel to
 * the wall survives.
 * NOT re-checkable from the C: D_00248950's angle values are ELF
 * .data. {0, +45, -45, +90, -90} deg is the s38 data-dump reading and
 * stands as OBSERVED, not source-derived — and it covers only the
 * first FIVE entries; loop 2's j = 0..7 indexing proves the table has
 * at least EIGHT. PORT GAP (em_game.c, owned by another pass):
 * player_wall_probes runs 5 directions x 2 passes, so the chest pass
 * is missing three of the engine's eight lanes.
 * AUDIT 2026-07-31 - HOLDS. func_001764E0 re-read: the ankle quad is literally {0, 0.05, 4.5,
 * 1} queried with mask 6 over i < 5, the chest loop runs j < 8 with mask 7 and ORs (1 << j) into
 * +0x314, and the wall-class arm adds all three spad overshoot components back into the position.
 * The +0x236 latch condition is the `hit.y - actor.y < 13.8f` arm, as recorded.
 * */
#define PLAYER_WALL_RADIUS 4.5f   /* the (0, y, 4.5) local probe vector */
#define PROBE_ANKLE_LIFT   0.05f  /* loop-1 local y (0x3D4CCCCD) */
#define PROBE_CHEST_LIFT   4.01f  /* loop-2 scratch y immediate */

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
 * IDLE CYCLE — CONFIRMED, re-verified 2026-07-31 against
 * func_00161020 [NEARMISS] case 1:
 * the 300-frame re-arm (+0x28 = 0x12C), the fidget request
 * func_001749A0(self, 0x15D, 1, 8.0f), the entry blend 12.0 and the
 * post-fidget re-request blend 8.0 are all literally there. One
 * caveat: the base-idle request goes through func_00174A50
 * [byte-matched], which resolves the clip as D_00248AB0[0][self+0x235]
 * (func_0017B490/func_0017B460) — "anim id 0" is therefore the value
 * of that table entry for the unarmed variant, i.e. DATA, not a
 * literal in the code. func_0017B490 [byte-matched] also carries an
 * OVERRIDE arm the port does not model: when self+0x236 == 0 &&
 * !(self+0x235 & 1) && (func_001B0070() & 4) the index is REPLACED by
 * a fixed 4, i.e. the request becomes D_00248AB0[0][4] — the same
 * unarmed gate the fidget uses, plus a global mode bit. The engine also gates the fidget on
 * self+0x236 == 0 && !(self+0x235 & 1) (unarmed), which the port does
 * not model. (FINDINGS "PLAYER IDLE CYCLE"): the player
 * mode-0 top func_00161020 requests the BASE idle anim id 0 (the
 * 80-frame breathing idle; mode-0 family table D_00248AB0[0] via
 * func_00174A50, blend arg 12.0 on entry — CORRECTED 2026-07-31: the
 * old "D_00248A00[0]" contradicted this block's own (and correct)
 * D_00248AB0 citation above; func_0017B460 [byte-matched] is literally
 * `return D_00248AB0[a0][a1];`), then runs a 300-frame
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

/* STATUS-MENU UI SCENE (FINDINGS.md "STATUS-MENU UI SCENE DECODED").
 * PROVENANCE SPLIT (audit): the menu-player behavior func_0020E6F0 and
 * its publisher func_0020EC80 are BYTE-MATCHED — everything in the
 * "Decoded constants" list below was re-read out of them and holds.
 * The screen host func_0020CDC0 is now NEARMISS readable C, but the
 * "camera matrix 0x810610 goes IDENTITY" framing has not been re-checked
 * against it: it stays OBSERVED (the s66 live read), not source-derived.
 * While the status screen is up the engine's 3D frame IS the menu
 * scene — a dedicated static-array actor renders the player
 * turntabling on black, under the animated tile background and the
 * panels. Decoded constants (all from func_0020E6F0):
 *   placement  view-space (7.4, 2.4, 40) of the identity UI camera
 *              (pos = camCol3 + 40*colZ + 7.4*colX + 2.4*colY); the
 *              actor scale is never written (stays the alloc's 1.0)
 *   rotation   init (0, pi, 0) — facing the camera. CORRECTED (audit):
 *              the publisher func_0020EC80 [byte-matched] composes
 *              diag(-1,-1,-1) and then rotates about X by pi, NOT
 *              about Y — it feeds pi to func_00102B08, the same
 *              composer it uses for the actor's +0xC0 (X) euler,
 *              while the +0xC4 spin yaw goes through func_00102BB0
 *              (Y). yaw += 0.01 rad/frame, wrapped > pi -> -2pi (one
 *              rev ~10.5 s)
 *   anim       displayed health > 35 -> clip 0x1C2 (450), a SINGLE-
 *              FRAME stance (static pose; all motion is the spin);
 *              <= 35 -> clip 0xA (10), the 90-frame low-health idle;
 *              advance 1.0/frame; recovery past 35 swaps back to
 *              0x1C2 (no swap TO 0xA mid-open — init only)
 *   tint       per-actor color delta (-0.8, -1.0, -0.3) * infection
 *              * ramp (G clamped >= -127), ramp breathing 1.0 <-> 1.3
 *              at +-0.01/frame. CORRECTED (audit): that is 30 frames
 *              up + 30 frames down = a ~1 s pulse, not the "2 s" the
 *              old comment claimed.
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
 * lit — also explained at ui_scene_render.
 * AUDIT 2026-07-31 - all four source-derived bullets HOLD, re-read in func_0020E6F0 and
 * func_0020EC80. func_0020CDC0 (now NEARMISS C) has not been re-read for the camera-identity
 * framing, so it stays OBSERVED.
 * */
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
 * KILL PLANE (player spine, s15/s17): pos.y < -200 -> state 6.
 * func_0015D460 [byte-matched] — CONFIRMED by audit: sub 0 writes
 * +0x220 = 0 (health) and +0x00 = 0 (event byte) then advances, sub 1
 * calls func_001AEDE0(4, 0) (the same fade-out) then advances, every
 * later sub falls through the switch and parks. No anim, no sound.
 * (The -200 threshold itself is the caller's test, not in this
 * function — it stays an observed constant.)
 *
 * GAME OVER — THE DECODED CHAIN (s66 live + the s70 static decode of
 * func_001AD4E0 / func_001AC070 / func_001AC480; the old "screen id
 * D_008106CF via state 6" framing was the END-OF-LEVEL path —
 * D_008106CE/CF are never written on death):
 *
 *   vitals mirror (~0x0015CFB0)   health <= 0 latches D_008106B9 = 1
 *                                 [DOWNGRADED by audit: 0x0015CFB0 is
 *                                 a bare address, not a recovered
 *                                 function — observed, not decoded]
 *   0x001AE040 state-1 tail       latch && fade == 2 (hold-black) ->
 *                                 func_001AD140: game task 3/2
 *                                 [0x001AE040 is `anim_frame_top_b`,
 *                                 now NEARMISS readable C: state 1,
 *                                 after the variant, calls 001AD140
 *                                 when D_008106B9 != 0 and
 *                                 D_0028A9A0 == 2
 *                                 (SCENE_COORDINATOR_DESIGN.md §2.3).
 *                                 The two links BELOW it are decoded:
 *                                 see the func_001AD4E0 /
 *                                 func_001AC070 / func_001AC480
 *                                 notes.]
 *   GAME-OVER WAIT func_001AD4E0  sub 0: timer task+0x18 = 0xF0 = 240
 *                                 sub 1: raise the busy flag
 *                                   D_00275BD8 = 1 and launch SCREEN
 *                                   MODULE 0x27 (func_001FF080(0,
 *                                   0x27)) — the GAME OVER art screen.
 *                                   (Citation tightened by audit: the
 *                                   busy GATE is sub 2's, not sub 1's)
 *                                 sub 2: module up (D_00275BD8 back to
 *                                   0) -> FADE-IN
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
 * Since S11b the PORT runs the game-over half as that original chain in
 * the scene coordinator: B9 (0015CF90 at the player stage) -> 001AD140 at
 * fade substate 2 -> the byte-matched 001AD4E0 core (the 240 hold, the
 * CROSS skip, the fades) -> 001ADF00 -> 001AB790(001AC070), which
 * replaces the game task with the interim 001AC070 (the legacy continue
 * prompt, em_game_legacy_continue_task_001AC070: cursor init from
 * D_00275BDC, d-pad moves, START/CROSS confirms, 1200-frame idle timeout,
 * fade out, dispatch). FLAGGED stand-ins: the two screen modules'
 * art is unexported (em_hud_game_over / em_hud_continue draw the
 * skeleton presentation: black base + tall-font title / option
 * lines); option labels are port guesses (the module text is not
 * decoded); options 1/2 and the timeout have no native target (no
 * save system, no title screen) — all three return to the prompt,
 * documented at the dispatch; option 0 = the title menu's New Game
 * route (death reaches the 001AC070 prompt via func_001ADF00 with
 * D_00275BDC = 1): the 001AF2C0 reset (game_state_new_game +
 * em_pickup_reset) and a restart at AREA11 0x0B/0/0 (001AD360 step 4)
 * that re-arms the opening.
 *
 * HEARTBEAT (func_0015D000 [byte-matched] — CONFIRMED by audit, the
 * counter at +0x210 resets on `slti 0x79`/`slti 0x3D`): health <= 35
 * -> pad-rumble pulse (func_001B61C0(0, 0xD0, 4, 0)) every 121
 * frames, <= 10 -> stronger (0xE0) every 61; health exactly 0 exits
 * before either. PURE RUMBLE — the port has no force-feedback
 * backend; documented not-applicable (no sound is involved).
 *
 * HAZARD-ROOM passive drain (func_0015D100 first arm: room attr bit
 * 4 + area flags & 0x60 + suit byte 0x810C7E == 0 -> health -= 1.0
 * every 360 frames) is untranslated — the port has no room-attribute
 * flags yet (flagged).
 *
 * The port's enemy producers post one mailbox int (em_enemy.h):
 * 0x4000 | 15 from the worm's lunge connect -> 15.0 pending HEALTH
 * damage. CONFIRMED by audit: func_00154120 [NEARMISS] sub 3 writes
 * D_008104D4 = 0x41700000 = 15.0 (sub 0's earlier latch writes
 * 0x40A00000 = 5.0), and D_008104D4 is exactly player+0x224 since the
 * player actor base is 0x008102B0. It also sets the damage TYPE byte
 * D_008102BF = 2, so the engine takes a TYPED reaction the port does
 * not translate — replaces the invented 0x400A/10; whether
 * the engine's latch drains health or infection is undecoded,
 * flagged in em_enemy.h), and GEN_TRAP_HIT (5) from the open
 * breather pad -> 5.0 pending INFECTION (the engine pad writes
 * +0x22C = 5.0 — the old port consume burned it as health damage;
 * corrected).
 * AUDIT 2026-07-31 - every claim in this block that cites recovered C HOLDS:
 * func_0015D460's two arms and its fall-through parking; func_0015D000's health == 0 early-out
 * ahead of the <= 10.0 / 61-frame and <= 35.0 / 121-frame rumble counters; func_00154120 sub 0
 * and sub 3 writing 0x40A00000 and 0x41700000 to D_008104D4 with D_008102BF = 2; and
 * func_0021F330 / func_0021F850 / func_0021C120's clips 0x2C / 0x2E / 0x24 and sounds 0x150 /
 * 0x14D. The GAME OVER chain's three source-derived links (func_001AD4E0, func_001AC480, func_001AC070)
 * all check out, and the two downgraded links are correctly downgraded.
 * */
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
/* BUG LATCH / SHAKE-OFF — the CROSS-mash struggle (FINDINGS "BUG LATCH
 * / SHAKE-OFF"). CLIPS + SOUNDS CONFIRMED by audit against the recovered
 * C: func_0021F330 [byte-matched] state 0 requests clip 0x2C;
 * func_0021F850 [byte-matched] step 0 fires sound 0x150 + clip 0x2E and
 * step 6 fires clip 0x24; func_0021C120 [byte-matched] fires sound
 * 0x14D. The RATES/THRESHOLD below remain flagged PORT constants — none
 * of them appears in those functions (PD_STRUGGLE_WIN's 16 is a port
 * press count; the engine's 0x10 at func_0021F850 step 4 is a frame
 * countdown, not a press target). */
#define PD_CLIP_CLING        0x2Cu  /* clinging idle (sub 0x0B phase 0)    */
#define PD_CLIP_STRUGGLE     0x2Eu  /* per-CROSS struggle (sub 0x0C ph 0)  */
#define PD_CLIP_THROWOFF     0x24u  /* final throw-off (sub 0x0C phase 6)  */
#define PD_SFX_SHAKE        0x150u  /* struggle-start sound (func_0021F850) */
#define PD_SFX_THROWOFF     0x14Du  /* throw-off sound (func_0021C120)     */
#define PD_LATCH_HIT        10.0f   /* LIVE: health hit on the latch connect
                                     * (+0x224 = 10.0; worm lunge was 15)  */
#define PD_LATCH_INFECT     0.80f   /* FLAGGED: infection gained per frame
                                     * per clinging bug (live: 0->100 fast
                                     * — the real killer; needs a live rate)*/
#define PD_LATCH_DRAIN      0.25f   /* FLAGGED: health drained per frame per
                                     * clinging bug (live: 90->1 over the
                                     * latch; needs a live rate)           */
#define PD_STRUGGLE_WIN     16      /* FLAGGED: CROSS presses to throw the
                                     * bug off (live counter advanced ~1 per
                                     * press; real mashing is fast)        */
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
 * above — engine values from func_001AD4E0 [byte-matched] and
 * func_001AC480 [NEARMISS]). ALL FIVE re-verified by audit: the 240 is
 * the u16 at task+0x18 written in func_001AD4E0 state 0; the 1200 is
 * task+0x16 written in func_001AC480 states 0/2/4; the cursor lives at
 * task+0xF, inits 1 when D_00275BDC != 0 and 0 otherwise, increments
 * only while < 2; the confirm sounds and the move blip 5 all go
 * through func_001FB9F0.
 * AUDIT 2026-07-31 - all five HOLD, re-read: 0xF0 into task+0x18 in func_001AD4E0 state 0;
 * 0x4B0 into task+0x16 in func_001AC480 states 0/2/4; the cursor at task+0xF seeded from
 * D_00275BDC and incremented only while < 2; and 5 / 0x5DD / 0x5DE / 0x5DF all issued through
 * func_001FB9F0.
 * */
#define GO_PROMPT_FRAMES  1200     /* task+0x16 = 0x4B0: continue-prompt
                                    * idle timeout (reset by any held
                                    * button) */
#define GO_CURSOR_MAX     2        /* prompt options 0..2 (func_001AC480
                                    * clamps the d-pad walk to < 2 on
                                    * increment) */
#define GO_SFX_MOVE       5u       /* cursor move blip (func_001FB9F0) */
#define GO_SFX_CONFIRM0   0x5DDu   /* option-0 confirm */
#define GO_SFX_CONFIRM1   0x5DEu   /* option-1 confirm */
#define GO_SFX_CONFIRM2   0x5DFu   /* option-2 confirm */

/* go_state values: the death latch, the GAME OVER screen module
 * stand-in (set by the 001FF080(0, 0x27) binding; the 001AD4E0 core owns
 * its timing) and the interim 001AC070's continue prompt (continue_tick
 * in em_game.c). */
enum {
    GO_OFF = 0,
    GO_ARMED,          /* death fade-out running (pd_phase 3)         */
    GO_SCREEN,         /* GAME OVER screen module 0x27 stand-in shown */
    GO_PROMPT,         /* CONTINUE prompt: fade-in + cursor + timer   */
    GO_PROMPT_CONFIRM, /* confirmed: fading out; dispatch at black —
                        * func_001AC480 [NEARMISS] state 2 fires
                        * func_001AEDE0(4,0) + the per-option sound and
                        * moves to state 3, which returns the verdict 1
                        * only once D_0028A9A0[0] == 2 (hold-black)    */
    GO_PROMPT_TIMEOUT  /* idle timeout: fading out; engine -> title —
                        * func_001AC480 state 2 with no held button
                        * counts task+0x16 to 0, fades out and moves to
                        * state 4 (verdict 3; a held button there fades
                        * back IN and returns to state 2)             */
};

/* Camera values — the AUTHENTIC engine numbers (FINDINGS.md "CAMERA
 * SYSTEM" + the s65 walk-camera decode): idle eye ~33 u behind the
 * player (the tether band [follow - slack, follow]) and 19 u above the
 * player's ground Y; the cut-table target follow seeks player.y + 11 +
 * cam[0x8C] — +17 idle (matching the live state-01 measurement), +8
 * while moving. Chase caps: 2.0 u/frame x/z + 4.0 y for the target
 * pre-step (func_001916C0), 4.0 for the eye solver chase (style 0). */
#define CAM_DIST        33.0f   /* legacy yaw-anchored desired-eye distance.
                                   NO LONGER on the idle path (s76 idle-
                                   emergence pass routed idle through the
                                   tether + solver + entry seat — D14
                                   retired). Still used by the doorcam /
                                   examine-restore re-seats and the idle
                                   auto-orbit / L1 placement (camera_desired_
                                   eye, cam_yaw_blocked) until those are
                                   ported to the emergent path too. */
#define CAM_EYE_HEIGHT  19.0f   /* IDLE eye height above player ground Y =
                                   11 + cam[0x5C] + cam[0x8C] (2 + 6 default;
                                   the -31.2 areas use 6 + 2 — same sum.
                                   func_00191390 + func_00230000, s65) */
#define CAM_TGT_HEIGHT  17.0f   /* IDLE target height = player.y + 11 +
                                   cam[0x8C](6) — func_001916C0 idle case
                                   (cut table, decoded s65). The old 15.0
                                   belonged to the SMOOTH-table inline
                                   follow, which gameplay does not use; the
                                   live capture measured +17.
                                   FAMILY CAVEAT (audit): unlike the eye
                                   height, this one is NOT family-neutral.
                                   func_00191390 [byte-matched] writes
                                   0x8C = 6 only when cam+0x64 != -31.2;
                                   in a -31.2 record 0x8C = 2, so the
                                   engine target rides player.y + 13
                                   there. The port pins 17 for every
                                   scene, which overstates the target
                                   height by 4 in the office (-31.2)
                                   records. Fix belongs with the runtime
                                   in em_camera.c/em_game.c (another
                                   pass owns those). */
#define CAM_AIM_OFFSET  6.0f    /* struct +0x8C idle/default table value.
                                   CONFIRMED by audit — func_00191390
                                   [byte-matched, asm-word leaf] is a
                                   state switch that writes
                                   (+0x8C, +0x5C) = (6, 2) for states 1/3
                                   AND for every state it does not name
                                   (the fall-through lands on the same
                                   test), flipping to (2, 6) when
                                   cam+0x64 == -31.2 (sum 8 either way);
                                   (-3, 1) for states 2/4/0xF, (0, 2) for
                                   6/7/8/9/0x2C/0x2D, (11, 2) for 0x13.
                                   It also clears +0x94/+0x98 on entry and
                                   sets +0x98 = 23.0 when +0x6D != 0.
                                   NOTE the test is `== -31.2`, so "the
                                   -46.8 param" really means "anything
                                   that is not -31.2".
                                   AUDIT 2026-07-31 - HOLDS. func_00191390 was read out
                                   instruction by instruction: the `== -31.2` test and all five
                                   (0x8C, 0x5C) rows are exactly as recorded, as are the
                                   +0x94/+0x98 clear on entry and the +0x98 = 23.0 arm.
                                   func_001916C0's presets were re-read at the same time: the dip
                                   family really is states 0/1/3/14/20/21/22 and the no-dip family
                                   2/4/15.
 */
#define CAM_TGT_CAP_XZ  2.0f    /* desired-target x/z chase cap, u/frame
                                   (func_001916C0 walk+idle cases; the old
                                   0.8 was the smooth-table inline's cap) */
#define CAM_TGT_CAP_Y   4.0f    /* desired-target y seek cap (func_0018C4B0
                                   calls inside func_001916C0) */
#define CAM_TGT_DIP_K   0.3f    /* func_001916C0 [NEARMISS] first preset:
                                   target.y want = player.y + 11 + cam[0x8C]
                                   + 0.3 * shaped(excess) (0x3E99999A). With
                                   excess = horiz_dist - |camdist|, this SAGS
                                   the target while the camera is over-close:
                                   at AREA-11 (horiz ~31.5, follow 46.8)
                                   excess -15.3 -> dip -4.6, tgt.y -> player
                                   + 12.4 (the live read).
                                   CORRECTED (audit) — the dip is NOT
                                   idle-only. func_001916C0's dip preset
                                   covers player states 0, 1, 3, 14, 20, 21,
                                   22; the NO-dip preset is 2/4/15 (the
                                   ELEVATED family). Since ordinary ground
                                   walking is state 3 (the s71 reading kept
                                   below), a normal WALK gets the dip too —
                                   only the elevated/fall family escapes it.
                                   The port gating this on idle understates
                                   the sag while walking close to the eye. */
#define CAM_TGT_DIP_CAP 7.0f    /* the excess re-map cap (0xC0E00000 = -7.0):
                                   when excess < -slack the term is re-shaped
                                   to (-2*slack - excess) and then CAPPED at
                                   -7.0 — `if (!(u <= -7.0f)) u = -7.0f;`, so
                                   the shaped term always stays <= -7.0. (The
                                   old comment said "floored at -7.0", which
                                   reads as the opposite bound.) */
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
 *    (CORRECTED (audit): func_001764E0 [NEARMISS] raises +0x236 when
 *    the ledge it probed is LESS than 13.8 u ABOVE the actor —
 *    `!(hit.y - actor.y < 13.8f)` is the BAIL arm, so the latch is a
 *    "can reach this ledge" test, not "player 13.8 u above the floor
 *    probe" as the old comment read; the j<3 / stance-code /
 *    falling-speed gates also have to pass.
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
 *    (func_00230000 [byte-matched] case 11: when func_00194D10(self,
 *    other, 1) reports the player inside region 1 the goal becomes
 *    6.0 + 11 + cam+0x5C + player.y — a constant 6.0 REPLACING
 *    cam+0x8C. CORRECTED (audit): the size of that swap is whatever
 *    0x8C currently holds, so it is +9 only for the ELEVATED family
 *    (0x8C = -3); on an ordinary ground walk (state 3, 0x8C = 6) it is
 *    a NO-OP, and +4 in a -31.2 record (0x8C = 2). The old flat "the
 *    walking eye rides 9 u HIGHER inside that rect" was reading the
 *    elevated row. This still retires the s50 "+12 AIM target-height
 *    tweak" guess), and the
 *    area-0 fall cam (player.y < -83: eye (120, -1590), target y ->
 *    -67.5, style-5 solve, manual 4.0 chase).
 *  - tail (every walk frame): cam+0x44 = atan2 of the ACTUAL pair —
 *    the camera heading is an OUTPUT of the tether, never an input.
 *  - L1 arm: func_00230000 runs it for states 2/0xF only (not 4);
 *    the port arms it for every gait (state<->gait map unpinned).
 * AUDIT 2026-07-31 - HOLDS. func_00230000, func_00191D40, func_0015CBA0 and func_001764E0
 * were all re-read. The elevated-family reading holds: func_0015CBA0 picks 1-vs-2 and 3-vs-4 on
 * +0x236, func_00162A40 sets that byte together with +0x235 |= 2, and func_00179680 clears it.
 * func_00230000 runs the L1 arm for player states 2 and 0xF only.
 * */
/* The follow distance is fabs(g.cam_dist_param) — the engine's
 * cam+0x0C (per-record, scene.txt `camdist`; default -46.8, office
 * records -31.2 [s66 live]); slack keys on the -46.8 default (the
 * engine tests cam+0x64 — the port folds record + area param into the
 * one scene knob). */
/* All three CONFIRMED by audit against func_0022FCA0 [NEARMISS]: the
 * error term is `D_00810690 - fabsf(cam+0xC)`; the threshold is -20.0
 * when cam+0x64 == -46.8f and -10.0 otherwise; below it the response
 * splits on `D_0081069C > 8.6f`; and the swing steps the bearing by
 * (pi * (0.3f * overclosure)) / 180 per frame, direction latched at
 * cam+3 from sign(cam+0x90 - eye->target heading).
 * AUDIT 2026-07-31 - HOLDS. func_0011DF78 really is fabs, so the error term reads exactly as
 * written, and the -20/-10 threshold, the 8.6 split and the 0.3-deg-per-unit swing with its cam+3
 * direction latch are all literal in func_0022FCA0.
 * */
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
 *    cameras (func_001B0460 hard-copies desired AND actual).
 * AUDIT 2026-07-31 - the source-derived halves HOLD. func_0018DD20 has no arm that
 * RAISES the eye: the ordinary wall arm moves x/z only, and the 0x8800 floor/ceiling arm drops y
 * by 1.0 and re-bounds rather than lifting. func_0018F870 keeps the 0.5 pull-in on x/z, gates at
 * 0.99 and bounds the eye at floor + 2.0. func_00195130's inner switch is on D_00810700 with
 * exactly the listed area cases, and its lone func_823FE0 call sits in case 0xD behind D_00810702
 * >= 8. func_001B0460 splits *(p+0x10) into bit 7 -> cam+0x05 and bits 0..6 -> cam+0x06, indexes
 * D_0024A8D0 by (word >> 8) * 0xC, and hard-copies desired AND actual with the target at player +
 * 15 on y. The per-area EYE VALUES remain data reads, not recovered C.
 * */
#define CAM_REGION_YGATE 4.0f   /* func_00194D10's |player.y - rec.y|
                                   region gate, engine constant */
/* func_0018DD20 solver constants — ALL engine immediates from the .s
 * (decoded 2026-06-11 s61; hex floats noted where non-obvious). */
#define SOLV_EXT          1.5f   /* primary probe extension past the eye   */
/* CORRECTED (audit): 0x3F34FDF4 is 0.70699977 — a ROUND 0.707, NOT
 * sin45 (0.70710678 = 0x3F3504F3). func_0018DD20 [NEARMISS] reads
 * `if (dot < 0.707f) steep = 1;`. */
#define SOLV_GLANCE_COS   0.707f /* glancing gate (0x3F34FDF4):
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
 * with the follow solver above.
 * AUDIT 2026-07-31 - HOLDS. Every SOLV_* and AIMS_* immediate was re-read in func_0018DD20 /
 * func_0018F870 (the 0.17 lives in func_0018CE60). Two worth keeping in view: the follow solver's
 * floor pad is 17.0 and drops to 6.0 only when cam+0x5C == 1.0, and the aim solver casts its
 * bound probe along a UNIT vector, which is where AIMS_BOUND_PULL's 1.0 comes from.
 * */
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
#define CAM_L1_RATE     0.034906585f /* rad/FRAME — the L1 orient-behind seek
                                   MOTION: the orient-to-heading family
                                   rate (func_001921D0 [NEARMISS] states
                                   7/0x2C/0x2D, 0.034906585f = 2 deg/f,
                                   fed to func_001B12B0(goal, cur, rate);
                                   state 7 aims at pi + player yaw, i.e.
                                   BEHIND). Re-read + TIGHTENED by audit
                                   (was a rounded 0.0349): the engine
                                   literal is 0.034906585f, re-read in
                                   func_001921D0 cases 7 and 0x2C/0x2D.
                                   One nuance:
                                   0x2C/0x2D run 2 deg/f only until the
                                   alignment latch cam+0x6C sets, after
                                   which they hold at 0.0034906587f =
                                   0.2 deg/f. The sub-state-3 motion
                                   handler is still unread — the rate
                                   the port uses stays flagged. The ARM
                                   is decoded (func_00191000
                                   [byte-matched]): see the L1 block in
                                   camera_mode_dispatch.
                                   AUDIT 2026-07-31 - HOLDS. func_00191000 is the ARM only and
                                   clamps cam+0x4C into [7.0, |cam+0x64|]; func_001921D0 cases 7
                                   and 0x2C/0x2D carry the 0.034906585f literal and the post-latch
                                   0.0034906587f.
 */
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
 *     test, cam_yaw_blocked).
 * AUDIT 2026-07-31 - HOLDS. func_001921D0's idle tail re-read: +0x08 is reset by (+0x07 & 9)
 * and by any player state outside 1/2, fires at >= 0x1E1, gates on 0.05235988f, and picks its
 * direction with ~bit4 (delta < 0) / ~bit2. func_00193D90 feeds 0x3B64C389 straight into the
 * blend call and never reads self+0x40, so the orbit rate really is flat.
 * */
#define CAM_IDLE_ORIENT_FRAMES 481        /* +0x08 >= 0x1E1 — CONFIRMED
                                             func_001921D0 [NEARMISS] */
/* CORRECTED (audit): 0x3D567750 decodes to 0.05235988, not the
 * 0.052368 the old comment carried. Both func_001921D0 and the L1 arm
 * func_00191000 [byte-matched] compare against 0.05235988f. */
#define CAM_IDLE_ORIENT_DEADBAND 0.05235988f /* 3 deg (0x3D567750) */
/* CORRECTED (2nd audit): 0x3B64C389 decodes to 0.0034906587, i.e.
 * EXACTLY 0.2 deg/frame — NOT the 0.0034904801 the previous audit note
 * claimed (that value is 0x3B64C08A, a different literal). The bit
 * pattern is built lui 0x3B64 / ori 0xC389 in func_00193D90
 * [byte-matched, asm-word leaf], and the same 0.0034906587f literal
 * appears in func_001921D0 [NEARMISS] as the arm-time floor on the
 * orbit rate (self+0x40) and as the post-latch rate of camera states
 * 0x2C/0x2D — three independent sightings of the same constant.
 * DISAMBIGUATED (s86 audit): func_00193D90 feeds the literal STRAIGHT
 * to func_001B12B0 (mtc1 of the lui 0x3B64 / ori 0xC389 pair into
 * $f14) and never reads self+0x40, so the idle orbit really does run
 * at a FLAT 0.2 deg/frame. func_001921D0's self+0x40 =
 * 0.022222f * fabsf(delta), floored at this same value, belongs to a
 * different (unread) motion handler — the port constant below is the
 * orbit's actual rate, not just its floor. */
#define CAM_ORBIT_RATE  0.0034906587f     /* rad/frame (0x3B64C389) */

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
 *   dispatcher tail — CORRECTED (audit), the comparison was INVERTED:
 *     func_00197D20 [byte-matched] case 1 reads
 *       `if (D_008105D4 < 23.0f + *(float *)(arg1 + 0xA4))`
 *     (D_008105D4 = the ACTUAL eye Y, arg1+0xA4 = player Y), so the
 *     min-distance clamp runs while the eye is BELOW player.y + 23 —
 *     a LOW eye that would otherwise slide inside the player — not
 *     above it. Inside that gate, a horizontal eye<->player distance
 *     < 8 pushes the eye out to EXACTLY 8 along its own heading. It
 *     also lives ONLY in the ENTRY sub-state arm (case 1); the steady
 *     arm (case 2 -> func_00197870) returns without it. em_camera.c
 *     has since been fixed to test `<`;
 *   release (player states 0xC/0x29): the engine swaps to transition
 *     mode 2 (func_00198650, untranslated) — the port re-seeds the
 *     chase yaw from the actual eye->player heading and lets mode-0
 *     chase blend back (the engine's own .L001935EC reset shape).
 * AUDIT 2026-07-31 - HOLDS. func_00197D20 case 1 reads `D_008105D4 < 23.0f + player.y` and,
 * inside that gate, pushes the eye out to exactly 8.0; case 2 has no such arm.
 * */
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
 *     decoded constant-height func_0018DD20 solve).
 *     THE +10 — CORRECTED (audit), attribution twice wrong. It is not
 *     func_00191000 (s64 was right that that one is only the L1
 *     re-orient ARM: it writes cam+6/+1/+0x48/+0x4C and never places
 *     the eye) and it is not func_00230000 either — that one IS
 *     recovered now [byte-matched] and its goal is the ordinary
 *     11 + cam+0x5C + (cam+0x8C or the region-1 constant 6.0) above
 *     player.y = 19. The over-close HEIGHT BOOST in func_001921D0's
 *     [NEARMISS] free-look tail produces it: when the tether slack
 *     falls below the limit the desired eye height becomes
 *       11 + cam+0x8C + player.y + (cam+0x5C - scratch)
 *     with scratch = 0.5*(slack - lim) in the -46.8 family and
 *     (slack - lim) FLOORED AT -10.0 otherwise (`if (d < -10.0f) d =
 *     -10.0f`) — i.e. a rise of up to exactly +10 over the default
 *     ride in the non -46.8 family, which is the family the office
 *     records (-31.2) sit in. The port does not model the boost
 *     (flagged).
 *   LOCKED try (script D_0024DEC0 record 2, op 0x09 -> func_001BBBF0):
 *     TARGET = door pos + 8 u toward the HANDLE side (the door-yaw
 *     left: (-8*cos(dyaw), +10, +8*sin(dyaw))) and EYE = TARGET -
 *     13*(sin,cos)(camera yaw D_00810374) with EYE.y = door.y + 12 —
 *     the camera parked at the handle while the try animation plays;
 *     the finish script (op 0x07 sub 4) restores the saved camera. */
/* CONFIRMED by audit: func_001B7B30 [byte-matched] case 5 calls
 * func_0018CBD0(cam, player, -20.0f) then sets cam+0xA0 = 0x78, and
 * func_0018CBD0 [NEARMISS] writes eye.y = 11 + f4 + f5 + player.y and
 * tgt.y = 11 + f4 + player.y + 0.3*shave, with (f4, f5) = (6, 2) when
 * cam+0x64 == -46.8 and (2, 6) otherwise — so eye = +19 in BOTH
 * parameter families and the target is +13 default / +17 at -46.8,
 * exactly as recorded. (That family split agrees with func_00191390
 * [byte-matched], which reaches the same pair from the opposite test,
 * `cam+0x64 == -31.2`.) The +0.3*shave term: func_0018CBD0's NEARMISS
 * renders the shared subexpression ambiguously, so the shaping value
 * is read off the STRUCTURALLY IDENTICAL block in func_001916C0
 * [NEARMISS, 98.80%, zero control-flow diff] — there the un-reshaped
 * term is the raw excess (desired horiz dist minus |chase dist|), and
 * at the door cut the eye is placed at exactly the chase distance, so
 * the excess is 0 and the shave vanishes. That is what makes the
 * recorded +13 the right value.
 * RE-VERIFIED (s86 audit) — DO NOT "fix" this back by reading
 * func_0018CBD0.c literally. That NEARMISS renders the shave seed as
 * `t = f3 - ang; f20 = t;`, which taken at face value would make the
 * door-cut target player.y + 10 (default family) / +11 (-46.8), NOT
 * the live-measured +13/+17. The twin in func_001916C0 settles it:
 * there the seed is the excess ITSELF (`0x70003A20 = D_00810690 -
 * fabsf(cam+0xC)`), the reshape is `u = lim + (lim - t)` — exactly
 * func_0018CBD0's `f20 = f3 + t` once t is the excess — and the cap
 * is `if (!(u <= -7.0f)) u = -7.0f`, which func_0018CBD0 renders with
 * the test and the assigned value both scrambled. Both reshape arms
 * are gated on `ang < f3` and at the door cut ang == 0 (the eye is
 * placed at exactly |speed| = 20 from the target, and ang is
 * horizdist - fabsf(speed)), so no reshape runs and the shave is 0
 * under the corrected reading. func_001BBBF0 [NEARMISS] likewise
 * matches the LOCKCAM_* quartet below line for line: target =
 * (door.x - 8*cos(door yaw), door.y + 10, door.z + 8*sin(door yaw)),
 * then -= 13*sin/cos(D_00810374) on x/z with y = door.y + 12.
 * AUDIT 2026-07-31 - HOLDS. func_001B7B30 case 5 calls func_0018CBD0(cam, player, -20.0f) and
 * then writes cam+0xA0 = 0x78; func_0018CBD0's (f4, f5) family split and both height formulas re-
 * read; func_001BBBF0 matches the LOCKCAM_* quartet exactly. The func_001916C0 twin argument
 * still holds. The +10 over-close boost is borne out by func_001921D0's tail, which computes 11 +
 * cam[0x8C] + player.y + (cam[0x5C] - scratch) with scratch = 0.5*(slack - lim) at lim -20 and
 * (slack - lim) floored at -10.0 otherwise.
 * */
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

/* ===================================================================== *
 * AREA11 event director. Milestone selection, polygon geometry and Y
 * gates are recovered from runtime 0x008253F0..0x008257A0 and loaded by
 * em_area11_flow. The three programs are triggered during traversal;
 * none is active at the New Game spawn. The old overlay symbol map was
 * shifted by 0x40, so use runtime addresses when consulting evidence.
 *
 * The camera-keyframe executor remains an approximation: it does not
 * implement every command in the original scripts. Correct triggers do
 * not establish that the resulting cutscenes are faithful.
 *
 * GATING / SAFETY: the director is DORMANT (cine_active == 0) outside a
 * beat — the camera (movement-v3 chase) and player movement run EXACTLY
 * as before. A beat starts only when the player walks into the beat's
 * XZ zone on the correct Y tier; while a beat runs, the camera is
 * overridden (director_camera) and the player is locked (cine_active
 * folds into em_game_player_interact_busy). On completion (or any
 * interruption) the lock drops, the camera does a one-shot chase
 * restore, and the step advances — NO soft-lock by construction
 * (control always returns: the beat ends when its keyframes run out).
 * ===================================================================== */
#define CINE_STEP_BEAT0   0x00     /* D_00810813 milestones */
#define CINE_STEP_BEAT1   0x10
#define CINE_STEP_BEAT2   0x20
#define CINE_STEP_DONE    0xFF
/* op0C sub0 = 001B7D60, the message op: rec+0x14 is a message LINE word
 * (-> D_002821B8), not a sound or music cue. Lines 151/153. */
#define CINE_MESSAGE_LINE_BEAT1 0x97
#define CINE_MESSAGE_LINE_BEAT2 0x99
/* TRANSITIONAL aliases for em_director.c (not this lane's file); drop them
 * once its two initializers and b->music read use the names above. */
#define CINE_MUSIC_BEAT1  CINE_MESSAGE_LINE_BEAT1
#define CINE_MUSIC_BEAT2  CINE_MESSAGE_LINE_BEAT2
#define CINE_BAR_LINES    64.0f    /* s81: ~64-line top/bottom black bars */
#define CINE_BAR_FADE     20       /* approximate raise/drop fade window
                                    * (FLAGGED: exact opening staggered-
                                    * fade count un-stopwatched, doc §F) */
#define CINE_KF_MAX       15       /* longest beat (beat 0) = 15 keyframes
                                    * (5 cuts + 5 blend/wait pairs) */

/* One camera keyframe — a literal eye->target vector pair held/blended
 * for `dur` frames. `blend` = 0 HARD CUT (snap eye/tgt, hold dur
 * frames, op00 sub0), 1 BLEND from the live eye/tgt toward this one over
 * dur frames (op00 sub1/sub5). A `wait` keyframe (eye/tgt NaN-free but
 * blend = -1) just holds the current framing dur frames (op02 WAIT). */
typedef struct {
    int   blend;          /* 0 = cut, 1 = blend, -1 = wait (hold) */
    int   dur;            /* frames (the record +0xC duration) */
    float eye[3];
    float tgt[3];
} CineKey;

/* Camera program plus descriptive bounds for logging/test placement.
 * Trigger inclusion uses the exported quadrilateral in em_area11_flow,
 * not these bounds; the second polygon is not axis-aligned. */
typedef struct {
    float   x0, x1, z0, z1;   /* trigger XZ AABB (world) */
    float   ylo, yhi;         /* Y gate band [ylo, yhi] */
    int     music;            /* MISNAMED: the op0C (001B7D60) message
                               * line word at beat start, 0 = none — not
                               * a sound or music cue. Rename to
                               * message_line together with
                               * em_director.c's `b->music` read. */
    uint8_t next_step;        /* D_00810813 value on completion */
    int     reg_keyitem;      /* func_1C4760(1) on completion (beat 0) */
    int     n_key;
    CineKey key[CINE_KF_MAX];
} CineBeat;

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
 * default 480 — func_001D25F0 [byte-matched] is the one-line writer
 * `ctx+0x2468 = fa0`, and every static caller in the recovered corpus
 * (func_001D2880, func_001B6BF0, func_001B7B30, func_001B82D0, and
 * func_0022EEF0's non-scope path) passes 480.0f. The scope camera
 * (top_mode 3, func_0022EEF0) passes 224.0f / tan(fov) — the
 * ENGINE_CAM_ZOOM_SCOPE below.
 * CORRECTED (audit): func_001D2590 is NOT a "scripted lerp". The
 * recovered asm is a two-arg projection helper — it computes
 * func_001D25F0(a / tan(b / 2)) — only the SECOND argument is halved
 * (re-read this pass: `a` is the dividend, NOT halved; the earlier
 * "halving both args" was wrong), tan via func_0011E398. That is the
 * same half-height/half-fov form the scope path uses. Nothing in the
 * recovered corpus interpolates this field; every writer snaps it.
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
                             — nothing in the recovered corpus
                             interpolates this field; every writer
                             snaps it. WRITER CONFIRMED, re-verified
                             2026-07-31: func_001D25F0 [byte-matched]
                             is the one-line setter —
                             `D_00275670[0x2468] = fa0` plus a spad
                             mirror at 0x70003B60 — and func_001D2590
                             [byte-matched, asm-void] is the pair
                             setter, feeding arg1/2 to func_0011E398
                             (the fov side).
                             CORRECTED 2026-07-31: the old "NOTE the
                             /2: the zoom func_001D2590 stores is HALF
                             its first argument" was WRONG, and
                             contradicted this header's own (correct)
                             projection note above. func_001D2590 puts
                             2.0 in $f0 and halves ONLY the second
                             argument for the tan call; the SECOND
                             divide reuses $f0 AFTER that call has
                             overwritten it with the tan RESULT, so
                             what reaches func_001D25F0 is
                             arg0 / tan(arg1/2) — arg0 UNHALVED. ($f20,
                             not $f0, is the register the routine
                             bothers to save across the call: it is
                             arg0 that must survive, not a constant.)
                             That is exactly the 224/tan(half-vfov)
                             scope form of ENGINE_CAM_ZOOM_SCOPE. The
                             480 default itself is a LIVE read (s66),
                             not a literal in either function —
                             observed, not source-derived. */
    float    eye_des[3];  /* +0x10: desired EYE (world) */
    float    tgt_des[3];  /* +0x20: desired TARGET (world) */
    float    seed_euler[3]; /* +0x30: original interaction retarget rotation */
    float    yaw;         /* +0x44: eye->target heading; the R1/L1
                             orient and the idle auto-orient steer it */
    /* func_0018DD20 solver state (decoded s61) */
    float    y_lo;        /* +0x50: eye-Y lower bound (floor-under-eye +
                             17), re-probed every solve; init -200 */
    float    y_hi;        /* +0x54: eye-Y upper bound (ceiling-over-eye -
                             1, else eye + 200); init 1000 */
    uint16_t hit_attr;    /* +0x58: primary-hit surface-class halfword
                             (kept across clear frames, engine-true) */
    uint16_t probe_flags; /* +0x5A: original collision prepass flags */
    float    overhead_y; /* +0x60: overhead contact Y; only written on hit */
    uint8_t  ground_attr78; /* +0x6D: ground contact attribute 0x78 */
    float    var_5c;      /* +0x5C: solver height variant, init 2.0.
                             WRITER FOUND (s65): the pre-step
                             func_00191390 per-state table. CORRECTED
                             (audit) — name the STATES, not "walk"/
                             "idle" (the s71 pass showed 2/4/0xF are
                             the ELEVATED family, not ordinary
                             locomotion): states 2/4/0xF write 1.0
                             (head-clear 13 / floor pad 6, the low
                             ride); states 1/3 and every unlisted
                             state write 2.0, or 6.0 when
                             cam+0x64 == -31.2 (same pad rule) */
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
                             (camera_prestep_00191390, s65). CORRECTED
                             (audit), same state-id fix as var_5c:
                             states 1/3 + every unlisted state get 6.0
                             (2.0 in a -31.2 record), states 2/4/0xF
                             (the ELEVATED family) get -3.0, 6/7/8/9/
                             0x2C/0x2D get 0.0 and 0x13 gets 11.0.
                             Feeds the target height (11 + 0x8C) and
                             the eye base (11 + 0x5C + 0x8C); the aim
                             camera proper is MODE 1 */
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
/* LIGHTING — max placed lamps per scene. Citation tightened (audit):
 * func_001F6760 [byte-matched] is only the TABLE SELECTOR — it packs
 * (D_00810700 << 8) | D_00810701 and returns one of eight per-area
 * lamp-list pointers (NULL for an unlisted key). The record COUNT of
 * any one list is ELF .data, not visible in that function; "office0
 * has 8 records" is the exporter's data dump, i.e. OBSERVED. 16 is a
 * port headroom cap either way. */
#define LAMP_MAX 16

#define CAM_REGION_MAX 8
typedef struct {
    float x0, z0, x1, z1;  /* XZ rect (min/max) */
    float ygate;           /* elevation gate center (+-CAM_REGION_YGATE) */
    float eye[3];          /* the room's fixed camera eye spec */
} EmCamRegion;

typedef struct {
    /* assets */
    EmModel    model;            /* player */
    EmGfxMesh *mesh;             /* player (NULL = not loaded) */
    SceneItem  scene[SCENE_MAX];
    int        n_scene;
    float      player_palette[1024 * 16]; /* bone_count <= 1024 (loader) */

    /* gameplay-frame state */
    int        frame_no;         /* gameplay frames run */
    uint8_t    opening_event_39; /* D_00810791: automatic opening state */
    uint8_t    opening_key_item_zero; /* D_00810CC3[0], 001C4760(0,1) */
    uint8_t    opening_complete; /* D_00810811 (event flag 0xB9): the
                                  * AREA11 opening controller 00823E80
                                  * stores 0xFF here when its script
                                  * 0x828FC0 ends (0x00823F74..80), next
                                  * to +0x2E = 0xFFFF and 001C4760(0,1).
                                  * Captured 0 mid-opening, 0xFF at the
                                  * handoff before any battery exists. */

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
    uint8_t    probe_low_clearance; /* original +236; initial AREA11 value0 */
    uint8_t    probe_block_mask; /* original +314 radial lane results */
    float      loco_upt;         /* ramped ground speed +0x38, u/tick */
    uint8_t    loco_mode, loco_substate; /* original +1F0/+1F1 scalar motor */
    int        loco_entry_ticks; /* pending 0017B5C0 eight-tick clip blend */
    float      loco_rate, loco_blend; /* published +204/+208 */
    float      loco_animation_step; /* prior +204 consumed by0015BA50 */
    EmPlayerStop loco_stop;
    EmPlayerReentry loco_reentry;
    int        loco_stop_clip;
    float      loco_stop_from[1024 * 16]; /* frozen world pose at blend request */
    float      move_speed;       /* this frame's ground speed, units/sec */
    float      walk_palette[1024 * 16];  /* scratch for the blends */
    float      loco_palette[1024 * 16];  /* C1: next-tier clip scratch for
                                          * the jog->run ramp cross-blend
                                          * (anim_matrix_player +0x208) */

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
                                  * trigger volumes for this scene).
                                  * CITATION TIGHTENED by audit — the
                                  * claim rests on func_00194D10
                                  * [NEARMISS], which probes
                                  * func_001B1EA0(0, player+0xA0,
                                  * &D_0024A5F0[i*0x40], 4) (an XZ
                                  * point-in-quad over a stride-0x40
                                  * record) and accepts only when
                                  * fabsf(player.y - rec+0x04) < 4.0f;
                                  * the caller is the director
                                  * func_00195130 [NEARMISS]. The
                                  * per-region EYE is NOT in that
                                  * table — it comes from the separate
                                  * spawn-record path (D_0024A8D0 via
                                  * func_001B0460), so only the VOLUME
                                  * half of this field is
                                  * source-derived.
                                  * AUDIT 2026-07-31 - HOLDS. func_00194D10 is exactly the
                                  * stride-0x40 point-in-quad probe plus the < 4.0f Y gate, and
                                  * its caller is func_00195130.
                                  * */
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
                                  * — the cut's Euler, live-verified).
                                  * Grounded by audit: func_0018CBD0
                                  * [NEARMISS] opens by copying
                                  * D_70003B50 into cam+0x30 and builds
                                  * the cut basis from THAT, so the cut
                                  * really does ride the snapshot pose,
                                  * never the live camera heading.
                                  * AUDIT 2026-07-31 - HOLDS. func_0018CBD0's first statement
                                  * copies D_70003B50 into cam+0x30.
                                  * */

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
    int        struggle_n;     /* bug-latch shake-off mash counter (CROSS) */
    int        pd_cue_fall;    /* death-clip T-80 sound fired */
    int        pd_cue_thud;    /* death-clip T-16 sound fired */
    int        go_state;       /* GAME-OVER/CONTINUE machine (GO_*
                                * enum at the PD block) */
    int        go_frames;      /* frames in the current GO state */
    int        go_timer;       /* continue-prompt idle timeout (engine
                                * task+0x16 = 1200; reset by any held
                                * button) */
    int        go_cursor;      /* prompt cursor (engine task+0xF;
                                * init D_00275BDC ? 1 : 0) */
    int        go_restart;     /* option 0 confirmed: New Game-route
                                * restart latch (001AF2C0 reset + AREA11),
                                * serviced by the interim 001AC070 task.
                                * Grounded by audit: func_001AC070
                                * [NEARMISS] state 2 sends cursor 0 to
                                * state 4 with D_00275BE0 = 0, and state
                                * 4 runs func_001AB790(func_001ACEC0)
                                * and returns WITHOUT the common tail —
                                * the gameplay task is reinstalled
                                * wholesale, which is what this latch
                                * stands in for.
                                * AUDIT 2026-07-31 - HOLDS. func_001AC070 state 4 calls
                                * func_001AB790(func_001ACEC0) and returns before the common tail.
                                * */

    /* the camera block the recorded chain consumes (native K = P*V) */
    float      viewproj[16];

    /* render chain (this frame's recorded draws). Single-slot extra draws,
     * each independent of n_scene and of each other: +1 player, +1 wedged
     * truck, +1 elevator platform, +1 gated grate. The earlier sizing
     * budgeted only two of these and leaned on "the elevator reuses the
     * SCENE_MAX scene-slot headroom" — but n_scene can reach SCENE_MAX while
     * all four are present, which overflowed by 2 (a future 16-part
     * AREA-11-style scene would corrupt the trailing g members). All four are
     * now budgeted explicitly (CHAIN_CAP), AND render_chain_build now routes
     * every push through chain_push() with a defensive bound check + one-time
     * overflow log, so any future cap drift drops a draw instead of
     * corrupting `g`. */
    ChainDraw  chain[SCENE_MAX + 1 + 1 + 1 + 1 + EM_DOOR_MAX + EM_ENEMY_MAX
                     + EM_PICKUP_MAX];
#define CHAIN_CAP (SCENE_MAX + 1 + 1 + 1 + 1 + EM_DOOR_MAX + EM_ENEMY_MAX \
                   + EM_PICKUP_MAX)
    int        chain_len;
    int        chain_test_triangle;

    /* EM_BGM=<path.wav>: debug-only listening override (see boot task);
     * not an original music path */
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
    float       cam_dist_param;  /* "camdist <f>" — the engine camera
                                  * distance cam+0x0C/+0x64 (signed;
                                  * default -46.8, office records -31.2).
                                  * Walk tether + door re-seat consume
                                  * fabs(); slack keys on == -46.8. */

    /* OPENING-CAMERA SEAT (scene.txt `opencam` — s79 iteration 4). The
     * AREA-11 opening idle camera is the engine's func_0018DD20 wall
     * solver acting on a wall-buried entry seat; its settled +X eye is a
     * 44-u lateral relocation from the -X-Z buried seat that the decoded
     * solver branches STRUCTURALLY CANNOT produce (the pull-in keeps the
     * seat side, the wedge needs a reverse-probe hit that the single-
     * sided collision misses, and the 5.5-u side probes run PARALLEL to
     * the faced wall — proven across s79 iterations 1-4 with a live
     * geometry probe of snow.emcl). AUDIT STATUS: UNRESOLVED, and
     * deliberately so. The three solver facts the argument leans on are
     * confirmed in func_0018DD20 [NEARMISS] (pull-in 0.5 along the
     * sight line, a wedge arm that needs the reverse probe to hit, and
     * 5.5-u side probes); the CONCLUSION that no branch reaches +44 u
     * is a property of snow.emcl's geometry, not of the recovered C, so
     * it cannot be re-derived from the decomp. Treat the seat as a
     * scene-data workaround, not a decoded behaviour.
     * Rather than corrupt the byte-faithful
     * solver with a fake branch, the known emergent result is SEATED here
     * for the one scene that needs it: a data line, like the engine's own
     * data-driven spawn-record cameras. eye.y SEEKS from ey0 (the seat
     * height +19) to ey1 (the settled wall-clear height) at the engine
     * eye-Y rate, reproducing the live +9.55 rise; eye.x/z and the target
     * are pinned (the dead-band freeze). Absent line = no opening seat
     * (every other scene runs the pure solver).
     * AUDIT 2026-07-31 - UNRESOLVED stands. The three cited solver facts hold in
     * func_0018DD20; the conclusion that no branch reaches +44 u is a property of snow.emcl's
     * geometry and cannot be re-derived from the decomp.
     * */
    int         opencam_on;      /* an `opencam` line was parsed         */
    int         opencam_idle;    /* idle frames since the seat armed; the
                                  * raise stops being applied once the
                                  * 481-frame reorient takes over (cam
                                  * orbit), and any move/aim disarms it    */
    float       opencam_eye[3];  /* settled eye x / (rise target y) / z   */
    float       opencam_ey0;     /* frame-0 eye.y (the seat height)       */
    float       opencam_tgt[3];  /* the pinned look-at target             */

    /* LIGHTING — the scene's CHARACTER LIGHT RIG (scene.txt light*
     * lines, export_level.py --lightrig: the decoded per-room rig
     * table D_00251C50 record for this scene's (area<<8)|sub key plus
     * the room's placed-lamp list). The engine selects the rig by the
     * area/sub bytes D_00810700/701 (func_001D7B30 [byte-matched],
     * re-read by audit: key = (D_00810700 << 8) + D_00810701, linear
     * scan of at most 45 records at stride 0x78 comparing the record's
     * first word, and NO-MATCH falls back to the table BASE record —
     * plus an override key 0xF00 whenever func_001D2910(8) is
     * non-zero, which the port does not model) — a sub-state
     * flip IS a scene switch in the port, so the rig is per-scene
     * constant by the engine's own mechanism (no spatial room bounds
     * exist or are invented; see char_rig_build). rig_on = 0 (no
     * manifest lines) keeps the historical shader stand-in.
     * AUDIT 2026-07-31 - HOLDS. func_001D7B30 keys (D_00810700 << 8) + D_00810701, scans 0x2D
     * records at stride 0x78 comparing the record's first word, falls back to the table base on
     * no match, and overrides the key with 0xF00 when func_001D2910(8) is non-zero.
     * */
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
    EmPointLightPool point_lights; /* original active/staging pool */
    uint16_t    point_lights_area_key;
    int         point_lights_loaded;

    /* LIGHTING — the scene's DISTANCE FOG (scene.txt `fog` line,
     * export_level.py / the D_00251C50 rig record fog fields rec+4/+8 =
     * near/far, rec+0xC/10/14 = RGB). 001D8FD0 (-> 0021B970/0021BA80)
     * writes the render-ctx fog block: coefficients
     * a = 255*far/(far-near), b = -255/(far-near) and FOGCOL = the record
     * RGB (0..255 GS units). The VU1 kernel (0x23C8A0..0x23C928) computes
     * F = clamp(a + b*w, 0, 255) per VERTEX (w = the vertex's clip w) and
     * the GS blends toward FOGCOL by F; em_fog_gs.h mirrors it
     * (tools/test_area11_fog_reference.py). AREA-11 (key 0x0B00) =
     * near -209, far 304, FOGCOL (48,48,48). fog_on = 0 (no `fog` line)
     * leaves the scene unfogged (office/drawbridge stay byte-identical). */
    int         fog_on;          /* a `fog` line was parsed */
    float       fog_near;        /* GS fog near (view-space depth; may be <0) */
    float       fog_far;         /* GS fog far */
    float       fog_rgb[3];      /* GS FOGCOL, 0..255 units */

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
                                  * the player (see em_game_selftest_pre_frame) */
    int         capture_examine; /* EM_CAPTURE_EXAMINE=1 — snow scene:
                                  * walk to the AREA11 switch + CROSS so
                                  * the capture samples the refusal line
                                  * ("Switch / No power...") presenting
                                  * (see em_game_selftest_pre_frame) */
    int         capture_orient;  /* EM_CAPTURE_ORIENT=1 — turn-in-place,
                                  * then idle: the slow auto-orient demo */
    int         capture_walk;    /* EM_CAPTURE_WALK=1 — hold 'w' (run
                                  * AWAY from the camera) so the capture
                                  * samples the plain moving-player chase
                                  * framing: eye +19 / target +17 (the
                                  * s71 idle-row correction — no dive
                                  * toward the feet) */
    int         cam_print;       /* EM_CAM_PRINT=1 — dump the settled
                                  * camera state (eye/tgt/yaw/horiz dist)
                                  * at the capture frame, any capture mode
                                  * (the idle-emergence A/B read, s76) */
    int         capture_locked;  /* EM_CAPTURE_LOCKED=N — drawbridge m15
                                  * LOCKED-door try from approach variant
                                  * N (1 = straight on, 2 = oblique SW —
                                  * different prior camera orientations);
                                  * the capture samples the locked-look
                                  * cut and prints the camera at the
                                  * capture frame: runs 1 and 2 must
                                  * match (the s71 snap-yaw anchor) */
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
    int         cine_test;       /* EM_CINE_TEST=1 — AREA-11 opening
                                  * director self-test (teleport into a
                                  * beat zone, verify trigger/lock/camera
                                  * /completion/control-return/step) */
    int         ct_fail;         /* director test: failed checkpoints */
    int         ct_phase;        /* director test: script phase */
    int         ct_mark;         /* director test: phase anchor frame */
    int         ct_locked_max;   /* director test: max frames seen locked */
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

    /* The AREA11 power byte D_0081084C (formerly terminal_powered) is a
     * canonical D2 progress byte since WP-4 (em_scene_state.h,
     * em_game_terminal_powered). The legacy elevator ride (elev_state,
     * elev_pending, elev_frame, elev_rate) was retired in WP-4: the
     * original owners 00159210 / 00827B10 and the carry 00828050 run in
     * the AREA11 interaction host (em_area11_interaction_host.c). */
    /* ELEVATOR PLATFORM mesh (optional — manifest `elevator <model> x y
     * z`). When present it descends with the ride; when absent the ride
     * still works (player + camera descend) and the missing mesh is
     * flagged. */
    int         elev_has_mesh;   /* a parsed+loaded elevator platform */
    EmModel     elev_model;      /* platform model (valid if elev_has_mesh) */
    EmGfxMesh  *elev_mesh;       /* platform GPU mesh */
    float      *elev_palette;    /* platform pose palette (world-placed) */
    float       elev_pos[3];     /* platform placement (world); +0xB4 = [1]
                                  * descends with the ride */
    float       elev_yaw;        /* platform facing */

    /* Original AREA11 switch actor00159210 / per-area model04.
     * Legacy grate_* field names remain local to the scene/props boundary;
     * this actor has no translated gate slide or synthetic blocker. */
    int         grate_present;
    EmModel     grate_model;
    EmGfxMesh  *grate_mesh;
    float      *grate_palette;
    float       grate_pos[3];
    float       grate_yaw;

    /* AREA-11 OPENING DIRECTOR (the D_00810813 step machine — OBSERVED,
     * see the DOWNGRADED note on the director block above: the driving
     * body is overlay code the decomp does not have and the cited
     * INVESTIGATION_area11_director.md is not in either repo). The
     * 3-beat establishing
     * cinematic. cine_step is the persistent milestone byte (reset to 0
     * at scene arm, like the elevator actor — the opening plays once in
     * AREA-11). The rest is the per-beat transient (cleared when a beat
     * ends or is interrupted — the guarantee against soft-lock). */
    uint8_t     cine_step;       /* D_00810813: 0/0x10/0x20/0xFF */
    int         cine_active;     /* a beat is running (cinematic + lock) */
    int         cine_beat;       /* index of the running beat (0/1/2) */
    int         cine_kf;         /* current keyframe index */
    int         cine_kf_t;       /* frames elapsed in the current keyframe */
    float       cine_blend_eye[3];  /* a BLEND keyframe's live start eye */
    float       cine_blend_tgt[3];  /* a BLEND keyframe's live start tgt */
    int         cine_fade;       /* letterbox fade accumulator (0..FADE) */
    int         cine_was;        /* a beat ran last frame (restore edge) */

    /* AREA-11 AREA-TITLE CARD ("FORT STEWART - REAR ENTRANCE", string table
     * 0x00273B80 idx1 — INVESTIGATION_area11_director.md §4.4. Rides the
     * gameplay HUD frame, independent of the cinematic director. Armed ONCE
     * on AREA-11 scene entry; em_hud owns the fade-in/hold/fade-out. This
     * field only records that the card was armed for the active scene so a
     * re-entry re-arms it (and non-AREA-11 scenes never arm). */
    int         area_title_armed; /* the title card was armed this scene */
    /* Vertical fall velocity (engine actor +0x2EC). Integrated by the
     * gravity tick in player_move_collide; zeroed on landing. */
    float       fall_vel;
} EmGameState;

/* func_001AF2C0 (src/func_001AF2C0.c; reached by 001ACEC0 route 1 ->
 * 001AD230 for BOTH the title's New Game and the game-over prompt's
 * option 0) — the part of the new-game reset that EmGameState mirrors.
 * The 0x640-byte memset of D_00810700 clears D_00810791, D_00810811,
 * D_00810813, D_0081084C (D_00810841[11]) and D_00810CC3; then health
 * D_00810858 = 100.0, D_0081085C = 0, battery D_00810CB2/CB7 = 0, and
 * (after 001C40B0(0x10,2)) magazine D_00810C62 = 30, reserve
 * D_00810CB4 = 60. health_max/mag_max are port display constants. The
 * inventory part is em_pickup_reset(). Instruction-checked by
 * tools/test_continue_reset_reference.py. */
static inline void game_state_new_game(EmGameState *s)
{
    s->status = (EmPlayerStatus){ .health = 100.0f, .health_max = 100.0f,
        .infection = 0.0f, .mag = 30, .mag_max = 30, .reserve = 60,
        .battery = 0, .battery_max = 0 };
    s->opening_event_39      = 0;
    s->opening_key_item_zero = 0;
    s->opening_complete      = 0;
    s->cine_step             = 0;
}

/* Compose a loaded palette with a placement transform: T(pos) * R_y(yaw).
 * Shared with em_props.c, whose set pieces pose through the same path the
 * player and doors use (defined in em_game.c). */
void palette_apply_placement(float *pal, uint32_t bone_count,
                             const float pos[3], float yaw);

/* Original-channel player source and shared interaction host boundary.
 * The stage hook returns -1 fault,0 ordinary callback,1 callback consumed. */
void player_pose_set_stage_hook(int (*hook)(void *), void *context);
void player_use_set_hook(int (*hook)(void *), void *context);
int player_use_poll(void);
int player_pose_load(const char *path);
void player_pose_unload(void);
int player_pose_opening_release(void);
int player_pose_stage(void);
void player_pose_finish_state(void);
void player_pose_request(unsigned clip, float frame, unsigned blend, int force);
void player_pose_idle_enter(void);
void player_pose_entry_cancel(void);
int player_pose_entry_return_tick(void);
int player_pose_foot_stop_begin(void);
int player_pose_foot_stop_active(void);
int player_pose_foot_stop_tick(void);
int player_pose_foot_stop_palette(void);
int player_pose_idle_state_wait(void);
void player_pose_invalidate(const char *reason);
int player_pose_acquire(void);
int player_pose_use_accepted(void);
int player_pose_idle_tick(float *local_palette);
/* Original0A/sub1 request and next-player-stage83090 special-bank commit.
 * Bank is borrowed until release/unload. Caller must tick its attached face
 * separately before this body worker when shared player-ready is2. */
int player_pose_cinematic_request(const EmPoseBank *bank, unsigned clip, float rate);
int player_pose_cinematic_tick(float *local_palette, int freeze_motion);
int player_pose_cinematic_active(void);
int player_pose_release(void);
int player_pose_script_tick(const EmInteractionAnimation *animation, int result,
                            float *local_palette);
int player_pose_publish(const float *local_palette);
int player_pose_hip(float out[3]);
void player_pose_finish_palette(void);
int player_pose_align(const float position[3]);
int player_pose_face(float yaw);
int player_pose_script_euler(float out[3]);
int player_pose_owned(void);
int player_pose_source(unsigned *clip, float *remaining, unsigned *flags, int *transition);

/* Aim direction for the current frame (player lane, defined in em_game.c).
 * The camera's aim mode reads it to place the over-shoulder eye. */
void aim_dir_get(float out[3]);

/* Cinematic camera override (director lane, defined in em_game.c). Returns
 * non-zero when the director has taken the camera this frame. */
int director_camera(EmCamera *cam);

/* Settle the camera inside the room bounds (camera lane, still defined in
 * em_game.c because em_game.c also calls it; belongs in em_camera.c once the
 * player split moves its other caller out). */
void cam_bounds_settle_0018CE60(EmCamera *cam, const float pt[3], int style);

/* The engine's 5-bit LCG draw (defined in em_game.c). Shared because the
 * damage lane's infection roll uses the same generator as the footstep
 * picker — moves into em_player.c when the footstep lane is split. */
unsigned footstep_rand5(void);

/* GRAVITY (func_00179880, byte-matched): -0.04 per frame, terminal -4.0.
 * PLAYER_FALL_ENTRY is PORT-SIDE — the engine's airborne handoff lives in
 * func_001796C0, an all-.word leaf with no recoverable C. Chosen just above
 * the largest single-frame floor step so stairs do not trigger a fall. */
#define PLAYER_GRAVITY        (-0.04f)
#define PLAYER_FALL_TERMINAL  (-4.0f)
#define PLAYER_FALL_ENTRY     (1.5f)

/* Locomotion speed tiers and the wall-segment probe (defined in em_game.c).
 * Shared with em_player.c: both the player's own move and the gameplay
 * frame's tier logic read the table. */
extern const float kLocoTierSpeed[4];
int probe_wall_seg(const float start[3], const float target[3], int with_doors, EmCollHit *hit);

/* The one gameplay state object (defined in em_game.c). */
extern EmGameState g;

/* ------------------------------------------------------------------ */
/* World-frame stages (S5 of docs/SCENE_COORDINATOR_DESIGN.md)          */
/* ------------------------------------------------------------------ */
/* Each stage is named by the original function whose frame position it
 * occupies and wraps the port code that already ran at that position,
 * unchanged. Return convention of the scene cores (design section 3.1):
 * >= 0 done, -1 = fault. Since S10a em_scene_bindings.c binds them as the
 * stage workers of em_sf_001AE5E0 / em_sf_001AE6B0; positions with no port
 * code (0015C160, 001F0360, 001AAD00) are reported by the bindings. */

/* em_player_frame.c */
int em_player_0015BCF0(void);   /* player actor update (gameplay only) */

/* em_render_frame.c */
int em_render_001D1C50(void);   /* per-frame point-light tick          */
int em_render_001C1D00(void);   /* render-env init (skeleton no-op)    */
int em_render_001D1EA0(int a0); /* today's close-out (flush, overlays) */
int em_render_001ABF90(void);   /* 001AD4E0's game-over screen packet  */
int em_camera_0018B9C0(void);   /* camera_update + em_sfx_listener     */
int em_camera_0018B9C0_opening(void); /* cutscene variant's camera stage */

/* Port-native draw-list collector and the close-out body (the latter also
 * drawn by the interim 001AC070 continue task, em_game.c). */
void render_chain_build(void);
void frame_close_out(void);

/* Env-gated self-tests (em_game_selftest.c). em_game_selftest_pre_frame
 * runs every EM_*_TEST script and EM_CAPTURE_* injection at the head of
 * the gameplay variant (em_game_legacy_variant_head). move_test_inject is the synthetic key injection those
 * scripts use; em_director.c's cine_test_script uses it too. */
void em_game_selftest_pre_frame(void);
void move_test_inject(int key, int down);

#endif /* EM_GAME_INTERNAL_H */
