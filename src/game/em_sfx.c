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
 *
 * VOICE STEALING rides the same protocol through one extra atomic per
 * slot (`kill`): raised by the game thread on the oldest live voice when
 * the engine's 48-voice budget is hit, honored by the audio thread at
 * the next callback (FREE instead of mix), lowered by whoever retires or
 * re-claims the slot. See em_sfx.h "VOICE STEALING" for the engine
 * policy decode (func_001172B8, the 0x90 note-on allocator) this
 * mirrors — corrected 2026-07-31 from the func_00117428 that was cited
 * here before, which serves the 0xA0 event path instead.
 */
#include "game/em_sfx.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_bgm.h"

#define SFX_REGISTRY   "assets/sfx/sfx.txt"
#define SFX_SOUND_MAX  96    /* registry entries (39 banks dedup to ~241
                              * sounds total; one area uses far fewer).
                              * Bumped 64 -> 96 (s82): the AREA-11 snow
                              * footstep merge grew assets/sfx/sfx.txt to
                              * 69 ids; at 64 the RUN-tier snow block
                              * 0x5E..0x62 was dropped at load ("registry
                              * full") and run-pace snow steps went
                              * silent. 96 reloads the full set with
                              * headroom — a pure array bound, no engine
                              * semantics (the engine's own table holds
                              * ~241).                                    */
#define SFX_VOICE_BUDGET 48  /* the engine's voice table D_0027CCC0 size
                              * (0x30 entries, stride 0x6A): at 48 live
                              * voices the OLDEST is stolen — minimum
                              * +0x0A note-on serial, func_001172B8
                              * pass 2. NOT func_00117428: that allocator
                              * serves the 0xA0 event path, while SFX
                              * one-shots are 0x90 note-ons. Corrected
                              * 2026-07-31; see em_sfx.h "VOICE
                              * STEALING".                                */
#define SFX_VOICE_MAX  64    /* physical slots: budget + 16 spares that
                              * absorb the one-callback kill latency      */
#define SFX_DEV_RATE   48000 /* device rate when SFX brings it up first
                              * (the BGM/stream native rate)             */
#define SFX_GAIN       0.6f  /* per-voice mix gain — PORT headroom choice
                              * on top of the engine-exact 0..1 stereo
                              * pair; keeps BGM + a few simultaneous
                              * shots inside [-1,1]                      */
#define SFX_PAN_NEAR   18.0f /* func_001FBF50 proximity pan ramp: |t|
                              * forced toward 1 (center) inside 18 u     */
#define SFX_PI         3.14159265358979323846f

enum { V_FREE = 0, V_STAGING, V_READY, V_PLAYING };

typedef struct {
    unsigned id;
    EmBgmWav w;          /* preloaded PCM16 (em_bgm's shared reader) */
} SfxSound;

typedef struct {
    atomic_int      state;
    atomic_int      kill; /* steal request: game raises, audio (or the
                           * next claimer) lowers — see em_sfx.h       */
    const SfxSound *snd;  /* written in STAGING, read after READY     */
    float           gl;   /* stereo gain pair (1.0 = engine 0x1000;   */
    float           gr;   /*  signed: behind = inverted far channel)  */
    unsigned        serial; /* note-on order (engine voice+0x0A) —
                             * GAME-thread only: steal victim pick    */
    double          pos;  /* source frame cursor (fractional resample);
                           * audio thread advances it while PLAYING   */
} SfxVoice;

static struct {
    /* game thread */
    SfxSound sounds[SFX_SOUND_MAX];
    int      n_sounds;
    int      plays;          /* accepted plays                        */
    int      drops;          /* plays dropped: 64 physical slots busy */
    int      steals;         /* oldest-voice kills at the 48 budget   */
    int      culls;          /* play_at beyond radius (engine -1)     */
    unsigned serial;         /* monotonically increasing play counter
                              * (the engine's D_0027F740+0x34)        */
    /* listener mirror (em_sfx_listener; game thread) */
    int      lis_valid;
    float    lis_player[3];  /* D_00810360 — distance listener        */
    float    lis_eye[3];     /* D_008105D0 — pan listener             */
    float    lis_yaw;        /* D_0081027C (cam+0x9C)                 */

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
        if (st != V_READY && st != V_PLAYING) continue;
        /* STEAL: a raised kill flag retires the voice instead of mixing
         * it (the engine's deferred-by-one-tick voice command). Lower
         * the flag before the FREE release-store so the claimer's CAS
         * (acquire) sees it cleared. */
        if (atomic_load_explicit(&v->kill, memory_order_acquire)) {
            atomic_store_explicit(&v->kill, 0, memory_order_relaxed);
            atomic_store_explicit(&v->state, V_FREE,
                                  memory_order_release);
            continue;
        }
        if (st == V_READY) {
            /* Adopt: READY -> PLAYING is audio-thread-only traffic (the
             * game thread treats both as "busy"), relaxed is enough. */
            atomic_store_explicit(&v->state, V_PLAYING,
                                  memory_order_relaxed);
        }

        const SfxSound *snd  = v->snd;
        const double    step = (double)snd->w.rate / (double)device_rate;
        double          pos  = v->pos;
        const long      last = snd->w.nframes - 1;
        /* The stereo gain pair from the BYTE-MATCHED func_001FBF50
         * (src/func_001FBF50.c), baked at trigger time: func_001FBD50
         * hands it to func_001FB9F0(id, 0x1000, gainA, gainB), which
         * reaches func_0011A218 -> channel +0x48/+0x4C. Under the PORT
         * headroom constant. gr may be negative — func_001FBF50 writes
         * float_to_int(scaled * k) with k down to -1, and func_0011A218
         * accepts the full [-0x1000, 0x1000] range: the behind-the-
         * camera phase inversion. */
        const float     wl   = v->gl * (SFX_GAIN / 32768.0f);
        const float     wr   = v->gr * (SFX_GAIN / 32768.0f);
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
            float l = (l0 + (l1 - l0) * fr) * wl;
            float r = (r0 + (r1 - r0) * fr) * wr;
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

void em_sfx_listener(const float player_pos[3], const float cam_eye[3],
                     float cam_yaw)
{
    if (!player_pos || !cam_eye) return;
    memcpy(s.lis_player, player_pos, sizeof s.lis_player);
    memcpy(s.lis_eye,    cam_eye,    sizeof s.lis_eye);
    s.lis_yaw   = cam_yaw;
    s.lis_valid = 1;
}

/* wrap to (-pi, pi] — func_001B1470 */
static float sfx_wrap_pi(float a)
{
    while (a >  SFX_PI) a -= 2.0f * SFX_PI;
    while (a < -SFX_PI) a += 2.0f * SFX_PI;
    return a;
}

/* The BYTE-MATCHED func_001FBF50, line for line (src/func_001FBF50.c;
 * see em_sfx.h "POSITIONAL AUDIO"): stereo gain pair for a source at
 * `pos` with attenuation `radius`, against the em_sfx_listener state.
 * Returns 0 = out of range (func_001FBF50 returns 0, which func_001FBD50
 * reports as play_sound -1). */
int em_sfx_compute_gains(const float pos[3], float radius,
                         float *gain_l, float *gain_r)
{
    *gain_l = *gain_r = 0.0f;
    if (!s.lis_valid) {
        /* no listener yet (early boot / headless): degrade to the
         * non-positional center/full submit — no cull, no pan */
        *gain_l = *gain_r = 1.0f;
        return 1;
    }
    /* Engine parity: func_001FBF50's range test is `!(dist < radius)`,
     * and dist is a magnitude (>= 0), so a non-positive radius culls
     * EVERY source. The old code short-circuited radius <= 0 into the
     * no-listener center/full path, which played sounds the engine
     * would have refused to submit. (No translated call site passes a
     * non-positive radius today — every one seen in the decomp passes
     * 300.0f, with 450/500/800/1000 at a handful of untranslated
     * sites — so this is a latent divergence, not an observed one.) */
    if (radius <= 0.0f) return 0;

    /* DISTANCE: player listener (D_00810360), full 3-D (flat2d = 0 at
     * every translated call site). */
    float dx = pos[0] - s.lis_player[0];
    float dy = pos[1] - s.lis_player[1];
    float dz = pos[2] - s.lis_player[2];
    float d  = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d >= radius) return 0;          /* engine: not submitted */
    float vol = sinf((SFX_PI * 0.5f) * (radius - d) / radius);

    /* PAN: camera listener (eye D_008105D0, yaw cam+0x9C), XZ plane.
     * The engine's dot(RotY(yaw)*(0,0,1), normalize_xz(src-eye)) ==
     * cos(bearing - yaw); the side test func_001B1380 is the sign of
     * the same wrapped bearing difference. */
    float ex    = pos[0] - s.lis_eye[0];
    float ez    = pos[2] - s.lis_eye[2];
    float delta = sfx_wrap_pi(atan2f(ex, ez) - s.lis_yaw);
    float c     = cosf(delta);
    float k     = d <= SFX_PAN_NEAR ? d / SFX_PAN_NEAR : 1.0f;
    float t     = c * c;
    t *= t;
    t *= c * k;                          /* c^5 * k */
    t += (t < 0.0f) ? -(1.0f - k) : (1.0f - k);
    if (delta >= 0.0f) {                 /* source on the LEFT */
        *gain_l = vol;
        *gain_r = vol * t;
    } else {
        *gain_r = vol;
        *gain_l = vol * t;
    }
    return 1;
}

/* Claim a slot and publish one voice with the given gain pair —
 * including the engine steal policy (em_sfx.h "VOICE STEALING"): at
 * SFX_VOICE_BUDGET live voices the OLDEST (minimum serial) is killed. */
static void sfx_submit(const SfxSound *snd, float gl, float gr)
{
    /* The device is em_bgm's; bring it up if music hasn't already. */
    if (em_bgm_device_ensure(SFX_DEV_RATE) != 0) return;

    /* One pass: count live voices (busy, not kill-pending), remember
     * the first FREE slot and the oldest live victim. Slot states only
     * move FREE->busy on this thread, so the census cannot run ahead
     * of itself; busy->FREE flips by the audio thread mid-scan only
     * make the count conservative. */
    int      free_idx = -1, victim = -1, live = 0;
    unsigned victim_serial = 0;
    for (int vi = 0; vi < SFX_VOICE_MAX; vi++) {
        SfxVoice *v  = &s.voices[vi];
        int       st = atomic_load_explicit(&v->state,
                                            memory_order_acquire);
        if (st == V_FREE) {
            if (free_idx < 0) free_idx = vi;
            continue;
        }
        if (atomic_load_explicit(&v->kill, memory_order_relaxed))
            continue;                    /* dying: no longer live      */
        live++;
        /* serial is game-thread data: valid for every busy slot */
        if (victim < 0 || (int)(v->serial - victim_serial) < 0) {
            victim        = vi;
            victim_serial = v->serial;
        }
    }
    if (free_idx < 0) {
        s.drops++;                       /* 64 physical busy — engine
                                          * equivalent: allocator -1   */
        return;
    }
    if (live >= SFX_VOICE_BUDGET && victim >= 0) {
        /* engine pass 3: steal the oldest (priorities engine-equal
         * across shipped SFX tones — the gate always passes) */
        atomic_store_explicit(&s.voices[victim].kill, 1,
                              memory_order_release);
        s.steals++;
    }

    SfxVoice *v        = &s.voices[free_idx];
    int       expected = V_FREE;
    if (!atomic_compare_exchange_strong_explicit(
            &v->state, &expected, V_STAGING,
            memory_order_acquire, memory_order_relaxed)) {
        s.drops++;                       /* unreachable: single producer */
        return;
    }
    atomic_store_explicit(&v->kill, 0, memory_order_relaxed);
    v->snd    = snd;
    v->gl     = gl;
    v->gr     = gr;
    v->serial = s.serial++;
    v->pos    = 0.0;
    atomic_store_explicit(&v->state, V_READY, memory_order_release);
    s.plays++;
}

static const SfxSound *sfx_lookup(unsigned id)
{
    for (int i = 0; i < s.n_sounds; i++)
        if (s.sounds[i].id == id) return &s.sounds[i];
    return NULL;
}

void em_sfx_play(unsigned id)
{
    if (!s.n_sounds) return;            /* module disabled */
    const SfxSound *snd = sfx_lookup(id);
    if (!snd) return;                   /* unmapped id: silent no-op */
    /* center/full — func_001FB9F0(id, 0x1000, 0x1000, 0x1000), also the
     * exact play_sound result for a player-attached source */
    sfx_submit(snd, 1.0f, 1.0f);
}

void em_sfx_play_at(unsigned id, const float pos[3], float radius)
{
    if (!s.n_sounds || !pos) return;
    const SfxSound *snd = sfx_lookup(id);
    if (!snd) return;
    float gl, gr;
    if (!em_sfx_compute_gains(pos, radius, &gl, &gr)) {
        s.culls++;                      /* engine play_sound -1 */
        return;
    }
    sfx_submit(snd, gl, gr);
}

void em_sfx_shutdown(void)
{
    /* Caller contract: em_bgm_shutdown already destroyed the device, so
     * no callback can be reading the samples or the slots. */
    if (s.plays || s.drops || s.culls)
        printf("sfx: %d play(s), %d stolen, %d culled (range), %d "
               "dropped, %ld voice frames mixed, peak %d concurrent\n",
               s.plays, s.steals, s.culls, s.drops,
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
int  em_sfx_steals(void)      { return s.steals; }
int  em_sfx_culls(void)       { return s.culls; }

long em_sfx_frames_mixed(void)
{
    return atomic_load_explicit(&s.frames_mixed, memory_order_relaxed);
}

int em_sfx_max_concurrent(void)
{
    return atomic_load_explicit(&s.max_concurrent, memory_order_relaxed);
}
