/* Area spawn table D_0024D650 and the original player placement (WP-3 step
 * S12a, SCENE_COORDINATOR_DESIGN.md section 6).
 *
 * Hand translation of (Extermination/src, splat .s checked):
 *   001B0250  area flags: D_008106C8 = spawn record +0x1C, and in area 0x0B
 *             with D_00810788 != 0 the mask 0xF1FFFF8F then | 0x44
 *             (byte-matched)
 *   001B07C0  player placement from the spawn record (byte-matched; no
 *             NEARMISS marker, mwcc 2.3.3 -sdatathreshold 1)
 * over the bytes tools/export_spawn_table.py copies from the user's own ELF
 * into assets/spawn/spawn_table.emsp (ignored). The asset is an address-
 * mapped window of the ELF data: D_0024D650[0..0x16], every area's room
 * pointer array and every room's 0x30-byte entry array, at their original
 * addresses. The translation walks it exactly as the original walks memory
 * (D_0024D650[D_00810700] -> [D_00810701] -> + D_00810702 * 0x30); a read
 * outside the exported window FAULTS (the original has no bounds and would
 * read whatever follows). Verified by tools/test_spawn_place_reference.py,
 * which executes the original 0x1B07C0 (with 001B0250) and compares every
 * write and every callee's view.
 *
 * Record layout (0x30 bytes, the offsets the originals read):
 *   +0x00/+0x04/+0x08  position        (001B07C0)
 *   +0x0C              heading         (001B07C0 -> player +0xC4)
 *   +0x10              camera byte/word (001B0460)
 *   +0x14              walk-out byte   (001B07C0 -> player +0x0E); word (001B0460)
 *   +0x18              camera float    (001B0460 -> D_00810244, D_008101EC)
 *   +0x1C              area flag word  (001B0250 -> D_008106C8)
 *
 * The module is pure: the player record, the progress bytes and the
 * scratchpad words it reads and writes come in and go out through EmSpawnIo,
 * addressed by original offset; the callees 001EFE00, 0015C1F0 and 001B0460
 * and the object stores through player +0x1C/+0x304 are workers. It keeps no
 * copy of anything.
 */
#ifndef EM_SPAWN_TABLE_H
#define EM_SPAWN_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SPAWN_MAGIC "EMSP"
#define EM_SPAWN_VERSION 1u
#define EM_SPAWN_TABLE_ADDRESS 0x0024D650u /* D_0024D650 */
#define EM_SPAWN_RECORD_SIZE 0x30u         /* 001B07C0 / 001B0250 stride */
#define EM_SPAWN_MAX_RANGES 256u           /* exporter limit, not original */
#define EM_SPAWN_TABLE_PATH "assets/spawn/spawn_table.emsp"

#define EM_SPAWN_FN_001B0250 0x001B0250u
#define EM_SPAWN_FN_001B07C0 0x001B07C0u
#define EM_SPAWN_FN_001EFE00 0x001EFE00u
#define EM_SPAWN_FN_0015C1F0 0x0015C1F0u
#define EM_SPAWN_FN_001B0460 0x001B0460u
#define EM_SPAWN_D_008102B0 0x008102B0u    /* player record (worker argument) */
#define EM_SPAWN_EFFECT_80000018 0x80000018u /* 001EFE00 a0 */

typedef struct {
    uint32_t address; /* original address of the first byte */
    uint32_t size;
    const uint8_t *bytes; /* into EmSpawnTable.data */
} EmSpawnRange;

typedef struct {
    uint32_t range_count;
    EmSpawnRange ranges[EM_SPAWN_MAX_RANGES];
    uint8_t *data; /* owned */
} EmSpawnTable;

/* Parse an EMSP image (layout in em_spawn_table.c). 0, or -1 (table left
 * empty) on a malformed image. */
int em_spawn_table_parse(EmSpawnTable *table, const uint8_t *data, size_t size);
int em_spawn_table_load(EmSpawnTable *table, const char *path);
void em_spawn_table_free(EmSpawnTable *table);

