/* em_player_closure_10_12_19.h - the player's +4 = 1 states 0x10, 0x12, 0x19
 * and 0x1A of the FLOOR closure (docs/PLAYER_CLOSURE_10_12_19.md).
 *
 * Translations of the original routines, not models of them:
 *   00169730  +4 1 / +5 0x10  (0015B130 state[0x10]; entered by 001662D0)
 *   0016AE40  +4 1 / +5 0x12  (0015B130 state[0x12]; entered by 002230A0)
 *   0016DE40  +4 1 / +5 0x19  (0015B130 state[0x19]; entered by 0016D130)
 *   0016EBA0  +4 1 / +5 0x1A  (0015B130 state[0x1A]; entered by 0016D130
 *                              and by 0016DE40 itself)
 * and their private callees (no other caller in the executable):
 *   001696A0, 0016ADE0  the use-button exit to +5 0x13
 *   0016A4B0            the +7 sub-state machine of 00169730 +6 0x50 and
 *                       0016AE40 +6 0x29; with 00181A70 (its own callee)
 *   0016A8B0            the push swing 0016A4B0 +7 2 / 3 calls; also
 *                       0015B130's state[0x15], which no store in the
 *                       FLOOR closure enters
 *   00181950            the three forward move probes (0016A4B0, 0016AE40)
 *   001814E0, 00181730, 001818D0, 00181B80, 00181BA0, 001787B0  (00169730)
 *   00181E20, 00181F60, 00182090, 00182100, 00175390          (0016AE40)
 *   001811F0, 00181430  the alternating step clips (00169730, 0016AE40)
 *   00179010, 00179910  the ground search and the area-2 exit (0016DE40)
 *   001B0B50            D_008106BE from D_008106C8 (a leaf; 0016EBA0 and
 *                       001B0460 call it), translated here as well.
 * Already translated elsewhere and called directly (pure over the record):
 *   00179880 em_player_fall_drop (em_player_fall.c);
 *   00181D70 em_player_major2_00181D70 and 001823E0
 *   em_player_major2_001823E0 (em_player_major2.c).
 *
 * Every routine works on the raw 0x320-byte player record
 * (EmPlayerLiveActor) by its original offsets; the +214/+308 pointer words
 * are never touched. Every other original callee is an explicit worker. A
 * state routine checks, before its first write, that every worker is bound,
 * and faults (-1) otherwise. A worker that returns a negative value is a
 * fault too: the routine stops at once, and the writes made before the call
 * stay, as the original order leaves them. Reading a table entry the
 * original's table does not have (D_00248630[+25C] or D_00248640[+25C] with
 * +25C > 3) is also a fault, at the point of the read. Arithmetic and float compares follow the
 * EE COP1 model (game/em_ee_float.h, docs/EE_FLOAT_MODEL.md), on raw bits.
 *
 * Oracle: tools/test_player_closure_10_12_19_reference.py executes the
 * original instructions of every routine above over the captured AREA11
 * RAM (every worker hooked and recorded) and compares all 0x320 record
 * bytes, the scene and scratchpad words, and every worker call with its
 * arguments, at exit and at entry to every worker call. */
#ifndef EM_PLAYER_CLOSURE_10_12_19_H
#define EM_PLAYER_CLOSURE_10_12_19_H

#include <stdint.h>

#include "game/em_player_floor.h"
#include "game/em_player_major2.h"

/* The globals these routines read or write outside the record. The routines
 * write only the fields their originals write. */
