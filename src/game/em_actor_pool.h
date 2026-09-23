/* Native actor pool (WP-3 step S4, SCENE_COORDINATOR_DESIGN.md sections 2.5 and 4.1).
 *
 * Hand translation of the original pool over the 0x100 x 0x2F0 arena at
 * D_007A5640 (Extermination/src):
 *   001AF8E0 reset   (NEARMISS; checked against the splat .s)
 *   001AFA90 alloc   (byte-matched)
 *   001AFA50 link    (byte-matched)
 *   001AFBC0 unlink  (byte-matched)
 *   001AFC10 free    (byte-matched)
 *   001AFD70 walk    (NEARMISS; checked against the splat .s)
 * Verified by tools/test_actor_pool_reference.py, which executes those
 * original instructions, and by tests/actor_pool_test.c.
 *
 * This is not a binary overlay: records are native structs. Every field below
 * that carries an offset is an original record byte one of the six functions
 * reads or writes; em_actor_pool_record_image() writes them back at their
 * original offsets (every other byte of the image is 0) so that a test or a
 * census can compare against original RAM.
 *
 * Deliberate differences from the original, all fail-stop:
 * - the walk faults (EM_SCENE_FAULT_FREED_NEXT, owner's callback) when a
 *   behaviour frees the node the walk already captured as `next`; the original
 *   would continue into the freed record;
 * - a reached NULL behaviour, a negative behaviour result, a free of a record
 *   whose +0x14 is 0, and a free of a record with bones (+9) != 0 and no
 *   001AF800 worker all fault instead of dereferencing garbage;
 * - the native-only binding fields (behavior, release, owner) are cleared by
 *   alloc and free so an unbound record faults instead of running a stale
 *   callback. The original +0x10 word (`callback`) keeps its original
 *   lifetime: neither alloc nor free writes it.
 *
 * Not modelled here (owned elsewhere):
 * - 001AF8E0's second half, the per-class list block D_00275B54..D_00275BB8
 *   (pointers to D_0028AAB0..D_0028B020 and their u16 counters). It belongs to
 *   the class lists that 001AAD00 swaps;
 * - D_00275B40 (= current + 0x110, the bone work array anim_bone_array_setup
 *   publishes from 001CB590) and the bone-slot stack D_00275BD0/D_00275BCC used
 *   by 001AF800. The walk reports 001CB590 through the trace hook; free calls
 *   the w_001AF800 worker only when a record has bones.
 */
#ifndef EM_ACTOR_POOL_H
#define EM_ACTOR_POOL_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_ACTOR_POOL_CAPACITY 0x100     /* 001AF8E0 loop bound, D_00275BC8 = 0x100 */
#define EM_ACTOR_RECORD_SIZE 0x2F0       /* 001AF8E0 stride / memset size; 001CB590 a1 */
#define EM_ACTOR_POOL_BASE 0x007A5640u   /* D_007A5640 */
#define EM_ACTOR_CLASS_MASK 0x1F         /* 001AFA90/001AFD70: cls & ~0xE0 on the byte */
#define EM_ACTOR_CLASS_RESERVED 0x0C     /* 001AFA90: refused while free count < 10 */
#define EM_ACTOR_CLASS_RESERVE 10
#define EM_ACTOR_SCRATCH_OFFSET 0x1F0    /* 001AFC10 zeroes 0x40 words from here */
#define EM_ACTOR_SCRATCH_SIZE 0x100

/* Original function addresses used as fault / trace keys. */
#define EM_ACTOR_FN_001AF800 0x001AF800u
#define EM_ACTOR_FN_001AFC10 0x001AFC10u
#define EM_ACTOR_FN_001AFD70 0x001AFD70u
#define EM_ACTOR_FN_001CB590 0x001CB590u

typedef struct EmActor EmActor;

/* Behaviour called by the walk in place of the original jalr *(+0x10).
 * Returns 1 when it ran, a negative value on a fault. `world` is the opaque
 * argument given to the walk. */
