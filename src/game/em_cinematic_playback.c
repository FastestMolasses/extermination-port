#include "game/em_cinematic_playback.h"
#include "game/em_camera_rotation.h"
#include "game/em_effect_color.h"

#include <stdio.h>
#include <string.h>

static float add(float a, float b) { return em_effect_float32((double)a + b); }
static float multiply(float a, float b) { return em_effect_float32((double)a * b); }
/* DIV.S follows the measured nearest-rounded path, unlike the separate
 * scalar add/multiply truncation above. Captured projection checks pin it. */
static float divide(float a, float b) { return (float)((double)a / b); }

static uint32_t word(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int em_cinematic_projection_load(EmCinematicProjection *projection, const char *path)
{
    if (!projection || !path) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    unsigned char raw[64];
    int valid = fread(raw, 1, sizeof raw, file) == sizeof raw &&
        !memcmp(raw, "EMCP", 4) && word(raw+4) == 1 && word(raw+8) == 13 &&
        fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (!valid) return 0;
    EmCinematicProjection next;
    for (unsigned i = 0; i < 13; ++i) {
        uint32_t bits = word(raw+12+4*i);
        memcpy(&next.tangent[i], &bits, 4);
        if (!isfinite(next.tangent[i])) return 0;
    }
    *projection = next;
    return 1;
}

float em_cinematic_projection_zoom(const EmCinematicProjection *projection, float fov)
{
    if (!projection || !isfinite(fov)) return NAN;
    if (fov > 45) fov = 45;
    else if (fov < .5f) fov = .5f;
    float x = divide(multiply(0x1.921fb6p+1f, divide(fov, 1.45f)), 180);
    /* The camera's clamped input stays below the tangent kernel's angle
     * reduction threshold. 11E398 therefore calls11D878(x,0,1) directly. */
    const float *t = projection->tangent;
    float z = multiply(x, x), w = multiply(z, z);
    float r = t[11], v = t[12];
    for (int i = 9; i >= 1; i -= 2) r = add(t[i], multiply(w, r));
    for (int i = 10; i >= 2; i -= 2) v = add(t[i], multiply(w, v));
    v = multiply(z, v);
    float s = multiply(z, x);
    r = add(0, multiply(z, add(multiply(s, add(r, v)), 0)));
    r = add(r, multiply(t[0], s));
    return divide(224, add(x, r));
}

int em_cinematic_playback_start(EmCinematicPlayback *playback,
                                 const EmCinematicCamera *track, int scene_id)
{
    if (!playback || !track || !track->samples || track->sample_count < 2 ||
        track->duration != track->sample_count - 1 || scene_id != 1) return 0;
    playback->track = track;
    playback->time = 0;
    return 1;
}

static int restore(EmCinematicPlayback *playback, EmCinematicPlaybackEmit emit, void *context)
{
    if (emit(context, EM_CINEMATIC_CAMERA_RESTORE_ROOM, playback) != 1 ||
        emit(context, EM_CINEMATIC_CAMERA_EFFECT_OFF, playback) != 1 ||
        emit(context, EM_CINEMATIC_CAMERA_FLAG_OFF, playback) != 1) return 0;
    playback->zoom = 480;
    playback->up[0] = playback->up[2] = 0;
    playback->up[1] = -1;
    playback->up[3] = 1;
    return 1;
}

int em_cinematic_playback_wait(EmCinematicPlayback *playback,
                               EmCinematicPlaybackEmit emit, void *context)
{
    if (!playback || !playback->track || !emit || !isfinite(playback->time)) return -1;
    if (playback->time < playback->track->duration) return 0;
    return restore(playback, emit, context) ? 1 : -1;
}

int em_cinematic_playback_tick(EmCinematicPlayback *playback,
    const EmCinematicProjection *projection, EmCinematicPlaybackEmit emit, void *context)
{
    if (!playback || !projection || !emit) return -1;
    EmCinematicCameraFrame sample;
    int active = em_cinematic_camera_sample(playback->track, playback->time, &sample);
    if (active < 0) return -1;
    if (!active) {
        playback->time = playback->track->duration;
        return restore(playback, emit, context) ? 0 : -1;
    }
    playback->auxiliary = (uint8_t)sample.cut;
    if (sample.cut) playback->cut_counter = 32;
    memcpy(playback->eye, sample.eye, sizeof sample.eye);
    memcpy(playback->target, sample.target, sizeof sample.target);
    if (emit(context, EM_CINEMATIC_CAMERA_PUBLISH, playback) != 1) return -1;
    float angle = divide(multiply(0x1.921fb6p+1f, sample.roll_degrees), 180);
    float angles[3] = {angle, 0, 0}, matrix[16], unused[4];
    if (!em_camera_rotation_offset(angles, 0, matrix, unused)) return -1;
    for (unsigned row = 0; row < 4; ++row) {
        float value = add(multiply(matrix[row], 0), multiply(matrix[4+row], -1));
        value = add(value, multiply(matrix[8+row], 0));
        playback->up[row] = add(value, matrix[12+row]);
    }
    playback->zoom = em_cinematic_projection_zoom(projection, sample.fov_degrees);
    if (!isfinite(playback->zoom)) return -1;
    playback->time = add(playback->time, .5f);
    return 1;
}
