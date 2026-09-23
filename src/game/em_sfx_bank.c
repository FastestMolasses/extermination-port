#include "game/em_sfx_bank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t read16(const unsigned char *p)
{
    return (uint16_t)(p[0] | (unsigned)p[1] << 8);
}

static uint32_t read32(const unsigned char *p)
{
    return read16(p) | (uint32_t)read16(p + 2) << 16;
}

void em_sfx_bank_free(EmSfxBank *bank)
{
    if (!bank) return;
    for (unsigned i = 0; i < 2; ++i) free(bank->cues[i].pcm);
    memset(bank, 0, sizeof *bank);
}

int em_sfx_bank_load(EmSfxBank *bank, const char *path)
{
    if (!bank || !path) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    EmSfxBank next = {0};
    unsigned char header[24];
    int valid = 0;
    if (fread(header, 1, 20, file) != 20 || memcmp(header, "EMSF", 4) ||
        read32(header + 4) != 1 || read32(header + 8) != 11 ||
        read32(header + 12) != 0 || read32(header + 16) != 2) goto done;
    next.area = 11;
    next.count = 2;
    for (unsigned i = 0; i < 2; ++i) {
        EmSfxCue *cue = &next.cues[i];
        if (fread(header, 1, 24, file) != 24) goto done;
        cue->id = read32(header);
        cue->flags = read32(header + 4);
        cue->pitch = read16(header + 8);
        cue->gain_l = (int16_t)read16(header + 10);
        cue->gain_r = (int16_t)read16(header + 12);
        cue->adsr1 = read16(header + 14);
        cue->adsr2 = read16(header + 16);
        cue->frames = read32(header + 20);
        if (read16(header + 18)) goto done;
        if (i == 0) {
            if (cue->id != 0x3EE || cue->flags != EM_SFX_CUE_ABSENT ||
                cue->pitch || cue->gain_l || cue->gain_r || cue->adsr1 ||
                cue->adsr2 || cue->frames) goto done;
            continue;
        }
        /* This is a deliberately narrow profile. In particular, do not
         * interpret another authored ADSR pair as this constant envelope. */
        if (cue->id != 0x3EF || cue->flags != EM_SFX_CUE_REVERB ||
            cue->pitch != 862 || cue->gain_l != 2217 || cue->gain_r != 2217 ||
            cue->adsr1 != 0x80FF || cue->adsr2 != 0x5FD0 ||
            cue->frames != 2296) goto done;
        cue->pcm = malloc(cue->frames * sizeof *cue->pcm);
        if (!cue->pcm) goto done;
        for (uint32_t j = 0; j < cue->frames; ++j) {
            if (fread(header, 1, 2, file) != 2) goto done;
            cue->pcm[j] = (int16_t)read16(header);
            if (j < 41 && cue->pcm[j]) goto done;
        }
    }
    if (fgetc(file) != EOF || ferror(file)) goto done;
    valid = 1;
done:
    fclose(file);
    if (!valid) {
        em_sfx_bank_free(&next);
        return 0;
    }
    em_sfx_bank_free(bank);
    *bank = next;
    return 1;
}

int em_sfx_cue_frame(const EmSfxCue *cue, uint64_t output_frame,
                     unsigned device_rate, float stereo[2])
{
    if (!cue || !stereo || !device_rate || device_rate > 384000 ||
        !cue->pitch || !cue->pcm || !cue->frames ||
        (cue->flags & EM_SFX_CUE_ABSENT)) return 0;
    const uint64_t numerator = 48000u * (uint64_t)cue->pitch;
    const uint64_t denominator = 4096u * (uint64_t)device_rate;
    const uint64_t end = (cue->frames * denominator + numerator - 1) / numerator;
    if (output_frame >= end) return 0;
    const uint64_t phase = output_frame * numerator;
    const uint32_t index = (uint32_t)(phase / denominator);
    const uint32_t next = index + 1 < cue->frames ? index + 1 : index;
    const float fraction = (float)((double)(phase % denominator) / (double)denominator);
    const float sample = cue->pcm[index] + (cue->pcm[next] - cue->pcm[index]) * fraction;
    /* Original 80FF/5FD0: fastest pseudo-exponential attack, one decay Ts
     * at sustain level 1, then infinite sustain. The first 41 source PCM
     * samples are zero (validated on load): the whole short attack is
     * silent even with the native linear interpolator. Thus every nonzero
     * output has ENVX 7FFF. Non-loop end clears ENVX and retires the voice.
     * See docs/AREA11_PANEL_SFX.md for the hardware/manual boundary. */
    const float envelope = 32767.0f / 32768.0f;
    stereo[0] = sample * (cue->gain_l / 16384.0f) * (envelope / 32768.0f);
    stereo[1] = sample * (cue->gain_r / 16384.0f) * (envelope / 32768.0f);
    return 1;
}

/* ---- EMSR v2 registry ------------------------------------------------- */

enum {
    EMSR_MAX_SAMPLES = 1024, EMSR_MAX_ENTRIES = 1024,
    EMSR_MAX_FRAMES = 1u << 22, EMSR_MAX_REASON = 10
};
/* Six authored bytes (<= 255 each) >> 27: the largest 001179E0 scalar. */
#define EMSR_MAX_SCALAR 2048477u
#define EMSR_NO_LOOP 0xFFFFFFFFu

void em_sfx_registry_free(EmSfxRegistry *registry)
{
    if (!registry) return;
    if (registry->samples)
        for (unsigned i = 0; i < registry->sample_count; ++i)
            free(registry->samples[i].pcm);
    free(registry->samples);
    free(registry->entries);
    free(registry->ladder);
    memset(registry, 0, sizeof *registry);
}

