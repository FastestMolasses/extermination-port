#include "game/em_opening_media.h"
#include "game/em_bgm.h"
#include "game/em_frame.h"
#include "game/em_sfx.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

enum { MAX_FADE_VALUES = 64 };

static struct {
    char resume_path[1024];
    EmBgmWav wav; /* immutable between successful prepare and final shutdown */
    float fade_values[MAX_FADE_VALUES];
    unsigned fade_count, fade_next, fade_mode;
    int prepared, armed, playing;
    uint8_t *hold;                /* D_008106F4 */
    float volume, volume_step;
    atomic_uint serial;
    atomic_int play;
    atomic_int audio_volume;
    unsigned audio_serial;
    double audio_position;
    int audio_play;
} s;

static unsigned u16(const unsigned char *b)
{ return (unsigned)b[0] | (unsigned)b[1] << 8; }
static unsigned u32(const unsigned char *b)
{ return u16(b) | u16(b + 2) << 16; }

static int read_fades(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char h[12];
    if (!f) return -1;
    int okay = fread(h, 1, sizeof h, f) == sizeof h &&
               !memcmp(h, "EMFX", 4) && u32(h + 4) == 1;
    unsigned count = okay ? u32(h + 8) : 0;
    okay = count && count <= MAX_FADE_VALUES &&
           fread(s.fade_values, sizeof(float), count, f) == count &&
           fgetc(f) == EOF;
    fclose(f);
    if (!okay || s.fade_values[count - 1] != 0) return -1;
    for (unsigned i = 0; i < count; i++)
        if (!isfinite(s.fade_values[i])) return -1;
    s.fade_count = count;
    return 0;
}

int em_opening_media_prepare(const char *directory)
{
    if (s.prepared) return 0;
    char path[1024];
    if (!directory) return -1;
    if (snprintf(path, sizeof path, "%s/opening.emfx", directory) >= (int)sizeof path || read_fades(path))
        goto fail;
    if (snprintf(path, sizeof path, "%s/opening.wav", directory) >= (int)sizeof path ||
        em_bgm_wav_read(path, &s.wav, "opening") || s.wav.rate != 48000 ||
        s.wav.channels != 2 || s.wav.nframes <= 0) goto fail;
    if (em_bgm_device_ensure(48000)) goto fail;
    if (snprintf(s.resume_path, sizeof s.resume_path, "%s/opening_resume.wav", directory) >= (int)sizeof s.resume_path)
        goto fail;
    s.prepared = 1;
    return 0;
fail:
    fprintf(stderr, "opening: missing or malformed media in %s\n", directory);
    free(s.wav.pcm); memset(&s.wav, 0, sizeof s.wav);
    return -1;
}

/* The lane stand-in at step H (em_bgm_set_lane_service): see the header. */
static void lane_service(void *context)
{
    (void)context;
    if (!s.armed || s.playing || !s.hold) return;
    if (*s.hold == 2) {
        *s.hold = 1;
    } else if (*s.hold == 0) {
        s.playing = 1;
        atomic_store_explicit(&s.play, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&s.serial, 1, memory_order_release);
    }
}

void em_opening_media_set_hold(uint8_t *hold)
{
    s.hold = hold;
    em_bgm_set_lane_service(hold ? lane_service : NULL, NULL);
}

int em_opening_media_audio_start(void)
{
    if (!s.prepared) return -1;
    em_bgm_stop(0);
    em_sfx_stop_all();
    em_opening_media_stop();
    s.armed = 1;
    s.volume = 16383.0f;
    s.volume_step = 0.0f;
    atomic_store_explicit(&s.audio_volume, 16383, memory_order_relaxed);
    s.fade_next = s.fade_mode = 0;
    return 0;
}

void em_opening_media_tick(void)
{
    if (s.volume_step != 0.0f) {
        s.volume += s.volume_step;
        if (s.volume <= 0.0f) {
            s.volume = s.volume_step = 0.0f;
            atomic_store_explicit(&s.play, 0, memory_order_relaxed);
            atomic_fetch_add_explicit(&s.serial, 1, memory_order_release);
        }
        atomic_store_explicit(&s.audio_volume, (int)s.volume, memory_order_relaxed);
    }
}

void em_opening_media_fade_out(int ticks)
{
    if (!s.armed) return;
    s.volume_step = ticks > 0 ? -16383.0f / (float)ticks : -16383.0f;
}

int em_opening_media_resume_music(unsigned fade_ticks)
{
    if (!s.prepared) return -1;
    em_opening_media_stop();
    return em_bgm_play_ticks(s.resume_path, 1, fade_ticks);
}

void em_opening_media_camera_tick(float time)
{
    if (!s.armed || s.fade_next >= s.fade_count) return;
    float value = s.fade_values[s.fade_next];
    if (value < 0) {
        if (value == -3) s.fade_mode = 0;
        else if (value == -4) s.fade_mode = 0x80;
        else if (value == -1) s.fade_mode = 1;
        else if (value == -2) s.fade_mode = 0x81;
        s.fade_next++;
    } else if (time >= value && s.fade_next + 1 < s.fade_count) {
        int speed = (int)s.fade_values[s.fade_next + 1];
        em_frame_fade_start_colour((s.fade_mode & 1) ? 1 : -1,
                                   speed, (s.fade_mode & 0x80) != 0);
        s.fade_mode ^= 1;
        s.fade_next += 2;
        if (s.fade_next < s.fade_count && s.fade_values[s.fade_next] == 0)
            s.fade_next = s.fade_count;
    }
}

void em_opening_media_stop(void)
{
    s.armed = s.playing = 0;
    s.volume_step = 0.0f;
    atomic_store_explicit(&s.play, 0, memory_order_relaxed);
    atomic_fetch_add_explicit(&s.serial, 1, memory_order_release);
}

void em_opening_media_shutdown(void)
{
    /* The device has joined, so both callback-only and producer state
     * can now be reset before a later initialization. */
    em_opening_media_stop();
    s.audio_play = 0;
    s.audio_position = 0;
    free(s.wav.pcm); memset(&s.wav, 0, sizeof s.wav);
    s.prepared = 0;
}

void em_opening_media_mix(float *out, int frames, int rate)
{
    unsigned serial = atomic_load_explicit(&s.serial, memory_order_acquire);
    if (serial != s.audio_serial) {
        s.audio_serial = serial;
        s.audio_play = atomic_load_explicit(&s.play, memory_order_relaxed);
        s.audio_position = 0;
    }
    if (!s.audio_play || rate <= 0) return;
    float gain = atomic_load_explicit(&s.audio_volume, memory_order_relaxed) / 16383.0f;
    double step = (double)s.wav.rate / rate;
    for (int i = 0; i < frames; i++) {
        long pos = (long)s.audio_position;
        if (pos >= s.wav.nframes) { s.audio_play = 0; break; }
        out[i * 2] += s.wav.pcm[pos * 2] / 32768.0f * gain;
        out[i * 2 + 1] += s.wav.pcm[pos * 2 + 1] / 32768.0f * gain;
        s.audio_position += step;
    }
}
