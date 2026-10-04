#include "game/em_area01_rcl_workers.h"
#include "game/em_render_context_live.h"
#include <string.h>

static int forward(void *ctx, EmArea01SysCall *in)
{
    const EmArea01RuntimeHost *host=ctx;
    EmArea01Call c={.function=in->fn,.sp=in->sp,.na=in->na,.nf=in->nf};
    memcpy(c.a,in->a,sizeof in->a);memcpy(c.f,in->f,sizeof in->f);
    int rc=host->worker(host->ctx,&c);
    in->v0=c.v0;in->f0=c.f0;return rc;
}
static uint8_t *view(void *ctx,uint32_t a,uint32_t n,int write)
{
    const EmArea01RuntimeHost *host=ctx;
    uint8_t *p=write ? em_rcl_bytes_mut(a,n) : (uint8_t *)em_rcl_bytes(a,n);
    return p ? p : host->bytes(host->ctx,a,n,write);
}
int ar_water(const EmArea01RuntimeHost *host,uint32_t actor,uint32_t sp,uint32_t *fault)
{
    EmArea01Sys sys={.call=forward,.ctx=(void *)host,.sp=sp,.view=view};
    int rc=em_area01_sys_001E7D20(&sys,actor);
    *fault=sys.fault_address;return rc;
}
