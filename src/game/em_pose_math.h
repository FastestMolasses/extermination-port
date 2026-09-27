#ifndef EM_POSE_MATH_H
#define EM_POSE_MATH_H
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "game/em_ee_float.h"

/* The EE FPU (COP1) arithmetic of the pose, script and owner translations:
 * the measured EE model of em_ee_float.h (docs/EE_FLOAT_MODEL.md: the
 * pre-trimmed truncating sum, the truncated product, the round-to-nearest
 * quotient, with DAZ, FTZ and saturation). pose_scalar / pose_trim remain
 * for a host double that already holds an exact value. */
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
    return em_ee_add(a, b);
}
static inline float pose_sub(float a, float b)
{
    return em_ee_sub(a, b);
}
static inline float pose_mul(float a, float b)
{
    return em_ee_mul(a, b);
}
static inline float pose_div(float a, float b)
{
    return em_ee_div(a, b);
}
/* MUL.S then ADD.S / SUB.S (the pose decoder's key steps). A translated
 * MADD.S / MSUB.S through the FPU accumulator uses em_ee_madd / em_ee_msub
 * (its product is not saturated). */
static inline float pose_madd(float accumulator, float a, float b)
{
    return em_ee_add(accumulator, em_ee_mul(a, b));
}
static inline float pose_msub(float accumulator, float a, float b)
{
    return em_ee_sub(accumulator, em_ee_mul(a, b));
}
#endif
