#include "game/em_area01_door_live.h"
#include "game/em_actor_pool.h"
#include "game/em_door_original.h"
#include "game/em_door_program.h"
#include "game/em_script_door_fan.h"
#include "game/em_startup_load_gaps.h"
#include "game/em_ee_float.h"
#include <string.h>

typedef struct {
    EmArea01Door *binding;
    EmDoorOriginal owner;
    uint32_t node, block, sp;
    int freed;
} Frame;

static int fail(EmArea01Door *d, uint32_t address)
{
    if (d && !d->fault) { d->fault = 1; d->fault_address = address; }
    return -1;
}
static uint8_t *mem(Frame *f, uint32_t a, uint32_t n, int write)
{
    EmArea01Door *d = f->binding;
    if (d->fault) return NULL;
    uint8_t *p = n && (uint64_t)a + n <= UINT64_C(0x100000000) ?
        d->host.bytes(d->host.ctx, a, n, write) : NULL;
    if (!p) fail(d, a);
    return p;
}
static uint32_t read_word(Frame *f, uint32_t a, unsigned n)
{
    const uint8_t *p = mem(f, a, n, 0);
    uint32_t v = 0;
    if (p) for (unsigned i = 0; i < n; ++i) v |= (uint32_t)p[i] << (8u*i);
    return v;
}
static void write_word(Frame *f, uint32_t a, uint32_t v, unsigned n)
{
    uint8_t *p = mem(f, a, n, 1);
    if (p) for (unsigned i = 0; i < n; ++i) p[i] = (uint8_t)(v >> (8u*i));
}
static int load(Frame *f)
{
    const uint8_t *p = mem(f, f->node, EM_ACTOR_RECORD_SIZE, 0);
    if (!p) return -1;
    EmDoorOriginal *o = &f->owner;
    o->status=p[0]; o->visible=p[1]; o->class_flags=p[2]; o->subtype=p[3];
    o->lifecycle=p[4]; o->phase=p[5]; o->armed=p[11]; o->freed=0;
    memcpy(&o->side,p+0x2E,2); memcpy(&o->door_id,p+0x34,2); memcpy(&o->link_flags,p+0x56,2);
    memcpy(o->origin,p+0xB0,12); memcpy(o->initialized_scale,p+0x80,12);
    o->animation_active=(int8_t)read_word(f,f->block+12,1);
    o->animation_flags=(int16_t)read_word(f,f->block+14,2);
    return f->binding->fault ? -1 : 0;
}
static int store(Frame *f)
{
    if (f->freed) return 0;
    const EmDoorOriginal *o = &f->owner;
    const uint8_t head[6] = {o->status,o->visible,o->class_flags,o->subtype,o->lifecycle,o->phase};
    uint8_t *p = mem(f,f->node,6,1);
    if (p) memcpy(p,head,6);
    write_word(f,f->node+11,o->armed,1);
    write_word(f,f->node+0x2E,(uint16_t)o->side,2);
    write_word(f,f->node+0x34,(uint16_t)o->door_id,2);
    p = mem(f,f->node+0x80,12,1);
    if (p) memcpy(p,o->initialized_scale,12);
    write_word(f,f->block+12,(uint8_t)o->animation_active,1);
    write_word(f,f->block+14,(uint16_t)o->animation_flags,2);
    return f->binding->fault ? -1 : 0;
}
static int worker(Frame *f, EmArea01Call *c)
{
    c->sp=f->sp;
    if (store(f)<0) return -1;
    if (f->binding->host.worker(f->binding->host.ctx,c)<0) return fail(f->binding,c->function);
    return f->freed ? 0 : load(f);
}
static int unary_worker(Frame *f, uint32_t fn)
{
    EmArea01Call c={.function=fn,.a={f->node},.na=1};
    return worker(f,&c)<0 ? -1 : 1;
}

