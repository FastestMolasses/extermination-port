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
#include <string.h>

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
    /* The 0x700031D4 value this probe left: the hit owner (the pool record,
     * an EmActor * in the port), or NULL. 0019AB20 keeps the last cell owner
     * hit even when the grid then wins (kind 4). 00175CF0 stores it in the
     * player's +214 (D_008104C4) when kind & 2 (docs/ACTOR_COLLISION.md
     * section 7 item 4). `entity` is owner != NULL. */
    const void *owner;
} EmPlayerProbeHit;

/* ---- Original SDK vector math (00102BB0 family), EE truncating floats --- */

/* 001B1470. */
float em_player_sdk_wrap(float angle);
/* 001029C0, 00102BB0(M, M, wrap(yaw + offset)), 00102918(M, M, position),
 * 001026A0(out, M, local): an actor-relative point. */
void em_player_sdk_lane_point(float yaw, float offset, const float position[3],
                              const float local[4], float out[4]);
/* 001029C0, 00102BB0(M, M, angle) with the angle as given (no wrap), then
 * 00102918(M, M, position) when position is not NULL, and 001026A0(out, M,
 * local). Used by the slide (0016CD70, 0016C570) and climb routines. */
void em_player_sdk_yaw_transform(float angle, const float position[3],
                                 const float local[4], float out[4]);
/* build_trs_matrix: 001029C0, 00102B08(rot x), 00102BB0(rot y),
 * 00102A60(rot z), rows 0..2 scaled by scale x/y/z, 00102918(position). */
void em_player_sdk_trs(float out[16], const float position[3], const float rotation[3],
                       const float scale[3]);
/* 001029C0 then 00102BB0(M, M, angle). */
void em_player_sdk_yaw_matrix(float angle, float out[16]);
/* 001026A0(out, M, local). */
void em_player_sdk_apply(const float matrix[16], const float local[4], float out[4]);

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
    uint8_t link_flags;   /* (+214)+2 */
    uint8_t link_type;    /* (+214)+3 */
    /* +214 (D_008104C4): the owner the player stands on, or NULL. 0015BA50
     * moves it to +308 and clears it at the start of every player stage;
     * 00175CF0 stores a probe's 0x700031D4 here (when kind & 2) and reads it
     * back in the same call (00175640, the contact |= 0x80 test). A binder
     * that seeds a non-NULL value also seeds link_flags/link_type from that
     * owner's +2/+3. */
    const void *link_owner;
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
    /* 00175640(*(+214)): nonzero lets a 0x1000 floor push. `owner` is the
     * value 00175CF0 stored in +214 (link_owner), NULL included. */
    int (*link_test)(void *context, const void *owner, int *result);
    /* 0017F9E0 (0) and 0017FB90 (1): the surface-0x39 handlers. */
    int (*surface39)(void *context, int handler);
    /* 00187DC0 (0x5A), 00187DE0 (0x5B), 00187EA0 (0x5C) first contact. */
    int (*first_contact)(void *context, uint8_t surface);
    /* SDK transcendental calls: 0011E620 atan2f, 0011E398 tanf (00175CF0's
     * t = tanf(+9C) on the slope; 0011E398 is the tangent, not a cosine:
     * docs/SDK_MATH_ORIGINAL.md 6.3), 0011DBB8 atanf, 0011E748 sqrtf. */
    float (*atan2)(void *context, float y, float x);
    float (*tangent)(void *context, float x);
    float (*atan)(void *context, float x);
    float (*sqrt)(void *context, float x);
} EmPlayerFloorWorkers;

/* 00175640(owner) over the owner bytes it reads: 1 when `present` and the
 * type byte +3 is 0xC, 0x2A, 0xA, 0x18 or 2, or the behaviour word +0x10 is
 * 00156F30, 00827880 or 00828700; else 0 (NULL gives 0). Byte-matched in
 * the decomp (src/func_00175640.c); the floor oracle executes it. */
static inline int em_player_link_00175640(int present, uint8_t type, uint32_t behaviour)
{
    if (!present) return 0;
    if (type == 0x0C || type == 0x2A || type == 0x0A || type == 0x18) return 1;
    if (behaviour == 0x00156F30u || behaviour == 0x00827880u || behaviour == 0x00828700u)
        return 1;
    return type == 2;
}
/* The same, exported for the oracle. */
int em_player_floor_link_test(int present, uint8_t type, uint32_t behaviour);

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

/* ---- The live player actor (em_player.c "Live player states") ----------
 * The player actor record in its original layout: every translated state
 * routine reads and writes its mirror by these offsets, so one byte image
 * carries them all between routines and stages. The pointer words +214
 * (D_008104C4) and +308 hold port pointers (the owner's EmActor) and are kept
 * beside the image, with the owner's +2/+3 bytes cached when +214 is stored
 * (00175CF0 reads them through the pointer). */
