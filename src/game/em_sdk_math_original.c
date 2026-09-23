/* The boot ELF's SDK float math, translated from the original instructions.
 * See em_sdk_math_original.h and docs/SDK_MATH_ORIGINAL.md. Every address
 * is a boot-ELF runtime address; every value is a raw binary32 word and
 * every float operation is the EE COP1 instruction at the cited address,
 * computed by the shared EE model game/em_ee_float.h (docs/EE_FLOAT_MODEL.md). */
#include "game/em_sdk_math_original.h"

#include "game/em_ee_float.h"

typedef uint32_t F; /* binary32 bits */

#define SIGN EM_EE_SIGN

static int fail(uint32_t *fault, uint32_t address)
{
    if (fault)
        *fault = address;
    return -1;
}

/* Every COP1 instruction below is computed by the shared EE float model,
 * game/em_ee_float.h (docs/EE_FLOAT_MODEL.md section 6): em_ee_*_bits on
 * raw binary32 words. This module keeps no float arithmetic of its own. */

/* ---------------------------------------------------------------------------
 * Leaves.
 * ------------------------------------------------------------------------- */

#define ONE 0x3F800000u
#define HALF 0x3F000000u
#define TWO8 0x43800000u
#define TWON8 0x3B800000u
#define HUGE_ 0x7149F2CAu     /* 1.0e30 */
#define TINY 0x0DA24260u      /* 1.0e-30 */

/* 0011DF78: and with 0x7FFFFFFF. */
static F sdk_0011DF78(F x) { return x & 0x7FFFFFFFu; }

/* 0011E080: (0x7F800000 - (x & 0x7FFFFFFF)) >> 31 (srl). */
static int32_t sdk_0011E080(F x) { return (int32_t)((0x7F800000u - (x & 0x7FFFFFFFu)) >> 31); }

/* 0011DE60: magnitude of f12, sign of f13. */
static F sdk_0011DE60(F x, F y) { return (x & 0x7FFFFFFFu) | (y & SIGN); }

/* 0011DF98 (floorf). */
static F sdk_0011DF98(F x)
{
    int32_t i0 = (int32_t)x;
    const int32_t j0 = ((i0 >> 23) & 0xFF) - 0x7F;                /* 0x11DFA0..0x11DFA8 */
    if (!(j0 < 23)) {                                            /* 0x11DFAC: j0 < 23 */
        if (j0 == 0x80)
            return em_ee_add_bits(x, x);                                 /* 0x11E06C */
        return x;                                                /* 0x11E01C */
    }
    if (j0 < 0) {
        /* 0x11DFD0: the EE sum x + 1e30; 0x11DFD4: the compare 0 < sum. */
        if (em_ee_c_lt_bits(0u, em_ee_add_bits(x, HUGE_))) {
            if (i0 >= 0)
                i0 = 0;
            else if (i0 & 0x7FFFFFFF)
                i0 = (int32_t)0xBF800000u;                       /* 0x11E000 movn */
        }
        return (F)i0;
    }
    const uint32_t i = 0x7FFFFFu >> j0;                          /* 0x11E008 srav */
    if (((uint32_t)i0 & i) == 0)
        return x;                                                /* integral: 0x11E01C */
    if (em_ee_c_lt_bits(0u, em_ee_add_bits(x, HUGE_))) {                         /* 0x11E030 / 0x11E034 */
        if (i0 < 0)
            i0 = (int32_t)((uint32_t)i0 + (0x800000u >> j0));    /* 0x11E04C srav, addu */
        i0 = (int32_t)((uint32_t)i0 & ~i);                       /* 0x11E05C */
    }
    return (F)i0;
}

/* 0011E148 (scalbnf). */
static F sdk_0011E148(F x, int32_t n)
{
    F hx = x;
    int32_t k = (int32_t)((hx & 0x7F800000u) >> 23);             /* 0x11E15C / 0x11E160 */
    if (k == 0) {
        if ((hx & 0x7FFFFFFFu) == 0)
            return x;                                            /* 0x11E180 */
        hx = em_ee_mul_bits(x, 0x4C000000u);                             /* 0x11E19C x * 2^25 */
        k = (int32_t)((hx & 0x7F800000u) >> 23) - 25;            /* 0x11E1B0 / 0x11E1C0 */
        if (n < -50000)                                          /* 0x11E1B8: n < -50000 */
            return em_ee_mul_bits(hx, TINY);                             /* 0x11E1D4 */
    }
    if (k == 0xFF)
        return em_ee_add_bits(hx, hx);                                   /* 0x11E1EC */
    k = (int32_t)((uint32_t)k + (uint32_t)n);                    /* 0x11E1E0 addu */
    F sized;
    if (!(k < 0xFF))
        goto overflow;                                           /* 0x11E1F4 */
    if (k > 0)
        return (hx & 0x807FFFFFu) | ((uint32_t)k << 23);         /* 0x11E204..0x11E218 */
    if (k < -24) {                                               /* 0x11E228: k < -24 */
        if (n > 50000)                                           /* 0x11E234 */
            goto overflow;
        return em_ee_mul_bits(sdk_0011DE60(TINY, hx), TINY);             /* 0x11E25C, 0x11E264 */
    }
    sized = (hx & 0x807FFFFFu) | ((uint32_t)(k + 25) << 23);     /* 0x11E26C..0x11E280 */
    return em_ee_mul_bits(sized, 0x33000000u);                           /* 0x11E290 * 2^-25 */
overflow:
    return em_ee_mul_bits(sdk_0011DE60(HUGE_, hx), HUGE_);               /* 0x11E25C, 0x11E264 */
}

/* 0011D770(x, y, iy): the sine kernel. */
static F sdk_0011D770(F x, F y, int32_t iy)
{
    const int32_t ix = (int32_t)(x & 0x7FFFFFFFu);
    /* 0x11D790 slt 0x31FFFFFF; 0x11D79C cvt.w.s: a zero word returns x. */
    if (!(ix > 0x31FFFFFF) && em_ee_word_int(em_ee_cvt_w_s_bits(x)) == 0)
        return x;                                                /* 0x11D7B4 mov.s */
    const F z = em_ee_mul_bits(x, x);                                    /* 0x11D798 / 0x11D7AC */
    F r = em_ee_mul_bits(z, 0x2F2EC9D3u);                                /* 0x11D7DC; S6 at 0x11D7B8 */
    const F v = em_ee_mul_bits(z, x);                                    /* 0x11D7F8 */
    r = em_ee_add_bits(r, 0xB2D72F34u);                                  /* 0x11D7FC; S5 */
    r = em_ee_mul_bits(z, r);
    r = em_ee_add_bits(r, 0x3638EF1Bu);                                  /* 0x11D804; S4 */
    r = em_ee_mul_bits(z, r);
    r = em_ee_add_bits(r, 0xB9500D01u);                                  /* 0x11D80C; S3 */
    r = em_ee_mul_bits(z, r);
    r = em_ee_add_bits(r, 0x3C088889u);                                  /* 0x11D818; S2 */
    if (iy == 0) {                                               /* 0x11D814: branch on iy */
        F t = em_ee_add_bits(em_ee_mul_bits(z, r), 0xBE2AAAABu);                 /* 0x11D81C / 0x11D82C; S1 */
        return em_ee_add_bits(x, em_ee_mul_bits(v, t));                          /* 0x11D830 / 0x11D838 */
    }
    F c = em_ee_mul_bits(y, HALF);                                       /* 0x11D854 */
    const F vr = em_ee_mul_bits(v, r);                                   /* 0x11D844 */
    const F vs = em_ee_mul_bits(v, 0xBE2AAAABu);                         /* 0x11D858 */
    c = em_ee_sub_bits(c, vr);                                           /* 0x11D85C */
    c = em_ee_mul_bits(z, c);                                            /* 0x11D860 */
    c = em_ee_sub_bits(c, y);                                            /* 0x11D864 */
    c = em_ee_sub_bits(c, vs);                                           /* 0x11D868 */
    return em_ee_sub_bits(x, c);                                         /* 0x11D870 */
}

