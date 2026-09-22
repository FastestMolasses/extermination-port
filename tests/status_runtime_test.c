#include "game/em_hud.h"
#include "game/em_status_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    EmStatusInventory inventory;
    unsigned sounds[256], sound_count, triangle_count, upload_count;
    unsigned finished, module, begins, ready, writes, frame_events, page_events;
    int fail_write, available;
} World;

static World *drawing;
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
void em_hud_background_sprite(EmGfx *g, float u, float v, float w, float h)
{
    (void)g;
    (void)u;
    (void)v;
    (void)w;
    (void)h;
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
static int available(void *c, EmPanel *owner, unsigned item)
{
    (void)owner;
    assert(item == 0x1B);
    return ((World *)c)->available;
}
static int finished(void *c, EmPanel *owner)
{
    World *world = c;
    assert(owner->charged == 1 && owner->armed == 5 && world->inventory.charge == 8);
    ++world->finished;
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

static EmStatusRuntime *create(World *world, EmPanel *owner, const char *battery, const char *item,
                               int pickup)
{
    *world = (World){
        .inventory = {.battery_count = {1, 0, 0}, .charge = 12, .capacity = 12, .status = 1},
        .available = 1};
    drawing = world;
    EmItemMath math = {NULL, sine, cosine, angle, root};
    EmStatusRuntimeHooks hooks = {.context = world,
                                  .read_inventory = inventory,
                                  .write_charge = write_charge,
                                  .frame_event = frame_event,
                                  .page_event = page_event,
                                  .sound = sound,
                                  .owner_available = available,
                                  .battery_finished = finished,
                                  .module_begin = begin_module,
                                  .module_ready = ready_module,
                                  .write_battery_capacity = write_capacity};
    EmStatusRuntime *runtime = em_status_runtime_load(battery, item, &math, &hooks);
    assert(runtime);
    em_panel_init(owner, 0);
    if (pickup) {
        world->available = 0;
        world->inventory.status = 0;
        world->inventory.primary = 0xFF;
        assert(!em_status_runtime_pickup_request(runtime, 2, 0x1B));
        assert(!em_status_runtime_pickup_request(runtime, 1, 0x17));
        assert(em_status_runtime_pickup_request(runtime, 1, 0x1B));
    } else {
        assert(em_status_runtime_battery_open(runtime, owner, 0x82));
    }
    assert(!em_status_runtime_battery_open(runtime, owner, 0x82));
    assert(em_status_runtime_ordinary_enabled(runtime));
    return runtime;
}

static int tick(EmStatusRuntime *runtime, unsigned pressed, uint8_t x, uint8_t y)
{
    EmStatusInput input = {.pressed = pressed, .stick_x = x, .stick_y = y};
    return em_status_runtime_tick(runtime, &input);
}

static void confirmation(EmStatusRuntime *runtime)
{
    for (unsigned i = 0; i < 7; ++i) {
        assert(tick(runtime, 0, 128, 128) == 1);
        assert(!em_status_runtime_ordinary_enabled(runtime));
    }
    const EmStatusPage *page = em_status_runtime_page(runtime);
    assert(page->phase == 3 && page->step == 2 && page->item.state == 5 && page->item.step == 4);
    assert(page->request == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    World world;
    EmPanel owner;
    EmStatusRuntime *runtime = create(&world, &owner, argv[1], argv[2], 0);
    confirmation(runtime);
    assert(tick(runtime, 0x8040, 128, 128) == 1);
    assert(em_status_runtime_page(runtime)->request == 1);
    for (unsigned i = 1; i <= 61; ++i) {
        assert(tick(runtime, 0, 128, 128) == 1);
        assert(world.inventory.charge == (i < 31 ? 10 : 8));
        assert(world.finished == (i == 61));
    }
    assert(world.writes == 2 && owner.charged && owner.armed == 5);
    for (unsigned i = 0; i < 4; ++i) {
        assert(tick(runtime, 0, 128, 128) == 1);
        assert(!em_status_runtime_ordinary_enabled(runtime));
    }
    assert(em_status_runtime_frame(runtime)->phase == 1);
    assert(tick(runtime, 0, 128, 128) == 0 && em_status_runtime_ordinary_enabled(runtime));
    assert(!world.begins);
    em_status_runtime_free(runtime);

    runtime = create(&world, &owner, argv[1], argv[2], 0);
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
    /* Up selects original ITEM wedge3; reopening BATTERY starts browsing. */
    assert(tick(runtime, 0x40, 128, 0) == 1);
    for (unsigned i = 0; i < 4; ++i)
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
    assert(tick(runtime, 0, 128, 128) == -1); /* Missing actual hub must fault. */
    assert(em_status_runtime_page(runtime)->phase == 1 &&
           !em_status_runtime_ordinary_enabled(runtime));
    em_status_runtime_free(runtime);

    runtime = create(&world, &owner, argv[1], argv[2], 0);
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

    runtime = create(&world, &owner, argv[1], argv[2], 0);
    confirmation(runtime);
    assert(tick(runtime, 0x8040, 128, 128) == 1);
    world.fail_write = 1;
    assert(tick(runtime, 0, 128, 128) == -1);
    assert(!owner.charged && world.inventory.charge == 12 &&
           !em_status_runtime_ordinary_enabled(runtime));
    em_status_runtime_free(runtime);
    runtime = create(&world, &owner, argv[1], argv[2], 1);
    for (unsigned i = 0; i < 7; ++i)
        assert(tick(runtime, 0, 128, 128) == 1);
    assert(em_status_runtime_page(runtime)->item.step == 3);
    assert(em_status_runtime_page(runtime)->item.message_group == 4);
    assert(world.writes == 1 && world.inventory.charge == 12 && world.inventory.capacity == 12);
    assert(!world.finished && !owner.charged);
    assert(tick(runtime, 0x40, 128, 128) == 1); /* Notice dismisses to browse. */
    assert(em_status_runtime_page(runtime)->item.step == 1);
    assert(em_status_runtime_page(runtime)->item.message_group == 3);
    assert(tick(runtime, 0x40, 128, 128) == 1); /* Actual empty device lookup. */
    assert(em_status_runtime_page(runtime)->item.step == 8);
    assert(world.sounds[world.sound_count - 1] == 2);
    assert(!world.finished && !owner.charged && world.inventory.charge == 12);
    assert(tick(runtime, 0x10, 128, 128) == 1); /* Real outer Triangle exit. */
    unsigned consumed = 0;
    while (tick(runtime, 0, 128, 128) == 1) {
        assert(!em_status_runtime_ordinary_enabled(runtime));
        assert(++consumed < 8);
    }
    assert(consumed == 3 && em_status_runtime_ordinary_enabled(runtime));
    assert(!world.finished && !world.begins);
    em_status_runtime_free(runtime);

    runtime = create(&world, &owner, argv[1], argv[2], 1);
    world.inventory.charge = 7;
    world.inventory.capacity = 11;
    world.fail_write = 1;
    for (unsigned i = 0; i < 6; ++i)
        assert(tick(runtime, 0, 128, 128) == 1);
    assert(tick(runtime, 0, 128, 128) == -1);
    assert(!em_status_runtime_ordinary_enabled(runtime));
    assert(world.inventory.charge == 7 && world.inventory.capacity == 11 && !world.writes);
    em_status_runtime_free(runtime);
    puts("PASS original status adapter: default No, Back/ITEM/reselect, real discharge/reload "
         "gates, final-frame ownership and fault retention");
}
