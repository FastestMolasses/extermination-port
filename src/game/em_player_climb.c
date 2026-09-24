/* em_player_climb.c - the player's ledge climb (see em_player_climb.h).
 *
 * Every routine here was read from the original instructions of the named
 * function; tools/test_player_climb_reference.py executes those instructions
 * on the measured float model and compares every field and worker call. The
 * scratchpad ledge frame that 00177510 publishes (0x70003050 point,
 * 0x70003060 normal, 0x700031E4 heading, 0x70003070 yaw matrix) is the local
 * EmPlayerRecoveryLedge below. Every COP1 operation and compare goes through
 * em_ee_float.h; the helpers and SDK leaves are their owners'
 * (em_player_record_helpers.h). */
#include "game/em_player_climb.h"
#include "game/em_player_record_helpers.h"
#include "game/em_player_stage_workers.h"
#include "game/em_sdk_math_original.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

static float add(float a, float b) { return em_ee_add(a, b); }
static float sub(float a, float b) { return em_ee_sub(a, b); }
static float mul(float a, float b) { return em_ee_mul(a, b); }
static float divide(float a, float b) { return em_ee_div(a, b); }
static int lt(float a, float b) { return em_ee_c_lt(a, b); }
static int le(float a, float b) { return em_ee_c_le(a, b); }
static int eq(float a, float b) { return em_ee_c_eq(a, b); }
static uint32_t fbits(float v) { return em_ee_bits(v); }
static float bfloat(uint32_t b) { return em_ee_float(b); }
/* 0011DF78 (em_sdk_math_original_0011DF78). */
static float fabs_0011DF78(float a) { return em_sdk_math_original_0011DF78(a); }
/* 001B1470 (em_player_001B1470, bounded). */
static int wrap(float x, float *out)
{
    uint32_t r;
    FAULT(em_player_helper_wrap(fbits(x), &r));
    *out = bfloat(r);
    return 0;
}

static void vec_bits(uint32_t *out, const float *in, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) out[i] = fbits(in[i]);
}

static void vec_floats(float *out, const uint32_t *in, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) out[i] = bfloat(in[i]);
}

/* 001026A0(out, M, v) over float vectors. */
static int apply(const float matrix[16], const float local[4], float out[4])
{
    uint32_t m[16], v[4], o[4];
    vec_bits(m, matrix, 16);
    vec_bits(v, local, 4);
    FAULT(em_player_helper_apply(m, v, o));
    vec_floats(out, o, 4);
    return 0;
}

/* 001026A0 of the ledge matrix, then 001028B8 (all four lanes) with base,
 * then w = 1.0. */
static int ledge_offset(const EmPlayerRecoveryLedge *l, const float base[4], const float local[4],
                        float out[4])
{
    uint32_t m[16], b[4], v[4], o[4];
    vec_bits(m, l->matrix, 16);
    vec_bits(b, base, 4);
    vec_bits(v, local, 4);
    FAULT(em_player_helper_ledge_offset(m, b, v, o));
    vec_floats(out, o, 4);
    return 0;
}

/* 001029C0, 00102BB0(M, M, angle), 00102918(M, M, base), 001026A0. */
static int yaw_point(float angle, const float base[3], const float local[4], float out[4])
{
    uint32_t b[3], v[4], o[4];
    vec_bits(b, base, 3);
    vec_bits(v, local, 4);
    FAULT(em_player_helper_yaw_point(fbits(angle), b, v, o));
    vec_floats(out, o, 4);
    return 0;
}

int em_player_climb_trs(EmPlayerClimbActor *a)
{
    return em_player_helper_trs(a->matrix, a->position, a->rotation, a->scale);
}

/* ---- The helper bodies over the climb's workers --------------------------- */

static int calls_sweep(void *context, const uint32_t from[4], const uint32_t to[4], unsigned mask,
                       uint16_t *node)
{
    const EmPlayerClimbWorkers *w = context;
    float f[4], t[4];
    vec_floats(f, from, 4);
    vec_floats(t, to, 4);
    EmPlayerClimbHit hit;
    memset(&hit, 0, sizeof hit);
    int kind = w->sweep(w->context, f, t, mask, &hit);
    if (kind < 0) return -1;
    if (kind) *node = hit.probe.node;
    return kind;
}

static int calls_atan2(void *context, uint32_t y, uint32_t x, uint32_t *out)
{
    const EmPlayerClimbWorkers *w = context;
    *out = fbits(w->atan2(w->context, bfloat(y), bfloat(x)));
    return 0;
}

static EmPlayerHelperCalls helper_calls(const EmPlayerClimbWorkers *w)
{
    EmPlayerHelperCalls c;
    c.context = (void *)(uintptr_t)w;
    c.sweep = w->sweep ? calls_sweep : NULL;
    c.atan2 = w->atan2 ? calls_atan2 : NULL;
    c.cosine = NULL;
    return c;
}

/* The 0x700038A0 words the helpers write: the bound scratch, else a local. */
static uint32_t *scratch_38A0(const EmPlayerClimbWorkers *w, uint32_t local[4])
{
    return w->scratch ? w->scratch->s38A0 : local;
}

/* 0015DF10's own scratch stores: the workspace vector 0x700038A0 and the
 * word 0x70003A20 (NULL scratch: discarded). */
static void store_38A0(const EmPlayerClimbWorkers *w, const float v[4])
{
    if (w->scratch) vec_bits(w->scratch->s38A0, v, 4);
}

static void store_3A20(const EmPlayerClimbWorkers *w, float v)
{
    if (w->scratch) w->scratch->s3A20 = fbits(v);
}

/* 0015DEC0: a wall face that is neither attribute 0x46 nor 0x32. */
static int face_gate(const EmPlayerClimbHit *hit)
{
    unsigned attribute = hit->probe.node & 0xFF;
    if (attribute == 0x46 || attribute == 0x32) return 0;
    return (hit->probe.node & 0xFF00) == 0x2000;
}

/* 00177510 (em_player_helper_00177510). */
static int capture(EmPlayerRecoveryLedge *ledge, const EmPlayerClimbHit *hit,
                   const EmPlayerClimbWorkers *w)
{
    const EmPlayerHelperCalls c = helper_calls(w);
    return em_player_helper_00177510(&c, hit->probe.point, hit->probe.normal, ledge);
}

static int move(const EmPlayerClimbActor *a, const float target[4], const EmPlayerClimbWorkers *w,
                EmPlayerClimbHit *hit)
{
    if (!w->move) return -1;
    memset(hit, 0, sizeof *hit);
    return w->move(w->context, a->position, target, 7, hit);
}

static int sweep(const float from[4], const float to[4], unsigned mask,
                 const EmPlayerClimbWorkers *w, EmPlayerClimbHit *hit)
{
    if (!w->sweep) return -1;
    memset(hit, 0, sizeof *hit);
    return w->sweep(w->context, from, to, mask, hit);
}

static int column(const float at[4], float height, const EmPlayerClimbWorkers *w)
{
    if (!w->column) return -1;
    return w->column(w->context, at, height);
}

