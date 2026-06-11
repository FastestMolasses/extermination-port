/* em_weapon.c — SPR4 firing loop (see em_weapon.h for the engine mapping).
 *
 * Module-level singleton like em_input/em_door: the original game has one
 * player and one gun actor (player +0x20 / D_008102D0), all weapon state
 * is global/static on the PS2 too (the s18 inventory block) — no heap.
 */
#include "game/em_weapon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_input.h"
#include "em_model.h"
#include "game/em_enemy.h"
#include "game/em_game.h"
#include "game/em_sfx.h"

/* --- Engine constants (FINDINGS "WEAPON SYSTEM") ----------------------- */
#define WPN_MAG_MAX     30      /* func_0017B300: mag = min(30, reserve)   */
#define WPN_RANGE       260.0f  /* func_001861C0 untargeted endpoint, and
                                 * the acquisition max distance            */
/* FIRE INTERVAL +0x2F4 (decoded 2026-06-11 from func_0017A8B0 — the
 * trigger-press handler the action machine func_001607D0 runs on every
 * FIRE press): the press latches +0x274 AND writes +0x2F4 = the FRAME
 * COUNT of the stance's aim-ladder base clip (func_001C61D0 = container
 * header halfword +2), i.e. SEMI fires one round per ladder-clip length
 * — 25 frames for the SPR4 (clip 0x112) -> counter +2/tick reaches 25
 * at the 12th tick after the shot, queued shot the 13th: ~4.6 rds/s,
 * the gun re-fires only after the 12.5-tick recoil replay settles.
 * The BURST/AUTO fire states overwrite +0x2F4 = 12.0 per round (the
 * func_00170A60 0x15/0x1E stores) -> the 6-frame in-burst cadence.
 * (The port's old flat 12 made semi twice the engine rate — the
 * user-reported "no fire rate".) */
#define WPN_INTERVAL_AUTO  12.0f /* +0x2F4 burst/auto per-round store     */
#define WPN_SEMI_FALLBACK  25.0f /* ladder clip 0x112 true length (EMDL
                                  * without the clip)                     */
#define WPN_QUEUE_WINDOW   8     /* +0x2A press queue samples from
                                  * counter >= int(+0x2F4) - 8 — presses
                                  * earlier in the cadence are DROPPED   */
#define WPN_COUNT_STEP  2       /* +0x276 fire counter gains 2/frame       */
#define WPN_BURST_LEN   3       /* burst family 20..23: +0x28 slti 3       */
#define WPN_BURST_PAUSE 8       /* contract: burst pause = 8 ticks         */
#define WPN_RAY_MASK    0x7u    /* func_001861C0: set mask 7 (hulls +
                                 * cells + grid)                           */
#define WPN_RAY_ID      0x20    /* func_001861C0: query id 0x20            */
#define WPN_HIT_DMG     5       /* func_001B41F0(victim, ..., 5, 0): the
                                 * bullet's damage written into the
                                 * victim's +0x36 mailbox (crawler HP 1
                                 * = one-shot kill)                        */
#define WPN_OVERSHOOT   5.0f    /* targeted rays overshoot the aim point
                                 * by 5 units (func_001861C0 step 1)       */
#define WPN_AIM_CONE    0.9848f /* cos ~10 deg — acquisition facing cone.
                                 * PORT STAND-IN for the engine's SCREEN-
                                 * space cone (func_00199220: |x|<=66+50s,
                                 * |y|<=45+45s on the GS canvas) until a
                                 * projection-space acquisition lands      */

/* --- LASER SIGHT (em_weapon.h header block; s23 disasm) ---------------- */
#define WPN_LASER_SEGS  32      /* func_001E2BA0: the beam is 32 GS LINE
                                 * segments muzzle -> endpoint             */
#define WPN_LASER_PHASE 0.025f  /* per-segment flicker-phase step factor:
                                 * f21 = (0.1 * len) / 4.0 radians         */
#define WPN_DOT_SIZE    3.0f    /* func_001854E0/760: endpoint dot sprite
                                 * 3.0 units (locked-on uses 5.0)          */
#define WPN_LASER_WIDTH 0.12f   /* PORT VALUE: the engine beam is a GS
                                 * LINE prim = 1 screen pixel at 512x448;
                                 * ~0.12 world units reads as ~1 px at the
                                 * aim camera's typical 25-35 u depth
                                 * (1 px ~= z / 240 at zoom s = 480)       */
/* func_00185760 beam base color: unlocked (0.7, 0, 0, 1). With a locked
 * target (D_008106E0 nonzero, aim option 1) the engine switches to
 * (1.0, 0.6, 0.2, 1) and a 5.0-unit warm dot — pending native lock-on. */
static const float kLaserColor[4] = { 0.7f, 0.0f, 0.0f, 1.0f };

/* --- PLAYER ANIMS (wired 2026-06-10 s24 — FINDINGS "ANIM ID MAPPING":
 *     the anim id IS the container index in the player clip library
 *     chunk28/f01_id3c, so these ids are the EMDL clip-table ids the
 *     re-exported player.emdl carries; requests go through em_game's
 *     anim mailbox, the native +0x1F2/+0x20C commit path) ------------- */
#define WPN_ANIM_DRAW    0x110  /* draw, 20 fr — property-table rate 1.4
                                 * (true directory length, 2026-06-11)   */
#define WPN_ANIM_HOLSTER 0x111  /* holster, 20 fr, rate 1.0              */
#define WPN_ANIM_RELOAD  0x11B  /* reload, 60 fr, rate 1.0 — THE TRUE
                                 * RELOAD CLIP, decoded 2026-06-11 from
                                 * func_0016F600's reload entry: it
                                 * requests D_00248B98[sub] (sub 0 ->
                                 * 283 = 0x11B, the slot right after
                                 * the sub-0 aim ladder 0x112..0x11A;
                                 * stance B uses D_00248C78 -> 0x193).
                                 * The old 0x33 was the ACTION CODE
                                 * (+0x1F0) — clip 51 is actually a
                                 * knockdown/stagger (motion-audited:
                                 * the body folds to the ground), the
                                 * EXACT clip the user kept seeing.
                                 * 283 motion-audited: root planted,
                                 * gun stays shouldered (y ~14.2), the
                                 * support hand leaves the grip for
                                 * the mag work (inter-hand 2.5 -> 6.9
                                 * -> 2.1) — a shouldered tactical
                                 * reload.                              */
#define WPN_ANIM_AIM     0x112  /* SPR4 sub-0 aim-pose ladder BASE (the
                                 * level-pitch step of D_00248B70[0] ->
                                 * 0x112..0x11A); HELD while in the AIM
                                 * state via em_game_anim_hold — the
                                 * pitch-step blend (+0x278) is not
                                 * translated yet                        */
#define WPN_DRAW_RATE    1.4f   /* D_00248C90[0x110].rate_scale          */
/* FIRE RECOIL (decoded s25 — FINDINGS "FIRE ANIM MECHANISM"; corrects
 * the s23 guess that 0x31/0x32/0x34/0x35 name fire CLIPS): the engine
 * has NO separate fire clip. Those four values are the armed-stance
 * ACTION CODES at player +0x1F0 (stance +0x05 0x1D->0x31, 0x1E->0x32,
 * 0x1F->0x34, 0x20->0x35 — set by func_0016F600 on stance entry, NOT
 * by the semi/burst/auto fire families, which only READ the code to
 * pick the fire sound: 0x164 for 0x31/0x34, 0x165 for 0x32/0x35).
 * Containers 49/50/52/53 in the clip library are unrelated neighbors
 * (49/50 are 110/79-frame root-travel locomotion clips — verified, not
 * recoil snaps). The recoil: while the code is 0x31/0x34 the per-bone
 * publisher (bone_matrix_publish -> anim_clip_arbiter, f13 = (float)
 * lh(+0x276)) re-seeds the COMMITTED aim-ladder clip every frame with
 * sample time = the fire counter — 0 at every shot, +2/frame. The
 * snap is baked into the FRONT frames of the aim-pose clip (0x112:
 * 4.0 deg/frame over frames 0..4 decaying to a 0.9 deg/frame static
 * tail), so each shot replays the clip from frame 0 at 2 frames/tick
 * and settles back into the clamped hold. The property-table rate
 * (D_00248C90[0x112].rate_scale = 1.0, extracted) does not drive this
 * path — the counter step does. */
#define WPN_FIRE_RATE    2.0f   /* +0x276 counter step: the recoil
                                 * playhead gains 2 clip-frames/tick    */

/* --- FLASHLIGHT (em_weapon.h "FLASHLIGHT" block; SQUARE while aiming,
 *     attachment 0 — the engine's D_00810D3C flag: a PERSISTENT
 *     PREFERENCE, replayed on every rifle draw, NO timer; corrected
 *     2026-06-11 user-fidelity pass against the real game). --------- */

/* SHOULDER-LIGHT BURST (the SEPARATE s28b system — the player +0xA
 * light byte family: 300-frame auto-off burst, anim/sound id 0x15D,
 * zero battery drain). KEPT but UNHOOKED: the old port wiring ran this
 * burst off the Square toggle, which made the gun light "run out" —
 * the real game's gun light never does (user-attested against the
 * original). Nothing arms the burst until the L3 stealth-light input
 * path is decoded; the tick + expiry code below stays faithful for
 * that day. */
#define WPN_LIGHT_FRAMES   300    /* +0x28 = 0x12C (5 s @ 60 Hz)        */
#define WPN_ANIM_LIGHT_OFF 0x15D  /* auto-off gesture (engine commits it
                                   * via the clip arbiter, blend 8.0)   */
#define WPN_SFX_LIGHT_OFF  0x15Du /* the SAME id is the pinned 920 ms
                                   * switch sound (soundmap snd_0361);
                                   * local define — em_sfx.h untouched  */

/* FLASHLIGHT SPOT (the port's documented DEVIATION — em_weapon.h
 * "RENDERING" / em_gfx.h "Flashlight spot light": the engine renders
 * NOTHING for the toggle, so these are PORT VALUES, flagged, not
 * decoded constants — tuned 2026-06-11 against the user's reference
 * capture of the original). Pose = the hand-frame muzzle ray (the
 * laser's anchor and axis; yaw fallback without the clips). The
 * reference shows a SHARP-EDGED projected disc on the wall (a tight
 * cone, crisp rim) — so the cone is narrow (~12-degree half-angle)
 * with a 1-degree smoothstep rim: a hard-cut disc with a slightly
 * soft edge, not the old 15->25-degree wash. Visible ONLY in the AIM
 * phase, the laser's own gate. */
/* CONE ANGLE — ASSET-DERIVED (2026-06-11, the light-cone hunt): the
 * global chunk27 library carries a LIGHT-CONE mesh family (entries
 * 0x10/0x11/0x16 — decomp FINDINGS "LIGHT-CONE MESH FAMILY"): apex at
 * the origin opening to radius 25.0 at z = 200 -> half-angle
 * atan(25/200) = 7.13 deg, length 200 = this falloff range. The spot
 * cone adopts the asset's own angle: the projected disc at the typical
 * ~30-unit aim-camera wall distance is 2*30*0.125 = 7.5 units across
 * = 2.5x the 3-unit laser dot (the reference's "2-3x dot" size; the
 * old 12-deg cone read ~2x too big). 1-deg smoothstep rim INSIDE the
 * asset angle keeps the crisp disc edge. */
#define WPN_LIGHT_RANGE   200.0f  /* = the cone mesh's 200-unit length  */
#define WPN_LIGHT_TAN     0.125f  /* tan 7.13 deg = 25/200 (the asset)  */
#define WPN_LIGHT_COS_IN  0.99428f /* cos 6.13 deg — full-bright disc   */
#define WPN_LIGHT_COS_OUT 0.99223f /* cos 7.13 deg — the asset's edge   */
static const float kLightColor[3] = { 1.00f, 0.95f, 0.82f };
/* The VISIBLE CONE (assets/fx/light_cone.emdl = chunk27 entry 0x10,
 * export_props --cone): drawn additively from the muzzle tip along the
 * aim ray through the beam pass's triangle queue, sampling the cone's
 * own glow sheet (slot FX_TEX_CONE). The sheet's planar projection
 * fades the cone toward its wide end — bright at the gun, dissolving
 * mid-air, the reference's visible beam. Intensity is PORT-TUNED (the
 * sheet interior is faint; the engine stacks shells 0x10/0x11/0x16 —
 * the multiplier stands in for the stack, flagged). */
