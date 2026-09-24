/* em_player_slide.h - the player's slope slide: state 0x1C (docs/PLAYER_CLIMB_SLIDE.md).
 *
 * Translations of the original routines, not models of them:
 *   0016C6A0  state 0x1C callback (sub-states 0..3, 0xA..0xC, 0x14/0x15, 0x1E)
 *   0016C570  entry side probes        0016C520  release the slide loop sound
 *   0016CD70  slide motion              0017F5F0  slide steering
 *   001791D0  lean/wall sweeps
 * and the callees these routines reach through their one translation
 * elsewhere (never re-translated here):
 *   00174FD0  steering quadrant   em_player_record_00174FD0 (em_player_record_helpers.c)
 *   00179880  drop accumulator    em_player_fall_00179880 (em_player_fall.c)
 *   001B12B0  angle approach      em_script_host_001B12B0 (em_script_host_workers.c)
 *   001B1470  angle wrap          em_player_001B1470 (bounded, em_player_helper_wrap)
 *   0011DF78  fabsf               em_sdk_math_original_0011DF78
 *   001029C0 / 00102BB0 / 00102918 / 001026A0  the SDK owners
 *             (em_player_record_helpers.h)
 *
 * Entry: 00175CF0 records a class-0x1000 floor as +237 = 1 with +218 = the
 * downhill heading; 001796C0 then sets +5 = 0x1C, +6 = 0, +1F0 = 0x30.
 *
 * Every routine works on EmPlayerSlideActor, a mirror of the player actor
 * bytes these functions read and write (the comments name the offsets).
 * Every callee the port does not translate here is a worker. A worker that is
 * missing (NULL) or returns a negative value is a fault: the routine stops and
 * returns -1, leaving the writes made before the call, as the original order
 * leaves them. Arithmetic: every COP1 operation and compare goes through
 * em_ee_float.h (the measured EE model, docs/EE_FLOAT_MODEL.md).
 *
 * Oracle: tools/test_player_slide_reference.py executes the original
 * instructions from the user's pinned ELF on the measured float model and
 * compares every field below, the scratch words and every worker call
 * (order and arguments); its world mode replays route beat 06_hill_slide
 * with the live adapter (em_player_slide_live_state) on the record. */
#ifndef EM_PLAYER_SLIDE_H
#define EM_PLAYER_SLIDE_H

#include <stdint.h>

#include "game/em_player_floor.h"
#include "game/em_player_fall.h"

typedef struct EmPlayerSlideActor {
    float position[3];    /* +B0 (the feet during the state callback) */
    float rotation[3];    /* +C0 lean toward the slope, +C4 yaw, +C8 */
    float speed;          /* +38 */
    float clock;          /* +3C remaining source frames */
    float slope;          /* +9C slope angle from 0019A310 */
    float slide_yaw;      /* +218 downhill heading from 00175CF0 */
    float root_prev;      /* +21C previous root-node forward translation */
    float impact;         /* +26C landing speed factor */
    float fall_rate;      /* +2E0 */
    float ramp;           /* +2E4 steering speed ramp per tick */
    float drop;           /* +2EC vertical drop accumulator */
    float land_y;         /* +2F4 */
    float ramp_timer;     /* +2F8 */
    float entry_y;        /* +294 */
    float pad_x;          /* +244 00174FD0 stick term */
    float pad_y;          /* +248 */
    int32_t steer;        /* +24C steering quadrant, -1 for none */
    uint32_t anim_flags;  /* +200 (0x1000 = non-looping clip end) */
    int16_t ticks;        /* +2A effect counter */
    uint16_t sound_id;    /* +31C */
    int8_t sound;         /* +31B loop handle, -1 for none */
    uint8_t sound_live;   /* +31A */
    uint8_t major;        /* +4 */
    uint8_t state;        /* +5 */
    uint8_t walk;         /* +6 sub-state */
    uint8_t sub;          /* +7 */
    uint8_t mode;         /* +1F0 */
    uint8_t variant;      /* +1F1 steering ramp latch */
    uint8_t lean;         /* +25D steering lean clip latch */
    uint8_t lock;         /* +25F */
    uint8_t step;         /* +302 recovery step phase */
    uint8_t obstruction;  /* +314 lane bits */
    uint8_t slide;        /* +237 */
    uint8_t surface;      /* +23A */
    uint8_t gait;         /* +23F */
    uint8_t contact;      /* +A */
} EmPlayerSlideActor;

