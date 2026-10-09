/* The live music and voice streams (WP-8b). See em_stream_live.h,
 * docs/STREAM_LANES.md and docs/IOP_STREAM.md ("Binding"). */
#include "game/em_stream_live.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "em_settings.h"
#include "game/em_bgm.h"
#include "game/em_game_internal.h"
#include "game/em_iop_stream.h"
#include "game/em_message_service.h"
#include "game/em_random.h"
#include "game/em_scene_bindings.h"
#include "game/em_sfx.h"
#include "game/em_sfx_bank.h"
#include "game/em_sound_bank.h"
#include "game/em_startup_load_gaps.h"
#include "game/em_stream_lanes_original.h"

enum { VOICE_RECORD = 0x6A };

static struct {
    /* The lanes pass one ctx to every worker; em_iop_stream's adapters read
     * the EmIopStream pointer from its first member (IOP_STREAM.md
     * "Binding" 3). */
    struct {
        EmIopStream *iop;
    } ctx;
    int booted;
    EmIopStreamDisc disc;
    EmStreamLanesData data;
    EmStreamLanesGlobals globals;
    EmStreamLanes lanes;
    /* D_0027CCC0 (48 records) and D_0027F740 + 0x28: the raw view 0011A2B0
     * scans once, in 001F9820 at the boot. The SFX driver owns the table
     * afterwards (em_sfx_bank's parsed records); the boot asserts that the
     * voices 001F9820 took are the ones the SFX driver reserves. */
    uint8_t voice_table[48 * VOICE_RECORD];
    uint64_t voice_scan;
    uint32_t irx_buffers[5]; /* D_00275B50, D_00275B28, D_00275B4C, D_00275B24, D_00275B20 */
    /* The sound-bank upload 001FB370 over this IOP (em_sound_bank; bound by
     * em_stream_live_bind_sound_bank once the loader's pack gives
     * D_00264890). */
    EmSoundBank bank;
    int bank_bound;
    /* 001FB100's own storage (step H): D_00281F30,
     * 001FC6E0's ten delayed cues {delay, cue, a2, a3} (001FBC50 leaves
     * {0, -1, 0, 0}; their writer 001FC580 stores through
     * em_stream_live_d281F30, the boxes' break cue). */
    int32_t d281F30[EM_SLG_CUES][4];
    int depth;               /* nested entries (a worker re-entering) skip the sync */
    int device;              /* em_bgm's device ensured for the first stream */
    const char *fault;
    int reported;
} S;

static _Atomic(EmIopStream *) s_mixer;

static int fail(const char *why)
{
    if (!S.fault) S.fault = why;
    if (!S.reported) {
        S.reported = 1;
        const EmStreamLanesFault *lf = &S.lanes.fault;
        const EmIopStreamFault *io = S.ctx.iop ? em_iop_stream_fault(S.ctx.iop) : NULL;
        fprintf(stderr, "stream lanes: fault: %s (lanes %08X code %d; iop %08X code %d)\n", S.fault,
                (unsigned)lf->address, (int)lf->code, io ? (unsigned)io->address : 0u,
                io ? (int)io->code : 0);
    }
    return -1;
}

/* ------------------------------------------------------------ workers */

/* 00122BB8: the game LCG (the same stream as every other caller). */
static int w_00122BB8(void *ctx, int32_t *value)
{
    (void)ctx;
    *value = (int32_t)em_random_next();
    return 0;
}

/* 001FC280: the room ambience owner (em_scene_bindings, which sends its two
 * 00119828 calls back to em_stream_live_00119828). */
static int w_001FC280(void *ctx)
{
    (void)ctx;
    return em_scene_bindings_001FC280();
}

/* 001FBC50: the SFX stop-all (001FD470 bit 0). */
static int w_001FBC50(void *ctx)
{
    (void)ctx;
    return em_scene_bindings_001FBC50();
}

/* ------------------------------------------------------------ the view */

static uint8_t *d810D38(void)
{
    return em_scene_progress_at(em_scene_state(), 0x00810D38u, 4);
}

