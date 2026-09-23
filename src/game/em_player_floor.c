/* em_player_floor.c - player floor contact (see em_player_floor.h).
 *
 * Arithmetic follows the EE: single-precision results truncate toward zero
 * (em_effect_float32), as the oracle interpreters model. */
#include "game/em_player_floor.h"
#include "game/em_effect_color.h"

#include <stddef.h>

/* ---- 00187350 / 00187EE0 / 00182430 ----------------------------------- */

/* D_00248C90 is 459 rows of 12 bytes. Only these rows carry both step
 * frames; tools/test_player_footstep_reference.py checks every row of the
 * original table against this list. */
static const struct { int16_t clip, frame_a, frame_b; } kStepFrames[] = {
    {0x001, 72, 21}, {0x002, 26, 3}, {0x003, 21, 2},
    {0x00B, 80, 21}, {0x00C, 28, 5}, {0x00D, 26, 6},
    {0x015, 69, 8},  {0x016, 29, 9}, {0x017, 22, 2},
    {0x04C, 135, 24}, {0x04D, 41, 9}, {0x04E, 20, 6},
    {0x148, 35, 6},  {0x149, 35, 6}, {0x14A, 27, 7}, {0x14B, 27, 7},
};

int em_player_step_frames(int clip, int *frame_a, int *frame_b)
{
    for (size_t i = 0; i < sizeof kStepFrames / sizeof kStepFrames[0]; ++i) {
        if (kStepFrames[i].clip != clip) continue;
        if (frame_a) *frame_a = kStepFrames[i].frame_a;
        if (frame_b) *frame_b = kStepFrames[i].frame_b;
        return 1;
    }
    return 0;
}

unsigned em_player_step_sound_base(uint8_t surface, uint8_t depth, uint8_t tier)
{
    /* 00182430: one base per surface, then +10 for tier 3 and +5 for tier 2
     * (compared as the low byte of the tier argument). */
    unsigned base;
    switch (surface) {
    case 1: base = 0x21; break;
    case 2: base = 0x32; break;
    case 3: base = 0x43; break;
    case 4: base = 0x54; break;
    case 5: base = 0x65; break;
    case 6: case 7: base = 0xA9; break;
    case 8: base = 0x87; break;
    case 0xD: base = 0xDC; break;
    case 0xE: base = 0xED; break;
    case 0x5A: base = 0x76; break;
    case 0x5B: base = depth == 1 ? 0xBA : 0xCB; break; /* +23C */
    case 0x5C: base = 0x98; break;
    default: base = 0x10; break;
    }
    return base + (tier == 3 ? 0xA : tier == 2 ? 5 : 0);
}

/* 00182430(actor, tier): surface layer, then the gear layer 0x138. */
static int step_sounds(const EmPlayerStepActor *actor, uint8_t tier,
                       const EmPlayerStepWorkers *w)
{
    unsigned variant;
    if (!w->random5 || !w->sound) return -1;
    unsigned base = em_player_step_sound_base(actor->surface, actor->depth, tier);
    if (w->random5(w->context, &variant) < 0 ||
        w->sound(w->context, base + variant) < 0) return -1;
    if (w->random5(w->context, &variant) < 0 ||
        w->sound(w->context, 0x138 + variant) < 0) return -1;
    return 0;
}

static int step_effect_at(const EmPlayerStepWorkers *w, uint32_t id,
                          const float position[3], const float rotation[3])
{
    if (!w->effect) return -1;
    return w->effect(w->context, id, position, rotation) < 0 ? -1 : 0;
}

/* 00187EE0(actor, foot): the surface effect at the foot, 1.5 below it. */
static int step_effect(const EmPlayerStepActor *actor, const float foot[3],
                       const EmPlayerStepWorkers *w)
{
    float at[3] = { foot[0], em_effect_float32((double)foot[1] - 1.5f), foot[2] };
    /* 0x5A..0x5C place the effect at the recorded surface height. */
    float water[3] = { actor->position[0], actor->surface_y, actor->position[2] };
    switch (actor->surface) {
    case 0:
        if (actor->wet != 0) {
            if (!w->decal) return -1;
            return w->decal(w->context, at, actor->rotation[1], -actor->slope) < 0 ? -1 : 0;
        }
        return step_effect_at(w, 0x80000011u, at, actor->rotation);
    case 5: return step_effect_at(w, 0x80000028u, at, actor->rotation);
    case 6: return step_effect_at(w, 0x80000005u, at, actor->rotation);
    case 7: return step_effect_at(w, 0x80000068u, at, actor->rotation);
    case 8: return step_effect_at(w, 0x80000066u, at, actor->rotation);
    case 0x5A: return step_effect_at(w, 0x80000065u, water, actor->rotation);
    case 0x5B: return step_effect_at(w, 0x8000001Du, water, actor->rotation);
    case 0x5C: return step_effect_at(w, 0x80000067u, water, actor->rotation);
    default: return 0;   /* 1..4, 0xD, 0xE and unlisted surfaces */
    }
}

