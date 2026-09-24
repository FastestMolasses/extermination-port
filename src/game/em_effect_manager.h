/* Effect manager barrel 001F0360 and its workers (lane effect-manager,
 * census lane L26). Docs: docs/EFFECT_MANAGER.md.
 *
 * Hand translation of the original functions
 *   001F0360  the barrel: 001F6210, 001F5C20, 001F6BB0, 001F6EB0, 001F40C0,
 *             then 001F0720(0, 1, 3, 4, 5, 6)               byte-matched C
 *   001F6210  area model sprites (001F5CA0 list)             byte-matched C, float order from the .s
 *   001F6BB0  area 0 / 0x1301 point-light upkeep selector    NEARMISS: the .s
 *   001F6EB0  area 7 / 0x12 track switch                     byte-matched C
 *   001F40C0  the 0x80-entity sweep at D_007709C0            byte-matched C
 *   001F0720  ring-lane ageing and the four-packet lane draw NEARMISS: the .s
 *   001F0A60  camera-facing glint sprite (two triangles)     asm only: the .s
 *   001F4D40  rand-pulsed 001CD520 sprite                    asm only: the .s
 *   the draw block of 001F1180 (0x1F136C..0x1F1470)          NEARMISS: the .s
 * and the inline leaves 001F6AC0 (a word compare), 00102948 (quadword copy),
 * 001028B8 (vector add) and 001026D0 (four 001026A0 rows).
 *
 * Reused verified translations: em_effect_original (001026A0, float_to_int
 * 001281C0, the 001F0460 ring type) and em_owner_services_original (001029C0,
 * 00102A60, 00102C58, 00102918). 001F1110 and the rest of 001F1180 are
 * em_pickup_items_original; em_effect_manager_aura_draw is its w_draw worker.
 *
 * Every float operation goes through game/em_ee_float.h (docs/EE_FLOAT_MODEL.md);
 * floats cross this API as raw binary32 bit patterns (uint32_t).
 *
 * Verified by tools/test_effect_manager_reference.py: a Python MIPS/VU0
 * oracle executes the ORIGINAL instructions over captured route RAM and
 * compares every written byte, every packet byte and every worker call.
 *
 * Everything outside the modelled bytes is an explicit worker named by its
 * original address. A reached NULL worker or view, a negative worker result,
 * an address outside the loaded ELF windows, or an input the float model has
 * not measured latches a fault (fail-stop) and the call returns -1; a latched
 * fault makes every later call return -1.
 *
 * stdint only; no dependency on any port subsystem. Not wired: the
 * coordinator binds it (docs/EFFECT_MANAGER.md, "Binding").
 */
#ifndef EM_EFFECT_MANAGER_H
#define EM_EFFECT_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_effect_original.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_EFFECT_MANAGER_CHAIN 0x007635C0u     /* D_007635C0 = D_0028F700 + 0x4D3EC0 */
#define EM_EFFECT_MANAGER_ENTITY_BASE 0x007709C0u /* D_007709C0: 0x80 records of 0x90 */
#define EM_EFFECT_MANAGER_ENTITIES 0x80
#define EM_EFFECT_MANAGER_ENTITY_STRIDE 0x90u
#define EM_EFFECT_MANAGER_SCRATCH_36A0 0x700036A0u /* 001F3E30 a0 in 001F40C0 */
#define EM_EFFECT_MANAGER_MICROCODE 0x00233290u /* D_00233290: 001CB760 a2 in 001F0720 */
#define EM_EFFECT_MANAGER_TABLE_D25D270 0x0025D270u /* 001F6BB0 slot 0 record */
#define EM_EFFECT_MANAGER_TABLE_D25D2C0 0x0025D2C0u /* 001F6BB0 slot 1 record */

/* Fault codes: numerically those of em_effect_original.h. */
enum {
    EM_EFFECT_MANAGER_FAULT_NONE = 0,
    EM_EFFECT_MANAGER_FAULT_NULL_WORKER = 1,   /* reached worker, view or storage is NULL */
    EM_EFFECT_MANAGER_FAULT_WORKER_FAILED = 2, /* worker returned a negative value */
    EM_EFFECT_MANAGER_FAULT_BAD_RESULT = 3,    /* worker result the original cannot produce */
    EM_EFFECT_MANAGER_FAULT_BAD_INDEX = 4,     /* address outside the loaded windows / arrays */
    EM_EFFECT_MANAGER_FAULT_UNMEASURED = 5     /* float form outside the model, non-finite clip input */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_EFFECT_MANAGER_FAULT_* */
} EmEffectManagerFault;

/* Static ELF data windows (identical in the ELF and in every route capture;
 * the reference test asserts that). Any read outside them faults BAD_INDEX. */
