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

/* ---- EMSR v1: the per-area SFX registry (tools/export_sfx_registry.py) ----
 * Every entry is one sound id resolved through the original A0 path for one
 * scope: area (-1,-1) = the global record table's group-1 banks, which are
 * area-independent; any other (area, sub) = an area-tabled id or a record
 * bound to that area's region banks. docs/SFX_PITCH.md has the byte layout. */
enum {
    EM_SFX_STATE_AUDIBLE = 1,     /* all events reproducible natively      */
    EM_SFX_STATE_ABSENT = 2,      /* original 001FB9F0/00119EA0 return -1  */
    EM_SFX_STATE_UNSUPPORTED = 3, /* needs driver features not reproduced  */
    EM_SFX_EVENT_MAX = 8
};

typedef struct {
    uint16_t tick;        /* sequencer tick the A0 event is dispatched on */
    uint16_t sample;      /* registry sample index                        */
    uint16_t pitch;       /* SPU2 pitch word, 4096 = 48000 Hz             */
    uint16_t pan;         /* D_00242630 word: left byte << 8 | right byte */
    uint32_t scalar;      /* 001179E0 six-way product >> 27               */
    uint16_t adsr1, adsr2;/* authored envelope registers (not applied)    */
    uint8_t flags;        /* tone flags (0x80 = reverb, not applied)      */
} EmSfxEvent;

typedef struct {
    uint32_t id;
    int16_t area, sub;
    uint8_t state, count;
    uint16_t reason;      /* UNSUPPORTED reason code (docs/SFX_PITCH.md) */
    EmSfxEvent events[EM_SFX_EVENT_MAX];
} EmSfxEntry;

typedef struct {
    uint32_t frames;      /* decoded source frames at the 48 kHz base clock */
    int16_t *pcm;
} EmSfxSample;

typedef struct {
    unsigned entry_count, sample_count;
    EmSfxEntry *entries;
    EmSfxSample *samples;
} EmSfxRegistry;

/* Transactional: on any validation failure the old registry is kept and
 * zero is returned. Load/free only while no audio callback can read it. */
int em_sfx_registry_load(EmSfxRegistry *registry, const char *path);
void em_sfx_registry_free(EmSfxRegistry *registry);
/* Exact (id, area, sub) match; NULL when absent from the registry. */
const EmSfxEntry *em_sfx_registry_find(const EmSfxRegistry *registry,
                                       unsigned id, int area, int sub);

/* 0011A218 + 001179E0 (D_0027F778 mono flag 0): the two SPU volume words
 * for one voice. Requests outside [-0x1000, 0x1000] leave the track at the
 * 00119EA0 defaults 0x1000/0x1000, as 0011A218 refuses the pair. */
void em_sfx_volume_words(uint32_t scalar, uint16_t pan, int32_t request_left,
                         int32_t request_right, uint16_t words[2]);
/* Fixed-mode SPU2 volume word as a gain: 15-bit two's complement,
 * gain = value / 0x4000, range -0x4000..0x3FFF (bit 14 is the sign, so
 * 0x4000 itself is -1.0). */
float em_sfx_volume_gain(uint16_t word);
/* 001FBF50's float_to_int(4096 * gain): truncation toward zero. */
int32_t em_sfx_request_word(float gain);
/* Output frame of a sequencer tick at the NTSC field rate 60000/1001. */
uint64_t em_sfx_tick_frame(unsigned tick, unsigned device_rate);
/* Output frames an event voice lasts (non-loop end), 0 on bad input. */
uint64_t em_sfx_event_frames(const EmSfxSample *sample, uint16_t pitch,
                             unsigned device_rate);
/* Linear-interpolated source sample (-1..1) at an output frame relative to
 * the event start. Returns zero past the end or on bad input. */
int em_sfx_event_sample(const EmSfxSample *sample, uint16_t pitch,
                        uint64_t output_frame, unsigned device_rate,
                        float *value);

#endif
