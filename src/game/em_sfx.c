/* em_sfx.c — the SFX sound driver. See em_sfx.h for the engine model.
 *
 * DRIVER (WP-14, docs/SFX_SEQUENCER.md): registry plays run through the
 * translated original sound driver in em_sfx_bank.c — 00119EA0 tracks,
 * the 001152D8 tick (00115850 key-on with 00117428 allocation, 001176E0
 * key-off, 00118078 portamento, 00116598 re-sends, 00118EC0 reaper) and a
 * 48-voice SPU2 model with ADSR stepping and loop replay.
 *
 * WHERE IT RUNS (chain step AUDIO, 2026-10-08): the game thread owns the
 * driver, as the EE owns it in the original. The game's own calls
 * (001FB9F0 / 00119EA0, 0011A218, 0011A070, 00119890) act on the driver at
 * once, and the sequencer tick runs once per NTSC field, at the vblank:
 * the original's vblank handler 001AB140 wakes the sound thread 001FB0C0
 * (priority 2, created by 001F9780), whose 001152B0 runs 001152D8 before
 * the main loop resumes (em_sfx_field, called from em_stream_live_field).
 * After the tick the SPU2 model renders the field's samples (800 or 801 at
 * 48 kHz, the field clock of em_sfx_tick_frame and the IOP stream backend)
 * into a lock-free ring that the audio thread drains (em_sfx_mix), the
 * pattern of the stream backend (em_iop_stream_mix). So the tracks, the
 * voice records D_0027CCC0, the allocation cursor and the reaper's ENVX
 * feedback follow the game's field count, never the host's audio clock;
 * the level smoke compares them with the original's per-frame sound state
 * (tools/level_smoke_audio.py). The earlier hand-off, in which the device
 * callback ran the ticks on wall-clock time, made the track a sound got
 * host-timed (FIRST_LEVEL_AUDIT.md item 1, the AIM fix round's finding).
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
#define SFX_DEV_RATE   48000 /* the SPU2 rate; the device must run at it   */
#define SFX_PAN_NEAR   18.0f /* func_001FBF50 proximity pan ramp: |t|
                              * forced toward 1 (center) inside 18 u     */
#define SFX_PI         3.14159265358979323846f
/* Voices 0..3 are the stream voices (kind 3) in every AREA11 capture;
 * 00117428 never hands them to a key-on. */
#define SFX_STREAM_VOICES 0xFull
#define SFX_RING_FRAMES 16384 /* mixer ring (stereo frames), as the streams' */
#define SFX_FIELD_MAX   801   /* samples in one field at 48 kHz            */
/* The IOP clock (em_iop_stream's model): 525 half-lines per field, a driver
 * tick every 128 half-lines (64 H-lines), 572 / 375 samples per half-line. */
#define SFX_HALF_LINES_PER_FIELD 525u
#define SFX_HALF_LINES_PER_TICK  128u
#define SFX_SAMPLES_NUM 572u
#define SFX_SAMPLES_DEN 375u
#define SFX_QUEUE    255      /* 001157F0's entries per exchange             */
#define SFX_IOP_RING 1024     /* commands one exchange can leave unrun       */
/* D_00281D50: 6 buckets of 20 handles (001FB9F0 reads [group * 20 + bank]). */
#define SFX_HANDLE_WORDS 120

