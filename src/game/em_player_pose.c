#include "game/em_player_pose.h"
#include "game/em_pose_math.h"

#include <math.h>
#include <string.h>

static int publish_channels(EmPlayerPose *pose)
{
    if (pose->transition.active) {
        memcpy(pose->channels, pose->transition.current,
               pose->bank->bone_count * sizeof *pose->channels);
        return 1;
    }
    return em_pose_playback_channels(&pose->playback, pose->channels);
}

int em_player_pose_init(EmPlayerPose *pose, const EmPoseBank *bank, unsigned clip,
                        float source_frame)
{
    if (!pose || !bank || bank->bone_count != 21) return 0;
    EmPosePlayback initial;
    if (!em_pose_playback_begin(&initial, bank, clip, source_frame)) return 0;
    memset(pose, 0, sizeof *pose);
    pose->bank = bank;
    pose->playback = initial;
    pose->valid = publish_channels(pose);
    return pose->valid;
}

int em_player_pose_select(EmPlayerPose *pose, unsigned clip, float source_frame,
                          unsigned blend_ticks, int force)
{
    if (!pose || !pose->valid || blend_ticks > 65535) return 0;
    if (!force && pose->playback.clip->id == clip) return 1;

    EmPosePlayback target;
    EmPoseChannels target_channels[EM_POSE_NODE_MAX];
    if (!em_pose_playback_begin(&target, pose->bank, clip, source_frame) ||
        !em_pose_playback_channels(&target, target_channels))
        return 0;

    /* anim_clip_init stores the positive source-frame integer at node0+8E.
     * The sampled fractional target is used during a nonzero transition;
     * the normal clock restarts from that integer on the final callback. */
    float reset_frame = (float)(unsigned)source_frame;
    if (blend_ticks) {
        if (!em_pose_transition_begin(&pose->transition, pose->channels, target_channels,
                                      pose->bank->bone_count, blend_ticks))
            return 0;
    } else {
        if (!em_pose_playback_begin(&target, pose->bank, clip, reset_frame)) return 0;
        pose->transition.active = 0;
    }
    pose->playback = target;
    pose->reset_frame = reset_frame;
    return publish_channels(pose);
}

int em_player_pose_advance(EmPlayerPose *pose, float rate, int freeze_motion)
{
    if (!pose || !pose->valid || !isfinite(rate) || rate < 0 || rate > 64) return 0;
    pose->flags = 0;
    while (rate > 0) {
        float step = rate <= 1 ? rate : 1;
        rate = pose_sub(rate, step);
        if (pose->transition.active) {
            int active = em_pose_transition_step(&pose->transition, step);
            if (active < 0) return 0;
            if (active) {
                pose->flags |= 0x8000;
                if (freeze_motion) {
                    pose->transition.reciprocal = 0;
                    memset(pose->transition.translation_velocity, 0,
                           sizeof pose->transition.translation_velocity);
                    memset(pose->transition.scale_velocity, 0,
                           sizeof pose->transition.scale_velocity);
                }
            } else {
                if (!em_pose_playback_begin(&pose->playback, pose->bank, pose->playback.clip->id,
                                            pose->reset_frame))
                    return 0;
            }
        } else {
            if (!em_pose_playback_advance(&pose->playback, step, freeze_motion)) return 0;
            pose->flags |= pose->playback.flags;
        }
    }
    /*001C64F0 returns a signed halfword. Preserve flags from earlier split
     * steps even when a later step resolves the transition in this call. */
    pose->flags = (unsigned)(int)(int16_t)pose->flags;
    return publish_channels(pose);
}

int em_player_pose_gait_base(EmPlayerPose *pose, unsigned tier, unsigned substate, float blend)
{
    if (!pose || !pose->valid || pose->transition.active || pose->acquired || tier > 3 ||
        substate > 2 || !isfinite(blend) || blend < 0 || blend > 1)
        return 0;

    unsigned original_clip = pose->playback.clip->id;
    float old_length = pose->playback.clip->duration;
    float source = pose_sub(old_length, pose->playback.remaining);
    if (substate == 0 && original_clip == tier) return 1;

    int adjacent = substate == 1 ? (int)tier + 1 : (int)tier - 1;
    unsigned target = substate == 0 ? tier : (unsigned)adjacent;
    if (target > 3) return 0;
    EmPosePlayback next;
    if (!em_pose_playback_begin(&next, pose->bank, target, 0)) return 0;
    float target_frame = pose_mul(next.clip->duration, pose_div(source, old_length));
    if (!em_player_pose_select(pose, target, target_frame, 0, 1)) return 0;

    /* Matrix evaluation may display the adjacent tier, but node channels are
     * restored by another749F0 call unless its blend has reached one. */
    if (substate != 0 && blend < 1) return em_player_pose_select(pose, original_clip, source, 0, 1);
    return 1;
}

