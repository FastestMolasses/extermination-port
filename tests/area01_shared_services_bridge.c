#include "game/em_area01_shared_services.h"
#include "game/em_area01_scene_view.h"
#include "game/em_actor_collision.h"
#include "game/em_player_stage_workers.h"
#include <string.h>
static EmSceneState scene;
static EmPlayerLiveActor player;
static EmActorPool pool;
static EmActorClassLists lists;
static EmSdkMathTables tables;
static EmSdkMathContext sdk;
static int32_t math_mode=-1;
static uint8_t geometry[0x100];
static uint64_t result;
static uint32_t deny_address,stores[16],store_count;
uint32_t *ss_stores(void){return stores;}
unsigned ss_store_count(void){return store_count;}
uint8_t *ss_bytes(uint32_t a,uint32_t n)
{
    if(a>=0x8102B0 && n && n<=sizeof player.bytes && a-0x8102B0<=sizeof player.bytes-n)
        return player.bytes+a-0x8102B0;
    if(a>=0x680000 && n && n<=sizeof geometry && a-0x680000<=sizeof geometry-n)
        return geometry+a-0x680000;
    return em_area01_scene_view(&scene,a,n);
}
static uint8_t *memory(void *ctx,uint32_t a,uint32_t n,int write)
{(void)ctx;if(write && store_count<16)stores[store_count++]=a;return write && a==deny_address?NULL:ss_bytes(a,n);}
/* World getter boundary only: the actual class-list owner runs here. */
int em_collision_world_publish_001B1B70(const EmActor *a)
{return em_actor_class_publish_001B1B70(&lists,a);}
int ss_init(const uint8_t *elf,uint32_t n)
{
    memset(&scene,0,sizeof scene);memset(&player,0,sizeof player);memset(&sdk,0,sizeof sdk);
    sdk.tables=&tables;sdk.world.d26C5D0=&math_mode;
    return em_sdk_math_original_load_tables(elf,n,&tables);
}
uint64_t ss_result(void){return result;}
void ss_deny(uint32_t a){deny_address=a;}
int ss_call(uint32_t fn,uint32_t a,uint32_t b,uint32_t d,uint32_t e)
{
    sdk.fault=0;store_count=0;EmArea01RuntimeHost h={.bytes=memory};
    EmArea01Call c={.function=fn,.a={a,b,d,e},.na=fn==0x1B1EA0?4u:fn==0x1C4760?2u:1u};
    int rc=em_area01_shared_services_call(&h,&sdk,&scene,&c);result=c.v0;return rc;
}
void ss_lists_reset(void){memset(&pool,0,sizeof pool);em_actor_class_lists_reset(&lists);}
int ss_publish(uint32_t index,uint32_t cls)
{
    if(index>=EM_ACTOR_POOL_CAPACITY)return -1;
    EmActor *a=&pool.records[index];a->cls=(uint8_t)cls;a->allocated=1;a->self=a;
    EmArea01Call c={.function=0x1B1B70,.a={EM_ACTOR_POOL_BASE+index*EM_ACTOR_RECORD_SIZE},.na=1};
    int rc=em_area01_shared_services_publish(&pool,&c);result=c.v0;return rc;
}
unsigned ss_list_count(unsigned i){return i<EM_ACTOR_LIST_COUNT?(unsigned)lists.list[i].live:0;}
uint32_t ss_list_entry(unsigned i,unsigned j)
{
    if(i>=EM_ACTOR_LIST_COUNT||j>=EM_ACTOR_LIST_MAX)return 0;
    const EmActor *a=lists.list[i].slot[j];return a?EM_ACTOR_POOL_BASE+(uint32_t)(a-pool.records)*EM_ACTOR_RECORD_SIZE:0;
}
