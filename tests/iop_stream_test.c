/* Native contract test for the IOP stream backend (src/game/em_iop_stream.c,
 * docs/IOP_STREAM.md): heap blocks, 0011A2B0, the disc directory, the
 * driver's command decode, a synthetic stereo stream driven through the
 * original lanes (em_stream_lanes_original) field by field with the played
 * samples checked for continuity across buffer wraps, fail-stop faults and
 * the mixer ring. No assets needed; the original-instruction oracle and the
 * capture comparisons are tools/test_iop_stream_reference.py. */
#include "game/em_iop_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            failures++;                                                       \
        }                                                                     \
    } while (0)

/* ---- a synthetic disc: music LSN 1000 holding one stereo cue ------------ */
#define MUSIC_LSN 1000u
#define VOICE_LSN 5000u
#define CUE 5
#define CUE_SECTORS 40u   /* 20 L/R pairs of 0x400-byte blocks per 8 sectors */

static uint8_t disc_data[(CUE_SECTORS + 16) * 2048];
static EmIopStreamExtent extents[2];

/* Block n of a channel: shift 0, filter 0, every nibble equal to a value
 * derived from (channel, n), so every decoded sample is (nib << 12) and a
 * skipped or repeated block shows. */
static int8_t block_nibble(int channel, uint32_t n) { return (int8_t)(((n * 3u + (uint32_t)channel * 5u) % 15u) - 7); }

static void build_disc(EmIopStreamDisc *d)
{
    uint32_t byte, i;
    memset(d, 0, sizeof *d);
    for (byte = 0; byte < CUE_SECTORS * 2048u; byte += 16) {
        uint32_t pair = byte / 0x800, in = byte % 0x800, channel = in >= 0x400;
        uint32_t n = pair * 64u + (in % 0x400) / 16u;  /* block index in the channel */
        uint8_t nib = (uint8_t)(block_nibble((int)channel, n) & 0xF);
        disc_data[byte] = 0x00;
        disc_data[byte + 1] = 0x00;
        for (i = 2; i < 16; i++)
            disc_data[byte + i] = (uint8_t)(nib | nib << 4);
    }
    d->blob = disc_data;
    d->size = sizeof disc_data;
    d->lsn[0] = MUSIC_LSN;
    d->lsn[1] = VOICE_LSN;
    d->sectors[0] = CUE_SECTORS + 8;
    d->sectors[1] = 8;
    strcpy(d->search[0], "\\STREAM\\MUSIC.DAT;1");
    strcpy(d->search[1], "\\STREAM\\VOICE.DAT;1");
    strcpy(d->path[0], "\\STREAM\\MUSIC.DAT;1");
    strcpy(d->path[1], "\\STREAM\\VOICE.DAT;1");
    d->music_rows = 68;
    d->voice_rows = 179;
    d->rows[CUE].sector = 0;
    d->rows[CUE].unread = 0;
    d->rows[CUE].size = (int32_t)(CUE_SECTORS * 2048u);
    d->rows[CUE].loop = 1;
    extents[0].lsn = MUSIC_LSN;
    extents[0].sectors = CUE_SECTORS;
    extents[0].offset = 0;
    extents[0].file = 0;
    d->extent = extents;
    d->extents = 1;
}

/* ---- the lanes' worker context (first member: the stream) --------------- */
typedef struct {
    EmIopStream *iop;
    uint32_t lcg;
} Ctx;

static int w_rng(void *c, int32_t *v) { Ctx *x = c; x->lcg = x->lcg * 1103515245u + 12345u; *v = (int32_t)(x->lcg >> 1); return 0; }
static int w_none(void *c) { (void)c; return 0; }

static uint8_t voice_table[0x30 * 0x6A];
static uint64_t sdk_mask;

typedef struct {
    int16_t samples[2][400000];
    uint32_t count[2];
} Tap;

static void tap(void *ctx, int voice, int16_t sample)
{
    Tap *t = ctx;
    if (voice < 2 && t->count[voice] < 400000)
        t->samples[voice][t->count[voice]++] = sample;
}

