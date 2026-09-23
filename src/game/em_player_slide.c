/* em_player_slide.c - the player's slope slide, state 0x1C (see em_player_slide.h).
 *
 * Read from the original instructions (0016C6A0 and its callees), not from
 * the readable decompilation alone: the NEARMISS 0016C6A0 C shows the two
 * 00178B90 calls of sub-states 0xB/0xC as (1, 0); the instructions pass
 * (p, 1). tools/test_player_slide_reference.py executes the original
 * instructions and compares every field and worker call. */
#include "game/em_player_slide.h"
#include "game/em_effect_color.h"

#include <stddef.h>

static float f32_add(float a, float b) { return em_effect_float32((double)a + b); }
static float f32_sub(float a, float b) { return em_effect_float32((double)a - b); }
static float f32_mul(float a, float b) { return em_effect_float32((double)a * b); }
static float f32_div(float a, float b) { return em_effect_float32((double)a / b); }

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

static int request(const EmPlayerSlideWorkers *w, int clip, int force, float blend)
{
    return w->request ? w->request(w->context, clip, force, blend) : -1;
}

/* 001B12B0: step `current` toward `target` by at most `rate` along the
 * wrapped difference. A zero difference returns wrap(current). */
float em_player_slide_approach(float target, float current, float rate)
{
    float difference = em_player_sdk_wrap(f32_sub(target, current));
    if (difference == 0.0f) return em_player_sdk_wrap(current);
    if (difference <= 0.0f) {
        if (-difference <= rate) return target;
        return em_player_sdk_wrap(f32_sub(current, rate));
    }
    if (difference <= rate) return target;
    return em_player_sdk_wrap(f32_add(current, rate));
}

/* 00174FD0: the stick quadrant relative to the body (+24C). */
int em_player_slide_steer_input(EmPlayerSlideActor *a, const EmPlayerSlideScene *s,
                                const EmPlayerSlideWorkers *w)
{
    const float pi = 3.1415927f;
    if (s->scripted) {
        if (a->major == 1 && a->state == 9) {
            a->gait = 0;
            a->steer = -1;
        }
        return 0;
    }
    a->gait = s->pad_gait;
    if (a->gait == 0) {
        a->steer = -1;
        return 0;
    }
    if (!w->cosine || !w->atan2) return -1;
    float angle_y = f32_mul(pi, f32_div((float)s->pad_y, 256.0f));
    a->pad_x = w->cosine(w->context, f32_mul(pi, f32_div((float)s->pad_x, 256.0f)));
    a->pad_y = w->cosine(w->context, angle_y);
    float heading = w->atan2(w->context, -a->pad_y, a->pad_x);
    float magnitude = heading < 0.0f ? -heading : heading;             /* 0011DF78 */
    if (magnitude > 2.3561945f) a->steer = 2;
    else if (magnitude < 0.7853982f) a->steer = 3;
    else if (heading < 0.0f) a->steer = 0;
    else a->steer = 1;
    return 0;
}

/* D_00248790 / D_002487A0, indexed by the gait byte +23F. */
static const float kSteerSpeed[4] = { 0.0f, 0.1f, 0.3f, 0.6f };
static const float kSteerAngle[4] = { 0.0f, 5.0f, 15.0f, 30.0f };

