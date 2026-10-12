/* gs_swizzle_test - the CPU GS model's separable address tables
 * (src/gs/em_gs_raster.c section 1b, docs/PERFORMANCE.md M1) against its
 * address functions.
 *
 * The drawing path reads and writes the frame (PSMCT32 / CT24), the Z buffer
 * (PSMZ32 / Z24) and the textures (PSMCT32, PSMT8, PSMT4) through
 * bp * unit + row[bw][y] + col[s][x], wrapped by the 4 MiB mask. This proves
 * that sum (em_gs_swz_addr, the same inline code the drawing path runs)
 * equal to em_gs_addr32 / em_gs_addr8 / em_gs_addr4 (the documented page /
 * block / column tables, which test_gs_raster_reference.py checks against
 * the measured maps and uploads):
 *
 *   1. every x and y (0..2047) at each width, with a base block that varies
 *      with x, y and the width (default: widths 0, 1, 2, 4, 8, 10, 16, 63;
 *      EM_TEST_FULL=1: every width 0..63);
 *   2. every base block 0..16383, each at sampled positions and widths
 *      (EM_TEST_FULL=1: also every x < 128, y < 64 at widths 1 and 8);
 *   3. positions past 2047 (the functions wrap x and y at 2048);
 *   4. negative controls: the T8 / T4 sum with the other column table, and
 *      the CT32 tables against the Z32 function, must differ somewhere.
 *
 * Default about 2 s; EM_TEST_FULL=1 about 25 s (2.15 billion addresses). */
#include "gs/em_gs_raster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
static unsigned long long checks;

static uint32_t ref(unsigned psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    switch (psm) {
    case EM_GS_PSMCT32: case EM_GS_PSMCT24: return em_gs_addr32(bp, bw, x, y, 0);
    case EM_GS_PSMZ32: case EM_GS_PSMZ24: return em_gs_addr32(bp, bw, x, y, 1);
    case EM_GS_PSMT8: return em_gs_addr8(bp, bw, x, y);
    default: return em_gs_addr4(bp, bw, x, y);
    }
}

static void check(EmGs *g, unsigned psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    const uint32_t want = ref(psm, bp, bw, x, y), got = em_gs_swz_addr(g, psm, bp, bw, x, y);
    ++checks;
    if (want != got && fails++ < 20)
        printf("  FAIL psm %#x bp %u bw %u (%u, %u): function %#x, tables %#x\n", psm, bp, bw, x, y, want, got);
}

static uint64_t rs = 0x2545F4914F6CDD1Dull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 20); }

int main(void)
{
    const char *full_env = getenv("EM_TEST_FULL");
    const int full = full_env && full_env[0] && strcmp(full_env, "0") != 0;
    printf("gs_swizzle_test (%s)\n", full ? "full" : "quick");
    static const unsigned psms[] = {EM_GS_PSMCT32, EM_GS_PSMZ32, EM_GS_PSMT8, EM_GS_PSMT4};
    static const unsigned aliases[] = {EM_GS_PSMCT24, EM_GS_PSMZ24};
    static const uint32_t widths[] = {0, 1, 2, 4, 8, 10, 16, 63};
    uint8_t *mem = calloc(1, EM_GS_MEM_BYTES);
    EmGs g;
    em_gs_init(&g, mem);

    /* 1. every position at each width */
    const unsigned nw = full ? 64 : (unsigned)(sizeof widths / sizeof widths[0]);
    for (unsigned p = 0; p < 4; ++p) {
        const unsigned long long c0 = checks;
        const int f0 = fails;
        for (unsigned i = 0; i < nw; ++i) {
            const uint32_t bw = full ? i : widths[i];
            for (uint32_t y = 0; y < 2048; ++y)
                for (uint32_t x = 0; x < 2048; ++x)
                    check(&g, psms[p], (x * 2654435761u + y * 40503u + bw * 7919u) >> 7 & 16383u, bw, x, y);
        }
        printf("  psm 0x%02x: every x, y at %u widths: %llu addresses, %d differ\n", psms[p], nw, checks - c0,
               fails - f0);
    }
    /* the CT24 / Z24 aliases share the CT32 / Z32 tables */
    for (unsigned a = 0; a < 2; ++a)
        for (uint32_t y = 0; y < 2048; y += 3)
            for (uint32_t x = 0; x < 2048; x += 5)
                check(&g, aliases[a], (x + y * 3) & 16383u, 8, x, y);

    /* 2. every base block */
    {
        const unsigned long long c0 = checks;
        const int f0 = fails;
        for (uint32_t bp = 0; bp < 16384; ++bp)
            for (unsigned p = 0; p < 4; ++p) {
                for (unsigned k = 0; k < 64; ++k)
                    check(&g, psms[p], bp, rnd() & 63u, rnd() & 2047u, rnd() & 2047u);
                if (full)
                    for (uint32_t bw = 1; bw <= 8; bw += 7)
                        for (uint32_t y = 0; y < 64; ++y)
                            for (uint32_t x = 0; x < 128; ++x)
                                check(&g, psms[p], bp, bw, x, y);
            }
        printf("  every base block 0..16383: %llu addresses, %d differ\n", checks - c0, fails - f0);
    }

    /* 3. positions past 2047 */
    for (unsigned k = 0; k < 1u << 20; ++k) {
        const uint32_t x = rnd() & 0xFFFFu, y = rnd() & 0xFFFFu;
        check(&g, psms[k & 3], rnd() & 16383u, rnd() & 63u, x, y);
    }

    /* 4. negative controls: a wrong sum must be seen */
    {
        int t8_wrong = 0, t4_wrong = 0, z_wrong = 0;
        for (uint32_t y = 0; y < 16; ++y)
            for (uint32_t x = 0; x < 32; ++x) {
                /* the other column table: the row's own entry at (0, y), plus
                 * the column entries of row y ^ 2 (s = bit 1 ^ bit 2 of y, so
                 * that row reads the other table) */
                const uint32_t y2 = y ^ 2;
                const uint32_t t8 = (em_gs_swz_addr(&g, EM_GS_PSMT8, 0, 1, 0, y)
                                     + em_gs_swz_addr(&g, EM_GS_PSMT8, 0, 1, x, y2)
                                     - em_gs_swz_addr(&g, EM_GS_PSMT8, 0, 1, 0, y2)) & (EM_GS_MEM_BYTES - 1);
                const uint32_t t4 = (em_gs_swz_addr(&g, EM_GS_PSMT4, 0, 1, 0, y)
                                     + em_gs_swz_addr(&g, EM_GS_PSMT4, 0, 1, x, y2)
                                     - em_gs_swz_addr(&g, EM_GS_PSMT4, 0, 1, 0, y2)) & (2 * EM_GS_MEM_BYTES - 1);
                t8_wrong += t8 != em_gs_addr8(0, 1, x, y);
                t4_wrong += t4 != em_gs_addr4(0, 1, x, y);
                z_wrong += em_gs_swz_addr(&g, EM_GS_PSMCT32, 0, 1, x, y) != em_gs_addr32(0, 1, x, y, 1);
            }
        printf("  negative controls: the other column table differs at %d (T8) and %d (T4) of 512, "
               "the CT32 tables from the Z32 function at %d of 512\n", t8_wrong, t4_wrong, z_wrong);
        if (!t8_wrong || !t4_wrong || !z_wrong) {
            printf("  FAIL a negative control was not seen\n");
            ++fails;
        }
    }
    em_gs_release(&g);
    free(mem);
    printf("%llu addresses compared; %s\n", checks, fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
