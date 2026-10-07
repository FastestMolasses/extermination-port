#include "game/em_area01_shared_services.h"
#include "game/em_actor_roster.h"
#include "game/em_collision_world.h"
#include "game/em_director_original.h"
#include "game/em_panel.h"
#include "game/em_script_host_workers.h"
#include <string.h>

static int atan2_original(void *ctx,float y,float x,float *out)
{ EmSdkMathContext *s=ctx;*out=em_sdk_math_original_float_0011E620(s,y,x);return s->fault?-1:0; }
static int predicate_store(void *ctx,uint32_t address,const void *value,uint32_t size)
{
    const EmArea01RuntimeHost *h=ctx;
    uint8_t *out=h->bytes(h->ctx,address,size,1);
    if(!out)return -1;memcpy(out,value,size);return 0;
}
static int predicate(const EmArea01RuntimeHost *h,EmArea01Call *c)
{
    if(!h || !h->bytes || c->na!=1 || c->nf || c->a[0]!=0x008102B0u)return -1;
    EmScriptHostWorkers w={0};w.world.player_address=0x008102B0u;
    w.world.player=(EmPlayerLiveActor *)(void *)h->bytes(h->ctx,0x008102B0u,EM_PLAYER_ACTOR_SIZE,0);
    w.world.d8106BC=h->bytes(h->ctx,0x008106BCu,1,0);
    w.world.d81083C=h->bytes(h->ctx,0x0081083Cu,1,0);
    w.world.d8106F1=h->bytes(h->ctx,0x008106F1u,1,0);
    w.world.predicate_store=predicate_store;w.world.predicate_ctx=(void *)h;
    int32_t result=0;
    if(em_script_host_00182BF0(&w,0x008102B0u,&result)<0)return -1;
    c->v0=(uint64_t)(int64_t)result;return 0;
}
int em_area01_shared_services_call(const EmArea01RuntimeHost *h,EmSdkMathContext *sdk,
                                   EmSceneState *scene,EmArea01Call *c)
{
    if(!c)return -1;
    int32_t result=0;int rc;
    switch(c->function) {
    case 0x00182BF0u:return predicate(h,c);
    case 0x00157F60u:
        if(!h || !h->bytes || c->na!=3 || c->nf)return -1;
        if(em_panel_request_00157F60(h->ctx,h->bytes,(uint32_t)c->a[0])<0)return -1;
        result=1;break;
    case 0x001B11E0u:
        if(!scene || c->na!=1 || c->nf)return -1;
        result=em_actor_roster_001B11E0(scene,(EmActorRosterProgress *)em_scene_progress_spawn_view(scene),(int32_t)c->a[0]);
        if(result<0)return -1;break;
    case 0x001C4760u:
        if(!scene || c->na!=2 || c->nf)return -1;
        if(em_director_original_001C4760_scene(scene,(int32_t)c->a[0],(int32_t)c->a[1])<0)return -1;
        break;
    case 0x001B1EA0u: {
        if(!h || !h->bytes || c->na!=4 || c->nf)return -1;
        int32_t mode=(int32_t)c->a[0],count=(int32_t)c->a[3];
        const float *point=NULL;const float (*quad)[4]=NULL;
        if(count>=3 && mode>=0 && mode<=2) {
            if(!sdk || !sdk->tables || sdk->fault || (uint32_t)count>UINT32_MAX/16 ||
               (c->a[1]&3) || (c->a[2]&3))return -1;
            point=(const float *)(const void *)h->bytes(h->ctx,(uint32_t)c->a[1],12,0);
            quad=(const float (*)[4])(const void *)h->bytes(h->ctx,(uint32_t)c->a[2],16u*(uint32_t)count,0);
            if(!point || !quad)return -1;
        }
        rc=em_director_original_001B1EA0_bound(mode,point,quad,count,atan2_original,sdk,&result);
        if(rc<0)return -1;break;
    }
    default:return 1;
    }
    c->v0=(uint64_t)(int64_t)result;return 0;
}
int em_area01_shared_services_publish(EmActorPool *pool,EmArea01Call *c)
{
    if(!pool || !c || (c->function!=0x001B1B70u && c->function!=0x001B1DE0u) || c->na!=1 || c->nf)return -1;
    uint32_t node=(uint32_t)c->a[0];
    if(node<EM_ACTOR_POOL_BASE || (node-EM_ACTOR_POOL_BASE)%EM_ACTOR_RECORD_SIZE)return -1;
    uint32_t i=(node-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE;
    if(i>=EM_ACTOR_POOL_CAPACITY)return -1;
    EmActor *a=&pool->records[i];
    if(!a->allocated || a->self!=a)return -1;
    int rc=c->function==0x001B1DE0u
        ? em_collision_world_push80_001B1DE0(a)
        : em_collision_world_publish_001B1B70(a);
    if(rc<0)return -1;c->v0=(uint64_t)(int64_t)rc;return 0;
}
