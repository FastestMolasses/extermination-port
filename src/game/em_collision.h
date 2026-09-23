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
 *   bit 1 (2) cell world, including published class4 actor cells
 *             (func_001A0B10 -> func_001A4030 / compact face tests)
 *   bit 2 (4) "grid" world (func_0019D330 -> func_0019ED80) — a polygon
 *             soup with a rank-table acceleration index, NOT a quantized
 *             heightfield; it owns the walkable floor. CONFIRMED (audit)
 *             from the recovered func_0019D330: it picks buckets via
 *             func_0019F1A0 + a per-axis min-gap scan, then walks the
 *             winning bucket's 64-byte node list running a bbox-overlap
 *             reject and calling func_0019ED80 per node — a spatial
 *             prune over explicit polygons, with no height sampling
 *             anywhere. (The port brute-forces the poly list instead of
 *             reproducing the prune: same result set, no accel index.)
 *   bit 31    move-probe only: apply the collide-and-slide correction
 *             (hit-minus-target delta) to the actor x/z.
 *
 * Polygon records in both worlds share a plane, convex vertex ring and
 * outward edge normals. The native EMCL container combines those polygon
 * lists, tagged with their engine set bit. Compact actor-cell box faces
 * use separate EMCB assets and preserve their original owner lifetime (see
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
 * (attr 0x50..0x59) are gated against it:
 * 0x50 never collides, 0x51 only id 0, 0x52 only id 2, 0x53 skipped for
 * id -1, 0x54..0x59 always collide, >=0x5A never collide.
 * Movement probes admit 0x50 as well. Camera queries instead reject only
 * 0x51..0x53, independently of query ID. These are distinct original
 * walker families; see attr_passes() in em_collision.c. */
#define EM_COLL_ID_NONE (-1)

/* Surface classification halfword (SPR 0x700030CA). CONFIRMED (audit)
 * against decomp Extermination/src/func_001A4030.c: ny^2/(nx^2+nz^2)
 * tested `< 0.49029058f` then `<= 3.0f`, sign of ny picks the family.
 * All five constants below are the recovered ones verbatim. */
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
    uint8_t  pad;       /* grid node +0x1B when EM_COLL_FLAG_NODE_CLASS */
} EmCollPoly;

/* EMCL header flags. Bit 1: each grid poly's pad byte is the node's +0x1B,
 * the high byte of the +0x1A halfword. For a grid hit the original result
 * record (0x700031D0) IS the node (0019ED80 stores it), and every consumer
 * reads the class as `*(u16 *)(record + 0x1A) & 0xFF00` (00175CF0, 00176BE0,
 * 001791D0, 0015DEC0): the class is authored per node, not derived from the
 * normal. In the captured AREA11 grid (state04 RAM, 3099 nodes) that class is
 * 0x1000 on exactly 18 nodes - the slide hill - while the normal-ratio rule
 * of 001A4030 (which applies to cell n-gons only) would give 250. Only cell
 * hits stage the ratio class (001A4030 -> 0x700030CA, record 0x700030B0). */
#define EM_COLL_FLAG_GRID       0x1u
#define EM_COLL_FLAG_NODE_CLASS 0x2u

/* Original cell type0x2000: a single directed face, not a solid AABB.
 * Signed extents retain the authored face orientation. */
typedef struct {
    uint32_t face;
    float origin[3], extent[3];
} EmCollBoxFace;

typedef struct {
    uint32_t uid, attr, face_count;
    float bbox[6];
    EmCollBoxFace *faces;
} EmCollCell;

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
    const EmCollCell *actor_cells[32]; /* published class4 cell owners */
    unsigned actor_cell_count;
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

/* Optional compact cells are published by their actual scene actor. They
 * participate in set2, including mask6 queries; they are not set1 hulls. */
int em_collision_cell_load(EmCollCell *cell, const char *path);
void em_collision_cell_free(EmCollCell *cell);
int em_collision_cell_bind(EmCollision *c, const EmCollCell *cell);
void em_collision_cell_unbind(EmCollision *c, unsigned uid);
/* 001A4D10 (horizontal movement) / 001A50A0 (segment/camera). */
int em_collision_box_face(const EmCollBoxFace *face, const float start[3],
                          const float end[3], int movement, EmCollHit *hit);