enum { LOOKUP_NONE = 0, LOOKUP_AUDIBLE, LOOKUP_ABSENT, LOOKUP_UNSUPPORTED,
       LOOKUP_UNSCOPED };

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
    int32_t  requested[EM_SFX_TRACKS]; /* D_00281B70 */
    int32_t  snapshot[EM_SFX_TRACKS];  /* D_00281C30 (001FB100 copy)  */
    /* listener mirror (em_sfx_listener; game thread) */
    int      lis_valid;
    float    lis_player[3];  /* D_00810360 — distance listener        */
    float    lis_eye[3];     /* D_008105D0 — pan listener             */
    float    lis_yaw;        /* D_0081027C (cam+0x9C)                 */
    /* The driver: tracks D_0027E0C0, voices D_0027CCC0, the cursor and
     * serial D_0027F740 + 0x30 / + 0x34, and the SPU2 model whose ENVX is
     * the reaper's D_002817C0 feedback. */
    EmSfxDriver driver;
    uint64_t ticks;          /* sequencer ticks run (fields)          */
    /* The IOP side (em_sfx_field): the EE queue, the IOP driver's ring,
     * its status block (ENVX per voice) and the reply the last exchange
     * delivered (D_002817C0), on the SFX side's own half-line clock. */
    uint32_t queue[SFX_QUEUE][4];
    unsigned queued, queue_drops;
    uint32_t ring[SFX_IOP_RING][4];
    unsigned ring_n;
    int32_t  status[EM_SFX_VOICES], reply[EM_SFX_VOICES], feedback[EM_SFX_VOICES];
    uint64_t half_line;      /* the SFX side's IOP clock at the next field */
    uint64_t samples;        /* SPU2 samples rendered (absolute)      */
    long     frames_mixed;   /* summed voice frames rendered          */
    int      max_concurrent; /* peak sounding voices                  */
    const char *fault;       /* latched: em_sfx_field returns -1      */
    uint64_t digest;         /* FNV-1a over every rendered frame      */

    /* the mixer ring: written by the game thread, drained by the audio
     * thread (em_sfx_mix); host output, not the driver's state */
    float    pcm[SFX_RING_FRAMES * 2];
    _Atomic uint64_t pcm_write, pcm_read;
    _Atomic uint64_t overruns, underruns, bad_rate;
} s;

/* D_00281D50's reader (em_stream_live binds the sound bank's table). */
static const int32_t *(*s_bank_handles)(void);

void em_sfx_bind_bank_handles(const int32_t *(*reader)(void)) { s_bank_handles = reader; }

static void sfx_fault(const char *what)
{
    if (!s.fault) {
        s.fault = what;
        fprintf(stderr, "sfx: FAULT %s\n", what);
    }
}

/* ------------------------------------------------------------------ */
/* The field: the sequencer tick, then the field's SPU2 samples         */
/* ------------------------------------------------------------------ */

static void ring_push(const float *frames, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        uint32_t bits[2];
        memcpy(&bits[0], &frames[2 * i], 4);
        memcpy(&bits[1], &frames[2 * i + 1], 4);
        for (int k = 0; k < 2; ++k)
            for (int b = 0; b < 4; ++b) {
                s.digest ^= (bits[k] >> (8 * b)) & 0xFFu;
                s.digest *= 0x100000001B3ull;
            }
        const uint64_t w = atomic_load_explicit(&s.pcm_write, memory_order_relaxed);
        const uint64_t r = atomic_load_explicit(&s.pcm_read, memory_order_acquire);
        if (w - r >= SFX_RING_FRAMES) {
            atomic_fetch_add_explicit(&s.overruns, 1, memory_order_relaxed);
            continue;
        }
        s.pcm[(w % SFX_RING_FRAMES) * 2] = frames[2 * i];
        s.pcm[(w % SFX_RING_FRAMES) * 2 + 1] = frames[2 * i + 1];
        atomic_store_explicit(&s.pcm_write, w + 1, memory_order_release);
    }
}

/* The EE command queue D_0027F7C0 (001157F0: 255 entries per exchange;
 * a full queue drops the command, as 001157F0's -1, which the driver
 * ignores). */
static void queue_push(void *context, int command, int voice, uint32_t a, uint32_t b)
{
    (void)context;
    if (s.queued >= SFX_QUEUE) {
        s.queue_drops++;
        return;
    }
    uint32_t *e = s.queue[s.queued++];
    e[0] = (uint32_t)command;
    e[1] = (uint32_t)voice;
    e[2] = a;
    e[3] = b;
}

/* SPU2 samples up to the absolute sample `target` of the IOP clock. */
static void render_to(uint64_t target)
{
    float out[2 * SFX_FIELD_MAX];
    while (s.samples < target) {
        unsigned count = target - s.samples > SFX_FIELD_MAX ? SFX_FIELD_MAX
                                                            : (unsigned)(target - s.samples);
        memset(out, 0, sizeof(float) * 2u * count);
        s.frames_mixed += em_sfx_driver_render(&s.driver, out, count, SFX_DEV_RATE);
        ring_push(out, count);
        s.samples += count;
    }
}

