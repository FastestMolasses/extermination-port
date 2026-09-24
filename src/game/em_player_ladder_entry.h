/* em_player_ladder_entry.h - the Use surface actions and the ladder entry
 * state (docs/PLAYER_LADDER_ENTRY.md).
 *
 * Translations of the original routines, not models of them. Each works on
 * the raw 0x320-byte player record (EmPlayerLiveActor) by its original
 * offsets:
 *   0015D4C0  the surface-attribute actions of the Use chain (00160220
 *             calls it at 00160318). It switches on the attribute byte +23B:
 *             0x37/0x38 (+5 = 0x18), 0x32 (+5 = 0xB, the ladder columns),
 *             0x3B (+5 = 0xB), 0x33 (+5 = 0xD), 0x3A (+5 = 0x11 or 0xF),
 *             0x20 (+5 = 0x16) and 0x3D (+5 = 0xA). Returns 1 when an
 *             action started.
 *   00176F90  the attribute refresh: +23B from a short probe below +B0
 *   00177030  the facing gate / placement toward the attribute record
 *             (modes 0..4)
 *   00180300  the probe 10 units along +D0 that classifies an attribute
 *             (0x32, 0x3B or 0x33): 0 expected, 1 other, 2 no hit
 *   00199DB0  the centre of the hit record (grid node or cell hull)
 *   00199FA0  the two corners of the hit record
 *   00165B60  +4 = 1, +5 = 0xB: the ladder entry (0015B130 state[0xB])
 *   00176DC0  the five radial wall probes 00165B60 runs in area 2
 *   0017FC80  the clip request by +2F1 (001885D0 or 001885F0)
 *   00182A70  the climb step sound (00179B90 base + 0x109)
 *   001B61C0  the pad vibration request (EmPlayerRumble below)
 * and the two SDK copies they call, 00102948 (quadword copy) and 001031E0
 * (three-word copy).
 *
 * Every other original callee is a worker (EmPlayerLadderWorkers). A worker
 * that is missing is a fault: each entry point checks the whole worker set
 * and returns -1 before its first write. A worker that returns a negative
 * value is a fault too: the routine stops at once and returns -1, leaving
 * the writes made before the call, as the original order leaves them.
 * Reads the native world cannot give (a record field when 0x700031D0 is 0,
 * a vertex or directory entry outside the bound data, the stale stack word
 * 0015D4C0 reads after an unsuccessful 00199FA0) are faults as well; the
 * scratch's `fault` then names the original instruction.
 *
 * Arithmetic: every COP1 operation goes through em_ee_float.h (the measured
 * EE model, docs/EE_FLOAT_MODEL.md), on raw bit patterns.
 *
 * Oracle: tools/test_player_ladder_entry_reference.py executes the original
 * instructions of every routine above from the user's pinned ELF (callees
 * hooked and recorded) and compares all 0x320 actor bytes, the scratchpad
 * words, the return values and the worker call sequence; EM_TEST_WORLD=1
 * replays route beat 10 (the two cage ladder entries) over the captured RAM
 * with these routines in place of the originals. */
#ifndef EM_PLAYER_LADDER_ENTRY_H
#define EM_PLAYER_LADDER_ENTRY_H

#include <stdint.h>

#include "game/em_player_floor.h"

/* What 0x700031D0 names. */
enum {
    EM_PLAYER_LADDER_RECORD_NONE = 0,   /* 0x700031D0 == 0 */
    EM_PLAYER_LADDER_RECORD_CELL = 1,   /* 0x700031D0 == D_700030B0 (the cell prim record) */
    EM_PLAYER_LADDER_RECORD_OTHER = 2   /* any other record (a grid node) */
};

/* 0x700030F0 (heights) and 0x70003170 (flags) hold 32 entries each before
 * the next scratch block; 0015D4C0 reading entry 32 or beyond faults. */
#define EM_PLAYER_LADDER_COLUMN_MAX 32

/* The scratchpad words these routines write and read, in original order
 * (raw bits). The binder owns one instance and hands the SAME instance to
 * every worker: the collision workers write the probe state (the second
 * block) as the original probes leave the scratchpad, and 00177030 /
 * 00165B60 leave 0x70003A20.. for their callers. */
