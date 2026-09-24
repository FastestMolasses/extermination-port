/* em_collision_world.h - the scene's one original collision world (census
 * lanes L05..L08; docs/ACTOR_COLLISION.md section 7, COLL_PROBES.md section 4,
 * COLL_SEGMENT_WALKERS.md section 3, COLL_LIST_PASSES.md section 4).
 *
 * The original keeps its collision state in a handful of globals and one
 * scratchpad; this module holds exactly one copy of each for the live game:
 *
 *   *0x70003250 / 0x7000324C  the cell directory (EmActorCellTable), loaded
 *                             from the user's own export at every area build
 *   D_00275B54..D_00275BB8    the per-frame class lists (EmActorClassLists):
 *                             reset by 001AF8E0's list half, pushed by the
 *                             owners' 001B1B70, published by 001AAD00
 *   the set-4 grid            the loaded EMCL (g.coll) and its rank section
 *                             (EmCollProbeGrid; EMCL flags 7)
 *   0x70003190..0x70003B88    the walkers' scratchpad (one EmCollProbeState
 *                             and one EmCollSegmentFaceScratch), zeroed at
 *                             every area build and shared by every query
 *   D_0026C170..D_0026C658    the SDK math tables and D_0026C5D0 (the
 *                             user's export, assets/sdk_math_tables.emsm)
 *   D_0024295C, *0x00242670   the soft-float workers' errno pointer and
 *                             errno word (assets/sdk_soft_float.emsf; loaded
 *                             once, kept across area builds), bound with
 *                             em_sdk_soft_float into the one SDK context
 *
 * Only AREA11 has an original world (the roster scene). Other scenes leave it
 * unloaded and their callers keep the port's own collision (em_collision.c).
 *
 * Fail-stop: every entry point returns -1 when its original cannot run over
 * this world (not loaded, a walker fault, a list pass that reached an
 * unported callee); nothing is substituted. */
#ifndef EM_COLLISION_WORLD_H
#define EM_COLLISION_WORLD_H

#include <stdint.h>

#include "game/em_actor_collision.h"
#include "game/em_coll_grid_hull.h"
#include "game/em_coll_move_original.h"
#include "game/em_coll_probe_original.h"
#include "game/em_coll_segment_walkers.h"
#include "game/em_collision.h"
#include "game/em_player.h"
#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The AREA11 cell directory export (tools/test_actor_collision_reference.py
 * --export; docs/STARTUP.md). */
#define EM_COLLISION_WORLD_CELLS_PATH "assets/scene_snow/area11_cells.bin"
#define EM_COLLISION_WORLD_SDK_PATH "assets/sdk_math_tables.emsm"
/* The soft-float data export (tools/export_sdk_math_tables.py; STARTUP.md). */
#define EM_COLLISION_WORLD_SOFT_FLOAT_PATH "assets/sdk_soft_float.emsf"

/* Build the world for an area: the directory at `cells_path`, the rank
 * section of the EMCL at `emcl_path` (which `emcl` is the loaded copy of; it
 * must carry EM_COLL_FLAG_NODE_CLASS | EM_COLL_PROBE_FLAG_RANKS) and the SDK
 * tables at `sdk_path`, with the soft-float workers bound over the data at
 * EM_COLLISION_WORLD_SOFT_FLOAT_PATH (read by the first load only). The
 * scratchpad state is zeroed and the class lists reset. Returns 0, or -1 (the world is left unloaded; a line names the
 * missing export on stderr). */
int em_collision_world_load(const EmCollision *emcl, const char *emcl_path, const char *cells_path,
                            const char *sdk_path);
/* Drop the world (a scene without an original roster). */
void em_collision_world_unload(void);
int em_collision_world_loaded(void);

/* 001AF8E0's class-list half: every cursor at its base, every count 0. */
void em_collision_world_lists_reset_001AF8E0(void);
/* The lists themselves (published and live), for the Use scan's
 * interactive list (EM_ACTOR_LIST_FLAG80). */
const EmActorClassLists *em_collision_world_lists(void);

/* 001B1B70(actor): push `actor` (its +0x14) onto the lists its class byte
 * selects (EmActor.cls, +0x02) and, for class bit 0x80, the interactive list.
 * 1, or -1. */
int em_collision_world_publish_001B1B70(const EmActor *actor);
/* 001B1D20(actor): the class-4 push alone (the drums' contact worker inside
 * 50 units of the player). 1, or -1. */
int em_collision_world_push4_001B1D20(const EmActor *actor);
/* 001A2370(actor, matrix): re-transform the actor's extended cell (uid
 * +0x0E >> 8) by `matrix` and rebuild its AABB. 1, or -1 (not loaded). */
int em_collision_world_retransform_001A2370(const EmActor *actor, const float matrix[16]);

