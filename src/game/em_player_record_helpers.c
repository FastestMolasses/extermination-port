/* em_player_record_helpers.c - the shared player helpers over the raw
 * record (see em_player_record_helpers.h, docs/PLAYER_RECORD_HELPERS.md).
 *
 * Read from the decomp: the byte-matched C of 001755B0, 00177510, 001775E0,
 * 00174FD0 and 00188550, and the instructions (.s) of the NEARMISS
 * 001776E0, 00177CF0, 0017F320 and 0019A180, whose readable C was checked
 * against them for operand and evaluation order. Every COP1 operation and
 * compare goes through em_ee_float.h on bit patterns. */
#include "game/em_player_record_helpers.h"

#include "game/em_ee_float.h"
#include "game/em_effect_original.h"
#include "game/em_owner_services_original.h"
#include "game/em_player_stage_workers.h"
#include "game/em_script_host_workers.h"
#include "game/em_sdk_math_original.h"

#include <stddef.h>
#include <string.h>

#define F_ZERO       UINT32_C(0x00000000)
#define F_ONE        UINT32_C(0x3F800000)
#define F_HALF       UINT32_C(0x3F000000)
#define F_1_5        UINT32_C(0x3FC00000)
#define F_4_01       UINT32_C(0x408051EC)   /* 4.01 */
#define F_5          UINT32_C(0x40A00000)
#define F_PI         UINT32_C(0x40490FDB)
#define F_HALF_PI    UINT32_C(0x3FC90FDB)   /* 1.5707964 */
#define F_THREE_HALF_PI UINT32_C(0x4096CBE4) /* 4.712389 */
#define F_3PI_4      UINT32_C(0x4016CBE4)   /* 2.3561945 */
#define F_PI_4       UINT32_C(0x3F490FDB)   /* 0.7853982 */
#define F_256        UINT32_C(0x43800000)
#define F_ATTR_LOW   UINT32_C(0x3F3340CD)   /* 0.70020753 */
#define F_ATTR_LOW_N UINT32_C(0xBF3340CD)
#define F_ATTR_HIGH  UINT32_C(0x3FDDB3D7)   /* 1.7320508 */
#define F_ATTR_HIGH_N UINT32_C(0xBFDDB3D7)

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

static uint32_t fbits(float v) { return em_ee_bits(v); }
static float bfloat(uint32_t b) { return em_ee_float(b); }

static void to_bits(uint32_t *out, const float *in, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) out[i] = fbits(in[i]);
}

static void to_floats(float *out, const uint32_t *in, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) out[i] = bfloat(in[i]);
}

int em_player_helper_wrap(uint32_t x, uint32_t *out)
{
    if (!out || (x & UINT32_C(0x7FFFFFFF)) >= EM_SCRIPT_HOST_WRAP_LIMIT) return -1;
    *out = em_player_001B1470(x);
    return 0;
}

/* 001026A0(out, M, v) = v x M (em_effect_original_001026A0). */
int em_player_helper_apply(const uint32_t m[16], const uint32_t v[4], uint32_t out[4])
{
    float mf[16], vf[4], of[4];
    if (!m || !v || !out) return -1;
    to_floats(mf, m, 16);
    to_floats(vf, v, 4);
    em_effect_original_001026A0(of, mf, vf);
    to_bits(out, of, 4);
    return 0;
}

static void apply(const uint32_t m[16], const uint32_t v[4], uint32_t out[4])
{
    (void)em_player_helper_apply(m, v, out);
}

/* 001028B8(out, a, b): out = a + b in all four lanes (VADD.xyzw). */
static int vadd(const uint32_t a[4], const uint32_t b[4], uint32_t out[4])
{
    return em_vu_vec_bits(EM_VU_ADD, 15, EM_VU_NO_BC, a, b, 0, NULL, out) == EM_EE_FLOAT_OK ? 0 : -1;
}

