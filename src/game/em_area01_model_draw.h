/* Generic record views for the existing draw and shadow owners. */
#ifndef EM_AREA01_MODEL_DRAW_H
#define EM_AREA01_MODEL_DRAW_H
#include "game/em_area01_model_live.h"
#include "game/em_owner_draw_live.h"

typedef struct {
    EmArea01Model *model;
    /* Rebuilt for each call. These are typed views, not another model or
     * bone allocation. Referenced resource bytes remain loader-owned. */
    EmWorldModels bank;
    EmOwnerServicesOwner owner;
    EmOwnerBone bones[EM_OWNER_SERVICES_MAX_BONES];
    const uint8_t *nodes[EM_OWNER_SERVICES_MAX_BONES];
    EmOwnerDrawLiveRegion regions[4];
    uint32_t rgb[4], record;
    unsigned region_count;
    /* Set by the caller before a 001CAF60 call (AREA02's draw method): the
     * word D_00275B44 (the owner being ticked) and the original stack
     * pointer at the call. */
    uint32_t current, sp;
} EmArea01ModelDraw;

void em_area01_model_draw_bind(EmArea01ModelDraw *draw, EmArea01Model *model);
/* Named draw method (001CAA00, 001CACB0, 001CB360, 001CAF60) or 001DA6A0, with a0 naming the original pool record.
 * Unknown methods fail. Missing resources do not substitute another area.
 * Restore the caller's active/inactive actor-view state on a draw failure
 * unless the actor view itself is faulty, preserving the primary fault. */
int em_area01_model_draw_call(EmArea01ModelDraw *draw, uint32_t function, uint32_t record);
#endif
