/* Canonical live wrappers with explicit random, range and sprite boundaries.
 * The Python companion compares those boundaries with original instructions. */
#define main earlier_effects_fixture_main
#include "aim_fire_effects_live_test.c"
#undef main
#include "game/em_area01_effects_services.h"

static uint8_t memory[0x100];
static uint32_t events[16][16], counts[16], nevents;
static int refuse_write;
static uint8_t *view(void *ctx,uint32_t a,uint32_t n,int write)
{
    (void)ctx;
    if(write && refuse_write)return NULL;
    return a>=0x1E00000u && n<=sizeof memory && a-0x1E00000u<=sizeof memory-n
        ? memory+(a-0x1E00000u) : NULL;
}
static void event(const uint32_t *v,unsigned n)
{ assert(nevents<16 && n<=16);memcpy(events[nevents],v,n*4);counts[nevents++]=n; }
static int draw_rand(void *ctx,int32_t *out)
{
    (void)ctx;const uint32_t v[]={0x122BB8,0x12345678};event(v,2);*out=0x12345678;return 0;
}
static int draw_range(void *ctx,int32_t mode,uint32_t f12,uint32_t f13)
{
    (void)ctx;const uint32_t v[]={0x21B9A0,(uint32_t)mode,f12,f13};event(v,4);return 0;
}
static int draw_sprite(void *ctx,int32_t a0,int32_t a1,uint32_t pos,uint64_t tex0,
                        uint32_t rgb,uint32_t f12,uint32_t f13,uint32_t f14)
{
    (void)ctx;assert(pos==MARKER_POSITION_HANDLE && S.marker_set);
    uint32_t v[]={0x1CD520,(uint32_t)a0,(uint32_t)a1,0,0,0,0,
                  (uint32_t)tex0,(uint32_t)(tex0>>32),rgb,f12,f13,f14};
    memcpy(v+3,S.marker,16);event(v,13);return 0;
}
static void draw_setup(void)
{
    setup();nevents=0;refuse_write=0;memset(memory,0xA5,sizeof memory);
    const float pos[4]={0,0,.5f,1}, matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,2,3,.5f,1};
    const uint32_t colour[4]={0x80,0x40,0x20,0x7F};
    memcpy(memory,pos,16);memcpy(memory+0x10,colour,16);memcpy(memory+0x20,matrix,64);
    S.kworkers.w_001F4D40=w_001F4D40;S.kworkers.w_0021B9A0=draw_range;
    S.mworkers.w_00122BB8=draw_rand;S.mworkers.w_001CD520=draw_sprite;
    S.m.workers=&S.mworkers;S.m.view=&S.mview;
}
static EmArea01Call draw_call(uint32_t fn,uint32_t kind)
{
    EmArea01Call c={.function=fn,.na=2,.nf=2,.a={0x1E00000,0x1E00010},
                   .f={0x40000000,0x40B00000}};
    if(fn==0x1F5940){c.na=3;c.nf=0;c.a[0]=kind;c.a[1]=0x1E00000;c.a[2]=0;}
    if(fn==0x1CCF70){c.na=1;c.nf=0;}
    if(fn==0x1CFA60){c.a[0]=0x1E00080;c.a[1]=0x1E00020;}
    return c;
}
static int invoke(EmArea01Call *c)
{
    EmArea01RuntimeHost h={.bytes=view};EmArea01EffectsDraw d;uint32_t fault=0;
    assert(em_area01_effects_draw_prepare(&h,c,&d,&fault)==0);
    memset(memory,0x3C,0x60); /* the prepared call owns no source pointer */
    int rc=em_area01_effects_draw_invoke(&d,c);
    assert(em_area01_effects_draw_finish(&h,&d,&fault)==0);
    return rc;
}
static void contracts(void)
{
    draw_setup();EmArea01Call c=draw_call(0x1CFA60,0);
    assert(invoke(&c)==0);
    for(unsigned i=0xD8;i<0x100;++i)assert(memory[i]==0xA5);
    draw_setup();S.hworkers.w_001CD370=NULL;c=draw_call(0x1CFA60,0);
    assert(invoke(&c)<0 && em_effects_live_fault()==0x1CD370);
    for(unsigned i=0x80;i<0xC4;++i)assert(memory[i]==0xA5);
    for(unsigned i=0xD8;i<0x100;++i)assert(memory[i]==0xA5);
    assert(rd32(memory+0xC4)==c.f[0] && rd32(memory+0xC8)==0x3F800000 &&
           rd32(memory+0xCC)==c.f[1] && rd32(memory+0xD0)==0x358637BD && rd32(memory+0xD4)==0);
    uint8_t prior[sizeof memory];memcpy(prior,memory,sizeof prior);assert(invoke(&c)<0);
    assert(!memcmp(memory,prior,sizeof prior));
    draw_setup();EmArea01RuntimeHost h={.bytes=view};EmArea01EffectsDraw d;uint32_t fault=0;
    c=draw_call(0x1F5940,9);c.na=2;assert(em_area01_effects_draw_prepare(&h,&c,&d,&fault)<0);
    c=draw_call(0x1F4D40,0);c.a[1]++;assert(em_area01_effects_draw_prepare(&h,&c,&d,&fault)<0);
    c=draw_call(0x1CFA60,0);c.a[0]=c.a[1];assert(em_area01_effects_draw_prepare(&h,&c,&d,&fault)<0);
    c=draw_call(0x1CFA60,0);assert(em_area01_effects_draw_prepare(&h,&c,&d,&fault)==0);
    assert(em_area01_effects_draw_invoke(&d,&c)==0);refuse_write=1;
    assert(em_area01_effects_draw_finish(&h,&d,&fault)<0 && fault==0x1E00080);
    draw_setup();c=draw_call(0x1F5940,3);S.kworkers.w_001F4D40=NULL;
    assert(invoke(&c)<0 && em_effects_live_fault()==0x1F4D40 && !nevents);
}
int main(int argc,char **argv)
{
    if(argc==3){
        draw_setup();EmArea01Call c=draw_call((uint32_t)strtoul(argv[1],NULL,16),
                                             (uint32_t)strtoul(argv[2],NULL,16));
        assert(invoke(&c)==0);
        printf("{\"v0\":%llu,\"events\":[",(unsigned long long)c.v0);
        for(unsigned i=0;i<nevents;++i){printf("%s[",i?",":"");
            for(unsigned j=0;j<counts[i];++j)printf("%s%u",j?",":"",events[i][j]);
            printf("]");}
        printf("],\"block\":\"");for(unsigned i=0x80;i<0xE0;++i)printf("%02x",memory[i]);
        printf("\",\"depth\":%d}",S.eglobals.d275C04);return 0;
    }
    contracts();puts("PASS AREA00 effects canonical prepare/invoke/publish, partial-store and fail-stop contracts");
    return 0;
}
