#ifndef EM_AIM_FIRE_RUNTIME_H
#define EM_AIM_FIRE_RUNTIME_H
#include "game/em_actor_pool.h"
/* Install the quarantined diagnostic composition over existing live owners. */
void em_aim_fire_runtime_attach(EmActorPool *pool);
/* Called after the effect owner's area attach; detach clears its callback. */
int em_aim_fire_runtime_effects_attach(void);
/* The AREA11 binder's hook: binds a record the composition allocated
 * (001861C0's 001AFA90, the impact marker) to its pool behaviour by its
 * +0x10 once the root call that allocated it returns. Cleared by attach. */
void em_aim_fire_runtime_set_bind(int (*bind)(EmActor *));
/* The pool behaviour of such a record: 0018ABA0 (em_aim_fire_marker)
 * through the binding. 1 ran, -1 fault. */
int em_aim_fire_runtime_tick(EmActor *actor);
/* The tick log's view of a record the composition allocated: its +0x28
 * halfword once written (the marker's countdown, the muzzle node's frame).
 * 1, or 0 (none). */
int em_aim_fire_runtime_h28(const EmActor *actor, uint16_t *value);
/* 00188630's 001F4010(index, 0x700036A0) (the shell casing's seed) with the
 * equipment's copy of 0x700036A0..0x700036DF; 0, or -1. */
int em_aim_fire_runtime_001F4010(int32_t index, const uint32_t *at);
/* The knife's callees for em_equipment_live_set_world (behind the gate). */
int em_aim_fire_runtime_world_call(uint32_t fn, const uint32_t *a, unsigned na, uint32_t f12, unsigned nf,
                                   uint32_t *spad, uint32_t *v0);
int em_aim_fire_runtime_world_read(uint32_t address, void *out, uint32_t size);
const uint8_t *em_aim_fire_runtime_world_bytes(uint32_t address, uint32_t size);
int em_aim_fire_runtime_world_temp(const uint32_t words[4], uint32_t *address);
#endif
