/* Player drop shadow, original 001DA6A0 chain. See em_shadow_original.h and
 * docs/SHADOW_ORIGINAL.md. Every float step names the original operation it
 * reproduces; tools/test_shadow_original_reference.py checks the results
 * byte for byte against the executed original instructions. */
#include "game/em_shadow_original.h"
#include "game/em_ee_float.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

/* ---- arithmetic ------------------------------------------------------- */

/* EE FPU / VU0 binary32 results truncate toward zero. The operands are
 * binary32, so one double operation is exact for + - * (bounded exponents);
 * step the rounded result one ULP toward zero when rounding went away. */
static float tr(double value)
{
    float result = (float)value;
    if (fabs((double)result) > fabs(value)) {
        uint32_t bits;
        memcpy(&bits, &result, sizeof bits);
        --bits;
        memcpy(&result, &bits, sizeof result);
    }
    return result;
}

static float add(float a, float b) { return tr((double)a + (double)b); }
static float sub(float a, float b) { return tr((double)a - (double)b); }
static float mul(float a, float b) { return tr((double)a * (double)b); }
/* EE float divide: rounded (the point-light oracle's established semantics). */
static float divide(float a, float b) { return a / b; }

static float f32_of(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static void qcopy(float dst[4], const float src[4]) { memmove(dst, src, 16); }

static void identity(float m[16])
{
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* VU0 multiply-accumulate chain over the rows r0..r3 of `m`:
 * out = r0*v.x + r1*v.y + r2*v.z + r3*v.w, every product and sum truncated
 * in that order (the macro-mode ACC chain of 001026A0/001026D0). */
static void vu0_transform(float out[4], const float m[16], const float v[4])
{
    float r[4];
    for (int lane = 0; lane < 4; ++lane) {
        float acc = mul(m[lane], v[0]);
        acc = add(acc, mul(m[4+lane], v[1]));
        acc = add(acc, mul(m[8+lane], v[2]));
        r[lane] = add(acc, mul(m[12+lane], v[3]));
    }
    memcpy(out, r, sizeof r);
}

/* 001026D0(dst, a, b): each row of b through a, i.e. dst = b x a in the
 * row-vector convention. dst may alias a or b (rows are read first). */
static void vu0_product(float dst[16], const float a[16], const float b[16])
{
    float out[16], am[16];
    memcpy(am, a, sizeof am);
    for (int row = 0; row < 4; ++row) vu0_transform(out + 4*row, am, b + 4*row);
    memcpy(dst, out, sizeof out);
}

/* 00102918(dst, src, v): rows 0..2 copied, row 3 xyz += v (one VU0 add of the xyz lanes). */
static void vu0_translate(float m[16], const float v[4])
{
    for (int lane = 0; lane < 3; ++lane) m[12+lane] = add(m[12+lane], v[lane]);
}

/* 001029E8 at angle 0: the capture proves sin = 1, cos = 0 exactly
 * (D_00817F70 = (0,-1,0,0) bit for bit); with those, 00102A60, 00102BB0 and
 * 00102B08 assemble exactly the identity (+0 zeros; the oracle executes all
 * three on a signed-zero probe). Applying it is still a VU0 product, which
 * can turn -0 into +0. */
static void vu0_rotate_zero(float m[16])
{
    float r[16];
    identity(r);
    vu0_product(m, r, m);
}

/* VU clip test of the x/y/z lanes against |w|: bits +x, -x, +y, -y, +z, -z */
static uint32_t clipw(const float v[4])
{
    float w = fabsf(v[3]);
    uint32_t f = 0;
    if (v[0] > w) f |= 1;
    if (v[0] < -w) f |= 2;
    if (v[1] > w) f |= 4;
    if (v[1] < -w) f |= 8;
    if (v[2] > w) f |= 16;
    if (v[2] < -w) f |= 32;
    return f;
}

/* float_to_int (001281C0, called by 001D5C80 for the grid cells):
 * truncation; |x| >= 2^31 saturates by sign. */
static int32_t f2i(float x)
{
    if (x >= 2147483648.0f) return INT32_MAX;
    if (x <= -2147483648.0f) return INT32_MIN;
    return (int32_t)x;
}

/* 00128250 (called by 001DA310 for the four RGBAQ bytes): the soft-float
 * float-to-unsigned conversion. After 001278C0 unpacks x: NaN, zero and any
 * negative x give 0; infinity and exponents >= 32 give 0xFFFFFFFF; negative
 * exponents give 0; otherwise the fraction is shifted, i.e. truncation. */
static uint32_t f2u_00128250(float x)
{
    if (x != x) return 0u;                    /* class < 2 (NaN) */
    if (x == 0.0f || signbit(x)) return 0u;   /* class 2, or sign set */
    if (x >= 4294967296.0f) return 0xFFFFFFFFu;
    return (uint32_t)x;
}

/* 001D2D20(m, zoom, w, h, near, far) */
static void projection(float m[16], float zoom, float w, float h, float near, float far)
{
    identity(m);
    /* EE COP1 code: the measured EE model (docs/EE_FLOAT_MODEL.md), incl.
     * the add.s/sub.s pre-trim (far - near = far on the EE). */
    m[0] = em_ee_div(zoom, em_ee_mul(0.5f, w));
    m[5] = em_ee_div(zoom, em_ee_mul(0.5f, h));
    float fn = em_ee_mul(far, near);
    float sum = em_ee_add(far, near);
    float twice = em_ee_mul(-2.0f, fn);
    float diff = em_ee_sub(far, near);
    m[10] = em_ee_div(sum, diff);
    m[14] = em_ee_div(twice, diff);
    m[11] = 1.0f;
    m[15] = 0.0f;
}

/* ---- fault plumbing --------------------------------------------------- */

static int fail(EmShadowOriginalFault *fault, uint32_t address, int32_t code)
{
    if (fault && fault->code == EM_SHADOW_FAULT_NONE) {
        fault->address = address;
        fault->code = code;
    }
    return -1;
}

static int worker(EmShadowOriginalFault *fault, uint32_t address, int result)
{
    return result < 0 ? fail(fault, address, EM_SHADOW_FAULT_WORKER_FAILED) : 0;
}

int em_shadow_original_route_0015C160(uint8_t d8102B1, uint8_t d810771, uint32_t player_214,
                                      EmShadowOriginalFault *fault)
{
    if (fault && fault->code != EM_SHADOW_FAULT_NONE) return -1;
    if (d8102B1 == 0) return 0;
    if (d810771 == 1) return 0;
    if (player_214 != 0)
        return fail(fault, 0x0015BF90u, EM_SHADOW_FAULT_UNTRANSLATED);
    return 1;
}

/* ---- record views ----------------------------------------------------- */

static int node_qword(const uint8_t *const *nodes, uint32_t slots, uint32_t index,
                      uint32_t offset, float out[4], EmShadowOriginalFault *fault)
{
    if (!nodes || index >= slots || !nodes[index])
        return fail(fault, 0x1DA6A0u, EM_SHADOW_FAULT_BAD_INPUT);
    memcpy(out, nodes[index] + offset, 16);
    return 0;
}

/* ---- data ------------------------------------------------------------- */

/* D_0026E550 / D_0026E590 / D_0026E5D0: identity on u, v; z -> w lane
 * scaled by -2 plus 8388608 + 71 / 101 / 51 (float-mantissa integer). */
static void alpha_matrix(float m[16], uint32_t bias_bits)
{
    memset(m, 0, 64);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[11] = -2.0f;
    m[14] = 1.0f;
    m[15] = f32_of(bias_bits);
}

/* 001D98A0 area switch (key = D_00810700 << 8 | D_00810701). */
static const uint16_t AREA_101[] = {0x601, 0x1600, 0x800, 0x401, 0x202, 0x101, 0x100, 0x002, 0x000};
static const uint16_t AREA_71[] = {
    0x1500, 0x1400, 0x1301, 0x1300, 0x1200, 0x1100, 0x1001, 0x1000, 0xF01, 0xF00, 0xE00,
    0xD00, 0xC00, 0xB00, 0xA00, 0x900, 0x806, 0x805, 0x804, 0x803, 0x802, 0x801, 0x704,
    0x703, 0x702, 0x701, 0x700, 0x600, 0x500, 0x400, 0x302, 0x301, 0x300, 0x201, 0x200, 0x001};

static int in_list(const uint16_t *list, size_t n, uint32_t key)
{
    for (size_t i = 0; i < n; ++i)
        if (list[i] == key) return 1;
    return 0;
}

/* ---- 001D98A0 ------------------------------------------------------------ */

/* 00102718(dst, a, b): the VU0 outer-product pair — ACC = a.yzx * b.zxy,
 * then dst.xyz = ACC - b.yzx * a.zxy — and dst.w = dst.w - dst.w (0). */
static void cross(float out[4], const float a[4], const float b[4])
{
    float acc0 = mul(a[1], b[2]), acc1 = mul(a[2], b[0]), acc2 = mul(a[0], b[1]);
    float r[4];
    r[0] = sub(acc0, mul(b[1], a[2]));
    r[1] = sub(acc1, mul(b[2], a[0]));
    r[2] = sub(acc2, mul(b[0], a[1]));
    r[3] = 0.0f;
    memcpy(out, r, sizeof r);
}

static void light_setup(EmShadowOriginalPlan *plan, EmShadowOriginalState *state,
                        const EmShadowOriginalScene *scene)
{
    static const float up[4] = {0.0f, 1.0f, 0.0f, 0.0f}; /* D_002531C0 */
    float spC0[16], m[16], sp80[16], sp40[16];

    /* D_00817F70 = (0,-1,0,0) through the zero-angle rotations. */
    float light[4] = {0.0f, -1.0f, 0.0f, 0.0f};
    identity(spC0);
    vu0_rotate_zero(spC0);   /* 00102B08(spC0, spC0, 0.0) */
    vu0_rotate_zero(spC0);   /* 00102BB0(spC0, spC0, 0.0) */
    vu0_transform(light, spC0, light); /* 001026A0(F70, spC0, F70) */
    qcopy(plan->light_817F70, light);

    /* D_00817FC0 = up x (D_00817FF0 x up), from the PREVIOUS light. */
    float c[4];
    cross(c, state->d817FF0, up);
    cross(c, up, c);
    qcopy(plan->cross_817FC0, c);

    /* D_00817F80 = (0, 0 - L.z, L.y - 0, 0), normalized (EE FPU). */
    float r0 = 0.0f, r1 = sub(0.0f, light[2]), r2 = sub(light[1], 0.0f);
    float len2 = mul(r0, r0);
    len2 = add(len2, mul(r1, r1));          /* accumulator add */
    len2 = add(len2, mul(r2, r2));          /* multiply-add */
    /* 0011E748 (sqrt) only ever sees 1.0 here: L is the constant above. */
    float inv = divide(1.0f, (float)sqrt((double)len2));
    float right[4] = {mul(r0, inv), mul(r1, inv), mul(r2, inv), 0.0f};
    qcopy(plan->right_817F80, right);
    /* D_00817F90 = L x right through the FPU accumulator (product, then
     * multiply-subtract) */
    float upv[4];
    upv[0] = sub(mul(light[1], right[2]), mul(light[2], right[1]));
    upv[1] = sub(mul(light[2], right[0]), mul(light[0], right[2]));
    upv[2] = sub(mul(light[0], right[1]), mul(light[1], right[0]));
    upv[3] = 0.0f;
    qcopy(plan->up_817F90, upv);
    qcopy(state->d817FF0, light);            /* D_00817FF0 = D_00817F70 */

    /* sp80 = rows (F80, F90, F70, (0,0,0,1)), transposed in place by the
     * MMI word shuffle of 00102798 (bit moves only). */
    {
        const float src[16] = {right[0], right[1], right[2], right[3],
                               upv[0], upv[1], upv[2], upv[3],
                               light[0], light[1], light[2], light[3],
                               0.0f, 0.0f, 0.0f, 1.0f};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) sp80[j*4+i] = src[i*4+j];
    }

    /* D_00817F60 = -(anchor - 6 * L), w = 1 */
    float eye[4];
    for (int lane = 0; lane < 3; ++lane)
        eye[lane] = -sub(plan->anchor[lane], mul(light[lane], 6.0f));
    eye[3] = 1.0f;
    qcopy(plan->eye_817F60, eye);

    identity(sp40);
    memcpy(sp40 + 12, eye, 16);
    vu0_product(sp40, sp80, sp40);            /* 001026D0(sp40, sp80, sp40) */
    memcpy(plan->view_817F20, sp40, 64);      /* copy_qw4(D_00817F20, sp40) */

    identity(m);                              /* sp110 */
    float s = mul(f32_of(0x3C000000u), plan->unit);
    m[0] = s;
    m[5] = s;
    vu0_product(sp40, m, sp40);
    identity(m);                              /* sp150 */
    m[12] = 0.5f;
    m[13] = 0.5f;
    vu0_product(sp40, m, sp40);

    uint32_t key = ((uint32_t)scene->area_700 << 8) + scene->sub_701;
    plan->alpha_matrix = 0;
    if (plan->variant) {
        alpha_matrix(m, 0x4B000033u);         /* D_0026E5D0 */
        vu0_product(sp40, m, sp40);
        plan->alpha_matrix = 3;
    } else if (in_list(AREA_101, sizeof AREA_101 / sizeof AREA_101[0], key)) {
        alpha_matrix(m, 0x4B000065u);         /* D_0026E590 */
        vu0_product(sp40, m, sp40);
        plan->alpha_matrix = 2;
    } else if (in_list(AREA_71, sizeof AREA_71 / sizeof AREA_71[0], key)) {
        alpha_matrix(m, 0x4B000047u);         /* D_0026E550 */
        vu0_product(sp40, m, sp40);
        plan->alpha_matrix = 1;
    }
    memcpy(plan->uv_24B0, sp40, 64);          /* copy_qw4(ctx+0x24B0, sp40) */
}

/* 00102738: the VU0 dot product — xyz lanes multiplied, then x + y, then + z */
static float dot3(const float a[4], const float b[4])
{
    float x = mul(a[0], b[0]), y = mul(a[1], b[1]), z = mul(a[2], b[2]);
    return add(add(x, y), z);
}

/* ---- 001DA310 ------------------------------------------------------------ */

static void box_pass(EmShadowOriginalBox *box, int32_t model, const float anchor[4],
                     const float rgba[4], float size, const EmShadowOriginalScene *scene)
{
    float tpos[4], w[16], m[16], zero[16];
    qcopy(tpos, anchor);
    tpos[3] = 1.0f;                                   /* w = 1.0 in the stack copy */
    identity(w);
    identity(m);
    float f = mul(f32_of(0x3DCCCCCDu), size);         /* 0.1 * size */
    m[0] = f;
    m[5] = 4.0f;
    m[10] = f;
    vu0_product(w, m, w);                             /* sp50 = sp50 x sp90 */
    static const float drop[4] = {0.0f, -20.0f, 0.0f, 1.0f};
    vu0_translate(w, drop);                           /* 00102918 */
    vu0_rotate_zero(w);                               /* 00102C58: 00102A60 */
    vu0_rotate_zero(w);                               /*           00102BB0 */
    vu0_rotate_zero(w);                               /*           00102B08 */
    vu0_translate(w, tpos);                           /* 00102918 */
    box->model = model;
    memcpy(box->world, w, 64);
    vu0_product(box->camera, scene->camera_3AC0, w);  /* 001026D0(dmem0, 3AC0, sp50) */
    memset(zero, 0, sizeof zero);                     /* D_70003400..3F zeroed */
    vu0_product(box->normal, zero, w);
    for (int lane = 0; lane < 4; ++lane)              /* 001028B8(D_70003470, rgba, D_0026E610) */
        box->color_row[lane] = add(rgba[lane], 8388608.0f);
    /* 00128250 at 0x1DA5BC/5C8/5D8/5E8 (A, B, G, R); (A<<24)|(B<<16)|(G<<8)|R, no masking. */
    box->rgbaq = (f2u_00128250(rgba[3]) << 24) | (f2u_00128250(rgba[2]) << 16) |
                 (f2u_00128250(rgba[1]) << 8) | f2u_00128250(rgba[0]);
    float wv[16];
    vu0_product(wv, scene->view_2380, w);             /* sp130 = W x V */
    vu0_product(box->clip_pass, scene->proj_2340, wv);/* D_70003AC0 = sp130 x P */
}

/* ---- 001D5C80 ------------------------------------------------------------ */

static uint32_t clip_corners(const float m[16], const float lo[3], const float hi[3], float z)
{
    uint32_t reg = 0;
    const float corners[4][2] = {{lo[0], lo[1]}, {hi[0], lo[1]}, {lo[0], hi[1]}, {hi[0], hi[1]}};
    for (int k = 0; k < 4; ++k) {
        float v[4] = {corners[k][0], corners[k][1], z, 1.0f}, out[4];
        vu0_transform(out, m, v);
        reg = ((reg << 6) | clipw(out)) & 0xFFFFFFu;
    }
    return reg;
}

static int receivers(EmShadowOriginalPlan *plan, const EmShadowOriginalScene *scene,
                     const EmShadowOriginalWorkers *w, EmShadowOriginalFault *fault)
{
    const float *pos = plan->near_817FB0;
    float ox = scene->origin_x_158, oz = scene->origin_z_15C;
    float cx_ = scene->cell_x_150, cz_ = scene->cell_z_154;
    /* 001D5C80's cell math is EE COP1: the measured EE model applies. */
    int32_t cx = f2i(em_ee_add(1.0f, em_ee_div(em_ee_sub(pos[0], ox), cx_)));
    int32_t cz = f2i(em_ee_add(1.0f, em_ee_div(em_ee_sub(pos[2], oz), cz_)));
    int32_t x0 = f2i(em_ee_sub(em_ee_div(em_ee_sub(em_ee_sub(pos[0], ox), 15.0f), cx_), 0.5f));
    int32_t x1 = f2i(em_ee_add(0.5f, em_ee_div(em_ee_add(15.0f, em_ee_sub(pos[0], ox)), cx_)));
    int32_t z0 = f2i(em_ee_sub(em_ee_div(em_ee_sub(em_ee_sub(pos[2], oz), 15.0f), cz_), 0.5f));
    int32_t z1 = f2i(em_ee_add(0.5f, em_ee_div(em_ee_add(15.0f, em_ee_sub(pos[2], oz)), cz_)));
    if (!(x0 < cx - 1)) x0 = cx - 1;
    if (!(cx + 1 < x1)) x1 = cx + 1;
    if (!(z0 < cz - 1)) z0 = cz - 1;
    if (!(cz + 1 < z1)) z1 = cz + 1;
    if (x0 < 0) x0 = 0;
    if (!(x1 < 0x20)) x1 = 0x1F;
    if (z0 < 0) z0 = 0;
    if (!(z1 < 0x20)) z1 = 0x1F;
    plan->cell_cx = cx; plan->cell_cz = cz;
    plan->cell_x0 = x0; plan->cell_x1 = x1; plan->cell_z0 = z0; plan->cell_z1 = z1;

    if (!w->w_receiver_begin) return fail(fault, 0x1D4CD0u, EM_SHADOW_FAULT_NULL_WORKER);
    if (worker(fault, 0x1D4CD0u, w->w_receiver_begin(w->ctx, plan->uv_24B0))) return -1;

    float p[16];
    projection(p, scene->zoom_2468, 4096.0f, 4096.0f, f32_of(0x3DCCCCCDu), 16711680.0f);
    vu0_product(plan->guard_3440, p, scene->view_810610);
    projection(p, scene->zoom_2468, 1024.0f, 448.0f, f32_of(0x3DCCCCCDu), 16711680.0f);
    vu0_product(plan->screen_3400, p, scene->view_810610);

    for (int32_t iz = z0; iz <= z1; ++iz)
        for (int32_t ix = x0; ix <= x1; ++ix)
            for (int32_t slot = 0; slot < 4; ++slot) {
                int64_t index = ((int64_t)ix * scene->stride_148 + iz) * 4 + slot;
                if (!scene->grid_140 || index < 0 || index >= (int64_t)scene->grid_words)
                    return fail(fault, 0x1D5F84u, EM_SHADOW_FAULT_BAD_INPUT);
                int32_t id = scene->grid_140[index];
                if (id <= 0) continue;
                if (!w->w_object_bounds) return fail(fault, 0x1C6120u, EM_SHADOW_FAULT_NULL_WORKER);
                float lo[3], hi[3];
                if (worker(fault, 0x1C6120u, w->w_object_bounds(w->ctx, id, lo, hi))) return -1;
                uint32_t a = clip_corners(plan->screen_3400, lo, hi, lo[2]);
                uint32_t b = clip_corners(plan->screen_3400, lo, hi, hi[2]);
                uint32_t v = a & b;
                v = v & (v >> 6) & (v >> 12) & (v >> 18);
                v &= 0x3F;
                if (((v ^ (v << 1)) & 0x2A) != 0) continue;   /* trivially outside */
                EmShadowOriginalReceiver r = {id, 0, a, b};
                if ((a | b) & 0xFFFFFFu) {
                    uint32_t ga = clip_corners(plan->guard_3440, lo, hi, lo[2]);
                    uint32_t gb = clip_corners(plan->guard_3440, lo, hi, hi[2]);
                    r.cls = ((ga | gb) & 0xFFFFFFu) ? 2 : 1;
                }
                if (plan->receiver_count >= EM_SHADOW_RECEIVER_MAX)
                    return fail(fault, 0x1D5C80u, EM_SHADOW_FAULT_OVERFLOW);
                plan->receiver[plan->receiver_count++] = r;
                if (!w->w_receiver) return fail(fault, 0x1D4FB0u, EM_SHADOW_FAULT_NULL_WORKER);
                if (worker(fault, 0x1D4FB0u, w->w_receiver(w->ctx, &r))) return -1;
            }
    if (!w->w_receiver_end) return fail(fault, 0x1D1FF0u, EM_SHADOW_FAULT_NULL_WORKER);
    return worker(fault, 0x1D1FF0u, w->w_receiver_end(w->ctx));
}

/* ---- 001DA6A0 ------------------------------------------------------------ */

int em_shadow_original_001DA6A0(const uint8_t *actor, const uint8_t *const *nodes,
                                uint32_t node_slots, const EmShadowOriginalScene *scene,
                                EmShadowOriginalState *state, EmShadowOriginalPlan *plan,
                                const EmShadowOriginalWorkers *w,
                                EmShadowOriginalFault *fault)
{
    if (fault && fault->code != EM_SHADOW_FAULT_NONE) return -1;
    if (!actor || !scene || !state || !plan || !w)
        return fail(fault, EM_SHADOW_ORIGINAL_001DA6A0, EM_SHADOW_FAULT_NULL_WORKER);
    memset(plan, 0, sizeof *plan);
    plan->box_size = 16.0f;                           /* f20 = 0x41800000 */
    plan->unit = divide(128.0f, plan->box_size);      /* f21 = 128 / f20 */

    int16_t kind;
    memcpy(&kind, actor + 0x96, sizeof kind);
    plan->kind = kind;
    if (kind == 0) return 0;

    /* anchor (jtbl_0026E690) */
    if (kind == 0x28) {
        uint8_t sub_ = actor[0x98];
        if (sub_ == 0xFF) memcpy(plan->anchor, actor + 0xB0, 16);
        else if (node_qword(nodes, node_slots, sub_, 0xC0, plan->anchor, fault)) return -1;
        if (actor[0x23C] != 0) plan->variant = 1;
    } else if (kind == 0x2E || kind == 0x2F || kind == 0x30 || kind == 0x31 || kind == 0x34) {
        if (node_qword(nodes, node_slots, 2, 0xC0, plan->anchor, fault)) return -1;
    } else {
        if (node_qword(nodes, node_slots, 3, 0xC0, plan->anchor, fault)) return -1;
    }
    plan->anchor[3] = 1.0f;

    /* probes (jtbl_0026E650: only kind 0x2E uses D_0026E640) and the clip */
    qcopy(plan->probe[0], plan->anchor);
    const float drop40[4] = {0.0f, -40.0f, 0.0f, 0.0f};        /* D_0026E620 */
    const float third[4] = {0.0f, kind == 0x2E ? -5.0f : 0.0f, 0.0f, 0.0f};
    for (int lane = 0; lane < 4; ++lane) {
        plan->probe[1][lane] = add(plan->anchor[lane], drop40[lane]);
        plan->probe[2][lane] = add(plan->anchor[lane], third[lane]);
    }
    uint32_t common = 0x3F;
    for (int i = 0; i < 3; ++i) {
        float v[4] = {plan->probe[i][0], plan->probe[i][1], plan->probe[i][2], 1.0f}, out[4];
        vu0_transform(out, scene->clip_2240, v);
        plan->probe_clip[i] = clipw(out);
        common &= plan->probe_clip[i];
    }
    if (common != 0) return 0;

    /* node spread: the shipped loop keeps the MINIMUM in all four
     * accumulators (both compare-and-branch arms select x when x <= acc). */
    float first[4];
    if (node_qword(nodes, node_slots, 1, 0xC0, first, fault)) return -1;
    float minx = first[0], minz = first[2];
    float ax = minx, bx = minx, az = minz, bz = minz;
    for (uint32_t i = 2; i < actor[0x09]; ++i) {
        float q[4];
        if (node_qword(nodes, node_slots, i, 0xC0, q, fault)) return -1;
        float x = q[0], z = q[2];
        if (!(ax < x)) ax = x;
        if (!(bx < x)) bx = x;
        if (!(az < z)) az = z;
        if (!(bz < z)) bz = z;
    }
    float dx = sub(bx, ax), dz = sub(bz, az);
    float d2 = add(mul(dx, dx), mul(dz, dz));      /* through the FPU accumulator */
    plan->spread = (float)sqrt((double)d2);        /* 0011E748: d2 is 0 */
    if (!(plan->spread <= 7.0f)) plan->box_size = add(1.0f, mul(2.0f, plan->spread));

    light_setup(plan, state, scene);

    /* 001DA080(D_00817FB0, D_00817FA0, actor, D_00817FF0) */
    {
        float n[4], best_a, best_b;
        if (node_qword(nodes, node_slots, 1, 0xC0, n, fault)) return -1;
        best_a = dot3(state->d817FF0, n);
        best_b = dot3(plan->cross_817FC0, n);
        plan->near_index = 1;
        plan->far_index = 1;
        for (uint32_t i = 2; i < actor[0x09]; ++i) {
            if (node_qword(nodes, node_slots, i, 0xC0, n, fault)) return -1;
            float va = dot3(state->d817FF0, n);
            if (va < best_a) { best_a = va; plan->near_index = (int32_t)i; }
            float vb = dot3(plan->cross_817FC0, n);
            if (!(vb <= best_b)) { best_b = vb; plan->far_index = (int32_t)i; }
        }
        if (node_qword(nodes, node_slots, (uint32_t)plan->near_index, 0xC0, plan->near_817FB0, fault) ||
            node_qword(nodes, node_slots, (uint32_t)plan->far_index, 0xC0, plan->far_817FA0, fault))
            return -1;
    }

    /* 001DA290 + two 001DA310 box passes (D_00253220 then D_00253210). */
    static const float colB[4] = {0.0f, 128.0f, 0.0f, 128.0f};
    static const float colA[4] = {128.0f, 0.0f, 0.0f, 1.0f};
    box_pass(&plan->box[0], EM_SHADOW_BOX_MODEL_FRONT, plan->anchor, colB, plan->box_size, scene);
    box_pass(&plan->box[1], EM_SHADOW_BOX_MODEL_BACK, plan->anchor, colA, plan->box_size, scene);

    /* 001D9EE0 silhouette view-projection: D_00817F20 x diag(unit,unit,1,1)
     * x the ortho/GS row set (GS centre 2048, the main Z row). */
    {
        float m[16];
        memcpy(plan->silhouette_vp, plan->view_817F20, 64);
        identity(m);
        m[0] = plan->unit;
        m[5] = plan->unit;
        vu0_product(plan->silhouette_vp, m, plan->silhouette_vp);
        identity(m);
        m[10] = f32_of(0x3F664CB3u);
        m[12] = 2048.0f;
        m[13] = 2048.0f;
        m[14] = f32_of(0x49CCCCCCu);
        vu0_product(plan->silhouette_vp, m, plan->silhouette_vp);
    }

    plan->drawn = 1;
    if (!w->w_alpha_clear) return fail(fault, 0x1DA290u, EM_SHADOW_FAULT_NULL_WORKER);
    if (worker(fault, 0x1DA290u, w->w_alpha_clear(w->ctx))) return -1;
    if (!w->w_box) return fail(fault, 0x1DA310u, EM_SHADOW_FAULT_NULL_WORKER);
    if (worker(fault, 0x1DA310u, w->w_box(w->ctx, &plan->box[0]))) return -1;
    if (worker(fault, 0x1DA310u, w->w_box(w->ctx, &plan->box[1]))) return -1;
    if (!w->w_silhouette) return fail(fault, 0x1D9EE0u, EM_SHADOW_FAULT_NULL_WORKER);
    if (worker(fault, 0x1D9EE0u, w->w_silhouette(w->ctx, kind, plan->silhouette_vp))) return -1;
    if (receivers(plan, scene, w, fault)) return -1;
    return 1;
}

void em_shadow_original_receiver_vertex(const float uv[16], const float p[3],
                                        float out[4], uint32_t *alpha)
{
    float v[4] = {p[0], p[1], p[2], 1.0f};  /* w = 1 (the VU constant register's w) */
    vu0_transform(out, uv, v);
    float a = out[3];
    if (a > 8388863.0f) a = 8388863.0f;      /* w lane: VU minimum against the x constant */
    if (a < 8388608.0f) a = 8388608.0f;      /* w lane: VU maximum against the y constant */
    if (alpha) *alpha = (uint32_t)(a - 8388608.0f);
}

/* ---- the receivers' original data (asset) ------------------------------ */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float rdf(const uint8_t *p)
{
    const uint32_t b = rd32(p);
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}

void em_shadow_receivers_free(EmShadowReceivers *r)
{
    if (!r) return;
    if (r->object)
        for (uint32_t i = 0; i < r->slots; ++i) free((void *)r->object[i].qw3);
    free((void *)r->box[0].qw3);
    free((void *)r->box[1].qw3);
    free(r->object);
    free(r->blob);
    memset(r, 0, sizeof *r);
}

/* One record: id, batches, AABB, batches x 128 qwords. */
static int receiver_record(const uint8_t *data, size_t size, size_t *at,
                           EmShadowReceiverObject *o)
{
    if (size - *at < 32u) return -1;
    const uint8_t *h = data + *at;
    o->id = (int32_t)rd32(h);
    o->batches = rd32(h + 4);
    for (unsigned k = 0; k < 3; ++k) {
        o->bmin[k] = rdf(h + 8 + 4 * k);
        o->bmax[k] = rdf(h + 20 + 4 * k);
    }
    *at += 32u;
    if (o->batches == 0u || o->batches > (size - *at) / 2048u) return -1;
    o->qwords = (const uint32_t *)(const void *)(data + *at);
    float *q3 = malloc(sizeof(float) * 128u * o->batches);
    if (!q3) return -1;
    for (uint32_t v = 0; v < 32u * o->batches; ++v)
        for (unsigned k = 0; k < 4; ++k) q3[4 * v + k] = rdf(data + *at + 64u * v + 48u + 4u * k);
    o->qw3 = q3;
    *at += 2048u * o->batches;
    return 0;
}

int em_shadow_receivers_load(EmShadowReceivers *r, const char *path)
{
    if (!r) return -1;
    memset(r, 0, sizeof *r);
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return -1;
    uint8_t *data = NULL;
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (size >= 0x38 && fseek(f, 0, SEEK_SET) == 0) {
        data = malloc((size_t)size);
        if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
            free(data);
            data = NULL;
        }
    }
    fclose(f);
    if (!data) return -1;
    r->blob = data;
    const size_t n = (size_t)size;
    const uint32_t words = rd32(data + 0x28), slots = rd32(data + 0x2C), boxes = rd32(data + 0x30);
    if (memcmp(data, "EMSR", 4) != 0 || rd32(data + 4) != 1u || boxes != 2u || slots < 2u ||
        slots > 0x8000u || words > (n - 0x38u) / 4u) {
        em_shadow_receivers_free(r);
        return -1;
    }
    r->rows_144 = (int32_t)rd32(data + 8);
    r->stride_148 = (int32_t)rd32(data + 12);
    for (unsigned k = 0; k < 6; ++k) r->f150[k] = rdf(data + 16 + 4 * k);
    if (r->rows_144 <= 0 || r->stride_148 <= 0 ||
        (uint64_t)r->rows_144 * (uint64_t)r->stride_148 * 4u != words) {
        em_shadow_receivers_free(r);
        return -1;
    }
    r->grid = (const int32_t *)(const void *)(data + 0x38);
    r->grid_words = words;
    r->slots = slots;
    r->object = calloc(slots, sizeof *r->object);
    if (!r->object) { em_shadow_receivers_free(r); return -1; }
    size_t at = 0x38u + 4u * words;
    for (uint32_t id = 1; id < slots; ++id) {
        if (receiver_record(data, n, &at, &r->object[id]) || r->object[id].id != (int32_t)id) {
            em_shadow_receivers_free(r);
            return -1;
        }
    }
    for (unsigned b = 0; b < 2; ++b) {
        if (receiver_record(data, n, &at, &r->box[b]) ||
            r->box[b].id != (b ? EM_SHADOW_BOX_MODEL_BACK : EM_SHADOW_BOX_MODEL_FRONT)) {
            em_shadow_receivers_free(r);
            return -1;
        }
    }
    if (at != n) { em_shadow_receivers_free(r); return -1; }
    return 0;
}