typedef struct EmPlayerClosure1019Scene {
    uint16_t pad_held;     /* D_00810E70[0] (001696A0, 0016ADE0) */
    uint16_t pad_pressed;  /* D_00810E74[0] (the use test of every state) */
    uint16_t use_mask;     /* 0x70003B76 */
    uint16_t mask_3B7C;    /* 0x70003B7C (001696A0, 0016ADE0) */
    uint16_t mask_3B7E;    /* 0x70003B7E (001696A0, 0016ADE0) */
    int16_t fade;          /* D_0028A9A0[0] (0016DE40, 0016EBA0) */
    uint8_t area;          /* D_00810700 */
    uint8_t sub_area;      /* D_00810701 */
    uint8_t zone;          /* D_00810702: 0016DE40 writes it */
    uint8_t d8106BE;       /* D_008106BE: 0016DE40 case 0 and 001B0B50 write it */
    uint8_t pad_gait;      /* D_00810E57 (00175390) */
    uint8_t pad_x;         /* D_00810E64 (00175390) */
    uint8_t pad_y;         /* D_00810E65 (00175390) */
    uint8_t area_flags2;   /* D_00810730[2] (00179910 reads D_00810730[area] with area 2) */
    uint8_t d8106B5, d8106B6, d8106B7, d8106B8;   /* 00179910 writes them */
    uint32_t d8106C8;      /* D_008106C8 (001B0B50) */
    uint32_t camera_yaw;   /* D_008106A0, raw bits (00175390) */
    uint32_t spad31E4;     /* 0x700031E4, raw bits (0016AE40 +6 0x14 phase 0) */
    uint32_t d275B10;      /* D_00275B10: 001696A0 writes +2E0 there */
    uint32_t d275B0C;      /* D_00275B00[3]: 001696A0 writes +2E8 there */
    uint32_t d281B64;      /* D_00281B64[0]: 001696A0 writes +C4 there */
} EmPlayerClosure1019Scene;

/* The scratchpad words the routines write (raw bits). 0x70003A20 is also
 * written by other modules' routines (em_player_fall.h EmPlayerLandScratch,
 * em_player_recovery.h): a worker whose original writes 0x70003A20 must
 * write s3A20 here, since the routines reload it after each call, as the
 * original does. The matrix and vectors are the operands the routines hand
 * the SDK workers. */
typedef struct EmPlayerClosure1019Scratch {
    uint32_t s3A20, s3A24, s3A28;   /* 0x70003A20 / 24 / 28 */
    uint32_t s36A0[16];             /* 0x700036A0: the yaw matrix of 00181950, 00181A70, 00181E20 */
    uint32_t s38A0[4];              /* 0x700038A0: the local point */
    uint32_t s38B0[4];              /* 0x700038B0: the transformed point */
    uint32_t s38C0[4];              /* 0x700038C0 */
    uint32_t s38D0[4];              /* 0x700038D0 */
} EmPlayerClosure1019Scratch;

/* The original callees. Each returns 0, or a negative value on a fault.
 * `actor` is the record the original passes in $a0. Vectors and matrices
 * are raw binary32 words. */
