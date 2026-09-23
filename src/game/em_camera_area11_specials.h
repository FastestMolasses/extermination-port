/* em_camera_area11_specials.h - the walking camera's area specials, its
 * event router, the melee lock-on and the aim release
 * (docs/CAMERA_AREA11_SPECIALS.md).
 *
 * Translations of the original routines, not models of them. Each works on
 * the raw 0xD0-byte camera block (D_008101E0, cleared by 001CB590 with
 * 0xD0) and the raw 0x320-byte player record (D_008102B0,
 * EmPlayerLiveActor) by their original offsets:
 *   00195130  camera action 0 (0018BC20 mode 0): the per-area walking
 *             camera. State +1 0/1 runs 001916C0, the area arm (AREA11 is
 *             area 0xB: the box arm and the centre arm, CAM-09), then 001921D0 if no
 *             arm took the frame; 2 is the auto orbit 00193D90; 3 the
 *             freelook hand-back 001921D0(.., 1); 4 the area-0xD pan. Every
 *             state ends with 00191210 and 00193EB0.
 *   00193EB0  the event router on the player's action code (+0x230): it
 *             sets the camera action +6 (and clears +1) for codes 0x10,
 *             0xD/0x2A, 0xC/0x29, 0x28, 0x12, runs the L1 orient-behind
 *             00191000 for codes 1/0x21 (CAM-11 gating) and fires the
 *             area 0x13/0xD region events 001B0C60 for the run codes.
 *   001936E0  camera action 3 (both 0018BC20 tables): the melee lock-on
 *             swing (CAM-11).
 *   00197490  the aim release (CAM-16): called by 00198650 (action 2) and
 *             00197D20 (action 1) with a2 = 0 or 1.
 *   00191210  the area-0x10 eye clamp (a leaf all of them share).
 *
 * Inline leaves (translated where they are called): 0011DF78 (fabsf: the
 * sign bit cleared), 00102948 (a 16-byte copy), 001031E0 (a 3-word copy),
 * 001029C0 (the identity matrix), 001028D0 (VSUB.xyzw) and 00102738
 * (VMUL.xyz, VADDy.x, VADDz.x).
 *
 * Every other original callee is a worker. A worker the path needs that is
 * missing is a fault: each entry point checks the workers its path can
 * reach (by the state and area bytes it dispatches on, section 4 of the
 * doc) and returns -1 before its first write. A worker that returns a
 * negative value is a fault too: the routine stops at once and returns -1,
 * leaving the writes made before the call, as the original order leaves
 * them. `fault_address` records the first missing or failing callee.
 *
 * Arithmetic: every COP1 operation and VU0 macro op goes through
 * em_ee_float.h (docs/EE_FLOAT_MODEL.md), on raw bit patterns.
 *
 * Oracle: tools/test_camera_area11_specials_reference.py executes the
 * original instructions of every routine above from the user's pinned ELF
 * (callees hooked and recorded) and compares the camera block, the player
 * record, the globals, the scratchpad words and every worker call (its
 * arguments and the whole compared state at the call); its world mode runs
 * the routines over every captured route snapshot with the ORIGINAL callees
 * bound as workers and compares the whole RAM and scratchpad. The original
 * routines are void; the 0 / -1 returns here are the port's fault channel. */
#ifndef EM_CAMERA_AREA11_SPECIALS_H
#define EM_CAMERA_AREA11_SPECIALS_H

#include <stdint.h>

#include "game/em_player_floor.h"

#define EM_CAM_SPECIALS_BLOCK_SIZE 0xD0u     /* D_008101E0 .. D_008102AF */
#define EM_CAM_SPECIALS_D_008101E0 0x008101E0u
#define EM_CAM_SPECIALS_D_008102B0 0x008102B0u

/* The scratchpad words the routines read and write (raw bits). The binder
 * owns one instance and must hand the SAME instance to every worker whose
 * original reads or writes these words: 0019A910 leaves its hit point in
 * 0x700031B0 (00197490 copies it back), 00102C58 and 001026A0 get pointers
 * into s3400 / s3600. */
