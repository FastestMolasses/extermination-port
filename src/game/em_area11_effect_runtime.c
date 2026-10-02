#include "game/em_area11_effect_runtime.h"
#include "game/em_effects_live.h"
#include "game/em_random.h"
#include "game/em_sfx.h"
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
    EffectConfig config;
    float matrix[16];
    int loaded;
    int fault;
    uint32_t fault_address;
    const EmArea11EffectRuntimeCall *call;
} effect;

void em_area11_effect_runtime_clear(void)
{
    memset(&effect, 0, sizeof effect);
}

/* The record's fields <-> the controller's view: +0x00, +0x04, +0x30,
 * +0x34 and the +0x1F0 block (half extents +0x00..+0x0B, phase +0x14, seed
 * +0x18, sound handle +0x1C, cooldown +0x20 of the block). */
static void record_load(const EmActor *a, uint32_t record, EmArea11Effect *e)
{
    memset(e, 0, sizeof *e);
    e->state = a->u04[0];
    e->flags = a->status;
    memcpy(e->half_extent, a->scratch + 0x00, sizeof e->half_extent);
    memcpy(&e->phase, a->scratch + 0x14, 4);
    memcpy(&e->seed, a->scratch + 0x18, 4);
    memcpy(&e->sound_handle, a->scratch + 0x1C, 4);
    memcpy(&e->contact_cooldown, a->scratch + 0x20, 4);
    e->record = record;
    e->w30 = a->w30;
    e->w34 = a->w34;
}

static void record_store(EmActor *a, const EmArea11Effect *e)
{
    a->u04[0] = e->state;
    a->status = e->flags;
    memcpy(a->scratch + 0x00, e->half_extent, sizeof e->half_extent);
    memcpy(a->scratch + 0x14, &e->phase, 4);
    memcpy(a->scratch + 0x18, &e->seed, 4);
    memcpy(a->scratch + 0x1C, &e->sound_handle, 4);
    memcpy(a->scratch + 0x20, &e->contact_cooldown, 4);
    a->w30 = e->w30;
    a->w34 = e->w34;
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

static void call_fault(uint32_t address)
{
    if (!effect.fault) {
        effect.fault = 1;
        effect.fault_address = address;
    }
}

static void effect_call(void *context, EmArea11EffectCall call,
                         EmArea11Effect *owner)
{
    (void)context;
    const EmArea11EffectRuntimeCall *c = effect.call;
    if (effect.fault) return;   /* fail-stop: nothing after a failed callee */
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
            call_fault(0x001D04B0u);
        break;
    }
    case EM_AREA11_EFFECT_SOUND:
        /* 001FC3C0(self, +0x20C, 0x413, 100.0, 4096.0): the looped
         * positional service at the record's +0xB0 on the (frame +
         * ordinal) % 10 cadence (em_sfx.h; 001FBD50 passes its own 4096,
         * so the caller's f13 is not read). */
        owner->sound_handle = em_sfx_loop_service(&owner->sound_handle, EM_AREA11_EFFECT_SOUND_ID,
                                                  c->actor->pos, 100.0f, c->frame, c->ordinal);
        break;
    case EM_AREA11_EFFECT_PUBLISH:
        /* 001B17A0(self): the record onto the collision world's class-0xD
         * list when visible, where the contact pass 001A8660 reads +0x30 /
         * +0x34 (docs/AREA11_EFFECT.md "Binding"). The record must hold the
         * state the controller has reached before the call. */
        record_store(c->actor, owner);
        if (!c->publish || c->publish(c->ctx) < 0)
            call_fault(0x001B17A0u);
        break;
    case EM_AREA11_EFFECT_STOP_SOUND:
        /* 001FC520(+0x20C). */
        em_sfx_loop_release(&owner->sound_handle);
        break;
    case EM_AREA11_EFFECT_FREE:
        /* 001AFC10(self): the record is freed; nothing is written back. */
        record_store(c->actor, owner);
        if (!c->free_record || c->free_record(c->ctx) < 0)
            call_fault(0x001AFC10u);
        else
            effect.call = NULL;
        break;
    }
}

int em_area11_effect_runtime_tick(const EmArea11EffectRuntimeCall *call, uint32_t *fault_address)
{
    if (fault_address) *fault_address = 0;
    if (!call || !call->actor || !effect.loaded) {
        if (fault_address) *fault_address = EM_AREA11_EFFECT_CALLBACK;
        return -1;
    }
    EmArea11Effect owner;
    record_load(call->actor, call->record, &owner);
    effect.fault = 0;
    effect.fault_address = 0;
    effect.call = call;
    em_area11_effect_tick(&owner, effect_random, effect_call, NULL);
    const int freed = effect.call == NULL;
    effect.call = NULL;
    if (effect.fault) {
        if (fault_address) *fault_address = effect.fault_address;
        return -1;
    }
    if (!freed)
        record_store(call->actor, &owner);
    return 0;
}

int em_area11_effect_runtime_contact(EmActor *actor, uint8_t target_flags,
                                     const EmArea11EffectContactWorkers *workers,
                                     uint8_t *target_reaction, uint32_t *fault_address)
{
    if (!actor) {
        if (fault_address) *fault_address = EM_AREA11_EFFECT_CONTACT;
        return -1;
    }
    EmArea11Effect owner;
    record_load(actor, 0, &owner);
    if (em_area11_effect_contact(&owner, target_flags, workers, target_reaction, fault_address) < 0)
        return -1;
    memcpy(actor->scratch + 0x20, &owner.contact_cooldown, 4);   /* +0x210 */
    return 0;
}
