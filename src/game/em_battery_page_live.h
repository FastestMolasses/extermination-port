/* em_battery_page_live.h - the ITEM > BATTERY page 002149F0 bound live, with
 * its page draws (docs/STATUS_PAGE_RECORD.md sections 4 and 7).
 *
 * This module adds no behaviour of its own. One call runs the original
 * page (em_spr_002149F0, em_status_page_record) over the caller's records
 * and binds its workers to the translations:
 *   0020A7A0  the background tile      a background call (em_battery_ui)
 *   0020AE40  the frame                em_sul_0020AE40 over the page table
 *                                      D_00265C50; its 00209280 is
 *                                      em_status_battery_draw over D_00810CB2 /
 *                                      CB7 / C7F with the hub's resident text
 *   0020B210  the list                 em_sul_0020B210 over D_00265CD0, with
 *                                      D_002821B4 / B8 / D_00282240 loaded
 *                                      from and stored to the message words
 *   0020B0D0  the arrows               em_sul_0020B0D0 (D_00810E70)
 *   0020CCB0  the Yes/No marker        em_cs_0020CCB0
 *   001FCF10  the confirmation line    em_rvr_001FCF10, its 001FCB90 the
 *                                      host's presenter (the message service)
 *   0020CD40 / 60 / A0, 0020CD80       em_sul_0020CD40 / 60 / A0, em_spr_0020CD80
 *   00207D00 / 00207E40 / 00207F80     leaf calls recorded in em_battery_ui
 *   001281C0                           em_player_float_to_int
 *   001FB9F0, 00185420, the owner      the host's workers
 * 0020BBE0 / 0020BC50 (the list refill and scroll) are unreachable on this
 * page (at most one row: the original 0020B210 never raises the wrap event;
 * STATUS_PAGE_RECORD.md section 4) and fault if reached.
 *
 * The em_sul memory regions are the page tables (from the atlas records,
 * em_battery_ui_tables) and the pad words D_00810E70 / D_00810E78 of this
 * frame.
 *
 * Fail-stop: a fault of the page or of any worker returns -1 with the
 * original address in *fault; the writes and calls before it stay. */
#ifndef EM_BATTERY_PAGE_LIVE_H
#define EM_BATTERY_PAGE_LIVE_H

#include <stdint.h>

#include "game/em_battery_ui.h"
#include "game/em_status_draw.h"
#include "game/em_status_page_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The host's workers (each: negative = fault). */
typedef struct {
    void *context;
    /* 001FB9F0(id, a1, a2, a3) */
    int (*sound)(void *context, int32_t id, int32_t a1, int32_t a2, int32_t a3);
    /* 00185420(item): *owner = the record address it returns (0 = none) */
    int (*find_device)(void *context, int32_t item, uint32_t *owner);
    /* the owner record at `owner`: EmSprWorkers.owner_read / owner_write */
    int (*owner_read)(void *context, uint32_t owner, uint32_t offset, uint32_t size,
                      int32_t *value);
    int (*owner_write)(void *context, uint32_t owner, uint32_t offset, uint8_t value);
    /* 001FCB90(x, y, group, line) */
    int (*present)(void *context, int32_t x, int32_t y, int32_t group, int32_t line);
} EmBatteryPageHost;

typedef struct {
    EmSprRecords records;             /* every pointer bound */
    uint16_t held, repeat;            /* D_00810E70, D_00810E78 of this frame */
    const EmStatusBatteryData *gauge; /* 00209280's resident text */
    EmBatteryUI *draw;                /* the atlas and the frame's leaf calls */
} EmBatteryPageCall;

/* One 002149F0 call: the draw list is restarted, then filled in the
 * original order. 0, or -1 (fault->address names the original). */
int em_battery_page_live_tick(const EmBatteryPageHost *host, const EmBatteryPageCall *call,
                              EmSprFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_BATTERY_PAGE_LIVE_H */
