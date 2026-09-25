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
/* The opening's camera track at the camera stage: while the opening owns
 * the camera it samples its bank-0x98 track into the g.cam view (the eye,
 * target, up and zoom; the stand-in for the opening's 0022EEF0 timeline,
 * census L33) and returns 1; 0 when it does not own the camera, -1 when the
 * opening failed. The camera frame commits (em_camera_live.c). */
int em_opening_runtime_camera_sample(void);
int em_opening_runtime_busy(void);
int em_opening_runtime_actors_active(void);
uint32_t em_opening_runtime_half_tick(void);
int em_opening_runtime_failed(void);
void em_opening_runtime_shutdown(void);

#endif
