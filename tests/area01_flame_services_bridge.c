#include <stdio.h>
#include <stdint.h>
static int manifest_overflow;
static FILE *manifest_open(const char *path,const char *mode)
{
    if(!manifest_overflow)return fopen(path,mode);
    FILE *f=tmpfile();if(!f)return NULL;
    const uint32_t header[4]={0x54454D45u,1,65,0},entry[4]={0x100000,0,0,0};
    if(fwrite(header,sizeof header,1,f)!=1){fclose(f);return NULL;}
    for(unsigned i=0;i<65;++i)if(fwrite(entry,sizeof entry,1,f)!=1){fclose(f);return NULL;}
    rewind(f);return f;
}
#define fopen manifest_open
#define em_rcl_bytes fs_previous_rcl_bytes
#define main previous_effects_contract_main
#include "aim_fire_effects_live_test.c"
#undef main
#undef em_rcl_bytes
#undef fopen
#include "game/em_area01_flame_services.h"
#include "game/em_aim_fire_sdk_memory.h"

static uint32_t glow_context_word=CTX;
const uint8_t *em_rcl_bytes(uint32_t a,uint32_t n)
{
    if(a>=0x275670u && n && (uint64_t)a+n<=0x275674u)
        return (const uint8_t *)&glow_context_word+(a-0x275670u);
    for(unsigned i=0;i<3;i++) {
        EmPacketChainRegion *r=&regions[i];
        if(a>=r->base && n && (uint64_t)a+n<=(uint64_t)r->base+r->size)
            return r->bytes+(a-r->base);
    }
    return fs_previous_rcl_bytes(a,n);
}
uint8_t *em_rcl_bytes_mut(uint32_t a,uint32_t n)
{ return (uint8_t *)(uintptr_t)em_rcl_bytes(a,n); }
EmPacketChain *em_rcl_packet_chain(void){return &pc;}
int fs_load(void) { memset(&S,0,sizeof S);return em_effects_live_load(); }
int fs_overflow(void)
{
    memset(&S,0,sizeof S);manifest_overflow=1;
    int rc=em_effects_live_load();manifest_overflow=0;
    return rc<0 && !S.loaded && !S.windows ? 0 : -1;
}
const void *fs_window(uint32_t a,uint32_t n) { return em_effects_live_window(a,n); }
void fs_unload(void) { free(S.elf);memset(&S,0,sizeof S); }

int fs_call(const EmArea01RuntimeHost *h,EmArea01Call *c,uint32_t *fault)
{ return em_area01_flame_services_call(h,c,fault); }
void fs_seed(const uint8_t *ram,const uint8_t *spr)
{
    setup();
    memcpy(context_bytes,ram+CTX,sizeof context_bytes);
    memcpy(matrices,spr+0x3A40,sizeof matrices);
    memcpy(arena,ram+0x300000,sizeof arena);
    memcpy(chain_table,ram+0x7635C0,sizeof chain_table);
    memcpy(field_index,ram+0x810E80,sizeof field_index);
    memcpy(S.htables.d251260,ram+0x251260,sizeof S.htables.d251260);
}
void fs_publish(uint8_t *ram)
{
    memcpy(ram+CTX,context_bytes,sizeof context_bytes);
    memcpy(ram+0x300000,arena,sizeof arena);
    memcpy(ram+0x7635C0,chain_table,sizeof chain_table);
}
int fs_packet(const EmArea01RuntimeHost *h,EmArea01Call *c,uint32_t *fault)
{
    EmArea01FlamePacket packet;
    memset(&packet,0xA5,sizeof packet);
    int rc=em_area01_flame_packet_prepare(h,c,&packet,fault);
    return rc ? rc : em_area01_flame_packet_invoke(&packet,c);
}
static int sys_worker(void *ctx,EmArea01SysCall *in)
{
    const EmArea01RuntimeHost *h=ctx;
    EmArea01Call c={.function=in->fn,.sp=in->sp,.na=in->na,.nf=in->nf};
    memcpy(c.a,in->a,in->na*sizeof in->a[0]);memcpy(c.f,in->f,in->nf*sizeof in->f[0]);
    int rc=h->worker(h->ctx,&c);in->v0=c.v0;in->f0=c.f0;return rc;
}
static uint8_t *sys_memory(void *ctx,uint32_t a,uint32_t n,int w)
{ const EmArea01RuntimeHost *h=ctx;return h->bytes(h->ctx,a,n,w); }
int fs_sys(const EmArea01RuntimeHost *h,uint32_t node,uint32_t *fault)
{
    EmArea01Sys s={.ctx=(void *)h,.view=sys_memory,.call=sys_worker,.sp=0x7F0F0000};
    int rc=em_area01_sys_001E3D90(&s,node);*fault=s.fault_address;return rc;
}
static void *sdk_memory(void *ctx,uint32_t a,size_t n,int w)
{ const EmArea01RuntimeHost *h=ctx;return n<=UINT32_MAX?h->bytes(h->ctx,a,(uint32_t)n,w):NULL; }
int fs_sdk(const EmArea01RuntimeHost *h,EmArea01Call *c)
{
    EmAimFireTargetCall v={.function=c->function,.sp=c->sp,.na=c->na,.nf=c->nf};
    memcpy(v.a,c->a,c->na*sizeof c->a[0]);memcpy(v.f,c->f,c->nf*sizeof c->f[0]);
    int rc=em_aim_fire_sdk_memory_call((void *)h,sdk_memory,&v);c->v0=v.v0;c->f0=v.f0;return rc;
}
int fs_fog(int mode,uint32_t scale,uint32_t bias)
{ return em_packet_chain_0021B9A0(&pc,mode,scale,bias); }
