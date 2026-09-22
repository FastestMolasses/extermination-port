#include "game/em_interaction_alignment.h"
#include "game/em_effect_color.h"

int em_interaction_alignment(const float matrix[16], float owner_yaw,
    const float point[4], float offset, float ground_y,
    float target[4], float *player_yaw)
{
    if (!matrix || !point || !target || !player_yaw ||
        !isfinite(owner_yaw) || !isfinite(offset) || !isfinite(ground_y))
        return 0;
    for (unsigned i = 0; i < 16; ++i)
        if (!isfinite(matrix[i])) return 0;
    for (unsigned i = 0; i < 4; ++i)
        if (!isfinite(point[i])) return 0;
    /* Original callers pass normalized yaw and an offset no larger than
     * a turn. Bound invalid host inputs before the original wrap loop. */
    if (fabsf(owner_yaw) > 6.2831855f || fabsf(offset) > 6.2831855f)
        return 0;
    float yaw = em_effect_float32((double)owner_yaw + offset);
    while (yaw > 3.1415927f)
        yaw = em_effect_float32((double)yaw - 6.2831855f);
    while (yaw <= -3.1415927f)
        yaw = em_effect_float32((double)yaw + 6.2831855f);
    *player_yaw = yaw;
    for (unsigned row = 0; row < 4; ++row) {
        /* VU MULAx/MADDAy/MADDAz/MADDw: retain even zero terms and
         * each product/add rounding boundary, including homogeneous W. */
        float value = em_effect_float32((double)matrix[row] * point[0]);
        for (unsigned column = 1; column < 4; ++column) {
            float term = em_effect_float32((double)matrix[column * 4 + row] * point[column]);
            value = em_effect_float32((double)value + term);
        }
        target[row] = value;
    }
    target[1] = ground_y;
    return 1;
}
