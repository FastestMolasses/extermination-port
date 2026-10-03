/* em_area_title.c - see em_area_title.h. Nothing here computes: the
 * behaviour is em_sul_001C5930 and every worker is a translation. */
#include "game/em_area_title.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_message_draw_original.h"
#include "game/em_message_live.h"
#include "game/em_status_ui_leftovers.h"

typedef uint32_t u32;

#define D_0024A850 0x0024A850u
#define D_00289B40 0x00289B40u
#define RECORD_BYTES 0x1F8u          /* +0x00..+0x1F7: every byte 001C5930 reads */
#define SIDES 8u
#define BLOCKS_MAX 8u

static struct {
    int tried, loaded;
    u32 fault;
    uint8_t *file;
    EmSulRegion block[BLOCKS_MAX];
    unsigned blocks;
    uint32_t lines;                  /* 001CC1E0 calls made (the tick log) */
    uint8_t d289B40[EM_SUL_AREA_COUNT * 4];
    /* +0x28..+0x2B of the title nodes (the pool record keeps no such
     * field), by record and generation */
    struct { const EmActor *node; uint32_t generation; uint8_t b[4]; } side[SIDES];
} S;

/* The call's context: the node, its worker, and whether it was freed. */
typedef struct {
    EmActor *node;
    EmAreaTitleFree free_node;
    void *ctx;
    int freed;
} Call;

static int fail(u32 address, const char *what)
{
    if (!S.fault) {
        S.fault = address;
        fprintf(stderr, "area title: %s (%08X)\n", what, (unsigned)address);
    }
    return -1;
}

uint32_t em_area_title_fault(void) { return S.fault; }
uint32_t em_area_title_lines(void) { return S.lines; }

