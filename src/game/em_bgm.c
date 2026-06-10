/* em_bgm.c — background-music service implementation. See em_bgm.h for
 * the engine model (D_00810D38 current-BGM + func_001FAE70 fade restart)
 * and the threading design.
 *
 * Lock-free publication scheme (single producer = game thread, single
 * consumer = the OS audio thread, per the em_audio.h contract):
 *
 *   game thread                          audio thread (callback)
 *   -----------                          -----------------------
 *   build BgmTrack (malloc + file I/O)
 *   store s.cur        (release)
 *   store s.serial     (release)  --->   load s.serial (acquire) each call;
 *                                        on mismatch, FADE OUT the track it
 *                                        is holding (~1 s), then adopt
 *                                        s.cur, reset pos, fade in, and
 *                                 <---   store s.ack = serial (release)
 *   service: free retired tracks
 *   whose replacing serial <= ack
 *
 * The audio thread keeps using its old track pointer for the whole
 * fade-out, so retirement is acknowledged-based, never timed: a retired
 * track is freed only after s.ack proves the audio thread adopted a
 * publication NEWER than the one that replaced it (it never reaches
 * backwards). The callback itself does no allocation, locking, or I/O —
 * it only reads the preloaded PCM and steps the gain ramp.
 */
#include "game/em_bgm.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_audio.h"
#include "game/em_sfx.h"

/* ~1 s fade, the shape of the engine's func_001FAE70(1) fade-out-then-
 * start transition (and the fade-in on the far side). */
#define BGM_FADE_SECONDS 1.0f

/* Retired tracks awaiting the audio thread's ack. BGM swaps are rare
 * (area changes); 8 in flight at once means something is very wrong. */
#define BGM_PENDING_MAX 8

typedef struct {
    int16_t *pcm;      /* interleaved s16, audio thread reads only */
    long     nframes;
    int      channels; /* 1 or 2 */
    int      rate;
    int      loop;
} BgmTrack;

static struct {
    /* game thread */
    EmAudio  *audio;
    int       device_rate;  /* set before em_audio_create */
    float     fade_step;    /* gain delta per sample, set before create */
    BgmTrack *published;    /* shadow of s.cur (game-side bookkeeping) */
    struct {
        BgmTrack *t;
        unsigned  replaced_by;  /* serial of the publication that retired it */
    } pending[BGM_PENDING_MAX];
    int       n_pending;
    int       started;      /* any track ever played -> exit stats line */

    /* game -> audio */
    _Atomic(BgmTrack *) cur;
    atomic_uint serial;     /* bumped (release) after each cur store */
    atomic_int  hard_cut;   /* next transition skips the fade-out */

    /* audio -> game */
    atomic_uint ack;        /* last serial the audio thread adopted */
    atomic_long played;     /* WAV frames mixed (non-silent) */
    atomic_long delivered;  /* device frames delivered */

    /* audio thread only */
    const BgmTrack *at_track;
    unsigned  at_serial;
    long      at_pos;
    float     at_gain;
} s;

/* ------------------------------------------------------------------ */
/* Audio-thread side: the render callback (em_audio pull model)        */
/* ------------------------------------------------------------------ */

static void bgm_render(void *user, float *out, int frames)
{
    (void)user;
    /* One publication snapshot per callback; a publish that lands
     * mid-callback is picked up on the next one. */
    const unsigned want = atomic_load_explicit(&s.serial,
                                               memory_order_acquire);
    const float step = s.fade_step;
    long mixed = 0;

    for (int i = 0; i < frames; i++) {
        if (s.at_serial != want) {
            /* Transition: fade the held track out, then adopt. */
            int cut = atomic_load_explicit(&s.hard_cut,
                                           memory_order_relaxed);
            if (cut || !s.at_track || s.at_gain <= 0.0f) {
                s.at_track  = atomic_load_explicit(&s.cur,
                                                   memory_order_acquire);
                s.at_serial = want;
                s.at_pos    = 0;
                s.at_gain   = 0.0f;
                if (cut)
                    atomic_store_explicit(&s.hard_cut, 0,
                                          memory_order_relaxed);
                /* Old pointer dropped for good — let the game free it. */
                atomic_store_explicit(&s.ack, want, memory_order_release);
            } else {
                s.at_gain -= step;
                if (s.at_gain < 0.0f) s.at_gain = 0.0f;
            }
        } else if (s.at_gain < 1.0f) {
            s.at_gain += step;                       /* fade-in */
            if (s.at_gain > 1.0f) s.at_gain = 1.0f;
        }

        float l = 0.0f, r = 0.0f;
        const BgmTrack *t = s.at_track;
        if (t && s.at_pos < t->nframes) {
            const int16_t *src = t->pcm + s.at_pos * t->channels;
            l = (float)src[0] / 32768.0f;
            r = (t->channels == 2) ? (float)src[1] / 32768.0f : l;
            l *= s.at_gain;
            r *= s.at_gain;
            s.at_pos++;
            mixed++;
            if (s.at_pos >= t->nframes && t->loop)
                s.at_pos = 0;                        /* seamless loop */
        }
        out[i * 2 + 0] = l;
        out[i * 2 + 1] = r;
    }

    if (mixed)
        atomic_fetch_add_explicit(&s.played, mixed, memory_order_relaxed);
    atomic_fetch_add_explicit(&s.delivered, (long)frames,
                              memory_order_relaxed);

    /* One-shot SFX voices are SUMMED over the BGM in the same callback —
     * em_bgm owns the only device (em_sfx.h "DEVICE OWNERSHIP"). A no-op
     * while no voices are live, so BGM-only output is untouched.
     * s.device_rate is written before em_audio_create and never changes
     * while the device exists, so this read is race-free. */
    em_sfx_mix(out, frames, s.device_rate);
}

