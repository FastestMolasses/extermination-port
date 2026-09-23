/* em_coll_move_original.h - the original horizontal move/sweep walkers.
 *
 * Lane "collision-move-walkers" (docs/COLL_MOVE.md). Hand translation of the
 * original routines below; the .s is the authority (0019AD00, 0019AFE0 and
 * 0019FE50 are NEARMISS readable C, 001A4830 and 001A4D10 are asm-word files;
 * 001A4030 is byte-matched C):
 *
 *   0019AD00  move probe: segment (actor +0xB0.x, target.y, actor +0xB8.z) ->
 *             target, the end pushed 1% of a unit past the target; mask bit 0
 *             the hull lock (001A6440 / 001A7280, workers), bit 1 the cell
 *             walker 0019FE50, bit 2 the grid pass 0019CB60 (worker), bit 31
 *             adds the hit delta to the actor's +0xB0/+0xB8
 *   0019AFE0  sweep: the same, from (from.x, to.y, from.z) to `to`; after the
 *             passes it adds the 1% step to the START (0019AD00 subtracts it
 *             from the end)
 *   0019FE50  the horizontal cell walker: pass 1 the static cells of the
 *             directory (*0x70003250), pass 2 the published class-4 owners'
 *             cells (D_00275B7C / D_00275B84), each hull AABB-gated and each
 *             prim tested by
 *   001A4830  0x8000 / 0x4000 prims: circle crossing in x/z inside the
 *             prim's vertical band (two SDK sqrt 0011E748 calls, a worker)
 *   001A4D10  0x2000 prims: one axis face (x faces 0..2, z faces >= 5; the
 *             y faces 3/4 never hit a horizontal walk)
 *   001A4030  0x1000 prims: the convex n-gon segment test
 *
 * The scratchpad the originals share is EmCollMoveScratch, field for field
 * (addresses on each member). It is caller-owned and persistent, like the
 * scratchpad: a binder keeps ONE per world and passes it to every call, so a
 * field no call writes keeps its last value exactly as in the original.
 *
 * Arithmetic: every EE COP1 op and VU0 macro op goes through
 * src/game/em_ee_float.h (docs/EE_FLOAT_MODEL.md); nothing here does host
 * float arithmetic. A VU form the header refuses is a fault.
 *
 * Fail-stop: every original callee that is not translated here is a named
 * worker. A call whose flags can reach a missing worker returns -1 before it
 * writes anything; a malformed directory, a static cell without its kind
 * view and a published owner whose offset word carries bit 31 also fault.
 *
 * Verified by tools/test_coll_move_reference.py (the original instructions
 * over captured AREA11 RAM and the 05_boxes / 06_hill_slide route beats).
 */
#ifndef EM_COLL_MOVE_ORIGINAL_H
#define EM_COLL_MOVE_ORIGINAL_H

#include <stdint.h>

#include "game/em_actor_collision.h"
#include "game/em_player_climb.h"
#include "game/em_player_floor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The record 0x700031D0 names when a cell prim hit: D_700030B0 itself, whose
 * +0x1A is `cell_class` (0x700030CA) and +0x24 `cell_normal` (0x700030D4). */
extern const unsigned char em_coll_move_cell_record_tag;
#define EM_COLL_MOVE_CELL_RECORD ((const void *)&em_coll_move_cell_record_tag)

typedef struct EmCollMoveScratch {
    float start[4];          /* 0x70003190..0x7000319C */
    float end[4];            /* 0x700031A0..0x700031AC */
    float point[4];          /* 0x700031B0..0x700031BC (lane 3 is never written here) */
    float delta[4];          /* 0x700031C0..0x700031CC (lanes 0..2 written) */
    /* 0x700031D0: NULL, EM_COLL_MOVE_CELL_RECORD, or a record a worker
     * stored (a grid node); for a worker record the worker also fills
     * record_node (+0x1A), record_normal (+0x24) and record_axis (+0x34). */
    const void *record;
    uint16_t record_node;
    float record_normal[3];
    float record_axis[3];
    const EmActor *entity;   /* 0x700031D4 */
    int32_t mode;            /* 0x700031D8 */
    uint16_t cell_class;     /* 0x700030CA: D_700030B0 +0x1A */
    float cell_normal[3];    /* 0x700030D4..0x700030DC: D_700030B0 +0x24 */
    int16_t query_class;     /* 0x7000324E */
    const void *self;        /* 0x70003254 */
    int16_t kind;            /* 0x70003B88 */
    float work[4];           /* 0x70003680..0x7000368C (001A4D10; 001A4030 writes +0) */
} EmCollMoveScratch;

/* The query actor's bytes the walkers read and write. */
typedef struct EmCollMoveActor {
    uint8_t status;          /* +0x00: bit 0 enables the mask-bit-0 lock */
    uint8_t cls;             /* +0x02: & 0x1F -> 0x7000324E; 0 selects 001A6440 */
    uint16_t h52;            /* +0x52: lock vetoes (bit 1, bit 0) */
    const void *self;        /* +0x14 -> 0x70003254 (pass 2 skips this owner) */
    float position[3];       /* +0xB0..+0xB8: 0019AD00 reads x and z; bit 31
                              * adds 0x700031C0 / 0x700031C8 to x and z */
} EmCollMoveActor;

