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
 * the 48-slot budget is hit, honored by the audio thread at the next
 * callback (FREE instead of mix), lowered by whoever retires or re-claims
 * the slot. This is a PORT policy: every trigger script is A0 events,
 * whose voices come from func_00117428 (00115850), which is not
 * reproduced (em_sfx.h "VOICE STEALING").
 *
 * PITCH/GAIN (WP-14, H19/AM-02): the legacy WAV registry is retired. Each
 * slot is one trigger-script instance from the EMSR registry: every A0
 * voice starts at its sequencer tick with its integer SPU pitch and its
 * two Q14 volume words (em_sfx_volume_words), exactly the values the
 * original 00115850 submits as voice commands 6 and 1.
 */
#include "game/em_sfx.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_bgm.h"
#include "game/em_sfx_bank.h"

#define SFX_REGISTRY   "assets/sfx/sfx_registry.emsr"
#define SFX_VOICE_BUDGET 48  /* the driver's 0x30 track slots (D_0027E0C0);
                              * the steal choice itself is a PORT policy,
                              * see em_sfx.h "VOICE STEALING"            */
#define SFX_VOICE_MAX  64    /* physical slots: budget + 16 spares that
                              * absorb the one-callback kill latency      */
#define SFX_DEV_RATE   48000 /* device rate when SFX brings it up first
                              * (the BGM/stream native rate)             */
#define SFX_PAN_NEAR   18.0f /* func_001FBF50 proximity pan ramp: |t|
                              * forced toward 1 (center) inside 18 u     */
#define SFX_PI         3.14159265358979323846f

enum { V_FREE = 0, V_STAGING, V_READY, V_PLAYING };

typedef struct {
    unsigned id;
    const EmSfxCue *cue;     /* audited AREA11 panel cue, or NULL       */
    const EmSfxEntry *entry; /* EMSR registry entry, or NULL            */
} SfxSound;

enum { LOOKUP_NONE = 0, LOOKUP_AUDIBLE, LOOKUP_ABSENT, LOOKUP_UNSUPPORTED,
       LOOKUP_UNSCOPED };

typedef struct {
    atomic_int      state;
    atomic_int      kill; /* steal request: game raises, audio (or the
                           * next claimer) lowers — see em_sfx.h       */
    SfxSound        snd;  /* written in STAGING, read after READY     */
    float           gl;   /* panel cue: request pair (1.0 = 0x1000)   */
    float           gr;
    float           gain[EM_SFX_EVENT_MAX][2]; /* registry: Q14 volume
                           * words per A0 voice, as float gains        */
    unsigned        serial; /* note-on order (engine voice+0x0A) —
                             * GAME-thread only: steal victim pick    */
    uint64_t        output_frame; /* exact rational cursor, from key-on */
} SfxVoice;

