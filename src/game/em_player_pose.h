#ifndef EM_PLAYER_POSE_H
#define EM_PLAYER_POSE_H

#include "game/em_interaction_animation.h"
#include "game/em_pose_bank.h"

/* The source state is the original base clip's channels. A displayed blend
 * between gait tiers must never replace these with decomposed matrices. */
typedef struct {
    const EmPoseBank *bank;
    EmPosePlayback playback;
    EmPoseTransition transition;
    EmPoseChannels channels[EM_POSE_NODE_MAX];
    float reset_frame;
    unsigned flags;
    int valid;
    int acquired;
    int script_active;
    unsigned script_clip;
} EmPlayerPose;

/* Seed only from an explicit, recovered original clip/source frame. The bank
 * must outlive the state. This does not infer a source pose from the renderer. */
int em_player_pose_init(EmPlayerPose *pose, const EmPoseBank *bank, unsigned clip,
                        float source_frame);
/* 001749A0/001749F0: force=0 preserves a current equal clip; force=1 performs
 * the initializer even for the same clip. Source time is sampled before the
 * transition, then reset to its integer conversion when that interval ends. */
int em_player_pose_select(EmPlayerPose *pose, unsigned clip, float source_frame,
                          unsigned blend_ticks, int force);
/* Call before the ordinary state callback. Updated source channels remain
 * published in pose->channels. A state callback may then select another clip.
 * Status/menu frames must not call this worker. */
int em_player_pose_advance(EmPlayerPose *pose, float rate, int freeze_motion);
/*0017B660 source-state side effects after the ordinary locomotion callback.
 * The adjacent-tier display blend is separate; when blend <1 the original
 * restores base-clip channels at its integer source cursor. */
int em_player_pose_gait_base(EmPlayerPose *pose, unsigned tier, unsigned substate, float blend);

/* 0015B130 -> 00174A50 -> 00182D70. The host has already advanced the old
 * source this callback. Readiness is immediate; this consumes no idle tick. */
int em_player_pose_acquire(EmPlayerPose *pose);
int em_player_pose_idle_tick(EmPlayerPose *pose, float *local_palette, unsigned palette_bones);
/* 00182DF0, limited to the exported healthy first-level clip row. Release
 * from40..42/47/15C forces idle with blend0; other clips follow table flags. */
int em_player_pose_release(EmPlayerPose *pose);

/* Optional EmInteractionRuntime pose worker. Call after EVERY scripted
 * animation tick, including the blend1 callback that preserves the palette.
 * Validate its temporal state and replace published baked palettes with the
 * corresponding original channels. The existing timing core remains intact. */
int em_player_pose_script_tick(EmPlayerPose *pose, const EmInteractionAnimation *animation,
                               int palette_result, float *local_palette, unsigned palette_bones);

/* Actor-local hierarchy for the verified player skeleton:21 original nodes
 * plus its trailing identity palette slot. No owner placement is applied.
 * Node adjustment channels are identity in the validated first-level slice. */
int em_player_pose_palette(const EmPlayerPose *pose, float *local_palette, unsigned palette_bones);

#endif
