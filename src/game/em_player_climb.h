/* em_player_climb.h - the player's ledge climb: the Use-press ledge probe and
 * climb state 2 (docs/PLAYER_CLIMB_SLIDE.md).
 *
 * Translations of the original routines, not models of them:
 *   0015DF10  ledge probe (00160220 calls it at the body yaw, then +-45 deg)
 *   0015DEC0  ledge face gate           00177510  ledge frame capture
 *   001775E0  ledge lip sweep           00177F40  ledge depth test
 *   00177460  vault distance test       001776E0  high-ledge side sweeps
 *   00177CF0  high-ledge hand sweeps    0019A180  column entry attribute
 *   00161790  climb state 2 callback    00161690  AREA11 ledge target override
 *   0017D800  climb rise set-up         0017D8D0  climb rise step
 *   0017DEB0  climb dust/sound          0017F320  hang clearance test
 *   00188550  hang clip row
 *   00162190  vault state 3 callback    00162080  AREA11 vault target override
 *   0017D940 / 0017DAF0 / 0017DC80  vault set-ups   0017DE20  vault step
 *
 * AREA11 evidence (whole-world original run, docs/PLAYER_CLIMB_SLIDE.md): Use
 * in front of the single crate 0x7A7C70 (top 203.78 over ground 189.84)
 * enters state 2, +1F0 = 8, sub-state 0x14 with +2F1 = 1 (clip 0x70, then
 * 0x78 = D_002754A0[1], then 0x8C), and ends on the crate top in state 0.
 *
 * As in em_player_floor.h, each routine works on a mirror of the actor bytes
 * it reads and writes, and every callee the port does not translate here is a
 * worker. A missing worker or a negative worker result is a fault (-1).
 * Arithmetic follows the EE (em_effect_float32 truncation).
 *
 * Oracle: tools/test_player_climb_reference.py executes the original
 * instructions and compares every field below and every worker call. */
#ifndef EM_PLAYER_CLIMB_H
#define EM_PLAYER_CLIMB_H

#include <stdint.h>

#include "game/em_player_floor.h"

typedef struct EmPlayerClimbActor {
    float position[4];    /* +B0 (the feet during the state callback); +BC w */
    float rotation[3];    /* +C0, +C4 yaw, +C8 */
    float scale[3];       /* +60 */
    float matrix[16];     /* +D0 world matrix (build_trs_matrix of +B0/+C0/+60) */
    float speed;          /* +38 */
    float clock;          /* +3C remaining source frames */
    float rate;           /* +204 animation rate multiplier */
    float ledge;          /* +254 ledge height above the feet */
    float target_y;       /* +258 */
    float velocity[3];    /* +2E0 / +2E4 / +2E8 (also the grab point x/z) */
    float goal[2];        /* +2F4 / +2F8 */
    float drop;           /* +2EC */
    float aim;            /* +218 */
    float ledge_normal[2];/* +290 / +298 */
    float surface_y;      /* +250 */
    float push;           /* +26C vault lift */
    float push_decay;     /* +270 */
    uint32_t anim_flags;  /* +200 (0x1000 clip end, 0x8000 blend active) */
    int16_t counter;      /* +28 */
    uint8_t major;        /* +4 */
    uint8_t state;        /* +5 */
    uint8_t walk;         /* +6 sub-state */
    uint8_t hang;         /* +D */
    uint8_t mode;         /* +1F0 */
    uint8_t variant;      /* +1F1 */
    uint8_t lock;         /* +25F */
    uint8_t running;      /* +2F2 */
    uint8_t height_class; /* +2F1 */
    uint8_t tier;         /* +25C */
    uint8_t surface;      /* +23A */
    uint8_t depth;        /* +23C */
    uint8_t puddle;       /* +23D */
    uint8_t row;          /* +235 */
    uint8_t special;      /* +236 */
    uint8_t gait;         /* +23F */
    /* +308 link: 2 when its behaviour (+10) is the AREA11 overlay routine
     * 00828700 or 00827880, 1 for any other link, 0 for none. */
    uint8_t link_kind;
} EmPlayerClimbActor;

typedef struct EmPlayerClimbScene {
    /* *(D_00275B40 + 4) + C0..CC: node 1 world translation from the last
     * skeleton evaluation (read by the vault landing, 00162190 case 23). */
    float hip_world[4];
    uint8_t flags;        /* D_008106BE */
    uint8_t area;         /* D_00810700 */
} EmPlayerClimbScene;

/* What 0019BC40 leaves: D_70003170 flags, D_700030F0 heights, D_00282250
 * aux, and per entry the D_70003130 object's +54 byte and +1A halfword
 * (0019A180 reads them). 0019BC40 keeps up to 20 candidates, but its result
 * arrays are 0x40 bytes apart in the scratchpad: past 16 survivors the
 * compaction writes alias one another. The port does not reproduce that
 * aliasing; a table producer reporting more than 16 entries is a fault. */
