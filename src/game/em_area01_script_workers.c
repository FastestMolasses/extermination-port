#include "game/em_area01_script_workers.h"
#include "game/em_area00_low.h"
#include "game/em_area02_misc.h"
#include "game/em_area01_revisit.h"
#include "game/em_ee_float.h"
#include <string.h>

typedef struct { const EmArea01RuntimeHost *h;uint32_t sp; } Frame;
static uint8_t *view(void *ctx,uint32_t a,uint32_t n,int write)
{ Frame *f=ctx;return f->h->bytes(f->h->ctx,a,n,write); }
static uint8_t *revisit_view(void *ctx,uint32_t a,uint32_t n)
{ return view(ctx,a,n,0); }
static int misc_call(void *ctx,EmArea02MiscCall *in)
{
    Frame *f=ctx;
    EmArea01Call c={.function=in->fn,.sp=in->sp,.na=in->na,.nf=in->nf};
    memcpy(c.a,in->a,sizeof c.a);memcpy(c.f,in->f,sizeof c.f);
    int rc=f->h->worker(f->h->ctx,&c);in->v0=c.v0;in->f0=c.f0;return rc;
}
static int low_call(void *ctx,EmArea00LowCall *in)
{
    Frame *f=ctx;
    EmArea01Call c={.function=in->fn,.sp=in->sp,.na=in->na,.nf=in->nf};
    memcpy(c.a,in->a,sizeof c.a);memcpy(c.f,in->f,sizeof c.f);
    int rc=f->h->worker(f->h->ctx,&c);in->v0=c.v0;in->f0=c.f0;return rc;
}
static int math_call(void *ctx,uint32_t fn,const uint32_t *a,unsigned na,
                     const uint32_t *fp,unsigned nf,uint32_t *v0,uint32_t *f0)
{
    Frame *f=ctx;EmArea01Call c={.function=fn,.sp=f->sp,.na=na,.nf=nf};
    for(unsigned i=0;i<na;++i)c.a[i]=(uint64_t)(int64_t)(int32_t)a[i];
    if(nf)memcpy(c.f,fp,4*nf);
    int rc=f->h->worker(f->h->ctx,&c);*v0=(uint32_t)c.v0;*f0=c.f0;return rc;
}
static uint32_t invoke(EmA01Math *m,uint32_t fn,unsigned na,uint32_t a,uint32_t b,
                       uint32_t c,uint32_t d,unsigned nf,uint32_t x,uint32_t y,uint32_t z,int fp)
{
    uint32_t args[4]={a,b,c,d},fs[3]={x,y,z},v0=0,f0=0;
    em_a01m_call(m,fn,args,na,fs,nf,&v0,&f0);return fp ? f0 : v0;
}
/* Runtime 00826950; link/decomp name 00826910. The original matched C
 * drives the player on the parameter path; all SDK/camera/sound operations
 * stay original worker calls, with the caller's 0x50-byte frame preserved. */
