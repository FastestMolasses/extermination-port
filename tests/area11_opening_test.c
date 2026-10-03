#include "game/em_area11_opening.h"
#include "game/em_script.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The controller 00823E80 (em_area11_opening_tick, the whole function)
 * over recording workers: the calls in their original order, the record
 * bytes +0x00 / +0x04 / +0x05 / +0x2E, and the fail-stops. The original
 * instructions are compared by tools/test_area11_opening_reference.py. */
typedef struct {
    char calls[32][24];
    unsigned count;
    int32_t flag, tick_result;
    int fail_at;          /* index of the call that fails, or -1 */
    int32_t refused;      /* 001B0FD0's result */
    uint8_t d810811;
} Fixture;

static int record(Fixture *f, const char *name)
{
    assert(f->count < 32);
    snprintf(f->calls[f->count], sizeof f->calls[0], "%s", name);
    return (int)f->count++ == f->fail_at ? -1 : 0;
}

static int w_001BA1C0(void *ctx, uint32_t a1, int32_t *result)
{
    Fixture *f = ctx;
    assert(a1 == 0x39);
    *result = f->flag;
    return record(f, "001BA1C0");
}
static int w_001BA1A0(void *ctx, uint32_t entry)
{
    assert(entry == 0x828FC0);
    return record(ctx, "001BA1A0");
}
static int w_001BA1F0(void *ctx, int32_t *result)
{
    Fixture *f = ctx;
    *result = f->tick_result;
    return record(f, "001BA1F0");
}
static int w_001FABB0(void *ctx) { return record(ctx, "001FABB0"); }
static int s_00810811(void *ctx, uint8_t value)
{
    ((Fixture *)ctx)->d810811 = value;
    return record(ctx, "D_00810811");
}
static int w_001C4760(void *ctx, int32_t a0, int32_t a1)
{
    assert(a0 == 0 && a1 == 1);
    return record(ctx, "001C4760");
}
static int w_001FAE70(void *ctx, int32_t a0)
{
    assert(a0 == 0);
    return record(ctx, "001FAE70");
}
static int w_001AEE10(void *ctx, int16_t a0, uint8_t a1)
{
    assert(a0 == 4 && a1 == 0);
    return record(ctx, "001AEE10");
}

static int w_001B0FD0(void *ctx, int32_t *result)
{
    *result = ((Fixture *)ctx)->refused;
    return record(ctx, "001B0FD0");
}
static int w_001C6380(void *ctx) { return record(ctx, "001C6380"); }
static int w_001B1B70(void *ctx) { return record(ctx, "001B1B70"); }
static int m_4C(void *ctx) { return record(ctx, "+0x4C"); }
static int w_001AFC10(void *ctx) { return record(ctx, "001AFC10"); }

static void calls_are(const Fixture *f, const char *const *names, unsigned n)
{
    assert(f->count == n);
    for (unsigned i = 0; i < n; ++i) assert(!strcmp(f->calls[i], names[i]));
}