int em_sfx_field(void)
{
    if (s.fault) return -1;
    if (!s.registry.entries) return 0;
    /* The IOP driver's tick grid on the SFX side's own field clock, phase 0
     * at the first field after the registry's load (the New Game's area
     * load): the timer's phase against the frame is hardware timing (in
     * the original it follows how long the title ran), and a clock of the
     * port's own keeps every run's sound state the same from New Game on. */
    const uint64_t half_line = s.half_line;
    /* 001192D0 waits for the previous exchange; its reply is D_002817C0,
     * which the tick's reaper reads. */
    memcpy(s.feedback, s.reply, sizeof s.feedback);
    s.driver.feedback = s.feedback;
    em_sfx_driver_tick(&s.driver, queue_push, NULL);
    /* The tick's RPC 0x64: the queue joins the IOP driver's ring; the reply
     * is the status block as the driver's last tick wrote it. */
    memcpy(s.reply, s.status, sizeof s.reply);
    for (unsigned i = 0; i < s.queued; ++i) {
        if (s.ring_n >= SFX_IOP_RING) {
            sfx_fault("the IOP driver's command ring overflowed");
            return -1;
        }
        memcpy(s.ring[s.ring_n++], s.queue[i], sizeof s.queue[i]);
    }
    s.queued = 0;
    /* The field: the IOP driver ticks every 64 H-lines (em_iop_stream's
     * clock); each runs the ring's commands on the SPU2 model, then
     * snapshots every voice's ENVX & 0x7FFF into the status block. */
    const uint64_t end = half_line + SFX_HALF_LINES_PER_FIELD;
    for (uint64_t h = (half_line / SFX_HALF_LINES_PER_TICK + 1) * SFX_HALF_LINES_PER_TICK;
         h <= end; h += SFX_HALF_LINES_PER_TICK) {
        render_to(h * SFX_SAMPLES_NUM / SFX_SAMPLES_DEN);
        for (unsigned i = 0; i < s.ring_n; ++i)
            em_sfx_driver_apply(&s.driver, (int)s.ring[i][0], (int)s.ring[i][1], s.ring[i][2],
                                s.ring[i][3]);
        s.ring_n = 0;
        for (int v = 0; v < EM_SFX_VOICES; ++v)
            s.status[v] = em_sfx_driver_envx(&s.driver, v) & 0x7FFF;
    }
    render_to(end * SFX_SAMPLES_NUM / SFX_SAMPLES_DEN);
    s.half_line = end;
    const int sounding = em_sfx_driver_voices(&s.driver);
    if (sounding > s.max_concurrent) s.max_concurrent = sounding;
    s.ticks++;
    return 0;
}

void em_sfx_mix(float *out, int frames, int device_rate)
{
    if (frames <= 0 || !out) return;
    if (device_rate != SFX_DEV_RATE) {
        atomic_fetch_add_explicit(&s.bad_rate, 1, memory_order_relaxed);
        return;
    }
    const uint64_t r = atomic_load_explicit(&s.pcm_read, memory_order_relaxed);
    const uint64_t w = atomic_load_explicit(&s.pcm_write, memory_order_acquire);
    const uint64_t n = w - r < (uint64_t)frames ? w - r : (uint64_t)frames;
    for (uint64_t i = 0; i < n; ++i) {
        out[2 * i] += s.pcm[((r + i) % SFX_RING_FRAMES) * 2];
        out[2 * i + 1] += s.pcm[((r + i) % SFX_RING_FRAMES) * 2 + 1];
    }
    atomic_store_explicit(&s.pcm_read, r + n, memory_order_release);
    if (n < (uint64_t)frames)
        atomic_fetch_add_explicit(&s.underruns, (uint64_t)frames - n, memory_order_relaxed);
}

