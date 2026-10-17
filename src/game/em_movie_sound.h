/* em_movie_sound.h — the movie's sound on the shared output (em_bgm.h).
 *
 * The original plays a movie's PCM through the SPU2 (002065D0 moves it to
 * the IOP ring the SPU2 plays at 48 kHz; 00206520 keys it on, 00206320
 * stops it, each with the next field's RPC 0x64 exchange). The port renders
 * it as the other producers do: field n's frames in step n's movie vblank
 * hook (em_frame.h EmFrameMovieVblank), on the same 572/375 clock (field
 * n's samples end at floor((n + 1) * 800.8)), into a lock-free ring the
 * audio callback sums. The samples are the movie's PCM16 as they are, each
 * x / 32768 (what the system's own player output: no gain, no resampling);
 * em_replay's offline pull carries them like every producer's
 * (em_replay_audio.h). Which fields sound is em_movie_field.h's. */
#ifndef EM_MOVIE_SOUND_H
#define EM_MOVIE_SOUND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The frames field n holds on the 572/375 clock (800 or 801). */
uint32_t em_movie_sound_field_frames(uint64_t field);
/* Game thread: append `frames` interleaved stereo PCM16 frames. Returns the
 * frames that fitted (the rest are counted as overruns: nothing drains the
 * ring, e.g. no device). */
uint32_t em_movie_sound_push(const int16_t *pcm, uint32_t frames);
/* Audio thread: add the queued frames into `out` (interleaved float
 * stereo). Only at 48000 Hz (the movie's rate); other rates are counted. */
void em_movie_sound_mix(float *out, int frames, int device_rate);
/* Frames pushed / dropped so far (diagnostics). */
void em_movie_sound_counters(uint64_t *pushed, uint64_t *overruns, uint64_t *bad_rate);
/* The ring and its counters (host output and the audio thread's read
 * position), which the test of the New Game switch leaves out
 * (tools/test_new_game_switch.py). */
const void *em_movie_sound_host_state(size_t *size);

#ifdef __cplusplus
}
#endif

#endif /* EM_MOVIE_SOUND_H */
