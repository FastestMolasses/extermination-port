/* Original 001DD980: the camera-dependent render-context publication. */
#ifndef EM_INTERACTION_PROJECTION_H
#define EM_INTERACTION_PROJECTION_H

#include <stdint.h>

/* 001DD980(eye, target) up to its tail call: d = sqrtf(dx*dx + dy*dy +
 * dz*dz) of target - eye (the SDK square root), then 001DD950(&D_008105E0,
 * 2 + 1.02 d, d). The store 001DD950 is the render context's
 * (em_rcl_001DD950, em_render_context_001DD950); this computes the two
 * float registers it receives: f12[0] = 2 + 1.02 d, f12[1] = d (raw bits).
 * The original's 001DD950 reads the camera target D_008105E0, whatever
 * `target` is. Returns 1, or 0 for a non-finite input or result (the
 * callers fault). It does not copy desired camera vectors or run a chase. */
int em_interaction_projection_001DD980(const float eye[3], const float target[3], uint32_t f12[2]);

#endif
