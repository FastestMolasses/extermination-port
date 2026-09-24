/* Contract test for em_sdk_soft_float (docs/SDK_SOFT_FLOAT.md), under
 * ASan/UBSan. The original-instruction evidence is
 * tools/test_sdk_soft_float_reference.py; this test checks the values the
 * doc states (each measured there against the original), the adapter
 * protocol with em_sdk_math_original, and runs every entry point over a
 * random sweep for undefined behaviour.
 *
 * Usage: sdk_soft_float_test <path to the user's SCUS_971.12> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_sdk_soft_float.h"

static int failures;

#define EXPECT(cond)                                                                 \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: FAILED %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                              \
        }                                                                            \
    } while (0)

static uint64_t state = 0x128350u;
static uint64_t next64(void)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

static uint32_t bits(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }
static float value(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = n > 0 ? malloc((size_t)n) : NULL;
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
    fclose(f);
    *size = data ? (size_t)n : 0;
    return data;
}

static int write_file(const char *path, const uint8_t *bytes, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file)
        return -1;
    const int ok = fwrite(bytes, 1, size, file) == size;
    return fclose(file) == 0 && ok ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s SCUS_971.12\n", argv[0]);
        return 2;
    }
    size_t size = 0;
    uint8_t *elf = read_file(argv[1], &size);
    if (!elf) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }

    /* Values stated in the doc (section 2), each an oracle result. */
    EXPECT(em_sdk_soft_float_00128350(0x3F800000u) == UINT64_C(0x3FF0000000000000));
    EXPECT(em_sdk_soft_float_00128350(0x00000001u) == 0);                        /* a denormal is zero */
    EXPECT(em_sdk_soft_float_00128350(0x80000001u) == UINT64_C(0x8000000000000000));
    EXPECT(em_sdk_soft_float_00128350(0xFF800000u) == UINT64_C(0xFFF0000000000000));
    EXPECT(em_sdk_soft_float_00128350(0x7F800001u) == UINT64_C(0x7FF8000040000000));
    EXPECT(em_sdk_soft_float_00127758(0) == 0);
    EXPECT(em_sdk_soft_float_00127758(UINT64_C(0x7FF8000000000000)) == 0x7FB00000u);
    EXPECT(em_sdk_soft_float_00127758(UINT64_C(0x47EFFFFFF0000000)) == 0x7F800000u);  /* rounds up to Inf */
    EXPECT(em_sdk_soft_float_00127758(UINT64_C(0x36A0000000000000)) == 0x00000001u);  /* float denormal, truncated */
    EXPECT(em_sdk_soft_float_001274B0(UINT64_C(0x7FF8000000000000), 0) == 1);
    EXPECT(em_sdk_soft_float_001274B0(0, UINT64_C(0x8000000000000000)) == 0);
    EXPECT(em_sdk_soft_float_001274B0(UINT64_C(0xBFF0000000000000), 0) == -1);
    EXPECT(em_sdk_soft_float_001274B0(UINT64_C(0x7FF0000000000000), UINT64_C(0xFFF0000000000000)) == 1);

    /* Every normal float survives the widening and the narrowing. */
    for (uint32_t e = 1; e < 255; ++e)
        for (int i = 0; i < 64; ++i) {
            const uint32_t x = (uint32_t)(next64() & 0x807FFFFFu) | e << 23;
            EXPECT(em_sdk_soft_float_00127758(em_sdk_soft_float_00128350(x)) == x);
        }

    /* Loader and the 0011FD78 adapter. */
    uint32_t pointer = 0;
    EXPECT(em_sdk_soft_float_load_d24295C(elf, size, &pointer) == 0 && pointer == 0x00242670u);
    EXPECT(em_sdk_soft_float_load_d24295C(elf, size - 1, &pointer) == -1);

    /* The export loader (assets/sdk_soft_float.emsf's layout), over files
     * built here from the ELF's D_0024295C: the well-formed one and one
     * defect each. */
    {
        uint8_t good[24];
        const uint32_t words[5] = { 1u, 0x0024295Cu, pointer, pointer, 0u };
        memcpy(good, "EMSF", 4);
        for (int i = 0; i < 5; ++i)
            for (int b = 0; b < 4; ++b)
                good[4 + 4 * i + b] = (uint8_t)(words[i] >> (8 * b));
        const char *path = "build/sdk_soft_float/export_case.emsf";
        uint32_t got_pointer = 0;
        int32_t got_word = -1;
        EXPECT(write_file(path, good, sizeof good) == 0);
        EXPECT(em_sdk_soft_float_load_export(path, &got_pointer, &got_word) == 0 &&
               got_pointer == 0x00242670u && got_word == 0);
        EXPECT(em_sdk_soft_float_load_export(NULL, &got_pointer, &got_word) == -1);
        EXPECT(em_sdk_soft_float_load_export(path, NULL, &got_word) == -1);
        EXPECT(em_sdk_soft_float_load_export(path, &got_pointer, NULL) == -1);
        EXPECT(em_sdk_soft_float_load_export("build/sdk_soft_float/no_such.emsf", &got_pointer, &got_word) == -1);
        /* a wrong magic, version, first address or second address */
        const int bytes[4] = { 0, 4, 8, 16 };
        for (int i = 0; i < 4; ++i) {
            uint8_t bad[24];
            memcpy(bad, good, sizeof bad);
            bad[bytes[i]] ^= 0x04;
            got_pointer = 0x1234u;
            EXPECT(write_file(path, bad, sizeof bad) == 0);
            EXPECT(em_sdk_soft_float_load_export(path, &got_pointer, &got_word) == -1 && got_pointer == 0x1234u);
        }
        EXPECT(write_file(path, good, sizeof good - 1) == 0);                  /* short */
        EXPECT(em_sdk_soft_float_load_export(path, &got_pointer, &got_word) == -1);
        uint8_t longer[25];
        memcpy(longer, good, sizeof good);
        longer[24] = 0;
        EXPECT(write_file(path, longer, sizeof longer) == 0);                  /* trailing byte */
        EXPECT(em_sdk_soft_float_load_export(path, &got_pointer, &got_word) == -1);
        remove(path);
    }

    /* The wrappers with this module bound (D_0026C5D0 = 1 as in the ELF). */
    static EmSdkMathTables tables;
    EXPECT(em_sdk_math_original_load_tables(elf, size, &tables) == 0);
    int32_t errno_word = 0, mode = 1;
    EmSdkSoftFloatContext soft = { &pointer, 0x00242670u, &errno_word, 0 };
    EmSdkMathContext sdk;
    memset(&sdk, 0, sizeof sdk);
    sdk.tables = &tables;
    sdk.world.d26C5D0 = &mode;
    EXPECT(em_sdk_soft_float_bind(&sdk.workers, &soft) == 0);
    EXPECT(bits(em_sdk_math_original_float_0011E620(&sdk, 0.0f, 0.0f)) == 0 && sdk.fault == 0 && errno_word == 0x21);
    errno_word = 0;
    EXPECT(bits(em_sdk_math_original_float_0011E620(&sdk, value(0x80000001u), value(0x00000001u))) == 0 &&
           errno_word == 0x21);                                                   /* a denormal pair */
    errno_word = 0;
    EXPECT(bits(em_sdk_math_original_float_0011E748(&sdk, -1.0f)) == 0x7FB00000u && sdk.fault == 0 &&
           errno_word == 0x21);
    errno_word = 0;
    EXPECT(bits(em_sdk_math_original_float_0011E748(&sdk, value(0x80000001u))) == 0x7F7FFFFFu && errno_word == 0);
    mode = -1;
    EXPECT(em_sdk_math_original_float_0011E748(&sdk, 4.0f) == 2.0f && errno_word == 0);
    EXPECT(bits(em_sdk_math_original_float_0011E748(&sdk, -1.0f)) == 0x7F7FFFFFu && errno_word == 0);
    mode = 1;
    soft.errno_address = 0x00242674u;                                             /* no cell for 0x242670 */
    (void)em_sdk_math_original_float_0011E748(&sdk, -1.0f);
    EXPECT(sdk.fault == 0x0011FD78u && soft.fault == 0x0011FD78u);

    /* Undefined-behaviour sweep over every entry point. */
    uint64_t sink = 0;
    for (int i = 0; i < 200000; ++i) {
        const uint64_t r = next64(), s = next64();
        EmSdkSoftFloatPartsF pf = { (uint32_t)r, (uint32_t)(r >> 32), (int32_t)s, (uint32_t)(s >> 32) };
        EmSdkSoftFloatPartsD pd = { (uint32_t)s, (uint32_t)(s >> 32), (int32_t)r, 0, next64() };
        EmSdkSoftFloatPartsD pe = { (uint32_t)(i % 7), (uint32_t)(r & 1), (int32_t)(s % 2200) - 1100, 0, r };
        sink += em_sdk_soft_float_00128350((uint32_t)r) + em_sdk_soft_float_00127758(s);
        sink += (uint64_t)em_sdk_soft_float_001274B0(r, s) + em_sdk_soft_float_00126AB8(&pd) +
                em_sdk_soft_float_00126AB8(&pe) + em_sdk_soft_float_001277B0(&pf);
        sink += (uint64_t)em_sdk_soft_float_00127398(&pd, &pe) + (uint64_t)em_sdk_soft_float_00127398(&pe, &pd);
        sink += em_sdk_soft_float_00127728(pf.cls, pf.sign, pf.exp, r) +
                em_sdk_soft_float_00128320(pf.cls, pf.sign, (int32_t)(s % 400) - 200, (uint32_t)r);
        em_sdk_soft_float_001278C0((uint32_t)s, &pf);
        em_sdk_soft_float_00126BE8(r, &pd);
        EmSdkMathException e = { 1, 0x0026C640u, r, s, 0, 0 };
        sink += (uint64_t)em_sdk_soft_float_0011DB90(&e);
    }
    printf("sdk_soft_float_test: %s (sweep %llx)\n", failures ? "FAILED" : "ok", (unsigned long long)(sink & 0xFFFF));
    free(elf);
    return failures ? 1 : 0;
}