/* 0011E748(dx*dx + dz*dz): MULA.S then MADD.S. */
static float distance(float dx, float dz, const EmPlayerClimbWorkers *w, int *fault)
{
    if (!w->sqrt) { *fault = 1; return 0.0f; }
    const uint32_t sum = em_ee_madd_bits(em_ee_mula_bits(fbits(dx), fbits(dx)), fbits(dz), fbits(dz));
    return w->sqrt(w->context, bfloat(sum));
}

/* 001775E0(p, y, wide) (em_player_helper_001775E0). */
static int lip_sweep(const EmPlayerRecoveryLedge *l, float y, int wide, const EmPlayerClimbWorkers *w)
{
    const EmPlayerHelperCalls c = helper_calls(w);
    uint32_t local[4];
    int result = 0;
    FAULT(em_player_helper_001775E0(&c, l, fbits(y), wide, scratch_38A0(w, local), &result));
    return result;
}

/* 00177F40(y): the ledge top 1.5 behind the face is solid. */
static int depth_test(const EmPlayerRecoveryLedge *l, float y, const EmPlayerClimbWorkers *w)
{
    const float base[3] = { sub(l->point[0], mul(1.5f, l->normal[0])), y,
                            sub(l->point[2], mul(1.5f, l->normal[2])) };
    static const float up[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    static const float down[4] = { 0.0f, -1.0f, 0.0f, 1.0f };
    float from[4], to[4];
    FAULT(yaw_point(l->heading, base, up, from));
    FAULT(yaw_point(l->heading, base, down, to));
    store_38A0(w, up);                                                 /* 0x700038A0 */
    if (!w->segment) return -1;
    int kind = w->segment(w->context, from, to, 6, 0);
    if (kind < 0) return -1;
    return kind != 0;
}

/* 00177460(p, far): +2F2 = 1 when the grab point is beyond 5 (8 for far). */
static int vault_test(EmPlayerClimbActor *a, int far, const EmPlayerClimbWorkers *w)
{
    float limit = far == 0 ? 5.0f : 8.0f;
    int fault = 0;
    float d = distance(sub(a->velocity[0], a->position[0]), sub(a->velocity[2], a->position[2]),
                       w, &fault);
    if (fault) return -1;
    a->running = le(d, limit) ? 0 : 1;
    return a->running;
}

/* 001776E0(p, y) (em_player_helper_001776E0): the hit mask, else 0 when a
 * side sweep is blocked, else -2 (the original -1). */
static int high_sides(const EmPlayerRecoveryLedge *l, float y, const EmPlayerClimbWorkers *w)
{
    const EmPlayerHelperCalls c = helper_calls(w);
    uint32_t local[4];
    int result = 0;
    FAULT(em_player_helper_001776E0(&c, l, fbits(y), scratch_38A0(w, local), &result));
    return result == -1 ? -2 : result;
}

/* 00177CF0(p, y) (em_player_helper_00177CF0). */
static int high_hands(const EmPlayerRecoveryLedge *l, float y, const EmPlayerClimbWorkers *w)
{
    const EmPlayerHelperCalls c = helper_calls(w);
    uint32_t local[4];
    int result = 0;
    FAULT(em_player_helper_00177CF0(&c, l, fbits(y), scratch_38A0(w, local), &result));
    return result;
}

/* One 0019AD00 ledge probe at `target`: -1 fault, 0 rejected (the routine
 * returns 0), 1 no hit (distance 50), 2 a hit with its distance and error. */
static int ledge_probe(EmPlayerClimbActor *a, const float target[4], float ang,
                       EmPlayerRecoveryLedge *l, const EmPlayerClimbWorkers *w, float *heading,
                       float *error, float *dist)
{
    EmPlayerClimbHit hit;
    int kind = move(a, target, w, &hit);
    if (kind < 0) return -1;
    if (kind & 1) return 0;
    if (kind == 0) { *dist = 50.0f; return 1; }
    if (!face_gate(&hit)) return 0;
    FAULT(capture(l, &hit, w));
    *heading = l->heading;
    float wrapped;
    FAULT(wrap(sub(l->heading, ang), &wrapped));
    *error = fabs_0011DF78(wrapped);
    const float dx = sub(l->point[0], a->position[0]);
    store_3A20(w, dx);                                                 /* 0x70003A20 */
    int fault = 0;
    *dist = distance(dx, sub(l->point[2], a->position[2]), w, &fault);
    return fault ? -1 : 2;
}

static void commit(EmPlayerClimbActor *a, const EmPlayerRecoveryLedge *l, float dy, int variant)
{
    a->ledge = dy;
    a->rotation[1] = l->heading;
    a->aim = l->heading;
    a->variant = (uint8_t)variant;
    a->lock = 1;
}

/* 0015DF10(p, mode, ang). */
int em_player_climb_probe(EmPlayerClimbActor *a, const EmPlayerClimbScene *s, int mode,
                          float ang, const EmPlayerClimbWorkers *w)
{
    if ((s->flags & 1) && !(s->flags & 0x80)) return 0;
    if (a->link_kind == 2 && !lt(a->position[1], 60.0f)) return 0;
    float local[4] = { 0.0f, 18.0f, 6.0f, 1.0f };                        /* D_002489F0 */
    if (mode == 0) {
        a->running = 1;
        float scale = a->tier < 2 ? 2.0f : a->tier == 2 ? 4.0f : 6.0f;
        local[2] = mul(local[2], mul(0.75f, scale));
    } else {
        a->running = 0;
        local[2] = 5.0f;
    }
    store_38A0(w, local);                                              /* 00102948 + the z store */
    float v[4];
    FAULT(apply(a->matrix, local, v));
    EmPlayerRecoveryLedge ledge;
    memset(&ledge, 0, sizeof ledge);
    float h0 = 0.0f, e0 = 0.0f, d0 = 0.0f, h1 = 0.0f, e1 = 0.0f, d1 = 0.0f;
    int r = ledge_probe(a, v, ang, &ledge, w, &h0, &e0, &d0);
    if (r <= 0) return r;
    v[1] = sub(v[1], 13.99f);
    r = ledge_probe(a, v, ang, &ledge, w, &h1, &e1, &d1);
    if (r <= 0) return r;
    if (eq(d0, 50.0f) && eq(d1, 50.0f)) return 0;
    float lift;
    if (!lt(d0, d1)) {
        if (!le(e1, 0.5235988f)) return 0;
        a->rotation[1] = h1;
        lift = 4.01f;
    } else {
        if (!le(e0, 0.5235988f)) return 0;
        a->rotation[1] = h0;
        lift = 18.0f;
    }
    FAULT(em_player_climb_trs(a));
    float target[4];
    FAULT(apply(a->matrix, local, target));
    target[1] = add(lift, a->position[1]);
    EmPlayerClimbHit hit;
    int kind = move(a, target, w, &hit);
    if (kind < 0) return -1;
    if (kind & 1) return 0;
    if (kind != 0) {
        if (!face_gate(&hit)) return 0;
        FAULT(capture(&ledge, &hit, w));
        float wrapped;
        FAULT(wrap(sub(ledge.heading, ang), &wrapped));
        const float error = fabs_0011DF78(wrapped);
        store_3A20(w, error);                                          /* 0x70003A20 */
        if (!le(error, 0.5235988f)) return 0;
    }
    const float at[4] = { sub(ledge.point[0], mul(0.5f, ledge.normal[0])), ledge.point[1],
                          sub(ledge.point[2], mul(0.5f, ledge.normal[2])), 1.0f };
    store_38A0(w, at);
    EmPlayerClimbTable table;
    memset(&table, 0, sizeof table);
    if (!w->table) return -1;
    FAULT(w->table(w->context, at, &table));
    if (table.count > EM_PLAYER_CLIMB_TABLE_MAX) return -1;
    int found = 0;
    float last = 0.0f;
    for (int i = table.count - 1; i >= 0; --i) {
        if (!(table.flags[i] & 1)) {
            last = table.height[i];
            found = 1;
            continue;
        }
        if ((em_player_helper_0019A180(&table, i) & 0xFF) == 0x46) continue;
        float y = table.height[i];
        float dy = sub(y, a->position[1]);
        if (le(dy, 4.01f)) continue;                                   /* D_002488A0 */
        if (!le(dy, 32.0f)) continue;                                  /* D_002488B4 */
        if (!lt(table.aux[i], 0.62831855f)) continue;
        int lip = lip_sweep(&ledge, y, 1, w);
        if (lip < 0) return -1;
        if (lip) continue;
        if (le(dy, 24.0f)) {                                           /* D_002488B0 */
            if (found && lt(sub(last, y), 14.0f)) continue;
            const float lip_base[4] = { ledge.point[0], y, ledge.point[2], 1.0f };
            static const float over[4] = { 0.0f, 4.01f, 4.5f, 0.0f };
            store_38A0(w, lip_base);
            float b[4];
            FAULT(ledge_offset(&ledge, lip_base, over, b));
            kind = sweep(lip_base, b, 7, w, &hit);
            if (kind < 0) return -1;
            if (kind != 0) {
                if (!hit.probe.entity) continue;
                if ((hit.probe.entity_flags & 0x1F) != 4) continue;
                if (!hit.pickup_box) continue;
            }
            float top[4];
            memcpy(top, b, sizeof top);                                /* 00102948 */
            store_38A0(w, top);
            static const float plus[4] = { 4.0f, 0.0f, -0.5f, 0.0f };
            static const float minus[4] = { -4.0f, 0.0f, -0.5f, 0.0f };
            float right[4], left[4];
            FAULT(ledge_offset(&ledge, top, plus, right));
            FAULT(ledge_offset(&ledge, top, minus, left));
            kind = sweep(right, left, 7, w, &hit);
            if (kind < 0) return -1;
            if (kind != 0) continue;
            kind = sweep(left, right, 7, w, &hit);
            if (kind < 0) return -1;
            if (kind != 0) continue;
            int deep = depth_test(&ledge, y, w);
            if (deep < 0) return -1;
            if (!deep) continue;
            float below = sub(y, 4.0f);
            const float feet[4] = { a->position[0], below, a->position[2], 0.0f };
            store_38A0(w, feet);
            int covered = column(feet, 18.0f, w);
            if (covered < 0) return -1;
            if (covered) continue;
            static const float right_side[4] = { 4.5f, 0.0f, 0.0f, 1.0f };
            static const float left_side[4] = { -4.5f, 0.0f, 0.0f, 1.0f };
            float side[4];
            FAULT(apply(a->matrix, right_side, side));
            side[1] = below;
            store_38A0(w, side);
            covered = column(side, 18.0f, w);
            if (covered < 0) return -1;
            if (covered) continue;
            FAULT(apply(a->matrix, left_side, side));
            side[1] = below;
            store_38A0(w, side);
            covered = column(side, 18.0f, w);
            if (covered < 0) return -1;
            if (covered) continue;
            a->velocity[0] = add(ledge.point[0], mul(4.5f, ledge.normal[0]));
            a->velocity[2] = add(ledge.point[2], mul(4.5f, ledge.normal[2]));
            commit(a, &ledge, dy, 0);
            int far = a->running ? vault_test(a, 0, w) : 0;
            if (far < 0) return -1;
            if (far) {
                a->state = 3; a->walk = 0; a->mode = 9;
                a->velocity[0] = ledge.point[0];
                a->velocity[2] = ledge.point[2];
                a->ledge_normal[0] = ledge.normal[0];
                a->ledge_normal[1] = ledge.normal[2];
            } else {
                a->state = 2; a->walk = 0; a->mode = 8;
            }
            return 1;
        }
        if (mode != 0) continue;
        int sides = high_sides(&ledge, y, w);
        if (sides == -1) return -1;
        if (sides != 0) continue;
        int hands = high_hands(&ledge, sub(y, 1.0f), w);
        if (hands < 0) return -1;
        if (hands) continue;
        float reach = add(2.0f, sub(y, a->position[1]));
        store_3A20(w, reach);                                          /* 0x70003A20 */
        const float feet[4] = { a->position[0], a->position[1], a->position[2], 1.0f };
        int covered = column(feet, reach, w);
        if (covered < 0) return -1;
        if (covered) continue;
        const float ahead[4] = { add(ledge.point[0], ledge.normal[0]), a->position[1],
                                 add(ledge.point[2], ledge.normal[2]), 1.0f };
        store_38A0(w, ahead);
        covered = column(ahead, reach, w);
        if (covered < 0) return -1;
        if (covered) continue;
        a->velocity[0] = add(ledge.point[0], mul(1.5f, ledge.normal[0]));
        a->velocity[2] = add(ledge.point[2], mul(1.5f, ledge.normal[2]));
        commit(a, &ledge, dy, 1);
        int far = a->running ? vault_test(a, 1, w) : 0;
        if (far < 0) return -1;
        if (far) {
            a->state = 3; a->walk = 0; a->mode = 9;
            a->ledge_normal[0] = ledge.normal[0];
            a->ledge_normal[1] = ledge.normal[2];
        } else {
            a->state = 2; a->walk = 0; a->mode = 8;
        }
        return 1;
    }
    return 0;
}

/* 0017F320 (em_player_helper_0017F320). */
int em_player_climb_hang_clear(const EmPlayerClimbActor *a, const EmPlayerClimbWorkers *w)
{
    const EmPlayerHelperCalls c = helper_calls(w);
    uint32_t m[16], local[4];
    vec_bits(m, a->matrix, 16);
    int result = 0;
    FAULT(em_player_helper_0017F320(&c, m, scratch_38A0(w, local), &result));
    return result;
}

/* 00161690: AREA11's corrected grab point for one ledge (D_00810700 == 0xB). */
static void area11_target(EmPlayerClimbActor *a, const EmPlayerClimbScene *s)
{
    if (s->area != 0xB) return;
    float top = add(a->position[1], a->ledge);
    if (lt(top, 285.0f) || !le(top, 587.0f)) return;
    if (lt(a->velocity[0], 400.0f) || !le(a->velocity[0], 410.0f)) return;
    if (lt(a->velocity[2], 236.0f) || !le(a->velocity[2], 246.0f)) return;
    a->velocity[0] = 405.0f;          /* 0x43CA8000 */
    a->velocity[2] = 241.0f;          /* 0x43710000 */
    a->rotation[1] = 0x1.7750ccp+0f;      /* 0x3FBBA866 */
}

/* 001281C0 float_to_int (em_player_float_to_int). */
static int16_t to_counter(float value)
{
    return (int16_t)em_player_float_to_int(fbits(value));
}

/* 0017D800: the rise toward the grab point over +28 ticks (the tick count
 * is kept in 0x70003A20). */
static void rise_setup(EmPlayerClimbActor *a, const EmPlayerClimbWorkers *w)
{
    a->velocity[1] = 0.600000023841857910f;                             /* 0x3F19999A */
    float g = divide(a->target_y, a->velocity[1]);
    if (lt(g, 1.0f)) g = 1.0f;
    store_3A20(w, g);
    a->target_y = add(a->target_y, a->position[1]);
    a->counter = to_counter(g);
    a->goal[0] = a->velocity[0];
    a->goal[1] = a->velocity[2];
    a->velocity[0] = divide(sub(a->goal[0], a->position[0]), g);
    a->velocity[2] = divide(sub(a->goal[1], a->position[2]), g);
    a->lock = 1;
}

/* 0017D8D0. */
static int rise_step(EmPlayerClimbActor *a)
{
    int16_t counter = a->counter;
    a->counter = (int16_t)(counter - 1);
    if (counter == 0) {
        a->position[1] = a->target_y;
        a->position[0] = a->goal[0];
        a->position[2] = a->goal[1];
        return 1;
    }
    a->position[1] = add(a->position[1], a->velocity[1]);
    a->position[0] = add(a->position[0], a->velocity[0]);
    a->position[2] = add(a->position[2], a->velocity[2]);
    return 0;
}

static int request(const EmPlayerClimbWorkers *w, int clip, float blend)
{
    return w->request ? w->request(w->context, clip, 0, blend) : -1;
}

/* 0017DEB0: the grab sound (00182870 tier 0) and the surface effect. */
static int grab_effect(EmPlayerClimbActor *a, const EmPlayerClimbWorkers *w)
{
    if (!w->land_sound || !w->effect) return -1;
    FAULT(w->land_sound(w->context, 0));
    if (a->surface == 6 || a->surface == 5)
        return w->effect(w->context, 0x80000028u, a->position, a->rotation);
    if (a->depth != 0) {
        const float at[3] = { a->position[0], a->surface_y, a->position[2] };   /* 001031E0 */
        return w->effect(w->context, 0x80000016u, at, a->rotation);
    }
    if (a->puddle == 0)
        return w->effect(w->context, 0x80000011u, a->position, a->rotation);
    return 0;
}

static int worker_actor(int (*fn)(void *, EmPlayerClimbActor *), void *context,
                        EmPlayerClimbActor *a)
{
    return fn ? fn(context, a) : -1;
}

static int worker_arg(int (*fn)(void *, EmPlayerClimbActor *, int), void *context,
                      EmPlayerClimbActor *a, int arg)
{
    return fn ? fn(context, a, arg) : -1;
}

static int floor_service(EmPlayerClimbActor *a, const EmPlayerClimbWorkers *w, int *result)
{
    *result = 0;
    return w->floor ? w->floor(w->context, a, 1, result) : -1;
}

/* Sub-states 12 and 22: the pull-up onto the ledge. */
static int pull_up(EmPlayerClimbActor *a, const EmPlayerClimbWorkers *w, float drop,
                   int clip, int one)
{
    a->lock = 0;
    float hip_y = 0.0f, hip_8 = 0.0f;
    if (!w->skeleton) return -1;
    FAULT(w->skeleton(w->context, a, &hip_y, &hip_8));
    a->position[1] = sub(hip_y, drop);
    a->speed = one ? add(1.0f, hip_8) : hip_8;
    FAULT(worker_arg(w->translate, w->context, a, 1));
    FAULT(request(w, clip, 0.0f));
    a->position[1] = add(a->position[1], -0.4f);
    int contact = 0;
    FAULT(floor_service(a, w, &contact));
    if (contact != 0) {
        a->walk = (uint8_t)(a->walk + 1);
        return 1;
    }
    a->goal[0] = a->position[1];
    a->state = 7;
    a->walk = 0;
    a->mode = 0xD;
    return 0;
}

/* Sub-states 13 and 23: stand, or hand on to the walk. */
static int stand(EmPlayerClimbActor *a, const EmPlayerClimbWorkers *w, int probes_first)
{
    FAULT(worker_arg(w->heading, w->context, a, 0));
    if (a->gait > 1) {
        a->walk = (uint8_t)(a->walk + 1);
        FAULT(worker_arg(w->reentry, w->context, a, 1));
    } else if (probes_first) {
        FAULT(worker_actor(w->probes, w->context, a));
        a->tier = 0;
        FAULT(worker_actor(w->handoff, w->context, a));
    } else {
        a->tier = 0;
        FAULT(worker_actor(w->handoff, w->context, a));
        FAULT(worker_actor(w->probes, w->context, a));
    }
    a->position[1] = add(a->position[1], -0.2f);
    int contact = 0;
    FAULT(floor_service(a, w, &contact));
    if (!w->land_sound) return -1;
    return w->land_sound(w->context, 1);
}

/* Sub-states 14 and 24: the walk hand-off. */
static int walk_on(EmPlayerClimbActor *a, const EmPlayerClimbWorkers *w)
{
    FAULT(worker_arg(w->translate, w->context, a, 1));
    if (!(a->anim_flags & 0x8000)) FAULT(worker_actor(w->handoff, w->context, a));
    a->position[1] = add(a->position[1], -0.2f);
    int contact = 0;
    FAULT(floor_service(a, w, &contact));
    return worker_actor(w->fall, w->context, a);
}

/* D_00248540 / D_00248550 by +2F1, D_002754A0 grab clips (four entries
 * each; +2F1 is only ever set to 0..2). */
static const float kGrabClock[4] = { 13.0f, 11.0f, 9.0f, 0.0f };
static const float kSlowClock[4] = { 20.0f, 26.0f, 32.0f, 0.0f };
static const int16_t kGrabClip[4] = { 119, 120, 121, 0 };

/* 00161790. */
int em_player_climb_tick(EmPlayerClimbActor *a, const EmPlayerClimbScene *s,
                         const EmPlayerClimbWorkers *w)
{
    uint8_t walk = a->walk;
    switch (walk) {
    case 0: {
        area11_target(a, s);
        float h = a->ledge;
        if (lt(24.0f, h)) {
            a->walk = 0x1E;
            a->target_y = sub(h, 20.5f);
        } else if (lt(17.0f, h)) {
            a->walk = 0x14; a->target_y = sub(h, 17.0f); a->height_class = 2;
        } else if (lt(12.0f, h)) {
            a->walk = 0x14; a->target_y = sub(h, 12.0f); a->height_class = 1;
        } else if (lt(7.0f, h)) {
            a->walk = 0x14; a->target_y = sub(h, 7.0f); a->height_class = 0;
        } else {
            a->walk = 0xA;
            a->target_y = add(1.0f, sub(h, 4.01f));
        }
        rise_setup(a, w);
        FAULT(request(w, 0x70, 0.0f));
        break;
    }
    case 10:
        if (le(a->clock, 14.0f)) {
            a->walk = (uint8_t)(walk + 1);
            FAULT(request(w, a->tier == 3 ? 0x8B : 0x8A, 1.0f));
            FAULT(grab_effect(a, w));
        }
        break;
    case 11:
        if (rise_step(a)) a->walk = (uint8_t)(a->walk + 1);
        break;
    case 12:
        if (a->anim_flags & 0x1000) FAULT(pull_up(a, w, 10.0f, 0x8D, 1));
        break;
    case 13:
        FAULT(stand(a, w, 0));
        break;
    case 14:
        FAULT(walk_on(a, w));
        break;
    case 20:
        if (a->height_class > 3) return -1;
        if (le(a->clock, kGrabClock[a->height_class])) {
            a->walk = (uint8_t)(walk + 1);
            FAULT(request(w, kGrabClip[a->height_class], 8.0f));
            FAULT(grab_effect(a, w));
            if (!w->sound) return -1;
            FAULT(w->sound(w->context, a->height_class < 2 ? 0x12B : 0x12C));
        }
        if (s->area == 2) FAULT(worker_actor(w->probes, w->context, a));
        break;
    case 21:
        if (rise_step(a)) a->walk = (uint8_t)(a->walk + 1);
        if (s->area == 2) FAULT(worker_actor(w->probes, w->context, a));
        break;
    case 22:
        if (a->anim_flags & 0x1000) {
            FAULT(pull_up(a, w, 11.0f, 0x8C, 0));
            const float feet[4] = { a->position[0], a->position[1], a->position[2], 1.0f };
            int covered = column(feet, 18.0f, w);
            if (covered < 0) return -1;
            if (covered) {
                a->special = 1;
                a->row |= 2;
            }
            break;
        }
        if (a->height_class > 3) return -1;
        if (le(a->clock, kSlowClock[a->height_class])) a->rate = 0.75f;
        if (s->area == 2) FAULT(worker_actor(w->probes, w->context, a));
        break;
    case 23:
        FAULT(stand(a, w, 1));
        break;
    case 24:
        FAULT(walk_on(a, w));
        break;
    case 30:
        if (a->anim_flags & 0x1000) {
            a->walk = (uint8_t)(walk + 1);
            FAULT(request(w, 0x71, 1.0f));
            FAULT(grab_effect(a, w));
        }
        break;
    case 31:
        if (rise_step(a)) {
            int clear = em_player_climb_hang_clear(a, w);
            if (clear < 0) return -1;
            if (clear) {
                a->walk = 0x21;
                break;
            }
            a->walk = (uint8_t)(a->walk + 1);
            if (!w->sound) return -1;
            FAULT(w->sound(w->context, 0xFE));
            FAULT(request(w, 0x7A, 1.0f));
        }
        break;
    case 32: {
        int clear = em_player_climb_hang_clear(a, w);
        if (clear < 0) return -1;
        if (clear) {
            a->walk = 0x21;
            break;
        }
        if (a->anim_flags & 0x1000) {
            a->state = 9;
            a->walk = 0;
            a->mode = 0x10;
            a->hang = 0;
            FAULT(request(w, em_player_helper_00188550(a->row), 16.0f));
        }
        break;
    }
    case 33:
        a->goal[0] = a->position[1];
        a->state = 7;
        a->walk = 0;
        a->mode = 0xD;
        a->drop = -0.2f;
        break;
    default:
        break;
    }
    return 0;
}

/* ---- vault: state 3 ------------------------------------------------------ */

/* 00162080: AREA11's corrected vault target (D_00810700 == 0xB). */
static void area11_vault_target(EmPlayerClimbActor *a, const EmPlayerClimbScene *s)
{
    if (s->area != 0xB) return;
    float top = add(a->position[1], a->ledge);
    if (lt(top, 285.0f) || !le(top, 587.0f)) return;
    if (lt(a->velocity[0], 397.0f) || !le(a->velocity[0], 427.0f)) return;
    if (lt(a->velocity[2], 227.0f) || !le(a->velocity[2], 257.0f)) return;
    a->velocity[0] = 411.0f;          /* 0x43CD8000 */
    a->velocity[2] = 240.0f;          /* 0x43700000 */
    a->rotation[1] = 0x1.657186p+0f;  /* 0x3FB2B8C3 */
}

/* 0017D940 (form 0), 0017DAF0 (form 1), 0017DC80 (form 2): the vault arc
 * toward the grab point pushed out along the ledge normal. */
static int vault_setup(EmPlayerClimbActor *a, int form, const EmPlayerClimbWorkers *w)
{
    a->goal[0] = a->velocity[0];
    a->goal[1] = a->velocity[2];
    float reach, pace = 0.8f;
    if (form == 1) {
        reach = 4.1f;
    } else if (a->tier == 3) {
        reach = 6.3f;
        if (form == 0) pace = 0.9f;
    } else {
        reach = 4.1f;
    }
    float dx = sub(add(a->goal[0], mul(a->ledge_normal[0], reach)), a->position[0]);
    float dz = sub(add(a->goal[1], mul(a->ledge_normal[1], reach)), a->position[2]);
    store_3A20(w, dx);                               /* 0x70003A20 (dz and the ticks: 3A24, 3A28) */
    int fault = 0;
    float d = distance(dx, dz, w, &fault);
    if (fault) return -1;
    float ticks = divide(d, pace);
    float least = form == 0 ? 8.0f : 12.0f;
    if (lt(ticks, least)) ticks = least;
    a->counter = to_counter(sub(ticks, 1.0f));
    a->velocity[0] = divide(dx, ticks);
    a->velocity[2] = divide(dz, ticks);
    if (form == 1)
        a->velocity[1] = divide(sub(a->target_y, 4.0f), sub(ticks, 8.0f));
    else
        a->velocity[1] = divide(a->target_y, ticks);
    a->target_y = add(a->target_y, a->position[1]);
    a->push = form == 2 ? 1.0f : 0.600000023841857910f;
    float half = form == 1 ? divide(sub(ticks, 8.0f), 2.0f) : divide(ticks, 2.0f);
    a->push_decay = divide(a->push, half);
    return 0;
}

/* 0017DE20. */
static int vault_step(EmPlayerClimbActor *a)
{
    int16_t counter = a->counter;
    a->counter = (int16_t)(counter - 1);
    if (counter == 0) {
        a->position[0] = a->goal[0];
        a->position[2] = a->goal[1];
        a->position[1] = a->target_y;
        return 1;
    }
    a->position[0] = add(a->position[0], a->velocity[0]);
    a->position[2] = add(a->position[2], a->velocity[2]);
    a->position[1] = add(a->position[1], a->velocity[1]);
    a->position[1] = add(a->position[1], a->push);
    a->push = sub(a->push, a->push_decay);
    return 0;
}

static int vault_launch(EmPlayerClimbActor *a, const EmPlayerClimbWorkers *w)
{
    if (a->tier == 3) {
        a->speed = 6.3f;
        FAULT(request(w, 0x6B, 0.0f));
    } else {
        a->speed = 4.1f;
        FAULT(request(w, 0x6C, 0.0f));
    }
    return worker_arg(w->translate, w->context, a, 1);
}

/* 00162190. */
int em_player_climb_vault_tick(EmPlayerClimbActor *a, const EmPlayerClimbScene *s,
                               const EmPlayerClimbWorkers *w)
{
    uint8_t walk = a->walk;
    int contact = 0;
    switch (walk) {
    case 0:
        area11_vault_target(a, s);
        if (a->variant == 0) {
            a->target_y = a->ledge;
            if (lt(a->ledge, 7.0f)) {                                   /* D_002488A4 */
                a->walk = 0xA;
                FAULT(vault_setup(a, 0, w));
            } else {
                a->walk = 0x14;
                a->tier = 1;
                FAULT(vault_setup(a, 1, w));
            }
        } else {
            a->walk = 0x1E;
            a->target_y = sub(a->ledge, 20.5f);
            FAULT(vault_setup(a, 2, w));
        }
        if (a->tier == 3) {
            int frames = 0;
            if (!w->clip_frames || !w->arbiter) return -1;
            FAULT(w->clip_frames(w->context, 0x69, &frames));
            const float length = em_ee_cvt_s_w(frames);
            store_3A20(w, length);                                     /* 0x70003A20 */
            FAULT(w->arbiter(w->context, 0x69, 0.0f, sub(length, 4.0f)));
        } else {
            FAULT(request(w, 0x6A, 0.0f));
        }
        FAULT(grab_effect(a, w));
        break;
    case 10:
    case 30:
        if (a->anim_flags & 0x1000) {
            a->walk = (uint8_t)(walk + 1);
            FAULT(vault_launch(a, w));
        }
        break;
    case 11: {
        int16_t counter = a->counter;
        a->counter = (int16_t)(counter - 1);
        if (counter == 0) {
            a->walk = (uint8_t)(a->walk + 1);
            a->position[1] = a->target_y;
            a->position[0] = a->goal[0];
            a->position[2] = a->goal[1];
            a->rotation[1] = a->aim;
        } else {
            a->position[1] = add(a->position[1], a->velocity[1]);
            a->position[0] = add(a->position[0], a->velocity[0]);
            a->position[2] = add(a->position[2], a->velocity[2]);
            a->position[1] = add(a->position[1], a->push);
            a->push = sub(a->push, a->push_decay);
        }
        if (a->tier == 3) a->rate = 2.0f;
        break;
    }
    case 12:
        a->position[1] = add(a->position[1], -0.2f);
        FAULT(floor_service(a, w, &contact));
        if (contact != 0) {
            a->goal[0] = add(1.0f, a->position[1]);
            FAULT(worker_actor(w->land, w->context, a));
            break;
        }
        a->goal[0] = a->position[1];
        a->state = 7;
        a->walk = 0;
        a->mode = 0xD;
        break;
    case 20:
        if (a->anim_flags & 0x1000) {
            a->walk = (uint8_t)(walk + 1);
            a->speed = 4.1f;
            FAULT(worker_arg(w->translate, w->context, a, 1));
            FAULT(request(w, 0x6C, 0.0f));
            break;
        }
        if (s->area == 2) FAULT(worker_actor(w->probes, w->context, a));
        break;
    case 21:
        a->counter = (int16_t)(a->counter - 1);
        a->position[0] = add(a->position[0], a->velocity[0]);
        a->position[2] = add(a->position[2], a->velocity[2]);
        a->position[1] = add(a->position[1], a->velocity[1]);
        a->position[1] = add(a->position[1], a->push);
        a->push = sub(a->push, a->push_decay);
        if (a->counter < 9) {
            a->walk = (uint8_t)(a->walk + 1);
            FAULT(request(w, 0x7D, 8.0f));
            a->rotation[1] = a->aim;
        } else {
            a->rate = 2.0f;
        }
        if (s->area == 2) FAULT(worker_actor(w->probes, w->context, a));
        break;
    case 22: {
        int16_t counter = a->counter;
        a->counter = (int16_t)(counter - 1);
        if (counter == 0) {
            a->position[0] = a->goal[0];
            a->position[2] = a->goal[1];
            a->position[1] = a->target_y;
            a->position[1] = add(a->position[1], -0.2f);
            FAULT(floor_service(a, w, &contact));
            if (contact != 0) {
                a->walk = (uint8_t)(a->walk + 1);
                if (!w->land_sound) return -1;
                FAULT(w->land_sound(w->context, 1));
                break;
            }
            a->goal[0] = a->position[1];
            a->state = 7;
            a->walk = 0;
            a->mode = 0xD;
            break;
        }
        a->position[0] = add(a->position[0], a->velocity[0]);
        a->position[2] = add(a->position[2], a->velocity[2]);
        a->position[1] = add(a->position[1], 0.5f);
        break;
    }
    case 23:
        if (a->anim_flags & 0x1000) {
            /* 00102948 is an lq/sq pair: all four lanes, so +BC takes the node's w. */
            for (int i = 0; i < 4; ++i) a->position[i] = s->hip_world[i];
            a->position[1] = sub(a->position[1], 11.0f);
            FAULT(request(w, 0x8C, 0.0f));
            const float feet[4] = { a->position[0], a->position[1], a->position[2], a->position[3] };
            int covered = column(feet, 18.0f, w);
            if (covered < 0) return -1;
            if (covered) {
                a->special = 1;
                a->row |= 2;
            }
            FAULT(worker_arg(w->heading, w->context, a, 0));
            if (a->gait > 1) {
                a->walk = (uint8_t)(a->walk + 1);
                FAULT(worker_arg(w->reentry, w->context, a, 1));
            } else {
                a->tier = 0;
                FAULT(worker_actor(w->handoff, w->context, a));
                FAULT(worker_actor(w->probes, w->context, a));
            }
            a->position[1] = add(a->position[1], -0.2f);
            FAULT(floor_service(a, w, &contact));
            FAULT(worker_actor(w->fall, w->context, a));
            break;
        }
        if (s->area == 2) FAULT(worker_actor(w->probes, w->context, a));
        break;
    case 24:
        FAULT(walk_on(a, w));
        break;
    case 31:
        (void)vault_step(a);
        if (a->counter < 9) {
            a->walk = (uint8_t)(a->walk + 1);
            FAULT(request(w, 0x7A, 8.0f));
            a->rotation[1] = a->aim;
            break;
        }
        if (a->tier == 3) a->rate = 3.0f;
        break;
    case 32:
        if (vault_step(a)) {
            int clear = em_player_climb_hang_clear(a, w);
            if (clear < 0) return -1;
            if (clear) {
                a->walk = 0x22;
                break;
            }
            a->walk = (uint8_t)(a->walk + 1);
            if (!w->sound) return -1;
            FAULT(w->sound(w->context, 0xFF));
        }
        break;
    case 33: {
        int clear = em_player_climb_hang_clear(a, w);
        if (clear < 0) return -1;
        if (clear) {
            a->walk = 0x22;
            break;
        }
        if (a->anim_flags & 0x1000) {
            a->state = 9;
            a->walk = 0;
            a->mode = 0x10;
            a->hang = 0;
            FAULT(request(w, em_player_helper_00188550(a->row), 16.0f));
        }
        break;
    }
    case 34:
        a->goal[0] = a->position[1];
        a->state = 7;
        a->walk = 0;
        a->mode = 0xD;
        a->drop = -0.2f;
        break;
    default:
        break;
    }
    return 0;
}

/* ---- The live climb states ------------------------------------------------ */

static void climb_vec(const EmPlayerLiveActor *live, unsigned at, float *out, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) out[i] = em_live_f32(live, at + 4 * i);
}

