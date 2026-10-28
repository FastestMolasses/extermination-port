/* Binding checks, not a replacement for each reused owner's instruction oracle. */
#include "game/em_aim_fire_world_live.h"
#include "game/em_coll_grid_hull.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* em_effects_live's ELF-source 001D04B0 (the spat hazard's draw) is outside
 * this fixture: reaching it is a refusal. */
int em_effects_live_001D04B0_elf(const float m[16], int32_t kind, uint32_t source, uint32_t f12, uint32_t f13);
uint32_t em_effects_live_fault(void);
int em_effects_live_001D04B0_elf(const float m[16], int32_t kind, uint32_t source, uint32_t f12, uint32_t f13)
{ (void)m; (void)kind; (void)source; (void)f12; (void)f13; return -1; }
uint32_t em_effects_live_fault(void) { return 0x001D04B0u; }
/* The player's hull lock (0019AA80, em_coll_segment_0019AA80 over
 * em_coll_grid_hull's 001A7280) is outside this fixture too; it is
 * compared with the original by test_area01_collision_view_reference.py. */
int em_coll_segment_0019AA80(EmCollProbeState *state, EmCollSegmentFaceScratch *face,
                             const EmCollHullWorld *player, const float from[3], const float to[3],
                             uint32_t mask, const void *hit_entity)
{ (void)state; (void)face; (void)player; (void)from; (void)to; (void)mask; (void)hit_entity; return -1; }

static EmAimFireWorldLive world;
static EmCollProbeState probe;
static EmCollSegmentFaceScratch face;
static EmCollSegment seg;
static EmActorClassLists lists;
static EmCollisionWorldOwners owner;
static EmActor actors[3];
static uint8_t victim[0x320],node[0x320],point[16],direction[16];
static EmPoseRegion regions[4];
static uint8_t target_slots[12],target_matrix[0xD0];
static unsigned target_queries;
static int target_regions(void *ctx,uint32_t node,EmPoseRegion *out,unsigned capacity,unsigned *count)
{
    (void)ctx;assert(node==0x5000);++target_queries;
    if(capacity<2)return -1;
    out[0]=(EmPoseRegion){node+0x110,sizeof target_slots,target_slots,0};
    out[1]=(EmPoseRegion){0xA000,sizeof target_matrix,target_matrix,0};
    *count=2;return 0;
}
static int allocated,fail_call,calls,checks,parent_valid,parent_notifications;
#define CHECK(x) do { assert(x); ++checks; } while(0)
const EmCollSegment *em_collision_world_segment(void) { return &seg; }
const EmActorClassLists *em_collision_world_lists(void) { return &lists; }
const EmCollisionWorldOwners *em_collision_world_owners(void) { return &owner; }
/* The move walkers' world and scratch (0019B2C0's). */
const unsigned char em_coll_move_cell_record_tag;
static EmCollMoveWorld move_world;
static EmCollMoveScratch move;
static int move_calls;
const EmCollMoveWorld *em_collision_world_move(void) { return &move_world; }
EmCollMoveScratch *em_collision_world_move_scratch(void) { return &move; }
int em_coll_grid_hull_node_index(const EmCollProbeGrid *g,const void *record) { (void)g;(void)record;return -1; }
int em_coll_move_probe_0019B2C0(const EmCollMoveWorld *w,EmCollMoveScratch *s,float a0[3],const float a1[3],uint32_t flags)
{
    ++move_calls;CHECK(w==&move_world&&s==&move);CHECK(a0[0]==1&&a1[2]==6);
    s->point[0]=9;s->record=EM_COLL_MOVE_CELL_RECORD;s->cell_class=0x2001;
    if(flags&0x80000000u)a0[0]=8;
    return flags==7?-1:2;
}
int em_coll_segment_face_worker(void *c,const uint8_t *p,EmCollProbeState *s,int *h)
{ (void)c;(void)p;(void)s;(void)h;return -1; }
int em_coll_segment_round_worker(void *c,const uint8_t *p,EmCollProbeState *s,int *h)
{ (void)c;(void)p;(void)s;(void)h;return -1; }
int em_coll_segment_0019A570(const EmCollSegment *s,const float *a,const float *b,unsigned mask,int id)
{ CHECK(s==&seg);CHECK(a[0]==1&&b[2]==6);CHECK(mask==7&&id==32);s->state->point[1]=17;return 2; }
int em_coll_probe_0019B6C0(const EmCollProbeWorld *w,const EmCollProbeWorkers *workers,
                         EmCollProbeState *s,const float *a,const float *b)
{ (void)w;CHECK(workers&&workers->face_segment);CHECK(a[0]==1&&b[2]==6);s->record=1;return 2; }
static uint32_t address_of(void *ctx,const EmActor *a)
{ (void)ctx;return a>=actors&&a<actors+3?0x5000u+0x400u*(uint32_t)(a-actors):0; }
static int enumerate(void *ctx,EmPoseRegion *out,unsigned capacity,unsigned *count)
{
    (void)ctx;*count=allocated?(parent_valid?3:2):0;if(*count>capacity)return -1;
    if(*count){out[0]=(EmPoseRegion){0x9000,0x24,node,1};out[1]=(EmPoseRegion){0x9028,sizeof node-0x28,node+0x28,1};}
    if(*count==3)out[2]=(EmPoseRegion){0x9024,4,node+0x24,1};return 0;
}
static void *node_map(void *ctx,uint32_t a,size_t n,int write)
{
    (void)ctx;if(!allocated||a<0x9000||(uint64_t)a+n>0x9000+sizeof node)return NULL;
    if(a<0x9028&&(uint64_t)a+n>0x9024&&!write&&!parent_valid)return NULL;
    return node+(a-0x9000);
}
static int written(void *ctx,uint32_t a,size_t n)
{ (void)ctx;CHECK(a==0x9024&&n==4);uint32_t value;memcpy(&value,node+0x24,4);CHECK(value==0x5000);parent_valid=1;++parent_notifications;return 0; }
void *em_aim_fire_live_map(void *ctx,uint32_t a,size_t n,int write)
{
    EmAimFireLive *h=ctx;
    if(h->fault_address||h->fault_function)return NULL;
    if(a>=0x7F001000u&&(uint64_t)a+n<=0x7F001000u+sizeof h->temporary)return h->temporary+(a-0x7F001000u);
    for(unsigned i=0;i<h->region_count;++i){const EmPoseRegion *r=&h->regions[i];
        if(a>=r->address&&(uint64_t)a+n<=(uint64_t)r->address+r->size&&(!write||r->writable))return r->bytes+(a-r->address);}
    void *p=em_aim_fire_world_live_map(&world,a,n,write);
    if(!p)h->fault_address=a;
    return p;
}
/* 001AA840's 001AA7A0 callee (a test worker inside the composition call):
 * probes the running call's owner memory for the freed class-4 record at
 * 0x5800 (actors[2]) and re-enters the composition once. */