typedef struct EmCamSpecialsScratch {
    uint32_t s3040[4];   /* 0x70003040: read (00197490 copies it to player +0xA0) */
    uint32_t s31B0[4];   /* 0x700031B0: read (0019A910's point) */
    uint32_t s3400[16];  /* 0x70003400: the matrix; 0x70003430 = s3400[12..14] */
    uint32_t s3600[4];   /* 0x70003600: the offset 001026A0 transforms */
    uint32_t s3630[4];   /* 0x70003630: written (001936E0) */
    uint32_t s38A0[4];   /* 0x700038A0: written (001936E0) */
    uint32_t s3A20;      /* 0x70003A20: written */
    uint32_t s3A24;      /* 0x70003A24: written */
    uint32_t s3B50[4];   /* 0x70003B50: read (the camera angles staged into +0x30) */
    uint16_t s3B80;      /* 0x70003B80: read (held pad bits, 001936E0 tail) */
    uint8_t s3B8D;       /* 0x70003B8D: read (00193EB0 area gate) */
} EmCamSpecialsScratch;

/* The globals, in the binder's canonical storage (raw bits). */
typedef struct EmCamSpecialsWorld {
    uint8_t *d8101E0;           /* the camera block: 00191210 writes D_008101F8 (+0x18) */
    uint32_t *d8105D0;          /* the working eye, vec4 (D_008105D0..DC) */
    uint32_t *d8105E0;          /* the working target, vec4 (D_008105E0..EC) */
    const uint32_t *d81069C;    /* float */
    const uint8_t *d8106B8;     /* 00193EB0 area gate */
    const uint8_t *d8106F2;     /* 00195130 area 0x13 region index */
    const uint8_t *d810700;     /* area */
    const uint8_t *d810701;     /* room */
    const uint8_t *d810702;     /* camera index */
    const uint8_t *d81078B;     /* 00191210 gate */
    const uint8_t *d810803;     /* 00195130 area 0 gate */
    const uint16_t *d810E74;    /* 001936E0 pad mask */
    EmCamSpecialsScratch *spad;
} EmCamSpecialsWorld;

/* The original callees. Each returns 0, or a negative value on a fault.
 * `cam` and `player` are the arguments the routine was given; vector
 * arguments point into the camera block, the globals or the scratch (the
 * original passes those addresses). Float arguments and results are raw
 * bits. `*result` receives the callee's v0 (or f0). */
