/* Device registry contract for the shared world catalog. The production
 * Metal registry copies pixels; this bridge records their exact hash and
 * keys so delivery/invalidation can be tested without a GPU. */
#include "game/em_world_textures_live.h"
#include <string.h>

typedef struct { uint64_t key; uint32_t width, height, hash; } Entry;
static struct { Entry entries[EM_GFX_OBJECT_TEX_MAX]; unsigned count, resets, uploads; } devices[2];
static int reject_at = -1;

static unsigned device(EmGfx *g) { return (unsigned)(uintptr_t)g - 1u; }
int em_gfx_world_textures_reset(EmGfx *g)
{
    const unsigned d = device(g);
    if (d >= 2u) return -1;
    devices[d].count = 0;
    devices[d].resets++;
    return 0;
}
int em_gfx_gs_texture(EmGfx *g, uint64_t key, const uint8_t *p, uint32_t w, uint32_t h)
{
    const unsigned d = device(g);
    if (d >= 2u || devices[d].count >= EM_GFX_OBJECT_TEX_MAX) return -1;
    if (reject_at == (int)devices[d].count) return -1;
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < 4u * w * h; ++i) hash = (hash ^ p[i]) * 16777619u;
    devices[d].entries[devices[d].count++] = (Entry){key, w, h, hash};
    devices[d].uploads++;
    return 0;
}
unsigned texture_test_count(unsigned d) { return devices[d].count; }
unsigned texture_test_resets(unsigned d) { return devices[d].resets; }
unsigned texture_test_uploads(unsigned d) { return devices[d].uploads; }
const Entry *texture_test_entry(unsigned d, unsigned i) { return &devices[d].entries[i]; }
void texture_test_reject(int at) { reject_at = at; }
