#include "game/em_area01_interaction_live.h"
#include "game/em_door_candidate.h"
#include "game/em_roger.h"
#include "game/em_ee_float.h"
#include <string.h>

typedef struct { EmArea01Interaction *binding;uint32_t player,node,sp;float *score; } Frame;
static int fail(EmArea01Interaction *s,uint32_t address)
{ if (s && !s->fault) { s->fault=1;s->fault_address=address; } return -1; }
static uint8_t *mem(Frame *f,uint32_t a,uint32_t n,int write)
{
    if (f->binding->fault) return NULL;
    uint8_t *p=n && (uint64_t)a+n<=UINT64_C(0x100000000) ?
        f->binding->host.bytes(f->binding->host.ctx,a,n,write) : NULL;
    if (!p) fail(f->binding,a);
    return p;
}
static uint32_t read_value(Frame *f,uint32_t a,unsigned n)
{
    const uint8_t *p=mem(f,a,n,0);uint32_t v=0;
    if (p) for (unsigned i=0;i<n;++i) v|=(uint32_t)p[i]<<(8*i);
    return v;
}
static void write_value(Frame *f,uint32_t a,uint32_t v,unsigned n)
{
    uint8_t *p=mem(f,a,n,1);
    if (p) for (unsigned i=0;i<n;++i) p[i]=(uint8_t)(v>>(8*i));
}
static int floats(Frame *f,uint32_t a,float *out,unsigned count)
{
    const uint8_t *p=mem(f,a,4*count,0);if (!p) return -1;
    memcpy(out,p,4*count);return 0;
}
static int ray(void *ctx,const float from[4],const float to[4],unsigned mode,EmInteractionRayHit *hit)
{
    Frame *f=ctx;(void)to;
    unsigned special=read_value(f,f->player+0x1F0,1)==0x2D;
    uint32_t origin=special ? 0x70003640u : 0x70003600u;
    uint8_t *p=mem(f,origin,16,1);if (!p) return 0;memcpy(p,from,16);
    /* The special branch copies the player's whole vector before adding
     * height. Ordinary pickup LOS writes an explicit homogeneous one. */
    if (special) {
        uint32_t w=read_value(f,f->player+0xAC,4);memcpy(p+12,&w,4);
    }
    if (f->score) write_value(f,0x70003B98u,em_ee_bits(*f->score),4);
    EmArea01Call c={.function=0x0019A910u,.sp=f->sp,.a={origin,f->node+0xB0u,mode},.na=3};
    if (f->binding->fault || f->binding->host.worker(f->binding->host.ctx,&c)<0) {
        fail(f->binding,c.function);return 0;
    }
    memset(hit,0,sizeof *hit);hit->hit=(uint32_t)c.v0!=0;
    if (hit->hit) {
        uint32_t record=read_value(f,0x700031D0u,4);
        hit->flags=(uint16_t)read_value(f,record+0x1A,2);
        hit->kind=read_value(f,0x700031D8u,4);hit->owner=read_value(f,0x700031D4u,4);
    }
    return !f->binding->fault;
}
static int candidate(Frame *f,float *score)
{
    EmInteractionPickup o={.identity=f->node};EmInteractionPlayer p={0};
    o.class_flags=(uint8_t)read_value(f,f->node+2,1);o.subtype=(uint8_t)read_value(f,f->node+3,1);
    o.selector=(uint8_t)read_value(f,f->node+8,1);o.callback=read_value(f,f->node+0x10,4);
    p.action=(uint8_t)read_value(f,f->player+0x1F0,1);
    unsigned kind=o.class_flags&31;
    if (p.action==0x2D && kind!=7) return 0;
    if (floats(f,f->player+0xA0,p.position,3)<0 || floats(f,f->node+0xB0,o.position,3)<0 ||
        floats(f,f->node+0xC0,o.angles,3)<0) return -1;
    p.yaw=em_ee_float(read_value(f,f->player+0xC4,4));
    float descriptor[6]={0};
    if (p.action!=0x2D) {
        uint32_t at=read_value(f,f->node+0x30,4);
        unsigned count=o.selector==1 ? 6 : ((o.selector==0 || o.selector>5) && kind!=5 && kind!=6 && kind!=10 ? 3 : 2);
        if (floats(f,at,descriptor,count)<0) return -1;
        memcpy(o.descriptor,descriptor,8);
    }
    if (p.action==0x2D || o.selector==3 || o.selector==4) {
        EmInteractionPickupScratch scratch={0};
        if (p.action==0x2D) {
            if (floats(f,0x008105E0u,p.view_target,3)<0) return -1;
            scratch.view3640=(float *)(void *)mem(f,0x70003640u,16,1);
            scratch.toward3650=(float *)(void *)mem(f,0x70003650u,16,1);
            scratch.raw3660=(float *)(void *)mem(f,0x70003660u,16,1);
            scratch.dots3690=(float *)(void *)mem(f,0x70003690u,8,1);
            if (f->binding->fault) return -1;
        }
        f->score=score;
        return em_interaction_pickup_candidate_views(&o,&p,f->binding->math,ray,f,score,
                                                       p.action==0x2D ? &scratch : NULL);
    }
    if (o.selector==0 || o.selector>5) {
        if (kind==5) return em_door_candidate(descriptor,o.position,o.angles[1],o.subtype,&p,f->binding->math,score);
        if (kind==10) return em_roger_candidate(descriptor,o.position,&p,f->binding->math,score);
        /* The shared descriptor-point gate has the same distance/height
         * and wrapped-facing body. Select its views as this branch does. */
        float yaw=descriptor[2];
        if (kind==6) yaw=o.angles[1];
        if (kind==4) switch (o.subtype) {
        case 0x14:case 0x22:case 0x23:case 0x24:case 0x25:case 0x26:case 0x37:case 0x38:
            yaw=o.angles[1];break;
        case 0x2C:return fail(f->binding,0x00183EF0u);
        default:break;
        }
        float point[6]={o.position[0],o.position[1],o.position[2],descriptor[0],descriptor[1],yaw};
        return em_interaction_elevator_candidate(point,p.position,p.yaw,p.action,score);
    }
    if (o.selector==1) {
        if (kind==10) return em_roger_candidate(descriptor+3,descriptor,&p,f->binding->math,score);
        return em_interaction_elevator_candidate(descriptor,p.position,p.yaw,p.action,score);
    }
    return fail(f->binding,0x00183EF0u);
}
static int predicate(void *ctx,const EmInteractionCandidate *c,float *score)
{
    Frame *f=ctx;if (f->binding->fault) return 0;
    f->node=(uint32_t)(uintptr_t)c->owner;
    int rc=candidate(f,score);
    write_value(f,0x70003B98u,em_ee_bits(*score),4);
    if (rc<0) { fail(f->binding,0x00183EF0u);return 0; }
    return rc;
}
int em_area01_interaction_bind(EmArea01Interaction *s,const EmArea01RuntimeHost *host,
    const EmInteractionMath *math,int (*claim)(void *,uint32_t),void *ctx)
{
    if (!s || !host || !host->bytes || !host->worker || !math) return -1;
    memset(s,0,sizeof *s);s->host=*host;s->math=math;s->claim=claim;s->claim_ctx=ctx;return 0;
}
int em_area01_interaction_call(EmArea01Interaction *s,EmArea01Call *c)
{
    if (!s || !c || s->fault) return -1;
    if (c->na<1 || (c->function!=0x00184BA0u && c->function!=0x00183EF0u)) return fail(s,c->function);
    Frame f={.binding=s,.player=(uint32_t)c->a[0],.sp=c->sp};
    float score=em_ee_float(read_value(&f,0x70003B98u,4));
    if (c->function==0x00183EF0u) {
        if (c->na<2) return fail(s,c->function);
        f.node=(uint32_t)c->a[1];int rc=candidate(&f,&score);
        write_value(&f,0x70003B98u,em_ee_bits(score),4);
        if (rc<0 || s->fault) return fail(s,c->function);
        c->v0=(uint32_t)rc;return 0;
    }
    EmInteractionScanState state={(uint8_t)read_value(&f,0x70003B8Du,1),
        (int16_t)read_value(&f,0x0028A9A0u,2),(uint8_t)read_value(&f,0x008106EFu,1),score};
    if (s->fault) return -1;
    if (state.selector || state.fade_wait || state.inhibited) { c->v0=0;return 0; }
    int count=(int16_t)read_value(&f,0x00275B64u,2);
    if (count<0 || count>EM_INTERACTION_CAPACITY) return fail(s,0x00275B64u);
    uint32_t table=read_value(&f,0x00275B5Cu,4),nodes[EM_INTERACTION_CAPACITY];
    uint8_t armed[EM_INTERACTION_CAPACITY];EmInteractionCandidate items[EM_INTERACTION_CAPACITY];
    for (int i=0;i<count;++i) {
        nodes[i]=read_value(&f,table+4u*(unsigned)i,4);armed[i]=(uint8_t)read_value(&f,nodes[i]+11,1);
        items[i]=(EmInteractionCandidate){(void *)(uintptr_t)nodes[i],(uint8_t)read_value(&f,nodes[i],1),
                                          (uint8_t)read_value(&f,nodes[i]+2,1),armed+i};
    }
    if (s->fault) return -1;
    write_value(&f,0x70003B98u,0,4);
    size_t winner;
    int rc=em_interaction_scan(&state,items,(size_t)count,predicate,&f,&winner);
    if (rc<0 || s->fault) return fail(s,c->function);
    write_value(&f,0x70003B98u,em_ee_bits(state.score),4);
    if (rc) {
        write_value(&f,nodes[winner]+11,4,1);
        if (s->fault || !s->claim || s->claim(s->claim_ctx,nodes[winner])!=1) return fail(s,c->function);
        write_value(&f,0x70003B8Du,state.selector,1);
    }
    c->v0=(uint32_t)rc;return s->fault ? -1 : 0;
}