int em_sfx_voice_record(int voice, uint16_t *state, uint16_t *bank)
{
    if (voice < 0 || voice >= EM_SFX_VOICES || !state || !bank) return -1;
    *state = s.driver.voices[voice].state;
    *bank = s.driver.voices[voice].bank;
    return 0;
}

const EmSfxDriver *em_sfx_driver_state(uint64_t *ticks)
{
    if (ticks) *ticks = s.ticks;
    return s.registry.entries ? &s.driver : NULL;
}

/* ------------------------------------------------------------------ */
/* Game-thread side                                                     */
/* ------------------------------------------------------------------ */

int em_sfx_init(void)
{
    /* Once per process: every area load calls it (em_game_legacy_area_load),
     * and the original's sound driver keeps its tracks, voices, cursor and
     * serial across an area load (one registry holds every area's scopes;
     * em_sfx_set_area selects one). */
    if (s.registry.entries) return s.n_sounds;
    s.area = s.sub = -1;
    for (int t = 0; t < EM_SFX_TRACKS; ++t) s.requested[t] = s.snapshot[t] = -1;
    if (em_sfx_registry_load(&s.registry, SFX_REGISTRY)) {
        for (unsigned i = 0; i < s.registry.entry_count; ++i)
            s.n_sounds += s.registry.entries[i].state == EM_SFX_STATE_AUDIBLE;
        em_sfx_driver_init(&s.driver, &s.registry, SFX_STREAM_VOICES);
        s.driver.deferred = 1;
        s.driver.feedback = s.feedback;
        printf("sfx: %d audible id(s) of %u registry entries from %s\n",
               s.n_sounds, s.registry.entry_count, SFX_REGISTRY);
    } else {
        fprintf(stderr, "sfx: %s missing or invalid (run "
                "tools/export_sfx_registry.py); registry ids are silent\n",
                SFX_REGISTRY);
    }
    return s.n_sounds;
}

int em_sfx_set_area(int area, int sub)
{
    s.area = s.sub = -1;
    if (area < 0 || sub < 0) return 1;
    s.area = area;
    s.sub = sub;
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
/* D_0028215B, the committed output mode 001FBF50 tests: the stream lanes'
 * byte (em_stream_live binds its reader at the boot, 001F9820). Fixtures
 * that run em_sfx without the stream lanes have no reader: they see the
 * byte 001FB210 commits at the boot from the boot settings (0, stereo). */
static const uint8_t *(*s_output_mode)(void);

void em_sfx_bind_output_mode(const uint8_t *(*reader)(void)) { s_output_mode = reader; }

/* D_00810360's reader (the game binds the player record's +0xB0 view);
 * fixtures without one use em_sfx_listener's player position. */
static int (*s_distance_listener)(float out[3]);

void em_sfx_bind_distance_listener(int (*reader)(float out[3])) { s_distance_listener = reader; }

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

    /* DISTANCE: player listener (D_00810360, read at the call: the
     * player record's +0xB0, which 0015BCF0's tail leaves as the bone-1
     * hip), full 3-D (flat2d = 0 at every translated call site). */
    float listener[3];
    memcpy(listener, s.lis_player, sizeof listener);
    if (s_distance_listener && s_distance_listener(listener) < 0) return 0;
    float dx = pos[0] - listener[0];
    float dy = pos[1] - listener[1];
    float dz = pos[2] - listener[2];
    float d  = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d >= radius) return 0;          /* engine: not submitted */
    float vol = sinf((SFX_PI * 0.5f) * (radius - d) / radius);

    /* 001FBF50's mono arm (D_0028215B == 1): both channels get the volume,
     * no pan. */
    const uint8_t *mode = s_output_mode ? s_output_mode() : NULL;
    if (s_output_mode && !mode) return 0; /* the lanes faulted: nothing is submitted */
    if (mode && *mode == 1) {
        *gain_l = *gain_r = vol;
        return 1;
    }

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

/* 001FB9F0's tail: the handle D_00281D50[group * 20 + bank], then
 * 00119EA0 (the LOWEST free track), 0011A270(track, 0x1000) and
 * 0011A218(track, left, right). Returns the track (the handle 001FC3C0
 * keeps) or -1 when all 48 are busy. */
