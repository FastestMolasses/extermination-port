#include "game/em_door_transit.h"
#include "game/em_ee_float.h"

#include <math.h>
#include <string.h>

#define PI_F 0x1.921fb6p+1f     /* 3.1415927f */
#define HALF_PI_F 0x1.921fb6p+0f /* 1.5707964f */

/* 001BBE40's geometry (byte-matched src/func_001BBE40.c), with every FPU
 * operation on the EE model:
 *   side    = fabsf(001B1470(001B1240(door +0xB0, player x, player z)
 *             - door yaw)) <= pi/2 ? 0 : 1 (0011DF78; c.le.s)
 *   yaw'    = 001B1470(pi + door yaw) (side 0) or 001B1470(door yaw)
 *   point   = (door.x - 5 cosf(door yaw), player y, door.z + 5 sinf(door yaw), 1)
 *   point.x -= 5 sinf(yaw'), point.z -= 5 cosf(yaw')
 * The patch words (clips, wait and the sound pair's side) are the
 * program's (0x24DC14 / 0x24DC54 / 0x24DC8C, or 0x24DCD4 / 0x24DD14). */
int em_door_transit_prepare(EmDoorTransitPlan *out, const float origin[3], float yaw,
    const float player[3], const uint16_t sounds[2], int locked,
    const EmDoorTransitMath *math)
{
    if (!out || !origin || !player || !sounds || !math || !math->bearing || !math->wrap ||
        !math->sine || !math->cosine || !isfinite(yaw) || fabsf(yaw) > PI_F ||
        (locked != 0 && locked != 1))
        return 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!isfinite(origin[axis]) || !isfinite(player[axis])) return 0;
    void *c = math->context;
    float bearing, relative, value;
    if (math->bearing(c, origin, player[0], player[2], &bearing) < 0 ||
        math->wrap(c, em_ee_sub(bearing, yaw), &relative) < 0)
        return 0;
    const uint32_t magnitude = em_ee_bits(relative) & UINT32_C(0x7FFFFFFF);   /* 0011DF78 */
    EmDoorTransitPlan plan = {0};
    plan.locked = locked;
    plan.side = em_ee_c_le_bits(magnitude, em_ee_bits(HALF_PI_F)) ? 0 : 1;
    if (math->wrap(c, plan.side ? yaw : em_ee_add(PI_F, yaw), &plan.player_yaw) < 0) return 0;
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
    if (math->cosine(c, yaw, &value) < 0) return 0;
    plan.position[0] = em_ee_sub(origin[0], em_ee_mul(5.0f, value));
    plan.position[1] = player[1];
    if (math->sine(c, yaw, &value) < 0) return 0;
    plan.position[2] = em_ee_add(origin[2], em_ee_mul(5.0f, value));
    plan.position[3] = 1;
    if (math->sine(c, plan.player_yaw, &value) < 0) return 0;
    plan.position[0] = em_ee_sub(plan.position[0], em_ee_mul(5.0f, value));
    if (math->cosine(c, plan.player_yaw, &value) < 0) return 0;
    plan.position[2] = em_ee_sub(plan.position[2], em_ee_mul(5.0f, value));
    *out = plan;
    return 1;
}

int em_door_transit_kickoff(EmDoorOriginal *door, float yaw,
    const float player[3], const uint16_t sounds[2], int locked,
    const EmDoorTransitMath *math, const EmDoorTransitHooks *hooks)
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
