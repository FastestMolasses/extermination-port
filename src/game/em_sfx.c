/* em_sfx.c — the SFX sound driver summed over the BGM stream. See em_sfx.h
 * for the engine model and the threading contract; em_bgm.c owns the
 * device and calls em_sfx_mix() from its render callback.
 *
 * DRIVER (WP-14, docs/SFX_SEQUENCER.md): registry plays run through the
 * translated original sound driver in em_sfx_bank.c — 00119EA0 tracks,
 * the 001152D8 tick (00115850 key-on with 00117428 allocation, 001176E0
 * key-off, 00118078 portamento, 00116598 re-sends, 00118EC0 reaper) and a
 * 48-voice SPU2 model with ADSR stepping and loop replay. The audio thread
 * owns that driver and runs one sequencer tick per NTSC field (60000/1001
 * Hz) on a grid anchored at the first callback after it was idle.
 *
 * TRACK HAND-OFF (one atomic state word per original track, 48):
 *   FREE     driver track free; only the game thread may CAS it away.
 *   STAGING  game thread writes the start request (00119EA0 picks the
 *            LOWEST free track; so does sfx_start).
 *   READY    published; the audio thread starts it on the driver at its
 *            next tick (the original also waits for the next 001152D8).
 *   LIVE     allocated on the driver; the audio thread stores FREE when the
 *            00118EC0 reaper frees it (CAS, so a pending stop wins).
 *   STOP     0011A070(track) requested (001FC3C0/001FC520): applied at the
 *            next callback start, then FREE.
 *   HALT     001FBC50 stop-all: 0011A070(track | 0x8000) at the next
 *            callback start, its voices silenced at once, then FREE.
 * The original frees a stopped track immediately; here it is unavailable
 * to new starts until the next callback (at most one callback period).
 *
 * The audited AREA11 panel cue (EMSF bank, docs/AREA11_PANEL_SFX.md) keeps
 * its own small slot pool outside the driver while (11,0) is selected.
 */
#include "game/em_sfx.h"

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_bgm.h"
#include "game/em_sfx_bank.h"

#define SFX_REGISTRY   "assets/sfx/sfx_registry.emsr"
#define SFX_CUE_SLOTS  16    /* panel-cue voices (EMSF path only)          */
#define SFX_DEV_RATE   48000 /* device rate when SFX brings it up first
                              * (the BGM/stream native rate)             */
#define SFX_PAN_NEAR   18.0f /* func_001FBF50 proximity pan ramp: |t|
                              * forced toward 1 (center) inside 18 u     */
#define SFX_PI         3.14159265358979323846f
/* Voices 0..3 are the stream voices (kind 3) in every AREA11 capture;
 * 00117428 never hands them to a key-on. */
#define SFX_STREAM_VOICES 0xFull

enum { V_FREE = 0, V_STAGING, V_READY, V_PLAYING };
enum { T_FREE = 0, T_STAGING, T_READY, T_LIVE, T_STOP, T_HALT };

enum { LOOKUP_NONE = 0, LOOKUP_AUDIBLE, LOOKUP_ABSENT, LOOKUP_UNSUPPORTED,
       LOOKUP_UNSCOPED };

typedef struct {
    const EmSfxCue *cue;     /* audited AREA11 panel cue, or NULL       */
    const EmSfxEntry *entry; /* EMSR registry entry, or NULL            */
} SfxSound;

typedef struct {
    atomic_int      state;
    atomic_int      kill;    /* stop-all: game raises, audio lowers     */
    const EmSfxCue *cue;     /* written in STAGING, read after READY    */
    float           gl, gr;  /* request pair (1.0 = 0x1000)             */
    uint64_t        output_frame;
} CueSlot;

