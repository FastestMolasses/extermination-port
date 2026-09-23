/* AREA11 static roster; see em_actor_roster.h for the original functions,
 * the evidence and what is not modelled. */
#include "game/em_actor_roster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ byte access */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static int16_t rs16(const uint8_t *p) { return (int16_t)rd16(p); }

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float rdf(const uint8_t *p)
{
    uint32_t bits = rd32(p);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

/* Byte of the progress view by ORIGINAL address, or NULL outside it. */
static uint8_t *progress_at(EmActorRosterProgress *progress, int64_t address, unsigned size)
{
    if (!progress || address < (int64_t)EM_ROSTER_PROGRESS_BASE ||
        address + size > (int64_t)EM_ROSTER_PROGRESS_END)
        return NULL;
    return &progress->bytes[address - EM_ROSTER_PROGRESS_BASE];
}

/* ------------------------------------------------------------------ file */

void em_actor_roster_free(EmActorRoster *roster)
{
    if (!roster)
        return;
    free(roster->storage);
    memset(roster, 0, sizeof *roster);
}

/* Layout (little-endian; written by tools/export_area11_roster.py):
 *   0x00 "EMRO", 0x04 u32 version, 0x08 u8 area, 0x09 u8 sub,
 *   0x0A u16 group_count, 0x0C u32 placement_count, 0x10 u32 placement_address,
 *   0x14 u32 0; then group_count x {u32 address, u32 count}; then each group's
 *   records (count * 0x2C); then the placements (placement_count * 0x28). */
int em_actor_roster_parse(EmActorRoster *roster, const uint8_t *data, size_t size)
{
    if (!roster)
        return -1;
    memset(roster, 0, sizeof *roster);
    if (!data || size < 0x18 || memcmp(data, EM_ROSTER_MAGIC, 4) != 0 || rd32(data + 4) != EM_ROSTER_VERSION ||
        rd32(data + 0x14) != 0)
        return -1;
    uint32_t groups = rd16(data + 0x0A);
    uint32_t placements = rd32(data + 0x0C);
    if (groups > EM_ROSTER_MAX_GROUPS || placements > 0x10000u)
        return -1;
    size_t at = 0x18 + (size_t)groups * 8;
    if (at > size)
        return -1;
    size_t expected = at;
    for (uint32_t g = 0; g < groups; ++g) {
        uint32_t count = rd32(data + 0x18 + g * 8 + 4);
        if (count > 0x10000u)
            return -1;
        expected += (size_t)count * EM_ROSTER_DEFERRED_RECORD_SIZE;
    }
    expected += (size_t)placements * EM_ROSTER_PLACEMENT_RECORD_SIZE;
    if (expected != size)
        return -1;
    uint8_t *storage = malloc(size);
    if (!storage)
        return -1;
    memcpy(storage, data, size);
    roster->storage = storage;
    roster->area = storage[8];
    roster->sub = storage[9];
    roster->group_count = groups;
    size_t cursor = at;
    for (uint32_t g = 0; g < groups; ++g) {
        roster->groups[g].address = rd32(storage + 0x18 + g * 8);
        roster->groups[g].count = rd32(storage + 0x18 + g * 8 + 4);
        roster->groups[g].records = storage + cursor;
        cursor += (size_t)roster->groups[g].count * EM_ROSTER_DEFERRED_RECORD_SIZE;
    }
    roster->placement_address = rd32(storage + 0x10);
    roster->placement_count = placements;
    roster->placements = storage + cursor;
    return 0;
}

int em_actor_roster_load(EmActorRoster *roster, const char *path)
{
    if (roster)
        memset(roster, 0, sizeof *roster);
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (!file)
        return -1;
    uint8_t *data = NULL;
    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0)
        size = ftell(file);
    if (size > 0 && fseek(file, 0, SEEK_SET) == 0) {
        data = malloc((size_t)size);
        if (data && fread(data, 1, (size_t)size, file) != (size_t)size) {
            free(data);
            data = NULL;
        }
    }
    fclose(file);
    int result = data ? em_actor_roster_parse(roster, data, (size_t)size) : -1;
    free(data);
    return result;
}