static int emsr_scope_valid(int area, int sub)
{
    if (area == -1 && sub == -1) return 1;
    /* 001FB9F0 indexes D_00264A70 by D_00810700/701 (24 areas, 8 subs). */
    return area >= 0 && area < 24 && sub >= 0 && sub < 8;
}

static int emsr_op_valid(const EmSfxRegistry *registry, const EmSfxOp *op,
                         const unsigned char *b)
{
    switch (op->kind) {
    case EM_SFX_OP_KEY_ON: {
        /* Pitch register range; tone flags 0x02 (noise, command 0x33) and
         * 0x20 (00115850 voice +0x14 modulation) are never reproduced. */
        if (b[25] || b[26] || b[27] || read32(b + 28) || !op->pitch ||
            op->pitch > 0x3FFF || op->scalar > EMSR_MAX_SCALAR ||
            (op->flags & 0x22) || op->sample >= registry->sample_count)
            return 0;
        /* The key-on word is 00117918 (bend 0x40) * 44100 / 48000. */
        const int32_t ladder = em_sfx_ladder(registry, op->center, op->note,
                                             op->fine, 0x40, op->range);
        return ladder >= 0 &&
               (uint32_t)((int64_t)ladder * 44100 / 48000) == op->pitch;
    }
    case EM_SFX_OP_PORTAMENTO:
        for (unsigned i = 5; i < 25; ++i)
            if (b[i]) return 0;
        return !b[27] && !read32(b + 28);
    case EM_SFX_OP_KEY_OFF:
    case EM_SFX_OP_END:
        for (unsigned i = (op->kind == EM_SFX_OP_END ? 3 : 5); i < 32; ++i)
            if (b[i]) return 0;
        return 1;
    default:
        return 0;
    }
}

int em_sfx_registry_load(EmSfxRegistry *registry, const char *path)
{
    if (!registry || !path) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    EmSfxRegistry next = {0};
    unsigned char b[32];
    int valid = 0;
    if (fread(b, 1, 24, file) != 24 || memcmp(b, "EMSR", 4) ||
        read32(b + 4) != 2 || read32(b + 20)) goto done;
    next.sample_count = read32(b + 8);
    next.entry_count = read32(b + 12);
    next.ladder_count = read32(b + 16);
    if (!next.sample_count || next.sample_count > EMSR_MAX_SAMPLES ||
        !next.entry_count || next.entry_count > EMSR_MAX_ENTRIES ||
        next.ladder_count != EM_SFX_LADDER_MAX) goto done;
    next.entries = calloc(next.entry_count, sizeof *next.entries);
    next.samples = calloc(next.sample_count, sizeof *next.samples);
    next.ladder = calloc(next.ladder_count, sizeof *next.ladder);
    if (!next.entries || !next.samples || !next.ladder) goto done;
    for (unsigned i = 0; i < next.ladder_count; ++i) {
        if (fread(b, 1, 2, file) != 2) goto done;
        next.ladder[i] = read16(b);
    }
    for (unsigned i = 0; i < next.entry_count; ++i) {
        EmSfxEntry *entry = &next.entries[i];
        if (fread(b, 1, 16, file) != 16) goto done;
        entry->id = read32(b);
        entry->area = (int16_t)read16(b + 4);
        entry->sub = (int16_t)read16(b + 6);
        entry->state = b[8];
        entry->count = b[9];
        entry->reason = read16(b + 10);
        entry->bank = read16(b + 12);
        if (read16(b + 14) || !emsr_scope_valid(entry->area, entry->sub))
            goto done;
        if (entry->state == EM_SFX_STATE_AUDIBLE) {
            if (!entry->count || entry->count > EM_SFX_OP_MAX ||
                entry->reason) goto done;
        } else if (entry->state == EM_SFX_STATE_ABSENT) {
            if (entry->count || entry->reason || entry->bank) goto done;
        } else if (entry->state == EM_SFX_STATE_UNSUPPORTED) {
            if (entry->count || !entry->reason || entry->bank ||
                entry->reason > EMSR_MAX_REASON) goto done;
        } else goto done;
        for (unsigned j = 0; j < i; ++j)
            if (next.entries[j].id == entry->id &&
                next.entries[j].area == entry->area &&
                next.entries[j].sub == entry->sub) goto done;
        unsigned key_ons = 0;
        for (unsigned j = 0; j < entry->count; ++j) {
            EmSfxOp *op = &entry->ops[j];
            if (fread(b, 1, 32, file) != 32) goto done;
            op->tick = read16(b);
            op->kind = b[2];
            op->note = b[3];
            op->prog = b[4];
            op->flags = b[5];
            op->sample = read16(b + 6);
            op->pitch = read16(b + 8);
            op->pan = read16(b + 10);
            op->scalar = read32(b + 12);
            op->adsr1 = read16(b + 16);
            op->adsr2 = read16(b + 18);
            op->center = b[20];
            op->fine = (int8_t)b[21];
            op->range = b[22];
            op->alloc = b[23];
            op->priority = b[24];
            op->length = b[25];
            op->depth = b[26];
            key_ons += op->kind == EM_SFX_OP_KEY_ON;
            /* The script ends with exactly one FF 2F, ticks never go back. */
            if ((j && op->tick < entry->ops[j - 1].tick) ||
                ((op->kind == EM_SFX_OP_END) != (j + 1 == entry->count)))
                goto done;
            if (!emsr_op_valid(&next, op, b)) goto done;
        }
        if (entry->state == EM_SFX_STATE_AUDIBLE && !key_ons) goto done;
    }
    for (unsigned i = 0; i < next.sample_count; ++i) {
        EmSfxSample *sample = &next.samples[i];
        if (fread(b, 1, 8, file) != 8) goto done;
        sample->frames = read32(b);
        sample->loop_start = read32(b + 4);
        if (!sample->frames || sample->frames > EMSR_MAX_FRAMES ||
            (sample->loop_start != EMSR_NO_LOOP &&
             sample->loop_start >= sample->frames)) goto done;
        const uint32_t total = sample->frames +
            (sample->loop_start == EMSR_NO_LOOP ? 0
                                                : sample->frames - sample->loop_start);
        sample->pcm = malloc(total * sizeof *sample->pcm);
        if (!sample->pcm) goto done;
        for (uint32_t j = 0; j < total; ++j) {
            if (fread(b, 1, 2, file) != 2) goto done;
            sample->pcm[j] = (int16_t)read16(b);
        }
    }
    if (fgetc(file) != EOF || ferror(file)) goto done;
    valid = 1;
done:
    fclose(file);
    if (!valid) {
        em_sfx_registry_free(&next);
        return 0;
    }
    em_sfx_registry_free(registry);
    *registry = next;
    return 1;
}