static void test_heap_and_table(void)
{
    EmIopStream *s = em_iop_stream_create();
    EmIopVoiceTable table = {voice_table, &sdk_mask};
    uint32_t b[5];
    int32_t v = 0;
    int i;
    CHECK(s && em_iop_stream_boot_buffers(s, b) == 0);
    CHECK(b[0] == 0x85B00 && b[1] == 0xADC00 && b[2] == 0xADC00 && b[3] == 0xBDD00 && b[4] == 0xCDE00);
    memset(voice_table, 0, sizeof voice_table);
    sdk_mask = 0;
    em_iop_stream_set_voice_table(s, &table);
    for (i = 0; i < 4; i++) {
        CHECK(em_iop_stream_0011A2B0(s, 0, &v) == 0 && v == i);
        CHECK(voice_table[i * 0x6A] == 1 && voice_table[i * 0x6A + 0x1A] == 3);
    }
    CHECK(em_iop_stream_0011A2B0(s, 2, &v) == 0 && v == 0x18);
    CHECK(em_iop_stream_0011A2B0(s, 7, &v) == 0 && v == -1);
    CHECK(em_iop_stream_0011A2B0(s, -1, &v) == 0 && v == -1);
    /* every record busy (+0x00 != 0), voices 0..23 of kind 0 with +0x0A
     * ages 50 - i: the scan takes the lowest over a0 = 1 (voices 0..23). */
    for (i = 0; i < 0x30; i++) {
        uint8_t *e = voice_table + i * 0x6A;
        e[0] = 1;
        e[0x1A] = i < 24 ? 0 : 3;
        e[0x0A] = (uint8_t)(50 - i);
        e[0x0B] = 0;
    }
    voice_table[23 * 0x6A + 0x0A] = 60;  /* 22 becomes the lowest */
    sdk_mask = 0;
    CHECK(em_iop_stream_0011A2B0(s, 1, &v) == 0 && v == 22);
    CHECK(sdk_mask == (uint64_t)1 << 24);   /* 1 << (end - start), not the voice's bit */
    CHECK(voice_table[22 * 0x6A + 0x4E] == 0x78 && voice_table[22 * 0x6A + 0x06] == 0xFF &&
          voice_table[22 * 0x6A + 0x0A] == 0 && voice_table[22 * 0x6A + 0x1A] == 3);
    {   /* the queued command carries the scan's end index, 0x18 */
        const EmIopDriver *d = em_iop_stream_driver(s);
        uint32_t seen[4] = {0};
        (void)d;
        em_iop_stream_set_forward(s, NULL, NULL);
        /* deliver to the ring and read it back without running the tick */
        CHECK(em_iop_stream_001157F0(s, 0x3D, 0, 0, 0) == 2);
        CHECK(em_iop_stream_field(s) == -1);   /* command 3 has no forward sink */
        CHECK(em_iop_stream_fault(s)->code == EM_IOP_FAULT_UNSUPPORTED);
        memcpy(seen, em_iop_stream_driver(s)->ring[0], sizeof seen);
        CHECK(seen[0] == 3 && seen[1] == 0x18 && seen[2] == 0 && seen[3] == 0);
    }
    em_iop_stream_destroy(s);
}

static void test_directory(const EmIopStreamDisc *disc)
{
    EmIopStream *s = em_iop_stream_create();
    EmIopStreamDisc other = *disc;
    uint32_t a = 0, b = 0;
    em_iop_stream_attach_disc(s, disc);
    CHECK(em_iop_stream_sub_O_STREAM_MUSIC_DAT_1(s, &a, &b) == 0 && a == MUSIC_LSN && b == VOICE_LSN);
    strcpy(other.path[1], "\\STREAM\\OTHER.DAT;1");
    em_iop_stream_attach_disc(s, &other);
    CHECK(em_iop_stream_sub_O_STREAM_MUSIC_DAT_1(s, &a, &b) == -1);
    CHECK(em_iop_stream_fault(s)->code == EM_IOP_FAULT_NOT_EXPORTED);
    em_iop_stream_destroy(s);
}

