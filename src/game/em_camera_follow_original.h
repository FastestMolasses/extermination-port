/* em_camera_follow_original.h - the generic follow camera (docs/CAMERA_FOLLOW_ORIGINAL.md).
 *
 * Translations of the original routines, not models of them. They work on
 * the camera record at 0x008101E0 (EmCameraFollowRecord, 0xD0 bytes, raw,
 * by its original offsets), the player record at 0x008102B0
 * (EmPlayerLiveActor) and the camera globals at 0x008105D0..0x00810702:
 *
 *   001921D0  the follow update: 00195130 calls it with freelook 0 for its
 *             camera states +1 = 0/1 and with freelook 1 for +1 = 3. It
 *             dispatches on the player state word +230: 2/4/0xF go to the
 *             locomotion tether 00230000, 6/7/8/9/0xA/0x13/0x14/0x15/0x18/
 *             0x19/0x2C/0x2D/0x2F pose the eye themselves, and 1/0x26/0x27
 *             (with the sign 1.0) and every other state (0.0) run the tail:
 *             the boom pull-in or the orbit push-out, the height chase, the
 *             481-frame idle re-orbit and 0018D7B0(cam, 0).
 *   0018D7B0  the solve dispatch: 0018D330, then the solver the style picks
 *             (a worker), cam+7 = its result, then for style 0 the chase of
 *             the actual eye D_008105D0 (0018C6A0 / 0018C4B0 at 4.0) and for
 *             style 1 the hard copy of eye and target.
 *   0018D330  the prepass: cam+6D (0019B7D0 ground under the hip), cam+5A
 *             (0x80 ceiling within 200 over the hip, 0x01 / 0x08 wall /
 *             ceiling on the ray from the player toward the eye, 0x10 the
 *             aim style's +6 ray) and cam+60 (the ceiling Y).
 *   00191390  the per-state heights: cam+94 = cam+98 = 0, cam+8C / cam+5C
 *             by the player state word +230, cam+98 = 23.0 when cam+6D.
 *   0018C6A0  the x/z chase (d/4 inside 1.0, else sign(d) * min(|d|/6, max)).
 *   0018C4B0  the y chase (d/4 inside 1.0, else sign(d) * min(|d|/8, max)).
 *   00191D40  the eye height chase under the cap cam+54 (d/5 inside 1.0,
 *             else min(|d|/10, rate)), with the area 0x10 / 3 clamps.
 *   00192010  the eye height chase with separate up / down dead bands.
 *   00191120  the bounded yaw step toward a goal.
 *   The SDK vector leaves they reach, as their VU0 macro instructions:
 *   001028D0 (sub), 001028B8 (add), 00102760 (normalize), 00103230
 *   (scale), 00102738 (dot), 001026A0 (matrix x vector), 00102948 (quad
 *   copy) and 001031E0 (xyz copy).
 *
 * 0011DF78 (fabsf) is the translation in em_sdk_math_original.c. Every
 * other original callee is a worker (EmCameraFollowWorkers). A worker that
 * is missing is a fault: each entry point checks the whole worker set and
 * returns -1 before its first write. A worker that returns a negative value
 * is a fault too: the routine stops at once and returns -1, leaving the
 * writes made before the call, as the original order leaves them.
 *
 * Arithmetic: every COP1 operation and every VU0 macro operation goes
 * through em_ee_float.h (docs/EE_FLOAT_MODEL.md), on raw bit patterns.
 *
 * Oracle: tools/test_camera_follow_original_reference.py executes the
 * original instructions of every routine above from the user's pinned ELF
 * (worker callees hooked and recorded) and compares the camera record, the
 * globals, the scratchpad words, the return values and the worker calls.
 * EM_TEST_WORLD=1 runs the original camera frame 0018B9C0 over the route
 * captures and compares these routines wherever the frame reaches them. */
#ifndef EM_CAMERA_FOLLOW_ORIGINAL_H
#define EM_CAMERA_FOLLOW_ORIGINAL_H

#include <stdint.h>
#include <string.h>

#include "game/em_player_floor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The camera record *D_00275B44 = 0x008101E0, up to the player record. */
#define EM_CAMERA_FOLLOW_RECORD_SIZE 0xD0
typedef struct EmCameraFollowRecord {
    uint8_t bytes[EM_CAMERA_FOLLOW_RECORD_SIZE];
} EmCameraFollowRecord;

