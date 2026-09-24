/* Effect kinds: the per-subtype draw handlers of the effect driver 001EA240,
 * the room glow markers, the effect colour, the effect-pool resets and the
 * room point-light lists (lane effect-kinds, census L27 plus the truck
 * handler 001EBF10 of L23). Docs: docs/EFFECT_KINDS.md.
 *
 * Hand translation of the original functions
 *   001EC1F0  subtype 0x0A handler (footstep surface 0)      NEARMISS: the .s
 *   001EC3F0  subtype 0x05 handler (0x80000028 snow puff)     NEARMISS: the .s
 *   001EC470  subtype 0x24 handler (0x80000065 slide puff)    NEARMISS: the .s
 *   001EBF10  subtype 0x20 handler (0x80000049 truck puffs)   NEARMISS: the .s
 *   001F54E0  effect colour (pickup indicators)              asm-word: the .s
 *   001F5640  room glow-marker list selector                 byte-matched C
 *   001F5940  one glow marker (colour, size, emit mode)      byte-matched C
 *   001F5C20  the glow-marker walker                         byte-matched C
 *   001F5CA0  room FX-record list selector                   byte-matched C
 *   001F0310  effect-pool reset barrel                       byte-matched C
 *   001F03D0  ring-decal lane reset                          byte-matched C
 *   001F3FA0  particle pool reset                            NEARMISS: the .s
 *   001F6760  room point-light list selector (primary)       byte-matched C
 *   001F6D60  room point-light list selector (auxiliary)     byte-matched C
 *   001F6640  point-light list registration                  byte-matched C
 *   001F66F0  point-light list release                       byte-matched C
 *   001F6850  primary list release                           byte-matched C
 *   001F68B0  primary list latch dispatch                    byte-matched C
 *   001F6E40  auxiliary list release + registration          byte-matched C
 * and the SDK leaf 001029C0 (identity) they reach. Every float operation
 * goes through em_ee_float.h on bit patterns (docs/EE_FLOAT_MODEL.md).
 *
 * Verified by tools/test_effect_kinds_reference.py: a Python MIPS/VU0 oracle
 * executes the ORIGINAL instructions (arithmetic by tools/ee_float_model.py)
 * over the ELF image and over captured route RAM, and compares every
 * modelled byte and every worker call.
 *
 * Everything the functions reach outside the modelled bytes is an explicit
 * worker named by its original address. A reached NULL worker or view, a
 * negative worker result, an address outside the loaded data, or a handler
 * address this module does not translate latches a fault (fail-stop) and the
 * call returns -1; a latched fault makes every later call return -1.
 *
 * stdint only, plus em_effect_original.h for the shared work-block and
 * ring-decal types. Not wired: the coordinator binds it (docs/EFFECT_KINDS.md,
 * "Binding").
 */
#ifndef EM_EFFECT_KINDS_H
#define EM_EFFECT_KINDS_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_effect_original.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ELF data window: the glow-marker, FX-record and point-light lists
 * (0x28-byte records, a negative first halfword terminates each list).
 * The point-light lists are mutable: +0x24 holds the registered handle. */
#define EM_EFFECT_KINDS_LISTS_BASE 0x0025AD80u
#define EM_EFFECT_KINDS_LISTS_END 0x0025D800u
#define EM_EFFECT_KINDS_LISTS_BYTES (EM_EFFECT_KINDS_LISTS_END - EM_EFFECT_KINDS_LISTS_BASE)
#define EM_EFFECT_KINDS_RECORD 0x28u
/* D_0026EB70: the colour presets 001F6640 hands to 001D7FA0 (16 bytes each).
 * The lists use presets 0..3 only; any other index faults. */
#define EM_EFFECT_KINDS_TEMPLATE_BASE 0x0026EB70u
#define EM_EFFECT_KINDS_TEMPLATES 4
/* The secondary lists 001F6850/001F68B0 name directly for key 0x1301. */
#define EM_EFFECT_KINDS_LIST_25D270 0x0025D270u
#define EM_EFFECT_KINDS_LIST_25D2C0 0x0025D2C0u
/* D_007709C0: the particle pool 001F3FA0 resets (0x80 records of 0x90). */
#define EM_EFFECT_KINDS_PARTICLES 0x80
#define EM_EFFECT_KINDS_PARTICLE_BYTES 0x90
/* D_0081F8F0: the transform block the handlers hand to 001CFB50/001CFBE0. */
#define EM_EFFECT_KINDS_XF 0x0081F8F0u
/* The handler addresses (D_00255434[subtype * 2] in the ELF). */
#define EM_EFFECT_KINDS_H_001EC1F0 0x001EC1F0u
#define EM_EFFECT_KINDS_H_001EC3F0 0x001EC3F0u
#define EM_EFFECT_KINDS_H_001EC470 0x001EC470u
#define EM_EFFECT_KINDS_H_001EBF10 0x001EBF10u

