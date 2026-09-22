#include "game/em_pose_transition.h"
#include <math.h>
#include <string.h>

#include "game/em_pose_math.h"

void em_pose_quaternion_blend(float out[4], const float a[4], const float b[4], float t)
{
    if (!(t <= 1)) t = 1;
    float inverse = pose_sub(1, t);
    float dot = pose_madd(pose_mul(a[0], b[0]), a[1], b[1]);
    dot = pose_add(pose_mul(a[2], b[2]), dot);
    dot = pose_madd(dot, a[3], b[3]);
    for (unsigned k = 0; k < 4; ++k)
        out[k] = dot < 0 ? pose_msub(pose_mul(b[k], t), a[k], inverse)
                         : pose_madd(pose_mul(a[k], inverse), b[k], t);
}

void em_pose_channels_matrix(float m[16], const EmPoseChannels *c)
{
    const float *q = c->rotation;
    float xx = pose_mul(q[0], q[0]), yy = pose_mul(q[1], q[1]), zz = pose_mul(q[2], q[2]);
    float xy = pose_mul(q[0], q[1]), xz = pose_mul(q[0], q[2]), yz = pose_mul(q[1], q[2]);
    float wx = pose_mul(q[3], q[0]), wy = pose_mul(q[3], q[1]), wz = pose_mul(q[3], q[2]);
    m[0] = pose_sub(1, pose_mul(2, pose_add(yy, zz)));
    m[1] = pose_mul(2, pose_sub(xy, wz));
    m[2] = pose_mul(2, pose_add(xz, wy));
    m[3] = 0;
    m[4] = pose_mul(2, pose_add(xy, wz));
    m[5] = pose_sub(1, pose_mul(2, pose_add(xx, zz)));
    m[6] = pose_mul(2, pose_sub(yz, wx));
    m[7] = 0;
    m[8] = pose_mul(2, pose_sub(xz, wy));
    m[9] = pose_mul(2, pose_add(yz, wx));
    m[10] = pose_sub(1, pose_mul(2, pose_add(xx, yy)));
    m[11] = 0;
    memcpy(m + 12, c->translation, 12);
    m[15] = 1;
    for (unsigned column = 0; column < 3; ++column)
        for (unsigned row = 0; row < 3; ++row)
            m[column * 4 + row] = pose_mul(m[column * 4 + row], c->scale[column]);
}

int em_pose_transition_begin(EmPoseTransition *s, const EmPoseChannels *source,
                             const EmPoseChannels *target, unsigned count, unsigned ticks)
{
    if (!s || !source || !target || !count || count > EM_POSE_NODE_MAX || !ticks || ticks > 65535)
        return 0;
    for (unsigned i = 0; i < count; ++i) {
        for (unsigned k = 0; k < 3; ++k)
            if (!isfinite(source[i].translation[k]) || !isfinite(target[i].translation[k]) ||
                !isfinite(source[i].scale[k]) || !isfinite(target[i].scale[k]))
                return 0;
        for (unsigned k = 0; k < 4; ++k)
            if (!isfinite(source[i].rotation[k]) || !isfinite(target[i].rotation[k])) return 0;
    }
    /* Copies allow an interrupted blend to restart from its actual current
     * channels without retaining the preceding target or decomposing matrices. */
    memmove(s->source, source, count * sizeof *source);
    memmove(s->target, target, count * sizeof *target);
    memcpy(s->current, s->source, count * sizeof *source);
    s->count = count;
    s->remaining = (float)ticks;
    s->reciprocal = pose_div(1, (float)ticks);
    s->fraction = 0;
    s->active = 1;
    for (unsigned i = 0; i < count; ++i)
        for (unsigned k = 0; k < 3; ++k) {
            s->translation_velocity[i][k] = pose_mul(
                pose_sub(s->target[i].translation[k], s->source[i].translation[k]), s->reciprocal);
            s->scale_velocity[i][k] =
                pose_mul(pose_sub(s->target[i].scale[k], s->source[i].scale[k]), s->reciprocal);
        }
    return 1;
}

int em_pose_transition_step(EmPoseTransition *s, float step)
{
    if (!s || !isfinite(step) || step <= 0 || step > 1) return -1;
    if (!s || !s->active) return 0;
    if (s->remaining <= 1) {
        /*001C64F0 invokes001C8710, reinitializing exact target channels;
         * it does not consume target source frame1 on this callback. */
        memcpy(s->current, s->target, s->count * sizeof *s->target);
        s->active = 0;
        s->fraction = 1;
        return 0;
    }
    s->remaining = pose_sub(s->remaining, step);
    s->fraction = pose_madd(s->fraction, step, s->reciprocal);
    for (unsigned i = 0; i < s->count; ++i) {
        for (unsigned k = 0; k < 3; ++k) {
            s->current[i].translation[k] =
                pose_madd(s->current[i].translation[k], step, s->translation_velocity[i][k]);
            s->current[i].scale[k] =
                pose_madd(s->current[i].scale[k], step, s->scale_velocity[i][k]);
        }
        em_pose_quaternion_blend(s->current[i].rotation, s->source[i].rotation,
                                 s->target[i].rotation, s->fraction);
    }
    return 1;
}

int em_pose_transition_tick(EmPoseTransition *s)
{
    return em_pose_transition_step(s, 1);
}
