/* Resource and forwarding contract; interpreter behavior has the separate
 * original-instruction corpus. The loader mock models replacement delivery. */
#include "game/em_area01_script_live.h"
#include "game/em_area11_boxes.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static EmAreaScriptHostArea area;
static uint8_t overlay[0x9800], replacement[0x9800], *delivered=overlay;
static uint8_t banks[0x400];
static unsigned mapped;
static EmStatusSceneLoader loader_state;
static EmOwnerServicesOwner model;
static EmArea01Call call;
static int reject;
int em_area11_script_host_area(const EmAreaScriptHostArea *a) { area=*a;return 0; }
uint8_t *em_area11_script_host_timeline_bytes(uint32_t a,uint32_t n) { (void)a;(void)n;return NULL; }
const uint8_t *em_module_loader_memory(const EmModuleLoader *l,uint32_t a,uint32_t n)
{ (void)l;return a>=0x823500u && a-0x823500u<=sizeof overlay && n<=sizeof overlay-(a-0x823500u) ? delivered+a-0x823500u : NULL; }
uint8_t *em_module_loader_memory_mutable(EmModuleLoader *l,uint32_t a,uint32_t n)
{ return (uint8_t *)(void *)em_module_loader_memory(l,a,n); }
EmStatusSceneLoader *em_module_loader_state(EmModuleLoader *l) { (void)l;return &loader_state; }
const uint8_t *em_module_loader_memory_rest(const EmModuleLoader *l,uint32_t a,uint32_t *n)
{ (void)l;if(a<0x1300000u || a>=0x1300400u)return NULL;*n=0x1300400u-a;return banks+a-0x1300000u; }
static int map_bank(void *ctx,uint32_t a,uint32_t n,const uint8_t *p)
{ assert(ctx==banks && a==0x1300080u && n==0x380u && p==banks+0x80);++mapped;return 0; }
EmOwnerServicesOwner *em_area11_boxes_owner_view(EmActor *a) { return a ? &model : NULL; }
static int worker(void *ctx,EmArea01Call *c)
{ assert(ctx==&call);call=*c;c->v0=7;return reject ? -1 : 0; }
static void w32(uint8_t *p,uint32_t x) { memcpy(p,&x,4); }
int main(int argc,char **argv)
{
    assert(argc==3);
    memcpy(overlay,"MWo3",4);w32(overlay+4,2);w32(overlay+8,0x823500u);
    memcpy(replacement,overlay,sizeof overlay);
    EmArea01Script s={0};EmArea01RuntimeHost h={.ctx=&call,.worker=worker};
    EmModuleLoader *ld=(EmModuleLoader *)(void *)&loader_state;
    assert(em_area01_script_bind(&s,&h,ld,argv[1],argv[2])==0);
    assert(area.overlay_id==2);
    loader_state.d28A490[0x96]=0x1300200u;loader_state.d28A490[0x97]=0x1300080u;
    loader_state.d28A490[0x98]=0x1300100u;
    assert(em_area01_script_player_banks(&s,map_bank,banks)==0 && mapped==1);
    loader_state.d28A490[0x98]=0;
    assert(em_area01_script_player_banks(&s,map_bank,banks)<0 && mapped==1);
    EmScriptImage *im=area.image(area.ctx,0x24DE40u);assert(im==&s.boot[1]);
    uint8_t *p=em_area01_script_bytes(&s,0x24DCC4u,4,1);assert(p);
    assert(p==em_script_image_read(im,0x24DCC4u,4));
    w32(p,0x12345678u);assert(em_script_u32(area.resource(area.ctx,0x24DCC4u,4),0)==0x12345678u);
    assert(!em_area01_script_bytes(&s,0x24D8F0u,2,1));
    assert(!em_area01_script_bytes(&s,0x24DB80u,4,1));
    assert(!em_area01_script_bytes(&s,0x24DFA0u,4,1));
    assert(em_area01_script_bytes(&s,0x24DFA0u,32,0));
    assert(!em_area01_script_bytes(&s,0x24DFBEu,4,0));
    assert(!em_area01_script_bytes(&s,0xFFFFFFFFu,8,0));
    assert(area.image(area.ctx,0x829860u)->bytes==overlay+0x5500u);
    delivered=replacement;
    assert(area.image(area.ctx,0x829860u)->bytes==replacement+0x5500u);
    w32(replacement+4,12);assert(!area.image(area.ctx,0x829860u));w32(replacement+4,2);
    assert(!area.image(area.ctx,0x8292C0u)); /* AREA11 alias must not resolve */
    uint32_t bank=7;assert(area.owner_bank(area.ctx,(EmActor *)&s,&bank,1)==0 && model.anim==7);
    bank=0;assert(area.owner_bank(area.ctx,(EmActor *)&s,&bank,0)==0 && bank==7);
    int32_t result=0;
    const uint32_t funcs[]={0x825130u,0x825240u,0x1BAC00u,0x1BBAE0u,0x1BBBF0u,0x1B6EA0u};
    for(unsigned i=0;i<sizeof funcs/sizeof funcs[0];++i) {
        assert(area.record(area.ctx,funcs[i],0x7ABD10u,0x7ABF00u,0x829FA0u,&result)==0);
        assert(result==7 && call.function==funcs[i] && call.na==3 && call.a[0]==0x7ABD10u &&
               call.a[1]==0x7ABF00u && call.a[2]==0x829FA0u);
    }
    assert(area.record(area.ctx,0x825900u,0,0,0x829FA0u,&result)<0);
    assert(area.clip(area.ctx,0x7ABD10u,-2,20.0f,0.0f)==0 && call.function==0x1C67E0u &&
           call.a[1]==UINT64_MAX-1 && call.f[0]==0x41A00000u && call.nf==2);
    assert(area.camera(area.ctx,0x22EC30u,0x8101E0u)==0 && call.na==1);
    assert(area.camera(area.ctx,0x22EEF0u,0x8101E0u)==0 && call.na==2 && call.a[1]==1);
    reject=1;assert(area.record(area.ctx,0x825130u,0,0,0x829FA0u,&result)<0);
    em_area01_script_free(&s);assert(!s.bound && !s.boot[0].bytes && !s.overlay.bytes);
    puts("PASS: AREA01 script binding aliases, replacement delivery, identity, exact callback arguments and failures");
    return 0;
}
