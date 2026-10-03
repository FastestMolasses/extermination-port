#include "game/em_status_runtime.h"
#include "game/em_battery_ui.h"
#include "game/em_item_ui.h"
#include "game/em_status_background.h"
#include "game/em_module_loader.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    int32_t xy[3][2];
    unsigned intensity;
} TrailTriangle;

enum { DRAW_NONE, DRAW_BATTERY, DRAW_ITEM, DRAW_OTHER, DRAW_HUB, DRAW_PAGE };

/* The hub's 0020A7A0 tile (0020CDC0 phase 1/2 step 1). */
#define HUB_BACKGROUND_TEX0 UINT64_C(0x20045EE59D421E40)

struct EmStatusRuntime {
    EmStatusFrame frame;
    EmStatusPage page;
    EmStatusInput input;
    EmStatusInventory inventory;
    EmStatusRuntimeHooks hooks;
    EmItemMath math;
    EmItemTrail trail;
    EmPanel *owner;
    EmBatteryUI *battery;       /* the BATTERY page's atlas and leaf calls */
    uint8_t ui[0xA0];           /* the UI block D_00810130 */
    EmItemUI *item;
    EmStatusHubUI *hub;
    EmStatusPagesLive *pages; /* MAP / SPR4 / DATABASE and the ITEM children */
    uint8_t *d275BD8;         /* the one D_00275BD8 (em_status_runtime_bind_busy) */
    EmModuleLoader *loader;   /* 001FF080's task (em_status_runtime_bind_loader) */
    EmStatusHubDisplay hub_display;
    EmItemStick hub_stick;
    uint32_t ui_clock; /* UI+0x20 (D_00810150) */
    TrailTriangle triangles[512];
    unsigned triangle_count, draw_kind, draw_hover, slot_kind;
    int draw_help, pending_module;
    int message_view; /* the page tick runs over the live block's words */
    int queued, consumed, rendered, failed;
};

static int fail(EmStatusRuntime *runtime)
{
    if (runtime)
        runtime->failed = 1;
    return -1;
}

static int sound(EmStatusRuntime *runtime, unsigned cue)
{
    return runtime->hooks.sound(runtime->hooks.context, cue) == 1;
}

/* D_00275BD8 is one byte, the host's (em_status_runtime_bind_busy):
 * page->item.asset_busy is its per-call view, loaded before the page
 * layer runs and stored after it (viewed_page_tick), as the request bytes
 * are. A write outside that window goes to both. */
static void busy_store(EmStatusRuntime *runtime, uint8_t value)
{
    runtime->page.item.asset_busy = value;
    if (runtime->d275BD8)
        *runtime->d275BD8 = value;
}

static int module_ready(EmStatusRuntime *runtime)
{
    if (runtime->pending_module < 0)
        return 1;
    if (!runtime->hooks.module_ready)
        return 0;
    int result =
        runtime->hooks.module_ready(runtime->hooks.context, (unsigned)runtime->pending_module);
    if (result < 0 || result > 1)
        return 0;
    if (result) {
        busy_store(runtime, 0);
        runtime->pending_module = -1;
    }
    return 1;
}

/* The page modules the status pages load (0020CDC0 phase 3, the ITEM root
 * and the SPR4 part pages). */
static int page_module(unsigned module)
{
    return (module >= 0x1E && module <= 0x24) || (module >= 0x2C && module <= 0x31);
}

static int begin_module(EmStatusRuntime *runtime, unsigned module)
{
    /* Every page module runs the loader's own steps (docs/MODULE_LOADER.md
     * section 4): 001FF080(0, module) registers the slot-2 task 001FF0D0,
     * whose steps read the header, the one chunk (the page's texture
     * upload, sent to loader_chain below) and the payload (module 0x1E's
     * map model bank, relocated into slot 0x38 = D_0028A570) at host speed
     * (or with the recorded drive time, EM_PS2_DISC_DRIVE_TIMING, where a
     * recording exists: module 0x21's); its 0x63 step clears D_00275BD8,
     * which the page core, the ITEM root and SPR4 wait on. The busy byte
     * stays as the caller set it. Without a loader a page module faults
     * (no instant path is left). The ITEM root's (0x1F) and the BATTERY
     * page's (0x21) uploads replace the texture slot the port's ITEM and
     * BATTERY atlases stand in (released here, at the request). */
    if (page_module(module)) {
        if (!runtime->loader)
            return 0;
        if (module == 0x1F || module == 0x21) {
            em_item_ui_deactivate(runtime->item);
            em_battery_ui_deactivate(runtime->battery);
        }
        return em_module_loader_request_001FF080(runtime->loader, 0, (uint8_t)module) == 0;
    }
    if (runtime->pending_module >= 0 || !runtime->hooks.module_begin ||
        !runtime->hooks.module_ready ||
        runtime->hooks.module_begin(runtime->hooks.context, module) != 1)
        return 0;
    runtime->pending_module = (int)module;
    return module_ready(runtime);
}

