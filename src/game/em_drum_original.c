#include "game/em_drum_original.h"
#include "game/em_ee_float.h"
#include "game/em_item_sdk_math.h"

#include <math.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

#define TRY(expr) do { if ((expr) < 0) return EM_DRUM_FAULT; } while (0)

/* The owner's own FPU arithmetic (sums, differences, products, quotients
 * and the integer-to-float conversion) is the measured EE model (em_ee_float.h, docs/EE_FLOAT_MODEL.md);
 * the SDK routines it calls keep the semantics their translations were
 * verified with. */
static float add(float a, float b) { return em_ee_add(a, b); }
static float sub(float a, float b) { return em_ee_sub(a, b); }
static float mul(float a, float b) { return em_ee_mul(a, b); }
static float divide(float a, float b) { return em_ee_div(a, b); }
static float cvt(int32_t v) { return em_ee_cvt_s_w(v); }

static int complete(const EmDrumOriginalHooks *h)
{
    return h && h->allocate_model && h->bone_init && h->place && h->segment &&
        h->effect_matrix && h->draw && h->contact && h->visibility && h->effect &&
        h->sound && h->random && h->sweep && h->probe && h->sound3d && h->hull &&
        h->free;
}

/* 0011E2A8 / 0011DE90 through the verified ITEM SDK translation, whose
 * domain is finite |angle| <= 4*pi (the heading is wrapped by 001B1470). */
static int trig(float angle, float *sine, float *cosine)
{
    if (!(fabsf(angle) <= 12.566371f)) return -1;
    if (sine) *sine = em_item_sdk_sine(angle);
    if (cosine) *cosine = em_item_sdk_cosine(angle);
    return 0;
}

static int tail(EmDrumOriginal *d, const EmDrumOriginalHooks *h)
{
    /* 00156EE0: 001C6380, 001A2370(self, +0xD0), 001B17A0, +0x4C. */
    TRY(h->place(h->context, d->world));
    TRY(h->hull(h->context, d->world));
    TRY(h->visibility(h->context, &d->visible));
    TRY(h->draw(h->context));
    return EM_DRUM_ALIVE;
}

static int landing(EmDrumOriginal *d, const EmDrumOriginalHooks *h, int status)
{
    /* 00156C94 / 00156D8C: FX at pos + (0,4,0), sound 0x1A0, free next. */
    const float point[4] = {d->position[0], add(4.0f, d->position[1]), d->position[2], 1.0f};
    TRY(h->effect(h->context, 0x80000013, point));
    TRY(h->effect(h->context, 0x8000002E, point));
    TRY(h->sound(h->context, 0x1A0));
    d->damage = 0; d->state = 3; d->phase = 0;
    if (status) d->status = 2;
    return tail(d, h);
}

static int armed(EmDrumOriginal *d, const EmDrumInput *in, const EmDrumOriginalHooks *h)
{
    if (d->damage) {                     /* 001566BC */
        d->status = 2; d->state = 2;
        float from[3] = {d->position[0], add(d->position[1], 4.0f), d->position[2]};
        float to[3] = {d->position[0], sub(d->position[1], 4.0f), d->position[2]};
        int hit = h->segment(h->context, from, to, 4, 0);
        TRY(hit);
        if (hit) {
            float m[16];
            em_crate_sdk_identity(m);
            em_crate_sdk_rotate(m, m, 0x1.921fb6p+0f, 0);
            em_crate_sdk_translate(m, m, d->position);
            m[13] = add(m[13], 0x1.99999ap-3f);
            TRY(h->effect_matrix(h->context, 4, m));
        }
    }
    TRY(h->draw(h->context));
    float delta[4];
    for (int i = 0; i < 4; ++i) delta[i] = sub(in->player[i], d->position[i]);
    delta[3] = 0.0f;
    float distance = em_crate_sdk_dot3(delta, delta);
    if (distance <= mul(50.0f, 50.0f)) {
        d->visible = 1;
        TRY(h->contact(h->context));
    } else {
        TRY(h->visibility(h->context, &d->visible));
    }
    return EM_DRUM_ALIVE;
}

