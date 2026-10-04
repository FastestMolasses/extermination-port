#ifndef EM_AREA01_EFFECTS_SERVICES_H
#define EM_AREA01_EFFECTS_SERVICES_H

#include "game/em_area01_actor_view.h"
#include "game/em_area01_runtime.h"

/* One existing effects owner; no allocation or effect state in this adapter.
 * Call with actor/player/collision transactions committed, then resume them.
 * Operands must be available through host.bytes while transactions are off
 * (or copied to the caller's ordinary scratch before suspending). All vector
 * arguments must have their original 16-byte alignment; unsupported shapes
 * fail before allocation. Returns 0 handled, 1 not this service, -1 fault. */
int em_area01_effects_services_call(const EmArea01RuntimeHost *host, EmArea01Call *call);
typedef struct {
    uint32_t function, id, f12;
    int has_position;
    float position[4], rotation[4];
} EmArea01EffectsSpawn;
/* Preferred native boundary: prepare while byte views are active, suspend,
 * invoke, resume. No host pointers survive prepare; only call-local vectors. */
int em_area01_effects_services_prepare(const EmArea01RuntimeHost *host, const EmArea01Call *call,
                                      EmArea01EffectsSpawn *spawn);
int em_area01_effects_services_invoke(const EmArea01EffectsSpawn *spawn, EmArea01Call *call);

/* Compose into actor project(): 0 non-effect, 1..8 canonical spans, -1 fault.
 * Do not add a model projection over these same fields. Install written on
 * the actor view; it certifies only the original first +24 word store. */
int em_area01_effects_services_project(EmActor *actor,
                                     EmArea01ActorSpan spans[EM_AREA01_ACTOR_SHARED_MAX]);
int em_area01_effects_services_written(void *ctx, EmActor *actor, uint16_t offset, uint16_t size);

#endif
