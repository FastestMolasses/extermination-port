/* em_camera_leftovers.h - the walking camera's remaining original routines
 * (docs/CAMERA_LEFTOVERS.md).
 *
 * Translations of the original routines, not models of them. They work on
 * the raw 0xD0-byte camera block at 0x008101E0 (EmCameraFollowRecord), the
 * raw 0x320-byte player record (EmPlayerLiveActor; D_008102B0 or whatever
 * record the caller's a1 names), the camera globals and the camera solvers'
 * scratchpad window 0x700038A0..0x70003A3F, by their original offsets:
 *
 *   0018B9C0  the camera frame: the D_008106EF cooldown, cam+8B |= the
 *             frame's input bits (0x700031F0), state 0 the one-shot seat
 *             (defaults, 0018CE60 style 5, the action pick, 0018C0C0,
 *             0018D7B0(1), the commit 0018C0D0(1)), state 1 the per-mode
 *             frame (+4 == 0: 00191390, 0018BC20, commit(1); +4 == 3:
 *             0022EEF0 and commit(0); anything else commit(1)).
 *   0018BC20  the camera action dispatch: 00190F20, then the two 16-way
 *             tables on the action byte +6 (mode byte +5 = 0 or 1).
 *   00190F20  the per-frame area-transition trigger (areas 0x12 and 0xE).
 *   0018C0C0  the target copy: D_008105E0 = cam+20 (16 bytes).
 *   001914A0  camera action 8 (the mode-8 settle), 00191580 its per-frame
 *             body and 0018C5A0 the height chase it ends with.
 *   001916C0  the per-state target placement (00195130 calls it with mode 0
 *             on every walking frame; 00191580 with mode 0).
 *   00191000  the L1 orient-behind request.
 *   00193D90  the idle auto orbit (camera state 2 of 00195130).
 *   0022FCA0  the boom pull-in / push-out / orbit of the locomotion tether.
 *   00230000  the locomotion tether (player codes 2 / 4 / 0xF), with
 *   00194D10  its region-height test.
 *   0018DD20  the desired-eye solver (0018D7B0's styles 0/1/3/4/7).
 *   0018F870  the aim solver (0018D7B0's styles 2 and 6).
 *   0018CE60  the floor / ceiling bounds under a point (cam+50 / cam+54).
 *   0018D910  the fixed-camera bounds (0018D7B0 style 5).
 *   0015CBA0  the player's state byte +1F0 -> action code +230 map (the
 *             codes every camera routine above dispatches on).
 *
 * Reused translations (called directly, not re-translated): 0018C6A0,
 * 0018C4B0, 00191D40, 00192010 and 00191390 from em_camera_follow_original.c;
 * 0011DF78 (fabsf) from em_sdk_math_original.c. The SDK vector leaves
 * (001028D0 sub, 001028B8 add, 00102760 normalize, 00103230 scale xyz,
 * 00102900 scale xyzw, 00102738 dot, 00102948 quad copy, 001031E0 xyz copy)
 * are translated in place as their VU0 macro instructions.
 *
 * Every other original callee is a worker (EmCamLeftWorkers). A worker the
 * current path needs and that is missing is a fault: the entry point checks
 * the workers its path can reach and returns -1 before its first write. A
 * worker that returns a negative value is a fault too: the routine stops at
 * once and returns -1, leaving the writes made before the call, as the
 * original order leaves them. *fault (EmCamLeftWorld.fault) names the first
 * missing or failing callee.
 *
 * Arithmetic: every COP1 operation and every VU0 macro operation goes
 * through em_ee_float.h (docs/EE_FLOAT_MODEL.md), on raw bit patterns.
 *
 * Oracle: tools/test_camera_leftovers_reference.py executes the original
 * instructions of every routine above from the user's pinned ELF (worker
 * callees hooked and scripted) and compares every written byte, the return
 * values and the worker calls; its captured mode runs the original camera
 * frame over the captured AREA11 RAM with these translations in place. */
#ifndef EM_CAMERA_LEFTOVERS_H
#define EM_CAMERA_LEFTOVERS_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_camera_follow_original.h"
#include "game/em_player_floor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The scratchpad the camera solvers share ------------------------------
 * 0x700038A0..0x70003A3F, one word per slot. The follow camera's
 * EmCameraFollowScratch overlaps it (s38A0, s38B0, s38C0, s3910, s3A20):
 * see em_camleft_scratch_from_follow / _to_follow. */
#define EM_CAMLEFT_SCRATCH_BASE  UINT32_C(0x700038A0)
#define EM_CAMLEFT_SCRATCH_WORDS 0x68
typedef struct EmCamLeftScratch {
    uint32_t w[EM_CAMLEFT_SCRATCH_WORDS];
} EmCamLeftScratch;

