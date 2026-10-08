#include "game/em_opening_runtime.h"

#include "game/em_frame.h"
#include "game/em_game_internal.h"

#include <stdio.h>
#include <string.h>

/* The opening's lane (see the header). The script, its handlers, its
 * actors and its camera timeline are the original owners'; this holds the
 * New Game request, the busy state and the timeline's data. */
enum { EVENT_WORDS = 64 };

static struct {
    EmCinematicCamera camera;
    EmCinematicEvents events;
    float event_words[EVENT_WORDS];
    int requested, ready, finished, failed;
} s;

static void fail(const char *service)
{
    if (!s.failed)
        fprintf(stderr, "opening: required %s unavailable; New Game cannot continue\n", service);
    s.failed = 1;
    em_frame_request_quit();
}

void em_opening_runtime_request(void)
{
    s.requested = 1;
    s.finished = 0;
    s.failed = 0;
}

void em_opening_runtime_scene_ready(void)
{
    if (!s.requested || s.ready || s.failed) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/opening_camera.emcc", g.scene_dir);
    if (em_cinematic_camera_load(&s.camera, path) != 0) {
        fail("original bank 0x98 camera track");
        return;
    }
    snprintf(path, sizeof path, "%s/opening.emfx", g.scene_dir);
    if (em_cinematic_events_load(&s.events, s.event_words, EVENT_WORDS, EM_CINEMATIC_OPENING_EVENTS, path) != 0) {
        fail("original D_0026AE00 event table (opening.emfx)");
        return;
    }
    s.ready = 1;
    fprintf(stderr, "opening: original AREA11 controller 0x00823E80 ready\n");
}

void em_opening_runtime_complete(void)
{
    if (!s.requested || s.finished) return;
    s.finished = 1;
    fprintf(stderr, "opening: complete frame=%d pos=(%.6f,%.6f,%.6f) yaw=%.8f eventB9=%u\n", g.frame_no,
            g.pos[0], g.pos[1], g.pos[2], g.yaw, g.opening_complete);
}

const EmCinematicCamera *em_opening_runtime_track(void)
{
    return s.ready ? &s.camera : NULL;
}

const EmCinematicEvents *em_opening_runtime_events(void)
{
    return s.ready ? &s.events : NULL;
}

int em_opening_runtime_busy(void)
{
    return s.requested && !s.finished && !s.failed;
}

int em_opening_runtime_failed(void)
{
    return s.failed;
}

void em_opening_runtime_shutdown(void)
{
    em_cinematic_camera_free(&s.camera);
    int failed = s.failed;
    memset(&s, 0, sizeof s);
    s.failed = failed; /* main reports the failure after resources release */
}