static struct {
    /* game thread */
    EmSfxRegistry registry;  /* immutable from init until shutdown     */
    int      n_sounds;       /* audible registry entries              */
    int      area, sub;      /* em_sfx_set_area scope, -1 = none      */
    unsigned char reported[4096 / 8]; /* one diagnostic per id        */
    int      plays;          /* accepted plays                        */
    int      drops;          /* plays dropped: 64 physical slots busy */
    int      steals;         /* oldest-voice kills at the 48 budget   */
    int      culls;          /* play_at beyond radius (engine -1)     */
    int      absent;         /* original remap FF: deliberately silent */
    int      unsupported;    /* registry UNSUPPORTED: silent, reported */
    int      unscoped;       /* area-dependent id with no area selected */
    EmSfxBank bank;
    SfxSound bank_sounds[2]; /* PCM is owned by bank, never freed here */
    int      bank_selected;
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

/* One registry voice slot: every A0 voice of the script starts at its
 * sequencer tick (converted at the NTSC field rate), plays its source at
 * the SPU pitch word, and ends at its non-loop sample end. */
static long sfx_mix_entry(SfxVoice *v, float *out, int frames,
                          unsigned rate, int *done)
{
    const EmSfxEntry *entry = v->snd.entry;
    uint64_t start[EM_SFX_EVENT_MAX], stop[EM_SFX_EVENT_MAX], end = 0;
    for (unsigned e = 0; e < entry->count; ++e) {
        const EmSfxEvent *event = &entry->events[e];
        start[e] = em_sfx_tick_frame(event->tick, rate);
        stop[e] = start[e] + em_sfx_event_frames(
            &s.registry.samples[event->sample], event->pitch, rate);
        if (stop[e] > end) end = stop[e];
    }
    long mixed = 0;
    for (int i = 0; i < frames && v->output_frame < end; ++i) {
        const uint64_t at = v->output_frame;
        for (unsigned e = 0; e < entry->count; ++e) {
            float value;
            if (at < start[e] || at >= stop[e]) continue;
            const EmSfxEvent *event = &entry->events[e];
            if (!em_sfx_event_sample(&s.registry.samples[event->sample],
                                     event->pitch, at - start[e], rate,
                                     &value)) continue;
            out[2 * i] += value * v->gain[e][0];
            out[2 * i + 1] += value * v->gain[e][1];
        }
        ++v->output_frame;
        ++mixed;
    }
    *done = v->output_frame >= end;
    return mixed;
}

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
        live++;
        if (v->snd.entry) {
            int done = 0;
            long mixed = sfx_mix_entry(v, out, frames, (unsigned)device_rate,
                                       &done);
            if (mixed) atomic_fetch_add_explicit(&s.frames_mixed, mixed,
                                                 memory_order_relaxed);
            if (done)
                atomic_store_explicit(&v->state, V_FREE, memory_order_release);
            continue;
        }
        /* Audited AREA11 panel cue: original A0 pitch, Q14 voice gains and
         * its verified steady envelope (em_sfx_cue_frame). */
        const EmSfxCue *cue = v->snd.cue;
        float pair[2];
        long mixed = 0;
        for (int i = 0; i < frames; ++i) {
            if (!em_sfx_cue_frame(cue, v->output_frame,
                                  (unsigned)device_rate, pair)) break;
            out[2*i] += pair[0] * v->gl;
            out[2*i+1] += pair[1] * v->gr;
            ++v->output_frame;
            ++mixed;
        }
        if (mixed) atomic_fetch_add_explicit(&s.frames_mixed, mixed,
                                             memory_order_relaxed);
        if (!em_sfx_cue_frame(cue, v->output_frame,
                              (unsigned)device_rate, pair))
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
    s.area = s.sub = -1;
    if (em_sfx_registry_load(&s.registry, SFX_REGISTRY)) {
        for (unsigned i = 0; i < s.registry.entry_count; ++i)
            s.n_sounds += s.registry.entries[i].state == EM_SFX_STATE_AUDIBLE;
        printf("sfx: %d audible id(s) of %u registry entries from %s\n",
               s.n_sounds, s.registry.entry_count, SFX_REGISTRY);
    } else {
        fprintf(stderr, "sfx: %s missing or invalid (run "
                "tools/export_sfx_registry.py); registry ids are silent\n",
                SFX_REGISTRY);
    }

    if (em_sfx_bank_load(&s.bank, "assets/sfx/area11/panel_sfx.emsf")) {
        for (unsigned i = 0; i < s.bank.count; ++i) {
            s.bank_sounds[i].id = s.bank.cues[i].id;
            s.bank_sounds[i].cue = &s.bank.cues[i];
        }
        printf("sfx: original AREA11 panel bank ready (one cue, one absent remap)\n");
    }
    return s.n_sounds + (s.bank.count ? 1 : 0);
}

