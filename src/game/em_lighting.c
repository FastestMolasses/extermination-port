#include "game/em_lighting.h"
#include "game/em_effect_color.h"

#include <math.h>
#include <string.h>

static float add(float a, float b)
{
    return em_effect_float32((double)a + b);
}

static float multiply(float a, float b)
{
    return em_effect_float32((double)a * b);
}

int em_lighting_matrices(EmLightingMatrices *out, const float bone[16],
                        const float directions[12], const float colors[12],
                        const float ambient[4])
{
    memset(out, 0, sizeof *out);
    for (unsigned column = 0; column < 3; ++column) {
        const float *basis = bone + column*4;
        float square = multiply(basis[0], basis[0]);
        square = add(square, multiply(basis[1], basis[1]));
        square = add(square, multiply(basis[2], basis[2]));
        if (!isfinite(square) || square < 0.0f) return 0;
        /* A suppressed body-head basis is zero; its triangles collapse.
         * Multiplication by the original saturated reciprocal also gives 0. */
        float unit[3] = {0};
        if (square > 0.0f) {
            float length = em_effect_float32(sqrt((double)square));
            float reciprocal = em_effect_float32(1.0 / length);
            for (unsigned axis = 0; axis < 3; ++axis)
                unit[axis] = multiply(basis[axis], reciprocal);
        }
        for (unsigned light = 0; light < 3; ++light) {
            const float *direction = directions + light*4;
            float value = multiply(direction[0], unit[0]);
            value = add(value, multiply(direction[1], unit[1]));
            value = add(value, multiply(direction[2], unit[2]));
            if (!isfinite(value)) return 0;
            out->normal[column*4+light] = value;
        }
    }
    for (unsigned light = 0; light < 3; ++light)
        for (unsigned channel = 0; channel < 3; ++channel) {
            float value = colors[light*4+channel];
            if (!isfinite(value)) return 0;
            out->color[light*4+channel] = value;
        }
    for (unsigned channel = 0; channel < 4; ++channel) {
        if (!isfinite(ambient[channel])) return 0;
        out->color[12+channel] = add(8388608.0f, channel < 3 ? ambient[channel] : 0.0f);
    }
    return 1;
}

void em_lighting_vertex(uint32_t packed_rgba[4], const float normal[3],
                        const EmLightingMatrices *matrices)
{
    float intensity[3];
    for (unsigned light = 0; light < 3; ++light) {
        float value = multiply(matrices->normal[light], normal[0]);
        value = add(value, multiply(matrices->normal[4+light], normal[1]));
        value = add(value, multiply(matrices->normal[8+light], normal[2]));
        intensity[light] = fmaxf(value, 0.0f);
    }
    for (unsigned channel = 0; channel < 4; ++channel) {
        float value = multiply(matrices->color[channel], intensity[0]);
        value = add(value, multiply(matrices->color[4+channel], intensity[1]));
        value = add(value, multiply(matrices->color[8+channel], intensity[2]));
        value = add(value, matrices->color[12+channel]);
        value = fminf(value, 8388863.0f);
        memcpy(&packed_rgba[channel], &value, sizeof value);
    }
}
