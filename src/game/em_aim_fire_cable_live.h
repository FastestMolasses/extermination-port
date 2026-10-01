#ifndef EM_AIM_FIRE_CABLE_LIVE_H
#define EM_AIM_FIRE_CABLE_LIVE_H
#include "game/em_aim_fire_live.h"
/* Existing verified 0021AAC0/0021A500 behaviours, over the live owners.
 * Returns their original alive/free status (1/0), or -1 with a live fault.
 * The effect owner retains allocation and generation lifetimes. */
int em_aim_fire_cable_live_tick(EmAimFireLive *,uint32_t node,uint32_t callback);
/* Original 001EFEB0 and 001CE860 service frames. Other entries return -1. */
int em_aim_fire_cable_live_call(EmAimFireLive *,EmAimFireTargetCall *);
#endif
