/* Effect spawn chain, generic puff driver and ring decals (see
 * em_effect_original.h and docs/EFFECT_ORIGINAL.md). Addresses in comments
 * are original runtime addresses; float constants are the original bit
 * patterns. Float arithmetic follows docs/EE_FLOAT_MODEL.md exactly; each VU0
 * lane operation uses one of the operand-clamp forms F_* below, which mirror
 * the model's VU_FORMS table. Comments describe what the original computes;
 * they never reproduce its instruction stream. */
#include "game/em_effect_original.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Float model (tools/ee_float_model.py, docs/EE_FLOAT_MODEL.md).       */
/* ------------------------------------------------------------------ */

#define FP_SIGN 0x80000000u
#define FP_MAX 0x7F7FFFFFu
#define FP_INF 0x7F800000u
#define FP_INDEF 0xFFC00000u
#define FP_ONE 0x3F800000u

static uint32_t fbits(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }
static float bfloat(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }
static unsigned fexp(uint32_t b) { return b >> 23 & 0xFFu; }
static int fnan(uint32_t b) { return fexp(b) == 0xFF && (b & 0x7FFFFFu); }
static int finf(uint32_t b) { return (b & 0x7FFFFFFFu) == FP_INF; }
static int fzero(uint32_t b) { return fexp(b) == 0; }
static uint32_t daz(uint32_t b) { return fexp(b) ? b : b & FP_SIGN; }
static uint32_t sat(uint32_t b)
{
    if (fnan(b)) return FP_MAX;
    if (finf(b)) return (b & FP_SIGN) | FP_MAX;
    return b;
}
static uint32_t sat_signed(uint32_t b) { return fexp(b) == 0xFF ? (b & FP_SIGN) | FP_MAX : b; }
static int bitlen(uint64_t v) { int n = 0; while (v) { n++; v >>= 1; } return n; }

/* Finite DAZ-applied operand -> sign, significand, exponent (value = m * 2^e). */
static void fp_unpack(uint32_t b, uint32_t *s, uint64_t *m, int *e)
{
    *s = b >> 31;
    if (!fexp(b)) { *m = 0; *e = 0; return; }
    *m = (b & 0x7FFFFFu) | 0x800000u;
    *e = (int)fexp(b) - 150;
}

/* sign * mag * 2^exp to binary32: truncation or nearest-even, FTZ, saturation. */
static uint32_t fp_pack(uint32_t sign, uint64_t mag, int exp, int nearest, int inexact_below)
{
    int shift = bitlen(mag) - 24;
    uint64_t kept;
    if (shift > 0) {
        kept = mag >> shift;
        uint64_t rest = mag & ((UINT64_C(1) << shift) - 1);
        if (nearest) {
            uint64_t half = UINT64_C(1) << (shift - 1);
            if (rest > half || (rest == half && (inexact_below || (kept & 1))))
                kept++;
        }
    } else {
        kept = mag << -shift;
    }
    exp += shift;
    if (kept >> 24) { kept >>= 1; exp++; }
    int biased = exp + 150;
    if (biased <= 0) return sign << 31;
    if (biased >= 0xFF) return sign << 31 | FP_MAX;
    return sign << 31 | (uint32_t)biased << 23 | (uint32_t)(kept & 0x7FFFFFu);
}

/* Exact finite a + b truncated. Beyond 30 bits of exponent distance the
 * smaller operand becomes a sticky bit, which keeps the truncation exact. */
static uint32_t exact_sum(uint32_t a, uint32_t b)
{
    uint32_t sa, sb;
    uint64_t ma, mb;
    int ea, eb;
    fp_unpack(a, &sa, &ma, &ea);
    fp_unpack(b, &sb, &mb, &eb);
    if (!ma && !mb) return (sa && sb) ? FP_SIGN : 0;
    if (!ma) return b;
    if (!mb) return a;
    if (eb > ea) {
        uint32_t ts = sa; sa = sb; sb = ts;
        uint64_t tm = ma; ma = mb; mb = tm;
        int te = ea; ea = eb; eb = te;
    }
    int d = ea - eb;
    uint64_t big = ma << 30, small;
    if (d <= 30) {
        small = mb << (30 - d);
    } else {
        int sh = d - 30;
        small = sh >= 25 ? 1 : (mb >> sh) | ((mb & ((UINT64_C(1) << sh) - 1)) != 0);
    }
    uint64_t mag;
    uint32_t sign;
    if (sa == sb) { mag = big + small; sign = sa; }
    else if (big >= small) { mag = big - small; sign = sa; }
    else { mag = small - big; sign = sb; }
    if (!mag) return 0;
    return fp_pack(sign, mag, ea - 30, 0, 0);
}

static uint32_t exact_product(uint32_t a, uint32_t b)
{
    uint32_t sa, sb;
    uint64_t ma, mb;
    int ea, eb;
    fp_unpack(a, &sa, &ma, &ea);
    fp_unpack(b, &sb, &mb, &eb);
    if (!ma || !mb) return (sa ^ sb) << 31;
    return fp_pack(sa ^ sb, ma * mb, ea + eb, 0, 0);
}

static uint32_t quotient(uint32_t a, uint32_t b, int nearest)
{
    uint32_t sa, sb;
    uint64_t ma, mb;
    int ea, eb;
    fp_unpack(a, &sa, &ma, &ea);
    fp_unpack(b, &sb, &mb, &eb);
    if (!ma) return (sa ^ sb) << 31;
    uint64_t n = ma << 39;
    return fp_pack(sa ^ sb, n / mb, ea - eb - 39, nearest, n % mb != 0);
}

/* ---- EE COP1 ---- */
static uint32_t trim(uint32_t b, int d) { return d >= 25 ? b & FP_SIGN : b & (0xFFFFFFFFu << (d - 1)); }

static uint32_t ee_sum(uint32_t a, uint32_t b)
{
    if (fexp(a) == 0xFF || fexp(b) == 0xFF) {
        if (fnan(a) || fnan(b)) return FP_MAX;
        if (finf(a) && finf(b)) return ((a ^ b) & FP_SIGN) ? FP_MAX : (a & FP_SIGN) | FP_MAX;
        return ((finf(a) ? a : b) & FP_SIGN) | FP_MAX;
    }
    int d = (int)fexp(a) - (int)fexp(b);
    if (d > 0) b = trim(b, d);
    else if (d < 0) a = trim(a, -d);
    return exact_sum(a, b);
}
static uint32_t ee_add(uint32_t a, uint32_t b) { return ee_sum(daz(a), daz(b)); }
static uint32_t ee_sub(uint32_t a, uint32_t b) { return ee_sum(daz(a), daz(b) ^ FP_SIGN); }
static uint32_t ee_mul(uint32_t a, uint32_t b)
{
    a = daz(a); b = daz(b);
    if (fnan(a) || fnan(b)) return FP_MAX; /* the QNAN product, saturated */
    if (finf(a) || finf(b)) {
        if (fzero(a) || fzero(b)) return FP_MAX;
        return ((a ^ b) & FP_SIGN) | FP_MAX;
    }
    return exact_product(a, b);
}
static uint32_t ee_div(uint32_t a, uint32_t b)
{
    a = daz(a); b = daz(b);
    uint32_t sign = (a ^ b) >> 31;
    if (fzero(b)) return sign << 31 | FP_MAX;
    if (fexp(a) == 0xFF || fexp(b) == 0xFF) {
        if (fnan(a) || fnan(b) || (finf(a) && finf(b))) return FP_MAX;
        return finf(a) ? (sign << 31 | FP_MAX) : sign << 31;
    }
    return quotient(a, b, 1);
}
static uint32_t ee_cvt_w_s(uint32_t a)
{
    if (fexp(a) >= 158) return (a & FP_SIGN) ? 0x80000000u : 0x7FFFFFFFu;
    uint32_t s;
    uint64_t m;
    int x;
    fp_unpack(a, &s, &m, &x);
    uint64_t v = x >= 0 ? m << x : (-x >= 64 ? 0 : m >> -x);
    return (uint32_t)(s ? 0u - (uint32_t)v : (uint32_t)v);
}
static uint32_t ee_cvt_s_w(uint32_t i)
{
    if (!i) return 0;
    uint32_t sign = i >> 31;
    uint64_t mag = sign ? (UINT64_C(1) << 32) - i : i;
    return fp_pack(sign, mag, 0, 0, 0);
}
static int64_t ckey(uint32_t b)
{
    b = sat_signed(daz(b));
    return (b & FP_SIGN) ? -(int64_t)(b & 0x7FFFFFFFu) : (int64_t)b;
}
static int ee_c_eq(uint32_t a, uint32_t b) { return ckey(a) == ckey(b); }
static int ee_c_lt(uint32_t a, uint32_t b) { return ckey(a) < ckey(b); }
static int ee_c_le(uint32_t a, uint32_t b) { return ckey(a) <= ckey(b); }

