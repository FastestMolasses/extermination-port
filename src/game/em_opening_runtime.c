#include "game/em_opening_runtime.h"
#include "game/em_render_context_live.h"
#include "game/em_ee_float.h"

#include "em_gamepad.h"
#include "game/em_camera.h"
#include "game/em_cinematic_camera.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_opening_media.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The opening's lane (see the header). The script, its handlers and its
 * actors are the original owners'; this holds the New Game request, the
 * busy state and the scene-0x22 camera timeline stand-in (census L33). */
static struct {
    EmCinematicCamera camera;
    int requested, ready, finished, failed;
    int camera_started, camera_active;
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
    if (s.ready) em_opening_media_stop();
    s.requested = 1;
    s.finished = 0;
    s.failed = 0;
    s.camera_started = s.camera_active = 0;
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
    if (em_opening_media_prepare(g.scene_dir) != 0) {
        fail("original opening fade track");
        return;
    }
    s.ready = 1;
    fprintf(stderr, "opening: original AREA11 controller 0x00823E80 ready\n");
}

void em_opening_runtime_complete(void)
{
    if (!s.requested || s.finished) return;
    s.finished = 1;
    s.camera_started = s.camera_active = 0;
    em_opening_media_stop();
    fprintf(stderr, "opening: complete frame=%d pos=(%.6f,%.6f,%.6f) yaw=%.8f eventB9=%u\n", g.frame_no,
            g.pos[0], g.pos[1], g.pos[2], g.yaw, g.opening_complete);
}

/* 001D25F0(480.0) on the render context and the up vector (0, -1, 0): the
 * end of the track (a fault latches there and stops the next frame head). */
static void camera_restore(void)
{
    s.camera_active = 0;
    (void)em_rcl_001D25F0(UINT32_C(0x43F00000));
    g.cam.up[0] = 0;
    g.cam.up[1] = -1;
    g.cam.up[2] = 0;
}

int em_opening_runtime_camera_start(uint32_t track, uint32_t expected, float head)
{
    if (track != expected) return 0;
    if (!s.ready) {
        fprintf(stderr, "opening: 0022EC30 on the opening's track before its camera track loaded\n");
        return -1;
    }
    if (head != s.camera.duration) {
        fprintf(stderr, "opening: 0022EC30: the track head %.9g is not the exported track's duration %.9g\n",
                head, s.camera.duration);
        return -1;
    }
    s.camera_started = s.camera_active = 1;
    /* The fade track runs on the timeline's cursor. */
    em_opening_media_restart();
    return 1;
}

int em_opening_runtime_camera_sample(void)
{
    if (!s.camera_started || s.failed || s.finished) return 0;
    if (s.camera_active) {
        EmCinematicCameraFrame frame;
        float time = g.cam.cine_time;   /* the camera's +0x74 */
        em_opening_media_camera_tick(time);
        if (time == 1.0f) em_gamepad_rumble_effect(6, 0);
        int result = em_cinematic_camera_sample(&s.camera, time, &frame);
        if (result < 0) {
            fail("camera sample");
            return -1;
        }
        if (g.capture_path && g.frame_no == g.capture_frame)
            fprintf(stderr, "opening capture: frame=%d camera_sample=%.1f eye=(%.9g,%.9g,%.9g) "
                    "target=(%.9g,%.9g,%.9g) roll=%.9g fov=%.9g cut=%d\n",
                    g.frame_no, time, frame.eye[0], frame.eye[1], frame.eye[2], frame.target[0],
                    frame.target[1], frame.target[2], frame.roll_degrees, frame.fov_degrees, frame.cut);
        if (time < s.camera.duration) {
            memcpy(g.cam.eye, frame.eye, sizeof frame.eye);
            memcpy(g.cam.tgt, frame.target, sizeof frame.target);
            /* 00102B08 rotates identity about X. Original up is (0,-1,0),
             * producing (0,-cos(roll),-sin(roll)), not a guessed Z roll. */
            float roll = frame.roll_degrees * EM_PI / 180.0f;
            g.cam.up[0] = 0;
            g.cam.up[1] = -cosf(roll);
            g.cam.up[2] = -sinf(roll);
            float fov = fmaxf(0.5f, fminf(45.0f, frame.fov_degrees));
            /* The stand-in's zoom (L33), stored through 001D25F0. */
            if (em_rcl_001D25F0(em_ee_bits(224.0f / tanf((EM_PI * (fov / 1.45f)) / 180.0f))) < 0) {
                fail("001D25F0");
                return -1;
            }
            g.cam.cine_time = time + 0.5f;
        } else {
            camera_restore();
        }
    }
    return 1;
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
    em_opening_media_shutdown();
    em_cinematic_camera_free(&s.camera);
    int failed = s.failed;
    memset(&s, 0, sizeof s);
    s.failed = failed; /* main reports the failure after resources release */
}
