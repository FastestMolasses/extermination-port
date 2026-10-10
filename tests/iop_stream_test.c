/* Native contract test for the IOP stream backend (src/game/em_iop_stream.c,
 * docs/IOP_STREAM.md): heap blocks, 0011A2B0, the disc directory, the
 * driver's command decode, a synthetic stereo stream driven through the
 * original lanes (em_stream_lanes_original) field by field with the played
 * samples checked for continuity across buffer wraps, fail-stop faults and
 * the mixer ring, and the drive's two timings (host speed, the default, and
 * the drive model behind the PS2 disc-drive timing switch). No assets
 * needed; the original-instruction oracle and the capture comparisons are
 * tools/test_iop_stream_reference.py. */
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

/* The synthetic cue through 001F9820, 001FA790 and 001F9CF0 once per field,
 * with the drive at host speed (ps2 = 0) or on the drive model (1). */
static void test_stream(const EmIopStreamDisc *disc, int ps2)
{
    static Tap t;
    EmIopStream *s = em_iop_stream_create();
    em_iop_stream_set_ps2_drive_timing(s, ps2);
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

/* Polls 00112D18 answers busy after a read issued now, advancing one field
 * before each poll (the lanes poll once per frame, after the field). */
static int busy_polls(EmIopStream *s)
{
    int32_t r = 1;
    int n = -1;
    while (r == 1 && n < 40) {
        CHECK(em_iop_stream_field(s) == 0);
        CHECK(em_iop_stream_00112D18(s, 1, &r) == 0);
        n++;
    }
    return n;
}

/* The drive model (docs/IOP_STREAM.md "Drive model", measured in the
 * decomp's docs/CAPTURES_C7.md section 1). */
static void test_reader(const EmIopStreamDisc *music_only)
{
    const uint8_t mode[3] = {0, 0, 0};
    int32_t r = 0;
    int measured = 0;
    /* A far extent (8 sectors 80000 past the music) for the full seek. */
    EmIopStreamDisc disc = *music_only;
    EmIopStreamExtent ext[2] = {music_only->extent[0], {MUSIC_LSN + 80000u, 8, CUE_SECTORS * 2048u, 1}};
    disc.extent = ext;
    disc.extents = 2;

    /* The classes: the measured distances and the nearest-measured rule. */
    CHECK(em_iop_stream_drive_seek_fields(0, &measured) == 0 && measured);
    CHECK(em_iop_stream_drive_seek_fields(14, &measured) == 0 && measured);
    CHECK(em_iop_stream_drive_seek_fields(-23, &measured) == 2 && measured);
    CHECK(em_iop_stream_drive_seek_fields(29, &measured) == 2 && measured);
    CHECK(em_iop_stream_drive_seek_fields(1604, &measured) == 2 && measured);
    CHECK(em_iop_stream_drive_seek_fields(-1604, &measured) == 2 && measured);
    CHECK(em_iop_stream_drive_seek_fields(72124, &measured) == 6 && measured);
    CHECK(em_iop_stream_drive_seek_fields(-89445, &measured) == 6 && measured);
    CHECK(em_iop_stream_drive_seek_fields(3026, &measured) == 2 && !measured);    /* nearer 1604 */
    CHECK(em_iop_stream_drive_seek_fields(-89509, &measured) == 6 && !measured);
    CHECK(em_iop_stream_drive_seek_fields(40000, &measured) == 6 && !measured);   /* nearer 72124 */
    CHECK(em_iop_stream_drive_seek_fields(-5, &measured) == 0 && !measured);
    CHECK(em_iop_stream_drive_seek_fields(-20, &measured) == 2 && !measured);

    EmIopStream *s = em_iop_stream_create();
    CHECK(em_iop_stream_ps2_drive_timing(s) == 0);   /* host speed by default */
    em_iop_stream_set_ps2_drive_timing(s, 1);
    CHECK(em_iop_stream_ps2_drive_timing(s) == 1);
    em_iop_stream_attach_disc(s, &disc);
    CHECK(em_iop_stream_00113280(s, 1, &r) == 0 && r == 2);
    /* The first read has no position: a full seek, done at the 7th poll. */
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 2, 3, 0xADC00, mode, &r) == 0 && r == 1);
    CHECK(em_iop_stream_00112D18(s, 1, &r) == 0 && r == 1);   /* not in the issue field */
    CHECK(em_iop_stream_00113280(s, 1, &r) == 0 && r == 6);   /* the drive is busy */
    CHECK(busy_polls(s) == 6);
    CHECK(!memcmp(em_iop_stream_iop_ram(s) + 0xADC00, disc_data + 2 * 2048, 3 * 2048));
    /* Contiguous (the position is the sector after the last read): the
     * first poll completes it. */
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 5, 16, 0xBDD00, mode, &r) == 0);
    CHECK(busy_polls(s) == 0);
    CHECK(!memcmp(em_iop_stream_iop_ram(s) + 0xBDD00, disc_data + 5 * 2048, 16 * 2048));
    /* +14 read-through: 0; then a backward fast seek: 2. */
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 35, 1, 0xBDD00, mode, &r) == 0);
    CHECK(busy_polls(s) == 0);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 0, 1, 0xBDD00, mode, &r) == 0);   /* d = -36 */
    CHECK(busy_polls(s) == 2);
    /* A full seek to the far extent and back: 6 each. */
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 80000u, 8, 0xCDE00, mode, &r) == 0);
    CHECK(busy_polls(s) == 6);
    CHECK(!memcmp(em_iop_stream_iop_ram(s) + 0xCDE00, disc_data + CUE_SECTORS * 2048, 8 * 2048));
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 1, 1, 0xCDE00, mode, &r) == 0);
    CHECK(busy_polls(s) == 6);
    /* A read the EE abandons (001FABB0's D_00282157 reset without the break)
     * keeps the drive busy: 00113280 answers 6 until it is done, then 2 with
     * its sectors landed (the opening, n2..n18). */
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 30, 1, 0xCDE00, mode, &r) == 0);   /* d = +28: fast */
    for (int k = 0; k < 3; k++) {
        CHECK(em_iop_stream_field(s) == 0);
        CHECK(em_iop_stream_00113280(s, 1, &r) == 0 && r == (k < 2 ? 6 : 2));
    }
    CHECK(!memcmp(em_iop_stream_iop_ram(s) + 0xCDE00, disc_data + 30 * 2048, 2048));
    /* A break drops the read (its data never land) and leaves no position:
     * the next read is served as a full seek. */
    memset(em_iop_stream_iop_ram(s) + 0xCDE00, 0, 2048);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 31, 1, 0xCDE00, mode, &r) == 0);
    CHECK(em_iop_stream_00113478(s, 1) == 0);
    CHECK(em_iop_stream_00113280(s, 1, &r) == 0 && r == 2);
    CHECK(em_iop_stream_iop_ram(s)[0xCDE00] == 0 && em_iop_stream_iop_ram(s)[0xCDE00 + 2047] == 0);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 22, 1, 0xCDE00, mode, &r) == 0);
    CHECK(busy_polls(s) == 6);
    {
        EmIopDriveStats st;
        em_iop_stream_drive_stats(s, &st);
        CHECK(st.reads == 9 && st.by_fields[0] == 3 && st.by_fields[2] == 2 && st.by_fields[6] == 4);
        CHECK(st.no_position == 2 && st.breaks == 1 && st.abandoned == 1 && st.unmeasured == 0);
        CHECK(st.host_speed == 0);
    }
    /* Faults: a read while one is in flight, a read longer than 16 sectors
     * (outside the model), a sector the export does not hold. */
    {
        EmIopStream *t = em_iop_stream_create();
        em_iop_stream_attach_disc(t, &disc);
        CHECK(em_iop_stream_00112610(t, MUSIC_LSN, 1, 0xBDD00, mode, &r) == 0);
        CHECK(em_iop_stream_00112610(t, MUSIC_LSN + 1, 1, 0xBDD00, mode, &r) == -1);
        CHECK(em_iop_stream_fault(t)->code == EM_IOP_FAULT_UNSUPPORTED &&
              em_iop_stream_fault(t)->address == 0x00112610);
        em_iop_stream_destroy(t);
        t = em_iop_stream_create();
        em_iop_stream_attach_disc(t, &disc);
        CHECK(em_iop_stream_00112610(t, MUSIC_LSN, 17, 0xBDD00, mode, &r) == -1);
        CHECK(em_iop_stream_fault(t)->code == EM_IOP_FAULT_UNSUPPORTED);
        em_iop_stream_destroy(t);
    }
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + CUE_SECTORS, 1, 0xBDD00, mode, &r) == -1);
    CHECK(em_iop_stream_fault(s)->code == EM_IOP_FAULT_NOT_EXPORTED && em_iop_stream_fault(s)->address == 0x00112610);
    CHECK(em_iop_stream_field(s) == -1);   /* latched */
    em_iop_stream_destroy(s);
}