static void test_commands(void)
{
    EmIopStream *s = em_iop_stream_create();
    EmIopDriver *d = em_iop_stream_driver(s);
    /* 0011A4E8's packing of lane 0's first voice (001F9820): voice 0, flags
     * 0x20002, IOP 0xAE000, size 0x10000, SPU 0x5010, 0x4000. */
    const uint32_t c3e[4] = {0x3E, (0u << 24) | 0x20000 | 0x4000 | 0x00, (0x5010u << 16) | (0x10000 >> 8),
                             (0x10000u << 24) | 0xAE000};
    const uint32_t c41[4] = {0x41, 1, 0, 0xBB80};
    const uint32_t c40[4] = {0x40, 1, 0, 0x3FFF0000};
    const uint32_t c16[4] = {0x16, 1, 0x1234, 0x5678};
    const uint32_t bad[4] = {0x3E, 48u << 24, 0, 0};
    CHECK(em_iop_stream_drv_command(s, c3e) == 0);
    CHECK(d->voice[0].stride == 0x20000 && d->voice[0].spu_addr == 0x5010 && d->voice[0].spu_size == 0x4000 &&
          d->voice[0].iop_addr == 0xAE000 && d->voice[0].iop_size == 0x10000);
    CHECK(em_iop_stream_spu_voice(s, 0)->ssa == 0x5010 && em_iop_stream_spu_voice(s, 0)->adsr1 == 0x8080 &&
          em_iop_stream_spu_voice(s, 0)->adsr2 == 0x808A);
    CHECK(em_iop_stream_drv_command(s, c41) == 0 && em_iop_stream_spu_voice(s, 0)->pitch == 0x1000 &&
          d->voice[0].rate == 0xBB80);
    CHECK(em_iop_stream_drv_command(s, c40) == 0 && d->voice[0].vol_left == 0x3FFF && d->voice[0].vol_right == 0 &&
          em_iop_stream_spu_voice(s, 0)->vol[0] == 0x3FFF);
    CHECK(em_iop_stream_drv_command(s, c16) == 0 && em_iop_stream_effect_volume(s, 1, 0) == 0x1234 &&
          em_iop_stream_effect_volume(s, 1, 1) == 0x5678);
    CHECK(em_iop_stream_drv_command(s, bad) == -1 && em_iop_stream_fault(s)->code == EM_IOP_FAULT_BAD_INDEX);
    em_iop_stream_destroy(s);
}

/* The synthetic cue through 001F9820, 001FA790 and 001F9CF0 once per field. */
static void test_stream(const EmIopStreamDisc *disc)
{
    static Tap t;
    EmIopStream *s = em_iop_stream_create();
    EmIopVoiceTable table = {voice_table, &sdk_mask};
    EmStreamLanes L;
    EmStreamLanesData data;
    EmStreamLanesGlobals g;
    EmStreamLanesWorkers w;
    Ctx ctx = {s, 1};
    uint32_t b[5], field, seen = 0, words[4] = {0}, k, i;
    int last = -1, ok = 1;
    memset(&L, 0, sizeof L);
    memset(&g, 0, sizeof g);
    memset(&w, 0, sizeof w);
    memset(voice_table, 0, sizeof voice_table);
    memset(&t, 0, sizeof t);
    em_iop_stream_attach_disc(s, disc);
    em_iop_stream_set_voice_table(s, &table);
    em_iop_stream_set_tap(s, tap, &t);
    CHECK(em_iop_stream_boot_buffers(s, b) == 0);
    g.d275B28 = b[1];
    g.d275B24 = b[3];
    g.d275B20 = b[4];
    g.d281880 = em_iop_stream_ee_status(s) + 48;
    CHECK(em_iop_stream_sub_O_STREAM_MUSIC_DAT_1(s, &L.state.music_sector, &L.state.voice_sector) == 0);
    em_iop_stream_lanes_data(disc, &data);
    w.ctx = &ctx;
    w.w_00122BB8 = w_rng;
    w.w_001FC280 = w_none;
    w.w_001FBC50 = w_none;
    em_iop_stream_lane_workers(s, &w);
    em_stream_lanes_bind(&L, &data, &g, &w);
    CHECK(em_stream_lanes_001F9820(&L) == 0);
    CHECK(L.state.lane[0].voice == 0 && L.state.voice_right == 1);
    for (field = 0; field < 3; field++)
        CHECK(em_iop_stream_field(s) == 0);
    CHECK(em_iop_stream_driver(s)->voice[1].iop_addr == 0xADC00 &&
          em_iop_stream_driver(s)->voice[0].iop_addr == 0xAE000);
    CHECK(em_stream_lanes_001FA790(&L, 0, CUE) == 0);
    for (field = 0; field < 900; field++) {
        int32_t word;
        g.d810E90 = field;
        CHECK(em_iop_stream_field(s) == 0);
        CHECK(em_stream_lanes_001F9CF0(&L) == 0);
        word = em_iop_stream_ee_status(s)[48];
        if (word != last && word != 0 && seen < 4)
            words[seen++] = (uint32_t)word;
        last = word;
    }
    CHECK(em_iop_stream_fault(s)->code == 0 && L.fault.code == 0);
    CHECK(L.state.active[0] == 2);
    /* the EE sees the cursor step through the quarters in order */
    CHECK(seen == 4 && words[0] == 0x4000 && words[1] == 0x8000 && words[2] == 0xC000 && words[3] == 0x4000);
    /* 900 fields = ~720,000 samples: more than 5 passes over the 64 KiB
     * buffer and 2 loops of the cue. Voice 1 (left) and voice 0 (right) play
     * the channel blocks in order, wrapping at the cue end, with no block
     * skipped or repeated. */
    for (k = 0; k < 2; k++) {
        uint32_t channel = k == 1 ? 0u : 1u, blocks = CUE_SECTORS * 2048u / 32u;
        CHECK(t.count[k] > 700000u / 2u || t.count[k] == 400000u);
        for (i = 0; i < t.count[k] && ok; i++) {
            uint32_t n = (i / 28u) % blocks;
            int16_t expect = (int16_t)(block_nibble((int)channel, n) * 4096);
            if (t.samples[k][i] != expect) {
                fprintf(stderr, "voice %u sample %u: %d, expected %d (block %u)\n", k, i, t.samples[k][i], expect, n);
                ok = 0;
            }
        }
    }
    CHECK(ok);
    /* release: the key-off entry clears the cursor, the EE sees 0 */
    CHECK(em_stream_lanes_001FABB0(&L) == 0);
    for (field = 0; field < 4; field++) {
        CHECK(em_iop_stream_field(s) == 0);
        CHECK(em_stream_lanes_001F9CF0(&L) == 0);
    }
    CHECK(em_iop_stream_ee_status(s)[48] == 0 && em_iop_stream_ee_status(s)[49] == 0);
    CHECK(em_iop_stream_driver(s)->voice[0].active == 0);
    {   /* mixer: 48 kHz drains, other rates are counted */
        static float out[2 * 4096];
        uint64_t over, under, rate;
        memset(out, 0, sizeof out);
        em_iop_stream_mix(s, out, 4096, 44100);
        em_iop_stream_mix(s, out, 4096, 48000);
        em_iop_stream_mix_counters(s, &over, &under, &rate);
        CHECK(rate == 1 && over > 0);
    }
    em_iop_stream_destroy(s);
}

