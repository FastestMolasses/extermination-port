/* Indicator children over canonical AREA01 actor/model views. */
#ifndef EM_AREA01_INDICATOR_LIVE_H
#define EM_AREA01_INDICATOR_LIVE_H
#include "game/em_area01_runtime.h"
int em_area01_indicator_handles(uint32_t function);
/* Active canonical segment in/out. Worker callbacks publish and refresh
 * native views. Generic children retain no separate colour/model record;
 * C2360/C22A0 are em_area01_model_call workers on the one slot arena.
 * The first reached failure is returned through fault_address. */
int em_area01_indicator_call(const EmArea01RuntimeHost *,EmArea01Call *,uint32_t *fault_address);
#endif
