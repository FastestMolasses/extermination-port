/* The boot ELF's soft-float conversions and compare, and the matherr /
 * errno callees of the SDK math wrappers, translated from the original
 * instructions. See em_sdk_soft_float.h and docs/SDK_SOFT_FLOAT.md. Every
 * address is a boot-ELF runtime address. All of this is integer code: the
 * registers are 64 bits wide, and each step below names the width the
 * original operation works in. */
#include "game/em_sdk_soft_float.h"

#include <stdio.h>
#include <string.h>

#include "game/em_ee_float.h"

#define FRAC52 UINT64_C(0x000FFFFFFFFFFFFF)   /* all-ones >> 12 (0x126BF0 / 0x126B94) */
#define QNAN_D (UINT64_C(0x8000) << 36)       /* bit 51: 0x126AD0..0x126AD4, 0x126C38..0x126C3C */
#define HIDDEN_D (UINT64_C(0x8000) << 45)     /* bit 60: 0x126C60..0x126C64 */
#define ROUND_LIMIT_D (UINT64_MAX >> 3)       /* 0x126B68..0x126B6C */

/* ---------------------------------------------------------------------------
 * 00128350: float -> double.
 * ------------------------------------------------------------------------- */

/* 001278C0: unpacks the float word the first argument points at. The sign
 * is stored before the first test (0x1278E0), so every class has it. */
void em_sdk_soft_float_001278C0(uint32_t word, EmSdkSoftFloatPartsF *dst)
{
    const uint32_t fraction = word & 0x007FFFFFu;              /* 0x1278C4..0x1278D4 */
    const uint32_t biased = (word >> 23) & 0xFFu;              /* 0x1278D0 / 0x1278D8 */
    dst->sign = word >> 31;                                    /* 0x1278CC / 0x1278E0 */
    if (biased == 0) {                                         /* 0x1278DC: exponent field 0 */
        dst->cls = EM_SDK_SOFT_FLOAT_CLASS_ZERO;               /* 0x1278E4: a denormal is zero too */
        return;
    }
    if (biased == 0xFFu) {                                     /* 0x1278F4 */
        if (fraction == 0) {                                   /* 0x1278FC */
            dst->cls = EM_SDK_SOFT_FLOAT_CLASS_INFINITY;       /* 0x127904 */
            return;
        }
        /* 0x127910: fraction bit 20 (0x100000) picks quiet over signalling. */
        dst->cls = (fraction & 0x00100000u) ? EM_SDK_SOFT_FLOAT_CLASS_QNAN
                                            : EM_SDK_SOFT_FLOAT_CLASS_SNAN;
        dst->fraction = fraction;                              /* 0x12792C: the raw 23 bits */
        return;
    }
    dst->fraction = (fraction << 7) | 0x40000000u;             /* 0x127930 / 0x127934 */
    dst->exp = (int32_t)biased - 0x7F;                         /* 0x127938 */
    dst->cls = EM_SDK_SOFT_FLOAT_CLASS_NUMBER;                 /* 0x12793C */
}

