/* Original 00185420 / 00184D20 item device lookup, in published order. */
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

/* Returns 1 with the original first eligible owner, 0 when the entire
 * published list rejects, -1 for invalid input. No owner is armed, no
 * nearest-distance arbitration is performed, and status does not move the
 * player. Every item id is defined, as in 00184D20: class 4 takes the
 * batteries 0x1B..0x1D (subtypes 0x14, 0x22..0x26, 0x2C), 0x20 (0x36, the
 * bed), 0x25 (0x21), 0x26 (0x49) and 0x27 (0x35); class 6 the batteries,
 * 0x23 (0x2F / 0x12) and 0x24 (0x13 / 0x12 / 0x2F); any other pair rejects.
 * Authored yaw magnitudes must be <= 16 radians (invalid huge angles cannot
 * hang the original repeated-subtraction wrap); coordinates and descriptors
 * are finite. */
int em_item_device_find(const EmItemDevice *devices, size_t count, unsigned item_id,
                        const float player[3], float player_yaw, const EmInteractionMath *math,
                        void **owner);

#endif
