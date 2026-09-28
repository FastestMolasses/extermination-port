/* Persistent original panel and pickup BATTERY/ITEM status adapter.
 * Unimplemented hub/child pages remain explicit workers, never cancellation. */
#ifndef EM_STATUS_RUNTIME_H
#define EM_STATUS_RUNTIME_H

#include <stddef.h>

#include "em_gfx.h"
#include "game/em_battery_ui.h"
#include "game/em_item_trail.h"
#include "game/em_panel.h"
#include "game/em_status_frame.h"
#include "game/em_status_hub.h"
#include "game/em_status_hub_ui.h"
#include "game/em_status_page.h"
#include "game/em_status_pages_live.h"

typedef struct EmStatusRuntime EmStatusRuntime;

typedef struct {
    uint8_t battery_count[3]; /* original item1B/1C/1D */
    uint16_t charge;
    uint8_t capacity, status, primary, secondary; /*810CB7/C60/CA4/CA6 */
} EmStatusInventory;

typedef struct {
    uint16_t pressed, held;   /* original D810E74 and held-button word */
    uint8_t stick_x, stick_y; /* original D810E64/E65 */
    uint8_t audio_busy;       /* actual D282157 worker gate */
    uint16_t repeat;          /* D810E78: pressed with the D-pad auto-repeat */
} EmStatusInput;

/* One ITEM > BATTERY page call (002149F0, the ITEM root's child page 5),
 * over the original records (docs/STATUS_PAGE_RECORD.md sections 4 and 7):
 *   ui           the one 0xA0-byte UI block D_00810130 (0020E060's memset
 *                clears it); EmStatusPage / EmItemRoot are views of its
 *                +0..+6, +8, +0xC, +0x10, +0x11, +0x15, +0x16, loaded before
 *                the call and stored after it
 *   b0 / b1 / c5 D_008106B0 / B1 / C5 (the page's request bytes)
 *   d810C7F.. the inventory bytes C7F (3), CB2 (2), CB7 (1): a per-call
 *                view of read_inventory; the runtime stores a changed charge
 *                or capacity through write_charge / write_battery_capacity
 *   d810E74      this frame's pressed word (2 bytes, little-endian)
 *   held, repeat D_00810E70 / D_00810E78 (0020B0D0, 0020B210)
 *   words        D_002821B0 / B4 / B8 / D_00282240 (the message view)
 *   gauge        00209280's resident text (the hub's records)
 *   draw         the page's 2D leaf calls, submitted by the render */
typedef struct {
    uint8_t *ui;
    size_t ui_size;
    uint8_t *b0;
    const uint8_t *b1;
    uint8_t *c5;
    const uint8_t *d810C7F;
    uint8_t *d810CB2, *d810CB7;
    const uint8_t *d810E74;
    uint16_t held, repeat;
    int32_t *words[4];
    const EmStatusBatteryData *gauge;
    EmBatteryUI *draw;
} EmStatusBatteryPage;

typedef struct {
    void *context;
    int (*read_inventory)(void *, EmStatusInventory *);
    int (*write_charge)(void *, uint16_t charge);
    /* These are the actual outer1AE040 and page CDC0/E0C0 world effects.
     * Return1 only after accepting the operation. RESET_UI is also handled
     * internally without clearing the original pending global request. */
    int (*frame_event)(void *, EmStatusFrameEvent, const EmStatusFrame *);
    int (*page_event)(void *, EmStatusPageEvent, unsigned argument);
    int (*sound)(void *, uint32_t cue); /* original4096 on all volume axes */
    /* The ITEM > BATTERY page 002149F0 (em_battery_page_live in the AREA11
     * host, which adds D_008106D0, 0x70003B8D, 00185420 and the owner
     * record). 1 accepted, anything else a fault. Required when the ITEM
     * root reaches its child page 5; it needs the message view below and a
     * bound hub (the gauge text). */
    int (*battery_page)(void *, const EmStatusBatteryPage *);
    /* Adapter owns its parsed modules1F/21. Other actual module reloads
     * (e.g.32..35 on exit) require these workers: begin1 accepted;
     * ready−1 failure,0 pending,1 complete. */
    int (*module_begin)(void *, unsigned module);
    int (*module_ready)(void *, unsigned module);
    /* Required only when navigation reaches the broader status hub or
     * another ITEM child. Missing workers fault and retain ownership. */
    int (*other_page_tick)(void *, EmStatusPage *, const EmStatusInput *);
    int (*other_page_render)(void *, EmGfx *, const EmStatusPage *);
    /* Required for pickup request1/item1B..1D. Original149F0 writes charge
     * then capacity before clearing the request and starting its notice. */
    int (*write_battery_capacity)(void *, uint16_t charge, uint8_t capacity);
    /* The original normal hub (0020CDC0 phases 1/2: em_status_hub with
     * em_status_hub_ui and 0020A7A0), live once em_status_runtime_bind_hub
     * has bound its records. Both are required when the hub is reached;
     * 1 accepted.
     *   hub_display  the 00209DF0 inputs (D_00810858/5C, D_008104E4,
     *                C7F, CB2, CB7, CA4/CA6, CA8..CB0, CB4); the runtime
     *                sets the hover (UI+0x11 after 0020D930).
     *   hub_models   the status-model workers EM_STATUS_HUB_INSTALL_DRAW
     *                (001AFF10 + callback 0020E6F0), _BUILD_MODELS
     *                (0020E250) and _ACTORS_TICK (001B0000).
     *   hub_models_draw  the model draws the last 001B0000 walk queued.
     *                em_status_runtime_render calls it after 0020A7A0's
     *                background (flushed first, em_gfx_overlay_backdrop_flush)
     *                and before 00209DF0's 2D layer: the original's GS
     *                packet order. */
    int (*hub_display)(void *, EmStatusHubDisplay *);
    int (*hub_models)(void *, EmStatusHubEvent, unsigned argument);
    int (*hub_models_draw)(void *, EmGfx *);
    /* The live message block's words the pages write (the live scene route
     * only): words[0..3] = D_002821B0 (mode), D_002821B4 (phase),
     * D_002821B8 (line) and D_00282240 (the mode-4 group). With it, the
     * page's EmItemRoot message fields are a per-call view of those words
     * (loaded before 0020CDC0, stored after), and step F's 001FCA10
     * presents the lines (001FCB90); the runtime's own help-line
     * presenters are not used. 1 bound. NULL keeps the private copy (the
     * sanitizer fixtures' route). */
    int (*message_words)(void *, int32_t *words[4]);
    /* The host's storage for the status pages (em_status_pages_live, bound
     * with em_status_runtime_bind_pages): the request bytes, the progress
     * block, the message block, the mode byte 0x70003B8D, the vitals and
     * D_008104E4, and the main-loop counter. The runtime fills the status
     * block, the busy byte, the pads and the gauge records. 1 bound. */
    int (*pages_frame)(void *, EmStatusPagesFrame *);
} EmStatusRuntimeHooks;

