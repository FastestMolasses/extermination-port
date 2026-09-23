/* em_message_service contract checks on SYNTHETIC tables (no game data).
 * Original equivalence is tools/test_message_service_reference.py; this
 * test pins the native-only guarantees: missing workers, missing tables,
 * out-of-range records and slots fault instead of being simulated. */
#include "game/em_message_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Always compiled (unlike assert under -DNDEBUG): every call under test
 * sits inside CHECK. */
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    exit(1); } } while (0)

typedef struct {
    int draws, face[2], voices[8], voice_count, lanes[4], lane_count;
    int stream_stop, stream_play_cue, fail_draw;
} Log;

static int draw(void *c, int global, uint32_t index)
{
    Log *l = c; (void)global; (void)index;
    if (l->fail_draw) return 0;
    l->draws++;
    return 1;
}
static int face(void *c, int on) { ((Log *)c)->face[on != 0]++; return 1; }
static int voice(void *c, int32_t cue) { Log *l = c; l->voices[l->voice_count++] = cue; return 1; }
static int lane(void *c, int n) { Log *l = c; l->lanes[l->lane_count++] = n; return 1; }
static int sstop(void *c, int32_t mask) { ((Log *)c)->stream_stop = mask; return 1; }
static int splay(void *c, int l, int32_t cue) { (void)l; ((Log *)c)->stream_play_cue = cue; return 1; }

/* Synthetic area records: line 0 = {3 frames, voice 7, slot 0}, line 1
 * terminal; line 2 = slot 12 (outside the mailbox), line 3 terminal. */
static const EmMessageRecord area_records[] = {
    {3, 7, 0, 0, {0, 0}}, {0, -1, 0xFF, 1, {0, 0}},
    {2, -1, 12, 0, {0, 0}}, {0, -1, 0xFF, 1, {0, 0}},
};
static const EmMessageRecord global_records[] = {
    {2, -1, 0xFF, 0, {0, 0}}, {0, -1, 0xFF, 1, {0, 0}},
};
static const EmMessageStreamRow rows[] = {{5, 0, 9, 77}};

static unsigned char *op(unsigned char r[32], uint32_t sub, uint32_t line, uint32_t delay)
{
    memset(r, 0, 32);
    r[0] = 0x0C;
    memcpy(r + 0x08, &sub, 4);
    memcpy(r + 0x14, &line, 4);
    memcpy(r + 0x18, &delay, 4);
    return r;
}