const EmSfxEntry *em_sfx_registry_find(const EmSfxRegistry *registry,
                                       unsigned id, int area, int sub)
{
    if (!registry) return NULL;
    for (unsigned i = 0; i < registry->entry_count; ++i) {
        const EmSfxEntry *entry = &registry->entries[i];
        if (entry->id == id && entry->area == area && entry->sub == sub)
            return entry;
    }
    return NULL;
}

/* One 001179E0 channel: (short)((scalar * pan_byte * request) >> 19), then
 * (word & 0xFFFF) >> 1. The shift is an arithmetic (floor) 64-bit shift. */
static uint16_t volume_word(uint32_t scalar, unsigned pan_byte, int32_t request)
{
    const int64_t product = (int64_t)scalar * (int64_t)pan_byte * request;
    const int64_t shifted = product >= 0 ? product / 524288
                                         : -((-product + 524287) / 524288);
    return (uint16_t)(((uint64_t)shifted & 0xFFFFu) >> 1);
}

void em_sfx_volume_words(uint32_t scalar, uint16_t pan, int32_t request_left,
                         int32_t request_right, uint16_t words[2])
{
    if (!words) return;
    /* 0011A218 stores the pair only when both lie in [-0x1000, 0x1000];
     * otherwise 00119EA0's 0x1000/0x1000 track defaults remain. */
    if (request_left < -0x1000 || request_left > 0x1000 ||
        request_right < -0x1000 || request_right > 0x1000)
        request_left = request_right = 0x1000;
    words[0] = volume_word(scalar, pan >> 8, request_left);
    words[1] = volume_word(scalar, pan & 0xFFu, request_right);
}

float em_sfx_volume_gain(uint16_t word)
{
    int value = word & 0x7FFF;
    if (value & 0x4000) value -= 0x8000;
    return (float)value / 16384.0f;
}

int32_t em_sfx_request_word(float gain)
{
    const float scaled = gain * 4096.0f;
    if (!(scaled > -2147483520.0f && scaled < 2147483520.0f)) return 0;
    return (int32_t)scaled;
}

uint64_t em_sfx_tick_frame(unsigned tick, unsigned device_rate)
{
    return (uint64_t)tick * device_rate * 1001u / 60000u;
}

static int32_t floor_shift2(int32_t value)
{
    return value >= 0 ? value / 4 : -((-value + 3) / 4);
}

int32_t em_sfx_ladder(const EmSfxRegistry *registry, int center, int note,
                      int fine, int bend, int range)
{
    if (!registry || !registry->ladder) return -1;
    /* 00117918: t = ((bend - 0x40) * range >> 2) + 0xD0 (sra). */
    const int32_t t = floor_shift2((bend - 0x40) * range) + 0xD0;
    int32_t index, d;
    if (center <= note) {
        d = note - center;
        index = (d % 12) * 16 + fine + t;
        if (index < 0 || index >= (int32_t)registry->ladder_count) return -1;
        /* lhu, then sllv: the shift amount is the low five bits. */
        return (int32_t)((uint32_t)registry->ladder[index] << ((d / 12) & 31));
    }
    d = center - note;
    index = (12 - d % 12) * 16 + fine + t;
    if (index < 0 || index >= (int32_t)registry->ladder_count) return -1;
    return (int32_t)((uint32_t)registry->ladder[index] >> ((d / 12 + 1) & 31));
}

/* ---- SPU2 ADSR hardware model ----------------------------------------- */

static void envelope_rate(int rate, int decrease, int exponential,
                          int32_t level, uint32_t *cycles, int32_t *step)
{
    const int shift = rate >> 2, index = rate & 3;
    const int32_t base = decrease ? -8 + index : 7 - index;
    uint32_t wait = shift > 11 ? 1u << (shift - 11 < 15 ? shift - 11 : 15) : 1u;
    int32_t size = shift < 11 ? base * (1 << (11 - shift)) : base;
    if (exponential && !decrease && level > 0x6000) wait *= 4;
    if (wait > 0x8000u) wait = 0x8000u;
    if (exponential && decrease) {
        const int32_t product = size * level;
        size = product >= 0 ? product >> 15 : -((-product + 32767) >> 15);
    }
    *cycles = wait;
    *step = size;
}

void em_sfx_envelope_key_on(EmSfxEnvelope *envelope, uint16_t adsr1,
                            uint16_t adsr2)
{
    envelope->adsr1 = adsr1;
    envelope->adsr2 = adsr2;
    envelope->phase = EM_SFX_ENV_ATTACK;
    envelope->level = 0;
    envelope->counter = 0;
}

