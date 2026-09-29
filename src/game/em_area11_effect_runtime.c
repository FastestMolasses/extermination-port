#include "game/em_area11_effect_runtime.h"
#include "game/em_effects_live.h"
#include "game/em_random.h"
#include "em_math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum { EFFECT_PARTICLES = 80 };
/* The EMEF record (tools/export_area11_effect.py). The lookup and the rig's
 * fog range are validated but not read: the sprite program's own packet
 * uploads the lookup, and 001CFBE0 copies the render context's fog. */
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
    int loaded;
    int fault;
} effect;

void em_area11_effect_runtime_clear(void)
{
    memset(&effect, 0, sizeof effect);
}

const EmArea11Effect *em_area11_effect_runtime_state(void)
{
    return effect.loaded ? &effect.owner : NULL;
}

int em_area11_effect_runtime_load(const char *directory, const char *config)
{
    em_area11_effect_runtime_clear();
    if (!directory || !config) return 0;
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
    effect.config = loaded;
    effect.loaded = 1;
    return 1;
}

static uint32_t effect_random(void *context)
{
    (void)context;
    return em_random_next();
}

static uint32_t bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return b;
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
        /* 001D04B0(+0xD0, 1, D_00828340, f12 phase, f13 seed) (0x8236AC). */
        uint8_t descriptor[0x90];
        memcpy(descriptor, effect.config.descriptor, sizeof descriptor);
        if (em_effects_live_001D04B0(effect.matrix, 1, EM_AREA11_EFFECT_DESCRIPTOR, descriptor,
                                     bits(owner->phase), bits(owner->seed)) < 0)
            effect.fault = 1;
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

int em_area11_effect_runtime_tick(void)
{
    if (!effect.loaded) return 0;
    effect.fault = 0;
    em_area11_effect_tick(&effect.owner, effect_random, effect_call, NULL);
    if (effect.fault) {
        fprintf(stderr, "AREA11 effect: 001D04B0 faulted (em_effects_live)\n");
        return -1;
    }
    return 0;
}
