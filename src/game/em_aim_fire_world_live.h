#ifndef EM_AIM_FIRE_WORLD_LIVE_H
#define EM_AIM_FIRE_WORLD_LIVE_H
#include "game/em_aim_fire_live.h"
#include "game/em_collision_world.h"

#define EM_AIM_FIRE_WORLD_VIEWS 512u
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
    /* EFE00 certifies its completed node+24 store before its next callee.
     * Acquiring a writable view alone must not initialize owner metadata. */
    int (*written)(void *, uint32_t address, size_t size);
    const EmCollSegment *segment; /* NULL uses em_collision_world_segment() */
    const EmActorClassLists *lists; /* NULL uses the world's published lists */
    const EmCollisionWorldOwners *owners; /* NULL uses the world's owner hooks */
    uint32_t (*grid_address)(void *, const EmCollProbeGrid *, uint32_t node);
    /* Encodings of native pointer fields, never independent game state. */
    uint32_t record_word, entity_word;
    uint32_t class2_cursor, class2_words[EM_ACTOR_LIST_MAX];
} EmAimFireWorldLive;

void *em_aim_fire_world_live_map(void *, uint32_t address, size_t size, int write);
int em_aim_fire_world_live_call(void *, EmAimFireLive *, EmAimFireTargetCall *);
#endif