static int collect_triangle(void *context, const int32_t xy[3][2], unsigned intensity)
{
    EmStatusRuntime *runtime = context;
    if (runtime->triangle_count >= 512)
        return 0;
    TrailTriangle *triangle = &runtime->triangles[runtime->triangle_count++];
    memcpy(triangle->xy, xy, sizeof triangle->xy);
    triangle->intensity = intensity;
    return 1;
}

static int other_tick(EmStatusRuntime *runtime)
{
    if (!runtime->hooks.other_page_tick || !runtime->hooks.other_page_render ||
        runtime->hooks.other_page_tick(runtime->hooks.context, &runtime->page, &runtime->input) !=
            1)
        return 0;
    runtime->draw_kind = DRAW_OTHER;
    return 1;
}

/* The UI block D_00810130 bytes EmStatusPage / EmItemRoot view. */
static void ui_load(uint8_t *ui, const EmStatusPage *page)
{
    ui[0x00] = page->active;
    ui[0x01] = page->phase;
    ui[0x02] = page->step;
    ui[0x03] = page->transition_step;
    ui[0x04] = page->item.state;
    ui[0x05] = page->item.step;
    ui[0x06] = page->item.next_state;
    for (unsigned b = 0; b < 4; ++b)
        ui[0x08 + b] = (uint8_t)((uint32_t)page->saved_module >> (8 * b));
    ui[0x0C] = page->saved_status;
    ui[0x10] = page->item.screen;
    ui[0x11] = page->item.hover;
    ui[0x15] = page->item.selected;
    ui[0x16] = page->item.module;
}

static void ui_store(const uint8_t *ui, EmStatusPage *page)
{
    page->active = ui[0x00];
    page->phase = ui[0x01];
    page->step = ui[0x02];
    page->transition_step = ui[0x03];
    page->item.state = ui[0x04];
    page->item.step = ui[0x05];
    page->item.next_state = ui[0x06];
    page->saved_module = (int32_t)((uint32_t)ui[0x08] | (uint32_t)ui[0x09] << 8 |
                                   (uint32_t)ui[0x0A] << 16 | (uint32_t)ui[0x0B] << 24);
    page->saved_status = ui[0x0C];
    page->item.screen = ui[0x10];
    page->item.hover = ui[0x11];
    page->item.selected = ui[0x15];
    page->item.module = ui[0x16];
}

/* The ITEM root's child page 5: 002149F0 through the battery_page hook
 * (em_battery_page_live), over the UI block, the request bytes, the
 * inventory view and the message view. */
static int battery_tick(EmStatusRuntime *runtime)
{
    EmStatusPage *page = &runtime->page;
    const EmStatusBatteryData *gauge = em_status_hub_ui_battery_data(runtime->hub);
    if (!runtime->hooks.battery_page || !runtime->message_view || !gauge)
        return 0;
    ui_load(runtime->ui, page);
    uint8_t counts[3], charge[2], capacity = runtime->inventory.capacity, pressed[2];
    memcpy(counts, runtime->inventory.battery_count, sizeof counts);
    charge[0] = (uint8_t)runtime->inventory.charge;
    charge[1] = (uint8_t)(runtime->inventory.charge >> 8);
    pressed[0] = (uint8_t)runtime->input.pressed;
    pressed[1] = (uint8_t)(runtime->input.pressed >> 8);
    const EmStatusBatteryPage call = {
        runtime->ui, sizeof runtime->ui, &page->request, &page->request_kind,
        &page->status_request, counts, charge, &capacity, pressed, runtime->input.held,
        runtime->input.repeat,
        {(int32_t *)(void *)&page->item.message_mode, (int32_t *)(void *)&page->item.message_phase,
         (int32_t *)(void *)&page->item.message_line, (int32_t *)(void *)&page->item.message_group},
        gauge, runtime->battery};
    int accepted = runtime->hooks.battery_page(runtime->hooks.context, &call) == 1;
    ui_store(runtime->ui, page);
    /* The page's charge and capacity writes (the acquisition writes both,
     * the discharge and recharge the charge). */
    const uint16_t new_charge = (uint16_t)(charge[0] | charge[1] << 8);
    if (capacity != runtime->inventory.capacity) {
        if (!runtime->hooks.write_battery_capacity ||
            runtime->hooks.write_battery_capacity(runtime->hooks.context, new_charge, capacity) !=
                1)
            return 0;
    } else if (new_charge != runtime->inventory.charge &&
               runtime->hooks.write_charge(runtime->hooks.context, new_charge) != 1) {
        return 0;
    }
    runtime->inventory.charge = new_charge;
    runtime->inventory.capacity = capacity;
    if (!accepted)
        return 0;
    if (em_battery_ui_count(runtime->battery))
        runtime->draw_kind = DRAW_BATTERY;
    return 1;
}

