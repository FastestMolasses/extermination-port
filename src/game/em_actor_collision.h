/* em_actor_collision.h - original actor collision cells: publication and query.
 *
 * Lane "actor-collision" (docs/ACTOR_COLLISION.md). Hand translation of the
 * original owner-cell path (Extermination/src + build/asm; the .s is the
 * authority where the readable C is a NEARMISS):
 *
 *   publication
 *     001A2370  re-transform an owner's hull by its world matrix and rebuild
 *               the hull AABB (the truck +0xD0, drums, pickups, elevator)
 *     001B1B70  per-frame class-list publish; class 4 -> 001B1D20 pushes the
 *               owner's +0x14 onto D_00275B80 (cap 0x80); the other classes
 *               (001B1C60, 001B1CA0, 001B1D60, 001B1DA0, 001B1DE0) likewise
 *     001AAD00  the list half of the frame close-out: publish each live list
 *               (cursor, count) and reset it (001AF8E0 resets all of them)
 *   query
 *     0019AB20  the vertical probe (floor service 00175900, crate supports,
 *               001760C0 columns): 0019F730 over the cells, then the grid
 *     0019F730  pass 1 static cells of *0x70003250, pass 2 the published
 *               class-4 owners' cells, with the prim tests 001A44B0
 *               (0x8000/0x4000), 001A4650 (0x2000) and 001A4030 (0x1000)
 *     0019BC40  the column table's pass 1 over the owners, with 001A56A0
 *               (0x4000), 001A5760 (0x2000) and 001A58B0 (0x1000); pass 2
 *               and the cull are em_collision_column_finish
 *
 * The cell directory is kept in its original byte layout (the table
 * *0x70003250 points at; count word at +0, then one offset word per uid,
 * then the hulls: min xyz, max xyz, +0x18 prim count (s16), +0x1C prims).
 * It is loaded from the user's own disc data (the AREA11 directory is
 * chunk15/f12_id44.bin +0x39800; see docs/ACTOR_COLLISION.md for the
 * export) and never embedded. Owners are the pool's EmActor records: the
 * lists hold each owner's +0x14 (EmActor.self), and the walkers read +0x00,
 * +0x02, +0x0E and the byte +0x54.
 *
 * Arithmetic is the EE model every original-instruction oracle here shares:
 * each single-precision result truncates toward zero, overflow gives the
 * largest finite value and denormal results are zero; mula/madd accumulate
 * as separate truncated operations; division by zero gives the signed
 * largest finite value.
 *
 * Verified by tools/test_actor_collision_reference.py (the original
 * instructions over captured AREA11 RAM) and tests/actor_collision_test.c.
 */
#ifndef EM_ACTOR_COLLISION_H
#define EM_ACTOR_COLLISION_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_actor_pool.h"
#include "game/em_collision.h"
#include "game/em_crate_original.h"
#include "game/em_player_floor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The cell directory (*0x70003250, count 0x7000324C) ----------------- */

typedef struct {
    uint8_t *bytes;   /* original layout, little-endian; owned (malloc) */
    uint32_t size;
    int16_t count;    /* 0x7000324C: the directory's count word */
} EmActorCellTable;

/* Copy and validate a directory image. Every offset word must name a hull
 * whose prims lie inside the image (as a raw offset: pass 2 of the walkers
 * and 001A2370 use the word unmasked). Returns 0, or -1 on a malformed
 * image (the table is left empty). */
int em_actor_cells_init(EmActorCellTable *table, const void *image, size_t size);
/* Load a raw directory file (the export of the user's disc data). */
int em_actor_cells_load(EmActorCellTable *table, const char *path);
void em_actor_cells_free(EmActorCellTable *table);
/* The hull of `uid` (tbl + offset word), or NULL for uid >= count / word 0. */
const uint8_t *em_actor_cells_hull(const EmActorCellTable *table, unsigned uid);
/* The hull AABB header: min xyz, max xyz. Returns 1, or 0 for no hull. */
int em_actor_cells_bounds(const EmActorCellTable *table, unsigned uid, float out[6]);

