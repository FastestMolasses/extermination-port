/* Sanitizer fixture for em_render_context.c (docs/RENDER_CONTEXT.md): the
 * fail-stop and view-bound contract on small synthetic views. The original
 * behaviour itself is checked by tools/test_render_context_reference.py;
 * this fixture only pins that a missing view or worker faults before any
 * byte is written, that a latched fault blocks every later call, and that
 * packet writes never leave their view (run under ASan/UBSan). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_render_context.h"

#define CTX 0x00811CC0u
#define CTX_BYTES 0x2530u
#define PKT 0x00600000u
#define PKT_BYTES 0x100u

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static uint8_t ctx_bytes[CTX_BYTES];
static uint8_t pkt_bytes[PKT_BYTES];

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t get32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static void setup(EmRenderContext *s, EmRenderContextView *views, uint32_t count)
{
    memset(s, 0, sizeof *s);
    s->world.ctx = CTX;
    s->world.views = views;
    s->world.view_count = count;
}

int main(void)
{
    EmRenderContextView views[2] = {{CTX, CTX_BYTES, ctx_bytes}, {PKT, PKT_BYTES, pkt_bytes}};
    EmRenderContext s;
    uint32_t r = 0;

    /* Flag words: 001E0C80 sets bit (a0 - 0x20) and returns the old bit;
     * 001D2910 routes to it; a0 >= 0x40 returns 0. */
    setup(&s, views, 2);
    memset(ctx_bytes, 0, sizeof ctx_bytes);
    CHECK(em_render_context_001E0C80(&s, 0x25, 1, &r) == 0 && r == 0);
    CHECK(get32(ctx_bytes + 0x174) == (1u << 5));
    CHECK(em_render_context_001E0C80(&s, 0x25, 0, &r) == 0 && r == 1);
    CHECK(get32(ctx_bytes + 0x174) == 0);
    put32(ctx_bytes + 0xC, 0x80u);
    CHECK(em_render_context_001D2910(&s, 7, &r) == 0 && r == 0x80u);
    CHECK(em_render_context_001D2910(&s, 0x40, &r) == 0 && r == 0);

    /* 001D6C90 at a cursor whose 0x60 bytes do not fit the packet view:
     * BAD_INDEX, and neither the cursor nor the view changes. */
    setup(&s, views, 2);
    memset(pkt_bytes, 0xAB, sizeof pkt_bytes);
    put32(ctx_bytes + 0x10 + 4 * 3, PKT + PKT_BYTES - 0x50u);
    const int32_t args[15] = {3, 0, 1, 0, 0, 1, 0, 0, 1, 2, 0, 1, 0, 1, 0};
    CHECK(em_render_context_001D6C90(&s, args, &r) == -1);
    CHECK(s.fault.code == EM_RC_FAULT_BAD_INDEX);
    CHECK(get32(ctx_bytes + 0x1C) == PKT + PKT_BYTES - 0x50u);
    for (unsigned i = 0; i < PKT_BYTES; ++i) CHECK(pkt_bytes[i] == 0xAB);
    /* A latched fault blocks every later call, even one that would work. */
    put32(ctx_bytes + 0x1C, PKT);
    CHECK(em_render_context_001D6C90(&s, args, &r) == -1);
    CHECK(pkt_bytes[0] == 0xAB);
    /* Cleared, the same call writes one packet and advances the cursor. */
    memset(&s.fault, 0, sizeof s.fault);
    CHECK(em_render_context_001D6C90(&s, args, &r) == 0 && r == PKT + 0x10u);
    CHECK(get32(ctx_bytes + 0x1C) == PKT + 0x60u && pkt_bytes[3] == 0x10 && pkt_bytes[0] == 5);
    CHECK(get32(pkt_bytes + 0x1C) == 0x50000004u && pkt_bytes[0x60] == 0xAB);

    /* 001DD950 without a view for its source quadword: nothing written. */
    setup(&s, views, 1);
    memset(ctx_bytes + 0x2450, 0x5A, 0x18);
    CHECK(em_render_context_001DD950(&s, PKT, 0x3F800000u, 0) == -1);
    CHECK(s.fault.code == EM_RC_FAULT_BAD_INDEX && s.fault.address == PKT);
    for (unsigned i = 0; i < 0x18; ++i) CHECK(ctx_bytes[0x2450 + i] == 0x5A);

    /* 001DD7B0 without its 001006D8 worker: NULL_WORKER, nothing written. */
    setup(&s, views, 2);
    memset(ctx_bytes + 0x24F0, 0x77, 0x24);
    CHECK(em_render_context_001DD7B0(&s) == -1);
    CHECK(s.fault.code == EM_RC_FAULT_NULL_WORKER && s.fault.address == 0x001006D8u);
    for (unsigned i = 0; i < 0x24; ++i) CHECK(ctx_bytes[0x24F0 + i] == 0x77);

    /* 001DDA00 / 001D5370 without workers: NULL_WORKER before any write. */
    setup(&s, views, 2);
    memset(ctx_bytes + 0x2470, 0, 0x40);
    CHECK(em_render_context_001DDA00(&s) == -1 && s.fault.code == EM_RC_FAULT_NULL_WORKER);
    CHECK(get32(ctx_bytes + 0x2470 + 0x1C) == 0 && ctx_bytes[0x2470] == 0);
    setup(&s, views, 2);
    CHECK(em_render_context_001D5370(&s) == -1 && s.fault.code == EM_RC_FAULT_NULL_WORKER);

    /* The clip rule: exponent 255 is not modelled; denormals are zero. */
    const uint32_t inf[4] = {0x7F800000u, 0, 0, 0x3F800000u};
    const uint32_t v[4] = {0x40000000u, 0xC0000000u, 0x00000001u, 0x3F800000u};
    uint32_t flags = 0;
    CHECK(em_render_context_clipw(inf, &flags) == -1);
    CHECK(em_render_context_clipw(v, &flags) == 0 && flags == (1u | 8u));

    if (failures) {
        printf("render_context_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("render_context_test: PASS (flag words, packet view bound, latch, missing view/worker fail-stop, clip rule)\n");
    return 0;
}
