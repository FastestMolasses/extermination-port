/* AREA01 render lane, module frame: 0015B610. See em_area01_render_frame.h
 * and docs/AREA01_RENDER.md. */
#include "game/em_area01_render_frame.h"

typedef EmArea01RenderFrame S;
typedef uint32_t u32;

#define F_8 0x41000000u /* 8.0 */
#define TRY EM_A01R_TRY
#define CALLW(address, expr) \
    do { if ((expr) < 0) return em_a01r_fault(&s->core, (address), EM_A01R_FAULT_WORKER_FAILED, 0); } while (0)

static int need(S *s)
{
    s->core.function = 0x0015B610u;
    if (em_a01r_latched(&s->core)) return -1;
    const struct { const void *w; u32 address; } list[] = {
        {(const void *)s->workers.w_00182B30, 0x00182B30u}, {(const void *)s->workers.w_00174A50, 0x00174A50u},
        {(const void *)s->workers.w_00182D70, 0x00182D70u}, {(const void *)s->workers.w_00183240, 0x00183240u},
        {(const void *)s->workers.w_00183250, 0x00183250u}, {(const void *)s->workers.w_001833F0, 0x001833F0u},
        {(const void *)s->workers.w_00183440, 0x00183440u}, {(const void *)s->workers.w_001834E0, 0x001834E0u},
    };
    for (unsigned i = 0; i < sizeof list / sizeof list[0]; ++i)
        if (!list[i].w) return em_a01r_fault(&s->core, list[i].address, EM_A01R_FAULT_NULL_WORKER, 0);
    return 0;
}

/* a +4 = 4, +5 = five, +6 = 0, +0x1F0 = mode (the original's store order). */
static int set_state(S *s, u32 a, u32 five, u32 mode)
{
    EmArea01RenderCore *c = &s->core;
    TRY(em_a01r_st8(c, a + 4u, 4u));
    TRY(em_a01r_st8(c, a + 5u, five));
    TRY(em_a01r_st8(c, a + 6u, 0u));
    return em_a01r_st8(c, a + 0x1F0u, mode);
}

int em_area01_render_0015B610(S *s, u32 a)
{
    EmArea01RenderCore *c = &s->core;
    u32 b, state;
    TRY(need(s));
    TRY(em_a01r_ld8(c, EM_A01R_SPAD_3B8D, &b));
    if (b != 0 && b != 4u) {
        int32_t busy = 0;
        CALLW(0x00182B30u, s->workers.w_00182B30(s->workers.ctx, a, b, &busy));
        if (busy == 0) {
            TRY(em_a01r_ld8(c, a + 5u, &state));
            if (state == 3u) {
                TRY(set_state(s, a, 0xCu, 0x17u));
            } else {
                if (state == 1u) CALLW(0x00174A50u, s->workers.w_00174A50(s->workers.ctx, a, F_8));
                TRY(set_state(s, a, 0u, 0x41u));
            }
            CALLW(0x00182D70u, s->workers.w_00182D70(s->workers.ctx, a));
            return 0;
        }
    }
    TRY(em_a01r_ld8(c, a + 5u, &state));
    switch (state) {
    case 4: CALLW(0x001834E0u, s->workers.w_001834E0(s->workers.ctx, a)); break;
    case 3: CALLW(0x00183440u, s->workers.w_00183440(s->workers.ctx, a)); break;
    case 2: CALLW(0x001833F0u, s->workers.w_001833F0(s->workers.ctx, a)); break;
    case 1: CALLW(0x00183250u, s->workers.w_00183250(s->workers.ctx, a)); break;
    case 0: CALLW(0x00183240u, s->workers.w_00183240(s->workers.ctx, a)); break;
    default: break;
    }
    return 0;
}
