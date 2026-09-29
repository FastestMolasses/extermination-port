#include "game/em_snow_runtime.h"
#include "game/em_collision_world.h"
#include "game/em_frame.h"
#include "game/em_random.h"
#include "game/em_render_context_live.h"
#include "game/em_sdk_math_original.h"
#include "game/em_snow.h"
#include "game/em_weather_packets.h"

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
            p3 = fnv(packet3, 0x100);
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

void em_snow_runtime_log(EmSnowRuntimeLog *out)
{
    if (out) *out = snow.log;
}
