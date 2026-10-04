#include "game/em_area01_audio_services.h"
#include "game/em_player_misc_workers.h"
#include "game/em_script_host_workers.h"
#include "game/em_owner_services_original.h"
#include "game/em_coll_probe_original.h"
#include "game/em_effect_original.h"
#include "game/em_ee_float.h"
#include "game/em_sfx.h"
#include "game/em_sfx_bank.h"

#include <string.h>

typedef struct {
    const EmArea01RuntimeHost *host;
    EmSdkMathContext *sdk;
    EmScriptHostWorkers side;
    EmPlayerMiscScratch scratch;
    uint32_t sp;
} Audio;

static int load(const EmArea01RuntimeHost *h, uint32_t at, void *out, uint32_t n)
{
    const void *p = h->bytes(h->ctx, at, n, 0);
    if (!p) return -1;
    memcpy(out, p, n); return 0;
}
static int scratch(Audio *a, int publish)
{
    const uint32_t at[] = {0x70003400,0x70003600,0x70003610};
    const uint32_t size[] = {64,16,16};
    void *fields[] = {a->scratch.s3400,a->scratch.s3600,a->scratch.s3610};
    for (unsigned i=0;i<3;++i) {
        void *p=a->host->bytes(a->host->ctx,at[i],size[i],publish);
        if (!p) return -1;
        if (publish) memcpy(p,fields[i],size[i]); else memcpy(fields[i],p,size[i]);
    }
    return 0;
}
static int identity(void *c,float out[16])
{ (void)c;return em_owner_services_identity_001029C0(out)==0?0:-1; }
static int euler(void *c,float out[16],const float in[16],const float angles[4])
{ (void)c;return em_owner_services_euler_00102C58(out,in,angles)==0?0:-1; }
static int transform(void *c,float out[4],const float in[16],const float v[4])
{ (void)c;em_effect_original_001026A0(out,in,v);return 0; }
static int sub(void *c,float out[4],const float a[4],const float b[4])
{ (void)c;return em_coll_probe_sdk_sub(out,a,b); }
static int normalize(void *c,float out[4],const float in[4])
{ (void)c;em_effect_original_00102760(out,in);return 0; }
static int dot(void *c,const float a[4],const float b[4],float *out)
{ (void)c;return em_coll_probe_sdk_dot(out,a,b); }
static int sine(void *c,float in,float *out)
{ Audio *a=c;return em_sdk_math_original_w_0011E2A8(a->sdk,in,out); }
static int square_root(void *c,float in,float *out)
{ Audio *a=c;*out=em_sdk_math_original_float_0011E748(a->sdk,in);return a->sdk->fault?-1:0; }
static int side(void *c,const float from[4],const float to[4],float yaw,int32_t *out)
{ Audio *a=c;return em_script_host_w_001B1380(&a->side,from,to,yaw,out); }
static int submit(void *c,int32_t id,int32_t kind,int32_t left,int32_t right,int32_t *out)
{
    Audio *a=c;
    if (!a->host->worker || scratch(a,1)<0) return -1;
    EmArea01Call call={.function=0x001FB9F0,.sp=a->sp,.na=4,
        .a={(uint64_t)(int64_t)id,(uint64_t)(int64_t)kind,
            (uint64_t)(int64_t)left,(uint64_t)(int64_t)right}};
    if (a->host->worker(a->host->ctx,&call)<0) return -1;
    *out=(int32_t)(uint32_t)call.v0;
    /* The submit owns voices only; it must not mutate these scratch words. */
    return 0;
}
static int overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m)
{ return (uint64_t)a+n>b && (uint64_t)b+m>a; }
static uint32_t physical(uint32_t address)
{
    return (address>=0x20000000u && address<0x22000000u) ||
           (address>=0x30000000u && address<0x32000000u) ? address&0x1FFFFFFFu : address;
}
static int scalar_output(uint32_t out,uint32_t pos)
{
    out=physical(out);pos=physical(pos);
    if (out&3) return -1;
    const uint32_t sources[][2]={{pos,16},{0x810360,16},{0x81027C,4},{0x8105D0,16},
                                  {0x28215B,1},{0x70003400,64},{0x70003600,32}};
    for (unsigned i=0;i<sizeof sources/sizeof sources[0];++i)
        if (overlap(out,4,sources[i][0],sources[i][1])) return -1;
    return 0;
}

