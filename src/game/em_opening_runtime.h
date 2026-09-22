/* Native services for the original AREA11 0x00823E80 opening actor.
 * The controller and its 64-byte instructions remain in the pure
 * em_area11_opening / em_script modules. */
#ifndef EM_OPENING_RUNTIME_H
#define EM_OPENING_RUNTIME_H

#include <stdint.h>

void em_opening_runtime_request(void);
/* Called after New Game's scene and ordinary player resources load. */
void em_opening_runtime_scene_ready(void);
/* Original actor-pool phase; exactly once per ordinary game frame. */
void em_opening_runtime_tick(void);
/* Original camera phase, after actors. Returns 1 if it committed camera. */
int em_opening_runtime_camera(void);
int em_opening_runtime_busy(void);
int em_opening_runtime_actors_active(void);
uint32_t em_opening_runtime_half_tick(void);
int em_opening_runtime_failed(void);
void em_opening_runtime_shutdown(void);

#endif
