/* Effect spawn chain, the generic puff driver 001EA240 and the ring decals
 * 001F0460 (lane effect-puff-original). Docs: docs/EFFECT_ORIGINAL.md.
 *
 * Hand translation of the original functions
 *   001EFD90  spawn at (pos, rot)              byte-matched C
 *   001EFD20  spawn at pos, rot = 0            byte-matched C
 *   001EF9D0  effect allocator                 NEARMISS: logic read from the .s
 *   001EF940  effect sound                     byte-matched C (prototype from the .s)
 *   001D80E0 / 001D8100  point-light presets   byte-matched C (001D7FA0 is a worker)
 *   001EA240  effect driver (state 0 seeding, state 1 tick)   byte-matched C
 *   001CCF70  projection / depth key           NEARMISS: read from the .s
 *   001CD390  look-at rows                     NEARMISS: read from the .s
 *   001F0460  ring decal slots                 byte-matched C
 * and the SDK leaves they reach (001029C0, 00102918, 001026A0, 00102760,
 * 00102718, 001031E0, 00102948, copy_qw4, 001029E8, 00102A60, 00102BB0,
 * 00102B08, 00102C58, 001B1470, 0011E860, float_to_int 001281C0 with its
 * unpack 001278C0). Every float operation follows the measured EE/VU0 model
 * of docs/EE_FLOAT_MODEL.md bit for bit (EE add/sub pre-trim, truncation,
 * DAZ/FTZ, saturation; VU0 lanes with the per-form operand clamps).
 *
 * Verified by tools/test_effect_original_reference.py: a Python MIPS/VU0
 * oracle executes the ORIGINAL instructions (arithmetic by
 * tools/ee_float_model.py) over captured route RAM and compares every
 * modelled byte and every worker call.
 *
 * Everything the functions reach outside the modelled bytes is an explicit
 * worker named by its original address. A reached NULL worker, a negative
 * worker result, an address outside the loaded tables, or an input the
 * float model has not measured latches a fault (fail-stop) and the call
 * returns -1; a latched fault makes every later call return -1.
 *
 * stdint only; no dependency on any port subsystem. Not wired: the
 * coordinator binds it (docs/EFFECT_ORIGINAL.md, "Binding").
 */
#ifndef EM_EFFECT_ORIGINAL_H
#define EM_EFFECT_ORIGINAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_EFFECT_ORIGINAL_DRIVER 0x001EA240u      /* entity +0x0C for the puff types */
#define EM_EFFECT_ORIGINAL_TABLE_BASE 0x00257C90u  /* *(D_00259C70): first record */
#define EM_EFFECT_ORIGINAL_TABLE_LIMIT 0x00259C70u /* D_00259C70 itself: end of the tables */
#define EM_EFFECT_ORIGINAL_TABLE_BYTES (EM_EFFECT_ORIGINAL_TABLE_LIMIT - EM_EFFECT_ORIGINAL_TABLE_BASE)
#define EM_EFFECT_ORIGINAL_RECORD 0x30u            /* one entity record */
#define EM_EFFECT_ORIGINAL_AREAS 23                /* D_00259C74[0..22] */
#define EM_EFFECT_ORIGINAL_SUBTYPES 0x2B           /* D_00255430 {step, handler} pairs */
#define EM_EFFECT_ORIGINAL_DECAL_LANES 7           /* 001F0460 presets 0..6 */
#define EM_EFFECT_ORIGINAL_DECAL_SLOTS 32          /* 0xC00 bytes / 0x60 per lane */
#define EM_EFFECT_ORIGINAL_CLIPPED 0x00FFFFFF      /* 001CCF70 result outside the clip volume */

/* Fault codes (numerically the EM_SCENE_FAULT_* codes 1..4 of
 * em_scene_state.h; 5 is this module's own). */
