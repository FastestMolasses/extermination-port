#include "game/em_aim_fire_world_live.h"
#include "game/em_aim_fire_leaves.h"
#include "game/em_area00_fx.h"
#include "game/em_area00_hud.h"
#include "game/em_area00_world.h"
#include "game/em_area01_side.h"
#include "game/em_area01_ui.h"
#include "game/em_area01_render_gs.h"
#include "game/em_area02_math.h"
#include "game/em_area02_misc.h"
#include "game/em_player.h"
#include "game/em_effects_live.h"
#include "game/em_coll_grid_hull.h"
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
/* D_008102B0 as 001A7280 reads it (em_coll_grid_hull's EmCollHullWorld
 * player): its +0x00, +0x09, +0x58 and +0x5C..+0x5E, refreshed from the
 * player record before each lock; 0x700031D4 names it as 0x008102B0. */
static EmActor player_body;
#define PLAYER_RECORD UINT32_C(0x008102B0)
static uint32_t actor_address(EmAimFireWorldLive *w,const EmActor *a)
{
    if(a==&player_body)return PLAYER_RECORD;
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
static uint32_t cell16_reads;
uint32_t em_aim_fire_world_live_cell_quadwords(void) { return cell16_reads; }
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
    if (a==0x700030D0u && n==16 && !write) {
        /* The cell record's +0x20 and +0x24..+0x2C as one quadword: the
         * two fields the walkers keep at those addresses (the hull locks'
         * +0x20 word, the cell walkers' normal). */
        const void *w20=collision_map(w,0x700030D0u,4,0);
        const void *nrm=w20?collision_map(w,0x700030D4u,12,0):NULL;
        if (!nrm) return NULL;
        memcpy(w->cell16,w20,4);memcpy(w->cell16+1,nrm,12);
        ++cell16_reads;
        return w->cell16;
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
    EmPoseRegion views[EM_AIM_FIRE_WORLD_VIEWS];
    EmArea02MathRegion math[EM_AIM_FIRE_WORLD_VIEWS];
    EmArea00WorldRegion hit[EM_AIM_FIRE_WORLD_VIEWS];
    EmArea01SideRegion side[EM_AIM_FIRE_WORLD_VIEWS];
    EmArea01RenderView fx[EM_AIM_FIRE_WORLD_VIEWS];
    EmArea02MiscRegion misc[EM_AIM_FIRE_WORLD_VIEWS];
    EmArea00HudRegion hud[EM_AIM_FIRE_WORLD_VIEWS];
    struct Bridge *bridge; /* this depth's running call, NULL before it has one */
} BridgeViews;
/* One view set per nesting depth of em_aim_fire_world_live_call (a
 * composition callee can come back through it), allocated on first use
 * and kept: a nested call never touches its caller's views. */
static BridgeViews **bridge_slabs;
static unsigned bridge_slab_count,bridge_depth;
typedef struct Bridge {
    EmAimFireWorldLive *world; EmAimFireLive *live;
    EmPoseRegion *views; unsigned count;
    EmArea02MathRegion *math;
    EmArea00WorldRegion *hit;
    EmArea01SideRegion *side;
    EmArea01RenderView *fx;
    EmArea02MiscRegion *misc;
    EmArea00HudRegion *hud;
    EmArea00Hud *hud_state;
    EmArea02Misc *misc_state;
    EmArea01Ui *ui_state;
    EmArea02Math *math_state; EmArea00World *hit_state;
    EmArea01Side *side_state; EmArea00Fx *fx_state;
    uint32_t side_node, target_node;
    int hit_jitter;
    EmPoseRegion parent_view;
    /* The glow node 001F6FB0's parent record (its +0x24): header, +0x60,
     * +0x80 and +0xB0 as the pool's fields, +0xD0 a read-only copy. */
    EmPoseRegion glow_view[6];
    unsigned glow_views;
    uint8_t glow_matrix[64], glow_block[0x100];
    uint32_t point[4];
    /* The published class-4 list (D_00275B7C / D_00275B84, base D_0028AE30)
     * as views: 001AA840 walks it (the knife's reach). */
    uint32_t c4_cursor, c4_words[EM_ACTOR_LIST_MAX];
    int16_t c4_count;
    /* Published class-4 entries freed since publication: their cleared
     * header words (see refresh and owner_view). */
    uint32_t freed_address[EM_ACTOR_LIST_MAX];
    const uint8_t *freed_bytes[EM_ACTOR_LIST_MAX];
    unsigned freed_count;
} Bridge;
#define FREED_HEADER 0x10u
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
    b->count=0;b->freed_count=0;
    TRY(append(b,LOCAL_BASE,sizeof h->temporary,h->temporary,1));
    /* First: a spat hazard's hit (001E2E80) stores the pending infection
     * +0x22C between two stages, where the record's word is not its storage. */
    for(unsigned i=0;h->player&&i<4;++i)TRY(append(b,h->player_address+0x220u+4u*i,4,h->vitals[i],1));
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
    for(unsigned i=0;i<b->glow_views;++i)
        TRY(append(b,b->glow_view[i].address,b->glow_view[i].size,b->glow_view[i].bytes,b->glow_view[i].writable));
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
                    /* A record freed after the list was published stays in
                     * it until the next build; 001AA840 still reads its
                     * +0, +2 and +3. The original memory then holds what
                     * 001AFC10 (byte-matched) left: its four header words
                     * +0x00..+0x0F cleared, which the native record keeps
                     * (em_actor_pool_free_001AFC10). Those 16 bytes, read
                     * only, for the region-only effect owners (owner_view);
                     * no other field of a freed record is exposed. */
                    for(int j=0;j<l->published;++j) {
                        const EmActor *actor=l->slot[j];
                        if(!actor->allocated) {
                            b->freed_address[b->freed_count]=actor_address(w,actor);
                            b->freed_bytes[b->freed_count++]=&actor->status;
                        }
                    }
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
            /* The cell record D_700030B0 (an entity or cell hit): its
             * halfword +0x1A (0x700030CA, the class) and words +0x1C / +0x20
             * (0x700030CC / 0x700030D0, the hull walkers'), read only;
             * 00189FE0 (byte-matched decomp C) passes the word at record
             * +0x1C to 001B41F0, the spat hazard 001E2E80 tests the byte
             * +0x1A of the record a 0019A570 hit names. */
            if(node==0x700030B0u) {
                static const uint32_t record_words[][2]={{0x700030CAu,2},{0x700030CCu,4},{0x700030D0u,4}};
                for(unsigned k=0;k<3;++k) {
                    void *q=collision_map(w,record_words[k][0],record_words[k][1],0);
                    if(q)APPEND_OPTIONAL(append(b,record_words[k][0],record_words[k][1],q,0));
                }
            }
        }
        /* 001B41F0's spray rewrites the collision ratio and cross lanes
         * before reading them. The region-only hit owner borrows those
         * same fields that the direct SDK/segment map already exposes. */
        static const uint32_t jitter[][2]={{0x70003680u,4},{0x70003684u,8}};
        for(unsigned i=0;b->hit_jitter && i<sizeof jitter/sizeof jitter[0];++i) {
            void *p=collision_map(w,jitter[i][0],jitter[i][1],1);
            if(p)TRY(append(b,jitter[i][0],jitter[i][1],p,1));
        }
    }
    if(b->target_node && w->target_regions) {
        unsigned count=0;
        TRY(w->target_regions(w->context,b->target_node,b->views+b->count,
                             EM_AIM_FIRE_WORLD_VIEWS-b->count,&count));
        if(count>EM_AIM_FIRE_WORLD_VIEWS-b->count)return -1;
        b->count+=count;
    }
    for(unsigned i=0;i<b->count;++i) {
        const EmPoseRegion *r=&b->views[i];
        b->math[i]=(EmArea02MathRegion){r->address,r->size,r->bytes};
        b->hit[i]=(EmArea00WorldRegion){r->address,r->size,r->bytes};
        b->side[i]=(EmArea01SideRegion){r->address,r->size,r->bytes};
        b->fx[i]=(EmArea01RenderView){r->address,r->size,r->bytes};
        b->misc[i]=(EmArea02MiscRegion){r->address,r->size,r->bytes};
        if(b->hud)b->hud[i]=(EmArea00HudRegion){r->address,r->size,r->bytes};
    }
    if(b->hud_state)b->hud_state->region_count=b->count;
    if(b->misc_state)b->misc_state->region_count=b->count;
    if(b->math_state)b->math_state->region_count=b->count;
    if(b->hit_state)b->hit_state->region_count=b->count;
    if(b->side_state)b->side_state->region_count=b->count;
    if(b->fx_state)b->fx_state->core.world.view_count=b->count;
    if(b->ui_state)b->ui_state->core.world.view_count=b->count;
    return 0;
}
/* The region-only effect owners' memory (EmArea01RenderWorld.view): a
 * freed class-4 header first (reads inside its 16 bytes only; a write, or
 * an access reaching past it, gets nothing), then the views in order,
 * exactly as em_a01r_mem searches them. */
