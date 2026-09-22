#ifndef EM_PLAYER_FOOT_STOP_H
#define EM_PLAYER_FOOT_STOP_H

/*0017B910 entry and0017C030 mode5. The caller supplies the evaluated
 * original source pose's world-space foot nodes17/18, not a display blend. */
typedef struct EmPlayerFootStop {
    float step_x, step_z, remaining;
    unsigned tier;
    int active;
} EmPlayerFootStop;

int em_player_foot_stop_begin(EmPlayerFootStop *stop, unsigned tier,
    float clip_remaining, const float foot17[3], const float foot18[3],
    const float position[3], const float euler[3]);

/* Returns1 while mode5 continues,0 when it returns to idle,-1 invalid.
 * The caller requests clip4/default-frame0 with blend10 for tier2 only. */
int em_player_foot_stop_tick(EmPlayerFootStop *stop, unsigned animation_flags,
    float position[3], float *animation_rate);

#endif
