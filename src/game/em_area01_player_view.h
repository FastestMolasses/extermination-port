/* Post-stage original-address access to the EXISTING live player record.
 * The only readable record is actor->bytes; saved/before are transaction
 * snapshots. Commit before every native worker, begin after it. Never nest
 * the projected view inside the player's own stage or retain a pointer across
 * commit. Inside a stage, use the explicitly read-only borrow instead.
 * See docs/LEVEL2_RUNTIME.md ("AREA01 external player record segments"). */
#ifndef EM_AREA01_PLAYER_VIEW_H
#define EM_AREA01_PLAYER_VIEW_H
#include "game/em_player_floor.h"
#define EM_AREA01_PLAYER_BASE 0x008102B0u

typedef struct {
    uint32_t position[3], hip[3], heading, vitals[4]; /* raw float bits */
    uint16_t iframes;
    uint8_t infected, low;
    uint32_t generation, hip_valid;
    uint32_t link_owner, link_prev, owner_generation, prev_generation;
} EmArea01PlayerValues;
/* Snapshot canonical external owners without changing them. Validate and
 * serialize native link pointers as original addresses; refuse unknown or
 * freed links. Publish is called only after validation, with actor->bytes
 * still in post-stage layout. It must store placement/yaw/vitals to their
 * canonical owners and must not fail or invoke other workers. */
typedef int (*EmArea01PlayerSnapshot)(void *ctx, EmArea01PlayerValues *out);
typedef void (*EmArea01PlayerPublish)(void *ctx, EmPlayerLiveActor *actor);

typedef struct EmArea01PlayerView {
    EmPlayerLiveActor *actor;
    EmArea01PlayerSnapshot snapshot;
    EmArea01PlayerPublish publish;
    void *ctx;
    uint8_t saved[EM_PLAYER_ACTOR_SIZE], before[EM_PLAYER_ACTOR_SIZE];
    EmArea01PlayerValues owners;
    const void *link_owner, *link_prev;
    uint8_t link_flags, link_type;
    uint32_t fault_address;
    int fault, active; /* active: 1 projected, 2 in-stage read-only borrow.
                       * fault: 1 boundary, 2 lifetime/stale owner, 3 protected field */
} EmArea01PlayerView;
void em_area01_player_view_init(EmArea01PlayerView *v, EmPlayerLiveActor *actor,
                               EmArea01PlayerSnapshot snapshot, EmArea01PlayerPublish publish, void *ctx);
int em_area01_player_view_begin(EmArea01PlayerView *v);
/* For a caller already executing inside the native player stage: borrow
 * actor->bytes exactly as they stand. No external owner is projected or
 * published, all byte writes refuse, and commit verifies the record/link
 * identity is unchanged. End before native work; begin again afterward. */
int em_area01_player_view_borrow_begin(EmArea01PlayerView *v);
int em_area01_player_view_commit(EmArea01PlayerView *v);
uint8_t *em_area01_player_view_bytes(EmArea01PlayerView *v, uint32_t address, uint32_t size, int write);
#endif
