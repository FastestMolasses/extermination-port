/* em_effect_kinds.c - effect kinds, glow markers, effect colour, effect-pool
 * resets and the room point-light lists (see em_effect_kinds.h,
 * docs/EFFECT_KINDS.md).
 *
 * Read from the original instructions: the decomp C where it is byte-matched,
 * the split listing for the NEARMISS handlers 001EC1F0 / 001EC3F0 / 001EC470 /
 * 001EBF10 and the particle reset 001F3FA0, and the asm-word 001F54E0. The
 * NEARMISS C of 001EBF10 is wrong in two constants (its fraction offset is
 * 1e-4, 0x38D1B717, and its f15 is 1e-6); the values below are the listing's.
 * Float arithmetic and compares go through em_ee_float.h on bit patterns, in
 * the operand order of the COP1 instruction that performs them. */
#include "game/em_effect_kinds.h"
#include "game/em_ee_float.h"

#include <string.h>

#define ZERO     UINT32_C(0x00000000)
#define ONE      UINT32_C(0x3F800000)
#define TWO      UINT32_C(0x40000000)
#define HALF     UINT32_C(0x3F000000)
#define THREE    UINT32_C(0x40400000)
#define FIVE     UINT32_C(0x40A00000)
#define EIGHT    UINT32_C(0x41000000)
#define TWELVE   UINT32_C(0x41400000)
#define FIFTEEN  UINT32_C(0x41700000)
#define THIRTY   UINT32_C(0x41F00000)
#define SIXTY4   UINT32_C(0x42800000)
#define P127     UINT32_C(0x42FE0000)
#define N127     UINT32_C(0xC2FE0000)
#define P254     UINT32_C(0x437E0000)
#define TWO_M31  UINT32_C(0x30000000) /* 2^-31 */
#define U16_MAX  UINT32_C(0x477FFF00) /* 65535.0 */
#define E_M4     UINT32_C(0x38D1B717) /* 1e-4 */
#define E_M6     UINT32_C(0x358637BD) /* 1e-6 */
#define F1E5     UINT32_C(0x47C35000) /* 100000.0 */
#define F1E6     UINT32_C(0x49742400) /* 1000000.0 */

#define CALL(expr) do { if ((expr) < 0) return -1; } while (0)

static uint32_t fbits(float value) { uint32_t b; memcpy(&b, &value, 4); return b; }
static float bitsf(uint32_t b) { float value; memcpy(&value, &b, 4); return value; }

static int fail(EmEffectKinds *k, uint32_t address, int32_t code)
{
    if (k && k->fault.code == EM_EFFECT_KINDS_FAULT_NONE) {
        k->fault.address = address;
        k->fault.code = code;
    }
    return -1;
}

static int ready(const EmEffectKinds *k)
{
    return k && k->fault.code == EM_EFFECT_KINDS_FAULT_NONE;
}

static int worker(EmEffectKinds *k, uint32_t address, int result)
{
    return result < 0 ? fail(k, address, EM_EFFECT_KINDS_FAULT_WORKER_FAILED) : 0;
}

#define NEED(k, ptr, address) \
    do { if (!(ptr)) return fail((k), (address), EM_EFFECT_KINDS_FAULT_NULL_WORKER); } while (0)

/* ---- the ELF data ------------------------------------------------------- */

int em_effect_kinds_load_tables(const uint8_t *elf, size_t size, EmEffectKindsTables *out)
{
    /* One PROGBITS section at file 0x300 = vram 0x100000. */
    if (!elf || !out || size != 1532624u) return -1;
    size_t lists = EM_EFFECT_KINDS_LISTS_BASE - 0x100000u + 0x300u;
    size_t templ = EM_EFFECT_KINDS_TEMPLATE_BASE - 0x100000u + 0x300u;
    if (lists + EM_EFFECT_KINDS_LISTS_BYTES > size || templ + sizeof out->templates > size) return -1;
    memcpy(out->lists, elf + lists, EM_EFFECT_KINDS_LISTS_BYTES);
    memcpy(out->templates, elf + templ, sizeof out->templates);
    return 0;
}

/* A record of `bytes` at vram `at` inside the lists window, or NULL. */
static uint8_t *list_at(EmEffectKinds *k, uint32_t at, uint32_t bytes)
{
    if (at < EM_EFFECT_KINDS_LISTS_BASE || at > EM_EFFECT_KINDS_LISTS_END ||
        EM_EFFECT_KINDS_LISTS_END - at < bytes)
        return NULL;
    return k->tables->lists + (at - EM_EFFECT_KINDS_LISTS_BASE);
}

