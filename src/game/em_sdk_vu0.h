/* em_sdk_vu0.h - the boot ELF's SDK VU0 leaves that several translated
 * modules call: one translation each, shared by every caller
 * (docs/SDK_VU0.md).
 *
 *   001026D0(dst, a, b)  4x4 product: the four rows of `a` are loaded
 *                        first; then each row v of `b` is loaded and gives
 *                        dst's row: ACC = a0 * v.x, ACC += a1 * v.y,
 *                        ACC += a2 * v.z, row = ACC + a3 * v.w (all four
 *                        lanes), stored before the next row of b is loaded
 *   00102900(dst, v, s)  dst = v * s in all four lanes (the float argument
 *                        moved into the x lane of the second operand)
 *   00102948(dst, src)   one quadword: all four words loaded, then stored
 *
 * Every instruction goes through em_ee_float.h's VU0 macro model
 * (docs/EE_FLOAT_MODEL.md); the module has no arithmetic of its own. Like
 * em_ee_float.h it is header-only and depends on nothing else, so every
 * caller (and every caller's test) uses this one translation without
 * linking another module or the caller's hosts.
 *
 * Aliasing is the original's: dst may be a or b (001026D0), or v (00102900),
 * or src (00102948).
 *
 * Status. 001026D0 and 00102900 return EM_EE_FLOAT_OK (0), or the model's
 * nonzero status for a form it refuses; on a refusal of row r, 001026D0 has
 * stored rows 0..r-1 (as the original has) and 00102900 has stored nothing.
 *
 * The decomp's files are hand-written asm; this follows their instructions.
 *
 * Oracle: tools/test_sdk_vu0_reference.py executes the original
 * instructions of all three and compares every stored word. */
#ifndef EM_SDK_VU0_H
#define EM_SDK_VU0_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "game/em_ee_float.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline int em_sdk_vu0_001026D0(uint32_t dst[16], const uint32_t a[16], const uint32_t b[16])
{
    uint32_t m[16];
    memcpy(m, a, sizeof m);                        /* the four rows of a first */
    for (unsigned row = 0; row < 4; ++row) {
        uint32_t v[4], acc[4] = {0, 0, 0, 0}, out[4] = {0, 0, 0, 0};
        memcpy(v, b + 4 * row, sizeof v);          /* then one row of b */
        int st = em_vu_vec_bits(EM_VU_MULABC, 15, 0, m + 0, v, 0, NULL, acc);
        if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDABC, 15, 1, m + 4, v, 0, acc, acc);
        if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDABC, 15, 2, m + 8, v, 0, acc, acc);
        if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDBC, 15, 3, m + 12, v, 0, acc, out);
        if (st != EM_EE_FLOAT_OK) return st;
        memcpy(dst + 4 * row, out, sizeof out);    /* stored before the next row */
    }
    return EM_EE_FLOAT_OK;
}

static inline int em_sdk_vu0_00102900(uint32_t dst[4], const uint32_t v[4], uint32_t s)
{
    const uint32_t t[4] = {s, 0, 0, 0};            /* only the x lane is broadcast */
    uint32_t in[4], out[4] = {0, 0, 0, 0};
    memcpy(in, v, sizeof in);
    int st = em_vu_vec_bits(EM_VU_MULBC, 15, 0, in, t, 0, NULL, out);
    if (st == EM_EE_FLOAT_OK) memcpy(dst, out, sizeof out);
    return st;
}

static inline void em_sdk_vu0_00102948(void *dst, const void *src)
{
    unsigned char q[16];
    memcpy(q, src, sizeof q);                      /* the load, then the store */
    memcpy(dst, q, sizeof q);
}

#ifdef __cplusplus
}
#endif

#endif /* EM_SDK_VU0_H */
