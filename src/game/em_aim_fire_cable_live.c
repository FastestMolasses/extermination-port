#include "game/em_aim_fire_cable_live.h"
#include "game/em_security_gun_rest.h"
#include "game/em_area06_port.h"
#include "game/em_effects_live.h"
#include "game/em_effect_original.h"
#include "game/em_owner_services_original.h"
#include "game/em_coll_probe_original.h"
#include "game/em_sdk_vu0.h"
#include "game/em_ee_float.h"
#include <string.h>

#define TRY(x) do { if((x)<0)return -1; } while(0)
typedef struct {EmAimFireLive *live;EmGunRestMem mem;EmGunRestWorkers workers;EmGunFault fault;} Cable;
static const uint8_t *load(void *ctx,uint32_t a,uint32_t n)
{ return em_aim_fire_live_map(((Cable *)ctx)->live,a,n,0); }
static uint8_t *store(void *ctx,uint32_t a,uint32_t n)
{ return em_aim_fire_live_map(((Cable *)ctx)->live,a,n,1); }
static int invoke(EmAimFireLive *h,uint32_t fn,unsigned na,const uint64_t *a,
                  unsigned nf,const uint32_t *f,EmAimFireTargetCall *result)
{
    EmAimFireTargetCall c={0};
    if(na>sizeof c.a/sizeof c.a[0]||nf>sizeof c.f/sizeof c.f[0])return -1;
    c.function=fn;c.na=na;c.nf=nf;
    if(na)memcpy(c.a,a,na*sizeof *a);
    if(nf)memcpy(c.f,f,nf*sizeof *f);
    TRY(em_aim_fire_live_call(h,&c));if(result)*result=c;return 0;
}
static int free_node(void *ctx,uint32_t node)
{ uint64_t a[]={node};return invoke(((Cable*)ctx)->live,0x1AFC10,1,a,0,NULL,NULL); }
static int random_word(void *ctx,int32_t *out)
{ EmAimFireTargetCall c;TRY(invoke(((Cable*)ctx)->live,0x122BB8,0,NULL,0,NULL,&c));*out=(int32_t)c.v0;return 0; }
static int sine(void *ctx,uint32_t bits,uint32_t *out)
{ EmAimFireTargetCall c;TRY(invoke(((Cable*)ctx)->live,0x11E2A8,0,NULL,1,&bits,&c));*out=c.f0;return 0; }
static int to_int(void *ctx,uint32_t bits,int32_t *out)
{ EmAimFireTargetCall c;TRY(invoke(((Cable*)ctx)->live,0x1281C0,0,NULL,1,&bits,&c));*out=(int32_t)c.v0;return 0; }
static int copy4(void *ctx,uint8_t *d,const uint8_t *s)
{ (void)ctx;em_sdk_vu0_00102948(d,s);return 0; }
static int copy16(void *ctx,uint8_t *d,const uint8_t *s)
{ (void)ctx;em_owner_services_copy_qw4_00102958((float *)(void *)d,(const float *)(const void *)s);return 0; }
static int identity(void *ctx,uint8_t *d)
{ (void)ctx;float m[16];if(em_owner_services_identity_001029C0(m)!=0)return -1;memcpy(d,m,64);return 0; }
static int rotate(void *ctx,uint8_t *d,const uint8_t *s,uint32_t angle)
{ (void)ctx;float m[16],out[16];memcpy(m,s,64);if(em_owner_services_rotate_x_00102B08(out,m,angle)!=0)return -1;memcpy(d,out,64);return 0; }
static int translate(void *ctx,uint8_t *d,const uint8_t *s,const uint8_t *v)
{ (void)ctx;float m[16],p[3],out[16];memcpy(m,s,64);memcpy(p,v,12);if(em_owner_services_translate_00102918(out,m,p)!=0)return -1;memcpy(d,out,64);return 0; }
static int transform(void *ctx,uint8_t *d,const uint8_t *s,const uint8_t *v)
{ (void)ctx;float m[16],p[4],out[4];memcpy(m,s,64);memcpy(p,v,16);em_effect_original_001026A0(out,m,p);memcpy(d,out,16);return 0; }
static int add(void *ctx,uint8_t *d,const uint8_t *a,const uint8_t *b)
{ (void)ctx;float x[4],y[4],out[4];memcpy(x,a,16);memcpy(y,b,16);TRY(em_coll_probe_sdk_add(out,x,y));memcpy(d,out,16);return 0; }
static int scale(void *ctx,uint8_t *d,const uint8_t *s,uint32_t bits)
{ (void)ctx;float x[4],out[4];memcpy(x,s,16);TRY(em_coll_probe_sdk_scale(out,x,em_ee_float(bits)));memcpy(d,out,16);return 0; }
static int depth(void *ctx,const uint8_t *p,int32_t *key)
{ (void)ctx;float v[4];memcpy(v,p,16);return em_effects_live_001CCF70(v,key); }
static int block(void *ctx,uint8_t *d,const uint8_t *p,uint32_t a,uint32_t b)
{ (void)ctx;float m[16];memcpy(m,p,64);return em_effects_live_001CFA60(d,m,a,b); }
static int block5(void *ctx,uint8_t *d,int32_t mode,const uint8_t *p,uint32_t a,uint32_t b,uint32_t c,uint32_t e,uint32_t f)
{ (void)ctx;float m[16];const uint32_t bits[]={a,b,c,e,f};memcpy(m,p,64);return em_effects_live_001CFB50(d,mode,m,bits); }
static int chain(void *ctx,int32_t key,int32_t kind,uint32_t source,const uint8_t *p,int32_t copy)
{ (void)ctx;return em_effects_live_001CFBE0(key,kind,source,p,copy); }
static int spawn(void *ctx,uint32_t id,const uint8_t *p,uint32_t bits,uint32_t *out)
{ (void)ctx;float point[4];if(p)memcpy(point,p,16);return em_effects_live_001EF9D0(id,p?point:NULL,bits,out); }
static int spawn_matrix(void *ctx,uint32_t id,const uint8_t *m,uint32_t *out)
{ Cable *c=ctx;return em_gun_rest_001EFEB0(id,m,&c->mem,&c->workers,out,&c->fault); }
static int sprite(void *ctx,int32_t a,int32_t b,const uint8_t *point,uint64_t tag,uint32_t rgb,
                  uint32_t width,uint32_t height,uint32_t z)
{
    Cable *c=ctx;EmAimFireLive *h=c->live;
    /* A native caller-local point takes one private call slot. It is not a
     * game global; nested callees allocate their own subsequent slot. */
    const size_t at=(size_t)h->depth*0x400u;
    if(at+16>sizeof h->temporary)return -1;
    memcpy(h->temporary+at,point,16);
    uint64_t args[]={ (uint32_t)a,(uint32_t)b,0x7F001000u+(uint32_t)at,tag,rgb };
    uint32_t floats[]={width,height,z};
    return invoke(h,0x1CD520,5,args,3,floats,NULL);
}