static void climb_set_vec(EmPlayerLiveActor *live, unsigned at, const float *in, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) em_live_set_f32(live, at + 4 * i, in[i]);
}

void em_player_climb_actor_from_live(const EmPlayerLiveActor *live, EmPlayerClimbActor *a)
{
    memset(a, 0, sizeof *a);
    climb_vec(live, 0xB0, a->position, 4);
    climb_vec(live, 0xC0, a->rotation, 3);
    climb_vec(live, 0x60, a->scale, 3);
    climb_vec(live, 0xD0, a->matrix, 16);
    a->speed = em_live_f32(live, 0x38);
    a->clock = em_live_f32(live, 0x3C);
    a->rate = em_live_f32(live, 0x204);
    a->ledge = em_live_f32(live, 0x254);
    a->target_y = em_live_f32(live, 0x258);
    climb_vec(live, 0x2E0, a->velocity, 3);
    climb_vec(live, 0x2F4, a->goal, 2);
    a->drop = em_live_f32(live, 0x2EC);
    a->aim = em_live_f32(live, 0x218);
    a->ledge_normal[0] = em_live_f32(live, 0x290);
    a->ledge_normal[1] = em_live_f32(live, 0x298);
    a->surface_y = em_live_f32(live, 0x250);
    a->push = em_live_f32(live, 0x26C);
    a->push_decay = em_live_f32(live, 0x270);
    a->anim_flags = em_live_u32(live, 0x200);
    a->counter = (int16_t)em_live_u16(live, 0x28);
    a->major = em_live_u8(live, 4);
    a->state = em_live_u8(live, 5);
    a->walk = em_live_u8(live, 6);
    a->hang = em_live_u8(live, 0xD);
    a->mode = em_live_u8(live, 0x1F0);
    a->variant = em_live_u8(live, 0x1F1);
    a->lock = em_live_u8(live, 0x25F);
    a->running = em_live_u8(live, 0x2F2);
    a->height_class = em_live_u8(live, 0x2F1);
    a->tier = em_live_u8(live, 0x25C);
    a->surface = em_live_u8(live, 0x23A);
    a->depth = em_live_u8(live, 0x23C);
    a->puddle = em_live_u8(live, 0x23D);
    a->row = em_live_u8(live, 0x235);
    a->special = em_live_u8(live, 0x236);
    a->gait = em_live_u8(live, 0x23F);
}