#define EM_EFFECT_MANAGER_PAL_BASE 0x00259CD0u    /* D_00259CD0: 001F0720 colour records */
#define EM_EFFECT_MANAGER_PAL_BYTES 0x100u
#define EM_EFFECT_MANAGER_AURA_BASE 0x00259DD0u   /* D_00259DD0/EE0/F90, D_0025A040: 0x2C records */
#define EM_EFFECT_MANAGER_AURA_BYTES 0x320u
#define EM_EFFECT_MANAGER_KIND_BASE 0x0025A350u   /* D_0025A350: 001F40C0 records (0x60) */
#define EM_EFFECT_MANAGER_KIND_BYTES 0xA20u       /* up to D_0025AD70 */
#define EM_EFFECT_MANAGER_LIST_BASE 0x0025CA40u   /* D_0025CA40 keys + the 001F5CA0 lists */
#define EM_EFFECT_MANAGER_LIST_BYTES 0x4B0u       /* up to 0x25CEF0 */
#define EM_EFFECT_MANAGER_COLOUR_BASE 0x0026EB20u /* D_0026EB20 rows + D_0026EB60 */
#define EM_EFFECT_MANAGER_COLOUR_BYTES 0x50u

typedef struct {
    uint8_t pal[EM_EFFECT_MANAGER_PAL_BYTES];
    uint8_t aura[EM_EFFECT_MANAGER_AURA_BYTES];
    uint8_t kind[EM_EFFECT_MANAGER_KIND_BYTES];
    uint8_t list[EM_EFFECT_MANAGER_LIST_BYTES];
    uint8_t colour[EM_EFFECT_MANAGER_COLOUR_BYTES];
} EmEffectManagerTables;

/* Globals and scratchpad words the functions read or write. */
typedef struct {
    uint8_t d810700;         /* area */
    uint8_t d810701;         /* room */
    uint8_t d810702;         /* sub-mode (001F6BB0, 001F6EB0) */
    uint8_t d81075D;         /* 001F6BB0 key 0 guard */
    uint8_t d810778;         /* 001F6BB0 slot 0 guards */
    uint8_t d81077B;
    uint8_t d81079E;         /* 001F6BB0 slot 1 guard */
    int32_t d25D524;         /* 001F6EB0 area 7 track word (read) */
    int32_t d25D6E4;         /* 001F6EB0 area 0x12 track word (read) */
    int32_t d275C44;         /* 001F40C0: decremented once per call */
    uint32_t d28A59C;        /* 001F6210: 001C6120 a0 (read) */
    uint32_t d275670;        /* D_00275670: the render context address (001CAAC0 a2) */
    uint32_t list_cursor;    /* *(D_00275670 + 0x1C): 001F6210's display-list cursor */
    uint32_t spad3400[32];   /* 0x70003400..0x7000347F (001F6210, 001F0A60) */
    uint32_t spad3600[4];    /* 0x70003600..0x7000360F (001F0A60) */
    uint32_t spad3A20;       /* 0x70003A20 (001F0720) */
} EmEffectManagerGlobals;

/* View data (raw bits). fog is re-read after every w_0021B9A0 call, which
 * must keep it current (0021B9A0 rewrites D_00275670 + 0xA0). */
typedef struct {
    uint32_t clip0[16];  /* D_00275670 + 0x2240 = 001CD370(0) */
    uint32_t clip2[16];  /* D_00275670 + 0x22C0 = 001CD370(2) */
    uint32_t fog[4];     /* D_00275670 + 0xA0 */
    uint32_t camera[16]; /* scratchpad 0x70003AC0 */
    uint32_t screen[16]; /* scratchpad 0x70003A40 */
} EmEffectManagerView;

/* The two halfwords 001F40C0 tests in each D_007709C0 record. */
typedef struct {
    int16_t live;  /* +0x80: 0 = live */
    int16_t kind;  /* +0x82: D_0025A350 record index */
} EmEffectManagerEntity;

/* Workers, one per original callee. All return >= 0 on success; < 0 is a
 * port failure (fault). Float arguments are raw bits. */