static int sync_in(void)
{
    EmSceneState *s = em_scene_state();
    const uint8_t *d38 = d810D38();
    uint8_t *f4 = em_scene_req_at(s, 0x008106F4u);
    if (!d38 || !f4) return fail("D_00810D38 / D_008106F4 not canonical");
    EmStreamLanesGlobals *v = &S.globals;
    v->d8106C8 = (int32_t)em_scene_req_u32(s, EM_SCENE_REQ_C8);
    v->d810D38 = (int32_t)((uint32_t)d38[0] | (uint32_t)d38[1] << 8 | (uint32_t)d38[2] << 16 |
                           (uint32_t)d38[3] << 24);
    v->d810700 = s->d810700;
    /* D_008104E4 is the player record's +0x234 (g.pd_infected, the latch
     * 0021C270 sets; em_player.c keeps the live record's byte in it). */
    v->d8104E4 = (uint8_t)g.pd_infected;
    v->d8106F4 = *f4;
    v->d8106F5 = s->req[EM_SCENE_REQ_F5];
    return 0;
}

static void sync_out(void)
{
    EmSceneState *s = em_scene_state();
    uint8_t *d38 = d810D38();
    uint8_t *f4 = em_scene_req_at(s, 0x008106F4u);
    const EmStreamLanesGlobals *v = &S.globals;
    for (unsigned i = 0; i < 4; ++i) d38[i] = (uint8_t)((uint32_t)v->d810D38 >> (8 * i));
    *f4 = v->d8106F4;
    s->req[EM_SCENE_REQ_F5] = v->d8106F5;
}

static int enter(void)
{
    if (S.fault) return -1;
    if (!S.booted) return fail("a stream entry before the boot (001F9820)");
    if (S.depth++ == 0 && sync_in() < 0) {
        S.depth--;
        return -1;
    }
    return 0;
}

static int leave(int rc, const char *what)
{
    if (--S.depth == 0 && !S.fault) sync_out();
    if (rc < 0 || S.lanes.fault.code || em_iop_stream_fault(S.ctx.iop)->code) return fail(what);
    return 0;
}

#define ENTRY(what, call) \
    do { if (enter() < 0) return -1; return leave((call), what); } while (0)

/* ------------------------------------------------------------ boot */

