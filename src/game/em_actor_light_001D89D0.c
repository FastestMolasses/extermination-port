/* The per-draw actor lighting driver 001D89D0. See em_actor_light_001D89D0.h
 * and docs/ACTOR_LIGHT_001D89D0.md. Every routine names the original address
 * it translates; tools/test_actor_light_001d89d0_reference.py runs the
 * original instructions against it.
 *
 * Float arithmetic: every EE COP1 and VU0-macro instruction goes through
 * game/em_ee_float.h, named by its real form. No host float operation is
 * performed here. */
#include "game/em_actor_light_001D89D0.h"

#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

typedef uint32_t u32;

/* Dest masks (x = 8, y = 4, z = 2, w = 1). */
#define DX 8u
#define DXYZ 14u
#define DXYZW 15u
#define NO_BC EM_VU_NO_BC

/* Constants built by instruction immediates (upper halves, plus the one
 * lui/ori pair each), named by value. */
#define F_ZERO 0x00000000u
#define F_ONE 0x3F800000u
#define F_TWO 0x40000000u
#define F_TEN 0x41200000u
#define F_THIRTY 0x41F00000u
#define F_SIXTY_FOUR 0x42800000u
#define F_128 0x43000000u
#define F_BIAS 0x4B000000u        /* 8388608.0 */
#define F_BIAS_64 0x4B000040u     /* 8388672.0, stored as a word by mode 4 */
#define F_TENTH 0x3DCCCCCDu       /* 0.1 */
#define F_FIFTH 0x3E4CCCCDu       /* 0.2 */

/* The VU constant register: (0, 0, 0, 1.0). */
static const u32 VF0[4] = {0, 0, 0, F_ONE};

/* Rig record word index of a byte offset. */
#define G(off) ((off) / 4)

/* ======================================================================
 * Fault plumbing
 * ==================================================================== */

static int fail(EmActorLight *s, u32 address, int32_t code)
{
    if (s->fault.code == EM_OWNER_FAULT_NONE) {
        s->fault.address = address;
        s->fault.code = code;
    }
    return -1;
}

static int latched(const EmActorLight *s) { return s->fault.code != EM_OWNER_FAULT_NONE; }

/* ======================================================================
 * SDK VU0 routines (macro mode). `*st` is sticky: once an instruction
 * reports a nonzero status nothing later executes and the caller faults.
 * ==================================================================== */

static void vu(int *st, em_vu_op op, unsigned dest, int bc, const u32 fs[4], const u32 ft[4], u32 q,
               const u32 acc[4], u32 dst[4])
{
    if (*st != EM_EE_FLOAT_OK) return;
    *st = em_vu_vec_bits(op, dest, bc, fs, ft, q, acc, dst);
}

/* 001026A0(out, m, v): out = m row 0 * v.x + row 1 * v.y + row 2 * v.z +
 * row 3 * v.w, all four lanes, accumulated left to right (MULAbc x, MADDAbc
 * y and z, MADDbc w into the result). The matrix and the vector are loaded
 * before the store, so out may alias either. */
static void sdk_001026A0(int *st, u32 out[4], const u32 m[16], const u32 v[4])
{
    u32 mm[16], vv[4], acc[4] = {0, 0, 0, 0}, r[4] = {0, 0, 0, 0};
    memcpy(mm, m, sizeof mm);
    memcpy(vv, v, sizeof vv);
    vu(st, EM_VU_MULABC, DXYZW, 0, mm, vv, 0, NULL, acc);
    vu(st, EM_VU_MADDABC, DXYZW, 1, mm + 4, vv, 0, acc, acc);
    vu(st, EM_VU_MADDABC, DXYZW, 2, mm + 8, vv, 0, acc, acc);
    vu(st, EM_VU_MADDBC, DXYZW, 3, mm + 12, vv, 0, acc, r);
    if (*st == EM_EE_FLOAT_OK) memcpy(out, r, sizeof r);
}