enum {
    EM_EFFECT_FAULT_NONE = 0,
    EM_EFFECT_FAULT_NULL_WORKER = 1,   /* reached worker or data view is NULL */
    EM_EFFECT_FAULT_WORKER_FAILED = 2, /* worker returned a negative value */
    EM_EFFECT_FAULT_BAD_RESULT = 3,    /* worker result the original cannot produce */
    EM_EFFECT_FAULT_BAD_INDEX = 4,     /* address outside the loaded tables/ring, or a freed node */
    EM_EFFECT_FAULT_UNMEASURED = 5     /* non-finite clip input, or a 001B1470 loop that never ends */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_EFFECT_FAULT_* */
} EmEffectOriginalFault;

/* ELF data the chain reads. The entity records are mutable: 001EF9D0
 * writes the table's first record +0x24 for ids 0x80000026/2C/67. */
typedef struct {
    uint8_t bytes[EM_EFFECT_ORIGINAL_TABLE_BYTES]; /* vram 0x257C90..0x259C70 */
    uint32_t global;                               /* D_00259C70 */
    uint32_t area[EM_EFFECT_ORIGINAL_AREAS];       /* D_00259C74[]: 0 = none */
    uint32_t step[EM_EFFECT_ORIGINAL_SUBTYPES];    /* D_00255430 + 8n: float bits */
    uint32_t handler[EM_EFFECT_ORIGINAL_SUBTYPES]; /* D_00255434 + 8n */
} EmEffectOriginalTables;

/* The work block at node +0x1F0 (D_00275C34). */
typedef struct {
    int32_t seed;      /* +0x1F0: 00122BB8 in state 0 */
    int32_t seed_copy; /* +0x1F4: = seed before each handler call */
    float step;        /* +0x1F8: D_00255430[subtype] */
    float limit;       /* +0x1FC: 0 = endless with the 2.0 decay clamp */
    float accumulator; /* +0x244 */
    float fraction;    /* +0x24C: rand / 2^31 */
} EmEffectOriginalWork;

/* The pool record bytes the chain reads or writes. The rest of the record
 * belongs to the pool (001AFA90) and to the per-subtype handler. */
typedef struct {
    uint8_t b03;       /* +0x03 <- entity +0x04 */
    uint8_t state;     /* +0x04: 0 seed, 1 run, 2/3 free, other: nothing */
    uint8_t b09;       /* +0x09: state 0 writes 0 */
    uint8_t b0C;       /* +0x0C: state 0 writes 0 */
    uint8_t subtype;   /* +0x0D <- entity +0x08 */
    uint32_t callback; /* +0x10 <- entity +0x0C */
    int32_t live38;    /* +0x38 <- 1 */
    float pos[4];      /* +0xB0 (+0xBC = 1.0 after 001EFD90/001EFD20) */
    float rot[4];      /* +0xC0 */
    float matrix[16];  /* +0xD0..+0x10F, row 3 (+0x100) = translation */
    EmEffectOriginalWork work;
    uint8_t freed;     /* native: 1 after the 001AFC10 worker ran */
} EmEffectOriginalNode;

/* Globals and scratchpad words the chain reads or writes. */
typedef struct {
    uint8_t d8101E4;   /* 001EF940: == 3 skips the sound */
    uint8_t d810700;   /* area index for ids without the sign bit */
    int32_t d275C38;   /* 001EF9D0 kind-4 throttle stamp */
    int32_t spad3B68;  /* 0x70003B68 frame clock (read) */
    float d8102E8;     /* 001EA240 subtypes 9/0xE/0x24 (read) */
    EmEffectOriginalNode *d275C30; /* 001EA240: current node */
    EmEffectOriginalWork *d275C34; /* 001EA240: its work block, re-read after the handler */
    int32_t d275C04;   /* 001CCF70: float_to_int(view w) */
    uint32_t spad3600[16]; /* 0x70003600..0x7000363F raw words (001CCF70, 001CD390) */
    uint32_t spad38A0[4];  /* 0x700038A0..0x700038AC raw words (001EA240 9/0xE/0x24) */
} EmEffectOriginalGlobals;

/* View data 001CCF70 reads. */
typedef struct {
    float clip[16];   /* *(D_00275670) + 0x2240 (001CD370(0)), rows 0..3 */
    float fog[4];     /* *(D_00275670) + 0xA0 */
    float camera[16]; /* scratchpad 0x70003AC0, rows 0..3 */
} EmEffectOriginalView;