/* Values the routines read outside the actor. */
typedef struct EmPlayerSlideScene {
    float root_forward;   /* *(*D_00275B40) + 8: root node forward translation */
    float hip[2];         /* *(D_00275B40 + 4) + C0 / + C8: hip world x, z */
    uint8_t scripted;     /* spad 0x70003B8D */
    uint8_t pad_gait;     /* D_00810E57 */
    uint8_t pad_x;        /* D_00810E64 */
    uint8_t pad_y;        /* D_00810E65 */
} EmPlayerSlideScene;

typedef struct EmPlayerSlideWorkers {
    void *context;
    /* 001749A0(p, clip, force, blend). */
    int (*request)(void *context, int clip, int force, float blend);
    /* anim_clip_arbiter(p, clip, blend, frame). */
    int (*arbiter)(void *context, int clip, float blend, float frame);
    /* 001C61D0(+40, clip): the clip's frame count. */
    int (*clip_frames)(void *context, int clip, int *frames);
    /* 001FBD50(p, id, 0, 300.0): its return value becomes the +31B byte. */
    int (*sound)(void *context, unsigned id, int *handle);
    /* 0011A070(handle). */
    int (*stop_sound)(void *context, int handle);
    /* 001EFD90(id, p+B0, p+C0). */
    int (*effect)(void *context, uint32_t id, const float position[3],
                  const float rotation[3]);
    /* 0019AD00(p, target, 0x80000006): the probe and its x/z response update
     * `position` (the actor's +B0). */
    int (*move)(void *context, float position[3], const float target[4], unsigned mask);
    /* 0019AFE0(p, from, to, mask) without the 0x80000000 bit. */
    int (*sweep)(void *context, const float from[4], const float to[4], unsigned mask,
                 EmPlayerProbeHit *hit);
    /* 00175900(p, search): the floor service over this actor; *result is its
     * return value. */
    int (*floor)(void *context, EmPlayerSlideActor *actor, int search, int *result);
    /* 001796C0(p). */
    int (*fall)(void *context, EmPlayerSlideActor *actor);
    /* 00178B90(p, arg). */
    int (*translate)(void *context, EmPlayerSlideActor *actor, int arg);
    /* 00224B80(p): *result 0, 1 or 2 (2 ends the slide). */
    int (*damage)(void *context, EmPlayerSlideActor *actor, int *result);
    /* 00224290(p): *result nonzero selects the recovery clip 0x6D. */
    int (*land_check)(void *context, EmPlayerSlideActor *actor, int *result);
    /* 0017C580(p): the landing reaction. */
    int (*land)(void *context, EmPlayerSlideActor *actor);
    /* 00182430(p, tier) and 00182870(p, tier). */
    int (*step_sound)(void *context, int tier);
    int (*land_sound)(void *context, int tier);
    /* 0021D250(p, 0) (surface 0x5D) and 0021D2E0(p, 0x78, 0) (sub-state 0x1E). */
    int (*surface5d)(void *context);
    int (*teleport)(void *context);
    /* SDK transcendental calls: 0011E2A8 sine, 0011DE90 cosine, 0011E620
     * atan2. The port and the oracle bind the same host models. */
    float (*sine)(void *context, float x);
    float (*cosine)(void *context, float x);
    float (*atan2)(void *context, float y, float x);
    /* Optional: the scratch words these routines write (0x700038A0..AC by
     * 0016C570 and 0016CD70; 0x70003A20 by 0016C6A0, 0017F5F0 and 00174FD0),
     * the SAME instance every other writer and reader is bound to. NULL
     * keeps the writes in a local (the unit oracles); the live adapter
     * requires it. */
    EmPlayerLandScratch *scratch;
} EmPlayerSlideWorkers;

/* 001B12B0(target, current, rate) through its owner
 * (em_script_host_001B12B0). Returns `current` unchanged when the owner
 * refuses (an argument outside 001B1470's bounded domain). */
float em_player_slide_approach(float target, float current, float rate);
/* 00174FD0 (em_player_record_00174FD0) over the mirror. Returns 0, or -1
 * on a fault. */
int em_player_slide_steer_input(EmPlayerSlideActor *actor, const EmPlayerSlideScene *scene,
                                const EmPlayerSlideWorkers *workers);
/* 0017F5F0(p, skip_clips). */
int em_player_slide_steer(EmPlayerSlideActor *actor, const EmPlayerSlideScene *scene,
                          int skip_clips, const EmPlayerSlideWorkers *workers);