/* 001029C0, 00102BB0(M, M, angle), then 00102918(M, M, v) when v is set. */
static int yaw_translate_matrix(uint32_t angle, const uint32_t v[3], uint32_t out[16])
{
    float m[16], t[3];
    if (em_owner_services_identity_001029C0(m) != EM_EE_FLOAT_OK) return -1;
    if (em_owner_services_rotate_y_00102BB0(m, m, angle) != EM_EE_FLOAT_OK) return -1;
    if (v) {
        to_floats(t, v, 3);
        if (em_owner_services_translate_00102918(m, m, t) != EM_EE_FLOAT_OK) return -1;
    }
    to_bits(out, m, 16);
    return 0;
}

static int yaw_matrix(uint32_t angle, uint32_t out[16])
{
    return yaw_translate_matrix(angle, NULL, out);
}

int em_player_helper_yaw_point(uint32_t angle, const uint32_t base[3], const uint32_t local[4],
                               uint32_t out[4])
{
    uint32_t m[16];
    if (!local || !out || yaw_translate_matrix(angle, base, m) < 0) return -1;
    return em_player_helper_apply(m, local, out);
}

/* 001026A0(D0, M, local) then 001028B8(local, base, D0), then w = 1.0. */
int em_player_helper_ledge_offset(const uint32_t m[16], const uint32_t base[4],
                                  const uint32_t local[4], uint32_t out[4])
{
    uint32_t turned[4];
    if (!m || !base || !local || !out) return -1;
    apply(m, local, turned);
    FAULT(vadd(base, turned, out));
    out[3] = F_ONE;
    return 0;
}

static int ledge_offset(const uint32_t m[16], const uint32_t base[4], const uint32_t local[4],
                        uint32_t out[4])
{
    return em_player_helper_ledge_offset(m, base, local, out);
}

int em_player_helper_trs(float out[16], const float position[3], const float rotation[3],
                         const float scale[3])
{
    if (!out || !position || !rotation || !scale) return -1;
    return em_owner_services_build_trs_matrix(out, position, rotation, scale) == EM_EE_FLOAT_OK ? 0 : -1;
}

static int sweep(const EmPlayerHelperCalls *c, const uint32_t from[4], const uint32_t to[4],
                 unsigned mask, uint16_t *node)
{
    uint16_t dummy = 0;
    if (!c || !c->sweep) return -1;
    return c->sweep(c->context, from, to, mask, node ? node : &dummy);
}

static void ledge_bits(const EmPlayerRecoveryLedge *l, uint32_t point[3], uint32_t normal[3],
                       uint32_t matrix[16])
{
    if (point) to_bits(point, l->point, 3);
    if (normal) to_bits(normal, l->normal, 3);
    if (matrix) to_bits(matrix, l->matrix, 16);
}

/* ---- 00177510 ------------------------------------------------------------ */

int em_player_helper_00177510(const EmPlayerHelperCalls *c, const float point[3],
                              const float normal[3], EmPlayerRecoveryLedge *l)
{
    if (!c || !c->atan2 || !point || !normal || !l) return -1;
    for (int i = 0; i < 3; ++i) {
        l->point[i] = point[i];                                 /* 0x70003050..58 */
        l->normal[i] = normal[i];                               /* 0x70003060..68 */
    }
    uint32_t angle, heading, m[16];
    FAULT(c->atan2(c->context, em_ee_neg_bits(fbits(normal[2])), fbits(normal[0]), &angle));
    FAULT(em_player_helper_wrap(em_ee_add_bits(F_THREE_HALF_PI, angle), &heading));
    l->heading = bfloat(heading);                               /* 0x700031E4 */
    FAULT(yaw_matrix(heading, m));                              /* 0x70003070 */
    to_floats(l->matrix, m, 16);
    return 0;
}

/* ---- 001775E0 ------------------------------------------------------------ */