int em_sfx_set_area(int area, int sub)
{
    s.bank_selected = 0;
    s.area = s.sub = -1;
    if (area < 0 || sub < 0) return 1;
    if (area == 11 && sub == 0 && s.bank.count != 2) return 0;
    s.area = area;
    s.sub = sub;
    s.bank_selected = area == 11 && sub == 0;
    return 1;
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

/* Claim a slot and publish one script instance. `left`/`right` are the
 * original request words (0x1000 = full) that 001FB9F0 hands to 0011A218.
 * Includes the port steal policy (em_sfx.h "VOICE STEALING"): at
 * SFX_VOICE_BUDGET live slots the OLDEST (minimum serial) is killed. */
static void sfx_submit(const SfxSound *snd, int32_t left, int32_t right)
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
        s.drops++;                       /* 64 physical busy           */
        return;
    }
    if (live >= SFX_VOICE_BUDGET && victim >= 0) {
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
    v->snd    = *snd;
    v->gl     = (float)left / 4096.0f;
    v->gr     = (float)right / 4096.0f;
    if (snd->entry) {
        /* 00115850 -> 001179E0: one volume-word pair per A0 voice. */
        for (unsigned e = 0; e < snd->entry->count; ++e) {
            uint16_t words[2];
            em_sfx_volume_words(snd->entry->events[e].scalar,
                                snd->entry->events[e].pan, left, right, words);
            v->gain[e][0] = em_sfx_volume_gain(words[0]);
            v->gain[e][1] = em_sfx_volume_gain(words[1]);
        }
    }
    v->serial = s.serial++;
    v->output_frame = 0;
    atomic_store_explicit(&v->state, V_READY, memory_order_release);
    s.plays++;
}

/* Scope order: the audited panel bank (AREA11 selected), then the registry
 * entry of the selected area, then the area-independent entry. An id known
 * only for another area, with no area selected, is an unbound scope. */
static int sfx_lookup(unsigned id, SfxSound *out)
{
    memset(out, 0, sizeof *out);
    out->id = id;
    if (s.bank_selected) {
        for (unsigned i = 0; i < s.bank.count; ++i) {
            if (s.bank_sounds[i].id != id) continue;
            *out = s.bank_sounds[i];
            return (s.bank_sounds[i].cue->flags & EM_SFX_CUE_ABSENT)
                ? LOOKUP_ABSENT : LOOKUP_AUDIBLE;
        }
    }
    const EmSfxEntry *entry = NULL;
    if (s.area >= 0)
        entry = em_sfx_registry_find(&s.registry, id, s.area, s.sub);
    if (!entry) entry = em_sfx_registry_find(&s.registry, id, -1, -1);
    if (!entry) {
        /* Exported only for other areas: the selected scope (or the lack
         * of one) has no original resolution here -- never borrow one. */
        for (unsigned i = 0; i < s.registry.entry_count; ++i)
            if (s.registry.entries[i].id == id) return LOOKUP_UNSCOPED;
        return LOOKUP_NONE;
    }
    out->entry = entry;
    if (entry->state == EM_SFX_STATE_ABSENT) return LOOKUP_ABSENT;
    if (entry->state == EM_SFX_STATE_UNSUPPORTED) return LOOKUP_UNSUPPORTED;
    return LOOKUP_AUDIBLE;
}

static void sfx_report(unsigned id, const char *what)
{
    unsigned bit = id & 4095u;
    if (s.reported[bit >> 3] & (1u << (bit & 7))) return;
    s.reported[bit >> 3] |= (unsigned char)(1u << (bit & 7));
    fprintf(stderr, "sfx: id 0x%X %s\n", id, what);
}

/* Shared gate: returns 1 when the id should be submitted. */
static int sfx_accept(unsigned id, SfxSound *snd)
{
    switch (sfx_lookup(id, snd)) {
    case LOOKUP_AUDIBLE: return 1;
    case LOOKUP_ABSENT: ++s.absent; return 0;
    case LOOKUP_UNSUPPORTED:
        ++s.unsupported;
        sfx_report(id, "needs driver features the native mixer does not "
                       "reproduce (docs/SFX_PITCH.md); silent");
        return 0;
    case LOOKUP_UNSCOPED:
        ++s.unscoped;
        sfx_report(id, "is area-dependent and was not exported for the "
                       "em_sfx_set_area scope (or none is bound); silent");
        return 0;
    default: return 0;           /* unmapped id: silent no-op */
    }
}

void em_sfx_play(unsigned id)
{
    SfxSound snd;
    if (!sfx_accept(id, &snd)) return;
    /* center/full — func_001FB9F0(id, 0x1000, 0x1000, 0x1000), also the
     * exact play_sound result for a player-attached source */
    sfx_submit(&snd, 0x1000, 0x1000);
}

