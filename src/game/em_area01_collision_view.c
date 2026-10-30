#include "game/em_area01_collision_view.h"
#include <string.h>
#include <stddef.h>
_Static_assert(offsetof(EmCollSegmentFaceScratch,box_max)==offsetof(EmCollSegmentFaceScratch,box_min)+16,"3600/3610 alias");
_Static_assert(offsetof(EmCollSegmentFaceScratch,delta)==offsetof(EmCollSegmentFaceScratch,box_min)+32,"3600/3620 alias");
_Static_assert(offsetof(EmCollSegmentFaceScratch,rel)==offsetof(EmCollSegmentFaceScratch,box_min)+48,"3600/3630 alias");
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Original-address views require little-endian storage
#endif
static int fail(EmArea01CollisionView *v,uint32_t a,int reason)
{ if(v&&!v->fault){v->fault=reason;v->fault_address=a;}return -1; }
static uint8_t *span(uint32_t a,uint32_t n,uint32_t base,uint32_t size,void *p)
{ return p&&n&&a>=base&&(uint64_t)a+n<=(uint64_t)base+size?(uint8_t *)p+(a-base):NULL; }
/* The delivered file's current bytes and the rest of their region (see
 * EmArea01CollisionHost.file_rest): never a mapping a later delivery
 * replaced. */
static const uint8_t *file_map(const EmArea01CollisionView *v,uint32_t *size)
{
    if(!v->host.file_rest){*size=v->host.file.size;return v->host.file.bytes;}
    uint32_t n=0;const uint8_t *p=v->host.file_rest(v->host.ctx,v->host.file.address,&n);
    *size=p?n:0;return p;
}
static const EmCollProbeGrid *grid(const EmArea01CollisionView *v)
{ return v->host.segment->world->grid; }
static EmActorCellTable *cells(const EmArea01CollisionView *v)
{ return v->host.segment->world->cells->table; }
static const EmActorClassLists *lists(const EmArea01CollisionView *v)
{ return v->host.segment->world->cells->lists; }
/* The aim/fire composition's proxy of D_008102B0 (em_aim_fire_world_live's
 * player_body), which its segment probes leave in 0x700031D4: registered by
 * that composition, the same word. */
static const void *s_player_proxy;
void em_area01_collision_set_player_proxy(const void *proxy) { s_player_proxy=proxy; }
uint32_t em_area01_collision_pool_address(const EmActorPool *pool,const EmPlayerLiveActor *player,const void *p)
{
    if(!p)return 0;
    if(p==player || (s_player_proxy && p==s_player_proxy))return 0x008102B0u;
    /* uintptr comparisons avoid relational comparisons of unrelated C objects. */
    uintptr_t at=(uintptr_t)p,base=pool?(uintptr_t)pool->records:0;
    if(!pool||at<base||at-base>=sizeof pool->records||(at-base)%sizeof pool->records[0])return 0;
    return EM_ACTOR_POOL_BASE+(uint32_t)((at-base)/sizeof pool->records[0])*EM_ACTOR_RECORD_SIZE;
}
const void *em_area01_collision_pool_identity(const EmActorPool *pool,const EmPlayerLiveActor *player,uint32_t a)
{
    if(a==0x008102B0u)return player;
    if(!pool||a<EM_ACTOR_POOL_BASE)return NULL;
    uint32_t off=a-EM_ACTOR_POOL_BASE;
    return off%EM_ACTOR_RECORD_SIZE||off/EM_ACTOR_RECORD_SIZE>=EM_ACTOR_POOL_CAPACITY?NULL:&pool->records[off/EM_ACTOR_RECORD_SIZE];
}
static uint32_t encode(EmArea01CollisionView *v,const void *p)
{ return !p?0:v->host.address?v->host.address(v->host.ctx,p):em_area01_collision_pool_address(v->host.pool,v->host.player,p); }
static const void *decode(EmArea01CollisionView *v,uint32_t a)
{ return !a?NULL:v->host.identity?v->host.identity(v->host.ctx,a):em_area01_collision_pool_identity(v->host.pool,v->host.player,a); }
static int node_index(EmArea01CollisionView *v,uint32_t address)
{
    if(!v->staged)return fail(v,0x700031F8,1);
    uint32_t base=v->metadata[(0x3208-0x31F8)/4];
    if(address<base||(address-base)%0x40)return -1;
    uint32_t i=(address-base)/0x40;
    return i<grid(v)->count?(int)i:-1;
}
static int record_word(EmArea01CollisionView *v,EmCollProbeState *s,uint32_t *out)
{
    if(s->record==EM_COLL_PROBE_RECORD_NONE)*out=0;
    else if(s->record==EM_COLL_PROBE_RECORD_CELL)*out=0x700030B0;
    else if(s->record==EM_COLL_PROBE_RECORD_GRID&&s->node>=0&&(uint32_t)s->node<grid(v)->count&&v->staged)
        *out=v->metadata[(0x3208-0x31F8)/4]+0x40u*(uint32_t)s->node;
    else return fail(v,0x700031D0,3);
    return 0;
}
/* Existing port walkers have two representations of the ONE scratchpad.
 * Import the last writer, before exposing the probe representation. Never
 * reset the last-writer bit without transferring all overlapping fields. */
