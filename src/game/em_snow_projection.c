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

static uint32_t fixed4(float value)
{
    double scaled = (double)value * 16.0;
    if (scaled >= 2147483647.0) return INT32_MAX;
    if (scaled <= -2147483648.0) return (uint32_t)INT32_MIN;
    return (uint32_t)(int32_t)scaled;
}

static int project(const EmSnowProjection *projection,
                   const EmSnowParticle *particle, EmSnowProjected *out,
                   int snow_near_fade)
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
    if (snow_near_fade) {
        em_snow_particles_color(particle->color, clip_w, projection->fog, out->color);
    } else {
        /* 00231770's sprite program shares the projection instructions,
         * but does not apply 00233800's additional min(depth/50,1). */
        for (unsigned component = 0; component < 4; ++component) {
            float value = multiply(particle->color[component], 1.0f / 256.0f);
            out->color[component] = (uint32_t)(int32_t)multiply(value, screen[3]);
        }
    }
    for (unsigned axis = 0; axis < 4; ++axis) {
        out->xyzf[0][axis] = fixed4(add(screen[axis], extent[axis]));
        out->xyzf[1][axis] = fixed4(add(screen[axis], -extent[axis]));
    }
    out->st[0][0] = out->st[0][1] = 0.0f;
    out->st[1][0] = out->st[1][1] = 1.0f;
    return 1;
}

int em_snow_project(const EmSnowProjection *projection,
                    const EmSnowParticle *particle, EmSnowProjected *out)
{
    return project(projection, particle, out, 1);
}

int em_effect_sprite_project(const EmSnowProjection *projection,
                             const EmSnowParticle *particle,
                             EmSnowProjected *out)
{
    return project(projection, particle, out, 0);
}