/* Fault codes 1..4 are numerically EM_EFFECT_FAULT_* 1..4. */
enum {
    EM_EFFECT_KINDS_FAULT_NONE = 0,
    EM_EFFECT_KINDS_FAULT_NULL_WORKER = 1,   /* reached worker or view is NULL */
    EM_EFFECT_KINDS_FAULT_WORKER_FAILED = 2, /* worker returned a negative value */
    EM_EFFECT_KINDS_FAULT_BAD_RESULT = 3,    /* (reserved) */
    EM_EFFECT_KINDS_FAULT_BAD_INDEX = 4,     /* list/template/lane outside the loaded data */
    EM_EFFECT_KINDS_FAULT_UNTRANSLATED = 6   /* handler address this module does not translate */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_EFFECT_KINDS_FAULT_* */
} EmEffectKindsFault;

/* Data loaded from the user's boot ELF. */
typedef struct {
    uint8_t lists[EM_EFFECT_KINDS_LISTS_BYTES];            /* vram 0x25AD80.. */
    uint32_t templates[EM_EFFECT_KINDS_TEMPLATES][4];      /* D_0026EB70, float bits */
} EmEffectKindsTables;

/* Globals and scratchpad words the functions read or write. */
typedef struct {
    uint8_t d810700;           /* area (key high byte) */
    uint8_t d810701;           /* sub-area (key low byte) */
    uint8_t d81075D, d81075E, d810761, d810778, d81077B, d810784, d810785, d81079E;
                               /* 001F68B0 latch bytes (read) */
    int32_t spad3B68;          /* 0x70003B68 frame clock (001F5940 mode 1, read) */
    uint32_t spad36A0[16];     /* 0x700036A0..0x700036DF (001EBF10 source matrix, written) */
    int32_t d275C40, d275C44;  /* 001F3FA0 writes 0 */
} EmEffectKindsGlobals;

/* D_007709C0: 0x80 particle records; 001F3FA0 clears each and sets the
 * halfword at +0x80 (the "free" mark 001F40C0 tests) to 1. */
typedef struct {
    uint8_t record[EM_EFFECT_KINDS_PARTICLES][EM_EFFECT_KINDS_PARTICLE_BYTES];
} EmEffectKindsParticles;

/* Workers, one per original callee. All return >= 0 on success and < 0 on
 * a port failure (fault). Float arguments are binary32 bit patterns. */
typedef struct {
    void *ctx;
    /* 001D7FA0(pos, &D_0026EB70[preset*16], 1, 1.0, 0.0) as 001F6640 calls
     * it: *handle = its v0, which 001F6640 stores in the record. -1 (a full
     * light pool) is an original result and is stored, not a fault. */
    int (*w_001D7FA0)(void *ctx, const float pos[4], uint32_t template_address,
                      const float color[4], int32_t type, uint32_t f12, uint32_t f13,
                      int32_t *handle);
    /* 001D80B0(handle): point-light release. */
    int (*w_001D80B0)(void *ctx, int32_t handle);
    /* 001F4D40(pos, col, size, half): glow-marker emit (col = 4 ints). */
    int (*w_001F4D40)(void *ctx, const float pos[4], const int32_t col[4], uint32_t f12,
                      uint32_t f13);
    /* 0011DF78(f12): *f0 = its result. */
    int (*w_0011DF78)(void *ctx, uint32_t f12, uint32_t *f0);
    /* 001281C0 float_to_int(f12): *v0 = its result. */
    int (*w_001281C0)(void *ctx, uint32_t f12, int32_t *v0);
    /* 0021B9A0(a0, f12, f13). */
    int (*w_0021B9A0)(void *ctx, int32_t a0, uint32_t f12, uint32_t f13);
    /* 00122BB8(): the game rand; *v0 = its result. */
    int (*w_00122BB8)(void *ctx, int32_t *v0);
    /* 001F54E0's indirect call obj->+0x4C(obj): `fn` is the +0x4C word. */
    int (*w_indirect)(void *ctx, uint32_t fn, void *obj);
    /* 001CFB50(dst, a1, src, f12, f13, f14, f15, f16): dst is the vram of
     * the transform block (EM_EFFECT_KINDS_XF); src is the 64-byte matrix
     * (the node's +0xD0 matrix, or globals->spad36A0 for 001EBF10). */
    int (*w_001CFB50)(void *ctx, uint32_t dst, uint32_t a1, const float src[16], uint32_t f12,
                      uint32_t f13, uint32_t f14, uint32_t f15, uint32_t f16);
    /* 001CFBE0(id, kind, source, xf, copy). */
    int (*w_001CFBE0)(void *ctx, int32_t id, int32_t kind, uint32_t source, uint32_t xf,
                      int32_t copy);
} EmEffectKindsWorkers;