/* 00126AB8: packs the double record. */
uint64_t em_sdk_soft_float_00126AB8(const EmSdkSoftFloatPartsD *src)
{
    const uint32_t cls = src->cls;                             /* 0x126AB8 */
    uint64_t fraction = src->fraction;                         /* 0x126ACC */
    uint32_t biased = 0;                                       /* 0x126ABC */

    if (cls < 2u) {                                            /* 0x126AC4: a NaN */
        fraction |= QNAN_D;                                    /* 0x126AE0: no guard shift */
        biased = 0x7FFu;                                       /* 0x126AD8 */
    } else if (cls == EM_SDK_SOFT_FLOAT_CLASS_INFINITY) {      /* 0x126AE4 */
        biased = 0x7FFu;                                       /* 0x126AEC */
        fraction = 0;                                          /* 0x126B48 */
    } else if (cls == EM_SDK_SOFT_FLOAT_CLASS_ZERO) {          /* 0x126AF0 */
        fraction = 0;                                          /* 0x126B00 */
    } else if (fraction != 0) {                                /* 0x126B04: zero keeps biased 0 */
        const int32_t exp = src->exp;                          /* 0x126B0C */
        if (exp < -0x3FE) {                                    /* 0x126B10: below the normal range */
            /* 0x126B1C: the 32-bit shift count; 0x126B28 shifts by its low
             * six bits, kept only when it is below 57. No rounding. */
            const int32_t shift = (int32_t)((uint32_t)-0x3FE - (uint32_t)exp);
            fraction = shift < 0x39 ? fraction >> ((uint32_t)shift & 63u) : 0;
            fraction >>= 8;                                    /* 0x126B84 */
        } else if (!(exp < 0x400)) {                           /* 0x126B34: overflow */
            biased = 0x7FFu;                                   /* 0x126B40 */
            fraction = 0;                                      /* 0x126B48 */
        } else {
            biased = (uint32_t)exp + 0x3FFu;                   /* 0x126B3C */
            if ((fraction & 0xFFu) != 0x80u)                   /* 0x126B54 */
                fraction += 0x7F;                              /* 0x126B58 */
            else if (fraction & 0x100u)                        /* 0x126B5C..0x126B64: tie to even */
                fraction += 0x80;
            if (ROUND_LIMIT_D < fraction) {                    /* 0x126B70: the carry out */
                fraction >>= 1;                                /* 0x126B7C */
                biased += 1;                                   /* 0x126B80 */
            }
            fraction >>= 8;                                    /* 0x126B78 / 0x126B84 */
        }
    }
    /* 0x126B88..0x126BE0. The incoming a2 register is merged in at 0x126B9C
     * and then cleared bit by bit: bits 52..62 by the and at 0x126BC4, bit 63
     * by the and at 0x126BD8. What remains is sign, exponent and fraction. */
    return ((uint64_t)(src->sign & 1u) << 63) |                /* 0x126BD4 */
           ((uint64_t)(biased & 0x7FFu) << 52) |               /* 0x126BA4 / 0x126BC0 */
           (fraction & FRAC52);                                /* 0x126B98 */
}

/* 00127728: stores its four arguments as a double record (the 64-bit
 * fraction at +0x10) and packs it with 00126AB8. */
uint64_t em_sdk_soft_float_00127728(uint32_t cls, uint32_t sign, int32_t exp, uint64_t fraction)
{
    EmSdkSoftFloatPartsD parts;
    memset(&parts, 0, sizeof parts);
    parts.cls = cls;                                           /* 0x12772C */
    parts.sign = sign;                                         /* 0x127738 */
    parts.exp = exp;                                           /* 0x12773C */
    parts.fraction = fraction;                                 /* 0x127744 */
    return em_sdk_soft_float_00126AB8(&parts);                 /* 0x127740 */
}

/* 00128350: stores f12 on its stack, unpacks it with 001278C0 and passes
 * the record to 00127728, the 32-bit fraction widened to 64 bits and moved
 * up by 30 (0x128370 / 0x128380). */
uint64_t em_sdk_soft_float_00128350(uint32_t x)
{
    EmSdkSoftFloatPartsF parts;
    memset(&parts, 0, sizeof parts);
    em_sdk_soft_float_001278C0(x, &parts);                     /* 0x128360 */
    return em_sdk_soft_float_00127728(parts.cls, parts.sign, parts.exp,
                                      (uint64_t)parts.fraction << 30);  /* 0x12837C */
}

/* ---------------------------------------------------------------------------
 * 0011DB90: matherr.
 * ------------------------------------------------------------------------- */

/* 00126BE8: unpacks the double the first argument points at. The sign is
 * stored before the first test (0x126C00). */
void em_sdk_soft_float_00126BE8(uint64_t value, EmSdkSoftFloatPartsD *dst)
{
    uint64_t fraction = value & FRAC52;                        /* 0x126BF8 */
    const uint32_t biased = (uint32_t)(value >> 52) & 0x7FFu;  /* 0x126BFC / 0x126C04 */
    dst->sign = (uint32_t)(value >> 63);                       /* 0x126BF4 / 0x126C00 */
    if (biased == 0) {                                         /* 0x126C08: a denormal is zero too */
        dst->cls = EM_SDK_SOFT_FLOAT_CLASS_ZERO;               /* 0x126C10 */
        return;
    }
    if (biased == 0x7FFu) {                                    /* 0x126C20 */
        if (fraction == 0) {                                   /* 0x126C28 */
            dst->cls = EM_SDK_SOFT_FLOAT_CLASS_INFINITY;       /* 0x126C2C */
            return;
        }
        dst->cls = (fraction & QNAN_D) ? EM_SDK_SOFT_FLOAT_CLASS_QNAN   /* 0x126C40 */
                                       : EM_SDK_SOFT_FLOAT_CLASS_SNAN;
        dst->fraction = fraction;                              /* 0x126C5C: unshifted */
        return;
    }
    fraction = (fraction << 8) | HIDDEN_D;                     /* 0x126C24 / 0x126C68 */
    dst->exp = (int32_t)biased - 0x3FF;                        /* 0x126C6C */
    dst->fraction = fraction;                                  /* 0x126C74 */
    dst->cls = EM_SDK_SOFT_FLOAT_CLASS_NUMBER;                 /* 0x126C80 */
}

