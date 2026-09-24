/* em_sdk_soft_float.h - the boot ELF's soft-float conversions and compare,
 * and the matherr / errno callees of the SDK math wrappers
 * (docs/SDK_SOFT_FLOAT.md).
 *
 * Translations of the original routines, not models of them. They are the
 * four worker slots of em_sdk_math_original's EmSdkMathWorkers (the error
 * path of 0011E620 atan2f and 0011E748 sqrtf) and every function below them:
 *
 *   00128350  float -> double: 001278C0 unpacks the float into a 0x10-byte
 *             record, 00127728 widens it into a 0x18-byte record and
 *             00126AB8 packs that as a double (the 64-bit v0)
 *   0011DB90  matherr: 001274B0(arg1, arg1) (00126BE8 twice, 00127398),
 *             result discarded; returns 0 and changes nothing
 *   0011FD78  returns the word at D_0024295C (the errno cell pointer)
 *   00127758  double -> float: 00126BE8 unpacks the double, the fraction is
 *             narrowed with a sticky bit, 00128320 builds the float record
 *             and 001277B0 packs it (f0)
 *
 * The records are the library's unpacked number: +0 class (0 signalling
 * NaN, 1 quiet NaN, 2 zero, 3 normal, 4 infinity; other values reach the
 * packers' normal path), +4 sign, +8 unbiased exponent, then the fraction
 * (+0xC, 32 bits, for the float record; +0x10, 64 bits, for the double).
 *
 * Integer code only: the one COP1 instruction on these paths is a move
 * (the float argument's word, the float result's word), done with
 * em_ee_bits / em_ee_float from em_ee_float.h. No arithmetic uses the host
 * FPU.
 *
 * Unwritten fields. The unpackers write only the fields a class needs (zero
 * and infinity leave the exponent and fraction; NaN leaves the exponent),
 * and 00128350 / 00127758 then read the whole record from their stack. The
 * packers mask off every bit such a stale word could contribute (and also
 * every bit of their stale a2 register), so the results depend on the
 * argument alone; the oracle checks that over different stack fills. The
 * native records start zeroed only so that C never reads an indeterminate
 * value.
 *
 * Fail-stop. The worker adapters return 0, or -1 with the fault address in
 * the context (when there is one): a NULL output or record at the worker's
 * own address, and 0011FD78 at 0x0011FD78 when D_0024295C has no storage or
 * holds an address the context has no host cell for. */
#ifndef EM_SDK_SOFT_FLOAT_H
#define EM_SDK_SOFT_FLOAT_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_sdk_math_original.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SDK_SOFT_FLOAT_CLASS_SNAN 0u
#define EM_SDK_SOFT_FLOAT_CLASS_QNAN 1u
#define EM_SDK_SOFT_FLOAT_CLASS_ZERO 2u
#define EM_SDK_SOFT_FLOAT_CLASS_NUMBER 3u
#define EM_SDK_SOFT_FLOAT_CLASS_INFINITY 4u

/* The float record (001278C0 writes it, 001277B0 reads it). */
typedef struct {
    uint32_t cls;          /* +0 */
    uint32_t sign;         /* +4 */
    int32_t exp;           /* +8 */
    uint32_t fraction;     /* +0xC */
} EmSdkSoftFloatPartsF;

/* The double record (00126BE8 writes it, 00126AB8 and 00127398 read it). */
typedef struct {
    uint32_t cls;          /* +0 */
    uint32_t sign;         /* +4 */
    int32_t exp;           /* +8 */
    uint32_t pad;          /* +0xC: never read or written */
    uint64_t fraction;     /* +0x10 */
} EmSdkSoftFloatPartsD;

/* ---- The functions (total; they cannot fail) ---- */

