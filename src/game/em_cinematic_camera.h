#ifndef EM_CINEMATIC_CAMERA_H
#define EM_CINEMATIC_CAMERA_H

#include <stdint.h>

/* Original 001C7C00 track: eight floats per sample, with one lookahead
 * sample beyond duration. Sample 7 is signed FOV; a negative value marks
 * a camera cut. Roll (sample 6) is held, not interpolated. */
typedef struct {
    float *samples;
    uint32_t sample_count;
    float duration;
} EmCinematicCamera;

typedef struct {
    float eye[3], target[3];
    float roll_degrees, fov_degrees;
    int cut;
} EmCinematicCameraFrame;

/* Initialize camera to zero before its first load. Failed loads preserve it. */
int em_cinematic_camera_load(EmCinematicCamera *camera, const char *path);
void em_cinematic_camera_free(EmCinematicCamera *camera);
/* Returns 1 during playback, 0 at either clamped endpoint, -1 on bad input.
 * Original 0022EEF0 samples before advancing its cursor by 0.5 per tick. */
int em_cinematic_camera_sample(const EmCinematicCamera *camera, float time,
                              EmCinematicCameraFrame *frame);

#endif
