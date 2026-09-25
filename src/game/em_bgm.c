/* em_bgm.c — the shared audio output device and the WAV reader. See
 * em_bgm.h. The callback only sums the producers' lock-free outputs; it
 * does no allocation, locking or I/O. */
#include "game/em_bgm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_audio.h"
#include "game/em_sfx.h"
#include "game/em_startup_audio.h"
#include "game/em_stream_live.h"

static struct {
    EmAudio *audio;
    int device_rate; /* set before em_audio_create, constant while it exists */
} s;

/* The render callback (audio thread): silence, then every producer adds. */
static void bgm_render(void *user, float *out, int frames)
{
    (void)user;
    memset(out, 0, sizeof(float) * 2u * (size_t)frames);
    em_sfx_mix(out, frames, s.device_rate);
    em_startup_audio_mix(out, frames, s.device_rate);
    em_stream_live_mix(out, frames, s.device_rate);
}

/* Minimal RIFF/WAVE reader: PCM16 only, mono/stereo, zero dependencies.
 * The whole file is loaded so the audio thread never touches the disk.
 * Little-endian host assumed (every port target is). Returns 0 and fills
 * *out (pcm malloc'd) on success. SHARED with em_sfx (em_bgm.h): `tag`
 * keeps each caller's diagnostics prefix ("bgm"/"sfx") byte-identical. */
int em_bgm_wav_read(const char *path, EmBgmWav *out, const char *tag)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "%s: cannot open %s\n", tag, path);
        return -1;
    }
    unsigned char hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) != 0 ||
        memcmp(hdr + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "%s: %s is not a RIFF/WAVE file\n", tag, path);
        fclose(f);
        return -1;
    }

    uint16_t fmt = 0, channels = 0, bits = 0;
    uint32_t rate = 0, data_size = 0;
    int16_t *pcm = NULL;
    unsigned char ch[8];
    while (fread(ch, 1, 8, f) == 8) {
        uint32_t size = (uint32_t)ch[4] | (uint32_t)ch[5] << 8 |
                        (uint32_t)ch[6] << 16 | (uint32_t)ch[7] << 24;
        if (memcmp(ch, "fmt ", 4) == 0 && size >= 16) {
            unsigned char fc[16];
            if (fread(fc, 1, 16, f) != 16)
                break;
            fmt      = (uint16_t)(fc[0] | fc[1] << 8);
            channels = (uint16_t)(fc[2] | fc[3] << 8);
            rate     = (uint32_t)fc[4] | (uint32_t)fc[5] << 8 |
                       (uint32_t)fc[6] << 16 | (uint32_t)fc[7] << 24;
            bits     = (uint16_t)(fc[14] | fc[15] << 8);
            if (fseek(f, (long)(size - 16 + (size & 1)), SEEK_CUR) != 0)
                break;
        } else if (memcmp(ch, "data", 4) == 0) {
            pcm = malloc(size);
            if (!pcm || fread(pcm, 1, size, f) != size) {
                free(pcm);
                pcm = NULL;
                break;
            }
            data_size = size;
            break;                          /* fmt always precedes data */
        } else {
            if (fseek(f, (long)(size + (size & 1)), SEEK_CUR) != 0)
                break;
        }
    }
    fclose(f);

    if (fmt != 1 || bits != 16 || (channels != 1 && channels != 2) ||
        rate == 0 || !pcm || data_size < (uint32_t)(channels * 2u)) {
        fprintf(stderr, "%s: %s unsupported (need PCM16 mono/stereo; "
                        "got fmt=%u bits=%u ch=%u rate=%u data=%u)\n",
                tag, path, fmt, bits, channels, rate, data_size);
        free(pcm);
        return -1;
    }
    out->pcm      = pcm;
    out->nframes  = (long)(data_size / (channels * 2u));
    out->channels = channels;
    out->rate     = (int)rate;
    return 0;
}

int em_bgm_device_ensure(int sample_rate)
{
    if (s.audio) return 0;
    s.device_rate = sample_rate;
    s.audio = em_audio_create(sample_rate, bgm_render, NULL);
    if (!s.audio) {
        fprintf(stderr, "bgm: audio device creation failed\n");
        s.device_rate = 0;
        return -1;
    }
    return 0;
}

int em_bgm_device_rate(void)
{
    return s.audio ? s.device_rate : 0;
}

void em_bgm_shutdown(void)
{
    if (s.audio) {
        em_audio_destroy(s.audio);  /* blocks: no callback after this */
        s.audio = NULL;
    }
    s.device_rate = 0;
}
