/* Original AREA11 dialogue and streamed opening audio.
 * Assets are exported locally from original message/timing/music tables.
 * PCM decode uses the project's ADPCM exporter; hardware interpolation,
 * SPU2 mixing and device latency are not claimed to be waveform-identical. */
#ifndef EM_OPENING_MEDIA_H
#define EM_OPENING_MEDIA_H
#include <stdint.h>
#include "em_gfx.h"

typedef struct {
    uint16_t line, duration;
    int16_t voice;
    uint8_t speaker, terminal, skew;
    const char *text;
} EmOpeningLine;

typedef struct {
    const EmOpeningLine *lines;
    unsigned count, next, displayed, remaining;
    int active, loaded;
} EmOpeningDialogue;

/* 001FD790/001FD950: duration N is drawn N+1 times; zero-duration,
 * nonterminal records are skipped. No keyboard dismissal/typewriter.
 * next/remaining/loaded mirror D_002821B0 +0x60/+0x6C/+0x5C after each
 * 001FCA10 message tick: a completed line leaves the timer at zero and the
 * following record's duration is loaded on the next tick. */
void em_opening_dialogue_start(EmOpeningDialogue *d,
                              const EmOpeningLine *lines, unsigned count);
void em_opening_dialogue_tick(EmOpeningDialogue *d);
const EmOpeningLine *em_opening_dialogue_line(const EmOpeningDialogue *d);
unsigned em_opening_dialogue_talk_mask(const EmOpeningDialogue *d);

/* Game-thread service. prepare loads once, before the first audio_start;
 * reuse is allowed. shutdown must follow em_bgm_shutdown/device teardown. */
int em_opening_media_prepare(const char *directory);
int em_opening_media_audio_start(void); /* prefill/arm, not audible yet */
int em_opening_media_audio_ready(void);
int em_opening_media_dialogue_start(void); /* releases prefilled stream */
/* 001FAD70/001FA330: subtract 16383/ticks on each ordinary service. */
void em_opening_media_fade_out(int ticks);
int em_opening_media_resume_music(unsigned fade_ticks);
void em_opening_media_tick(void);
void em_opening_media_camera_tick(float camera_time);
/* Original speaker metadata for the current draw; actor behavior decides
 * how the 001FD950 talk mailbox affects its pose. No guessed mouth motion. */
const EmOpeningLine *em_opening_media_line(void);
/* 001FD950 talks while its pre-service timer is positive. The completion
 * draw still shows the line, but releases its speaker's mouth targets. */
unsigned em_opening_media_talk_mask(void);
void em_opening_media_render(EmGfx *gfx);
void em_opening_media_stop(void);
void em_opening_media_shutdown(void);
/* The shared device callback calls this after BGM and one-shot mixing. */
void em_opening_media_mix(float *out, int frames, int device_rate);
#endif
