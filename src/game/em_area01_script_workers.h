/* Existing shared script handlers plus the missing AREA01 walk callback. */
#ifndef EM_AREA01_SCRIPT_WORKERS_H
#define EM_AREA01_SCRIPT_WORKERS_H
#include "game/em_area01_runtime.h"
int em_area01_script_worker_handles(uint32_t function);
/* Canonical address views must be active. No persistent byte copy is held. */
int em_area01_script_worker_call(const EmArea01RuntimeHost *, EmArea01Call *, uint32_t *fault_address);
#endif
