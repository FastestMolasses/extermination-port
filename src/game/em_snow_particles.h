#ifndef EM_SNOW_PARTICLES_H
#define EM_SNOW_PARTICLES_H

#include <stdint.h>

typedef struct EmSnowParticle {
    float position[4];       /* Original world coordinates, before projection. */
    float color[4];          /* VU scratch color, before distance/fog attenuation. */
    float half_size[4];
    uint32_t source_index;   /* Retains the descriptor index when age rejects it. */
} EmSnowParticle;

/* Original 00233828 particle generation/trajectory. descriptor is the nine
 * VU qwords 50..58; its final row contains raw integer count/flags/kind bits.
 * lookup holds the 80 original scalar values uploaded to VU 00..4F. params
 * is VU59: phase, color multiplier, fade-in interval, random seed fraction.
 * matrix is VU5A..5D, column-major. No global random state is consumed.
 * Returns the number emitted, or -1 for invalid input/insufficient capacity.
 */
int em_snow_particles_generate(const float descriptor[9][4],
                               const float lookup[80], const float params[4],
                               const float matrix[16], EmSnowParticle *out,
                               unsigned capacity);

/* Original 00234110..00234240 attenuation and FTOI0 conversion. clip_w is
 * the positive projection W; fog is VU7A, supplied by the original renderer.
 * Returns original integer GS channels (not normalized host shader colors).
 */
void em_snow_particles_color(const float color[4], float clip_w,
                             const float fog[4], uint32_t gs_color[4]);

#endif