/* Host speed (the switch off, the default): the same reads as test_reader,
 * each done at the first query after its issue whatever its distance, with
 * the same data, abandoned-read and break rules; the counters still classify
 * the distances. */
static void test_reader_host(const EmIopStreamDisc *music_only)
{
    const uint8_t mode[3] = {0, 0, 0};
    int32_t r = 0;
    EmIopStreamDisc disc = *music_only;
    EmIopStreamExtent ext[2] = {music_only->extent[0], {MUSIC_LSN + 80000u, 8, CUE_SECTORS * 2048u, 1}};
    static const struct { uint32_t sector, count, addr; } reads[] = {
        {MUSIC_LSN + 2, 3, 0xADC00},       /* no position (full-seek class)   */
        {MUSIC_LSN + 5, 16, 0xBDD00},      /* contiguous                      */
        {MUSIC_LSN + 35, 1, 0xBDD00},      /* +14 read-through                */
        {MUSIC_LSN + 0, 1, 0xBDD00},       /* d = -36: fast-seek class        */
        {MUSIC_LSN + 80000u, 8, 0xCDE00},  /* full-seek class                 */
        {MUSIC_LSN + 1, 1, 0xCDE00},       /* full-seek class back            */
    };
    disc.extent = ext;
    disc.extents = 2;
    EmIopStream *s = em_iop_stream_create();
    em_iop_stream_attach_disc(s, &disc);
    for (unsigned i = 0; i < sizeof reads / sizeof reads[0]; i++) {
        const uint8_t *want = reads[i].sector >= MUSIC_LSN + 80000u
            ? disc_data + CUE_SECTORS * 2048 : disc_data + (reads[i].sector - MUSIC_LSN) * 2048;
        CHECK(em_iop_stream_00113280(s, 1, &r) == 0 && r == 2);
        CHECK(em_iop_stream_00112610(s, reads[i].sector, reads[i].count, reads[i].addr, mode, &r) == 0 && r == 1);
        CHECK(busy_polls(s) == 0);
        CHECK(!memcmp(em_iop_stream_iop_ram(s) + reads[i].addr, want, reads[i].count * 2048u));
    }
    /* A query in the issue field already finds the read done. */
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 2, 1, 0xADC00, mode, &r) == 0);
    CHECK(em_iop_stream_00112D18(s, 1, &r) == 0 && r == 0);
    /* An abandoned read lands at the next ready query, which answers 2. */
    memset(em_iop_stream_iop_ram(s) + 0xCDE00, 0, 2048);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 30, 1, 0xCDE00, mode, &r) == 0);
    CHECK(em_iop_stream_field(s) == 0);
    CHECK(em_iop_stream_00113280(s, 1, &r) == 0 && r == 2);
    CHECK(!memcmp(em_iop_stream_iop_ram(s) + 0xCDE00, disc_data + 30 * 2048, 2048));
    /* A break before any query drops the read: its data never land. */
    memset(em_iop_stream_iop_ram(s) + 0xCDE00, 0, 2048);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 31, 1, 0xCDE00, mode, &r) == 0);
    CHECK(em_iop_stream_00113478(s, 1) == 0);
    CHECK(em_iop_stream_00113280(s, 1, &r) == 0 && r == 2);
    CHECK(em_iop_stream_iop_ram(s)[0xCDE00] == 0 && em_iop_stream_iop_ram(s)[0xCDE00 + 2047] == 0);
    {
        EmIopDriveStats st;
        em_iop_stream_drive_stats(s, &st);
        CHECK(st.reads == 9 && st.host_speed == 9 && st.breaks == 1 && st.abandoned == 1);
        CHECK(st.by_fields[0] == 4 && st.by_fields[2] == 2 && st.by_fields[6] == 3 && st.no_position == 1);
    }
    em_iop_stream_destroy(s);
}

