#include "game/em_area01_light_live.h"
#include "game/em_area00_fx.h"
#include "game/em_area00_world.h"
#include "game/em_owner_draw_live.h"
#include "game/em_point_light.h"
#include "game/em_render_context_live.h"
#include <string.h>

typedef struct { const EmArea01RuntimeHost *host; uint32_t fault; } Frame;
static int fail(Frame *f, uint32_t address)
{ if (!f->fault) f->fault=address; return -1; }
static uint8_t *memory(void *ctx, uint32_t a, uint32_t n, int write)
{
    Frame *f=ctx;
    uint8_t *p=f->fault ? NULL : f->host->bytes(f->host->ctx,a,n,write);
    if (!p) fail(f,a);
    return p;
}
static int forward(Frame *f, EmArea01Call *c)
{
    if (f->fault) return -1;
    return f->host->worker(f->host->ctx,c)<0 ? fail(f,c->function) : 0;
}
static int world_worker(void *ctx, EmArea00WorldCall *w)
{
    EmArea01Call c={.function=w->fn,.sp=w->sp,.na=w->na,.nf=w->nf};
    memcpy(c.a,w->a,sizeof w->a); memcpy(c.f,w->f,sizeof w->f);
    int rc=forward(ctx,&c); w->v0=c.v0; w->f0=c.f0; return rc;
}
static int fx_worker(void *ctx, uint32_t fn, uint32_t sp, const EmArea00FxRegs *r,
                     uint64_t *v0, uint32_t *f0)
{
    Frame *f=ctx;
    EmArea01Call c={.function=fn,.sp=sp};
    if ((r->imask & ~0xFF0u) || (r->fmask & ~0xFF000u)) return fail(f,fn);
    for (unsigned i=0;i<8;++i) {
        if (r->imask & (1u<<(4+i))) { c.a[i]=r->r[4+i]; c.na=i+1; }
        if (r->fmask & (1u<<(12+i))) { c.f[i]=r->f[12+i]; c.nf=i+1; }
    }
    if (r->imask != (((1u<<c.na)-1u)<<4) ||
        r->fmask != (((1u<<c.nf)-1u)<<12)) return fail(f,fn);
    int rc=forward(f,&c); *v0=c.v0; *f0=c.f0; return rc;
}
int em_area01_light_handles(uint32_t fn)
{ return fn==0x001F5490u || fn==0x001C5050u || fn==0x001F5F60u; }
int em_area01_light_call(const EmArea01RuntimeHost *host, EmArea01Call *c, uint32_t *fault)
{
    if (!host || !host->bytes || !host->worker || !c || !fault) return -1;
    if (*fault) return -1;
    unsigned na=c->function==0x001F5F60u ? 4u : 1u;
    unsigned nf=c->function==0x001C5050u ? 1u : 0u;
    uint32_t frame=c->function==0x001F5F60u ? 0xA0u : c->function==0x001C5050u ? 0x40u : 0x20u;
    if (!em_area01_light_handles(c->function) || c->na!=na || c->nf!=nf || c->sp<frame) {
        *fault=c->function; return -1;
    }
    Frame f={.host=host}; int rc;
    if (c->function==0x001C5050u) {
        /* This owner's only direct memory access is its local vector's w.
         * The stack storage remains stable across nested native boundaries. */
        EmArea00WorldRegion local={c->sp-4u,4,memory(&f,c->sp-4u,4,1)};
        EmArea00World w={.regions=&local,.region_count=1,.call=world_worker,.ctx=&f,.sp=c->sp};
        rc=f.fault ? -1 : em_area00_world_001C5050(&w,(uint32_t)c->a[0],c->f[0],&c->v0);
        if (rc<0 && !f.fault) fail(&f,w.fault_address);
    } else {
        EmArea00Fx x={0}; x.core.world.view=memory; x.core.world.view_ctx=&f;
        x.call=fx_worker; x.ctx=&f; x.sp=c->sp;
        rc=c->function==0x001F5490u ? em_area00_fx_001F5490(&x,c->a[0],&c->v0)
            : em_area00_fx_001F5F60(&x,c->a[0],c->a[1],c->a[2],c->a[3]);
        if (rc<0 && !f.fault) fail(&f,x.core.fault.code==EM_A01R_FAULT_BAD_ADDRESS
                                             ? x.core.fault.detail : x.core.fault.address);
    }
    if (rc<0) { *fault=f.fault ? f.fault : c->function; return -1; }
    return 0;
}
static int copy(const EmArea01RuntimeHost *h, uint32_t a, void *out, uint32_t n)
{
    const uint8_t *p=h && h->bytes ? h->bytes(h->ctx,a,n,0) : NULL;
    if (!p) return -1;
    memcpy(out,p,n); return 0;
}
int em_area01_light_service_prepare(const EmArea01RuntimeHost *h, const EmArea01Call *c,
                                    EmArea01LightService *s)
{
    if (!c || !s) return -1;
    if (c->function!=0x001D7FA0u && c->function!=0x001D80B0u &&
        c->function!=0x001D3990u && c->function!=0x001CAAC0u) return 1;
    memset(s,0,sizeof *s); s->function=c->function; s->argument=(uint32_t)c->a[0];
    if (c->function==0x001D7FA0u) {
        if (c->na!=3 || c->nf!=2 || ((c->a[0]|c->a[1])&15u) ||
            copy(h,(uint32_t)c->a[0],s->position,16)<0 || copy(h,(uint32_t)c->a[1],s->color,16)<0)
            return -1;
        s->argument=(uint32_t)c->a[2]; s->multiplier=c->f[0]; s->adder=c->f[1];
    } else if (c->function==0x001CAAC0u) {
        if (c->na!=3 || c->nf || (c->a[0]&15u) || (uint32_t)c->a[2]!=EM_RCL_CONTEXT ||
            copy(h,(uint32_t)c->a[0],s->position,16)<0) return -1;
        s->argument=(uint32_t)c->a[1];
    } else if (c->na!=1 || c->nf) return -1;
    else if (c->function==0x001D3990u &&
             copy(h,(uint32_t)c->a[0]+4u,&s->word,4)<0) return -1;
    return 0;
}
static const uint8_t *resource(void *ctx, uint32_t a, uint32_t n)
{
    const EmArea01RuntimeHost *h=ctx;
    const uint8_t *p=em_rcl_bytes(a,n);
    return p ? p : h && h->bytes ? h->bytes(h->ctx,a,n,0) : NULL;
}
int em_area01_light_service_invoke(const EmArea01RuntimeHost *h, const EmArea01LightService *s,
                                   EmArea01Call *c)
{
    if (!s || !c || s->function!=c->function) return -1;
    switch(s->function) {
    case 0x001D7FA0u: {
        EmPointLightPool *pool=em_rcl_point_lights(); float mult,add;
        if (!pool || em_rcl_fault()) return -1;
        memcpy(&mult,&s->multiplier,4); memcpy(&add,&s->adder,4);
        int32_t handle=em_point_light_register(pool,s->position,s->color,(int32_t)s->argument,mult,add);
        c->v0=(uint64_t)(int64_t)handle; return 0;
    }
    case 0x001D80B0u: return em_rcl_001D80B0((int32_t)s->argument);
    case 0x001D3990u: return em_owner_draw_live_001D3990_at(s->argument,s->word);
    case 0x001CAAC0u: return em_owner_draw_live_001CAAC0_packet((const uint32_t *)(const void *)s->position,
                                                              s->argument,resource,(void *)h);
    default: return -1;
    }
}
