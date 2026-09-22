#include "game/em_status_page.h"

static int emit(EmStatusPageWorker worker, void *context, EmStatusPage *state,
                EmStatusPageEvent event, unsigned argument)
{
    return worker && worker(context, state, event, argument) == 1;
}

static int inventory_module(const EmStatusPage *state)
{
    /* Original001FEF70; the saved module gates the actual reload on exit. */
    if (state->inventory_primary == 2)
        return 0x35;
    switch (state->inventory_secondary) {
    case 1:
        return 0x32;
    case 2:
    case 3:
        return 0x33;
    case 4:
        return 0x34;
    default:
        return -1;
    }
}

static int exit_tick(EmStatusPage *state, EmStatusPageWorker worker, void *context)
{
    switch (state->step) {
    case 0: {
        state->restore_textures = 1;
        state->item.message_phase = 2;
        state->step = 2;
        int module = inventory_module(state);
        if (module != -1 && state->saved_module != module) {
            state->item.asset_busy = 1;
            if (!emit(worker, context, state, EM_STATUS_PAGE_LOAD, (unsigned)module))
                return -1;
            state->step = 1;
        }
        break;
    }
    case 1:
        if (!state->item.asset_busy) {
            state->step = 2;
            if (!emit(worker, context, state, EM_STATUS_PAGE_CLOSE_SOUND, 0))
                return -1;
        }
        break;
    case 2:
        if (state->saved_status == 1 && state->current_status == 2 &&
            !emit(worker, context, state, EM_STATUS_PAGE_RESTORE_PLAYER, 0))
            return -1;
        if (!emit(worker, context, state, EM_STATUS_PAGE_END_PROJECTION, 0))
            return -1;
        state->status_request = 0;
        state->request = 0;
        state->item.message_phase = 2;
        if (!emit(worker, context, state, EM_STATUS_PAGE_CLEAR_DRAW, 0))
            return -1;
        state->active = state->phase = state->step = state->transition_step = 0;
        return 1;
    default:
        break;
    }
    return 0;
}

int em_status_page_tick(EmStatusPage *state, unsigned buttons, EmStatusPageWorker worker,
                        void *context)
{
    if (!state || !worker)
        return -1;
    switch (state->phase) {
    case 0:
        if (state->request != 1 || (!(state->request_kind & 0xC0) &&
                                    (state->request_kind < 0x1B || state->request_kind >= 0x1E)))
            return -1;
        if (!emit(worker, context, state, EM_STATUS_PAGE_BLACK_HOLD, 0) ||
            !emit(worker, context, state, EM_STATUS_PAGE_OPEN_SOUND, 0))
            return -1;
        state->step = state->transition_step = state->item.state = state->item.step = 0;
        if (!emit(worker, context, state, EM_STATUS_PAGE_CONFIGURE, 0))
            return -1;
        state->phase = 1;
        state->saved_status = state->current_status;
        state->saved_module = inventory_module(state);
        state->phase = 3;
        state->item.screen = 0;
        state->step = 2;
        state->item.state = 2;
        state->item.selected = 3;
        break;
    case 1:
    case 2:
        return emit(worker, context, state, EM_STATUS_PAGE_HUB_TICK, state->phase) ? 0 : -1;
    case 3:
        switch (state->step) {
        case 0:
            if (!emit(worker, context, state, EM_STATUS_PAGE_CLEAR_DRAW, 0) ||
                !emit(worker, context, state, EM_STATUS_PAGE_RESET_DRAW, 0))
                return -1;
            state->step = 1;
            state->transition_step = 0;
            break;
        case 1:
            if (!state->transition_step) {
                if (state->item.screen != 0)
                    return -1;
                state->item.asset_busy = 1;
                if (!emit(worker, context, state, EM_STATUS_PAGE_LOAD, 0x1F))
                    return -1;
                state->transition_step = 1;
            } else if (state->transition_step == 1 && !state->item.asset_busy) {
                state->step = 2;
                state->transition_step = 0;
                state->item.state = 0;
            }
            break;
        case 2:
            if (state->status_request == 0xFF || (!state->item.asset_busy && !state->request &&
                                                  !state->status_request && (buttons & 0x810))) {
                if (state->status_request != 0xFF &&
                    !emit(worker, context, state, EM_STATUS_PAGE_BACK_SOUND, 0))
                    return -1;
                state->item.message_phase = 0;
                state->phase = 5;
                state->step = state->transition_step = 0;
                if (!emit(worker, context, state, EM_STATUS_PAGE_PLAYER_TEXTURE, 1))
                    return -1;
            } else if (state->item.screen == 0) {
                if (!emit(worker, context, state, EM_STATUS_PAGE_ITEM_TICK, 0))
                    return -1;
            } else if (state->item.screen == 0x63) {
                state->item.message_phase = 0;
                state->phase = 4;
                state->step = state->transition_step = 0;
                if (!emit(worker, context, state, EM_STATUS_PAGE_PLAYER_TEXTURE, 1))
                    return -1;
            } else {
                return -1;
            }
            break;
        default:
            break;
        }
        break;
    case 4:
        state->phase = 1;
        state->step = state->transition_step = 0;
        break;
    case 5:
        return exit_tick(state, worker, context);
    default:
        return -1;
    }
    return 0;
}
