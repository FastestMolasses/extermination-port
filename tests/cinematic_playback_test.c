/* em_cinematic_playback over the real exported resources (Roger's track,
 * the opening's track and its D_0026AE00 table): the endpoints, the event
 * order, the refusals and every failure boundary, under ASan / UBSan. Run by
 * tools/test_cinematic_playback_reference.py after it writes the malformed
 * projection and table files. */
#include "game/em_cinematic_playback.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { KINDS = 11 };
static int reject = -1;
static unsigned calls[KINDS];
static int event(void *context, EmCinematicPlaybackEvent kind, const EmCinematicPlayback *state)
{
    assert(context == state);
    assert((int)kind >= 0 && (int)kind < KINDS);
    ++calls[kind];
    return (int)kind != reject;
}

static void roger_playback(const EmCinematicCamera *camera, const EmCinematicProjection *projection)
{
    EmCinematicPlayback playback = {0};
    playback.eye[3] = 7;
    playback.target[3] = 9;
    assert(!em_cinematic_playback_start(&playback, camera, NULL, 32, 0));
    assert(!playback.track);
    assert(!em_cinematic_playback_start(&playback, camera, NULL, 0x22, 0)); /* its table is not in the window */
    assert(!playback.track);
    assert(em_cinematic_playback_start(&playback, camera, NULL, 1, 77));
    assert(playback.start_clock == 77 && !playback.cursor[0] && !playback.cursor[1] && !playback.cursor[2]);
    for (unsigned i = 0; i < 1382; ++i) {
        assert(em_cinematic_playback_tick(&playback, projection, event, &playback) == 1);
        assert(playback.time == (i+1)*.5f && isfinite(playback.zoom));
        assert(playback.eye[3] == 7 && playback.target[3] == 9);
    }
    for (unsigned i = 0; i < 3; ++i)
        assert(!em_cinematic_playback_tick(&playback, projection, event, &playback));
    assert(playback.time == 691 && playback.zoom == 480);
    assert(calls[0] == 1382 && calls[1] == 3 && calls[2] == 3 && calls[3] == 3);
    for (unsigned k = 4; k < KINDS; ++k) assert(!calls[k]);
    for (reject = 0; reject < 4; ++reject) {
        memset(calls, 0, sizeof calls);
        assert(em_cinematic_playback_start(&playback, camera, NULL, 1, 0));
        playback.time = reject ? 691 : 0;
        playback.zoom = 123;
        playback.up[3] = 7;
        assert(em_cinematic_playback_tick(&playback, projection, event, &playback) == -1);
        assert(playback.zoom == 123 && playback.up[3] == 7);
        assert(calls[reject] == 1);
        for (int next = reject+1; next < 4; ++next) assert(!calls[next]);
    }
    reject = -1;
    playback.time = NAN;
    assert(em_cinematic_playback_tick(&playback, projection, event, &playback) == -1);
    /* A scene the start refuses is refused by the tick too. */
    assert(em_cinematic_playback_start(&playback, camera, NULL, 1, 0));
    playback.time = 0;
    playback.scene = 22;
    memset(calls, 0, sizeof calls);
    assert(em_cinematic_playback_tick(&playback, projection, event, &playback) == -1);
    for (unsigned k = 0; k < KINDS; ++k) assert(!calls[k]);
}

