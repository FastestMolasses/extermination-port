#include "game/em_camera_rotation.h"
#include "game/em_effect_color.h"
#include <string.h>

static float multiply(float a, float b)
{
    return em_effect_float32((double)a * b);
}

static float add(float a, float b)
{
    return em_effect_float32((double)a + b);
}

/* 001029E8's four odd polynomial terms. Its callers transform the input
 * to pi/2-|angle|; the other component comes from the original VU square
 * root relationship. Each multiply and addition has its own rounding. */
static void components(float angle, float *sine, float *cosine)
{
    static const float coefficient[4] = {
        0x1.5d3828p-19f, -0x1.9f643ep-13f,
        0x1.110e7cp-7f, -0x1.555548p-3f
    };
    float argument = add(0x1.921fb6p+0f, -fabsf(angle));
    float square = multiply(argument, argument);
    float term[4];
    for (unsigned i = 0; i < 4; ++i)
        term[i] = multiply(coefficient[i], argument);
    for (unsigned lanes = 4; lanes > 0; --lanes)
        for (unsigned i = 0; i < lanes; ++i)
            term[i] = multiply(term[i], square);
    float value = argument;
    for (unsigned i = 4; i > 0; --i)
        value = add(value, term[i - 1]);
    *cosine = value;
    *sine = em_effect_float32(sqrt(fabs((double)add(1.0f, -multiply(value, value)))));
    if (angle < 0.0f)
        *sine = -*sine;
}

static void rotate_rows(float matrix[16], unsigned first, unsigned second,
                         float angle)
{
    /* The SDK's zero-angle branch leaves the input matrix alone. Evaluating
     * its approximate polynomial at zero would not produce exact identity. */
    if (angle == 0.0f)
        return;
    float sine, cosine;
    components(angle, &sine, &cosine);
    float rotation[16] = {0};
    float input[16];
    rotation[0] = rotation[5] = rotation[10] = rotation[15] = 1.0f;
    rotation[first * 4 + first] = cosine;
    rotation[second * 4 + first] = -sine;
    rotation[first * 4 + second] = sine;
    rotation[second * 4 + second] = cosine;
    memcpy(input, matrix, sizeof input);
    for (unsigned column = 0; column < 4; ++column) {
        for (unsigned row = 0; row < 4; ++row) {
            float value = multiply(rotation[row], input[column * 4]);
            for (unsigned lane = 1; lane < 4; ++lane)
                value = add(value, multiply(rotation[lane * 4 + row],
                                            input[column * 4 + lane]));
            matrix[column * 4 + row] = value;
        }
    }
}

int em_camera_rotation_offset(const float angles[3], float distance,
    float matrix[16], float offset[4])
{
    if (!angles || !matrix || !offset || !isfinite(distance))
        return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (!isfinite(angles[i]) || fabsf(angles[i]) > 0x1.921fb6p+1f)
            return 0;
    memset(matrix, 0, 16 * sizeof *matrix);
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
    rotate_rows(matrix, 0, 1, angles[2]);
    rotate_rows(matrix, 2, 0, angles[1]);
    rotate_rows(matrix, 1, 2, angles[0]);
    /* 001026A0 includes every homogeneous component, including the zeros. */
    for (unsigned row = 0; row < 4; ++row) {
        float value = multiply(matrix[row], 0.0f);
        value = add(value, multiply(matrix[4 + row], 0.0f));
        value = add(value, multiply(matrix[8 + row], distance));
        offset[row] = add(value, matrix[12 + row]);
    }
    return 1;
}