/* 001791D0. */
int em_player_slide_sweeps(EmPlayerSlideActor *actor, const EmPlayerSlideScene *scene,
                           const EmPlayerSlideWorkers *workers);
/* 0016C570. */
int em_player_slide_side_probes(EmPlayerSlideActor *actor, const EmPlayerSlideWorkers *workers);
/* 0016C520. */
int em_player_slide_release_sound(EmPlayerSlideActor *actor, const EmPlayerSlideWorkers *workers);
/* 0016CD70(p, hold). Returns 0, 1 (landed), 2 (airborne), or -1. */
int em_player_slide_motion(EmPlayerSlideActor *actor, const EmPlayerSlideScene *scene,
                           int hold, const EmPlayerSlideWorkers *workers);
/* 0016C6A0. Returns 0, or -1 on a fault. */
int em_player_slide_tick(EmPlayerSlideActor *actor, const EmPlayerSlideScene *scene,
                         const EmPlayerSlideWorkers *workers);

/* ---- The live state 0x1C (em_player.c "Live player states") -------------
 * The mirror above over the live actor's original bytes (offsets in the
 * field comments; tools/test_player_slide_reference.py checks them against
 * its original-verified offset table). */
void em_player_slide_actor_from_live(const EmPlayerLiveActor *live, EmPlayerSlideActor *out);
void em_player_slide_actor_to_live(const EmPlayerSlideActor *in, EmPlayerLiveActor *live);

/* EmPlayerStatesBinding.stage.state[0x1C] = em_player_slide_live_state with an
 * EmPlayerSlideLive context.
 *
 * Every callee that takes the record runs on the record itself:
 *   - `workers` binds the callees whose original takes no record: stop_sound
 *     (0011A070), effect (001EFD90 on +B0 / +C0), move (0019AD00: it writes
 *     its x/z response into `position`, which the adapter stores at +B0 and
 *     +B8; anything else it writes, it writes on the record),
 *     sweep (0019AFE0) and the SDK sine / cosine / atan2; its other slots and
 *     its scratch are not read;
 *   - the record-level slots below bind the rest (context = live_context):
 *     00175900 floor (player_states_floor_service), 001796C0 fall
 *     (player_states_fall_check), 001749A0 request, anim_clip_arbiter,
 *     001C61D0 clip_frames (the +40 bank word), 001FBD50 sound (*handle =
 *     its return), 00178B90 translate, 00224B80 damage, 00224290
 *     land_check, 0017C580 land, 00182430 step_sound, 00182870 land_sound,
 *     0021D250(p, arg) surface5d and 0021D2E0(p, frames, hold) teleport
 *     (the shapes of em_pose_host_* and em_player_fall_*);
 *   - `scratch` is the shared 0x700038A0 / 0x70003A20 instance.
 * Around every worker call the adapter stores the mirror into the record
 * and reads it back after, so a worker that reads or writes the record sees
 * and leaves exactly what the original would. `scene` fills the values
 * 0016C6A0 reads outside the actor this stage (the skeleton's root and hip
 * nodes, spad 0x70003B8D, the pad bytes). Every one is required: a missing
 * one faults (-1) before the callback runs. */
typedef struct EmPlayerSlideLive {
    EmPlayerSlideWorkers workers;
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    int (*fall)(void *context, EmPlayerLiveActor *actor);
    void *live_context;
    int (*scene)(void *context, EmPlayerSlideScene *scene);
    void *scene_context;
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int force, float blend);
    int (*arbiter)(void *context, EmPlayerLiveActor *actor, int clip, float blend, float frame);
    int (*clip_frames)(void *context, uint32_t bank, int clip, int32_t *frames);
    int (*sound)(void *context, EmPlayerLiveActor *actor, int id, int *handle);
    int (*translate)(void *context, EmPlayerLiveActor *actor, int arg);
    int (*damage)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*land_check)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*land)(void *context, EmPlayerLiveActor *actor);
    int (*step_sound)(void *context, EmPlayerLiveActor *actor, int tier);
    int (*land_sound)(void *context, EmPlayerLiveActor *actor, int tier);
    int (*surface5d)(void *context, EmPlayerLiveActor *actor, int arg);
    int (*teleport)(void *context, EmPlayerLiveActor *actor, int frames, int hold);
    EmPlayerLandScratch *scratch;
} EmPlayerSlideLive;
int em_player_slide_live_state(void *context, EmPlayerLiveActor *actor);

#endif