void em_sfx_envelope_key_off(EmSfxEnvelope *envelope)
{
    if (envelope->phase == EM_SFX_ENV_OFF) return;
    envelope->phase = EM_SFX_ENV_RELEASE;
    envelope->counter = 0;
    if (envelope->level <= 0) {
        envelope->level = 0;
        envelope->phase = EM_SFX_ENV_OFF;
    }
}

void em_sfx_envelope_step(EmSfxEnvelope *envelope)
{
    const uint16_t a1 = envelope->adsr1, a2 = envelope->adsr2;
    uint32_t cycles;
    int32_t step;
    switch (envelope->phase) {
    case EM_SFX_ENV_ATTACK:
        envelope_rate((a1 >> 8) & 0x7F, 0, a1 >> 15, envelope->level,
                      &cycles, &step);
        if (++envelope->counter < cycles) return;
        envelope->counter = 0;
        envelope->level += step;
        if (envelope->level >= 0x7FFF) {
            envelope->level = 0x7FFF;
            envelope->phase = EM_SFX_ENV_DECAY;
        }
        return;
    case EM_SFX_ENV_DECAY: {
        const int32_t target = ((a1 & 0xF) + 1) << 11;
        if (envelope->level <= target) {
            envelope->phase = EM_SFX_ENV_SUSTAIN;
            envelope->counter = 0;
            return;
        }
        envelope_rate(((a1 >> 4) & 0xF) << 2, 1, 1, envelope->level,
                      &cycles, &step);
        if (++envelope->counter < cycles) return;
        envelope->counter = 0;
        envelope->level += step;
        if (envelope->level < 0) envelope->level = 0;
        if (envelope->level <= target) {
            envelope->phase = EM_SFX_ENV_SUSTAIN;
            envelope->counter = 0;
        }
        return;
    }
    case EM_SFX_ENV_SUSTAIN:
        envelope_rate((a2 >> 6) & 0x7F, (a2 >> 14) & 1, a2 >> 15,
                      envelope->level, &cycles, &step);
        if (++envelope->counter < cycles) return;
        envelope->counter = 0;
        envelope->level += step;
        if (envelope->level < 0) envelope->level = 0;
        if (envelope->level > 0x7FFF) envelope->level = 0x7FFF;
        return;
    case EM_SFX_ENV_RELEASE:
        envelope_rate((a2 & 0x1F) << 2, 1, (a2 >> 5) & 1, envelope->level,
                      &cycles, &step);
        if (++envelope->counter < cycles) return;
        envelope->counter = 0;
        envelope->level += step;
        if (envelope->level <= 0) {
            envelope->level = 0;
            envelope->phase = EM_SFX_ENV_OFF;
        }
        return;
    default:
        return;
    }
}

/* ---- Sound driver ------------------------------------------------------ */

static void voice_reset(EmSfxVoice *voice)
{
    /* 00118EC0 / driver init: memset, then +0x06/+0x22/+0x24/+0x26 = 0xFFFF
     * and +0x4E = 0x78. The SPU2 voice itself keeps running (at ENVX < 2). */
    const EmSfxVoice spu = *voice;
    memset(voice, 0, sizeof *voice);
    voice->owner = voice->bank = 0xFFFF;
    voice->base = 0x78;
    voice->pitch = spu.pitch;
    voice->volume[0] = spu.volume[0];
    voice->volume[1] = spu.volume[1];
    voice->adsr1 = spu.adsr1;
    voice->adsr2 = spu.adsr2;
    voice->sample = spu.sample;
    voice->next_sample = spu.next_sample;
    voice->on = spu.on;
    voice->envelope = spu.envelope;
    voice->phase = spu.phase;
    voice->frames = spu.frames;
    voice->steps = spu.steps;
}

void em_sfx_driver_init(EmSfxDriver *driver, const EmSfxRegistry *registry,
                        uint64_t stream_voices)
{
    memset(driver, 0, sizeof *driver);
    driver->registry = registry;
    for (unsigned i = 0; i < EM_SFX_VOICES; ++i) {
        EmSfxVoice *voice = &driver->voices[i];
        voice->owner = voice->bank = 0xFFFF;
        voice->base = 0x78;
        if (stream_voices >> i & 1) {
            voice->state = 1;
            voice->kind = 3;
        }
    }
}

static void track_start(EmSfxTrack *track, const EmSfxEntry *entry)
{
    memset(track, 0, sizeof *track);
    track->entry = entry;
    track->running = 1;            /* +0x34 */
    track->allocated = 1;          /* +0x32 */
    track->left = track->right = 0x1000;
    /* +0x42 = handle & 0x8000 ? 1 : 0 (00119EA0 movz; the decomp C has it
     * inverted). 001FB9F0 handles never carry 0x8000, so only the tone's
     * 0x80 flag routes a registry voice to the effect send. */
    track->effect = 0;
    track->pitch_scale = 0x1000;   /* 0011A270(track, 0x1000)             */
    track->pitch_dirty = 1;        /* +0x50                               */
}

int em_sfx_driver_start_at(EmSfxDriver *driver, int track,
                           const EmSfxEntry *entry, int32_t left,
                           int32_t right)
{
    if (!driver || !entry || entry->state != EM_SFX_STATE_AUDIBLE ||
        track < 0 || track >= EM_SFX_TRACKS ||
        driver->tracks[track].allocated || driver->tracks[track].running)
        return -1;
    track_start(&driver->tracks[track], entry);
    em_sfx_driver_request(driver, track, left, right);
    return track;
}

