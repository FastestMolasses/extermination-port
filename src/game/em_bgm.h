/* em_bgm.h — background-music service: the native mirror of the engine's
 * stream-BGM model (FINDINGS.md "Music cue table", 2026-06-10).
 *
 * Engine model being mirrored:
 *   - The boot ELF addresses 67 music cues through the fixed table
 *     D_0025DD30 (16 bytes/entry: start_sector / start_byte / byte_len /
 *     loop flag); the loop flag is 1 on the in-level BGM set (cues 3-16,
 *     20-26), which the engine plays as LOOPING level music.
 *   - func_001FB0B0(cue) writes the current-BGM global D_00810D38 and
 *     tail-calls func_001FAE70(1): FADE OUT whatever is streaming, then
 *     start the new cue — em_bgm_play() is that pair (path instead of cue
 *     id until the native cue table lands). em_bgm_stop(fade) is the stop
 *     half (func_001FABB0-style cut when fade == 0).
 *   - The PS2 pumps the stream from per-frame EE-side service calls (the
 *     frame loop's F/H/M audio-service slots, em_frame.c). Natively
 *     em_audio is pull-model — the OS audio thread mixes — so the
 *     per-frame em_bgm_service() only does the game-thread half: reclaim
 *     tracks the audio thread has finished switching away from.
 *
 * THREADING (per the em_audio.h contract): the game thread is the single
 * producer — it loads the whole WAV up front (so the audio thread only
 * ever reads memory), publishes the track pointer with an atomic serial,
 * and frees retired tracks only after the audio thread acknowledges (an
 * atomic serial ack) that it has adopted a newer publication. The audio
 * callback takes no locks, allocates nothing, and does no I/O; fades
 * (~1 s, the engine's func_001FAE70(1) fade-out-then-start shape) are
 * ramped sample-by-sample on the audio thread.
 */
#ifndef EM_BGM_H
#define EM_BGM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Start (or replace) the background music: load the PCM16 WAV at `path`
 * (mono or stereo; the in-level cue exports are 48 kHz stereo), fade in
 * over ~1 s, and loop it when `loop` is nonzero (the D_0025DD30 loop-flag
 * set). If music is already playing it fades out first, then the new
 * track fades in — the func_001FB0B0 -> func_001FAE70(1) sequence.
 * Call from the game thread only. Returns 0 on success, -1 on failure
 * (unreadable/unsupported WAV, no audio device); failure leaves any
 * current music untouched. */
int em_bgm_play(const char *path, int loop);

/* Stop the music: fade out over ~1 s when `fade` is nonzero, cut
 * immediately when 0 (the func_001FABB0 hard stop). Game thread only;
 * a no-op when nothing is playing. */
void em_bgm_stop(int fade);

/* Per-frame service — the frame loop's audio-service slot (steps F/H/M,
 * em_frame.c). Game thread only: frees swapped-out tracks once the audio
 * thread has acknowledged moving past them. Safe to call always, even
 * when no music was ever started. */
void em_bgm_service(void);

/* Tear down: destroy the audio device (blocks until the callback can no
 * longer fire — the em_audio.h teardown-ordering guarantee), free all
 * track memory, and print the delivered/played frame counters if music
 * ever ran (the audio-test-style exit line). Game thread only; safe when
 * nothing was ever played. */
void em_bgm_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_BGM_H */