typedef struct EmPlayerClosure1019Workers {
    void *context;
    /* 001749A0(p, clip, force, blend). Its return value is not read. */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int force, float blend);
    /* 001885B0(p), 00188610(p), 00188550(p): *clip is the return value. */
    int (*clip_885B0)(void *context, EmPlayerLiveActor *actor, int *clip);
    int (*clip_88610)(void *context, EmPlayerLiveActor *actor, int *clip);
    int (*clip_88550)(void *context, EmPlayerLiveActor *actor, int *clip);
    /* 001751A0(p): the stick quadrant (+24C). 00174FD0(p): the steer input. */
    int (*stick)(void *context, EmPlayerLiveActor *actor);
    int (*steer)(void *context, EmPlayerLiveActor *actor);
    /* 00175900(p, search): the floor service; *result is its return value. */
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    /* 00178B90(p, arg): the translation along +C4. */
    int (*translate)(void *context, EmPlayerLiveActor *actor, int arg);
    /* 00179B90(): *value is its return value (0..4). */
    int (*random5)(void *context, int *value);
    /* 001FBD50(p, id, a2, radius). */
    int (*sound)(void *context, EmPlayerLiveActor *actor, int id, int a2, float radius);
    /* 001FB9F0(a0, a1, a2, a3). */
    int (*sound_1FB9F0)(void *context, int a0, int a1, int a2, int a3);
    /* 001B1470(x) and 001B12B0(target, current, rate) (raw bits). */
    int (*wrap)(void *context, uint32_t x, uint32_t *out);
    int (*approach)(void *context, uint32_t target, uint32_t current, uint32_t rate,
                    uint32_t *out);
    /* SDK: 0011DE90 cos, 0011E2A8 sin, 0011E620 atan2(y $f12, x $f13),
     * 0011E748 sqrt. */
    float (*cosine)(void *context, float x);
    float (*sine)(void *context, float x);
    float (*atan2)(void *context, float y, float x);
    float (*sqrt)(void *context, float x);
    /* float_to_int (001281C0). */
    int (*to_int)(void *context, uint32_t x, int32_t *out);
    /* build_trs_matrix(p+D0, p+B0, p+C0, p+60): out is written to p+D0. */
    int (*trs)(void *context, uint32_t out[16], const uint32_t position[3],
               const uint32_t rotation[3], const uint32_t scale[3]);
    /* 001026A0(out, M, v): out = v x M. */
    int (*apply)(void *context, const uint32_t matrix[16], const uint32_t v[4], uint32_t out[4]);
    /* 001028B8(out, a, b): out = a + b (xyzw). */
    int (*vadd)(void *context, const uint32_t a[4], const uint32_t b[4], uint32_t out[4]);
    /* 001029C0(M): identity. 00102BB0(dst, src, angle): the y rotation.
     * 00102918(dst, src, v): the translation (v is read as a quadword). */
    int (*identity)(void *context, uint32_t out[16]);
    int (*rotate_y)(void *context, uint32_t out[16], const uint32_t in[16], uint32_t angle);
    int (*translate_m)(void *context, uint32_t out[16], const uint32_t in[16],
                       const uint32_t v[4]);
    /* The collision probes. *result is the return value; `hit` is what the
     * probe leaves at 0x700031B0 (point), 0x700031D0 (the node record: the
     * attribute byte +1A is the low byte of hit->node, +24.. is
     * hit->normal, +34.. is hit->axis). The mask is passed as the original
     * passes it (0x80000000 bit included).
     *   0019A570(from, to, mask, id), 0019AD00(p, target, mask),
     *   0019AFE0(p, from, to, mask), 0019AB20(p, at, probe, mask). */
    int (*segment)(void *context, const uint32_t from[4], const uint32_t to[4], unsigned mask,
                   int id, int *result, EmPlayerProbeHit *hit);
    int (*move)(void *context, EmPlayerLiveActor *actor, const uint32_t target[4], unsigned mask,
                int *result, EmPlayerProbeHit *hit);
    int (*sweep)(void *context, EmPlayerLiveActor *actor, const uint32_t from[4],
                 const uint32_t to[4], unsigned mask, int *result, EmPlayerProbeHit *hit);
    int (*ground)(void *context, EmPlayerLiveActor *actor, const uint32_t at[4],
                  const uint32_t probe[4], unsigned mask, int *result, EmPlayerProbeHit *hit);
    /* 0019A310(out) over the hit the ground probe just left: *out is the
     * word it stores (p+9C). */
    int (*slope)(void *context, const EmPlayerProbeHit *hit, uint32_t *out);
    /* 00179450(p, p+B0): the column-table floor query (writes +258);
     * *result is its return value (0, 1 or 2). */
    int (*floor_query)(void *context, EmPlayerLiveActor *actor, const uint32_t point[3],
                       int *result);
    /* 00179150(p): the crawl step along +C4 (then 001790B0). */
    int (*w00179150)(void *context, EmPlayerLiveActor *actor);
    /* 00199DB0(out) over the last probe hit (0x700031D0/D4/D8): *result is
     * its return value; out[0..2] is what it stores when *result is 1. */
    int (*midpoint)(void *context, uint32_t out[3], int *result);
    /* 001782A0(p): *result is its return value. */
    int (*ledge_top)(void *context, EmPlayerLiveActor *actor, int *result);
    /* The three words at *(*(D_0024D650 + area*4) + sub*4) + offset
     * (00179910; area is 2 there, offset 0xF0 or 0x120). */
    int (*area_point)(void *context, int area, int sub, unsigned offset, uint32_t out[3]);
    /* anim_eval_skeleton(p). */
    int (*skeleton)(void *context, EmPlayerLiveActor *actor);
    /* 001B0460(a0), 001AEDE0(a0, a1), 001AEE10(a0, a1). */
    int (*script_1B0460)(void *context, int a0);
    int (*fade)(void *context, int a0, int a1);
    int (*fade_1AEE10)(void *context, int a0, int a1);
    /* 00184BA0(p) and 00176F90(p): *result is the return value. */
    int (*use)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*use_probe)(void *context, EmPlayerLiveActor *actor, int *result);
    /* anim_clip_arbiter(p, clip, blend, frame) (0016A8B0). */
    int (*arbiter)(void *context, EmPlayerLiveActor *actor, int clip, float blend, float frame);
    /* 00102B08(dst, src, angle): the x rotation (0016A8B0). */
    int (*rotate_x)(void *context, uint32_t out[16], const uint32_t in[16], uint32_t angle);
    /* 0017C580(p), 0021D250(p, a1), 0021D2E0(p, a1, a2). */
    int (*land)(void *context, EmPlayerLiveActor *actor);
    int (*surface5d)(void *context, EmPlayerLiveActor *actor, int a1);
    int (*teleport)(void *context, EmPlayerLiveActor *actor, int a1, int a2);
    /* 00182870(p, tier), 00182A70(p), 0017FC80(p, blend). */
    int (*land_sound)(void *context, EmPlayerLiveActor *actor, int tier);
    int (*sound_182A70)(void *context, EmPlayerLiveActor *actor);
    int (*clip_FC80)(void *context, EmPlayerLiveActor *actor, float blend);
    /* The word at *(*D_00275B40) + offset (0 or 8): a float of the current
     * skeleton's first node, read where the original loads it. */
    int (*root_node)(void *context, unsigned offset, uint32_t *bits);
    /* The word at *(p + slot) + offset (00182100: slot 0x15C / 0x160, offset
     * 0xC0 / 0xC8): a float of the node the record's pointer word names. */
    int (*bone)(void *context, EmPlayerLiveActor *actor, unsigned slot, unsigned offset,
                uint32_t *bits);
} EmPlayerClosure1019Workers;