/* func_0019A570 — segment query. Tests the sets in `mask` (low bits) and
 * returns the set bit of the nearest hit, or 0 for no hit. `id` gates the
 * conditional surfaces (EM_COLL_ID_NONE for plain queries). `hit` is
 * optional and only written on a hit. */
int em_collision_segment_query(const EmCollision *c, const float from[3],
                               const float to[3], unsigned mask, int id,
                               EmCollHit *hit);

/* func_0019A910 — camera segment query. Like the segment API, with the
 * camera world's filter: skip 0x51..0x53, admit 0x50 and >=0x54. */
int em_collision_camera_query(const EmCollision *c, const float from[3],
                              const float to[3], unsigned mask, EmCollHit *hit);

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

/* --- func_0019BC40: the vertical column table ----------------------------
 *
 * Every floor/ceiling crossing of the vertical line through `pos` (x, z),
 * sorted by height, with close pairs culled: the table the ledge probe
 * 0015DF10 and the fall query 00179450 read (D_70003170 flags, D_700030F0
 * heights, D_70003130 objects, D_00282250 aux, count 0x700031E0).
 *
 * Pass 1 walks the published class-4 owners (D_00275B7C, in list order) and
 * the type-0x2000 faces of each owner's cell (001A5760: only faces 3/4, x and
 * z strictly inside; top faces give flag 0x8001 and aux 1e-8, bottom faces
 * 0x8000 and -1e-8). Pass 2 walks the grid nodes with |ny| >= 0.001 and attr
 * < 0x50 through 0019F330 (plane crossing, edge test <= 1e-5, aux = +-(pi/2
 * - atan(|ny| / |n.xz|)), flag 0x4000 | (aux > 0)). The original visits the
 * grid nodes one rank-table span admits; the port visits every grid poly in
 * EMCL node order. KNOWN INEXACT: the rank tables are not a pure prune, so
 * without them this query can report extra grid entries the original never
 * visits (4 of 5,939 AREA11 columns checked, at level edges x 112-208,
 * z 434-478, off the route; docs/PLAYER_CLIMB_SLIDE.md section 3). Order at
 * exactly equal heights and past the 20-candidate cap may also differ. An
 * exact query needs the rank data the EMCL does not carry: the per-point
 * ranks 0019F1A0 writes to 0x70003240 (six halfwords, from the level's rank
 * tables) and each grid node's six rank bounds (node +0x0C..+0x17), which
 * 0019BC40 compares before 0019F330. Binders must treat this as a boundary.
 * 0019A180 then reads, per entry: for 0x8000 entries the owner's +0x54
 * byte, for grid entries the node's +0x1A halfword (attr | class << 8). */
#define EM_COLL_COLUMN_MAX 20
typedef struct {
    const EmCollCell *cell;  /* the owner's published cell (uid), or NULL */
    uint8_t alive;           /* owner +0x00 != 0 */
    uint8_t owner_class;     /* owner +0x02 & 0x1F (only 4 is walked) */
    uint8_t uid;             /* owner +0x0E >> 8 (0xFF: none) */
    uint8_t kind54;          /* owner +0x54 */
} EmCollColumnOwner;

typedef struct {
    int count;
    uint16_t flags[EM_COLL_COLUMN_MAX];
    float height[EM_COLL_COLUMN_MAX];
    float aux[EM_COLL_COLUMN_MAX];
    int owner[EM_COLL_COLUMN_MAX];         /* owner index, or -1 for a grid node */
    int poly[EM_COLL_COLUMN_MAX];          /* grid poly index, or -1 */
    int16_t object_node[EM_COLL_COLUMN_MAX]; /* grid: node +0x1A halfword */
    uint8_t object_kind[EM_COLL_COLUMN_MAX]; /* cell: owner +0x54 */
} EmCollColumn;