int em_player_helper_001775E0(const EmPlayerHelperCalls *c, const EmPlayerRecoveryLedge *l,
                              uint32_t y, int wide, uint32_t s38A0[4], int *result)
{
    if (!c || !c->sweep || !l || !s38A0 || !result) return -1;
    uint32_t out_scale, lift, in_scale;
    if (wide == 0) { out_scale = F_ONE; in_scale = F_HALF; lift = F_ONE; }
    else { out_scale = F_5; lift = F_4_01; in_scale = F_5; }
    uint32_t p[3], n[3];
    ledge_bits(l, p, n, NULL);
    const uint32_t height = em_ee_add_bits(y, lift);
    const uint32_t from[4] = { em_ee_add_bits(p[0], em_ee_mul_bits(n[0], out_scale)), height,
                               em_ee_add_bits(p[2], em_ee_mul_bits(n[2], out_scale)), F_ONE };
    const uint32_t to[4] = { em_ee_sub_bits(p[0], em_ee_mul_bits(n[0], in_scale)), height,
                             em_ee_sub_bits(p[2], em_ee_mul_bits(n[2], in_scale)), F_ONE };
    memcpy(s38A0, from, sizeof from);                           /* 0x700038A0 */
    int kind = sweep(c, from, to, 7, NULL);
    if (kind < 0) return -1;
    *result = kind != 0;
    return 0;
}

/* ---- 001776E0 ------------------------------------------------------------ */

int em_player_helper_001776E0(const EmPlayerHelperCalls *c, const EmPlayerRecoveryLedge *l,
                              uint32_t y, uint32_t s38A0[4], int *result)
{
    static const uint32_t plus4[4] = { UINT32_C(0x40800000), 0, 0, F_ONE };
    static const uint32_t minus4[4] = { UINT32_C(0xC0800000), 0, 0, F_ONE };
    static const uint32_t right[4] = { UINT32_C(0x40800000), 0, UINT32_C(0x40000000), F_ONE };
    static const uint32_t left[4] = { UINT32_C(0xC0800000), 0, UINT32_C(0x40000000), F_ONE };
    if (!c || !c->sweep || !l || !s38A0 || !result) return -1;
    uint32_t p[3], n[3], m[16];
    ledge_bits(l, p, n, m);
    int mask = 0;
    for (int pass = 0; pass < 2; ++pass) {
        uint32_t base[4];
        if (pass == 0) {
            base[0] = em_ee_sub_bits(p[0], em_ee_mul_bits(F_HALF, n[0]));
            base[2] = em_ee_sub_bits(p[2], em_ee_mul_bits(F_HALF, n[2]));
            base[1] = em_ee_add_bits(F_ONE, y);
        } else {
            base[0] = em_ee_add_bits(p[0], em_ee_mul_bits(F_1_5, n[0]));
            base[2] = em_ee_add_bits(p[2], em_ee_mul_bits(F_1_5, n[2]));
            base[1] = em_ee_sub_bits(y, F_ONE);
        }
        base[3] = F_ONE;
        memcpy(s38A0, base, sizeof base);                       /* 0x700038A0 */
        uint32_t b[4], d[4];
        FAULT(ledge_offset(m, base, plus4, b));                 /* 0x700038B0 */
        FAULT(ledge_offset(m, base, minus4, d));                /* 0x700038C0 */
        int kind = sweep(c, b, d, 7, NULL);
        if (kind < 0) return -1;
        if (kind) mask |= 1;
        kind = sweep(c, d, b, 7, NULL);
        if (kind < 0) return -1;
        if (kind) mask |= 2;
        if (mask) {
            *result = mask;
            return 0;
        }
    }
    int side = 0;
    uint32_t base[4], to[4];
    memcpy(base, s38A0, sizeof base);
    FAULT(ledge_offset(m, base, right, to));
    int kind = sweep(c, base, to, 7, NULL);
    if (kind < 0) return -1;
    if (kind & 6) side |= 1;
    FAULT(ledge_offset(m, base, left, to));
    kind = sweep(c, base, to, 7, NULL);
    if (kind < 0) return -1;
    if (kind & 6) side |= 2;
    *result = side ? 0 : -1;
    return 0;
}

/* ---- 00177CF0 ------------------------------------------------------------ */

