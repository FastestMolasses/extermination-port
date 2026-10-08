/* The scripted camera timeline: 0022EC30 (its start), 0022EEF0 (one frame
 * of it) and 001B7B30 sub 0 (the script's wait for its end).
 *
 * Translated from the original instructions (the decomp's NEARMISS C
 * src/func_0022EC30.c and src/func_0022EEF0.c follows them);
 * tools/test_cinematic_playback_reference.py executes the original
 * instructions and compares every written word and every call in order.
 * The playback record is the camera block's timeline words: +0x6E the scene,
 * +0x70 the track, +0x74 the cursor, +0x78 the head, +0x7C / +0x80 / +0x84
 * the three event cursors (original addresses into the ELF's event tables,
 * 0 when done) and +0x88 / +0x89 / +0x8A their state bytes; D_00275C98 the
 * start clock.
 *
 * Admitted scenes: 0x22 (the AREA11 opening, bank 0x98's clip 0: the +0x80
 * table D_0026AE00 and the cue 001B1E20(6, 0) at the cursor 1.0) and the
 * scenes whose start binds no table and which have no cue (0, 1, 3, 4, 9,
 * 11, 14, 18, 36; the first level starts scene 1, Roger's encounter on bank
 * 0x96's clip 0; docs/CAMERA_LIVE.md section 5). Every other scene binds
 * other tables and runs other per-scene cues (0022EEF0's switch); start
 * refuses them, and so does a tick on one (fail-stop). The three tracks'
 * code is translated whole: the tests drive the +0x7C and +0x84 paths and
 * the +0x80 records the opening's table does not hold over tables of their
 * own. AREA01's scenes 2 and 35 bind their own event tracks
 * (em_area01_timeline.c) and enter the shared post-sample core
 * em_cinematic_playback_sampled_tick. */
#ifndef EM_CINEMATIC_PLAYBACK_H
#define EM_CINEMATIC_PLAYBACK_H

#include "game/em_cinematic_camera.h"

#include <stddef.h>

typedef struct { float tangent[13]; } EmCinematicProjection;
int em_cinematic_projection_load(EmCinematicProjection *, const char *path);
/* Exact finite scalar tangent-kernel path used by the clamped camera FOV. */
float em_cinematic_projection_zoom(const EmCinematicProjection *, float fov);

/* A window of the ELF's 0022EC30 event tables: the float words from the
 * original address `base`. The cursors read through it; a cursor outside
 * it is a fault. */
typedef struct {
    uint32_t base;
    const float *words;
    uint32_t count;
} EmCinematicEvents;
/* One exported event track (EMFX v1: its words up to and including the 0.0
 * terminator, tools/export_opening_media.py) as the window at `base`, its
 * words copied into `storage` (`capacity` floats). 0, or -1 on a missing or
 * malformed file (`events` unchanged). */
int em_cinematic_events_load(EmCinematicEvents *events, float *storage, size_t capacity,
                             uint32_t base, const char *path);

enum {
    EM_CINEMATIC_SCENE_ROGER = 1,       /* bank 0x96's clip 0 */
    EM_CINEMATIC_SCENE_OPENING = 0x22   /* bank 0x98's clip 0 */
};
/* 0022EC30's scene-0x22 table for +0x80: D_0026AE00. */
#define EM_CINEMATIC_OPENING_EVENTS 0x0026AE00u

typedef struct {
    const EmCinematicCamera *track;    /* borrowed until the script releases it */
    const EmCinematicEvents *events;   /* borrowed; NULL when no cursor is set */
    int16_t scene;                     /* +0x6E */
    float time;                        /* +0x74 */
    uint32_t cursor[3];                /* +0x7C, +0x80, +0x84 */
    uint8_t state[3];                  /* +0x88, +0x89, +0x8A */
    uint64_t start_clock;              /* D_00275C98 (0021BAB0 at the start) */
    float eye[4], target[4], up[4], zoom;
    uint32_t cut_counter; /* original D00275BFC, set32 on a cut */
    uint8_t auxiliary;    /* original D008106F3 */
    /* The arguments of the call an event names, valid during its emit. */
    int32_t arg[2];
    float value;
} EmCinematicPlayback;

