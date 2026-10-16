#include "game/em_shadow_original.h"
#include "em_gfx.h"
#include "game/em_shadow_live.c"

/* The selector depends on the current frame only to refuse a receiver
 * swap while the frame still has queued passes. No draw workers run. */
uint32_t em_frame_counter(void) { return 1; }

unsigned area00_texture_capacity(void) { return EM_GFX_OBJECT_TEX_MAX; }

int area00_receivers_select(unsigned area, unsigned sub)
{ return em_shadow_live_select_area(area, sub); }

unsigned area00_receivers_selected(unsigned field)
{
    if (field == 0) return S.receiver_area;
    if (field == 1) return S.receiver_subarea;
    if (field == 2) return S.receivers.slots - 1u;
    unsigned blocks = 0;
    for (unsigned i = 1; i < S.receivers.slots; ++i)
        blocks += S.receivers.object[i].batches;
    return blocks;
}

void area00_receivers_pending(int pending)
{
    S.pass_count = pending ? 1u : 0u;
    S.pass_frame = 1;
    S.pass[0].cmd_count = pending ? 1u : 0u;
}

int area00_receivers_check(const char *path, unsigned objects, unsigned blocks)
{
    EmShadowReceivers r = {0};
    if (em_shadow_receivers_load(&r, path) < 0) return -1;
    unsigned total = 0;
    int ok = r.slots == objects + 1u;
    for (unsigned i = 1; i < r.slots; ++i) {
        const EmShadowReceiverObject *o = em_shadow_receivers_object(&r, (int32_t)i);
        if (!o || !o->qwords || !o->qw3) ok = 0;
        else total += o->batches;
    }
    ok = ok && total == blocks && r.grid_words != 0;
    em_shadow_receivers_free(&r);
    return ok ? 0 : -1;
}
