#include "game/em_status_runtime.h"
#include "game/em_battery_ui.h"
#include "game/em_item_ui.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    int32_t xy[3][2];
    unsigned intensity;
} TrailTriangle;

enum { DRAW_NONE, DRAW_BATTERY, DRAW_ITEM, DRAW_OTHER };

struct EmStatusRuntime {
    EmStatusFrame frame;
    EmStatusPage page;
    EmStatusInput input;
    EmStatusInventory inventory;
    EmStatusRuntimeHooks hooks;
    EmItemMath math;
    EmItemTrail trail;
    EmPanel *owner;
    EmBatteryUI *battery;
    EmItemUI *item;
    TrailTriangle triangles[512];
    unsigned triangle_count, draw_kind, draw_hover;
    int draw_help, battery_kind, pending_module;
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
        runtime->page.item.asset_busy = 0;
        runtime->pending_module = -1;
    }
    return 1;
}

static int begin_module(EmStatusRuntime *runtime, unsigned module)
{
    /* Native modules1F/21 are the actual parsed original artwork and text,
     * already validated at load. No fake asynchronous timer is needed. */
    if (module == 0x1F || module == 0x21) {
        em_item_ui_deactivate(runtime->item);
        em_battery_ui_close(runtime->battery);
        runtime->page.item.asset_busy = 0;
        return 1;
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

static int battery_tick(EmStatusRuntime *runtime)
{
    EmStatusPage *page = &runtime->page;
    if (!page->item.step) {
        runtime->battery_kind = -1;
        for (int kind = 2; kind >= 0; --kind) {
            if (runtime->inventory.battery_count[kind]) {
                runtime->battery_kind = kind;
                break;
            }
        }
        if (runtime->battery_kind < 0)
            return 0; /* No fabricated list row for an unsupported empty inventory. */
        page->item.message_mode = 4;
        page->item.message_phase = 0;
        page->item.message_group = 3;
        if (page->request) {
            if (page->request == 1 && page->request_kind >= 0x1B && page->request_kind < 0x1E) {
                static const uint8_t capacities[3] = {12, 36, 48};
                int acquired_kind = page->request_kind - 0x1B;
                uint8_t capacity = capacities[acquired_kind];
                if (!runtime->hooks.write_battery_capacity ||
                    !em_battery_ui_begin_pickup(runtime->battery, capacity, runtime->battery_kind,
                                                acquired_kind) ||
                    runtime->hooks.write_battery_capacity(runtime->hooks.context, capacity,
                                                          capacity) != 1)
                    return 0;
                runtime->inventory.charge = runtime->inventory.capacity = capacity;
                page->item.message_group = acquired_kind == runtime->battery_kind ? 4 : 3;
                page->request = 0;
                page->item.step = 3;
                return 1;
            }
            if (!(page->request_kind & 0x80) ||
                !em_battery_ui_begin(runtime->battery, runtime->owner, runtime->inventory.charge,
                                     runtime->battery_kind))
                return 0;
            page->request = 0;
            page->item.step = 4;
            page->item.next_state = 1;
            return 1; /* Original state0 request branch does not draw this callback. */
        }
        if (!em_battery_ui_begin_browse(runtime->battery, runtime->owner, runtime->inventory.charge,
                                        runtime->battery_kind))
            return 0;
        page->item.step = 1;
    }
    unsigned before = em_battery_ui_original_step(runtime->battery);
    int available = 1;
    if (before == 1 && (runtime->input.pressed & 0x40) && !(runtime->input.pressed & 0x20)) {
        available = runtime->hooks.owner_available(runtime->hooks.context, runtime->owner,
                                                   0x1B + (unsigned)runtime->battery_kind);
        if (available < 0 || available > 1)
            return 0;
        if (available && !runtime->owner)
            return 0; /* A different eligible device requires its actual owner binding. */
    }
    int charge = runtime->inventory.charge;
    unsigned events =
        em_battery_ui_tick(runtime->battery, runtime->input.pressed, &charge, available);
    if (events & EM_BATTERY_UNSUPPORTED_OWNER)
        return 0;
    if (charge != runtime->inventory.charge) {
        if (charge < 0 || charge > 255 ||
            runtime->hooks.write_charge(runtime->hooks.context, (uint16_t)charge) != 1)
            return 0;
        runtime->inventory.charge = (uint16_t)charge;
    }
    if ((events & EM_PANEL_MENU_CURSOR) && !sound(runtime, 4))
        return 0;
    if ((events & EM_PANEL_MENU_UNIT_SOUND) && !sound(runtime, 6))
        return 0;
    if ((events & EM_PANEL_MENU_ACCEPT) && !sound(runtime, 0))
        return 0;
    if ((events & EM_PANEL_MENU_CANCEL) && !sound(runtime, 1))
        return 0;
    if ((events & EM_BATTERY_NO_DEVICE_SOUND) && !sound(runtime, 2))
        return 0;
    if (events & EM_BATTERY_BACK_TO_STATUS) {
        page->item.message_phase = 2;
        page->phase = 3;
        page->step = page->transition_step = page->item.state = page->item.step = 0;
        em_battery_ui_close(runtime->battery);
        return 1;
    }
    runtime->draw_kind = DRAW_BATTERY;
    page->item.step = (uint8_t)em_battery_ui_original_step(runtime->battery);
    if (page->item.step == 6 && before == 4)
        page->request = 1; /* Original confirmation protects the discharge from outer exit. */
    if (page->item.step == 1 && before != 1) {
        page->item.message_phase = before == 3 ? 1 : 0;
        page->item.message_group = 3;
    } else if (before == 6) {
        page->item.message_phase = 0;
    } else if (before == 3) {
        page->item.message_phase = 1;
        page->item.message_line = 27 + (unsigned)runtime->battery_kind;
    } else {
        page->item.message_phase = 1;
        page->item.message_group = before == 1 ? 3 : 5;
        page->item.message_line = before == 1   ? 27 + (unsigned)runtime->battery_kind
                                  : before == 4 ? 8
                                  : before == 5 ? 9
                                                : 25;
    }
    if (events & EM_PANEL_MENU_FINISHED) {
        page->status_request = 0xFF;
        if (runtime->hooks.battery_finished(runtime->hooks.context, runtime->owner) != 1)
            return 0;
    }
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
        return argument == 5 ? battery_tick(runtime) : other_tick(runtime);
    }
    return 0;
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
        runtime->draw_help = page->item.message_phase == 1 && page->item.message_group == 1
                                 ? (int)page->item.message_line
                                 : -1;
        return result == 0;
    }
    case EM_STATUS_PAGE_HUB_TICK:
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
    }
    return runtime->hooks.frame_event(runtime->hooks.context, event, &runtime->frame) == 1;
}