int em_player_helper_00177CF0(const EmPlayerHelperCalls *c, const EmPlayerRecoveryLedge *l,
                              uint32_t y, uint32_t s38A0[4], int *result)
{
    if (!c || !c->sweep || !l || !s38A0 || !result) return -1;
    uint32_t p[3], m[16];
    ledge_bits(l, p, NULL, NULL);
    const uint32_t at[3] = { p[0], y, p[2] };                   /* the stack vector, w = 1.0 */
    FAULT(yaw_translate_matrix(fbits(l->heading), at, m));      /* 0x700036A0 */
    for (int side = 0; side < 2; ++side) {
        const uint32_t x = side == 0 ? UINT32_C(0xC0900000) : UINT32_C(0x40900000);  /* -+4.5 */
        const uint32_t near[4] = { x, 0, UINT32_C(0xC0000000), F_ONE };
        const uint32_t far[4] = { x, 0, UINT32_C(0x40000000), F_ONE };
        memcpy(s38A0, near, sizeof near);                       /* 0x700038A0 */
        uint32_t from[4], to[4];
        apply(m, near, from);                                   /* 0x700038C0 */
        apply(m, far, to);                                      /* 0x700038D0 */
        uint16_t node = 0;
        int kind = sweep(c, from, to, 7, &node);
        if (kind < 0) return -1;
        if (kind & 1) { *result = 1; return 0; }
        if ((kind & 6) && (node & 0xFF) == 0x32) { *result = 1; return 0; }
    }
    *result = 0;
    return 0;
}

/* ---- 0019A180 ------------------------------------------------------------ */

int em_player_helper_0019A180(const EmPlayerClimbTable *t, int i)
{
    if (!t || i >= t->count || i < 0) return 0;
    if (t->flags[i] & 0x8000) {
        const uint32_t v = fbits(t->aux[i]);
        int16_t bits;
        if (em_ee_c_lt_bits(v, F_ZERO)) {
            if (em_ee_c_le_bits(v, F_ATTR_LOW_N))
                bits = em_ee_c_lt_bits(v, F_ATTR_HIGH_N) ? 0x2000 : 0x800;
            else
                bits = (int16_t)-0x8000;
        } else if (em_ee_c_lt_bits(v, F_ATTR_LOW)) {
            bits = 0x4000;
        } else if (em_ee_c_le_bits(v, F_ATTR_HIGH)) {
            bits = 0x1000;
        } else {
            bits = 0x2000;
        }
        return (int16_t)(bits | t->object_kind[i]);
    }
    return t->object_node[i];
}

/* ---- 0017F320 ------------------------------------------------------------ */

int em_player_helper_0017F320(const EmPlayerHelperCalls *c, const uint32_t matrix[16],
                              uint32_t s38A0[4], int *result)
{
    static const uint32_t probe[4][2] = {        /* (x, y) of the four probes */
        { UINT32_C(0x40400000), UINT32_C(0x41A00000) }, { UINT32_C(0xC0400000), UINT32_C(0x41A00000) },
        { UINT32_C(0x40400000), UINT32_C(0x41980000) }, { UINT32_C(0xC0400000), UINT32_C(0x41980000) },
    };
    if (!c || !c->sweep || !matrix || !s38A0 || !result) return -1;
    int flags = 0;
    for (int i = 0; i < 4; ++i) {
        const uint32_t from_local[4] = { probe[i][0], probe[i][1], 0, F_ONE };
        const uint32_t to_local[4] = { probe[i][0], probe[i][1], UINT32_C(0x40D00000), F_ONE };
        memcpy(s38A0, from_local, sizeof from_local);           /* 0x700038A0 */
        uint32_t from[4], to[4];
        apply(matrix, from_local, from);                        /* 0x700038C0 */
        apply(matrix, to_local, to);                            /* 0x700038D0 */
        int kind = sweep(c, from, to, 6, NULL);
        if (kind < 0) return -1;
        if (kind) flags |= (i & 1) ? 2 : 1;
    }
    *result = flags == 0;
    return 0;
}

/* ---- 00188550 ------------------------------------------------------------ */

int em_player_helper_00188550(uint8_t row)
{
    static const int16_t kRowClip[2] = { 0x7B, 0x8E };          /* D_002754C0 */
    return kRowClip[row & 1];
}