void em_player_climb_actor_to_live(const EmPlayerClimbActor *a, EmPlayerLiveActor *live)
{
    climb_set_vec(live, 0xB0, a->position, 4);
    climb_set_vec(live, 0xC0, a->rotation, 3);
    climb_set_vec(live, 0x60, a->scale, 3);
    climb_set_vec(live, 0xD0, a->matrix, 16);
    em_live_set_f32(live, 0x38, a->speed);
    em_live_set_f32(live, 0x3C, a->clock);
    em_live_set_f32(live, 0x204, a->rate);
    em_live_set_f32(live, 0x254, a->ledge);
    em_live_set_f32(live, 0x258, a->target_y);
    climb_set_vec(live, 0x2E0, a->velocity, 3);
    climb_set_vec(live, 0x2F4, a->goal, 2);
    em_live_set_f32(live, 0x2EC, a->drop);
    em_live_set_f32(live, 0x218, a->aim);
    em_live_set_f32(live, 0x290, a->ledge_normal[0]);
    em_live_set_f32(live, 0x298, a->ledge_normal[1]);
    em_live_set_f32(live, 0x250, a->surface_y);
    em_live_set_f32(live, 0x26C, a->push);
    em_live_set_f32(live, 0x270, a->push_decay);
    em_live_set_u32(live, 0x200, a->anim_flags);
    em_live_set_u16(live, 0x28, (uint16_t)a->counter);
    em_live_set_u8(live, 4, a->major);
    em_live_set_u8(live, 5, a->state);
    em_live_set_u8(live, 6, a->walk);
    em_live_set_u8(live, 0xD, a->hang);
    em_live_set_u8(live, 0x1F0, a->mode);
    em_live_set_u8(live, 0x1F1, a->variant);
    em_live_set_u8(live, 0x25F, a->lock);
    em_live_set_u8(live, 0x2F2, a->running);
    em_live_set_u8(live, 0x2F1, a->height_class);
    em_live_set_u8(live, 0x25C, a->tier);
    em_live_set_u8(live, 0x23A, a->surface);
    em_live_set_u8(live, 0x23C, a->depth);
    em_live_set_u8(live, 0x23D, a->puddle);
    em_live_set_u8(live, 0x235, a->row);
    em_live_set_u8(live, 0x236, a->special);
    em_live_set_u8(live, 0x23F, a->gait);
}