typedef struct EmPlayerLadderScratch {
    uint32_t s38A0[4];   /* 0x700038A0 */
    uint32_t s38B0[4];   /* 0x700038B0 */
    uint32_t s38C0[4];   /* 0x700038C0 */
    uint32_t s38D0[4];   /* 0x700038D0 */
    uint32_t s3A20[4];   /* 0x70003A20..0x70003A2C */
    uint32_t s3600[4];   /* 0x70003600 (00180300's offset) */
    uint32_t s3610[4];   /* 0x70003610 (00180300's probe end) */
    uint32_t s36A0[16];  /* 0x700036A0 (00176DC0's matrix) */
    /* ---- the probe state (written by the collision workers) ---- */
    uint32_t s31B0[3];   /* 0x700031B0..0x700031B8: the hit point */
    int32_t record;      /* 0x700031D0: EM_PLAYER_LADDER_RECORD_* */
    /* The 32-bit word 0x700031D0 holds (the record's address in the
     * original). 0015D4C0 case 0x3A stores it at +30C; the binder gives
     * each record an identity the readers of +30C resolve. */
    uint32_t record_word;
    /* The record's bytes +0x00..+0x3F: grid node vertex indices +0..+0xB
     * (s16), attribute +0x1A, and the floats at +0x24, +0x2C, +0x34, +0x3C.
     * For RECORD_CELL these are the scratch words 0x700030B0..0x700030EC. */
    uint8_t record_bytes[0x40];
    uint32_t entity;     /* 0x700031D4 (nonzero when an owner was hit) */
    uint16_t entity_0E;  /* that owner's +0x0E halfword */
    int32_t s31D8;       /* 0x700031D8 */
    int32_t s31E0;       /* 0x700031E0: the column entry count (0019BC40) */
    uint32_t s30F0[EM_PLAYER_LADDER_COLUMN_MAX];  /* 0x700030F0: column heights */
    uint16_t s3170[EM_PLAYER_LADDER_COLUMN_MAX];  /* 0x70003170: column flags */
    /* Port-side diagnostic, not an original word: 0, or the original
     * instruction address of the first read the native world cannot give. */
    uint32_t fault;
} EmPlayerLadderScratch;

/* Data the routines read outside the actor and the scratch. The binder
 * keeps it current (the bytes are the originals' globals). */
typedef struct EmPlayerLadderWorld {
    /* *0x70003250: the cell directory in its original byte layout (count
     * word, one offset word per uid, then the hulls); EmActorCellTable
     * bytes/size. NULL faults where 00199DB0 / 00199FA0 read it. */
    const uint8_t *directory;
    uint32_t directory_size;
    /* *0x700031FC: the grid vertex pool, three raw words per vertex
     * (EmCollProbeGrid verts / vert_count). */
    const uint32_t *verts;
    uint32_t vert_count;
    uint8_t area;        /* D_00810700 (00165B60 cases 0 and the area-2 tail) */
    uint8_t d810C7C;     /* D_00810C7C: 0015D4C0 case 0x20 gate */
    uint8_t d810C7D;     /* D_00810C7D: 0015D4C0 case 0x33 gate */
    int16_t d2754D0;     /* D_002754D0[0]: the clip 00165B60 requests on hand-off */
} EmPlayerLadderWorld;