typedef int (*EmActorBehavior)(EmActor *actor, void *world);
/* Native resource hook, no original counterpart: called by the pool reset for
 * every record that is allocated when the reset runs (the original reset just
 * memsets the arena). */
typedef void (*EmActorRelease)(EmActor *actor);
/* Worker standing in for 001AF800's bone-slot return (the original pushes the
 * +0x110 slot pointers onto D_00275BD0 and adds +9 to D_00275BCC). Called by
 * free only when bones (+9) != 0. Returns >= 0 on success. */
typedef int (*EmActorBoneRelease)(void *ctx, EmActor *actor);
/* Instrumentation only; never changes behaviour. `callee` is 0x001CB590 or
 * the actor's `callback`; `actor_address` is the original record address. */
typedef void (*EmActorTraceFn)(void *ctx, uint32_t caller, uint32_t callee,
                               uint32_t actor_address, const EmActor *actor);

struct EmActor {
    /* +0x00..+0x0F: 001AFC10 clears these four words. */
    uint8_t status;   /* +0x00: 001AFA90 writes 2 */
    uint8_t drawn;    /* +0x01: 001AFD70 writes 0 before each behaviour call */
    uint8_t cls;      /* +0x02: 001AFA90 stores its argument byte (flags 0xE0 kept) */
    uint8_t model;    /* +0x03 */
    uint8_t u04[5];   /* +0x04..+0x08 */
    uint8_t bones;    /* +0x09: 001CB590 a2 in the walk; 001AF800 loop count */
    uint8_t u0A[3];   /* +0x0A..+0x0C (001AF800 also clears +0x0C) */
    uint8_t param;    /* +0x0D */
    uint16_t uid;     /* +0x0E */
    uint32_t callback; /* +0x10: original behaviour address (trace and census key) */
    EmActor *self;    /* +0x14: 001AFA90 writes self; 001AFC10 reads it and clears it */
    EmActor *prev;    /* +0x18 */
    EmActor *next;    /* +0x1C: active-list next, or free-list next when free */
    uint32_t w30;     /* +0x30: 001AFA90 writes 0 */
    uint16_t h36;     /* +0x36: 001AFC10 writes 0 */
    uint16_t h52;     /* +0x52: 001AFA90 writes 0 */
    uint16_t kind;    /* +0x54: 001AFA90 writes 0 */
    uint16_t link;    /* +0x56: 001AFA90 writes 0 */
    uint32_t w58;     /* +0x58: 001AFA90 writes 0 */
    uint32_t w5C;     /* +0x5C: 001AFA90 writes 0x00010101 */
    float f60[8];     /* +0x60..+0x7C: 001AFA90 writes 1,1,1,1, 0,0, 1,1 (bit patterns) */
    float f80[4];     /* +0x80..+0x8C: 001AFA90 writes 1.0 x4 */
    uint32_t w90;     /* +0x90: 001AFC10 writes 0 */
    int16_t h94;      /* +0x94: 001AFA90 writes -1 */
    int16_t h96;      /* +0x96: 001AFA90 writes 0 */
    uint8_t b98;      /* +0x98: 001AFC10 writes 0 */
    uint8_t b99;      /* +0x99: 001AFA90 writes 0 */
    uint8_t table_index; /* +0x9A: 001AFA90 writes 0 */
    uint8_t b9C;      /* +0x9C: 001AFA90 writes 0 */
    uint8_t b9D, b9E; /* +0x9D/+0x9E: class 2 latches D_00810701/D_00810702 */
    float pos[4];     /* +0xB0..+0xBC: 001AFA90 writes 0,0,0,1 */
    float rot[4];     /* +0xC0..+0xCC: no pool function writes it (survives free/alloc) */
    uint8_t scratch[EM_ACTOR_SCRATCH_SIZE]; /* +0x1F0..+0x2EF: 001AFC10 zeroes it */

