/* em_player_slide.h - the player's slope slide: state 0x1C (docs/PLAYER_CLIMB_SLIDE.md).
 *
 * Translations of the original routines, not models of them:
 *   0016C6A0  state 0x1C callback (sub-states 0..3, 0xA..0xC, 0x14/0x15, 0x1E)
 *   0016C570  entry side probes        0016C520  release the slide loop sound
 *   0016CD70  slide motion              0017F5F0  slide steering
 *   00174FD0  steering quadrant         001791D0  lean/wall sweeps
 *   00179880  drop accumulator          001B12B0  angle approach
 *
 * Entry: 00175CF0 records a class-0x1000 floor as +237 = 1 with +218 = the
 * downhill heading; 001796C0 then sets +5 = 0x1C, +6 = 0, +1F0 = 0x30.
 *
 * Every routine works on EmPlayerSlideActor, a mirror of the player actor
 * bytes these functions read and write (the comments name the offsets).
 * Every callee the port does not translate here is a worker. A worker that is
 * missing (NULL) or returns a negative value is a fault: the routine stops and
 * returns -1, leaving the writes made before the call, as the original order
 * leaves them. Arithmetic follows the EE: single-precision results truncate
 * toward zero (em_effect_float32), as the oracle interpreter models.
 *
 * Oracle: tools/test_player_slide_reference.py executes the original
 * instructions from the user's pinned ELF and compares every field below and
 * every worker call (order and arguments). */
#ifndef EM_PLAYER_SLIDE_H
#define EM_PLAYER_SLIDE_H

#include <stdint.h>

#include "game/em_player_floor.h"

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
} EmPlayerSlideWorkers;

/* 001B12B0(target, current, rate). */
float em_player_slide_approach(float target, float current, float rate);
/* 00174FD0. Returns 0, or -1 on a fault. */
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

#endif
