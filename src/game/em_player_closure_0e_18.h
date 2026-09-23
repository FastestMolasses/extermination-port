/* em_player_closure_0e_18.h - four FLOOR-closure player states
 * (docs/PLAYER_CLOSURE_0E_18.md).
 *
 * Translations of the original routines, not models of them. Each works on
 * the raw 0x320-byte player record (EmPlayerLiveActor) by its original
 * offsets:
 *   00168050  +4 = 1, +5 = 0xE   (0015B130 state[0xE]; entered by the hang
 *             001647D0 sub-state 0x23 at 0016575C)
 *   0016B790  +4 = 1, +5 = 0x13  (0015B130 state[0x13]; entered by 001696A0)
 *   0016B8A0  +4 = 1, +5 = 0x14  (0015B130 state[0x14]; entered by 0016B790
 *             and 00223C70)
 *   0016D130  +4 = 1, +5 = 0x18  (0015B130 state[0x18]; entered by the hang
 *             001647D0 sub-state 0x23 at 001657E8)
 * and the private callees they reach, also translated here:
 *   00180000 (with its body 00180004), 00180040, 00180080, 001800C0,
 *   00180100, 00180180, 00180200, 00180280   the side clip requests
 *   00180420  the reach point (+290) from the +D0 matrix
 *   00180300  the reach sweep (surface byte to +23B)
 *   001806E0, 00180790, 00180850             00168050's reach tests
 *   00182AB0  the random step sound 0x11B..0x11F
 *   00174AB0  001749A0(p, 0, 1, 0.0)
 *   00178620  0016D130's wall test (surface 0x3D / 0x3B)
 *   00179150  the forward step along +C4, then 001790B0
 *   001790B0  the seven D_00248970 point tests into +314
 *   0016BAE0  0016B790's helper spawn (001AFA90(8))
 * Already-translated originals are called, not re-translated:
 *   00181180  em_player_major2_00181180 (em_player_major2.c)
 *   0017F240  em_player_hang_0017F240 (em_player_hang.c)
 *   0011DF78  em_sdk_math_original_0011DF78 (em_sdk_math_original.c)
 * 00102948 (quadword copy) and 001031E0 (three-word copy) are moves; they
 * are written out in place.
 *
 * Every other original callee is a worker (EmPlayerClosureWorkers). A worker
 * that is missing is a fault: each entry point checks the whole worker set
 * and returns -1 before its first write. A worker that returns a negative
 * value is a fault too: the routine stops at once and returns -1, leaving
 * the writes made before the call, as the original order leaves them. A
 * table index outside an original table (+23F > 3, +2F1 > 1) faults at the
 * read instead of reading neighbouring data.
 *
 * Arithmetic: every COP1 operation goes through em_ee_float.h (the measured
 * EE model, docs/EE_FLOAT_MODEL.md), on raw bit patterns.
 *
 * Oracle: tools/test_player_closure_0e_18_reference.py executes the original
 * instructions of every routine above from the captured RAM (checked against
 * the user's pinned ELF), with every worker hooked and scripted, and compares
 * all 0x320 actor bytes, the scratchpad words, the globals, the spawned node
 * and the worker call sequence with every argument. */
#ifndef EM_PLAYER_CLOSURE_0E_18_H
#define EM_PLAYER_CLOSURE_0E_18_H

#include <stdint.h>

#include "game/em_player_floor.h"

/* Globals the routines read, fetched through EmPlayerClosureWorkers.scene at
 * the point of each original read. */
typedef struct EmPlayerClosureScene {
    uint16_t pad;        /* D_00810E74[0] (the pad's held buttons) */
    uint16_t use_mask;   /* spad 0x70003B76 (ANDed with pad: the Use test) */
    uint8_t area;        /* D_00810700 */
    int16_t fade;        /* D_0028A9A0 (halfword, sign-extended) */
    int32_t d275B14;     /* D_00275B14 (00181D70 stores 0x34, 0x36 or 0x1E) */
    uint32_t d275B0C;    /* D_00275B00 + 0xC (binary32 bits) */
    uint32_t d275B10;    /* D_00275B10 (binary32 bits) */
    uint32_t d281B64;    /* D_00281B64[0] (binary32 bits) */
} EmPlayerClosureScene;

/* The scratchpad words these routines write. The binder owns one instance;
 * the workers that the original lets read them see them by value (every
 * worker below that takes a vector receives the words the original passes). */
typedef struct EmPlayerClosureScratch {
    uint32_t s3600[8];   /* 0x70003600..0x7000361F (00180300) */
    uint32_t s38A0[16];  /* 0x700038A0..0x700038DF */
    uint32_t s3A20;      /* 0x70003A20 */
} EmPlayerClosureScratch;

