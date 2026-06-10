/* em_audio_linux.c — Linux audio output backend on ALSA (or PipeWire).
 *
 * Clean-room, no third-party libraries; only the system's own sound API —
 * ALSA (libasound, the kernel's userspace API) or, where it is the system
 * layer, PipeWire's native API. SKELETON — not yet implemented. Plan:
 *   - snd_pcm_open("default", PLAYBACK), hw params: SND_PCM_FORMAT_FLOAT_LE,
 *     interleaved access, 2 channels, requested rate (let the "default"
 *     plug device resample, mirroring the macOS backend's converter).
 *   - A dedicated thread loops snd_pcm_writei: it invokes the EmAudioCallback
 *     into a staging buffer and writes it — that thread IS the "audio thread"
 *     of the em_audio.h contract. Recover from xruns via snd_pcm_recover,
 *     writing silence on callback-unavailable so the speaker never gets
 *     garbage.
 *   - destroy: flag the thread, pthread_join, snd_pcm_drain/close —
 *     preserving the "no callback after destroy" guarantee.
 */
#include "em_audio.h"
#include <stddef.h>

EmAudio *em_audio_create(int sample_rate, EmAudioCallback cb, void *user)
{ (void)sample_rate; (void)cb; (void)user; return NULL; } /* TODO */
void em_audio_destroy(EmAudio *audio) { (void)audio; }
void em_audio_pause(EmAudio *audio) { (void)audio; }
void em_audio_resume(EmAudio *audio) { (void)audio; }