/* The word at original scratchpad address `address` (0x700038A0..0x70003A3C). */
static inline uint32_t *em_camleft_spad(EmCamLeftScratch *s, uint32_t address)
{
    return &s->w[(address - EM_CAMLEFT_SCRATCH_BASE) / 4u];
}

/* ---- What the segment query 0019A910 leaves ------------------------------
 * The scratchpad words its callers read afterwards: the point 0x700031B0
 * (all four words: 00102948 copies the quad) and, through the record
 * pointer *0x700031D0, the record's +0x1A halfword and its +0x24..+0x2C
 * normal. The segment worker updates this after every call, exactly as the
 * original scratchpad would read after it (hit or miss). */
typedef struct EmCamLeftHit {
    uint32_t point[4];      /* 0x700031B0..0x700031BC */
    uint16_t record_1A;     /* (*0x700031D0) + 0x1A */
    uint32_t normal[3];     /* (*0x700031D0) + 0x24, + 0x28, + 0x2C */
} EmCamLeftHit;

/* ---- Globals (pointers into the binder's canonical storage) -------------- */
typedef struct EmCamLeftGlobals {
    /* eye D_008105D0, target D_008105E0, D_00810690 / 98 / 9C and the area
     * bytes D_00810700..702: the SAME storage the follow camera uses. */
    const EmCameraFollowGlobals *follow;
    uint32_t *d5F0;              /* D_008105F0 vec4 (0018B9C0 state 0 writes it) */
    uint8_t *d6EF;               /* D_008106EF: the camera frame's countdown */
    const uint8_t *d6B8;         /* D_008106B8: 00190F20's gate */
    const uint16_t *dE74;        /* D_00810E74: 00191000's pad mask */
    const int16_t *d28A9A0;      /* D_0028A9A0 (actions 6 and 8 read it) */
    const uint16_t *s3B80;       /* 0x70003B80: the frame's pad-held bits */
    const uint8_t *s3B8D;        /* 0x70003B8D: 00190F20's area-0xE gate */
    const uint8_t *s31F0;        /* 0x700031F0: the frame's input bits */
    /* D_0024A5F0: 00194D10's region table (stride 0x40; the word at +4 of
     * entry i is its reference height). Words from the user's ELF. */
    const uint32_t *d24A5F0;
    size_t d24A5F0_words;
} EmCamLeftGlobals;

/* ---- Workers --------------------------------------------------------------
 * Each returns >= 0, or < 0 for a fault. `cam` is always the camera block;
 * `e` the record the original passes as a1. */
typedef struct EmCamLeftWorkers {
    void *context;
    /* The SDK / math callees: *out is the original's f0 (raw bits). */
    int (*wrap)(void *ctx, uint32_t x, uint32_t *out);                  /* 001B1470 */
    /* 001B12B0(target, current, rate): the angle approach. */
    int (*approach)(void *ctx, uint32_t target, uint32_t current, uint32_t rate, uint32_t *out);
    int (*heading)(void *ctx, const uint32_t obj[3], uint32_t x, uint32_t z,
                   uint32_t *out);                                      /* 001B1240 */
    int (*sine)(void *ctx, uint32_t x, uint32_t *out);                  /* 0011E2A8 */
    int (*cosine)(void *ctx, uint32_t x, uint32_t *out);                /* 0011DE90 */
    int (*atan2)(void *ctx, uint32_t y, uint32_t x, uint32_t *out);     /* 0011E620 */
    int (*sqrt)(void *ctx, uint32_t x, uint32_t *out);                  /* 0011E748 */
    /* 0019A910(from, to, mask): *result = its v0; *hit = the scratchpad
     * words above as they read after the call. */
    int (*segment)(void *ctx, const uint32_t from[4], const uint32_t to[4], int mask,
                   EmCamLeftHit *hit, int *result);
    /* 001B1EA0(mode, point, polygon, count): `point` the three words at
     * a1, `polygon` the original address of a2; *result its v0. */
    int (*inside)(void *ctx, int mode, const uint32_t point[3], uint32_t polygon, int count,
                  int *result);
    /* 0018D7B0(cam, style): *result its v0 (the follow module's
     * em_camera_follow_0018D7B0). */
    int (*solve_dispatch)(void *ctx, EmCameraFollowRecord *cam, int style, int *result);
    /* 0018C0D0(cam, mode): the commit. */
    int (*commit)(void *ctx, EmCameraFollowRecord *cam, int mode);
    /* 0022EEF0(cam, a1): the scripted-camera frame (+4 == 3). */
    int (*w_0022EEF0)(void *ctx, EmCameraFollowRecord *cam, int a1);
    /* 001B0C60(a0, a1, a2): the area-change request. */
    int (*w_001B0C60)(void *ctx, int a0, int a1, int a2);
    /* 0018BC20's action handlers, (cam, e). */
    int (*w_00195130)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e);
    int (*w_00197D20)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e);
    int (*w_00198650)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e);
    int (*w_00198AF0)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e);
    int (*w_001936E0)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e);
    int (*w_0018CA90)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e);
    int (*w_00198CE0)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e);
    int (*w_00198D90)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e);
    int (*w_00198F10)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e);
    int (*w_001963A0)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e);
    int (*w_00196CE0)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e);
    int (*w_00197390)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e);
    /* 00193EB0(cam, e, a2): the event router. */
    int (*w_00193EB0)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e, int a2);
    /* 001DD980(D_008105D0, &D_008105E0) (action 8). */
    int (*w_001DD980)(void *ctx, uint32_t *eye, uint32_t *target);
    /* 001D2830(a0, a1) (action 10). */
    int (*w_001D2830)(void *ctx, int a0, int a1);
    /* 001B0300() (mode 1's first frame). */
    int (*w_001B0300)(void *ctx);
} EmCamLeftWorkers;