/* 001CE860 takes original pointers; every nested call uses the same live bus. */
static uint8_t *strip_bytes(void *ctx,uint32_t a,uint32_t n)
{ return em_aim_fire_live_map(ctx,a,n,1); }
static int strip_sqrt(void *ctx,float f,float *out)
{ EmAimFireTargetCall c;uint32_t bits=em_ee_bits(f);TRY(invoke(ctx,0x11E748,0,NULL,1,&bits,&c));*out=em_ee_float(c.f0);return 0; }
static int strip_int(void *ctx,float f,int32_t *out)
{ EmAimFireTargetCall c;uint32_t bits=em_ee_bits(f);TRY(invoke(ctx,0x1281C0,0,NULL,1,&bits,&c));*out=(int32_t)c.v0;return 0; }
static int strip_transform(void *ctx,uint32_t a,uint32_t b,uint32_t c)
{ uint64_t args[]={a,b,c};return invoke(ctx,0x1026A0,3,args,0,NULL,NULL); }
static int strip_matrix(void *ctx,int32_t mode,uint32_t *out)
{ EmAimFireTargetCall c;uint64_t a[]={(uint32_t)mode};TRY(invoke(ctx,0x1CD370,1,a,0,NULL,&c));*out=(uint32_t)c.v0;return 0; }
static int strip_depth(void *ctx,uint32_t a,int32_t *out)
{ float v[4];const void *p=em_aim_fire_live_map(ctx,a,16,0);if(!p)return -1;memcpy(v,p,16);return em_effects_live_001CCF70(v,out); }
static int strip_open(void *ctx,uint32_t table,int32_t key,int32_t count,uint32_t *out)
{ EmAimFireTargetCall c;uint64_t a[]={table,(uint32_t)key,(uint32_t)count};TRY(invoke(ctx,0x1CB5F0,3,a,0,NULL,&c));*out=(uint32_t)c.v0;return 0; }
static int strip_close(void *ctx,uint32_t table,int32_t key,int32_t mode)
{ uint64_t a[]={table,(uint32_t)key,(uint32_t)mode};return invoke(ctx,0x1CB900,3,a,0,NULL,NULL); }
static int strip(EmAimFireLive *h,EmAimFireTargetCall *f)
{
    EmArea06PortHooks hooks={0};EmArea06PortFault fault={0};
    hooks.ctx=h;hooks.bytes=strip_bytes;hooks.w_0011E748=strip_sqrt;hooks.w_001281C0=strip_int;
    hooks.w_001026A0=strip_transform;hooks.w_001CD370=strip_matrix;hooks.w_001CCF70=strip_depth;
    hooks.w_001CB5F0=strip_open;hooks.w_001CB900=strip_close;
    int status=em_area06_port_001CE860(&hooks,(int32_t)f->a[0],(int32_t)f->a[1],(uint32_t)f->a[2],
        (uint32_t)f->a[3],(int32_t)f->a[4],f->a[5],em_ee_float(f->f[0]),f->sp,&fault);
    if(status<0&&!h->fault_function)h->fault_function=fault.address?fault.address:0x1CE860;
    return status;
}
static int draw_strip(void *ctx,int32_t a,int32_t b,const uint8_t *p,const uint8_t *colour,
                      uint32_t width,int32_t count,uint64_t tag)
{
    Cable *c=ctx;EmAimFireLive *h=c->live;
    /* These are precisely the buffers 0021A500 passes to this worker. */
    if(p!=load(ctx,0x821400,(uint32_t)(count>0?count:1)*16u)||colour!=load(ctx,0x700038B0,16))return -1;
    EmAimFireTargetCall frame={.function=0x1CE860,.na=6,.nf=1};
    frame.a[0]=(uint32_t)a;frame.a[1]=(uint32_t)b;frame.a[2]=0x821400;
    frame.a[3]=0x700038B0;frame.a[4]=(uint32_t)count;frame.a[5]=tag;frame.f[0]=width;
    return em_aim_fire_live_call(h,&frame);
}
static void init(Cable *c,EmAimFireLive *h)
{
    memset(c,0,sizeof *c);c->live=h;c->mem=(EmGunRestMem){c,load,store};
    EmGunRestWorkers *w=&c->workers;w->ctx=c;
    w->w_001AFC10=free_node;w->w_00122BB8=random_word;w->w_0011E2A8=sine;w->w_001281C0=to_int;
    w->w_00102948=copy4;w->w_00102958=copy16;w->w_001029C0=identity;w->w_00102B08=rotate;
    w->w_00102918=translate;w->w_001026A0=transform;w->w_001028B8=add;w->w_00103230=scale;
    w->w_001CCF70=depth;w->w_001CFA60=block;w->w_001CFB50=block5;w->w_001CFBE0=chain;
    w->w_001EF9D0=spawn;w->w_001EFEB0=spawn_matrix;w->w_001CE860=draw_strip;w->w_001CD520=sprite;
}
int em_aim_fire_cable_live_tick(EmAimFireLive *h,uint32_t node,uint32_t callback)
{
    if(!h||h->fault_function||h->fault_address)return -1;
    Cable c;init(&c,h);int status;
    if(callback==0x21AAC0)status=em_gun_rest_0021AAC0(node,&c.mem,&c.workers,&c.fault);
    else if(callback==0x21A500)status=em_gun_rest_0021A500(node,&c.mem,&c.workers,&c.fault);
    else return -1;
    if(status<0&&!h->fault_function)h->fault_function=c.fault.address?c.fault.address:callback;
    return status;
}
int em_aim_fire_cable_live_call(EmAimFireLive *h,EmAimFireTargetCall *f)
{
    if(!h||!f||h->fault_function||h->fault_address)return -1;
    if(f->function==0x1CE860)return strip(h,f);
    if(f->function==0x1EFEB0) {
        Cable c;init(&c,h);uint32_t node;
        const uint8_t *m=load(&c,(uint32_t)f->a[1],64);if(!m)return -1;
        int status=spawn_matrix(&c,(uint32_t)f->a[0],m,&node);
        if(status<0){if(!h->fault_function)h->fault_function=c.fault.address?c.fault.address:f->function;return -1;}
        f->v0=node;return 0;
    }
    return -1;
}
