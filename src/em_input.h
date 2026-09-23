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
 * work). This is verbatim the user's PCSX2 binding, so muscle memory carries
 * over from the emulator to the port:
 *   W/A/S/D     left stick up/left/down/right (digital, full deflection)
 *   T/F/G/H     right stick up/left/down/right (same layout, home row)
 *   Arrow keys  d-pad UP/LEFT/DOWN/RIGHT
 *   I           TRIANGLE        (status screen)
 *   J           SQUARE          (heavy knife / sub-weapon toggle)
 *   L           CIRCLE          (FIRE / light knife combo)
 *   K           CROSS           (USE / confirm)
 *   Q / E       L1 / R1         (R1 = weapon-draw hold)
 *   1 / 3       L2 / R2
 *   2 / 4       L3 / R3         (L3 = the engine's raw-bit manual reload)
 *   Backspace   SELECT
 *   Return      START
 *
 * GAIT HOLD TIERS: the keyboard's digital keys emulate the analog stick,
 * so the MODIFIERS select the engine's gait rings (both synthesized as
 * KEY_DOWN/KEY_UP by the platform layer, em_platform.h):
 *
 *   (none)        FULL deflection 1.0  -> gait 3 = RUN (raw 127 > 122,
 *                 anim id 3, 0.8 u/tick = 48 u/s sustained)
 *   Command held  JOG cap         0.8  -> gait 2 = JOG (raw ~102, the
 *                 88 < r <= 122 ring; anim id 2, 18 u/s)
 *   Option held   WALK cap        0.5  -> gait 1 = WALK (raw 64, the
 *                 48 < r <= 88 ring; anim id 1, 6 u/s)
 *
 * Rationale: the engine quantizes the left-stick MAGNITUDE r =
 * |raw - 0x80| through rings r=48/88/122 (func_001B5CC0) into the gait
 * byte; func_00174AC0 maps the gait to a TARGET speed D_00248870 =
 * {0, 0.1, 0.3, 0.8} u/tick and the tier ramp func_0017BC40 promotes
 * the locomotion tier until the tier speed matches (the 2026-06-11
 * tier-ramp correction — the old reading paired every gait with the
 * tier BELOW it, which is why the port "walked by default": the PCSX2
 * full stick is the id-3 RUN). Turn-in-place (tier 0) is only the
 * gait-1 entry transient, not a sustained band. If both modifiers are
 * held, the slower tier (Option) wins. Full push with no modifier is
 * RUN — exactly the PCSX2 full-stick behavior this map mirrors.
 *
 * Every cap bounds the stick VECTOR magnitude: a diagonal (e.g. W+A) is
 * normalized by 1/sqrt(2) so the combined deflection stays in-band — a
 * per-axis cap would quantize sqrt(2) larger and jump a ring (0.8-cap
 * diagonals would hit raw ~144 > 122 = RUN). Applied by this module in
 * em_input_pad (the platform only reports the modifiers), so it is
 * identical on every OS and covered by tests/input_test.c.
 *
 * ENGINE DEFAULT BUTTON CONFIG (live-pinned 2026-06-10 s29 — decomp repo
 * FINDINGS.md "GAMEPLAY SOUND IDS PINNED LIVE"). The engine reads actions
 * through a CONFIG-MASK BLOCK of u16 slots at scratchpad 0x70003B70..7E,
 * tested against the pressed-edge word D_00810E74 (001B5940: held & ~prev).
 * The block words are the DS2 digital halfword BYTE-SWAPPED, so the
 * engine masks decode as:
 *
 *   0x0001 L2   0x0002 R2     0x0004 L1    0x0008 R1
 *   0x0010 TRI  0x0020 CIRCLE 0x0040 CROSS 0x0080 SQUARE
 *   0x0100 SEL  0x0200 L3     0x0400 R3    0x0800 START
 *   0x1000 UP   0x2000 RIGHT  0x4000 DOWN  0x8000 LEFT
 *
 * Default config block (slot -> engine mask -> button -> action):
 *   spad 0x3B70  0x0800  START   (action not pinned)
 *   spad 0x3B72  0x0800  START   (action not pinned)
 *   spad 0x3B74  0x0080  SQUARE  HEAVY knife stab when unarmed (mode
 *                                0x22); sub-weapon action while armed
 *                                (attachment 0 = the 0x179 toggle) —
 *                                decoded s36, em_weapon.h "KNIFE / MELEE"
 *   spad 0x3B76  0x0040  CROSS   USE / confirm (the use-scan gate
 *                                0x810E74 & spad3B76 — doors)
 *   spad 0x3B78  0x0020  CIRCLE  FIRE (the trigger — NOT Cross); the
 *                                LIGHT knife combo when unarmed (mode
 *                                0x21 — s36)
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