/* 001FF080(0, module) from inside a page (the SPR4 part pages). */
static int pages_module(void *context, uint32_t module)
{
    return begin_module(context, module);
}

/* One call of a status page (em_status_pages_live) over the canonical
 * bytes: the page core's views (the status block, the request bytes, the
 * message words) are stored before the call and loaded after it, as
 * battery_tick does. */
static int pages_tick(EmStatusRuntime *runtime, uint32_t address)
{
    EmStatusPage *page = &runtime->page;
    EmStatusPagesFrame frame;
    memset(&frame, 0, sizeof frame);
    if (!runtime->pages || !runtime->hooks.pages_frame || !runtime->message_view ||
        runtime->hooks.pages_frame(runtime->hooks.context, &frame) != 1 || !frame.req ||
        !frame.message)
        return 0;
    ui_load(runtime->ui, page);
    frame.ui = runtime->ui;
    frame.ui_clock = &runtime->ui_clock;
    frame.busy = &page->item.asset_busy;
    frame.held = runtime->input.held;
    frame.pressed = runtime->input.pressed;
    frame.repeat = runtime->input.repeat;
    frame.stick_x = runtime->input.stick_x;
    frame.stick_y = runtime->input.stick_y;
    frame.health_data = em_status_hub_ui_health_data(runtime->hub);
    frame.module_load = pages_module;
    frame.module_context = runtime;
    frame.math = &runtime->math;
    frame.trail = &runtime->trail;
    if (runtime->loader) { /* D_0028A570: the loader's slot 0x38 (one storage) */
        const EmStatusSceneLoader *ld = em_module_loader_state(runtime->loader);
        frame.d28A570 = &ld->d28A490[EM_STATUS_SCENE_SLOT_D_0028A570];
    }
    frame.req[EM_STATUS_PAGES_REQ_B0] = page->request;
    frame.req[EM_STATUS_PAGES_REQ_B1] = page->request_kind;
    frame.req[EM_STATUS_PAGES_REQ_C5] = page->status_request;
    frame.req[EM_STATUS_PAGES_REQ_CC] = page->restore_textures;
    frame.message->mode = (int32_t)page->item.message_mode;
    frame.message->phase = (int32_t)page->item.message_phase;
    frame.message->line = page->item.message_line;
    frame.message->aux_mode = (int32_t)page->item.message_group;
    const int result = em_status_pages_live_tick(runtime->pages, address, &frame);
    ui_store(runtime->ui, page);
    page->request = frame.req[EM_STATUS_PAGES_REQ_B0];
    page->request_kind = frame.req[EM_STATUS_PAGES_REQ_B1];
    page->status_request = frame.req[EM_STATUS_PAGES_REQ_C5];
    page->restore_textures = frame.req[EM_STATUS_PAGES_REQ_CC];
    page->item.message_mode = (uint32_t)frame.message->mode;
    page->item.message_phase = (uint32_t)frame.message->phase;
    page->item.message_line = frame.message->line;
    page->item.message_group = (uint32_t)frame.message->aux_mode;
    if (result < 0)
        return 0;
    runtime->draw_kind = DRAW_PAGE;
    return 1;
}

static int item_worker(void *context, EmItemRoot *item, EmItemRootEvent event, unsigned argument)
{
    EmStatusRuntime *runtime = context;
    switch (event) {
    case EM_ITEM_RESET_INPUT:
        em_item_trail_reset(&runtime->trail);
        return 1;
    case EM_ITEM_DRAW_BACKGROUND:
        return 1; /* Submitted together with the following original ordered root commands. */
    case EM_ITEM_DRAW_ROOT: {
        EmItemStick stick;
        if (!em_item_stick_sample(&stick, runtime->input.stick_x, runtime->input.stick_y,
                                  &runtime->math))
            return 0;
        if (em_item_root_hover(item, stick.magnitude, stick.angle) && !sound(runtime, 5))
            return 0;
        runtime->draw_hover = item->hover;
        runtime->triangle_count = 0;
        if (!em_item_stick_sample(&stick, runtime->input.stick_x, runtime->input.stick_y,
                                  &runtime->math) ||
            !em_item_trail_step(&runtime->trail, &stick, 248, 208, &runtime->math, collect_triangle,
                                runtime) ||
            runtime->triangle_count != 512)
            return 0;
        runtime->draw_kind = DRAW_ITEM;
        return 1;
    }
    case EM_ITEM_SOUND_BACK:
        return sound(runtime, 1);
    case EM_ITEM_SOUND_ACCEPT:
        return sound(runtime, 0);
    case EM_ITEM_LOAD_MODULE:
        return begin_module(runtime, argument);
    case EM_ITEM_CHILD_PAGE:
        /* 0020EE50 states 4..7: 00214570 EQUIPMENT, 002149F0 BATTERY,
         * 00215870 EVENT, 002160B0 HEALING. */
        if (argument == 5)
            return battery_tick(runtime);
        if (runtime->pages)
            return argument == 4   ? pages_tick(runtime, 0x00214570u)
                   : argument == 6 ? pages_tick(runtime, 0x00215870u)
                   : argument == 7 ? pages_tick(runtime, 0x002160B0u)
                                   : 0;
        return other_tick(runtime);
    }
    return 0;
}

