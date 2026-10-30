#include "game/em_area01_interaction_live.h"
#include "game/em_door_candidate.h"
#include "game/em_roger.h"
#include "game/em_ee_float.h"
#include "game/em_item_device.h"
#include <math.h>
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
    if (o.selector==5 && p.action!=0x2D) {
        /* Selector 5: the squared planar distance to the owner within 196
         * (mula / madd) and |owner y - player y| <= 4; no descriptor and
         * no score. */
        uint32_t dx=em_ee_sub_bits(em_ee_bits(o.position[0]),em_ee_bits(p.position[0]));
        uint32_t dz=em_ee_sub_bits(em_ee_bits(o.position[2]),em_ee_bits(p.position[2]));
        uint32_t d2=em_ee_madd_bits(em_ee_mula_bits(dx,dx),dz,dz);
        if (!(em_ee_float(d2)<=196.0f)) return 0;
        float dy=fabsf(em_ee_float(em_ee_sub_bits(em_ee_bits(o.position[1]),em_ee_bits(p.position[1]))));
        return dy<=4.0f;
    }
    if (p.action!=0x2D) {
        uint32_t at=read_value(f,f->node+0x30,4);
        unsigned count=o.selector==1 ? 6 : o.selector==2 ? 5 :
            ((o.selector==0 || o.selector>5) && kind!=5 && kind!=6 && kind!=10 ? 3 : 2);
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
        case 0x2C:yaw=o.angles[1];break;
        default:break;
        }
        float point[6]={o.position[0],o.position[1],o.position[2],descriptor[0],descriptor[1],yaw};
        if (kind==4 && o.subtype==0x2C) {
            /* 0x2C (the 00159210 panel): after the distance and height
             * gates, a line of sight from the player's +16 height to the
             * owner's +3 (0019A910 mode 4 over 0x70003600 / 0x70003610)
             * that a hit polygon with flags 0x2800 blocks; then the
             * facing gate on the owner's yaw. The gates alone: the same
             * test with a facing that is exactly 0. */
            float gates[6];memcpy(gates,point,sizeof gates);
            gates[5]=em_ee_float(em_ee_add_bits(em_ee_bits(3.1415927410125732421875f),em_ee_bits(p.yaw)));
            if (!em_interaction_elevator_candidate(gates,p.position,p.yaw,p.action,score)) return 0;
            uint32_t ray[8]={em_ee_bits(p.position[0]),em_ee_add_bits(0x41800000u,em_ee_bits(p.position[1])),
                             em_ee_bits(p.position[2]),0x3F800000u,em_ee_bits(o.position[0]),
                             em_ee_add_bits(0x40400000u,em_ee_bits(o.position[1])),em_ee_bits(o.position[2]),
                             0x3F800000u};
            uint8_t *q=mem(f,0x70003600u,32,1);if (!q) return -1;memcpy(q,ray,sizeof ray);
            write_value(f,0x70003B98u,em_ee_bits(*score),4);
            EmArea01Call c={.function=0x0019A910u,.sp=f->sp,.a={0x70003600u,0x70003610u,4},.na=3};
            if (f->binding->fault || f->binding->host.worker(f->binding->host.ctx,&c)<0) return fail(f->binding,c.function);
            if ((uint32_t)c.v0!=0) {
                uint32_t record=read_value(f,0x700031D0u,4);
                if ((uint16_t)read_value(f,record+0x1A,2)&0x2800u) return f->binding->fault ? -1 : 0;
            }
            if (f->binding->fault) return -1;
        }
        return em_interaction_elevator_candidate(point,p.position,p.yaw,p.action,score);
    }
    if (o.selector==1) {
        if (kind==10) return em_roger_candidate(descriptor+3,descriptor,&p,f->binding->math,score);
        return em_interaction_elevator_candidate(descriptor,p.position,p.yaw,p.action,score);
    }
    if (o.selector==2) {
        /* Selector 2: the descriptor point's planar distance (sqrt of mula /
         * madd) within its +0x0C, the score, then the owner-to-player
         * height within its +0x10; no facing gate (angle 0). */
        uint32_t dx=em_ee_sub_bits(em_ee_bits(descriptor[0]),em_ee_bits(p.position[0]));
        uint32_t dz=em_ee_sub_bits(em_ee_bits(descriptor[2]),em_ee_bits(p.position[2]));
        float dist=sqrtf(em_ee_float(em_ee_madd_bits(em_ee_mula_bits(dx,dx),dz,dz)));
        if (!(dist<=descriptor[3])) return 0;
        *score=dist;
        uint32_t dy=em_ee_sub_bits(em_ee_bits(p.position[1]),em_ee_bits(o.position[1]));
        return sqrtf(em_ee_float(em_ee_mul_bits(dy,dy)))<=descriptor[4];
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
    if (c->na<1 || (c->function!=0x00184BA0u && c->function!=0x00183EF0u && c->function!=0x00185420u))
        return fail(s,c->function);
    if (c->function==0x00185420u) {
        /* 00185420(item) (byte-matched C) over D_00275B5C / D_00275B64:
         * the first node with +0 bit 0, +2 bit 7 and +0xB == 0 that
         * 00184D20(D_008102B0, node, item) accepts; em_item_device_find is
         * that test for the battery items 0x1B..0x1D (any other item
         * faults, as there). */
        Frame f={.binding=s,.player=0x008102B0u,.sp=c->sp};
        const unsigned item=(unsigned)c->a[0];
        int count=(int16_t)read_value(&f,0x00275B64u,2);
        if (s->fault || count<0 || count>EM_INTERACTION_CAPACITY) return fail(s,0x00275B64u);
        uint32_t table=read_value(&f,0x00275B5Cu,4);
        EmItemDevice devices[EM_INTERACTION_CAPACITY];
        for (int i=0;i<count;++i) {
            const uint32_t node=read_value(&f,table+4u*(unsigned)i,4);
            EmItemDevice *d=&devices[i];
            memset(d,0,sizeof *d);
            d->owner=(void *)(uintptr_t)node;
            d->status=(uint8_t)read_value(&f,node,1);d->class_flags=(uint8_t)read_value(&f,node+2,1);
            d->subtype=(uint8_t)read_value(&f,node+3,1);d->shape=(uint8_t)read_value(&f,node+8,1);
            d->armed=(uint8_t)read_value(&f,node+0xB,1);
            if (floats(&f,node+0xB0,d->position,3)<0 || floats(&f,node+0xC4,&d->yaw,1)<0) return -1;
            /* The descriptor at +0x30: the words the node's shape reads
             * (0 / 3 / 4: two; 1 / 2: six; 5: none). */
            const unsigned words=d->shape==5 ? 0u : d->shape==1 || d->shape==2 ? 6u : 2u;
            if (words && floats(&f,read_value(&f,node+0x30,4),d->parameters,words)<0) return -1;
        }
        float player[3],yaw;
        if (floats(&f,0x008102B0u+0xA0u,player,3)<0 || floats(&f,0x008102B0u+0xC4u,&yaw,1)<0) return -1;
        void *owner=NULL;
        int rc=em_item_device_find(devices,(size_t)count,item,player,yaw,s->math,&owner);
        if (rc<0) return fail(s,c->function);
        c->v0=rc ? (uint32_t)(uintptr_t)owner : 0u;
        return 0;
    }
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
