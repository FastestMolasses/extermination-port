/* em_input.h — platform-neutral PS2 controller abstraction for the
 * Extermination native port.
 *
 * Clean-room and OS-free: the implementation (em_input.c) is plain C with no
 * platform calls, so the game-facing pad model is identical on every OS and
 * unit-testable without a window. The platform layer stays dumb — it emits
 * raw key events (em_platform.h); this module turns them into the DualShock 2
 * pad state the decompiled game code expects.
 *
 * BUTTON MASK: EmPadState.buttons uses the canonical DualShock 2 digital bit
 * order (bit 0 = SELECT .. bit 15 = SQUARE, exactly the layout the PS2 pad
 * library reports), so once the decompiled game code is dropped in it can
 * consume the mask directly with no translation table. Bits are ACTIVE-HIGH
 * (1 = pressed); the original hardware reports active-low, so a future shim
 * that feeds the decomp can simply invert.
 *
 * STICKS: float x/y in [-1, 1] per stick, matching the PS2 raw-byte
 * convention's directions: -1 = left/up, +1 = right/down, 0 = centered
 * (raw 0x00/0x80/0xFF map to -1/0/+1).
 *
 * DEFAULT KEYBOARD MAP (the only source today; gamepad backends are future
 * work):
 *   W/A/S/D     left stick up/left/down/right (digital: full ±1 deflection)
 *   Arrow keys  d-pad UP/LEFT/DOWN/RIGHT
 *   K / L       CROSS / CIRCLE
 *   I / J       TRIANGLE / SQUARE
 *   Q / E       L1 / R1
 *   U / O       L2 / R2
 *   R           L3 (stick click — the engine's reload button)
 *   Return      START
 *   Tab         SELECT
 *   (R3 and the right stick are unmapped for now.)
 *
 * ENGINE DEFAULT BUTTON CONFIG (live-pinned 2026-06-10 s29 — decomp repo
 * FINDINGS.md "GAMEPLAY SOUND IDS PINNED LIVE"). The engine reads actions
 * through a CONFIG-MASK BLOCK of u16 slots at scratchpad 0x70003B70..7E,
 * tested against the held-buttons word D_00810E74. That word is the DS2
 * digital halfword BYTE-SWAPPED, so the engine masks decode as:
 *
 *   0x0001 L2   0x0002 R2     0x0004 L1    0x0008 R1
 *   0x0010 TRI  0x0020 CIRCLE 0x0040 CROSS 0x0080 SQUARE
 *   0x0100 SEL  0x0200 L3     0x0400 R3    0x0800 START
 *   0x1000 UP   0x2000 RIGHT  0x4000 DOWN  0x8000 LEFT
 *
 * Default config block (slot -> engine mask -> button -> action):
 *   spad 0x3B70  0x0800  START   (action not pinned)
 *   spad 0x3B72  0x0800  START   (action not pinned)
 *   spad 0x3B74  0x0080  SQUARE  sub-weapon/melee-class action (sound
 *                                0x179; the action itself is unidentified)
 *   spad 0x3B76  0x0040  CROSS   USE / confirm (the use-scan gate
 *                                0x810E74 & spad3B76 — doors)
 *   spad 0x3B78  0x0020  CIRCLE  FIRE (the trigger — NOT Cross)
 *   spad 0x3B7A  0x0010  TRI     status screen
 *   spad 0x3B7C  0x0008  R1      weapon-draw hold
 *   spad 0x3B7E  0x0002  R2      (action not pinned)
 * RELOAD is NOT config-mapped: the weapon code tests the raw pad bit
 * 0x0200 = L3 directly (func_0017B300(.,2) top-up).
 *
 * The port consumes the canonical EM_PAD_* bits below (em_weapon fires on
 * EM_PAD_CIRCLE, doors use EM_PAD_CROSS, reload is EM_PAD_L3), so this
 * module never needs the swapped layout — it is recorded here as the
 * authoritative decode of the engine's default control scheme.
 *
 * FUTURE WORK (deliberately not in the struct yet): DualShock 2 buttons are
 * pressure-sensitive (8-bit analog value per button) and real gamepad
 * backends (GameController.framework / XInput / evdev) will replace this
 * keyboard map as the primary source. When pressure values are needed they
 * will be added alongside `buttons` without disturbing the existing fields.
 */
#ifndef EM_INPUT_H
#define EM_INPUT_H

#include <stdint.h>

#include "em_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Digital button bits — canonical DualShock 2 order. */
enum {
    EM_PAD_SELECT   = 1 << 0,
    EM_PAD_L3       = 1 << 1,
    EM_PAD_R3       = 1 << 2,
    EM_PAD_START    = 1 << 3,
    EM_PAD_UP       = 1 << 4,
    EM_PAD_RIGHT    = 1 << 5,
    EM_PAD_DOWN     = 1 << 6,
    EM_PAD_LEFT     = 1 << 7,
    EM_PAD_L2       = 1 << 8,
    EM_PAD_R2       = 1 << 9,
    EM_PAD_L1       = 1 << 10,
    EM_PAD_R1       = 1 << 11,
    EM_PAD_TRIANGLE = 1 << 12,
    EM_PAD_CIRCLE   = 1 << 13,
    EM_PAD_CROSS    = 1 << 14,
    EM_PAD_SQUARE   = 1 << 15
};

typedef struct {
    uint16_t buttons;        /* EM_PAD_* bits, 1 = pressed */
    float    lx, ly;         /* left stick,  [-1,1], -1 = left/up */
    float    rx, ry;         /* right stick, [-1,1], -1 = left/up */
} EmPadState;

/* Reset all internal state to "nothing held, sticks centered". */
void em_input_init(void);

/* Feed one platform event. Only KEY_DOWN/KEY_UP are consumed (per the
 * keyboard map above); everything else is ignored, so the caller can route
 * its whole event stream through unconditionally. */
void em_input_handle_event(const EmEvent *ev);

/* Snapshot the current pad state into *out. */
void em_input_pad(EmPadState *out);

/* Name of the button at `bit_index` (0..15, canonical order above), e.g.
 * 14 -> "CROSS". Returns "?" for out-of-range values. For debug output. */
const char *em_pad_button_name(int bit_index);

#ifdef __cplusplus
}
#endif

#endif /* EM_INPUT_H */