int em_stream_live_boot(const char *path)
{
    em_stream_live_shutdown();
    if (em_iop_stream_disc_load(&S.disc, path) != 0) {
        fprintf(stderr, "stream lanes: %s missing or malformed (run tools/export_streams.py); the "
                        "streams cannot start\n", path ? path : "(null)");
        return fail("assets/streams/streams.emst");
    }
    S.ctx.iop = em_iop_stream_create();
    if (!S.ctx.iop) return fail("em_iop_stream_create");
    em_iop_stream_attach_disc(S.ctx.iop, &S.disc);
    /* The PS2 disc-drive timing switch (em_settings, LAUNCHER_OPTIONS.md):
     * off by default, the disc answers at host speed. */
    em_iop_stream_set_ps2_drive_timing(S.ctx.iop, em_settings()->ps2_disc_drive_timing);
    EmIopVoiceTable table = {S.voice_table, &S.voice_scan};
    em_iop_stream_set_voice_table(S.ctx.iop, &table);
    /* sub_cdrom0_IRX_SNDN2DRV_IRX_1's tail: the five IOP heap blocks. */
    if (em_iop_stream_boot_buffers(S.ctx.iop, S.irx_buffers) != 0)
        return fail("sub_cdrom0_IRX_SNDN2DRV_IRX_1 (001FA6A0)");
    /* The measured IOP heap occupancy after the buffers (its owner is not
     * established; only its end 0xDE800 is pinned: IOP_STREAM.md). */
    if (em_iop_stream_boot_driver(S.ctx.iop) != 0)
        return fail("the IOP heap's measured start-up block (owner not established)");
    S.globals.d275B28 = S.irx_buffers[1];
    S.globals.d275B24 = S.irx_buffers[3];
    S.globals.d275B20 = S.irx_buffers[4];
    S.globals.d281880 = em_iop_stream_ee_status(S.ctx.iop) + 48;
    if (em_iop_stream_sub_O_STREAM_MUSIC_DAT_1(S.ctx.iop, &S.lanes.state.music_sector,
                                                &S.lanes.state.voice_sector) != 0)
        return fail("sub_O_STREAM_MUSIC_DAT_1");
    em_iop_stream_lanes_data(&S.disc, &S.data);
    EmStreamLanesWorkers w;
    memset(&w, 0, sizeof w);
    w.ctx = &S.ctx;
    w.w_00122BB8 = w_00122BB8;
    w.w_001FC280 = w_001FC280;
    w.w_001FBC50 = w_001FBC50;
    em_iop_stream_lane_workers(S.ctx.iop, &w);
    em_stream_lanes_bind(&S.lanes, &S.data, &S.globals, &w); /* keeps the two sectors above */
    if (em_stream_lanes_001F9820(&S.lanes) != 0) return fail("001F9820");
    /* One storage for D_0027CCC0: the voices 001F9820 took (0011A2B0 on the
     * fresh table) must be the SFX driver's reserved stream voices. */
    const EmStreamLanesState *st = &S.lanes.state;
    int32_t voices[4] = {st->lane[0].voice, st->voice_right, st->lane[1].voice, st->lane[2].voice};
    uint64_t mask = 0;
    for (int i = 0; i < 4; ++i) {
        if (voices[i] < 0 || voices[i] >= 48) return fail("001F9820: 0011A2B0 found no free voice");
        mask |= UINT64_C(1) << voices[i];
    }
    if (mask != em_sfx_stream_voices())
        return fail("001F9820's stream voices differ from the SFX driver's reserved voices");
    em_stream_live_001FBC50_cues();
    S.booted = 1;
    em_sfx_bind_output_mode(em_stream_live_output_mode); /* 001FBF50's D_0028215B */
    atomic_store_explicit(&s_mixer, S.ctx.iop, memory_order_release);
    return 0;
}

int32_t (*em_stream_live_d281F30(void))[4]
{
    return S.booted ? S.d281F30 : NULL;
}

/* 001FBC50's tail over D_00281F30: each record's delay 0 and cue -1. */
void em_stream_live_001FBC50_cues(void)
{
    for (int i = 0; i < EM_SLG_CUES; ++i) {
        S.d281F30[i][0] = 0;
        S.d281F30[i][1] = -1;
    }
}

void em_stream_live_shutdown(void)
{
    atomic_store_explicit(&s_mixer, NULL, memory_order_release);
    if (S.ctx.iop) em_iop_stream_destroy(S.ctx.iop);
    if (S.disc.blob) em_iop_stream_disc_free(&S.disc);
    memset(&S, 0, sizeof S);
}

int em_stream_live_failed(void)
{
    return S.fault != NULL;
}

/* ------------------------------------------------------------ time */

static EmSoundSample s_sound_sample;

/* The main-loop-top sample (em_stream_live.h EmSoundSample). */
static void sound_sample(void)
{
    uint64_t ticks = 0;
    const EmSfxDriver *d = em_sfx_driver_state(&ticks);
    EmSoundSample *o = &s_sound_sample;
    memset(o, 0, sizeof *o);
    if (!d) return;
    const EmStreamLanesState *st = &S.lanes.state;
    o->valid = 1;
    o->ticks = ticks;
    o->d810E90 = S.globals.d810E90;
    for (int i = 0; i < 3; ++i) {
        o->active[i] = st->active[i];
        o->cue[i] = st->cue[i];
    }
    o->read_phase = st->read_phase;
    o->read_lane = st->read_lane;
    o->mono = st->mono;
    o->music_clip = st->music_clip;
    memcpy(o->ring, st->ring, sizeof o->ring);
    o->ring_head = st->ring_head;
    o->ring_tail = st->ring_tail;
    em_sfx_tables(o->b70, o->c30);
    memcpy(o->f30, S.d281F30, sizeof o->f30);
    o->cursor = d->cursor;
    o->serial = d->serial;
    for (int v = 0; v < 48; ++v) {
        const EmSfxVoice *x = &d->voices[v];
        const uint16_t f[EM_SOUND_VOICE_FIELDS] = {
            x->state, x->note, x->owner, x->release, x->serial, x->sustain, x->kind, x->age,
            x->priority, x->alloc, x->porta, x->base, x->depth, x->remaining, x->length, x->prog,
            x->bank};
        memcpy(o->voice[v], f, sizeof f);
        /* D_002817C0[v] as the tick's reaper read it (em_sfx's reply of
         * the previous exchange; the stream voices' words are the stream
         * backend's, not compared). */
        o->envx[v] = d->feedback ? d->feedback[v] : 0;
    }
    for (int k = 0; k < 48; ++k)
        o->track_id[k] = d->tracks[k].allocated && d->tracks[k].entry
                             ? (int32_t)d->tracks[k].entry->id : -1;
}

