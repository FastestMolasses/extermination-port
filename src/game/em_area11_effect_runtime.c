#include "game/em_area11_effect_runtime.h"
#include "game/em_render_context_live.h"
#include "game/em_effect_color.h"
#include "game/em_random.h"
#include "game/em_snow_particles.h"
#include "game/em_snow_projection.h"
#include "em_math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { EFFECT_PARTICLES = 80, EFFECT_TEXTURE = 1 };
typedef struct EffectConfig {
    float position[3];
    float descriptor[9][4];
    float lookup[80];
    float fog_range[2];
} EffectConfig;

static struct {
    EmArea11Effect owner;
    EffectConfig config;
    float matrix[16];
    EmSnowParticle particles[EFFECT_PARTICLES];
    EmGfxParticle projected[EFFECT_PARTICLES];
    unsigned count;
    int loaded;
} effect;

void em_area11_effect_runtime_clear(EmGfx *gfx)
{
    em_gfx_particle_texture_set_slot(gfx, EFFECT_TEXTURE, NULL, 0, 0);
    memset(&effect, 0, sizeof effect);
}

const EmArea11Effect *em_area11_effect_runtime_state(void)
{
    return effect.loaded ? &effect.owner : NULL;
}

int em_area11_effect_runtime_load(EmGfx *gfx, const char *directory,
                                  const char *config, const char *texture)
{
    em_area11_effect_runtime_clear(gfx);
    if (!directory || !config || !texture) return 0;
    char path[1024];
    if (snprintf(path, sizeof path, "%s/%s", directory, config) >= (int)sizeof path)
        return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    uint32_t header[4];
    EffectConfig loaded;
    int valid = fread(header, sizeof header, 1, file) == 1 &&
        memcmp(header, "EMEF", 4) == 0 && header[1] == 1 &&
        header[2] == 0x0b00 && header[3] == EFFECT_PARTICLES &&
        fread(&loaded, sizeof loaded, 1, file) == 1 && fgetc(file) == EOF;
    fclose(file);
    if (!valid) return 0;
    uint32_t count, flags, mode;
    memcpy(&count, &loaded.descriptor[8][0], 4);
    memcpy(&flags, &loaded.descriptor[8][2], 4);
    memcpy(&mode, &loaded.descriptor[8][3], 4);
    if (count != EFFECT_PARTICLES || flags != 9 || mode != 2 ||
        loaded.descriptor[6][0] == loaded.descriptor[6][1] ||
        !isfinite(loaded.fog_range[0]) || !isfinite(loaded.fog_range[1]) ||
        loaded.fog_range[1] <= loaded.fog_range[0]) return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (!isfinite(loaded.position[i])) return 0;
    for (unsigned i = 0; i < 80; ++i)
        if (!isfinite(loaded.lookup[i])) return 0;
    for (unsigned row = 0; row < 7; ++row)
        for (unsigned lane = 0; lane < 4; ++lane)
            if (!isfinite(loaded.descriptor[row][lane])) return 0;
    if (!isfinite(loaded.descriptor[7][2]) || !isfinite(loaded.descriptor[7][3]) ||
        !isfinite(loaded.descriptor[8][1])) return 0;

    if (snprintf(path, sizeof path, "%s/%s", directory, texture) >= (int)sizeof path)
        return 0;
    file = fopen(path, "rb");
    if (!file) return 0;
    valid = fread(header, sizeof header, 1, file) == 1 &&
        memcmp(header, "EMTX", 4) == 0 && header[1] == 1 &&
        header[2] > 0 && header[2] <= 256 && header[3] > 0 && header[3] <= 256;
    if (!valid) { fclose(file); return 0; }
    size_t size = (size_t)header[2] * header[3] * 4;
    uint8_t *texels = malloc(size);
    valid = texels && fread(texels, size, 1, file) == 1 && fgetc(file) == EOF;
    fclose(file);
    if (valid) valid = em_gfx_particle_texture_set_slot(gfx, EFFECT_TEXTURE,
                                                       texels, header[2], header[3]);
    free(texels);
    if (!valid) return 0;
    effect.config = loaded;
    effect.loaded = 1;
    return 1;
}

static uint32_t effect_random(void *context)
{
    (void)context;
    return em_random_next();
}