/* 001AAD00's collision half: the nine list-pass hooks (em_coll_list_passes,
 * over the live lists) and then the list block (publish every list, reset
 * the cursors). `scene` supplies 0x70003B8D and the area bytes, `d28A9A0` the
 * fade word D_0028A9A0. 0, or -1 with *fault = the original address of the
 * failing call. */
int em_collision_world_close_out_001AAD00(const EmSceneState *scene, int16_t d28A9A0, uint32_t *fault);

/* 0019A910(from, to, mask) over the world; *hit gets the caller's view
 * (em_coll_segment_hit) when the result is nonzero. 0, 1, 2 or 4, or -1. */
int em_collision_world_0019A910(const float from[3], const float to[3], unsigned mask,
                                EmCollSegmentHit *hit);
/* 0019B7D0(from, to) (the camera's attr-0x78 ground query); on a hit *hit
 * as above. 0 or 4, or -1. */
int em_collision_world_0019B7D0(const float from[3], const float to[3], EmCollSegmentHit *hit);

/* The floor service's collision workers over this world (census L06/L07;
 * docs/COLL_PROBES.md section 4, ACTOR_COLLISION.md section 7 item 4):
 * b->ground = 0019AB20 (em_actor_collision_player_ground), b->grid = the EMCL
 * with the node class, b->head / b->object = 0019B6C0 / 0019B8C0
 * (em_coll_probe_player_*, with 001A50A0 / 001A5C30 as pass-2 workers),
 * b->link_test = 00175640 and b->column = 0019BC40 (with the SDK 0011E748 /
 * 0011DBB8 of em_sdk_math_original; a fault they record fails the column),
 * and 00175CF0's SDK calls b->atan2 / tangent / atan / sqrt = 0011E620 /
 * 0011E398 / 0011DBB8 / 0011E748 over the same context (soft-float bound).
 * `self` is the player's identity (+0x14) and `cls` its +0x02 byte. Returns
 * 0, or -1 when the world is not loaded (nothing is bound). The FLOOR
 * mechanism these serve stays gated until its other prerequisites exist
 * (player_states_missing). */
int em_collision_world_bind_player(EmPlayerStatesBinding *b, const void *self, uint8_t cls);

/* The world's other original walkers, for the binders of the player states
 * and owners (NULL while the world is not loaded):
 *   em_collision_world_move       0019AD00 / 0019AFE0's world (the cells, the
 *                                 rank grid, the hull locks' world and the SDK
 *                                 context); the hull world has no chain
 *                                 reader (AREA11 publishes no class-2 owner:
 *                                 a lock that needs a chain faults) and no
 *                                 001A7280 player
 *   em_collision_world_move_scratch  their one scratchpad state (0x70003190..
 *                                 0x700031D8 as the move walkers leave it)
 *   em_collision_world_segment    0019A570 / 0019A910's segment (the probe
 *                                 state, the face scratch, the same hulls)
 *   em_collision_world_cells      the actor-collision world (0019AB20,
 *                                 0019BC40 and the owners' probes)
 *   em_collision_world_sdk        the one SDK context (tables, soft float)
 *   em_collision_world_column_math  0019BC40's SDK workers over it
 *   em_collision_world_player     the player's 0019AB20 query view */
/* The 00174A50 worker 001756E0's probe set calls (pose(context, actor,
 * 12.0) over the live record); unbound, reaching it faults. */
void em_collision_world_bind_player_pose(int (*pose)(void *context, EmPlayerLiveActor *actor,
                                                     float blend),
                                         void *context, EmPlayerLiveActor *actor);
const EmCollMoveWorld *em_collision_world_move(void);
EmCollMoveScratch *em_collision_world_move_scratch(void);
const EmCollSegment *em_collision_world_segment(void);
EmActorCollisionWorld *em_collision_world_cells(void);
EmSdkMathContext *em_collision_world_sdk(void);
const EmCollColumnMath *em_collision_world_column_math(void);
EmActorCollisionPlayer *em_collision_world_player(void);
/* 0019BC40's player column view (the fall check's `column`). */
EmActorCollisionPlayerColumn *em_collision_world_column_player(void);
/* The player's EmCollMovePlayer (0019AD00 / 0019AFE0 with the live record
 * as the query actor): the context of em_coll_move_player_* and
 * em_coll_move_climb_* / _slide_*. */
EmCollMovePlayer *em_collision_world_move_player(void);

/* EM_COLL_WORLD_DUMP=<path>: after every 001AAD00 the directory image and
 * the published class-4 uids are written to <path> (the last frame wins;
 * tools/test_collision_world_capture.py compares them with the route
 * captures). Instrumentation only. */
void em_collision_world_dump_if_requested(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_COLLISION_WORLD_H */
