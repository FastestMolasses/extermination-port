/* em_player_floor.h - the player's floor contact: footsteps, floor service,
 * fall entry and the radial wall probes (WP-15, FIRST_LEVEL_AUDIT P14-P18).
 *
 * Pure translations of original routines. Each works on a small mirror of
 * the actor bytes it reads and writes; every callee that the port does not
 * translate here is a worker supplied by the caller. A worker that is
 * missing or returns a negative value is a fault: the routine stops and
 * returns -1. docs/PLAYER_FLOOR.md records the evidence for each field.
 *
 * Oracles: tools/test_player_footstep_reference.py (00187350, 00187EE0,
 * 00182430), tools/test_player_floor_reference.py (00175900, 00175CF0,
 * 001796C0, 00179450, 00179680) and tools/test_player_probe_reference.py
 * (001764E0, 001760C0, 001756E0). They run the original instructions. */
#ifndef EM_PLAYER_FLOOR_H
#define EM_PLAYER_FLOOR_H

#include <stdint.h>

/* ---- 00187350 footstep dispatch --------------------------------------- */

/* The fields 00187350 and its callees 00187EE0/00182430 read or write. */
typedef struct EmPlayerStepActor {
    float position[3];   /* +B0 (the feet: 0015BCF0 has not yet published the hip) */
    float rotation[3];   /* +C0 Euler, +C4 is the body yaw */
    float clock;         /* +3C remaining source frames */
    float speed;         /* +38 */
    float slope;         /* +9C */
    float surface_y;     /* +250 floor height recorded by 00175900 */
    uint32_t anim_flags; /* +200 */
    int16_t clip;        /* +20C source clip id */
    int16_t wet;         /* +212 wet-feet timer */
    uint8_t mode;        /* +1F0 */
    uint8_t tier;        /* +25C */
    uint8_t step;        /* +25E step phase 0/1/2, or mailbox 0x80|tier */
    uint8_t surface;     /* +23A floor attribute */
    uint8_t depth;       /* +23C water depth state */
    uint8_t contact;     /* +A floor contact from 00175900 */
    uint8_t obstruction; /* +314 wall-probe lane bits */
} EmPlayerStepActor;

typedef struct EmPlayerStepWorkers {
    void *context;
    /* 00179B90: rand()&7 with 5..7 folded to 0..2. */
    int (*random5)(void *context, unsigned *value);
    /* 00122BB8: the SDK rand() value (wade ripple gate). */
    int (*random)(void *context, uint32_t *value);
    /* 001FBD50(actor, id, 0, 300.0): positional sound at the actor. */
    int (*sound)(void *context, unsigned id);
    /* 001EFD90(id, position, actor+C0). */
    int (*effect)(void *context, uint32_t id, const float position[3],
                  const float rotation[3]);
    /* 001F0460(1, M), M = identity, 00102BB0 yaw, 00102B08 pitch, row3 = position. */
    int (*decal)(void *context, const float position[3], float yaw, float pitch);
    /* 001E8B90(actor+B0, level). */
    int (*wade)(void *context, const float position[3], float level);
} EmPlayerStepWorkers;

/* Scene inputs 00187350 reads outside the actor. */
typedef struct EmPlayerStepScene {
    const float *foot17; /* world translation of skeleton node 17 (D_00275B40+0x44, +C0) */
    const float *foot18; /* node 18 (D_00275B40+0x48) */
    uint32_t frame;      /* 0x70003B68 frame counter */
    uint8_t area;        /* D_00810700 */
} EmPlayerStepScene;

/* D_00248C90 row: frameA at +2, frameB at +4. Returns 0 for rows without
 * both step frames (the dispatcher then does nothing that frame). */
int em_player_step_frames(int clip, int *frame_a, int *frame_b);

/* 00182430 surface sound id before its 00179B90 variant is added. */
unsigned em_player_step_sound_base(uint8_t surface, uint8_t depth, uint8_t tier);

/* 00187350. Returns 0, or -1 on a worker fault (fields written before the
 * fault keep their values, as the original's order leaves them). */
int em_player_footstep_tick(EmPlayerStepActor *actor, const EmPlayerStepScene *scene,
                            const EmPlayerStepWorkers *workers);

/* ---- Collision probe results shared by the floor and wall routines ------ */

/* What an original probe leaves in the scratchpad result block. */
typedef struct EmPlayerProbeHit {
    int kind;            /* return value / 0x700031D8: 0 or the set bit (1, 2, 4) */
    uint16_t node;       /* *(0x700031D0)+0x1A halfword: class 0xFF00 | attribute */
    uint8_t entity_flags;/* *(0x700031D4)+2 (0 when no entity) */
    uint8_t entity_type; /* *(0x700031D4)+3 */
    int entity;          /* 0x700031D4 != 0 */
    float point[3];      /* 0x700031B0 */
    float delta[3];      /* 0x700031C0 = point - target */
    float normal[3];     /* the hit node's plane normal (+0x24 on the node) */
    float axis[3];       /* the hit node's +0x34 vector (surface 0x35 uses x/z) */
} EmPlayerProbeHit;

