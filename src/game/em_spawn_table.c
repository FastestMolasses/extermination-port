/* Area spawn table and player placement (S12a). See em_spawn_table.h.
 *
 * EMSP image (little-endian; written by tools/export_spawn_table.py):
 *   "EMSP", u32 version (1), u32 range_count, u32 0,
 *   range_count x { u32 address, u32 size },
 *   the bytes of every range, in order.
 * Ranges are the original addresses of D_0024D650[0..0x16], each area's room
 * pointer array and each room's entry array (sorted, disjoint).
 */
#include "game/em_spawn_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int em_spawn_table_parse(EmSpawnTable *table, const uint8_t *data, size_t size)
{
    if (!table)
        return -1;
    memset(table, 0, sizeof *table);
    if (!data || size < 16 || memcmp(data, EM_SPAWN_MAGIC, 4) != 0 || le32(data + 4) != EM_SPAWN_VERSION ||
        le32(data + 12) != 0)
        return -1;
    uint32_t count = le32(data + 8);
    if (count == 0 || count > EM_SPAWN_MAX_RANGES || (size - 16) / 8 < count)
        return -1;
    size_t at = 16 + (size_t)count * 8, total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t address = le32(data + 16 + 8 * i), n = le32(data + 20 + 8 * i);
        if (n == 0 || address + n < address)
            return -1;
        if (i && address < table->ranges[i - 1].address + table->ranges[i - 1].size)
            return -1; /* sorted and disjoint */
        table->ranges[i].address = address;
        table->ranges[i].size = n;
        total += n;
        if (total > size - at)
            return -1;
    }
    if (at + total != size)
        return -1;
    table->data = malloc(total);
    if (!table->data)
        return -1;
    memcpy(table->data, data + at, total);
    size_t offset = 0;
    for (uint32_t i = 0; i < count; ++i) {
        table->ranges[i].bytes = table->data + offset;
        offset += table->ranges[i].size;
    }
    table->range_count = count;
    return 0;
}

int em_spawn_table_load(EmSpawnTable *table, const char *path)
{
    if (table)
        memset(table, 0, sizeof *table);
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
    int result = data ? em_spawn_table_parse(table, data, (size_t)size) : -1;
    free(data);
    return result;
}

void em_spawn_table_free(EmSpawnTable *table)
{
    if (!table)
        return;
    free(table->data);
    memset(table, 0, sizeof *table);
}

const uint8_t *em_spawn_table_read(const EmSpawnTable *table, uint32_t address, uint32_t size)
{
    if (!table || size == 0 || address + size < address)
        return NULL;
    for (uint32_t i = 0; i < table->range_count; ++i) {
        const EmSpawnRange *r = &table->ranges[i];
        if (address >= r->address && address + size <= r->address + r->size)
            return r->bytes + (address - r->address);
    }
    return NULL;
}

/* ------------------------------------------------------------ helpers */

static int fault(EmSpawnIo *io, uint32_t address, EmSceneFaultCode code)
{
    if (io->fault.code == EM_SCENE_FAULT_NONE) {
        io->fault.address = address;
        io->fault.code = (int32_t)code;
    }
    return -1;
}

static int read_u32(const EmSpawnTable *t, EmSpawnIo *io, uint32_t fn, uint32_t address, uint32_t *out)
{
    const uint8_t *p = em_spawn_table_read(t, address, 4);
    if (!p)
        return fault(io, fn, EM_SCENE_FAULT_BAD_INDEX);
    *out = le32(p);
    return 0;
}

