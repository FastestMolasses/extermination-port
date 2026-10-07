/* The production region enumerator and world composition, with unrelated
 * optional owners absent. Only actual native actor fields are available. */
#define em_sfx_compute_gains ah_gain_boundary
#include "game/em_aim_fire_runtime.c"
#undef em_sfx_compute_gains
#include "game/em_aim_fire_sdk_memory.h"
int em_sfx_compute_gains(const float pos[3],float radius,float *left,float *right);
static EmActorPool hit_pool;
static EmCollProbeState hit_probe;
static EmCollSegmentFaceScratch hit_face;
static EmCollSegment hit_segment;
static uint8_t *hit_ram,*hit_spr;
static uint32_t hit_node;
static int (*hit_callback)(EmAimFireTargetCall *);
static int gain_result,cue_missing,gain_calls,gain_bad;
static float gain_left,gain_right;
int32_t (*em_stream_live_d281F30(void))[4]
{ return cue_missing?NULL:(int32_t (*)[4])(void *)(hit_ram+0x281F30); }
int ah_gain_boundary(const float p[3],float radius,float *left,float *right)
{
    ++gain_calls;
    if(radius!=300.0f || memcmp(p,hit_ram+hit_node+0xB0,12)){gain_bad=1;return 0;}
    if(gain_result==2)return em_sfx_compute_gains(p,radius,left,right);
    *left=gain_left;*right=gain_right;return gain_result;
}
const unsigned char em_coll_move_cell_record_tag;
const EmCollisionWorldOwners *em_collision_world_owners(void) { return NULL; }
const EmCollSegment *em_collision_world_segment(void) { return &hit_segment; }
const EmActorClassLists *em_collision_world_lists(void) { return NULL; }
int em_coll_segment_face_worker(void *c,const uint8_t *p,EmCollProbeState *s,int *h)
{ (void)c;(void)p;(void)s;(void)h;return -1; }
int em_coll_segment_round_worker(void *c,const uint8_t *p,EmCollProbeState *s,int *h)
{ (void)c;(void)p;(void)s;(void)h;return -1; }
const EmCollMoveWorld *em_collision_world_move(void) { return NULL; }
EmCollMoveScratch *em_collision_world_move_scratch(void) { return NULL; }
int em_coll_grid_hull_node_index(const EmCollProbeGrid *g,const void *r) { (void)g;(void)r;return -1; }
int em_coll_move_probe_0019B2C0(const EmCollMoveWorld *w,EmCollMoveScratch *s,float a[3],const float b[3],uint32_t f)
{ (void)w;(void)s;(void)a;(void)b;(void)f;return -1; }
int em_coll_segment_0019A570(const EmCollSegment *s,const float *a,const float *b,unsigned m,int id)
{ (void)s;(void)a;(void)b;(void)m;(void)id;return -1; }
float *em_area11_boxes_owner_world(EmActor *a) { (void)a;return NULL; }
size_t em_effects_live_particle_regions(EmEffectsLiveNodeRegion *r,size_t n) { (void)r;(void)n;return 0; }
size_t em_effects_live_node_regions(uint32_t a,EmEffectsLiveNodeRegion *r,size_t n) { (void)a;(void)r;(void)n;return 0; }
unsigned em_rcl_views(uint32_t *a,uint32_t *n,uint8_t **p,int *w,unsigned c) { (void)a;(void)n;(void)p;(void)w;(void)c;return 0; }
size_t em_equipment_live_regions(uint32_t a,EmEquipmentLiveRegion *r,size_t n) { (void)a;(void)r;(void)n;return 0; }
int em_aim_fire_flash_owns(const EmActor *a) { (void)a;return 0; }
size_t em_aim_fire_flash_regions(uint32_t a,EmPoseRegion *r,size_t n) { (void)a;(void)r;(void)n;return 0; }
size_t em_aim_fire_trail_regions(uint32_t a,EmPoseRegion *r,size_t n) { (void)a;(void)r;(void)n;return 0; }
size_t em_bone_burst_regions(uint32_t a,EmPoseRegion *r,size_t n) { (void)a;(void)r;(void)n;return 0; }
static EmActor *hit_actor(void) { return &hit_pool.records[(hit_node-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE]; }
static void publish(void)
{
    EmActor *a=hit_actor();
    memcpy(hit_ram+hit_node,&a->status,0x14);
    memcpy(hit_ram+hit_node+0x34,&a->w34,2);
    memcpy(hit_ram+hit_node+0x36,&a->h36,2);
    memcpy(hit_ram+hit_node+0x60,a->f60,32);
    memcpy(hit_spr+0x3680,&hit_probe.ratio,4);
    memcpy(hit_spr+0x3684,hit_face.cross,8);
}
void *em_aim_fire_live_map(void *ctx,uint32_t a,size_t n,int w)
{
    EmAimFireLive *h=ctx;
    for(unsigned i=0;i<h->region_count;++i) {
        EmPoseRegion *r=(EmPoseRegion *)&h->regions[i];
        void *p=(!w||r->writable)?span(a,n,r->address,r->size,r->bytes):NULL;
        if(p)return p;
    }
    void *p=em_aim_fire_world_live_map(&R.world,a,n,w);
    return p?p:pool_field(a,n,w);
}
int em_aim_fire_live_call(EmAimFireLive *h,EmAimFireTargetCall *c)
{ (void)h;publish();return hit_callback?hit_callback(c):-1; }
int ah_sdk(EmAimFireTargetCall *c)
{
    EmPoseRegion inputs[]={{0x700038B0,32,hit_spr+0x38B0,1}};
    EmAimFireLive live={.regions=inputs,.region_count=1};
    int rc=em_aim_fire_sdk_memory_call(&live,em_aim_fire_live_map,c);publish();return rc;
}
static void initialize(uint8_t *ram,uint8_t *spr,uint32_t node)
{
    memset(&R,0,sizeof R);memset(&hit_pool,0,sizeof hit_pool);
    memset(&hit_probe,0,sizeof hit_probe);memset(&hit_face,0,sizeof hit_face);
    hit_ram=ram;hit_spr=spr;hit_node=node;R.pool=&hit_pool;
    EmActor *a=hit_actor();a->allocated=1;a->self=a;
    memcpy(&a->status,ram+node,0x14);memcpy(&a->w34,ram+node+0x34,2);
    memcpy(&a->h36,ram+node+0x36,2);memcpy(a->f60,ram+node+0x60,32);
    memcpy(a->pos,ram+node+0xB0,16);memcpy(a->rot,ram+node+0xC0,16);
    memcpy(&hit_probe.ratio,spr+0x3680,4);memcpy(hit_face.cross,spr+0x3684,8);
    hit_segment=(EmCollSegment){.state=&hit_probe,.face=&hit_face};
    R.world.enumerate=enumerate;R.world.segment=&hit_segment;
}
int ah_run(uint8_t *ram,uint8_t *spr,uint32_t node,int(*worker)(EmAimFireTargetCall *),uint32_t *fault)
{
    initialize(ram,spr,node);hit_callback=worker;
    EmPoseRegion inputs[]={{0x810374,4,ram+0x810374,0},
                          {0x700038B0,32,spr+0x38B0,1},
                          {0x70003600,64,spr+0x3600,1}};
    EmAimFireLive live={.regions=inputs,.region_count=3};
    EmAimFireTargetCall c={.function=0x1B41F0,.sp=0x7F0F0000,.a={node,0x700038B0,0x700038C0,0,0,5},.na=6};
    int rc=em_aim_fire_world_live_call(&R.world,&live,&c);publish();
    fault[0]=live.fault_function;fault[1]=live.fault_address;fault[2]=(uint32_t)c.v0;
    return rc;
}
int ah_sound(uint8_t *ram,uint8_t *spr,uint32_t node,uint32_t id,int result,
             float left,float right,int deny)
{
    initialize(ram,spr,node);gain_result=result;gain_left=left;gain_right=right;
    gain_calls=gain_bad=0;cue_missing=deny==1;if(deny==2)hit_actor()->allocated=0;
    if(result==2) {
        float player[3],eye[3],yaw;
        memcpy(player,ram+0x810360,12);memcpy(eye,ram+0x8105D0,12);memcpy(&yaw,ram+0x81027C,4);
        em_sfx_listener(player,eye,yaw);
    }
    EmAimFireLive live={0};
    EmAimFireTargetCall c={.function=0x1FC580,.sp=0x7F0EFF80,.a={node,id},.na=2};
    int rc=target_sound(&live,&c);
    return rc<0?rc:gain_bad?-99:gain_calls;
}
