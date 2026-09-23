/* Head sprite effect: effect-table entry 0x10 (pool node callback 001E2560)
 * and its spawner 001F0120. Docs: docs/HEAD_SPRITE_ORIGINAL.md.
 *
 * Hand translation of the byte-matched decomp C (and the matching .s) of:
 *   001F0120  spawner: 001E2290 gate, 001EF9D0(0x80000010, 0, 1.0) allocation,
 *             then record +0x0D = key, +0x24 = owner +0x14;
 *   001E2290  key gate (20 key values);
 *   001E2560  the node behaviour (lifecycle +0x04, sub-state +0x05);
 *   001E23A0  key -> D_002535F0 entry (bone index +0x28, offset +0xA0..+0xAC);
 *   001B0070  returns D_008106C8;
 *   001CFA60  builds the 0x58-byte transform block (sp+0x40 in 001E2560);
 *   001CFBE0  emits the four-packet GIF chain for one effect instance.
 * Verified by tools/test_head_sprite_reference.py, which executes the
 * original instructions of all seven from the user's pinned ELF and compares
 * every written record byte, every emitted packet byte and every call.
 *
 * What the code does (read off the instructions): a node bound to an owner
 * actor (the player via 0015C420, Roger via 001BA8E0) waits 60..99 ticks,
 * then ramps +0x244 from 0 by 0.02 per tick to 1.5. On every ramp tick it
 * places itself on the owner's bone (+0x28, 7 for both first-level owners)
 * at the entry offset, builds a matrix from the owner's rotation (+0xC0) and
 * that point, and emits one GIF chain (source block D_00253670, kind 1) with
 * the ramp value and a per-cycle random scalar. It ends (lifecycle 3, then
 * 001AFC10) when the owner's +0x220 <= 0 (owner +0x02 & 0x1F == 0) or the
 * owner's +0x04 >= 2 (otherwise), or at lifecycle 0 when the key has no
 * entry or D_008106C8 bit 3 is set.
 *
 * Everything the functions call outside the seven translations is an
 * explicit worker named by original address. A reached NULL worker or data
 * view, a negative worker result, an index outside a view, or a path where
 * the original reads an uninitialised register latches a fault (fail-stop)
 * and the call returns -1. A latched fault (fault->code != 0 on entry) makes
 * every later call of the functions that take the fault block (_tick,
 * _spawn_001F0120, _001E23A0, _001CFA60, _001CFBE0) return -1 without
 * reading or writing anything; 001E2290 and the EE helpers are pure.
 *
 * stdint only; no dependency on any port subsystem.
 */
#ifndef EM_HEAD_SPRITE_ORIGINAL_H
#define EM_HEAD_SPRITE_ORIGINAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_HEAD_SPRITE_ORIGINAL_CALLBACK 0x001E2560u /* node +0x10 (effect entry 0x10) */
#define EM_HEAD_SPRITE_ORIGINAL_HANDLE 0x80000010u   /* 001EF9D0 a0 in 001F0120 */
#define EM_HEAD_SPRITE_ORIGINAL_SOURCE 0x00253670u   /* D_00253670: 001CFBE0 a2 */
#define EM_HEAD_SPRITE_ORIGINAL_KIND 1               /* 001CFBE0 a1 in 001E2560 */
#define EM_HEAD_SPRITE_ORIGINAL_CHAIN 0x007635C0u    /* D_007635C0: chain table (a0) */

/* Fault codes 1..4 are numerically the EM_SCENE_FAULT_* codes of
 * em_scene_state.h; 6 has no scene twin (an adapter maps it to BAD_RESULT). */
