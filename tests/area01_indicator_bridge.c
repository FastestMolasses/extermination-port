/* The production canonical actor/model adapters; only RNG and final draw
 * are explicit test boundaries. No persistent indicator record is added. */
#include "area01_model_bridge.c"
#include "game/em_area01_indicator_live.h"
static uint32_t indicator_fault,indicator_random,indicator_draws,indicator_rngs;
static uint32_t last_color[4],last_matrix[16];
static int indicator_reject;
static uint8_t *indicator_bytes(void *ctx,uint32_t a,uint32_t n,int write)
{
    (void)ctx;uint8_t *p=em_area01_actor_view_bytes(&views,a,n,write);
    if(p)return p;
    p=em_area01_model_slot_bytes(&model,a,n);if(p)return p;
    if(a>=0x70000000u && a-0x70000000u<=0x4000u && n<=0x4000u-(a-0x70000000u))
        return spr+a-0x70000000u;
    return a<0x2000000u && n<=0x2000000u-a ? ram+a : NULL;
}
static int indicator_worker(void *ctx,EmArea01Call *c)
{
    (void)ctx;uint32_t out=0;
    if(indicator_reject)return -1;
    if(c->function==0x001C2360u || c->function==0x001C22A0u || c->function==0x001C6380u) {
        int rc=em_area01_model_call(&model,c->function,(uint32_t)c->a[0],0,0,0,0,0,&out);
        c->v0=out;return rc;
    }
    if(c->function==0x00122BB8u) {
        if(em_area01_actor_view_commit(&views)<0)return -1;
        ++indicator_rngs;c->v0=indicator_random;
        return em_area01_actor_view_begin(&views);
    }
    if(c->function==0x001CACB0u) {
        uint8_t *p=em_area01_actor_view_bytes(&views,(uint32_t)c->a[0]+0x80,16,0);
        uint8_t *word=em_area01_actor_view_bytes(&views,(uint32_t)c->a[0]+0x110,4,0);
        if(!p || !word)return -1;
        uint32_t address;memcpy(&address,word,4);
        uint8_t *slot=em_area01_model_slot_bytes(&model,address,EM_POSE_NODE_BYTES);
        if(!slot)return -1;
        memcpy(last_color,p,16);memcpy(last_matrix,slot+0x90,64);
        if(em_area01_actor_view_commit(&views)<0)return -1;
        ++indicator_draws;
        pool.records[((uint32_t)c->a[0]-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE].drawn=1;
        return em_area01_actor_view_begin(&views);
    }
    if(c->function==0x001AFC10u) {
        if(em_area01_actor_view_commit(&views)<0 || am_release_pool((uint32_t)c->a[0],0)<0)return -1;
        return em_area01_actor_view_begin(&views);
    }
    return -1;
}
int ai_seed(uint8_t *memory,uint8_t *scratch,uint32_t node,int preserve)
{
    indicator_fault=indicator_draws=indicator_rngs=0;indicator_reject=0;
    memset(last_color,0,sizeof last_color);memset(last_matrix,0,sizeof last_matrix);
    if(am_seed(memory,scratch,node,preserve)<0)return -1;
    am_snapshot();am_pool_snapshot();return 0;
}
int ai_step(uint32_t fn,uint32_t node,uint32_t random)
{
    indicator_random=random;
    const EmArea01RuntimeHost host={NULL,indicator_bytes,indicator_worker};
    EmArea01Call call={.function=fn,.sp=0x7F0F0000u,.na=1};call.a[0]=node;
    if(em_area01_actor_view_begin(&views)<0)return -1;
    int rc=em_area01_indicator_call(&host,&call,&indicator_fault);
    if(views.fault || em_area01_actor_view_commit(&views)<0)return -1;
    return rc;
}
void ai_snapshot(void)
{
    if(pool.records[(current-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE].self)am_snapshot();
    am_pool_snapshot();
}
int ai_write(uint32_t address,const uint8_t *source,uint32_t size)
{
    if(em_area01_actor_view_begin(&views)<0)return -1;
    uint8_t *p=indicator_bytes(NULL,address,size,1);if(!p)return -1;
    memcpy(p,source,size);return em_area01_actor_view_commit(&views);
}
void ai_cap(int16_t value){S.bones.count=value;}
void ai_reject(void){indicator_reject=1;}
uint32_t ai_fault(void){return indicator_fault ? indicator_fault : model.fault_address;}
uint32_t ai_draws(void){return indicator_draws;}
uint32_t ai_rngs(void){return indicator_rngs;}
const uint32_t *ai_color(void){return last_color;}
const uint32_t *ai_matrix(void){return last_matrix;}
