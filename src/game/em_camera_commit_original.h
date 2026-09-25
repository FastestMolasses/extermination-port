/* em_camera_commit_original.h - the camera commit 0018C0D0 and the lock-on
 * grab test 00193660 (docs/CAMERA_LIVE.md section 2).
 *
 * Translations of the original routines, not models of them:
 *
 *   0018C0D0(cam, mode)  the per-frame camera commit (byte-matched C; the
 *                        instructions were read as well): the actual
 *                        horizontal distance D_0081069C, the eye height over
 *                        the player D_00810698, the desired distance
 *                        D_00810690, |desired dy| D_00810694, the view
 *                        forward 0x700038A0 (normalised), the view position
 *                        0x700038C0 (the eye pushed along the forward by 4.0
 *                        or -1.0, or the eye itself), the view matrix
 *                        D_00810610 (00102CD0), its transpose D_00810650
 *                        (00102798), cam+B0 and D_00810600 (the forward),
 *                        the forward heading D_008106A0 (atan2(-z, x)) and
 *                        cam+9C (001B1240 from the eye toward the target).
 *   00102798(out, m)     the 4x4 transpose (MMI word interleaves; a pure
 *                        data move).
 *   00193660()           001936E0's grab test: 0x700038A0 = D_008105E0 -
 *                        D_008105D0, 0x70003A20 = sqrtf(its xyz dot), and
 *                        1 when that is below 5.5.
 *
 * The SDK vector leaves 001028D0, 00102760, 00102738, 00102948 and 001031E0
 * are the VU0 macro translations of em_camera_leftovers_internal.h;
 * 0011DF78 is em_sdk_math_original's. 0011E748 (sqrtf), 0011E620 (atan2f),
 * 001B1240 and 00102CD0 are workers. A missing worker faults before the
 * first write; a failing worker stops the routine at once (-1, `fault` = its
 * address), leaving the writes made before the call, as the original order
 * leaves them.
 *
 * Every COP1 and VU0 macro operation goes through em_ee_float.h on raw bit
 * patterns (docs/EE_FLOAT_MODEL.md).
 *
 * Oracle: tools/test_camera_live_reference.py executes the original
 * instructions of 0018C0D0 (with every leaf and callee it reaches:
 * 001028D0, 0011E748, 0011DF78, 00102760, 001031E0, 00102CD0 and its leaves,
 * 00102948, 00102798, 0011E620, 001B1240) and of 00193660 (with 001028D0,
 * 00102738, 0011E748) and compares every written byte. */
#ifndef EM_CAMERA_COMMIT_ORIGINAL_H
#define EM_CAMERA_COMMIT_ORIGINAL_H

#include <stdint.h>

#include "game/em_camera_leftovers.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EmCameraCommitWorkers {
    void *context;
    /* 0011E748(x): sqrtf; *out its f0. */
    int (*sqrt)(void *context, uint32_t x, uint32_t *out);
    /* 0011E620(y, x): atan2f; *out its f0. */
    int (*atan2)(void *context, uint32_t y, uint32_t x, uint32_t *out);
    /* 001B1240(obj, x, z): obj is the three words at the original's a0. */
    int (*heading)(void *context, const uint32_t obj[3], uint32_t x, uint32_t z, uint32_t *out);
    /* 00102CD0(out, pos, fwd, up): the look-at (em_cs_00102CD0). */
    int (*lookat)(void *context, uint32_t out[16], const uint32_t pos[4], const uint32_t fwd[4],
                  const uint32_t up[4]);
} EmCameraCommitWorkers;

/* The storage 0018C0D0 reads and writes (raw bits, the binder's canonical
 * storage). */
typedef struct EmCameraCommitWorld {
    EmCameraFollowRecord *cam;          /* a0 (always 0x008101E0) */
    const EmPlayerLiveActor *player;    /* D_008102B0: +A4 is D_00810354 */
    uint32_t *eye;                      /* D_008105D0, vec4 */
    uint32_t *target;                   /* D_008105E0, vec4 */
    const uint32_t *up;                 /* D_008105F0, vec4 */
    uint32_t *fwd;                      /* D_00810600, vec4 */
    uint32_t *view;                     /* D_00810610, 16 words */
    uint32_t *view_t;                   /* D_00810650, 16 words */
    uint32_t *d690, *d694, *d698, *d69C, *d6A0;
    EmCamLeftScratch *scratch;          /* 0x700038A0..: 38A0, 38B0, 38C0 */
    const EmCameraCommitWorkers *workers;
    uint32_t fault;                     /* 0, or the first missing / failing callee */
} EmCameraCommitWorld;

/* 0018C0D0(cam, mode). 0, or -1 on a fault. */
int em_camera_commit_0018C0D0(EmCameraCommitWorld *w, int mode);

/* 00102798(out, m): out = the transpose of m (out may alias m). */
void em_camera_commit_00102798(uint32_t out[16], const uint32_t m[16]);

/* 00193660(): *result its v0. s38A0 / s3A20 are the scratchpad words
 * 0x700038A0..AC / 0x70003A20; eye / target are D_008105D0 / D_008105E0.
 * `workers` supplies sqrt only. 0, or -1 (a missing or failing sqrt, or a
 * refused VU form). */
int em_camera_commit_00193660(const EmCameraCommitWorkers *workers, const uint32_t eye[4],
                              const uint32_t target[4], uint32_t s38A0[4], uint32_t *s3A20,
                              int32_t *result);

#ifdef __cplusplus
}
#endif

#endif /* EM_CAMERA_COMMIT_ORIGINAL_H */
