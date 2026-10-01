/* em_replay.h — input recording and playback for the side-by-side video
 * comparison (the decomp's tools/video_compare/, docs/VIDEO_COMPARE.md).
 *
 * Not a game feature and not an original function: a host-side tool that
 * sits on the outer input layer (em_input_set_filter) and the audio device
 * layer (em_bgm offline output). It never changes what the game computes
 * from a given pad; it only records the pad the game reads, or supplies a
 * recorded one.
 *
 *   EM_INPUT_RECORD=<file>   write one row per main-loop step: the pad the
 *                            frame read (the raw DualShock bytes step C
 *                            unpacks) and the anchors the playback re-syncs
 *                            on (sync phase, area bytes, loader busy byte
 *                            D_00275BD8, selector 0x70003B8D, the slot-0
 *                            task's +9/+B, player position and heading).
 *   EM_INPUT_PLAY=<file>     replay such a recording: within a sync segment
 *                            tick for tick; at a segment boundary (a load,
 *                            a cutscene, a phase change) the next segment's
 *                            inputs start on this run's own first tick of
 *                            the next phase. Movies are skipped (START held)
 *                            when the recording skipped them. The run quits
 *                            when the recording is used up.
 *   EM_REPLAY_CAPTURE=<path> during playback, request a frame capture
 *                            (em_gfx_request_capture) at every captured
 *                            tick; a path with a "%u" takes the capture
 *                            index, otherwise every capture is written to
 *                            the same path (a FIFO the tool reads).
 *   EM_REPLAY_CAPTURE_EVERY=<k>  capture ticks whose segment offset is a
 *                            multiple of k (default 1), never title ticks.
 *   EM_REPLAY_AUDIO=<file.wav>   during playback, no audio device: the
 *                            mixer is rendered on the game thread, 800.8
 *                            frames per main-loop step at 48 kHz, into a
 *                            stereo PCM16 WAV (row column "af").
 *
 * With EM_INPUT_PLAY and EM_INPUT_RECORD together, the record file is the
 * playback log: the same format, with the live anchors and the pads that
 * were applied. The file format ("EMREC 1") is documented in the decomp's
 * docs/VIDEO_COMPARE.md. */
#ifndef EM_REPLAY_H
#define EM_REPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read the variables above and install the input filter when any is set.
 * Call once, after em_frame_init (whose own pad snapshot is not a step).
 * Returns 0, or -1 when a file cannot be opened or parsed (the caller
 * quits: a playback that cannot run must not silently play live input). */
int em_replay_install(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_REPLAY_H */