typedef struct EmPlayerLadderWorkers {
    void *context;
    EmPlayerLadderScratch *scratch;
    const EmPlayerLadderWorld *world;
    /* ---- collision probes: each writes the probe state in `scratch` as the
     * original leaves the scratchpad; *result is the return value. Vector
     * arguments point at the scratch vector the original passes, or at a
     * copy of the actor words it passes. ---- */
    /* 0019BA80(p, point, p+280, mask): 00176F90's probe; `box` is +280..+28C. */
    int (*probe_0019BA80)(void *context, EmPlayerLiveActor *actor, const uint32_t point[4],
                          const uint32_t box[4], int mask, int *result);
    /* 0019AD00(p, target, mask). */
    int (*move_0019AD00)(void *context, EmPlayerLiveActor *actor, const uint32_t target[4],
                         int mask, int *result);
    /* 0019A570(from, to, mask, id). */
    int (*segment_0019A570)(void *context, const uint32_t from[4], const uint32_t to[4],
                            int mask, int id, int *result);
    /* 0019AFE0(p, from, to, mask). */
    int (*sweep_0019AFE0)(void *context, EmPlayerLiveActor *actor, const uint32_t from[4],
                          const uint32_t to[4], int mask, int *result);
    /* 0019BC40(at, record): the column table at `at` for the record
     * 0x700031D0 names (the scratch's record); writes 0x700031E0, the
     * heights and the flags. */
    int (*column_0019BC40)(void *context, const uint32_t at[4]);
    /* ---- SDK VU0 routines (raw words). An output may be the same array as
     * an input where the original passes one address for both (each VU0
     * routine loads its inputs before it stores). ---- */
    /* build_trs_matrix (001C94B0)(p+D0, p+B0, p+C0, p+60). */
    int (*trs)(void *context, uint32_t out[16], const uint32_t position[4],
               const uint32_t rotation[4], const uint32_t scale[4]);
    /* 001026A0(out, M, v): out = v x M. */
    int (*apply)(void *context, uint32_t out[4], const uint32_t matrix[16], const uint32_t v[4]);
    /* 001028B8(out, a, b): out = a + b (four lanes). */
    int (*vadd)(void *context, uint32_t out[4], const uint32_t a[4], const uint32_t b[4]);
    /* 001029C0(m): identity. */
    int (*identity)(void *context, uint32_t m[16]);
    /* 00102C58(out, in, angles). */
    int (*euler)(void *context, uint32_t out[16], const uint32_t in[16], const uint32_t angles[4]);
    /* 00102918(out, in, v): rows 0..2 copied, row 3 = in row 3 + v. */
    int (*translate)(void *context, uint32_t out[16], const uint32_t in[16], const uint32_t v[4]);
    /* 00102BB0(out, in, angle): the Y rotation. */
    int (*rotate_y)(void *context, uint32_t out[16], const uint32_t in[16], uint32_t angle);
    /* 00102738(a, b): the xyz dot product (returned in $f0). */
    int (*dot)(void *context, const uint32_t a[4], const uint32_t b[4], uint32_t *out);
    /* 00102760(out, in): xyz normalized, w = 0. */
    int (*normalize)(void *context, uint32_t out[4], const uint32_t in[4]);
    /* ---- SDK scalar routines (raw bits in, raw bits out) ---- */
    int (*atan2_0011E620)(void *context, uint32_t y, uint32_t x, uint32_t *out);
    int (*wrap_001B1470)(void *context, uint32_t x, uint32_t *out);
    int (*fabs_0011DF78)(void *context, uint32_t x, uint32_t *out);
    int (*cos_0011DE90)(void *context, uint32_t x, uint32_t *out);
    int (*sqrt_0011E748)(void *context, uint32_t x, uint32_t *out);
    /* ---- the actor services ---- */
    /* 001749A0(p, clip, force, blend). The return value is not read.
     * (EmPlayerLandWorkers.request has this signature.) */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int force, float blend);
    /* 001FBD50(p, id, 0, 300.0). (EmPlayerLandWorkers.sound.) */
    int (*sound)(void *context, EmPlayerLiveActor *actor, int id);
    /* 00179B90(p): the actor's sound base. */
    int (*sound_base_00179B90)(void *context, EmPlayerLiveActor *actor, int *result);
    /* 001762E0(p): 00176DC0's test after a cell hit (nonzero stops it). */
    int (*wall_001762E0)(void *context, EmPlayerLiveActor *actor, int *result);
    /* The node *(D_00275B40 + 4): its words +C0..+CC, as they are when
     * 00165B60 reads them (raw bits). A data read, not a call. */
    int (*node)(void *context, uint32_t out[4]);
} EmPlayerLadderWorkers;

/* 1 when every worker, the scratch and the world are bound. */
int em_player_ladder_workers_bound(const EmPlayerLadderWorkers *workers);

/* 0015D4C0(p). *result is its return value (1 when an action started). */
int em_player_ladder_0015D4C0(const EmPlayerLadderWorkers *workers, EmPlayerLiveActor *actor,
                              int *result);
/* 00176F90(p). *result is the refreshed +23B. */
int em_player_ladder_00176F90(const EmPlayerLadderWorkers *workers, EmPlayerLiveActor *actor,
                              int *result);