/* 001A2370(actor, matrix). `uid_halfword` is the owner's +0x0E. Returns 1
 * when it re-transformed (or, like the original, silently did nothing: uid
 * 0xFF, word 0, uid >= count, first prim without 0x800), -1 on a NULL
 * argument. */
int em_actor_cells_retransform_001A2370(EmActorCellTable *table, uint16_t uid_halfword,
                                        const float matrix[16]);

/* ---- Per-frame class lists (D_00275B54..D_00275BB8) --------------------- */

enum {
    EM_ACTOR_LIST_CLASS1,   /* 001B1C60: D_00275BB0/BB8, base D_0028B020, cap 0x0C */
    EM_ACTOR_LIST_CLASS_D,  /* 001B1DA0: D_00275BA0/BA8, base D_0028AFF0, cap 0x30 */
    EM_ACTOR_LIST_CLASS2,   /* 001B1CA0: D_00275B90/B98, base D_0028AF30, cap 0x40 (2, 0xA) */
    EM_ACTOR_LIST_CLASS4,   /* 001B1D20: D_00275B80/B88, base D_0028AE30, cap 0x80 */
    EM_ACTOR_LIST_CLASS7,   /* 001B1D60: D_00275B70/B78, base D_0028AC30, cap 0x40 */
    EM_ACTOR_LIST_FLAG80,   /* 001B1DE0: D_00275B60/B68, base D_0028AB30, cap 0x20 */
    EM_ACTOR_LIST_COUNT
};
#define EM_ACTOR_LIST_MAX 0x80

/* One list. The original pushes downward from a static base (slot i lives
 * at base - 4 * (i + 1)); 001AAD00 publishes the cursor and count and
 * resets the cursor to the base, so this frame's pushes overwrite the
 * published entries from the end, exactly as `slot` does here. Published
 * entry j is slot[published - 1 - j]. */
typedef struct {
    const EmActor *slot[EM_ACTOR_LIST_MAX];
    int16_t live;       /* the live count (e.g. D_00275B88) */
    int16_t published;  /* the published count (e.g. D_00275B84) */
} EmActorClassList;

typedef struct {
    EmActorClassList list[EM_ACTOR_LIST_COUNT];
    int16_t count_b58;  /* D_00275B58: 001AAD00 zeroes it; no pusher is translated */
} EmActorClassLists;

/* 001AF8E0's list half: every cursor at its base, every count 0. */
void em_actor_class_lists_reset(EmActorClassLists *lists);
/* 001B1B70(actor). Returns 1, or -1 on a NULL argument. */
int em_actor_class_publish_001B1B70(EmActorClassLists *lists, const EmActor *actor);
/* 001B1D20(actor): the class-4 push alone (the drum's contact worker). */
int em_actor_class_push4_001B1D20(EmActorClassLists *lists, const EmActor *actor);
/* 001AAD00's list block (after its nine hooks, which are not this module's). */
void em_actor_class_lists_swap_001AAD00(EmActorClassLists *lists);
/* Published entry j of a list (the pointer the original stored), or NULL. */
const EmActor *em_actor_class_list_entry(const EmActorClassLists *lists, int which, int j);

/* ---- Queries ------------------------------------------------------------- */

typedef struct {
    EmActorCellTable *table;          /* *0x70003250 */
    const EmActorClassLists *lists;   /* D_00275B7C / D_00275B84 */
    const EmCollision *grid;          /* the set-4 grid (EMCL) */
    /* Pass 1 of 0019F730 reads, for each static cell (offset word bit 31
     * set, bit 30 clear), byte +8 of record i (stride 0x28) of
     * D_0024D7C0[D_00810700][D_00810701]. NULL when the area supplies none:
     * AREA11's directory has no static cell (word 0 has bit 31 clear, so
     * pass 1 stops at once); reaching one without this view faults. */
    const uint8_t *static_kind;
    unsigned static_kind_count;
} EmActorCollisionWorld;

