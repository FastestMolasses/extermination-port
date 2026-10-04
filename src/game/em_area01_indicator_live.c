#include "game/em_area01_indicator_live.h"
#include "game/em_indicator_child.h"
#include "game/em_effect_kinds.h"
#include <string.h>

typedef struct {
    const EmArea01RuntimeHost *host;
    uint32_t node,sp,fault;
    uint8_t status,alt;
    float a0[4],c80[4];
    int freed;
} Frame;
static int fail(Frame *f,uint32_t a)
{ if(!f->fault)f->fault=a;return -1; }
static uint8_t *mem(Frame *f,uint32_t a,uint32_t n,int write)
{
    uint8_t *p=f->fault ? NULL : f->host->bytes(f->host->ctx,a,n,write);
    if(!p)fail(f,a);return p;
}
static int load(Frame *f)
{
    uint8_t *p=mem(f,f->node+4,1,0);if(!p)return -1;f->status=*p;
    p=mem(f,f->node+0xA,1,0);if(!p)return -1;f->alt=*p;
    p=mem(f,f->node+0xA0,16,0);if(!p)return -1;memcpy(f->a0,p,16);
    p=mem(f,f->node+0x80,16,0);if(!p)return -1;memcpy(f->c80,p,16);
    return 0;
}
static int store(Frame *f)
{
    uint8_t *p=mem(f,f->node+4,1,1);if(!p)return -1;*p=f->status;
    p=mem(f,f->node+0x80,16,1);if(!p)return -1;memcpy(p,f->c80,16);return 0;
}
static int invoke(Frame *f,uint32_t fn,int argument,uint64_t *result)
{
    if(store(f)<0)return -1;
    EmArea01Call c={.function=fn,.sp=f->sp,.na=(uint32_t)argument};c.a[0]=f->node;
    if(f->host->worker(f->host->ctx,&c)<0)return fail(f,fn);
    if(result)*result=c.v0;
    if(fn==0x001AFC10u) { f->freed=1;return 0; }
    return load(f);
}
static int init(void *ctx,uint32_t fn,int32_t *result)
{ uint64_t value=0;int rc=invoke(ctx,fn,1,&value);*result=(int32_t)value;return rc; }
static int place(void *ctx) { return invoke(ctx,0x001C6380u,1,NULL); }
static int free_self(void *ctx) { return invoke(ctx,0x001AFC10u,1,NULL); }
static int random_value(void *ctx,int32_t *result)
{ uint64_t value=0;int rc=invoke(ctx,0x00122BB8u,0,&value);*result=(int32_t)value;return rc; }
static int draw(void *ctx,uint32_t fn,void *obj)
{ Frame *f=ctx;return obj==f ? invoke(f,fn,1,NULL) : fail(f,fn); }
static int color(void *ctx,float c80[4])
{
    Frame *f=ctx;uint32_t method=0;
    uint8_t *p=mem(f,f->node+0x4C,4,0);if(!p)return -1;memcpy(&method,p,4);
    EmEffectKindsWorkers workers={.ctx=f,.w_00122BB8=random_value,.w_indirect=draw};
    EmEffectKinds kinds={.workers=&workers};
    int rc=em_effect_kinds_001F54E0(&kinds,f,c80,method,c80);
    return rc<0 ? fail(f,kinds.fault.address) : 0;
}
int em_area01_indicator_handles(uint32_t fn)
{ return fn==0x001C5680u || fn==0x001C5760u; }
int em_area01_indicator_call(const EmArea01RuntimeHost *host,EmArea01Call *c,uint32_t *fault)
{
    if(!host || !host->bytes || !host->worker || !c || !fault)return -1;
    if(*fault)return -1;
    if(!em_area01_indicator_handles(c->function) || c->na!=1) { *fault=c->function;return -1; }
    Frame f={.host=host,.node=(uint32_t)c->a[0],.sp=c->sp};
    int rc=load(&f);
    if(rc>=0) {
        EmIndicatorChildRecord record={&f.status,f.alt,f.a0,f.c80};
        const EmIndicatorChildWorkers workers={&f,init,place,color,free_self};
        rc=em_indicator_child_step(c->function,&record,&workers);
        if(!f.freed && store(&f)<0)rc=-1;
    }
    if(rc<0) { *fault=f.fault ? f.fault : c->function;return -1; }
    c->v0=0;return 0;
}