/* The context of the state callbacks below. major2 carries D_00275B14,
 * which 00181D70, 001696A0 and 0016ADE0 write and 002230A0 reads: bind the
 * SAME EmPlayerMajor2Scene the +4 = 2 states use. */
typedef struct EmPlayerClosure1019 {
    const EmPlayerClosure1019Workers *workers;
    EmPlayerClosure1019Scene *scene;
    EmPlayerClosure1019Scratch *scratch;
    EmPlayerMajor2Scene *major2;
} EmPlayerClosure1019;

/* 1 when the context, every worker and the scene/scratch/major2 storage are
 * bound. */
int em_player_closure1019_bound(const EmPlayerClosure1019 *closure);

/* 0015B130 state[] callbacks (EmPlayerStateCallback); context is an
 * EmPlayerClosure1019. Each returns 0, or -1 on a fault. */
int em_player_closure1019_00169730(void *context, EmPlayerLiveActor *actor);   /* state[0x10] */
int em_player_closure1019_0016AE40(void *context, EmPlayerLiveActor *actor);   /* state[0x12] */
int em_player_closure1019_0016DE40(void *context, EmPlayerLiveActor *actor);   /* state[0x19] */
int em_player_closure1019_0016EBA0(void *context, EmPlayerLiveActor *actor);   /* state[0x1A] */
/* 0016A8B0, the push swing (state[0x15]; see above). */
int em_player_closure1019_0016A8B0(void *context, EmPlayerLiveActor *actor);

/* 001B0B50: D_008106BE = 1 (D_008106C8 & 1), 0x81 (& 2) or 0. */
void em_player_closure1019_001B0B50(EmPlayerClosure1019Scene *scene);

#endif