int em_player_footstep_tick(EmPlayerStepActor *a, const EmPlayerStepScene *scene,
                            const EmPlayerStepWorkers *w)
{
    if (!a || !scene || !w) return -1;
    switch (a->mode) {
    case 0x01: case 0x02: case 0x2F: case 0x41: {
        int frame_a, frame_b;
        if (!em_player_step_frames(a->clip, &frame_a, &frame_b)) break;
        if (a->step == 0) {
            if (a->clock <= (float)frame_a) {
                if (step_sounds(a, a->tier, w) < 0) return -1;
                if (!scene->foot17 || step_effect(a, scene->foot17, w) < 0) return -1;
                a->step = 1;
            }
        } else if (a->step == 1) {
            if (a->clock <= (float)frame_b) {
                if (step_sounds(a, a->tier, w) < 0) return -1;
                if (!scene->foot18 || step_effect(a, scene->foot18, w) < 0) return -1;
                a->step = 2;
            }
        } else {
            /* Re-arm when the tier drops to zero or on the loop, end or
             * blend flags. */
            if (a->tier == 0) a->step = 0;
            if (a->anim_flags & 0xB000) a->step = 0;
        }
        break;
    }
    case 0x36: case 0x37:
        if (a->step & 0x80) {
            if (step_sounds(a, a->step & 0xF, w) < 0) return -1;
        }
        a->step = 0;
        break;
    default:
        /* The stop and foot-placement endings post 0x80|tier (0017C030). */
        if (a->step & 0x80) {
            if (step_sounds(a, a->step & 0xF, w) < 0) return -1;
            if (step_effect(a, a->position, w) < 0) return -1;
        }
        a->step = 0;
        break;
    }

    /* Wet feet for 120 ticks after surfaces 6 and 0x5B. */
    if (a->surface == 6 || a->surface == 0x5B) a->wet = 0x78;
    else if (a->wet != 0) a->wet = (int16_t)(a->wet - 1);

    /* Wading ripple and its level while +23C is set (not in area 0x15). */
    if (scene->area != 0x15 && a->depth != 0) {
        float speed;
        if (a->contact != 0) {
            if (a->obstruction & 1) speed = (scene->frame & 3) == 0 ? a->speed : 0.0f;
            else speed = a->speed;
        } else {
            speed = em_effect_float32(0.3f * (double)a->speed);
        }
        if (speed != 0.0f) {
            if (a->contact != 0 && (scene->frame & 3) == 0) {
                uint32_t value;
                if (!w->random || w->random(w->context, &value) < 0) return -1;
                if ((int32_t)value < 0x1FFFFFFF) {
                    float water[3] = { a->position[0], a->surface_y, a->position[2] };
                    if (step_effect_at(w, 0x8000001Du, water, a->rotation) < 0) return -1;
                }
            }
            if (!w->wade ||
                w->wade(w->context, a->position, em_effect_float32(0.3f * (double)speed)) < 0)
                return -1;
        }
    }
    return 0;
}

/* ---- 001796C0 / 00179450 / 00179680 ------------------------------------- */

int em_player_floor_query(EmPlayerFallActor *a, const EmPlayerFloorTable *t)
{
    /* 00179450: from the last entry down, the first flagged height below
     * the actor. */
    if (t->count == 0) return 0;
    for (int i = t->count - 1; i >= 0; --i) {
        if (!(t->flags[i] & 1)) continue;
        float y = a->position[1];
        if (!(t->height[i] < y)) continue;
        a->below = em_effect_float32((double)t->height[i] - y);
        return t->aux[i] < 0.62831855f ? 1 : 2;
    }
    return 0;
}

void em_player_fall_enter(EmPlayerFallActor *a)
{
    a->state = 5;
    a->walk = 0;
    a->mode = 11;
    a->lock = 2;
    if (a->special != 0) {
        a->row &= 1;
        a->special = 0;
    }
}

int em_player_fall_check(EmPlayerFallActor *a, const EmPlayerFallWorkers *w)
{
    if (!a || !w) return -1;
    if (a->lock != 0) return 0;
    float rate = a->mode == 0x3A ? 2.0f : 3.0f;
    if (a->contact != 0) {
        /* On the floor: reset the drop; a slope contact (+237) starts the
         * slide state unless the aim stances own +5. */
        a->drop = 0.0f;
        if (a->state == 0x1D || a->state == 0x1E || a->slide == 0) return 0;
        a->state = 0x1C;
        a->walk = 0;
        a->mode = 0x30;
        return 0;
    }
    float limit = em_effect_float32(-0.04f * (double)rate);
    if (!(a->drop <= limit)) {
        a->drop = em_effect_float32((double)a->drop + -0.04f);
        a->position[1] = em_effect_float32((double)a->position[1] + a->drop);
        return 0;
    }
    EmPlayerFloorTable table;
    if (!w->column || w->column(w->context, a->position, &table) < 0) return -1;
    if (table.count < 0 || table.count > EM_PLAYER_FLOOR_TABLE_MAX) return -1;
    if (em_player_floor_query(a, &table) == 0) {
        em_player_fall_enter(a);
        return 0;
    }
    if (a->below < -4.01f) {
        em_player_fall_enter(a);
        return 0;
    }
    a->drop = em_effect_float32((double)a->drop + -0.04f);
    if (a->drop < -4.0f) a->drop = -4.0f;
    a->position[1] = em_effect_float32((double)a->position[1] + a->drop);
    EmPlayerProbeHit hit = {0};
    if (!w->ground) return -1;
    int kind = w->ground(w->context, a->position, a->probe, 6, &hit);
    if (kind < 0) return -1;
    if (kind != 0 && (hit.node & 0xFF00) == 0x1000)
        em_player_fall_enter(a);
    return 0;
}

/* ---- SDK vector math: 001B1470, 001029E8/00102BB0, 00102918, 001026A0 --- */

static float f32_mul(float a, float b) { return em_effect_float32((double)a * b); }
static float f32_add(float a, float b) { return em_effect_float32((double)a + b); }
static float f32_sub(float a, float b) { return em_effect_float32((double)a - b); }

float em_player_sdk_wrap(float x)
{
    const float pi = 3.14159274101257324f, two_pi = 6.28318548202514648f;
    if (!(x <= pi)) {
        do x = f32_sub(x, two_pi); while (!(x <= pi));
    }
    if (x <= -pi) {
        do x = f32_add(x, two_pi); while (x <= -pi);
    }
    return x;
}

/* 001029E8 on pi/2 -+ angle: (sine, cosine) with each VU operation truncated. */
static void sdk_sine_cosine(float angle, float *sine, float *cosine)
{
    static const float coefficient[4] = {   /* D_00241100 */
        0x1.5d3828p-19f, -0x1.9f643ep-13f, 0x1.110e7cp-7f, -0x1.555548p-3f
    };
    const float half_pi = 1.57079637050628662f;
    int negative = angle < 0.0f;
    float argument = negative ? f32_add(half_pi, angle) : f32_sub(half_pi, angle);
    float square = f32_mul(argument, argument);
    float term[4];
    for (unsigned i = 0; i < 4; ++i) term[i] = f32_mul(coefficient[i], argument);
    for (unsigned lanes = 4; lanes > 0; --lanes)
        for (unsigned i = 0; i < lanes; ++i) term[i] = f32_mul(term[i], square);
    float value = argument;
    for (unsigned i = 4; i > 0; --i) value = f32_add(value, term[i - 1]);
    *cosine = f32_add(0.0f, value);
    float root = em_effect_float32(sqrt(fabs((double)f32_sub(1.0f, f32_mul(value, value)))));
    *sine = negative ? f32_sub(0.0f, root) : f32_add(0.0f, root);
}