/* One camera world. */
typedef struct EmCamLeftWorld {
    EmCameraFollowRecord *cam;         /* 0x008101E0 */
    EmPlayerLiveActor *player;         /* D_008102B0 (0018B9C0's base) */
    const EmCamLeftGlobals *globals;
    EmCamLeftScratch *scratch;         /* 0x700038A0.. */
    EmCamLeftHit *hit;                 /* 0x700031B0 / *0x700031D0 */
    const EmCamLeftWorkers *workers;
    uint32_t fault;                    /* 0, or the first missing / failing callee */
} EmCamLeftWorld;

/* ---- Entry points ---------------------------------------------------------
 * Each returns 0 (with the original's return value in *result where it has
 * one; result may be NULL), or -1 on a fault. `e` is the record the
 * original receives as a1. */

int em_camleft_0018B9C0(EmCamLeftWorld *w);
int em_camleft_0018BC20(EmCamLeftWorld *w, EmPlayerLiveActor *e);
int em_camleft_00190F20(EmCamLeftWorld *w, EmPlayerLiveActor *e);
int em_camleft_0018C0C0(EmCamLeftWorld *w);
int em_camleft_001914A0(EmCamLeftWorld *w, EmPlayerLiveActor *e);
int em_camleft_00191580(EmCamLeftWorld *w, EmPlayerLiveActor *e);
/* 0018C5A0(v, y, max): chases v[1] (the word at a0 + 4) toward y + cam+98
 * (D_00810278 is cam+98); *result 0 or 4. */
int em_camleft_0018C5A0(EmCamLeftWorld *w, uint32_t v[2], uint32_t y, uint32_t max, int *result);
int em_camleft_001916C0(EmCamLeftWorld *w, EmPlayerLiveActor *e, int mode);
int em_camleft_00191000(EmCamLeftWorld *w, const EmPlayerLiveActor *e, int *result);
int em_camleft_00193D90(EmCamLeftWorld *w, EmPlayerLiveActor *e);
int em_camleft_0022FCA0(EmCamLeftWorld *w);
int em_camleft_00230000(EmCamLeftWorld *w, EmPlayerLiveActor *e);
int em_camleft_00194D10(EmCamLeftWorld *w, const EmPlayerLiveActor *e, int index, int *result);
int em_camleft_0018DD20(EmCamLeftWorld *w, EmPlayerLiveActor *e, int style, int mask, int *result);
int em_camleft_0018F870(EmCamLeftWorld *w, EmPlayerLiveActor *e, int style, int mask, int *result);
/* 0018CE60(cam, v, style): `v` addresses the four words at a1 (player +B0,
 * or the scratch vector 0x700038D0 from 0018F870); its word 1 is read again
 * at the end. */
int em_camleft_0018CE60(EmCamLeftWorld *w, const void *v, int style);
int em_camleft_0018D910(EmCamLeftWorld *w, EmPlayerLiveActor *e, int mask);
/* 0015CBA0(p): a leaf on the player record; -1 only for NULL. */
int em_camleft_0015CBA0(EmPlayerLiveActor *p);

/* ---- Binding helpers ------------------------------------------------------ */
/* Copy the words EmCameraFollowScratch shares with the solvers' window
 * (0x700038A0..0x700038CC, 0x70003910..0x7000391C, 0x70003A20..0x70003A2C). */
void em_camleft_scratch_from_follow(EmCamLeftScratch *s, const EmCameraFollowScratch *f);
void em_camleft_scratch_to_follow(const EmCamLeftScratch *s, EmCameraFollowScratch *f);

#ifdef __cplusplus
}
#endif

#endif /* EM_CAMERA_LEFTOVERS_H */
