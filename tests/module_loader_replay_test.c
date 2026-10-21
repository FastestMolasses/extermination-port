/* The module loader's comparison-only drive replay (EM_PS2_DRIVE_REPLAY,
 * the user's decision of 2026-10-19; docs/IOP_STREAM.md "Drive replay",
 * docs/MODULE_LOADER.md 1.7): em_module_loader_set_drive_replay.
 *
 * A synthetic pack (written here; no disc data) holds one kind-2 bank
 * module, id 0x1D like 001FF030's: a header sector at INDEX.IDX sector 0x1D,
 * no chunks, a two-sector payload and one relocation of slot 0x44. Its load
 * runs through the translated loader (001FF080 -> 001FF0D0 -> 001FF830)
 * on the measured drive, one dispatch per field, and the test counts the
 * fields the slot-2 record spends in the header poll (step 1) and the
 * payload poll (step 4). Checks:
 *   - without a table the synthetic reads have no measurement: host speed;
 *   - with entries at the read's counter they take the entries' busy
 *     fields (the a00_08 recording's 6 and 75), each served once;
 *   - an entry 2 counters off is taken (counted as near), 3 off is not;
 *   - another sector count, another sector or another file is not taken;
 *   - an unanchored counter takes nothing and counts no miss;
 *   - the table is refused on the host drive, out of counter order and
 *     over EM_MODULE_LOADER_REPLAY_BUSY_MAX.
 * Mutations of the window, the count match or the served flag fail it. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_module_loader.h"
#include "game/em_task.h"

#define SECTOR 0x800u
#define INDEX_LSN 100u
#define DATA_LSN 1000u
#define MODULE 0x1Du
#define PAYLOAD_SECTOR 8u /* DATA.DAT byte 0x4000 */
#define PAYLOAD_SECTORS 2u

static int failures;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            ++failures;                                                                            \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while (0)

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static const char *write_pack(const char *path)
{
    const uint32_t ranges = 2, header = 0x120, table = header + 0x10 * ranges;
    const size_t size = table + (1u + PAYLOAD_SECTORS) * SECTOR;
    uint8_t *b = calloc(1, size);
    if (!b)
        return NULL;
    memcpy(b, "EMML", 4);
    put32(b + 4, 2);
    put32(b + 8, ranges);
    put32(b + 0x10, INDEX_LSN);
    put32(b + 0x14, 0x40u * SECTOR);
    put32(b + 0x18, DATA_LSN);
    put32(b + 0x1C, 0x40u * SECTOR);
    /* cursor seeds: D_00275C70, D_00275C74, D_0028A5A0, D_0028A738,
     * D_0028A73C, D_0028A744, D_0028A748 */
    const uint32_t seeds[7] = {0x289BC0u, 0x10E99C0u, 0x1516F40u, 0x10E99C0u,
                               0x1335F40u, 0x19A3F40u, 0x19A3F40u};
    for (int i = 0; i < 7; ++i)
        put32(b + 0x20 + 4 * i, seeds[i]);
    put32(b + header, INDEX_LSN + MODULE);
    put32(b + header + 4, 1);
    put32(b + header + 8, table);
    put32(b + header + 0x10, DATA_LSN + PAYLOAD_SECTOR);
    put32(b + header + 0x14, PAYLOAD_SECTORS);
    put32(b + header + 0x18, table + SECTOR);
    /* The module header: id, base, total, chunk count 0, no sections, no
     * resident offset, one relocation (slot 0x44 at offset 0). */
    uint8_t *h = b + table;
    put32(h, MODULE);
    put32(h + 4, PAYLOAD_SECTOR * SECTOR);
    put32(h + 8, PAYLOAD_SECTORS * SECTOR);
    put32(h + 0x1C, 1);
    put32(h + 0x20, 0x44u << 24);
    for (uint32_t i = 0; i < PAYLOAD_SECTORS * SECTOR; ++i)
        b[table + SECTOR + i] = (uint8_t)(i * 7u + 3u);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(b, 1, size, f) != size) {
        if (f)
            fclose(f);
        free(b);
        return NULL;
    }
    fclose(f);
    free(b);
    return path;
}

static uint32_t s_counter;
static int s_anchored = 1;
static uint8_t s_bd8;

static int counter_fn(void *ctx, uint32_t *counter)
{
    (void)ctx;
    if (!s_anchored)
        return 0;
    *counter = s_counter;
    return 1;
}

static uint8_t gate_fn(void *ctx)
{
    (void)ctx;
    return 0;
}