/* 0011CCC8(x, y): the cosine kernel. */
static F sdk_0011CCC8(F x, F y)
{
    const int32_t ix = (int32_t)(x & 0x7FFFFFFFu);
    if (!(ix > 0x31FFFFFF) && em_ee_word_int(em_ee_cvt_w_s_bits(x)) == 0)                /* 0x11CCE4 / 0x11CCF0 */
        return ONE;                                              /* 0x11CD04 */
    const F z = em_ee_mul_bits(x, x);                                    /* 0x11CCEC / 0x11CD00 */
    F r = em_ee_mul_bits(z, 0xAD47D74Eu);                                /* 0x11CD44; C6 at 0x11CD18 */
    r = em_ee_add_bits(r, 0x310F74F6u);                                  /* 0x11CD70; C5 */
    r = em_ee_mul_bits(z, r);
    r = em_ee_add_bits(r, 0xB493F27Cu);                                  /* 0x11CD78; C4 */
    r = em_ee_mul_bits(z, r);
    r = em_ee_add_bits(r, 0x37D00D01u);                                  /* 0x11CD80; C3 */
    r = em_ee_mul_bits(z, r);
    r = em_ee_add_bits(r, 0xBAB60B61u);                                  /* 0x11CD88; C2 */
    r = em_ee_mul_bits(z, r);
    r = em_ee_add_bits(r, 0x3D2AAAABu);                                  /* 0x11CD90; C1 */
    r = em_ee_mul_bits(z, r);                                            /* 0x11CD98 */
    if (!(ix > 0x3E999999)) {                                    /* 0x11CD60 / 0x11CD94 */
        const F zr = em_ee_mul_bits(z, r);                               /* 0x11CD9C */
        const F xy = em_ee_mul_bits(x, y);                               /* 0x11CDA8 */
        const F hz = em_ee_mul_bits(z, HALF);                            /* 0x11CDB4 */
        return em_ee_sub_bits(ONE, em_ee_sub_bits(hz, em_ee_sub_bits(zr, xy)));          /* 0x11CDB8..0x11CDC4 */
    }
    /* 0x11CDC8: qx = 0.28125 above 0x3F480000, else ix - 2^24 (addu 0xFF000000). */
    const F qx = ix > 0x3F480000 ? 0x3E900000u : (F)ix + 0xFF000000u;
    const F zr = em_ee_mul_bits(z, r);                                   /* 0x11CDF0 */
    const F xy = em_ee_mul_bits(x, y);                                   /* 0x11CDF4 */
    const F hz = em_ee_mul_bits(z, HALF);                                /* 0x11CE00 */
    const F a = em_ee_sub_bits(zr, xy);                                  /* 0x11CE08 */
    const F hq = em_ee_sub_bits(hz, qx);                                 /* 0x11CE0C */
    const F iq = em_ee_sub_bits(ONE, qx);                                /* 0x11CE10 */
    return em_ee_sub_bits(iq, em_ee_sub_bits(hq, a));                            /* 0x11CE14 / 0x11CE1C */
}

/* 0011CB90: the integer digit-by-digit square root. */
static F sdk_0011CB90(F x)
{
    uint32_t ix = x;
    if ((ix & 0x7F800000u) == 0x7F800000u)                       /* 0x11CBA0 */
        return em_ee_add_bits(em_ee_mul_bits(x, x), x);                          /* 0x11CBA8 / 0x11CBB0 */
    if (!((int32_t)ix > 0)) {                                    /* 0x11CBB8 bgtz */
        if ((ix & 0x7FFFFFFFu) == 0)
            return x;                                            /* 0x11CBD0 mov.s */
        if ((int32_t)ix < 0) {                                   /* 0x11CBD4 bgez */
            const F d = em_ee_sub_bits(x, x);                            /* 0x11CBDC */
            return em_ee_div_bits(d, d);                                 /* 0x11CBE8 */
        }
    }
    int32_t m = (int32_t)ix >> 23;                               /* 0x11CBBC sra */
    if (m == 0) {                                                /* 0x11CBF4: subnormal */
        int32_t i = 0;
        if (!(ix & 0x800000u)) {
            do {                                                 /* 0x11CC10 */
                ix <<= 1;
                ++i;
            } while (!(ix & 0x800000u));
        }
        m = 1 - i;                                               /* 0x11CC2C / 0x11CC38 */
    }
    m -= 127;                                                    /* 0x11CC40 */
    const uint32_t odd = (uint32_t)m & 1u;                       /* 0x11CC48 */
    ix = (ix & 0x7FFFFFu) | 0x800000u;                           /* 0x11CC50 / 0x11CC58 */
    m >>= 1;                                                     /* 0x11CC54 sra */
    ix <<= odd;                                                  /* 0x11CC5C sllv */
    const uint32_t exponent = (uint32_t)m << 23;                 /* 0x11CC60 */
    ix <<= 1;                                                    /* 0x11CC64 */
    uint32_t s = 0, q = 0, r = 0x01000000u;
    while (r != 0) {                                             /* 0x11CC78 .. 0x11CC9C */
        const uint32_t t = s + r;
        if (!((int32_t)ix < (int32_t)t)) {                       /* 0x11CC7C slt */
            s = t + r;
            ix -= t;
            q += r;
        }
        ix <<= 1;                                                /* 0x11CC9C (delay slot) */
        r >>= 1;
    }
    if (ix != 0)                                                 /* 0x11CCA0 */
        q += q & 1u;
    ix = (uint32_t)((int32_t)q >> 1) + 0x3F000000u;              /* 0x11CCAC / 0x11CCB4 */
    return ix + exponent;                                        /* 0x11CCB8 */
}

/* ---------------------------------------------------------------------------
 * Table readers.
 * ------------------------------------------------------------------------- */

/* 0011D878(x, y, iy): the tangent kernel. */
static int sdk_0011D878(const EmSdkMathTables *t, F x, F y, int32_t iy, F *out, uint32_t *fault)
{
    const int32_t hx = (int32_t)x;
    const int32_t ix = hx & 0x7FFFFFFF;
    if (!(ix > 0x317FFFFF) && em_ee_word_int(em_ee_cvt_w_s_bits(x)) == 0) {             /* 0x11D898 / 0x11D8A4 */
        if ((ix | (int32_t)((uint32_t)iy + 1u)) == 0) {          /* 0x11D8B8 / 0x11D8BC */
            *out = em_ee_div_bits(ONE, sdk_0011DF78(x));                 /* 0x11D8E0 */
            return 0;
        }
        if (iy == 1) {
            *out = x;                                            /* 0x11D918 */
            return 0;
        }
        *out = em_ee_div_bits(0xBF800000u, x);                           /* 0x11D908 */
        return 0;
    }
    F z, w;
    const int big = ix > 0x3F2CA13F;                             /* 0x11D924 */
    if (big) {
        if (hx < 0) {                                            /* 0x11D930 */
            x = em_ee_neg_bits(x);
            y = em_ee_neg_bits(y);
        }
        z = em_ee_sub_bits(0x3F490FDAu, x);                              /* 0x11D958 pio4 - x */
        w = em_ee_sub_bits(0x33222168u, y);                              /* 0x11D95C pio4lo - y */
        x = em_ee_add_bits(z, w);                                        /* 0x11D964 */
        y = 0u;                                                  /* 0x11D960 */
    }
    z = em_ee_mul_bits(x, x);                                            /* 0x11D92C / 0x11D968 */
    if (!t)
        return fail(fault, 0x0011D974u);
    const uint32_t *T = t->tan_t;
    w = em_ee_mul_bits(z, z);                                            /* 0x11D980 */
    const F s = em_ee_mul_bits(z, x);                                    /* 0x11D98C */
    F a = em_ee_add_bits(T[10], em_ee_mul_bits(w, T[12]));                       /* 0x11D998 / 0x11D9B0 */
    F b = em_ee_add_bits(T[9], em_ee_mul_bits(w, T[11]));                        /* 0x11D9A0 / 0x11D9B8 */
    const F t0s = em_ee_mul_bits(T[0], s);                               /* 0x11D9AC */
    a = em_ee_add_bits(T[8], em_ee_mul_bits(w, a));                              /* 0x11D9C8 / 0x11D9D8 */
    b = em_ee_add_bits(T[7], em_ee_mul_bits(w, b));                              /* 0x11D9D0 / 0x11D9DC */
    a = em_ee_add_bits(T[6], em_ee_mul_bits(w, a));                              /* 0x11D9E0 / 0x11D9E8 */
    b = em_ee_add_bits(T[5], em_ee_mul_bits(w, b));                              /* 0x11D9E4 / 0x11D9EC */
    a = em_ee_add_bits(T[4], em_ee_mul_bits(w, a));                              /* 0x11D9F0 / 0x11D9F8 */
    b = em_ee_add_bits(T[3], em_ee_mul_bits(w, b));                              /* 0x11D9F4 / 0x11D9FC */
    a = em_ee_add_bits(T[2], em_ee_mul_bits(w, a));                              /* 0x11DA00 / 0x11DA08 */
    const F r0 = em_ee_add_bits(T[1], em_ee_mul_bits(w, b));                     /* 0x11DA04 / 0x11DA0C */
    const F v = em_ee_mul_bits(z, a);                                    /* 0x11DA10 */
    F r = em_ee_add_bits(r0, v);                                         /* 0x11DA14 */
    r = em_ee_mul_bits(s, r);                                            /* 0x11DA18 */
    r = em_ee_add_bits(r, y);                                            /* 0x11DA1C */
    r = em_ee_mul_bits(z, r);                                            /* 0x11DA20 */
    r = em_ee_add_bits(y, r);                                            /* 0x11DA24 */
    r = em_ee_add_bits(r, t0s);                                          /* 0x11DA28 */
    w = em_ee_add_bits(x, r);                                            /* 0x11DA30 */
    if (big) {
        const F vv = em_ee_cvt_s_w_bits(em_ee_int_word(iy));                             /* 0x11DA38 */
        const F sign = em_ee_cvt_s_w_bits(em_ee_int_word(1 - (int32_t)(((uint32_t)hx >> 30) & 2u))); /* 0x11DA3C..0x11DA58 */
        F e = em_ee_div_bits(em_ee_mul_bits(w, w), em_ee_add_bits(w, vv));               /* 0x11DA40 / 0x11DA4C / 0x11DA64 */
        e = em_ee_sub_bits(e, r);                                        /* 0x11DA68 */
        e = em_ee_sub_bits(x, e);                                        /* 0x11DA6C */
        e = em_ee_add_bits(e, e);                                        /* 0x11DA70 */
        e = em_ee_sub_bits(vv, e);                                       /* 0x11DA74 */
        *out = em_ee_mul_bits(sign, e);                                  /* 0x11DA7C */
        return 0;
    }
    if (iy == 1) {
        *out = w;                                                /* 0x11DA88 */
        return 0;
    }
    const F zc = w & 0xFFFFF000u;                                /* 0x11DA8C..0x11DAA0 */
    const F vd = em_ee_sub_bits(r, em_ee_sub_bits(zc, x));                       /* 0x11DAA4 / 0x11DABC */
    const F aa = em_ee_div_bits(0xBF800000u, w);                         /* 0x11DAB8 */
    const F tc = aa & 0xFFFFF000u;                               /* 0x11DAC8 */
    const F s1 = em_ee_add_bits(em_ee_mul_bits(tc, zc), ONE);                    /* 0x11DAD0 / 0x11DADC */
    F e = em_ee_add_bits(s1, em_ee_mul_bits(tc, vd));                            /* 0x11DAE4 / 0x11DAEC */
    e = em_ee_mul_bits(aa, e);                                           /* 0x11DAF0 */
    *out = em_ee_add_bits(tc, e);                                        /* 0x11DAF8 */
    return 0;
}

