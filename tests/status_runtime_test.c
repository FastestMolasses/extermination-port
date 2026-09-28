#include "game/em_battery_page_live.h"
#include "game/em_hud.h"
#include "game/em_status_hub.h"
#include "game/em_message_live.h"
#include "game/em_status_runtime.h"
#include "game/em_effect_original.h"
#include "game/em_owner_services_original.h"
#include "game/em_render_context_live.h"
#include "game/em_status_models.h"
#include "game/em_module_loader.h"
#include "game/em_task.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    EmStatusInventory inventory;
    unsigned sounds[256], sound_count, triangle_count, upload_count;
    unsigned module, begins, ready, writes, frame_events, page_events;
    int fail_write;
    unsigned hub_workers, hub_draws;
    /* The BATTERY page's records and host (battery_page below). */
    EmPanel *owner;
    int32_t words[4];     /* D_002821B0 / B4 / B8, D_00282240 */
    uint8_t spad3B8D;     /* 0x70003B8D */
    uint32_t device;      /* what 00185420 returns */
    unsigned units, presents, page_calls, sprites;
} World;

static World *drawing;
/* The status pages' message entries (em_message_live): this fixture binds
 * no status pages, so reaching them is a fault. */
int em_message_live_record_draw(int32_t line, int32_t x, int32_t y)
{
    (void)line;
    (void)x;
    (void)y;
    return -1;
}
int em_message_live_fe070(const EmMessageBank *bank, int32_t index, int32_t x, int32_t y,
                          int32_t *result)
{
    (void)bank;
    (void)index;
    (void)x;
    (void)y;
    (void)result;
    return -1;
}
/* The MAP page's workers (em_status_pages_live: the UI pool em_status_models,
 * the SDK leaves, the render context, the 2D flush before the pool's
 * models): this fixture binds no status pages and no UI pool, so reaching
 * one is a fault. */
int em_status_models_call(EmStatusModels *m, uint32_t target, const uint64_t *a, unsigned na,
                          uint64_t *v0)
{
    (void)m; (void)target; (void)a; (void)na; (void)v0;
    assert(!"em_status_models_call without a UI pool");
    return -1;
}
unsigned em_status_models_free_slots(const EmStatusModels *m) { (void)m; assert(!"no UI pool"); return 0; }
uint8_t *em_status_models_pool_bytes(EmStatusModels *m) { (void)m; assert(!"no UI pool"); return NULL; }
uint8_t *em_status_models_view_bytes(EmStatusModels *m) { (void)m; assert(!"no UI pool"); return NULL; }
void em_status_models_set_node(EmStatusModels *m, uint32_t fn, EmStatusModelsNodeFn node, void *ctx)
{
    (void)m; (void)fn; (void)node; (void)ctx;
    assert(!"em_status_models_set_node without a UI pool");
}
void em_effect_original_001026A0(float out[4], const float m[16], const float v[4])
{
    (void)out; (void)m; (void)v;
    assert(!"001026A0 outside MAP");
}
int em_owner_services_identity_001029C0(float m[16]) { (void)m; assert(!"001029C0 outside MAP"); return -1; }
int em_owner_services_rotate_y_00102BB0(float dst[16], const float src[16], uint32_t angle)
{
    (void)dst; (void)src; (void)angle;
    assert(!"00102BB0 outside MAP");
    return -1;
}
const uint8_t *em_rcl_bytes(uint32_t address, uint32_t size) { (void)address; (void)size; return NULL; }
void em_gfx_overlay_decor_flush(EmGfx *g)
{
    (void)g;
    assert(!"em_gfx_overlay_decor_flush without the MAP page");
}
int em_gfx_overlay_texture_set(EmGfx *g, int slot, const uint8_t *p, uint32_t w, uint32_t h)
{
    (void)g;
    assert(slot == 1 && p && w && h);
    ++drawing->upload_count;
    return 1;
}
void em_gfx_overlay_canvas(EmGfx *g, float w, float h)
{
    (void)g;
    (void)w;
    (void)h;
}
void em_hud_decor_invalidate(void)
{
}
int em_status_background_render(struct EmGfx *g, float u, float v, float w, float h)
{
    (void)g;
    (void)u;
    (void)v;
    (void)w;
    (void)h;
    return 1;
}
/* The hub's ordered 2D layer: this fixture never binds the hub (its hub
 * pages run the other_page hooks), so nothing may flush it. */
