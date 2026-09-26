#ifndef EM_SNOW_RUNTIME_H
#define EM_SNOW_RUNTIME_H
#include "em_gfx.h"
#include "game/em_weather.h"

void em_snow_runtime_clear(EmGfx *gfx);
int em_snow_runtime_load(EmGfx *gfx, const char *scene_dir,
                          const char *config, const char *texture,
                          unsigned flags);
void em_snow_runtime_tick(const float eye[3], unsigned selector);
/* One 001E55F0 behaviour call of a weather actor whose own state (+0x1F0
 * block and +4 byte) is *weather (a fresh actor passes a zeroed EmWeather:
 * state 0 seeds). `transition` is D_008106B8 and `fade_state` the
 * D_0028A9A0 halfword, read by the original at the end of state 1 (== 2 and
 * == 2 -> state 3). Returns 1 when the call is state 2/3, where the original
 * frees the actor (001AFC10) and draws nothing: the caller frees its node.
 * A drawing call replaces this frame's snow particles; a releasing call
 * leaves them to the other weather actor. em_snow_runtime_tick is this over
 * the module's own EmWeather with transition 0 (the non-roster scenes).
 * A drawing call runs 001E67C0's fog programmer calls on the render context
 * (em_rcl_0021B9A0); -1 when they fault. */
int em_snow_runtime_tick_actor(EmWeather *weather, const float eye[3], unsigned selector,
                               unsigned transition, unsigned fade_state);
void em_snow_runtime_draw(EmGfx *gfx, const float view[16], float zoom);

#endif
