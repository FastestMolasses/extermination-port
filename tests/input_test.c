/* input_test.c — standalone unit test for the OS-free input model
 * (src/em_input.c). Plain C, no frameworks: synthesize EmEvent key
 * sequences, assert the resulting pad state. Build + run via
 * `make test-input` (links only em_input.c — no platform/gfx/audio).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "em_input.h"

static void key(EmEventType type, int k)
{
    EmEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.key  = k;
    em_input_handle_event(&ev);
}

static void press(int k)   { key(EM_EVENT_KEY_DOWN, k); }
static void release(int k) { key(EM_EVENT_KEY_UP,   k); }

static EmPadState pad(void)
{
    EmPadState p;
    em_input_pad(&p);
    return p;
}

/* Engine-side gait of a stick deflection: replicate the frame layer's
 * raw-byte conversion (em_frame.c stick_byte: raw = 0x80 + (int)(axis*128),
 * clamped to 0..255) and the engine quantizer func_001B5CC0
 * (r = sqrt((raw_x-128)^2 + (raw_y-128)^2) through rings 48/88/122), in
 * exact integer math (r^2 vs ring^2). 0 = dead, 1 = WALK, 2 = JOG,
 * 3 = RUN (the tier-ramp-corrected labels, em_input.h GAIT HOLD
 * TIERS). The contract is end-to-end: each hold's capped deflection
 * must quantize to ITS gait ring (Cmd -> 2, Option -> 1) in EVERY
 * stick direction. */
static int gait_of(float x, float y)
{
    int ox = (int)(x * 128.0f), oy = (int)(y * 128.0f);
    if (ox >  127) ox =  127;
    if (ox < -128) ox = -128;
    if (oy >  127) oy =  127;
    if (oy < -128) oy = -128;
    int r2 = ox * ox + oy * oy;
    if (r2 <= 48 * 48)   return 0;
    if (r2 <= 88 * 88)   return 1;
    if (r2 <= 122 * 122) return 2;
    return 3;
}

