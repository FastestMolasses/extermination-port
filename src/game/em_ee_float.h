#ifndef EM_EE_FLOAT_H
#define EM_EE_FLOAT_H
/*
 * Bit-exact EE FPU (COP1) and VU0 macro (COP2) arithmetic, as the ORIGINAL
 * computes it under the user's saved PCSX2 configuration.
 *
 * This is the C form of tools/ee_float_model.py; docs/EE_FLOAT_MODEL.md
 * states every rule and the measurement behind it (33,800 recorded original
 * results). tools/test_ee_float_header.py checks this header against every
 * recording and against the Python model. New translations of original code
 * use these helpers instead of inventing their own float arithmetic.
 *
 * Arithmetic is integer-only (significands in uint64_t). No host float
 * operation is performed, so the host rounding mode, FTZ/DAZ state and FMA
 * contraction cannot change a result.
 *
 * API
 *   Raw form (primary): functions ending in _bits take and return binary32
 *   bit patterns (uint32_t). They are defined for every input pattern.
 *   Float form: the same names without _bits take/return float, converting
 *   with memcpy. Out-parameters and arrays are copied with memcpy and keep
 *   every bit. A float *return value* keeps every bit on the SSE/NEON ABIs
 *   the port targets (x86-64, arm64); use the _bits form wherever a result
 *   may be a signalling NaN on a 32-bit x87 host.
 *   Integer words (CVT.W.S, CVT.S.W, VFTOI, VITOF) are uint32_t in the _bits
 *   form and int32_t in the float form.
 *
 * EE (COP1): em_ee_add/sub/mul/div, em_ee_madd(acc, fs, ft) = ACC + fs*ft,
 *   em_ee_msub(acc, fs, ft) = ACC - fs*ft, em_ee_adda/suba/mula (return the
 *   new ACC), em_ee_neg, em_ee_mov, em_ee_cvt_w_s, em_ee_cvt_s_w,
 *   em_ee_c_eq/c_lt/c_le (return the C bit, 0 or 1).
 *   Not provided because the model does not define them (the instructions
 *   occur neither in the boot ELF nor in any overlay, so nothing was
 *   measured): SQRT.S, RSQRT.S, ABS.S, MAX.S, MIN.S, MADDA.S, MSUBA.S.
 *
 * VU0 macro (COP2): operand clamping depends on the instruction FORM
 *   (op, dest mask with x=8 y=4 z=2 w=1, broadcast lane). Only the forms that
 *   occur in the original are defined; any other form is refused:
 *     em_vu_form_lookup()  resolves a form once (EM_EE_FLOAT_UNMEASURED if the
 *                          original never executes it);
 *     em_vu_form_lane_bits() one lane of a resolved form (cannot fail);
 *     em_vu_lane_bits()    lookup + one lane;
 *     em_vu_vec_bits()     a whole instruction on 4-lane registers:
 *                          broadcast, Q, the VOPMULA/VOPMSUB swizzle and the
 *                          dest mask (unwritten lanes of dst keep their value).
 *   em_vu_div_bits(fs.fsf, ft.ftf, fsf, ftf, &q) likewise refuses a
 *   (fsf, ftf) pair the original does not use. em_vu_sqrt, em_vu_ftoi0/4,
 *   em_vu_itof0/4, em_vu_max/min (VMAXbc/VMINIbc lane), em_vu_abs are total.
 *   Not provided (absent from the original): VRSQRT, VMSUB/VMSUBbc/VMSUBA,
 *   VMAX/VMINI (non-bc), VADDA/VSUBA/VMULA/VMADDA (non-bc), the other q
 *   forms, the I-register forms, VFTOI12/15, VITOF12/15.
 *
 *   Status codes: EM_EE_FLOAT_OK (0); EM_EE_FLOAT_UNMEASURED (-1) for an
 *   unmeasured form; EM_EE_FLOAT_NO_OPERAND (-2) when em_vu_vec_bits gets a
 *   NULL array the op reads. On any nonzero status nothing is written. A
 *   caller must treat a nonzero status as a fault (fail-stop), never guess.
 *
 * Not modelled: FCR31 flag bits, VU MAC/status flags, Q-pipeline timing,
 * VU1 (never measured).
 */
#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "em_ee_float.h needs IEEE binary32 float");

#define EM_EE_SIGN       UINT32_C(0x80000000)
#define EM_EE_MAX        UINT32_C(0x7F7FFFFF) /* every saturation lands here */
#define EM_EE_INF        UINT32_C(0x7F800000)
#define EM_EE_QNAN       UINT32_C(0x7FC00000) /* EE MADD/MSUB invalid product */
#define EM_EE_INDEFINITE UINT32_C(0xFFC00000) /* VU0 0*Inf, Inf-Inf */
#define EM_EE_ONE        UINT32_C(0x3F800000)

#define EM_EE_FLOAT_OK          0
#define EM_EE_FLOAT_UNMEASURED  (-1)
#define EM_EE_FLOAT_NO_OPERAND  (-2)

#define EM_VU_NO_BC (-1)  /* the bc argument of a non-broadcast form */

/* ------------------------------------------------------------ punning --- */