/* The query actor as 0019AB20 reads it. */
typedef struct {
    const void *self;   /* +0x14 -> 0x70003254 (the walkers skip this owner) */
    uint8_t cls;        /* +0x02 & 0x1F -> 0x7000324E */
    float *feet_y;      /* +0xB4, moved by mask bit 31; required only then */
} EmActorCollisionQuery;

enum {
    EM_ACTOR_RECORD_NONE = 0,  /* 0x700031D0 = 0 */
    EM_ACTOR_RECORD_CELL = 1,  /* 0x700031D0 = D_700030B0 (the prim tests' record) */
    EM_ACTOR_RECORD_GRID = 2   /* 0x700031D0 = the grid node */
};

typedef struct {
    int kind;                /* return value / 0x700031D8: 0, 2 or 4 */
    float point[3];          /* 0x700031B0 */
    float delta[3];          /* 0x700031C0 = point - position */
    const EmActor *entity;   /* 0x700031D4: the last owner hit, kept across the grid pass */
    int record;              /* EM_ACTOR_RECORD_* */
    int poly;                /* the grid node's EMCL poly index (record GRID), else -1 */
    uint16_t node;           /* record +0x1A: class (high byte) | kind (low byte) */
    uint8_t node_class_known;/* 0 for a grid node when the EMCL lacks EM_COLL_FLAG_NODE_CLASS */
    float normal[3];         /* record +0x24 */
    /* The segment 0019C830 received (0x70003190 / 0x700031A0 at its entry,
     * after 0019F730 clamped the end), when mask bit 2 ran the grid pass;
     * zero otherwise. 0019AB20 restores both before it returns, so no
     * caller reads them there; they record where the grid pass started
     * (the reference test checks them against the original's entry state). */
    float grid_start[3];
    float grid_end[3];
} EmActorCollisionHit;

/* 0019AB20(actor, position, probe, mask): the segment runs from
 * (x, y - probe.y +- 0.001, z) to the position; mask bit 1 walks the cells
 * (0019F730), bit 2 the grid (0019C830), bit 31 adds delta.y to *feet_y.
 * Returns 0, 2 or 4 (as the original), or -1 on a fault (NULL argument, a
 * static cell without its kind view, a malformed hull). */
int em_actor_collision_ground_0019AB20(const EmActorCollisionWorld *world,
                                       const EmActorCollisionQuery *query,
                                       const float position[3], const float probe[3],
                                       uint32_t mask, EmActorCollisionHit *hit);

/* 0019BC40(pos): the column table with every published class-4 owner cell
 * (all prim types). `math` is required, with both workers: 001A58B0 and the
 * grid pass's 0019F330 call the SDK 0011E748 (sqrt) and 0011DBB8 (atan);
 * bind em_item_sdk_sqrt (0011E748's nonnegative path, 0011CB90) and
 * em_director_original_0011DBB8 (docs/ACTOR_COLLISION.md section 7).
 * Returns the survivor count, or -1 on a fault (NULL or incomplete math
 * included). */
int em_actor_collision_column_0019BC40(const EmActorCollisionWorld *world,
                                       const float position[3],
                                       const EmCollColumnMath *math, EmCollColumn *out);

/* ---- Worker adapters for the owner modules and the player -------------- */

/* One owner's view of the world, for the owners' worker tables. */
typedef struct {
    EmActorCollisionWorld *world;
    EmActorClassLists *lists;       /* the live lists the owner publishes to */
    const EmActor *actor;           /* the owner's pool record */
    const EmActorPool *pool;        /* for the probe's D_700031D4 address */
} EmActorCollisionOwner;