/* 00102738(a, b): t.xyz = a.xyz * b.xyz; t.x += t.y; t.x += t.z; returns
 * t.x (moved to f0). */
static u32 sdk_00102738(int *st, const u32 a[4], const u32 b[4])
{
    u32 t[4];
    memcpy(t, b, sizeof t);
    vu(st, EM_VU_MUL, DXYZ, NO_BC, a, t, 0, NULL, t);
    vu(st, EM_VU_ADDBC, DX, 1, t, t, 0, NULL, t);
    vu(st, EM_VU_ADDBC, DX, 2, t, t, 0, NULL, t);
    return t[0];
}

/* 00102760(out, v): d.xyz = v * v; d.x += d.y; d.x += d.z; Q = sqrt(d.x);
 * d.x = 0 + Q; Q = 1.0 / d.x (VDIV of vf0.w by d.x); r = vf0 - vf0;
 * r.xyz = v * Q. So out = (v.xyz / |v|, 0). */
static void sdk_00102760(int *st, u32 out[4], const u32 v[4])
{
    u32 in[4], d[4], r[4] = {0, 0, 0, 0}, q;
    memcpy(in, v, sizeof in);
    memcpy(d, in, sizeof d);          /* only its x lane is read below */
    vu(st, EM_VU_MUL, DXYZ, NO_BC, in, in, 0, NULL, d);
    vu(st, EM_VU_ADDBC, DX, 1, d, d, 0, NULL, d);
    vu(st, EM_VU_ADDBC, DX, 2, d, d, 0, NULL, d);
    if (*st != EM_EE_FLOAT_OK) return;
    q = em_vu_sqrt_bits(d[0]);
    vu(st, EM_VU_ADDQ, DX, NO_BC, VF0, NULL, q, NULL, d);
    if (*st != EM_EE_FLOAT_OK) return;
    *st = em_vu_div_bits(VF0[3], d[0], 3, 0, &q);
    vu(st, EM_VU_SUB, DXYZW, NO_BC, VF0, VF0, 0, NULL, r);
    vu(st, EM_VU_MULQ, DXYZ, NO_BC, in, NULL, q, NULL, r);
    if (*st == EM_EE_FLOAT_OK) memcpy(out, r, sizeof r);
}

/* 00102798(dst, src): the 4x4 transpose (four quadword loads, lane
 * interleaves, four stores). */
static void sdk_00102798(u32 dst[16], const u32 src[16])
{
    u32 t[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) t[4 * c + r] = src[4 * r + c];
    memcpy(dst, t, sizeof t);
}

/* 001028B8(out, a, b): out = a + b (VADD xyzw). */
static void sdk_001028B8(int *st, u32 out[4], const u32 a[4], const u32 b[4])
{
    u32 r[4] = {0, 0, 0, 0};
    vu(st, EM_VU_ADD, DXYZW, NO_BC, a, b, 0, NULL, r);
    if (*st == EM_EE_FLOAT_OK) memcpy(out, r, sizeof r);
}

/* 001028D0(out, a, b): out = a - b (VSUB xyzw). */
static void sdk_001028D0(int *st, u32 out[4], const u32 a[4], const u32 b[4])
{
    u32 r[4] = {0, 0, 0, 0};
    vu(st, EM_VU_SUB, DXYZW, NO_BC, a, b, 0, NULL, r);
    if (*st == EM_EE_FLOAT_OK) memcpy(out, r, sizeof r);
}

/* 00102900(out, v, f12): out = v * f12 (VMULbc xyzw, the scalar moved into
 * the x lane of the second operand). */
static void sdk_00102900(int *st, u32 out[4], const u32 v[4], u32 f12)
{
    u32 k[4] = {f12, 0, 0, 0}, r[4] = {0, 0, 0, 0};
    vu(st, EM_VU_MULBC, DXYZW, 0, v, k, 0, NULL, r);
    if (*st == EM_EE_FLOAT_OK) memcpy(out, r, sizeof r);
}

