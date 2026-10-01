#ifndef EM_AIM_FIRE_RUNTIME_H
#define EM_AIM_FIRE_RUNTIME_H
#include "game/em_actor_pool.h"
/* Install the quarantined diagnostic composition over existing live owners. */
void em_aim_fire_runtime_attach(EmActorPool *pool);
/* Called after the effect owner's area attach; detach clears its callback. */
int em_aim_fire_runtime_effects_attach(void);
#endif
