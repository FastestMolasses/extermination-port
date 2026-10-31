#include <stddef.h>
#include "game/em_area01_runtime.h"
#include "game/em_stream_lanes_original.h"
static EmArea01Runtime runtime;
int a01rt_bind(const EmArea01RuntimeHost *h) { return em_area01_runtime_bind(&runtime, h); }
int a01rt_bind_area(const EmArea01RuntimeHost *h, unsigned area)
{ return em_area01_runtime_bind_area(&runtime, h, area); }
int a01rt_call(EmArea01Call *c) { return em_area01_runtime_call(&runtime, c); }
uint32_t a01rt_fault(void) { return runtime.fault_address; }
int a01rt_stream_worker(EmArea01Call *c)
{
    if (!c || c->function != 0x001281C0u || c->na != 0 || c->nf != 1) return -1;
    c->v0 = (uint64_t)(int64_t)em_stream_lanes_001281C0(c->f[0]);
    return 0;
}

/* The composition services the later levels' translations name; the runtime
 * oracle's entries never reach them. */
void *em_aim_fire_binding_bytes(uint32_t address, size_t size, int write);
int em_aim_fire_runtime_particle(uint32_t function, const uint32_t a[5]);
void em_pose_host_001CA1C0(uint32_t m[16], const uint32_t q[4], const uint32_t translation[3],
                           uint32_t spad3760[11]);
void *em_aim_fire_binding_bytes(uint32_t address, size_t size, int write)
{
    (void)address; (void)size; (void)write;
    return NULL;
}
int em_aim_fire_runtime_particle(uint32_t function, const uint32_t a[5])
{
    (void)function; (void)a;
    return -1;
}
void em_pose_host_001CA1C0(uint32_t m[16], const uint32_t q[4], const uint32_t translation[3],
                           uint32_t spad3760[11])
{
    (void)m; (void)q; (void)translation; (void)spad3760;
}
