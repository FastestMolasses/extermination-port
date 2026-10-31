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
    default:
        /* The post-sample core emits only the four above; the cue and the
         * event tracks are this module's own (events() below). */
        if(!f->fault)f->fault=0x0022EEF0u;break;
    }
    return f->fault ? 0 : 1;
}
#define T_AT(x) em_ee_c_eq_bits(time,em_ee_bits(x))
#define T_GE(x) em_ee_c_le_bits(em_ee_bits(x),time)
#define T_LE(x) em_ee_c_le_bits(time,em_ee_bits(x))
#define T_LT(x) em_ee_c_lt_bits(time,em_ee_bits(x))
/* D_008106C8's bar bits (mask 0xF1FFFF8F). */
static void bars(Frame *f,uint32_t set)
{
    uint32_t v=read32(f,0x008106C8u)&0xF1FFFF8Fu;
    write32(f,0x008106C8u,v);
    if(set)write32(f,0x008106C8u,v|set);
}
static void cue(Frame *f,uint32_t id,uint32_t frames) { call(f,0x001B1E20u,2,id,frames,0,0,0); }
/* 0022EEF0's leading switch (byte-matched structure of func_0022EEF0.c):
 * the per-scene script at its time stamps. Scene 27's 0011E0A8 stretch
 * (3852..3960) is not on any recorded route: it faults. */
