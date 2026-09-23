/* AREA11 Roger actor: lifecycle-0 init, face services and the equipment
 * node. See em_roger_actor_original.h and docs/ROGER_ACTOR_ORIGINAL.md.
 *
 * Every routine names the original address it translates and the address
 * of each branch or store it follows where the order matters. VU0 macro
 * operations go through em_ee_float.h under their real forms (op, dest
 * mask, broadcast lane); no host float operation is performed here. */
#include "game/em_roger_actor_original.h"

#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

typedef uint32_t u32;

#define F_ONE UINT32_C(0x3F800000)
#define F_5_4 UINT32_C(0x40ACCCCD) /* 001C5EB0: the far probe x */

/* ======================================================================
 * Fault plumbing
 * ==================================================================== */

static int latched(const EmRogerActor *s)
{
    return s->fault.code != EM_ROGER_ACTOR_FAULT_NONE;
}

static int fault(EmRogerActor *s, u32 address, int32_t code)
{
    if (!latched(s)) {
        s->fault.address = address;
        s->fault.code = code;
    }
    return -1;
}

#define NEED(ptr, address) \
    do { if (!(ptr)) return fault(s, (address), EM_ROGER_ACTOR_FAULT_NULL_WORKER); } while (0)
#define CALL(address, expr) \
    do { if ((expr) < 0) return fault(s, (address), EM_ROGER_ACTOR_FAULT_WORKER_FAILED); } while (0)
#define ENTER(address, rec) \
    do { \
        if (!s) return -1; \
        if (latched(s)) return -1; \
        if (!(rec)) return fault(s, (address), EM_ROGER_ACTOR_FAULT_NULL_WORKER); \
    } while (0)

/* ======================================================================
 * Views
 * ==================================================================== */

/* The bytes [address, address + size) of the slot arena, or NULL. */
static uint8_t *slot_bytes(EmRogerActor *s, u32 address, u32 size)
{
    const EmRogerActorWorld *w = &s->world;
    if (!w->slots || address < w->slots_base) return NULL;
    u32 at = address - w->slots_base;
    if (at > w->slots_size || size > w->slots_size - at) return NULL;
    return w->slots + at;
}

/* The stack word at `address`, or NULL. */
static u32 *stack_word(EmRogerActor *s, u32 address)
{
    const EmRogerActorWorld *w = &s->world;
    if (!w->slot_stack || address < w->slot_stack_base || (address - w->slot_stack_base) & 3u)
        return NULL;
    u32 index = (address - w->slot_stack_base) / 4u;
    return index < w->slot_stack_words ? &w->slot_stack[index] : NULL;
}

