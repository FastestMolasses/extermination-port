/* replay_audio_test.c — the replay's offline audio pull (em_replay_audio.h,
 * EM_REPLAY_AUDIO) puts the samples one field renders at one WAV position,
 * whichever producer renders them.
 *
 * The run's order, as em_replay.c and main.c have it: each step's field
 * hook renders field n of every running producer (the IOP stream backend
 * from step 0, the SFX from the New Game's area load, each on its own
 * 572/375 clock), then step C's pad filter pulls the mix up to
 * em_replay_audio_frames_through(n), each producer's ring giving what it
 * holds (a ring that holds less leaves the rest silent, as em_sfx_mix and
 * em_iop_stream_mix do). The model below runs that order over two
 * producers and checks where each field's first sample lands against the
 * row's "af" (the frames before the step's pull):
 *   - the stream (from step 0): exactly at af, and its ring empty after
 *     every pull; the pull that stopped at n fields left it one field
 *     queued (the fork demo's 0.97-field late music; em_replay_audio.h);
 *   - the SFX (from step 37, its own clock phase): within one frame of af,
 *     the phase difference between the two clocks.
 * Host tooling only; nothing here is original code. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "game/em_replay_audio.h"

enum { STEPS = 3000, SFX_START = 37, RING = 4096 };

typedef struct {
    int64_t start;               /* the step whose hook renders its first field */
    uint32_t tag[RING];          /* the field each queued frame belongs to */
    uint64_t head, tail;         /* queued frames tag[head % RING .. tail % RING) */
    int64_t first_at[STEPS];     /* output position of each field's first frame */
} Producer;

/* Frames through field k of a producer's own clock: 525 half-lines a field
 * at 572 / 375 frames per half-line (em_sfx.c, em_iop_stream.c). */
static uint64_t clock_frames(uint64_t fields) { return fields * 525u * 572u / 375u; }

static int render(Producer *p, int64_t step)
{
    if (step < p->start)
        return 0;
    const uint64_t k = (uint64_t)(step - p->start);
    const uint64_t n = clock_frames(k + 1) - clock_frames(k);
    for (uint64_t i = 0; i < n; ++i) {
        if (p->tail - p->head >= RING) {
            fprintf(stderr, "replay audio test: ring overflow at step %lld\n", (long long)step);
            return -1;
        }
        p->tag[p->tail++ % RING] = (uint32_t)step;
    }
    return 0;
}

static void pull(Producer *p, uint64_t out, uint64_t want)
{
    for (uint64_t i = 0; i < want && p->head < p->tail; ++i) {
        const uint32_t field = p->tag[p->head++ % RING];
        if (p->first_at[field] < 0)
            p->first_at[field] = (int64_t)(out + i);
    }
}

int main(void)
{
    static Producer stream, sfx;
    static uint64_t af[STEPS];
    stream.start = 0;
    sfx.start = SFX_START;
    for (int i = 0; i < STEPS; ++i)
        stream.first_at[i] = sfx.first_at[i] = -1;
    int fails = 0;
    uint64_t out = 0;
    for (int64_t step = 0; step < STEPS; ++step) {
        if (render(&stream, step) || render(&sfx, step))
            return 1;
        af[step] = out;
        const uint64_t through = em_replay_audio_frames_through((uint64_t)step);
        if (through < out) {
            fprintf(stderr, "FAIL: the pull goes backwards at step %lld\n", (long long)step);
            return 1;
        }
        pull(&stream, out, through - out);
        pull(&sfx, out, through - out);
        out = through;
        if (stream.tail != stream.head && fails++ < 5)
            fprintf(stderr, "FAIL: step %lld leaves %llu stream frames queued\n", (long long)step,
                    (unsigned long long)(stream.tail - stream.head));
    }
    for (int64_t step = 0; step < STEPS; ++step) {
        if (stream.first_at[step] != (int64_t)af[step] && fails++ < 10)
            fprintf(stderr, "FAIL: the stream's field %lld starts at %lld, row af %llu\n", (long long)step,
                    (long long)stream.first_at[step], (unsigned long long)af[step]);
        if (step < SFX_START)
            continue;
        const int64_t d = sfx.first_at[step] - (int64_t)af[step];
        if ((sfx.first_at[step] < 0 || d < -1 || d > 1) && fails++ < 10)
            fprintf(stderr, "FAIL: the SFX's field %lld starts at %lld, row af %llu\n", (long long)step,
                    (long long)sfx.first_at[step], (unsigned long long)af[step]);
    }
    if (fails) {
        fprintf(stderr, "replay audio test: FAIL (%d)\n", fails);
        return 1;
    }
    printf("replay audio test: PASS (%d steps: every stream field at its row's af, every SFX field within "
           "one frame of it, the stream ring empty after every pull)\n", STEPS);
    return 0;
}
