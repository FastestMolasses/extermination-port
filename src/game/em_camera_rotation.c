#include "game/em_camera_rotation.h"
#include "game/em_effect_original.h"
#include "game/em_ee_float.h"
#include "game/em_owner_services_original.h"

#include <math.h>
#include <string.h>

/* 001029C0(m); 00102C58(m, m, angles) (Z, then Y, then X); 001026A0(offset,
 * m, (0, 0, distance, 1)). Each is the one bound translation of that SDK
 * routine (em_owner_services_original, em_effect_original; docs/SDK_VU0.md),
 * on the measured EE model (docs/EE_FLOAT_MODEL.md). */
int em_camera_rotation_offset(const float angles[3], float distance,
    float matrix[16], float offset[4])
{
    if (!angles || !matrix || !offset || !isfinite(distance))
        return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (!isfinite(angles[i]) || fabsf(angles[i]) > 0x1.921fb6p+1f)
            return 0;
    float a[3];
    memcpy(a, angles, sizeof a);
    if (em_owner_services_identity_001029C0(matrix) != EM_EE_FLOAT_OK ||
        em_owner_services_euler_00102C58(matrix, matrix, a) != EM_EE_FLOAT_OK)
        return 0;
    const float v[4] = {0.0f, 0.0f, distance, 1.0f};
    em_effect_original_001026A0(offset, matrix, v);
    return 1;
}
