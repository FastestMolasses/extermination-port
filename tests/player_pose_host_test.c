#include "game/em_game_internal.h"
#include "game/em_player.h"
#include "game/em_player_stage_workers.h"

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

/* The player record the pose lives in, and the canonical bytes the attach
 * names (D_008106F3, D_008106F1, D_00810707). */
static EmPlayerLiveActor actor;
static uint8_t d8106F3, d8106F1, d810707;
static EmPlayerStageScene stage_scene = { .d8106F1 = &d8106F1 };
static EmPlayerStageGlobals stage_globals = { .d810707 = &d810707 };

static void reset(void)
{
    player_pose_unload();
    memset(&actor, 0, sizeof actor);
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
    assert(player_pose_load(PLAYER_CLIP_BANK_PATH, PLAYER_CLIP_ROW0_PATH));
    assert(player_pose_attach(&actor, &d8106F3, &stage_scene, &stage_globals));
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

static int use(void *context)
{
    ++*(unsigned *)context;
    return player_pose_use_accepted_port() ? 1 : -1;
}

int main(void)
{
    reset();
    expected(0, 80, 0);

    unsigned polls = 0;
    player_use_set_hook(use, &polls);
    assert(!player_use_poll() && !polls); /* idle case0 */
    ordinary();
    fade.substate = 1;
    assert(!player_use_poll() && !polls); /* gated idle case1 */
    g.loco_entry_ticks = 8;
    assert(player_use_poll() == 1 && polls == 1); /* case2 ignores fade */
    player_pose_entry_cancel();
    assert(!player_use_poll() && polls == 1); /* case99 */
    assert(player_pose_entry_return_tick());
    assert(!player_use_poll() && polls == 1); /* case100 */
    reset();
    player_use_set_hook(use, &polls);
    g.loco_mode = 4;
    g.loco_reentry.phase = 1;
    assert(!player_use_poll() && polls == 1); /* walking case63 */
    g.loco_reentry.phase = 2;
    assert(player_use_poll() == 1 && polls == 2); /* next case0 falls to1 */
    reset();

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
    /* A Use press 00160220 took: the record's clip request is 001798D0's
     * 00174A50(p, 0.0) (em_player_use_dispatch, test_player_use_dispatch_reference);
     * the pose host's half only leaves the port's own locomotion. */
    assert(player_pose_use_accepted_port());
    player_pose_finish_state();
    expected(0, 79, 0); /* no request of its own: the idle keeps its cursor */
    player_pose_request(2, 12, 0, 1);
    g.loco_mode = 1;
    g.loco_tier = 2;
    g.loco_upt = .3f;
    assert(player_pose_use_accepted_port());
    assert(!g.loco_mode && !g.loco_tier && g.loco_upt == 0);

    for (unsigned tier = 1; tier <= 2; ++tier) {
        reset();
        player_pose_request(tier, tier == 1 ? 64 : 10, 0, 1);
        g.loco_tier = tier;
        g.loco_mode = 3;
        assert(player_pose_foot_stop_begin());
        if (tier == 2) expected(4, 10, 1);
        assert(player_pose_foot_stop_palette() == 1);
        unsigned ticks = 0;
        while (player_pose_foot_stop_active()) {
            assert(player_pose_stage() == 0);
            g.loco_rate = 1; /* ordinary player-stage reset */
            assert(player_pose_foot_stop_tick() >= 0);
            player_pose_finish_state();
            assert(player_pose_foot_stop_palette() == 1);
            assert(++ticks < 80);
        }
        assert(g.loco_stop.phase == 3);
        assert(player_pose_stage() == 0);
        g.loco_stop.phase = 4;
        player_pose_idle_enter();
        player_pose_finish_state();
        expected(0, 12, 1);
        for (unsigned i = 0; i < 12; ++i) {
            assert(player_pose_stage() == 0);
            g.loco_rate = 1;
            player_pose_finish_state();
            assert(player_pose_foot_stop_palette() == 1);
        }
        expected(0, 80, 0);
        assert(player_pose_use_accepted_port());
        assert(!player_pose_foot_stop_active() && !g.loco_stop.phase);
    }
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
    memcpy(hip_before, hip_after, sizeof hip_before);
    assert(player_pose_face(-1.3037610054016113f));
    float current[3];
    assert(player_pose_script_euler(current) && current[1] == saved[1]);
    assert(player_pose_hip(hip_after));
    assert(!memcmp(hip_before, hip_after, sizeof hip_before));
    player_pose_finish_palette();
    assert(player_pose_script_euler(current) && current[1] == g.yaw);
    assert(player_pose_hip(hip_after));
    assert(!memcmp(hip_after, g.player_palette + 28, sizeof hip_after));
    expected(0, 76, 0);

    /* Refusal align->face->camera and panel face->align both preserve
     * the original hip until the following real player callback. */
    reset();
    for (unsigned i = 0; i < 5; ++i) ordinary();
    assert(player_pose_acquire() == 1);
    float lever[3] = {222, 230, 250};
    assert(player_pose_align(lever));
    assert(player_pose_hip(hip_before));
    assert(player_pose_face(-1.3037610054016113f));
    assert(player_pose_hip(hip_after));
    assert(!memcmp(hip_before, hip_after, sizeof hip_before));
    assert(memcmp(hip_after, g.player_palette + 28, sizeof hip_after));
    assert(player_pose_align(lever));
    assert(player_pose_hip(hip_after));
    assert(!memcmp(hip_before, hip_after, sizeof hip_before));
    player_pose_finish_palette();
    assert(player_pose_hip(hip_after));
    assert(!memcmp(hip_after, g.player_palette + 28, sizeof hip_after));
    assert(player_pose_release());

    /* Script release resets its default at the end of the consumed frame. */
    assert(player_pose_acquire() == 1);
    EmInteractionAnimation animation = {
        .current_clip = 0x47, .duration = 200, .remaining = 200, .active = 1};
    float local[22 * 16];
    assert(player_pose_script_tick(&animation, 1, local));
    assert(player_pose_publish(local));
    assert(player_pose_release());
    expected(0, 80, 0);

    /* WP-2/H12: a legacy stand-in (aim) freezes the source; its release
     * re-seeds the row default like00182DF0 via1C63E0 (clip0, frame0, no
     * blend, idle state0). The foot-stop and acquisition keep working. */
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        reset();
        for (unsigned i = 0; i < 5; ++i) ordinary();
        expected(0, 75, 0);
        for (unsigned i = 0; i < 30; ++i) {
            ++g.frame_no;
            assert(player_pose_stage() == 0);
            player_pose_legacy_hold("test aim stand-in");
            player_pose_finish_state();
        }
        assert(!player_pose_source(NULL, NULL, NULL, NULL)); /* frozen, not advanced */
        assert(!player_pose_foot_stop_begin() && !player_pose_idle_state_wait());
        player_pose_request(2, 10, 0, 1);                   /* ignored while held */
        if (attempt == 1) {
            /* Acquisition from the stand-in (0015B130 from armed stances). */
            assert(player_pose_acquire() == 1);
            expected(0, 80, 0);
            assert(player_pose_release());
            expected(0, 80, 0);
            continue;
        }
        assert(player_pose_stage() == 0); /* release frame: no advance while held */
        g.loco_mode = 1;
        g.loco_tier = 2;
        assert(player_pose_legacy_release() == 1);
        expected(0, 80, 0);
        assert(!g.loco_mode && !g.loco_tier && g.idle_timer == 300);
        assert(player_pose_idle_state_wait());
        player_pose_finish_state(); /* idle case0 request is the same clip */
        expected(0, 80, 0);
        ordinary();
        expected(0, 79, 0);
        /* Jog, then the tier-2 foot-placement stop still fires. */
        player_pose_request(2, 10, 0, 1);
        g.loco_tier = 2;
        g.loco_mode = 3;
        assert(player_pose_foot_stop_begin());
        expected(4, 10, 1);
        unsigned ticks = 0;
        while (player_pose_foot_stop_active()) {
            assert(player_pose_stage() == 0);
            g.loco_rate = 1;
            assert(player_pose_foot_stop_tick() >= 0);
            player_pose_finish_state();
            assert(++ticks < 80);
        }
        assert(g.loco_stop.phase == 3);
        /* The run tier's clip5 stop request is accepted again as well. */
        player_pose_request(5, 0, 6, 1);
        expected(5, 6, 1); /* six-tick blend toward clip5 */
        assert(player_pose_acquire() == 1);
        assert(player_pose_release());
    }

    /* Held stand-ins the host checks itself keep holding until they end. */
    reset();
    player_pose_legacy_hold("test hit stand-in");
    g.pd_state = 2;
    assert(player_pose_stage() == 0 && player_pose_legacy_release() == 0);
    /* Acquire must not re-seed past a busy original state (hit / sa_*):
     * 00182B30/0015B610 admission is not modelled, so it is refused. */
    assert(player_pose_acquire() == -1 && !player_pose_owned());
    assert(!player_pose_source(NULL, NULL, NULL, NULL)); /* still held */
    g.pd_state = 0;
    g.sa_cur = 0x111;
    assert(player_pose_legacy_release() == 0);
    assert(player_pose_acquire() == -1 && !player_pose_owned());
    assert(!player_pose_source(NULL, NULL, NULL, NULL));
    g.sa_cur = 0;
    g.sa_req = 0x111;
    assert(player_pose_legacy_release() == 0);
    assert(player_pose_acquire() == -1 && !player_pose_owned());
    assert(!player_pose_source(NULL, NULL, NULL, NULL));
    g.sa_req = 0;
    g.status.health = PD_LOW_HEALTH; /* row1 default clip0x0A is not exported */
    assert(player_pose_legacy_release() == 0 && player_pose_acquire() == -1);
    g.status.health = 100;
    assert(player_pose_legacy_release() == 1);
    expected(0, 80, 0);
    assert(player_pose_acquire() == 1 && player_pose_release());

    /* 0017C030 mode 3 runs the 0017B910 solve even while a pose blend is
     * active (no blend gate; the old refusal here was not original). The
     * begin uses the transition's current channels and clock: tier 2
     * requests clip 4 with blend 10 from the blended pose, tier 1 keeps
     * the walk clip and halves (clock - 1). Both run to phase 3. */
    for (unsigned tier = 1; tier <= 2; ++tier) {
        reset();
        for (unsigned i = 0; i < 5; ++i) ordinary();
        player_pose_request(tier, tier == 1 ? 64 : 10, 12, 1); /* blend12 */
        for (unsigned i = 0; i < 3; ++i) assert(player_pose_stage() == 0);
        expected(tier, 9, 1);
        g.loco_tier = tier;
        g.loco_mode = 3;
        assert(player_pose_foot_stop_begin());
        if (tier == 2) expected(4, 10, 1);
        else expected(1, 9, 1);
        unsigned ticks = 0;
        while (player_pose_foot_stop_active()) {
            assert(player_pose_stage() == 0);
            g.loco_rate = 1;
            assert(player_pose_foot_stop_tick() >= 0);
            player_pose_finish_state();
            assert(player_pose_foot_stop_palette() == 1);
            assert(++ticks < 80);
        }
        assert(g.loco_stop.phase == 3);
    }

    /* The native unsupported path itself: reported once, held, then
     * re-seeded to the row default. */
    reset();
    for (unsigned i = 0; i < 5; ++i) ordinary();
    player_pose_unsupported_hold("test unsupported path");
    assert(!player_pose_source(NULL, NULL, NULL, NULL));
    assert(player_pose_legacy_release() == 1);
    expected(0, 80, 0);

    player_pose_invalidate("intentional unsupported-source test");
    assert(!player_pose_source(NULL, NULL, NULL, NULL));
    mode = 1;
    player_pose_set_stage_hook(interaction, &mode);
    assert(player_pose_stage() == -1 && quit == 1);
    player_pose_unload();
    puts("player pose host PASS: real idle/fade timing, ownership callback order, alignment/Euler "
         "mirrors, legacy hold/re-seed, explicit unsupported source");
    return 0;
}
