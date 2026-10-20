/* em_status_gs_page.c - see em_status_gs_page.h. */
#include "game/em_status_gs_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_battery_ui.h"
#include "game/em_frame.h"
#include "game/em_message_glyph_original.h"
#include "game/em_message_live.h"
#include "game/em_player_stage_workers.h"
#include "game/em_render_context_live.h"
#include "game/em_status_background.h"
#include "game/em_ui_font_raw.h"
#include "game/em_ui_packet_original.h"

#define D_00275670 0x00275670u
#define D_00275674 0x00275674u
#define ARENA 0x0028F700u
#define SLOT1_BASE (ARENA + 0xC9000u) /* 001D1AE0's +0x14 cursor for index 0 */
#define SLOT1_SIZE 0x95760u           /* the index stride: slot 2's buffer follows */

static struct {
    int installed;
    EmUfrFonts fonts;
    uint16_t nibbles[16];
    EmMessageGlyph glyph;  /* the page text's own 001CC1E0 runs */
    uint64_t bg_tex0;      /* the background call's TEX0 */
    const char *fault;
    int reported;
    unsigned long packets;
    uint32_t frame_start;
    int frame_page, frame_any;
    FILE *dump;
    unsigned dumped;
} G;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int word(uint32_t address, uint32_t *out)
{
    const uint8_t *p = em_rcl_bytes(address, 4);
    if (!p) return 0;
    *out = rd32(p);
    return 1;
}

static int fault(const char *why, const char *detail)
{
    if (!G.fault) G.fault = why;
    if (!G.reported) {
        G.reported = 1;
        fprintf(stderr, "status GS page: fault: %s%s%s\n", why, detail ? ": " : "", detail ? detail : "");
    }
    return 0;
}

/* The bytes a builder may touch: the slot-1 cursor word and the frame's
 * slot-1 buffer (the frame head's index, context +0x9C). */
static uint8_t *bytes(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    uint32_t context, index;
    if (!word(D_00275670, &context) || !word(context + 0x9Cu, &index) || index > 1u) return NULL;
    if (address == context + 0x14u && size == 4u) return em_rcl_bytes_mut(address, size);
    const uint32_t base = SLOT1_BASE + index * SLOT1_SIZE;
    if (address < base || size > SLOT1_SIZE || address - base > SLOT1_SIZE - size) return NULL;
    return em_rcl_bytes_mut(address, size);
}

static int to_int(void *ctx, uint32_t bits, int32_t *result)
{
    (void)ctx;
    *result = em_player_float_to_int(bits);
    return 1;
}

static int active(void)
{
    return G.installed && em_rcl_bound();
}

/* A fresh EmUip over the live context for one call. */
static int uip(EmUip *u)
{
    uint32_t context, blocks;
    if (!word(D_00275670, &context) || !word(D_00275674, &blocks)) return fault("D_00275670 / D_00275674 unmapped", NULL);
    static const EmUipMemory mem = {NULL, bytes, to_int};
    if (!em_uip_init(u, &mem, context, blocks)) return fault("em_uip_init", NULL);
    u->nibbles = G.nibbles;
    u->font_tall = G.fonts.tall;
    u->font_tall_size = EM_UFR_TALL_BYTES;
    u->font_small = G.fonts.small;
    u->font_small_size = EM_UFR_SMALL_BYTES;
    return 1;
}

static int done(const EmUip *u, int rc, const char *builder)
{
    if (rc < 0) return fault(builder, u->fault);
    G.packets++;
    return 1;
}

/* An EmMessageTextStyle from the 8 bytes of a style record. */
static EmMessageTextStyle style_of(uint64_t record)
{
    EmMessageTextStyle s;
    s.color = (int32_t)(uint32_t)record;
    s.glyph = (uint8_t)(record >> 32);
    s.flag = (uint8_t)(record >> 40);
    s.pad[0] = (uint8_t)(record >> 48);
    s.pad[1] = (uint8_t)(record >> 56);
    return s;
}

/* ---- the glyph runs' workers: 001CC8A0(1, ...) and 001CC3B0 ------------- */

static int glyph_upload(void *ctx, const EmMessageGlyphUpload *up)
{
    (void)ctx;
    if (G.fault) return 0;
    if (!active()) return 1;
    EmUip u;
    if (!uip(&u)) return 0;
    G.frame_any = 1;
    return done(&u, em_uip_001CC8A0(&u, 1, up->x, up->y, (int32_t)up->offset), "001CC8A0");
}

static int glyph_flush(void *ctx, const EmMessageGlyphFlush *f)
{
    (void)ctx;
    if (G.fault) return 0;
    if (!active()) return 1;
    EmUip u;
    if (!uip(&u)) return 0;
    G.frame_any = 1;
    return done(&u, em_uip_001CC3B0(&u, f->slot, f->x, f->y, f->u_end, f->v_end, f->width, f->height, f->style),
                "001CC3B0");
}

static const EmMessageGlyphWorkers k_glyph_sink = {NULL, glyph_upload, glyph_flush};

/* ---- the BATTERY page's leaves (em_battery_ui's sink) -------------------- */

static int bg_mode(void *ctx, int slot, int mode)
{
    (void)ctx;
    EmUip u;
    if (!uip(&u)) return 0;
    return done(&u, em_uip_00207D00(&u, slot, mode), "00207D00 (0020A7A0)");
}

static int bg_sprite(void *ctx, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba)
{
    (void)ctx;
    EmUip u;
    if (!uip(&u)) return 0;
    return done(&u, em_uip_00207E40(&u, 1, x, y, w, h, rgba, G.bg_tex0), "00207E40 (0020A7A0)");
}

