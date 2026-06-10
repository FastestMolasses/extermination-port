/* em_frame.h — the engine's per-frame phase sequence, translated to native C.
 *
 * Faithful structural translation of the PS2 main loop func_001AAE40
 * (decomp repo, docs/FINDINGS.md "ENGINE FRAME ANATOMY", steps A..W).
 * em_frame_run() executes the same phases in the same order, with the
 * port's em_* subsystems standing in for the PS2 hardware; steps that only
 * exist because of PS2 hardware are documented no-ops (full table in
 * em_frame.c).
 *
 * FRAME INPUT BLOCK — mirrors the engine block at 0x00810E60..0x00810E7B
 * that func_001B57E0 -> func_001B5F40 unpacks from the raw libpad RPC
 * buffer each frame (step C). Natively the OS event pump is the "RPC
 * buffer" and em_input is the unpacker; the result is the same shape:
 * analog bytes (0x80-centered) + button halfwords as current/pressed/
 * released triples. Button bits are the EM_PAD_* mask (canonical
 * DualShock 2 order, active-high — see em_input.h).
 */
#ifndef EM_FRAME_H
#define EM_FRAME_H

#include <stdint.h>

#include "em_gfx.h"
#include "em_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Analog sticks as raw 0x80-centered bytes (0x00 = left/up, 0xFF =
     * right/down), the engine's representation: FINDINGS pins lx/ly at
     * block +4/+5 (0x00810E64/65, forced to 0x80 on pad failure). */
    uint8_t  lx, ly, rx, ry;
    /* Button halfword triple (block +0x10/+0x12/+0x14 = 0x00810E70/72/74).
     * The original block carries a second triple at +0x16..+0x1A
     * (0x810E76/78/7A); the single-pad port carries one. */
    uint16_t held;      /* buttons currently down */
    uint16_t pressed;   /* went down this frame  (edge, computed step C/I) */
    uint16_t released;  /* went up this frame    (edge, computed step C/I) */
} EmFrameInput;

/* Bind the loop to the platform window + gfx device and reset the task
 * table, the input model, and the frame counters. Call once, after
 * em_window_create/em_gfx_create and before registering any task. */
void em_frame_init(EmWindow *win, EmGfx *gfx);

/* Run frames (steps A..W) until quit is requested — by the window (close /
 * ESC) or by em_frame_request_quit(). The PS2 loop never returns; natively
 * this returns so main can tear down cleanly. */
void em_frame_run(void);

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

/* --- SCREEN-FADE MACHINE (step D, func_001AEDE0) ---------------------
 * The engine's full-screen fade: func_001AEDE0(speed, dir) arms it and
 * the main loop's step D ticks it once per frame. The captured speed is
 * 4 -> a 64-frame ramp (level steps speed/256 per frame), used by BOTH
 * door-transition fades (FINDINGS.md "AREA TRANSITION LIFECYCLE": commit
 * runs func_001AEDE0(4,0) fade-out; fade-in after the re-place is the
 * same 4-speed machine). Natively the level is drawn as one full-screen
 * black overlay rect at close-out (em_game.c), alpha = level. */
#define EM_FADE_SPEED_DOOR 4   /* the captured door-transit fade speed */

/* Arm a fade: dir > 0 fades OUT (toward black), dir < 0 fades IN (toward
 * clear); `speed` in engine units (level moves speed/256 per frame, so
 * speed 4 = 64 frames full ramp). */
void em_frame_fade_start(int dir, int speed);

/* Current fade level: 0.0 = clear, 1.0 = full black. */
float em_frame_fade_level(void);

/* Nonzero while a ramp is still in motion (level not yet at its end). */
int em_frame_fade_active(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_FRAME_H */
