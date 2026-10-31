#include "game/em_snow_runtime.h"
#include "game/em_collision_world.h"
#include "game/em_diag.h"
#include "game/em_frame.h"
#include "game/em_random.h"
#include "game/em_render_context_live.h"
#include "game/em_sdk_math_original.h"
#include "game/em_snow.h"
#include "game/em_weather_packets.h"
#include "game/em_aim_fire_sdk_memory.h"
#include "game/em_camera_live.h"
#include "game/em_effects_live.h"
#include "game/em_level9_port.h"
#include "game/em_level10_port.h"
#include "game/em_stream_lanes_original.h"

#include <stdio.h>
#include <string.h>

static struct {
    EmSnowConfig config;
    EmSnowTile tiles[EM_SNOW_TILE_COUNT];
    unsigned flags;
    int loaded;
    EmSnowRuntimeLog log;
} snow;

void em_snow_runtime_clear(void)
{
    memset(&snow, 0, sizeof snow);
}

int em_snow_runtime_load(const char *scene_dir, const char *config, unsigned flags)
{
    char path[1024];
    em_snow_runtime_clear();
    /* Only the recovered first-level snow branch is connected. Other
     * weather variants require their own original renderer and data. */
    if ((flags & 0x0e000070U) != 0x10U) return 0;
    if (snprintf(path, sizeof path, "%s/%s", scene_dir, config) >= (int)sizeof path ||
        !em_snow_config_load(&snow.config, path)) return 0;
    snow.flags = flags;
    snow.loaded = 1;
    return 1;
}

static uint32_t weather_random(void *context)
{
    (void)context;
    return em_random_next();
}

/* The render context's storage, by original address. */
static const uint8_t *mem_read(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    return em_rcl_bytes(address, size);
}

static uint8_t *mem_write(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    return em_rcl_bytes_mut(address, size);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return b;
}

static uint32_t fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* 0011E2A8 on the one SDK context (em_collision_world_sdk). */
static int sdk_sine(void *ctx, float x, float *result)
{
    (void)ctx;
    EmSdkMathContext *sdk = em_collision_world_sdk();
    if (!sdk || em_sdk_math_original_w_0011E2A8(sdk, x, result) < 0 || sdk->fault) return -1;
    return 0;
}

static int refuse(const char *what, uint32_t address)
{
    fprintf(stderr, "snow: %s (at %08X)\n", what, (unsigned)address);
    return -1;
}