static int sfx_start(const EmSfxEntry *entry, int32_t left, int32_t right)
{
    const int32_t *handles = s_bank_handles ? s_bank_handles() : NULL;
    const unsigned group = entry->bank >> 8, bank = entry->bank & 0xFF;
    if (!handles || group * 20u + bank >= SFX_HANDLE_WORDS) {
        sfx_fault(handles ? "001FB9F0: a record's bank outside D_00281D50"
                          : "001FB9F0 without D_00281D50 (the sound bank is not bound)");
        return -1;
    }
    /* The device is em_bgm's; bring it up if music hasn't already. */
    if (em_bgm_device_ensure(SFX_DEV_RATE) != 0) return -1;
    const int track = em_sfx_driver_start(&s.driver, entry,
                                          (unsigned)handles[group * 20u + bank],
                                          left, right);
    if (track < 0) {
        s.drops++;
        return -1;
    }
    s.plays++;
    return track;
}

/* Scope order: the registry entry of the selected area, then the
 * area-independent entry. An id known only for another area, with no area
 * selected, is an unbound scope. */
static int sfx_lookup(unsigned id, const EmSfxEntry **out)
{
    *out = NULL;
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
    *out = entry;
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

/* Shared gate: the entry when the id should be submitted, else NULL. */
static const EmSfxEntry *sfx_accept(unsigned id)
{
    const EmSfxEntry *entry;
    switch (sfx_lookup(id, &entry)) {
    case LOOKUP_AUDIBLE: return entry;
    case LOOKUP_ABSENT: ++s.absent; return NULL;
    case LOOKUP_UNSUPPORTED:
        ++s.unsupported;
        sfx_report(id, "needs driver features the native driver does not "
                       "reproduce (docs/SFX_PITCH.md); silent");
        return NULL;
    case LOOKUP_UNSCOPED:
        ++s.unscoped;
        sfx_report(id, "is area-dependent and was not exported for the "
                       "em_sfx_set_area scope (or none is bound); silent");
        return NULL;
    default: return NULL;        /* unmapped id: silent no-op */
    }
}

void em_sfx_play(unsigned id)
{
    const EmSfxEntry *entry = sfx_accept(id);
    /* center/full — func_001FB9F0(id, 0x1000, 0x1000, 0x1000), also the
     * exact play_sound result for a player-attached source */
    if (entry) sfx_start(entry, 0x1000, 0x1000);
}

void em_sfx_submit_001FB9F0(unsigned id, int32_t left, int32_t right)
{
    const EmSfxEntry *entry = sfx_accept(id);
    if (entry) sfx_start(entry, left, right);
}

int em_sfx_submit_001FB9F0_track(unsigned id, int32_t left, int32_t right)
{
    const EmSfxEntry *entry = sfx_accept(id);
    return entry ? sfx_start(entry, left, right) : -1;
}

void em_sfx_tables(int32_t requested[EM_SFX_TRACKS], int32_t snapshot[EM_SFX_TRACKS])
{
    memcpy(requested, s.requested, sizeof s.requested);
    memcpy(snapshot, s.snapshot, sizeof s.snapshot);
}

void em_sfx_set_snapshot(const int32_t snapshot[EM_SFX_TRACKS])
{
    memcpy(s.snapshot, snapshot, sizeof s.snapshot);
}

void em_sfx_play_at(unsigned id, const float pos[3], float radius)
{
    (void)em_sfx_play_at_track(id, pos, radius);
}

/* em_sfx_play_at with 001FBD50's return: the track 001FB9F0 allocated, or
 * -1 when the source is out of range (or the id plays nothing). */
int em_sfx_play_at_track(unsigned id, const float pos[3], float radius)
{
    if (!pos) return -1;
    const EmSfxEntry *entry = sfx_accept(id);
    if (!entry) return -1;
    float gl, gr;
    if (!em_sfx_compute_gains(pos, radius, &gl, &gr)) {
        s.culls++;                      /* engine play_sound -1 */
        return -1;
    }
    /* 001FBF50 hands float_to_int(4096 * gain) words to 001FB9F0. */
    return sfx_start(entry, em_sfx_request_word(gl), em_sfx_request_word(gr));
}

/* ---- 001FC3C0 / 001FC520 service ---------------------------------------- */

typedef struct {
    const float *pos;
    float radius;
} SfxLoopSource;

/* 00119890(1, track): 2 while the track is allocated. */
static int loop_status(void *context, int track)
{
    (void)context;
    return em_sfx_driver_status(&s.driver, track);
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

/* 0011A218(track, left, right). */
static void loop_request(void *context, int track, int32_t left, int32_t right)
{
    (void)context;
    em_sfx_driver_request(&s.driver, track, left, right);
}

/* 0011A070(track): the soft stop. */
static void loop_stop(void *context, int track)
{
    (void)context;
    em_sfx_driver_stop(&s.driver, track, 0, queue_push, NULL);
}

static int loop_start(void *context, unsigned id, int32_t left, int32_t right)
{
    (void)context;
    const EmSfxEntry *entry = sfx_accept(id);
    return entry ? sfx_start(entry, left, right) : -1;
}

int32_t em_sfx_loop_service(int32_t *handle, unsigned id, const float pos[3],
                            float radius, int32_t frame, int16_t ordinal)
{
    if (!handle || !pos) return -1;
    SfxLoopSource source = {pos, radius};
    const EmSfxLoopOps ops = {&source, loop_status, loop_gains, loop_request,
                              loop_stop, loop_start, NULL};
    return em_sfx_service_step(&ops, s.requested, s.snapshot, handle, id,
                               frame, ordinal);
}

void em_sfx_loop_release(int32_t *handle)
{
    if (!handle) return;
    const EmSfxLoopOps ops = {NULL, loop_status, loop_gains, loop_request,
                              loop_stop, loop_start, NULL};
    em_sfx_service_release(&ops, s.requested, handle);
}

typedef struct { void *ctx;EmSfxLoopGain gains;EmSfxLoopStore store;int updating,fault; } BoundLoop;
static int bound_status(void *ctx,int track)
{ BoundLoop *b=ctx;b->updating=1;return loop_status(NULL,track); }
static int bound_gains(void *ctx,int32_t *left,int32_t *right)
{
    BoundLoop *b=ctx;int rc=b->gains?b->gains(b->ctx,b->updating,left,right):-1;
    if(rc<0)b->fault=1;return rc;
}
static int bound_store(void *ctx,int32_t *handle,int32_t value)
{
    BoundLoop *b=ctx;(void)handle;
    if(!b->store || b->store(b->ctx,value)<0){b->fault=1;return -1;}return 0;
}
int em_sfx_loop_service_bound(int32_t handle,unsigned id,int32_t frame,int16_t ordinal,
                              int release,EmSfxLoopGain gains,EmSfxLoopStore store,void *ctx)
{
    if(!store || (!release && !gains) || handle < -1 || handle>=EM_SFX_TRACKS)return -1;
    BoundLoop b={.ctx=ctx,.gains=gains,.store=store};
    const EmSfxLoopOps ops={&b,bound_status,bound_gains,loop_request,loop_stop,loop_start,bound_store};
    if(release)em_sfx_service_release(&ops,s.requested,&handle);
    else (void)em_sfx_service_step(&ops,s.requested,s.snapshot,&handle,id,frame,ordinal);
    return b.fault?-1:0;
}

/* 0011A070(track | (hard ? 0x8000 : 0)): it frees the track when it is
 * allocated and keys off every SFX voice whose +0x06 names it, allocated
 * or not. The hard stop's command 3 (ADSR 0) reaches the SPU2 model at
 * once and its key-off with the next tick: both reach the IOP in the
 * tick's one exchange, and the SPU2 model renders only after the tick. */
int em_sfx_stop_track(int track, int hard)
{
    if (track < 0 || track >= EM_SFX_TRACKS) return -1;
    em_sfx_driver_stop(&s.driver, track, hard, queue_push, NULL);
    return 0;
}

uint64_t em_sfx_stream_voices(void)
{
    return SFX_STREAM_VOICES;
}


/* Stop every live voice — func_001FBC50: 0011A198(1) hard-stops every
 * allocated SFX track (0011A070(track | 0x8000); its +0x2E test is 0 on
 * every SFX track) and D_00281B70/C30 return to -1.
 *
 * CALLERS: em_frontend.c (movie start), the scene bindings' w_001FBC50
 * (0x1AE040 state 1's status open, r == 2, and its r == 1 arm; 001FD470
 * bit 0; 001AC3B0's audio stop; the scripts' stops), which then sends
 * 001FBC50's two 00119828(0/1, 0x1999, 0x1999) IOP commands through the
 * stream lanes (em_stream_live, WP-8b). Not wired at the death entry: that entry has just
 * started the death voice and body cues.
 * Game thread. */
void em_sfx_stop_all(void)
{
    for (int t = 0; t < EM_SFX_TRACKS; ++t) {
        s.requested[t] = s.snapshot[t] = -1;
        if (em_sfx_driver_status(&s.driver, t))
            em_sfx_driver_stop(&s.driver, t, 1, queue_push, NULL);
    }
}

const void *em_sfx_host_clock(size_t *size)
{
    /* The mixer ring and its counters: host output and the audio thread's
     * read position, not the driver's state. */
    *size = (size_t)((const char *)(&s.bad_rate + 1) - (const char *)s.pcm);
    return s.pcm;
}

void em_sfx_mix_counters(uint64_t *overruns, uint64_t *underruns, uint64_t *bad_rate,
                         uint64_t *digest)
{
    if (overruns) *overruns = atomic_load_explicit(&s.overruns, memory_order_relaxed);
    if (underruns) *underruns = atomic_load_explicit(&s.underruns, memory_order_relaxed);
    if (bad_rate) *bad_rate = atomic_load_explicit(&s.bad_rate, memory_order_relaxed);
    if (digest) *digest = s.digest;
}

void em_sfx_shutdown(void)
{
    /* Caller contract: em_bgm_shutdown already destroyed the device, so
     * no callback can be reading the ring. */
    if (s.plays || s.drops || s.culls)
        printf("sfx: %d play(s), %d dropped (no track), %u key-on(s) without "
               "a voice, %d culled (range), %ld voice frames mixed, peak %d "
               "concurrent\n",
               s.plays, s.drops, s.driver.no_voice, s.culls, s.frames_mixed,
               s.max_concurrent);
    em_sfx_registry_free(&s.registry);
    s.fault = NULL;
    s.ticks = s.samples = s.half_line = 0;
    s.queued = s.queue_drops = s.ring_n = 0;
    memset(s.status, 0, sizeof s.status);
    memset(s.reply, 0, sizeof s.reply);
    memset(s.feedback, 0, sizeof s.feedback);
    s.frames_mixed = 0;
    s.max_concurrent = 0;
    s.digest = 0;
    s.plays = s.drops = s.culls = s.absent = s.unsupported = s.unscoped = s.n_sounds = 0;
    s.lis_valid = 0;
    memset(s.reported, 0, sizeof s.reported);
    memset(&s.driver, 0, sizeof s.driver);
    atomic_store(&s.pcm_write, 0);
    atomic_store(&s.pcm_read, 0);
    atomic_store(&s.overruns, 0);
    atomic_store(&s.underruns, 0);
    atomic_store(&s.bad_rate, 0);
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
unsigned em_sfx_voice_refusals(void) { return s.driver.no_voice; }
int em_sfx_track_status(int track) { return loop_status(NULL, track); }

int em_sfx_cue_state(unsigned id)
{
    const EmSfxEntry *entry;
    switch (sfx_lookup(id, &entry)) {
    case LOOKUP_AUDIBLE: return 1;
    case LOOKUP_ABSENT: return 2;
    default: return 0;
    }
}

long em_sfx_frames_mixed(void) { return s.frames_mixed; }
int em_sfx_max_concurrent(void) { return s.max_concurrent; }
