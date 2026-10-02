#ifndef EM_LIGHTING_H
#define EM_LIGHTING_H

#include <stdint.h>

typedef struct EmLightingMatrices {
    float normal[16]; /* Original bone-local normal -> three light intensities. */
    float color[16];  /* Original VU color matrix, including the 8388608 bias. */
} EmLightingMatrices;

/* The original kernel receives matrices prepared on EE/VU0. This builder
 * takes the rig as em_gfx.h EmGfxCharRig carries it: the slot directions
 * (A's columns) and the colour and ambient rows of the bound 001D89D0's
 * colour matrix B (the actor RGB product, the glow and the 8388608 bias
 * already applied there; the ambient row here is B's less the bias, which
 * this builder adds back exactly). 001D8270 and 001D8690 have one
 * translation each, in em_actor_light_001D89D0. */
int em_lighting_matrices(EmLightingMatrices *out, const float bone[16],
                        const float directions[12], const float colors[12],
                        const float ambient[4]);

/* Original face VU0023C5D8..0023C690 (body 0023C878..0023C928). The normal
 * is the authored vertex attribute, without renormalization. Output is the
 * biased float bit pattern submitted to PACKED RGBAQ; each channel's low
 * byte is the GS integer color. */
void em_lighting_vertex(uint32_t packed_rgba[4], const float normal[3],
                        const EmLightingMatrices *matrices);

#endif
