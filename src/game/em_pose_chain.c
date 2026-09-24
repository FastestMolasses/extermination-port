/* em_pose_chain.c - see em_pose_chain.h and docs/PLAYER_CLIPS.md. */
#include "game/em_pose_chain.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_ee_float.h"
#include "game/em_stream_lanes_original.h"  /* 001281C0 float_to_int, 00128250 */

#define F_ZERO UINT32_C(0x00000000)
#define F_ONE UINT32_C(0x3F800000)
#define LOOKUP_SIZE 0x8000u

/* ---- the bank (EMPX v1, tools/export_player_clips.py) ------------------------ */

void em_pose_chain_bank_free(EmPoseChainBank *b)
{
    if (!b) return;
    em_pose_bank_free(&b->bank);
    free(b->event_first);
    free(b->event_count);
    free(b->events);
    free(b->lookup);
    memset(b, 0, sizeof *b);
}

static int read_all(FILE *f, void *out, size_t size, size_t count)
{
    return fread(out, size, count, f) == count;
}

int em_pose_chain_bank_load(EmPoseChainBank *out, const char *path)
{
    if (!out || !path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    EmPoseChainBank next;
    memset(&next, 0, sizeof next);
    size_t event_capacity = 0, event_total = 0;
    uint32_t header[5];
    if (!read_all(f, header, sizeof header, 1) || memcmp(header, "EMPX", 4) || header[1] != 1 ||
        !header[2] || header[2] > EM_POSE_NODE_MAX || !header[3] || header[3] > LOOKUP_SIZE ||
        header[4] < header[3] || header[4] > LOOKUP_SIZE)
        goto fail;
    next.bank.bone_count = header[2];
    next.bank.clip_count = header[3];
    next.bank_clip_count = header[4];
    if (!read_all(f, next.bank.parents, 4, next.bank.bone_count)) goto fail;
    for (unsigned b = 0; b < next.bank.bone_count; ++b)
        if (next.bank.parents[b] < -1 || next.bank.parents[b] >= (int)b) goto fail;
    next.bank.clips = calloc(next.bank.clip_count, sizeof *next.bank.clips);
    next.event_first = calloc(next.bank.clip_count, sizeof *next.event_first);
    next.event_count = calloc(next.bank.clip_count, sizeof *next.event_count);
    next.lookup = calloc(LOOKUP_SIZE, sizeof *next.lookup);
    if (!next.bank.clips || !next.event_first || !next.event_count || !next.lookup) goto fail;
    for (unsigned c = 0; c < next.bank.clip_count; ++c) {
        EmPoseClip *clip = next.bank.clips + c;
        uint32_t events;
        if (!read_all(f, clip, 8, 1) || !read_all(f, &events, 4, 1) || !clip->duration ||
            clip->id >= LOOKUP_SIZE || next.lookup[clip->id] || events > 0x7FFF)
            goto fail;
        next.lookup[clip->id] = (uint16_t)(c + 1);
        if (event_total + events > event_capacity) {
            size_t capacity = (event_total + events) * 2;
            EmPoseChainEvent *grown = realloc(next.events, capacity * sizeof *grown);
            if (!grown) goto fail;
            next.events = grown;
            event_capacity = capacity;
        }
        if (events && !read_all(f, next.events + event_total, sizeof *next.events, events)) goto fail;
        next.event_first[c] = (uint32_t)event_total;
        next.event_count[c] = events;
        event_total += events;
        for (unsigned b = 0; b < next.bank.bone_count; ++b)
            for (unsigned k = 0; k < 3; ++k) {
                EmPoseTrack *track = &clip->tracks[b][k];
                if (!read_all(f, &track->count, 4, 1) || track->count < 2 || track->count > 4096)
                    goto fail;
                track->keys = malloc(track->count * sizeof *track->keys);
                if (!track->keys || !read_all(f, track->keys, sizeof *track->keys, track->count))
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
    /* Every follow-on link names a clip of the file (-1 loop, -2 hold). */
    for (unsigned c = 0; c < next.bank.clip_count; ++c) {
        int link = next.bank.clips[c].next_clip;
        if (link != -1 && link != -2 && (link < 0 || !next.lookup[link])) goto fail;
    }
    fclose(f);
    em_pose_chain_bank_free(out);
    *out = next;
    return 1;
fail:
    fclose(f);
    if (!next.bank.clips) next.bank.clip_count = 0;
    em_pose_chain_bank_free(&next);
    return 0;
}

const EmPoseClip *em_pose_chain_find(const EmPoseChainBank *b, int clip)
{
    if (!b || !b->lookup) return NULL;
    unsigned index = b->lookup[(unsigned)clip & 0x7FFFu];
    return index ? b->bank.clips + index - 1 : NULL;
}

int em_pose_chain_frames(const EmPoseChainBank *b, int clip, int32_t *frames)
{
    const EmPoseClip *found = em_pose_chain_find(b, (int16_t)clip);
    if (!found || !frames) return -1;
    *frames = found->duration;
    return 0;
}

/* ---- channels -------------------------------------------------------------------- */

/* The node channels as 001C6DA0 evaluates them: during a transition the
 * rotation is 001CA0A0 of the frozen source (+30) and the target (+40) by +50. */
static void publish(EmPoseChain *c)
{
    unsigned n = c->bank->bank.bone_count;
    if (c->clip_word & 0x8000) {
        const EmPoseTransition *t = &c->transition;
        for (unsigned i = 0; i < n; ++i) {
            memcpy(c->channels[i].translation, t->current[i].translation, 12);
            memcpy(c->channels[i].scale, t->current[i].scale, 12);
            em_pose_quaternion_blend(c->channels[i].rotation, t->source[i].rotation,
                                     t->target[i].rotation, t->fraction);
        }
    } else {
        em_pose_playback_channels(&c->playback, c->channels);
    }
}

int em_pose_chain_channels(const EmPoseChain *c, EmPoseChannels *out)
{
    if (!c || !c->bank || !c->posed || !out) return -1;
    memcpy(out, c->channels, c->bank->bank.bone_count * sizeof *out);
    return 0;
}

/* 001C8D50(nodes, new_t, prev_t): freeze the evaluated channels as the source
 * and seed a prev_t-long transition toward `clip` sampled at the integer
 * frame. The cursors of that sample are the target's (+66/+68/+6A). */
static int seed_transition(EmPoseChain *c, const EmPoseClip *clip, int32_t frame, uint32_t prev_t)
{
    int32_t ticks = em_stream_lanes_001281C0(prev_t);
    if (!c->posed || ticks < 1 || ticks > 65535 || em_ee_cvt_s_w_bits((uint32_t)ticks) != prev_t)
        return -1;
    EmPosePlayback target;
    EmPoseChannels sampled[EM_POSE_NODE_MAX];
    if (frame < 0 || !em_pose_playback_begin(&target, &c->bank->bank, clip->id, (float)frame) ||
        !em_pose_playback_channels(&target, sampled) ||
        !em_pose_transition_begin(&c->transition, c->channels, sampled,
                                  c->bank->bank.bone_count, (unsigned)ticks))
        return -1;
    c->playback = target;
    return 0;
}

/* 001C8710(nodes, frame): the clip's channels at a whole frame. */
static int sample_at(EmPoseChain *c, const EmPoseClip *clip, uint32_t frame)
{
    if (!em_pose_playback_begin(&c->playback, &c->bank->bank, clip->id, em_ee_float(frame)))
        return -1;
    c->transition.active = 0;
    return 0;
}

/* ---- 001C64F0 anim_advance_time ------------------------------------------ */

static int advance(EmPoseChain *c, uint32_t dt, int freeze, int16_t *out)
{
    int16_t flags = 0;
    uint32_t t = dt;
    while (!em_ee_c_le_bits(t, F_ZERO)) {
        uint32_t step = em_ee_c_le_bits(t, F_ONE) ? t : F_ONE;
        t = em_ee_sub_bits(t, step);
        const EmPoseClip *hd = em_pose_chain_find(c->bank, (int16_t)c->clip_word);   /* 001C8480 */
        if (!hd) return -1;
        c->clip = hd;
        if (!(c->clip_word & 0x8000)) {                        /* the event table, outside transitions */
            unsigned index = (unsigned)(hd - c->bank->bank.clips);
            const EmPoseChainEvent *e = c->bank->events + c->bank->event_first[index];
            int16_t now = (int16_t)em_stream_lanes_001281C0(c->clock);
            for (uint32_t i = 0; i < c->bank->event_count[index]; ++i)
                if (e[i].frame == now) {
                    flags = (int16_t)(uint16_t)((uint16_t)flags | e[i].flags);
                    break;
                }
        }
        if (em_ee_c_le_bits(c->clock, F_ONE)) {
            if (c->clip_word & 0x8000) {                       /* the transition's end */
                c->clip_word &= 0x7FFF;
                uint32_t held = em_ee_cvt_s_w_bits((uint32_t)(int32_t)(int16_t)c->hold_frame);
                c->clock = em_ee_sub_bits(em_ee_cvt_s_w_bits(hd->duration), held);
                if (sample_at(c, hd, held)) return -1;
            } else if (hd->next_clip == -2) {                  /* hold at the end */
                flags = (int16_t)(flags | 0x1000);
            } else if (hd->next_clip == -1) {                  /* loop */
                flags = (int16_t)(flags | 0x3000);
                c->clock = em_ee_cvt_s_w_bits(hd->duration);
                if (sample_at(c, hd, F_ZERO)) return -1;
            } else {                                           /* the chain step */
                c->clip_word = (uint16_t)(hd->next_clip | 0x8000);
                flags = (int16_t)(flags | 0x4000);
                c->clock = em_ee_cvt_s_w_bits((uint32_t)(int32_t)(int16_t)hd->blend);
                const EmPoseClip *follow = em_pose_chain_find(c->bank, (int16_t)c->clip_word);
                if (!follow) return -1;
                c->clip = follow;
                if (seed_transition(c, follow, 0, c->clock)) return -1;
            }
        } else {
            uint32_t before = c->clock;
            c->clock = em_ee_sub_bits(c->clock, step);
            if (c->clip_word & 0x8000) {                       /* 001C87C0 on the transition */
                if (!c->transition.active || em_ee_bits(c->transition.remaining) != before ||
                    em_pose_transition_step(&c->transition, em_ee_float(step)) != 1)
                    return -1;
                if (freeze) {
                    c->transition.reciprocal = 0;
                    memset(c->transition.translation_velocity, 0, sizeof c->transition.translation_velocity);
                    memset(c->transition.scale_velocity, 0, sizeof c->transition.scale_velocity);
                }
                flags = (int16_t)(uint16_t)((uint16_t)flags | 0x8000u);
            } else {                                           /* 001C87C0 on the clip */
                if (em_ee_bits(c->playback.remaining) != before ||
                    !em_pose_playback_advance(&c->playback, em_ee_float(step), freeze))
                    return -1;
            }
        }
        publish(c);
    }
    *out = flags;
    return 0;
}

int em_pose_chain_advance(EmPoseChain *c, float dt, int freeze_motion, int32_t *flags)
{
    if (!c || !c->bank || !c->posed || !flags || !isfinite(dt)) return -1;
    int16_t result;
    if (advance(c, em_ee_bits(dt), freeze_motion, &result)) return -1;
    *flags = result;
    return 0;
}

/* ---- 001C67E0 / 001749A0 / 001749F0 ------------------------------------------ */

int em_pose_chain_init(EmPoseChain *c, const EmPoseChainBank *bank, int16_t requested)
{
    if (!c || !bank || !bank->bank.clips) return -1;
    memset(c, 0, sizeof *c);
    c->bank = bank;
    c->requested = requested;
    return 0;
}

int em_pose_chain_clip_init(EmPoseChain *c, int clip, float blend_in, float frame_in)
{
    if (!c || !c->bank) return -1;
    const EmPoseClip *target = em_pose_chain_find(c->bank, (int16_t)(clip | 0x8000));
    if (!target) return -1;
    uint32_t blend = em_ee_bits(blend_in), frame = em_ee_bits(frame_in);
    c->clip_word = (uint16_t)(clip | 0x8000);                  /* 001C67FC */
    c->clip = target;
    c->hold_frame = (uint16_t)em_stream_lanes_00128250(frame);
    if (em_ee_c_eq_bits(F_ZERO, blend)) {
        /* +3C = 1.0 and the one-tick 001C8D50 seed, which the advance by 1.0
         * resolves at once: 001C8710 rewrites every node channel at +8E. */
        c->clock = F_ONE;
        int16_t ignored;
        if (advance(c, F_ONE, 0, &ignored)) return -1;
        c->posed = 1;
        publish(c);
        return 0;
    }
    c->clock = blend;
    if (seed_transition(c, target, em_stream_lanes_001281C0(frame), blend)) return -1;
    publish(c);
    return 0;
}

int em_pose_chain_request(EmPoseChain *c, int clip, int flags, float blend, int *result)
{
    if (!c || !result) return -1;
    if (flags == 0 && (int16_t)clip == c->requested) {
        *result = 1;
        return 0;
    }
    c->requested = (int16_t)clip;
    if (em_pose_chain_clip_init(c, c->requested, blend, 0.0f)) return -1;
    *result = 0;
    return 0;
}

int em_pose_chain_arbiter(EmPoseChain *c, int clip, float blend, float frame, int *result)
{
    if (!c || !result) return -1;
    if (em_pose_chain_clip_init(c, clip, blend, frame)) return -1;
    if ((int16_t)clip != c->requested) {
        c->requested = (int16_t)clip;
        *result = 1;
    } else {
        *result = 0;
    }
    return 0;
}
