#include "game/em_snow_runtime.h"
#include "game/em_snow.h"
#include "game/em_snow_particles.h"
#include "game/em_effect_color.h"
#include "game/em_random.h"

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
    snow.count = 0;
    if (!snow.loaded) return;
    EmWeatherFrame frame = em_weather_tick(&snow.weather, snow.flags, 0x0b00,
                                            selector, 0, 0, weather_random, NULL);
    if (frame.draw != 1) return;
    em_snow_tiles(&snow.weather, &snow.config, frame.strength, eye, snow.tiles);
    for (unsigned i = 0; i < EM_SNOW_TILE_COUNT; ++i) {
        const EmSnowTile *tile = &snow.tiles[i];
        int count = em_snow_particles_generate(tile->descriptor, snow.config.lookup,
            tile->params, tile->matrix, snow.particles + snow.count,
            SNOW_PARTICLE_COUNT - snow.count);
        if (count < 0) {
            fprintf(stderr, "snow: original particle generation failed\n");
            snow.loaded = 0;
            snow.count = 0;
            return;
        }
        snow.count += (unsigned)count;
    }
}

void em_snow_runtime_draw(EmGfx *gfx, const float viewproj[16])
{
    if (!snow.loaded || !snow.count) return;
    /* E67C0 sets fog near=0, far=300 through 0021B9A0. Its descriptor's
     * sprite colors are attenuated by the VU before texture modulation. */
    /* Original snow DMA's VU7A stores -0.8500000238 (rounded DIV.S),
     * not the adjacent value produced by truncating this quotient. */
    float slope = (float)(255.0 / 300.0);
    const float fog[4] = {255.0f, 2048.0f,
        em_effect_float32(300.0 * slope), -slope};
    float projection_x = sqrtf(viewproj[0]*viewproj[0] +
        viewproj[4]*viewproj[4] + viewproj[8]*viewproj[8]);
    float projection_y = sqrtf(viewproj[1]*viewproj[1] +
        viewproj[5]*viewproj[5] + viewproj[9]*viewproj[9]);
    unsigned count = 0;
    for (unsigned i = 0; i < snow.count; ++i) {
        const EmSnowParticle *particle = &snow.particles[i];
        EmGfxParticle *out = &snow.projected[count];
        for (unsigned row = 0; row < 4; ++row) {
            float value = em_effect_float32((double)viewproj[row] * particle->position[0]);
            for (unsigned column = 1; column < 4; ++column) {
                float product = em_effect_float32((double)viewproj[column*4+row] * particle->position[column]);
                value = em_effect_float32((double)value + product);
            }
            out->clip[row] = value;
        }
        const float *clip = out->clip;
        if (clip[3] <= 0 || fabsf(clip[0]) > clip[3] ||
            fabsf(clip[1]) > clip[3] || clip[2] < 0 || clip[2] > clip[3]) continue;
        uint32_t color[4];
        em_snow_particles_color(particle->color, clip[3], fog, color);
        for (unsigned component = 0; component < 4; ++component)
            out->color[component] = (float)color[component] / 128.0f;
        out->half_extent[0] = projection_x * particle->half_size[0];
        out->half_extent[1] = projection_y * particle->half_size[1];
        ++count;
    }
    em_gfx_particles_draw(gfx, snow.projected, count);
}
