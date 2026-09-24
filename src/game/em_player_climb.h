/* em_player_climb.h - the player's ledge climb: the Use-press ledge probe and
 * climb state 2 (docs/PLAYER_CLIMB_SLIDE.md).
 *
 * Translations of the original routines, not models of them:
 *   0015DF10  ledge probe (00160220 calls it at the body yaw, then +-45 deg)
 *   0015DEC0  ledge face gate           00177F40  ledge depth test
 *   00177460  vault distance test
 *   00161790  climb state 2 callback    00161690  AREA11 ledge target override
 *   0017D800  climb rise set-up         0017D8D0  climb rise step
 *   0017DEB0  climb dust/sound
 *   00162190  vault state 3 callback    00162080  AREA11 vault target override
 *   0017D940 / 0017DAF0 / 0017DC80  vault set-ups   0017DE20  vault step
 * The helpers 00177510, 001775E0, 001776E0, 00177CF0, 0019A180, 0017F320
 * and 00188550 have one translation, em_player_record_helpers.c; these
 * routines call its bodies (em_player_helper_*), and other modules bind its
 * record entries (em_player_record_*). The SDK leaves are their owners':
 * 001B1470 em_player_001B1470, 001281C0 em_player_float_to_int
 * (em_player_stage_workers.c), 0011DF78 em_sdk_math_original_0011DF78,
 * build_trs_matrix / 001029C0 / 00102BB0 / 00102918 em_owner_services_original,
 * 001026A0 em_effect_original_001026A0.
 *
 * AREA11 evidence (whole-world original run, docs/PLAYER_CLIMB_SLIDE.md): Use
 * in front of the single crate 0x7A7C70 (top 203.78 over ground 189.84)
 * enters state 2, +1F0 = 8, sub-state 0x14 with +2F1 = 1 (clip 0x70, then
 * 0x78 = D_002754A0[1], then 0x8C), and ends on the crate top in state 0.
 *
 * As in em_player_floor.h, each routine works on a mirror of the actor bytes
 * it reads and writes, and every callee the port does not translate here is a
 * worker. A missing worker or a negative worker result is a fault (-1).
 * Arithmetic: every COP1 operation and compare goes through em_ee_float.h
 * (the measured EE model, docs/EE_FLOAT_MODEL.md); the VU0 leaves are their
 * owners' measured translations.
 *
 * Oracle: tools/test_player_climb_reference.py executes the original
 * instructions on the measured float model and compares every field below
 * and every worker call; its world mode replays the route beats with the
 * live adapters (em_player_climb_live_*) on the record. */
#ifndef EM_PLAYER_CLIMB_H
#define EM_PLAYER_CLIMB_H

#include <stdint.h>

#include "game/em_player_floor.h"
#include "game/em_player_fall.h"

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
    /* Optional: the scratch words the helpers write (0x700038A0..AC by
     * 001775E0, 001776E0, 00177CF0 and 0017F320), the SAME instance every
     * other writer and reader is bound to. NULL keeps the writes in a local
     * (the unit oracles); the live adapter requires it. */
    EmPlayerLandScratch *scratch;
} EmPlayerClimbWorkers;

/* build_trs_matrix(+D0, +B0, +C0, +60). 0, or -1 when a form is refused. */
int em_player_climb_trs(EmPlayerClimbActor *actor);
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

/* ---- The live climb states (em_player.c "Live player states") -----------
 * The mirror above over the live actor's original bytes (offsets in the
 * field comments; tools/test_player_climb_reference.py checks them against
 * its original-verified offset tables). link_kind is not a byte: it is
 * derived from the +308 owner by the binder's link_kind worker. */
void em_player_climb_actor_from_live(const EmPlayerLiveActor *live, EmPlayerClimbActor *out);
void em_player_climb_actor_to_live(const EmPlayerClimbActor *in, EmPlayerLiveActor *live);

/* EmPlayerStatesBinding.stage.state[2] and [3] = em_player_climb_live_state
 * with an EmPlayerClimbLive context (+5 2 runs 00161790, 3 runs 00162190).
 *
 * Every callee that takes the record runs on the record itself:
 *   - `workers` binds the callees whose original takes no record: move
 *     (0019AD00), sweep (0019AFE0), segment (0019A570), column (001760C0),
 *     table (0019BC40), effect (001EFD90 on +B0 / +C0) and the SDK atan2 /
 *     sqrt; its other slots and its scratch are not read;
 *   - the record-level slots below bind the rest (context = live_context):
 *     00175900 floor (player_states_floor_service), 001764E0 probes
 *     (player_states_wall_probes), 001796C0 fall (player_states_fall_check),
 *     001749A0 request, anim_clip_arbiter, 001C61D0 clip_frames (the +40
 *     bank word), 001FBD50 sound, 00182870 land_sound, 00178B90 translate,
 *     00174AC0 heading, 0017C440 reentry, 0017C540 handoff, 0017C580 land,
 *     and anim_eval_skeleton with node 1's +C4 / +8 (the shapes of
 *     em_pose_host_*, em_player_fall_* and em_player_heading_record_*);
 *   - `scratch` is the shared 0x700038A0 / 0x70003A20 instance.
 * Around every worker call the adapter stores the mirror into the record
 * and reads it back after, so a worker that reads or writes the record (the
 * pose host's 001749A0 rewrites +20C, +2C and the +3C clock the mirror also
 * carries) sees and leaves exactly what the original would. `scene` fills EmPlayerClimbScene this stage;
 * `link_kind` maps the +308 owner (em_actor_collision_player_link_kind).
 * A missing worker or pointer faults (-1) before anything runs. */
typedef struct EmPlayerClimbLive {
    EmPlayerClimbWorkers workers;
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    int (*probes)(void *context, EmPlayerLiveActor *actor);
    int (*fall)(void *context, EmPlayerLiveActor *actor);
    void *live_context;
    int (*scene)(void *context, EmPlayerClimbScene *scene);
    void *scene_context;
    int (*link_kind)(void *context, const void *owner);
    void *link_context;
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int force, float blend);
    int (*arbiter)(void *context, EmPlayerLiveActor *actor, int clip, float blend, float frame);
    int (*clip_frames)(void *context, uint32_t bank, int clip, int32_t *frames);
    int (*sound)(void *context, EmPlayerLiveActor *actor, int id);
    int (*land_sound)(void *context, EmPlayerLiveActor *actor, int tier);
    int (*translate)(void *context, EmPlayerLiveActor *actor, int arg);
    int (*heading)(void *context, EmPlayerLiveActor *actor, int arg);
    int (*reentry)(void *context, EmPlayerLiveActor *actor, int arg);
    int (*handoff)(void *context, EmPlayerLiveActor *actor);
    int (*land)(void *context, EmPlayerLiveActor *actor);
    int (*skeleton)(void *context, EmPlayerLiveActor *actor, float *hip_y, float *hip_8);
    EmPlayerLandScratch *scratch;
} EmPlayerClimbLive;
int em_player_climb_live_state(void *context, EmPlayerLiveActor *actor);
/* 0015DF10(p, mode, ang) over the live actor, for the Use chain 00160220
 * (em_player_use_dispatch): the same binding and record rules. 1 when a
 * climb started, 0, or -1. */
int em_player_climb_live_probe(void *context, EmPlayerLiveActor *actor, int mode, float ang);
/* The same in the shape of EmPlayerUseWorkers.ledge (the angle as raw
 * bits; *result = 1 when a climb started, else 0). 0, or -1 on a fault. */
int em_player_climb_live_ledge(void *context, EmPlayerLiveActor *actor, int mode, uint32_t angle,
                               int *result);

#endif
