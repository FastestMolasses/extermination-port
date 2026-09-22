#include "game/em_player_motor.h"

#include <math.h>

/* Finite EE arithmetic, rounded after every operation. This matters at
 * .3: repeated .05 additions remain one ULP below the boundary for a tick. */
static float scalar(double value)
{
    float result = (float)value;
    if (fabs((double)result) > fabs(value)) result = nextafterf(result, 0);
    return result;
}

static const float speed[4] = {0, .1f, .3f, .8f};
static const float rise[4] = {.05f, .05f, .0625f, 0};
static const float fall[4] = {0, .05f, .025f, .022727273404598236f};

static float fraction(float upper, float value, float lower)
{
    float numerator = scalar((double)upper - value);
    float denominator = scalar((double)upper - lower);
    return scalar(1.0 - scalar((double)numerator / denominator));
}

static void stop(EmPlayerMotor *m)
{
    if (m->tier < 3) m->mode = 3;
    else { m->mode = 2; m->blend = 0; }
}

void em_player_motor_tick(EmPlayerMotor *m)
{
    if (!m || m->tier > 3) return;
    unsigned i = m->tier;
    switch (m->mode) {
    case 1:
        if (m->substate == 1) {
            if (i == 3) return; /* No higher tier in valid original states. */
            m->speed = scalar((double)m->speed + rise[i]);
            if (m->speed >= speed[i + 1]) {
                m->speed = speed[i + 1];
                ++m->tier;
                m->substate = 0;
            } else {
                m->blend = fraction(speed[i + 1], m->speed, speed[i]);
                m->rate = scalar(1.0 + m->blend);
            }
        } else if (m->substate == 2) {
            if (m->target == 0) stop(m);
            else if (i != 0) {
                m->speed = scalar((double)m->speed - fall[i]);
                if (m->speed <= speed[i - 1]) {
                    m->speed = speed[i - 1];
                    --m->tier;
                    m->substate = 0;
                } else {
                    m->blend = fraction(speed[i - 1], m->speed, speed[i]);
                    m->rate = scalar(1.0 + m->blend);
                }
            }
        } else if (m->target == 0) stop(m);
        else if (m->speed < m->target) { m->substate = 1; m->blend = 0; }
        else if (m->speed > m->target) {
            if (i > 1) m->substate = 2;
            else m->mode = 3;
            m->blend = 0;
        } else if (i == 2) m->rate = .75f;
        break;
    case 2:
        if (i == 0) return;
        m->speed = scalar((double)m->speed - ((m->obstruction & 31) ? .0625f : .03125f));
        if (m->speed <= speed[i - 1]) {
            m->speed = speed[i - 1];
            if (m->gait) {
                m->mode = 1;
                --m->tier;
                m->blend = 0;
                m->substate = m->speed < m->target ? 1 : m->speed > m->target ? 2 : 0;
            } else { m->speed = 0; m->mode = 3; m->substate = 0; }
        }
        m->rate = scalar(1.0 + scalar(.5 * fraction(speed[i - 1], m->speed, speed[i])));
        break;
    case 6:
        m->speed = scalar((double)m->speed - .05f);
        if (m->speed < 0) m->speed = 0;
        break;
    default:
        break;
    }
}

void em_player_stop_begin(EmPlayerStop *s, unsigned frames)
{
    if (!s || frames < 6) return;
    *s = (EmPlayerStop){1, 6, frames - 6, frames - 1};
}

void em_player_stop_tick(EmPlayerStop *s)
{
    if (!s) return;
    switch (s->phase) {
    case 1:
        if (--s->blend_left == 0) s->phase = 2;
        break;
    case 2:
        if (s->frame >= s->last_frame) s->phase = 3;
        else ++s->frame;
        break;
    case 3:
        s->phase = 4;
        s->blend_left = 12;
        break;
    case 4:
        if (--s->blend_left == 0) s->phase = 0;
        break;
    default:
        break;
    }
}

int em_player_reentry_begin(EmPlayerReentry *r, EmPlayerMotor *m,
                           unsigned gait, unsigned frames)
{
    unsigned remaining = gait == 3 ? 18 : 46;
    if (!r || !m || gait < 2 || gait > 3 || frames < remaining) return 0;
    m->tier = gait - 1;
    m->speed = speed[m->tier];
    m->mode = 1;
    *r = (EmPlayerReentry){1, 4, frames - remaining};
    return 1;
}

void em_player_reentry_tick(EmPlayerReentry *r, EmPlayerMotor *m)
{
    if (!r || !m || r->phase != 1) return;
    if (--r->blend_left == 0) {
        r->phase = 2;
        /* 0017C540 restores walk phase0 and scalar substate0. Both
         * valid interruption tiers are nonzero. */
        m->mode = 1;
        m->substate = 0;
    }
}
