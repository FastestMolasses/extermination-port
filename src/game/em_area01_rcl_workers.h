/* Original AREA01 packet workers over the existing live RCL owner. */
#ifndef EM_AREA01_RCL_WORKERS_H
#define EM_AREA01_RCL_WORKERS_H
#include "game/em_area01_runtime.h"

int em_area01_rcl_workers_handles(uint32_t function);
/* No actor transactions or native callbacks: safe within the active byte
 * segment. Returned packet addresses resolve through em_rcl_bytes_mut, and
 * all context/chain writes target the same RCL storage. The adapter retains
 * no state. The caller owns the first-fault latch: zero on entry, original
 * failing address on refusal. 0 handled, 1 unknown function, -1 refusal. */
int em_area01_rcl_workers_call(EmArea01Call *, uint32_t *fault_address);
#endif
