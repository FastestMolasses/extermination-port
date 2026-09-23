/* em_player_reversal.c - WP-15/H11 reversal skid (00174AC0 reversal gate,
 * 0017C030 cases 7/6, 001612D0 case 2). Pure actor-state logic; animation,
 * sound, effect and turn side effects leave through explicit workers.
 * tools/test_player_reversal_reference.py executes the original
 * instructions against this file. */
#include "game/em_player_reversal.h"

#include <math.h>
#include <stddef.h>

#include "game/em_player_motor.h"

#define REVERSAL_PI     3.1415927f  /* 001B1470 / 0017C030 literal */
#define REVERSAL_TWO_PI 6.2831855f  /* 001B1470 literal */
#define REVERSAL_LIMIT  2.3561945f  /* 00174AC0 literal, 3pi/4 */
#define REVERSAL_SOUND  0x137u      /* 0017C030 case 7 */

/* Finite EE arithmetic: each operation rounds toward zero (as in
 * em_player_motor.c). */
static float ee(double value)
{
    float result = (float)value;
    if (fabs((double)result) > fabs(value)) result = nextafterf(result, 0);
    return result;
}

float em_player_reversal_wrap(float angle)
{
    while (angle > REVERSAL_PI) angle = ee((double)angle - REVERSAL_TWO_PI);
    while (angle <= -REVERSAL_PI) angle = ee((double)angle + REVERSAL_TWO_PI);
    return angle;
}

/* D_00248AB0[2..5] rows 0..4 (D_00248A38/48/58/68). Row 4 is the 0017B490
 * override when +236 == 0, !(+235 & 1) and 001B0070() & 4. */
static const int16_t kSkidClips[4][5] = {
    {0x06, 0x10, 0x51, 0x51, 0x1A},   /* command 2: turn, variant 3 */
    {0x08, 0x12, 0x53, 0x53, 0x1C},   /* command 3: follow-up, variant 3 */
    {0x07, 0x11, 0x52, 0x52, 0x1B},   /* command 4: turn, other variant */
    {0x09, 0x13, 0x54, 0x54, 0x1D},   /* command 5: follow-up, other */
};
/* D_00248AB0[1] (D_00248A10): 0017B490 body B reads tier + 4 * row, or
 * tier + 16 under the override. */
static const int16_t kGaitClips[20] = {
    0x00, 0x01, 0x02, 0x03, 0x0A, 0x0B, 0x0C, 0x0D, 0x4B, 0x4C,
    0x4D, 0x4E, 0x55, 0x4C, 0x4D, 0x4E, 0x14, 0x15, 0x16, 0x17,
};
/* D_00248870: 001612D0 case 2 resume speed by tier. */
static const float kTierSpeed[4] = {0.0f, 0.1f, 0.3f, 0.8f};
/* 00174AC0 target speed by gait (raw 0x3DCCCCCD/0x3E99999A/0x3F4CCCCD). */
static const float kGaitTarget[4] = {0.0f, 0.1f, 0.3f, 0.8f};

int em_player_reversal_clip(const EmPlayerReversalActor *a, unsigned command, int *clip)
{
    if (!a || !clip || command < 1 || command > 5) return 0;
    int override = a->special == 0 && !(a->row & 1) && (a->global_mode & 4);
    if (!override && a->row > 3) return 0;      /* +235 uses bits 0/1 only */
    if (command == 1) {
        if (a->tier > 3) return 0;
        *clip = kGaitClips[override ? a->tier + 16 : a->tier + 4 * a->row];
    } else {
        *clip = kSkidClips[command - 2][override ? 4 : a->row];
    }
    return 1;
}

int em_player_reversal_heading(EmPlayerReversalActor *a, float desired)
{
    if (!a) return 0;
    /* 00174AC0: latch +23F, write +240; gait 0 clears it and returns. */
    if (a->gait == 0) {
        a->target = 0.0f;
        return 0;
    }
    if (a->gait <= 3) a->target = kGaitTarget[a->gait];
    if (a->player_state == 1) {
        if (a->mode == 7 || a->mode == 6) return 0;
        if (!(a->speed <= 0.5f) && a->gait >= 2) {
            float error = em_player_reversal_wrap(ee((double)desired - a->yaw));
            a->delta = error;
            if (!(error <= REVERSAL_LIMIT)) {
                a->mode = 7;
                a->variant = 4;
                return 0;
            }
            if (error < -REVERSAL_LIMIT) {
                a->mode = 7;
                a->variant = 3;
                return 0;
            }
        }
    }
    return 1;
}

