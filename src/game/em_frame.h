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
 * em_frame_pad_block() exposes them (original layout) with the analog
 * bytes and the gait byte. The native released field below is useful host
 * state, not a claimed binary overlay of one of those original fields.
 *
 * 001B5940 (port 0, analog read) rewrites the pad before any consumer:
 * with the left stick inside the gait-0 ring a held D-pad becomes stick
 * bytes with gait 3; with the stick outside it the D-pad bits are
 * replaced by stick-derived bits (bytes < 0x10 / >= 0xE1) and lx/ly are
 * quantized. So held/pressed below can carry stick-made D-pad bits.
 */
#ifndef EM_FRAME_H
#define EM_FRAME_H

#include <stdint.h>

#include "em_gfx.h"
#include "em_input.h"
#include "em_platform.h"
#include "game/em_fade.h"
#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Analog sticks as 0x80-centered bytes (0x00 = left/up, 0xFF =
     * right/down) after 001B5940: 0x00810E64..67. */
    uint8_t  lx, ly, rx, ry;
    /* Canonical (byte-swapped) views of the original block. */
    uint16_t held;      /* 0x810E70 processed held mask */
    uint16_t pressed;   /* 0x810E74 held & ~previous held */
    uint16_t released;  /* previous held & ~held (native convenience) */
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

/* The same step-C result in the original layout: halfwords 0x00810E70..7A
 * (including the repeat word 0x00810E78), analog 0x00810E64..67 and the
 * 001B5CC0 gait byte 0x00810E57. */
const EmPadUnpack *em_frame_pad_block(void);

/* The one translation of step C's result into the scene coordinator's
 * canonical input bytes (design 3.2, S11a), in the ORIGINAL layout:
 *   D_00810E74 (pressed edge) and D_00810E70 (held) are the halfwords
 *   001B5940 stores, i.e. em_frame_pad_block()->pressed / ->held, unswapped;
 *   D_00810E50 is byte +0x10 of the pad record D_00810E40 (001B57E0 passes
 *   it to 001B5F40), which 001B5F40 sets to 4 once the pad is initialised
 *   (sb at 0x1B604C); it is 4 in every original capture (design 10.2 Q8).
 *   The native pad is always that connected, initialised analog DualShock
 *   (see frame_input_read), so it is always 4, and 001B57E0's read-failure
 *   clear (0x1B5804..0x1B5844) is unreachable.
 * The slot-0 scene task calls it at the start of every tick, after this
 * frame's step C and before any original code reads the words; no other
 * module writes them. */
void em_frame_scene_input(EmSceneState *scene);

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

/* Main-loop step F, 001FCA10 (the shared message service), right after the
 * task dispatch (step E): since WP-8 the live message service
 * (em_message_live.h) is installed here at bring-up. tick returns -1 on a
 * fault (the frame quits); render draws the frame's message glyphs under
 * the transition. NULL clears. */
typedef struct {
    int (*tick)(void *context);
    void (*render)(void *context, EmGfx *gfx);
    void *context;
} EmFrameMessageService;
void em_frame_set_message_service(const EmFrameMessageService *service);
/* Main-loop step I (0x1AAF6C), 001B5B70, the rumble countdown: it runs every
 * frame, after the full-screen transition (step G) and not gated
 * (ORIGINAL_FRAME_ORDER.md section 1). The game installs the pad actuator's
 * service (em_pad_actuator_step_i); -1 is a fault (the frame quits). NULL
 * uninstalls it. */
void em_frame_set_step_i(int (*service)(void *context), void *context);

/* Main-loop step B (0x1AAF34), 001D1AE0(D_00810E80): the frame buffer
 * set-up of the render context (em_rcl_001D1AE0). It runs at the top of every
 * main iteration, not while the blocking movie holds the iteration; `index`
 * is the signed halfword D_00810E80. -1 is a fault (the frame quits). NULL
 * uninstalls it. */
void em_frame_set_step_b(int (*service)(void *context, int32_t index), void *context);
/* The bytes of D_00810E80 (a halfword, 0 or 1; step W flips it), for the
 * render context's view. */
uint8_t *em_frame_d810E80(void);

/* The sound service: `field` runs at the top of every em_frame_step (one
 * NTSC field: the vblank handler's D_00810E90 and the IOP's field work),
 * `step_h` at main-loop step H (0x1AAF64, 001FB100's lane service 001F9CF0),
 * after the transition (G) and before step I, skipped while D_00821058 == 1
 * (em_frame_movie_active). The game installs em_stream_live's; -1 is a
 * fault (the frame quits). NULL uninstalls it. */
typedef struct {
    int (*field)(void *context);
    int (*step_h)(void *context);
    void *context;
} EmFrameSoundService;
void em_frame_set_sound_service(const EmFrameSoundService *service);

void em_frame_set_movie_active(int active);
/* The D_00821058 == 1 mirror: the frame in which 00203350 plays (the
 * original then runs 001D1C10, which sets render flag 4). */
int em_frame_movie_active(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_FRAME_H */
