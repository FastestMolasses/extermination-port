/* Shared owner model services. See em_owner_services_original.h and
 * docs/OWNER_SERVICES.md. Every routine names the original address it
 * translates; tools/test_owner_services_reference.py runs the original
 * instructions against it.
 *
 * Float arithmetic: every EE COP1 and VU0-macro instruction goes through
 * game/em_ee_float.h (docs/EE_FLOAT_MODEL.md section 6), named by its real
 * form (op, dest, bc). No host float operation is performed here. A nonzero
 * em_ee_float status is a fault (EM_OWNER_FAULT_UNMEASURED_FORM). */
#include "game/em_owner_services_original.h"

#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

typedef uint32_t u32;

/* ======================================================================
 * VU0 macro register plumbing
 * ==================================================================== */

/* Dest masks (x = 8, y = 4, z = 2, w = 1). */
#define DX 8u
#define DY 4u
#define DZ 2u
#define DW 1u
#define DXY 12u
#define DXYZ 14u
#define DYZW 7u
#define DZW 3u
#define DXYZW 15u
#define NO_BC EM_VU_NO_BC

/* The VU constant register: (0, 0, 0, 1.0). */
static const u32 VF0[4] = {0, 0, 0, EM_EE_ONE};

/* One VU0 macro instruction on 4-lane registers. `*st` is sticky: once an
 * instruction reports a nonzero status, nothing later is executed and the
 * routine's caller faults. */
static void vu(int *st, em_vu_op op, unsigned dest, int bc, const u32 fs[4], const u32 ft[4], u32 q,
               const u32 acc[4], u32 dst[4])
{
    if (*st != EM_EE_FLOAT_OK) return;
    *st = em_vu_vec_bits(op, dest, bc, fs, ft, q, acc, dst);
}

/* The row transform shared by 00102A60/B08/BB0, 001C9610 and 001C7420:
 * out = M row 0 * v.x + M row 1 * v.y + M row 2 * v.z + M row 3 * v.w, all
 * four lanes, summed left to right in the VU accumulator (forms MULABC /0,
 * MADDABC /1 and /2, and MADDBC /3 writing out). */
static void vu_row(int *st, u32 out[4], const u32 m[16], const u32 v[4])
{
    u32 acc[4] = {0, 0, 0, 0};
    vu(st, EM_VU_MULABC, DXYZW, 0, m, v, 0, NULL, acc);
    vu(st, EM_VU_MADDABC, DXYZW, 1, m + 4, v, 0, acc, acc);
    vu(st, EM_VU_MADDABC, DXYZW, 2, m + 8, v, 0, acc, acc);
    vu(st, EM_VU_MADDBC, DXYZW, 3, m + 12, v, 0, acc, out);
}

/* dst rows = src rows x M. The whole matrix is read first; each source row
 * is read before its result row is stored, so dst may equal src. Returns the
 * em_ee_float status; on a nonzero status the failing row and the rows after
 * it are not stored. */
static int vu_transform(u32 dst[16], const u32 m[16], const u32 src[16])
{
    u32 mm[16];
    int st = EM_EE_FLOAT_OK;
    memcpy(mm, m, sizeof mm);
    for (int row = 0; row < 4; ++row) {
        u32 v[4], out[4] = {0, 0, 0, 0};
        memcpy(v, src + 4 * row, sizeof v);
        vu_row(&st, out, mm, v);
        if (st != EM_EE_FLOAT_OK) return st;
        memcpy(dst + 4 * row, out, sizeof out);
    }
    return st;
}

static void load16(u32 out[16], const float in[16]) { memcpy(out, in, 16 * sizeof(u32)); }
static void store16(float out[16], const u32 in[16]) { memcpy(out, in, 16 * sizeof(u32)); }

/* ======================================================================
 * SDK VU0 routines
 * ==================================================================== */

/* 001029C0: a = (0, 0, 0, 0) (SUB xyzw of the constant register with itself),
 * then a.w += 1 (ADD w), giving (0, 0, 0, 1). Three lane rotations of it give
 * (0, 0, 1, 0), (0, 1, 0, 0) and (1, 0, 0, 0). Rows 3, 2, 1, 0 are stored in
 * that order of construction. */
int em_owner_services_identity_001029C0(float m[16])
{
    int st = EM_EE_FLOAT_OK;
    u32 v4[4] = {0, 0, 0, 0}, v5[4], v6[4], v7[4];
    vu(&st, EM_VU_SUB, DXYZW, NO_BC, VF0, VF0, 0, NULL, v4);
    vu(&st, EM_VU_ADD, DW, NO_BC, v4, VF0, 0, NULL, v4);
    if (st != EM_EE_FLOAT_OK) return st;
    /* Each lane rotation is a plain move: next = (prev.y, prev.z, prev.w, prev.x). */
    v5[0] = v4[1]; v5[1] = v4[2]; v5[2] = v4[3]; v5[3] = v4[0];
    v6[0] = v5[1]; v6[1] = v5[2]; v6[2] = v5[3]; v6[3] = v5[0];
    v7[0] = v6[1]; v7[1] = v6[2]; v7[2] = v6[3]; v7[3] = v6[0];
    u32 b[16];
    memcpy(b + 0, v7, 16);
    memcpy(b + 4, v6, 16);
    memcpy(b + 8, v5, 16);
    memcpy(b + 12, v4, 16);
    store16(m, b);
    return st;
}

/* D_00241100, lanes x..w (the oracle asserts these equal the user's ELF). */
static const u32 k_sine_coefficient[4] = {0x362E9C14u, 0xB94FB21Fu, 0x3C08873Eu, 0xBE2AAAA4u};
#define F_HALF_PI 0x3FC90FDBu