static void u2f16(float out[16], const u32 in[16]) { memcpy(out, in, 16 * sizeof(u32)); }
static void f2u16(u32 out[16], const float in[16]) { memcpy(out, in, 16 * sizeof(u32)); }

/* ======================================================================
 * 001D8270: the point-light fold gate
 * ==================================================================== */

static int gate_excluded(uint8_t kind)
{
    /* The type bytes compared in turn: 3E, 3D, 17, 16, 15, 0D, 0B, 09, 08, 03. */
    switch (kind) {
    case 0x3E: case 0x3D: case 0x17: case 0x16: case 0x15:
    case 0x0D: case 0x0B: case 0x09: case 0x08: case 0x03:
        return 1;
    default:
        return 0;
    }
}

/* Pure evaluation; -1 when the model view is needed and missing. */
static int gate_value(const EmActorLightOwner *o)
{
    if (gate_excluded(o->kind)) return 0;
    if (!o->model_radius) return -1;
    /* radius < 30.0 (EE compare). */
    return em_ee_c_lt_bits(*o->model_radius, F_THIRTY) ? 1 : 0;
}

int em_actor_light_001D8270(EmActorLight *s, const EmActorLightOwner *o, int32_t *result)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!o || !result) return fail(s, 0x001D8270u, EM_OWNER_FAULT_NULL_WORKER);
    int g = gate_value(o);
    if (g < 0) return fail(s, 0x001D8270u, EM_OWNER_FAULT_NULL_WORKER);
    *result = g;
    return 0;
}

/* ======================================================================
 * 001D7B30 / 001D2910(8) / 001D2710(8): the room rig lookup
 * ==================================================================== */

static int lookup_ready(const EmActorLight *s)
{
    const EmActorLightWorld *w = &s->world;
    if (!w->ctx_000C) return 0;
    if (!(*w->ctx_000C & 0x100u) && !w->d00810700) return 0;
    return w->d00251C50 != NULL;
}

static u32 lookup(const EmActorLightWorld *w)
{
    /* 001D2910(8): 8 < 0x20, so 001D2710(8) = context +0x0C & (1 << 8). */
    u32 key = (*w->ctx_000C & 0x100u) ? 0xF00u : ((u32)w->d00810700[0] << 8) + (u32)w->d00810700[1];
    for (u32 i = 0; i < EM_ACTOR_LIGHT_TABLE_ENTRIES; ++i)
        if (w->d00251C50[i * EM_ACTOR_LIGHT_TABLE_ENTRY_WORDS] == key) return i;
    return 0; /* no match: the table base */
}

static u32 lookup_fault_address(const EmActorLight *s)
{
    if (!s->world.ctx_000C) return 0x001D2710u;
    return 0x001D7B30u;
}

int em_actor_light_001D7B30(EmActorLight *s, uint32_t *entry)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!entry) return fail(s, 0x001D7B30u, EM_OWNER_FAULT_NULL_WORKER);
    if (!lookup_ready(s)) return fail(s, lookup_fault_address(s), EM_OWNER_FAULT_NULL_WORKER);
    *entry = lookup(&s->world);
    return 0;
}

/* ======================================================================
 * 001D8130: the room rig load
 * ==================================================================== */

