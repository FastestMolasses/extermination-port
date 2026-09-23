/* AREA11 record 13, overlay manager 0x008257A0 (S12a). See the header. */
#include "game/em_manager_008257A0.h"

#include <stddef.h>

int em_manager_008257A0_tick(EmManager8257A0 *m, const EmManager8257A0Workers *w, uint32_t *fault_address)
{
    switch (m->b04) {
    case 0:
        /* 001BA1C0(self, 0x3C): D_00810758[0x3C] == 0xFF. */
        if (m->d810794 == 0xFF) {
            m->b04 = 3;
        } else if (m->d810788 == 0) {
            m->b04 = 3;
        } else {
            m->b04 = 1;
            m->b00 = 1;
        }
        return 0;
    case 1:
        if (fault_address)
            *fault_address = EM_MANAGER_008257A0;
        return -1;
    case 2:
    case 3:
        if (!w || !w->w_001AFC10 || w->w_001AFC10(w->ctx) < 0) {
            if (fault_address)
                *fault_address = 0x001AFC10u;
            return -1;
        }
        return 0;
    default:
        return 0;
    }
}
