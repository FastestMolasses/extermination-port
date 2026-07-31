/* em_truck.h — the AREA-11 WEDGED TRUCK actor, native.
 *
 * Faithful translation of placement-table record 16 (behavior
 * ov 0x00823FF0) and its paired AABB fall-trigger record 17
 * (ov 0x008251E0) — the "run across the top before it falls into the
 * crevice" set-piece. Full decode: decomp repo
 * Extermination/docs/INVESTIGATION_first_level_area11.md §11 + §11.1a.
 *
 * The truck is a rigid prop (model-local mesh, the actor builds its own
 * 4x4 world matrix from baked constants — there is no library-model byte)
 * jammed into the SE crevice at (380.8, 164.0, 391.1), tilted rx -0.314
 * and yawed ry pi/2. It begins WEDGED (a small idle teeter). When the
 * player steps into the trigger volume the truck ARMS and FALLS: an
 * accelerating downward drop over a ~1-second window, with sound 0x454 and
 * debris FX. Its walkable TOP is a moving surface (em_collision_moving_*):
 * a player standing on the top while it falls is carried DOWN into the
 * crevice (the fail); a player who runs off the footprint before the
 * window closes leaves it and is safe.
 *
 * STATE MACHINE (PS2 actor +0x04):
 *   STATE_WEDGED (4) — idle/teeter; spawn here; registers vel = 0.
 *   STATE_FALL   (1) — the timed tumble; registers the accelerating
 *                      downward vel and integrates the truck position.
 *
 * The CARRY is wired in em_game.c: em_collision_moving_clear() once at the
 * top of the gameplay frame, em_truck_update() registers the truck's
 * footprint + this-frame velocity, and the player ground-solve consumes
 * em_collision_moving_carry(player_pos) (the single player-code touch).
 *
 * Zero dependencies beyond the port's em_model / em_gfx / em_sfx /
 * em_collision modules; the mesh is the git-ignored, user-generated
 * assets/scene_snow/props/area_truck.emdl.
 */
#ifndef EM_TRUCK_H
#define EM_TRUCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct EmGfx;
struct EmGfxMesh;

/* Reset the truck module to "no truck" (scene unload / new area build).
 * Frees the loaded mesh/model/palette against `gfx` if one is present. */
void em_truck_clear(struct EmGfx *gfx);

/* Install the truck actor for this scene: load `model_path` (an EMDL under
 * the scene dir) and seat the wedged actor at world `pos` with the placed
 * Euler tilt `rx` and yaw `ry`. Spawns in STATE_WEDGED (idle teeter).
 * Returns 0 on success, nonzero if the mesh failed to load (the actor is
 * then absent — the trigger/fall logic does NOT run without a placed
 * truck, matching the engine never instantiating record 16 without it).
 * One truck per scene; a second call replaces the first. */
int em_truck_install(struct EmGfx *gfx, const char *model_path,
                     const float pos[3], float rx, float ry);

/* Is a truck installed this scene? */
int em_truck_present(void);

/* Per-frame update (call ONCE per gameplay frame, AFTER
 * em_collision_moving_clear() and BEFORE the player ground-solve consumes
 * em_collision_moving_carry — i.e. near the top of the sim). `player_pos`
 * is the player world position (footprint X/Z) used for the trigger-AABB
 * test. The update: advances the state machine, on ARM fires the audio cue
 * and transitions WEDGED->FALL, integrates the truck's own fall, and
 * registers the truck's walkable-top footprint + current velocity with the
 * moving-surface registry so the player carry can ride it. No-op when no
 * truck is installed. */
void em_truck_update(const float player_pos[3]);

/* Publish this frame's render draw (rigid prop, re-posed by the update).
 * Returns 1 and writes *mesh / *palette / *bone_count when a truck is
 * drawable, else 0. Same pointer contract as the door/elevator draws. */
int em_truck_draw(struct EmGfxMesh **mesh, const float **palette,
                  uint32_t *bone_count);

/* Test hooks (headless; EM_TRUCK_TEST). Force-arm the fall, and read the
 * state/position for assertions. em_truck_state() returns the PS2 +0x04
 * value (4 wedged, 1 falling, 0 done/never). */
void  em_truck_force_arm(void);
int   em_truck_state(void);
void  em_truck_pos(float out[3]);
int   em_truck_moving_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_TRUCK_H */