/* The call in progress (one player stage at a time): the binding, the
 * record and the mirror the routine is working on. */
static struct {
    const EmPlayerClimbLive *binding;
    EmPlayerLiveActor *live;
    EmPlayerClimbActor *mirror;
} climb_call;

/* The mirror into the record before a worker runs, and back after (the
 * link kind is derived from +308, not a record byte, and is kept). */
static void flush(void) { em_player_climb_actor_to_live(climb_call.mirror, climb_call.live); }

static void reload(void)
{
    const uint8_t kind = climb_call.mirror->link_kind;
    em_player_climb_actor_from_live(climb_call.live, climb_call.mirror);
    climb_call.mirror->link_kind = kind;
}

#define LIVE (climb_call.binding)
#define W (&climb_call.binding->workers)
#define AROUND(call) do { flush(); int r_ = (call); reload(); return r_; } while (0)

static int t_move(void *c, const float p[3], const float t[4], unsigned m, EmPlayerClimbHit *h)
{ (void)c; AROUND(W->move(W->context, p, t, m, h)); }
static int t_sweep(void *c, const float f[4], const float t[4], unsigned m, EmPlayerClimbHit *h)
{ (void)c; AROUND(W->sweep(W->context, f, t, m, h)); }
static int t_segment(void *c, const float f[4], const float t[4], unsigned m, int id)
{ (void)c; AROUND(W->segment(W->context, f, t, m, id)); }
static int t_column(void *c, const float at[4], float height)
{ (void)c; AROUND(W->column(W->context, at, height)); }
static int t_table(void *c, const float at[4], EmPlayerClimbTable *t)
{ (void)c; AROUND(W->table(W->context, at, t)); }
static float t_atan2(void *c, float y, float x)
{ (void)c; flush(); float r = W->atan2(W->context, y, x); reload(); return r; }
static float t_sqrt(void *c, float x)
{ (void)c; flush(); float r = W->sqrt(W->context, x); reload(); return r; }
static int t_request(void *c, int clip, int force, float blend)
{ (void)c; AROUND(LIVE->request(LIVE->live_context, climb_call.live, clip, force, blend)); }
static int t_arbiter(void *c, int clip, float blend, float frame)
{ (void)c; AROUND(LIVE->arbiter(LIVE->live_context, climb_call.live, clip, blend, frame)); }
static int t_clip_frames(void *c, int clip, int *frames)
{
    (void)c;
    int32_t n = 0;
    flush();
    int r = LIVE->clip_frames(LIVE->live_context, em_live_u32(climb_call.live, 0x40), clip, &n);
    reload();
    *frames = n;
    return r;
}
static int t_sound(void *c, unsigned id)
{ (void)c; AROUND(LIVE->sound(LIVE->live_context, climb_call.live, (int)id)); }
static int t_effect(void *c, uint32_t id, const float p[3], const float r[3])
{ (void)c; AROUND(W->effect(W->context, id, p, r)); }
static int t_land_sound(void *c, int tier)
{ (void)c; AROUND(LIVE->land_sound(LIVE->live_context, climb_call.live, tier)); }
static int t_skeleton(void *c, EmPlayerClimbActor *a, float *hip_y, float *hip_8)
{ (void)c; (void)a; AROUND(LIVE->skeleton(LIVE->live_context, climb_call.live, hip_y, hip_8)); }
static int t_translate(void *c, EmPlayerClimbActor *a, int arg)
{ (void)c; (void)a; AROUND(LIVE->translate(LIVE->live_context, climb_call.live, arg)); }
static int t_floor(void *c, EmPlayerClimbActor *a, int search, int *result)
{ (void)c; (void)a; AROUND(LIVE->floor(LIVE->live_context, climb_call.live, search, result)); }
static int t_probes(void *c, EmPlayerClimbActor *a)
{ (void)c; (void)a; AROUND(LIVE->probes(LIVE->live_context, climb_call.live)); }
static int t_heading(void *c, EmPlayerClimbActor *a, int arg)
{ (void)c; (void)a; AROUND(LIVE->heading(LIVE->live_context, climb_call.live, arg)); }
static int t_reentry(void *c, EmPlayerClimbActor *a, int arg)
{ (void)c; (void)a; AROUND(LIVE->reentry(LIVE->live_context, climb_call.live, arg)); }
static int t_handoff(void *c, EmPlayerClimbActor *a)
{ (void)c; (void)a; AROUND(LIVE->handoff(LIVE->live_context, climb_call.live)); }
static int t_fall(void *c, EmPlayerClimbActor *a)
{ (void)c; (void)a; AROUND(LIVE->fall(LIVE->live_context, climb_call.live)); }
static int t_land(void *c, EmPlayerClimbActor *a)
{ (void)c; (void)a; AROUND(LIVE->land(LIVE->live_context, climb_call.live)); }

