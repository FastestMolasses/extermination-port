#include "game/em_cinematic_playback.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int reject = -1;
static unsigned calls[4];
static int event(void *context, EmCinematicPlaybackEvent kind, const EmCinematicPlayback *state)
{
    assert(context == state);
    assert(kind >= 0 && kind < 4);
    ++calls[kind];
    return (int)kind != reject;
}

int main(void)
{
    EmCinematicCamera camera = {0};
    EmCinematicProjection projection = {0};
    assert(!em_cinematic_camera_load(&camera, "assets/scene_snow/roger/encounter_camera.emcc"));
    assert(em_cinematic_projection_load(&projection, "assets/scene_snow/roger/camera_projection.emcp"));
    EmCinematicProjection saved = projection;
    for (unsigned i = 0; i < 6; ++i) {
        char path[128];
        snprintf(path, sizeof path, "build/cinematic_playback_reference/bad_%u.emcp", i);
        assert(!em_cinematic_projection_load(&projection, path));
        assert(!memcmp(&projection, &saved, sizeof saved));
    }
    EmCinematicPlayback playback = {0};
    playback.eye[3] = 7;
    playback.target[3] = 9;
    assert(!em_cinematic_playback_start(&playback, &camera, 32));
    assert(!playback.track);
    assert(em_cinematic_playback_start(&playback, &camera, 1));
    for (unsigned i = 0; i < 1382; ++i) {
        assert(em_cinematic_playback_tick(&playback, &projection, event, &playback) == 1);
        assert(playback.time == (i+1)*.5f && isfinite(playback.zoom));
        assert(playback.eye[3] == 7 && playback.target[3] == 9);
    }
    for (unsigned i = 0; i < 3; ++i)
        assert(!em_cinematic_playback_tick(&playback, &projection, event, &playback));
    assert(playback.time == 691 && playback.zoom == 480);
    assert(calls[0] == 1382 && calls[1] == 3 && calls[2] == 3 && calls[3] == 3);
    for (reject = 0; reject < 4; ++reject) {
        memset(calls, 0, sizeof calls);
        assert(em_cinematic_playback_start(&playback, &camera, 1));
        playback.time = reject ? 691 : 0;
        playback.zoom = 123;
        playback.up[3] = 7;
        assert(em_cinematic_playback_tick(&playback, &projection, event, &playback) == -1);
        assert(playback.zoom == 123 && playback.up[3] == 7);
        assert(calls[reject] == 1);
        for (int next = reject+1; next < 4; ++next) assert(!calls[next]);
    }
    playback.time = NAN;
    assert(em_cinematic_playback_tick(&playback, &projection, event, &playback) == -1);
    assert(isnan(em_cinematic_projection_zoom(&projection, NAN)));
    em_cinematic_camera_free(&camera);
    puts("Cinematic playback actual resources, endpoints and failure boundaries PASS");
    return 0;
}
