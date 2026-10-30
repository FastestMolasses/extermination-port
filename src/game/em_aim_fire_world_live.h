#ifndef EM_AIM_FIRE_WORLD_LIVE_H
#define EM_AIM_FIRE_WORLD_LIVE_H
#include "game/em_aim_fire_live.h"
#include "game/em_collision_world.h"

#include "game/em_actor_pool.h"

/* The composition's host view list (host bookkeeping, not an original
 * table), sized so it can never refuse a view the original's state holds:
 * one record's most (the runtime's enumerate: an effect record's 40 node
 * fields, 57 trail views, 6 burst views and its +0x14 word) for each of the
 * actor pool's 256 records (D_00275BC8), plus the fixed views (the locals,
 * the player, the pose's 12 regions, the attach's bss words, the render
 * context's 32, the particles, the probe words, the class-4 list and the
 * target's two). The arrays are on the heap, one set per call depth. */
#define EM_AIM_FIRE_WORLD_RECORD_VIEWS 104u
#define EM_AIM_FIRE_WORLD_FIXED_VIEWS 256u
#define EM_AIM_FIRE_WORLD_VIEWS \
    (EM_AIM_FIRE_WORLD_FIXED_VIEWS + EM_ACTOR_POOL_CAPACITY * EM_AIM_FIRE_WORLD_RECORD_VIEWS)
/* Composition of existing owners, with no shadow actor records or game globals.
 * Region bytes must be the owner's live storage. enumerate appends views of
 * dynamically allocated records; it is called again after every worker call.
 * map is only the fallback for ranges not enumerated here. Region-only owners
 * need their accessed storage enumerated, including nodes allocated by callees.
 *
 * Native collision pointer fields are exposed read-only as original words.
 * grid_address must come from the area's actual original-layout metadata; no
 * address is invented when an older export does not carry that metadata. */
typedef struct {
    void *context;
    const EmPoseRegion *regions;
    unsigned region_count;
    int (*enumerate)(void *, EmPoseRegion *, unsigned capacity, unsigned *count);
    void *(*map)(void *, uint32_t address, size_t size, int write);
    /* Borrow the current 00183C40 target's canonical model storage.
     * Called after general enumeration and again after every callee; the
     * bytes are observations, never an independent actor/model record. */
    int (*target_regions)(void *, uint32_t node, EmPoseRegion *, unsigned capacity, unsigned *count);
    /* EFE00 certifies its completed node+24 store before its next callee.
     * Acquiring a writable view alone must not initialize owner metadata. */
    int (*written)(void *, uint32_t address, size_t size);
    /* A read-only copy of a pool record's bytes owned by another
     * composition (the AREA00 / AREA01 actor view): 0, or -1. */
    int (*actor_snapshot)(void *, uint32_t address, uint32_t size, void *out);
    const EmCollSegment *segment; /* NULL uses em_collision_world_segment() */
    const EmActorClassLists *lists; /* NULL uses the world's published lists */
    const EmCollisionWorldOwners *owners; /* NULL uses the world's owner hooks */
    uint32_t (*grid_address)(void *, const EmCollProbeGrid *, uint32_t node);
    /* The record bytes of a grid node by that address (NULL: none). */
    const uint8_t *(*grid_bytes)(void *, uint32_t address, uint32_t size);
    /* Encodings of native pointer fields, never independent game state. */
    uint32_t record_word, entity_word;
    /* 0x700031BC, the scratchpad word after the probes' point: no original
     * writes it in AREA11 (0 in the 7272 per-frame rows of the twelve AIM
     * beats and the 29 snapshot scratchpads), and the quadword loads of
     * 0x700031B0 (001028D0 in 00187780, 00102948 in 001F3340) read it. Its
     * one owner here: 0 at the attach, and a store through the
     * composition lands in it. point16 is the quadword read's snapshot. */
    uint32_t word_31BC, point16[4];
    /* 0x700030D0..0x700030DF, the quadword 00187780's 00102948 copies
     * from *0x700031D0 + 0x24 when the probe names the cell record
     * D_700030B0 (the copy's load drops the low four address bits): the
     * record's +0x20 word and its normal +0x24..+0x2C, a read-only
     * snapshot of the walker's fields (em_aim_fire_world_live_map). */
    uint32_t cell16[4];
    /* 1 while the move walkers' scratch is the one a composition call
     * wrote last (0019B2C0); the scratchpad views then show it. */
    int move_last;
    uint32_t class2_cursor, class2_words[EM_ACTOR_LIST_MAX];
} EmAimFireWorldLive;

void *em_aim_fire_world_live_map(void *, uint32_t address, size_t size, int write);
/* How many times the quadword 0x700030D0 (cell16) was served: the
 * EM_LAMP_PAGE_DUMP observer's count (em_chain_page_live). */
uint32_t em_aim_fire_world_live_cell_quadwords(void);
int em_aim_fire_world_live_call(void *, EmAimFireLive *, EmAimFireTargetCall *);
/* The innermost running em_aim_fire_world_live_call's resolution for its
 * region-only effect owners (the bytes they get for [a, a + n), with the
 * freed class-4 headers read only); NULL outside a call. Used by the
 * composition's effect owners and by tests. */
void *em_aim_fire_world_live_owner_view(uint32_t a, size_t n, int write);
/* The player's hull chain, resource 0x3A at D_0028A578[0] = 0xD1B9C0
 * (assets/player_hull.emom, tools/export_player_hull.py; its one home): the
 * bytes from `address` to the chain's end, or NULL. */
const uint8_t *em_aim_fire_world_live_player_hull(uint32_t address, uint32_t *size);
/* The probes' proxy of the player record D_008102B0 (what 0x700031D4 names
 * after one of this composition's segment probes hit the player; the host
 * hands it to em_area01_collision_set_player_proxy). */
const void *em_aim_fire_world_live_player_body(void);
#endif
