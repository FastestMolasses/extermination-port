#include "game/em_pose_bank.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_pose_math.h"

void em_pose_bank_free(EmPoseBank *bank)
{
    if (!bank) return;
    for (unsigned c = 0; c < bank->clip_count; ++c)
        for (unsigned b = 0; b < bank->bone_count; ++b)
            for (unsigned k = 0; k < 3; ++k)
                free(bank->clips[c].tracks[b][k].keys);
    free(bank->clips);
    memset(bank, 0, sizeof *bank);
}

int em_pose_bank_load(EmPoseBank *bank, const char *path)
{
    if (!bank || !path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    EmPoseBank next = {0};
    uint32_t header[4];
    if (fread(header, sizeof header, 1, f) != 1 || memcmp(header, "EMPC", 4) || header[1] != 1 ||
        !header[2] || header[2] > EM_POSE_NODE_MAX || !header[3] || header[3] > 64)
        goto fail;
    next.bone_count = header[2];
    next.clip_count = header[3];
    if (fread(next.parents, 4, next.bone_count, f) != next.bone_count) goto fail;
    for (unsigned b = 0; b < next.bone_count; ++b)
        if (next.parents[b] < -1 || next.parents[b] >= (int)b) goto fail;
    next.clips = calloc(next.clip_count, sizeof *next.clips);
    if (!next.clips) goto fail;
    for (unsigned c = 0; c < next.clip_count; ++c) {
        EmPoseClip *clip = next.clips + c;
        if (fread(clip, 8, 1, f) != 1 || !clip->duration || clip->blend ||
            (clip->next_clip != -1 && clip->next_clip != -2))
            goto fail;
        for (unsigned previous = 0; previous < c; ++previous)
            if (next.clips[previous].id == clip->id) goto fail;
        for (unsigned b = 0; b < next.bone_count; ++b)
            for (unsigned k = 0; k < 3; ++k) {
                EmPoseTrack *track = &clip->tracks[b][k];
                if (fread(&track->count, 4, 1, f) != 1 || track->count < 2 || track->count > 4096)
                    goto fail;
                track->keys = malloc(track->count * sizeof *track->keys);
                if (!track->keys ||
                    fread(track->keys, sizeof *track->keys, track->count, f) != track->count)
                    goto fail;
                if (track->keys[0].time || track->keys[track->count - 1].time != 65535) goto fail;
                for (unsigned i = 0; i < track->count; ++i) {
                    const EmPoseKey *key = track->keys + i;
                    if (key->hold > 1 || (i && key->time <= track->keys[i - 1].time)) goto fail;
                    for (unsigned component = 0; component < 4; ++component)
                        if (!isfinite(key->value[component])) goto fail;
                }
            }
    }
    if (fgetc(f) != EOF) goto fail;
    fclose(f);
    em_pose_bank_free(bank);
    *bank = next;
    return 1;
fail:
    fclose(f);
    /* A failure before allocation has a nonzero declared clip count. */
    if (!next.clips) next.clip_count = 0;
    em_pose_bank_free(&next);
    return 0;
}

static void cursor_seed(EmPoseCursor *cursor, const EmPoseTrack *track, float frame, int vector)
{
    unsigned index = 1;
    while (track->keys[index].time <= frame)
        ++index;
    const EmPoseKey *a = track->keys + index - 1, *b = track->keys + index;
    memset(cursor, 0, sizeof *cursor);
    cursor->index = index;
    cursor->remaining = pose_sub((float)b->time, (float)frame);
    cursor->reciprocal = pose_div(1, pose_sub((float)b->time, (float)a->time));
    float elapsed = pose_sub((float)frame, (float)a->time);
    cursor->fraction = pose_mul(cursor->reciprocal, elapsed);
    if (vector)
        for (unsigned k = 0; k < 3; ++k) {
            cursor->velocity[k] = pose_mul(cursor->reciprocal, pose_sub(b->value[k], a->value[k]));
            cursor->value[k] = pose_madd(a->value[k], cursor->velocity[k], elapsed);
        }
}

int em_pose_playback_begin(EmPosePlayback *s, const EmPoseBank *bank, unsigned id, float frame)
{
    if (!s || !bank || !bank->clips) return 0;
    const EmPoseClip *clip = NULL;
    for (unsigned i = 0; i < bank->clip_count; ++i)
        if (bank->clips[i].id == id) {
            clip = bank->clips + i;
            break;
        }
    if (!clip || !isfinite(frame) || frame < 0 || frame >= clip->duration) return 0;
    memset(s, 0, sizeof *s);
    s->bank = bank;
    s->clip = clip;
    s->remaining = pose_sub(clip->duration, frame);
    for (unsigned b = 0; b < bank->bone_count; ++b)
        for (unsigned k = 0; k < 3; ++k)
            cursor_seed(&s->nodes[b][k], &clip->tracks[b][k], frame, k != 0);
    return 1;
}

static int cursor_advance(EmPoseCursor *cursor, const EmPoseTrack *track, float dt, int vector)
{
    cursor->remaining = pose_sub(cursor->remaining, dt);
    int hold = -1;
    if (cursor->remaining <= 0) {
        if (cursor->index + 1 >= track->count) return -2;
        const EmPoseKey *a = track->keys + cursor->index, *b = a + 1;
        float duration = pose_sub((float)b->time, (float)a->time), elapsed = -cursor->remaining;
        cursor->remaining = pose_add(cursor->remaining, duration);
        cursor->reciprocal = pose_div(1, duration);
        cursor->fraction = pose_mul(elapsed, cursor->reciprocal);
        if (vector)
            for (unsigned k = 0; k < 3; ++k) {
                cursor->velocity[k] =
                    pose_mul(cursor->reciprocal, pose_sub(b->value[k], a->value[k]));
                cursor->value[k] = pose_madd(a->value[k], elapsed, cursor->velocity[k]);
            }
        ++cursor->index;
        hold = a->hold;
    } else if (vector) {
        for (unsigned k = 0; k < 3; ++k)
            cursor->value[k] = pose_madd(cursor->value[k], dt, cursor->velocity[k]);
    } else cursor->fraction = pose_madd(cursor->fraction, dt, cursor->reciprocal);
    return hold;
}

int em_pose_playback_advance(EmPosePlayback *s, float dt, int freeze_motion)
{
    if (!s || !s->bank || !s->clip || !isfinite(dt) || dt < 0 || dt > 64) return 0;
    s->flags = 0;
    while (dt > 0) {
        float step = dt <= 1 ? dt : 1;
        dt = pose_sub(dt, step);
        if (s->remaining <= 1) {
            if (s->clip->next_clip == -2) s->flags |= 0x1000;
            else {
                unsigned flags = s->flags | 0x3000;
                if (!em_pose_playback_begin(s, s->bank, s->clip->id, 0)) return 0;
                s->flags = flags;
            }
            continue;
        }
        s->remaining = pose_sub(s->remaining, step);
        int hold = 0;
        for (unsigned b = 0; b < s->bank->bone_count; ++b)
            for (unsigned k = 0; k < 3; ++k) {
                int result = cursor_advance(&s->nodes[b][k], &s->clip->tracks[b][k], step, k != 0);
                if (result == -2) return 0;
                /* Original last scale transition wins, rather than ORing every
                 * node's hold flag. Rotation/translation flags do not own it. */
                if (k == 2 && result >= 0) hold = result;
            }
        if (hold || freeze_motion)
            for (unsigned b = 0; b < s->bank->bone_count; ++b) {
                s->nodes[b][0].reciprocal = 0;
                memset(s->nodes[b][1].velocity, 0, sizeof s->nodes[b][1].velocity);
                memset(s->nodes[b][2].velocity, 0, sizeof s->nodes[b][2].velocity);
            }
    }
    return 1;
}

int em_pose_playback_channels(const EmPosePlayback *s, EmPoseChannels *out)
{
    if (!s || !s->bank || !s->clip || !out) return 0;
    for (unsigned b = 0; b < s->bank->bone_count; ++b) {
        const EmPoseCursor *rotation = &s->nodes[b][0];
        const EmPoseKey *a = s->clip->tracks[b][0].keys + rotation->index - 1, *target = a + 1;
        memcpy(out[b].translation, s->nodes[b][1].value, 12);
        memcpy(out[b].scale, s->nodes[b][2].value, 12);
        em_pose_quaternion_blend(out[b].rotation, a->value, target->value, rotation->fraction);
    }
    return 1;
}
