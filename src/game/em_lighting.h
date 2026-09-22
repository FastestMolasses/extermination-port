#ifndef EM_LIGHTING_H
#define EM_LIGHTING_H

#include <stdint.h>

typedef struct EmLightingMatrices {
    float normal[16]; /* Original bone-local normal -> three light intensities. */
    float color[16];  /* Original VU color matrix, including the8388608 bias. */
} EmLightingMatrices;

/* The original kernel receives matrices prepared on EE/VU0. This builder
 * covers the ordinary three-direction rig and identity actor RGB used by the
 * opening. Per-actor RGB/self-glow and dynamic light registration remain
 * caller-side dependencies; they must not be approximated with new lights. */
int em_lighting_matrices(EmLightingMatrices *out, const float bone[16],
                        const float directions[12], const float colors[12],
                        const float ambient[4]);

/* Original face VU0023C5D8..0023C690. The normal is the authored vertex
 * attribute, without renormalization. Output is the biased float bit pattern
 * submitted to PACKED RGBAQ; each channel's low byte is the GS integer color. */
void em_lighting_vertex(uint32_t packed_rgba[4], const float normal[3],
                        const EmLightingMatrices *matrices);

#endif
