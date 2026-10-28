/* em_player_equipment_variants.h - the equipment nodes' variant routines
 * that 00188A50 (flavour 1, +0x0D 0x15) and 00188DF0 (flavour 2, +0x0D 1,
 * 2, 3, 0xC) hand a node to. Docs: docs/PLAYER_EQUIPMENT.md section 4.7.
 *
 * Hand translation of the byte-matched decomp C of:
 *   00188C70  flavour 1 variant 0x15: the player's bone 4 (or, in a window
 *             of the 0x33 action's clip, bone 19) world translation into
 *             the work bone 0, with the +0x01 clear and the 001F4010(9)
 *             seed at clip time 39.0; D_008106CC != 0 frees the node
 *             (+0x04 = 3)
 *   00189090  flavour 2 variant 1: the work bones from the player's bone
 *             4, then a four-state recoil on +0x05 (001FBD50 0x5DF, the
 *             001F4010(7) seed, the D_0024A360 ramp on work bone 1)
 *   00189330  flavour 2 variant 2: the same bone copy, then a six-state
 *             push on +0x05 (the 001F4010(8) seed, a +0.1 / -0.1 ramp
 *             added to work bone 1 through 001028B8)
 *   001899C0  flavour 2 variant 3: 00189630 and 00189730 (its own two
 *             state machines on +0x06 and +0x05, compiled in its unit),
 *             then the +0xD0 placement and 001C9610
 *   00189A20  flavour 2 variant 0xC: the +0xD0 placement and 001C9610,
 *             then a six-state ramp of +-0.1573875 on +0x05 with the
 *             +0x28 countdown
 * The node's +0xD0 copy (copy_qw4, 00102958) is done inline.
 *
 * Record: the node is the EmPlayerEquipmentNode of em_player_equipment.h;
 * the bytes only these routines use are EmPlayerEquipmentVariantBytes
 * (+0x06, +0x2A, +0x38, +0x70..+0x7F), each at its original offset.
 *
 * D_00275B40 is the node's own bone array (the walk sets it to +0x110):
 * `bones[i]` is *(D_00275B40 + 4 i); a NULL slot the original dereferences
 * is a fault. Its +0x70 / +0x74 words are EmOwnerBone.rot[0] / rot[1], its
 * +0x90..+0xCF the world matrix (+0xC0..+0xCC its row 3).
 *
 * Workers: 001026A0 (v x M) and 001028B8 (a + b) take their inputs by value
 * and return what the translation stores at the original destination, as
 * in em_player_equipment.h. 001F4010 receives the node's copy of
 * 0x700036A0..0x700036DF; 001FBD50 is the player's sound (a0 = D_008102B0).
 *
 * Arithmetic: every float add and compare is the EE COP1 form
 * (em_ee_float.h); the clip-time compares are "at most" (c.le) or "below"
 * (c.lt) as the C states them.
 *
 * Fail-stop: each entry checks the workers and views it can reach before
 * its first write; a negative worker result, a NULL bone slot or a table
 * index outside the view latches a fault (writes before it stay). A
 * latched fault makes every later call return -1.
 *
 * Oracle: tools/test_player_equipment_variants_reference.py executes these
 * routines' original instructions over synthetic records and compares every
 * written byte and worker call. */
#ifndef EM_PLAYER_EQUIPMENT_VARIANTS_H
#define EM_PLAYER_EQUIPMENT_VARIANTS_H

#include <stdint.h>

#include "game/em_player_equipment.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The node bytes only the variant routines read or write. */
typedef struct {
    uint8_t b06;      /* +0x06: 00189630's state */
    int16_t h2A;      /* +0x2A: 00189730's row (0 or 1) */
    uint32_t w38;     /* +0x38: the per-frame step (raw binary32) */
    uint32_t v70[4];  /* +0x70..+0x7F: the accumulator vector */
} EmPlayerEquipmentVariantBytes;

typedef struct {
    void *ctx;
    int (*w_001026A0)(void *ctx, const uint32_t m[16], const uint32_t v[4], uint32_t out[4]); /* v x M */
    int (*w_001028B8)(void *ctx, const uint32_t a[4], const uint32_t b[4], uint32_t out[4]);  /* a + b */
    int (*w_001C9610)(void *ctx, EmOwnerBone *const *bones, uint32_t bones_count, int32_t count,
                      const uint32_t world[16]);                 /* 001C9610(D_00275B40, n, +0xD0) */
    int (*w_001F4010)(void *ctx, int32_t index, const uint32_t *at); /* at = spad 0x700036A0 */
    int (*w_001FBD50)(void *ctx, int32_t id, int32_t a2, uint32_t f12); /* 001FBD50(D_008102B0, ...) */
    int (*w_00122BB8)(void *ctx, int32_t *value);                /* rand() */
} EmPlayerEquipmentVariantWorkers;

typedef struct {
    const uint8_t *player;              /* D_008102B0: the 0x320-byte player record image */
    const EmOwnerBone *player_bone4;    /* *(D_008103D0), the player's +0x120 slot */
    const EmOwnerBone *player_bone19;   /* *(D_008102B0 + 0x15C) */
    EmOwnerBone *const *bones;          /* D_00275B40: the node's +0x110 slots */
    uint32_t bones_count;
    const uint8_t *d008106CC;
    const uint8_t *d00810CA8;           /* the halfword D_00810CA8 (2 bytes, little endian) */
    const int16_t *d00248B98;           /* D_00248B98..D_00248BA7 (8 halfwords) */
    const int16_t *d00248C78;           /* D_00248C78..D_00248C87 (8 halfwords) */
    const uint32_t *table;              /* words from EM_PLAYER_EQUIPMENT_TABLE_BASE */
    uint32_t table_words;
    uint32_t *spad3600;                 /* 0x70003600..0x7000371F (0x48 words) */
    uint32_t *spad38A0;                 /* 0x700038A0..0x700038DF (0x10 words) */
    const int32_t *spad3B68;            /* 0x70003B68 */
} EmPlayerEquipmentVariantWorld;

typedef struct {
    const EmPlayerEquipmentVariantWorkers *workers;
    const EmPlayerEquipmentVariantWorld *world;
    EmPlayerEquipmentFault *fault;
} EmPlayerEquipmentVariants;

/* Each: 0, or -1 on a fault. `x` holds the node's bytes of
 * EmPlayerEquipmentVariantBytes (00188C70 uses none). */
int em_player_equipment_00188C70(const EmPlayerEquipmentVariants *e, EmPlayerEquipmentNode *node);
int em_player_equipment_00189090(const EmPlayerEquipmentVariants *e, EmPlayerEquipmentNode *node,
                                 EmPlayerEquipmentVariantBytes *x);
int em_player_equipment_00189330(const EmPlayerEquipmentVariants *e, EmPlayerEquipmentNode *node,
                                 EmPlayerEquipmentVariantBytes *x);
int em_player_equipment_001899C0(const EmPlayerEquipmentVariants *e, EmPlayerEquipmentNode *node,
                                 EmPlayerEquipmentVariantBytes *x);
int em_player_equipment_00189A20(const EmPlayerEquipmentVariants *e, EmPlayerEquipmentNode *node,
                                 EmPlayerEquipmentVariantBytes *x);

#ifdef __cplusplus
}
#endif

#endif /* EM_PLAYER_EQUIPMENT_VARIANTS_H */
