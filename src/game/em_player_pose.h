#ifndef EM_PLAYER_POSE_H
#define EM_PLAYER_POSE_H

#include "em_model.h"
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

/* Actor-local hierarchy for the verified player skeleton:21 original nodes
 * plus its trailing identity palette slot. No owner placement is applied.
 * Node adjustment channels are identity in the validated first-level slice. */
int em_player_pose_palette(const EmPlayerPose *pose, float *local_palette, unsigned palette_bones);

#endif