/* 0017F5F0(p, skip_clips). */
int em_player_slide_steer(EmPlayerSlideActor *a, const EmPlayerSlideScene *s,
                          int skip, const EmPlayerSlideWorkers *w)
{
    FAULT(em_player_slide_steer_input(a, s, w));
    if (a->gait > 3 || !w->sine) return -1;
    if (a->steer == 0) {
        if (!skip) FAULT(request(w, 0x61, 0, 8.0f));
        float factor = f32_add(1.0f, kSteerSpeed[a->gait]);
        float sine = w->sine(w->context, a->slope);
        a->speed = f32_add(a->speed, f32_mul(factor, f32_mul(0.01f, sine)));
        if (a->variant != 1) {
            a->ramp_timer = 30.0f;
            a->ramp = f32_div(f32_mul(a->speed, factor), a->ramp_timer);
            a->variant = 1;
        } else {
            float timer = a->ramp_timer;
            a->ramp_timer = f32_sub(timer, 1.0f);
            if (timer == 0.0f) a->speed = f32_add(a->speed, a->ramp);
        }
    } else if (a->steer == 1) {
        if (!skip) FAULT(request(w, 0x62, 0, 8.0f));
        float factor = f32_sub(1.0f, kSteerSpeed[a->gait]);
        if (a->variant != 2) {
            a->ramp_timer = 30.0f;
            a->ramp = f32_div(f32_mul(a->speed, factor), a->ramp_timer);
            a->variant = 2;
        } else {
            float timer = a->ramp_timer;
            a->ramp_timer = f32_sub(timer, 1.0f);
            if (timer == 0.0f) a->speed = f32_sub(a->speed, a->ramp);
        }
    } else {
        a->variant = 0;
        if (!skip) FAULT(request(w, 0x5F, 0, 8.0f));
        float sine = w->sine(w->context, a->slope);
        a->speed = f32_add(a->speed, f32_mul(0.01f, sine));
    }
    if (!(a->speed <= 1.5f)) a->speed = 1.5f;

    const float pi = 3.1415927f, rate = 0.06981317f;
    if (a->steer == 2 || a->steer == 3) {
        a->lean = a->steer == 2 ? 1 : 2;
        if (!skip) FAULT(request(w, a->steer == 2 ? 0x63 : 0x64, 0, 8.0f));
        float offset = f32_div(f32_mul(pi, kSteerAngle[a->gait]), 180.0f);
        float angle = a->steer == 2 ? f32_sub(a->slide_yaw, offset)
                                    : f32_add(a->slide_yaw, offset);
        a->rotation[1] = em_player_slide_approach(em_player_sdk_wrap(angle), a->rotation[1], rate);
    } else {
        if ((a->lean == 1 || a->lean == 2) && !skip) FAULT(request(w, 0x5F, 0, 12.0f));
        a->lean = 0;
        a->rotation[1] = em_player_slide_approach(a->slide_yaw, a->rotation[1], rate);
    }
    return 0;
}

/* D_00248950: the eight sweep lanes. */
static const float kLaneAngles[8] = {
    0.0f, 0.785398185253143311f, -0.785398185253143311f, 1.57079637050628662f,
    -1.57079637050628662f, 2.35619449615478516f, -2.35619449615478516f,
    3.14159274101257324f
};

/* 001791D0: two rings of eight sweeps from the hip; wall hits push. */
int em_player_slide_sweeps(EmPlayerSlideActor *a, const EmPlayerSlideScene *s,
                           const EmPlayerSlideWorkers *w)
{
    static const float zero[3] = { 0.0f, 0.0f, 0.0f };
    a->obstruction = 0;
    for (int ring = 0; ring < 2; ++ring) {
        float height = ring == 0 ? 4.01f : 10.0f;
        const float outer[4] = { 0.0f, height, 4.5f, 0.0f };
        const float inner[4] = { 0.0f, height, 0.0f, 0.0f };
        for (int lane = 0; lane < 8; ++lane) {
            float to[4], from[4];
            em_player_sdk_lane_point(a->rotation[1], kLaneAngles[lane], zero, outer, to);
            to[0] = f32_add(to[0], s->hip[0]);
            to[2] = f32_add(to[2], s->hip[1]);
            to[1] = f32_add(to[1], a->position[1]);
            em_player_sdk_lane_point(a->rotation[1], kLaneAngles[lane], zero, inner, from);
            from[0] = f32_add(from[0], s->hip[0]);
            from[2] = f32_add(from[2], s->hip[1]);
            from[1] = f32_add(from[1], a->position[1]);
            if (!w->sweep) return -1;
            EmPlayerProbeHit hit = {0};
            int kind = w->sweep(w->context, from, to, 7, &hit);
            if (kind < 0) return -1;
            if (kind != 0 && (hit.node & 0xFF00) == 0x2000) {
                a->obstruction |= (uint8_t)(1u << lane);
                for (int axis = 0; axis < 3; ++axis)
                    a->position[axis] = f32_add(a->position[axis], hit.delta[axis]);
            }
        }
    }
    return 0;
}

