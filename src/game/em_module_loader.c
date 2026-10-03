/* em_module_loader.c — the screen-module loader's disc/DMA layer and its
 * live slot-2 binding. See em_module_loader.h and docs/MODULE_LOADER.md.
 *
 * Checked by tools/test_module_loader_reference.py against the original
 * instructions of 00200780, 00200730, 00200830, 00200890 and 00200970, and
 * (whole loads) the original 001FF080 / 001FF0D0 / 001FF830 / 001FF3F0 /
 * 00200780 / 00200730 / 00200830 / 00101BB8 over the captured route RAM.
 */
#include "game/em_module_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------
 * Fail-stop plumbing (codes are the EM_STATUS_SCENE_FAULT_* codes). */
static int fail(EmStatusSceneFault *fault, uint32_t address, int32_t code)
{
    if (fault && fault->code == EM_STATUS_SCENE_FAULT_NONE) {
        fault->address = address;
        fault->code = code;
    }
    return -1;
}

#define LEAF(fn, addr)                                                                          \
    do {                                                                                        \
        if (!sdk || !sdk->fn)                                                                   \
            return fail(fault, (addr), EM_STATUS_SCENE_FAULT_NULL_WORKER);                      \
    } while (0)
#define CALL(addr, expr)                                                                        \
    do {                                                                                        \
        if ((expr) < 0)                                                                         \
            return fail(fault, (addr), EM_STATUS_SCENE_FAULT_WORKER_FAILED);                    \
    } while (0)

/* ------------------------------------------------------------------------
 * The translations. */

