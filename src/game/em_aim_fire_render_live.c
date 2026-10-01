#include "game/em_aim_fire_render_live.h"
#include "game/em_aim_fire_reticle.h"
#include "game/em_packet_chain_original.h"
#include "game/em_player_equipment_sprite.h"
#include "game/em_render_context_live.h"
#include <string.h>

static int fail(EmAimFireRenderLive *h, uint32_t fn, uint32_t address)
{
    if (h && !h->fault_function) {
        h->fault_function=fn ? fn : UINT32_MAX;
        h->fault_address=address;
    }
    return -1;
}
static uint8_t *bytes(EmAimFireRenderLive *h,uint32_t address,uint32_t size,int write)
{
    uint8_t *p=write ? em_rcl_bytes_mut(address,size) : (uint8_t *)(uintptr_t)em_rcl_bytes(address,size);
    if (!p && h->map) p=h->map(h->context,address,size,write);
    return p;
}
static int forward(EmAimFireRenderLive *h,EmAimFireTargetCall *c)
{
    if (!h->call || h->call(h->context,c)<0) return fail(h,c->function,0);
    return 0;
}
static int chain_call(EmAimFireRenderLive *h,EmAimFireTargetCall *c)
{
    EmPacketChain *pc=em_rcl_packet_chain();
    if (!pc) return fail(h,c->function,0x275670);
    int status;
    uint32_t packet=0;
    if (c->function==0x1CB5F0) {
        status=em_packet_chain_001CB5F0(pc,(uint32_t)c->a[0],(int32_t)c->a[1],(int32_t)c->a[2],&packet,NULL);
        if (status==0) c->v0=(uint64_t)(int64_t)(int32_t)packet;
    } else if (c->function==0x1CB6B0)
        status=em_packet_chain_001CB6B0(pc,(uint32_t)c->a[0],(int32_t)c->a[1],(int32_t)c->a[2],c->a[3]);
    else if (c->function==0x1CB900)
        status=em_packet_chain_001CB900(pc,(uint32_t)c->a[0],(int32_t)c->a[1],(int32_t)c->a[2]);
    else return -1;
    return status<0 ? fail(h,pc->fault_function ? pc->fault_function:c->function,pc->fault_address) : 0;
}
static int matrix(EmAimFireRenderLive *h,uint32_t index,uint32_t *address)
{
    const uint8_t *p=em_rcl_bytes(0x275670,4);
    if (!p) return fail(h,0x1CD370,0x275670);
    uint32_t context;memcpy(&context,p,4);
    *address=context+0x2240+(index<<6);
    if (!em_rcl_bytes(*address,64)) return fail(h,0x1CD370,*address);
    return 0;
}
static int sprite_matrix(void *context,int32_t index,uint32_t out[16])
{
    EmAimFireRenderLive *h=context;uint32_t address;
    if (matrix(h,(uint32_t)index,&address)<0) return -1;
    memcpy(out,em_rcl_bytes(address,64),64);return 0;
}
static int sprite_open(void *context,uint32_t table,int32_t depth,int32_t count,uint8_t **packet)
{
    EmAimFireRenderLive *h=context;
    /* Reaching this call proves 001CD520 passed its clip and loaded vf23. */
    const uint8_t *fog=em_rcl_bytes(EM_RCL_CONTEXT+0xA0,16);
    if (!fog) return fail(h,0x1CD520,EM_RCL_CONTEXT+0xA0);
    memcpy(h->vu->vf[23],fog,16);*h->vf23_valid=1;
    EmAimFireTargetCall c={0};c.function=0x1CB5F0;c.a[0]=table;c.a[1]=(uint64_t)(int64_t)depth;c.a[2]=(uint64_t)(int64_t)count;c.na=3;
    if (chain_call(h,&c)<0) return -1;
    *packet=em_rcl_bytes_mut((uint32_t)c.v0,(uint32_t)count*16);
    return *packet ? 0:fail(h,c.function,(uint32_t)c.v0);
}
static int sprite_ref(void *context,uint32_t table,int32_t depth,int32_t kind,uint32_t address)
{
    EmAimFireTargetCall c={0};c.function=0x1CB6B0;c.a[0]=table;c.a[1]=(uint64_t)(int64_t)depth;c.a[2]=(uint64_t)(int64_t)kind;c.a[3]=address;c.na=4;
    return chain_call(context,&c);
}
static int sprite_blend(void *context,uint32_t table,int32_t depth,int32_t mode)
{
    EmAimFireTargetCall c={0};c.function=0x1CB900;c.a[0]=table;c.a[1]=(uint64_t)(int64_t)depth;c.a[2]=(uint64_t)(int64_t)mode;c.na=3;
    return chain_call(context,&c);
}
static int sprite(EmAimFireRenderLive *h,EmAimFireTargetCall *c)
{
    uint32_t point[4],fog[4],p[16],k[16];
    uint8_t *src=bytes(h,(uint32_t)c->a[2]&~15u,16,0);
    const uint8_t *f=em_rcl_bytes(EM_RCL_CONTEXT+0xA0,16);
    const uint8_t *pm=em_rcl_bytes(0x70003A40,64),*km=em_rcl_bytes(0x70003AC0,64);
    uint8_t *scratch=bytes(h,0x70003600,32,1);
    if (!src || !f || !pm || !km || !scratch) return fail(h,c->function,!src?(uint32_t)c->a[2]:!f?EM_RCL_CONTEXT+0xA0:!pm?0x70003A40:!km?0x70003AC0:0x70003600);
    memcpy(point,src,16);memcpy(fog,f,16);memcpy(p,pm,64);memcpy(k,km,64);
    const EmPlayerEquipmentSpriteWorld world={fog,p,k,(uint32_t *)(void *)scratch};
    const EmPlayerEquipmentSpriteWorkers workers={h,sprite_matrix,sprite_open,sprite_ref,sprite_blend};
    EmPlayerEquipmentFault fault={0};const EmPlayerEquipmentSprite owner={&workers,&world,&fault};
    int32_t result;
    int status=em_player_equipment_001CD520(&owner,(int32_t)c->a[0],(int32_t)c->a[1],point,c->a[3],c->f[0],c->f[1],c->f[2],c->a[4],&result);
    if (status<0) return fail(h,c->function,fault.address);
    c->v0=(uint64_t)(int64_t)result;return 0;
}
static int beam_worker(void *context,EmArea00HudCall *in)
{
    EmAimFireRenderLive *h=context;EmAimFireTargetCall c={0};
    if (in->na>7 || in->nf>8) return fail(h,in->fn,0);
    c.function=in->fn;c.sp=in->sp;c.na=in->na;c.nf=in->nf;
    memcpy(c.a,in->a,c.na*sizeof *c.a);memcpy(c.f,in->f,c.nf*sizeof *c.f);
    int status;
    if (c.function==0x1CD370) {
        uint32_t address=0;
        status=matrix(h,(uint32_t)c.a[0],&address);c.v0=(uint64_t)(int64_t)(int32_t)address;
    } else if (c.function==0x1CB5F0 || c.function==0x1CB6B0 || c.function==0x1CB900)
        status=chain_call(h,&c);
    else {
        /* VU0 SDK leaves used here never write vf23. The beam overwrites the
         * other SDK-clobbered registers before it reads them. */
        status=forward(h,&c);
    }
    in->v0=c.v0;in->f0=c.f0;return status;
}
static int beam_region(EmAimFireRenderLive *h,EmArea00HudRegion *r,uint32_t address,uint32_t size,int write,int canonical)
{
    r->base=address;r->size=size;
    r->bytes=canonical ? (write?em_rcl_bytes_mut(address,size):(uint8_t *)(uintptr_t)em_rcl_bytes(address,size)) : bytes(h,address,size,write);
    return r->bytes?0:fail(h,0x1E2BA0,address);
}
static int beam(EmAimFireRenderLive *h,EmAimFireTargetCall *c)
{
    EmArea00HudRegion region[9];unsigned n=0;
#define REGION(a,z,w,own) do { if(beam_region(h,&region[n++],(a),(z),(w),(own))<0)return -1; }while(0)
    REGION(0x275670,4,0,1);REGION(EM_RCL_CONTEXT,EM_RCL_CONTEXT_SIZE,0,1);
    REGION(0x70003AC0,64,0,1);REGION(0x70003600,64,1,0);
    REGION(c->sp-0xE0,0xE0,1,0);REGION(0x28F700,0x4D3EC0,1,1);
    for(unsigned i=0;i<2;++i)REGION((uint32_t)c->a[i]&~15u,16,0,0);
    REGION((uint32_t)c->a[2],16,0,0);
#undef REGION
    EmArea00Hud owner={0};owner.regions=region;owner.region_count=n;owner.call=beam_worker;owner.ctx=h;owner.sp=c->sp;owner.vu=*h->vu;
    int status=em_area00_hud_001E2BA0(&owner,(uint32_t)c->a[0],(uint32_t)c->a[1],(uint32_t)c->a[2]);
    *h->vu=owner.vu;
    if (status<0) { *h->vf23_valid=0;return fail(h,owner.fault_function?owner.fault_function:c->function,owner.fault_address); }
    *h->vf23_valid=1;return 0;
}
static uint8_t *reticle_map(void *context,uint32_t address,uint32_t size,int write)
{ return bytes(context,address,size,write); }
static int reticle_worker(void *context,EmAimFireReticleCall *in)
{
    EmAimFireRenderLive *h=context;EmAimFireTargetCall c={0};
    c.function=in->function;c.sp=in->sp;c.na=in->na;c.nf=in->nf;
    memcpy(c.a,in->a,sizeof in->a);c.f[0]=in->f12;
    int status=(c.function==0x1DD2F0 || c.function==0x1DD600) ? em_aim_fire_render_live_call(h,&c):forward(h,&c);
    in->v0=c.v0;return status;
}
static int reticle(EmAimFireRenderLive *h,EmAimFireTargetCall *c)
{
    if (c->function==0x1DD170 && !*h->vf23_valid) return fail(h,c->function,23);
    EmAimFireReticle owner={0};owner.context=h;owner.map=reticle_map;owner.call=reticle_worker;owner.sp=c->sp;owner.vu=h->vu;
    int status=c->function==0x1DD170 ? em_aim_fire_reticle_001DD170(&owner,(uint32_t)c->a[0],(uint32_t)c->a[1],(uint32_t)c->a[2],(uint32_t)c->a[3],(uint32_t)c->a[4]):
        c->function==0x1DD2F0 ? em_aim_fire_reticle_001DD2F0(&owner,(uint32_t)c->a[0],(uint32_t)c->a[1],(uint32_t)c->a[2]):
        em_aim_fire_reticle_001DD600(&owner,(uint32_t)c->a[0],(uint32_t)c->a[1],(uint32_t)c->a[2]);
    return status<0 ? fail(h,owner.fault_function?owner.fault_function:c->function,owner.fault_address):0;
}
int em_aim_fire_render_live_call(EmAimFireRenderLive *h,EmAimFireTargetCall *c)
{
    if (!h || !c || h->fault_function) return -1;
    if (!em_rcl_packet_chain()) return fail(h,c->function,0x275670);
    if (c->function==0x1CD370) {
        uint32_t address=0;
        if (matrix(h,(uint32_t)c->a[0],&address)<0) return -1;
        c->v0=(uint64_t)(int64_t)(int32_t)address;return 0;
    }
    if (c->function==0x1CB5F0 || c->function==0x1CB6B0 || c->function==0x1CB900)
        return chain_call(h,c);
    if (!h->vu || !h->vf23_valid || !h->map) return fail(h,c->function,0);
    switch(c->function) {
    case 0x1CD520:return sprite(h,c);
    case 0x1E2BA0:return beam(h,c);
    case 0x1DD170:case 0x1DD2F0:case 0x1DD600:return reticle(h,c);
    default:return fail(h,c->function,0);
    }
}
