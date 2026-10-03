/* em_bone_burst.c - the bone-burst effect node's slots (see
 * em_bone_burst.h, docs/DAMAGE.md section 3).
 *
 * Nothing here computes: 001AF780 / 001AF800 are em_roger_actor's
 * translations on the one slot stack. */
#include "game/em_bone_burst.h"

#include <stdio.h>
#include <string.h>

#include "game/em_area11_boxes.h"
#include "game/em_roger_actor_original.h"

typedef uint32_t u32;

#define SLOTS_MAX 5u                 /* 0022BBC0 state 0: 0022B700(seq, 5) */

typedef struct {
    uint32_t generation;
    int live;
    u32 word[SLOTS_MAX];             /* +0x110: the popped slot addresses */
    unsigned held;
} Burst;

static struct {
    EmActorPool *pool;
    EmSceneState *scene;
    int attached;
    u32 fault;
    Burst rec[EM_ACTOR_POOL_CAPACITY];
    Burst *current;
    EmRogerActor stack;              /* 001AF780 / 001AF800 on the one stack */
} S;

static int fail(u32 address, const char *what)
{
    if (!S.fault) {
        S.fault = address ? address : EM_BONE_BURST_CALLBACK;
        fprintf(stderr, "bone burst node: %s at %08X (stack %08X/%d)\n", what, (unsigned)S.fault,
                (unsigned)S.stack.fault.address, (int)S.stack.fault.code);
    }
    return -1;
}

uint32_t em_bone_burst_fault(void) { return S.fault; }

static int index_of(const EmActor *a)
{
    if (!S.pool || !a || a < S.pool->records || a >= S.pool->records + EM_ACTOR_POOL_CAPACITY) return -1;
    return (int)(a - S.pool->records);
}

static Burst *burst_of(const EmActor *a)
{
    const int i = index_of(a);
    if (i < 0 || !a->allocated) return NULL;
    Burst *t = &S.rec[i];
    return t->live && t->generation == a->generation ? t : NULL;
}

int em_bone_burst_attach(EmActorPool *pool, EmSceneState *scene)
{
    S.attached = 0;
    const EmRogerActorWorld *slots = em_area11_boxes_slot_world();
    if (!pool || !scene || !slots || !slots->d00275BCC || !slots->slots) {
        fprintf(stderr, "bone burst node: the bone-slot stack is missing\n");
        return -1;
    }
    memset(S.rec, 0, sizeof S.rec);
    S.pool = pool;
    S.scene = scene;
    S.fault = 0;
    S.current = NULL;
    memset(&S.stack, 0, sizeof S.stack);
    S.stack.world = *slots;
    S.attached = 1;
    return 0;
}

int em_bone_burst_set_current(EmActor *actor)
{
    if (!actor) {
        S.current = NULL;
        return 0;
    }
    const int i = index_of(actor);
    if (!S.attached || S.fault || i < 0 || !actor->allocated || actor->callback != EM_BONE_BURST_CALLBACK)
        return fail(EM_BONE_BURST_CALLBACK, "no bone-burst node to run");
    Burst *t = &S.rec[i];
    if (!t->live || t->generation != actor->generation) {
        /* A new record: 001EF9D0 allocated it with +0x09 = 0 (no slots). */
        if (actor->bones) return fail(EM_BONE_BURST_CALLBACK, "a new bone-burst node already holds slots");
        memset(t, 0, sizeof *t);
        t->generation = actor->generation;
        t->live = 1;
    }
    S.current = t;
    return 0;
}

int em_bone_burst_owns(const EmActor *actor) { return S.attached && burst_of(actor) != NULL; }

static void *span(u32 address, size_t size, u32 base, size_t count, void *bytes)
{
    return bytes && size && address >= base && size <= count && (uint64_t)address + size <= (uint64_t)base + count
               ? (uint8_t *)bytes + (address - base)
               : NULL;
}

static uint8_t *slot_bytes(u32 word)
{
    const EmRogerActorWorld *w = &S.stack.world;
    if (!w->slots || word < w->slots_base) return NULL;
    const u32 at = word - w->slots_base;
    if (at % EM_ROGER_ACTOR_SLOT_BYTES || at > w->slots_size - EM_ROGER_ACTOR_SLOT_BYTES) return NULL;
    return w->slots + at;
}