int em_stream_live_field(void)
{
    if (S.fault) return -1;
    if (!S.booted) return 0; /* before 001AAE40's start-up nothing counts */
    /* The vblank handler's D_00810E90 += 1; its wake of the sound thread
     * 001FB0C0, whose 001152B0 runs the SFX sequencer's tick (em_sfx);
     * then the field's IOP work (the tick's RPC 0x64 exchange). */
    S.globals.d810E90 += 1u;
    if (em_sfx_field() != 0) return fail("the SFX sequencer's tick (001152D8)");
    if (em_iop_stream_field(S.ctx.iop) != 0) return fail("the IOP field (RPC 0x64, driver ticks)");
    sound_sample();
    return 0;
}

const EmSoundSample *em_stream_live_sound_sample(void)
{
    return &s_sound_sample;
}

/* 001FB100's workers. */
static int h_001F9CF0(void *ctx, int32_t mode)
{
    (void)ctx;
    (void)mode; /* 001F9CF0 does not read it (em_stream_lanes_original) */
    return em_stream_lanes_001F9CF0(&S.lanes);
}

static int h_00119870(void *ctx, int32_t a0)
{
    (void)ctx;
    em_sfx_output_mode_00119870((int16_t)a0); /* 00119870: D_0027F778 = a0 (sh); em_sfx_bank's */
    return 0;
}

static int h_0011A608(void *ctx, uint64_t mask, int32_t a1, int32_t a2)
{
    (void)ctx;
    return em_stream_lanes_0011A608(&S.lanes, mask, a1, a2);
}

static int h_001FB9F0(void *ctx, int32_t cue, int32_t a1, int32_t a2, int32_t a3)
{
    (void)ctx;
    if (a1 != 0x1000) return -1; /* 001FC6E0 always passes 0x1000 */
    em_sfx_submit_001FB9F0((unsigned)cue, a2, a3);
    return 0;
}

/* Step H, 001FB100 (byte-matched; em_slg_001FB100) over its views: the
 * lanes' D_0028215B (mono), D_00281FD4 (lane 0's voice) and D_002820F4,
 * em_sfx's D_00281B70 / D_00281C30, the settings' D_0081011C and this
 * module's D_00281F30. em_frame calls it only while D_00821058 != 1 (the port's
 * movie flag is 1 or 0), so the translation's own gate sees 0. */
static int step_h_001FB100(void)
{
    EmSlgSoundFrame f;
    memset(&f, 0, sizeof f);
    f.d821058 = 0;
    f.d28215B = S.lanes.state.mono;
    /* D_0081011C, the requested output mode: the settings' sound byte
     * (EmSceneState.d810118[4]; 001AB430 stores 0, the options screen's
     * sound row toggles it). 001FB100 only reads it. */
    f.d81011C = em_scene_state()->d810118[4];
    f.d281FD4 = S.lanes.state.lane[0].voice;
    f.d2820F4 = S.lanes.state.voice_right;
    int32_t requested[48], snapshot[48];
    em_sfx_tables(requested, snapshot);
    memcpy(f.d281B70, requested, 0xC0);
    memcpy(f.d281B70 + 0xC0, snapshot, 0xC0);
    memcpy(f.d281F30, S.d281F30, sizeof f.d281F30);
    const EmSlgSoundWorkers w = {NULL, h_001F9CF0, h_00119870, h_0011A608, h_001FB9F0};
    int rc = em_slg_001FB100(&w, &f);
    S.lanes.state.mono = f.d28215B;
    memcpy(snapshot, f.d281B70 + 0xC0, 0xC0);
    em_sfx_set_snapshot(snapshot);
    memcpy(S.d281F30, f.d281F30, sizeof S.d281F30);
    return rc;
}

