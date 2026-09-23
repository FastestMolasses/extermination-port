#include "game/em_status_background.h"
#include "game/em_pose_math.h"

#include <math.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

/* float_to_int (001281C0): cvt.w.s, truncation toward 0. */
static int to_int(float value, int32_t *out)
{
    if (!isfinite(value) || value >= 2147483648.0f || value < -2147483648.0f)
        return 0;
    *out = (int32_t)value;
    return 1;
}

/* 00128250: truncation to an unsigned word. Every value this function
 * passes is a colour component in 0..96; a negative or non-finite one is
 * outside the translated domain. */
static int to_unsigned(float value, uint32_t *out)
{
    if (!isfinite(value) || !(value >= 0.0f) || value >= 4294967296.0f)
        return 0;
    *out = (uint32_t)value;
    return 1;
}

static int colour(const EmStatusBackgroundLayer *e, uint32_t *rgba)
{
    uint32_t r, g, b, a;
    if (!to_unsigned(e->rgba[0], &r) || !to_unsigned(e->rgba[1], &g) ||
        !to_unsigned(e->rgba[2], &b) || !to_unsigned(e->rgba[3], &a))
        return 0;
    *rgba = r | g << 8 | b << 16 | a << 24;
    return 1;
}

/* The shared colour block: (96, 96, 96, 64), then +0x1C *= sin(pi *
 * +0x8 / 180) (mul.s pi*phase, div.s by 180, 0011E2A8, mul.s). */
static int shade(EmStatusBackgroundLayer *e, const EmStatusBackgroundWorkers *w)
{
    e->rgba[0] = e->rgba[1] = e->rgba[2] = 96.0f;
    e->rgba[3] = 64.0f;
    float angle = pose_div(pose_mul(3.14159274101257324f, e->phase), 180.0f);
    float sine = w->sine(w->context, angle);
    if (!isfinite(sine))
        return 0;
    e->rgba[3] = pose_mul(e->rgba[3], sine);
    return 1;
}

void em_status_background_init(EmStatusBackground *state)
{
    if (!state)
        return;
    memset(state, 0, sizeof *state);
    for (int i = 0; i < 2; ++i) {
        EmStatusBackgroundLayer *e = &state->layer[i];
        e->phase = 90.0f;
        e->rgba[0] = e->rgba[1] = e->rgba[2] = 96.0f;
        e->rgba[3] = 64.0f;
    }
}

int em_status_background_step(EmStatusBackground *state, const EmStatusBackgroundWorkers *w)
{
    if (!state || !w || !w->sine || !w->random || !w->mode || !w->sprite)
        return -1;
    if (w->mode(w->context, 1, 0) != 1)
        return -1;
    for (int i = 0; i < 3; ++i) {
        EmStatusBackgroundLayer *e = &state->layer[i];
        /* Pulse, layers 1 and 2 (0x20A810). */
        if (i != 0) {
            if (e->timer >= 0) {
                e->timer -= 1;
            } else {
                float phase = pose_add(e->phase, 0.25f);
                e->phase = phase;
                if (!(phase < 180.0f)) {
                    int32_t r = w->random(w->context);
                    if (r < 0)
                        return -1;
                    e->timer = (r % 60) * 3 + 60;
                    e->phase = 0.0f;
                    e->x = 0.0f;
                    e->y = 0.0f;
                }
            }
        }
        /* Update (0x20A8B8 / 0x20A948 / 0x20A9D8). */
        if (i == 0 || i == 1) {
            float *offset = i == 0 ? &e->x : &e->y;
            float value = pose_sub(*offset, 0.5f);
            if (value < 0.0f)
                value = 255.0f;
            *offset = value;
            if (!shade(e, w))
                return -1;
        } else if (e->timer < 0) {
            e->x = pose_add(e->x, 0.300000011920928955f);
            e->y = pose_add(e->y, 0.300000011920928955f);
            if (!shade(e, w))
                return -1;
        }
        /* Draw (0x20AA64). */
        if (i == 0 || i == 1) {
            for (int32_t row = 0x400; row < 0xC00; row += 0x40) {
                for (int32_t column = 0x400; column < 0xC00; column += 0x100) {
                    uint32_t rgba;
                    int32_t x, y;
                    if (!colour(e, &rgba) || !to_int(e->x, &x) || !to_int(e->y, &y))
                        return -1;
                    if (w->sprite(w->context, (int32_t)((uint32_t)(column + x) << 4),
                                  (int32_t)((uint32_t)(row + y) << 4), 0x100, 0x80, rgba) != 1)
                        return -1;
                }
            }
        } else if (e->timer < 0) {
            float px = e->x, py = e->y;
            int32_t x, y, width, height;
            uint32_t rgba;
            if (!to_int(pose_mul(16.0f, pose_sub(1792.0f, px)), &x) ||
                !to_int(pose_mul(16.0f, pose_add(1936.0f, pose_div(-py, 2.0f))), &y) ||
                !colour(e, &rgba) || !to_int(pose_mul(2.0f, px), &width) ||
                !to_int(pose_mul(2.0f, py), &height))
                return -1;
            if (w->sprite(w->context, x, y, width + 0x200, height + 0x1C0, rgba) != 1)
                return -1;
        }
    }
    return 1;
}