int main(void)
{
    EmMessageTable areas[6] = {{0, 0}};
    areas[5].records = area_records;
    areas[5].count = 4;
    EmMessageData data = {{global_records, 2}, areas, 6, rows, 1, 0x606060, 0x264D10};
    uint8_t voice_mode = 0, stream_mode = 0, mailbox[12] = {0};
    EmMessageShared shared = {5, 2, 0, 0, &voice_mode, &stream_mode, mailbox};
    Log log = {0};
    EmMessageWorkers workers = {&log, draw, face, voice, lane, sstop, splay, 0, 0, 0, 0};
    EmMessageService s;
    unsigned char rec[32];
    uint8_t handshake = 0;

    CHECK(em_message_init(&s, &data, &workers) == 1);
    em_message_reset(&s);
    CHECK(s.text_color == 0x606060 && s.text_glyph == 0x80 && s.block.cursor == 0x264D10);

    /* Voiced area message: first tick pushes the voice and waits (F5 = 2). */
    CHECK(em_message_op0c(&s, &handshake, op(rec, 0, 0, 0)) == 0 && handshake == 1);
    CHECK(em_message_tick(&s, &shared) == 0);
    CHECK(log.voice_count == 1 && log.voices[0] == 7 && voice_mode == 2 && log.draws == 0);
    CHECK(em_message_tick(&s, &shared) == 0 && log.draws == 0);
    voice_mode = 1; /* the voice lane accepts it (001F9CF0, external) */
    for (int i = 0; i < 4; i++) CHECK(em_message_tick(&s, &shared) == 0);
    /* duration 3 is drawn 4 times; slot 0 talks (game mode 2) then the
     * completion draw releases it: mailbox 1 -> 2. */
    CHECK(voice_mode == 0 && log.draws == 4 && log.face[1] == 1 && log.face[0] == 1);
    CHECK(mailbox[0] == 2 && s.block.voice_line == -1);
    CHECK(em_message_tick(&s, &shared) == 0);       /* terminal record */
    CHECK(s.block.phase == 2 && log.draws == 5);
    CHECK(em_message_op0c(&s, &handshake, op(rec, 0, 0, 0)) == 1);
    CHECK(em_message_tick(&s, &shared) == 0);       /* teardown + reset */
    CHECK(log.lane_count == 2 && log.lanes[0] == 1 && log.lanes[1] == 2);
    CHECK(s.block.phase == 0 && s.block.mode == 0 && voice_mode == 0);

    /* Busy stream bytes hold the finished message. */
    handshake = 0;
    em_message_op0c(&s, &handshake, op(rec, 0, 0x80000000u, 0));
    shared.busy156 = -1;
    for (int i = 0; i < 6; i++) CHECK(em_message_tick(&s, &shared) == 0);
    CHECK(s.block.phase == 1);
    shared.busy156 = 0;
    CHECK(em_message_tick(&s, &shared) == 0 && s.block.phase == 2);
    CHECK(em_message_tick(&s, &shared) == 0 && s.block.phase == 0);

    /* Stream table request (001FD4C0). */
    CHECK(em_message_stream_request(&s, &shared, 9) == 1);
    CHECK(log.stream_stop == -1 && log.stream_play_cue == 77 && stream_mode == 2);
    CHECK(em_message_stream_request(&s, &shared, 10) == 0);

    /* Faults: slot outside the mailbox, latched. */
    handshake = 0;
    em_message_op0c(&s, &handshake, op(rec, 0, 2, 0));
    voice_mode = 1;
    CHECK(em_message_tick(&s, &shared) == -1 && s.fault);
    CHECK(strstr(s.fault, "slot"));
    CHECK(em_message_tick(&s, &shared) == -1);

    /* Missing and failing workers. */
    workers.draw_line = 0;
    em_message_init(&s, &data, &workers);
    em_message_reset(&s);
    handshake = 0;
    em_message_op0c(&s, &handshake, op(rec, 0, 0x80000000u, 0));
    CHECK(em_message_tick(&s, &shared) == -1 && !strcmp(s.fault, "draw_line worker missing"));
    workers.draw_line = draw;
    log.fail_draw = 1;
    em_message_init(&s, &data, &workers);
    em_message_reset(&s);
    handshake = 0;
    em_message_op0c(&s, &handshake, op(rec, 0, 0x80000000u, 0));
    CHECK(em_message_tick(&s, &shared) == -1 && !strcmp(s.fault, "draw_line worker failed"));
    log.fail_draw = 0;

    /* Mode 3/4 without presenters fault. */
    em_message_init(&s, &data, &workers);
    em_message_reset(&s);
    s.block.mode = 3; s.block.phase = 1;
    CHECK(em_message_tick(&s, &shared) == -1 && strstr(s.fault, "mode3_present"));
    em_message_init(&s, &data, &workers);
    em_message_reset(&s);
    handshake = 0;
    CHECK(em_message_op0c(&s, &handshake, op(rec, 4, 8, 0)) == 0 && s.block.aux_mode == 5);
    CHECK(em_message_tick(&s, &shared) == -1 && strstr(s.fault, "help_draw"));

    /* Missing area table, record outside table, missing shared state. */
    em_message_init(&s, &data, &workers);
    em_message_reset(&s);
    shared.area = 4;
    handshake = 0;
    em_message_op0c(&s, &handshake, op(rec, 0, 0, 0));
    CHECK(em_message_tick(&s, &shared) == -1 && strstr(s.fault, "no message table"));
    shared.area = 5;
    em_message_init(&s, &data, &workers);
    em_message_reset(&s);
    handshake = 0;
    em_message_op0c(&s, &handshake, op(rec, 0, 0x80000005u, 0));
    CHECK(em_message_tick(&s, &shared) == -1 && strstr(s.fault, "outside table"));
    em_message_init(&s, &data, &workers);
    em_message_reset(&s);
    handshake = 0;
    em_message_op0c(&s, &handshake, op(rec, 0, 0x80000000u, 0));
    shared.flag_mailbox = 0;
    CHECK(em_message_tick(&s, &shared) == -1 && strstr(s.fault, "shared"));
    shared.flag_mailbox = mailbox;

    /* op0C refuses once a fault is latched and leaves the block alone. */
    handshake = 0;
    CHECK(s.fault && em_message_op0c(&s, &handshake, op(rec, 0, 0x80000001u, 7)) == -1);
    CHECK(handshake == 0 && s.block.line == 0x80000000u && s.block.delay == 0);
    em_message_init(&s, &data, &workers);
    CHECK(em_message_op0c(&s, NULL, op(rec, 0, 0, 0)) == -1 && strstr(s.fault, "op0C"));

    /* init refuses a count without its table. */
    EmMessageService u;
    EmMessageData bad = data;
    bad.areas = NULL;
    CHECK(em_message_init(&u, &bad, &workers) == 0);
    bad = data;
    bad.streams = NULL;
    CHECK(em_message_init(&u, &bad, &workers) == 0);
    bad = data;
    bad.global.records = NULL;
    CHECK(em_message_init(&u, &bad, &workers) == 0);
    bad = data;
    bad.areas = NULL; bad.area_count = 0; bad.streams = NULL; bad.stream_count = 0;
    CHECK(em_message_init(&u, &bad, &workers) == 1);

    /* 001FA5A0 ring helper. */
    EmMessageVoiceRing ring;
    memset(ring.slots, 0xFF, sizeof ring.slots);
    ring.head = 15;
    CHECK(em_message_voice_ring_push(&ring, 150) == 1 && ring.slots[15] == 150 && ring.head == 0);
    ring.head = 16;
    CHECK(em_message_voice_ring_push(&ring, 1) == -1);

    puts("message_service_test: PASS");
    return 0;
}