/* Arithmetic right shift of a 32-bit word (the sra instruction). */
static int32_t sra32(int32_t v, unsigned n)
{
    uint32_t u = (uint32_t)v >> n;
    if (v < 0) u |= ~(UINT32_MAX >> n);
    return (int32_t)u;
}

static int16_t rd16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static void wr32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }

/* The list walks read the terminator halfword first; a record the walk
 * uses must lie entirely inside the window. */
static int list_live(EmEffectKinds *k, uint32_t at, uint32_t caller, uint8_t **record)
{
    uint8_t *head = list_at(k, at, 2);
    if (!head) return fail(k, caller, EM_EFFECT_KINDS_FAULT_BAD_INDEX);
    if (rd16(head) < 0) { *record = NULL; return 0; }
    *record = list_at(k, at, EM_EFFECT_KINDS_RECORD);
    if (!*record) return fail(k, caller, EM_EFFECT_KINDS_FAULT_BAD_INDEX);
    return 1;
}

/* ---- 001029C0: identity (vsub zero row, w lane 0 + 1, three vmr32) ---- */

static void identity(uint32_t m[16])
{
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? ONE : ZERO;
}

/* ---- the per-subtype handlers ------------------------------------------- */

/* The shared tail of 001EC1F0 / 001EC3F0: 001CFB50(D_0081F8F0, 0, a0,
 * work +0x54, work +0x5C, 1.0, 1e-6, 5.0), then 001CFBE0(a1, 1, source,
 * D_0081F8F0, copy). */
static int single_draw(EmEffectKinds *k, uint32_t self, const float matrix[16], int32_t depth,
                       EmEffectOriginalWork *work, uint32_t source, int32_t copy)
{
    if (!ready(k)) return -1;
    const EmEffectKindsWorkers *w = k->workers;
    NEED(k, w, self);
    NEED(k, w->w_001CFB50, 0x001CFB50u);
    NEED(k, w->w_001CFBE0, 0x001CFBE0u);
    NEED(k, work, self);
    NEED(k, matrix, self);
    CALL(worker(k, 0x001CFB50u, w->w_001CFB50(w->ctx, EM_EFFECT_KINDS_XF, 0, matrix,
                                               fbits(work->accumulator), fbits(work->fraction),
                                               ONE, E_M6, FIVE)));
    CALL(worker(k, 0x001CFBE0u, w->w_001CFBE0(w->ctx, depth, 1, source, EM_EFFECT_KINDS_XF, copy)));
    return 0;
}

/* 001EC1F0: source D_00256700, copy 0. */
int em_effect_kinds_001EC1F0(EmEffectKinds *k, const float matrix[16], int32_t depth,
                             EmEffectOriginalWork *work)
{
    return single_draw(k, 0x001EC1F0u, matrix, depth, work, 0x00256700u, 0);
}

/* 001EC3F0: source D_002568B0, copy 1 (the 001CFBE0 t0 is a copy of a1 = 1). */
int em_effect_kinds_001EC3F0(EmEffectKinds *k, const float matrix[16], int32_t depth,
                             EmEffectOriginalWork *work)
{
    return single_draw(k, 0x001EC3F0u, matrix, depth, work, 0x002568B0u, 1);
}

/* The per-draw random scalar of 001EC470 / 001EBF10:
 *   v = work +4;  f13 = cvt((v >> 16) & 0xFFFF) / 65535.0 + 1e-4;
 *   work +4 = v * 0x25 + 0xB (32-bit wrap). */
static uint32_t lcg_fraction(EmEffectOriginalWork *work)
{
    int32_t v = work->seed_copy;
    uint32_t f13 = em_ee_cvt_s_w_bits(((uint32_t)v >> 16) & 0xFFFFu);
    f13 = em_ee_div_bits(f13, U16_MAX);
    work->seed_copy = (int32_t)((uint32_t)v * 0x25u + 0xBu);
    return em_ee_add_bits(f13, E_M4);
}

/* 001EC470: two draws (sources D_00256940, then D_002569D0), each with f16 = 15.0
 * and copy 1. */
