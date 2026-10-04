#include "game/em_area01_runtime.h"
#include "game/em_stream_lanes_original.h"
static EmArea01Runtime runtime;
int a01rt_bind(const EmArea01RuntimeHost *h) { return em_area01_runtime_bind(&runtime, h); }
int a01rt_call(EmArea01Call *c) { return em_area01_runtime_call(&runtime, c); }
uint32_t a01rt_fault(void) { return runtime.fault_address; }
int a01rt_stream_worker(EmArea01Call *c)
{
    if (!c || c->function != 0x001281C0u || c->na != 0 || c->nf != 1) return -1;
    c->v0 = (uint64_t)(int64_t)em_stream_lanes_001281C0(c->f[0]);
    return 0;
}