/* 0016C570: the entry probes 4.5 either side across the downhill heading. */
int em_player_slide_side_probes(EmPlayerSlideActor *a, const EmPlayerSlideWorkers *w)
{
    /* One matrix, translated by the position before either response. */
    static const float left[4] = { 4.5f, 0.1f, 0.0f, 1.0f };
    static const float right[4] = { -4.5f, 0.1f, 0.0f, 1.0f };
    float target[2][4];
    em_player_sdk_yaw_transform(a->slide_yaw, a->position, left, target[0]);
    em_player_sdk_yaw_transform(a->slide_yaw, a->position, right, target[1]);
    for (int side = 0; side < 2; ++side) {
        if (!w->move) return -1;
        FAULT(w->move(w->context, a->position, target[side], 0x80000006u));
    }
    return 0;
}

/* 0016C520. */
int em_player_slide_release_sound(EmPlayerSlideActor *a, const EmPlayerSlideWorkers *w)
{
    if (a->sound == -1) return 0;
    if (!w->stop_sound) return -1;
    FAULT(w->stop_sound(w->context, a->sound));
    a->sound = -1;
    a->sound_live = 0;
    return 0;
}

static int play_loop(EmPlayerSlideActor *a, const EmPlayerSlideWorkers *w)
{
    int handle = 0;
    if (!w->sound) return -1;
    FAULT(w->sound(w->context, 0x12E, &handle));
    a->sound = (int8_t)handle;
    return 0;
}

static int floor_service(EmPlayerSlideActor *a, int search, const EmPlayerSlideWorkers *w,
                         int *result)
{
    if (!w->floor) return -1;
    return w->floor(w->context, a, search, result);
}

/* 0016CD70(p, hold). */
int em_player_slide_motion(EmPlayerSlideActor *a, const EmPlayerSlideScene *s,
                           int hold, const EmPlayerSlideWorkers *w)
{
    const float local[4] = { 0.0f, 0.0f, a->speed, 0.0f };
    float error = em_player_sdk_wrap(f32_sub(a->slide_yaw, a->rotation[1]));
    if (error < 0.0f) error = -error;                                  /* 0011DF78 */
    float heading = error <= 0.5235988f ? a->rotation[1] : a->slide_yaw;
    float step[4];
    em_player_sdk_yaw_transform(heading, NULL, local, step);
    a->position[0] = f32_add(a->position[0], step[0]);
    a->position[2] = f32_add(a->position[2], step[2]);
    FAULT(em_player_slide_sweeps(a, s, w));
    if (!w->sine) return -1;
    float sine = w->sine(w->context, a->slope);
    a->position[1] = f32_sub(a->position[1], f32_mul(a->speed, sine));
    a->position[1] = f32_add(a->position[1], -0.6f);
    a->position[1] = f32_add(a->position[1], a->drop);
    int contact = 0;
    FAULT(floor_service(a, 0, w, &contact));
    if (contact != 0) {
        if (a->slide == 0) {
            if (a->speed != 0.0f) {
                float impact = f32_div(a->speed, 0.75f);
                a->impact = impact <= 1.0f ? impact : 1.0f;
                a->speed = 0.0f;
            }
            if (hold == 0 && a->walk >= 3) {
                a->walk = 0xA;
                a->mode = 0;
                FAULT(em_player_slide_release_sound(a, w));
                return 1;
            }
        } else {
            a->drop = 0.0f;
        }
    } else if (!(a->drop <= -0.19999999f)) {
        a->drop = f32_add(a->drop, -0.04f);
    } else if (hold == 0 && a->walk >= 3) {
        a->walk = 0x14;
        a->mode = 0xB;
        FAULT(em_player_slide_release_sound(a, w));
        return 2;
    }
    if (a->sound_live != 0 && a->sound_id == 0x12E && a->sound == -1)
        FAULT(play_loop(a, w));
    a->ticks = (int16_t)(a->ticks + 1);
    if (!(a->ticks & 7)) {
        uint32_t id;
        switch (a->surface) {
        case 0x5: case 0x5A: id = 0x80000065u; break;
        case 0x8: case 0x5C: id = 0x80000066u; break;
        case 0x6: case 0x5B: case 0x7: id = 0x80000033u; break;
        default: id = 0x80000012u; break;
        }
        if (!w->effect) return -1;
        FAULT(w->effect(w->context, id, a->position, a->rotation));
    }
    return 0;
}