/* The original's integer adds and subtracts wrap at 32 bits. */
static int32_t wadd(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }

/* The stack arrays of 0011CE20 hold 20 entries each (iq at sp+0, f at
 * sp+0x50, fq at sp+0xA0, q at sp+0xF0). */
#define CE20_N 20
#define IN_RANGE(i) ((i) >= 0 && (i) < CE20_N)

/* 0011CE20(x, y, e0, nx, prec, ipio2): the large-argument reduction. */
static int sdk_0011CE20(const EmSdkMathTables *t, const F *x, F *y, int32_t e0, int32_t nx,
                        int32_t prec, const int32_t *ipio2, size_t ipio2_count, int32_t *out_n,
                        uint32_t *fault)
{
    F f[CE20_N], q[CE20_N], fq[CE20_N];
    int32_t iq[CE20_N];
    const uint32_t here = 0x0011CE20u;
    if (!t)
        return fail(fault, 0x0011CEA0u);                         /* lw init_jk[prec] */
    if (!x || !y || !out_n || !ipio2 || prec < 0 || prec > 3 || nx < 0 || nx > CE20_N)
        return fail(fault, here);
    const int32_t jk = t->init_jk[prec];                         /* 0x11CEA0 */
    const int32_t jp = jk;
    const int32_t jx = nx - 1;                                   /* 0x11CE7C */
    /* 0x11CE20..0x11CE64: jv = (e0 - 3) / 8 (rounded toward zero by a conditional bias, then an arithmetic shift), at least 0. */
    const int32_t e0m3 = wadd(e0, -3);
    int32_t jv = (e0m3 > -1 ? e0m3 : wadd(e0, 4)) >> 3;
    if (!(jv > -1))
        jv = 0;
    int32_t q0 = (int32_t)((uint32_t)e0 - ((uint32_t)(jv + 1) << 3)); /* 0x11CE84 subu */
    if (jk < 0 || jk >= CE20_N)
        return fail(fault, here);

    /* 0x11CED0: f[0..jx+jk] = (float)ipio2[jv-jx+i], or 0 below the table. */
    {
        int32_t j = jv - jx;
        const int32_t m = jx + jk;
        for (int32_t i = 0; i <= m; ++i, ++j) {
            if (!IN_RANGE(i) || (j >= 0 && (size_t)j >= ipio2_count))
                return fail(fault, here);
            f[i] = j < 0 ? 0u : em_ee_cvt_s_w_bits(em_ee_int_word(ipio2[j]));            /* 0x11CEEC cvt.s.w */
        }
    }
    /* 0x11CF40: q[i] = sum x[j] * f[jx+i-j]. */
    for (int32_t i = 0; i <= jk; ++i) {
        F fw = 0u;
        for (int32_t j = 0; j <= jx; ++j) {
            if (!IN_RANGE(jx + i - j))
                return fail(fault, here);
            fw = em_ee_add_bits(fw, em_ee_mul_bits(x[j], f[jx + i - j]));       /* 0x11CF88 / 0x11CF90 */
        }
        q[i] = fw;
    }

    int32_t jz = jk, n, ih;
    F z;
recompute:                                                       /* 0x11CFBC */
    if (!IN_RANGE(jz))
        return fail(fault, here);
    z = q[jz];
    for (int32_t i = 0, j = jz; j > 0; ++i, --j) {               /* 0x11CFF0 */
        const F fw = em_ee_cvt_s_w_bits(em_ee_int_word(em_ee_word_int(em_ee_cvt_w_s_bits(em_ee_mul_bits(z, TWON8)))));   /* 0x11CFF0..0x11D00C */
        iq[i] = em_ee_word_int(em_ee_cvt_w_s_bits(em_ee_sub_bits(z, em_ee_mul_bits(fw, TWO8))));         /* 0x11D010 / 0x11D014 / 0x11D01C */
        z = em_ee_add_bits(q[j - 1], fw);                                /* 0x11D018 */
    }
    z = sdk_0011E148(z, q0);                                     /* 0x11D030 */
    z = em_ee_sub_bits(z, em_ee_mul_bits(sdk_0011DF98(em_ee_mul_bits(z, 0x3E000000u)), 0x41000000u)); /* 0x11D048..0x11D05C */
    n = em_ee_word_int(em_ee_cvt_w_s_bits(z));                                           /* 0x11D060 */
    z = em_ee_sub_bits(z, em_ee_cvt_s_w_bits(em_ee_int_word(n)));                                /* 0x11D06C / 0x11D074 */
    ih = 0;
    if (q0 > 0) {                                                /* 0x11D070 blez */
        if (!IN_RANGE(jz - 1))
            return fail(fault, here);
        const uint32_t sh = (8u - (uint32_t)q0) & 31u;
        const int32_t i = iq[jz - 1] >> sh;                      /* 0x11D098 srav */
        n = wadd(n, i);                                          /* 0x11D0A0 */
        iq[jz - 1] = (int32_t)((uint32_t)iq[jz - 1] - ((uint32_t)i << sh)); /* 0x11D09C / 0x11D0A4 */
        ih = iq[jz - 1] >> ((7u - (uint32_t)q0) & 31u);          /* 0x11D0A8 srav */
    } else if (q0 == 0) {
        if (!IN_RANGE(jz - 1))
            return fail(fault, here);
        ih = iq[jz - 1] >> 8;                                    /* 0x11D0CC */
    } else if (em_ee_c_le_bits(HALF, z)) {                               /* 0x11D0E0 */
        ih = 2;
    }
    if (ih > 0) {                                                /* 0x11D0F8 */
        int32_t carry = 0;
        n = wadd(n, 1);
        for (int32_t i = 0; i < jz; ++i) {                       /* 0x11D120 */
            const int32_t j = iq[i];
            if (carry == 0) {
                if (j != 0) {
                    carry = 1;
                    iq[i] = (int32_t)(0x100u - (uint32_t)j);
                }
            } else {
                iq[i] = (int32_t)(0xFFu - (uint32_t)j);
            }
        }
        if (q0 > 0) {                                            /* 0x11D14C */
            if (q0 == 1)
                iq[jz - 1] &= 0x7F;
            else if (q0 == 2)
                iq[jz - 1] &= 0x3F;
        }
        if (ih == 2) {                                           /* 0x11D1A4 */
            z = em_ee_sub_bits(ONE, z);                                  /* 0x11D1B8 */
            if (carry != 0)
                z = em_ee_sub_bits(z, sdk_0011E148(ONE, q0));            /* 0x11D1BC / 0x11D1C4 */
        }
    }

    if (em_ee_c_eq_bits(z, 0u)) {                                        /* 0x11D1CC */
        int32_t j = 0;
        for (int32_t i = jz - 1; i >= jk; --i)                   /* 0x11D1F0 */
            j |= iq[i];
        if (j == 0) {
            int32_t k = 1;
            for (;;) {                                           /* 0x11D21C / 0x11D230 */
                if (!IN_RANGE(jk - k))
                    return fail(fault, here);
                if (iq[jk - k] != 0)
                    break;
                ++k;
            }
            for (int32_t i = jz + 1; i <= jz + k; ++i) {         /* 0x11D278 */
                if (!IN_RANGE(jx + i) || !IN_RANGE(i) || jv + i < 0 ||
                    (size_t)(jv + i) >= ipio2_count)
                    return fail(fault, here);
                f[jx + i] = em_ee_cvt_s_w_bits(em_ee_int_word(ipio2[jv + i]));           /* 0x11D294 */
                F fw = 0u;
                for (int32_t jj = 0; jj <= jx; ++jj)
                    fw = em_ee_add_bits(fw, em_ee_mul_bits(x[jj], f[jx + i - jj])); /* 0x11D2E0 / 0x11D2E8 */
                q[i] = fw;
            }
            jz += k;
            goto recompute;
        }
    }

    const int32_t n7 = n & 7;                                    /* n & 7 */
    if (em_ee_c_eq_bits(z, 0u)) {                                        /* 0x11D324 */
        jz -= 1;
        q0 = wadd(q0, -8);
        for (;;) {                                               /* 0x11D338 / 0x11D348 */
            if (!IN_RANGE(jz))
                return fail(fault, here);
            if (iq[jz] != 0)
                break;
            jz -= 1;
            q0 = wadd(q0, -8);
        }
    } else {
        z = sdk_0011E148(z, (int32_t)(0u - (uint32_t)q0));       /* 0x11D36C negu */
        if (em_ee_c_le_bits(TWO8, z)) {                                  /* 0x11D380 */
            if (!IN_RANGE(jz + 1))
                return fail(fault, here);
            const F fw = em_ee_cvt_s_w_bits(em_ee_int_word(em_ee_word_int(em_ee_cvt_w_s_bits(em_ee_mul_bits(z, TWON8))))); /* 0x11D3A0..0x11D3B8 */
            iq[jz] = em_ee_word_int(em_ee_cvt_w_s_bits(em_ee_sub_bits(z, em_ee_mul_bits(fw, TWO8))));    /* 0x11D3BC..0x11D3C8 */
            jz += 1;
            q0 = wadd(q0, 8);
            iq[jz] = em_ee_word_int(em_ee_cvt_w_s_bits(fw));                             /* 0x11D3CC */
        } else {
            iq[jz] = em_ee_word_int(em_ee_cvt_w_s_bits(z));                              /* 0x11D3E0 */
        }
    }

    /* 0x11D3F4: q[i] = 2^q0 * iq[i] * 2^-8(jz-i). */
    F fw = sdk_0011E148(ONE, q0);
    for (int32_t i = jz; i >= 0; --i) {                          /* 0x11D428 */
        q[i] = em_ee_mul_bits(fw, em_ee_cvt_s_w_bits(em_ee_int_word(iq[i])));                    /* 0x11D42C / 0x11D438 */
        fw = em_ee_mul_bits(fw, TWON8);                                  /* 0x11D43C */
    }
    /* 0x11D460: fq[jz-i] = sum PIo2[k] * q[i+k], k <= jp and k <= jz - i. */
    for (int32_t i = jz; i >= 0; --i) {
        F sum = 0u;
        for (int32_t k = 0; k <= jp && k <= jz - i; ++k) {
            if (k >= 11)
                return fail(fault, here);
            sum = em_ee_add_bits(sum, em_ee_mul_bits(t->pio2[k], q[i + k]));     /* 0x11D4B0 / 0x11D4B8 */
        }
        fq[jz - i] = sum;
    }

    if (jz < 0)
        return fail(fault, here);                                /* fq[0] would be unwritten */
    switch (prec) {
    case 0: {                                                    /* 0x11D528 */
        F s = 0u;
        for (int32_t i = jz; i >= 0; --i)
            s = em_ee_add_bits(s, fq[i]);                                /* 0x11D550 */
        y[0] = ih == 0 ? s : em_ee_neg_bits(s);                          /* 0x11D55C / 0x11D56C */
        break;
    }
    case 1:
    case 2: {                                                    /* 0x11D578 */
        F s = 0u;
        for (int32_t i = jz; i >= 0; --i)
            s = em_ee_add_bits(s, fq[i]);                                /* 0x11D5A0 */
        y[0] = ih == 0 ? s : em_ee_neg_bits(s);                          /* 0x11D5B0 / 0x11D5C8 */
        s = em_ee_sub_bits(fq[0], s);                                    /* 0x11D5C4 / 0x11D5D0 */
        for (int32_t i = 1; i <= jz; ++i)
            s = em_ee_add_bits(s, fq[i]);                                /* 0x11D5F4 */
        y[1] = ih == 0 ? s : em_ee_neg_bits(s);                          /* 0x11D600 / 0x11D610 */
        break;
    }
    default: {                                                   /* 3: 0x11D61C */
        if (jz < 1)
            return fail(fault, here);                            /* fq[1] would be unwritten */
        for (int32_t i = jz; i > 0; --i) {                       /* 0x11D630 */
            const F s = em_ee_add_bits(fq[i - 1], fq[i]);
            fq[i] = em_ee_add_bits(fq[i], em_ee_sub_bits(fq[i - 1], s));
            fq[i - 1] = s;
        }
        for (int32_t i = jz; i > 1; --i) {                       /* 0x11D680 */
            const F s = em_ee_add_bits(fq[i - 1], fq[i]);
            fq[i] = em_ee_add_bits(fq[i], em_ee_sub_bits(fq[i - 1], s));
            fq[i - 1] = s;
        }
        F s = 0u;
        for (int32_t i = jz; i >= 2; --i)
            s = em_ee_add_bits(s, fq[i]);                                /* 0x11D6EC */
        if (ih == 0) {
            y[0] = fq[0];
            y[1] = fq[1];
            y[2] = s;
        } else {
            y[0] = em_ee_neg_bits(fq[0]);
            y[1] = em_ee_neg_bits(fq[1]);
            y[2] = em_ee_neg_bits(s);
        }
        break;
    }
    }
    *out_n = n7;
    return 0;
}

