#include "game/em_status_draw.h"
#include "game/em_effect_color.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static float add(float a, float b)
{
    return em_effect_float32((double)a + b);
}

static float multiply(float a, float b)
{
    return em_effect_float32((double)a * b);
}

static int half_floor(int value)
{
    return value >= 0 ? value / 2 : -(int)((-(int64_t)value + 1) / 2);
}

/* Original001C5FB0 emits a fixed count of decimal places. In particular, a
 * value wider than its field is not handled like printf's minimum width. */
static void digits(char *output, int value, unsigned width, int leading_blank)
{
    static const int divisors[] = {1000, 100, 10, 1};
    int started = 0;
    int negative = 0;
    for (unsigned index = 0; index < width; ++index) {
        int divisor = divisors[4 - width + index];
        int digit = value / divisor;
        if (leading_blank && !started && digit == 0 && index != width - 1) {
            *output++ = ' ';
        } else {
            if (digit < 0) {
                if (!negative) {
                    *output++ = '-';
                    negative = 1;
                }
            }
            unsigned magnitude = (unsigned)(digit < 0 ? -digit : digit);
            *output++ = (char)(uint8_t)(magnitude + '0');
            started = 1;
        }
        value -= digit * divisor;
    }
    *output = 0;
}

int em_status_health_draw(uint32_t *counter, float health, uint8_t warning, int x, int y,
                          const EmStatusHealthData *data, const EmStatusDrawWorkers *workers)
{
    if (!counter || !data || !workers || !workers->blend || !workers->rectangle || !workers->text ||
        !workers->arc || !data->label || !data->warning_max || !data->normal_max ||
        !data->separator || data->label_width < 0 || data->label_width > 1024 || x < -4096 ||
        x > 4096 || y < -4096 || y > 4096 || !isfinite(health) || health < 0 || health > 100)
        return 0;
    void *context = workers->context;
    ++*counter;
    int left = x - half_floor(data->label_width);
    if (workers->blend(context, 0) != 1 ||
        workers->text(context, 0, left + 0x700, half_floor(y - 0x4E) + 0x790, 12, 12, data->label,
                      data->white) != 1 ||
        workers->rectangle(context, (left + 0x6F4) * 16, (half_floor(y - 0x4C) + 0x790) * 16,
                           (left + 0x6FC) * 16, (half_floor(y - 0x44) + 0x790) * 16,
                           0x80CE6000) != 1)
        return 0;

    char number[16];
    digits(number, (int)health, 3, 1);
    int number_y = half_floor(y + 0x42) + 0x790;
    uint64_t style = warning || health <= 60.0f ? data->red : data->white;
    if (workers->text(context, 0, x - 0x2A + 0x700, number_y, 12, 12, number, style) != 1 ||
        workers->text(context, 0, x + 0x706, number_y, 12, 12,
                      warning ? data->warning_max : data->normal_max,
                      warning ? data->red : data->white) != 1 ||
        workers->text(context, 0, x + 0x6FA, number_y, 12, 12, data->separator, data->white) != 1)
        return 0;

    float arcs[4][24];
    memcpy(arcs, data->arcs, sizeof arcs);
    float center_x = (float)((x + 0x700) * 16);
    float center_y = (float)((half_floor(y) + 0x790) * 16);
    for (unsigned i = 0; i < 4; ++i) {
        arcs[i][0] = center_x;
        arcs[i][1] = center_y;
    }
    arcs[0][2] = 180;
    arcs[0][3] = 540;
    if (workers->arc(context, arcs[0]) != 1)
        return 0;
    static const float healthy[8] = {192, 224, 0, 128, 224, 128, 24, 128};
    static const float critical[8] = {160, 0, 0, 128, 192, 0, 0, 128};
    const float *colors = health > 35.0f ? healthy : critical;
    memcpy(arcs[1] + 8, colors, sizeof healthy);
    memcpy(arcs[1] + 16, colors, sizeof healthy);
    if (workers->arc(context, arcs[1]) != 1 || workers->blend(context, 1) != 1)
        return 0;

    int phase = half_floor((int32_t)*counter) % 30;
    float angle = add(180.0f, multiply(12.0f, (float)phase));
    arcs[2][2] = add(angle, -60.0f);
    arcs[2][3] = angle;
    arcs[3][2] = angle;
    arcs[3][3] = add(60.0f, angle);
    if (workers->arc(context, arcs[2]) != 1 || workers->arc(context, arcs[3]) != 1 ||
        workers->blend(context, 0) != 1)
        return 0;
    arcs[0][2] = add(-180.0f, multiply(360.0f, health / 100.0f));
    arcs[0][3] = 180.0f;
    return workers->arc(context, arcs[0]) == 1;
}

