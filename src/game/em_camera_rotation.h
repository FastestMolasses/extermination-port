/* Original SDK Euler rotation used by camera seed0018CBD0. */
#ifndef EM_CAMERA_ROTATION_H
#define EM_CAMERA_ROTATION_H

/* Construct the identity-based Z/Y/X rotation and transform[0,0,distance,1].
 * Input angles must be finite and normalized to[-pi,pi]. Returns0 for inputs
 * outside that supported camera domain; no host sin/cos substitution. */
int em_camera_rotation_offset(const float angles[3], float distance,
    float matrix[16], float offset[4]);

#endif