static void opening_playback(const EmCinematicCamera *camera, const EmCinematicProjection *projection,
                             const EmCinematicEvents *events)
{
    EmCinematicPlayback playback = {0};
    memset(calls, 0, sizeof calls);
    assert(em_cinematic_playback_start(&playback, camera, events, 0x22, 5));
    assert(playback.cursor[1] == EM_CINEMATIC_OPENING_EVENTS && !playback.state[1]);
    unsigned ticks = 0;
    int result;
    while ((result = em_cinematic_playback_tick(&playback, projection, event, &playback)) == 1) ++ticks;
    assert(!result && ticks == (unsigned)(2 * camera->duration));
    /* The -1 record at 0, the cue at 1.0, the fade-out at 634, then the
     * end's restores once. */
    assert(calls[4] == 1 && calls[5] == 1 && !calls[6] && calls[1] == 1);
    assert(!playback.cursor[1] && !playback.state[1]);
    /* Each emit refusal of the opening's calls fails the tick. */
    for (reject = 4; reject <= 5; ++reject) {
        assert(em_cinematic_playback_start(&playback, camera, events, 0x22, 5));
        playback.time = reject == 4 ? 1.0f : 634.0f;
        if (reject == 5) playback.cursor[1] = EM_CINEMATIC_OPENING_EVENTS + 4, playback.state[1] = 1;
        assert(em_cinematic_playback_tick(&playback, projection, event, &playback) == -1);
    }
    reject = -1;
    /* A cursor outside the window faults. */
    assert(em_cinematic_playback_start(&playback, camera, events, 0x22, 5));
    playback.cursor[1] = EM_CINEMATIC_OPENING_EVENTS + 4 * events->count;
    assert(em_cinematic_playback_tick(&playback, projection, event, &playback) == -1);
    playback.cursor[1] = EM_CINEMATIC_OPENING_EVENTS + 2;
    assert(em_cinematic_playback_tick(&playback, projection, event, &playback) == -1);
    playback.cursor[1] = 0;
    playback.cursor[0] = EM_CINEMATIC_OPENING_EVENTS - 4;
    assert(em_cinematic_playback_tick(&playback, projection, event, &playback) == -1);
}

int main(void)
{
    EmCinematicCamera camera = {0}, opening = {0};
    EmCinematicProjection projection = {0};
    assert(!em_cinematic_camera_load(&camera, "assets/scene_snow/roger/encounter_camera.emcc"));
    assert(!em_cinematic_camera_load(&opening, "assets/scene_snow/opening_camera.emcc"));
    assert(em_cinematic_projection_load(&projection, "assets/scene_snow/roger/camera_projection.emcp"));
    EmCinematicProjection saved = projection;
    for (unsigned i = 0; i < 6; ++i) {
        char path[128];
        snprintf(path, sizeof path, "build/cinematic_playback_reference/bad_%u.emcp", i);
        assert(!em_cinematic_projection_load(&projection, path));
        assert(!memcmp(&projection, &saved, sizeof saved));
    }
    float words[64], small[3];
    EmCinematicEvents events = {0};
    assert(!em_cinematic_events_load(&events, words, 64, EM_CINEMATIC_OPENING_EVENTS,
                                     "assets/scene_snow/opening.emfx"));
    EmCinematicEvents kept = events;
    for (unsigned i = 0; i < 8; ++i) {
        char path[128];
        snprintf(path, sizeof path, "build/cinematic_playback_reference/bad_%u.emfx", i);
        assert(em_cinematic_events_load(&events, words, 64, EM_CINEMATIC_OPENING_EVENTS, path) == -1);
        assert(!memcmp(&events, &kept, sizeof kept));
    }
    assert(em_cinematic_events_load(&events, small, 3, EM_CINEMATIC_OPENING_EVENTS,
                                    "assets/scene_snow/opening.emfx") == -1);
    assert(em_cinematic_events_load(&events, words, 64, EM_CINEMATIC_OPENING_EVENTS + 2,
                                    "assets/scene_snow/opening.emfx") == -1);
    assert(!memcmp(&events, &kept, sizeof kept));
    roger_playback(&camera, &projection);
    opening_playback(&opening, &projection, &events);
    assert(isnan(em_cinematic_projection_zoom(&projection, NAN)));
    em_cinematic_camera_free(&camera);
    em_cinematic_camera_free(&opening);
    puts("Cinematic playback actual resources, endpoints, tables and failure boundaries PASS");
    return 0;
}
