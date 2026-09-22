#include "game/em_elevator.h"
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
        !hooks->update_actor) return -1;
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
        if (hooks->tick_script(hooks->context)) {
            owner->phase = owner->armed = 0;
            if (powered) {
                owner->lower = !owner->lower;
                set_height(owner);
                hooks->rebuild_pose(hooks->context, owner->height);
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
