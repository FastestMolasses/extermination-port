#include "game/em_elevator.h"
#include "game/em_pose_math.h"
#include <string.h>

static void set_height(EmElevator *owner)
{
    owner->height = owner->lower ? 190.0f : 230.0f;
    owner->script_heights[0] = owner->height;
    owner->script_heights[1] = owner->lower ? 205.0f : 245.0f;
    owner->script_heights[2] = owner->lower ? 245.0f : 205.0f;
}

void em_elevator_init(EmElevator *owner, int lower)
{
    memset(owner, 0, sizeof *owner);
    owner->lower = lower != 0;
    set_height(owner);
}

int em_elevator_tick(EmElevator *owner, int powered,
                     const EmElevatorHooks *hooks)
{
    if (!owner || !hooks || !hooks->start_script || !hooks->tick_script ||
        !hooks->sound || !hooks->rebuild_pose || !hooks->copy_indicator_pose ||
        !hooks->update_actor || !hooks->retransform) return -1;
    if (owner->phase == 0) {
        if (owner->armed & 4) {
            hooks->start_script(hooks->context, powered ?
                EM_ELEVATOR_POWERED_SCRIPT : EM_ELEVATOR_REFUSAL_SCRIPT);
            owner->phase = 1;
            owner->sound_timer = powered ? 0 : 300;
        }
    } else if (owner->phase == 1) {
        /* +2A is a sound-delay counter, not an interaction cooldown.
         * The refusal's300 sentinel suppresses the tick120 sound. */
        if (owner->sound_timer < 120 && ++owner->sound_timer == 120)
            hooks->sound(hooks->context, 0x19a, 300.0f);
        int result = hooks->tick_script(hooks->context);
        if (result < 0) return -1;
        if (result > 0) {
            owner->phase = owner->armed = 0;
            if (powered) {
                owner->lower = !owner->lower;
                set_height(owner);
                hooks->rebuild_pose(hooks->context, owner->height);   /* 0x827E48: 001C6380 */
                hooks->retransform(hooks->context);                   /* 0x827E54: 001A2370 */
            }
        }
        hooks->copy_indicator_pose(hooks->context);
    }
    hooks->update_actor(hooks->context);
    if (powered) {
        if (owner->indicator_level < 128) {
            owner->indicator_level += 8;
            if (owner->indicator_level > 128) owner->indicator_level = 128;
        }
    } else if (owner->indicator_level > 0) {
        owner->indicator_level -= 8;
        if (owner->indicator_level < 0) owner->indicator_level = 0;
    }
    return 0;
}

int em_elevator_motion_tick(EmElevatorMotion *motion, int lower,
                             float *owner_y, float *player_y,
                             float *camera_target_y,
                             const EmElevatorHooks *hooks)
{
    if (!motion || !owner_y || !player_y || !camera_target_y || !hooks ||
        !hooks->sound || !hooks->rebuild_pose) return -1;
    if (motion->phase == 0) {
        motion->ticks = 0;
        hooks->sound(hooks->context, lower ? 0x452 : 0x453, 300.0f);
        motion->rate = lower ? 0.26666668f : -0.26666668f;
        motion->phase = 1;
        return 0;
    }
    if (motion->phase == 1) {
        /* Three add.s (0x8280FC, 0x828114, 0x828128): the EE's single-
         * guard-bit add (em_pose_math.h pose_add), not a plain truncation.
         * Route capture 04_elevator_ride holds all 150 player Y values of
         * the descent (f394..f543, ending 190.00061); a truncating add would
         * end at 189.99832. */
        *owner_y = pose_add(*owner_y, motion->rate);
        *player_y = pose_add(*player_y, motion->rate);
        *camera_target_y = pose_add(*camera_target_y, motion->rate);
        hooks->rebuild_pose(hooks->context, *owner_y);
        /* Original signed32 counter. Use unsigned addition for defined
         * wrapping, then inspect its signed value as the EE branch does. */
        uint32_t next = (uint32_t)motion->ticks + 1;
        memcpy(&motion->ticks, &next, sizeof next);
        return motion->ticks >= 150;
    }
    return 1;
}
