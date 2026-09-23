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

/* ---- EMSR v1 registry ------------------------------------------------- */

enum {
    EMSR_MAX_SAMPLES = 1024, EMSR_MAX_ENTRIES = 1024,
    EMSR_MAX_FRAMES = 1u << 22, EMSR_MAX_REASON = 10
};
/* Six authored bytes (<= 255 each) >> 27: the largest 001179E0 scalar. */
#define EMSR_MAX_SCALAR 2048477u

void em_sfx_registry_free(EmSfxRegistry *registry)
{
    if (!registry) return;
    if (registry->samples)
        for (unsigned i = 0; i < registry->sample_count; ++i)
            free(registry->samples[i].pcm);
    free(registry->samples);
    free(registry->entries);
    memset(registry, 0, sizeof *registry);
}

static int emsr_scope_valid(int area, int sub)
{
    if (area == -1 && sub == -1) return 1;
    /* 001FB9F0 indexes D_00264A70 by D_00810700/701 (24 areas, 8 subs). */
    return area >= 0 && area < 24 && sub >= 0 && sub < 8;
}

int em_sfx_registry_load(EmSfxRegistry *registry, const char *path)
{
    if (!registry || !path) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    EmSfxRegistry next = {0};
    unsigned char b[20];
    int valid = 0;
    if (fread(b, 1, 20, file) != 20 || memcmp(b, "EMSR", 4) ||
        read32(b + 4) != 1 || read32(b + 16)) goto done;
    next.sample_count = read32(b + 8);
    next.entry_count = read32(b + 12);
    if (!next.sample_count || next.sample_count > EMSR_MAX_SAMPLES ||
        !next.entry_count || next.entry_count > EMSR_MAX_ENTRIES) goto done;
    next.entries = calloc(next.entry_count, sizeof *next.entries);
    next.samples = calloc(next.sample_count, sizeof *next.samples);
    if (!next.entries || !next.samples) goto done;
    for (unsigned i = 0; i < next.entry_count; ++i) {
        EmSfxEntry *entry = &next.entries[i];
        if (fread(b, 1, 16, file) != 16) goto done;
        entry->id = read32(b);
        entry->area = (int16_t)read16(b + 4);
        entry->sub = (int16_t)read16(b + 6);
        entry->state = b[8];
        entry->count = b[9];
        entry->reason = read16(b + 10);
        if (read32(b + 12) || !emsr_scope_valid(entry->area, entry->sub))
            goto done;
        if (entry->state == EM_SFX_STATE_AUDIBLE) {
            if (!entry->count || entry->count > EM_SFX_EVENT_MAX ||
                entry->reason) goto done;
        } else if (entry->state == EM_SFX_STATE_ABSENT) {
            if (entry->count || entry->reason) goto done;
        } else if (entry->state == EM_SFX_STATE_UNSUPPORTED) {
            if (entry->count || !entry->reason ||
                entry->reason > EMSR_MAX_REASON) goto done;
        } else goto done;
        for (unsigned j = 0; j < i; ++j)
            if (next.entries[j].id == entry->id &&
                next.entries[j].area == entry->area &&
                next.entries[j].sub == entry->sub) goto done;
        for (unsigned j = 0; j < entry->count; ++j) {
            EmSfxEvent *event = &entry->events[j];
            if (fread(b, 1, 20, file) != 20) goto done;
            event->tick = read16(b);
            event->sample = read16(b + 2);
            event->pitch = read16(b + 4);
            event->pan = read16(b + 6);
            event->scalar = read32(b + 8);
            event->adsr1 = read16(b + 12);
            event->adsr2 = read16(b + 14);
            event->flags = b[16];
            /* Pitch register range; tone flags 0x02 (noise, command 0x33)
             * and 0x20 (00115850 voice +0x14) are never reproduced. */
            if (b[17] || read16(b + 18) || event->sample >= next.sample_count ||
                !event->pitch || event->pitch > 0x3FFF ||
                event->scalar > EMSR_MAX_SCALAR || (event->flags & 0x22) ||
                (j && event->tick < entry->events[j - 1].tick)) goto done;
        }
    }
    for (unsigned i = 0; i < next.sample_count; ++i) {
        EmSfxSample *sample = &next.samples[i];
        if (fread(b, 1, 4, file) != 4) goto done;
        sample->frames = read32(b);
        if (!sample->frames || sample->frames > EMSR_MAX_FRAMES) goto done;
        sample->pcm = malloc(sample->frames * sizeof *sample->pcm);
        if (!sample->pcm) goto done;
        for (uint32_t j = 0; j < sample->frames; ++j) {
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

uint64_t em_sfx_event_frames(const EmSfxSample *sample, uint16_t pitch,
                             unsigned device_rate)
{
    if (!sample || !sample->frames || !pitch || !device_rate ||
        device_rate > 384000) return 0;
    const uint64_t numerator = 48000u * (uint64_t)pitch;
    const uint64_t denominator = 4096u * (uint64_t)device_rate;
    return (sample->frames * denominator + numerator - 1) / numerator;
}

int em_sfx_event_sample(const EmSfxSample *sample, uint16_t pitch,
                        uint64_t output_frame, unsigned device_rate,
                        float *value)
{
    if (!value || !sample || !sample->pcm ||
        output_frame >= em_sfx_event_frames(sample, pitch, device_rate))
        return 0;
    const uint64_t numerator = 48000u * (uint64_t)pitch;
    const uint64_t denominator = 4096u * (uint64_t)device_rate;
    const uint64_t phase = output_frame * numerator;
    const uint32_t index = (uint32_t)(phase / denominator);
    const uint32_t next = index + 1 < sample->frames ? index + 1 : index;
    const float fraction =
        (float)((double)(phase % denominator) / (double)denominator);
    *value = (sample->pcm[index] +
              (sample->pcm[next] - sample->pcm[index]) * fraction) / 32768.0f;
    return 1;
}