/* em_status_hub's workers, in 0020CDC0 phase 1's order. */
static int hub_worker(void *context, EmStatusPage *page, EmStatusHubEvent event,
                      unsigned argument)
{
    EmStatusRuntime *runtime = context;
    switch (event) {
    case EM_STATUS_HUB_CLEAR_DRAW: /* 001AFEB0, as the page core's phase 3 calls it */
        return runtime->hooks.page_event(runtime->hooks.context, EM_STATUS_PAGE_CLEAR_DRAW, 0) ==
               1;
    case EM_STATUS_HUB_RESET_DRAW: /* 001AFE60 */
        return runtime->hooks.page_event(runtime->hooks.context, EM_STATUS_PAGE_RESET_DRAW, 0) ==
               1;
    case EM_STATUS_HUB_RESET_INPUT: /* 0020E020: the shared D_00821300 trail and D_00275C90 */
        em_item_trail_reset(&runtime->trail);
        return 1;
    case EM_STATUS_HUB_INSTALL_DRAW:
    case EM_STATUS_HUB_BUILD_MODELS:
    case EM_STATUS_HUB_ACTORS_TICK:
        return runtime->hooks.hub_models &&
               runtime->hooks.hub_models(runtime->hooks.context, event, argument) == 1;
    case EM_STATUS_HUB_BACKGROUND:
        /* 0020A7A0(0x20045EE59D421E40): stepped and drawn once, with this
         * frame's page (em_status_runtime_render). */
        return 1;
    case EM_STATUS_HUB_DRAW: /* 00209DF0, after 0020D930 set UI+0x11 */
        runtime->hub_display.hover = page->item.hover;
        if (!em_status_hub_ui_prepare(runtime->hub, &runtime->hub_display, &runtime->hub_stick,
                                      &runtime->ui_clock, &runtime->trail))
            return 0;
        runtime->draw_kind = DRAW_HUB;
        return 1;
    case EM_STATUS_HUB_SOUND:
        return sound(runtime, argument);
    }
    return 0;
}

/* 0020CDC0 phase 1 (em_status_hub). Phase 2, the health count-up, is
 * reached only from page id 8, which the first level never selects: it
 * faults. */
static int hub_tick(EmStatusRuntime *runtime)
{
    EmStatusPage *page = &runtime->page;
    if (page->phase != 1 || !runtime->hooks.hub_display)
        return 0;
    EmStatusHubDisplay display;
    memset(&display, 0, sizeof display);
    if (runtime->hooks.hub_display(runtime->hooks.context, &display) != 1 ||
        !em_item_stick_sample(&runtime->hub_stick, runtime->input.stick_x, runtime->input.stick_y,
                              &runtime->math))
        return 0;
    runtime->hub_display = display;
    if (em_status_hub_tick(page, display.infection, runtime->input.pressed, &runtime->hub_stick,
                           hub_worker, runtime) != 0)
        return 0;
    /* 001FCA10 mode 4 presents group 0's line while D_002821B4 == 1 (the
     * live route: step F itself, over the block's words). */
    runtime->draw_help = !runtime->message_view && runtime->draw_kind == DRAW_HUB &&
                                 page->item.message_mode == 4 && page->item.message_phase == 1 &&
                                 page->item.message_group == 0
                             ? (int)page->item.message_line
                             : -1;
    return 1;
}

