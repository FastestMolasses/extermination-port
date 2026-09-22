#include "game/em_cinematic_camera.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

static uint32_t le32(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static float le_float(const unsigned char *p)
{
    uint32_t bits = le32(p);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

int em_cinematic_camera_load(EmCinematicCamera *camera, const char *path)
{
    unsigned char header[16], bytes[32];
    EmCinematicCamera next = {0};
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    if (fread(header, 1, sizeof header, file) != sizeof header ||
        memcmp(header, "EMCC", 4) || le32(header + 4) != 1) goto fail;
    next.sample_count = le32(header + 8);
    next.duration = le_float(header + 12);
    if (next.sample_count < 2 || next.sample_count > 1000000 ||
        !isfinite(next.duration) || next.duration != next.sample_count - 1)
        goto fail;
    next.samples = malloc((size_t)next.sample_count * 8 * sizeof(float));
    if (!next.samples) goto fail;
    for (uint32_t i = 0; i < next.sample_count; ++i) {
        if (fread(bytes, 1, sizeof bytes, file) != sizeof bytes) goto fail;
        for (unsigned j = 0; j < 8; ++j) {
            float value = le_float(bytes + 4 * j);
            if (!isfinite(value)) goto fail;
            next.samples[i * 8 + j] = value;
        }
    }
    if (fgetc(file) != EOF) goto fail;
    fclose(file);
    em_cinematic_camera_free(camera);
    *camera = next;
    return 0;
fail:
    fclose(file);
    free(next.samples);
    return -1;
}

void em_cinematic_camera_free(EmCinematicCamera *camera)
{
    free(camera->samples);
    memset(camera, 0, sizeof *camera);
}

/* R5900's finite camera arithmetic rounds toward zero at each operation.
 * Double precision preserves these binary32 camera intermediates; correct
 * a host cast that rounded away from zero. This is scoped to finite camera
 * coordinates, not a general implementation of the PS2 FPU. */
static float camera_float(double value)
{
    float result = (float)value;
    if ((value > 0 && (double)result > value) ||
        (value < 0 && (double)result < value))
        result = nextafterf(result, 0.0f);
    return result;
}

static float interpolate(float a, float b, float fraction)
{
    float difference = camera_float((double)b - a);
    float scaled = camera_float((double)fraction * difference);
    return camera_float((double)scaled + a);
}

int em_cinematic_camera_sample(const EmCinematicCamera *camera, float time,
                              EmCinematicCameraFrame *frame)
{
    if (!camera || !camera->samples || camera->sample_count < 2 || !frame ||
        !isfinite(time) || camera->duration != camera->sample_count - 1)
        return -1;
    unsigned index;
    float fraction;
    int active = time >= 0 && time < camera->duration;
    if (time < 0) { index = 0; fraction = 0; }
    else if (!active) { index = (unsigned)camera->duration - 1; fraction = 0; }
    else { index = (unsigned)time; fraction = time - index; }
    const float *a = camera->samples + index * 8;
    const float *b = a + 8;
    if (!active) {
        memcpy(frame->eye, a, sizeof frame->eye);
        memcpy(frame->target, a + 3, sizeof frame->target);
        frame->roll_degrees = a[6];
        frame->fov_degrees = a[7];
        frame->cut = 0;
        return 0;
    }
    frame->cut = active && a[7] < 0;
    if (frame->cut) fraction = 0;
    for (unsigned i = 0; i < 3; ++i) {
        frame->eye[i] = interpolate(a[i], b[i], fraction);
        frame->target[i] = interpolate(a[i + 3], b[i + 3], fraction);
    }
    frame->roll_degrees = a[6];
    frame->fov_degrees = interpolate(fabsf(a[7]), fabsf(b[7]), fraction);
    return active;
}
