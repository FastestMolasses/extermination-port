/* Exercise the production effect export loader and bounded read provider. */
#define main earlier_effects_fixture_main
#include "aim_fire_effects_live_test.c"
#undef main

int main(int argc, char **argv)
{
    assert(argc == 3);
    const int present = atoi(argv[2]);
    uint8_t expected[46];
    FILE *f = fopen(argv[1], "rb");
    assert(f && fread(expected, 1, sizeof expected, f) == sizeof expected);
    assert(fgetc(f) == EOF);
    fclose(f);
    assert(em_effects_live_load() == 0);
    const uint32_t base = 0x00248290u;
    const unsigned surface_size = 40;
    if (!present) {
        assert(!em_effects_live_window(base, surface_size));
        puts("PASS prior export refuses the absent surface table");
        return 0;
    }
    unsigned slices = 0;
    for (unsigned offset = 0; offset < surface_size; ++offset) {
        for (unsigned size = 1; size <= surface_size - offset; ++size) {
            const uint8_t *p = em_effects_live_window(base + offset, size);
            assert(p && !memcmp(p, expected + offset, size));
            ++slices;
        }
    }
    assert(!em_effects_live_window(base - 1, 1));
    assert(!em_effects_live_window(base - 1, 2));
    assert(!em_effects_live_window(base + surface_size, 1));
    assert(!em_effects_live_window(base + surface_size - 1, 2));
    assert(!em_effects_live_window(UINT32_MAX - 3, 8));
    const uint32_t roots[] = {0x0024A850u, 0x0024D820u};
    unsigned start = surface_size, root_slices = 0;
    for (unsigned i = 0; i < 2; ++i) {
        unsigned length = i ? 4u : 2u;
        for (unsigned offset = 0; offset < length; ++offset) {
            for (unsigned size = 1; size <= length - offset; ++size) {
                const uint8_t *p = em_effects_live_window(roots[i] + offset, size);
                assert(p && !memcmp(p, expected + start + offset, size));
                root_slices++;
            }
        }
        assert(!em_effects_live_window(roots[i] + length - 1, 2));
        start += length;
    }
    assert(root_slices == 13);
    printf("PASS production surface read view: %u slices, 5 bounds refusals\n", slices);
    puts("PASS production crate roots: 13 slices, 2 crossing refusals");
    return 0;
}
