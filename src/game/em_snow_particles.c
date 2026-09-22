#include "game/em_snow_particles.h"

#include <math.h>
#include <string.h>

static uint32_t float_bits(float value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof result);
    return result;
}

static float from_bits(uint32_t bits)
{
    float result;
    memcpy(&result, &bits, sizeof result);
    return result;
}

/* Preserve the VU operation order and finite binary32 truncation. This covers
 * the recovered particle inputs; exceptional VU arithmetic is not emulated.
 * It also prevents a host compiler from contracting multiply/add into FMA.
 */
static float vu_float(double value)
{
    float result = (float)value;
    if (fabs((double)result) > fabs(value))
        result = from_bits(float_bits(result) - 1);
    return result;
}

static float multiply(float left, float right)
{
    return vu_float((double)left * right);
}

static float add(float left, float right)
{
    return vu_float((double)left + right);
}

/* Sony VU RANDU: the 23-bit mantissa follows x^23 + x^5 + 1. RINIT and
 * RXOR use only source mantissa bits; RNEXT supplies sign/exponent for 1.x.
 * This sequence has been checked against captured original VU particle data.
 */
static float random_next(uint32_t *state)
{
    uint32_t feedback = ((*state >> 22) ^ (*state >> 4)) & 1;
    *state = ((*state << 1) | feedback) & 0x7fffff;
    return from_bits(*state | 0x3f800000);
}

static unsigned random_angle(uint32_t *state)
{
    float first = random_next(state);
    float second = random_next(state);
    /* The original evaluates (-63) + (first * 63), not (first-1)*63. */
    unsigned index = (unsigned)add(-63.0f, multiply(first, 63.0f));
    *state ^= float_bits(multiply(first, second)) & 0x7fffff;
    return index;
}

static float clamp_channel(float value)
{
    return fminf(fmaxf(value, 0.0f), 255.0f);
}

void em_snow_particles_color(const float color[4], float clip_w,
                             const float fog[4], uint32_t gs_color[4])
{
    float fog_weight = add(fog[2], multiply(fog[3], clip_w));
    fog_weight = fmaxf(fminf(fog_weight, fog[0]), 0.0f);
    float near_weight = fminf(multiply(clip_w, 0.02f), 1.0f);
    for (unsigned c = 0; c < 4; ++c) {
        float value = multiply(color[c], 0.00390625f);
        value = multiply(value, fog_weight);
        value = multiply(value, near_weight);
        gs_color[c] = (uint32_t)(int32_t)value;
    }
}

int em_snow_particles_generate(const float descriptor[9][4],
                               const float lookup[80], const float params[4],
                               const float matrix[16], EmSnowParticle *out,
                               unsigned capacity)
{
    if (!descriptor || !lookup || !params || !matrix || !out)
        return -1;
    unsigned count = float_bits(descriptor[8][0]);
    unsigned flags = float_bits(descriptor[8][2]);
    if (!count || count > 32767 || capacity < count ||
        !isfinite(params[0]) || params[2] <= 0.0f ||
        descriptor[6][1] == descriptor[6][0])
        return -1;

    uint32_t random = float_bits(params[3]) & 0x7fffff;
    float step = vu_float(1.0 / count);
    float fraction = 0.0f;
    float color_start[4], color_end[4];
    for (unsigned c = 0; c < 4; ++c) {
        color_start[c] = multiply(descriptor[2][c], params[1]);
        color_end[c] = multiply(descriptor[3][c], params[1]);
    }

    unsigned emitted = 0;
    for (unsigned i = 0; i < count; ++i) {
        fraction = add(fraction, step);
        unsigned first = random_angle(&random);
        unsigned second = random_angle(&random);
        float elapsed = add(params[0], -multiply(fraction, descriptor[6][2]));
        /* FMAND reads Sx for elapsed, then Sw for life-elapsed. Both RNG
         * angle draws occur even when these age checks reject the particle. */
        if (elapsed < 0.0f || add(descriptor[6][3], -elapsed) < 0.0f)
            continue;
        float age = add(elapsed, -(float)(int)elapsed);
        float velocity[3], offset[3];
        memcpy(velocity, descriptor[0], sizeof velocity);
        memcpy(offset, descriptor[1], sizeof offset);
        velocity[0] = multiply(velocity[0], lookup[first + 16]);
        offset[2] = multiply(offset[2], lookup[first]);
        offset[0] = multiply(offset[0], lookup[first + 16]);
        if (!(flags & 0x10))
            velocity[2] = multiply(velocity[2], lookup[first]);
        velocity[0] = multiply(velocity[0], lookup[second]);
        velocity[1] = multiply(velocity[1], lookup[second + 16]);
        if (!(flags & 0x10))
            velocity[2] = multiply(velocity[2], lookup[second]);
        offset[1] = multiply(offset[1], lookup[second + 16]);
        if (!(flags & 8))
            for (unsigned c = 0; c < 3; ++c)
                velocity[c] = multiply(velocity[c], fraction);
        if (flags & 1) velocity[1] = fabsf(velocity[1]);
        if (flags & 2) velocity[2] = fabsf(velocity[2]);
        if (!(flags & 4)) memset(offset, 0, sizeof offset);

        /* VU57.z is used as a trajectory time offset despite sharing the
         * qword with TEX0. The recovered snow descriptor supplies zero. */
        float time = add(age, descriptor[7][2]);
        float local[3];
        for (unsigned c = 0; c < 3; ++c)
            local[c] = add(multiply(velocity[c], time), offset[c]);
        EmSnowParticle *particle = &out[emitted++];
        for (unsigned c = 0; c < 4; ++c) {
            float value = multiply(matrix[c], local[0]);
            value = add(value, multiply(matrix[4+c], local[1]));
            value = add(value, multiply(matrix[8+c], local[2]));
            particle->position[c] = add(value, matrix[12+c]);
        }
        float gravity = multiply(multiply(time, time), descriptor[8][1]);
        particle->position[1] = add(particle->position[1], gravity);

        float blend = vu_float((double)add(age, -descriptor[6][0]) /
                               add(descriptor[6][1], -descriptor[6][0]));
        float fade = fminf(vu_float((double)age / params[2]), 1.0f);
        for (unsigned c = 0; c < 4; ++c) {
            float color = add(color_start[c],
                multiply(add(color_end[c], -color_start[c]), blend));
            float size = add(descriptor[4][c],
                multiply(add(descriptor[5][c], -descriptor[4][c]), blend));
            particle->color[c] = multiply(clamp_channel(color), fade);
            particle->half_size[c] = clamp_channel(size);
        }
        particle->source_index = i;
    }
    return (int)emitted;
}