/* ---- VU0 macro lanes ---- */
static uint32_t quiet(uint32_t b) { return b | 0x00400000u; }
static uint32_t vu_add_raw(uint32_t x, uint32_t y)
{
    x = daz(x); y = daz(y);
    if (fnan(x)) return quiet(x);
    if (fnan(y)) return quiet(y);
    if (finf(x) && finf(y)) return ((x ^ y) & FP_SIGN) ? FP_INDEF : x;
    if (finf(x)) return x;
    if (finf(y)) return y;
    return exact_sum(x, y);
}
static uint32_t vu_sub_raw(uint32_t x, uint32_t y)
{
    x = daz(x); y = daz(y);
    if (fnan(x)) return quiet(x);
    if (fnan(y)) return quiet(y);
    return vu_add_raw(x, y ^ FP_SIGN);
}
static uint32_t vu_mul_raw(uint32_t x, uint32_t y)
{
    x = daz(x); y = daz(y);
    if (fnan(x)) return quiet(x);
    if (fnan(y)) return quiet(y);
    if (finf(x) || finf(y)) {
        if (fzero(x) || fzero(y)) return FP_INDEF;
        return ((x ^ y) & FP_SIGN) | FP_INF;
    }
    return exact_product(x, y);
}

/* The operand clamps of one original instruction form (VU_FORMS value). */
typedef struct { uint8_t cs, ct, ca, order; } Form;
/* No clamp: every add form, whole-vector subtract with mask xyz, broadcast
 * subtracts, the outer-product pair. */
static const Form F_NONE = {0, 0, 0, 0};
/* First operand clamped: whole-vector multiply with mask x or xyz, broadcast
 * multiply with masks 7..14, multiply by Q with mask xyz, and the ACC-only
 * broadcast multiply / multiply-add. */
static const Form F_S = {1, 0, 0, 0};
/* Both operands clamped: subtract with mask w/zw/xyzw, broadcast multiply
 * with mask xyzw by lane x or w. */
static const Form F_ST = {1, 1, 0, 0};
/* Both operands and ACC clamped: broadcast multiply-add by lane w, mask w or
 * xyzw. */
static const Form F_STA = {1, 1, 1, 0};
enum { K_ADD, K_SUB, K_MUL, K_MADD, K_OPMSUB };

static uint32_t vu_lane(int kind, Form f, uint32_t s, uint32_t t, uint32_t acc)
{
    if (f.cs) s = sat(s);
    if (f.ct) t = sat(t);
    if (kind == K_ADD) return vu_add_raw(s, t);
    if (kind == K_SUB) return vu_sub_raw(s, t);
    if (kind == K_MUL) return vu_mul_raw(s, t);
    uint32_t product = vu_mul_raw(s, t);
    if (f.ca) acc = sat(acc);
    if (kind == K_MADD) return f.order ? vu_add_raw(product, acc) : vu_add_raw(acc, product);
    return vu_sub_raw(acc, product);
}
static uint32_t vadd(uint32_t s, uint32_t t) { return vu_lane(K_ADD, F_NONE, s, t, 0); }
static uint32_t vsub(uint32_t s, uint32_t t) { return vu_lane(K_SUB, F_NONE, s, t, 0); }

/* VU0 divide; recip = the forms whose dividend is the constant-1.0 lane, where
 * a NaN divisor passes through. */
static uint32_t vu_div(uint32_t a, uint32_t b, int recip)
{
    a = daz(a); b = daz(b);
    uint32_t sign = (a ^ b) >> 31;
    if (fzero(b)) return sign << 31 | FP_MAX;
    if (recip && fnan(b) && !fnan(a)) return quiet(b);
    if (fexp(a) == 0xFF || fexp(b) == 0xFF) {
        if (fnan(a) || fnan(b) || (finf(a) && finf(b))) return FP_MAX;
        return finf(a) ? (sign << 31 | FP_MAX) : sign << 31;
    }
    return quotient(a, b, 0);
}
static uint64_t isqrt64(uint64_t n)
{
    uint64_t r = 0, bit = UINT64_C(1) << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}
static uint32_t vu_sqrt(uint32_t b)
{
    b = sat_signed(daz(b)) & 0x7FFFFFFFu;
    uint32_t s;
    uint64_t m;
    int e;
    fp_unpack(b, &s, &m, &e);
    if (!m) return 0;
    if (e & 1) { m <<= 1; e -= 1; }
    return fp_pack(0, isqrt64(m << 38), (e - 38) / 2, 0, 0);
}
static uint32_t vu_ftoi(uint32_t a, int frac)
{
    if (fexp(a) == 0) return 0;
    if ((int)fexp(a) + frac >= 158) return (a & FP_SIGN) ? 0x80000000u : 0x7FFFFFFFu;
    uint32_t s;
    uint64_t m;
    int x;
    fp_unpack(a, &s, &m, &x);
    x += frac;
    uint64_t v = x >= 0 ? m << x : (-x >= 64 ? 0 : m >> -x);
    return (uint32_t)(s ? 0u - (uint32_t)v : (uint32_t)v);
}
static int64_t okey(uint32_t b) { return (b & FP_SIGN) ? -(int64_t)(b & 0x7FFFFFFFu) - 1 : (int64_t)b; }
static uint32_t vu_max(uint32_t a, uint32_t b) { return okey(a) >= okey(b) ? a : b; }
static uint32_t vu_min(uint32_t a, uint32_t b) { return okey(a) <= okey(b) ? a : b; }

static int32_t float_to_int_bits(uint32_t b);
static int wrap_001B1470(uint32_t f, uint32_t *out);

