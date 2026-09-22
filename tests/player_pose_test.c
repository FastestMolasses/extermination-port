#include "game/em_player_pose.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* The temporal worker needs metadata and an intentionally distinguishable
 * baked-palette boundary. Actual pose channels come only from the raw bank. */
int em_model_clip_index(const EmModel *model, uint32_t id)
{
    for (unsigned i = 0; i < model->clip_count; ++i)
        if (model->clips[i].id == id) return (int)i;
    return -1;
}

void em_model_palette_at(const EmModel *model, uint32_t clip, double frame, float *out)
{
    (void)clip;
    (void)frame;
    for (unsigned i = 0; i < model->bone_count * 16; ++i)
        out[i] = 12345;
}

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
    float palette[22 * 16];
    assert(em_pose_bank_load(&bank, "assets/player_channels.empc"));

    /* Acquiring the already-current default clip must not restart it. */
    assert(em_player_pose_init(&pose, &bank, 0, 5));
    memcpy(before, pose.channels, sizeof before);
    assert(em_player_pose_acquire(&pose));
    assert(pose.playback.remaining == 75 && !pose.transition.active);
    assert(memcmp(before, pose.channels, sizeof before) == 0);
    assert(em_player_pose_idle_tick(&pose, palette, 22) == 1);
    assert(pose.playback.remaining == 74);
    assert(em_player_pose_release(&pose));
    assert(pose.playback.remaining == 74);

    /* A walking source gets the real eight-callback transition. */
    assert(em_player_pose_init(&pose, &bank, 1, 64));
    assert(em_player_pose_advance(&pose, .75f, 0));
    memcpy(before, pose.channels, sizeof before);
    assert(em_player_pose_acquire(&pose));
    assert(pose.transition.active && pose.transition.remaining == 8);
    assert(memcmp(before, pose.channels, 21 * sizeof *before) == 0);
    for (unsigned i = 0; i < 7; ++i) {
        assert(em_player_pose_idle_tick(&pose, palette, 22) == 1);
        assert(pose.transition.active);
    }
    assert(em_player_pose_idle_tick(&pose, palette, 22) == 1);
    assert_idle_zero(&pose, &bank);
    assert(em_player_pose_idle_tick(&pose, palette, 22) == 1);
    assert(pose.playback.remaining == 79);
    assert(em_player_pose_release(&pose));

    /* Interrupted transitions freeze their actual current channels. */
    assert(em_player_pose_select(&pose, 1, 64, 8, 1));
    assert(em_player_pose_advance(&pose, 2.75f, 0));
    memcpy(before, pose.channels, sizeof before);
    assert(em_player_pose_select(&pose, 5, 4, 6, 1));
    assert(memcmp(before, pose.channels, 21 * sizeof *before) == 0);
    assert(em_player_pose_advance(&pose, 6, 0));
    assert(!pose.transition.active && pose.playback.remaining == 6);

    EmModelClip clips[] = {{0x47, 0, 200, 60}, {0x15C, 200, 121, 60},
                           {0x40, 321, 45, 60}, {0x41, 366, 45, 60}, {0x42, 411, 45, 60}};
    EmModel model = {0};
    model.bone_count = 22;
    model.clips = clips;
    model.clip_count = sizeof clips / sizeof *clips;
    model.palette = palette;
    unsigned callbacks = 0;
    for (unsigned clip = 0; clip < model.clip_count; ++clip) {
        for (unsigned blend = 0; blend < 2; ++blend) {
            EmInteractionAnimation animation;
            em_interaction_animation_clear(&animation);
            assert(em_player_pose_init(&pose, &bank, 2, 27));
            assert(em_player_pose_acquire(&pose));
            for (unsigned i = 0; i < 3; ++i)
                assert(em_player_pose_idle_tick(&pose, palette, 22) == 1);
            assert(em_interaction_animation_request(&animation, &model, clips[clip].id, 1,
                                                    (float)blend));
            for (unsigned frame = 0; frame < clips[clip].frame_count + 5; ++frame) {
                int result = em_interaction_animation_tick(&animation, &model, palette);
                assert(em_player_pose_script_tick(&pose, &animation, result, palette, 22));
                if (result) {
                    assert(palette[0] == 1 && palette[21 * 16] == 1);
                    for (unsigned i = 0; i < 22 * 16; ++i)
                        assert(isfinite(palette[i]));
                }
                ++callbacks;
                /* Repeating a current request must keep both clocks moving. */
                if (frame == 10)
                    assert(
                        em_interaction_animation_request(&animation, &model, clips[clip].id, 1, 1));
            }
            assert(em_interaction_animation_done(&animation) == 1);
            assert(em_player_pose_release(&pose));
            assert_idle_zero(&pose, &bank);
        }
    }

    /* A missing worker callback is a fault, not an implicit seek. */
    assert(em_player_pose_acquire(&pose));
    EmInteractionAnimation animation;
    em_interaction_animation_clear(&animation);
    assert(em_interaction_animation_request(&animation, &model, 0x47, 1, 0));
    int result = em_interaction_animation_tick(&animation, &model, palette);
    assert(em_player_pose_script_tick(&pose, &animation, result, palette, 22));
    assert(em_interaction_animation_tick(&animation, &model, palette) == 1);
    result = em_interaction_animation_tick(&animation, &model, palette);
    assert(!em_player_pose_script_tick(&pose, &animation, result, palette, 22));

    em_pose_bank_free(&bank);
    printf("player pose PASS: conditional takeover/release, interrupted channels, %u scripted "
           "callbacks\n",
           callbacks);
    return 0;
}