/* EmTruckHooks.hull (001A2370(actor, +0xD0)) and .hull_bounds. 1 or -1. */
int em_actor_collision_owner_hull(void *owner, const float matrix[16]);
int em_actor_collision_owner_hull_bounds(void *owner, float bounds[6]);
/* EmTruckHooks.publish / EmCrateOriginalHooks.publish (001B1B70). */
int em_actor_collision_owner_publish(void *owner);
/* EmDrumOriginalHooks.contact (001B1D20). */
int em_actor_collision_owner_contact(void *owner);
/* EmCrateOriginalHooks.probe / EmDrumOriginalHooks.probe: 0019AB20(self,
 * from, {0, dy, 0}, mode) with the owner as the query (its own cell is
 * skipped). position is the owner's +0xB0; mode bit 31 moves position[1].
 * out->actor is D_700031D4 as the original record address (the pool's
 * em_actor_pool_address); a hit owner outside `pool` faults. 1 or -1. */
int em_actor_collision_owner_probe(void *owner, float position[4], const float from[3], float dy,
                                   uint32_t mode, EmCrateProbe *out);

/* The player's floor workers (EmPlayerFloorWorkers.ground,
 * EmPlayerFallWorkers.ground): 0019AB20(player, position, probe, mask). */
typedef struct {
    EmActorCollisionWorld *world;
    EmActorCollisionQuery query;    /* self = the player's +0x14, cls = +0x02 */
    /* 0x700031D4 after the LAST call (also returned per probe as
     * EmPlayerProbeHit.owner). 00175CF0 stores it in the player's
     * +0x214 (D_008104C4) when kind & 2 and it is not NULL, and reads +0x214
     * again in the same function (00175640 for a 0x1000 floor, the
     * contact |= 0x80 test), while 00175900 probes again afterwards (the
     * 0x5B depth probe). So the store belongs inside the floor module's
     * 00175CF0 translation, from the hit record of that very probe
     * (docs/ACTOR_COLLISION.md section 7, item 4); a binder must not read
     * this field after em_player_floor_service returns. */
    const EmActor *entity;
} EmActorCollisionPlayer;

/* Fills the floor module's probe record. Faults (-1) where the native world
 * cannot give the original's answer instead of guessing: a grid hit when the
 * EMCL lacks EM_COLL_FLAG_NODE_CLASS (the class byte is unknown), and a
 * record whose surface byte is 0x35 (its +0x34 drive axis is not carried;
 * nothing else reads EmPlayerProbeHit.axis, which is left zero). */
int em_actor_collision_player_ground(void *player, const float position[3], const float probe[3],
                                     unsigned mask, EmPlayerProbeHit *hit);
/* EmPlayerFloorWorkers.link_test: 00175640(owner) over the EmActor the
 * ground worker reported (EmPlayerProbeHit.owner, stored in +214): its
 * +3 (model) and +0x10 (callback, the original behaviour address).
 * `player` is unused (00175640 reads only the owner). 0, or -1 for a NULL
 * result pointer. */
int em_actor_collision_player_link(void *player, const void *owner, int *result);
/* EmPlayerClimbLive.link_kind: the climb's view of the +308 owner (00161790
 * and 0015DF10 read its +0x10 behaviour): 2 for the AREA11 overlay routines
 * 00828700 / 00827880, 1 for any other owner, 0 for none. */
int em_actor_collision_player_link_kind(void *context, const void *owner);
/* EmPlayerFallWorkers.column (00179450's 0019BC40): the column table at
 * `position` over the player's world. `context` is an
 * EmActorCollisionPlayerColumn: the world and 0019BC40's SDK workers
 * (section 7 item 4 names the translations; NULL math faults). Past 16 survivors
 * the original's result arrays alias one another (docs/PLAYER_CLIMB_SLIDE.md
 * section 3), which the port does not reproduce: that is a fault, as in the
 * climb. 0, or -1 on a fault. */
typedef struct {
    EmActorCollisionWorld *world;
    const EmCollColumnMath *math;
} EmActorCollisionPlayerColumn;
int em_actor_collision_player_column(void *context, const float position[3],
                                     EmPlayerFloorTable *table);

#ifdef __cplusplus
}
#endif

#endif /* EM_ACTOR_COLLISION_H */
