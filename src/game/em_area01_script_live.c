#include "game/em_area01_script_live.h"
#include "game/em_area11_boxes.h"
#include "game/em_ee_float.h"
#include <stdio.h>
#include <string.h>

static int contains(uint32_t a,uint32_t n,uint32_t lo,uint32_t hi)
{ return n && a>=lo && a<hi && n<=hi-a; }
static uint32_t word(const uint8_t *p)
{ uint32_t v;memcpy(&v,p,4);return v; }

void em_area01_script_free(EmArea01Script *s)
{
    if (!s) return;
    for (unsigned i=0;i<4;++i) em_script_image_free(&s->boot[i]);
    em_spawn_table_free(&s->destinations);
    em_spawn_table_free(&s->timeline_tables);
    memset(s,0,sizeof *s);
}
uint8_t *em_area01_script_bytes(EmArea01Script *s,uint32_t a,uint32_t n,int write)
{
    if (!s || !s->bound) return NULL;
    uint8_t *timeline=em_area11_script_host_timeline_bytes(a,n);
    if(timeline)return timeline;
    for (unsigned i=0;i<4;++i) {
        uint8_t *p=em_script_image_read(&s->boot[i],a,n);
        if (!p) continue;
        /* The walk table and door sound descriptors are read-only. The
         * script windows contain mutable op00 targets and kickoff patches. */
        if (write && !(contains(a,n,0x00246C20u,0x00247E20u) ||
                       contains(a,n,0x0024D900u,0x0024DB80u) ||
                       contains(a,n,0x0024DBC0u,0x0024DF80u) ||
                       contains(a,n,0x002482C0u,0x00248540u) ||
                       contains(a,n,0x00266620u,0x002668A0u))) return NULL;
        return p;
    }
    if (write) return NULL;
    const uint8_t *table=em_spawn_table_read(&s->timeline_tables,a,n);
    if(table)return (uint8_t *)(void *)table;
    return (uint8_t *)(void *)em_spawn_table_read(&s->destinations,a,n);
}
EmScriptImage *em_area01_script_image(EmArea01Script *s,uint32_t entry)
{
    if (!s || !s->bound) return NULL;
    for (unsigned i=0;i<4;++i)
        if (em_script_image_read(&s->boot[i],entry,EM_SCRIPT_RECORD_SIZE)) return &s->boot[i];
    if (!contains(entry,EM_SCRIPT_RECORD_SIZE,0x00829860u,0x0082BD50u)) return NULL;
    const uint8_t *h=em_module_loader_memory(s->loader,0x00823500u,12);
    if (!h || memcmp(h,"MWo3",4) || word(h+4)!=2 || word(h+8)!=0x00823500u) return NULL;
    uint8_t *p=em_module_loader_memory_mutable(s->loader,0x00828A00u,0x4300u);
    if (!p) return NULL;
    s->overlay=(EmScriptImage){p,0x00828A00u,0x00829860u,0x4300u};
    return &s->overlay;
}
static EmScriptImage *image(void *ctx,uint32_t entry)
{ return em_area01_script_image(ctx,entry); }
static const uint8_t *resource(void *ctx,uint32_t a,uint32_t n)
{
    EmArea01Script *s=ctx;
    uint8_t *p=em_area01_script_bytes(s,a,n,0);
    if (p) return p;
    if (contains(a,n,0x0028A490u,0x0028A750u)) {
        EmStatusSceneLoader *ld=em_module_loader_state(s->loader);
        return ld ? (uint8_t *)ld->d28A490+a-0x0028A490u : NULL;
    }
    return em_module_loader_memory(s->loader,a,n);
}
int em_area01_script_player_banks(EmArea01Script *s,
    int (*map)(void *,uint32_t,uint32_t,const uint8_t *),void *ctx)
{
    if(!s || !s->bound || !map)return -1;
    EmStatusSceneLoader *ld=em_module_loader_state(s->loader);
    if(!ld)return -1;
    struct Span { uint32_t a,n;const uint8_t *p; } span[3];
    for(unsigned i=0;i<3;++i) {
        span[i].a=ld->d28A490[0x96+i];span[i].n=0;
        span[i].p=em_module_loader_memory_rest(s->loader,span[i].a,&span[i].n);
        if(!span[i].p || !span[i].n || span[i].a>UINT32_MAX-span[i].n)return -1;
    }
    for(unsigned i=1;i<3;++i)for(unsigned j=i;j && span[j].a<span[j-1].a;--j) {
        struct Span tmp=span[j];span[j]=span[j-1];span[j-1]=tmp;
    }
    unsigned count=1;
    for(unsigned i=1;i<3;++i) {
        struct Span *last=&span[count-1];
        if(span[i].a<last->a+last->n) {
            if(span[i].a+span[i].n!=last->a+last->n ||
               span[i].p!=last->p+(span[i].a-last->a))return -1;
        } else span[count++]=span[i];
    }
    for(unsigned i=0;i<count;++i)
        if(map(ctx,span[i].a,span[i].n,span[i].p)<0)return -1;
    return 0;
}
static int owner_bank(void *ctx,EmActor *actor,uint32_t *value,int write)
{
    EmArea01Script *s=ctx;
    if (s->bank) return s->bank(s->bank_ctx,actor,value,write);
    EmOwnerServicesOwner *o=em_area11_boxes_owner_view(actor);
    if (!o || !value) return -1;
    if (write) o->anim=*value; else *value=o->anim;
    return 0;
}
void em_area01_script_bank(EmArea01Script *s,void *ctx,
                            int (*bank)(void *,EmActor *,uint32_t *,int))
{ if (s) { s->bank_ctx=ctx;s->bank=bank; } }
static int clip(void *ctx,uint32_t actor,int16_t index,float blend,float frame)
{
    EmArea01Script *s=ctx;
    EmArea01Call c={.function=0x001C67E0u,.a={actor,(uint64_t)(int64_t)index},.na=2,
                    .f={em_ee_bits(blend),em_ee_bits(frame)},.nf=2};
    return s->host.worker(s->host.ctx,&c);
}
static int record(void *ctx,uint32_t fn,uint32_t actor,uint32_t block,
                  uint32_t rec,int32_t *result)
{
    EmArea01Script *s=ctx;
    /* The callback address is meaningful only in the bound overlay. Boot
     * door callbacks are shared originals. Unknown callbacks never succeed. */
    switch (fn) {
    case 0x001BAC00u: /* op14 placement */
    case 0x001798D0u: case 0x001B7F90u: case 0x001B9CF0u: case 0x001B76D0u:
    case 0x001B7670u: case 0x001B6D70u: case 0x001B6AE0u:
    case 0x001B6EA0u: /* shared pickup take callback */
    case 0x00158130u: case 0x00158050u: case 0x001580C0u:
    case 0x00157F60u: case 0x001575B0u: /* shared panel callbacks */
    case 0x001BB400u: case 0x001BB310u: case 0x001BBAE0u: case 0x001BBBF0u:
    case 0x00825130u: case 0x00825240u: case 0x00825910u: case 0x00826950u: break;
    default: return -1;
    }
    if (fn>=0x00823500u && !em_area01_script_image(s,rec)) return -1;
    EmArea01Call c={.function=fn,.a={actor,block,rec},.na=3};
    if (fn==0x001798D0u) { c.a[0]=0x008102B0u;c.na=1; }
    int rc=s->host.worker(s->host.ctx,&c);
    if (rc>=0) *result=(int32_t)c.v0;
    return rc;
}
static int camera(void *ctx,uint32_t fn,uint32_t actor)
{
    EmArea01Script *s=ctx;
    EmArea01Call c={.function=fn,.a={actor,1},.na=fn==0x0022EEF0u ? 2u : 1u};
    return s->host.worker(s->host.ctx,&c);
}
int em_area01_script_bind(EmArea01Script *s,const EmArea01RuntimeHost *host,
                         EmModuleLoader *loader,const char *boot_path,const char *dest_path)
{
    if (!s || !host || !host->worker || !loader || s->bound) return -1;
    const uint32_t lo[4]={0x00246C20u,0x0024D8F0u,0x002482C0u,0x00266620u};
    const uint32_t hi[4]={0x00247E20u,0x0024DF80u,0x00248540u,0x002668A0u};
    const uint32_t entry[4]={0x00246C20u,0x0024D900u,0x002482C0u,0x00266620u};
    const char *name[4]={"panels.emsc","doors.emsc","pickup.emsc","take.emsc"};
    char path[1024];
    for (unsigned i=0;i<4;++i) {
        int n=snprintf(path,sizeof path,"%s/%s",boot_path ? boot_path : EM_AREA01_SCRIPT_BOOT_PATH,name[i]);
        if (n<0 || (size_t)n>=sizeof path || !em_script_image_load(&s->boot[i],path) ||
            s->boot[i].base!=lo[i] || s->boot[i].entry!=entry[i] || s->boot[i].length!=hi[i]-lo[i]) goto fail;
    }
    if (em_spawn_table_load(&s->destinations,dest_path ? dest_path : EM_AREA01_SCRIPT_DEST_PATH)<0) goto fail;
    int n=snprintf(path,sizeof path,"%s/timeline.emsp",boot_path ? boot_path : EM_AREA01_SCRIPT_BOOT_PATH);
    if(n<0 || (size_t)n>=sizeof path || em_spawn_table_load(&s->timeline_tables,path)<0)goto fail;
    s->host=*host;s->loader=loader;s->bound=1;
    EmAreaScriptHostArea area={.ctx=s,.overlay_id=2,.image=image,.resource=resource,
        .owner_bank=owner_bank,.clip=clip,.record=record,.camera=camera};
    if (em_area11_script_host_area(&area)<0) goto fail;
    return 0;
fail:
    em_area01_script_free(s);return -1;
}