#define EM_PLAYER_ACTOR_SIZE 0x320
typedef struct EmPlayerLiveActor {
    uint8_t bytes[EM_PLAYER_ACTOR_SIZE];
    const void *link_owner;   /* +214 */
    const void *link_prev;    /* +308 (0015BA50: the previous stage's +214) */
    uint8_t link_flags;       /* (+214)+2 */
    uint8_t link_type;        /* (+214)+3 */
} EmPlayerLiveActor;

/* A +5 state callback (0015B130's table) over the live actor: 0, or a
 * negative value on a fault. */
typedef int (*EmPlayerStateCallback)(void *context, EmPlayerLiveActor *actor);

static inline float em_live_f32(const EmPlayerLiveActor *a, unsigned at)
{
    float v; memcpy(&v, a->bytes + at, 4); return v;
}
static inline void em_live_set_f32(EmPlayerLiveActor *a, unsigned at, float v)
{
    memcpy(a->bytes + at, &v, 4);
}
static inline uint32_t em_live_u32(const EmPlayerLiveActor *a, unsigned at)
{
    uint32_t v; memcpy(&v, a->bytes + at, 4); return v;
}
static inline void em_live_set_u32(EmPlayerLiveActor *a, unsigned at, uint32_t v)
{
    memcpy(a->bytes + at, &v, 4);
}
static inline uint16_t em_live_u16(const EmPlayerLiveActor *a, unsigned at)
{
    uint16_t v; memcpy(&v, a->bytes + at, 2); return v;
}
static inline void em_live_set_u16(EmPlayerLiveActor *a, unsigned at, uint16_t v)
{
    memcpy(a->bytes + at, &v, 2);
}
static inline uint8_t em_live_u8(const EmPlayerLiveActor *a, unsigned at) { return a->bytes[at]; }
static inline void em_live_set_u8(EmPlayerLiveActor *a, unsigned at, uint8_t v) { a->bytes[at] = v; }

/* The floor-service and fall-check mirrors over the live actor (offsets as
 * in the field comments; tools/test_player_floor_reference.py checks them
 * against its original-verified offset tables). */
void em_player_floor_actor_from_live(const EmPlayerLiveActor *live, EmPlayerFloorActor *out);
void em_player_floor_actor_to_live(const EmPlayerFloorActor *in, EmPlayerLiveActor *live);
void em_player_fall_actor_from_live(const EmPlayerLiveActor *live, EmPlayerFallActor *out);
void em_player_fall_actor_to_live(const EmPlayerFallActor *in, EmPlayerLiveActor *live);

/* ---- The player stage around the state callbacks -----------------------
 * 0015BCF0 -> 0015BA50 -> the +4 handler (0015B130 for +4 = 1, 0015B770 for
 * +4 = 2, ...) over the live actor. Translated here so that the floor
 * oracle executes the original routines against them
 * (tools/test_player_floor_reference.py "stage" cases). Every callee the
 * port does not translate is a worker; a missing worker or a negative
 * return is a fault (-1). */

#define EM_PLAYER_MAJOR_COUNT 7      /* 0015BA50's jump table: +4 = 0..6 */
#define EM_PLAYER_STATE1_COUNT 0x26  /* 0015B130's table: +5 = 0..0x25 (0x25 empty) */
#define EM_PLAYER_STATE2_COUNT 0x1A  /* 0015B770's table: +5 = 0..0x19 */

/* What the stage reads or writes outside the actor. The first three bytes
 * and `busy` are a per-stage view (the binder loads them before
 * em_player_stage_begin and stores what the stage writes after
 * em_player_stage_end). D_008106F1 and D_00810CB6 are pointers at their one
 * canonical byte (EmSceneState request block / D2 progress region), which
 * every translation that reads or writes them shares: 0021C270 sets
 * D_008106F1 in the middle of a stage and later code of the same stage
 * reads it (SCENE_COORDINATOR_DESIGN.md, D2; PLAYER_STAGE_WORKERS.md 2). */
typedef struct EmPlayerStageScene {
    uint8_t spad3B8D; /* 0x70003B8D: scripted takeover (0015B130's prelude and case 0x19) */
    uint8_t spad3B8F; /* 0x70003B8F: 0015BA50's +94 gate; 0015B130 writes 1 on +5 = 0x19 */
    uint8_t area;     /* D_00810700 */
    uint8_t busy;     /* D_008106B3: 0015BA50 clears it before the switch and sets it after */
    uint8_t *d8106F1;        /* D_008106F1: 0015BA50's busy test reads it; 0021C270 sets it */
    const uint8_t *d810CB6;  /* D_00810CB6: read by 0015BA50's busy test */
} EmPlayerStageScene;