/* 0011C7B0(x, y): argument reduction by pi/2. */
static int sdk_0011C7B0(const EmSdkMathTables *t, F x, F y[2], int32_t *out_n, uint32_t *fault)
{
    const int32_t hx = (int32_t)x;
    const int32_t ix = hx & 0x7FFFFFFF;
    if (!(ix > 0x3F490FD8)) {                                    /* 0x11C7E0 */
        y[0] = x;
        y[1] = 0u;
        *out_n = 0;
        return 0;
    }
    if (!(ix > 0x4016CBE3)) {                                    /* 0x11C808: n = +-1 */
        F z;
        if (hx > 0) {                                            /* 0x11C814 blez */
            z = em_ee_sub_bits(x, 0x3FC90F80u);                          /* 0x11C83C pio2_1 */
            if (((uint32_t)ix & 0xFFFFFFF0u) != 0x3FC90FD0u) {
                y[0] = em_ee_sub_bits(z, 0x37354443u);                   /* 0x11C850 pio2_1t */
                y[1] = em_ee_sub_bits(em_ee_sub_bits(z, y[0]), 0x37354443u);     /* 0x11C874 / 0x11C87C */
            } else {
                z = em_ee_sub_bits(z, 0x37354400u);                      /* 0x11C86C pio2_2 */
                y[0] = em_ee_sub_bits(z, 0x2E85A308u);                   /* 0x11C870 pio2_2t */
                y[1] = em_ee_sub_bits(em_ee_sub_bits(z, y[0]), 0x2E85A308u);
            }
            *out_n = 1;
        } else {
            z = em_ee_add_bits(x, 0x3FC90F80u);                          /* 0x11C8AC */
            if (((uint32_t)ix & 0xFFFFFFF0u) != 0x3FC90FD0u) {
                y[0] = em_ee_add_bits(z, 0x37354443u);                   /* 0x11C8C0 */
                y[1] = em_ee_add_bits(em_ee_sub_bits(z, y[0]), 0x37354443u);     /* 0x11C8E4 / 0x11C8EC */
            } else {
                z = em_ee_add_bits(z, 0x37354400u);                      /* 0x11C8DC */
                y[0] = em_ee_add_bits(z, 0x2E85A308u);                   /* 0x11C8E0 */
                y[1] = em_ee_add_bits(em_ee_sub_bits(z, y[0]), 0x2E85A308u);
            }
            *out_n = -1;
        }
        return 0;
    }
    if (!(ix > 0x43490F80)) {                                    /* 0x11C900: medium */
        const F tt = sdk_0011DF78(x);                            /* 0x11C90C */
        const int32_t n = em_ee_word_int(em_ee_cvt_w_s_bits(em_ee_add_bits(em_ee_mul_bits(tt, 0x3F22F984u), HALF))); /* 0x11C92C..0x11C94C */
        const F fn = em_ee_cvt_s_w_bits(em_ee_int_word(n));                              /* 0x11C958 */
        F r = em_ee_sub_bits(tt, em_ee_mul_bits(fn, 0x3FC90F80u));               /* 0x11C960 / 0x11C96C */
        F w = em_ee_mul_bits(fn, 0x37354443u);                           /* 0x11C964 */
        F y0;
        int quick = 0;
        if (n < 32) {                                            /* 0x11C95C: n < 32 */
            if (!t)
                return fail(fault, 0x0011C98Cu);
            if (n - 1 < 0)
                return fail(fault, 0x0011C98Cu);                 /* below D_0026C490 */
            quick = (F)(ix & (int32_t)0xFFFFFF00) != t->npio2_hw[n - 1]; /* 0x11C98C..0x11C994 */
        }
        y0 = em_ee_sub_bits(r, w);                                       /* 0x11C998 / 0x11C9A4 */
        if (!quick) {
            const int32_t j = ix >> 23;                          /* 0x11C9A8 */
            int32_t i = j - (int32_t)((y0 >> 23) & 0xFFu);       /* 0x11C9B4..0x11C9BC */
            if (!(i < 9)) {                                      /* 0x11C9C0: second step */
                const F tt2 = r;
                w = em_ee_mul_bits(fn, 0x37354400u);                     /* 0x11C9E8 pio2_2 */
                const F lo = em_ee_mul_bits(fn, 0x2E85A308u);            /* 0x11C9EC pio2_2t */
                r = em_ee_sub_bits(tt2, w);                              /* 0x11C9F0 */
                w = em_ee_sub_bits(lo, em_ee_sub_bits(em_ee_sub_bits(tt2, r), w));       /* 0x11C9F4..0x11C9FC */
                y0 = em_ee_sub_bits(r, w);                               /* 0x11CA00 */
                i = j - (int32_t)((y0 >> 23) & 0xFFu);
                if (!(i < 26)) {                                 /* 0x11CA18: third step */
                    const F tt3 = r;
                    w = em_ee_mul_bits(fn, 0x2E85A300u);                 /* 0x11CA40 pio2_3 */
                    const F lo3 = em_ee_mul_bits(fn, 0x248D3132u);       /* 0x11CA44 pio2_3t */
                    r = em_ee_sub_bits(tt3, w);                          /* 0x11CA48 */
                    w = em_ee_sub_bits(lo3, em_ee_sub_bits(em_ee_sub_bits(tt3, r), w));  /* 0x11CA4C..0x11CA54 */
                    y0 = em_ee_sub_bits(r, w);                           /* 0x11CA58 */
                }
            }
        }
        F y1 = em_ee_sub_bits(em_ee_sub_bits(r, y0), w);                         /* 0x11CA64 / 0x11CA68 */
        if (hx < 0) {                                            /* 0x11CA6C bgez */
            y[0] = em_ee_neg_bits(y0);                                   /* 0x11CA74 */
            y[1] = em_ee_neg_bits(y1);                                   /* 0x11CB64 */
            *out_n = (int32_t)(0u - (uint32_t)n);
        } else {
            y[0] = y0;
            y[1] = y1;
            *out_n = n;
        }
        return 0;
    }
    if (ix > 0x7F7FFFFF) {                                       /* 0x11CA84: Inf / NaN */
        y[0] = y[1] = em_ee_sub_bits(x, x);                              /* 0x11CA90 */
        *out_n = 0;
        return 0;
    }
    /* 0x11CAA0: z = |x| scaled to [2^7, 2^8); three 8-bit chunks. */
    const int32_t e0 = (ix >> 23) - 0x86;
    F z = (F)ix - ((uint32_t)e0 << 23);
    F tx[3];
    for (int i = 0; i < 2; ++i) {                                /* 0x11CAC8 */
        tx[i] = em_ee_cvt_s_w_bits(em_ee_int_word(em_ee_word_int(em_ee_cvt_w_s_bits(z))));
        z = em_ee_mul_bits(em_ee_sub_bits(z, tx[i]), TWO8);                      /* 0x11CADC / 0x11CAEC */
    }
    tx[2] = z;
    int32_t nx = 3;
    while (em_ee_c_eq_bits(tx[nx - 1], 0u)) {                            /* 0x11CAF8 / 0x11CB24 */
        if (--nx == 0)
            return fail(fault, 0x0011CB20u);                     /* would read below tx */
    }
    if (!t)
        return fail(fault, 0x0011CEA0u);
    int32_t n;
    if (sdk_0011CE20(t, tx, y, e0, nx, 2, t->two_over_pi, EM_SDK_MATH_TWO_OVER_PI_COUNT, &n,
                     fault) < 0)
        return -1;                                               /* 0x11CB44 */
    if (hx < 0) {                                                /* 0x11CB4C */
        y[0] = em_ee_neg_bits(y[0]);
        y[1] = em_ee_neg_bits(y[1]);
        n = (int32_t)(0u - (uint32_t)n);
    }
    *out_n = n;
    return 0;
}