static int import_move(EmArea01CollisionView *v)
{
    if(!v->host.shared->move_last)return 0;
    EmCollMoveScratch *m=v->host.move;EmCollProbeState *s=v->host.segment->state;
    EmCollSegmentFaceScratch *f=v->host.segment->face;
    int record=EM_COLL_PROBE_RECORD_NONE,node=-1;
    if(m->record==EM_COLL_MOVE_CELL_RECORD)record=EM_COLL_PROBE_RECORD_CELL;
    else if(m->record){node=em_coll_grid_hull_node_index(grid(v),m->record);if(node<0)return fail(v,0x700031D0,3);record=EM_COLL_PROBE_RECORD_GRID;}
    memcpy(s->start,m->start,16);memcpy(s->end,m->end,16);
    memcpy(s->point,m->point,12);memcpy(s->delta,m->delta,12);
    s->record=record;s->node=node;s->entity=m->entity;s->kind=m->mode;
    s->cell_class=m->cell_class;memcpy(s->cell_normal,m->cell_normal,12);
    s->query_class=m->query_class;s->self=m->self;memcpy(s->rank,m->rank,12);
    s->span_lo=m->span_lo;s->span_hi=m->kind;
    memcpy(&s->ratio,m->work,4);memcpy(f->cross,m->work+1,8);
    f->hull_word_1c=m->cell_word_1c;f->hull_word_20=m->cell_word_20;
    v->host.shared->move_last=0;return 0;
}
/* The inverse of import_move, for a move walker (0019AD00) called through
 * this view: the probe representation (the last writer, just committed)
 * becomes the move scratch, field for field, so every scratchpad field the
 * walker does not write keeps its last value as in the original. The lanes
 * both representations share at one address (0x700031CC, 0x7000368C) are
 * already canonical in the move scratch. */
