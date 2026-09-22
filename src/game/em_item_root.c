#include "game/em_item_root.h"

int em_item_root_hover(EmItemRoot *state, float magnitude, float angle)
{
    if (!state)
        return -1;
    unsigned hover = 0;
    if ((double)magnitude >= 0.8) {
        if (angle < -0.5235988f)
            hover = angle < -2.0071287f ? 4 : 3;
        else if (angle < 0.5235988f)
            hover = 2;
        else if (angle < 2.0071287f)
            hover = 1;
        else
            hover = angle < 3.1415927f ? 5 : 4;
    }
    int sound = hover != 0 && hover != state->hover;
    state->hover = (uint8_t)hover;
    return sound;
}

static int emit(EmItemRootWorker worker, void *context, EmItemRoot *state, EmItemRootEvent event,
                unsigned argument)
{
    return worker && worker(context, state, event, argument) == 1;
}

int em_item_root_tick(EmItemRoot *state, unsigned buttons, EmItemRootWorker worker, void *context)
{
    if (!state || !worker)
        return -1;
    switch (state->state) {
    case 0:
        state->message_mode = 4;
        state->message_phase = 0;
        state->message_group = 1;
        state->state = 1;
        state->step = 0;
        return emit(worker, context, state, EM_ITEM_RESET_INPUT, 0) ? 0 : -1;
    case 1:
        if (!emit(worker, context, state, EM_ITEM_DRAW_BACKGROUND, 0) ||
            !emit(worker, context, state, EM_ITEM_DRAW_ROOT, 0))
            return -1;
        switch (state->hover) {
        case 1:
            state->message_phase = 1;
            state->message_line = 1;
            break;
        case 2:
            state->message_phase = 1;
            state->message_line = 3;
            break;
        case 3:
            state->message_phase = 1;
            state->message_line = 0;
            break;
        case 4:
            state->message_phase = 1;
            state->message_line = 2;
            break;
        case 5:
            state->message_phase = 1;
            state->message_line = 4;
            break;
        default:
            state->message_phase = 0;
            break;
        }
        /* Back wins over simultaneous confirmation in the original. */
        if (buttons & 0x20) {
            if (!emit(worker, context, state, EM_ITEM_SOUND_BACK, 0))
                return -1;
            state->screen = 0x63;
        } else if (state->hover && (buttons & 0x40)) {
            if (!emit(worker, context, state, EM_ITEM_SOUND_ACCEPT, 0))
                return -1;
            state->message_phase = 0;
            state->selected = state->hover;
            state->state = 2;
        }
        break;
    case 2:
        switch (state->selected) {
        case 1:
        case 3:
        case 4:
        case 5:
            state->asset_busy = 1;
            state->next_state = state->selected == 1 ? 4 : state->selected + 2;
            state->state = 3;
            state->module = state->next_state + 0x1C;
            break;
        case 2:
            state->screen = 0x63;
            break;
        default:
            break;
        }
        break;
    case 3:
        if (!state->step) {
            if (!emit(worker, context, state, EM_ITEM_LOAD_MODULE, state->module))
                return -1;
            ++state->step;
        } else if (!state->asset_busy) {
            state->state = state->next_state;
            state->step = 0;
        }
        break;
    case 4:
    case 5:
    case 6:
    case 7:
        return emit(worker, context, state, EM_ITEM_CHILD_PAGE, state->state) ? 0 : -1;
    default:
        break;
    }
    return 0;
}
