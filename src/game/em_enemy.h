/* em_enemy.h — placed crawler enemies: the port's first hostile actors.
 *
 * Native translation of the engine's most common placed enemy (decomp
 * repo Extermination/docs/FINDINGS.md "ENEMY AI ARCHITECTURE", session
 * 22 — func_001551B0, 95 placements across 13 areas):
 *
 *   func_001551B0  crawler behavior (lifecycle byte actor +0x04, engine
 *                  values kept: 0 INIT -> 4 IDLE -> 1 ATTACK -> 2 DEATH
 *                  -> 3 FREE)                     -> em_enemy_update()
 *   actor +0x34    HIT POINTS, s16 (crawler init = 1: any damage kills)
 *   actor +0x36    INCOMING-DAMAGE MAILBOX, s16 — attackers write it
 *                  (low bits = amount, high bits = weapon-type flags,
 *                  e.g. 0x4000), the behavior polls + clears it in its
 *                  own tick. There is NO central HP system.
 *                                                 -> em_enemy_damage()
 *   actor +0x0A    GROUP-ALARM flag: a damaged crawler walks the live
 *                  actor list and wakes every other crawler.
 *   +0x2D0..0x2EC  4 precomputed diagonal probe directions (INIT) — the
 *                  steer phase probes them with func_0019AB20 and turns
 *                  the velocity vector +-3 deg/frame (0x3D56774F ~=
 *                  0.0524 rad) away from blocked sides.
 *   hop physics    velocity integration with vertical gravity
 *                  0.052/tick (state 1 sub 1).
 *
 * FIDELITY NOTES (port deviations, each flagged in em_enemy.c):
 *  - WAKE: the documented crawler IDLE wakes only via the group alarm
 *    (+0x0A) or by being damaged. "Seeing" the player is distance-only
 *    in this engine (FINDINGS: leech <= 32 u; no creature ray-tests for
 *    vision), so the port additionally wakes a crawler when the player
 *    is within that same documented 32-unit radius — otherwise a lone
 *    placed crawler would never engage.
 *  - DAMAGE WINDOW: FINDINGS notes state 1 (attack) never visibly polls
 *    +0x36 ("crawlers appear undamageable mid-lunge — verify live", an
 *    open item). The port polls the mailbox in IDLE and ATTACK so the
 *    player can always shoot back; revisit after the live verify.
 *  - LUNGE DAMAGE: the crawler is a suicide attacker (it bursts on/after
 *    the lunge). The amount it deals the player is not pinned in
 *    FINDINGS; the port writes the engine's documented contact-damage
 *    code 0x400A (type 0x4000 | amount 10, the func_001A9480 swipe/
 *    contact pass) into a player-side mailbox with the same +0x36
 *    semantics. em_game.c consumes it into EmPlayerStatus.health.
 *  - DEATH: the engine's state 2 spawns nest children, gore FX, a
 *    MODEL REBIND to the gib models (library entries 0x22/0x29 — the
 *    leech clip bank has NO death clip; FINDINGS "CRAWLER RESOLVED")
 *    and a knockback corpse-slide. None of that is translated: the
 *    gameplay slot despawns immediately, and a VISUAL-ONLY placeholder
 *    draws the frozen last pose sinking for a few frames (flagged in
 *    em_enemy.c — a fade needs renderer per-draw alpha, not yet there).
 *  - SPEED/RANGES: hop forward speed, hop airtime, the burst-on-player
 *    radius and the per-model hit-sphere radius are not exported from
 *    the disc; the port constants are flagged in em_enemy.c (the lunge
 *    contact reuses the engine's documented radius-6 contact test).
 *
 * MESH: assets/enemy_crawler.emdl (EMD2/EMD3 via the em_model API,
 * disc-derived, generated locally, git-ignored) when present; otherwise
 * a PLACEHOLDER box-ish crawler built procedurally at runtime (original
 * vertices, NOT disc data) through em_gfx_mesh_create.
 *
 * ANIMATION: with the EMD3 multi-clip asset (the leech clip bank, 4
 * clips: 0 crawl loop / 1 emerge / 2 windup / 3 lunge — FINDINGS
 * "CRAWLER RESOLVED" section 4) the state machine drives a visual-only
 * anim layer with 0.15 s crossfades: emerge on spawn, the crawl loop
 * slow in IDLE and ground-speed-scaled in ATTACK (the loco clips are
 * baked in place; root motion = the entity's own hop integration), and
 * windup -> lunge at close range under the suicide burst. An EMD2 or
 * single-clip asset (and the placeholder mesh) keeps the old static
 * frame-0 pose. The layer never feeds back into the gameplay state
 * machine, so spawn/wake/lunge/death TIMING is identical either way.
 *
 * Instances come from the SCENE MANIFEST: `enemy crawler <x> <y> <z>
 * <yaw>` lines (parsed by em_game.c next to the door lines). No enemy
 * lines = this module never loads, updates or draws anything, keeping
 * default-run frame output byte-identical.
 */