/* Workers: >= 0 on success (the original's return value in *result), < 0
 * faults the query. */
typedef struct EmCollMoveWorkers {
    void *context;
    /* 001A6440(arg): the hull lock of a class-0 query actor (arg 0x40). It
     * may write the scratch (0x700031B0 the locked point, 0x700031D4 the
     * entity whose +0x52 bit 1 vetoes the lock). */
    int (*lock_6440)(void *context, EmCollMoveScratch *s, int arg, int *result);
    /* 001A7280(): the lock of a query actor of nonzero class. */
    int (*lock_7280)(void *context, EmCollMoveScratch *s, int *result);
    /* 0019CB60(): the grid pass of mask bit 2 over the scratch segment; its
     * return is 0 on a hit. It sets record / record_node / record_normal /
     * record_axis for the node it names. */
    int (*grid)(void *context, EmCollMoveScratch *s, int *result);
    /* 0011E748: the SDK sqrt 001A4830 calls (arguments are >= 0). */
    int (*sqrt)(void *context, float x, float *result);
} EmCollMoveWorkers;

typedef struct EmCollMoveWorld {
    const EmActorCollisionWorld *cells;  /* directory, class lists, static kinds */
    EmCollMoveWorkers workers;
} EmCollMoveWorld;

/* 0019FE50. Returns 1 when nothing was hit, 0 on a hit, -1 on a fault. */
int em_coll_move_walk_0019FE50(const EmCollMoveWorld *world, EmCollMoveScratch *s);

/* 0019AD00(actor, target, flags). Returns the mode (0, 1, 2 or 4; also
 * s->mode), or -1 on a fault (nothing written when a needed worker is
 * missing). */
int em_coll_move_0019AD00(const EmCollMoveWorld *world, EmCollMoveScratch *s,
                          EmCollMoveActor *actor, const float target[3], uint32_t flags);
/* 0019AFE0(actor, from, to, flags). */
int em_coll_move_sweep_0019AFE0(const EmCollMoveWorld *world, EmCollMoveScratch *s,
                                EmCollMoveActor *actor, const float from[3], const float to[3],
                                uint32_t flags);

/* The prim tests over the scratch segment, for the reference test: 1 hit,
 * 0 miss, -1 fault (a missing sqrt worker, a refused VU form). `p` is the
 * prim header in the original byte layout. */
int em_coll_move_prim_001A4830(const EmCollMoveWorld *world, EmCollMoveScratch *s, const uint8_t *p);
int em_coll_move_prim_001A4D10(EmCollMoveScratch *s, const uint8_t *p);
int em_coll_move_prim_001A4030(EmCollMoveScratch *s, const uint8_t *p);

/* ---- Worker adapters (docs/COLL_MOVE.md "Binding") --------------------- */

/* The player's view: its live record supplies +0x00, +0x02 and +0x52; `self`
 * is what 0x70003254 receives (the player is no class-4 owner, so any
 * non-owner value is equivalent; bind the live actor pointer). */
typedef struct EmCollMovePlayer {
    const EmCollMoveWorld *world;
    EmCollMoveScratch *scratch;
    const EmPlayerLiveActor *live;
    const void *self;
} EmCollMovePlayer;

/* EmPlayerProbeWorkers.move / .sweep (00176C80, 001764E0, 001756E0 and the
 * floor module's other callers): flags without bit 31. The hit record is
 * filled from the scratch the call left (EmPlayerProbeHit fields). A record
 * whose surface byte is 0x35 faults (its +0x34 axis is not carried for a
 * cell record). */
int em_coll_move_player_move(void *player, const float position[3], const float target[3],
                             unsigned mask, EmPlayerProbeHit *hit);
int em_coll_move_player_sweep(void *player, const float from[3], const float to[3], unsigned mask,
                              EmPlayerProbeHit *hit);
/* EmPlayerClimbWorkers.move / .sweep (0015DF10 and its sweeps). */
int em_coll_move_climb_move(void *player, const float position[3], const float target[4],
                            unsigned mask, EmPlayerClimbHit *hit);
int em_coll_move_climb_sweep(void *player, const float from[4], const float to[4], unsigned mask,
                             EmPlayerClimbHit *hit);
/* EmPlayerSlideWorkers.move (0016C570: 0019AD00(p, target, 0x80000006));
 * bit 31 moves position x/z. Returns the mode or -1. */
int em_coll_move_slide_move(void *player, float position[3], const float target[4], unsigned mask);
/* EmPlayerSlideWorkers.sweep. */
int em_coll_move_slide_sweep(void *player, const float from[4], const float to[4], unsigned mask,
                             EmPlayerProbeHit *hit);

/* An owner's view (EmDrumOriginalHooks.sweep = 0019AD00(self, point, mode)). */
typedef struct EmCollMoveOwner {
    const EmCollMoveWorld *world;
    EmCollMoveScratch *scratch;
    const EmActor *actor;     /* +0x00, +0x02, +0x52, +0x14 */
    float *position;          /* the owner's +0xB0 lanes 0..2 */
} EmCollMoveOwner;
int em_coll_move_owner_move(void *owner, const float point[3], uint32_t mode);

#ifdef __cplusplus
}
#endif

#endif /* EM_COLL_MOVE_ORIGINAL_H */
