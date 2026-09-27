/* em_module_loader.h — the screen-module loader's disc/DMA layer and its
 * live frame-task slot-2 binding (docs/MODULE_LOADER.md).
 *
 * The loader's own state machine is the verified translation in
 * em_status_scene_original.c (001FF080 request, 001FF0D0 slot-2 task,
 * 001FF830 bank streamer, 001FF3F0 chunk streamer; STATUS_SCENE.md
 * section 2). This module adds what those call and binds them live:
 *
 *   00200780  start a disc read (sector arithmetic, ready/read retry loop)
 *   00200730  poll the read (busy / done / error)
 *   00200830  send one DMA chain on channel 1 (VIF1): wait, send, wait
 *   00200890  pick the player's texture packet by D_00810707 / D_00810C60
 *   00200970  the player's texture restore (00200830 + 001CCB10 / 00200890)
 *
 * and the SDK leaves they reach (libcdvd RPC 00113280 / 00112440 /
 * 00112D18 / 00113680, DMA 00101BB8 / 00102468 / 00101F08) as a host
 * "drive": every read is answered at host speed from the user's exported
 * disc sectors (tools/export_module_loader.py), so the loader's steps all
 * run and only the drive's time goes (port CLAUDE.md, 2026-09-27). The
 * measured PCSX2 drive time of the module-0x21 load is an optional switch
 * (EM_MODULE_LOADER_DRIVE_MEASURED), off by default.
 *
 * Game logic stays separable from the platform: the translations take an
 * EmModuleLoaderSdk, the host drive is one implementation of it.
 */
#ifndef EM_MODULE_LOADER_H
#define EM_MODULE_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_status_scene_original.h"
#include "game/em_task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Original addresses. */
#define EM_MODULE_LOADER_D_0028A480 0x0028A480u /* INDEX.IDX descriptor {lsn, size} */
#define EM_MODULE_LOADER_D_0028A488 0x0028A488u /* DATA.DAT descriptor {lsn, size} */
#define EM_MODULE_LOADER_TASK_SLOT 2            /* 001FF080 registers slot 2 */
#define EM_MODULE_LOADER_DMA_VIF1 0x10009000u   /* D_00241050[1], 00101BB8(1) */

/* ---------------------------------------------------------------------------
 * The SDK leaves, by original address. Each returns >= 0, or < 0 on a port
 * failure (the caller faults with EM_STATUS_SCENE_FAULT_WORKER_FAILED at
 * the leaf's address). */
typedef struct {
    void *ctx;
    /* 00113280(mode): the drive-ready RPC; 00200780 ignores its reply. */
    int (*ready_00113280)(void *ctx, int32_t mode, int32_t *reply);
    /* 00112440(lsn, sectors, buf, mode): queue a read; `accepted` 0 means
     * "not queued" (00200780 then retries). mode = the three bytes
     * trycount, spindlctrl, datapattern. */
    int (*read_00112440)(void *ctx, uint32_t lsn, uint32_t sectors, uint32_t buf,
                         const uint8_t mode[3], int32_t *accepted);
    /* 00112D18(mode): nonzero `busy` while the read is in progress. */
    int (*sync_00112D18)(void *ctx, int32_t mode, int32_t *busy);
    /* 00113680(): the drive's error code (0 = none). */
    int (*error_00113680)(void *ctx, int32_t *error);
    /* 00101BB8(channel): the DMAC channel's register base. */
    int (*channel_00101BB8)(void *ctx, int32_t channel, uint32_t *base);
    /* 00102468(base, 0, 0): wait for the channel. */
    int (*wait_00102468)(void *ctx, uint32_t base, int32_t mode, int32_t timeout);
    /* 00101F08(base, chain): start a source-chain transfer at `chain`. */
    int (*send_00101F08)(void *ctx, uint32_t base, uint32_t chain);
    /* 001CCB10(): a GS texture upload (00200970 only). */
    int (*upload_001CCB10)(void *ctx);
} EmModuleLoaderSdk;

/* ---- the translations (each: 0, or -1 with *fault set) ---- */

/* 00200780(file, buf, offset, size): file = the descriptor's words {lsn,
 * size} (D_0028A480 or D_0028A488 in the loader). sectors =
 * (size + 0x7FF) >> 11 for size >= 0 (arithmetic), else the descriptor size
 * rounded up (logical shift); lsn = file.lsn + (offset >> 11). Calls
 * 00113280(0), then 00112440(lsn, sectors, buf, {0, 1, 0}); while that is
 * not accepted, again 00113280(0) and the read. *bytes = sectors << 11. */