uint32_t em_effect_original_fp(int op, unsigned flags, uint32_t a, uint32_t b, uint32_t acc)
{
    Form f = {(uint8_t)(flags & 1), (uint8_t)(flags >> 1 & 1), (uint8_t)(flags >> 2 & 1),
              (uint8_t)(flags >> 3 & 1)};
    switch (op) {
    case EM_EFFECT_FP_EE_ADD: return ee_add(a, b);
    case EM_EFFECT_FP_EE_SUB: return ee_sub(a, b);
    case EM_EFFECT_FP_EE_MUL: return ee_mul(a, b);
    case EM_EFFECT_FP_EE_DIV: return ee_div(a, b);
    case EM_EFFECT_FP_EE_CVT_W_S: return ee_cvt_w_s(a);
    case EM_EFFECT_FP_EE_CVT_S_W: return ee_cvt_s_w(a);
    case EM_EFFECT_FP_EE_C_EQ: return (uint32_t)ee_c_eq(a, b);
    case EM_EFFECT_FP_EE_C_LT: return (uint32_t)ee_c_lt(a, b);
    case EM_EFFECT_FP_EE_C_LE: return (uint32_t)ee_c_le(a, b);
    case EM_EFFECT_FP_VU_ADD: return vu_lane(K_ADD, f, a, b, acc);
    case EM_EFFECT_FP_VU_SUB: return vu_lane(K_SUB, f, a, b, acc);
    case EM_EFFECT_FP_VU_MUL: return vu_lane(K_MUL, f, a, b, acc);
    case EM_EFFECT_FP_VU_MADD: return vu_lane(K_MADD, f, a, b, acc);
    case EM_EFFECT_FP_VU_OPMSUB: return vu_lane(K_OPMSUB, f, a, b, acc);
    case EM_EFFECT_FP_VU_DIV: return vu_div(a, b, (int)(flags & 1));
    case EM_EFFECT_FP_VU_SQRT: return vu_sqrt(b);
    case EM_EFFECT_FP_VU_FTOI4: return vu_ftoi(a, 4);
    case EM_EFFECT_FP_VU_MIN: return vu_min(a, b);
    case EM_EFFECT_FP_VU_MAX: return vu_max(a, b);
    case EM_EFFECT_FP_FLOAT_TO_INT: return (uint32_t)float_to_int_bits(a);
    case EM_EFFECT_FP_WRAP: {
        uint32_t r;
        return wrap_001B1470(a, &r) < 0 ? 0xFFFFFFFFu : r;
    }
    default: return 0;
    }
}

/* ------------------------------------------------------------------ */
/* SDK leaves on raw words (quadword = 4 lanes, matrix = 4 rows).       */
/* ------------------------------------------------------------------ */

typedef uint32_t Quad[4];

/* 001029C0: row 3 = (0,0,0,1) built as 0 - 0 plus a 1.0 w lane; rows 2, 1, 0
 * are each the next row rotated by one lane. The result is always the
 * identity (+0 off the diagonal). */
static void sdk_identity(Quad m[4])
{
    memset(m, 0, 4 * sizeof *m);
    m[0][0] = m[1][1] = m[2][2] = m[3][3] = FP_ONE;
}

/* 001026A0 body: ACC = r0 * v.x (F_S); ACC += r1 * v.y, then r2 * v.z (F_S);
 * out = ACC + r3 * v.w (F_STA). The same four-step multiply-accumulate chain
 * is in the 00102A60/00102BB0/00102B08 row loop and in 001CCF70. */
static void sdk_apply(Quad out, const Quad r[4], const Quad v)
{
    Quad res;
    for (int i = 0; i < 4; ++i) {
        uint32_t acc = vu_lane(K_MUL, F_S, r[0][i], v[0], 0);
        acc = vu_lane(K_MADD, F_S, r[1][i], v[1], acc);
        acc = vu_lane(K_MADD, F_S, r[2][i], v[2], acc);
        res[i] = vu_lane(K_MADD, F_STA, r[3][i], v[3], acc);
    }
    memcpy(out, res, sizeof res);
}

/* 00102918(out, in, v): rows 0..2 copied as quadwords; row 3 xyz = in row 3
 * + v (F_NONE); row 3 w kept. */
static void sdk_translate(Quad out[4], const Quad in[4], const Quad v)
{
    Quad r3;
    memcpy(r3, in[3], sizeof r3);
    for (int i = 0; i < 3; ++i) r3[i] = vadd(r3[i], v[i]);
    if (out != in) memcpy(out, in, 3 * sizeof *out);
    memcpy(out[3], r3, sizeof r3);
}

/* 00102760(out, in): squares of x, y, z (F_S); sum = (x2 + y2) + z2; len =
 * 0 + sqrt(sum); Q = 1.0 / len (reciprocal divide); out = (in.xyz * Q (F_S),
 * w = +0). */
static void sdk_normalize(Quad out, const Quad in)
{
    uint32_t sq[3];
    for (int i = 0; i < 3; ++i) sq[i] = vu_lane(K_MUL, F_S, in[i], in[i], 0);
    uint32_t sum = vadd(vadd(sq[0], sq[1]), sq[2]);
    uint32_t len = vadd(0, vu_sqrt(sum));
    uint32_t q = vu_div(FP_ONE, len, 1);
    Quad r = {0, 0, 0, 0};
    for (int i = 0; i < 3; ++i) r[i] = vu_lane(K_MUL, F_S, in[i], q, 0);
    r[3] = vu_lane(K_SUB, F_ST, FP_ONE, FP_ONE, 0); /* w = 1.0 - 1.0, both clamped */
    memcpy(out, r, sizeof r);
}

/* 00102718(out, a, b): outer product, no clamps: ACC = a.yzx * b.zxy; out.xyz
 * = ACC - b.yzx * a.zxy; out.w = out.w - out.w, i.e. +0 whatever the
 * destination register held. */
static void sdk_cross(Quad out, const Quad a, const Quad b)
{
    static const int S[3] = {1, 2, 0}, T[3] = {2, 0, 1};
    Quad r;
    for (int k = 0; k < 3; ++k) {
        uint32_t acc = vu_lane(K_MUL, F_NONE, a[S[k]], b[T[k]], 0);
        r[k] = vu_lane(K_OPMSUB, F_NONE, b[S[k]], a[T[k]], acc);
    }
    r[3] = 0;
    memcpy(out, r, sizeof r);
}

/* 001029E8 (arg in the x lane, neg flag as the fourth argument): returns
 * (x, y) = (+/-c, s). D_00241100 holds the four polynomial coefficients (ELF
 * .data). The odd series: t[i] = C[i] * arg, every lane * a2, then lanes xyz,
 * xy and x get one more * a2 each, so lane i carries a2^(4-i); s = arg + t.w +
 * t.z + t.y + t.x in that order. c = sqrt(1 - s*s). */
static void sdk_sincos(uint32_t arg, int neg, uint32_t *x, uint32_t *y)
{
    static const uint32_t C[4] = {0x362E9C14u, 0xB94FB21Fu, 0x3C08873Eu, 0xBE2AAAA4u};
    uint32_t v4x = vadd(0, arg);                               /* s = 0 + arg */
    uint32_t a2 = vu_lane(K_MUL, F_S, arg, arg, 0);            /* a2 = arg * arg */
    uint32_t v8[4];
    for (int i = 0; i < 4; ++i) v8[i] = vu_lane(K_MUL, F_ST, C[i], arg, 0); /* C * arg */
    for (int i = 0; i < 4; ++i) v8[i] = vu_lane(K_MUL, F_ST, v8[i], a2, 0); /* all lanes * a2 */
    for (int i = 0; i < 3; ++i) v8[i] = vu_lane(K_MUL, F_S, v8[i], a2, 0);  /* xyz * a2 */
    v4x = vadd(v4x, v8[3]);                                    /* s += t.w */
    for (int i = 0; i < 2; ++i) v8[i] = vu_lane(K_MUL, F_S, v8[i], a2, 0);  /* xy * a2 */
    v4x = vadd(v4x, v8[2]);                                    /* s += t.z */
    v8[0] = vu_lane(K_MUL, F_S, v8[0], a2, 0);                 /* x * a2 */
    v4x = vadd(v4x, v8[1]);                                    /* s += t.y */
    v4x = vadd(v4x, v8[0]);                                    /* s += t.x */
    uint32_t s = vadd(0, v4x);                                 /* s copied to x and y (0 + s) */
    uint32_t w = vsub(FP_ONE, vu_lane(K_MUL, F_S, s, s, 0));   /* 1.0 - s*s */
    uint32_t c = vadd(0, vu_sqrt(w));                          /* c = 0 + sqrt(1 - s*s) */
    *x = neg ? vsub(0, c) : vadd(0, c);                        /* x = 0 - c (neg) or 0 + c */
    *y = s;
}

