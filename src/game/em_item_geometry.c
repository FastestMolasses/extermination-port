#include "game/em_item_geometry.h"
#include "game/em_effect_color.h"
#include "game/em_item_sdk_math.h"

#include <math.h>

static float add(float a, float b)
{
    return em_effect_float32((double)a + b);
}

static float multiply(float a, float b)
{
    return em_effect_float32((double)a * b);
}

int em_item_geometry_arc(const float descriptor[24], EmItemVertex *vertices, size_t capacity,
                         size_t *count)
{
    if (!descriptor || !vertices || !count)
        return 0;
    for (unsigned i = 0; i < 24; ++i) {
        if (!isfinite(descriptor[i]))
            return 0;
    }
    if (fabsf(descriptor[0]) > 1000000 || fabsf(descriptor[1]) > 1000000 ||
        fabsf(descriptor[2]) > 700 || fabsf(descriptor[3]) > 700)
        return 0;
    for (unsigned i = 4; i < 8; ++i) {
        if (fabsf(descriptor[i]) > 1024)
            return 0;
    }
    for (unsigned i = 8; i < 24; ++i) {
        if (descriptor[i] < 0 || descriptor[i] > 255)
            return 0;
    }

    float angle = descriptor[2];
    float span = add(descriptor[3], -angle) / 16.0f;
    int whole = (int)span;
    int columns = whole < 0 ? -whole : whole;
    size_t pairs = (size_t)columns + 2;
    if (capacity < pairs * 2)
        return 0;
    float remainder = multiply(add(span, -(float)whole), 16.0f);
    float reciprocal = em_effect_float32(1.0 / (float)pairs);
    float colors[2][4], deltas[2][4];
    for (unsigned side = 0; side < 2; ++side) {
        for (unsigned channel = 0; channel < 4; ++channel) {
            unsigned at = 8 + side * 4 + channel;
            colors[side][channel] = descriptor[at];
            deltas[side][channel] = multiply(add(descriptor[at + 8], -descriptor[at]), reciprocal);
        }
    }
    for (size_t i = 0; i < pairs; ++i) {
        float radians = multiply(3.1415927410125732f, angle) / 180.0f;
        float sine = em_item_sdk_sine(radians);
        float cosine = em_item_sdk_cosine(radians);
        if (!isfinite(sine) || !isfinite(cosine))
            return 0;
        for (unsigned side = 0; side < 2; ++side) {
            float x = multiply(multiply(16.0f, descriptor[4 + side * 2]), sine);
            float y = multiply(multiply(16.0f, descriptor[5 + side * 2]), cosine);
            int32_t px = (int32_t)add(descriptor[0], multiply(0.8f, x));
            int32_t py = (int32_t)add(descriptor[1], multiply(0.5f, y));
            uint32_t rgba = 0;
            for (unsigned channel = 0; channel < 4; ++channel) {
                rgba |= (uint32_t)(int32_t)colors[side][channel] << (channel * 8);
                colors[side][channel] = add(colors[side][channel], deltas[side][channel]);
            }
            /* Original packed XYZ2 write truncates each screen coordinate. */
            uint32_t packed = (uint32_t)px | ((uint32_t)py << 16);
            vertices[i * 2 + side] = (EmItemVertex){rgba, packed & 65535, packed >> 16};
        }
        angle = add(angle, i == (size_t)columns ? remainder : 16.0f);
    }
    *count = pairs * 2;
    return 1;
}
