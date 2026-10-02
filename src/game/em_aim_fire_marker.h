/* em_aim_fire_marker.h - the round's impact marker: the pool behaviour
 * 0018ABA0 a shot (001861C0) gives every record it allocates for a hit
 * (AIM_FIRE.md section 7).
 *
 * 0018ABA0(node), by its state +4:
 *   0  +0xA = 0; +0xCC == 0.0: +0xA0 = 0, +0xA4 = 001B1470(pi + D_00810374),
 *      +0xA8 = 0, +0xAC = 1.0; else +0xA0 = the quadword +0xC0 (00102948);
 *      then +0xAC = 0 when +0xD == 2, else 1.0; +4 + 1, +0x36 = 1, +0x28 = 4,
 *      +0 = 1, +0x30 = &D_002754E0.
 *   1  when +5 == 1 and +0xD != 0: +0xA set: 001EFD90(0x80000026, +0xB0,
 *      +0xA0); else by +0x2E: no 0x300 bit: 001EFD20(0x80000019, +0xB0),
 *      001F00A0(0x80000060, +0xB0, +0xA0, +0x2E & 1) and (+0xD != 2) a
 *      ricochet sound (0x18A / 0x18B with bit 0x10, else 0x188 / 0x189, by
 *      bit 12 of rand); bit 0x100: 001F00A0(0x80000003, ...) and 0x188 /
 *      0x189; bit 0x200 only: 001F00A0(0x80000060, ...) and 0x18A / 0x18B;
 *      each sound 001FBD50(node, id, 0, 300.0). Then +5 + 1; the halfword
 *      +0x28 counts down (when it was 0: +0 = 2, +4 + 1); +0xA = 0;
 *      001B17A0(node).
 *   2, 3  001AFC10(node) (the free).
 * Read from the original instructions (the decomp's C is byte-matched).
 *
 * Memory by original address through the EmAimFireTarget host (map / call /
 * store), as em_aim_fire_target; every callee goes through `call`. Fail-stop
 * as there. Verified by tools/test_aim_fire_marker_reference.py.
 *
 * Bound (the only aim / fire path since 2026-10-02, AIM_FIRE.md section
 * 10): em_aim_fire_live dispatches 0x0018ABA0,
 * and em_aim_fire_runtime allocates the record (001861C0's 001AFA90),
 * holds its +0x28 / +0xA0..+0xAF and binds it to the AREA11 pool walk by
 * its +0x10 (AIM_FIRE.md section 3). */
#ifndef EM_AIM_FIRE_MARKER_H
#define EM_AIM_FIRE_MARKER_H

#include <stdint.h>

#include "game/em_aim_fire_target.h"

#ifdef __cplusplus
extern "C" {
#endif

int em_aim_fire_marker_0018ABA0(EmAimFireTarget *h, uint32_t node);

#ifdef __cplusplus
}
#endif

#endif /* EM_AIM_FIRE_MARKER_H */