EmStatusRuntime *em_status_runtime_load(const char *battery_path, const char *item_path,
                                        const EmItemMath *, const EmStatusRuntimeHooks *);
void em_status_runtime_free(EmStatusRuntime *); /* owner tears down the game first */
/* Bind the original hub's 00209DF0 records (the runtime owns ui from now
 * on, also on failure). Without it EM_STATUS_PAGE_HUB_TICK reaches the
 * other_page hooks (the fixtures' path). 1 bound, 0 failure. */
int em_status_runtime_bind_hub(EmStatusRuntime *, EmStatusHubUI *ui);
/* Bind the status pages MAP / SPR4 / DATABASE and the ITEM children
 * EQUIPMENT / EVENT / HEALING (the runtime owns `pages` from now on, also
 * on failure; it needs the pages_frame hook and a bound hub). Their module
 * loads (0x1E, 0x20, 0x22, 0x23, 0x24, 0x2C..0x31) complete at host speed,
 * as 0x1F / 0x21 do, and apply the module's GS blocks; 00200970(1) applies
 * the restore. Without it those pages fault. 1 bound, 0 failure. */
int em_status_runtime_bind_pages(EmStatusRuntime *, EmStatusPagesLive *pages);
/* The shared UI+0x20 clock (00208AD0 advances it; the 0020E060 memset
 * clears it), for tests. */
uint32_t em_status_runtime_ui_clock(const EmStatusRuntime *);
/* Queue the original normal status route (B0=0/C5=0), after the host's
 * actual gameplay input gates. Requires both real hub workers; it does
 * not substitute a panel request or silently accept unsupported artwork. */
int em_status_runtime_open(EmStatusRuntime *);
/* Called by the original panel callback; queues globals without consuming
 * the remainder of the current ordinary task callback. */
int em_status_runtime_battery_open(EmStatusRuntime *, EmPanel *owner, uint8_t request);
/* Called after the pickup inventory mutation, before opcode09 yields.
 * Accepts only the verified battery request kind1/index1B..1D. */
int em_status_runtime_pickup_request(EmStatusRuntime *, uint8_t kind, uint8_t index);
/* Call at the beginning of each ordinary outer frame. Return1 means the
 * whole frame belongs to status, including the final phase5 release frame;
 * return0 permits ordinary tasks,−1 is a retained-ownership fault. */
int em_status_runtime_tick(EmStatusRuntime *, const EmStatusInput *);
int em_status_runtime_ordinary_enabled(const EmStatusRuntime *);

/* The live scene route (WP-4): 0x1AE040 states 3/5 run in the scene core
 * (em_scene_frame.c, em_status_frame over the canonical task bytes), so
 * only the page layer runs here, at the core's worker positions. These
 * never touch the runtime's own EmStatusFrame, so the ordinary gate above
 * stays open for the host's owners.
 *   page_open  0020E060: clears the page block D_00810130 (the RESET_UI
 *              event) and binds the owner the request's D_008106D0 names
 *              (NULL for a pickup request). 1 accepted, -1 fault.
 *   page_tick  0020CDC0 over the request bytes: b0, b1, c5 and cc point at the
 *              canonical D_008106B0/B1/C5/CC, loaded into the page before
 *              the tick and stored back after it (the page reads and
 *              clears them as the original does). 0 waiting, 1 the page
 *              completed its 0020E0C0 exit, -1 fault.
 * em_status_runtime_render draws what the last page_tick selected. */
int em_status_runtime_page_open(EmStatusRuntime *, EmPanel *owner);
int em_status_runtime_page_tick(EmStatusRuntime *, const EmStatusInput *, uint8_t *b0,
                                uint8_t *b1, uint8_t *c5, uint8_t *cc);
/* Submit the page selected by the preceding update once. Repeated calls in
 * the same frame are no-ops, so neither glow nor shared background advances
 * twice. No inventory/animation/script state is ticked here.1 success,−1 fault. */
int em_status_runtime_render(EmStatusRuntime *, EmGfx *);
const EmStatusFrame *em_status_runtime_frame(const EmStatusRuntime *);
const EmStatusPage *em_status_runtime_page(const EmStatusRuntime *);

#endif