/* ==== The record entries ================================================== */

/* EmPlayerHelperCalls over an EmPlayerRecordHelpers and the record. */
typedef struct RecordCalls {
    const EmPlayerRecordHelpers *h;
    EmPlayerLiveActor *actor;
} RecordCalls;

static int record_sweep(void *context, const uint32_t from[4], const uint32_t to[4], unsigned mask,
                        uint16_t *node)
{
    RecordCalls *r = context;
    float f[4], t[4];
    to_floats(f, from, 4);
    to_floats(t, to, 4);
    EmPlayerProbeHit hit;
    memset(&hit, 0, sizeof hit);
    int kind = r->h->sweep(r->h->context, r->actor, f, t, mask, &hit);
    if (kind < 0) return -1;
    if (kind) *node = hit.node;
    return kind;
}

static int record_atan2(void *context, uint32_t y, uint32_t x, uint32_t *out)
{
    RecordCalls *r = context;
    *out = fbits(r->h->atan2(r->h->context, bfloat(y), bfloat(x)));
    return 0;
}

static int record_cosine(void *context, uint32_t x, uint32_t *out)
{
    RecordCalls *r = context;
    *out = fbits(r->h->cosine(r->h->context, bfloat(x)));
    return 0;
}

static EmPlayerHelperCalls record_calls(RecordCalls *r)
{
    EmPlayerHelperCalls c;
    c.context = r;
    c.sweep = r->h->sweep ? record_sweep : NULL;
    c.atan2 = r->h->atan2 ? record_atan2 : NULL;
    c.cosine = r->h->cosine ? record_cosine : NULL;
    return c;
}

int em_player_record_001755B0(void *helpers, EmPlayerLiveActor *a, int *result)
{
    const EmPlayerRecordHelpers *h = helpers;
    if (!h || !a || !result || !h->d8106A0 || !h->scratch) return -1;
    uint32_t heading, error;
    FAULT(em_player_helper_wrap(em_ee_add_bits(em_ee_add_bits(F_PI, em_live_u32(a, 0x24C)),
                                               *h->d8106A0), &heading));
    FAULT(em_player_helper_wrap(em_ee_sub_bits(heading, em_live_u32(a, 0xC4)), &error));
    error = fbits(em_sdk_math_original_0011DF78(bfloat(error)));
    h->scratch->s3A20 = error;                                  /* 0x70003A20 */
    *result = em_ee_c_le_bits(error, F_HALF_PI) ? 0 : 1;
    return 0;
}

int em_player_record_0017F320(void *helpers, EmPlayerLiveActor *a, int *result)
{
    const EmPlayerRecordHelpers *h = helpers;
    if (!h || !a || !result || !h->sweep || !h->scratch) return -1;
    RecordCalls r = { h, a };
    const EmPlayerHelperCalls c = record_calls(&r);
    uint32_t m[16];
    for (unsigned i = 0; i < 16; ++i) m[i] = em_live_u32(a, 0xD0 + 4 * i);
    return em_player_helper_0017F320(&c, m, h->scratch->s38A0, result);
}

int em_player_record_00188550(void *helpers, EmPlayerLiveActor *a, int *clip)
{
    (void)helpers;
    if (!a || !clip) return -1;
    *clip = em_player_helper_00188550(em_live_u8(a, 0x235));
    return 0;
}

/* pi * (byte / 256.0), the byte converted as a word. */
static uint32_t stick_angle(uint8_t byte)
{
    return em_ee_mul_bits(F_PI, em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)byte), F_256));
}

