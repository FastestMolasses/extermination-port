/* The actual live scratch accessor, with test-only views elsewhere. */
#include "game/em_area01_live.h"
#include "game/em_area01_math_actor.h"
#include "game/em_aim_fire_sdk_memory.h"
#include <string.h>
static EmArea01Live live;
static uint8_t *ram,*spr;
static EmA01Math math;
static EmArea01Sys sys;
static uint32_t stopped;
static uint8_t *memory(void *ctx,uint32_t a,uint32_t n,int write)
{
    (void)ctx;(void)write;
    if((uint64_t)a+n>0x70003000u && a<0x70003040u)
        return em_area01_live_matrix_3000(&live,a,n);
    if(a<0x2000000u && n<=0x2000000u-a)return ram+a;
    if(a>=0x70000000u && a<0x70004000u && n<=0x70004000u-a)return spr+a-0x70000000u;
    return NULL;
}
static void *sdk_memory(void *ctx,uint32_t a,size_t n,int write)
{return n<=UINT32_MAX ? memory(ctx,a,(uint32_t)n,write) : NULL;}
static int worker(void *ctx,uint32_t fn,const uint32_t *a,unsigned na,
                  const uint32_t *f,unsigned nf,uint32_t *v0,uint32_t *f0)
{
    (void)ctx;
    if(fn==0x001B2140u)return em_area01_math_001B2140(&math,a[0],v0);
    if(fn==0x001B17A0u){stopped=fn;return -1;}
    EmAimFireTargetCall c={.function=fn,.na=na,.nf=nf};
    for(unsigned i=0;i<na;++i)c.a[i]=(uint64_t)(int64_t)(int32_t)a[i];
    if(nf)memcpy(c.f,f,nf*4);
    int rc=em_aim_fire_sdk_memory_call(NULL,sdk_memory,&c);
    *v0=(uint32_t)c.v0;*f0=c.f0;
    if(rc)stopped=fn;
    return rc==0 ? 0 : -1;
}
static int sys_worker(void *ctx,EmArea01SysCall *c)
{
    uint32_t a[8],v0=0,f0=0;for(unsigned i=0;i<c->na;++i)a[i]=(uint32_t)c->a[i];
    int rc=worker(ctx,c->fn,a,c->na,c->f,c->nf,&v0,&f0);
    c->v0=(uint64_t)(int64_t)(int32_t)v0;c->f0=f0;return rc;
}
void ms_seed(uint8_t *r,uint8_t *s)
{
    ram=r;spr=s;memset(&live,0,sizeof live);live.bound=live.active=1;
    memset(live.scratch_3000,0xA5,sizeof live.scratch_3000);
    /* The test's old snapshot window is poisoned differently and must never
     * receive the translated writes; all 3000 accesses use the live owner. */
    memset(spr+0x3000,0xC7,64);
    memset(&math,0,sizeof math);math.view=memory;math.call=worker;
    memset(&sys,0,sizeof sys);sys.view=memory;sys.call=sys_worker;sys.sp=0x7F0F0000u;stopped=0;
}
int ms_call(uint32_t fn,uint32_t node)
{
    if(fn==0x001C3BE0u)return em_area01_math_001C3BE0(&math,node,node+0x1F0u);
    if(fn==0x001C3D60u)return em_area01_math_001C3D60(&math,node,node+0x1F0u);
    if(fn==0x00128C10u)return em_area01_sys_00128C10(&sys,node);
    return -1;
}
uint32_t ms_stop(void){return stopped;}
int ms_snapshot(void)
{
    for(unsigned i=0;i<64;i++)if(spr[0x3000+i]!=0xC7)return -1;
    memcpy(spr+0x3000,live.scratch_3000,64);return 0;
}
int ms_contracts(void)
{
    uint8_t *base=(uint8_t *)live.scratch_3000;
    if(em_area01_live_matrix_3000(&live,0x70003000,64)!=base)return -1;
    if(em_area01_live_matrix_3000(&live,0x7000303F,1)!=base+63)return -1;
    const uint32_t spans[][2]={{0x70002FFF,2},{0x7000303F,2},{0x70003040,1},
                              {0x70003000,65},{0x70003000,0},{0x70003000,UINT32_MAX}};
    for(unsigned i=0;i<sizeof spans/sizeof spans[0];i++)
        if(em_area01_live_matrix_3000(&live,spans[i][0],spans[i][1]))return -1;
    live.active=0;if(em_area01_live_matrix_3000(&live,0x70003000,4))return -1;
    live.active=1;live.bound=0;if(em_area01_live_matrix_3000(&live,0x70003000,4))return -1;
    live.bound=1;live.fault=1;if(em_area01_live_matrix_3000(&live,0x70003000,4))return -1;
    live.fault=0;
    uint8_t *alias=em_area01_live_matrix_3000(&live,0x7000300C,4);
    uint32_t value=0x13579BDF;memcpy(alias,&value,4);
    if(live.scratch_3000[3]!=value)return -1;
    live.scratch_3000[3]=0x2468ACE0;memcpy(&value,alias,4);
    return value==0x2468ACE0 ? 13 : -1;
}
