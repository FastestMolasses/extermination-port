/* Existing flicker-light workers over the canonical AREA01 address view. */
#ifndef EM_AREA01_LIGHT_LIVE_H
#define EM_AREA01_LIGHT_LIVE_H
#include "game/em_area01_runtime.h"

int em_area01_light_handles(uint32_t function);
/* Active byte transaction in/out. Each external worker is forwarded through
 * host.worker with the original nested stack and complete argument lanes.
 * No actor, light, resource, or packet storage is retained by this adapter. */
int em_area01_light_call(const EmArea01RuntimeHost *, EmArea01Call *, uint32_t *fault_address);

typedef struct {
    uint32_t function, argument, word;
    float position[4], color[4];
    uint32_t multiplier, adder;
} EmArea01LightService;
/* D7FA0/D80B0/D3990/CAAC0: prepare while byte views are active, suspend
 * native transactions, invoke, then resume. Prepare copies only call-local
 * register/vector inputs. invoke's bytes provider need only resolve immutable
 * model REF targets; their lifetime extends through the frame's page flush.
 * Returns 0 handled, 1 not this service, -1 refusal. */
int em_area01_light_service_prepare(const EmArea01RuntimeHost *, const EmArea01Call *,
                                    EmArea01LightService *);
int em_area01_light_service_invoke(const EmArea01RuntimeHost *, const EmArea01LightService *,
                                   EmArea01Call *);
#endif
