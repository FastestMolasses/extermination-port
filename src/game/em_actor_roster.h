/* AREA11 static roster (WP-3 step S7, SCENE_COORDINATOR_DESIGN.md sections
 * 4.2-4.4).
 *
 * Hand translation of the original state-0 spawners (Extermination/src):
 *   001B6990 placement spawner (byte-matched); calls 001B6910 first
 *   001B6910 deferred-registry walk (byte-matched)
 *   001B65C0 first-visit prime pass (NEARMISS; logic checked against the .s)
 *   001B64F0 prime-pass record sweep (byte-matched)
 *   001B6660 deferred record spawner (byte-matched)
 *   001B11E0 bit test (NEARMISS; checked against the .s)
 *   001C5C50 area-title node (byte-matched)
 * over the records tools/export_area11_roster.py copies from the user's own
 * ELF and AREA11 overlay into assets/scene_snow/roster.emro (ignored). The
 * records stay in their original byte layout so every field below is read at
 * the offset the original reads it. Verified by
 * tools/test_actor_census_reference.py, which executes those original
 * instructions and walks D_00275BC0 in the captures.
 *
 * Only the static roster is spawned here: the deferred group then the
 * placement table, then (separately) the 001C5C50 node. Every other node of
 * the AREA11 list is spawned by the owner that spawns it in the original
 * (ORIGINAL_FRAME_ORDER.md section 6, "Q4"); the callback registry below
 * names each one and its original spawner.
 *
 * Record 13 (class 9, callback 0x008257A0) IS spawned: the original frees it
 * from its own behaviour (0x8258E0 calls 001AFC10 with a0 = the node, on the
 * second world frame; ORIGINAL_FRAME_ORDER.md section 6, "Q3"). It carries
 * EM_ROSTER_FLAG_SELF_FREEING and stays UNBOUND until 008257A0 is translated.
 *
 * Not modelled (fail-stop or documented):
 * - 001B65C0 also stores D_00810B40+area into D_00275BE4 and the current
 *   item pointer into D_00275BE8. No main-ELF function other than 001B65C0
 *   reads either word; they are not kept.
 * - The original has no bounds: an area/sub the roster was not exported for,
 *   or a progress byte outside EmActorRosterProgress, FAULTS (BAD_INDEX).
 * - The +0x2E halfword 001B6660 and 001B6990 write is stored in
 *   EmActor.flags2 (em_actor_pool.h); EmActorRosterSpawned.flags2 is a copy
 *   of it for the report.
 */
#ifndef EM_ACTOR_ROSTER_H
#define EM_ACTOR_ROSTER_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_actor_pool.h"
#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_ROSTER_MAGIC "EMRO"
#define EM_ROSTER_VERSION 1u
#define EM_ROSTER_DEFERRED_RECORD_SIZE 0x2Cu  /* 001B6660 stride */
#define EM_ROSTER_PLACEMENT_RECORD_SIZE 0x28u /* 001B6990 stride */
#define EM_ROSTER_MAX_GROUPS 64u              /* exporter limit, not original */

/* Original function addresses used as fault keys. */
#define EM_ROSTER_FN_001B11E0 0x001B11E0u
#define EM_ROSTER_FN_001B64F0 0x001B64F0u
#define EM_ROSTER_FN_001B65C0 0x001B65C0u
#define EM_ROSTER_FN_001B6660 0x001B6660u
#define EM_ROSTER_FN_001B6910 0x001B6910u
#define EM_ROSTER_FN_001B6990 0x001B6990u
#define EM_ROSTER_FN_001C5C50 0x001C5C50u
#define EM_ROSTER_CALLBACK_001C5930 0x001C5930u /* 001C5C50 stores it at +0x10 */

/* Caller-owned view of the original bytes D_00810758..D_00810B5F that the
 * spawners read and (001B65C0/001B64F0) write:
 *   D_00810700[0x58 + i]  (i = key >> 8; 001B6660 cases 2, 3, 5, 6)
 *   D_00810700[0xD8 + i]  (001B6660 cases 4, 5)
 *   D_00810778, D_00810788 (001B6660 case 6; 001B65C0)
 *   D_00810860[area][0x20] (001B11E0 test; 001B64F0 clear)
 *   D_00810B40[area]      (001B65C0 first-visit bits)
 * The roster keeps no copy; the owner of these bytes passes them in. */
#define EM_ROSTER_PROGRESS_BASE 0x00810758u
#define EM_ROSTER_PROGRESS_END 0x00810B60u
typedef struct {
    uint8_t bytes[EM_ROSTER_PROGRESS_END - EM_ROSTER_PROGRESS_BASE];
} EmActorRosterProgress;

typedef struct {
    uint32_t address;       /* original address of the group's first record */
    uint32_t count;         /* records before the -1 terminator */
    const uint8_t *records; /* count * 0x2C original bytes */
} EmActorRosterGroup;

/* One exported roster: D_0024D820[area][sub] (every item of the list, in
 * order) and D_0024D7C0[area][sub] (records before the 0xFF terminator). */
