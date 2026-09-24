/* ASan/UBSan fixture for em_status_ui_leftovers.c: the fail-stop contract
 * at the edges of the supplied blocks and regions (the original-instruction
 * comparison is tools/test_status_ui_leftovers_reference.py, which builds
 * and runs this). Synthetic inputs only; no original bytes. */
#include "game/em_status_ui_leftovers.h"

#include <stdio.h>
#include <string.h>

static int calls;

static int ok_blend(void *c, int32_t s, int32_t m) { (void)c; (void)s; (void)m; return ++calls, 0; }
static int ok_sprite(void *c, int32_t s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba,
                     uint64_t t)
{
    (void)c; (void)s; (void)x; (void)y; (void)w; (void)h; (void)rgba; (void)t;
    return ++calls, 0;
}
static int ok_f2i(void *c, uint32_t b, int32_t *v) { (void)c; (void)b; *v = 0; return ++calls, 0; }
static int ok_sound(void *c, int32_t a, int32_t b, int32_t d, int32_t e)
{
    (void)c; (void)a; (void)b; (void)d; (void)e;
    return ++calls, 0;
}
static int ok_battery(void *c, uint32_t p, int32_t x, int32_t y, uint64_t t, int32_t k)
{
    (void)c; (void)p; (void)x; (void)y; (void)t; (void)k;
    return ++calls, 0;
}
static int ok_width(void *c, uint32_t t, int32_t *w) { (void)c; (void)t; *w = 10; return ++calls, 0; }
static int ok_textp(void *c, int32_t s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t t,
                    uint32_t st)
{
    (void)c; (void)s; (void)x; (void)y; (void)w; (void)h; (void)t; (void)st;
    return ++calls, 0;
}
static int ok_actor(void *c, uint8_t *a) { (void)c; (void)a; return ++calls, 0; }
static int ok_copy(void *c, uint8_t *block, uint32_t d, uint32_t s, int32_t n)
{
    (void)c;
    memmove(block + d, block + s, (size_t)n);
    return ++calls, 0;
}

#define CHECK(x)                                                                               \
    do {                                                                                       \
        if (!(x)) {                                                                            \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x);                                 \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

int main(void)
{
    static uint8_t data[0x3000], game[0xC00], table[0x100], spad[0x20];
    for (unsigned i = 0; i < sizeof data; ++i)
        data[i] = (uint8_t)(i * 7u);
    EmSulRegion regions[] = {{0x265000u, sizeof data, data}, {0x810400u, sizeof game, game},
                             {0x289B40u, sizeof table, table}, {0x70003B80u, sizeof spad, spad}};
    EmSulMemory mem = {regions, 4}, short_mem = {regions, 1};
    EmSulWorkers w = {0};
    w.blend = ok_blend;
    w.sprite = ok_sprite;
    w.float_to_int = ok_f2i;
    w.sound = ok_sound;
    w.battery = ok_battery;
    w.text_width = ok_width;
    w.text_proportional = ok_textp;
    w.free_actor = ok_actor;
    w.block_copy = ok_copy;

    /* A list page the ring index reaches past: the dry run faults first. */
    uint8_t page[0x60];
    memset(page, 0, sizeof page);
    page[0x18] = 3;
    page[0x19] = 0x20; /* 0x50 + 0x20 lies outside the 0x60-byte page */
    EmSulListGlobals g = {1, 2, 4};
    int32_t result = 0;
    calls = 0;
    CHECK(em_sul_0020B210(&w, &mem, page, sizeof page, 0x265CD0u, 0, 2, &g, &result) == -1);
    CHECK(calls == 0 && page[0x1A] == 0 && g.d2821B4 == 1);
    /* The same page with room for the ring runs. */
    uint8_t big[0xA0];
    memset(big, 0, sizeof big);
    big[0x18] = 3;
    CHECK(em_sul_0020B210(&w, &mem, big, sizeof big, 0x265CD0u, 0, 2, &g, &result) == 0);
    CHECK(calls > 0 && g.d2821B4 == 1);
    /* A table outside the regions, or a misaligned one, faults before drawing. */
    calls = 0;
    CHECK(em_sul_0020AE40(&w, &mem, 0, 0x100000u, 2) == -1 && calls == 0);
    CHECK(em_sul_0020AE40(&w, &mem, 0, 0x265C54u, 2) == -1 && calls == 0);
    CHECK(em_sul_0020B0D0(&w, &short_mem, 0x265C50u) == -1 && calls == 0);
    CHECK(em_sul_0020AE40(&w, &mem, 0, 0x265C50u, 2) == 0 && calls == 13);
    /* The ring index at the edge of its page. */
    int32_t index = 0;
    CHECK(em_sul_0020BEF0(big, 0x18, &index) == -1);
    CHECK(em_sul_0020BEF0(big, 0x1A, &index) == 0);
    /* The area title: a record too small, and a string table index
     * outside the regions (+0x2A = 0x7FFF). */
    uint8_t actor[0x2F0];
    memset(actor, 0, sizeof actor);
    actor[4] = 1;
    actor[0x2A] = 0xFF;
    actor[0x2B] = 0x7F;
    calls = 0;
    CHECK(em_sul_001C5930(&w, &mem, actor, 0x100) == -1);
    CHECK(em_sul_001C5930(&w, &mem, actor, sizeof actor) == -1 && calls == 0 && actor[0x28] == 0);
    actor[0x2A] = 0x1A;
    actor[0x2B] = 0;
    CHECK(em_sul_001C5930(&w, &mem, actor, sizeof actor) == 0 && calls == 2 && actor[0x28] == 0xFF);
    /* Context saves at the end of the block. */
    uint8_t context[0x140];
    memset(context, 0x5A, sizeof context);
    CHECK(em_sul_0021BAC0(&w, context, sizeof context, 0) == 0);
    CHECK(em_sul_0021BAC0(&w, context, sizeof context, 1) == -1);
    CHECK(em_sul_0021BA70(&w, context, 0xD0, 1) == -1 && context[0xB0] == 0x5A);
    uint64_t value = 0;
    CHECK(em_sul_0021BA70(&w, context, sizeof context, 0x1122334455667788ull) == 0);
    CHECK(em_sul_0021BAB0(context, sizeof context, &value) == 0 && value == 0x1122334455667788ull);
    printf("PASS\n");
    return 0;
}
