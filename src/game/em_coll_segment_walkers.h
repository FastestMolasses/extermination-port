/* em_coll_segment_walkers.h - the segment and camera queries and their walkers.
 *
 * Lane "coll-segment-walkers" (docs/COLL_SEGMENT_WALKERS.md). Hand
 * translations of the original routines below, read from the .s (every one
 * is a NEARMISS file in the decomp, so its readable C is not trusted; the
 * doc lists where the C is wrong):
 *
 *   0019A570(from, to, mask, id)   the segment query: 001A6440 (mask bit 0,
 *                                  a worker), 001A0B10 (bit 1), 0019D330
 *                                  (bit 2). The climb, drum, recovery,
 *                                  shadow (0015BF90) and pickup LOS callers
 *                                  use it with masks 4 and 6.
 *   0019A910(from, to, mask)       the camera query: 001A6AD0 (bit 0, a
 *                                  worker), 001A1390 (bit 1), 0019D770
 *                                  (bit 2). The camera (0018D330, 0018DD20,
 *                                  00197490, 00198240) uses masks 6 and 7.
 *   001A0B10 / 001A1390            the two cell walkers: pass 1 the static
 *                                  cells of *0x70003250, pass 2 the published
 *                                  class-4 owners (D_00275B7C). They test
 *                                  0x1000 prims with 001A4030, 0x2000 prims
 *                                  with 001A50A0 and 0x4000 prims with
 *                                  001A5C30; 0x8000 prims are skipped.
 *   0019D330 / 0019D770            the two grid walkers: a span of one rank
 *                                  table (0019F1A0 over all six directions),
 *                                  each node's six rank bounds, an attribute
 *                                  gate, then 0019ED80.
 *   001A50A0(prim)                 the segment against one face of a 0x2000
 *                                  box prim (face code prim +2)
 *   001A5C30(prim)                 the segment against a 0x4000 round prim
 *                                  (a vertical cylinder)
 *
 * 001A50A0 and 001A5C30 are also the two pass-2 workers of
 * em_coll_probe_original's surface walker 001A2AE0 (EmCollProbeWorkers):
 * em_coll_segment_face_worker / em_coll_segment_round_worker.
 *
 * Reused translations (not re-translated here): 001A4030, 0019F1A0 and
 * 0019ED80 from em_coll_probe_original.c; 0011DF78 (fabsf) and 0011E748
 * (sqrtf) from em_sdk_math_original.c.
 *
 * State. The routines keep their state in the scratchpad. It is the same
 * EmCollProbeState the floor probes use (one per caller world, zeroed at
 * area load), plus EmCollSegmentFaceScratch for the words only 001A50A0
 * writes. Pass the SAME state to every probe and query, as the original has
 * one scratchpad.
 *
 * Fail-stop. A missing worker (001A6440 / 001A6AD0 when mask bit 0 is set),
 * a static cell without its D_0024D7C0 kind view, a pass-2 directory word
 * with bit 31, a prim or hull outside the directory image, an index outside
 * a grid table, the grid walkers' uninitialized span registers, and a
 * failing 0011E748 are faults: -1. The two queries then leave the caller's
 * state and scratch exactly as they were (they work on copies). A walker or
 * prim test called directly may leave a partly updated state.
 *
 * Arithmetic: every EE COP1 and VU0 macro instruction goes through
 * em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Verified by tools/test_coll_segment_walkers_reference.py (the original
 * instructions over captured AREA11 RAM and the route beats). */
#ifndef EM_COLL_SEGMENT_WALKERS_H
#define EM_COLL_SEGMENT_WALKERS_H

#include <stdint.h>

#include "game/em_coll_probe_original.h"
#include "game/em_sdk_math_original.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The scratchpad words 001A50A0 writes besides EmCollProbeState (which
 * holds 0x70003680, its t). Written lane by lane, x/y/z only. */
typedef struct {
    float box_min[3];   /* 0x70003600..0x70003608 */
    float box_max[3];   /* 0x70003610..0x70003618 */
    float delta[3];     /* 0x70003620..0x70003628: end - start */
    float rel[3];       /* 0x70003630..0x70003638: box origin - start */
    float cross[2];     /* 0x70003684, 0x70003688: the two in-plane coordinates */
} EmCollSegmentFaceScratch;

/* The hull locks of mask bit 0. Each returns >= 0 with the original's v0 in
 * *result (nonzero = the query copies 0x700031B0 to 0x700031A0), or < 0 for
 * a fault. It may read and write the state as its original does. */