/* The camera globals these routines read and write, in the binder's
 * canonical storage (raw bits; the same storage the commit 0018C0D0, the
 * area specials and the scripts use). */
typedef struct EmCameraFollowGlobals {
    uint32_t *eye;           /* D_008105D0, vec4: the actual eye (read and chased) */
    uint32_t *target;        /* D_008105E0, vec4: the actual target (x and z read;
                                0018D7B0 style 1 copies cam+20 here) */
    const uint32_t *d690;    /* D_00810690: the commit's horizontal eye-target distance goal */
    const uint32_t *d698;    /* D_00810698: the eye height over the player ground */
    const uint32_t *d69C;    /* D_0081069C: the actual horizontal distance */
    const uint8_t *area;     /* D_00810700 */
    const uint8_t *d701;     /* D_00810701 */
    const uint8_t *d702;     /* D_00810702 */
} EmCameraFollowGlobals;

/* The scratchpad words these routines write and read back (raw bits). The
 * binder owns one instance and must hand the SAME instance to every worker
 * whose original reads or writes these words (the solvers 0018DD20 /
 * 0018F870 / 0018D910 use 0x700038A0.. as well). */
typedef struct EmCameraFollowScratch {
    uint32_t s38A0[4];    /* 0x700038A0: probe end / pull direction */
    uint32_t s38B0[4];    /* 0x700038B0: probe start / orbit direction */
    uint32_t s38C0[4];    /* 0x700038C0: 0018D330's 20-unit ray */
    uint32_t s3910[4];    /* 0x70003910: 0018D330's 9-unit ray (style 2) */
    uint32_t s3A20[4];    /* 0x70003A20..0x70003A2C: slack, step, angle, margin */
    uint32_t s3B50[4];    /* 0x70003B50: read only (0x2F copies it to cam+30;
                             the orbit reads 0x70003B54) */
    uint32_t s3400[16];   /* 0x70003400: state 0x2F's matrix */
    uint32_t s3600[4];    /* 0x70003600: state 0x2F's offset vector */
} EmCameraFollowScratch;

/* What 0019A910 leaves for its caller: its v0, and on a nonzero v0 the
 * halfword at (*0x700031D0) + 0x1A (the hit record's surface class) and the
 * word at 0x700031B4 (the hit point's y). */
typedef struct EmCameraFollowHit {
    int32_t result;
    uint16_t record_1A;
    uint32_t point_y;
} EmCameraFollowHit;

typedef struct EmCameraFollowWorkers {
    void *context;
    /* 001B12B0(target, current, rate): the angle approach; *out its f0. */
    int (*approach)(void *context, uint32_t target, uint32_t current, uint32_t rate,
                    uint32_t *out);
    /* 001B1470(x): the angle wrap; *out its f0. */
    int (*wrap)(void *context, uint32_t x, uint32_t *out);
    /* 001B1240(obj, x, z): wrap(atan2(x - obj[0], z - obj[2])); `obj` is a
     * copy of the three words the original's a0 points at (cam+10 or
     * D_008105D0). *out its f0. */
    int (*heading)(void *context, const uint32_t obj[3], uint32_t x, uint32_t z, uint32_t *out);
    /* 0011E2A8 sinf and 0011DE90 cosf. */
    int (*sine)(void *context, uint32_t x, uint32_t *out);
    int (*cosine)(void *context, uint32_t x, uint32_t *out);
    /* 00230000(cam, player): the locomotion tether (player states 2/4/0xF). */
    int (*tether)(void *context, EmCameraFollowRecord *cam, EmPlayerLiveActor *player);
    /* 0018DD20(cam, player, style, mask) and 0018F870(cam, player, style,
     * mask): the solvers; *result is their v0 (0018D7B0 stores it at cam+7). */
    int (*solve)(void *context, EmCameraFollowRecord *cam, EmPlayerLiveActor *player, int style,
                 int mask, int *result);
    int (*solve_aim)(void *context, EmCameraFollowRecord *cam, EmPlayerLiveActor *player,
                     int style, int mask, int *result);
    /* 0018D910(cam, player, mask): the fixed-camera bounds (style 5); its
     * return value is not read. */
    int (*bounds)(void *context, EmCameraFollowRecord *cam, EmPlayerLiveActor *player, int mask);
    /* 0019A910(from, to, mask): the camera segment query. from/to are the
     * four words at the original's a0/a1. */
    int (*segment)(void *context, const uint32_t from[4], const uint32_t to[4], int mask,
                   EmCameraFollowHit *hit);
    /* 0019B7D0(from, to): the grid ground query; *result is its v0. */
    int (*ground)(void *context, const uint32_t from[4], const uint32_t to[4], int *result);
    /* 001029C0(m): the identity; 00102C58(out, in, angles): the Euler
     * rotation (state 0x2F; `angles` is the four words at cam+30). */
    int (*identity)(void *context, uint32_t m[16]);
    int (*euler)(void *context, uint32_t out[16], const uint32_t in[16], const uint32_t angles[4]);
} EmCameraFollowWorkers;

