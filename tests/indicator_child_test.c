/* em_indicator_child: 001C5680 / 001C5760 over one child record, and the
 * 00827B10 level tail. The executed original is compared in
 * tools/test_census_unverified_reference.py; this fixture runs the same
 * paths under ASan/UBSan. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "game/em_indicator_child.h"

static char calls[64];
static int refuse;

static void log_call(char c)
{
    size_t n = strlen(calls);
    assert(n + 1 < sizeof calls);
    calls[n] = c;
    calls[n + 1] = 0;
}
static int w_init(void *ctx, uint32_t fn, int32_t *result)
{
    (void)ctx;
    log_call(fn == 0x001C2360u ? 'i' : 'I');
    *result = refuse;
    return 0;
}
static int w_place(void *ctx) { (void)ctx; log_call('p'); return 0; }
static int w_color(void *ctx, float c80[4])
{
    (void)ctx;
    log_call('c');
    c80[0] = -127.0f;   /* 001F54E0 rewrites +0x80 in place */
    return 0;
}
static int w_free(void *ctx) { (void)ctx; log_call('f'); return 0; }

static const EmIndicatorChildWorkers W = {NULL, w_init, w_place, w_color, w_free};

static int step(uint32_t cb, uint8_t *status, uint8_t alt, const float a0[4], float c80[4])
{
    EmIndicatorChildRecord r = {status, alt, a0, c80};
    return em_indicator_child_step(cb, &r, &W);
}

int main(void)
{
    const float a0[4] = {1.0f, 0.0f, 0.0f, 0.25f};
    float c80[4] = {1, 1, 1, 1};
    uint8_t status = 0;

    /* State 0 refused by the bind: nothing else, still state 0. */
    refuse = 1;
    assert(step(0x001C5680u, &status, 0, a0, c80) == EM_INDICATOR_CHILD_WAITING);
    assert(!strcmp(calls, "i") && status == 0);
    refuse = 0;
    calls[0] = 0;
    assert(step(0x001C5680u, &status, 0, a0, c80) == EM_INDICATOR_CHILD_PLACED);
    assert(!strcmp(calls, "ip") && status == 1 && c80[0] == 1.0f);
    calls[0] = 0;
    assert(step(0x001C5680u, &status, 1, a0, c80) == EM_INDICATOR_CHILD_DRAWN);
    assert(!strcmp(calls, "c"));                 /* 001C5680 has no +0xA path */
    assert(c80[0] == -127.0f && c80[1] == 0.0f && c80[3] == 0.25f);
    /* 001C5760 with +0xA != 0 places before each draw. */
    calls[0] = 0;
    assert(step(0x001C5760u, &status, 1, a0, c80) == EM_INDICATOR_CHILD_DRAWN);
    assert(!strcmp(calls, "pc"));
    calls[0] = 0;
    status = 0;
    assert(step(0x001C5760u, &status, 0, a0, c80) == EM_INDICATOR_CHILD_PLACED);
    assert(!strcmp(calls, "Ip"));
    /* Any +4 above 1 frees (2, 3, 4, 0x80 measured). */
    const uint8_t frees[] = {2, 3, 4, 0x80};
    for (unsigned i = 0; i < sizeof frees; ++i) {
        calls[0] = 0;
        status = frees[i];
        assert(step(0x001C5680u, &status, 0, a0, c80) == EM_INDICATOR_CHILD_FREED);
        assert(!strcmp(calls, "f"));
    }
    assert(step(0x001C5000u, &status, 0, a0, c80) == -1);

    /* 00827B10's colour tail over its +0x28 level (em_elevator_tick steps
     * the level; tools/test_elevator_reference.py checks that step). */
    float child[4];
    uint32_t spad = 0xDEADBEEFu;
    assert(em_indicator_00827B10_colour(0, child, &spad) == 0);
    assert(child[0] == 1.0f && child[1] == 0.0f && child[3] == 0.25f);
    assert(spad == 0xDEADBEEFu);                 /* level 0 leaves 0x70003A20 */
    assert(em_indicator_00827B10_colour(8, child, &spad) == 0);
    assert(child[0] == 0.0f && child[1] == 0.0625f && spad == 0x3D800000u);
    assert(em_indicator_00827B10_colour(0x80, child, &spad) == 0);
    assert(child[1] == 1.0f && child[3] == 0.25f && spad == 0x3F800000u);
    assert(em_indicator_00827B10_colour(8, NULL, &spad) == -1);
    puts("indicator child (001C5680 / 001C5760) and the 00827B10 colour tail: PASS");
    return 0;
}
