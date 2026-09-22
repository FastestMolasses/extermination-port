/* Frame loop and native input/presentation contract.
 *
 * Original 001AAE40 drives separate letterbox and full-screen transition
 * machines around task dispatch; em_fade.h provides their exact state
 * transitions. em_frame_step exposes one presentation step for tests and
 * for the interactive loop, without sleeping.
 *
 * INPUT BOUNDARY: native EM_PAD masks use canonical DualShock 2 bit
 * positions. Original 001B5940 swaps the button word's bytes, so original
 * Start 0x0800 is native EM_PAD_START 0x0008, for example. Always use
 * EM_PAD names in native consumers; swap bytes when comparing original
 * traces. The original six halfwords at 0x00810E70 are held, previous
 * held, pressed, previous pressed, directional repeat, repeat countdown.
 * The native released field below is useful host state, not a claimed
 * binary overlay of one of those original fields.
 */
#ifndef EM_FRAME_H
#define EM_FRAME_H

#include <stdint.h>

#include "em_gfx.h"
#include "em_platform.h"
#include "game/em_fade.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Analog sticks as raw 0x80-centered bytes (0x00 = left/up, 0xFF =
     * right/down), the engine's representation: FINDINGS pins lx/ly at
     * block +4/+5 (0x00810E64/65, forced to 0x80 on pad failure). */
    uint8_t  lx, ly, rx, ry;
    /* Native convenience fields, not the original memory layout. */
    uint16_t held;      /* buttons currently down */
    uint16_t pressed;   /* went down this frame (original +4, byte-swapped) */
    uint16_t released;  /* went up this frame (native convenience) */
} EmFrameInput;

/* Bind the loop to the platform window + gfx device and reset the task
 * table, the input model, and the frame counters. Call once, after
 * em_window_create/em_gfx_create and before registering any task. */
void em_frame_init(EmWindow *win, EmGfx *gfx);

/* Run frames (steps A..W) until quit is requested — by the window (close /
 * ESC) or by em_frame_request_quit(). The PS2 loop never returns; natively
 * this returns so main can tear down cleanly. */
void em_frame_run(void);

/* Execute one presentation step without pacing. Returns zero on quit.
 * A blocked movie may need many presentation steps per engine frame. */
int em_frame_step(void);

/* Ask the loop to stop after the current frame completes. */
void em_frame_request_quit(void);

/* The current frame's input block (updated at step C each frame). */
const EmFrameInput *em_frame_input(void);

/* Loop environment accessors for game code (the engine reaches its
 * equivalents through globals). */
EmWindow *em_frame_window(void);
EmGfx    *em_frame_gfx(void);

/* Lifetime frame counter — the scratchpad 0x70003B64 main-loop counter. */
uint32_t em_frame_counter(void);

/* Frame parity (step W's `frame_idx ^= 1`, halfword 0x00810E80) — selects
 * the engine's double-buffered per-frame resources. */
uint32_t em_frame_parity(void);

/* Full-screen transition: 001AED80/001AEDB0 force clear/full;
 * 001AEDE0/001AEE10 arm from the existing level. Colour 0 subtracts to
 * black; nonzero adds to white. The frame loop owns ticking AND drawing.
 * Level is an integer 0..255 and normalization is level/255, not /256.
 * Fade-in settles only after crossing below zero, exactly as the original.
 */
#define EM_FADE_SPEED_DOOR 4
void em_frame_fade_start(int dir, int speed); /* black convenience */
void em_frame_fade_start_colour(int dir, int speed, uint8_t colour);
void em_frame_fade_clear(uint8_t colour);
void em_frame_fade_full(uint8_t colour);
/* Original001AEE40 state4 flash, followed by its own fade lifecycle. */
void em_frame_fade_flash(int speed);
const EmTransitionFade *em_frame_transition(void);
float em_frame_fade_level(void);
int em_frame_fade_active(void); /* substate 1 or 3 */

/* Separate 001AEBE0 letterbox-bar effect, before task dispatch. The
 * original two rectangles cover 32/224 field lines at top and bottom.
 * Positive dir calls 001AEB60, nonpositive calls 001AEBA0. Gate mirrors
 * 0x70003B90 and 0x008106C4; it suppresses drawing, never state updates. */
void em_frame_screen_fade_start(int dir, int speed);
void em_frame_screen_fade_gate(uint8_t request, uint8_t suppress);
const EmScreenFade *em_frame_screen_fade(void);

/* Native continuation of original blocking 00203350. Arm movie_active
 * during task dispatch, and install a pump that presents one movie frame
 * and returns 1 while playing or 0 once complete. While suspended only
 * input, the pump and presentation run: no normal tasks, fades, BGM
 * service, frame counter or parity advancement. Completion performs the
 * original one extra transition tick and completes the main iteration.
 * The callback can read em_frame_input() to honor movie skip requests.
 */
typedef int (*EmFrameMoviePump)(void *user);
void em_frame_set_movie_pump(EmFrameMoviePump pump, void *user);
void em_frame_set_movie_active(int active);

#ifdef __cplusplus
}
#endif

#endif /* EM_FRAME_H */