enum {
    EM_HEAD_SPRITE_FAULT_NONE = 0,
    EM_HEAD_SPRITE_FAULT_NULL_WORKER = 1,   /* reached worker or data view is NULL */
    EM_HEAD_SPRITE_FAULT_WORKER_FAILED = 2, /* worker returned a negative value */
    EM_HEAD_SPRITE_FAULT_BAD_RESULT = 3,    /* view/record mismatch (owner address) */
    EM_HEAD_SPRITE_FAULT_BAD_INDEX = 4,     /* bone slot outside the owner view, or
                                               a tick after the 001AFC10 free */
    EM_HEAD_SPRITE_FAULT_UNDEFINED = 6      /* 001CFBE0: mode not 1..4 or kind >= 7,
                                               where the original uses stale s0/s1 */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_HEAD_SPRITE_FAULT_* */
} EmHeadSpriteOriginalFault;

/* The node record bytes the translations read or write (original offsets).
 * Float fields are carried bit-exactly; every arithmetic on them follows the
 * EE model (docs/EE_FLOAT_MODEL.md). */
typedef struct EmHeadSpriteOriginal {
    uint32_t self;       /* native: the record's original address (a0) */
    uint8_t lifecycle;   /* +0x04: 0 init, 1 run, 2/3 free, other: nothing */
    uint8_t sub;         /* +0x05: 0 wait, 1 ramp, other: nothing */
    uint8_t b09;         /* +0x09: lifecycle 0 writes 0 */
    uint8_t b0C;         /* +0x0C: lifecycle 0 writes 0 */
    uint8_t key;         /* +0x0D: 001F0120 writes the key; 001E23A0 reads it */
    uint32_t owner;      /* +0x24: owner record (001F0120: owner +0x14) */
    int32_t bone;        /* +0x28: 001E23A0 writes the entry word */
    float local[4];      /* +0xA0..+0xAC: 001E23A0 writes x, y, z, 1.0 */
    float pos[4];        /* +0xB0..+0xBC: written by the 001026A0 worker */
    float matrix[16];    /* +0xD0..+0x10C: written by the 00102xxx workers */
    int32_t timer;       /* +0x1F0: wait countdown */
    float ramp;          /* +0x244 (+0x54 of the +0x1F0 block) */
    float scalar;        /* +0x24C (+0x5C of the +0x1F0 block) */
    uint8_t freed;       /* native: 1 after the 001AFC10 worker ran */
} EmHeadSpriteOriginal;

/* The owner record bytes 001E2560 reads (s1 = record +0x24). */
typedef struct {
    uint32_t address;       /* must equal the record's +0x24 */
    uint8_t b01;            /* +0x01: nonzero runs the sub-state machine */
    uint8_t b02;            /* +0x02: low 5 bits pick the +0x220 or the +0x04 test */
    uint8_t b04;            /* +0x04: >= 2 ends the effect (b02 & 0x1F != 0) */
    float f220;             /* +0x220: <= 0 ends the effect (b02 & 0x1F == 0) */
    const uint32_t *slots;  /* +0x110: bone slot addresses (read in the ramp) */
    uint32_t slot_count;    /* entries the view covers */
    const float *rot;       /* +0xC0..+0xC8: handed to 00102C58 (ramp only) */
} EmHeadSpriteOriginalOwner;

/* Data from the user's boot ELF (constant in every capture). */
typedef struct {
    uint8_t d2535F0[8][16];  /* 001E23A0 entries {int32 word; float x, y, z} */
    uint8_t d253670[0x90];   /* D_00253670: 9 quadwords; +0x8C = mode word */
    uint8_t d251260[8][16];  /* 001CFBE0 rows (D_00251260, 16-byte stride) */
} EmHeadSpriteOriginalTables;

/* Load the tables from the user's boot ELF image (SCUS_971.12, 1532624
 * bytes; one PROGBITS section at file 0x300 = vram 0x100000). 0 or -1. */
int em_head_sprite_original_load_tables(const uint8_t *elf, size_t size,
                                        EmHeadSpriteOriginalTables *out);