#define WPN_CONE_GAIN     3.0f
#define WPN_CONE_FILE     "assets/fx/light_cone.emdl"

/* --- FIRE SUB-STATE MACHINE (engine +0x07; decoded 2026-06-11 from the
 *     func_00170A60 .s — em_weapon.h "FIRE SUB-STATE MACHINE"). The
 *     semi family HOLDS through the cadence: per-press shots are rate-
 *     gated exactly like full-auto. ---------------------------------- */
enum {
    WPN_SUB_WAIT = 0, /* engine 0:    trigger wait; L3 reload honored   */
    WPN_SUB_SEMI,     /* engine 0xB:  semi cadence (+0x2A press queue)  */
    WPN_SUB_BURST,    /* engine 0x16: burst cadence (+0x28 round count) */
    WPN_SUB_GAP,      /* engine 0x17: 8-tick burst gap; L3 honored      */
    WPN_SUB_AUTO      /* engine 0x1F: auto cadence (held -> refire)     */
};

/* --- Fallback state windows (flagged; used ONLY when the loaded player
 *     EMDL lacks the clip — anim_ticks() prefers the honest clip
 *     length from em_game_anim_frames) --------------------------------- */
#define WPN_DRAW_FRAMES    15   /* anim 0x110 length stand-in (0.25 s)     */
#define WPN_HOLSTER_FRAMES 15   /* anim 0x111 length stand-in              */
#define WPN_RELOAD_FRAMES  60   /* anim 0x11B true length stand-in (1 s)   */
#define WPN_RELOAD_MAG_TICK 30  /* MAG-ACTION sound 0x168 offset into the
                                 * reload window: ~0.5 s after the reload
                                 * start (s29 live capture) = 30 ticks at
                                 * 60 Hz; clamped inside a shorter window  */
/* FIRE-CHAIN TAIL (s29 live capture — scheduled like the reload's 0x168):
 * the wall impact 0x189 lands ~2 frames after the fire sound. The fire
 * event already spends one frame in the gun mailbox (shot at T, ray
 * resolved at T+1), so the resolve arms ONE more tick and the sound
 * plays at T+2 — the engine's observed latency by construction. */
#define WPN_IMPACT_SFX_TICKS 1  /* resolve(T+1) + 1 tick -> 0x189 at T+2  */
#define WPN_CASING_TICKS    42  /* shell casing 0x16A hits the floor
                                 * ~0.7 s = 42 ticks after EACH shot      */
#define WPN_CASING_SLOTS     8  /* pending casings: the 6-tick full-auto
                                 * cadence keeps ceil(42/6) = 7 in flight */

/* --- MUZZLE / HAND-FRAME ANCHORING (decoded 2026-06-11 from the local
 *     boot ELF: func_00188630 disasm + the offset tables at vram
 *     0x0024A220 / 0x0024A2A0; FINDINGS "WEAPON SYSTEM" sec. 6 + the
 *     s23 laser decode).
 *
 * The engine derives the muzzle from the published HAND-BONE matrix M
 * (player +0x90, copied to the equipment blob every gun tick — the s8
 * "equipment draw matrix == bone matrix" mechanism). The port's
 * player.emdl attaches the rifle models to skeleton node 4 with
 * IDENTITY local offsets (export_props ATTACHMENTS, live-frame proof),
 * so the evaluated palette's node-4 matrix IS that hand matrix:
 *
 *   ray origin   gun+0xA0 = M * (-3,      tbl.y, 0)
 *   barrel tip   gun+0xB0 = M * D_0024A220[idx]   (also the muzzle-
 *                           flash anchor: func_00187CC0 copies +0xB0)
 *   fire dir     gun+0xC0 = normalize(tip - origin)
 *
 * idx for sub-weapon 0 remaps by the aim option D_00810CA4 (0 -> row 7,
 * 2 -> row 6, else row 0); the port is the manual-aim default (option
 * 0) -> row 7 = (6.0, 1.088, 0, 1). Both points share tbl.y and z = 0,
 * so the fire direction is EXACTLY the hand bone's local +X axis — the
 * camera-aim relationship is carried by the ANIMATION (the aim-pose
 * ladder points that axis along the player's aim yaw; the pitch-step
 * blend +0x278 would add camera pitch, untranslated like the rest of
 * the vertical aim). The laser BEAM draw starts at gun+0x1F0 =
 * M * D_0024A2A0[0] = M * (3.6, 0.5, 0) — on the barrel just behind
 * the tip — while the RAY runs from the (-3, 1.088, 0) origin
 * (func_001854E0/760 head; s23). The hand matrix reaches em_weapon
 * through em_gfx_last_skinned_bone (the gfx-side bone publish — one
 * frame of latency by construction, see em_gfx.h). */
#define WPN_HAND_NODE   4u      /* rifle attach node (s9 attach decode)  */
#define WPN_MUZ_Y       1.088f  /* D_0024A220[7].y                       */
#define WPN_MUZ_X_RAY  -3.0f    /* ray-origin local x (func_00188630)    */
#define WPN_MUZ_X_TIP   6.0f    /* D_0024A220[7].x — the barrel tip      */
#define WPN_BEAM_X      3.6f    /* D_0024A2A0[0] — laser beam-draw start */
#define WPN_BEAM_Y      0.5f
#define WPN_MUZZLE_HEIGHT  12.0f /* FALLBACK ONLY (flagged): chest height
                                  * along the yaw when the loaded player
                                  * EMDL has no weapon clips (and so no
                                  * trustworthy hand bone to read)        */

/* --- MUZZLE FLASH (decoded 2026-06-11: func_00187CC0 -> the class-0xC
 *     FX actor func_001F4F40 / behavior func_001F5040, variant 0 for
 *     the SPR4) ---------------------------------------------------------
 * The engine spawns an FX actor at the barrel tip: chunk27 library
 * model 0x0D at init (a ~1.8-unit-radius radial puff), model 0x08 for
 * ticks 0..2 (a 4.9-unit forward star along local +X, +-2.3 radial,
 * with a func_001F4F90(2.4) line-burst pass), model 0x07 at tick 3
 * (same star shape), freed at tick 15; scale starts 0.15 + 0.05*rand01
 * and grows by a decaying velocity (vel 0.15, *0.8 per tick).
 * TEXTURES (exported 2026-06-11, export_props --fx — correcting the
 * s43 "one sheet" note): models 0x0D and 0x07 sample the 64x32 sheet
 * 0x...4220A0 full-frame (flash_puff.emtx); model 0x08's forward
 * streak samples its OWN 64x32 sheet 0x...4220A4 (flash_star.emtx)
 * and its muzzle-base radial cross (X ~ 0, +-1.3 YZ) a 32x32 sheet
 * 0x...4221E6 (flash_ball.emtx). The port draws the engine's own
 * model-per-tick schedule as textured additive billboards through the
 * beam pass (em_gfx_beam_tex / _dot_tex): spawn tick = the model-0xD
 * radial puff (camera-facing, puff sheet); ticks 0..2 = the model-8
 * forward streak (axial quad along the gun axis, star sheet) + muzzle
 * ball (camera-facing, ball sheet); tick 3+ = the model-7 star (the
 * puff sheet on the streak quad) — all scaled by the live FX scale,
 * intensity decaying with the engine's own 0.8^t velocity constant
 * (the stand-in for the untranslated rotation lerp, flagged). With
 * the .emtx files absent the old flat-color core + streak fallback
 * draws instead. */
#define WPN_FLASH_TICKS    16    /* FX lifetime: freed at tick 15        */
#define WPN_FLASH_S0       0.15f /* initial scale (+ 0.05 * rand01)      */
#define WPN_FLASH_S0_RND   0.05f
#define WPN_FLASH_VEL      0.15f /* scale velocity, *0.8 per tick        */
#define WPN_FLASH_DECAY    0.8f
#define WPN_FLASH_STAR_LEN 4.9f  /* model 0x08 +X extent (measured)      */
#define WPN_FLASH_STAR_W   4.6f  /* model 0x08 radial extent (2 x 2.3)   */
#define WPN_FLASH_CORE     3.6f  /* model 0x0D radial footprint          */
#define WPN_FLASH_BALL     2.6f  /* model 0x08 muzzle-cross extent
                                  * (2 x 1.3 — the ball-sheet records)   */

/* --- FX SPRITE TEXTURES (the assets/fx .emtx files -> the em_gfx beam-
 *     slots; exported by the decomp repo's export_props.py --fx).
 *     .emtx v1: "EMTX", u32 version=1, u32 w, u32 h, w*h*4 RGBA8 rows
 *     top-down. Loaded lazily on the first em_weapon_render with a
 *     device; a missing/bad file leaves its slot unregistered and the
 *     drawers fall back to the old flat-color primitives — a missing
 *     asset never regresses the frame. ------------------------------- */
enum {
    FX_TEX_DOT  = 0,    /* laser_dot.emtx  — func_001CD520 dot sprite  */
    FX_TEX_PUFF = 1,    /* flash_puff.emtx — flash models 0x0D/0x07    */
    FX_TEX_STAR = 2,    /* flash_star.emtx — model 0x08 forward streak */
    FX_TEX_BALL = 3,    /* flash_ball.emtx — model 0x08 muzzle cross   */
    FX_TEX_CONE = 4     /* light_cone.emdl's embedded glow sheet — the
                         * flashlight cone (chunk27 entry 0x10)        */
};
static const char *const kFxFiles[4] = {
    "assets/fx/laser_dot.emtx", "assets/fx/flash_puff.emtx",
    "assets/fx/flash_star.emtx", "assets/fx/flash_ball.emtx"
};

/* --- KNIFE / MELEE constants (em_weapon.h "KNIFE / MELEE"; decoded
 *     2026-06-10 s36 — FINDINGS "KNIFE/MELEE DECODED". All table values
 *     are the boot-ELF row idx 0; idx 1 is the alternate-context row
 *     (+0x236), untranslated) ------------------------------------------ */
#define MELEE_ANIM_L1     0x10B  /* light hit 1, 35 fr (D_00248690[0][0]) */
#define MELEE_ANIM_L2     0x10C  /* light hit 2, 35 fr (D_00248690[0][1]) */
#define MELEE_ANIM_L3     0x10D  /* light hit 3, 50 fr (D_00248690[0][2]) */
#define MELEE_ANIM_HEAVY  0x10E  /* heavy stab, 50 fr  (D_002754A8[0])    */
#define MELEE_ANIM_RECOV  0x10F  /* hit-confirm recover, 25 fr (state
                                  * 0x51 idx-0 arm; blend 4.0)            */
/* Impact gates T (the +0x3C c.le.s thresholds; impact_tick =
 * max(3, len - T) — the down-count reading, see the header's TIMING
 * NOTE; 3 covers the request->commit mailbox latency + blend-in). */
#define MELEE_GATE_L1     24.0f  /* D_002486A0[0]                         */
#define MELEE_GATE_L2     26.0f  /* D_002486A8[0]                         */
#define MELEE_GATE_L3     41.0f  /* D_002486B0[0]                         */
#define MELEE_GATE_HEAVY  43.0f  /* D_00248700[0]                         */
/* Chain windows: the buffered (+0x2E) next hit starts when the clip
 * time passes len - 19 (D_002486D0/D4 idx 0, both 19.0). */
#define MELEE_CHAIN_TAIL  19.0f
#define MELEE_DMG_L1      3      /* func_001735C0 state-1 +0x36 write     */
#define MELEE_DMG_L2      3      /* state-2 write                         */
#define MELEE_DMG_L3      5      /* state-3 write                         */
#define MELEE_DMG_HEAVY   15     /* func_00173E60 (0xF)                   */
#define MELEE_RECOV_PAUSE 4      /* engine state 0x50: +0x28 = 4 ticks    */
#define MELEE_RECOV_RATE  1.0f   /* property-table rate (1.0; the 4.0 the
                                  * engine passes the arbiter is BLEND)   */