typedef struct EmPlayerClosureWorkers {
    void *context;
    EmPlayerClosureScratch *scratch;

    /* ---- reads and stores outside the record ---------------------------- */
    /* The EmPlayerClosureScene words, at each original read. */
    int (*scene)(void *context, EmPlayerClosureScene *scene);
    /* *(uint32_t *)(*(D_00275B40 + 4 * node) + offset): a skeleton node word
     * (node 0 +4/+8; node 1 +C0/+C4/+C8/+CC), read when the original reads
     * it. D_00275B40 is the player's own +40 (docs/PLAYER_HANG.md). */
    int (*node)(void *context, int node, unsigned offset, uint32_t *bits);
    /* *(uint8_t *)(*(spad 0x700031D0) + 0x1A): the surface byte of the hit
     * the last sweep left, read only after a sweep returned nonzero. */
    int (*hit_surface)(void *context, uint8_t *surface);
    /* The stores D_00275B00 + 8 = value (0016B790, 0016B8A0) and
     * D_00810702 = value (0016D130). */
    int (*set_275B08)(void *context, int32_t value);
    int (*set_810702)(void *context, uint8_t value);

    /* ---- callees taking the record (each may read and write any byte) ---- */
    /* 001749A0(p, clip, force, blend); its return value is not read. */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int force, float blend);
    /* 001FBD50(p, id, flags, range); its return value is not read. */
    int (*sound)(void *context, EmPlayerLiveActor *actor, int id, int flags, float range);
    int (*steer)(void *context, EmPlayerLiveActor *actor);                      /* 00174FD0(p) */
    /* 001809B0(p, side): *result = its return value. */
    int (*ledge_move)(void *context, EmPlayerLiveActor *actor, int side, int *result);
    /* 0017FF80(p, blend), 0017FC80(p, blend). */
    int (*clip_FF80)(void *context, EmPlayerLiveActor *actor, float blend);
    int (*clip_FC80)(void *context, EmPlayerLiveActor *actor, float blend);
    /* The side clip requests (p, side, blend): 0017DFB0, 0017E0D0, 0017E150,
     * 0017E1D0; side is the +2F1 byte. */
    int (*clip_DFB0)(void *context, EmPlayerLiveActor *actor, int side, float blend);
    int (*clip_E0D0)(void *context, EmPlayerLiveActor *actor, int side, float blend);
    int (*clip_E150)(void *context, EmPlayerLiveActor *actor, int side, float blend);
    int (*clip_E1D0)(void *context, EmPlayerLiveActor *actor, int side, float blend);
    int (*surface_sound)(void *context, EmPlayerLiveActor *actor, int gait);    /* 00182430(p, gait) */
    int (*sound_109)(void *context, EmPlayerLiveActor *actor);                  /* 00182A70(p) */
    int (*land_sound)(void *context, EmPlayerLiveActor *actor, int tier);       /* 00182870(p, tier) */
    /* 00175900(p, search): *result = its return value (not read here). */
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    int (*place)(void *context, EmPlayerLiveActor *actor);   /* 00187EE0(p, p + B0, p + D0) */
    int (*pose_reset)(void *context, EmPlayerLiveActor *actor, float blend);    /* 00174A50(p, blend) */
    int (*skeleton)(void *context, EmPlayerLiveActor *actor);                   /* 001C68C0(p) */
    int (*translate)(void *context, EmPlayerLiveActor *actor, int arg);         /* 00178B90(p, arg) */
    /* anim_clip_arbiter(p, clip, blend, frame). */
    int (*arbiter)(void *context, EmPlayerLiveActor *actor, int clip, float blend, float frame);
    /* 001607D0(p): *result = its return value. */
    int (*use_test)(void *context, EmPlayerLiveActor *actor, int *result);
    /* 00188550(p) and 001885B0(p): *clip = the (sign-extended halfword)
     * return value. */
    int (*clip_row)(void *context, EmPlayerLiveActor *actor, int *clip);
    int (*clip_row_B)(void *context, EmPlayerLiveActor *actor, int *clip);
    /* 00178390(p) (surface 0x3D) and 001782A0(p) (surface 0x3B): *result =
     * the return value. Both read the hit the preceding sweep left (spad
     * 0x700031B0/0x700031D0): the binder shares that state with `sweep`. */
    int (*ledge_3D)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*ledge_3B)(void *context, EmPlayerLiveActor *actor, int *result);
    /* 0019AFE0(p, from, to, mask): *result = its return value; it must leave
     * the hit record that hit_surface / ledge_3D / ledge_3B then read. */
    int (*sweep)(void *context, EmPlayerLiveActor *actor, const uint32_t from[4],
                 const uint32_t to[4], unsigned mask, int *result);
    /* 0019AB20(p, point, p + 280, mask): the ground probe; it writes the
     * record's +280.. itself. *result = its return value. */
    int (*ground)(void *context, EmPlayerLiveActor *actor, const uint32_t point[4], unsigned mask,
                  int *result);
    /* 0019AD00(p, point, mask): *result = its return value. */
    int (*point_test)(void *context, EmPlayerLiveActor *actor, const uint32_t point[4], unsigned mask,
                      int *result);

    /* ---- callees not taking the record ---------------------------------- */
    int (*random)(void *context, int *result);                                   /* 00179B90() */
    /* 001C61D0(bank, clip) with bank = the word at +40: *frames = its return. */
    int (*clip_frames)(void *context, uint32_t bank, int clip, int32_t *frames);
    /* float_to_int (001281C0)(x): *result = its return value. */
    int (*to_int)(void *context, uint32_t x, int32_t *result);
    int (*fade)(void *context, int a, int b);                                    /* 001AEDE0(a, b) */
    int (*fade_end)(void *context, int a, int b);                                /* 001AEE10(a, b) */
    int (*camera)(void *context, int a);                                         /* 001B0460(a) */
    /* 001AFA90(cls): *node = a byte view (at least 0xD0 bytes, original
     * offsets) of the node it allocated, or NULL when it returned 0. */
    int (*alloc)(void *context, int cls, uint8_t **node);
    /* build_trs_matrix(p + D0, p + B0, p + C0, p + 60): out is written to
     * the record's +D0 by the caller. */
    int (*trs)(void *context, uint32_t out[16], const uint32_t position[3],
               const uint32_t rotation[3], const uint32_t scale[3]);
    /* 001026A0(out, M, v): out = v x M. */
    int (*transform)(void *context, const uint32_t matrix[16], const uint32_t v[4], uint32_t out[4]);
    /* 001028B8(out, a, b): out = a + b (VU0 vadd.xyzw). */
    int (*vadd)(void *context, const uint32_t a[4], const uint32_t b[4], uint32_t out[4]);
    /* 0011E2A8 sinf and 0011DE90 cosf (em_sdk_math_original_w_* fit). */
    int (*sine)(void *context, float x, float *result);
    int (*cosine)(void *context, float x, float *result);
    /* 001B1470(x) (angle wrap) and 001B12B0(target, current, rate), on bits. */
    int (*wrap)(void *context, uint32_t x, uint32_t *out);
    int (*approach)(void *context, uint32_t target, uint32_t current, uint32_t rate, uint32_t *out);
} EmPlayerClosureWorkers;

