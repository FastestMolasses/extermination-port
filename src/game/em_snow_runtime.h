#ifndef EM_SNOW_RUNTIME_H
#define EM_SNOW_RUNTIME_H
#include "em_gfx.h"

void em_snow_runtime_clear(EmGfx *gfx);
int em_snow_runtime_load(EmGfx *gfx, const char *scene_dir,
                          const char *config, const char *texture,
                          unsigned flags);
void em_snow_runtime_tick(const float eye[3], unsigned selector);
void em_snow_runtime_draw(EmGfx *gfx, const float view[16], float zoom);

#endif
