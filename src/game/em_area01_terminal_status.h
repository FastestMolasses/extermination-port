/* AREA01 terminal status boundary: existing original owners and canonical
 * storage only. No accepted-save worker or card I/O is provided here. */
#ifndef EM_AREA01_TERMINAL_STATUS_H
#define EM_AREA01_TERMINAL_STATUS_H
#include "game/em_area01_actor_view.h"
#include "game/em_area01_room.h"
#include "game/em_area01_scene_view.h"
#include <string.h>

/* 00121A28 is the C runtime memset. Limit this binding to the exact reset
 * span passed by the one translated 00225A00 owner. */
static inline int em_area01_terminal_reset_worker(void *ctx, EmArea01RoomCall *call)
{
    if (!call || call->fn != 0x00121A28u || call->na != 3 || call->nf ||
        call->a[0] != 0x00810040u || call->a[1] != 0 || call->a[2] != 0xD4u)
        return -1;
    uint8_t *block = em_area01_scene_view(ctx, 0x00810040u, 0xD4u);
    if (!block) return -1;
    memset(block, 0, 0xD4u);
    call->v0 = 0x00810040u;
    return 0;
}

static inline int em_area01_terminal_reset(EmSceneState *scene)
{
    if (!scene) return -1;
    EmArea01Room room = {.ctx = scene, .call = em_area01_terminal_reset_worker};
    return em_area01_room_00225A00(&room);
}

/* BATTERY's request-6 reads borrow the live terminal's type/cost. Snapshot
 * composes the canonical actor and passive private fields between owner
 * segments, validates live allocation, and retains no replacement owner. */
static inline int em_area01_terminal_owner_read(EmArea01ActorView *actors, uint32_t owner,
                                                uint32_t offset, uint32_t size, int32_t *value)
{
    if (!value || owner < EM_ACTOR_POOL_BASE ||
        (owner - EM_ACTOR_POOL_BASE) % EM_ACTOR_RECORD_SIZE ||
        !((offset == 3 && size == 1) || (offset == 0x34 && size == 2)))
        return -1;
    uint8_t type, cost[2];
    if (em_area01_actor_view_snapshot(actors, owner + 3, 1, &type) < 0 || type != 0x38)
        return -1;
    if (offset == 3) { *value = type; return 0; }
    if (em_area01_actor_view_snapshot(actors, owner + 0x34, 2, cost) < 0)
        return -1;
    *value = (int16_t)((uint16_t)cost[0] | (uint16_t)cost[1] << 8);
    return 0;
}
#endif