int main(void)
{
    EmPadState p;

    /* Initial state: nothing pressed, sticks centered. */
    em_input_init();
    p = pad();
    assert(p.buttons == 0);
    assert(p.lx == 0.0f && p.ly == 0.0f && p.rx == 0.0f && p.ry == 0.0f);

    /* Canonical DualShock 2 bit positions. */
    assert(EM_PAD_SELECT   == 0x0001);
    assert(EM_PAD_L3       == 0x0002);
    assert(EM_PAD_R3       == 0x0004);
    assert(EM_PAD_START    == 0x0008);
    assert(EM_PAD_UP       == 0x0010);
    assert(EM_PAD_RIGHT    == 0x0020);
    assert(EM_PAD_DOWN     == 0x0040);
    assert(EM_PAD_LEFT     == 0x0080);
    assert(EM_PAD_L2       == 0x0100);
    assert(EM_PAD_R2       == 0x0200);
    assert(EM_PAD_L1       == 0x0400);
    assert(EM_PAD_R1       == 0x0800);
    assert(EM_PAD_TRIANGLE == 0x1000);
    assert(EM_PAD_CIRCLE   == 0x2000);
    assert(EM_PAD_CROSS    == 0x4000);
    assert(EM_PAD_SQUARE   == 0x8000);

    /* Every digital-button key maps to its bit, and releases cleanly.
     * This is the user's PCSX2 binding, verbatim (em_input.h). */
    struct { int k; uint16_t bit; } map[] = {
        { 'i',              EM_PAD_TRIANGLE },
        { 'j',              EM_PAD_SQUARE   },
        { 'l',              EM_PAD_CIRCLE   },
        { 'k',              EM_PAD_CROSS    },
        { 'q',              EM_PAD_L1       },
        { '1',              EM_PAD_L2       },
        { '2',              EM_PAD_L3       },
        { 'e',              EM_PAD_R1       },
        { '3',              EM_PAD_R2       },
        { '4',              EM_PAD_R3       },
        { EM_KEY_BACKSPACE, EM_PAD_SELECT   },
        { EM_KEY_RETURN,    EM_PAD_START    },
        { EM_KEY_UP,        EM_PAD_UP       },
        { EM_KEY_RIGHT,     EM_PAD_RIGHT    },
        { EM_KEY_DOWN,      EM_PAD_DOWN     },
        { EM_KEY_LEFT,      EM_PAD_LEFT     },
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        press(map[i].k);
        assert(pad().buttons == map[i].bit);
        release(map[i].k);
        assert(pad().buttons == 0);
    }

    /* The PREVIOUS map's now-retired button keys are unmapped: U/O (old
     * L2/R2), R (old L3) and Tab (old SELECT) must not touch the pad. */
    press('u'); press('o'); press('r'); press(EM_KEY_TAB);
    p = pad();
    assert(p.buttons == 0 && p.lx == 0.0f && p.ly == 0.0f &&
           p.rx == 0.0f && p.ry == 0.0f);
    release('u'); release('o'); release('r'); release(EM_KEY_TAB);

    /* Chords accumulate and release independently. */
    press('k');
    press(EM_KEY_RETURN);
    press('q');
    assert(pad().buttons == (EM_PAD_CROSS | EM_PAD_START | EM_PAD_L1));
    release('k');
    assert(pad().buttons == (EM_PAD_START | EM_PAD_L1));
    release(EM_KEY_RETURN);
    release('q');
    assert(pad().buttons == 0);

    /* Left stick: WASD digital full deflection, -1 = left/up. */
    press('w');
    p = pad();
    assert(p.lx == 0.0f && p.ly == -1.0f);
    press('d');
    p = pad();
    assert(p.lx == 1.0f && p.ly == -1.0f);
    release('w');
    press('s');
    p = pad();
    assert(p.lx == 1.0f && p.ly == 1.0f);
    press('a');                       /* opposing keys cancel */
    p = pad();
    assert(p.lx == 0.0f && p.ly == 1.0f);
    release('a'); release('s'); release('d');
    p = pad();
    assert(p.lx == 0.0f && p.ly == 0.0f);

    /* Right stick: TFGH, same digital convention, independent of left. */
    press('t');
    p = pad();
    assert(p.rx == 0.0f && p.ry == -1.0f);
    press('h');
    p = pad();
    assert(p.rx == 1.0f && p.ry == -1.0f);
    release('t');
    press('g');
    p = pad();
    assert(p.rx == 1.0f && p.ry == 1.0f);
    press('f');                       /* opposing keys cancel */
    p = pad();
    assert(p.rx == 0.0f && p.ry == 1.0f);
    assert(p.lx == 0.0f && p.ly == 0.0f);   /* left stick untouched */
    release('f'); release('g'); release('h');
    p = pad();
    assert(p.rx == 0.0f && p.ry == 0.0f);

    /* GAIT HOLD TIERS (em_input.h): no modifier = FULL (gait 3 RUN,
     * anim id 3 at 48 u/s — the PCSX2 full-stick behavior), Command =
     * the JOG band (0.8, gait 2 — id 2 at 18 u/s, the anim the old
     * default misplayed at full stick), Option = the WALK band (0.5,
     * gait 1 — id 1 at 6 u/s, the engine's slowest sustained
     * movement tier; tier-ramp corrected 2026-06-11). Caps bound
     * BOTH sticks' VECTOR magnitude while held — including when pressed
     * mid-hold — and the full RUN deflection returns on release.
     * Cardinal pushes sit at the cap exactly; diagonals normalize by
     * 1/sqrt(2) so the engine-side quantized magnitude stays in the
     * held ring (gait_of above). Buttons are unaffected. */
    assert(EM_INPUT_DEFLECT_FULL == 1.0f);
    assert(EM_INPUT_DEFLECT_JOG  == 0.8f);
    assert(EM_INPUT_DEFLECT_WALK == 0.5f);
    press('w');
    assert(pad().ly == -EM_INPUT_DEFLECT_FULL);
    assert(gait_of(pad().lx, pad().ly) == 3);     /* full push = RUN */

    /* COMMAND tier: the JOG band. */
    press(EM_KEY_CMD);                /* modifier arrives mid-hold */
    p = pad();
    assert(p.ly == -EM_INPUT_DEFLECT_JOG && p.lx == 0.0f);
    assert(gait_of(p.lx, p.ly) == 2);             /* cardinal = JOG */
    press('d'); press('t');
    p = pad();
    assert(p.lx > 0.0f && p.ly < 0.0f);           /* W+D diagonal... */
    assert(p.lx == -p.ly);                        /* ...normalized evenly */
    assert(gait_of(p.lx, p.ly) == 2);             /* diagonal stays JOG */
    assert(p.ry == -EM_INPUT_DEFLECT_JOG);   /* right stick capped too */
    assert(gait_of(p.rx, p.ry) == 2);
    assert(p.buttons == 0);                  /* Cmd is not a button */

    /* OPTION tier: the WALK band — and it WINS over a held Cmd. */
    press(EM_KEY_ALT);
    p = pad();
    assert(p.lx == -p.ly);                        /* diagonal normalized */
    assert(gait_of(p.lx, p.ly) == 1);             /* diagonal = WALK */
    assert(p.ry == -EM_INPUT_DEFLECT_WALK);       /* cardinal at the cap */
    assert(gait_of(p.rx, p.ry) == 1);
    release(EM_KEY_CMD);                          /* Alt alone: same tier */
    p = pad();
    assert(gait_of(p.lx, p.ly) == 1);
    assert(p.buttons == 0);                  /* Alt is not a button */
    release(EM_KEY_ALT);
    p = pad();
    assert(p.lx ==  EM_INPUT_DEFLECT_FULL);  /* no normalization unheld */
    assert(p.ly == -EM_INPUT_DEFLECT_FULL);
    assert(p.ry == -EM_INPUT_DEFLECT_FULL);
    assert(gait_of(p.lx, p.ly) == 3);             /* full diagonal = RUN */
    press(EM_KEY_CMD);                /* re-engage; buttons still clean */
    press('k');
    p = pad();
    assert(p.buttons == EM_PAD_CROSS);
    assert(gait_of(p.lx, p.ly) == 2);  /* W+D diagonal back in JOG */
    release('k'); release(EM_KEY_CMD);
    release('w'); release('d'); release('t');
    p = pad();
    assert(p.buttons == 0 && p.lx == 0.0f && p.ly == 0.0f && p.ry == 0.0f);

    /* Stick keys never touch the button mask; idle sticks stay centered. */
    press('w'); press('a');
    p = pad();
    assert(p.buttons == 0);
    assert(p.rx == 0.0f && p.ry == 0.0f);
    release('w'); release('a');

    /* Unmapped keys, stray releases, and non-key events are ignored. */
    press('z');
    release('k');                     /* not held — harmless */
    press(EM_KEY_ESCAPE);
    press(EM_KEY_SPACE);
    {
        EmEvent ev;
        memset(&ev, 0, sizeof ev);
        ev.type  = EM_EVENT_RESIZE;
        ev.width = 640; ev.height = 480;
        em_input_handle_event(&ev);
        ev.type = EM_EVENT_QUIT;
        em_input_handle_event(&ev);
        em_input_handle_event(NULL);
    }
    p = pad();
    assert(p.buttons == 0 && p.lx == 0.0f && p.ly == 0.0f);
    release('z'); release(EM_KEY_ESCAPE); release(EM_KEY_SPACE);

    /* em_input_init resets held state — including both gait holds. */
    press('k'); press('w'); press('t'); press(EM_KEY_ALT); press(EM_KEY_CMD);
    em_input_init();
    p = pad();
    assert(p.buttons == 0 && p.lx == 0.0f && p.ly == 0.0f && p.ry == 0.0f);
    press('w');
    assert(pad().ly == -EM_INPUT_DEFLECT_FULL);   /* both flags cleared */
    release('w');

    /* Button-name helper covers the canonical order. */
    assert(strcmp(em_pad_button_name(0),  "SELECT")   == 0);
    assert(strcmp(em_pad_button_name(3),  "START")    == 0);
    assert(strcmp(em_pad_button_name(12), "TRIANGLE") == 0);
    assert(strcmp(em_pad_button_name(14), "CROSS")    == 0);
    assert(strcmp(em_pad_button_name(15), "SQUARE")   == 0);
    assert(strcmp(em_pad_button_name(16), "?")        == 0);
    assert(strcmp(em_pad_button_name(-1), "?")        == 0);

    printf("PASS\n");
    return 0;
}
