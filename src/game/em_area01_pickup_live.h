/* Original pickup owners over canonical AREA01 record/script/global views. */
#ifndef EM_AREA01_PICKUP_LIVE_H
#define EM_AREA01_PICKUP_LIVE_H
#include "game/em_area01_runtime.h"
typedef int (*EmArea01PickupAuraDraw)(void *ctx,uint32_t node,uint32_t record,uint32_t angle,uint32_t timer);
typedef struct {
    EmArea01RuntimeHost host;
    EmArea01PickupAuraDraw aura_draw;
    void *aura_ctx;
    uint32_t fault_address;
    int fault;
} EmArea01Pickup;
int em_area01_pickup_bind(EmArea01Pickup *,const EmArea01RuntimeHost *);
/* Draw-block callback brackets native view transactions itself. */
void em_area01_pickup_set_aura_draw(EmArea01Pickup *,EmArea01PickupAuraDraw,void *ctx);
int em_area01_pickup_handles(uint32_t function);
/* Active canonical actor segment in/out. The host commits and refreshes
 * every worker call, exactly as for EmArea01Runtime. No per-pickup cache. */
int em_area01_pickup_call(EmArea01Pickup *,EmArea01Call *);
#endif