static inline uint32_t em_ee_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static inline float em_ee_float(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static inline int32_t em_ee_word_int(uint32_t word)
{
    int32_t value; /* int32_t is two's complement by definition */
    memcpy(&value, &word, sizeof value);
    return value;
}

static inline uint32_t em_ee_int_word(int32_t value)
{
    return (uint32_t)value;
}

/* ---------------------------------------------------------- internals --- */

static inline unsigned em_eei_exp(uint32_t b) { return (unsigned)(b >> 23) & 0xFFu; }
static inline int em_eei_is_nan(uint32_t b) { return em_eei_exp(b) == 0xFFu && (b & UINT32_C(0x7FFFFF)) != 0; }
static inline int em_eei_is_inf(uint32_t b) { return (b & UINT32_C(0x7FFFFFFF)) == EM_EE_INF; }
static inline int em_eei_is_zero(uint32_t b) { return em_eei_exp(b) == 0; } /* after DAZ */
static inline uint32_t em_eei_daz(uint32_t b) { return em_eei_exp(b) == 0 ? b & EM_EE_SIGN : b; }
static inline uint32_t em_eei_quiet(uint32_t b) { return b | UINT32_C(0x00400000); }

/* Instruction-result saturation: any NaN -> +MAX, +-Inf -> +-MAX. */
static inline uint32_t em_eei_saturate(uint32_t b)
{
    if (em_eei_is_nan(b)) return EM_EE_MAX;
    if (em_eei_is_inf(b)) return (b & EM_EE_SIGN) | EM_EE_MAX;
    return b;
}

/* NEG.S, the compares, VSQRT: every exponent-255 pattern -> +-MAX by sign. */
static inline uint32_t em_eei_saturate_signed(uint32_t b)
{
    return em_eei_exp(b) == 0xFFu ? (b & EM_EE_SIGN) | EM_EE_MAX : b;
}

/* Finite DAZ-applied operand: value = sig * 2**uexp (sig 0 for zeros). */
static inline uint32_t em_eei_sig(uint32_t b)
{
    return em_eei_exp(b) == 0 ? 0 : (b & UINT32_C(0x7FFFFF)) | UINT32_C(0x800000);
}

static inline int em_eei_uexp(uint32_t b)
{
    return em_eei_exp(b) == 0 ? 0 : (int)em_eei_exp(b) - 150;
}

static inline int em_eei_bitlen64(uint64_t v)
{
    int n = 0;
    if (v >> 32) { v >>= 32; n += 32; }
    if (v >> 16) { v >>= 16; n += 16; }
    if (v >> 8) { v >>= 8; n += 8; }
    if (v >> 4) { v >>= 4; n += 4; }
    if (v >> 2) { v >>= 2; n += 2; }
    if (v >> 1) { v >>= 1; n += 1; }
    return n + (int)v;
}

/* Round sign * mag * 2**exp (mag > 0) to binary32. nearest = 0 truncates,
 * nearest = 1 rounds to nearest-even; inexact_below marks a nonzero
 * remainder below mag's last bit. A rounded magnitude below the smallest
 * normal flushes to a signed zero; one above the largest finite saturates. */
static inline uint32_t em_eei_pack(uint32_t sign, uint64_t mag, int exp, int nearest, int inexact_below)
{
    int shift = em_eei_bitlen64(mag) - 24;
    uint64_t kept;
    if (shift > 0) {
        kept = mag >> shift;
        if (nearest) {
            uint64_t rest = mag & ((UINT64_C(1) << shift) - 1);
            uint64_t half = UINT64_C(1) << (shift - 1);
            if (rest > half || (rest == half && (inexact_below || (kept & 1))))
                kept++;
        }
    } else {
        kept = mag << -shift;
    }
    exp += shift;
    if (kept >> 24) {
        kept >>= 1;
        exp++;
    }
    int biased = exp + 150;
    if (biased <= 0) return sign << 31;
    if (biased >= 0xFF) return (sign << 31) | EM_EE_MAX;
    return (sign << 31) | ((uint32_t)biased << 23) | ((uint32_t)kept & UINT32_C(0x7FFFFF));
}

/* Finite DAZ-applied a + b, truncated. The larger operand's significand
 * sits at bits 39..62 and the smaller one is aligned exactly below it while
 * the exponent distance is at most 39. Beyond that the smaller magnitude is
 * nonzero and below 2**23 in these units: it can never carry into the kept
 * 24 bits (which start at bit 38 or higher), and against an opposite sign it
 * always borrows exactly one unit from them. Every value in (0, 2**38] has
 * that same effect, so it is replaced by 1 and the truncated result is
 * exactly the model's big-integer sum. */
static inline uint32_t em_eei_exact_sum(uint32_t a, uint32_t b)
{
    uint32_t ma = em_eei_sig(a), mb = em_eei_sig(b);
    uint32_t sa = a >> 31, sb = b >> 31;
    if (ma == 0 && mb == 0) return (sa && sb) ? EM_EE_SIGN : 0; /* -0 only for (-0)+(-0) */
    if (ma == 0) return b;
    if (mb == 0) return a;
    int ea = em_eei_uexp(a), eb = em_eei_uexp(b);
    if (ea < eb) {
        uint32_t tm = ma, ts = sa;
        int te = ea;
        ma = mb; sa = sb; ea = eb;
        mb = tm; sb = ts; eb = te;
    }
    unsigned d = (unsigned)(ea - eb);
    uint64_t x = (uint64_t)ma << 39;
    uint64_t y = d <= 39 ? (uint64_t)mb << (39 - d) : 1;
    if (sa == sb) return em_eei_pack(sa, x + y, ea - 39, 0, 0);
    if (x == y) return 0;
    if (x > y) return em_eei_pack(sa, x - y, ea - 39, 0, 0);
    return em_eei_pack(sb, y - x, ea - 39, 0, 0);
}

/* Finite DAZ-applied a * b, truncated. */
static inline uint32_t em_eei_exact_product(uint32_t a, uint32_t b)
{
    uint32_t ma = em_eei_sig(a), mb = em_eei_sig(b), sign = (a ^ b) >> 31;
    if (ma == 0 || mb == 0) return sign << 31;
    return em_eei_pack(sign, (uint64_t)ma * mb, em_eei_uexp(a) + em_eei_uexp(b), 0, 0);
}

/* Finite DAZ-applied a / b, b nonzero. The quotient carries at least 39
 * bits and the remainder becomes the sticky bit, which decides truncation
 * and nearest-even exactly as the model's 50-bit quotient does. */
static inline uint32_t em_eei_quotient(uint32_t a, uint32_t b, int nearest)
{
    uint32_t ma = em_eei_sig(a), mb = em_eei_sig(b), sign = (a ^ b) >> 31;
    if (ma == 0) return sign << 31;
    uint64_t n = (uint64_t)ma << 39;
    return em_eei_pack(sign, n / mb, em_eei_uexp(a) - em_eei_uexp(b) - 39, nearest, n % mb != 0);
}

/* ADD/SUB pre-trim of the operand with the smaller exponent field (d >= 1):
 * clear its low d-1 bits; at d >= 25 it becomes a signed zero. */
static inline uint32_t em_eei_trim(uint32_t b, unsigned d)
{
    return d >= 25 ? b & EM_EE_SIGN : b & (UINT32_MAX << (d - 1));
}

/* EE a + b for DAZ-applied operands (Inf/NaN may occur): pre-trim,
 * truncation, result saturation. */
static inline uint32_t em_eei_ee_sum(uint32_t a, uint32_t b)
{
    if (em_eei_exp(a) == 0xFFu || em_eei_exp(b) == 0xFFu) {
        if (em_eei_is_nan(a) || em_eei_is_nan(b)) return EM_EE_MAX;
        if (em_eei_is_inf(a) && em_eei_is_inf(b))
            return ((a ^ b) & EM_EE_SIGN) ? EM_EE_MAX : (a & EM_EE_SIGN) | EM_EE_MAX;
        return ((em_eei_is_inf(a) ? a : b) & EM_EE_SIGN) | EM_EE_MAX;
    }
    int d = (int)em_eei_exp(a) - (int)em_eei_exp(b);
    if (d > 0) b = em_eei_trim(b, (unsigned)d);
    else if (d < 0) a = em_eei_trim(a, (unsigned)-d);
    return em_eei_exact_sum(a, b);
}

/* EE fs * ft truncated but not saturated (Inf / QNAN pass through). */
static inline uint32_t em_eei_ee_raw_product(uint32_t a, uint32_t b)
{
    a = em_eei_daz(a);
    b = em_eei_daz(b);
    if (em_eei_is_nan(a) || em_eei_is_nan(b)) return EM_EE_QNAN;
    if (em_eei_is_inf(a) || em_eei_is_inf(b)) {
        if (em_eei_is_zero(a) || em_eei_is_zero(b)) return EM_EE_QNAN;
        return ((a ^ b) & EM_EE_SIGN) | EM_EE_INF;
    }
    return em_eei_exact_product(a, b);
}

/* Truncate finite a * 2**frac toward zero (caller has excluded
 * exp + frac >= 158, so the magnitude is below 2**31). */
static inline uint32_t em_eei_trunc_word(uint32_t a, int frac)
{
    uint32_t m = em_eei_sig(a), v;
    if (m == 0) return 0;
    int x = em_eei_uexp(a) + frac;
    if (x >= 0) v = m << x;
    else if (x <= -24) v = 0;
    else v = m >> -x;
    return (a & EM_EE_SIGN) ? 0u - v : v;
}

/* Signed word * 2**exp to float, truncated. */
static inline uint32_t em_eei_word_float(uint32_t w, int exp)
{
    if (w == 0) return 0;
    uint32_t sign = w >> 31;
    uint64_t mag = sign ? (UINT64_C(1) << 32) - w : w;
    return em_eei_pack(sign, mag, exp, 0, 0);
}

static inline int64_t em_eei_compare_key(uint32_t b)
{
    b = em_eei_saturate_signed(em_eei_daz(b));
    return (b & EM_EE_SIGN) ? -(int64_t)(b & UINT32_C(0x7FFFFFFF)) : (int64_t)b;
}

/* ------------------------------------------------------------ EE COP1 --- */

/* ADD.S: fs + ft (DAZ, pre-trim, truncation, FTZ, saturation). */
static inline uint32_t em_ee_add_bits(uint32_t fs, uint32_t ft)
{
    return em_eei_ee_sum(em_eei_daz(fs), em_eei_daz(ft));
}

/* SUB.S: fs - ft. */
static inline uint32_t em_ee_sub_bits(uint32_t fs, uint32_t ft)
{
    return em_eei_ee_sum(em_eei_daz(fs), em_eei_daz(ft) ^ EM_EE_SIGN);
}

/* MUL.S: fs * ft, truncated, saturated. */
static inline uint32_t em_ee_mul_bits(uint32_t fs, uint32_t ft)
{
    return em_eei_saturate(em_eei_ee_raw_product(fs, ft));
}

/* DIV.S: fs / ft rounded to nearest-even. A zero divisor gives +-MAX by the
 * XOR of the signs, even for a zero or NaN dividend. */
static inline uint32_t em_ee_div_bits(uint32_t fs, uint32_t ft)
{
    uint32_t a = em_eei_daz(fs), b = em_eei_daz(ft);
    uint32_t sign = (a ^ b) >> 31;
    if (em_eei_is_zero(b)) return (sign << 31) | EM_EE_MAX;
    if (em_eei_exp(a) == 0xFFu || em_eei_exp(b) == 0xFFu) {
        if (em_eei_is_nan(a) || em_eei_is_nan(b) || (em_eei_is_inf(a) && em_eei_is_inf(b)))
            return EM_EE_MAX;
        return em_eei_is_inf(a) ? (sign << 31) | EM_EE_MAX : sign << 31;
    }
    return em_eei_quotient(a, b, 1);
}

/* MADD.S: ACC + fs*ft. The product is truncated but NOT saturated; the sum
 * then follows ADD.S. */
static inline uint32_t em_ee_madd_bits(uint32_t acc, uint32_t fs, uint32_t ft)
{
    return em_eei_ee_sum(em_eei_daz(acc), em_eei_ee_raw_product(fs, ft));
}

/* MSUB.S: ACC - fs*ft (the accumulator is the minuend). */
static inline uint32_t em_ee_msub_bits(uint32_t acc, uint32_t fs, uint32_t ft)
{
    return em_eei_ee_sum(em_eei_daz(acc), em_eei_ee_raw_product(fs, ft) ^ EM_EE_SIGN);
}

/* ADDA.S / SUBA.S / MULA.S: the new ACC is exactly the ADD/SUB/MUL result. */
static inline uint32_t em_ee_adda_bits(uint32_t fs, uint32_t ft) { return em_ee_add_bits(fs, ft); }
static inline uint32_t em_ee_suba_bits(uint32_t fs, uint32_t ft) { return em_ee_sub_bits(fs, ft); }
static inline uint32_t em_ee_mula_bits(uint32_t fs, uint32_t ft) { return em_ee_mul_bits(fs, ft); }

/* NEG.S: exponent-255 inputs saturate by sign, then the sign flips.
 * Denormals are negated raw (no DAZ). */
static inline uint32_t em_ee_neg_bits(uint32_t fs)
{
    return em_eei_saturate_signed(fs) ^ EM_EE_SIGN;
}

/* MOV.S: raw copy. */
static inline uint32_t em_ee_mov_bits(uint32_t fs) { return fs; }

/* CVT.W.S: truncate toward zero; |x| >= 2**31, Inf and NaN give 0x7FFFFFFF /
 * 0x80000000 by the sign bit; denormals give 0. Returns the word. */
static inline uint32_t em_ee_cvt_w_s_bits(uint32_t fs)
{
    if (em_eei_exp(fs) >= 158u) return (fs & EM_EE_SIGN) ? UINT32_C(0x80000000) : UINT32_C(0x7FFFFFFF);
    return em_eei_trunc_word(fs, 0);
}

/* CVT.S.W: signed word to float, truncated. */
static inline uint32_t em_ee_cvt_s_w_bits(uint32_t word) { return em_eei_word_float(word, 0); }

/* C.EQ.S / C.LT.S / C.LE.S condition bit: DAZ, sign-keeping saturation,
 * then compare (-0 == +0 == denormal). */
static inline int em_ee_c_eq_bits(uint32_t fs, uint32_t ft) { return em_eei_compare_key(fs) == em_eei_compare_key(ft); }
static inline int em_ee_c_lt_bits(uint32_t fs, uint32_t ft) { return em_eei_compare_key(fs) < em_eei_compare_key(ft); }
static inline int em_ee_c_le_bits(uint32_t fs, uint32_t ft) { return em_eei_compare_key(fs) <= em_eei_compare_key(ft); }

/* float forms */
static inline float em_ee_add(float fs, float ft) { return em_ee_float(em_ee_add_bits(em_ee_bits(fs), em_ee_bits(ft))); }
static inline float em_ee_sub(float fs, float ft) { return em_ee_float(em_ee_sub_bits(em_ee_bits(fs), em_ee_bits(ft))); }
static inline float em_ee_mul(float fs, float ft) { return em_ee_float(em_ee_mul_bits(em_ee_bits(fs), em_ee_bits(ft))); }
static inline float em_ee_div(float fs, float ft) { return em_ee_float(em_ee_div_bits(em_ee_bits(fs), em_ee_bits(ft))); }
static inline float em_ee_madd(float acc, float fs, float ft)
{
    return em_ee_float(em_ee_madd_bits(em_ee_bits(acc), em_ee_bits(fs), em_ee_bits(ft)));
}
static inline float em_ee_msub(float acc, float fs, float ft)
{
    return em_ee_float(em_ee_msub_bits(em_ee_bits(acc), em_ee_bits(fs), em_ee_bits(ft)));
}
static inline float em_ee_adda(float fs, float ft) { return em_ee_float(em_ee_adda_bits(em_ee_bits(fs), em_ee_bits(ft))); }
static inline float em_ee_suba(float fs, float ft) { return em_ee_float(em_ee_suba_bits(em_ee_bits(fs), em_ee_bits(ft))); }
static inline float em_ee_mula(float fs, float ft) { return em_ee_float(em_ee_mula_bits(em_ee_bits(fs), em_ee_bits(ft))); }
static inline float em_ee_neg(float fs) { return em_ee_float(em_ee_neg_bits(em_ee_bits(fs))); }
static inline float em_ee_mov(float fs) { return em_ee_float(em_ee_mov_bits(em_ee_bits(fs))); }
static inline int32_t em_ee_cvt_w_s(float fs) { return em_ee_word_int(em_ee_cvt_w_s_bits(em_ee_bits(fs))); }
static inline float em_ee_cvt_s_w(int32_t word) { return em_ee_float(em_ee_cvt_s_w_bits(em_ee_int_word(word))); }
static inline int em_ee_c_eq(float fs, float ft) { return em_ee_c_eq_bits(em_ee_bits(fs), em_ee_bits(ft)); }
static inline int em_ee_c_lt(float fs, float ft) { return em_ee_c_lt_bits(em_ee_bits(fs), em_ee_bits(ft)); }
static inline int em_ee_c_le(float fs, float ft) { return em_ee_c_le_bits(em_ee_bits(fs), em_ee_bits(ft)); }

/* ---------------------------------------------------------- VU0 macro --- */
/*
 * Lane arithmetic: DAZ inputs, truncation, FTZ, finite overflow -> +-MAX,
 * NO add/sub pre-trim and NO result saturation. An unclamped Inf/NaN
 * propagates: a NaN is quieted (bit 22 set) and the first operand's NaN
 * wins; 0*Inf and Inf-Inf give EM_EE_INDEFINITE. Whether an operand is
 * clamped first (NaN -> +MAX, +-Inf -> +-MAX) depends on the form.
 */

static inline uint32_t em_eei_vu_add_raw(uint32_t x, uint32_t y)
{
    x = em_eei_daz(x);
    y = em_eei_daz(y);
    if (em_eei_is_nan(x)) return em_eei_quiet(x);
    if (em_eei_is_nan(y)) return em_eei_quiet(y);
    if (em_eei_is_inf(x) && em_eei_is_inf(y)) return ((x ^ y) & EM_EE_SIGN) ? EM_EE_INDEFINITE : x;
    if (em_eei_is_inf(x)) return x;
    if (em_eei_is_inf(y)) return y;
    return em_eei_exact_sum(x, y);
}

static inline uint32_t em_eei_vu_sub_raw(uint32_t x, uint32_t y)
{
    x = em_eei_daz(x);
    y = em_eei_daz(y);
    if (em_eei_is_nan(x)) return em_eei_quiet(x);
    if (em_eei_is_nan(y)) return em_eei_quiet(y);
    return em_eei_vu_add_raw(x, y ^ EM_EE_SIGN);
}

static inline uint32_t em_eei_vu_mul_raw(uint32_t x, uint32_t y)
{
    x = em_eei_daz(x);
    y = em_eei_daz(y);
    if (em_eei_is_nan(x)) return em_eei_quiet(x);
    if (em_eei_is_nan(y)) return em_eei_quiet(y);
    if (em_eei_is_inf(x) || em_eei_is_inf(y)) {
        if (em_eei_is_zero(x) || em_eei_is_zero(y)) return EM_EE_INDEFINITE;
        return ((x ^ y) & EM_EE_SIGN) | EM_EE_INF;
    }
    return em_eei_exact_product(x, y);
}

/* The lane-table instructions (ee_float_model.VU_FORMS). The ACC-writing
 * ones are VMULAbc, VMADDAbc and VOPMULA. */
typedef enum em_vu_op {
    EM_VU_ADD,      /* vadd     */
    EM_VU_ADDBC,    /* vaddbc   */
    EM_VU_ADDQ,     /* vaddq    */
    EM_VU_SUB,      /* vsub     */
    EM_VU_SUBBC,    /* vsubbc   */
    EM_VU_MUL,      /* vmul     */
    EM_VU_MULBC,    /* vmulbc   */
    EM_VU_MULQ,     /* vmulq    */
    EM_VU_MULABC,   /* vmulabc  */
    EM_VU_MADDBC,   /* vmaddbc  */
    EM_VU_MADDABC,  /* vmaddabc */
    EM_VU_OPMULA,   /* vopmula  */
    EM_VU_OPMSUB,   /* vopmsub  */
    EM_VU_OP_COUNT
} em_vu_op;

/* A resolved original form. Obtain it only from em_vu_form_lookup. */
typedef struct em_vu_form {
    em_vu_op op;
    unsigned char clamp_fs, clamp_ft, clamp_acc;
    unsigned char product_nan_first; /* VMADD: the product's NaN wins over ACC's */
} em_vu_form;

static inline int em_vu_op_is_bc(em_vu_op op)
{
    return op == EM_VU_ADDBC || op == EM_VU_SUBBC || op == EM_VU_MULBC || op == EM_VU_MULABC ||
           op == EM_VU_MADDBC || op == EM_VU_MADDABC;
}

static inline int em_vu_op_is_q(em_vu_op op) { return op == EM_VU_ADDQ || op == EM_VU_MULQ; }

static inline int em_vu_op_reads_acc(em_vu_op op)
{
    return op == EM_VU_MADDBC || op == EM_VU_MADDABC || op == EM_VU_OPMSUB;
}

/* One entry per form that occurs in the original boot ELF's code, each
 * recorded on a real instance (docs/EE_FLOAT_MODEL.md section 4). Keep this
 * table identical to ee_float_model.VU_FORMS; the header test checks every
 * (op, dest, bc) combination against it. */
static inline int em_vu_form_lookup(em_vu_op op, unsigned dest, int bc, em_vu_form *form)
{
    static const struct {
        unsigned char op, dest;
        signed char bc;
        unsigned char cs, ct, ca, order;
    } table[] = {
        {EM_VU_ADD, 1, -1, 0, 0, 0, 0},       /* ft = vf0: ft clamp free */
        {EM_VU_ADD, 14, -1, 0, 0, 0, 0},
        {EM_VU_ADD, 15, -1, 0, 0, 0, 0},
        {EM_VU_ADDBC, 1, 3, 0, 0, 0, 0},      /* ft = vf0: ft clamp free */
        {EM_VU_ADDBC, 2, 0, 0, 0, 0, 0},
        {EM_VU_ADDBC, 2, 1, 0, 0, 0, 0},
        {EM_VU_ADDBC, 4, 0, 0, 0, 0, 0},
        {EM_VU_ADDBC, 4, 1, 0, 0, 0, 0},
        {EM_VU_ADDBC, 4, 3, 0, 0, 0, 0},      /* ft = vf0: ft clamp free */
        {EM_VU_ADDBC, 8, 0, 0, 0, 0, 0},
        {EM_VU_ADDBC, 8, 1, 0, 0, 0, 0},
        {EM_VU_ADDBC, 8, 2, 0, 0, 0, 0},
        {EM_VU_ADDBC, 8, 3, 0, 0, 0, 0},
        {EM_VU_ADDBC, 12, 0, 0, 0, 0, 0},
        {EM_VU_ADDQ, 8, -1, 0, 0, 0, 0},      /* fs = vf0: fs clamp free */
        {EM_VU_ADDQ, 15, -1, 0, 0, 0, 0},     /* fs = vf0: fs clamp free */
        {EM_VU_SUB, 1, -1, 1, 1, 0, 0},       /* fs == ft on every instance */
        {EM_VU_SUB, 3, -1, 1, 1, 0, 0},       /* fs == ft on every instance */
        {EM_VU_SUB, 8, -1, 0, 0, 0, 0},
        {EM_VU_SUB, 12, -1, 0, 0, 0, 0},
        {EM_VU_SUB, 13, -1, 0, 0, 0, 0},
        {EM_VU_SUB, 14, -1, 0, 0, 0, 0},
        {EM_VU_SUB, 15, -1, 1, 1, 0, 0},
        {EM_VU_SUBBC, 1, 0, 0, 0, 0, 0},
        {EM_VU_SUBBC, 2, 0, 0, 0, 0, 0},
        {EM_VU_SUBBC, 4, 0, 0, 0, 0, 0},
        {EM_VU_SUBBC, 8, 0, 0, 0, 0, 0},
        {EM_VU_SUBBC, 15, 3, 1, 1, 0, 0},
        {EM_VU_MUL, 8, -1, 1, 0, 0, 0},       /* fs == ft: only "not both" is determined */
        {EM_VU_MUL, 14, -1, 1, 0, 0, 0},
        {EM_VU_MUL, 15, -1, 1, 1, 0, 0},
        {EM_VU_MULBC, 1, 0, 1, 0, 0, 0},
        {EM_VU_MULBC, 7, 0, 1, 0, 0, 0},      /* ft = vf0: ft clamp free */
        {EM_VU_MULBC, 8, 0, 1, 0, 0, 0},
        {EM_VU_MULBC, 12, 0, 1, 0, 0, 0},
        {EM_VU_MULBC, 14, 0, 1, 0, 0, 0},
        {EM_VU_MULBC, 14, 1, 1, 0, 0, 0},
        {EM_VU_MULBC, 14, 2, 1, 0, 0, 0},
        {EM_VU_MULBC, 14, 3, 1, 0, 0, 0},
        {EM_VU_MULBC, 15, 0, 1, 1, 0, 0},
        {EM_VU_MULBC, 15, 3, 1, 1, 0, 0},
        {EM_VU_MULQ, 2, -1, 1, 0, 0, 0},
        {EM_VU_MULQ, 12, -1, 1, 0, 0, 0},
        {EM_VU_MULQ, 14, -1, 1, 0, 0, 0},
        {EM_VU_MULQ, 15, -1, 1, 1, 0, 0},
        {EM_VU_MULABC, 1, 2, 1, 0, 0, 0},     /* fs = vf0: fs clamp free */
        {EM_VU_MULABC, 14, 0, 1, 0, 0, 0},
        {EM_VU_MULABC, 15, 0, 1, 0, 0, 0},
        {EM_VU_MULABC, 15, 1, 1, 0, 0, 0},
        {EM_VU_MADDBC, 1, 3, 1, 1, 1, 0},     /* all clamped: NaN order free */
        {EM_VU_MADDBC, 14, 0, 1, 0, 0, 1},
        {EM_VU_MADDBC, 14, 2, 1, 0, 0, 1},
        {EM_VU_MADDBC, 15, 0, 1, 0, 0, 1},
        {EM_VU_MADDBC, 15, 3, 1, 1, 1, 0},    /* all clamped: NaN order free */
        {EM_VU_MADDABC, 14, 1, 1, 0, 0, 0},
        {EM_VU_MADDABC, 15, 1, 1, 0, 0, 0},
        {EM_VU_MADDABC, 15, 2, 1, 0, 0, 0},
        {EM_VU_MADDABC, 15, 3, 1, 0, 0, 0},
        {EM_VU_OPMULA, 14, -1, 0, 0, 0, 0},
        {EM_VU_OPMSUB, 14, -1, 0, 0, 0, 0},
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        if ((int)table[i].op == (int)op && (unsigned)table[i].dest == dest && (int)table[i].bc == bc) {
            if (form) {
                form->op = op;
                form->clamp_fs = table[i].cs;
                form->clamp_ft = table[i].ct;
                form->clamp_acc = table[i].ca;
                form->product_nan_first = table[i].order;
            }
            return EM_EE_FLOAT_OK;
        }
    }
    return EM_EE_FLOAT_UNMEASURED;
}

/* One written lane of a resolved form. `t` is the already-selected ft lane
 * (broadcast lane, Q, or the swizzled lane for VOPMULA/VOPMSUB); `acc` is the
 * ACC lane (read only by VMADDbc, VMADDAbc and VOPMSUB). */
static inline uint32_t em_vu_form_lane_bits(const em_vu_form *form, uint32_t s, uint32_t t, uint32_t acc)
{
    if (form->clamp_fs) s = em_eei_saturate(s);
    if (form->clamp_ft) t = em_eei_saturate(t);
    switch (form->op) {
    case EM_VU_ADD:
    case EM_VU_ADDBC:
    case EM_VU_ADDQ:
        return em_eei_vu_add_raw(s, t);
    case EM_VU_SUB:
    case EM_VU_SUBBC:
        return em_eei_vu_sub_raw(s, t);
    case EM_VU_MUL:
    case EM_VU_MULBC:
    case EM_VU_MULQ:
    case EM_VU_MULABC:
    case EM_VU_OPMULA:
        return em_eei_vu_mul_raw(s, t);
    case EM_VU_MADDBC:
    case EM_VU_MADDABC:
    case EM_VU_OPMSUB:
    case EM_VU_OP_COUNT:
    default:
        break;
    }
    uint32_t product = em_eei_vu_mul_raw(s, t);
    if (form->clamp_acc) acc = em_eei_saturate(acc);
    if (form->op == EM_VU_OPMSUB) return em_eei_vu_sub_raw(acc, product);
    return form->product_nan_first ? em_eei_vu_add_raw(product, acc) : em_eei_vu_add_raw(acc, product);
}

/* Lookup + one lane. On EM_EE_FLOAT_UNMEASURED *out is untouched. */
static inline int em_vu_lane_bits(em_vu_op op, unsigned dest, int bc, uint32_t s, uint32_t t, uint32_t acc,
                                  uint32_t *out)
{
    em_vu_form form;
    int status = em_vu_form_lookup(op, dest, bc, &form);
    if (status != EM_EE_FLOAT_OK) return status;
    *out = em_vu_form_lane_bits(&form, s, t, acc);
    return EM_EE_FLOAT_OK;
}

/* A whole instruction on 4-lane registers (index 0 = x .. 3 = w).
 *   fs, ft: the source registers (ft may be NULL for the Q forms);
 *   q:      the Q register (Q forms only);
 *   acc:    the ACC register (VMADDbc, VMADDAbc, VOPMSUB; else may be NULL);
 *   dst:    in/out destination: vd, or ACC for VMULAbc/VMADDAbc/VOPMULA.
 *           Lanes outside the dest mask keep their value.
 * Any argument may alias any other. VOPMULA/VOPMSUB compute fs.yzx * ft.zxy. */
static inline int em_vu_vec_bits(em_vu_op op, unsigned dest, int bc, const uint32_t fs[4], const uint32_t ft[4],
                                 uint32_t q, const uint32_t acc[4], uint32_t dst[4])
{
    static const unsigned char op_s[3] = {1, 2, 0}, op_t[3] = {2, 0, 1};
    em_vu_form form;
    int status = em_vu_form_lookup(op, dest, bc, &form);
    if (status != EM_EE_FLOAT_OK) return status;
    int outer = op == EM_VU_OPMULA || op == EM_VU_OPMSUB;
    if (outer && (dest & 1u)) return EM_EE_FLOAT_UNMEASURED; /* no w lane in the swizzle */
    if (!fs || !dst || (!em_vu_op_is_q(op) && !ft) || (em_vu_op_reads_acc(op) && !acc))
        return EM_EE_FLOAT_NO_OPERAND;
    uint32_t out[4];
    for (unsigned k = 0; k < 4; k++) {
        if (!((dest >> (3 - k)) & 1u)) {
            out[k] = dst[k];
            continue;
        }
        uint32_t s = fs[k], t;
        if (outer) {
            s = fs[op_s[k]];
            t = ft[op_t[k]];
        } else if (em_vu_op_is_q(op)) {
            t = q;
        } else if (em_vu_op_is_bc(op)) {
            t = ft[bc];
        } else {
            t = ft[k];
        }
        out[k] = em_vu_form_lane_bits(&form, s, t, em_vu_op_reads_acc(op) ? acc[k] : 0);
    }
    memcpy(dst, out, sizeof out);
    return EM_EE_FLOAT_OK;
}

/* VDIV Q = fs.fsf / ft.ftf, truncated. Zero divisor: +-MAX by the XOR of
 * the signs (whatever the dividend); Inf/Inf: +MAX; a NaN operand: +MAX,
 * except that the reciprocal forms (3,0) and (3,3) return a NaN divisor
 * quieted; Inf/x: +-MAX; x/Inf: signed zero. Forms other than (0,0), (3,0),
 * (3,3) return EM_EE_FLOAT_UNMEASURED and leave *q untouched. */
static inline int em_vu_div_bits(uint32_t fs_lane, uint32_t ft_lane, int fsf, int ftf, uint32_t *q)
{
    int nan_passes;
    if (fsf == 0 && ftf == 0) nan_passes = 0;
    else if (fsf == 3 && (ftf == 0 || ftf == 3)) nan_passes = 1;
    else return EM_EE_FLOAT_UNMEASURED;
    uint32_t a = em_eei_daz(fs_lane), b = em_eei_daz(ft_lane);
    uint32_t sign = (a ^ b) >> 31, r;
    if (em_eei_is_zero(b)) r = (sign << 31) | EM_EE_MAX;
    else if (nan_passes && em_eei_is_nan(b) && !em_eei_is_nan(a)) r = em_eei_quiet(b);
    else if (em_eei_exp(a) == 0xFFu || em_eei_exp(b) == 0xFFu) {
        if (em_eei_is_nan(a) || em_eei_is_nan(b) || (em_eei_is_inf(a) && em_eei_is_inf(b))) r = EM_EE_MAX;
        else r = em_eei_is_inf(a) ? (sign << 31) | EM_EE_MAX : sign << 31;
    } else {
        r = em_eei_quotient(a, b, 0);
    }
    *q = r;
    return EM_EE_FLOAT_OK;
}

static inline uint64_t em_eei_isqrt64(uint64_t n)
{
    uint64_t root = 0, bit = UINT64_C(1) << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

/* VSQRT Q = sqrt(|ft.ftf|), truncated; exponent-255 inputs read as MAX.
 * floor(sqrt(m * 2**38)) has at least 31 bits, so truncating it to 24 bits
 * equals truncating the exact root. */
static inline uint32_t em_vu_sqrt_bits(uint32_t ft_lane)
{
    uint32_t b = em_eei_saturate_signed(em_eei_daz(ft_lane)) & UINT32_C(0x7FFFFFFF);
    uint64_t m = em_eei_sig(b);
    int e = em_eei_uexp(b);
    if (m == 0) return 0;
    if (e % 2 != 0) {
        m <<= 1;
        e -= 1;
    }
    return em_eei_pack(0, em_eei_isqrt64(m << 38), (e - 38) / 2, 0, 0);
}

static inline uint32_t em_eei_vu_ftoi(uint32_t a, int frac)
{
    if (em_eei_exp(a) == 0) return 0;
    if ((int)em_eei_exp(a) + frac >= 158) return (a & EM_EE_SIGN) ? UINT32_C(0x80000000) : UINT32_C(0x7FFFFFFF);
    return em_eei_trunc_word(a, frac);
}

/* VFTOI0 / VFTOI4: truncate x * 2**n toward zero; out of range, Inf and NaN
 * saturate by sign; denormals give 0. */
static inline uint32_t em_vu_ftoi0_bits(uint32_t fs_lane) { return em_eei_vu_ftoi(fs_lane, 0); }
static inline uint32_t em_vu_ftoi4_bits(uint32_t fs_lane) { return em_eei_vu_ftoi(fs_lane, 4); }

/* VITOF0 / VITOF4: int32 * 2**-n to float, truncated. */
static inline uint32_t em_vu_itof0_bits(uint32_t word) { return em_eei_word_float(word, 0); }
static inline uint32_t em_vu_itof4_bits(uint32_t word) { return em_eei_word_float(word, -4); }

/* VMAX / VMINI order: raw sign-magnitude bits with -0 below +0. */
static inline int64_t em_eei_vu_order_key(uint32_t b)
{
    return (b & EM_EE_SIGN) ? -(int64_t)(b & UINT32_C(0x7FFFFFFF)) - 1 : (int64_t)b;
}

/* VMAXbc / VMINIbc lane: the larger / smaller raw operand (no DAZ, no clamp). */
static inline uint32_t em_vu_max_bits(uint32_t a, uint32_t b)
{
    return em_eei_vu_order_key(a) >= em_eei_vu_order_key(b) ? a : b;
}

static inline uint32_t em_vu_min_bits(uint32_t a, uint32_t b)
{
    return em_eei_vu_order_key(a) <= em_eei_vu_order_key(b) ? a : b;
}

/* VABS: clear the sign bit, raw. */
static inline uint32_t em_vu_abs_bits(uint32_t a) { return a & UINT32_C(0x7FFFFFFF); }

/* float forms */
static inline int em_vu_lane(em_vu_op op, unsigned dest, int bc, float s, float t, float acc, float *out)
{
    uint32_t r;
    int status = em_vu_lane_bits(op, dest, bc, em_ee_bits(s), em_ee_bits(t), em_ee_bits(acc), &r);
    if (status == EM_EE_FLOAT_OK) memcpy(out, &r, sizeof r);
    return status;
}

static inline int em_vu_vec(em_vu_op op, unsigned dest, int bc, const float fs[4], const float ft[4], float q,
                            const float acc[4], float dst[4])
{
    uint32_t s[4], t[4], a[4], d[4];
    if (fs) memcpy(s, fs, sizeof s);
    if (ft) memcpy(t, ft, sizeof t);
    if (acc) memcpy(a, acc, sizeof a);
    if (dst) memcpy(d, dst, sizeof d);
    int status = em_vu_vec_bits(op, dest, bc, fs ? s : NULL, ft ? t : NULL, em_ee_bits(q), acc ? a : NULL,
                                dst ? d : NULL);
    if (status == EM_EE_FLOAT_OK) memcpy(dst, d, sizeof d);
    return status;
}

static inline int em_vu_div(float fs_lane, float ft_lane, int fsf, int ftf, float *q)
{
    uint32_t r;
    int status = em_vu_div_bits(em_ee_bits(fs_lane), em_ee_bits(ft_lane), fsf, ftf, &r);
    if (status == EM_EE_FLOAT_OK) memcpy(q, &r, sizeof r);
    return status;
}

static inline float em_vu_sqrt(float ft_lane) { return em_ee_float(em_vu_sqrt_bits(em_ee_bits(ft_lane))); }
static inline int32_t em_vu_ftoi0(float fs_lane) { return em_ee_word_int(em_vu_ftoi0_bits(em_ee_bits(fs_lane))); }
static inline int32_t em_vu_ftoi4(float fs_lane) { return em_ee_word_int(em_vu_ftoi4_bits(em_ee_bits(fs_lane))); }
static inline float em_vu_itof0(int32_t word) { return em_ee_float(em_vu_itof0_bits(em_ee_int_word(word))); }
static inline float em_vu_itof4(int32_t word) { return em_ee_float(em_vu_itof4_bits(em_ee_int_word(word))); }
static inline float em_vu_max(float a, float b) { return em_ee_float(em_vu_max_bits(em_ee_bits(a), em_ee_bits(b))); }
static inline float em_vu_min(float a, float b) { return em_ee_float(em_vu_min_bits(em_ee_bits(a), em_ee_bits(b))); }
static inline float em_vu_abs(float a) { return em_ee_float(em_vu_abs_bits(em_ee_bits(a))); }

#endif