typedef struct {
    void *ctx;
    /* 001F5C20 (lane L27): the barrel's second call. */
    int (*w_001F5C20)(void *ctx);
    /* 001F6210 */
    int (*w_001F5CA0)(void *ctx, uint32_t *list);          /* L27: list address or 0 */
    int (*w_001D8C20)(void *ctx, int32_t mode);
    int (*w_001C6120)(void *ctx, uint32_t bank, int32_t id, uint32_t *handle);
    int (*w_00122BB8)(void *ctx, int32_t *value);          /* SDK rand */
    /* May advance globals->list_cursor (the original re-reads D_00275670 +0x1C). */
    int (*w_001D3D90)(void *ctx, uint32_t handle);
    int (*w_001CAAC0)(void *ctx, const uint32_t position[4], uint32_t cursor, uint32_t context);
    /* Writable display-list bytes at original address..address+size, or NULL. */
    uint8_t *(*w_list_at)(void *ctx, uint32_t address, uint32_t size);
    /* 001F6BB0 */
    int (*w_001F6760)(void *ctx, uint32_t *record);
    int (*w_001F66F0)(void *ctx, uint32_t record);
    int (*w_001F6850)(void *ctx);
    int (*w_001F6640)(void *ctx, uint32_t record);
    int (*w_word)(void *ctx, uint32_t address, uint32_t *value); /* 001F6AC0's one load */
    /* 001F6EB0 (a0 = D_00810702, a1 = D_00810700 << 8, as the registers hold) */
    int (*w_001F6E40)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001F6E80)(void *ctx, uint32_t a0, uint32_t a1);
    /* 001F40C0. 001F3620 may change entity->live and entity->kind. */
    int (*w_001F3620)(void *ctx, uint32_t entity_address, int32_t kind, EmEffectManagerEntity *entity);
    int (*w_001F3E30)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t t0);
    /* Packet chain: 001CB5F0 opens `count` quadwords (*out = count*16 writable bytes). */
    int (*w_001CB5F0)(void *ctx, uint32_t chain, int32_t id, int32_t count, uint8_t **out);
    int (*w_001CB760)(void *ctx, uint32_t chain, int32_t id, uint32_t address);
    int (*w_001CB900)(void *ctx, uint32_t chain, int32_t id, int32_t mode);
    /* 0021B9A0(mode, f12, f13): the fog / depth-range programmer. */
    int (*w_0021B9A0)(void *ctx, int32_t mode, uint32_t f12, uint32_t f13);
    /* 0011E2A8: SDK sinf. */
    int (*w_0011E2A8)(void *ctx, uint32_t x, uint32_t *result);
    /* 001CD520(a0, a1, position, tex0, rgb, f12, f13, f14). */
    int (*w_001CD520)(void *ctx, int32_t a0, int32_t a1, uint32_t position, uint64_t tex0,
                      uint32_t rgb, uint32_t f12, uint32_t f13, uint32_t f14);
} EmEffectManagerWorkers;

typedef struct {
    const EmEffectManagerTables *tables;
    EmEffectManagerGlobals *globals;
    EmEffectOriginalDecals *decals;   /* the 001F0460 ring (shared with em_effect_original) */
    EmEffectManagerView *view;
    EmEffectManagerEntity *entities;  /* [EM_EFFECT_MANAGER_ENTITIES] */
    const EmEffectManagerWorkers *workers;
    EmEffectManagerFault fault;
} EmEffectManager;

/* Loads the windows from the user's boot ELF (SCUS_971.12, 1532624 bytes). */
int em_effect_manager_load_tables(const uint8_t *elf, size_t size, EmEffectManagerTables *out);

/* Each returns 0, or -1 on a fault. */
int em_effect_manager_001F0360(EmEffectManager *m);
int em_effect_manager_001F6210(EmEffectManager *m);
int em_effect_manager_001F6BB0(EmEffectManager *m);
int em_effect_manager_001F6EB0(EmEffectManager *m);
int em_effect_manager_001F40C0(EmEffectManager *m);
int em_effect_manager_001F0720(EmEffectManager *m, int32_t n);

/* 001F0A60(a0, a1, pos, a3, t0, f12, f13, f14): pos is the a2 quadword. */
int em_effect_manager_001F0A60(EmEffectManager *m, int32_t a0, int32_t mode, const uint32_t pos[4],
                               uint32_t colour_a3, uint32_t colour_t0, uint32_t f12, uint32_t f13,
                               uint32_t f14);

/* 001F4D40(position, colour, f12, f13): position is the a0 address handed
 * to 001CD520; colour holds the four words at a1 +0, +4, +8, +0xC. */
int em_effect_manager_001F4D40(EmEffectManager *m, uint32_t position, const uint32_t colour[4],
                               uint32_t f12, uint32_t f13);

/* The draw block of 001F1180 (em_pickup_items_original's w_draw): record is
 * the selected sprite record address, angle / timer the aura's +0x2D0 /
 * +0x2D4 before the step's ramps, owner_d0 the owner's +0xD0 matrix. */
int em_effect_manager_aura_draw(EmEffectManager *m, const uint32_t owner_d0[16], uint32_t record,
                                uint32_t angle, uint32_t timer);

#ifdef __cplusplus
}
#endif

#endif /* EM_EFFECT_MANAGER_H */
