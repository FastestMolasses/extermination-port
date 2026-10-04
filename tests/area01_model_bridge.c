/* Test-only fixture memory is supplied by the oracle. Production uses the
 * loader and actor services; no captured image is opened by the adapter. */
#include "game/em_area11_boxes.c"
#include "game/em_area01_model_live.h"

int em_area11_roger_001C6120(uint32_t bank,uint32_t id,uint32_t *handle)
{ (void)bank;(void)id;(void)handle; abort(); }
const uint8_t *em_area11_roger_resource(uint32_t address,uint32_t size)
{ (void)address;(void)size; abort(); }

static EmActorPool pool;
static EmSceneState scene;
static EmArea01ActorView views;
static EmArea01Model model;
static uint8_t *ram,*spr;
static uint32_t current,bone_array;
static EmPoseGlobals pose;
static EmPlayerFloorTable column;
static uint32_t metadata[3],words[EM_OWNER_SERVICES_MAX_BONES];
static unsigned random_calls,external_calls;
static uint32_t resource_hole;
static int dummy(EmActor *a,void *w) { (void)a;(void)w;return 1; }
static int rebind(void *ctx,EmActor *a,uint32_t callback)
{ (void)ctx;a->callback=callback;a->behavior=dummy;return 0; }
static const uint8_t *resource(void *ctx,uint32_t a,uint32_t n)
{ (void)ctx;if(resource_hole && a<=resource_hole && (uint64_t)a+n>resource_hole)return NULL;
  return a<0x2000000u && n<=0x2000000u-a ? ram+a : NULL; }