static void rig_load(const EmActorLightWorld *w)
{
    u32 *g = w->d00817BC0;
    *w->d00275688 = EM_ACTOR_LIGHT_RIG_ADDRESS;
    const u32 *src = w->d00251C50 + lookup(w) * EM_ACTOR_LIGHT_TABLE_ENTRY_WORDS;
#define E(off) src[(off) / 4]
    g[G(0xB0)] = 0;
    g[G(0xB4)] = E(0x1C);
    g[G(0x80)] = E(0x20);           /* slot 0 angles x, y (z is not loaded) */
    g[G(0x84)] = E(0x24);
    g[G(0xF0)] = E(0x28);           /* slot 0 colour and weight */
    g[G(0xF4)] = E(0x2C);
    g[G(0xF8)] = E(0x30);
    g[G(0xFC)] = E(0x34);
    g[G(0x90)] = E(0x38);           /* slot 1 angles x, y */
    g[G(0x94)] = E(0x3C);
    g[G(0x100)] = E(0x40);          /* slot 1 colour */
    g[G(0x104)] = E(0x44);
    g[G(0x108)] = E(0x48);
    g[G(0x10C)] = E(0x4C);
    g[G(0xA0)] = E(0x50);           /* slot 2 angles x, y */
    g[G(0xA4)] = E(0x54);
    g[G(0x110)] = E(0x58);          /* slot 2 colour */
    g[G(0x114)] = E(0x5C);
    g[G(0x118)] = E(0x60);
    g[G(0x11C)] = E(0x64);
    g[G(0x120)] = E(0x68);          /* ambient xyz */
    g[G(0x124)] = E(0x6C);
    g[G(0x128)] = E(0x70);
    g[G(0x12C)] = F_128;
#undef E
}

int em_actor_light_001D8130(EmActorLight *s)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!s->world.d00275688 || !s->world.d00817BC0)
        return fail(s, 0x001D8130u, EM_OWNER_FAULT_NULL_WORKER);
    if (!lookup_ready(s)) return fail(s, lookup_fault_address(s), EM_OWNER_FAULT_NULL_WORKER);
    rig_load(&s->world);
    return 0;
}

/* ======================================================================
 * 001D8340: slot directions, camera fill and the point-light fold
 * ==================================================================== */

/* Preconditions (checked by the callers): rig record present; the view
 * matrix when flag != 0; when the gate passes, the seed, the point-light
 * table and the point. Returns the em_ee_float status and the failing
 * routine's address. */
