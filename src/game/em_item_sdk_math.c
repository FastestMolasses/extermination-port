#include "game/em_item_sdk_math.h"
#include "game/em_effect_color.h"

#include <math.h>
#include <string.h>

static uint32_t bits(float value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof result);
    return result;
}

static float number(uint32_t value)
{
    float result;
    memcpy(&result, &value, sizeof result);
    return result;
}

static float add(float a, float b)
{
    return em_effect_float32((double)a + b);
}

static float subtract(float a, float b)
{
    return em_effect_float32((double)a - b);
}

static float multiply(float a, float b)
{
    return em_effect_float32((double)a * b);
}

/* 0011D770. Retain separate EE scalar operations and the original tail path. */
static float sine_kernel(float x, float tail, int reduced)
{
    if ((bits(x) & 0x7fffffff) <= 0x31ffffff)
        return x;
    float z = multiply(x, x);
    float v = multiply(z, x);
    float r = add(multiply(z, 1.589691e-10f), -2.505076e-8f);
    r = add(multiply(z, r), 0.0000027557314f);
    r = add(multiply(z, r), -0.0001984127f);
    r = add(multiply(z, r), 0.008333334f);
    if (!reduced)
        return add(x, multiply(v, add(multiply(z, r), -0.16666667f)));
    float correction = subtract(multiply(tail, 0.5f), multiply(v, r));
    correction = subtract(multiply(z, correction), tail);
    correction = subtract(correction, multiply(v, -0.16666667f));
    return subtract(x, correction);
}

/* 0011CCC8. q separates the leading subtraction where cancellation matters. */
static float cosine_kernel(float x, float tail)
{
    uint32_t absolute = bits(x) & 0x7fffffff;
    if (absolute <= 0x31ffffff)
        return 1.0f;
    float z = multiply(x, x);
    float r = add(multiply(z, -1.1359648e-11f), 2.0875723e-9f);
    r = add(multiply(z, r), -0.00000027557314f);
    r = add(multiply(z, r), 0.000024801588f);
    r = add(multiply(z, r), -0.0013888889f);
    r = multiply(z, add(multiply(z, r), 0.041666668f));
    float correction = subtract(multiply(z, r), multiply(x, tail));
    if (absolute <= 0x3e999999)
        return subtract(1.0f, subtract(multiply(z, 0.5f), correction));
    float q = number(absolute > 0x3f480000 ? 0x3e900000 : absolute - 0x01000000);
    return subtract(subtract(1.0f, q), subtract(subtract(multiply(z, 0.5f), q), correction));
}

/* 0011C7B0's bounded UI domain. Its large-argument multiword reducer is
 * unreachable for an atan2 angle and is deliberately not approximated. */
static int reduce_angle(float x, float *high, float *low)
{
    const float first = 0x1.921fp+0f, first_tail = 0x1.6a8886p-17f;
    const float second = 0x1.6a88p-17f, second_tail = 0x1.0b461p-34f;
    const float third = 0x1.0b46p-34f, third_tail = 0x1.1a6264p-54f;
    uint32_t absolute = bits(x) & 0x7fffffff;
    if (absolute <= 0x3f490fd8) {
        *high = x;
        *low = 0;
        return 0;
    }
    if (absolute < 0x4016cbe4) {
        float sign = x < 0 ? -1.0f : 1.0f;
        float z = subtract(x, sign * first);
        if ((absolute & 0xfffffff0) != 0x3fc90fd0) {
            *high = subtract(z, sign * first_tail);
            *low = subtract(subtract(z, *high), sign * first_tail);
        } else {
            z = subtract(z, sign * second);
            *high = subtract(z, sign * second_tail);
            *low = subtract(subtract(z, *high), sign * second_tail);
        }
        return x < 0 ? -1 : 1;
    }
    float magnitude = number(absolute);
    int quadrant = (int)add(multiply(magnitude, 0x1.45f308p-1f), 0.5f);
    float n = (float)quadrant;
    float r = subtract(magnitude, multiply(n, first));
    float w = multiply(n, first_tail);
    *high = subtract(r, w);
    /* In this domain n is2, and original26C490[1] is0x40490f00. */
    if ((absolute & 0xffffff00) == 0x40490f00) {
        int exponent = (int)(absolute >> 23);
        if (exponent - (int)((bits(*high) >> 23) & 255) > 8) {
            float previous = r;
            w = multiply(n, second);
            r = subtract(previous, w);
            w = subtract(multiply(n, second_tail), subtract(subtract(previous, r), w));
            *high = subtract(r, w);
            if (exponent - (int)((bits(*high) >> 23) & 255) > 25) {
                previous = r;
                w = multiply(n, third);
                r = subtract(previous, w);
                w = subtract(multiply(n, third_tail), subtract(subtract(previous, r), w));
                *high = subtract(r, w);
            }
        }
    }
    *low = subtract(subtract(r, *high), w);
    if (x < 0) {
        *high = -*high;
        *low = -*low;
        return -quadrant;
    }
    return quadrant;
}