/* The two returns that pick on a sign word: 0x1273E0 / 0x127450 give -1
 * when it is nonzero, else 1; 0x12747C and 0x127424 / 0x1273F8 the reverse. */
static int32_t minus_if(uint32_t sign) { return sign != 0 ? -1 : 1; }
static int32_t plus_if(uint32_t sign) { return sign != 0 ? 1 : -1; }

/* 00127398: the three-way compare of two double records. */
int32_t em_sdk_soft_float_00127398(const EmSdkSoftFloatPartsD *a, const EmSdkSoftFloatPartsD *b)
{
    const uint32_t ca = a->cls, cb = b->cls;
    if (ca < 2u || cb < 2u)                                    /* 0x12739C / 0x1273AC */
        return 1;                                              /* 0x1273BC */
    if (ca == EM_SDK_SOFT_FLOAT_CLASS_INFINITY) {              /* 0x1273C0 */
        if (cb != EM_SDK_SOFT_FLOAT_CLASS_INFINITY)            /* 0x1273C8 */
            return minus_if(a->sign);                          /* 0x1273E0 */
        return (int32_t)(b->sign - a->sign);                   /* 0x1273DC */
    }
    if (cb == EM_SDK_SOFT_FLOAT_CLASS_INFINITY)                /* 0x1273F0 */
        return plus_if(b->sign);                               /* 0x1273F8 */
    if (ca == EM_SDK_SOFT_FLOAT_CLASS_ZERO) {                  /* 0x12740C */
        if (cb != EM_SDK_SOFT_FLOAT_CLASS_ZERO)                /* 0x127414 */
            return plus_if(b->sign);                           /* 0x127424 */
        return 0;                                              /* 0x127420 */
    }
    if (cb == EM_SDK_SOFT_FLOAT_CLASS_ZERO)                    /* 0x127434 */
        return minus_if(a->sign);                              /* 0x1273E0 */
    const uint32_t sa = a->sign;                               /* 0x12743C */
    if (sa != b->sign)                                         /* 0x127444 */
        return minus_if(sa);                                   /* 0x127450 */
    if (b->exp < a->exp)                                       /* 0x127460 */
        return minus_if(sa);                                   /* 0x127450 */
    if (a->exp < b->exp)                                       /* 0x12746C */
        return plus_if(sa);                                    /* 0x12747C */
    if (b->fraction < a->fraction)                             /* 0x12748C: unsigned */
        return minus_if(sa);                                   /* 0x127450 */
    if (a->fraction < b->fraction)                             /* 0x127498 */
        return plus_if(sa);                                    /* 0x12747C */
    return 0;                                                  /* 0x1274A8 */
}

/* 001274B0: unpacks both doubles into its stack (+0 and +0x20) and returns
 * 00127398 of the two records (v0 passes through). */
int32_t em_sdk_soft_float_001274B0(uint64_t a, uint64_t b)
{
    EmSdkSoftFloatPartsD pa, pb;
    memset(&pa, 0, sizeof pa);
    memset(&pb, 0, sizeof pb);
    em_sdk_soft_float_00126BE8(a, &pa);                        /* 0x1274C8 */
    em_sdk_soft_float_00126BE8(b, &pb);                        /* 0x1274D8 */
    return em_sdk_soft_float_00127398(&pa, &pb);               /* 0x1274E4 */
}

/* 0011DB90: loads the record's arg1 (+8, 0x11DB98), calls 001274B0 with it
 * as both arguments, ignores the result and returns 0 (0x11DBA8). It writes
 * nothing outside its own stack. */
int32_t em_sdk_soft_float_0011DB90(const EmSdkMathException *record)
{
    (void)em_sdk_soft_float_001274B0(record->arg1, record->arg1);  /* 0x11DB9C */
    return 0;
}

