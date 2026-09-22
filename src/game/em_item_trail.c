#include "game/em_item_trail.h"
#include "game/em_effect_color.h"

#include <string.h>

static float add(float a, float b)
{
    return em_effect_float32((double)a + b);
}

static float multiply(float a, float b)
{
    return em_effect_float32((double)a * b);
}

int em_item_stick_sample(EmItemStick *stick, uint8_t x, uint8_t y, const EmItemMath *math)
{
    if (!stick || !math || !math->sine || !math->cosine || !math->atan2 || !math->sqrt)
        return 0;
    float dx = (float)x - 128.0f, dy = (float)y - 128.0f;
    float magnitude = math->sqrt(math->context, add(multiply(dx, dx), multiply(dy, dy)));
    float angle = math->atan2(math->context, dy, dx);
    float term;
    if (angle >= 0.7853981852531433f && angle < 2.356194496154785f)
        term = math->cosine(math->context, angle);
    else if (angle > -0.7853981852531433f || angle <= -2.356194496154785f)
        term = math->sine(math->context, angle);
    else
        term = math->cosine(math->context, angle);
    term = multiply(term, 128.0f);
    float denominator = math->sqrt(math->context, add(16384.0f, multiply(term, term)));
    /* The captured EE configuration rounds scalar division to nearest. */
    magnitude = (float)((double)magnitude / denominator);
    float scale = 0;
    if (magnitude > 0.25f) {
        scale = multiply(1.5384615659713745f, add(magnitude, -0.25f));
        if (scale > 1.0f)
            scale = 1.0f;
    }
    stick->x = multiply(scale, math->cosine(math->context, angle));
    stick->y = multiply(scale, math->sine(math->context, angle));
    stick->magnitude = scale;
    stick->angle = angle;
    return 1;
}

void em_item_trail_reset(EmItemTrail *trail)
{
    if (trail)
        memset(trail, 0, sizeof *trail);
}

static int fan(float x, float y, float radius, float angle, unsigned intensity,
               const EmItemMath *math, EmItemTrailEmit emit, void *context)
{
    float sine = math->sine(math->context, angle);
    float cosine = math->cosine(math->context, angle);
    float sine_step = math->sine(math->context, 0.09817477f);
    float cosine_step = math->cosine(math->context, 0.09817477f);
    float k = multiply(2, sine_step);
    float a = multiply(radius, cosine), b = multiply(radius, sine);
    float c = add(multiply(a, sine_step), multiply(cosine_step, multiply(28, sine)));
    float d = add(multiply(cosine_step, multiply(28, cosine)), -multiply(b, sine_step));
    int32_t triangle[3][2];
    triangle[0][0] = (int32_t)multiply(16, x);
    triangle[0][1] = (int32_t)multiply(16, y);
    for (unsigned vertex = 0; vertex < 33; ++vertex) {
        triangle[2][0] = (int32_t)multiply(16, add(multiply(0.8f, a), x));
        triangle[2][1] = (int32_t)multiply(16, add(multiply(0.5f, b), y));
        if (vertex && emit(context, triangle, intensity) != 1)
            return 0;
        memcpy(triangle[1], triangle[2], sizeof triangle[1]);
        a = add(a, -multiply(k, c));
        b = add(b, -multiply(k, d));
        c = add(c, multiply(k, a));
        d = add(d, multiply(k, b));
    }
    return 1;
}

int em_item_trail_step(EmItemTrail *trail, const EmItemStick *stick, float x, float y,
                       const EmItemMath *math, EmItemTrailEmit emit, void *context)
{
    if (!trail || !stick || !math || !math->sine || !math->cosine || !emit)
        return 0;
    x = add(1792, x);
    y = add(1824, y);
    trail->cursor = (trail->cursor + 1) & 15;
    trail->slots[trail->cursor] = *stick;
    for (unsigned i = 0; i < 16; ++i) {
        EmItemStick *slot = &trail->slots[i];
        float center_x = add(x, multiply(0.8f, multiply(44, slot->x)));
        float center_y = add(y, multiply(0.5f, multiply(44, slot->y)));
        float radius = add(28, multiply(16, slot->magnitude));
        unsigned intensity = (unsigned)multiply(16, add(1, slot->magnitude));
        if (!fan(center_x, center_y, radius, slot->angle, intensity, math, emit, context))
            return 0;
        slot->magnitude = multiply(slot->magnitude, 0.8f);
    }
    return 1;
}
