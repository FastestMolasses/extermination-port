#include "game/em_crate_original.h"
#include "game/em_effect_color.h"

#include <math.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

/* ---- SDK helpers: EE products/sums truncate, division rounds. ---- */

float em_crate_sdk_add(float a, float b) { return em_effect_float32((double)a + b); }
float em_crate_sdk_sub(float a, float b) { return em_effect_float32((double)a - b); }
float em_crate_sdk_mul(float a, float b) { return em_effect_float32((double)a * b); }
float em_crate_sdk_int_to_float(int32_t v) { return em_effect_float32((double)v); }

int32_t em_crate_sdk_float_to_int(float value)
{
    /* float_to_int via the 001278C0 unpack: NaN/zero/denormal -> 0,
     * |x| >= 2^31 or inf saturates by sign, otherwise truncates. */
    if (isnan(value)) return 0;
    if (fabsf(value) >= 2147483648.0f) return value < 0 ? INT32_MIN : INT32_MAX;
    return (int32_t)value;
}

float em_crate_sdk_wrap(float angle)
{
    const float pi = 3.14159274101257324f, two_pi = 6.28318548202514648f;
    if (!(angle <= pi))
        do angle = em_crate_sdk_sub(angle, two_pi); while (!(angle <= pi));
    while (angle <= -pi) angle = em_crate_sdk_add(angle, two_pi);
    return angle;
}

