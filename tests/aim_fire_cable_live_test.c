/* Integration boundaries of the already instruction-verified cable behaviours. */
#include "game/em_aim_fire_cable_live.h"
#include "game/em_ee_float.h"
#include "game/em_effect_original.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t parent[0x320],node[0x320],child[0x320],scratch[0x1000],points[0x100];
static uint8_t projection[64],render_context[0xB0],packet[0x200];
static uint32_t context_address=0xB000;
static int opened,closed;
static EmAimFireLive live;
static int checks,randoms,spawns,chains,blocks,blocks5,sprites,strips,frees,fail_depth;
#define CHECK(x) do{assert(x);++checks;}while(0)
static uint32_t word(const void*p){uint32_t x;memcpy(&x,p,4);return x;}
static void put(void*p,uint32_t x){memcpy(p,&x,4);}
static void identity(uint8_t*p){memset(p,0,64);for(int i=0;i<4;++i)put(p+20*i,0x3F800000);}
void *em_aim_fire_live_map(void *ctx,uint32_t a,size_t n,int write)
{
    EmAimFireLive*h=ctx;(void)write;
    if(h->fault_function||h->fault_address)return NULL;
    void *out=NULL;
#define MAP(base,bytes) if(a>=(base)&&(uint64_t)a+n<=(uint64_t)(base)+sizeof(bytes))out=(bytes)+(a-(base))
    MAP(0x5000u,parent);MAP(0x6000u,node);MAP(0x7000u,child);MAP(0x70003000u,scratch);MAP(0x821400u,points);MAP(0x7F001000u,h->temporary);MAP(0xA000u,projection);MAP(0xB000u,render_context);MAP(0xC000u,packet);
    if(a==0x275670&&n<=4)out=&context_address;
#undef MAP
    if(!out)h->fault_address=a;
    return out;
}
int em_aim_fire_live_call(EmAimFireLive*h,EmAimFireTargetCall*c)
{
    if(h->fault_function||h->fault_address)return -1;
    switch(c->function){
    case 0x1CD370:CHECK(c->na==1&&c->a[0]==0);c->v0=0xA000;return 0;
    case 0x1CB5F0:CHECK(c->na==3&&c->a[0]==0x7635C0&&c->a[1]==23&&c->a[2]==14);++opened;c->v0=0xC000;return 0;
    case 0x1CB900:CHECK(c->na==3&&c->a[0]==0x7635C0&&c->a[1]==23&&c->a[2]==2);++closed;return 0;
    case 0x11E748:CHECK(c->nf==1&&c->f[0]==0x43800000);c->f0=0x41800000;return 0;
    case 0x1026A0:{float m[16],v[4],o[4];CHECK(c->na==3);
        const void*p=em_aim_fire_live_map(h,(uint32_t)c->a[1],64,0);if(!p)return -1;memcpy(m,p,64);
        p=em_aim_fire_live_map(h,(uint32_t)c->a[2],16,0);if(!p)return -1;memcpy(v,p,16);em_effect_original_001026A0(o,m,v);
        void*d=em_aim_fire_live_map(h,(uint32_t)c->a[0],16,1);if(!d)return -1;memcpy(d,o,16);return 0;}
    case 0x122BB8:++randoms;c->v0=0;return 0;
    case 0x11E2A8:CHECK(c->nf==1&&c->f[0]==0);c->f0=0;return 0;
    case 0x1281C0:CHECK(c->nf==1);c->v0=(uint32_t)(int32_t)em_ee_float(c->f[0]);return 0;
    case 0x1AFC10:CHECK(c->na==1&&c->a[0]==0x6000);++frees;return 0;
    case 0x1CE860:
        CHECK(c->na==6&&c->nf==1);CHECK(c->a[0]==0&&c->a[1]==2);CHECK(c->a[2]==0x821400&&c->a[3]==0x700038B0);
        CHECK(c->a[4]==2&&c->a[5]==UINT64_C(0x20045D8555422188));CHECK(c->f[0]==0x3F000000);++strips;return 0;
    case 0x1CD520:
        CHECK(c->na==5&&c->nf==3);CHECK(c->a[0]==0&&c->a[1]==2);CHECK(c->f[0]==0x40E00000&&c->f[1]==0x40E00000&&c->f[2]==0x40A00000);
        CHECK(em_aim_fire_live_map(h,(uint32_t)c->a[2],16,0)!=NULL);++sprites;return 0;
    default:h->fault_function=c->function;return -1;
    }
}
int em_effects_live_001EF9D0(uint32_t id,const float *p,uint32_t bits,uint32_t*out)
{ CHECK(id==0x8000003B&&p&&bits==0x3F800000);++spawns;*out=0x7000;return 0; }
int em_effects_live_001CCF70(const float p[4],int32_t*out)
{ CHECK(p);if(fail_depth)return -1;*out=23;return 0; }
int em_effects_live_001CFA60(uint8_t b[0x60],const float m[16],uint32_t f,uint32_t g)
{ CHECK(m&&g==0);if(!blocks)CHECK(f==0);++blocks;memset(b,0x51,0x58);return 0; }
int em_effects_live_001CFB50(uint8_t b[0x60],int32_t mode,const float m[16],const uint32_t f[5])
{ CHECK(mode==0&&m);CHECK(f[0]==0&&f[2]==0x3F800000&&f[3]==0x358637BD&&f[4]==0x40A00000);++blocks5;memset(b,0x71,0x58);return 0; }
int em_effects_live_001CFBE0(int32_t key,int32_t kind,uint32_t source,const uint8_t b[0x60],int32_t copy)
{ CHECK(key==23&&copy==0);if(source==0x266930){CHECK(kind==1&&b[0]==0x71);}else{CHECK((kind==2&&source==0x2669C0)||(kind==1&&source==0x266A50));CHECK(b[0]==0x51);}++chains;return 0; }
static void reset(void)
{
    memset(&live,0,sizeof live);memset(parent,0,sizeof parent);memset(node,0,sizeof node);memset(child,0,sizeof child);
    memset(scratch,0,sizeof scratch);memset(points,0,sizeof points);put(node+0x24,0x5000);identity(parent+0xD0);identity(node+0xD0);
    randoms=spawns=chains=blocks=blocks5=sprites=strips=frees=fail_depth=opened=closed=0;
}
int main(void)
{
    reset();CHECK(em_aim_fire_cable_live_tick(&live,0x6000,0x21AAC0)==1);
    CHECK(randoms==6&&spawns==1);CHECK(node[4]==1&&word(node+0x288)==1);
    CHECK(child[5]==0&&word(child+0x1F0)==12&&word(child+0x1F4)==0x42400000&&word(child+0x1F8)==0x3F000000);
    CHECK(word(node+0xB8)==0x3F800000);CHECK(word(child+0x10C)==0x3F800000);
    put(node+0x288,30);CHECK(em_aim_fire_cable_live_tick(&live,0x6000,0x21AAC0)==1);
    CHECK(blocks==1&&chains==2&&spawns==2);CHECK(word(node+0x284)==1);
    put(node+0x284,0);put(node+0x288,59);CHECK(em_aim_fire_cable_live_tick(&live,0x6000,0x21AAC0)==1);CHECK(parent[4]==3);
    put(node+0x288,120);CHECK(em_aim_fire_cable_live_tick(&live,0x6000,0x21AAC0)==1);CHECK(node[4]==3);
    CHECK(em_aim_fire_cable_live_tick(&live,0x6000,0x21AAC0)==0);CHECK(frees==1);
    reset();node[4]=1;put(node+0x288,30);fail_depth=1;
    CHECK(em_aim_fire_cable_live_tick(&live,0x6000,0x21AAC0)<0);CHECK(live.fault_function==0x1CCF70);CHECK(chains==0&&spawns==0);
    CHECK(word(node+0x288)==31&&word(node+0x284)==1);CHECK(em_aim_fire_cable_live_tick(&live,0x6000,0x21AAC0)<0);
    reset();put(node+0x1F0,2);put(node+0x1F4,0x42400000);put(node+0x1F8,0x3F000000);
    CHECK(em_aim_fire_cable_live_tick(&live,0x6000,0x21A500)==1);CHECK(randoms==1&&strips==1&&sprites==0);
    CHECK(node[4]==1);CHECK(word(points+0x2C-16)==0x3F800000);CHECK(word(points+16+8)==0x42400000);
    reset();node[4]=1;node[5]=1;put(node+0x1F0,2);put(node+0x1F4,0x42400000);put(node+0x1F8,0x3F000000);
    CHECK(em_aim_fire_cable_live_tick(&live,0x6000,0x21A500)==1);CHECK(blocks5==2&&chains==2&&sprites==2&&strips==1);
    reset();node[4]=3;CHECK(em_aim_fire_cable_live_tick(&live,0x6000,0x21A500)==0);CHECK(frees==1);
    reset();identity(projection);identity(scratch+0xAC0);identity(scratch+0xA40);
    memset(packet,0,sizeof packet);memset(render_context,0,sizeof render_context);put(render_context+0xA0,0x3F800000);
    put(points+12,0x3F800000);put(points+16,0x3F800000);put(points+28,0x3F800000);
    for(unsigned i=0;i<4;++i)put(scratch+0x8B0+4*i,0x43400000);
    EmAimFireTargetCall frame={.function=0x1CE860,.sp=0x7F001400,.a={0,2,0x821400,0x700038B0,2,UINT64_C(0x20045D8555422188)},.f={0x3F000000},.na=6,.nf=1};
    CHECK(em_aim_fire_cable_live_call(&live,&frame)==0);CHECK(opened==1&&closed==1);
    CHECK(word(packet+0xC)==0x5000000D);CHECK(word(packet+0x40)==192);CHECK(word(packet+0x24)==0x20045D85);
    CHECK(word(packet+0x64)==8&&word(packet+0x84)==(uint32_t)-8);
    printf("aim/fire cable live: %d binding checks PASS\n",checks);return 0;
}