    /* Native only. */
    EmActorBehavior behavior;
    EmActorRelease release;
    void *owner;        /* the native controller */
    uint32_t source_id; /* EMIS record id, set by the spawner */
    uint32_t generation; /* bumped by every free and reset; detects a freed `next` */
    uint8_t allocated;   /* 1 between alloc and free */
};

typedef struct {
    EmActor records[EM_ACTOR_POOL_CAPACITY]; /* record i is D_007A5640 + i * 0x2F0 */
    EmActor *head;      /* D_00275BC0 */
    EmActor *tail;      /* D_00275BBC */
    EmActor *free_head; /* D_00275BC4 */
    int16_t free_count; /* D_00275BC8 (lh/sh) */
    EmActor *current;   /* D_00275B44 / D_00275B48, written by 001CB590 in the walk */
    /* 001AF800 worker, reached from free when bones != 0. */
    EmActorBoneRelease w_001AF800;
    void *worker_ctx;
} EmActorPool;

/* The pool globals as original words, for comparison with original RAM. */
typedef struct {
    uint32_t head;      /* D_00275BC0 */
    uint32_t tail;      /* D_00275BBC */
    uint32_t free_head; /* D_00275BC4 */
    int16_t free_count; /* D_00275BC8 */
    uint32_t current;   /* D_00275B44 */
} EmActorPoolGlobals;

/* 001AF8E0 (pool half). Calls `release` of every allocated record (record
 * order, native hook), then zeroes all 256 records, chains them through +0x1C
 * in record order (last = 0) and sets head = tail = 0, free head = record 0,
 * free count = 0x100. `current` and the workers are kept. */
void em_actor_pool_reset_001AF8E0(EmActorPool *pool);

/* 001AFA90. Returns the record or NULL (class 0xC with fewer than 10 free, or
 * no free record); NULL is the original result, not a fault. Class 2 reads
 * D_00810701/D_00810702 from `scene`. */
EmActor *em_actor_pool_alloc_001AFA90(EmActorPool *pool, const EmSceneState *scene, uint8_t cls);

/* 001AFA50: tail append. */
void em_actor_pool_link_001AFA50(EmActorPool *pool, EmActor *actor);
/* 001AFBC0: unlink; the actor's own prev/next are left as they were. */
void em_actor_pool_unlink_001AFBC0(EmActorPool *pool, EmActor *actor);

/* 001AFC10. `handle` may be any record whose +0x14 names the canonical record.
 * Returns 0, or -1 with a fault latched in `scene` (0x1AFC10 BAD_INDEX for a
 * handle outside the pool or +0x14 == 0; 0x1AF800 NULL_WORKER / WORKER_FAILED
 * for a record with bones and no / a failing worker). */
int em_actor_pool_free_001AFC10(EmActorPool *pool, EmSceneState *scene, EmActor *handle);

/* 001AFD70(mode): mode 1 skips class 1, mode 2 ticks only class 1, any other
 * mode ticks all. Writes scene->spad3B8A. Returns 0, or -1 with a fault
 * latched (also when `scene` already holds one; then nothing runs). */
int em_actor_pool_walk_001AFD70(EmActorPool *pool, EmSceneState *scene, int mode, void *world,
                                EmActorTraceFn trace, void *trace_ctx);

/* Original record address of `actor` (0 for NULL or a pointer outside the pool). */
uint32_t em_actor_pool_address(const EmActorPool *pool, const EmActor *actor);
/* The pool globals as original words. */
void em_actor_pool_globals(const EmActorPool *pool, EmActorPoolGlobals *out);
/* Original-layout image of the bytes this module owns (see the struct). */
void em_actor_pool_record_image(const EmActorPool *pool, const EmActor *actor,
                                uint8_t out[EM_ACTOR_RECORD_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* EM_ACTOR_POOL_H */
