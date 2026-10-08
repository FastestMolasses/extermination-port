#include "game/em_cinematic_playback.h"
#include "game/em_camera_rotation.h"
#include "game/em_effect_color.h"
#include "game/em_ee_float.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The scalar arithmetic of 0022EEF0 and the SDK tangent 0011E398 / 0011D878
 * is COP1: em_ee_float.h, the measured EE model (docs/EE_FLOAT_MODEL.md;
 * DIV.S rounds to nearest, which the captured projection checks pin). */
static float add(float a, float b) { return em_ee_add(a, b); }
static float multiply(float a, float b) { return em_ee_mul(a, b); }
static float divide(float a, float b) { return em_ee_div(a, b); }
/* 001026A0's VU0 lanes (truncated per operation). */
static float vu_add(float a, float b) { return em_effect_float32((double)a + b); }
static float vu_multiply(float a, float b) { return em_effect_float32((double)a * b); }

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

/* em_cinematic_events_load: the EMFX words (see the header). */
int em_cinematic_events_load(EmCinematicEvents *events, float *storage, size_t capacity,
                             uint32_t base, const char *path)
{
    if (!events || !storage || !path || (base & 3u)) return -1;
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    unsigned char header[12];
    int valid = fread(header, 1, sizeof header, file) == sizeof header &&
        !memcmp(header, "EMFX", 4) && word(header+4) == 1;
    uint32_t count = valid ? word(header+8) : 0;
    valid = valid && count && count <= capacity;
    float next[64];
    if (valid && count > sizeof next / sizeof next[0]) valid = 0;
    for (uint32_t i = 0; valid && i < count; ++i) {
        unsigned char raw[4];
        if (fread(raw, 1, 4, file) != 4) { valid = 0; break; }
        uint32_t bits = word(raw);
        memcpy(&next[i], &bits, 4);
        if (!isfinite(next[i])) valid = 0;
    }
    valid = valid && fgetc(file) == EOF && !ferror(file) && next[count - 1] == 0;
    fclose(file);
    if (!valid) return -1;
    memcpy(storage, next, count * sizeof next[0]);
    events->base = base;
    events->words = storage;
    events->count = count;
    return 0;
}

/* The word at the original address `at` of the events window, or NULL
 * outside it. */
static const float *event_word(const EmCinematicPlayback *playback, uint32_t at)
{
    const EmCinematicEvents *e = playback->events;
    if (!e || !e->words || at < e->base || (at - e->base) & 3u || (at - e->base) / 4u >= e->count)
        return NULL;
    return &e->words[(at - e->base) / 4u];
}

/* The scenes whose 0022EC30 arm binds no table and which have no 0022EEF0
 * cue: 0, 1, 3, 4, 9, 11, 14, 18 and 36. */
static int plain_scene(int scene)
{
    switch (scene) {
    case 0: case 1: case 3: case 4: case 9: case 11: case 14: case 18: case 36: return 1;
    default: return 0;
    }
}