#ifndef EM_ENEMY_H
#define EM_ENEMY_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_collision.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Instance-list capacity (em_game.c sizes its render chain with this). */
#define EM_ENEMY_MAX 16

/* Lifecycle states — the ENGINE's values for the actor byte +0x04. */
enum {
    EM_ENEMY_INIT   = 0,
    EM_ENEMY_ATTACK = 1,
    EM_ENEMY_DEATH  = 2,   /* burst/death sequence (engine sub-machine) */
    EM_ENEMY_FREE   = 3,   /* released (func_001AFC10); slot inactive   */
    EM_ENEMY_IDLE   = 4    /* dormant on the nest                       */
};

/* Reset the instance list (boot / scene reload). Does not free GPU
 * resources — pair with em_enemy_shutdown for that. */
void em_enemy_reset(void);

/* Spawn one crawler at `pos` facing `yaw` (a manifest line or a test
 * script). Loads the shared mesh on first use (asset, else the runtime
 * placeholder). Returns the instance index, or -1. */
int em_enemy_add(EmGfx *gfx, const float pos[3], float yaw);

/* Per-frame update: every crawler's state machine (idle/alarm wake,
 * steer + hop toward the player, lunge, mailbox-driven death). `coll`
 * (may be NULL / empty) is the world the steering probes and floor
 * queries run through. Call once per gameplay frame, at the actor-pool
 * tick (func_001AFD70), before the weapon update so shots see this
 * frame's positions. */
void em_enemy_update(const EmCollision *coll, const float player_pos[3]);

/* Write the incoming-damage mailbox (actor +0x36): low bits = amount,
 * high bits = weapon-type flags. The crawler consumes it in its tick. */
void em_enemy_damage(int i, int16_t code);

/* Player-side damage mailbox (same +0x36 semantics, pointed at the
 * player): the lunge writes it, em_game.c consumes it into
 * EmPlayerStatus.health. Returns the pending code and clears it. */
int em_enemy_player_hit_take(void);

/* Hitscan support for em_weapon.c — keeps the em_collision world API
 * untouched (port choice, documented in em_weapon.h):
 *
 * em_enemy_acquire: nearest live crawler within `max_dist` of `from`
 * whose XZ bearing lies inside the facing cone (dot >= cone_cos).
 * Stand-in for the engine's screen-space target acquisition
 * (func_00199220); writes the target's AIM POINT. Returns index or -1.
 *
 * em_enemy_ray_test: nearest live crawler whose hit sphere intersects
 * the segment [from, to] (the per-victim test the bullet runs BEFORE
 * crediting a world hit). Writes the entry point. Returns index or -1. */
int em_enemy_acquire(const float from[3], float yaw, float max_dist,
                     float cone_cos, float aim_out[3]);
int em_enemy_ray_test(const float from[3], const float to[3],
                      float hit_out[3]);

/* Draw accessors for the render chain. em_enemy_draw returns 0 for an
 * inactive slot, EXCEPT while the death-sink placeholder is still
 * lowering the frozen corpse pose (a few frames after the burst). */
int em_enemy_count(void);
int em_enemy_draw(int i, EmGfxMesh **mesh, const float **palette,
                  uint32_t *bone_count);

/* Introspection (debug / self-tests). */
int  em_enemy_alive(void);        /* live instances (INIT/IDLE/ATTACK) */
int  em_enemy_state(int i);       /* engine lifecycle value, or -1     */
int  em_enemy_hp(int i);          /* the +0x34 halfword                */
void em_enemy_pos(int i, float out[3]);

/* Destroy the GPU mesh + free the model. */
void em_enemy_shutdown(EmGfx *gfx);

#ifdef __cplusplus
}
#endif

#endif /* EM_ENEMY_H */