/* The first stream read after a module-loader read (docs/IOP_STREAM.md
 * "Drive model"; em_iop_stream_loader_read): with the switch on it takes the
 * measured 17-field seek whatever its distance, the head then follows the
 * stream read again; a break forgets it; at host speed nothing changes. */
static void test_reader_after_loader(const EmIopStreamDisc *music_only)
{
    const uint8_t mode[3] = {0, 0, 0};
    int32_t r = 0;
    EmIopStreamDisc disc = *music_only;
    EmIopStreamExtent ext[2] = {music_only->extent[0], {MUSIC_LSN + 80000u, 8, CUE_SECTORS * 2048u, 1}};
    EmIopDriveStats st;
    disc.extent = ext;
    disc.extents = 2;

    EmIopStream *s = em_iop_stream_create();
    em_iop_stream_set_ps2_drive_timing(s, 1);
    em_iop_stream_attach_disc(s, &disc);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 2, 3, 0xADC00, mode, &r) == 0);
    CHECK(busy_polls(s) == 6);   /* the first read: no position */
    /* A loader read (the head moves to the sector after it): the next
     * stream read seeks 17 fields, also where its distance alone would be
     * contiguous; the 00113280 ready query answers 6 meanwhile. */
    em_iop_stream_loader_read(s, MUSIC_LSN - 161u, 161u, 1);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN, 1, 0xADC00, mode, &r) == 0);
    CHECK(em_iop_stream_00113280(s, 1, &r) == 0 && r == 6);
    CHECK(busy_polls(s) == EM_IOP_SEEK_AFTER_LOADER_READ);
    CHECK(!memcmp(em_iop_stream_iop_ram(s) + 0xADC00, disc_data, 2048));
    /* Only the first: the head is the stream read's again. */
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 1, 1, 0xADC00, mode, &r) == 0);
    CHECK(busy_polls(s) == 0);
    /* A load no capture measured takes the same seek and is counted. */
    em_iop_stream_loader_read(s, MUSIC_LSN + 80000u, 8u, 0);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 2, 1, 0xADC00, mode, &r) == 0);
    CHECK(busy_polls(s) == EM_IOP_SEEK_AFTER_LOADER_READ);
    /* A break with a read in flight forgets the loader read too: the next
     * read has no position (a full seek). */
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 3, 1, 0xADC00, mode, &r) == 0);
    em_iop_stream_loader_read(s, MUSIC_LSN + 80000u, 8u, 1);
    CHECK(em_iop_stream_00113478(s, 1) == 0);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 4, 1, 0xADC00, mode, &r) == 0);
    CHECK(busy_polls(s) == 6);
    em_iop_stream_drive_stats(s, &st);
    CHECK(st.reads == 6 && st.after_loader == 2 && st.after_loader_unmeasured == 1);
    CHECK(st.by_fields[0] == 2 && st.by_fields[6] == 2 && st.no_position == 2 && st.breaks == 1);
    em_iop_stream_destroy(s);

    /* Host speed (the default): a loader read changes nothing, the next
     * read is done at its first query and keeps its distance class. */
    s = em_iop_stream_create();
    em_iop_stream_attach_disc(s, &disc);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 2, 3, 0xADC00, mode, &r) == 0);
    CHECK(busy_polls(s) == 0);
    em_iop_stream_loader_read(s, MUSIC_LSN + 80000u, 8u, 1);
    CHECK(em_iop_stream_00112610(s, MUSIC_LSN + 5, 1, 0xADC00, mode, &r) == 0);
    CHECK(busy_polls(s) == 0);
    em_iop_stream_drive_stats(s, &st);
    CHECK(st.reads == 2 && st.host_speed == 2 && st.after_loader == 0 && st.after_loader_unmeasured == 0);
    CHECK(st.by_fields[0] == 1 && st.by_fields[6] == 1 && st.no_position == 1);
    em_iop_stream_destroy(s);
}

