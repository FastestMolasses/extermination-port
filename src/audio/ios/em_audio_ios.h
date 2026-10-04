/* em_audio_ios.h — iOS-only hooks of the AudioUnit backend (em_audio_ios.m)
 * for the UIKit platform layer (src/platform/ios). The game never calls
 * these; it sees only the em_audio.h contract.
 */
#ifndef EM_AUDIO_IOS_H
#define EM_AUDIO_IOS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Configure and activate the app's AVAudioSession: category Playback (the
 * game is audible with the ring/silent switch on silent, as a game the
 * player started), 48000 Hz preferred. Idempotent; the platform layer calls
 * it at launch so the movie player (AVPlayer) and the AudioUnit share it.
 * 0 on success, -1 when the session refused (the game still runs). */
int em_audio_ios_session_begin(void);

/* The platform layer parks the game thread while the app is not active
 * (no game ticks, no GPU work from the background). suspended != 0 stops
 * every open output unit, so the mixer is not pulled while the game is
 * frozen; 0 restarts them. Main thread only. */
void em_audio_ios_set_suspended(int suspended);

#ifdef __cplusplus
}
#endif

#endif /* EM_AUDIO_IOS_H */
