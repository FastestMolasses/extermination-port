/* Original normal status hub: CDC0 phase1 and D930 selector table0. */
#ifndef EM_STATUS_HUB_H
#define EM_STATUS_HUB_H

#include "game/em_item_trail.h"
#include "game/em_status_page.h"

typedef enum {
    EM_STATUS_HUB_CLEAR_DRAW,   /*1AFEB0 */
    EM_STATUS_HUB_RESET_DRAW,   /*1AFE60 */
    EM_STATUS_HUB_RESET_INPUT,  /*20E020 */
    EM_STATUS_HUB_INSTALL_DRAW, /*1AFF10 then callback20E6F0 */
    EM_STATUS_HUB_BUILD_MODELS, /*20E250 */
    EM_STATUS_HUB_BACKGROUND,   /*20A7A0 */
    EM_STATUS_HUB_ACTORS_TICK,  /*1B0000 */
    EM_STATUS_HUB_DRAW,         /*209DF0 */
    EM_STATUS_HUB_SOUND
} EmStatusHubEvent;

typedef int (*EmStatusHubWorker)(void *, EmStatusPage *, EmStatusHubEvent, unsigned argument);

/* Original normal phase1 only. The item/page fields are the same UI object,
 * not a fabricated second hub. Return0 for an accepted callback,−1 for a
 * missing worker or unsupported phase. Infection is original81085C in0..100.
 * The caller supplies one original normalized stick sample for this frame.
 * Every worker must return1 only when the actual operation was accepted. */
int em_status_hub_tick(EmStatusPage *page, float infection, unsigned buttons,
                       const EmItemStick *stick, EmStatusHubWorker worker, void *context);

#endif