int em_sfx_driver_start(EmSfxDriver *driver, const EmSfxEntry *entry,
                        int32_t left, int32_t right)
{
    if (!driver) return -1;
    /* 00119EA0: the lowest track with +0x2E, +0x30 and +0x34 all clear. */
    for (int i = 0; i < EM_SFX_TRACKS; ++i)
        if (!driver->tracks[i].allocated && !driver->tracks[i].running)
            return em_sfx_driver_start_at(driver, i, entry, left, right);
    return -1;
}

void em_sfx_driver_request(EmSfxDriver *driver, int track, int32_t left,
                           int32_t right)
{
    if (!driver || track < 0 || track >= EM_SFX_TRACKS ||
        left < -0x1000 || left > 0x1000 || right < -0x1000 || right > 0x1000)
        return;
    driver->tracks[track].left = left;
    driver->tracks[track].right = right;
    driver->tracks[track].volume_dirty = 1;
}

static void emit(EmSfxDriver *driver, EmSfxCommandSink sink, void *context,
                 int command, int index, uint32_t a, uint32_t b);

static void driver_stop(EmSfxDriver *driver, int track, int hard,
                        EmSfxCommandSink sink, void *context)
{
    if (track < 0 || track >= EM_SFX_TRACKS) return;
    if (driver->tracks[track].allocated)
        memset(&driver->tracks[track], 0, sizeof driver->tracks[track]);
    for (int i = 0; i < EM_SFX_VOICES; ++i) {
        EmSfxVoice *voice = &driver->voices[i];
        if (voice->kind != 2 || voice->owner != track) continue;
        if (hard) {
            emit(driver, sink, context, 3, i, 0, 0);
            voice->state = 0;
        }
        driver->key_off |= 1ull << i;
        voice->release = 1;
    }
}

void em_sfx_driver_stop(EmSfxDriver *driver, int track, int hard,
                        EmSfxCommandSink sink, void *context)
{
    if (driver) driver_stop(driver, track, hard, sink, context);
}

int em_sfx_driver_status(const EmSfxDriver *driver, int track)
{
    if (!driver || track < 0 || track >= EM_SFX_TRACKS) return 0;
    return driver->tracks[track].allocated ? 2 : 0;
}

int32_t em_sfx_driver_envx(const EmSfxDriver *driver, int voice)
{
    if (!driver || voice < 0 || voice >= EM_SFX_VOICES) return 0;
    return driver->voices[voice].on ? driver->voices[voice].envelope.level : 0;
}

int em_sfx_driver_voices(const EmSfxDriver *driver)
{
    int count = 0;
    for (int i = 0; i < EM_SFX_VOICES; ++i) count += driver->voices[i].on;
    return count;
}

int em_sfx_driver_busy(const EmSfxDriver *driver)
{
    for (int i = 0; i < EM_SFX_TRACKS; ++i)
        if (driver->tracks[i].allocated || driver->tracks[i].running) return 1;
    for (int i = 0; i < EM_SFX_VOICES; ++i)
        if (driver->voices[i].on) return 1;
    return 0;
}

/* The SPU2 side of one 001157F0 command. */
static void apply(EmSfxDriver *driver, int command, int index, uint32_t a,
                  uint32_t b)
{
    const uint64_t mask = (uint64_t)(a & 0xFFFFFF) | (uint64_t)(b & 0xFFFFFF) << 24;
    switch (command) {
    case 6: driver->voices[index].pitch = (uint16_t)a; break;
    case 1:
        driver->voices[index].volume[0] = (uint16_t)a;
        driver->voices[index].volume[1] = (uint16_t)b;
        break;
    case 5:
        driver->voices[index].next_sample =
            a < driver->registry->sample_count ? &driver->registry->samples[a] : NULL;
        break;
    case 3:            /* the envelope reads the ADSR registers live */
        driver->voices[index].adsr1 = driver->voices[index].envelope.adsr1 =
            (uint16_t)a;
        driver->voices[index].adsr2 = driver->voices[index].envelope.adsr2 =
            (uint16_t)b;
        break;
    case 0xA:          /* KON: ENVX 0, attack, source from the start address */
        for (int i = 0; i < EM_SFX_VOICES; ++i) {
            EmSfxVoice *voice = &driver->voices[i];
            if (!(mask >> i & 1) || !voice->next_sample) continue;
            voice->sample = voice->next_sample;
            voice->on = 1;
            em_sfx_envelope_key_on(&voice->envelope, voice->adsr1, voice->adsr2);
            voice->phase = voice->frames = voice->steps = 0;
        }
        break;
    case 0xB:          /* KOFF: release from the current level */
        for (int i = 0; i < EM_SFX_VOICES; ++i) {
            EmSfxVoice *voice = &driver->voices[i];
            if (!(mask >> i & 1) || !voice->on) continue;
            em_sfx_envelope_key_off(&voice->envelope);
            if (voice->envelope.phase == EM_SFX_ENV_OFF) voice->on = 0;
        }
        break;
    default:           /* 0xC effect send, 0xD noise: dry native output */
        break;
    }
}

static void emit(EmSfxDriver *driver, EmSfxCommandSink sink, void *context,
                 int command, int index, uint32_t a, uint32_t b)
{
    apply(driver, command, index, a, b);
    if (sink) sink(context, command, index, a, b);
}

/* 00117428(tone[0], tone[1], bank), exactly as the shipped code behaves:
 * the pass-3 minimum trackers start at -1 and compare signed, so only a
 * released (+0x08) kind-1 voice is ever taken; kind-2 SFX voices are never
 * stolen and a full table returns -1. */