int main(int argc, char **argv)
{
    Fixture f;
    EmArea11OpeningWorkers w = {&f, w_001BA1C0, w_001BA1A0, w_001BA1F0, w_001FABB0,
                                s_00810811, w_001C4760, w_001FAE70, w_001AEE10,
                                w_001B0FD0, w_001C6380, w_001B1B70, m_4C, w_001AFC10};
    EmArea11Opening op = {0, 0, 0, 0};
    uint32_t at = 0;

    /* +0x04 == 0: 001B0FD0; a refusal stops there, a bind places the
     * record and sets +0x04 = +0x00 = 1 (no tail). */
    memset(&f, 0, sizeof f); f.fail_at = -1; f.refused = 1;
    assert(em_area11_opening_tick(&op, &w, &at) == 0);
    { const char *e[] = {"001B0FD0"}; calls_are(&f, e, 1); }
    assert(op.b04 == 0 && op.b00 == 0);
    memset(&f, 0, sizeof f); f.fail_at = -1;
    assert(em_area11_opening_tick(&op, &w, &at) == 0);
    { const char *e[] = {"001B0FD0", "001C6380"}; calls_are(&f, e, 2); }
    assert(op.b04 == 1 && op.b00 == 1 && op.b05 == 0);

    /* +0x04 == 2 / 3: the record frees itself; any other value returns. */
    for (unsigned b04 = 2; b04 < 256; ++b04) {
        EmArea11Opening other = {1, (uint8_t)b04, 0, 0};
        memset(&f, 0, sizeof f); f.fail_at = -1;
        assert(em_area11_opening_tick(&other, &w, &at) == 0);
        if (b04 <= 3) { const char *e[] = {"001AFC10"}; calls_are(&f, e, 1); }
        else assert(f.count == 0);
        assert(other.b04 == b04 && other.b00 == 1);
    }

    /* +0x05 == 0: the start and the stream stop, then +0x05 = 1. */
    memset(&f, 0, sizeof f); f.fail_at = -1;
    assert(em_area11_opening_tick(&op, &w, &at) == 0);
    { const char *e[] = {"001BA1C0", "001BA1A0", "001FABB0", "001B1B70", "+0x4C"}; calls_are(&f, e, 5); }
    assert(op.b05 == 1 && op.h2E == 0);

    /* +0x05 == 1, the script running (0): the poll only. */
    memset(&f, 0, sizeof f); f.fail_at = -1;
    assert(em_area11_opening_tick(&op, &w, &at) == 0);
    { const char *e[] = {"001BA1C0", "001BA1F0", "001B1B70", "+0x4C"}; calls_are(&f, e, 4); }
    assert(op.b05 == 1 && op.h2E == 0);

    /* Finished (1) and the skip path (3): the completion, in order. */
    for (int result = 1; result <= 3; result += 2) {
        op.b05 = 1; op.h2E = 0;
        memset(&f, 0, sizeof f); f.fail_at = -1; f.tick_result = result;
        assert(em_area11_opening_tick(&op, &w, &at) == 0);
        const char *e[] = {"001BA1C0", "001BA1F0", "D_00810811", "001C4760", "001FAE70", "001AEE10",
                           "001B1B70", "+0x4C"};
        calls_are(&f, e, 8);
        assert(op.b05 == 2 && op.h2E == 0xFFFF && f.d810811 == 0xFF);
    }

    /* +0x05 == 2 (and any other value): nothing past the flag test. */
    for (unsigned b05 = 2; b05 < 6; ++b05) {
        op.b05 = (uint8_t)b05;
        memset(&f, 0, sizeof f); f.fail_at = -1; f.tick_result = 1;
        assert(em_area11_opening_tick(&op, &w, &at) == 0);
        { const char *e[] = {"001BA1C0", "001B1B70", "+0x4C"}; calls_are(&f, e, 3); }
        assert(op.b05 == b05);
    }

    /* D_00810758[0x39] == 0xFF: nothing past the flag test, whatever +0x05. */
    for (unsigned b05 = 0; b05 < 3; ++b05) {
        op.b05 = (uint8_t)b05;
        memset(&f, 0, sizeof f); f.fail_at = -1; f.flag = 1; f.tick_result = 1;
        assert(em_area11_opening_tick(&op, &w, &at) == 0);
        { const char *e[] = {"001BA1C0", "001B1B70", "+0x4C"}; calls_are(&f, e, 3); }
        assert(op.b05 == b05);
    }

    /* Fail-stop: a failing callee faults at its address; a missing one too. */
    op.b05 = 0;
    memset(&f, 0, sizeof f); f.fail_at = 1;
    assert(em_area11_opening_tick(&op, &w, &at) == -1 && at == 0x001BA1A0u && op.b05 == 0);
    op.b05 = 1;
    memset(&f, 0, sizeof f); f.fail_at = 3; f.tick_result = 1;
    assert(em_area11_opening_tick(&op, &w, &at) == -1 && at == 0x001C4760u);
    EmArea11OpeningWorkers missing = w;
    missing.w_001AEE10 = NULL;
    op.b05 = 1; op.h2E = 0;
    memset(&f, 0, sizeof f); f.fail_at = -1; f.tick_result = 1;
    assert(em_area11_opening_tick(&op, &missing, &at) == -1 && at == 0x001AEE10u);
    assert(op.h2E == 0 && f.count == 2);   /* checked before the first write */
    assert(em_area11_opening_tick(NULL, &w, &at) == -1 && at == 0x00823E80u);
    /* A failing tail callee faults at its address after the machine ran. */
    op.b05 = 0;
    memset(&f, 0, sizeof f); f.fail_at = 3;
    assert(em_area11_opening_tick(&op, &w, &at) == -1 && at == 0x001B1B70u && op.b05 == 1);

    if (argc == 2) {
        /* The exported opening image (tools/export_area11_opening.py). */
        EmScriptImage image = {0};
        assert(em_script_image_load(&image, argv[1]));
        assert(image.base == 0x828F30 && image.entry == 0x828FC0 && image.length == 0x390);
        unsigned char *first = em_script_image_read(&image, image.entry, 64);
        assert(first && em_script_u32(first, 0) == 7 && em_script_u32(first, 8) == 12);
        unsigned char *last = em_script_image_read(&image, 0x829280, 64);
        assert(last && em_script_u32(last, 0) == 0x80000007);
        assert(!em_script_image_read(&image, 0x8292C0, 1));
        assert(!em_script_image_read(&image, UINT32_MAX, 64));
        em_script_image_free(&image);
        assert(!image.bytes);
    }
    puts("area11_opening_test: PASS");
    return 0;
}
