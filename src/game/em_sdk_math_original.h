/* The boot ELF's SDK float math (the EE toolchain's fdlibm-derived float
 * library), translated from the original instructions. Standalone and not
 * wired: docs/SDK_MATH_ORIGINAL.md has the verification and the binding
 * recipe for the coordinator chain.
 *
 *   0011E2A8 sinf     0011DE90 cosf     0011E398 tanf
 *   0011C7B0 argument reduction (rem_pio2f), 0011CE20 its large-argument
 *            kernel (kernel_rem_pio2f), 0011E148 scalbnf, 0011DF98 floorf,
 *            0011DE60 copysignf
 *   0011D770 sine kernel, 0011CCC8 cosine kernel, 0011D878 tangent kernel
 *   0011DBB8 atanf    0011C4C8 atan2f kernel    0011E620 atan2f wrapper
 *   0011CB90 sqrtf kernel (integer digit loop)  0011E748 sqrtf wrapper
 *   0011DF78 fabsf    0011E080 isnanf
 *
 * Every float operation is the EE COP1 instruction the original executes,
 * computed by the shared EE model game/em_ee_float.h (em_ee_*_bits;
 * docs/EE_FLOAT_MODEL.md). The module has no float arithmetic of its own and
 * the host FPU is never used for arithmetic.
 *
 * Data. The tables the original reads are loaded from the user's own boot
 * ELF (em_sdk_math_original_load_tables); nothing disc-derived is stored in
 * the source. Constants that are instruction immediates (lui/ori pairs) are
 * written in the code with the address of the instruction that builds them.
 *
 * Fail-stop. Functions return 0, or -1 with *fault set to an original
 * address: a missing table/world cell at the instruction that reads it, a
 * missing or failing worker at the worker's address, a NULL output at the
 * function's entry, and 0011CE20 indices outside its 20-entry stack arrays
 * (or outside the loaded 198-entry D_0026C178) at 0x0011CE20. */
#ifndef EM_SDK_MATH_ORIGINAL_H
#define EM_SDK_MATH_ORIGINAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SDK_MATH_ELF_SIZE 1532624u         /* SCUS_971.12 */
#define EM_SDK_MATH_TWO_OVER_PI_COUNT 198     /* D_0026C178..D_0026C490 */

/* Every table the translated functions read, as raw words from the ELF. */
typedef struct {
    uint32_t d26C170;                                 /* -0.0f: 0011C4C8 lwc1 at 0x0011C684 */
    int32_t two_over_pi[EM_SDK_MATH_TWO_OVER_PI_COUNT]; /* D_0026C178: the ipio2 chunks 0011C7B0 passes */
    uint32_t npio2_hw[32];                            /* D_0026C490: 0011C7B0 lw at 0x0011C98C */
    int32_t init_jk[4];                               /* D_0026C538: 0011CE20 lw at 0x0011CEA0 */
    uint32_t pio2[11];                                /* D_0026C548: 0011CE20 lwc1 at 0x0011D4A0 */
    uint32_t tan_t[13];                               /* D_0026C598: 0011D878 lwc1 at 0x0011D974.. */
    uint32_t atan_hi[4];                              /* D_0026C5D8: 0011DBB8 lwc1 at 0x0011DE38 */
    uint32_t atan_lo[4];                              /* D_0026C5E8: 0011DBB8 lwc1 at 0x0011DE28 */
    uint32_t atan_t[11];                              /* D_0026C5F8: 0011DBB8 lwc1 at 0x0011DD78.. */
    uint64_t d26C650;                                 /* double: 0011E748 ld at 0x0011E7D8 */
} EmSdkMathTables;

/* Loads every table from the user's boot ELF (size and magic checked).
 * Returns 0, or -1. */
int em_sdk_math_original_load_tables(const uint8_t *elf, size_t size, EmSdkMathTables *out);
/* Reads the local export of that .data window (tools/export_sdk_math_tables.py:
 * 'EMSM', version 1, base 0x0026C170, size 0x4E8, then the window) into the
 * same tables. `d26C5D0` (may be NULL) receives the window's word at
 * D_0026C5D0, the library's error-mode global in its initial .data value.
 * Returns 0, or -1 (missing, malformed or wrong window). */
int em_sdk_math_original_load_export(const char *path, EmSdkMathTables *out, int32_t *d26C5D0);

/* The exception record 0011E620 / 0011E748 build on their stack and pass to
 * 0011DB90 (the original stack layout: +0 type, +4 name, +8 arg1, +0x10
 * arg2, +0x18 retval, +0x20 err). Doubles are raw 64-bit words. */
typedef struct {
    int32_t type;          /* 1 */
    uint32_t name;         /* 0x0026C640 ("atan2f") or 0x0026C648 ("sqrtf") */
    uint64_t arg1, arg2;   /* 00128350 results */
    uint64_t retval;       /* 0, or D_0026C650 (0011E748 with mode != 0) */
    int32_t err;           /* 0 */
} EmSdkMathException;

/* The callees of the two wrappers' error paths. Each returns 0, or -1 (the
 * wrapper then faults at the worker's address). */
