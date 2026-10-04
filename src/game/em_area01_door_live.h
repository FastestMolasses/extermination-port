/* Shared door originals over canonical AREA01 records and delivered data.
 * No singleton placement, pose bank, script copy or per-door cache. */
#ifndef EM_AREA01_DOOR_LIVE_H
#define EM_AREA01_DOOR_LIVE_H
#include "game/em_area01_runtime.h"

typedef struct {
    EmArea01RuntimeHost host;
    uint32_t fault_address;
    int fault;
} EmArea01Door;

int em_area01_door_bind(EmArea01Door *, const EmArea01RuntimeHost *);
int em_area01_door_handles(uint32_t function);
/* Same worker/transaction contract as EmArea01Runtime. The caller's actor
 * segment is active; every worker commits and refreshes it. The data view
 * must map mutable script words, scratchpad, globals and the model owner's
 * writable +40 bank word. Other model/pose bytes remain their owner's.
 * Unknown functions and missing resources fail without substitutes. */
int em_area01_door_call(EmArea01Door *, EmArea01Call *);
#endif
