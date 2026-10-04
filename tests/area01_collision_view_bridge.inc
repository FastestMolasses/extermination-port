/* Included after the existing segment oracle's Bridge: test fixture only. */
#include "game/em_area01_collision_view.h"
static struct {
    Bridge *b;EmCollProbeState state;EmCollSegmentFaceScratch face;
    EmCollMoveScratch move;EmAimFireWorldLive shared;EmArea01CollisionView view;
    uint64_t generation;
} CV;
static uint32_t cv_address(void *ctx,const void *p){return addr_of(ctx,p);}
static const void *cv_identity(void *ctx,uint32_t a)
{ Bridge *b=ctx;int i=find(b,a);return i>=0?&b->rec[i]:a==b->stranger_addr?&b->stranger:NULL; }
static uint64_t cv_generation(void *ctx){(void)ctx;return CV.generation;}
static uint8_t cv_stack[0x100000];
static uint8_t *cv_fallback(void *ctx,uint32_t a,uint32_t n,int write)
{ Bridge *b=ctx;(void)write;if(a>=0x7F000000u&&n&&a-0x7F000000u<=sizeof cv_stack&&n<=sizeof cv_stack-(a-0x7F000000u))return cv_stack+(a-0x7F000000u);return a<=b->ram.size&&n<=b->ram.size-a?(uint8_t *)b->ram.ram+a:NULL; }
int cv_setup(Bridge *b,BState *st,uint32_t file,uint32_t file_size,uint32_t table,uint32_t fourth)
{
    memset(&CV,0,sizeof CV);CV.b=b;CV.generation=1;
    to_native(b,st,&CV.state,&CV.face);b->seg.state=&CV.state;b->seg.face=&CV.face;
    b->cells.ranks=&b->grid;b->cells.grid=&b->emcl;CV.shared.word_31BC=fourth;
    EmArea01CollisionHost h={.shared=&CV.shared,.segment=&b->seg,.move=&CV.move,
        .file={.address=file,.bytes=b->ram.ram+file,.size=file_size,.d28A5A8=table,.d28A5A8_value=b->table.count},
        .ctx=b,.address=cv_address,.identity=cv_identity,.generation=cv_generation,.bytes=cv_fallback};
    return em_area01_collision_view_bind(&CV.view,&h);
}
int cv_begin(void){return em_area01_collision_view_begin(&CV.view);}
int cv_commit(void){return em_area01_collision_view_commit(&CV.view);}
uint8_t *cv_bytes(uint32_t a,uint32_t n,int w){return em_area01_collision_view_bytes(&CV.view,a,n,w);}
int cv_owns(uint32_t a,uint32_t n){return em_area01_collision_view_owns(&CV.view,a,n);}
void cv_state(BState *st){from_native(CV.b,&CV.state,&CV.face,st);}
int cv_fault(void){return CV.view.fault;}
uint32_t cv_fault_address(void){return CV.view.fault_address;}
void cv_invalidate(void){++CV.generation;}
void cv_native_point(void){CV.state.point[0]=123;}
uint32_t cv_fourth(void){return CV.shared.word_31BC;}
void cv_quad_w_seed(const uint32_t words[4])
{ memcpy(CV.face.box_min+3,words,4);memcpy(CV.face.box_max+3,words+1,4);
  memcpy(CV.face.delta+3,words+2,4);memcpy(CV.face.rel+3,words+3,4); }
uint32_t cv_quad_w(unsigned i)
{ const float *v[]={CV.face.box_min,CV.face.box_max,CV.face.delta,CV.face.rel};uint32_t w=0;
  if(i<4)memcpy(&w,v[i]+3,4);return w; }
uint8_t *cv_cells(void){return CV.b->table.bytes;}
int cv_call(uint32_t fn,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t *out)
{
    EmArea01Call call={.function=fn,.a={a,b,c,d},.na=4};
    int r=em_area01_collision_view_call(&CV.view,&call);*out=(uint32_t)call.v0;return r;
}
void cv_move_seed(void)
{
    EmCollMoveScratch *m=&CV.move;EmCollProbeState *s=&CV.state;
    memcpy(m->start,s->start,16);memcpy(m->end,s->end,16);memcpy(m->point,s->point,12);memcpy(m->delta,s->delta,12);
    m->record=s->record==1?EM_COLL_MOVE_CELL_RECORD:s->record==2?em_coll_grid_hull_node_record(&CV.b->grid,s->node):NULL;
    m->entity=s->entity;m->mode=s->kind;m->cell_class=s->cell_class;memcpy(m->cell_normal,s->cell_normal,12);
    m->query_class=s->query_class;m->self=s->self;memcpy(m->rank,s->rank,12);m->span_lo=s->span_lo;m->kind=s->span_hi;
    memcpy(m->work,&s->ratio,4);memcpy(m->work+1,CV.face.cross,8);
    m->cell_word_1c=CV.face.hull_word_1c;m->cell_word_20=CV.face.hull_word_20;
    memset(s,0xA5,sizeof *s);CV.shared.move_last=1;
}

