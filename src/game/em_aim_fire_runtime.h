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
#endif
