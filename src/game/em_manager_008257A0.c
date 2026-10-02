/* AREA11 record 13, overlay manager 0x008257A0 (S12a). See the header. */
#include "game/em_manager_008257A0.h"

#include <stddef.h>

#define CALL(address, expr) \
    do { \
        if ((expr) < 0) { \
            if (fault_address) *fault_address = (address); \
            return -1; \
        } \
    } while (0)
#define NEED(worker, address) \
    do { \
        if (!w || !w->worker) { \
            if (fault_address) *fault_address = (address); \
            return -1; \
        } \
    } while (0)

int em_manager_008257A0_tick(EmManager8257A0 *m, const EmManager8257A0Workers *w, uint32_t *fault_address)
{
    if (fault_address) *fault_address = 0;
    if (!m) {
        if (fault_address) *fault_address = EM_MANAGER_008257A0;
        return -1;
    }
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
        NEED(w_001B17A0, 0x001B17A0u);
        switch (m->b05) {
        case 0: {
            int32_t inside = 0;
            NEED(w_001B1EA0, 0x001B1EA0u);
            CALL(0x001B1EA0u, w->w_001B1EA0(w->ctx, &inside));
            if (inside != 0) {
                NEED(w_001BA1A0, 0x001BA1A0u);
                CALL(0x001BA1A0u, w->w_001BA1A0(w->ctx, EM_MANAGER_008257A0_SCRIPT));
                m->b05 = 1;
            }
            break;
        }
        case 1: {
            int32_t result = 0;
            NEED(w_001BA1F0, 0x001BA1F0u);
            CALL(0x001BA1F0u, w->w_001BA1F0(w->ctx, &result));
            if (result != 0) {
                NEED(w_001DFE40, 0x001DFE40u);
                NEED(s_00810814, 0x00810814u);
                CALL(0x001DFE40u, w->w_001DFE40(w->ctx));
                m->b04 = 3;
                CALL(0x00810814u, w->s_00810814(w->ctx, 1));
            }
            break;
        }
        default:
            break;
        }
        CALL(0x001B17A0u, w->w_001B17A0(w->ctx));
        return 0;
    case 2:
    case 3:
        NEED(w_001AFC10, 0x001AFC10u);
        CALL(0x001AFC10u, w->w_001AFC10(w->ctx));
        return 0;
    default:
        return 0;
    }
}