void em_gfx_overlay_backdrop_flush(EmGfx *g)
{
    (void)g;
    assert(!"em_gfx_overlay_backdrop_flush outside the bound hub");
}
void em_status_background_frame(struct EmGfx *g)
{
    (void)g;
}
void em_hud_text(EmGfx *g, float x, float y, const char *s, EmHudTextStyle style)
{
    (void)g;
    (void)x;
    (void)y;
    (void)s;
    (void)style;
}
void em_hud_text_color(EmGfx *g, float x, float y, const char *s, EmHudTextStyle style, uint32_t c)
{
    (void)g;
    (void)x;
    (void)y;
    (void)s;
    (void)style;
    (void)c;
}
float em_hud_text_width(const char *s, EmHudTextStyle style)
{
    (void)s;
    (void)style;
    return 0;
}
void em_gfx_overlay_sprite(EmGfx *g, float x, float y, float w, float h, float u, float v, float u1,
                           float v1, const float color[4])
{
    (void)g;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)u;
    (void)v;
    (void)u1;
    (void)v1;
    (void)color;
    ++drawing->sprites;
}
void em_gfx_overlay_sprite_blend(EmGfx *g, float x, float y, float w, float h, float u, float v,
                                 float u1, float v1, const float color[4], EmGfxOverlayBlend mode)
{
    (void)mode;
    em_gfx_overlay_sprite(g, x, y, w, h, u, v, u1, v1, color);
}
int em_gfx_overlay_triangle(EmGfx *g, const float xy[3][2], const float rgba[3][4], float u,
                            float v, EmGfxOverlayBlend blend)
{
    (void)g;
    (void)xy;
    (void)u;
    (void)v;
    assert(blend == EM_GFX_UI_ADD && rgba[0][0] <= 32 / 255.0f);
    assert(rgba[1][0] == 0 && rgba[2][0] == 0);
    ++drawing->triangle_count;
    return 1;
}

static float sine(void *c, float x)
{
    (void)c;
    return sinf(x);
}
static float cosine(void *c, float x)
{
    (void)c;
    return cosf(x);
}
static float root(void *c, float x)
{
    (void)c;
    return sqrtf(x);
}
static float angle(void *c, float y, float x)
{
    (void)c;
    return atan2f(y, x);
}
static int inventory(void *c, EmStatusInventory *out)
{
    *out = ((World *)c)->inventory;
    return 1;
}
static int write_charge(void *c, uint16_t charge)
{
    World *world = c;
    if (world->fail_write)
        return 0;
    world->inventory.charge = charge;
    ++world->writes;
    return 1;
}
static int write_capacity(void *c, uint16_t charge, uint8_t capacity)
{
    World *world = c;
    if (!write_charge(c, charge))
        return 0;
    world->inventory.capacity = capacity;
    return 1;
}
static int frame_event(void *c, EmStatusFrameEvent event, const EmStatusFrame *state)
{
    (void)event;
    (void)state;
    ++((World *)c)->frame_events;
    return 1;
}
static int page_event(void *c, EmStatusPageEvent event, unsigned argument)
{
    (void)event;
    (void)argument;
    ++((World *)c)->page_events;
    return 1;
}
static int sound(void *c, uint32_t cue)
{
    World *world = c;
    assert(world->sound_count < 256);
    world->sounds[world->sound_count++] = cue;
    return 1;
}
static int begin_module(void *c, unsigned module)
{
    World *world = c;
    world->module = module;
    ++world->begins;
    return 1;
}
static int ready_module(void *c, unsigned module)
{
    World *world = c;
    assert(module == world->module);
    return world->ready;
}

/* The ITEM > BATTERY page: the original 002149F0 with its bound draws
 * (em_battery_page_live), as the AREA11 host binds it, over this fixture's
 * records. The owner record is the fixture panel at the captured address
 * 0x7AA590 (+3 type 0x24, +0x34 cost, +0xA / +0xB); 00185420 returns
 * world->device. */