int em_module_loader_read_00200780(const EmModuleLoaderSdk *sdk, const uint32_t file[2],
                                   uint32_t buf, int32_t offset, int32_t size, int32_t *bytes,
                                   EmStatusSceneFault *fault)
{
    if (!file || !bytes)
        return fail(fault, 0x00200780u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    LEAF(ready_00113280, 0x00113280u);
    LEAF(read_00112440, 0x00112440u);
    /* The read mode on the stack: trycount 0, spindlctrl 1, datapattern 0. */
    const uint8_t mode[3] = {0, 1, 0};
    uint32_t sectors;
    if (size >= 0)
        sectors = (uint32_t)((int32_t)((uint32_t)size + 0x7FFu) >> 11); /* arithmetic */
    else
        sectors = (file[1] + 0x7FFu) >> 11; /* the whole file, logical */
    uint32_t first = (uint32_t)(offset >> 11); /* arithmetic */
    int32_t reply, accepted;
    CALL(0x00113280u, sdk->ready_00113280(sdk->ctx, 0, &reply));
    for (;;) {
        /* The descriptor's lsn is re-read for every attempt. */
        CALL(0x00112440u,
             sdk->read_00112440(sdk->ctx, file[0] + first, sectors, buf, mode, &accepted));
        if (accepted != 0)
            break;
        CALL(0x00113280u, sdk->ready_00113280(sdk->ctx, 0, &reply));
    }
    *bytes = (int32_t)(sectors << 11);
    return 0;
}

int em_module_loader_poll_00200730(const EmModuleLoaderSdk *sdk, int32_t *status,
                                   EmStatusSceneFault *fault)
{
    if (!status)
        return fail(fault, 0x00200730u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    LEAF(sync_00112D18, 0x00112D18u);
    int32_t busy, error;
    CALL(0x00112D18u, sdk->sync_00112D18(sdk->ctx, 1, &busy));
    if (busy != 0) {
        *status = 0;
        return 0;
    }
    LEAF(error_00113680, 0x00113680u);
    CALL(0x00113680u, sdk->error_00113680(sdk->ctx, &error));
    *status = error == 0 ? 1 : 2;
    return 0;
}

int em_module_loader_dma_00200830(const EmModuleLoaderSdk *sdk, uint32_t chain,
                                  EmStatusSceneFault *fault)
{
    LEAF(channel_00101BB8, 0x00101BB8u);
    LEAF(wait_00102468, 0x00102468u);
    LEAF(send_00101F08, 0x00101F08u);
    uint32_t base;
    CALL(0x00101BB8u, sdk->channel_00101BB8(sdk->ctx, 1, &base));
    CALL(0x00102468u, sdk->wait_00102468(sdk->ctx, base, 0, 0));
    CALL(0x00101F08u, sdk->send_00101F08(sdk->ctx, base, chain));
    CALL(0x00102468u, sdk->wait_00102468(sdk->ctx, base, 0, 0));
    return 0;
}

int em_module_loader_packet_00200890(const EmModuleLoaderSdk *sdk, uint8_t d810707,
                                     uint8_t d810C60, const uint32_t d28A4B0[5],
                                     uint32_t *chain, EmStatusSceneFault *fault)
{
    if (!d28A4B0)
        return fail(fault, 0x0028A4B0u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    /* d28A4B0[k] = D_0028A4B0 + 4k: [0] 4B0, [1] 4B4, [2] 4B8, [3] 4BC, [4] 4C0. */
    uint32_t word;
    if (d810707 == 0)
        word = d810C60 == 2 ? d28A4B0[2] : d810C60 == 1 ? d28A4B0[3] : d28A4B0[0];
    else if (d810707 == 1)
        word = d810C60 == 2 ? d28A4B0[2] : d810C60 == 1 ? d28A4B0[3] : d28A4B0[4];
    else
        word = d28A4B0[1];
    if (chain)
        *chain = word;
    return em_module_loader_dma_00200830(sdk, word, fault);
}

int em_module_loader_restore_00200970(const EmModuleLoaderSdk *sdk, int32_t a0,
                                      uint32_t d28A564, const uint8_t *spad3B90, uint8_t d810707,
                                      uint8_t d810C60, const uint32_t d28A4B0[5],
                                      EmStatusSceneFault *fault)
{
    if (em_module_loader_dma_00200830(sdk, d28A564, fault) < 0)
        return -1;
    if (a0 == 0) {
        LEAF(upload_001CCB10, 0x001CCB10u);
        if (!spad3B90)
            return fail(fault, 0x70003B90u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
        CALL(0x001CCB10u, sdk->upload_001CCB10(sdk->ctx));
        /* The byte is read after 001CCB10 returns, as the original does. */
        if (*spad3B90 != 2)
            return 0;
    }
    return em_module_loader_packet_00200890(sdk, d810707, d810C60, d28A4B0, NULL, fault);
}

/* ------------------------------------------------------------------------
 * The pack (tools/export_module_loader.py, EMML version 2, little endian):
 *   0x00 'EMML', u32 version 2, u32 range count, u32 0
 *   0x10 u32 D_0028A480 {lsn, size}, D_0028A488 {lsn, size}
 *   0x20 u32 D_00275C70, D_00275C74, D_0028A5A0, D_0028A738, D_0028A73C,
 *        D_0028A744, D_0028A748 (the captured values before a load), u32 0
 *   0x40 u32 D_00275304[0], u32 0, u32 D_00264890[0..4], u32 0
 *   0x60 D_0028A3C0: 0x17 x {u32 lsn, u32 size}, 8 zero bytes
 *   0x120 ranges {u32 lsn, u32 sectors, u32 file offset, u32 0}, then data. */
#define PACK_HEADER 0x120u
#define PACK_VERSION 2u
#define PACK_RANGE 0x10u
#define SECTOR 0x800u

typedef struct {
    uint32_t lsn, sectors;
    const uint8_t *bytes;
} Range;

typedef struct {
    uint32_t address, size;
    uint8_t *bytes;
} Region;

#define MAX_REGIONS 16
#define MAX_READ_SECTORS 0x4000u /* 32 MiB: no EE buffer is larger */

/* The measured drive (docs/STATUS_LOAD_WAIT_PROBE.md, decomp CAPTURES_C7.md
 * section 6): busy fields between a read's kick and the poll that
 * completes it, per read, in PCSX2 on the rebuilt disc image. A read is
 * named by its file (0 = the D_0028A480 file INDEX.IDX, 1 = the D_0028A488
 * file DATA.DAT), its first sector inside that file and its sector count,
 * so the table does not depend on where an image places the two files.
 * Only these reads were measured; any other read is answered at host speed
 * and counted in `unmeasured`. */
typedef struct {
    uint32_t file, sector, sectors, busy;
} Measured;
/* File 0x10 + a is the area file D_0028A3C0[a] (001FFCD0 state 0). The
 * New Game rows are the PCSX2 New Game capture's (the decomp's
 * build/startup-reference/newgame_samples.jsonl: the slot-2 record sampled
 * about twice a frame while the VM ran, so each row's frame is known to
 * within the frame; docs/MODULE_LOADER.md 1.7). */
static const Measured MEASURED[] = {
    {0u, 0x21u, 1u, 6u},                  /* module 0x21 header: INDEX.IDX sector 0x21 */
    {1u, 0x0E4BC000u >> 11, 161u, 8u},    /* module 0x21 chunk 0: DATA.DAT byte 0xE4BC000 */
    {0u, 0x03u, 1u, 6u},                  /* New Game module 3 header: INDEX.IDX sector 3 */
    {1u, 0x00214800u >> 11, 1175u, 54u},  /* module 3 payload: DATA.DAT byte 0x214800 */
    {0x10u + 0x0Bu, 0u, 15u, 6u},         /* AREA11 overlay: \OVERLAY\AREA11.BIN, whole */
    {0u, 0x0Fu, 1u, 1u},                  /* AREA11 header: INDEX.IDX sector 0x0F */
    {1u, 0x076E7800u >> 11, 149u, 11u},   /* AREA11 sound bank: DATA.DAT byte 0x76E7800 */
    {1u, 0x07732000u >> 11, 433u, 16u},   /* AREA11 A entry 1: DATA.DAT byte 0x7732000 */
    {1u, 0x0780A800u >> 11, 3292u, 131u}, /* AREA11 resident region: DATA.DAT byte 0x780A800 */
};

struct EmModuleLoader {
    EmStatusSceneLoader ld; /* the loader globals (single storage while bound) */
    uint32_t d28A480[2], d28A488[2];
    EmTask *record;
    EmModuleLoaderViews views;
    uint8_t local_bd8, local_gate, local_ca4, local_ca6, local_3B90;
    /* pack */
    uint8_t *pack;
    size_t pack_size;
    Range *ranges;
    uint32_t range_count;
    /* drive */
    int drive;
    uint32_t field, busy_until;
    Region regions[MAX_REGIONS];
    /* hooks */
    EmModuleLoaderChainHook chain_hook;
    void *chain_ctx;
    EmModuleLoaderChainHook area_chain_hook;
    void *area_chain_ctx;
    EmModuleLoaderBankHook bank_hook;
    void *bank_ctx;
    EmModuleLoaderAreaDone area_done;
    void *area_done_ctx;
    /* the boot's tables (the pack's 0x40 block) */
    uint32_t d275304;
    int32_t d264890[5];
    uint32_t d28A3C0[EM_STATUS_SCENE_AREA_FILES][2];
    EmModuleLoaderTrace trace;
    void *trace_ctx;
    /* workers */
    EmModuleLoaderSdk sdk;
    EmStatusSceneWorkers workers;
    EmStatusSceneFault fault, io_fault;
    uint32_t dispatches, reads, unmeasured;
    int in_dispatch; /* the views are copied into `ld` while a dispatch runs */
};

static EmModuleLoader *s_live;
/* The slot-2 task ran with no live loader (unbound or closed mid-load). */
static EmStatusSceneFault s_orphan;

static uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void u32_put(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void trace(EmModuleLoader *ml, uint32_t callee, uint32_t a0, uint32_t a1, uint32_t a2,
                  uint32_t a3)
{
    if (ml->trace)
        ml->trace(ml->trace_ctx, callee, a0, a1, a2, a3);
}

/* ---- the host drive (EmModuleLoaderSdk over the pack) ---- */

static const uint8_t *pack_sectors(const EmModuleLoader *ml, uint32_t lsn, uint32_t sectors)
{
    for (uint32_t i = 0; i < ml->range_count; ++i) {
        const Range *r = &ml->ranges[i];
        if (lsn >= r->lsn && lsn - r->lsn <= r->sectors && sectors <= r->sectors - (lsn - r->lsn))
            return r->bytes + (size_t)(lsn - r->lsn) * SECTOR;
    }
    return NULL;
}

static int overlaps(uint32_t a, uint32_t n, uint32_t b, uint32_t m)
{
    return (uint64_t)a < (uint64_t)b + m && (uint64_t)b < (uint64_t)a + n;
}

/* The host storage a read of `size` bytes at original `buf` lands in. */
static uint8_t *region_for(EmModuleLoader *ml, uint32_t buf, uint32_t size)
{
    if (buf == EM_STATUS_SCENE_D_00289BC0)
        return size <= sizeof ml->ld.header ? ml->ld.header : NULL;
    if (overlaps(buf, size, EM_STATUS_SCENE_D_00289BC0, sizeof ml->ld.header))
        return NULL;
    Region *slot = NULL;
    for (int i = 0; i < MAX_REGIONS; ++i) {
        Region *r = &ml->regions[i];
        if (r->bytes && r->address == buf) {
            slot = r;
        } else if (r->bytes && overlaps(buf, size, r->address, r->size)) {
            free(r->bytes); /* a newer read replaced these bytes */
            memset(r, 0, sizeof *r);
        }
    }
    for (int i = 0; !slot && i < MAX_REGIONS; ++i)
        if (!ml->regions[i].bytes)
            slot = &ml->regions[i];
    if (!slot)
        return NULL;
    if (!slot->bytes || slot->size < size) {
        uint8_t *bytes = realloc(slot->bytes, size ? size : 1);
        if (!bytes)
            return NULL;
        slot->bytes = bytes;
        slot->size = size;
    }
    slot->address = buf;
    return slot->bytes;
}

static int drive_ready(void *ctx, int32_t mode, int32_t *reply)
{
    EmModuleLoader *ml = ctx;
    trace(ml, 0x00113280u, (uint32_t)mode, 0, 0, 0);
    /* 00200780 ignores the reply; the host drive is always ready. */
    *reply = 2;
    return 0;
}

static int drive_read(void *ctx, uint32_t lsn, uint32_t sectors, uint32_t buf,
                      const uint8_t mode[3], int32_t *accepted)
{
    EmModuleLoader *ml = ctx;
    trace(ml, 0x00112440u, lsn, sectors, buf,
          (uint32_t)mode[0] | (uint32_t)mode[1] << 8 | (uint32_t)mode[2] << 16);
    if (ml->drive == EM_MODULE_LOADER_DRIVE_MEASURED && ml->field < ml->busy_until)
        return fail(&ml->io_fault, 0x00112440u, EM_STATUS_SCENE_FAULT_BAD_RESULT);
    if (sectors > MAX_READ_SECTORS)
        return fail(&ml->io_fault, 0x00112440u, EM_STATUS_SCENE_FAULT_BAD_INDEX);
    if (sectors) {
        const uint8_t *src = pack_sectors(ml, lsn, sectors);
        if (!src) /* sectors the pack does not hold: fail-stop */
            return fail(&ml->io_fault, 0x00112440u, EM_STATUS_SCENE_FAULT_BAD_INDEX);
        uint8_t *dst = region_for(ml, buf, sectors * SECTOR);
        if (!dst)
            return fail(&ml->io_fault, buf, EM_STATUS_SCENE_FAULT_BAD_INDEX);
        memcpy(dst, src, (size_t)sectors * SECTOR);
    }
    uint32_t busy = 0;
    if (ml->drive == EM_MODULE_LOADER_DRIVE_MEASURED) {
        int found = sectors == 0; /* a zero-sector read completes at its first poll */
        for (size_t i = 0; !found && i < sizeof MEASURED / sizeof MEASURED[0]; ++i) {
            const uint32_t f = MEASURED[i].file;
            const uint32_t base = f == 0 ? ml->d28A480[0]
                                  : f == 1 ? ml->d28A488[0]
                                  : f - 0x10u < EM_STATUS_SCENE_AREA_FILES ? ml->d28A3C0[f - 0x10u][0]
                                                                           : 0u;
            if (base + MEASURED[i].sector == lsn && MEASURED[i].sectors == sectors) {
                busy = MEASURED[i].busy;
                found = 1;
            }
        }
        if (!found)
            ++ml->unmeasured;
    }
    ml->busy_until = ml->field + busy + (busy ? 1u : 0u);
    ++ml->reads;
    *accepted = 1;
    return 0;
}

static int drive_sync(void *ctx, int32_t mode, int32_t *busy)
{
    EmModuleLoader *ml = ctx;
    trace(ml, 0x00112D18u, (uint32_t)mode, 0, 0, 0);
    if (mode != 1) /* the blocking form is not on the translated path */
        return fail(&ml->io_fault, 0x00112D18u, EM_STATUS_SCENE_FAULT_BAD_RESULT);
    *busy = ml->field < ml->busy_until ? 1 : 0;
    return 0;
}

static int drive_error(void *ctx, int32_t *error)
{
    EmModuleLoader *ml = ctx;
    trace(ml, 0x00113680u, 0, 0, 0, 0);
    *error = 0;
    return 0;
}

static int dma_channel(void *ctx, int32_t channel, uint32_t *base)
{
    EmModuleLoader *ml = ctx;
    trace(ml, 0x00101BB8u, (uint32_t)channel, 0, 0, 0);
    if (channel != 1) /* only VIF1 is on the translated path */
        return fail(&ml->io_fault, 0x00101BB8u, EM_STATUS_SCENE_FAULT_BAD_INDEX);
    *base = EM_MODULE_LOADER_DMA_VIF1;
    return 0;
}

static int dma_wait(void *ctx, uint32_t base, int32_t mode, int32_t timeout)
{
    EmModuleLoader *ml = ctx;
    trace(ml, 0x00102468u, base, (uint32_t)mode, (uint32_t)timeout, 0);
    /* The host consumer takes a chain synchronously: nothing is in flight. */
    return 0;
}

static int dma_send(void *ctx, uint32_t base, uint32_t chain)
{
    EmModuleLoader *ml = ctx;
    trace(ml, 0x00101F08u, base, chain, 0, 0);
    if (base != EM_MODULE_LOADER_DMA_VIF1)
        return fail(&ml->io_fault, 0x00101F08u, EM_STATUS_SCENE_FAULT_BAD_INDEX);
    /* The area streamer's sends (record +8 == 1) go to the area's consumer,
     * the bank streamer's to the page consumer. */
    const int area = ml->record && ml->record->user[0] == 1;
    EmModuleLoaderChainHook hook = area ? ml->area_chain_hook : ml->chain_hook;
    void *hook_ctx = area ? ml->area_chain_ctx : ml->chain_ctx;
    if (!hook)
        return fail(&ml->io_fault, 0x00101F08u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    for (int i = 0; i < MAX_REGIONS; ++i) {
        const Region *r = &ml->regions[i];
        if (r->bytes && chain >= r->address && chain - r->address < r->size) {
            uint32_t at = chain - r->address;
            if (hook(hook_ctx, chain, r->bytes + at, r->size - at) < 0)
                return fail(&ml->io_fault, 0x00101F08u, EM_STATUS_SCENE_FAULT_WORKER_FAILED);
            return 0;
        }
    }
    /* A chain the drive did not deliver (resident original data): fail-stop. */
    return fail(&ml->io_fault, chain, EM_STATUS_SCENE_FAULT_BAD_INDEX);
}

/* ---- the loader's workers over the translations ---- */

static int w_read(void *ctx, uint32_t file, uint32_t buf, int32_t offset, int32_t size,
                  uint8_t *header)
{
    EmModuleLoader *ml = ctx;
    trace(ml, 0x00200780u, file, buf, (uint32_t)offset, (uint32_t)size);
    /* The descriptor words: D_0028A480 (INDEX.IDX), D_0028A488 (DATA.DAT)
     * or an area file of D_0028A3C0 (001FFCD0 state 0). */
    const uint32_t *desc = file == EM_MODULE_LOADER_D_0028A480   ? ml->d28A480
                           : file == EM_MODULE_LOADER_D_0028A488 ? ml->d28A488
                                                                 : NULL;
    if (!desc && file >= 0x0028A3C0u && file < 0x0028A3C0u + 8u * EM_STATUS_SCENE_AREA_FILES &&
        (file & 7u) == 0)
        desc = ml->d28A3C0[(file - 0x0028A3C0u) >> 3];
    if (!desc)
        return fail(&ml->io_fault, file, EM_STATUS_SCENE_FAULT_BAD_INDEX);
    if ((header != NULL) != (buf == EM_STATUS_SCENE_D_00289BC0))
        return fail(&ml->io_fault, buf, EM_STATUS_SCENE_FAULT_BAD_INDEX);
    int32_t bytes;
    return em_module_loader_read_00200780(&ml->sdk, desc, buf, offset, size, &bytes,
                                          &ml->io_fault);
}

static int w_poll(void *ctx, int32_t *status)
{
    EmModuleLoader *ml = ctx;
    trace(ml, 0x00200730u, 0, 0, 0, 0);
    return em_module_loader_poll_00200730(&ml->sdk, status, &ml->io_fault);
}

static int w_section(void *ctx, uint32_t address)
{
    EmModuleLoader *ml = ctx;
    trace(ml, 0x00200830u, address, 0, 0, 0);
    return em_module_loader_dma_00200830(&ml->sdk, address, &ml->io_fault);
}

/* The host bytes of the one region that holds [address, address + 4), or NULL. */
static const Region *region_at(const EmModuleLoader *ml, uint32_t address)
{
    for (int i = 0; i < MAX_REGIONS; ++i) {
        const Region *r = &ml->regions[i];
        if (r->bytes && address >= r->address && address - r->address < r->size)
            return r;
    }
    return NULL;
}

/* 001FB370(bank): the file as the drive delivered it, from `address` to
 * the end of its region, through the binder's bank hook. */
static int w_bank(void *ctx, uint32_t address, uint32_t *result)
{
    EmModuleLoader *ml = ctx;
    trace(ml, 0x001FB370u, address, 0, 0, 0);
    const Region *r = region_at(ml, address);
    if (!ml->bank_hook)
        return fail(&ml->io_fault, 0x001FB370u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    if (!r) /* a bank the drive did not deliver: fail-stop */
        return fail(&ml->io_fault, address, EM_STATUS_SCENE_FAULT_BAD_INDEX);
    const uint32_t at = address - r->address;
    if (ml->bank_hook(ml->bank_ctx, address, r->bytes + at, r->size - at, result) < 0)
        return fail(&ml->io_fault, 0x001FB370u, EM_STATUS_SCENE_FAULT_WORKER_FAILED);
    return 0;
}

/* 00200890: the player's texture packet by D_00810707 / D_00810C60 over
 * the slot words D_0028A4B0..D_0028A4C0 (the table's slots 8..12). */
static int w_packet(void *ctx)
{
    EmModuleLoader *ml = ctx;
    if (!ml->views.d810707 || !ml->views.d810C60)
        return fail(&ml->io_fault, 0x00200890u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    return em_module_loader_packet_00200890(&ml->sdk, *ml->views.d810707, *ml->views.d810C60,
                                            &ml->ld.d28A490[EM_STATUS_SCENE_SLOT_D_0028A4B0], NULL,
                                            &ml->io_fault);
}

/* 002009E0(p, off) (byte-matched C): FlushCache(2) (the EE caches are not
 * host state), then n = the overlay's word +0x14 and, when n != 0, the n
 * bytes at p + off (the overlay's bss after its file) are cleared: here in
 * the drive's memory, where the loaded overlay is (the port's AREA11
 * overlay code is native and keeps its own storage). */
static int w_overlay(void *ctx, uint32_t p, uint32_t off)
{
    EmModuleLoader *ml = ctx;
    trace(ml, 0x002009E0u, p, off, 0, 0);
    const Region *r = region_at(ml, p + 0x14u);
    if (!r || r->size - (p + 0x14u - r->address) < 4)
        return fail(&ml->io_fault, p + 0x14u, EM_STATUS_SCENE_FAULT_BAD_INDEX);
    const uint32_t n = u32_at(r->bytes + (p + 0x14u - r->address));
    if (n == 0)
        return 0;
    uint8_t *bss = region_for(ml, p + off, n);
    if (!bss)
        return fail(&ml->io_fault, p + off, EM_STATUS_SCENE_FAULT_BAD_INDEX);
    memset(bss, 0, n);
    return 0;
}

/* 001FFCD0 over the area views, the pack's D_0028A3C0 and D_00275304[0]. */
static int w_area(void *ctx, uint8_t user[24])
{
    EmModuleLoader *ml = ctx;
    const EmModuleLoaderViews *v = &ml->views;
    if (!v->d810700 || !v->d810701 || !v->d810703 || !v->d810704)
        return fail(&ml->io_fault, 0x001FFCD0u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    EmStatusSceneArea area;
    area.d810700 = *v->d810700;
    area.d810701 = *v->d810701;
    area.d810703 = *v->d810703;
    area.d810704 = *v->d810704;
    area.d275304 = ml->d275304;
    memcpy(area.d28A3C0, ml->d28A3C0, sizeof area.d28A3C0);
    int r = em_status_scene_area_001FFCD0(user, &ml->ld, &area, &ml->workers, &ml->fault);
    *v->d810701 = area.d810701;
    *v->d810703 = area.d810703;
    *v->d810704 = area.d810704;
    if (r == 0 && user[0] == 0x63 && ml->area_done &&
        ml->area_done(ml->area_done_ctx, area.d810700, area.d810704) < 0)
        return fail(&ml->io_fault, 0x001FFCD0u, EM_STATUS_SCENE_FAULT_WORKER_FAILED);
    return r;
}

/* ---- lifetime ---- */

EmModuleLoader *em_module_loader_open(const char *pack_path)
{
    if (!pack_path)
        return NULL;
    FILE *f = fopen(pack_path, "rb");
    if (!f)
        return NULL;
    EmModuleLoader *ml = calloc(1, sizeof *ml);
    uint8_t *data = NULL;
    long n = -1;
    if (ml && fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= (long)PACK_HEADER &&
        fseek(f, 0, SEEK_SET) == 0 && (data = malloc((size_t)n)) &&
        fread(data, 1, (size_t)n, f) == (size_t)n) {
        fclose(f);
        f = NULL;
    }
    if (f)
        fclose(f);
    if (!ml || !data || n < (long)PACK_HEADER || memcmp(data, "EMML", 4) != 0 ||
        u32_at(data + 4) != PACK_VERSION) {
        free(data);
        free(ml);
        return NULL;
    }
    size_t size = (size_t)n;
    uint32_t count = u32_at(data + 8);
    if (count == 0 || count > (size - PACK_HEADER) / PACK_RANGE) {
        free(data);
        free(ml);
        return NULL;
    }
    Range *ranges = calloc(count, sizeof *ranges);
    if (!ranges) {
        free(data);
        free(ml);
        return NULL;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *e = data + PACK_HEADER + i * PACK_RANGE;
        uint32_t lsn = u32_at(e), sectors = u32_at(e + 4), off = u32_at(e + 8);
        if (sectors == 0 || sectors > MAX_READ_SECTORS || off > size ||
            (size_t)sectors * SECTOR > size - off) {
            free(ranges);
            free(data);
            free(ml);
            return NULL;
        }
        ranges[i] = (Range){lsn, sectors, data + off};
    }
    ml->pack = data;
    ml->pack_size = size;
    ml->ranges = ranges;
    ml->range_count = count;
    ml->d28A480[0] = u32_at(data + 0x10);
    ml->d28A480[1] = u32_at(data + 0x14);
    ml->d28A488[0] = u32_at(data + 0x18);
    ml->d28A488[1] = u32_at(data + 0x1C);
    ml->ld.d275C70 = u32_at(data + 0x20);
    ml->ld.d275C74 = u32_at(data + 0x24);
    ml->ld.d28A490[EM_STATUS_SCENE_SLOT_D_0028A5A0] = u32_at(data + 0x28);
    ml->ld.d28A490[EM_STATUS_SCENE_SLOT_D_0028A738] = u32_at(data + 0x2C);
    ml->ld.d28A490[EM_STATUS_SCENE_SLOT_D_0028A73C] = u32_at(data + 0x30);
    ml->ld.d28A490[EM_STATUS_SCENE_SLOT_D_0028A744] = u32_at(data + 0x34);
    ml->ld.d28A490[EM_STATUS_SCENE_SLOT_D_0028A748] = u32_at(data + 0x38);
    ml->d275304 = u32_at(data + 0x40);
    for (int i = 0; i < 5; ++i)
        ml->d264890[i] = (int32_t)u32_at(data + 0x48 + 4 * i);
    for (int i = 0; i < EM_STATUS_SCENE_AREA_FILES; ++i) {
        ml->d28A3C0[i][0] = u32_at(data + 0x60 + 8 * i);
        ml->d28A3C0[i][1] = u32_at(data + 0x64 + 8 * i);
    }
    ml->sdk = (EmModuleLoaderSdk){ml,         drive_ready, drive_read, drive_sync, drive_error,
                                  dma_channel, dma_wait,   dma_send,   NULL};
    ml->workers.ctx = ml;
    ml->workers.w_00200780 = w_read;
    ml->workers.w_00200730 = w_poll;
    ml->workers.w_00200830 = w_section;
    ml->workers.w_001FFCD0 = w_area;
    ml->workers.w_001FB370 = w_bank;
    ml->workers.w_00200890 = w_packet;
    ml->workers.w_002009E0 = w_overlay;
    /* 00200360 (the bank-set streamer, +8 == 2) is not translated: reaching
     * it faults. */
    return ml;
}

void em_module_loader_close(EmModuleLoader *ml)
{
    if (!ml)
        return;
    if (s_live == ml)
        s_live = NULL;
    for (int i = 0; i < MAX_REGIONS; ++i)
        free(ml->regions[i].bytes);
    free(ml->ranges);
    free(ml->pack);
    free(ml);
}

void em_module_loader_set_views(EmModuleLoader *ml, const EmModuleLoaderViews *views)
{
    if (!ml)
        return;
    if (views)
        ml->views = *views;
    else
        memset(&ml->views, 0, sizeof ml->views);
}

void em_module_loader_set_chain_hook(EmModuleLoader *ml, EmModuleLoaderChainHook hook, void *ctx)
{
    if (!ml)
        return;
    ml->chain_hook = hook;
    ml->chain_ctx = ctx;
}

void em_module_loader_set_area_chain_hook(EmModuleLoader *ml, EmModuleLoaderChainHook hook,
                                          void *ctx)
{
    if (!ml)
        return;
    ml->area_chain_hook = hook;
    ml->area_chain_ctx = ctx;
}

void em_module_loader_set_area_done_hook(EmModuleLoader *ml, EmModuleLoaderAreaDone hook, void *ctx)
{
    if (!ml)
        return;
    ml->area_done = hook;
    ml->area_done_ctx = ctx;
}

void em_module_loader_set_bank_hook(EmModuleLoader *ml, EmModuleLoaderBankHook hook, void *ctx)
{
    if (!ml)
        return;
    ml->bank_hook = hook;
    ml->bank_ctx = ctx;
}

void em_module_loader_bank_bases(const EmModuleLoader *ml, int32_t out[5])
{
    for (int i = 0; i < 5; ++i)
        out[i] = ml ? ml->d264890[i] : 0;
}

void em_module_loader_set_trace(EmModuleLoader *ml, EmModuleLoaderTrace fn, void *ctx)
{
    if (!ml)
        return;
    ml->trace = fn;
    ml->trace_ctx = ctx;
}

int em_module_loader_set_drive(EmModuleLoader *ml, int mode)
{
    if (!ml || (mode != EM_MODULE_LOADER_DRIVE_HOST && mode != EM_MODULE_LOADER_DRIVE_MEASURED))
        return -1;
    ml->drive = mode;
    return 0;
}

void em_module_loader_bind_live(EmModuleLoader *ml)
{
    s_live = ml;
    if (ml)
        s_orphan = (EmStatusSceneFault){0, EM_STATUS_SCENE_FAULT_NONE};
}

EmModuleLoader *em_module_loader_live(void)
{
    return s_live;
}

const void *em_module_loader_image(const EmModuleLoader *ml, size_t *size)
{
    if (size)
        *size = ml ? sizeof *ml : 0;
    return ml;
}

const EmTask *em_module_loader_record(const EmModuleLoader *ml)
{
    return ml ? ml->record : NULL;
}

int em_module_loader_orphaned(EmStatusSceneFault *out)
{
    if (s_orphan.code == EM_STATUS_SCENE_FAULT_NONE)
        return 0;
    if (out)
        *out = s_orphan;
    return 1;
}

const EmModuleLoaderSdk *em_module_loader_sdk(const EmModuleLoader *ml)
{
    return ml ? &ml->sdk : NULL;
}

/* ---- the task ---- */

int em_module_loader_request_001FF080(EmModuleLoader *ml, uint8_t state, uint8_t module)
{
    if (!ml || ml != s_live)
        return -1;
    EmTask *rec = em_task_register(EM_MODULE_LOADER_TASK_SLOT, em_module_loader_task_001FF0D0);
    if (!rec)
        return -1;
    /* em_task_register is 001AB740: state 1, +8..+0x17 cleared. */
    rec->user[0] = state;  /* D_0028A798 */
    rec->user[6] = module; /* D_0028A79E */
    ml->record = rec;
    return 0;
}

static uint8_t view_in(const uint8_t *view, uint8_t local)
{
    return view ? *view : local;
}

static uint8_t gate(const EmModuleLoader *ml)
{
    return ml->views.r_00282157 ? ml->views.r_00282157(ml->views.r_00282157_ctx) : ml->local_gate;
}

int em_module_loader_dispatch(EmModuleLoader *ml, EmTask *record)
{
    if (!ml)
        return -1;
    if (ml->fault.code != EM_STATUS_SCENE_FAULT_NONE ||
        ml->io_fault.code != EM_STATUS_SCENE_FAULT_NONE)
        return -1;
    if (!record)
        return fail(&ml->fault, 0x0028A790u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
    ml->record = record;
    EmStatusSceneLoader *ld = &ml->ld;
    ld->d275BD8 = view_in(ml->views.d275BD8, ml->local_bd8);
    ld->d282157 = gate(ml);
    ld->d810CA4 = view_in(ml->views.d810CA4, ml->local_ca4);
    ld->d810CA6 = view_in(ml->views.d810CA6, ml->local_ca6);
    ld->spad3B90 = view_in(ml->views.spad3B90, ml->local_3B90);
    ++ml->dispatches;
    ml->in_dispatch = 1;
    int r = em_status_scene_loader_001FF0D0(&record->state, record->user, ld, &ml->workers,
                                            &ml->fault);
    ml->in_dispatch = 0;
    if (ml->views.d275BD8)
        *ml->views.d275BD8 = ld->d275BD8;
    else
        ml->local_bd8 = ld->d275BD8;
    return r;
}

void em_module_loader_task_001FF0D0(void)
{
    EmTask *rec = em_task_current();
    if (!s_live) {
        /* The slot runs but its loader was unbound or closed mid-load:
         * fail-stop (em_module_loader_orphaned), never a silent skip. */
        (void)fail(&s_orphan, 0x001FF0D0u, EM_STATUS_SCENE_FAULT_NULL_WORKER);
        return;
    }
    (void)em_module_loader_dispatch(s_live, rec);
}

void em_module_loader_field(EmModuleLoader *ml)
{
    if (ml)
        ++ml->field;
}

int em_module_loader_failed(const EmModuleLoader *ml, EmStatusSceneFault *out)
{
    if (!ml)
        return 1;
    const EmStatusSceneFault *f =
        ml->io_fault.code != EM_STATUS_SCENE_FAULT_NONE ? &ml->io_fault : &ml->fault;
    if (f->code == EM_STATUS_SCENE_FAULT_NONE)
        return 0;
    if (out)
        *out = *f;
    return 1;
}

void em_module_loader_counts(const EmModuleLoader *ml, uint32_t *dispatches, uint32_t *reads,
                             uint32_t *unmeasured)
{
    if (dispatches)
        *dispatches = ml ? ml->dispatches : 0;
    if (reads)
        *reads = ml ? ml->reads : 0;
    if (unmeasured)
        *unmeasured = ml ? ml->unmeasured : 0;
}

void em_module_loader_snapshot(const EmModuleLoader *ml, const EmTask *record,
                               uint8_t out[EM_MODULE_LOADER_SNAPSHOT_SIZE])
{
    memset(out, 0, EM_MODULE_LOADER_SNAPSHOT_SIZE);
    if (!ml)
        return;
    const EmTask *rec = record ? record : ml->record;
    const EmStatusSceneLoader *ld = &ml->ld;
    uint8_t *p = out;
    if (rec) {
        p[0] = rec->state;
        memcpy(p + 1, rec->user, 24);
    }
    p += 25;
    *p++ = ml->in_dispatch ? ld->d275BD8 : view_in(ml->views.d275BD8, ml->local_bd8);
    *p++ = ml->in_dispatch ? ld->d282157 : gate(ml);
    u32_put(p, ld->d275C70);
    u32_put(p + 4, ld->d275C74);
    p += 8;
    for (int i = 0; i < EM_STATUS_SCENE_RELOC_WORDS; ++i, p += 4)
        u32_put(p, ld->d28A490[i]);
    memcpy(p, ld->header, sizeof ld->header);
}

EmStatusSceneLoader *em_module_loader_state(EmModuleLoader *ml)
{
    return ml ? &ml->ld : NULL;
}

const uint8_t *em_module_loader_memory_rest(const EmModuleLoader *ml, uint32_t address, uint32_t *size)
{
    if (!ml || !size)
        return NULL;
    for (int i = 0; i < MAX_REGIONS; ++i) {
        const Region *r = &ml->regions[i];
        if (r->bytes && address >= r->address && address < (uint64_t)r->address + r->size) {
            *size = (uint32_t)(r->address + r->size - address);
            return r->bytes + (address - r->address);
        }
    }
    return NULL;
}

const uint8_t *em_module_loader_memory(const EmModuleLoader *ml, uint32_t address, uint32_t size)
{
    if (!ml)
        return NULL;
    if (address >= EM_STATUS_SCENE_D_00289BC0 &&
        (uint64_t)address + size <= (uint64_t)EM_STATUS_SCENE_D_00289BC0 + sizeof ml->ld.header)
        return ml->ld.header + (address - EM_STATUS_SCENE_D_00289BC0);
    for (int i = 0; i < MAX_REGIONS; ++i) {
        const Region *r = &ml->regions[i];
        if (r->bytes && address >= r->address &&
            (uint64_t)address + size <= (uint64_t)r->address + r->size)
            return r->bytes + (address - r->address);
    }
    return NULL;
}
