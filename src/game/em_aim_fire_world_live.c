#include "game/em_aim_fire_world_live.h"
#include "game/em_aim_fire_leaves.h"
#include "game/em_area00_fx.h"
#include "game/em_area00_world.h"
#include "game/em_area01_side.h"
#include "game/em_area02_math.h"
#include <string.h>

#define LOCAL_BASE UINT32_C(0x7F001000)
#define CLASS2_BASE UINT32_C(0x0028AF30)
#define TRY(x) do { if ((x)<0) return -1; } while (0)
static void *span(uint32_t a,size_t n,uint32_t base,size_t size,void *p)
{
    return p && n && a>=base && (uint64_t)a+n<=(uint64_t)base+size
           ? (uint8_t *)p+(a-base) : NULL;
}
static const EmCollSegment *segment(EmAimFireWorldLive *w)
{ return w && w->segment ? w->segment : em_collision_world_segment(); }
static const EmCollisionWorldOwners *owners(EmAimFireWorldLive *w)
{ return w && w->owners ? w->owners : em_collision_world_owners(); }
static uint32_t actor_address(EmAimFireWorldLive *w,const EmActor *a)
{
    const EmCollisionWorldOwners *o=owners(w);
    return !a ? 0 : o && o->address_of ? o->address_of(o->context,a) : 0;
}
static void *collision_map(EmAimFireWorldLive *w,uint32_t a,size_t n,int write)
{
    const EmCollSegment *s=segment(w);
    void *p;
    if (!s || !s->state || !s->face) return NULL;
    EmCollProbeState *q=s->state;
#define FIELD(at,field) do { p=span(a,n,at,sizeof(field),&(field)); if(p)return p; } while(0)
    FIELD(0x70003190u,q->start); FIELD(0x700031A0u,q->end);
    FIELD(0x700031B0u,q->point); FIELD(0x700031C0u,q->delta);
    FIELD(0x700031D8u,q->kind); FIELD(0x700030CAu,q->cell_class);
    FIELD(0x700030D4u,q->cell_normal); FIELD(0x70003680u,q->ratio);
    FIELD(0x7000324Eu,q->query_class); FIELD(0x70003240u,q->rank);
    FIELD(0x70003B86u,q->span_lo); FIELD(0x70003B88u,q->span_hi);
    FIELD(0x70003600u,s->face->box_min); FIELD(0x70003610u,s->face->box_max);
    FIELD(0x70003620u,s->face->delta); FIELD(0x70003630u,s->face->rel);
    FIELD(0x70003684u,s->face->cross);
    FIELD(0x700030CCu,s->face->hull_word_1c); FIELD(0x700030D0u,s->face->hull_word_20);
#undef FIELD
    if (write || !w) return NULL;
    if (span(a,n,0x700031D0u,4,&w->record_word)) {
        if (q->record==EM_COLL_PROBE_RECORD_NONE) w->record_word=0;
        else if (q->record==EM_COLL_PROBE_RECORD_CELL) w->record_word=0x700030B0u;
        else if (q->record==EM_COLL_PROBE_RECORD_GRID && q->node>=0 && s->world &&
                 s->world->grid && (uint32_t)q->node<s->world->grid->count && w->grid_address) {
            w->record_word=w->grid_address(w->context,s->world->grid,(uint32_t)q->node);
            if (!w->record_word) return NULL;
        } else return NULL;
        return span(a,n,0x700031D0u,4,&w->record_word);
    }
    if (span(a,n,0x700031D4u,4,&w->entity_word)) {
        w->entity_word=actor_address(w,q->entity);
        if (q->entity && !w->entity_word) return NULL;
        return span(a,n,0x700031D4u,4,&w->entity_word);
    }
    return NULL;
}
static void *class2_map(EmAimFireWorldLive *w,uint32_t a,size_t n,int write)
{
    if (!w || write) return NULL;
    const EmActorClassLists *ls=w->lists?w->lists:em_collision_world_lists();
    if (!ls) return NULL;
    const EmActorClassList *l=&ls->list[EM_ACTOR_LIST_CLASS2];
    if (l->published<0 || l->published>EM_ACTOR_LIST_MAX || l->live<0 || l->live>EM_ACTOR_LIST_MAX)
        return NULL;
    void *p=span(a,n,0x00275B94u,2,(void *)&l->published);
    if (p) return p;
    w->class2_cursor=CLASS2_BASE-4u*(unsigned)l->published;
    p=span(a,n,0x00275B8Cu,4,&w->class2_cursor);
    if (p) return p;
    /* Live pushes overwrite the same slots from the end. Read current slot[]
     * even when the published count is from the preceding close-out. */
    const unsigned count=l->published>l->live?(unsigned)l->published:(unsigned)l->live;
    const uint32_t low=CLASS2_BASE-4u*count;
    if (a<low || (uint64_t)a+n>CLASS2_BASE || !n) return NULL;
    const unsigned first=(a-low)/4u,last=(unsigned)((a-low+n-1)/4u);
    for (unsigned k=first;k<=last;++k) {
        const EmActor *actor=l->slot[count-1-k];
        const uint32_t address=actor_address(w,actor);
        if (!actor || !address) return NULL;
        w->class2_words[EM_ACTOR_LIST_MAX-count+k]=address;
    }
    return (uint8_t *)w->class2_words+4u*EM_ACTOR_LIST_MAX-(CLASS2_BASE-a);
}
void *em_aim_fire_world_live_map(void *context,uint32_t a,size_t n,int write)
{
    EmAimFireWorldLive *w=context;
    if (!w || !n || (uint64_t)a+n>UINT64_C(0x100000000)) return NULL;
    for (unsigned i=0;w->regions && i<w->region_count;++i) {
        const EmPoseRegion *r=&w->regions[i];
        void *p=(!write||r->writable)?span(a,n,r->address,r->size,r->bytes):NULL;
        if(p)return p;
    }
    void *p=collision_map(w,a,n,write);
    if(p)return p;
    p=class2_map(w,a,n,write);
    if(p)return p;
    const EmCollisionWorldOwners *o=owners(w);
    if(o && o->record_bytes && n<=UINT32_MAX) {
        p=o->record_bytes(o->context,a,(uint32_t)n);
        if(p)return p;
    }
    return w->map?w->map(w->context,a,n,write):NULL;
}

