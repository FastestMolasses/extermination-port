#include "game/em_aim_fire_runtime.h"
#include "game/em_aim_fire_binding.h"
#include "game/em_aim_fire_cable_live.h"
#include "game/em_aim_fire_diagnostic.h"
#include "game/em_aim_fire_render_live.h"
#include "game/em_aim_fire_world_live.h"
#include "game/em_effects_live.h"
#include "game/em_equipment_live.h"
#include "game/em_render_context_live.h"
#include "game/em_area11_boxes.h"
#include "game/em_scene_bindings.h"
#include <string.h>

#define TRY(x) do { if ((x)<0) return -1; } while (0)

/* Original BSS owners. The executable's phase word is not an area-reset
 * value. The cable worker writes each point before the strip drawer reads it. */
static uint32_t target_phase;             /* D_00275B00's first word only */
static uint32_t cable_points[12][4];       /* D_00821400..D_008214BF */

static struct {
    EmActorPool *pool;
    EmAimFireWorldLive world;
    EmAimFireRenderLive render;
    EmArea00HudVu vu;
    int vf23_valid;
    uint32_t self_word[EM_ACTOR_POOL_CAPACITY]; /* original pointer encodings */
    EmPoseRegion bss[2];
} R;
static void *span(uint32_t a,size_t n,uint32_t base,size_t size,void *p)
{
    return p && a>=base && n<=size && (uint64_t)a+n<=(uint64_t)base+size
        ? (uint8_t *)p+(a-base) : NULL;
}
static EmActor *pool_actor(uint32_t address)
{
    if (!R.pool || address<EM_ACTOR_POOL_BASE) return NULL;
    uint32_t offset=address-EM_ACTOR_POOL_BASE;
    if (offset%EM_ACTOR_RECORD_SIZE || offset/EM_ACTOR_RECORD_SIZE>=EM_ACTOR_POOL_CAPACITY) return NULL;
    return &R.pool->records[offset/EM_ACTOR_RECORD_SIZE];
}
static void *pool_field(uint32_t address,size_t size,int write)
{
    if (!R.pool || address<EM_ACTOR_POOL_BASE) return NULL;
    unsigned index=(address-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE;
    if (index>=EM_ACTOR_POOL_CAPACITY) return NULL;
    EmActor *a=&R.pool->records[index];
    if (!a->allocated) return NULL;
    uint32_t base=EM_ACTOR_POOL_BASE+index*EM_ACTOR_RECORD_SIZE;
    void *p;
    _Static_assert(offsetof(EmActor,callback)==0x10,"original pool header");
    if ((p=span(address,size,base,0x14,&a->status))) return p;
    if (!write && (p=span(address,size,base+0x14,4,&R.self_word[index]))) {
        R.self_word[index]=em_actor_pool_address(R.pool,a->self);return p;
    }
#define FIELD(field,offset) do { if ((p=span(address,size,base+(offset),sizeof a->field,&a->field))) return p; } while (0)
    FIELD(flags2,0x2E); FIELD(w30,0x30); FIELD(h36,0x36);
    FIELD(h52,0x52); FIELD(kind,0x54); FIELD(link,0x56); FIELD(w58,0x58); FIELD(w5C,0x5C);
    FIELD(f60,0x60); FIELD(f80,0x80); FIELD(w90,0x90); FIELD(h94,0x94); FIELD(h96,0x96);
    FIELD(b98,0x98); FIELD(b99,0x99); FIELD(table_index,0x9A);
    FIELD(b9C,0x9C); FIELD(b9D,0x9D); FIELD(b9E,0x9E); FIELD(pos,0xB0); FIELD(rot,0xC0);
#undef FIELD
    /* The matrix belongs to the bound world owner, not the pool header. */
    return span(address,size,base+0xD0,64,em_area11_boxes_owner_world(a));
}
static void *render_map(void *context,uint32_t a,size_t n,int write)
{ (void)context;return em_aim_fire_binding_bytes(a,n,write); }
static int render_forward(void *context,EmAimFireTargetCall *f)
{ (void)context;return em_aim_fire_binding_frame(f); }
static void *external_map(void *context,uint32_t a,size_t n,int write)
{
    (void)context;
    if (!n || n>UINT32_MAX || (uint64_t)a+n>UINT64_C(0x100000000)) return NULL;
    void *p=write ? em_rcl_bytes_mut(a,(uint32_t)n) : (void *)em_rcl_bytes(a,(uint32_t)n);
    if (!p) p=em_effects_live_node_field(a,n,write);
    if (!p) p=em_aim_fire_world_live_map(&R.world,a,n,write);
    if (!p) p=pool_field(a,n,write);
    if (!p && !write) p=(void *)em_effects_live_window(a,(uint32_t)n);
    return p;
}
static int append(EmPoseRegion *out,unsigned capacity,unsigned *count,
                  uint32_t address,uint32_t size,void *bytes,int write)
{
    if (!bytes) return 0;
    if (*count>=capacity) return -1;
    out[(*count)++]=(EmPoseRegion){address,size,bytes,write};return 0;
}
static int enumerate(void *context,EmPoseRegion *out,unsigned capacity,unsigned *count)
{
    (void)context;
    const EmCollisionWorldOwners *owners=em_collision_world_owners();
    if (!R.pool || !count) return -1;
    for (unsigned i=0;i<EM_ACTOR_POOL_CAPACITY;++i) {
        const EmActor *a=&R.pool->records[i];
        if (!a->allocated) continue;
        uint32_t address=em_actor_pool_address(R.pool,a);
        EmEquipmentLiveRegion equipment[16];
        size_t equipment_count=em_equipment_live_regions(address,equipment,16);
        if (equipment_count>16) return -1;
        if (equipment_count) {
            for (size_t j=0;j<equipment_count;++j)
                TRY(append(out,capacity,count,equipment[j].address,equipment[j].size,
                           equipment[j].bytes,equipment[j].writable));
            continue;
        }
        uint8_t *p=NULL;
        EmEffectsLiveNodeRegion fields[40];
        size_t fields_count=em_effects_live_node_regions(address,fields,40);
        if (fields_count>40) return -1;
        if (fields_count) {
            for (size_t j=0;j<fields_count;++j) {
                if (fields[j].size>UINT32_MAX) return -1;
                TRY(append(out,capacity,count,fields[j].address,(uint32_t)fields[j].size,fields[j].bytes,1));
            }
            TRY(append(out,capacity,count,address+0x14,4,pool_field(address+0x14,4,0),0));
            continue;
        }
        if (owners && owners->record_bytes)
            p=owners->record_bytes(owners->context,address,EM_ACTOR_RECORD_SIZE);
        if (p) {
            TRY(append(out,capacity,count,address,EM_ACTOR_RECORD_SIZE,p,1));
            continue;
        }
        /* Only original fields with actual native owners are enumerated.
         * The effect owner separately omits +24 until its observed write. */
        static const uint32_t spans[][2]={{0,0x14},{0x14,4},{0x2E,2},{0x36,2},
            {0xB0,16},{0xC0,16},{0xD0,64}};
        for (unsigned j=0;j<sizeof spans/sizeof spans[0];++j) {
            uint32_t at=address+spans[j][0],n=spans[j][1];
            p=pool_field(at,n,0);
            TRY(append(out,capacity,count,at,n,p,spans[j][0]!=0x14));
        }
    }
    return 0;
}
static int written(void *context,uint32_t address,size_t size)
{ (void)context;return em_effects_live_node_written(address,size); }
static int external_call(void *context,EmAimFireLive *live,EmAimFireTargetCall *f)
{
    (void)context;
    switch (f->function) {
    case 0x1CD520: case 0x1E2BA0: case 0x1DD170: case 0x1DD2F0: case 0x1DD600:
        /* Untracked render owners run between actor calls. Their last vf23
         * write is not exposed yet, so an ambient reticle input is unknown. */
        R.vf23_valid=0;
        return em_aim_fire_render_live_call(&R.render,f);
    case 0x1CD370: case 0x1CB5F0: case 0x1CB6B0: case 0x1CB900:
        return em_aim_fire_render_live_call(&R.render,f);
    case 0x1EFEB0: case 0x1CE860:return em_aim_fire_cable_live_call(live,f);
    case 0x21AAC0: case 0x21A500: {
        int status=em_aim_fire_cable_live_tick(live,(uint32_t)f->a[0],f->function);
        if (status<0) return -1;
        f->v0=(uint32_t)status;return 0;
    }
    case 0x1AFC10: {
        EmActor *a=pool_actor((uint32_t)f->a[0]);
        EmSceneState *scene=em_scene_state();
        if (!a || !a->allocated || !scene) return -1;
        return em_actor_pool_free_001AFC10(R.pool,scene,a)<0 ? -1 : 0;
    }
    case 0x1EF9D0: {
        float position[4];const float *p=NULL;uint32_t node=0;
        if (f->a[1]) {
            const void *q=em_aim_fire_live_map(live,(uint32_t)f->a[1],16,0);
            if (!q) return -1;
            memcpy(position,q,16);p=position;
        }
        TRY(em_effects_live_001EF9D0((uint32_t)f->a[0],p,f->f[0],&node));
        f->v0=node;return 0;
    }
    case 0x1EFD90: {
        const void *p=f->a[1] ? em_aim_fire_live_map(live,(uint32_t)f->a[1],16,0) : NULL;
        const void *q=em_aim_fire_live_map(live,(uint32_t)f->a[2],16,0);
        if ((!p && f->a[1]) || !q) return -1;
        float position[4],rotation[4];uint32_t node=0;
        if (p) memcpy(position,p,16);
        memcpy(rotation,q,16);
        TRY(em_effects_live_001EFD90_result((uint32_t)f->a[0],p ? position : NULL,rotation,&node));
        f->v0=node;return 0;
    }
    case 0x1CCF70: {
        const void *p=em_aim_fire_live_map(live,(uint32_t)f->a[0],16,0);
        if (!p) return -1;
        float position[4];int32_t key;memcpy(position,p,16);
        TRY(em_effects_live_001CCF70(position,&key));f->v0=(uint64_t)(int64_t)key;return 0;
    }
    case 0x1CFA60: case 0x1CFB50: {
        uint8_t *block=em_aim_fire_live_map(live,(uint32_t)f->a[0],0x60,1);
        uint32_t source=(uint32_t)f->a[f->function==0x1CFA60 ? 1 : 2];
        const void *p=em_aim_fire_live_map(live,source,64,0);
        if (!block || !p) return -1;
        float matrix[16];memcpy(matrix,p,64);
        if (f->function==0x1CFA60) return em_effects_live_001CFA60(block,matrix,f->f[0],f->f[1]);
        return em_effects_live_001CFB50(block,(int32_t)f->a[1],matrix,f->f);
    }
    case 0x1CFBE0: {
        const uint8_t *block=em_aim_fire_live_map(live,(uint32_t)f->a[3],0x60,0);
        if (!block) return -1;
        return em_effects_live_001CFBE0((int32_t)f->a[0],(int32_t)f->a[1],(uint32_t)f->a[2],block,(int32_t)f->a[4]);
    }
    default:return em_aim_fire_world_live_call(&R.world,live,f);
    }
}
void em_aim_fire_runtime_attach(EmActorPool *pool)
{
    memset(&R,0,sizeof R);R.pool=pool;
    if (!em_aim_fire_diagnostic()) {
        em_aim_fire_binding_set_extension(NULL,NULL,NULL);
        return;
    }
    R.world.enumerate=enumerate;
    R.world.written=written;
    R.bss[0]=(EmPoseRegion){0x275B00,sizeof target_phase,(uint8_t *)&target_phase,1};
    R.bss[1]=(EmPoseRegion){0x821400,sizeof cable_points,(uint8_t *)cable_points,1};
    R.world.regions=R.bss;R.world.region_count=2;
    R.render.map=render_map;R.render.call=render_forward;
    R.render.vu=&R.vu;R.render.vf23_valid=&R.vf23_valid;
    em_aim_fire_binding_set_extension(external_call,external_map,NULL);
}
static int other_tick(void *context,uint32_t node,uint32_t callback)
{
    (void)context;
    if (callback!=0x21AAC0 && callback!=0x21A500) return -1;
    EmAimFireTargetCall frame={0};frame.function=callback;frame.a[0]=node;frame.na=1;
    TRY(em_aim_fire_binding_frame(&frame));return (int32_t)frame.v0;
}
int em_aim_fire_runtime_effects_attach(void)
{
    return em_effects_live_set_other_tick(em_aim_fire_diagnostic() ? other_tick : NULL,NULL);
}