/* out = c0*v.x + c1*v.y + c2*v.z + c3*v.w through the VU accumulator. */
static void sdk_combine(const float c[4][4], const float v[4], float out[4])
{
    for (unsigned lane = 0; lane < 4; ++lane) {
        float acc = f32_mul(c[0][lane], v[0]);
        acc = f32_add(acc, f32_mul(c[1][lane], v[1]));
        acc = f32_add(acc, f32_mul(c[2][lane], v[2]));
        out[lane] = f32_add(acc, f32_mul(c[3][lane], v[3]));
    }
}

/* 00102B08 (X), 00102BB0 (Y), 00102A60 (Z): M = M x R, row by row. */
static void sdk_rotate(float m[4][4], float angle, int axis)
{
    float s, c;
    sdk_sine_cosine(angle, &s, &c);
    float ns = f32_sub(0.0f, s);
    float r[4][4] = {{0}};
    if (axis == 0) {
        r[0][0] = 1.0f; r[1][1] = c; r[1][2] = s; r[2][1] = ns; r[2][2] = c;
    } else if (axis == 1) {
        r[0][0] = c; r[0][2] = ns; r[1][1] = 1.0f; r[2][0] = s; r[2][2] = c;
    } else {
        r[0][0] = c; r[0][1] = s; r[1][0] = ns; r[1][1] = c; r[2][2] = 1.0f;
    }
    r[3][3] = 1.0f;
    for (unsigned k = 0; k < 4; ++k) {
        float in[4] = { m[k][0], m[k][1], m[k][2], m[k][3] };
        sdk_combine((const float (*)[4])r, in, m[k]);
    }
}

static void sdk_rotate_y(float m[4][4], float angle) { sdk_rotate(m, angle, 1); }

void em_player_sdk_trs(float out[16], const float position[3], const float rotation[3],
                       const float scale[3])
{
    float m[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};      /* 001029C0 */
    sdk_rotate(m, rotation[0], 0);
    sdk_rotate(m, rotation[1], 1);
    sdk_rotate(m, rotation[2], 2);
    for (unsigned row = 0; row < 3; ++row)                            /* vmul[xyz].xyz */
        for (unsigned axis = 0; axis < 3; ++axis)
            m[row][axis] = f32_mul(m[row][axis], scale[row]);
    for (unsigned axis = 0; axis < 3; ++axis)                         /* 00102918 */
        m[3][axis] = f32_add(m[3][axis], position[axis]);
    for (unsigned i = 0; i < 16; ++i) out[i] = m[i / 4][i % 4];
}