#define PANEL_ADDRESS 0x7AA590u
static int page_sound(void *c, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    World *world = c;
    assert(a1 == 0x1000 && a2 == 0x1000 && a3 == 0x1000 && id >= 0);
    if (id == 6)
        ++world->units;
    return sound(c, (uint32_t)id) == 1 ? 0 : -1;
}
static int page_find(void *c, int32_t item, uint32_t *owner)
{
    assert(item == 0x1B);
    *owner = ((World *)c)->device;
    return 0;
}
static int page_read(void *c, uint32_t owner, uint32_t offset, uint32_t size, int32_t *value)
{
    World *world = c;
    assert(owner == PANEL_ADDRESS && world->owner);
    if (offset == 3 && size == 1) {
        *value = 0x24;
        return 0;
    }
    if (offset == 0x34 && size == 2) {
        *value = (int16_t)world->owner->cost;
        return 0;
    }
    return -1;
}
static int page_write(void *c, uint32_t owner, uint32_t offset, uint8_t value)
{
    World *world = c;
    assert(owner == PANEL_ADDRESS && world->owner);
    if (offset == 0xA)
        world->owner->charged = value;
    else if (offset == 0xB)
        world->owner->armed = value;
    else
        return -1;
    return 0;
}
static int page_present(void *c, int32_t x, int32_t y, int32_t group, int32_t line)
{
    (void)x;
    (void)y;
    (void)group;
    (void)line;
    ++((World *)c)->presents;
    return 0;
}
static int battery_page(void *c, const EmStatusBatteryPage *p)
{
    World *world = c;
    static const uint8_t d0[4] = {0x90, 0xA5, 0x7A, 0x00}; /* D_008106D0 = PANEL_ADDRESS */
    const EmBatteryPageCall call = {
        {p->ui, p->ui_size, p->b0, p->b1, p->c5, d0, p->d810C7F, p->d810CB2, p->d810CB7,
         p->d810E74, p->words[0], p->words[1], p->words[2], p->words[3], &world->spad3B8D},
        p->held, p->repeat, p->gauge, p->draw};
    const EmBatteryPageHost host = {world, page_sound, page_find, page_read, page_write,
                                    page_present};
    EmSprFault fault;
    ++world->page_calls;
    return em_battery_page_live_tick(&host, &call, &fault) == 0 ? 1 : 0;
}
static int message_words(void *c, int32_t *words[4])
{
    World *world = c;
    for (unsigned i = 0; i < 4; ++i)
        words[i] = &world->words[i];
    return 1;
}

static int hub_worker(void *context, EmStatusPage *page, EmStatusHubEvent event, unsigned argument)
{
    (void)page;
    World *world = context;
    ++world->hub_workers;
    if (event == EM_STATUS_HUB_SOUND)
        return sound(context, argument);
    return 1; /* Explicit draw/model boundary in this lifecycle fixture. */
}

static int hub_tick(void *context, EmStatusPage *page, const EmStatusInput *input)
{
    EmItemMath math = {NULL, sine, cosine, angle, root};
    EmItemStick stick;
    return em_item_stick_sample(&stick, input->stick_x, input->stick_y, &math) &&
           em_status_hub_tick(page, 0, input->pressed, &stick, hub_worker, context) == 0;
}

static int hub_render(void *context, EmGfx *gfx, const EmStatusPage *page)
{
    (void)gfx;
    (void)page;
    ++((World *)context)->hub_draws;
    return 1; /* Renderer is separately validated; do not invent a game host. */
}

static const char *const *asset_paths; /* battery.emba, item_root.emir, hub records, atlas,
                                        * the module loader's sectors */

/* Module 0x21's load runs the screen-module loader's own steps
 * (docs/MODULE_LOADER.md): the fixture binds one live, with D_00275BD8 in
 * s_bd8, and dispatches the task table after each status tick (step E
 * follows the game task's status frame in the original main loop). */
static EmModuleLoader *s_loader;
static uint8_t s_bd8;

/* pickup: 0 the panel request 0x82, 1 the pickup request 1/0x1B, 2 the
 * normal hub route. page: bind the original BATTERY page (battery_page,
 * message_words and the hub's records for 00209280's gauge text). */
