/* main.c — entry point for the Extermination native port shell.
 *
 * Cross-platform: talks only to em_platform.h + em_gfx.h. No OS or GPU API
 * appears here. This is the bootstrap loop — open a window, clear the screen
 * to an animated colour, present, handle quit/Esc — that proves the
 * clean-room platform + graphics layers work end to end on each target. The
 * game logic (decompiled C) and the translated renderer plug in on top of
 * this later.
 */
#include "em_platform.h"
#include "em_gfx.h"

#include <math.h>
#include <stdio.h>

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

    printf("Extermination native port shell: window + renderer up. "
           "Close the window or press Esc to quit.\n");

    bool running = true;
    double t = 0.0;
    while (running) {
        EmEvent ev;
        while (em_window_poll(win, &ev)) {
            switch (ev.type) {
                case EM_EVENT_QUIT:
                    running = false;
                    break;
                case EM_EVENT_KEY_DOWN:
                    if (ev.key == EM_KEY_ESCAPE) running = false;
                    break;
                default:
                    break;
            }
        }

        /* animated clear colour so it's visibly alive */
        float r = 0.15f + 0.15f * (float)sin(t);
        float g = 0.15f + 0.15f * (float)sin(t + 2.0944);  /* +120 deg */
        float b = 0.20f + 0.20f * (float)sin(t + 4.1888);  /* +240 deg */
        em_gfx_begin_frame(gfx, r, g, b, 1.0f);
        em_gfx_end_frame(gfx);

        t += 0.02;
    }

    em_gfx_destroy(gfx);
    em_window_destroy(win);
    return 0;
}
