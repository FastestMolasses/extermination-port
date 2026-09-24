/* IOP stream backend: the IOP side the stream lanes (em_stream_lanes_original,
 * docs/STREAM_LANES.md) talk to through 001157F0 and 0011A730.
 * Docs: docs/IOP_STREAM.md.
 *
 * What it holds, and where each part comes from:
 *
 *  1. EE side (translations of original EE code):
 *     001FA6A0 IOP heap block (align 16 over 0010F8F8(size + 0x10)) and the
 *              five boot allocations of sub_cdrom0_IRX_SNDN2DRV_IRX_1
 *              (D_00275B50, D_00275B28 = D_00275B4C, D_00275B24, D_00275B20);
 *     0011A2B0 the SDK voice allocator over D_0027CCC0 (from its .s; the
 *              NEARMISS C passes 0 where the .s passes the scan end index,
 *              see the doc);
 *     sub_O_STREAM_MUSIC_DAT_1 (D_00282188 / D_0028218C);
 *     001157F0 the EE sound command queue (255 entries per exchange).
 *  2. The stream part of the IOP sound driver SNDN2DRV.IRX, translated from
 *     the module the user's disc carries (its code is read locally, never
 *     reproduced): the RPC 0x64 command copy, the per-tick command drain and
 *     status snapshot, commands 0x16, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41,
 *     0x42, 0x43, the SPU play-address scan, the transfer queue push and its
 *     consumer (deinterleaving copy, ADPCM loop flags, SPU transfer, cursor
 *     advance). Driver .bss offsets are named on each field.
 *  3. Hardware models (stated, not translated): the libsd calls the driver
 *     makes are applied to an SPU2 voice model (ADPCM decode, loop flags,
 *     pitch counter, the port's SPU2 ADSR model em_sfx_envelope_*, volume
 *     words), the IOP hard timer (a driver tick every 64 H-lines), the NTSC
 *     field (262.5 H-lines), the SPU2 clock (48000 Hz) and transfer
 *     completion (within one driver tick).
 *  4. A sector reader standing in for the libcdvd calls 00113280 / 00112610 /
 *     00112D18 / 00113478 / 00111C28 over the locally exported EMST file
 *     (tools/export_streams.py); it never reads the disc at run time.
 *  5. The mixer interface: the SPU output is rendered on the game thread,
 *     field by field, into a lock-free ring the audio thread drains
 *     (em_iop_stream_mix, summed like em_sfx_mix).
 *
 * Fail-stop: an original index outside the modelled storage, a sector the
 * export does not hold, a missing worker or a command outside the stream set
 * with no forward sink latches a fault; every entry point then returns -1.
 *
 * Threading: everything except em_iop_stream_mix runs on the game thread.
 * em_iop_stream_mix may run on the audio thread (no locks, no allocation).
 */
#ifndef EM_IOP_STREAM_H
#define EM_IOP_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_sfx_bank.h"
#include "game/em_stream_lanes_original.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_IOP_RAM_SIZE = 0x200000,   /* IOP main RAM (2 MB)                     */
    EM_SPU_RAM_SIZE = 0x200000,   /* SPU2 RAM (2 MB)                         */
    EM_IOP_VOICES = 48,           /* 2 cores x 24 voices                     */
    EM_IOP_QUEUE = 0x60,          /* transfer queue entries (.bss 0x8080)    */
    EM_IOP_RING = 0x100,          /* command ring entries (.bss 0x66C0)      */
    EM_IOP_STAGING = 0x2000,      /* transfer staging bytes (.bss 0x46B0)    */
    EM_IOP_STATUS_WORDS = 0x80,   /* status block (.bss 0x44B0, 0x200 bytes) */
    EM_EE_QUEUE = 255,            /* 001157F0 limit per exchange             */
    EM_IOP_PCM_FRAMES = 16384     /* mixer ring (stereo frames)              */
};

/* Fault codes: 1, 2 and 4 are numerically the EM_STREAM_FAULT_* codes. */
enum {
    EM_IOP_FAULT_NONE = 0,
    EM_IOP_FAULT_NULL_WORKER = 1,  /* reached worker / view is NULL           */
    EM_IOP_FAULT_WORKER_FAILED = 2,
    EM_IOP_FAULT_BAD_INDEX = 4,    /* voice / address outside the storage     */
    EM_IOP_FAULT_NOT_EXPORTED = 8, /* sector outside the exported extents     */
    EM_IOP_FAULT_UNSUPPORTED = 16, /* command outside the stream set, no sink */
    EM_IOP_FAULT_DIVIDE = 32       /* the driver would trap (divide by zero)  */
};