int em_snow_runtime_tick_actor(EmWeather *weather, const float eye[3], unsigned selector,
                               unsigned transition, unsigned fade_state)
{
    if (!snow.loaded) return 0;
    EmWeatherFrame frame = em_weather_tick(weather, snow.flags, 0x0b00, selector,
                                           transition, fade_state, weather_random, NULL);
    if (frame.released)
        return 1;
    if (frame.draw != 1) return 0;
    /* 001E55F0 state 1, intensity above 0: the channel-3 cursor it reads
     * before 001E67C0 (the list's start). */
    const uint8_t *d275674 = em_rcl_bytes(0x00275674u, 4);
    const uint8_t *cursor = em_rcl_bytes(EM_RCL_CONTEXT + 0x1Cu, 4);
    if (!d275674 || !cursor) return refuse("the render context is not loaded", 0x00275670u);
    const EmWeatherMem mem = { NULL, mem_read, mem_write, EM_RCL_CONTEXT, rd32(d275674) };
    const uint32_t start = rd32(cursor);
    /* 001E67C0: the fog programmer (0021B9A0 on the one render context)
     * moves the range to near 0, far 300 for the whole emission:
     * 0021B9A0(2, 0.0, 0.0), then 0021B9A0(3, 0.0, 300.0); every tile's
     * 001CFFE0 copies the context's +0xA0 quadword into its packet;
     * 0021B9A0(1, 0.0, 0.0) restores the mode-1 pair at the end. */
    if (em_rcl_0021B9A0(2, 0, 0) < 0 || em_rcl_0021B9A0(3, 0, UINT32_C(0x43960000)) < 0)
        return refuse("001E67C0's 0021B9A0 faulted (the render context)", 0x0021B9A0u);
    if (em_snow_tiles(weather, &snow.config, frame.strength, eye, sdk_sine, NULL, snow.tiles) < 0)
        return refuse("001E67C0's SDK 00102900 or 0011E2A8 faulted", 0x001E67C0u);
    uint32_t fault = 0, first = 0;
    uint32_t p3 = 0, uniform = 1;
    uint8_t p3_first[0x100];
    for (unsigned i = 0; i < EM_SNOW_TILE_COUNT; ++i) {
        const EmSnowTile *tile = &snow.tiles[i];
        /* 001CFAE0(state, 0, 0x700036A0, f12 phase, f13 seed + 0.0001,
         * f14 1.0, f15 0.000001) and 001CFFE0(3, 3, D_00255170, state):
         * em_snow's params hold the four in the VU59 order. */
        uint8_t matrix[64], obj[0x90];
        uint32_t vu59[4];
        memcpy(matrix, tile->matrix, sizeof matrix);
        memcpy(obj, tile->descriptor, sizeof obj);
        for (unsigned k = 0; k < 4; ++k) vu59[k] = bits(tile->params[k]);
        const uint8_t *before = em_rcl_bytes(EM_RCL_CONTEXT + 0x1Cu, 4);
        const uint32_t at = before ? rd32(before) : 0;
        if (em_weather_packets_tile(&mem, obj, vu59, matrix, &fault) < 0)
            return refuse("001CFFE0 could not write the tile's packets", fault);
        const uint8_t *packet3 = em_rcl_bytes(at + 0x30u, 0x100);
        if (!packet3) return refuse("001CFFE0's packet 3 is not in the arena", at + 0x30u);
        if (i == 0) {
            first = at;
            memcpy(p3_first, packet3, sizeof p3_first);
            p3 = em_diag_digests() ? fnv(packet3, 0x100) : 0u;   /* the tick log's only */
        } else if (memcmp(packet3, p3_first, sizeof p3_first) != 0) {
            uniform = 0;
        }
    }
    if (em_rcl_0021B9A0(1, 0, 0) < 0)
        return refuse("001E67C0's 0021B9A0(1) faulted (the render context)", 0x0021B9A0u);
    /* 001E55F0: the RET tag, then 001D2DE0(0, start) when start != 0. */
    uint32_t pending = 0;
    if (em_weather_packets_close(&mem, start, &pending, &fault) < 0)
        return refuse("001E55F0 could not close the list", fault);
    if (pending != 0 && em_rcl_001D2DE0(0, pending) < 0)
        return refuse("001E55F0's 001D2DE0 faulted (the render context)", 0x001D2DE0u);
    snow.log.frame = em_frame_counter();
    snow.log.lists++;
    snow.log.start = first;
    snow.log.tiles = EM_SNOW_TILE_COUNT;
    snow.log.p3_digest = p3;
    snow.log.p3_uniform = uniform;
    return 0;
}

/* ---- 001E5AC0 (flags & 0x0C000060): AREA13's outdoor strips ---------------
 *
 * em_level9_port_001E5AC0, its one translation, over the render context's
 * storage, the node's block (an image of *weather at its +0x1F0 offsets), the
 * boot data D_00254F30..D_002551BF it edits in place (the two descriptors and
 * the row tables, one process-lifetime block as the original's .data), a
 * private stack and private scratchpad words (only this function reads them). */
#define WX_DATA 0x00254F30u
#define WX_DATA_SIZE 0x290u
#define WX_SPAD 0x70003000u
#define WX_SPAD_SIZE 0xC00u
#define WX_STACK_TOP 0x01FFD000u
#define WX_STACK_SIZE 0x200u
static uint8_t s_wx_data[WX_DATA_SIZE];
static int s_wx_data_ready;
static uint8_t s_wx_spad[WX_SPAD_SIZE];
static uint8_t s_wx_stack[WX_STACK_SIZE];
static uint8_t s_wx_block[0x44];
static uint32_t s_wx_self;

