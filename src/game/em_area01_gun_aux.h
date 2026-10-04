#ifndef EM_AREA01_GUN_AUX_H
#define EM_AREA01_GUN_AUX_H
#include "game/em_area01_runtime.h"
#include "game/em_sdk_math_original.h"
int em_area01_gun_aux_handles(uint32_t function);
/* Existing 8282F0 and asin owners, plus 8287C0's original child spawn.
 * All persistent bytes are borrowed. SDK may be NULL for boundary fixtures;
 * the production caller supplies its canonical SDK context. */
int em_area01_gun_aux_call(const EmArea01RuntimeHost *,EmSdkMathContext *,EmArea01Call *,uint32_t *fault);
#endif