static int export_move(EmArea01CollisionView *v,const EmCollMoveWorld *mw)
{
    EmCollMoveScratch *m=v->host.move;const EmCollProbeState *s=v->host.segment->state;
    const EmCollSegmentFaceScratch *f=v->host.segment->face;
    if(!mw->cells||mw->grid!=grid(v))return fail(v,0x0019AD00u,1);
    const void *record=NULL;
    if(s->record==EM_COLL_PROBE_RECORD_CELL)record=EM_COLL_MOVE_CELL_RECORD;
    else if(s->record==EM_COLL_PROBE_RECORD_GRID){
        if(s->node<0||(uint32_t)s->node>=grid(v)->count||!grid(v)->words)return fail(v,0x700031D0,3);
        record=grid(v)->words+12*(size_t)s->node;
    }else if(s->record!=EM_COLL_PROBE_RECORD_NONE)return fail(v,0x700031D0,3);
    memcpy(m->start,s->start,16);memcpy(m->end,s->end,16);
    memcpy(m->point,s->point,12);memcpy(&m->point[3],&v->host.shared->word_31BC,4);
    memcpy(m->delta,s->delta,12);
    m->record=record;m->entity=s->entity;m->mode=s->kind;
    m->cell_class=s->cell_class;memcpy(m->cell_normal,s->cell_normal,12);
    m->query_class=s->query_class;m->self=s->self;memcpy(m->rank,s->rank,12);
    m->span_lo=s->span_lo;m->kind=s->span_hi;
    memcpy(m->work,&s->ratio,4);memcpy(m->work+1,f->cross,8);
    m->cell_word_1c=f->hull_word_1c;m->cell_word_20=f->hull_word_20;
    return 0;
}
int em_area01_collision_view_bind(EmArea01CollisionView *v,const EmArea01CollisionHost *h)
{
    if(!v||!h)return -1;
    memset(v,0,sizeof *v);v->host=*h;
    if(h->file_rest){v->host.file.bytes=file_map(v,&v->host.file.size);if(!v->host.file.bytes)return fail(v,h->file.address,1);}
    h=&v->host;
    if(!h->shared||!h->segment||!h->segment->state||!h->segment->face||!h->segment->world||
       !h->segment->world->cells||!h->segment->world->grid||!h->move||(!h->pool&&(!h->address||!h->identity))||!h->generation||!h->bytes)
        return fail(v,0,1);
    /* The bind checks the file against the grid with the metadata the
     * original's 00199C50 will stage (a local copy); the view's own copy is
     * staged at that call (em_area01_collision_view_00199C50). */
    uint32_t meta[EM_SLG_COLL_SPAD_WORDS];
    if(!cells(v)||!cells(v)->bytes||!lists(v)||h->file.d28A5A8_value!=cells(v)->count||
       em_slg_00199C50(&h->file,meta)<0||meta[(0x320C-0x31F8)/4]!=grid(v)->count)
        return fail(v,0x700031F8,1);
    if(!h->file.d28A5A8||(uint64_t)h->file.address+h->file.size>UINT64_C(0x100000000)||
       (uint64_t)h->file.d28A5A8+cells(v)->size>0x02000000u)return fail(v,0x70003250,1);
    for(unsigned i=0;i<12;i++){
        uint32_t a=meta[(0x3210-0x31F8)/4+i];
        if(!a&&i>=6)continue;
        if(!span(a,2u*grid(v)->count,h->file.address,h->file.size,(void *)h->file.bytes))return fail(v,0x70003210u+4*i,1);
    }
    if(!span(meta[(0x3208-0x31F8)/4],64u*grid(v)->count,h->file.address,h->file.size,(void *)h->file.bytes)||
       !span(meta[1],12u*grid(v)->vert_count,h->file.address,h->file.size,(void *)h->file.bytes))return fail(v,0x700031FC,1);
    v->generation=h->generation(h->ctx);return 0;
}
int em_area01_collision_view_00199C50(EmArea01CollisionView *v)
{
    if(!v||v->fault)return -1;
    if(em_slg_00199C50(&v->host.file,v->metadata)<0||v->metadata[(0x320C-0x31F8)/4]!=grid(v)->count)
        return fail(v,0x700031F8,1);
    v->staged=1;return 0;
}
int em_area01_collision_view_begin(EmArea01CollisionView *v)
{
    if(!v||v->fault||v->active)return fail(v,0,1);
    if(v->host.generation(v->host.ctx)!=v->generation)return fail(v,0,2);
    if(import_move(v)<0)return -1;
    EmCollProbeState *s=v->host.segment->state;EmAimFireWorldLive *w=v->host.shared;
    if(record_word(v,s,&w->record_word)<0)return -1;
    w->entity_word=encode(v,s->entity);v->self_word=encode(v,s->self);
    if((s->entity&&!w->entity_word)||(s->self&&!v->self_word))return fail(v,0x700031D4,3);
    v->entity_before=s->entity;v->self_before=s->self;v->entity_word_before=w->entity_word;v->record_before=s->record;v->node_before=s->node;
    memcpy(w->point16,s->point,12);w->point16[3]=w->word_31BC;memcpy(v->point_before,w->point16,16);
    static const uint32_t base[]={0x28B020,0x28AFF0,0x28AF30,0x28AE30,0x28AC30,0x28AB30};
    const EmActorClassLists *ls=lists(v);
    static const int cap[]={0xC,0x30,0x40,0x80,0x40,0x20};
    for(unsigned k=0;k<EM_ACTOR_LIST_COUNT;k++){
        const EmActorClassList *l=&ls->list[k];
        if(l->live<0||l->live>cap[k]||l->published<0||l->published>cap[k])return fail(v,base[k],3);
        v->cursor[k][0]=base[k]-4u*(uint32_t)l->published;v->cursor[k][1]=base[k]-4u*(uint32_t)l->live;
        int count=l->live>l->published?l->live:l->published;
        for(int j=0;j<count;j++){
            uint32_t a=encode(v,l->slot[j]);if(!a)return fail(v,base[k]-4u*(unsigned)(j+1),3);
            v->list[k][EM_ACTOR_LIST_MAX-1-j]=a;
        }
    }
    if(v->host.pool)for(unsigned i=0;i<EM_ACTOR_POOL_CAPACITY;i++){
        v->pool_generation[i]=v->host.pool->records[i].generation;
        v->pool_allocated[i]=v->host.pool->records[i].allocated;
    }
    v->active=1;return 0;
}
/* The delivered area file outside the cell directory is immutable (the
 * view never hands it out for writing), so its bytes stay readable while
 * the native close-out owns the collision state between transactions:
 * 001A7870 reads a listed owner's +0x58 hull record in that file. */