/* Emulated stick deflection magnitudes — SEMANTIC CONSTANTS for the game
 * code (src/game/): test |lx|/|ly| against these, not bare literals.
 *   FULL (1.0)  keyboard default = the engine quantizer's gait-3 RUN band
 *               (raw > 122 of 127) — anim id 3, 48 u/s sustained.
 *   JOG  (0.8)  the COMMAND gait hold = the gait-2 JOG band (raw ~102,
 *               inside the 88 < r <= 122 ring; the band only spans
 *               deflections ~0.70..0.95) — anim id 2, 18 u/s.
 *   WALK (0.5)  the OPTION/ALT gait hold = the gait-1 WALK band
 *               (raw 64, mid the 48 < r <= 88 ring) — anim id 1,
 *               6 u/s, the engine's slowest sustained movement tier
 *               (see "GAIT HOLD TIERS" above; tier-ramp corrected
 *               2026-06-11 — the old TURN label was the misread).
 * Under a hold the cap bounds the stick VECTOR magnitude (diagonals
 * normalize by 1/sqrt(2)), so the quantized r stays in-band in every
 * direction. Movement code that maps deflection -> gait must classify
 * 0.5 as WALK, 0.8 as JOG and 1.0 as RUN; a future analog gamepad
 * backend will deliver the continuous range and the same ring
 * thresholds apply. */
#define EM_INPUT_DEFLECT_FULL 1.0f
#define EM_INPUT_DEFLECT_JOG  0.8f
#define EM_INPUT_DEFLECT_WALK 0.5f

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
/* Push a real gamepad's state in (src/platform/<os>/, via em_gamepad.h).
 * While set, it REPLACES the keyboard map for that frame; NULL clears it.
 * Keeps this module OS-free — see em_gamepad.h for why a pad matters beyond
 * convenience (force feedback, pressure-sensitive buttons, the analog gait
 * rings are all decoded but unusable without one). */
void em_input_set_gamepad(const EmPadState *gp);

void em_input_pad(EmPadState *out);

/* Name of the button at `bit_index` (0..15, canonical order above), e.g.
 * 14 -> "CROSS". Returns "?" for out-of-range values. For debug output. */
const char *em_pad_button_name(int bit_index);

/* ORIGINAL PAD UNPACK (001B5940, called by 001B5F40 from step C 001B57E0).
 * Fields mirror the original storage: the six halfwords at 0x00810E70 in
 * the original BYTE-SWAPPED layout (START = 0x0800, UP = 0x1000), the
 * analog bytes of the pad struct 0x00810E40 at +0x24..+0x27
 * (0x00810E64..67) and its gait byte at +0x17 (0x00810E57). The whole
 * translation is checked instruction-for-instruction by
 * tools/test_input_block_reference.py. Zero-initialize (original .bss). */
typedef struct {
    uint16_t held;          /* 0x810E70 processed held mask */
    uint16_t prev_held;     /* 0x810E72 previous held mask */
    uint16_t pressed;       /* 0x810E74 held & ~prev_held */
    uint16_t prev_pressed;  /* 0x810E76 previous pressed mask */
    uint16_t repeat;        /* 0x810E78 pressed with D-pad auto-repeat */
    int16_t  repeat_timer;  /* 0x810E7A repeat countdown (32, then 10) */
    uint8_t  lx, ly;        /* 0x810E64/65 left stick (quantized when gait) */
    uint8_t  rx, ry;        /* 0x810E66/67 right stick, always raw */
    uint8_t  gait;          /* 0x810E57 001B5CC0 ring 0..3 */
} EmPadUnpack;

/* Byte swap between canonical EM_PAD_* bits and the original layout. */
uint16_t em_pad_swap(uint16_t mask);

/* Native pad -> the 8-byte libpad read buffer 001B5940 consumes: status 0,
 * mode 0x73, active-low button bytes, then right x/y and left x/y bytes
 * (0x80-centred; float axis v maps to 0x80 + (int)(v * 128), clamped).
 * The conversion is native host adaptation, not an original function. */
void em_pad_raw(const EmPadState *pad, uint8_t raw[8]);

/* One 001B5940 call. `port` is pad struct +4 (the D-pad/stick exchange
 * only happens on port 0); `analog` is its third argument (1 when
 * 001B5F40 sees libpad state 6, 0 for state 2). Returns 0 without
 * touching *u when raw[0] (libpad status) is nonzero, else 1. */
int em_pad_unpack(EmPadUnpack *u, const uint8_t raw[8], unsigned port,
                  int analog);

#ifdef __cplusplus
}
#endif

#endif /* EM_INPUT_H */
