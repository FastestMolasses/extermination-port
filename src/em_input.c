/* em_input.c — keyboard -> DualShock 2 pad-state mapping.
 *
 * Plain C, no OS calls (see em_input.h). State is a module-level singleton:
 * the original game reads one pad, and keeping the model allocation-free
 * makes it trivially testable (tests/input_test.c).
 */
#include "em_input.h"

#include <string.h>

typedef struct {
    uint16_t buttons;                    /* EM_PAD_* bits currently held */
    int      lsu, lsd, lsl, lsr;         /* WASD held flags, left stick  */
    int      rsu, rsd, rsl, rsr;         /* TFGH held flags, right stick */
    int      alt;                        /* Option/Alt held — TURN-band gait
                                          * hold (em_input.h GAIT HOLD TIERS) */
    int      cmd;                        /* Command held — WALK-band gait
                                          * hold */
} InputState;

static InputState s_in;

void em_input_init(void)
{
    memset(&s_in, 0, sizeof s_in);
}

/* Keyboard map (documented in em_input.h). Returns the EM_PAD_* bit for a
 * digital-button key, or 0 if the key is not a button (or unmapped). */
static uint16_t key_to_button(int key)
{
    switch (key) {
        case EM_KEY_UP:        return EM_PAD_UP;
        case EM_KEY_RIGHT:     return EM_PAD_RIGHT;
        case EM_KEY_DOWN:      return EM_PAD_DOWN;
        case EM_KEY_LEFT:      return EM_PAD_LEFT;
        case 'k':              return EM_PAD_CROSS;
        case 'l':              return EM_PAD_CIRCLE;
        case 'i':              return EM_PAD_TRIANGLE;
        case 'j':              return EM_PAD_SQUARE;
        case 'q':              return EM_PAD_L1;
        case 'e':              return EM_PAD_R1;
        case '1':              return EM_PAD_L2;
        case '3':              return EM_PAD_R2;
        case '2':              return EM_PAD_L3;
        case '4':              return EM_PAD_R3;
        case EM_KEY_RETURN:    return EM_PAD_START;
        case EM_KEY_BACKSPACE: return EM_PAD_SELECT;
        default:               return 0;
    }
}

void em_input_handle_event(const EmEvent *ev)
{
    if (!ev) return;
    if (ev->type != EM_EVENT_KEY_DOWN && ev->type != EM_EVENT_KEY_UP) return;

    const int down = (ev->type == EM_EVENT_KEY_DOWN);

    uint16_t bit = key_to_button(ev->key);
    if (bit) {
        if (down) s_in.buttons |=  bit;
        else      s_in.buttons &= (uint16_t)~bit;
        return;
    }

    switch (ev->key) {           /* sticks + the gait modifier */
        case 'w': s_in.lsu = down; break;    /* left stick (WASD)  */
        case 's': s_in.lsd = down; break;
        case 'a': s_in.lsl = down; break;
        case 'd': s_in.lsr = down; break;
        case 't': s_in.rsu = down; break;    /* right stick (TFGH) */
        case 'g': s_in.rsd = down; break;
        case 'f': s_in.rsl = down; break;
        case 'h': s_in.rsr = down; break;
        case EM_KEY_ALT: s_in.alt = down; break; /* TURN-band gait hold */
        case EM_KEY_CMD: s_in.cmd = down; break; /* WALK-band gait hold */
        default:  break;         /* unmapped key — ignore */
    }
}

/* GAMEPAD OVERLAY (em_gamepad.h). em_input stays OS-free — the platform
 * backend does the Apple/Win32/evdev work and pushes a finished EmPadState
 * here. While a pad is attached its state REPLACES the keyboard's for that
 * frame; NULL clears the overlay so unplugging falls straight back to the
 * keyboard with no state carried across. */
static EmPadState s_gp;
static int        s_gp_present;

void em_input_set_gamepad(const EmPadState *gp)
{
    if (gp) { s_gp = *gp; s_gp_present = 1; }
    else    { s_gp_present = 0; }
}

void em_input_pad(EmPadState *out)
{
    if (!out) return;
    if (s_gp_present) { *out = s_gp; return; }
    out->buttons = s_in.buttons;
    /* Opposing keys cancel; -1 = left/up, +1 = right/down (em_input.h).
     * The GAIT HOLD TIERS (em_input.h): no modifier = FULL (RUN),
     * Command = the JOG band, Option = the WALK band — the slower
     * tier wins when both modifiers are held. */
    const float d = s_in.alt ? EM_INPUT_DEFLECT_WALK
                  : s_in.cmd ? EM_INPUT_DEFLECT_JOG
                             : EM_INPUT_DEFLECT_FULL;
    out->lx = d * (float)(s_in.lsr - s_in.lsl);
    out->ly = d * (float)(s_in.lsd - s_in.lsu);
    out->rx = d * (float)(s_in.rsr - s_in.rsl);
    out->ry = d * (float)(s_in.rsd - s_in.rsu);
    if (s_in.alt || s_in.cmd) {
        /* A hold caps the VECTOR magnitude, not each axis: the engine
         * quantizes sqrt(lx^2 + ly^2), so a per-axis cap would push a
         * diagonal sqrt(2) past the held ring into the next gait.
         * Digital axes are 0/±d here, so a diagonal pair normalizes by
         * exactly 1/sqrt(2). Without a hold the per-axis full
         * deflection stands — corner pushes overshoot the run ring
         * exactly like the PCSX2 keyboard binding this map mirrors. */
        const float diag = 0.70710678f;
        if (out->lx != 0.0f && out->ly != 0.0f) {
            out->lx *= diag;
            out->ly *= diag;
        }
        if (out->rx != 0.0f && out->ry != 0.0f) {
            out->rx *= diag;
            out->ry *= diag;
        }
    }
}

