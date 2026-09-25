/* The live music and voice streams (WP-8b). See em_stream_live.h,
 * docs/STREAM_LANES.md and docs/IOP_STREAM.md ("Binding"). */
#include "game/em_stream_live.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "game/em_bgm.h"
#include "game/em_game_internal.h"
#include "game/em_iop_stream.h"
#include "game/em_message_service.h"
#include "game/em_random.h"
#include "game/em_scene_bindings.h"
#include "game/em_sfx.h"
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
    EmIopVoiceTable table = {S.voice_table, &S.voice_scan};
    em_iop_stream_set_voice_table(S.ctx.iop, &table);
    /* sub_cdrom0_IRX_SNDN2DRV_IRX_1's tail: the five IOP heap blocks. */
    if (em_iop_stream_boot_buffers(S.ctx.iop, S.irx_buffers) != 0)
        return fail("sub_cdrom0_IRX_SNDN2DRV_IRX_1 (001FA6A0)");
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
    S.booted = 1;
    atomic_store_explicit(&s_mixer, S.ctx.iop, memory_order_release);
    return 0;
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

int em_stream_live_field(void)
{
    if (S.fault) return -1;
    if (!S.booted) return 0; /* before 001AAE40's start-up nothing counts */
    /* The vblank handler's D_00810E90 += 1, then the field's IOP work. */
    S.globals.d810E90 += 1u;
    if (em_iop_stream_field(S.ctx.iop) != 0) return fail("the IOP field (RPC 0x64, driver ticks)");
    return 0;
}

int em_stream_live_step_h(void)
{
    if (S.fault) return -1;
    if (!S.booted) return fail("step H before the boot (001F9820)");
    if (enter() < 0) return -1;
    int rc = leave(em_stream_lanes_001F9CF0(&S.lanes), "001F9CF0");
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

int8_t em_stream_live_active(int lane)
{
    return S.booted && lane >= 0 && lane < EM_STREAM_LANES ? S.lanes.state.active[lane] : 0;
}

uint8_t em_stream_live_read_phase(void)
{
    return S.booted ? (uint8_t)S.lanes.state.read_phase : 0;
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