/* ---------------------------------------------------------------------------
 * 00127758: double -> float.
 * ------------------------------------------------------------------------- */

/* 001277B0: packs the float record (the word is moved to f0 at 0x1278B0). */
uint32_t em_sdk_soft_float_001277B0(const EmSdkSoftFloatPartsF *src)
{
    const uint32_t cls = src->cls;                             /* 0x1277B0 */
    uint32_t fraction = src->fraction;                         /* 0x1277C4 */
    uint32_t biased = 0;                                       /* 0x1277B4 */

    if (cls < 2u) {                                            /* 0x1277BC: a NaN */
        fraction |= 0x00100000u;                               /* 0x1277D4: no guard shift */
        biased = 0xFFu;                                        /* 0x1277CC */
    } else if (cls == EM_SDK_SOFT_FLOAT_CLASS_INFINITY) {      /* 0x1277DC */
        biased = 0xFFu;                                        /* 0x127830 */
        fraction = 0;                                          /* 0x127838 */
    } else if (cls == EM_SDK_SOFT_FLOAT_CLASS_ZERO) {          /* 0x1277E4 */
        fraction = 0;                                          /* 0x1277F0 */
    } else if (fraction != 0) {                                /* 0x1277F4: zero keeps biased 0 */
        const int32_t exp = src->exp;                          /* 0x1277FC */
        if (exp < -0x7E) {                                     /* 0x127800: below the normal range */
            /* 0x12780C: the 32-bit shift count; 0x127818 shifts by its low
             * five bits, kept only when it is below 26. No rounding. */
            const int32_t shift = (int32_t)((uint32_t)-0x7E - (uint32_t)exp);
            fraction = shift < 0x1A ? fraction >> ((uint32_t)shift & 31u) : 0;
            fraction >>= 7;                                    /* 0x127868 */
        } else if (!(exp < 0x80)) {                            /* 0x127824: overflow */
            biased = 0xFFu;                                    /* 0x127830 */
            fraction = 0;                                      /* 0x127838 */
        } else {
            biased = (uint32_t)exp + 0x7Fu;                    /* 0x12782C */
            if ((fraction & 0x7Fu) != 0x40u)                   /* 0x127844 */
                fraction += 0x3F;                              /* 0x127848 */
            else if (fraction & 0x80u)                         /* 0x12784C..0x127854: tie to even */
                fraction += 0x40;
            if (fraction & 0x80000000u) {                      /* 0x127858: the 32-bit sign */
                fraction >>= 1;                                /* 0x127860 */
                biased += 1;                                   /* 0x127864 */
            }
            fraction >>= 7;                                    /* 0x12785C / 0x127868 */
        }
    }
    /* 0x12786C..0x1278AC. The incoming a2 register is merged in at 0x127884
     * and then cleared: bits 23..30 by the and at 0x127890, bit 31 and the
     * upper word by the and at 0x1278A8. */
    return (src->sign << 31) |                                 /* 0x1278A4 */
           ((biased & 0xFFu) << 23) |                          /* 0x12788C / 0x127894 */
           (fraction & 0x007FFFFFu);                           /* 0x12787C */
}

/* 00128320: stores its four arguments as a float record and packs it with
 * 001277B0. */
uint32_t em_sdk_soft_float_00128320(uint32_t cls, uint32_t sign, int32_t exp, uint32_t fraction)
{
    EmSdkSoftFloatPartsF parts;
    parts.cls = cls;                                           /* 0x128324 */
    parts.sign = sign;                                         /* 0x128330 */
    parts.exp = exp;                                           /* 0x128334 */
    parts.fraction = fraction;                                 /* 0x12833C */
    return em_sdk_soft_float_001277B0(&parts);                 /* 0x128338 */
}

/* 00127758: stores a0 on its stack, unpacks it with 00126BE8, narrows the
 * 64-bit fraction to 32 bits (bits 30..61, 0x127780 / 0x127784) with a
 * sticky bit 0 for any of the low 30 bits (0x12778C..0x12779C), and packs
 * with 00128320. */
