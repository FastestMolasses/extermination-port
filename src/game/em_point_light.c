#include "game/em_point_light.h"
#include "game/em_effect_color.h"
#include "game/em_ee_float.h"
#include "game/em_owner_services_original.h"

#include <math.h>
#include <string.h>

_Static_assert(sizeof(EmPointLight) == 0x80, "original light slot layout");

/* VU0 macro sums and products of the fold (truncated per operation). The
 * COP1 sites (traced from the original: 001D7C80, 001D7D8C/90, 001D7E1C/20
 * in the tick and 001D85AC..001D85FC in the fold) use em_ee_float.h; the
 * flicker matrix is the one bound translation of 001029C0 / 00102B08 /
 * 00102BB0 (em_owner_services_original). */
static float add(float a, float b)
{
    return em_effect_float32((double)a + b);
}

static float multiply(float a, float b)
{
    return em_effect_float32((double)a * b);
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

static uint32_t word_of(float value)
{
    uint32_t word;
    memcpy(&word, &value, sizeof word);
    return word;
}

int em_point_light_tick(EmPointLightPool *pool, uint16_t area_key,
                        EmPointLightRandom random, void *context)
{
    for (unsigned i = 0; i < EM_POINT_LIGHT_COUNT; ++i) {
        EmPointLight *light = &pool->active[i];
        if (light->color[3] <= 0.0f) continue;
        /* 001D7C30's own arithmetic is COP1 (mul.s / add.s / cvt.s.w). */
        light->color[3] = fmaxf(0.0f, em_ee_add(light->adder,
                                      em_ee_mul(light->color[3], light->multiplier)));
        for (unsigned channel = 0; channel < 3; ++channel)
            light->color[channel] = em_ee_mul(light->color[channel], light->multiplier);
        if (light->multiplier != 1.0f || area_key == 0x0f00 || light->type != 1) {
            int status = em_owner_services_identity_001029C0(light->matrix);
            if (status != EM_EE_FLOAT_OK) return status;
            continue;
        }
        for (unsigned axis = 0; axis < 2; ++axis) {
            float value = em_ee_cvt_s_w((int32_t)random(context));
            value = em_ee_mul(0x1p-31f, value);
            value = em_ee_add(-0.001f, em_ee_mul(0.002f, value));
            value = em_ee_add(light->angle[axis], value);
            light->angle[axis] = fminf(fmaxf(value, -0.03141593f), 0.03141593f);
        }
        /* 001029C0(scratch); 00102B08(scratch, scratch, +0x30);
         * 00102BB0(+0x40, scratch, +0x34). */
        float scratch[16];
        int status = em_owner_services_identity_001029C0(scratch);
        if (status == EM_EE_FLOAT_OK)
            status = em_owner_services_rotate_x_00102B08(scratch, scratch, word_of(light->angle[0]));
        if (status == EM_EE_FLOAT_OK)
            status = em_owner_services_rotate_y_00102BB0(light->matrix, scratch, word_of(light->angle[1]));
        if (status != EM_EE_FLOAT_OK) return status;
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
    return EM_EE_FLOAT_OK;
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
        /* COP1 (001D85AC..001D85FC): EE DIV.S rounds to nearest in the
         * captured renderer; VU reciprocal normalization below retains its
         * truncating path. */
        float strength = em_ee_div(em_ee_mul(0.1f, light->color[3]), distance);
        float direction_scale = em_ee_mul(10.0f, strength);
        float color_scale = em_ee_mul(2.0f, strength);
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