typedef struct EmCamSpecialsWorkers {
    void *ctx;
    /* The walking-camera helpers (all in the boot ELF). */
    int (*w_001916C0)(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t mode);
    int (*w_001921D0)(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t mode);
    int (*w_00193D90)(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t mode);
    int (*w_0018D7B0)(void *ctx, uint8_t *cam, int32_t style);
    /* 0018C4B0(vec, y, rate): eases vec[1] toward y; *result its v0. */
    int (*w_0018C4B0)(void *ctx, void *vec, uint32_t y, uint32_t rate, int32_t *result);
    /* 0018C6A0(from, to, rate): eases to[0]/to[2] toward from; *result its v0. */
    int (*w_0018C6A0)(void *ctx, const void *from, void *to, uint32_t rate, int32_t *result);
    int (*w_00191D40)(void *ctx, uint8_t *cam, uint32_t want, uint32_t rate);
    int (*w_00192010)(void *ctx, uint8_t *cam, uint32_t f12, uint32_t f13, uint32_t f14);
    int (*w_0022FCA0)(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t a2);
    int (*w_001944B0)(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t index,
                      int32_t *result);
    int (*w_00194D10)(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t index,
                      int32_t *result);
    int (*w_00194DB0)(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t a2);
    int (*w_00230230)(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t *result);
    /* 0x823FE0: the AREA13 overlay's camera hook (a0 = cam). */
    int (*w_00823FE0)(void *ctx, uint8_t *cam, int32_t *result);
    int (*w_001AEDE0)(void *ctx, int32_t a0, int32_t a1);
    /* 00193EB0's callees. */
    int (*w_00191000)(void *ctx, uint8_t *cam, EmPlayerLiveActor *player);
    int (*w_001B0C60)(void *ctx, int32_t a0, int32_t a1, int32_t a2);
    /* The VU0 matrix leaves: 00102C58(dst, src, angles) with dst == src,
     * and 001026A0(out, m, v) = v x m. */
    int (*w_00102C58)(void *ctx, uint32_t dst[16], const uint32_t src[16], const void *angles);
    int (*w_001026A0)(void *ctx, void *out, const uint32_t m[16], const uint32_t v[4]);
    /* The SDK math (0011E748 sqrtf, 0011E620 atan2f(f12, f13), 0011E2A8
     * sinf, 0011DE90 cosf) and 001B1470 (the angle wrap). */
    int (*w_0011E748)(void *ctx, uint32_t x, uint32_t *result);
    int (*w_0011E620)(void *ctx, uint32_t y, uint32_t x, uint32_t *result);
    int (*w_0011E2A8)(void *ctx, uint32_t x, uint32_t *result);
    int (*w_0011DE90)(void *ctx, uint32_t x, uint32_t *result);
    int (*w_001B1470)(void *ctx, uint32_t x, uint32_t *result);
    /* 001B1240(object, x, z): the yaw from object toward (x, z). */
    int (*w_001B1240)(void *ctx, const uint32_t object[4], uint32_t x, uint32_t z,
                      uint32_t *result);
    /* 001936E0's grab test (a0 = cam, a1 = player). */
    int (*w_00193660)(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t *result);
    /* 00197490's callees. */
    int (*w_00197870)(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t a2);
    int (*w_00198440)(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t a2);
    int (*w_001912B0)(void *ctx, EmPlayerLiveActor *player);
    /* 0019A910(from, to, mask): the segment test; *result its v0. */
    int (*w_0019A910)(void *ctx, const void *from, const void *to, int32_t mask,
                      int32_t *result);
    int (*w_001B0300)(void *ctx);
} EmCamSpecialsWorkers;

typedef struct EmCamSpecials {
    EmCamSpecialsWorld world;
    EmCamSpecialsWorkers w;
    uint32_t fault_address;     /* 0, or the first missing / failing callee */
} EmCamSpecials;

/* ---- The routines ------------------------------------------------------
 * cam is the camera block (EM_CAM_SPECIALS_BLOCK_SIZE bytes; the original
 * always passes D_008101E0), player the player record (D_008102B0). Each
 * returns 0, or -1 on a fault. */

/* 00195130(cam, player). */
int em_cam_specials_00195130(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player);
/* 00193EB0(cam, player, handled). */
int em_cam_specials_00193EB0(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player,
                             int32_t handled);
/* 001936E0(cam, player). */
int em_cam_specials_001936E0(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player);
/* 00197490(cam, player, a2). */
int em_cam_specials_00197490(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player,
                             int32_t a2);
/* 00191210(): reads the world only; -1 when the world is incomplete. */
int em_cam_specials_00191210(EmCamSpecials *s);

/* 1 when every world pointer and every worker the routine can reach from
 * this camera state / area is bound (the check each entry point makes). */
int em_cam_specials_ready_00195130(const EmCamSpecials *s, const uint8_t *cam);
int em_cam_specials_ready_00193EB0(const EmCamSpecials *s);
int em_cam_specials_ready_001936E0(const EmCamSpecials *s);
int em_cam_specials_ready_00197490(const EmCamSpecials *s);

/* ---- Adapters (ctx = EmCamSpecials) -------------------------------------
 * The shape of a 0018BC20 action slot (camera, player) and of the aim
 * callers' 00197490 call. */
int em_cam_specials_action_00195130(void *ctx, uint8_t *cam, EmPlayerLiveActor *player);
int em_cam_specials_action_001936E0(void *ctx, uint8_t *cam, EmPlayerLiveActor *player);
int em_cam_specials_call_00193EB0(void *ctx, uint8_t *cam, EmPlayerLiveActor *player,
                                  int32_t handled);
int em_cam_specials_call_00197490(void *ctx, uint8_t *cam, EmPlayerLiveActor *player,
                                  int32_t a2);

#endif
