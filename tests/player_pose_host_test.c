#include "game/em_game_internal.h"

#include <assert.h>
#include <math.h>

EmGameState g;
static EmTransitionFade fade;
static int quit;

const EmTransitionFade *em_frame_transition(void)
{
    return &fade;
}
void em_frame_request_quit(void)
{
    ++quit;
}

void palette_apply_placement(float *palette, uint32_t count, const float position[3], float yaw)
{
    float c = cosf(yaw), s = sinf(yaw);
    for (unsigned bone = 0; bone < count; ++bone) {
        float *matrix = palette + bone * 16;
        for (unsigned column = 0; column < 4; ++column) {
            float x = matrix[column * 4], z = matrix[column * 4 + 2];
            matrix[column * 4] = c * x + s * z;
            matrix[column * 4 + 2] = -s * x + c * z;
        }
        for (unsigned axis = 0; axis < 3; ++axis)
            matrix[12 + axis] += position[axis];
    }
}

static void reset(void)
{
    player_pose_unload();
    memset(&g, 0, sizeof g);
    g.model.bone_count = 22;
    g.status.health = 100;
    g.pos[0] = 250.8f;
    g.pos[1] = 229.9f;
    g.pos[2] = 209;
    g.yaw = .6108652949333191f;
    g.loco_rate = 1;
    em_transition_fade_init(&fade);
    quit = 0;
    assert(player_pose_load("assets/player_channels.empc"));
    assert(player_pose_opening_release());
}

static void expected(unsigned clip, float time, int transition)
{
    unsigned actual, flags;
    float remaining;
    int active;
    assert(player_pose_source(&actual, &remaining, &flags, &active));
    assert(actual == clip && remaining == time && active == transition);
}

static void ordinary(void)
{
    ++g.frame_no;
    assert(player_pose_stage() == 0);
    player_pose_finish_state();
    em_transition_fade_tick(&fade);
}

static int interaction(void *context)
{
    int mode = *(int *)context;
    if (mode == 1) return player_pose_acquire();
    float local[22 * 16];
    assert(player_pose_owned());
    if (player_pose_idle_tick(local) != 1 || !player_pose_publish(local)) return -1;
    if (mode == 3 && !player_pose_release()) return -1;
    return 1;
}

int main(void)
{
    reset();
    expected(0, 80, 0);

    reset();
    g.loco_entry_ticks = 8;
    player_pose_request(1, 64, 8, 1);
    for (unsigned i = 0; i < 8; ++i) {
        assert(player_pose_stage() == 0);
        --g.loco_entry_ticks;
    }
    expected(1, 56, 0);
    player_pose_entry_cancel();
    player_pose_finish_state();
    expected(1, 56, 0); /* State99 is assigned without a request. */
    assert(player_pose_stage() == 0 && player_pose_entry_return_tick());
    player_pose_finish_state();
    expected(0, 8, 1);
    for (unsigned i = 0; i < 8; ++i) {
        assert(player_pose_stage() == 0 && player_pose_entry_return_tick());
        player_pose_finish_state();
    }
    expected(0, 80, 0);
    assert(!player_pose_entry_return_tick());
    /* A pending small-stick turn freezes the original rate despite idle
     * mode0. The next callback's animation step must consume that zero. */
    g.loco_rate = 0;
    assert(player_pose_stage() == 0);
    expected(0, 80, 0);

    reset();
    ordinary();
    assert(player_pose_use_accepted());
    player_pose_finish_state();
    expected(0, 79, 0); /* Same-idle Use never restarts. */
    player_pose_request(2, 12, 0, 1);
    g.loco_mode = 1;
    g.loco_tier = 2;
    g.loco_upt = .3f;
    assert(player_pose_use_accepted());
    player_pose_finish_state();
    expected(0, 80, 0);
    assert(!g.loco_mode && !g.loco_tier && g.loco_upt == 0);
    /* Immutable03 has fade level251; after120 ordinary callbacks the
     * immutable04 idle cursor is40, with its countdown still at243. */
    fade = (EmTransitionFade){1, 0, 2, 251, 4};
    g.player_palette[0] = 42;
    for (unsigned i = 0; i < 120; ++i)
        ordinary();
    expected(0, 40, 0);
    assert(g.player_palette[0] == 42); /* Ordinary display remains separate. */
    for (unsigned i = 0; i < 243; ++i)
        ordinary();
    unsigned clip;
    assert(player_pose_source(&clip, NULL, NULL, NULL) && clip == 0);
    ordinary();
    expected(0x15D, 8, 1);
    for (unsigned i = 0; i < 188; ++i)
        ordinary();
    expected(0, 8, 1);
    for (unsigned i = 0; i < 8; ++i)
        ordinary();
    expected(0, 80, 0);

    reset();
    int mode = 1;
    player_pose_set_stage_hook(interaction, &mode);
    assert(player_pose_stage() == 1);
    expected(0, 79, 0); /* Existing advance, then same-idle acquisition. */
    mode = 2;
    assert(player_pose_stage() == 1);
    expected(0, 78, 0); /* Owned worker advances exactly once. */
    mode = 3;
    assert(player_pose_stage() == 1);
    expected(0, 77, 0); /* Release consumes this callback, no added idle tick. */
    player_pose_set_stage_hook(NULL, NULL);
    ordinary();
    expected(0, 76, 0);

    /* Alignment and orientation update caches without advancing channels.
     * Op4 changes live yaw; saved Euler changes only at align/player tail. */
    player_pose_finish_palette();
    float saved[3], hip_before[3], hip_after[3];
    assert(player_pose_script_euler(saved) && player_pose_hip(hip_before));
    float target[3] = {g.pos[0] - 2, g.pos[1] + 1, g.pos[2] + 3};
    assert(player_pose_align(target));
    assert(player_pose_hip(hip_after));
    assert(fabsf(hip_after[0] - hip_before[0] + 2) < .0001f);
    assert(fabsf(hip_after[1] - hip_before[1] - 1) < .0001f);
    assert(fabsf(hip_after[2] - hip_before[2] - 3) < .0001f);
    assert(player_pose_face(-1.3037610054016113f));
    float current[3];
    assert(player_pose_script_euler(current) && current[1] == saved[1]);
    player_pose_finish_palette();
    assert(player_pose_script_euler(current) && current[1] == g.yaw);
    expected(0, 76, 0);

    /* Script release resets its default at the end of the consumed frame. */
    assert(player_pose_acquire() == 1);
    EmInteractionAnimation animation = {
        .current_clip = 0x47, .duration = 200, .remaining = 200, .active = 1};
    float local[22 * 16];
    assert(player_pose_script_tick(&animation, 1, local));
    assert(player_pose_publish(local));
    assert(player_pose_release());
    expected(0, 80, 0);

    player_pose_invalidate("intentional unsupported-source test");
    assert(!player_pose_source(NULL, NULL, NULL, NULL));
    mode = 1;
    player_pose_set_stage_hook(interaction, &mode);
    assert(player_pose_stage() == -1 && quit == 1);
    player_pose_unload();
    puts("player pose host PASS: real idle/fade timing, ownership callback order, alignment/Euler "
         "mirrors, explicit unsupported source");
    return 0;
}
