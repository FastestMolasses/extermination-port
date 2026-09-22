#include "game/em_door_candidate.h"
#include "game/em_effect_color.h"
#include "game/em_item_sdk_math.h"

static float add(float a, float b) { return em_effect_float32((double)a + b); }
static float sub(float a, float b) { return em_effect_float32((double)a - b); }
static float mul(float a, float b) { return em_effect_float32((double)a * b); }
static float wrap(float value)
{
    while (value > 3.1415927f) value = sub(value, 6.2831855f);
    while (value <= -3.1415927f) value = add(value, 6.2831855f);
    return value;
}

int em_door_candidate(const float descriptor[2], const float owner[3], float owner_yaw,
    uint8_t subtype, const EmInteractionPlayer *player, const EmInteractionMath *math,
    float *score)
{
    if (!descriptor || !owner || !player || !math || !score ||
        !isfinite(owner_yaw) || !isfinite(player->yaw) ||
        fabsf(owner_yaw) > 3.1415927f || fabsf(player->yaw) > 6.2831855f ||
        !isfinite(descriptor[0]) || !isfinite(descriptor[1])) return -1;
    if (player->action == 0x2D) return 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!isfinite(owner[axis]) || !isfinite(player->position[axis])) return -1;
    float center_x = owner[0], center_z = owner[2];
    if (subtype == 3 || subtype == 0x15) {
        center_x = sub(center_x, mul(5, em_item_sdk_cosine(owner_yaw)));
        center_z = add(center_z, mul(5, em_item_sdk_sine(owner_yaw)));
    }
    float dx = sub(player->position[0], center_x), dz = sub(player->position[2], center_z);
    float distance = em_item_sdk_sqrt(add(mul(dx, dx), mul(dz, dz)));
    if (!(distance <= descriptor[0])) return 0;
    *score = distance;
    float dy = sub(player->position[1], owner[1]);
    if (!(em_item_sdk_sqrt(mul(dy, dy)) <= descriptor[1])) return 0;
    float bearing = em_interaction_sdk_atan2(math,
        sub(player->position[0], owner[0]), sub(player->position[2], owner[2]));
    float facing = player->yaw;
    if (fabsf(wrap(sub(bearing, owner_yaw))) <= 1.5707964f)
        facing = add(3.1415927f, facing);
    return fabsf(wrap(sub(facing, owner_yaw))) <= 0.7853982f;
}