/* Fallback clip lengths (flagged; used only when the player EMDL lacks
 * the clip — the values ARE the disc clip lengths, baked as stand-ins;
 * CORRECTED 2026-06-11 to the true directory-id lengths: the s36
 * numbers were the pre-directory-fix bake's shifted neighbors). */
#define MELEE_L1_FRAMES    35
#define MELEE_L2_FRAMES    35
#define MELEE_L3_FRAMES    50
#define MELEE_HEAVY_FRAMES 50
#define MELEE_RECOV_FRAMES 25
/* RANGE — PORT STAND-IN (the engine has no player-side knife range: the
 * damage goes to the melee-target link's +0x36 mailbox and the TARGET-
 * side polls do the range work, e.g. func_00219870's func_0019AA80
 * probe). 12.0 = the engine's documented hands-reach (the use-scan
 * dist^2 <= 144, func_0019A910 mode 6, s17); the cone is a port value. */
#define EM_MELEE_REACH    12.0f
#define MELEE_CONE        0.5f   /* cos 60 deg frontal arc                */

static struct {
    uint8_t state;       /* EM_WPN_* (player major-state byte +0x06)      */
    uint8_t fire_mode;   /* D_00810C61: 0 semi / 1 burst-3 / 2 full-auto  */
    uint8_t mag;         /* D_00810C62                                    */
    int16_t reserve;     /* D_00810CB4 — TOTAL pool including the mag     */

    int     timer;       /* DRAW/RELOAD/HOLSTER frames remaining          */
    int     mag_sfx;     /* RELOAD ticks until the mag-action sound 0x168
                          * (0 = none pending; cancelled by a stance drop) */
    int     fire_sub;    /* WPN_SUB_* — the engine's fire sub-state byte
                          * +0x07 (family position collapsed)             */
    int     fire_next;   /* a chained shot is armed for the NEXT tick
                          * (the engine cadence states step BACK to the
                          * fire sub-state, which shoots one tick later)  */
    int     counter;     /* +0x276 fire counter (+2/frame incl. the shot
                          * tick, shot at expiry >= interval)             */
    float   interval;    /* +0x2F4 fire interval: semi = the aim-ladder
                          * clip length (func_0017A8B0 per press), burst/
                          * auto rounds overwrite 12.0 (engine 0x15/0x1E) */
    int     laser_vis;   /* player +0x2F2 (mirror D_008105A2) — the
                          * LASER-VISIBLE flag of the fire SM: set every
                          * WAIT tick and at the cadence EXPIRY, CLEARED
                          * by every shot state -> the laser hides for
                          * the cadence of each shot (FINDINGS "LASER
                          * HIDE WINDOW"); the gun-tick drawers gate on
                          * it beside the stance code/phase             */
    int     pending;     /* +0x2A queued-shot flag (semi press latched
                          * during the cadence window)                    */
    int     burst;       /* +0x28 burst counter, rounds fired this burst  */
    int     burst_pause; /* WPN_SUB_GAP ticks until the next burst        */

    int     light_on;    /* FLASHLIGHT preference flag (engine D_00810D3C:
                          * persists across aim sessions until toggled —
                          * NO timer; the spot renders only in AIM)       */
    int     shoulder_timer; /* the SEPARATE s28b shoulder-light burst
                          * countdown (+0x28 = 0x12C, ticks every frame,
                          * any stance) — KEPT but UNHOOKED: nothing arms
                          * it (see the SHOULDER-LIGHT BURST block)       */
    int     light_live;  /* the spot/cone pose below is valid for THIS
                          * frame (set while light_on && laser_on)       */
    float   light_pos[3];/* spot anchor — the barrel TIP (muzzle front,
                          * gun+0xB0; PORT visual, em_weapon.h
                          * "RENDERING")                                 */
    float   light_dir[3];/* spot axis — the muzzle ray direction          */
    int     light_cap;   /* EM_CAPTURE_LIGHT=1 one-shot: synthesize the
                          * Square toggle on the first aim frame (debug
                          * instrumentation for headless captures)        */

    int     fire_event;  /* gun actor +0x2E: posted by the player SM,
                          * consumed (ray + FX) on the NEXT update — the
                          * contract's mandatory one-frame latency        */

    int     impact_sfx;  /* ticks until the wall-impact sound 0x189
                          * (0 = none pending; armed by a WALL ray hit)   */
    float   impact_pos[3];
                         /* the armed shot's wall hit point — 0x189 plays
                          * POSITIONAL there (engine: the impact family is
                          * actor-attached at the hit, play_sound radius
                          * 300; em_sfx.h decode)                         */
    int     casing[WPN_CASING_SLOTS];
                         /* per-shot countdowns to the shell-casing
                          * sound 0x16A (0 = slot free); the casing is
                          * already in the air, so these keep ticking
                          * through reloads/holsters/stance drops        */

    int     flash;         /* muzzle-flash ticks remaining (16 -> 0; the
                            * FX actor's own life, free at tick 15)       */
    float   flash_pos[3];  /* flash anchor = hand * (6, 1.088, 0) — the
                            * engine FX copies gun+0xB0 at spawn          */
    float   flash_dir[3];  /* gun axis at the shot (the +X star axis)     */
    float   flash_scale;   /* FX scale: 0.15 + 0.05*rand01 at spawn       */
    float   flash_vel;     /* scale velocity: 0.15, *0.8 per tick         */
    float   flash_rot;     /* FX rotation (+0x80 triple, DEGREES; variant
                            * 0 starts 0): engine tick >= 4 lerps
                            * rot += (-128 - rot) * 0.35 per tick — the
                            * star tumbles around the barrel as it dies
                            * (func_001F5040 .L001F53A8, translated)      */
    int     last_hit;    /* last resolved shot: 1/0; -1 = none yet        */

    int     shots;       /* introspection: rounds fired since reset       */
    int     reloads;     /* introspection: reloads since reset            */

    /* LASER SIGHT — the gun-side per-frame raycast result (the engine
     * stores the clipped endpoint + flags in the gun actor's +0x1F0
     * block; func_001854E0/760 refresh it every aim frame). */
    int      laser_on;     /* gun +0x210 "laser active" flag              */
    int      laser_surf;   /* 1 = endpoint is a WORLD surface hit (the
                            * dot billboard offsets along its normal)     */
    float    laser_n[3];   /* hit normal (world surface) or -dir (enemy)  */
    float    laser_a[3];   /* BEAM DRAW start (gun +0x1F0 vec) — the
                            * hand-frame (3.6, 0.5, 0) point; the RAY
                            * itself runs from the (-3, 1.088, 0) origin  */
    float    laser_b[3];   /* clipped endpoint (gun +0x200 vec)           */
    uint32_t rng;          /* flicker LCG (engine: func_00122BB8 rand)    */

    EmGfx   *gfx;          /* cached each em_weapon_render call: the
                            * update stage reads the published hand-bone
                            * matrix through em_gfx_last_skinned_bone     */
} w;

/* KNIFE / MELEE state (engine player modes 0x21/0x22 — see the header
 * block; the engine keeps this in the player struct, the port in the
 * same module-level singleton style as the rifle). */
static struct {
    int state;       /* EM_MELEE_* (the engine's +0x05 mode + +0x06
                      * major collapsed: IDLE / SWING / RECOVER)        */
    int heavy;       /* 1 = mode 0x22 (heavy stab), 0 = mode 0x21      */
    int combo;       /* light combo hit 1..3 (engine major +0x06)      */
    int tick;        /* ticks into the current swing / recover window  */
    int len;         /* current swing window, ticks (= clip frames)    */
    int impact;      /* impact tick (max(3, len - T))                  */
    int chain_at;    /* chain-window tick (len - 19); 0 = no chain     */
    int buffered;    /* +0x2E: FIRE pressed during the swing           */
    int confirm;     /* target +0x0A read-back: a victim was struck    */
    int recov_pause; /* engine 0x50/0x51 countdown before anim 0x10F   */
    int swings;      /* introspection: attacks started since reset     */
    int hits;        /* introspection: impact-tick victims since reset */
} m;

/* The flicker random source — engine func_00122BB8 is the C-library
 * rand(); a freestanding LCG keeps the port deterministic per run. */
static uint32_t wpn_rand(void)
{
    w.rng = w.rng * 1103515245u + 12345u;
    return (w.rng >> 16) & 0x7FFF;
}

void em_weapon_reset(uint8_t mag, int16_t reserve)
{
    memset(&w, 0, sizeof w);
    memset(&m, 0, sizeof m);
    w.state    = EM_WPN_HOLSTERED;
    w.mag      = mag;
    w.reserve  = reserve;
    w.last_hit = -1;
    /* EM_CAPTURE_LIGHT=1: arm the one-shot synthetic flashlight toggle
     * (em_weapon.h "RENDERING" — debug instrumentation only; it rides
     * the exact Square-press code path on the first aim frame). */
    const char *cl = getenv("EM_CAPTURE_LIGHT");
    w.light_cap = cl && cl[0] == '1';
}

/* HONEST state windows from the committed clip lengths: the scripted
 * clip plays at `rate` frames/tick and em_game's commit clears it when
 * the clip time passes (frames - 1) + 1, i.e. after ceil(frames / rate)
 * ticks — the state timer matches that exactly, so the state machine
 * and the visible anim end together (one frame of request->commit
 * latency aside, the engine's own mailbox latency). A player EMDL
 * without the clip falls back to the flagged stand-in window. */
static int anim_ticks(unsigned clip_id, float rate, int fallback)
{
    int fc = em_game_anim_frames(clip_id);
    if (fc <= 0) return fallback;
    return (int)ceilf((float)fc / rate);
}

int em_weapon_draw_ticks(void)
{
    return anim_ticks(WPN_ANIM_DRAW, WPN_DRAW_RATE, WPN_DRAW_FRAMES);
}

int em_weapon_reload_ticks(void)
{
    return anim_ticks(WPN_ANIM_RELOAD, 1.0f, WPN_RELOAD_FRAMES);
}

int em_weapon_holster_ticks(void)
{
    return anim_ticks(WPN_ANIM_HOLSTER, 1.0f, WPN_HOLSTER_FRAMES);
}

/* func_0017B300(_, mode) — MATCHED 100% in the decomp repo (mirrored
 * branch-for-branch, 2026-06-11 fidelity pass). mode 0 = only if the
 * mag is empty (and the reserve is live); mode 1 = unconditional (any
 * live reserve); else TOP-UP — gated on mag < 30 AND reserve > mag (a
 * full mag ignores the manual reload — the engine mode-2 rule). The
 * reserve is NOT subtracted (it is the total pool — each shot already
 * decremented both). Returns 0 = reloaded, 1 = nothing to do.
 *
 * Top-up fill quirk (engine-faithful): the matched C compares the
 * reserve against the rounds NEEDED (30 - mag), not against 30 — with
 * 30-mag <= reserve < 30 the engine writes mag = 30 even though the
 * pool holds fewer rounds. Replicated verbatim; it never triggers with
 * a healthy reserve and keeps the port byte-faithful when it does. */
static int weapon_reload(int mode)
{
    if (mode == 0) {
        if (w.reserve == 0 || w.mag != 0) return 1;
        w.mag = (uint8_t)(w.reserve < WPN_MAG_MAX ? w.reserve
                                                  : WPN_MAG_MAX);
        return 0;
    }
    if (mode == 1) {
        if (w.reserve == 0) return 1;
        w.mag = (uint8_t)(w.reserve < WPN_MAG_MAX ? w.reserve
                                                  : WPN_MAG_MAX);
        return 0;
    }
    if (w.mag >= WPN_MAG_MAX || w.reserve <= w.mag) return 1;
    w.mag = (uint8_t)(w.reserve < (WPN_MAG_MAX - w.mag) ? w.reserve
                                                        : WPN_MAG_MAX);
    return 0;
}

/* HOLSTER entry (engine major state 0x65): anim 0x111 + sound 0x163 —
 * every transition into the holster ramp goes through here, exactly the
 * single state-entry the engine plays the sound and anim on. */
