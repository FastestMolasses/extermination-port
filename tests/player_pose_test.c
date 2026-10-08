#include "game/em_player_pose.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void assert_idle_zero(const EmPlayerPose *pose, const EmPoseBank *bank)
{
    EmPosePlayback idle;
    EmPoseChannels channels[EM_POSE_NODE_MAX];
    assert(em_pose_playback_begin(&idle, bank, 0, 0));
    assert(em_pose_playback_channels(&idle, channels));
    assert(pose->playback.clip->id == 0 && !pose->transition.active);
    assert(pose->playback.remaining == 80);
    assert(memcmp(channels, pose->channels, 21 * sizeof *channels) == 0);
}

int main(void)
{
    EmPoseBank bank = {0};
    EmPlayerPose pose;
    EmPoseChannels before[EM_POSE_NODE_MAX];
    assert(em_pose_bank_load(&bank, "assets/player_channels.empc"));

    /* 001749A0 with force 0 (00174A50's request, blend 8) keeps an already
     * current default clip's cursor. */
    assert(em_player_pose_init(&pose, &bank, 0, 5));
    memcpy(before, pose.channels, sizeof before);
    assert(em_player_pose_select(&pose, 0, 0, 8, 0));
    assert(pose.playback.remaining == 75 && !pose.transition.active);
    assert(memcmp(before, pose.channels, sizeof before) == 0);
    assert(em_player_pose_advance(&pose, 1, 0));
    assert(pose.playback.remaining == 74);

    /* A walking source gets the real eight-callback transition. */
    assert(em_player_pose_init(&pose, &bank, 1, 64));
    assert(em_player_pose_advance(&pose, .75f, 0));
    memcpy(before, pose.channels, sizeof before);
    assert(em_player_pose_select(&pose, 0, 0, 8, 0));
    assert(pose.transition.active && pose.transition.remaining == 8);
    assert(memcmp(before, pose.channels, 21 * sizeof *before) == 0);
    for (unsigned i = 0; i < 7; ++i) {
        assert(em_player_pose_advance(&pose, 1, 0));
        assert(pose.transition.active);
    }
    assert(em_player_pose_advance(&pose, 1, 0));
    assert_idle_zero(&pose, &bank);
    assert(em_player_pose_advance(&pose, 1, 0));
    assert(pose.playback.remaining == 79);

    /* Interrupted transitions freeze their actual current channels. */
    assert(em_player_pose_select(&pose, 1, 64, 8, 1));
    assert(em_player_pose_advance(&pose, 2.75f, 0));
    memcpy(before, pose.channels, sizeof before);
    assert(em_player_pose_select(&pose, 5, 4, 6, 1));
    assert(memcmp(before, pose.channels, 21 * sizeof *before) == 0);
    assert(em_player_pose_advance(&pose, 6, 0));
    assert(!pose.transition.active && pose.playback.remaining == 6);

    em_pose_bank_free(&bank);
    printf("player pose PASS: conditional request, the eight-callback transition, interrupted "
           "channels\n");
    return 0;
}