static int page_worker(void *context, EmStatusPage *page, EmStatusPageEvent event,
                       unsigned argument)
{
    EmStatusRuntime *runtime = context;
    switch (event) {
    case EM_STATUS_PAGE_OPEN_SOUND:
        return sound(runtime, 0xB);
    case EM_STATUS_PAGE_CLOSE_SOUND:
        return sound(runtime, 0xD);
    case EM_STATUS_PAGE_BACK_SOUND:
        return sound(runtime, 1);
    case EM_STATUS_PAGE_LOAD:
        return begin_module(runtime, argument);
    case EM_STATUS_PAGE_ITEM_TICK: {
        int result = em_item_root_tick(&page->item, runtime->input.pressed, item_worker, runtime);
        runtime->draw_help = !runtime->message_view && page->item.message_phase == 1 &&
                                     page->item.message_group == 1
                                 ? (int)page->item.message_line
                                 : -1;
        return result == 0;
    }
    case EM_STATUS_PAGE_CONFIGURE:
        /* 0020DFA0 in the original order: 001AFE60 (the host's RESET_DRAW),
         * then 0020E020 (the shared trail D_00821300 / D_00275C90, the
         * runtime's), then the rest (001029C0, D_00810624, 0021BAC0,
         * 0021B9A0, 001D2610: the host's CONFIGURE). */
        if (runtime->hooks.page_event(runtime->hooks.context, EM_STATUS_PAGE_RESET_DRAW, 0) != 1)
            return 0;
        em_item_trail_reset(&runtime->trail);
        return runtime->hooks.page_event(runtime->hooks.context, event, argument) == 1;
    case EM_STATUS_PAGE_PAGE_TICK: {
        /* 0020CDC0 phase 3 sub-state 2: page id 1 MAP, 2 SPR4, 3 DATABASE. */
        static const uint32_t pages[4] = {0, 0x0020F950u, 0x00211970u, 0x00214020u};
        if (!runtime->pages || argument < 1 || argument > 3)
            return runtime->hooks.page_event(runtime->hooks.context, event, argument) == 1;
        return pages_tick(runtime, pages[argument]);
    }
    case EM_STATUS_PAGE_PLAYER_TEXTURE:
        /* 00200970(1): the library slot 0x35 and the player texture packet
         * go back into the GS memory a page module overwrote. */
        if (runtime->pages &&
            !em_gs_texture_apply(em_status_pages_live_gs(runtime->pages), EM_GS_TEXTURE_RESTORE))
            return 0;
        return runtime->hooks.page_event(runtime->hooks.context, event, argument) == 1;
    case EM_STATUS_PAGE_HUB_TICK:
        if (runtime->hub)
            return hub_tick(runtime);
        /* 0020CDC0 phases 1/2 sub-state 0 call 0020E020: the shared trail
         * D_00821300/D_00275C90 is reset before the hub's first frame. */
        if (page->step == 0)
            em_item_trail_reset(&runtime->trail);
        return other_tick(runtime);
    default:
        return runtime->hooks.page_event(runtime->hooks.context, event, argument) == 1;
    }
}

static int frame_worker(void *context, EmStatusFrameEvent event)
{
    EmStatusRuntime *runtime = context;
    if (event == EM_STATUS_RESET_UI) {
        EmStatusPage *page = &runtime->page;
        page->active = page->phase = page->step = page->transition_step = page->saved_status = 0;
        page->saved_module = 0;
        page->item.state = page->item.step = page->item.next_state = 0;
        page->item.screen = page->item.hover = page->item.selected = page->item.module = 0;
        memset(runtime->ui, 0, sizeof runtime->ui); /* 0020E060's 0xA0-byte memset */
        runtime->ui_clock = 0; /* UI+0x20 is inside 0020E060's 0xA0-byte memset */
    }
    return runtime->hooks.frame_event(runtime->hooks.context, event, &runtime->frame) == 1;
}

/* 0020CDC0 with the page's message words as a view of the binder's block
 * (D_002821B0 / B4 / B8 / D_00282240), loaded before and stored after,
 * when the binder supplies it (message_words); else the page's own copy. */
static int viewed_page_tick(EmStatusRuntime *runtime)
{
    EmStatusPage *page = &runtime->page;
    int32_t *words[4] = {NULL, NULL, NULL, NULL};
    if (runtime->d275BD8)
        page->item.asset_busy = *runtime->d275BD8;
    runtime->message_view = 0;
    if (runtime->hooks.message_words) {
        if (runtime->hooks.message_words(runtime->hooks.context, words) != 1 || !words[0] ||
            !words[1] || !words[2] || !words[3])
            return -1;
        runtime->message_view = 1;
        page->item.message_mode = (uint32_t)*words[0];
        page->item.message_phase = (uint32_t)*words[1];
        page->item.message_line = (uint32_t)*words[2];
        page->item.message_group = (uint32_t)*words[3];
    }
    int result = em_status_page_tick(page, runtime->input.pressed, page_worker, runtime);
    if (runtime->message_view) {
        *words[0] = (int32_t)page->item.message_mode;
        *words[1] = (int32_t)page->item.message_phase;
        *words[2] = (int32_t)page->item.message_line;
        *words[3] = (int32_t)page->item.message_group;
    }
    if (runtime->d275BD8)
        *runtime->d275BD8 = page->item.asset_busy;
    return result;
}

static int page_tick(void *context)
{
    return viewed_page_tick(context);
}