static void test_reader_faults(const EmIopStreamDisc *disc)
{
    EmIopStream *s = em_iop_stream_create();
    const uint8_t mode[3] = {0, 0, 0};
    int32_t r = 0;
    em_iop_stream_attach_disc(s, disc);
    CHECK(em_iop_stream_00113280(s, 1, &r) == 0 && r == 2);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 2, 3, 0xADC00, mode, &r) == 0 && r == 1);
    em_iop_stream_set_disc_latency(s, 0);
    CHECK(em_iop_stream_00112D18(s, 1, &r) == 0 && r == 0);
    CHECK(!memcmp(em_iop_stream_iop_ram(s) + 0xADC00, disc_data + 2 * 2048, 3 * 2048));
    em_iop_stream_set_disc_latency(s, 2);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN, 1, 0xBDD00, mode, &r) == 0);
    CHECK(em_iop_stream_00112D18(s, 1, &r) == 0 && r == 1);
    CHECK(em_iop_stream_00112D18(s, 1, &r) == 0 && r == 1);
    CHECK(em_iop_stream_00112D18(s, 1, &r) == 0 && r == 0);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + CUE_SECTORS, 1, 0xBDD00, mode, &r) == -1);
    CHECK(em_iop_stream_fault(s)->code == EM_IOP_FAULT_NOT_EXPORTED && em_iop_stream_fault(s)->address == 0x00112610);
    CHECK(em_iop_stream_field(s) == -1);   /* latched */
    em_iop_stream_destroy(s);
}

int main(void)
{
    EmIopStreamDisc disc;
    build_disc(&disc);
    test_heap_and_table();
    test_directory(&disc);
    test_commands();
    test_reader_faults(&disc);
    test_stream(&disc);
    if (failures) {
        fprintf(stderr, "iop_stream_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("iop_stream_test: ok\n");
    return 0;
}