/* 001029E8(t, negative): the sine/cosine pair of the rotate prologue.
 * Outputs: v4.x = sqrt(1 - s*s) signed by the angle (the sine), v4.y = s (the
 * power series, the cosine), and v5 = (0, 0, 0, 0).
 *   The entry v4 is read only in its y, z, w lanes, by a multiply by 0.0 that
 *   the next steps overwrite: y by the (s, s) store below, z and w by the
 *   callers' zw clear and then by the row reload of the transform loop. The
 *   entry value never reaches a result, so the caller passes zeros.
 *   t reaches the VU in its x lane only; the other lanes of that register,
 *   of the square-root register (y, z) and of the product register are never
 *   read at entry. */
static void sdk_001029E8(int *st, u32 t, int negative, u32 v4[4], u32 v5[4])
{
    u32 v6[4] = {t, 0, 0, 0}, v7[4] = {0, 0, 0, 0}, v8[4] = {0, 0, 0, 0}, q;
    /* 001029E8..: c = D_00241100 (c.x..c.w, highest power first); t copied
     * into the w lane; v4.x = 0 + t; then the t lane squared (u = t*t). */
    memcpy(v5, k_sine_coefficient, 16);
    v6[3] = v6[0];
    vu(st, EM_VU_ADDBC, DX, 0, VF0, v6, 0, NULL, v4);
    vu(st, EM_VU_MUL, DX, NO_BC, v6, v6, 0, NULL, v6);
    /* v4.yzw *= 0.0; p = c * t (all lanes); v5 = 0 (constant minus itself). */
    vu(st, EM_VU_MULBC, DYZW, 0, v4, VF0, 0, NULL, v4);
    vu(st, EM_VU_MULBC, DXYZW, 3, v5, v6, 0, NULL, v8);
    vu(st, EM_VU_SUB, DXYZW, NO_BC, VF0, VF0, 0, NULL, v5);
    /* Power ladder: p.xyzw *= u, then p.xyz *= u, and the partial sum takes
     * p.w (c.w t^3) first; p.xy *= u, sum += p.z; p.x *= u, sum += p.y;
     * sum += p.x. So s = t + c.w t^3 + c.z t^5 + c.y t^7 + c.x t^9, added
     * lowest power first, one x-lane add at a time. */
    vu(st, EM_VU_MULBC, DXYZW, 0, v8, v6, 0, NULL, v8);
    vu(st, EM_VU_MULBC, DXYZ, 0, v8, v6, 0, NULL, v8);
    vu(st, EM_VU_ADDBC, DX, 3, v4, v8, 0, NULL, v4);
    vu(st, EM_VU_MULBC, DXY, 0, v8, v6, 0, NULL, v8);
    vu(st, EM_VU_ADDBC, DX, 2, v4, v8, 0, NULL, v4);
    vu(st, EM_VU_MULBC, DX, 0, v8, v6, 0, NULL, v8);
    vu(st, EM_VU_ADDBC, DX, 1, v4, v8, 0, NULL, v4);
    vu(st, EM_VU_ADDBC, DX, 0, v4, v8, 0, NULL, v4);
    /* v4.xy = 0 + s; r.x = s*s; r.w = 1.0 - r.x. */
    vu(st, EM_VU_ADDBC, DXY, 0, v5, v4, 0, NULL, v4);
    vu(st, EM_VU_MUL, DX, NO_BC, v4, v4, 0, NULL, v7);
    vu(st, EM_VU_SUBBC, DW, 0, VF0, v7, 0, NULL, v7);
    if (*st != EM_EE_FLOAT_OK) return;
    /* Q = VU square root of r.w; r.x = 0 + Q; v4.x = 0 - r.x for a negative
     * angle, else 0 + r.x. */
    q = em_vu_sqrt_bits(v7[3]);
    vu(st, EM_VU_ADDQ, DX, NO_BC, VF0, NULL, q, NULL, v7);
    if (negative)
        vu(st, EM_VU_SUBBC, DX, 0, v5, v7, 0, NULL, v4);
    else
        vu(st, EM_VU_ADDBC, DX, 0, v5, v7, 0, NULL, v4);
}

