/* em_menu_hover.c - see em_menu_hover.h. */
#include "game/em_menu_hover.h"

/* The angle -> hover tables, as 0020D930's compare chains (each bound is
 * a strict less-than on the float angle). */
static unsigned table0(float a)
{
    if (a < -0.7853982f)
        return a < -2.3561945f ? 4 : 3;
    if (a < 0.7853982f)
        return 2;
    return a < 2.3561945f ? 1 : 4;
}

static unsigned table1(float a)
{
    if (a < -0.5235988f)
        return a < -2.0071287f ? 4 : 3;
    if (a < 0.5235988f)
        return 2;
    if (a < 2.0071287f)
        return 1;
    return a < 3.1415927f ? 5 : 4;
}

static unsigned table2(float a)
{
    if (a < -1.5707964f)
        return a < -2.670354f ? 1 : 2;
    if (a < -0.41887903f)
        return 3;
    if (a < 0.36651915f)
        return 4;
    if (a < 1.5707964f)
        return 5;
    return a < 2.7576203f ? 6 : 1;
}

int em_menu_hover_0020D930(uint8_t *hover, int32_t mode, float magnitude, float angle)
{
    if ((double)magnitude < 0.8) {
        *hover = 0;
        return 0;
    }
    const unsigned selected = mode == 0 ? table0(angle) : mode == 1 ? table1(angle) : table2(angle);
    const int sound = *hover != selected;
    *hover = (uint8_t)selected;
    return sound;
}
