#include "game/em_area11_opening.h"

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
        if (!w->worker) { \
            if (fault_address) *fault_address = (address); \
            return -1; \
        } \
    } while (0)

/* +0x04 == 1: the script machine (everything before the tail). */
static int state1(EmArea11Opening *self, const EmArea11OpeningWorkers *w, uint32_t *fault_address)
{
    int32_t done = 0;
    NEED(w_001BA1C0, 0x001BA1C0u);
    CALL(0x001BA1C0u, w->w_001BA1C0(w->ctx, EM_AREA11_OPENING_EVENT, &done));
    if (done) return 0;
    switch (self->b05) {
    case 0:
        NEED(w_001BA1A0, 0x001BA1A0u);
        NEED(w_001FABB0, 0x001FABB0u);
        CALL(0x001BA1A0u, w->w_001BA1A0(w->ctx, EM_AREA11_OPENING_SCRIPT));
        CALL(0x001FABB0u, w->w_001FABB0(w->ctx));
        self->b05 = 1;
        return 0;
    case 1: {
        int32_t result = 0;
        NEED(w_001BA1F0, 0x001BA1F0u);
        CALL(0x001BA1F0u, w->w_001BA1F0(w->ctx, &result));
        if (result == 0) return 0;
        NEED(s_00810811, 0x00810811u);
        NEED(w_001C4760, 0x001C4760u);
        NEED(w_001FAE70, 0x001FAE70u);
        NEED(w_001AEE10, 0x001AEE10u);
        self->h2E = 0xFFFF;
        CALL(0x00810811u, w->s_00810811(w->ctx, 0xFF));
        CALL(0x001C4760u, w->w_001C4760(w->ctx, 0, 1));
        CALL(0x001FAE70u, w->w_001FAE70(w->ctx, 0));
        self->b05 = 2;
        CALL(0x001AEE10u, w->w_001AEE10(w->ctx, 4, 0));
        return 0;
    }
    default:
        return 0;
    }
}

int em_area11_opening_tick(EmArea11Opening *self, const EmArea11OpeningWorkers *w, uint32_t *fault_address)
{
    if (fault_address) *fault_address = 0;
    if (!self || !w) {
        if (fault_address) *fault_address = EM_AREA11_OPENING_CALLBACK;
        return -1;
    }
    switch (self->b04) {
    case 0: {
        int32_t refused = 0;
        NEED(w_001B0FD0, 0x001B0FD0u);
        CALL(0x001B0FD0u, w->w_001B0FD0(w->ctx, &refused));
        if (refused != 0) return 0;
        NEED(w_001C6380, 0x001C6380u);
        CALL(0x001C6380u, w->w_001C6380(w->ctx));
        self->b04 = 1;
        self->b00 = 1;
        return 0;
    }
    case 1:
        NEED(w_001B1B70, 0x001B1B70u);
        NEED(m_4C, EM_AREA11_OPENING_CALLBACK);
        if (state1(self, w, fault_address) < 0) return -1;
        CALL(0x001B1B70u, w->w_001B1B70(w->ctx));
        CALL(EM_AREA11_OPENING_CALLBACK, w->m_4C(w->ctx));
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