typedef struct {
    uint32_t address; /* original function address (EE, or driver .text offset | 0x1D000000) */
    int32_t code;
} EmIopStreamFault;

/* Driver .text offsets, tagged so a fault address cannot be read as EE. */
#define EM_IOP_DRV(offset) (0x1D000000u | (uint32_t)(offset))

/* One stream voice record, driver .bss 0x76C0 + voice * 0x34. */
typedef struct {
    uint32_t active;    /* +0x00 bit 0: set by the key-on entry, cleared by key-off  */
    uint32_t stride;    /* +0x04 0x3E flags & 0xFF0000: 0x10000 mono, 0x20000 L/R    */
    uint32_t spu_addr;  /* +0x08 SPU2 buffer (two halves)                            */
    uint32_t spu_size;  /* +0x0C 0x3E word & 0xFF00: both halves (0x4000)            */
    uint32_t iop_addr;  /* +0x10 IOP buffer (this voice's first byte)                */
    uint32_t iop_size;  /* +0x14 IOP buffer size (the cursor wraps here)             */
    uint32_t vol_left;  /* +0x18 command 0x40                                        */
    uint32_t vol_right; /* +0x1C                                                     */
    uint32_t rate;      /* +0x20 command 0x41 (Hz)                                   */
    uint32_t nax;       /* +0x24 last play address the scan read                     */
    uint32_t cursor;    /* +0x28 IOP-buffer offset of the next transfer: the status
                         *       word 0011A730 returns                               */
    uint32_t half;      /* +0x2C 0x1000000 play in the first SPU half, 0x2000000 second */
    uint32_t sent;      /* +0x30 the half a transfer was last queued for             */
} EmIopStreamVoice;

/* The driver's stream state (every byte the translated functions touch);
 * offsets are from the module's load address (.bss starts at +0x34B0). */
typedef struct {
    EmIopStreamVoice voice[EM_IOP_VOICES]; /* .bss 0x76C0                         */
    uint32_t queue[EM_IOP_QUEUE][4];       /* .bss 0x8080 transfer queue          */
    int32_t queue_write;                   /* module +0x3494 (.data)              */
    int32_t queue_read;                    /* module +0x3498 (.data)              */
    uint32_t pending;                      /* module +0x34A8 (.data): voice | 0x10000000 */
    uint32_t ring[EM_IOP_RING][4];         /* .bss 0x66C0 command ring (0x1000 B) */
    uint32_t ring_write;                   /* module +0x34A0 (.data) bytes written */
    uint32_t ring_read;                    /* module +0x34A4 (.data) bytes drained */
    /* .bss 0x44B0, the RPC 0x64 reply: [0..47] ENVX & 0x7FFF, [48..95] cursor
     * words (the EE's D_00281880), [96..111] the +0xC words of .bss 0x8680's
     * sixteen 0x20-byte records; [112..127] are not written by this subset. */
    uint32_t status[EM_IOP_STATUS_WORDS];
    uint8_t staging[EM_IOP_STAGING];       /* .bss 0x46B0                         */
    uint32_t records[0x80];                /* .bss 0x8680 (0x200 bytes, 0x3C clears) */
} EmIopDriver;

/* The libsd calls the driver makes (import ordinals of the module).
 * Default: the SPU2 model of this module. Tests may substitute recorders. */
typedef struct {
    void *ctx;
    void (*set_param)(void *ctx, uint16_t reg, uint16_t value);      /* libsd 5  */
    uint16_t (*get_param)(void *ctx, uint16_t reg);                   /* libsd 6  */
    void (*set_switch)(void *ctx, uint16_t reg, uint32_t value);      /* libsd 7  */
    void (*set_addr)(void *ctx, uint16_t reg, uint32_t value);        /* libsd 9  */
    uint32_t (*get_addr)(void *ctx, uint16_t reg);                    /* libsd 10 */
    /* libsd 17 (channel, mode, IOP source, SPU address, size): data is the
     * staging bytes the driver passes. */
    void (*voice_trans)(void *ctx, int16_t channel, uint16_t mode, const uint8_t *data,
                        uint32_t spu_addr, uint32_t size);
    int32_t (*voice_trans_status)(void *ctx, int16_t channel, int16_t flag); /* libsd 19 */
} EmIopLibsd;

/* Commands the stream subset does not handle (the SFX driver's 1/3/5/6/0xA..
 * 0xD, ...) go to this sink at the same point of the driver tick. */
typedef void (*EmIopStreamForward)(void *ctx, const uint32_t command[4]);