/* The region-only verified owners share these exact byte pointers. There is no
 * retry after a fault and no replay of a routine that has already stored. */
typedef struct {
    EmAimFireWorldLive *world; EmAimFireLive *live;
    EmPoseRegion views[EM_AIM_FIRE_WORLD_VIEWS]; unsigned count;
    EmArea02MathRegion math[EM_AIM_FIRE_WORLD_VIEWS];
    EmArea00WorldRegion hit[EM_AIM_FIRE_WORLD_VIEWS];
    EmArea01SideRegion side[EM_AIM_FIRE_WORLD_VIEWS];
    EmArea01RenderView fx[EM_AIM_FIRE_WORLD_VIEWS];
    EmArea02Math *math_state; EmArea00World *hit_state;
    EmArea01Side *side_state; EmArea00Fx *fx_state;
    uint32_t side_node;
    EmPoseRegion parent_view;
} Bridge;
static int append(Bridge *b,uint32_t a,uint32_t n,void *bytes,int writable)
{
    if(!bytes||!n)return 0;
    if(b->count==EM_AIM_FIRE_WORLD_VIEWS)return -1;
    b->views[b->count++]=(EmPoseRegion){a,n,bytes,writable};return 0;
}
static int refresh(Bridge *b)
{
    EmAimFireLive *h=b->live; EmAimFireWorldLive *w=b->world;
    b->count=0;
    TRY(append(b,LOCAL_BASE,sizeof h->temporary,h->temporary,1));
    if(h->player)TRY(append(b,h->player_address,sizeof h->player->bytes,h->player->bytes,1));
    for(unsigned i=0;h->regions&&i<h->region_count;++i) {
        const EmPoseRegion *r=&h->regions[i];TRY(append(b,r->address,r->size,r->bytes,r->writable));
    }
    for(unsigned i=0;h->pose&&i<h->pose->region_count&&i<EM_POSE_REGION_MAX;++i) {
        const EmPoseRegion *r=&h->pose->region[i];TRY(append(b,r->address,r->size,r->bytes,r->writable));
    }
    for(unsigned i=0;w->regions&&i<w->region_count;++i) {
        const EmPoseRegion *r=&w->regions[i];TRY(append(b,r->address,r->size,r->bytes,r->writable));
    }
    if(w->enumerate) {
        unsigned count=0;
        TRY(w->enumerate(w->context,b->views+b->count,EM_AIM_FIRE_WORLD_VIEWS-b->count,&count));
        if(count>EM_AIM_FIRE_WORLD_VIEWS-b->count)return -1;
        b->count+=count;
    }
    if(b->parent_view.bytes) {
        int found=0;
        for(unsigned i=0;i<b->count;++i) {
            const EmPoseRegion *r=&b->views[i];
            found|=r->address<=b->parent_view.address &&
                   (uint64_t)r->address+r->size>=(uint64_t)b->parent_view.address+4 &&
                   r->bytes+(b->parent_view.address-r->address)==b->parent_view.bytes;
        }
        if(!found)TRY(append(b,b->parent_view.address,4,b->parent_view.bytes,1));
    }
    for(unsigned i=0;i<b->count;++i) {
        const EmPoseRegion *r=&b->views[i];
        b->math[i]=(EmArea02MathRegion){r->address,r->size,r->bytes};
        b->hit[i]=(EmArea00WorldRegion){r->address,r->size,r->bytes};
        b->side[i]=(EmArea01SideRegion){r->address,r->size,r->bytes};
        b->fx[i]=(EmArea01RenderView){r->address,r->size,r->bytes};
    }
    if(b->math_state)b->math_state->region_count=b->count;
    if(b->hit_state)b->hit_state->region_count=b->count;
    if(b->side_state)b->side_state->region_count=b->count;
    if(b->fx_state)b->fx_state->core.world.view_count=b->count;
    return 0;
}
static int forward(Bridge *b,uint32_t fn,uint32_t sp,uint64_t *a,unsigned na,
                   uint32_t *f,unsigned nf,uint64_t *v0,uint32_t *f0)
{
    EmAimFireTargetCall c={0};
    if(na>sizeof c.a/sizeof c.a[0] || nf>sizeof c.f/sizeof c.f[0])return -1;
    c.function=fn;c.sp=sp;c.na=na;c.nf=nf;
    memcpy(c.a,a,na*sizeof *a);memcpy(c.f,f,nf*sizeof *f);
    TRY(em_aim_fire_live_call(b->live,&c));
    *v0=c.v0;*f0=c.f0;
    return refresh(b);
}
static int math_call(void *ctx,EmArea02MathCall *c)
{ return forward(ctx,c->fn,c->sp,c->a,c->na,c->f,c->nf,&c->v0,&c->f0); }
static int hit_call(void *ctx,EmArea00WorldCall *c)
{ return forward(ctx,c->fn,c->sp,c->a,c->na,c->f,c->nf,&c->v0,&c->f0); }
static int side_call(void *ctx,EmArea01SideCall *c)
{
    Bridge *b=ctx;
    /* The verified EFE00 owner stores node+24 and immediately calls the first
     * position copy. Certify that actual store before the callee observes it;
     * enumerating or acquiring the writable view never certifies a store. */
    if(b->side_node && c->fn==0x00102948u && (uint32_t)c->a[0]==b->side_node+0xB0u) {
        if(b->world->written)TRY(b->world->written(b->world->context,b->side_node+0x24u,4));
        b->side_node=0;
    }
    TRY(forward(b,c->fn,c->sp,c->a,c->na,c->f,c->nf,&c->v0,&c->f0));
    if(c->fn==0x001EF9D0u && c->v0) {
        /* A newly allocated effect has no initialized parent yet. Publish
         * only the exact upcoming store, after checking array capacity. */
        if(b->count==EM_AIM_FIRE_WORLD_VIEWS)return -1;
        const uint32_t node=(uint32_t)c->v0;
        uint8_t *p=em_aim_fire_live_map(b->live,node+0x24u,4,1);
        if(!p)return -1;
        b->side_node=node;b->parent_view=(EmPoseRegion){node+0x24u,4,p,1};
        const unsigned i=b->count++;
        b->views[i]=b->parent_view;
        b->side[i]=(EmArea01SideRegion){node+0x24u,4,p};
        b->side_state->region_count=b->count;
    }
    return 0;
}
static int fx_call(void *ctx,uint32_t fn,uint32_t sp,const EmArea00FxRegs *r,uint64_t *v0,uint32_t *f0)
{
    uint64_t a[7]={0};uint32_t f[8]={0};unsigned na=0,nf=0;
    if((r->imask&~(0x7Fu<<4)) || (r->fmask&~(0xFFu<<12)))return -1;
    for(unsigned i=0;i<7;++i)if(r->imask&(1u<<(i+4))) {a[i]=r->r[i+4];na=i+1;}
    for(unsigned i=0;i<8;++i)if(r->fmask&(1u<<(i+12))) {f[i]=r->f[i+12];nf=i+1;}
    return forward(ctx,fn,sp,a,na,f,nf,v0,f0);
}
static int fault(EmAimFireLive *h,uint32_t fn,uint32_t a)
{
    if(!h->fault_function)h->fault_function=fn;
    if(a&&!h->fault_address)h->fault_address=a;
    return -1;
}
int em_aim_fire_world_live_call(void *context,EmAimFireLive *h,EmAimFireTargetCall *c)
{
    EmAimFireWorldLive *w=context;
    if(!w||!h||!c||h->fault_function||h->fault_address)return -1;
    const uint32_t a=(uint32_t)c->a[0],d=(uint32_t)c->a[1];
    if(c->function==0x001839A0u) {
        const uint8_t *p=em_aim_fire_live_map(h,a+3u,1,0);
        if(!p)return -1;
        c->v0=em_aim_fire_001839A0(*p);return 0;
    }
    if(c->function==0x001B1510u) {c->f0=em_aim_fire_001B1510(c->f[0]);return 0;}
    if(c->function==0x0019A570u || c->function==0x0019B6C0u) {
        const EmCollSegment *s=segment(w);
        if(!s||!s->state||!s->world||!s->face)return fault(h,c->function,0);
        float from[3],to[3];
        const void *p=em_aim_fire_live_map(h,a,12,0);if(!p)return -1;memcpy(from,p,12);
        p=em_aim_fire_live_map(h,d,12,0);if(!p)return -1;memcpy(to,p,12);
        int result;
        if(c->function==0x0019A570u)
            result=em_coll_segment_0019A570(s,from,to,(unsigned)c->a[2],(int32_t)c->a[3]);
        else {
            EmCollProbeWorkers workers={(void *)s,em_coll_segment_face_worker,em_coll_segment_round_worker};
            result=em_coll_probe_0019B6C0(s->world,&workers,s->state,from,to);
        }
        if(result<0)return fault(h,c->function,0);
        c->v0=(uint32_t)result;return 0;
    }
    if(c->function!=0x00183C40u && c->function!=0x001B41F0u &&
       c->function!=0x001F4F40u && c->function!=0x001EFE00u)return -1;
    Bridge b={0};b.world=w;b.live=h;
    TRY(refresh(&b));
    int status;int32_t result=0;
    if(c->function==0x00183C40u) {
        EmArea02Math s={b.math,b.count,math_call,&b,c->sp,0,0,0};b.math_state=&s;
        status=em_area02_math_00183C40(&s,a,d);
        if(status<0||s.fault)return fault(h,s.fault_function?s.fault_function:c->function,s.fault_address);
    } else if(c->function==0x001B41F0u) {
        EmArea00World s={b.hit,b.count,hit_call,&b,c->sp,0,0,0};b.hit_state=&s;
        status=em_area00_world_001B41F0(&s,a,d,(uint32_t)c->a[2],(uint32_t)c->a[3],
                                      (uint32_t)c->a[4],(uint32_t)c->a[5],&result);
        if(status<0||s.fault)return fault(h,s.fault_function?s.fault_function:c->function,s.fault_address);
        c->v0=(uint32_t)result;
    } else if(c->function==0x001EFE00u) {
        EmArea01Side s={b.side,b.count,side_call,&b,c->sp,0,0,0};b.side_state=&s;
        status=em_area01_side_001EFE00(&s,(int32_t)a,d,&result);
        if(status<0||s.fault)return fault(h,s.fault_function?s.fault_function:c->function,s.fault_address);
        c->v0=(uint32_t)result;
    } else {
        EmArea00Fx s={0};s.core.world.views=b.fx;s.core.world.view_count=b.count;
        s.call=fx_call;s.ctx=&b;s.sp=c->sp;b.fx_state=&s;
        status=em_area00_fx_001F4F40(&s,c->a[0],&c->v0);
        if(status<0||s.core.fault.code)return fault(h,s.core.fault.address?s.core.fault.address:c->function,s.core.fault.detail);
    }
    return 0;
}