static uint8_t *wx_inside(uint32_t a, uint32_t n, uint32_t base, uint32_t size, uint8_t *bytes)
{
    return a >= base && n <= size && a - base <= size - n ? bytes + (a - base) : NULL;
}
static uint8_t *wx_bytes(void *ctx, uint32_t a, uint32_t n)
{
    (void)ctx;
    uint8_t *p = wx_inside(a, n, s_wx_self + 0x1F0u, sizeof s_wx_block, s_wx_block);
    if (!p) p = wx_inside(a, n, WX_STACK_TOP - WX_STACK_SIZE, WX_STACK_SIZE, s_wx_stack);
    if (!p) p = wx_inside(a, n, WX_DATA, WX_DATA_SIZE, s_wx_data);
    if (!p) p = em_rcl_bytes_mut(a, n);
    if (!p) p = (uint8_t *)(uintptr_t)em_rcl_bytes(a, n);
    if (!p) p = (uint8_t *)em_camera_live_bytes(a, n);
    if (!p) p = wx_inside(a, n, WX_SPAD, WX_SPAD_SIZE, s_wx_spad);
    return p;
}
static void *wx_map(void *ctx, uint32_t a, size_t n, int write)
{
    (void)write;
    return n > 0x1000u ? NULL : wx_bytes(ctx, a, (uint32_t)n);
}
static int wx_leaf(EmAimFireTargetCall *f)
{
    return em_aim_fire_sdk_memory_call(NULL, wx_map, f) == 0 ? 0 : -1;
}
static int wx_2(uint32_t fn, uint32_t a0, uint32_t a1)
{
    EmAimFireTargetCall f = {fn, 0, {a0, a1}, {0}, 2, 0, 0, 0};
    return wx_leaf(&f);
}
static int wx_3(uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2)
{
    EmAimFireTargetCall f = {fn, 0, {a0, a1, a2}, {0}, 3, 0, 0, 0};
    return wx_leaf(&f);
}
static int wx_f(uint32_t fn, uint32_t a0, uint32_t a1, float x)
{
    EmAimFireTargetCall f = {fn, 0, {a0, a1}, {bits(x)}, 2, 1, 0, 0};
    return wx_leaf(&f);
}
static int wx_001029C0(void *c, uint32_t a0)
{
    (void)c;
    EmAimFireTargetCall f = {0x001029C0u, 0, {a0}, {0}, 1, 0, 0, 0};
    return wx_leaf(&f);
}
static int wx_001026A0(void *c, uint32_t a0, uint32_t a1, uint32_t a2) { (void)c; return wx_3(0x001026A0u, a0, a1, a2); }
static int wx_001028B8(void *c, uint32_t a0, uint32_t a1, uint32_t a2) { (void)c; return wx_3(0x001028B8u, a0, a1, a2); }
static int wx_00102798(void *c, uint32_t a0, uint32_t a1) { (void)c; return wx_2(0x00102798u, a0, a1); }
static int wx_00102948(void *c, uint32_t a0, uint32_t a1) { (void)c; return wx_2(0x00102948u, a0, a1); }
static int wx_00102900(void *c, uint32_t a0, uint32_t a1, float x) { (void)c; return wx_f(0x00102900u, a0, a1, x); }
static int wx_00102B08(void *c, uint32_t a0, uint32_t a1, float x) { (void)c; return wx_f(0x00102B08u, a0, a1, x); }
static int wx_00103230(void *c, uint32_t a0, uint32_t a1, float x) { (void)c; return wx_f(0x00103230u, a0, a1, x); }
static int wx_0011E2A8(void *c, float x, float *out) { return sdk_sine(c, x, out); }
static int wx_00128250(void *c, float x, int32_t *out)
{
    (void)c;
    *out = (int32_t)em_stream_lanes_00128250(bits(x));
    return 0;
}
static int wx_001281C0(void *c, float x, int32_t *out)
{
    (void)c;
    *out = em_stream_lanes_001281C0(bits(x));
    return 0;
}
static int wx_0021B9A0(void *c, int32_t a0, float x, float y)
{
    (void)c;
    return em_rcl_0021B9A0(a0, bits(x), bits(y));
}
static int wx_001CFAE0(void *c, uint32_t dst, int32_t a1, uint32_t src, float f12, float f13, float f14, float f15)
{
    (void)c;
    const uint8_t *m = wx_bytes(NULL, src, 64);
    uint8_t *d = wx_bytes(NULL, dst, sizeof(EmWeatherDrawState));
    if (!m || !d) return -1;
    const uint8_t *ctxw = em_rcl_bytes(0x00275670u, 4);
    if (!ctxw) return -1;
    EmWeatherDrawState st;
    em_weather_packets_001CFAE0(&st, a1, m, bits(f12), bits(f13), bits(f14), bits(f15), rd32(ctxw));
    memcpy(d, &st, sizeof st);
    return 0;
}
static int wx_001CFFE0(void *c, int32_t slot, int32_t variant, uint32_t obj, uint32_t src, int32_t a4)
{
    (void)c;
    (void)a4;                       /* 001CFFE0 reads four registers */
    const uint8_t *d275674 = em_rcl_bytes(0x00275674u, 4), *o = wx_bytes(NULL, obj, 0x90),
                  *s = wx_bytes(NULL, src, sizeof(EmWeatherDrawState));
    if (!d275674 || !o || !s) return -1;
    const EmWeatherMem mem = {NULL, mem_read, mem_write, EM_RCL_CONTEXT, rd32(d275674)};
    EmWeatherDrawState st;
    memcpy(&st, s, sizeof st);
    uint32_t fault = 0;
    return em_weather_packets_001CFFE0(&mem, slot, (uint32_t)variant, o, &st, &fault);
}
static int wx_001E6F60(void *c, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t n4, int32_t n5, int32_t n6)
{
    (void)c;
    EmLevel10PortHooks h;
    memset(&h, 0, sizeof h);
    h.bytes = wx_bytes;
    EmLevel10PortFault f = {0, 0};
    return em_level10_port_001E6F60(&h, n0, n1, n2, n3, n4, n5, n6, &f);
}