int em_sfx_driver_allocate(EmSfxDriver *driver, unsigned alloc,
                           unsigned priority, unsigned bank)
{
    int32_t best[3] = {-1, -1, -1};
    int candidate[3] = {-1, -1, -1};
    if (alloc) {
        for (int i = 0; i < EM_SFX_VOICES; ++i) {
            const unsigned index = driver->cursor % EM_SFX_VOICES;
            const EmSfxVoice *voice = &driver->voices[index];
            if (voice->kind == 2 && voice->alloc == alloc && voice->bank == bank)
                return (int)index;
            driver->cursor++;
        }
    }
    for (int i = 0; i < EM_SFX_VOICES; ++i) {
        const unsigned index = driver->cursor % EM_SFX_VOICES;
        const EmSfxVoice *voice = &driver->voices[index];
        if (!voice->state && voice->kind != 3) return (int)index;
        driver->cursor++;
    }
    for (int i = 0; i < EM_SFX_VOICES; ++i) {
        const unsigned index = driver->cursor % EM_SFX_VOICES;
        const EmSfxVoice *voice = &driver->voices[index];
        if (voice->release == 1) {
            if (voice->kind == 1) return (int)index;
            if (voice->kind == 2 && (int)priority >= voice->priority &&
                (int32_t)voice->serial < best[1]) {
                candidate[1] = (int)index;
                best[1] = voice->serial;
            }
        } else if (voice->kind == 1) {
            if ((int32_t)voice->serial < best[0]) {
                candidate[0] = (int)index;
                best[0] = voice->serial;
            }
        } else if (voice->kind == 2) {
            if ((int)priority >= voice->priority &&
                (int32_t)voice->serial < best[2]) {
                candidate[2] = (int)index;
                best[2] = voice->serial;
            }
        }
        driver->cursor++;
    }
    if (candidate[0] != -1) return candidate[0];
    if (candidate[1] != candidate[0]) return candidate[1];
    return candidate[2];
}

/* 00115850 for one KEY_ON operation (track = +0x18). */
static void key_on(EmSfxDriver *driver, int index, const EmSfxOp *op,
                   EmSfxCommandSink sink, void *context)
{
    EmSfxTrack *track = &driver->tracks[index];
    const int key = em_sfx_driver_allocate(driver, op->alloc, op->priority,
                                           track->entry->bank);
    if (key < 0) {
        driver->no_voice++;
        return;
    }
    const uint64_t mask = 1ull << key;
    EmSfxVoice *voice = &driver->voices[key];
    voice->release = op->flags & 1 ? 0 : 1;
    voice->sustain = op->flags & 1 ? 1 : 0;
    voice->state = 1;
    voice->note = op->note;
    voice->owner = (uint16_t)index;
    voice->serial = (uint16_t)driver->serial;
    voice->kind = 2;
    voice->age = 0;
    voice->priority = op->priority;
    voice->alloc = op->alloc;
    voice->bank = track->entry->bank;
    voice->pan = op->pan;
    voice->scalar = op->scalar;
    voice->fine = op->fine;
    voice->bend = 0x40;
    voice->range = op->range;
    voice->prog = op->prog;
    voice->center = op->center;
    voice->porta = 0;
    voice->base = 0x78;
    voice->remaining = 0;
    emit(driver, sink, context, 6, key, op->pitch, 0);
    uint16_t words[2];
    em_sfx_volume_words(op->scalar, op->pan, track->left, track->right, words);
    emit(driver, sink, context, 1, key, words[0], words[1]);
    emit(driver, sink, context, 5, key, op->sample, 0);
    emit(driver, sink, context, 3, key, op->adsr1, op->adsr2);
    driver->key_on |= mask;
    if (track->effect == 1 || (op->flags & 0x80)) driver->effect |= mask;
    else driver->effect &= ~mask;
    driver->noise &= ~mask;
    driver->serial++;
}

/* 001176E0: this track's sustained matches, or else other tracks'. */
static void key_off(EmSfxDriver *driver, int index, const EmSfxOp *op)
{
    uint64_t own = 0, other = 0;
    const unsigned bank = driver->tracks[index].entry->bank;
    for (int i = 0; i < EM_SFX_VOICES; ++i) {
        const EmSfxVoice *voice = &driver->voices[i];
        if (voice->state != 1 || voice->kind != 2 || voice->sustain != 1 ||
            voice->prog != op->prog || voice->note != op->note ||
            voice->bank != bank) continue;
        if (voice->owner == index) own |= 1ull << i;
        else other |= 1ull << i;
    }
    const uint64_t chosen = own ? own : other;
    for (int i = 0; i < EM_SFX_VOICES; ++i) {
        if (!(chosen >> i & 1)) continue;
        driver->voices[i].release = 1;
        driver->key_off |= 1ull << i;
    }
}

/* 00118078 (tick divisor D_0027F740+0x3A = 60 in both AREA11 captures). */
static void portamento(EmSfxDriver *driver, int index, const EmSfxOp *op)
{
    const EmSfxTrack *track = &driver->tracks[index];
    for (int i = 0; i < EM_SFX_VOICES; ++i) {
        EmSfxVoice *voice = &driver->voices[i];
        if (voice->state != 1 || voice->kind != 2 ||
            voice->bank != track->entry->bank || voice->prog != op->prog ||
            voice->note != op->note || voice->owner != index) continue;
        if (voice->remaining) voice->base = track->porta_note;
        voice->porta = 1;
        voice->depth = op->depth;
        voice->remaining = (uint16_t)((op->length << 2) * 60 / 60);
        voice->length = voice->remaining;
    }
}