static int flight(EmDrumOriginal *d, const EmDrumInput *in, const EmDrumOriginalHooks *h)
{
    /* 00156AF8 */
    float step = d->lift < 0.0f ? 0x1.1df46ap-6f : 0x1.1df46ap-7f;
    d->rotation[0] = em_crate_sdk_wrap(add(step, d->rotation[0]));
    float s, c;
    TRY(trig(d->heading, &s, &c));
    d->position[0] = add(d->position[0], mul(d->speed, s));
    d->position[2] = add(d->position[2], mul(d->speed, c));
    float point[3] = {add(d->position[0], mul(7.0f, s)), add(7.0f, d->position[1]),
                      add(d->position[2], mul(7.0f, c))};
    if (!(d->speed == 0.0f)) {
        int hit = h->sweep(h->context, point, 0x80000007);
        TRY(hit);
        if (hit) d->speed = 0.0f;
    }
    d->lift = sub(d->lift, 0x1.eb851ep-5f);
    if (d->lift < -4.0f) d->lift = -4.0f;
    d->position[1] = add(d->position[1], d->lift);
    if (d->phase == 3 && d->damage) {
        if (!(d->damage & 0x2000)) return landing(d, h, 1);
        d->damage = 0;
    }
    if (!(d->lift < 0.0f)) return tail(d, h);
    EmCrateProbe p = {0, 0};
    float from[3] = {d->position[0], d->position[1], d->position[2]};
    TRY(h->probe(h->context, d->position, from, -10.0f, 0x80000007, &p));
    if (p.result) return landing(d, h, 0);
    if ((((uint32_t)in->frame + (uint32_t)(int32_t)in->dispatch_index) & 0x3F) == 0 &&
        d->position[1] < -200.0f) {      /* 001B0D80 */
        d->state = 3;
        return tail(d, h);
    }
    if (in->area == 0x15 && d->position[1] < 5.0f) {
        const float at[4] = {d->position[0], 10.0f, d->position[2], 1.0f};
        TRY(h->effect(h->context, 0x8000005F, at));
        TRY(h->sound3d(h->context, 0xDB, 0, 800.0f));
        d->damage = 0; d->state = 3; d->phase = 0;
    }
    return tail(d, h);
}

static int broken(EmDrumOriginal *d, const EmDrumInput *in, const EmDrumOriginalHooks *h)
{
    switch (d->phase) {
    case 0: {                            /* 00156890 */
        const float point[4] = {d->position[0], add(7.0f, d->position[1]), d->position[2], 1.0f};
        TRY(h->effect(h->context, 0x80000013, point));
        int small = d->model == 0x18 || d->model == 0x2A;
        TRY(h->effect(h->context, small ? 0x8000001C : 0x8000002E, point));
        TRY(h->sound(h->context, small ? 0x1A1 : 0x19F));
        d->timer = 2;
        d->phase = (uint8_t)(d->phase + 1);
        uint32_t r;
        if (d->model == 0xA) {
            TRY(h->random(h->context, &r));
            float v = divide(mul(0x1.921fb6p+2f, cvt((int32_t)(r & 0xF0))), 256.0f);
            d->heading = em_crate_sdk_wrap(v);
        } else if (d->model == 0xC) {
            TRY(h->random(h->context, &r));
            float v = divide(mul(0x1.921fb6p+1f, cvt((int32_t)(r & 0x1F))), 180.0f);
            d->heading = em_crate_sdk_wrap(add(d->rotation[1], v));
        }
        TRY(h->random(h->context, &r));
        d->speed = in->speed_table[(r & 0x300) >> 8];
        TRY(h->random(h->context, &r));
        d->lift = in->lift_table[(r & 0x300) >> 8];
        return tail(d, h);
    }
    case 1:                              /* 00156A60 */
        d->timer = (int16_t)(d->timer - 1);
        if (d->timer) return tail(d, h);
        d->rotation[1] = d->heading;
        if (d->model == 0x18 || d->model == 0x2A) {
            d->state = 3; d->phase = 0;
        } else {
            d->phase = (uint8_t)(d->phase + 1);
            d->timer = 8;
        }
        return tail(d, h);
    case 2:                              /* 00156AC8 */
        if (d->timer) {
            d->timer = (int16_t)(d->timer - 1);
        } else {
            d->phase = 3; d->status = 1; d->damage = 0; d->health = 1;
        }
        return flight(d, in, h);
    case 3:
        return flight(d, in, h);
    default:
        return tail(d, h);
    }
}

int em_drum_original_tick(EmDrumOriginal *d, const EmDrumInput *in,
                          const EmDrumOriginalHooks *h)
{
    if (!d || !in || !complete(h)) return EM_DRUM_FAULT;
    switch (d->state) {
    case 0: {                            /* 0015666C: 001B0FD0 then INIT */
        int busy = h->allocate_model(h->context);
        TRY(busy);
        if (busy) return EM_DRUM_ALIVE;
        TRY(h->bone_init(h->context));
        d->state = (uint8_t)(d->state + 1);
        d->health = 1; d->status = 1;
        memcpy(d->origin, d->position, sizeof d->origin);
        memcpy(d->origin_rotation, d->rotation, sizeof d->origin_rotation);
        TRY(h->place(h->context, d->world));
        return EM_DRUM_ALIVE;
    }
    case 1: return armed(d, in, h);
    case 2: return broken(d, in, h);
    case 3:
        TRY(h->free(h->context));
        return EM_DRUM_FREED;
    default:
        return EM_DRUM_ALIVE;            /* no arm: returns untouched */
    }
}