/* 00102B08 (axis 0), 00102BB0 (axis 1), 00102A60 (axis 2). */
static int rotate(float dst[16], const float src[16], u32 angle, int axis)
{
    int st = EM_EE_FLOAT_OK;
    /* Prologue: t = pi/2 + angle when angle < 0.0 (EE compare), else
     * t = pi/2 - angle (EE add / sub on the raw bits). */
    int negative = em_ee_c_lt_bits(angle, 0);
    u32 t = negative ? em_ee_add_bits(F_HALF_PI, angle) : em_ee_sub_bits(F_HALF_PI, angle);
    u32 v4[4] = {0, 0, 0, 0}, v5[4], m[16];
    sdk_001029E8(&st, t, negative, v4, v5);
    if (st != EM_EE_FLOAT_OK) return st;
    /* Below: sn = v4.x, cs = v4.y, z = the zero register v5; rows 0..3 of M. */
    u32 *v6 = m, *v7 = m + 4, *v8 = m + 8, *v9 = m + 12;
    if (axis == 2) {
        /* 00102A60 (Z): rows 0 and 1 = z; row 3 = (0, 0, 0, 1) by clearing
         * xyz of the constant register (SUB xyz); row 2 = row 3 rotated one
         * lane, (0, 0, 1, 0). Then v4.zw cleared (SUB zw, not read again). */
        memcpy(v6, v5, 16);
        memcpy(v7, v5, 16);
        memcpy(v9, VF0, 16);
        vu(&st, EM_VU_SUB, DXYZ, NO_BC, v9, v9, 0, NULL, v9);
        v8[0] = v9[1]; v8[1] = v9[2]; v8[2] = v9[3]; v8[3] = v9[0];
        vu(&st, EM_VU_SUB, DZW, NO_BC, v4, v4, 0, NULL, v4);
        /* row 0 = (0 + cs, 0 + sn, 0, 0); row 1 = (0 - sn, 0 + cs, 0, 0). */
        vu(&st, EM_VU_ADDBC, DY, 0, v5, v4, 0, NULL, v6);
        vu(&st, EM_VU_ADDBC, DX, 1, v5, v4, 0, NULL, v6);
        vu(&st, EM_VU_SUBBC, DX, 0, v5, v4, 0, NULL, v7);
        vu(&st, EM_VU_ADDBC, DY, 1, v5, v4, 0, NULL, v7);
    } else {
        /* 00102B08 / 00102BB0: rows 0..3 all start as z. */
        memcpy(v6, v5, 16);
        memcpy(v7, v5, 16);
        memcpy(v8, v5, 16);
        memcpy(v9, v5, 16);
        if (axis == 0) {
            /* 00102B08 (X): row 0.x = 0 + 1.0, row 3.w = 0 + 1.0; v4.zw
             * cleared; row 1 = (0, 0 + cs, 0 + sn, 0) (z lane first);
             * row 2 = (0, 0 - sn, 0 + cs, 0). */
            vu(&st, EM_VU_ADDBC, DX, 3, v5, VF0, 0, NULL, v6);
            vu(&st, EM_VU_ADDBC, DW, 3, v5, VF0, 0, NULL, v9);
            vu(&st, EM_VU_SUB, DZW, NO_BC, v4, v4, 0, NULL, v4);
            vu(&st, EM_VU_ADDBC, DZ, 0, v5, v4, 0, NULL, v7);
            vu(&st, EM_VU_ADDBC, DY, 1, v5, v4, 0, NULL, v7);
            vu(&st, EM_VU_SUBBC, DY, 0, v5, v4, 0, NULL, v8);
            vu(&st, EM_VU_ADDBC, DZ, 1, v5, v4, 0, NULL, v8);
        } else {
            /* 00102BB0 (Y): row 1.y = 0 + 1.0, row 3.w = 0 + 1.0; v4.zw
             * cleared; row 0 = (0 + cs, 0, 0 - sn, 0) (z lane first);
             * row 2 = (0 + sn, 0, 0 + cs, 0) (x lane first). */
            vu(&st, EM_VU_ADDBC, DY, 3, v5, VF0, 0, NULL, v7);
            vu(&st, EM_VU_ADDBC, DW, 3, v5, VF0, 0, NULL, v9);
            vu(&st, EM_VU_SUB, DZW, NO_BC, v4, v4, 0, NULL, v4);
            vu(&st, EM_VU_SUBBC, DZ, 0, v5, v4, 0, NULL, v6);
            vu(&st, EM_VU_ADDBC, DX, 1, v5, v4, 0, NULL, v6);
            vu(&st, EM_VU_ADDBC, DX, 0, v5, v4, 0, NULL, v8);
            vu(&st, EM_VU_ADDBC, DZ, 1, v5, v4, 0, NULL, v8);
        }
    }
    if (st != EM_EE_FLOAT_OK) return st;
    /* For each of the four rows: dst row = src row x M (the row transform);
     * the source row is read before its result is stored. */
    for (int row = 0; row < 4; ++row) {
        u32 v[4], out[4] = {0, 0, 0, 0};
        memcpy(v, src + 4 * row, sizeof v);
        vu_row(&st, out, m, v);
        if (st != EM_EE_FLOAT_OK) return st;
        memcpy(dst + 4 * row, out, sizeof out);
    }
    return st;
}

int em_owner_services_rotate_x_00102B08(float dst[16], const float src[16], uint32_t angle)
{
    return rotate(dst, src, angle, 0);
}

int em_owner_services_rotate_y_00102BB0(float dst[16], const float src[16], uint32_t angle)
{
    return rotate(dst, src, angle, 1);
}

int em_owner_services_rotate_z_00102A60(float dst[16], const float src[16], uint32_t angle)
{
    return rotate(dst, src, angle, 2);
}

static u32 word_at(const float *p)
{
    u32 w;
    memcpy(&w, p, sizeof w);
    return w;
}

/* 00102C58: each angle word is read from `angles` right before its rotate. */
int em_owner_services_euler_00102C58(float dst[16], const float src[16], const float angles[3])
{
    int st = em_owner_services_rotate_z_00102A60(dst, src, word_at(angles + 2));
    if (st == EM_EE_FLOAT_OK) st = em_owner_services_rotate_y_00102BB0(dst, dst, word_at(angles + 1));
    if (st == EM_EE_FLOAT_OK) st = em_owner_services_rotate_x_00102B08(dst, dst, word_at(angles + 0));
    return st;
}

/* 00102918: dst rows 0..2 = src rows 0..2 (raw copies); dst row 3 xyz =
 * src row 3 xyz + v (VU ADD xyz); row 3 w is carried over unchanged. */
int em_owner_services_translate_00102918(float dst[16], const float src[16], const float v[3])
{
    int st = EM_EE_FLOAT_OK;
    u32 in[16], t[4] = {0, 0, 0, 0}, r3[4];
    memcpy(t, v, 3 * sizeof(u32));                  /* an xyz add: v's w lane is not read */
    load16(in, src);
    memcpy(r3, in + 12, sizeof r3);
    vu(&st, EM_VU_ADD, DXYZ, NO_BC, r3, t, 0, NULL, r3);
    if (st != EM_EE_FLOAT_OK) return st;
    memcpy(in + 12, r3, sizeof r3);
    store16(dst, in);
    return st;
}