static EmStatusRuntime *create(World *world, EmPanel *owner, int pickup, int page)
{
    *world = (World){
        .inventory = {.battery_count = {1, 0, 0}, .charge = 12, .capacity = 12, .status = 1},
        .owner = owner,
        .device = PANEL_ADDRESS,
    };
    drawing = world;
    EmItemMath math = {NULL, sine, cosine, angle, root};
    EmStatusRuntimeHooks hooks = {.context = world,
                                  .read_inventory = inventory,
                                  .write_charge = write_charge,
                                  .frame_event = frame_event,
                                  .page_event = page_event,
                                  .sound = sound,
                                  .module_begin = begin_module,
                                  .module_ready = ready_module,
                                  .write_battery_capacity = write_capacity};
    if (pickup == 2) {
        hooks.other_page_tick = hub_tick;
        hooks.other_page_render = hub_render;
    }
    if (page) {
        hooks.battery_page = battery_page;
        hooks.message_words = message_words;
    }
    EmStatusRuntime *runtime =
        em_status_runtime_load(asset_paths[0], asset_paths[1], &math, &hooks);
    assert(runtime);
    if (!s_loader) {
        s_loader = em_module_loader_open(asset_paths[4]);
        assert(s_loader);
        const EmModuleLoaderViews views = {&s_bd8, NULL, NULL, NULL, NULL, NULL};
        em_module_loader_set_views(s_loader, &views);
        em_module_loader_bind_live(s_loader);
    }
    em_task_init();
    s_bd8 = 0;
    em_status_runtime_bind_busy(runtime, &s_bd8);
    assert(em_status_runtime_bind_loader(runtime, s_loader));
    if (page)
        assert(em_status_runtime_bind_hub(
            runtime, em_status_hub_ui_load(asset_paths[2], asset_paths[3], &math)));
    em_panel_init(owner, 0);
    if (pickup == 2) {
        assert(em_status_runtime_open(runtime));
        assert(!em_status_runtime_open(runtime));
    } else if (pickup) {
        world->inventory.status = 0;
        world->inventory.primary = 0xFF;
        assert(!em_status_runtime_pickup_request(runtime, 2, 0x1B));
        assert(!em_status_runtime_pickup_request(runtime, 1, 0x17));
        assert(em_status_runtime_pickup_request(runtime, 1, 0x1B));
    } else {
        assert(!em_status_runtime_open(runtime)); /* Missing actual hub workers. */
        assert(em_status_runtime_battery_open(runtime, owner, 0x82));
    }
    assert(!em_status_runtime_battery_open(runtime, owner, 0x82));
    assert(em_status_runtime_ordinary_enabled(runtime));
    return runtime;
}

static int tick(EmStatusRuntime *runtime, unsigned pressed, uint8_t x, uint8_t y)
{
    EmStatusInput input = {.pressed = pressed, .stick_x = x, .stick_y = y};
    int result = em_status_runtime_tick(runtime, &input);
    em_task_dispatch();
    assert(!em_module_loader_failed(s_loader, NULL));
    return result;
}

/* The panel request's first frames: 002149F0 state 0 takes the request
 * (B1 & 0x80) into the confirmation, state 4 with the cursor on No. The
 * ITEM root's module-0x21 load in between runs the loader's 10 host-speed
 * dispatches (MODULE_LOADER.md finding 2), 9 frames more than the instant
 * load this fixture counted before (7). */