/* 00102B08 (axis 0, X), 00102BB0 (1, Y), 00102A60 (2, Z): angle < 0 ->
 * sincos(pi/2 + angle, 1), else sincos(pi/2 - angle, 0); build the four
 * rotation rows; then out[i] = rows applied to in[i] for the four rows. */
static void sdk_rotate(Quad out[4], const Quad in[4], uint32_t angle, int axis)
{
    const uint32_t half_pi = 0x3FC90FDBu;
    int neg = ee_c_lt(angle, 0);
    uint32_t arg = neg ? ee_add(half_pi, angle) : ee_sub(half_pi, angle);
    uint32_t S, C;
    sdk_sincos(arg, neg, &S, &C);
    Quad r[4];
    memset(r, 0, sizeof r); /* every row starts as a copy of a zero vector */
    /* Each non-zero entry below is written as 0 + value or 0 - value
     * (unclamped), one lane at a time. */
    if (axis == 0) {        /* 00102B38..: rows (1,0,0,0) (0,C,S,0) (0,-S,C,0) (0,0,0,1) */
        r[0][0] = vadd(0, FP_ONE);
        r[3][3] = vadd(0, FP_ONE);
        r[1][2] = vadd(0, S);
        r[1][1] = vadd(0, C);
        r[2][1] = vsub(0, S);
        r[2][2] = vadd(0, C);
    } else if (axis == 1) { /* 00102BE0..: rows (C,0,-S,0) (0,1,0,0) (S,0,C,0) (0,0,0,1) */
        r[1][1] = vadd(0, FP_ONE);
        r[3][3] = vadd(0, FP_ONE);
        r[0][2] = vsub(0, S);
        r[0][0] = vadd(0, C);
        r[2][0] = vadd(0, S);
        r[2][2] = vadd(0, C);
    } else {                /* 00102A90..: rows (C,S,0,0) (-S,C,0,0) (0,0,1,0) (0,0,0,1) */
        uint32_t zero = vu_lane(K_SUB, F_NONE, 0, 0, 0); /* row 3 xyz = 0 - 0 */
        r[3][0] = r[3][1] = r[3][2] = zero;
        r[3][3] = FP_ONE;               /* row 3 w = 1.0 (copied from the constant row) */
        /* row 2 = row 3 rotated by one lane: (y, z, w, x) */
        r[2][0] = r[3][1]; r[2][1] = r[3][2]; r[2][2] = r[3][3]; r[2][3] = r[3][0];
        r[0][1] = vadd(0, S);
        r[0][0] = vadd(0, C);
        r[1][0] = vsub(0, S);
        r[1][1] = vadd(0, C);
    }
    for (int i = 0; i < 4; ++i) {
        Quad row;
        memcpy(row, in[i], sizeof row);
        sdk_apply(out[i], (const Quad *)r, row);
    }
}

/* 00102C58(out, in, angles): 00102A60(z), then 00102BB0(y), 00102B08(x). */
static void sdk_euler(Quad out[4], const Quad in[4], const Quad angles)
{
    uint32_t x = angles[0], y = angles[1], z = angles[2];
    sdk_rotate(out, in, z, 2);
    sdk_rotate(out, (const Quad *)out, y, 1);
    sdk_rotate(out, (const Quad *)out, x, 0);
}

/* 001B1470: while !(f <= pi) f -= 2pi; while f <= -pi f += 2pi. A step that
 * leaves f unchanged repeats forever in the original: -1. */
static int wrap_001B1470(uint32_t f, uint32_t *out)
{
    const uint32_t pi = 0x40490FDBu, npi = 0xC0490FDBu, two_pi = 0x40C90FDBu;
    while (!ee_c_le(f, pi)) {
        uint32_t next = ee_sub(f, two_pi);
        if (next == f) return -1;
        f = next;
    }
    while (ee_c_le(f, npi)) {
        uint32_t next = ee_add(f, two_pi);
        if (next == f) return -1;
        f = next;
    }
    *out = f;
    return 0;
}

/* float_to_int 001281C0 over the unpack 001278C0 (software conversion):
 * exponent 0 -> 0; NaN -> 0; Inf -> saturate; |x| < 1 -> 0; exponent >= 31
 * -> saturate; else the fraction shifted toward zero. */
static int32_t float_to_int_bits(uint32_t b)
{
    uint32_t e = fexp(b), mant = b & 0x7FFFFFu, sign = b >> 31;
    if (e == 0) return 0;
    if (e == 0xFF) {
        if (mant) return 0;
        return sign ? INT32_MIN : INT32_MAX;
    }
    int x = (int)e - 0x7F;
    if (x < 0) return 0;
    if (x >= 31) return sign ? INT32_MIN : INT32_MAX;
    uint32_t v = ((mant << 7) | 0x40000000u) >> (30 - x);
    return (int32_t)(sign ? 0u - v : v);
}

/* 0011E860: abs on the low word (INT_MIN stays INT_MIN). */
static int32_t abs_0011E860(int32_t v) { return v >= 0 ? v : (int32_t)(0u - (uint32_t)v); }

/* ---- public wrappers (floats <-> raw words) ---- */
static void to_quads(Quad q[4], const float *f) { memcpy(q, f, 64); }
static void from_quads(float *f, const Quad q[4]) { memcpy(f, q, 64); }

void em_effect_original_00102C58(float out[16], const float in[16], const float angles[4])
{
    Quad m[4], a;
    to_quads(m, in);
    memcpy(a, angles, sizeof a);
    sdk_euler(m, (const Quad *)m, a);
    from_quads(out, (const Quad *)m);
}
void em_effect_original_00102BB0(float out[16], const float in[16], float angle)
{
    Quad m[4];
    to_quads(m, in);
    sdk_rotate(m, (const Quad *)m, fbits(angle), 1);
    from_quads(out, (const Quad *)m);
}
void em_effect_original_00102760(float out[4], const float in[4])
{
    Quad a, r;
    memcpy(a, in, sizeof a);
    sdk_normalize(r, a);
    memcpy(out, r, sizeof r);
}
void em_effect_original_00102718(float out[4], const float a[4], const float b[4])
{
    Quad x, y, r;
    memcpy(x, a, sizeof x);
    memcpy(y, b, sizeof y);
    sdk_cross(r, x, y);
    memcpy(out, r, sizeof r);
}
void em_effect_original_001026A0(float out[4], const float m[16], const float v[4])
{
    Quad r[4], x, o;
    to_quads(r, m);
    memcpy(x, v, sizeof x);
    sdk_apply(o, (const Quad *)r, x);
    memcpy(out, o, sizeof o);
}
int em_effect_original_001B1470(float angle, float *out)
{
    uint32_t r;
    if (wrap_001B1470(fbits(angle), &r) < 0) return -1;
    *out = bfloat(r);
    return 0;
}
int32_t em_effect_original_float_to_int(float value) { return float_to_int_bits(fbits(value)); }

/* ------------------------------------------------------------------ */
/* Faults, table access.                                               */
/* ------------------------------------------------------------------ */

static int fault_at(EmEffectOriginal *e, uint32_t address, int32_t code)
{
    if (e->fault.code == EM_EFFECT_FAULT_NONE) {
        e->fault.address = address;
        e->fault.code = code;
    }
    return -1;
}
#define NEED(ptr, address) do { if (!(ptr)) return fault_at(e, (address), EM_EFFECT_FAULT_NULL_WORKER); } while (0)
#define CALL(address, expr) do { if ((expr) < 0) return fault_at(e, (address), EM_EFFECT_FAULT_WORKER_FAILED); } while (0)

