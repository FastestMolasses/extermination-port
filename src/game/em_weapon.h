/* em_weapon.h — the SPR4 rifle: native weapon state machine + firing loop.
 *
 * Native translation of the engine's weapon system (decomp repo
 * Extermination/docs/FINDINGS.md "WEAPON SYSTEM", 2026-06-10, section 8
 * port contract):
 *
 *   func_0016FCF0..      armed-stance tops (player +0x05 modes 0x1D..0x20,
 *                        major-state byte +0x06)  -> em_weapon_update()
 *   func_00170A60        rifle FIRE SUB-MACHINE (sub-state +0x07, fire-mode
 *                        families 10/20/30)       -> the AIM trigger logic
 *   func_0017B300        RELOAD (matched 100%): mag = min(30, reserve),
 *                        reserve NOT subtracted   -> weapon_reload()
 *   func_001861C0        the BULLET (hitscan: one func_0019A570 segment
 *                        query per shot, range 260) -> the fire-event
 *                        resolution inside em_weapon_update()
 *   gun actor +0x2E      fire-event halfword: the player SM posts 1, the
 *                        gun behavior consumes it NEXT tick -> the port
 *                        keeps this exact one-frame latency (contract:
 *                        required for animation/FX sync).
 *
 * ENEMY HITS (design choice, documented): on the PS2 the segment query
 * itself reports the hit ACTOR (*0x700031D4 in the scratchpad result
 * block) because movable hulls live in collision set 0. The port keeps
 * the em_collision world-geometry API untouched; instead the bullet (a)
 * acquires a target through em_enemy_acquire (distance + facing-cone
 * stand-in for the engine's screen-cone acquisition func_00199220) so
 * the ray aims at the victim's aim point (+5-unit overshoot, the
 * engine's targeted-endpoint rule), and (b) runs em_enemy_ray_test —
 * segment vs every live enemy's hit sphere — BEFORE crediting the world
 * hit; the nearest of enemy-vs-world wins. An enemy hit writes damage
 * code 5 into the victim's +0x36 mailbox (func_001B41F0's contract);
 * the enemy behavior consumes it in its own tick (crawler HP 1 =
 * one-shot kill) and the crosshair pulses the HIT shape.
 *
 * AMMO MODEL (the engine's TOTAL-pool rule, FINDINGS "INVENTORY LOCATED"):
 *   mag      D_00810C62, u8  — rounds in the magazine, max 30
 *   reserve  D_00810CB4, s16 — TOTAL rounds INCLUDING the mag
 * Each shot decrements BOTH; a reload sets mag = min(30, reserve) and
 * leaves the reserve untouched.
 *
 * FIRE MODES (D_00810C61): 0 = semi (one shot per trigger press, engine
 * sub-states 10/11), 1 = 3-round burst (20..23, 8-tick pause between
 * bursts), 2 = full-auto (30..32). Cadence for all: the fire counter
 * (+0x276) gains 2/frame and a shot needs >= 12.0 (+0x2F4) — one shot
 * every 6 frames, 10/s at 60 Hz.
 *
 * FIRE SUB-STATE MACHINE (re-read 2026-06-11 from the func_00170A60
 * .s — the 2026-06-11 weapon-fidelity pass; replaces the old flat
 * "pending" latch): the engine's per-sub-weapon fire machine holds a
 * SUB-STATE byte (+0x07) through the cadence, and the SEMI family is
 * rate-gated exactly like auto — one press can never beat the 6-frame
 * interval:
 *
 *   0    TRIGGER WAIT: a press with ammo fires IMMEDIATELY (no counter
 *        test — the state is only reachable >= one full cadence after
 *        the last shot) and enters the family's cadence state; a press
 *        on an empty mag plays the dry click 0x169 ONLY (an empty mag
 *        with a live reserve never survives to this state — the
 *        cadence expiry below already reloaded). L3 manual reload is
 *        checked HERE (and in the burst gap 0x17) — never mid-cadence.
 *   0xA/0xB  SEMI shot + cadence: +0x276 += 2/tick; a NEW press during
 *        the cadence latches the queued-shot flag +0x2A (sampled from
 *        counter >= interval-8 — every tick after the shot tick); at
 *        counter >= interval(12): counter = 0, then EITHER mag empty ->
 *        UNCONDITIONAL reload func_0017B300(.,1) (the dry-mag auto
 *        reload happens at the expiry, NOT on the next press), OR
 *        queued -> step back to the shot state (fires the NEXT tick =
 *        exact 6-frame spacing), OR back to WAIT.
 *   0x14..0x17  BURST: same cadence; +0x28 counts the rounds, 3 ends
 *        the burst into the 0x17 gap (8 ticks, L3 honored there).
 *   0x1E..0x20  AUTO: same cadence; still-held trigger refires via the
 *        same step-back (every 6 frames).
 *
 * The fire tick itself performs the first cadence increment (the
 * engine's shot states fall through into the cadence head), so the
 * port seeds counter = 2 at every shot.
 *
 * KEY MAPPING (engine-faithful since the s29 live decode of the default
 * config block — em_input.h "ENGINE DEFAULT BUTTON CONFIG" has the full
 * spad 0x70003B70..7E table):
 *   R1 (E) HELD   draw + stay in the armed stance; release = holster
 *                 (config slot 0x3B7C = R1, the weapon-draw hold).
 *   CIRCLE (L)    FIRE — the real default-config trigger (config slot
 *                 0x3B78 = 0x0020 = CIRCLE, live-verified s29). CROSS
 *                 stays USE/confirm (slot 0x3B76 — the door use scan).
 *   L3 (key 2)    manual reload (top-up) — the engine's raw L3 pad bit
 *                 (NOT config-mapped), func_0017B300(.,2): reloads ONLY
 *                 if the mag is short (mag < 30 AND reserve > mag — the
 *                 engine mode-2 gate, matched 100% in the decomp); a
 *                 full mag ignores L3. Checked in the trigger-wait
 *                 sub-states only (engine states 0 / 0x17), never
 *                 mid-cadence.
 *   CIRCLE (L) / SQUARE (J) while HOLSTERED = the KNIFE attacks (light
 *                 combo / heavy stab — the s36 melee decode; the
 *                 "KNIFE / MELEE" block below). SQUARE while AIMING =
 *                 the FLASHLIGHT toggle (the "FLASHLIGHT" block below).
 *
 * RELOAD SOUNDS (live-pinned s29 — replaces the old 0xF002 placeholder
 * alias): reload START plays 0x163 (the shared weapon-handling foley,
 * same id as holster) and the MAG ACTION plays 0x168 ~0.5 s into the
 * 0x33 reload anim (the distinctive reload sound, snd_0351). em_weapon
 * schedules the mag-action tick from the reload entry; dropping the
 * stance mid-reload (holster) cancels the pending mag sound.
 *
 * FIRE-CHAIN TAIL (live-pinned s29, wired with the same scheduling
 * pattern): a shot that ray-hits the WORLD (not an enemy) plays the
 * wall impact/ricochet 0x189 two frames after the fire sound (the
 * fire event's one-frame mailbox latency + one armed tick), and EVERY
 * shot ejects a casing whose floor bounce 0x16A plays 42 ticks
 * (~0.7 s) after the shot — the casing countdowns keep ticking through
 * reloads/holsters (the brass is already in the air). An ENEMY hit
 * plays no impact sound — the victim's flinch/death path owns that
 * audio. Surface-variant impact ids are NOT pinned (see the em_sfx.h
 * flag on the 0x188/0x18A/0x18B family): 0x189 plays for every wall.
 *
 * LASER SIGHT (translated 2026-06-10 s23 — live capture + disasm of the
 * gun actor's per-frame drawers func_001854E0 / func_00185760, selected
 * by player +0x318 while the aim-pose anim is in phase): every aim frame
 * the gun raycasts muzzle -> muzzle + dir*260 with the SAME segment
 * query as the bullet (func_0019A570, mode 7, mask 0x20) and clips the
 * laser at the hit point (no hit: the full 260-unit endpoint — the
 * laser still draws). Render, via em_gfx's world-space beam pass:
 *   BEAM (func_00185760 -> func_001E2BA0): 32 consecutive segments
 *     muzzle -> endpoint with per-vertex color = base * max(sin(ph), 0),
 *     ph starting RANDOM each frame and advancing 0.025*len per segment
 *     (the dashed shimmer in the reference capture). Base color
 *     (0.7, 0, 0, 1); with a LOCKED target (D_008106E0, aim option 1)
 *     the engine switches to (1.0, 0.6, 0.2, 1) — pending lock-on.
 *   DOT (func_001CD520 billboard at the endpoint): 3.0-unit additive
 *     sprite sampling the REAL exported glow texture (assets/fx/
 *     laser_dot.emtx — the disasm's 0x...4222DC key, a 32x16 soft
 *     radial blob squeezed onto the square quad = one smooth round
 *     dot), modulated by color R = (0x50 + rand5)/0x80, G = B = 0
 *     (locked: 5.0-unit, R/G/B = (0x70/0x40/0x20 + rand5)/0x80).
 *     Texture absent: the old 3-layer flat-color glow fallback.
 * Both draws are additive with depth test on / write off, exactly the
 * GS states of the original pass (em_gfx_beam / em_gfx_beam_dot_tex).
 *
 * PLAYER ANIMS (wired 2026-06-10 s24, fire recoil s25 — FINDINGS "ANIM
 * ID MAPPING" + "FIRE ANIM MECHANISM"): the state entries drive the
 * real clips through em_game's scripted-anim mailbox (the +0x1F2
 * request / +0x20C commit path; the anim id is the clip-table id in
 * the re-exported player.emdl):
 *   DRAW    anim 0x110 once at rate 1.4 (D_00248C90 rate_scale)
 *   AIM     anim 0x112 HELD (em_game_anim_hold — the SPR4 sub-0 aim
 *           ladder base, D_00248B70[0][0]; the pitch-step blend +0x278
 *           is not translated yet). FIRE = each shot REWINDS the held
 *           clip to frame 0 at 2 frames/tick (em_game_anim_hold_restart
 *           — the engine's bone_matrix_publish samples the committed
 *           ladder clip at frame = fire counter +0x276, which resets
 *           per shot; the recoil snap is baked into the clip's front
 *           frames). There is NO separate fire clip: 0x31/0x32/0x34/
 *           0x35 are the four armed-stance ACTION CODES at +0x1F0
 *           (stance 0x1D/0x1E/0x1F/0x20 respectively — fire-mode
 *           INDEPENDENT; the fire families only read the code for the
 *           shot sound 0x164 vs 0x165), and library containers
 *           49/50/52/53 are unrelated clips (s23's id=index guess for
 *           them is corrected in FINDINGS).
 *   RELOAD  anim 0x33 once; the state window IS the clip length
 *   HOLSTER anim 0x111 once, then locomotion resumes by itself
 * Every state window gates on the committed clip's honest length
 * (em_game_anim_frames -> ceil(frames / rate) ticks); the old fixed
 * frame counts remain only as flagged fallbacks for a clip-less EMDL.
 * While aiming the player is PLANTED (movement locked to turn-in-place;
 * the decision + engine evidence live in em_game.c player_move).
 *
 * MUZZLE ANCHORING (2026-06-11 — replaces the chest-height stand-in):
 * the muzzle ray is the engine's exact two-point form (func_00188630,
 * offset tables decoded from the local boot ELF): ray origin = hand
 * bone matrix * (-3, 1.088, 0), barrel tip = M * (6, 1.088, 0)
 * (D_0024A220 row 7, the manual-aim remap of sub-weapon 0), fire dir =
 * the hand bone's local +X axis; the laser BEAM draws from M *
 * (3.6, 0.5, 0) (D_0024A2A0[0] = the gun+0x1F0 point). The hand matrix
 * is the player palette's node-4 matrix (the rifle attach node),
 * published by the gfx layer (em_gfx_last_skinned_bone — one frame of
 * latency by construction). Fallback when the loaded player EMDL lacks
 * the weapon clips: the old flagged chest-height/yaw stand-in.
 *
 * MUZZLE FLASH (2026-06-11; TEXTURED 2026-06-11 fidelity pass): the
 * engine's func_00187CC0 spawns a 16-tick FX actor (func_001F5040
 * variant 0) at the barrel tip: chunk27 effect models 0xD (radial
 * puff) / 8 then 7 (forward +X star, 4.9 x 4.6 footprint), scale
 * 0.15 + 0.05*rand growing by a 0.8-decay velocity, additive. The
 * port draws the engine's own model-per-tick schedule as TEXTURED
 * additive billboards through the beam pass, sampling the REAL
 * exported effect sheets (assets/fx/flash_puff/_star/_ball.emtx —
 * export_props --fx; the models sample them full-frame): spawn tick
 * = the 0xD puff, ticks 0..2 = the model-8 star streak + muzzle
 * ball, tick 3+ = the model-7 star on the puff sheet; intensity
 * decays with the engine's own 0.8^t constant (stand-in for the
 * untranslated rotation lerp, flagged; sheets absent = flat-color
 * fallback; tracer func_001860A0 still untranslated). There is NO
 * crosshair and NO hit-pulse overlay: the real game aims with the
 * laser dot alone (s23 live aim capture).
 *
 * AIM CAMERA HOOKUP (APPLIED 2026-06-10 s24): the engine lowers the
 * camera's follow target while aiming (camera struct +0x8C target-height
 * offset, default 6.0 — FINDINGS "CAMERA SYSTEM"; the live s23 aim
 * capture ran camera mode 1 with +0x8C = 2.0 in AREA02). em_weapon
 * exposes em_weapon_is_aiming(); em_game's camera_mode_dispatch()
 * applies it by replacing its target-height term:
 *
 *   cam->tgt_des[1] = cam_chase_v(cam->tgt_des[1],
 *       g.pos[1] + (em_weapon_is_aiming() ? cam->aim_h : CAM_TGT_HEIGHT),
 *       CAM_TGT_CAP);
 *
 * Holstered (the default) the module queues nothing and touches nothing,
 * so default-run frame output stays byte-identical to pre-weapon builds.
 *
 * ------------------------------------------------------------------------
 * KNIFE / MELEE (decoded 2026-06-10 s36 — decomp FINDINGS "KNIFE/MELEE
 * DECODED"; retires the s29 "SQUARE = unidentified action" open item).
 *
 * Engine architecture: the knife is permanent equipment with TWO attacks
 * on TWO DIFFERENT BUTTONS (NOT tap-vs-hold), dispatched by the action
 * machine func_001607D0 from the unarmed actions (+0x1F0 codes 0..7):
 *
 *   CIRCLE press (config slot 0x3B78 — the FIRE button) while no rifle
 *     is drawn -> player mode 0x21 (action code 0x36) = func_001735C0,
 *     the LIGHT 3-HIT COMBO machine;
 *   SQUARE press (config slot 0x3B74) while no rifle is drawn ->
 *     player mode 0x22 (action code 0x37) = func_00173E60, the HEAVY
 *     single stab.
 *
 * FLASHLIGHT (2026-06-11 weapon-fidelity pass — user-attested identity
 * for the s36 open item "what does D_00810D3C arm?"; MODEL CORRECTED
 * 2026-06-11 against the real game): while the rifle IS drawn (armed
 * stances 0x31/0x32/0x34/0x35), SQUARE routes to the SUB-WEAPON action
 * func_0017A970; with attachment 0 (D_00810CA6 == 0) it TOGGLES the
 * FLASHLIGHT — the real game's gun light (the flag the engine keeps in
 * D_00810D3C, replayed on the next rifle draw; the s29 live capture's
 * "0x179, no ammo use" was the toggle-ON sound). The flag is a
 * PERSISTENT PREFERENCE:
 *   - toggle ON: flag set, sound 0x179 vol 300 (pinned);
 *   - toggle OFF (second press): silent (engine: 1->0 plays nothing);
 *   - NO timer, NO auto-off, ZERO battery drain — the light never
 *     runs out (user-attested vs the original; the s28b 300-frame
 *     burst the port previously hung off this toggle belongs to the
 *     SEPARATE shoulder-light stealth system, see below);
 *   - the preference persists across holsters/re-draws until toggled.
 * SHOULDER-LIGHT BURST (the separate s28b system — FINDINGS "BATTERY
 * LOCATED ... L3 light", the player +0xA light byte family): the
 * 300-frame (5 s) auto-off burst (+0x28 = 0x12C, down-counting every
 * frame on the player spine 0x00161138; expiry commits anim/event id
 * 0x15D — the turn-off gesture AND the pinned 920 ms switch sound —
 * and drains zero battery, live-verified s28b). The port KEEPS that
 * code (em_weapon.c "SHOULDER-LIGHT BURST") but it is UNHOOKED from
 * Square — nothing arms it until the L3 stealth-light input path is
 * decoded. em_weapon_flashlight_timer() introspects it (always 0).
 * RENDERING (2026-06-11 render-decode session — retires the s47 "visual
 * TODO" flag): the ENGINE TRUTH, pinned by an exhaustive static sweep
 * of the boot ELF (decomp FINDINGS "FLASHLIGHT RENDER DECODE"), is that
 * the toggle draws NOTHING — no beam geometry, no glow sprite, and no
 * vertex-light change is keyed on D_00810D3C or player +0xA (their only
 * readers are gameplay: the enemy-AI awareness checks, the pose-row
 * substitution and the sounds; the engine's per-actor VU1 light matrix
 * carries an ALWAYS-ON camera-direction light instead, and level
 * geometry is baked). The port deliberately DEVIATES: em_weapon_update
 * sets the gfx layer's forward SPOT term (em_gfx_spot_light) from the
 * hand-frame muzzle ray (the laser's anchor; yaw fallback without the
 * clips) — gated EXACTLY like the laser, AIM phase only: light and
 * laser appear together while aiming and vanish together during the
 * draw/reload/holster clips and holstered (the reference capture of
 * the original). The projected circle is SHARP-EDGED per the
 * reference: a ~12-degree cone with a 1-degree smoothstep rim (a
 * crisp disc with a slightly soft edge — em_weapon.c "FLASHLIGHT
 * SPOT" constants). Deviation documented at the API (em_gfx.h
 * "Flashlight spot light").
 * EM_CAPTURE_LIGHT=1 (debug instrumentation): synthesizes ONE Square
 * toggle on the first aim frame so headless captures show the lit disc
 * (use with EM_CAPTURE_AIM=1).
 *
 * LIGHT COMBO (mode 0x21, func_001735C0; per-attack rows idx 0 of the
 * boot-ELF tables — idx 1 is an alternate-context row, anim ids
 * 0x1BD..0x1C1, selected by player +0x236, untranslated):
 *
 *   hit  anim   len  dmg  sound  gate(+0x3C vs T)  chain window
 *   1    0x10B  35   3    0x17D  T=24 (D_002486A0) open at len-19 (D0)
 *   2    0x10C  35   3    0x17E  T=26 (D_002486A8) open at len-19 (D4)
 *   3    0x10D  50   5    0x17F  T=41 (D_002486B0) none (combo ends)
 *
 *   (Clip lengths CORRECTED 2026-06-11: the s36 table measured the
 *   pre-directory-fix bake — the old enumeration shifted every player-
 *   library id >= 54 by up to +3, so "0x10B = 50fr" was really 0x10E's
 *   length. True directory lengths: 0x10B 35, 0x10C 35, 0x10D 50,
 *   0x10E 50, 0x10F 25, 0x110 20, 0x111 20, 0x112 25 — verified
 *   against a fresh fixed-resolver bake, byte-identical to the
 *   re-exported player.emdl.)
 *
 *   - The swing sound + the damage-mailbox write fire together at the
 *     IMPACT gate, unconditionally (range gating is the TARGET's job,
 *     below). +0x25E hit-marker codes 0x81/0x82/0x82 feed the per-swing
 *     effect dispatcher (func_00187350 -> func_00182430, untranslated).
 *   - COMBO: a FIRE-button press during the swing buffers in +0x2E; at
 *     the chain window the next attack starts (blend 1.0). The buffered
 *     chain is checked on the WHIFF path — a CONFIRMED hit instead
 *     EARLY-EXITS to the recover states (engine: the target's +0x0A
 *     flag read back the tick after the mailbox write -> state 0x50).
 *   - RECOVER (states 0x50/0x51/0x52, hit-confirm only): 4-tick pause,
 *     then anim 0x10F (25 fr, blend 4.0), then the 0x63/0x64 exit ramp.
 *     A whiffed swing exits at clip end with NO recover anim.
 *
 * HEAVY (mode 0x22, func_00173E60): anim 0x10E (50 fr, via the
 * D_002754A8 row), damage 0xF = 15 at gate T=43 (D_00248700), sound
 * 0x17F, marker 0x83, release T=29 (D_00248704), then clip-end exit /
 * the same hit-confirm recover. During the swing func_00173DD0 steers
 * yaw toward the goal at D_002486F0[gait] deg-style rates (PORT: the
 * player stays planted — steer untranslated, flagged).
 *
 * TIMING NOTE (2026-06-11: the corrected clip lengths RESOLVE the s36
 * contradiction): under the true lengths the down-count reading
 * impact_tick = len - T is self-consistent for ALL FOUR attacks —
 * impacts at frame 11/9/9 (light 1..3) and 7 (heavy), each safely
 * BEFORE its release gate (len - releaseT = 15/20/20 light, 21 heavy)
 * and inside the clip; the up-count reading would put every release
 * before its impact. The port keeps impact_tick = max(3, len - T)
 * (the 3 covers the request->commit mailbox latency + blend-in) — a
 * live capture remains the final word but is no longer load-bearing.
 *
 * DAMAGE / RANGE: the engine machines write the damage to the melee
 * target link (player +0x18) +0x36 mailbox and let the TARGET-side
 * polls do the range work (e.g. func_00219870 state-1 reads the link's
 * status byte and runs its own func_0019AA80 segment/proximity test) —
 * there is NO global knife-range constant in the player code. The port
 * resolves the victim at the impact tick: nearest live enemy within
 * EM_MELEE_REACH (12.0 — the engine's documented hands-reach, the
 * use-scan dist^2 <= 144 of func_0019A910 mode 6; PORT STAND-IN) inside
 * a 60-degree frontal cone (PORT constant), damaged through the same
 * +0x36 mailbox as the bullet (em_enemy_damage).
 *
 * VISUAL (RESOLVED 2026-06-11 — retires the s36 "no rebind found"
 * flag): there IS no rebind, and none is needed. The hip-HOLSTER node
 * 14 (the knife's attach node, s9) is itself KEYED INTO THE HAND by
 * the swing clips: in every true melee clip (0x10B..0x10F) node 14
 * rides hand node 20 at ~1.0 u for the whole swing (hip distance
 * balloons to 9-15 u) and re-seats on the thigh at the end; in idle/
 * walk/reload it stays parked at the thigh (3.9 u constant). The
 * knife model attached to node 14 therefore swings with the attack
 * automatically — the port's player.emdl (node-14 attachment) already
 * renders this correctly with the corrected clips.
 *
 * KEY MAPPING: CIRCLE = L (light 3-chain, holstered only), SQUARE = J
 * (heavy stab holstered / FLASHLIGHT toggle while aiming) — both per
 * the engine default config (s36: CIRCLE -> mode 0x21 light combo,
 * SQUARE -> mode 0x22 heavy; verified NOT inverted in this module,
 * 2026-06-11 fidelity pass).
 */
