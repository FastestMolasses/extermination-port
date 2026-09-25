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
 * table's default duration byte. frames = 0 stops immediately. frames < 0
 * holds the levels until the next call: the libpad actuator write 00111018
 * (em_pad_actuator), whose duration is the game's own countdown 001B5B70.
 *
 * Safe to call with no pad attached; the request is recorded either way so
 * callers never have to branch on presence. */
void em_gamepad_rumble(float big, float small, int frames);

/* Read back the current rumble request — what the game last asked for, not
 * what any device is doing. Lets the rumble call sites be finished and tested
 * before a haptics mapping is chosen. Any pointer may be NULL. */
void em_gamepad_rumble_state(float *big, float *small, int *frames);

/* --- RUMBLE EFFECT TABLE (engine D_0024D6F0) -------------------------
 *
 * Read directly out of the boot ELF, 4-byte stride {big, small, defaultDur}.
 * func_001B1E20(id, dur) indexes this table and fires
 * func_001B61C0(big, small, dur ? dur : defaultDur, 1); a NEGATIVE dur stops
 * instead. Motor bytes are 0..255, durations are FRAMES.
 *
 * Note the shape: almost every effect drives the SMALL motor only. The big
 * motor appears at level 1 in just two entries (4 and 6) — this pad model
 * treats "big" as essentially on/off, so a port mapping that scales both
 * motors linearly will feel wrong.
 *
 * Entries 12/13/14/15 have BOTH motors zero with long durations (240/160/
 * 240/22) — they are silent timers, presumably used to hold a rumble slot
 * busy rather than to vibrate. Do not "fix" them to non-zero.
 *
 * Call-site mapping is NOT done: the ids are known but which game event fires
 * each one is only partly traced (effect 6 belongs to the scope camera
 * func_0022EEF0, which this port has not implemented yet). Fill these in as
 * the owning features land. */
typedef struct { unsigned char big, small, dur; } EmRumbleEffect;

static const EmRumbleEffect kEmRumbleTable[16] = {
    {   0,  80,  60 }, {   0, 112,  60 }, {   0, 187,  60 }, {   0,  72,  60 },
    {   1, 255,  60 }, {   0,  96,  60 }, {   1,   0,  20 }, {   0, 144,   5 },
    {   0, 204,   5 }, {   0,  88,  15 }, {   0,  80,   8 }, {   0,   0,   0 },
    {   0,   0, 240 }, {   0,   0, 160 }, {   0,   0, 240 }, {   0,   0,  22 },
};

/* Fire effect `id` from the table above. dur = 0 uses the table's default,
 * dur < 0 stops. Mirrors func_001B1E20's dispatch exactly. */
static inline void em_gamepad_rumble_effect(int id, int dur)
{
    if (id < 0 || id >= 16) return;
    if (dur < 0) { em_gamepad_rumble(0.0f, 0.0f, 0); return; }
    const EmRumbleEffect *e = &kEmRumbleTable[id];
    em_gamepad_rumble(e->big / 255.0f, e->small / 255.0f,
                      dur ? dur : e->dur);
}

#ifdef __cplusplus
}
#endif
#endif /* EM_GAMEPAD_H */