static struct {
    /* game thread */
    EmSfxRegistry registry;  /* immutable from init until shutdown     */
    int      n_sounds;       /* audible registry entries              */
    int      area, sub;      /* em_sfx_set_area scope, -1 = none      */
    unsigned char reported[4096 / 8]; /* one diagnostic per id        */
    int      plays;          /* accepted plays (a track was assigned) */
    int      drops;          /* 00119EA0 -1: all 48 tracks busy       */
    int      culls;          /* play_at beyond radius (engine -1)     */
    int      absent;         /* original remap FF: deliberately silent */
    int      unsupported;    /* registry UNSUPPORTED: silent, reported */
    int      unscoped;       /* area-dependent id with no area selected */
    EmSfxBank bank;
    const EmSfxCue *bank_cues[2]; /* PCM is owned by bank             */
    int      bank_selected;
    int32_t  requested[EM_SFX_TRACKS]; /* D_00281B70 */
    int32_t  snapshot[EM_SFX_TRACKS];  /* D_00281C30 (001FB100 copy)  */
    /* listener mirror (em_sfx_listener; game thread) */
    int      lis_valid;
    float    lis_player[3];  /* D_00810360 — distance listener        */
    float    lis_eye[3];     /* D_008105D0 — pan listener             */
    float    lis_yaw;        /* D_0081027C (cam+0x9C)                 */

    /* shared */
    atomic_int track[EM_SFX_TRACKS];
    const EmSfxEntry *start_entry[EM_SFX_TRACKS]; /* STAGING -> READY */
    int32_t  start_left[EM_SFX_TRACKS], start_right[EM_SFX_TRACKS];
    _Atomic uint64_t request[EM_SFX_TRACKS]; /* 0011A218 hand-off     */
    atomic_long frames_mixed;   /* summed voice frames                */
    atomic_int  max_concurrent; /* peak sounding voices               */
    atomic_uint no_voice;       /* 00117428 refusals                  */
    CueSlot  cues[SFX_CUE_SLOTS];

    /* audio thread */
    EmSfxDriver driver;
    int      ticking;
    unsigned rate, grid;
    uint64_t frame, anchor;
} s;

/* ------------------------------------------------------------------ */
/* Audio-thread side                                                    */
/* ------------------------------------------------------------------ */

#define REQUEST_PENDING (1ull << 63)

static uint64_t pack_request(int32_t left, int32_t right)
{
    /* 20-bit fields; anything outside +-0x1000 stays outside, so 0011A218
     * still refuses it. */
    if (left < -0x80000) left = -0x80000;
    if (left > 0x7FFFF) left = 0x7FFFF;
    if (right < -0x80000) right = -0x80000;
    if (right > 0x7FFFF) right = 0x7FFFF;
    return REQUEST_PENDING | ((uint64_t)((uint32_t)left & 0xFFFFFu) << 20) |
           ((uint32_t)right & 0xFFFFFu);
}

static int32_t unpack_field(uint64_t value)
{
    int32_t field = (int32_t)(value & 0xFFFFFu);
    return field & 0x80000 ? field - 0x100000 : field;
}

/* STOP / HALT requests, at the start of every callback. */
static void sfx_apply_stops(void)
{
    for (int t = 0; t < EM_SFX_TRACKS; ++t) {
        int st = atomic_load_explicit(&s.track[t], memory_order_acquire);
        if (st != T_STOP && st != T_HALT) continue;
        const int hard = st == T_HALT;
        if (s.registry.entries)
            em_sfx_driver_stop(&s.driver, t, hard, NULL, NULL);
        if (hard && s.registry.entries) {
            /* The original's hard stop zeroes the ADSR words and keys the
             * voices off: a rate-0 release, gone within two samples. The
             * port silences them at once (the pre-existing stop-all
             * contract). */
            for (int v = 0; v < EM_SFX_VOICES; ++v) {
                EmSfxVoice *voice = &s.driver.voices[v];
                if (voice->kind != 2 || voice->owner != t) continue;
                voice->on = 0;
                voice->envelope.level = 0;
                voice->envelope.phase = EM_SFX_ENV_OFF;
            }
        }
        atomic_exchange_explicit(&s.request[t], 0, memory_order_relaxed);
        atomic_store_explicit(&s.track[t], T_FREE, memory_order_release);
    }
}

static int sfx_any_ready(void)
{
    for (int t = 0; t < EM_SFX_TRACKS; ++t)
        if (atomic_load_explicit(&s.track[t], memory_order_acquire) == T_READY)
            return 1;
    return 0;
}

/* One sequencer tick: adopt starts (in track order, as 001152D8 walks
 * them), hand over gain requests, run the driver, publish freed tracks. */