/* build_trs_matrix (0x001C94B0). */
int em_owner_services_build_trs_matrix(float m[16], const float pos[3], const float rot[3],
                                       const float scale[3])
{
    int st = em_owner_services_identity_001029C0(m);
    if (st == EM_EE_FLOAT_OK) st = em_owner_services_rotate_x_00102B08(m, m, word_at(rot + 0));
    if (st == EM_EE_FLOAT_OK) st = em_owner_services_rotate_y_00102BB0(m, m, word_at(rot + 1));
    if (st == EM_EE_FLOAT_OK) st = em_owner_services_rotate_z_00102A60(m, m, word_at(rot + 2));
    if (st != EM_EE_FLOAT_OK) return st;
    u32 b[16], s[4] = {0, 0, 0, 0};
    load16(b, m);                                   /* the rotated matrix */
    memcpy(s, scale, 3 * sizeof(u32));              /* scale x, y, z (its w lane is not read) */
    /* rows 0, 1, 2 xyz *= scale x, y, z respectively; row 3 untouched. */
    vu(&st, EM_VU_MULBC, DXYZ, 0, b + 0, s, 0, NULL, b + 0);
    vu(&st, EM_VU_MULBC, DXYZ, 1, b + 4, s, 0, NULL, b + 4);
    vu(&st, EM_VU_MULBC, DXYZ, 2, b + 8, s, 0, NULL, b + 8);
    if (st != EM_EE_FLOAT_OK) return st;
    store16(m, b);                                  /* all four rows stored back */
    return em_owner_services_translate_00102918(m, m, pos);
}

void em_owner_services_copy_qw4_00102958(float dst[16], const float src[16])
{
    memmove(dst, src, 16 * sizeof *dst);
}


/* ======================================================================
 * Fault plumbing
 * ==================================================================== */

static int latched(const EmOwnerServices *s)
{
    return s->fault.code != EM_OWNER_FAULT_NONE;
}

static int fault(EmOwnerServices *s, u32 address, int32_t code)
{
    if (!latched(s)) {
        s->fault.address = address;
        s->fault.code = code;
    }
    return -1;
}

static int worker_result(EmOwnerServices *s, u32 address, int result)
{
    return result < 0 ? fault(s, address, EM_OWNER_FAULT_WORKER_FAILED) : 0;
}

#define NEED(s, ptr, address) \
    do { if (!(ptr)) return fault((s), (address), EM_OWNER_FAULT_NULL_WORKER); } while (0)
#define CALL(s, address, expr) \
    do { if (worker_result((s), (address), (expr)) < 0) return -1; } while (0)
/* A nonzero em_ee_float status (a form the float model refuses) faults at
 * the original routine that executes the instruction. */
#define FLOAT(s, address, expr) \
    do { if ((expr) != EM_EE_FLOAT_OK) return fault((s), (address), EM_OWNER_FAULT_UNMEASURED_FORM); } while (0)

/* ======================================================================
 * Model binding and bones
 * ==================================================================== */

/* 001C6150(model): model +0x08. */
static int model_bone_count(EmOwnerServices *s, const EmOwnerServicesOwner *o, uint8_t *count)
{
    if (!o->model) return fault(s, 0x001C6150u, EM_OWNER_FAULT_BAD_INDEX);
    *count = o->model->bone_count;
    return 0;
}

/* The shared tail of 001B0EA0 and 001B0DC0, from the +0x0C store. */
static int allocate_bones(EmOwnerServices *s, EmOwnerServicesOwner *o, u32 function)
{
    uint8_t count;
    if (model_bone_count(s, o, &count) < 0) return -1;
    o->bone_count = count;                          /* +0x0C = count (byte) */
    NEED(s, s->world.d00275BCC, 0x00275BCCu);
    if ((int32_t)*s->world.d00275BCC < (int32_t)o->bone_count) {   /* signed halfword cap < count */
        o->lifecycle = 3;
        return 1;
    }
    int i = 0;
    for (; i < (int)o->bone_count; ++i) {           /* +0x0C re-read on every pass */
        if (i >= EM_OWNER_SERVICES_MAX_BONES) return fault(s, function, EM_OWNER_FAULT_BAD_INDEX);
        EmOwnerBone *slot = NULL;
        NEED(s, s->workers.w_001AF780, 0x001AF780u);
        CALL(s, 0x001AF780u, s->workers.w_001AF780(s->workers.ctx, &slot));
        o->bone[i] = slot;                          /* +0x110 slot i = the popped slot */
    }
    o->bones_held = o->bone_count;                  /* +0x09 = +0x0C */
    NEED(s, s->workers.w_anim_bone_array_setup, 0x001CB5B0u);
    CALL(s, 0x001CB5B0u, s->workers.w_anim_bone_array_setup(s->workers.ctx, o->bone_count));
    return 0;
}

static int bind_model(EmOwnerServices *s, EmOwnerServicesOwner *o, const uint32_t *bank,
                      u32 bank_address, uint32_t id)
{
    uint32_t handle = 0;
    NEED(s, bank, bank_address);
    NEED(s, s->workers.w_001C6120, 0x001C6120u);
    CALL(s, 0x001C6120u, s->workers.w_001C6120(s->workers.ctx, *bank, id, &handle));
    NEED(s, s->workers.w_001CA6E0, 0x001CA6E0u);
    CALL(s, 0x001CA6E0u, s->workers.w_001CA6E0(s->workers.ctx, o, handle));
    return 0;
}

