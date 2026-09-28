/* em_face_slot.c - see em_face_slot.h. Nothing here computes: the
 * functions are em_roger_actor_original's and em_opening_face's. */
#include "game/em_face_slot.h"

#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int ready(const EmFaceSlot *s)
{
    return s && s->record && s->actor;
}

/* The typed view of the two fields these functions read or write. */
static void load(const EmFaceSlot *s, EmRogerActorRecord *t)
{
    memset(t, 0, sizeof *t);
    t->address = s->record_address;
    t->face = rd32(s->record + 0x90);
    t->face_bone = (int16_t)(s->record[0x94] | s->record[0x95] << 8);
}

static void store(const EmFaceSlot *s, const EmRogerActorRecord *t)
{
    memcpy(s->record + 0x90, &t->face, 4);
    const uint16_t h = (uint16_t)t->face_bone;
    memcpy(s->record + 0x94, &h, 2);
}

int em_face_slot_001CA700(EmFaceSlot *s, uint32_t resource, int32_t a2, int32_t *result)
{
    if (!ready(s) || !result) return -1;
    EmRogerActorRecord t;
    load(s, &t);
    const int r = em_roger_actor_001CA700(s->actor, &t, resource, a2);
    if (r < 0) return -1;
    store(s, &t);             /* the popped slot, or the 0 the original stores */
    *result = r;
    return 0;
}

int em_face_slot_001D06D0(EmFaceSlot *s, uint32_t a1)
{
    if (!ready(s)) return -1;
    EmRogerActorRecord t;
    load(s, &t);
    return em_roger_actor_001D06D0(s->actor, &t, a1) < 0 ? -1 : 0;
}

int em_face_slot_001D06E0(EmFaceSlot *s, uint32_t a1)
{
    if (!ready(s)) return -1;
    EmRogerActorRecord t;
    load(s, &t);
    return em_roger_actor_001D06E0(s->actor, &t, a1) < 0 ? -1 : 0;
}

int em_face_slot_001CA770(EmFaceSlot *s)
{
    if (!ready(s)) return -1;
    EmRogerActorRecord t;
    load(s, &t);
    if (em_roger_actor_001CA770(s->actor, &t) < 0) return -1;
    store(s, &t);
    return 0;
}

int em_face_slot_001D0C70(EmFaceSlot *s)
{
    if (!ready(s) || !s->random) return -1;
    const EmRogerActorWorld *w = &s->actor->world;
    const uint32_t face = rd32(s->record + 0x90);
    if (!w->slots || face < w->slots_base || face - w->slots_base >= w->slots_size ||
        (face - w->slots_base) % EM_ROGER_ACTOR_SLOT_BYTES)
        return -1;
    em_opening_face_tick_slot(w->slots + (face - w->slots_base), s->random, s->random_context);
    return 0;
}
