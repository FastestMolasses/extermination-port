/* The live message service (WP-8). See em_message_live.h and
 * docs/MESSAGE_SERVICE.md "Binding". */
#include "game/em_message_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_hud.h"
#include "game/em_message_draw_original.h"
#include "game/em_message_glyph_original.h"
#include "game/em_scene_bindings.h"

enum {
    AREA_TABLES = 23,          /* D_00264DD0[1..23]                 */
    FRAME_FLUSHES = 64,        /* 001CC3B0 calls kept per frame      */
    FRAME_UPLOADS = 1024       /* strip uploads kept per frame       */
};

static struct {
    int installed, loaded;
    uint8_t *blob;
    EmMessageRecord *global_records, *area_records;
    EmMessageStreamRow *rows;
    EmMessageTable areas[AREA_TABLES];
    EmMessageData data;
    uint32_t area;
    int32_t colors[16];
    EmMessageTextStyle style;            /* D_00275C50, the one storage       */
    EmMessageDrawConfig line_config;     /* D_00264CD0 (+0x14 = &style)       */
    EmMessageDrawConfig fallback;        /* D_00264BF0 (+0x14 = 0)            */
    EmMessageDrawData draw_data;
    EmMessageDraw draw;
    EmMessageGlyph glyph;
    EmMessageService service;
    EmMessageLiveHost host;
    EmMessageLiveStreams streams;
    EmMessageGlyphFlush flushes[FRAME_FLUSHES];
    EmMessageGlyphUpload uploads[FRAME_UPLOADS];
    uint32_t flush_count, upload_count;
    const char *fault;
    int reported;
} s;

static int fail(const char *why)
{
    if (!s.fault) s.fault = why;
    return 0;
}

static void report(void)
{
    if (s.reported) return;
    s.reported = 1;
    fprintf(stderr, "message service: fault: %s", s.fault ? s.fault : "?");
    if (s.service.fault) fprintf(stderr, "; service: %s", s.service.fault);
    if (s.draw.fault) fprintf(stderr, "; draw: %s", s.draw.fault);
    if (s.glyph.fault) fprintf(stderr, "; glyph: %s", s.glyph.fault);
    fputc('\n', stderr);
}

/* ------------------------------------------------------------ workers */

/* 001FD950's draw half: the area bank is AREA11's (the only one exported). */
static int w_draw_line(void *ctx, int global, uint32_t index)
{
    (void)ctx;
    if (!global && em_scene_state()->d810700 != s.area)
        return fail("area message bank of another area");
    return em_message_draw_line(&s.draw, global, index) ? 1 : fail("001FD950 draw");
}

static int w_face_talk(void *ctx, int on)
{
    (void)ctx;
    if (!s.host.face_talk) return fail("001D06E0 face talk has no binding");
    return s.host.face_talk(s.host.context, on) ? 1 : fail("001D06E0 face talk");
}

/* 001FAAC0 on voice lanes 1 and 2: it does nothing for an idle lane, and
 * no voice lane is ever active in the port (voice_push is not bound). */
static int w_stop_lane(void *ctx, int lane)
{
    (void)ctx;
    return lane == 1 || lane == 2 ? 1 : fail("001FAAC0 on a lane other than 1/2");
}

static int w_stream_stop(void *ctx, int32_t mask)
{
    (void)ctx;
    if (!s.streams.stream_stop) return fail("001FD470 has no binding");
    return s.streams.stream_stop(s.streams.context, mask) ? 1 : fail("001FD470");
}

static int w_stream_play(void *ctx, int lane, int32_t cue)
{
    (void)ctx;
    if (!s.streams.stream_play) return fail("001FA790 has no binding");
    return s.streams.stream_play(s.streams.context, lane, cue) ? 1 : fail("001FA790");
}

/* The draw module's workers: 001CBE10 and 001FC7B0. */
static int w_glyph_advance(void *ctx, uint8_t c, int32_t *advance)
{
    (void)ctx;
    *advance = em_message_glyph_advance(c);
    return 1;
}

static int w_draw_text(void *ctx, int32_t x, int32_t y, const uint8_t *text,
                       const EmMessageDrawConfig *cfg)
{
    (void)ctx;
    /* The draw module hands over a NUL-terminated run (its contract). */
    size_t length = strlen((const char *)text);
    return em_message_glyph_fc7b0(&s.glyph, x, y, text, (uint32_t)length + 1u, cfg) == 0;
}

/* 001CC8A0: the texels are the atlas's; the strip keeps the call. */
static int w_upload(void *ctx, const EmMessageGlyphUpload *upload)
{
    (void)ctx;
    if (upload->y != 0 || upload->offset % 30u != 0)
        return fail("001CC8A0 outside the one-row tall strip");
    if (!em_hud_tall_glyph_cell(upload->offset / 30u, NULL))
        return fail("tall glyph missing from assets/font.emfn");
    return 1;
}

