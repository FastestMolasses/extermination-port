#ifndef EM_AIM_FIRE_SDK_MEMORY_H
#define EM_AIM_FIRE_SDK_MEMORY_H
#include "game/em_aim_fire_target.h"
#include <stddef.h>
/* Original memory access order around the existing verified SDK owners.
 * Returns 0 handled, 1 unhandled, -1 missing view or failed arithmetic. */
int em_aim_fire_sdk_memory_call(void *context,
    void *(*map)(void *,uint32_t,size_t,int),EmAimFireTargetCall *);
#endif
