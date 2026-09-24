/* em_coll_probe_original.h - the floor service's surface and object probes.
 *
 * Lane "surface-object-probes" (docs/COLL_PROBES.md). Hand translation of the
 * original routines below, read from the .s (every one of them is a NEARMISS
 * or an asm-word file in the decomp except 001A4030, whose C is byte-matched):
 *
 *   0019B6C0(top, bottom)          the surface record: the segment top ->
 *                                  bottom through 001A2AE0 (cells) and then
 *                                  0019DF10 (grid); 00175900 reads +B, +23A
 *                                  and +250 from it
 *   0019B8C0(actor, at, probe, m)  the object probe: the vertical segment
 *                                  (at.y - probe.y -+ 0.001) -> at through
 *                                  001A32C0 (cells, mask 2) and 0019E640
 *                                  (grid, mask 4); 00175900 reads +23A
 *   001A2AE0 / 001A32C0            the two cell walkers: pass 1 the static
 *                                  cells of *0x70003250, pass 2 the
 *                                  published class-4 owners (D_00275B7C)
 *   0019DF10 / 0019E640            the two grid walkers: a span of one
 *                                  rank table picked through 0019F1A0,
 *                                  each node's rank bounds, then 0019ED80
 *   0019F1A0(point, mask)          the per-direction rank of a point
 *   0019ED80(segment, node)        the grid node segment test
 *   001A4030, 001A4650, 001A44B0   the cell prim tests the walkers call
 *                                  (n-gon, face, round)
 *
 * The walkers keep their state in the scratchpad; EmCollProbeState is that
 * state, word for word (addresses in the field comments). Several of its
 * words outlive a call (the ranks at 0x70003240, the cell record
 * D_700030B0, 0x70003B86/88, 0x70003680, 0x7000324E), so a caller keeps one
 * state and passes it to every probe, as the original keeps its scratchpad.
 *
 * The grid needs data the EMCL poly records do not carry: each node's class
 * byte (+0x1B, EMCL flag EM_COLL_FLAG_NODE_CLASS) and the rank section (EMCL
 * flag EM_COLL_PROBE_FLAG_RANKS: the grid vertex pool, node +0x00..+0x17 and
 * the 12 tables at 0x70003210 / 0x70003228). The decomp's
 * tools/export_collision.py writes both and verifies them byte for byte
 * against captured RAM (--verify-ram).
 *
 * Callees this module does not translate are workers: 001A50A0 (0x2000) and
 * 001A5C30 (0x4000), which 001A2AE0's pass 2 calls for an owner whose +0x54
 * kind is 0x5A or more (no AREA11 owner has one). A missing worker, and
 * every state the original reads that the native world cannot give (a
 * static cell without its D_0024D7C0 kind view, an out-of-range table
 * entry, uninitialized registers), is a fault: -1, and the caller's state is
 * left exactly as it was.
 *
 * Arithmetic: every EE COP1 and VU0 macro instruction goes through
 * em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Verified by tools/test_coll_probe_reference.py (the original instructions
 * over captured AREA11 RAM and the route beats).
 */
#ifndef EM_COLL_PROBE_ORIGINAL_H
#define EM_COLL_PROBE_ORIGINAL_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_actor_collision.h"
#include "game/em_collision.h"
#include "game/em_player_floor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* EMCL header flag: the grid rank section follows the edge normals
 * (tools/export_collision.py, "Grid rank section"). */
#define EM_COLL_PROBE_FLAG_RANKS 0x4u

/* ---- The grid rank view --------------------------------------------------- */

typedef struct EmCollProbeGrid {
    const EmCollision *emcl;   /* the loaded EMCL (polys, ring, planes, classes) */
    uint32_t count;            /* N: 0x7000320C */
    uint32_t first;            /* grid node i is EMCL poly first + i */
    uint32_t vert_count;
    const float *verts;        /* the grid vertex pool, *0x700031FC */
    const int16_t *words;      /* node i +0x00..+0x17: words[12 * i + k] */
    const int16_t *tables;     /* table k (0..11) at tables + k * N: 0..5 are
                                * *(0x70003210 + 4k), 6..11 *(0x70003228 + 4(k-6)) */
    void *blob;                /* owned copy of the section */
} EmCollProbeGrid;