int em_stream_live_step_h(void)
{
    if (S.fault) return -1;
    if (!S.booted) return fail("step H before the boot (001F9820)");
    if (enter() < 0) return -1;
    int rc = leave(step_h_001FB100(), "001FB100");
    /* The shared device (em_bgm) is opened for the first playing lane when
     * the startup audio has not opened it (fixtures without the frontend). */
    if (rc == 0 && !S.device &&
        (S.lanes.state.active[0] || S.lanes.state.active[1] || S.lanes.state.active[2])) {
        S.device = 1;
        if (em_bgm_device_rate() == 0 && em_bgm_device_ensure(48000) != 0)
            fprintf(stderr, "stream lanes: no audio device; the streams run silent\n");
    }
    return rc;
}

/* ------------------------------------------------------------ entries */

int em_stream_live_001FA790(int32_t lane, int32_t cue)
{ ENTRY("001FA790", em_stream_lanes_001FA790(&S.lanes, lane, cue)); }
int em_stream_live_001FAAC0(int32_t lane)
{ ENTRY("001FAAC0", em_stream_lanes_001FAAC0(&S.lanes, lane)); }
int em_stream_live_001FAB50(void)
{ ENTRY("001FAB50", em_stream_lanes_001FAB50(&S.lanes)); }
int em_stream_live_001FABB0(void)
{ ENTRY("001FABB0", em_stream_lanes_001FABB0(&S.lanes)); }
int em_stream_live_001FAD70(int32_t lane, int32_t fade, int32_t release)
{ ENTRY("001FAD70", em_stream_lanes_001FAD70(&S.lanes, lane, fade, release)); }
int em_stream_live_001FAE70(int32_t a0)
{ ENTRY("001FAE70", em_stream_lanes_001FAE70(&S.lanes, a0)); }
int em_stream_live_001FD470(int32_t mask)
{ ENTRY("001FD470", em_stream_lanes_001FD470(&S.lanes, mask)); }
int em_stream_live_00119828(int32_t a0, int32_t a1, int32_t a2)
{ ENTRY("00119828", em_stream_lanes_00119828(&S.lanes, a0, a1, a2)); }

/* 001FA5A0 writes only the ring slot and the push index (STREAM_LANES.md
 * "Binding notes"): the message service's translation over a view copied
 * from and back to the lanes' storage. */
static int ring_push(int32_t cue)
{
    EmMessageVoiceRing ring;
    memcpy(ring.slots, S.lanes.state.ring, sizeof ring.slots);
    ring.head = S.lanes.state.ring_head;
    if (em_message_voice_ring_push(&ring, cue) != 1) return -1;
    memcpy(S.lanes.state.ring, ring.slots, sizeof ring.slots);
    S.lanes.state.ring_head = ring.head;
    return 0;
}

int em_stream_live_001FA5A0(int32_t cue)
{ ENTRY("001FA5A0", ring_push(cue)); }

/* D_0027CCC0[voice] for 001195A8: the SFX driver owns the records
 * (em_sfx's published +0x00 / +0x22). */
static int bank_voice(void *ctx, int32_t voice, uint16_t *state, uint16_t *handle)
{
    (void)ctx;
    return em_sfx_voice_record(voice, state, handle);
}

/* D_00281D50, the bank handles 001FB9F0 reads (one storage: the sound
 * bank's load state). */
static const int32_t *bank_handles(void)
{
    return S.bank_bound ? S.bank.load.d281D50 : NULL;
}

