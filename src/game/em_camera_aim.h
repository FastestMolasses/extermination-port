/* em_camera_aim.h - the aim camera: camera actions 1, 2 and 5 of the
 * action dispatch 0018BC20 and the routines they own (docs/CAMERA_LIVE.md
 * section 7, docs/AIM_FIRE.md), and camera action 14 with its follow and
 * action 11's raised target (section 6), which address memory the same way.
 *
 * Translated here, read from the original instructions (the decomp's C is
 * byte-matched for 00197740, 00198240, 001912B0 and 001DB800 and NEARMISS
 * for the rest; it differs from the instructions where docs/CAMERA_LIVE.md
 * section 7 lists it):
 *
 *   00197D20  camera action 1: the R1 aim camera (player code 0xD / 0x2A)
 *   00198650  camera action 2: the R2 aim camera (player code 0xC / 0x29)
 *   0018CA90  camera action 5: the over-the-shoulder seat (then action 7)
 *   00197740  the R1 draw-in: eye and target from the published Euler
 *   00197870  the R1 hold: target along the gun's barrel, eye behind the
 *             anchor, the height clamps and the near-wall lift
 *   00198050  the R2 draw-in: eye and target from the player's heading
 *   00198440  the R2 hold: eye at the gun's bone offset, target along the
 *             barrel (or the laser dot +0x200 while +0x210 and D_008106C6)
 *   00198240  the gun-to-hip wall test and its push of the player (00183010)
 *   001912B0  the area-0x10 floor lift (a no-op in every other area)
 *   001999C0  the sight-system switch (by D_00810CA4, then D_00810CA7)
 *   001DB800  the sight bytes D_0081C040..43 cleared
 *   00198AF0  camera action 14 (player code 0x28): the follow, then the
 *             seat behind the player and the hand-back to action 0
 *   00198930  action 14's follow (eye behind +C4, target along 0x70003B50)
 *   00191530  action 11's raised target (+A0 + 17); action 11 itself
 *             (00198F10) and action 9 (00198CE0) are em_area00_low's
 *
 * 00198930 and 00191530 are byte-matched C in the decomp; 00198AF0 is
 * NEARMISS and read from the instructions (its sub-state 2 differs from
 * the NEARMISS C: see the translation).
 *
 * Calls between these are direct. Every other callee (the SDK leaves, the
 * camera solve 0018D7B0, the chases 0018C4B0 / 0018C6A0 / 0018C850 /
 * 0018C920, the aim release 00197490, the area clamp 00191210, the math,
 * the render-context helpers, the sight drawers) goes through `call` by
 * original address with the original's argument registers; the module never
 * simulates a callee.
 *
 * Memory: every read and store of original memory goes through `map` by
 * original address (the host maps each range to its owner's storage); after
 * each store `store` (optional) is told the range. Pointers held in memory
 * (the player's +0x20 gun, the gun's +0x110 bone word, *0x700031D0,
 * *0x700031D4) are original addresses and are resolved through `map` again.
 *
 * Fail-stop (the EmAimFireTarget contract): a missing map or call (code 1),
 * an unmapped range (2) or a failed callee (3) latches the fault with the
 * root entry and the address; nothing runs after it and the entry returns
 * -1. A fault already latched makes every entry return -1 at once.
 *
 * Verified by tools/test_camera_aim_reference.py (the original instructions
 * over captured AREA11 RAM: every store in order, every callee entry with
 * its arguments, whole RAM and scratchpad at the end). */
#ifndef EM_CAMERA_AIM_H
#define EM_CAMERA_AIM_H

#include <stdint.h>

#include "game/em_aim_fire_target.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef EmAimFireTarget EmCamAim;

int em_cam_aim_00197D20(EmCamAim *h, uint32_t cam, uint32_t player);
int em_cam_aim_00198650(EmCamAim *h, uint32_t cam, uint32_t player);
int em_cam_aim_0018CA90(EmCamAim *h, uint32_t cam, uint32_t player);
int em_cam_aim_00197740(EmCamAim *h, uint32_t cam, uint32_t player);
int em_cam_aim_00197870(EmCamAim *h, uint32_t cam, uint32_t player, int32_t a2);
int em_cam_aim_00198050(EmCamAim *h, uint32_t cam, uint32_t player, int32_t *result);
int em_cam_aim_00198440(EmCamAim *h, uint32_t cam, uint32_t player, int32_t a2);
int em_cam_aim_00198240(EmCamAim *h, uint32_t player, uint32_t gun, int32_t *result);
int em_cam_aim_001912B0(EmCamAim *h, uint32_t player);
int em_cam_aim_001999C0(EmCamAim *h, uint32_t player, int32_t a1, int32_t *result);
int em_cam_aim_001DB800(EmCamAim *h);
int em_cam_aim_00198AF0(EmCamAim *h, uint32_t cam, uint32_t player);
int em_cam_aim_00198930(EmCamAim *h, uint32_t cam, uint32_t player, int32_t *result);
int em_cam_aim_00191530(EmCamAim *h, uint32_t cam, uint32_t player);

#ifdef __cplusplus
}
#endif

#endif /* EM_CAMERA_AIM_H */