/* Global data. Each pointer is required only on the path that reads it. */
typedef struct {
    int32_t d8106C8;              /* 001B0070 result (lifecycle 0) */
    int16_t d810E80;              /* D_00810E80[0]: 0 -> end D_004F35C0, else D_005635C0 */
    uint32_t cursor;              /* *(D_00275670 + 0x18): packet write cursor */
    const uint8_t *scratch_3A40;  /* D_70003A40: 64 bytes */
    const uint8_t *scratch_3AC0;  /* D_70003AC0: 64 bytes */
    const uint8_t *ctx_A0;        /* D_00275670 + 0xA0: 16 bytes */
    const EmHeadSpriteOriginalTables *tables;
} EmHeadSpriteOriginalWorld;

/* The 0x58-byte block 001CFA60 fills (001E2560's sp+0x40). */
typedef struct {
    uint8_t q[64];                /* +0x00..+0x3F: the source matrix, copied */
    uint32_t m40;                 /* +0x40: 001CD370(0) result (an address) */
    const uint8_t *m40_bytes;     /* native: the 64 bytes at m40 */
    uint32_t w44, w48, w4C, w50, w54; /* +0x44..+0x54, bit patterns */
} EmHeadSpriteOriginalXf;

/* The source block 001CFBE0 reads (a2): address + its 0x90 bytes. */
typedef struct {
    uint32_t address;
    const uint8_t *bytes;
} EmHeadSpriteOriginalSource;

/* Workers, one per original callee. All return >= 0 on success, < 0 on a
 * port failure (fault). `ctx` identifies the caller to the adapter. */
typedef struct {
    void *ctx;
    /* 00122BB8(): the game RNG; *value = its v0 (0..0x7FFFFFFF). */
    int (*w_00122BB8)(void *ctx, int32_t *value);
    /* 001EF9D0(handle, a1, weight): effect allocation (it takes a pool
     * record through 001AFA90 and writes +0x03, +0x0D, +0x10, +0x38).
     * *record = the native record, NULL for the original 0 result. */
    int (*w_001EF9D0)(void *ctx, uint32_t handle, uint32_t a1, float weight,
                      EmHeadSpriteOriginal **record);
    /* 001AFC10(self): pool free. */
    int (*w_001AFC10)(void *ctx, uint32_t self);
    /* 001026A0(out, m, v): out = m * v (all four lanes); m is the owner's
     * bone matrix at slot address + 0x90. */
    int (*w_001026A0)(void *ctx, float out[4], uint32_t matrix_address, const float v[4]);
    /* 001029C0(m): identity. */
    int (*w_001029C0)(void *ctx, float m[16]);
    /* 00102C58(m, m, v): Z, Y, X rotations by v[2], v[1], v[0] (in place;
     * the original a1 is the same matrix). */
    int (*w_00102C58)(void *ctx, float m[16], const float v[3]);
    /* 00102918(out, in, v): rows 0..2 copied, row 3 = in row 3 + v (xyz). */
    int (*w_00102918)(void *ctx, float out[16], const float in[16], const float v[4]);
    /* 001CCF70(pos): clip test + projection; *handle = the depth key
     * (0xFFFFFF when clipped) used as the chain id. */
    int (*w_001CCF70)(void *ctx, const float pos[4], int32_t *handle);
    /* 001CD370(a0): *address = D_00275670 + a0 * 0x40 + 0x2240 and *bytes
     * = the 64 bytes there. */
    int (*w_001CD370)(void *ctx, int32_t a0, uint32_t *address, const uint8_t **bytes);
    /* 001CB5F0(a0, id, count): opens a packet of `count` quadwords in the
     * chain; *out = its writable bytes (count * 16). */
    int (*w_001CB5F0)(void *ctx, uint32_t a0, int32_t id, int32_t count, uint8_t **out);
    /* 001CB6B0(a0, id, count, address): reference tag to `address`. */
    int (*w_001CB6B0)(void *ctx, uint32_t a0, int32_t id, int32_t count, uint32_t address);
    /* 001CB760(a0, id, tbl, ent). */
    int (*w_001CB760)(void *ctx, uint32_t a0, int32_t id, uint32_t tbl, uint32_t ent);
    /* 001CB900(a0, id, mode). */
    int (*w_001CB900)(void *ctx, uint32_t a0, int32_t id, int32_t mode);
} EmHeadSpriteOriginalWorkers;

