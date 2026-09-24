/* em_coll_grid_hull.h - the move walkers' grid pass and the three hull locks.
 *
 * Lane "coll-grid-hull" (docs/COLL_GRID_HULL.md). Hand translations of the
 * original routines below, read from the .s (all four are NEARMISS files in
 * the decomp; the doc lists where the readable C is wrong):
 *
 *   0019CB60      the grid pass of 0019AD00 / 0019AFE0 (mask bit 2): the
 *                 x/z rank directions of the segment, the narrowest span of
 *                 one rank table over directions 0, 1, 4 and 5, each node's
 *                 four x/z rank bounds, the static kind gate on the node
 *                 attribute, then 0019ED80; every accepted crossing clamps
 *                 the end's x and z. Returns 0 on a hit, 1 otherwise.
 *   001A6440(arg) the hull lock of a class-0 query (0019AD00 / 0019AFE0 with
 *                 mask bit 0, arg 0x40; 0019A570 with mask bit 0, arg
 *                 id & 0xFFFF): the segment against the +0x58 geometry
 *                 chain of every published class-2 entity (D_00275B8C /
 *                 D_00275B94) whose +0x00 has bit 0
 *   001A6AD0(arg) the camera's lock (0019A910 mask bit 0, arg 0x40): the
 *                 same walk, skipping entities of class 0 and 2, with the
 *                 facing test always on and a surface class derived from
 *                 the last hit normal
 *   001A7280(arg) the lock of a nonzero-class query (0019AD00 / 0019AFE0,
 *                 arg 0x40): the same test against the player record
 *                 D_008102B0's chain only
 *
 * Reused translations (not re-translated here): 0019F1A0 and 0019ED80 from
 * em_coll_probe_original.c. The SDK vector leaves the locks call (001026A0,
 * 001028B8, 001028D0, 00102738, 00103230, copy_qw4) are single VU0 macro
 * sequences, written inline as their instructions.
 *
 * A chain is the original byte layout: a count word, then records of
 * { mask bytes x/y/z, bone slot, u16 stride, s16 vertex count n, normal
 * xyz at +8, then n vertices from +0x18 and n edge normals after them }.
 * The record's bone slot m names the matrix at *(entity +0x110 + 4m) +
 * 0x90. The native world supplies both through EmCollHullWorld.chain.
 *
 * Fail-stop: a missing grid or rank table entry, the grid pass's
 * uninitialized span registers, a class-2 list entry that names no actor,
 * an entity whose chain the world cannot supply, a read outside a chain or
 * a misaligned one, a bone slot outside the supplied matrices and a refused
 * VU form are faults: -1 (a partly updated scratch may remain; the callers
 * work on copies).
 *
 * Arithmetic: every EE COP1 and VU0 macro instruction goes through
 * em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Verified by tools/test_coll_grid_hull_reference.py (the original
 * instructions over captured AREA11 RAM, route beats and synthetic
 * chains) and, end to end inside their callers, by
 * tools/test_coll_move_reference.py and
 * tools/test_coll_segment_walkers_reference.py. */
#ifndef EM_COLL_GRID_HULL_H
#define EM_COLL_GRID_HULL_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_actor_collision.h"
#include "game/em_coll_probe_original.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 0019CB60 -------------------------------------------------------------- */

/* 0019CB60 over the scratchpad state (start, end, point, record, ranks,
 * span words, query class): 0 on a hit (0x700031A0 x/z clamped,
 * 0x700031B0 the last accepted crossing, 0x700031D0 its node), 1, or -1. */
int em_coll_grid_hull_0019CB60(const EmCollProbeGrid *grid, EmCollProbeState *s);

/* The identity a caller stores for grid node `node` in a record slot
 * (EmCollMoveScratch.record), and back: the index, or -1 when `record`
 * names no node of `grid`. */
const void *em_coll_grid_hull_node_record(const EmCollProbeGrid *grid, int node);
int em_coll_grid_hull_node_index(const EmCollProbeGrid *grid, const void *record);

/* ---- The hull locks ---------------------------------------------------------- */

/* The chain of one entity: `bytes` is what +0x58 points at (count word
 * first), `size` how many bytes of it are readable; slot m's matrix is the
 * 16 floats `slots + 16 m` (the 0x40 bytes at *(+0x110 + 4m) + 0x90, row
 * by row as copy_qw4 loads them). */
typedef struct {
    const uint8_t *bytes;
    size_t size;
    const float *slots;
    unsigned slot_count;
} EmCollHullChain;

typedef struct {
    void *context;
    /* The chain of `body` (a class-2 list entry or `player`), valid until
     * the lock returns. 0, or -1 when the world cannot supply it (the lock
     * faults). NULL: every entity that needs its chain faults. */
    int (*chain)(void *context, const EmActor *body, EmCollHullChain *out);
    /* D_008102B0 for 001A7280: its +0x00, +0x58 and +0x5C..+0x5E, and what
     * 0x700031D4 receives. NULL: 001A7280 faults. */
    const EmActor *player;
} EmCollHullWorld;

/* The scratchpad words the locks read and write. */
typedef struct {
    float start[3];          /* 0x70003190 (read) */
    float end[3];            /* 0x700031A0 (read) */
    float point[3];          /* 0x700031B0: the last accepted crossing */
    uint16_t cell_class;     /* 0x700030CA: 0 at entry; 001A6AD0 classifies */
    uint32_t word_1c;        /* 0x700030CC: 001A6440 / 001A6AD0 */
    uint32_t word_20;        /* 0x700030D0: 001A6440 / 001A6AD0 */
    float cell_normal[3];    /* 0x700030D4: the accepted record's normal */
    int record_cell;         /* set to 1: 0x700031D0 = D_700030B0 at entry */
    const EmActor *entity;   /* 0x700031D4: the entity of the last hit */
} EmCollHullScratch;

/* The original's return value (1 when a record was accepted, else 0), or
 * -1. `lists` supplies the published class-2 list (EM_ACTOR_LIST_CLASS2). */
int em_coll_grid_hull_001A6440(const EmActorClassLists *lists, const EmCollHullWorld *hulls,
                               EmCollHullScratch *s, uint32_t arg);
int em_coll_grid_hull_001A6AD0(const EmActorClassLists *lists, const EmCollHullWorld *hulls,
                               EmCollHullScratch *s, uint32_t arg);
int em_coll_grid_hull_001A7280(const EmCollHullWorld *hulls, EmCollHullScratch *s, uint32_t arg);

#ifdef __cplusplus
}
#endif

#endif /* EM_COLL_GRID_HULL_H */