/* ---- Original SDK vector math (00102BB0 family), EE truncating floats --- */

/* 001B1470. */
float em_player_sdk_wrap(float angle);
/* 001029C0, 00102BB0(M, M, wrap(yaw + offset)), 00102918(M, M, position),
 * 001026A0(out, M, local): an actor-relative point. */
void em_player_sdk_lane_point(float yaw, float offset, const float position[3],
                              const float local[4], float out[4]);

/* ---- 001796C0 fall-state check, 00179450 floor-table query --------------- */

/* The column table 0019BC40 rebuilds: D_70003170 flags, D_700030F0 heights,
 * D_00282250 aux, count at 0x700031E0. */
#define EM_PLAYER_FLOOR_TABLE_MAX 20
typedef struct EmPlayerFloorTable {
    int count;
    uint16_t flags[EM_PLAYER_FLOOR_TABLE_MAX];
    float height[EM_PLAYER_FLOOR_TABLE_MAX];
    float aux[EM_PLAYER_FLOOR_TABLE_MAX];
} EmPlayerFloorTable;

typedef struct EmPlayerFallActor {
    float position[3];   /* +B0 */
    float probe[3];      /* +280 floor-probe vector */
    float drop;          /* +2EC */
    float below;         /* +258 written by 00179450 */
    uint8_t lock;        /* +25F */
    uint8_t mode;        /* +1F0 */
    uint8_t contact;     /* +A */
    uint8_t state;       /* +5 */
    uint8_t walk;        /* +6 */
    uint8_t slide;       /* +237 */
    uint8_t row;         /* +235 */
    uint8_t special;     /* +236 */
} EmPlayerFallActor;

typedef struct EmPlayerFallWorkers {
    void *context;
    /* 0019BC40(position): the column table at the actor. */
    int (*column)(void *context, const float position[3], EmPlayerFloorTable *table);
    /* 0019AB20(actor, position, probe, mask) without the 0x80000000 bit. */
    int (*ground)(void *context, const float position[3], const float probe[3],
                  unsigned mask, EmPlayerProbeHit *hit);
} EmPlayerFallWorkers;

/* 00179450 over a rebuilt table: 0, 1 or 2 (aux >= 0.62831855). */
int em_player_floor_query(EmPlayerFallActor *actor, const EmPlayerFloorTable *table);
/* 00179680: enter the fall callback (+5=5, +6=0, +1F0=11, +25F=2). */
void em_player_fall_enter(EmPlayerFallActor *actor);
/* 001796C0. Returns 0, or -1 on a worker fault. */
int em_player_fall_check(EmPlayerFallActor *actor, const EmPlayerFallWorkers *workers);

/* ---- 00175900 floor service, 00175CF0 floor-hit apply, 0019A310 slope --- */

typedef struct EmPlayerFloorActor {
    float position[3];    /* +B0 */
    float probe[3];       /* +280 floor-probe vector */
    float yaw;            /* +C4 */
    float pitch;          /* +C0, cleared on contact */
    float slope;          /* +9C */
    float surface_y;      /* +250 */
    float conveyor_yaw;   /* +310 */
    float slide_yaw;      /* +218 */
    uint16_t surface_class; /* +238 */
    uint8_t mode;         /* +1F0 */
    uint8_t surface_mode; /* +23B */
    uint8_t surface;      /* +23A */
    uint8_t contact;      /* +A */
    uint8_t floor_hit;    /* +B */
    uint8_t depth;        /* +23C */
    uint8_t puddle;       /* +23D */
    uint8_t marsh;        /* +23E */
    uint8_t lock;         /* +25F */
    uint8_t slide;        /* +237 */
    uint8_t major;        /* +4 */
    uint8_t state;        /* +5 */
    uint8_t link;         /* +214 != 0 */
    uint8_t link_flags;   /* (+214)+2 */
    uint8_t link_type;    /* (+214)+3 */
} EmPlayerFloorActor;

typedef struct EmPlayerFloorWorkers {
    void *context;
    /* 0019AB20(actor, position, probe, mask) without the 0x80000000 bit. */
    int (*ground)(void *context, const float position[3], const float probe[3],
                  unsigned mask, EmPlayerProbeHit *hit);
    /* 0019B6C0(top, bottom). */
    int (*head)(void *context, const float top[3], const float bottom[3],
                EmPlayerProbeHit *hit);
    /* 0019B8C0(actor, at, probe, mask). */
    int (*object)(void *context, const float at[3], const float probe[3],
                  unsigned mask, EmPlayerProbeHit *hit);
    /* 00175640(+214): nonzero lets a 0x1000 floor push. */
    int (*link_test)(void *context, int *result);
    /* 0017F9E0 (0) and 0017FB90 (1): the surface-0x39 handlers. */
    int (*surface39)(void *context, int handler);
    /* 00187DC0 (0x5A), 00187DE0 (0x5B), 00187EA0 (0x5C) first contact. */
    int (*first_contact)(void *context, uint8_t surface);
    /* SDK transcendental calls (0011E620 atan2, 0011E398 cos, 0011DBB8 atan,
     * 0011E748 sqrt). The port and the oracle bind the same host models. */
    float (*atan2)(void *context, float y, float x);
    float (*cosine)(void *context, float x);
    float (*atan)(void *context, float x);
    float (*sqrt)(void *context, float x);
} EmPlayerFloorWorkers;

