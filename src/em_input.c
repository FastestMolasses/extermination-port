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
    int      lsu, lsd, lsl, lsr;         /* WASD held flags for the left stick */
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
        case EM_KEY_UP:     return EM_PAD_UP;
        case EM_KEY_RIGHT:  return EM_PAD_RIGHT;
        case EM_KEY_DOWN:   return EM_PAD_DOWN;
        case EM_KEY_LEFT:   return EM_PAD_LEFT;
        case 'k':           return EM_PAD_CROSS;
        case 'l':           return EM_PAD_CIRCLE;
        case 'i':           return EM_PAD_TRIANGLE;
        case 'j':           return EM_PAD_SQUARE;
        case 'q':           return EM_PAD_L1;
        case 'e':           return EM_PAD_R1;
        case 'u':           return EM_PAD_L2;
        case 'o':           return EM_PAD_R2;
        case 'r':           return EM_PAD_L3;
        case EM_KEY_RETURN: return EM_PAD_START;
        case EM_KEY_TAB:    return EM_PAD_SELECT;
        default:            return 0;
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

    switch (ev->key) {           /* left stick, digital full deflection */
        case 'w': s_in.lsu = down; break;
        case 's': s_in.lsd = down; break;
        case 'a': s_in.lsl = down; break;
        case 'd': s_in.lsr = down; break;
        default:  break;         /* unmapped key — ignore */
    }
}

void em_input_pad(EmPadState *out)
{
    if (!out) return;
    out->buttons = s_in.buttons;
    /* Opposing keys cancel; -1 = left/up, +1 = right/down (em_input.h). */
    out->lx = (float)(s_in.lsr - s_in.lsl);
    out->ly = (float)(s_in.lsd - s_in.lsu);
    out->rx = 0.0f;              /* right stick unmapped for now */
    out->ry = 0.0f;
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
