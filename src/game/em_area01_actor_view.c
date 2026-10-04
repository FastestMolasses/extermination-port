#include "game/em_area01_actor_view.h"

#include <stddef.h>
#include <string.h>

/* Exactly the fields serialized by em_actor_pool_record_image. +34 and
 * +36 alias: unlike the older Roger bridge, the low half is included. */
static const struct { uint16_t at, end; } owned[] = {
    {0x00, 0x20}, {0x2E, 0x38}, {0x52, 0x9B}, {0x9C, 0x9F},
    {0xB0, 0xD0}, {0x1F0, 0x2F0}
};

static int fail(EmArea01ActorView *v, uint32_t address, int code)
{
    if (v && !v->fault) { v->fault = code; v->fault_address = address; }
    return -1;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int represented(unsigned offset)
{
    for (unsigned i = 0; i < sizeof owned / sizeof owned[0]; ++i)
        if (offset >= owned[i].at && offset < owned[i].end) return 1;
    return 0;
}

static int projections(EmArea01ActorView *v, EmActor *a, EmArea01ActorSpan spans[EM_AREA01_ACTOR_SHARED_MAX],
                        uint8_t mask[EM_ACTOR_RECORD_SIZE], int observation)
{
    memset(mask, 0, EM_ACTOR_RECORD_SIZE);
    int n = v->project ? v->project(v->ctx, a, spans) : 0;
    uint32_t base = em_actor_pool_address(v->pool, a);
    if (n < 0 || n > EM_AREA01_ACTOR_SHARED_MAX) return observation ? -1 : fail(v, base, 4);
    for (int i = 0; i < n; ++i) {
        const EmArea01ActorSpan *s = &spans[i];
        if (!s->bytes || !s->size || s->offset >= EM_ACTOR_RECORD_SIZE ||
            s->size > EM_ACTOR_RECORD_SIZE - s->offset || s->writable < 0 || s->writable > 2 ||
            (s->writable == 2 && (!v->written || s->size != 4 || (s->offset & 3))))
            return observation ? -1 : fail(v, base + s->offset, 4);
        for (unsigned j = s->offset; j < s->offset + s->size; ++j) {
            if ((represented(j) && j < 0x1F0) || mask[j]) return observation ? -1 : fail(v, base + j, 4);
            mask[j] = (uint8_t)(s->writable + 1);
        }
    }
    return n;
}

void em_area01_actor_view_reset(EmArea01ActorView *v, EmActorPool *pool,
                                EmArea01ActorProject project, EmArea01ActorRebind rebind, void *ctx)
{
    if (!v) return;
    memset(v, 0, sizeof *v);
    v->pool = pool; v->project = project; v->rebind = rebind; v->ctx = ctx;
}

int em_area01_actor_view_begin(EmArea01ActorView *v)
{
    if (!v || v->fault) return -1;
    if (!v->pool || v->active) return fail(v, EM_ACTOR_POOL_BASE, 1);
    for (unsigned i = 0; i < EM_ACTOR_POOL_CAPACITY; ++i) v->record[i].touched = 0;
    v->active = 1;
    return 0;
}

static int refresh(EmArea01ActorView *v, unsigned index)
{
    EmActor *a = &v->pool->records[index];
    EmArea01ActorRecordView *r = &v->record[index];
    uint32_t base = EM_ACTOR_POOL_BASE + index * EM_ACTOR_RECORD_SIZE;
    if (!a->allocated || a->self != a) return fail(v, base, 2);
    uint8_t image[EM_ACTOR_RECORD_SIZE];
    em_actor_pool_record_image(v->pool, a, image);
    for (unsigned i = 0; i < sizeof owned / sizeof owned[0]; ++i)
        memcpy(r->image + owned[i].at, image + owned[i].at, owned[i].end - owned[i].at);
    EmArea01ActorSpan spans[EM_AREA01_ACTOR_SHARED_MAX];
    int n = projections(v, a, spans, r->shared, 0);
    if (n < 0) return -1;
    for (int i = 0; i < n; ++i) memcpy(r->image + spans[i].offset, spans[i].bytes, spans[i].size);
    memcpy(r->before, r->image, sizeof r->before);
    memset(r->written, 0, sizeof r->written);
    r->generation = a->generation;
    r->touched = 1;
    return 0;
}

int em_area01_actor_view_touch(EmArea01ActorView *v, uint32_t node)
{
    if (!v || v->fault) return -1;
    if (!v->active || !v->pool || node < EM_ACTOR_POOL_BASE ||
        (node - EM_ACTOR_POOL_BASE) % EM_ACTOR_RECORD_SIZE ||
        (node - EM_ACTOR_POOL_BASE) / EM_ACTOR_RECORD_SIZE >= EM_ACTOR_POOL_CAPACITY)
        return fail(v, node, 1);
    unsigned i = (node - EM_ACTOR_POOL_BASE) / EM_ACTOR_RECORD_SIZE;
    EmActor *a = &v->pool->records[i];
    if (!v->record[i].touched && refresh(v, i) < 0) return -1;
    return a->allocated && a->self == a && a->generation == v->record[i].generation ?
        0 : fail(v, node, 2);
}

uint8_t *em_area01_actor_view_bytes(EmArea01ActorView *v, uint32_t address, uint32_t size, int write)
{
    if (!v || v->fault) return NULL;
    if (address < EM_ACTOR_POOL_BASE || address - EM_ACTOR_POOL_BASE >=
        EM_ACTOR_POOL_CAPACITY * EM_ACTOR_RECORD_SIZE) return NULL;
    if (!v->active || !v->pool || !size) { fail(v, address, 1); return NULL; }
    unsigned delta = address - EM_ACTOR_POOL_BASE;
    unsigned index = delta / EM_ACTOR_RECORD_SIZE, offset = delta % EM_ACTOR_RECORD_SIZE;
    if (size > EM_ACTOR_RECORD_SIZE - offset) { fail(v, address, 1); return NULL; }
    EmActor *a = &v->pool->records[index];
    EmArea01ActorRecordView *r = &v->record[index];
    if (em_area01_actor_view_touch(v, address - offset) < 0) return NULL;
    if (!a->allocated || a->self != a || a->generation != r->generation) {
        fail(v, address, 2); return NULL;
    }
    if (write) for (unsigned i = offset; i < offset + size; ++i) {
        if ((i >= 0x14 && i < 0x20) || r->shared[i] == 1) {
            fail(v, address, i >= 0x14 && i < 0x20 ? 3 : 4); return NULL;
        }
    }
    for (unsigned i = offset; i < offset + size; ++i) if (r->shared[i] == 3 && !r->written[i]) {
        if (!write || size != 4 || (offset & 3) || r->shared[offset] != 3 ||
            r->shared[offset+3] != 3) { fail(v, address, 4); return NULL; }
    }
    if (write) for (unsigned i = offset; i < offset + size; ++i)
        if (r->shared[i] == 3) r->written[i] = 1;
    return r->image + offset;
}

static void commit_actor(EmActor *a, const uint8_t *p, const uint8_t *shared)
{
    a->status = p[0]; a->drawn = p[1]; a->cls = p[2]; a->model = p[3];
    memcpy(a->u04, p + 4, sizeof a->u04); a->bones = p[9];
    memcpy(a->u0A, p + 0xA, sizeof a->u0A); a->param = p[0xD]; a->uid = rd16(p + 0xE);
    /* +10 is changed only by the registered behavior binder; +14..1F
     * remain exclusively the native pool's links. */
    a->flags2 = rd16(p + 0x2E); a->w30 = rd32(p + 0x30);
    a->w34 = rd32(p + 0x34); a->h36 = rd16(p + 0x36);
    a->h52 = rd16(p + 0x52); a->kind = rd16(p + 0x54); a->link = rd16(p + 0x56);
    a->w58 = rd32(p + 0x58); a->w5C = rd32(p + 0x5C);
    memcpy(a->f60, p + 0x60, sizeof a->f60); memcpy(a->f80, p + 0x80, sizeof a->f80);
    a->w90 = rd32(p + 0x90); a->h94 = (int16_t)rd16(p + 0x94); a->h96 = (int16_t)rd16(p + 0x96);
    a->b98 = p[0x98]; a->b99 = p[0x99]; a->table_index = p[0x9A];
    a->b9C = p[0x9C]; a->b9D = p[0x9D]; a->b9E = p[0x9E];
    memcpy(a->pos, p + 0xB0, sizeof a->pos); memcpy(a->rot, p + 0xC0, sizeof a->rot);
    for (unsigned i = 0; i < sizeof a->scratch; ++i)
        if (!shared[0x1F0+i]) a->scratch[i] = p[0x1F0+i];
}

int em_area01_actor_view_commit(EmArea01ActorView *v)
{
    if (!v || v->fault) return -1;
    if (!v->active || !v->pool) return fail(v, EM_ACTOR_POOL_BASE, 1);
    for (unsigned index = 0; index < EM_ACTOR_POOL_CAPACITY; ++index) {
        EmArea01ActorRecordView *r = &v->record[index];
        if (!r->touched) continue;
        EmActor *a = &v->pool->records[index];
        uint32_t base = EM_ACTOR_POOL_BASE + index * EM_ACTOR_RECORD_SIZE;
        if (!a->allocated || a->self != a || a->generation != r->generation) return fail(v, base, 2);
        uint8_t live[EM_ACTOR_RECORD_SIZE], mask[EM_ACTOR_RECORD_SIZE];
        em_actor_pool_record_image(v->pool, a, live);
        if (memcmp(r->image + 0x14, r->before + 0x14, 12)) return fail(v, base + 0x14, 3);
        /* An unbracketed native worker would otherwise have its changes
         * silently overwritten. All represented fields must still agree
         * with the pre-segment view, including links changed by allocation. */
        for (unsigned i = 0; i < EM_ACTOR_RECORD_SIZE; ++i)
            if (represented(i) && !r->shared[i] && live[i] != r->before[i])
                return fail(v, base + i, 1);
        EmArea01ActorSpan spans[EM_AREA01_ACTOR_SHARED_MAX];
        int n = projections(v, a, spans, mask, 0);
        if (n < 0) return -1;
        if (memcmp(mask, r->shared, sizeof mask)) return fail(v, base, 4);
        for (int i = 0; i < n; ++i) {
            const EmArea01ActorSpan *s = &spans[i];
            if (memcmp(s->bytes, r->before + s->offset, s->size) ||
                ((!s->writable || (s->writable == 2 && !r->written[s->offset])) &&
                 memcmp(r->image + s->offset, r->before + s->offset, s->size)))
                return fail(v, base + s->offset, 4);
        }
        uint32_t callback = rd32(r->image + 0x10);
        if (callback != a->callback) {
            if (!v->rebind || v->rebind(v->ctx, a, callback) < 0 || !a->behavior ||
                a->callback != callback || a->generation != r->generation || !a->allocated)
                return fail(v, base + 0x10, 5);
            em_actor_pool_record_image(v->pool, a, live);
            if (memcmp(live + 0x14, r->before + 0x14, 12)) return fail(v, base + 0x14, 3);
            for (unsigned j = 0; j < EM_ACTOR_RECORD_SIZE; ++j)
                if (represented(j) && !r->shared[j] && (j < 0x10 || j >= 0x14) && live[j] != r->before[j])
                    return fail(v, base + j, 5);
        }
        commit_actor(a, r->image, r->shared);
        for (int i = 0; i < n; ++i)
            if (spans[i].writable == 1 || (spans[i].writable == 2 && r->written[spans[i].offset])) {
                memcpy(spans[i].bytes, r->image + spans[i].offset, spans[i].size);
                if (spans[i].writable == 2 && v->written(v->ctx, a, spans[i].offset, spans[i].size) < 0)
                    return fail(v, base + spans[i].offset, 4);
            }
    }
    v->active = 0;
    return 0;
}

int em_area01_actor_view_snapshot(EmArea01ActorView *v, uint32_t address, uint32_t size, void *out)
{
    if (!v || v->fault || v->active || !v->pool || !out || !size || address < EM_ACTOR_POOL_BASE)
        return -1;
    unsigned delta = address - EM_ACTOR_POOL_BASE, index = delta / EM_ACTOR_RECORD_SIZE;
    unsigned offset = delta % EM_ACTOR_RECORD_SIZE;
    if (index >= EM_ACTOR_POOL_CAPACITY || size > EM_ACTOR_RECORD_SIZE-offset) return -1;
    EmActor *a = &v->pool->records[index];
    if (!a->allocated || a->self != a) return -1;
    uint8_t image[EM_ACTOR_RECORD_SIZE], native[EM_ACTOR_RECORD_SIZE], mask[EM_ACTOR_RECORD_SIZE];
    memcpy(image, v->record[index].image, sizeof image);
    em_actor_pool_record_image(v->pool, a, native);
    for (unsigned i = 0; i < sizeof image; ++i) if (represented(i)) image[i] = native[i];
    EmArea01ActorSpan spans[EM_AREA01_ACTOR_SHARED_MAX];
    int n = projections(v, a, spans, mask, 1);
    if (n < 0) return -1;
    for (unsigned i = offset; i < offset+size; ++i) if (mask[i] == 3) return -1;
    for (int i = 0; i < n; ++i) memcpy(image+spans[i].offset, spans[i].bytes, spans[i].size);
    memcpy(out, image+offset, size);
    return 0;
}