static void sfx_run_tick(void)
{
    for (int t = 0; t < EM_SFX_TRACKS; ++t) {
        int expected = T_READY;
        if (!atomic_compare_exchange_strong_explicit(
                &s.track[t], &expected, T_LIVE,
                memory_order_acquire, memory_order_relaxed)) continue;
        if (em_sfx_driver_start_at(&s.driver, t, s.start_entry[t],
                                   s.start_left[t], s.start_right[t]) < 0) {
            /* Unreachable: FREE is only published for a free driver track. */
            expected = T_LIVE;
            atomic_compare_exchange_strong_explicit(
                &s.track[t], &expected, T_FREE,
                memory_order_release, memory_order_relaxed);
        }
    }
    for (int t = 0; t < EM_SFX_TRACKS; ++t) {
        const uint64_t request =
            atomic_exchange_explicit(&s.request[t], 0, memory_order_acquire);
        if (request & REQUEST_PENDING &&
            atomic_load_explicit(&s.track[t], memory_order_relaxed) == T_LIVE)
            em_sfx_driver_request(&s.driver, t, unpack_field(request >> 20),
                                  unpack_field(request));
    }
    const unsigned refused = s.driver.no_voice;
    em_sfx_driver_tick(&s.driver, NULL, NULL);
    if (s.driver.no_voice != refused)
        atomic_fetch_add_explicit(&s.no_voice, s.driver.no_voice - refused,
                                  memory_order_relaxed);
    for (int t = 0; t < EM_SFX_TRACKS; ++t) {
        if (s.driver.tracks[t].allocated) continue;
        int expected = T_LIVE;
        atomic_compare_exchange_strong_explicit(
            &s.track[t], &expected, T_FREE,
            memory_order_release, memory_order_relaxed);
    }
    const int sounding = em_sfx_driver_voices(&s.driver);
    if (sounding > atomic_load_explicit(&s.max_concurrent, memory_order_relaxed))
        atomic_store_explicit(&s.max_concurrent, sounding, memory_order_relaxed);
}

static void sfx_mix_driver(float *out, unsigned frames, unsigned rate)
{
    if (rate != s.rate) {             /* re-anchor the tick grid */
        s.rate = rate;
        s.anchor = s.frame;
        s.grid = 0;
    }
    unsigned pos = 0;
    while (pos < frames) {
        if (!s.ticking) {
            if (!sfx_any_ready()) break;
            s.ticking = 1;
            s.anchor = s.frame;
            s.grid = 0;
        }
        const uint64_t due = s.anchor + em_sfx_tick_frame(s.grid, rate);
        if (s.frame >= due) {
            sfx_run_tick();
            s.grid++;
            continue;
        }
        const uint64_t room = due - s.frame;
        const unsigned count = room < frames - pos ? (unsigned)room : frames - pos;
        const long produced =
            em_sfx_driver_render(&s.driver, out + 2 * pos, count, rate);
        if (produced)
            atomic_fetch_add_explicit(&s.frames_mixed, produced,
                                      memory_order_relaxed);
        pos += count;
        s.frame += count;
        if (!em_sfx_driver_busy(&s.driver) && !sfx_any_ready()) s.ticking = 0;
    }
    s.frame += frames - pos;
}

static void sfx_mix_cues(float *out, int frames, int device_rate)
{
    for (int vi = 0; vi < SFX_CUE_SLOTS; vi++) {
        CueSlot *v = &s.cues[vi];
        int st = atomic_load_explicit(&v->state, memory_order_acquire);
        if (st != V_READY && st != V_PLAYING) continue;
        if (atomic_load_explicit(&v->kill, memory_order_acquire)) {
            atomic_store_explicit(&v->kill, 0, memory_order_relaxed);
            atomic_store_explicit(&v->state, V_FREE, memory_order_release);
            continue;
        }
        if (st == V_READY)
            atomic_store_explicit(&v->state, V_PLAYING, memory_order_relaxed);
        /* Original A0 pitch, Q14 voice gains and the verified steady
         * envelope (em_sfx_cue_frame). */
        float pair[2];
        long mixed = 0;
        for (int i = 0; i < frames; ++i) {
            if (!em_sfx_cue_frame(v->cue, v->output_frame,
                                  (unsigned)device_rate, pair)) break;
            out[2*i] += pair[0] * v->gl;
            out[2*i+1] += pair[1] * v->gr;
            ++v->output_frame;
            ++mixed;
        }
        if (mixed) atomic_fetch_add_explicit(&s.frames_mixed, mixed,
                                             memory_order_relaxed);
        if (!em_sfx_cue_frame(v->cue, v->output_frame,
                              (unsigned)device_rate, pair))
            atomic_store_explicit(&v->state, V_FREE, memory_order_release);
    }
}