uint16_t em_pad_swap(uint16_t mask)
{
    return (uint16_t)((mask << 8) | (mask >> 8));
}

static uint8_t pad_axis_byte(float axis)
{
    int v = 0x80 + (int)(axis * 128.0f);
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

void em_pad_raw(const EmPadState *pad, uint8_t raw[8])
{
    raw[0] = 0;                                   /* libpad status: ok */
    raw[1] = 0x73;                                /* DualShock analog */
    raw[2] = (uint8_t)~(pad->buttons & 0xFF);     /* active low */
    raw[3] = (uint8_t)~(pad->buttons >> 8);
    raw[4] = pad_axis_byte(pad->rx);
    raw[5] = pad_axis_byte(pad->ry);
    raw[6] = pad_axis_byte(pad->lx);
    raw[7] = pad_axis_byte(pad->ly);
}

/* 001B5CC0: r = sqrt(dx^2 + dy^2) against 48/88/122. dx^2 + dy^2 is an
 * integer <= 32768, so the float compare equals this integer compare. */
static uint8_t pad_gait(uint8_t x, uint8_t y)
{
    int dx = x - 0x80, dy = y - 0x80, r2 = dx * dx + dy * dy;
    if (r2 <= 48 * 48)   return 0;
    if (r2 <= 88 * 88)   return 1;
    if (r2 <= 122 * 122) return 2;
    return 3;
}

/* 001B5C90: (v + 2) & 0xFC, saturating at 0xFC. */
static uint8_t pad_quantize(uint8_t v)
{
    unsigned t = v + 2u;
    return (uint8_t)(t < 0x100 ? (t & 0xFC) : 0xFC);
}

/* 001B5D70: stick byte -> original-layout D-pad bit (axis 0 = x). */
static uint16_t pad_stick_dpad(uint8_t v, int axis)
{
    if (v < 0x10)  return axis ? 0x1000 : 0x8000;   /* UP / LEFT */
    if (v >= 0xE1) return axis ? 0x4000 : 0x2000;   /* DOWN / RIGHT */
    return 0;
}

/* 001B5E20: D-pad -> left stick bytes with gait 3; UP beats DOWN and
 * RIGHT beats LEFT; no direction centres the stick with gait 0. */
static void pad_dpad_stick(EmPadUnpack *u)
{
    uint16_t b = u->held;
    uint8_t x = 0x80, y = 0x80;
    u->gait = 3;
    if (b & 0x1000) {
        if (b & 0x2000)      { x = 0xF0; y = 0x10; }
        else if (b & 0x8000) { x = 0x10; y = 0x10; }
        else                 { y = 0x00; }
    } else if (b & 0x4000) {
        if (b & 0x2000)      { x = 0xF0; y = 0xF0; }
        else if (b & 0x8000) { x = 0x10; y = 0xF0; }
        else                 { y = 0xFF; }
    } else if (b & 0x2000) {
        x = 0xFF;
    } else if (b & 0x8000) {
        x = 0x00;
    } else {
        u->gait = 0;
    }
    u->lx = x;
    u->ly = y;
}

int em_pad_unpack(EmPadUnpack *u, const uint8_t raw[8], unsigned port,
                  int analog)
{
    if (raw[0] != 0) return 0;
    u->prev_held = u->held;
    u->held = (uint16_t)(((unsigned)raw[2] << 8 | raw[3]) ^ 0xFFFFu);
    if (!analog) {
        pad_dpad_stick(u);
    } else {
        u->rx = raw[4];
        u->ry = raw[5];
        u->lx = raw[6];
        u->ly = raw[7];
        u->gait = pad_gait(u->lx, u->ly);
        if (u->gait == 0) {
            if (port == 0 && (u->held & 0xF000)) pad_dpad_stick(u);
        } else {
            if (port == 0) {
                u->held &= 0x0FFF;
                u->held |= pad_stick_dpad(u->lx, 0);
                u->held |= pad_stick_dpad(u->ly, 1);
            }
            u->lx = pad_quantize(u->lx);
            u->ly = pad_quantize(u->ly);
        }
    }
    u->prev_pressed = u->pressed;
    u->pressed = (uint16_t)(u->held & ~u->prev_held);
    uint16_t dpad = u->held & 0xF000, prev_dpad = u->prev_held & 0xF000;
    if (dpad != prev_dpad || dpad == 0) {
        u->repeat_timer = 0x20;
        u->repeat = u->pressed;
    } else {
        u->repeat_timer = (int16_t)(u->repeat_timer - 1);
        if (u->repeat_timer == 0) {
            u->repeat = (uint16_t)(dpad | (u->pressed & 0x0FFF));
            u->repeat_timer = 10;
        } else {
            u->repeat = u->pressed & 0x0FFF;
        }
    }
    return 1;
}

const char *em_pad_button_name(int bit_index)
{
    static const char *const names[16] = {
        "SELECT", "L3", "R3", "START",
        "UP", "RIGHT", "DOWN", "LEFT",
        "L2", "R2", "L1", "R1",
        "TRIANGLE", "CIRCLE", "CROSS", "SQUARE"
    };
    if (bit_index < 0 || bit_index > 15) return "?";
    return names[bit_index];
}
