/* Scene frame classifier: translation of the byte-matched original 001AE7E0
 * (Extermination/src/func_001AE7E0.c), called by 0x1AE040 state 1
 * (SCENE_COORDINATOR_DESIGN.md sections 2.3 and 3.1).
 *
 * Verified by tools/test_scene_classify_reference.py, which executes the
 * original ELF instructions at 0x1AE7E0 over the design's full input product.
 */
#ifndef EM_SCENE_CLASSIFY_H
#define EM_SCENE_CLASSIFY_H

#include <stdint.h>

#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Faithful 001AE7E0. Reads only B8, B9, CE, C5, B0, B3, spad 3B8D, D_00810E74
 * and D_00810E50 from `s`, plus D_0028A9A0, which the coordinator does not own
 * (design 3.2) and therefore arrives as the halfword the caller read through
 * the r_0028A9A0 reader. Writes nothing. Returns the original 0..3:
 *   B8 -> 0, B9 -> 0, CE -> 3, C5|B0 -> 2, D_0028A9A0 -> 0, 3B8D -> 0,
 *   (E74 & 0x100) | (E50 != 4) -> 1, B3 -> 0, (E74 & 0x800) | (E74 & 0x10) -> 2,
 *   else 0. */
int em_sf_001AE7E0(const EmSceneState *s, int16_t d0028A9A0);

#ifdef __cplusplus
}
#endif

#endif /* EM_SCENE_CLASSIFY_H */
