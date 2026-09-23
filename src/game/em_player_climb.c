/* em_player_climb.c - the player's ledge climb (see em_player_climb.h).
 *
 * Every routine here was read from the original instructions of the named
 * function; tools/test_player_climb_reference.py executes those instructions
 * and compares every field and worker call. The scratchpad ledge frame that
 * 00177510 publishes (0x70003050 point, 0x70003060 normal, 0x700031E4
 * heading, 0x70003070 yaw matrix) is the local Ledge below. */
#include "game/em_player_climb.h"
#include "game/em_effect_color.h"

#include <stddef.h>
#include <string.h>

static float f32_add(float a, float b) { return em_effect_float32((double)a + b); }
static float f32_sub(float a, float b) { return em_effect_float32((double)a - b); }
static float f32_mul(float a, float b) { return em_effect_float32((double)a * b); }
static float f32_div(float a, float b) { return em_effect_float32((double)a / b); }
static float f32_abs(float a) { return a < 0.0f ? -a : a; }                /* 0011DF78 */

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

typedef struct Ledge {
    float point[3];       /* 0x70003050 */
    float normal[3];      /* 0x70003060 */
    float heading;        /* 0x700031E4 */
    float matrix[16];     /* 0x70003070 */
} Ledge;

void em_player_climb_trs(EmPlayerClimbActor *a)
{
    em_player_sdk_trs(a->matrix, a->position, a->rotation, a->scale);
}

/* 0015DEC0: a wall face that is neither attribute 0x46 nor 0x32. */
static int face_gate(const EmPlayerClimbHit *hit)
{
    unsigned attribute = hit->probe.node & 0xFF;
    if (attribute == 0x46 || attribute == 0x32) return 0;
    return (hit->probe.node & 0xFF00) == 0x2000;
}

