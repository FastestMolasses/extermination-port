#include "game/em_aim_fire_pose.h"
#include "game/em_ee_float.h"
#include <string.h>

#define HALF 0x3F000000u
#define ONE 0x3F800000u
#define TWO 0x40000000u
#define SCRATCH 0x70003A20u
#define BONES 0x00275B40u
#define CHECK(x) do { if ((x) < 0) return -1; } while (0)

static void *view(EmAimFirePose *h, uint32_t at, size_t size, int write)
{
    if (h->fault) return NULL;
    void *p = h->map ? h->map(h->context, at, size, write) : NULL;
    if (!p) h->fault = at ? at : 0x0017A130u;
    return p;
}
static uint32_t rd(EmAimFirePose *h, uint32_t at, unsigned size)
{
    uint32_t word = 0;
    const uint8_t *p = view(h, at, size, 0);
    if (p) for (unsigned i = 0; i < size; ++i) word |= (uint32_t)p[i] << (8*i);
    return word;
}
static void wr(EmAimFirePose *h, uint32_t at, uint32_t word, unsigned size)
{
    uint8_t *p = view(h, at, size, 1);
    if (p) for (unsigned i = 0; i < size; ++i) p[i] = (uint8_t)(word >> (8*i));
}
static int call(EmAimFirePose *h, uint32_t at, uint32_t a, uint32_t b,
                uint32_t c, uint32_t f, uint32_t g, uint32_t *result)
{
    uint32_t args[3] = {a,b,c}, floats[2] = {f,g}, ignored;
    if (h->fault) return -1;
    if (!h->call || h->call(h->context, at, args, floats, result ? result : &ignored) < 0) {
        h->fault = at;
        return -1;
    }
    return h->fault ? -1 : 0;
}
static int pose(EmAimFirePose *h, uint32_t p, unsigned slot, uint32_t dest)
{
    uint32_t clip;
    CHECK(call(h, 0x0017A0B0u, p, slot, 0, 0, 0, &clip));
    clip = (uint32_t)(int32_t)(int16_t)clip;
    return call(h, 0x00179BC0u, p, clip, dest, 0, 0, NULL);
}
static int blend_bank(EmAimFirePose *h, uint32_t p, uint32_t dest)
{
    for (unsigned i=0; i<rd(h,p+0xCu,1); ++i) {
        uint32_t t = em_ee_mul_bits(TWO, rd(h,SCRATCH,4));
        CHECK(call(h,0x00179CA0u,dest+64*i,0x00288D40u+64*i,
                   0x00287F40u+64*i,t,0,NULL));
    }
    return h->fault ? -1 : 0;
}
static int plane(EmAimFirePose *h, uint32_t p, unsigned center,
                 unsigned low, unsigned high, uint32_t dest)
{
    uint32_t x = rd(h,p+0x278u,4);
    if (em_ee_c_le_bits(x,0)) return pose(h,p,low,dest);
    if (!em_ee_c_lt_bits(x,ONE)) return pose(h,p,high,dest);
    CHECK(pose(h,p,center,0x00288D40u));
    x = rd(h,p+0x278u,4);
    if (em_ee_c_le_bits(x,HALF)) {
        wr(h,SCRATCH,em_ee_sub_bits(HALF,x),4);
        CHECK(pose(h,p,low,0x00287F40u));
    } else {
        wr(h,SCRATCH,em_ee_sub_bits(x,HALF),4);
        CHECK(pose(h,p,high,0x00287F40u));
    }
    return blend_bank(h,p,dest);
}
static int dispatch(EmAimFirePose *h, uint32_t p)
{
    CHECK(plane(h,p,0,2,1,0x00287140u));
    if (em_ee_c_le_bits(rd(h,p+0x27Cu,4),HALF))
        CHECK(plane(h,p,4,8,7,0x00286340u));
    else CHECK(plane(h,p,3,6,5,0x00286340u));
    uint32_t y = rd(h,p+0x27Cu,4), t;
    if (em_ee_c_le_bits(y,0) || !em_ee_c_lt_bits(y,ONE)) t=HALF;
    else if (em_ee_c_le_bits(y,HALF)) t=em_ee_sub_bits(HALF,y);
    else t=em_ee_sub_bits(y,HALF);
    wr(h,SCRATCH,t,4);
    for (unsigned i=0; i<rd(h,p+0xCu,1); ++i) {
        uint32_t table=rd(h,BONES,4), node=rd(h,table+4*i,4);
        t=em_ee_mul_bits(TWO,rd(h,SCRATCH,4));
        CHECK(call(h,0x00179CA0u,node+0x90u,0x00287140u+64*i,
                   0x00286340u+64*i,t,0,NULL));
    }
    for (unsigned i=0; i<rd(h,p+0xCu,1); ++i)
        for (unsigned j=0; j<3; ++j) {
            uint32_t table=rd(h,BONES,4), node=rd(h,table+4*i,4);
            uint32_t at=node+0x90u+16*j;
            CHECK(call(h,0x00102760u,at,at,0,0,0,NULL));
        }
    wr(h,p+0x303u,1,1);
    uint32_t clip;
    CHECK(call(h,0x0017A0B0u,p,0,0,0,0,&clip));
    uint32_t frame=em_ee_cvt_s_w_bits((uint32_t)(int32_t)(int16_t)rd(h,p+0x276u,2));
    return call(h,0x001749F0u,p,clip,0,0,frame,NULL);
}

int em_aim_fire_pose_run(EmAimFirePose *h, uint32_t entry,
                        uint32_t a, uint32_t b, uint32_t c,
                        uint32_t f, uint32_t *result)
{
    if (!h || h->fault) return -1;
    switch (entry) {
    case 0x0017A130u: return dispatch(h,a);
    case 0x0017A0B0u: {
        unsigned state=rd(h,a+5,1);
        uint32_t table=(state==0x1D || state==0x1E) ? 0x00248B70u : 0x00248C50u;
        uint32_t row=rd(h,table+4*rd(h,a+0x275u,1),4);
        uint32_t clip=(uint32_t)(int32_t)(int16_t)rd(h,row+2*b,2);
        if (result && !h->fault) *result=clip;
        break;
    }
    case 0x00179BC0u: {
        unsigned state=rd(h,a+0x1F0u,1);
        uint32_t frame=(state==0x31 || state==0x34)
            ? em_ee_cvt_s_w_bits((uint32_t)(int32_t)(int16_t)rd(h,a+0x276u,2)) : ONE;
        CHECK(call(h,0x001749F0u,a,b,0,0,frame,NULL));
        CHECK(call(h,0x001C6DA0u,a,0,0,0,0,NULL));
        for (unsigned i=0; i<rd(h,a+0xCu,1); ++i) {
            uint32_t table=rd(h,BONES,4), node=rd(h,table+4*i,4);
            CHECK(call(h,0x00102958u,c+64*i,node+0x90u,0,0,0,NULL));
        }
        break;
    }
    case 0x00179CA0u: {
        uint32_t omt=em_ee_sub_bits(ONE,f);
        for (unsigned i=0;i<4;++i)
            for (unsigned j=0;j<3;++j) {
                unsigned off=16*i+4*j;
                uint32_t x=rd(h,b+off,4), y=rd(h,c+off,4);
                uint32_t acc=em_ee_mula_bits(omt,x);
                wr(h,a+off,em_ee_madd_bits(acc,f,y),4);
            }
        wr(h,a+12,0,4); wr(h,a+28,0,4); wr(h,a+44,0,4); wr(h,a+60,ONE,4);
        break;
    }
    default: h->fault=entry; return -1;
    }
    return h->fault ? -1 : 0;
}
