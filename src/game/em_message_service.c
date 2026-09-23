#include "game/em_message_service.h"
#include <string.h>

_Static_assert(sizeof(EmMessageBlock) == EM_MESSAGE_BLOCK_SIZE, "D_002821B0 block size");
_Static_assert(sizeof(EmMessageRecord) == 8, "D_00264DD0 record stride");
_Static_assert(sizeof(EmMessageStreamRow) == 16, "D_0026EC60 row stride");
_Static_assert(offsetof(EmMessageBlock, cursor) == 0x20, "block +0x20");
_Static_assert(offsetof(EmMessageBlock, current) == 0x34, "block +0x34");
_Static_assert(offsetof(EmMessageBlock, wait_stream) == 0x50, "block +0x50");
_Static_assert(offsetof(EmMessageBlock, loaded) == 0x5C, "block +0x5C");
_Static_assert(offsetof(EmMessageBlock, voice_line) == 0x70, "block +0x70");
_Static_assert(offsetof(EmMessageBlock, status) == 0x74, "block +0x74");
_Static_assert(offsetof(EmMessageBlock, aux_mode) == 0x90, "block +0x90");
_Static_assert(offsetof(EmMessageBlock, aux_result) == 0x98, "block +0x98");

static int fail(EmMessageService *s, const char *why)
{
    if (!s->fault) s->fault = why;
    return -1;
}