void *em_bone_burst_field(uint32_t address, size_t size, int write)
{
    if (!S.attached || S.fault || !size || size > EM_ACTOR_RECORD_SIZE) return NULL;
    for (unsigned i = 0; i < EM_ACTOR_POOL_CAPACITY; ++i) {
        Burst *t = &S.rec[i];
        if (!t->live || !burst_of(&S.pool->records[i])) continue;
        for (unsigned k = 0; k < t->held; ++k) {
            void *p = span(address, size, t->word[k], EM_ROGER_ACTOR_SLOT_BYTES, slot_bytes(t->word[k]));
            if (p) return p;
        }
    }
    if (write || address < EM_ACTOR_POOL_BASE) return NULL;
    const unsigned index = (address - EM_ACTOR_POOL_BASE) / EM_ACTOR_RECORD_SIZE;
    if (index >= EM_ACTOR_POOL_CAPACITY) return NULL;
    Burst *t = burst_of(&S.pool->records[index]);
    if (!t || !t->held) return NULL;
    const u32 base = EM_ACTOR_POOL_BASE + index * EM_ACTOR_RECORD_SIZE;
    return span(address, size, base + 0x110u, 4u * t->held, t->word);
}

size_t em_bone_burst_regions(uint32_t address, EmPoseRegion *out, size_t capacity)
{
    if (!S.attached || address < EM_ACTOR_POOL_BASE || (address - EM_ACTOR_POOL_BASE) % EM_ACTOR_RECORD_SIZE)
        return 0;
    const unsigned index = (address - EM_ACTOR_POOL_BASE) / EM_ACTOR_RECORD_SIZE;
    if (index >= EM_ACTOR_POOL_CAPACITY) return 0;
    Burst *t = burst_of(&S.pool->records[index]);
    if (!t || !t->held) return 0;
    size_t n = 0;
    if (out && n < capacity) out[n] = (EmPoseRegion){address + 0x110u, 4u * t->held, (uint8_t *)t->word, 0};
    ++n;
    for (unsigned k = 0; k < t->held; ++k) {
        uint8_t *b = slot_bytes(t->word[k]);
        if (!b) continue;
        if (out && n < capacity) out[n] = (EmPoseRegion){t->word[k], EM_ROGER_ACTOR_SLOT_BYTES, b, 1};
        ++n;
    }
    return n;
}

int em_bone_burst_call(EmAimFireTargetCall *c)
{
    if (c->function != 0x001AF780u && c->function != 0x001CB5B0u) return 0;
    if (!S.current) return 0;
    if (!S.attached || S.fault) return -1;
    if (c->function == 0x001CB5B0u) return 1;   /* D_00275B40 is the node's +0x110 already */
    /* 001AF780: the slot at the stack cursor (0 while fewer than 31 are
     * free), popped for the node whose behaviour runs. */
    Burst *t = S.current;
    u32 word = 0;
    if (em_roger_actor_001AF780(&S.stack, &word) < 0) return fail(0x001AF780u, "001AF780 faulted");
    c->v0 = word;
    if (!word) return 1;
    if (t->held >= SLOTS_MAX || !slot_bytes(word)) return fail(0x001AF780u, "a slot past the node's five");
    t->word[t->held++] = word;
    return 1;
}

int em_bone_burst_001AF800(EmActor *actor)
{
    Burst *t = S.attached ? burst_of(actor) : NULL;
    if (!t) return 0;
    if (S.fault) return -1;
    /* 001AF800 (em_roger_actor_001AF800, its own slot loop) over the node's
     * +0x09, +0x0C and the +0x110 words it popped. */
    if (actor->bones > t->held) return fail(0x001AF800u, "001AF800: +0x09 names a slot the node did not pop");
    EmRogerActorRecord v;
    memset(&v, 0, sizeof v);
    v.address = em_actor_pool_address(S.pool, actor);
    v.bones_held = actor->bones;
    v.bone_count = actor->u0A[2];
    for (unsigned k = 0; k < t->held; ++k) v.bone[k] = t->word[k];
    if (em_roger_actor_001AF800(&S.stack, &v) < 0) return fail(0x001AF800u, "001AF800 faulted");
    actor->bones = v.bones_held;
    actor->u0A[2] = v.bone_count;
    memset(t, 0, sizeof *t);
    return 1;
}