static int compose(const EmActorLightWorld *w, int gate, u32 flag, const u32 point[4], u32 *where)
{
    u32 *g = w->d00817BC0;
    int st = EM_EE_FLOAT_OK;
    if (flag == 0) {
        g[G(0xFC)] = 0;
        g[G(0xF8)] = 0;
        g[G(0xF4)] = 0;
        g[G(0xF0)] = 0;
        g[G(0xC0)] = 0;
        g[G(0xC4)] = 0;
        g[G(0xC8)] = 0;
        g[G(0xCC)] = 0;
    }
    for (int i = 0; i < 3; ++i) {
        u32 *s1 = g + G(0x80) + 4 * i; /* angles */
        u32 *s2 = g + G(0xC0) + 4 * i; /* direction */
        float rf[16];
        /* identity, then X by angle +0, Z by angle +8, Y by angle +4. */
        st = em_owner_services_identity_001029C0(rf);
        if (st == EM_EE_FLOAT_OK) st = em_owner_services_rotate_x_00102B08(rf, rf, s1[0]);
        if (st == EM_EE_FLOAT_OK) st = em_owner_services_rotate_z_00102A60(rf, rf, s1[2]);
        if (st == EM_EE_FLOAT_OK) st = em_owner_services_rotate_y_00102BB0(rf, rf, s1[1]);
        if (st != EM_EE_FLOAT_OK) {
            *where = 0x00102A60u;
            return st;
        }
        u32 rot[16];
        f2u16(rot, rf);
        u32 qa[4] = {0, F_ONE, 0, F_ONE};
        if (i == 0) {
            if (flag != 0) {
                /* Camera fill: im = transpose(view); qa = rot x (0,1,0,1);
                 * qa.w = 0; direction = im x qa; direction.w = 1.0. */
                u32 im[16];
                memcpy(im, w->d00810610, sizeof im);
                sdk_00102798(im, im);
                sdk_001026A0(&st, qa, rot, qa);
                qa[3] = 0;
                sdk_001026A0(&st, s2, im, qa);
                if (st != EM_EE_FLOAT_OK) {
                    *where = 0x001026A0u;
                    return st;
                }
                s2[3] = F_ONE;
            } else {
                s2[0] = 0;
                s2[1] = 0;
                s2[2] = 0;
                s2[3] = 0;
                s1[0] = 0;
                s1[1] = 0;
                s1[2] = 0;
                s1[3] = 0;
            }
        } else {
            sdk_001026A0(&st, s2, rot, qa);
            if (st != EM_EE_FLOAT_OK) {
                *where = 0x001026A0u;
                return st;
            }
        }
    }
    if (!gate) return EM_EE_FLOAT_OK;

    /* The fold. acc = seed + slot 0 direction x slot 0 weight; wacc =
     * slot 0 colour. The second seed (D_00253180) is stored to the wacc
     * slot and immediately overwritten by the copy, so it is never read. */
    u32 acc[4], wacc[4], tmp[4];
    memcpy(acc, w->d00253170, sizeof acc);
    sdk_00102900(&st, tmp, g + G(0xC0), g[G(0xFC)]);
    sdk_001028B8(&st, acc, acc, tmp);
    memcpy(wacc, g + G(0xF0), sizeof wacc);
    if (st != EM_EE_FLOAT_OK) {
        *where = 0x00102900u;
        return st;
    }
    for (int j = 0; j < EM_ACTOR_LIGHT_POINTS; ++j) {
        const u32 *e = w->ctx_0220 + EM_ACTOR_LIGHT_POINT_WORDS * j;
        u32 weight = e[G(0x2C)];
        if (em_ee_c_le_bits(weight, F_ZERO)) continue;
        u32 probe[4];
        sdk_001028D0(&st, probe, e + G(0x10), point);
        u32 len = sdk_00102738(&st, probe, probe);  /* the squared distance */
        if (st != EM_EE_FLOAT_OK) {
            *where = 0x00102738u;
            return st;
        }
        if (em_ee_c_lt_bits(len, F_ONE)) len = F_ONE;
        u32 f20 = em_ee_div_bits(em_ee_mul_bits(F_TENTH, e[G(0x2C)]), len);
        sdk_00102900(&st, tmp, probe, em_ee_mul_bits(F_TEN, f20));
        sdk_001026A0(&st, tmp, e + G(0x40), tmp);
        sdk_001028B8(&st, acc, acc, tmp);
        sdk_00102900(&st, tmp, e + G(0x20), em_ee_mul_bits(F_TWO, f20));
        sdk_001028B8(&st, wacc, wacc, tmp);
        if (st != EM_EE_FLOAT_OK) {
            *where = 0x001026A0u;
            return st;
        }
    }
    u32 res[4];
    sdk_00102760(&st, res, acc);
    if (st != EM_EE_FLOAT_OK) {
        *where = 0x00102760u;
        return st;
    }
    memcpy(g + G(0xC0), res, sizeof res);
    memcpy(g + G(0xF0), wacc, sizeof wacc);
    return EM_EE_FLOAT_OK;
}

/* The views 001D8340 will reach, checked before any store. */
static int compose_ready(EmActorLight *s, int gate, u32 flag, const u32 point[4])
{
    const EmActorLightWorld *w = &s->world;
    if (!w->d00817BC0) return fail(s, 0x001D8340u, EM_OWNER_FAULT_NULL_WORKER);
    if (flag != 0 && !w->d00810610) return fail(s, 0x001D8340u, EM_OWNER_FAULT_NULL_WORKER);
    if (gate && (!w->d00253170 || !w->ctx_0220 || !point))
        return fail(s, 0x001D8340u, EM_OWNER_FAULT_NULL_WORKER);
    return 0;
}

