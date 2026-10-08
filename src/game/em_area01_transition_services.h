#ifndef EM_AREA01_TRANSITION_SERVICES_H
#define EM_AREA01_TRANSITION_SERVICES_H
#include "game/em_area01_runtime.h"

/* Existing interaction-alignment, door-sound and scene-fade owners over
 * the caller's active canonical byte views. No state is retained. */
int em_area01_transition_handles(uint32_t function);
int em_area01_transition_call(const EmArea01RuntimeHost *, EmArea01Call *, uint32_t *fault);
#endif