typedef struct {
    void *context;
    /* 00128350(f12): float -> double, returned as the 64-bit v0. */
    int (*w_00128350)(void *context, float x, uint64_t *result);
    /* 0011DB90(&exception): its v0; it may change the record. */
    int (*w_0011DB90)(void *context, EmSdkMathException *exception, int32_t *result);
    /* 0011FD78(): the errno cell the wrapper then stores to. */
    int (*w_0011FD78)(void *context, int32_t **cell);
    /* 00127758(a0): double -> float, the wrapper's return value. */
    int (*w_00127758)(void *context, uint64_t value, float *result);
} EmSdkMathWorkers;

/* The one mutable global the wrappers read: D_0026C5D0 (the library's error
 * mode; the ELF image and the captured RAM hold 1). NULL faults where it is
 * read (0x0011E648, 0x0011E76C, 0x0011E7E0). */
typedef struct {
    const int32_t *d26C5D0;
} EmSdkMathWorld;

/* ---- Leaves (no data, cannot fail) ---- */
float em_sdk_math_original_0011DF78(float x);                    /* fabsf */
int32_t em_sdk_math_original_0011E080(float x);                  /* isnanf */
float em_sdk_math_original_0011DE60(float x, float y);           /* copysignf(x, y) */
float em_sdk_math_original_0011DF98(float x);                    /* floorf */
float em_sdk_math_original_0011E148(float x, int32_t n);         /* scalbnf */
float em_sdk_math_original_0011D770(float x, float y, int32_t iy); /* sine kernel */
float em_sdk_math_original_0011CCC8(float x, float y);           /* cosine kernel */
float em_sdk_math_original_0011CB90(float x);                    /* sqrtf kernel */

/* ---- Table readers ---- */
int em_sdk_math_original_0011D878(const EmSdkMathTables *tables, float x, float y, int32_t iy,
                                  float *result, uint32_t *fault);
/* 0011CE20(x, y, e0, nx, prec, ipio2): y receives 1..3 words by prec, *n the
 * returned n & 7. ipio2 is read up to ipio2_count entries. */
int em_sdk_math_original_0011CE20(const EmSdkMathTables *tables, const float *x, float *y,
                                  int32_t e0, int32_t nx, int32_t prec, const int32_t *ipio2,
                                  size_t ipio2_count, int32_t *n, uint32_t *fault);
/* 0011C7B0(x, y): y[0], y[1] and the returned n. */
int em_sdk_math_original_0011C7B0(const EmSdkMathTables *tables, float x, float y[2], int32_t *n,
                                  uint32_t *fault);
int em_sdk_math_original_0011E2A8(const EmSdkMathTables *tables, float x, float *result,
                                  uint32_t *fault);                            /* sinf */
int em_sdk_math_original_0011DE90(const EmSdkMathTables *tables, float x, float *result,
                                  uint32_t *fault);                            /* cosf */
int em_sdk_math_original_0011E398(const EmSdkMathTables *tables, float x, float *result,
                                  uint32_t *fault);                            /* tanf */
int em_sdk_math_original_0011DBB8(const EmSdkMathTables *tables, float x, float *result,
                                  uint32_t *fault);                            /* atanf */
int em_sdk_math_original_0011C4C8(const EmSdkMathTables *tables, float y, float x, float *result,
                                  uint32_t *fault);                            /* atan2 kernel */

/* ---- Wrappers ---- */
int em_sdk_math_original_0011E620(const EmSdkMathTables *tables, const EmSdkMathWorld *world,
                                  const EmSdkMathWorkers *workers, float y, float x, float *result,
                                  uint32_t *fault);                            /* atan2f */
int em_sdk_math_original_0011E748(const EmSdkMathTables *tables, const EmSdkMathWorld *world,
                                  const EmSdkMathWorkers *workers, float x, float *result,
                                  uint32_t *fault);                            /* sqrtf */

/* ---- Worker adapters for the owner modules ----
 * One context serves every adapter. The float-returning forms match the
 * player/collision worker slots (float (*)(void *, float...)): on a fault
 * they return +0.0f and record the first fault address in `fault`, which the
 * caller must check after the call and fail its frame (no value is
 * substituted). The int forms match the w_XXXXXXXX(void *, ..., float *)
 * slots of the script host. */
typedef struct {
    const EmSdkMathTables *tables;
    EmSdkMathWorld world;
    EmSdkMathWorkers workers;
    uint32_t fault;        /* 0, or the first fault address */
} EmSdkMathContext;

float em_sdk_math_original_float_0011E2A8(void *context, float x);          /* sinf */
float em_sdk_math_original_float_0011DE90(void *context, float x);          /* cosf */
float em_sdk_math_original_float_0011E398(void *context, float x);          /* tanf */
float em_sdk_math_original_float_0011DBB8(void *context, float x);          /* atanf */
float em_sdk_math_original_float_0011E620(void *context, float y, float x); /* atan2f */
float em_sdk_math_original_float_0011E748(void *context, float x);          /* sqrtf */
int em_sdk_math_original_w_0011E2A8(void *context, float x, float *result);
int em_sdk_math_original_w_0011DE90(void *context, float x, float *result);

#ifdef __cplusplus
}
#endif

#endif /* EM_SDK_MATH_ORIGINAL_H */
