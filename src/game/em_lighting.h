#ifndef EM_LIGHTING_H
#define EM_LIGHTING_H

#include <stdint.h>

typedef struct EmLightingMatrices {
    float normal[16]; /* Original bone-local normal -> three light intensities. */
    float color[16];  /* Original VU color matrix, including the 8388608 bias. */
} EmLightingMatrices;

/* The original kernel receives matrices prepared on EE/VU0. This builder
 * covers the three-direction rig of 001D8340 and the 001D8690 color rows.
 * `colors`/`ambient` must already carry the actor RGB product (see
 * em_lighting_actor_rgb); self-glow (actor flag +2 bit 0x40) and dynamic
 * light registration remain caller-side dependencies; they must not be
 * approximated with new lights. */
int em_lighting_matrices(EmLightingMatrices *out, const float bone[16],
                        const float directions[12], const float colors[12],
                        const float ambient[4]);

/* Original face VU0023C5D8..0023C690 (body 0023C878..0023C928). The normal
 * is the authored vertex attribute, without renormalization. Output is the
 * biased float bit pattern submitted to PACKED RGBAQ; each channel's low
 * byte is the GS integer color. */
void em_lighting_vertex(uint32_t packed_rgba[4], const float normal[3],
                        const EmLightingMatrices *matrices);

/* Original 001D8690 color stage: every light color row and the ambient row
 * are multiplied lane-by-lane by the actor RGB at actor+0x80..0x88 (one
 * truncated EE mul.s each) before the 8388608 bias is added. Apply this to
 * the composed rig's three color rows (stride 4, xyz used) and ambient
 * (xyz) before em_lighting_matrices. Identity RGB (1,1,1) is exact. The
 * alpha lanes (actor+0x8C glow) and the 001D89D0 self-glow tail
 * (actor+2 bit 0x40) are not represented; callers with that flag must not
 * use this path. Returns 0 for a non-finite input. */
int em_lighting_actor_rgb(float colors[12], float ambient[4],
                          const float actor_rgb[3]);

/* Original 001D8270: whether 001D8340 folds the dynamic point lights for
 * an actor. `type` is actor byte +3; `model_radius` is the float at +0x20
 * of the actor's model record (actor+0x44). Types 03, 08, 09, 0B, 0D,
 * 15, 16, 17, 3D and 3E never fold; every other type folds only when the
 * radius compares below 30.0 (c.lt.s: NaN does not fold). */
int em_lighting_fold_gate(unsigned type, float model_radius);

#endif
