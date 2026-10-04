#include "game/em_area01_player_view.h"
#include "game/em_area01_scene_view.h"
#include "game/em_area01_math_player.h"
#include <string.h>
static EmPlayerLiveActor actor;
static EmArea01PlayerView view;
static EmArea01PlayerValues owners;
static EmSceneState scene;
static uint8_t other[0x400];
static uint16_t span_lo;
static int snapshot(void *ctx,EmArea01PlayerValues *out){(void)ctx;*out=owners;return 0;}
static void publish(void *ctx,EmPlayerLiveActor *a)
{
    (void)ctx;
    memcpy(owners.position,a->bytes+0xA0,12);memcpy(&owners.heading,a->bytes+0xC4,4);
    memcpy(owners.vitals,a->bytes+0x220,16);owners.iframes=em_live_u16(a,0x20E);
    owners.infected=a->bytes[0x234];owners.low=a->bytes[0x235]&1;
}
void pv_reset(const uint8_t *bytes)
{
    memset(&actor,0,sizeof actor);memcpy(actor.bytes,bytes,sizeof actor.bytes);
    memset(&owners,0,sizeof owners);memcpy(owners.position,bytes+0xA0,12);
    memcpy(owners.hip,bytes+0xB0,12);memcpy(&owners.heading,bytes+0xC4,4);
    memcpy(owners.vitals,bytes+0x220,16);owners.iframes=em_live_u16(&actor,0x20E);
    owners.infected=bytes[0x234];owners.low=bytes[0x235]&1;owners.generation=7;owners.hip_valid=1;
    owners.link_owner=em_live_u32(&actor,0x214);owners.link_prev=em_live_u32(&actor,0x308);
    em_area01_player_view_init(&view,&actor,snapshot,publish,NULL);
    memset(&scene,0,sizeof scene);memset(other,0,sizeof other);span_lo=0x1234;
}
int pv_begin(void){return em_area01_player_view_begin(&view);}
int pv_borrow_begin(void){return em_area01_player_view_borrow_begin(&view);}
int pv_commit(void){return em_area01_player_view_commit(&view);}
int pv_fault(void){return view.fault;}
void *pv_actor(void){return actor.bytes;}
void *pv_owners(void){return &owners;}
unsigned pv_owner_size(void){return sizeof owners;}
void *pv_other(void){return other;}
unsigned pv_span(void){return span_lo;}
void pv_stale(unsigned kind)
{
    if(kind==0)owners.generation++;
    if(kind==1)owners.position[0]^=1;
    if(kind==2)owners.hip[0]^=1;
    if(kind==3)owners.owner_generation++;
    if(kind==4)actor.link_owner=(void *)1;
    if(kind==5)actor.link_flags^=1;
}
uint8_t *pv_memory(void *ctx,uint32_t a,uint32_t n,int write)
{
    (void)ctx;
    if(a>=EM_AREA01_PLAYER_BASE && a<EM_AREA01_PLAYER_BASE+EM_PLAYER_ACTOR_SIZE)
        return em_area01_player_view_bytes(&view,a,n,write);
    if(a>=0x01100000 && n && n<=sizeof other && a-0x01100000<=sizeof other-n)
        return other+(a-0x01100000);
    if(a>=0x70003B86 && n && n<=2 && a-0x70003B86<=2-n)return (uint8_t *)&span_lo+(a-0x70003B86);
    return em_area01_scene_view(&scene,a,n);
}
int pv_sound(uint32_t a,uint32_t b)
{
    EmA01Math math={.view=pv_memory};
    return em_area01_math_00187EC0(&math,a,b);
}
uint8_t *sv_bytes(uint32_t a,uint32_t n){return em_area01_scene_view(&scene,a,n);}
void sv_native(void){scene.d810701=9;scene.spad3B8D=7;scene.spad3B68=0x12345678;scene.d810E70=0xFEDC;}
int sv_check(void){return scene.d810701==5 && scene.spad3B8D==3 && scene.spad3B68==0x12345678 && scene.d810E70==0xFEDC;}
