/* The AREA11 opening's lane: what the port keeps around the original
 * controller 00823E80 (em_area11_opening, bound in em_area11_bindings.c
 * tick_opening), whose script 0x828FC0 runs on the AREA11 script host
 * (em_area11_script_host: 001BA1A0 / 001BA1F0 with the ftab_0024D880
 * handlers of em_area_script) and whose actors are records of the pool (the
 * player's on its stage, the script's two 001BB0E0 records on
 * em_area11_roger).
 *
 * What stays here:
 *   - the New Game request and the opening's busy state (the controller's
 *     +0x05 from its start to its completion), which the port's own glue
 *     reads (the player-busy and menu-lock gates, the first-control tests);
 *   - the camera timeline of scene 0x22 (the opening's bank-0x98 camera
 *     track with its fade track): 001B8FC0 kind 6's 0022EC30 on that track
 *     starts it, and the camera stage samples it at the camera's +0x74
 *     cursor (g.cam.cine_time). This is the stand-in for the opening's
 *     0022EEF0 timeline, census L33. */
#ifndef EM_OPENING_RUNTIME_H
#define EM_OPENING_RUNTIME_H

#include <stdint.h>

/* New Game: the opening will run (the controller starts it). */
void em_opening_runtime_request(void);
/* Called after New Game's scene and ordinary player resources load: the
 * camera track and the fade track. */
void em_opening_runtime_scene_ready(void);
/* The controller 00823E80's state 1 ran its completion (+0x05 = 2): the
 * opening is no longer busy. */
void em_opening_runtime_complete(void);
/* 0022EC30 for the opening's track: `track` the camera's +0x70 (bank
 * 0x98's clip 0) and `head` its +0x78. 1 when the track is the opening's
 * (the timeline starts at the camera's +0x74), 0 when it is not, -1 when
 * the opening's track is not loaded or its head differs. */
int em_opening_runtime_camera_start(uint32_t track, uint32_t expected, float head);
/* The opening's camera track at the camera stage: while its timeline runs
 * it samples the track at the camera's +0x74 into the g.cam view (the eye,
 * target, up and zoom) and returns 1; 0 when it does not own the camera,
 * -1 when the opening failed. The camera frame commits (em_camera_live.c). */
int em_opening_runtime_camera_sample(void);
int em_opening_runtime_busy(void);
int em_opening_runtime_failed(void);
void em_opening_runtime_shutdown(void);

#endif