void em_player_sdk_yaw_matrix(float angle, float out[16])
{
    float m[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    sdk_rotate_y(m, angle);
    for (unsigned i = 0; i < 16; ++i) out[i] = m[i / 4][i % 4];
}

void em_player_sdk_apply(const float matrix[16], const float local[4], float out[4])
{
    float m[4][4];
    for (unsigned i = 0; i < 16; ++i) m[i / 4][i % 4] = matrix[i];
    float result[4];
    sdk_combine((const float (*)[4])m, local, result);
    for (unsigned i = 0; i < 4; ++i) out[i] = result[i];
}

void em_player_sdk_lane_point(float yaw, float offset, const float position[3],
                              const float local[4], float out[4])
{
    float m[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};      /* 001029C0 */
    sdk_rotate_y(m, em_player_sdk_wrap(f32_add(yaw, offset)));
    for (unsigned axis = 0; axis < 3; ++axis)                         /* 00102918 */
        m[3][axis] = f32_add(m[3][axis], position[axis]);
    sdk_combine((const float (*)[4])m, local, out);                   /* 001026A0 */
}

void em_player_sdk_yaw_transform(float angle, const float position[3],
                                 const float local[4], float out[4])
{
    float m[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};      /* 001029C0 */
    sdk_rotate_y(m, angle);
    if (position)
        for (unsigned axis = 0; axis < 3; ++axis)                     /* 00102918 */
            m[3][axis] = f32_add(m[3][axis], position[axis]);
    sdk_combine((const float (*)[4])m, local, out);                   /* 001026A0 */
}

/* The eight-lane angle table D_00248950. */
static const float kLaneAngles[8] = {
    0.0f, 0.785398185253143311f, -0.785398185253143311f, 1.57079637050628662f,
    -1.57079637050628662f, 2.35619449615478516f, -2.35619449615478516f,
    3.14159274101257324f
};

/* ---- 0019A310 / 00175CF0 / 00175900 ------------------------------------ */

int em_player_floor_link_test(int present, uint8_t type, uint32_t behaviour)
{
    return em_player_link_00175640(present, type, behaviour);
}

void em_player_slope_angle(const EmPlayerProbeHit *hit, const EmPlayerFloorWorkers *w,
                           float *out)
{
    /* mula/madd: nx*nx + nz*nz, then the SDK square root. */
    float sum = f32_add(f32_mul(hit->normal[0], hit->normal[0]),
                        f32_mul(hit->normal[2], hit->normal[2]));
    float h = w->sqrt(w->context, sum);
    float ratio;
    if (h < 1.0e-4f) {
        const uint32_t huge = 0x7F7FC99Eu;
        memcpy(&ratio, &huge, sizeof ratio);
    } else {
        ratio = em_effect_float32((double)fabsf(hit->normal[1]) / h);
    }
    float angle = f32_sub(1.57079637050628662f, w->atan(w->context, ratio));
    *out = ratio < 0.0f ? -angle : angle;
}

int em_player_floor_apply(EmPlayerFloorActor *a, const EmPlayerProbeHit *hit, int index,
                          const EmPlayerFloorWorkers *w)
{
    a->surface_class = hit->node & 0xFF00;
    a->surface_mode = (uint8_t)hit->node;
    em_player_slope_angle(hit, w, &a->slope);
    if (a->surface_mode == 0x35) {
        /* A 0x35 surface records its drive direction at +310. */
        float angle = w->atan2(w->context, -hit->axis[2], hit->axis[0]);
        a->conveyor_yaw = em_player_sdk_wrap(f32_sub(angle, 1.57079637050628662f));
    }
    for (unsigned axis = 0; axis < 3; ++axis)
        a->position[axis] = f32_add(a->position[axis], hit->delta[axis]);
    /* .s 0x00175DD0..0x00175DE0: kind & 2 with a nonzero 0x700031D4 stores
     * it in +214 (D_008104C4). A kind-4 result keeps 0x700031D4 but does not
     * store it. */
    if ((hit->kind & 2) && hit->owner) {
        a->link_owner = hit->owner;
        a->link_flags = hit->entity_flags;
        a->link_type = hit->entity_type;
    }
    int push = a->surface_class == 0x2000;
    if (!push && a->surface_class == 0x1000) {
        /* 00175640(*(+214)): the jal at 0x00175E00 loads +214 in its delay
         * slot, so the test sees the owner stored above or seeded before. */
        int linked;
        if (!w->link_test || w->link_test(w->context, a->link_owner, &linked) < 0) return -1;
        push = linked != 0;
    }
    if (push) {
        /* A wall or linked slope under the feet: undo the vertical step and
         * slide down the slope by delta.y / cos(slope) along its fall line. */
        if (!(a->slope < 0.017453292f) && a->slope <= 3.1241393f) {
            float angle = w->atan2(w->context, -hit->normal[2], hit->normal[0]);
            float yaw = em_player_sdk_wrap(f32_add(1.57079637050628662f, angle));
            a->position[1] = f32_sub(a->position[1], hit->delta[1]);
            float t = w->cosine(w->context, a->slope);
            const float local[4] = { 0.0f, 0.0f,
                                     em_effect_float32((double)hit->delta[1] / t), 0.0f };
            float m[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
            sdk_rotate_y(m, yaw);
            float out[4];
            sdk_combine((const float (*)[4])m, local, out);
            a->position[0] = f32_add(a->position[0], out[0]);
            a->position[2] = f32_add(a->position[2], out[2]);
        }
        return 0;
    }
    a->contact = index == 0 ? 1 : index < 6 ? 3 : 2;
    if (a->link_owner) a->contact |= 0x80;     /* +214 != 0 (0x00175F84) */
    if (a->surface_mode == 0x39) {
        if (index < 6 && a->major == 1 && a->state == 1) {
            if (!w->surface39 || w->surface39(w->context, 0) < 0) return -1;
            return 0;
        }
        if (a->major == 1 && (a->state == 8 || a->state == 0)) {
            if (!w->surface39 || w->surface39(w->context, 1) < 0) return -1;
        }
        return 0;
    }
    if (a->surface_class == 0x1000) {
        a->slide = 1;
        float angle = w->atan2(w->context, -hit->normal[2], hit->normal[0]);
        a->slide_yaw = em_player_sdk_wrap(f32_add(1.57079637050628662f, angle));
        return 0;
    }
    a->slide = 0;
    return 0;
}

int em_player_floor_service(EmPlayerFloorActor *a, int search, float at[3],
                            const EmPlayerFloorWorkers *w)
{
    if (!a || !w || !at || !w->ground || !w->head || !w->sqrt || !w->atan) return -1;
    if (a->mode != 0x30 && a->surface_mode != 0x35) {
        a->surface_mode = 0;
        a->slope = 0.0f;
    }
    EmPlayerProbeHit hit;
    memset(&hit, 0, sizeof hit);
    int kind = w->ground(w->context, a->position, a->probe, 6, &hit);
    if (kind < 0) return -1;
    if (kind != 0) {
        memcpy(at, a->position, 3 * sizeof *at);
        if (em_player_floor_apply(a, &hit, 0, w) < 0) return -1;
    } else if (search) {
        /* No floor under the feet: the first of eight points two units out. */
        static const float local[4] = { 0.0f, 0.0f, 2.0f, 1.0f };
        for (int i = 0; i < 8; ++i) {
            float point[4];
            em_player_sdk_lane_point(a->yaw, kLaneAngles[i], a->position, local, point);
            memset(&hit, 0, sizeof hit);
            kind = w->ground(w->context, point, a->probe, 6, &hit);
            if (kind < 0) return -1;
            if (kind != 0) {
                memcpy(at, point, 3 * sizeof *at);
                if (em_player_floor_apply(a, &hit, i + 1, w) < 0) return -1;
                break;
            }
        }
    }

    /* The surface record: the first face within 18 units above the feet. */
    float top[3] = { a->position[0], f32_add(a->position[1], 18.0f), a->position[2] };
    memset(&hit, 0, sizeof hit);
    kind = w->head(w->context, top, a->position, &hit);
    if (kind < 0) return -1;
    if (kind != 0) {
        a->floor_hit = 1;
        a->surface = (uint8_t)hit.node;
        a->surface_y = hit.point[1];
        if (a->surface == 0x5A) {
            if (a->puddle == 0) {
                a->puddle = 1;
                if (!w->first_contact || w->first_contact(w->context, 0x5A) < 0) return -1;
            }
        } else if (a->surface == 0x5B) {
            if (a->depth == 0) {
                /* Deep when nothing lies between the surface and 4.01 below it. */
                float probe_at[3] = { a->position[0], f32_sub(a->surface_y, 4.01f), a->position[2] };
                memset(&hit, 0, sizeof hit);
                kind = w->ground(w->context, probe_at, a->probe, 6, &hit);
                if (kind < 0) return -1;
                a->depth = kind != 0 ? 1 : 2;
                if (!w->first_contact || w->first_contact(w->context, 0x5B) < 0) return -1;
            }
        } else if (a->surface == 0x5C) {
            if (a->marsh == 0) {
                a->marsh = 1;
                if (!w->first_contact || w->first_contact(w->context, 0x5C) < 0) return -1;
            }
        }
    } else {
        a->depth = a->puddle = a->marsh = 0;
        a->surface_y = a->position[1];
    }

    if (a->contact != 0) {
        if (a->floor_hit == 0) {
            if (!w->object) return -1;
            memset(&hit, 0, sizeof hit);
            kind = w->object(w->context, at, a->probe, 7, &hit);
            if (kind < 0) return -1;
            if (kind != 0) {
                a->surface = (uint8_t)hit.node;
            } else {
                a->surface = 0;
                /* 0x80 is only set with a +214 link (00175CF0). */
                if ((a->contact & 0x80) && a->link_owner && (a->link_flags & ~0xE0) == 4) {
                    uint8_t t = a->link_type;
                    if (t == 2 || t == 10 || t == 12 || t == 24 || t == 42 || t == 40)
                        a->surface = 4;
                }
            }
        }
        a->lock = 0;
        if (a->state != 0x1C) a->pitch = 0.0f;
    }
    return a->contact;
}

/* ---- 001764E0 / 00176390 / 00176BE0 / 001762E0 / 00176C80 / 001756E0 ---- */

/* 00176BE0: apply the hit delta unless it came from a floor or ceiling, or
 * the R2 stance variant 1 owns the actor. */
static void probe_slide(EmPlayerProbeActor *a, const EmPlayerProbeHit *hit)
{
    unsigned cls = hit->node & 0xFF00;
    if (cls == 0x4000 || cls == 0x8000) return;
    if (a->major == 1 && a->state == 0x1E && a->variant == 1) return;
    for (unsigned axis = 0; axis < 3; ++axis)
        a->position[axis] = f32_add(a->position[axis], hit->delta[axis]);
}

/* 00176390(actor, kind, target, lane). */
static int probe_response(EmPlayerProbeActor *a, const EmPlayerProbeScene *scene, int kind,
                          const EmPlayerProbeHit *hit, const float target[3], int lane,
                          const EmPlayerProbeWorkers *w)
{
    static const uint8_t kNeighbours[8] = { 0xE0, 0xD0, 0xA8, 0x54, 0x2A, 0x15, 0x0B, 0x07 };
    if (kind & 1) {
        if (!hit->entity || (hit->entity_flags & ~0xE0) != 2) {
            probe_slide(a, hit);
            return 0;
        }
        EmPlayerProbeHit again;
        memset(&again, 0, sizeof again);
        int r = w->move(w->context, a->position, target, 6, &again);
        if (r < 0) return -1;
        if (r & 6) {
            probe_slide(a, &again);
            return 0;
        }
        uint8_t mask = kNeighbours[lane];
        if (!(scene->previous_obstruction & mask) && !(a->obstruction & mask)) {
            if (!w->hull_shove || w->hull_shove(w->context, target) < 0) return -1;
        }
        return 0;
    }
    if (kind & 2) {
        /* 001762E0 passes only in area 2 against a 'T' (0x54) entity. */
        if (scene->area == 2 && hit->entity && hit->entity_type == 0x54) {
            if (!w->target_shove || w->target_shove(w->context) < 0) return -1;
            return 0;
        }
        probe_slide(a, hit);
        return 0;
    }
    probe_slide(a, hit);
    return 0;
}

int em_player_crawl_ahead(const EmPlayerProbeActor *a, const EmPlayerProbeWorkers *w)
{
    static const float local[4] = { 0.0f, 4.01000022888183594f, 10.0f, 1.0f };
    if (!w->move || !w->column) return -1;
    for (int i = 0; i < 3; ++i) {
        float point[4];
        em_player_sdk_lane_point(a->yaw, em_effect_float32((double)kLaneAngles[i] / 2.0f),
                                 a->position, local, point);
        EmPlayerProbeHit hit;
        memset(&hit, 0, sizeof hit);
        int kind = w->move(w->context, a->position, point, 7, &hit);
        if (kind < 0) return -1;
        if (kind != 0) return 0;
        memset(&hit, 0, sizeof hit);
        kind = w->column(w->context, point, 13.9899997711181641f, &hit);
        if (kind < 0) return -1;
        if (kind == 0) return 0;
    }
    return 1;
}

int em_player_wall_probes(EmPlayerProbeActor *a, EmPlayerProbeScene *scene,
                          const EmPlayerProbeWorkers *w)
{
    static const float kAnkle[4] = { 0.0f, 0.0500000007450580597f, 4.5f, 1.0f };
    static const float kChest[4] = { 0.0f, 4.01000022888183594f, 4.5f, 1.0f };   /* D_002488C0 */
    static const float kCentre[4] = { 0.0f, 4.01000022888183594f, 0.0f, 1.0f };
    static const float kLow[4] = { 0.0f, 13.8000001907348633f, 4.5f, 1.0f };     /* D_002488D0 */
    static const float kHigh[4] = { 0.0f, 18.0f, 4.5f, 1.0f };                   /* D_002488E0 */
    if (!a || !scene || !w || !w->move || !w->sweep || !w->column) return -1;
    int crawl = 0;
    if (a->special == 0) {
        crawl = em_player_crawl_ahead(a, w);
        if (crawl < 0) return -1;
    }

    /* The ankle pass runs only in the walk callback (+4 == 1, +5 == 1). */
    if (a->major == 1 && a->state == 1) {
        for (int i = 0; i < 5; ++i) {
            float target[4];
            em_player_sdk_lane_point(a->yaw, kLaneAngles[i], a->position, kAnkle, target);
            EmPlayerProbeHit hit;
            memset(&hit, 0, sizeof hit);
            int kind = w->move(w->context, a->position, target, 6, &hit);
            if (kind < 0) return -1;
            if (kind == 0) continue;
            unsigned cls = hit.node & 0xFF00;
            int apply = 0;
            if (cls == 0x1000 || cls == 0x800) {
                apply = 1;
            } else if (cls == 0x2000) {
                if (hit.entity) {
                    apply = (hit.entity_flags & ~0xE0) == 4 && hit.entity_type == 2;
                } else if (scene->inherited_s1 & 4) {
                    if (!w->sqrt || !w->atan) return -1;
                    EmPlayerFloorWorkers math;
                    memset(&math, 0, sizeof math);
                    math.context = w->context;
                    math.sqrt = w->sqrt;
                    math.atan = w->atan;
                    float angle;
                    em_player_slope_angle(&hit, &math, &angle);
                    angle = fabsf(angle);
                    apply = angle < 1.22173059f || !(angle <= 1.91986215f);
                }
            }
            if (apply)
                for (unsigned axis = 0; axis < 3; ++axis)
                    a->position[axis] = f32_add(a->position[axis], hit.delta[axis]);
        }
    }

    scene->previous_obstruction = a->obstruction;
    a->obstruction = 0;
    for (int j = 0; j < 8; ++j) {
        /* The lane matrix keeps its rotation; its translation is refreshed
         * from the corrected position before the last ray. */
        float target[4], centre[4];
        em_player_sdk_lane_point(a->yaw, kLaneAngles[j], a->position, kChest, target);
        em_player_sdk_lane_point(a->yaw, kLaneAngles[j], a->position, kCentre, centre);
        EmPlayerProbeHit hit;
        memset(&hit, 0, sizeof hit);
        int kind = w->sweep(w->context, centre, target, 7, &hit);
        if (kind < 0) return -1;
        if (kind != 0) {
            a->obstruction |= (uint8_t)(1u << j);
            if (probe_response(a, scene, kind, &hit, target, j, w) < 0) return -1;
        } else {
            /* Something overhead at the lane end: the 4.01 point up to 18
             * (13.8 while low). */
            int low = a->special != 0;
            float height = f32_sub(low ? 13.8000001907348633f : 18.0f, 4.01000022888183594f);
            memset(&hit, 0, sizeof hit);
            kind = w->column(w->context, target, height, &hit);
            if (kind < 0) return -1;
            if (kind != 0) {
                float under[3] = { hit.point[0], f32_add(hit.point[1], 0.100000001490116119f),
                                   hit.point[2] };
                int probe = 1;
                if (!low) {
                    /* An overhang below 13.8 is a wall. A higher one on the
                     * three front lanes, with crawl space ahead, lowers the
                     * player instead (+236). The readable 001764E0 C inverts
                     * the height clause; the branch at 0x176990 is bc1f. */
                    probe = j >= 3 || (unsigned)(a->mode - 0x36) < 2 || a->mode == 0x3E ||
                            a->speed < 0.0f ||
                            f32_sub(hit.point[1], a->position[1]) < 13.8000001907348633f ||
                            crawl == 0;
                    if (!probe) a->special = 1;
                }
                if (probe) {
                    memset(&hit, 0, sizeof hit);
                    kind = w->move(w->context, a->position, under, 7, &hit);
                    if (kind < 0) return -1;
                    if (kind != 0) {
                        a->obstruction |= (uint8_t)(1u << j);
                        if (probe_response(a, scene, kind, &hit, under, j, w) < 0) return -1;
                    }
                }
            }
        }
        float upper[4];
        float m[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
        sdk_rotate_y(m, em_player_sdk_wrap(f32_add(a->yaw, kLaneAngles[j])));
        for (unsigned axis = 0; axis < 3; ++axis) m[3][axis] = a->position[axis];
        sdk_combine((const float (*)[4])m, a->special ? kLow : kHigh, upper);
        memset(&hit, 0, sizeof hit);
        kind = w->move(w->context, a->position, upper, 7, &hit);
        if (kind < 0) return -1;
        if (kind != 0) {
            a->obstruction |= (uint8_t)(1u << j);
            if (probe_response(a, scene, kind, &hit, upper, j, w) < 0) return -1;
        }
    }
    return 0;
}

int em_player_clearance_release(EmPlayerProbeActor *a, const EmPlayerProbeScene *scene,
                                const EmPlayerProbeWorkers *w)
{
    static const float local[4] = { 0.0f, 4.01000022888183594f, 4.0f, 1.0f };
    if (!a || !scene || !w) return -1;
    if (scene->area == 0x12 && !(a->position[1] < 165.0f) && (a->contact & 0x80) &&
        a->link && a->link_type == 6) {
        a->special = 1;
        a->row |= 2;
        if (a->major == 1 && a->state == 0) {
            if (!w->pose || w->pose(w->context, 12.0f) < 0) return -1;
        }
        return 1;
    }
    uint8_t old = a->special;
    if (old != 0) {
        a->special = 0;
        if (!w->column) return -1;
        for (int i = 0; i < 7; ++i) {
            float point[4];
            em_player_sdk_lane_point(a->yaw, kLaneAngles[i], a->position, local, point);
            EmPlayerProbeHit hit;
            memset(&hit, 0, sizeof hit);
            int kind = w->column(w->context, point, 13.9899997711181641f, &hit);
            if (kind < 0) return -1;
            if (kind != 0) { a->special = 1; break; }
        }
    }
    if (a->special) a->row |= 2;
    else a->row &= 1;
    if (a->major == 1 && a->state == 0 && old != a->special) {
        if (!w->pose || w->pose(w->context, 12.0f) < 0) return -1;
    }
    return 0;
}

/* ---- The live actor mirrors -------------------------------------------- */

static void live_vector(const EmPlayerLiveActor *live, unsigned at, float *out, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) out[i] = em_live_f32(live, at + 4 * i);
}

static void live_set_vector(EmPlayerLiveActor *live, unsigned at, const float *in, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) em_live_set_f32(live, at + 4 * i, in[i]);
}

void em_player_floor_actor_from_live(const EmPlayerLiveActor *live, EmPlayerFloorActor *a)
{
    memset(a, 0, sizeof *a);
    live_vector(live, 0xB0, a->position, 3);
    live_vector(live, 0x280, a->probe, 3);
    a->yaw = em_live_f32(live, 0xC4);
    a->pitch = em_live_f32(live, 0xC0);
    a->slope = em_live_f32(live, 0x9C);
    a->surface_y = em_live_f32(live, 0x250);
    a->conveyor_yaw = em_live_f32(live, 0x310);
    a->slide_yaw = em_live_f32(live, 0x218);
    a->surface_class = em_live_u16(live, 0x238);
    a->mode = em_live_u8(live, 0x1F0);
    a->surface_mode = em_live_u8(live, 0x23B);
    a->surface = em_live_u8(live, 0x23A);
    a->contact = em_live_u8(live, 0xA);
    a->floor_hit = em_live_u8(live, 0xB);
    a->depth = em_live_u8(live, 0x23C);
    a->puddle = em_live_u8(live, 0x23D);
    a->marsh = em_live_u8(live, 0x23E);
    a->lock = em_live_u8(live, 0x25F);
    a->slide = em_live_u8(live, 0x237);
    a->major = em_live_u8(live, 4);
    a->state = em_live_u8(live, 5);
    a->link_owner = live->link_owner;
    a->link_flags = live->link_flags;
    a->link_type = live->link_type;
}

void em_player_floor_actor_to_live(const EmPlayerFloorActor *a, EmPlayerLiveActor *live)
{
    live_set_vector(live, 0xB0, a->position, 3);
    live_set_vector(live, 0x280, a->probe, 3);
    em_live_set_f32(live, 0xC4, a->yaw);
    em_live_set_f32(live, 0xC0, a->pitch);
    em_live_set_f32(live, 0x9C, a->slope);
    em_live_set_f32(live, 0x250, a->surface_y);
    em_live_set_f32(live, 0x310, a->conveyor_yaw);
    em_live_set_f32(live, 0x218, a->slide_yaw);
    em_live_set_u16(live, 0x238, a->surface_class);
    em_live_set_u8(live, 0x1F0, a->mode);
    em_live_set_u8(live, 0x23B, a->surface_mode);
    em_live_set_u8(live, 0x23A, a->surface);
    em_live_set_u8(live, 0xA, a->contact);
    em_live_set_u8(live, 0xB, a->floor_hit);
    em_live_set_u8(live, 0x23C, a->depth);
    em_live_set_u8(live, 0x23D, a->puddle);
    em_live_set_u8(live, 0x23E, a->marsh);
    em_live_set_u8(live, 0x25F, a->lock);
    em_live_set_u8(live, 0x237, a->slide);
    em_live_set_u8(live, 4, a->major);
    em_live_set_u8(live, 5, a->state);
    live->link_owner = a->link_owner;
    live->link_flags = a->link_flags;
    live->link_type = a->link_type;
}

void em_player_fall_actor_from_live(const EmPlayerLiveActor *live, EmPlayerFallActor *a)
{
    memset(a, 0, sizeof *a);
    live_vector(live, 0xB0, a->position, 3);
    live_vector(live, 0x280, a->probe, 3);
    a->drop = em_live_f32(live, 0x2EC);
    a->below = em_live_f32(live, 0x258);
    a->lock = em_live_u8(live, 0x25F);
    a->mode = em_live_u8(live, 0x1F0);
    a->contact = em_live_u8(live, 0xA);
    a->state = em_live_u8(live, 5);
    a->walk = em_live_u8(live, 6);
    a->slide = em_live_u8(live, 0x237);
    a->row = em_live_u8(live, 0x235);
    a->special = em_live_u8(live, 0x236);
}

void em_player_fall_actor_to_live(const EmPlayerFallActor *a, EmPlayerLiveActor *live)
{
    live_set_vector(live, 0xB0, a->position, 3);
    live_set_vector(live, 0x280, a->probe, 3);
    em_live_set_f32(live, 0x2EC, a->drop);
    em_live_set_f32(live, 0x258, a->below);
    em_live_set_u8(live, 0x25F, a->lock);
    em_live_set_u8(live, 0x1F0, a->mode);
    em_live_set_u8(live, 0xA, a->contact);
    em_live_set_u8(live, 5, a->state);
    em_live_set_u8(live, 6, a->walk);
    em_live_set_u8(live, 0x237, a->slide);
    em_live_set_u8(live, 0x235, a->row);
    em_live_set_u8(live, 0x236, a->special);
}

/* ---- The player stage: 0015BA50 / 0015BCF0 / 0015B130 / 0015B770 / 0015D460 */

#define STAGE_CALL(expression) do { if ((expression) < 0) return -1; } while (0)

int em_player_stage_begin(EmPlayerLiveActor *a, EmPlayerStageScene *s,
                          const EmPlayerStageWorkers *w)
{
    if (!a || !s || !w || !w->clip_rate) return -1;
    /* idx = *(short *)(p + 0x20C); +34 = D_00248C98[idx * 3] * +204. */
    float rate;
    STAGE_CALL(w->clip_rate(w->context, (int16_t)em_live_u16(a, 0x20C), &rate));
    em_live_set_f32(a, 0x34, em_effect_float32((double)rate * (double)em_live_f32(a, 0x204)));
    em_live_set_f32(a, 0x204, 1.0f);
    em_live_set_u8(a, 0x303, 0);
    em_live_set_u8(a, 0x25D, 0);
    em_live_set_u8(a, 1, 1);
    em_live_set_u8(a, 0x319, em_live_u8(a, 0xA));
    em_live_set_u8(a, 0xA, 0);
    a->link_prev = a->link_owner;           /* +308 = +214 */
    a->link_owner = NULL;                   /* +214 = 0 */
    em_live_set_u8(a, 0x318, 0);
    if (s->spad3B8F != 2) em_live_set_u16(a, 0x94, 0xFFFF);
    s->busy = 0;                            /* D_008106B3 = 0 */
    return 0;
}

static int stage_advance(EmPlayerLiveActor *a, const EmPlayerStageWorkers *w, unsigned step_at)
{
    uint32_t flags;
    if (!w->advance || w->advance(w->context, a, em_live_f32(a, step_at), &flags) < 0) return -1;
    em_live_set_u32(a, 0x200, flags);
    return 0;
}

static int stage_major(EmPlayerLiveActor *a, const EmPlayerStageWorkers *w, unsigned major)
{
    if (!w->major[major]) return -1;
    return w->major[major](w->major_context[major], a) < 0 ? -1 : 0;
}

int em_player_stage_dispatch(EmPlayerLiveActor *a, const EmPlayerStageWorkers *w)
{
    if (!a || !w) return -1;
    uint8_t major = em_live_u8(a, 4);
    switch (major) {
    case 0:
    case 6:
        return stage_major(a, w, major);
    case 1:
    case 2:
    case 5:
        STAGE_CALL(stage_advance(a, w, 0x34));
        return stage_major(a, w, major);
    case 4: {
        uint8_t v = em_live_u8(a, 5);
        if (v == 0 || v == 0x17) {
            int committed;
            if (!w->commit) return -1;
            STAGE_CALL(w->commit(w->context, a, &committed));
            if (committed != 0) STAGE_CALL(stage_advance(a, w, 0x1F4));
        } else {
            STAGE_CALL(stage_advance(a, w, 0x34));
        }
        return stage_major(a, w, 4);
    }
    default:
        return 0;   /* +4 = 3 (the table's default entry) and above 6 */
    }
}

void em_player_stage_end(EmPlayerLiveActor *a, EmPlayerStageScene *s)
{
    em_live_set_u8(a, 0xB, 0);
    if (em_live_u8(a, 4) != 1) {
        em_live_set_u16(a, 0x276, 0);
        em_live_set_u8(a, 0x274, 0);
    }
    uint8_t mode = em_live_u8(a, 0x1F0);
    if (mode != 0x31 && mode != 0x32 && !(mode == 0x34 || mode == 0x35)) {
        em_live_set_u16(a, 0x276, 0);
        em_live_set_u8(a, 0x274, 0);
    }
    uint8_t major = em_live_u8(a, 4), state = em_live_u8(a, 5);
    if (s->d8106F1 != 0 || em_live_u16(a, 0x276) != 0 || em_live_u8(a, 0x1F0) == 0x33 ||
        s->d810CB6 != 0 ||
        (major == 2 &&
         (state == 0xD || state == 0xE || state == 0xF || state == 0xB || state == 0xC) &&
         em_live_u8(a, 0x1F1) == 1))
        s->busy = 1;
}

int em_player_stage_tail(EmPlayerLiveActor *a, const EmPlayerStageWorkers *w)
{
    if (!a || !w) return -1;
    em_live_set_u32(a, 0xBC, 0x3F800000u);
    uint8_t major = em_live_u8(a, 4);
    if (major != 6 && (major != 2 || em_live_u8(a, 5) != 0x16) && em_live_f32(a, 0xB4) < -200.0f) {
        em_live_set_u8(a, 4, 6);
        em_live_set_u8(a, 5, 0);
    }
    if (em_live_u8(a, 0x31A) == 0 || (int8_t)em_live_u8(a, 0x31B) == -1) return 0;
    uint16_t event = em_live_u16(a, 0x31C);
    major = em_live_u8(a, 4);
    uint8_t state = em_live_u8(a, 5);
    int stop = 0;
    if (event == 0x5DD)
        stop = em_live_u8(a, 0x275) != 4 || major != 1 ||
               (state != 0x1D && (unsigned)(state - 0x1E) > 2);
    else if (event == 0x12E)
        stop = major != 1 || state != 0x1C;
    else if (event == 0x135)
        stop = major != 1 || state != 0x17;
    if (!stop) return 0;
    if (!w->stop_sound) return -1;
    STAGE_CALL(w->stop_sound(w->context, (int8_t)em_live_u8(a, 0x31B)));
    em_live_set_u8(a, 0x31B, 0xFF);
    em_live_set_u8(a, 0x31A, 0);
    return 0;
}

static int stage_state(EmPlayerStateCallback const *table, void *const *contexts, unsigned index,
                       EmPlayerLiveActor *a)
{
    if (!table[index]) return -1;
    return table[index](contexts[index], a) < 0 ? -1 : 0;
}

int em_player_stage_0015B130(void *context, EmPlayerLiveActor *a)
{
    EmPlayerStage *stage = context;
    if (!stage || !stage->scene || !stage->workers || !a) return -1;
    EmPlayerStageScene *s = stage->scene;
    const EmPlayerStageWorkers *w = stage->workers;
    if (s->spad3B8D != 0) {
        if (em_live_u8(a, 5) == 0x19) {
            s->spad3B8F = 1;
        } else if (s->area != 0x15 && em_live_u8(a, 0x1F0) == 0x2A) {
            em_live_set_u8(a, 4, 4);
            em_live_set_u8(a, 5, 0x17);
            em_live_set_u8(a, 6, 0);
            if (!w->scripted_notify) return -1;
            return w->scripted_notify(w->context, a) < 0 ? -1 : 0;
        } else if (em_live_u8(a, 0x1F0) == 0x17) {
            em_live_set_u8(a, 4, 4);
            em_live_set_u8(a, 5, 0xC);
            em_live_set_u8(a, 6, 0);
            if (!w->scripted_notify) return -1;
            return w->scripted_notify(w->context, a) < 0 ? -1 : 0;
        } else {
            int result;
            if (!w->scripted_check) return -1;
            STAGE_CALL(w->scripted_check(w->context, a, &result));
            if (result == 0) {
                em_live_set_u8(a, 4, 4);
                em_live_set_u8(a, 5, 0);
                em_live_set_u8(a, 6, 0);
                em_live_set_u8(a, 0x1F0, 0x41);
                if (!w->row_request || !w->scripted_notify) return -1;
                STAGE_CALL(w->row_request(w->context, a, 8.0f));
                return w->scripted_notify(w->context, a) < 0 ? -1 : 0;
            }
        }
    }
    int reacted;
    if (!w->reaction) return -1;
    STAGE_CALL(w->reaction(w->context, a, &reacted));
    if (reacted != 0) return 0;
    uint8_t state = em_live_u8(a, 5);
    if (state == 0x19) {
        if (s->spad3B8D == 0) STAGE_CALL(stage_state(w->state, w->state_context, 0x19, a));
        else em_live_set_u8(a, 1, 0);
    } else if (state < 0x25) {
        STAGE_CALL(stage_state(w->state, w->state_context, state, a));
    }   /* 0x25 is an empty case; above it the dispatch is skipped */
    int16_t countdown = (int16_t)em_live_u16(a, 0x20E);
    if (countdown != 0) {
        em_live_set_u16(a, 0x20E, (uint16_t)(countdown - 1));
        if (em_live_u16(a, 0x20E) == 0) em_live_set_u8(a, 0, 1);
    } else if (!(em_live_u8(a, 0) & 2)) {
        if (!w->drain) return -1;
        STAGE_CALL(w->drain(w->context, a));
    }
    if (!w->heartbeat) return -1;
    return w->heartbeat(w->context, a) < 0 ? -1 : 0;
}

int em_player_stage_0015B770(void *context, EmPlayerLiveActor *a)
{
    EmPlayerStage *stage = context;
    if (!stage || !stage->workers || !a) return -1;
    const EmPlayerStageWorkers *w = stage->workers;
    int ignored;
    if (!w->reaction) return -1;
    STAGE_CALL(w->reaction(w->context, a, &ignored));   /* result unused */
    uint8_t state = em_live_u8(a, 5), phase = em_live_u8(a, 0xD);
    switch (state) {
    case 8:
        return 0;
    case 0xD:
        if (phase > 4) return 0;
        return stage_state(w->phase13, w->phase13_context, phase, a);
    case 0xE:
        if (phase > 3) return 0;   /* phase 4 is an empty case */
        return stage_state(w->phase14, w->phase14_context, phase, a);
    case 0x19:
        em_live_set_u8(a, 1, 0);
        return stage_state(w->state2, w->state2_context, 0x19, a);
    default:
        if (state >= EM_PLAYER_STATE2_COUNT) return 0;
        return stage_state(w->state2, w->state2_context, state, a);
    }
}

int em_player_stage_0015D460(void *context, EmPlayerLiveActor *a)
{
    const EmPlayerStageFade *f = context;
    if (!a) return -1;
    uint8_t state = em_live_u8(a, 5);
    switch (state) {
    case 0:
        em_live_set_u8(a, 5, (uint8_t)(state + 1));
        em_live_set_u32(a, 0x220, 0);
        em_live_set_u8(a, 0, 0);
        return 0;
    case 1:
        em_live_set_u8(a, 5, (uint8_t)(state + 1));
        if (!f || !f->fade) return -1;
        return f->fade(f->context, 4, 0) < 0 ? -1 : 0;
    default:
        return 0;
    }
}
