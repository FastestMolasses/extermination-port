/* em_coll_list_passes_walkers.h - the four collision walkers of census lane
 * L08 and the two queries that call them (docs/COLL_LIST_PASSES.md).
 *
 * Hand translations of the original routines below. The .s is the
 * authority wherever the decomp C is not byte-matched (0019E930 and
 * 001A3980 are NEARMISS files, 0019F330 an asm-word file; the readable C
 * of 0019E930 reads the rank-y sentinel as a word and walks the rank table
 * two entries at a time, both wrong against the .s):
 *
 *   0019B7D0(from, to)          the camera's grid ground query (byte-matched
 *                               C): stages the segment and calls
 *   0019E280                    the grid walk over attr 0x78 nodes only
 *                               (byte-matched C): all six rank directions,
 *                               the narrowest rank-table span, each node's
 *                               six rank bounds, then 0019ED80
 *   0019BA80(actor, point, box, mask)
 *                               the ladder attribute probe (00176F90):
 *                               the vertical segment (point.y - box.y -+
 *                               0.001) -> point through
 *   001A3980                    the attribute cell walk (mask bit 1): pass 1
 *                               the static cells whose kind byte is
 *                               0x1E..0x59, pass 2 every published class-4
 *                               owner (no kind gate), the prim tests
 *                               001A4030 / 001A4650 / 001A44B0
 *   0019E930                    the attribute grid walk (mask bit 2): nodes
 *                               whose attr is 0x1E..0x59, ranks from the
 *                               y-ordered pair of 0019F1A0 calls
 *   0019F330(a, b, q, node)     the column table's plane crossing (0019BC40
 *                               pass 2): the line a -> b through the node's
 *                               plane, the ring edge test, q[0..2] the
 *                               crossing, q[3] the signed slope complement,
 *                               0x70003680 / 0x70003684 its scratch
 *
 * Reused translations (not re-translated here): 0019F1A0, 0019ED80,
 * 001A4030, 001A4650 and 001A44B0 from em_coll_probe_original.c; the SDK
 * 0011E748 (sqrtf), 0011DBB8 (atanf) and 0011DF78 (fabsf) from
 * em_sdk_math_original.c.
 *
 * State. The walkers keep their state in the scratchpad; it is the same
 * EmCollProbeState the floor probes and the segment queries use (one per
 * caller world). 0019F330 also writes 0x70003684, which is
 * EmCollSegmentFaceScratch.cross[0] (em_coll_segment_walkers.h) and is
 * passed here as a bare float.
 *
 * Fail-stop. A missing grid, rank section, cell world or SDK math context,
 * an index outside a rank table, a node outside the grid, a hull or prim
 * outside the directory image, a static cell without its D_0024D7C0 kind
 * view, a pass-2 directory word with bit 31, and the grid walkers'
 * uninitialized span registers are faults: -1. The two queries (0019B7D0,
 * 0019BA80) work on a copy and leave the caller's state exactly as it was
 * on a fault; a walker called directly may leave a partly updated state.
 *
 * Arithmetic: every EE COP1 and VU0 macro instruction goes through
 * em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Verified by tools/test_coll_list_passes_reference.py (the original
 * instructions over captured AREA11 RAM and the route beats). */
#ifndef EM_COLL_LIST_PASSES_WALKERS_H
#define EM_COLL_LIST_PASSES_WALKERS_H

#include <stdint.h>

#include "game/em_coll_probe_original.h"
#include "game/em_sdk_math_original.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The walkers (over the state as their callers stage it) ------------ */

/* 0019E280: 1 on a hit (0x700031B0 = the last accepted crossing,
 * 0x700031D0 = its node), 0, or -1. */
int em_coll_list_passes_0019E280(const EmCollProbeGrid *grid, EmCollProbeState *state);
/* 0019E930: 0 on a hit (0x700031A4 = the crossing y, 0x700031B0, 0x700031D0),
 * 1, or -1. */
int em_coll_list_passes_0019E930(const EmCollProbeGrid *grid, EmCollProbeState *state);
/* 001A3980: 0 on a hit (0x700031A4, 0x700031D4, 0x700030CA's low byte), 1,
 * or -1. Reads world->cells (directory, class-4 lists, static kinds). */
int em_coll_list_passes_001A3980(const EmCollProbeWorld *world, EmCollProbeState *state);

/* ---- The queries ---------------------------------------------------------- */

/* 0019B7D0(from, to): 0 or 4 (also state->kind), or -1. Only x/y/z of
 * from and to are read. */
int em_coll_list_passes_0019B7D0(const EmCollProbeGrid *grid, EmCollProbeState *state,
                                 const float from[3], const float to[3]);
/* 0019BA80(actor, point, box, mask): `self` is the actor's +0x14 word and
 * `cls` its +0x02 byte; only box[1] is read. 0, 2 or 4, or -1. */
int em_coll_list_passes_0019BA80(const EmCollProbeWorld *world, EmCollProbeState *state,
                                 const void *self, uint8_t cls, const float point[3],
                                 const float box[3], unsigned mask);

/* 0019F330(a, b, q, node): 1 when the line crosses the node inside its
 * ring (q[0..2] the crossing, q[3] the slope term, state->ratio =
 * 0x70003680, *s3684 = 0x70003684), 0 when an edge test rejects it (q and
 * the scratch untouched), -1 on a fault. `node` is the grid node index
 * (the original's node address is D_70003208 + 64 * node). */
int em_coll_list_passes_0019F330(const EmCollProbeGrid *grid, const EmSdkMathContext *math,
                                 EmCollProbeState *state, float *s3684, const float a[3],
                                 const float b[3], int node, float q[4]);

/* ---- Binding adapter ------------------------------------------------------ */

/* EmCameraFollowWorkers.ground (0019B7D0 for 0018D330 / the camera
 * specials): `context` is an EmCollListPassesGround; from/to are the raw
 * words at the original's a0/a1 (x, y, z read). 0 with *result = the
 * original's v0, or -1. */
typedef struct {
    const EmCollProbeGrid *grid;
    EmCollProbeState *state;   /* the one scratchpad state of the caller's world */
} EmCollListPassesGround;
int em_coll_list_passes_camera_ground(void *context, const uint32_t from[4], const uint32_t to[4],
                                      int *result);

#ifdef __cplusplus
}
#endif

#endif /* EM_COLL_LIST_PASSES_WALKERS_H */