int em_owner_services_001B0EA0(EmOwnerServices *s, EmOwnerServicesOwner *o)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!o) return fault(s, 0x001B0EA0u, EM_OWNER_FAULT_NULL_WORKER);
    if (bind_model(s, o, s->world.d0028A59C, 0x0028A59Cu, o->model_id) < 0) return -1;
    return allocate_bones(s, o, 0x001B0EA0u);
}

int em_owner_services_001B0DC0(EmOwnerServices *s, EmOwnerServicesOwner *o, uint32_t a1, int32_t a2)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!o) return fault(s, 0x001B0DC0u, EM_OWNER_FAULT_NULL_WORKER);
    if (bind_model(s, o, s->world.d0028A56C, 0x0028A56Cu, a1) < 0) return -1;
    if (a2 != -1) {
        NEED(s, s->world.d0028A490, 0x0028A490u);
        if (a2 < 0 || (uint32_t)a2 >= s->world.d0028A490_count)
            return fault(s, 0x0028A490u, EM_OWNER_FAULT_BAD_INDEX);
        o->anim = s->world.d0028A490[a2];           /* +0x40 = D_0028A490[a2] */
    }
    return allocate_bones(s, o, 0x001B0DC0u);
}

int em_owner_services_001C62C0(EmOwnerServices *s, EmOwnerServicesOwner *o)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!o) return fault(s, 0x001C62C0u, EM_OWNER_FAULT_NULL_WORKER);
    if (!o->model) return fault(s, 0x001C62C0u, EM_OWNER_FAULT_BAD_INDEX);   /* model = +0x44 */
    const EmOwnerModel *m = o->model;
    for (int i = 0; i < (int)o->bone_count; ++i) {  /* +0x0C re-read on every pass */
        if (i >= EM_OWNER_SERVICES_MAX_BONES || !m->skeleton || (uint32_t)i >= m->skeleton_records)
            return fault(s, 0x001C62C0u, EM_OWNER_FAULT_BAD_INDEX);
        EmOwnerBone *node = o->bone[i];
        if (!node) return fault(s, 0x001C62C0u, EM_OWNER_FAULT_BAD_INDEX);
        const EmOwnerSkeletonRecord *rec = &m->skeleton[i];
        node->parent = rec->parent;                 /* node +0x64 = record +0x04 (halfword) */
        node->scale[0] = node->scale[1] = node->scale[2] = 0x1000;  /* node +0x88/+0x8A/+0x8C */
        memset(node->trans, 0, sizeof node->trans); /* node +0x7C..+0x87 = 0 */
        memset(node->rot, 0, sizeof node->rot);     /* node +0x70..+0x7B = 0 */
        memmove(node->bind, rec->bind, sizeof node->bind); /* node +0x00..+0x3F = record +0x10..+0x4F */
    }
    return 0;
}

int em_owner_services_001B0FD0(EmOwnerServices *s, EmOwnerServicesOwner *o)
{
    int r = em_owner_services_001B0EA0(s, o);
    if (r < 0) return -1;
    if (r != 0) return 1;
    if (em_owner_services_001C62C0(s, o) < 0) return -1;
    o->lifecycle = (uint8_t)(o->lifecycle + 1);     /* +0x04 += 1 (byte, wraps) */
    return 0;
}

int em_owner_services_001B1020(EmOwnerServices *s, EmOwnerServicesOwner *o,
                               uint32_t a1, int32_t a2, int32_t a3)
{
    int r = em_owner_services_001B0DC0(s, o, a1, a2);
    if (r < 0) return -1;
    if (r != 0) return 1;
    o->lifecycle = (uint8_t)(o->lifecycle + 1);
    if (a2 == -1) {
        if (em_owner_services_001C62C0(s, o) < 0) return -1;
    } else {
        NEED(s, s->workers.w_bone_init_default_2, 0x001C63E0u);
        CALL(s, 0x001C63E0u, s->workers.w_bone_init_default_2(s->workers.ctx, o,
                                                              (int16_t)(a3 & 0xFFFF)));
    }
    return 0;
}

/* ======================================================================
 * Placement
 * ==================================================================== */

int em_owner_services_001C9610(EmOwnerServices *s, EmOwnerBone *const *bones, int32_t count,
                               const float root[16])
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (count <= 0) return 0;
    NEED(s, bones, 0x001C9610u);
    NEED(s, root, 0x001C9610u);
    NEED(s, s->world.scratch, 0x70003400u);
    if (count > EM_OWNER_SERVICES_MAX_BONES) return fault(s, 0x001C9610u, EM_OWNER_FAULT_BAD_INDEX);
    for (int i = 0; i < count; ++i)
        if (!bones[i]) return fault(s, 0x001C9610u, EM_OWNER_FAULT_BAD_INDEX);
    float *spr = s->world.scratch->s3400;
    const u32 f_2m12 = 0x39800000u;                 /* 2^-12 (1/4096) */
    for (int i = 0; i < count; ++i) {
        EmOwnerBone *n = bones[i];
        FLOAT(s, 0x001029C0u, em_owner_services_identity_001029C0(spr));        /* SPR = identity */
        FLOAT(s, 0x00102C58u, em_owner_services_euler_00102C58(spr, spr, n->rot)); /* angles: node +0x70 */
        memcpy(spr + 12, n->trans, 3 * sizeof(float));           /* SPR row 3 xyz = node +0x7C..+0x84 */
        u32 m[16];
        for (int row = 0; row < 3; ++row) {
            /* sc = 2^-12 * (float)(int16)node[+0x88 + 2 row]: EE int-to-float
             * convert, then EE multiply. */
            u32 sc = em_ee_mul_bits(f_2m12, em_ee_cvt_s_w_bits((u32)(int32_t)n->scale[row]));
            u32 v5[4] = {sc, 0, 0, 0};                            /* sc reaches the VU in lane x only */
            u32 v4[4];
            memcpy(v4, spr + 4 * row, sizeof v4);                 /* SPR row xyz *= sc (MULBC xyz/x) */
            FLOAT(s, 0x001C9610u, em_vu_vec_bits(EM_VU_MULBC, DXYZ, 0, v4, v5, 0, NULL, v4));
            memcpy(spr + 4 * row, v4, sizeof v4);
        }
        u32 bind[16];
        load16(bind, n->bind);                                    /* bind = node +0x00 */
        load16(m, spr);
        FLOAT(s, 0x001C9610u, vu_transform(m, bind, m));          /* SPR rows = SPR rows x bind */
        store16(spr, m);
        u32 parent[16], world[16];
        if (n->parent != -1) {                                    /* node +0x64 */
            if (n->parent < 0 || n->parent >= count)
                return fault(s, 0x001C9610u, EM_OWNER_FAULT_BAD_INDEX);
            load16(parent, bones[n->parent]->world);              /* parent +0x90 */
        } else {
            load16(parent, root);
        }
        FLOAT(s, 0x001C9610u, vu_transform(world, parent, m));    /* +0x90 = SPR rows x P */
        store16(n->world, world);
    }
    return 0;
}

