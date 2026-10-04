/* Borrowed original-address collision boundary; owns no game state. */
#ifndef EM_AREA01_COLLISION_VIEW_H
#define EM_AREA01_COLLISION_VIEW_H
#include "game/em_aim_fire_world_live.h"
#include "game/em_area01_runtime.h"
#include "game/em_startup_load_gaps.h"

typedef struct {
    EmAimFireWorldLive *shared;
    const EmCollSegment *segment;
    EmCollMoveScratch *move;
    /* Actual loader resource, and the address of the existing mutable cells.
     * The original 00199C50 derives immutable pointer metadata from this. */
    EmSlgCollFile file;
    /* Optional canonical identity defaults, and exact per-borrow pool lifetime
     * guards. A pool slot retains its EE identity while free. */
    const EmActorPool *pool;
    const EmPlayerLiveActor *player;
    void *ctx;
    uint32_t (*address)(void *, const void *identity);
    const void *(*identity)(void *, uint32_t address);
    uint64_t (*generation)(void *);
    uint8_t *(*bytes)(void *, uint32_t, uint32_t, int);
} EmArea01CollisionHost;

typedef struct {
    EmArea01CollisionHost host;
    uint32_t metadata[EM_SLG_COLL_SPAD_WORDS];
    uint32_t self_word, point_before[4];
    const void *entity_before, *self_before;
    int record_before, node_before;
    uint64_t generation;
    uint32_t pool_generation[EM_ACTOR_POOL_CAPACITY];
    uint8_t pool_allocated[EM_ACTOR_POOL_CAPACITY];
    /* Read-only encodings of the canonical native lists. */
    uint32_t cursor[EM_ACTOR_LIST_COUNT][2];
    uint32_t list[EM_ACTOR_LIST_COUNT][EM_ACTOR_LIST_MAX];
    uint32_t fault_address;
    int active, fault;
} EmArea01CollisionView;

uint32_t em_area01_collision_pool_address(const EmActorPool *, const EmPlayerLiveActor *, const void *);
const void *em_area01_collision_pool_identity(const EmActorPool *, const EmPlayerLiveActor *, uint32_t);
int em_area01_collision_view_bind(EmArea01CollisionView *, const EmArea01CollisionHost *);
/* Every native worker boundary must commit the previous borrow then begin a
 * new one. Unknown and incompatible spans are refused, never filled. */
int em_area01_collision_view_begin(EmArea01CollisionView *);
/* True for overlap with a reserved collision span, even when bytes refuses. */
int em_area01_collision_view_owns(const EmArea01CollisionView *, uint32_t, uint32_t);
uint8_t *em_area01_collision_view_bytes(EmArea01CollisionView *, uint32_t, uint32_t, int write);
int em_area01_collision_view_commit(EmArea01CollisionView *);
/* Requires an inactive view; original translated workers only. */
int em_area01_collision_view_call(EmArea01CollisionView *, EmArea01Call *);
#endif
