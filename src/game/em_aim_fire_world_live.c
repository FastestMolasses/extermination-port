#include "game/em_aim_fire_world_live.h"
#include "game/em_aim_fire_leaves.h"
#include "game/em_area00_fx.h"
#include "game/em_area00_world.h"
#include "game/em_area01_side.h"
#include "game/em_area01_ui.h"
#include "game/em_area01_render_gs.h"
#include "game/em_area02_math.h"
#include "game/em_area02_misc.h"
#include <stdio.h>
#include <stdlib.h>
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
/* The move walkers' scratch (em_collision_world_move_scratch) at the same
 * scratchpad addresses, while it is the one a composition call wrote last
 * (0019B2C0 sets w->move_last; a segment probe here clears it): in the
 * original both are the one scratchpad 0x70003190.., the port keeps the
 * two walkers' states apart. */
static void *move_map(EmAimFireWorldLive *w,uint32_t a,size_t n,int write)
{
    EmCollMoveScratch *m=em_collision_world_move_scratch();
    const EmCollMoveWorld *mw=em_collision_world_move();
    void *p;
    if (!m) return NULL;
#define FIELD(at,field) do { p=span(a,n,at,sizeof(field),&(field)); if(p)return p; } while(0)
    FIELD(0x70003190u,m->start); FIELD(0x700031A0u,m->end);
    FIELD(0x700031B0u,m->point); FIELD(0x700031C0u,m->delta);
    FIELD(0x700031D8u,m->mode); FIELD(0x700030CAu,m->cell_class);
    FIELD(0x700030CCu,m->cell_word_1c); FIELD(0x700030D0u,m->cell_word_20);
    FIELD(0x700030D4u,m->cell_normal); FIELD(0x70003240u,m->rank);
    FIELD(0x7000324Eu,m->query_class); FIELD(0x70003B86u,m->span_lo);
    FIELD(0x70003B88u,m->kind); FIELD(0x70003680u,m->work);
#undef FIELD
    if (write) return NULL;
    if (span(a,n,0x700031D0u,4,&w->record_word)) {
        if (!m->record) w->record_word=0;
        else if (m->record==EM_COLL_MOVE_CELL_RECORD) w->record_word=0x700030B0u;
        else {
            const int node=mw?em_coll_grid_hull_node_index(mw->grid,m->record):-1;
            w->record_word=node>=0&&w->grid_address?w->grid_address(w->context,mw->grid,(uint32_t)node):0;
            if (!w->record_word) return NULL;
        }
        return span(a,n,0x700031D0u,4,&w->record_word);
    }
    if (span(a,n,0x700031D4u,4,&w->entity_word)) {
        w->entity_word=actor_address(w,m->entity);
        if (m->entity && !w->entity_word) return NULL;
        return span(a,n,0x700031D4u,4,&w->entity_word);
    }
    return NULL;
}
static void *collision_map(EmAimFireWorldLive *w,uint32_t a,size_t n,int write)
{
    if (w && w->move_last) return move_map(w,a,n,write);
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
    if (a>=0x700031BCu && (uint64_t)a+n<=0x700031C0u) return (uint8_t *)&w->word_31BC+(a-0x700031BCu);
    if (a==0x700031B0u && n==16 && !write) {
        const void *pt=collision_map(w,0x700031B0u,12,0);
        if (!pt) return NULL;
        memcpy(w->point16,pt,12);w->point16[3]=w->word_31BC;
        return w->point16;
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
    EmArea02MiscRegion misc[EM_AIM_FIRE_WORLD_VIEWS];
    EmArea02Misc *misc_state;
    EmArea01Ui *ui_state;
    EmArea02Math *math_state; EmArea00World *hit_state;
    EmArea01Side *side_state; EmArea00Fx *fx_state;
    uint32_t side_node;
    EmPoseRegion parent_view;
    uint32_t point[4];
    /* The published class-4 list (D_00275B7C / D_00275B84, base D_0028AE30)
     * as views: 001AA840 walks it (the knife's reach). */
    uint32_t c4_cursor, c4_words[EM_ACTOR_LIST_MAX];
    int16_t c4_count;
} Bridge;
static int append(Bridge *b,uint32_t a,uint32_t n,void *bytes,int writable)
{
    if(!bytes||!n)return 0;
    if(b->count==EM_AIM_FIRE_WORLD_VIEWS)return -1;
    b->views[b->count++]=(EmPoseRegion){a,n,bytes,writable};return 0;
}
#define APPEND_OPTIONAL(x) do { if (b->count < EM_AIM_FIRE_WORLD_VIEWS) TRY(x); } while (0)
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
        if(w->enumerate(w->context,b->views+b->count,EM_AIM_FIRE_WORLD_VIEWS-b->count,&count)<0) {
            fprintf(stderr,"aim/fire world: enumerate failed after %u\n",count);
            return -1;
        }
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
    {
        /* The collision probe's result words as views for the region-only
         * owners (001F3340 reads 0x700031B0 and the hit record through
         * 0x700031D0 after its 0019A570): the point, the record and entity
         * words (their original encodings) and the grid node's bytes. They
         * come last and only while there is room: an owner that reads one
         * without its view faults at that read. */
        static const uint32_t fields[][2]={{0x700031D0u,4},{0x700031D4u,4}};
        for(unsigned k=0;k<2;++k) {
            void *q=collision_map(w,fields[k][0],fields[k][1],0);
            if(q)APPEND_OPTIONAL(append(b,fields[k][0],fields[k][1],q,0));
        }
        /* 0x700031B0 as the quadword 001F3340's 00102948 copies: the probe's
         * point (x, y, z; every probe stores these three words) and the
         * fourth word 0x700031BC, its one owner's (word_31BC, see the
         * header); a read-only snapshot, refreshed after every call. */
        /* The published class-4 list. */
        const EmActorClassLists *ls=w->lists?w->lists:em_collision_world_lists();
        if(ls) {
            const EmActorClassList *l=&ls->list[EM_ACTOR_LIST_CLASS4];
            if(l->published>=0 && l->published<=EM_ACTOR_LIST_MAX) {
                int ok=1;
                for(int j=0;j<l->published && ok;++j) {
                    const EmActor *actor=l->slot[l->published-1-j];
                    const uint32_t address=actor_address(w,actor);
                    ok=actor && address;
                    b->c4_words[EM_ACTOR_LIST_MAX-l->published+j]=address;
                }
                if(ok) {
                    b->c4_count=l->published;
                    b->c4_cursor=0x0028AE30u-4u*(uint32_t)l->published;
                    APPEND_OPTIONAL(append(b,0x00275B7Cu,4,&b->c4_cursor,0));
                    APPEND_OPTIONAL(append(b,0x00275B84u,2,&b->c4_count,0));
                    if(l->published)
                        APPEND_OPTIONAL(append(b,b->c4_cursor,4u*(uint32_t)l->published,
                                               b->c4_words+EM_ACTOR_LIST_MAX-l->published,0));
                }
            }
        }
        const void *pt=collision_map(w,0x700031B0u,12,0);
        if(pt) {
            memcpy(b->point,pt,12);b->point[3]=w->word_31BC;
            APPEND_OPTIONAL(append(b,0x700031B0u,16,b->point,0));
        }
        const uint8_t *word=collision_map(w,0x700031D0u,4,0);
        if(word) {
            const uint32_t node=(uint32_t)word[0]|(uint32_t)word[1]<<8|(uint32_t)word[2]<<16|(uint32_t)word[3]<<24;
            const uint8_t *bytes=node && w->grid_bytes ? w->grid_bytes(w->context,node,64) : NULL;
            if(bytes)APPEND_OPTIONAL(append(b,node,64,(void *)(uintptr_t)bytes,0));
        }
    }
    for(unsigned i=0;i<b->count;++i) {
        const EmPoseRegion *r=&b->views[i];
        b->math[i]=(EmArea02MathRegion){r->address,r->size,r->bytes};
        b->hit[i]=(EmArea00WorldRegion){r->address,r->size,r->bytes};
        b->side[i]=(EmArea01SideRegion){r->address,r->size,r->bytes};
        b->fx[i]=(EmArea01RenderView){r->address,r->size,r->bytes};
        b->misc[i]=(EmArea02MiscRegion){r->address,r->size,r->bytes};
    }
    if(b->misc_state)b->misc_state->region_count=b->count;
    if(b->math_state)b->math_state->region_count=b->count;
    if(b->hit_state)b->hit_state->region_count=b->count;
    if(b->side_state)b->side_state->region_count=b->count;
    if(b->fx_state)b->fx_state->core.world.view_count=b->count;
    if(b->ui_state)b->ui_state->core.world.view_count=b->count;
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
    /* 001F3340 hands 0019A570 its s4 (the piece), which 0019A570 reads only
     * on the path where no cell span beats the node count; the live segment
     * walker faults on that path (em_coll_segment_walkers 0x19D540), so s4
     * is not forwarded. */
    uint32_t imask=r->imask;
    if(fn==0x0019A570u)imask&=~(1u<<20);
    if((imask&~(0x7Fu<<4)) || (r->fmask&~(0xFFu<<12)))return -1;
    for(unsigned i=0;i<7;++i)if(r->imask&(1u<<(i+4))) {a[i]=r->r[i+4];na=i+1;}
    for(unsigned i=0;i<8;++i)if(r->fmask&(1u<<(i+12))) {f[i]=r->f[i+12];nf=i+1;}
    return forward(ctx,fn,sp,a,na,f,nf,v0,f0);
}
static int misc_call(void *ctx,EmArea02MiscCall *c)
{ return forward(ctx,c->fn,c->sp,c->a,c->na,c->f,c->nf,&c->v0,&c->f0); }
/* 001CD070's float_to_int (001281C0) through the composition. */
static int gs_001281C0(void *ctx,uint32_t f12,int32_t *v0)
{
    uint64_t a[1]={0},r0=0;uint32_t f[1]={f12},rf0=0;
    TRY(forward(ctx,0x001281C0u,0,a,0,f,1,&r0,&rf0));
    *v0=(int32_t)(uint32_t)r0;
    return 0;
}
static int ui_call(void *ctx,uint32_t fn,uint32_t sp,const uint64_t *a,unsigned na,
                   const uint32_t *f,unsigned nf,uint64_t *v0,uint32_t *f0)
{
    uint64_t aa[8]={0};uint32_t ff[8]={0};uint64_t r0=0;uint32_t rf0=0;
    if(na>8||nf>8)return -1;
    if(na)memcpy(aa,a,na*sizeof *a);
    if(nf)memcpy(ff,f,nf*sizeof *f);
    TRY(forward(ctx,fn,sp,aa,na,ff,nf,&r0,&rf0));
    if(v0)*v0=r0;
    if(f0)*f0=rf0;
    return 0;
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
    if(c->function==0x0019B2C0u) {
        /* The knife's reach (0018A1F0's 0019B2C0(player + 0xB0, 0x700038A0,
         * 6)): em_coll_move_original's translation over the move walkers'
         * world and scratch. */
        const EmCollMoveWorld *mw=em_collision_world_move();
        EmCollMoveScratch *m=em_collision_world_move_scratch();
        if(!mw||!m)return fault(h,c->function,0);
        float from[3],to[3];
        void *pa=em_aim_fire_live_map(h,a,12,1);if(!pa)return fault(h,c->function,a);
        const void *pd=em_aim_fire_live_map(h,d,12,0);if(!pd)return fault(h,c->function,d);
        memcpy(from,pa,12);memcpy(to,pd,12);
        const int mode=em_coll_move_probe_0019B2C0(mw,m,from,to,(uint32_t)c->a[2]);
        if(mode<0)return fault(h,c->function,0);
        if((uint32_t)c->a[2]&0x80000000u)memcpy(pa,from,12);
        w->move_last=1;
        c->v0=(uint32_t)mode;return 0;
    }
    if(c->function==0x0019A570u || c->function==0x0019B6C0u) {
        w->move_last=0;
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
    if(c->function!=0x00183C40u && c->function!=0x001B41F0u && c->function!=0x001F4F40u &&
       c->function!=0x001EFE00u && c->function!=0x001F00A0u && c->function!=0x001F5040u &&
       c->function!=0x001F4010u && c->function!=0x001F3620u && c->function!=0x001F3E30u &&
       c->function!=0x001F2F90u && c->function!=0x001F3340u &&
       c->function!=0x001CA3B0u && c->function!=0x001CA4D0u && c->function!=0x001C63D0u &&
       c->function!=0x001AA840u && c->function!=0x00189EC0u && c->function!=0x001F18C0u &&
       c->function!=0x00189FE0u && c->function!=0x0018A180u && c->function!=0x001EFF10u &&
       c->function!=0x0022BBC0u && c->function!=0x001F0190u && c->function!=0x001F0290u &&
       c->function!=0x001CD070u && c->function!=0x001F2BA0u && c->function!=0x001C6200u)return -1;
    Bridge b;memset(&b,0,sizeof b);b.world=w;b.live=h;
    if(refresh(&b)<0) {
        fprintf(stderr,"aim/fire world: %08X refresh failed (%u views)\n",
            (unsigned)c->function,b.count);
        return -1;
    }
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
    } else if(c->function==0x001C63D0u) {
        /* The muzzle node's placement (001F5040's 001C63D0): em_area00_world,
         * its one translation; its 001C9610 comes back to em_aim_fire_flash. */
        EmArea00World s={b.hit,b.count,hit_call,&b,c->sp,0,0,0};b.hit_state=&s;
        status=em_area00_world_001C63D0(&s,a,&c->v0);
        if(status<0||s.fault)return fault(h,s.fault_function?s.fault_function:c->function,s.fault_address);
    } else if(c->function==0x00189EC0u || c->function==0x00189FE0u || c->function==0x0018A180u) {
        /* The knife's (flavour 4) entity test 00189EC0, strike 00189FE0 and
         * reaction 0018A180 (em_equipment_live's knife workers):
         * em_area00_world, their one translation. */
        EmArea00World s={b.hit,b.count,hit_call,&b,c->sp,0,0,0};b.hit_state=&s;
        if(c->function==0x00189EC0u)
            status=em_area00_world_00189EC0(&s,a,&result);
        else if(c->function==0x00189FE0u)
            status=em_area00_world_00189FE0(&s,a,d,(uint32_t)c->a[2]);
        else
            status=em_area00_world_0018A180(&s,a);
        if(status<0||s.fault)return fault(h,s.fault_function?s.fault_function:c->function,s.fault_address);
        c->v0=(uint32_t)result;
    } else if(c->function==0x001C6200u) {
        /* The debris node's slot reset (001F2E90's 001C6200(node)):
         * em_area00_world, its one translation; the slots' bytes are
         * em_aim_fire_trail's views. */
        EmArea00World s={b.hit,b.count,hit_call,&b,c->sp,0,0,0};b.hit_state=&s;
        status=em_area00_world_001C6200(&s,a);
        if(status<0||s.fault)return fault(h,s.fault_function?s.fault_function:c->function,s.fault_address);
    } else if(c->function==0x001CA3B0u || c->function==0x001CA4D0u) {
        /* The debris pieces' quaternion leaves (001F2F90 / 001F3620 of a
         * kind-7 piece): em_area00_world, their one translation. */
        EmArea00World s={b.hit,b.count,hit_call,&b,c->sp,0,0,0};b.hit_state=&s;
        status=c->function==0x001CA3B0u
            ? em_area00_world_001CA3B0(&s,a,c->f[0],c->f[1],c->f[2])
            : em_area00_world_001CA4D0(&s,a,d,(uint32_t)c->a[2]);
        if(status<0||s.fault)return fault(h,s.fault_function?s.fault_function:c->function,s.fault_address);
    } else if(c->function==0x001F4010u) {
        /* The shell casing's seed (00188630's 001F4010(3, 0x700036A0)):
         * em_area02_misc, its one translation; 001F2F90 / 001F3340 come back
         * through the composition (em_area00_fx_debris). */
        EmArea02Misc s={b.misc,b.count,misc_call,&b,c->sp,NULL,0,0,0};b.misc_state=&s;
        status=em_area02_misc_001F4010(&s,(uint32_t)c->a[0],(uint32_t)c->a[1]);
        if(status<0||s.fault)return fault(h,s.fault_address?s.fault_address:c->function,s.fault_address);
    } else if(c->function==0x0022BBC0u) {
        /* The bone-burst node's behaviour (the flame contact's 0x80000027,
         * subtype 9): em_area01_ui, its one translation (AREA01_UI.md);
         * its +0x110 words and slots are em_bone_burst's. */
        EmArea01Ui s;memset(&s,0,sizeof s);
        s.core.world.views=b.fx;s.core.world.view_count=b.count;
        s.call=ui_call;s.ctx=&b;s.sp=c->sp;b.ui_state=&s;
        status=em_area01_ui_0022BBC0(&s,a);
        if(status<0||s.core.fault.code) {
            fprintf(stderr,"aim/fire world: 0022BBC0 fault code %d at %08X detail %08X\n",
                (int)s.core.fault.code,(unsigned)s.core.fault.address,(unsigned)s.core.fault.detail);
            return fault(h,s.core.fault.address?s.core.fault.address:c->function,s.core.fault.detail);
        }
    } else if(c->function==0x001CD070u) {
        /* The bone burst's ring-slot screen test 001CD070(p, mask):
         * em_area01_render_gs, its one translation (AREA01_RENDER.md);
         * D_00275C04 is em_aim_fire_runtime's word. */
        EmArea01RenderGs s;memset(&s,0,sizeof s);
        s.core.world.views=b.fx;s.core.world.view_count=b.count;
        s.workers.ctx=&b;s.workers.w_001281C0=gs_001281C0;
        uint32_t v0=0;
        status=em_area01_render_001CD070(&s,a,d,&v0);
        if(status<0||s.core.fault.code) {
            fprintf(stderr,"aim/fire world: 001CD070 fault code %d at %08X detail %08X\n",
                (int)s.core.fault.code,(unsigned)s.core.fault.address,(unsigned)s.core.fault.detail);
            return fault(h,s.core.fault.address?s.core.fault.address:c->function,s.core.fault.detail);
        }
        c->v0=v0;
    } else if(c->function==0x001F0190u || c->function==0x001F0290u) {
        /* The bone burst's fog bracket (0022BBC0 subtype 9 around each
         * burst): em_area01_side, their one translation (AREA01_SIDE.md);
         * D_00275C3C is em_aim_fire_runtime's word. */
        EmArea01Side s={b.side,b.count,side_call,&b,c->sp,0,0,0,NULL};b.side_state=&s;
        status=c->function==0x001F0190u ? em_area01_side_001F0190(&s,c->f[0],c->f[1])
                                        : em_area01_side_001F0290(&s);
        if(status<0||s.fault)return fault(h,s.fault_function?s.fault_function:c->function,s.fault_address);
    } else if(c->function==0x001EFE00u) {
        EmArea01Side s={b.side,b.count,side_call,&b,c->sp,0,0,0,NULL};b.side_state=&s;
        status=em_area01_side_001EFE00(&s,(int32_t)a,d,&result);
        if(status<0||s.fault)return fault(h,s.fault_function?s.fault_function:c->function,s.fault_address);
        c->v0=(uint32_t)result;
    } else {
        EmArea00Fx s={0};s.core.world.views=b.fx;s.core.world.view_count=b.count;
        s.call=fx_call;s.ctx=&b;s.sp=c->sp;b.fx_state=&s;
        /* 001F00A0(id, pos, rot, a3): the marker's (0018ABA0) effect spawn;
         * em_area00_fx is its one bound owner (AIM_FIRE.md section 7). */
        if(c->function==0x001F00A0u)
            status=em_area00_fx_001F00A0(&s,c->a[0],c->a[1],c->a[2],c->a[3],&c->v0);
        else if(c->function==0x001F5040u)
            /* The muzzle node's behaviour (em_aim_fire_flash holds its
             * record bytes and model-node workers). */
            status=em_area00_fx_001F5040(&s,c->a[0]);
        else if(c->function==0x001AA840u)
            /* The knife's reach over the class-4 list (0018A1F0's first call);
             * its 001AA7A0 is em_aim_fire_leaves' (em_aim_fire_runtime). */
            status=em_area00_fx_001AA840(&s);
        else if(c->function==0x001F18C0u)
            /* The knife's trail node (em_aim_fire_trail holds its slots). */
            status=em_area00_fx_001F18C0(&s,c->a[0]);
        else if(c->function==0x001F2BA0u)
            /* A box's debris node (em_aim_fire_trail holds its slots). */
            status=em_area00_fx_001F2BA0(&s,c->a[0]);
        else if(c->function==0x001EFF10u)
            /* The knife's effect spawn (00189D30's 001EFF10). */
            status=em_area00_fx_001EFF10(&s,c->a[0],c->a[1],c->a[2],c->a[3],c->a[4],c->a[5],c->f[0],&c->v0);
        else if(c->function==0x001F2F90u)
            status=em_area00_fx_001F2F90(&s,c->a[0],c->a[1],c->a[2],c->a[3]);
        else if(c->function==0x001F3340u)
            status=em_area00_fx_001F3340(&s,c->a[0],c->a[1],c->a[2]);
        else if(c->function==0x001F3620u)
            /* The barrel's particle sweep 001F40C0 (em_effects_live's hook). */
            status=em_area00_fx_001F3620(&s,c->a[0],c->a[1]);
        else if(c->function==0x001F3E30u)
            status=em_area00_fx_001F3E30(&s,c->a[0],c->a[1],c->a[2],c->a[3],c->a[4]);
        else
            status=em_area00_fx_001F4F40(&s,c->a[0],&c->v0);
        if(status<0||s.core.fault.code) {
            fprintf(stderr,"aim/fire world: %08X fx fault code %d at %08X detail %08X\n",
                (unsigned)c->function,(int)s.core.fault.code,(unsigned)s.core.fault.address,(unsigned)s.core.fault.detail);
            return fault(h,s.core.fault.address?s.core.fault.address:c->function,s.core.fault.detail);
        }
    }
    return 0;
}
