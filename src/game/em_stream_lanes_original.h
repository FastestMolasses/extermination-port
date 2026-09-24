/* Original EE stream lanes: music lane 0 and voice lanes 1/2 (the EE side of
 * the audio streams). Docs: docs/STREAM_LANES.md.
 *
 * Hand translation of the original functions
 *   001F9820 lane initial state at boot (NEARMISS C; read from the .s)
 *   001FA790 lane start (NEARMISS C; read from the .s)
 *   001FABF0 lane fade-in / (re)start (NEARMISS C; read from the .s)
 *   001FAD70 lane fade-out            001FAAC0 lane release
 *   001FAB50 music release            001FAB80 voice lanes release
 *   001FA570 voice ring reset         001FABB0 stop all streams
 *   001FD470 stream stop by mask      001FAE70 music cue select / resume
 *   001FB0B0 set BGM cue + 001FAE70(1)
 *   001F9CF0 per-frame lane service (NEARMISS C; read from the .s; its C has
 *            one inverted min/max, see the doc), which runs
 *   001FA0D0 disc read sequencer (NEARMISS), 001FA5F0 voice ring consumer,
 *   001FA330 volume ramps (NEARMISS)
 * and the SDK leaves they reach: 00119828 / 0011A608 / 0011A6A0 / 0011A6E8
 * and, from 001F9820, 0011A4B8 / 0011A4E8 / 0011A658 (IOP sound command
 * packers over 001157F0), 0011A730 (IOP voice status
 * read), 001281C0 (float -> int) and 00128250 (float -> unsigned).
 * Verified by tools/test_stream_lanes_reference.py, which executes the
 * ORIGINAL instructions of every function above over captured original RAM
 * and compares every modelled byte and every worker call.
 *
 * Only the original bytes these functions read or write are modelled; each
 * field names its original address/offset. Everything reached outside them
 * is an explicit worker named by original address. The IOP side (SPU voices,
 * the disc stream reads, the command queue drain) is a stated boundary.
 *
 * Fail-stop: a reached NULL worker or data view, a negative worker result, or
 * an original index outside the owned storage latches a fault and the call
 * returns -1; once latched, every entry point returns -1 without effect.
 *
 * stdint only; no dependency on any port subsystem.
 */
#ifndef EM_STREAM_LANES_ORIGINAL_H
#define EM_STREAM_LANES_ORIGINAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_STREAM_LANES 3             /* lanes 0 (music), 1, 2 (voice)       */
#define EM_STREAM_RING 16             /* D_00281CF0[16]                       */
#define EM_STREAM_VOICE_STATUS 0x30   /* 0011A730 bound: a0 < 0x30            */

/* IOP command ids 001157F0 receives from the packers. */
#define EM_STREAM_CMD_00119828 0x16   /* 00119828(a0,a1,a2) -> (0x16,a0,a1,a2) */
#define EM_STREAM_CMD_0011A608 0x40   /* 0011A608(mask, a1, a2): (0x40, lo24, hi24, a1<<16|a2) */
#define EM_STREAM_CMD_0011A6A0 0x42   /* 0011A6A0(mask): (0x42, lo24, hi24, 0) */
#define EM_STREAM_CMD_0011A6E8 0x43   /* 0011A6E8(mask): (0x43, lo24, hi24, 0) */
#define EM_STREAM_CMD_0011A4B8 0x3C   /* 0011A4B8(): (0x3C, 0, 0, 0)            */
#define EM_STREAM_CMD_0011A4E8 0x3E   /* 0011A4E8(p[6]): three packed words      */
#define EM_STREAM_CMD_0011A658 0x41   /* 0011A658(mask, a1): (0x41, lo24, hi24, a1) */
/* What the IOP driver does with a command is outside this module and was not
 * read; the doc records only where the EE side sends each one. */

/* Fault codes (numerically the EM_SCENE_FAULT_* codes of em_scene_state.h). */
enum {
    EM_STREAM_FAULT_NONE = 0,
    EM_STREAM_FAULT_NULL_WORKER = 1,   /* reached worker or data view is NULL */
    EM_STREAM_FAULT_WORKER_FAILED = 2, /* worker returned a negative value */
    EM_STREAM_FAULT_BAD_INDEX = 4      /* lane / cue / ring index outside storage */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_STREAM_FAULT_* */
} EmStreamLanesFault;

/* One 0x60-byte lane record at D_00281FD0 + lane * 0x60 (only the bytes the
 * functions above read or write). Floats are kept as raw binary32 bits. */