static void weapon_enter_holster(void)
{
    em_sfx_play(EM_SFX_WPN_HANDLE);     /* 0x163, state-0x65 entry (the
                                         * shared weapon-handling foley) */
    em_game_anim_request(WPN_ANIM_HOLSTER, 1.0f);   /* anim 0x111 */
    w.state     = EM_WPN_HOLSTER;
    w.timer     = em_weapon_holster_ticks();
    w.mag_sfx   = 0;    /* stance drop mid-reload: no mag action played */
    w.fire_sub  = WPN_SUB_WAIT;
    w.fire_next = 0;
}

/* RELOAD entry (engine major state 3): anim 0x33 gates firing for the
 * clip's length; the ammo move (func_0017B300) already happened at the
 * call site. Both entries (dry-mag auto-reload and the manual top-up)
 * go through here. */
static void weapon_enter_reload(void)
{
    /* RELOAD SOUNDS (live-pinned s29): 0x163 at the reload START (the
     * shared weapon-handling foley — same id the holster plays), then
     * 0x168 at the MAG ACTION ~0.5 s in (the distinctive reload sound),
     * scheduled below and ticked by the RELOAD state. */
    em_sfx_play(EM_SFX_WPN_HANDLE);
    /* HOLD-type request (2026-06-11 reload-stagger fix): a plain
     * request releases the clip at its end — with the commit latency
     * that left ~2 frames of LOCOMOTION IDLE between the reload's last
     * frame and the re-held aim pose: a full-pose snap to idle and
     * back, the user-visible "stagger" at every reload (runtime-traced:
     * the clip itself commits and plays 0x33 frames 1..56 correctly).
     * The engine has no such interlude — its stance top re-selects the
     * stance pose every frame — so the port clamps the reload's last
     * frame until weapon_enter_aim's hold replaces it. */
    em_game_anim_hold(WPN_ANIM_RELOAD, 1.0f);       /* anim 0x11B */
    w.reloads++;
    w.pending   = 0;
    w.burst     = 0;
    w.fire_sub  = WPN_SUB_WAIT;
    w.fire_next = 0;
    w.state     = EM_WPN_RELOAD;
    w.timer     = em_weapon_reload_ticks();
    w.mag_sfx   = WPN_RELOAD_MAG_TICK < w.timer ? WPN_RELOAD_MAG_TICK
                                                : w.timer - 1;
    if (w.mag_sfx < 1) w.mag_sfx = 1;
}

/* AIM entry (engine major state 2, and the post-reload re-entry): HOLD
 * the aim pose — the per-sub-weapon stance-table clip (sub 0 -> 0x112,
 * D_00248B88[0]), which the engine's armed tops keep re-selecting every
 * frame through the arbiter. em_game_anim_hold clamps the clip at its
 * last frame and keeps it committed until the next request replaces it
 * (the holster/reload clips) — the native equivalent of that persistent
 * re-selection. */
/* SEMI fire interval — the engine's func_0017A8B0 store: +0x2F4 = the
 * aim-ladder base clip's frame count (func_001C61D0), refreshed on
 * every trigger press. 25 for the SPR4's clip 0x112; the honest clip
 * length when the EMDL carries it, the true-length fallback otherwise. */
static float semi_interval(void)
{
    int fc = em_game_anim_frames(WPN_ANIM_AIM);
    return fc > 0 ? (float)fc : WPN_SEMI_FALLBACK;
}

static void weapon_enter_aim(void)
{
    /* The hold plays the clip's front settle once at the counter rate
     * (the engine zeroes +0x276 on stance entry, so the pose clip's
     * recoil/settle frames run at 2/tick there too) and clamps. */
    em_game_anim_hold(WPN_ANIM_AIM, WPN_FIRE_RATE); /* aim pose 0x112 */
    w.state     = EM_WPN_AIM;
    w.counter   = 0;                /* engine: +0x276 = 0 at stance entry;
                                     * the first press fires immediately
                                     * because the WAIT sub-state has no
                                     * counter test (func_00170A60 st 0) */
    w.fire_sub  = WPN_SUB_WAIT;
    w.fire_next = 0;
    w.interval  = semi_interval();  /* +0x2F4 ladder-clip default        */
    w.laser_vis = 1;                /* +0x2F2: the first WAIT tick sets
                                     * it — the port sets it at entry
                                     * (same frame the engine's WAIT
                                     * head runs)                        */
}

/* One SHOT — the common per-shot block of the fire sub-machine (engine
 * states 0xA/0x15/0x1E). Callers gate the ammo (the engine's per-shot
 * states are only entered with a live mag — dry handling lives in the
 * WAIT press and the cadence expiry, see weapon_fire_logic). A live
 * round: mag-- AND reserve-- (the TOTAL-pool consume path), post the
 * fire event to the gun (+0x2E = 1), seed the fire counter with the
 * shot tick's own cadence increment (+0x276: the engine fire states
 * fall THROUGH into the cadence head, so the counter reads 2 after a
 * shot — and the counter reset IS the recoil restart, WPN_FIRE_RATE)
 * and play the stance fire sound (the block reads the +0x1F0 stance
 * code: 0x164 for 0x31/0x34, 0x165 for 0x32/0x35; the port's single
 * stance is the 0x1D family -> code 0x31 -> 0x164). */
static void weapon_shot(void)
{
    if (w.mag == 0) return;             /* guard; callers gate the ammo */
    w.laser_vis = 0;      /* +0x2F2 = 0: every engine shot state hides
                           * the laser until the cadence expiry (the
                           * decoded LASER HIDE WINDOW — the original
                           * laser vanishes during the shot)             */
    w.mag--;
    w.reserve--;          /* the engine's TOTAL-pool rule: BOTH, per shot */
    w.shots++;
    w.fire_event = 1;     /* gun +0x2E — resolved next update             */
    w.counter    = WPN_COUNT_STEP;      /* the fall-through increment     */
    /* RECOIL: the counter reset rewinds the held aim clip to frame 0 —
     * the engine's publisher samples the committed ladder clip at
     * frame = +0x276 every tick (FIRE RECOIL block above). The replay
     * runs 25 frames @ 2/tick = 12.5 ticks; at the 6-tick full-auto
     * cadence the next shot restarts it from frame 12, so sustained
     * fire only ever shows the clip's front half — engine-identical
     * overlap by construction (the counter never passes the interval). */
    em_game_anim_hold_restart(WPN_ANIM_AIM, WPN_FIRE_RATE);
    em_sfx_play(EM_SFX_WPN_FIRE);         /* 0x164, the per-shot block    */
    /* SHELL CASING (s29): every shot ejects a casing that hits the
     * floor ~0.7 s later — arm a 42-tick countdown to 0x16A (a free
     * slot always exists: 8 slots vs 7 in flight at the auto cadence;
     * a saturated table drops the oldest-pending overlap harmlessly). */
    for (int k = 0; k < WPN_CASING_SLOTS; k++) {
        if (w.casing[k] == 0) {
            w.casing[k] = WPN_CASING_TICKS;
            break;
        }
    }
}

/* Transform a hand-frame point by a column-major 4x4 (w = 1) — the
 * native func_001026A0 (matrix * point). */
static void hand_point(const float m[16], float x, float y, float z,
                       float out[3])
{
    out[0] = m[0] * x + m[4] * y + m[8]  * z + m[12];
    out[1] = m[1] * x + m[5] * y + m[9]  * z + m[13];
    out[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
}

/* Fetch the published hand-bone matrix (the engine's player +0x90; the
 * port reads the player palette's node-4 matrix recorded by the gfx
 * layer — see the MUZZLE/HAND-FRAME block above). Trust it only when
 * the loaded player EMDL actually carries the weapon clips: that
 * guarantees the player model is loaded and is the render chain's LAST
 * skinned draw (the recorded palette is the player's). Returns 0 (use
 * the flagged fallback) otherwise. */
static int weapon_hand_matrix(float m[16])
{
    if (!w.gfx) return 0;
    if (em_game_anim_frames(WPN_ANIM_AIM) <= 0) return 0;
    return em_gfx_last_skinned_bone(w.gfx, WPN_HAND_NODE, m);
}

/* Muzzle ray = the engine's two-table-point form (func_00188630):
 * origin = M * (-3, 1.088, 0), dir = normalize(M * (6, 1.088, 0) -
 * origin) = the hand bone's +X axis. Optional `tip` receives the
 * barrel-tip point (gun +0xB0 — the muzzle-flash anchor). Fallback
 * (flagged, EMDL without the weapon clips): chest height along yaw. */
static void weapon_muzzle_ray(const float pos[3], float yaw,
                              float muzzle[3], float dir[3], float tip[3])
{
    float m[16];
    if (weapon_hand_matrix(m)) {
        float t[3];
        hand_point(m, WPN_MUZ_X_RAY, WPN_MUZ_Y, 0.0f, muzzle);
        hand_point(m, WPN_MUZ_X_TIP, WPN_MUZ_Y, 0.0f, t);
        float dx = t[0] - muzzle[0];
        float dy = t[1] - muzzle[1];
        float dz = t[2] - muzzle[2];
        float dl = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dl > 1e-4f) {
            dir[0] = dx / dl;
            dir[1] = dy / dl;
            dir[2] = dz / dl;
            if (tip) memcpy(tip, t, 3 * sizeof(float));
            return;
        }
        /* degenerate bone (zero-scale) — fall through to the stand-in */
    }
    muzzle[0] = pos[0];
    muzzle[1] = pos[1] + WPN_MUZZLE_HEIGHT;
    muzzle[2] = pos[2];
    dir[0]    = sinf(yaw);
    dir[1]    = 0.0f;
    dir[2]    = cosf(yaw);
    if (tip) {
        tip[0] = muzzle[0];
        tip[1] = muzzle[1];
        tip[2] = muzzle[2];
    }
}

/* LASER SIGHT raycast — the per-aim-frame half of func_001854E0/760:
 * the SAME segment query as the bullet (mode 7, mask 0x20) from the
 * muzzle along the fire direction, range 260; the laser clips at the
 * hit point and keeps drawing to the full 260-unit endpoint on a miss.
 * The engine's query reports the hit ACTOR in the scratchpad result
 * (*0x700031D4 — it even tags the victim's +0x0A "laser on me" byte);
 * the port's split runs em_enemy_ray_test beside the world query and
 * the NEAREST of enemy-vs-world clips the beam, exactly like the
 * bullet's victim test. */
static void laser_update(const EmCollision *coll, const float pos[3],
                         float yaw)
{
    float muzzle[3], dir[3];
    weapon_muzzle_ray(pos, yaw, muzzle, dir, NULL);

    float end[3] = { muzzle[0] + dir[0] * WPN_RANGE,
                     muzzle[1] + dir[1] * WPN_RANGE,
                     muzzle[2] + dir[2] * WPN_RANGE };

    EmCollHit h;
    w.laser_surf = 0;
    if (coll && coll->poly_count &&
        em_collision_segment_query(coll, muzzle, end, WPN_RAY_MASK,
                                   WPN_RAY_ID, &h)) {
        end[0] = h.point[0];
        end[1] = h.point[1];
        end[2] = h.point[2];
        /* surface hit: keep the plane normal — the DOT billboard sits
         * OFF the surface along it (half its size) so it never
         * half-clips into the wall (the engine's endpoint sprites sit
         * on the surface plane plus an offset). */
        memcpy(w.laser_n, h.normal, sizeof w.laser_n);
        w.laser_surf = 1;
    }
    float epoint[3];
    if (em_enemy_ray_test(muzzle, end, epoint) >= 0) {
        /* enemy inside the (already world-clipped) segment: it is the
         * nearer hit by construction — clip the laser on the victim */
        end[0] = epoint[0];
        end[1] = epoint[1];
        end[2] = epoint[2];
        /* no plane on a victim: back the dot toward the gun instead */
        w.laser_n[0] = -dir[0];
        w.laser_n[1] = -dir[1];
        w.laser_n[2] = -dir[2];
        w.laser_surf = 1;
    }
    /* The drawn beam STARTS at the gun+0x1F0 point — hand-frame
     * (3.6, 0.5, 0), on the barrel just behind the tip (func_00188630
     * computes it from M * D_0024A2A0[sub] each frame); the ray origin
     * above sits further back inside the receiver. Fallback: muzzle. */
    float m[16];
    if (weapon_hand_matrix(m))
        hand_point(m, WPN_BEAM_X, WPN_BEAM_Y, 0.0f, w.laser_a);
    else
        memcpy(w.laser_a, muzzle, sizeof w.laser_a);
    memcpy(w.laser_b, end, sizeof w.laser_b);
}

