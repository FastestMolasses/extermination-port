/* The production effect export loader and bounded read provider over the
 * AREA00 arrival's added windows (tools/test_area00_arrival_bindings.py):
 * 0022DCD0's record list D_00269400 (six records and the terminator word)
 * and its 0x90-byte record D_0026A900, and 0015A2C0's config rows 3 and 4
 * (D_00248120 + 20 * 3, 40 bytes), and (since 2026-10-19, later) the
 * class-2 owners' D_00242EB0..D_00242F20 (00128C10's clip tables and
 * 00128600's byte rows) and 0012CAA0's grab table D_00242DD0..D_00242EAF
 * (since 2026-10-28). argv[1] holds the expected bytes, in that order;
 * argv[2] is 0 for an export without these windows. */
#define main earlier_effects_fixture_main
#include "aim_fire_effects_live_test.c"
#undef main

static const struct { uint32_t base, size; } k_windows[] = {
    {0x00269400u, 0x184u}, {0x0026A900u, 0x90u}, {0x0024815Cu, 0x28u},
    {0x00242EB0u, 0x70u}, {0x00242DD0u, 0xE0u},
};
enum { WINDOWS = sizeof k_windows / sizeof k_windows[0] };

int main(int argc, char **argv)
{
    assert(argc == 3);
    const int present = atoi(argv[2]);
    uint8_t expected[0x184 + 0x90 + 0x28 + 0x70 + 0xE0];
    FILE *f = fopen(argv[1], "rb");
    assert(f && fread(expected, 1, sizeof expected, f) == sizeof expected);
    assert(fgetc(f) == EOF);
    fclose(f);
    assert(em_effects_live_load() == 0);
    if (!present) {
        for (unsigned i = 0; i < WINDOWS; ++i) assert(!em_effects_live_window(k_windows[i].base, 1));
        puts("PASS prior export refuses the absent arrival windows");
        return 0;
    }
    unsigned slices = 0, refusals = 0, start = 0;
    for (unsigned i = 0; i < WINDOWS; ++i) {
        const uint32_t base = k_windows[i].base, size = k_windows[i].size;
        for (uint32_t offset = 0; offset < size; ++offset)
            for (uint32_t n = 1; n <= size - offset && n <= 0x40u; ++n) {
                const uint8_t *p = em_effects_live_window(base + offset, n);
                assert(p && !memcmp(p, expected + start + offset, n));
                ++slices;
            }
        /* The whole window in one read. */
        const uint8_t *all = em_effects_live_window(base, size);
        assert(all && !memcmp(all, expected + start, size));
        /* One byte past the end, and a read across it. */
        assert(!em_effects_live_window(base + size - 1, 2)); ++refusals;
        start += size;
    }
    /* Before each window: D_002693FF and D_0026A8FF are not exported, and
     * config row 2 (D_00248148..D_0024815B) is not either. */
    assert(!em_effects_live_window(0x002693FFu, 1)); ++refusals;
    assert(!em_effects_live_window(0x0026A8FFu, 1)); ++refusals;
    assert(!em_effects_live_window(0x00248148u, 20)); ++refusals;
    assert(!em_effects_live_window(0x0024815Bu, 2)); ++refusals;
    /* D_00242DCF is not exported; 0012CAA0's grab table ends where the
     * class-2 window starts, and the class-2 window's end meets the
     * D_00242F20 clip-id window: each side reads, a span across does not. */
    assert(!em_effects_live_window(0x00242DCFu, 1)); ++refusals;
    assert(em_effects_live_window(0x00242EAFu, 1) && em_effects_live_window(0x00242EB0u, 1));
    assert(!em_effects_live_window(0x00242EAFu, 2)); ++refusals;
    assert(em_effects_live_window(0x00242F1Fu, 1) && em_effects_live_window(0x00242F20u, 1));
    /* Row 4's end meets the AREA01 row-5 window: each side reads, a span
     * across the two windows does not. */
    assert(em_effects_live_window(0x00248183u, 1) && em_effects_live_window(0x00248184u, 1));
    assert(!em_effects_live_window(0x00248183u, 2)); ++refusals;
    printf("PASS production arrival read views: %u slices, %u bounds refusals\n", slices, refusals);
    return 0;
}
