#include "game/em_interaction_projection.h"
#include "game/em_ee_float.h"
#include "game/em_effect_color.h"
#include "game/em_item_sdk_math.h"

#include <string.h>

int em_interaction_projection_001DD980(const float eye[3], const float target[3], uint32_t f12[2])
{
    if (!eye || !target || !f12) return 0;
    float square[3];
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!isfinite(eye[axis]) || !isfinite(target[axis])) return 0;
        /* 001DD980's arithmetic is COP1 (em_ee_float.h, the measured EE
         * model; its accumulator sums equal these for finite values). */
        float delta = em_ee_sub(target[axis], eye[axis]);
        square[axis] = em_ee_mul(delta, delta);
    }
    float sum = em_ee_add(square[0], square[1]);
    sum = em_ee_add(sum, square[2]);
    float distance = em_item_sdk_sqrt(sum);
    float span = em_ee_mul(1.02f, distance);
    span = em_ee_add(2.0f, span);
    if (!isfinite(distance) || !isfinite(span)) return 0;
    memcpy(&f12[0], &span, 4);
    memcpy(&f12[1], &distance, 4);
    return 1;
}