static int table_word(EmRogerActor *s, u32 index, u32 *out)
{
    NEED(s->world.d0028A490, 0x0028A490u);
    if (index >= s->world.d0028A490_count)
        return fault(s, 0x0028A490u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    *out = s->world.d0028A490[index];
    return 0;
}

static void put32(uint8_t *p, u32 v) { memcpy(p, &v, 4); }

/* ======================================================================
 * Bone slots: 001AF780 / 001AF890
 * ==================================================================== */

int em_roger_actor_001AF780(EmRogerActor *s, uint32_t *slot)
{
    ENTER(0x001AF780u, slot);
    NEED(s->world.d00275BCC, 0x00275BCCu);
    /* 001AF784: count (signed halfword) < 31 returns 0 and changes nothing. */
    int16_t count = *s->world.d00275BCC;
    if (count < EM_ROGER_ACTOR_SLOT_MIN_FREE) {
        *slot = 0;
        return 0;
    }
    NEED(s->world.d00275BD0, 0x00275BD0u);
    u32 cursor = *s->world.d00275BD0;
    u32 *word = stack_word(s, cursor);
    if (!word) return fault(s, 0x001AF780u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    /* 001AF798..001AF7AC: count -= 1, cursor += 4, return the word at the
     * old cursor. */
    *s->world.d00275BCC = (int16_t)(count - 1);
    *s->world.d00275BD0 = cursor + 4u;
    *slot = *word;
    return 0;
}

int em_roger_actor_001AF890(EmRogerActor *s, uint32_t slot)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    uint8_t *bytes = slot_bytes(s, slot, EM_ROGER_ACTOR_SLOT_BYTES);
    if (!bytes) return fault(s, 0x001AF890u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    NEED(s->world.d00275BD0, 0x00275BD0u);
    NEED(s->world.d00275BCC, 0x00275BCCu);
    u32 cursor = *s->world.d00275BD0 - 4u;
    u32 *word = stack_word(s, cursor);
    if (!word) return fault(s, 0x001AF890u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    /* 001AF890..001AF8AC: 13 quadwords of the slot cleared; then the cursor
     * moves down one word, the slot address is stored there and the count
     * (signed halfword) grows by one. */
    memset(bytes, 0, EM_ROGER_ACTOR_SLOT_BYTES);
    *s->world.d00275BD0 = cursor;
    *word = slot;
    *s->world.d00275BCC = (int16_t)(*s->world.d00275BCC + 1);
    return 0;
}

/* ======================================================================
 * Model binding: 001CA6E0, 001C6150, 001B10B0
 * ==================================================================== */

int em_roger_actor_001CA6E0(EmRogerActor *s, EmRogerActorRecord *a, uint32_t model)
{
    ENTER(0x001CA6E0u, a);
    /* 001CA6E0 tail-calls 001CA5E0(a, model, 0): +0x44 = model, then
     * 001CA5F0(a, 0): kind 0 selects the default draw method 001CAA00. */
    a->model = model;
    a->draw = EM_ROGER_ACTOR_DRAW_001CAA00;
    return 0;
}

/* 001C6150(model): the byte at model +0x08. */
static int model_bone_count(EmRogerActor *s, u32 model, uint8_t *count)
{
    NEED(s->world.resource, 0x001C6150u);
    const uint8_t *bytes = s->world.resource(s->world.resource_ctx, model, 9);
    if (!bytes) return fault(s, 0x001C6150u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    *count = bytes[8];
    return 0;
}

int em_roger_actor_001B10B0(EmRogerActor *s, EmRogerActorRecord *a, uint32_t a1, int32_t a2)
{
    ENTER(0x001B10B0u, a);
    /* Everything this path reads is checked before the first write. */
    u32 model, anim = 0;
    uint8_t count;
    if (table_word(s, a1, &model) < 0) return -1;
    if (a2 != -1 && table_word(s, (u32)a2, &anim) < 0) return -1;
    NEED(s->world.d00275BCC, 0x00275BCCu);
    if (model_bone_count(s, model, &count) < 0) return -1;
    int16_t free_slots = *s->world.d00275BCC;
    if ((int32_t)free_slots >= (int32_t)count) {
        if (count > EM_ROGER_ACTOR_MAX_BONES)
            return fault(s, 0x001B10B0u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
        NEED(s->workers.w_001CB5B0, 0x001CB5B0u);
        /* 001AF780 pops while the count is still >= 31. */
        int pops = 0;
        for (int i = 0; i < (int)count; ++i)
            if ((int32_t)free_slots - i >= EM_ROGER_ACTOR_SLOT_MIN_FREE) ++pops;
        if (pops) {
            NEED(s->world.d00275BD0, 0x00275BD0u);
            for (int j = 0; j < pops; ++j)
                if (!stack_word(s, *s->world.d00275BD0 + 4u * (u32)j))
                    return fault(s, 0x001AF780u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
        }
    }
    /* 001B10B0: 001CA6E0(a, D_0028A490[a1]); a2 != -1: +0x40 = D_0028A490[a2];
     * +0x0C = 001C6150(+0x44). */
    if (em_roger_actor_001CA6E0(s, a, model) < 0) return -1;
    if (a2 != -1) a->anim = anim;
    a->bone_count = count;
    /* Signed halfword cap < +0x0C: +0x04 = 3, return 1. */
    if ((int32_t)*s->world.d00275BCC < (int32_t)a->bone_count) {
        a->lifecycle = 3;
        return 1;
    }
    /* +0x110 slot i = 001AF780() while i < +0x0C (re-read every pass). */
    uint8_t n;
    for (int i = 0; i < (int)(n = a->bone_count); ++i) {
        u32 slot;
        if (em_roger_actor_001AF780(s, &slot) < 0) return -1;
        a->bone[i] = slot;
    }
    a->bones_held = n;                              /* +0x09 = the last +0x0C read */
    CALL(0x001CB5B0u, s->workers.w_001CB5B0(s->workers.ctx, a->bone_count));
    return 0;
}

/* ======================================================================
 * Face slot: 001D0690, 001CA700, 001D06D0, 001D06E0, 001CA770
 * ==================================================================== */

int em_roger_actor_001D0690(EmRogerActor *s, uint32_t record)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    uint8_t *r = slot_bytes(s, record, 0x38);
    if (!r) return fault(s, 0x001D0690u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    /* Byte +0x10, words +0x00, +0x08, +0x14, +0x1C, +0x18, then the six
     * words +0x20..+0x34 (the slot's +0x80, +0x70, +0x78, +0x84, +0x8C,
     * +0x88 and +0x90..+0xA7). +0x04, +0x0C and +0x11 are kept. */
    r[0x10] = 0;
    put32(r + 0x00, 0);
    put32(r + 0x08, 0);
    put32(r + 0x14, 0);
    put32(r + 0x1C, 0);
    put32(r + 0x18, 0);
    for (int i = 0; i < 6; ++i) put32(r + 0x20 + 4 * i, 0);
    return 0;
}

int em_roger_actor_001CA700(EmRogerActor *s, EmRogerActorRecord *a, uint32_t resource, int32_t a2)
{
    ENTER(0x001CA700u, a);
    u32 face = a->face;
    if (face == 0) {
        /* 001CA728: +0x90 = 001AF780(); 0 returns 0 (the 0 is stored). */
        if (em_roger_actor_001AF780(s, &face) < 0) return -1;
        a->face = face;
        if (face == 0) return 0;
    }
    uint8_t *slot = slot_bytes(s, face, EM_ROGER_ACTOR_SLOT_BYTES);
    if (!slot) return fault(s, 0x001CA700u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    a->face_bone = (int16_t)a2;                     /* 001CA740: +0x94 (halfword) */
    put32(slot + 0x60, resource);                   /* 001CA74C: slot +0x60 */
    if (em_roger_actor_001D0690(s, face + 0x70u) < 0) return -1;
    return 1;
}

int em_roger_actor_001D06D0(EmRogerActor *s, EmRogerActorRecord *a, uint32_t a1)
{
    ENTER(0x001D06D0u, a);
    uint8_t *slot = slot_bytes(s, a->face, EM_ROGER_ACTOR_SLOT_BYTES);
    if (!slot) return fault(s, 0x001D06D0u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    slot[0x81] = (uint8_t)a1;                       /* *(+0x90) +0x81 = a1 */
    return 0;
}

int em_roger_actor_001D06E0(EmRogerActor *s, EmRogerActorRecord *a, uint32_t a1)
{
    ENTER(0x001D06E0u, a);
    uint8_t *slot = slot_bytes(s, a->face, EM_ROGER_ACTOR_SLOT_BYTES);
    if (!slot) return fault(s, 0x001D06E0u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    slot[0x80] = (uint8_t)a1;                       /* *(+0x90) +0x80 = a1 */
    if (a1 == 0)                                    /* then +0x90..+0xA7 = 0 */
        for (int i = 0; i < 6; ++i) put32(slot + 0x90 + 4 * i, 0);
    return 0;
}

int em_roger_actor_001CA770(EmRogerActor *s, EmRogerActorRecord *a)
{
    ENTER(0x001CA770u, a);
    if (a->face != 0) {
        if (em_roger_actor_001AF890(s, a->face) < 0) return -1;
        a->face = 0;                                /* +0x90 = 0 */
        a->face_bone = -1;                          /* +0x94 = -1 */
    }
    return 0;
}

int em_roger_actor_001D8BF0(EmRogerActor *s, EmRogerActorRecord *a, int32_t a1)
{
    ENTER(0x001D8BF0u, a);
    if (a1 != 0) a->cls = (uint8_t)(a->cls | 0x20);
    else a->cls = (uint8_t)(a->cls & 0xDF);
    return 0;
}

/* ======================================================================
 * 001BA8E0: first-tick face attach
 * ==================================================================== */

/* The compare chain 001BA934..001BAA44 and its targets, as read off the
 * instructions (the delay-slot index loads belong to the taken branch). */
typedef struct {
    uint8_t kind;
    uint8_t index;   /* D_0028A490 index for 001CA700 */
    uint8_t a2;      /* 001CA700 a2 (+0x94) */
    uint8_t shadow;  /* +0x96 */
    uint8_t speed;   /* 001D06D0 a1 (slot +0x81) */
} AttachRow;

static const AttachRow k_attach[] = {
    {0x68, 0x8D, 6, 0x31, 0}, {0x66, 0x92, 6, 0x30, 1}, {0x64, 0x8C, 6, 0x2F, 1},
    {0x61, 0x8B, 6, 0x2E, 1}, {0x5E, 0x91, 7, 0x2D, 1}, {0x5D, 0x8A, 7, 0x2D, 1},
    {0x5A, 0x94, 7, 0x2C, 0}, {0x59, 0x90, 7, 0x2C, 0}, {0x55, 0x8F, 7, 0x2B, 1},
    {0x54, 0x89, 7, 0x2B, 1}, {0x51, 0x95, 7, 0x2A, 1}, {0x50, 0x93, 7, 0x2A, 1},
    {0x4F, 0x8E, 7, 0x2A, 1}, {0x49, 0x88, 7, 0x29, 1}, {0x48, 0x88, 7, 0x29, 1},
    {0x47, 0x88, 7, 0x29, 1}, {0x40, 0x1B, 7, 0x00, 1}, {0x3F, 0x1A, 7, 0x00, 1},
    {0x3E, 0x19, 7, 0x00, 1}, {0x3B, 0x18, 7, 0x00, 1},
};

static const AttachRow *attach_row(uint32_t kind)
{
    for (size_t i = 0; i < sizeof k_attach / sizeof k_attach[0]; ++i)
        if (k_attach[i].kind == kind) return &k_attach[i];
    return NULL;
}

int em_roger_actor_001BA8E0(EmRogerActor *s, EmRogerActorRecord *a, uint32_t kind)
{
    ENTER(0x001BA8E0u, a);
    NEED(s->world.d00810788, 0x00810788u);
    /* 001BA8F8..001BA924: 001F0120(a, kind) unless D_00810788 == 1 and
     * kind != 0x3B (then no call at all). */
    int spawn = *s->world.d00810788 != 1 || kind == 0x3B;
    const AttachRow *row = (kind == 0x6C || kind == 0x6A) ? NULL : attach_row(kind);
    u32 resource = 0;
    /* Checked before the first call or write. */
    if (spawn) NEED(s->workers.w_001F0120, 0x001F0120u);
    if (row && table_word(s, row->index, &resource) < 0) return -1;
    if (spawn) CALL(0x001F0120u, s->workers.w_001F0120(s->workers.ctx, a->w14, (int32_t)kind));
    /* 001BA928: 001D8BF0(a, 1) with a constant 1, whatever the kind. */
    if (em_roger_actor_001D8BF0(s, a, 1) < 0) return -1;
    if (kind == 0x6C || kind == 0x6A) {
        /* 001BAB68 / 001BAB74: +0x96 = 0x33 / 0x34, +0x56 = 0. */
        a->shadow_kind = kind == 0x6C ? 0x34 : 0x33;
        a->face_active = 0;
        return 0;
    }
    if (!row) {
        /* 001BAB88: index -1, +0x56 = 0; +0x96 untouched. */
        a->face_active = 0;
        return 0;
    }
    a->shadow_kind = row->shadow;                   /* 001BABA0: +0x96 */
    int r = em_roger_actor_001CA700(s, a, resource, row->a2);
    if (r < 0) return -1;
    if (r != 0) {
        if (em_roger_actor_001D06D0(s, a, row->speed) < 0) return -1;
        a->face_active = 1;                         /* 001BABDC */
    } else {
        a->face_active = 0;                         /* 001BABE0 */
    }
    return 0;
}

/* ======================================================================
 * 001BA580: face update; 001BA540: face release
 * ==================================================================== */

/* The category chain 001BA590..001BA6A0: activity index per kind; -1 none.
 * 0x6C and 0x6A are handled first (001DA6A0 only). */
static int activity_index(uint32_t kind)
{
    switch (kind) {
    case 0x68: return 9;
    case 0x66: return 8;
    case 0x64: return 7;
    case 0x61: return 6;
    case 0x5E: case 0x5D: return 5;
    case 0x5A: case 0x59: return 4;
    case 0x55: case 0x54: return 3;
    case 0x51: case 0x50: case 0x4F: return 2;
    case 0x49: case 0x48: case 0x47: return 1;
    case 0x40: case 0x3F: case 0x3E: case 0x3B: return 0;
    default: return -1;
    }
}

int em_roger_actor_001BA580(EmRogerActor *s, EmRogerActorRecord *a, uint32_t kind)
{
    ENTER(0x001BA580u, a);
    if (kind == 0x6C || kind == 0x6A) {
        /* 001BA72C: 001DA6A0(a) and return, whatever +0x56 holds. */
        NEED(s->workers.w_001DA6A0, 0x001DA6A0u);
        CALL(0x001DA6A0u, s->workers.w_001DA6A0(s->workers.ctx, a));
        return 0;
    }
    int index = activity_index(kind);
    int alternative = 0;
    if (kind == 0x61) {
        /* 001BA6EC: category 6 takes 001BA7F0 when D_00810700 == 0xD. */
        NEED(s->world.d00810700, 0x00810700u);
        alternative = *s->world.d00810700 == 0xD;
    }
    /* 001BA744: nothing more unless +0x56 (signed halfword) != 0 and the
     * kind has a category. */
    if (a->face_active == 0 || index == -1) return 0;
    /* Checked before the first call or write. */
    if (alternative) NEED(s->workers.w_001BA7F0, 0x001BA7F0u);
    else NEED(s->workers.w_001DA6A0, 0x001DA6A0u);
    NEED(s->world.d008106D4, 0x008106D4u);
    NEED(s->workers.w_001D0720, 0x001D0720u);
    uint8_t *activity = &s->world.d008106D4[index];
    if ((*activity == 1 || *activity == 2) && !slot_bytes(s, a->face, EM_ROGER_ACTOR_SLOT_BYTES))
        return fault(s, 0x001D06E0u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    if (alternative) CALL(0x001BA7F0u, s->workers.w_001BA7F0(s->workers.ctx, a));
    else CALL(0x001DA6A0u, s->workers.w_001DA6A0(s->workers.ctx, a));
    /* 001BA780: the activity byte D_008106D4[index]: 1 = talking on,
     * 2 = talking off; either is consumed (stored 0) after 001D06E0. */
    if (*activity == 1) {
        if (em_roger_actor_001D06E0(s, a, 1) < 0) return -1;
        *activity = 0;
    } else if (*activity == 2) {
        if (em_roger_actor_001D06E0(s, a, 0) < 0) return -1;
        *activity = 0;
    }
    /* 001BA7CC: 001D0C70(a), the tail call of the face kernel 001D0720. */
    CALL(0x001D0720u, s->workers.w_001D0720(s->workers.ctx, a));
    return 0;
}

int em_roger_actor_001BA540(EmRogerActor *s, EmRogerActorRecord *a)
{
    ENTER(0x001BA540u, a);
    if (a->face_active != 0 && em_roger_actor_001CA770(s, a) < 0) return -1;
    return em_roger_actor_001D8BF0(s, a, 0);
}

/* ======================================================================
 * 008237E0 lifecycle 0
 * ==================================================================== */

int em_roger_actor_001BA1C0(EmRogerActor *s, uint32_t a1)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    NEED(s->world.d00810758, 0x00810758u);
    /* Only entry 0 is modelled (the one 008237E0 reads). */
    if (a1 != 0) return fault(s, 0x001BA1C0u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    return s->world.d00810758[a1] == 0xFF;
}

int em_roger_actor_008237E0_init(EmRogerActor *s, EmRogerActorRecord *roger)
{
    ENTER(EM_ROGER_ACTOR_CONTROLLER, roger);
    if (roger->lifecycle != 0)
        return fault(s, EM_ROGER_ACTOR_CONTROLLER, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    /* 00823824: 001BA1C0(roger, 0) set -> +0x04 = 3 and nothing else. */
    int gone = em_roger_actor_001BA1C0(s, 0);
    if (gone < 0) return -1;
    if (gone) {
        roger->lifecycle = 3;
        return 0;
    }
    /* The workers and views the path reaches unconditionally, checked
     * before the first write (the callees check their own data again). */
    u32 w58;
    NEED(s->workers.w_001C63E0, 0x001C63E0u);
    NEED(s->world.d00810788, 0x00810788u);
    if (*s->world.d00810788 != 1 || roger->kind == 0x3B) NEED(s->workers.w_001F0120, 0x001F0120u);
    /* Bounds / NULL pre-check only; the word itself is read after
     * 001BA8E0 returns, where the original loads it (008238A4..008238B0). */
    if (table_word(s, EM_ROGER_ACTOR_W58_INDEX, &w58) < 0) return -1;
    /* 0082384C: 001B10B0(roger, +0x0D, 0x4A). Its result is not read: over
     * the bone cap the +0x04 = 3 it stores is overwritten below. */
    if (em_roger_actor_001B10B0(s, roger, roger->kind, EM_ROGER_ACTOR_BANK_INDEX) < 0) return -1;
    /* 00823858: bone_init_default_2(roger, 8). */
    CALL(0x001C63E0u, s->workers.w_001C63E0(s->workers.ctx, roger, EM_ROGER_ACTOR_CLIP));
    roger->pose_bone = EM_ROGER_ACTOR_POSE_BONE;    /* 00823864: 001CA6F0(roger, 2) */
    if (em_roger_actor_001BA8E0(s, roger, roger->kind) < 0) return -1;   /* 00823870 */
    if (table_word(s, EM_ROGER_ACTOR_W58_INDEX, &w58) < 0) return -1;
    roger->descriptor = EM_ROGER_ACTOR_DESCRIPTOR;  /* 00823880: +0x30 */
    roger->w58 = w58;                               /* 00823890: +0x58 = word at 0028A5C4 */
    roger->lifecycle = 1;                           /* 00823894 */
    roger->status = 1;                              /* 0082389C */
    return 0;
}

/* ======================================================================
 * 001C5C90: the equipment node
 * ==================================================================== */

static const u32 VF0[4] = {0, 0, 0, F_ONE};

#define VU(expr) \
    do { if ((expr) != EM_EE_FLOAT_OK) return fault(s, 0x001C5C90u, EM_ROGER_ACTOR_FAULT_UNMEASURED_FORM); } while (0)

/* 001026A0(out, m, v): ACC = row0 * v.x; ACC += row1 * v.y; ACC += row2 * v.z;
 * out = ACC + row3 * v.w (all four lanes). */
static int sdk_001026A0(EmRogerActor *s, u32 out[4], const u32 m[16], const u32 v[4])
{
    u32 acc[4] = {0, 0, 0, 0}, r[4] = {0, 0, 0, 0};
    VU(em_vu_vec_bits(EM_VU_MULABC, 15, 0, m, v, 0, NULL, acc));
    VU(em_vu_vec_bits(EM_VU_MADDABC, 15, 1, m + 4, v, 0, acc, acc));
    VU(em_vu_vec_bits(EM_VU_MADDABC, 15, 2, m + 8, v, 0, acc, acc));
    VU(em_vu_vec_bits(EM_VU_MADDBC, 15, 3, m + 12, v, 0, acc, r));
    memcpy(out, r, sizeof r);
    return 0;
}

/* 001028D0(out, a, b): out = a - b (all four lanes). */
static int sdk_001028D0(EmRogerActor *s, u32 out[4], const u32 a[4], const u32 b[4])
{
    u32 r[4] = {0, 0, 0, 0};
    VU(em_vu_vec_bits(EM_VU_SUB, 15, EM_VU_NO_BC, a, b, 0, NULL, r));
    memcpy(out, r, sizeof r);
    return 0;
}

/* 00102760(out, v): t.xyz = v * v; t.x += t.y; t.x += t.z; Q = sqrt(t.x);
 * t.x = 0 + Q; Q = 1.0 / t.x; r = (0, 0, 0, 0) (the constant register minus
 * itself); r.xyz = v * Q. Stores all four lanes of r (so w = 0). */
static int sdk_00102760(EmRogerActor *s, u32 out[4], const u32 in[4])
{
    u32 v[4], t[4] = {0, 0, 0, 0}, r[4] = {0, 0, 0, 0}, q;
    memcpy(v, in, sizeof v);
    VU(em_vu_vec_bits(EM_VU_MUL, 14, EM_VU_NO_BC, v, v, 0, NULL, t));
    VU(em_vu_vec_bits(EM_VU_ADDBC, 8, 1, t, t, 0, NULL, t));
    VU(em_vu_vec_bits(EM_VU_ADDBC, 8, 2, t, t, 0, NULL, t));
    q = em_vu_sqrt_bits(t[0]);
    VU(em_vu_vec_bits(EM_VU_ADDQ, 8, EM_VU_NO_BC, VF0, NULL, q, NULL, t));
    VU(em_vu_div_bits(VF0[3], t[0], 3, 0, &q));
    VU(em_vu_vec_bits(EM_VU_SUB, 15, EM_VU_NO_BC, VF0, VF0, 0, NULL, r));
    VU(em_vu_vec_bits(EM_VU_MULQ, 14, EM_VU_NO_BC, v, NULL, q, NULL, r));
    memcpy(out, r, sizeof r);
    return 0;
}

static void load_words(u32 *out, const uint8_t *p, unsigned count) { memcpy(out, p, 4u * count); }

/* The ten parent kinds whose case copies the parent's bone 1 world matrix
 * (001C5D34..001C5DB0); every other kind skips the copy. */
static int copies_parent_matrix(uint8_t kind)
{
    switch (kind) {
    case 0x47: case 0x4E: case 0x54: case 0x55: case 0x58:
    case 0x59: case 0x5A: case 0x5D: case 0x5E: case 0x6A:
        return 1;
    default:
        return 0;
    }
}

int em_roger_actor_001C5C90(EmRogerActor *s, EmRogerActorRecord *e, const EmRogerActorRecord *parent)
{
    ENTER(EM_ROGER_ACTOR_EQUIPMENT, e);
    uint8_t state = e->lifecycle;
    if (state == 2 || state == 3) {
        /* 001C5F88: 001AFC10(e). */
        NEED(s->workers.w_001AFC10, 0x001AFC10u);
        CALL(0x001AFC10u, s->workers.w_001AFC10(s->workers.ctx, e));
        return 0;
    }
    if (state > 3) return 1;                        /* the dispatcher does nothing */
    /* Both live states read the parent record at +0x18 first. */
    if (!parent) return fault(s, EM_ROGER_ACTOR_EQUIPMENT, EM_ROGER_ACTOR_FAULT_NULL_WORKER);
    if (parent->address != e->parent)
        return fault(s, EM_ROGER_ACTOR_EQUIPMENT, EM_ROGER_ACTOR_FAULT_BAD_RESULT);
    if (parent->lifecycle >= 2) {                   /* 001C5CE0 / 001C5D14 */
        e->lifecycle = 3;
        return 1;
    }
    if (state == 0) {
        /* 001C5CFC: 001B1020(e, +0x0D, -1, -1); its result is not read
         * (only checked to lie in 001B1020's range {0, 1}). Whatever +0x04
         * stores the worker makes on the way (001B1020's += 1, 001B0DC0's
         * = 3), 001C5D0C then stores +0x04 = 1. */
        int32_t r = -1;
        NEED(s->workers.w_001B1020, 0x001B1020u);
        CALL(0x001B1020u, s->workers.w_001B1020(s->workers.ctx, e, e->kind, -1, -1, &r));
        if (r != 0 && r != 1) return fault(s, 0x001B1020u, EM_ROGER_ACTOR_FAULT_BAD_RESULT);
        e->lifecycle = 1;                           /* 001C5D0C */
        return 1;
    }
    /* State 1. 001C5D28: nothing until the parent's +0x09 is set. */
    if (parent->bones_held == 0) return 1;
    NEED(s->world.d00275B40, 0x00275B40u);
    if (s->world.d00275B40_count < 1)
        return fault(s, 0x00275B40u, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    NEED(s->world.spad3600, 0x70003600u);
    if (parent->drawn != 0) {
        /* 001C5F68 calls through +0x4C; w_draw stands for 001CAA00 only,
         * the method 001CA5F0 installs for kind 0 (both AREA11 owners). */
        NEED(s->workers.w_draw, e->draw);
        if (e->draw != EM_ROGER_ACTOR_DRAW_001CAA00)
            return fault(s, e->draw, EM_ROGER_ACTOR_FAULT_BAD_RESULT);
    }
    uint8_t *bone0 = slot_bytes(s, s->world.d00275B40[0] + 0x90u, 0x40);
    if (!bone0) return fault(s, EM_ROGER_ACTOR_EQUIPMENT, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
    if (copies_parent_matrix(parent->kind)) {
        /* 00102958 copy_qw4(*D_00275B40 +0x90, *(parent +0x114) +0x90). */
        const uint8_t *src = slot_bytes(s, parent->bone[1] + 0x90u, 0x40);
        if (!src) return fault(s, EM_ROGER_ACTOR_EQUIPMENT, EM_ROGER_ACTOR_FAULT_BAD_INDEX);
        memmove(bone0, src, 0x40);
    }
    u32 m[16], va[4], vb[4], dir[4];
    u32 *spad = s->world.spad3600;
    /* 001C5E70: spad = (0, 1.0, 0, 1.0); va = 001026A0(bone0 matrix, spad). */
    spad[0] = 0; spad[1] = F_ONE; spad[2] = 0; spad[3] = F_ONE;
    load_words(m, bone0, 16);
    if (sdk_001026A0(s, va, m, spad) < 0) return -1;
    /* 001C5EB0: spad = (5.4, 1.0, 0, 1.0); vb = 001026A0(bone0 matrix, spad). */
    spad[0] = F_5_4; spad[1] = F_ONE; spad[2] = 0; spad[3] = F_ONE;
    load_words(m, bone0, 16);
    if (sdk_001026A0(s, vb, m, spad) < 0) return -1;
    /* 001C5F00: +0xC0 = vb - va; 001C5F0C: +0xC0 = 00102760(+0xC0). */
    if (sdk_001028D0(s, dir, vb, va) < 0) return -1;
    memcpy(e->fC0, dir, sizeof dir);
    if (sdk_00102760(s, dir, e->fC0) < 0) return -1;
    memcpy(e->fC0, dir, sizeof dir);
    /* 001C5F14..001C5F60: +0xA0 = va, +0xAC = 1.0; +0xB0 = vb, +0xBC = 1.0. */
    memcpy(e->fA0, va, sizeof va);
    e->fA0[3] = F_ONE;
    memcpy(e->fB0, vb, sizeof vb);
    e->fB0[3] = F_ONE;
    e->drawn = 1;                                   /* 001C5F64 */
    if (parent->drawn != 0) {                       /* 001C5F68: jalr +0x4C */
        CALL(e->draw, s->workers.w_draw(s->workers.ctx, e));
    }
    return 1;
}