#ifndef EM_WEAPON_H
#define EM_WEAPON_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_collision.h"
#include "game/em_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Weapon states — native names; engine mapping in the comments (the
 * armed-stance major-state byte player +0x06, jtbl arms in section 2 of
 * the FINDINGS "WEAPON SYSTEM" write-up). */
enum {
    EM_WPN_HOLSTERED = 0, /* not in an armed stance (+0x05 not 0x1D..0x20) */
    EM_WPN_DRAW,          /* major 0 ENTER: anim 0x110, sound 0x162,
                           * reload-if-empty func_0017B300(.,0); major 1
                           * (wait for the R1 hold) is collapsed into it —
                           * holding R1 is already the port's draw input */
    EM_WPN_AIM,           /* major 2 AIM/FIRE loop (fire sub-machine)      */
    EM_WPN_RELOAD,        /* major 3: anim 0x33 gates firing; the mag is
                           * already refilled (func_0017B300 runs first)   */
    EM_WPN_HOLSTER        /* major 0x65: anim 0x111, sound 0x163           */
};

/* Fire-mode select — the engine's D_00810C61 values. */
enum {
    EM_WPN_MODE_SEMI  = 0,
    EM_WPN_MODE_BURST = 1,
    EM_WPN_MODE_AUTO  = 2
};