/* One 0x60-byte 001F0460 ring slot at D_0028F700 + 0x4DBEC0 + n*0xC00 + i*0x60. */
typedef struct {
    uint32_t source[16]; /* +0x00: copy_qw4(src) raw */
    uint32_t params[4];  /* +0x40: preset parameter vector (float bits) */
    uint64_t tag;        /* +0x50 */
    int32_t life;        /* +0x58: count * 60 */
    uint32_t w5C;        /* +0x5C: not written */
} EmEffectOriginalDecalSlot;

typedef struct {
    int32_t index[EM_EFFECT_ORIGINAL_DECAL_LANES]; /* D_0081F950[n] */
    EmEffectOriginalDecalSlot slot[EM_EFFECT_ORIGINAL_DECAL_LANES][EM_EFFECT_ORIGINAL_DECAL_SLOTS];
} EmEffectOriginalDecals;

/* Workers, one per original callee. All return >= 0 on success and < 0 on
 * a port failure (fault). */
typedef struct {
    void *ctx;
    /* 001AFA90(class): pool alloc. *node = NULL when the pool refuses (the
     * original then returns 0 without a fault). The module writes only the
     * modelled bytes; the adapter owns the rest of the record. */
    int (*w_001AFA90)(void *ctx, uint8_t cls, EmEffectOriginalNode **node);
    /* 00122BB8: the game rand; *value = its v0. */
    int (*w_00122BB8)(void *ctx, int32_t *value);
    /* 001D7FA0(pos, color, type, fa, fb): the point-light register. When the
     * pool's live count (D_00275670 +0x214) is already 0x20 the original
     * writes nothing and returns -1; every caller here (001D80E0, 001D8100,
     * 001EF9D0 kind 4) discards that result, so a full pool silently drops
     * the light. The worker must therefore return >= 0 in that case; any
     * negative return is a worker fault. Do NOT bind em_point_light_register
     * directly (it returns -1 when full); wrap it and discard its result, as
     * docs/EFFECT_ORIGINAL.md "Boundaries" shows. */
    int (*w_001D7FA0)(void *ctx, const float pos[4], const float color[4], int32_t type,
                      float fa, float fb);
    /* 001FBF50(scratch, &a, &b, 0, f12, f13): reads only scratch +0xB0..+0xBC,
     * the copy of `pos`. *result = its v0. */
    int (*w_001FBF50)(void *ctx, const float pos[4], float f12, float f13, int32_t *a,
                      int32_t *b, int32_t *result);
    /* 001FB9F0(id, 0x1000, a, b): sound submit. */
    int (*w_001FB9F0)(void *ctx, int32_t id, int32_t a1, int32_t a2, int32_t a3);
    /* 0021B9A0(channel, f12, f13): pad rumble. */
    int (*w_0021B9A0)(void *ctx, int32_t channel, float f12, float f13);
    /* Indirect call through D_00255434[subtype] with (node + 0xD0, depth,
     * D_00275C34): the per-subtype draw handler at `handler`. */
    int (*w_handler)(void *ctx, uint32_t handler, EmEffectOriginalNode *node, int32_t depth,
                     EmEffectOriginalWork *work);
    /* 001AFC10(node): pool free. */
    int (*w_001AFC10)(void *ctx, EmEffectOriginalNode *node);
} EmEffectOriginalWorkers;

typedef struct {
    EmEffectOriginalTables *tables;
    EmEffectOriginalGlobals *globals;
    EmEffectOriginalDecals *decals;
    const EmEffectOriginalView *view;
    const EmEffectOriginalWorkers *workers;
    EmEffectOriginalFault fault;
} EmEffectOriginal;

/* Loads the tables from the user's boot ELF (SCUS_971.12, 1532624 bytes). */
int em_effect_original_load_tables(const uint8_t *elf, size_t size, EmEffectOriginalTables *out);

/* The spawn chain. *node receives the allocated node or NULL (the original
 * returns 0: no entity, or the pool refused). Return 0, or -1 on a fault. */
