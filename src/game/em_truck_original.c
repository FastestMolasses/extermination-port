#include "game/em_truck_original.h"
#include "game/em_effect_color.h"
#include "game/em_pose_math.h" /* EE add.s/sub.s single-guard-bit model */

#include <math.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

/* Every constant is spelled by the bit pattern the overlay loads. */
static float f32(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

/* EE FPU add.s/sub.s keep one guard bit (pose_add, as for the fan owner;
 * the PCSX2 capture of this set piece differs by one ulp from plain
 * truncation on the arm tick). mul.s truncates, div.s rounds to nearest.
 * VU0 macro add/sub/mul truncate. */
static float add(float a, float b) { return pose_add(a, b); }
static float sub(float a, float b) { return pose_sub(a, b); }
static float mul(float a, float b) { return pose_mul(a, b); }
static float divide(float a, float b) { return pose_div(a, b); }
static float vu_add(float a, float b) { return em_effect_float32((double)a + b); }
static float vu_sub(float a, float b) { return em_effect_float32((double)a - b); }
static float vu_mul(float a, float b) { return em_effect_float32((double)a * b); }

/* 001029E8 at t = pi/2 - |angle| (00102B08/00102A60 add or subtract pi/2
 * 0x3FC90FDB on the EE FPU). Lanes {c9,c7,c5,c3} are D_00241100. */
static void sdk_sine(float angle, float *sine, float *cosine)
{
    static const uint32_t coefficient[4] = {0x362E9C14u, 0xB94FB21Fu, 0x3C08873Eu, 0xBE2AAAA4u};
    int negative = angle < 0.0f;
    float t = negative ? add(f32(0x3FC90FDBu), angle) : sub(f32(0x3FC90FDBu), angle);
    float u = vu_mul(t, t), term[4];
    for (unsigned i = 0; i < 4; ++i) term[i] = vu_mul(f32(coefficient[i]), t);
    for (unsigned lanes = 4; lanes > 0; --lanes)
        for (unsigned i = 0; i < lanes; ++i) term[i] = vu_mul(term[i], u);
    float s = vu_add(0.0f, t);
    for (unsigned i = 4; i > 0; --i) s = vu_add(s, term[i-1]);
    /* vsqrt takes the magnitude of 1 - s*s, so a sum slightly above 1
     * still yields a nonzero other component. */
    float q = em_effect_float32(sqrt(fabs((double)vu_sub(1.0f, vu_mul(s, s)))));
    float other = vu_add(0.0f, q);
    *sine = negative ? vu_sub(0.0f, other) : vu_add(0.0f, other);
    *cosine = s;
}

/* VU0 vmulax/vmadday/vmaddaz/vmaddw over each source column. */
static void transform(float dst[16], const float rotation[16], const float src[16])
{
    float out[16];
    for (unsigned column = 0; column < 4; ++column) {
        const float *v = src + column*4;
        for (unsigned lane = 0; lane < 4; ++lane) {
            float acc = vu_mul(rotation[lane], v[0]);
            acc = vu_add(acc, vu_mul(rotation[4+lane], v[1]));
            acc = vu_add(acc, vu_mul(rotation[8+lane], v[2]));
            out[column*4+lane] = vu_add(acc, vu_mul(rotation[12+lane], v[3]));
        }
    }
    memcpy(dst, out, sizeof out);
}

void em_truck_rotate_x(float dst[16], const float src[16], float angle)
{
    float s, c, r[16] = {0};
    sdk_sine(angle, &s, &c);
    r[0] = vu_add(0.0f, 1.0f); r[15] = vu_add(0.0f, 1.0f);
    r[5] = vu_add(0.0f, c); r[6] = vu_add(0.0f, s);
    r[9] = vu_sub(0.0f, s); r[10] = vu_add(0.0f, c);
    transform(dst, r, src);
}

void em_truck_rotate_z(float dst[16], const float src[16], float angle)
{
    float s, c, r[16] = {0};
    sdk_sine(angle, &s, &c);
    r[0] = vu_add(0.0f, c); r[1] = vu_add(0.0f, s);
    r[4] = vu_sub(0.0f, s); r[5] = vu_add(0.0f, c);
    r[10] = 1.0f; r[15] = 1.0f;
    transform(dst, r, src);
}

int em_truck_trigger_bands(float x, float z)
{
    /* 008252xx: 312 < x < 336 and 413 < z < 427, else 319 < x < 336 and
     * 390 < z < 427 (both written as the original's c.le/c.lt tests). */
    if (!(x <= f32(0x439C0000u)) && x < f32(0x43A80000u) &&
        !(z <= f32(0x43CE8000u)) && z < f32(0x43D58000u)) return 1;
    return !(x <= f32(0x439F8000u)) && x < f32(0x43A80000u) &&
           !(z <= f32(0x43C30000u)) && z < f32(0x43D58000u);
}

static int world_valid(const EmTruckWorld *w)
{
    return w && w->story && w->player_phase && w->player_0a && w->player_a0 &&
           w->player_b0 && w->carry;
}

/* D_008104C4 != 0, D_008102BA != 0 and ground +0x0D == 9. */
static int standing(const EmTruckWorld *w)
{
    return w->ground_kind && *w->player_0a && *w->ground_kind == 9;
}

int em_truck_trigger_tick(EmTruckTrigger *t, EmTruckWorld *w, const EmTruckTriggerHooks *h)
{
    if (!t || !world_valid(w) || !h || !h->script_start || !h->script_tick || !h->free_owner)
        return -1;
    if (t->freed) return 0;
    switch (t->state) {
    case 0: /* 008251E8 */
        t->state = *w->story ? 3 : 4;
        return 1;
    case 4: /* 00825248 */
        if (*w->story) { t->state = 3; return 1; }
        if (!em_truck_trigger_bands(w->player_a0[0], w->player_a0[2]) || *w->player_phase >= 2)
            return 1;
        t->armed = 4;
        if (h->script_start(h->context, EM_TRUCK_CAMERA_SCRIPT) != 1) return -1;
        t->state = 1;
        return 1;
    case 1: { /* 008253A0 */
        int done = -1;
        if (h->script_tick(h->context, &done) != 1 || (done != 0 && done != 1)) return -1;
        if (done) { *w->story = 1; t->state = 3; }
        return 1;
    }
    default: /* state 3 and every unlisted state free the node */
        if (h->free_owner(h->context) != 1) return -1;
        t->freed = 1;
        return 0;
    }
}

static int effect(const EmTruckHooks *h, uint32_t x, float y, uint32_t z)
{
    const float position[4] = {f32(x), y, f32(z), 1.0f};
    return h->effect(h->context, EM_TRUCK_EFFECT_ID, position) == 1;
}

/* Beat effects: the Y lane is 20 + current +0xB4. */
static int beat_effect(const EmTruckHooks *h, const EmTruckOriginal *o, uint32_t x, uint32_t z)
{
    return effect(h, x, add(f32(0x41A00000u), o->position[1]), z);
}

static int tail(EmTruckOriginal *o, const EmTruckHooks *h)
{
    return h->pose(h->context, o->matrix) == 1 && h->publish(h->context) == 1 &&
           h->draw(h->context) == 1;
}

/* State 1 beats (00824520..00825000). */
static int fall_beat(EmTruckOriginal *o, EmTruckWorld *w, const EmTruckHooks *h)
{
    static const uint32_t slow[2] = {0xBD088889u, 0xBD088889u};
    static const uint32_t fast[2] = {0xBE088889u, 0xBF2AAAABu};
    const int f = o->frame;
    const uint32_t *velocity;
    const uint32_t A = 0x43A9D99Au, B = 0x43B5D99Au, C = 0x43C5D99Au, Z1 = 0x43C44CCDu,
                   Z2 = 0x43BBCCCDu, Z3 = 0x43BA4CCDu;
    int ok = 1;
    if (f >= 119) { /* 00824FF0 */
        *w->story = 0xFF;
        o->state = 2;
        o->velocity[0] = o->velocity[1] = o->velocity[2] = 0.0f;
        return 1;
    }
    if (f < 10) {
        if (f == 8)
            ok = h->sound(h->context, EM_TRUCK_SOUND_FALL_START, f32(0x43960000u)) == 1 &&
                 beat_effect(h, o, 0x43C70000u, Z3) && beat_effect(h, o, C, Z1);
        velocity = slow;
    } else if (f < 15) {
        if (standing(w) && f == 14) ok = h->rumble(h->context, 2) == 1;
        velocity = fast;
    } else if (f < 30) {
        if (f == 28)
            ok = beat_effect(h, o, 0x43C70000u, Z3) && beat_effect(h, o, 0x43C58000u, Z1) &&
                 beat_effect(h, o, A, Z1) && beat_effect(h, o, B, Z2);
        velocity = slow;
    } else if (f < 42) {
        if (f == 40) ok = beat_effect(h, o, A, Z1) && beat_effect(h, o, B, Z2);
        velocity = fast;
    } else if (f < 52) {
        if (f == 50) ok = beat_effect(h, o, A, Z1) && beat_effect(h, o, C, Z1);
        velocity = slow;
    } else if (f < 65) {
        if (f == 64) ok = beat_effect(h, o, A, Z1) && beat_effect(h, o, C, Z1);
        velocity = fast;
    } else if (f < 90) {
        if (f == 88)
            ok = beat_effect(h, o, 0x43C38000u, Z3) && beat_effect(h, o, C, Z1) &&
                 beat_effect(h, o, A, Z1) && beat_effect(h, o, B, Z2);
        if (ok && standing(w) && f == 87) ok = h->rumble(h->context, 2) == 1;
        velocity = slow;
    } else {
        if (f == 110)
            ok = h->sound(h->context, EM_TRUCK_SOUND_FALL_END, f32(0x43960000u)) == 1 &&
                 beat_effect(h, o, 0x43C7599Au, Z3) && beat_effect(h, o, C, Z1) &&
                 beat_effect(h, o, A, Z1) && beat_effect(h, o, B, Z2);
        velocity = fast;
    }
    if (!ok) return 0;
    em_truck_rotate_x(o->matrix, o->matrix, 0.0f);
    em_truck_rotate_z(o->matrix, o->matrix, f32(0xBB449BA6u));
    o->velocity[0] = f32(velocity[0]);
    o->velocity[1] = f32(velocity[1]);
    o->velocity[2] = 0.0f;
    return 1;
}

static int falling(EmTruckOriginal *o, EmTruckWorld *w, const EmTruckHooks *h)
{
    float bounds[6];
    if (!fall_beat(o, w, h)) return -1;
    /* 00825014: player +0xB0 footprint against the hull AABB (x: +0/+0xC,
     * z: +8/+0x14); inside adds only the z velocity to player +0xA8. */
    if (h->hull_bounds(h->context, bounds) != 1) return -1;
    const float x = w->player_b0[0], z = w->player_b0[2];
    if (!(x <= bounds[0]) && x < bounds[3] && !(z <= bounds[2]) && z < bounds[5]) {
        w->player_a0[2] = add(w->player_a0[2], o->velocity[2]);
        *w->carry = 1;
    }
    for (unsigned i = 0; i < 3; ++i) o->position[i] = add(o->position[i], o->velocity[i]);
    o->frame = (int16_t)(o->frame + 1);
    if (!(o->frame & 3)) o->jitter_x = f32(0x3F000000u);
    if ((o->frame & 15) == 4) o->jitter_z = f32(0x3E4CCCCDu);
    o->matrix[12] = add(o->position[0], o->jitter_x);
    o->matrix[13] = o->position[1];
    o->matrix[14] = add(o->position[2], o->jitter_z);
    if (h->pose(h->context, o->matrix) != 1 || h->hull(h->context, o->matrix) != 1 ||
        h->publish(h->context) != 1 || h->draw(h->context) != 1) return -1;
    return 1;
}

/* State 4 shake cases (jump table 0x82ABC0 on shake & 15, counts < 47). */
static int shake_case(EmTruckOriginal *o, const EmTruckHooks *h)
{
    switch (o->shake & 15) {
    case 0:
        em_truck_rotate_x(o->matrix, o->rest_matrix, f32(0xBB03126Fu));
        o->position[1] = sub(o->rest_y, mul(f32(0x3D23D70Au), (float)(o->shake - 1)));
        return 1;
    case 3:
        memcpy(o->matrix, o->rest_matrix, sizeof o->matrix);
        return 1;
    case 4:
        em_truck_rotate_x(o->matrix, o->rest_matrix, f32(0x3A03126Fu));
        return effect(h, 0x43C7599Au, f32(0x432ECCCDu), 0x43BA4CCDu) &&
               effect(h, 0x43B5D99Au, f32(0x4322CCCDu), 0x43BBCCCDu);
    case 5: case 7:
        em_truck_rotate_x(o->matrix, o->rest_matrix, f32(0x3AC49BA6u));
        return 1;
    case 6:
        em_truck_rotate_x(o->matrix, o->rest_matrix, f32(0x3B23D70Au));
        return effect(h, 0x43C5D99Au, f32(0x4323CCCDu), 0x43C44CCDu) &&
               effect(h, 0x43A9D99Au, f32(0x432FCCCDu), 0x43C44CCDu);
    default: /* 1, 2 and 8..15 */
        return 1;
    }
}

static int wedged(EmTruckOriginal *o, EmTruckWorld *w, const EmTruckHooks *h)
{
    if (o->shake > 0) { /* 0082424C */
        ++o->shake;
        if (o->shake >= 47) {
            o->state = 1;
            memcpy(o->matrix, o->rest_matrix, sizeof o->matrix);
            o->position[1] = sub(o->rest_y, f32(0x3FE66666u));
            o->jitter_z = 0.0f;
            o->jitter_x = 0.0f;
            return tail(o, h) ? 1 : -1;
        }
        if (!shake_case(o, h)) return -1;
        o->matrix[13] = o->position[1];
        if (h->hull(h->context, o->matrix) != 1) return -1;
        return tail(o, h) ? 1 : -1;
    }
    if (standing(w)) { /* 0082418C: arm */
        if (h->rumble(h->context, 0) != 1) return -1;
        ++o->shake;
        float offset = divide((float)(o->shake % 20 - 10), f32(0x42480000u));
        o->matrix[13] = add(o->position[1], offset);
        offset = divide(offset, f32(0x40000000u));
        o->matrix[12] = add(o->position[0], offset);
        o->matrix[14] = sub(o->position[2], offset);
        if (h->hull(h->context, o->matrix) != 1) return -1;
    }
    return tail(o, h) ? 1 : -1;
}

static int initialize(EmTruckOriginal *o, EmTruckWorld *w, const EmTruckHooks *h)
{
    int pending = -1;
    if (h->model_bind(h->context, &pending) != 1 || (pending != 0 && pending != 1)) return -1;
    if (pending) return 1;
    if (*w->story == 0xFF) { /* 00824068: the persisted fallen pose */
        static const uint32_t fallen[16] = {
            0, 0, 0xBF800000u, 0, 0x3D20D994u, 0x3F7FBE77u, 0, 0,
            0x3F7FBE77u, 0xBD20D994u, 0, 0, 0x43B991ECu, 0x4302B0A4u, 0x43C39439u, 0x3F800000u};
        for (unsigned i = 0; i < 16; ++i) o->matrix[i] = f32(fallen[i]);
        o->state = 2;
        return h->pose(h->context, o->matrix) == 1 && h->hull(h->context, o->matrix) == 1 ? 1 : -1;
    }
    if (h->placement_matrix(h->context, o->matrix) != 1) return -1;
    o->rest_rotation_x = o->rotation_x;
    o->rest_y = o->position[1];
    o->state = 4;
    o->shake = 0;
    memcpy(o->rest_matrix, o->matrix, sizeof o->matrix);
    return h->hull(h->context, o->matrix) == 1 ? 1 : -1;
}

int em_truck_original_tick(EmTruckOriginal *o, EmTruckWorld *w, const EmTruckHooks *h)
{
    if (!o || !world_valid(w) || !h || !h->model_bind || !h->placement_matrix || !h->pose ||
        !h->hull || !h->hull_bounds || !h->publish || !h->draw || !h->rumble || !h->effect ||
        !h->sound || !h->free_owner) return -1;
    if (o->freed) return 0;
    switch (o->state) {
    case 0: return initialize(o, w, h);
    case 1: return falling(o, w, h);
    case 2: return h->publish(h->context) == 1 && h->draw(h->context) == 1 ? 1 : -1;
    case 4: return wedged(o, w, h);
    default: /* state 3 and every unlisted state */
        if (h->free_owner(h->context) != 1) return -1;
        o->freed = 1;
        return 0;
    }
}