static int32_t mips_div(int32_t a, int32_t b)
{
    return b ? a / b : 0;
}

/* 00116598 for one voice (the paths the registry scripts reach). */
static void voice_update(EmSfxDriver *driver, int index, EmSfxCommandSink sink,
                         void *context)
{
    EmSfxVoice *voice = &driver->voices[index];
    if (voice->state != 1 || voice->kind == 3 || voice->owner >= EM_SFX_TRACKS)
        return;
    EmSfxTrack *track = &driver->tracks[voice->owner];
    if (voice->porta == 1 || track->pitch_dirty) {
        int32_t center = voice->center, note = voice->note, fine = voice->fine;
        if (voice->porta == 1) {
            center = (0x78 + voice->center - voice->note) & 0xFFFF;
            if (voice->remaining & 0x7FFF) {
                voice->remaining = (uint16_t)((voice->remaining & 0x7FFF) - 1);
                const int32_t n = voice->length - voice->remaining;
                int32_t v;
                if (voice->depth & 0x80) {
                    note = (voice->base -
                            mips_div(mips_div(n * (0x100 - voice->depth), 10),
                                     voice->length)) & 0xFFFF;
                    v = mips_div(n * ((-(int32_t)voice->depth) & 0xFF) * 0xC0,
                                 voice->length * 0x78);
                    fine = (int16_t)(fine - v % 16);
                } else {
                    note = (voice->base +
                            mips_div(mips_div(n * voice->depth, 10),
                                     voice->length)) & 0xFFFF;
                    v = mips_div(n * voice->depth * 0xC0, voice->length * 0x78);
                    fine = (int16_t)(fine + v % 16);
                }
                if ((uint32_t)note <= 0xC) note = 0xC;
                if ((uint32_t)note >= 0xF3) note = 0xF3;
                track->porta_note = (uint16_t)note;
                if (!voice->remaining) {
                    voice->base = (uint16_t)note;
                    voice->porta = 0;
                }
            }
        }
        const int32_t ladder = em_sfx_ladder(driver->registry, center, note,
                                             fine, voice->bend, voice->range);
        const int32_t scaled = (int32_t)((uint32_t)ladder * track->pitch_scale) >> 12;
        emit(driver, sink, context, 6, index,
             (uint32_t)scaled * 0x1B9u / 0x1E0u, 0);
    }
    if (voice->kind == 2 && track->volume_dirty) {
        uint16_t words[2];
        em_sfx_volume_words(voice->scalar, voice->pan, track->left,
                            track->right, words);
        emit(driver, sink, context, 1, index, words[0], words[1]);
    }
}

void em_sfx_driver_tick(EmSfxDriver *driver, EmSfxCommandSink sink,
                        void *context)
{
    /* 00118EC0: free ended tracks nobody claims, reap silent voices. */
    for (int i = 0; i < EM_SFX_TRACKS; ++i) {
        EmSfxTrack *track = &driver->tracks[i];
        if (!track->ended || !track->allocated) continue;
        int claimed = 0;
        for (int j = 0; j < EM_SFX_VOICES && !claimed; ++j)
            claimed = driver->voices[j].owner == i;
        if (!claimed) memset(track, 0, sizeof *track);
    }
    for (int i = 0; i < EM_SFX_VOICES; ++i) {
        EmSfxVoice *voice = &driver->voices[i];
        const int32_t envx = driver->feedback ? driver->feedback[i]
                                              : voice->envelope.level;
        if (envx < 2 && voice->age >= 2 &&
            voice->release == 1 && voice->kind != 3)
            voice_reset(voice);
        voice->age++;
    }
    /* The track loop: running tracks dispatch this tick's operations. */
    for (int i = 0; i < EM_SFX_TRACKS; ++i) {
        EmSfxTrack *track = &driver->tracks[i];
        if (!track->running || !track->allocated || !track->entry) continue;
        const EmSfxEntry *entry = track->entry;
        while (track->next < entry->count &&
               entry->ops[track->next].tick == track->tick) {
            const EmSfxOp *op = &entry->ops[track->next++];
            if (op->kind == EM_SFX_OP_KEY_ON) key_on(driver, i, op, sink, context);
            else if (op->kind == EM_SFX_OP_KEY_OFF) key_off(driver, i, op);
            else if (op->kind == EM_SFX_OP_PORTAMENTO) portamento(driver, i, op);
            else {                        /* 00117C28 (SFX track) */
                track->running = 0;
                track->effect = 0;
                track->ended = 1;
                break;
            }
        }
        track->tick++;
    }
    /* 00116598 */
    for (int i = 0; i < EM_SFX_VOICES; ++i) voice_update(driver, i, sink, context);
    for (int i = 0; i < EM_SFX_TRACKS; ++i)
        driver->tracks[i].pitch_dirty = driver->tracks[i].volume_dirty = 0;
    /* Flush: NON, EON (when changed), KON, KOFF (commands 0xD/0xC/0xA/0xB). */
    if (driver->noise != driver->noise_sent) {
        emit(driver, sink, context, 0xD, 0, (uint32_t)(driver->noise & 0xFFFFFF),
             (uint32_t)(driver->noise >> 24 & 0xFFFFFF));
        driver->noise_sent = driver->noise;
    }
    if (driver->effect != driver->effect_sent) {
        emit(driver, sink, context, 0xC, 0, (uint32_t)(driver->effect & 0xFFFFFF),
             (uint32_t)(driver->effect >> 24 & 0xFFFFFF));
        driver->effect_sent = driver->effect;
    }
    if (driver->key_on) {
        const uint64_t mask = driver->key_on;
        driver->key_on = 0;
        emit(driver, sink, context, 0xA, 0, (uint32_t)(mask & 0xFFFFFF),
             (uint32_t)(mask >> 24 & 0xFFFFFF));
    }
    if (driver->key_off) {
        const uint64_t mask = driver->key_off;
        driver->key_off = 0;
        emit(driver, sink, context, 0xB, 0, (uint32_t)(mask & 0xFFFFFF),
             (uint32_t)(mask >> 24 & 0xFFFFFF));
    }
}