static int ready(EmEffectOriginal *e) { return e && e->fault.code == EM_EFFECT_FAULT_NONE; }

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* A table byte range, or NULL (fault) outside 0x257C90..0x259C70. */
static uint8_t *table_at(EmEffectOriginal *e, uint32_t address, uint32_t size)
{
    if (address < EM_EFFECT_ORIGINAL_TABLE_BASE ||
        address - EM_EFFECT_ORIGINAL_TABLE_BASE > EM_EFFECT_ORIGINAL_TABLE_BYTES - size) {
        fault_at(e, address, EM_EFFECT_FAULT_BAD_INDEX);
        return NULL;
    }
    return e->tables->bytes + (address - EM_EFFECT_ORIGINAL_TABLE_BASE);
}
static int table32(EmEffectOriginal *e, uint32_t address, uint32_t *v)
{
    uint8_t *p = table_at(e, address, 4);
    if (!p) return -1;
    *v = rd32(p);
    return 0;
}
static int table8(EmEffectOriginal *e, uint32_t address, uint8_t *v)
{
    uint8_t *p = table_at(e, address, 1);
    if (!p) return -1;
    *v = *p;
    return 0;
}

int em_effect_original_load_tables(const uint8_t *elf, size_t size, EmEffectOriginalTables *out)
{
    /* SCUS_971.12: one PROGBITS section, file 0x300 = vram 0x00100000. */
#define ELF_AT(a) (elf + ((size_t)(a) - 0x00100000u + 0x300u))
    if (!elf || !out || size != 1532624u || memcmp(elf, "\x7F" "ELF", 4))
        return -1;
    memcpy(out->bytes, ELF_AT(EM_EFFECT_ORIGINAL_TABLE_BASE), EM_EFFECT_ORIGINAL_TABLE_BYTES);
    out->global = rd32(ELF_AT(0x00259C70u));
    if (out->global != EM_EFFECT_ORIGINAL_TABLE_BASE)
        return -1;
    for (int i = 0; i < EM_EFFECT_ORIGINAL_AREAS; ++i)
        out->area[i] = rd32(ELF_AT(0x00259C74u + 4u * (uint32_t)i));
    for (int i = 0; i < EM_EFFECT_ORIGINAL_SUBTYPES; ++i) {
        out->step[i] = rd32(ELF_AT(0x00255430u + 8u * (uint32_t)i));
        out->handler[i] = rd32(ELF_AT(0x00255434u + 8u * (uint32_t)i));
    }
    return 0;
#undef ELF_AT
}

/* ------------------------------------------------------------------ */
/* Spawn chain.                                                         */
/* ------------------------------------------------------------------ */

static int point_light(EmEffectOriginal *e, const float pos[4], uint32_t entity, int32_t type,
                       uint32_t fa, uint32_t fb)
{
    const EmEffectOriginalWorkers *w = e->workers;
    float color[4];
    for (int i = 0; i < 4; ++i) {
        uint32_t v;
        if (table32(e, entity + 0x10u + 4u * (uint32_t)i, &v) < 0) return -1;
        color[i] = bfloat(v);
    }
    NEED(w->w_001D7FA0, 0x001D7FA0u);
    CALL(0x001D7FA0u, w->w_001D7FA0(w->ctx, pos, color, type, bfloat(fa), bfloat(fb)));
    return 0;
}

int em_effect_original_001D80E0(EmEffectOriginal *e, const float pos[4], const float color[4])
{
    if (!ready(e)) return -1;
    NEED(e->workers, 0x001D80E0u);
    NEED(e->workers->w_001D7FA0, 0x001D7FA0u);
    /* Float arguments 0x3F19999A (0.6) and -1.0, type 0; tail call to 001D7FA0. */
    CALL(0x001D7FA0u, e->workers->w_001D7FA0(e->workers->ctx, pos, color, 0, bfloat(0x3F19999Au),
                                             bfloat(0xBF800000u)));
    return 0;
}

int em_effect_original_001D8100(EmEffectOriginal *e, const float pos[4], const float color[4])
{
    if (!ready(e)) return -1;
    NEED(e->workers, 0x001D8100u);
    NEED(e->workers->w_001D7FA0, 0x001D7FA0u);
    /* Float arguments 0x3F733333 (0.95) and 0xBD4CCCCD (-0.05), type 0; tail call. */
    CALL(0x001D7FA0u, e->workers->w_001D7FA0(e->workers->ctx, pos, color, 0, bfloat(0x3F733333u),
                                             bfloat(0xBD4CCCCDu)));
    return 0;
}

int em_effect_original_001EF940(EmEffectOriginal *e, uint32_t entity, const float pos[4])
{
    if (!ready(e)) return -1;
    NEED(e->globals, 0x008101E4u);
    NEED(e->tables, 0x00259C70u);
    NEED(e->workers, 0x001EF940u);
    const EmEffectOriginalWorkers *w = e->workers;
    if (e->globals->d8101E4 == 3)
        return 0;
    uint32_t sound, f28, f2C;
    if (table32(e, entity + 0x24u, &sound) < 0) return -1;
    if (sound == 0xFFFFFFFFu)
        return 0;
    NEED(pos, 0x00102948u); /* 00102948: pos copied to a stack quadword */
    float copy[4];
    memcpy(copy, pos, sizeof copy);
    if (table32(e, entity + 0x28u, &f28) < 0 || table32(e, entity + 0x2Cu, &f2C) < 0) return -1;
    int32_t a = 0, b = 0, result = 0;
    NEED(w->w_001FBF50, 0x001FBF50u);
    CALL(0x001FBF50u, w->w_001FBF50(w->ctx, copy, bfloat(f28), bfloat(f2C), &a, &b, &result));
    if (result == 0)
        return 0;
    if (table32(e, entity + 0x24u, &sound) < 0) return -1; /* entity +0x24 re-read here */
    NEED(w->w_001FB9F0, 0x001FB9F0u);
    CALL(0x001FB9F0u, w->w_001FB9F0(w->ctx, (int32_t)sound, 0x1000, a, b));
    return 0;
}