/* The gun-side fire-event consumption — func_001861C0, the BULLET.
 *
 * 1. ENDPOINT: with an acquired target the ray aims at its AIM POINT,
 *    overshot by 5 units (the engine cycles 3 screen-cone targets from
 *    func_00199220; the port's em_enemy_acquire is a distance + facing-
 *    cone stand-in — see WPN_AIM_CONE). No target: muzzle + dir*260.
 * 2. One world segment query (mask 7, id 0x20).
 * 3. VICTIM TEST before crediting the world hit: the segment against
 *    every live enemy's hit sphere (em_enemy_ray_test); the NEAREST of
 *    enemy-vs-world wins. An enemy hit applies damage through the
 *    victim's +0x36 mailbox (func_001B41F0's contract: code 5) — the
 *    enemy's own behavior consumes it next tick. Design note: the
 *    world-geometry API (em_collision) stays untouched; actor hits go
 *    through em_enemy's own ray test, the native split of the engine's
 *    "hit actor pointer in the scratchpad result block" (*0x700031D4).
 *
 * Tracer / impact-marker / shell-eject / rumble / gore FX are pending;
 * the overlay flash + crosshair pulse stand in (em_weapon.h). */
static void weapon_resolve_fire(const EmCollision *coll,
                                const float pos[3], float yaw)
{
    float muzzle[3], dir[3], tip[3];
    float end[3];
    float aim[3];
    weapon_muzzle_ray(pos, yaw, muzzle, dir, tip);

    if (em_enemy_acquire(muzzle, yaw, WPN_RANGE, WPN_AIM_CONE, aim) >= 0) {
        /* targeted shot: endpoint = aim point + 5-unit overshoot */
        float dx = aim[0] - muzzle[0];
        float dy = aim[1] - muzzle[1];
        float dz = aim[2] - muzzle[2];
        float dl = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dl > 1e-4f) {
            dir[0] = dx / dl;
            dir[1] = dy / dl;
            dir[2] = dz / dl;
        }
        end[0] = aim[0] + dir[0] * WPN_OVERSHOOT;
        end[1] = aim[1] + dir[1] * WPN_OVERSHOOT;
        end[2] = aim[2] + dir[2] * WPN_OVERSHOOT;
    } else {
        end[0] = muzzle[0] + dir[0] * WPN_RANGE;
        end[1] = muzzle[1] + dir[1] * WPN_RANGE;
        end[2] = muzzle[2] + dir[2] * WPN_RANGE;
    }

    int       hit = 0;
    int       world_hit = 0;
    EmCollHit h;
    if (coll && coll->poly_count)
        world_hit = em_collision_segment_query(coll, muzzle, end,
                                               WPN_RAY_MASK, WPN_RAY_ID,
                                               &h) != 0;

    float epoint[3];
    int   victim = em_enemy_ray_test(muzzle, end, epoint);
    if (victim >= 0 && world_hit) {
        /* nearest hit wins (the engine clamps the segment per set) */
        float wd2 = 0.0f, ed2 = 0.0f;
        for (int k = 0; k < 3; k++) {
            float dw = h.point[k] - muzzle[k];
            float de = epoint[k] - muzzle[k];
            wd2 += dw * dw;
            ed2 += de * de;
        }
        if (wd2 < ed2)
            victim = -1;           /* the wall is in front of the enemy */
    }
    if (victim >= 0) {
        /* ENEMY hit: no impact sound here — the flinch/death path owns
         * the victim's audio (the +0x36 mailbox consumer / 0x7D8). */
        em_enemy_damage(victim, WPN_HIT_DMG);  /* victim +0x36 mailbox */
        hit = 1;
    } else {
        hit = world_hit;
        /* WALL hit: arm the impact/ricochet 0x189 one tick out — with
         * the fire event's own one-frame latency the sound lands fire
         * +2 frames, the s29-observed chain (WPN_IMPACT_SFX_TICKS).
         * SURFACE VARIANTS: unpinned — 0x189 for every wall (the
         * em_sfx.h flag on the 0x188/0x18A/0x18B family). */
        if (world_hit) {
            w.impact_sfx = WPN_IMPACT_SFX_TICKS;
            memcpy(w.impact_pos, h.point, sizeof w.impact_pos);
        }
    }

    w.last_hit = hit;

    /* MUZZLE FLASH spawn — the engine calls func_00187CC0 from this
     * same gun tick: the FX actor copies the barrel-tip point (gun
     * +0xB0) and lives 16 ticks with the documented scale envelope
     * (constants block above). A new shot RESTARTS the FX — the engine
     * spawns a fresh pool actor per shot; the port keeps one slot (at
     * the 6-tick full-auto cadence the brightest window dominates). */
    w.flash       = WPN_FLASH_TICKS;
    w.flash_scale = WPN_FLASH_S0 +
                    WPN_FLASH_S0_RND * (float)wpn_rand() / 32768.0f;
    w.flash_vel   = WPN_FLASH_VEL;
    w.flash_rot   = 0.0f;   /* +0x80 rotation triple: variant 0 inits 0 */
    memcpy(w.flash_pos, tip, sizeof w.flash_pos);
    memcpy(w.flash_dir, dir, sizeof w.flash_dir);
}

/* The AIM/FIRE loop's trigger logic — the engine fire sub-machine
 * (func_00170A60 .s, re-read 2026-06-11; em_weapon.h "FIRE SUB-STATE
 * MACHINE"). The sub-state HOLDS through the 6-frame cadence in every
 * family — SEMI per-press shots are rate-gated exactly like full-auto
 * (the user-fidelity fix: the port can no longer out-shoot the engine
 * by mashing). The trigger is CIRCLE — the engine's default-config
 * fire button (config slot spad 0x3B78 = 0x0020, live-pinned s29). */
static void weapon_fire_logic(const EmFrameInput *in)
{
    /* Chained shot armed at the last cadence expiry: the engine steps
     * BACK to the fire sub-state, which shoots one tick later — exact
     * 6-frame spacing for queued-semi / burst rounds / auto refire. */
    if (w.fire_next) {
        w.fire_next = 0;
        weapon_shot();      /* counter = 2, the fall-through increment */
        return;
    }

    switch (w.fire_sub) {
        case WPN_SUB_WAIT:
            /* Engine state 0: a press fires IMMEDIATELY (no counter
             * test — WAIT is only reachable a full cadence after the
             * last shot); empty mag -> dry click 0x169 only (an empty
             * mag with a live reserve never survives to WAIT: the
             * cadence expiry below already reloaded). */
            /* WAIT tick head: the engine re-sets +0x2F2 every tick
             * here (.L00170C08) — the laser shows whenever the gun is
             * between cadences. */
            w.laser_vis = 1;
            if (in->pressed & EM_PAD_CIRCLE) {
                /* the press runs func_0017A8B0: latch + refresh the
                 * interval from the ladder clip (semi keeps it; the
                 * burst/auto FIRE states overwrite 12.0 per round —
                 * the engine 0x15/0x1E stores). */
                w.interval = semi_interval();
                if (w.mag == 0) {
                    em_sfx_play(EM_SFX_WPN_DRY);
                } else {
                    switch (w.fire_mode) {
                        case EM_WPN_MODE_BURST:
                            w.burst    = 1;     /* +0x28: rounds fired */
                            w.fire_sub = WPN_SUB_BURST;
                            w.interval = WPN_INTERVAL_AUTO;
                            break;
                        case EM_WPN_MODE_AUTO:
                            w.fire_sub = WPN_SUB_AUTO;
                            w.interval = WPN_INTERVAL_AUTO;
                            break;
                        default:
                            w.fire_sub = WPN_SUB_SEMI;
                            break;
                    }
                    w.pending = 0;
                    weapon_shot();
                }
            } else if (in->pressed & EM_PAD_L3) {
                /* Manual reload — the engine's raw L3 pad bit (NOT
                 * config-mapped), func_0017B300(.,2) TOP-UP: reloads
                 * ONLY if mag < 30 AND reserve > mag (a full mag
                 * ignores L3). Checked in the trigger-wait states
                 * only (engine 0 / 0x17), never mid-cadence. */
                if (weapon_reload(2) == 0)
                    weapon_enter_reload();
            }
            break;

        case WPN_SUB_SEMI:
            /* Engine 0xB: cadence hold at the LADDER-CLIP interval
             * (+0x2F4 = 25 for the SPR4 — see the FIRE INTERVAL block).
             * A NEW press queues ONE shot (+0x2A), but ONLY from
             * counter >= interval - 8: presses in the first ~7 ticks
             * of the cadence are DROPPED (engine sampling window). */
            w.counter += WPN_COUNT_STEP;
            if (w.counter >= (int)w.interval - WPN_QUEUE_WINDOW &&
                (in->pressed & EM_PAD_CIRCLE))
                w.pending = 1;
            if (w.counter >= (int)w.interval) {
                w.counter = 0;
                if (w.mag == 0) {
                    /* dry-mag auto reload at the EXPIRY (mode 1,
                     * unconditional) — not on the next press */
                    if (weapon_reload(1) == 0) weapon_enter_reload();
                    else w.fire_sub = WPN_SUB_WAIT;
                } else if (w.pending) {
                    /* queued: +0x2F2 = 1 for the expiry tick (the
                     * engine .L00170E4C blink), shot next tick */
                    w.laser_vis = 1;
                    w.pending   = 0;
                    w.fire_next = 1;    /* 0xB -> 0xA: fires next tick */
                } else {
                    /* plain expiry leaves +0x2F2 alone — the WAIT head
                     * re-sets it NEXT tick (engine .L00170E44) */
                    w.fire_sub = WPN_SUB_WAIT;
                }
            }
            break;

        case WPN_SUB_BURST:
            /* Engine 0x16: cadence; +0x28 counts the burst rounds. */
            w.counter += WPN_COUNT_STEP;
            if (w.counter >= (int)w.interval) {
                w.counter   = 0;
                w.laser_vis = 1;    /* +0x2F2 = 1 at the expiry head
                                     * (engine .L00170F50)             */
                if (w.mag == 0) {
                    if (weapon_reload(1) == 0) {
                        weapon_enter_reload();
                    } else {
                        em_sfx_play(EM_SFX_WPN_DRY);    /* 0x169 */
                        w.burst       = 0;
                        w.fire_sub    = WPN_SUB_GAP;
                        w.burst_pause = WPN_BURST_PAUSE;
                    }
                } else if (w.burst < WPN_BURST_LEN) {
                    w.burst++;
                    w.fire_next = 1;
                } else {
                    w.burst       = 0;
                    w.fire_sub    = WPN_SUB_GAP;
                    w.burst_pause = WPN_BURST_PAUSE;    /* engine 0x17 */
                }
            }
            break;

        case WPN_SUB_GAP:
            /* Engine 0x17: the 8-tick inter-burst pause; L3 manual
             * reload is honored here (the second engine L3 site). */
            if (in->pressed & EM_PAD_L3) {
                if (weapon_reload(2) == 0) {
                    weapon_enter_reload();
                    break;
                }
            }
            if (w.burst_pause > 0 && --w.burst_pause == 0)
                w.fire_sub = WPN_SUB_WAIT;
            break;

        case WPN_SUB_AUTO:
            /* Engine 0x1F: cadence; a still-held trigger refires via
             * the same step-back (0x1F -> 0x1E, next tick). */
            w.counter += WPN_COUNT_STEP;
            if (w.counter >= (int)w.interval) {
                w.counter   = 0;
                w.laser_vis = 1;    /* +0x2F2 = 1 at the expiry head
                                     * (engine .L00171158)             */
                if (w.mag == 0) {
                    if (weapon_reload(1) == 0) {
                        weapon_enter_reload();
                    } else {
                        em_sfx_play(EM_SFX_WPN_DRY);    /* 0x169 */
                        w.fire_sub = WPN_SUB_WAIT;
                    }
                } else if (in->held & EM_PAD_CIRCLE) {
                    w.fire_next = 1;
                } else {
                    w.fire_sub = WPN_SUB_WAIT;
                }
            }
            break;

        default:
            w.fire_sub = WPN_SUB_WAIT;
            break;
    }
}