static int reach_probe,reach_calls,sdk_vectors,sdk_calls;
static uint8_t victim2[0x320];
static void reach(EmAimFireLive *h,EmAimFireTargetCall *c);
int em_aim_fire_live_call(EmAimFireLive *h,EmAimFireTargetCall *c)
{
    ++calls;
    if(fail_call){h->fault_function=c->function;return -1;}
    if(c->function==0x1AFA90){CHECK(c->na==1&&c->a[0]==12);allocated=1;c->v0=0x9000;return 0;}
    if(c->function==0x1031E0 || c->function==0x102948){
        CHECK(c->na==2);
        CHECK(victim[0]==3 || c->function==0x102948);
        const size_t n=c->function==0x1031E0?12:16;
        void *to=em_aim_fire_live_map(h,(uint32_t)c->a[0],n,1);
        const void *from=em_aim_fire_live_map(h,(uint32_t)c->a[1],n,0);
        if(!to||!from)return -1;
        memcpy(to,from,n);return 0;
    }
    if(c->function==0xEF9D0 || c->function==0x1EF9D0){allocated=1;c->v0=0x9000;return 0;}
    if(sdk_vectors && (c->function==0x1028D0 || c->function==0x102760)){
        /* test leaves: 001028D0 out = a - b (4 lanes), 00102760 a copy */
        ++sdk_calls;--calls;
        float *o=em_aim_fire_live_map(h,(uint32_t)c->a[0],16,1),x[4],y[4];
        const float *p=em_aim_fire_live_map(h,(uint32_t)c->a[1],16,0);
        const float *q=c->function==0x1028D0?em_aim_fire_live_map(h,(uint32_t)c->a[2],16,0):NULL;
        if(!o||!p||(c->function==0x1028D0&&!q))return -1;
        memcpy(x,p,16);if(q)memcpy(y,q,16);
        for(int i=0;i<4;++i)o[i]=q?x[i]-y[i]:x[i];
        return 0;
    }
    if(c->function==0x1AA7A0 && reach_probe){reach(h,c);c->v0=0;return 0;}
    h->fault_function=c->function;return -1;
}
static uint32_t word(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
static void freed_header_probes(void)
{
    static const uint8_t zero[16];
    const uint8_t *p=em_aim_fire_world_live_owner_view(0x5800,16,0);
    CHECK(p==(const uint8_t *)&actors[2].status);CHECK(!memcmp(p,zero,16)); /* the cleared header */
    CHECK(em_aim_fire_world_live_owner_view(0x580F,1,0)==p+15);
    CHECK(!em_aim_fire_world_live_owner_view(0x5810,1,0));                 /* +0x10 is not exposed */
    CHECK(!em_aim_fire_world_live_owner_view(0x5810,4,0));
    CHECK(!em_aim_fire_world_live_owner_view(0x580E,4,0));                 /* reaching past it */
    CHECK(!em_aim_fire_world_live_owner_view(0x57FC,8,0));                 /* reaching before it */
    for(uint32_t k=0;k<16;++k)CHECK(!em_aim_fire_world_live_owner_view(0x5800+k,1,1)); /* read only */
    CHECK(!em_aim_fire_world_live_owner_view(0x5800,16,1));
}
static void reach(EmAimFireLive *h,EmAimFireTargetCall *c)
{
    ++reach_calls;CHECK(c->na>=2&&c->a[1]==0x5000);
    if(reach_calls!=1)return;
    freed_header_probes();
    CHECK(em_aim_fire_world_live_owner_view(0x5002,1,0)==victim+2);
    /* A nested composition call with other views for 0x5000 (an
     * all-zero record, so its own walk calls nothing): afterwards this
     * call's views are its own again. */
    static EmAimFireLive inner;memset(&inner,0,sizeof inner);
    static EmPoseRegion inner_regions[1];
    inner_regions[0]=(EmPoseRegion){0x5000,sizeof victim2,victim2,1};
    inner.regions=inner_regions;inner.region_count=1;
    EmAimFireTargetCall n={.function=0x1AA840,.sp=0x7F001400};
    CHECK(em_aim_fire_world_live_call(&world,&inner,&n)==0);CHECK(reach_calls==1);
    CHECK(em_aim_fire_world_live_owner_view(0x5002,1,0)==victim+2);
    freed_header_probes();
    (void)h;
}
static void put(void *p,uint32_t v){memcpy(p,&v,4);}
static void reset(EmAimFireLive *h)
{
    memset(h,0,sizeof *h);memset(&world,0,sizeof world);memset(&probe,0,sizeof probe);memset(&face,0,sizeof face);
    memset(victim,0,sizeof victim);memset(node,0,sizeof node);memset(point,0,sizeof point);memset(direction,0,sizeof direction);
    allocated=fail_call=calls=parent_valid=parent_notifications=0;
    regions[0]=(EmPoseRegion){0x5000,sizeof victim,victim,1};regions[1]=(EmPoseRegion){0x6000,16,point,1};
    regions[2]=(EmPoseRegion){0x7000,16,direction,1};
    h->regions=regions;h->region_count=3;world.enumerate=enumerate;world.map=node_map;world.written=written;owner.address_of=address_of;
    static EmCollProbeWorld cw;seg=(EmCollSegment){&cw,NULL,NULL,&probe,&face};
}
int main(void)
{
    EmAimFireLive h;EmAimFireTargetCall c;reset(&h);
    c=(EmAimFireTargetCall){.function=0x1839A0,.a={0x5000}};victim[3]=0x28;
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);CHECK(c.v0==0x100);
    c=(EmAimFireTargetCall){.function=0x1B1510,.f={0}};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);CHECK(c.f0==0);
    put(victim+0xB0,0x3F800000);put(victim+0xB4,0x40000000);put(victim+0xB8,0x40400000);
    c=(EmAimFireTargetCall){.function=0x183C40,.a={0x5000,0x6000},.sp=0x7F001400};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);CHECK(!memcmp(point,victim+0xB0,12));
    /* Region-only 00183C40 borrows the current target's model storage.
     * No complete target record covers +110; missing views keep the fault. */
    reset(&h);victim[2]=2;regions[0].size=0x110;target_queries=0;
    put(target_slots+8,0xA000);put(target_matrix+0xC0,0x3F800000);
    put(target_matrix+0xC4,0x40000000);put(target_matrix+0xC8,0x40400000);
    world.target_regions=target_regions;
    c=(EmAimFireTargetCall){.function=0x183C40,.a={0x5000,0x6000},.sp=0x7F001400};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);
    CHECK(!memcmp(point,target_matrix+0xC0,12));CHECK(target_queries==1);
    world.target_regions=NULL;
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)<0);CHECK(h.fault_address==0x5118);
    reset(&h);victim[3]=19;put(point,0xABCDEF01);put(point+4,0x23456789);put(point+8,0x87654321);
    c=(EmAimFireTargetCall){.function=0x1B41F0,.a={0x5000,0x6000,0x7000,1,0x1000,5},.sp=0x7F001400};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);CHECK(c.v0==1);CHECK(victim[0]==3);
    CHECK(!memcmp(victim+0x70,point,12));CHECK(victim[0x36]==25&&victim[0x37]==0x90);CHECK(calls==1);
    /* The knife's strike 00189FE0 on a kind-2 entity after a cell / entity
     * hit (0x700031D0 = D_700030B0): it passes the word at record +0x1C,
     * 0x700030CC (the hull walkers' word), to 001B41F0 (a00_09's first
     * bug). Read only; without that view the read faulted at 700030CC. */
    reset(&h);victim[0]=1;victim[2]=2;victim[3]=19;probe.record=EM_COLL_PROBE_RECORD_CELL;probe.entity=actors;
    face.hull_word_1c=1;face.hull_word_20=0x12345678u;
    {float a1[3]={4,5,6},a2[3]={1,2,3};memcpy(point,a1,12);memcpy(direction,a2,12);}
    sdk_vectors=1;sdk_calls=0;
    c=(EmAimFireTargetCall){.function=0x189FE0,.a={0x5000,0x6000,0x7000},.sp=0x7F001400};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);CHECK(!h.fault_address&&!h.fault_function);
    sdk_vectors=0;CHECK(sdk_calls==2);CHECK(victim[0]==3);CHECK(calls==1);
    CHECK(victim[0x36]==0&&victim[0x37]==0x90); /* type 19, word 1: p5 | p6 * 5 | 0x8000 */
    /* With another word at record +0x1C the strike differs (001B41F0's
     * fourth argument): the word is read, not assumed. */
    reset(&h);victim[0]=1;victim[2]=2;victim[3]=19;probe.record=EM_COLL_PROBE_RECORD_CELL;probe.entity=actors;
    face.hull_word_1c=0;sdk_vectors=1;
    {float a1[3]={4,5,6},a2[3]={1,2,3};memcpy(point,a1,12);memcpy(direction,a2,12);}
    c=(EmAimFireTargetCall){.function=0x189FE0,.a={0x5000,0x6000,0x7000},.sp=0x7F001400};
    {int rc=em_aim_fire_world_live_call(&world,&h,&c);sdk_vectors=0;CHECK(rc==0);}
    CHECK(victim[0]==3&&victim[0x36]==0&&victim[0x37]==0x10); /* word 0: p6 | p5 */
    reset(&h);victim[3]=19;fail_call=1;
    c=(EmAimFireTargetCall){.function=0x1B41F0,.a={0x5000,0x6000,0x7000,1,0,5},.sp=0x7F001400};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)<0);CHECK(victim[0]==3);CHECK(victim[0x36]==0);CHECK(h.fault_function==0x1031E0);
    const int before=calls;CHECK(em_aim_fire_world_live_call(&world,&h,&c)<0);CHECK(calls==before);
    reset(&h);c=(EmAimFireTargetCall){.function=0x1F4F40,.a={4},.sp=0x7F001400};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);CHECK(c.v0==0x9000);CHECK(node[0xD]==4);CHECK(word(node+0x10)==0x1F5040);
    reset(&h);put(victim+0x14,0x5000);put(victim+0xB0,0x3F800000);put(victim+0xB4,0x40000000);put(victim+0xBC,0x3F800000);
    c=(EmAimFireTargetCall){.function=0x1EFE00,.a={0x80000045u,0x5000},.sp=0x7F001400};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);CHECK(c.v0==0x9000);CHECK(word(node+0x24)==0x5000);
    CHECK(!memcmp(node+0xB0,victim+0xB0,16));CHECK(parent_valid&&parent_notifications==1);
    reset(&h);regions[0]=(EmPoseRegion){0x50B0,32,victim+0xB0,1};h.region_count=1;
    c=(EmAimFireTargetCall){.function=0x1EFE00,.a={0x80000045u,0x5000},.sp=0x7F001400};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)<0);CHECK(h.fault_address==0x5014);
    CHECK(!parent_valid&&parent_notifications==0);CHECK(!node_map(NULL,0x9024,4,0));
    reset(&h);EmPoseRegion crowded[EM_AIM_FIRE_WORLD_VIEWS-3];
    for(unsigned i=0;i<EM_AIM_FIRE_WORLD_VIEWS-3;++i)crowded[i]=regions[0];
    h.regions=crowded;h.region_count=EM_AIM_FIRE_WORLD_VIEWS-3;put(victim+0x14,0x5000);
    c=(EmAimFireTargetCall){.function=0x1EFE00,.a={0x80000045u,0x5000},.sp=0x7F001400};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)<0);CHECK(allocated&&!parent_valid&&parent_notifications==0);
    CHECK(word(node+0x24)==0);
    /* More views than the old 1,024-view list held (a00_03's 994 enumerated
     * plus the fixed views): the list is sized from the actor pool now. */
    {
        static EmPoseRegion many[2000];
        reset(&h);put(victim+0x14,0x5000);put(victim+0xB0,0x3F800000);put(victim+0xB4,0x40000000);put(victim+0xBC,0x3F800000);
        for(unsigned i=0;i<2000;++i)many[i]=regions[i<3?i:0];
        h.regions=many;h.region_count=2000;
        c=(EmAimFireTargetCall){.function=0x1EFE00,.a={0x80000045u,0x5000},.sp=0x7F001400};
        CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);CHECK(c.v0==0x9000);CHECK(word(node+0x24)==0x5000);
    }
    CHECK(EM_AIM_FIRE_WORLD_VIEWS>=EM_ACTOR_POOL_CAPACITY*EM_AIM_FIRE_WORLD_RECORD_VIEWS);
    reset(&h);float a[3]={1,2,3},b[3]={4,5,6};memcpy(point,a,12);memcpy(direction,b,12);
    c=(EmAimFireTargetCall){.function=0x19A570,.a={0x6000,0x7000,7,32}};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);CHECK(c.v0==2);
    CHECK(em_aim_fire_world_live_map(&world,0x700031B4,4,1)==&probe.point[1]);CHECK(probe.point[1]==17);
    c.function=0x19B6C0;CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);
    CHECK(word(em_aim_fire_world_live_map(&world,0x700031D0,4,0))==0x700030B0);
    CHECK(!em_aim_fire_world_live_map(&world,0x700031D0,4,1));
    /* point.w is 0x700031BC's one owner (word_31BC, see the header); the
     * quadword loads of 0x700031B0 read the point plus that word */
    world.word_31BC=0x12345678u;
    {const uint32_t *q=em_aim_fire_world_live_map(&world,0x700031B0,16,0);CHECK(q);
     CHECK(!memcmp(q,&probe.point[0],12));CHECK(q[3]==0x12345678u);}
    CHECK(!em_aim_fire_world_live_map(&world,0x700031B0,16,1)); /* the quadword view is read-only */
    probe.record=EM_COLL_PROBE_RECORD_GRID;probe.node=0;
    CHECK(!em_aim_fire_world_live_map(&world,0x700031D0,4,0)); /* absent original grid metadata */
    probe.entity=actors+1;CHECK(word(em_aim_fire_world_live_map(&world,0x700031D4,4,0))==0x5400);
    memset(&lists,0,sizeof lists);EmActorClassList *list=&lists.list[EM_ACTOR_LIST_CLASS2];
    list->slot[0]=actors;list->slot[1]=actors+1;list->published=2;
    CHECK(word(em_aim_fire_world_live_map(&world,0x275B8C,4,0))==0x28AF28);
    CHECK(word(em_aim_fire_world_live_map(&world,0x28AF28,4,0))==0x5400);
    CHECK(word(em_aim_fire_world_live_map(&world,0x28AF2C,4,0))==0x5000);
    list->slot[0]=actors+2;list->live=1; /* a push before the next close-out */
    CHECK(word(em_aim_fire_world_live_map(&world,0x28AF28,4,0))==0x5400);
    CHECK(word(em_aim_fire_world_live_map(&world,0x28AF2C,4,0))==0x5800);
    CHECK(!em_aim_fire_world_live_map(&world,0x28AF2C,4,1));
    /* 0019B2C0 on the move scratch: its words become the scratchpad views
     * until a segment probe runs; bit 31 writes a0 back. */
    reset(&h);memset(&move,0,sizeof move);memcpy(point,a,12);memcpy(direction,b,12);
    c=(EmAimFireTargetCall){.function=0x19B2C0,.a={0x6000,0x7000,0x80000006u}};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);CHECK(c.v0==2);CHECK(move_calls==1);
    {float x;memcpy(&x,point,4);CHECK(x==8);}
    CHECK(em_aim_fire_world_live_map(&world,0x700031B0,4,0)==&move.point[0]);
    CHECK(word(em_aim_fire_world_live_map(&world,0x700031D0,4,0))==0x700030B0);
    CHECK(em_aim_fire_world_live_map(&world,0x700030CA,2,0)==&move.cell_class);
    memcpy(point,a,12);c=(EmAimFireTargetCall){.function=0x19A570,.a={0x6000,0x7000,7,32}};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);
    CHECK(em_aim_fire_world_live_map(&world,0x700031B0,4,0)==&probe.point[0]);
    reset(&h);memcpy(point,a,12);memcpy(direction,b,12);
    c=(EmAimFireTargetCall){.function=0x19B2C0,.a={0x6000,0x7000,7}};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)<0);CHECK(h.fault_function==0x19B2C0);
    CHECK(!world.move_last);
    reset(&h);c=(EmAimFireTargetCall){.function=0x183C40,.a={0x5555,0x6000},.sp=0x7F001400};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)<0);CHECK(h.fault_address==0x5557);CHECK(calls==0);
    /* 001AA840 over a class-4 list that still holds a record freed after
     * publication: the free's cleared header words (+0x00..+0x0F) are its
     * view, so the entry's +2 class reads 0 and the knife's 001AA7A0 is not
     * called. An allocated record gets no such view here (the enumerate
     * owns it), so the same walk faults on it. */
    {
        memset(&lists,0,sizeof lists);EmActorClassList *c4=&lists.list[EM_ACTOR_LIST_CLASS4];
        memset(&actors[2],0,sizeof actors[2]);c4->slot[0]=actors+2;c4->published=1;
        reset(&h);c=(EmAimFireTargetCall){.function=0x1AA840,.sp=0x7F001400};
        CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);CHECK(calls==0);
        CHECK(!em_aim_fire_world_live_map(&world,0x5800,1,0)); /* not a general map */
        CHECK(!em_aim_fire_world_live_owner_view(0x5800,1,0)); /* outside a call */
        actors[2].allocated=1;actors[2].cls=4;
        reset(&h);c=(EmAimFireTargetCall){.function=0x1AA840,.sp=0x7F001400};
        CHECK(em_aim_fire_world_live_call(&world,&h,&c)<0);CHECK(calls==0);
        memset(&actors[2],0,sizeof actors[2]);memset(&lists,0,sizeof lists);
    }
    /* Inside the call: the list walks 0x5000 (an allocated reach target),
     * the freed 0x5800, then 0x5000 again. The first 001AA7A0 probes the
     * freed header (its 16 cleared bytes read, nothing past them, no
     * write) and re-enters the composition; the outer walk then still
     * reads its own views and reaches 0x5000 a second time. */
    {
        memset(&lists,0,sizeof lists);EmActorClassList *c4=&lists.list[EM_ACTOR_LIST_CLASS4];
        memset(&actors[2],0,sizeof actors[2]);memset(&actors[0],0,sizeof actors[0]);
        actors[0].allocated=1;actors[0].cls=4;
        c4->slot[0]=actors;c4->slot[1]=actors+2;c4->slot[2]=actors;c4->published=3;
        reset(&h);victim[0]=1;victim[2]=4;victim[3]=0x29;memset(victim2,0,sizeof victim2);
        reach_probe=1;reach_calls=0;
        c=(EmAimFireTargetCall){.function=0x1AA840,.sp=0x7F001400};
        CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);CHECK(reach_calls==2);
        CHECK(!em_aim_fire_world_live_owner_view(0x5800,1,0));
        reach_probe=0;memset(&actors[0],0,sizeof actors[0]);memset(&lists,0,sizeof lists);
    }
    printf("aim/fire world live: %d binding checks PASS\n",checks);return 0;
}