/* SPU2 voice model (hardware; stated semantics in docs/IOP_STREAM.md). */
typedef struct {
    uint16_t vol[2], pitch, adsr1, adsr2;
    uint16_t evol_unused;
    uint32_t ssa, nax, lsa;  /* byte addresses; nax = the block being played */
    uint8_t on, flags;       /* keyed; flag byte of the current block        */
    uint16_t pos;            /* sample 0..27 inside the current block        */
    uint32_t counter;        /* 12-bit pitch fraction                        */
    int32_t s1, s2;          /* ADPCM history                                */
    int16_t block[28];
    EmSfxEnvelope env;
} EmIopSpuVoice;

/* The exported EMST file (tools/export_streams.py). */
typedef struct {
    uint32_t lsn, sectors, offset, file;
} EmIopStreamExtent;

typedef struct {
    uint8_t *blob;
    size_t size;
    uint32_t lsn[2], sectors[2];     /* MUSIC.DAT, VOICE.DAT                   */
    char search[2][32];              /* D_0026EBB0, D_0026EBD0 (ELF strings)   */
    char path[2][32];                /* the disc's directory paths             */
    EmStreamClip rows[68 + 179];     /* D_0025DD30 x 68, then D_0025E170 x 179 */
    uint32_t music_rows, voice_rows;
    const EmIopStreamExtent *extent;
    uint32_t extents;
} EmIopStreamDisc;

/* D_0027CCC0 (48 x 0x6A-byte voice records) and D_0027F740 + 0x28, the two
 * SDK stores 0011A2B0 reaches. Owned by the sequencer's binder. */
typedef struct {
    uint8_t *d27CCC0;
    uint64_t *d27F768;
} EmIopVoiceTable;

typedef struct EmIopStream EmIopStream;

/* ---- lifetime ---------------------------------------------------------- */
/* A fresh IOP after the driver's start-up: zeroed driver .bss, zeroed IOP and
 * SPU RAM, heap at the captured first free block, time 0. NULL on failure. */
EmIopStream *em_iop_stream_create(void);
void em_iop_stream_destroy(EmIopStream *s);
const EmIopStreamFault *em_iop_stream_fault(const EmIopStream *s);

/* ---- the exported disc -------------------------------------------------- */
/* 0 on success, -1 (with a message on stderr) on a missing or bad file. */
int em_iop_stream_disc_load(EmIopStreamDisc *disc, const char *path);
void em_iop_stream_disc_free(EmIopStreamDisc *disc);
/* The lanes' clip views over the disc's rows (music_count 68 as the lanes
 * require for the lane-0 cue >= 68 reach). */
void em_iop_stream_lanes_data(const EmIopStreamDisc *disc, EmStreamLanesData *out);
void em_iop_stream_attach_disc(EmIopStream *s, const EmIopStreamDisc *disc);
/* Polls 00112D18 answers "busy" before a read completes (0 = the first
 * poll completes it). A stated model of the drive, default 0. */
void em_iop_stream_set_disc_latency(EmIopStream *s, uint32_t polls);

/* ---- EE side ------------------------------------------------------------ */
uint32_t em_iop_stream_001FA6A0(EmIopStream *s, int32_t size);
/* sub_cdrom0_IRX_SNDN2DRV_IRX_1's tail: out = {D_00275B50, D_00275B28,
 * D_00275B4C, D_00275B24, D_00275B20}. */
int em_iop_stream_boot_buffers(EmIopStream *s, uint32_t out[5]);
/* sub_O_STREAM_MUSIC_DAT_1: D_00282188 / D_0028218C from the disc directory. */
int em_iop_stream_sub_O_STREAM_MUSIC_DAT_1(EmIopStream *s, uint32_t *d282188, uint32_t *d28218C);
/* 0011A2B0(a0) over the bound voice table; *voice = its return. */
int em_iop_stream_0011A2B0(EmIopStream *s, int32_t a0, int32_t *voice);
void em_iop_stream_set_voice_table(EmIopStream *s, const EmIopVoiceTable *table);
/* 001157F0: queue one command for the next exchange. Returns the queued
 * count, or -1 when 255 are already queued (the original drops it). */
int em_iop_stream_001157F0(EmIopStream *s, int32_t cmd, int32_t a1, int32_t a2, int32_t a3);
/* The commands queued for the next exchange (count in *count). */
const uint32_t (*em_iop_stream_ee_queue(const EmIopStream *s, uint32_t *count))[4];
/* Heap model: the next block 0010F8F8 hands out (tests may place it). */
uint32_t em_iop_stream_heap_next(const EmIopStream *s);
void em_iop_stream_set_heap_next(EmIopStream *s, uint32_t next);
/* D_002817C0 as the last exchange delivered it (0x80 words); word 48 + v is
 * D_00281880[v], the value 0011A730(v) returns. */
