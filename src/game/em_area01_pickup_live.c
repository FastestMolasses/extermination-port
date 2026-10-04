#include "game/em_area01_pickup_live.h"
#include "game/em_pickup_owner.h"
#include "game/em_pickup_items_original.h"
#include "game/em_ee_float.h"
#include <string.h>

typedef struct {
    EmArea01Pickup *binding;
    EmPickupOwner owner;
    EmPickupState0 initial;
    uint32_t node,block,sp,child;
    int initializing,freed;
} Frame;
static int fail(EmArea01Pickup *p,uint32_t address)
{ if (p && !p->fault) { p->fault=1;p->fault_address=address; } return -1; }
static uint8_t *mem(Frame *f,uint32_t a,uint32_t n,int write)
{
    if (f->binding->fault) return NULL;
    uint8_t *p=n && (uint64_t)a+n<=UINT64_C(0x100000000) ?
        f->binding->host.bytes(f->binding->host.ctx,a,n,write) : NULL;
    if (!p) fail(f->binding,a);
    return p;
}
static uint32_t read_value(Frame *f,uint32_t a,unsigned n)
{
    const uint8_t *p=mem(f,a,n,0);uint32_t v=0;
    if (p) for (unsigned i=0;i<n;++i) v|=(uint32_t)p[i]<<(8u*i);
    return v;
}
static void write_value(Frame *f,uint32_t a,uint32_t v,unsigned n)
{
    uint8_t *p=mem(f,a,n,1);
    if (p) for (unsigned i=0;i<n;++i) p[i]=(uint8_t)(v>>(8u*i));
}
static int load(Frame *f)
{
    EmPickupOwner *o=&f->owner;EmPickupState0 *s=&f->initial;uint32_t a=f->node;
    o->status=s->status=(uint8_t)read_value(f,a,1);
    o->class_flags=(uint8_t)read_value(f,a+2,1);
    o->subtype=s->subtype=(uint8_t)read_value(f,a+3,1);
    o->lifecycle=(uint8_t)read_value(f,a+4,1);o->phase=(uint8_t)read_value(f,a+5,1);
    s->mode=(uint8_t)read_value(f,a+8,1);o->armed=(uint8_t)read_value(f,a+11,1);
    s->model=(uint8_t)read_value(f,a+0xD,1);
    o->item_type=s->flags2=(uint16_t)read_value(f,a+0x2E,2);
    o->uid=(uint8_t)read_value(f,a+0x9A,1);
    for (unsigned k=0;k<3;++k) {
        s->scale[k]=em_ee_float(read_value(f,a+0x60+4*k,4));
        s->color[k]=em_ee_float(read_value(f,a+0x80+4*k,4));
    }
    f->child=read_value(f,a+0x2EC,4);o->has_child=f->child!=0;
    return f->binding->fault ? -1 : 0;
}
static int store(Frame *f)
{
    if (f->freed) return 0;
    uint32_t a=f->node;
    if (f->initializing) {
        const EmPickupState0 *s=&f->initial;
        write_value(f,a,s->status,1);write_value(f,a+3,s->subtype,1);
        write_value(f,a+8,s->mode,1);write_value(f,a+0xD,s->model,1);
        write_value(f,a+0x2E,s->flags2,2);
        for (unsigned k=0;k<3;++k) {
            write_value(f,a+0x60+4*k,em_ee_bits(s->scale[k]),4);
            write_value(f,a+0x80+4*k,em_ee_bits(s->color[k]),4);
        }
    } else {
        const EmPickupOwner *o=&f->owner;
        write_value(f,a,o->status,1);write_value(f,a+2,o->class_flags,1);
        write_value(f,a+3,o->subtype,1);write_value(f,a+4,o->lifecycle,1);
        write_value(f,a+5,o->phase,1);write_value(f,a+11,o->armed,1);
        write_value(f,a+0x2E,o->item_type,2);write_value(f,a+0x9A,o->uid,1);
    }
    return f->binding->fault ? -1 : 0;
}
static int worker(Frame *f,EmArea01Call *c)
{
    c->sp=f->sp;
    if (store(f)<0) return -1;
    if (f->binding->host.worker(f->binding->host.ctx,c)<0) return fail(f->binding,c->function);
    return f->freed ? 0 : load(f);
}
static int unary(Frame *f,uint32_t function)
{
    EmArea01Call c={.function=function,.a={f->node},.na=1};
    return worker(f,&c)<0 ? -1 : 1;
}
static int bind_world(void *ctx,int32_t *result)
{
    Frame *f=ctx;EmArea01Call c={.function=0x001B0FD0u,.a={f->node},.na=1};
    if (worker(f,&c)<0) return -1;
    *result=(int32_t)c.v0;return 0;
}
static int bind_library(void *ctx,uint32_t a1,int32_t a2,int32_t a3,int32_t *result)
{
    Frame *f=ctx;EmArea01Call c={.function=0x001B1020u,
        .a={f->node,a1,(uint64_t)(int64_t)a2,(uint64_t)(int64_t)a3},.na=4};
    if (worker(f,&c)<0) return -1;
    *result=(int32_t)c.v0;return 0;
}
static int place(void *ctx) { return unary(ctx,0x001C6380u); }
static int aura_init(void *ctx,int16_t variant)
{
    Frame *f=ctx;
    write_value(f,f->node+0x30,0x00275488u,4);
    EmArea01Call c={.function=0x001F1110u,.a={f->node,(uint64_t)(int64_t)variant},.na=2};
    return worker(f,&c);
}
static int cell(void *ctx)
{
    Frame *f=ctx;
    uint32_t area=read_value(f,0x00810700u,1);
    uint32_t descriptor=area==0x10 ? 0x00275880u : 0x00275878u;
    write_value(f,f->node+0x30,descriptor,4);
    EmArea01Call c={.function=0x001A2370u,.a={f->node,f->node+0xD0u},.na=2};
    return worker(f,&c);
}
static int initialize(Frame *f)
{
    f->initializing=1;
    const EmPickupState0Hooks h={f,bind_world,bind_library,place,aura_init,cell};
    int class4=f->owner.callback==0x00219550u;
    int rc=class4 ? em_pickup_owner_00219550_state0(&f->initial,
        (uint8_t)read_value(f,0x00810C67u,1),&h) : em_pickup_owner_0015AC00(&f->initial,&h);
    if (rc<0 || f->binding->fault || store(f)<0) return -1;
    if (class4 && !rc) {
        const uint32_t params[4]={0,0x3F800000u,0,0x3E800000u};
        uint8_t *p=mem(f,0x700038A0u,16,1);if (!p) return -1;memcpy(p,params,16);
        EmArea01Call c={.function=0x001C5570u,.a={f->node,0x700038A0u,0x73,1},.na=4};
        if (worker(f,&c)<0) return -1;
        write_value(f,f->node+0x2EC,(uint32_t)c.v0,4);
    }
    return f->binding->fault ? -1 : rc;
}
static int script_start(void *ctx,uint32_t entry,uint16_t clip)
{
    Frame *f=ctx;
    if (clip) write_value(f,f->owner.callback==0x00219550u ? 0x002666B4u : 0x00248354u,clip,4);
    EmArea01Call c={.function=0x001BA1A0u,.a={f->block,entry},.na=2};
    return worker(f,&c)<0 ? -1 : 1;
}
static int script_tick(void *ctx)
{
    Frame *f=ctx;EmArea01Call c={.function=0x001BA1F0u,.a={f->node},.na=1};
    return worker(f,&c)<0 ? -1 : (uint32_t)c.v0!=0;
}
static int event(void *ctx,EmPickupOwnerEvent kind,uint32_t argument)
{
    Frame *f=ctx;EmArea01Call c={.a={f->node},.na=1};
    switch (kind) {
    case EM_PICKUP_OWNER_AURA:
        /* The script worker may have changed the original frame gate. */
        if (read_value(f,0x70003B92u,1)) return f->binding->fault ? -1 : 1;
        c.function=0x001F1180u;break;
    case EM_PICKUP_OWNER_PUBLISH:c.function=0x001B17A0u;break;
    case EM_PICKUP_OWNER_DRAW:c.function=read_value(f,f->node+0x4C,4);break;
    case EM_PICKUP_OWNER_TAKE_SOUND:
        c.function=0x001FBD50u;c.a[1]=argument;c.a[2]=0;c.na=3;c.f[0]=0x43960000u;c.nf=1;break;
    case EM_PICKUP_OWNER_PERSIST:c.function=0x001B1190u;c.a[0]=argument;break;
    case EM_PICKUP_OWNER_STOP_CHILD:
        if (store(f)<0) return -1;
        if (f->child) write_value(f,f->child+4,argument,1);
        return f->binding->fault ? -1 : 1;
    case EM_PICKUP_OWNER_FREE:
        if (store(f)<0) return -1;
        f->freed=1;c.function=0x001AFC10u;break;
    default:return -1;
    }
    if (!c.function || worker(f,&c)<0) return -1;
    return kind==EM_PICKUP_OWNER_PUBLISH ? (int)c.v0 : 1;
}
static int add_item(void *ctx,uint16_t type,int amount)
{
    Frame *f=ctx;EmArea01Call c={.function=0x001C40B0u,.a={type,(uint32_t)amount},.na=2};
    return worker(f,&c)<0 ? -1 : 1;
}
static int take(Frame *f)
{
    uint8_t maps[256]={0},keys[256]={0};
    unsigned type=f->owner.item_type,kind=f->owner.subtype;
    if (type>255) return fail(f->binding,0x001B6EA0u);
    if (kind==1) maps[type]=(uint8_t)read_value(f,0x00810CB8u+type,1);
    else if (kind) keys[type]=(uint8_t)read_value(f,0x00810CC3u+type,1);
    EmPickupStatusRequest request={(uint8_t)read_value(f,0x008106B0u,1),(uint8_t)read_value(f,0x008106B1u,1)};
    if (f->binding->fault || em_pickup_owner_take(&f->owner,maps,keys,&request,add_item,f)<0) return -1;
    if (kind==1) write_value(f,0x00810CB8u+type,maps[type],1);
    else if (kind) write_value(f,0x00810CC3u+type,keys[type],1);
    write_value(f,0x008106B0u,request.kind,1);write_value(f,0x008106B1u,request.index,1);
    return f->binding->fault ? -1 : 1;
}
typedef struct { Frame *frame;EmPickupAura state; } AuraFrame;
static int aura_transfer(AuraFrame *a,int store)
{
    uint8_t *p=mem(a->frame,a->frame->node+0x2D0u,16,store);
    if (!p) return -1;
    if (store) memcpy(p,&a->state,16);else memcpy(&a->state,p,16);
    return 0;
}
static int32_t aura_random(void *ctx)
{
    AuraFrame *a=ctx;Frame *f=a->frame;
    EmArea01Call c={.function=0x00122BB8u,.sp=f->sp};
    if (aura_transfer(a,1)<0 || f->binding->host.worker(f->binding->host.ctx,&c)<0 || aura_transfer(a,0)<0) {
        fail(f->binding,c.function);return -1;
    }
    return (int32_t)c.v0;
}
static int aura_draw(void *ctx,uint32_t record,uint32_t angle,uint32_t timer)
{
    AuraFrame *a=ctx;Frame *f=a->frame;EmArea01Pickup *p=f->binding;
    if (aura_transfer(a,1)<0 || !p->aura_draw ||
        p->aura_draw(p->aura_ctx,f->node,record,angle,timer)<0 || aura_transfer(a,0)<0)
        return fail(p,0x001F136Cu);
    return 0;
}
static int aura_call(Frame *f,EmArea01Call *c)
{
    AuraFrame a={.frame=f};
    if (aura_transfer(&a,0)<0) return -1;
    const EmPickupAuraWorkers workers={&a,aura_random,aura_draw};
    int rc;
    if (c->function==0x001F1110u) {
        if (c->na<2) return fail(f->binding,c->function);
        rc=em_pickup_aura_001F1110(&a.state,(int16_t)c->a[1],&workers);
    } else {
        float world[16],eye[4];uint8_t area=0;
        if (a.state.state==1) {
            const uint8_t *p=mem(f,f->node+0xD0u,64,0);if (!p) return -1;
            memcpy(world,p,64);
            if (a.state.variant==1 || a.state.variant==2 || a.state.variant==4) {
                p=mem(f,0x008105D0u,16,0);if (!p) return -1;memcpy(eye,p,16);
            }
            if (a.state.variant==1) area=(uint8_t)read_value(f,0x00810700u,1);
        }
        rc=em_pickup_aura_001F1180(&a.state,world,eye,area,&workers);
    }
    if (aura_transfer(&a,1)<0 || rc<0 || f->binding->fault) return fail(f->binding,c->function);
    c->v0=0;return 0;
}
void em_area01_pickup_set_aura_draw(EmArea01Pickup *p,EmArea01PickupAuraDraw draw,void *ctx)
{ if (p) { p->aura_draw=draw;p->aura_ctx=ctx; } }
int em_area01_pickup_handles(uint32_t function)
{
    return function==0x001F1110u || function==0x001F1180u || function==0x00219550u || function==0x0015AFA0u || function==0x0015AC00u ||
           function==0x0015AE20u || function==0x001B6EA0u;
}
int em_area01_pickup_bind(EmArea01Pickup *p,const EmArea01RuntimeHost *host)
{
    if (!p || !host || !host->bytes || !host->worker) return -1;
    memset(p,0,sizeof *p);p->host=*host;return 0;
}
int em_area01_pickup_call(EmArea01Pickup *p,EmArea01Call *c)
{
    if (!p || !c || p->fault) return -1;
    if (!em_area01_pickup_handles(c->function) || c->na<1) return fail(p,c->function);
    Frame f={.binding=p,.node=(uint32_t)c->a[0],.sp=c->sp};f.block=f.node+0x1F0u;
    if (c->function==0x001F1110u || c->function==0x001F1180u) return aura_call(&f,c);
    f.owner.callback=c->function==0x00219550u ? c->function : 0x0015AFA0u;
    if (load(&f)<0) return -1;
    int rc;
    if (c->function==0x001B6EA0u) rc=take(&f);
    else if (c->function==0x0015AC00u || (!f.owner.lifecycle && c->function!=0x0015AE20u)) rc=initialize(&f);
    else {
        if (c->function==0x0015AE20u) {
            if (c->na<2) return fail(p,c->function);
            f.block=(uint32_t)c->a[1];
        }
        const EmPickupOwnerHooks h={&f,script_start,script_tick,event};
        float item_y=em_ee_float(read_value(&f,f.node+0xB4u,4));
        float player_y=em_ee_float(read_value(&f,0x00810354u,4));
        uint8_t action=(uint8_t)read_value(&f,0x008104A0u,1);
        uint8_t no_grab=(uint8_t)read_value(&f,0x008104E6u,1);
        rc=c->function==0x0015AE20u ? em_pickup_owner_0015AE20(&f.owner,item_y,player_y,action,no_grab,0,&h)
                                   : em_pickup_owner_tick(&f.owner,item_y,player_y,action,no_grab,0,&h);
    }
    if (rc<0 || p->fault || store(&f)<0) return fail(p,c->function);
    c->v0=(uint32_t)rc;return 0;
}
