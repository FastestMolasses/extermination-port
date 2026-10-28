/* Native fail-stop contract of em_message_glyph_original (docs/MESSAGE_GLYPH.md).
 * The original-instruction comparison is tools/test_message_glyph_reference.py;
 * this file checks only what the native module adds: missing or failing
 * workers, unterminated text, an overlong run, a missing config and the
 * latched fault, for the tall font and the small font (001CBA50).
 * Synthetic inputs only. */
#include "game/em_message_glyph_original.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); exit(1); } checks++; } while (0)

static int uploads, flushes, fail_upload, fail_flush;
static EmMessageGlyphFlush last;

static int on_upload(void *ctx, const EmMessageGlyphUpload *u)
{
    (void)ctx; (void)u;
    uploads++;
    return !fail_upload;
}

static int on_flush(void *ctx, const EmMessageGlyphFlush *f)
{
    (void)ctx;
    flushes++;
    last = *f;
    return !fail_flush;
}

static int small_flushes;
static EmMessageGlyphSmallFlush small_last;

static int on_small_flush(void *ctx, const EmMessageGlyphSmallFlush *f)
{
    (void)ctx;
    small_flushes++;
    small_last = *f;
    return !fail_flush;
}

static void fresh_small(EmMessageGlyphSmall *g, int with_upload, int with_flush)
{
    EmMessageGlyphSmallWorkers w = {NULL, with_upload ? on_upload : NULL, with_flush ? on_small_flush : NULL};
    uploads = small_flushes = fail_upload = fail_flush = 0;
    CHECK(em_message_glyph_small_init(g, &w) == 1);
}

static void small_font(void)
{
    EmMessageGlyphSmall g;
    const uint8_t text[] = "12";
    const uint8_t open_text[2] = {'1', '2'};
    CHECK(em_message_glyph_small_init(NULL, NULL) == 0);
    CHECK(em_message_glyph_small_init(&g, NULL) == 0);

    /* Two cells, one flush with both uploads and the default colour. */
    fresh_small(&g, 1, 1);
    CHECK(em_message_glyph_cba50(&g, 1, 0x7AF, 0x7F3, 0xC, 0xC, text, sizeof text, NULL) == 0 && !g.fault);
    CHECK(uploads == 2 && small_flushes == 1 && small_last.upload_count == 2);
    CHECK(small_last.width == 0x18 && small_last.u_end == 0x20 && small_last.v_end == 0x10);
    CHECK((uint32_t)small_last.rgbaq == 0x80808080u);

    /* Unterminated text, missing or failing workers: the fault latches. */
    fresh_small(&g, 1, 1);
    CHECK(em_message_glyph_cba50(&g, 1, 0, 0, 0xC, 0xC, open_text, sizeof open_text, NULL) == -1 && g.fault);
    CHECK(em_message_glyph_cba50(&g, 1, 0, 0, 0xC, 0xC, text, sizeof text, NULL) == -1);
    fresh_small(&g, 0, 1);
    CHECK(em_message_glyph_cba50(&g, 1, 0, 0, 0xC, 0xC, text, sizeof text, NULL) == -1 && g.fault);
    fresh_small(&g, 1, 0);
    CHECK(em_message_glyph_cba50(&g, 1, 0, 0, 0xC, 0xC, text, sizeof text, NULL) == -1 && g.fault);
    fresh_small(&g, 1, 1);
    fail_upload = 1;
    CHECK(em_message_glyph_cba50(&g, 1, 0, 0, 0xC, 0xC, text, sizeof text, NULL) == -1 && g.fault);
    fresh_small(&g, 1, 1);
    fail_flush = 1;
    CHECK(em_message_glyph_cba50(&g, 1, 0, 0, 0xC, 0xC, text, sizeof text, NULL) == -1 && g.fault);
    CHECK(em_message_glyph_cba50(NULL, 1, 0, 0, 0xC, 0xC, text, sizeof text, NULL) == -1);
}

static void fresh(EmMessageGlyph *g, int with_upload, int with_flush)
{
    EmMessageGlyphWorkers w = {NULL, with_upload ? on_upload : NULL, with_flush ? on_flush : NULL};
    uploads = flushes = fail_upload = fail_flush = 0;
    CHECK(em_message_glyph_init(g, &w) == 1);
}

