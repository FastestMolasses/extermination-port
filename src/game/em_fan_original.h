/* AREA11 fan pair: overlay behaviour 0x00827630 (placement records 1 and 2,
 * WP-11, FIRST_LEVEL_AUDIT H20/H21). Docs: docs/FAN_ORIGINAL.md.
 *
 * Hand translation of the AREA11 overlay function at runtime 0x827630
 * (overlay file offset 0x4130; the splat listing names it
 * func_overlay_AREA11_008275F0 because its vram base is 0x40 low). Verified
 * by tools/test_fan_original_reference.py, which executes the original
 * overlay instructions (plus 001B0FD0/001B0EA0/001B1470 from the ELF) and
 * compares every written actor, player and global byte and every call.
 *
 * Only original record bytes the behaviour reads or writes are modelled;
 * each field carries its original offset. Everything the behaviour reaches
 * outside those bytes is an explicit worker named by original address. A
 * reached NULL worker, a NULL player/globals view when the original would
 * read it, or a negative worker result latches a fault (fail-stop) and the
 * tick returns -1; a latched fault makes every later tick return -1.
 *
 * stdint only; no dependency on any port subsystem.
 */
#ifndef EM_FAN_ORIGINAL_H
#define EM_FAN_ORIGINAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_FAN_ORIGINAL_CALLBACK 0x00827630u /* placement record +0x24 -> actor +0x10 */
#define EM_FAN_ORIGINAL_D_008102B0 0x008102B0u /* player actor the box test reads */
#define EM_FAN_ORIGINAL_D_008106B8 0x008106B8u
#define EM_FAN_ORIGINAL_D_00810758 0x00810758u
#define EM_FAN_ORIGINAL_D_00810788 0x00810788u
#define EM_FAN_ORIGINAL_D_008107D8 0x008107D8u
#define EM_FAN_ORIGINAL_SOUND_CUE 0x451       /* 001FBD50 a1 */
#define EM_FAN_ORIGINAL_EXIT_A0 1             /* 001B0C60(1, 1, 4) */
#define EM_FAN_ORIGINAL_EXIT_A1 1
#define EM_FAN_ORIGINAL_EXIT_A2 4

/* Fault codes (numerically the EM_SCENE_FAULT_* codes of em_scene_state.h,
 * so a coordinator adapter can copy the record into its own latch). */
enum {
    EM_FAN_FAULT_NONE = 0,
    EM_FAN_FAULT_NULL_WORKER = 1,   /* reached worker or data view is NULL */
    EM_FAN_FAULT_WORKER_FAILED = 2, /* worker returned a negative value */
    EM_FAN_FAULT_BAD_RESULT = 3,    /* 001B0FD0 result outside {0, 1} */
    EM_FAN_FAULT_BAD_INDEX = 4      /* ticked after the 001AFC10 free */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_FAN_FAULT_* */
} EmFanOriginalFault;

/* The actor record bytes 0x827630 reads or writes. */
typedef struct {
    uint8_t lifecycle; /* +0x04: 0 init, 1 run, 2/3 free, other: nothing */
    uint8_t phase;     /* +0x05: spin cycle step 0..4 (>= 5: tail only) */
    int16_t timer;     /* +0x28: lh/sh countdown */
    uint16_t flags2;   /* +0x2E: lhu; 001B6990 copies placement byte +3 */
    float spin;        /* +0x38: per-tick rot.z increment */
    float rot_z;       /* +0xC8: rot.z, the Z leg of 001C6380's TRS */
    uint8_t freed;     /* native: 1 after the 001AFC10 worker ran */
} EmFanOriginal;

/* Player actor D_008102B0 bytes the behaviour reads (+0x00 and +0xA0..A8)
 * and writes (the hit: +0x00, +0x0F, +0x70..+0x7C, +0x224). Named by
 * offset only: this lane did not verify what the consumers make of them. */