static int page_tick(void *context)
{
    EmStatusRuntime *runtime = context;
    return em_status_page_tick(&runtime->page, runtime->input.pressed, page_worker, runtime);
}

EmStatusRuntime *em_status_runtime_load(const char *battery_path, const char *item_path,
                                        const EmItemMath *math, const EmStatusRuntimeHooks *hooks)
{
    if (!math || !math->sine || !math->cosine || !math->atan2 || !math->sqrt || !hooks ||
        !hooks->read_inventory || !hooks->write_charge || !hooks->frame_event ||
        !hooks->page_event || !hooks->sound || !hooks->owner_available || !hooks->battery_finished)
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
    em_battery_ui_free(runtime->battery);
    em_item_ui_free(runtime->item);
    free(runtime);
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
        em_battery_ui_close(runtime->battery);
        em_item_ui_deactivate(runtime->item);
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
    int result = em_status_page_tick(page, input->pressed, page_worker, runtime);
    *b0 = page->request;
    *b1 = page->request_kind;
    *c5 = page->status_request;
    *cc = page->restore_textures;
    if (result < 0 || result > 1)
        return fail(runtime);
    if (result == 1) {
        em_battery_ui_close(runtime->battery);
        em_item_ui_deactivate(runtime->item);
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
    switch (runtime->draw_kind) {
    case DRAW_BATTERY:
        result = em_battery_ui_render(runtime->battery, gfx, runtime->inventory.capacity,
                                      runtime->input.held);
        break;
    case DRAW_ITEM:
        result = em_item_ui_render(runtime->item, gfx, runtime->draw_hover, runtime->draw_help,
                                   render_trail, runtime);
        break;
    case DRAW_OTHER:
        result = runtime->hooks.other_page_render(runtime->hooks.context, gfx, &runtime->page);
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
