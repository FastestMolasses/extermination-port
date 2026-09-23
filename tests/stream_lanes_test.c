/* Unit test for em_stream_lanes_original (001F9CF0 and the stream-lane
 * functions). Behaviour equality with the original instructions is
 * established by tools/test_stream_lanes_reference.py; this test pins the
 * native contract: fail-stop faults, the worker protocol and the order of
 * worker calls on a few hand-built states. Clip rows here are synthetic. */
#include <stdio.h>
#include <string.h>

#include "game/em_stream_lanes_original.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); failures++; } } while (0)

typedef struct {
    int fail;            /* worker id that returns -1 (0 none) */
    int32_t rng, r13280, r12610, r12D18;
    int n;
    int32_t ev[64][5];
} Log;

static int rec(Log *l, int id, int32_t a, int32_t b, int32_t c, int32_t d)
{
    if (l->n < 64) {
        l->ev[l->n][0] = id; l->ev[l->n][1] = a; l->ev[l->n][2] = b;
        l->ev[l->n][3] = c; l->ev[l->n][4] = d; l->n++;
    }
    return l->fail == id ? -1 : 0;
}
static int w_iop(void *c, int32_t cmd, int32_t a, int32_t b, int32_t d) { return rec(c, 1, cmd, a, b, d); }
static int w_rng(void *c, int32_t *v) { Log *l = c; *v = l->rng; return rec(l, 2, 0, 0, 0, 0); }
static int w_fc280(void *c) { return rec(c, 3, 0, 0, 0, 0); }
static int w_fbc50(void *c) { return rec(c, 4, 0, 0, 0, 0); }
static int w_13280(void *c, int32_t a, int32_t *r) { Log *l = c; *r = l->r13280; return rec(l, 5, a, 0, 0, 0); }
static int w_12610(void *c, uint32_t s, uint32_t n, uint32_t a, const uint8_t m[3], int32_t *r)
{
    Log *l = c; *r = l->r12610;
    return rec(l, 6, (int32_t)s, (int32_t)n, (int32_t)a, m[0] | m[1] << 8 | m[2] << 16);
}
static int w_12D18(void *c, int32_t a, int32_t *r) { Log *l = c; *r = l->r12D18; return rec(l, 7, a, 0, 0, 0); }
static int w_13478(void *c, int32_t a) { return rec(c, 8, a, 0, 0, 0); }

static EmStreamClip music[4], voice[160];
static EmStreamLanesData data;
static EmStreamLanesGlobals globals;
static int32_t status[EM_STREAM_VOICE_STATUS];

static void fresh(EmStreamLanes *L, Log *l)
{
    EmStreamLanesWorkers w = { l, w_iop, w_rng, w_fc280, w_fbc50, w_13280, w_12610, w_12D18, w_13478 };
    int i;
    memset(L, 0, sizeof *L);
    memset(l, 0, sizeof *l);
    memset(music, 0, sizeof music);
    memset(voice, 0, sizeof voice);
    music[3].sector = 100; music[3].size = 0x200000; music[3].loop = 1;
    voice[150].sector = 7; voice[150].size = 0x30000;
    voice[149].sector = 9; voice[149].size = 0x18000;
    data.music = music; data.music_count = 4;
    data.voice = voice; data.voice_count = 160;
    memset(&globals, 0, sizeof globals);
    memset(status, 0, sizeof status);
    globals.d281880 = status;
    globals.d810E90 = 5000;
    em_stream_lanes_bind(L, &data, &globals, &w);
    for (i = 0; i < EM_STREAM_LANES; i++) {
        L->state.lane[i].voice = 1 + i;
        L->state.lane[i].voice_mask = (uint64_t)1 << (1 + i);
        L->state.lane[i].buffer = 0x10000u * (uint32_t)(i + 1);
        L->state.lane[i].buffer_size = 0x10000;
    }
    L->state.voice_right = 4;
    memset(L->state.ring, 0xFF, sizeof L->state.ring);
}

