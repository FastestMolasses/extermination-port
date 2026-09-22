#include "game/em_snow_particles.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void read_original(FILE *file, unsigned address, void *out, size_t size)
{
    assert(fseek(file, (long)address - 0x100000 + 0x300, SEEK_SET) == 0);
    assert(fread(out, 1, size, file) == size);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    float descriptor[9][4], lookup[80];
    read_original(file, 0x255170, descriptor, sizeof descriptor);
    read_original(file, 0x2342bc, lookup, sizeof lookup);
    fclose(file);
    float matrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float params[4] = {1.5f,1.0f,0.000001f,0.234f};
    struct {
        uint32_t before;
        EmSnowParticle particles[28];
        uint32_t after;
    } storage = {0};
    storage.before = 0x12345678;
    storage.after = 0x87654321;
    assert(em_snow_particles_generate(NULL, lookup, params, matrix,
                                      storage.particles, 28) == -1);
    assert(em_snow_particles_generate(descriptor, lookup, params, matrix,
                                      storage.particles, 0) == -1);
    unsigned iterations = 0;
    for (uint32_t count = 1; count <= 28; ++count) {
        memcpy(&descriptor[8][0], &count, sizeof count);
        for (uint32_t flags = 0; flags < 32; ++flags) {
            memcpy(&descriptor[8][2], &flags, sizeof flags);
            for (int phase = -2; phase < 8; ++phase) {
                params[0] = phase * 0.5f;
                int emitted = em_snow_particles_generate(descriptor, lookup,
                    params, matrix, storage.particles, count);
                assert(emitted >= 0 && emitted <= (int)count);
                for (int i = 0; i < emitted; ++i) {
                    assert(storage.particles[i].source_index < count);
                    if (i) assert(storage.particles[i-1].source_index <
                                  storage.particles[i].source_index);
                    for (unsigned c = 0; c < 4; ++c) {
                        assert(isfinite(storage.particles[i].position[c]));
                        assert(isfinite(storage.particles[i].color[c]));
                        assert(isfinite(storage.particles[i].half_size[c]));
                    }
                }
                assert(storage.before == 0x12345678);
                assert(storage.after == 0x87654321);
                ++iterations;
            }
        }
    }
    printf("snow particle bounds/sanitizer fixture: %u cases PASS\n", iterations);
    return 0;
}
