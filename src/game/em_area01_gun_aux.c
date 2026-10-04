#include "game/em_area01_gun_aux.h"
#include "game/em_area01_revisit.h"
#include "game/em_area02_misc.h"
#include "game/em_ee_float.h"
#include <string.h>
typedef struct {
    const EmArea01RuntimeHost *host;
    EmSdkMathContext *sdk;
    uint32_t sp,fault;
} Aux;
static int fail(Aux *s,uint32_t a)
{ if(!s->fault)s->fault=a;return -1; }
static uint8_t *view(void *ctx,uint32_t a,uint32_t n,int write)
{
    Aux *s=ctx;if(s->fault)return NULL;
    if(a>=0x0026C5D0u && (uint64_t)a+n<=0x0026C5D4u && s->sdk) {
        if(!write && s->sdk->world.d26C5D0)
            return (uint8_t *)(void *)s->sdk->world.d26C5D0+a-0x0026C5D0u;
        fail(s,a);return NULL;
    }
    uint8_t *p=s->host->bytes(s->host->ctx,a,n,write);
    if(!p)fail(s,a);return p;
}
static uint8_t *revisit_view(void *ctx,uint32_t a,uint32_t n)
{ return view(ctx,a,n,0); }
static int invoke(Aux *s,EmArea01Call *c)
{
    if(s->fault)return -1;
    if(s->host->worker(s->host->ctx,c)<0)return fail(s,c->function);
    return 0;
}
static uint64_t reg(uint32_t v){return (uint64_t)(int64_t)(int32_t)v;}
static int triple(Aux *s,uint32_t fn,uint32_t a,uint32_t b,uint32_t d)
{
    EmArea01Call c={.function=fn,.sp=s->sp,.na=3,.a={reg(a),reg(b),reg(d)}};
    return invoke(s,&c);
}
#define TRIPLE(name) static int w_##name(void *ctx,uint32_t a,uint32_t b,uint32_t c) \
{ return triple(ctx,0x##name##u,a,b,c); }
TRIPLE(001026A0) TRIPLE(001028B8) TRIPLE(001028D0)
#undef TRIPLE
#define PAIR(name) static int w_##name(void *ctx,uint32_t a,uint32_t b) \
{ Aux *s=ctx;EmArea01Call c={.function=0x##name##u,.sp=s->sp,.na=2,.a={reg(a),reg(b)}}; \
  return invoke(s,&c); }
PAIR(00102948) PAIR(001031E0)
#undef PAIR
static int dot(void *ctx,uint32_t a,uint32_t b,float *result)
{
    Aux *s=ctx;EmArea01Call c={.function=0x00102738u,.sp=s->sp,.na=2,.a={reg(a),reg(b)}};
    int rc=invoke(s,&c);*result=em_ee_float(c.f0);return rc;
}
static int ray(void *ctx,uint32_t a,uint32_t b,int32_t c,int32_t d,int32_t *result)
{
    Aux *s=ctx;EmArea01Call call={.function=0x0019A570u,.sp=s->sp,.na=4,
        .a={reg(a),reg(b),reg((uint32_t)c),reg((uint32_t)d)}};
    int rc=invoke(s,&call);*result=(int32_t)call.v0;return rc;
}
static int world_ray(void *ctx,uint32_t a,uint32_t b,int32_t c,int32_t *result)
{
    Aux *s=ctx;EmArea01Call call={.function=0x0019AA80u,.sp=s->sp,.na=3,
        .a={reg(a),reg(b),reg((uint32_t)c)}};
    int rc=invoke(s,&call);*result=(int32_t)call.v0;return rc;
}
static int random_value(void *ctx,int32_t *result)
{
    Aux *s=ctx;EmArea01Call c={.function=0x00122BB8u,.sp=s->sp};
    int rc=invoke(s,&c);*result=(int32_t)c.v0;return rc;
}
static int sprite(void *ctx,int32_t a,int32_t b,uint32_t p,uint64_t tag,uint32_t rgba,
                  float x,float y,float z,int32_t *result)
{
    Aux *s=ctx;EmArea01Call c={.function=0x001CD520u,.sp=s->sp,.na=5,.nf=3,
        .a={reg((uint32_t)a),reg((uint32_t)b),reg(p),tag,reg(rgba)},
        .f={em_ee_bits(x),em_ee_bits(y),em_ee_bits(z)}};
    int rc=invoke(s,&c);*result=(int32_t)c.v0;return rc;
}
static int beam(void *ctx,uint32_t a,uint32_t b,uint32_t color,float length)
{
    Aux *s=ctx;EmArea01Call c={.function=0x001E2BA0u,.sp=s->sp,.na=3,.nf=1,
        .a={reg(a),reg(b),reg(color)},.f={em_ee_bits(length)}};
    return invoke(s,&c);
}
static int misc_call(void *ctx,EmArea02MiscCall *in)
{
    Aux *s=ctx;EmArea01Call c={.function=in->fn,.sp=in->sp,.na=in->na,.nf=in->nf};
    memcpy(c.a,in->a,sizeof c.a);memcpy(c.f,in->f,sizeof c.f);
    if(s->sdk && in->nf==1) {
        float x=em_ee_float(in->f[0]);
        if(in->fn==0x0011DF78u)in->f0=em_ee_bits(em_sdk_math_original_0011DF78(x));
        else if(in->fn==0x0011CB90u)in->f0=em_ee_bits(em_sdk_math_original_0011CB90(x));
        else if(in->fn==0x0011E080u)in->v0=reg((uint32_t)em_sdk_math_original_0011E080(x));
        else goto external;
        return 0;
    }
external:
    if(invoke(s,&c)<0)return -1;
    in->v0=c.v0;in->f0=c.f0;return 0;
}
/* Runtime 8287C0, link name 828780. The byte-matched original first
 * allocates C, then copies 82CB30 to a stack QW before the three math calls.
 * Those workers consume canonical addresses and preserve their alias order.
 * The child callback is published last, through the actor rebind boundary. */
static int spawn(Aux *s,uint32_t matrix,uint32_t entry_sp)
{
    EmArea01Call c={.function=0x001AFA90u,.sp=s->sp,.na=1,.a={0xC}};
    if(invoke(s,&c)<0)return -1;
    uint32_t child=(uint32_t)c.v0;if(!child)return 0;
    uint8_t vec[16];const uint8_t *p=view(s,0x0082CB30u,16,0);
    if(!p)return -1;memcpy(vec,p,16);
    uint8_t *dst=view(s,entry_sp-16,16,1);if(!dst)return -1;memcpy(dst,vec,16);
    c=(EmArea01Call){.function=0x00102948u,.sp=s->sp,.na=2,
        .a={reg(child+0xB0),reg(matrix+0x30)}};
    if(invoke(s,&c)<0)return -1;
    c=(EmArea01Call){.function=0x00102958u,.sp=s->sp,.na=2,
        .a={reg(child+0xD0),reg(matrix)}};
    if(invoke(s,&c)<0 || triple(s,0x001026A0u,child+0x100,child+0xD0,entry_sp-16)<0)return -1;
    dst=view(s,child+0x10,4,1);if(!dst)return -1;
    uint32_t callback=0x001F5040u;memcpy(dst,&callback,4);return 0;
}
int em_area01_gun_aux_handles(uint32_t fn)
{ return fn==0x008282F0u || fn==0x008287C0u || fn==0x0011E520u; }
int em_area01_gun_aux_call(const EmArea01RuntimeHost *h,EmSdkMathContext *sdk,EmArea01Call *c,uint32_t *fault)
{
    if(!h || !h->bytes || !h->worker || !c || !fault || *fault)return -1;
    Aux s={.host=h,.sdk=sdk,.sp=c->sp-(c->function==0x008287C0u?0x40u:0x80u)};int rc=-1;
    if(c->function==0x008282F0u && c->na==2) {
        EmArea01RevisitHooks hooks={.ctx=&s,.bytes=revisit_view,
            .w_001026A0=w_001026A0,.w_001028B8=w_001028B8,.w_001028D0=w_001028D0,
            .w_00102738=dot,.w_00102948=w_00102948,.w_001031E0=w_001031E0,
            .w_0019AA80=world_ray,.w_0019A570=ray,.w_00122BB8=random_value,
            .w_001CD520=sprite,.w_001E2BA0=beam};
        EmArea01RevisitFault f={0};int32_t result=0;
        rc=em_area01_revisit_008282F0(&hooks,(uint32_t)c->a[0],(uint32_t)c->a[1],c->sp,&result,&f);
        if(rc<0)fail(&s,f.address);else c->v0=reg((uint32_t)result);
    } else if(c->function==0x008287C0u && c->na==1)rc=spawn(&s,(uint32_t)c->a[0],c->sp);
    else if(c->function==0x0011E520u && c->nf==1) {
        EmArea02Misc misc={.ctx=&s,.sp=c->sp,.call=misc_call};
        rc=em_area02_misc_view_0011E520(&misc,view,c->f[0],&c->f0);
        if(rc<0)fail(&s,misc.fault_address);
    }
    if(rc<0) { *fault=s.fault ? s.fault : c->function;return -1; }
    return 0;
}