/* The SDK transcendental calls 0019F330 makes (0011E748 sqrt, 0011DBB8 atan)
 * as host models, as elsewhere in the port; NULL selects sqrtf/atanf. */
typedef struct {
    float (*sqrt)(void *context, float x);
    float (*atan)(void *context, float x);
    void *context;
} EmCollColumnMath;

/* 0019BC40(pos). Returns the survivor count (also out->count). */
int em_collision_column_table(const EmCollision *c, const EmCollColumnOwner *owners,
                              unsigned owner_count, const float pos[3],
                              const EmCollColumnMath *math, EmCollColumn *out);

/* --- Moving walkable surfaces (footprint-AABB + velocity carry) -------
 *
 * The "player rides a moving surface" primitive (decomp repo
 * Extermination/docs/INVESTIGATION_first_level_area11.md §11.4, the
 * truck's convergence block 0x00825014). The PS2 engine keeps the moving
 * surface in the per-frame zone/collision table D_70003250[(uid>>8)&0xFF]
 * and, in the truck's update, tests the player FOOTPRINT (player X
 * D_00810360 / Z D_00810368) against the record's [minX,maxX] (+0x0/+0xC)
 * x [minZ,maxZ] (+0x8/+0x14). If inside, it reads the player position,
 * ADDS the surface's current velocity (D_70003.._38A8) directly — NO
 * smoothing — writes it back, and sets the "player is riding" flag
 * (D_70003.._31F0 = 1). The truck's accelerating fall (vel.y −0.033 →
 * −0.667) is thereby carried into the player, which is the fail/death
 * mechanic: stay on the footprint and you ride it into the crevice.
 *
 * This is a per-FRAME registry: actors clear it at frame start and
 * re-register their footprint+velocity each frame, then the player's
 * carry resolve runs once. The registry is engine-global (the PS2's zone
 * table is a fixed scratch array), so these are file-static, not bound to
 * an EmCollision world. The TRUCK actor (a later task) is the first and,
 * for AREA-11, only registrant; with no actor registering, the primitive
 * is dormant (carry always returns 0).
 *
 * The Y test ("is the player actually standing ON this surface, not just
 * over/under its footprint") is a PORT addition: the PS2 truck block does
 * a pure X/Z footprint test because the truck owns the only walkable top
 * at that height, so an X/Z match implies the player is on it. Natively we
 * may have several registrants and a free-fall Y, so we gate the carry to
 * a small Y band around top_y. EM_MOVING_Y_ABOVE / EM_MOVING_Y_BELOW are
 * PORT CONSTANTS flagged for tuning against the truck's live fall (the
 * descent must not outrun the band within one frame, |vel.y| ≤ ~0.667). */

/* Y band for "standing on the surface", relative to top_y. A few units of
 * headroom above (foot/eye offset slack) down to slightly below (so the
 * carry survives the surface having just dropped under the player by up to
 * one frame of fall). PORT CONSTANTS — tune against the live truck. */
#define EM_MOVING_Y_ABOVE 4.0f
#define EM_MOVING_Y_BELOW 1.0f

/* Clear the moving-surface registry. Call at the START of each frame,
 * before any actor registers (mirrors the PS2 zone table being rebuilt
 * per frame). */
void em_collision_moving_clear(void);

/* Register one moving walkable surface for this frame: an axis-aligned
 * footprint [minX,maxX] x [minZ,maxZ], its top surface Y, and its
 * per-frame velocity (added directly to a rider, no smoothing). Returns
 * the slot index, or -1 if the registry is full. */
int em_collision_moving_register(float minX, float maxX,
                                 float minZ, float maxZ, float top_y,
                                 const float vel[3]);

/* Resolve the player carry against the registered surfaces. If the player
 * footprint (pos x/z) lies inside a registered AABB AND pos[1] is within
 * [top_y - EM_MOVING_Y_BELOW, top_y + EM_MOVING_Y_ABOVE] of that surface,
 * ADD that surface's velocity to pos[] (direct, no smoothing — the PS2
 * D_70003.._38A8 add) and return 1 ("riding"); else leave pos[] untouched
 * and return 0. When more than one surface matches, the one with the
 * highest top_y at/below the player wins. */
