#include "game/em_item_device.h"
#include "game/em_effect_color.h"
#include "game/em_item_sdk_math.h"

#include <math.h>

static float add(float a, float b)
{
    return em_effect_float32((double)a + b);
}

static float subtract(float a, float b)
{
    return em_effect_float32((double)a - b);
}

static float multiply(float a, float b)
{
    return em_effect_float32((double)a * b);
}

static float planar_distance(float x, float z)
{
    return em_item_sdk_sqrt(add(multiply(x, x), multiply(z, z)));
}

static float wrap(float value)
{
    const float pi = 3.1415927410125732f, two_pi = 6.2831854820251465f;
    while (value > pi)
        value = subtract(value, two_pi);
    while (value <= -pi)
        value = add(value, two_pi);
    return value;
}

static int eligible(const EmItemDevice *device, const float player[3], float yaw,
                    const EmInteractionMath *math)
{
    unsigned type = device->class_flags & 0x1F;
    if (type != 4 && type != 6)
        return 0;
    switch (device->subtype) {
    case 0x14:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x2C:
        break;
    default:
        return 0;
    }
    const float *parameters = device->parameters;
    float x, y, z, angle;
    const float pi = 3.1415927410125732f;
    switch (device->shape) {
    case 1:
        x = subtract(player[0], parameters[0]);
        z = subtract(player[2], parameters[2]);
        if (!(planar_distance(x, z) <= parameters[3]))
            return 0;
        y = subtract(player[1], parameters[1]);
        if (!(em_item_sdk_sqrt(multiply(y, y)) <= parameters[4]))
            return 0;
        angle = wrap(subtract(add(pi, yaw), parameters[5]));
        break;
    case 2:
        x = subtract(parameters[0], player[0]);
        z = subtract(parameters[2], player[2]);
        if (!(planar_distance(x, z) <= parameters[3]))
            return 0;
        y = subtract(player[1], device->position[1]);
        if (!(em_item_sdk_sqrt(multiply(y, y)) <= parameters[4]))
            return 0;
        angle = 0;
        break;
    case 3:
    case 4: {
        x = subtract(device->position[0], player[0]);
        z = subtract(device->position[2], player[2]);
        float distance = planar_distance(x, z);
        if (!(distance <= parameters[0]))
            return 0;
        y = subtract(player[1], device->position[1]);
        if (y >= 0) {
            if (!(y <= parameters[1]))
                return 0;
        } else if (!(fabsf(y) <= add(17, parameters[1]))) {
            return 0;
        }
        if (device->shape == 4)
            angle = wrap(subtract(yaw, device->yaw));
        else if (distance <= 7)
            angle = 0;
        else
            angle = wrap(subtract(yaw, em_interaction_sdk_atan2(math, x, z)));
        return fabsf(angle) <= 1.5707963705062866f;
    }
    case 5:
        x = subtract(device->position[0], player[0]);
        z = subtract(device->position[2], player[2]);
        if (!(add(multiply(x, x), multiply(z, z)) <= 196))
            return 0;
        return !(fabsf(subtract(device->position[1], player[1])) > 4);
    default:
        x = subtract(player[0], device->position[0]);
        z = subtract(player[2], device->position[2]);
        if (!(planar_distance(x, z) <= parameters[0]))
            return 0;
        y = subtract(player[1], device->position[1]);
        if (!(em_item_sdk_sqrt(multiply(y, y)) <= parameters[1]))
            return 0;
        angle = wrap(subtract(add(pi, yaw), device->yaw));
        break;
    }
    return fabsf(angle) <= 0.7853981852531433f;
}

int em_item_device_find(const EmItemDevice *devices, size_t count, unsigned item_id,
                        const float player[3], float player_yaw, const EmInteractionMath *math,
                        void **owner)
{
    if ((!devices && count) || count > EM_INTERACTION_CAPACITY || item_id < 0x1B ||
        item_id > 0x1D || !player || !math || !owner || !isfinite(player_yaw) ||
        fabsf(player_yaw) > 16)
        return -1;
    *owner = NULL;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!isfinite(player[axis]))
            return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        const EmItemDevice *device = &devices[i];
        if (!(device->status & 1) || !(device->class_flags & 0x80) || device->armed)
            continue;
        if (!device->owner || !isfinite(device->yaw) || fabsf(device->yaw) > 16 ||
            (device->shape == 1 && fabsf(device->parameters[5]) > 16))
            return -1;
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (!isfinite(device->position[axis]))
                return -1;
        }
        for (unsigned parameter = 0; parameter < 6; ++parameter) {
            if (!isfinite(device->parameters[parameter]))
                return -1;
        }
        if (eligible(device, player, player_yaw, math)) {
            *owner = device->owner;
            return 1;
        }
    }
    return 0;
}
