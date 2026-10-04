#include "game/em_door_program.h"

#include <string.h>

static void store(unsigned char *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (unsigned char)(value >> (i*8));
}

int em_door_program_patch(EmScriptImage *image, const EmDoorTransitPlan *plan)
{
    if (!image || !image->bytes || image->base != 0x24dbc0 || image->length != 0x3c0 || !plan ||
        plan->side > 1)
        return 0;
    if (plan->locked) {
        if (plan->locked != 1 || plan->script_entry != 0x24dec0 ||
            plan->player_clip != (plan->side ? 0x44 : 0x46) ||
            plan->door_clip != (plan->side ? 1 : 3)) return 0;
        store(image->bytes + 0x24dcd4 - image->base, plan->player_clip);
        store(image->bytes + 0x24dd14 - image->base, plan->door_clip);
        return 1;
    }
    if (plan->script_entry != 0x24de40 ||
        plan->player_clip != (plan->side ? 0x43 : 0x45) ||
        plan->door_clip != (plan->side ? 0 : 2) ||
        plan->wait_ticks != (plan->side ? 70 : 90))
        return 0;
    uint32_t wait;
    memcpy(&wait, &plan->wait_ticks, 4);
    store(image->bytes + 0x24dc14 - image->base, plan->player_clip);
    store(image->bytes + 0x24dc54 - image->base, plan->door_clip);
    store(image->bytes + 0x24dc58 - image->base, plan->sound);
    store(image->bytes + 0x24dc8c - image->base, wait);
    return 1;
}