/* Read the rank section of the EMCL image `emcl` (the whole file) for the
 * already loaded `grid`. Every table entry must name a node, every boundary
 * vertex index a grid vertex, and every node an EMCL grid poly whose class
 * byte is present (flags EM_COLL_FLAG_NODE_CLASS | EM_COLL_PROBE_FLAG_RANKS).
 * Returns 0, or -1 (nothing kept). */
int em_coll_probe_grid_init(EmCollProbeGrid *out, const EmCollision *grid, const void *emcl,
                            size_t size);
/* The same, reading the file. */
int em_coll_probe_grid_load(EmCollProbeGrid *out, const EmCollision *grid, const char *path);
void em_coll_probe_grid_free(EmCollProbeGrid *grid);

/* ---- The scratchpad state ------------------------------------------------- */

enum {
    EM_COLL_PROBE_RECORD_NONE = 0,  /* 0x700031D0 = 0 */
    EM_COLL_PROBE_RECORD_CELL = 1,  /* 0x700031D0 = D_700030B0 (the prim tests' record) */
    EM_COLL_PROBE_RECORD_GRID = 2   /* 0x700031D0 = grid node `node` */
};

typedef struct {
    float start[4];          /* 0x70003190..0x7000319C */
    float end[4];            /* 0x700031A0..0x700031AC */
    float point[3];          /* 0x700031B0 */
    float delta[3];          /* 0x700031C0 */
    int record;              /* 0x700031D0, EM_COLL_PROBE_RECORD_* */
    int node;                /* the grid node 0x700031D0 names (RECORD_GRID), else -1 */
    const EmActor *entity;   /* 0x700031D4 */
    int kind;                /* 0x700031D8 */
    uint16_t cell_class;     /* 0x700030CA: D_700030B0 +0x1A (class | kind byte) */
    float cell_normal[3];    /* 0x700030D4..0x700030DC: D_700030B0 +0x24 */
    float ratio;             /* 0x70003680: 001A4030's ny^2 / (nx^2 + nz^2) */
    int16_t query_class;     /* 0x7000324E */
    const void *self;        /* 0x70003254 */
    int16_t rank[6];         /* 0x70003240..0x7000324A (0019F1A0) */
    int16_t span_lo, span_hi;/* 0x70003B86 / 0x70003B88 (the walkers' span pick) */
} EmCollProbeState;

/* The record +0x1A halfword 0x700031D0 names (cell: 0x700030CA; grid: the
 * node's attr | class << 8), or 0 for RECORD_NONE. 00175900 reads its low
 * byte. */
uint16_t em_coll_probe_record_node(const EmCollProbeGrid *grid, const EmCollProbeState *s);

/* ---- World and workers ------------------------------------------------------ */

typedef struct {
    /* The cell directory (*0x70003250, count 0x7000324C), the class-4 lists
     * (D_00275B7C / D_00275B84) and the static-cell kind view (D_0024D7C0
     * [area][sub] record +8). Its `grid` field is not used here. */
    const EmActorCollisionWorld *cells;
    const EmCollProbeGrid *grid;
} EmCollProbeWorld;

typedef struct {
    void *context;
    /* 001A50A0(prim) and 001A5C30(prim): 001A2AE0 pass 2's tests of an
     * owner's 0x2000 and 0x4000 prims. Each reads and writes the segment
     * state as the original does and sets *hit to its return value. */
    int (*face_segment)(void *context, const uint8_t *prim, EmCollProbeState *state, int *hit);
    int (*round_segment)(void *context, const uint8_t *prim, EmCollProbeState *state, int *hit);
} EmCollProbeWorkers;

/* ---- The originals ----------------------------------------------------------
 * Each returns the original's return value, or -1 on a fault. Only the two
 * probes (em_coll_probe_0019B6C0 / 0019B8C0, which work on a copy) and the
 * player adapters leave the state as it was on a fault; a walker or helper
 * called directly may leave a partly updated state when it faults. */

/* 0019B6C0(top, bottom): 0 (0x700031D0 = 0), 2 (cells) or 4 (grid). */
int em_coll_probe_0019B6C0(const EmCollProbeWorld *world, const EmCollProbeWorkers *workers,
                           EmCollProbeState *state, const float top[3], const float bottom[3]);
/* 0019B8C0(actor, at, probe, mask): `self` is the actor's +0x14 and `cls`
 * its +0x02 byte. 0, 2 or 4. */
int em_coll_probe_0019B8C0(const EmCollProbeWorld *world, EmCollProbeState *state,
                           const void *self, uint8_t cls, const float at[3],
                           const float probe[3], unsigned mask);