int em_player_record_00174FD0(void *helpers, EmPlayerLiveActor *a)
{
    const EmPlayerRecordHelpers *h = helpers;
    if (!h || !a || !h->spad3B8D || !h->d810E57 || !h->d810E64 || !h->d810E65 || !h->cosine ||
        !h->atan2 || !h->scratch)
        return -1;
    if (*h->spad3B8D != 0) {
        if (em_live_u8(a, 4) == 1 && em_live_u8(a, 5) == 9) {
            em_live_set_u8(a, 0x23F, 0);
            em_live_set_u32(a, 0x24C, UINT32_C(0xFFFFFFFF));
        }
        return 0;
    }
    em_live_set_u8(a, 0x23F, *h->d810E57);
    if (em_live_u8(a, 0x23F) == 0) {
        em_live_set_u32(a, 0x24C, UINT32_C(0xFFFFFFFF));
        return 0;
    }
    RecordCalls r = { h, a };
    const EmPlayerHelperCalls c = record_calls(&r);
    const uint32_t y_angle = stick_angle(*h->d810E65);          /* computed before the X cosine */
    uint32_t v;
    FAULT(c.cosine(c.context, stick_angle(*h->d810E64), &v));
    em_live_set_u32(a, 0x244, v);
    FAULT(c.cosine(c.context, y_angle, &v));
    em_live_set_u32(a, 0x248, v);
    uint32_t angle;
    FAULT(c.atan2(c.context, em_ee_neg_bits(v), em_live_u32(a, 0x244), &angle));
    const uint32_t magnitude = fbits(em_sdk_math_original_0011DF78(bfloat(angle)));
    h->scratch->s3A20 = magnitude;                              /* 0x70003A20 */
    uint32_t quadrant;
    if (!em_ee_c_le_bits(magnitude, F_3PI_4)) quadrant = 2;
    else if (em_ee_c_lt_bits(magnitude, F_PI_4)) quadrant = 3;
    else if (em_ee_c_lt_bits(angle, F_ZERO)) quadrant = 0;
    else quadrant = 1;
    em_live_set_u32(a, 0x24C, quadrant);
    return 0;
}

int em_player_record_00177510(void *helpers, const EmPlayerProbeHit *hit, EmPlayerRecoveryLedge *ledge)
{
    const EmPlayerRecordHelpers *h = helpers;
    if (!h || !hit || !ledge || !h->atan2) return -1;
    RecordCalls r = { h, NULL };
    const EmPlayerHelperCalls c = record_calls(&r);
    return em_player_helper_00177510(&c, hit->point, hit->normal, ledge);
}

int em_player_record_001775E0(void *helpers, EmPlayerLiveActor *a, const EmPlayerRecoveryLedge *ledge,
                              int wide, float y, int *result)
{
    const EmPlayerRecordHelpers *h = helpers;
    if (!h || !a || !ledge || !result || !h->sweep || !h->scratch) return -1;
    RecordCalls r = { h, a };
    const EmPlayerHelperCalls c = record_calls(&r);
    return em_player_helper_001775E0(&c, ledge, fbits(y), wide, h->scratch->s38A0, result);
}

int em_player_record_001776E0(void *helpers, EmPlayerLiveActor *a, const EmPlayerRecoveryLedge *ledge,
                              float y, int *result)
{
    const EmPlayerRecordHelpers *h = helpers;
    if (!h || !a || !ledge || !result || !h->sweep || !h->scratch) return -1;
    RecordCalls r = { h, a };
    const EmPlayerHelperCalls c = record_calls(&r);
    return em_player_helper_001776E0(&c, ledge, fbits(y), h->scratch->s38A0, result);
}

int em_player_record_00177CF0(void *helpers, EmPlayerLiveActor *a, const EmPlayerRecoveryLedge *ledge,
                              float y, int *result)
{
    const EmPlayerRecordHelpers *h = helpers;
    if (!h || !a || !ledge || !result || !h->sweep || !h->scratch) return -1;
    RecordCalls r = { h, a };
    const EmPlayerHelperCalls c = record_calls(&r);
    return em_player_helper_00177CF0(&c, ledge, fbits(y), h->scratch->s38A0, result);
}

int em_player_record_0019A180(void *helpers, const EmPlayerClimbTable *table, int index,
                              int *attribute)
{
    (void)helpers;
    if (!table || !attribute) return -1;
    if (table->count < 0 || table->count > EM_PLAYER_CLIMB_TABLE_MAX) return -1;
    *attribute = em_player_helper_0019A180(table, index);
    return 0;
}