static void test_faults(void)
{
    EmStreamLanes L;
    Log l;

    fresh(&L, &l);
    L.workers.w_001157F0 = NULL;
    CHECK(em_stream_lanes_00119828(&L, 0, 1, 2) == -1);
    CHECK(L.fault.address == 0x001157F0u && L.fault.code == EM_STREAM_FAULT_NULL_WORKER);
    /* Latched: every entry point refuses without effect. */
    L.workers.w_001157F0 = w_iop;
    CHECK(em_stream_lanes_00119828(&L, 0, 1, 2) == -1 && l.n == 0);
    CHECK(em_stream_lanes_001FA570(&L) == -1 && L.state.ring_head == 0);

    fresh(&L, &l);
    l.fail = 1;
    CHECK(em_stream_lanes_00119828(&L, 0, 0x1999, 0x1999) == -1);
    CHECK(L.fault.address == 0x001157F0u && L.fault.code == EM_STREAM_FAULT_WORKER_FAILED);

    fresh(&L, &l);
    CHECK(em_stream_lanes_001FAAC0(&L, 3) == -1 && L.fault.code == EM_STREAM_FAULT_BAD_INDEX &&
          L.fault.address == 0x001FAAC0u);
    fresh(&L, &l);
    CHECK(em_stream_lanes_001FAD70(&L, -1, 4, 1) == -1 && L.fault.code == EM_STREAM_FAULT_BAD_INDEX);

    /* Cue rows outside the supplied tables. */
    fresh(&L, &l);
    CHECK(em_stream_lanes_001FA790(&L, 0, 4) == -1 && L.fault.address == 0x0025DD30u &&
          L.fault.code == EM_STREAM_FAULT_BAD_INDEX);
    fresh(&L, &l);
    CHECK(em_stream_lanes_001FA790(&L, 2, 160) == -1 && L.fault.address == 0x0025E170u);
    /* Cue 0 or an active lane: the original returns before any row read. */
    fresh(&L, &l);
    CHECK(em_stream_lanes_001FA790(&L, 0, 0) == 0 && L.state.active[0] == 0);
    L.state.active[1] = 1;
    CHECK(em_stream_lanes_001FA790(&L, 1, 9999) == 0 && L.state.cue[1] == 0);

    /* Ring pop index outside 0..15. */
    fresh(&L, &l);
    L.state.ring_tail = 16;
    CHECK(em_stream_lanes_001FA5F0(&L) == -1 && L.fault.address == 0x00275B34u);

    /* Views: NULL globals / status table / data when reached. */
    fresh(&L, &l);
    L.globals = NULL;
    CHECK(em_stream_lanes_001FAB50(&L) == -1 && L.fault.code == EM_STREAM_FAULT_NULL_WORKER);
    fresh(&L, &l);
    globals.d281880 = NULL;
    CHECK(em_stream_lanes_001F9CF0(&L) == -1 && L.fault.address == 0x00281880u);
    fresh(&L, &l);
    L.state.lane[0].voice = 0x30; L.state.lane[1].voice = -1; L.state.lane[2].voice = 0x7FFF;
    globals.d281880 = NULL;   /* voices >= 0x30 never read the table (0011A730 returns 0) */
    CHECK(em_stream_lanes_001F9CF0(&L) == 0);
    fresh(&L, &l);
    L.data = NULL;
    CHECK(em_stream_lanes_001FA790(&L, 1, 150) == -1 && L.fault.code == EM_STREAM_FAULT_NULL_WORKER);

    /* A missing disc worker faults only when 001FA0D0 reaches it. */
    fresh(&L, &l);
    L.workers.w_00113280 = NULL;
    CHECK(em_stream_lanes_001FA0D0(&L) == 0 && L.state.read_phase == 0);
    L.state.lane[1].load = 1;
    CHECK(em_stream_lanes_001FA0D0(&L) == 0 && L.state.read_phase == 1 && L.state.read_lane == 1);
    CHECK(em_stream_lanes_001FA0D0(&L) == -1 && L.fault.address == 0x00113280u);
}

/* Lane 0 reads D_0025DD30 + 16 * cue unbounded: with the full 68-row music
 * table, cue 68 + k is voice row k (the voice table follows it). */
static void test_lane0_contiguous_rows(void)
{
    static EmStreamClip music68[68];
    EmStreamLanes L;
    Log l;
    fresh(&L, &l);
    memset(music68, 0, sizeof music68);
    data.music = music68; data.music_count = 68;
    CHECK(em_stream_lanes_001FA790(&L, 0, 68 + 150) == 0);
    CHECK(L.fault.code == EM_STREAM_FAULT_NONE && L.state.active[0] == 1 && L.state.cue[0] == 218);
    CHECK(L.state.music_clip == 218 && L.state.lane[0].base_sector == 7);   /* voice[150] */
    /* Past both tables: fault at the music table. */
    fresh(&L, &l);
    data.music = music68; data.music_count = 68;
    CHECK(em_stream_lanes_001FA790(&L, 0, 68 + 160) == -1 && L.fault.address == 0x0025DD30u &&
          L.fault.code == EM_STREAM_FAULT_BAD_INDEX);
}