/* The walkers and helpers, over the state as the probes stage it. */
int em_coll_probe_001A2AE0(const EmCollProbeWorld *world, const EmCollProbeWorkers *workers,
                           EmCollProbeState *state);                 /* 1 hit, 0 */
int em_coll_probe_0019DF10(const EmCollProbeWorld *world, EmCollProbeState *state); /* 1 hit, 0 */
int em_coll_probe_001A32C0(const EmCollProbeWorld *world, EmCollProbeState *state); /* 0 hit, 1 */
int em_coll_probe_0019E640(const EmCollProbeWorld *world, EmCollProbeState *state); /* 0 hit, 1 */
/* 0019F1A0(point, mask): writes state->rank[i] for each set bit i < 6. */
int em_coll_probe_0019F1A0(const EmCollProbeGrid *grid, EmCollProbeState *state,
                           const float point[3], unsigned mask);
/* 0019ED80(D_70003190, node): 1 on accept (0x700031B0 and 0x700031D0
 * written), 0. */
int em_coll_probe_0019ED80(const EmCollProbeGrid *grid, EmCollProbeState *state, int node);
/* The prim tests over the state's segment: 1 hit, 0. */
int em_coll_probe_001A4030(const uint8_t *prim, EmCollProbeState *state);
int em_coll_probe_001A4650(const uint8_t *prim, EmCollProbeState *state);
int em_coll_probe_001A44B0(const uint8_t *prim, EmCollProbeState *state);
/* 0019C830(): 0019AB20's vertical grid pass over the segment the state
 * holds (0x70003190 -> 0x700031A0): the ranks of the lower end (0019F1A0
 * mask 0x33), the span pick over directions 0, 1, 4, 5, then every node of
 * the span inside the rank bounds whose kind byte passes the query-class
 * filter (kind < 0x5A; 0x51 only for class 0, 0x52 only for class 2, 0x53
 * not for class -1) through 0019ED80, each hit lowering 0x700031A4. 0 on a
 * hit (0x700031D0 = the last node hit, 0x700031B0 its point), 1 when
 * nothing was hit, -1 on a fault. */
int em_coll_probe_0019C830(const EmCollProbeGrid *grid, EmCollProbeState *state);

/* The SDK VU0 routines the walkers use, for other translations of callers of
 * the same routines (em_actor_collision.c): 001028D0 out = a - b (vsub.xyzw),
 * 001028B8 out = a + b (vadd.xyzw), 00102738 the xyz dot (vmul.xyz, then
 * vaddy.x, vaddz.x) and 00103230 out = v * t (vmulx.xyz, w kept). 0, or -1
 * when em_ee_float.h refuses a form. */
int em_coll_probe_sdk_sub(float out[4], const float a[4], const float b[4]);
int em_coll_probe_sdk_add(float out[4], const float a[4], const float b[4]);
int em_coll_probe_sdk_dot(float *out, const float a[4], const float b[4]);
int em_coll_probe_sdk_scale(float out[4], const float v[4], float t);

/* ---- The player floor service's workers (EmPlayerFloorWorkers) -----------
 * docs/COLL_PROBES.md "Binding". `context` is an EmCollProbePlayer. */
typedef struct {
    const EmCollProbeWorld *world;
    const EmCollProbeWorkers *workers;   /* 001A50A0 / 001A5C30 (may be NULL: faults when reached) */
    EmCollProbeState *state;             /* the one scratchpad state */
    EmActorCollisionQuery query;         /* self = the player's +0x14, cls = its +0x02 byte */
} EmCollProbePlayer;

/* EmPlayerFloorWorkers.head: 0019B6C0(top, bottom). Fills kind, node (the
 * record's +0x1A halfword), point (0x700031B0), normal (record +0x24),
 * owner/entity (0x700031D4); 0019B6C0 does not write 0x700031C0, so delta
 * is left zero (00175900 does not read it after this probe). */
int em_coll_probe_player_head(void *context, const float top[3], const float bottom[3],
                              EmPlayerProbeHit *hit);
/* EmPlayerFloorWorkers.object: 0019B8C0(player, at, probe, mask). Fills the
 * same fields and delta (0x700031C0 = point - at). */
int em_coll_probe_player_object(void *context, const float at[3], const float probe[3],
                                unsigned mask, EmPlayerProbeHit *hit);

#ifdef __cplusplus
}
#endif

#endif /* EM_COLL_PROBE_ORIGINAL_H */
