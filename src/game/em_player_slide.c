/* em_player_slide.c - the player's slope slide, state 0x1C (see em_player_slide.h).
 *
 * Read from the original instructions (0016C6A0 and its callees), not from
 * the readable decompilation alone: the NEARMISS 0016C6A0 C shows the two
 * 00178B90 calls of sub-states 0xB/0xC as (1, 0); the instructions pass
 * (p, 1). tools/test_player_slide_reference.py executes the original
 * instructions on the measured float model and compares every field, the
 * scratch words and every worker call. Every COP1 operation and compare
 * goes through em_ee_float.h; 00174FD0, 00179880, 001B12B0, 001B1470,
 * 0011DF78 and the SDK leaves run through their one translation. */
#include "game/em_player_slide.h"
#include "game/em_player_record_helpers.h"
#include "game/em_script_host_workers.h"
#include "game/em_sdk_math_original.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

static float add(float a, float b) { return em_ee_add(a, b); }
static float sub(float a, float b) { return em_ee_sub(a, b); }
static float mul(float a, float b) { return em_ee_mul(a, b); }
static float divide(float a, float b) { return em_ee_div(a, b); }
static int le(float a, float b) { return em_ee_c_le(a, b); }
static int eq(float a, float b) { return em_ee_c_eq(a, b); }
static uint32_t fbits(float v) { return em_ee_bits(v); }
static float bfloat(uint32_t b) { return em_ee_float(b); }

static int request(const EmPlayerSlideWorkers *w, int clip, int force, float blend)
{
    return w->request ? w->request(w->context, clip, force, blend) : -1;
}

/* The scratch stores: the bound instance, else discarded. */
static void store_38A0(const EmPlayerSlideWorkers *w, const uint32_t v[4])
{
    if (w->scratch) memcpy(w->scratch->s38A0, v, sizeof w->scratch->s38A0);
}

static void store_3A20(const EmPlayerSlideWorkers *w, float v)
{
    if (w->scratch) w->scratch->s3A20 = fbits(v);
}

/* 001B12B0(target, current, rate) (em_script_host_001B12B0). */
static int approach(float target, float current, float rate, float *out)
{
    uint32_t r;
    FAULT(em_script_host_001B12B0(NULL, fbits(target), fbits(current), fbits(rate), &r));
    *out = bfloat(r);
    return 0;
}

float em_player_slide_approach(float target, float current, float rate)
{
    float out;
    return approach(target, current, rate, &out) < 0 ? current : out;
}

/* 001B1470 (bounded, em_player_helper_wrap). */
static int wrap(float x, float *out)
{
    uint32_t r;
    FAULT(em_player_helper_wrap(fbits(x), &r));
    *out = bfloat(r);
    return 0;
}

/* 001029C0, 00102BB0(M, M, angle), [00102918(M, M, base)], 001026A0. */
static int yaw_point(float angle, const float base[3], const float local[4], float out[4])
{
    uint32_t b[3], v[4], o[4];
    for (int i = 0; i < 3 && base; ++i) b[i] = fbits(base[i]);
    for (int i = 0; i < 4; ++i) v[i] = fbits(local[i]);
    FAULT(em_player_helper_yaw_point(fbits(angle), base ? b : NULL, v, o));
    for (int i = 0; i < 4; ++i) out[i] = bfloat(o[i]);
    return 0;
}

/* ---- 00174FD0 over the mirror (em_player_record_00174FD0) ------------------- */

typedef struct SteerCalls {
    const EmPlayerSlideWorkers *w;
} SteerCalls;

static float steer_cosine(void *context, float x)
{
    const SteerCalls *c = context;
    return c->w->cosine(c->w->context, x);
}

static float steer_atan2(void *context, float y, float x)
{
    const SteerCalls *c = context;
    return c->w->atan2(c->w->context, y, x);
}

int em_player_slide_steer_input(EmPlayerSlideActor *a, const EmPlayerSlideScene *s,
                                const EmPlayerSlideWorkers *w)
{
    if (!w->cosine || !w->atan2) return -1;
    SteerCalls calls = { w };
    const uint8_t spad3B8D = s->scripted, gait = s->pad_gait, x = s->pad_x, y = s->pad_y;
    EmPlayerLandScratch local;
    memset(&local, 0, sizeof local);
    EmPlayerRecordHelpers h;
    memset(&h, 0, sizeof h);
    h.context = &calls;
    h.cosine = steer_cosine;
    h.atan2 = steer_atan2;
    h.spad3B8D = &spad3B8D;
    h.d810E57 = &gait;
    h.d810E64 = &x;
    h.d810E65 = &y;
    h.scratch = w->scratch ? w->scratch : &local;
    EmPlayerLiveActor record;
    memset(&record, 0, sizeof record);
    em_player_slide_actor_to_live(a, &record);
    int r = em_player_record_00174FD0(&h, &record);
    em_player_slide_actor_from_live(&record, a);
    return r;
}

