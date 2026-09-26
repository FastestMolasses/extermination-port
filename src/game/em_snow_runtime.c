#include "game/em_snow_runtime.h"
#include "game/em_render_context_live.h"
#include "game/em_snow.h"
#include "game/em_snow_particles.h"
#include "game/em_snow_projection.h"
#include "game/em_effect_color.h"
#include "game/em_random.h"
#include "em_math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SNOW_PARTICLE_COUNT = EM_SNOW_TILE_COUNT * 20 };
static struct {
    EmWeather weather;
    EmSnowConfig config;
    EmSnowTile tiles[EM_SNOW_TILE_COUNT];
    EmSnowParticle particles[SNOW_PARTICLE_COUNT];
    EmGfxParticle projected[SNOW_PARTICLE_COUNT];
    unsigned count;
    unsigned flags;
    int loaded;
    /* The render context's +0xA0 fog quadword as 001E67C0's 001CFFE0 reads
     * it: after its 0021B9A0(2, 0, 0) and 0021B9A0(3, 0, 300.0). */
    uint32_t fog[4];
} snow;

void em_snow_runtime_clear(EmGfx *gfx)
{
    em_gfx_particle_texture_set(gfx, NULL, 0, 0);
    memset(&snow, 0, sizeof snow);
}

int em_snow_runtime_load(EmGfx *gfx, const char *scene_dir,
                          const char *config, const char *texture,
                          unsigned flags)
{
    char path[1024];
    em_snow_runtime_clear(gfx);
    /* Only the recovered first-level snow branch is connected. Other
     * weather variants require their own original renderer and data. */
    if ((flags & 0x0e000070U) != 0x10U) return 0;
    if (snprintf(path, sizeof path, "%s/%s", scene_dir, config) >= (int)sizeof path ||
        !em_snow_config_load(&snow.config, path)) return 0;
    if (snprintf(path, sizeof path, "%s/%s", scene_dir, texture) >= (int)sizeof path)
        return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    uint32_t header[4];
    int valid = fread(header, sizeof header, 1, file) == 1 &&
                memcmp(header, "EMTX", 4) == 0 && header[1] == 1 &&
                header[2] > 0 && header[2] <= 256 &&
                header[3] > 0 && header[3] <= 256;
    if (!valid) { fclose(file); return 0; }
    size_t bytes = (size_t)header[2] * header[3] * 4;
    uint8_t *texels = malloc(bytes);
    valid = texels && fread(texels, bytes, 1, file) == 1 && fgetc(file) == EOF;
    fclose(file);
    if (valid) valid = em_gfx_particle_texture_set(gfx, texels, header[2], header[3]);
    free(texels);
    if (!valid) return 0;
    snow.flags = flags;
    snow.loaded = 1;
    return 1;
}

static uint32_t weather_random(void *context)
{
    (void)context;
    return em_random_next();
}

void em_snow_runtime_tick(const float eye[3], unsigned selector)
{
    (void)em_snow_runtime_tick_actor(&snow.weather, eye, selector, 0, 0);
}