/* 0011E2A8 (sinf). */
static int sdk_0011E2A8(const EmSdkMathTables *t, F x, F *out, uint32_t *fault)
{
    const int32_t ix = (int32_t)(x & 0x7FFFFFFFu);
    if (!(ix > 0x3F490FD8)) {                                    /* 0x11E2C8 */
        *out = sdk_0011D770(x, 0u, 0);                           /* 0x11E2D8 */
        return 0;
    }
    if (ix > 0x7F7FFFFF) {                                       /* 0x11E2F0 */
        *out = em_ee_sub_bits(x, x);                                     /* 0x11E300 */
        return 0;
    }
    F y[2];
    int32_t n;
    if (sdk_0011C7B0(t, x, y, &n, fault) < 0)                    /* 0x11E304 */
        return -1;
    switch (n & 3) {                                             /* 0x11E30C */
    case 0: *out = sdk_0011D770(y[0], y[1], 1); break;           /* 0x11E348 */
    case 1: *out = sdk_0011CCC8(y[0], y[1]); break;              /* 0x11E35C */
    case 2: *out = em_ee_neg_bits(sdk_0011D770(y[0], y[1], 1)); break;   /* 0x11E370 / 0x11E37C */
    default: *out = em_ee_neg_bits(sdk_0011CCC8(y[0], y[1])); break;     /* 0x11E380 / 0x11E388 */
    }
    return 0;
}

/* 0011DE90 (cosf). */
static int sdk_0011DE90(const EmSdkMathTables *t, F x, F *out, uint32_t *fault)
{
    const int32_t ix = (int32_t)(x & 0x7FFFFFFFu);
    if (!(ix > 0x3F490FD8)) {                                    /* 0x11DEB0 */
        *out = sdk_0011CCC8(x, 0u);                              /* 0x11DEC0 */
        return 0;
    }
    if (ix > 0x7F7FFFFF) {                                       /* 0x11DED8 */
        *out = em_ee_sub_bits(x, x);                                     /* 0x11DEE8 */
        return 0;
    }
    F y[2];
    int32_t n;
    if (sdk_0011C7B0(t, x, y, &n, fault) < 0)                    /* 0x11DEEC */
        return -1;
    switch (n & 3) {                                             /* 0x11DEF4 */
    case 0: *out = sdk_0011CCC8(y[0], y[1]); break;              /* 0x11DF2C */
    case 1: *out = em_ee_neg_bits(sdk_0011D770(y[0], y[1], 1)); break;   /* 0x11DF44 / 0x11DF50 */
    case 2: *out = em_ee_neg_bits(sdk_0011CCC8(y[0], y[1])); break;      /* 0x11DF54 / 0x11DF60 */
    default: *out = sdk_0011D770(y[0], y[1], 1); break;          /* 0x11DF64 */
    }
    return 0;
}

