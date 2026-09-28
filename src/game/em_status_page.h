/* Original 0020CDC0 (the status screen's page core) and the 0020E0C0 exit
 * lifecycle. The hub, the ITEM root and the pages MAP / SPR4 / DATABASE are
 * required workers; Back is never substituted with closing the interaction. */
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
    EM_STATUS_PAGE_BACK_SOUND,     /*0020CD60 */
    EM_STATUS_PAGE_PAGE_TICK       /* phase 3 sub-state 2, argument t[0x10]: 1 0020F950 MAP,
                                    * 2 00211970 SPR4, 3 00214020 DATABASE */
} EmStatusPageEvent;

/* All side effects return1 only after accepted work. Actual asynchronous
 * completion is represented by item.asset_busy. Missing/failed workers
 * return a fault, preserving the caller's status/player ownership. */
typedef int (*EmStatusPageWorker)(void *context, EmStatusPage *state, EmStatusPageEvent event,
                                  unsigned argument);

/* 0 waiting, 1 actual exit completed, -1 unsupported route or worker failure.
 * Cold entry follows 0020CDC0 case 0's request map (docs/STATUS_PAGES.md
 * section 1) for every request except 6 (00225A00) and, without a request,
 * a nonzero D_008106C5 (the passcode pages): those fault before any side
 * effect. Phase 3 loads the module of pages 0..3, ticks them and returns to
 * the hub on any other page id; the passcode pages 4 / 5 and page 8 (phase
 * 2) fault. None of the faulting branches is reachable in AREA11. */
int em_status_page_tick(EmStatusPage *state, unsigned buttons, EmStatusPageWorker worker,
                        void *context);

#endif
