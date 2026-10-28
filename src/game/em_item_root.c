#include "game/em_item_root.h"
#include "game/em_menu_hover.h"

int em_item_root_hover(EmItemRoot *state, float magnitude, float angle)
{
    if (!state)
        return -1;
    return em_menu_hover_0020D930(&state->hover, 1, magnitude, angle);
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

/* ---- 0020F170 / 0020F2A0 (the header) ---------------------------------- */

#define LIT 0x805FFF6Eu  /* a lit region's colour */
#define DIM 0x80808080u
#define HALF 0x40808080u

static int sprite(const EmItemRootLeaves *l, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba,
                  uint64_t tex0)
{
    return l->sprite(l->context, 1, x, y, w, h, rgba, tex0) == 1;
}

int em_item_root_draw_0020F170(const EmItemRootLeaves *l)
{
    if (!l || !l->background || !l->blend || !l->sprite)
        return 0;
    return l->background(l->context, 0x2003C8A59D421DB0ull) == 1 &&
           sprite(l, 0x7BC0, 0x7CF0, 0x80, 0x80, HALF, 0x2003C405DD321D00ull) &&
           sprite(l, 0x7800, 0x8300, 0x100, 0x80, HALF, 0x2003CA85E1421D40ull) &&
           l->blend(l->context, 1, 3) == 1 &&
           sprite(l, 0x7000, 0x8300, 0x80, 0x80, DIM, 0x2003CA25DD421D20ull) &&
           sprite(l, 0x7100, 0x7900, 0x80, 0x40, DIM, 0x2003C8859D421DA0ull);
}

int em_item_root_draw_0020F2A0(const EmItemRootLeaves *l, uint8_t hover)
{
    if (!l || !l->blend || !l->sprite || !l->trail)
        return 0;
    /* D_700038A0 / D_700038A4 = 248.0, 208.0 (0x43780000, 0x43500000). */
    if (l->trail(l->context, 248.0f, 208.0f, 0) != 1 || l->blend(l->context, 1, 0) != 1)
        return 0;
    int ok;
    if (hover == 1)
        ok = sprite(l, 0x80D0, 0x8010, 0x80, 0x20, LIT, 0x2003C6855D421DD4ull) &&
             sprite(l, 0x7F60, 0x7FC0, 0x20, 0x20, LIT, 0x2003C40555321E24ull) &&
             sprite(l, 0x85C0, 0x8070, 0x40, 0x40, DIM, 0x2003CA0599421DF8ull);
    else
        ok = sprite(l, 0x80D0, 0x8010, 0x80, 0x20, DIM, 0x2003C6855D421DD4ull) &&
             sprite(l, 0x85C0, 0x8070, 0x40, 0x40, HALF, 0x2003CA0599421DF8ull);
    if (!ok)
        return 0;
    if (hover == 2)
        ok = sprite(l, 0x8000, 0x7E80, 0x80, 0x20, LIT, 0x2003C6A55D421DC4ull) &&
             sprite(l, 0x8800, 0x7E80, 0x80, 0x20, LIT, 0x2003C8055D421DD0ull) &&
             sprite(l, 0x8190, 0x7E70, 0x20, 0x20, LIT, 0x2003C40555321E24ull);
    else
        ok = sprite(l, 0x8000, 0x7E80, 0x80, 0x20, DIM, 0x2003C6A55D421DC4ull) &&
             sprite(l, 0x8800, 0x7E80, 0x80, 0x20, DIM, 0x2003C8055D421DD0ull);
    if (!ok)
        return 0;
    if (hover == 3)
        ok = sprite(l, 0x80D0, 0x7CE0, 0x80, 0x20, LIT, 0x2003C6855D421DC0ull) &&
             sprite(l, 0x7F50, 0x7D10, 0x20, 0x20, LIT, 0x2003C40555321E24ull) &&
             sprite(l, 0x85C0, 0x7B90, 0x40, 0x40, DIM, 0x2003CA0599421DF0ull);
    else
        ok = sprite(l, 0x80D0, 0x7CE0, 0x80, 0x20, DIM, 0x2003C6855D421DC0ull) &&
             sprite(l, 0x85C0, 0x7B90, 0x40, 0x40, HALF, 0x2003CA0599421DF0ull);
    if (!ok)
        return 0;
    if (hover == 4)
        ok = sprite(l, 0x7590, 0x7D10, 0x80, 0x20, LIT, 0x2003C6855D421DE0ull) &&
             sprite(l, 0x7C20, 0x7DA0, 0x20, 0x20, LIT, 0x2003C40555321E24ull) &&
             sprite(l, 0x7550, 0x7B90, 0x40, 0x40, DIM, 0x2003CA0599421E00ull);
    else
        ok = sprite(l, 0x7590, 0x7D10, 0x80, 0x20, DIM, 0x2003C6855D421DE0ull) &&
             sprite(l, 0x7550, 0x7B90, 0x40, 0x40, HALF, 0x2003CA0599421E00ull);
    if (!ok)
        return 0;
    if (hover == 5)
        ok = sprite(l, 0x7590, 0x7FE0, 0x80, 0x20, LIT, 0x2003C6855D421DE4ull) &&
             sprite(l, 0x7C20, 0x7F60, 0x20, 0x20, LIT, 0x2003C40555321E24ull) &&
             sprite(l, 0x7550, 0x8070, 0x40, 0x40, DIM, 0x2003CA0599421E10ull);
    else
        ok = sprite(l, 0x7590, 0x7FE0, 0x80, 0x20, DIM, 0x2003C6855D421DE4ull) &&
             sprite(l, 0x7550, 0x8070, 0x40, 0x40, HALF, 0x2003CA0599421E10ull);
    return ok && l->blend(l->context, 1, 3) == 1 &&
           sprite(l, 0x8D50, 0x7E80, 0x20, 0x20, DIM, 0x2003C82555421E30ull);
}