void em_shadow_receivers_scene(const EmShadowReceivers *r, EmShadowOriginalScene *scene)
{
    if (!r || !scene) return;
    scene->grid_140 = r->grid;
    scene->grid_words = r->grid_words;
    scene->stride_148 = r->stride_148;
    scene->cell_x_150 = r->f150[0];
    scene->cell_z_154 = r->f150[1];
    scene->origin_x_158 = r->f150[2];
    scene->origin_z_15C = r->f150[3];
}

const EmShadowReceiverObject *em_shadow_receivers_object(const EmShadowReceivers *r,
                                                         int32_t id)
{
    const uint32_t a1 = (uint32_t)id & 0xFFFFu & 0xFFFF7FFFu;   /* 001C6120 */
    if (!r || !r->object || a1 == 0u || a1 >= r->slots) return NULL;
    return &r->object[a1];
}

int em_shadow_receivers_bounds(void *ctx, int32_t id, float bmin[3], float bmax[3])
{
    const EmShadowReceiverObject *o = em_shadow_receivers_object(ctx, id);
    if (!o) return -1;
    memcpy(bmin, o->bmin, sizeof o->bmin);
    memcpy(bmax, o->bmax, sizeof o->bmax);
    return 0;
}

const EmShadowReceiverObject *em_shadow_receivers_box(const EmShadowReceivers *r,
                                                      int32_t model)
{
    if (!r) return NULL;
    if (model == EM_SHADOW_BOX_MODEL_FRONT && r->box[0].qw3) return &r->box[0];
    if (model == EM_SHADOW_BOX_MODEL_BACK && r->box[1].qw3) return &r->box[1];
    return NULL;
}