int em_effect_original_001EF9D0(EmEffectOriginal *e, uint32_t id, const float *pos, float f12,
                                EmEffectOriginalNode **out)
{
    if (out) *out = NULL;
    if (!ready(e)) return -1;
    NEED(out, 0x001EF9D0u);
    NEED(e->tables, 0x00259C70u);
    NEED(e->workers, 0x001EF9D0u);
    const EmEffectOriginalWorkers *w = e->workers;
    uint32_t base, index;
    if (id & 0x80000000u) {
        base = e->tables->global;         /* the global table, D_00259C70 */
        index = id & 0x7FFFFFFFu;         /* top bit cleared (64-bit shift pair) */
    } else {
        NEED(e->globals, 0x00810700u);
        uint8_t area = e->globals->d810700;
        if (area >= EM_EFFECT_ORIGINAL_AREAS)
            return fault_at(e, 0x00259C74u + 4u * area, EM_EFFECT_FAULT_BAD_INDEX);
        base = e->tables->area[area];     /* D_00259C74[D_00810700] */
        index = id;
    }
    uint32_t entity = base + index * 0x30u;
    uint32_t flag;
    if (table32(e, entity + 0x0Cu, &flag) < 0) return -1;
    if (flag == 0)
        return 0;

    const int special = id == 0x80000026u || id == 0x8000002Cu || id == 0x80000067u;
    if (ee_c_eq(FP_ONE, fbits(f12))) {    /* f12 == 1.0 (EE float compare) */
        if (special) {
            int32_t r;
            NEED(w->w_00122BB8, 0x00122BB8u);
            CALL(0x00122BB8u, w->w_00122BB8(w->ctx, &r));
            int32_t m = id == 0x80000026u ? r % 4 : r % 2;
            if (m >= 0) {
                uint32_t v = (uint32_t)m + (id == 0x80000026u ? 0x18Eu : id == 0x8000002Cu ? 0x18Cu : 0x192u);
                uint8_t *p = table_at(e, base + 0x24u, 4); /* base + 0x24: the table's first record */
                if (!p) return -1;
                wr32(p, v);
            }
        }
    } else if (special) {
        uint8_t *p = table_at(e, base + 0x24u, 4);
        if (!p) return -1;
        wr32(p, 0xFFFFFFFFu);
    }

    uint8_t cls, b4, b8;
    if (table8(e, entity, &cls) < 0) return -1;
    EmEffectOriginalNode *node = NULL;
    NEED(w->w_001AFA90, 0x001AFA90u);
    CALL(0x001AFA90u, w->w_001AFA90(w->ctx, cls, &node));
    if (!node)
        return 0;
    if (table8(e, entity + 4u, &b4) < 0 || table8(e, entity + 8u, &b8) < 0 ||
        table32(e, entity + 0x0Cu, &flag) < 0)
        return -1;
    node->b03 = b4;
    node->subtype = b8;
    node->callback = flag;
    node->live38 = 1;
    node->freed = 0;
    *out = node;
    if (!pos)
        return 0;

    uint32_t kind;
    if (table32(e, entity + 0x20u, &kind) < 0) return -1;
    if (kind == 4) {
        NEED(e->globals, 0x70003B68u);
        EmEffectOriginalGlobals *g = e->globals;
        int32_t d = abs_0011E860((int32_t)((uint32_t)g->spad3B68 - (uint32_t)g->d275C38));
        if (d >= 13) {
            g->d275C38 = g->spad3B68;
            if (point_light(e, pos, entity, 1, 0x3F733333u, 0xBD4CCCCDu) < 0) return -1;
        }
    } else if (kind == 2) {       /* 001D8100(pos, entity + 0x10) */
        if (point_light(e, pos, entity, 0, 0x3F733333u, 0xBD4CCCCDu) < 0) return -1;
    } else if (kind == 1) {       /* 001D80E0(pos, entity + 0x10) */
        if (point_light(e, pos, entity, 0, 0x3F19999Au, 0xBF800000u) < 0) return -1;
    }
    return em_effect_original_001EF940(e, entity, pos) < 0 ? -1 : 0;
}

int em_effect_original_001EFD90(EmEffectOriginal *e, uint32_t id, const float pos[4],
                                const float rot[4], EmEffectOriginalNode **out)
{
    if (out) *out = NULL;
    if (!ready(e)) return -1;
    NEED(out, 0x001EFD90u);
    NEED(rot, 0x001EFD90u); /* 001EFD90 passes rot[3] (rot + 0xC) as f12 */
    EmEffectOriginalNode *node = NULL;
    if (em_effect_original_001EF9D0(e, id, pos, rot[3], &node) < 0) return -1;
    if (!node)
        return 0;
    NEED(pos, 0x00102948u);
    memcpy(node->pos, pos, sizeof node->pos);   /* 00102948(p + 0xB0, pos) */
    memcpy(node->rot, rot, sizeof node->rot);   /* 00102948(p + 0xC0, rot) */
    node->pos[3] = bfloat(FP_ONE);              /* node +0xBC = 1.0 (integer store) */
    *out = node;
    return 0;
}