static uint32_t walk(EmA01Math *m,uint32_t block,uint32_t rec)
{
    const uint32_t p=0x008102B0u;
    uint32_t phase=em_a01m_lbu(m,block+4);
    if(phase==0) {
        invoke(m,0x00102948u,2,rec+0x20,p+0xA0,0,0,0,0,0,0,0);
        em_a01m_sb(m,block+4,em_a01m_lbu(m,block+4)+1);
        em_a01m_sw(m,rec+0x10,0);em_a01m_sh(m,p+0x1F2,2);
        em_a01m_sb(m,p+0x25C,2);em_a01m_sw(m,p+0x1F8,0x40800000u);
        return 0;
    }
    if(phase==1) {
        uint32_t goal=invoke(m,0x001B1240u,1,p+0xA0,0,0,0,2,
                              em_a01m_lw(m,rec+0x30),em_a01m_lw(m,rec+0x38),0,1);
        uint32_t yaw=invoke(m,0x001B12B0u,0,0,0,0,0,3,goal,em_a01m_lw(m,p+0xC4),0x3D8EFA35u,1);
        em_a01m_sw(m,p+0xC4,yaw);
        if(!em_ee_c_eq_bits(yaw,goal))return 0;
        em_a01m_sb(m,block+4,em_a01m_lbu(m,block+4)+1);phase=2;
    }
    if(phase!=2)return 0;
    if(!em_ee_c_lt_bits(em_a01m_lw(m,rec+0x10),em_a01m_lw(m,rec+0x0C))) {
        em_a01m_sh(m,p+0x1F2,0);em_a01m_sb(m,p+0x25C,0);em_a01m_sw(m,p+0x1F8,0x40800000u);
        return 1;
    }
    const uint32_t cue[3]={0x41A00000u,0x42200000u,0x425C0000u};
    for(unsigned i=0;i<3;++i)
        if(em_ee_c_eq_bits(em_a01m_lw(m,rec+0x10),cue[i]))
            invoke(m,0x001FB9F0u,4,0x4A,0x1000,0x1000,0x1000,0,0,0,0,0);
    em_a01m_sw(m,rec+0x10,em_ee_add_bits(em_a01m_lw(m,rec+0x10),0x3F800000u));
    uint32_t ratio=em_ee_div_bits(em_a01m_lw(m,rec+0x10),em_a01m_lw(m,rec+0x0C));
    invoke(m,0x001028D0u,3,block+0x10,rec+0x30,rec+0x20,0,0,0,0,0,0);
    for(unsigned i=0;i<3;++i)
        em_a01m_sw(m,0x70003600u+4*i,em_ee_add_bits(em_a01m_lw(m,rec+0x20+4*i),
            em_ee_mul_bits(em_a01m_lw(m,block+0x10+4*i),ratio)));
    invoke(m,0x00182F90u,2,p,0x70003600u,0,0,0,0,0,0,0);
    return 0;
}
int em_area01_script_worker_handles(uint32_t fn)
{ return em_area01_script_worker_handles_area(fn,1); }
int em_area01_script_worker_handles_area(uint32_t fn,unsigned area)
{
    /* the boot workers serve every bound area (AREA00..02, AREA04, AREA06,
     * AREA19, AREA22) */
    if(area>2 && area!=4 && area!=6 && area!=0x13 && area!=0x16)return 0;
    return fn==0x001B7670u || fn==0x001B7700u || fn==0x001B8AB0u || fn==0x001B6AE0u ||
           (area==1 && (fn==0x00825910u || fn==0x00826950u)) ||
           (area==0 && fn==0x00824E40u);
}
int em_area01_script_worker_call(const EmArea01RuntimeHost *host,EmArea01Call *c,uint32_t *fault)
{
    return em_area01_script_worker_call_area(host,c,fault,1);
}
int em_area01_script_worker_call_area(const EmArea01RuntimeHost *host,EmArea01Call *c,
                                     uint32_t *fault,unsigned area)
{
    if(!host || !host->bytes || !host->worker || !c || c->na<3 ||
       !em_area01_script_worker_handles_area(c->function,area))return -1;
    Frame f={host,c->sp};int32_t result=0;int rc=-1;
    uint32_t block=(uint32_t)c->a[1],rec=(uint32_t)c->a[2],failed=c->function;
    if(c->function==0x001B7670u) {
        EmArea00LowRegion regions[2]={{rec+8,4,view(&f,rec+8,4,0)},
                                     {0x70003B91u,1,view(&f,0x70003B91u,1,1)}};
        EmArea00Low owner={.regions=regions,.region_count=2,.sp=c->sp};
        rc=em_area00_low_001B7670(&owner,rec,&result);failed=owner.fault_address;
    } else if(c->function==0x001B7700u) {
        EmArea00LowRegion regions[4]={{rec,0x18,view(&f,rec,0x18,0)},
            {block+4,1,view(&f,block+4,1,1)},{0x008106CEu,2,view(&f,0x008106CEu,2,1)},
            {0x00275BD8u,1,view(&f,0x00275BD8u,1,1)}};
        EmArea00Low owner={.regions=regions,.region_count=4,.sp=c->sp};
        rc=em_area00_low_001B7700(&owner,block,rec,&result);failed=owner.fault_address;
    } else if(c->function==0x001B8AB0u) {
        /* 001B8AB0 (op03; NEARMISS decomp C, em_area00_low's translation
         * checked against the original instructions): the camera shake of
         * D_008105D0 / D_008105E0 by the mode word rec +0x08. It reads and
         * writes rec +0x08..+0x17, the phase byte block +4 and the scratch
         * quadword 0x70003600; its quadword copies, 001281C0, 00122BB8,
         * 001026A0 and 001028B8 are calls through the host. */
        EmArea00LowRegion regions[3]={{rec+8,0x10,view(&f,rec+8,0x10,1)},
            {block+4,1,view(&f,block+4,1,1)},{0x70003600u,0x10,view(&f,0x70003600u,0x10,1)}};
        EmArea00Low owner={.regions=regions,.region_count=3,.sp=c->sp,.call=low_call,.ctx=&f};
        rc=em_area00_low_001B8AB0(&owner,block,rec,&result);failed=owner.fault_address;
    } else if(c->function==0x00824E40u) {
        /* Byte-matched func_overlay_AREA00_00824E00: the arrival's clip
         * callback. Stores and clip call stay on the canonical actor view. */
        f.sp=c->sp-0x20u;
        EmA01Math m={.ctx=&f,.view=view,.call=math_call};
        const uint32_t actor=(uint32_t)c->a[0],anim=actor+0x1F0u;
        em_a01m_sw(&m,anim+0xD8u,0x3F19999Au);
        em_a01m_sw(&m,anim+0xECu,0x40266666u);
        invoke(&m,0x001C67E0u,2,actor,6,0,0,2,0x40C00000u,0,0,0);
        em_a01m_sh(&m,anim+0xD0u,0xF0u);
        result=1;rc=m.fault_code ? -1 : 0;failed=m.fault_address;
    } else if(c->function==0x001B6AE0u) {
        EmArea02MiscRegion regions[3]={{block,0x30,view(&f,block,0x30,1)},
            {rec,64,view(&f,rec,64,1)},{0x008106F4u,1,view(&f,0x008106F4u,1,0)}};
        EmArea02Misc owner={.regions=regions,.region_count=3,.sp=c->sp,.ctx=&f,.call=misc_call};
        rc=em_area02_misc_001B6AE0(&owner,block,rec,&result);failed=owner.fault_address;
    } else if(c->function==0x00825910u) {
        EmArea01RevisitHooks h={.ctx=&f,.bytes=revisit_view};EmArea01RevisitFault err={0};
        rc=em_area01_revisit_00825910(&h,(uint32_t)c->a[0],block,rec,&result,&err);failed=err.address;
    } else {
        f.sp=c->sp-0x50u;
        EmA01Math m={.ctx=&f,.view=view,.call=math_call};
        result=(int32_t)walk(&m,block,rec);rc=m.fault_code ? -1 : 0;failed=m.fault_address;
    }
    if(rc<0) { if(fault)*fault=failed;return -1; }
    c->v0=(uint64_t)(int64_t)result;return 0;
}
