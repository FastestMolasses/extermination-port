#include "game/em_aim_fire_reticle.h"
#include "game/em_ee_float.h"
#include <string.h>

typedef struct { EmAimFireReticle *h; uint32_t entry, sp; } Run;
#define CHECK() do { if (r->h->fault) return -1; } while(0)
#define ENTER(fn,frame) Run run={h,fn,h ? h->sp-frame : 0}, *r=&run; if (!h || h->fault) return -1
#define VU(expr) do { if ((expr)!=0) { fault(r,4,0); return -1; } } while(0)
static void fault(Run *r,int code,uint32_t address)
{
    if (!r->h->fault) { r->h->fault=code;r->h->fault_function=r->entry;r->h->fault_address=address; }
}
static uint8_t *mem(Run *r,uint32_t a,unsigned n,int w)
{
    if (r->h->fault) return NULL;
    if (!r->h->map) { fault(r,1,a);return NULL; }
    uint8_t *p=r->h->map(r->h->context,a,n,w);
    if (!p) fault(r,2,a);
    return p;
}
static uint32_t word(Run *r,uint32_t a)
{
    uint8_t *p=mem(r,a,4,0);
    return p ? (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24 : 0;
}
static void put(Run *r,uint32_t a,uint64_t value,unsigned n)
{
    uint8_t *p=mem(r,a,n,1);if (!p) return;
    for (unsigned i=0;i<n;++i) p[i]=(uint8_t)(value>>(8*i));
    if (r->h->store) r->h->store(r->h->context,a,n);
}
static void copy(Run *r,uint32_t dest,uint32_t source,unsigned n)
{
    uint8_t tmp[80];uint8_t *p=mem(r,source,n,0);if (!p) return;memcpy(tmp,p,n);
    p=mem(r,dest,n,1);if (!p) return;memcpy(p,tmp,n);
    if (r->h->store) r->h->store(r->h->context,dest,n);
}
static void quad_read(Run *r,uint32_t a,uint32_t out[4])
{ a&=~15u;for(unsigned i=0;i<4;++i) out[i]=word(r,a+4*i); }
static void quad_write(Run *r,uint32_t a,const uint32_t value[4])
{ a&=~15u;for(unsigned i=0;i<4;++i) put(r,a+4*i,value[i],4); }
static uint64_t sx(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }
static uint64_t invoke(Run *r,uint32_t fn,unsigned na,unsigned nf,const uint64_t a[7],uint32_t f)
{
    EmAimFireReticleCall c={fn,r->sp,{0},f,na,nf,0};memcpy(c.a,a,na*sizeof *a);
    if (r->h->fault) return 0;
    if (!r->h->call) fault(r,1,fn);
    else if (r->h->call(r->h->context,&c)<0) fault(r,3,fn);
    return c.v0;
}
static int transform(Run *r,unsigned row)
{
    EmArea00HudVu *v=r->h->vu;
    const uint32_t zero[4]={0,0,0,0x3F800000};
    VU(em_vu_vec_bits(EM_VU_MULABC,15,0,v->vf[row],v->vf[1],0,NULL,v->acc));
    VU(em_vu_vec_bits(EM_VU_MADDABC,15,1,v->vf[row+1],v->vf[1],0,v->acc,v->acc));
    VU(em_vu_vec_bits(EM_VU_MADDABC,15,2,v->vf[row+2],v->vf[1],0,v->acc,v->acc));
    VU(em_vu_vec_bits(EM_VU_MADDBC,15,3,v->vf[row+3],zero,0,v->acc,v->vf[2]));
    return 0;
}
static int clip(Run *r)
{
    EmArea00HudVu *v=r->h->vu;uint32_t flags=0;
    for(unsigned i=0;i<4;++i) if ((v->vf[2][i]&0x7F800000)==0x7F800000) { fault(r,4,0);return -1; }
    uint32_t w=em_eei_daz(v->vf[2][3])&0x7FFFFFFF;
    for(unsigned i=0;i<3;++i) {
        uint32_t x=em_eei_daz(v->vf[2][i]);
        if ((x&0x7FFFFFFF)>w) flags|=(x>>31 ? 2u : 1u)<<(2*i);
    }
    v->clip=((v->clip<<6)|flags)&0xFFFFFF;return (int)flags;
}
int em_aim_fire_reticle_001DD170(EmAimFireReticle *h,uint32_t style,uint32_t position,
                                uint32_t kind,uint32_t rgba,uint32_t value)
{
    ENTER(0x1DD170,0x50);
    if (!h->vu) { fault(r,1,0);return -1; }
    EmArea00HudVu *v=h->vu;
    uint32_t m=word(r,0x275670)+0x2240;CHECK();
    for(unsigned i=0;i<4;++i) quad_read(r,m+16*i,v->vf[24+i]);
    quad_read(r,position,v->vf[1]);CHECK();
    if(transform(r,24)<0)return -1;
    int flags=clip(r);CHECK();if(flags)return 0;
    for(unsigned i=0;i<4;++i) quad_read(r,0x70003AC0+16*i,v->vf[28+i]);CHECK();
    if(transform(r,28)<0)return -1;
    VU(em_vu_div_bits(0x3F800000,v->vf[2][3],3,3,&v->q));
    quad_write(r,r->sp+0x30,v->vf[2]);CHECK();
    VU(em_vu_vec_bits(EM_VU_MULQ,14,EM_VU_NO_BC,v->vf[2],NULL,v->q,NULL,v->vf[2]));
    const uint32_t zero[4]={0,0,0,0x3F800000};
    VU(em_vu_vec_bits(EM_VU_MULABC,1,2,zero,v->vf[23],0,NULL,v->acc));
    VU(em_vu_vec_bits(EM_VU_MADDBC,1,3,v->vf[23],v->vf[2],0,v->acc,v->vf[2]));
    v->vf[2][3]=em_vu_min_bits(v->vf[2][3],v->vf[23][0]);
    v->vf[2][3]=em_vu_max_bits(v->vf[2][3],0);
    for(unsigned i=0;i<4;++i)v->vf[2][i]=em_vu_ftoi4_bits(v->vf[2][i]);
    quad_write(r,0x70003600,v->vf[2]);CHECK();
    if(kind==0) {
        uint64_t a[7]={sx(style),0x70003600,sx(rgba)};
        (void)invoke(r,0x1DD2F0,3,0,a,0);CHECK();
    } else if(kind==1) {
        put(r,r->sp+0x48,rgba&0xFFFFFF,4);put(r,r->sp+0x4C,rgba>>24,1);put(r,r->sp+0x4D,0,1);
        uint64_t a[7]={sx(value),4,0};
        uint64_t text=invoke(r,0x1C5FB0,3,0,a,0);CHECK();
        uint32_t x=word(r,0x70003600)+0xC0,y=word(r,0x70003604);CHECK();
        a[0]=1;a[1]=sx((uint32_t)((int32_t)x>>4));a[2]=sx((uint32_t)((int32_t)y>>4));
        a[3]=8;a[4]=8;a[5]=text;a[6]=r->sp+0x48;
        (void)invoke(r,0x1CBA50,7,0,a,0);CHECK();
        a[0]=sx(style);a[1]=0x70003600;a[2]=sx(rgba);
        (void)invoke(r,0x1DD600,3,0,a,0);CHECK();
    }
    return 0;
}

/* Build one original inline GS packet, preserving its cursor reloads and
 * both scratch-coordinate stores at every vertex. */
static int packet(Run *r,uint32_t style,uint32_t screen,uint32_t rgba,uint32_t table,
                  unsigned vertices,unsigned count,uint32_t advance,uint64_t tag,uint64_t regs,uint32_t prim)
{
    uint32_t slot=word(r,0x275670)+4*style+0x10;CHECK();
    uint32_t block=word(r,slot);put(r,block+3,0x10,1);CHECK();
    block=word(r,slot);put(r,block+4,0,4);CHECK();
    block=word(r,slot);put(r,block,count,2);CHECK();
    block=word(r,slot);uint32_t dest=block+0x40;
    put(r,slot,block+advance,4);put(r,block+0x10,0,8);put(r,block+0x18,0,8);
    put(r,block+0x1C,0x50000000u+count-1,4);
    put(r,block+0x20,tag,8);put(r,block+0x28,regs,8);put(r,block+0x30,prim,8);put(r,block+0x38,rgba,8);CHECK();
    for(unsigned i=0;i<vertices;++i) {
        uint64_t a[7]={0};
        uint32_t f=em_ee_mul_bits(0x41800000,em_ee_mul_bits(0x3F4CCCCD,word(r,table+8*i)));
        uint32_t q=(uint32_t)invoke(r,0x1281C0,0,1,a,f);CHECK();
        put(r,0x70003620,word(r,screen)+q,4);CHECK();
        f=em_ee_mul_bits(0x41800000,em_ee_mul_bits(0x3F000000,word(r,table+8*i+4)));
        q=(uint32_t)invoke(r,0x1281C0,0,1,a,f);CHECK();
        put(r,0x70003624,word(r,screen+4)+q,4);CHECK();
        uint32_t xy=word(r,0x70003620)|(word(r,0x70003624)<<16);
        put(r,dest+8*i,sx(xy)|UINT64_C(0xFFFFFF00000000),8);CHECK();
    }
    return 0;
}
int em_aim_fire_reticle_001DD2F0(EmAimFireReticle *h,uint32_t style,uint32_t screen,uint32_t rgba)
{
    ENTER(0x1DD2F0,0xF0);
    copy(r,r->sp+0x70,0x2533D0,80);copy(r,r->sp+0xC0,0x253420,40);CHECK();
    if(packet(r,style,screen,rgba,r->sp+0x70,10,8,0x90,UINT64_C(0xC400000000008001),UINT64_C(0x444444444410),0x144)<0)return -1;
    return packet(r,style,screen,rgba,r->sp+0xC0,5,6,0x70,UINT64_C(0x7400000000008001),UINT64_C(0x4444410),0x142);
}
int em_aim_fire_reticle_001DD600(EmAimFireReticle *h,uint32_t style,uint32_t screen,uint32_t rgba)
{
    ENTER(0x1DD600,0x90);
    copy(r,r->sp+0x50,0x253450,64);CHECK();
    return packet(r,style,screen,rgba,r->sp+0x50,8,7,0x80,UINT64_C(0xA400000000008001),UINT64_C(0x4444444410),0x144);
}