void em_crate_sdk_identity(float m[16])
{
    memset(m, 0, 16 * sizeof *m);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* 001029E8 with the caller's pi/2-|angle| argument: (sin, cos). */
static void sine_cosine(float angle, float *sine, float *cosine)
{
    static const float c[4] = { 0x1.5d3828p-19f, -0x1.9f643ep-13f,
                                0x1.110e7cp-7f, -0x1.555548p-3f };
    const float half_pi = 1.57079637050628662f;
    int negative = angle < 0.0f;
    float t = negative ? em_crate_sdk_add(half_pi, angle) : em_crate_sdk_sub(half_pi, angle);
    float u = em_crate_sdk_mul(t, t), term[4];
    for (unsigned i = 0; i < 4; ++i) term[i] = em_crate_sdk_mul(c[i], t);
    for (unsigned lanes = 4; lanes > 0; --lanes)
        for (unsigned i = 0; i < lanes; ++i) term[i] = em_crate_sdk_mul(term[i], u);
    float s = em_crate_sdk_add(0.0f, t);
    for (unsigned i = 4; i > 0; --i) s = em_crate_sdk_add(s, term[i-1]);
    float q = em_effect_float32(sqrt(fabs((double)em_crate_sdk_sub(1.0f, em_crate_sdk_mul(s, s)))));
    q = em_crate_sdk_add(0.0f, q);
    *sine = negative ? em_crate_sdk_sub(0.0f, q) : em_crate_sdk_add(0.0f, q);
    *cosine = em_crate_sdk_add(0.0f, s);
}

/* One VU row: ACC = m0*x; ACC += m1*y; ACC += m2*z; out = ACC + m3*w. */
static void chain(float out[4], const float m[16], const float v[4])
{
    float r[4];
    for (unsigned lane = 0; lane < 4; ++lane) {
        float acc = em_crate_sdk_mul(m[lane], v[0]);
        acc = em_crate_sdk_add(acc, em_crate_sdk_mul(m[4+lane], v[1]));
        acc = em_crate_sdk_add(acc, em_crate_sdk_mul(m[8+lane], v[2]));
        r[lane] = em_crate_sdk_add(acc, em_crate_sdk_mul(m[12+lane], v[3]));
    }
    memcpy(out, r, sizeof r);
}

void em_crate_sdk_multiply(float out[16], const float left[16], const float right[16])
{
    float l[16];
    memcpy(l, left, sizeof l);           /* 001026D0 loads a1 before any store */
    for (unsigned row = 0; row < 4; ++row) {
        float v[4];
        memcpy(v, right + row*4, sizeof v);
        chain(out + row*4, l, v);
    }
}

void em_crate_sdk_apply(float out[4], const float m[16], const float v[4])
{
    float in[4];
    memcpy(in, v, sizeof in);
    chain(out, m, in);
}

void em_crate_sdk_rotate(float out[16], const float in[16], float angle, int axis)
{
    float s, c, r[16] = {0};
    sine_cosine(angle, &s, &c);
    float ns = em_crate_sdk_sub(0.0f, s), one = em_crate_sdk_add(0.0f, 1.0f);
    if (axis == 0) {        /* 00102B08 */
        r[0] = one; r[5] = c; r[6] = s; r[9] = ns; r[10] = c;
    } else if (axis == 1) { /* 00102BB0 */
        r[0] = c; r[2] = ns; r[5] = one; r[8] = s; r[10] = c;
    } else {                /* 00102A60 */
        r[0] = c; r[1] = s; r[4] = ns; r[5] = c; r[10] = 1.0f;
    }
    r[15] = axis == 2 ? 1.0f : one;
    em_crate_sdk_multiply(out, r, in);
}

void em_crate_sdk_euler(float m[16], const float angles[3])
{
    float a[3];
    memcpy(a, angles, sizeof a);         /* 00102C58: Z, then Y, then X */
    em_crate_sdk_rotate(m, m, a[2], 2);
    em_crate_sdk_rotate(m, m, a[1], 1);
    em_crate_sdk_rotate(m, m, a[0], 0);
}

void em_crate_sdk_translate(float out[16], const float in[16], const float v[3])
{
    float t[3];
    for (unsigned i = 0; i < 3; ++i) t[i] = em_crate_sdk_add(in[12+i], v[i]);
    if (out != in) memcpy(out, in, 16 * sizeof *out);
    memcpy(out + 12, t, sizeof t);
}

float em_crate_sdk_dot3(const float a[4], const float b[4])
{
    float x = em_crate_sdk_mul(a[0], b[0]), y = em_crate_sdk_mul(a[1], b[1]);
    float z = em_crate_sdk_mul(a[2], b[2]);
    return em_crate_sdk_add(em_crate_sdk_add(x, y), z);
}

/* ---- the +0x238/+0x23C ints overlaid on step[2]/step[3] ---- */

int32_t em_crate_original_rattle_wait(const EmCrateOriginal *crate)
{
    int32_t v; memcpy(&v, &crate->step[2], sizeof v); return v;
}

int32_t em_crate_original_rattle_row(const EmCrateOriginal *crate)
{
    int32_t v; memcpy(&v, &crate->step[3], sizeof v); return v;
}

void em_crate_original_set_rattle(EmCrateOriginal *crate, int32_t wait, int32_t row)
{
    memcpy(&crate->step[2], &wait, sizeof wait);
    memcpy(&crate->step[3], &row, sizeof row);
}

/* ---- owner ---- */

#define TRY(expr) do { if ((expr) < 0) return EM_CRATE_FAULT; } while (0)

static int complete(const EmCrateOriginalHooks *h)
{
    return h && h->allocate_model && h->bone_init && h->publish && h->place &&
        h->probe && h->random && h->sound && h->sound3d && h->effect &&
        h->taken && h->spawn && h->rebind && h->bone_matrix && h->draw &&
        h->set_taken && h->free;
}

static int16_t s16le(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static uint16_t u16le(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float f32le(const uint8_t *p) { uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; float f; memcpy(&f, &v, 4); return f; }

/* 001553E4 / 00155FE0: base = D_0024A850[area] (0 -> 1), then
 * D_0024D820[area][base + link]. */
static const uint8_t *group(const EmCrateOriginal *c, const EmCrateInput *in)
{
    const EmCrateRegistry *r = in->registry;
    if (!r || !r->first_group || !r->groups || in->area >= r->area_count ||
        !r->groups[in->area]) return NULL;
    int32_t base = r->first_group[in->area];
    if (!base) base = 1;
    int32_t index = base + c->link;
    if (index < 0) return NULL;
    return r->groups[in->area][index];
}

static int probe(EmCrateOriginal *c, const EmCrateOriginalHooks *h, const float from[4],
                 float dy, uint32_t mode, EmCrateProbe *out)
{
    float point[3] = {from[0], from[1], from[2]};
    out->result = out->actor = 0;
    return h->probe(h->context, c->position, point, dy, mode, out);
}

static int tail(EmCrateOriginal *c, const EmCrateOriginalHooks *h)
{
    /* copy_qw4(*D_00275B40 + 0x90, +0xD0), then the +0x4C hook. */
    TRY(h->bone_matrix(h->context, c->world));
    TRY(h->draw(h->context));
    return EM_CRATE_ALIVE;
}

static int initialise(EmCrateOriginal *c, const EmCrateInput *in, const EmCrateOriginalHooks *h)
{
    /* 001B0FD0 */
    int busy = h->allocate_model(h->context);
    TRY(busy);
    if (busy) return EM_CRATE_ALIVE;
    TRY(h->bone_init(h->context));
    c->state = (uint8_t)(c->state + 1);
    /* 00155230 */
    c->state = 4; c->alarm = 0; c->status = 1; c->health = 1;
    TRY(h->publish(h->context));
    TRY(h->place(h->context, c->world));
    memcpy(c->rest, c->world, sizeof c->rest);
    c->velocity[1] = 0.0f;
    float radius = c->model == 0x1F || c->model == 0x50 || c->model == 0x1C ?
        0x1.0f876cp+1f : 0x1.26280cp+2f; /* 0x4007C3B6 / 0x40931406 */
    float v[4] = {radius, 0.0f, radius, 0.0f};
    em_crate_sdk_apply(v, c->world, v);
    float x = c->position[0], z = c->position[2];
    c->probe[7] = em_crate_sdk_add(x, v[0]); c->probe[3] = em_crate_sdk_add(z, v[2]);
    c->probe[6] = em_crate_sdk_sub(x, v[2]); c->probe[2] = em_crate_sdk_add(z, v[0]);
    c->probe[5] = em_crate_sdk_sub(x, v[0]); c->probe[1] = em_crate_sdk_sub(z, v[2]);
    c->probe[4] = em_crate_sdk_add(x, v[2]); c->probe[0] = em_crate_sdk_sub(z, v[0]);
    if (c->placement & 1) {
        int owed;
        if (c->link >= 0) {
            const uint8_t *record = group(c, in);
            if (!record) return EM_CRATE_FAULT;
            owed = 0;
            for (; s16le(record) != -1; record += 0x2C) {
                int taken = h->taken(h->context, record[2]);
                TRY(taken);
                if (!taken) ++owed;
            }
        } else {
            owed = in->dispatch_has_next;   /* uninitialised s0 = node->next */
        }
        if (owed) em_crate_original_set_rattle(c, -1, 0);
        else c->placement &= 0xFFFE;
    }
    float from[4] = {c->position[0], em_crate_sdk_sub(c->position[1], 2.0f), c->position[2], 0};
    EmCrateProbe p;
    TRY(probe(c, h, from, -3.0f, 7, &p));
    c->raised = p.result == 4 ? 0 : 1;
    return EM_CRATE_ALIVE;
}

static int rest(EmCrateOriginal *c, const EmCrateInput *in, const EmCrateOriginalHooks *h)
{
    if (c->damage) {                     /* 00155504 */
        c->status = 2; c->state = 2;
        for (size_t i = 0; i < in->actor_count; ++i) {
            const EmCrateListEntry *e = &in->actors[i];
            uint8_t m = e->model;
            if (e->raised && (m == 6 || m == 0x1C || m == 0x1E || m == 0x1F || m == 0x50))
                *e->alarm = 1;
        }
        c->alarm = 0;
    }
    if (c->alarm) { c->alarm = 0; c->state = 1; c->timer = 6; }
    if (c->placement & 1) {
        int32_t wait = em_crate_original_rattle_wait(c);
        if (wait < 0) {                  /* 001556D8 */
            uint32_t r;
            TRY(h->sound3d(h->context, 0x19C, 0, 300.0f));
            TRY(h->random(h->context, &r));
            int32_t next = (int32_t)((uint32_t)((int32_t)r >> 16) << 8) >> 15;
            em_crate_original_set_rattle(c, next, em_crate_original_rattle_row(c));
            TRY(h->random(h->context, &r));
            int32_t high = (int32_t)r >> 16;
            int32_t row = (int32_t)((uint32_t)high * 7u) >> 15;
            em_crate_original_set_rattle(c, next, row);
        } else {
            if (wait < 9 && (wait & 1)) {
                int32_t row = em_crate_original_rattle_row(c), column = (wait - 1) >> 1;
                if (!in->rattle || row < 0 || (size_t)row >= in->rattle_rows || column > 3)
                    return EM_CRATE_FAULT;
                const float *cell = in->rattle[row][column];
                em_crate_sdk_rotate(c->world, c->rest, cell[0], 1);
                c->world[12] = em_crate_sdk_add(c->rest[12], cell[1]);
                c->world[14] = em_crate_sdk_add(c->rest[14], cell[2]);
            }
            if (em_crate_original_rattle_wait(c) == 0) memcpy(c->world, c->rest, sizeof c->world);
            em_crate_original_set_rattle(c, em_crate_original_rattle_wait(c) - 1,
                                         em_crate_original_rattle_row(c));
        }
    }
    TRY(tail(c, h));
    TRY(h->publish(h->context));
    return EM_CRATE_ALIVE;
}

static int corners(EmCrateOriginal *c, const EmCrateOriginalHooks *h, float y, float dy,
                   uint32_t mode, int solid_only, int hit[4])
{
    int count = 0;
    float from[4] = {0, y, 0, c->position[3]};
    for (int i = 0; i < 4; ++i) {
        EmCrateProbe p;
        from[0] = c->probe[7-i]; from[2] = c->probe[3-i];
        TRY(probe(c, h, from, dy, mode, &p));
        hit[i] = solid_only ? p.result == 2 && p.actor != 0 : p.result != 0;
        count += hit[i];
    }
    return count;
}

static int hold(EmCrateOriginal *c, const EmCrateOriginalHooks *h)
{
    if (!c->timer) c->state = 4;         /* 00155950 */
    TRY(h->publish(h->context));
    return tail(c, h);
}

static float centred(uint32_t r, float scale)
{
    /* (float)r / 2^31 - 0.5, then / scale (00155C04). */
    float v = em_crate_sdk_int_to_float((int32_t)r) / 2147483648.0f;
    return em_crate_sdk_sub(v, 0.5f) / scale;
}

static int fall(EmCrateOriginal *c, const EmCrateOriginalHooks *h)
{
    const float lean = 0x1.acee9ep-5f;     /* 0x3D56774F */
    if (c->fall_phase == 0) {
        int hit[4];
        c->timer = (int16_t)(c->timer - 1);
        int count = corners(c, h, em_crate_sdk_sub(c->position[1], 1.0f), -2.0f, 7, 1, hit);
        TRY(count);
        if (count >= 3 || (hit[0] && hit[2]) || (hit[1] && hit[3])) return hold(c, h);
        float from[4] = {c->position[0], em_crate_sdk_sub(c->position[1], 2.0f),
                         c->position[2], c->position[3]};
        EmCrateProbe p;
        TRY(probe(c, h, from, -3.0f, 7, &p));
        if (p.result == 2 && p.actor) return hold(c, h);
        em_crate_sdk_identity(c->step);
        c->tilt = 0x1E;
        c->spin[0] = c->spin[1] = c->spin[2] = 0.0f;
        int axis = -1; float angle = 0;
        if (count == 2) {
            if (hit[0] && hit[1]) axis = 0, angle = -lean;
            else if (hit[1] && hit[2]) axis = 2, angle = -lean;
            else if (hit[2] && hit[3]) axis = 0, angle = lean;
            else if (hit[3] && hit[0]) axis = 2, angle = lean;
        } else if (count == 1) {
            if (hit[0]) axis = 0, angle = -lean;
            else if (hit[1]) axis = 2, angle = -lean;
            else if (hit[2]) axis = 0, angle = lean;
            else axis = 2, angle = lean;
        } else {
            uint32_t r;
            c->tilt = 0;
            TRY(h->random(h->context, &r));
            c->spin[2] = em_crate_sdk_add(c->spin[2], centred(r, 60.0f));
            TRY(h->random(h->context, &r));
            c->spin[1] = em_crate_sdk_add(c->spin[1], centred(r, 50.0f));
            TRY(h->random(h->context, &r));
            c->spin[0] = em_crate_sdk_add(c->spin[0], centred(r, 60.0f));
            const float e[3] = {c->spin[2], c->spin[1], c->spin[0]};
            em_crate_sdk_euler(c->step, e);
        }
        if (axis >= 0) {
            em_crate_sdk_rotate(c->step, c->step, angle, axis);
            /* X turns store +0x2C0 (spin[2]); Z turns store +0x2B8 (spin[0]). */
            c->spin[axis == 0 ? 2 : 0] = angle;
        }
        if (c->model == 6) {
            float t = em_crate_sdk_mul(60.0f, c->position[1]) / 12.0f;
            c->timer = (int16_t)(uint16_t)em_crate_sdk_float_to_int(t);
        } else {
            c->timer = 0xB4;
        }
        c->fall_phase = (uint8_t)(c->fall_phase + 1);
    } else if (c->fall_phase != 1) {
        return tail(c, h);
    }
    /* 00155D64 */
    c->timer = (int16_t)(c->timer - 1);
    if (c->tilt >= 2) {
        c->tilt = (int16_t)(c->tilt - 1);
        c->position[1] = em_crate_sdk_add(c->position[1], c->velocity[1]);
        if (c->tilt < 2) {
            em_crate_sdk_identity(c->step);
            c->velocity[2] = em_crate_sdk_mul(11.0f, -c->spin[0]);   /* neg.s */
            c->velocity[0] = em_crate_sdk_mul(11.0f, c->spin[2]);
            c->spin[2] = c->spin[2] / 0x1.666666p+0f;
            c->spin[0] = c->spin[0] / 0x1.666666p+0f;
            const float e[3] = {c->spin[2], c->spin[1], c->spin[0]};
            em_crate_sdk_euler(c->step, e);
        }
    } else {
        c->position[0] = em_crate_sdk_add(c->position[0], c->velocity[2]);
        c->position[1] = em_crate_sdk_add(c->position[1], c->velocity[1]);
        c->position[2] = em_crate_sdk_add(c->position[2], c->velocity[0]);
        float dy = c->velocity[1];
        c->velocity[1] = em_crate_sdk_sub(c->velocity[1], 0x1.a9fbe8p-5f);
        float from[4];
        memcpy(from, c->position, sizeof from);
        if (c->tilt)
            from[1] = em_crate_sdk_sub(from[1], c->model == 6 ? 9.0f : 4.5f);
        EmCrateProbe p;
        TRY(probe(c, h, from, dy, 7, &p));
        if (p.result == 4 || c->timer < 0) {
            c->damage = 0; c->timer = 0; c->state = 2;
        }
    }
    em_crate_sdk_multiply(c->world, c->world, c->step);
    memcpy(c->world + 12, c->position, 3 * sizeof(float));
    return tail(c, h);
}

static int spawn_group(EmCrateOriginal *c, const EmCrateInput *in, const EmCrateOriginalHooks *h)
{
    const uint8_t *record = group(c, in);
    if (!record) return EM_CRATE_FAULT;
    for (; s16le(record) != -1; record += 0x2C) {
        int taken = h->taken(h->context, record[2]);
        TRY(taken);
        if (taken) continue;
        EmCrateChild child = {0};
        child.class_id = record[4];
        child.puid = record[2];
        child.model = record[6];
        child.model_high = (uint16_t)((s16le(record + 6) >> 8) & 0xFF);
        child.param = record[8];
        child.class_two = (s16le(record + 4) & ~0xE0) == 2;
        if (child.class_two) { child.sub_area = in->sub_area; child.condition = record[0xA]; }
        else child.placement = u16le(record + 0xA);
        child.kind = s16le(record + 0xC);
        child.link = s16le(record + 0xE);
        for (int i = 0; i < 3; ++i) {
            child.position[i] = em_crate_sdk_add(c->position[i], f32le(record + 0x10 + 4*i));
            child.rotation[i] = f32le(record + 0x1C + 4*i);
        }
        child.behavior = (uint32_t)u16le(record + 0x28) | (uint32_t)u16le(record + 0x2A) << 16;
        TRY(h->spawn(h->context, &child));
    }
    return 0;
}

static int burst(EmCrateOriginal *c, const EmCrateInput *in, const EmCrateOriginalHooks *h)
{
    if (c->break_phase == 1) {           /* 001563E4 */
        int16_t timer = c->timer;
        if (!(timer == 0 && c->alarm == 0) && c->raised) {
            int hit[4];
            if (timer) c->timer = (int16_t)(c->timer - 1);
            int count = corners(c, h, em_crate_sdk_sub(c->position[1], 1.0f), -2.0f, 6, 0, hit);
            TRY(count);
            if (count < 3) c->state = 3;
            else if (!c->timer && c->alarm) c->timer = 6;
        }
        return tail(c, h);
    }
    if (c->break_phase != 0) return tail(c, h);
    c->break_phase = 1;                  /* 00155FC0 */
    c->rotation[1] = 0.0f; c->rotation[0] = 0.0f; c->timer = 6;
    if (c->link >= 0) TRY(spawn_group(c, in, h));
    static const struct { uint8_t model; uint16_t sound; uint32_t fx[2]; } gore[] = {
        {6, 0x19D, {0x8000000A, 0x80000015}}, {0x1C, 0x19E, {0x8000000B, 0x80000014}},
        {0x50, 0x19E, {0x8000000B, 0x80000014}}, {0x1E, 0x19D, {0x80000031, 0x80000015}},
        {0x1F, 0x19E, {0x80000032, 0x80000014}}};
    for (size_t i = 0; i < sizeof gore / sizeof gore[0]; ++i) {
        if (gore[i].model != c->model) continue;
        TRY(h->sound(h->context, gore[i].sound));
        TRY(h->effect(h->context, gore[i].fx[0], c->position, c->rotation));
        TRY(h->effect(h->context, gore[i].fx[1], c->position, c->rotation));
        break;
    }
    if (!c->damage || (c->model != 6 && c->model != 0x1E)) {
        c->state = 3;
        return tail(c, h);
    }
    /* 00156284: identity, 0..3 quarter turns about Y, then pre-multiply. */
    float turn[16];
    uint32_t r;
    em_crate_sdk_identity(turn);
    TRY(h->random(h->context, &r));
    int32_t quarter = (int32_t)((uint32_t)((int32_t)r >> 16) << 2) >> 15;
    if (quarter == 3) em_crate_sdk_rotate(turn, turn, 0x1.2d97c8p+2f, 1);
    else if (quarter == 2) em_crate_sdk_rotate(turn, turn, 0x1.921fb6p+1f, 1);
    else if (quarter == 1) em_crate_sdk_rotate(turn, turn, 0x1.921fb6p+0f, 1);
    float saved[4];
    memcpy(saved, c->world + 12, sizeof saved);
    c->world[14] = c->world[13] = c->world[12] = 0.0f;
    em_crate_sdk_multiply(c->world, c->world, turn);
    memcpy(c->world + 12, saved, sizeof saved);
    TRY(h->rebind(h->context, c->model == 6 ? 0x22 : 0x29));
    TRY(h->bone_init(h->context));
    return tail(c, h);
}

int em_crate_original_tick(EmCrateOriginal *c, const EmCrateInput *in,
                           const EmCrateOriginalHooks *h)
{
    if (!c || !in || !complete(h) || (in->actor_count && !in->actors)) return EM_CRATE_FAULT;
    for (size_t i = 0; i < in->actor_count; ++i)
        if (!in->actors[i].alarm) return EM_CRATE_FAULT;
    switch (c->state) {
    case 0: return initialise(c, in, h);
    case 4: return rest(c, in, h);
    case 1: return fall(c, h);
    case 2: return burst(c, in, h);
    default:                             /* 3 and every other value: 001565B4 */
        if (c->model == 0x50 && c->puid) TRY(h->set_taken(h->context, c->puid));
        TRY(h->free(h->context));
        return EM_CRATE_FREED;
    }
}
