/* A captured canonical address view for the production light adapter.
 * RCL service boundaries use the same original owners as live RCL. */
#include "game/em_area01_light_live.h"
#include "game/em_owner_draw_live.h"
#include "game/em_render_context_live.h"
#include "game/em_frame_render_heads.h"
#include "game/em_packet_chain_original.h"
#include "game/em_load_veil_particles.h"
#include "game/em_aim_fire_sdk_memory.h"
#include <string.h>
static uint8_t *ram,*spr;
static uint32_t frame;
static EmPacketChain chain;
static EmPacketChainRegion region;
static EmLoadVeilParticles veil;
static EmFrh frh;
static EmFrhView frh_view;
uint32_t em_frame_counter(void) { return frame; }
uint32_t em_rcl_fault(void) { return 0; }
const uint8_t *em_rcl_bytes(uint32_t a,uint32_t n)
{
    if (a>=0x70000000u && a-0x70000000u<0x4000u && n<=0x4000u-(a-0x70000000u))
        return spr+a-0x70000000u;
    return a<0x2000000u && n<=0x2000000u-a ? ram+a : NULL;
}
uint8_t *em_rcl_bytes_mut(uint32_t a,uint32_t n)
{ return (uint8_t *)(void *)em_rcl_bytes(a,n); }
EmPointLightPool *em_rcl_point_lights(void)
{ return (EmPointLightPool *)(void *)(ram+EM_RCL_CONTEXT+0x210); }
int em_rcl_001D80B0(int32_t id) { return em_frh_001D80B0(&frh,id); }
EmPacketChain *em_rcl_packet_chain(void) { return &chain; }
int em_rcl_001D1F80(int32_t a0,int32_t a1,int32_t a2)
{ return em_load_veil_particles_001D1F80(&veil,a0,a1,a2); }
void al_seed(uint8_t *memory,uint8_t *scratch)
{
    ram=memory; spr=scratch; ++frame;
    region=(EmPacketChainRegion){0,0x2000000,ram};
    uint32_t base;memcpy(&base,ram+0x275674,4);
    em_packet_chain_init(&chain,&region,1,EM_RCL_CONTEXT,base);
    memset(&veil,0,sizeof veil);
    veil.world.cursor=(uint32_t *)(void *)(ram+EM_RCL_CONTEXT+0x10);veil.world.cursor_count=4;
    veil.world.ctx_9C=(uint32_t *)(void *)(ram+EM_RCL_CONTEXT+0x9C);
    veil.world.d00275674=(uint32_t *)(void *)(ram+0x275674);
    veil.world.packet=ram;veil.world.packet_address=0;veil.world.packet_size=0x2000000;
    memset(&frh,0,sizeof frh);frh_view=(EmFrhView){0,0x2000000,ram,1};
    frh.views=&frh_view;frh.view_count=1;
    em_owner_draw_live_reset();
}
int al_call(const EmArea01RuntimeHost *h,EmArea01Call *c,uint32_t *fault)
{ return em_area01_light_call(h,c,fault); }
int al_prepare(const EmArea01RuntimeHost *h,const EmArea01Call *c,EmArea01LightService *s)
{ return em_area01_light_service_prepare(h,c,s); }
int al_invoke(const EmArea01RuntimeHost *h,const EmArea01LightService *s,EmArea01Call *c)
{ return em_area01_light_service_invoke(h,s,c); }
int al_page(uint32_t a) { return em_owner_draw_live_page_unit(a)!=NULL; }
int al_triangles(uint32_t address,EmObjectUnitTriangle *out,uint32_t capacity)
{
    const EmObjectUnitPieces *p=em_owner_draw_live_page_unit(address);
    EmObjectUnitResult result={0};
    if(!p || em_object_unit_run(&p->unit,&result)<0) {
        em_object_unit_result_free(&result);return -1;
    }
    int count=(int)result.count;
    if(result.count>capacity)count=-1;
    else memcpy(out,result.tri,result.count*sizeof *out);
    em_object_unit_result_free(&result);return count;
}
static const uint8_t *resolve(void *ctx,uint32_t a,uint32_t n)
{ (void)ctx;return em_rcl_bytes(a,n); }
int al_parse(uint32_t address,uint32_t size,int light)
{
    EmObjectUnitPieces p;const char *why=NULL;
    return light ? em_object_unit_parse_light(ram+address,size,resolve,NULL,&p,&why)
                 : em_object_unit_parse(ram+address,size,resolve,NULL,&p,&why);
}
static void *sdk_map(void *ctx,uint32_t a,size_t n,int write)
{
    const EmArea01RuntimeHost *h=ctx;
    return n<=UINT32_MAX ? h->bytes(h->ctx,a,(uint32_t)n,write) : NULL;
}
int al_sdk(const EmArea01RuntimeHost *h,EmArea01Call *c)
{
    EmAimFireTargetCall f={.function=c->function,.sp=c->sp,.na=c->na,.nf=c->nf};
    if(c->na>7)return -1;
    memcpy(f.a,c->a,sizeof f.a);memcpy(f.f,c->f,sizeof f.f);
    int rc=em_aim_fire_sdk_memory_call((void *)h,sdk_map,&f);
    c->v0=f.v0;c->f0=f.f0;return rc;
}