typedef struct {
    int8_t state;          /* +0x00 D_00281FD0: 001F9CF0 switch 0 / 1 / 2  */
    int8_t half;           /* +0x01 D_00281FD1: 1 / 2 from the 0011A730 test */
    int8_t last_half;      /* +0x02 D_00281FD2: +0x01 when a read completes */
    int8_t load;           /* +0x03 D_00281FD3: 1 = 001FA0D0 may read, 2 = read done */
    int32_t voice;         /* +0x04 D_00281FD4: SPU voice (0011A730 index) */
    uint64_t voice_mask;   /* +0x08 D_00281FD8: ld, 0011A608/6A0/6E8 a0    */
    uint32_t buffer;       /* +0x14 D_00281FE4: first read address         */
    uint32_t buffer_size;  /* +0x18 D_00281FE8: bytes; halved by the tests  */
    int32_t loop;          /* +0x20 D_00281FF0: clip row +0xC; 0 = end by +0x4C timer */
    uint32_t loop_sector;  /* +0x24 D_00281FF4 */
    uint32_t end_sector;   /* +0x28 D_00281FF8 */
    uint32_t base_sector;  /* +0x2C D_00281FFC */
    uint32_t read_sector;  /* +0x30 D_00282000: 00112610 a0 */
    uint32_t read_count;   /* +0x34 D_00282004: 00112610 a1 (sectors) */
    uint32_t read_addr;    /* +0x38 D_00282008: 00112610 a2 */
    int32_t remaining;     /* +0x48: frames left (timed lanes) */
    uint32_t duration;     /* +0x4C D_0028201C: frames (00128250 result) */
    uint32_t start_time;   /* +0x50 D_00282020: D_00810E90 at start / key-on */
    uint32_t fade_step;    /* +0x54 D_00282024: f32 bits, volume per frame */
    uint32_t volume;       /* +0x58 D_00282028: f32 bits, 0 .. cap */
    int8_t release;        /* +0x5C D_0028202C: release when faded to 0 */
} EmStreamLane;

/* Original bytes this module owns (single storage for them). */
typedef struct {
    EmStreamLane lane[EM_STREAM_LANES];
    int32_t voice_right;    /* D_002820F4 (record 3 +0x04): lane 0's second voice */
    int8_t active[EM_STREAM_LANES]; /* D_00282154/55/56 (lb)                 */
    int8_t read_phase;      /* D_00282157: 001FA0D0 phase (lb)               */
    int8_t read_lane;       /* D_00282158: 001FA0D0 lane index (lb)          */
    uint8_t mono;           /* D_0028215B (lbu): lane-0 cap 0x3000 + same volume both voices */
    int32_t cue[EM_STREAM_LANES]; /* D_00282178[3]                            */
    uint32_t music_sector;  /* D_00282188: lane 0 disc base (lw)             */
    uint32_t voice_sector;  /* D_0028218C: lanes 1/2 disc base (lw)          */
    int32_t music_clip;     /* D_00275B2C: last lane-0 cue (gp)              */
    int32_t ring[EM_STREAM_RING]; /* D_00281CF0[16], -1 = empty               */
    int8_t ring_head;       /* D_00275B30: 001FA5A0 push index (gp, lb)      */
    int8_t ring_tail;       /* D_00275B34: 001FA5F0 pop index (gp, lb)       */
} EmStreamLanesState;

/* Bytes owned elsewhere that these functions read or write. The binder keeps
 * them in its own storage and points `globals` at this view. */
typedef struct {
    uint32_t d810E90;       /* frame counter (lw)                        */
    int32_t d8106C8;        /* per-area flags word; 001FAE70 bits 8..15  */
    int32_t d810D38;        /* current BGM cue override (001FB0B0 writes) */
    uint8_t d810700;        /* area byte (lbu)                           */
    uint8_t d8104E4;        /* 001FAE70 override gate (lbu == 1)         */
    uint8_t d8106F4;        /* lane-0 hold byte (001FAB50 clears)        */
    uint8_t d8106F5;        /* voice hold byte (001FAB80 clears)         */
    /* D_00281880 = D_002817C0 + 0xC0: 0x30 words the IOP status reply
     * fills; 0011A730(voice) returns word[voice] (0 when voice >= 0x30). */
    const int32_t *d281880;
    /* Read or written only by 001F9820 and the 0011A4E8 it calls: */
    uint64_t d27F740;       /* SDK sound voice mask (ld/sd; 0011A4E8 flag bits 0/1) */
    uint32_t d275B20;       /* lane 2 buffer (gp; 001FA6A0 result at the IRX bring-up) */
    uint32_t d275B24;       /* lane 1 buffer (gp) */
    uint32_t d275B28;       /* lane 0 buffer (gp) */
} EmStreamLanesGlobals;

/* One 16-byte clip row (D_0025DD30 music, D_0025E170 voice). */
typedef struct {
    int32_t sector; /* +0x0: sector offset from the lane's disc base */
    int32_t unread; /* +0x4: not read by these functions */
    int32_t size;   /* +0x8: bytes */
    int32_t loop;   /* +0xC: -> lane +0x20 */
} EmStreamClip;

/* Lane 0 reads D_0025DD30 + 16 * cue without a bound; with music_count == 68
 * a lane-0 cue >= 68 is voice[cue - 68], as in the original's memory. */
typedef struct {
    const EmStreamClip *music; uint32_t music_count; /* D_0025DD30 */
    const EmStreamClip *voice; uint32_t voice_count; /* D_0025E170 */
} EmStreamLanesData;

