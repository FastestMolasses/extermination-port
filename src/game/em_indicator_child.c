/* em_indicator_child.c - see em_indicator_child.h. */
#include "game/em_indicator_child.h"

#include "game/em_ee_float.h"

#include <string.h>

int em_indicator_child_step(uint32_t callback, EmIndicatorChildRecord *r,
                            const EmIndicatorChildWorkers *w)
{
    if (!r || !r->status || !r->a0 || !r->c80 || !w || !w->init || !w->place || !w->color ||
        !w->free_self)
        return -1;
    if (callback != EM_INDICATOR_CHILD_001C5680 && callback != EM_INDICATOR_CHILD_001C5760)
        return -1;
    const uint8_t status = *r->status;
    if (status == 0) {
        int32_t refused = 0;
        uint32_t fn = callback == EM_INDICATOR_CHILD_001C5680 ? 0x001C2360u : 0x001C22A0u;
        if (w->init(w->ctx, fn, &refused) < 0)
            return -1;
        if (refused != 0)
            return EM_INDICATOR_CHILD_WAITING;
        if (w->place(w->ctx) < 0)
            return -1;
        *r->status = 1;
        return EM_INDICATOR_CHILD_PLACED;
    }
    if (status == 1) {
        /* +0x80..+0x8C = +0xA0..+0xAC. The even-frame stack copy that
         * follows is never read, so it has no port counterpart. */
        float colour[4];
        memcpy(colour, r->a0, sizeof colour);
        memcpy(r->c80, colour, sizeof colour);
        if (callback == EM_INDICATOR_CHILD_001C5760 && r->alt != 0 && w->place(w->ctx) < 0)
            return -1;
        if (w->color(w->ctx, r->c80) < 0)
            return -1;
        return EM_INDICATOR_CHILD_DRAWN;
    }
    if (w->free_self(w->ctx) < 0)
        return -1;
    return EM_INDICATOR_CHILD_FREED;
}

int em_indicator_00827B10_colour(int16_t level, float child_a0[4], uint32_t *spad3A20)
{
    if (!child_a0 || !spad3A20)
        return -1;
    if (level != 0) {
        uint32_t f = em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)(int32_t)level), 0x43000000u);
        *spad3A20 = f;
        child_a0[0] = 0.0f;
        child_a0[1] = em_ee_float(f);
        child_a0[2] = 0.0f;
        child_a0[3] = em_ee_float(0x3E800000u);
    } else {
        child_a0[0] = em_ee_float(0x3F800000u);
        child_a0[1] = 0.0f;
        child_a0[2] = 0.0f;
        child_a0[3] = em_ee_float(0x3E800000u);
    }
    return 0;
}