static uint8_t *owner_view(void *ctx,uint32_t a,uint32_t n,int write)
{
    const Bridge *b=ctx;
    if(!b||!n)return NULL;
    for(unsigned k=0;k<b->freed_count;++k) {
        const uint32_t base=b->freed_address[k];
        if((uint64_t)a+n<=base || (uint64_t)a>=(uint64_t)base+FREED_HEADER)continue;
        return !write && a>=base && (uint64_t)a+n<=(uint64_t)base+FREED_HEADER
               ? (uint8_t *)(uintptr_t)b->freed_bytes[k]+(a-base) : NULL;
    }
    for(unsigned i=0;i<b->count;++i) {
        const EmArea01RenderView *v=&b->fx[i];
        if(v->bytes && a>=v->address && (uint64_t)a+n<=(uint64_t)v->address+v->size)
            return v->bytes+(a-v->address);
    }
    return NULL;
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
static int hud_call(void *ctx,EmArea00HudCall *c)
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
/* The player's hull chain: resource 0x3A at D_0028A578[0] = 0xD1B9C0
 * (tools/export_player_hull.py, verified against the captures), and the
 * matrices *(+0x110 + 4m) + 0x90 of its +0x09 bone slots as they stand. */
#define PLAYER_HULL_PATH "assets/player_hull.emom"
#define PLAYER_HULL_ADDRESS UINT32_C(0x00D1B9C0)
#define PLAYER_HULL_SIZE 0x800u
static struct { int tried, loaded; uint8_t bytes[PLAYER_HULL_SIZE]; float slots[16*32]; } player_hull;
static uint32_t rd32le(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static int player_hull_load(void)
{
    if(player_hull.loaded)return 0;
    if(player_hull.tried)return -1;
    player_hull.tried=1;
    FILE *f=fopen(PLAYER_HULL_PATH,"rb");
    uint8_t head[0x20];
    int ok=f && fread(head,1,sizeof head,f)==sizeof head && !memcmp(head,"EMOM",4) && rd32le(head+4)==1 &&
           rd32le(head+8)==PLAYER_HULL_ADDRESS && rd32le(head+12)==PLAYER_HULL_SIZE &&
           fread(player_hull.bytes,1,PLAYER_HULL_SIZE,f)==PLAYER_HULL_SIZE;
    if(f)fclose(f);
    if(!ok) {
        fprintf(stderr,"aim/fire world: %s is missing or invalid (tools/export_player_hull.py)\n",PLAYER_HULL_PATH);
        return -1;
    }
    player_hull.loaded=1;return 0;
}
static int player_chain(void *context,const EmActor *body,EmCollHullChain *out)
{
    EmAimFireLive *h=context;
    if(body!=&player_body || !h || !h->player || player_hull_load()<0)return -1;
    const uint8_t *r=h->player->bytes;
    if(rd32le(r+0x58)!=PLAYER_HULL_ADDRESS || r[9]>32)return -1;
    for(unsigned i=0;i<r[9];++i) {
        const uint32_t slot=rd32le(r+0x110+4*i);
        const void *m=slot<=UINT32_MAX-0x90u ? em_aim_fire_live_map(h,(slot+0x90u)&~15u,64,0) : NULL;
        if(!m)return -1;
        memcpy(player_hull.slots+16*i,m,64);
    }
    *out=(EmCollHullChain){player_hull.bytes,PLAYER_HULL_SIZE,player_hull.slots,r[9]};
    return 0;
}
const void *em_aim_fire_world_live_player_body(void) { return &player_body; }
const uint8_t *em_aim_fire_world_live_player_hull(uint32_t address,uint32_t *size)
{
    if(!size || player_hull_load()<0 || address<PLAYER_HULL_ADDRESS ||
       address-PLAYER_HULL_ADDRESS>=PLAYER_HULL_SIZE)return NULL;
    *size=PLAYER_HULL_SIZE-(address-PLAYER_HULL_ADDRESS);
    return player_hull.bytes+(address-PLAYER_HULL_ADDRESS);
}
/* 0019AA80(from, to, mask) over the one probe state:
 * em_coll_segment_0019AA80 (its one translation; the AREA01 collision view
 * runs the same), against the player record and this composition's view
 * of his hull chain. */
static int probe_0019AA80(EmAimFireWorldLive *w,EmAimFireLive *h,EmAimFireTargetCall *c)
{
    w->move_last=0;
    const EmCollSegment *s=segment(w);
    if(!s||!s->state||!s->face||!h->player||c->na!=3)return fault(h,c->function,0);
    const uint32_t a=(uint32_t)c->a[0],d=(uint32_t)c->a[1];
    /* Sources inside the destination words would make the interleaved
     * copy order matter: refused. */
    if((a<0x700031B0u && a+12u>0x70003190u) || (d<0x700031B0u && d+12u>0x70003190u))
        return fault(h,c->function,a);
    const void *pa=em_aim_fire_live_map(h,a,12,0);if(!pa)return fault(h,c->function,a);
    const void *pd=em_aim_fire_live_map(h,d,12,0);if(!pd)return fault(h,c->function,d);
    const uint8_t *r=h->player->bytes;
    memset(&player_body,0,sizeof player_body);
    player_body.status=r[0];player_body.bones=r[9];
    player_body.w58=rd32le(r+0x58);player_body.w5C=rd32le(r+0x5C);
    const EmCollHullWorld hw={h,player_chain,&player_body};
    float from[3],to[3];memcpy(from,pa,12);memcpy(to,pd,12);
    const int result=em_coll_segment_0019AA80(s->state,s->face,&hw,from,to,(uint32_t)c->a[2],NULL);
    if(result<0)return fault(h,0x001A7280u,0);
    c->v0=(uint64_t)result;
    return 0;
}
static int world_call(void *context,EmAimFireLive *h,EmAimFireTargetCall *c,BridgeViews *v)
{
    EmAimFireWorldLive *w=context;
    if(!w||!h||!c||h->fault_function||h->fault_address)return -1;
    const uint32_t a=(uint32_t)c->a[0],d=(uint32_t)c->a[1];
    if(c->function==0x0019AA80u)return probe_0019AA80(w,h,c);
    if(c->function==0x001D04B0u) {
        /* 001D04B0(m, kind, source; f12, f13), the spat hazard's particle
         * draw (001E2E80 with its +0xD0 and D_00253740 .. D_00253A10):
         * em_effects_live's translation over the exported ELF source block
         * and the matrix as it stands. */
        float m[16];
        const void *pm=em_aim_fire_live_map(h,a,64,0);
        if(!pm || c->na!=3 || c->nf!=2)return fault(h,c->function,a);
        memcpy(m,pm,64);
        if(em_effects_live_001D04B0_elf(m,(int32_t)d,(uint32_t)c->a[2],c->f[0],c->f[1])<0)
            return fault(h,c->function,em_effects_live_fault());
        return 0;
    }
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
       c->function!=0x001CD070u && c->function!=0x001F2BA0u && c->function!=0x001C6200u &&
       c->function!=0x001E2E80u && c->function!=0x001F02C0u && c->function!=0x001F6FB0u &&
       c->function!=0x001DEE80u && c->function!=0x001DEEC0u && c->function!=0x001DF5A0u &&
       c->function!=0x001DF9D0u)return -1;
    Bridge b;memset(&b,0,sizeof b);b.world=w;b.live=h;
    b.views=v->views;b.math=v->math;b.hit=v->hit;b.side=v->side;b.fx=v->fx;b.misc=v->misc;b.hud=v->hud;
    v->bridge=&b;
    b.hit_jitter=c->function==0x001B41F0u || c->function==0x00189FE0u;
    if(c->function==0x00183C40u)b.target_node=a;
    if(c->function==0x0022BBC0u) {
        /* A burst on a creature (node +0x24): its +0x110 words and bones. */
        const uint8_t *pp=em_aim_fire_live_map(h,a+0x24u,4,0);
        const uint32_t scn=pp?(uint32_t)pp[0]|(uint32_t)pp[1]<<8|(uint32_t)pp[2]<<16|(uint32_t)pp[3]<<24:0;
        if(scn && scn!=h->player_address)b.target_node=scn;
    }
    /* 001DF5A0's 001DF180 keeps a 0x10F0-byte frame (its 16 x 16 grid):
     * the runtime's private stack view below 0x7E001400 (em_aim_fire_runtime
     * add_windows), which its SDK callees reach through the same map. */
    const uint32_t ramp_sp=c->function==0x001DF5A0u || c->function==0x001DF9D0u ? 0x7E001400u : c->sp;
    /* 001F6FB0 dereferences its parent (node +0x24) only in its states 0
     * and 1 (0 falls into 1). In states 2 and 3 it only frees itself
     * (001AFC10(node)) and in any other state it returns at once. By its
     * state-3 frame the parent is normally free already: the glow set
     * both +4 bytes to 3 the frame before, and the parent, ahead of it in
     * the pool walk, frees itself at +4 = 3 (00128C10's state 3), its
     * record then free or reused. So the parent's views below are made
     * only for states 0 and 1, where a parent that is not mapped still
     * faults. */
    const uint8_t *glow_state=c->function==0x001F6FB0u ? em_aim_fire_live_map(h,a+4u,1,0) : NULL;
    if(c->function==0x001F6FB0u && !glow_state)return fault(h,c->function,a+4u);
    if(c->function==0x001F6FB0u && *glow_state<=1u) {
        /* The glow's parent (node +0x24): the pool fields it reads and
         * writes and its +0xD0 matrix as it stands. */
        const uint8_t *pp=em_aim_fire_live_map(h,a+0x24u,4,0);
        if(!pp)return fault(h,c->function,a+0x24u);
        const uint32_t parent=(uint32_t)pp[0]|(uint32_t)pp[1]<<8|(uint32_t)pp[2]<<16|(uint32_t)pp[3]<<24;
        static const uint32_t spans[][3]={{0,0x14,1},{0x60,0x20,1},{0x80,0x10,1},{0xB0,0x10,0}};
        for(unsigned k=0;k<4;++k) {
            void *q=em_aim_fire_live_map(h,parent+spans[k][0],spans[k][1],(int)spans[k][2]);
            if(!q)return fault(h,c->function,parent+spans[k][0]);
            b.glow_view[b.glow_views++]=(EmPoseRegion){parent+spans[k][0],spans[k][1],q,(int)spans[k][2]};
        }
        if(!w->actor_snapshot || w->actor_snapshot(w->context,parent+0xD0u,64,b.glow_matrix)<0)
            return fault(h,c->function,parent+0xD0u);
        b.glow_view[b.glow_views++]=(EmPoseRegion){parent+0xD0u,64,b.glow_matrix,0};
        /* Its work block (the glow reads the halfword +0xF4, the parent's
         * animation flags), read only. */
        if(w->actor_snapshot(w->context,parent+0x1F0u,0x100,b.glow_block)<0)
            return fault(h,c->function,parent+0x1F0u);
        b.glow_view[b.glow_views++]=(EmPoseRegion){parent+0x1F0u,0x100,b.glow_block,0};
    }
    if(refresh(&b)<0) {
        fprintf(stderr,"aim/fire world: %08X refresh failed (%u views)\n",
            (unsigned)c->function,b.count);
        return -1;
    }
    int status;int32_t result=0;
    if(c->function==0x00183C40u) {
        EmArea02Math s={b.math,b.count,math_call,&b,c->sp,0,0,0,NULL};b.math_state=&s;
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
    } else if(c->function==0x001E2E80u) {
        /* The spat hazard node's behaviour (entity 0x80000008, subtype 3:
         * the creatures' spray through 001EFFD0, which leaves +0x20 the
         * scale and +0x94 the tag): em_area00_hud, its one translation
         * (test_area00_hud_reference.py), over this depth's views; its
         * callees come back through the composition. */
        EmArea00Hud s;memset(&s,0,sizeof s);
        s.regions=b.hud;s.region_count=b.count;s.call=hud_call;s.ctx=&b;s.sp=c->sp;b.hud_state=&s;
        status=em_area00_hud_001E2E80(&s,a);
        if(status<0||s.fault) {
            fprintf(stderr,"aim/fire world: 001E2E80 hud fault %d at %08X\n",(int)s.fault,(unsigned)s.fault_address);
            return fault(h,s.fault_function?s.fault_function:c->function,s.fault_address);
        }
    } else if(c->function==0x001DEE80u || c->function==0x001DEEC0u || c->function==0x001DF5A0u ||
              c->function==0x001DF9D0u) {
        /* The render context's ramp records (the flash node 001E7310's
         * 001DEE80 / 001DEEC0 / 001DF5A0): em_area00_hud's translations over
         * this depth's views (the render context's storage among them). */
        EmArea00Hud s;memset(&s,0,sizeof s);
        s.regions=b.hud;s.region_count=b.count;s.call=hud_call;s.ctx=&b;s.sp=ramp_sp;b.hud_state=&s;
        uint32_t out=0;
        status=c->function==0x001DEE80u ? em_area00_hud_001DEE80(&s,a,d,&out)
              : c->function==0x001DEEC0u ? em_area00_hud_001DEEC0(&s,a,d,&out)
              : c->function==0x001DF9D0u ? em_area00_hud_001DF9D0(&s,c->f[0])
                                         : em_area00_hud_001DF5A0(&s,c->f[0]);
        if(status<0||s.fault) {
            fprintf(stderr,"aim/fire world: %08X hud fault %d at %08X\n",(unsigned)c->function,(int)s.fault,(unsigned)s.fault_address);
            return fault(h,s.fault_function?s.fault_function:c->function,s.fault_address);
        }
        if(c->function!=0x001DF5A0u && c->function!=0x001DF9D0u)c->v0=out;
    } else if(c->function==0x001EFE00u) {
        EmArea01Side s={b.side,b.count,side_call,&b,c->sp,0,0,0,NULL};b.side_state=&s;
        status=em_area01_side_001EFE00(&s,(int32_t)a,d,&result);
        if(status<0||s.fault)return fault(h,s.fault_function?s.fault_function:c->function,s.fault_address);
        c->v0=(uint32_t)result;
    } else {
        EmArea00Fx s={0};s.core.world.views=b.fx;s.core.world.view_count=b.count;
        s.core.world.view=owner_view;s.core.world.view_ctx=&b;
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
        else if(c->function==0x001F6FB0u)
            /* The creatures' glow node (0x8000000F; its parent is +0x24). */
            status=em_area00_fx_001F6FB0(&s,c->a[0]);
        else if(c->function==0x001F02C0u)
            /* The spat hazard's hit sound (001F02C0(p, id; f12)). */
            status=em_area00_fx_001F02C0(&s,c->a[0],c->a[1],c->f[0]);
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
int em_aim_fire_world_live_call(void *context,EmAimFireLive *h,EmAimFireTargetCall *c)
{
    if(bridge_depth==bridge_slab_count) {
        BridgeViews **grown=realloc(bridge_slabs,(bridge_slab_count+1u)*sizeof *grown);
        if(!grown)return -1;
        bridge_slabs=grown;
        if(!(bridge_slabs[bridge_slab_count]=malloc(sizeof **bridge_slabs)))return -1;
        ++bridge_slab_count;
    }
    BridgeViews *v=bridge_slabs[bridge_depth++];
    v->bridge=NULL;
    const int status=world_call(context,h,c,v);
    v->bridge=NULL;
    --bridge_depth;
    return status;
}
void *em_aim_fire_world_live_owner_view(uint32_t a,size_t n,int write)
{
    const BridgeViews *v=bridge_depth?bridge_slabs[bridge_depth-1]:NULL;
    return v&&v->bridge&&n<=UINT32_MAX?owner_view(v->bridge,a,(uint32_t)n,write):NULL;
}