int em_effect_kinds_001EC470(EmEffectKinds *k, const float matrix[16], int32_t depth,
                             EmEffectOriginalWork *work)
{
    if (!ready(k)) return -1;
    const EmEffectKindsWorkers *w = k->workers;
    NEED(k, w, 0x001EC470u);
    NEED(k, w->w_001CFB50, 0x001CFB50u);
    NEED(k, w->w_001CFBE0, 0x001CFBE0u);
    NEED(k, work, 0x001EC470u);
    NEED(k, matrix, 0x001EC470u);
    static const uint32_t source[2] = {0x00256940u, 0x002569D0u};
    for (int i = 0; i < 2; ++i) {
        uint32_t f13 = lcg_fraction(work);
        CALL(worker(k, 0x001CFB50u, w->w_001CFB50(w->ctx, EM_EFFECT_KINDS_XF, 0, matrix,
                                                   fbits(work->accumulator), f13, ONE, E_M6,
                                                   FIFTEEN)));
        CALL(worker(k, 0x001CFBE0u,
                    w->w_001CFBE0(w->ctx, depth, 1, source[i], EM_EFFECT_KINDS_XF, 1)));
    }
    return 0;
}

/* 001EBF10: identity into 0x700036A0, then three draws. Each sets row 3 of
 * that matrix to (x, a0 +0x34 (the node's row-3 y), z, 1.0) and draws with
 * f16 = 0.0 and copy 0: (370, 380) from D_002565E0, (345, 390) and
 * (395, 390) from D_00256670. */
int em_effect_kinds_001EBF10(EmEffectKinds *k, const float matrix[16], int32_t depth,
                             EmEffectOriginalWork *work)
{
    if (!ready(k)) return -1;
    const EmEffectKindsWorkers *w = k->workers;
    NEED(k, w, 0x001EBF10u);
    NEED(k, w->w_001CFB50, 0x001CFB50u);
    NEED(k, w->w_001CFBE0, 0x001CFBE0u);
    NEED(k, k->globals, 0x700036A0u);
    NEED(k, work, 0x001EBF10u);
    NEED(k, matrix, 0x001EBF10u);
    static const uint32_t x[3] = {0x43B90000u, 0x43AC8000u, 0x43C58000u};
    static const uint32_t z[3] = {0x43BE0000u, 0x43C30000u, 0x43C30000u};
    static const uint32_t source[3] = {0x002565E0u, 0x00256670u, 0x00256670u};
    uint32_t *m = k->globals->spad36A0;
    identity(m);                                           /* 001029C0(0x700036A0) */
    for (int i = 0; i < 3; ++i) {
        m[12] = x[i];
        m[13] = fbits(matrix[13]);                         /* a0 +0x34 */
        m[14] = z[i];
        m[15] = ONE;
        uint32_t f13 = lcg_fraction(work);
        float src[16];
        memcpy(src, m, sizeof src);
        CALL(worker(k, 0x001CFB50u, w->w_001CFB50(w->ctx, EM_EFFECT_KINDS_XF, 0, src,
                                                   fbits(work->accumulator), f13, ONE, E_M6,
                                                   ZERO)));
        CALL(worker(k, 0x001CFBE0u,
                    w->w_001CFBE0(w->ctx, depth, 1, source[i], EM_EFFECT_KINDS_XF, 0)));
    }
    return 0;
}

/* ---- 001D0540 / 001CFB50 ------------------------------------------------ */

/* One point through the four rows the original loads into vf20..vf23:
 * ACC = row0 * v.x (VMULAx), ACC += row1 * v.y (VMADDAy), ACC += row2 * v.z
 * (VMADDAz), out = ACC + row3 * vf0.w (VMADDw; vf0.w is 1.0). */
static int vu_point(EmEffectKinds *k, const uint32_t m[16], const uint32_t v[4], uint32_t out[4])
{
    static const uint32_t vf0[4] = {ZERO, ZERO, ZERO, ONE};
    uint32_t acc[4] = {0, 0, 0, 0}, r[4];
    if (em_vu_vec_bits(EM_VU_MULABC, 15, 0, m + 0, v, 0, NULL, acc) != EM_EE_FLOAT_OK ||
        em_vu_vec_bits(EM_VU_MADDABC, 15, 1, m + 4, v, 0, acc, acc) != EM_EE_FLOAT_OK ||
        em_vu_vec_bits(EM_VU_MADDABC, 15, 2, m + 8, v, 0, acc, acc) != EM_EE_FLOAT_OK ||
        em_vu_vec_bits(EM_VU_MADDBC, 15, 3, m + 12, vf0, 0, acc, r) != EM_EE_FLOAT_OK)
        return fail(k, 0x001D0540u, EM_EFFECT_KINDS_FAULT_UNMEASURED);
    memcpy(out, r, sizeof r);
    return 0;
}