typedef struct {
    uint8_t b00;     /* +0x00: fast arm requires == 1; hit writes 3 */
    uint8_t b0F;     /* +0x0F: hit writes 6 */
    float f70[4];    /* +0x70..+0x7C: hit writes 0, 0, 1.0, 1.0 */
    float pos[3];    /* +0xA0/+0xA4/+0xA8: X, Y, Z */
    float f224;      /* +0x224: hit writes 5.0 */
} EmFanOriginalPlayer;

/* Global bytes the behaviour reads, and D_008107D8 it read-modify-writes. */
typedef struct {
    uint8_t d810788; /* record with flags2 0 skips sound 0x451 when == 1 */
    uint8_t d8106B8; /* area-change request byte B8: != 0 skips the box */
    uint8_t d810758; /* == 0xFF selects 001B0C60(1,1,4), else 7D8 |= 0x80 */
    uint8_t d8107D8; /* |= 0x80 when 758 != 0xFF */
} EmFanOriginalGlobals;

/* Workers, one per original callee. `ctx` identifies the actor to the
 * adapter. All return >= 0 on success, < 0 on a port failure (fault). */
typedef struct {
    void *ctx;
    /* 001B0FD0(actor) minus its +0x04 byte writes, which this module owns:
     * the model/bone init of 001B0EA0 and, when that returns 0,
     * bone_init_default_1 (001C62C0). Returns 001B0EA0's result: 0
     * (module: +0x04 += 1) or 1 (bone cap exceeded; module: +0x04 = 3, the
     * 001B0EA0 store). */
    int (*w_001B0FD0)(void *ctx, const EmFanOriginal *fan);
    /* 001FBD50(actor, cue, a2, f12). The original result is ignored; the
     * adapter maps the original "no channel" -1 to 0. */
    int (*w_001FBD50)(void *ctx, int32_t cue, int32_t a2, float f12);
    /* 001C6380(actor): TRS world matrix from +0xB0/+0xC0/+0x60 into +0xD0
     * and every bone slot; called after rot_z is stored. */
    int (*w_001C6380)(void *ctx, const EmFanOriginal *fan);
    /* 001B17A0(actor): publication; its byte result is ignored here. */
    int (*w_001B17A0)(void *ctx);
    /* 001B0C60(a0, a1, a2): the area-change request (3B8D = 3, 001B0C00(4),
     * B5 = a0, B6 = a1, B7 = a2, B8 = 1). The coordinator owns it. */
    int (*w_001B0C60)(void *ctx, int32_t a0, int32_t a1, int32_t a2);
    /* jalr *(actor + 0x4C)(actor): the actor's draw callback. */
    int (*w_draw_4C)(void *ctx);
    /* 001AFC10(actor): pool free. */
    int (*w_001AFC10)(void *ctx);
} EmFanOriginalWorkers;

/* Record state right after 001B6990 spawned it: +0x04/+0x05 are 0 (the pool
 * clears them), +0x2E = placement byte +3, +0xC8 = placement rot.z (+0x20).
 * +0x28 and +0x38 are written before they are read. */
void em_fan_original_spawn(EmFanOriginal *fan, uint16_t flags2, float placement_rot_z);

/* One call of 0x827630. `player` is read only when flags2 == 1 and B8 == 0
 * (and written on a hit); `globals` only in lifecycle 1. Returns 1 while
 * allocated, 0 after the 001AFC10 free, -1 on a fault (latched in *fault;
 * a fault already latched there makes the tick do nothing). */
int em_fan_original_tick(EmFanOriginal *fan, EmFanOriginalPlayer *player,
                         EmFanOriginalGlobals *globals, const EmFanOriginalWorkers *w,
                         EmFanOriginalFault *fault);

/* 001B1470 on the EE add/sub model: wrap to (-pi, pi]. Exposed for tests. */
float em_fan_original_wrap_001B1470(float angle);

#ifdef __cplusplus
}
#endif

#endif /* EM_FAN_ORIGINAL_H */
