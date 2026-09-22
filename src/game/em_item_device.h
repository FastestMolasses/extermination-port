/* Original185420/184D20 battery-item device lookup, in published order. */
#ifndef EM_ITEM_DEVICE_H
#define EM_ITEM_DEVICE_H

#include "game/em_interaction_scan.h"
#include <stddef.h>

typedef struct {
    void *owner;
    uint8_t status, class_flags, subtype, shape, armed;
    float position[3], yaw;
    float parameters[6]; /* original actor+30 descriptor, shape-dependent */
} EmItemDevice;

/* Returns1 with the original first eligible owner,0 when the entire
 * published list rejects,-1 for invalid input. No owner is armed, no
 * nearest-distance arbitration is performed, and status does not move the
 * player. Only the verified battery item IDs1B..1D are accepted. Authored
 * yaw magnitudes must be<=16 radians; invalid huge angles cannot hang the
 * original repeated-subtraction wrap. Coordinates/descriptors are finite. */
int em_item_device_find(const EmItemDevice *devices, size_t count, unsigned item_id,
                        const float player[3], float player_yaw, const EmInteractionMath *math,
                        void **owner);

#endif