int cv_input(uint32_t a,const uint8_t *p,uint32_t n) {uint8_t *q=cv_fallback(CV.b,a,n,1);if(!q)return -1;memcpy(q,p,n);return 0;}
#include "game/em_aim_fire_sdk_memory.h"
static uint8_t *cv_memory(void *ctx,uint32_t a,uint32_t n,int write)
{
    (void)ctx;if(em_area01_collision_view_owns(&CV.view,a,n))return cv_bytes(a,n,write);
    return cv_fallback(CV.b,a,n,write);
}
static void *cv_sdk_memory(void *ctx,uint32_t a,size_t n,int write)
{ return n<=UINT32_MAX?cv_memory(ctx,a,(uint32_t)n,write):NULL; }
static int cv_worker(void *ctx,EmArea01SysCall *c)
{
    (void)ctx;EmAimFireTargetCall sdk={.function=c->fn,.sp=c->sp,.na=c->na,.nf=c->nf};
    memcpy(sdk.a,c->a,c->na*sizeof c->a[0]);memcpy(sdk.f,c->f,c->nf*sizeof c->f[0]);
    int r=em_aim_fire_sdk_memory_call(NULL,cv_sdk_memory,&sdk);
    if(r!=1){c->v0=sdk.v0;c->f0=sdk.f0;return r;}
    if(cv_commit()<0)return -1;
    EmArea01Call call={.function=c->fn,.sp=c->sp,.na=c->na,.nf=c->nf};
    memcpy(call.a,c->a,c->na*sizeof c->a[0]);memcpy(call.f,c->f,c->nf*sizeof c->f[0]);
    r=em_area01_collision_view_call(&CV.view,&call);c->v0=call.v0;c->f0=call.f0;
    if(r<0)return r;
    return cv_begin();
}
int cv_sys(uint32_t fn,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t *out)
{
    (void)fn;if(cv_begin()<0)return -1;
    EmArea01Sys s={.call=cv_worker,.sp=0x7F080000,.view=cv_memory};int32_t result;
    int r=em_area01_sys_0019B4C0(&s,a,b,c,(int32_t)d,&result);
    if(r<0){CV.view.fault=s.fault;CV.view.fault_address=s.fault_address;return -1;}
    *out=(uint32_t)result;return cv_commit();
}
/* Exercise the default native identities and exact free/reuse guard. */
int cv_pool_contract(void)
{
    static EmActorPool pool;static EmPlayerLiveActor player;
    EmSceneState scene={0};memset(&pool,0,sizeof pool);em_actor_pool_reset_001AF8E0(&pool);
    if(em_area01_collision_pool_address(&pool,&player,&player)!=0x8102B0||
       em_area01_collision_pool_identity(&pool,&player,0x8102B0)!=&player)return -1;
    for(unsigned i=0;i<EM_ACTOR_POOL_CAPACITY;i++){
        uint32_t a=EM_ACTOR_POOL_BASE+i*EM_ACTOR_RECORD_SIZE;
        if(em_area01_collision_pool_address(&pool,&player,&pool.records[i])!=a||
           em_area01_collision_pool_identity(&pool,&player,a)!=&pool.records[i]||
           em_area01_collision_pool_identity(&pool,&player,a+1))return -2;
    }
    int stranger=0;
    if(em_area01_collision_pool_address(&pool,&player,&stranger)||
       em_area01_collision_pool_address(&pool,&player,NULL)||
       em_area01_collision_pool_identity(&pool,&player,EM_ACTOR_POOL_BASE-1))return -3;
    EmArea01CollisionHost host=CV.view.host;host.pool=&pool;
    if(em_area01_collision_view_bind(&CV.view,&host)<0||cv_begin()<0)return -4;
    EmActor *a=em_actor_pool_alloc_001AFA90(&pool,&scene,4);
    if(!a||cv_commit()!=-1||CV.view.fault!=2)return -5;
    if(em_area01_collision_view_bind(&CV.view,&host)<0||cv_begin()<0)return -6;
    if(em_actor_pool_free_001AFC10(&pool,&scene,a)<0||!em_actor_pool_alloc_001AFA90(&pool,&scene,4)||
       cv_commit()!=-1||CV.view.fault!=2)return -7;
    if(em_area01_collision_view_bind(&CV.view,&host)<0||cv_begin()<0||cv_commit()<0)return -8;
    return 0;
}
void cv_reset_lists(void){em_actor_class_lists_reset(&CV.b->lists);}
