#include "game/em_area01_door_live.h"
static EmArea01Door door;
static EmArea01Runtime runtime;
int a01door_bind(const EmArea01RuntimeHost *h)
{
    return em_area01_door_bind(&door,h)<0 ? -1 : em_area01_runtime_bind(&runtime,h);
}
int a01door_call(EmArea01Call *c)
{
    return em_area01_door_handles(c->function) ? em_area01_door_call(&door,c) :
        em_area01_runtime_call(&runtime,c);
}
uint32_t a01door_fault(void) { return door.fault ? door.fault_address : runtime.fault_address; }
int a01door_direct(EmArea01Call *c) { return em_area01_door_call(&door,c); }
