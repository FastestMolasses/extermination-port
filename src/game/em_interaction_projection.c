#include "game/em_interaction_projection.h"
#include "game/em_effect_color.h"
#include "game/em_item_sdk_math.h"

int em_interaction_projection_publish(EmInteractionProjection *projection,
    const float eye[3], const float target[3])
{
    if (!projection || !eye || !target) return 0;
    float square[3];
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!isfinite(eye[axis]) || !isfinite(target[axis])) return 0;
        float delta = em_effect_float32((double)target[axis] - eye[axis]);
        square[axis] = em_effect_float32((double)delta * delta);
    }
    float sum = em_effect_float32((double)square[0] + square[1]);
    sum = em_effect_float32((double)sum + square[2]);
    float distance = em_item_sdk_sqrt(sum);
    float span = em_effect_float32((double)1.02f * distance);
    span = em_effect_float32(2.0 + span);
    if (!isfinite(distance) || !isfinite(span)) return 0;
    for (unsigned axis = 0; axis < 3; ++axis) projection->center[axis] = target[axis];
    projection->center[3] = 1;
    /* The reference scalar configuration rounds DIV.S to nearest. */
    projection->scale = (float)(16777215.0 / span);
    projection->distance = distance;
    return 1;
}