/* Reset to HOLSTERED with this ammo state (mag = rounds in the magazine,
 * reserve = TOTAL pool including the mag). Scene-init slot. */
void em_weapon_reset(uint8_t mag, int16_t reserve);

/* Per-frame update: the player-side state machine + fire sub-machine and
 * the gun-side fire-event consumption (one-frame latency, see header).
 * `coll` (may be NULL / empty) is the world the hitscan ray runs through;
 * the ray leaves from the hand-bone muzzle point along the gun axis
 * ("MUZZLE ANCHORING" above; chest-height/yaw is the flagged fallback
 * for a player EMDL without the weapon clips). Call once per gameplay
 * frame. */
void em_weapon_update(const EmCollision *coll, const float player_pos[3],
                      float player_yaw, const EmFrameInput *in);

/* Queue this frame's weapon visuals: the LASER SIGHT (world-space beam +
 * hit dot through em_gfx_beam/em_gfx_beam_dot — header block above) in
 * the AIM state, plus the MUZZLE FLASH FX while one is alive (a flash
 * outlives a stance drop, like the engine's pool FX actor). Also caches
 * `gfx` for the update stage's hand-bone reads. Queues nothing while
 * holstered with no live flash, so the default frame stays
 * byte-identical. */
void em_weapon_render(EmGfx *gfx);