int em_module_loader_read_00200780(const EmModuleLoaderSdk *sdk, const uint32_t file[2],
                                   uint32_t buf, int32_t offset, int32_t size, int32_t *bytes,
                                   EmStatusSceneFault *fault);
/* 00200730(): 0 while 00112D18(1) reports busy, else 1 when 00113680()
 * reports no error and 2 otherwise. */
int em_module_loader_poll_00200730(const EmModuleLoaderSdk *sdk, int32_t *status,
                                   EmStatusSceneFault *fault);
/* 00200830(chain): base = 00101BB8(1); 00102468(base, 0, 0);
 * 00101F08(base, chain); 00102468(base, 0, 0). */
int em_module_loader_dma_00200830(const EmModuleLoaderSdk *sdk, uint32_t chain,
                                  EmStatusSceneFault *fault);
/* 00200890(): 00200830 of one of the five packet words D_0028A4B0..C0
 * (d28A4B0[0..4]) chosen by D_00810707 and D_00810C60. *chain (optional)
 * receives the word sent. */
int em_module_loader_packet_00200890(const EmModuleLoaderSdk *sdk, uint8_t d810707,
                                     uint8_t d810C60, const uint32_t d28A4B0[5],
                                     uint32_t *chain, EmStatusSceneFault *fault);
/* 00200970(a0): 00200830(D_0028A564); then a0 == 0: 001CCB10() and, when
 * spad 0x70003B90 (read after 001CCB10 returns) == 2, 00200890();
 * a0 != 0: 00200890(). `spad3B90` points at the port's storage of that
 * byte (NULL faults on the a0 == 0 path). */
int em_module_loader_restore_00200970(const EmModuleLoaderSdk *sdk, int32_t a0,
                                      uint32_t d28A564, const uint8_t *spad3B90, uint8_t d810707,
                                      uint8_t d810C60, const uint32_t d28A4B0[5],
                                      EmStatusSceneFault *fault);

/* ---------------------------------------------------------------------------
 * The live binding. */

typedef struct EmModuleLoader EmModuleLoader;

enum {
    EM_MODULE_LOADER_DRIVE_HOST = 0,     /* default: every read completes at once */
    EM_MODULE_LOADER_DRIVE_MEASURED = 1  /* the PCSX2-measured busy fields (option) */
};

/* Canonical storage the binder points the loader at (NULL: a private byte
 * initialised to 0). Read before and written back (BD8) after every
 * dispatch. D_00282157 is read through a reader of this signature;
 * em_stream_live_read_phase() is uint8_t(void), so the binder passes a
 * one-line wrapper (em_scene_bindings.c has one, its static r_00282157).
 * The port holds D_00275BD8 in more than one place today
 * (docs/MODULE_LOADER.md section 4 item 3): `d275BD8` must point at the
 * one byte they are unified into. */
typedef struct {
    uint8_t *d275BD8;                    /* the unified D_00275BD8 (docs section 4) */
    uint8_t (*r_00282157)(void *ctx);    /* the stream lanes' read phase (the gate) */
    void *r_00282157_ctx;
    const uint8_t *d810CA4;              /* 001FEF70 inputs (area chaining only) */
    const uint8_t *d810CA6;
    const uint8_t *spad3B90;             /* modules 0x2A/0x2B buffer choice */
} EmModuleLoaderViews;

/* Test / log hook: called at every SDK leaf entry with its original
 * address and arguments, before the leaf acts. */
typedef void (*EmModuleLoaderTrace)(void *ctx, uint32_t callee, uint32_t a0, uint32_t a1,
                                    uint32_t a2, uint32_t a3);

/* The DMA consumer (00101F08 on channel 1): `bytes` are the host bytes the
 * drive delivered at `chain` through the end of that read's region
 * (`size`). Returns >= 0, or < 0 to fault. Unset: the send faults. */
typedef int (*EmModuleLoaderChainHook)(void *ctx, uint32_t chain, const uint8_t *bytes,
                                       uint32_t size);

/* Loads the EMML pack (tools/export_module_loader.py): the disc sectors,
 * the two file descriptors and the loader globals' captured start values.
 * NULL on a missing or malformed pack. */
