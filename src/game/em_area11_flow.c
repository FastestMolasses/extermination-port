#include "game/em_area11_flow.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

static uint32_t little_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

int em_area11_triggers_load(EmArea11Triggers *triggers, const char *path)
{
    unsigned char bytes[108];
    EmArea11Triggers decoded;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    size_t size = fread(bytes, 1, sizeof bytes, file);
    int trailing = fgetc(file);
    fclose(file);
    if (size != sizeof bytes || trailing != EOF ||
        memcmp(bytes, "EMAF", 4) || little_u32(bytes + 4) != 1 ||
        little_u32(bytes + 8) != 3) return 0;
    for (unsigned beat = 0; beat < 3; ++beat)
    for (unsigned vertex = 0; vertex < 4; ++vertex)
    for (unsigned axis = 0; axis < 2; ++axis) {
        size_t offset = 12 + ((beat * 4 + vertex) * 2 + axis) * 4;
        uint32_t bits = little_u32(bytes + offset);
        float coordinate;
        memcpy(&coordinate, &bits, sizeof coordinate);
        if (!isfinite(coordinate)) return 0;
        decoded.polygon[beat][vertex][axis] = coordinate;
    }
    *triggers = decoded;
    return 1;
}

int em_area11_beat_for_step(uint8_t step)
{
    /* 0x008253F0 dispatches both halves of each actor transition. */
    switch (step) {
    case 0x00:
    case 0x01: return 0;
    case 0x10:
    case 0x11: return 1;
    case 0x20: return 2;
    default: return -1;
    }
}

int em_area11_trigger_contains(const EmArea11Triggers *triggers,
                              int beat, const float position[3])
{
    if (beat < 0 || beat >= 3) return 0;
    /* Original compare direction at 0x00825528, 825628 and 8256F8.
     * Preserve the lower-bound comparison spelling, including its NaN
     * behavior; the upper bound exists only for the first event. */
    if (beat == 0) {
        if (position[1] < 260.0f || !(position[1] <= 280.0f)) return 0;
    } else if (beat == 1) {
        if (position[1] < 275.0f) return 0;
    } else {
        if (position[1] < 285.0f) return 0;
    }

    /* func_001B1EA0 plane 0 accumulates signed corner angles in XZ.
     * Keep individual float operations and original vertex order.  The
     * host atan2f is a native math adaptation; exact EE transcendental
     * rounding at polygon boundaries has not been established. */
    float total = 0.0f;
    for (int i = 0; i < 4; ++i) {
        int next = i == 3 ? 0 : i + 1;
        float ax = triggers->polygon[beat][i][0] - position[0];
        float az = triggers->polygon[beat][i][1] - position[2];
        float bx = triggers->polygon[beat][next][0] - position[0];
        float bz = triggers->polygon[beat][next][1] - position[2];
        float cross = bz * ax - bx * az;
        float dot = bx * ax + bz * az;
        total += atan2f(cross, dot);
    }
    const float pi = 3.1415927410125732421875f; /* original 0x40490FDB */
    return total < 0.0f ? total < -pi : !(total <= pi);
}

uint8_t em_area11_step_after_beat(int beat)
{
    static const uint8_t next[] = {0x10, 0x20, 0xFF};
    return beat >= 0 && beat < 3 ? next[beat] : 0xFF;
}