/* 1 while the weapon is in the armed stance (DRAW / AIM / RELOAD — the
 * engine's player weapon modes 0x1D..0x20), 0 holstered/holstering. The
 * camera consumes this for the aim-state target-height offset (struct
 * +0x8C) — see "AIM CAMERA HOOKUP" above for the one-line em_game use. */
int em_weapon_is_aiming(void);

/* --- KNIFE / MELEE (header block above; engine modes 0x21/0x22) -------- */

/* Melee phases — native names; engine mapping in the comments. */
enum {
    EM_MELEE_IDLE = 0,   /* not in a melee mode (+0x05 not 0x21/0x22)   */
    EM_MELEE_SWING,      /* a light hit 1..3 or the heavy stab playing
                          * (engine majors 1..3 of func_001735C0, or
                          * 1..4 of func_00173E60)                      */
    EM_MELEE_RECOVER     /* hit-confirm recover (engine 0x50/0x51/0x52:
                          * 4-tick pause + anim 0x10F)                  */
};

/* 1 while a melee attack (swing or recover) owns the player — em_game's
 * player_move plants the player exactly like the armed stance (the
 * engine's melee modes replace the locomotion modes outright; the heavy
 * yaw steer func_00173DD0 is untranslated, flagged). */
int em_weapon_is_melee(void);