/* 0019A310(out): the slope angle of the hit node's normal. */
void em_player_slope_angle(const EmPlayerProbeHit *hit, const EmPlayerFloorWorkers *workers,
                           float *out);
/* 00175CF0(actor, kind, index). */
int em_player_floor_apply(EmPlayerFloorActor *actor, const EmPlayerProbeHit *hit, int index,
                          const EmPlayerFloorWorkers *workers);
/* 00175900(actor, search). Returns +A, or -1 on a worker fault. `at` is the
 * point 0019B8C0 receives (the stack copy sp50: the probe point of the floor
 * hit); when no floor hit wrote it the original passes stale stack, and the
 * caller's `at` is used. */
int em_player_floor_service(EmPlayerFloorActor *actor, int search, float at[3],
                            const EmPlayerFloorWorkers *workers);

/* ---- 001764E0 radial wall probes, 001756E0 clearance release ------------ */

typedef struct EmPlayerProbeActor {
    float position[3];    /* +B0, corrected in place by each response */
    float yaw;            /* +C4 */
    float speed;          /* +38 */
    uint8_t major;        /* +4 */
    uint8_t state;        /* +5 */
    uint8_t mode;         /* +1F0 */
    uint8_t variant;      /* +1F1 */
    uint8_t row;          /* +235 */
    uint8_t special;      /* +236 low clearance */
    uint8_t obstruction;  /* +314 lane bits */
    uint8_t contact;      /* +A (001756E0 area 0x12 gate) */
    uint8_t link;         /* +214 != 0 */
    uint8_t link_type;    /* (+214)+3 */
} EmPlayerProbeActor;

typedef struct EmPlayerProbeScene {
    uint8_t area;         /* D_00810700 */
    /* 001764E0 tests ($s1 & 4) of its caller without setting $s1. The
     * register comes unchanged from main (1 in the captured state04 register
     * file) through every ordinary player callback; 001612D0's reversal-end
     * tick leaves the 0017B490 clip id there. */
    uint8_t inherited_s1;
    uint8_t previous_obstruction; /* D_00275B00[4], written by 001764E0 */
} EmPlayerProbeScene;

typedef struct EmPlayerProbeWorkers {
    void *context;
    /* 0019AD00(actor, target, mask) without the 0x80000000 bit; the probe
     * starts at (position.x, target.y, position.z). */
    int (*move)(void *context, const float position[3], const float target[3],
                unsigned mask, EmPlayerProbeHit *hit);
    /* 0019AFE0(actor, from, to, mask) without the 0x80000000 bit. */
    int (*sweep)(void *context, const float from[3], const float to[3], unsigned mask,
                 EmPlayerProbeHit *hit);
    /* 001760C0(actor, at, 1, height) -> 0019AB20 over at .. at + (0,height,0). */
    int (*column)(void *context, const float at[3], float height, EmPlayerProbeHit *hit);
    /* 00176180(actor, kind, target): the class-2 hull shove (unported). */
    int (*hull_shove)(void *context, const float target[3]);
    /* 001762E0's area-2 target shove when its gate passes (unported). */
    int (*target_shove)(void *context);
    /* 00174A50(actor, 12.0): the idle row request after a clearance change. */
    int (*pose)(void *context, float blend);
    /* 0019A310's SDK calls (0011E748 sqrt, 0011DBB8 atan): host models. */
    float (*sqrt)(void *context, float x);
    float (*atan)(void *context, float x);
} EmPlayerProbeWorkers;

/* 00176C80: 1 when all three forward points 10 units out are clear and
 * have cover within 13.99 above. -1 on a worker fault. */
int em_player_crawl_ahead(const EmPlayerProbeActor *actor, const EmPlayerProbeWorkers *workers);
/* 001764E0. Returns 0, or -1 on a worker fault. */
int em_player_wall_probes(EmPlayerProbeActor *actor, EmPlayerProbeScene *scene,
                          const EmPlayerProbeWorkers *workers);
/* 001756E0. Returns 1 on the area-0x12 forced clearance, 0, or -1. */
int em_player_clearance_release(EmPlayerProbeActor *actor, const EmPlayerProbeScene *scene,
                                const EmPlayerProbeWorkers *workers);

#endif
