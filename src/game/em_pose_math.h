#ifndef EM_POSE_MATH_H
#define EM_POSE_MATH_H
#include <math.h>
#include <stdint.h>
#include <string.h>

/* Finite animation arithmetic for the original reference execution.
 * The saved PCSX2 configuration uses ordinary truncation, nearest division,
 * and the EE add/sub single-guard-bit model. These helpers are deliberately
 * scoped to finite pose channels; they are not a complete EE FPU emulator. */
static inline float pose_scalar(double value)
{
    float result = (float)value;
    return fabs((double)result) > fabs(value) ? nextafterf(result, 0) : result;
}
static inline float pose_trim(float value, unsigned difference)
{
    uint32_t raw;
    memcpy(&raw, &value, 4);
    raw &= difference >= 25 ? UINT32_C(0x80000000) : UINT32_MAX << (difference - 1);
    memcpy(&value, &raw, 4);
    return value;
}
static inline float pose_add(float a, float b)
{
    uint32_t aa, bb;
    memcpy(&aa, &a, 4);
    memcpy(&bb, &b, 4);
    int difference = (int)((aa >> 23) & 255) - (int)((bb >> 23) & 255);
    if (difference > 0) b = pose_trim(b, (unsigned)difference);
    else if (difference < 0) a = pose_trim(a, (unsigned)-difference);
    return pose_scalar((double)a + b);
}
static inline float pose_sub(float a, float b)
{
    return pose_add(a, -b);
}
static inline float pose_mul(float a, float b)
{
    return pose_scalar((double)a * b);
}
static inline float pose_div(float a, float b)
{
    return (float)((double)a / b);
}
static inline float pose_madd(float accumulator, float a, float b)
{
    return pose_add(accumulator, pose_mul(a, b));
}
static inline float pose_msub(float accumulator, float a, float b)
{
    return pose_sub(accumulator, pose_mul(a, b));
}
#endif
