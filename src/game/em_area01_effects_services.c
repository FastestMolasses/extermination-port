#include "game/em_area01_effects_services.h"
#include "game/em_effects_live.h"

#include <string.h>

static int vector(const EmArea01RuntimeHost *h, uint32_t address, float out[4])
{
    if (!address || (address & 15) || !h || !h->bytes) return -1;
    const uint8_t *p = h->bytes(h->ctx, address, 16, 0);
    if (!p) return -1;
    memcpy(out, p, 16);
    return 0;
}

int em_area01_effects_services_prepare(const EmArea01RuntimeHost *h, const EmArea01Call *c,
                                      EmArea01EffectsSpawn *s)
{
    if (!c || !s) return -1;
    if (c->function != 0x001EFD90u && c->function != 0x001EF9D0u && c->function != 0x001EFD20u)
        return 1;
    if (c->na != (c->function == 0x001EFD90u ? 3u : 2u) ||
        c->nf != (c->function == 0x001EF9D0u ? 1u : 0u)) return -1;
    memset(s, 0, sizeof *s);
    s->function = c->function; s->id = (uint32_t)c->a[0]; s->f12 = c->f[0];
    /* EF9D0 explicitly accepts a null position. The vector wrappers require
     * readable aligned sources; fail-stop rather than changing QW semantics. */
    if ((uint32_t)c->a[1]) {
        if (vector(h, (uint32_t)c->a[1], s->position) < 0) return -1;
        s->has_position = 1;
    } else if (c->function != 0x001EF9D0u) return -1;
    if (c->function == 0x001EFD90u && vector(h, (uint32_t)c->a[2], s->rotation) < 0) return -1;
    return 0;
}

int em_area01_effects_services_invoke(const EmArea01EffectsSpawn *s, EmArea01Call *c)
{
    if (!s || !c || s->function != c->function) return -1;
    uint32_t node = 0;
    const float *p = s->has_position ? s->position : NULL;
    int rc;
    if (c->function == 0x001EFD90u) {
        if (!p) return -1;
        rc = em_effects_live_001EFD90_result(s->id, p, s->rotation, &node);
    } else if (c->function == 0x001EFD20u)
        rc = p ? em_effects_live_001EFD20_result(s->id, p, &node) : -1;
    else if (c->function == 0x001EF9D0u) rc = em_effects_live_001EF9D0(s->id, p, s->f12, &node);
    else return -1;
    if (rc < 0) return -1;
    c->v0 = node;
    return 0;
}

int em_area01_effects_services_call(const EmArea01RuntimeHost *h, EmArea01Call *c)
{
    EmArea01EffectsSpawn s;
    int rc = em_area01_effects_services_prepare(h, c, &s);
    return rc ? rc : em_area01_effects_services_invoke(&s, c);
}

int em_area01_effects_services_project(EmActor *actor,
                                     EmArea01ActorSpan spans[EM_AREA01_ACTOR_SHARED_MAX])
{
    uint32_t node;
    if (!spans) return -1;
    if (!em_effects_live_node_identity(actor, &node)) return 0;
    /* These are only fields owned by the effect slot. All header/pose
     * fields already live in EmActor; OTHER scratch is already its own. */
    static const struct { uint16_t offset, size; } fields[] = {
        {0x24,4},{0x28,4},{0x38,4},{0xA0,16},{0xD0,64},
        {0x1F0,16},{0x244,4},{0x24C,4}
    };
    int n = 0;
    for (unsigned i = 0; i < sizeof fields/sizeof fields[0]; ++i) {
        uint16_t at = fields[i].offset, size = fields[i].size;
        uint8_t *p = em_effects_live_node_field(node+at, size, 0);
        int mode = 1;
        if (at == 0x24 && !p) { p = em_effects_live_node_field(node+at, size, 1); mode = 2; }
        if (at == 0x1F0 && !p) { size = 4; p = em_effects_live_node_field(node+at, size, 0); }
        if (!p) continue;
        if (at >= 0x1F0 && p == actor->scratch + (at-0x1F0)) continue;
        spans[n++] = (EmArea01ActorSpan){at, size, p, mode};
    }
    return n;
}

int em_area01_effects_services_written(void *ctx, EmActor *actor, uint16_t offset, uint16_t size)
{
    (void)ctx;
    uint32_t node;
    if (offset != 0x24 || size != 4 || !em_effects_live_node_identity(actor, &node)) return -1;
    return em_effects_live_node_written(node+offset, size);
}