int em_cinematic_playback_start(EmCinematicPlayback *playback, const EmCinematicCamera *track,
                                const EmCinematicEvents *events, int scene_id,
                                uint64_t start_clock)
{
    if (!playback || !track || !track->samples || track->sample_count < 2 ||
        track->duration != track->sample_count - 1) return 0;
    uint32_t c80 = 0;
    if (scene_id == EM_CINEMATIC_SCENE_OPENING) c80 = EM_CINEMATIC_OPENING_EVENTS;
    else if (!plain_scene(scene_id)) return 0;       /* not admitted */
    EmCinematicPlayback next = *playback;
    next.track = track;
    next.events = events;
    next.scene = (int16_t)scene_id;
    /* D_00275C98 = 0021BAB0(); +0x7C / +0x80 / +0x84 = 0; +0x88 / +0x8A /
     * +0x89 = 0; then the scene's tables. */
    next.start_clock = start_clock;
    next.cursor[0] = next.cursor[1] = next.cursor[2] = 0;
    next.state[0] = next.state[1] = next.state[2] = 0;
    next.cursor[1] = c80;
    if (c80 && !event_word(&next, c80)) return 0;
    *playback = next;
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

/* float_to_int (libgcc __fixsfsi): truncation toward zero; NaN 0;
 * |x| >= 2**31 and the infinities saturate by the sign. */
static int32_t fix_sfsi(float x)
{
    if (isnan(x)) return 0;
    if (x >= 2147483648.0f) return INT32_MAX;
    if (x <= -2147483648.0f) return INT32_MIN;
    return (int32_t)x;
}

static int call(EmCinematicPlayback *playback, EmCinematicPlaybackEmit emit, void *context,
                EmCinematicPlaybackEvent event, int32_t a0, int32_t a1, float value)
{
    playback->arg[0] = a0;
    playback->arg[1] = a1;
    playback->value = value;
    return emit(context, event, playback) == 1;
}

/* The track's next cursor after the word `next`: 0 at the 0.0 terminator. */
static int step_cursor(EmCinematicPlayback *playback, unsigned track, uint32_t next)
{
    const float *w = event_word(playback, next);
    if (!w) return 0;
    playback->cursor[track] = em_ee_c_eq(*w, 0.0f) ? 0 : next;
    return 1;
}

/* 0022EEF0's three event tracks, in its order. 1, or 0 on a fault. */
static int event_tracks(EmCinematicPlayback *playback, EmCinematicPlaybackEmit emit, void *context)
{
    const float *p;
    /* +0x7C: the screen effect (state +0x88). */
    if (playback->cursor[0]) {
        if (!(p = event_word(playback, playback->cursor[0]))) return 0;
        if (em_ee_c_le(*p, playback->time)) {
            uint32_t at = playback->cursor[0] + 4;
            if (playback->state[0] == 0) {
                if (!(p = event_word(playback, at))) return 0;
                float v = *p;
                at += 4;
                if (em_ee_c_eq(v, -1.0f)) {
                    if (!call(playback, emit, context, EM_CINEMATIC_CAMERA_EFFECT_GREY, 0x80, 0x80, 0)) return 0;
                    playback->state[0] = 2;
                } else {
                    if (!call(playback, emit, context, EM_CINEMATIC_CAMERA_EFFECT, 5, 0, v)) return 0;
                    playback->state[0] = 1;
                }
            } else {
                if (playback->state[0] == 1) {
                    if (!call(playback, emit, context, EM_CINEMATIC_CAMERA_EFFECT_OFF, 0, 0, 0)) return 0;
                } else if (!call(playback, emit, context, EM_CINEMATIC_CAMERA_EFFECT_CLOCK, 0, 0, 0)) {
                    return 0;
                }
                playback->state[0] = 0;
            }
            if (!step_cursor(playback, 0, at)) return 0;
        }
    }
    /* +0x80: the fades (state +0x89: bit 0 out / in, bit 7 the colour). */
    if (playback->cursor[1]) {
        if (!(p = event_word(playback, playback->cursor[1]))) return 0;
        float t = *p;
        if (em_ee_c_lt(t, 0.0f)) {
            if (em_ee_c_eq(t, -3.0f)) playback->state[1] = 0;
            else if (em_ee_c_eq(t, -4.0f)) playback->state[1] = 0x80;
            else if (em_ee_c_eq(t, -1.0f)) playback->state[1] = 1;
            else if (em_ee_c_eq(t, -2.0f)) playback->state[1] = 0x81;
            playback->cursor[1] += 4;
        } else if (em_ee_c_le(t, playback->time)) {
            uint32_t at = playback->cursor[1] + 4;
            if (!(p = event_word(playback, at))) return 0;
            uint8_t f = playback->state[1];
            int32_t speed = fix_sfsi(*p);
            at += 4;
            if (!call(playback, emit, context, (f & 1) ? EM_CINEMATIC_CAMERA_FADE_OUT : EM_CINEMATIC_CAMERA_FADE_IN,
                      speed, (f & 0x80) ? 1 : 0, 0)) return 0;
            f = playback->state[1];
            playback->state[1] = (uint8_t)((f & 0x80) + (1 - (f & 1)));
            if (!step_cursor(playback, 1, at)) return 0;
        }
    }
    /* +0x84: the letterbox flag (state +0x8A). */
    if (playback->cursor[2]) {
        if (!(p = event_word(playback, playback->cursor[2]))) return 0;
        if (em_ee_c_le(*p, playback->time)) {
            if (!call(playback, emit, context,
                      playback->state[2] == 0 ? EM_CINEMATIC_CAMERA_FLAG_ON : EM_CINEMATIC_CAMERA_FLAG_OFF,
                      2, playback->state[2] == 0 ? 1 : 0, 0)) return 0;
            playback->state[2] = (uint8_t)(1 - playback->state[2]);
            if (!step_cursor(playback, 2, playback->cursor[2] + 4)) return 0;
        }
    }
    return 1;
}

int em_cinematic_playback_events(EmCinematicPlayback *playback, EmCinematicPlaybackEmit emit,
                                 void *context)
{
    if (!playback || !emit || !playback->track) return -1;
    /* The per-scene cues: scene 0x22's 001B1E20(6, 0) at the cursor 1.0;
     * the plain scenes have none. Any other scene's cues are not
     * translated. */
    if (playback->scene == EM_CINEMATIC_SCENE_OPENING) {
        if (em_ee_c_eq(playback->time, 1.0f) &&
            !call(playback, emit, context, EM_CINEMATIC_CAMERA_RUMBLE, 6, 0, 0)) return -1;
    } else if (!plain_scene(playback->scene)) {
        return -1;
    }
    return event_tracks(playback, emit, context) ? 0 : -1;
}

int em_cinematic_playback_tick(EmCinematicPlayback *playback,
    const EmCinematicProjection *projection, EmCinematicPlaybackEmit emit, void *context)
{
    if (!playback || !projection || !emit || !playback->track) return -1;
    if (em_cinematic_playback_events(playback, emit, context) < 0) return -1;
    EmCinematicCameraFrame sample;
    int active = em_cinematic_camera_sample(playback->track, playback->time, &sample);
    if (active < 0) return -1;
    return em_cinematic_playback_sampled_tick(playback, projection, emit, context, &sample, active, NULL);
}

int em_cinematic_playback_sampled_tick(EmCinematicPlayback *playback,
    const EmCinematicProjection *projection, EmCinematicPlaybackEmit emit, void *context,
    const EmCinematicCameraFrame *sample, int active, float rotation[16])
{
    if (!playback || !playback->track || !projection || !emit || !sample || active < 0) return -1;
    if (!active) {
        playback->time = playback->track->duration;
        return restore(playback, emit, context) ? 0 : -1;
    }
    playback->auxiliary = (uint8_t)sample->cut;
    if (sample->cut) playback->cut_counter = 32;
    memcpy(playback->eye, sample->eye, sizeof sample->eye);
    memcpy(playback->target, sample->target, sizeof sample->target);
    if (emit(context, EM_CINEMATIC_CAMERA_PUBLISH, playback) != 1) return -1;
    float angle = divide(multiply(0x1.921fb6p+1f, sample->roll_degrees), 180);
    float angles[3] = {angle, 0, 0}, matrix[16], unused[4];
    if (!em_camera_rotation_offset(angles, 0, matrix, unused)) return -1;
    if (rotation) memcpy(rotation, matrix, sizeof matrix);
    for (unsigned row = 0; row < 4; ++row) {
        float value = vu_add(vu_multiply(matrix[row], 0), vu_multiply(matrix[4+row], -1));
        value = vu_add(value, vu_multiply(matrix[8+row], 0));
        playback->up[row] = vu_add(value, matrix[12+row]);
    }
    playback->zoom = em_cinematic_projection_zoom(projection, sample->fov_degrees);
    if (!isfinite(playback->zoom)) return -1;
    playback->time = add(playback->time, .5f);
    return 1;
}