static int positional(const EmArea01RuntimeHost *h,EmSdkMathContext *sdk,const uint8_t *mode,
                      EmArea01Call *c,int32_t *left,int32_t *right,int32_t *out)
{
    Audio a={0};a.host=h;a.sdk=sdk;a.sp=c->sp;
    a.side.world.sdk_tables=sdk->tables;a.side.world.sdk_world=&sdk->world;a.side.world.sdk_workers=&sdk->workers;
    uint32_t pos=(uint32_t)c->a[0]+0xB0u;int gain=c->function==0x1FBF50;int32_t result=0;
    float position[4];EmPlayerMiscScene scene={0};scene.d28215B=*mode;
    if (load(h,pos,position,16)<0 || load(h,0x810360,scene.d810360,16)<0 ||
        load(h,0x81027C,&scene.d81027C,4)<0 || load(h,0x8105D0,scene.d8105D0,16)<0 ||
        scratch(&a,0)<0) return -1;
    /* Validate write permissions before invoking a routine that may store. */
    if (!h->bytes(h->ctx,0x70003400,64,1) || !h->bytes(h->ctx,0x70003600,16,1) ||
        !h->bytes(h->ctx,0x70003610,16,1)) return -1;
    EmPlayerMiscWorkers workers={.context=&a,.identity=identity,.euler=euler,.transform=transform,
        .vsub=sub,.normalize=normalize,.dot=dot,.sine=sine,.sqrt=square_root,.side=side,.submit=submit};
    EmPlayerMiscHost host={&workers,&scene,&a.scratch};
    int rc=gain ? em_player_misc_001FBF50(&host,position,left,right,(int32_t)(uint32_t)c->a[3],
                                          em_ee_float(c->f[0]),em_ee_float(c->f[1]),&result)
                : em_player_misc_001FBD50(&host,position,(int32_t)(uint32_t)c->a[1],
                                          (int32_t)(uint32_t)c->a[2],em_ee_float(c->f[0]),&result);
    int published=scratch(&a,1);
    if (rc<0 || published<0 || a.side.fault_address || sdk->fault) return -1;
    *out=result;return 0;
}
typedef struct {
    const EmArea01RuntimeHost *host;EmSdkMathContext *sdk;const uint8_t *mode;EmArea01Call *call;
} LoopAudio;
static int loop_gain(void *ctx,int updating,int32_t *left,int32_t *right)
{
    LoopAudio *l=ctx;EmArea01Call c={.function=0x1FBF50,.sp=l->call->sp,.na=4,.nf=2};
    c.a[0]=l->call->a[0];c.f[0]=l->call->f[0];c.f[1]=updating?l->call->f[1]:0x45800000u;
    uint32_t pos=(uint32_t)c.a[0]+0xB0u;
    if(!l->mode || !l->sdk || !l->sdk->tables || l->sdk->fault || (pos&15) ||
       pos<(uint32_t)c.a[0] || overlap(pos,16,0x70003400,64) || overlap(pos,16,0x70003600,32))return -1;
    int32_t result=0;
    if(positional(l->host,l->sdk,l->mode,&c,left,right,&result)<0)return -1;
    return result;
}
static int loop_store(void *ctx,int32_t value)
{
    LoopAudio *l=ctx;unsigned index=l->call->function==0x1FC520?0:1;
    uint8_t *p=l->host->bytes(l->host->ctx,(uint32_t)l->call->a[index],4,1);
    if(!p)return -1;memcpy(p,&value,4);return 0;
}
static int loop_call(const EmArea01RuntimeHost *h,EmSdkMathContext *sdk,const uint8_t *mode,EmArea01Call *c)
{
    int release=c->function==0x1FC520;
    if(!h || !h->bytes || c->na!=(release?1u:3u) || c->nf!=(release?0u:2u))return -1;
    uint32_t address=(uint32_t)c->a[release?0:1];int32_t handle,frame=0;int16_t ordinal=0;
    if((address&3) || load(h,address,&handle,4)<0 || handle < -1 || handle>=EM_SFX_TRACKS)return -1;
    if(!release && (load(h,0x70003B68,&frame,4)<0 || load(h,0x70003B8A,&ordinal,2)<0))return -1;
    LoopAudio loop={h,sdk,mode,c};
    return em_sfx_loop_service_bound(handle,release?0:(uint32_t)c->a[2],frame,ordinal,release,
                                      loop_gain,loop_store,&loop);
}

int em_area01_audio_services_call(const EmArea01RuntimeHost *h,EmSdkMathContext *sdk,
                                 const uint8_t *mode,EmArea01Call *c)
{
    if (!c) return -1;
    if(c->function==0x1FC3C0 || c->function==0x1FC520)return loop_call(h,sdk,mode,c);
    if (c->function!=0x1FBF50 && c->function!=0x1FBD50 && c->function!=0x1B1380) return 1;
    if (!h || !h->bytes || !sdk || !sdk->tables || sdk->fault) return -1;
    Audio a={0};a.host=h;a.sdk=sdk;a.sp=c->sp;
    a.side.world.sdk_tables=sdk->tables;a.side.world.sdk_world=&sdk->world;
    a.side.world.sdk_workers=&sdk->workers;
    int32_t result;
    if (c->function==0x1B1380) {
        uint32_t from[3],to[3];
        if (c->na!=2 || c->nf!=1 || load(h,(uint32_t)c->a[0],from,12)<0 ||
            load(h,(uint32_t)c->a[1],to,12)<0) return -1;
        if (em_script_host_001B1380(&a.side,from,to,c->f[0],&result)<0) return -1;
        c->v0=(uint64_t)(int64_t)result;return 0;
    }
    int gain=c->function==0x1FBF50;
    if (!mode || c->na!=(gain?4u:3u) || c->nf!=(gain?2u:1u) || (!gain&&!h->worker)) return -1;
    uint32_t pos=(uint32_t)c->a[0]+0xB0u;
    /* Copied typed input and scratch must not alias a writable gain result
     * or each other. Those unsupported shapes are refused before a write. */
    if ((pos&15) || pos<(uint32_t)c->a[0] || overlap(pos,16,0x70003400,64) ||
        overlap(pos,16,0x70003600,32)) return -1;
    int32_t *left=NULL,*right=NULL;
    if (gain) {
        if (scalar_output((uint32_t)c->a[1],pos)<0 || scalar_output((uint32_t)c->a[2],pos)<0)
            return -1;
        left=(int32_t *)(void *)h->bytes(h->ctx,(uint32_t)c->a[1],4,1);
        right=(int32_t *)(void *)h->bytes(h->ctx,(uint32_t)c->a[2],4,1);
        if (!left || !right) return -1;
    }
    int rc=positional(h,sdk,mode,c,left,right,&result);
    if(rc<0)return -1;c->v0=(uint64_t)(int64_t)result;return 0;
}
