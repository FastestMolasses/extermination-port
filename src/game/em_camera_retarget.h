/* Scalar portion of original 0018CBD0. Transform construction and collision
 * styles 5/1 are separate operations; this helper never commits a camera. */
#ifndef EM_CAMERA_RETARGET_H
#define EM_CAMERA_RETARGET_H

/* rotated_offset is the output of 001026A0 applied to [0,0,distance,1].
 * preset_distance is camera+64, which need not equal current camera+C.
 * The sqrt helper uses host sqrtf; this is not an SDK transcendental port. */
void em_camera_retarget_seed(const float position[3],
                            const float rotated_offset[3], float distance,
                            float preset_distance, float eye[3], float target[3]);

#endif
