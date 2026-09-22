#include "game/em_door_original_runtime.h"
#include "game/em_camera_rotation.h"
#include "game/em_effect_color.h"

#include <stdio.h>
#include <string.h>

static int fault(EmDoorOriginalRuntime *runtime, const char *message)
{
    runtime->failed = 1;
    runtime->error = message;
    return -1;
}

static void multiply(float out[16], const float left[16], const float right[16])
{
    /* Original C9940 matrix accumulator ordering, with finite VU stores.
     * Runtime capture tests report the resulting matrix agreement. */
    for (unsigned column = 0; column < 4; ++column) {
        for (unsigned row = 0; row < 4; ++row) {
            float value = em_effect_float32((double)left[row] * right[column*4]);
            for (unsigned lane = 1; lane < 4; ++lane) {
                float product = em_effect_float32((double)left[lane*4 + row] *
                                                   right[column*4 + lane]);
                value = em_effect_float32((double)value + product);
            }
            out[column*4 + row] = value;
        }
    }
}

static int place(void *context)
{
    EmDoorOriginalRuntime *r = context;
    EmPoseChannels channels[2];
    float unused[4];
    if (!em_camera_rotation_offset(r->angles, 0, r->matrix, unused) ||
        !em_pose_playback_channels(&r->playback, channels))
        return fault(r, "door source pose could not be evaluated");
    memcpy(r->matrix + 12, r->owner.origin, 3*sizeof(float));
    for (unsigned bone = 0; bone < 2; ++bone) {
        float local[16];
        em_pose_channels_matrix(local, channels + bone);
        int parent = r->bank.parents[bone];
        multiply(r->palette + bone*16,
                 parent < 0 ? r->matrix : r->palette + parent*16, local);
    }
    memset(r->palette + 32, 0, 16*sizeof(float));
    r->palette[32] = r->palette[37] = r->palette[42] = r->palette[47] = 1;
    return 1;
}

int em_door_original_runtime_animation(EmDoorOriginalRuntime *r, unsigned clip)
{
    if (!r || !r->loaded || r->failed || clip > 3) return 0;
    return em_pose_playback_begin(&r->playback, &r->bank, clip, 0);
}

static int initialize(void *context)
{
    return em_door_original_runtime_animation(context, 0);
}

static int advance(void *context, int16_t *flags)
{
    EmDoorOriginalRuntime *r = context;
    if (!em_pose_playback_advance(&r->playback, 1, 0))
        return fault(r, "door source animation advance failed");
    /* C64F0 returns flags, not the current source-frame number. */
    *flags = (int16_t)r->playback.flags;
    return 1;
}

static int kickoff(void *context, int locked)
{
    EmDoorOriginalRuntime *r = context;
    if (!r->hooks.kickoff)
        return fault(r, "original door BBE40 kickoff worker is unbound");
    return r->hooks.kickoff(r->hooks.context, locked);
}

static int script_tick(void *context)
{
    EmDoorOriginalRuntime *r = context;
    if (!r->hooks.script_tick)
        return fault(r, "original door script worker is unbound");
    return r->hooks.script_tick(r->hooks.context);
}

static int script_start(void *context, uint32_t entry)
{
    EmDoorOriginalRuntime *r = context;
    if (!r->hooks.script_start)
        return fault(r, "original door script entry worker is unbound");
    return r->hooks.script_start(r->hooks.context, entry);
}

static int transition(void *context)
{
    EmDoorOriginalRuntime *r = context;
    if (!r->hooks.transition)
        return fault(r, "original door room-transition worker is unbound");
    return r->hooks.transition(r->hooks.context);
}

static int publish(void *context, const float point[3])
{
    EmDoorOriginalRuntime *r = context;
    return r->hooks.publish(r->hooks.context, point);
}

static int draw(void *context)
{
    EmDoorOriginalRuntime *r = context;
    return r->hooks.draw(r->hooks.context);
}

static int retire(void *context)
{
    EmDoorOriginalRuntime *r = context;
    /* Host storage stays valid for a previous-frame canonical list.
     * Mark it inactive before the scene releases those bindings. */
    r->owner.status = 0;
    r->owner.class_flags &= 0x7f;
    r->owner.armed = 0;
    return 1;
}

