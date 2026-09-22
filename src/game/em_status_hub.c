#include "game/em_status_hub.h"
#include <math.h>

static int emit(EmStatusHubWorker worker, void *context, EmStatusPage *page, EmStatusHubEvent event,
                unsigned argument)
{
    return worker && worker(context, page, event, argument) == 1;
}

static unsigned hover(const EmItemStick *stick)
{
    /* D930 promotes the float magnitude and compares against double0.8. */
    if ((double)stick->magnitude <= 0.8)
        return 0;
    float angle = stick->angle;
    if (angle < -0.7853981852531433f) {
        if (angle < -2.356194496154785f)
            return 4;
        return 3;
    }
    if (angle < 0.7853981852531433f)
        return 2;
    if (angle < 2.356194496154785f)
        return 1;
    return 4;
}

int em_status_hub_tick(EmStatusPage *page, float infection, unsigned buttons,
                       const EmItemStick *stick, EmStatusHubWorker worker, void *context)
{
    if (!page || !stick || !worker || page->phase != 1 || !isfinite(infection) || infection < 0 ||
        infection > 100 || !isfinite(stick->magnitude) || !isfinite(stick->angle))
        return -1;
    switch (page->step) {
    case 0:
        if (!emit(worker, context, page, EM_STATUS_HUB_CLEAR_DRAW, 0) ||
            !emit(worker, context, page, EM_STATUS_HUB_RESET_DRAW, 0) ||
            !emit(worker, context, page, EM_STATUS_HUB_RESET_INPUT, 0) ||
            !emit(worker, context, page, EM_STATUS_HUB_INSTALL_DRAW, 0x0020E6F0) ||
            !emit(worker, context, page, EM_STATUS_HUB_BUILD_MODELS, 0))
            return -1;
        page->item.message_mode = 4;
        page->item.message_phase = 0;
        page->item.message_group = 0;
        page->step = 1;
        return 0;
    case 1: {
        if (!emit(worker, context, page, EM_STATUS_HUB_BACKGROUND, 0) ||
            !emit(worker, context, page, EM_STATUS_HUB_ACTORS_TICK, 0))
            return -1;
        unsigned selected = hover(stick);
        if (selected && selected != page->item.hover &&
            !emit(worker, context, page, EM_STATUS_HUB_SOUND, 5))
            return -1;
        page->item.hover = (uint8_t)selected;
        if (!emit(worker, context, page, EM_STATUS_HUB_DRAW, 0))
            return -1;
        if (selected) {
            static const uint8_t lines[4] = {0, 9, 2, 1};
            page->item.message_phase = 1;
            page->item.message_line = lines[selected - 1];
        } else {
            int remaining = 100 - (int)infection;
            if (remaining == 100) {
                page->item.message_phase = 0;
            } else {
                page->item.message_phase = 1;
                page->item.message_line = remaining >= 81   ? 4
                                          : remaining >= 51 ? 5
                                          : remaining >= 31 ? 6
                                          : remaining >= 11 ? 7
                                          : remaining > 0   ? 8
                                                            : 3;
            }
        }
        if (buttons & 0x830) {
            if (!emit(worker, context, page, EM_STATUS_HUB_SOUND, 1))
                return -1;
            page->item.message_phase = 0;
            page->phase = 5;
            page->step = page->transition_step = 0;
        } else if (buttons & 0x40) {
            if (selected) {
                if (!emit(worker, context, page, EM_STATUS_HUB_SOUND, 0))
                    return -1;
                static const uint8_t screens[4] = {3, 2, 1, 0};
                page->item.message_phase = 0;
                page->phase = 3;
                page->step = page->transition_step = page->item.state = 0;
                page->item.screen = screens[selected - 1];
            } else if (!emit(worker, context, page, EM_STATUS_HUB_SOUND, 2)) {
                return -1;
            }
        }
        return 0;
    }
    default:
        return -1;
    }
}
