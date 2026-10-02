#ifndef EM_AIM_FIRE_LAMP_H
#define EM_AIM_FIRE_LAMP_H
#include <stdint.h>

#include "game/em_aim_fire_target.h"

/* The gun lamp (docs/AIM_FIRE.md section 10): 00188ED0's 00187780 while
 * D_008106C7 is set and the player is in an armed stance. Translated from
 * the original instructions (the decomp's C of 00187780 and 001D91A0 is
 * NEARMISS; tools/test_aim_fire_lamp_reference.py executes the originals):
 *
 *   00187780  the lamp matrix (the bone's +0x90..+0xBF, a quarter turn,
 *             the node's +0xB0), the 250-unit probe 0019A570(.., 6, 0),
 *             the cone length and scale, the view attenuation (mode 0),
 *             0021B9A0, the flare 00187690 (twice in mode 1) and, unless
 *             001B0070() has 0x20000000, the cone 001D9530
 *   00187690  the flare's source block D_002487E0 (its rows written here)
 *             through 001CCF70 / 001CFA60 / 001CFBE0(kind 1)
 *   001D9530  the cone: library models 0x10 / 0x11 / 0x16 (D_0028A56C),
 *             the scale 650 / 120, 001DA290(3, 0), three 001D91A0 shells,
 *             the RET tag and 001CB760(D_007635C0, 0, list, context)
 *   001D91A0  one shell on channel 3: the matrix uploads, the GS preset
 *             (2, 9) or (2, 5), the level kernel and the guard-band clip
 *             kernel over the model, the colour (00128250 per lane)
 *   001DA290  001D1F80(a0, 2, 9), then 001DA1E0(a0, a copy of D_002531D0, a1)
 *   001DA1E0  the DIRECT alpha-clear strip; returns the packet's data address
 *   001D4E20 / 001D4EB0   001D4750(a0), 001D2090(a0, 0x00237180), the
 *             template REF D_00816640 / D_00816740 + (context +0x9C << 7)
 *   001D4B80 / 001D4C30   001D4750(a0), 001D2090(a0, 0x00239C90), the same
 *             template REF, then 001D4A90(a0, a1)
 *
 * Every callee goes through the host's `call` (the SDK leaves, 0019A570,
 * the render context's 001D1F80 / 001D7080 / 001D4750 / 001D2090 /
 * 001D4F30 / 001D4A90, 001C6120, 001CB760, the effect writers, and these
 * functions' calls of each other). All pointers are original addresses;
 * the stack frames sit at host sp - frame as the originals' (the callees
 * receive their addresses). Register saves have no native representation.
 * 0, or -1 on a latched fault (the host's fault fields). */
int em_aim_fire_lamp_00187780(EmAimFireTarget *h, uint32_t node, int32_t a1, int32_t a2);
int em_aim_fire_lamp_00187690(EmAimFireTarget *h, uint32_t matrix, uint32_t b, uint32_t c, int32_t size,
                              uint64_t word, uint32_t f12, uint32_t f13, uint32_t f14);
int em_aim_fire_lamp_001D9530(EmAimFireTarget *h, uint32_t matrix);
int em_aim_fire_lamp_001D91A0(EmAimFireTarget *h, uint32_t matrix, uint32_t colour, uint32_t model, int32_t flag,
                              uint32_t f12);
int em_aim_fire_lamp_001DA290(EmAimFireTarget *h, int32_t a0, int32_t a1);
int em_aim_fire_lamp_001DA1E0(EmAimFireTarget *h, int32_t a0, uint32_t rows, int32_t a2, uint32_t *result);
int em_aim_fire_lamp_001D4E20(EmAimFireTarget *h, int32_t a0);
int em_aim_fire_lamp_001D4EB0(EmAimFireTarget *h, int32_t a0);
int em_aim_fire_lamp_001D4B80(EmAimFireTarget *h, int32_t a0, uint32_t a1);
int em_aim_fire_lamp_001D4C30(EmAimFireTarget *h, int32_t a0, uint32_t a1);
#endif