#undef AROUND
#undef W
#undef LIVE

/* Every worker bound, and the mirror, scene and link kind prepared. */
static int climb_live_begin(const EmPlayerClimbLive *b, EmPlayerLiveActor *live,
                            EmPlayerClimbActor *actor, EmPlayerClimbScene *scene,
                            EmPlayerClimbWorkers *workers)
{
    if (!b || !live || !b->floor || !b->probes || !b->fall || !b->scene || !b->link_kind ||
        !b->request || !b->arbiter || !b->clip_frames || !b->sound || !b->land_sound ||
        !b->translate || !b->heading || !b->reentry || !b->handoff || !b->land || !b->skeleton ||
        !b->scratch)
        return -1;
    const EmPlayerClimbWorkers *w = &b->workers;
    if (!w->move || !w->sweep || !w->segment || !w->column || !w->table || !w->atan2 ||
        !w->sqrt || !w->effect)
        return -1;
    memset(scene, 0, sizeof *scene);
    if (b->scene(b->scene_context, scene) < 0) return -1;
    em_player_climb_actor_from_live(live, actor);
    int kind = b->link_kind(b->link_context, live->link_prev);   /* +308 */
    if (kind < 0 || kind > 2) return -1;
    actor->link_kind = (uint8_t)kind;
    memset(workers, 0, sizeof *workers);
    workers->context = NULL;
    workers->move = t_move;
    workers->sweep = t_sweep;
    workers->segment = t_segment;
    workers->column = t_column;
    workers->table = t_table;
    workers->atan2 = t_atan2;
    workers->sqrt = t_sqrt;
    workers->request = t_request;
    workers->arbiter = t_arbiter;
    workers->clip_frames = t_clip_frames;
    workers->sound = t_sound;
    workers->effect = t_effect;
    workers->land_sound = t_land_sound;
    workers->skeleton = t_skeleton;
    workers->translate = t_translate;
    workers->floor = t_floor;
    workers->probes = t_probes;
    workers->heading = t_heading;
    workers->reentry = t_reentry;
    workers->handoff = t_handoff;
    workers->fall = t_fall;
    workers->land = t_land;
    workers->scratch = b->scratch;
    climb_call.binding = b;
    climb_call.live = live;
    climb_call.mirror = actor;
    return 0;
}