static void confirmation(EmStatusRuntime *runtime)
{
    for (unsigned i = 0; i < 16; ++i) {
        assert(tick(runtime, 0, 128, 128) == 1);
        assert(!em_status_runtime_ordinary_enabled(runtime));
    }
    const EmStatusPage *page = em_status_runtime_page(runtime);
    assert(page->phase == 3 && page->step == 2 && page->item.state == 5 && page->item.step == 4);
    assert(page->request == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 6);
    asset_paths = (const char *const *)(argv + 1);
    World world;
    EmPanel owner;

    /* Yes: the discharge (units at 1 and 31), the owner's +0xA / +0xB, the
     * mode byte 3 and the exit request, then the status frame's release. */
    EmStatusRuntime *runtime = create(&world, &owner, 0, 1);
    confirmation(runtime);
    /* State 4 draws the confirmation from its second call on. */
    assert(tick(runtime, 0, 128, 128) == 1 && em_status_runtime_page(runtime)->item.step == 4);
    assert(em_status_runtime_render(runtime, (EmGfx *)1) == 1 && world.sprites && world.presents);
    assert(tick(runtime, 0x8040, 128, 128) == 1);
    assert(em_status_runtime_page(runtime)->request == 1);
    for (unsigned i = 1; i <= 61; ++i) {
        assert(tick(runtime, 0, 128, 128) == 1);
        assert(world.inventory.charge == (i < 31 ? 10 : 8));
        assert(world.units == (i < 31 ? 1u : 2u));
        assert(owner.charged == (i == 61) && owner.armed == (i == 61 ? 5 : 0));
    }
    assert(world.writes == 2 && world.spad3B8D == 3);
    for (unsigned i = 0; i < 4; ++i) {
        assert(tick(runtime, 0, 128, 128) == 1);
        assert(!em_status_runtime_ordinary_enabled(runtime));
    }
    assert(em_status_runtime_frame(runtime)->phase == 1);
    assert(tick(runtime, 0, 128, 128) == 0 && em_status_runtime_ordinary_enabled(runtime));
    assert(!world.begins);
    em_status_runtime_free(runtime);

    /* Default No; Back into the ITEM root (its trail render), BATTERY
     * again, No again, then Back out to the broader hub, whose missing
     * display worker faults with ownership kept. */
    runtime = create(&world, &owner, 0, 1);
    confirmation(runtime);
    assert(tick(runtime, 0x40, 128, 128) == 1); /* Default No. */
    assert(!owner.charged && world.inventory.charge == 12);
    assert(tick(runtime, 0x20, 128, 128) == 1); /* Browse Back enters ITEM load. */
    for (unsigned i = 0; i < 5; ++i)
        assert(tick(runtime, 0, 128, 128) == 1);
    assert(em_status_runtime_page(runtime)->item.state == 1);
    assert(em_status_runtime_render(runtime, (EmGfx *)1) == 1);
    assert(world.triangle_count == 512);
    assert(em_status_runtime_render(runtime, (EmGfx *)1) == 1 && world.triangle_count == 512);
    /* Up selects original ITEM wedge3; reopening BATTERY starts browsing
     * (after the module-0x21 load's 10 loader dispatches: 4 + 9 frames). */
    assert(tick(runtime, 0x40, 128, 0) == 1);
    for (unsigned i = 0; i < 13; ++i)
        assert(tick(runtime, 0, 128, 128) == 1);
    assert(em_status_runtime_page(runtime)->item.step == 1);
    assert(tick(runtime, 0x40, 128, 128) == 1);
    assert(em_status_runtime_page(runtime)->item.step == 4);
    assert(tick(runtime, 0x40, 128, 128) == 1); /* Again defaults to No. */
    assert(!owner.charged && world.inventory.charge == 12);
    assert(tick(runtime, 0x20, 128, 128) == 1);
    for (unsigned i = 0; i < 5; ++i)
        assert(tick(runtime, 0, 128, 128) == 1);
    assert(tick(runtime, 0x20, 128, 128) == 1); /* Root Back -> broader hub. */
    assert(tick(runtime, 0, 128, 128) == 1);
    assert(tick(runtime, 0, 128, 128) == 1);
    assert(tick(runtime, 0, 128, 128) == -1); /* No hub display worker: fault. */
    assert(em_status_runtime_page(runtime)->phase == 1 &&
           !em_status_runtime_ordinary_enabled(runtime));
    em_status_runtime_free(runtime);

    /* The outer exit with inventory +0xCA6 set reloads module 0x32 first:
     * the page waits on it (50 pending frames), then releases. */
    runtime = create(&world, &owner, 0, 1);
    confirmation(runtime);
    assert(tick(runtime, 0x40, 128, 128) == 1);
    world.inventory.secondary = 1; /* Original exit now requires module32. */
    assert(tick(runtime, 0x10, 128, 128) == 1);
    assert(tick(runtime, 0, 128, 128) == 1);
    assert(world.begins == 1 && world.module == 0x32);
    for (unsigned i = 0; i < 50; ++i) {
        assert(tick(runtime, 0, 128, 128) == 1);
        assert(em_status_runtime_page(runtime)->step == 1);
    }
    world.ready = 1;
    assert(tick(runtime, 0, 128, 128) == 1);
    assert(tick(runtime, 0, 128, 128) == 1);
    assert(tick(runtime, 0, 128, 128) == 1);
    assert(tick(runtime, 0, 128, 128) == 0);
    em_status_runtime_free(runtime);

    /* A failing charge write during the discharge faults with ownership
     * and the inventory kept. */
    runtime = create(&world, &owner, 0, 1);
    confirmation(runtime);
    assert(tick(runtime, 0x8040, 128, 128) == 1);
    world.fail_write = 1;
    assert(tick(runtime, 0, 128, 128) == -1);
    assert(!owner.charged && world.inventory.charge == 12 && !world.writes &&
           !em_status_runtime_ordinary_enabled(runtime));
    em_status_runtime_free(runtime);

    /* The pickup request 1/0x1B: the acquisition writes charge and
     * capacity, its notice, browse, the empty device lookup (0020CD80's
     * cue 2) and the outer exit. */
    runtime = create(&world, &owner, 1, 1);
    world.device = 0;
    world.inventory.charge = world.inventory.capacity = 0; /* no pack before the pickup */
    for (unsigned i = 0; i < 16; ++i) /* 7 + the module-0x21 load's 9 more frames */
        assert(tick(runtime, 0, 128, 128) == 1);
    assert(em_status_runtime_page(runtime)->item.step == 3);
    assert(world.words[3] == 4);
    assert(world.writes == 1 && world.inventory.charge == 12 && world.inventory.capacity == 12);
    assert(!owner.charged);
    assert(tick(runtime, 0x40, 128, 128) == 1); /* Notice dismisses to browse. */
    assert(em_status_runtime_page(runtime)->item.step == 1);
    assert(world.words[3] == 3);
    assert(tick(runtime, 0x40, 128, 128) == 1); /* Actual empty device lookup. */
    assert(em_status_runtime_page(runtime)->item.step == 8);
    assert(world.sounds[world.sound_count - 1] == 2);
    assert(!owner.charged && world.inventory.charge == 12);
    assert(tick(runtime, 0x10, 128, 128) == 1); /* Real outer Triangle exit. */
    unsigned consumed = 0;
    while (tick(runtime, 0, 128, 128) == 1) {
        assert(!em_status_runtime_ordinary_enabled(runtime));
        assert(++consumed < 8);
    }
    assert(consumed == 3 && em_status_runtime_ordinary_enabled(runtime));
    assert(!world.begins);
    em_status_runtime_free(runtime);

    /* The pickup's capacity write fails: fault, inventory unchanged. */
    runtime = create(&world, &owner, 1, 1);
    world.inventory.charge = 7;
    world.inventory.capacity = 11;
    world.fail_write = 1;
    for (unsigned i = 0; i < 15; ++i) /* 6 + the module-0x21 load's 9 more frames */
        assert(tick(runtime, 0, 128, 128) == 1);
    assert(tick(runtime, 0, 128, 128) == -1);
    assert(!em_status_runtime_ordinary_enabled(runtime));
    assert(world.inventory.charge == 7 && world.inventory.capacity == 11 && !world.writes);
    em_status_runtime_free(runtime);

    /* Without a bound page, reaching the child page faults and keeps the
     * status screen's ownership. */
    runtime = create(&world, &owner, 1, 0);
    unsigned steps = 0;
    int result;
    while ((result = tick(runtime, 0, 128, 128)) == 1)
        assert(++steps < 20);
    assert(result == -1 && !em_status_runtime_ordinary_enabled(runtime));
    assert(em_status_runtime_page(runtime)->item.state == 5 && !world.writes && !world.page_calls);
    em_status_runtime_free(runtime);
    runtime = create(&world, &owner, 2, 0);
    steps = 0;
    while (em_status_runtime_page(runtime)->phase != 1 ||
           em_status_runtime_page(runtime)->step != 1) {
        assert(tick(runtime, 0, 128, 128) == 1 && ++steps < 20);
    }
    assert(!world.writes && !world.begins);
    assert(tick(runtime, 0x40, 0, 128) == 1); /* Actual hub left sector -> ITEM. */
    assert(em_status_runtime_page(runtime)->phase == 3);
    assert(em_status_runtime_page(runtime)->item.screen == 0);
    steps = 0;
    while (em_status_runtime_page(runtime)->item.state != 1 ||
           em_status_runtime_page(runtime)->step != 2) {
        assert(tick(runtime, 0, 128, 128) == 1 && ++steps < 20);
    }
    assert(tick(runtime, 0x20, 128, 128) == 1); /* ITEM Back, then actual hub. */
    steps = 0;
    while (em_status_runtime_page(runtime)->phase != 1 ||
           em_status_runtime_page(runtime)->step != 1) {
        assert(tick(runtime, 0, 128, 128) == 1 && ++steps < 20);
    }
    assert(tick(runtime, 0x10, 128, 128) == 1);
    assert(em_status_runtime_page(runtime)->phase == 5);
    steps = 0;
    while (tick(runtime, 0, 128, 128) == 1) {
        assert(!em_status_runtime_ordinary_enabled(runtime) && ++steps < 8);
    }
    assert(em_status_runtime_ordinary_enabled(runtime));
    assert(world.hub_workers && !world.writes && !owner.charged);
    em_status_runtime_free(runtime);
    em_module_loader_bind_live(NULL);
    em_module_loader_close(s_loader);
    puts("PASS original status adapter on the bound 002149F0: Yes/discharge, default No, "
         "Back/ITEM/reselect, module 0x32 reload gate, charge and capacity write faults, the "
         "pickup notice and empty lookup, the hub route, final-frame ownership");
}