static int tail(EmPlayerSlideActor *a, float lower, const EmPlayerSlideWorkers *w)
{
    int ignored = 0;
    a->position[1] = f32_add(a->position[1], lower);
    if (lower == -0.6f) a->position[1] = f32_add(a->position[1], a->drop);
    FAULT(floor_service(a, 1, w, &ignored));
    if (!w->fall) return -1;
    return w->fall(w->context, a);
}

static int translate(EmPlayerSlideActor *a, const EmPlayerSlideWorkers *w)
{
    if (!w->translate) return -1;
    return w->translate(w->context, a, 1);
}

static int damage(EmPlayerSlideActor *a, const EmPlayerSlideWorkers *w, int *result)
{
    if (!w->damage) return -1;
    return w->damage(w->context, a, result);
}

/* 0016C6A0. */
int em_player_slide_tick(EmPlayerSlideActor *a, const EmPlayerSlideScene *s,
                         const EmPlayerSlideWorkers *w)
{
    const float lean_rate = 0.10471976f;
    int result = 0;
    switch (a->walk) {
    case 0: {
        FAULT(em_player_slide_side_probes(a, w));
        a->position[1] = f32_add(a->position[1], -0.6f);
        int ignored = 0;
        FAULT(floor_service(a, 0, w, &ignored));
        a->walk = (uint8_t)(a->walk + 1);
        a->sub = 0;
        a->lean = 0;
        a->speed = 0.2f;                                               /* fabs(-0.2) */
        a->drop = 0.0f;
        int frames = 0;
        if (!w->clip_frames || !w->arbiter) return -1;
        FAULT(w->clip_frames(w->context, 0x5E, &frames));
        FAULT(w->arbiter(w->context, 0x5E, 8.0f, f32_sub((float)frames, 15.0f)));
        a->variant = 0;
        a->ramp = 0.0f;
        a->entry_y = a->position[1];
        a->ticks = 0;
        a->ramp_timer = 0.0f;
        FAULT(play_loop(a, w));
        a->sound_live = 1;
        a->sound_id = 0x12E;
        break;
    }
    case 1:
    case 2:
        FAULT(damage(a, w, &result));
        if (result == 2) {
            FAULT(em_player_slide_release_sound(a, w));
        } else if (a->walk == 1) {
            a->rotation[1] = em_player_slide_approach(a->slide_yaw, a->rotation[1], lean_rate);
            a->rotation[0] = em_player_slide_approach(a->slope, a->rotation[0], lean_rate);
            if (a->rotation[0] == a->slope) {
                a->walk = (uint8_t)(a->walk + 1);
                a->drop = 0.0f;
            }
        } else {
            a->rotation[1] = em_player_slide_approach(a->slide_yaw, a->rotation[1], lean_rate);
            if (a->rotation[1] == a->slide_yaw) a->walk = (uint8_t)(a->walk + 1);
        }
        FAULT(em_player_slide_sweeps(a, s, w));
        break;
    case 3:
        FAULT(damage(a, w, &result));
        if (result == 2) {
            FAULT(em_player_slide_release_sound(a, w));
        } else {
            FAULT(em_player_slide_steer(a, s, result, w));
            FAULT(em_player_slide_motion(a, s, result, w));
        }
        break;
    case 0xA:
        a->rotation[1] = a->slide_yaw;
        a->walk = (uint8_t)(a->walk + 1);
        a->rotation[0] = 0.0f;
        FAULT(request(w, 0x60, 0, 1.0f));
        a->lock = 0;
        a->root_prev = 0.0f;
        a->speed = 0.0f;
        a->drop = 0.0f;
        if (!w->land_sound) return -1;
        FAULT(w->land_sound(w->context, 1));
        break;
    case 0xB:
        if (a->anim_flags & 0x1000) {
            a->walk = (uint8_t)(a->walk + 1);
            a->step = 0;
            FAULT(request(w, 0x65, 0, 1.0f));
            a->root_prev = 0.0f;
            a->speed = 0.0f;
        } else {
            a->speed = f32_sub(s->root_forward, a->root_prev);
            a->root_prev = s->root_forward;
            FAULT(translate(a, w));
        }
        FAULT(tail(a, -0.6f, w));
        break;
    case 0xC:
        if (a->anim_flags & 0x1000) {
            a->state = 0;
            a->walk = 0;
            a->mode = 0;
        } else {
            a->speed = f32_sub(s->root_forward, a->root_prev);
            a->root_prev = s->root_forward;
            a->speed = f32_mul(a->speed, a->impact);
            FAULT(translate(a, w));
            static const float kStepClock[3] = { 24.0f, 13.0f, 2.0f };
            if (a->step < 3 && a->clock <= kStepClock[a->step]) {
                a->step = (uint8_t)(a->step + 1);
                if (!w->step_sound) return -1;
                FAULT(w->step_sound(w->context, 2));
            }
        }
        FAULT(tail(a, -0.4f, w));
        break;
    case 0x14: {
        a->walk = (uint8_t)(a->walk + 1);
        int frames = 0;
        if (!w->clip_frames || !w->arbiter) return -1;
        FAULT(w->clip_frames(w->context, 0x73, &frames));
        FAULT(w->arbiter(w->context, 0x73, 8.0f, f32_sub((float)frames, 10.0f)));
        a->land_y = a->position[1];
        a->drop = 0.0f;
        a->fall_rate = f32_div(a->speed, 60.0f);
        break;
    }
    case 0x15: {
        a->rotation[0] = em_player_slide_approach(0.0f, a->rotation[0], 0.06981317f);
        int landed = 0;
        if (!w->land_check) return -1;
        FAULT(w->land_check(w->context, a, &landed));
        if (a->speed <= a->fall_rate) a->speed = 0.0f;
        else a->speed = f32_sub(a->speed, a->fall_rate);
        FAULT(translate(a, w));
        if (a->obstruction & 1) {
            a->speed = 0.0f;
            a->state = 7;
            a->walk = 0;
            a->mode = 0xD;
        } else {
            a->drop = f32_add(a->drop, -0.04f);                        /* 00179880 */
            if (a->drop < -4.0f) a->drop = -4.0f;
            a->position[1] = f32_add(a->position[1], a->drop);
            a->lock = 2;
            int ignored = 0;
            FAULT(floor_service(a, 1, w, &ignored));
            if (a->contact != 0) {
                if (landed == 0) {
                    if (!w->land) return -1;
                    FAULT(w->land(w->context, a));
                } else {
                    FAULT(request(w, 0x6D, 0, 1.0f));
                }
            }
        }
        break;
    }
    case 0x1E:
        if (!w->teleport) return -1;
        FAULT(w->teleport(w->context));
        break;
    default:
        break;
    }
    if (a->walk != 0x1E && a->surface == 0x5D) {
        if (!w->surface5d) return -1;
        FAULT(w->surface5d(w->context));
    }
    return 0;
}