int em_status_battery_draw(uint16_t charge, uint8_t capacity, uint8_t equipped, int x, int y,
                           uint64_t tex0, int compact, const EmStatusBatteryData *data,
                           const EmStatusDrawWorkers *workers)
{
    if (!data || !workers || !workers->blend || !workers->rectangle || !workers->text ||
        !workers->sprite || !data->label || !data->separator || charge > 255 || x < -4096 ||
        x > 4096 || y < -4096 || y > 4096)
        return 0;
    void *context = workers->context;
    if (workers->blend(context, 0) != 1)
        return 0;
    int width = compact ? 12 : 8;
    if (!compact &&
        (workers->rectangle(context, (x + 0x700) * 16, (half_floor(y + 2) + 0x790) * 16,
                            (x + 0x708) * 16, (half_floor(y + 10) + 0x790) * 16, 0x80CE6000) != 1 ||
         workers->text(context, 0, x + 0x70C, half_floor(y) + 0x790, 12, 12, data->label,
                       data->white) != 1))
        return 0;
    if (!equipped)
        return 1;
    char current[8], maximum[8], caption[80];
    digits(current, charge >> 1, 2, 0);
    digits(maximum, capacity >> 1, 2, 0);
    if (strlen(data->separator) > 60)
        return 0;
    snprintf(caption, sizeof caption, "%s%s%s", current, data->separator, maximum);
    if (workers->text(context, 0, x + (compact ? 0x746 : 0x710),
                      half_floor(y + (compact ? 0x50 : 0x34)) + 0x790, 16, 16, caption,
                      data->white) != 1 ||
        workers->blend(context, 3) != 1)
        return 0;
    int y_base = y + (compact ? 0x16 : 0x10);
    int right = x + width * 11 + (compact ? 0x26 : 0);
    static const float start[4] = {163, 54, 160, 128};
    static const float finish[4] = {255, 230, 52, 128};
    float step[4];
    for (unsigned channel = 0; channel < 4; ++channel)
        step[channel] = multiply(add(finish[channel], -start[channel]), 1.0f / 12.0f);
    for (unsigned row = 0; row <= charge / 12; ++row) {
        unsigned count = row == charge / 12 ? charge % 12 : 12;
        float color[4];
        memcpy(color, start, sizeof color);
        for (unsigned column = 0; column < count; ++column) {
            int left = right - (int)column * width - (column & 1 ? 0 : width >> 3);
            uint32_t rgba = 0;
            for (unsigned channel = 0; channel < 4; ++channel)
                rgba |= (uint32_t)(int)color[channel] << (channel * 8);
            if (workers->sprite(context, (left + 0x700) * 16,
                                (half_floor(y_base + (int)row * width) + 0x790) * 16, width, width,
                                rgba, tex0) != 1)
                return 0;
            for (unsigned channel = 0; channel < 4; ++channel)
                color[channel] = add(color[channel], step[channel]);
        }
    }
    return 1;
}

int em_status_ammo_draw(const EmStatusAmmoInventory *inventory, int x, int y,
                        const EmStatusAmmoData *data, const EmStatusDrawWorkers *workers)
{
    if (!inventory || !data || !workers || !workers->blend || !workers->rectangle ||
        !workers->text || !workers->sprite || !data->label || !data->percent ||
        strlen(data->percent) > 60 || x < -4096 || x > 4096 || y < -4096 || y > 4096 ||
        (inventory->primary != 2 && inventory->secondary > 4))
        return 0;
    void *context = workers->context;
    if (workers->blend(context, 3) != 1 ||
        workers->rectangle(context, (x + 0x700) * 16, (half_floor(y + 2) + 0x790) * 16,
                           (x + 0x708) * 16, (half_floor(y + 10) + 0x790) * 16, 0x80CE6000) != 1 ||
        workers->text(context, 0, x + 0x70C, half_floor(y) + 0x790, 12, 12, data->label,
                      data->white) != 1 ||
        workers->sprite(context, (x + 0x700) * 16, (half_floor(y + 0x48) + 0x790) * 16, 24, 24,
                        0x80808080, UINT64_C(0x2004518555422196)) != 1)
        return 0;
    char number[80];
    digits(number, inventory->reserve, 4, 1);
    if (workers->text(context, 0, x + 0x71A, half_floor(y + 0x4C) + 0x790, 16, 16, number,
                      data->white) != 1)
        return 0;
    int amount;
    int percentage = 0;
    uint64_t tex0;
    if (inventory->primary == 2) {
        amount = inventory->amount[4];
        tex0 = UINT64_C(0x20045385554221C2);
    } else {
        switch (inventory->secondary) {
        case 0:
            return 1;
        case 1:
            amount = inventory->amount[0];
            tex0 = UINT64_C(0x200451A5554221A2);
            break;
        case 2:
        case 3:
            amount = inventory->amount[1];
            tex0 = UINT64_C(0x20045305554221A6);
            break;
        case 4:
            amount = inventory->amount[3] + inventory->amount[2] * 100;
            percentage = 1;
            tex0 = UINT64_C(0x20045325554221B2);
            break;
        default:
            return 0;
        }
    }
    digits(number, amount, percentage ? 4 : 3, 1);
    if (percentage)
        strcat(number, data->percent);
    int text_x = x + (percentage && number[0] == ' ' ? 0x70A : 0x71A);
    if (workers->text(context, 0, text_x, half_floor(y + 0x64) + 0x790, 16, 16, number,
                      data->white) != 1)
        return 0;
    return workers->sprite(context, (x + 0x700) * 16, (half_floor(y + 0x60) + 0x790) * 16, 24, 24,
                           0x80808080, tex0) == 1;
}
