#include "game/em_interaction_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int acquire_result, acquires, idles, published, releases, cameras, events;
    int release_result, last_event;
    int pose_calls, pose_commits;
    int cinematic_calls, cinematic_result;
    float last_pose;
} Host;

static int acquire(void *context)
{
    Host *h = context;
    h->acquires++;
    return h->acquire_result;
}
static int idle(void *context, float *palette)
{
    Host *h = context;
    h->idles++;
    palette[0] = -42;
    return 1;
}
static int publish(void *context, const float *palette)
{
    Host *h = context;
    h->published++;
    h->last_pose = palette[0];
    h->last_event = 1;
    return 1;
}
static int release(void *context)
{
    Host *h = context;
    assert(h->last_event == 1);
    h->releases++;
    h->last_event = 2;
    return h->release_result;
}
static int frame_event(void *context, EmInteractionFrameEvent event)
{
    Host *h = context;
    (void)event;
    h->events++;
    return 1;
}
static int camera(void *context)
{
    Host *h = context;
    h->cameras++;
    return 1;
}

static int cinematic_player(void *context, float *palette)
{
    Host *host = context;
    ++host->cinematic_calls;
    palette[0] = 96;
    return host->cinematic_result;
}

static int pose_worker(void *context, const EmInteractionAnimation *animation,
                       int palette_result, float *palette)
{
    Host *host = context;
    assert(animation->active);
    assert(animation->current_clip == 0x47 || animation->current_clip == 0x15C);
    assert(palette);
    ++host->pose_calls;
    if (!palette_result) {
        assert(animation->transition && animation->current_clip == 0x47);
        ++host->pose_commits;
    }
    return 1;
}
static void word(unsigned char *record, unsigned offset, unsigned value)
{
    for (unsigned i = 0; i < 4; i++)
        record[offset + i] = (unsigned char)(value >> (8 * i));
}

