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

int em_area11_opening_state1(EmArea11Opening *self, const EmArea11OpeningWorkers *w, uint32_t *fault_address)
{
    if (!self || !w) {
        if (fault_address) *fault_address = 0x00823E80u;
        return -1;
    }
    int32_t done = 0;
    NEED(w_001BA1C0, 0x001BA1C0u);
    CALL(0x001BA1C0u, w->w_001BA1C0(w->ctx, EM_AREA11_OPENING_EVENT, &done));
    if (done) return 0;                                        /* 0x823F00 */
    switch (self->b05) {
    case 0:                                                    /* 0x823F34 */
        NEED(w_001BA1A0, 0x001BA1A0u);
        NEED(w_001FABB0, 0x001FABB0u);
        CALL(0x001BA1A0u, w->w_001BA1A0(w->ctx, EM_AREA11_OPENING_SCRIPT));
        CALL(0x001FABB0u, w->w_001FABB0(w->ctx));
        self->b05 = 1;
        return 0;
    case 1: {                                                  /* 0x823F5C */
        int32_t result = 0;
        NEED(w_001BA1F0, 0x001BA1F0u);
        CALL(0x001BA1F0u, w->w_001BA1F0(w->ctx, &result));
        if (result == 0) return 0;
        NEED(s_00810811, 0x00810811u);
        NEED(w_001C4760, 0x001C4760u);
        NEED(w_001FAE70, 0x001FAE70u);
        NEED(w_001AEE10, 0x001AEE10u);
        self->h2E = 0xFFFF;                                    /* 0x823F70 */
        CALL(0x00810811u, w->s_00810811(w->ctx, 0xFF));        /* 0x823F80 */
        CALL(0x001C4760u, w->w_001C4760(w->ctx, 0, 1));
        CALL(0x001FAE70u, w->w_001FAE70(w->ctx, 0));
        self->b05 = 2;                                         /* 0x823F9C */
        CALL(0x001AEE10u, w->w_001AEE10(w->ctx, 4, 0));
        return 0;
    }
    default:                                                   /* 2 and others */
        return 0;
    }
}
