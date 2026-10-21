/* options_card_live_fixture.c - the options binding (em_options_live) on
 * the card screen past the slot choice, for tools/test_options_card_live.py
 * (docs/SAVE_GAME.md section 6, docs/OPTIONS.md section 4).
 *
 * A shared library the test loads with ctypes: the status pages' EMSP (the
 * GS memory with the card screen's module 0x2A applied and the .data
 * windows), the message service's data and the area-title export are the
 * assets the game loads; the host workers accept every call (the fades and
 * the loader are outside this test). The binding's draw calls (logged
 * where it hands them to em_page_draw / em_message_live:
 * em_options_live_set_trace) and the cues this host's sound receives are
 * logged for the test to compare with the original's. Each frame is one em_options_live_00225AC0
 * call over storage the test owns. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game/em_gs_texture.h"
#include "game/em_hud.h"
#include "game/em_memcard.h"
#include "game/em_message_live.h"
#include "game/em_message_service.h"
#include "game/em_options_live.h"
#include "game/em_scene_state.h"

/* The boundaries a game build supplies from em_hud / em_scene_bindings
 * (the glyph strips are drawn by the game; nothing here renders). */
EmSceneState *em_scene_state(void) { static EmSceneState state; return &state; }
void em_hud_decor_invalidate(void) {}
int em_hud_tall_glyph_cell(uint32_t index, EmHudGlyphCell *cell) { (void)index; (void)cell; return 1; }
void em_hud_glyph_strip(EmGfx *gfx, const EmMessageGlyphFlush *flush) { (void)gfx; (void)flush; }

static EmGsTexture *gs;
static EmOptionsLive *live;
static EmMessageBlock block;
static unsigned cues, fades;

/* The frame's draw and cue log: the binding's draw records
 * (em_options_live_set_trace) and the cues this host's sound receives, in
 * call order; per entry a callee word, a size word and the record; a NULL
 * record (the binding's record overflowed) is logged with size 0xFFFFFFFF. */
static uint8_t trace_buf[1u << 16];
static uint32_t trace_len;
static int trace_full;

static void trace_put(const void *p, uint32_t n)
{
    if (trace_len + n > sizeof trace_buf) {
        trace_full = 1;
        return;
    }
    memcpy(trace_buf + trace_len, p, n);
    trace_len += n;
}

static void trace(void *c, uint32_t callee, const uint8_t *record, uint32_t size)
{
    (void)c;
    const uint32_t head[2] = {callee, record ? size : 0xFFFFFFFFu};
    trace_put(head, sizeof head);
    if (record) trace_put(record, size);
}

/* The host's sound (001FB9F0 as 0020CD40 / 60 / A0 call it): logged here,
 * where the cue arrives, as callee 001FB9F0 with id, a1, a2, a3. */
static int sound(void *c, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    (void)c;
    const uint32_t head[2] = {0x001FB9F0u, 16u};
    const int32_t cue[4] = {id, a1, a2, a3};
    trace_put(head, sizeof head);
    trace_put(cue, sizeof cue);
    ++cues;
    return 0;
}
static int one(void *c) { (void)c; return 1; }
static int module_load(void *c, uint32_t m) { (void)c; (void)m; return 1; }
static int fade(void *c, int out, int32_t a0, int32_t a1) { (void)c; (void)out; (void)a0; (void)a1; ++fades; return 1; }
static int draw_context(void *c, int32_t a0, int32_t a1) { (void)c; (void)a0; (void)a1; return 1; }
static int rumble(void *c, int32_t a, int32_t b, int32_t d, int32_t f) { (void)c; (void)a; (void)b; (void)d; (void)f; return 1; }