int em_owner_services_001C6380(EmOwnerServices *s, EmOwnerServicesOwner *o)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!o) return fault(s, 0x001C6380u, EM_OWNER_FAULT_NULL_WORKER);
    FLOAT(s, 0x001C94B0u, em_owner_services_build_trs_matrix(o->world, o->pos, o->rot, o->scale));
    return em_owner_services_001C9610(s, o->bone, o->bone_count, o->world);
}

/* ======================================================================
 * Publication
 * ==================================================================== */

int em_owner_services_001B17A0(EmOwnerServices *s, EmOwnerServicesOwner *o)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!o) return fault(s, 0x001B17A0u, EM_OWNER_FAULT_NULL_WORKER);
    uint8_t flag = (uint8_t)(o->cls & ~0xE0);
    NEED(s, s->world.d00810CA5, 0x00810CA5u);
    if (*s->world.d00810CA5 == 6) {
        int gate = 0;
        switch (flag) {
        case 2:
            gate = (o->kind == 1 && (o->model_id & 1)) || o->kind == 7;
            break;
        case 8: gate = o->kind == 7; break;
        case 10: gate = o->kind == 1; break;
        case 7: gate = o->kind == 0 && o->flags2 >= 0x2D; break;
        default: break;
        }
        if (gate) {
            NEED(s, s->workers.w_001B1CE0, 0x001B1CE0u);
            CALL(s, 0x001B1CE0u, s->workers.w_001B1CE0(s->workers.ctx, o));
        }
    }
    uint8_t visible = 0;
    NEED(s, s->workers.w_001B1630, 0x001B1630u);
    /* 001B1630's three float arguments are +0xB0, +0xB4, +0xB8 as raw bits. */
    CALL(s, 0x001B1630u, s->workers.w_001B1630(s->workers.ctx, word_at(o->pos + 0), word_at(o->pos + 1),
                                                word_at(o->pos + 2), &visible));
    o->drawn = visible;
    if (o->drawn != 0) {
        NEED(s, s->workers.w_001B1B70, 0x001B1B70u);
        CALL(s, 0x001B1B70u, s->workers.w_001B1B70(s->workers.ctx, o));
    }
    return o->drawn;
}

/* ======================================================================
 * Draw
 * ==================================================================== */

