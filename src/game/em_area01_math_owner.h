/* em_area01_math_owner.h - AREA01 lane "math": pool-node behaviours and
 * script-op routines of the AREA01 route (docs/AREA01_MATH.md).
 *
 * Hand translations of these original functions (boot ELF SCUS-97112):
 *   001BB860  node behaviour (+0x10) of two AREA01 sub-0 nodes: state byte
 *             +4 / sub-state +5 machine, 001BB560 on sub-state 0, then the
 *             per-frame tail and a distance-20 test against D_00810350..58
 *   001BB560  called by 001BB860: side selection from the player record
 *             D_008102B0 (+0xA0 / +0xA8 / +0xC4), the point SPR
 *             0x700038A0 handed to 00182F90, 001BBD60 / 001BA1A0 calls, the
 *             D_008106C8 bit 0x80 per the D_0024E140 table (area 0x16 only)
 *   001C02E0  node behaviour: state 0 spawns a companion node (001AFA90)
 *             with behaviour 001BFFD0 and builds its tail vectors; state 1
 *             runs 001BF630 and the sound / anim steps
 *   001BF630  called by 001C02E0: 1 when the scratchpad byte 0x70003B8D is
 *             0 and the x/z distance of the player record to the node is at
 *             most the float the tail's +0x18 pointer points at
 *   001BFFD0  node behaviour of the companion node (the decomp's
 *             001BFFD0 + 001C0004 pieces, one routine)
 *   001CB360  four calls: 001C7420(n, 0x3F5, 0), 001CB2C0(n, 0x3F3, 0),
 *             001D1F80(0, 1, 0), then 001D3F50(*(n + 0x44)) as a tail call
 *   001B9CF0  script op: steps one angle / position lane of the node (or
 *             D_00810354[n]) toward a target with 001B12B0; 1 when reached
 *   001BBAE0  script op: issues the request block D_002821B0..BC once
 *             (latched in st[4]) and polls D_002821B4 == 2
 *   001BBBF0  script op: two points around the node into SPR 0x700038A0,
 *             each copied by 00102948 to D_008105E0 / D_008105D0; returns 1
 * Names are addresses; roles are stated only where the original code shows
 * them. 001BB860 calls the native 001BB560, 001C02E0 the native 001BF630.
 *
 * Each returns 0, or -1 when a fault is latched (em_area01_math_core.h).
 * Integer results go to *v0. */
#ifndef EM_AREA01_MATH_OWNER_H
#define EM_AREA01_MATH_OWNER_H

#include <stdint.h>

#include "game/em_area01_math_core.h"

#ifdef __cplusplus
extern "C" {
#endif

int em_area01_math_001BB860(EmA01Math *m, uint32_t node);
int em_area01_math_001BB560(EmA01Math *m, uint32_t node, uint32_t a1, uint32_t a2, uint32_t *v0);
int em_area01_math_001C02E0(EmA01Math *m, uint32_t node);
int em_area01_math_001BF630(EmA01Math *m, uint32_t player, uint32_t node, uint32_t tail, uint32_t *v0);
int em_area01_math_001BFFD0(EmA01Math *m, uint32_t node);
int em_area01_math_001CB360(EmA01Math *m, uint32_t node);
int em_area01_math_001B9CF0(EmA01Math *m, uint32_t node, uint32_t a1, uint32_t op, uint32_t *v0);
int em_area01_math_001BBAE0(EmA01Math *m, uint32_t node, uint32_t st, uint32_t *v0);
int em_area01_math_001BBBF0(EmA01Math *m, uint32_t node, uint32_t *v0);

#ifdef __cplusplus
}
#endif

#endif