/* 1 ready, 0 an asset is missing. */
int fx_init(const char *emsp, const char *message)
{
    if (live) return 1;
    gs = em_gs_texture_load(emsp);
    if (!gs || em_gs_texture_apply(gs, 0x2A) != 1) {
        fprintf(stderr, "fixture: %s (module 0x2A)\n", emsp);
        return 0;
    }
    if (em_message_live_install(message) != 1) return 0;
    static const EmOptionsHost host = {NULL, sound, module_load, one, fade, one, one, draw_context, rumble};
    live = em_options_live_create(gs, &host);
    if (live) em_options_live_set_trace(live, trace, NULL);
    return live != NULL;
}

unsigned fx_message_size(void) { return (unsigned)sizeof(EmMessageBlock); }

/* The card's host folder and the card helpers' globals as the image holds
 * them (the 0x18 + 0x0C + 0x08 bytes of D_00275C58, D_00264E30, D_00275840). */
int fx_card(const char *root, const uint8_t *c58, const uint8_t *e30, const uint8_t *s840)
{
    em_memcard_reset();
    em_memcard_set_root(root);
    if (em_memcard_boot_check() != 1) return 0;
    memcpy(em_memcard_globals(0x00275C58u, 0x18), c58, 0x18);
    memcpy(em_memcard_globals(0x00264E30u, 0x0C), e30, 0x0C);
    memcpy(em_memcard_globals(0x00275840u, 0x08), s840, 0x08);
    return 1;
}

int fx_globals(uint8_t *c58, uint8_t *e30, uint8_t *s840)
{
    memcpy(c58, em_memcard_globals(0x00275C58u, 0x18), 0x18);
    memcpy(e30, em_memcard_globals(0x00264E30u, 0x0C), 0x0C);
    memcpy(s840, em_memcard_globals(0x00275840u, 0x08), 0x08);
    return 1;
}

const char *fx_memcard_fault(void) { return em_memcard_fault(); }

/* One frame: 00225AC0(mode) over the test's storage. `message` holds
 * sizeof(EmMessageBlock) bytes (copied in and out). 0 and *result, or -1
 * on a fault. */
int fx_frame(uint8_t *settings, uint32_t task_address, uint8_t *task, uint8_t *mc, uint8_t *progress,
             uint8_t *message, uint8_t *busy, uint16_t *masks, int16_t *offset, uint8_t read_phase,
             int16_t fade_word, uint16_t held, uint16_t pressed, uint16_t repeat, uint16_t pad_mode,
             uint8_t pad_phase, uint8_t b90, uint8_t b93, uint32_t mode, uint32_t *result)
{
    memcpy(&block, message, sizeof block);
    trace_len = 0;
    trace_full = 0;
    EmOptionsFrame f = {
        .settings = settings, .task_address = task_address, .task = task, .mc = mc, .progress = progress,
        .message = &block, .busy = busy, .masks = masks, .offset = offset, .read_phase = read_phase,
        .fade = fade_word, .held = held, .pressed = pressed, .repeat = repeat, .pad_mode = pad_mode,
        .pad_phase = pad_phase, .spad3B90 = b90, .spad3B93 = b93,
    };
    const int rc = em_options_live_00225AC0(live, &f, mode, result);
    memcpy(message, &block, sizeof block);
    em_message_live_render(NULL); /* the frame's glyph passes (drawn by the game at step F) */
    return rc;
}

/* The binding's own card buffers by original address (docs/OPTIONS.md 4.1). */
int fx_read(uint32_t address, uint8_t *dst, uint32_t size)
{
    return em_options_live_card_bytes(live, address, dst, size);
}

int fx_seed(uint32_t address, const uint8_t *src, uint32_t size)
{
    return em_options_live_card_seed(live, address, src, size);
}

unsigned fx_calls(uint32_t callee) { return em_options_live_calls(live, callee); }
unsigned fx_cues(void) { return cues; }

/* The last frame's draw and cue log (see trace): its length, or -1 when it
 * did not fit (the fixture's or the caller's buffer). */
int fx_trace(uint8_t *dst, uint32_t cap)
{
    if (trace_full || trace_len > cap) return -1;
    memcpy(dst, trace_buf, trace_len);
    return (int)trace_len;
}