static int16_t source_at(const EmSfxSample *sample, uint64_t position)
{
    if (position < sample->frames) return sample->pcm[position];
    const uint64_t body = sample->frames - sample->loop_start;
    return sample->pcm[sample->frames + (position - sample->frames) % body];
}

int em_sfx_driver_render(EmSfxDriver *driver, float *out, unsigned frames,
                         unsigned device_rate)
{
    if (!driver || !device_rate || device_rate > 384000) return 0;
    const uint64_t denominator = 4096u * (uint64_t)device_rate;
    int produced = 0;
    for (int i = 0; i < EM_SFX_VOICES; ++i) {
        EmSfxVoice *voice = &driver->voices[i];
        if (!voice->on) continue;
        const EmSfxSample *sample = voice->sample;
        const int loops = sample->loop_start != EMSR_NO_LOOP;
        const uint16_t pitch = voice->pitch > 0x3FFF ? 0x3FFF : voice->pitch;
        const float left = em_sfx_volume_gain(voice->volume[0]);
        const float right = em_sfx_volume_gain(voice->volume[1]);
        for (unsigned f = 0; f < frames && voice->on; ++f) {
            /* The level used for output frame n is ENVX after
             * floor(n * 48000 / rate) SPU samples since key-on. */
            const uint64_t due = voice->frames * 48000u / device_rate;
            while (voice->steps < due && voice->envelope.phase != EM_SFX_ENV_OFF) {
                em_sfx_envelope_step(&voice->envelope);
                voice->steps++;
            }
            voice->steps = due;
            if (voice->envelope.phase == EM_SFX_ENV_OFF) {
                voice->on = 0;
                break;
            }
            const uint64_t index = voice->phase / denominator;
            if (!loops && index >= sample->frames) {
                voice->on = 0;             /* non-loop end: ENVX 0 */
                voice->envelope.level = 0;
                voice->envelope.phase = EM_SFX_ENV_OFF;
                break;
            }
            if (out) {
                const uint64_t next = !loops && index + 1 >= sample->frames
                                    ? sample->frames - 1 : index + 1;
                const float a = source_at(sample, index), b = source_at(sample, next);
                const float fraction =
                    (float)((double)(voice->phase % denominator) / (double)denominator);
                const float value = (a + (b - a) * fraction) / 32768.0f *
                                    ((float)voice->envelope.level / 32768.0f);
                out[2 * f] += value * left;
                out[2 * f + 1] += value * right;
            }
            produced++;
            voice->phase += 48000u * (uint64_t)pitch;
            voice->frames++;
            if (loops) {
                const uint64_t body = sample->frames - sample->loop_start;
                const uint64_t wrap = (sample->frames + body) * denominator;
                if (voice->phase >= wrap) voice->phase -= body * denominator;
            }
        }
    }
    return produced;
}

/* ---- 001FC3C0 / 001FC520 ------------------------------------------------ */

static int loop_cadence(int32_t frame, int16_t ordinal)
{
    /* addu, then signed div by 10: mfhi == 0. */
    return (int32_t)((uint32_t)frame + (uint32_t)(int32_t)ordinal) % 10 == 0;
}

int32_t em_sfx_service_step(const EmSfxLoopOps *ops, int32_t requested[EM_SFX_TRACKS],
                         const int32_t snapshot[EM_SFX_TRACKS], int32_t *handle,
                         unsigned id, int32_t frame, int16_t ordinal)
{
    const int32_t live = *handle;
    if (live != -1) {
        /* The original indexes D_00281C30 unchecked; a track index is all
         * 001FB9F0 ever stores here. Anything else is refused. */
        if (live < 0 || live >= EM_SFX_TRACKS) return *handle = -1;
        const int32_t current = snapshot[live];
        if (current == -1) return *handle = -1;
        if ((int32_t)id != current) {
            ops->stop(ops->context, live);
            requested[live] = -1;
            return *handle = -1;
        }
        if (!loop_cadence(frame, ordinal)) return live;
        /* 001FBDB0 */
        int32_t result = -1, left, right;
        if (ops->status(ops->context, live) == 2) {
            if (ops->gains(ops->context, &left, &right)) {
                ops->request(ops->context, live, left, right);
                result = live;
            } else {
                ops->stop(ops->context, live);
            }
        }
        *handle = result;
        if (result == -1) requested[live] = -1;
        return result;
    }
    if (!loop_cadence(frame, ordinal)) return -1;
    /* 001FBD50 */
    int32_t left, right, result = -1;
    if (ops->gains(ops->context, &left, &right))
        result = ops->start(ops->context, id, left, right);
    *handle = result;
    if (result != -1 && result >= 0 && result < EM_SFX_TRACKS)
        requested[result] = (int32_t)id;
    return result;
}

void em_sfx_service_release(const EmSfxLoopOps *ops,
                         int32_t requested[EM_SFX_TRACKS], int32_t *handle)
{
    if (*handle == -1) return;
    ops->stop(ops->context, *handle);
    if (*handle >= 0 && *handle < EM_SFX_TRACKS) requested[*handle] = -1;
    *handle = -1;
}