#define WORKER(s, name, ...) \
    ((s)->workers.name ? ((s)->workers.name((s)->workers.context, __VA_ARGS__) ? 0 \
        : fail((s), #name " worker failed")) : fail((s), #name " worker missing"))

static uint32_t u32le(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* D_00264DD0[area + 1]: fault instead of reading a missing table. */
static const EmMessageTable *area_table(EmMessageService *s, const EmMessageShared *sh)
{
    if (sh->area >= s->data->area_count || !s->data->areas[sh->area].records) {
        fail(s, "no message table for area");
        return NULL;
    }
    return &s->data->areas[sh->area];
}

static const EmMessageRecord *record_at(EmMessageService *s, const EmMessageTable *t, uint32_t index)
{
    if (!t || !t->records || index >= t->count) {
        fail(s, "message record outside table");
        return NULL;
    }
    return &t->records[index];
}

static int shared_ok(EmMessageService *s, const EmMessageShared *sh)
{
    if (!sh || !sh->voice_mode || !sh->stream_mode || !sh->flag_mailbox)
        return fail(s, "shared message state missing");
    return 0;
}

void em_message_reset(EmMessageService *s)
{
    /* 001FC9B0: 00121A28(&D_002821B0, 0, 0x9C), D_00275C50 = D_0026EC10[0],
     * D_00275C54 = 0x80, D_002821D0 = &D_00264D10, D_00275C55 = 0. */
    memset(&s->block, 0, sizeof s->block);
    s->text_color = s->data->default_color;
    s->text_glyph = 0x80;
    s->block.cursor = s->data->default_cursor;
    s->text_flag = 0;
}

int em_message_init(EmMessageService *s, const EmMessageData *data, const EmMessageWorkers *workers)
{
    if (!s || !data) return 0;
    /* A count without its table would be dereferenced by area_table,
     * record_at or the D_0026EC60 scans; refuse it here. */
    if ((data->area_count && !data->areas) || (data->stream_count && !data->streams) ||
        (data->global.count && !data->global.records))
        return 0;
    s->data = data;
    if (workers) s->workers = *workers;
    else memset(&s->workers, 0, sizeof s->workers);
    s->fault = NULL;
    return 1;
}

/* 001FA5A0: push only into an empty (-1) slot; head advances & 0xF. */
int em_message_voice_ring_push(EmMessageVoiceRing *r, int32_t cue)
{
    if (r->head < 0 || r->head >= EM_MESSAGE_VOICE_RING) return -1;
    int32_t *slot = &r->slots[r->head];
    if (*slot != -1) return 1;
    *slot = cue;
    r->head = (int8_t)((r->head + 1) & 0xF);
    return 1;
}

/* Shared tail of 001FD580/001FD6A0: first record from `index` whose voice
 * is not -1 is pushed (D_008106F5 = 2, 001FA5A0); a voiceless record with
 * nonzero +5 ends the scan as a miss. */
static int voice_scan(EmMessageService *s, const EmMessageShared *sh,
                      const EmMessageTable *t, int32_t index, int32_t *out)
{
    for (int32_t at = index;; at++) {
        const EmMessageRecord *r = record_at(s, t, (uint32_t)at);
        if (!r) return -1;
        if (r->voice != -1) {
            *sh->voice_mode = 2;
            if (WORKER(s, voice_push, r->voice)) return -1;
            *out = at;
            return 1;
        }
        if (r->wait_stream) { *out = -1; return 0; }
    }
}

/* 001FD580(index, &+0x70): a (area, line) stream-table row claims the line
 * (D_008106F4 = 0, return 2); otherwise voice_scan from index. */
static int voice_first(EmMessageService *s, const EmMessageShared *sh, uint32_t index, int32_t *out)
{
    const EmMessageTable *t = area_table(s, sh);
    if (!t) return -1;
    for (uint32_t i = 0; i < s->data->stream_count; i++) {
        const EmMessageStreamRow *row = &s->data->streams[i];
        if (row->area == sh->area && row->line == (int32_t)index) {
            *sh->stream_mode = 0;
            *out = -1;
            return 2;
        }
    }
    return voice_scan(s, sh, t, (int32_t)index, out);
}

/* 001FD6A0(index, &+0x70): the current record's +5 == 1 ends the voice
 * chain; otherwise voice_scan from index + 1. */
static int voice_next(EmMessageService *s, const EmMessageShared *sh, uint32_t index, int32_t *out)
{
    const EmMessageTable *t = area_table(s, sh);
    const EmMessageRecord *r = t ? record_at(s, t, index) : NULL;
    if (!r) return -1;
    if (r->wait_stream == 1) { *out = -1; return 0; }
    return voice_scan(s, sh, t, (int32_t)index + 1, out);
}

/* 001FD790: fetch the record at line + record, skipping 0/0 records. */
static int fetch(EmMessageService *s, const EmMessageShared *sh)
{
    EmMessageBlock *b = &s->block;
    for (;;) {
        b->current = b->line + (uint32_t)b->record;
        uint32_t index = b->current & 0x7FFFFFFFu;
        const EmMessageRecord *r;
        if (b->current & 0x80000000u) {
            r = record_at(s, &s->data->global, index); /* D_00264DD0[0] */
            if (!r) return -1;
        } else {
            const EmMessageTable *t = area_table(s, sh);
            r = t ? record_at(s, t, index) : NULL;
            if (!r) return -1;
            if (*sh->voice_mode == 2) return 0;
            if (b->record == 0 && *sh->voice_mode == 0) {
                int found = voice_first(s, sh, index, &b->voice_line);
                if (found < 0) return -1;
                if (found == 1) return 0;
            }
        }
        if (!r->wait_stream && !r->duration) { b->record++; continue; }
        if (b->record == 0) b->frames = 0;
        b->remaining = r->duration;
        b->wait_stream = r->wait_stream;
        b->slot = r->slot;
        return 1;
    }
}

/* 001FD950: draw every present tick, then the flag mailbox. */
static int present(EmMessageService *s, const EmMessageShared *sh)
{
    EmMessageBlock *b = &s->block;
    if (WORKER(s, draw_line, (b->current & 0x80000000u) != 0, b->current & 0x7FFFFFFFu))
        return -1;
    if (b->remaining > 0) {
        if (b->slot != 0xFF) {
            if (b->slot >= EM_MESSAGE_FLAG_SLOTS) return fail(s, "flag slot outside mailbox");
            if (sh->game_mode == 2 && b->slot == 0 && WORKER(s, face_talk, 1)) return -1;
            sh->flag_mailbox[b->slot] = 1;
            b->talk_mask |= 1u << b->slot;
            b->slot = 0xFF;
        }
        b->remaining--;
        return 0;
    }
    for (int bit = 0; bit < EM_MESSAGE_FLAG_SLOTS; bit++) {
        if (!(b->talk_mask & 1u << bit)) continue;
        if (sh->game_mode == 2 && bit == 0 && WORKER(s, face_talk, 0)) return -1;
        sh->flag_mailbox[bit] = 2;
        b->talk_mask &= ~(1u << bit);
    }
    return 1;
}

/* 001FDB80(0): mode-2 tick. Returns the original value (1 = message done). */
static int text_tick(EmMessageService *s, const EmMessageShared *sh)
{
    EmMessageBlock *b = &s->block;
    if (b->loaded != 1) {
        if (b->loaded != 0) return 1;
        int got = fetch(s, sh);
        if (got <= 0) return got;
        b->loaded = 1;
    }
    if (!(b->current & 0x80000000u)) {
        int32_t current = (int32_t)b->current;
        if (*sh->voice_mode == 1) {
            if (b->voice_line == current) *sh->voice_mode = 0;
        } else if (*sh->voice_mode == 0 && current >= b->voice_line) {
            if (voice_next(s, sh, b->current, &b->voice_line) < 0) return -1;
        }
    }
    b->frames++;
    int shown = present(s, sh);
    if (shown <= 0) return shown;
    if (!b->wait_stream) {
        b->loaded = 0;
        b->record++;
        return 0;
    }
    if (sh->busy155 != 0 || sh->busy156 != 0) return 0;
    return 1;
}

/* 001FDB80(1) then 001FC9B0: mode-2 teardown (001FCA10 phase 2). */
static int teardown(EmMessageService *s, const EmMessageShared *sh)
{
    EmMessageBlock *b = &s->block;
    b->record = b->loaded = b->frames = b->remaining = b->voice_line = 0;
    b->status = b->status_76 = 0;
    for (int bit = 0; bit < EM_MESSAGE_FLAG_SLOTS; bit++) {
        if (!(b->talk_mask & 1u << bit)) continue;
        if (sh->game_mode == 2 && bit == 0 && WORKER(s, face_talk, 0)) return -1;
        sh->flag_mailbox[bit] = 2;
        b->talk_mask &= ~(1u << bit);
    }
    /* 001FAB80: 001FAAC0(1), 001FAAC0(2), D_008106F5 = 0. */
    if (WORKER(s, stop_lane, 1) || WORKER(s, stop_lane, 2)) return -1;
    *sh->voice_mode = 0;
    em_message_reset(s);
    return 0;
}

int em_message_tick(EmMessageService *s, const EmMessageShared *sh)
{
    if (s->fault) return -1;
    EmMessageBlock *b = &s->block;
    if (b->phase == 2) {
        if (shared_ok(s, sh)) return -1;
        return teardown(s, sh);
    }
    if (b->phase != 1) return 0;
    switch (b->mode) {
    case 2:
        if (b->delay > 0) { b->delay--; return 0; }
        if (shared_ok(s, sh)) return -1;
        {
            int done = text_tick(s, sh);
            if (done < 0) return -1;
            if (done == 1) b->phase = 2;
        }
        return 0;
    case 3:
        return WORKER(s, mode3_present, b);
    case 4:
        if (b->aux_mode == 0x64) {
            int32_t result = 0;
            if (WORKER(s, record_setup, b->line, b->aux_arg, b->aux_mode, &result)) return -1;
            b->aux_result = result;
            return WORKER(s, record_draw, b->line, 0xA8, 0xBE);
        }
        return WORKER(s, help_draw, 0x8A, 0xA8, b->aux_mode, b->line);
    case 16:
        em_message_reset(s);
        return 0;
    default: /* 0, 1 and unknown modes do nothing */
        return 0;
    }
}

static void post(EmMessageBlock *b, int32_t mode, const unsigned char *record)
{
    b->mode = mode;
    b->phase = 1;
    b->line = u32le(record + 0x14);
    b->delay = (int32_t)u32le(record + 0x18);
}

int em_message_op0c(EmMessageService *s, uint8_t *handshake, const unsigned char *record)
{
    /* Native contract, not original: refuse after a latched fault. */
    if (!s || s->fault) return -1;
    if (!handshake || !record) return fail(s, "op0C record missing");
    EmMessageBlock *b = &s->block;
    switch (u32le(record + 0x08)) {
    case 0: /* post a mode-2 message, complete when phase reaches 2 */
        if (*handshake == 0) { post(b, 2, record); *handshake = 1; }
        else if (*handshake != 1) return 0;
        return b->phase == 2;
    case 1: /* post; optional status handshake on +0x74 bit 15 */
        if (*handshake == 0) {
            post(b, 2, record);
            if (u32le(record + 0x1C) == 0) return 1;
            b->status = 1;
            *handshake = 1;
            return 0;
        }
        if (*handshake == 1) return (b->status & 0x8000) != 0;
        return 0;
    case 2: /* wait for idle */
        return b->phase == 2 || b->mode == 0;
    case 3:
        em_message_reset(s);
        return 1;
    case 4: /* post mode 4 with group 5, complete at phase 2 */
        if (*handshake == 0) { b->aux_mode = 5; post(b, 4, record); *handshake = 1; }
        else if (*handshake != 1) return 0;
        return b->phase == 2;
    case 5:
        b->aux_mode = 5;
        post(b, 4, record);
        return 1;
    case 6:
        b->phase = 2;
        return 1;
    default:
        return 0;
    }
}

int em_message_stream_request(EmMessageService *s, const EmMessageShared *sh, int32_t line)
{
    if (s->fault) return -1;
    if (shared_ok(s, sh)) return -1;
    for (uint32_t i = 0; i < s->data->stream_count; i++) {
        const EmMessageStreamRow *row = &s->data->streams[i];
        if (row->area != sh->area || row->line != line) continue;
        /* 001FD470(-1), D_008106F4 = 2, 001FA790(0, cue). */
        if (WORKER(s, stream_stop, -1)) return -1;
        *sh->stream_mode = 2;
        if (WORKER(s, stream_play, 0, row->cue)) return -1;
        return 1;
    }
    return 0;
}
