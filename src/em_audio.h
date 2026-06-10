/* em_audio.h — audio output backend interface for the Extermination native
 * port.
 *
 * Clean-room: implemented per platform on the OS-native audio API
 * (AudioToolbox/AudioUnit on macOS, WASAPI on Windows, ALSA/PipeWire on
 * Linux). NO third-party libraries. Implementation files live under
 * src/audio/<os>/.
 *
 * PULL model: the backend owns the device and calls a user-supplied render
 * callback from the OS audio thread whenever the hardware needs frames. The
 * game side will eventually run its mixer inside this callback — decoded PS2
 * ADPCM streams (the original game streams at 48000 Hz) mixed into the
 * interleaved float stereo buffer the callback hands out. s16 sources are
 * converted to float by the mixer, not by this layer.
 *
 * THREADING CONTRACT:
 *   - The callback runs on a high-priority audio thread owned by the OS, NOT
 *     the thread that called em_audio_create. It may fire at any time between
 *     em_audio_create (or em_audio_resume) and em_audio_pause/em_audio_destroy.
 *   - The callback must be real-time safe: no locks shared with long critical
 *     sections, no malloc/free, no file or console I/O. Communicate with the
 *     game thread via atomics or lock-free queues.
 *   - The callback MUST fully fill `frames * 2` floats (interleaved L,R pairs,
 *     nominal range [-1,1]) every call; write zeros for silence.
 *   - em_audio_destroy blocks until any in-flight callback has returned and
 *     guarantees no further calls, so the `user` pointer may be freed after it.
 *   - em_audio_pause does NOT give that guarantee promptly on every platform;
 *     it only stops the device. Use destroy for teardown ordering.
 */
#ifndef EM_AUDIO_H
#define EM_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EmAudio EmAudio;

/* Render callback: fill out_interleaved_stereo with `frames` frames
 * (frames * 2 floats, L/R interleaved). See the threading contract above. */
typedef void (*EmAudioCallback)(void *user, float *out_interleaved_stereo,
                                int frames);

/* Open the default output device at `sample_rate` Hz, stereo, and start
 * pulling from `cb` (the device starts running before this returns). The
 * game uses 48000. Returns NULL on failure. */
EmAudio *em_audio_create(int sample_rate, EmAudioCallback cb, void *user);

/* Stop the device and release it. Blocks until the callback can no longer
 * fire. NULL is a no-op. */
void em_audio_destroy(EmAudio *audio);

/* Stop/restart pulling without tearing the device down (e.g. app loses
 * focus). While paused the OS emits silence; the callback is not invoked. */
void em_audio_pause(EmAudio *audio);
void em_audio_resume(EmAudio *audio);

#ifdef __cplusplus
}
#endif

#endif /* EM_AUDIO_H */
