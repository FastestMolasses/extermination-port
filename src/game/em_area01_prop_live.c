#include "game/em_area01_prop_live.h"
#include "game/em_status_ui_leftovers.h"
#include <string.h>

typedef struct {
    const EmArea01RuntimeHost *host;
    uint32_t node,sp,fault;
    uint8_t image[0x50];
} Frame;
static int fail(Frame *f,uint32_t a)
{ if(!f->fault)f->fault=a;return -1; }
static int load(Frame *f)
{
    const uint8_t *p=f->host->bytes(f->host->ctx,f->node,sizeof f->image,0);
    if(!p)return fail(f,f->node);
    memcpy(f->image,p,sizeof f->image);return 0;
}
static int invoke(Frame *f,uint32_t fn,int32_t *result)
{
    EmArea01Call c={.function=fn,.sp=f->sp,.a={f->node},.na=1};
    if(f->host->worker(f->host->ctx,&c)<0)return fail(f,fn);
    if(result)*result=(int32_t)c.v0;
    /* Free invalidates the record. Every other worker may replace model
     * metadata, so refresh before the existing owner reads its method. */
    return fn==0x001AFC10u ? 0 : load(f);
}
static int bind_model(void *ctx,uint8_t *image,int32_t *result)
{ (void)image;return invoke(ctx,0x001B0FD0u,result); }
static int place(void *ctx,uint8_t *image)
{ (void)image;return invoke(ctx,0x001C6380u,NULL); }
static int publish(void *ctx,uint8_t *image)
{ (void)image;return invoke(ctx,0x001B17A0u,NULL); }
static int method(void *ctx,uint8_t *image,uint32_t fn)
{ (void)image;return invoke(ctx,fn,NULL); }
static int free_actor(void *ctx,uint8_t *image)
{ (void)image;return invoke(ctx,0x001AFC10u,NULL); }
int em_area01_prop_call(const EmArea01RuntimeHost *host,EmArea01Call *c,uint32_t *fault)
{
    if(!host || !host->bytes || !host->worker || !c || !fault)return -1;
    if(*fault)return -1;
    if(c->function!=0x001C4820u || c->na!=1 || c->sp<0x20u) {
        *fault=c->function;return -1;
    }
    Frame frame={.host=host,.node=(uint32_t)c->a[0],.sp=c->sp-0x20u};
    EmSulWorkers w={0};w.context=&frame;w.model_bind=bind_model;
    w.place=place;w.publish=publish;w.method=method;w.free_actor=free_actor;
    int rc=load(&frame);
    if(rc>=0)rc=em_sul_001C4820(&w,frame.image,sizeof frame.image);
    if(rc<0) { *fault=frame.fault ? frame.fault : c->function;return -1; }
    c->v0=0;return 0;
}