int em_snow_runtime_tick_actor(EmWeather *weather, const float eye[3], unsigned selector,
                               unsigned transition, unsigned fade_state)
{
    if (!snow.loaded) {
        snow.count = 0;
        return 0;
    }
    EmWeatherFrame frame = em_weather_tick(weather, snow.flags, 0x0b00, selector,
                                           transition, fade_state, weather_random, NULL);
    if (frame.released)
        return 1;
    snow.count = 0;
    if (frame.draw != 1) return 0;
    /* 001E67C0: the fog programmer (em_packet_chain_0021B9A0 on the one
     * render context) moves the range to near 0, far 300 for the whole
     * emission: 0021B9A0(2, 0.0, 0.0), then 0021B9A0(3, 0.0, 300.0); every
     * tile's 001CFFE0 copies the context's +0xA0 quadword into its packet;
     * 0021B9A0(1, 0.0, 0.0) restores the mode-1 pair at the end. */
    const uint8_t *fog = NULL;
    if (em_rcl_0021B9A0(2, 0, 0) < 0 || em_rcl_0021B9A0(3, 0, UINT32_C(0x43960000)) < 0 ||
        !(fog = em_rcl_bytes(EM_RCL_CONTEXT + 0xA0u, 16))) {
        fprintf(stderr, "snow: 001E67C0's 0021B9A0 faulted (the render context)\n");
        return -1;
    }
    memcpy(snow.fog, fog, sizeof snow.fog);
    em_snow_tiles(weather, &snow.config, frame.strength, eye, snow.tiles);
    for (unsigned i = 0; i < EM_SNOW_TILE_COUNT; ++i) {
        const EmSnowTile *tile = &snow.tiles[i];
        int count = em_snow_particles_generate(tile->descriptor, snow.config.lookup,
            tile->params, tile->matrix, snow.particles + snow.count,
            SNOW_PARTICLE_COUNT - snow.count);
        if (count < 0) {
            fprintf(stderr, "snow: original particle generation failed\n");
            snow.loaded = 0;
            snow.count = 0;
            return 0;
        }
        snow.count += (unsigned)count;
    }
    if (em_rcl_0021B9A0(1, 0, 0) < 0) {
        fprintf(stderr, "snow: 001E67C0's 0021B9A0(1) faulted (the render context)\n");
        return -1;
    }
    return 0;
}

void em_snow_runtime_draw(EmGfx *gfx, const float view[16], float zoom)
{
    if (!snow.loaded || !snow.count) return;
    /* The fog quadword 001E67C0's emission read from the render context
     * (its descriptor's sprite colours are attenuated by the VU with it
     * before texture modulation). */
    EmSnowProjection projection = {0};
    memcpy(projection.fog, snow.fog, sizeof projection.fog);
    /* P (+0x2340), the 001CD370(0) clip projection (+0x2240) and K (+0x23C0)
     * of the render context, as this frame's head 001D1C50 built them (the
     * matrices the original's VU packets carry). */
    float native_projection[16];
    uint32_t p[16], clip[16], k[16];
    if (em_rcl_frame_matrices(p, clip, k) < 0) return;
    memcpy(projection.extent_projection, p, sizeof p);
    memcpy(projection.clip_from_world, clip, sizeof clip);
    memcpy(projection.screen_from_world, k, sizeof k);
    (void)view;
    em_mat4_perspective_gs(native_projection, zoom);
    unsigned count = 0;
    for (unsigned i = 0; i < snow.count; ++i) {
        const EmSnowParticle *particle = &snow.particles[i];
        EmSnowProjected sprite;
        if (!em_snow_project(&projection, particle, &sprite)) continue;
        EmGfxParticle *out = &snow.projected[count];
        for (unsigned corner = 0; corner < 2; ++corner) {
            float x = (float)(sprite.xyzf[corner][0] & 0xffffU) / 16.0f;
            float y = (float)(sprite.xyzf[corner][1] & 0xffffU) / 16.0f;
            out->corner[corner][0] = (x - 2048.0f) / 256.0f;
            out->corner[corner][1] = -(y - 2048.0f) / 112.0f;
        }
        /* Convert the quantized GS reciprocal depth to the native world's
         * existing depth convention. This preserves sprite depth steps;
         * it does not turn the native geometry pass into a GS rasterizer. */
        float gs_depth = (float)((sprite.xyzf[0][2] >> 4) & 0xffffffU);
        float inverse_w = (gs_depth - projection.extent_projection[10] -
            projection.depth_bias[2]) / projection.extent_projection[14];
        out->depth = -native_projection[10] + native_projection[14] * inverse_w;
        memcpy(out->st, sprite.st, sizeof out->st);
        for (unsigned component = 0; component < 4; ++component)
            out->color[component] = (float)sprite.color[component] / 128.0f;
        ++count;
    }
    em_gfx_particles_draw(gfx, snow.projected, count);
}