/* 001CC3B0: keep the packed passes and the strip for this frame's render. */
static int w_flush(void *ctx, const EmMessageGlyphFlush *flush)
{
    (void)ctx;
    if (s.flush_count >= FRAME_FLUSHES || flush->upload_count > FRAME_UPLOADS - s.upload_count)
        return fail("too many glyph passes in one frame");
    EmMessageGlyphFlush *out = &s.flushes[s.flush_count++];
    *out = *flush;
    memcpy(&s.uploads[s.upload_count], flush->uploads, flush->upload_count * sizeof *flush->uploads);
    out->uploads = &s.uploads[s.upload_count];
    s.upload_count += flush->upload_count;
    return 1;
}

/* ------------------------------------------------------------ data */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void unload(void)
{
    free(s.blob);
    free(s.global_records);
    free(s.area_records);
    free(s.rows);
    s.blob = NULL;
    s.global_records = s.area_records = NULL;
    s.rows = NULL;
    s.loaded = 0;
}

static int load(const char *path)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 140 || size > (1 << 22)) { fclose(f); return 0; }
    s.blob = malloc((size_t)size);
    int ok = s.blob && fread(s.blob, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!ok) return 0;
    const uint8_t *b = s.blob;
    if (memcmp(b, "EMMD", 4) || rd32(b + 4) != 1) return 0;
    uint32_t area = rd32(b + 8), gcount = rd32(b + 12), acount = rd32(b + 16), rows = rd32(b + 20);
    uint32_t gbank = rd32(b + 28), abank = rd32(b + 32);
    if (area == 0 || area > AREA_TABLES || !gcount || !acount || gcount > 4096 ||
        acount > 4096 || rows > 4096 || !gbank || !abank)
        return 0;
    uint64_t need = 140u + 8u * (uint64_t)gcount + 8u * (uint64_t)acount + 16u * (uint64_t)rows +
                    gbank + abank;
    if (need != (uint64_t)size) return 0;
    for (int i = 0; i < 16; i++) s.colors[i] = (int32_t)rd32(b + 36 + 4 * i);
    for (int i = 0; i < 5; i++) {
        s.line_config.word[i] = (int32_t)rd32(b + 100 + 4 * i);
        s.fallback.word[i] = (int32_t)rd32(b + 120 + 4 * i);
    }
    s.line_config.style = &s.style;
    s.fallback.style = NULL;
    const uint8_t *p = b + 140;
    s.global_records = calloc(gcount, sizeof *s.global_records);
    s.area_records = calloc(acount, sizeof *s.area_records);
    s.rows = rows ? calloc(rows, sizeof *s.rows) : NULL;
    if (!s.global_records || !s.area_records || (rows && !s.rows)) return 0;
    for (uint32_t i = 0; i < gcount + acount; i++, p += 8) {
        EmMessageRecord *r = i < gcount ? &s.global_records[i] : &s.area_records[i - gcount];
        r->duration = (uint16_t)(p[0] | p[1] << 8);
        r->voice = (int16_t)(p[2] | p[3] << 8);
        r->slot = p[4];
        r->wait_stream = p[5];
        r->unread[0] = p[6];
        r->unread[1] = p[7];
    }
    for (uint32_t i = 0; i < rows; i++, p += 16) {
        s.rows[i].area = (int32_t)rd32(p);
        s.rows[i].word1 = (int32_t)rd32(p + 4);
        s.rows[i].line = (int32_t)rd32(p + 8);
        s.rows[i].cue = (int32_t)rd32(p + 12);
    }
    memset(s.areas, 0, sizeof s.areas);
    s.areas[area].records = s.area_records;
    s.areas[area].count = acount;
    s.area = area;
    s.data = (EmMessageData){{s.global_records, gcount}, s.areas, AREA_TABLES, s.rows, rows,
                             s.colors[0], rd32(b + 24)};
    s.draw_data = (EmMessageDrawData){{p, gbank}, {p + gbank, abank}, s.colors, 16,
                                      &s.line_config, &s.fallback, &s.style};
    EmMessageWorkers workers = {NULL, w_draw_line, w_face_talk, NULL, w_stop_lane, w_stream_stop,
                                w_stream_play, NULL, NULL, NULL, NULL};
    EmMessageDrawWorkers draw_workers = {NULL, w_glyph_advance, w_draw_text};
    EmMessageGlyphWorkers glyph_workers = {NULL, w_upload, w_flush};
    if (!em_message_init(&s.service, &s.data, &workers) ||
        !em_message_draw_init(&s.draw, &s.draw_data, &draw_workers) ||
        !em_message_glyph_init(&s.glyph, &glyph_workers))
        return 0;
    return 1;
}

/* ------------------------------------------------------------ the service */

static EmMessageShared shared(void)
{
    EmSceneState *scene = em_scene_state();
    /* D_00282155/156: the voice lanes' active bytes; no voice lane runs. */
    EmMessageShared sh = {scene->d810700, scene->spad3B8F, 0, 0,
                          em_scene_req_at(scene, 0x008106F5u), em_scene_req_at(scene, 0x008106F4u),
                          em_scene_req_at(scene, 0x008106D4u)};
    return sh;
}