/* 0011E398 (tanf). */
static int sdk_0011E398(const EmSdkMathTables *t, F x, F *out, uint32_t *fault)
{
    const int32_t ix = (int32_t)(x & 0x7FFFFFFFu);
    if (!(ix > 0x3F490FDA))                                      /* 0x11E3B8 */
        return sdk_0011D878(t, x, 0u, 1, out, fault);            /* 0x11E3C4 / 0x11E40C */
    if (ix > 0x7F7FFFFF) {                                       /* 0x11E3D8 */
        *out = em_ee_sub_bits(x, x);                                     /* 0x11E3E8 */
        return 0;
    }
    F y[2];
    int32_t n;
    if (sdk_0011C7B0(t, x, y, &n, fault) < 0)                    /* 0x11E3EC */
        return -1;
    return sdk_0011D878(t, y[0], y[1], 1 - (int32_t)(((uint32_t)n & 1u) << 1), out, fault); /* 0x11E3F4..0x11E40C */
}

/* 0011DBB8 (atanf). */
static int sdk_0011DBB8(const EmSdkMathTables *t, F x, F *out, uint32_t *fault)
{
    const int32_t hx = (int32_t)x;
    const int32_t ix = hx & 0x7FFFFFFF;
    int32_t id;
    if (ix > 0x507FFFFF) {                                       /* 0x11DBE4 */
        if (ix > 0x7F800000) {                                   /* 0x11DBF0: NaN */
            *out = em_ee_add_bits(x, x);                                 /* 0x11DC00 */
            return 0;
        }
        if (!t)
            return fail(fault, 0x0011DC14u);
        if (hx > 0)                                              /* 0x11DC08 blez */
            *out = em_ee_add_bits(t->atan_hi[3], t->atan_lo[3]);         /* 0x11DC20 */
        else
            *out = em_ee_sub_bits(em_ee_neg_bits(t->atan_hi[3]), t->atan_lo[3]); /* 0x11DC30 / 0x11DC38 */
        return 0;
    }
    if (!(ix > 0x3EDFFFFF)) {                                    /* 0x11DC44 */
        if (!(ix > 0x30FFFFFF)) {                                /* 0x11DC54 */
            /* 0x11DC74 add.s x + 1e30; 0x11DC78 c.lt.s 1.0 < sum returns x. */
            if (em_ee_c_lt_bits(ONE, em_ee_add_bits(x, HUGE_))) {
                *out = x;                                        /* 0x11DC8C */
                return 0;
            }
        }
        id = -1;                                                 /* 0x11DC5C */
    } else {
        x = sdk_0011DF78(x);                                     /* 0x11DC90 */
        if (!(ix > 0x3F97FFFF)) {                                /* 0x11DCA0 */
            if (!(ix > 0x3F2FFFFF)) {                            /* 0x11DCB4 */
                id = 0;
                x = em_ee_div_bits(em_ee_sub_bits(em_ee_add_bits(x, x), ONE), em_ee_add_bits(x, 0x40000000u)); /* 0x11DCC0..0x11DCE4 */
            } else {
                id = 1;
                x = em_ee_div_bits(em_ee_sub_bits(x, ONE), em_ee_add_bits(x, ONE));      /* 0x11DCF8..0x11DD08 */
            }
        } else if (!(ix > 0x401BFFFF)) {                         /* 0x11DD1C */
            id = 2;
            x = em_ee_div_bits(em_ee_sub_bits(x, 0x3FC00000u),                   /* 0x11DD3C */
                       em_ee_add_bits(em_ee_mul_bits(x, 0x3FC00000u), ONE));     /* 0x11DD38 / 0x11DD40 / 0x11DD4C */
        } else {
            id = 3;
            x = em_ee_div_bits(0xBF800000u, x);                          /* 0x11DD68 */
        }
    }
    if (!t)
        return fail(fault, 0x0011DD78u);
    const uint32_t *aT = t->atan_t;
    const F z = em_ee_mul_bits(x, x);                                    /* 0x11DD6C / 0x11DD70 */
    const F w = em_ee_mul_bits(z, z);                                    /* 0x11DD80 */
    F odd = em_ee_add_bits(aT[8], em_ee_mul_bits(w, aT[10]));                    /* 0x11DD90 / 0x11DDA4 */
    F even = em_ee_add_bits(aT[7], em_ee_mul_bits(w, aT[9]));                    /* 0x11DD98 / 0x11DDAC */
    odd = em_ee_add_bits(aT[6], em_ee_mul_bits(w, odd));                         /* 0x11DDB8 / 0x11DDC0 */
    even = em_ee_add_bits(aT[5], em_ee_mul_bits(w, even));                       /* 0x11DDBC / 0x11DDC4 */
    odd = em_ee_add_bits(aT[4], em_ee_mul_bits(w, odd));                         /* 0x11DDC8 / 0x11DDD0 */
    even = em_ee_add_bits(aT[3], em_ee_mul_bits(w, even));                       /* 0x11DDCC / 0x11DDD4 */
    odd = em_ee_add_bits(aT[2], em_ee_mul_bits(w, odd));                         /* 0x11DDD8 / 0x11DDE0 */
    even = em_ee_add_bits(aT[1], em_ee_mul_bits(w, even));                       /* 0x11DDDC / 0x11DDE4 */
    odd = em_ee_add_bits(aT[0], em_ee_mul_bits(w, odd));                         /* 0x11DDE8 / 0x11DDF0 */
    const F s2 = em_ee_mul_bits(w, even);                                /* 0x11DDEC */
    const F s1 = em_ee_mul_bits(z, odd);                                 /* 0x11DDF8 */
    if (id < 0) {                                                /* 0x11DDF4 bgez */
        *out = em_ee_sub_bits(x, em_ee_mul_bits(x, em_ee_add_bits(s1, s2)));             /* 0x11DDFC..0x11DE08 */
        return 0;
    }
    F r = em_ee_mul_bits(x, em_ee_add_bits(s1, s2));                             /* 0x11DE0C / 0x11DE24 */
    r = em_ee_sub_bits(r, t->atan_lo[id]);                               /* 0x11DE34 */
    r = em_ee_sub_bits(r, x);                                            /* 0x11DE3C */
    r = em_ee_sub_bits(t->atan_hi[id], r);                               /* 0x11DE44 */
    *out = hx < 0 ? em_ee_neg_bits(r) : r;                               /* 0x11DE40 bgez / 0x11DE48 */
    return 0;
}