int em_actor_light_001D8340(EmActorLight *s, const EmActorLightOwner *o, uint32_t flag,
                            const uint32_t point[4])
{
    if (!s) return -1;
    if (latched(s)) return -1;
    int gate = 0;
    if (o) {
        gate = gate_value(o);
        if (gate < 0) return fail(s, 0x001D8270u, EM_OWNER_FAULT_NULL_WORKER);
    }
    if (compose_ready(s, gate, flag, point) != 0) return -1;
    u32 where = 0;
    if (compose(&s->world, gate, flag, point, &where) != EM_EE_FLOAT_OK)
        return fail(s, where, EM_OWNER_FAULT_UNMEASURED_FORM);
    return 0;
}

/* ======================================================================
 * 001D8690: the A and B matrices
 * ==================================================================== */

static void matrices(const u32 *g, u32 a[16], u32 b[16], const u32 rgb[4])
{
    /* c = rgb.w - 1.0, clamped at +0 when c <= 0 (EE compare). */
    u32 c = em_ee_sub_bits(rgb[3], F_ONE);
    if (em_ee_c_le_bits(c, F_ZERO)) c = F_ZERO;
    u32 bw = em_ee_add_bits(F_BIAS, em_ee_mul_bits(F_SIXTY_FOUR, c));
    /* A: the three slot directions as columns 0..2, slot 0's colour xyz in
     * the w lanes of rows 0..2, row 3 zero. Stored in the original order. */
    a[0] = g[G(0xC0)];
    a[4] = g[G(0xC4)];
    a[8] = g[G(0xC8)];
    a[12] = 0;
    a[1] = g[G(0xD0)];
    a[5] = g[G(0xD4)];
    a[9] = g[G(0xD8)];
    a[13] = 0;
    a[2] = g[G(0xE0)];
    a[6] = g[G(0xE4)];
    a[10] = g[G(0xE8)];
    a[14] = 0;
    a[3] = g[G(0xF0)];
    a[7] = g[G(0xF4)];
    a[11] = g[G(0xF8)];
    a[15] = 0;
    /* B rows 0..2: slot colour xyz times rgb xyz, slot w times c; row 3:
     * 8388608 + ambient times rgb, and 8388608 + 64c. */
    for (int r = 0; r < 3; ++r) {
        const u32 *col = g + G(0xF0) + 4 * r;
        b[4 * r + 0] = em_ee_mul_bits(col[0], rgb[0]);
        b[4 * r + 1] = em_ee_mul_bits(col[1], rgb[1]);
        b[4 * r + 2] = em_ee_mul_bits(col[2], rgb[2]);
        b[4 * r + 3] = em_ee_mul_bits(col[3], c);
    }
    b[12] = em_ee_add_bits(F_BIAS, em_ee_mul_bits(g[G(0x120)], rgb[0]));
    b[13] = em_ee_add_bits(F_BIAS, em_ee_mul_bits(g[G(0x124)], rgb[1]));
    b[14] = em_ee_add_bits(F_BIAS, em_ee_mul_bits(g[G(0x128)], rgb[2]));
    b[15] = bw;
}

int em_actor_light_001D8690(EmActorLight *s, uint32_t a[16], uint32_t b[16], const uint32_t rgb[4])
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!s->world.d00817BC0 || !a || !b || !rgb) return fail(s, 0x001D8690u, EM_OWNER_FAULT_NULL_WORKER);
    matrices(s->world.d00817BC0, a, b, rgb);
    return 0;
}

/* ======================================================================
 * 001D8C30: the non-rig modes
 * ==================================================================== */

static void biased_row_128(u32 out[3], const u32 in[4])
{
    for (int k = 0; k < 3; ++k) out[k] = em_ee_add_bits(F_BIAS, em_ee_mul_bits(F_128, in[k]));
}