int em_snow_runtime_tick_outdoor(EmWeather *weather, uint32_t self, uint32_t flags, unsigned area, unsigned sub,
                                 unsigned selector, unsigned transition, unsigned fade_state, uint8_t *d8106BF)
{
    EmWeatherFrame frame = em_weather_tick(weather, flags, (area << 8) + sub, selector, transition, fade_state,
                                           weather_random, NULL);
    if (frame.released) return 1;
    if (weather->state != 1 && weather->state != 3) return 0;
    if (!(flags & 0x0e000070U)) return 0;
    *d8106BF = frame.intensity;
    if (frame.draw == 0) return 0;
    if (frame.draw != 2) return refuse("001E67C0 outside AREA11 (no exported snow config)", 0x001E67C0u);
    if (!s_wx_data_ready) {
        const uint8_t *elf = em_effects_live_window(WX_DATA, WX_DATA_SIZE);
        if (!elf) return refuse("the boot rows D_00254F30.. are not exported", WX_DATA);
        memcpy(s_wx_data, elf, WX_DATA_SIZE);
        s_wx_data_ready = 1;
    }
    const uint8_t *d275674 = em_rcl_bytes(0x00275674u, 4);
    const uint8_t *cursor = em_rcl_bytes(EM_RCL_CONTEXT + 0x1Cu, 4);
    if (!d275674 || !cursor) return refuse("the render context is not loaded", 0x00275670u);
    const EmWeatherMem mem = {NULL, mem_read, mem_write, EM_RCL_CONTEXT, rd32(d275674)};
    const uint32_t start = rd32(cursor);
    s_wx_self = self;
    memcpy(s_wx_block, weather->phase, 24);
    memcpy(s_wx_block + 0x18, weather->drift, 24);
    memcpy(s_wx_block + 0x30, &weather->intensity, 4);
    memcpy(s_wx_block + 0x34, &weather->target, 4);
    memcpy(s_wx_block + 0x38, &weather->rate, 4);
    memcpy(s_wx_block + 0x3C, &weather->seed, 4);
    memcpy(s_wx_block + 0x40, &weather->burst_wait, 4);
    EmLevel9PortHooks h;
    memset(&h, 0, sizeof h);
    h.bytes = wx_bytes;
    h.w_001026A0 = wx_001026A0; h.w_001028B8 = wx_001028B8; h.w_00102798 = wx_00102798;
    h.w_00102900 = wx_00102900; h.w_00102948 = wx_00102948; h.w_001029C0 = wx_001029C0;
    h.w_00102B08 = wx_00102B08; h.w_00103230 = wx_00103230; h.w_0011E2A8 = wx_0011E2A8;
    h.w_00128250 = wx_00128250; h.w_001281C0 = wx_001281C0; h.w_0021B9A0 = wx_0021B9A0;
    h.w_001CFAE0 = wx_001CFAE0; h.w_001CFFE0 = wx_001CFFE0; h.w_001E6F60 = wx_001E6F60;
    EmLevel9PortFault fault9 = {0, 0};
    const int flags9 = frame.render_flags;
    int32_t seed;
    memcpy(&seed, &weather->seed, 4);
    if (em_level9_port_001E5AC0(&h, self, flags9, seed, frame.strength, WX_STACK_TOP, &fault9) < 0) {
        fprintf(stderr, "snow: em_level9_port_001E5AC0 fault code %d at %08X\n", (int)fault9.code,
                (unsigned)fault9.address);
        return -1;
    }
    memcpy(weather->phase, s_wx_block, 24);
    memcpy(weather->drift, s_wx_block + 0x18, 24);
    uint32_t fault = 0, pending = 0;
    if (em_weather_packets_close(&mem, start, &pending, &fault) < 0)
        return refuse("001E55F0 could not close the list", fault);
    if (pending != 0 && em_rcl_001D2DE0(0, pending) < 0)
        return refuse("001E55F0's 001D2DE0 faulted (the render context)", 0x001D2DE0u);
    return 0;
}

void em_snow_runtime_log(EmSnowRuntimeLog *out)
{
    if (out) *out = snow.log;
}
