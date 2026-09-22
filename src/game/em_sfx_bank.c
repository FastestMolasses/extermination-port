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
