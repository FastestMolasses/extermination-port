#ifndef EM_AREA11_EFFECT_RUNTIME_H
#define EM_AREA11_EFFECT_RUNTIME_H

#include "em_gfx.h"
#include "game/em_area11_effect.h"

int em_area11_effect_runtime_load(EmGfx *gfx, const char *directory,
                                  const char *config, const char *texture);
void em_area11_effect_runtime_clear(EmGfx *gfx);
void em_area11_effect_runtime_tick(void);
void em_area11_effect_runtime_draw(EmGfx *gfx, const float view[16], float zoom);
const EmArea11Effect *em_area11_effect_runtime_state(void);

#endif