/* One camera world: the records and globals the routines work on. */
typedef struct EmCameraFollowWorld {
    EmCameraFollowRecord *cam;          /* 0x008101E0 */
    EmPlayerLiveActor *player;          /* D_008102B0, the player 0018D7B0 hands its callees */
    EmCameraFollowGlobals *globals;
    EmCameraFollowScratch *scratch;
    const EmCameraFollowWorkers *workers;
} EmCameraFollowWorld;

/* 1 when every worker, the records, every global pointer and the scratch are bound. */
int em_camera_follow_bound(const EmCameraFollowWorld *world);

/* 001921D0(cam, other, freelook, unused): `other` is the a1 00195130 passes
 * (the player record). Returns 0, or -1 on a fault. */
int em_camera_follow_001921D0(const EmCameraFollowWorld *world, EmPlayerLiveActor *other,
                              int freelook);
/* 0018D7B0(cam, style): *result is its return value (also stored at cam+7). */
int em_camera_follow_0018D7B0(const EmCameraFollowWorld *world, int style, int *result);
/* 0018D330(cam, player, style, mask). */
int em_camera_follow_0018D330(const EmCameraFollowWorld *world, EmPlayerLiveActor *player,
                              int style, int mask);
/* 00191390(cam, player): cannot fault (a leaf); -1 only for NULL records. */
int em_camera_follow_00191390(EmCameraFollowRecord *cam, const EmPlayerLiveActor *player);

/* The leaves. They call nothing, so they cannot fault; each returns -1
 * only for a NULL argument (before any write), else 0.
 * The chases work on raw words: `v` is the vector the original's pointer
 * addresses (x at [0], y at [1], z at [2]); *result (may be NULL) is the
 * original's return value.
 * 0018C6A0(src, dst, max): chases dst toward src. */
int em_camera_follow_0018C6A0(const uint32_t src[3], uint32_t dst[3], uint32_t max, int *result);
/* 0018C4B0(v, y, max): chases v[1] toward y. */
int em_camera_follow_0018C4B0(uint32_t v[3], uint32_t y, uint32_t max, int *result);
/* 00191D40(cam, y, rate) and 00192010(cam, y, up, down): the eye height
 * chases (00191D40 reads the area globals area / d701 / d702 only). */
int em_camera_follow_00191D40(EmCameraFollowRecord *cam, const EmCameraFollowGlobals *globals,
                              uint32_t y, uint32_t rate);
int em_camera_follow_00192010(EmCameraFollowRecord *cam, uint32_t y, uint32_t up, uint32_t down);
/* 00191120(goal, current, rate, limit): *out its f0. */
int em_camera_follow_00191120(const EmCameraFollowWorkers *workers, uint32_t goal,
                              uint32_t current, uint32_t rate, uint32_t limit, uint32_t *out);

/* Record access by original offset (raw words). */
static inline uint32_t em_camera_follow_word(const EmCameraFollowRecord *c, unsigned at)
{
    uint32_t v; memcpy(&v, c->bytes + at, 4); return v;
}
static inline void em_camera_follow_set_word(EmCameraFollowRecord *c, unsigned at, uint32_t v)
{
    memcpy(c->bytes + at, &v, 4);
}

#ifdef __cplusplus
}
#endif

#endif
