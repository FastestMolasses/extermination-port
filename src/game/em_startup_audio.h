/* Startup-only SShd note sequencer, traced from 001152D8/00119EA0.
 * The exporter preserves the original integer pitch and stereo registers.
 * Playback is a dry PCM approximation: SPU2 Gaussian interpolation, ADSR,
 * effect processing and hardware voice stealing are NOT yet reproduced.
 * Do not describe the resulting audio waveform as byte-faithful.
 */
#ifndef EM_STARTUP_AUDIO_H
#define EM_STARTUP_AUDIO_H

#include <stddef.h>
#include <stdint.h>

enum { EM_STARTUP_AUDIO_TRACKS = 48, EM_STARTUP_AUDIO_EVENTS = 16 };

typedef struct {
    uint32_t wait;               /* cumulative VLQ delta, not frames */
    uint16_t sample, pitch;      /* SPU pitch: 4096 = 48000 Hz */
    uint16_t left, right;        /* direct SPU volume register words */
    uint16_t adsr1, adsr2;
    uint8_t flags;
} EmStartupNote;

typedef struct {
    unsigned id, count;
    EmStartupNote notes[EM_STARTUP_AUDIO_EVENTS];
} EmStartupCue;

typedef struct {
    const EmStartupCue *cue;
    uint64_t elapsed;
    unsigned next;
} EmStartupAudioTrack;

typedef struct {
    EmStartupAudioTrack tracks[EM_STARTUP_AUDIO_TRACKS];
} EmStartupSequencer;

typedef void (*EmStartupNoteFn)(void *user, const EmStartupNote *note);

/* Pure, allocation-free scheduler. The first notes occur on the next tick;
 * cumulative waits are tested against 0,8,16,... (60 Hz startup setting).
 * End-of-script releases the track, not its A0 one-shot sample voices.
 * Cue memory must remain alive until all of its events have been emitted. */
void em_startup_sequencer_clear(EmStartupSequencer *seq);
int em_startup_sequencer_play(EmStartupSequencer *seq, const EmStartupCue *cue);
void em_startup_sequencer_tick(EmStartupSequencer *seq,
                               EmStartupNoteFn emit, void *user);

/* Game thread. Init preloads all samples before creating the shared device;
 * it fails on missing/malformed data, without substituting any sound. Do not
 * call init while the shared device exists. Shutdown AFTER em_bgm_shutdown.
 * Startup audio never owns a second device. Returns 0 success, -1 error. */
int em_startup_audio_init(const char *manifest);
int em_startup_audio_play(unsigned cue);
int em_startup_audio_tick(void);  /* once per sequencer VBlank, also in movies */
void em_startup_audio_stop(void); /* cancel pending scripts and sample voices */
void em_startup_audio_shutdown(void);

/* Audio callback only. Adds to interleaved stereo float PCM. No allocation,
 * file I/O or locks. Safe no-op before initialization. */
void em_startup_audio_mix(float *out, int frames, int device_rate);

#endif
