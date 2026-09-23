/* Original event-free camera scene1 (Roger), 001B8FC0/sub6 and0022EEF0.
 * Other scene IDs require their actual timeline workers before admission. */
#ifndef EM_CINEMATIC_PLAYBACK_H
#define EM_CINEMATIC_PLAYBACK_H

#include "game/em_cinematic_camera.h"

typedef struct { float tangent[13]; } EmCinematicProjection;
int em_cinematic_projection_load(EmCinematicProjection *, const char *path);
/* Exact finite scalar tangent-kernel path used by the clamped camera FOV. */
float em_cinematic_projection_zoom(const EmCinematicProjection *, float fov);

typedef struct {
    const EmCinematicCamera *track; /* borrowed until the script releases it */
    float time;
    float eye[4], target[4], up[4], zoom;
    uint32_t cut_counter; /* original D00275BFC, set32 on a cut */
    uint8_t auxiliary;    /* original D008106F3 */
} EmCinematicPlayback;

typedef enum {
    EM_CINEMATIC_CAMERA_PUBLISH, /* DD980, after xyz writes, before up/zoom */
    EM_CINEMATIC_CAMERA_RESTORE_ROOM, /* B0250 */
    EM_CINEMATIC_CAMERA_EFFECT_OFF,   /*21B9A0(0,0,0) */
    EM_CINEMATIC_CAMERA_FLAG_OFF      /*D2830(2,0) */
} EmCinematicPlaybackEvent;
typedef int (*EmCinematicPlaybackEmit)(void *, EmCinematicPlaybackEvent,
                                      const EmCinematicPlayback *);

/* Start changes only the track/time, mirroring the scene1 no-event setup.
 * The caller separately publishes camera_top3 and the script's scene ID. */
int em_cinematic_playback_start(EmCinematicPlayback *, const EmCinematicCamera *, int scene_id);
/* 1 sampled,0 at end,-1 failed worker/input. At the endpoint each invocation
 * repeats the original restore services until the script releases ownership. */
int em_cinematic_playback_tick(EmCinematicPlayback *, const EmCinematicProjection *,
                               EmCinematicPlaybackEmit, void *);
/* Original001B7B30/sub0, called at the script stage. 0 waiting,1 done,-1
 * failed service. Completion restores presentation again without changing
 * the camera cursor or relinquishing camera_top3. */
int em_cinematic_playback_wait(EmCinematicPlayback *, EmCinematicPlaybackEmit, void *);

#endif
