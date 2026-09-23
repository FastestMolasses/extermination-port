#ifndef EM_PLAYER_REVERSAL_H
#define EM_PLAYER_REVERSAL_H

#include <stdint.h>

/* WP-15/H11 reversal skid: the player-actor bytes read or written by
 * 00174AC0's reversal gate, 0017C030 cases 7/6 and 001612D0 case 2.
 * Field comments name the original actor offsets. The owner (the player
 * callback host) fills and consumes this struct around each call. */
typedef struct EmPlayerReversalActor {
    float speed;          /* +38  scalar ground speed, units/tick */
    float yaw;            /* +C4  body heading */
    float target;         /* +240 gait target speed, written by 00174AC0 */
    float rate;           /* +204 animation multiplier */
    float blend;          /* +208 tier blend */
    float delta;          /* 70003A20 scratch: last wrapped reversal error */
    uint32_t anim_flags;  /* +200 (0x1000 = non-looping clip end) */
    uint32_t global_mode; /* D_008106C8, read by 001B0070 */
    uint16_t ticks;       /* +28  state-2 effect counter */
    uint8_t player_state; /* +5   1 = walk callback 001612D0 */
    uint8_t walk_state;   /* +6   1 walking, 2 reversal/resume */
    uint8_t mode;         /* +1F0 */
    uint8_t variant;      /* +1F1 (3/4 during the skid) */
    uint8_t tier;         /* +25C */
    uint8_t gait;         /* +23F */
    uint8_t row;          /* +235 */
    uint8_t special;      /* +236 */
    uint8_t surface;      /* +23A */
    uint8_t depth[2];     /* +23C, +23D */
    uint8_t obstruction;  /* +314 */
} EmPlayerReversalActor;

/* Side effects leave through these workers. Every worker returns >= 0 on
 * success and -1 on failure. A NULL worker that is reached is a fault:
 * the calling function returns -1 at that point and the owner abandons
 * the callback (writes made earlier in the same call remain). */
typedef struct EmPlayerReversalWorkers {
    void *context;
    /* 001749A0(p, clip, force, blend): source frame 0. */
    int (*request)(void *context, int clip, int force, float blend);
    /* anim_clip_arbiter(p, clip, blend, frame): always initializes. */
    int (*arbiter)(void *context, int clip, float blend, float frame);
    /* 001C61D0(+40, clip): the clip header's frame count. */
    int (*clip_frames)(void *context, int clip, int *frames);
    /* 001FB9F0(id, 0x1000, 0x1000, 0x1000). */
    int (*sound)(void *context, unsigned id);
    /* 001EFD90(id, actor+B0, actor+C0). */
    int (*effect)(void *context, uint32_t id);
    /* 00174AC0 arg1==1 ordinary body turn toward `desired`. */
    int (*turn)(void *context, float desired);
} EmPlayerReversalWorkers;

/* 001B1470 with EE finite arithmetic (-pi, pi]. */
float em_player_reversal_wrap(float angle);

/* 0017B490 for commands 1..5: fills *clip, or returns 0 for a row the
 * verified D_00248AB0 rows do not cover. */
int em_player_reversal_clip(const EmPlayerReversalActor *actor,
                            unsigned command, int *clip);

/* 00174AC0 without its pad/trigonometry front end: latch the target speed
 * for the caller-supplied gait, then apply the walk-state reversal gate to
 * the desired heading. Returns 1 when the ordinary arg1==1 turn follows,
 * 0 when gait is zero or the turn is suppressed (modes 6/7 or a new skid:
 * +1F0=7, +1F1=4 for error > 3pi/4, 3 for error < -3pi/4). */
int em_player_reversal_heading(EmPlayerReversalActor *actor, float desired);

/* 0017C030 cases 7 and 6. Returns 1 when handled, 0 when the mode is not
 * 6/7 (the caller owns that case), -1 on a worker fault. */
int em_player_reversal_animation(EmPlayerReversalActor *actor,
                                 const EmPlayerReversalWorkers *workers);

/* 001612D0 case1 tail after 0017C030/00178B90: stage 6/7 moves the walk
 * state to 2 and clears +28. Returns 1 when it did. */
int em_player_reversal_walk_tail(EmPlayerReversalActor *actor);

/* 001612D0 case2 from its 00174AC0 call through 0017C030 (the 001607D0
 * action machine and the 00184BA0 use scan precede it; the caller then
 * performs 00178B90(p,0) at actor->speed and the common tail). Returns 1
 * on success, -1 on a worker fault. On a resume, mode 1 case 1
 * (0017B660) belongs to the caller's ordinary source host. */
int em_player_reversal_state2(EmPlayerReversalActor *actor, float desired,
                              const EmPlayerReversalWorkers *workers);

#endif
