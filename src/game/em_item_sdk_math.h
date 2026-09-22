/* Original SDK math used by the ITEM stick and cursor, not the camera SDK. */
#ifndef EM_ITEM_SDK_MATH_H
#define EM_ITEM_SDK_MATH_H

#include "game/em_interaction_scan.h"
#include "game/em_item_trail.h"

typedef struct {
    EmInteractionMath atan;
    /* Numerical SDK wrapper state: neutral atan2 sets EDOM (0x21).
     * No process-global libc errno or original diagnostic hook is installed. */
    int error;
} EmItemSdkMath;

/* The UI supplies finite angles in [-float(4*pi), float(4*pi)] and nonnegative
 * finite square-root inputs. Values outside those domains return NaN.
 * atan coefficients are copied from the original exported EMIS resource. */
int em_item_sdk_math_bind(EmItemSdkMath *state, const EmInteractionMath *atan, EmItemMath *workers);
float em_item_sdk_sine(float angle);
float em_item_sdk_cosine(float angle);
float em_item_sdk_sqrt(float value);

#endif
