/* AREA01's 001C4FA0 predicate and 001C50B0 flicker-light owner.
 * Original-layout memory and workers use the existing EmA01Math contract.
 * This standalone owner is unbound. See docs/LEVEL2_RUNTIME.md ("AREA01 flicker-light owner").
 *
 * Worker addresses: 001028B8 / 001028D0 / 00102900 (vector operations),
 * 00122BB8 (random), 001AFC10 (free), 001C5050 (point-light request),
 * 001D80B0 (light release), 001F5490 (model setup), 001F5F60 (draw).
 * Existing translations own those workers; none is simulated here.
 */
#ifndef EM_AREA01_LIGHT_OWNER_H
#define EM_AREA01_LIGHT_OWNER_H

#include "game/em_area01_math_core.h"

int em_area01_light_001C4FA0(EmA01Math *m, uint32_t self, uint32_t *result);

/* sp is the original caller's stack pointer, in the supplied RAM view.
 * The draw worker receives the original local-vector address sp - 0x10.
 * External workers are entered with the owner's original sp - 0x40;
 * supply that through the dispatcher context to stack-aware translations.
 * Return 0 on success, -1 on the existing memory/worker fault contract. */
int em_area01_light_001C50B0(EmA01Math *m, uint32_t self, uint32_t sp);

#endif