static u32 rd32(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

int em_area_title_load(void)
{
    if (S.tried) return S.loaded ? 0 : -1;
    S.tried = 1;
    FILE *f = fopen(EM_AREA_TITLE_EXPORT, "rb");
    long size = -1;
    if (f && fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (f) rewind(f);
    S.file = size >= 16 ? malloc((size_t)size) : NULL;
    const int read_ok = f && S.file && fread(S.file, 1, (size_t)size, f) == (size_t)size;
    if (f) fclose(f);
    if (!read_ok || memcmp(S.file, "EMAT", 4) != 0 || rd32(S.file + 4) != 1u || rd32(S.file + 8) > BLOCKS_MAX) {
        free(S.file);
        S.file = NULL;
        return fail(0x001C5930u, EM_AREA_TITLE_EXPORT " is missing or malformed (run tools/export_area_title.py)");
    }
    S.blocks = rd32(S.file + 8);
    int16_t counts[EM_SUL_AREA_COUNT];
    int have_counts = 0;
    for (unsigned k = 0; k < S.blocks; ++k) {
        const uint8_t *r = S.file + 16u + 16u * k;
        const u32 address = rd32(r), bytes = rd32(r + 4), at = rd32(r + 8);
        if ((uint64_t)at + bytes > (uint64_t)size) {
            free(S.file);
            S.file = NULL;
            return fail(0x001C5930u, EM_AREA_TITLE_EXPORT " has a block outside the file");
        }
        S.block[k] = (EmSulRegion){address, bytes, S.file + at};
        if (address == D_0024A850 && bytes == 2u * EM_SUL_AREA_COUNT) {
            for (unsigned a = 0; a < EM_SUL_AREA_COUNT; ++a)
                counts[a] = (int16_t)(uint16_t)(S.file[at + 2 * a] | S.file[at + 2 * a + 1] << 8);
            have_counts = 1;
        }
    }
    /* 001B0BA0 at boot: D_00289B40 from the counts. */
    if (!have_counts || em_sul_001B0BA0(counts, S.d289B40) < 0) {
        free(S.file);
        S.file = NULL;
        return fail(0x001B0BA0u, EM_AREA_TITLE_EXPORT " holds no D_0024A850 counts");
    }
    S.loaded = 1;
    return 0;
}

/* The bytes of `address` inside an exported block (its text up to the
 * block's end), or NULL. */
static const uint8_t *exported(u32 address, u32 *avail)
{
    for (unsigned k = 0; k < S.blocks; ++k)
        if (address >= S.block[k].base && address - S.block[k].base < S.block[k].size) {
            *avail = S.block[k].size - (address - S.block[k].base);
            return S.block[k].bytes + (address - S.block[k].base);
        }
    return NULL;
}

/* ---- the workers ---------------------------------------------------- */

/* 001CC170(text) */
static int w_text_width(void *context, uint32_t text, int32_t *width)
{
    (void)context;
    u32 avail = 0;
    const uint8_t *t = exported(text, &avail);
    if (!t) return fail(0x001CC170u, "001CC170 of a string outside the export");
    return em_message_live_cc170(t, avail, width) < 0 ? fail(0x001CC170u, "001CC170 faulted") : 0;
}

/* 001CC1E0(slot, x, y, w, h, text, style): style 0 is none; D_00265520's
 * record is the exported bytes (+0 colour, +4 glyph, +5 flag). */
static int w_text_proportional(void *context, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h,
                               uint32_t text, uint32_t style)
{
    (void)context;
    u32 avail = 0, style_avail = 0;
    const uint8_t *t = exported(text, &avail);
    if (!t) return fail(0x001CC1E0u, "001CC1E0 of a string outside the export");
    EmMessageTextStyle st, *sp = NULL;
    if (style) {
        const uint8_t *b = exported(style, &style_avail);
        if (!b || style_avail < sizeof st) return fail(0x001CC1E0u, "001CC1E0 of a style outside the export");
        memset(&st, 0, sizeof st);
        st.color = (int32_t)rd32(b);
        st.glyph = b[4];
        st.flag = b[5];
        st.pad[0] = b[6];
        st.pad[1] = b[7];
        sp = &st;
    }
    S.lines++;
    return em_message_live_cc1e0(slot, x, y, w, h, t, avail, sp) < 0 ? fail(0x001CC1E0u, "001CC1E0 faulted")
                                                                     : 0;
}

/* 001AFC10(node) */
static int w_free(void *context, uint8_t *actor)
{
    (void)actor;
    Call *c = context;
    c->freed = 1;
    return c->free_node(c->ctx, c->node) < 0 ? -1 : 0;
}

/* ---- the node ------------------------------------------------------- */

static uint8_t *side_of(const EmActor *node, int create)
{
    unsigned empty = SIDES;
    for (unsigned k = 0; k < SIDES; ++k) {
        if (S.side[k].node == node) {
            if (S.side[k].generation == node->generation) return S.side[k].b;
            empty = k;                                 /* a stale record's slot */
            break;
        }
        if (!S.side[k].node && empty == SIDES) empty = k;
    }
    if (!create || empty == SIDES) return NULL;
    S.side[empty].node = node;
    S.side[empty].generation = node->generation;
    memset(S.side[empty].b, 0, sizeof S.side[empty].b);
    return S.side[empty].b;
}

static void record_bytes(const EmActor *node, const uint8_t *side, uint8_t r[RECORD_BYTES])
{
    memset(r, 0, RECORD_BYTES);
    r[0x04] = node->u04[0];
    r[0x05] = node->u04[1];
    r[0x06] = node->u04[2];
    memcpy(r + 0x28, side, 4);
    memcpy(r + 0x1F0, node->scratch, RECORD_BYTES - 0x1F0u);
}

int em_area_title_001C5930(EmActor *node, const EmSceneState *scene, float infection,
                           EmAreaTitleFree free_node, void *ctx)
{
    if (S.fault) return -1;
    if (!node || !scene || !free_node) return fail(0x001C5930u, "no node, scene or 001AFC10");
    if (em_area_title_load() < 0) return -1;
    uint8_t *side = side_of(node, 1);
    if (!side) return fail(0x001C5930u, "more live title nodes than the port keeps");
    uint8_t r[RECORD_BYTES];
    record_bytes(node, side, r);
    const uint8_t area[2] = {scene->d810700, scene->d810701};
    const uint8_t b8 = scene->req[EM_SCENE_REQ_B8], mode = scene->spad3B8D;
    uint8_t inf[4];
    memcpy(inf, &infection, 4);
    EmSulRegion regions[BLOCKS_MAX + 5] = {
        {0x00810700u, 2, area},
        {0x008106B8u, 1, &b8},
        {0x008104D8u, 4, inf},
        {D_00289B40, sizeof S.d289B40, S.d289B40},
        {0x70003B8Du, 1, &mode},
    };
    unsigned n = 5;
    for (unsigned k = 0; k < S.blocks; ++k) regions[n++] = S.block[k];
    const EmSulMemory mem = {regions, n};
    Call call = {node, free_node, ctx, 0};
    EmSulWorkers w;
    memset(&w, 0, sizeof w);
    w.context = &call;
    w.text_width = w_text_width;
    w.text_proportional = w_text_proportional;
    w.free_actor = w_free;
    if (em_sul_001C5930(&w, &mem, r, sizeof r) < 0) return fail(0x001C5930u, "001C5930 faulted");
    if (call.freed) {
        memset(side, 0, 4);
        for (unsigned k = 0; k < SIDES; ++k)
            if (S.side[k].b == side) S.side[k].node = NULL;
        return 0;
    }
    node->u04[0] = r[0x04];
    node->u04[1] = r[0x05];
    node->u04[2] = r[0x06];
    memcpy(side, r + 0x28, 4);
    memcpy(node->scratch, r + 0x1F0, RECORD_BYTES - 0x1F0u);
    return 1;
}

int em_area_title_record(const EmActor *node, EmAreaTitleRecord *out)
{
    const uint8_t *side = node && out ? side_of(node, 0) : NULL;
    if (!side) return 0;
    out->state = node->u04[0];
    out->title_phase = node->u04[1];
    out->band_phase = node->u04[2];
    out->title_timer = (int16_t)(uint16_t)(side[0] | side[1] << 8);
    out->title_index = (int16_t)(uint16_t)(side[2] | side[3] << 8);
    out->band = (int32_t)rd32(node->scratch);
    out->band_timer = (int16_t)(uint16_t)(node->scratch[4] | node->scratch[5] << 8);
    return 1;
}
