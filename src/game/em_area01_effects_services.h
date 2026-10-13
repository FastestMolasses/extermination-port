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

/* AREA00's markers and puff transforms use these shared effects owners.
 * Prepare copies source operands while views are active; invoke runs with
 * views suspended; finish publishes only the transform bytes actually
 * written, after resuming. The transform destination is a disjoint local. */
typedef struct {
    uint32_t function, destination, kind, phase, f12, f13;
    float position[4], matrix[16];
    int32_t colour[4];
    uint8_t block[0x60];
    uint32_t written_offset, written_size;
} EmArea01EffectsDraw;
int em_area01_effects_draw_handles(uint32_t function);
int em_area01_effects_draw_prepare(const EmArea01RuntimeHost *, const EmArea01Call *,
                                   EmArea01EffectsDraw *, uint32_t *fault);
int em_area01_effects_draw_invoke(EmArea01EffectsDraw *, EmArea01Call *);
int em_area01_effects_draw_finish(const EmArea01RuntimeHost *, const EmArea01EffectsDraw *,
                                  uint32_t *fault);

/* Compose into actor project(): 0 non-effect, 1..8 canonical spans, -1 fault.
 * Do not add a model projection over these same fields. Install written on
 * the actor view; it certifies only the original first +24 word store. */
int em_area01_effects_services_project(EmActor *actor,
                                     EmArea01ActorSpan spans[EM_AREA01_ACTOR_SHARED_MAX]);
int em_area01_effects_services_written(void *ctx, EmActor *actor, uint16_t offset, uint16_t size);

#endif
