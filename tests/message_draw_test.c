/* Contract test for em_message_draw_original (docs/MESSAGE_DRAW.md).
 *
 * The message banks are synthetic: plain ASCII text and records built here in
 * the original bank layout; no disc file is read. Some fixture values are
 * original values, written here as small constants so the fixture has the
 * original's shape (each checked against the pinned ELF):
 *   - `config` words = the first five words of D_00264CD0 (0, 6, 0x14, 0, 4);
 *   - `fallback` = the D_00264BF0 template (0, 0xA, 0x14, 0, 0, style 0);
 *   - colors[0] = D_0026EC10[0] (0x606060) and color_count 16 = that table's
 *     length (colors[1..3] = 1, 2, 3 are placeholders, not the ELF's words);
 *   - y = 0xC2 is the row 001FD950 passes to 001FE070.
 * Behaviour against the original is proven by
 * tools/test_message_draw_reference.py; this fixture only checks the native
 * fail-stop contract under ASan/UBSan: missing or failing workers, reads
 * outside the bank, colour indices outside D_0026EC10, a missing style or
 * template, buffer overruns the original would run into, and the latch.
 * Prints "<checks> PASS". */
#include "game/em_message_draw_original.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(cond) do { checks++; if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); exit(1); } } while (0)

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* One-line bank: text plus `n` records (tag, arg, trigger, byte12). */
static uint32_t build(uint8_t *out, const char *text, const int32_t (*recs)[4], uint32_t n)
{
    memset(out, 0, 0x400);
    uint32_t rec_size = (n * 16 + 15) & ~15u;
    put32(out + 0, 0x20);            /* records at 0x20 */
    put32(out + 4, 1);
    put32(out + 8, rec_size);
    put32(out + 0x10 + 0xC, n << 4); /* entry 0: records at +0, count n */
    for (uint32_t i = 0; i < n; i++)
        for (int k = 0; k < 4; k++) put32(out + 0x20 + i * 16 + 4 * (uint32_t)k, (uint32_t)recs[i][k]);
    uint32_t h = 0x20 + rec_size;
    put32(out + h, 0x20);            /* pool at H + 0x20 */
    put32(out + h + 4, 1);
    size_t len = strlen(text);
    put32(out + h + 8, (uint32_t)len + 1);
    memcpy(out + h + 0x20, text, len + 1);
    return h + 0x20 + (uint32_t)len + 1;
}

static int draws, advances, fail_draw, fail_advance;

static int advance(void *ctx, uint8_t c, int32_t *out)
{
    (void)ctx;
    advances++;
    *out = c == ' ' ? 5 : 9;
    return !fail_advance;
}

static int draw(void *ctx, int32_t x, int32_t y, const uint8_t *text, const EmMessageDrawConfig *cfg)
{
    (void)ctx; (void)x; (void)y; (void)text; (void)cfg;
    draws++;
    return !fail_draw;
}

static EmMessageTextStyle style;
static EmMessageDrawConfig config = { { 0, 6, 0x14, 0, 4 }, &style };
static EmMessageDrawConfig fallback = { { 0, 0xA, 0x14, 0, 0 }, NULL };
static const int32_t colors[16] = { 0x606060, 1, 2, 3 };
static uint8_t bank_bytes[0x400];

static void setup(EmMessageDraw *d, EmMessageDrawData *data, uint32_t size, int with_workers)
{
    EmMessageDrawWorkers w = { NULL, advance, draw };
    memset(data, 0, sizeof *data);
    data->global.bytes = bank_bytes;
    data->global.size = size;
    data->area = data->global;
    data->colors = colors;
    data->color_count = 16;
    data->line_config = &config;
    data->fallback = &fallback;
    data->text = &style;
    CHECK(em_message_draw_init(d, data, with_workers ? &w : NULL) == 1);
    draws = advances = fail_draw = fail_advance = 0;
    config.style = &style;
}