void em_sfx_play_at(unsigned id, const float pos[3], float radius)
{
    if (!pos) return;
    SfxSound snd;
    if (!sfx_accept(id, &snd)) return;
    float gl, gr;
    if (!em_sfx_compute_gains(pos, radius, &gl, &gr)) {
        s.culls++;                      /* engine play_sound -1 */
        return;
    }
    /* 001FBF50 hands float_to_int(4096 * gain) words to 001FB9F0. */
    sfx_submit(&snd, em_sfx_request_word(gl), em_sfx_request_word(gr));
}

/* Stop every live voice immediately — DECODED from func_001FBC50.
 *
 * The engine's audio reset first runs func_0011A198(1) (stop each active
 * slot of the 0x30-entry sound table through func_0011A070), then walks the
 * handle table D_00281D50 and, for each handle whose status
 * (func_00119D38) shows it busy, issues the stop command
 * func_00119AA0(h, 1), and clears the per-channel record array. The decomp
 * header now names it stop-all SFX (the old "Subsystem init" label is
 * corrected). The game calls it on status/SELECT/end-screen entry
 * (anim_frame_top_b state 1), at game-over, at scripted cuts, and on the
 * script's op-0x17 sub-3 "stop".
 *
 * The port had no runtime stop — em_sfx_shutdown() is a process-exit teardown
 * that frees the PCM and cannot be called mid-session — so effects kept
 * playing straight through a death or a cut.
 *
 * CALLERS (the old "NOT YET CALLED" note was stale — first-level audit
 * H22/AM-06): em_frontend.c (movie start, EM_STARTUP_AUDIO_STOP),
 * em_opening_media_audio_start, and the interaction host's
 * EM_STATUS_RESET_SOUNDS handler (em_status_frame's 001FBC50 step on
 * status open; live once the status frame machine is wired, WP-5). The
 * mismatched argument lists in some decomp callers' extern declarations
 * do not move the original jal sites, so they are not a reason to doubt
 * where the calls happen. Still NOT wired at the death entry: that entry
 * has just started the death voice and body cues.
 *
 * Implemented with the existing per-voice kill flag rather than a new
 * mechanism: the mixer already retires a killed voice to V_FREE on its next
 * pass, which is the same deferred-by-one-tick shape as the engine's voice
 * command queue. Safe to call from the game thread. */
void em_sfx_stop_all(void)
{
    for (int vi = 0; vi < SFX_VOICE_MAX; vi++) {
        SfxVoice *v = &s.voices[vi];
        int st = atomic_load_explicit(&v->state, memory_order_acquire);
        if (st == V_READY || st == V_PLAYING)
            atomic_store_explicit(&v->kill, 1, memory_order_release);
    }
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
    em_sfx_registry_free(&s.registry);
    em_sfx_bank_free(&s.bank);
    memset(&s, 0, sizeof s);
}

/* --- Introspection ------------------------------------------------------ */

int  em_sfx_sound_count(void) { return s.n_sounds; }
int  em_sfx_plays(void)       { return s.plays; }
int  em_sfx_drops(void)       { return s.drops; }
int  em_sfx_steals(void)      { return s.steals; }
int  em_sfx_culls(void)       { return s.culls; }
int  em_sfx_absent_cues(void) { return s.absent; }
int  em_sfx_unsupported_cues(void) { return s.unsupported; }
int  em_sfx_unscoped_cues(void)    { return s.unscoped; }

int em_sfx_cue_state(unsigned id)
{
    SfxSound snd;
    switch (sfx_lookup(id, &snd)) {
    case LOOKUP_AUDIBLE: return 1;
    case LOOKUP_ABSENT: return 2;
    default: return 0;
    }
}

long em_sfx_frames_mixed(void)
{
    return atomic_load_explicit(&s.frames_mixed, memory_order_relaxed);
}

int em_sfx_max_concurrent(void)
{
    return atomic_load_explicit(&s.max_concurrent, memory_order_relaxed);
}