static uint8_t *file_bytes(EmArea01CollisionView *v,uint32_t a,uint32_t n)
{
    uint32_t size=0;const uint8_t *file=file_map(v,&size);
    if(!n||(uint64_t)a+n>UINT64_C(0x100000000)||!file)return NULL;
    if(v->host.generation(v->host.ctx)!=v->generation)return NULL;
    const EmActorCellTable *t=cells(v);
    uint32_t alias=a>=0x20000000u&&a<0x22000000u ? a-0x20000000u : a;
    if(!t||((uint64_t)alias+n>v->host.file.d28A5A8&&(uint64_t)v->host.file.d28A5A8+t->size>alias))return NULL;
    return span(a,n,v->host.file.address,size,(void *)file);
}
uint8_t *em_area01_collision_view_bytes(EmArea01CollisionView *v,uint32_t a,uint32_t n,int write)
{
    if(!v||v->fault)return NULL;
    if(!v->active)return write ? NULL : file_bytes(v,a,n);
    if(!n||(uint64_t)a+n>UINT64_C(0x100000000)){fail(v,a,1);return NULL;}
    if(v->host.generation(v->host.ctx)!=v->generation){fail(v,a,2);return NULL;}
    EmCollProbeState *s=v->host.segment->state;EmCollSegmentFaceScratch *f=v->host.segment->face;
    uint8_t *p;
    /* The shove vector as the quadword 001028B8 reads (AREA04's creature,
     * 001B3440): its xyz and the word beside it, composed for this read. */
    if(a==0x700031C0u&&n==16u&&!write) {
        memcpy(v->delta16,s->delta,12);memcpy(&v->delta16[3],&v->host.move->delta[3],4);
        return (uint8_t *)v->delta16;
    }
#define FIELD(at,field) do{p=span(a,n,at,sizeof(field),&(field));if(p)return p;}while(0)
    FIELD(0x70003190u,s->start);FIELD(0x700031A0u,s->end);
    FIELD(0x700031B0u,v->host.shared->point16);FIELD(0x700031C0u,s->delta);
    FIELD(0x700031CCu,v->host.move->delta[3]);
    FIELD(0x700031D0u,v->host.shared->record_word);FIELD(0x700031D4u,v->host.shared->entity_word);
    FIELD(0x700031D8u,s->kind);FIELD(0x70003254u,v->self_word);
    FIELD(0x700030CAu,s->cell_class);FIELD(0x700030CCu,f->hull_word_1c);FIELD(0x700030D0u,f->hull_word_20);
    FIELD(0x700030D4u,s->cell_normal);FIELD(0x70003240u,s->rank);FIELD(0x7000324Eu,s->query_class);
    FIELD(0x70003B86u,s->span_lo);FIELD(0x70003B88u,s->span_hi);FIELD(0x70003680u,s->ratio);
    /* The existing four QWs are contiguous canonical storage. CD520 asks
     * for the first two together; pose/beam may borrow all four. */
    p=span(a,n,0x70003600u,64,f->box_min);if(p)return p;
    FIELD(0x70003684u,f->cross);FIELD(0x7000368Cu,v->host.move->work[3]);
#undef FIELD
    EmActorCellTable *t=cells(v);uint32_t alias=a;
    if(a>=0x20000000u&&a<0x22000000u)alias=a-0x20000000u;
    p=span(alias,n,v->host.file.d28A5A8,t->size,t->bytes);if(p)return p;
    /* A containing loader delivery can include the directory's old input
     * bytes. Never expose those across the canonical mutable cell boundary. */
    if((uint64_t)alias+n>v->host.file.d28A5A8&&(uint64_t)v->host.file.d28A5A8+t->size>alias)return NULL;
    if(write)return NULL;
    if(!v->staged&&(uint64_t)a+n>0x700031F8u&&a<0x70003240u)return NULL;   /* before 00199C50 */
    p=span(a,n,0x700031F8,0x48,v->metadata);if(p)return p;
    p=span(a,n,0x7000324C,2,&t->count);if(p)return p;
    p=span(a,n,0x70003250,4,&v->host.file.d28A5A8);if(p)return p;
    {uint32_t size=0;const uint8_t *file=file_map(v,&size);
     p=span(a,n,v->host.file.address,size,(void *)file);if(p)return p;}
    const EmActorClassLists *ls=lists(v);
    static const uint32_t base[]={0x28B020,0x28AFF0,0x28AF30,0x28AE30,0x28AC30,0x28AB30};
    static const uint32_t vars[]={0x275BAC,0x275B9C,0x275B8C,0x275B7C,0x275B6C,0x275B5C};
    for(unsigned k=0;k<EM_ACTOR_LIST_COUNT;k++){
        const EmActorClassList *l=&ls->list[k];int count=l->live>l->published?l->live:l->published;
        p=span(a,n,vars[k],8,v->cursor[k]);if(p)return p;
        p=span(a,n,vars[k]+8,2,(void *)&l->published);if(p)return p;
        p=span(a,n,vars[k]+12,2,(void *)&l->live);if(p)return p;
        p=span(a,n,base[k]-4u*(unsigned)count,4u*(unsigned)count,v->list[k]+EM_ACTOR_LIST_MAX-count);if(p)return p;
    }
    return NULL;
}
int em_area01_collision_view_commit(EmArea01CollisionView *v)
{
    if(!v||v->fault||!v->active)return fail(v,0,1);
    EmCollProbeState *s=v->host.segment->state;EmAimFireWorldLive *w=v->host.shared;
    if(v->host.generation(v->host.ctx)!=v->generation||w->move_last||s->entity!=v->entity_before||s->self!=v->self_before||
       s->record!=v->record_before||s->node!=v->node_before||memcmp(s->point,v->point_before,12)||w->word_31BC!=v->point_before[3])
        return fail(v,0x700031B0,2);
    if(v->host.pool)for(unsigned i=0;i<EM_ACTOR_POOL_CAPACITY;i++)
        if(v->pool_generation[i]!=v->host.pool->records[i].generation||v->pool_allocated[i]!=v->host.pool->records[i].allocated)
            return fail(v,EM_ACTOR_POOL_BASE+i*EM_ACTOR_RECORD_SIZE,2);
    /* An unchanged word keeps the representation it had (another
     * composition's proxy of the same record). */
    const void *entity=w->entity_word==v->entity_word_before ? v->entity_before : decode(v,w->entity_word);
    const void *self=decode(v,v->self_word);
    if((w->entity_word&&(!entity||encode(v,entity)!=w->entity_word))||(v->self_word&&(!self||encode(v,self)!=v->self_word)))
        return fail(v,0x700031D4,3);
    int record=EM_COLL_PROBE_RECORD_NONE,node=-1;
    if(w->record_word==0x700030B0)record=EM_COLL_PROBE_RECORD_CELL;
    else if(w->record_word){node=node_index(v,w->record_word);if(node<0)return fail(v,0x700031D0,3);record=EM_COLL_PROBE_RECORD_GRID;}
    memcpy(s->point,w->point16,12);w->word_31BC=w->point16[3];
    s->entity=entity;s->self=self;s->record=record;s->node=node;
    v->active=0;return 0;
}
int em_area01_collision_view_owns(const EmArea01CollisionView *v,uint32_t a,uint32_t n)
{
    if(!v||!n||!v->host.segment||!v->host.segment->world||!v->host.segment->world->cells||!cells(v))return 0;
    static const uint32_t spans[][2]={{0x700030CA,0x16},{0x70003190,0x4C},
        {0x700031F8,0x60},{0x70003600,64},
        {0x70003680,16},{0x70003B86,4},{0x275B5C,0x5E},{0x28AAB0,0x570}};
    for(unsigned i=0;i<sizeof spans/sizeof spans[0];i++)
        if((uint64_t)a+n>spans[i][0]&&(uint64_t)spans[i][0]+spans[i][1]>a)return 1;
    uint32_t bases[]={v->host.file.address,v->host.file.d28A5A8,v->host.file.d28A5A8+0x20000000u};
    uint32_t file_size=0;(void)file_map(v,&file_size);
    uint32_t sizes[]={file_size,cells(v)->size,cells(v)->size};
    for(unsigned i=0;i<3;i++)if((uint64_t)a+n>bases[i]&&(uint64_t)bases[i]+sizes[i]>a)return 1;
    return 0;
}
static int get(EmArea01CollisionView *v,uint32_t a,void *out,uint32_t n)
{
    uint8_t *p=em_area01_collision_view_bytes(v,a,n,0);
    if(!p&&!em_area01_collision_view_owns(v,a,n))p=v->host.bytes(v->host.ctx,a,n,0);
    if(!p)return fail(v,a,1);memcpy(out,p,n);return 0;
}
/* 0019AA80's hull world: the player's call-local record and chain. */
typedef struct { EmActor actor; EmArea01CollisionView *v; } PlayerHull;
static int player_hull_chain(void *ctx,const EmActor *body,EmCollHullChain *out)
{
    PlayerHull *ph=ctx;
    if(body!=&ph->actor||!out)return -1;
    return ph->v->host.player_chain(ph->v->host.ctx,&ph->actor,out);
}
int em_area01_collision_view_call(EmArea01CollisionView *v,EmArea01Call *c)
{
    if(!v||!c||v->fault||v->active)return fail(v,c?c->function:0,1);
    /* Gather operands while the serialized pointers are valid; never keep an
     * operand pointer across the following native mutation. */
    if(em_area01_collision_view_begin(v)<0)return -1;
    float a[3],b[3],feet=0;const uint8_t *prim=NULL;int node=-1,r=-1;
    EmActorCollisionQuery query={0};uint8_t *feet_out=NULL;
    EmCollMoveActor mover;memset(&mover,0,sizeof mover);uint8_t *mover_x=NULL,*mover_z=NULL;
    switch(c->function){
    case 0x19A570:case 0x19A910:case 0x19B6C0:
        if(c->na<(c->function==0x19A570?4u:c->function==0x19A910?3u:2u)||get(v,(uint32_t)c->a[0],a,12)<0||get(v,(uint32_t)c->a[1],b,12)<0)return -1;
        break;
    case 0x19AB20:{
        uint32_t self;
        if(c->na<4||get(v,(uint32_t)c->a[1],a,12)<0||get(v,(uint32_t)c->a[2],b,12)<0||
           get(v,(uint32_t)c->a[0]+2,&query.cls,1)<0||get(v,(uint32_t)c->a[0]+0x14,&self,4)<0)return -1;
        query.self=decode(v,self);
        if(self&&(!query.self||encode(v,query.self)!=self))return fail(v,self,3);
        if(c->a[3]&0x80000000u){
            feet_out=v->host.bytes(v->host.ctx,(uint32_t)c->a[0]+0xB4,4,1);
            if(!feet_out)return fail(v,(uint32_t)c->a[0]+0xB4,1);
            memcpy(&feet,feet_out,4);query.feet_y=&feet;
        }break;}
    case 0x19AD00:{
        /* 0019AD00(actor, target, flags): the actor's +0x00, +0x02, +0x52,
         * +0x14 (its self word) and +0xB0..+0xB8, the target's x, y, z. */
        uint32_t self,at=(uint32_t)c->a[0];
        if(c->na<3||get(v,at,&mover.status,1)<0||get(v,at+2,&mover.cls,1)<0||get(v,at+0x52,&mover.h52,2)<0||
           get(v,at+0x14,&self,4)<0||get(v,at+0xB0,mover.position,12)<0||get(v,(uint32_t)c->a[1],b,12)<0)return -1;
        mover.self=decode(v,self);
        if(self&&(!mover.self||encode(v,mover.self)!=self))return fail(v,self,3);
        if(c->a[2]&0x80000000u){
            mover_x=v->host.bytes(v->host.ctx,at+0xB0,4,1);mover_z=v->host.bytes(v->host.ctx,at+0xB8,4,1);
            if(!mover_x||!mover_z)return fail(v,at+0xB0,1);
        }break;}
    case 0x19AA80:
        /* Sources inside the destination words would make the original's
         * interleaved copy order matter: refused (as em_aim_fire_world_live). */
        if(c->na<3)return -1;
        for(int k=0;k<2;k++){uint32_t at=(uint32_t)c->a[k];if(at<0x700031B0u&&at+12u>0x70003190u)return fail(v,at,3);}
        if(get(v,(uint32_t)c->a[0],a,12)<0||get(v,(uint32_t)c->a[1],b,12)<0)return -1;break;
    case 0x19F1A0:
        if(c->na<2||get(v,(uint32_t)c->a[0],a,12)<0)return -1;break;
    case 0x19ED80:
        if(c->na<2||c->a[0]!=0x70003190u||(node=node_index(v,(uint32_t)c->a[1]))<0)return fail(v,(uint32_t)c->a[1],3);break;
    case 0x1A4030:case 0x1A50A0:case 0x1A5C30:{
        uint32_t address=(uint32_t)c->a[0];if(address>=0x20000000u&&address<0x22000000u)address-=0x20000000u;
        if(c->na<1||address<v->host.file.d28A5A8||!(prim=em_actor_cells_prim(cells(v),address-v->host.file.d28A5A8)))return fail(v,address,3);break;}
    default:return fail(v,c->function,4);
    }
    if(em_area01_collision_view_commit(v)<0)return -1;
    const EmCollSegment *seg=v->host.segment;EmCollProbeState *s=seg->state;
    switch(c->function){
    case 0x19A570:r=em_coll_segment_0019A570(seg,a,b,(uint32_t)c->a[2],(int32_t)c->a[3]);break;
    case 0x19A910:r=em_coll_segment_0019A910(seg,a,b,(uint32_t)c->a[2]);break;
    case 0x19B6C0:{EmCollProbeWorkers w={(void *)seg,em_coll_segment_face_worker,em_coll_segment_round_worker};r=em_coll_probe_0019B6C0(seg->world,&w,s,a,b);break;}
    case 0x19AB20:{EmActorCollisionHit hit;r=em_actor_collision_ground_state_0019AB20(seg->world->cells,&query,a,b,(uint32_t)c->a[3],&hit,s);if(r>=0&&feet_out)memcpy(feet_out,&feet,4);break;}
    case 0x19AD00:{
        /* The move walkers' world is the segment's own (em_collision_world
         * builds both from one directory, grid, hull world and SDK math). */
        /* A non-class-0 mover with bit 0 (AREA04's creature 0012E3A0) locks
         * against the player's hull (0019AD00's 001A7280): the call-local
         * record and chain of 0019AA80 below; the scratch keeps the view's
         * 0x8102B0 identity of a hit. */
        PlayerHull ph;memset(&ph,0,sizeof ph);ph.v=v;
        const EmCollHullWorld hw={&ph,player_hull_chain,&ph.actor};
        const EmCollHullWorld *hulls=seg->hulls;const void *player=NULL;
        if((c->a[2]&1u)&&(mover.status&1)&&(mover.cls&0x1F)) {
            player=decode(v,0x008102B0u);
            if(!player||!v->host.player_chain||v->host.player_chain(v->host.ctx,&ph.actor,NULL)<0)
                return fail(v,0x1A7280,4);
            hulls=&hw;
        }
        const EmCollMoveWorld mw={seg->world->cells,seg->world->grid,hulls,seg->math};
        if(export_move(v,&mw)<0)return -1;
        r=em_coll_move_0019AD00(&mw,v->host.move,&mover,b,(uint32_t)c->a[2]);
        if(r<0)break;
        if(player&&v->host.move->entity==(void *)&ph.actor)v->host.move->entity=(void *)player;
        /* The move scratch is now the last writer (the next begin imports
         * it); bit 31 added the hit delta to the actor's x and z. */
        v->host.shared->move_last=1;
        if(mover_x){memcpy(mover_x,&mover.position[0],4);memcpy(mover_z,&mover.position[2],4);}
        break;}
    case 0x19AA80:{
        /* 0019AA80 (em_coll_segment_0019AA80): the AREA01 revisit's
         * 0x8282F0 probe. The lock reads the player through a call-local
         * record (the host's player_chain); a lock stores the view's
         * 0x8102B0 identity. */
        PlayerHull ph;memset(&ph,0,sizeof ph);ph.v=v;
        const void *player=decode(v,0x008102B0u);
        if(!player||!v->host.player_chain||v->host.player_chain(v->host.ctx,&ph.actor,NULL)<0)return fail(v,0x1A7280,4);
        const EmCollHullWorld hw={&ph,player_hull_chain,&ph.actor};
        r=em_coll_segment_0019AA80(s,seg->face,&hw,a,b,(uint32_t)c->a[2],player);
        break;}
    case 0x19F1A0:r=em_coll_probe_0019F1A0(grid(v),s,a,(uint32_t)c->a[1]);break;
    case 0x19ED80:r=em_coll_probe_0019ED80(grid(v),s,node);break;
    case 0x1A4030:r=em_coll_probe_001A4030(prim,s);break;
    case 0x1A50A0:r=em_coll_segment_001A50A0(prim,s,seg->face);break;
    case 0x1A5C30:r=em_coll_segment_001A5C30(seg->math,prim,s);break;
    }
    if(r<0)return fail(v,c->function,4);
    c->v0=(uint64_t)(int64_t)r;return 0;
}
