#ifndef EM_AIM_FIRE_LEAVES_H
#define EM_AIM_FIRE_LEAVES_H
#include <stdint.h>
/* Original type-byte classification and positive-angle reduction. */
uint32_t em_aim_fire_001839A0(uint8_t kind);
uint32_t em_aim_fire_001B1510(uint32_t angle);
/* 001AA7A0(a0, e) (decomp func_001AA7A0.c, byte-matched; a0 is not read):
 * the knife's reach over a class-4 entry, called by 001AA840 with `node` the
 * bone record *D_00275B40 (its +0xC0 / +0xC4 / +0xC8 world translation) and
 * `pos` the entry's +0xB0..+0xB8 (binary32 bits). dx, dz = the node's x / z
 * less the entry's; the result is 0 when ACC = dx * dx (MULA.S), dx*dx +
 * dz * dz (MADD.S) is not <= 25.0, or the node's y is not <= the entry's y,
 * or the node's y < the entry's y - 45.0; otherwise 1, and the caller stores
 * the halfword +0x36 = 1 (*hit). */
uint32_t em_aim_fire_001AA7A0(const uint32_t node_c0[3], const uint32_t pos[3], int *hit);
#endif