static void test_music_select(void)
{
    EmStreamLanes L;
    Log l;
    fresh(&L, &l);
    /* bits 8..15 of the per-area word select cue 3; no override. */
    globals.d8106C8 = 0x00000300;
    globals.d810700 = 0x0B;
    globals.d8106F4 = 1;
    l.rng = 56 << 16;                                   /* fade 270 + 56 */
    CHECK(em_stream_lanes_001FAE70(&L, 1) == 0);
    CHECK(L.state.active[0] == 1 && L.state.cue[0] == 3 && L.state.music_clip == 3);
    CHECK(L.state.lane[0].fade_step == 0x424904B6u);   /* 16383 / 326 */
    CHECK(L.state.lane[0].volume == 0 && L.state.lane[0].release == 0);
    CHECK(L.state.lane[0].base_sector == 100 && L.state.lane[0].read_count == 16);
    CHECK(globals.d8106F4 == 0);
    /* 001FC280, LCG, then the 0011A608(mask, 0, 0) of 001FABF0. */
    CHECK(l.n == 3 && l.ev[0][0] == 3 && l.ev[1][0] == 2);
    CHECK(l.ev[2][0] == 1 && l.ev[2][1] == EM_STREAM_CMD_0011A608 && l.ev[2][2] == 2 && l.ev[2][4] == 0);

    /* Resume (a0 = 0) with the same cue already on an active lane: nothing. */
    l.n = 0;
    CHECK(em_stream_lanes_001FAE70(&L, 0) == 0 && l.n == 2);

    /* 001FB0B0: the override cue wins over the area bits. */
    fresh(&L, &l);
    CHECK(em_stream_lanes_001FB0B0(&L, 2) == 0 && globals.d810D38 == 2);
    CHECK(L.fault.code == EM_STREAM_FAULT_NONE && L.state.cue[0] == 2 && L.state.active[0] == 1);
}

static void test_voice_ring_and_fades(void)
{
    EmStreamLanes L;
    Log l;
    int i;
    fresh(&L, &l);
    L.state.ring[0] = 150;
    L.state.ring[1] = 149;
    L.state.ring_head = 2;
    CHECK(em_stream_lanes_001FA5F0(&L) == 0 && L.state.active[1] == 1 && L.state.cue[1] == 150);
    CHECK(em_stream_lanes_001FA5F0(&L) == 0 && L.state.active[2] == 1 && L.state.cue[2] == 149);
    CHECK(L.state.ring_tail == 2 && L.state.ring[0] == -1 && L.state.ring[1] == -1);
    /* 001FA790 ends in 001FABF0(lane, cue, 0, 0): full-speed step 16383.0. */
    CHECK(L.state.lane[1].fade_step == 0x467FFC00u && L.state.lane[1].loop == 0);

    /* 001FAD70(lane, 30, 1) on the active lanes: step -(16383/30), release. */
    for (i = 0; i < EM_STREAM_LANES; i++)
        CHECK(em_stream_lanes_001FAD70(&L, i, 0x1E, 1) == 0);
    CHECK(L.state.lane[0].fade_step == 0 && L.state.lane[0].release == 0); /* lane 0 idle */
    CHECK(L.state.lane[1].fade_step == 0xC4088666u && L.state.lane[1].release == 1);
    L.state.lane[1].volume = 0x44000000u;  /* 512.0: one step reaches 0 */
    L.state.lane[2].fade_step = 0;
    l.n = 0;
    CHECK(em_stream_lanes_001FA330(&L) == 0);
    CHECK(L.state.active[1] == 0 && L.state.cue[1] == 0 && L.state.lane[1].volume == 0);
    /* release (0x43) then the volume words (0x40) for the released lane. */
    CHECK(l.n == 2 && l.ev[0][1] == EM_STREAM_CMD_0011A6E8 && l.ev[1][1] == EM_STREAM_CMD_0011A608);

    /* 001FD470(3): SFX stop worker, then 001FABB0. */
    l.n = 0;
    L.state.read_phase = 2;
    CHECK(em_stream_lanes_001FD470(&L, 3) == 0);
    CHECK(l.ev[0][0] == 4 && L.state.active[2] == 0 && L.state.read_phase == 0 &&
          L.state.ring_tail == 0 && L.state.ring_head == 0 && L.state.ring[5] == -1);
}

int main(void)
{
    test_faults();
    test_lane0_contiguous_rows();
    test_music_select();
    test_voice_ring_and_fades();
    if (failures) {
        fprintf(stderr, "stream_lanes_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("stream_lanes_test: PASS\n");
    return 0;
}