/* 1 when every worker and the scratch are bound. */
int em_player_closure_workers_bound(const EmPlayerClosureWorkers *workers);

/* The state callbacks (EmPlayerStateCallback): context is a
 * const EmPlayerClosureWorkers *. Bind as EmPlayerStageWorkers.state[0xE],
 * state[0x13], state[0x14] and state[0x18]. Each returns 0, or -1 on a fault. */
int em_player_closure_state0E(void *workers, EmPlayerLiveActor *actor);   /* 00168050 */
int em_player_closure_state13(void *workers, EmPlayerLiveActor *actor);   /* 0016B790 */
int em_player_closure_state14(void *workers, EmPlayerLiveActor *actor);   /* 0016B8A0 */
int em_player_closure_state18(void *workers, EmPlayerLiveActor *actor);   /* 0016D130 */

/* The private callees, for other translations that call them (001662D0,
 * 0016DE40, 0016BC40, 001834E0, 0015D4C0, 00165B60 also reach some). Each
 * returns 0 (or -1 on a fault); *result is the original's return value.
 * blend is the caller's $f12 (binary32 bits). */
int em_player_closure_00180000(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, uint32_t blend);
int em_player_closure_00180040(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, uint32_t blend);
int em_player_closure_00180080(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, uint32_t blend);
int em_player_closure_001800C0(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, uint32_t blend);
int em_player_closure_00180100(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int sel,
                               uint32_t blend);
int em_player_closure_00180180(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int sel,
                               uint32_t blend);
int em_player_closure_00180200(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int sel,
                               uint32_t blend);
int em_player_closure_00180280(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int sel,
                               uint32_t blend);
int em_player_closure_00180420(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a);
/* 00180300(p, v, kind): v is the four words the original passes by pointer. */
int em_player_closure_00180300(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a,
                               const uint32_t v[4], int kind, int *result);
int em_player_closure_001806E0(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int *result);
int em_player_closure_00180790(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int *result);
int em_player_closure_00180850(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int flag,
                               int *result);
int em_player_closure_00182AB0(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a);
int em_player_closure_00174AB0(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a);
int em_player_closure_00178620(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int side,
                               int *result);
int em_player_closure_00179150(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a);
int em_player_closure_001790B0(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int *result);
int em_player_closure_0016BAE0(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int arg);

/* The original's update routine stored at the spawned node's +10 by
 * 0016BAE0 (the address of 00188340, as the 32-bit word the node holds). */
#define EM_PLAYER_CLOSURE_SPAWN_UPDATE UINT32_C(0x00188340)

#endif