/* 00179880(p, p + 2EC) (em_player_fall_00179880) over the mirror. */
static void drop_step(EmPlayerSlideActor *a)
{
    EmPlayerLiveActor record;
    memset(&record, 0, sizeof record);
    em_player_slide_actor_to_live(a, &record);
    em_player_fall_00179880(&record, 0x2EC);
    em_player_slide_actor_from_live(&record, a);
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
        float factor = add(1.0f, kSteerSpeed[a->gait]);
        store_3A20(w, factor);
        float sine = w->sine(w->context, a->slope);
        a->speed = add(a->speed, mul(factor, mul(0.01f, sine)));
        if (a->variant != 1) {
            a->ramp_timer = 30.0f;
            a->ramp = divide(mul(a->speed, factor), a->ramp_timer);
            a->variant = 1;
        } else {
            float timer = a->ramp_timer;
            a->ramp_timer = sub(timer, 1.0f);
            if (eq(timer, 0.0f)) a->speed = add(a->ramp, a->speed);
        }
    } else if (a->steer == 1) {
        if (!skip) FAULT(request(w, 0x62, 0, 8.0f));
        float factor = sub(1.0f, kSteerSpeed[a->gait]);
        store_3A20(w, factor);
        if (a->variant != 2) {
            a->ramp_timer = 30.0f;
            a->ramp = divide(mul(a->speed, factor), a->ramp_timer);
            a->variant = 2;
        } else {
            float timer = a->ramp_timer;
            a->ramp_timer = sub(timer, 1.0f);
            if (eq(timer, 0.0f)) a->speed = sub(a->speed, a->ramp);
        }
    } else {
        a->variant = 0;
        if (!skip) FAULT(request(w, 0x5F, 0, 8.0f));
        float sine = w->sine(w->context, a->slope);
        a->speed = add(a->speed, mul(0.01f, sine));
    }
    if (!le(a->speed, 1.5f)) a->speed = 1.5f;

    const float pi = 3.1415927f, rate = 0.06981317f;
    if (a->steer == 2 || a->steer == 3) {
        a->lean = a->steer == 2 ? 1 : 2;
        if (!skip) FAULT(request(w, a->steer == 2 ? 0x63 : 0x64, 0, 8.0f));
        float offset = divide(mul(pi, kSteerAngle[a->gait]), 180.0f);
        float angle = a->steer == 2 ? sub(a->slide_yaw, offset) : add(a->slide_yaw, offset);
        float wrapped;
        FAULT(wrap(angle, &wrapped));
        FAULT(approach(wrapped, a->rotation[1], rate, &a->rotation[1]));
    } else {
        if ((a->lean == 1 || a->lean == 2) && !skip) FAULT(request(w, 0x5F, 0, 12.0f));
        a->lean = 0;
        FAULT(approach(a->slide_yaw, a->rotation[1], rate, &a->rotation[1]));
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
    a->obstruction = 0;
    for (int ring = 0; ring < 2; ++ring) {
        float height = ring == 0 ? 4.01f : 10.0f;
        const float outer[4] = { 0.0f, height, 4.5f, 0.0f };
        const float inner[4] = { 0.0f, height, 0.0f, 0.0f };
        for (int lane = 0; lane < 8; ++lane) {
            float angle, to[4], from[4];
            FAULT(wrap(add(a->rotation[1], kLaneAngles[lane]), &angle));
            FAULT(yaw_point(angle, NULL, outer, to));
            to[0] = add(to[0], s->hip[0]);
            to[2] = add(to[2], s->hip[1]);
            to[1] = add(to[1], a->position[1]);
            FAULT(yaw_point(angle, NULL, inner, from));
            from[0] = add(from[0], s->hip[0]);
            from[2] = add(from[2], s->hip[1]);
            from[1] = add(from[1], a->position[1]);
            if (!w->sweep) return -1;
            EmPlayerProbeHit hit;
            memset(&hit, 0, sizeof hit);
            int kind = w->sweep(w->context, from, to, 7, &hit);
            if (kind < 0) return -1;
            if (kind != 0 && (hit.node & 0xFF00) == 0x2000) {
                a->obstruction |= (uint8_t)(1u << lane);
                for (int axis = 0; axis < 3; ++axis)
                    a->position[axis] = add(a->position[axis], hit.delta[axis]);
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
    const float base[3] = { a->position[0], a->position[1], a->position[2] };
    float target[4];
    for (int side = 0; side < 2; ++side) {
        const float *local = side == 0 ? left : right;
        uint32_t v[4];
        for (int i = 0; i < 4; ++i) v[i] = fbits(local[i]);
        store_38A0(w, v);                                               /* 0x700038A0 */
        FAULT(yaw_point(a->slide_yaw, base, local, target));
        if (!w->move) return -1;
        FAULT(w->move(w->context, a->position, target, 0x80000006u));
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
    uint32_t v[4];
    for (int i = 0; i < 4; ++i) v[i] = fbits(local[i]);
    store_38A0(w, v);                                                   /* 0x700038A0 */
    float wrapped;
    FAULT(wrap(sub(a->slide_yaw, a->rotation[1]), &wrapped));
    float error = em_sdk_math_original_0011DF78(wrapped);
    float heading = le(error, 0.5235988f) ? a->rotation[1] : a->slide_yaw;
    float step[4];
    FAULT(yaw_point(heading, NULL, local, step));
    a->position[0] = add(a->position[0], step[0]);
    a->position[2] = add(a->position[2], step[2]);
    FAULT(em_player_slide_sweeps(a, s, w));
    if (!w->sine) return -1;
    float sine = w->sine(w->context, a->slope);
    a->position[1] = sub(a->position[1], mul(a->speed, sine));
    a->position[1] = add(a->position[1], -0.6f);
    a->position[1] = add(a->position[1], a->drop);
    int contact = 0;
    FAULT(floor_service(a, 0, w, &contact));
    if (contact != 0) {
        if (a->slide == 0) {
            if (!eq(a->speed, 0.0f)) {
                float impact = divide(a->speed, 0.75f);
                a->impact = impact;
                if (!le(impact, 1.0f)) a->impact = 1.0f;
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
    } else if (!le(a->drop, -0.19999999f)) {
        a->drop = add(a->drop, -0.04f);
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
    a->position[1] = add(a->position[1], lower);
    if (eq(lower, -0.6f)) a->position[1] = add(a->position[1], a->drop);
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

/* The clip length as 0016C6A0 keeps it: (float)001C61D0(...) in 0x70003A20. */
static int clip_length(const EmPlayerSlideWorkers *w, int clip, float *length)
{
    int frames = 0;
    if (!w->clip_frames) return -1;
    FAULT(w->clip_frames(w->context, clip, &frames));
    *length = em_ee_cvt_s_w(frames);
    store_3A20(w, *length);                                             /* 0x70003A20 */
    return 0;
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
        a->position[1] = add(a->position[1], -0.6f);
        int ignored = 0;
        FAULT(floor_service(a, 0, w, &ignored));
        a->walk = (uint8_t)(a->walk + 1);
        a->sub = 0;
        a->lean = 0;
        a->speed = em_sdk_math_original_0011DF78(-0.2f);                /* fabs(-0.2) */
        a->drop = 0.0f;
        float length;
        if (!w->arbiter) return -1;
        FAULT(clip_length(w, 0x5E, &length));
        FAULT(w->arbiter(w->context, 0x5E, 8.0f, sub(length, 15.0f)));
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
            FAULT(approach(a->slide_yaw, a->rotation[1], lean_rate, &a->rotation[1]));
            FAULT(approach(a->slope, a->rotation[0], lean_rate, &a->rotation[0]));
            if (eq(a->rotation[0], a->slope)) {
                a->walk = (uint8_t)(a->walk + 1);
                a->drop = 0.0f;
            }
        } else {
            FAULT(approach(a->slide_yaw, a->rotation[1], lean_rate, &a->rotation[1]));
            if (eq(a->rotation[1], a->slide_yaw)) a->walk = (uint8_t)(a->walk + 1);
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
            a->speed = sub(s->root_forward, a->root_prev);
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
            a->speed = sub(s->root_forward, a->root_prev);
            a->root_prev = s->root_forward;
            a->speed = mul(a->speed, a->impact);
            FAULT(translate(a, w));
            static const float kStepClock[3] = { 24.0f, 13.0f, 2.0f };
            if (a->step < 3 && le(a->clock, kStepClock[a->step])) {
                a->step = (uint8_t)(a->step + 1);
                if (!w->step_sound) return -1;
                FAULT(w->step_sound(w->context, 2));
            }
        }
        FAULT(tail(a, -0.4f, w));
        break;
    case 0x14: {
        a->walk = (uint8_t)(a->walk + 1);
        float length;
        if (!w->arbiter) return -1;
        FAULT(clip_length(w, 0x73, &length));
        FAULT(w->arbiter(w->context, 0x73, 8.0f, sub(length, 10.0f)));
        a->land_y = a->position[1];
        a->drop = 0.0f;
        a->fall_rate = divide(a->speed, 60.0f);
        break;
    }
    case 0x15: {
        FAULT(approach(0.0f, a->rotation[0], 0.06981317f, &a->rotation[0]));
        int landed = 0;
        if (!w->land_check) return -1;
        FAULT(w->land_check(w->context, a, &landed));
        if (le(a->speed, a->fall_rate)) a->speed = 0.0f;
        else a->speed = sub(a->speed, a->fall_rate);
        FAULT(translate(a, w));
        if (a->obstruction & 1) {
            a->speed = 0.0f;
            a->state = 7;
            a->walk = 0;
            a->mode = 0xD;
        } else {
            drop_step(a);                                              /* 00179880 */
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

/* ---- The live state 0x1C ------------------------------------------------ */

void em_player_slide_actor_from_live(const EmPlayerLiveActor *live, EmPlayerSlideActor *a)
{
    memset(a, 0, sizeof *a);
    for (unsigned i = 0; i < 3; ++i) {
        a->position[i] = em_live_f32(live, 0xB0 + 4 * i);
        a->rotation[i] = em_live_f32(live, 0xC0 + 4 * i);
    }
    a->speed = em_live_f32(live, 0x38);
    a->clock = em_live_f32(live, 0x3C);
    a->slope = em_live_f32(live, 0x9C);
    a->slide_yaw = em_live_f32(live, 0x218);
    a->root_prev = em_live_f32(live, 0x21C);
    a->impact = em_live_f32(live, 0x26C);
    a->fall_rate = em_live_f32(live, 0x2E0);
    a->ramp = em_live_f32(live, 0x2E4);
    a->drop = em_live_f32(live, 0x2EC);
    a->land_y = em_live_f32(live, 0x2F4);
    a->ramp_timer = em_live_f32(live, 0x2F8);
    a->entry_y = em_live_f32(live, 0x294);
    a->pad_x = em_live_f32(live, 0x244);
    a->pad_y = em_live_f32(live, 0x248);
    a->steer = (int32_t)em_live_u32(live, 0x24C);
    a->anim_flags = em_live_u32(live, 0x200);
    a->ticks = (int16_t)em_live_u16(live, 0x2A);
    a->sound_id = em_live_u16(live, 0x31C);
    a->sound = (int8_t)em_live_u8(live, 0x31B);
    a->sound_live = em_live_u8(live, 0x31A);
    a->major = em_live_u8(live, 4);
    a->state = em_live_u8(live, 5);
    a->walk = em_live_u8(live, 6);
    a->sub = em_live_u8(live, 7);
    a->mode = em_live_u8(live, 0x1F0);
    a->variant = em_live_u8(live, 0x1F1);
    a->lean = em_live_u8(live, 0x25D);
    a->lock = em_live_u8(live, 0x25F);
    a->step = em_live_u8(live, 0x302);
    a->obstruction = em_live_u8(live, 0x314);
    a->slide = em_live_u8(live, 0x237);
    a->surface = em_live_u8(live, 0x23A);
    a->gait = em_live_u8(live, 0x23F);
    a->contact = em_live_u8(live, 0xA);
}

void em_player_slide_actor_to_live(const EmPlayerSlideActor *a, EmPlayerLiveActor *live)
{
    for (unsigned i = 0; i < 3; ++i) {
        em_live_set_f32(live, 0xB0 + 4 * i, a->position[i]);
        em_live_set_f32(live, 0xC0 + 4 * i, a->rotation[i]);
    }
    em_live_set_f32(live, 0x38, a->speed);
    em_live_set_f32(live, 0x3C, a->clock);
    em_live_set_f32(live, 0x9C, a->slope);
    em_live_set_f32(live, 0x218, a->slide_yaw);
    em_live_set_f32(live, 0x21C, a->root_prev);
    em_live_set_f32(live, 0x26C, a->impact);
    em_live_set_f32(live, 0x2E0, a->fall_rate);
    em_live_set_f32(live, 0x2E4, a->ramp);
    em_live_set_f32(live, 0x2EC, a->drop);
    em_live_set_f32(live, 0x2F4, a->land_y);
    em_live_set_f32(live, 0x2F8, a->ramp_timer);
    em_live_set_f32(live, 0x294, a->entry_y);
    em_live_set_f32(live, 0x244, a->pad_x);
    em_live_set_f32(live, 0x248, a->pad_y);
    em_live_set_u32(live, 0x24C, (uint32_t)a->steer);
    em_live_set_u32(live, 0x200, a->anim_flags);
    em_live_set_u16(live, 0x2A, (uint16_t)a->ticks);
    em_live_set_u16(live, 0x31C, a->sound_id);
    em_live_set_u8(live, 0x31B, (uint8_t)a->sound);
    em_live_set_u8(live, 0x31A, a->sound_live);
    em_live_set_u8(live, 4, a->major);
    em_live_set_u8(live, 5, a->state);
    em_live_set_u8(live, 6, a->walk);
    em_live_set_u8(live, 7, a->sub);
    em_live_set_u8(live, 0x1F0, a->mode);
    em_live_set_u8(live, 0x1F1, a->variant);
    em_live_set_u8(live, 0x25D, a->lean);
    em_live_set_u8(live, 0x25F, a->lock);
    em_live_set_u8(live, 0x302, a->step);
    em_live_set_u8(live, 0x314, a->obstruction);
    em_live_set_u8(live, 0x237, a->slide);
    em_live_set_u8(live, 0x23A, a->surface);
    em_live_set_u8(live, 0x23F, a->gait);
    em_live_set_u8(live, 0xA, a->contact);
}

/* The call in progress (the game runs one player stage at a time): the
 * binding, the record and the mirror the routine is working on. */
static struct {
    const EmPlayerSlideLive *binding;
    EmPlayerLiveActor *live;
    EmPlayerSlideActor *mirror;
} slide_call;

/* The mirror into the record before a worker runs, and back after. */
static void flush(void) { em_player_slide_actor_to_live(slide_call.mirror, slide_call.live); }
static void reload(void) { em_player_slide_actor_from_live(slide_call.live, slide_call.mirror); }

#define LIVE (slide_call.binding)
#define REC (slide_call.live)
#define W (&slide_call.binding->workers)
#define AROUND(call) do { flush(); int r_ = (call); reload(); return r_; } while (0)

static int t_request(void *c, int clip, int force, float blend)
{ (void)c; AROUND(LIVE->request(LIVE->live_context, REC, clip, force, blend)); }
static int t_arbiter(void *c, int clip, float blend, float frame)
{ (void)c; AROUND(LIVE->arbiter(LIVE->live_context, REC, clip, blend, frame)); }
static int t_clip_frames(void *c, int clip, int *frames)
{
    (void)c;
    int32_t n = 0;
    flush();
    int r = LIVE->clip_frames(LIVE->live_context, em_live_u32(REC, 0x40), clip, &n);
    reload();
    *frames = n;
    return r;
}
static int t_sound(void *c, unsigned id, int *handle)
{ (void)c; AROUND(LIVE->sound(LIVE->live_context, REC, (int)id, handle)); }
static int t_stop_sound(void *c, int handle)
{ (void)c; AROUND(W->stop_sound(W->context, handle)); }
static int t_effect(void *c, uint32_t id, const float p[3], const float r[3])
{ (void)c; AROUND(W->effect(W->context, id, p, r)); }
/* 0019AD00 with bit 31: the worker writes its x/z response into the
 * position it is given (the original moves +B0 and +B8 only); the adapter
 * hands it the record's +B0 lanes and stores x and z back. Any other byte
 * the worker writes, it writes on the record. */
static int t_move(void *c, float position[3], const float target[4], unsigned mask)
{
    (void)c;
    (void)position;
    flush();
    float at[3];
    for (unsigned i = 0; i < 3; ++i) at[i] = em_live_f32(REC, 0xB0 + 4 * i);
    int r = W->move(W->context, at, target, mask);
    em_live_set_f32(REC, 0xB0, at[0]);
    em_live_set_f32(REC, 0xB8, at[2]);
    reload();
    return r;
}
static int t_sweep(void *c, const float f[4], const float t[4], unsigned m, EmPlayerProbeHit *h)
{ (void)c; AROUND(W->sweep(W->context, f, t, m, h)); }
static float t_sine(void *c, float x)
{ (void)c; flush(); float r = W->sine(W->context, x); reload(); return r; }
static float t_cosine(void *c, float x)
{ (void)c; flush(); float r = W->cosine(W->context, x); reload(); return r; }
static float t_atan2(void *c, float y, float x)
{ (void)c; flush(); float r = W->atan2(W->context, y, x); reload(); return r; }
static int t_floor(void *c, EmPlayerSlideActor *a, int search, int *result)
{ (void)c; (void)a; AROUND(LIVE->floor(LIVE->live_context, REC, search, result)); }
static int t_fall(void *c, EmPlayerSlideActor *a)
{ (void)c; (void)a; AROUND(LIVE->fall(LIVE->live_context, REC)); }
static int t_translate(void *c, EmPlayerSlideActor *a, int arg)
{ (void)c; (void)a; AROUND(LIVE->translate(LIVE->live_context, REC, arg)); }
static int t_damage(void *c, EmPlayerSlideActor *a, int *result)
{ (void)c; (void)a; AROUND(LIVE->damage(LIVE->live_context, REC, result)); }
static int t_land_check(void *c, EmPlayerSlideActor *a, int *result)
{ (void)c; (void)a; AROUND(LIVE->land_check(LIVE->live_context, REC, result)); }
static int t_land(void *c, EmPlayerSlideActor *a)
{ (void)c; (void)a; AROUND(LIVE->land(LIVE->live_context, REC)); }
static int t_step_sound(void *c, int tier)
{ (void)c; AROUND(LIVE->step_sound(LIVE->live_context, REC, tier)); }
static int t_land_sound(void *c, int tier)
{ (void)c; AROUND(LIVE->land_sound(LIVE->live_context, REC, tier)); }
/* 0021D250(p, 0) and 0021D2E0(p, 0x78, 0): the constants 0016C6A0 passes. */
static int t_surface5d(void *c)
{ (void)c; AROUND(LIVE->surface5d(LIVE->live_context, REC, 0)); }
static int t_teleport(void *c)
{ (void)c; AROUND(LIVE->teleport(LIVE->live_context, REC, 0x78, 0)); }

#undef AROUND
#undef W
#undef REC
#undef LIVE

int em_player_slide_live_state(void *context, EmPlayerLiveActor *live)
{
    const EmPlayerSlideLive *b = context;
    if (!b || !live || !b->floor || !b->fall || !b->scene || !b->request || !b->arbiter ||
        !b->clip_frames || !b->sound || !b->translate || !b->damage || !b->land_check ||
        !b->land || !b->step_sound || !b->land_sound || !b->surface5d || !b->teleport ||
        !b->scratch)
        return -1;
    const EmPlayerSlideWorkers *w = &b->workers;
    if (!w->stop_sound || !w->effect || !w->move || !w->sweep || !w->sine || !w->cosine ||
        !w->atan2)
        return -1;
    EmPlayerSlideScene scene;
    memset(&scene, 0, sizeof scene);
    if (b->scene(b->scene_context, &scene) < 0) return -1;
    EmPlayerSlideWorkers workers;
    memset(&workers, 0, sizeof workers);
    workers.request = t_request;
    workers.arbiter = t_arbiter;
    workers.clip_frames = t_clip_frames;
    workers.sound = t_sound;
    workers.stop_sound = t_stop_sound;
    workers.effect = t_effect;
    workers.move = t_move;
    workers.sweep = t_sweep;
    workers.floor = t_floor;
    workers.fall = t_fall;
    workers.translate = t_translate;
    workers.damage = t_damage;
    workers.land_check = t_land_check;
    workers.land = t_land;
    workers.step_sound = t_step_sound;
    workers.land_sound = t_land_sound;
    workers.surface5d = t_surface5d;
    workers.teleport = t_teleport;
    workers.sine = t_sine;
    workers.cosine = t_cosine;
    workers.atan2 = t_atan2;
    workers.scratch = b->scratch;
    EmPlayerSlideActor actor;
    em_player_slide_actor_from_live(live, &actor);
    slide_call.binding = b;
    slide_call.live = live;
    slide_call.mirror = &actor;
    int result = em_player_slide_tick(&actor, &scene, &workers);
    slide_call.binding = NULL;
    slide_call.live = NULL;
    slide_call.mirror = NULL;
    /* Writes made before a fault stay, as the original order leaves them. */
    em_player_slide_actor_to_live(&actor, live);
    return result;
}