typedef struct {
    EmEffectKindsTables *tables;
    EmEffectKindsGlobals *globals;
    EmEffectOriginalDecals *decals;       /* 001F03D0 */
    EmEffectKindsParticles *particles;    /* 001F3FA0 */
    const EmEffectKindsWorkers *workers;
    EmEffectKindsFault fault;
} EmEffectKinds;

/* Loads the tables from the user's boot ELF (SCUS_971.12, 1532624 bytes). */
int em_effect_kinds_load_tables(const uint8_t *elf, size_t size, EmEffectKindsTables *out);

/* ---- per-subtype handlers (called by 001EA240 through D_00255434) ---- */
/* matrix = node +0xD0 (the handler's a0), depth = its a1, work = D_00275C34. */
int em_effect_kinds_001EC1F0(EmEffectKinds *k, const float matrix[16], int32_t depth,
                             EmEffectOriginalWork *work);
int em_effect_kinds_001EC3F0(EmEffectKinds *k, const float matrix[16], int32_t depth,
                             EmEffectOriginalWork *work);
int em_effect_kinds_001EC470(EmEffectKinds *k, const float matrix[16], int32_t depth,
                             EmEffectOriginalWork *work);
int em_effect_kinds_001EBF10(EmEffectKinds *k, const float matrix[16], int32_t depth,
                             EmEffectOriginalWork *work);
/* Dispatch by handler address; any other address faults (UNTRANSLATED). */
int em_effect_kinds_handler(EmEffectKinds *k, uint32_t handler, const float matrix[16],
                            int32_t depth, EmEffectOriginalWork *work);

/* ---- effect colour ---- */
/* 001F54E0(obj, color): out = obj +0x80..+0x8C, callback = obj +0x4C, obj is
 * handed to the indirect call. color may alias out (all four lanes are read
 * before the first store, as in the original). */
int em_effect_kinds_001F54E0(EmEffectKinds *k, void *obj, float out[4], uint32_t callback,
                             const float color[4]);

/* ---- selectors (vram of the selected list, or 0) ---- */
uint32_t em_effect_kinds_001F5640(uint8_t area, uint8_t sub);
uint32_t em_effect_kinds_001F5CA0(uint8_t area, uint8_t sub);
uint32_t em_effect_kinds_001F6760(uint8_t area, uint8_t sub);
uint32_t em_effect_kinds_001F6D60(uint8_t area, uint8_t sub);

/* ---- glow markers ---- */
/* 001F5940(kind, pos, t). pos is passed through to 001F4D40. */
int em_effect_kinds_001F5940(EmEffectKinds *k, uint32_t kind, const float pos[4], int32_t t);
int em_effect_kinds_001F5C20(EmEffectKinds *k);

/* ---- pool resets ---- */
int em_effect_kinds_001F0310(EmEffectKinds *k);
int em_effect_kinds_001F03D0(EmEffectKinds *k, int32_t lane);
int em_effect_kinds_001F3FA0(EmEffectKinds *k);

/* ---- room point-light lists (list = vram inside the lists window, or 0) ---- */
int em_effect_kinds_001F6640(EmEffectKinds *k, uint32_t list);
int em_effect_kinds_001F66F0(EmEffectKinds *k, uint32_t list);
int em_effect_kinds_001F6850(EmEffectKinds *k);
int em_effect_kinds_001F68B0(EmEffectKinds *k);
int em_effect_kinds_001F6E40(EmEffectKinds *k);

#ifdef __cplusplus
}
#endif

#endif /* EM_EFFECT_KINDS_H */
