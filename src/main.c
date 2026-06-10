/* main.c — entry point for the Extermination native port.
 *
 * Cross-platform: talks only to the em_* contracts. No OS or GPU API, and
 * no game logic, appears here — exactly like the original's crt0, main()
 * only brings the machine up and enters the engine frame loop:
 *
 *   platform/gfx/audio/input init  ->  em_frame_init()
 *   register the boot task         ->  em_game_install()
 *   run frames forever (steps A..W) -> em_frame_run()
 *
 * The PS2 main (func_001AAE40) never returns; natively em_frame_run()
 * returns on quit so the teardown below can run.
 */
#include "em_audio.h"
#include "em_gfx.h"
#include "em_platform.h"
#include "game/em_frame.h"
#include "game/em_game.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

/* Audio smoke test (EM_AUDIO_TEST=1): synthesize a quiet 440 Hz sine from the
 * audio thread and count delivered frames. Per the em_audio.h contract the
 * callback runs on the OS audio thread, so the counter the main thread reads
 * at exit is atomic; the phase is touched only by the audio thread. */
#define AUDIO_TEST_RATE 48000

typedef struct {
    double      phase;   /* audio thread only */
    atomic_long frames;  /* written by audio thread, read at exit */
} AudioTest;

static void audio_test_cb(void *user, float *out, int frames)
{
    AudioTest *at = user;
    const double step =
        2.0 * 3.14159265358979323846 * 440.0 / (double)AUDIO_TEST_RATE;
    for (int i = 0; i < frames; i++) {
        float s = 0.1f * (float)sin(at->phase);
        at->phase += step;
        if (at->phase > 2.0 * 3.14159265358979323846)
            at->phase -= 2.0 * 3.14159265358979323846;
        out[i * 2 + 0] = s;
        out[i * 2 + 1] = s;
    }
    atomic_fetch_add_explicit(&at->frames, (long)frames,
                              memory_order_relaxed);
}

int main(void)
{
    EmWindow *win = em_window_create("Extermination (native port)", 960, 720);
    if (!win) {
        fprintf(stderr, "fatal: could not create window\n");
        return 1;
    }
    EmGfx *gfx = em_gfx_create(win);
    if (!gfx) {
        fprintf(stderr, "fatal: could not create graphics device\n");
        em_window_destroy(win);
        return 1;
    }

    /* Audio smoke test: EM_AUDIO_TEST=1 opens the output device with the
     * sine callback above. When unset, no audio object exists at all. */
    AudioTest audio_test = { 0.0, 0 };
    EmAudio  *audio = NULL;
    const char *audio_env = getenv("EM_AUDIO_TEST");
    if (audio_env && audio_env[0] == '1') {
        audio = em_audio_create(AUDIO_TEST_RATE, audio_test_cb, &audio_test);
        if (!audio)
            fprintf(stderr, "warning: EM_AUDIO_TEST set but audio device "
                            "creation failed\n");
    }

    /* Engine bring-up: frame loop env + input + task table, then the boot
     * task into slot 0 (the engine init's func_001AB740(0, boot)). */
    em_frame_init(win, gfx);
    em_game_install();

    em_frame_run();

    em_game_shutdown();
    if (audio) {
        em_audio_destroy(audio); /* blocks: no callback after this */
        printf("audio test: %ld frames delivered\n",
               atomic_load_explicit(&audio_test.frames,
                                    memory_order_relaxed));
    }
    em_gfx_destroy(gfx);
    em_window_destroy(win);
    return 0;
}
