#include "game/em_area01_pickup_live.h"
static EmArea01Pickup pickup;
int ap_bind(const EmArea01RuntimeHost *host) { return em_area01_pickup_bind(&pickup,host); }
int ap_call(EmArea01Call *call) { return em_area01_pickup_call(&pickup,call); }
uint32_t ap_fault(void) { return pickup.fault_address; }
void ap_draw(EmArea01PickupAuraDraw draw,void *ctx)
{ em_area01_pickup_set_aura_draw(&pickup,draw,ctx); }
