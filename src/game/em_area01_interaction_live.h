/* Canonical published-list and candidate views for the shared Use scan. */
#ifndef EM_AREA01_INTERACTION_LIVE_H
#define EM_AREA01_INTERACTION_LIVE_H
#include "game/em_area01_runtime.h"
#include "game/em_interaction_scan.h"
typedef struct {
    EmArea01RuntimeHost host;
    const EmInteractionMath *math;
    int (*claim)(void *ctx,uint32_t node); /* 1 accepted, -1 failed */
    void *claim_ctx;
    uint32_t fault_address;
    int fault;
} EmArea01Interaction;
int em_area01_interaction_bind(EmArea01Interaction *,const EmArea01RuntimeHost *,
    const EmInteractionMath *,int (*claim)(void *,uint32_t),void *ctx);
/* 00183EF0(player,node), 00184BA0(player). Active byte views in/out.
 * The claim callback observes the winner's +B=4 while selector is still 0;
 * it commits the views and claims the existing shared staged token. */
int em_area01_interaction_call(EmArea01Interaction *,EmArea01Call *);
#endif