int em_collision_moving_carry(float pos[3]);

/* Headless self-test of the carry primitive (EM_CARRY_TEST=1 hook; also
 * directly callable). Returns 0 on PASS, nonzero on FAIL. OS-free. */
int em_collision_moving_selftest(void);

/* --- Static blocking AABBs (the gated path-blocker primitive) ----------
 *
 * The "solid prop blocks the path while locked" primitive (decomp repo
 * Extermination/docs/INVESTIGATION_area11_grate.md §5 — the AREA-11 gated
 * GRATE, placement record 18). The PS2 grate (func_00159210) is a solid
 * path blocker while closed: it carries a collision hull from its bars
 * model (per-area id 0x04) and removes it on the open clip. Until the
 * unlock bit (D_0081084C & 0x80 = em_game_terminal_powered) is set, the
 * grate blocks the corridor; once powered, the hull is dropped and the
 * bars slide open.
 *
 * This is a per-FRAME registry mirroring the moving-surface one above:
 * actors clear it at frame start and re-register their CLOSED hull each
 * frame they are blocking, then the player wall-solve consults it once,
 * AFTER the static EMCL wall solve, to push the player OUT of any AABB
 * they have penetrated. Unlike the EMCL polygon world (single-sided,
 * front-facing only — an actor standing against a wall slides along it),
 * a blocker AABB is a SOLID volume: the player is pushed out along the
 * axis of least penetration so they cannot enter it at all.
 *
 * Engine-global like the moving-surface table (a fixed scratch array, not
 * bound to an EmCollision world). Dormant until an actor registers — the
 * GRATE actor (em_game.c) is the first and, for AREA-11, only registrant;
 * with nothing registered, the probe always returns 0 (no push). The
 * grate registers ONLY while NOT powered, so once the area is powered the
 * registry is empty and movement is identical to before. */

/* One static blocking AABB (world-axis-aligned). */
typedef struct {
    float minX, maxX, minZ, maxZ, minY, maxY;
} EmBlockerAabb;

/* Clear the blocker registry. Call at the START of each frame, alongside
 * em_collision_moving_clear (mirrors the PS2 zone table being rebuilt). */
void em_collision_blocker_clear(void);

/* Register one static blocking AABB for this frame. Returns the slot index
 * or -1 if the registry is full. The box is normalized (min/max swapped if
 * needed) so callers may pass either order. */
int em_collision_blocker_register(const EmBlockerAabb *aabb);

/* Push `pos` OUT of any registered blocker it has entered, treating the
 * player as a cylinder of horizontal `radius` (the X/Z wall radius) with
 * its body spanning [pos.y, pos.y + EM_BLOCKER_BODY_H] vertically. For each
 * blocker whose X/Z footprint (expanded by `radius`) AND Y span the player
 * overlaps, resolve along the axis of least horizontal penetration (X or Z)
 * — the player is ejected to the nearer face. Returns the number of
 * blockers that pushed the player this frame (0 = untouched, so normal
 * movement is bit-for-bit unchanged when nothing is registered or the
 * player is clear). pos.y is never modified (a blocker is a wall, not a
 * floor — height stays owned by the floor query). */
int em_collision_blocker_probe(float pos[3], float radius);

/* Player body height for the vertical-overlap gate, relative to pos.y (the
 * foot). A blocker only pushes the player when their body band
 * [pos.y, pos.y + EM_BLOCKER_BODY_H] overlaps the blocker's [minY,maxY], so
 * a low blocker the player has climbed above (or a high one they pass
 * under) does not eject them. PORT CONSTANT — sized to the player capsule
 * (the eye/foot offset is ~17 u; a conservative body height blocks at the
 * grate's mid-height while not gating on sub-unit Y noise). */
#define EM_BLOCKER_BODY_H 12.0f

/* Headless self-test of the blocker primitive (EM_BLOCKER_TEST=1 hook;
 * also directly callable). Returns 0 on PASS, nonzero on FAIL. OS-free. */
int em_collision_blocker_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_COLLISION_H */