typedef struct {
    void *context;
    /* 001A6440(id & 0xFFFF), called by 0019A570 before it writes 0x7000324E. */
    int (*lock_6440)(void *context, EmCollProbeState *state, int id, int *result);
    /* 001A6AD0(0x40), called by 0019A910. */
    int (*lock_6AD0)(void *context, EmCollProbeState *state, int arg, int *result);
} EmCollSegmentWorkers;

/* One caller world. `math` supplies 0011E748 (its tables, its world word
 * D_0026C5D0 and its error-path workers) for 001A5C30. */
typedef struct {
    const EmCollProbeWorld *world;        /* cells (directory, class-4 list, static kinds) and grid */
    const EmSdkMathContext *math;
    const EmCollSegmentWorkers *workers;  /* may be NULL: mask bit 0 faults */
    EmCollProbeState *state;              /* the one scratchpad state */
    EmCollSegmentFaceScratch *face;       /* 0x70003600.. */
} EmCollSegment;

/* ---- The queries ---------------------------------------------------------
 * The original's return value (0, 1, 2 or 4; also state->kind), or -1. */

/* 0019A570(from, to, mask, id). Only x/y/z of from and to are read. */
int em_coll_segment_0019A570(const EmCollSegment *seg, const float from[3], const float to[3],
                             unsigned mask, int id);
/* 0019A910(from, to, mask). */
int em_coll_segment_0019A910(const EmCollSegment *seg, const float from[3], const float to[3],
                             unsigned mask);

/* ---- The walkers and prim tests, over the state as the queries stage it.
 * The cell walkers and grid walkers return 1 on a hit, 0, or -1. */
int em_coll_segment_001A0B10(const EmCollProbeWorld *world, const EmSdkMathContext *math,
                             EmCollProbeState *state, EmCollSegmentFaceScratch *face);
int em_coll_segment_001A1390(const EmCollProbeWorld *world, const EmSdkMathContext *math,
                             EmCollProbeState *state, EmCollSegmentFaceScratch *face);
int em_coll_segment_0019D330(const EmCollProbeGrid *grid, EmCollProbeState *state);
int em_coll_segment_0019D770(const EmCollProbeGrid *grid, EmCollProbeState *state);
/* 1 hit, 0 miss, -1 fault. `prim` is the prim header in the original
 * byte layout (0x1C bytes for 001A50A0, at least 0x18 for 001A5C30). */
int em_coll_segment_001A50A0(const uint8_t *prim, EmCollProbeState *state,
                             EmCollSegmentFaceScratch *face);
int em_coll_segment_001A5C30(const EmSdkMathContext *math, const uint8_t *prim,
                             EmCollProbeState *state);

/* ---- Binding adapters ------------------------------------------------------ */

/* EmCollProbeWorkers.face_segment / .round_segment (001A2AE0 pass 2);
 * `context` is an EmCollSegment (its math and face scratch are used; the
 * state is the one the probe passes). */
int em_coll_segment_face_worker(void *context, const uint8_t *prim, EmCollProbeState *state,
                                int *hit);
int em_coll_segment_round_worker(void *context, const uint8_t *prim, EmCollProbeState *state,
                                 int *hit);

/* 0019A570 in the callers' worker shapes; `context` is an EmCollSegment.
 *   EmPlayerClimbWorkers.segment:   returns the result or -1;
 *   EmDrumOriginalHooks.segment:    the same, int32_t arguments (bind through
 *                                   the drum's own context);
 *   EmPlayerRecoveryWorkers.segment: 0 with *result, or -1. */
int em_coll_segment_query(void *context, const float from[4], const float to[4], unsigned mask, int id);
int em_coll_segment_query_i32(void *context, const float from[3], const float to[3], int32_t mask,
                              int32_t id);
int em_coll_segment_query_result(void *context, const float from[4], const float to[4], unsigned mask,
                                 int id, int *result);

/* What a caller reads after a query hit: 0x700031D8, 0x700031B0, the record
 * *0x700031D0's +0x1A halfword and +0x24 normal, and 0x700031D4 (with its
 * +2/+3 bytes). Returns 0, or -1 when the state names no record (the
 * original would read address 0; callers read these only after a hit). */
typedef struct {
    int kind;
    float point[3];
    uint16_t record_node;
    float record_normal[3];
    const EmActor *entity;
    uint8_t entity_flags, entity_type;
} EmCollSegmentHit;
int em_coll_segment_hit(const EmCollSegment *seg, EmCollSegmentHit *out);

#ifdef __cplusplus
}
#endif

#endif /* EM_COLL_SEGMENT_WALKERS_H */