typedef struct {
    int header_fields, payload_fields, dispatches, done;
} Load;

/* One whole load from counter `start` (the request and its first dispatch
 * in that field). */
static Load run_load(EmModuleLoader *ml, uint32_t start)
{
    Load L = {0};
    s_bd8 = 1;
    em_task_init();
    em_module_loader_bind_live(ml);
    if (em_module_loader_request_001FF080(ml, 0, MODULE) != 0)
        return L;
    for (int f = 0; f < 400; ++f) {
        s_counter = start + (uint32_t)f;
        if (f)
            em_module_loader_field(ml);
        em_task_dispatch();
        const EmTask *t = em_task_slot(EM_MODULE_LOADER_TASK_SLOT);
        ++L.dispatches;
        if (em_module_loader_failed(ml, NULL))
            return L;
        if (t->user[0] == 0 && t->user[1] == 1)
            ++L.header_fields;
        if (t->user[0] == 0 && t->user[1] == 4)
            ++L.payload_fields;
        if (s_bd8 == 0) {
            L.done = 1;
            return L;
        }
    }
    return L;
}

static EmModuleLoader *open_loader(const char *pack, int drive)
{
    EmModuleLoader *ml = em_module_loader_open(pack);
    if (!ml)
        return NULL;
    static EmModuleLoaderViews views;
    memset(&views, 0, sizeof views);
    views.d275BD8 = &s_bd8;
    views.r_00282157 = gate_fn;
    em_module_loader_set_views(ml, &views);
    if (em_module_loader_set_drive(ml, drive) != 0) {
        em_module_loader_close(ml);
        return NULL;
    }
    return ml;
}

/* Fields in the header / payload poll for a load at counter 50 with the
 * given table (NULL: none). */
static Load case_load(const char *pack, EmModuleLoaderReplayRead *reads, uint32_t n, uint32_t *served,
                      uint32_t *near_, uint32_t *missed)
{
    EmModuleLoader *ml = open_loader(pack, EM_MODULE_LOADER_DRIVE_MEASURED);
    Load L = {0};
    if (!ml)
        return L;
    if (reads && em_module_loader_set_drive_replay(ml, reads, n, counter_fn, NULL) != 0) {
        em_module_loader_close(ml);
        return L;
    }
    L = run_load(ml, 50);
    em_module_loader_drive_replay_stats(ml, served, near_, missed);
    em_module_loader_bind_live(NULL);
    em_module_loader_close(ml);
    return L;
}