EmModuleLoader *em_module_loader_open(const char *pack_path);
void em_module_loader_close(EmModuleLoader *ml);

void em_module_loader_set_views(EmModuleLoader *ml, const EmModuleLoaderViews *views);
void em_module_loader_set_chain_hook(EmModuleLoader *ml, EmModuleLoaderChainHook hook, void *ctx);
void em_module_loader_set_trace(EmModuleLoader *ml, EmModuleLoaderTrace trace, void *ctx);
/* EM_MODULE_LOADER_DRIVE_HOST (default) or _MEASURED. 0, or -1. */
int em_module_loader_set_drive(EmModuleLoader *ml, int mode);

/* The loader record the live task uses: em_module_loader_task_001FF0D0
 * dispatches through this instance (NULL unbinds). Binding a non-NULL
 * instance clears the orphan fault. */
void em_module_loader_bind_live(EmModuleLoader *ml);

/* 1 with *fault = {0x001FF0D0, NULL_WORKER} once the slot-2 task ran while
 * no loader was bound (unbound or closed mid-load), else 0. The host checks
 * it with em_module_loader_failed every frame. */
int em_module_loader_orphaned(EmStatusSceneFault *fault);

/* The host drive as an EmModuleLoaderSdk (for 00200890 / 00200970 callers
 * and tests); valid while `ml` is open. */
const EmModuleLoaderSdk *em_module_loader_sdk(const EmModuleLoader *ml);

/* 001FF080(state, module): em_task_register(2, em_module_loader_task_001FF0D0)
 * (001AB740: slot state 1, +8..+0x17 cleared), then +8 = state and
 * +0xE = module. The dispatcher's pass that follows runs the first step.
 * 0, or -1 (no live binding / slot refused). */
int em_module_loader_request_001FF080(EmModuleLoader *ml, uint8_t state, uint8_t module);

/* The slot-2 task (EmTaskFn): one 001FF0D0 dispatch over the running
 * record through the live instance. A fault latches (em_module_loader_failed). */
void em_module_loader_task_001FF0D0(void);

/* One 001FF0D0 over an explicit record (what the task calls). 0, or -1. */
int em_module_loader_dispatch(EmModuleLoader *ml, EmTask *record);

/* The drive's field clock: call once per frame, before em_task_dispatch.
 * Only the measured drive reads it. */
void em_module_loader_field(EmModuleLoader *ml);

/* 1 with *fault filled once a fault latched, else 0. A latched fault
 * stops every later dispatch (slot 2 stays running, D_00275BD8 stays 1):
 * the host must check this every frame and fail itself with the address
 * and code, or the stop shows only as a hung ITEM root. */
int em_module_loader_failed(const EmModuleLoader *ml, EmStatusSceneFault *fault);

/* Counters: dispatches run, reads queued, reads the measured drive had no
 * measurement for (answered at host speed). */
void em_module_loader_counts(const EmModuleLoader *ml, uint32_t *dispatches, uint32_t *reads,
                             uint32_t *unmeasured);

/* The modelled original bytes, in this order: slot record +0, +8..+0x1F
 * (24), D_00275BD8, D_00282157, then little-endian words D_00275C70,
 * D_00275C74, D_0028A5A0, D_0028A738, D_0028A73C, D_0028A744, D_0028A748,
 * the relocation words D_0028A490[0..67], and the header D_00289BC0
 * (0x800). `record` NULL: the last request's record. */
#define EM_MODULE_LOADER_SNAPSHOT_SIZE (1u + 24u + 2u + 7u * 4u + 68u * 4u + 0x800u)
void em_module_loader_snapshot(const EmModuleLoader *ml, const EmTask *record,
                               uint8_t out[EM_MODULE_LOADER_SNAPSHOT_SIZE]);

/* The loader globals (D_00282157 .. D_00289BC0 as EmStatusSceneLoader).
 * The pack seeds the cursors; the relocation words start at 0 (nothing in
 * the port reads them). Binders and tests may seed or inspect them; the
 * view bytes are refreshed from the views at every dispatch. */
EmStatusSceneLoader *em_module_loader_state(EmModuleLoader *ml);

/* The host bytes the drive delivered at original `address` (`size` bytes
 * inside one read's region), or NULL. */
const uint8_t *em_module_loader_memory(const EmModuleLoader *ml, uint32_t address, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* EM_MODULE_LOADER_H */
