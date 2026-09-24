/* em_player_equipment_sprite.h - 001CD520, the depth-faded billboard
 * sprite. Docs: docs/PLAYER_EQUIPMENT.md section 5.
 *
 * Hand translation of 001CD520, read from its split listing (the decomp C is
 * a NEARMISS at 21.9%; this translation follows the instructions, not that
 * C, and differs from it where the C is wrong: the second scratchpad
 * quadword store writes all four words of 0x70003610..0x7000361C).
 *
 * What it does: culls a world point against the 001CD370(0) matrix, projects
 * it with the 0x70003AC0 matrix into 28.4 fixed point at 0x70003600..0C
 * (x, y, z, fog), projects the half-extent (0.5 w, 0.5 h) at the point's
 * depth with the 0x70003A40 matrix into 0x70003610/14, fades the colour by
 * the fog term (mode 1: alpha; modes 2..4: RGB, fog word pinned to 0xFF0),
 * then writes one six-quadword sprite primitive into the packet 001CB5F0
 * opens on the chain D_0028F700 + (bucket << 15) + 0x4D3EC0, and closes it
 * with 001CB6B0(chain, z, 2, D_00251220) and 001CB900(chain, z, mode). It
 * returns the 28.4 depth word, or 0x00FFFFFF when culled (then nothing is
 * written).
 *
 * Arithmetic: every VU0 macro instruction goes through em_ee_float.h under
 * its real form; the one COP1 multiply (0.5 * w, 0.5 * h) too. The clip test
 * (a vclipw) is not part of the measured float model: as in
 * em_effect_original.c (001CCF70), it is evaluated on DAZ'd finite
 * operands, and a non-finite operand faults (EM_PLAYER_EQUIPMENT_FAULT_UNMEASURED).
 *
 * Oracle: tools/test_player_equipment_reference.py (the sprite part)
 * executes 001CD520's original instructions and compares the scratchpad
 * words, every packet byte, the worker calls and the return value. */
#ifndef EM_PLAYER_EQUIPMENT_SPRITE_H
#define EM_PLAYER_EQUIPMENT_SPRITE_H

#include <stdint.h>

#include "game/em_player_equipment.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_PLAYER_EQUIPMENT_FAULT_UNMEASURED 6  /* a float form outside the measured model */
#define EM_PLAYER_EQUIPMENT_SPRITE_CULLED 0x00FFFFFF
#define EM_PLAYER_EQUIPMENT_SPRITE_CHAIN_BASE 0x007635C0u /* D_0028F700 + 0x4D3EC0 */
#define EM_PLAYER_EQUIPMENT_SPRITE_STATE 0x00251220u      /* D_00251220: 001CB6B0 a3 */
#define EM_PLAYER_EQUIPMENT_SPRITE_PACKET_BYTES 0x60u     /* 001CB5F0(chain, z, 6) */

typedef struct {
    const uint32_t *fog;   /* *(D_00275670) + 0xA0: the four fog words */
    const uint32_t *s3A40; /* 0x70003A40: the projection matrix (16 words) */
    const uint32_t *s3AC0; /* 0x70003AC0: the world-to-screen matrix (16 words) */
    uint32_t *s3600;       /* 0x70003600..0x7000361F (8 words) */
} EmPlayerEquipmentSpriteWorld;

typedef struct {
    void *ctx;
    /* 001CD370(a0): the 16 words at D_00275670 + a0 * 0x40 + 0x2240. */
    int (*w_001CD370)(void *ctx, int32_t a0, uint32_t m[16]);
    /* 001CB5F0(chain, z, count): opens `count` quadwords; *packet receives
     * the primitive's first byte (count * 16 bytes writable). */
    int (*w_001CB5F0)(void *ctx, uint32_t chain, int32_t z, int32_t count, uint8_t **packet);
    int (*w_001CB6B0)(void *ctx, uint32_t chain, int32_t z, int32_t kind, uint32_t address);
    int (*w_001CB900)(void *ctx, uint32_t chain, int32_t z, int32_t mode);
} EmPlayerEquipmentSpriteWorkers;

typedef struct {
    const EmPlayerEquipmentSpriteWorkers *workers;
    const EmPlayerEquipmentSpriteWorld *world;
    EmPlayerEquipmentFault *fault;
} EmPlayerEquipmentSprite;

/* 001CD520(bucket, mode, point, giftag, f12 = w, f13 = h, f14 = zbias,
 * t0 = rgba). `point` is the 16 bytes at a2 (its w word is not used);
 * floats are raw bits; rgba is the whole t0 register (only its low 32 bits
 * reach any result). *result is the return value. 0, or -1 on a fault. */
int em_player_equipment_001CD520(const EmPlayerEquipmentSprite *s, int32_t bucket, int32_t mode,
                                 const uint32_t point[4], uint64_t giftag, uint32_t w, uint32_t h,
                                 uint32_t zbias, uint64_t rgba, int32_t *result);

#ifdef __cplusplus
}
#endif

#endif /* EM_PLAYER_EQUIPMENT_SPRITE_H */