/* 00177510. */
static int capture(Ledge *ledge, const EmPlayerClimbHit *hit, const EmPlayerClimbWorkers *w)
{
    for (int i = 0; i < 3; ++i) {
        ledge->point[i] = hit->probe.point[i];
        ledge->normal[i] = hit->probe.normal[i];
    }
    if (!w->atan2) return -1;
    float angle = w->atan2(w->context, -ledge->normal[2], ledge->normal[0]);
    ledge->heading = em_player_sdk_wrap(f32_add(4.71238899230957f, angle));
    em_player_sdk_yaw_matrix(ledge->heading, ledge->matrix);
    return 0;
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

static float distance(float dx, float dz, const EmPlayerClimbWorkers *w, int *fault)
{
    if (!w->sqrt) { *fault = 1; return 0.0f; }
    return w->sqrt(w->context, f32_add(f32_mul(dx, dx), f32_mul(dz, dz)));
}

/* 001775E0(p, y, wide): the sweep across the ledge lip. */
static int lip_sweep(const Ledge *l, float y, int wide, const EmPlayerClimbWorkers *w)
{
    float out_scale, lift, in_scale;
    if (wide == 0) { out_scale = 1.0f; in_scale = 0.5f; lift = 1.0f; }
    else { out_scale = 5.0f; lift = 4.01f; in_scale = 5.0f; }
    float height = f32_add(y, lift);
    const float from[4] = { f32_add(l->point[0], f32_mul(l->normal[0], out_scale)), height,
                            f32_add(l->point[2], f32_mul(l->normal[2], out_scale)), 1.0f };
    const float to[4] = { f32_sub(l->point[0], f32_mul(l->normal[0], in_scale)), height,
                          f32_sub(l->point[2], f32_mul(l->normal[2], in_scale)), 1.0f };
    EmPlayerClimbHit hit;
    int kind = sweep(from, to, 7, w, &hit);
    if (kind < 0) return -1;
    return kind != 0;
}

/* 00177F40(y): the ledge top 1.5 behind the face is solid. */
static int depth_test(const Ledge *l, float y, const EmPlayerClimbWorkers *w)
{
    const float base[3] = { f32_sub(l->point[0], f32_mul(1.5f, l->normal[0])), y,
                            f32_sub(l->point[2], f32_mul(1.5f, l->normal[2])) };
    static const float up[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    static const float down[4] = { 0.0f, -1.0f, 0.0f, 1.0f };
    float from[4], to[4];
    em_player_sdk_yaw_transform(l->heading, base, up, from);
    em_player_sdk_yaw_transform(l->heading, base, down, to);
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
    float d = distance(f32_sub(a->velocity[0], a->position[0]),
                       f32_sub(a->velocity[2], a->position[2]), w, &fault);
    if (fault) return -1;
    a->running = d <= limit ? 0 : 1;
    return a->running;
}

/* 001026A0 of the ledge matrix, then 001028B8 (all four lanes) with base. */
static void ledge_offset(const Ledge *l, const float base[4], const float local[4], float out[4])
{
    float turned[4];
    em_player_sdk_apply(l->matrix, local, turned);
    for (int i = 0; i < 4; ++i) out[i] = f32_add(base[i], turned[i]);
}

/* 001776E0(p, y): the side sweeps around a high ledge. Returns the hit
 * mask, else 0 when a side sweep is blocked, else -2 (the original -1). */
static int high_sides(const Ledge *l, float y, const EmPlayerClimbWorkers *w)
{
    static const float plus4[4] = { 4.0f, 0.0f, 0.0f, 1.0f };
    static const float minus4[4] = { -4.0f, 0.0f, 0.0f, 1.0f };
    EmPlayerClimbHit hit;
    int mask = 0;
    for (int pass = 0; pass < 2; ++pass) {
        float scale = pass == 0 ? 0.5f : 1.5f;
        float base[4];
        if (pass == 0) {
            base[0] = f32_sub(l->point[0], f32_mul(scale, l->normal[0]));
            base[2] = f32_sub(l->point[2], f32_mul(scale, l->normal[2]));
            base[1] = f32_add(1.0f, y);
        } else {
            base[0] = f32_add(l->point[0], f32_mul(scale, l->normal[0]));
            base[2] = f32_add(l->point[2], f32_mul(scale, l->normal[2]));
            base[1] = f32_sub(y, 1.0f);
        }
        base[3] = 1.0f;
        float b[4], c[4];
        ledge_offset(l, base, plus4, b); b[3] = 1.0f;
        ledge_offset(l, base, minus4, c); c[3] = 1.0f;
        int kind = sweep(b, c, 7, w, &hit);
        if (kind < 0) return -1;
        if (kind) mask |= 1;
        kind = sweep(c, b, 7, w, &hit);
        if (kind < 0) return -1;
        if (kind) mask |= 2;
        if (mask) return mask;
        if (pass == 1) {
            int side = 0;
            static const float right[4] = { 4.0f, 0.0f, 2.0f, 1.0f };
            static const float left[4] = { -4.0f, 0.0f, 2.0f, 1.0f };
            float to[4];
            ledge_offset(l, base, right, to); to[3] = 1.0f;
            kind = sweep(base, to, 7, w, &hit);
            if (kind < 0) return -1;
            if (kind & 6) side |= 1;
            ledge_offset(l, base, left, to); to[3] = 1.0f;
            kind = sweep(base, to, 7, w, &hit);
            if (kind < 0) return -1;
            if (kind & 6) side |= 2;
            return side ? 0 : -2;
        }
    }
    return 0;
}

/* 00177CF0(p, y): the hand sweeps at a high ledge. */
static int high_hands(const Ledge *l, float y, const EmPlayerClimbWorkers *w)
{
    const float base[3] = { l->point[0], y, l->point[2] };
    for (int side = 0; side < 2; ++side) {
        float x = side == 0 ? -4.5f : 4.5f;
        const float near[4] = { x, 0.0f, -2.0f, 1.0f };
        const float far[4] = { x, 0.0f, 2.0f, 1.0f };
        float from[4], to[4];
        em_player_sdk_yaw_transform(l->heading, base, near, from);
        em_player_sdk_yaw_transform(l->heading, base, far, to);
        EmPlayerClimbHit hit;
        int kind = sweep(from, to, 7, w, &hit);
        if (kind < 0) return -1;
        if (kind & 1) return 1;
        if ((kind & 6) && (hit.probe.node & 0xFF) == 0x32) return 1;
    }
    return 0;
}

/* 0019A180(0, i). */
static int entry_attribute(const EmPlayerClimbTable *t, int i)
{
    if (i >= t->count) return 0;
    if (t->flags[i] & 0x8000) {
        float v = t->aux[i];
        int16_t bits;
        if (v < 0.0f) {
            if (v <= -0.70020753f) bits = v < -1.7320508f ? 0x2000 : 0x800;
            else bits = (int16_t)-0x8000;
        } else if (v < 0.70020753f) {
            bits = 0x4000;
        } else if (v <= 1.7320508f) {
            bits = 0x1000;
        } else {
            bits = 0x2000;
        }
        return (int16_t)(bits | t->object_kind[i]);
    }
    return t->object_node[i];
}

/* One 0019AD00 ledge probe at `target`: -1 fault, 0 rejected (the routine
 * returns 0), 1 no hit (distance 50), 2 a hit with its distance and error. */
static int ledge_probe(EmPlayerClimbActor *a, const float target[4], float ang, Ledge *l,
                       const EmPlayerClimbWorkers *w, float *heading, float *error, float *dist)
{
    EmPlayerClimbHit hit;
    int kind = move(a, target, w, &hit);
    if (kind < 0) return -1;
    if (kind & 1) return 0;
    if (kind == 0) { *dist = 50.0f; return 1; }
    if (!face_gate(&hit)) return 0;
    FAULT(capture(l, &hit, w));
    *heading = l->heading;
    *error = f32_abs(em_player_sdk_wrap(f32_sub(l->heading, ang)));
    int fault = 0;
    *dist = distance(f32_sub(l->point[0], a->position[0]), f32_sub(l->point[2], a->position[2]),
                     w, &fault);
    return fault ? -1 : 2;
}

static void commit(EmPlayerClimbActor *a, const Ledge *l, float dy, int variant)
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
    if (a->link_kind == 2 && !(a->position[1] < 60.0f)) return 0;
    float local[4] = { 0.0f, 18.0f, 6.0f, 1.0f };                        /* D_002489F0 */
    if (mode == 0) {
        a->running = 1;
        float scale = a->tier < 2 ? 2.0f : a->tier == 2 ? 4.0f : 6.0f;
        local[2] = f32_mul(local[2], f32_mul(0.75f, scale));
    } else {
        a->running = 0;
        local[2] = 5.0f;
    }
    float v[4];
    em_player_sdk_apply(a->matrix, local, v);
    Ledge ledge;
    memset(&ledge, 0, sizeof ledge);
    float h0 = 0.0f, e0 = 0.0f, d0 = 0.0f, h1 = 0.0f, e1 = 0.0f, d1 = 0.0f;
    int r = ledge_probe(a, v, ang, &ledge, w, &h0, &e0, &d0);
    if (r <= 0) return r;
    v[1] = f32_sub(v[1], 13.99f);
    r = ledge_probe(a, v, ang, &ledge, w, &h1, &e1, &d1);
    if (r <= 0) return r;
    if (d0 == 50.0f && d1 == 50.0f) return 0;
    float lift;
    if (!(d0 < d1)) {
        if (!(e1 <= 0.5235988f)) return 0;
        a->rotation[1] = h1;
        lift = 4.01f;
    } else {
        if (!(e0 <= 0.5235988f)) return 0;
        a->rotation[1] = h0;
        lift = 18.0f;
    }
    em_player_climb_trs(a);
    float target[4];
    em_player_sdk_apply(a->matrix, local, target);
    target[1] = f32_add(lift, a->position[1]);
    EmPlayerClimbHit hit;
    int kind = move(a, target, w, &hit);
    if (kind < 0) return -1;
    if (kind & 1) return 0;
    if (kind != 0) {
        if (!face_gate(&hit)) return 0;
        FAULT(capture(&ledge, &hit, w));
        if (!(f32_abs(em_player_sdk_wrap(f32_sub(ledge.heading, ang))) <= 0.5235988f)) return 0;
    }
    const float at[4] = { f32_sub(ledge.point[0], f32_mul(0.5f, ledge.normal[0])), ledge.point[1],
                          f32_sub(ledge.point[2], f32_mul(0.5f, ledge.normal[2])), 1.0f };
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
        if ((entry_attribute(&table, i) & 0xFF) == 0x46) continue;
        float y = table.height[i];
        float dy = f32_sub(y, a->position[1]);
        if (dy <= 4.01f) continue;                                      /* D_002488A0 */
        if (!(dy <= 32.0f)) continue;                                   /* D_002488B4 */
        if (!(table.aux[i] < 0.62831855f)) continue;
        int lip = lip_sweep(&ledge, y, 1, w);
        if (lip < 0) return -1;
        if (lip) continue;
        if (dy <= 24.0f) {                                              /* D_002488B0 */
            if (found && f32_sub(last, y) < 14.0f) continue;
            const float lip_base[4] = { ledge.point[0], y, ledge.point[2], 1.0f };
            static const float over[4] = { 0.0f, 4.01f, 4.5f, 0.0f };
            float b[4];
            ledge_offset(&ledge, lip_base, over, b);
            b[3] = 1.0f;
            kind = sweep(lip_base, b, 7, w, &hit);
            if (kind < 0) return -1;
            if (kind != 0) {
                if (!hit.probe.entity) continue;
                if ((hit.probe.entity_flags & 0x1F) != 4) continue;
                if (!hit.pickup_box) continue;
            }
            float top[4];
            memcpy(top, b, sizeof top);
            static const float plus[4] = { 4.0f, 0.0f, -0.5f, 0.0f };
            static const float minus[4] = { -4.0f, 0.0f, -0.5f, 0.0f };
            float right[4], left[4];
            ledge_offset(&ledge, top, plus, right); right[3] = 1.0f;
            ledge_offset(&ledge, top, minus, left); left[3] = 1.0f;
            kind = sweep(right, left, 7, w, &hit);
            if (kind < 0) return -1;
            if (kind != 0) continue;
            kind = sweep(left, right, 7, w, &hit);
            if (kind < 0) return -1;
            if (kind != 0) continue;
            int deep = depth_test(&ledge, y, w);
            if (deep < 0) return -1;
            if (!deep) continue;
            float below = f32_sub(y, 4.0f);
            const float feet[4] = { a->position[0], below, a->position[2], 0.0f };
            int covered = column(feet, 18.0f, w);
            if (covered < 0) return -1;
            if (covered) continue;
            static const float right_side[4] = { 4.5f, 0.0f, 0.0f, 1.0f };
            static const float left_side[4] = { -4.5f, 0.0f, 0.0f, 1.0f };
            float side[4];
            em_player_sdk_apply(a->matrix, right_side, side);
            side[1] = below;
            covered = column(side, 18.0f, w);
            if (covered < 0) return -1;
            if (covered) continue;
            em_player_sdk_apply(a->matrix, left_side, side);
            side[1] = below;
            covered = column(side, 18.0f, w);
            if (covered < 0) return -1;
            if (covered) continue;
            a->velocity[0] = f32_add(ledge.point[0], f32_mul(4.5f, ledge.normal[0]));
            a->velocity[2] = f32_add(ledge.point[2], f32_mul(4.5f, ledge.normal[2]));
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
        int hands = high_hands(&ledge, f32_sub(y, 1.0f), w);
        if (hands < 0) return -1;
        if (hands) continue;
        float reach = f32_add(2.0f, f32_sub(y, a->position[1]));
        const float feet[4] = { a->position[0], a->position[1], a->position[2], 1.0f };
        int covered = column(feet, reach, w);
        if (covered < 0) return -1;
        if (covered) continue;
        const float ahead[4] = { f32_add(ledge.point[0], ledge.normal[0]), a->position[1],
                                 f32_add(ledge.point[2], ledge.normal[2]), 1.0f };
        covered = column(ahead, reach, w);
        if (covered < 0) return -1;
        if (covered) continue;
        a->velocity[0] = f32_add(ledge.point[0], f32_mul(1.5f, ledge.normal[0]));
        a->velocity[2] = f32_add(ledge.point[2], f32_mul(1.5f, ledge.normal[2]));
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

/* 0017F320. */
int em_player_climb_hang_clear(const EmPlayerClimbActor *a, const EmPlayerClimbWorkers *w)
{
    static const float probes[4][2] = { {3.0f, 20.0f}, {-3.0f, 20.0f}, {3.0f, 19.0f}, {-3.0f, 19.0f} };
    int flags = 0;
    for (int i = 0; i < 4; ++i) {
        const float from_local[4] = { probes[i][0], probes[i][1], 0.0f, 1.0f };
        const float to_local[4] = { probes[i][0], probes[i][1], 6.5f, 1.0f };
        float from[4], to[4];
        em_player_sdk_apply(a->matrix, from_local, from);
        em_player_sdk_apply(a->matrix, to_local, to);
        EmPlayerClimbHit hit;
        int kind = sweep(from, to, 6, w, &hit);
        if (kind < 0) return -1;
        if (kind) flags |= (i & 1) ? 2 : 1;
    }
    return flags == 0;
}

/* 00161690: AREA11's corrected grab point for one ledge (D_00810700 == 0xB). */
static void area11_target(EmPlayerClimbActor *a, const EmPlayerClimbScene *s)
{
    if (s->area != 0xB) return;
    float top = f32_add(a->position[1], a->ledge);
    if (top < 285.0f || !(top <= 587.0f)) return;
    if (a->velocity[0] < 400.0f || !(a->velocity[0] <= 410.0f)) return;
    if (a->velocity[2] < 236.0f || !(a->velocity[2] <= 246.0f)) return;
    a->velocity[0] = 405.0f;          /* 0x43CA8000 */
    a->velocity[2] = 241.0f;          /* 0x43710000 */
    a->rotation[1] = 0x1.7750ccp+0f;      /* 0x3FBBA866 */
}

/* 0017D800: the rise toward the grab point over +28 ticks. */
static void rise_setup(EmPlayerClimbActor *a)
{
    a->velocity[1] = 0.600000023841857910f;                             /* 0x3F19999A */
    float g = f32_div(a->target_y, a->velocity[1]);
    if (g < 1.0f) g = 1.0f;
    a->target_y = f32_add(a->target_y, a->position[1]);
    a->counter = (int16_t)(int32_t)g;                                   /* float_to_int */
    a->goal[0] = a->velocity[0];
    a->goal[1] = a->velocity[2];
    a->velocity[0] = f32_div(f32_sub(a->goal[0], a->position[0]), g);
    a->velocity[2] = f32_div(f32_sub(a->goal[1], a->position[2]), g);
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
    a->position[1] = f32_add(a->position[1], a->velocity[1]);
    a->position[0] = f32_add(a->position[0], a->velocity[0]);
    a->position[2] = f32_add(a->position[2], a->velocity[2]);
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
        const float at[3] = { a->position[0], a->surface_y, a->position[2] };
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
    a->position[1] = f32_sub(hip_y, drop);
    a->speed = one ? f32_add(1.0f, hip_8) : hip_8;
    FAULT(worker_arg(w->translate, w->context, a, 1));
    FAULT(request(w, clip, 0.0f));
    a->position[1] = f32_add(a->position[1], -0.4f);
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
    a->position[1] = f32_add(a->position[1], -0.2f);
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
    a->position[1] = f32_add(a->position[1], -0.2f);
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
        if (h > 24.0f) {
            a->walk = 0x1E;
            a->target_y = f32_sub(h, 20.5f);
        } else if (h > 17.0f) {
            a->walk = 0x14; a->target_y = f32_sub(h, 17.0f); a->height_class = 2;
        } else if (h > 12.0f) {
            a->walk = 0x14; a->target_y = f32_sub(h, 12.0f); a->height_class = 1;
        } else if (h > 7.0f) {
            a->walk = 0x14; a->target_y = f32_sub(h, 7.0f); a->height_class = 0;
        } else {
            a->walk = 0xA;
            a->target_y = f32_add(1.0f, f32_sub(h, 4.01f));
        }
        rise_setup(a);
        FAULT(request(w, 0x70, 0.0f));
        break;
    }
    case 10:
        if (a->clock <= 14.0f) {
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
        if (a->clock <= kGrabClock[a->height_class]) {
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
        if (a->clock <= kSlowClock[a->height_class]) a->rate = 0.75f;
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
            FAULT(request(w, (a->row & 1) ? 142 : 123, 16.0f));        /* 00188550 */
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
    float top = f32_add(a->position[1], a->ledge);
    if (top < 285.0f || !(top <= 587.0f)) return;
    if (a->velocity[0] < 397.0f || !(a->velocity[0] <= 427.0f)) return;
    if (a->velocity[2] < 227.0f || !(a->velocity[2] <= 257.0f)) return;
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
    float dx = f32_sub(f32_add(a->goal[0], f32_mul(a->ledge_normal[0], reach)), a->position[0]);
    float dz = f32_sub(f32_add(a->goal[1], f32_mul(a->ledge_normal[1], reach)), a->position[2]);
    int fault = 0;
    float d = distance(dx, dz, w, &fault);
    if (fault) return -1;
    float ticks = f32_div(d, pace);
    float least = form == 0 ? 8.0f : 12.0f;
    if (ticks < least) ticks = least;
    a->counter = (int16_t)(int32_t)f32_sub(ticks, 1.0f);             /* float_to_int */
    a->velocity[0] = f32_div(dx, ticks);
    a->velocity[2] = f32_div(dz, ticks);
    if (form == 1)
        a->velocity[1] = f32_div(f32_sub(a->target_y, 4.0f), f32_sub(ticks, 8.0f));
    else
        a->velocity[1] = f32_div(a->target_y, ticks);
    a->target_y = f32_add(a->target_y, a->position[1]);
    a->push = form == 2 ? 1.0f : 0.600000023841857910f;
    float half = form == 1 ? f32_div(f32_sub(ticks, 8.0f), 2.0f) : f32_div(ticks, 2.0f);
    a->push_decay = f32_div(a->push, half);
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
    a->position[0] = f32_add(a->position[0], a->velocity[0]);
    a->position[2] = f32_add(a->position[2], a->velocity[2]);
    a->position[1] = f32_add(a->position[1], a->velocity[1]);
    a->position[1] = f32_add(a->position[1], a->push);
    a->push = f32_sub(a->push, a->push_decay);
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
            if (a->ledge < 7.0f) {                                      /* D_002488A4 */
                a->walk = 0xA;
                FAULT(vault_setup(a, 0, w));
            } else {
                a->walk = 0x14;
                a->tier = 1;
                FAULT(vault_setup(a, 1, w));
            }
        } else {
            a->walk = 0x1E;
            a->target_y = f32_sub(a->ledge, 20.5f);
            FAULT(vault_setup(a, 2, w));
        }
        if (a->tier == 3) {
            int frames = 0;
            if (!w->clip_frames || !w->arbiter) return -1;
            FAULT(w->clip_frames(w->context, 0x69, &frames));
            FAULT(w->arbiter(w->context, 0x69, 0.0f, f32_sub((float)frames, 4.0f)));
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
            a->position[1] = f32_add(a->position[1], a->velocity[1]);
            a->position[0] = f32_add(a->position[0], a->velocity[0]);
            a->position[2] = f32_add(a->position[2], a->velocity[2]);
            a->position[1] = f32_add(a->position[1], a->push);
            a->push = f32_sub(a->push, a->push_decay);
        }
        if (a->tier == 3) a->rate = 2.0f;
        break;
    }
    case 12:
        a->position[1] = f32_add(a->position[1], -0.2f);
        FAULT(floor_service(a, w, &contact));
        if (contact != 0) {
            a->goal[0] = f32_add(1.0f, a->position[1]);
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
        a->position[0] = f32_add(a->position[0], a->velocity[0]);
        a->position[2] = f32_add(a->position[2], a->velocity[2]);
        a->position[1] = f32_add(a->position[1], a->velocity[1]);
        a->position[1] = f32_add(a->position[1], a->push);
        a->push = f32_sub(a->push, a->push_decay);
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
            a->position[1] = f32_add(a->position[1], -0.2f);
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
        a->position[0] = f32_add(a->position[0], a->velocity[0]);
        a->position[2] = f32_add(a->position[2], a->velocity[2]);
        a->position[1] = f32_add(a->position[1], 0.5f);
        break;
    }
    case 23:
        if (a->anim_flags & 0x1000) {
            /* 00102948 is an lq/sq pair: all four lanes, so +BC takes the node's w. */
            for (int i = 0; i < 4; ++i) a->position[i] = s->hip_world[i];
            a->position[1] = f32_sub(a->position[1], 11.0f);
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
            a->position[1] = f32_add(a->position[1], -0.2f);
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
            FAULT(request(w, (a->row & 1) ? 142 : 123, 16.0f));        /* 00188550 */
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