/* The `size` bytes at original `address`, or NULL when any of them is outside
 * the exported window. */
const uint8_t *em_spawn_table_read(const EmSpawnTable *table, uint32_t address, uint32_t size);

/* The fields of the player record D_008102B0 that 001B07C0 reads or writes,
 * named by offset. Object fields hold the original address of the object
 * (0 = none). */
typedef struct {
    uint8_t b000;       /* +0x000 event byte (1 when pending damage is dropped) */
    uint8_t b004, b005, b006; /* +0x004..+0x006 state, sub, phase */
    uint8_t b00E;       /* +0x00E walk-out byte */
    uint32_t w01C;      /* +0x01C attached object (its +4 byte is written) */
    float f060[4];      /* +0x060..+0x06C */
    float f080[4];      /* +0x080..+0x08C */
    float f0A0[4];      /* +0x0A0..+0x0AC position */
    float f0B0[4];      /* +0x0B0..+0x0BC position */
    float f0C0[4];      /* +0x0C0..+0x0CC rotation (x, heading, z, w) */
    float f220;         /* +0x220 health */
    float f224;         /* +0x224 pending health damage */
    float f228;         /* +0x228 infection */
    float f22C;         /* +0x22C pending infection damage */
    uint32_t w230;      /* +0x230 */
    uint8_t b234, b235; /* +0x234 infected latch, +0x235 low-health latch */
    uint32_t w304;      /* +0x304 effect object (its +4 byte is written) */
} EmSpawnPlayer;

typedef struct {
    /* In. */
    uint8_t d810700, d810701, d810702; /* area, room, entry */
    uint8_t d275BE0;                   /* load-game flag */
    float d810710[3];                  /* D_00810710/14/18 (load-game position) */
    float d810720[3];                  /* D_00810720/24/28 (load-game rotation) */
    uint8_t d810707;                   /* infected latch copy */
    float d810858, d81085C;            /* health, infection */
    uint8_t d810788;                   /* event 0x30 (001B0250) */
    uint8_t d810C7D, d810C7E;          /* item counts read for D_00810C60 */
    uint8_t spad3B8D;                  /* world-frame selector */
    /* In and out. */
    uint8_t d810706;                   /* low-health latch copy (masked & 1) */
    EmSpawnPlayer player;
    /* Out. */
    int32_t d8106C8;                   /* area flag word (001B0250) */
    uint8_t d810C60;                   /* equipment status */
    float spad3B40[8];                 /* 0x70003B40..0x70003B5C = player +0xB0..+0xCC */
    EmSceneFault fault;                /* first fault (address, code) */
} EmSpawnIo;

/* Callees. Each returns 0, or negative on failure (fail-stop). */
typedef struct {
    void *ctx;
    /* 001EFE00(a0, a1): the effect spawn; *result is its v0 (stored at +0x304). */
    int (*w_001EFE00)(void *ctx, uint32_t a0, uint32_t a1, uint32_t *result);
    /* *(object + 4) = value (player +0x304 object: 2; player +0x1C object: 1). */
    int (*s_object_04)(void *ctx, uint32_t object, uint8_t value);
    int (*w_0015C1F0)(void *ctx, uint32_t player);
    int (*w_001B0460)(void *ctx, int a0);
} EmSpawnWorkers;

/* 001B0250 over io->d810700..702 and io->d810788: writes io->d8106C8.
 * 0, or -1 with io->fault set (a read outside the table window). */
int em_spawn_001B0250(const EmSpawnTable *table, EmSpawnIo *io);

/* 001B07C0(arg0). 0, or -1 with io->fault set (the table read that left the
 * window, or the worker that is NULL or failed). */
int em_spawn_001B07C0(const EmSpawnTable *table, EmSpawnIo *io, const EmSpawnWorkers *w, int arg0);

#ifdef __cplusplus
}
#endif

#endif /* EM_SPAWN_TABLE_H */
