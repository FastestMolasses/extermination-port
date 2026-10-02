/* Binding checks, not a replacement for each reused owner's instruction oracle. */
#include "game/em_aim_fire_world_live.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static EmAimFireWorldLive world;
static EmCollProbeState probe;
static EmCollSegmentFaceScratch face;
static EmCollSegment seg;
static EmActorClassLists lists;
static EmCollisionWorldOwners owner;
static EmActor actors[3];
static uint8_t victim[0x320],node[0x320],point[16],direction[16];
static EmPoseRegion regions[4];
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
    h->fault_function=c->function;return -1;
}
static uint32_t word(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
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
    reset(&h);victim[3]=19;put(point,0xABCDEF01);put(point+4,0x23456789);put(point+8,0x87654321);
    c=(EmAimFireTargetCall){.function=0x1B41F0,.a={0x5000,0x6000,0x7000,1,0x1000,5},.sp=0x7F001400};
    CHECK(em_aim_fire_world_live_call(&world,&h,&c)==0);CHECK(c.v0==1);CHECK(victim[0]==3);
    CHECK(!memcmp(victim+0x70,point,12));CHECK(victim[0x36]==25&&victim[0x37]==0x90);CHECK(calls==1);
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
    printf("aim/fire world live: %d binding checks PASS\n",checks);return 0;
}