uint32_t em_sdk_soft_float_00127758(uint64_t value)
{
    EmSdkSoftFloatPartsD parts;
    memset(&parts, 0, sizeof parts);
    em_sdk_soft_float_00126BE8(value, &parts);                 /* 0x127768 */
    const uint32_t high = (uint32_t)(parts.fraction >> 30);
    const uint32_t fraction = (parts.fraction & UINT64_C(0x3FFFFFFF)) != 0 ? high | 1u : high;
    return em_sdk_soft_float_00128320(parts.cls, parts.sign, parts.exp, fraction);  /* 0x127798 */
}

/* ---------------------------------------------------------------------------
 * Data and the worker adapters.
 * ------------------------------------------------------------------------- */

int em_sdk_soft_float_load_d24295C(const uint8_t *elf, size_t size, uint32_t *out)
{
    static const uint8_t magic[4] = { 0x7F, 'E', 'L', 'F' };
    const size_t offset = 0x0024295Cu - 0x00100000u + 0x300u;   /* LOAD vaddr 0x100000 at file 0x300 */
    if (!elf || !out || size != EM_SDK_MATH_ELF_SIZE || memcmp(elf, magic, 4) != 0)
        return -1;
    *out = (uint32_t)elf[offset] | (uint32_t)elf[offset + 1] << 8 |
           (uint32_t)elf[offset + 2] << 16 | (uint32_t)elf[offset + 3] << 24;
    return 0;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int em_sdk_soft_float_load_export(const char *path, uint32_t *d24295C, int32_t *errno_word)
{
    uint8_t bytes[24];
    if (!path || !d24295C || !errno_word)
        return -1;
    FILE *file = fopen(path, "rb");
    if (!file)
        return -1;
    int ok = fread(bytes, 1, sizeof bytes, file) == sizeof bytes && fgetc(file) == EOF;
    fclose(file);
    /* 'EMSF', version 1, (D_0024295C, pointer), (pointer, cell word). */
    ok = ok && !memcmp(bytes, "EMSF", 4) && le32(bytes + 4) == 1u &&
         le32(bytes + 8) == 0x0024295Cu && le32(bytes + 16) == le32(bytes + 12);
    if (!ok)
        return -1;
    *d24295C = le32(bytes + 12);
    *errno_word = (int32_t)le32(bytes + 20);
    return 0;
}

static int adapter_fail(void *context, uint32_t address)
{
    EmSdkSoftFloatContext *c = context;
    if (c && c->fault == 0)
        c->fault = address;
    return -1;
}

int em_sdk_soft_float_w_00128350(void *context, float x, uint64_t *result)
{
    if (!result)
        return adapter_fail(context, 0x00128350u);
    *result = em_sdk_soft_float_00128350(em_ee_bits(x));       /* the f12 word, stored at 0x12835C */
    return 0;
}

int em_sdk_soft_float_w_0011DB90(void *context, EmSdkMathException *record, int32_t *result)
{
    if (!record || !result)
        return adapter_fail(context, 0x0011DB90u);
    *result = em_sdk_soft_float_0011DB90(record);
    return 0;
}

/* 0011FD78 returns the word at D_0024295C (0x11FD80). The wrappers store
 * through it, so the adapter hands out the host cell that stands for that
 * EE address, and faults when there is none. */
int em_sdk_soft_float_w_0011FD78(void *context, int32_t **cell)
{
    EmSdkSoftFloatContext *c = context;
    if (!c || !cell || !c->d24295C)
        return adapter_fail(context, 0x0011FD78u);
    const uint32_t address = *c->d24295C;                      /* 0x11FD80 */
    if (!c->errno_cell || address != c->errno_address)
        return adapter_fail(context, 0x0011FD78u);
    *cell = c->errno_cell;
    return 0;
}

int em_sdk_soft_float_w_00127758(void *context, uint64_t value, float *result)
{
    if (!result)
        return adapter_fail(context, 0x00127758u);
    *result = em_ee_float(em_sdk_soft_float_00127758(value));  /* the word in f0 (0x1278B0) */
    return 0;
}

int em_sdk_soft_float_bind(EmSdkMathWorkers *workers, EmSdkSoftFloatContext *context)
{
    if (!workers || !context)
        return -1;
    workers->context = context;
    workers->w_00128350 = em_sdk_soft_float_w_00128350;
    workers->w_0011DB90 = em_sdk_soft_float_w_0011DB90;
    workers->w_0011FD78 = em_sdk_soft_float_w_0011FD78;
    workers->w_00127758 = em_sdk_soft_float_w_00127758;
    return 0;
}