int em_effect_original_001EF9D0(EmEffectOriginal *e, uint32_t id, const float *pos, float f12,
                                EmEffectOriginalNode **node);
int em_effect_original_001EFD90(EmEffectOriginal *e, uint32_t id, const float pos[4],
                                const float rot[4], EmEffectOriginalNode **node);
int em_effect_original_001EFD20(EmEffectOriginal *e, uint32_t id, const float pos[4],
                                EmEffectOriginalNode **node);
/* 001EF940(entity at vram `entity`, pos). */
int em_effect_original_001EF940(EmEffectOriginal *e, uint32_t entity, const float pos[4]);
int em_effect_original_001D80E0(EmEffectOriginal *e, const float pos[4], const float color[4]);
int em_effect_original_001D8100(EmEffectOriginal *e, const float pos[4], const float color[4]);

/* 001F0460(n, src): src is 16 raw words (4 quadwords). */
int em_effect_original_001F0460(EmEffectOriginal *e, int32_t n, const float src[16]);

/* One call of 001EA240. Returns 1 while allocated, 0 after the 001AFC10
 * free, -1 on a fault. */
int em_effect_original_001EA240(EmEffectOriginal *e, EmEffectOriginalNode *node);

/* 001CCF70(pos): *result = the 12.4 depth key, or EM_EFFECT_ORIGINAL_CLIPPED. */
int em_effect_original_001CCF70(EmEffectOriginal *e, const float pos[4], int32_t *result);
/* 001CD390(out, v): writes out (a 4x4 matrix) and globals spad3600[4..15]. */
int em_effect_original_001CD390(EmEffectOriginal *e, float out[16], const float v[4]);

/* ---- SDK leaves on the float model, exposed for the reference test. ---- */
void em_effect_original_00102C58(float out[16], const float in[16], const float angles[4]);
void em_effect_original_00102BB0(float out[16], const float in[16], float angle);
void em_effect_original_00102760(float out[4], const float in[4]);
void em_effect_original_00102718(float out[4], const float a[4], const float b[4]);
void em_effect_original_001026A0(float out[4], const float m[16], const float v[4]);
/* 001B1470: 0 and *out on success, -1 when the loop cannot end. */
int em_effect_original_001B1470(float angle, float *out);
int32_t em_effect_original_float_to_int(float value); /* 001281C0 */

/* ---- The float model itself (raw binary32 bits), for the test. ---- */
enum {
    EM_EFFECT_FP_EE_ADD, EM_EFFECT_FP_EE_SUB, EM_EFFECT_FP_EE_MUL, EM_EFFECT_FP_EE_DIV,
    EM_EFFECT_FP_EE_CVT_W_S, EM_EFFECT_FP_EE_CVT_S_W, EM_EFFECT_FP_EE_C_EQ,
    EM_EFFECT_FP_EE_C_LT, EM_EFFECT_FP_EE_C_LE,
    EM_EFFECT_FP_VU_ADD, EM_EFFECT_FP_VU_SUB, EM_EFFECT_FP_VU_MUL, EM_EFFECT_FP_VU_MADD,
    EM_EFFECT_FP_VU_OPMSUB, EM_EFFECT_FP_VU_DIV, EM_EFFECT_FP_VU_SQRT, EM_EFFECT_FP_VU_FTOI4,
    EM_EFFECT_FP_VU_MIN, EM_EFFECT_FP_VU_MAX,
    EM_EFFECT_FP_FLOAT_TO_INT, /* 001281C0 on the bits of a */
    EM_EFFECT_FP_WRAP          /* 001B1470 on the bits of a; 0xFFFFFFFF when it never ends */
};
/* flags: bit0 clamp fs, bit1 clamp ft, bit2 clamp ACC, bit3 NaN order
 * (VU lanes), or for VU_DIV bit0 = the reciprocal (NaN divisor passes) form. */
uint32_t em_effect_original_fp(int op, unsigned flags, uint32_t a, uint32_t b, uint32_t acc);

#ifdef __cplusplus
}
#endif

#endif /* EM_EFFECT_ORIGINAL_H */