int em_effect_original_001EFD20(EmEffectOriginal *e, uint32_t id, const float pos[4],
                                EmEffectOriginalNode **out)
{
    if (out) *out = NULL;
    if (!ready(e)) return -1;
    NEED(out, 0x001EFD20u);
    EmEffectOriginalNode *node = NULL;
    if (em_effect_original_001EF9D0(e, id, pos, bfloat(FP_ONE), &node) < 0) return -1;
    if (!node)
        return 0;
    NEED(pos, 0x00102948u);
    memcpy(node->pos, pos, sizeof node->pos);
    memset(node->rot, 0, sizeof node->rot);     /* node +0xC0..+0xCC = 0, word stores */
    node->pos[3] = bfloat(FP_ONE);
    *out = node;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001F0460 ring decals.                                                */
/* ------------------------------------------------------------------ */

int em_effect_original_001F0460(EmEffectOriginal *e, int32_t n, const float src[16])
{
    if (!ready(e)) return -1;
    uint64_t tag;
    float params[4];
    int32_t count, limit;
    switch (n) { /* jtbl_0026E9C0 */
    case 0: {
        tag = UINT64_C(0x20040F8555322078);
        params[0] = 8.0f; params[1] = 8.0f; params[2] = 8.0f; params[3] = 64.0f;
        count = 3; limit = 0x20;
        NEED(src, 0x001F0460u);
        EmEffectOriginalNode *spawned = NULL; /* result unused */
        if (em_effect_original_001EFD20(e, 0x8000000Eu, src + 12, &spawned) < 0) return -1;
        break;
    }
    case 1:
    case 2: /* two identical blocks in the original */
        tag = UINT64_C(0x200418851532218C);
        params[0] = 48.0f; params[1] = 48.0f; params[2] = 48.0f; params[3] = 64.0f;
        count = 3; limit = 0x14;
        break;
    case 3:
        tag = UINT64_C(0x2004108555322080);
        params[0] = 255.0f; params[1] = 0.0f; params[2] = 0.0f; params[3] = 64.0f;
        count = 3; limit = 0x20;
        break;
    case 4:
        tag = UINT64_C(0x20040F8555322078);
        params[0] = 48.0f; params[1] = 32.0f; params[2] = 16.0f; params[3] = 80.0f;
        count = 0x14; limit = 0x20;
        break;
    case 5:
    case 6:
        tag = UINT64_C(0x20040F8555322078);
        params[0] = 16.0f; params[1] = 16.0f; params[2] = 16.0f; params[3] = 64.0f;
        count = n == 5 ? 3 : 2; limit = 0x20;
        break;
    default:
        return 0;
    }
    NEED(e->decals, 0x0081F950u);
    NEED(src, 0x001F0460u);
    EmEffectOriginalDecals *d = e->decals;
    d->index[n] = (int32_t)((uint32_t)d->index[n] + 1u);
    if (d->index[n] >= limit)
        d->index[n] = 0;
    int32_t i = d->index[n];
    if (i < 0 || i >= EM_EFFECT_ORIGINAL_DECAL_SLOTS)
        return fault_at(e, 0x0081F950u + 4u * (uint32_t)n, EM_EFFECT_FAULT_BAD_INDEX);
    EmEffectOriginalDecalSlot *slot = &d->slot[n][i];
    slot->tag = tag;
    slot->life = count * 60;
    memcpy(slot->source, src, sizeof slot->source); /* copy_qw4 */
    memcpy(slot->params, params, sizeof slot->params); /* 00102948(p + 0x40, buf) */
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001CD390 and 001CCF70.                                               */
/* ------------------------------------------------------------------ */

static int look_at_001CD390(EmEffectOriginal *e, Quad out[4], const Quad v)
{
    NEED(e->globals, 0x70003600u);
    uint32_t *s = e->globals->spad3600;
    const uint32_t five = 0x40A00000u;
    /* 0x70003610 block */
    if (ee_c_eq(0, v[0]) && ee_c_eq(0, v[2])) {
        s[4] = v[0];
        s[5] = ee_add(five, v[1]);
        s[6] = ee_add(five, v[2]);
    } else {
        s[4] = v[0];
        s[5] = ee_add(five, v[1]);
        s[6] = v[2];
    }
    s[7] = FP_ONE;
    s[0] = v[0]; s[1] = v[1]; s[2] = v[2]; s[3] = FP_ONE; /* 0x70003600 block */
    Quad s00, s10, s20, s30;
    memcpy(s00, s, 16);
    memcpy(s10, s + 4, 16);
    sdk_cross(s20, s10, s00);        /* 00102718(3620, 3610, 3600) */
    sdk_cross(s30, s20, s00);        /* 00102718(3630, 3620, 3600) */
    sdk_normalize(s20, s20);         /* 00102760(3620, 3620) */
    sdk_normalize(s30, s30);         /* 00102760(3630, 3630) */
    memcpy(s + 8, s20, 16);
    memcpy(s + 12, s30, 16);
    sdk_identity(out);               /* 001029C0(out) */
    memcpy(out[0], s20, 12);         /* 001031E0(out, 3620) */
    memcpy(out[1], s30, 12);         /* 001031E0(out + 0x10, 3630) */
    memcpy(out[2], s00, 12);         /* 001031E0(out + 0x20, 3600) */
    return 0;
}

int em_effect_original_001CD390(EmEffectOriginal *e, float out[16], const float v[4])
{
    if (!ready(e)) return -1;
    NEED(out, 0x001CD390u);
    NEED(v, 0x001CD390u);
    Quad m[4], x;
    memcpy(x, v, sizeof x);
    if (look_at_001CD390(e, m, x) < 0) return -1;
    from_quads(out, (const Quad *)m);
    return 0;
}

/* 001CCF70's clip test of v.xyz against v.w (the original uses a vclipw): bits
 * 0/1 x > |w| / x < -|w|, 2/3 for y, 4/5 for z. Not part of the measured
 * float model: finite inputs only (DAZ applied). */
static int clip_judge(const Quad v, unsigned *flags)
{
    for (int i = 0; i < 4; ++i)
        if (fexp(v[i]) == 0xFF) return -1;
    double w = fabs((double)bfloat(daz(v[3])));
    unsigned f = 0;
    for (int i = 0; i < 3; ++i) {
        double c = (double)bfloat(daz(v[i]));
        if (c > w) f |= 1u << (2 * i);
        if (c < -w) f |= 2u << (2 * i);
    }
    *flags = f;
    return 0;
}

static int project_001CCF70(EmEffectOriginal *e, const Quad pos, int32_t *result)
{
    NEED(e->view, 0x00275670u);
    NEED(e->globals, 0x70003600u);
    EmEffectOriginalGlobals *g = e->globals;
    Quad clip[4], cam[4], fog, p, v;
    to_quads(clip, e->view->clip); /* 001CD370(0) = ctx + 0x2240 */
    memcpy(p, pos, sizeof p);
    p[3] = FP_ONE;                 /* the w term uses the constant 1.0 */
    sdk_apply(v, (const Quad *)clip, p);
    unsigned flags;
    if (clip_judge(v, &flags) < 0)
        return fault_at(e, 0x001CCFACu, EM_EFFECT_FAULT_UNMEASURED);
    if (flags & 0x3F) {            /* any of this test's six clip flags */
        *result = EM_EFFECT_ORIGINAL_CLIPPED;
        return 0;
    }
    memcpy(fog, e->view->fog, sizeof fog);
    to_quads(cam, e->view->camera);
    sdk_apply(v, (const Quad *)cam, p);
    uint32_t q = vu_div(FP_ONE, v[3], 1);                 /* Q = 1.0 / w (reciprocal) */
    uint32_t stack_w = v[3];                              /* camera-space w kept on the stack */
    for (int i = 0; i < 3; ++i) v[i] = vu_lane(K_MUL, F_S, v[i], q, 0); /* xyz *= Q */
    uint32_t acc = vu_lane(K_MUL, F_S, FP_ONE, fog[2], 0);             /* ACC.w = 1.0 * fog.z */
    v[3] = vu_lane(K_MADD, F_STA, fog[3], v[3], acc);                  /* w = ACC.w + fog.w * w */
    v[3] = vu_min(v[3], fog[0]);                          /* w = min(w, fog.x) */
    v[3] = vu_max(v[3], 0);                               /* w = max(w, 0) */
    for (int i = 0; i < 4; ++i) g->spad3600[i] = vu_ftoi(v[i], 4); /* 12.4 fixed -> 0x70003600 */
    g->d275C04 = float_to_int_bits(stack_w);
    *result = (int32_t)g->spad3600[2];                    /* the z word, 0x70003608 */
    return 0;
}

int em_effect_original_001CCF70(EmEffectOriginal *e, const float pos[4], int32_t *result)
{
    if (!ready(e)) return -1;
    NEED(pos, 0x001CCF70u);
    NEED(result, 0x001CCF70u);
    Quad p;
    memcpy(p, pos, sizeof p);
    return project_001CCF70(e, p, result);
}

/* ------------------------------------------------------------------ */
/* 001EA240.                                                            */
/* ------------------------------------------------------------------ */

static int in_set(uint8_t v, const uint8_t *set, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (set[i] == v) return 1;
    return 0;
}
#define IN(v, ...) in_set((v), (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

static int rumble(EmEffectOriginal *e, int32_t channel, uint32_t f12, uint32_t f13)
{
    const EmEffectOriginalWorkers *w = e->workers;
    NEED(w->w_0021B9A0, 0x0021B9A0u);
    CALL(0x0021B9A0u, w->w_0021B9A0(w->ctx, channel, bfloat(f12), bfloat(f13)));
    return 0;
}

static int rand_value(EmEffectOriginal *e, int32_t *r)
{
    const EmEffectOriginalWorkers *w = e->workers;
    NEED(w->w_00122BB8, 0x00122BB8u);
    CALL(0x00122BB8u, w->w_00122BB8(w->ctx, r));
    return 0;
}

/* (pi * (120 * (rand / 2^31) - 60)) / 180, 0x1EA758..0x1EA7A8. */
static int jitter(EmEffectOriginal *e, uint32_t *out)
{
    int32_t r;
    if (rand_value(e, &r) < 0) return -1;
    uint32_t f = ee_div(ee_cvt_s_w((uint32_t)r), 0x4F000000u);
    f = ee_sub(ee_mul(0x42F00000u, f), 0x42700000u);
    *out = ee_div(ee_mul(0x40490FDBu, f), 0x43340000u);
    return 0;
}

/* State 0 (0x1EA294..0x1EA890): seeding and the direction block. */
static int driver_seed(EmEffectOriginal *e, EmEffectOriginalNode *n)
{
    EmEffectOriginalGlobals *g = e->globals;
    EmEffectOriginalWork *w = g->d275C34;
    uint8_t sub = n->subtype;
    uint32_t acc0 = 0, limit = 0x3FC00000u; /* default: 0.0, 1.5 */
    if (sub == 0x13) {
        limit = 0x40C00000u;                                       /* 6.0 */
    } else if (IN(sub, 0x2A, 0x24, 0x09, 0x1B, 0x22, 0x21, 0x1F, 0x0E, 0x29, 0x1A, 0x19, 0x28,
                  0x1D, 0x1C, 0x17, 0x16, 0x04, 0x15, 0x14)) {
        acc0 = 0x3E4CCCCDu;                                        /* 0.2 */
    } else if (sub == 0x20) {
        limit = 0x40000000u;                                       /* 2.0 */
    }
    w->accumulator = bfloat(acc0);
    w->limit = bfloat(limit);
    sub = n->subtype;
    if (sub >= EM_EFFECT_ORIGINAL_SUBTYPES)
        return fault_at(e, 0x00255430u + 8u * sub, EM_EFFECT_FAULT_BAD_INDEX);
    w->step = bfloat(e->tables->step[sub]);
    int32_t r;
    if (rand_value(e, &r) < 0) return -1;
    w->seed = r;
    if (rand_value(e, &r) < 0) return -1;
    w->fraction = bfloat(ee_div(ee_cvt_s_w((uint32_t)r), 0x4F000000u)); /* / 2^31 */
    n->b0C = 0;
    n->b09 = 0;
    n->state = 1;

    Quad m[4], rot, pos;
    to_quads(m, n->matrix);
    memcpy(rot, n->rot, sizeof rot);
    memcpy(pos, n->pos, sizeof pos);
    sub = n->subtype;
    if (IN(sub, 0x29, 0x28, 0x1D, 0x1C, 0x17, 0x16, 0x04)) {
        if (n->live38 == 0)
            return 0;
        uint32_t d;
        if (jitter(e, &d) < 0) return -1;
        rot[0] = ee_add(rot[0], d);
        memcpy(n->rot, rot, sizeof rot);
        if (jitter(e, &d) < 0) return -1;
        rot[1] = ee_add(rot[1], d);
        memcpy(n->rot, rot, sizeof rot);
        if (wrap_001B1470(rot[0], &rot[0]) < 0 || wrap_001B1470(rot[1], &rot[1]) < 0)
            return fault_at(e, 0x001B1470u, EM_EFFECT_FAULT_UNMEASURED);
        memcpy(n->rot, rot, sizeof rot);
        sdk_identity(m);
        sdk_euler(m, (const Quad *)m, rot);
        sdk_translate(m, (const Quad *)m, pos);
    } else if (IN(sub, 0x25, 0x01, 0x05, 0x0B, 0x0A)) {
        sdk_identity(m);
        sdk_euler(m, (const Quad *)m, rot);
        sdk_rotate(m, (const Quad *)m, 0x40490FDBu, 1);   /* 00102BB0(pi) */
        sdk_translate(m, (const Quad *)m, pos);
        m[3][1] = ee_add(m[3][1], 0x3F000000u);          /* +0x104 += 0.5 */
    } else if (IN(sub, 0x0E, 0x24, 0x09)) {
        sdk_identity(m);
        sdk_euler(m, (const Quad *)m, rot);
        sdk_rotate(m, (const Quad *)m, 0x40490FDBu, 1);
        uint32_t *s = g->spad38A0;
        s[0] = 0;
        s[1] = 0;
        s[2] = ee_mul(0xC0600000u, ee_div(fbits(g->d8102E8), 0x3F4CCCCDu)); /* -3.5 * (x / 0.8) */
        s[3] = FP_ONE;
        Quad v;
        memcpy(v, s, sizeof v);
        sdk_apply(v, (const Quad *)m, v);                 /* 001026A0(38A0, D0, 38A0) */
        memcpy(s, v, sizeof v);
        sdk_translate(m, (const Quad *)m, pos);
        for (int i = 0; i < 3; ++i) m[3][i] = ee_add(m[3][i], s[i]);
    } else if (IN(sub, 0x26, 0x1B, 0x18, 0x23, 0x00)) {
        sdk_normalize(rot, rot);                          /* 00102760(C0, C0) */
        memcpy(n->rot, rot, sizeof rot);
        if (look_at_001CD390(e, m, rot) < 0) return -1;
        sdk_translate(m, (const Quad *)m, pos);
        if ((sub == 0x23 || sub == 0x00) && n->live38 != 0) {
            float src[16];
            from_quads(n->matrix, (const Quad *)m);
            memcpy(src, n->matrix, sizeof src);
            return em_effect_original_001F0460(e, 0, src) < 0 ? -1 : 0;
        }
    } else if (IN(sub, 0x1E, 0x13, 0x1A, 0x19)) {
        return 0;
    } else {
        if (n->live38 == 0)
            return 0;
        sdk_identity(m);
        sdk_euler(m, (const Quad *)m, rot);
        sdk_translate(m, (const Quad *)m, pos);
    }
    from_quads(n->matrix, (const Quad *)m);
    return 0;
}

/* State 1 (0x1EA894..0x1EAB28). */
static int driver_run(EmEffectOriginal *e, EmEffectOriginalNode *n)
{
    EmEffectOriginalGlobals *g = e->globals;
    const EmEffectOriginalWorkers *w = e->workers;
    uint8_t sub = n->subtype;
    if (IN(sub, 0x28, 0x1D, 0x1C)) {
        if (rumble(e, 2, FP_ONE, 0x42C80000u) < 0 || rumble(e, 3, FP_ONE, 0x42C80000u) < 0) return -1;
    } else if (IN(sub, 0x06, 0x29, 0x1A, 0x19, 0x17, 0x16, 0x04, 0x15, 0x14, 0x03)) {
        if (rumble(e, 2, FP_ONE, 0x41A00000u) < 0 || rumble(e, 3, FP_ONE, 0x41A00000u) < 0) return -1;
    }
    Quad t;
    memcpy(t, &n->matrix[12], sizeof t);
    int32_t depth;
    if (project_001CCF70(e, t, &depth) < 0) return -1;     /* 001CCF70(e + 0x100) */
    EmEffectOriginalWork *wk = g->d275C34;
    NEED(wk, 0x00275C34u);
    wk->seed_copy = wk->seed;
    sub = n->subtype;
    if (sub >= EM_EFFECT_ORIGINAL_SUBTYPES)
        return fault_at(e, 0x00255434u + 8u * sub, EM_EFFECT_FAULT_BAD_INDEX);
    uint32_t handler = e->tables->handler[sub];
    NEED(w->w_handler, handler);
    CALL(handler, w->w_handler(w->ctx, handler, n, depth, wk));
    wk = g->d275C34;
    NEED(wk, 0x00275C34u);
    wk->accumulator = bfloat(ee_add(fbits(wk->accumulator), fbits(wk->step)));
    wk = g->d275C34;
    uint32_t limit = fbits(wk->limit), acc = fbits(wk->accumulator);
    if (ee_c_eq(0, limit)) {
        if (!ee_c_le(acc, 0x40000000u))
            wk->accumulator = bfloat(ee_sub(acc, FP_ONE));
    } else if (!ee_c_le(acc, limit)) {
        n->state = 3;
    }
    sub = n->subtype;
    if (IN(sub, 0x06, 0x29, 0x1A, 0x19, 0x28, 0x1D, 0x1C, 0x17, 0x16, 0x04, 0x15, 0x14, 0x03))
        if (rumble(e, 1, 0, 0) < 0) return -1;
    return 0;
}

int em_effect_original_001EA240(EmEffectOriginal *e, EmEffectOriginalNode *n)
{
    if (!ready(e)) return -1;
    NEED(n, EM_EFFECT_ORIGINAL_DRIVER);
    NEED(e->globals, 0x00275C30u);
    NEED(e->tables, 0x00255430u);
    NEED(e->workers, EM_EFFECT_ORIGINAL_DRIVER);
    if (n->freed)
        return fault_at(e, EM_EFFECT_ORIGINAL_DRIVER, EM_EFFECT_FAULT_BAD_INDEX);
    e->globals->d275C34 = &n->work;
    e->globals->d275C30 = n;
    switch (n->state) {
    case 2:
    case 3: /* 0x1EAB2C/0x1EAB30: 001AFC10(e) */
        NEED(e->workers->w_001AFC10, 0x001AFC10u);
        CALL(0x001AFC10u, e->workers->w_001AFC10(e->workers->ctx, n));
        n->freed = 1;
        return 0;
    case 0:
        if (driver_seed(e, n) < 0) return -1;
        return driver_run(e, n) < 0 ? -1 : 1;
    case 1:
        return driver_run(e, n) < 0 ? -1 : 1;
    default:
        return 1;
    }
}