/* 0011C4C8(y, x): the atan2f kernel. */
static int sdk_0011C4C8(const EmSdkMathTables *t, F y, F x, F *out, uint32_t *fault)
{
    const int32_t hx = (int32_t)x, ix = hx & 0x7FFFFFFF;
    const int32_t hy = (int32_t)y, iy = hy & 0x7FFFFFFF;
    if (ix > 0x7F800000 || iy > 0x7F800000) {                   /* 0x11C4F8 / 0x11C504 */
        *out = em_ee_add_bits(x, y);                                     /* 0x11C518 */
        return 0;
    }
    if (hx == 0x3F800000)                                        /* 0x11C51C */
        return sdk_0011DBB8(t, y, out, fault);                   /* 0x11C524 */
    const int32_t m = (int32_t)(((uint32_t)hy >> 31) | ((uint32_t)(hx >> 30) & 2u)); /* 0x11C534..0x11C540 */
    if (iy == 0) {                                               /* 0x11C53C */
        if (m == 2) { *out = 0x40490FDAu; return 0; }            /* 0x11C688 */
        if (m == 3) { *out = 0xC0490FDAu; return 0; }            /* 0x11C69C */
        *out = y;                                                /* 0x11C568: m 0 / 1 */
        return 0;
    }
    if (ix == 0) {                                               /* 0x11C570 */
        *out = hy >= 0 ? 0x3FC90FDBu : 0xBFC90FDBu;              /* 0x11C578 / 0x11C58C */
        return 0;
    }
    if (ix == 0x7F800000) {                                      /* 0x11C5A0 */
        if (iy == 0x7F800000) {                                  /* 0x11C5A8 */
            static const F both[4] = {0x3F490FDBu, 0xBF490FDBu, 0x4016CBE4u, 0xC016CBE4u};
            *out = both[m];                                      /* 0x11C5E8..0x11C624 */
            return 0;
        }
        if (m == 1) {
            if (!t)
                return fail(fault, 0x0011C684u);
            *out = t->d26C170;                                   /* 0x11C684: the float D_0026C170 */
            return 0;
        }
        static const F finite[4] = {0u, 0u, 0x40490FDAu, 0xC0490FDAu};
        *out = finite[m];                                        /* 0x11C670 / 0x11C688 / 0x11C69C */
        return 0;
    }
    if (iy == 0x7F800000) {                                      /* 0x11C6B0 */
        *out = hy >= 0 ? 0x3FC90FDBu : 0xBFC90FDBu;              /* 0x11C578 */
        return 0;
    }
    const int32_t k = (int32_t)((uint32_t)iy - (uint32_t)ix) >> 23; /* 0x11C6B4 / 0x11C6B8 */
    F z;
    if (!(k < 0x3D)) {                                           /* 0x11C6BC */
        z = 0x3FC90FDCu;                                         /* 0x11C6C8 */
    } else if (hx < 0 && k < -0x3C) {                            /* 0x11C6D8 / 0x11C6DC */
        z = 0u;
    } else {
        const F ratio = sdk_0011DF78(em_ee_div_bits(y, x));              /* 0x11C6F4 / 0x11C6F8 */
        if (sdk_0011DBB8(t, ratio, &z, fault) < 0)               /* 0x11C700 */
            return -1;
    }
    switch (m) {                                                 /* 0x11C710 */
    case 0: *out = z; break;                                     /* 0x11C748 */
    case 1: *out = z ^ SIGN; break;                              /* 0x11C744 xor */
    case 2: *out = em_ee_sub_bits(0x40490FDAu, em_ee_sub_bits(z, 0x34222168u)); break;  /* 0x11C770 / 0x11C778 */
    default: *out = em_ee_sub_bits(em_ee_sub_bits(z, 0x34222168u), 0x40490FDAu); break; /* 0x11C798 / 0x11C79C */
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Wrappers: 0011E620 (atan2f) and 0011E748 (sqrtf).
 * ------------------------------------------------------------------------- */

static int call_00128350(const EmSdkMathWorkers *k, F x, uint64_t *out, uint32_t *fault)
{
    if (!k || !k->w_00128350 || k->w_00128350(k->context, em_ee_float(x), out) < 0)
        return fail(fault, 0x00128350u);
    return 0;
}

/* The shared tail: 0011E6D4..0x11E71C (atan2f) / 0x11E7E0..0x11E830 (sqrtf). */
static int domain_tail(const EmSdkMathWorkers *k, int32_t mode, EmSdkMathException *exc, F *out,
                       uint32_t *fault)
{
    int set_edom = mode == 2;
    if (!set_edom) {
        int32_t r;
        if (!k || !k->w_0011DB90 || k->w_0011DB90(k->context, exc, &r) < 0)
            return fail(fault, 0x0011DB90u);
        set_edom = r == 0;                                       /* the branch on r */
    }
    if (set_edom) {
        int32_t *cell = NULL;
        if (!k || !k->w_0011FD78 || k->w_0011FD78(k->context, &cell) < 0 || !cell)
            return fail(fault, 0x0011FD78u);
        *cell = 0x21;                                            /* stores 0x21 */
    }
    if (exc->err != 0) {                                         /* the word exc +0x20 */
        int32_t *cell = NULL;
        if (!k || !k->w_0011FD78 || k->w_0011FD78(k->context, &cell) < 0 || !cell)
            return fail(fault, 0x0011FD78u);
        *cell = exc->err;
    }
    float value;
    if (!k || !k->w_00127758 || k->w_00127758(k->context, exc->retval, &value) < 0)
        return fail(fault, 0x00127758u);
    *out = em_ee_bits(value);
    return 0;
}

static int sdk_0011E620(const EmSdkMathTables *t, const EmSdkMathWorld *world,
                        const EmSdkMathWorkers *k, F y, F x, F *out, uint32_t *fault)
{
    F saved;
    if (sdk_0011C4C8(t, y, x, &saved, fault) < 0)                /* 0x11E63C */
        return -1;
    if (!world || !world->d26C5D0)
        return fail(fault, 0x0011E648u);
    const int32_t mode = *world->d26C5D0;                        /* 0x11E648 */
    if (mode == -1 || sdk_0011E080(x) != 0 || sdk_0011E080(y) != 0 ||  /* 0x11E650..0x11E670 */
        !em_ee_c_eq_bits(x, 0u) || !em_ee_c_eq_bits(y, 0u)) {                    /* 0x11E67C / 0x11E68C */
        *out = saved;
        return 0;
    }
    EmSdkMathException exc;
    memset(&exc, 0, sizeof exc);
    if (call_00128350(k, y, &exc.arg1, fault) < 0)               /* 0x11E69C */
        return -1;
    if (call_00128350(k, x, &exc.arg2, fault) < 0)               /* 0x11E6A8 */
        return -1;
    exc.retval = 0;                                              /* 0x11E6C4 */
    exc.type = 1;                                                /* 0x11E6C8 */
    exc.name = 0x0026C640u;                                      /* 0x11E6D0 */
    exc.err = 0;                                                 /* 0x11E6D8 */
    return domain_tail(k, mode, &exc, out, fault);
}

static int sdk_0011E748(const EmSdkMathTables *t, const EmSdkMathWorld *world,
                        const EmSdkMathWorkers *k, F x, F *out, uint32_t *fault)
{
    const F r = sdk_0011CB90(x);                                 /* 0x11E764 */
    if (!world || !world->d26C5D0)
        return fail(fault, 0x0011E76Cu);
    const int32_t mode = *world->d26C5D0;                        /* 0x11E76C */
    if (mode == -1 || sdk_0011E080(x) != 0 || !em_ee_c_lt_bits(x, 0u)) { /* 0x11E774 / 0x11E77C / 0x11E790 */
        *out = r;
        return 0;
    }
    EmSdkMathException exc;
    memset(&exc, 0, sizeof exc);
    exc.type = 1;                                                /* 0x11E7A8 */
    exc.name = 0x0026C648u;                                      /* 0x11E7AC */
    exc.err = 0;                                                 /* 0x11E7B8 */
    if (call_00128350(k, x, &exc.arg1, fault) < 0)               /* 0x11E7B4 */
        return -1;
    exc.arg2 = exc.arg1;                                         /* 0x11E7BC / 0x11E7C4 */
    if (mode == 0) {
        exc.retval = 0;                                          /* 0x11E7D0 */
    } else {
        if (!t)
            return fail(fault, 0x0011E7D8u);
        exc.retval = t->d26C650;                                 /* 0x11E7D8: the double D_0026C650 */
    }
    const int32_t again = *world->d26C5D0;                       /* 0x11E7E0: read again */
    return domain_tail(k, again, &exc, out, fault);
}

/* ---------------------------------------------------------------------------
 * Public entry points (float <-> word conversion is memcpy only).
 * ------------------------------------------------------------------------- */

float em_sdk_math_original_0011DF78(float x) { return em_ee_float(sdk_0011DF78(em_ee_bits(x))); }
int32_t em_sdk_math_original_0011E080(float x) { return sdk_0011E080(em_ee_bits(x)); }
float em_sdk_math_original_0011DE60(float x, float y) { return em_ee_float(sdk_0011DE60(em_ee_bits(x), em_ee_bits(y))); }
float em_sdk_math_original_0011DF98(float x) { return em_ee_float(sdk_0011DF98(em_ee_bits(x))); }
float em_sdk_math_original_0011E148(float x, int32_t n) { return em_ee_float(sdk_0011E148(em_ee_bits(x), n)); }
float em_sdk_math_original_0011D770(float x, float y, int32_t iy)
{
    return em_ee_float(sdk_0011D770(em_ee_bits(x), em_ee_bits(y), iy));
}
float em_sdk_math_original_0011CCC8(float x, float y) { return em_ee_float(sdk_0011CCC8(em_ee_bits(x), em_ee_bits(y))); }
float em_sdk_math_original_0011CB90(float x) { return em_ee_float(sdk_0011CB90(em_ee_bits(x))); }

int em_sdk_math_original_0011D878(const EmSdkMathTables *tables, float x, float y, int32_t iy,
                                  float *result, uint32_t *fault)
{
    F out;
    if (!result)
        return fail(fault, 0x0011D878u);
    if (sdk_0011D878(tables, em_ee_bits(x), em_ee_bits(y), iy, &out, fault) < 0)
        return -1;
    *result = em_ee_float(out);
    return 0;
}

int em_sdk_math_original_0011CE20(const EmSdkMathTables *tables, const float *x, float *y,
                                  int32_t e0, int32_t nx, int32_t prec, const int32_t *ipio2,
                                  size_t ipio2_count, int32_t *n, uint32_t *fault)
{
    F xs[CE20_N], ys[3] = {0u, 0u, 0u};
    if (!x || !y || !n || nx < 0 || nx > CE20_N)
        return fail(fault, 0x0011CE20u);
    for (int32_t i = 0; i < nx; ++i)
        xs[i] = em_ee_bits(x[i]);
    if (sdk_0011CE20(tables, xs, ys, e0, nx, prec, ipio2, ipio2_count, n, fault) < 0)
        return -1;
    const int words = prec == 0 ? 1 : prec == 3 ? 3 : 2;
    for (int i = 0; i < words; ++i)
        y[i] = em_ee_float(ys[i]);
    return 0;
}

int em_sdk_math_original_0011C7B0(const EmSdkMathTables *tables, float x, float y[2], int32_t *n,
                                  uint32_t *fault)
{
    F ys[2];
    if (!y || !n)
        return fail(fault, 0x0011C7B0u);
    if (sdk_0011C7B0(tables, em_ee_bits(x), ys, n, fault) < 0)
        return -1;
    y[0] = em_ee_float(ys[0]);
    y[1] = em_ee_float(ys[1]);
    return 0;
}

typedef int (*Unary)(const EmSdkMathTables *, F, F *, uint32_t *);

static int unary(Unary fn, uint32_t entry, const EmSdkMathTables *t, float x, float *result,
                 uint32_t *fault)
{
    F out;
    if (!result)
        return fail(fault, entry);
    if (fn(t, em_ee_bits(x), &out, fault) < 0)
        return -1;
    *result = em_ee_float(out);
    return 0;
}

int em_sdk_math_original_0011E2A8(const EmSdkMathTables *t, float x, float *result, uint32_t *fault)
{
    return unary(sdk_0011E2A8, 0x0011E2A8u, t, x, result, fault);
}

int em_sdk_math_original_0011DE90(const EmSdkMathTables *t, float x, float *result, uint32_t *fault)
{
    return unary(sdk_0011DE90, 0x0011DE90u, t, x, result, fault);
}

int em_sdk_math_original_0011E398(const EmSdkMathTables *t, float x, float *result, uint32_t *fault)
{
    return unary(sdk_0011E398, 0x0011E398u, t, x, result, fault);
}

int em_sdk_math_original_0011DBB8(const EmSdkMathTables *t, float x, float *result, uint32_t *fault)
{
    return unary(sdk_0011DBB8, 0x0011DBB8u, t, x, result, fault);
}

int em_sdk_math_original_0011C4C8(const EmSdkMathTables *t, float y, float x, float *result,
                                  uint32_t *fault)
{
    F out;
    if (!result)
        return fail(fault, 0x0011C4C8u);
    if (sdk_0011C4C8(t, em_ee_bits(y), em_ee_bits(x), &out, fault) < 0)
        return -1;
    *result = em_ee_float(out);
    return 0;
}

int em_sdk_math_original_0011E620(const EmSdkMathTables *t, const EmSdkMathWorld *world,
                                  const EmSdkMathWorkers *workers, float y, float x, float *result,
                                  uint32_t *fault)
{
    F out;
    if (!result)
        return fail(fault, 0x0011E620u);
    if (sdk_0011E620(t, world, workers, em_ee_bits(y), em_ee_bits(x), &out, fault) < 0)
        return -1;
    *result = em_ee_float(out);
    return 0;
}

int em_sdk_math_original_0011E748(const EmSdkMathTables *t, const EmSdkMathWorld *world,
                                  const EmSdkMathWorkers *workers, float x, float *result,
                                  uint32_t *fault)
{
    F out;
    if (!result)
        return fail(fault, 0x0011E748u);
    if (sdk_0011E748(t, world, workers, em_ee_bits(x), &out, fault) < 0)
        return -1;
    *result = em_ee_float(out);
    return 0;
}

/* ---- Worker adapters ---- */

static float record(EmSdkMathContext *c, uint32_t address)
{
    if (c && c->fault == 0)
        c->fault = address ? address : 0xFFFFFFFFu;
    return 0.0f;
}

static float adapt1(void *context, Unary fn, uint32_t entry, float x)
{
    EmSdkMathContext *c = context;
    F out;
    uint32_t fault = 0;
    if (!c)
        return record(c, entry);
    if (fn(c->tables, em_ee_bits(x), &out, &fault) < 0)
        return record(c, fault ? fault : entry);
    return em_ee_float(out);
}

float em_sdk_math_original_float_0011E2A8(void *context, float x)
{
    return adapt1(context, sdk_0011E2A8, 0x0011E2A8u, x);
}

float em_sdk_math_original_float_0011DE90(void *context, float x)
{
    return adapt1(context, sdk_0011DE90, 0x0011DE90u, x);
}

float em_sdk_math_original_float_0011E398(void *context, float x)
{
    return adapt1(context, sdk_0011E398, 0x0011E398u, x);
}

float em_sdk_math_original_float_0011DBB8(void *context, float x)
{
    return adapt1(context, sdk_0011DBB8, 0x0011DBB8u, x);
}

float em_sdk_math_original_float_0011E620(void *context, float y, float x)
{
    EmSdkMathContext *c = context;
    F out;
    uint32_t fault = 0;
    if (!c)
        return record(c, 0x0011E620u);
    if (sdk_0011E620(c->tables, &c->world, &c->workers, em_ee_bits(y), em_ee_bits(x), &out, &fault) < 0)
        return record(c, fault ? fault : 0x0011E620u);
    return em_ee_float(out);
}

float em_sdk_math_original_float_0011E748(void *context, float x)
{
    EmSdkMathContext *c = context;
    F out;
    uint32_t fault = 0;
    if (!c)
        return record(c, 0x0011E748u);
    if (sdk_0011E748(c->tables, &c->world, &c->workers, em_ee_bits(x), &out, &fault) < 0)
        return record(c, fault ? fault : 0x0011E748u);
    return em_ee_float(out);
}

static int adapt_w(void *context, Unary fn, uint32_t entry, float x, float *result)
{
    EmSdkMathContext *c = context;
    uint32_t fault = 0;
    if (!c)
        return -1;
    if (unary(fn, entry, c->tables, x, result, &fault) < 0) {
        record(c, fault ? fault : entry);
        return -1;
    }
    return 0;
}

int em_sdk_math_original_w_0011E2A8(void *context, float x, float *result)
{
    return adapt_w(context, sdk_0011E2A8, 0x0011E2A8u, x, result);
}

int em_sdk_math_original_w_0011DE90(void *context, float x, float *result)
{
    return adapt_w(context, sdk_0011DE90, 0x0011DE90u, x, result);
}

/* ---- Table loader ---- */

static uint32_t word_at(const uint8_t *elf, uint32_t address)
{
    const uint8_t *b = elf + (address - 0x00100000u + 0x300u);   /* file 0x300 = vram 0x00100000 */
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

int em_sdk_math_original_load_tables(const uint8_t *elf, size_t size, EmSdkMathTables *out)
{
    if (!elf || !out || size != EM_SDK_MATH_ELF_SIZE || memcmp(elf, "\x7F" "ELF", 4))
        return -1;
    EmSdkMathTables t;
    memset(&t, 0, sizeof t);
    t.d26C170 = word_at(elf, 0x0026C170u);
    for (int i = 0; i < EM_SDK_MATH_TWO_OVER_PI_COUNT; ++i)
        t.two_over_pi[i] = (int32_t)word_at(elf, 0x0026C178u + 4u * (uint32_t)i);
    for (int i = 0; i < 32; ++i)
        t.npio2_hw[i] = word_at(elf, 0x0026C490u + 4u * (uint32_t)i);
    for (int i = 0; i < 4; ++i)
        t.init_jk[i] = (int32_t)word_at(elf, 0x0026C538u + 4u * (uint32_t)i);
    for (int i = 0; i < 11; ++i)
        t.pio2[i] = word_at(elf, 0x0026C548u + 4u * (uint32_t)i);
    for (int i = 0; i < 13; ++i)
        t.tan_t[i] = word_at(elf, 0x0026C598u + 4u * (uint32_t)i);
    for (int i = 0; i < 4; ++i) {
        t.atan_hi[i] = word_at(elf, 0x0026C5D8u + 4u * (uint32_t)i);
        t.atan_lo[i] = word_at(elf, 0x0026C5E8u + 4u * (uint32_t)i);
    }
    for (int i = 0; i < 11; ++i)
        t.atan_t[i] = word_at(elf, 0x0026C5F8u + 4u * (uint32_t)i);
    t.d26C650 = (uint64_t)word_at(elf, 0x0026C650u) | (uint64_t)word_at(elf, 0x0026C654u) << 32;
    *out = t;
    return 0;
}
