/* Native composition checks: the original workers have separate instruction
 * oracles. These checks cover canonical views, recursive dispatch, argument
 * bridges and failure propagation at their integration seam. */
#include "game/em_aim_fire_live.h"
#include "game/em_ee_float.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    EmAimFireLive live;
    EmPlayerLiveActor player;
    uint8_t req[0x100], progress[0x200], rows[0x200], scratch[16], vec[12];
    uint8_t shot_scratch[0x70], hit[0x40], primary[0x40], secondary[0x100];
    EmPoseRegion region[10];
    unsigned calls;
    uint32_t entries[32];
    int fail;
} Fixture;
static void setup(Fixture *);
static void put(uint8_t *p,uint32_t v) { memcpy(p,&v,4); }
static uint32_t get(const uint8_t *p) { uint32_t v;memcpy(&v,p,4);return v; }
static void *permissive_map(void *context,uint32_t address,size_t size,int write)
{
    Fixture *x=context;(void)address;(void)size;(void)write;
    ++x->calls;return x->rows;
}
static void *failed_map(void *context,uint32_t address,size_t size,int write)
{
    Fixture *x=context;(void)size;(void)write;
    x->live.fault_address=address;return x->rows;
}
static int boundary(void *context,EmAimFireTargetCall *f)
{
    Fixture *x=context;
    assert(x->calls<32); x->entries[x->calls++]=f->function;
    if (x->fail) return -1;
    switch(f->function) {
    case 0x1C61D0:
        assert(f->na==2 && !f->nf && f->a[0]==0x12345678 && f->a[1]==321);
        assert(x->player.bytes[0x274]==1);
        x->player.bytes[0x275]=4; f->v0=29; return 0;
    case 0x1FBD50:
        assert(f->na==3 && f->nf==1 && f->a[0]==0x8102B0 && !f->a[2] && f->f[0]==0x43960000);
        if (x->calls==1) { assert(f->a[1]==0x162);x->progress[0xA6]=0;x->progress[0x13C]=1; }
        else assert(f->a[1]==0x179);
        return 0;
    case 0x11E860: {
        assert(f->na==1 && !f->nf);
        int32_t a=(int32_t)f->a[0];f->v0=a<0 ? (uint32_t)(-a) : (uint32_t)a;return 0;
    }
    case 0x11E748: assert(f->na==0 && f->nf==1);f->f0=em_ee_bits(3.0f);return 0;
    case 0x11E620:
        assert(f->na==0 && f->nf==2 && f->f[0]==em_ee_bits(3.0f) && f->f[1]==em_ee_bits(2.0f));
        f->f0=em_ee_bits(.75f);return 0;
    case 0x1B1510:assert(f->f[0]==em_ee_bits(.75f));f->f0=em_ee_bits(1.25f);return 0;
    case 0x103230:assert(f->na==2 && f->nf==1 && f->f[0]==0x43820000);return 0;
    case 0x1028B8: case 0x1028D0:assert(f->na==3 && !f->nf);return 0;
    case 0x102948: case 0x1031E0:assert(f->na==2 && !f->nf);return 0;
    case 0x19A570:assert(f->na==4 && !f->nf);f->v0=1;return 0;
    case 0x19B6C0:assert(f->na==2 && !f->nf);f->v0=0;return 0;
    case 0x1B41F0:
        assert(f->na==6 && !f->nf && f->a[0]==0x6A0000 && f->a[1]==0x700038A0);
        assert(f->a[2]==0x810370 && f->a[3]==0xC0FFEE && f->a[4]==0 && f->a[5]==5);
        f->v0=1;return 0;
    case 0x1AFA90:assert(f->na==1 && !f->nf && f->a[0]==1);f->v0=0;return 0;
    default:return -1;
    }
}
static void setup(Fixture *x)
{
    memset(x,0,sizeof *x);
    x->live.context=x; x->live.player=&x->player; x->live.player_address=0x8102B0;
    x->live.regions=x->region; x->live.region_count=5; x->live.call=boundary;
    x->region[0]=(EmPoseRegion){0x810600,sizeof x->req,x->req,1};
    x->region[1]=(EmPoseRegion){0x810C00,sizeof x->progress,x->progress,1};
    x->region[2]=(EmPoseRegion){0x248B00,sizeof x->rows,x->rows,0};
    x->region[3]=(EmPoseRegion){0x70003A20,sizeof x->scratch,x->scratch,1};
    x->region[4]=(EmPoseRegion){0x680000,sizeof x->vec,x->vec,1};
}
static int run(Fixture *x,uint32_t entry,uint32_t a,uint32_t b,EmAimFireTargetCall *f)
{
    memset(f,0,sizeof *f);f->function=entry;f->a[0]=a;f->a[1]=b;
    return em_aim_fire_live_call(&x->live,f);
}
int main(void)
{
    Fixture x; EmAimFireTargetCall f;setup(&x);
    x.player.bytes[5]=0x1D;put(x.player.bytes+0x40,0x12345678);
    x.progress[0xA4]=2;put(x.rows+0x70+5*4,0x248B00);x.rows[0]=0x41;x.rows[1]=1;
    assert(run(&x,0x17A970,0x8102B0,0,&f)==0 && f.v0==1);
    assert(x.calls==1 && x.entries[0]==0x1C61D0 && x.player.bytes[0x275]==5);
    assert(get(x.player.bytes+0x2F4)==em_ee_bits(29.0f));
    setup(&x);x.progress[0xA6]=3;
    assert(run(&x,0x16F530,0x8102B0,0,&f)==0 && x.calls==2 && x.req[0xC7]==1);
    setup(&x);assert(run(&x,0x1B5DC0,255,0,&f)==0 && f.v0==3 && x.calls==1);
    setup(&x);put(x.vec+4,em_ee_bits(2.0f));
    assert(run(&x,0x17A800,0x8102B0,0x680000,&f)==0 && f.f0==em_ee_bits(1.25f));
    assert(x.calls==3 && get(x.scratch)==em_ee_bits(.75f));
    setup(&x);x.fail=1;x.req[0xC7]=9;
    assert(run(&x,0x16F530,0x8102B0,0,&f)<0 && x.calls==1);
    assert(x.live.fault_function==0x1FBD50 && x.player.bytes[0x1F1]==1 && x.req[0xC7]==9);
    assert(run(&x,0x16F5D0,0x8102B0,0,&f)<0 && x.player.bytes[0x1F1]==1);
    assert(em_aim_fire_live_clear_fault(&x.live)==0);
    assert(run(&x,0x16F5D0,0x8102B0,0,&f)==0 && !x.req[0xC7]);
    setup(&x);assert(em_aim_fire_live_map(&x.live,0x248B00,4,1)==NULL);
    assert(x.live.fault_address==0x248B00);
    setup(&x);x.live.map=permissive_map;
    assert(em_aim_fire_live_map(&x.live,0x248B00,4,1)==NULL && x.calls==0);
    setup(&x);x.live.map=permissive_map;
    assert(em_aim_fire_live_map(&x.live,0x248AF0,32,0)==NULL && x.calls==0);
    setup(&x);x.live.map=permissive_map;
    assert(em_aim_fire_live_map(&x.live,0x8102A0,32,0)==NULL && x.calls==0);
    setup(&x);assert(em_aim_fire_live_map(&x.live,0x680000,SIZE_MAX,0)==NULL && x.live.fault_address==0x680000);
    setup(&x);x.live.map=failed_map;
    assert(em_aim_fire_live_map(&x.live,0xDEADBEEF,4,0)==NULL && x.live.fault_address==0xDEADBEEF);
    setup(&x);assert(run(&x,0xDEADBEEF,0,0,&f)<0 && x.live.fault_function==0xDEADBEEF);
    setup(&x);assert(run(&x,0,0,0,&f)<0 && x.live.fault_function==UINT32_MAX && x.calls==1);
    assert(run(&x,0x1B5DC0,255,0,&f)<0 && x.calls==1);
    assert(em_aim_fire_live_map(&x.live,0x680000,4,0)==NULL);
    assert(em_aim_fire_live_clear_fault(&x.live)==0);
    assert(run(&x,0x1B5DC0,255,0,&f)==0 && f.v0==3 && x.calls==2);
    setup(&x);x.live.depth=16;
    assert(run(&x,0,0,0,&f)<0 && x.live.fault_function==UINT32_MAX && !x.calls && x.live.depth==16);
    assert(em_aim_fire_live_clear_fault(&x.live)<0);
    x.live.depth=0;
    assert(run(&x,0x1B5DC0,255,0,&f)<0 && !x.calls);
    assert(em_aim_fire_live_clear_fault(&x.live)==0);
    assert(run(&x,0x1B5DC0,255,0,&f)==0 && f.v0==3 && x.calls==1);
    setup(&x);x.live.region_count=9;
    x.region[5]=(EmPoseRegion){0x700038A0,sizeof x.shot_scratch,x.shot_scratch,1};
    x.region[6]=(EmPoseRegion){0x700031B0,sizeof x.hit,x.hit,1};
    x.region[7]=(EmPoseRegion){0x690000,sizeof x.primary,x.primary,1};
    x.region[8]=(EmPoseRegion){0x6A0000,sizeof x.secondary,x.secondary,1};
    put(x.hit+0x20,0x690000);put(x.hit+0x24,0x6A0000);put(x.primary+0x1C,0xC0FFEE);
    x.secondary[0]=1;x.secondary[2]=2;
    assert(run(&x,0x1861C0,0x8102B0,0,&f)==0 && f.v0==0);
    assert(x.calls==9 && x.entries[7]==0x1B41F0 && get(x.hit+0x38)==UINT32_MAX);
    puts("aim/fire live seam: PASS (recursive owners, canonical writes, six-argument shots, fault stops)");
    return 0;
}