EmStatusRuntime *em_status_runtime_load(const char *battery_path, const char *item_path,
                                        const EmItemMath *math, const EmStatusRuntimeHooks *hooks)
{
    if (!math || !math->sine || !math->cosine || !math->atan2 || !math->sqrt || !hooks ||
        !hooks->read_inventory || !hooks->write_charge || !hooks->frame_event ||
        !hooks->page_event || !hooks->sound)
        return NULL;
    EmStatusRuntime *runtime = calloc(1, sizeof *runtime);
    if (!runtime)
        return NULL;
    runtime->frame.phase = 1;
    runtime->pending_module = -1;
    runtime->math = *math;
    runtime->hooks = *hooks;
    runtime->battery = em_battery_ui_load(battery_path);
    runtime->item = em_item_ui_load(item_path);
    if (!runtime->battery || !runtime->item) {
        em_status_runtime_free(runtime);
        return NULL;
    }
    return runtime;
}

void em_status_runtime_free(EmStatusRuntime *runtime)
{
    if (!runtime)
        return;
    em_status_runtime_bind_loader(runtime, NULL);
    em_battery_ui_free(runtime->battery);
    em_item_ui_free(runtime->item);
    em_status_hub_ui_free(runtime->hub);
    em_status_pages_live_free(runtime->pages);
    free(runtime);
}

int em_status_runtime_bind_pages(EmStatusRuntime *runtime, EmStatusPagesLive *pages)
{
    if (!runtime || !pages || runtime->pages || !runtime->hooks.pages_frame) {
        em_status_pages_live_free(pages);
        return 0;
    }
    runtime->pages = pages;
    return 1;
}

void em_status_runtime_bind_busy(EmStatusRuntime *runtime, uint8_t *d275BD8)
{
    if (runtime)
        runtime->d275BD8 = d275BD8;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The chain a page module's chunk step sends (001FF3F0 state 3, 00200830
 * of D_00275C74). Every page module's header (INDEX.IDX sector = module)
 * has one chunk (h[0x0E] = 1), no B section (h[0x10] = 0): the chunk is
 * the page's texture upload, one VIF1 chain of whole-page PSMCT32
 * transfers. docs/MODULE_LOADER.md finding 1 and section 3 I prove that
 * each chunk's upload writes exactly the blocks and bytes of that
 * module's step of the status pages' GS memory (em_gs_texture, exported
 * from the same disc bytes), and that the ITEM atlas decodes from module
 * 0x1F's upload and the BATTERY atlas from module 0x21's. With the status
 * pages bound the upload is applied here as that step; without them the
 * resident ITEM / BATTERY atlases already hold their texels (and the other
 * pages cannot open). Any other chain is refused (fail-stop). */
/* The game-over screen module 0x27 (001AD4E0 step 1's 001FF080(0, 0x27);
 * docs/DAMAGE.md section 5): one chunk, the screen's GS upload, with no B
 * section, as a page module's. The port draws the screen from its export
 * of that upload (assets/startup/game_over.emui, tools/export_game_over.py);
 * the chunk step records that the load delivered it. */
static uint32_t s_game_over_chunks;

uint32_t em_status_runtime_game_over_chunks(void) { return s_game_over_chunks; }

static int loader_chain(void *context, uint32_t chain, const uint8_t *bytes, uint32_t size)
{
    EmStatusRuntime *runtime = context;
    const EmTask *record = em_module_loader_record(runtime->loader);
    const EmStatusSceneLoader *ld = em_module_loader_state(runtime->loader);
    if (bytes && record && ld && record->user[0] == 0 && record->user[6] == EM_STATUS_RUNTIME_GAME_OVER_MODULE &&
        chain == ld->d275C74) {
        const uint8_t *h = ld->header;
        if (le32(h) != record->user[6] || (h[0x0E] | h[0x0F] << 8) != 1 || le32(h + 0x10) != 0 ||
            size < le32(h + 0x24))
            return -1;
        ++s_game_over_chunks;
        return 0;
    }
    if (!bytes || !record || !ld || record->user[0] != 0 || !page_module(record->user[6]) ||
        chain != ld->d275C74)
        return -1;
    const uint8_t *h = ld->header;
    if (le32(h) != record->user[6] || (h[0x0E] | h[0x0F] << 8) != 1 || le32(h + 0x10) != 0 ||
        size < le32(h + 0x24))
        return -1;
    if (runtime->pages &&
        !em_gs_texture_apply(em_status_pages_live_gs(runtime->pages), record->user[6]))
        return -1;
    return 0;
}

int em_status_runtime_bind_loader(EmStatusRuntime *runtime, EmModuleLoader *loader)
{
    if (!runtime)
        return 0;
    if (runtime->loader)
        em_module_loader_set_chain_hook(runtime->loader, NULL, NULL);
    runtime->loader = loader;
    if (loader)
        em_module_loader_set_chain_hook(loader, loader_chain, runtime);
    return 1;
}

int em_status_runtime_bind_hub(EmStatusRuntime *runtime, EmStatusHubUI *ui)
{
    if (!runtime || !ui || runtime->hub) {
        em_status_hub_ui_free(ui);
        return 0;
    }
    runtime->hub = ui;
    return 1;
}