const int32_t *em_iop_stream_ee_status(const EmIopStream *s);
/* The lanes' IOP-side workers: 001157F0 (queue-full -1 mapped to 0),
 * 00113280, 00112610, 00112D18, 00113478, 0011A2B0. Leaves ctx, 00122BB8,
 * 001FC280 and 001FBC50 (other owners) as the caller set them: the lanes
 * pass one ctx to every worker, so the binder's ctx object must hold the
 * EmIopStream pointer as its FIRST member (the adapters read it there). */
void em_iop_stream_lane_workers(EmIopStream *s, EmStreamLanesWorkers *w);
/* The disc workers as the lanes call them (exposed for tests). */
int em_iop_stream_00113280(EmIopStream *s, int32_t mode, int32_t *result);
int em_iop_stream_00112610(EmIopStream *s, uint32_t sector, uint32_t count, uint32_t addr,
                           const uint8_t mode[3], int32_t *result);
int em_iop_stream_00112D18(EmIopStream *s, int32_t mode, int32_t *result);
int em_iop_stream_00113478(EmIopStream *s, int32_t mode);

/* ---- time --------------------------------------------------------------- */
/* One NTSC field (call where D_00810E90 advances): the RPC 0x64 exchange
 * (queued commands in, the driver's last status snapshot out), then the
 * driver ticks and SPU2 samples that fall inside the field. */
int em_iop_stream_field(EmIopStream *s);

/* ---- driver entry points (the original .text offsets; for the oracle) --- */
int em_iop_stream_drv_rpc(EmIopStream *s, const uint32_t *words, uint32_t bytes); /* 0x12C, fno 0x64 */
int em_iop_stream_drv_command(EmIopStream *s, const uint32_t command[4]);        /* 0x634 subset */
int em_iop_stream_drv_tick_commands(EmIopStream *s);                             /* 0x328 */
int em_iop_stream_drv_scan(EmIopStream *s);                                      /* 0x20C8 */
int em_iop_stream_drv_consume(EmIopStream *s);                                   /* 0x23B8 */
int em_iop_stream_drv_push(EmIopStream *s, uint32_t a0, uint32_t a1, uint32_t a2,
                           uint32_t a3);                                         /* 0x2CF4 */
void em_iop_stream_set_libsd(EmIopStream *s, const EmIopLibsd *sd); /* NULL: the SPU2 model */
void em_iop_stream_set_forward(EmIopStream *s, EmIopStreamForward fn, void *ctx);

/* ---- views (tests, binder) ---------------------------------------------- */
EmIopDriver *em_iop_stream_driver(EmIopStream *s);
uint8_t *em_iop_stream_iop_ram(EmIopStream *s);
uint8_t *em_iop_stream_spu_ram(EmIopStream *s);
EmIopSpuVoice *em_iop_stream_spu_voice(EmIopStream *s, int voice);
/* The EVOL words command 0x16 sets: [core][0 left, 1 right]. */
uint16_t em_iop_stream_effect_volume(const EmIopStream *s, int core, int side);
/* Half-lines elapsed, driver ticks run, SPU2 samples rendered. */
void em_iop_stream_clock(const EmIopStream *s, uint64_t *half_lines, uint64_t *ticks,
                         uint64_t *samples);
/* Test tap: called for every sample a keyed voice outputs, with the decoded
 * ADPCM sample before envelope and volume (NULL clears). Game thread. */
typedef void (*EmIopStreamTap)(void *ctx, int voice, int16_t sample);
void em_iop_stream_set_tap(EmIopStream *s, EmIopStreamTap fn, void *ctx);
/* FNV-1a over the rendered stereo frames (float bits), game thread. */
uint64_t em_iop_stream_pcm_digest(const EmIopStream *s);

/* ---- mixer (audio thread) ----------------------------------------------- */
/* Sum up to `frames` rendered frames into out (interleaved stereo). Only
 * 48000 Hz is supported (em_bgm's device rate); another rate adds nothing
 * and is counted. Missing frames are counted as underruns. */
void em_iop_stream_mix(EmIopStream *s, float *out, int frames, int device_rate);
/* Frames dropped because the ring was full / frames the mixer found missing /
 * mix calls at an unsupported rate. */
void em_iop_stream_mix_counters(const EmIopStream *s, uint64_t *overruns, uint64_t *underruns,
                                uint64_t *bad_rate);

#ifdef __cplusplus
}
#endif

#endif /* EM_IOP_STREAM_H */