#define EM_PLAYER_CLIMB_TABLE_MAX 16
typedef struct EmPlayerClimbTable {
    int count;
    uint16_t flags[EM_PLAYER_CLIMB_TABLE_MAX];
    float height[EM_PLAYER_CLIMB_TABLE_MAX];
    float aux[EM_PLAYER_CLIMB_TABLE_MAX];
    uint8_t object_kind[EM_PLAYER_CLIMB_TABLE_MAX];   /* object +54 */
    int16_t object_node[EM_PLAYER_CLIMB_TABLE_MAX];   /* object +1A */
} EmPlayerClimbTable;

/* A probe result plus the hit entity's behaviour test used by 0015DF10. */
typedef struct EmPlayerClimbHit {
    EmPlayerProbeHit probe;  /* point, node halfword, normal (record +24..+2C) */
    uint8_t pickup_box;      /* hit entity +10 == 00219550 */
} EmPlayerClimbHit;

typedef struct EmPlayerClimbWorkers {
    void *context;
    /* 0019AD00(p, target, mask). */
    int (*move)(void *context, const float position[3], const float target[4], unsigned mask,
                EmPlayerClimbHit *hit);
    /* 0019AFE0(p, from, to, mask). */
    int (*sweep)(void *context, const float from[4], const float to[4], unsigned mask,
                 EmPlayerClimbHit *hit);
    /* 0019A570(from, to, mask, id). */
    int (*segment)(void *context, const float from[4], const float to[4], unsigned mask, int id);
    /* 001760C0(p, at, 1, height): nonzero when the column is covered. */
    int (*column)(void *context, const float at[4], float height);
    /* 0019BC40(at). */
    int (*table)(void *context, const float at[4], EmPlayerClimbTable *table);
    /* 0011E620 atan2 and 0011E748 sqrt (host models on both sides). */
    float (*atan2)(void *context, float y, float x);
    float (*sqrt)(void *context, float x);
    /* 001749A0(p, clip, force, blend), anim_clip_arbiter(p, clip, blend,
     * frame) and 001C61D0(+40, clip). */
    int (*request)(void *context, int clip, int force, float blend);
    int (*arbiter)(void *context, int clip, float blend, float frame);
    int (*clip_frames)(void *context, int clip, int *frames);
    /* 001FBD50(p, id, 0, 300.0). */
    int (*sound)(void *context, unsigned id);
    /* 001EFD90(id, position, p+C0). */
    int (*effect)(void *context, uint32_t id, const float position[3], const float rotation[3]);
    /* 00182870(p, tier). */
    int (*land_sound)(void *context, int tier);
    /* anim_eval_skeleton(p), then node 1: world y (+C4) and +8. */
    int (*skeleton)(void *context, EmPlayerClimbActor *actor, float *hip_y, float *hip_8);
    /* 00178B90(p, arg), 00175900(p, search) (*result = its return value),
     * 001764E0(p), 00174AC0(p, arg), 0017C440(p, arg), 0017C540(p), 001796C0(p).
     * Each may read and write the actor. */
    int (*translate)(void *context, EmPlayerClimbActor *actor, int arg);
    int (*floor)(void *context, EmPlayerClimbActor *actor, int search, int *result);
    int (*probes)(void *context, EmPlayerClimbActor *actor);
    int (*heading)(void *context, EmPlayerClimbActor *actor, int arg);
    int (*reentry)(void *context, EmPlayerClimbActor *actor, int arg);
    int (*handoff)(void *context, EmPlayerClimbActor *actor);
    int (*fall)(void *context, EmPlayerClimbActor *actor);
    /* 0017C580(p): the landing reaction. */
    int (*land)(void *context, EmPlayerClimbActor *actor);
} EmPlayerClimbWorkers;

/* 001B12B0-style helpers exported for the tests. */
/* build_trs_matrix(+D0, +B0, +C0, +60). */
void em_player_climb_trs(EmPlayerClimbActor *actor);
/* 0015DF10(p, mode, ang). Returns 1 when a climb started, 0, or -1. */
int em_player_climb_probe(EmPlayerClimbActor *actor, const EmPlayerClimbScene *scene,
                          int mode, float ang, const EmPlayerClimbWorkers *workers);
/* 0017F320: 1 when both hang clearance sweeps are clear. */
int em_player_climb_hang_clear(const EmPlayerClimbActor *actor,
                               const EmPlayerClimbWorkers *workers);
/* 00161790. Returns 0, or -1 on a fault. */
int em_player_climb_tick(EmPlayerClimbActor *actor, const EmPlayerClimbScene *scene,
                         const EmPlayerClimbWorkers *workers);
/* 00162190. Returns 0, or -1 on a fault. */
int em_player_climb_vault_tick(EmPlayerClimbActor *actor, const EmPlayerClimbScene *scene,
                               const EmPlayerClimbWorkers *workers);

#endif