float em_item_sdk_sine(float angle)
{
    uint32_t absolute = bits(angle) & 0x7fffffff;
    if (absolute > 0x40490fdb)
        return NAN;
    if (absolute <= 0x3f490fd8)
        return sine_kernel(angle, 0, 0);
    float high, low;
    int quadrant = reduce_angle(angle, &high, &low) & 3;
    if (quadrant == 0)
        return sine_kernel(high, low, 1);
    if (quadrant == 1)
        return cosine_kernel(high, low);
    if (quadrant == 2)
        return -sine_kernel(high, low, 1);
    return -cosine_kernel(high, low);
}

float em_item_sdk_cosine(float angle)
{
    uint32_t absolute = bits(angle) & 0x7fffffff;
    if (absolute > 0x40490fdb)
        return NAN;
    if (absolute <= 0x3f490fd8)
        return cosine_kernel(angle, 0);
    float high, low;
    int quadrant = reduce_angle(angle, &high, &low) & 3;
    if (quadrant == 0)
        return cosine_kernel(high, low);
    if (quadrant == 1)
        return -sine_kernel(high, low, 1);
    if (quadrant == 2)
        return -cosine_kernel(high, low);
    return sine_kernel(high, low, 1);
}

/* 0011CB90's integer digit-by-digit square root, including final tie-even
 * correction. The UI's0011E748 wrapper takes this nonnegative path. */
float em_item_sdk_sqrt(float value)
{
    uint32_t input = bits(value);
    if (!(input & 0x7fffffff))
        return value;
    if (input >= 0x7f800000)
        return NAN;
    int exponent = (int)(input >> 23);
    if (!exponent) {
        unsigned shifts = 0;
        while (!(input & 0x00800000)) {
            input <<= 1;
            ++shifts;
        }
        exponent = 1 - (int)shifts;
    }
    exponent -= 127;
    input = (input & 0x007fffff) | 0x00800000;
    if (exponent & 1)
        input += input;
    /* Explicit floor division avoids implementation-defined signed shifts. */
    exponent = exponent >= 0 ? exponent / 2 : -((-exponent + 1) / 2);
    input += input;
    uint32_t root = 0, partial = 0;
    for (uint32_t digit = 0x01000000; digit; digit >>= 1) {
        uint32_t trial = partial + digit;
        if (trial <= input) {
            partial = trial + digit;
            input -= trial;
            root += digit;
        }
        input += input;
    }
    if (input)
        root += root & 1;
    return number((root >> 1) + 0x3f000000 + (uint32_t)(exponent * 0x00800000));
}

static float sine_worker(void *context, float value)
{
    (void)context;
    return em_item_sdk_sine(value);
}

static float cosine_worker(void *context, float value)
{
    (void)context;
    return em_item_sdk_cosine(value);
}

static float sqrt_worker(void *context, float value)
{
    (void)context;
    return em_item_sdk_sqrt(value);
}

static float atan_worker(void *context, float y, float x)
{
    EmItemSdkMath *state = context;
    if (x == 0 && y == 0) {
        /* Original mode1 wrapper0011E620 records EDOM and returns positive0. */
        state->error = 0x21;
        return 0;
    }
    return em_interaction_sdk_atan2(&state->atan, y, x);
}

int em_item_sdk_math_bind(EmItemSdkMath *state, const EmInteractionMath *atan, EmItemMath *workers)
{
    if (!state || !atan || !workers)
        return 0;
    state->atan = *atan;
    state->error = 0;
    *workers = (EmItemMath){state, sine_worker, cosine_worker, atan_worker, sqrt_worker};
    return 1;
}