/* --- KNIFE / MELEE (engine modes 0x21/0x22 — em_weapon.h header) ------- */

/* Per-attack parameters: anim id, fallback length, impact gate T,
 * damage, sound. attack = 1..3 light combo hits, 0 = the heavy stab. */
static void melee_attack_params(int attack, unsigned *anim, int *fallback,
                                float *gate, int *dmg, unsigned *snd)
{
    switch (attack) {
        case 1:  *anim = MELEE_ANIM_L1;    *fallback = MELEE_L1_FRAMES;
                 *gate = MELEE_GATE_L1;    *dmg = MELEE_DMG_L1;
                 *snd  = EM_SFX_MELEE_HIT1; break;
        case 2:  *anim = MELEE_ANIM_L2;    *fallback = MELEE_L2_FRAMES;
                 *gate = MELEE_GATE_L2;    *dmg = MELEE_DMG_L2;
                 *snd  = EM_SFX_MELEE_HIT2; break;
        case 3:  *anim = MELEE_ANIM_L3;    *fallback = MELEE_L3_FRAMES;
                 *gate = MELEE_GATE_L3;    *dmg = MELEE_DMG_L3;
                 *snd  = EM_SFX_MELEE_HIT3; break;
        default: *anim = MELEE_ANIM_HEAVY; *fallback = MELEE_HEAVY_FRAMES;
                 *gate = MELEE_GATE_HEAVY; *dmg = MELEE_DMG_HEAVY;
                 *snd  = EM_SFX_MELEE_HIT3; break; /* heavy shares 0x17F */
    }
}

/* Start one attack (the engine's mode-0x21 state-N / mode-0x22 entry:
 * anim through the clip arbiter at the property rate 1.0, +0x2E combo
 * buffer cleared, the per-attack window timers armed). attack = 1..3
 * light, 0 = heavy. */
static void melee_start_attack(int attack)
{
    unsigned anim, snd;
    int      fallback, dmg;
    float    gate;
    melee_attack_params(attack, &anim, &fallback, &gate, &dmg, &snd);

    em_game_anim_request(anim, 1.0f);   /* rate 1.0 (D_00248C90 rows)  */
    m.state    = EM_MELEE_SWING;
    m.heavy    = attack == 0;
    m.combo    = attack;
    m.tick     = 0;
    m.len      = anim_ticks(anim, 1.0f, fallback);
    m.impact   = (int)((float)m.len - gate);
    if (m.impact < 3) m.impact = 3;     /* mailbox latency + blend-in  */
    /* Chain window (light hits 1/2 only): opens at len - 19
     * (D_002486D0/D4); hit 3 and the heavy end the string. */
    m.chain_at = (attack == 1 || attack == 2)
                     ? m.len - (int)MELEE_CHAIN_TAIL : 0;
    if (m.chain_at && m.chain_at <= m.impact) m.chain_at = m.impact + 1;
    m.buffered = 0;
    m.confirm  = 0;
    m.swings++;
}

/* The impact gate: swing sound + damage-mailbox write (engine: both
 * fire together, unconditionally — the sound is NOT hit-gated; the
 * mailbox lands on the melee-target link and the TARGET-side polls do
 * the range work). The port resolves the victim here with the reach
 * stand-in (header note) and reads the hit back immediately — the
 * native +0x0A confirm, consumed next tick exactly like the engine. */
static void melee_impact(const float pos[3], float yaw)
{
    unsigned anim, snd;
    int      fallback, dmg;
    float    gate, aim[3];
    melee_attack_params(m.heavy ? 0 : m.combo, &anim, &fallback, &gate,
                        &dmg, &snd);
    (void)anim; (void)fallback; (void)gate;  /* window params (start) */

    em_sfx_play(snd);                   /* 0x17D/0x17E/0x17F, vol 300  */
    int victim = em_enemy_acquire(pos, yaw, EM_MELEE_REACH, MELEE_CONE,
                                  aim);
    if (victim >= 0) {
        em_enemy_damage(victim, (int16_t)dmg);  /* victim +0x36        */
        m.confirm = 1;                  /* target +0x0A read-back      */
        m.hits++;
    }
}

/* The melee per-frame machine — func_001735C0 (light combo) +
 * func_00173E60 (heavy) collapsed to the port shape; engine states in
 * the comments. Runs only while the rifle is HOLSTERED (the engine
 * dispatches modes 0x21/0x22 from the unarmed action codes 0..7). */
static void melee_update(const float pos[3], float yaw,
                         const EmFrameInput *in)
{
    switch (m.state) {
        case EM_MELEE_IDLE:
            if (w.state != EM_WPN_HOLSTERED) return;
            /* Engine dispatch order (func_001607D0 action 0..7): the
             * draw masks first, then FIRE -> mode 0x21, SQUARE ->
             * mode 0x22. The R1 draw is handled by the rifle machine
             * before this runs (em_weapon_update orders it so). */
            if (in->pressed & EM_PAD_CIRCLE)
                melee_start_attack(1);          /* mode 0x21, code 0x36 */
            else if (in->pressed & EM_PAD_SQUARE)
                melee_start_attack(0);          /* mode 0x22, code 0x37 */
            break;

        case EM_MELEE_SWING:
            m.tick++;
            /* +0x2E combo buffer: a FIRE press during the swing (the
             * engine samples it from the damage phase on; the phases
             * open within a tick or two of the start). */
            if (!m.heavy && (in->pressed & EM_PAD_CIRCLE))
                m.buffered = 1;
            if (m.tick == m.impact) {
                melee_impact(pos, yaw);         /* sound + mailbox      */
                break;
            }
            if (m.tick > m.impact && m.confirm) {
                /* Hit confirmed (the engine reads the target's +0x0A
                 * the tick after the write): EARLY EXIT to the recover
                 * states — a landed hit SKIPS the combo chain. */
                m.state       = EM_MELEE_RECOVER;
                m.recov_pause = MELEE_RECOV_PAUSE;  /* state 0x50/0x51 */
                break;
            }
            if (m.chain_at && m.tick >= m.chain_at && m.buffered) {
                /* WHIFF chain: the buffered next hit starts at the
                 * chain window (blend 1.0 through the arbiter). */
                melee_start_attack(m.combo + 1);
                break;
            }
            if (m.tick >= m.len) {
                /* Clip end on a whiff: the 0x63/0x64 exit ramp — NO
                 * recover anim; locomotion resumes by itself. */
                m.state = EM_MELEE_IDLE;
                m.heavy = 0;
                m.combo = 0;
            }
            break;

        case EM_MELEE_RECOVER:
            /* Engine 0x50/0x51: 4-tick pause, then the recover anim
             * 0x10F (idx-0 arm; blend 4.0 — the port plays it at the
             * property rate), then 0x52 waits the clip out -> exit. */
            if (m.recov_pause > 0) {
                if (--m.recov_pause == 0) {
                    em_game_anim_request(MELEE_ANIM_RECOV,
                                         MELEE_RECOV_RATE);
                    m.tick = 0;
                    m.len  = anim_ticks(MELEE_ANIM_RECOV, MELEE_RECOV_RATE,
                                        MELEE_RECOV_FRAMES);
                }
                break;
            }
            if (++m.tick >= m.len) {
                m.state = EM_MELEE_IDLE;
                m.heavy = 0;
                m.combo = 0;
            }
            break;

        default:
            m.state = EM_MELEE_IDLE;
            break;
    }
}