/* ------------------------------------------------------------------ */
/* Game-thread side                                                    */
/* ------------------------------------------------------------------ */

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

static int bgm_wav_load(const char *path, BgmTrack *t)
{
    EmBgmWav w;
    if (em_bgm_wav_read(path, &w, "bgm") != 0) return -1;
    t->pcm      = w.pcm;
    t->nframes  = w.nframes;
    t->channels = w.channels;
    t->rate     = w.rate;
    return 0;
}

static void bgm_track_free(BgmTrack *t)
{
    if (!t) return;
    free(t->pcm);
    free(t);
}

/* Publish a new current track (or NULL = stop). The previously published
 * track is moved to the pending-retire list, freed by em_bgm_service once
 * the audio thread acks a newer serial. */
static void bgm_publish(BgmTrack *t, int cut)
{
    BgmTrack *old = s.published;
    if (old) {
        if (s.n_pending == BGM_PENDING_MAX)
            em_bgm_service();               /* try to reclaim a slot */
        if (s.n_pending == BGM_PENDING_MAX) {
            /* The audio thread is impossibly far behind (or the device
             * died). Leak rather than free under its feet. */
            fprintf(stderr, "bgm: retire queue full — leaking one track\n");
        } else {
            s.pending[s.n_pending].t = old;
            s.pending[s.n_pending].replaced_by =
                atomic_load_explicit(&s.serial, memory_order_relaxed) + 1u;
            s.n_pending++;
        }
    }
    s.published = t;

    atomic_store_explicit(&s.hard_cut, cut ? 1 : 0, memory_order_relaxed);
    atomic_store_explicit(&s.cur, t, memory_order_release);
    /* serial last: a callback that sees the new serial sees the new cur. */
    atomic_fetch_add_explicit(&s.serial, 1u, memory_order_release);
}

/* Open the shared device once. Both fields must be set before create —
 * the callback may fire before create returns. */
static int bgm_device_open(int rate)
{
    if (s.audio) return 0;
    s.device_rate = rate;
    s.fade_step   = 1.0f / (BGM_FADE_SECONDS * (float)rate);
    s.audio = em_audio_create(rate, bgm_render, NULL);
    if (!s.audio) {
        fprintf(stderr, "bgm: audio device creation failed\n");
        return -1;
    }
    return 0;
}

int em_bgm_device_ensure(int sample_rate)
{
    return bgm_device_open(sample_rate);
}

int em_bgm_device_rate(void)
{
    return s.audio ? s.device_rate : 0;
}

int em_bgm_play(const char *path, int loop)
{
    BgmTrack *t = malloc(sizeof *t);
    if (!t) return -1;
    if (bgm_wav_load(path, t) != 0) {
        free(t);
        return -1;
    }
    t->loop = loop ? 1 : 0;

    if (!s.audio) {
        /* First play: open the device at the track's rate (the engine
         * streams at 48 kHz; whatever the export used wins here). */
        if (bgm_device_open(t->rate) != 0) {
            bgm_track_free(t);
            return -1;
        }
    } else if (t->rate != s.device_rate) {
        fprintf(stderr, "bgm: %s is %d Hz but the device runs at %d Hz — "
                        "no resampler yet\n", path, t->rate, s.device_rate);
        bgm_track_free(t);
        return -1;
    }

    printf("bgm: %s — %ld frames @ %d Hz, %d ch (%.1f s)%s\n", path,
           t->nframes, t->rate, t->channels,
           (double)t->nframes / t->rate, t->loop ? ", looping" : "");
    bgm_publish(t, 0);          /* fade-out current, fade-in new */
    s.started = 1;
    return 0;
}

void em_bgm_stop(int fade)
{
    if (!s.published) return;
    bgm_publish(NULL, fade ? 0 : 1);
}

void em_bgm_service(void)
{
    if (!s.n_pending) return;
    unsigned ack = atomic_load_explicit(&s.ack, memory_order_acquire);
    int kept = 0;
    for (int i = 0; i < s.n_pending; i++) {
        /* Freed only once the audio thread adopted a publication at or
         * past the one that retired this track (serial-wrap safe). */
        if ((int)(ack - s.pending[i].replaced_by) >= 0)
            bgm_track_free(s.pending[i].t);
        else
            s.pending[kept++] = s.pending[i];
    }
    s.n_pending = kept;
}

void em_bgm_shutdown(void)
{
    if (s.audio) {
        em_audio_destroy(s.audio);  /* blocks: no callback after this */
        s.audio = NULL;
    }
    if (s.started)
        printf("bgm: %ld wav frames played, %ld device frames delivered\n",
               atomic_load_explicit(&s.played, memory_order_relaxed),
               atomic_load_explicit(&s.delivered, memory_order_relaxed));

    /* The audio thread is gone — everything is safe to free directly. */
    for (int i = 0; i < s.n_pending; i++)
        bgm_track_free(s.pending[i].t);
    s.n_pending = 0;
    bgm_track_free(s.published);
    s.published = NULL;
    atomic_store_explicit(&s.cur, NULL, memory_order_relaxed);
    s.at_track = NULL;
    s.started  = 0;
}
