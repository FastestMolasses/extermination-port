#include "game/em_player_foot_stop.h"
#include "game/em_camera_rotation.h"
#include "game/em_effect_color.h"

int em_player_foot_stop_begin(EmPlayerFootStop *stop, unsigned tier,
    float clip_remaining, const float foot17[3], const float foot18[3],
    const float position[3], const float euler[3])
{
    /* 0017B910 has no lower bound on the +0x3C clock: a clock below 1 takes
     * the cur < lim arm (residual cur - 1 < 0); walk then clamps the halved
     * residual to 1 and jog uses 10. Non-finite inputs are a native refusal. */
    if (!stop || !foot17 || !foot18 || !position || !euler ||
        (tier != 1 && tier != 2) || !isfinite(clip_remaining))
        return 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!isfinite(foot17[axis]) || !isfinite(foot18[axis]) || !isfinite(position[axis]))
            return 0;

    /*D0024875C's healthy rows select the next planted foot from the
     * current remaining clock. Only walk halves that residual interval. */
    float limit = tier == 1 ? 58.0f : 24.0f;
    const float *foot = clip_remaining < limit ? foot18 : foot17;
    float duration = 10.0f;
    if (tier == 1) {
        float residual = em_effect_float32((double)clip_remaining -
                                           (clip_remaining < limit ? 1.0f : limit));
        duration = residual / 2.0f;
        if (duration < 1) duration = 1;
    }
    float dx = em_effect_float32((double)foot[0] - position[0]);
    float dz = em_effect_float32((double)foot[2] - position[2]);
    float square = em_effect_float32((double)em_effect_float32((double)dx * dx) +
                                     em_effect_float32((double)dz * dz));
    /*0011E748 calls the original software square root, which rounds its
     * result nearest. The surrounding EE products/addition truncate. */
    float distance = sqrtf(square);
    float matrix[16], direction[4];
    if (!em_camera_rotation_offset(euler, distance, matrix, direction)) return 0;
    *stop = (EmPlayerFootStop){direction[0] / duration, direction[2] / duration,
                              duration, tier, 1};
    return 1;
}

int em_player_foot_stop_tick(EmPlayerFootStop *stop, unsigned animation_flags,
    float position[3], float *animation_rate)
{
    if (!stop || !position || !animation_rate || !stop->active ||
        (stop->tier != 1 && stop->tier != 2))
        return -1;
    if (stop->remaining < 1) {
        if (stop->tier == 1 || (animation_flags & 0x1000)) {
            stop->active = 0;
            return 0;
        }
    } else {
        position[0] = em_effect_float32((double)position[0] + stop->step_x);
        position[2] = em_effect_float32((double)position[2] + stop->step_z);
        stop->remaining = em_effect_float32((double)stop->remaining - 1);
        if (stop->tier == 1) *animation_rate = 2;
    }
    return 1;
}
