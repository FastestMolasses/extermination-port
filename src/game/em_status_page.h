/* Original0020CDC0 ITEM/BATTERY route and0020E0C0 exit lifecycle.
 * The broader status hub and other inventory pages remain required workers;
 * Back is never substituted with closing the interaction. */
#ifndef EM_STATUS_PAGE_H
#define EM_STATUS_PAGE_H

#include "game/em_item_root.h"

typedef struct {
    uint8_t active, phase, step, transition_step;                    /* UI+0..3 */
    uint8_t saved_status, current_status;                            /* UI+C, D810C60 */
    uint8_t request, request_kind, status_request, restore_textures; /*8106B0/B1/C5/CC */
    uint8_t inventory_primary, inventory_secondary;                  /*810CA4/A6 */
    int32_t saved_module;                                            /* UI+8 */
    EmItemRoot item;
} EmStatusPage;

typedef enum {
    EM_STATUS_PAGE_BLACK_HOLD,     /*001AED80(0) */
    EM_STATUS_PAGE_OPEN_SOUND,     /*001FB9F0(B,1000,1000,1000) */
    EM_STATUS_PAGE_CONFIGURE,      /*0020DFA0: actual UI camera/projection setup */
    EM_STATUS_PAGE_CLEAR_DRAW,     /*001AFEB0 */
    EM_STATUS_PAGE_RESET_DRAW,     /*001AFE60 */
    EM_STATUS_PAGE_LOAD,           /*001FF080(0,argument); clears busy only on completion */
    EM_STATUS_PAGE_PLAYER_TEXTURE, /*00200970(1) */
    EM_STATUS_PAGE_ITEM_TICK,      /*0020EE50; changes item and outer fields through state */
    EM_STATUS_PAGE_HUB_TICK,       /*0020CDC0 phase1/2; required broader status worker */
    EM_STATUS_PAGE_RESTORE_PLAYER, /*0015C7B0(player) */
    EM_STATUS_PAGE_END_PROJECTION, /*0021BAE0(0) inside0020E080 */
    EM_STATUS_PAGE_CLOSE_SOUND,    /*001FB9F0(D,1000,1000,1000) */
    EM_STATUS_PAGE_BACK_SOUND      /*0020CD60 */
} EmStatusPageEvent;

/* All side effects return1 only after accepted work. Actual asynchronous
 * completion is represented by item.asset_busy. Missing/failed workers
 * return a fault, preserving the caller's status/player ownership. */
typedef int (*EmStatusPageWorker)(void *context, EmStatusPage *state, EmStatusPageEvent event,
                                  unsigned argument);

/*0 waiting,1 actual exit completed,-1 unsupported route or worker failure.
 * Cold entry supports the normal hub (request0/status_request0), original
 * panel request1 with kind&C0 and battery
 * acquisition indices1B..1D. Phase3
 * supports ITEM screen0 and the original63 return to the status hub. */
int em_status_page_tick(EmStatusPage *state, unsigned buttons, EmStatusPageWorker worker,
                        void *context);

#endif