/* Introspection (debug / self-tests). */
int em_weapon_melee_state(void);  /* EM_MELEE_*                          */
int em_weapon_melee_combo(void);  /* current light combo hit 1..3, or 0
                                   * (the heavy reports 0; see _heavy)   */
int em_weapon_melee_heavy(void);  /* 1 = the active swing is the heavy   */
int em_weapon_melee_swings(void); /* attacks started since reset         */
int em_weapon_melee_hits(void);   /* impact-tick victims since reset     */

/* FLASHLIGHT (header block above): em_weapon_flashlight = the
 * persistent D_00810D3C preference flag (1 while set — note the SPOT
 * renders only in the AIM phase). em_weapon_flashlight_timer = frames
 * left on the SEPARATE shoulder-light burst (the dormant s28b system;
 * 0 until its L3 input path is decoded) — self-test introspection. */
int em_weapon_flashlight(void);
int em_weapon_flashlight_timer(void);

/* Live ammo state — the HUD's EmPlayerStatus mirrors these. */
uint8_t em_weapon_mag(void);
int16_t em_weapon_reserve(void);

/* Fire-mode select (D_00810C61). Out-of-range values are ignored. */
void    em_weapon_set_fire_mode(uint8_t mode);
uint8_t em_weapon_fire_mode(void);

/* Introspection (debug / self-tests). */
int em_weapon_state(void);    /* EM_WPN_* */
int em_weapon_shots(void);    /* rounds actually fired since reset */
int em_weapon_reloads(void);  /* reloads (auto + manual) since reset */
int em_weapon_last_hit(void); /* last resolved shot: 1 hit, 0 miss, -1 none */

/* The honest state windows, in gameplay ticks: ceil(clip frames / rate)
 * for the committed anim (draw 0x110 @1.4, reload 0x33 @1.0, holster
 * 0x111 @1.0), or the flagged fallback constants when the loaded player
 * EMDL lacks the clip. The weapon self-test derives its checkpoint
 * schedule from these, so the test stays honest for any asset. */
int em_weapon_draw_ticks(void);
int em_weapon_reload_ticks(void);
int em_weapon_holster_ticks(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_WEAPON_H */