/* 001278C0(&word, dst): writes only the fields the original writes. */
void em_sdk_soft_float_001278C0(uint32_t word, EmSdkSoftFloatPartsF *dst);
/* 00126AB8(src): the packed double. */
uint64_t em_sdk_soft_float_00126AB8(const EmSdkSoftFloatPartsD *src);
/* 00127728(class, sign, exp, fraction): 00126AB8 of that record. */
uint64_t em_sdk_soft_float_00127728(uint32_t cls, uint32_t sign, int32_t exp, uint64_t fraction);
/* 00128350(x): float word -> double word. */
uint64_t em_sdk_soft_float_00128350(uint32_t x);
/* 00126BE8(&value, dst): writes only the fields the original writes. */
void em_sdk_soft_float_00126BE8(uint64_t value, EmSdkSoftFloatPartsD *dst);
/* 00127398(a, b): the three-way compare of two records (1 for a NaN). */
int32_t em_sdk_soft_float_00127398(const EmSdkSoftFloatPartsD *a, const EmSdkSoftFloatPartsD *b);
/* 001274B0(a, b): 00127398 of the two unpacked doubles. */
int32_t em_sdk_soft_float_001274B0(uint64_t a, uint64_t b);
/* 001277B0(src): the packed float word. */
uint32_t em_sdk_soft_float_001277B0(const EmSdkSoftFloatPartsF *src);
/* 00128320(class, sign, exp, fraction): 001277B0 of that record. */
uint32_t em_sdk_soft_float_00128320(uint32_t cls, uint32_t sign, int32_t exp, uint32_t fraction);
/* 00127758(value): double word -> float word. */
uint32_t em_sdk_soft_float_00127758(uint64_t value);
/* 0011DB90(record): runs 001274B0(arg1, arg1) and returns 0. */
int32_t em_sdk_soft_float_0011DB90(const EmSdkMathException *record);

/* ---- Data ---- */

/* D_0024295C read from the user's boot ELF (.data at file offset
 * 0x142C5C; the ELF must be exactly EM_SDK_MATH_ELF_SIZE bytes and start
 * with the ELF magic). The ELF and every capture hold 0x00242670. Returns
 * 0, or -1. */
int em_sdk_soft_float_load_d24295C(const uint8_t *elf, size_t size, uint32_t *out);

/* The runtime's form of that data: the local export
 * assets/sdk_soft_float.emsf (tools/export_sdk_math_tables.py, from the
 * user's ELF; never committed): 'EMSF', u32 version 1, then two
 * (u32 address, u32 word) pairs, D_0024295C with its initial word and then
 * the errno cell that word names with the cell's initial word. The loader
 * checks the magic, the version, the first address (0x0024295C), that the
 * second address equals the first word, and the exact size. It fills
 * *d24295C (D_0024295C's value, which is also the errno cell's address) and
 * *errno_word (the cell's initial .data word; the ELF holds 0). Returns 0,
 * or -1 (missing or malformed; nothing is written). */
int em_sdk_soft_float_load_export(const char *path, uint32_t *d24295C, int32_t *errno_word);

/* ---- Worker adapters (EmSdkMathWorkers slots) ---- */

typedef struct {
    /* Canonical storage for D_0024295C (0011FD78 reads it each call). */
    const uint32_t *d24295C;
    /* The EE address the host cell below stands for (the errno word; the
     * ELF's D_0024295C is 0x00242670) and the cell's canonical storage. */
    uint32_t errno_address;
    int32_t *errno_cell;
    uint32_t fault;        /* 0, or the first fault address */
} EmSdkSoftFloatContext;

int em_sdk_soft_float_w_00128350(void *context, float x, uint64_t *result);
int em_sdk_soft_float_w_0011DB90(void *context, EmSdkMathException *record, int32_t *result);
int em_sdk_soft_float_w_0011FD78(void *context, int32_t **cell);
int em_sdk_soft_float_w_00127758(void *context, uint64_t value, float *result);

/* Fills the four slots of `workers` with the adapters above and sets its
 * context to `context`. Returns 0, or -1 when either pointer is NULL. */
int em_sdk_soft_float_bind(EmSdkMathWorkers *workers, EmSdkSoftFloatContext *context);

#ifdef __cplusplus
}
#endif

#endif /* EM_SDK_SOFT_FLOAT_H */