int main(void)
{
    EmModelClip clips[] = {{0x47, 0, 200, 60}, {0x15C, 200, 121, 60}};
    EmModel model = {.bone_count = 22, .frame_count = 321, .clip_count = 2, .clips = clips};
    model.palette = calloc(321 * 22 * 16, sizeof(float));
    assert(model.palette);
    for (unsigned f = 0; f < 321; f++)
        model.palette[f * 22 * 16] = (float)f;
    float local[22 * 16] = {0};
    EmInteractionFrame frame = {0};
    Host host = {.acquire_result = 0, .release_result = 1};
    EmInteractionRuntimeHooks hooks = {&host, acquire, idle, release, publish, frame_event, camera};
    EmInteractionRuntime runtime;
    int panel = 0, elevator = 0;
    assert(em_interaction_runtime_init(&runtime, &frame, &model, local, &hooks));
    assert(em_interaction_runtime_set_pose_worker(&runtime, pose_worker));
    assert(em_interaction_runtime_claim(&runtime, &panel));
    assert(!em_interaction_runtime_set_pose_worker(&runtime, NULL));
    assert(frame.selector == 3 && !em_interaction_runtime_claim(&runtime, &elevator));
    assert(!em_interaction_runtime_animation_start(&runtime, &elevator, 0x47, 1, 1));
    assert(!runtime.failed && em_interaction_runtime_owner(&runtime) == &panel);
    unsigned char record[64] = {0};
    word(record, 0, 7);
    word(record, 8, 2);
    EmScript script = {0};
    assert(em_interaction_runtime_frame(&runtime, &panel, &script, record) == EM_SCRIPT_WAIT);
    assert(frame.selector == 2 && frame.camera_top == 1 && script.phase == 1);
    assert(em_interaction_runtime_camera_owned(&runtime));
    assert(em_interaction_runtime_player_tick(&runtime, 0) == 0 && host.acquires == 0);
    assert(em_interaction_runtime_player_tick(&runtime, 1) == 0 && host.acquires == 1);
    assert(!frame.player_ready);
    assert(em_interaction_runtime_frame(&runtime, &panel, &script, record) == EM_SCRIPT_WAIT);
    host.acquire_result = 1;
    assert(em_interaction_runtime_player_tick(&runtime, 1) == 1);
    assert(frame.player_ready == 1 && host.idles == 0 && host.published == 0);
    assert(em_interaction_runtime_frame(&runtime, &panel, &script, record) == EM_SCRIPT_ADVANCE);
    assert(frame.ready == 1 && script.skip_phase == 1);
    assert(em_interaction_runtime_camera_retarget(&runtime, &panel));
    assert(host.cameras == 1);
    assert(em_interaction_runtime_player_tick(&runtime, 1) == 1);
    assert(host.idles == 1 && host.published == 1 && host.last_pose == -42);

    /* A status pause cannot consume a pending commit, a cursor, or release.
     * Resume publishes15C frame0 once; exactly121 later callbacks set1000. */
    assert(em_interaction_runtime_animation_start(&runtime, &panel, 0x15C, 1, 0));
    EmInteractionAnimation before = runtime.animation;
    for (int i = 0; i < 150; i++)
        assert(em_interaction_runtime_player_tick(&runtime, 0) == 0);
    assert(!memcmp(&before, &runtime.animation, sizeof before));
    assert(host.published == 1 && host.idles == 1);
    assert(host.pose_calls == 0);
    for (int i = 0; i <= 121; i++) {
        assert(em_interaction_runtime_player_tick(&runtime, 1) == 1);
        assert(em_interaction_runtime_animation_done(&runtime, &panel) == (i == 121));
    }
    assert(host.last_pose == 320);
    assert(host.pose_calls == 122 && host.pose_commits == 0);
    for (int i = 0; i < 8; i++)
        assert(em_interaction_runtime_player_tick(&runtime, 1) == 1);
    assert(host.last_pose == 320 && runtime.animation.frame == 120);
    word(record, 8, 4);
    assert(em_interaction_runtime_frame(&runtime, &panel, &script, record) == EM_SCRIPT_ADVANCE);
    assert(!frame.selector && frame.player_ready == 1 && !frame.ready);
    assert(em_interaction_runtime_owner(&runtime) == &panel);
    int prior = host.published;
    assert(em_interaction_runtime_player_tick(&runtime, 0) == 0 && host.releases == 0);
    assert(em_interaction_runtime_player_tick(&runtime, 1) == 1);
    assert(host.published == prior + 1 && host.releases == 1 && host.last_event == 2);
    assert(!em_interaction_runtime_owner(&runtime) && !frame.player_ready);
    assert(!runtime.animation.active);

    /* Elevator blend1 commits without publishing, preserving the prior pose. */
    assert(em_interaction_runtime_claim(&runtime, &elevator));
    assert(em_interaction_runtime_player_tick(&runtime, 1) == 1);
    assert(em_interaction_runtime_animation_start(&runtime, &elevator, 0x47, 1, 1));
    prior = host.published;
    assert(em_interaction_runtime_player_tick(&runtime, 1) == 1);
    assert(host.published == prior && runtime.animation.transition);
    assert(host.pose_commits == 1);
    assert(em_interaction_runtime_player_tick(&runtime, 1) == 1);
    assert(host.published == prior + 1 && host.last_pose == 0);
    /* Release failure preserves ownership, preventing ordinary motion from
     * overwriting an unresolved pose or another owner from entering. */
    frame.selector = 0;
    host.release_result = 0;
    assert(em_interaction_runtime_player_tick(&runtime, 1) == -1 && runtime.failed);
    assert(em_interaction_runtime_owner(&runtime) == &elevator && frame.player_ready == 1);
    assert(!em_interaction_runtime_claim(&runtime, &panel));
    assert(em_interaction_runtime_animation_done(&runtime, &elevator) == -1);

    /* Missing acquisition does not fabricate readiness. */
    memset(&frame, 0, sizeof frame);
    hooks.acquire_player = NULL;
    assert(em_interaction_runtime_init(&runtime, &frame, &model, local, &hooks));
    assert(em_interaction_runtime_claim(&runtime, &panel));
    assert(em_interaction_runtime_player_tick(&runtime, 1) == -1 && !frame.player_ready);

    /* Ready2 requires the attached-face/body worker. Status suppresses the
     * whole callback, and leaving ready2 resumes the ordinary idle worker. */
    memset(&frame, 0, sizeof frame);
    hooks.acquire_player = acquire;
    assert(em_interaction_runtime_init(&runtime, &frame, &model, local, &hooks));
    assert(em_interaction_runtime_claim(&runtime, &panel));
    frame.player_ready = 2;
    assert(em_interaction_runtime_player_tick(&runtime, 1) == -1 && runtime.failed);
    assert(em_interaction_runtime_owner(&runtime) == &panel);
    memset(&frame, 0, sizeof frame);
    assert(em_interaction_runtime_init(&runtime, &frame, &model, local, &hooks));
    assert(em_interaction_runtime_set_cinematic_player_worker(&runtime, cinematic_player));
    assert(em_interaction_runtime_claim(&runtime, &panel));
    assert(!em_interaction_runtime_set_cinematic_player_worker(&runtime, NULL));
    frame.player_ready = 2;
    host.cinematic_result = 1;
    prior = host.published;
    for (unsigned i = 0; i < 120; ++i)
        assert(em_interaction_runtime_player_tick(&runtime, 0) == 0);
    assert(host.published == prior && !host.cinematic_calls);
    assert(em_interaction_runtime_player_tick(&runtime, 1) == 1);
    assert(host.cinematic_calls == 1 && host.last_pose == 96 && host.published == prior + 1);
    frame.player_ready = 1; /* original frame/sub4 releases the face */
    frame.selector = 0;
    host.release_result = 1;
    assert(em_interaction_runtime_player_tick(&runtime, 1) == 1);
    assert(host.cinematic_calls == 1 && host.last_pose == -42 && !runtime.owner);
    assert(!frame.player_ready);
    free(model.palette);
    puts("Shared interaction ownership, real readiness boundary, status freeze, palette order and "
         "release faults: PASS");
    return 0;
}
