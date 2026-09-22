#ifndef EM_SFX_BANK_H
#define EM_SFX_BANK_H

#include <stdint.h>

enum { EM_SFX_CUE_ABSENT = 1, EM_SFX_CUE_REVERB = 2 };

typedef struct {
    uint32_t id, flags, frames;
    uint16_t pitch, adsr1, adsr2;
    int16_t gain_l, gain_r;
    int16_t *pcm;
} EmSfxCue;

typedef struct {
    unsigned area, sub, count;
    EmSfxCue cues[2];
} EmSfxBank;

/* EMSF v1: the audited AREA11.0 A0 panel bank. No engine data is embedded
 * in C. Unsupported profiles fail instead of silently losing parameters.
 * Load/free only before or after the shared audio device is running. */
int em_sfx_bank_load(EmSfxBank *bank, const char *path);
void em_sfx_bank_free(EmSfxBank *bank);

/* Native dry playback of the original driver parameters. Exact rational
 * source cursor, Q14 voice gains, and a manual-derived steady envelope.
 * Linear interpolation is a host boundary, not SPU2 Gaussian equality.
 * Returns zero at the non-loop sample end or for an absent cue. */
int em_sfx_cue_frame(const EmSfxCue *cue, uint64_t output_frame,
                     unsigned device_rate, float stereo[2]);

#endif