int em_player_reversal_animation(EmPlayerReversalActor *a, const EmPlayerReversalWorkers *w)
{
    if (!a) return -1;
    int clip;
    if (a->mode == 7) {
        /* 0017C030 case 7: turn clip (command 2 for variant 3, else 4),
         * 001749A0 flags 0 blend 4, stage 6, then 001FB9F0(0x137). */
        if (!w || !w->request || !w->sound) return -1;
        if (!em_player_reversal_clip(a, a->variant == 3 ? 2 : 4, &clip)) return -1;
        if (w->request(w->context, clip, 0, 4.0f) < 0) return -1;
        a->mode = 6;
        if (w->sound(w->context, REVERSAL_SOUND) < 0) return -1;
        return 1;
    }
    if (a->mode == 6) {
        /* 0017C030 case 6: wait for +200 bit 0x1000; meanwhile +204=0.75.
         * On the end flag: follow-up clip (3 / 5) with flags 1 blend 0,
         * stage 0, +25C=0, +38=0, heading turned by pi. */
        if (!(a->anim_flags & 0x1000)) {
            a->rate = 0.75f;
            return 1;
        }
        if (!w || !w->request) return -1;
        if (!em_player_reversal_clip(a, a->variant == 3 ? 3 : 5, &clip)) return -1;
        if (w->request(w->context, clip, 1, 0.0f) < 0) return -1;
        a->mode = 0;
        a->tier = 0;
        a->speed = 0.0f;
        a->yaw = em_player_reversal_wrap(ee((double)REVERSAL_PI + a->yaw));
        return 1;
    }
    return 0;
}

int em_player_reversal_walk_tail(EmPlayerReversalActor *a)
{
    if (!a || (a->mode != 6 && a->mode != 7)) return 0;
    ++a->walk_state;
    a->ticks = 0;
    return 1;
}

static int motor(EmPlayerReversalActor *a)
{
    /* 0017BC40 (em_player_motor.c, its own original-instruction oracle). */
    EmPlayerMotor m = {
        a->speed, a->target, a->rate, a->blend,
        a->mode, a->variant, a->tier, a->gait, a->obstruction
    };
    em_player_motor_tick(&m);
    a->speed = m.speed;
    a->rate = m.rate;
    a->blend = m.blend;
    a->mode = m.mode;
    a->variant = m.substate;
    a->tier = m.tier;
    return 1;
}

int em_player_reversal_state2(EmPlayerReversalActor *a, float desired,
                              const EmPlayerReversalWorkers *w)
{
    if (!a) return -1;
    if (em_player_reversal_heading(a, desired)) {
        if (!w || !w->turn || w->turn(w->context, desired) < 0) return -1;
    }
    if (a->mode != 6 && a->mode != 7) {
        if (a->target != 0.0f) {
            /* Resume: tier gait-1 at its D_00248870 speed; command-1 clip
             * with 001749A0(flags 0, blend 0) after variant 3, otherwise
             * anim_clip_arbiter(blend 0, frame = length / 2). */
            int clip;
            if (a->gait < 1 || a->gait > 3) return -1;
            a->tier = (uint8_t)(a->gait - 1);
            a->speed = kTierSpeed[a->tier];
            if (!em_player_reversal_clip(a, 1, &clip)) return -1;
            if (a->variant == 3) {
                if (!w || !w->request || w->request(w->context, clip, 0, 0.0f) < 0)
                    return -1;
            } else {
                int frames;
                if (!w || !w->clip_frames || !w->arbiter ||
                    w->clip_frames(w->context, clip, &frames) < 0)
                    return -1;
                /* (float)int / 2.0f: exact for clip lengths. */
                if (w->arbiter(w->context, clip, 0.0f, (float)frames / 2.0f) < 0)
                    return -1;
            }
            --a->walk_state;
            a->mode = 1;
            a->variant = 1;
        } else {
            a->player_state = 0;
            a->walk_state = 0;
            a->mode = 0;
        }
    } else {
        /* Surface effect on every eighth state-2 tick, keyed on +23A. */
        if (!(a->ticks & 7)) {
            uint32_t id = 0;
            if (a->surface != 6) {
                if (a->surface == 5) id = 0x80000033u;
                else if (a->depth[0] == 0 && a->depth[1] == 0) id = 0x80000012u;
            } else {
                id = 0x80000033u;
            }
            if (id && (!w || !w->effect || w->effect(w->context, id) < 0)) return -1;
        }
        a->ticks = (uint16_t)(a->ticks + 1);
    }
    motor(a);
    if (em_player_reversal_animation(a, w) < 0) return -1;
    return 1;
}