static void put32(uint8_t *p, u32 v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void put_rows(uint8_t *p, const u32 m[16])
{
    for (int i = 0; i < 16; ++i) put32(p + 4 * i, m[i]);
}

/* DMA tag: byte +3 = 0x10, word +4 = 0, halfword +0 = qwc (bytes +2 and
 * +8..+0xF untouched). */
static void put_tag(uint8_t *tag, u32 qwc)
{
    tag[3] = 0x10;
    put32(tag + 4, 0);
    tag[0] = (uint8_t)qwc;
    tag[1] = (uint8_t)(qwc >> 8);
}

/* One node row normalised into a C row (001C7420):
 *   p = row.xyz * row.xyz (MUL xyz); len2 = (p.x + p.y) + p.z, summed in
 *   lane x; Q = VU square root of len2; len = 0 + Q; Q = 1.0 / len (VU
 *   divide, form (3,0)); out = (0, 0, 0, 0), then out.xyz = row.xyz * Q.
 * The w lane of p is never read (its entry value does not matter). */
static int normalise_row(u32 out[4], const u32 row[4])
{
    int st = EM_EE_FLOAT_OK;
    u32 v4[4], v5[4] = {0, 0, 0, 0}, v6[4] = {0, 0, 0, 0}, q = 0;
    memcpy(v4, row, sizeof v4);
    vu(&st, EM_VU_MUL, DXYZ, NO_BC, v4, v4, 0, NULL, v5);
    vu(&st, EM_VU_ADDBC, DX, 1, v5, v5, 0, NULL, v5);
    vu(&st, EM_VU_ADDBC, DX, 2, v5, v5, 0, NULL, v5);
    if (st != EM_EE_FLOAT_OK) return st;
    q = em_vu_sqrt_bits(v5[0]);
    vu(&st, EM_VU_ADDQ, DX, NO_BC, VF0, NULL, q, NULL, v5);
    if (st != EM_EE_FLOAT_OK) return st;
    st = em_vu_div_bits(VF0[3], v5[0], 3, 0, &q);
    vu(&st, EM_VU_SUB, DXYZW, NO_BC, VF0, VF0, 0, NULL, v6);
    vu(&st, EM_VU_MULQ, DXYZ, NO_BC, v4, NULL, q, NULL, v6);
    if (st != EM_EE_FLOAT_OK) return st;
    memcpy(out, v6, sizeof v6);
    return st;
}

int em_owner_services_001C7420(EmOwnerServices *s, const EmOwnerServicesOwner *o,
                               int32_t vuaddr, int32_t chan, uint8_t **first)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!o) return fault(s, 0x001C7420u, EM_OWNER_FAULT_NULL_WORKER);
    NEED(s, s->world.channel, 0x00275670u);
    if (chan < 0 || (uint32_t)chan >= s->world.channel_count)
        return fault(s, 0x00275670u, EM_OWNER_FAULT_BAD_INDEX);
    EmOwnerServicesChannel *ch = &s->world.channel[chan];
    NEED(s, ch->cursor, 0x00275670u);
    NEED(s, s->world.scratch, 0x70003400u);
    EmOwnerServicesScratch *spr = s->world.scratch;
    uint8_t *start = ch->cursor;                    /* cursor word D_00275670 + 0x10 + 4 chan */

    NEED(s, s->workers.w_001D89D0, 0x001D89D0u);
    CALL(s, 0x001D89D0u, s->workers.w_001D89D0(s->workers.ctx, o, spr->s3400, spr->s3440));

    if (o->bone_count > EM_OWNER_SERVICES_MAX_BONES)
        return fault(s, 0x001C7420u, EM_OWNER_FAULT_BAD_INDEX);
    for (int i = 0; i < (int)o->bone_count; ++i)
        if (!o->bone[i]) return fault(s, 0x001C7420u, EM_OWNER_FAULT_BAD_INDEX);
    /* Space: the colour packet (6 qw) plus one packet per 248-qw chunk. */
    int total = (int)o->bone_count * 8;
    int chunks = total ? (total + 0xF7) / 0xF8 : 1;
    size_t need = 0x60 + (size_t)(total + 2 * chunks) * 16;
    if (!ch->end || ch->end < ch->cursor || (size_t)(ch->end - ch->cursor) < need)
        return fault(s, 0x001C7420u, EM_OWNER_FAULT_BAD_INDEX);

    /* B's clear value: a VU register minus itself (SUB xyzw, both operands the
     * same register). That form clamps both operands, so every lane is
     * x - x of a finite value or of a same-signed zero: +0 for ANY entry
     * value. The entry register is therefore passed as zeros; the lookup is
     * still made (and a refusal faults) before any byte is written. */
    u32 v1[4] = {0, 0, 0, 0};
    FLOAT(s, 0x001C7420u, em_vu_vec_bits(EM_VU_SUB, DXYZW, NO_BC, v1, v1, 0, NULL, v1));

    /* Packet 0: CNT, 5 qw: FLUSH, STCYCL 1/1, UNPACK V4-32 x4 to vuaddr, B. */
    uint8_t *tag = ch->cursor;
    put_tag(tag, 5);
    ch->cursor = tag + 0x60;
    memset(tag + 0x10, 0, 16);                      /* quadword +0x10 = 0 */
    put32(tag + 0x14, 0x11000000u);
    put32(tag + 0x18, 0x01000101u);
    put32(tag + 0x1C, 0x6C040000u | (u32)vuaddr);
    u32 b[16];
    load16(b, spr->s3440);
    put_rows(tag + 0x20, b);                        /* B's four rows follow the header */

    for (int r = 0; r < 4; ++r) memcpy(spr->s3440 + 4 * r, v1, sizeof v1);   /* B rows 0..3 = +0 */

    int remaining = total, vu_address = 0, bone = 0;
    do {
        int chunk = remaining < 0xF9 ? remaining : 0xF8;
        tag = ch->cursor;
        put_tag(tag, (u32)(chunk + 1));
        ch->cursor = tag + (chunk + 2) * 0x10;
        memset(tag + 0x10, 0, 16);                  /* quadword +0x10 = 0 */
        put32(tag + 0x18, 0x01000101u);
        put32(tag + 0x1C, (u32)vu_address | ((u32)chunk << 16) | 0x6C000000u);
        uint8_t *dst = tag + 0x20;
        vu_address += chunk;
        remaining -= chunk;
        for (int n = chunk; n != 0; n -= 8, dst += 0x80, ++bone) {
            const EmOwnerBone *node = o->bone[bone];
            u32 vp[16], a[16], m[16], rows[16];
            load16(vp, spr->s3AC0);                 /* VP = SPR 0x70003AC0 */
            if (o->collapsed_bone == bone) {        /* +0x94 (signed halfword) */
                memcpy(spr->s3440 + 12, node->world + 12, 16);   /* B row 3 (0x70003470) = node +0xC0 */
                load16(m, spr->s3440);
                FLOAT(s, 0x001C7420u, vu_transform(rows, vp, m));      /* B rows x VP */
                put_rows(dst, rows);
                load16(a, spr->s3400);
                load16(m, spr->s3440);
                FLOAT(s, 0x001C7420u, vu_transform(rows, a, m));       /* B rows x A */
                put_rows(dst + 0x40, rows);
            } else {
                load16(m, node->world);             /* node +0x90 rows */
                FLOAT(s, 0x001C7420u, vu_transform(rows, vp, m));      /* node rows x VP */
                put_rows(dst, rows);
                for (int r = 0; r < 3; ++r) {       /* C row r (0x70003480 + 0x10 r) = normalised node row r */
                    u32 in[4], out[4];
                    memcpy(in, node->world + 4 * r, sizeof in);
                    FLOAT(s, 0x001C7420u, normalise_row(out, in));
                    memcpy(spr->s3480 + 4 * r, out, sizeof out);
                }
                memcpy(spr->s3480 + 12, node->world + 12, 16);   /* C row 3 (0x700034B0) = node +0xC0 */
                load16(a, spr->s3400);
                load16(m, spr->s3480);
                FLOAT(s, 0x001C7420u, vu_transform(rows, a, m));       /* C rows x A */
                put_rows(dst + 0x40, rows);
            }
        }
    } while (remaining != 0);
    if (first) *first = start;
    return 0;
}

