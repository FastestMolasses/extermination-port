/* Canonical model geometry and bone matrices for the existing hull locks. */
#ifndef EM_AREA01_HULL_LIVE_H
#define EM_AREA01_HULL_LIVE_H
#include "game/em_actor_pool.h"
#include "game/em_coll_grid_hull.h"
#include "game/em_owner_services_original.h"

typedef struct {
    void *ctx;
    int (*record)(void *, const EmActor *, uint8_t out[EM_ACTOR_RECORD_SIZE]);
    const uint8_t *(*resource_rest)(void *, uint32_t, uint32_t *size);
    const uint8_t *(*slot_bytes)(void *, uint32_t, uint32_t size);
} EmArea01HullHost;

typedef struct {
    EmArea01HullHost host;
    float matrices[EM_OWNER_SERVICES_MAX_BONES * 16];
} EmArea01HullView;

/* Borrow the delivered chain and copy the current +0x90 slot matrices.
 * The result stays valid until the next chain request. No geometry is
 * rebuilt, no game field is stored, and a refused source stays a fault. */
int em_area01_hull_chain(void *ctx, const EmActor *, EmCollHullChain *out);
#endif
