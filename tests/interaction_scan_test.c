#include "game/em_interaction_scan.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct { int result, writes; float score; } Rule;
typedef struct { unsigned calls; } Context;

static int raycast(void *opaque,const float from[4],const float to[4],
                   unsigned mode,EmInteractionRayHit *hit)
{
    assert(mode==6 && from[3]==1 && to[3]==1);
    *hit=*(const EmInteractionRayHit *)opaque;
    return 1;
}

static int predicate(void *opaque,const EmInteractionCandidate *candidate,float *score)
{
    Context *context=opaque;
    const Rule *rule=candidate->owner;
    ++context->calls;
    if (rule->writes) *score=rule->score;
    return rule->result;
}

int main(void)
{
    EmInteractionScanState state={0};
    Context context={0};
    Rule rules[32];uint8_t armed[32]={0};EmInteractionCandidate candidates[32];
    for (unsigned i=0;i<32;++i) {
        rules[i]=(Rule){1,1,32-i};
        candidates[i]=(EmInteractionCandidate){&rules[i],3,0x84,&armed[i]};
    }
    size_t winner;
    assert(em_interaction_scan(&state,candidates,32,predicate,&context,&winner)==1);
    assert(context.calls==32 && winner==31 && armed[31]==4 && state.selector==3);
    assert(state.score==1);
    state.score=99;
    assert(em_interaction_scan(&state,NULL,999,NULL,NULL,&winner)==0);
    assert(winner==SIZE_MAX && state.score==99 && context.calls==32);
    state.selector=0;
    assert(em_interaction_scan(&state,candidates,33,predicate,&context,&winner)==-1);
    assert(state.score==99);
    assert(em_interaction_scan(&state,NULL,0,NULL,NULL,&winner)==0 && state.score==0);
    candidates[0].armed=NULL;
    assert(em_interaction_scan(&state,candidates,1,predicate,&context,&winner)==-1);
    candidates[0].armed=&armed[0];
    rules[0]=(Rule){0,1,4};rules[1]=(Rule){1,0,999};
    assert(em_interaction_scan(&state,candidates,2,predicate,&context,&winner)==1);
    assert(winner==1 && armed[1]==4 && state.score==4);
    EmInteractionList list={0};
    for (unsigned i=0;i<40;++i) em_interaction_list_push(&list,&candidates[i%32]);
    assert(list.pending_count==32 && list.active_count==0);
    em_interaction_list_publish(&list);
    assert(list.pending_count==0 && list.active_count==32);
    assert(list.active[0].owner==candidates[31].owner);
    EmInteractionPickup pickup={.identity=123,.class_flags=0x84,.selector=3,
        .callback=0x219550,.position={0,0,6},.descriptor={10,3.5}};
    EmInteractionPlayer player={.view_target={0,0,10}};
    EmInteractionMath math={0}; /* auto-facing path never reads coefficients */
    float score=0;EmInteractionRayHit hit={.hit=1,.flags=0x2800,.kind=2,.owner=456};
    assert(em_interaction_pickup_candidate(&pickup,&player,&math,NULL,NULL,&score)==-1);
    assert(em_interaction_pickup_candidate(&pickup,&player,&math,raycast,&hit,&score)==0);
    hit.owner=123;
    assert(em_interaction_pickup_candidate(&pickup,&player,&math,raycast,&hit,&score)==1);
    assert(score==6);
    pickup.class_flags=0x87;player.action=0x2d;hit.hit=0;
    assert(em_interaction_pickup_candidate(&pickup,&player,&math,raycast,&hit,&score)==2);
    assert(score==6); /* special action does not write the planar score */
    puts("Interaction scan: bounded host contracts, live winner writes and shared score PASS");
    return 0;
}
