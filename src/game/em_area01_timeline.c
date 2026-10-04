#include "game/em_area01_timeline.h"
#include "game/em_cinematic_playback.h"
#include "game/em_ee_float.h"
#include "game/em_stream_lanes_original.h"
#include <math.h>
#include <string.h>

typedef struct { const EmArea01RuntimeHost *h;uint32_t sp,fault,cam; } Frame;
static uint8_t *bytes(Frame *f,uint32_t a,uint32_t n,int write)
{
    if(f->fault)return NULL;
    uint8_t *p=f->h->bytes(f->h->ctx,a,n,write);
    if(!p)f->fault=a;
    return p;
}
static uint32_t read32(Frame *f,uint32_t a)
{ uint32_t v=0;uint8_t *p=bytes(f,a,4,0);if(p)memcpy(&v,p,4);return v; }
static uint8_t read8(Frame *f,uint32_t a)
{ uint8_t *p=bytes(f,a,1,0);return p ? *p : 0; }
static void write32(Frame *f,uint32_t a,uint32_t v)
{ uint8_t *p=bytes(f,a,4,1);if(p)memcpy(p,&v,4); }
static void write8(Frame *f,uint32_t a,uint8_t v)
{ uint8_t *p=bytes(f,a,1,1);if(p)*p=v; }
static void store(Frame *f,uint32_t a,const void *v,uint32_t n)
{ uint8_t *p=bytes(f,a,n,1);if(p)memcpy(p,v,n); }
static uint64_t call(Frame *f,uint32_t fn,unsigned na,uint32_t a,uint32_t b,
                     unsigned nf,uint32_t x,uint32_t y)
{
    EmArea01Call c={.function=fn,.sp=f->sp,.na=na,.nf=nf,
        .a={(uint64_t)(int64_t)(int32_t)a,(uint64_t)(int64_t)(int32_t)b},.f={x,y}};
    if(!f->fault && f->h->worker(f->h->ctx,&c)<0)f->fault=fn;
    return c.v0;
}
static int emit(void *ctx,EmCinematicPlaybackEvent event,const EmCinematicPlayback *pb)
{
    Frame *f=ctx;
    switch(event) {
    case EM_CINEMATIC_CAMERA_PUBLISH:
        store(f,0x008105D0u,pb->eye,12);store(f,0x008105E0u,pb->target,12);
        call(f,0x001DD980u,2,0x008105D0u,0x008105E0u,0,0,0);break;
    case EM_CINEMATIC_CAMERA_RESTORE_ROOM:
        call(f,0x001B0250u,0,0,0,0,0,0);break;
    case EM_CINEMATIC_CAMERA_EFFECT_OFF:
        call(f,0x0021B9A0u,1,0,0,2,0,0);break;
    case EM_CINEMATIC_CAMERA_FLAG_OFF:
        call(f,0x001D2830u,2,2,0,0,0,0);break;
    }
    return f->fault ? 0 : 1;
}
static int cursor(Frame *f,uint32_t a,uint32_t lo,uint32_t size)
{
    if(a && (a<lo || a-lo>=size || (a&3))) { f->fault=a;return 0; }
    return 1;
}
static void events(Frame *f,int scene)
{
    uint32_t d=f->cam;
    if(scene==35 && em_ee_c_eq_bits(read32(f,d+0x74),0x42480000u))
        call(f,0x001B1E20u,2,6,0,0,0,0);
    /* These two scenes have no audio track. A foreign cursor is an
     * unsupported timeline, never an apparently successful empty event. */
    if(read32(f,d+0x7C)) { f->fault=d+0x7C;return; }
    uint32_t p=read32(f,d+0x80);
    if(!cursor(f,p,0x0026AC80u,0x20))return;
    if(p) {
        uint32_t t=read32(f,p);
        if(em_ee_c_lt_bits(t,0)) {
            const uint32_t marker[4]={0xC0400000u,0xC0800000u,0xBF800000u,0xC0000000u};
            const uint8_t state[4]={0,0x80,1,0x81};
            for(unsigned i=0;i<4;++i)if(em_ee_c_eq_bits(t,marker[i]))write8(f,d+0x89,state[i]);
            write32(f,d+0x80,p+4);
        } else if(!em_ee_c_lt_bits(read32(f,d+0x74),t)) {
            uint8_t flag=read8(f,d+0x89);
            int32_t value=em_stream_lanes_001281C0(read32(f,p+4));
            call(f,(flag&1) ? 0x001AEDE0u : 0x001AEE10u,2,(uint32_t)value,(flag&0x80) ? 1u : 0u,0,0,0);
            flag=read8(f,d+0x89);
            write8(f,d+0x89,(uint8_t)((flag&0x80)+1-(flag&1)));
            write32(f,d+0x80,em_ee_c_eq_bits(read32(f,p+8),0) ? 0 : p+8);
        }
    }
    p=read32(f,d+0x84);
    if(!cursor(f,p,0x0026AAE0u,0x20))return;
    if(p && !em_ee_c_lt_bits(read32(f,d+0x74),read32(f,p))) {
        call(f,0x001D2830u,2,2,read8(f,d+0x8A)==0 ? 1u : 0u,0,0,0);
        write8(f,d+0x8A,(uint8_t)(1-read8(f,d+0x8A)));
        write32(f,d+0x84,em_ee_c_eq_bits(read32(f,p+4),0) ? 0 : p+4);
    }
}
static void tick(Frame *f,int scene)
{
    events(f,scene);
    if(f->fault)return;
    uint32_t d=f->cam,a=read32(f,d+0x70),duration_bits=read32(f,a);
    float duration=em_ee_float(duration_bits),time=em_ee_float(read32(f,d+0x74));
    if(a>UINT32_MAX-16u || !isfinite(duration) || duration<1 || duration>65535 || duration!=(uint32_t)duration ||
       !isfinite(time) || read32(f,d+0x78)!=duration_bits) { f->fault=a;return; }
    uint32_t count=(uint32_t)duration+1;
    uint8_t *samples=bytes(f,a+16,count*32,0);
    if(!samples)return;
    EmCinematicCamera track={(float *)(void *)samples,count,duration};
    EmCinematicCameraFrame sample;
    int active=em_cinematic_camera_sample(&track,time,&sample);
    if(active<0) { f->fault=a;return; }
    /* The sampler's result is contiguous eight floats, excluding cut. */
    store(f,0x008234C0u,&sample,32);
    if(active) {
        write8(f,0x008106F3u,(uint8_t)sample.cut);
        if(sample.cut)write32(f,0x00275BFCu,32);
    } else write32(f,d+0x74,duration_bits);
    EmCinematicProjection projection;
    const uint8_t *coeff=bytes(f,0x0026C598u,sizeof projection,0);
    if(!coeff)return;
    memcpy(&projection,coeff,sizeof projection);
    EmCinematicPlayback pb={.track=&track,.time=time};
    float rotation[16];
    int rc=em_cinematic_playback_sampled_tick(&pb,&projection,emit,f,&sample,active,rotation);
    if(rc<0) { if(!f->fault)f->fault=0x0022EEF0u;return; }
    if(rc) {
        /* The common core executes the existing SDK identity/rotation/VU0
         * translations. Publish their canonical scratch output as well. */
        const float down[4]={0,-1,0,1};
        store(f,0x70003400u,rotation,sizeof rotation);store(f,0x70003600u,down,sizeof down);
        store(f,0x008105F0u,pb.up,sizeof pb.up);
    }
    call(f,0x001D25F0u,0,0,0,1,em_ee_bits(pb.zoom),0);
    if(rc)write32(f,d+0x74,em_ee_bits(pb.time));
    else store(f,0x008105F0u,pb.up,sizeof pb.up);
}
int em_area01_timeline_call(const EmArea01RuntimeHost *h,EmArea01Call *c,uint32_t *fault)
{
    if(!h || !h->bytes || !h->worker || !c || c->na<1 ||
       (c->function!=0x0022EC30u && c->function!=0x0022EEF0u))return -1;
    Frame f={h,c->sp-(c->function==0x0022EC30u ? 0x20u : 0x30u),0,(uint32_t)c->a[0]};
    const uint8_t *p=bytes(&f,f.cam+0x6E,2,0);int16_t scene=0;
    if(p)memcpy(&scene,p,2);
    if(scene!=2 && scene!=35)f.fault=f.cam+0x6E;
    if(!f.fault && c->function==0x0022EC30u) {
        uint64_t start=call(&f,0x0021BAB0u,0,0,0,0,0,0);
        store(&f,0x00275C98u,&start,sizeof start);
        write32(&f,f.cam+0x7C,0);write32(&f,f.cam+0x80,0);write32(&f,f.cam+0x84,0);
        write8(&f,f.cam+0x88,0);write8(&f,f.cam+0x8A,0);write8(&f,f.cam+0x89,0);
        if(scene==2) { write32(&f,f.cam+0x84,0x0026AAE0u);write32(&f,f.cam+0x80,0x0026AC80u); }
    } else if(!f.fault)tick(&f,scene);
    if(f.fault) { if(fault)*fault=f.fault;return -1; }
    return 0;
}