void em_weapon_update(const EmCollision *coll, const float player_pos[3],
                      float player_yaw, const EmFrameInput *in)
{
    if (!in || !player_pos) return;

    /* Scheduled fire-chain tail (ticked BEFORE the fire-event resolve,
     * so an impact armed by this frame's resolve waits a full tick and
     * 0x189 lands exactly fire +2 frames — the s29 chain): */
    if (w.impact_sfx > 0 && --w.impact_sfx == 0)
        em_sfx_play_at(EM_SFX_WPN_IMPACT, w.impact_pos,
                       300.0f);             /* 0x189 AT THE WALL HIT  */
    for (int k = 0; k < WPN_CASING_SLOTS; k++) {
        if (w.casing[k] > 0 && --w.casing[k] == 0)
            em_sfx_play_at(EM_SFX_WPN_CASING, player_pos,
                           300.0f);         /* 0x16A casing at the
                                             * player, shot+42        */
    }

    /* SHOULDER-LIGHT auto-off burst (the SEPARATE s28b L3 stealth-light
     * system — em_weapon.c "SHOULDER-LIGHT BURST" constants block; NOT
     * the gun-light preference, which has no timer): the 300-frame
     * timer down-counts EVERY frame regardless of stance (engine: the
     * player spine, 0x00161138); at 0 the light turns itself off —
     * switch sound 0x15D, and the 0x15D turn-off gesture commits only
     * in the unarmed idle (the armed tops re-select their pose every
     * frame in the engine; the port's hold would otherwise be
     * clobbered — documented simplification). UNHOOKED: nothing arms
     * the timer until the L3 input path is decoded, so this block is
     * faithfully dormant. */
    if (w.shoulder_timer > 0 && --w.shoulder_timer == 0) {
        em_sfx_play(WPN_SFX_LIGHT_OFF);             /* 0x15D, 920 ms  */
        if (w.state == EM_WPN_HOLSTERED && m.state == EM_MELEE_IDLE)
            em_game_anim_request(WPN_ANIM_LIGHT_OFF, 1.0f);
    }

    /* Gun-side tick FIRST: an event posted last frame resolves now —
     * the contract's one-frame fire-event latency, kept exactly. */
    if (w.fire_event) {
        w.fire_event = 0;
        weapon_resolve_fire(coll, player_pos, player_yaw);
    }
    /* Muzzle-flash FX tick (the engine FX actor's run state: scale +=
     * vel, vel *= 0.8, free at tick 15). Ticks like the casings —
     * independent of the weapon state; the FX actor outlives a stance
     * drop. */
    if (w.flash > 0) {
        w.flash--;
        w.flash_scale += w.flash_vel;
        w.flash_vel   *= WPN_FLASH_DECAY;
        /* ROTATION LERP (func_001F5040 .L001F53A8, translated 2026-06-11
         * — retires the 0.8^t intensity stand-in): from engine tick 4
         * (age 6) the FX rotation triple chases -128 DEGREES at 0.35 of
         * the gap per tick (one value written to all three components;
         * variant 0 starts at 0) — the dying star rolls ~45 deg in its
         * first lerp tick and settles toward -128. The port applies it
         * as a roll around the gun axis (the star model's +X), the
         * dominant visible component of the uniform Euler triple. */
        int eng_t = (WPN_FLASH_TICKS - w.flash) - 2;
        if (eng_t >= 4)
            w.flash_rot += (-128.0f - w.flash_rot) * 0.35f;
    }

    const int draw_held = (in->held & EM_PAD_R1) != 0;

    switch (w.state) {
        case EM_WPN_HOLSTERED:
            if (draw_held && m.state == EM_MELEE_IDLE) {
                /* major 0 ENTER: reload-if-empty, anim 0x110 at the
                 * property-table rate 1.4, sound 0x162 @vol150
                 * (func_0016F530). The ENTER reload is covered by the
                 * draw anim — no RELOAD state, but it counts as a
                 * reload. */
                if (weapon_reload(0) == 0) w.reloads++;
                em_sfx_play(EM_SFX_WPN_DRAW);
                /* HOLD-type for the same reason as the reload (the
                 * draw's tail otherwise pops through idle for a frame
                 * before the aim hold commits). */
                em_game_anim_hold(WPN_ANIM_DRAW, WPN_DRAW_RATE);
                w.state = EM_WPN_DRAW;
                w.timer = em_weapon_draw_ticks();
            }
            break;
        case EM_WPN_DRAW:
            if (!draw_held) {
                weapon_enter_holster();     /* 0x65: anim 0x111, 0x163 */
            } else if (--w.timer <= 0) {
                weapon_enter_aim();         /* major 2: hold pose 0x112 */
                w.pending = 0;
                w.burst   = 0;
            }
            break;
        case EM_WPN_AIM:
            if (!draw_held) {
                weapon_enter_holster();
                w.pending = 0;
                w.burst   = 0;
            } else {
                weapon_fire_logic(in);
                /* SQUARE while armed = the SUB-WEAPON action
                 * (func_0017A970), attachment 0 = the FLASHLIGHT
                 * toggle (em_weapon.h "FLASHLIGHT" — the engine's
                 * D_00810D3C flag, user-attested identity). The flag
                 * is a PERSISTENT PREFERENCE: it survives holsters
                 * and re-draws until toggled again, with NO timer
                 * (the 300-frame burst belongs to the separate
                 * shoulder-light system — the dormant block above).
                 * ON: sound 0x179 (the s29 live capture); OFF: silent
                 * (engine 1->0 plays nothing). No ammo use, no
                 * battery drain, no state change. */
                int square = (in->pressed & EM_PAD_SQUARE) != 0;
                if (w.light_cap) {          /* EM_CAPTURE_LIGHT one-shot
                                             * synthetic Square (debug
                                             * instrumentation only)    */
                    square      = 1;
                    w.light_cap = 0;
                }
                if (square) {
                    if (!w.light_on) {
                        w.light_on = 1;
                        em_sfx_play(EM_SFX_SUB_TOGGLE);     /* 0x179 */
                    } else {
                        w.light_on = 0;
                    }
                }
            }
            break;
        case EM_WPN_RELOAD:
            /* anim 0x11B wait (major 3); the ammo move already happened.
             * MAG ACTION (s29): 0x168 fires ~0.5 s into the window. */
            if (getenv("EM_WEAPON_TRACE"))    /* runtime diagnosis aid:
                 * prints the clip the anim system actually plays each
                 * reload tick (the 2026-06-11 stagger hunt's tool) */
                fprintf(stderr,
                        "wpn trace: RELOAD tick %d committed clip 0x%X "
                        "frame %d\n", w.timer,
                        em_game_anim_active(), em_game_anim_frame());
            if (w.mag_sfx > 0 && --w.mag_sfx == 0)
                em_sfx_play(EM_SFX_WPN_MAG);
            if (--w.timer <= 0)
                weapon_enter_aim();         /* re-hold the aim pose */
            if (!draw_held)                 /* stance drop mid-reload */
                weapon_enter_holster();
            break;
        case EM_WPN_HOLSTER:
            if (--w.timer <= 0)
                w.state = EM_WPN_HOLSTERED;
            break;
        default:
            w.state = EM_WPN_HOLSTERED;
            break;
    }

    /* KNIFE / MELEE — the unarmed attack machine (engine modes
     * 0x21/0x22, dispatched from the unarmed action codes AFTER the
     * draw masks — the rifle switch above ran first, so a same-frame
     * R1 draw correctly wins over a melee press). */
    melee_update(player_pos, player_yaw, in);

    /* LASER SIGHT refresh — the engine's drawers gate on the armed-
     * stance CODE (player +0x1F0 in {0x31, 0x34} with phase +0x1F1 ==
     * 1), which is held for the ENTIRE aim including the fire/recoil
     * ticks (FINDINGS "LASER SIGHT DECODED": the laser runs the whole
     * time the player aims) — i.e. the AIM/FIRE loop, but not the
     * draw, reload (code 0x33) or holster clips. */
    /* ... AND on the fire SM's +0x2F2 visible flag (the decoded LASER
     * HIDE WINDOW): the laser vanishes from every shot tick until that
     * shot's cadence expiry — during sustained fire it only blinks for
     * the single expiry tick of each chained round, exactly the
     * engine's gun-tick gate (func_00188630 reads D_008105A2). */
    w.laser_on = (w.state == EM_WPN_AIM) && w.laser_vis;
    if (w.laser_on)
        laser_update(coll, player_pos, player_yaw);

    /* FLASHLIGHT SPOT pose + set (the PORT's documented deviation —
     * em_weapon.h "RENDERING": the engine draws nothing for the
     * toggle). Gated EXACTLY like the laser: the light is visible ONLY
     * in the AIM phase — not during the draw, reload or holster clips
     * and not holstered (the real game: light + laser appear only
     * while aiming, vanish together during a reload; the PREFERENCE
     * flag itself persists). Pose = the hand-frame muzzle ray. The
     * spot is handed to the gfx layer HERE, during the update stage:
     * em_gfx_begin_frame already ran (the frame loop's stage B) and
     * the close-out's skinned draws haven't — so the term lights THIS
     * frame's draws. w.gfx is the device cached by em_weapon_render
     * (NULL only before the first rendered frame — one unlit frame,
     * same staleness class as the bone publish). */
    w.light_live = (w.light_on && w.laser_on);
    if (w.light_live) {
        float lmuz[3], tip[3];
        weapon_muzzle_ray(player_pos, player_yaw, lmuz, w.light_dir, tip);
        /* the light leaves the muzzle FRONT (the barrel tip, gun+0xB0
         * — the muzzle-flash anchor), not the in-receiver ray origin:
         * anchoring at the origin put the cone apex inside the gun. */
        memcpy(w.light_pos, tip, sizeof w.light_pos);
        if (w.gfx)
            em_gfx_spot_light(w.gfx, w.light_pos, w.light_dir,
                              kLightColor, WPN_LIGHT_RANGE,
                              WPN_LIGHT_COS_IN, WPN_LIGHT_COS_OUT);
    }
}

/* --- World-space weapon visuals (laser sight + muzzle flash) ----------- */

/* Muzzle-flash FALLBACK palette (used only when the .emtx sheets are
 * absent: a hot near-white core falling to a warm orange streak — the
 * old additive stand-in reading of the flash sheets). */
static const float kFlashCore[3]  = { 1.0f, 0.93f, 0.70f };
static const float kFlashStar[3]  = { 1.0f, 0.62f, 0.22f };

/* FX sprite-texture loader (the FX SPRITE TEXTURES block at the top):
 * reads the assets/fx .emtx files once and registers the em_gfx
 * beam-texture slots. fx.ok[slot] = 1 only when the slot registered —
 * the drawers check it per primitive and otherwise keep the flat-color
 * fallback, so a missing asset never regresses the frame. */
static struct {
    int     tried;
    int     ok[5];          /* slots 0..3 = .emtx sheets, 4 = cone glow */
    EmModel cone;           /* light_cone.emdl (chunk27 0x10) geometry  */
    int     cone_ok;        /* model loaded AND its sheet registered    */
} fx;

static uint32_t fx_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void fx_load(EmGfx *gfx)
{
    if (fx.tried) return;
    fx.tried = 1;
    /* FLASHLIGHT CONE (the chunk27 light-cone mesh 0x10, export_props
     * --cone): a static 1-node EMDL whose embedded texture is the
     * additive glow sheet — registered as beam slot FX_TEX_CONE; the
     * geometry triangles are queued per aim frame (cone_render). The
     * cone mesh was FOUND in the library (entries 0x10/0x11/0x16 —
     * FINDINGS "LIGHT-CONE MESH FAMILY"), so there is no procedural
     * fallback: an absent asset just draws no cone (logged), exactly
     * the missing-sheet policy of the sprites below. */
    if (em_model_load(&fx.cone, WPN_CONE_FILE) == 0) {
        if (fx.cone.tex_count >= 1 && fx.cone.texels &&
            em_gfx_beam_texture_set(gfx, FX_TEX_CONE,
                                    fx.cone.texels +
                                        fx.cone.texs[0].offset,
                                    fx.cone.texs[0].width,
                                    fx.cone.texs[0].height)) {
            fx.ok[FX_TEX_CONE] = 1;
            fx.cone_ok         = 1;
            fprintf(stderr, "weapon: flashlight cone %s (%u verts, "
                    "%u tris)\n", WPN_CONE_FILE, fx.cone.vert_count,
                    fx.cone.index_count / 3);
        } else {
            fprintf(stderr, "weapon: %s: no usable glow sheet — cone "
                    "disabled\n", WPN_CONE_FILE);
            em_model_free(&fx.cone);
        }
    }
    for (int i = 0; i < 4; i++) {
        FILE *f = fopen(kFxFiles[i], "rb");
        if (!f) continue;                   /* absent: silent fallback */
        uint8_t hdr[16];
        if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr ||
            memcmp(hdr, "EMTX", 4) != 0 || fx_u32(hdr + 4) != 1) {
            fprintf(stderr, "weapon: %s: bad .emtx header\n", kFxFiles[i]);
            fclose(f);
            continue;
        }
        uint32_t tw = fx_u32(hdr + 8), th = fx_u32(hdr + 12);
        if (!tw || !th || tw > 1024 || th > 1024) {
            fprintf(stderr, "weapon: %s: implausible size %ux%u\n",
                    kFxFiles[i], tw, th);
            fclose(f);
            continue;
        }
        size_t   bytes = (size_t)tw * th * 4;
        uint8_t *rgba  = (uint8_t *)malloc(bytes);
        if (rgba && fread(rgba, 1, bytes, f) == bytes &&
            em_gfx_beam_texture_set(gfx, i, rgba, tw, th)) {
            fx.ok[i] = 1;
            fprintf(stderr, "weapon: fx sprite %s (%ux%u)\n",
                    kFxFiles[i], tw, th);
        } else {
            fprintf(stderr, "weapon: %s: truncated/failed\n", kFxFiles[i]);
        }
        free(rgba);
        fclose(f);
    }
}

/* Soft additive glow: three concentric camera-facing squares with the
 * intensity split across them (0.55/0.30/0.15 at 1/3, 2/3 and full
 * size) — the untextured approximation of the engine's radial-falloff
 * glow sprites (the laser dot's 0x20045BA5 sprite, the flash sheet).
 * A single hard square over-reads as a solid block; the layered split
 * keeps the same total energy at the center and fades to the rim. */
static void glow_dot(EmGfx *gfx, const float p[3], float size,
                     const float rgb[3])
{
    static const float layer[3][2] = {
        { 1.0f / 3.0f, 0.55f }, { 2.0f / 3.0f, 0.30f }, { 1.0f, 0.15f }
    };
    for (int i = 0; i < 3; i++) {
        float c[4] = { rgb[0] * layer[i][1], rgb[1] * layer[i][1],
                       rgb[2] * layer[i][1], 1.0f };
        em_gfx_beam_dot(gfx, p, size * layer[i][0], c);
    }
}

/* FLASHLIGHT CONE renderer — the chunk27 light-cone shell (entry 0x10)
 * drawn from the muzzle tip along the aim ray. The model is authored
 * apex-at-origin opening along +Z (radius 25 at z = 200, half-angle
 * 7.13 deg — the same angle the spot term projects); the basis maps
 * model +Z onto the light direction with an arbitrary stable roll (the
 * cone is rotationally symmetric). Triangles go through the beam
 * pass's additive textured-triangle queue (depth test on / write off,
 * cull none — the far cone clips into walls correctly). The glow
 * sheet's planar projection fades the shell toward the wide end, so
 * the visible beam is brightest at the gun. */
