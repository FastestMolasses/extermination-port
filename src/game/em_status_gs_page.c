/* em_status_gs_page.c - see em_status_gs_page.h. */
#include "game/em_status_gs_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_gfx.h"
#include "em_settings.h"
#include "game/em_battery_ui.h"
#include "game/em_frame.h"
#include "game/em_message_glyph_original.h"
#include "game/em_message_live.h"
#include "game/em_player_stage_workers.h"
#include "game/em_render_context_live.h"
#include "game/em_status_background.h"
#include "game/em_status_pages_live.h"
#include "game/em_status_runtime.h"
#include "game/em_ui_font_raw.h"
#include "game/em_ui_packet_original.h"
#include "gs/em_gs_world.h"

#define D_00275670 0x00275670u
#define D_00275674 0x00275674u
#define ARENA 0x0028F700u
#define SLOT1_BASE (ARENA + 0xC9000u) /* 001D1AE0's +0x14 cursor for index 0 */
#define SLOT1_SIZE 0x95760u           /* the index stride: slot 2's buffer follows */

_Static_assert(sizeof(EmStatusBackground) == 0x60, "D_002655A0 is three 0x20-byte blocks");

static struct {
    int installed;
    EmUfrFonts fonts;
    uint16_t nibbles[16];
    EmMessageGlyph glyph;  /* the page text's own 001CC1E0 runs */
    uint64_t bg_tex0;      /* the background call's TEX0 */
    EmStatusBackground bg_before; /* D_002655A0 before the frame's 0020A7A0 (the dump) */
    int bg_seen;
    const char *fault;
    int reported;
    unsigned long packets;
    uint32_t frame_start;
    int frame_page, frame_any, frame_item, frame_pages; /* a page's packets, any packets, the ITEM
                                                          * root's, a bound status page's */
    FILE *dump;
    unsigned dumped;
    /* the GS field of the bound pages (Original profile) */
    int field_on;          /* the GS frame draws the bound pages */
    int field;             /* this frame is drawn by the GS model */
    uint8_t *vif, *gif;    /* the kicked list's VIF stream and its GIF data */
    size_t vif_cap, gif_cap;
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
    if (!G.bg_seen) {
        em_status_background_live_state(&G.bg_before);
        G.bg_seen = 1;
    }
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

/* ---- the ITEM root's leaves (em_status_runtime's item sink) --------------
 * 0020F170 and 0020F2A0 call 0020A7A0, 00207D00 and 00207E40 as the BATTERY
 * page does (the functions above), and 0020AC70 its sixteen 00208AB0(pos,
 * colour, D_00273580, angle) = 001D66A0(1, ...) fans. The colour quadword
 * is 0020AC70's int[3] on its stack plus the word above it, which
 * 0020AC70 never writes (its frame's top word, sp + 0x5C). Measured:
 * every captured fan holds 0 there (test_ui_packet_reference's decode
 * asserts it), so 0 is passed. Why it is 0 is a hypothesis only (the top
 * word of a 128-bit $ra an earlier callee at the same depth saved;
 * STATUS_FIELD.md 3e). It is the centre vertex's RGBAQ alpha: blend mode 1
 * (additive, FIX 0x80) does not use it for the colour, but the
 * interpolated alpha is written to the framebuffer's alpha bits, so a
 * nonzero value would change GS local memory. D_00273580, the rim's
 * colour, is sixteen zero bytes of the ELF's data that nothing writes. */
static int s_fan(void *ctx, const float pos[4], int32_t colour, float angle, const float trig[4])
{
    (void)ctx;
    (void)angle; /* trig holds the worker results 001D66A0 computes from it */
    if (G.fault) return 0;
    if (!active()) return 1;
    EmUip u;
    if (!uip(&u)) return 0;
    uint32_t a1[4], t[4];
    memcpy(a1, pos, sizeof a1);
    memcpy(t, trig, sizeof t);
    const uint32_t a2[4] = {(uint32_t)colour, (uint32_t)colour, (uint32_t)colour, 0u};
    static const uint32_t k_d00273580[4] = {0u, 0u, 0u, 0u};
    G.frame_page = 1;
    return done(&u, em_uip_001D66A0(&u, 1, a1, a2, k_d00273580, t), "001D66A0 (0020AC70)");
}

static int i_background(void *ctx, uint64_t tex0, uint32_t *token)
{
    G.frame_item = 1;
    return s_background(ctx, tex0, token);
}

static int i_blend(void *ctx, int32_t slot, int32_t mode)
{
    G.frame_item = 1;
    return s_blend(ctx, slot, mode);
}

static int i_sprite(void *ctx, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba,
                    uint64_t tex0)
{
    G.frame_item = 1;
    return s_sprite(ctx, slot, x, y, w, h, rgba, tex0);
}

static const EmStatusItemSink k_item_sink = {NULL, i_background, i_blend, i_sprite, s_fan};

/* ---- the bound status pages' leaves (em_status_pages_live's sink) --------
 * EQUIPMENT 00214570 and EVENT 00215870 (STATUS_FIELD.md 3f): 0020A7A0,
 * 00207D00, 00207E40, 00207F80 (0020CCB0's marker) and the text runs
 * 001CBA50 / 001CC1E0 (0020B210's rows, the description) as the BATTERY
 * page's above. */
static int p_background(void *ctx, uint64_t tex0, uint32_t *token)
{
    G.frame_pages = 1;
    return s_background(ctx, tex0, token);
}

static int p_blend(void *ctx, int32_t slot, int32_t mode)
{
    G.frame_pages = 1;
    return s_blend(ctx, slot, mode);
}

static int p_sprite(void *ctx, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba,
                    uint64_t tex0)
{
    G.frame_pages = 1;
    return s_sprite(ctx, slot, x, y, w, h, rgba, tex0);
}

static int p_rectangle(void *ctx, int32_t slot, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgba)
{
    G.frame_pages = 1;
    return s_rectangle(ctx, slot, x0, y0, x1, y1, rgba);
}

/* 001CBA50(1, x, y, w, h, text, style) or 001CC1E0(1, ...): s_text (the
 * BATTERY page's) over the style record's 8 bytes. Not reached by the
 * first level's EQUIPMENT / EVENT frames (their text is the message
 * service's glyph runs: STATUS_FIELD.md 3f); a slot other than 1 or no
 * style record (style 0) is a form no capture shows: it faults. */
static int p_text(void *ctx, int proportional, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h,
                  const char *text, const uint8_t *style)
{
    G.frame_pages = 1;
    if (slot != 1 || !style) return fault("a bound page's text run with a slot other than 1 or no style", NULL);
    uint64_t record = 0;
    for (unsigned i = 0; i < 8; ++i) record |= (uint64_t)style[i] << (8 * i);
    return s_text(ctx, proportional, x, y, w, h, text, record);
}

static const EmStatusPagesSink k_pages_sink = {NULL, p_background, p_blend, p_sprite, p_rectangle, p_text};

/* ---- 001CCB10: the glyph strip page and its CLUT ----------------------------
 * The boot's 001CCB10 (decomp src/func_001CCB10.c, func_001CCBD0.c,
 * func_001CCE80.c): 001CCBD0(0x1B00, 0x1B80, 0) sends one 8 x 8 PSMCT32
 * transfer of colour 0 to each GS block 0x1B00..0x1B80, then one of the grey
 * ramp to block 0x1B67 (em_uip_001CCB10_chain builds the kicks). The
 * Original profile's GS model takes them as one upload chain
 * (em_gfx_gs_upload), which writes the same local memory. */
int em_sgp_001CCB10(EmGfx *gfx)
{
    uint8_t *chain = malloc(EM_UIP_001CCB10_CHAIN_BYTES);
    if (!chain) return -1;
    const size_t n = em_uip_001CCB10_chain(chain, EM_UIP_001CCB10_CHAIN_BYTES);
    const int rc = n ? em_gfx_gs_upload(gfx, chain, n) : -1;
    free(chain);
    return rc < 0 ? -1 : 0;
}

/* ---- the bound pages in the GS field (STATUS_FIELD.md section 3d) --------- */

int em_sgp_field_claim(void)
{
    if (!G.field_on || G.fault) return 0;
    if (!G.frame_page) {
        fault("a status page frame (BATTERY, ITEM root, EQUIPMENT, EVENT) whose page built no slot-1 packets",
              NULL);
        return -1;
    }
    G.field = 1;
    return 1;
}

int em_sgp_field_claim_page(void)
{
    G.frame_pages = 1;
    if (!G.field_on || G.fault) return 0;
    G.field = 1;
    return 1;
}

int em_sgp_field_frame(void)
{
    return G.field;
}

/* fault() for the callers that return -1. */
static int refuse(const char *why, const char *detail)
{
    fault(why, detail);
    return -1;
}

static int grow(uint8_t **buf, size_t *cap, size_t need)
{
    if (need <= *cap) return 1;
    size_t c = *cap ? *cap : 0x40000u;
    while (c < need) c *= 2u;
    if (c > (16u << 20)) return 0;
    uint8_t *p = realloc(*buf, c);
    if (!p) return 0;
    *buf = p;
    *cap = c;
    return 1;
}

/* The VIF stream the kicked list sends from tag `chain` (VIF1 source chain,
 * TTE off: CNT, NEXT, REF, REFS, CALL, RET, END; the data at original
 * addresses in the render context's storage), then its GIF data (VIF NOP /
 * FLUSHE / FLUSH / FLUSHA pass, DIRECT / DIRECTHL kept; any other VIF code
 * is not a status page's list). 0, or -1 (reported). */
static int list_gif(uint32_t chain, size_t *gif_bytes)
{
    char at[64];
    uint32_t t = chain, stack[2];
    unsigned depth = 0;
    size_t vn = 0;
    for (unsigned tags = 0;; ++tags) {
        snprintf(at, sizeof at, "tag %08X", (unsigned)t);
        if (tags >= 0x10000u) return refuse("the kicked list does not end", at);
        const uint8_t *tag = em_rcl_bytes(t, 16);
        if (!tag) return refuse("a DMA tag of the kicked list is not in the render context's storage", at);
        const uint64_t lo = (uint64_t)rd32(tag) | (uint64_t)rd32(tag + 4) << 32;
        const uint32_t qwc = (uint32_t)lo & 0xFFFFu, id = (uint32_t)(lo >> 28) & 7u;
        const uint32_t addr = (uint32_t)(lo >> 32) & 0x7FFFFFFFu;
        if (lo >> 63) return refuse("a DMA tag of the kicked list names scratchpad memory", at);
        uint32_t data, next = 0;
        int end = 0;
        switch (id) {
        case 1: data = t + 16u; next = t + 16u + 16u * qwc; break;                 /* CNT */
        case 2: data = t + 16u; next = addr; break;                                /* NEXT */
        case 3: case 4: data = addr; next = t + 16u; break;                        /* REF, REFS */
        case 5:                                                                    /* CALL */
            if (depth >= 2u) return refuse("the kicked list CALLs deeper than two", at);
            stack[depth++] = t + 16u + 16u * qwc;
            data = t + 16u;
            next = addr;
            break;
        case 6:                                                                    /* RET */
            data = t + 16u;
            if (depth) next = stack[--depth]; else end = 1;
            break;
        case 7: data = t + 16u; end = 1; break;                                    /* END */
        default: return refuse("a DMA tag of the kicked list is REFE", at);
        }
        if (qwc) {
            const uint8_t *p = em_rcl_bytes(data, 16u * qwc);
            if (!p) return refuse("data the kicked list sends is not in the render context's storage", at);
            if (!grow(&G.vif, &G.vif_cap, vn + 16u * qwc)) return refuse("the kicked list is too long", at);
            memcpy(G.vif + vn, p, 16u * qwc);
            vn += 16u * qwc;
        }
        if (end) break;
        t = next;
    }
    size_t used = 0;
    if (!grow(&G.gif, &G.gif_cap, vn ? vn : 16u) || em_gs_vif_direct(G.vif, vn, 0, G.gif, G.gif_cap, &used) < 0)
        return refuse("the kicked list sends VIF codes other than NOP / FLUSH / DIRECT (VU1 code)", NULL);
    *gif_bytes = used;
    return 0;
}

int em_sgp_kick(EmGfx *gfx)
{
    if (!G.field) return 0;
    G.field = 0;
    if (G.fault) return -1;
    uint32_t chain = 0, kicks = 0, bank = 0, slot = 0;
    uint64_t frame = 0, scissor = 0;
    int called = 0;
    if (em_rcl_kick(&chain, &kicks) < 0) return refuse("no kicked list (001D2300)", NULL);
    if (em_rcl_kick_called_001E0DF0(&called) < 0 || called)
        return refuse("a status page's list that CALLs the background (001E0DF0)", NULL);
    if (em_rcl_draw_env(&bank, &slot, &frame, &scissor) < 0)
        return refuse("no draw environment for the kicked buffer", NULL);
    size_t bytes = 0;
    if (list_gif(chain, &bytes) < 0) return -1;
    if (em_gfx_gs_gif_frame(gfx, G.gif, bytes, frame, scissor) < 0)
        return refuse("the GS frame refused the status page's list", em_gfx_gs_world_fault(gfx));
    return 0;
}

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
    /* The Original profile's GS frame draws the bound pages; the boot's
     * 001CCB10 then reaches its local memory too. */
    EmGfx *gfx = em_frame_gfx();
    G.field_on = gfx && em_gfx_gs_world_enabled(gfx) && !em_settings()->gpu_renderer;
    if (G.field_on && em_sgp_001CCB10(gfx) != 0) {
        fprintf(stderr, "status GS page: the GS model refused 001CCB10's uploads: %s\n",
                em_gfx_gs_world_fault(gfx) ? em_gfx_gs_world_fault(gfx) : "?");
        return -1;
    }
    G.installed = 1;
    em_battery_ui_set_sink(&k_battery_sink);
    em_status_runtime_set_item_sink(&k_item_sink);
    em_status_pages_live_set_sink(&k_pages_sink);
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
    G.frame_page = G.frame_any = G.frame_item = G.frame_pages = G.bg_seen = 0;
    G.field = 0;
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
    if (!G.dump || G.dumped >= EM_SGP_DUMP_FRAMES || !(G.frame_page || G.frame_any || G.frame_pages) ||
        !G.frame_start ||
        !slot1_cursor(&end) || end < G.frame_start)
        return;
    const uint8_t *p = end > G.frame_start ? em_rcl_bytes(G.frame_start, end - G.frame_start) : NULL;
    if (end > G.frame_start && !p) return;
    fwrite("SGPF", 1, 4, G.dump);
    put32(G.dump, em_frame_counter());
    put32(G.dump, G.frame_start);
    put32(G.dump, end);
    put32(G.dump, (G.frame_item ? 2u : G.frame_pages ? 8u : G.frame_page ? 1u : 0u) | (G.bg_seen ? 4u : 0u));
    if (p) fwrite(p, 1, end - G.frame_start, G.dump);
    if (G.bg_seen) fwrite(&G.bg_before, 1, sizeof G.bg_before, G.dump);
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