int em_owner_services_001CA990(EmOwnerServices *s, EmOwnerServicesOwner *o,
                               const float position[3], uint32_t radius)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!o) return fault(s, 0x001CA990u, EM_OWNER_FAULT_NULL_WORKER);
    int32_t flags = 0;
    NEED(s, s->workers.w_001CA7B0, 0x001CA7B0u);
    CALL(s, 0x001CA7B0u, s->workers.w_001CA7B0(s->workers.ctx, position, radius, &flags));
    if (flags < -1 || flags > 0x1F) return fault(s, 0x001CA7B0u, EM_OWNER_FAULT_BAD_RESULT);
    if (flags < 0) return 0;                        /* culled: nothing is drawn */
    NEED(s, s->workers.w_001D8C20, 0x001D8C20u);
    CALL(s, 0x001D8C20u, s->workers.w_001D8C20(s->workers.ctx, 0));
    if (em_owner_services_001C7420(s, o, EM_OWNER_SERVICES_COLOR_VU_ADDRESS,
                                   EM_OWNER_SERVICES_DRAW_CHANNEL, NULL) < 0) return -1;
    NEED(s, s->workers.w_001D1F80, 0x001D1F80u);
    CALL(s, 0x001D1F80u, s->workers.w_001D1F80(s->workers.ctx, 0, 1, 0));
    NEED(s, s->workers.w_001CA940, 0x001CA940u);
    CALL(s, 0x001CA940u, s->workers.w_001CA940(s->workers.ctx, flags, o->model));
    return 0;
}

int em_owner_services_001CAA00(EmOwnerServices *s, EmOwnerServicesOwner *o)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!o) return fault(s, 0x001CAA00u, EM_OWNER_FAULT_NULL_WORKER);
    u32 radius = 0x41A00000u;                       /* 20.0 when there is no model */
    if (o->model) {                                 /* +0x44 */
        radius = word_at(&o->model->radius);        /* model +0x20, raw bits */
        if (em_ee_c_lt_bits(radius, 0x41A00000u))   /* radius < 20.0 (EE compare) */
            radius = em_ee_mul_bits(radius, 0x3F99999Au);   /* radius * 1.2 (EE multiply) */
    }
    const float *position;
    if (o->pose_bone == 0xFF) {
        position = o->pos;                          /* owner +0xB0 */
    } else {
        NEED(s, s->world.d00275B40, 0x00275B40u);
        if (o->pose_bone >= s->world.d00275B40_count || !s->world.d00275B40[o->pose_bone])
            return fault(s, 0x00275B40u, EM_OWNER_FAULT_BAD_INDEX);
        position = s->world.d00275B40[o->pose_bone]->world + 12;   /* slot + 0xC0 */
    }
    if (em_owner_services_001CA990(s, o, position, radius) < 0) return -1;
    if (o->attachment != 0) {                       /* +0x90 */
        NEED(s, s->workers.w_001CB3C0, 0x001CB3C0u);
        CALL(s, 0x001CB3C0u, s->workers.w_001CB3C0(s->workers.ctx, o));
    }
    return 0;
}

/* ======================================================================
 * Rumble
 * ==================================================================== */

int em_owner_services_001B1E20(EmOwnerServices *s, int32_t effect, int64_t duration)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (duration < 0) {                             /* the whole 64-bit register is tested */
        NEED(s, s->workers.w_001B6250, 0x001B6250u);
        CALL(s, 0x001B6250u, s->workers.w_001B6250(s->workers.ctx));
        return 0;
    }
    NEED(s, s->world.d0024D6F0, 0x0024D6F0u);
    if (effect < 0 || (uint32_t)effect >= s->world.d0024D6F0_count)
        return fault(s, 0x0024D6F0u, EM_OWNER_FAULT_BAD_INDEX);
    const uint8_t *p = s->world.d0024D6F0 + 4 * effect;
    int64_t a2 = duration != 0 ? (int64_t)(int16_t)(duration & 0xFFFF)  /* low 16 bits, sign-extended */
                               : (int64_t)p[2];                         /* record byte +2 */
    NEED(s, s->workers.w_001B61C0, 0x001B61C0u);
    CALL(s, 0x001B61C0u, s->workers.w_001B61C0(s->workers.ctx, p[0], p[1], a2, 1));
    return 0;
}

int em_owner_services_001B5B70(EmOwnerServices *s)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    NEED(s, s->world.d00810E56, 0x00810E56u);
    if (*s->world.d00810E56 == 0) return 0;
    NEED(s, s->world.d00810E68, 0x00810E68u);
    int16_t v = *s->world.d00810E68;                /* pad block +0x28 (signed halfword) */
    if (v == 0) {
        NEED(s, s->workers.w_001B6250, 0x001B6250u);
        CALL(s, 0x001B6250u, s->workers.w_001B6250(s->workers.ctx));
    } else {
        *s->world.d00810E68 = (int16_t)(v - 1);     /* halfword store: -32768 wraps to 32767 */
    }
    return 0;
}