static void cone_render(EmGfx *gfx)
{
    const EmModel *m = &fx.cone;
    /* basis: z = light dir; x/y any orthonormal pair */
    float z[3] = { w.light_dir[0], w.light_dir[1], w.light_dir[2] };
    float ref[3] = { 0.0f, 1.0f, 0.0f };
    if (fabsf(z[1]) > 0.99f) { ref[0] = 1.0f; ref[1] = 0.0f; }
    float x[3] = { ref[1] * z[2] - ref[2] * z[1],
                   ref[2] * z[0] - ref[0] * z[2],
                   ref[0] * z[1] - ref[1] * z[0] };
    float xl = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    if (xl < 1e-5f) return;
    for (int k = 0; k < 3; k++) x[k] /= xl;
    float y[3] = { z[1] * x[2] - z[2] * x[1],
                   z[2] * x[0] - z[0] * x[2],
                   z[0] * x[1] - z[1] * x[0] };

    const float c[4] = { WPN_CONE_GAIN, WPN_CONE_GAIN, WPN_CONE_GAIN,
                         1.0f };
    for (uint32_t i = 0; i + 2 < m->index_count; i += 3) {
        float p[9], uv[6];
        for (int v = 0; v < 3; v++) {
            const float *vr = m->verts +
                (size_t)m->indices[i + v] * EM_MODEL_VERT_WORDS;
            for (int k = 0; k < 3; k++)
                p[v * 3 + k] = w.light_pos[k] + x[k] * vr[0] +
                               y[k] * vr[1] + z[k] * vr[2];
            uv[v * 2 + 0] = vr[6];
            uv[v * 2 + 1] = vr[7];
        }
        em_gfx_beam_tri_tex(gfx, FX_TEX_CONE, p, uv, c);
    }
}

/* The muzzle flash through the beam/dot pass — the engine FX actor
 * func_001F5040 variant 0's own model-per-tick schedule (the MUZZLE
 * FLASH block at the top), drawn as TEXTURED additive billboards with
 * the real effect sheets: spawn tick = the model-0xD radial puff;
 * ticks 0..2 = the model-8 forward star (its own sheet) + muzzle
 * ball; tick 3+ = the model-7 star (the puff sheet). Scaled by the
 * live FX scale; intensity decays with the engine's own 0.8^t
 * velocity constant (the flagged stand-in for the untranslated
 * rotation lerp). Sheets absent -> the old flat-color core + streak. */
static void flash_render(EmGfx *gfx)
{
    float in  = w.flash_vel / WPN_FLASH_VEL;       /* 0.8^t (FALLBACK) */
    float s   = w.flash_scale;
    int   age = WPN_FLASH_TICKS - w.flash;         /* 1 = spawn frame  */
    float end[3] = { w.flash_pos[0] + w.flash_dir[0] * WPN_FLASH_STAR_LEN * s,
                     w.flash_pos[1] + w.flash_dir[1] * WPN_FLASH_STAR_LEN * s,
                     w.flash_pos[2] + w.flash_dir[2] * WPN_FLASH_STAR_LEN * s };

    if (fx.ok[FX_TEX_PUFF] && fx.ok[FX_TEX_STAR] && fx.ok[FX_TEX_BALL]) {
        /* CONSTANT intensity (2026-06-11 fidelity pass): the engine FX
         * writes NO color fade — the 0.8 decay belongs to the SCALE
         * VELOCITY alone; the flash dies by the model swap, the scale
         * spread and the rotation lerp, then vanishes at tick 15 (the
         * old 0.8^t color decay was a flagged stand-in for the then-
         * untranslated rotation, now retired with it). */
        float c[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        /* the star quads ROLL around the gun axis by the FX rotation
         * (degrees -> radians; 0 until engine tick 4) */
        float roll = w.flash_rot * 0.017453293f;
        if (age <= 1) {
            /* INIT binding: model 0xD — the radial puff (camera-facing;
             * the engine model is a small rosette of quads) */
            em_gfx_beam_dot_tex(gfx, FX_TEX_PUFF, w.flash_pos,
                                WPN_FLASH_CORE * s, c);
        } else if (age <= 4) {
            /* ticks 0..2: model 8 — forward +X star (own sheet) +
             * the muzzle-base radial cross (ball sheet); the star is
             * AXIAL (a model aligned to the barrel, not a screen
             * sprite — the axial-billboard quad stands in for its
             * radial fins) */
            em_gfx_beam_tex_roll(gfx, FX_TEX_STAR, w.flash_pos, end,
                                 WPN_FLASH_STAR_W * s, roll, c);
            em_gfx_beam_dot_tex(gfx, FX_TEX_BALL, w.flash_pos,
                                WPN_FLASH_BALL * s, c);
        } else {
            /* tick 3+: model 7 — the same star shape on the puff
             * sheet, tumbling with the translated rotation lerp */
            em_gfx_beam_tex_roll(gfx, FX_TEX_PUFF, w.flash_pos, end,
                                 WPN_FLASH_STAR_W * s, roll, c);
        }
        return;
    }

    /* FALLBACK (sheets absent): flat-color core glow + tapered streak. */
    float core[3] = { kFlashCore[0] * in, kFlashCore[1] * in,
                      kFlashCore[2] * in };
    glow_dot(gfx, w.flash_pos, WPN_FLASH_CORE * s, core);
    float cb[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    static const float wlayer[2][2] = { { 0.45f, 0.65f }, { 1.0f, 0.35f } };
    for (int i = 0; i < 2; i++) {
        float ca[4] = { kFlashStar[0] * in * wlayer[i][1],
                        kFlashStar[1] * in * wlayer[i][1],
                        kFlashStar[2] * in * wlayer[i][1], 1.0f };
        em_gfx_beam(gfx, w.flash_pos, end, WPN_FLASH_STAR_W * s * wlayer[i][0],
                    ca, cb);
    }
}

void em_weapon_render(EmGfx *gfx)
{
    if (!gfx) return;
    /* Cache the device for the update stage's hand-bone reads
     * (em_gfx_last_skinned_bone — see weapon_hand_matrix), and load
     * the FX sprite sheets once (no draw — the registration alone
     * never touches the frame). */
    w.gfx = gfx;
    fx_load(gfx);

    /* The flash FX actor outlives a stance drop (engine: a pool actor,
     * not gun state) — draw it whenever it is alive. */
    if (w.flash > 0) flash_render(gfx);

    if (w.state == EM_WPN_HOLSTERED) return;

    /* FLASHLIGHT CONE — the visible beam mesh, gated exactly like the
     * spot term (the light pose set by this frame's update). */
    if (w.light_live && fx.cone_ok)
        cone_render(gfx);

    /* LASER SIGHT — the translated func_00185760 pass (em_weapon.h).
     * Beam: 32 segments muzzle -> clipped endpoint, per-vertex color =
     * base * max(sin(phase), 0) with a random phase start each frame
     * and a 0.025*len step per segment (func_001E2BA0: phase0 =
     * rand/2^31 * 2pi, f21 = 0.1*len/4; negative sine clamps to 0 in
     * the engine's float->color conversion — the dashed shimmer).
     * func_001E2BA0 zero-initializes the previous-vertex color, so the
     * first segment fades up from black. Dot: 3.0-unit additive glow
     * at the endpoint, R = (0x50 + rand5)/0x80 of the GS 0x80 = 1.0
     * scale, G = B = 0 (func_00185760's unlocked arm). */
    if (w.laser_on) {
        float d[3] = { w.laser_b[0] - w.laser_a[0],
                       w.laser_b[1] - w.laser_a[1],
                       w.laser_b[2] - w.laser_a[2] };
        float len   = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        float phase = (float)wpn_rand() / 32768.0f * 6.2831853f;
        float dph   = WPN_LASER_PHASE * len;
        float pa[3] = { w.laser_a[0], w.laser_a[1], w.laser_a[2] };
        float ca[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        for (int i = 1; i <= WPN_LASER_SEGS; i++) {
            float t     = (float)i / (float)WPN_LASER_SEGS;
            float pb[3] = { w.laser_a[0] + d[0] * t,
                            w.laser_a[1] + d[1] * t,
                            w.laser_a[2] + d[2] * t };
            float s  = sinf(phase + dph * (float)i);
            float in = s > 0.0f ? s : 0.0f;
            float cb[4] = { kLaserColor[0] * in, kLaserColor[1] * in,
                            kLaserColor[2] * in, kLaserColor[3] };
            em_gfx_beam(gfx, pa, pb, WPN_LASER_WIDTH, ca, cb);
            memcpy(pa, pb, sizeof pa);
            memcpy(ca, cb, sizeof ca);
        }
        /* The DOT — the real func_001CD520 sprite when the exported
         * texture is present: ONE textured additive billboard at the
         * endpoint, the 32x16 glow image squeezed onto the engine's
         * square 3x3 quad (= a soft round dot), modulated by the
         * flickering red exactly like the GS TFX-modulate draw.
         * Fallback: the old 3-layer concentric flat-color glow. */
        float dr = (float)(0x50 + (wpn_rand() & 0x1F)) / 128.0f;
        /* DOT placement: offset the billboard HALF ITS SIZE off the
         * surface along the hit normal — a sprite centered exactly on
         * the wall plane half-clips behind it under the depth test
         * (user-reported). Engine sprites sit on the surface plane
         * plus an offset; a miss (free 260-unit endpoint) draws at the
         * endpoint unchanged. */
        float dp[3] = { w.laser_b[0], w.laser_b[1], w.laser_b[2] };
        if (w.laser_surf) {
            for (int k = 0; k < 3; k++)
                dp[k] += w.laser_n[k] * (WPN_DOT_SIZE * 0.5f);
        }
        if (fx.ok[FX_TEX_DOT]) {
            float dc[4] = { dr, 0.0f, 0.0f, 1.0f };
            em_gfx_beam_dot_tex(gfx, FX_TEX_DOT, dp, WPN_DOT_SIZE, dc);
        } else {
            float dot[3] = { dr, 0.0f, 0.0f };
            glow_dot(gfx, dp, WPN_DOT_SIZE, dot);
        }
    }

    /* NO screen-space reticle: the real game aims with the laser dot
     * alone (s23 live aim capture — no crosshair overlay exists in the
     * engine's frame). The old placeholder crosshair / hit-pulse /
     * overlay flash rects are gone (2026-06-11 weapon-fidelity pass). */
}

/* --- Accessors ---------------------------------------------------------- */

uint8_t em_weapon_mag(void)      { return w.mag; }
int16_t em_weapon_reserve(void)  { return w.reserve; }
uint8_t em_weapon_fire_mode(void){ return w.fire_mode; }
int     em_weapon_state(void)    { return w.state; }
int     em_weapon_shots(void)    { return w.shots; }
int     em_weapon_reloads(void)  { return w.reloads; }
int     em_weapon_last_hit(void) { return w.last_hit; }

/* Armed-stance query for the camera's aim-state target-height offset
 * (em_weapon.h "AIM CAMERA HOOKUP"): 1 across DRAW/AIM/RELOAD — the
 * engine's weapon modes 0x1D..0x20 — not while holstering out. */
int em_weapon_is_aiming(void)
{
    return w.state == EM_WPN_DRAW || w.state == EM_WPN_AIM ||
           w.state == EM_WPN_RELOAD;
}

void em_weapon_set_fire_mode(uint8_t mode)
{
    if (mode <= EM_WPN_MODE_AUTO) w.fire_mode = mode;
}

/* --- KNIFE / MELEE accessors (em_weapon.h) ------------------------------ */

/* Movement plant for em_game's player_move: the engine's melee modes
 * 0x21/0x22 replace the locomotion modes outright (the heavy yaw steer
 * func_00173DD0 is untranslated — flagged in the header). */
int em_weapon_is_melee(void)      { return m.state != EM_MELEE_IDLE; }

int em_weapon_melee_state(void)   { return m.state; }
int em_weapon_melee_combo(void)   { return m.heavy ? 0 : m.combo; }
int em_weapon_melee_heavy(void)   { return m.heavy; }
int em_weapon_melee_swings(void)  { return m.swings; }
int em_weapon_melee_hits(void)    { return m.hits; }

/* FLASHLIGHT introspection (em_weapon.h): the persistent preference
 * flag (engine D_00810D3C — no timer), and the frames left on the
 * SEPARATE shoulder-light burst (the dormant s28b system; always 0
 * until its L3 input path is decoded and re-hooked). */
int em_weapon_flashlight(void)       { return w.light_on; }
int em_weapon_flashlight_timer(void) { return w.shoulder_timer; }

/* LASER introspection (self-tests): 1 = the laser draws this frame —
 * the AIM state AND the fire SM's +0x2F2 visible flag (the decoded
 * LASER HIDE WINDOW: hidden from each shot tick to its cadence
 * expiry). */
int em_weapon_laser_visible(void)    { return w.laser_on; }
