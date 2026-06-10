/* em_sfx.c — one-shot SFX voices summed over the BGM stream. See em_sfx.h
 * for the engine model (SShd trigger path), the registry format, and the
 * full lock-free slot protocol; em_bgm.c owns the device and calls
 * em_sfx_mix() from its render callback.
 *
 * Voice-slot states (atomic, one word per slot):
 *   FREE     no voice; only the game thread may CAS it away.
 *   STAGING  game thread is writing the slot fields (invisible to the
 *            audio thread — it ignores everything but READY/PLAYING).
 *   READY    published; the audio thread adopts it on its next callback.
 *   PLAYING  audio-thread owned; mixed until the sample runs out, then
 *            stored back to FREE (release) for reuse.
 *
 * The READY store (release) is what publishes the slot fields AND the
 * preloaded sample memory to the audio thread (acquire on load); FREE is
 * stored with release so the game thread's CAS (acquire) sees the audio
 * thread's final position writes before reusing the slot. Sample memory
 * itself is immutable from init until shutdown, and shutdown runs only
 * after em_bgm_shutdown's device-teardown guarantee.
 */
#include "game/em_sfx.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_bgm.h"

#define SFX_REGISTRY   "assets/sfx/sfx.txt"
#define SFX_SOUND_MAX  64    /* registry entries (39 banks dedup to ~241
                              * sounds total; one area uses far fewer)    */
#define SFX_VOICE_MAX  8     /* concurrent one-shots; the PS2 sequencer
                              * runs 48 channels but gameplay one-shots
                              * are a handful — raise when voice stealing
                              * lands with the pitch work                 */
#define SFX_DEV_RATE   48000 /* device rate when SFX brings it up first
                              * (the BGM/stream native rate)             */
#define SFX_GAIN       0.6f  /* per-voice mix gain — PORT headroom choice
                              * (the engine's vol-150 scale is not mapped
                              * to a linear gain yet); keeps BGM + a few
                              * simultaneous shots inside [-1,1]         */

enum { V_FREE = 0, V_STAGING, V_READY, V_PLAYING };

typedef struct {
    unsigned id;
    EmBgmWav w;          /* preloaded PCM16 (em_bgm's shared reader) */
} SfxSound;

typedef struct {
    atomic_int      state;
    const SfxSound *snd;  /* written in STAGING, read after READY     */
    double          pos;  /* source frame cursor (fractional resample);
                           * audio thread advances it while PLAYING   */
} SfxVoice;

static struct {
    /* game thread */
    SfxSound sounds[SFX_SOUND_MAX];
    int      n_sounds;
    int      plays;          /* accepted plays                        */
    int      drops;          /* plays dropped: no free voice slot     */

    /* shared (lock-free slots + stats) */
    SfxVoice voices[SFX_VOICE_MAX];
    atomic_long frames_mixed;   /* summed voice frames                */
    atomic_int  max_concurrent; /* peak live voices in one callback   */
} s;

/* ------------------------------------------------------------------ */
/* Audio-thread side                                                    */
/* ------------------------------------------------------------------ */

void em_sfx_mix(float *out, int frames, int device_rate)
{
    if (device_rate <= 0) return;
    int live = 0;

    for (int vi = 0; vi < SFX_VOICE_MAX; vi++) {
        SfxVoice *v  = &s.voices[vi];
        int       st = atomic_load_explicit(&v->state,
                                            memory_order_acquire);
        if (st == V_READY) {
            /* Adopt: READY -> PLAYING is audio-thread-only traffic (the
             * game thread treats both as "busy"), relaxed is enough. */
            atomic_store_explicit(&v->state, V_PLAYING,
                                  memory_order_relaxed);
        } else if (st != V_PLAYING) {
            continue;
        }

        const SfxSound *snd  = v->snd;
        const double    step = (double)snd->w.rate / (double)device_rate;
        double          pos  = v->pos;
        const long      last = snd->w.nframes - 1;
        long            mixed = 0;
        live++;

        for (int i = 0; i < frames; i++) {
            long ip = (long)pos;
            if (ip >= snd->w.nframes) break;
            /* Linear resample (clamped at the tail). */
            float  fr = (float)(pos - (double)ip);
            long   in = ip < last ? ip + 1 : last;
            const int16_t *a = snd->w.pcm + ip * snd->w.channels;
            const int16_t *b = snd->w.pcm + in * snd->w.channels;
            float l0 = (float)a[0], l1 = (float)b[0];
            float r0, r1;
            if (snd->w.channels == 2) {
                r0 = (float)a[1];
                r1 = (float)b[1];
            } else {
                r0 = l0;
                r1 = l1;
            }
            float l = (l0 + (l1 - l0) * fr) * (SFX_GAIN / 32768.0f);
            float r = (r0 + (r1 - r0) * fr) * (SFX_GAIN / 32768.0f);
            out[i * 2 + 0] += l;
            out[i * 2 + 1] += r;
            pos += step;
            mixed++;
        }
        v->pos = pos;
        if (mixed)
            atomic_fetch_add_explicit(&s.frames_mixed, mixed,
                                      memory_order_relaxed);
        if (pos >= (double)snd->w.nframes)   /* done — recycle the slot */
            atomic_store_explicit(&v->state, V_FREE, memory_order_release);
    }

    if (live > atomic_load_explicit(&s.max_concurrent,
                                    memory_order_relaxed))
        atomic_store_explicit(&s.max_concurrent, live,
                              memory_order_relaxed);
}

