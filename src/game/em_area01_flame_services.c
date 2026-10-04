#include "game/em_area01_flame_services.h"
#include "game/em_area01_render_gs.h"
#include "game/em_area01_render_hud.h"
#include "game/em_effects_live.h"
#include "game/em_stream_lanes_original.h"
#include "game/em_weather_packets.h"
#include "game/em_status_scene_original.h"
#include "game/em_aim_fire_runtime.h"
#include <string.h>

static int fail(uint32_t *fault,uint32_t address)
{ if(fault)*fault=address;return -1; }
static int convert(void *ctx,uint32_t f,int32_t *result)
{ (void)ctx;*result=em_stream_lanes_001281C0(f);return 0; }
typedef struct { const EmArea01RuntimeHost *host; uint32_t sp,fault; } Frame;
static int forward(Frame *f,EmArea01Call *c)
{
    c->sp=f->sp;
    if(f->fault)return -1;
    if(!f->host->worker || f->host->worker(f->host->ctx,c)<0) {
        f->fault=c->function;return -1;
    }
    return 0;
}
static uint64_t sx(uint32_t x){return (uint64_t)(int64_t)(int32_t)x;}
static int random_worker(void *ctx,int32_t *out)
{
    EmArea01Call c={.function=0x00122BB8u};int rc=forward(ctx,&c);*out=(int32_t)c.v0;return rc;
}
static int vector_worker(void *ctx,uint32_t obj,const uint32_t v[4],int32_t a2,int32_t a3)
{
    Frame *f=ctx;uint32_t at=f->sp+0x50u;
    uint8_t *p=f->host->bytes(f->host->ctx,at,16,1);
    if(!p){f->fault=at;return -1;}memcpy(p,v,16);
    EmArea01Call c={.function=0x001C7900u,.na=4,.a={sx(obj),sx(at),sx(a2),sx(a3)}};
    return forward(f,&c);
}
static int lookup_worker(void *ctx,uint32_t table,int32_t index,uint32_t *out)
{
    EmArea01Call c={.function=0x001C6120u,.na=2,.a={sx(table),sx(index)}};
    int rc=forward(ctx,&c);*out=(uint32_t)c.v0;return rc;
}
static int resource_worker(void *ctx,uint32_t entry)
{ EmArea01Call c={.function=0x001D3990u,.na=1,.a={sx(entry)}};return forward(ctx,&c); }
static int page_worker(void *ctx,uint32_t table,int32_t key,uint32_t packet)
{ EmArea01Call c={.function=0x001CB760u,.na=3,.a={sx(table),sx(key),packet}};return forward(ctx,&c); }
static int packet_worker(void *ctx,uint32_t table,int32_t key,int32_t qwc,uint32_t *out)
{
    EmArea01Call c={.function=0x001CB5F0u,.na=3,.a={sx(table),sx(key),sx(qwc)}};
    int rc=forward(ctx,&c);*out=(uint32_t)c.v0;return rc;
}
static int reference_worker(void *ctx,uint32_t table,int32_t key,int32_t qwc,uint32_t address)
{ EmArea01Call c={.function=0x001CB6B0u,.na=4,.a={sx(table),sx(key),sx(qwc),sx(address)}};return forward(ctx,&c); }
static int register_worker(void *ctx,uint32_t table,int32_t key,uint64_t word)
{ EmArea01Call c={.function=0x001CB950u,.na=3,.a={sx(table),sx(key),word}};return forward(ctx,&c); }
static int fog_worker(void *ctx,int32_t mode,uint32_t scale,uint32_t bias)
{
    EmArea01Call c={.function=0x0021B9A0u,.na=1,.nf=2,.a={sx(mode)},.f={scale,bias}};
    return forward(ctx,&c);
}
static int glow_worker(void *ctx,uint32_t position,uint32_t colour)
{
    EmArea01Call c={.function=0x001F4BF0u,.na=2,.a={sx(position),sx(colour)}};
    return forward(ctx,&c);
}
static uint32_t float_bits(float f){uint32_t u;memcpy(&u,&f,4);return u;}
static int sprite_worker(void *ctx,int32_t bucket,int32_t mode,uint32_t position,uint64_t tex0,
                         uint32_t rgb,float w,float h,float bias)
{
    EmArea01Call c={.function=0x001CD520u,.na=5,.nf=3,
        .a={sx(bucket),sx(mode),sx(position),tex0,sx(rgb)},
        .f={float_bits(w),float_bits(h),float_bits(bias)}};
    return forward(ctx,&c);
}
static void *sprite_memory(void *ctx,uint32_t address,size_t size,int write)
{
    const EmArea01RuntimeHost *h=ctx;
    return size<=UINT32_MAX?h->bytes(h->ctx,address,(uint32_t)size,write):NULL;
}
int em_area01_flame_services_handles(uint32_t fn)
{ return fn==0x001CD070u || fn==0x001CD2B0u || fn==0x001CFAE0u || fn==0x001F4A10u || fn==0x001E9E60u || fn==0x001F4CC0u || fn==0x001F4BF0u || fn==0x001CD520u; }
int em_area01_flame_services_call(const EmArea01RuntimeHost *h,EmArea01Call *c,uint32_t *fault)
{
    if(!c)return fail(fault,0x001E3D90u);
    if(!em_area01_flame_services_handles(c->function))return 1;
    if(!h || !h->bytes)return fail(fault,c->function);
    if(c->function==0x001CFAE0u) {
        if(c->na!=3 || c->nf!=4)return fail(fault,c->function);
        return em_weather_packets_001CFAE0_view(h->ctx,h->bytes,(uint32_t)c->a[0],
            (int32_t)c->a[1],(uint32_t)c->a[2],c->f,fault);
    }
    if(c->function==0x001CD520u) {
        if(c->na!=5 || c->nf!=3)return fail(fault,c->function);
        EmAimFireTargetCall target={.function=c->function,.sp=c->sp,.na=c->na,.nf=c->nf};
        memcpy(target.a,c->a,c->na*sizeof c->a[0]);memcpy(target.f,c->f,c->nf*sizeof c->f[0]);
        int rc=em_aim_fire_runtime_sprite_borrow(&target,(void *)h,sprite_memory,fault);
        c->v0=target.v0;c->f0=target.f0;return rc;
    }
    Frame frame={.host=h};
    if(c->function==0x001F4BF0u) {
        if(c->na!=2 || c->nf || c->sp<0x40u)return fail(fault,c->function);
        frame.sp=c->sp-0x40u;
        const uint8_t *p=h->bytes(h->ctx,(uint32_t)c->a[1],16,0);
        if(!p)return fail(fault,(uint32_t)c->a[1]);
        uint32_t colour[4];memcpy(colour,p,16);
        EmStatusSceneWorkers w={.ctx=&frame,.w_00122BB8=random_worker,.w_001CD520=sprite_worker};
        EmStatusSceneFault f={0};
        int rc=em_status_scene_glow_001F4BF0((uint32_t)c->a[0],colour,&w,&f);
        if(rc<0)return fail(fault,frame.fault?frame.fault:f.address);
        return 0;
    }
    if(c->function==0x001E9E60u) {
        if(c->na!=2 || c->nf || c->sp<0xC0u)return fail(fault,c->function);
        frame.sp=c->sp-0xC0u;
        EmArea01RenderHud s={0};s.core.world.view=h->bytes;s.core.world.view_ctx=h->ctx;
        s.workers.ctx=&frame;s.workers.w_001CB5F0=packet_worker;s.workers.w_001CB950=register_worker;
        s.workers.w_001CB6B0=reference_worker;s.workers.w_001CB760=page_worker;
        int rc=em_area01_render_001E9E60(&s,(uint32_t)c->a[0],(int32_t)c->a[1]);
        if(rc<0)return fail(fault,frame.fault?frame.fault:s.core.fault.detail?s.core.fault.detail:s.core.fault.address);
        return 0;
    }
    EmArea01RenderGs s={0};
    s.core.world.view=h->bytes;s.core.world.view_ctx=h->ctx;s.workers.w_001281C0=convert;
    uint32_t result=0;int rc;
    if(c->function==0x001F4CC0u) {
        if(c->na!=2 || c->nf || c->sp<0x30u)return fail(fault,c->function);
        frame.sp=c->sp-0x30u;s.workers.ctx=&frame;
        s.workers.w_0021B9A0=fog_worker;s.workers.w_001F4BF0=glow_worker;
        rc=em_area01_render_001F4CC0(&s,(uint32_t)c->a[0],(uint32_t)c->a[1]);
    } else if(c->function==0x001F4A10u) {
        if(c->na!=2 || c->nf || c->sp<0x90u)return fail(fault,c->function);
        frame.sp=c->sp-0x90u;s.workers.ctx=&frame;
        s.workers.w_00122BB8=random_worker;s.workers.w_001C7900=vector_worker;
        s.workers.w_001C6120=lookup_worker;s.workers.w_001D3990=resource_worker;s.workers.w_001CB760=page_worker;
        rc=em_area01_render_001F4A10(&s,(uint32_t)c->a[0],(uint32_t)c->a[1]);
    } else if(c->function==0x001CD070u) {
        if(c->na!=2 || c->nf)return fail(fault,c->function);
        rc=em_area01_render_001CD070(&s,(uint32_t)c->a[0],(uint32_t)c->a[1],&result);
        if(rc==0)c->v0=(uint64_t)(int64_t)(int32_t)result;
    } else {
        if(c->na || c->nf!=4)return fail(fault,c->function);
        rc=em_area01_render_001CD2B0(&s,c->f[0],c->f[1],c->f[2],c->f[3],&result);
        if(rc==0)c->f0=result;
    }
    if(rc<0)return fail(fault,frame.fault?frame.fault:s.core.fault.detail?s.core.fault.detail:s.core.fault.address);
    return 0;
}
int em_area01_flame_packet_prepare(const EmArea01RuntimeHost *h,const EmArea01Call *c,
                                   EmArea01FlamePacket *p,uint32_t *fault)
{
    if(!c)return fail(fault,0x001CFBE0u);
    if(c->function!=0x001CFBE0u)return 1;
    if(!h || !h->bytes || !p || c->na!=5 || c->nf)return fail(fault,c->function);
    uint32_t source=(uint32_t)c->a[2],xf=(uint32_t)c->a[3];
    const uint8_t *block=h->bytes(h->ctx,xf,0x58,0);
    if(!block)return fail(fault,xf);
    memcpy(p->transform,block,0x58);
    const uint8_t *descriptor=h->bytes(h->ctx,source,0x90,0);
    if(!descriptor)return fail(fault,source);
    memcpy(p->descriptor,descriptor,0x90);
    p->key=(int32_t)c->a[0];p->kind=(int32_t)c->a[1];p->copy=(int32_t)c->a[4];p->source=source;
    return 0;
}
int em_area01_flame_packet_invoke(const EmArea01FlamePacket *p,EmArea01Call *c)
{
    if(!p || !c || c->function!=0x001CFBE0u)return -1;
    return em_effects_live_001CFBE0_bytes(p->key,p->kind,p->source,p->descriptor,p->transform,p->copy);
}
