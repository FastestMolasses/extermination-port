/* em_area01_math_actor.h - AREA01 lane "math": the actor helpers the
 * original behaviour 00128C10 (and its siblings 0012A5D0, 00129FC0, ...)
 * calls every frame for each of its nodes (docs/AREA01_MATH.md).
 *
 * Hand translations of these original functions (boot ELF SCUS-97112):
 *   001B13F0  001028D0(0x70003600, a0, a1), then 0011E748 of the squared
 *             length of the three result words; 0 when the float argument
 *             is below that root, else 1
 *   001B2140  a table of per-area tests on the area bytes D_00810700..02
 *             against the node's bytes +0x9D / +0x9E (0 when D_008104E0 is
 *             0x11)
 *   001C25E0  two vectors built in SPR 0x700038B0..DF with 001026A0 /
 *             001028B8, then 0019B4C0(node, 0x700038C0, 0x700038D0, 6)
 *   001C2770  per-frame state machine on a state block (+0xE4: phase and
 *             sub-state): phase 0 runs 001C39F0 and 001C2540 / 001C2690 /
 *             0019AB20 calls whose hit record (SPR word 0x700031D0) flags
 *             choose the next phase; phases 1..6 advance a 001C9D50 blend
 *   001C39F0  node +0xB0..B8 += 001026A0(node + 0xD0, (side, 0, f12, 1));
 *             modes other than 0 / 4 also update the state's +0xDC and
 *             subtract half of its +0x80..88 (called by 001C2770)
 *   001C3BE0  SPR 0x70003000 matrix from the state block's +0x70 / +0x80
 *             vectors (00102718) and the node's +0xC0 / +0xC4 angles
 *             (00102B08 / 00102BB0, 001026D0) (called by 001C2770, 001C3D60)
 *   001C3D60  001C3BE0, then copy_qw4(node + 0xD0, 0x70003000) and
 *             001031E0(0x70003030, node + 0xB0)
 *   001C69A0  per-bone matrices of a model record (VU0 macro code): each
 *             bone's +0x90 matrix from its keys, rest pose and parent
 * Names are addresses; a role is stated only where the original code shows
 * it. Every routine works on EE addresses through em_area01_math_core.h and
 * reaches its callees through the dispatcher (the callee list of each
 * routine is in its comment in the .c file). 001C2770 calls the native
 * 001C39F0 and 001C3BE0 directly, 001C3D60 the native 001C3BE0.
 *
 * Each returns 0, or -1 when a fault is latched (em_area01_math_core.h).
 * Integer results go to *v0. Float arguments are raw EE bit patterns. */
#ifndef EM_AREA01_MATH_ACTOR_H
#define EM_AREA01_MATH_ACTOR_H

#include <stdint.h>

#include "game/em_area01_math_core.h"

#ifdef __cplusplus
extern "C" {
#endif

int em_area01_math_001B13F0(EmA01Math *m, uint32_t a0, uint32_t a1, uint32_t f12, uint32_t *v0);
int em_area01_math_001B2140(EmA01Math *m, uint32_t node, uint32_t *v0);
int em_area01_math_001C25E0(EmA01Math *m, uint32_t node, uint32_t a1);
int em_area01_math_001C2770(EmA01Math *m, uint32_t node, uint32_t state, uint32_t flags, uint32_t *v0);
int em_area01_math_001C39F0(EmA01Math *m, uint32_t node, uint32_t state, uint32_t f12);
int em_area01_math_001C3BE0(EmA01Math *m, uint32_t node, uint32_t state);
int em_area01_math_001C3D60(EmA01Math *m, uint32_t node, uint32_t state);
int em_area01_math_001C69A0(EmA01Math *m, uint32_t model);
/* The same C69A0 matrix stages, for typed hosts whose channel evaluator
 * already owns the blended quaternion. root scales/stores 70003400;
 * bone consumes that root and the quaternion already at 70003600, then
 * performs the original post-nlerp work and stores bone +90. These are
 * stages of the one translation above, not additional original entries. */
int em_area01_math_001C69A0_root(EmA01Math *m, uint32_t model);
int em_area01_math_001C69A0_bone(EmA01Math *m, uint32_t model, uint32_t bone);

#ifdef __cplusplus
}
#endif

#endif