static void climb_live_end(EmPlayerClimbActor *actor, EmPlayerLiveActor *live)
{
    climb_call.binding = NULL;
    climb_call.live = NULL;
    climb_call.mirror = NULL;
    /* Writes made before a fault stay, as the original order leaves them. */
    em_player_climb_actor_to_live(actor, live);
}

int em_player_climb_live_state(void *context, EmPlayerLiveActor *live)
{
    const EmPlayerClimbLive *b = context;
    EmPlayerClimbActor actor;
    EmPlayerClimbScene scene;
    EmPlayerClimbWorkers workers;
    if (climb_live_begin(b, live, &actor, &scene, &workers) < 0) return -1;
    int result;
    if (actor.state == 2) result = em_player_climb_tick(&actor, &scene, &workers);
    else if (actor.state == 3) result = em_player_climb_vault_tick(&actor, &scene, &workers);
    else result = -1;
    climb_live_end(&actor, live);
    return result;
}

int em_player_climb_live_probe(void *context, EmPlayerLiveActor *live, int mode, float ang)
{
    const EmPlayerClimbLive *b = context;
    EmPlayerClimbActor actor;
    EmPlayerClimbScene scene;
    EmPlayerClimbWorkers workers;
    if (climb_live_begin(b, live, &actor, &scene, &workers) < 0) return -1;
    int result = em_player_climb_probe(&actor, &scene, mode, ang, &workers);
    climb_live_end(&actor, live);
    return result;
}

int em_player_climb_live_ledge(void *context, EmPlayerLiveActor *live, int mode, uint32_t angle,
                               int *result)
{
    if (!result) return -1;
    int r = em_player_climb_live_probe(context, live, mode, bfloat(angle));
    if (r < 0) return -1;
    *result = r;
    return 0;
}