typedef struct {
    uint8_t area, sub;    /* D_00810700 / D_00810701 the tables belong to */
    uint32_t group_count; /* 0: D_0024D820[area] == 0 (001B6910 does nothing) */
    EmActorRosterGroup groups[EM_ROSTER_MAX_GROUPS];
    uint32_t placement_address;
    uint32_t placement_count;
    const uint8_t *placements; /* placement_count * 0x28 original bytes */
    uint8_t *storage;          /* owned copy of the file */
} EmActorRoster;

/* Parse an .emro image (copied). Returns 0, or -1 for a malformed image. */
int em_actor_roster_parse(EmActorRoster *roster, const uint8_t *data, size_t size);
/* Load an .emro file. Returns 0, or -1 (missing or malformed file). */
int em_actor_roster_load(EmActorRoster *roster, const char *path);
void em_actor_roster_free(EmActorRoster *roster);

/* ---------------------------------------------------------------- spawning */

typedef enum {
    EM_ROSTER_SOURCE_DEFERRED = 1,  /* 001B6660 via 001B6910 */
    EM_ROSTER_SOURCE_PLACEMENT = 2, /* 001B6990 */
    EM_ROSTER_SOURCE_001C5C50 = 3
} EmActorRosterSource;

/* The original frees this node from its own behaviour (record 13). */
#define EM_ROSTER_FLAG_SELF_FREEING 0x1u

typedef struct {
    EmActor *actor;
    uint32_t record_address; /* original record address (0 for 001C5C50); also actor->source_id */
    uint8_t source;          /* EmActorRosterSource */
    uint8_t group;           /* deferred group index */
    uint16_t index;          /* record index in its group / placement table */
    uint16_t flags2;         /* copy of actor->flags2 (+0x2E) after the spawner */
    uint8_t wrote_flags2;    /* 1 when the spawner writes +0x2E (not 001C5C50) */
    uint32_t flags;          /* EM_ROSTER_FLAG_* from the callback registry */
} EmActorRosterSpawned;

/* Spawn report. Instrumentation for the census and trace; it stores no
 * original state (every original field lives in the EmActor). */
typedef struct {
    EmActorRosterSpawned entries[EM_ACTOR_POOL_CAPACITY];
    uint32_t count;
    uint32_t skipped_condition; /* 001B6660 condition failed */
    uint32_t skipped_alloc;     /* 001AFA90 returned 0: record skipped, index advances */
    uint32_t skipped_class_0b;  /* 001B6990: cls & 0xFF == 0x0B */
} EmActorRosterSpawnLog;

/* Called for every spawned node after its original fields are written. It
 * attaches the native behaviour (or leaves it NULL: the walk then faults at
 * the node's callback). Returns >= 0, or < 0 to fault (WORKER_FAILED at the
 * node's callback). */
typedef int (*EmActorRosterBindFn)(void *ctx, EmActor *actor, const EmActorRosterSpawned *spawned);

/* 001B6990 (with 001B6910 first). `scene` supplies D_00810700/701 and the
 * fault latch; they must name the roster's area/sub or it faults. `log` and
 * `bind` may be NULL. Returns 0, or -1 with a fault latched. */
int em_actor_roster_spawn_001B6990(const EmActorRoster *roster, EmActorPool *pool, EmSceneState *scene,
                                   EmActorRosterProgress *progress, EmActorRosterBindFn bind,
                                   void *bind_ctx, EmActorRosterSpawnLog *log);

/* 001C5C50: alloc(8); on success model 3, param 0, callback 001C5930. An
 * alloc failure returns 0 like the original. Returns 0 or -1 (fault). */
int em_actor_roster_spawn_001C5C50(EmActorPool *pool, EmSceneState *scene, EmActorRosterBindFn bind,
                                   void *bind_ctx, EmActorRosterSpawnLog *log);

/* ---------------------------------------------------- callback registry */

typedef enum {
    EM_ROSTER_ORIGIN_DEFERRED = 1,  /* spawned by 001B6660 (deferred group) */
    EM_ROSTER_ORIGIN_PLACEMENT = 2, /* spawned by 001B6990 (placement table) */
    EM_ROSTER_ORIGIN_RUNTIME = 3    /* spawned later by the named spawner */
} EmActorRosterOrigin;

/* Every callback in the AREA11 owner list: where it comes from and its
 * native binding. `binding` NULL means UNBOUND: a walk that reaches the node
 * faults at `callback`. `name` states only what the cited evidence shows. */
typedef struct {
    uint32_t callback;
    uint8_t origin;   /* EmActorRosterOrigin */
    uint32_t spawner; /* original function that allocates the node */
    uint32_t flags;   /* EM_ROSTER_FLAG_* */
    const char *binding;
    const char *name;
} EmActorRosterCallback;

const EmActorRosterCallback *em_actor_roster_area11_callbacks(size_t *count);
/* NULL when `callback` is not in the registry. */
const EmActorRosterCallback *em_actor_roster_area11_callback(uint32_t callback);

#ifdef __cplusplus
}
#endif

#endif /* EM_ACTOR_ROSTER_H */