static const uint8_t *rest(void *ctx,uint32_t a,uint32_t *n)
{ (void)ctx;if(a>=0x2000000u)return NULL;*n=0x2000000u-a;return ram+a; }
static uint32_t random_value(void *ctx)
{ (void)ctx;random_calls++;return 7; }
static int worker(void *ctx,uint32_t fn,uint32_t a,uint32_t arg)
{ (void)ctx;(void)fn;(void)a;(void)arg;external_calls++;return 0; }
static int project(void *ctx,EmActor *a,EmArea01ActorSpan spans[EM_AREA01_ACTOR_SHARED_MAX])
{
    (void)ctx;
    if (!em_area11_boxes_owner_fields(a,&metadata[0],&metadata[1],&metadata[2])) return 0;
    for(unsigned k=0;k<a->bones;++k)
        if(!em_area11_boxes_owner_slot(a,k,&words[k]))return -1;
    EmOwnerServicesOwner *owner=em_area11_boxes_owner_view(a);
    if(!owner)return -1;
    spans[0]=(EmArea01ActorSpan){0x40,4,(uint8_t *)&owner->anim,1};
    spans[1]=(EmArea01ActorSpan){0x44,4,(uint8_t *)(metadata+1),0};
    spans[2]=(EmArea01ActorSpan){0x4C,4,(uint8_t *)(metadata+2),0};
    spans[3]=(EmArea01ActorSpan){0xD0,64,(uint8_t *)em_area11_boxes_owner_world(a),1};
    if(!a->bones)return 4;
    spans[4]=(EmArea01ActorSpan){0x110,(uint16_t)(a->bones*4),(uint8_t *)words,0};
    return 5;
}
int am_seed(uint8_t *memory,uint8_t *scratch,uint32_t node,int preserve_slots)
{
    ram=memory;spr=scratch;current=node;memcpy(&bone_array,ram+0x275B40,4);
    random_calls=external_calls=0;resource_hole=0;
    em_actor_pool_reset_001AF8E0(&pool);em_area11_boxes_reset();
    memset(&scene,0,sizeof scene);
    em_area01_actor_view_reset(&views,&pool,project,rebind,NULL);
    unsigned index=(node-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE;
    if(index>=EM_ACTOR_POOL_CAPACITY)return -1;
    EmActor *actor=NULL;
    for(unsigned i=0;i<=index;++i)actor=em_actor_pool_alloc_001AFA90(&pool,&scene,ram[node+2]);
    if(em_area01_actor_view_begin(&views)<0)return -1;
    uint8_t *p=em_area01_actor_view_bytes(&views,node,EM_ACTOR_RECORD_SIZE,0);
    if(!p)return -1;
    uint8_t links[12];memcpy(links,p+0x14,12);memcpy(p,ram+node,EM_ACTOR_RECORD_SIZE);memcpy(p+0x14,links,12);
    if(em_area01_actor_view_commit(&views)<0)return -1;
    (void)actor;
    if(preserve_slots) {
        memcpy(S.records,ram+EM_SLG_BONE_RECORDS,sizeof S.records);
        memcpy(S.bones.slot,ram+EM_SLG_BONE_ARRAY,sizeof S.bones.slot);
        memcpy(&S.bones.count,ram+0x275BCC,2);memcpy(&S.bones.head,ram+0x275BD0,4);
    }
    memset(&pose,0,sizeof pose);
    memcpy(&pose.d275BF8,ram+0x275BF8,4);memcpy(&pose.d275BF4,ram+0x275BF4,4);
    memcpy(&pose.d275BF0,ram+0x275BF0,4);memcpy(&pose.d275BEC,ram+0x275BEC,4);
    memcpy(pose.d8111F0,ram+0x8111F0,sizeof pose.d8111F0);
    pose.d8106F3=ram+0x8106F3;
    pose.spad3400=(uint32_t *)(void *)(spr+0x3400);pose.spad3440=(uint32_t *)(void *)(spr+0x3440);
    pose.spad3600=(uint32_t *)(void *)(spr+0x3600);pose.spad3760=(uint32_t *)(void *)(spr+0x3760);
    pose.spad3A3C=(uint32_t *)(void *)(spr+0x3A3C);pose.spad38B0=(uint32_t *)(void *)(spr+0x38B0);
    pose.spad3A20=(uint32_t *)(void *)(spr+0x3A20);pose.column=&column;
    EmArea01ModelSource source={0};source.resource=resource;source.resource_rest=rest;
    source.table=(const uint32_t *)(const void *)(ram+0x28A490);source.table_words=0xB0;
    source.current_actor=&current;source.current_bones=&bone_array;
    source.d8106F1=ram+0x8106F1;source.d810707=ram+0x810707;source.d810CB6=ram+0x810CB6;
    source.pose_globals=&pose;source.d810758=ram+0x810758;source.d810788=ram+0x810788;
    source.d810700=ram+0x810700;source.d8106D4=ram+0x8106D4;source.worker=worker;source.random=random_value;
    source.scratch3480=(uint32_t *)(void *)(spr+0x3480);
    return em_area01_model_bind(&model,&views,&source);
}
int am_call(uint32_t fn,uint32_t node,uint32_t a1,uint32_t a2,float f12,float f13,uint32_t *out)
{ return em_area01_model_call(&model,fn,node,a1,a2,0,f12,f13,out); }
uint8_t *am_record(void)
{
    if(em_area01_actor_view_begin(&views)<0)return NULL;
    uint8_t *p=em_area01_actor_view_bytes(&views,current,EM_ACTOR_RECORD_SIZE,0);
    if(!p || em_area01_actor_view_commit(&views)<0)return NULL;
    return p;
}
void am_snapshot(void)
{
    uint8_t *p=am_record();if(!p)abort();memcpy(ram+current,p,EM_ACTOR_RECORD_SIZE);
    memcpy(ram+EM_SLG_BONE_RECORDS,S.records,sizeof S.records);
    memcpy(ram+EM_SLG_BONE_ARRAY,S.bones.slot,sizeof S.bones.slot);
    memcpy(ram+0x275BCC,&S.bones.count,2);memcpy(ram+0x275BD0,&S.bones.head,4);
    memcpy(ram+0x275B40,&bone_array,4);memcpy(ram+0x275B48,&current,4);
    memcpy(ram+0x275BF8,&pose.d275BF8,4);memcpy(ram+0x275BF4,&pose.d275BF4,4);
    memcpy(ram+0x275BF0,&pose.d275BF0,4);memcpy(ram+0x275BEC,&pose.d275BEC,4);
    memcpy(ram+0x8111F0,pose.d8111F0,sizeof pose.d8111F0);
}
uint32_t am_fault(void){return model.fault_address;}
unsigned am_random_calls(void){return random_calls;}
unsigned am_external_calls(void){return external_calls;}
int am_bank(const char *path,uint32_t word){return em_area11_boxes_bind_world_bank(path,word);}
#include "game/em_area01_math_owner.h"
static uint8_t *math_bytes(void *ctx,uint32_t a,uint32_t n,int write)
{
    (void)ctx;
    uint8_t *p=em_area01_actor_view_bytes(&views,a,n,write);
    if(p)return p;
    p=em_area01_model_slot_bytes(&model,a,n);if(p)return p;
    if(a==0x275BCC && n==2)return (uint8_t *)S.stack.world.d00275BCC;
    return write ? NULL : (uint8_t *)(void *)resource(NULL,a,n);
}
static int math_worker(void *ctx,uint32_t fn,const uint32_t *a,unsigned na,
                       const uint32_t *f,unsigned nf,uint32_t *v0,uint32_t *f0)
{
    (void)ctx;(void)f0;float floats[2]={0};
    for(unsigned i=0;i<nf && i<2;++i)memcpy(&floats[i],&f[i],4);
    return em_area01_model_call(&model,fn,na?a[0]:0,na>1?a[1]:0,na>2?a[2]:0,
                                 na>3?a[3]:0,floats[0],floats[1],v0);
}
int am_special_bones(uint32_t node,uint32_t handle,uint32_t table,uint32_t frames,uint32_t loop)
{
    EmA01Math math={0};math.view=math_bytes;math.call=math_worker;
    if(em_area01_actor_view_begin(&views)<0)return -1;
    uint32_t result;
    if(em_area01_math_001D0C80(&math,node,handle,&result)<0 || result ||
       em_area01_math_001D0D40(&math,node,table,frames,loop)<0)return -1;
    return em_area01_actor_view_commit(&views);
}

int am_failure_boundary(uint32_t node,uint32_t hole,int active)
{
    if(active && em_area01_actor_view_begin(&views)<0)return -1;
    resource_hole=hole;
    uint32_t result;
    int rc=em_area01_model_call(&model,0x001B1020u,node,0,UINT32_MAX,0,0,0,&result);
    if(rc>=0 || model.fault_address!=hole || views.fault || views.active!=active)return -1;
    if(active && em_area01_actor_view_commit(&views)<0)return -1;
    return 0;
}

void am_library_owner(void)
{
    static uint32_t area_table[0xB0];
    memcpy(area_table,model.source.table,sizeof area_table);area_table[0x37]=0;
    model.source.library_word=model.source.table+0x37;
    model.source.table=area_table;
}

/* Compose only native-owned fields; the rest remains canonical passive
 * bytes, including the generic model's private +110 array after free. */
void am_pool_snapshot(void)
{
    static const unsigned spans[][2]={{0,0x20},{0x2E,0x38},{0x52,0x9B},
        {0x9C,0x9F},{0xB0,0xD0},{0x1F0,0x2F0}};
    unsigned index=(current-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE;
    memcpy(ram+current,views.record[index].image,EM_ACTOR_RECORD_SIZE);
    for(unsigned k=0;k<EM_ACTOR_POOL_CAPACITY;++k) {
        uint8_t image[EM_ACTOR_RECORD_SIZE];
        em_actor_pool_record_image(&pool,&pool.records[k],image);
        for(unsigned i=0;i<sizeof spans/sizeof spans[0];++i)
            memcpy(ram+EM_ACTOR_POOL_BASE+k*EM_ACTOR_RECORD_SIZE+spans[i][0],
                   image+spans[i][0],spans[i][1]-spans[i][0]);
    }
    EmActorPoolGlobals g;em_actor_pool_globals(&pool,&g);
    memcpy(ram+0x275BC0,&g.head,4);memcpy(ram+0x275BBC,&g.tail,4);
    memcpy(ram+0x275BC4,&g.free_head,4);memcpy(ram+0x275BC8,&g.free_count,2);
    memcpy(ram+EM_SLG_BONE_RECORDS,S.records,sizeof S.records);
    memcpy(ram+EM_SLG_BONE_ARRAY,S.bones.slot,sizeof S.bones.slot);
    memcpy(ram+0x275BCC,&S.bones.count,2);memcpy(ram+0x275BD0,&S.bones.head,4);
}
static int release_hook(void *ctx,EmActor *actor)
{ return em_area01_model_release_native(ctx,actor); }
int am_release_pool(uint32_t node,int reuse)
{
    EmArea01ModelRelease release;
    if(em_area01_actor_view_begin(&views)<0 ||
       em_area01_model_release_prepare(&model,node,&release)<0 ||
       em_area01_actor_view_commit(&views)<0)return -1;
    EmActor *a=release.actor;pool.w_001AF800=release_hook;pool.worker_ctx=&release;
    if(em_actor_pool_free_001AFC10(&pool,&scene,a)<0)return -1;
    if(views.fault || views.active || release.ready ||
       em_area01_model_release_native(&release,a)>=0)return -1;
    if(reuse) {
        if(em_actor_pool_alloc_001AFA90(&pool,&scene,7)!=a)return -1;
        if(em_area01_actor_view_begin(&views)<0 || em_area01_actor_view_touch(&views,node)<0 ||
           em_area01_actor_view_commit(&views)<0)return -1;
    }
    am_pool_snapshot();return 0;
}

static int equipment_worker(void *ctx,uint32_t fn,uint32_t node,uint32_t arg)
{
    if(fn!=0x001AFC10u)return worker(ctx,fn,node,arg);
    ++external_calls;
    unsigned index=(node-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE;
    return em_actor_pool_free_001AFC10(&pool,&scene,&pool.records[index]);
}
int am_equipment_parent(uint32_t parent)
{
    if(em_area01_actor_view_begin(&views)<0)return -1;
    uint8_t *p=em_area01_actor_view_bytes(&views,parent,EM_ACTOR_RECORD_SIZE,0);
    if(!p)return -1;
    uint8_t links[12];memcpy(links,p+0x14,12);
    memcpy(p,ram+parent,EM_ACTOR_RECORD_SIZE);memcpy(p+0x14,links,12);
    if(em_area01_actor_view_commit(&views)<0)return -1;
    memcpy(ram+parent,p,EM_ACTOR_RECORD_SIZE);
    model.source.worker=equipment_worker;
    am_snapshot();am_pool_snapshot();return 0;
}
void am_equipment_snapshot(void)
{
    if(pool.records[(current-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE].self)am_snapshot();
    am_pool_snapshot();
}

int am_box_bone_change(uint32_t node,uint32_t value)
{
    uint32_t result;
    if(em_area01_model_call(&model,0x001B0FD0u,node,0,0,0,0,0,&result)<0 || result)return -1;
    uint8_t *p=am_record();if(!p || p[9]<3)return -1;
    uint32_t word;memcpy(&word,p+0x118,4);
    uint8_t *slot=em_area01_model_slot_bytes(&model,word,EM_POSE_NODE_BYTES);if(!slot)return -1;
    memcpy(slot+0x78,&value,4);
    return em_area01_model_call(&model,0x001C6380u,node,0,0,0,0,0,&result);
}

/* Test-only: attach the captured generic parent to the existing Box view
 * without allocating slots, then change its canonical raw matrix after
 * taking the typed snapshot. This catches a stale typed export at child
 * worker entry; production does not reconstruct owners from captures. */
int am_equipment_parent_raw(uint32_t parent,uint32_t value)
{
    unsigned index=(parent-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE;
    if(index>=EM_ACTOR_POOL_CAPACITY)return -1;
    EmActor *a=&pool.records[index];Box *b=owner_box_bind(a,&pool);
    if(!b || a->bones<2)return -1;
    uint8_t *p=views.record[index].image;
    memcpy(&b->view.anim,p+0x40,4);memcpy(&b->view.model,p+0x44,4);
    memcpy(&b->method,p+0x4C,4);memcpy(b->view.world,p+0xD0,64);
    b->view.bones_held=a->bones;b->view.bone_count=a->u0A[2];
    for(unsigned i=0;i<a->bones;++i) {
        uint32_t address;memcpy(&address,p+0x110+4*i,4);
        if(address<EM_SLG_BONE_RECORDS || (address-EM_SLG_BONE_RECORDS)%EM_SLG_BONE_SLOT_SIZE)return -1;
        unsigned slot=(address-EM_SLG_BONE_RECORDS)/EM_SLG_BONE_SLOT_SIZE;
        if(slot>=BONE_SLOTS)return -1;b->view.bone[i]=&S.slots[slot];
    }
    if(em_area11_boxes_owner_sync_slots(a,1)<0)return -1;
    uint32_t address;memcpy(&address,p+0x114,4);
    uint8_t *slot=em_area01_model_slot_bytes(&model,address,EM_POSE_NODE_BYTES);
    if(!slot)return -1;memcpy(slot+0xC0,&value,4);
    return 0;
}

int am_pose69_call(uint32_t node,int active)
{
    if(active && em_area01_actor_view_begin(&views)<0)return -1;
    uint32_t out=0;int rc=em_area01_model_call(&model,0x001C69A0u,node,0,0,0,0,0,&out);
    if(views.fault || views.active!=active)return -2;
    if(active && em_area01_actor_view_commit(&views)<0)return -2;
    return rc;
}
int am_pose69_missing(unsigned span)
{
    switch(span) {
    case 0:pose.spad3400=NULL;break;
    case 1:pose.spad3440=NULL;break;
    case 2:model.source.scratch3480=NULL;break;
    case 3:pose.spad3600=NULL;break;
    case 4:pose.spad3760=NULL;break;
    default:return -1;
    }
    return 0;
}
int am_pose69_box_prepare(uint32_t node)
{
    /* Attach only a typed view of this fixture's existing raw slots. */
    uint32_t bone;memcpy(&bone,views.record[(node-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE].image+0x114,4);
    uint32_t before;memcpy(&before,S.records+(bone-EM_SLG_BONE_RECORDS)+0xC0,4);
    return am_equipment_parent_raw(node,before);
}
int am_pose69_box(uint32_t node,int active)
{
    const unsigned index=(node-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE;
    EmOwnerServicesOwner *view=em_area11_boxes_owner_view(&pool.records[index]);
    if(!view)return -1;
    /* Set a raw rest angle after the typed snapshot. The adapter must not
     * export that stale typed angle at entry and must import the new world
     * before a native draw consumes this same owner. */
    uint32_t bone;if(!em_area11_boxes_owner_slot(&pool.records[index],1,&bone))return -1;
    uint32_t angle=0x3F400000u;memcpy(S.records+(bone-EM_SLG_BONE_RECORDS)+0x78,&angle,4);
    if(am_pose69_call(node,active)<0)return -1;
    for(unsigned i=0;i<view->bones_held;++i) {
        uint32_t address;if(!em_area11_boxes_owner_slot(&pool.records[index],i,&address))return -1;
        if(memcmp(view->bone[i]->world,S.records+(address-EM_SLG_BONE_RECORDS)+0x90,64))return -1;
    }
    return 0;
}