static void mode_rows(const EmActorLightWorld *w, uint32_t mode, u32 a[16], u32 b[16], const u32 in[4])
{
    /* t = in.w - 1.0, clamped at +0 when t <= 0. */
    u32 t = em_ee_sub_bits(in[3], F_ONE);
    if (em_ee_c_le_bits(t, F_ZERO)) t = F_ZERO;
    u32 tw = em_ee_add_bits(F_BIAS, em_ee_mul_bits(F_SIXTY_FOUR, t));
    /* Modes 0..6 go through the seven-entry table (0 and 1 share the first
     * case); every other value, as an unsigned compare, the first case. */
    switch (mode < 7u ? mode : 0u) {
    case 2:
        memset(a, 0, 16 * sizeof(u32));
        memset(b, 0, 12 * sizeof(u32));
        for (int k = 0; k < 3; ++k) b[12 + k] = em_ee_add_bits(F_BIAS, in[k]);
        b[15] = tw;
        break;
    case 3:
        biased_row_128(b, in);
        b[3] = tw;
        break;
    case 4:
        memcpy(a, w->ctx_2380, 16 * sizeof(u32));
        biased_row_128(b, in);
        b[3] = F_BIAS_64;
        break;
    case 5:
        memcpy(a, w->ctx_2380, 16 * sizeof(u32));
        biased_row_128(b, in);
        b[3] = em_ee_mul_bits(F_FIFTH, em_ee_mul_bits(F_128, in[3]));
        break;
    case 6:
        memcpy(a, w->ctx_2380, 16 * sizeof(u32));
        biased_row_128(b, in);
        b[3] = em_ee_add_bits(F_BIAS, em_ee_mul_bits(F_128, in[3]));
        break;
    default: /* 0, 1 and >= 7 */
        memset(a, 0, 16 * sizeof(u32));
        memset(b, 0, 12 * sizeof(u32));
        for (int k = 0; k < 3; ++k) b[12 + k] = em_ee_add_bits(F_BIAS, em_ee_add_bits(F_128, in[k]));
        b[15] = tw;
        break;
    }
}

static int mode_ready(EmActorLight *s, uint32_t mode, const u32 *a, const u32 *b, const u32 *in)
{
    uint32_t m = mode < 7u ? mode : 0u;
    if (!in || !b || (m != 3 && !a)) return fail(s, 0x001D8C30u, EM_OWNER_FAULT_NULL_WORKER);
    if (m >= 4 && !s->world.ctx_2380) return fail(s, 0x001D8C30u, EM_OWNER_FAULT_NULL_WORKER);
    return 0;
}

int em_actor_light_001D8C30(EmActorLight *s, int32_t mode, uint32_t a[16], uint32_t b[16],
                            const uint32_t in[4])
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (mode_ready(s, (uint32_t)mode, a, b, in) != 0) return -1;
    mode_rows(&s->world, (uint32_t)mode, a, b, in);
    return 0;
}

/* ======================================================================
 * 001D89D0: the driver
 * ==================================================================== */