int main(void)
{
    static EmMessageDraw d;
    EmMessageDrawData data;
    memset(&data, 0, sizeof data);
    const int32_t flag_recs[2][4] = { { 3, 1, 0, 0 }, { 3, 0, 5, 0 } };
    uint32_t size = build(bank_bytes, "AB CD", flag_recs, 2);

    /* init refusals */
    CHECK(em_message_draw_init(NULL, &data, NULL) == 0);
    CHECK(em_message_draw_init(&d, NULL, NULL) == 0);
    memset(&data, 0, sizeof data);
    data.line_config = &config;
    CHECK(em_message_draw_init(&d, &data, NULL) == 0);          /* no text style */
    data.text = &style;
    data.color_count = 4;
    CHECK(em_message_draw_init(&d, &data, NULL) == 0);          /* count without table */

    /* success path: draw, flag set then cleared, three runs */
    setup(&d, &data, size, 1);
    CHECK(em_message_draw_line(&d, 1, 0) == 1 && !d.fault);
    CHECK(draws == 3 && style.flag == 0 && advances > 0);

    /* missing / failing workers */
    setup(&d, &data, size, 0);
    CHECK(em_message_draw_line(&d, 1, 0) == 0 && d.fault);
    CHECK(em_message_draw_line(&d, 1, 0) == 0);                  /* latched */
    CHECK(em_message_draw_fe070(&d, &data.global, 0, 0, 0) == -1);
    setup(&d, &data, size, 1);
    fail_advance = 1;
    CHECK(em_message_draw_line(&d, 0, 0) == 0 && d.fault);
    setup(&d, &data, size, 1);
    fail_draw = 1;
    CHECK(em_message_draw_fe070(&d, &data.global, 0, 0x40, 0xC2) == -1 && d.fault);

    /* reads outside the bank: truncated image, entry past the end */
    setup(&d, &data, size - 1, 1);
    CHECK(em_message_draw_line(&d, 1, 0) == 0 && d.fault);
    setup(&d, &data, 0x18, 1);
    CHECK(em_message_draw_line(&d, 1, 0) == 0 && d.fault);
    setup(&d, &data, size, 1);
    CHECK(em_message_draw_line(&d, 1, 0x40) == 0 && d.fault);    /* entry outside bank */
    setup(&d, &data, size, 1);
    CHECK(em_message_draw_fe070(&d, &data.global, 1, 0, 0) == 0 && !d.fault); /* original 0 */

    /* colour index outside D_0026EC10, style missing for tags 2/3 */
    const int32_t bad_color[1][4] = { { 2, 16, 0, 0 } };
    size = build(bank_bytes, "AB", bad_color, 1);
    setup(&d, &data, size, 1);
    CHECK(em_message_draw_fe070(&d, &data.global, 0, 0, 0) == -1 && d.fault);
    const int32_t neg_color[1][4] = { { 4, -1, 0, 0 } };
    size = build(bank_bytes, "AB", neg_color, 1);
    setup(&d, &data, size, 1);
    CHECK(em_message_draw_fe070(&d, &data.global, 0, 0, 0) == -1 && d.fault);
    size = build(bank_bytes, "AB", flag_recs, 2);
    setup(&d, &data, size, 1);
    config.style = NULL;
    CHECK(em_message_draw_fe070(&d, &data.global, 0, 0, 0) == -1 && d.fault);
    config.style = &style;

    /* the original's NULL-record read: records counted, index past tbl[1] */
    size = build(bank_bytes, "AB", flag_recs, 2);
    put32(bank_bytes + 4, 0);
    setup(&d, &data, size, 1);
    CHECK(em_message_draw_fe070(&d, &data.global, 0, 0, 0) == -1 && d.fault);

    /* line buffer overrun (a run of 0x80 bytes) and measure overrun (0xC0) */
    char long_text[0x100];
    memset(long_text, 'Z', sizeof long_text - 1);
    long_text[0x80] = 0;
    size = build(bank_bytes, long_text, flag_recs, 1);
    setup(&d, &data, size, 1);
    CHECK(em_message_draw_fe070(&d, &data.global, 0, 0, 0) == -1 && d.fault);
    long_text[0x80] = 'Z';
    long_text[0xC0] = 0;
    size = build(bank_bytes, long_text, NULL, 0);
    setup(&d, &data, size, 1);
    CHECK(em_message_draw_line(&d, 1, 0) == 0 && d.fault);
    long_text[0xBF] = 0;                                        /* 0xBF bytes + NUL fit */
    size = build(bank_bytes, long_text, NULL, 0);
    setup(&d, &data, size, 1);
    CHECK(em_message_draw_line(&d, 1, 0) == 1 && !d.fault && draws == 1);

    /* 001FC770 template: used for a NULL cfg, fault when not supplied */
    setup(&d, &data, size, 1);
    CHECK(em_message_draw_fc770(&d, 1, 2, (const uint8_t *)"X", NULL) == 0 && draws == 1);
    data.fallback = NULL;
    CHECK(em_message_draw_fc770(&d, 1, 2, (const uint8_t *)"X", NULL) == -1 && d.fault);

    /* strlen / segment helpers refuse unterminated input */
    uint32_t len, next;
    int32_t width;
    const uint8_t raw[3] = { 'A', 'B', 'C' };
    CHECK(em_message_draw_strlen(raw, 3, &len) == 0);
    setup(&d, &data, size, 1);
    CHECK(em_message_draw_cc170(&d, raw, 3, &width) == -1 && d.fault);
    setup(&d, &data, size, 1);
    CHECK(em_message_draw_fe530(&d, NULL, 0, raw, 3, 1, &next) == -1 && d.fault);
    setup(&d, &data, size, 1);
    uint8_t small[2];
    CHECK(em_message_draw_fe530(&d, small, 2, (const uint8_t *)"ABC", 4, 0, &next) == -1 && d.fault);

    printf("%d PASS\n", checks);
    return 0;
}