int main(int argc, char **argv)
{
    const char *pack = write_pack(argc > 1 ? argv[1] : "build/module_loader_replay/pack.emml");
    if (!pack) {
        fprintf(stderr, "FAIL: cannot write the synthetic pack\n");
        return 1;
    }
    uint32_t served, near_, missed;

    /* No table: host speed (the synthetic reads have no measurement). The
     * request's dispatch issues the header read; its first poll completes. */
    Load host = case_load(pack, NULL, 0, &served, &near_, &missed);
    CHECK(host.done && host.header_fields == 1 && host.payload_fields == 1,
          "host-speed load: done %d header %d payload %d", host.done, host.header_fields,
          host.payload_fields);

    /* The recording's entries (a00_08: the header issued at row 363, the
     * payload 9 rows later; 6 and 75 busy polls). The header is issued at
     * counter 50; step 2 (no chunks) and step 3 follow, so the payload read
     * is issued at 50 + 1 + 6 + 2 = 59. */
    EmModuleLoaderReplayRead t1[2] = {{50, 0, MODULE, 1, 6, 0}, {59, 1, PAYLOAD_SECTOR, PAYLOAD_SECTORS, 75, 0}};
    Load a = case_load(pack, t1, 2, &served, &near_, &missed);
    CHECK(a.done && a.header_fields == 7 && a.payload_fields == 76,
          "replayed load: done %d header %d payload %d (want 7 / 76)", a.done, a.header_fields,
          a.payload_fields);
    CHECK(served == 2 && near_ == 0 && missed == 0 && t1[0].served && t1[1].served,
          "replayed load: served %u near %u missed %u", served, near_, missed);

    /* 2 counters off on either side: taken, counted as near. */
    EmModuleLoaderReplayRead t2[2] = {{48, 0, MODULE, 1, 6, 0}, {61, 1, PAYLOAD_SECTOR, PAYLOAD_SECTORS, 75, 0}};
    Load b = case_load(pack, t2, 2, &served, &near_, &missed);
    CHECK(b.header_fields == 7 && b.payload_fields == 76 && served == 2 && near_ == 2,
          "edge of the window: header %d payload %d served %u near %u", b.header_fields,
          b.payload_fields, served, near_);

    /* 3 off: not taken (the measured table has nothing either: host speed). */
    EmModuleLoaderReplayRead t3[1] = {{53, 0, MODULE, 1, 6, 0}};
    Load c = case_load(pack, t3, 1, &served, &near_, &missed);
    CHECK(c.header_fields == 1 && served == 0 && missed == 2 && !t3[0].served,
          "3 counters off: header %d served %u missed %u", c.header_fields, served, missed);
    EmModuleLoaderReplayRead t3b[1] = {{47, 0, MODULE, 1, 6, 0}};
    Load c2 = case_load(pack, t3b, 1, &served, &near_, &missed);
    CHECK(c2.header_fields == 1 && served == 0, "3 counters early: header %d served %u",
          c2.header_fields, served);

    /* Another count, another sector, another file: not taken. */
    EmModuleLoaderReplayRead t4[3] = {{50, 0, MODULE, 2, 6, 0}, {50, 0, MODULE + 1, 1, 6, 0},
                                      {50, 1, MODULE, 1, 6, 0}};
    Load d = case_load(pack, t4, 3, &served, &near_, &missed);
    CHECK(d.header_fields == 1 && served == 0, "mismatched entries: header %d served %u",
          d.header_fields, served);

    /* Served once: two loads, one entry. */
    {
        EmModuleLoaderReplayRead t5[1] = {{50, 0, MODULE, 1, 6, 0}};
        EmModuleLoader *ml = open_loader(pack, EM_MODULE_LOADER_DRIVE_MEASURED);
        CHECK(ml && em_module_loader_set_drive_replay(ml, t5, 1, counter_fn, NULL) == 0, "set table");
        if (ml) {
            Load e1 = run_load(ml, 50);
            Load e2 = run_load(ml, 50);
            CHECK(e1.header_fields == 7 && e2.header_fields == 1, "served once: %d then %d",
                  e1.header_fields, e2.header_fields);
            em_module_loader_bind_live(NULL);
            em_module_loader_close(ml);
        }
    }

    /* Not anchored: nothing taken, no miss counted. */
    s_anchored = 0;
    EmModuleLoaderReplayRead t6[1] = {{50, 0, MODULE, 1, 6, 0}};
    Load g = case_load(pack, t6, 1, &served, &near_, &missed);
    CHECK(g.header_fields == 1 && served == 0 && missed == 0, "unanchored: header %d served %u missed %u",
          g.header_fields, served, missed);
    s_anchored = 1;

    /* Refusals. */
    {
        EmModuleLoaderReplayRead ok[2] = {{50, 0, MODULE, 1, 6, 0}, {59, 1, 8, 2, 75, 0}};
        EmModuleLoaderReplayRead order[2] = {{59, 0, MODULE, 1, 6, 0}, {50, 1, 8, 2, 75, 0}};
        EmModuleLoaderReplayRead big[1] = {{50, 0, MODULE, 1, EM_MODULE_LOADER_REPLAY_BUSY_MAX + 1u, 0}};
        EmModuleLoader *hostml = open_loader(pack, EM_MODULE_LOADER_DRIVE_HOST);
        CHECK(hostml && em_module_loader_set_drive_replay(hostml, ok, 2, counter_fn, NULL) == -1,
              "the host drive must refuse a table");
        em_module_loader_close(hostml);
        EmModuleLoader *ml = open_loader(pack, EM_MODULE_LOADER_DRIVE_MEASURED);
        CHECK(ml && em_module_loader_set_drive_replay(ml, order, 2, counter_fn, NULL) == -1,
              "out of counter order must be refused");
        CHECK(ml && em_module_loader_set_drive_replay(ml, big, 1, counter_fn, NULL) == -1,
              "a busy count over the maximum must be refused");
        CHECK(ml && em_module_loader_set_drive_replay(ml, ok, 2, NULL, NULL) == -1,
              "a table without a counter must be refused");
        em_module_loader_close(ml);
    }

    if (failures) {
        fprintf(stderr, "module loader replay test: %d failures\n", failures);
        return 1;
    }
    printf("module loader replay test: PASS (host speed 1/1 poll fields; replayed 7/76; window +-2 "
           "taken, +-3 not; count / sector / file mismatches not taken; served once; unanchored "
           "nothing; 4 refusals)\n");
    return 0;
}