int em_effect_kinds_001D0540(EmEffectKinds *k, EmEffectKindsXfState *s, const uint32_t pos[3],
                             const uint32_t m[16], uint32_t d, uint32_t *f0)
{
    if (!ready(k)) return -1;
    const EmEffectKindsWorkers *w = k->workers;
    NEED(k, s, 0x70003660u);
    NEED(k, pos, 0x001D0540u);
    NEED(k, m, 0x001D0540u);
    NEED(k, f0, 0x001D0540u);
    NEED(k, w, 0x001D0540u);
    NEED(k, w->w_0011DF78, 0x0011DF78u);
    /* The two scratch points: (x, y, z, 1) and (x, y, z - d, 1). */
    s->spad3660[0] = pos[0];
    s->spad3660[1] = pos[1];
    s->spad3660[2] = pos[2];
    s->spad3660[3] = ONE;
    s->spad3670[0] = pos[0];
    s->spad3670[1] = pos[1];
    s->spad3670[2] = em_ee_sub_bits(pos[2], d);
    s->spad3670[3] = ONE;
    /* The rows are loaded once, before either point. */
    uint32_t rows[16];
    memcpy(rows, m, sizeof rows);
    CALL(vu_point(k, rows, s->spad3660, s->spad3660));
    CALL(vu_point(k, rows, s->spad3670, s->spad3670));
    /* z * (1 / w) for each, stored back; the difference re-reads the first
     * point's stored z. */
    const uint32_t q1 = em_ee_div_bits(ONE, s->spad3660[3]);
    s->spad3660[2] = em_ee_mul_bits(s->spad3660[2], q1);
    const uint32_t q2 = em_ee_div_bits(ONE, s->spad3670[3]);
    s->spad3670[2] = em_ee_mul_bits(s->spad3670[2], q2);
    const uint32_t diff = em_ee_sub_bits(s->spad3660[2], s->spad3670[2]);
    return worker(k, 0x0011DF78u, w->w_0011DF78(w->ctx, diff, f0));
}

int em_effect_kinds_001CFB50(EmEffectKinds *k, EmEffectKindsXfState *s, EmEffectKindsXf *dst,
                             int32_t a1, const uint32_t src[16], uint32_t f12, uint32_t f13,
                             uint32_t f14, uint32_t f15, uint32_t f16)
{
    if (!ready(k)) return -1;
    NEED(k, dst, 0x0081F8F0u);
    NEED(k, src, 0x001CFB50u);
    NEED(k, s, 0x70003AC0u);
    /* The reached tail worker is checked before the first store. */
    NEED(k, k->workers, 0x001CFB50u);
    NEED(k, k->workers->w_0011DF78, 0x0011DF78u);
    /* The byte-matched C's order: the four scalars, the depth scale, the
     * context address, then the 64-byte copy of the source. */
    dst->word[0x44 / 4] = f12;
    dst->word[0x4C / 4] = f13;
    dst->word[0x48 / 4] = f14;
    dst->word[0x50 / 4] = f15;
    uint32_t scale;
    CALL(em_effect_kinds_001D0540(k, s, src + 12, s->spad3AC0, f16, &scale));
    dst->word[0x54 / 4] = scale;
    /* 001CD370(a1): D_00275670 + (a1 << 6) + 0x2240, 32-bit wrap. */
    dst->word[0x40 / 4] = s->d275670 + ((uint32_t)a1 << 6) + 0x2240u;
    memmove(dst->word, src, 64);
    return 0;
}

int em_effect_kinds_handler(EmEffectKinds *k, uint32_t handler, const float matrix[16],
                            int32_t depth, EmEffectOriginalWork *work)
{
    if (!ready(k)) return -1;
    switch (handler) {
    case EM_EFFECT_KINDS_H_001EC1F0: return em_effect_kinds_001EC1F0(k, matrix, depth, work);
    case EM_EFFECT_KINDS_H_001EC3F0: return em_effect_kinds_001EC3F0(k, matrix, depth, work);
    case EM_EFFECT_KINDS_H_001EC470: return em_effect_kinds_001EC470(k, matrix, depth, work);
    case EM_EFFECT_KINDS_H_001EBF10: return em_effect_kinds_001EBF10(k, matrix, depth, work);
    default: return fail(k, handler, EM_EFFECT_KINDS_FAULT_UNTRANSLATED);
    }
}

/* ---- 001F54E0: effect colour -------------------------------------------- */

/* Lower clamp: c.lt v, -127 selects -127. Upper clamp: when c.le v, 127 is
 * false, 127 is selected. */
static uint32_t clamp127(uint32_t v)
{
    if (em_ee_c_lt_bits(v, N127)) v = N127;
    if (!em_ee_c_le_bits(v, P127)) v = P127;
    return v;
}

