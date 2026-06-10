/* em_collision.h — the engine's collision-world query API, native.
 *
 * Faithful translation of the level_world query cluster (decomp repo
 * Extermination/docs/FINDINGS.md "COLLISION WORLD"; PS2 functions
 * 0x19A570..0x1A7xxx). The PS2 engine stages a query segment in
 * scratchpad (0x70003190 start / 0x700031A0 end) and dispatches over a
 * set mask, clamping the segment end to each nearer hit so the final
 * result is the closest hit across sets:
 *
 *   func_0019A570(from, to, set_mask, id)  -> em_collision_segment_query
 *   func_0019AD00(actor, target, mask)     -> em_collision_move_probe
 *
 * Collision sets (mask bits, the engine's):
 *   bit 0 (1) movable-object hulls (func_001A6440)  — no native objects
 *             yet; accepted in the mask, currently never hits.
 *   bit 1 (2) static cell/n-gon world (func_001A0B10 -> func_001A4030)
 *   bit 2 (4) "grid" world (func_0019D330 -> func_0019ED80) — decoded as
 *             a polygon soup with a rank-table acceleration index, NOT a
 *             quantized heightfield; it owns the walkable floor.
 *   bit 31    move-probe only: apply the collide-and-slide correction
 *             (hit-minus-target delta) to the actor x/z.
 *
 * Both worlds share one polygon shape — plane + convex vertex ring +
 * outward edge normals — so the native EMCL container bakes them into a
 * single poly list tagged with its engine set bit (see
 * tools/export_collision.py in the decomp repo for the disc-side layout).
 *
 * Zero dependencies beyond libc; loads the git-ignored, user-generated
 * EMCL files under assets/scene/.
 */
#ifndef EM_COLLISION_H
#define EM_COLLISION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Engine collision-set mask bits (see header comment). */
#define EM_COLL_SET_HULLS 0x1u
#define EM_COLL_SET_CELLS 0x2u
#define EM_COLL_SET_GRID  0x4u
#define EM_COLL_SLIDE     0x80000000u /* move-probe: apply x/z correction */

/* Query id (the engine's SPR 0x7000324E): the actor world-slot for move
 * probes (player = 0), -1 for plain segment queries. Conditional surfaces
 * (attr 0x50..0x59, func_0019D330) are gated against it:
 * 0x50 never collides, 0x51 only id 0, 0x52 only id 2, 0x53 skipped for
 * id -1, 0x54..0x59 always collide. */
#define EM_COLL_ID_NONE (-1)

/* Surface classification halfword (SPR 0x700030CA, func_001A4030: the
 * ratio ny^2/(nx^2+nz^2) against 0.4903 and 3.0). */
#define EM_SURF_WALL    0x2000
#define EM_SURF_SLOPE   0x1000  /* walkable slope (normal tilted up)   */
#define EM_SURF_FLOOR   0x4000
#define EM_SURF_STEEPDN 0x0800  /* overhanging slope (normal tilted down) */
#define EM_SURF_CEIL    0x8000

typedef struct {
    float plane[4];     /* unit normal xyz + plane d (node +0x24/+0x30) */
    uint32_t first;     /* base into the index / edge-normal pools */
    uint8_t  vcount;    /* node +0x18 / prim header byte +2 */
    uint8_t  set;       /* EM_COLL_SET_CELLS or EM_COLL_SET_GRID */
    uint8_t  attr;      /* surface attr byte (node +0x1A; 0 for cells) */
    uint8_t  pad;
} EmCollPoly;

typedef struct {
    uint32_t   vert_count;
    uint32_t   poly_count;
    uint32_t   index_count;
    uint32_t   flags;        /* bit0 = grid world decoded (floor owner) */
    float      bbox[6];      /* world min xyz / max xyz */
    float     *verts;        /* vert_count * 3 */
    EmCollPoly *polys;
    uint16_t  *indices;      /* index_count */
    float     *edge_n;       /* index_count * 3, outward edge normals */
    void      *blob;         /* single backing allocation */
} EmCollision;

/* Query result — the native mirror of the scratchpad result block. */
typedef struct {
    float point[3];      /* hit point             (SPR 0x700031B0) */
    float normal[3];     /* hit plane normal      (SPR 0x700030D4) */
    float delta[3];      /* hit - target          (SPR 0x700031C0) */
    int   kind;          /* set bit of the hit    (SPR 0x700031D8) */
    int   poly;          /* hit poly index        (SPR 0x700031D0) */
    uint16_t surf_class; /* EM_SURF_*             (SPR 0x700030CA) */
    uint8_t  attr;       /* surface attr byte     (SPR 0x70003B88) */
} EmCollHit;

/* Load / free an EMCL collision world. Returns 0 on success. */
int  em_collision_load(EmCollision *c, const char *path);
void em_collision_free(EmCollision *c);

/* func_0019A570 — segment query. Tests the sets in `mask` (low bits) and
 * returns the set bit of the nearest hit, or 0 for no hit. `id` gates the
 * conditional surfaces (EM_COLL_ID_NONE for plain queries). `hit` is
 * optional and only written on a hit. */
int em_collision_segment_query(const EmCollision *c, const float from[3],
                               const float to[3], unsigned mask, int id,
                               EmCollHit *hit);

/* func_0019AD00 — actor move probe with collide-and-slide response.
 * Probes horizontally from (pos.x, target.y, pos.z) to `target` extended
 * 0.01 past it (the engine's f12 = 0x3C23D70A pad). On a hit with
 * EM_COLL_SLIDE set, pos x/z receive target + (hit - target) — the
 * engine adds the delta to the actor's velocity-integrated position
 * (actor +0xB0/+0xB8), net effect: the actor lands on the hit point.
 * Without a hit (and with EM_COLL_SLIDE), pos x/z = target. pos.y is
 * never touched (the engine resolves height with separate vertical
 * queries). Returns the hit-set bit or 0. The query id is 0 (the player
 * actor's world slot, actor +0x02 & 0x1F). */
int em_collision_move_probe(const EmCollision *c, float pos[3],
                            const float target[3], unsigned mask,
                            EmCollHit *hit);

#ifdef __cplusplus
}
#endif

#endif /* EM_COLLISION_H */
