#include "game/em_area01_audio_services.h"
#include <string.h>
#include "../src/game/em_sfx.c"
int em_bgm_device_ensure(int rate){(void)rate;return 0;}

static uint8_t actor[0x320], listener[16], eye[16], yaw[4], outputs[8], scratch3400[64], scratch3600[32];
static uint8_t mono;
static int32_t frame;static int16_t ordinal;
static uint32_t stores;
static EmSdkMathTables tables;
static EmSdkMathContext sdk;
static int32_t math_mode=-1;
static uint32_t submits[5];
static uint64_t result;
static int deny;

static uint8_t *memory(void *ctx,uint32_t a,uint32_t n,int write)
{
    (void)ctx;
    if(write && a==0x6F0000)++stores;
    if(deny==2 && write && a==0x6F0000)return NULL;
    if(deny==1 && write && a==0x70003610)return NULL;
#define SPAN(base,data) if(n && a>=base && n<=sizeof data && a-base<=sizeof data-n)return (uint8_t *)&data+(a-base)
    SPAN(0x680000u,actor);SPAN(0x810360u,listener);SPAN(0x8105D0u,eye);SPAN(0x81027Cu,yaw);
    SPAN(0x70003B68u,frame);SPAN(0x70003B8Au,ordinal);
    SPAN(0x28215Bu,mono);SPAN(0x6F0000u,outputs);SPAN(0x70003400u,scratch3400);SPAN(0x70003600u,scratch3600);
#undef SPAN
    return NULL;
}
static int worker(void *ctx,EmArea01Call *c)
{
    (void)ctx;
    if(c->function!=0x1FB9F0 || c->na!=4 || c->nf)return -1;
    ++submits[0];for(unsigned i=0;i<4;++i)submits[i+1]=(uint32_t)c->a[i];
    c->v0=27;return 0;
}
/* D_00281D50 for the fixture: the handle of (group, bank) is its index. */
static int32_t handles[120];
static const int32_t *handle_table(void){return handles;}
int au_init(const uint8_t *elf,uint32_t n)
{
    memset(&sdk,0,sizeof sdk);sdk.tables=&tables;sdk.world.d26C5D0=&math_mode;
    for(int i=0;i<120;++i)handles[i]=i;
    em_sfx_bind_bank_handles(handle_table);
    if(em_sfx_init()<=0 || !em_sfx_set_area(1,0))return -1;
    return em_sdk_math_original_load_tables(elf,n,&tables);
}
uint8_t *au_bytes(uint32_t a,uint32_t n) {return memory(NULL,a,n,0);}
uint32_t *au_submits(void) {return submits;}
uint64_t au_result(void) {return result;}
void au_deny(int value) {deny=value;}
int au_call(uint32_t fn,uint32_t flat,uint32_t radius,uint32_t scale,uint32_t left,uint32_t right)
{
    memset(submits,0,sizeof submits);sdk.fault=0;stores=0;
    EmArea01RuntimeHost h={.bytes=memory,.worker=worker};
    EmArea01Call c={.function=fn,.sp=0x7F0FFF00};
    if(fn==0x1FC3C0){c.na=3;c.nf=2;c.a[0]=0x680000;c.a[1]=0x6F0000;c.a[2]=flat;c.f[0]=radius;c.f[1]=scale;}
    else if(fn==0x1FC520){c.na=1;c.a[0]=0x6F0000;}
    else if(fn==0x1FBF50) {c.na=4;c.nf=2;c.a[0]=0x680000;c.a[1]=left;c.a[2]=right;c.a[3]=flat;c.f[0]=radius;c.f[1]=scale;}
    else if(fn==0x1FBD50) {c.na=3;c.nf=1;c.a[0]=0x680000;c.a[1]=0x301;c.a[2]=flat;c.f[0]=radius;}
    else {c.na=2;c.nf=1;c.a[0]=0x6800B0;c.a[1]=0x8105D0;memcpy(c.f,yaw,4);}
    int rc=em_area01_audio_services_call(&h,&sdk,&mono,&c);result=c.v0;return rc;
}

void au_loop_state(const int32_t *requested,const int32_t *snapshot,int32_t tick,int32_t index,int32_t status)
{
    memcpy(s.requested,requested,sizeof s.requested);memcpy(s.snapshot,snapshot,sizeof s.snapshot);
    frame=tick;ordinal=(int16_t)index;
    /* The driver's tracks D_0027E0C0 (em_sfx owns them on the game thread):
     * every track allocated (00119890 = 2) or every track free. */
    memset(s.driver.tracks,0,sizeof s.driver.tracks);
    for(unsigned i=0;i<EM_SFX_TRACKS && status==2;++i)s.driver.tracks[i].allocated=1;
}
int32_t *au_requested(void){return s.requested;}
/* 1 while the driver's track is allocated (+0x32). */
int au_track(unsigned i){return i<EM_SFX_TRACKS?s.driver.tracks[i].allocated:-1;}
/* The track's 0011A218 request words +0x48 / +0x4C. */
int32_t au_gain(unsigned i,unsigned side){return i<EM_SFX_TRACKS?(side?s.driver.tracks[i].right:s.driver.tracks[i].left):0;}
uint32_t au_stores(void){return stores;}
int32_t au_start_gain(unsigned i,unsigned side){return au_gain(i,side);}