int em_effect_kinds_001F54E0(EmEffectKinds *k, void *obj, float out[4], uint32_t callback,
                             const float color[4])
{
    if (!ready(k)) return -1;
    const EmEffectKindsWorkers *w = k->workers;
    NEED(k, w, 0x001F54E0u);
    NEED(k, w->w_00122BB8, 0x00122BB8u);
    NEED(k, w->w_indirect, callback);
    NEED(k, out, 0x001F54E0u);
    NEED(k, color, 0x001F54E0u);
    int32_t random = 0;
    CALL(worker(k, 0x00122BB8u, w->w_00122BB8(w->ctx, &random)));
    /* All four colour lanes are read before the first store. */
    uint32_t c0 = fbits(color[0]), c1 = fbits(color[1]), c2 = fbits(color[2]);
    uint32_t c3 = fbits(color[3]);
    uint32_t r = em_ee_cvt_s_w_bits((uint32_t)random);
    uint32_t t = em_ee_mul_bits(TWO_M31, r);              /* 2^-31 * rand */
    t = em_ee_mul_bits(P254, t);                          /* 254 * t */
    t = em_ee_add_bits(N127, t);                          /* -127 + t */
    uint32_t b = em_ee_mul_bits(c3, t);                   /* c3 * t */
    b = em_ee_add_bits(b, P127);                          /* + 127 */
    uint32_t v0 = em_ee_sub_bits(em_ee_mul_bits(c0, b), P127);
    uint32_t v1 = em_ee_sub_bits(em_ee_mul_bits(c1, b), P127);
    uint32_t v2 = em_ee_sub_bits(em_ee_mul_bits(c2, b), P127);
    out[0] = bitsf(clamp127(v0));                          /* +0x80 */
    out[1] = bitsf(clamp127(v1));                          /* +0x84 */
    out[2] = bitsf(clamp127(v2));                          /* +0x88 */
    out[3] = bitsf(ONE);                                   /* +0x8C */
    CALL(worker(k, callback, w->w_indirect(w->ctx, callback, obj)));
    return 0;
}

/* ---- selectors: key = (D_00810700 << 8) + D_00810701 ------------------- */

static int key_of(uint8_t area, uint8_t sub) { return (area << 8) + sub; }

uint32_t em_effect_kinds_001F5640(uint8_t area, uint8_t sub)
{
    switch (key_of(area, sub)) {
    case 0x0000: return 0x0025AD80u;
    case 0x0001: return 0x0025B0D0u;
    case 0x0700: return 0x0025B330u;
    case 0x0703: return 0x0025B4F0u;
    case 0x0B00: return 0x0025B590u;
    case 0x0D00: return 0x0025B770u;
    case 0x0E00: return 0x0025BCF0u;
    case 0x0F00: return 0x0025BDE0u;
    case 0x0F01: return 0x0025BF20u;
    case 0x1100: return 0x0025C130u;
    case 0x0600: return 0x0025C400u;
    case 0x0601: return 0x0025C400u;
    case 0x1300: return 0x0025C4D0u;
    case 0x1400: return 0x0025C820u;
    case 0x1500: return 0x0025C8F0u;
    case 0x0802: return 0x0025C9C0u;
    default: return 0;
    }
}

uint32_t em_effect_kinds_001F5CA0(uint8_t area, uint8_t sub)
{
    switch (key_of(area, sub)) {
    case 0x0301: return 0x0025CAD0u;
    case 0x0302: return 0x0025CB20u;
    case 0x0400: return 0x0025CBA0u;
    case 0x0401: return 0x0025CC40u;
    case 0x0700: return 0x0025CCE0u;
    case 0x0800: return 0x0025CD30u;
    case 0x0803: return 0x0025CD80u;
    case 0x0D00: return 0x0025CDD0u;
    case 0x0F00: return 0x0025CE20u;
    case 0x1500: return 0x0025CEC0u;
    default: return 0;
    }
}

uint32_t em_effect_kinds_001F6760(uint8_t area, uint8_t sub)
{
    switch (key_of(area, sub)) {
    case 0x0000: return 0x0025CF10u;
    case 0x0001: return 0x0025CF90u;
    case 0x0002: return 0x0025CFE0u;
    case 0x0100: return 0x0025D030u;
    case 0x0200: return 0x0025D080u;
    case 0x0E00: return 0x0025D1F0u;
    case 0x1100: return 0x0025D340u;
    case 0x1301: return 0x0025D270u;
    default: return 0;
    }
}

