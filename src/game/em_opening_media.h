/* The AREA11 opening's streamed audio (music cue 63) and its fade track.
 * Assets are exported locally from the original music/timing tables
 * (tools/export_opening_media.py). The dialogue lines are the message
 * service's (em_message_live.h, WP-8). PCM decode uses the project's ADPCM
 * exporter; hardware interpolation, SPU2 mixing and device latency are not
 * claimed to be waveform-identical.
 *
 * This is the port's stand-in for stream lane 0 while it plays cue 63 (the
 * original lanes, em_stream_lanes_original, are not live): 001FA790(0, 63)
 * arms it (em_opening_media_audio_start), and its lane service at step H
 * follows the lane's hold byte D_008106F4: 2 (set by 001FD4C0) becomes 1
 * once the prefill is in (the port's read completes at once, as its other
 * readers do), and 0 (the message service's 001FD580 on the stream row of
 * line 0x66) starts the sound, as 001F9CF0 state 2 keys the voice on. */
#ifndef EM_OPENING_MEDIA_H
#define EM_OPENING_MEDIA_H
#include <stdint.h>

/* Game-thread service. prepare loads once, before the first audio_start;
 * reuse is allowed. shutdown must follow em_bgm_shutdown/device teardown. */
int em_opening_media_prepare(const char *directory);
/* The canonical hold byte D_008106F4 the lane service reads and writes;
 * NULL detaches the lane service. */
void em_opening_media_set_hold(uint8_t *hold);
/* 001FA790(0, 63): arm the stream (prefill); not audible until the hold
 * byte is released. -1 when not prepared. */
int em_opening_media_audio_start(void);
/* 001FAD70/001FA330: subtract 16383/ticks on each ordinary service. */
void em_opening_media_fade_out(int ticks);
int em_opening_media_resume_music(unsigned fade_ticks);
void em_opening_media_tick(void);
void em_opening_media_camera_tick(float camera_time);
void em_opening_media_stop(void);
void em_opening_media_shutdown(void);
/* The shared device callback calls this after BGM and one-shot mixing. */
void em_opening_media_mix(float *out, int frames, int device_rate);
#endif
