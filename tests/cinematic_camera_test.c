#include "game/em_cinematic_camera.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void word(FILE *file, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) fputc((int)(value >> (i * 8) & 255), file);
}

static void value(FILE *file, float number)
{
    uint32_t bits;
    memcpy(&bits, &number, sizeof bits);
    word(file, bits);
}

int main(void)
{
    float samples[] = {0, 1, 2, 3, 4, 5, 6, 10,
                      2, 3, 4, 5, 6, 7, 8, -20,
                      20, 30, 40, 50, 60, 70, 80, 30};
    EmCinematicCamera track = {samples, 3, 2};
    EmCinematicCameraFrame frame;
    assert(em_cinematic_camera_sample(&track, .5f, &frame) == 1);
    assert(frame.eye[0] == 1 && frame.target[2] == 6);
    assert(frame.fov_degrees == 15 && frame.roll_degrees == 6 && !frame.cut);
    assert(em_cinematic_camera_sample(&track, 1.5f, &frame) == 1);
    assert(frame.eye[0] == 2 && frame.target[2] == 7 && frame.cut);
    assert(frame.fov_degrees == 20 && frame.roll_degrees == 8);
    assert(em_cinematic_camera_sample(&track, 2, &frame) == 0);
    assert(frame.eye[0] == 2 && frame.fov_degrees == -20 && !frame.cut);
    assert(em_cinematic_camera_sample(&track, -1, &frame) == 0);
    assert(frame.eye[0] == 0 && frame.fov_degrees == 10);
    assert(em_cinematic_camera_sample(&track, NAN, &frame) == -1);

    /* Halfway between adjacent floats rounds toward zero, including when
     * host round-to-nearest-even would choose the larger-magnitude value. */
    samples[0] = nextafterf(1.0f, 2.0f);
    samples[8] = nextafterf(samples[0], 2.0f);
    assert(em_cinematic_camera_sample(&track, .5f, &frame) == 1);
    assert(frame.eye[0] == samples[0]);

    char path[] = "/tmp/em_camera_test_XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "wb");
    assert(file);
    fwrite("EMCC", 1, 4, file); word(file, 1); word(file, 3); value(file, 2);
    for (unsigned i = 0; i < 24; ++i) value(file, samples[i]);
    fclose(file);
    EmCinematicCamera loaded = {0};
    assert(em_cinematic_camera_load(&loaded, path) == 0);
    assert(loaded.duration == 2 && loaded.sample_count == 3);
    assert(!memcmp(loaded.samples, samples, sizeof samples));
    file = fopen(path, "ab"); assert(file); fputc(0, file); fclose(file);
    assert(em_cinematic_camera_load(&loaded, path) == -1);
    assert(loaded.duration == 2 && !memcmp(loaded.samples, samples, sizeof samples));
    em_cinematic_camera_free(&loaded);
    unlink(path);
    puts("cinematic camera PASS (interpolation, cuts, roll, endpoints, rounding, load)");
    return 0;
}