void em_sfx_mix(float *out, int frames, int device_rate)
{
    if (device_rate <= 0 || device_rate > 384000 || frames <= 0 || !out) return;
    sfx_apply_stops();
    if (s.registry.entries)
        sfx_mix_driver(out, (unsigned)frames, (unsigned)device_rate);
    sfx_mix_cues(out, frames, device_rate);
}

/* ------------------------------------------------------------------ */
/* Game-thread side                                                     */
/* ------------------------------------------------------------------ */

int em_sfx_init(void)
{
    s.area = s.sub = -1;
    for (int t = 0; t < EM_SFX_TRACKS; ++t) s.requested[t] = s.snapshot[t] = -1;
    if (em_sfx_registry_load(&s.registry, SFX_REGISTRY)) {
        for (unsigned i = 0; i < s.registry.entry_count; ++i)
            s.n_sounds += s.registry.entries[i].state == EM_SFX_STATE_AUDIBLE;
        em_sfx_driver_init(&s.driver, &s.registry, SFX_STREAM_VOICES);
        printf("sfx: %d audible id(s) of %u registry entries from %s\n",
               s.n_sounds, s.registry.entry_count, SFX_REGISTRY);
    } else {
        fprintf(stderr, "sfx: %s missing or invalid (run "
                "tools/export_sfx_registry.py); registry ids are silent\n",
                SFX_REGISTRY);
    }

    if (em_sfx_bank_load(&s.bank, "assets/sfx/area11/panel_sfx.emsf")) {
        for (unsigned i = 0; i < s.bank.count; ++i)
            s.bank_cues[i] = &s.bank.cues[i];
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
     * EVERY source. (No translated call site passes a non-positive
     * radius today, so this is a latent divergence, not an observed
     * one.) */
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

/* 001FB9F0's track side: 00119EA0 takes the LOWEST free track. Returns the
 * track (the handle 001FC3C0 keeps) or -1 when all 48 are busy. */
static int sfx_start(const EmSfxEntry *entry, int32_t left, int32_t right)
{
    /* The device is em_bgm's; bring it up if music hasn't already. */
    if (em_bgm_device_ensure(SFX_DEV_RATE) != 0) return -1;
    for (int t = 0; t < EM_SFX_TRACKS; ++t) {
        int expected = T_FREE;
        if (!atomic_compare_exchange_strong_explicit(
                &s.track[t], &expected, T_STAGING,
                memory_order_acquire, memory_order_relaxed)) continue;
        s.start_entry[t] = entry;
        s.start_left[t] = left;
        s.start_right[t] = right;
        atomic_store_explicit(&s.track[t], T_READY, memory_order_release);
        s.plays++;
        return t;
    }
    s.drops++;
    return -1;
}

static void sfx_cue_submit(const EmSfxCue *cue, int32_t left, int32_t right)
{
    if (em_bgm_device_ensure(SFX_DEV_RATE) != 0) return;
    for (int vi = 0; vi < SFX_CUE_SLOTS; vi++) {
        CueSlot *v = &s.cues[vi];
        int expected = V_FREE;
        if (!atomic_compare_exchange_strong_explicit(
                &v->state, &expected, V_STAGING,
                memory_order_acquire, memory_order_relaxed)) continue;
        atomic_store_explicit(&v->kill, 0, memory_order_relaxed);
        v->cue = cue;
        v->gl = (float)left / 4096.0f;
        v->gr = (float)right / 4096.0f;
        v->output_frame = 0;
        atomic_store_explicit(&v->state, V_READY, memory_order_release);
        s.plays++;
        return;
    }
    s.drops++;
}

/* Scope order: the audited panel bank (AREA11 selected, when `panel`),
 * then the registry entry of the selected area, then the area-independent
 * entry. An id known only for another area, with no area selected, is an
 * unbound scope. */
static int sfx_lookup(unsigned id, SfxSound *out, int panel)
{
    memset(out, 0, sizeof *out);
    if (panel && s.bank_selected) {
        for (unsigned i = 0; i < s.bank.count; ++i) {
            if (s.bank_cues[i]->id != id) continue;
            out->cue = s.bank_cues[i];
            return (s.bank_cues[i]->flags & EM_SFX_CUE_ABSENT)
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
static int sfx_accept(unsigned id, SfxSound *snd, int panel)
{
    switch (sfx_lookup(id, snd, panel)) {
    case LOOKUP_AUDIBLE: return 1;
    case LOOKUP_ABSENT: ++s.absent; return 0;
    case LOOKUP_UNSUPPORTED:
        ++s.unsupported;
        sfx_report(id, "needs driver features the native driver does not "
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

static void sfx_submit(const SfxSound *snd, int32_t left, int32_t right)
{
    if (snd->cue) sfx_cue_submit(snd->cue, left, right);
    else sfx_start(snd->entry, left, right);
}

void em_sfx_play(unsigned id)
{
    SfxSound snd;
    if (!sfx_accept(id, &snd, 1)) return;
    /* center/full — func_001FB9F0(id, 0x1000, 0x1000, 0x1000), also the
     * exact play_sound result for a player-attached source */
    sfx_submit(&snd, 0x1000, 0x1000);
}

void em_sfx_play_at(unsigned id, const float pos[3], float radius)
{
    if (!pos) return;
    SfxSound snd;
    if (!sfx_accept(id, &snd, 1)) return;
    float gl, gr;
    if (!em_sfx_compute_gains(pos, radius, &gl, &gr)) {
        s.culls++;                      /* engine play_sound -1 */
        return;
    }
    /* 001FBF50 hands float_to_int(4096 * gain) words to 001FB9F0. */
    sfx_submit(&snd, em_sfx_request_word(gl), em_sfx_request_word(gr));
}

/* em_sfx_play_at with 001FBD50's return: the track 001FB9F0 allocated, or
 * -1 when the source is out of range (or the id plays nothing / through a
 * cue sequence, which allocates no track here). */
int em_sfx_play_at_track(unsigned id, const float pos[3], float radius)
{
    if (!pos) return -1;
    SfxSound snd;
    if (!sfx_accept(id, &snd, 1)) return -1;
    float gl, gr;
    if (!em_sfx_compute_gains(pos, radius, &gl, &gr)) {
        s.culls++;
        return -1;
    }
    if (snd.cue) {
        sfx_cue_submit(snd.cue, em_sfx_request_word(gl), em_sfx_request_word(gr));
        return -1;
    }
    return sfx_start(snd.entry, em_sfx_request_word(gl), em_sfx_request_word(gr));
}

/* ---- 001FC3C0 / 001FC520 service ---------------------------------------- */

typedef struct {
    const float *pos;
    float radius;
} SfxLoopSource;

static int loop_status(void *context, int track)
{
    (void)context;
    if (track < 0 || track >= EM_SFX_TRACKS) return 0;
    const int st = atomic_load_explicit(&s.track[track], memory_order_acquire);
    return st == T_STAGING || st == T_READY || st == T_LIVE ? 2 : 0;
}

static int loop_gains(void *context, int32_t *left, int32_t *right)
{
    const SfxLoopSource *source = context;
    float gl, gr;
    if (!source || !em_sfx_compute_gains(source->pos, source->radius, &gl, &gr)) {
        s.culls++;
        return 0;
    }
    *left = em_sfx_request_word(gl);
    *right = em_sfx_request_word(gr);
    return 1;
}

static void loop_request(void *context, int track, int32_t left, int32_t right)
{
    (void)context;
    if (track < 0 || track >= EM_SFX_TRACKS) return;
    atomic_store_explicit(&s.request[track], pack_request(left, right),
                          memory_order_release);
}

static void loop_stop(void *context, int track)
{
    (void)context;
    if (track < 0 || track >= EM_SFX_TRACKS) return;
    for (int from = T_READY; from <= T_LIVE; ++from) {
        int st = from;
        if (atomic_compare_exchange_strong_explicit(
                &s.track[track], &st, T_STOP,
                memory_order_acq_rel, memory_order_relaxed)) return;
    }
}

static int loop_start(void *context, unsigned id, int32_t left, int32_t right)
{
    (void)context;
    SfxSound snd;
    if (!sfx_accept(id, &snd, 0)) return -1;
    return sfx_start(snd.entry, left, right);
}

int32_t em_sfx_loop_service(int32_t *handle, unsigned id, const float pos[3],
                            float radius, int32_t frame, int16_t ordinal)
{
    if (!handle || !pos) return -1;
    SfxLoopSource source = {pos, radius};
    const EmSfxLoopOps ops = {&source, loop_status, loop_gains, loop_request,
                              loop_stop, loop_start};
    return em_sfx_service_step(&ops, s.requested, s.snapshot, handle, id,
                               frame, ordinal);
}

void em_sfx_loop_release(int32_t *handle)
{
    if (!handle) return;
    const EmSfxLoopOps ops = {NULL, loop_status, loop_gains, loop_request,
                              loop_stop, loop_start};
    em_sfx_service_release(&ops, s.requested, handle);
}

int em_sfx_stop_track(int track, int hard)
{
    if (track < 0 || track >= EM_SFX_TRACKS) return -1;
    if (!hard) {
        loop_stop(NULL, track);
        return 0;
    }
    for (int from = T_READY; from <= T_STOP; ++from) {
        int st = from;
        if (atomic_compare_exchange_strong_explicit(
                &s.track[track], &st, T_HALT,
                memory_order_acq_rel, memory_order_relaxed)) break;
    }
    return 0;
}

void em_sfx_frame_snapshot(void)
{
    memcpy(s.snapshot, s.requested, sizeof s.snapshot);
}

/* Stop every live voice — func_001FBC50: 0011A198(1) hard-stops every
 * allocated SFX track (0011A070(track | 0x8000)) and D_00281B70/C30 return
 * to -1. The port silences the stopped tracks' voices at the next
 * callback start; the audited panel-cue slots are killed there too.
 *
 * CALLERS: em_frontend.c (movie start, EM_STARTUP_AUDIO_STOP),
 * em_opening_media_audio_start, the scene bindings' w_001FBC50 (0x1AE040
 * state 1's status open, r == 2, and its r == 1 arm; with the translated
 * 001FABB0 stream stop and the state-5 001FAE70(1) resume the status audio
 * stops and resumes on the original's schedule, but 001FBC50's and
 * 001FC280's 00119828(0/1, 0x1999, 0x1999) stream volumes are only
 * reported (UM_00119828: the port's streams have no per-channel gain), so
 * H22/AM-06 stays partial, WP-5) and the
 * interaction host's EM_STATUS_RESET_SOUNDS handler (the fixtures' status
 * frame machine). Not wired at the death entry: that entry has just
 * started the death voice and body cues.
 * Safe to call from the game thread. */
void em_sfx_stop_all(void)
{
    for (int t = 0; t < EM_SFX_TRACKS; ++t) {
        s.requested[t] = s.snapshot[t] = -1;
        for (int from = T_READY; from <= T_STOP; ++from) {
            int st = from;
            if (atomic_compare_exchange_strong_explicit(
                    &s.track[t], &st, T_HALT,
                    memory_order_acq_rel, memory_order_relaxed)) break;
        }
    }
    for (int vi = 0; vi < SFX_CUE_SLOTS; vi++) {
        CueSlot *v = &s.cues[vi];
        int st = atomic_load_explicit(&v->state, memory_order_acquire);
        if (st == V_READY || st == V_PLAYING)
            atomic_store_explicit(&v->kill, 1, memory_order_release);
    }
}

void em_sfx_shutdown(void)
{
    /* Caller contract: em_bgm_shutdown already destroyed the device, so
     * no callback can be reading the samples or the tracks. */
    if (s.plays || s.drops || s.culls)
        printf("sfx: %d play(s), %d dropped (no track), %u key-on(s) without "
               "a voice, %d culled (range), %ld voice frames mixed, peak %d "
               "concurrent\n",
               s.plays, s.drops,
               atomic_load_explicit(&s.no_voice, memory_order_relaxed),
               s.culls,
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
int  em_sfx_steals(void)      { return 0; }
int  em_sfx_culls(void)       { return s.culls; }
int  em_sfx_absent_cues(void) { return s.absent; }
int  em_sfx_unsupported_cues(void) { return s.unsupported; }
int  em_sfx_unscoped_cues(void)    { return s.unscoped; }

unsigned em_sfx_voice_refusals(void)
{
    return atomic_load_explicit(&s.no_voice, memory_order_relaxed);
}

int em_sfx_track_status(int track)
{
    return loop_status(NULL, track);
}

int em_sfx_cue_state(unsigned id)
{
    SfxSound snd;
    switch (sfx_lookup(id, &snd, 1)) {
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
