#include "game/em_point_light.h"
#include "game/em_effect_color.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(EmPointLight) == 0x80, "original light slot layout");

int em_point_light_load(EmPointLightPool *pool, uint16_t *area_key,
                        const char *path)
{
    struct Record { uint32_t type; float position[4], color[4]; } records[32];
    uint32_t header[4];
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    int valid = fread(header, sizeof header, 1, file) == 1 &&
                memcmp(header, "EMLP", 4) == 0 && header[1] == 1 &&
                header[2] <= 0xffff && header[3] > 0 && header[3] <= 32;
    if (valid) valid = fread(records, sizeof records[0], header[3], file) == header[3]
                       && fgetc(file) == EOF;
    fclose(file);
    if (!valid) return 0;
    for (unsigned i = 0; i < header[3]; ++i) {
        if (records[i].type != 1 || records[i].position[3] != 1.0f ||
            records[i].color[3] <= 0.0f) return 0;
        for (unsigned lane = 0; lane < 4; ++lane)
            if (!isfinite(records[i].position[lane]) ||
                !isfinite(records[i].color[lane])) return 0;
    }
    em_point_light_reset(pool);
    for (unsigned i = 0; i < header[3]; ++i)
        em_point_light_register(pool, records[i].position, records[i].color,
                                1, 1.0f, 0.0f);
    *area_key = (uint16_t)header[2];
    return 1;
}

static float add(float a, float b)
{
    return em_effect_float32((double)a + b);
}

static float multiply(float a, float b)
{
    return em_effect_float32((double)a * b);
}