uint32_t em_effect_kinds_001F6D60(uint8_t area, uint8_t sub)
{
    switch (key_of(area, sub)) {
    case 0x0100: return 0x0025D3C0u;
    case 0x0700: return 0x0025D500u;
    case 0x0702: return 0x0025D550u;
    case 0x0B00: return 0x0025D5A0u;
    case 0x1000: return 0x0025D5F0u;
    case 0x1200: return 0x0025D6C0u;
    case 0x1300: return 0x0025D760u;
    default: return 0;
    }
}

/* ---- glow markers -------------------------------------------------------- */

/* 001F5940: the jump table (jtbl_0026EAF0) picks the colour, the size and
 * the emit mode; kinds 3, 6 and every value above 9 take the default. */
int em_effect_kinds_001F5940(EmEffectKinds *k, uint32_t kind, const float pos[4], int32_t t)
{
    if (!ready(k)) return -1;
    const EmEffectKindsWorkers *w = k->workers;
    NEED(k, w, 0x001F5940u);
    NEED(k, w->w_001F4D40, 0x001F4D40u);
    int32_t col[4];
    uint32_t size;
    int mode;
#define SET(r, g, b_, s, m) do { col[0] = (r); col[1] = (g); col[2] = (b_); col[3] = 0x7F; \
                                 size = (s); mode = (m); } while (0)
    switch (kind) {
    case 0: SET(0x00, 0x80, 0x00, TWELVE, 1); break;
    case 1: SET(0x80, 0x40, 0x40, EIGHT, 0); break;
    case 2: SET(0x80, 0x80, 0x66, EIGHT, 0); break;
    case 4: SET(0x80, 0x40, 0x40, THREE, 0); break;
    case 5: SET(0x80, 0x00, 0x00, THIRTY, 0); break;
    case 7: SET(0x20, 0x80, 0x20, THREE, 2); break;
    case 8: SET(0x80, 0x80, 0x66, FIFTEEN, 2); break;
    case 9: SET(0x80, 0x10, 0x10, THREE, 2); break;
    default: SET(0x40, 0x40, 0x80, THREE, 0); break;
    }
#undef SET
    uint32_t half = em_ee_mul_bits(HALF, size);            /* 0.5 * size */
    if (mode == 1) {
        NEED(k, w->w_0011DF78, 0x0011DF78u);
        NEED(k, w->w_001281C0, 0x001281C0u);
        NEED(k, k->globals, 0x70003B68u);
        /* (clock + ((t * 0x12D687) >> 16)) & 0x7F: 32-bit product, arithmetic shift. */
        int32_t product = (int32_t)((uint32_t)t * 0x12D687u);
        col[3] = (int32_t)(((uint32_t)k->globals->spad3B68 + (uint32_t)sra32(product, 16)) & 0x7Fu);
        uint32_t x = em_ee_sub_bits(em_ee_cvt_s_w_bits((uint32_t)col[3]), SIXTY4);
        uint32_t r = 0;
        CALL(worker(k, 0x0011DF78u, w->w_0011DF78(w->ctx, x, &r)));
        int32_t v = 0;
        CALL(worker(k, 0x001281C0u, w->w_001281C0(w->ctx, em_ee_mul_bits(TWO, r), &v)));
        col[3] = v;
        CALL(worker(k, 0x001F4D40u, w->w_001F4D40(w->ctx, pos, col, size, half)));
    } else if (mode == 2) {
        NEED(k, w->w_0021B9A0, 0x0021B9A0u);
        CALL(worker(k, 0x0021B9A0u, w->w_0021B9A0(w->ctx, 2, ZERO, F1E5)));
        CALL(worker(k, 0x0021B9A0u, w->w_0021B9A0(w->ctx, 3, ZERO, F1E6)));
        CALL(worker(k, 0x001F4D40u, w->w_001F4D40(w->ctx, pos, col, size, half)));
        CALL(worker(k, 0x0021B9A0u, w->w_0021B9A0(w->ctx, 1, ZERO, ZERO)));
    } else {
        CALL(worker(k, 0x001F4D40u, w->w_001F4D40(w->ctx, pos, col, size, half)));
    }
    return 0;
}

/* 001F5C20: every record of the 001F5640 list: pos = (+0xC, +0x10, +0x14,
 * 1.0), 001F5940(+4 as a signed halfword, pos, record index). */
int em_effect_kinds_001F5C20(EmEffectKinds *k)
{
    if (!ready(k)) return -1;
    NEED(k, k->globals, 0x00810700u);
    uint32_t at = em_effect_kinds_001F5640(k->globals->d810700, k->globals->d810701);
    if (!at) return 0;
    NEED(k, k->tables, 0x001F5C20u);
    for (int32_t i = 0;; ++i, at += EM_EFFECT_KINDS_RECORD) {
        uint8_t *rec = NULL;
        int live = list_live(k, at, 0x001F5C20u, &rec);
        if (live < 0) return -1;
        if (!live) return 0;
        float pos[4];
        memcpy(pos, rec + 0xC, 12);
        pos[3] = bitsf(ONE);
        CALL(em_effect_kinds_001F5940(k, (uint32_t)(int32_t)rd16(rec + 4), pos, i));
    }
}