uint32_t em_status_runtime_ui_clock(const EmStatusRuntime *runtime)
{
    return runtime ? runtime->ui_clock : 0;
}

/* The pages share one UI texture slot (EM_GFX_OVERLAY_TEX_UI): the page
 * that draws marks the others' uploads stale. */
static void release_slot(EmStatusRuntime *runtime)
{
    switch (runtime->slot_kind) {
    case DRAW_BATTERY:
        em_battery_ui_deactivate(runtime->battery);
        break;
    case DRAW_ITEM:
        em_item_ui_deactivate(runtime->item);
        break;
    case DRAW_HUB:
        em_status_hub_ui_deactivate(runtime->hub);
        break;
    case DRAW_PAGE:
        em_status_pages_live_deactivate(runtime->pages);
        break;
    default:
        break;
    }
    runtime->slot_kind = DRAW_NONE;
}

static void claim_slot(EmStatusRuntime *runtime, unsigned kind)
{
    if (runtime->slot_kind != kind)
        release_slot(runtime);
    runtime->slot_kind = kind;
}

int em_status_runtime_open(EmStatusRuntime *runtime)
{
    if (!runtime || runtime->failed || runtime->queued || runtime->frame.phase != 1 ||
        !runtime->hooks.other_page_tick || !runtime->hooks.other_page_render ||
        runtime->page.request || runtime->page.status_request)
        return 0;
    runtime->owner = NULL;
    runtime->queued = 1;
    return 1;
}

int em_status_runtime_battery_open(EmStatusRuntime *runtime, EmPanel *owner, uint8_t request)
{
    if (!runtime || !owner || runtime->failed || runtime->queued || runtime->frame.phase != 1 ||
        owner->cost != 2 || request != 0x80 + owner->cost)
        return 0;
    runtime->owner = owner;
    runtime->page.request = 1;
    runtime->page.request_kind = request;
    runtime->queued = 1;
    return 1;
}

int em_status_runtime_pickup_request(EmStatusRuntime *runtime, uint8_t kind, uint8_t index)
{
    if (!runtime || runtime->failed || runtime->queued || runtime->frame.phase != 1 || kind != 1 ||
        index < 0x1B || index >= 0x1E || !runtime->hooks.write_battery_capacity)
        return 0;
    runtime->owner = NULL;
    runtime->page.request = kind;
    runtime->page.request_kind = index;
    runtime->queued = 1;
    return 1;
}

int em_status_runtime_tick(EmStatusRuntime *runtime, const EmStatusInput *input)
{
    if (!runtime || !input || runtime->failed)
        return -1;
    runtime->draw_kind = DRAW_NONE;
    runtime->consumed = 0;
    runtime->rendered = 0;
    runtime->input = *input;
    if (!runtime->queued && !em_status_frame_active(&runtime->frame))
        return 0;
    runtime->consumed = 1;
    if (runtime->hooks.read_inventory(runtime->hooks.context, &runtime->inventory) != 1 ||
        runtime->inventory.charge > 255 || !module_ready(runtime))
        return fail(runtime);
    runtime->page.current_status = runtime->inventory.status;
    runtime->page.inventory_primary = runtime->inventory.primary;
    runtime->page.inventory_secondary = runtime->inventory.secondary;
    runtime->frame.audio_busy = input->audio_busy;
    if (runtime->queued) {
        runtime->queued = 0;
        if (em_status_frame_enter(&runtime->frame, frame_worker, runtime) < 0)
            return fail(runtime);
        return 1;
    }
    int result = em_status_frame_tick(&runtime->frame, frame_worker, page_tick, runtime);
    if (result < 0)
        return fail(runtime);
    if (result == 1) {
        release_slot(runtime);
        em_battery_ui_deactivate(runtime->battery);
        em_item_ui_deactivate(runtime->item);
        em_status_hub_ui_deactivate(runtime->hub);
        em_status_pages_live_deactivate(runtime->pages);
        runtime->owner = NULL;
    }
    return 1;
}

int em_status_runtime_page_open(EmStatusRuntime *runtime, EmPanel *owner)
{
    if (!runtime || runtime->failed || runtime->queued || em_status_frame_active(&runtime->frame))
        return fail(runtime);
    runtime->draw_kind = DRAW_NONE;
    runtime->rendered = 0;
    runtime->owner = owner;
    /* frame_worker's RESET_UI: the page reset plus the host's event. */
    return frame_worker(runtime, EM_STATUS_RESET_UI) ? 1 : fail(runtime);
}