/* Workers, one per original callee outside this module. All return >= 0 on
 * success, < 0 on a port failure (fault). */
typedef struct {
    void *ctx;
    /* 001157F0(cmd, a1, a2, a3): the EE-side IOP sound command queue push.
     * The original returns -1 when its 255-entry queue is full; every packer
     * ignores the result, so the adapter maps that -1 to 0. */
    int (*w_001157F0)(void *ctx, int32_t cmd, int32_t a1, int32_t a2, int32_t a3);
    /* 00122BB8(): the game LCG; *value = its return (0 .. 0x7FFFFFFF). */
    int (*w_00122BB8)(void *ctx, int32_t *value);
    /* 001FC280(): room ambience select (it calls 00119828 twice; bind those
     * to em_stream_lanes_00119828). */
    int (*w_001FC280)(void *ctx);
    /* 001FBC50(): SFX stop-all (001FD470 mask bit 0). */
    int (*w_001FBC50)(void *ctx);
    /* Disc stream calls of 001FA0D0 (libcdvd-shaped; labels unverified):
     * 00113280(1) -> *result (== 2 lets the read start),
     * 00112610(sector, count, addr, mode[3] = {0,0,0}) -> *result (!= 0 accepted),
     * 00112D18(1) -> *result (0 = read finished), 00113478(1) cancels. */
    int (*w_00113280)(void *ctx, int32_t a0, int32_t *result);
    int (*w_00112610)(void *ctx, uint32_t sector, uint32_t count, uint32_t addr,
                      const uint8_t mode[3], int32_t *result);
    int (*w_00112D18)(void *ctx, int32_t a0, int32_t *result);
    int (*w_00113478)(void *ctx, int32_t a0);
    /* 0011A2B0(a0): the SDK's voice allocator over the sound voice table
     * D_0027CCC0 (owned outside this module); *voice = its return (a voice
     * index, or -1 when none is free). 001F9820 calls it with a0 = 0. */
    int (*w_0011A2B0)(void *ctx, int32_t a0, int32_t *voice);
} EmStreamLanesWorkers;

typedef struct {
    EmStreamLanesState state;
    const EmStreamLanesData *data;
    EmStreamLanesGlobals *globals;
    EmStreamLanesWorkers workers;
    EmStreamLanesFault fault;
} EmStreamLanes;

/* Binds views and workers, clears the fault. `state` is left as the caller
 * put it (zeroed, or loaded from a captured image). */
void em_stream_lanes_bind(EmStreamLanes *L, const EmStreamLanesData *data,
                          EmStreamLanesGlobals *globals, const EmStreamLanesWorkers *w);

/* Entry points: the original function of the same address. 0 on success,
 * -1 on a fault (latched in L->fault). Arguments are the original's. */
int em_stream_lanes_001F9820(EmStreamLanes *L);                 /* 001AAE40 start-up; 001F9BF0 */
int em_stream_lanes_001F9CF0(EmStreamLanes *L);                 /* per frame (001FB100) */
int em_stream_lanes_001FA0D0(EmStreamLanes *L);
int em_stream_lanes_001FA330(EmStreamLanes *L);
int em_stream_lanes_001FA5F0(EmStreamLanes *L);
int em_stream_lanes_001FA570(EmStreamLanes *L);
int em_stream_lanes_001FA790(EmStreamLanes *L, int32_t lane, int32_t cue);
int em_stream_lanes_001FABF0(EmStreamLanes *L, int32_t lane, int32_t cue, int32_t fade, int32_t enable);
int em_stream_lanes_001FAD70(EmStreamLanes *L, int32_t lane, int32_t fade, int32_t release);
int em_stream_lanes_001FAAC0(EmStreamLanes *L, int32_t lane);
int em_stream_lanes_001FAB50(EmStreamLanes *L);
int em_stream_lanes_001FAB80(EmStreamLanes *L);
int em_stream_lanes_001FABB0(EmStreamLanes *L);
int em_stream_lanes_001FD470(EmStreamLanes *L, int32_t mask);
int em_stream_lanes_001FAE70(EmStreamLanes *L, int32_t a0);
int em_stream_lanes_001FB0B0(EmStreamLanes *L, int32_t cue);
int em_stream_lanes_00119828(EmStreamLanes *L, int32_t a0, int32_t a1, int32_t a2);

/* 0011A4E8(p) (p = the six-word block), exposed for the reference test:
 * its D_0027F740 update and its 001157F0(0x3E, ...) command. */
int em_stream_lanes_0011A4E8(EmStreamLanes *L, const int32_t p[6]);

/* Pure leaves, exposed for the reference test. */
int32_t em_stream_lanes_001281C0(uint32_t f32_bits);  /* float_to_int */
uint32_t em_stream_lanes_00128250(uint32_t f32_bits); /* float -> unsigned */

#ifdef __cplusplus
}
#endif

#endif /* EM_STREAM_LANES_ORIGINAL_H */