typedef struct EmPlayerStageWorkers {
    void *context;
    /* D_00248C98[+20C * 3]: the rate float at +8 of the D_00248C90 row. */
    int (*clip_rate)(void *context, int clip, float *rate);
    /* anim_advance_time(p, step) (001C64F0); *flags is its result (+200). */
    int (*advance)(void *context, EmPlayerLiveActor *actor, float step, uint32_t *flags);
    /* 00183090(p), the +4 = 4 scripted-clip commit test. */
    int (*commit)(void *context, EmPlayerLiveActor *actor, int *result);
    /* 0021C440(p): the damage reaction; *result is its return value. */
    int (*reaction)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*drain)(void *context, EmPlayerLiveActor *actor);      /* 0015D100 */
    int (*heartbeat)(void *context, EmPlayerLiveActor *actor);  /* 0015D000 */
    /* 00182B30(p), 00182D70(p) and 00174A50(p, blend): 0015B130's prelude. */
    int (*scripted_check)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*scripted_notify)(void *context, EmPlayerLiveActor *actor);
    int (*row_request)(void *context, EmPlayerLiveActor *actor, float blend);
    /* 0011A070(handle): 0015BCF0's loop-sound stop. */
    int (*stop_sound)(void *context, int handle);
    /* 0015BA50's +4 handlers: [0] 0015C420, [1] 0015B130, [2] 0015B770,
     * [4] 0015B530, [5] 0015B610, [6] 0015D460 (+4 = 3 is a no-op). The
     * translations em_player_stage_0015B130 / _0015B770 / _0015D460 below
     * fit this signature. */
    EmPlayerStateCallback major[EM_PLAYER_MAJOR_COUNT];
    void *major_context[EM_PLAYER_MAJOR_COUNT];
    /* 0015B130's per-state routines, by +5 (entry 0x19 is 0016DE40, which
     * 0015B130 calls only outside the scripted takeover). */
    EmPlayerStateCallback state[EM_PLAYER_STATE1_COUNT];
    void *state_context[EM_PLAYER_STATE1_COUNT];
    /* 0015B770's per-state routines, by +5 (entry 0x19 is 002255C0, after
     * 0015B770's own +1 = 0). +5 = 0xD and 0xE dispatch on +D instead:
     * phase13[0..4] and phase14[0..3]. */
    EmPlayerStateCallback state2[EM_PLAYER_STATE2_COUNT];
    void *state2_context[EM_PLAYER_STATE2_COUNT];
    EmPlayerStateCallback phase13[5];
    void *phase13_context[5];
    EmPlayerStateCallback phase14[4];
    void *phase14_context[4];
} EmPlayerStageWorkers;

/* The context of the 0015B130 / 0015B770 callbacks. */
typedef struct EmPlayerStage {
    EmPlayerStageScene *scene;
    const EmPlayerStageWorkers *workers;
} EmPlayerStage;

/* 0015BA50 before its switch: +34 = D_00248C98[+20C] * +204, +204 = 1.0,
 * +303 = +25D = 0, +1 = 1, +319 = +A, +A = 0, +308 = +214, +214 = 0,
 * +318 = 0, +94 = -1 unless 0x70003B8F == 2, D_008106B3 = 0. -1 when
 * clip_rate is missing or fails (nothing written). */
int em_player_stage_begin(EmPlayerLiveActor *actor, EmPlayerStageScene *scene,
                          const EmPlayerStageWorkers *workers);
/* 0015BA50's switch: anim_advance_time into +200 where the original calls
 * it, then major[+4]. +4 = 3 and +4 > 6 do nothing. */
int em_player_stage_dispatch(EmPlayerLiveActor *actor, const EmPlayerStageWorkers *workers);
/* 0015BA50 after its switch: +B = 0, the +276/+274 clears, D_008106B3.
 * -1 (nothing written) when the scene's D_008106F1 or D_00810CB6 pointer
 * is missing. */
int em_player_stage_end(EmPlayerLiveActor *actor, EmPlayerStageScene *scene);
/* 0015BCF0's writes after 0015BA50 returns: +BC = 1.0, then (after
 * 0015CF90 / 0015CBA0 / 00187350, which write none of these bytes) the
 * +B4 < -200 check (+4 = 6, +5 = 0) and the +31B loop-sound stop. */
int em_player_stage_tail(EmPlayerLiveActor *actor, const EmPlayerStageWorkers *workers);
/* 0015B130 and 0015B770; context is an EmPlayerStage. */
int em_player_stage_0015B130(void *stage, EmPlayerLiveActor *actor);
int em_player_stage_0015B770(void *stage, EmPlayerLiveActor *actor);
/* 0015D460 (+4 = 6, entered by 0015BCF0's -200 check); context is an
 * EmPlayerStageFade. */
typedef struct EmPlayerStageFade {
    void *context;
    int (*fade)(void *context, int a0, int a1);   /* 001AEDE0(4, 0) */
} EmPlayerStageFade;
int em_player_stage_0015D460(void *fade, EmPlayerLiveActor *actor);

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