/* ---- pool resets ----------------------------------------------------------- */

/* 001F03D0(n): the 0x20 slots of ring lane n (D_0028F700 + 0x4DBEC0 +
 * n*0xC00): identity at +0x00, +0x50 (8 bytes), +0x58 and +0x5C zeroed;
 * then D_0081F950[n] = 0. The params at +0x40 are left alone. */
int em_effect_kinds_001F03D0(EmEffectKinds *k, int32_t lane)
{
    if (!ready(k)) return -1;
    NEED(k, k->decals, 0x0076B5C0u);
    if (lane < 0 || lane >= EM_EFFECT_ORIGINAL_DECAL_LANES)
        return fail(k, 0x001F03D0u, EM_EFFECT_KINDS_FAULT_BAD_INDEX);
    for (int i = 0; i < EM_EFFECT_ORIGINAL_DECAL_SLOTS; ++i) {
        EmEffectOriginalDecalSlot *s = &k->decals->slot[lane][i];
        identity(s->source);                               /* 001029C0(p) */
        s->tag = 0;
        s->life = 0;
        s->w5C = 0;
    }
    k->decals->index[lane] = 0;
    return 0;
}

/* 001F3FA0: 0x80 records of 0x90 cleared, halfword +0x80 = 1; then
 * D_00275C40 = D_00275C44 = 0. */
int em_effect_kinds_001F3FA0(EmEffectKinds *k)
{
    if (!ready(k)) return -1;
    NEED(k, k->particles, 0x007709C0u);
    NEED(k, k->globals, 0x00275C40u);
    for (int i = 0; i < EM_EFFECT_KINDS_PARTICLES; ++i) {
        uint8_t *rec = k->particles->record[i];
        memset(rec, 0, EM_EFFECT_KINDS_PARTICLE_BYTES);
        int16_t one = 1;
        memcpy(rec + 0x80, &one, 2);
    }
    k->globals->d275C40 = 0;
    k->globals->d275C44 = 0;
    return 0;
}

/* 001F0310: 001F3FA0, then 001F03D0 for lanes 0, 1, 3, 4, 5, 6 (not 2). */
int em_effect_kinds_001F0310(EmEffectKinds *k)
{
    if (!ready(k)) return -1;
    NEED(k, k->particles, 0x007709C0u);
    NEED(k, k->globals, 0x00275C40u);
    NEED(k, k->decals, 0x0076B5C0u);
    CALL(em_effect_kinds_001F3FA0(k));
    static const int32_t lanes[6] = {0, 1, 3, 4, 5, 6};
    for (int i = 0; i < 6; ++i) CALL(em_effect_kinds_001F03D0(k, lanes[i]));
    return 0;
}

/* ---- room point-light lists ----------------------------------------------- */

/* 001F6640(p): for every record whose handle (+0x24) is -1:
 * +0x24 = 001D7FA0((+0xC, +0x10, +0x14, 1.0), &D_0026EB70[+4 * 16], 1, 1.0, 0.0). */
int em_effect_kinds_001F6640(EmEffectKinds *k, uint32_t list)
{
    if (!ready(k)) return -1;
    if (!list) return 0;
    const EmEffectKindsWorkers *w = k->workers;
    NEED(k, k->tables, 0x001F6640u);
    for (uint32_t at = list;; at += EM_EFFECT_KINDS_RECORD) {
        uint8_t *rec = NULL;
        int live = list_live(k, at, 0x001F6640u, &rec);
        if (live < 0) return -1;
        if (!live) return 0;
        if (rd32(rec + 0x24) != -1) continue;
        int32_t preset = rd16(rec + 4);
        if (preset < 0 || preset >= EM_EFFECT_KINDS_TEMPLATES)
            return fail(k, EM_EFFECT_KINDS_TEMPLATE_BASE, EM_EFFECT_KINDS_FAULT_BAD_INDEX);
        NEED(k, w, 0x001F6640u);
        NEED(k, w->w_001D7FA0, 0x001D7FA0u);
        float pos[4], color[4];
        memcpy(pos, rec + 0xC, 12);
        pos[3] = bitsf(ONE);
        memcpy(color, k->tables->templates[preset], sizeof color);
        int32_t handle = 0;
        CALL(worker(k, 0x001D7FA0u,
                    w->w_001D7FA0(w->ctx, pos, EM_EFFECT_KINDS_TEMPLATE_BASE + (uint32_t)preset * 16u,
                                  color, 1, ONE, ZERO, &handle)));
        wr32(rec + 0x24, handle);
    }
}

