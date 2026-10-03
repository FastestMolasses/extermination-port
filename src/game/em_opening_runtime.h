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
 *   - the data of the scene-0x22 camera timeline, loaded when New Game's
 *     scene is ready: bank 0x98's clip 0 (opening_camera.emcc,
 *     tools/export_opening_camera.py) and the event table D_0026AE00 that
 *     0022EC30 binds to the camera's +0x80 (opening.emfx,
 *     tools/export_opening_media.py). The timeline itself is the original
 *     0022EC30 / 0022EEF0 (em_cinematic_playback) on the AREA11 script
 *     host, like Roger's encounter (docs/OPENING_ORIGINAL.md). */
#ifndef EM_OPENING_RUNTIME_H
#define EM_OPENING_RUNTIME_H

#include <stdint.h>

#include "game/em_cinematic_playback.h"

/* New Game: the opening will run (the controller starts it). */
void em_opening_runtime_request(void);
/* Called after New Game's scene and ordinary player resources load: the
 * camera track and the event table. */
void em_opening_runtime_scene_ready(void);
/* The controller 00823E80's state 1 ran its completion (+0x05 = 2): the
 * opening is no longer busy. */
void em_opening_runtime_complete(void);
/* The scene-0x22 timeline's data (bank 0x98's clip 0, the window of
 * D_0026AE00), or NULL before em_opening_runtime_scene_ready loaded them. */
const EmCinematicCamera *em_opening_runtime_track(void);
const EmCinematicEvents *em_opening_runtime_events(void);
int em_opening_runtime_busy(void);
int em_opening_runtime_failed(void);
void em_opening_runtime_shutdown(void);

#endif