static void identity(float matrix[16])
{
    memset(matrix, 0, 16 * sizeof *matrix);
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

/* Original 001029E8 polynomial used by 00102B08 / 00102BB0. */
static void rotation(float angle, float *sine, float *cosine)
{
    static const float coefficient[4] = {
        0x1.5d3828p-19f, -0x1.9f643ep-13f,
        0x1.110e7cp-7f, -0x1.555548p-3f
    };
    float argument = add(0x1.921fb6p+0f, -fabsf(angle));
    float square = multiply(argument, argument), term[4];
    for (unsigned i = 0; i < 4; ++i)
        term[i] = multiply(coefficient[i], argument);
    for (unsigned lanes = 4; lanes > 0; --lanes)
        for (unsigned i = 0; i < lanes; ++i)
            term[i] = multiply(term[i], square);
    float value = argument;
    for (unsigned i = 4; i > 0; --i) value = add(value, term[i-1]);
    *cosine = value;
    /* VU0 VSQRT takes |ft| (em_ee_float.h em_vu_sqrt_bits): the polynomial
     * can round 1 - value * value just below zero for a tiny flicker angle,
     * where the original's square root is of the magnitude, not a NaN. */
    *sine = em_effect_float32(sqrt((double)fabsf(add(1.0f, -multiply(value, value)))));
    if (angle < 0.0f) *sine = -*sine;
}

static void flicker_matrix(float matrix[16], const float angle[4])
{
    float sx, cx, sy, cy, x[16], y[16];
    rotation(angle[0], &sx, &cx);
    rotation(angle[1], &sy, &cy);
    identity(x); identity(y);
    x[5] = cx; x[6] = sx; x[9] = -sx; x[10] = cx;
    y[0] = cy; y[2] = -sy; y[8] = sy; y[10] = cy;
    /* The SDK left-multiplies all four columns, including zero terms.
     * Keep that sequence instead of reducing to a trigonometric formula. */
    for (unsigned column = 0; column < 4; ++column)
        for (unsigned row = 0; row < 4; ++row) {
            float value = multiply(y[row], x[column*4]);
            for (unsigned lane = 1; lane < 4; ++lane)
                value = add(value, multiply(y[lane*4+row], x[column*4+lane]));
            matrix[column*4+row] = value;
        }
}

void em_point_light_reset(EmPointLightPool *pool)
{
    pool->next_handle = 0;
    pool->pending_count = 0;
    for (unsigned i = 0; i < EM_POINT_LIGHT_COUNT; ++i) {
        EmPointLight *light = &pool->active[i];
        light->type = 0;
        light->handle = -1;
        memset(light->position, 0, sizeof light->position);
        memset(light->color, 0, sizeof light->color);
    }
}

int32_t em_point_light_register(EmPointLightPool *pool, const float position[4],
                              const float color[4], int32_t type,
                              float multiplier, float adder)
{
    if (pool->pending_count >= EM_POINT_LIGHT_COUNT) return -1;
    EmPointLight *light = &pool->pending[pool->pending_count++];
    memcpy(light->position, position, sizeof light->position);
    memcpy(light->color, color, sizeof light->color);
    light->multiplier = multiplier;
    light->adder = adder;
    light->type = type;
    light->handle = (int32_t)pool->next_handle++;
    return light->handle;
}

void em_point_light_tick(EmPointLightPool *pool, uint16_t area_key,
                         EmPointLightRandom random, void *context)
{
    for (unsigned i = 0; i < EM_POINT_LIGHT_COUNT; ++i) {
        EmPointLight *light = &pool->active[i];
        if (light->color[3] <= 0.0f) continue;
        light->color[3] = fmaxf(0.0f, add(light->adder,
                                      multiply(light->color[3], light->multiplier)));
        for (unsigned channel = 0; channel < 3; ++channel)
            light->color[channel] = multiply(light->color[channel], light->multiplier);
        if (light->multiplier != 1.0f || area_key == 0x0f00 || light->type != 1) {
            identity(light->matrix);
            continue;
        }
        for (unsigned axis = 0; axis < 2; ++axis) {
            float value = em_effect_float32((double)random(context));
            value = multiply(0x1p-31f, value);
            value = add(-0.001f, multiply(0.002f, value));
            value = add(light->angle[axis], value);
            light->angle[axis] = fminf(fmaxf(value, -0.03141593f), 0.03141593f);
        }
        flicker_matrix(light->matrix, light->angle);
    }
    for (int32_t i = 0; i < pool->pending_count; ++i) {
        for (unsigned slot = 0; slot < EM_POINT_LIGHT_COUNT; ++slot) {
            EmPointLight *light = &pool->active[slot];
            if (light->color[3] > 0.0f) continue;
            const EmPointLight *pending = &pool->pending[i];
            light->multiplier = pending->multiplier;
            light->adder = pending->adder;
            memcpy(light->position, pending->position, sizeof light->position);
            for (unsigned channel = 0; channel < 4; ++channel)
                light->color[channel] = multiply(pending->color[channel], 128.0f);
            light->handle = pending->handle;
            light->type = pending->type;
            light->angle[0] = light->angle[1] = light->angle[2] = 0.0f;
            /* Adoption preserves matrix and angle.w, just like 001D7C30. */
            break;
        }
    }
    pool->pending_count = 0;
}

void em_point_light_fold(float dir[4], float color[4],
                        const EmPointLightPool *pool, const float anchor[4])
{
    for (unsigned i = 0; i < EM_POINT_LIGHT_COUNT; ++i) {
        const EmPointLight *light = &pool->active[i];
        if (light->color[3] <= 0.0f) continue;
        float offset[4], scaled[4];
        for (unsigned axis = 0; axis < 4; ++axis)
            offset[axis] = add(light->position[axis], -anchor[axis]);
        float distance = multiply(offset[0], offset[0]);
        distance = add(distance, multiply(offset[1], offset[1]));
        distance = add(distance, multiply(offset[2], offset[2]));
        distance = fmaxf(distance, 1.0f);
        /* EE DIV.S rounds to nearest in the captured renderer; VU
         * reciprocal normalization below retains its truncating path. */
        float strength = (float)((double)multiply(0.1f, light->color[3]) / distance);
        float direction_scale = multiply(10.0f, strength);
        float color_scale = multiply(2.0f, strength);
        for (unsigned axis = 0; axis < 4; ++axis)
            scaled[axis] = multiply(offset[axis], direction_scale);
        for (unsigned axis = 0; axis < 4; ++axis) {
            float value = multiply(light->matrix[axis], scaled[0]);
            for (unsigned column = 1; column < 4; ++column)
                value = add(value, multiply(light->matrix[column*4+axis], scaled[column]));
            dir[axis] = add(dir[axis], value);
            color[axis] = add(color[axis], multiply(light->color[axis], color_scale));
        }
    }
    float square = multiply(dir[0], dir[0]);
    square = add(square, multiply(dir[1], dir[1]));
    square = add(square, multiply(dir[2], dir[2]));
    float length = em_effect_float32(sqrt((double)square));
    float reciprocal = length > 0.0f ? em_effect_float32(1.0 / length) : 0.0f;
    for (unsigned axis = 0; axis < 3; ++axis) dir[axis] = multiply(dir[axis], reciprocal);
    dir[3] = 0.0f;
}