static int alloc(void *ctx, uint8_t *node, int32_t *result)
{
    Frame *f=ctx;
    EmArea01Call c={.function=0x001B0EA0u,.a={f->node},.na=1};
    if (worker(f,&c)<0) return -1;
    const uint8_t *p=mem(f,f->node,0x50,0);
    if (!p) return -1;
    memcpy(node,p,0x50);
    *result=(int32_t)c.v0;
    return 0;
}
static int seed(void *ctx, uint8_t *node, int32_t clip)
{
    Frame *f=ctx;
    uint32_t bank;
    memcpy(&bank,node+0x40,4);
    write_word(f,f->node+0x40,bank,4);
    EmArea01Call c={.function=0x001C63E0u,.a={f->node,(uint64_t)(int64_t)clip},.na=2};
    if (worker(f,&c)<0) return -1;
    const uint8_t *p=mem(f,f->node,0x50,0);
    if (!p) return -1;
    memcpy(node,p,0x50);
    return 0;
}
static int node_start(Frame *f, int32_t clip)
{
    uint8_t node[0x50];
    const uint8_t *p=mem(f,f->node,sizeof node,0);
    if (!p) return -1;
    memcpy(node,p,sizeof node);
    const uint32_t bank=read_word(f,0x0028A574u,4);
    const EmSlgNodeStartWorkers w={f,alloc,seed};
    int32_t result;
    if (f->binding->fault || em_slg_001B0F60(&w,node,clip,bank,&result)<0) return -1;
    write_word(f,f->node+4,node[4],1);
    if (load(f)<0) return -1;
    return result;
}
static int initialize(void *ctx)
{
    Frame *f=ctx;
    int result=node_start(f,0);
    if (result<0) return -1;
    if (!result) write_word(f,f->node+0x30,0x002755F0u,4);
    return f->binding->fault ? -1 : result==0;
}
static int advance(void *ctx, int16_t *flags)
{
    Frame *f=ctx;
    EmArea01Call c={.function=0x001C64F0u,.a={f->node},.na=1,.f={0x3F800000u},.nf=1};
    if (worker(f,&c)<0) return -1;
    *flags=(int16_t)c.v0;
    return 1;
}
static int script_tick(void *ctx)
{
    Frame *f=ctx;
    EmArea01Call c={.function=0x001BA1F0u,.a={f->node},.na=1};
    return worker(f,&c)<0 ? -1 : (uint32_t)c.v0!=0;
}
static int script_start(void *ctx, uint32_t entry)
{
    Frame *f=ctx;
    EmArea01Call c={.function=0x001BA1A0u,.a={f->block,entry},.na=2};
    return worker(f,&c)<0 ? -1 : 1;
}
static int reset(void *ctx)
{
    Frame *f=ctx;
    EmArea01Call c={.function=0x001C67E0u,.a={f->node,0},.na=2,.f={0,0},.nf=2};
    return worker(f,&c)<0 ? -1 : 1;
}
static int place(void *ctx) { return unary_worker(ctx,0x001C68C0u); }
static int publish(void *ctx, const float position[3])
{
    Frame *f=ctx;
    EmArea01Call c={.function=0x001B1B30u,.a={f->node},.na=1,.nf=3};
    memcpy(c.f,position,12);
    return worker(f,&c)<0 ? -1 : f->owner.visible;
}
static int draw(void *ctx)
{
    Frame *f=ctx;
    const uint32_t fn=read_word(f,f->node+0x4C,4);
    return f->binding->fault || !fn ? fail(f->binding,f->node+0x4C) : unary_worker(f,fn);
}
static int release(void *ctx)
{
    Frame *f=ctx;
    if (store(f)<0) return -1;
    f->freed=1;
    return unary_worker(f,0x001AFC10u);
}