int main(void)
{
    EmMessageGlyph g;
    EmMessageTextStyle style = {0x606060, 0x80, 0, {0, 0}};
    EmMessageDrawConfig cfg = {{0, 6, 0x14, 0, 4}, &style};
    const uint8_t text[] = "AB\nC";

    CHECK(em_message_glyph_init(NULL, NULL) == 0);
    CHECK(em_message_glyph_init(&g, NULL) == 0);

    /* The ordinary path: two lines, one run each. */
    fresh(&g, 1, 1);
    CHECK(em_message_glyph_fc7b0(&g, 0x40, 0xC2, text, sizeof text, &cfg) == 0);
    CHECK(uploads == 3 && flushes == 2 && g.fault == NULL);
    CHECK(last.upload_count == 1 && last.kind == EM_MESSAGE_GLYPH_SPRITE && last.vertex_count == 2);
    CHECK(last.y == 0xC2 + 0x790 + 12);

    /* A style flag selects the skewed strip. */
    style.flag = 8;
    fresh(&g, 1, 1);
    CHECK(em_message_glyph_fc7b0(&g, 0, 0, text, sizeof text, &cfg) == 0);
    CHECK(last.kind == EM_MESSAGE_GLYPH_SKEWED && last.vertex_count == 4);
    style.flag = 0;

    /* Missing or failing workers latch a fault; later calls refuse. */
    fresh(&g, 0, 1);
    CHECK(em_message_glyph_fc7b0(&g, 0, 0, text, sizeof text, &cfg) == -1 && g.fault);
    CHECK(em_message_glyph_fc7b0(&g, 0, 0, text, sizeof text, &cfg) == -1);
    fresh(&g, 1, 0);
    CHECK(em_message_glyph_fc7b0(&g, 0, 0, text, sizeof text, &cfg) == -1 && g.fault);
    fresh(&g, 1, 1);
    fail_upload = 1;
    CHECK(em_message_glyph_fc7b0(&g, 0, 0, text, sizeof text, &cfg) == -1 && g.fault);
    fresh(&g, 1, 1);
    fail_flush = 1;
    CHECK(em_message_glyph_fc7b0(&g, 0, 0, text, sizeof text, &cfg) == -1 && g.fault);
    CHECK(em_message_glyph_cc1e0(&g, 1, 0, 0, 0xA, 0x14, text, sizeof text, &style) == -1);

    /* No terminator inside the supplied bytes. */
    const uint8_t open_text[4] = {'A', 'B', 'C', 'D'};
    fresh(&g, 1, 1);
    CHECK(em_message_glyph_fc7b0(&g, 0, 0, open_text, sizeof open_text, &cfg) == -1 && g.fault);
    fresh(&g, 1, 1);
    CHECK(em_message_glyph_cc1e0(&g, 1, 0, 0, 0xA, 0x14, open_text, sizeof open_text, &style) == -1);

    /* A run that would fill the 0x80-byte buffer (no terminator left). */
    uint8_t long_run[0x81];
    memset(long_run, 'A', 0x80);
    long_run[0x80] = 0;
    fresh(&g, 1, 1);
    CHECK(em_message_glyph_fc7b0(&g, 0, 0, long_run, sizeof long_run, &cfg) == -1 && g.fault);
    long_run[0x7E] = 0;
    fresh(&g, 1, 1);
    CHECK(em_message_glyph_fc7b0(&g, 0, 0, long_run, sizeof long_run, &cfg) == 0 && !g.fault);

    /* A missing config. */
    fresh(&g, 1, 1);
    CHECK(em_message_glyph_fc7b0(&g, 0, 0, text, sizeof text, NULL) == -1 && g.fault);

    /* 001CBE10 spot values. */
    CHECK(em_message_glyph_advance(0x4D) == 12 && em_message_glyph_advance(0x41) == 9);
    CHECK(em_message_glyph_advance(-1) == 9);

    small_font();

    printf("message_glyph_test: %d checks PASS\n", checks);
    return 0;
}