/* D_00275C50 has one storage, s.style. The service writes its own copy only
 * in 001FC9B0 (colour, 0x80, 0), so a sentinel in that copy before each
 * call shows whether a reset ran, and the reset's three stores are copied
 * into the style block. Bytes +6/+7 are not written by 001FC9B0. */
static void sentinel(void)
{
    s.service.text_glyph = 0;
    s.service.text_flag = 0xFF;
}

static void sync_reset(void)
{
    if (s.service.text_glyph == 0x80 && s.service.text_flag == 0) {
        s.style.color = s.service.text_color;
        s.style.glyph = 0x80;
        s.style.flag = 0;
    }
}

static int ready(void)
{
    if (s.fault) return 0;
    if (!s.loaded) return fail("message data missing (run tools/export_message_data.py)");
    return 1;
}

int em_message_live_tick(void)
{
    s.flush_count = s.upload_count = 0;
    if (!s.installed) return 0;
    if (s.fault) { report(); return -1; }
    if (s.host.gate) {
        int open = s.host.gate(s.host.context);
        if (open < 0) { fail("message gate"); report(); return -1; }
        if (!open) return 0;
    }
    if (!s.loaded) {
        if (s.service.block.phase == 0) return 0;
        ready();
        report();
        return -1;
    }
    EmMessageShared sh = shared();
    sentinel();
    int rc = em_message_tick(&s.service, &sh);
    sync_reset();
    if (rc < 0 || s.fault) {
        fail("001FCA10");
        report();
        return -1;
    }
    return 0;
}

void em_message_live_render(EmGfx *gfx)
{
    for (uint32_t i = 0; i < s.flush_count; i++)
        em_hud_glyph_strip(gfx, &s.flushes[i]);
}

static int frame_tick(void *ctx)
{
    (void)ctx;
    return em_message_live_tick();
}

static void frame_render(void *ctx, EmGfx *gfx)
{
    (void)ctx;
    em_message_live_render(gfx);
}

int em_message_live_install(const char *path)
{
    em_message_live_shutdown();
    s.installed = 1;
    s.loaded = load(path);
    if (!s.loaded) {
        unload();
        fprintf(stderr, "message service: %s missing or malformed; the first message request or "
                        "reset (001FC9B0) will fault (run tools/export_message_data.py)\n",
                path ? path : "(null)");
    }
    static const EmFrameMessageService service = {frame_tick, frame_render, NULL};
    em_frame_set_message_service(&service);
    return s.loaded;
}

void em_message_live_shutdown(void)
{
    if (s.installed) em_frame_set_message_service(NULL);
    unload();
    memset(&s, 0, sizeof s);
}

void em_message_live_set_host(const EmMessageLiveHost *host)
{
    if (host) s.host = *host;
    else memset(&s.host, 0, sizeof s.host);
}

void em_message_live_set_streams(const EmMessageLiveStreams *streams)
{
    if (streams) s.streams = *streams;
    else memset(&s.streams, 0, sizeof s.streams);
}

int em_message_live_op0c(uint8_t *handshake, const unsigned char *record)
{
    if (!ready()) { report(); return -1; }
    sentinel();
    int rc = em_message_op0c(&s.service, handshake, record);
    sync_reset();
    if (rc < 0 || s.fault) { fail("001B7D60"); report(); return -1; }
    return rc;
}

int em_message_live_post(uint32_t line, int32_t delay)
{
    unsigned char record[0x20] = {0};
    for (int i = 0; i < 4; i++) {
        record[0x14 + i] = (unsigned char)(line >> (8 * i));
        record[0x18 + i] = (unsigned char)((uint32_t)delay >> (8 * i));
    }
    uint8_t handshake = 0;          /* sub 0 (+0x08 = 0): post on a fresh handshake */
    return em_message_live_op0c(&handshake, record) < 0 ? -1 : 0;
}

int em_message_live_stream_request(int32_t line)
{
    if (!ready()) { report(); return -1; }
    EmMessageShared sh = shared();
    int rc = em_message_stream_request(&s.service, &sh, line);
    if (rc < 0 || s.fault) { fail("001FD4C0"); report(); return -1; }
    return rc;
}

int32_t em_message_live_stream_cue(int32_t area, int32_t line)
{
    if (!s.loaded) return -1;
    for (uint32_t i = 0; i < s.data.stream_count; i++)
        if (s.rows[i].area == area && s.rows[i].line == line) return s.rows[i].cue;
    return -1;
}

int em_message_live_reset(void)
{
    if (!ready()) { report(); return -1; }
    em_message_reset(&s.service);
    s.style.color = s.service.text_color;
    s.style.glyph = s.service.text_glyph;
    s.style.flag = s.service.text_flag;
    return 0;
}

EmMessageBlock *em_message_live_block(void)
{
    return s.installed ? &s.service.block : NULL;
}

const char *em_message_live_fault(void)
{
    return s.fault;
}