/* ------------------------------------------------------ callback registry */

/* Static rows: the record's callback word and the spawner that allocates it
 * (records read from the exported tables; the capture census checks every
 * row against D_00275BC0). Runtime rows: ORIGINAL_FRAME_ORDER.md section 6
 * (trace 2, Q4/Q5) and section 4. */
static const EmActorRosterCallback k_area11_callbacks[] = {
    { 0x00219550u, EM_ROSTER_ORIGIN_DEFERRED, EM_ROSTER_FN_001B6660, 0, NULL,
      "deferred g0.0-g0.5 (condition 1)" },
    { 0x0015AFA0u, EM_ROSTER_ORIGIN_DEFERRED, EM_ROSTER_FN_001B6660, 0, NULL, "deferred g0.6 (condition 1)" },
    { 0x00825940u, EM_ROSTER_ORIGIN_DEFERRED, EM_ROSTER_FN_001B6660, 0, NULL, "deferred g0.7 (condition 0)" },
    { 0x00827490u, EM_ROSTER_ORIGIN_DEFERRED, EM_ROSTER_FN_001B6660, 0, NULL, "deferred g0.8 (condition 0)" },
    { 0x001BC350u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[0] room-move door" },
    { 0x00827630u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[1], area11[2]" },
    { 0x001551B0u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[3]..area11[6]" },
    { 0x008235F0u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[7]" },
    { 0x008237E0u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[8] (Roger)" },
    { 0x001C5C90u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[9]" },
    { 0x00823E80u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL,
      "area11[10] opening controller (001BA1F0 -> 001B82D0)" },
    { 0x00823CE0u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[11]" },
    { 0x008253F0u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[12]" },
    { 0x008257A0u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, EM_ROSTER_FLAG_SELF_FREEING, NULL,
      "area11[13]; frees itself (0x8258E0 -> 001AFC10) on the second world frame" },
    { 0x00156620u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[14], area11[15]" },
    { 0x00823FF0u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[16]" },
    { 0x008251E0u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[17]" },
    { 0x00159210u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[18]" },
    { 0x00827B10u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[19]" },
    { 0x001C4820u, EM_ROSTER_ORIGIN_PLACEMENT, EM_ROSTER_FN_001B6990, 0, NULL, "area11[20]" },
    { 0x001E55F0u, EM_ROSTER_ORIGIN_RUNTIME, 0x001C1DC0u, 0, NULL,
      "effect type 0x17 from the area weather flags (001C1DC0 -> 001C1EA0 -> 001EFD20 -> 001EF9D0)" },
    { EM_ROSTER_CALLBACK_001C5930, EM_ROSTER_ORIGIN_RUNTIME, EM_ROSTER_FN_001C5C50, 0, NULL,
      "area title (001C5C50 in states 0 and 4)" },
    { 0x0018A6B0u, EM_ROSTER_ORIGIN_RUNTIME, 0x0018A880u, 0, NULL,
      "class-1 player attached model (0015C420/0015C310 -> 0018A880)" },
    { 0x001E2560u, EM_ROSTER_ORIGIN_RUNTIME, 0x001EF9D0u, 0, NULL,
      "effect type 0x10 on the owner's bone 7 (001F0120 for the player, 001BA8E0 for Roger and the opening actors)" },
    { 0x001C5680u, EM_ROSTER_ORIGIN_RUNTIME, 0x001C5570u, 0, NULL,
      "indicator child (001C5570 from its owner; the g0.7 owner 0x825940 spawns its own)" },
    { 0x001C5760u, EM_ROSTER_ORIGIN_RUNTIME, 0x00827B10u, 0, NULL, "indicator child of area11[19] (0x827C20)" },
    { 0x001BB0E0u, EM_ROSTER_ORIGIN_RUNTIME, 0x001BAC00u, 0, NULL, "opening-script actor (script op 0x14)" },
    { 0x001EA240u, EM_ROSTER_ORIGIN_RUNTIME, 0x001EF9D0u, 0, NULL,
      "effect type 0x28 from player animation events (00187EE0 -> 001EFD90), walking only" },
};

const EmActorRosterCallback *em_actor_roster_area11_callbacks(size_t *count)
{
    if (count)
        *count = sizeof k_area11_callbacks / sizeof k_area11_callbacks[0];
    return k_area11_callbacks;
}

const EmActorRosterCallback *em_actor_roster_area11_callback(uint32_t callback)
{
    for (size_t i = 0; i < sizeof k_area11_callbacks / sizeof k_area11_callbacks[0]; ++i)
        if (k_area11_callbacks[i].callback == callback)
            return &k_area11_callbacks[i];
    return NULL;
}

/* -------------------------------------------------------------- spawning */

static int record_spawn(EmSceneState *scene, EmActor *actor, uint8_t source, uint8_t group, uint16_t index,
                        uint32_t record_address, int flags2, EmActorRosterBindFn bind, void *bind_ctx,
                        EmActorRosterSpawnLog *log)
{
    EmActorRosterSpawned spawned;
    memset(&spawned, 0, sizeof spawned);
    spawned.actor = actor;
    spawned.record_address = record_address;
    spawned.source = source;
    spawned.group = group;
    spawned.index = index;
    spawned.wrote_flags2 = flags2 >= 0;
    spawned.flags2 = flags2 >= 0 ? (uint16_t)flags2 : 0;
    /* The registry describes AREA11 (D_00810700 == 0x0B) only. */
    const EmActorRosterCallback *known =
        scene->d810700 == 0x0B ? em_actor_roster_area11_callback(actor->callback) : NULL;
    spawned.flags = known ? known->flags : 0;
    actor->source_id = record_address;
    if (log && log->count < EM_ACTOR_POOL_CAPACITY)
        log->entries[log->count++] = spawned;
    if (bind && bind(bind_ctx, actor, &spawned) < 0)
        return em_scene_fault(scene, actor->callback, EM_SCENE_FAULT_WORKER_FAILED);
    return 0;
}

/* 001B11E0(arg): 0 for arg & 0xFF == 0, else bit (arg & 0x1F) of the word
 * D_00810860[D_00810700 << 5 + (arg >> 5) * 4]. Returns 0/1, or -1 (fault). */
static int test_001B11E0(EmSceneState *scene, EmActorRosterProgress *progress, int arg)
{
    int v = arg & 0xFF;
    if (v == 0)
        return 0;
    const uint8_t *word = progress_at(progress, 0x00810860 + ((int64_t)scene->d810700 << 5) + (v >> 5) * 4, 4);
    if (!word)
        return em_scene_fault(scene, EM_ROSTER_FN_001B11E0, EM_SCENE_FAULT_BAD_INDEX);
    return (rd32(word) & (1u << (arg & 0x1F))) != 0;
}

/* D_00810700[index] for the spawners' progress reads; -1 (fault) outside. */
static int progress_byte(EmSceneState *scene, EmActorRosterProgress *progress, int64_t index, uint32_t fn)
{
    const uint8_t *b = progress_at(progress, 0x00810700 + index, 1);
    if (!b)
        return em_scene_fault(scene, fn, EM_SCENE_FAULT_BAD_INDEX);
    return *b;
}

/* 001B64F0(records, area): clears the D_00810860[area] bit of each class-2
 * record whose state (+6) is 1, 4, 5 or 7 and whose +8 has bit 0x40. */
static int sweep_001B64F0(EmSceneState *scene, EmActorRosterProgress *progress, const EmActorRosterGroup *group,
                          int area)
{
    for (uint32_t i = 0; i < group->count; ++i) {
        const uint8_t *p = group->records + (size_t)i * EM_ROSTER_DEFERRED_RECORD_SIZE;
        if ((rs16(p + 4) & ~0xE0) != 2)
            continue;
        int16_t st = rs16(p + 6);
        if (st != 1 && st != 4 && st != 5 && st != 7)
            continue;
        if (!(rs16(p + 8) & 0x40))
            continue;
        uint8_t b = p[2];
        if (b == 0)
            continue;
        uint8_t *word = progress_at(progress, 0x00810860 + ((int64_t)area << 5) + ((b >> 5) << 2), 4);
        if (!word)
            return em_scene_fault(scene, EM_ROSTER_FN_001B64F0, EM_SCENE_FAULT_BAD_INDEX);
        uint32_t value = rd32(word) & ~(1u << (b & 0x1F));
        for (unsigned k = 0; k < 4; ++k)
            word[k] = (uint8_t)(value >> (8 * k));
    }
    return 0;
}

/* 001B65C0(list): when D_00810788 == 0xFF and bit (1 << D_00810701) of
 * D_00810B40[D_00810700] is clear, sets it and sweeps every item (001B64F0).
 * The shift is sllv (amount & 31) and the test/set is on the byte. */
static int prime_001B65C0(const EmActorRoster *roster, EmSceneState *scene, EmActorRosterProgress *progress)
{
    int mode = progress_byte(scene, progress, 0x88, EM_ROSTER_FN_001B65C0); /* D_00810788 */
    if (mode < 0)
        return -1;
    if (mode != 0xFF)
        return 0;
    uint8_t *q = progress_at(progress, 0x00810B40 + (int64_t)scene->d810700, 1);
    if (!q)
        return em_scene_fault(scene, EM_ROSTER_FN_001B65C0, EM_SCENE_FAULT_BAD_INDEX);
    uint32_t m = 1u << (scene->d810701 & 31u);
    if ((*q & m) != 0)
        return 0;
    *q = (uint8_t)(*q | m);
    for (uint32_t g = 0; g < roster->group_count; ++g)
        if (sweep_001B64F0(scene, progress, &roster->groups[g], scene->d810700) < 0)
            return -1;
    return 0;
}

/* 001B6660 condition for record `p`: 1 spawn, 0 skip, -1 fault. */
static int condition_001B6660(EmSceneState *scene, EmActorRosterProgress *progress, const uint8_t *p)
{
    int16_t id = rs16(p);
    int16_t v = rs16(p + 2);
    int hi = (v >> 8) & 0xFF;
    int b, c, live;
    switch (id) {
    case 1:
        live = test_001B11E0(scene, progress, p[2]);
        return live < 0 ? -1 : !live;
    case 2:
        b = progress_byte(scene, progress, hi + 0x58, EM_ROSTER_FN_001B6660);
        return b < 0 ? -1 : b != 0xFF;
    case 3:
        b = progress_byte(scene, progress, hi + 0x58, EM_ROSTER_FN_001B6660);
        if (b < 0 || b != 0xFF)
            return b < 0 ? -1 : 0;
        live = test_001B11E0(scene, progress, v & 0xFF);
        return live < 0 ? -1 : !live;
    case 4:
        b = progress_byte(scene, progress, (v >> 8) + 0xD8, EM_ROSTER_FN_001B6660); /* signed v >> 8 */
        if (b <= 0)
            return b;
        live = test_001B11E0(scene, progress, v & 0xFF);
        return live < 0 ? -1 : !live;
    case 5:
        b = progress_byte(scene, progress, hi + 0x58, EM_ROSTER_FN_001B6660);
        if (b < 0 || b == 0xFF)
            return b < 0 ? -1 : 0;
        c = progress_byte(scene, progress, hi + 0xD8, EM_ROSTER_FN_001B6660);
        return c < 0 ? -1 : c == 1;
    case 6:
        b = progress_byte(scene, progress, hi + 0x58, EM_ROSTER_FN_001B6660);
        if (b < 0 || b != 0xFF)
            return b < 0 ? -1 : 0;
        c = progress_byte(scene, progress, 0x78, EM_ROSTER_FN_001B6660); /* D_00810778 */
        if (c < 0)
            return -1;
        if (c == 0xFF ? (rs16(p + 8) & 0x80) == 0 : (rs16(p + 8) & 0x80) != 0)
            return 0;
        live = test_001B11E0(scene, progress, v & 0xFF);
        return live < 0 ? -1 : !live;
    default: /* 0 and every other id spawn unconditionally */
        return 1;
    }
}

/* 001B6660(records): per record, the condition, then 001AFA90(p[4]) and the
 * copy. An alloc failure skips the record. */
static int spawn_001B6660(const EmActorRosterGroup *group, uint8_t group_index, EmActorPool *pool,
                          EmSceneState *scene, EmActorRosterProgress *progress, EmActorRosterBindFn bind,
                          void *bind_ctx, EmActorRosterSpawnLog *log)
{
    for (uint32_t i = 0; i < group->count; ++i) {
        const uint8_t *p = group->records + (size_t)i * EM_ROSTER_DEFERRED_RECORD_SIZE;
        int pass = condition_001B6660(scene, progress, p);
        if (pass < 0)
            return -1;
        if (!pass) {
            if (log)
                log->skipped_condition++;
            continue;
        }
        EmActor *e = em_actor_pool_alloc_001AFA90(pool, scene, p[4]);
        if (!e) {
            if (log)
                log->skipped_alloc++;
            continue;
        }
        e->table_index = p[2];                                /* +0x9A */
        e->model = p[6];                                      /* +0x03 */
        uint16_t flags2 = (uint16_t)((rs16(p + 6) >> 8) & 0xFF); /* +0x2E */
        e->param = p[8];                                      /* +0x0D */
        if ((rs16(p + 4) & ~0xE0) == 2) {
            e->b9D = scene->d810701; /* +0x9D = D_00810701 */
            e->b9E = p[0xA];         /* +0x9E */
        } else {
            e->uid = rd16(p + 0xA); /* +0x0E */
        }
        e->kind = rd16(p + 0xC); /* +0x54 */
        e->link = rd16(p + 0xE); /* +0x56 */
        e->pos[0] = rdf(p + 0x10);
        e->pos[1] = rdf(p + 0x14);
        e->pos[2] = rdf(p + 0x18);
        e->rot[0] = rdf(p + 0x1C);
        e->rot[1] = rdf(p + 0x20);
        e->rot[2] = rdf(p + 0x24);
        e->callback = rd32(p + 0x28); /* +0x10 */
        if (record_spawn(scene, e, EM_ROSTER_SOURCE_DEFERRED, group_index, (uint16_t)i,
                         group->address + i * EM_ROSTER_DEFERRED_RECORD_SIZE, flags2, bind, bind_ctx, log) < 0)
            return -1;
    }
    return 0;
}

int em_actor_roster_spawn_001B6990(const EmActorRoster *roster, EmActorPool *pool, EmSceneState *scene,
                                   EmActorRosterProgress *progress, EmActorRosterBindFn bind,
                                   void *bind_ctx, EmActorRosterSpawnLog *log)
{
    if (em_scene_faulted(scene))
        return -1;
    if (log)
        memset(log, 0, sizeof *log);
    /* The roster holds D_0024D820/D_0024D7C0 for one (area, sub) only. */
    if (!roster || !pool || roster->area != scene->d810700 || roster->sub != scene->d810701)
        return em_scene_fault(scene, EM_ROSTER_FN_001B6990, EM_SCENE_FAULT_BAD_INDEX);

    /* 001B6910: D_0024D820[area] == 0 -> nothing; else 001B65C0(list), then
     * 001B6660 for every item of the list. */
    if (roster->group_count) {
        if (prime_001B65C0(roster, scene, progress) < 0)
            return -1;
        for (uint32_t g = 0; g < roster->group_count; ++g)
            if (spawn_001B6660(&roster->groups[g], (uint8_t)g, pool, scene, progress, bind, bind_ctx, log) < 0)
                return -1;
    }

    /* 001B6990: records until the halfword 0xFF; cls & 0xFF == 0x0B is
     * skipped; an alloc failure skips the record; the index always advances. */
    for (uint32_t idx = 0; idx < roster->placement_count; ++idx) {
        const uint8_t *rec = roster->placements + (size_t)idx * EM_ROSTER_PLACEMENT_RECORD_SIZE;
        int16_t cls = rs16(rec);
        if ((cls & 0xFF) == 0x0B) {
            if (log)
                log->skipped_class_0b++;
            continue;
        }
        EmActor *actor = em_actor_pool_alloc_001AFA90(pool, scene, (uint8_t)(cls & 0xFF));
        if (!actor) {
            if (log)
                log->skipped_alloc++;
            continue;
        }
        actor->model = rec[2];                                      /* +0x03 */
        uint16_t flags2 = (uint16_t)((rs16(rec + 2) >> 8) & 0xFF);   /* +0x2E */
        actor->param = rec[4];                                      /* +0x0D */
        actor->table_index = (uint8_t)idx;                          /* +0x9A (sb) */
        if ((rs16(rec) & ~0xE0) == 2) {
            actor->b9D = scene->d810701; /* +0x9D = D_00810701 */
            actor->b9E = rec[6];         /* +0x9E */
        } else {
            actor->uid = rd16(rec + 6); /* +0x0E */
        }
        actor->kind = rd16(rec + 8);  /* +0x54 */
        actor->link = rd16(rec + 0xA); /* +0x56 */
        actor->pos[0] = rdf(rec + 0xC);
        actor->pos[1] = rdf(rec + 0x10);
        actor->pos[2] = rdf(rec + 0x14);
        actor->rot[0] = rdf(rec + 0x18);
        actor->rot[1] = rdf(rec + 0x1C);
        actor->rot[2] = rdf(rec + 0x20);
        actor->callback = rd32(rec + 0x24); /* +0x10 */
        if (record_spawn(scene, actor, EM_ROSTER_SOURCE_PLACEMENT, 0, (uint16_t)idx,
                         roster->placement_address + idx * EM_ROSTER_PLACEMENT_RECORD_SIZE, flags2, bind,
                         bind_ctx, log) < 0)
            return -1;
    }
    return 0;
}

int em_actor_roster_spawn_001C5C50(EmActorPool *pool, EmSceneState *scene, EmActorRosterBindFn bind,
                                   void *bind_ctx, EmActorRosterSpawnLog *log)
{
    if (em_scene_faulted(scene))
        return -1;
    if (!pool)
        return em_scene_fault(scene, EM_ROSTER_FN_001C5C50, EM_SCENE_FAULT_BAD_INDEX);
    /* 001C5C50: v0 = 001AFA90(8); 0 -> return; +3 = 3, +0xD = 0, +0x10 = 001C5930. */
    EmActor *actor = em_actor_pool_alloc_001AFA90(pool, scene, 8);
    if (!actor) {
        if (log)
            log->skipped_alloc++;
        return 0;
    }
    actor->model = 3;
    actor->param = 0;
    actor->callback = EM_ROSTER_CALLBACK_001C5930;
    /* 001C5C50 writes no +0x2E (wrote_flags2 = 0). */
    return record_spawn(scene, actor, EM_ROSTER_SOURCE_001C5C50, 0, 0, 0, -1, bind, bind_ctx, log);
}
