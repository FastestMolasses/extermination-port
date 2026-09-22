#ifndef EM_POSE_BANK_H
#define EM_POSE_BANK_H
#include "game/em_pose_transition.h"

typedef struct {
    uint16_t time;
    uint16_t hold;
    float value[4];
} EmPoseKey;
typedef struct {
    uint32_t count;
    EmPoseKey *keys;
} EmPoseTrack;
typedef struct {
    uint16_t id, duration;
    int16_t next_clip;
    uint16_t blend;
    EmPoseTrack tracks[EM_POSE_NODE_MAX][3]; /* rotation, translation, scale */
} EmPoseClip;
typedef struct {
    unsigned bone_count, clip_count;
    int32_t parents[EM_POSE_NODE_MAX];
    EmPoseClip *clips;
} EmPoseBank;
typedef struct {
    unsigned index;
    float remaining, reciprocal, fraction;
    float value[3], velocity[3];
} EmPoseCursor;
typedef struct {
    const EmPoseBank *bank;
    const EmPoseClip *clip;
    float remaining;
    unsigned flags;
    EmPoseCursor nodes[EM_POSE_NODE_MAX][3];
} EmPosePlayback;

int em_pose_bank_load(EmPoseBank *bank, const char *path);
void em_pose_bank_free(EmPoseBank *bank);
int em_pose_playback_begin(EmPosePlayback *state, const EmPoseBank *bank, unsigned clip,
                           float source_frame);
/* Original normal (non-transition) clip clock, splitting dt into <=1 steps.
 * freeze_motion mirrors the original camera-cut latch after channel advance. */
int em_pose_playback_advance(EmPosePlayback *state, float dt, int freeze_motion);
int em_pose_playback_channels(const EmPosePlayback *state, EmPoseChannels *out);
#endif
