/* em_audio_win.c — Windows audio output backend on WASAPI.
 *
 * Clean-room, no third-party libraries; only the WASAPI/MMDevice headers in
 * the Windows SDK. SKELETON — not yet implemented. Plan:
 *   - IMMDeviceEnumerator -> default render endpoint -> IAudioClient in
 *     shared mode, float32 interleaved stereo at the requested rate (shared
 *     mode resamples, mirroring the macOS backend's converter).
 *   - Event-driven buffering (AUDCLNT_STREAMFLAGS_EVENTCALLBACK): a dedicated
 *     thread waits on the event, asks IAudioRenderClient for the buffer, and
 *     invokes the EmAudioCallback — that thread IS the "audio thread" of the
 *     em_audio.h contract.
 *   - destroy: signal the thread, WaitForSingleObject it, then release the
 *     COM interfaces — preserving the "no callback after destroy" guarantee.
 */
#include "em_audio.h"
#include <stddef.h>

EmAudio *em_audio_create(int sample_rate, EmAudioCallback cb, void *user)
{ (void)sample_rate; (void)cb; (void)user; return NULL; } /* TODO */
void em_audio_destroy(EmAudio *audio) { (void)audio; }
void em_audio_pause(EmAudio *audio) { (void)audio; }
void em_audio_resume(EmAudio *audio) { (void)audio; }
