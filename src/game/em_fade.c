#include "game/em_fade.h"

#include <string.h>

/* Preserve the original short-store wrap before its signed comparison,
 * without depending on a host's out-of-range signed conversion. */
static int16_t narrow_short(int value)
{
    unsigned bits = (unsigned)value & 0xffffu;
    return (int16_t)(bits < 0x8000u ? (int)bits : (int)bits - 0x10000);
}

void em_transition_fade_init(EmTransitionFade *fade)
{
    memset(fade, 0, sizeof *fade);
}

void em_transition_fade_clear(EmTransitionFade *fade, uint8_t colour)
{
    fade->substate = 0;
    fade->mode = 0;
    fade->colour = colour;
    fade->level = 0;
}

void em_transition_fade_full(EmTransitionFade *fade, uint8_t colour)
{
    fade->substate = 2;
    fade->mode = 1;
    fade->colour = colour;
    fade->level = 255;
}

void em_transition_fade_out(EmTransitionFade *fade, int16_t step,
                            uint8_t colour)
{
    fade->substate = 3;
    fade->step = step;
    fade->mode = 3;
    fade->colour = colour;
}

void em_transition_fade_in(EmTransitionFade *fade, int16_t step,
                           uint8_t colour)
{
    fade->substate = 1;
    fade->step = step;
    fade->mode = 2;
    fade->colour = colour;
}

void em_transition_fade_flash(EmTransitionFade *fade, int16_t step)
{
    fade->substate = 1;
    fade->step = step;
    fade->mode = 4;
    fade->colour = 0;
}

int em_transition_fade_tick(EmTransitionFade *fade)
{
    int mode = fade->mode;
    switch (mode) {
    case 0:
        if (fade->substate == 2)
            em_transition_fade_full(fade, EM_FADE_BLACK);
        else if (fade->substate == 3)
            em_transition_fade_out(fade, 4, EM_FADE_BLACK);
        else if (fade->substate != 0) {
            fade->substate = 0;
            fade->level = 0;
        }
        break;
    case 1:
        if (fade->substate == 0)
            em_transition_fade_clear(fade, EM_FADE_BLACK);
        else if (fade->substate == 1)
            em_transition_fade_in(fade, 4, EM_FADE_BLACK);
        else {
            fade->substate = 2;
            fade->level = 255;
        }
        break;
    case 2:
        if (fade->substate == 2) {
            fade->mode = 3;
        } else {
            fade->substate = 1;
            fade->level = narrow_short(fade->level - fade->step);
            if (fade->level < 0) {
                fade->level = 0;
                fade->mode = 0;
                fade->substate = 0;
            }
        }
        break;
    case 3:
        if (fade->substate == 0) {
            fade->mode = 2;
        } else {
            fade->substate = 3;
            fade->level = narrow_short(fade->level + fade->step);
            if (fade->level >= 255) {
                fade->level = 255;
                fade->mode = 1;
                fade->substate = 2;
            }
        }
        break;
    case 4:
    case 5:
        ++fade->mode;
        break;
    case 6:
        fade->mode = 2;
        break;
    }
    return mode != 0;
}

void em_screen_fade_init(EmScreenFade *fade)
{
    memset(fade, 0, sizeof *fade);
}

void em_screen_fade_out(EmScreenFade *fade, int16_t step)
{
    if (fade->state == 1) return;
    fade->state = 3;
    fade->step = step;
    fade->level = 0;
}

void em_screen_fade_in(EmScreenFade *fade, int16_t step)
{
    if (fade->state == 0) return;
    fade->state = 2;
    fade->step = step;
    fade->level = 255;
}

int em_screen_fade_tick(EmScreenFade *fade, uint8_t request,
                        uint8_t suppress_when_requested)
{
    int state = fade->state;
    switch (state) {
    case 2:
        fade->level -= fade->step;
        if (fade->level < 0) {
            fade->level = 0;
            fade->state = 0;
        }
        break;
    case 3:
        fade->level += fade->step;
        if (fade->level >= 255) {
            fade->level = 255;
            fade->state = 1;
        }
        break;
    }
    return state != 0 && (request != 2 || suppress_when_requested == 0);
}
