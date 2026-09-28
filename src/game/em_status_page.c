#include "game/em_status_page.h"
#include "game/em_status_scene_original.h"

static int emit(EmStatusPageWorker worker, void *context, EmStatusPage *state,
                EmStatusPageEvent event, unsigned argument)
{
    return worker && worker(context, state, event, argument) == 1;
}

/* 001FEF70 (em_status_scene_original's translation, the one owner): the
 * inventory's bank module; the saved module gates the reload on exit. */
static int inventory_module(const EmStatusPage *state)
{
    return em_status_scene_bank_001FEF70(state->inventory_primary, state->inventory_secondary);
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
        /* 0020CDC0 case 0. Two branches reach untranslated pages and fault
         * before any side effect: request 6 (00225A00 / 00225AC0, posted
         * only by 00157F60 for an owner of type 0x38) and, with no request,
         * a nonzero D_008106C5 (the passcode pages 002072C0). Neither is
         * reachable in AREA11 (docs/STATUS_PAGES.md section 1). */
        if (state->request == 6 || (!state->request && state->status_request))
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
        if (!state->request)
            break;
        /* The request map, in 0020CDC0's test order. The B1 compares are
         * signed compares of the zero-extended byte; bit 0xC0 is tested
         * first. Branches that do not store t[0x15] keep its old value. */
        state->phase = 3;
        if (state->request == 5) {
            state->item.screen = 2;
        } else if (state->request == 4) {
            state->item.screen = 0;
            state->step = 2;
            state->item.state = 2;
            state->item.selected = 5;
        } else if (state->request == 1) {
            const unsigned kind = state->request_kind;
            state->step = 2;
            state->item.state = 2;
            if (kind & 0xC0) {
                state->item.screen = 0;
                state->item.selected = 3;
            } else if (kind < 0x17) {
                state->item.screen = 2;
                if (kind < 5)
                    state->item.selected = 6;
                else if (kind < 7)
                    state->item.selected = 2;
                else if (kind < 0xA)
                    state->item.selected = 5;
                else if (kind < 0xF)
                    state->item.selected = 3;
                else if (kind < 0x10)
                    state->item.selected = 4;
                else
                    state->step = state->item.state = 0;
            } else {
                state->item.screen = 0;
                state->item.selected = kind < 0x1B ? 1 : kind < 0x1E ? 3 : kind < 0x23 ? 5 : 4;
            }
        } else if (state->request == 2) {
            state->item.screen = 1;
        } else {
            state->item.screen = 3;
        }
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
                /* The page module: 0x1F ITEM, 0x1E MAP, 0x2C SPR4, 0x24
                 * DATABASE (0x25 / 0x26, the passcode pages, are not
                 * reachable in AREA11: fault). Any other page id only
                 * advances t[3]. */
                static const uint8_t modules[4] = {0x1F, 0x1E, 0x2C, 0x24};
                if (state->item.screen == 4 || state->item.screen == 5)
                    return -1;
                if (state->item.screen < 4) {
                    state->item.asset_busy = 1;
                    if (!emit(worker, context, state, EM_STATUS_PAGE_LOAD,
                              modules[state->item.screen]))
                        return -1;
                }
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
            } else if (state->item.screen >= 1 && state->item.screen <= 3) {
                /* 0020F950 MAP, 00211970 SPR4, 00214020 DATABASE. */
                if (!emit(worker, context, state, EM_STATUS_PAGE_PAGE_TICK, state->item.screen))
                    return -1;
            } else if (state->item.screen == 4 || state->item.screen == 5 ||
                       state->item.screen == 8) {
                /* The passcode pages and page 8 (phase 2, the health count-up
                 * of HEALING kinds 2..4): not reachable in AREA11. */
                return -1;
            } else {
                /* 0x63 and every other page id return to the hub. */
                state->item.message_phase = 0;
                state->phase = 4;
                state->step = state->transition_step = 0;
                if (!emit(worker, context, state, EM_STATUS_PAGE_PLAYER_TEXTURE, 1))
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
