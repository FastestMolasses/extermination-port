#include "game/em_door_transit.h"
#include "game/em_effect_color.h"
#include "game/em_item_sdk_math.h"

#include <string.h>

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

static float wrap(float angle)
{
    while (angle > 0x1.921fb6p+1f) angle = subtract(angle, 0x1.921fb6p+2f);
    while (angle <= -0x1.921fb6p+1f) angle = add(angle, 0x1.921fb6p+2f);
    return angle;
}

int em_door_transit_prepare(EmDoorTransitPlan *out, const float origin[3], float yaw,
    const float player[3], const uint16_t sounds[2], int locked,
    const EmInteractionMath *math)
{
    if (!out || !origin || !player || !sounds || !math || !isfinite(yaw) ||
        fabsf(yaw) > 0x1.921fb6p+1f || (locked != 0 && locked != 1))
        return 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!isfinite(origin[axis]) || !isfinite(player[axis])) return 0;
    float bearing = em_interaction_sdk_atan2(math,
        subtract(player[0], origin[0]), subtract(player[2], origin[2]));
    if (!isfinite(bearing)) return 0;
    EmDoorTransitPlan plan = {0};
    plan.locked = locked;
    plan.side = fabsf(wrap(subtract(bearing, yaw))) <= 0x1.921fb6p+0f ? 0 : 1;
    plan.player_yaw = wrap(plan.side ? yaw : add(0x1.921fb6p+1f, yaw));
    if (locked) {
        plan.script_entry = 0x24dec0;
        plan.player_clip = plan.side ? 0x44 : 0x46;
        plan.door_clip = plan.side ? 1 : 3;
    } else {
        plan.script_entry = 0x24de40;
        plan.player_clip = plan.side ? 0x43 : 0x45;
        plan.door_clip = plan.side ? 0 : 2;
        plan.wait_ticks = plan.side ? 70 : 90;
        plan.sound = sounds[plan.side];
    }
    plan.position[0] = subtract(origin[0], multiply(5, em_item_sdk_cosine(yaw)));
    plan.position[1] = player[1];
    plan.position[2] = add(origin[2], multiply(5, em_item_sdk_sine(yaw)));
    plan.position[3] = 1;
    plan.position[0] = subtract(plan.position[0], multiply(5, em_item_sdk_sine(plan.player_yaw)));
    plan.position[2] = subtract(plan.position[2], multiply(5, em_item_sdk_cosine(plan.player_yaw)));
    *out = plan;
    return 1;
}

int em_door_transit_kickoff(EmDoorOriginal *door, float yaw,
    const float player[3], const uint16_t sounds[2], int locked,
    const EmInteractionMath *math, const EmDoorTransitHooks *hooks)
{
    if (!door) return -1;
    if (!(door->armed & 4)) return 0;
    if (!hooks || !hooks->patch || !hooks->face_player || !hooks->align_player ||
        !hooks->script_start || !hooks->script_tick)
        return -1;
    EmDoorTransitPlan plan;
    if (!em_door_transit_prepare(&plan, door->origin, yaw, player, sounds, locked, math))
        return -1;
    door->side = (int16_t)plan.side;
    if (hooks->patch(hooks->context, &plan) != 1 ||
        hooks->face_player(hooks->context, plan.player_yaw) != 1 ||
        hooks->align_player(hooks->context, plan.position) != 1 ||
        hooks->script_start(hooks->context, plan.script_entry) != 1)
        return -1;
    int result = hooks->script_tick(hooks->context);
    return result == 0 || result == 1 ? 1 : -1;
}

int em_door_transit_commit(EmDoorDestination *request, int16_t door_id, uint16_t side,
    const uint8_t record[4], int (*fade)(void *, int, int), void *context)
{
    int whole_area = (door_id & 0x80) != 0;
    if (!request || !record || !fade || (!whole_area && side >= 4)) return -1;
    if (fade(context, whole_area, 4) != 1) return -1;
    if (whole_area) {
        request->kind = 1;
        request->area = record[0];
        request->entry = record[1];
        request->sub_area = record[2] ? record[3] : 0xff;
    } else {
        request->kind = 2;
        request->entry = record[side];
    }
    return 1;
}
