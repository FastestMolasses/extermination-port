#include "game/em_shadow_original.h"
#include "em_gfx.h"

unsigned area00_texture_capacity(void) { return EM_GFX_OBJECT_TEX_MAX; }

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