static void effect_call(void *context, EmArea11EffectCall call,
                         EmArea11Effect *owner)
{
    (void)context;
    switch (call) {
    case EM_AREA11_EFFECT_MATRIX:
        /* The exporter verifies all three authored rotation fields are
         * zero, then checks this identity/translation against original EE
         * and VU matrices. Nonzero rotations are deliberately rejected. */
        em_mat4_identity(effect.matrix);
        memcpy(effect.matrix + 12, effect.config.position, 3 * sizeof(float));
        break;
    case EM_AREA11_EFFECT_DRAW: {
        float params[4] = {owner->phase, 1.0f, 0.000001f, owner->seed};
        int count = em_snow_particles_generate(effect.config.descriptor,
            effect.config.lookup, params, effect.matrix, effect.particles,
            EFFECT_PARTICLES);
        if (count < 0) {
            fprintf(stderr, "AREA11 effect: original particle generation failed\n");
            effect.loaded = 0;
        } else {
            effect.count = (unsigned)count;
        }
        break;
    }
    case EM_AREA11_EFFECT_SOUND:
        /* Original001FC3C0 manages sound413 with radius100 and an active-
         * list cadence. The old90-tick retrigger and radius300 were made
         * up. Binding the real service awaits its SPU note-off/envelope
         * and actor schedule, and is intentionally not replaced by a loop. */
        break;
    case EM_AREA11_EFFECT_PUBLISH:
        /* Original001B17A0 publishes this class13 actor's spatial category.
         * Native category selection and attached effect80000027 are not
         * yet recovered; do not invent damage from the visual bounds. */
        break;
    case EM_AREA11_EFFECT_STOP_SOUND:
        owner->sound_handle = -1;
        break;
    case EM_AREA11_EFFECT_FREE:
        effect.loaded = 0;
        break;
    }
}

void em_area11_effect_runtime_tick(void)
{
    effect.count = 0;
    if (effect.loaded)
        em_area11_effect_tick(&effect.owner, effect_random, effect_call, NULL);
}

void em_area11_effect_runtime_draw(EmGfx *gfx, const float view[16], float zoom)
{
    if (!effect.loaded || !effect.count || !gfx || !view) return;
    EmSnowProjection projection = {0};
    const float *range = effect.config.fog_range;
    float delta = em_effect_float32((double)range[1] - range[0]);
    float slope = (float)(255.0 / delta);
    projection.fog[0] = 255.0f;
    projection.fog[1] = 2048.0f;
    projection.fog[2] = em_effect_float32((double)range[1] * slope);
    projection.fog[3] = -slope;
    /* The render context's P, 001CD370(0) clip projection and K, as this
     * frame's head 001D1C50 built them. */
    float native_projection[16];
    uint32_t p[16], clip[16], k[16];
    if (em_rcl_frame_matrices(p, clip, k) < 0) return;
    memcpy(projection.extent_projection, p, sizeof p);
    memcpy(projection.clip_from_world, clip, sizeof clip);
    memcpy(projection.screen_from_world, k, sizeof k);
    em_mat4_perspective_gs(native_projection, zoom);
    unsigned count = 0;
    for (unsigned i = 0; i < effect.count; ++i) {
        EmSnowProjected sprite;
        if (!em_effect_sprite_project(&projection, &effect.particles[i], &sprite))
            continue;
        EmGfxParticle *out = &effect.projected[count++];
        for (unsigned corner = 0; corner < 2; ++corner) {
            float x = (float)(sprite.xyzf[corner][0] & 0xffffU) / 16.0f;
            float y = (float)(sprite.xyzf[corner][1] & 0xffffU) / 16.0f;
            out->corner[corner][0] = (x - 2048.0f) / 256.0f;
            out->corner[corner][1] = -(y - 2048.0f) / 112.0f;
        }
        float gs_depth = (float)((sprite.xyzf[0][2] >> 4) & 0xffffffU);
        float inverse_w = (gs_depth - projection.extent_projection[10] -
            projection.depth_bias[2]) / projection.extent_projection[14];
        out->depth = -native_projection[10] + native_projection[14] * inverse_w;
        memcpy(out->st, sprite.st, sizeof out->st);
        for (unsigned lane = 0; lane < 4; ++lane)
            out->color[lane] = (float)sprite.color[lane] / 128.0f;
    }
    em_gfx_particles_draw_slot(gfx, EFFECT_TEXTURE, effect.projected, count);
}