static void scene_script(Frame *f,int scene)
{
    const uint32_t time=read32(f,f->cam+0x74);
    switch(scene) {
    case 15: if(T_AT(1330.0f))cue(f,6,0);break;
    case 22: if(T_AT(573.0f))cue(f,7,0x32);else if(T_AT(648.0f))cue(f,3,0xA);break;
    case 24:
        if(T_AT(105.0f))cue(f,6,0);
        if((T_GE(0.0f) && T_LE(263.0f)) || (T_GE(384.0f) && T_LE(655.0f)) || (T_GE(910.0f) && T_LE(1159.0f)))
            bars(f,0x02000000u);
        else bars(f,0x10u);
        break;
    case 25: if(T_AT(230.0f))cue(f,2,0x32);else if(T_AT(316.0f))cue(f,3,0xA0);break;
    case 26:
        if(T_AT(2670.0f)) {
            write32(f,0x008106C8u,read32(f,0x008106C8u)|0x40u);
            call(f,0x001D2830u,2,8,0,0,0,0);
            call(f,0x001C1DC0u,0,0,0,0,0,0);
        }
        break;
    case 27:
        if(T_AT(2431.0f))cue(f,1,0x28);
        else if(T_AT(3491.0f))cue(f,5,0xA0);
        else if(T_AT(3580.0f))cue(f,8,0x46);
        else if(T_GE(3852.0f) && T_LT(3960.0f))f->fault=0x0011E0A8u;
        else if(T_AT(4260.0f))cue(f,4,0x32);
        else if(T_AT(4508.0f) || T_AT(4609.0f))cue(f,2,0xF);
        break;
    case 29:
        if(T_AT(777.0f))cue(f,2,0x32);
        if(T_AT(0.0f))bars(f,0x02000000u);
        break;
    case 30: if(T_AT(31.0f))cue(f,2,0x50);else if(T_AT(75.0f))cue(f,1,0x334);break;
    case 31: if(T_AT(1.0f))cue(f,4,0x30C);break;
    case 33:
        if((T_GE(200.0f) && T_LE(300.0f)) || (T_GE(360.0f) && T_LE(560.0f)))bars(f,0x02000000u);
        else if(T_GE(635.0f) && T_LE(700.0f))bars(f,0);
        else bars(f,0x02000000u);
        break;
    case 34: if(T_AT(1.0f))cue(f,6,0);break;
    case 35: if(T_AT(50.0f))cue(f,6,0);break;
    default: break;
    }
}
/* The tracks 0022EC30 and 0022EEF0 walk: float words in the boot data. */
static int track_ok(Frame *f,uint32_t p)
{
    if(p && (p<0x0026AAE0u || p>=0x0026AE40u || (p&3))) { f->fault=p;return 0; }
    return 1;
}
static void events(Frame *f,int scene)
{
    uint32_t d=f->cam;
    scene_script(f,scene);
    /* d+0x7C (d+0x88): a record's time, then with state 0 a level: -1
     * 0021BA80(0x80, 0x80, 0x80) and state 2, else 0021B9A0(5, 0, v) and
     * state 1; with state 1 0021B9A0(0, 0, 0), else 0021BA70(the scene's
     * start); state 0. A 0.0 next word ends the track. */
    uint32_t q=read32(f,d+0x7C);
    if(!track_ok(f,q))return;
    if(q && !em_ee_c_lt_bits(read32(f,d+0x74),read32(f,q))) {
        q+=4;
        const uint8_t state=read8(f,d+0x88);
        if(state==0) {
            const uint32_t v=read32(f,q);
            q+=4;
            if(em_ee_c_eq_bits(v,em_ee_bits(-1.0f))) {
                EmArea01Call c={.function=0x0021BA80u,.sp=f->sp,.na=3,.a={0x80,0x80,0x80}};
                if(!f->fault && f->h->worker(f->h->ctx,&c)<0)f->fault=c.function;
                write8(f,d+0x88,2);
            } else {
                call(f,0x0021B9A0u,1,5,0,2,0,v);
                write8(f,d+0x88,1);
            }
        } else {
            if(state==1)call(f,0x0021B9A0u,1,0,0,2,0,0);
            else {
                uint64_t start=0;
                const uint8_t *b=bytes(f,0x00275C98u,8,0);
                if(b)memcpy(&start,b,8);
                EmArea01Call c={.function=0x0021BA70u,.sp=f->sp,.na=1,.a={start}};
                if(!f->fault && f->h->worker(f->h->ctx,&c)<0)f->fault=c.function;
            }
            write8(f,d+0x88,0);
        }
        write32(f,d+0x7C,em_ee_c_eq_bits(read32(f,q),0) ? 0 : q);
    }
    if(f->fault)return;
    uint32_t p=read32(f,d+0x80);
    if(!track_ok(f,p))return;
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
    if(!track_ok(f,p))return;
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
    /* 0022EC30's jump table covers 0..0x25; another id sets up nothing and
     * no recorded scene has one: it faults. */
    if(scene<0 || scene>0x25)f.fault=f.cam+0x6E;
    if(!f.fault && c->function==0x0022EC30u) {
        uint64_t start=call(&f,0x0021BAB0u,0,0,0,0,0,0);
        store(&f,0x00275C98u,&start,sizeof start);
        write32(&f,f.cam+0x7C,0);write32(&f,f.cam+0x80,0);write32(&f,f.cam+0x84,0);
        write8(&f,f.cam+0x88,0);write8(&f,f.cam+0x8A,0);write8(&f,f.cam+0x89,0);
        /* The scene's tracks (func_0022EC30.c's table), in the original's
         * store order; a zero leaves the cursor cleared. */
        static const struct { uint8_t scene; uint32_t a,b; uint8_t ao,bo; } k[]={
            {2,0x0026AAE0u,0x0026AC80u,0x84,0x80},{5,0x0026ACA0u,0,0x80,0},{6,0x0026AB00u,0x0026AC80u,0x84,0x80},
            {7,0x0026ACB0u,0,0x80,0},{8,0x0026ACC0u,0,0x80,0},{10,0x0026ACD0u,0,0x80,0},
            {12,0x0026ACF0u,0x0026AB18u,0x80,0x84},{13,0x0026AD10u,0,0x80,0},{15,0x0026AD20u,0,0x80,0},
            {16,0x0026AB28u,0,0x84,0},{17,0x0026AB38u,0,0x84,0},{19,0x0026AD30u,0x0026AC10u,0x80,0x7C},
            {21,0x0026AB60u,0,0x84,0},{23,0x0026AD70u,0,0x80,0},{24,0x0026AD80u,0x0026AB78u,0x80,0x84},
            {25,0x0026AD90u,0x0026AB88u,0x80,0x84},{26,0x0026ADA0u,0x0026AB98u,0x80,0x84},{27,0x0026ABA8u,0,0x84,0},
            {30,0x0026ABC8u,0x0026AC50u,0x84,0x7C},{31,0x0026ADD0u,0x0026ABE0u,0x80,0x84},
            {32,0x0026ADF0u,0x0026AC70u,0x80,0x7C},{33,0x0026AC60u,0x0026AC00u,0x7C,0x84},{34,0x0026AE00u,0,0x80,0},
            {37,0x0026AD60u,0,0x80,0}};
        for(unsigned i=0;i<sizeof k/sizeof k[0];++i)if(k[i].scene==scene) {
            write32(&f,f.cam+k[i].ao,k[i].a);
            if(k[i].b)write32(&f,f.cam+k[i].bo,k[i].b);
        }
        /* the two scenes with three tracks */
        if(scene==20) {
            write32(&f,f.cam+0x80,0x0026AD40u);write32(&f,f.cam+0x7C,0x0026AC30u);write32(&f,f.cam+0x84,0x0026AB48u);
        } else if(scene==28) {
            write32(&f,f.cam+0x80,0x0026ADB0u);write32(&f,f.cam+0x7C,0x0026AC40u);write32(&f,f.cam+0x84,0x0026ABB8u);
        }
    } else if(!f.fault)tick(&f,scene);
    if(f.fault) { if(fault)*fault=f.fault;return -1; }
    return 0;
}