/* 00177030(p, mode). *result is its return value (0 or 1). */
int em_player_ladder_00177030(const EmPlayerLadderWorkers *workers, EmPlayerLiveActor *actor,
                              int mode, int *result);
/* 00180300(p, v, check): v is the vector the caller passes (both callers
 * pass 0x700038A0, the scratch's s38A0). *result is 0, 1 or 2. */
int em_player_ladder_00180300(const EmPlayerLadderWorkers *workers, EmPlayerLiveActor *actor,
                              const uint32_t v[4], int check, int *result);
/* The same 00180300 for callers outside this lane (the closure states,
 * em_player_closure_0e_18.c): it refuses (-1, nothing written) only when
 * what its own instructions reach is missing: the scratch, apply, vadd and
 * sweep_0019AFE0. This is the only translation of 00180300. */
int em_player_ladder_probe_00180300(const EmPlayerLadderWorkers *workers,
                                    EmPlayerLiveActor *actor, const uint32_t v[4], int check,
                                    int *result);
/* 00199DB0(out) and 00199FA0(a, b) over the probe state. They write all
 * their output words or none (the original writes them one by one, with
 * nothing between that can fail). *result is 1 or 0. */
int em_player_ladder_00199DB0(const EmPlayerLadderWorld *world, EmPlayerLadderScratch *scratch,
                              uint32_t out[3], int *result);
int em_player_ladder_00199FA0(const EmPlayerLadderWorld *world, EmPlayerLadderScratch *scratch,
                              uint32_t a[3], uint32_t b[3], int *result);
/* 00165B60(p): the state-0xB routine. */
int em_player_ladder_00165B60(const EmPlayerLadderWorkers *workers, EmPlayerLiveActor *actor);
/* 00176DC0(p). */
int em_player_ladder_00176DC0(const EmPlayerLadderWorkers *workers, EmPlayerLiveActor *actor);
/* 0017FC80(p, blend): translated once, in em_player_ladder_climb.c (with
 * 001885D0 / 001885F0, the D_002754D0 / D_002754D4 rows); this runs that
 * translation over this lane's `request` worker. */
int em_player_ladder_0017FC80(const EmPlayerLadderWorkers *workers, EmPlayerLiveActor *actor,
                              float blend);
/* 00182A70(p). */
int em_player_ladder_00182A70(const EmPlayerLadderWorkers *workers, EmPlayerLiveActor *actor);

/* The state callback (EmPlayerStateCallback): context is a
 * const EmPlayerLadderWorkers *. Bind as EmPlayerStageWorkers.state[0xB].
 * Returns 0, or -1 on a fault. */
int em_player_ladder_state_b(void *workers, EmPlayerLiveActor *actor);   /* 00165B60 */

/* ---- 001B61C0: the pad vibration request -------------------------------
 * The pad block D_00810E40 by its original offsets. */
typedef struct EmPlayerRumblePad {
    int32_t port;          /* +0x04 */
    int32_t slot;          /* +0x08 */
    uint8_t ready;         /* +0x12 */
    uint8_t active;        /* +0x16 */
    uint8_t act[6];        /* +0x18..+0x1D: the actuator block (+0x18 big
                            * motor on, +0x19 small motor power) */
    uint16_t duration;     /* +0x28 */
} EmPlayerRumblePad;

typedef struct EmPlayerRumble {
    EmPlayerRumblePad *pad;       /* D_00810E40 */
    const uint8_t *enable;        /* D_00810119: vibration on (options) */
    const uint8_t *mode;          /* D_00275BE0: 2 blocks the request */
    void *context;
    /* 00111018(port, slot, &pad+0x18): the libpad actuator submission (the
     * IOP boundary). The return value is not read. */
    int (*actuator)(void *context, int port, int slot, const uint8_t act[6]);
} EmPlayerRumble;

/* 001B61C0(big, small, duration, force). Returns 0, or -1 when the pad,
 * the two bytes or the actuator worker is missing (nothing written). */
int em_player_rumble_001B61C0(const EmPlayerRumble *rumble, int big, int small, int duration,
                              int force);
/* The same as a worker slot of the form int (*)(void *, int, int, int, int)
 * (EmPlayerLandWorkers.rumble, EmPlayerStageWorkers' cue): context is a
 * const EmPlayerRumble *. */
int em_player_rumble_worker(void *rumble, int big, int small, int duration, int force);

#endif