/* 001E2290(key): 1 for the 20 keys that have an 001E23A0 entry, else 0. */
int em_head_sprite_original_001E2290(int32_t key);

/* 001E23A0(record): entry lookup by record +0x0D. Returns 1 (no entry) or 0
 * (+0xA0..+0xAC and +0x28 written). -1 on a NULL tables view or a latched
 * fault. */
int em_head_sprite_original_001E23A0(EmHeadSpriteOriginal *record,
                                     const EmHeadSpriteOriginalTables *tables,
                                     EmHeadSpriteOriginalFault *fault);

/* 001F0120(owner, key): owner14 is the owner's +0x14 word. *record = the
 * new record or NULL (both original outcomes). Returns 0, or -1 on a fault. */
int em_head_sprite_original_spawn_001F0120(uint32_t owner14, int32_t key,
                                           const EmHeadSpriteOriginalWorkers *w,
                                           EmHeadSpriteOriginal **record,
                                           EmHeadSpriteOriginalFault *fault);

/* One call of 001E2560. `owner` is read in lifecycle 1 (the ramp also reads
 * its slots and rot); `world` in lifecycle 0 (d8106C8, tables) and on a ramp
 * tick (001CFBE0). Returns 1 while allocated, 0 after the 001AFC10 free,
 * -1 on a fault. */
int em_head_sprite_original_tick(EmHeadSpriteOriginal *record,
                                 const EmHeadSpriteOriginalOwner *owner,
                                 const EmHeadSpriteOriginalWorld *world,
                                 const EmHeadSpriteOriginalWorkers *w,
                                 EmHeadSpriteOriginalFault *fault);

/* 001CFA60(obj, src, f12, f13); f12/f13 as bit patterns (the original only
 * stores them, no arithmetic). Returns 0, or -1 on a fault (including a
 * latched one). */
int em_head_sprite_original_001CFA60(EmHeadSpriteOriginalXf *xf, const float src[16],
                                     uint32_t f12, uint32_t f13,
                                     const EmHeadSpriteOriginalWorkers *w,
                                     EmHeadSpriteOriginalFault *fault);

/* 001CFBE0(id, kind, st, xf, copy). Returns 1 when it emitted, 0 when the
 * free-space guard skipped it, -1 on a fault (including a latched one).
 * world->cursor must be *(D_00275670 + 0x18) as it stands at THIS call:
 * every emission earlier in the frame advances it. */
int em_head_sprite_original_001CFBE0(int32_t id, uint32_t kind,
                                     const EmHeadSpriteOriginalSource *st,
                                     const EmHeadSpriteOriginalXf *xf, int32_t copy,
                                     const EmHeadSpriteOriginalWorld *world,
                                     const EmHeadSpriteOriginalWorkers *w,
                                     EmHeadSpriteOriginalFault *fault);

/* EE-model helpers used above (exposed for tests): ADD.S, C.LE.S, CVT.S.W
 * and DIV.S on bit patterns (docs/EE_FLOAT_MODEL.md section 2). */
uint32_t em_head_sprite_ee_add(uint32_t a, uint32_t b);
int em_head_sprite_ee_c_le(uint32_t a, uint32_t b);
uint32_t em_head_sprite_ee_cvt_s_w(int32_t value);
uint32_t em_head_sprite_ee_div(uint32_t a, uint32_t b);

#ifdef __cplusplus
}
#endif

#endif /* EM_HEAD_SPRITE_ORIGINAL_H */