int em_player_pose_acquire(EmPlayerPose *pose)
{
    if (!pose || !pose->valid || pose->acquired) return 0;
    /*174A50 passes flags0. Already-idle acquisition preserves its cursor. */
    if (!em_player_pose_select(pose, 0, 0, 8, 0)) return 0;
    pose->acquired = 1;
    pose->script_active = 0;
    return 1;
}

int em_player_pose_idle_tick(EmPlayerPose *pose, float *local_palette, unsigned palette_bones)
{
    if (!pose || !pose->acquired || pose->script_active || pose->playback.clip->id != 0 ||
        !em_player_pose_advance(pose, 1, 0))
        return -1;
    return em_player_pose_palette(pose, local_palette, palette_bones) ? 1 : -1;
}

int em_player_pose_release(EmPlayerPose *pose)
{
    if (!pose || !pose->valid || !pose->acquired) return 0;
    unsigned clip = pose->playback.clip->id;
    if (clip != 0) {
        /* Original D00248C90[id*6] is0 for40..42/47/15C/15D,1 for clips1..5.
         * The zero row forces174AB0 (idle0,flags1,blend0) before174A50.
         * Its subsequent blend16 request then sees current==requested. */
        if ((clip >= 0x40 && clip <= 0x42) || clip == 0x47 || clip == 0x15C || clip == 0x15D) {
            if (!em_player_pose_select(pose, 0, 0, 0, 1)) return 0;
        } else if (clip > 5) {
            return 0;
        }
        if (!em_player_pose_select(pose, 0, 0, 16, 0)) return 0;
    }
    pose->acquired = 0;
    pose->script_active = 0;
    return 1;
}

static float remaining(const EmPlayerPose *pose)
{
    return pose->transition.active ? pose->transition.remaining : pose->playback.remaining;
}

int em_player_pose_script_tick(EmPlayerPose *pose, const EmInteractionAnimation *animation,
                               int palette_result, float *local_palette, unsigned palette_bones)
{
    if (!pose || !pose->acquired || !animation || !animation->active ||
        (palette_result != 0 && palette_result != 1) ||
        (animation->current_clip != 0x47 && animation->current_clip != 0x15C &&
         (animation->current_clip < 0x40 || animation->current_clip > 0x42)))
        return 0;

    if (!pose->script_active || pose->script_clip != animation->current_clip) {
        if (animation->frame != 0 || !em_player_pose_select(pose, animation->current_clip, 0,
                                                            animation->transition ? 1 : 0, 1))
            return 0;
        pose->script_active = 1;
        pose->script_clip = animation->current_clip;
        /*00183090 clears the published advance result after initialization;
         * ordinary749A0/749F0 selection does not own that actor field. */
        pose->flags = 0;
    } else if (!em_player_pose_advance(pose, 1, 0)) {
        return 0;
    }

    /* Do not conceal a missed callback by seeking to a baked frame index. */
    if (remaining(pose) != animation->remaining || pose->flags != animation->flags ||
        !!pose->transition.active != !!animation->transition)
        return 0;
    if (!palette_result) return 1;
    return em_player_pose_palette(pose, local_palette, palette_bones);
}

static void identity(float matrix[16])
{
    memset(matrix, 0, 16 * sizeof *matrix);
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;
}

static void multiply(float out[16], const float a[16], const float b[16])
{
    /* Finite host hierarchy composition. Channel scalar arithmetic is
     * independently exact against saved reference execution; this matrix
     * path has a measured tolerance and makes no VU bit-equality claim. */
    for (unsigned column = 0; column < 4; ++column) {
        for (unsigned row = 0; row < 4; ++row) {
            float value = a[row] * b[column * 4];
            for (unsigned k = 1; k < 4; ++k)
                value += a[k * 4 + row] * b[column * 4 + k];
            out[column * 4 + row] = value;
        }
    }
}

int em_player_pose_palette(const EmPlayerPose *pose, float *local_palette, unsigned palette_bones)
{
    if (!pose || !pose->valid || !local_palette || pose->bank->bone_count != 21 ||
        palette_bones != 22)
        return 0;
    for (unsigned bone = 0; bone < pose->bank->bone_count; ++bone) {
        float local[16];
        if (bone == 0) identity(local);
        else em_pose_channels_matrix(local, pose->channels + bone);
        int parent = pose->bank->parents[bone];
        float *out = local_palette + bone * 16;
        if (parent < 0) memcpy(out, local, sizeof local);
        else multiply(out, local_palette + parent * 16, local);
    }
    identity(local_palette + 21 * 16);
    return 1;
}