int em_status_runtime_page_tick(EmStatusRuntime *runtime, const EmStatusInput *input, uint8_t *b0,
                                uint8_t *b1, uint8_t *c5, uint8_t *cc)
{
    if (!runtime || !input || !b0 || !b1 || !c5 || !cc || runtime->failed || runtime->queued ||
        em_status_frame_active(&runtime->frame))
        return fail(runtime);
    runtime->draw_kind = DRAW_NONE;
    runtime->rendered = 0;
    runtime->input = *input;
    if (runtime->hooks.read_inventory(runtime->hooks.context, &runtime->inventory) != 1 ||
        runtime->inventory.charge > 255 || !module_ready(runtime))
        return fail(runtime);
    EmStatusPage *page = &runtime->page;
    page->current_status = runtime->inventory.status;
    page->inventory_primary = runtime->inventory.primary;
    page->inventory_secondary = runtime->inventory.secondary;
    page->request = *b0;
    page->request_kind = *b1;
    page->status_request = *c5;
    page->restore_textures = *cc;
    int result = viewed_page_tick(runtime);
    *b0 = page->request;
    *b1 = page->request_kind;
    *c5 = page->status_request;
    *cc = page->restore_textures;
    if (result < 0 || result > 1)
        return fail(runtime);
    if (result == 1) {
        release_slot(runtime);
        em_battery_ui_deactivate(runtime->battery);
        em_item_ui_deactivate(runtime->item);
        em_status_hub_ui_deactivate(runtime->hub);
        em_status_pages_live_deactivate(runtime->pages);
        runtime->owner = NULL;
    }
    return result;
}

static int render_trail(void *context, EmGfx *gfx, float x, float y, float u, float v)
{
    EmStatusRuntime *runtime = context;
    if (x != 248 || y != 208 || runtime->triangle_count != 512)
        return 0;
    for (unsigned i = 0; i < runtime->triangle_count; ++i) {
        const TrailTriangle *triangle = &runtime->triangles[i];
        float xy[3][2];
        for (unsigned vertex = 0; vertex < 3; ++vertex) {
            xy[vertex][0] = triangle->xy[vertex][0] / 16.0f - 1792;
            xy[vertex][1] = (triangle->xy[vertex][1] / 16.0f - 1936) * 2;
        }
        float intensity = triangle->intensity / 255.0f;
        const float color[3][4] = {
            {intensity, intensity, intensity, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
        if (!em_gfx_overlay_triangle(gfx, xy, color, u, v, EM_GFX_UI_ADD))
            return 0;
    }
    return 1;
}

int em_status_runtime_render(EmStatusRuntime *runtime, EmGfx *gfx)
{
    if (!runtime || !gfx || runtime->failed)
        return -1;
    if (runtime->rendered)
        return 1;
    runtime->rendered = 1;
    int result = 1;
    if (runtime->draw_kind != DRAW_NONE)
        claim_slot(runtime, runtime->draw_kind);
    switch (runtime->draw_kind) {
    case DRAW_HUB: {
        /* 0020CDC0 phase 1 step 1 draws 0020A7A0 with the hub tile first,
         * then 001B0000's model packets, then 00209DF0: the background
         * layer is flushed before the models so they composite over it,
         * and the 2D layer follows them in end_frame's overlay pass. */
        float tile[4];
        result = em_status_hub_ui_bind(runtime->hub, gfx) &&
                 em_status_hub_ui_tile(runtime->hub, HUB_BACKGROUND_TEX0, tile);
        if (result) {
            em_gfx_overlay_canvas(gfx, EM_GFX_STATUS_W, EM_GFX_STATUS_H);
            em_status_background_frame(gfx);
            result = em_status_background_render(gfx, tile[0], tile[1], tile[2], tile[3]);
            em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
            em_gfx_overlay_backdrop_flush(gfx);
        }
        result = result && runtime->hooks.hub_models_draw &&
                 runtime->hooks.hub_models_draw(runtime->hooks.context, gfx) == 1;
        result = result && em_status_hub_ui_render(runtime->hub, gfx, runtime->draw_help);
        break;
    }
    case DRAW_BATTERY:
        result = em_battery_ui_render(runtime->battery, gfx);
        break;
    case DRAW_ITEM:
        result = em_item_ui_render(runtime->item, gfx, runtime->draw_hover, runtime->draw_help,
                                   render_trail, runtime);
        break;
    case DRAW_OTHER:
        result = runtime->hooks.other_page_render(runtime->hooks.context, gfx, &runtime->page);
        break;
    case DRAW_PAGE:
        result = em_status_pages_live_render(runtime->pages, gfx);
        break;
    default:
        break;
    }
    return result == 1 ? 1 : fail(runtime);
}

int em_status_runtime_ordinary_enabled(const EmStatusRuntime *runtime)
{
    return runtime && !runtime->failed && !runtime->consumed &&
           !em_status_frame_active(&runtime->frame);
}

const EmStatusFrame *em_status_runtime_frame(const EmStatusRuntime *runtime)
{
    return runtime ? &runtime->frame : NULL;
}

const EmStatusPage *em_status_runtime_page(const EmStatusRuntime *runtime)
{
    return runtime ? &runtime->page : NULL;
}
