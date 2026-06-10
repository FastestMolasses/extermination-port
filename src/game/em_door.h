/* em_door.h — interactive doors: the port's first interactive objects.
 *
 * Native translation of the engine's class-5 double-door actor (decomp
 * repo Extermination/docs/FINDINGS.md "FIRST INTERACTIVE OBJECTS"):
 *
 *   func_001BC350  door behavior (per-frame state machine on the actor's
 *                  sub-state byte +0x05) -> em_door_update()
 *   func_00184BA0  player-side USE SCAN (nearest interactive-list
 *                  candidate: dist^2 <= 144, LOS clear over mask 6,
 *                  facing-dot >= ~0.4, immediate when dist < 2)
 *                  -> the trigger scan inside em_door_update()
 *   func_001BC300  per-frame articulation + publish/draw
 *                  -> em_door_palette build + the draw accessors
 *
 * Door instances come from the SCENE MANIFEST's doors section (one line
 * per placed door, written by the decomp repo's export_props.py --doors):
 *
 *   door <file.emdl> <x> <y> <z> <yaw> <trigger_radius>
 *
 * The EMDL is the door's own articulated model in DOOR-LOCAL space
 * (bone 0 = the door panel, bone 1 = the lock fixture; frame 0 = the
 * captured closed pose). Door files live under <scene>/doors/ so the
 * static scene loader (em_game.c scene_load, which slurps every .emdl in
 * the scene dir) never double-draws them.
 *
 * FIDELITY NOTES (port deviations, each flagged in em_door.c):
 *  - The engine triggers doors on WALK-INTO (player locomotion state
 *    0x2D = pressing forward); the port additionally requires the CROSS
 *    button except inside the engine's own 2.0-unit immediate radius.
 *  - The engine's open articulation is a keyframe clip on the door
 *    skeleton (1.0 frame/tick). That clip is not yet located on disc
 *    (export_props.py --doors hunts for it on every export), so a door
 *    EMDL with no baked animation plays a PLACEHOLDER 90-degree hinge
 *    swing of the same duration class. A future re-export with the real
 *    clip is picked up automatically (the EMDL then carries >1 frame).
 *  - Engine sub-states 4/5 commit a room/area transition (per-area
 *    destination tables D_0024E140) and close once the transition byte
 *    clears; the port has no area loader yet, so OPEN holds for a
 *    timeout and re-closes (never on top of the player).
 *
 * COLLISION: the engine gives placed objects collision through per-uid
 * AABB records in the area state blob (movable-hull set, mask bit 0) —
 * NOT through the static world, and the office EMCL bake confirms both
 * doorways are open in the static sets. em_door_probe() is the native
 * movable-hull stand-in: a segment-vs-AABB test against every door that
 * is not fully open, consumed by the player move probe (em_game.c). The
 * camera solver keeps the engine's own mask 6 (static only), so the
 * camera sees through doors exactly like the original.
 */
#ifndef EM_DOOR_H
#define EM_DOOR_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_collision.h"
#include "game/em_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Instance-list capacity (the office has 2; the render chain in
 * em_game.c sizes its draw slots with this). */
#define EM_DOOR_MAX 8

/* Door sub-states — the ENGINE's values for the actor byte +0x05
 * (jtbl_0026E1C0). 1/2 (the locked sequence, model 0x15 security doors)
 * need the unlock bitmask D_00810841 and are not in the port yet. */
enum {
    EM_DOOR_CLOSED  = 0,
    EM_DOOR_OPENING = 3,
    EM_DOOR_OPEN    = 4,   /* engine: transition commit; port: hold */
    EM_DOOR_CLOSING = 5
};

/* Reset the instance list (boot / scene reload). Does not free GPU
 * resources — pair with em_door_shutdown for that. */
void em_door_reset(void);

/* Add one door instance from a manifest line. `file` is relative to
 * `scene_dir` (e.g. "doors/door_m03.emdl"); the model is loaded and
 * uploaded once per distinct file. Returns 0 on success. */
int em_door_add(EmGfx *gfx, const char *scene_dir, const char *file,
                const float pos[3], float yaw, float radius);

/* Per-frame update: trigger scan (the func_00184BA0 use scan against
 * this player position/facing + the frame input block) and every door's
 * state machine + articulation palette. `coll` (may be NULL) is the
 * static world used for the engine's line-of-sight gate. Call once per
 * gameplay frame, at the world-services slot (func_001AFD70). */
void em_door_update(const EmCollision *coll, const float player_pos[3],
                    float player_yaw, const EmFrameInput *in);

/* Active door-transit MOVE-TO (func_001BBE40's player walk-through; the
 * engine's gameplay-frame selector 3 "door transit" variant). While it
 * returns 1, the player glides to `out_target` with yaw snapped to
 * `out_yaw`, collision-free — this is how the engine carries the player
 * across the statically sealed doorway boundary planes. Consumed by
 * em_game.c player_move. */
int em_door_transit_active(float out_target[3], float *out_yaw);

/* Draw accessors for the render chain. */
int  em_door_count(void);
void em_door_draw(int i, EmGfxMesh **mesh, const float **palette,
                  uint32_t *bone_count);

/* Movable-hull segment probe (collision-set bit 0 stand-in): nearest
 * blocking-door AABB hit on [from, to], or 0. A fully OPEN door does not
 * block (the engine clears the object's collision state word). */
int em_door_probe(const float from[3], const float to[3], EmCollHit *hit);

/* Introspection (debug / self-tests). */
int  em_door_state(int i);
void em_door_pos(int i, float out[3]);

/* Destroy GPU meshes + free models. */
void em_door_shutdown(EmGfx *gfx);

#ifdef __cplusplus
}
#endif

#endif /* EM_DOOR_H */
