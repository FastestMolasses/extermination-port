#include "game/em_snow_projection.h"
#include "game/em_effect_color.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static float add(float left, float right)
{
    return em_effect_float32((double)left + right);
}

static float multiply(float left, float right)
{
    return em_effect_float32((double)left * right);
}

static void transform(float out[4], const float matrix[16], const float point[4])
{
    for (unsigned row = 0; row < 4; ++row) {
        float value = multiply(matrix[row], point[0]);
        for (unsigned column = 1; column < 4; ++column)
            value = add(value, multiply(matrix[column*4+row], point[column]));
        out[row] = value;
    }
}

void em_snow_projection_matrices(EmSnowProjection *projection,
                                const float original_view[16], float zoom)
{
    /* Original 001D2960 and its 001D2D20 variant selected by 001CD370(0).
     * The 1280x560 clip projection is a guard band, not the GS raster size.
     * Its scalar MUL.S/DIV.S rounding differs from the VU matrix products. */
    float *screen = projection->extent_projection;
    memset(screen, 0, sizeof(projection->extent_projection));
    screen[0] = multiply(0.8f, zoom);
    screen[5] = multiply(0.5f, zoom);
    screen[8] = screen[9] = 2048.0f;
    screen[10] = 0x1.cc9966p-1f; /* original literal 0x3F664CB3 */
    screen[11] = 1.0f;
    screen[14] = 0x1.999998p+20f; /* original literal 0x49CCCCCC */

    float clip[16] = {0};
    clip[0] = (float)((double)zoom / 640.0f);
    clip[5] = (float)((double)zoom / 280.0f);
    /* Fixed near0.1/far16711680 variant. Its captured depth pair is1/-0.2.
     * Do not recompute far-near using the finite VU truncation helper:
     * exponent alignment in the original EE scalar addition differs. */
    clip[10] = 1.0f;
    clip[11] = 1.0f;
    clip[14] = -0.2f;
    for (unsigned column = 0; column < 4; ++column) {
        transform(projection->clip_from_world + column*4, clip, original_view + column*4);
        transform(projection->screen_from_world + column*4, screen, original_view + column*4);
    }
}

static uint32_t fixed4(float value)
{
    double scaled = (double)value * 16.0;
    if (scaled >= 2147483647.0) return INT32_MAX;
    if (scaled <= -2147483648.0) return (uint32_t)INT32_MIN;
    return (uint32_t)(int32_t)scaled;
}

int em_snow_project(const EmSnowProjection *projection,
                    const EmSnowParticle *particle, EmSnowProjected *out)
{
    /* The original matrix chain uses a constant1 for translation even if
     * the scratch particle's W differs. Preserve that operand explicitly. */
    float point[4] = {particle->position[0], particle->position[1],
                      particle->position[2], 1.0f};
    transform(out->clip, projection->clip_from_world, point);
    float boundary = fabsf(out->clip[3]);
    for (unsigned axis = 0; axis < 3; ++axis)
        if (out->clip[axis] > boundary || out->clip[axis] < -boundary)
            return 0;

    float screen[4];
    transform(screen, projection->screen_from_world, point);
    float clip_w = screen[3];
    float reciprocal = em_effect_float32(1.0 / clip_w);
    for (unsigned axis = 0; axis < 3; ++axis)
        screen[axis] = multiply(screen[axis], reciprocal);
    screen[2] = add(screen[2], projection->depth_bias[2]);

    /* VU clears the X/Y lanes of projection column2 before the extent
     * transform. The extent's W still comes from the unmodified column. */
    float extent[4];
    const float *matrix = projection->extent_projection;
    for (unsigned axis = 0; axis < 4; ++axis) {
        float value = multiply(matrix[axis], particle->half_size[0]);
        value = add(value, multiply(matrix[4+axis], particle->half_size[1]));
        value = add(value, multiply(axis < 2 ? 0.0f : matrix[8+axis], clip_w));
        extent[axis] = add(value, matrix[12+axis]);
    }
    reciprocal = em_effect_float32(1.0 / extent[3]);
    extent[0] = multiply(extent[0], reciprocal);
    extent[1] = multiply(extent[1], reciprocal);
    extent[2] = extent[3] = 0.0f;

    screen[3] = add(projection->fog[2], multiply(projection->fog[3], clip_w));
    screen[3] = fmaxf(fminf(screen[3], projection->fog[0]), 0.0f);
    em_snow_particles_color(particle->color, clip_w, projection->fog, out->color);
    for (unsigned axis = 0; axis < 4; ++axis) {
        out->xyzf[0][axis] = fixed4(add(screen[axis], extent[axis]));
        out->xyzf[1][axis] = fixed4(add(screen[axis], -extent[axis]));
    }
    out->st[0][0] = out->st[0][1] = 0.0f;
    out->st[1][0] = out->st[1][1] = 1.0f;
    return 1;
}
