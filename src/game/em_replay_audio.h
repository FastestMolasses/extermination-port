/* em_replay_audio.h — where the replay's offline audio pull stops
 * (EM_REPLAY_AUDIO, em_replay.c). Host tooling only: no original function
 * stands behind it.
 *
 * Every sound producer renders field n in step n's field hook (main.c
 * stream_field: em_stream_live_field runs the SFX tick, em_sfx_field, then
 * the IOP field, em_iop_stream_field), before step C's pad filter, where
 * em_replay pulls the mix. On the 572/375 clock both producers use, field
 * n's samples end at floor((n + 1) * 800.8). The pull therefore runs
 * through field n: every producer's ring is empty after it, so samples one
 * field renders land at one WAV position whichever producer renders them,
 * as the SFX and the stream voices share one SPU2 output in the original.
 * Row n's "af" (the frames before its pull) is then field n's first sample.
 *
 * Until 2026-10-09 the pull stopped at n fields. The SFX ring (rendering
 * from the New Game's area load) was emptied by its first pull all the
 * same, but the IOP stream backend renders from step 0, whose pull took
 * nothing, so it kept one field queued for the whole run: its music and
 * voices landed one field after the SFX of the same field. The fork demo
 * measured it (docs/FIRST_LEVEL_AUDIT.md 1b item 8): Roger's cue 29 keys on
 * at the same row on both sides and was heard 0.97 field later on the
 * port, while every compared SFX lies within 0.2 field of the original's.
 * tests/replay_audio_test.c. */
#ifndef EM_REPLAY_AUDIO_H
#define EM_REPLAY_AUDIO_H

#include <stdint.h>

/* The frames the offline mix holds once step n's pull is done: fields
 * 0..n at 800.8 frames each (48 kHz against 59.94 fields per second). */
static inline uint64_t em_replay_audio_frames_through(uint64_t step)
{
    return (step + 1u) * 4004u / 5u;
}

#endif /* EM_REPLAY_AUDIO_H */