typedef enum {
    EM_CINEMATIC_CAMERA_PUBLISH, /* DD980, after xyz writes, before up/zoom */
    EM_CINEMATIC_CAMERA_RESTORE_ROOM, /* B0250 */
    EM_CINEMATIC_CAMERA_EFFECT_OFF,   /*21B9A0(0,0,0) */
    EM_CINEMATIC_CAMERA_FLAG_OFF,     /*D2830(2,0) */
    EM_CINEMATIC_CAMERA_RUMBLE,       /* 001B1E20(arg[0], arg[1]): the scene cue */
    EM_CINEMATIC_CAMERA_FADE_OUT,     /* 001AEDE0(arg[0], arg[1]): +0x80 */
    EM_CINEMATIC_CAMERA_FADE_IN,      /* 001AEE10(arg[0], arg[1]): +0x80 */
    EM_CINEMATIC_CAMERA_EFFECT,       /* 0021B9A0(5, 0.0, value): +0x7C */
    EM_CINEMATIC_CAMERA_EFFECT_GREY,  /* 0021BA80(0x80, 0x80, 0x80): +0x7C */
    EM_CINEMATIC_CAMERA_EFFECT_CLOCK, /* 0021BA70(start_clock): +0x7C */
    EM_CINEMATIC_CAMERA_FLAG_ON       /* 001D2830(2, 1): +0x84 */
} EmCinematicPlaybackEvent;
typedef int (*EmCinematicPlaybackEmit)(void *, EmCinematicPlaybackEvent,
                                      const EmCinematicPlayback *);

/* 0022EC30 on the playback record: the start clock (0021BAB0's value, read
 * by the caller first, as 0022EC30 does), the three cursors and their
 * states cleared, then the scene's tables bound. The caller has stored
 * +0x6E / +0x70 / +0x74 / +0x78 (001B8FC0 kind 6): `scene_id` is +0x6E and
 * `track` the track +0x70 names; the cursor `time` is not touched. 1
 * started; 0 refused (an unadmitted scene, a malformed track, or the
 * scene's table outside `events`), the playback unchanged. */
int em_cinematic_playback_start(EmCinematicPlayback *, const EmCinematicCamera *track,
                                const EmCinematicEvents *events, int scene_id,
                                uint64_t start_clock);
/* 0022EEF0's part before the sampler: the scene's cue and the three event
 * tracks, in the original order. 0, or -1 on an unadmitted scene or a
 * failed worker. */
int em_cinematic_playback_events(EmCinematicPlayback *, EmCinematicPlaybackEmit, void *);
/* 0022EEF0: em_cinematic_playback_events, the sampler, then
 * em_cinematic_playback_sampled_tick. 1 sampled, 0 at the end, -1 failed
 * worker or input. At the endpoint each invocation repeats the original
 * restore services until the script releases ownership. */
int em_cinematic_playback_tick(EmCinematicPlayback *, const EmCinematicProjection *,
                               EmCinematicPlaybackEmit, void *);
/* Shared post-sample core of 0022EEF0. AREA01 publishes the sampler's
 * canonical result and advances its event tracks before entering this core.
 * rotation, when supplied, receives the original scratch matrix on active
 * frames; em_cinematic_playback_tick uses the same body with NULL. */
int em_cinematic_playback_sampled_tick(EmCinematicPlayback *, const EmCinematicProjection *,
    EmCinematicPlaybackEmit, void *, const EmCinematicCameraFrame *, int active, float rotation[16]);
/* Original001B7B30/sub0, called at the script stage. 0 waiting,1 done,-1
 * failed service. Completion restores presentation again without changing
 * the camera cursor or relinquishing camera_top3. */
int em_cinematic_playback_wait(EmCinematicPlayback *, EmCinematicPlaybackEmit, void *);

#endif