int em_actor_light_001D89D0(EmActorLight *s, const EmActorLightOwner *o, uint32_t a[16],
                            uint32_t b[16], const uint32_t arg3[4])
{
    if (!s) return -1;
    if (latched(s)) return -1;
    const EmActorLightWorld *w = &s->world;
    if (!w->ctx_246C) return fail(s, 0x001D89D0u, EM_OWNER_FAULT_NULL_WORKER);
    int32_t mode = *w->ctx_246C;
    /* Modes 1, 3, 4, 5 and 6 pass a, b and arg3 through to 001D8C30 and
     * never read the owner. */
    if (mode == 1 || mode == 3 || mode == 4 || mode == 5 || mode == 6)
        return em_actor_light_001D8C30(s, mode, a, b, arg3);
    if (!o) return fail(s, 0x001D89D0u, EM_OWNER_FAULT_NULL_WORKER);

    /* The rig path. The light point: +0xB0, or node(+0x110 + 4 * sub) + 0xC0. */
    const u32 *point;
    if (o->pose_bone == 0xFF) {
        point = o->pos;
    } else {
        if (!o->node_c0 || o->pose_bone >= o->node_count)
            return fail(s, 0x001D89D0u, EM_OWNER_FAULT_BAD_INDEX);
        point = o->node_c0[o->pose_bone];
    }
    u32 flag = o->cls & 0x20u;
    int gate = gate_value(o);
    if (gate < 0) return fail(s, 0x001D8270u, EM_OWNER_FAULT_NULL_WORKER);
    if (!a || !b || !arg3) return fail(s, 0x001D89D0u, EM_OWNER_FAULT_NULL_WORKER);
    if (!w->d00275688 || !w->d00817BC0) return fail(s, 0x001D89D0u, EM_OWNER_FAULT_NULL_WORKER);
    if (!lookup_ready(s)) return fail(s, lookup_fault_address(s), EM_OWNER_FAULT_NULL_WORKER);
    if (compose_ready(s, gate, flag, point) != 0) return -1;

    /* D_00275688 = D_00817BC0 (stored here and again by 001D8130). */
    *w->d00275688 = EM_ACTOR_LIGHT_RIG_ADDRESS;
    rig_load(w);
    u32 where = 0;
    if (compose(w, gate, flag, point, &where) != EM_EE_FLOAT_OK)
        return fail(s, where, EM_OWNER_FAULT_UNMEASURED_FORM);
    matrices(w->d00817BC0, a, b, arg3);

    if (o->cls & 0x40u) {
        /* The glow add, from the owner's own +0x80 words (not arg3): B row 3
         * xyz += 64 * rgb xyz; B.w += 64 * max(rgb.w - 1.0, +0). */
        for (int k = 0; k < 3; ++k)
            b[12 + k] = em_ee_add_bits(b[12 + k], em_ee_mul_bits(F_SIXTY_FOUR, o->rgb[k]));
        u32 c = em_ee_sub_bits(o->rgb[3], F_ONE);
        if (em_ee_c_le_bits(c, F_ZERO)) c = F_ZERO;
        b[15] = em_ee_add_bits(b[15], em_ee_mul_bits(F_SIXTY_FOUR, c));
    }
    return 0;
}

/* ======================================================================
 * Binding as EmOwnerServicesWorkers.w_001D89D0
 * ==================================================================== */

int em_actor_light_w_001D89D0(void *binding, const EmOwnerServicesOwner *owner, float a[16],
                              float b[16])
{
    EmActorLightBinding *bind = (EmActorLightBinding *)binding;
    if (!bind || !bind->light) return -1;
    EmActorLight *s = bind->light;
    if (latched(s)) return -1;
    if (!owner || !a || !b) return fail(s, 0x001D89D0u, EM_OWNER_FAULT_NULL_WORKER);
    if (!bind->w_owner_rgb) return fail(s, 0x001D89D0u, EM_OWNER_FAULT_NULL_WORKER);

    EmActorLightOwner o;
    memset(&o, 0, sizeof o);
    o.cls = owner->cls;
    o.kind = owner->kind;
    o.pose_bone = owner->pose_bone;
    memcpy(o.pos, owner->pos, sizeof o.pos);
    u32 radius = 0;
    if (owner->model) {
        memcpy(&radius, &owner->model->radius, sizeof radius);
        o.model_radius = &radius;
    }
    const u32 *nodes[EM_OWNER_SERVICES_MAX_BONES];
    for (int k = 0; k < EM_OWNER_SERVICES_MAX_BONES; ++k) {
        const EmOwnerBone *bone = owner->bone[k];
        nodes[k] = bone ? (const u32 *)(const void *)(bone->world + 12) : NULL;
    }
    o.node_c0 = nodes;
    o.node_count = EM_OWNER_SERVICES_MAX_BONES;
    if (bind->w_owner_rgb(bind->ctx, owner, o.rgb) < 0)
        return fail(s, 0x001D89D0u, EM_OWNER_FAULT_WORKER_FAILED);

    u32 ua[16], ub[16];
    f2u16(ua, a);
    f2u16(ub, b);
    if (em_actor_light_001D89D0(s, &o, ua, ub, o.rgb) != 0) return -1;
    u2f16(a, ua);
    u2f16(b, ub);
    return 0;
}
