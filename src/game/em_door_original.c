#include "game/em_door_original.h"
#include "game/em_effect_color.h"

static int advance(EmDoorOriginal *door, const EmDoorOriginalHooks *hooks)
{
    return hooks->advance_animation &&
        hooks->advance_animation(hooks->context, &door->animation_flags) == 1;
}

static int script_tick(EmDoorOriginal *door, const EmDoorOriginalHooks *hooks)
{
    /*001BC0E0 treats the flag as a signed byte but tests only zero.*/
    if (door->animation_active && !advance(door, hooks)) return -1;
    if (!hooks->script_tick) return -1;
    return hooks->script_tick(hooks->context);
}

int em_door_original_arm(EmDoorOriginal *door)
{
    if (!door || door->freed || door->lifecycle != 1) return 0;
    door->armed |= 4;
    return 1;
}

int em_door_original_tick(EmDoorOriginal *door, int unlocked,
    uint8_t transition_pending, const EmDoorOriginalHooks *hooks)
{
    if (!door || !hooks || door->freed) return -1;
    int result;
    switch (door->lifecycle) {
    case 0:
        if (!hooks->initialize) return -1;
        result = hooks->initialize(hooks->context);
        if (result < 0) return -1;
        if (result) {
            door->lifecycle = 1;
            door->door_id = (uint8_t)door->side;
            door->side = 0;
            float scale = door->link_flags & 0x40 ? 1.5f :
                          door->link_flags & 0x80 ? 2.0f : 1.0f;
            for (unsigned axis = 0; axis < 3; ++axis)
                door->initialized_scale[axis] = scale;
        } else {
            door->lifecycle = 3;
        }
        /*BC350 publishes this byte even when B0EA0 rejected allocation.*/
        door->status = 1;
        return 1;
    case 1:
        switch (door->phase) {
        case 0:
            if (door->armed & 4) {
                int locked = door->subtype == 0x15 && !unlocked;
                if (!hooks->kickoff || hooks->kickoff(hooks->context, locked) != 1)
                    return -1;
                door->phase = locked ? 1 : 3;
            }
            break;
        case 1:
            result = script_tick(door, hooks);
            if (result < 0) return -1;
            if (result) {
                if (!hooks->script_start ||
                    hooks->script_start(hooks->context, UINT32_C(0x24DBC0)) != 1)
                    return -1;
                ++door->phase;
            }
            break;
        case 2:
            result = script_tick(door, hooks);
            if (result < 0) return -1;
            if (result) {
                door->armed = 0;
                door->phase = 0;
            }
            break;
        case 3:
            result = script_tick(door, hooks);
            if (result < 0) return -1;
            if (result) ++door->phase;
            break;
        case 4:
            if (!advance(door, hooks) || !hooks->transition ||
                hooks->transition(hooks->context) != 1)
                return -1;
            ++door->phase;
            break;
        case 5:
            if (!advance(door, hooks)) return -1;
            if (!transition_pending) {
                if (!hooks->reset_animation ||
                    hooks->reset_animation(hooks->context) != 1)
                    return -1;
                door->armed = 0;
                door->phase = 0;
            }
            break;
        }
        /*001BC300 always runs after a lifecycle1 phase, including an
         * unknown phase. Its position comes from actor+B0, not A0.*/
        if (!hooks->place || hooks->place(hooks->context) != 1) return -1;
        float point[3] = {door->origin[0],
            em_effect_float32((double)door->origin[1] + 10.0), door->origin[2]};
        if (!hooks->publish) return -1;
        result = hooks->publish(hooks->context, point);
        if (result < 0 || result > 255) return -1;
        door->visible = (uint8_t)result;
        if (!hooks->draw || hooks->draw(hooks->context) != 1) return -1;
        return 1;
    case 2:
    case 3:
        if (!hooks->free || hooks->free(hooks->context) != 1) return -1;
        door->freed = 1;
        return 0;
    default:
        return 1;
    }
}