int em_stream_live_bind_sound_bank(const int32_t base[5])
{
    if (S.fault) return -1;
    if (!S.booted) return fail("the sound bank bound before the boot");
    if (em_sound_bank_init(&S.bank, S.ctx.iop, base) != 0) return fail("em_sound_bank_init");
    em_sound_bank_set_voice(&S.bank, bank_voice, NULL);
    S.bank_bound = 1;
    em_sfx_bind_bank_handles(bank_handles); /* 001FB9F0's D_00281D50 */
    return 0;
}

int em_stream_live_001FB370(uint32_t address, const uint8_t *bytes, uint32_t size, uint32_t *result)
{
    if (S.fault) return -1;
    if (!S.bank_bound) return fail("001FB370 before the sound bank was bound");
    if (em_sound_bank_001FB370(&S.bank, address, bytes, size, result) != 0) {
        uint32_t at = 0;
        int32_t code = 0;
        em_sound_bank_failed(&S.bank, &at, &code);
        fprintf(stderr, "stream lanes: 001FB370 faulted at %08X (code %d)\n", (unsigned)at, (int)code);
        return fail("001FB370 (the sound-bank upload)");
    }
    if (em_iop_stream_fault(S.ctx.iop)->code) return fail("001FB370's IOP work");
    return 0;
}

const EmSoundBank *em_stream_live_sound_bank(void)
{
    return S.bank_bound ? &S.bank : NULL;
}

const EmIopStream *em_stream_live_iop(void)
{
    return S.ctx.iop;
}

int8_t em_stream_live_active(int lane)
{
    return S.booted && lane >= 0 && lane < EM_STREAM_LANES ? S.lanes.state.active[lane] : 0;
}

uint8_t em_stream_live_read_phase(void)
{
    return S.booted ? (uint8_t)S.lanes.state.read_phase : 0;
}

const uint8_t *em_stream_live_output_mode(void)
{ return S.booted && !S.fault ? &S.lanes.state.mono : NULL; }

int em_stream_live_log(uint32_t out[9])
{
    if (!S.booted) return 0;
    out[0] = S.globals.d810E90;
    out[1] = (uint8_t)S.lanes.state.read_phase;
    out[2] = (uint8_t)S.lanes.state.read_lane;
    for (int i = 0; i < EM_STREAM_LANES; ++i) {
        out[3 + i] = (uint8_t)S.lanes.state.active[i];
        out[6 + i] = (uint8_t)S.lanes.state.lane[i].load;
    }
    return 1;
}

int em_stream_live_drive_stats(EmIopDriveStats *out, int *ps2_timing)
{
    if (!S.booted || !S.ctx.iop) return 0;
    em_iop_stream_drive_stats(S.ctx.iop, out);
    *ps2_timing = em_iop_stream_ps2_drive_timing(S.ctx.iop);
    return 1;
}

void em_stream_live_drive_report(FILE *out)
{
    EmIopDriveStats d;
    int ps2 = 0;
    if (!em_stream_live_drive_stats(&d, &ps2))
        return;
    fprintf(out, "stream drive: %s: %u reads (%u at host speed); the model's classes: %u contiguous, %u fast "
                 "seeks, %u full seeks; %u with no position; %u outside the measured distances (last %lld); "
                 "%u breaks; %u abandoned\n",
            ps2 ? "PS2 disc-drive timing on" : "host speed", d.reads, d.host_speed, d.by_fields[0],
            d.by_fields[2], d.by_fields[6], d.no_position, d.unmeasured, (long long)d.last_unmeasured, d.breaks,
            d.abandoned);
}

int32_t em_stream_live_cue(int lane)
{
    return S.booted && lane >= 0 && lane < EM_STREAM_LANES ? S.lanes.state.cue[lane] : 0;
}

void em_stream_live_mix(float *out, int frames, int device_rate)
{
    EmIopStream *iop = atomic_load_explicit(&s_mixer, memory_order_acquire);
    if (iop) em_iop_stream_mix(iop, out, frames, device_rate);
}
