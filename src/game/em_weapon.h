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
 * KEY MAPPING (port choices, via the em_input.h keyboard map):
 *   R1 (E) HELD   draw + stay in the armed stance; release = holster.
 *                 Authentic: the engine's action mask +0x200 bit 0x1000
 *                 ("weapon-draw hold") is R1 in the default config.
 *   CROSS (K)     trigger. The engine's fire trigger is config-mapped
 *                 (held mask D_00810E74 & *0x70003B78); the default-config
 *                 button is not pinned in FINDINGS yet — CROSS is the
 *                 port's stand-in until the config table is decoded.
 *   SQUARE (J)    manual reload (top-up). DEVIATION: the engine uses L3
 *                 (raw pad bit 0x200, func_0017B300(.,2)), but the
 *                 keyboard map has no L3 key; SQUARE is the only face
 *                 button with no gameplay role in the port yet.
 *
 * VISUAL FEEDBACK — PLACEHOLDER for the real beam/flash pass (the gun
 * actor's per-frame drawers func_001854E0/func_00185760 + muzzle FX
 * func_00187CC0 + tracer func_001860A0, none translated yet): the port
 * draws screen-space overlay rects (em_gfx_overlay_rect, 640x448 canvas) —
 * a 3-frame muzzle-flash flare at the lower center and a center-screen
 * crosshair that pulses bright/large on a ray HIT and dim/small on a
 * miss. Sounds (0x162 draw / 0x163 holster / 0x164-0x165 fire / 0x169
 * dry click) and anims (0x110/0x111/0x31../0x33) are recorded in
 * em_weapon.c as comments until an SFX/anim hookup exists.
 *
 * Holstered (the default) the module queues nothing and touches nothing,
 * so default-run frame output stays byte-identical to pre-weapon builds.
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

/* Queue this frame's placeholder feedback (crosshair + muzzle flash)
 * into the overlay pass. Queues nothing while holstered. */
void em_weapon_render(EmGfx *gfx);

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

#ifdef __cplusplus
}
#endif

#endif /* EM_WEAPON_H */