/* 001F66F0(p): for every record whose handle is not -1: 001D80B0(handle),
 * handle = -1. */
int em_effect_kinds_001F66F0(EmEffectKinds *k, uint32_t list)
{
    if (!ready(k)) return -1;
    if (!list) return 0;
    const EmEffectKindsWorkers *w = k->workers;
    NEED(k, k->tables, 0x001F66F0u);
    for (uint32_t at = list;; at += EM_EFFECT_KINDS_RECORD) {
        uint8_t *rec = NULL;
        int live = list_live(k, at, 0x001F66F0u, &rec);
        if (live < 0) return -1;
        if (!live) return 0;
        int32_t handle = rd32(rec + 0x24);
        if (handle == -1) continue;
        NEED(k, w, 0x001F66F0u);
        NEED(k, w->w_001D80B0, 0x001D80B0u);
        CALL(worker(k, 0x001D80B0u, w->w_001D80B0(w->ctx, handle)));
        wr32(rec + 0x24, -1);
    }
}

/* 001F6850: release the 001F6760 list; for key 0x1301 also D_0025D2C0. */
int em_effect_kinds_001F6850(EmEffectKinds *k)
{
    if (!ready(k)) return -1;
    NEED(k, k->globals, 0x00810700u);
    uint8_t area = k->globals->d810700, sub = k->globals->d810701;
    uint32_t list = em_effect_kinds_001F6760(area, sub);
    if (!list) return 0;
    CALL(em_effect_kinds_001F66F0(k, list));
    if (key_of(area, sub) == 0x1301) CALL(em_effect_kinds_001F66F0(k, EM_EFFECT_KINDS_LIST_25D2C0));
    return 0;
}

/* 001F68B0: obj = 001F6760(); 001F6850(); then per key, register obj (or
 * the 0x1301 lists) when its latch byte is (or is not) 0xFF; any other key
 * releases obj. The key is read again after 001F6850. */
int em_effect_kinds_001F68B0(EmEffectKinds *k)
{
    if (!ready(k)) return -1;
    NEED(k, k->globals, 0x00810700u);
    const EmEffectKindsGlobals *g = k->globals;
    uint32_t obj = em_effect_kinds_001F6760(g->d810700, g->d810701);
    CALL(em_effect_kinds_001F6850(k));
    switch (key_of(g->d810700, g->d810701)) {
    case 0x0000: if (g->d81075D != 0xFF) CALL(em_effect_kinds_001F6640(k, obj)); break;
    case 0x0001: if (g->d81075E == 0xFF) CALL(em_effect_kinds_001F6640(k, obj)); break;
    case 0x0002: if (g->d810784 == 0xFF) CALL(em_effect_kinds_001F6640(k, obj)); break;
    case 0x0100: if (g->d81075E != 0xFF) CALL(em_effect_kinds_001F6640(k, obj)); break;
    case 0x0200: if (g->d810761 == 0xFF) CALL(em_effect_kinds_001F6640(k, obj)); break;
    case 0x0E00: if (g->d810784 == 0xFF) CALL(em_effect_kinds_001F6640(k, obj)); break;
    case 0x1100: if (g->d810785 == 0xFF) CALL(em_effect_kinds_001F6640(k, obj)); break;
    case 0x1301:
        if (g->d81079E == 0xFF) CALL(em_effect_kinds_001F6640(k, EM_EFFECT_KINDS_LIST_25D2C0));
        if (g->d810778 != 0xFF && g->d81077B != 0xFF)
            CALL(em_effect_kinds_001F6640(k, EM_EFFECT_KINDS_LIST_25D270));
        break;
    default:
        if (obj) CALL(em_effect_kinds_001F66F0(k, obj));
        break;
    }
    return 0;
}

/* 001F6E40: p = 001F6D60(); if p: 001F66F0(p), 001F6640(p). */
int em_effect_kinds_001F6E40(EmEffectKinds *k)
{
    if (!ready(k)) return -1;
    NEED(k, k->globals, 0x00810700u);
    uint32_t list = em_effect_kinds_001F6D60(k->globals->d810700, k->globals->d810701);
    if (!list) return 0;
    CALL(em_effect_kinds_001F66F0(k, list));
    CALL(em_effect_kinds_001F6640(k, list));
    return 0;
}
