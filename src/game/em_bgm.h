/* em_bgm.h — the shared audio output device.
 *
 * em_bgm owns the single em_audio device (em_audio.h pull model). Its
 * render callback sums the three native producers, each lock-free and
 * allocation-free on the audio thread:
 *   - em_sfx_mix            the SFX driver's voices (em_sfx.h);
 *   - em_startup_audio_mix  the startup sequencer (em_startup_audio.h);
 *   - em_stream_live_mix    the music and voice streams: the SPU output of
 *                           the IOP stream backend the original stream
 *                           lanes drive (em_stream_live.h, WP-8b).
 * The streams have no player here any more: the lanes (001FA790, 001FAE70,
 * 001FABB0, ...) own them, and the IOP backend renders their samples on the
 * game thread, one NTSC field per frame (docs/IOP_STREAM.md "Binding").
 *
 * It also holds the zero-dependency PCM16 WAV reader the SFX and startup
 * samples share.
 */
#ifndef EM_BGM_H
#define EM_BGM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tear down: destroy the audio device (blocks until the callback can no
 * longer fire — the em_audio.h teardown-ordering guarantee). Game thread
 * only; safe when no device was ever created. */
void em_bgm_shutdown(void);

/* Decoded PCM16 WAV — the module's shared zero-dependency reader output
 * (whole file preloaded; the audio thread only ever reads memory). */
typedef struct {
    int16_t *pcm;       /* malloc'd interleaved s16; caller frees */
    long     nframes;
    int      channels;  /* 1 or 2 */
    int      rate;
} EmBgmWav;

/* Read a PCM16 mono/stereo RIFF/WAVE file whole. `tag` prefixes the
 * stderr diagnostics ("sfx"/"startup-audio"). Returns 0 and fills *out
 * (pcm malloc'd) on success, -1 on failure. Game thread only. */
int em_bgm_wav_read(const char *path, EmBgmWav *out, const char *tag);

/* Make sure the shared output device exists (created at `sample_rate`
 * with the mixing callback when none exists yet). Returns 0 when a device
 * is running, -1 on creation failure. Game thread only. */
int em_bgm_device_ensure(int sample_rate);

/* The running device's sample rate; 0 = no device yet. Game thread. */
int em_bgm_device_rate(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_BGM_H */