static float bits_float(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/* The record the originals address: D_0024D650[area][room] + entry * 0x30
 * (lw, lw, then sll/addu: 001B07C0 0x1B07D8..0x1B0828, 001B0250 the same). */
static int record_address(const EmSpawnTable *t, EmSpawnIo *io, uint32_t fn, uint32_t *out)
{
    uint32_t area_table, entries;
    if (read_u32(t, io, fn, EM_SPAWN_TABLE_ADDRESS + 4u * io->d810700, &area_table) < 0 ||
        read_u32(t, io, fn, area_table + 4u * io->d810701, &entries) < 0)
        return -1;
    *out = entries + (uint32_t)io->d810702 * EM_SPAWN_RECORD_SIZE;
    return 0;
}

/* ------------------------------------------------------------ 001B0250 */

int em_spawn_001B0250(const EmSpawnTable *table, EmSpawnIo *io)
{
    uint32_t record, flags;
    if (!io)
        return -1;
    if (record_address(table, io, EM_SPAWN_FN_001B0250, &record) < 0 ||
        read_u32(table, io, EM_SPAWN_FN_001B0250, record + 0x1C, &flags) < 0)
        return -1;
    io->d8106C8 = (int32_t)flags;
    if (io->d810700 == 0x0B && io->d810788 != 0) {
        io->d8106C8 = (int32_t)((uint32_t)io->d8106C8 & 0xF1FFFF8Fu);
        io->d8106C8 = (int32_t)((uint32_t)io->d8106C8 | 0x44u);
    }
    return 0;
}

/* ------------------------------------------------------------ 001B07C0 */

#define ONE 1.0f /* the 0x3F800000 word stores */

int em_spawn_001B07C0(const EmSpawnTable *table, EmSpawnIo *io, const EmSpawnWorkers *w, int arg0)
{
    enum { F = EM_SPAWN_FN_001B07C0 };
    uint32_t record;
    if (!io)
        return -1;
    if (io->fault.code != EM_SCENE_FAULT_NONE)
        return -1;
    EmSpawnPlayer *p = &io->player;

    /* 0x1B07D8..0x1B0828: s1 = the record, loaded before the 001B0250 call. */
    if (record_address(table, io, F, &record) < 0)
        return -1;
    if (em_spawn_001B0250(table, io) < 0)
        return -1;

    if (io->d275BE0 == 1) {
        for (int i = 0; i < 3; ++i)
            p->f0A0[i] = io->d810710[i];
        p->f0A0[3] = ONE;
        for (int i = 0; i < 3; ++i)
            p->f0B0[i] = io->d810710[i];
        p->f0B0[3] = ONE;
        for (int i = 0; i < 3; ++i)
            p->f0C0[i] = io->d810720[i];
        p->f0C0[3] = ONE;
    } else {
        const uint8_t *pos = em_spawn_table_read(table, record, 0x10);
        if (!pos)
            return fault(io, F, EM_SCENE_FAULT_BAD_INDEX);
        for (int i = 0; i < 3; ++i)
            p->f0A0[i] = bits_float(le32(pos + 4 * i));
        p->f0A0[3] = ONE;
        for (int i = 0; i < 3; ++i)
            p->f0B0[i] = bits_float(le32(pos + 4 * i));
        p->f0B0[3] = ONE;
        p->f0C0[0] = 0.0f;
        p->f0C0[1] = bits_float(le32(pos + 0xC));
        p->f0C0[2] = 0.0f;
        p->f0C0[3] = ONE;
    }

    /* Scratchpad 0x70003B40..0x70003B5C = +0xB0..+0xCC; D_00810706 &= 1. */
    uint8_t latch = (uint8_t)(io->d810706 & 1u);
    for (int i = 0; i < 4; ++i)
        io->spad3B40[i] = p->f0B0[i];
    for (int i = 0; i < 4; ++i)
        io->spad3B40[4 + i] = p->f0C0[i];
    io->d810706 = latch;
    p->b235 = io->d810706;
    p->b234 = io->d810707;
    p->f220 = io->d810858;
    p->f228 = io->d81085C;

    uint32_t flags = (uint32_t)io->d8106C8;
    if (flags & 4u) {
        if (io->d810C7E != 0)
            io->d810C60 = io->d810C7D != 0 ? 2 : 1;
        else
            io->d810C60 = 0;
        if (flags & 0x60u) {
            uint32_t object = 0;
            if (!w || !w->w_001EFE00)
                return fault(io, EM_SPAWN_FN_001EFE00, EM_SCENE_FAULT_NULL_WORKER);
            if (w->w_001EFE00(w->ctx, EM_SPAWN_EFFECT_80000018, EM_SPAWN_D_008102B0, &object) < 0)
                return fault(io, EM_SPAWN_FN_001EFE00, EM_SCENE_FAULT_WORKER_FAILED);
            p->w304 = object;
        }
    } else {
        io->d810C60 = 0;
        if (p->w304 != 0) {
            if (!w || !w->s_object_04)
                return fault(io, F, EM_SCENE_FAULT_NULL_WORKER);
            if (w->s_object_04(w->ctx, p->w304, 2) < 0)
                return fault(io, F, EM_SCENE_FAULT_WORKER_FAILED);
            p->w304 = 0;
        }
    }

    if (!w || !w->w_0015C1F0)
        return fault(io, EM_SPAWN_FN_0015C1F0, EM_SCENE_FAULT_NULL_WORKER);
    if (w->w_0015C1F0(w->ctx, EM_SPAWN_D_008102B0) < 0)
        return fault(io, EM_SPAWN_FN_0015C1F0, EM_SCENE_FAULT_WORKER_FAILED);

    const uint8_t *walkout = em_spawn_table_read(table, record + 0x14, 1);
    if (!walkout)
        return fault(io, F, EM_SCENE_FAULT_BAD_INDEX);
    p->b00E = walkout[0];
    for (int i = 0; i < 4; ++i)
        p->f060[i] = ONE;
    for (int i = 0; i < 4; ++i)
        p->f080[i] = ONE;
    p->w230 = 0;

    if (io->d275BE0 == 1) {
        p->b00E = 0;
    } else if (arg0 != 0) {
        if (p->b00E == 1) {
            p->b004 = 5;
            p->b005 = 1;
            p->b006 = 0;
        }
        if (p->w01C != 0 && io->spad3B8D == 0) {
            if (!w || !w->s_object_04)
                return fault(io, F, EM_SCENE_FAULT_NULL_WORKER);
            if (w->s_object_04(w->ctx, p->w01C, 1) < 0)
                return fault(io, F, EM_SCENE_FAULT_WORKER_FAILED);
        }
        /* c.eq.s against 0.0 (mtc1 $zero): -0.0 compares equal, as in C. */
        if (p->f224 != 0.0f || p->f22C != 0.0f) {
            p->f224 = 0.0f;
            p->f22C = 0.0f;
            p->b000 = 1;
        }
    }

    if (!w || !w->w_001B0460)
        return fault(io, EM_SPAWN_FN_001B0460, EM_SCENE_FAULT_NULL_WORKER);
    if (w->w_001B0460(w->ctx, arg0) < 0)
        return fault(io, EM_SPAWN_FN_001B0460, EM_SCENE_FAULT_WORKER_FAILED);
    return 0;
}
