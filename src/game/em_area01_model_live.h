/* AREA01 model/pose adapter. Original translations stay in their existing
 * owners; records are EmArea01ActorView's and slots are boxes_slot_world's.
 * No model cache, record array or bone arena is allocated here. */
#ifndef EM_AREA01_MODEL_LIVE_H
#define EM_AREA01_MODEL_LIVE_H
#include "game/em_area01_actor_view.h"
#include "game/em_opening_face.h"
#include "game/em_pose_host_workers.h"
#include "game/em_roger_actor_original.h"

typedef struct {
    void *ctx;
    const uint8_t *(*resource)(void *ctx, uint32_t address, uint32_t size);
    const uint8_t *(*resource_rest)(void *ctx, uint32_t address, uint32_t *size);
    const uint32_t *table;
    uint32_t table_words;
    /* D_0028A56C belongs to the retained global library owner. The area
     * loader's table may not contain it. NULL uses table[0x37] for fixtures. */
    const uint32_t *library_word;
    const uint32_t *current_actor; /* canonical D_00275B48 */
    uint32_t *current_bones;       /* canonical D_00275B40 */
    uint8_t *d8106F1, *d810707, *d810CB6;
    EmPoseGlobals *pose_globals; /* canonical clip globals and scratch views */
    const uint8_t *d810758, *d810788, *d810700;
    uint8_t *d8106D4;
    /* External head/shadow/draw services (001F0120, 001DA6A0, 001CAA00,
     * area-13-only 001BA7F0) and C5C90's native pool free (001AFC10).
     * The record is committed before this callback. Free runs with the
     * prepared generic bone hook installed and must call the actual pool;
     * it never refreshes the freed record. Missing workers fail. */
    int (*worker)(void *ctx, uint32_t function, uint32_t actor, uint32_t arg);
    EmFaceRandom random;
    /* C69A0's rest matrix, borrowed from the shared render scratch owner.
     * The other scratch windows remain in pose_globals. No private copy. */
    uint32_t *scratch3480; /* original 70003480..700034BF */
} EmArea01ModelSource;

typedef struct {
    EmArea01ActorView *actors;
    EmArea01ModelSource source;
    EmRogerActor actor;
    EmPoseHost pose;
    EmPlayerStageHost advance;
    EmPlayerStageScene stage;
    EmPlayerStageGlobals stage_globals;
    /* A temporary projection while a translation is executing. */
    EmRogerActorRecord typed;
    uint8_t *record;
    uint32_t node;
    uint32_t fault_address;
    int fault;
} EmArea01Model;

/* A single call's release projection, prepared while the record is live.
 * It is not an owner registry or a slot allocation. */
typedef struct {
    EmArea01Model *model;
    EmActor *actor;
    uint32_t generation, index;
    EmRogerActorRecord typed;
    int ready;
} EmArea01ModelRelease;

/* Area reset binds views only. The caller resets the actual pool and the
 * shared boxes stack; this adapter must never independently reset it. */
int em_area01_model_bind(EmArea01Model *m, EmArea01ActorView *actors,
                          const EmArea01ModelSource *source);
/* Execute one named worker. node is a0, a1/a2/a3 and f12/f13 retain the
 * original argument slots. *result receives integer return bits (void:0).
 * Ends an active owner segment, runs the worker, and begins a new one.
 * Failure latches and preserves writes made before the original fault. */
int em_area01_model_call(EmArea01Model *m, uint32_t function, uint32_t node,
                          uint32_t a1, uint32_t a2, uint32_t a3,
                          float f12, float f13, uint32_t *result);
/* The shared original-layout slot bytes, including face/animation slots.
 * No allocation. Unknown addresses return NULL. */
uint8_t *em_area01_model_slot_bytes(EmArea01Model *m, uint32_t address, uint32_t size);
/* Generic private-model fallback only: typed Box/face/equipment owners use
 * their existing release paths. Prepare in the active segment immediately
 * before its commit and native pool free. The pool clears self before its
 * bone hook, so native() must run there with the actor view inactive. It
 * consumes the projection once and calls the existing 001AF800 translation,
 * preserving partial slot writes on failure and private +110 bytes on reuse. */
int em_area01_model_release_prepare(EmArea01Model *m, uint32_t node,
                                    EmArea01ModelRelease *release);
int em_area01_model_release_native(EmArea01ModelRelease *release, EmActor *actor);
#endif