static int float_call(Frame *f, uint32_t fn, const uint32_t *a, unsigned na,
                       const float *floats, unsigned nf, float *result)
{
    EmArea01Call c={.function=fn,.na=na,.nf=nf};
    for (unsigned i=0;i<na;++i) c.a[i]=a[i];
    memcpy(c.f,floats,nf*4u);
    if (worker(f,&c)<0) return -1;
    *result=em_ee_float(c.f0);
    return 0;
}
static int bearing(void *ctx, const float origin[3], float x, float z, float *result)
{
    Frame *f=ctx;
    (void)origin;
    const uint32_t arg=f->node+0xB0;
    const float values[2]={x,z};
    return float_call(f,0x001B1240u,&arg,1,values,2,result);
}
static int wrap(void *ctx, float x, float *result) { return float_call(ctx,0x001B1470u,NULL,0,&x,1,result); }
static int sine(void *ctx, float x, float *result) { return float_call(ctx,0x0011E2A8u,NULL,0,&x,1,result); }
static int cosine(void *ctx, float x, float *result) { return float_call(ctx,0x0011DE90u,NULL,0,&x,1,result); }

static int sound_half(void *ctx, uint32_t address, uint16_t *result)
{
    Frame *f=ctx;
    *result=(uint16_t)read_word(f,address,2);
    return f->binding->fault ? -1 : 0;
}
static int patch(void *ctx, const EmDoorTransitPlan *plan)
{
    Frame *f=ctx;
    EmDoorTransitPlan patched=*plan;
    if (!plan->locked) {
        const EmSdfWorkers w={.ctx=f,.r_0024DB80=sound_half};
        EmSdfFault fault={0};
        uint32_t sound;
        if (em_sdf_001BBD60((int16_t)f->owner.link_flags,(uint16_t)f->owner.side,&sound,&w,&fault)<0)
            return fail(f->binding,fault.address);
        patched.sound=(uint16_t)sound;
    }
    /* Mutable ELF script storage is shared by all door instances, exactly
     * as in the original. The active door's block keeps its own cursor. */
    uint8_t *p=mem(f,0x0024DBC0u,0x3C0,1);
    EmScriptImage image={p,0x0024DBC0u,0x0024DE40u,0x3C0};
    if (!p || !em_door_program_patch(&image,&patched)) return fail(f->binding,0x001BBE40u);
    return store(f)<0 ? -1 : 1;
}
static int face(void *ctx, float yaw)
{
    Frame *f=ctx;
    write_word(f,0x00810374u,em_ee_bits(yaw),4);
    return f->binding->fault ? -1 : 1;
}
static int align(void *ctx, const float point[4])
{
    Frame *f=ctx;
    uint8_t *p=mem(f,0x700038A0u,16,1);
    if (!p) return -1;
    memcpy(p,point,16);
    EmArea01Call c={.function=0x00182F90u,.a={0x008102B0u,0x700038A0u},.na=2};
    return worker(f,&c)<0 ? -1 : 1;
}
static int kickoff(void *ctx, int locked)
{
    Frame *f=ctx;
    if (!(f->owner.armed&4u)) return 0;
    float player[3];
    const uint8_t *p=mem(f,0x00810350u,12,0);
    if (!p) return -1;
    memcpy(player,p,12);
    const float yaw=em_ee_float(read_word(f,f->node+0xC4,4));
    /* 001BBD60 reads only the selected column after the side latch. */
    const uint16_t sounds[2]={0,0};
    if (f->binding->fault) return -1;
    const EmDoorTransitMath m={f,bearing,wrap,sine,cosine};
    const EmDoorTransitHooks h={f,patch,face,align,script_start,script_tick};
    return em_door_transit_kickoff(&f->owner,yaw,player,sounds,locked,&m,&h);
}
static int fade(void *ctx, int whole, int ticks)
{
    Frame *f=ctx;
    EmArea01Call c={.function=whole?0x001B0C00u:0x001AEDE0u,.a={(uint32_t)ticks,0},.na=whole?1:2};
    return worker(f,&c)<0 ? -1 : 1;
}
static int transition(void *ctx)
{
    Frame *f=ctx;
    const uint32_t area=read_word(f,0x00810700u,1);
    const uint32_t table=read_word(f,0x0024E140u+area*4u,4);
    const uint32_t row=table+((uint16_t)f->owner.door_id&0x7Fu)*4u;
    uint8_t record[4];
    const uint8_t *p=mem(f,row,4,0);
    if (!p) return -1;
    memcpy(record,p,4);
    EmDoorDestination dest;
    p=mem(f,0x008106B5u,4,0);
    if (!p) return -1;
    memcpy(&dest,p,4);
    if (em_door_transit_commit(&dest,f->owner.door_id,(uint16_t)f->owner.side,record,fade,f)<0) return -1;
    p=mem(f,0x008106B5u,4,1);
    if (!p) return -1;
    memcpy((void *)p,&dest,4);
    return 1;
}