static EmDoorOriginalHooks workers(EmDoorOriginalRuntime *r)
{
    EmDoorOriginalHooks hooks = {r, initialize, kickoff, advance, script_tick,
        script_start, transition, initialize, place, publish, draw, retire};
    return hooks;
}

int em_door_original_runtime_tick(EmDoorOriginalRuntime *r, uint8_t pending)
{
    if (!r || !r->loaded || r->failed) return -1;
    EmDoorOriginalHooks hooks = workers(r);
    int result = em_door_original_tick(&r->owner, 1, pending, &hooks);
    if (result < 0 && !r->failed) return fault(r, "original door worker failed");
    return result;
}

int em_door_original_runtime_arm(EmDoorOriginalRuntime *r)
{
    return r && r->loaded && !r->failed && em_door_original_arm(&r->owner);
}

void em_door_original_runtime_free(EmDoorOriginalRuntime *r)
{
    if (!r) return;
    em_model_free(&r->model);
    em_pose_bank_free(&r->bank);
    memset(r, 0, sizeof *r);
}

static int path(char out[1024], const char *scene, const char *name)
{
    int length = snprintf(out, 1024, "%s/door_original/%s", scene, name);
    return length > 0 && length < 1024;
}

int em_door_original_runtime_load(EmDoorOriginalRuntime *r, const char *scene,
    const EmInteractionSceneOwner *source, const EmDoorOriginalRuntimeHooks *hooks)
{
    if (!r || !scene || !source || !hooks || !hooks->publish || !hooks->draw ||
        source->source_id != 0x82a3c0 || source->callback != 0x1bc350 ||
        source->role != EM_INTERACTION_DOOR || source->class_flags != 0x85 ||
        source->subtype != 3 || source->selector != 0)
        return 0;
    memset(r, 0, sizeof *r);
    char file[1024];
    unsigned char metadata[72];
    uint32_t header[8];
    if (!path(file, scene, "source.emdo")) goto fail;
    FILE *stream = fopen(file, "rb");
    if (!stream) goto fail;
    int valid = fread(metadata, sizeof metadata, 1, stream) == 1 && fgetc(stream) == EOF;
    fclose(stream);
    if (!valid) goto fail;
    memcpy(header, metadata, sizeof header);
    if (memcmp(metadata, "EMDO", 4) || header[1] != 1 || header[2] != source->source_id ||
        header[3] != source->callback || header[4] != 0x14 || header[5] != 0x400 ||
        header[6] != 0 || header[7] != 0x39 ||
        memcmp(metadata + 32, source->position, 12) ||
        memcmp(metadata + 44, source->angles, 12) ||
        memcmp(metadata + 56, source->descriptor, 8))
        goto fail;
    if (!path(file, scene, "model.emdl") || em_model_load(&r->model, file) != 0 ||
        r->model.bone_count != 3 || r->model.flags || r->model.frame_count != 1 ||
        !r->model.vert_count || !r->model.index_count ||
        !path(file, scene, "channels.empc") || !em_pose_bank_load(&r->bank, file) ||
        r->bank.bone_count != 2 || r->bank.clip_count != 4 ||
        r->bank.parents[0] != -1 || r->bank.parents[1] != 0)
        goto fail;
    for (unsigned clip = 0; clip < 4; ++clip) {
        if (r->bank.clips[clip].id != clip ||
            r->bank.clips[clip].duration != (clip & 1 ? 200 : 150) ||
            r->bank.clips[clip].next_clip != -2)
            goto fail;
    }
    r->source_id = source->source_id;
    r->hooks = *hooks;
    r->owner.class_flags = source->class_flags;
    r->owner.subtype = source->subtype;
    r->owner.link_flags = (uint16_t)header[5];
    r->owner.side = (int16_t)header[6];
    memcpy(r->owner.origin, source->position, sizeof r->owner.origin);
    memcpy(r->angles, source->angles, sizeof r->angles);
    memcpy(r->descriptor, source->descriptor, sizeof r->descriptor);
    memcpy(r->destination, metadata + 64, 4);
    memcpy(r->sounds, metadata + 68, 4);
    r->loaded = 1;
    if (em_door_original_runtime_tick(r, 0) != 1 || place(r) != 1) goto fail;
    return 1;
fail:
    em_door_original_runtime_free(r);
    r->error = "missing or inconsistent original AREA11 door resource";
    return 0;
}