static int s_background(void *ctx, uint64_t tex0, uint32_t *token)
{
    (void)ctx;
    if (G.fault) return 0;
    static const EmStatusBackgroundEmit emit = {NULL, bg_mode, bg_sprite};
    const int on = active();
    G.bg_tex0 = tex0;
    if (on) G.frame_page = 1;
    if (em_status_background_live_step(on ? &emit : NULL, token) != 1)
        return G.fault ? 0 : fault("0020A7A0 at its call time", NULL);
    return 1;
}

static int s_draw_background(void *ctx, EmGfx *gfx, uint32_t token, float u, float v, float w, float h)
{
    (void)ctx;
    return em_status_background_render_step(gfx, token, u, v, w, h) == 1 ? 1
                                                                          : fault("0020A7A0's sprites (render)", NULL);
}

static int s_blend(void *ctx, int32_t slot, int32_t mode)
{
    (void)ctx;
    if (G.fault) return 0;
    if (!active()) return 1;
    EmUip u;
    if (!uip(&u)) return 0;
    G.frame_page = 1;
    return done(&u, em_uip_00207D00(&u, slot, mode), "00207D00");
}

static int s_sprite(void *ctx, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba,
                    uint64_t tex0)
{
    (void)ctx;
    if (G.fault) return 0;
    if (!active()) return 1;
    EmUip u;
    if (!uip(&u)) return 0;
    G.frame_page = 1;
    return done(&u, em_uip_00207E40(&u, slot, x, y, w, h, rgba, tex0), "00207E40");
}

static int s_rectangle(void *ctx, int32_t slot, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgba)
{
    (void)ctx;
    if (G.fault) return 0;
    if (!active()) return 1;
    EmUip u;
    if (!uip(&u)) return 0;
    G.frame_page = 1;
    return done(&u, em_uip_00207F80(&u, slot, x0, y0, x1, y1, rgba), "00207F80");
}

/* 00209280's text: 001CBA50(1, x, y, w, h, text, style) or 001CC1E0(1, x,
 * y, w, h, text, style) over the style record's bytes. */
static int s_text(void *ctx, int proportional, int32_t x, int32_t y, int32_t w, int32_t h, const char *text,
                  uint64_t style)
{
    (void)ctx;
    if (G.fault) return 0;
    if (!active()) return 1;
    if (!text) return fault("00209280's text missing", NULL);
    const EmMessageTextStyle st = style_of(style);
    const uint32_t avail = (uint32_t)strlen(text) + 1u;
    G.frame_page = 1;
    if (proportional) {
        if (em_message_glyph_cc1e0(&G.glyph, 1, x, y, w, h, (const uint8_t *)text, avail, &st) < 0)
            return G.fault ? 0 : fault("001CC1E0", G.glyph.fault);
        return 1;
    }
    EmUip u;
    if (!uip(&u)) return 0;
    const int rc = em_uip_001CBA50(&u, 1, x, y, w, h, (const uint8_t *)text, avail, &st);
    if (rc < 0) return fault("001CBA50", u.fault);
    G.packets++;
    return 1;
}

static const EmBatteryUISink k_battery_sink = {NULL,     s_background, s_draw_background, s_blend,
                                               s_sprite, s_rectangle,  s_text};

/* ---- install, frame hooks ------------------------------------------------- */

int em_sgp_install(const char *font_path)
{
    const char *why = NULL;
    memset(&G, 0, sizeof G);
    if (em_ufr_load(font_path, &G.fonts, &why) != 0) {
        fprintf(stderr, "status GS page: %s: %s (the decomp's tools/export_font.py writes it)\n",
                font_path ? font_path : "(null)", why ? why : "?");
        return -1;
    }
    em_ufr_nibbles(G.nibbles);
    if (!em_message_glyph_init(&G.glyph, &k_glyph_sink)) {
        fprintf(stderr, "status GS page: the glyph runs cannot be bound\n");
        return -1;
    }
    const char *dump = getenv("EM_STATUS_GS_DUMP");
    if (dump && *dump) {
        G.dump = fopen(dump, "wb");
        if (!G.dump) {
            fprintf(stderr, "status GS page: cannot write %s\n", dump);
            return -1;
        }
    }
    G.installed = 1;
    em_battery_ui_set_sink(&k_battery_sink);
    em_message_live_set_glyph_sink(&k_glyph_sink);
    return 0;
}

static int slot1_cursor(uint32_t *cursor)
{
    uint32_t context;
    return word(D_00275670, &context) && word(context + 0x14u, cursor);
}

void em_sgp_frame_begin(void)
{
    G.frame_page = G.frame_any = 0;
    if (!G.installed || !em_rcl_bound() || !slot1_cursor(&G.frame_start)) G.frame_start = 0;
}

static void put32(FILE *f, uint32_t v)
{
    const uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}

void em_sgp_frame_end(void)
{
    uint32_t end;
    if (!G.dump || G.dumped >= EM_SGP_DUMP_FRAMES || !(G.frame_page || G.frame_any) || !G.frame_start ||
        !slot1_cursor(&end) || end < G.frame_start)
        return;
    const uint8_t *p = end > G.frame_start ? em_rcl_bytes(G.frame_start, end - G.frame_start) : NULL;
    if (end > G.frame_start && !p) return;
    fwrite("SGPF", 1, 4, G.dump);
    put32(G.dump, em_frame_counter());
    put32(G.dump, G.frame_start);
    put32(G.dump, end);
    put32(G.dump, G.frame_page ? 1u : 0u);
    if (p) fwrite(p, 1, end - G.frame_start, G.dump);
    fflush(G.dump);
    G.dumped++;
}

const char *em_sgp_fault(void)
{
    return G.fault;
}

unsigned long em_sgp_packets(void)
{
    return G.packets;
}