/* The sound-bank transfer (docs/IOP_STREAM.md "The sound-bank transfer"):
 * the heap's first fit with the driver's start-up block, 0010F968, the SIF
 * DMA at host speed, command 0x20 on the SPU2 model and its callback 0x544
 * at the next driver tick, which the exchange after it delivers. */
static void test_bank_transfer(void)
{
    EmIopStream *s = em_iop_stream_create();
    uint32_t b[5], block = 0, other = 0;
    int32_t id = 0, st = 0;
    static uint8_t bank[0x100];
    CHECK(s && em_iop_stream_boot_buffers(s, b) == 0 && em_iop_stream_boot_driver(s) == 0);
    CHECK(em_iop_stream_heap_next(s) == 0xDE800);
    CHECK(em_iop_stream_0010F8F8(s, 0x492E0, &block) == 0 && block == 0xDE800);
    CHECK(em_iop_stream_0010F8F8(s, 0x10, &other) == 0 && other == 0xDE800 + 0x49300);
    CHECK(em_iop_stream_0010F968(s, block) == 0);
    CHECK(em_iop_stream_0010F8F8(s, 0x100, &block) == 0 && block == 0xDE800);   /* first fit */
    CHECK(em_iop_stream_0010F968(s, block) == 0 && em_iop_stream_0010F968(s, other) == 0);
    for (unsigned i = 0; i < sizeof bank; i++) bank[i] = (uint8_t)(i * 7 + 1);
    CHECK(em_iop_stream_sif_set_dma(s, bank, 0xDE800, sizeof bank, &id) == 0 && id > 0);
    CHECK(em_iop_stream_sif_dma_stat(s, id, &st) == 0 && st < 0);
    CHECK(!memcmp(em_iop_stream_iop_ram(s) + 0xDE800, bank, sizeof bank));
    em_iop_stream_seed_transfer_count(s, 5);
    CHECK(em_iop_stream_ee_status(s)[112] == 5);
    /* 00119400(0x20, 0xDE800, 0x1A0000, 0x100) with the count 6 */
    CHECK(em_iop_stream_001157F0(s, 0x20, 6 << 8 | 0x0D, (int32_t)(0xE800u << 16 | 0x1A00u),
                                 0x100) == 1);
    CHECK(em_iop_stream_field(s) == 0);    /* the exchange (5), then the field runs 0x20 */
    CHECK(em_iop_stream_ee_status(s)[112] == 5 && em_iop_stream_driver(s)->trans_count == 6);
    CHECK(em_iop_stream_driver(s)->status[112] == 6);   /* 0x544 at the next tick */
    CHECK(!memcmp(em_iop_stream_spu_ram(s) + 0x1A0000, bank, sizeof bank));
    CHECK(em_iop_stream_field(s) == 0 && em_iop_stream_ee_status(s)[112] == 6);
    /* faults: an unheld block, a partial quadword */
    CHECK(em_iop_stream_0010F968(s, 0x12300) == -1 && em_iop_stream_fault(s)->code == EM_IOP_FAULT_BAD_INDEX);
    em_iop_stream_destroy(s);
    s = em_iop_stream_create();
    CHECK(em_iop_stream_sif_set_dma(s, bank, 0xDE800, 0x18, &id) == -1);
    em_iop_stream_destroy(s);
}

int main(void)
{
    EmIopStreamDisc disc;
    build_disc(&disc);
    test_heap_and_table();
    test_bank_transfer();
    test_directory(&disc);
    test_commands();
    test_reader(&disc);
    test_reader_host(&disc);
    test_reader_after_loader(&disc);
    test_stream(&disc, 0);
    test_stream(&disc, 1);
    if (failures) {
        fprintf(stderr, "iop_stream_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("iop_stream_test: ok\n");
    return 0;
}
