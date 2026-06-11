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
 * KEY MAPPING (engine-faithful since the s29 live decode of the default
 * config block — em_input.h "ENGINE DEFAULT BUTTON CONFIG" has the full
 * spad 0x70003B70..7E table):
 *   R1 (E) HELD   draw + stay in the armed stance; release = holster
 *                 (config slot 0x3B7C = R1, the weapon-draw hold).
 *   CIRCLE (L)    FIRE — the real default-config trigger (config slot
 *                 0x3B78 = 0x0020 = CIRCLE, live-verified s29). CROSS
 *                 stays USE/confirm (slot 0x3B76 — the door use scan).
 *   L3 (key 2)    manual reload (top-up) — the engine's raw L3 pad bit
 *                 (NOT config-mapped), func_0017B300(.,2). The keyboard
 *                 map has an L3 key (2 — em_input.h) so the port no
 *                 longer needs the old SQUARE deviation.
 *   CIRCLE (L) / SQUARE (J) while HOLSTERED = the KNIFE attacks (light
 *                 combo / heavy stab — the s36 melee decode; the
 *                 "KNIFE / MELEE" block below). SQUARE while AIMING =
 *                 the attachment-0 sub-weapon toggle (sound 0x179) —
 *                 the s29 "unidentified action" open item, now decoded.
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
 *     glow, color R = (0x50 + rand5)/0x80, G = B = 0 (locked: 5.0-unit,
 *     R/G/B = (0x70/0x40/0x20 + rand5)/0x80).
 * Both draws are additive with depth test on / write off, exactly the
 * GS states of the original pass (em_gfx_beam / em_gfx_beam_dot).
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
 * MUZZLE/FLASH FEEDBACK — still PLACEHOLDER (muzzle FX func_00187CC0 +
 * tracer func_001860A0 not translated): screen-space overlay rects —
 * a 3-frame muzzle-flash flare at the lower center and a center-screen
 * crosshair that pulses bright/large on a ray HIT and dim/small on a
 * miss.
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
 * While the rifle IS drawn (armed stances 0x31/0x32/0x34/0x35), SQUARE
 * routes to the SUB-WEAPON action func_0017A970 instead; with attachment
 * 0 (no underbarrel mounted, D_00810CA6 == 0) it just TOGGLES the global
 * D_00810D3C with sound 0x179 on toggle-ON — this is exactly the s29
 * live observation ("0x179, no ammo use, no state change"). What
 * D_00810D3C arms is still open (it replays 0x179 + a voice-line latch
 * on the next rifle draw; cleared by the inventory reset func_001AF2C0).
 * The port mirrors the toggle (sound + latched flag, no further effect).
 *
 * LIGHT COMBO (mode 0x21, func_001735C0; per-attack rows idx 0 of the
 * boot-ELF tables — idx 1 is an alternate-context row, anim ids
 * 0x1BD..0x1C1, selected by player +0x236, untranslated):
 *
 *   hit  anim   len  dmg  sound  gate(+0x3C vs T)  chain window
 *   1    0x10B  50   3    0x17D  T=24 (D_002486A0) open at len-19 (D0)
 *   2    0x10C  25   3    0x17E  T=26 (D_002486A8) open at len-19 (D4)
 *   3    0x10D  20   5    0x17F  T=41 (D_002486B0) none (combo ends)
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
 * HEAVY (mode 0x22, func_00173E60): anim 0x10E (20 fr, via the
 * D_002754A8 row), damage 0xF = 15 at gate T=43 (D_00248700), sound
 * 0x17F, marker 0x83, release T=29 (D_00248704), then clip-end exit /
 * the same hit-confirm recover. During the swing func_00173DD0 steers
 * yaw toward the goal at D_002486F0[gait] deg-style rates (PORT: the
 * player stays planted — steer untranslated, flagged).
 *
 * TIMING NOTE (flagged open item): the gates compare the clip time
 * +0x3C (counts UP per the property-table footstep semantics) with
 * c.le.s — read literally the impact lands right after the blend-in
 * for every attack except light hit 1, whose T=24 on a 50-frame clip
 * only fits a count-down (remaining-frames) reading = impact at frame
 * 26. The port uses impact_tick = max(3, len - T) — the down-count
 * reading, which both readings agree on for hits 2/3 and the heavy
 * (T >= len there) — pending a live capture.
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
 * VISUAL (flagged): the knife model (106) stays bound to the hip
 * HOLSTER node 14 (s9 attach decode). No draw-to-hand rebind was found
 * statically in the melee machines or their helpers — if the engine
 * re-binds the knife to the hand for the swing it happens in the
 * equipment-draw selection (the player-blob attach table), which needs
 * a live melee capture to pin. The port leaves the knife holstered
 * during attacks (the swing anims carry the read) — flagged note.
 *
 * KEY MAPPING: CIRCLE = L (light, holstered only), SQUARE = J (heavy
 * holstered / sub-toggle while aiming) — both per the engine default
 * config; the old "SQUARE unbound" note above is retired.
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
 * the ray leaves from chest height above `player_pos` along `player_yaw`.
 * Call once per gameplay frame. */
void em_weapon_update(const EmCollision *coll, const float player_pos[3],
                      float player_yaw, const EmFrameInput *in);

/* Queue this frame's weapon visuals: the LASER SIGHT (world-space beam +
 * hit dot through em_gfx_beam/em_gfx_beam_dot — header block above) in
 * the AIM state, plus the placeholder overlay feedback (crosshair +
 * muzzle flash). Queues nothing while holstered, so the default frame
 * stays byte-identical. */
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
int em_weapon_sub_toggle(void);   /* the D_00810D3C mirror (SQUARE while
                                   * aiming, attachment 0)               */

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
