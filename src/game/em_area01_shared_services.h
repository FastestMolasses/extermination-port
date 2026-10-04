#ifndef EM_AREA01_SHARED_SERVICES_H
#define EM_AREA01_SHARED_SERVICES_H
#include "game/em_area01_runtime.h"
#include "game/em_actor_pool.h"
#include "game/em_scene_state.h"
#include "game/em_sdk_math_original.h"

/* Active canonical memory segment. Reuses existing player predicate, roster
 * bit-test, director item and polygon workers. It owns no mutable storage.
 * 0 handled, 1 unknown entry, -1 missing/incompatible view or worker fault. */
int em_area01_shared_services_call(const EmArea01RuntimeHost *,EmSdkMathContext *,
                                   EmSceneState *,EmArea01Call *);
/* 001B1B70 only: call with actor/player/collision segments suspended after
 * touching the node. Publication borrows the actual pool and world lists;
 * resume all segments afterward. No actor fields or slots are copied. */
int em_area01_shared_services_publish(EmActorPool *,EmArea01Call *);
#endif
