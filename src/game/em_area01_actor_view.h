/* Canonical original-layout views for AREA01 pool owners. EmActor owns
 * represented fields; this adapter owns only its otherwise unrepresented
 * bytes. Shared model fields are projected from their existing service.
 * See docs/LEVEL2_RUNTIME.md ("AREA01 canonical actor view") for required worker/lifetime boundaries. */
#ifndef EM_AREA01_ACTOR_VIEW_H
#define EM_AREA01_ACTOR_VIEW_H

#include "game/em_actor_pool.h"

enum { EM_AREA01_ACTOR_SHARED_MAX = 8 };

typedef struct {
    uint16_t offset, size;
    uint8_t *bytes; /* original-layout bytes; valid until the next project call */
    int writable;  /* 0 read-only; 1 writable; 2 first exact store required */
} EmArea01ActorSpan;

/* Project fields outside EmActor's ownership, or effect-owned subranges of
 * scratch (+1F0..2EF). Projected scratch never commits to EmActor scratch.
 * A readable projection
 * may use serialized words kept in the provider context (not stack locals);
 * writable bytes must be the existing
 * canonical owner storage. No allocation/model initialization is allowed.
 * Return span count (0: no shared owner), or -1. Spans may not overlap.
 * Mode 2 is an uninitialized canonical word: only an exact whole-word store
 * grants reads in this segment; commit invokes the view's written hook.
 * Lifetime-only access must use touch(), not a readable full-record span. */
typedef int (*EmArea01ActorProject)(void *ctx, EmActor *actor,
                                  EmArea01ActorSpan spans[EM_AREA01_ACTOR_SHARED_MAX]);
/* Bind a registered original callback and its matching native behavior.
 * Return 0 or -1. On success actor->callback must equal callback and its
 * behavior must be non-NULL. It must not change pool links or lifetime. */
typedef int (*EmArea01ActorRebind)(void *ctx, EmActor *actor, uint32_t callback);
typedef int (*EmArea01ActorWritten)(void *ctx, EmActor *actor, uint16_t offset, uint16_t size);

typedef struct {
    uint8_t image[EM_ACTOR_RECORD_SIZE];
    uint8_t before[EM_ACTOR_RECORD_SIZE];
    uint8_t shared[EM_ACTOR_RECORD_SIZE]; /* 0 private, 1 read-only, 2 writable, 3 write-first */
    uint8_t written[EM_ACTOR_RECORD_SIZE]; /* mode-2 exact store acquired */
    uint32_t generation;
    uint8_t touched;
} EmArea01ActorRecordView;

typedef struct {
    EmActorPool *pool;
    EmArea01ActorProject project;
    EmArea01ActorRebind rebind;
    EmArea01ActorWritten written; /* required if project returns mode 2 */
    void *ctx;
    EmArea01ActorRecordView record[EM_ACTOR_POOL_CAPACITY];
    uint32_t fault_address;
    int32_t fault; /* first failure: 1 boundary, 2 lifetime, 3 links,
                     4 shared fields, 5 callback binding */
    int active;
} EmArea01ActorView;

/* Call immediately after the actual 001AF8E0 pool reset. This is the only
 * operation which clears unrepresented bytes; free/realloc must not call
 * it. The pool and hook context must outlive this binding. */
void em_area01_actor_view_reset(EmArea01ActorView *v, EmActorPool *pool,
                                EmArea01ActorProject project, EmArea01ActorRebind rebind, void *ctx);

/* Begin a translated-owner segment. First access lazily refreshes each
 * touched node from EmActor/shared owners. Commit before EVERY native
 * worker, begin again after it, and commit on return from the owner. The
 * dispatcher must first touch each live actor argument with touch(base),
 * including before a freeing worker. This records
 * current shared values even when the owner held its argument in a local. */
int em_area01_actor_view_begin(EmArea01ActorView *v);
int em_area01_actor_view_commit(EmArea01ActorView *v);
int em_area01_actor_view_touch(EmArea01ActorView *v, uint32_t node);

/* Read-only instrumentation between segments. Composes current canonical
 * fields and passive private bytes without starting a transaction or
 * changing actor state, transaction state or fault state. Refuses any
 * requested uninitialized projection. Provider project() must itself be a
 * read-only observation (serialization buffers in provider ctx are fine). */
int em_area01_actor_view_snapshot(EmArea01ActorView *v, uint32_t address, uint32_t size, void *out);

/* Read-only held bone-slot words (+110), for an external target query.
 * Borrows existing private storage or its shared owner's projection; no
 * record snapshot is retained. Valid until the next owner/project call. */
const uint8_t *em_area01_actor_view_slot_words(EmArea01ActorView *v, uint32_t address, uint32_t size);

/* Span must stay inside one live allocated record. Returned pointers are
 * valid only in the current segment. `write` permits early rejection of
 * pool-link/read-only-model stores. The canonical overlay and math views
 * both pass the actual direction; commit also validates every protected
 * byte. Unknown addresses return NULL without a fault so
 * a composite resolver can try other owners. */
uint8_t *em_area01_actor_view_bytes(EmArea01ActorView *v, uint32_t address, uint32_t size, int write);

#endif
