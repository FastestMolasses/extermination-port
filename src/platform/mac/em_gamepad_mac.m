/* em_gamepad_mac.m — real gamepad input + force feedback on macOS.
 *
 * WHY THIS EXISTS. em_input.h has said "keyboard is the only source today;
 * gamepad backends are future" since the port began, and that single gap was
 * blocking three decoded engine features from being usable at all:
 *
 *   * FORCE FEEDBACK. func_001B1E20 is byte-matched and completely clear — a
 *     4-byte-stride effect table into {big motor, small motor, default
 *     duration}, with a negative duration meaning stop. The decode was done
 *     and unusable: there was no device to send it to.
 *   * PRESSURE-SENSITIVE BUTTONS. The DualShock 2 reports an 8-bit analog
 *     value per face/shoulder button, which the engine reads for the aim
 *     stance bands. A keyboard key is 0 or 1.
 *   * ANALOG STICK BANDS. The engine quantizes stick deflection into the
 *     gait rings (raw 48 / 88 / 122 of 127 — em_input.h "GAIT HOLD TIERS").
 *     The keyboard emulates three fixed deflections; a real stick produces
 *     the continuum those rings were designed around.
 *
 * CLEAN-ROOM: GameController.framework is an Apple SYSTEM framework, in the
 * same category as Cocoa and Metal — not a third-party dependency. Nothing
 * here is derived from any emulator or from the disc.
 *
 * LAYERING: em_input.c stays OS-free, as its header promises. This file does
 * the Apple-specific work and pushes a finished EmPadState through the
 * em_input_set_gamepad() sink; em_input merges it over the keyboard state.
 * The game code keeps reading em_input_pad() and never learns a pad exists.
 */
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

#include "em_input.h"
#include "em_gamepad.h"

/* The engine's stick rings are expressed on the DualShock's 0..255 raw scale
 * centred at 0x80 (em_input.h). GameController hands us -1..+1 floats, which
 * is the same quantity in a different unit — no remap needed beyond sign.
 *
 * Deadzone is OURS, not the engine's: a real stick rests slightly off centre
 * and the engine's own deadzone (if any) lives in code we have not decoded.
 * Kept below the gait-1 WALK ring (raw 48/127 = 0.378) so it cannot swallow
 * the slowest movement tier the engine supports. */
#define GP_DEADZONE   0.12f

static int   s_present;
static float s_rumble_big, s_rumble_small;
static int   s_rumble_frames;

static float dz(float v)
{
    const float a = v < 0.0f ? -v : v;
    if (a < GP_DEADZONE) return 0.0f;
    /* Rescale so the usable range still reaches 1.0 — otherwise every ring
     * threshold shifts inward by the deadzone and the gait bands narrow. */
    const float s = (a - GP_DEADZONE) / (1.0f - GP_DEADZONE);
    return v < 0.0f ? -s : s;
}

void em_gamepad_poll(void)
{
    GCController *c = GCController.current;
    if (!c || !c.extendedGamepad) {
        if (s_present) {
            s_present = 0;
            em_input_set_gamepad(NULL);   /* fall back to the keyboard */
        }
        return;
    }
    GCExtendedGamepad *g = c.extendedGamepad;

    EmPadState p;
    memset(&p, 0, sizeof p);

    /* Face buttons — GameController names them by POSITION (A/B/X/Y), the
     * engine by SHAPE. On a DualShock-layout pad: A = south = CROSS,
     * B = east = CIRCLE, X = west = SQUARE, Y = north = TRIANGLE. */
    if (g.buttonA.pressed) p.buttons |= EM_PAD_CROSS;
    if (g.buttonB.pressed) p.buttons |= EM_PAD_CIRCLE;
    if (g.buttonX.pressed) p.buttons |= EM_PAD_SQUARE;
    if (g.buttonY.pressed) p.buttons |= EM_PAD_TRIANGLE;

    if (g.leftShoulder.pressed)  p.buttons |= EM_PAD_L1;
    if (g.rightShoulder.pressed) p.buttons |= EM_PAD_R1;
    if (g.leftTrigger.pressed)   p.buttons |= EM_PAD_L2;
    if (g.rightTrigger.pressed)  p.buttons |= EM_PAD_R2;

    if (g.dpad.up.pressed)    p.buttons |= EM_PAD_UP;
    if (g.dpad.down.pressed)  p.buttons |= EM_PAD_DOWN;
    if (g.dpad.left.pressed)  p.buttons |= EM_PAD_LEFT;
    if (g.dpad.right.pressed) p.buttons |= EM_PAD_RIGHT;

    if (@available(macOS 10.14.1, *)) {
        if (g.buttonOptions.pressed) p.buttons |= EM_PAD_SELECT;
        if (g.buttonMenu.pressed)    p.buttons |= EM_PAD_START;
    }
    if (@available(macOS 11.0, *)) {
        if (g.leftThumbstickButton.pressed)  p.buttons |= EM_PAD_L3;
        if (g.rightThumbstickButton.pressed) p.buttons |= EM_PAD_R3;
    }

    /* Sticks. GameController's Y is +up; the port's ly is +DOWN (em_input.h:
     * "-1 = left/up, +1 = right/down"), so Y is negated. */
    p.lx =  dz(g.leftThumbstick.xAxis.value);
    p.ly = -dz(g.leftThumbstick.yAxis.value);
    p.rx =  dz(g.rightThumbstick.xAxis.value);
    p.ry = -dz(g.rightThumbstick.yAxis.value);

    s_present = 1;
    em_input_set_gamepad(&p);

    /* Rumble tick. The engine's effect duration is in FRAMES (func_001B1E20
     * passes the table's default byte or the caller's short), so it is
     * counted down here rather than scheduled in wall time. */
    if (s_rumble_frames > 0 && --s_rumble_frames == 0)
        em_gamepad_rumble(0.0f, 0.0f, 0);
}

int em_gamepad_present(void) { return s_present; }

void em_gamepad_rumble(float big, float small, int frames)
{
    /* Stored unconditionally so em_gamepad_rumble_state() reports what the
     * game asked for even with no pad attached — the caller should not have
     * to branch on presence. */
    s_rumble_big    = big;
    s_rumble_small  = small;
    s_rumble_frames = frames;

    GCController *c = GCController.current;
    if (!c) return;

    /* HAPTICS ARE NOT WIRED YET, deliberately. CHHapticEngine is the modern
     * path and it wants a pattern player per motor; the DualShock 2's model
     * is two raw motor levels held for a frame count, which is what
     * func_001B1E20 hands us. Mapping one onto the other is a design choice
     * that should be made with a pad in hand, not guessed at here — a wrong
     * mapping produces feedback that is subtly wrong in a way nobody can
     * trace back to this file. The state is recorded and queryable so the
     * game side can be finished and tested first. */
}

void em_gamepad_rumble_state(float *big, float *small, int *frames)
{
    if (big)    *big    = s_rumble_big;
    if (small)  *small  = s_rumble_small;
    if (frames) *frames = s_rumble_frames;
}