/* ------------------------------------------------------------------ */
/* Game-thread side                                                     */
/* ------------------------------------------------------------------ */

int em_sfx_init(void)
{
    FILE *f = fopen(SFX_REGISTRY, "r");
    if (!f) return 0;   /* no registry = module disabled, silently */

    char line[640];
    while (fgets(line, sizeof line, f)) {
        unsigned id;
        char     path[512];
        char    *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        if (sscanf(p, "%x %511s", &id, path) != 2) {
            fprintf(stderr, "sfx: bad registry line: %s", line);
            continue;
        }
        if (s.n_sounds >= SFX_SOUND_MAX) {
            fprintf(stderr, "sfx: registry full (%d) — ignoring 0x%X\n",
                    SFX_SOUND_MAX, id);
            continue;
        }
        SfxSound *snd = &s.sounds[s.n_sounds];
        if (em_bgm_wav_read(path, &snd->w, "sfx") != 0)
            continue;   /* reader already printed the reason */
        snd->id = id;
        s.n_sounds++;
    }
    fclose(f);

    if (s.n_sounds)
        printf("sfx: %d sound(s) preloaded from %s\n", s.n_sounds,
               SFX_REGISTRY);
    return s.n_sounds;
}

void em_sfx_play(unsigned id)
{
    if (!s.n_sounds) return;            /* module disabled */

    const SfxSound *snd = NULL;
    for (int i = 0; i < s.n_sounds; i++) {
        if (s.sounds[i].id == id) {
            snd = &s.sounds[i];
            break;
        }
    }
    if (!snd) return;                   /* unmapped id: silent no-op */

    /* The device is em_bgm's; bring it up if music hasn't already. */
    if (em_bgm_device_ensure(SFX_DEV_RATE) != 0) return;

    for (int vi = 0; vi < SFX_VOICE_MAX; vi++) {
        SfxVoice *v        = &s.voices[vi];
        int       expected = V_FREE;
        if (!atomic_compare_exchange_strong_explicit(
                &v->state, &expected, V_STAGING,
                memory_order_acquire, memory_order_relaxed))
            continue;
        v->snd = snd;
        v->pos = 0.0;
        atomic_store_explicit(&v->state, V_READY, memory_order_release);
        s.plays++;
        return;
    }
    s.drops++;                          /* all slots busy — dropped */
}

void em_sfx_shutdown(void)
{
    /* Caller contract: em_bgm_shutdown already destroyed the device, so
     * no callback can be reading the samples or the slots. */
    if (s.plays || s.drops)
        printf("sfx: %d play(s), %d dropped, %ld voice frames mixed, "
               "peak %d concurrent\n", s.plays, s.drops,
               atomic_load_explicit(&s.frames_mixed, memory_order_relaxed),
               atomic_load_explicit(&s.max_concurrent,
                                    memory_order_relaxed));
    for (int i = 0; i < s.n_sounds; i++)
        free(s.sounds[i].w.pcm);
    memset(&s, 0, sizeof s);
}

/* --- Introspection ------------------------------------------------------ */

int  em_sfx_sound_count(void) { return s.n_sounds; }
int  em_sfx_plays(void)       { return s.plays; }
int  em_sfx_drops(void)       { return s.drops; }

long em_sfx_frames_mixed(void)
{
    return atomic_load_explicit(&s.frames_mixed, memory_order_relaxed);
}

int em_sfx_max_concurrent(void)
{
    return atomic_load_explicit(&s.max_concurrent, memory_order_relaxed);
}
