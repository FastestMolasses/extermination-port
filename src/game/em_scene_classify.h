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

/* ---- Lead decision Q1 (design section 9), NOT part of 001AE7E0 ----
 *
 * Until 0022A650 is ported, SELECT (0x100) is withheld from the E74 word the
 * classifier reads, so the r == 1 arm (0x1AE040 state 2 -> 0022A650) is not
 * entered by a SELECT press. The canonical D_00810E74 is NOT modified: other
 * original readers (e.g. 001AE6B0's E74 & 0x900 test) still see SELECT.
 * This does not mask E50 != 4, which still returns 1 (S11a writes E50 = 4).
 * Remove these helpers in the step that ports 0022A650. */
#define EM_SCENE_Q1_SELECT 0x0100u
#define EM_SCENE_Q1_UNPORTED_MESSAGE "unported: 0022A650 (SELECT)"

/* The E74 value the classifier sees under Q1. */
static inline uint16_t em_scene_q1_classifier_e74(uint16_t d810E74)
{
    return (uint16_t)(d810E74 & (uint16_t)~EM_SCENE_Q1_SELECT);
}

/* em_sf_001AE7E0 over a copy of `s` whose E74 has SELECT withheld. When
 * `select_withheld` is non-NULL it receives 1 if SELECT was set in the
 * canonical E74 (the caller logs EM_SCENE_Q1_UNPORTED_MESSAGE), else 0. */
int em_scene_classify_q1(const EmSceneState *s, int16_t d0028A9A0, int *select_withheld);

#ifdef __cplusplus
}
#endif

#endif /* EM_SCENE_CLASSIFY_H */