int em_area01_door_handles(uint32_t fn)
{
    switch (fn) {
    case 0x001B0F60u: case 0x001BBDA0u: case 0x001BBE40u: case 0x001BC0E0u:
    case 0x001BC150u: case 0x001BC240u: case 0x001BC290u: case 0x001BC300u: case 0x001BC350u:
        return 1;
    default: return 0;
    }
}
int em_area01_door_bind(EmArea01Door *d, const EmArea01RuntimeHost *host)
{
    if (!d || !host || !host->bytes || !host->worker) return -1;
    memset(d,0,sizeof *d);
    d->host=*host;
    return 0;
}
int em_area01_door_call(EmArea01Door *d, EmArea01Call *c)
{
    if (!d || !c || d->fault) return -1;
    if (!em_area01_door_handles(c->function) || !c->na) return fail(d,c->function);
    Frame f={.binding=d,.node=(uint32_t)c->a[0],.sp=c->sp};
    f.block=f.node+0x1F0u;
    if (c->function==0x001BBE40u || c->function==0x001BC0E0u ||
        c->function==0x001BC240u || c->function==0x001BC290u) {
        if (c->na<2) return fail(d,c->function);
        f.block=(uint32_t)c->a[1];
    }
    if (load(&f)<0) return -1;
    const EmDoorOriginalHooks h={&f,initialize,kickoff,advance,script_tick,script_start,
                                transition,reset,place,publish,draw,release};
    int result=-1;
    switch (c->function) {
    case 0x001B0F60u:
        if (c->na<2) return fail(d,c->function);
        result=node_start(&f,(int32_t)c->a[1]); break;
    case 0x001BBDA0u: result=em_door_original_001BBDA0(&f.owner,&h); break;
    case 0x001BBE40u:
        if (c->na<3) return fail(d,c->function);
        result=kickoff(&f,(uint32_t)c->a[2]!=0); break;
    case 0x001BC0E0u: result=em_door_original_001BC0E0(&f.owner,&h); break;
    case 0x001BC150u: result=transition(&f); break;
    case 0x001BC240u: result=em_door_original_001BC240(&f.owner,&h); break;
    case 0x001BC290u:
        result=em_door_original_001BC290(&f.owner,(uint8_t)read_word(&f,0x008106B8u,1),&h); break;
    case 0x001BC300u: result=em_door_original_001BC300(&f.owner,&h); break;
    case 0x001BC350u: {
        unsigned unlocked=1;
        if (f.owner.subtype==0x15 && f.owner.lifecycle==1 && f.owner.phase==0) {
            const uint32_t area=read_word(&f,0x00810700u,1);
            const uint32_t bits=read_word(&f,0x00810841u+area,1);
            unlocked=(bits>>(f.owner.door_id&31))&1u;
        }
        result=em_door_original_tick(&f.owner,unlocked,(uint8_t)read_word(&f,0x008106B8u,1),&h); break;
    }
    default: break;
    }
    if (result<0 || d->fault) return fail(d,c->function);
    if (store(&f)<0) return -1;
    c->v0=(uint32_t)result;
    return 0;
}
