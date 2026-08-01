/* em_gamepad.h — optional real-gamepad source and force-feedback sink.
 *
 * The keyboard map in em_input.c remains the always-available source. This is
 * an OVERLAY: when a pad is attached its state replaces the keyboard's for
 * that frame, and when it is unplugged the keyboard takes over again with no
 * state carried across.
 *
 * Why it matters beyond convenience — three decoded engine behaviours are
 * unusable without a pad:
 *   * force feedback (func_001B1E20, byte-matched: a 4-byte-stride effect
 *     table into {big motor, small motor, default duration}, negative
 *     duration = stop),
 *   * pressure-sensitive buttons (the DualShock 2 reports 8 bits per button;
 *     the engine reads them for the aim-stance bands),
 *   * the analog gait rings (raw 48 / 88 / 122 of 127 — em_input.h "GAIT HOLD
 *     TIERS"), which a keyboard can only approximate with three fixed steps.
 *
 * Backends live under src/platform/<os>/ and use that platform's own system
 * framework (macOS: GameController.framework). No third-party libraries.
 */
#ifndef EM_GAMEPAD_H
#define EM_GAMEPAD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Sample the pad and push its state into em_input. Call once per frame,
 * BEFORE the frame reads em_input_pad(). A no-op on platforms without a
 * backend, and safe with no pad attached. */
void em_gamepad_poll(void);

/* Non-zero while a pad is attached and driving input. */
int  em_gamepad_present(void);

/* Force feedback. `big`/`small` are the two motor levels in 0..1 (the engine's
 * table stores them as bytes); `frames` is the duration in FRAMES, matching
 * func_001B1E20's units — it passes either the caller's short or the effect
 * table's default duration byte. frames = 0 stops immediately.
 *
 * Safe to call with no pad attached; the request is recorded either way so
 * callers never have to branch on presence. */
void em_gamepad_rumble(float big, float small, int frames);

/* Read back the current rumble request — what the game last asked for, not
 * what any device is doing. Lets the rumble call sites be finished and tested
 * before a haptics mapping is chosen. Any pointer may be NULL. */
void em_gamepad_rumble_state(float *big, float *small, int *frames);

#ifdef __cplusplus
}
#endif
#endif /* EM_GAMEPAD_H */
