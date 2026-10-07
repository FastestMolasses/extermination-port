#include "game/em_area01_live.h"
#include "game/em_area01_scene_view.h"
#include "game/em_area01_script_workers.h"
#include "game/em_area01_timeline.h"
#include "game/em_area01_audio_services.h"
#include "game/em_area01_camera_services.h"
#include "game/em_area01_indicator_live.h"
#include "game/em_area01_prop_live.h"
#include "game/em_area01_gun_aux.h"
#include "game/em_area01_shared_services.h"
#include "game/em_area01_light_live.h"
#include "game/em_area01_rcl_workers.h"
#include "game/em_area01_matrix_service.h"
#include "game/em_area01_flame_services.h"
#include "game/em_area11_boxes.h"
#include "game/em_area00_low.h"
#include "game/em_anim_runtime_rest.h"
#include "game/em_shadow_live.h"
#include "game/em_area11_bindings.h"
#include "game/em_area11_roger.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_aim_fire_binding.h"
#include "game/em_aim_fire_runtime.h"
#include "game/em_aim_fire_sdk_memory.h"
#include "game/em_camera_live.h"
#include "game/em_collision_world.h"
#include "game/em_effects_live.h"
#include "game/em_area01_effects_services.h"
#include "game/em_pickup.h"
#include "game/em_pickup_motion.h"
#include "game/em_security_gun_rest.h"
#include "game/em_security_gun.h"
#include "game/em_player.h"
#include "game/em_owner_draw_live.h"
#include "game/em_random.h"
#include "game/em_render_context_live.h"
#include "game/em_script_host_workers.h"
#include "game/em_script_door_fan.h"
#include "game/em_player_recovery.h"
#include "game/em_stream_lanes_original.h"
#include "game/em_stream_live.h"
#include "game/em_frame.h"
#include "game/em_scene_bindings.h"
#include "game/em_sfx.h"
#include "game/em_pad_actuator.h"
#include "game/em_status_ui_leftovers.h"
#include "game/em_ee_float.h"
#include <string.h>

#define LOCAL_BASE 0x7F800000u
static int fail(EmArea01Live *l, uint32_t address)
{ if (l && !l->fault) { l->fault=1; l->fault_address=address; } return -1; }
static int contains(uint32_t a,uint32_t n,uint32_t base,uint32_t size)
{ return n && a>=base && n<=size && a-base<=size-n; }
static int overlaps(uint32_t a,uint32_t n,uint32_t base,uint32_t size)
{ return (uint64_t)a+n>base && (uint64_t)base+size>a; }
static const uint8_t *resource(void *,uint32_t,uint32_t);
static EmActor *actor(EmArea01Live *l,uint32_t a)
{
    if(a<EM_ACTOR_POOL_BASE)return NULL;
    uint32_t at=a-EM_ACTOR_POOL_BASE;
    if(at%EM_ACTOR_RECORD_SIZE || at/EM_ACTOR_RECORD_SIZE>=EM_ACTOR_POOL_CAPACITY)return NULL;
    EmActor *p=&l->host.pool->records[at/EM_ACTOR_RECORD_SIZE];
    return p->allocated && p->self==p ? p : NULL;
}
static int project(void *ctx,EmActor *a,EmArea01ActorSpan spans[EM_AREA01_ACTOR_SHARED_MAX])
{
    EmArea01Live *l=ctx;
    int effects=em_area01_effects_services_project(a,spans);
    if(effects)return effects;
    if(!em_area11_boxes_owner_fields(a,l->metadata,l->metadata+1,l->metadata+2))return 0;
    if(a->bones>EM_OWNER_SERVICES_MAX_BONES)return -1;
    for(unsigned i=0;i<a->bones;++i)
        if(!em_area11_boxes_owner_slot(a,i,&l->slots[i]))return -1;
    EmOwnerServicesOwner *view=em_area11_boxes_owner_view(a);
    if(!view)return -1;
    spans[0]=(EmArea01ActorSpan){0x40,4,(uint8_t *)&view->anim,1};
    spans[1]=(EmArea01ActorSpan){0x44,4,(uint8_t *)(l->metadata+1),0};
    spans[2]=(EmArea01ActorSpan){0x4C,4,(uint8_t *)(l->metadata+2),0};
    spans[3]=(EmArea01ActorSpan){0xD0,64,(uint8_t *)em_area11_boxes_owner_world(a),1};
    if(!a->bones)return 4;
    spans[4]=(EmArea01ActorSpan){0x110,(uint16_t)(4*a->bones),(uint8_t *)l->slots,0};
    return 5;
}
static int rebind(void *ctx,EmActor *a,uint32_t fn)
{ EmArea01Live *l=ctx;return l->host.rebind(l->host.ctx,a,fn); }
static int player_begin(EmArea01Live *l)
{
    return l->player_stage ? em_area01_player_view_borrow_begin(&l->player)
                           : em_area01_player_view_begin(&l->player);
}

int em_area01_live_resume(EmArea01Live *l)
{
    if(!l || !l->bound || l->fault || l->active)return -1;
    em_camera_live_adopt_view();
    if(em_area01_actor_view_begin(&l->actors)<0 || player_begin(l)<0)
        return fail(l,l->actors.fault ? l->actors.fault_address : l->player.fault_address);
    if(em_area01_collision_view_begin(&l->collision)<0)return fail(l,l->collision.fault_address);
    l->active=1;return 0;
}
int em_area01_live_suspend(EmArea01Live *l)
{
    if(!l || !l->bound || l->fault || !l->active)return -1;
    if(em_area01_actor_view_commit(&l->actors)<0 || em_area01_player_view_commit(&l->player)<0)
        return fail(l,l->actors.fault ? l->actors.fault_address : l->player.fault_address);
    if(em_area01_collision_view_commit(&l->collision)<0)return fail(l,l->collision.fault_address);
    em_camera_live_view_publish();l->active=0;return 0;
}

static uint8_t *pose_bytes(EmPoseHost *p,uint32_t a,uint32_t n,int write,int *owned)
{
    if(!p)return NULL;
    for(unsigned i=0;i<p->region_count;++i) {
        EmPoseRegion *r=&p->region[i];
        if(overlaps(a,n,r->address,r->size)) {
            *owned=1;
            return contains(a,n,r->address,r->size) && (!write || r->writable)
                ? r->bytes+a-r->address : NULL;
        }
    }
    return NULL;
}
uint8_t *em_area01_live_matrix_3000(EmArea01Live *l,uint32_t a,uint32_t n)
{
    return l && l->bound && l->active && !l->fault &&
           contains(a,n,0x70003000u,sizeof l->scratch_3000)
        ? (uint8_t *)l->scratch_3000+a-0x70003000u : NULL;
}
int em_area01_live_head_record(EmArea01Live *l,uint32_t address,uint8_t record[0x2F0])
{
    if(!l || !l->bound || l->fault || l->active || !record || address<EM_ACTOR_POOL_BASE)return -1;
    uint32_t at=address-EM_ACTOR_POOL_BASE;
    if(at%EM_ACTOR_RECORD_SIZE || at/EM_ACTOR_RECORD_SIZE>=EM_ACTOR_POOL_CAPACITY)return -1;
    const EmActor *p=&l->host.pool->records[at/EM_ACTOR_RECORD_SIZE];
    if(!p->allocated) {
        /* The talk owner 00825740 frees itself on the first visit while
         * its head still ticks. 001AFC10 cleared +0x00..+0x0F and the
         * +0x1F0 words, so the head reads b01 = b02 = 0 and +0x220 = 0
         * (it ends, 001E2560 lifecycle 3) without reaching the +0x110
         * slots: the pool's own image of the freed record is exact for
         * every byte it reads; its slot count reads 0. */
        em_actor_pool_record_image(l->host.pool,p,record);
        return 0;
    }
    EmActor *a=actor(l,address);
    if(!a || !l->host.private_model || !l->host.private_model(l->host.ctx,a))return -1;
    return em_area01_actor_view_snapshot(&l->actors,address,EM_ACTOR_RECORD_SIZE,record);
}
const uint8_t *em_area01_live_head_bytes(EmArea01Live *l,uint32_t address,uint32_t size)
{
    return l && l->bound && !l->fault && !l->active ? em_area01_model_slot_bytes(&l->model,address,size) : NULL;
}
uint8_t *em_area01_live_bytes(EmArea01Live *l,uint32_t a,uint32_t n,int write)
{
    if(!l || !l->bound || l->fault || !n || (uint64_t)a+n>UINT64_C(0x100000000))return NULL;
    uint8_t *p;
    if(overlaps(a,n,EM_ACTOR_POOL_BASE,EM_ACTOR_RECORD_SIZE*EM_ACTOR_POOL_CAPACITY))
        return em_area01_actor_view_bytes(&l->actors,a,n,write);
    if(overlaps(a,n,EM_AREA01_PLAYER_BASE,EM_PLAYER_ACTOR_SIZE))
        return em_area01_player_view_bytes(&l->player,a,n,write);
    if(overlaps(a,n,LOCAL_BASE,sizeof l->locals))
        return contains(a,n,LOCAL_BASE,sizeof l->locals) ? l->locals+a-LOCAL_BASE : NULL;
    if(overlaps(a,n,0x70003000u,sizeof l->scratch_3000))
        return em_area01_live_matrix_3000(l,a,n);
    if(overlaps(a,n,0x700034C0u,sizeof l->scratch_34C0))
        return l->active && contains(a,n,0x700034C0u,sizeof l->scratch_34C0)
            ? (uint8_t *)l->scratch_34C0+a-0x700034C0u : NULL;
    if(overlaps(a,n,0x700036E0u,sizeof l->scratch_36E0))
        return l->active && contains(a,n,0x700036E0u,sizeof l->scratch_36E0)
            ? (uint8_t *)l->scratch_36E0+a-0x700036E0u : NULL;
    if(overlaps(a,n,0x00275C00u,4))
        return l->active && contains(a,n,0x00275C00u,4)
            ? (uint8_t *)&l->d275C00+a-0x00275C00u : NULL;
    if(overlaps(a,n,0x00275C04u,4)) {
        int32_t *clip=em_effects_live_d275C04();
        return l->active && clip && contains(a,n,0x00275C04u,4)
            ? (uint8_t *)clip+a-0x00275C04u : NULL;
    }
    if(em_area01_collision_view_owns(&l->collision,a,n))
        return em_area01_collision_view_bytes(&l->collision,a,n,write);
    if((p=em_area01_scene_view(l->host.scene,a,n)))return p;
    if(overlaps(a,n,0x0028A9A0u,2)) {
        /* D_0028A9A0 (D_0028A8E0 + 0xC0): 001AEE70's transition substate,
         * held by its one owner em_frame_transition(); read-only here
         * (00184BA0's Use-scan gate). */
        const EmTransitionFade *fade=em_frame_transition();
        return !write && fade && contains(a,n,0x0028A9A0u,2)
            ? (uint8_t *)(void *)&fade->substate+a-0x0028A9A0u : NULL;
    }
    if((p=em_camera_live_bytes(a,n)))return l->active ? p : NULL;
    if(overlaps(a,n,0x700038A0u,0x60u))
        return l->active ? em_camera_live_scratch_bytes(a,n) : NULL;
    if((p=em_area11_interaction_host_scan_memory(a,n)))return p;
    if((p=em_effects_live_scratch_3660(a,n)))return p;
    if((p=em_area01_state_bytes(l->host.area,a,n)))return p;
    if(contains(a,n,0x00275B44u,4))return write ? NULL : (uint8_t *)l->host.current_actor+a-0x00275B44u;
    if(contains(a,n,0x00275B48u,4))return write ? NULL : (uint8_t *)l->host.current_actor+a-0x00275B48u;
    if(contains(a,n,0x00275B40u,4))return (uint8_t *)l->model.source.current_bones+a-0x00275B40u;
    const EmRogerActorWorld *slots=em_area11_boxes_slot_world();
    if(slots && contains(a,n,0x00275BCCu,2))return (uint8_t *)slots->d00275BCC+a-0x00275BCCu;
    if(slots && contains(a,n,0x00275BD0u,4))return (uint8_t *)slots->d00275BD0+a-0x00275BD0u;
    /* Player-reserved slots must resolve before the shared free-slot arena.
     * The existing 0015C420 removes those addresses from its stack. */
    EmPoseHost *pose=player_pose_record_host();
    int owned=0;
    p=pose_bytes(pose,a,n,write,&owned);
    if(owned)return p;
    EmPoseGlobals *pg=pose ? pose->globals : NULL;
    if(pg) {
#define POSE(base,count,ptr) do { \
    if(overlaps(a,n,(base),(count)))return contains(a,n,(base),(count)) && (ptr) \
        ? (uint8_t *)(ptr)+a-(base) : NULL; \
} while(0)
        POSE(0x00275BECu,4,&pg->d275BEC);POSE(0x00275BF0u,4,&pg->d275BF0);
        POSE(0x00275BF4u,4,&pg->d275BF4);POSE(0x00275BF8u,4,&pg->d275BF8);
        POSE(0x008111F0u,sizeof pg->d8111F0,pg->d8111F0);
        POSE(0x70003400u,64,pg->spad3400);POSE(0x70003440u,64,pg->spad3440);
        POSE(0x70003600u,16,pg->spad3600);POSE(0x70003760u,44,pg->spad3760);
        POSE(0x70003A3Cu,4,pg->spad3A3C);POSE(0x70003A20u,4,pg->spad3A20);
#undef POSE
    }
    if((p=em_owner_draw_live_memory(a,n)))return p;
    if((p=em_area01_script_bytes(&l->scripts,a,n,write)))return p;
    if((p=em_area01_model_slot_bytes(&l->model,a,n)))return p;
    if(overlaps(a,n,0x0028A56Cu,4)) {
        const uint32_t *library=em_area11_roger_library_word();
        return contains(a,n,0x0028A56Cu,4) && !write && library
            ? (uint8_t *)(void *)library+a-0x0028A56Cu : NULL;
    }
    if(overlaps(a,n,0x0028A574u,4)) {
        const uint32_t *bank=em_area11_roger_door_bank_word();
        return contains(a,n,0x0028A574u,4) && !write && bank
            ? (uint8_t *)(void *)bank+a-0x0028A574u : NULL;
    }
    EmStatusSceneLoader *ld=em_module_loader_state(l->host.loader);
    if(ld && contains(a,n,0x0028A490u,sizeof ld->d28A490))
        return write ? NULL : (uint8_t *)ld->d28A490+a-0x0028A490u;
    if((p=write ? em_rcl_bytes_mut(a,n) : (uint8_t *)(void *)em_rcl_bytes(a,n)))return p;
    if(l->host.bytes && (p=l->host.bytes(l->host.ctx,a,n,write)))return p;
    if(write)return NULL;
    const uint8_t *q=em_collision_world_contact_bytes(a,n);
    if(!q)q=em_effects_live_window(a,n);
    return (uint8_t *)(void *)(q ? q : resource(l,a,n));
}
static uint8_t *bytes(void *ctx,uint32_t a,uint32_t n,int write)
{ return em_area01_live_bytes(ctx,a,n,write); }
static const uint8_t *read_bytes(void *ctx,uint32_t a,uint32_t n)
{ return bytes(ctx,a,n,0); }
static uint8_t *write_bytes(void *ctx,uint32_t a,uint32_t n)
{ return bytes(ctx,a,n,1); }
static void *sdk_bytes(void *ctx,uint32_t a,size_t n,int write)
{
    EmArea01Live *l=ctx;
    void *p=n<=UINT32_MAX ? bytes(ctx,a,(uint32_t)n,write) : NULL;
    if(!p && !l->runtime.fault) {
        l->runtime.fault=1;l->runtime.fault_address=a;
    }
    return p;
}
static const uint8_t *resource(void *ctx,uint32_t a,uint32_t n)
{
    EmArea01Live *l=ctx;
    const uint8_t *p=em_module_loader_memory(l->host.loader,a,n);
    if(p)return p;
    uint32_t size=0;p=em_area11_roger_library_rest(a,&size);
    if(p)return n<=size ? p : NULL;
    p=em_area11_roger_door_bank_rest(a,&size);
    return p && n<=size ? p : NULL;
}
static const uint8_t *resource_rest(void *ctx,uint32_t a,uint32_t *n)
{
    EmArea01Live *l=ctx;
    const uint8_t *p=em_module_loader_memory_rest(l->host.loader,a,n);
    if(p)return p;
    p=em_area11_roger_library_rest(a,n);
    return p ? p : em_area11_roger_door_bank_rest(a,n);
}
static uint32_t random_value(void *ctx) { (void)ctx;return em_random_next(); }
static int model_external(void *ctx,uint32_t fn,uint32_t node,uint32_t arg)
{
    EmArea01Live *l=ctx;
    if(fn==0x001F0120u)return em_area11_bindings_spawn_001F0120(node,(uint8_t)arg);
    if(fn==0x001DA6A0u || fn==0x001CAA00u)return em_area01_model_draw_call(&l->draw,fn,node);
    if(fn==0x001AFC10u) {
        EmActor *a=actor(l,node);
        return a ? em_actor_pool_free_001AFC10(l->host.pool,l->host.scene,a) : -1;
    }
    EmArea01Call c={.function=fn,.a={node,arg},.na=2};
    return l->host.worker ? l->host.worker(l->host.ctx,&c) : -1;
}
static int model_function(uint32_t fn)
{
    switch(fn) {
    case 0x001B1020u: case 0x001C6380u: case 0x001B0FD0u: case 0x001B0EA0u: case 0x001AF890u: case 0x001AF780u:
    case 0x001C6150u: case 0x001CB5B0u: case 0x001B10B0u: case 0x001CA5E0u:
    case 0x001CA6E0u: case 0x001CA6F0u: case 0x001AF800u: case 0x001BA8E0u:
    case 0x001BA580u: case 0x001BA540u: case 0x001CA700u: case 0x001CA770u:
    case 0x001D06D0u: case 0x001D06E0u: case 0x001D0C70u: case 0x001D8BF0u:
    case 0x001C62C0u: case 0x001C63E0u: case 0x001C67E0u: case 0x001C68C0u: case 0x001C69A0u:
    case 0x001C64F0u: case 0x001C2360u: case 0x001C22A0u: case 0x001C5C90u:return 1;
    default:return 0;
    }
}
static int nested(void *ctx,EmArea01Call *c);
typedef struct {
    EmArea01ModelRelease model;
    EmActorBoneRelease previous;
    void *previous_ctx;
} ModelRelease;
static int release_model(void *ctx,EmActor *a)
{
    ModelRelease *release=ctx;
    if(release->model.ready && release->model.actor==a)
        return em_area01_model_release_native(&release->model,a);
    return release->previous ? release->previous(release->previous_ctx,a) : -1;
}
static int worker_result(EmArea01Live *l,int rc,uint32_t address)
{
    /* Retain an adapter's exact failing access without preventing the
     * outer live call from publishing stores that preceded the failure. */
    if(rc<0 && address && !l->runtime.fault) {
        l->runtime.fault=1;l->runtime.fault_address=address;
    }
    return rc;
}
static int worker(void *ctx,EmArea01Call *c);
static int nested(void *ctx,EmArea01Call *c);
/* em_area00_low's translations (oracle: make test-area00-low-reference)
 * over the live composition's checked byte views; their callees re-enter
 * the runtime with the stack pointer the module passes. */
static uint8_t *low_view(void *ctx,uint32_t a,uint32_t n,int write)
{ return em_area01_live_bytes(ctx,a,n,write); }
static int low_call(void *ctx,EmArea00LowCall *in)
{
    EmArea01Call c={.function=in->fn,.sp=in->sp,.na=in->na,.nf=in->nf};
    memcpy(c.a,in->a,sizeof c.a);memcpy(c.f,in->f,sizeof c.f);
    int rc=nested(ctx,&c);in->v0=c.v0;in->f0=c.f0;return rc;
}
/* 0012D580(a0, a1, a2): 00128C10's sub-state machine at a0 +7 (reached
 * through em_area01_exita's 00128C10 state 8). */
static int low_0012D580(EmArea01Live *l,EmArea01Call *c)
{
    if(c->na<3)return worker_result(l,-1,c->function);
    EmArea00Low low;memset(&low,0,sizeof low);
    low.call=low_call;low.ctx=l;low.sp=c->sp;low.view=low_view;low.view_ctx=l;
    int rc=em_area00_low_0012D580(&low,(uint32_t)c->a[0],(uint32_t)c->a[1],c->a[2]);
    return worker_result(l,rc,rc<0 ? (low.fault_address ? low.fault_address : c->function) : 0);
}
/* 001F9100(owner, point, normal, f12) from 001B5360 (em_area00_low): the
 * first level's decal route (em_shadow_actor_route through em_shadow_live)
 * over the three quadwords the original loads (low four address bits
 * ignored). */
static int owner_001F9100(EmArea01Live *l,EmArea01Call *c)
{
    if(c->na<3 || c->nf<1)return worker_result(l,-1,c->function);
    uint32_t q[3][4];
    for(unsigned i=0;i<3;++i) {
        const uint8_t *p=bytes(l,(uint32_t)c->a[i]&~15u,16,0);
        if(!p)return worker_result(l,-1,(uint32_t)c->a[i]);
        memcpy(q[i],p,16);
    }
    if(em_area01_live_suspend(l)<0)return -1;
    int rc=em_shadow_live_owner_001F9100(q[0],q[1],q[2],c->f[0]);
    if(em_area01_live_resume(l)<0)return -1;
    return worker_result(l,rc,rc<0 ? em_shadow_live_fault() : 0);
}
/* 001C9D50(out, a, b, f12) from 00128C10's pose blend (em_area01_math_actor):
 * em_anim_runtime_rest's translation, over the composition's scratch views
 * 0x700034C0..0x700034EF and 0x70003760..0x7000378B; the output words the
 * routine leaves keep their bytes. */
static int rest_001C9D50(EmArea01Live *l,EmArea01Call *c)
{
    if(c->na<3 || c->nf<1)return worker_result(l,-1,c->function);
    const uint32_t o=(uint32_t)c->a[0],x=(uint32_t)c->a[1],y=(uint32_t)c->a[2];
    if(overlaps(o,64,x,64) || overlaps(o,64,y,64))return worker_result(l,-1,c->function);
    uint32_t out[16],a[16],b[16];
    const uint8_t *pa=bytes(l,x,64,0),*pb=bytes(l,y,64,0);
    if(!pa || !pb)return worker_result(l,-1,pa ? y : x);
    memcpy(a,pa,64);memcpy(b,pb,64);
    uint8_t *po=bytes(l,o,64,1);
    uint8_t *s34C0=bytes(l,0x700034C0u,48,1),*s3760=bytes(l,0x70003760u,44,1);
    if(!po || !s34C0 || !s3760)return worker_result(l,-1,!po ? o : !s34C0 ? 0x700034C0u : 0x70003760u);
    memcpy(out,po,64);
    EmAnimRest r;memset(&r,0,sizeof r);
    r.world.spad34C0=(uint32_t *)(void *)s34C0;
    r.world.spad34D0=(uint32_t *)(void *)(s34C0+16);
    r.world.spad34E0=(uint32_t *)(void *)(s34C0+32);
    r.world.spad3760=(uint32_t *)(void *)s3760;
    r.workers.sqrt_ctx=em_collision_world_sdk();
    r.workers.w_0011E748=em_anim_rest_sqrt_0011E748;
    int rc=em_anim_rest_001C9D50(&r,out,a,b,c->f[0]);
    if(rc==0)memcpy(po,out,64);
    return worker_result(l,rc,rc<0 ? (r.fault.address ? r.fault.address : c->function) : 0);
}
/* 001B1630(x, y, z): the first level's camera cone/range owner over the
 * published camera view (D_008105D0 eye, D_00810600 forward). */
static int visible_001B1630(const float p[3])
{
    em_camera_live_view_publish();
    int r=em_area11_interaction_host_visible_001B1630(p);
    em_camera_live_adopt_view();
    return r;
}
typedef struct { EmArea01Live *l; uint32_t node; } Visibility;
static int sdf_001B1630(void *ctx,float x,float y,float z,int32_t *result)
{
    (void)ctx;
    const float p[3]={x,y,z};
    *result=visible_001B1630(p);return 0;
}
static int sdf_001B1B70(void *ctx)
{
    Visibility *v=ctx;
    EmArea01Call c={.function=0x001B1B70u,.a={v->node},.na=1};
    return worker(v->l,&c);
}
static int worker(void *ctx,EmArea01Call *c)
{
    EmArea01Live *l=ctx;
    if(em_area01_rcl_workers_handles(c->function)) {
        uint32_t fault=0;
        int rc=em_area01_rcl_workers_call(c,&fault);
        return worker_result(l,rc,fault);
    }
    if(em_area01_flame_services_handles(c->function)) {
        const EmArea01RuntimeHost host={l,bytes,nested};uint32_t fault=0;
        uint32_t page_start=0;
        if(c->function==0x001F4A10u &&
           em_area01_matrix_service_page_begin(&page_start,&fault)<0)
            return worker_result(l,-1,fault);
        int rc=em_area01_flame_services_call(&host,c,&fault);
        if(rc==0 && c->function==0x001F4A10u)
            rc=em_area01_matrix_service_page_finish(&host,page_start,&fault);
        return worker_result(l,rc,fault);
    }
    if(c->function==0x001C6120u) {
        const uint32_t *library=em_area11_roger_library_word();uint32_t result=0;
        if(c->na!=2 || c->nf || !library || (uint32_t)c->a[0]!=*library)
            return worker_result(l,-1,c->function);
        int rc=em_area11_roger_001C6120((uint32_t)c->a[0],(int32_t)c->a[1],&result);
        if(rc==0)c->v0=(uint64_t)(int64_t)(int32_t)result;
        return worker_result(l,rc,c->function);
    }
    if(c->function==0x001C7900u) {
        const EmArea01RuntimeHost host={l,bytes,nested};uint32_t fault=0;
        EmArea01MatrixService service;
        int rc=em_area01_matrix_service_prepare(&host,c,&service,&fault);
        if(rc<0)return worker_result(l,rc,fault);
        if(em_area01_live_suspend(l)<0)return -1;
        rc=em_area01_matrix_service_invoke(&service,c);
        if(em_area01_live_resume(l)<0)return -1;
        return worker_result(l,rc,c->function);
    }
    if(c->function==0x001CFBE0u) {
        const EmArea01RuntimeHost host={l,bytes,nested};uint32_t fault=0;
        EmArea01FlamePacket packet;
        int rc=em_area01_flame_packet_prepare(&host,c,&packet,&fault);
        if(rc<0)return worker_result(l,rc,fault);
        if(em_area01_live_suspend(l)<0)return -1;
        rc=em_area01_flame_packet_invoke(&packet,c);
        if(em_area01_live_resume(l)<0)return -1;
        return worker_result(l,rc,em_effects_live_fault());
    }
    if(c->function==0x00182BF0u || c->function==0x001B11E0u ||
       c->function==0x001B1EA0u || c->function==0x001C4760u) {
        const EmArea01RuntimeHost host={l,bytes,nested};
        return em_area01_shared_services_call(&host,em_collision_world_sdk(),l->host.scene,c);
    }
    if(em_area01_gun_aux_handles(c->function)) {
        const EmArea01RuntimeHost host={l,bytes,nested};uint32_t fault=0;
        int rc=em_area01_gun_aux_call(&host,em_collision_world_sdk(),c,&fault);
        return worker_result(l,rc,fault);
    }
    if(c->function==0x001C4820u) {
        const EmArea01RuntimeHost host={l,bytes,nested};uint32_t fault=0;
        int rc=em_area01_prop_call(&host,c,&fault);
        return worker_result(l,rc,fault);
    }
    if(em_area01_indicator_handles(c->function)) {
        const EmArea01RuntimeHost host={l,bytes,nested};uint32_t fault=0;
        int rc=em_area01_indicator_call(&host,c,&fault);
        return worker_result(l,rc,fault);
    }
    if(em_area01_light_handles(c->function)) {
        const EmArea01RuntimeHost host={l,bytes,nested};uint32_t fault=0;
        int rc=em_area01_light_call(&host,c,&fault);
        return worker_result(l,rc,fault);
    }
    if(c->function==0x001D7FA0u || c->function==0x001D80B0u ||
       c->function==0x001D3990u || c->function==0x001CAAC0u) {
        const EmArea01RuntimeHost host={l,bytes,nested};EmArea01LightService service;
        if(em_area01_light_service_prepare(&host,c,&service)!=0 ||
           em_area01_live_suspend(l)<0)return -1;
        int rc=em_area01_light_service_invoke(&host,&service,c);
        if(em_area01_live_resume(l)<0)return -1;
        return rc;
    }
    if(c->function==0x00183EF0u || c->function==0x00184BA0u)
        return em_area01_interaction_call(&l->interaction,c);
    if(c->function==0x0018C4B0u || c->function==0x0018C6A0u) {
        const EmArea01RuntimeHost host={l,bytes,nested};
        return em_area01_camera_services_call(&host,c);
    }
    if(c->function==0x0022EC30u || c->function==0x0022EEF0u) {
        const EmArea01RuntimeHost host={l,bytes,nested};uint32_t fault=0;
        int rc=em_area01_timeline_call(&host,c,&fault);
        return worker_result(l,rc,fault);
    }
    if(c->function==0x001FBD50u || c->function==0x001FBF50u || c->function==0x001B1380u ||
       c->function==0x001FC3C0u || c->function==0x001FC520u) {
        const EmArea01RuntimeHost host={l,bytes,nested};
        return em_area01_audio_services_call(&host,em_collision_world_sdk(),
                                              em_stream_live_output_mode(),c);
    }
    if(em_area01_script_worker_handles(c->function)) {
        const EmArea01RuntimeHost host={l,bytes,nested};uint32_t fault=0;
        int rc=em_area01_script_worker_call(&host,c,&fault);
        return worker_result(l,rc,fault);
    }
    if(c->function==0x0012D580u)return low_0012D580(l,c);
    if(c->function==0x001F9100u)return owner_001F9100(l,c);
    if(c->function==0x001C9D50u)return rest_001C9D50(l,c);
    if(em_area01_door_handles(c->function))return em_area01_door_call(&l->door,c);
    if(em_area01_pickup_handles(c->function))return em_area01_pickup_call(&l->pickups,c);
    if(c->function==0x001EFD90u || c->function==0x001EFD20u || c->function==0x001EF9D0u) {
        EmArea01EffectsSpawn spawn;
        const EmArea01RuntimeHost host={l,bytes,worker};
        if(em_area01_effects_services_prepare(&host,c,&spawn)<0 || em_area01_live_suspend(l)<0)return -1;
        int rc=em_area01_effects_services_invoke(&spawn,c);
        if(em_area01_live_resume(l)<0)return -1;
        return rc;
    }
    if(c->na<=7) {
        EmAimFireTargetCall s={.function=c->function,.sp=c->sp,.na=c->na,.nf=c->nf};
        memcpy(s.a,c->a,c->na*sizeof c->a[0]);memcpy(s.f,c->f,c->nf*sizeof c->f[0]);
        int rc=em_aim_fire_sdk_memory_call(l,sdk_bytes,&s);
        if(rc!=1) { c->v0=s.v0;c->f0=s.f0;return rc; }
    }
    if(model_function(c->function)) {
        /* Model worker brackets actor views itself; the player and camera
         * must also publish before its native head/shadow callbacks. */
        if(em_area01_collision_view_commit(&l->collision)<0 || em_area01_player_view_commit(&l->player)<0)return -1;
        em_camera_live_view_publish();
        uint32_t result;
        int rc=em_area01_model_call(&l->model,c->function,c->a[0],c->a[1],c->a[2],c->a[3],
                                     em_ee_float(c->f[0]),em_ee_float(c->f[1]),&result);
        em_camera_live_adopt_view();
        if(player_begin(l)<0 || em_area01_collision_view_begin(&l->collision)<0)return -1;
        c->v0=(uint64_t)(int64_t)(int32_t)result;return rc;
    }
    if(c->function==0x001CAA00u || c->function==0x001CACB0u ||
       c->function==0x001CB360u || c->function==0x001DA6A0u) {
        if(c->na<1 || em_area01_collision_view_commit(&l->collision)<0 || em_area01_player_view_commit(&l->player)<0)return -1;
        em_camera_live_view_publish();
        int rc=em_area01_model_draw_call(&l->draw,c->function,(uint32_t)c->a[0]);
        em_camera_live_adopt_view();
        if(player_begin(l)<0 || em_area01_collision_view_begin(&l->collision)<0)return -1;
        c->v0=0;return rc;
    }
    EmSdkMathContext *sdk=em_collision_world_sdk();float out;
    switch(c->function) {
    case 0x001B1240u: {
        if(c->na!=1 || c->nf!=2 || !sdk)return -1;
        const uint8_t *origin=bytes(l,(uint32_t)c->a[0],12,0);
        if(!origin)return -1;
        uint32_t xyz[3];memcpy(xyz,origin,sizeof xyz);
        EmScriptHostWorkers h={0};h.world.sdk_tables=sdk->tables;
        h.world.sdk_world=&sdk->world;h.world.sdk_workers=&sdk->workers;
        int rc=em_script_host_001B1240(&h,xyz,c->f[0],c->f[1],&c->f0);
        return worker_result(l,rc,h.fault_address);
    }
    case 0x001B1630u: {
        if(c->nf<3)return -1;
        float p[3];memcpy(p,c->f,sizeof p);
        c->v0=(uint64_t)(int64_t)visible_001B1630(p);return 0;
    }
    case 0x001B1B30u: {
        /* em_sdf_001B1B30 (the first level's door owner) stores 001B1630's
         * byte straight into the actor's +0x01 before 001B1B70 runs; the
         * result is that byte read back. */
        if(c->na<1 || c->nf<3)return -1;
        Visibility v={l,(uint32_t)c->a[0]};
        uint8_t *visible=bytes(l,v.node+1u,1,1);
        if(!visible)return -1;
        EmSdfWorkers w;memset(&w,0,sizeof w);
        w.ctx=&v;w.w_001B1630=sdf_001B1630;w.w_001B1B70=sdf_001B1B70;
        EmSdfFault fault={0,0};
        float p[3];memcpy(p,c->f,sizeof p);
        if(em_sdf_001B1B30(visible,p[0],p[1],p[2],&w,&fault)<0)
            return worker_result(l,-1,fault.address ? fault.address : c->function);
        const uint8_t *after=bytes(l,v.node+1u,1,0);
        if(!after)return -1;
        c->v0=*after;return 0;
    }
    case 0x001B1B70u: {
        if(c->na!=1 || em_area01_actor_view_touch(&l->actors,(uint32_t)c->a[0])<0 ||
           em_area01_live_suspend(l)<0)return -1;
        int rc=em_area01_shared_services_publish(l->host.pool,c);
        if(em_area01_live_resume(l)<0)return -1;
        return rc;
    }
    case 0x001BA1C0u: {
        if(c->na<2 || c->a[1]>UINT32_MAX-0x00810758u ||
           !bytes(l,0x00810758u+(uint32_t)c->a[1],1,0))return -1;
        EmGunWorld world={0};EmGunFault fault={0};int done=0;
        world.d810758=bytes(l,0x00810758u,1,0);
        int rc=em_gun_flag_done_001BA1C0(&world,(unsigned)c->a[1],&fault,&done);
        c->v0=(uint32_t)done;return rc;
    }
    case 0x0021BAB0u: {
        const uint8_t *context=em_rcl_bytes(EM_RCL_CONTEXT,EM_RCL_CONTEXT_SIZE);
        return context ? em_sul_0021BAB0(context,EM_RCL_CONTEXT_SIZE,&c->v0) : -1;
    }
    case 0x001AEDE0u: case 0x001AEE10u: case 0x001B1E20u:
    case 0x001B0250u: case 0x0021B9A0u: case 0x001D2830u:
    case 0x001DD980u: case 0x001D25F0u: case 0x001FB9F0u: {
        /* The byte views have the last original writes. Publish before
         * existing native frame, camera, render and sound services run. */
        if((c->function==0x001AEDE0u || c->function==0x001AEE10u ||
            c->function==0x001B1E20u || c->function==0x001D2830u ||
            c->function==0x001DD980u) && c->na<2)return -1;
        if(c->function==0x0021B9A0u && (c->na<1 || c->nf<2))return -1;
        if(c->function==0x001D25F0u && c->nf<1)return -1;
        if(c->function==0x001DD980u && (c->a[0]!=0x008105D0u || c->a[1]!=0x008105E0u))return -1;
        if(c->function==0x001FB9F0u && (c->na<4 || c->a[1]!=0x1000u))return -1;
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=0;
        switch(c->function) {
        case 0x001AEDE0u: case 0x001AEE10u:
            em_frame_fade_start_colour(c->function==0x001AEDE0u ? 1 : -1,
                                      (int16_t)c->a[0],(uint8_t)c->a[1]);break;
        case 0x001B1E20u:rc=em_pad_actuator_001B1E20((int32_t)c->a[0],(int64_t)c->a[1]);break;
        case 0x001B0250u:rc=em_scene_bindings_001B0250();break;
        case 0x0021B9A0u:rc=em_rcl_0021B9A0((int32_t)c->a[0],c->f[0],c->f[1]);break;
        case 0x001D2830u:rc=em_rcl_001D2830((int32_t)c->a[0],(int32_t)c->a[1]);break;
        case 0x001DD980u:rc=em_area11_interaction_host_camera_publish()==1 ? 0 : -1;break;
        case 0x001D25F0u:rc=em_rcl_001D25F0(c->f[0]);break;
        case 0x001FB9F0u:
            c->v0=(uint64_t)(int64_t)em_sfx_submit_001FB9F0_track((unsigned)c->a[0],
                                                               (int32_t)c->a[2],(int32_t)c->a[3]);break;
        }
        if(em_area01_live_resume(l)<0)return -1;
        return rc;
    }
    case 0x0019A570u: case 0x0019A910u: case 0x0019B6C0u: case 0x0019AB20u:
    case 0x0019F1A0u: case 0x0019ED80u: case 0x001A4030u: case 0x001A50A0u:
    case 0x001A5C30u: {
        /* Native lists need committed actors. The collision adapter then
         * reads vector operands through fresh actor/player byte views. */
        if(em_area01_live_suspend(l)<0)return -1;
        em_camera_live_adopt_view();
        if(em_area01_actor_view_begin(&l->actors)<0 || player_begin(l)<0)return -1;
        l->active=1;
        int rc=em_area01_collision_view_call(&l->collision,c);
        if(em_area01_collision_view_begin(&l->collision)<0)return fail(l,l->collision.fault_address);
        return rc;
    }
    case 0x001BA1A0u: case 0x001BA1F0u: {
        if(c->na<(c->function==0x001BA1A0u ? 2u : 1u))return -1;
        uint32_t at=(uint32_t)c->a[0];
        if(c->function==0x001BA1A0u)at-=0x1F0u;
        EmActor *a=actor(l,at);
        if(!a || em_area01_actor_view_touch(&l->actors,at)<0 || em_area01_live_suspend(l)<0)return -1;
        int32_t result=0;
        int rc=c->function==0x001BA1A0u ? em_area11_script_host_start(a,(uint32_t)c->a[1])
                                            : em_area11_script_host_tick(a,&result);
        if(em_area01_live_resume(l)<0)return -1;
        c->v0=(uint64_t)(int64_t)result;return rc;
    }
    case 0x001B7F90u: {
        if(c->na<3 || !sdk || !sdk->tables)return -1;
        const uint8_t *record=bytes(l,(uint32_t)c->a[2],0x28,0);
        if(!record)return -1;
        int32_t kind;memcpy(&kind,record+8,4);
        if(kind!=1) { c->v0=0;return 0; }
        float step,position[3],owner[3],yaw;
        memcpy(&step,record+0x24,4);
        const uint8_t *p=bytes(l,EM_AREA01_PLAYER_BASE+0xA0,12,0);
        const uint8_t *o=bytes(l,(uint32_t)c->a[0]+0xB0,12,0);
        uint8_t *y=bytes(l,EM_AREA01_PLAYER_BASE+0xC4,4,1);
        if(!p || !o || !y)return -1;
        memcpy(position,p,12);memcpy(owner,o,12);memcpy(&yaw,y,4);
        /* The existing helper's numerical domain excludes the SDK
         * zero-vector diagnostic path; refuse it at that boundary. */
        if(owner[0]==position[0] && owner[2]==position[2])return -1;
        EmInteractionMath math;
        memcpy(math.atan_high,sdk->tables->atan_hi,sizeof math.atan_high);
        memcpy(math.atan_low,sdk->tables->atan_lo,sizeof math.atan_low);
        memcpy(math.atan_coefficients,sdk->tables->atan_t,sizeof math.atan_coefficients);
        int rc=em_pickup_turn(&math,position,&yaw,owner,step);
        if(rc<0)return -1;
        memcpy(y,&yaw,4);c->v0=(uint32_t)rc;return 0;
    }
    case 0x001B1190u: {
        if(c->na<1)return -1;
        const EmGunRestMem mem={l,read_bytes,write_bytes};EmGunFault fault={0,0};
        int rc=em_gun_rest_001B1190((int32_t)c->a[0],&mem,&fault);
        c->v0=0;return rc;
    }
    case 0x001C40B0u: {
        if(c->na<2 || em_area01_live_suspend(l)<0)return -1;
        int rc=em_pickup_inventory_001C40B0((int32_t)c->a[0],(int32_t)c->a[1]);
        if(em_area01_live_resume(l)<0)return -1;
        c->v0=0;return rc;
    }
    case 0x001A2370u: case 0x001C5570u: {
        if(c->na<(c->function==0x001C5570u ? 4u : 2u))return -1;
        EmActor *a=actor(l,(uint32_t)c->a[0]);float data[16];
        unsigned size=c->function==0x001C5570u ? 16u : 64u;
        const uint8_t *p=bytes(l,(uint32_t)c->a[1],size,0);
        if(!a || !p || em_area01_actor_view_touch(&l->actors,(uint32_t)c->a[0])<0)return -1;
        memcpy(data,p,size);
        if(em_area01_live_suspend(l)<0)return -1;
        uint32_t result=0;
        int rc=c->function==0x001C5570u ?
            em_area11_bindings_spawn_001C5570(a,data,(uint8_t)c->a[2],(int)c->a[3],&result) :
            em_collision_world_retransform_001A2370(a,data);
        if(em_area01_live_resume(l)<0)return -1;
        c->v0=result;return rc<0 ? -1 : 0;
    }
    case 0x00122BB8u:c->v0=em_random_next();return 0;
    case 0x001B12B0u: {
        /* The first level's approach step (em_script_host_workers). */
        uint32_t out=0;
        if(c->nf<3)return worker_result(l,-1,c->function);
        int rc=em_script_host_approach(NULL,c->f[0],c->f[1],c->f[2],&out);
        if(rc==0)c->f0=out;
        return worker_result(l,rc,c->function);
    }
    case 0x001281C0u:
        if(c->nf<1)return -1;
        c->v0=(uint64_t)(int64_t)em_stream_lanes_001281C0(c->f[0]);return 0;
    case 0x001B1470u:
        return c->nf<1 ? -1 : em_player_recovery_wrap(c->f[0],&c->f0);
    case 0x0011DF78u:
        if(c->nf<1)return -1;
        c->f0=em_ee_bits(em_sdk_math_original_0011DF78(em_ee_float(c->f[0])));return 0;
    case 0x0011E2A8u: case 0x0011DE90u: case 0x0011E398u:
    case 0x0011DBB8u: case 0x0011E620u: case 0x0011E748u:
        if(!sdk || c->nf<(c->function==0x0011E620u ? 2u : 1u))return -1;
        if(c->function==0x0011E2A8u)out=em_sdk_math_original_float_0011E2A8(sdk,em_ee_float(c->f[0]));
        else if(c->function==0x0011DE90u)out=em_sdk_math_original_float_0011DE90(sdk,em_ee_float(c->f[0]));
        else if(c->function==0x0011E398u)out=em_sdk_math_original_float_0011E398(sdk,em_ee_float(c->f[0]));
        else if(c->function==0x0011DBB8u)out=em_sdk_math_original_float_0011DBB8(sdk,em_ee_float(c->f[0]));
        else if(c->function==0x0011E748u)out=em_sdk_math_original_float_0011E748(sdk,em_ee_float(c->f[0]));
        else out=em_sdk_math_original_float_0011E620(sdk,em_ee_float(c->f[0]),em_ee_float(c->f[1]));
        if(sdk->fault)return -1;
        c->f0=em_ee_bits(out);return 0;
    case 0x001AFA90u: case 0x001AFC10u: case 0x001B17A0u: {
        if(c->na<1)return -1;
        EmActor *a=actor(l,c->a[0]);
        if(a && em_area01_actor_view_touch(&l->actors,c->a[0])<0)return -1;
        ModelRelease release={0};
        if(c->function==0x001AFC10u && a && a->bones && l->host.private_model &&
           l->host.private_model(l->host.ctx,a) && !em_area11_boxes_owner_view(a)) {
            if(em_area01_model_release_prepare(&l->model,(uint32_t)c->a[0],&release.model)<0)return -1;
            release.previous=l->host.pool->w_001AF800;
            release.previous_ctx=l->host.pool->worker_ctx;
        }
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=0;
        if(c->function==0x001AFA90u) {
            EmActor *p=em_actor_pool_alloc_001AFA90(l->host.pool,l->host.scene,(uint8_t)c->a[0]);
            c->v0=em_actor_pool_address(l->host.pool,p);
        } else if(!a)rc=-1;
        else if(c->function==0x001AFC10u) {
            if(release.model.ready) {
                l->host.pool->w_001AF800=release_model;l->host.pool->worker_ctx=&release;
                rc=em_actor_pool_free_001AFC10(l->host.pool,l->host.scene,a);
                l->host.pool->w_001AF800=release.previous;l->host.pool->worker_ctx=release.previous_ctx;
            } else rc=em_actor_pool_free_001AFC10(l->host.pool,l->host.scene,a);
        }
        else {
            EmOwnerServicesOwner v={0};v.cls=a->cls;v.kind=a->model;v.model_id=a->param;v.flags2=a->flags2;
            memcpy(v.pos,a->pos,sizeof v.pos);
            rc=em_area11_interaction_host_offer_001B17A0(a,&v);c->v0=a->drawn;
        }
        if(em_area01_live_resume(l)<0)return -1;
        return rc<0 ? -1 : 0;
    }
    default:return l->host.worker ? l->host.worker(l->host.ctx,c) : -1;
    }
}
static int nested(void *ctx,EmArea01Call *c)
{ return em_area01_runtime_call(&((EmArea01Live *)ctx)->runtime,c); }
static int interaction_claim(void *ctx,uint32_t node)
{
    EmArea01Live *l=ctx;EmActor *a=actor(l,node);
    if(!a || em_area01_actor_view_touch(&l->actors,node)<0 || em_area01_live_suspend(l)<0)return -1;
    int rc=em_area11_interaction_host_claim_scan(a);
    if(em_area01_live_resume(l)<0)return -1;
    return rc;
}
static int pickup_aura_draw(void *ctx,uint32_t node,uint32_t record,uint32_t angle,uint32_t timer)
{
    EmArea01Live *l=ctx;float world[16];
    const uint8_t *p=bytes(l,node+0xD0,sizeof world,0);
    if(!p)return -1;
    memcpy(world,p,sizeof world);
    if(em_area01_live_suspend(l)<0)return -1;
    int rc=em_effects_live_aura_draw(world,record,angle,timer);
    if(em_area01_live_resume(l)<0)return -1;
    return rc;
}
static int script_worker(void *ctx,EmArea01Call *c)
{
    EmArea01Live *l=ctx;
    if(em_area01_live_resume(l)<0)return -1;
    if(!c->sp) {
        /* Native interpreter callbacks may nest under an owner. Its
         * native frames use C locals; reserve a separate logical slice
         * below the pending translated caller, never overwrite its locals. */
        uint32_t parent=l->runtime.sp ? l->runtime.sp : LOCAL_BASE+sizeof l->locals-16;
        if(parent<LOCAL_BASE+0x100u)return fail(l,parent);
        c->sp=parent-0x100u;
    }
    int rc=em_area01_runtime_call(&l->runtime,c);
    if(em_area01_live_suspend(l)<0)return -1;
    return rc;
}
void em_area01_live_detach(EmArea01Live *l)
{
    if(!l)return;
    em_area01_script_free(&l->scripts);
    l->bound=0;l->active=0;
}
static int script_bank(void *ctx,EmActor *a,uint32_t *value,int write)
{
    EmArea01Live *l=ctx;
    uint32_t address=em_actor_pool_address(l->host.pool,a);
    if(!address || !value || l->active)return -1;
    if(!write)return em_area01_actor_view_snapshot(&l->actors,address+0x40,4,value);
    if(em_area01_live_resume(l)<0)return -1;
    uint8_t *p=bytes(l,address+0x40,4,1);
    if(p)memcpy(p,value,4);
    if(em_area01_live_suspend(l)<0)return -1;
    return p ? 0 : -1;
}
static uint64_t resource_epoch(void *ctx)
{ return *((EmArea01Live *)ctx)->host.resource_epoch; }
int em_area01_live_bind(EmArea01Live *l,const EmArea01LiveHost *h)
{
    if(!l || !h || !h->pool || !h->scene || !h->area || !h->loader || !h->current_actor ||
       !h->resource_epoch || !h->rebind)
        return -1;
    memset(l,0,sizeof *l);l->host=*h;
    EmPoseHost *pose=player_pose_record_host();
    EmStatusSceneLoader *ld=em_module_loader_state(h->loader);
    uint32_t *bones=em_aim_fire_binding_bytes(0x00275B40u,4,1);
    if(!pose || !pose->globals || !ld || !bones)return fail(l,0x001CB5B0u);
    /* Shared scratch is installed at state 0 before the first camera and
     * player calls, so no last-writer handoff occurs inside the pool walk. */
    em_area01_actor_view_reset(&l->actors,h->pool,project,rebind,l);
    l->actors.written=em_area01_effects_services_written;
    player_states_external_view_init(&l->player,h->pool);
    EmArea01ModelSource s={0};s.ctx=l;s.resource=resource;s.resource_rest=resource_rest;
    s.table=ld->d28A490;s.table_words=EM_STATUS_SCENE_RELOC_WORDS;
    s.library_word=em_area11_roger_library_word();
    if(!s.library_word || !*s.library_word)return fail(l,0x0028A56Cu);
    s.current_actor=h->current_actor;s.current_bones=bones;s.pose_globals=pose->globals;
    s.scratch3480=(uint32_t *)(void *)em_owner_draw_live_memory(0x70003480u,64);
    s.d8106F1=em_area01_scene_view(h->scene,0x008106F1u,1);
    s.d810707=em_area01_scene_view(h->scene,0x00810707u,1);
    s.d810CB6=em_area01_scene_view(h->scene,0x00810CB6u,1);
    s.d810758=em_area01_scene_view(h->scene,0x00810758u,1);
    s.d810788=em_area01_scene_view(h->scene,0x00810788u,1);
    s.d810700=&h->scene->d810700;s.d8106D4=em_area01_scene_view(h->scene,0x008106D4u,1);
    s.worker=model_external;s.random=random_value;
    const EmArea01RuntimeHost runtime={l,bytes,worker};
    if(em_area01_model_bind(&l->model,&l->actors,&s)<0 || em_area01_runtime_bind(&l->runtime,&runtime)<0)
        return fail(l,0x001AFCA0u);
    em_area01_model_draw_bind(&l->draw,&l->model);
    const EmArea01RuntimeHost door={l,bytes,nested};
    if(em_area01_door_bind(&l->door,&door)<0)return fail(l,0x001BC350u);
    if(em_area01_pickup_bind(&l->pickups,&door)<0)return fail(l,0x00219550u);
    em_area01_pickup_set_aura_draw(&l->pickups,pickup_aura_draw,l);
    if(em_area01_interaction_bind(&l->interaction,&door,em_area11_interaction_host_math(),
                                 interaction_claim,l)<0)return fail(l,0x00184BA0u);
    EmArea01CollisionHost collision={0};collision.ctx=l;collision.pool=h->pool;
    collision.player=player_states_actor();collision.shared=em_aim_fire_runtime_world_state();
    collision.segment=em_collision_world_segment();collision.move=em_collision_world_move_scratch();
    collision.bytes=bytes;collision.generation=resource_epoch;
    collision.file.address=ld->d28A490[0x42];collision.file.d28A5A8=ld->d28A490[0x46];
    collision.file.bytes=em_module_loader_memory_rest(h->loader,collision.file.address,&collision.file.size);
    const uint8_t *count=em_module_loader_memory(h->loader,collision.file.d28A5A8,2);
    if(!count)return fail(l,0x0028A5A8u);
    memcpy(&collision.file.d28A5A8_value,count,2);
    if(em_area01_collision_view_bind(&l->collision,&collision)<0)return fail(l,l->collision.fault_address);
    const EmArea01RuntimeHost script={l,bytes,script_worker};
    if(em_area01_script_bind(&l->scripts,&script,h->loader,NULL,NULL)<0)return fail(l,0x001BA1A0u);
    em_area01_script_bank(&l->scripts,l,script_bank);
    l->bound=1;return 0;
}
int em_area01_live_call_active(EmArea01Live *l,EmArea01Call *c)
{
    if(!l || !c || !l->bound || !l->active || l->fault)return -1;
    if(!c->sp)c->sp=LOCAL_BASE+sizeof l->locals-16;
    return em_area01_runtime_call(&l->runtime,c);
}
int em_area01_live_call(EmArea01Live *l,EmArea01Call *c)
{
    if(!l || !c || em_area01_live_resume(l)<0)return -1;
    int rc=em_area01_live_call_active(l,c);
    /* Keep original writes preceding a missing worker; never roll back. */
    if(em_area01_live_suspend(l)<0)return -1;
    if(rc<0)return fail(l,l->runtime.fault_address);
    return 0;
}
int em_area01_live_player_call(EmArea01Live *l,EmPlayerLiveActor *p,EmArea01Call *c)
{
    if(!l || !l->bound || l->active || l->player_stage || p!=l->player.actor || !c)return -1;
    l->player_stage=1;
    int rc=em_area01_live_call(l,c);
    l->player_stage=0;
    return rc;
}
int em_area01_live_scan(EmArea01Live *l,EmPlayerLiveActor *p,int *result)
{
    if(!result)return -1;
    EmArea01Call c={.function=0x00184BA0u,.a={EM_AREA01_PLAYER_BASE},.na=1};
    int rc=em_area01_live_player_call(l,p,&c);
    if(rc>=0)*result=(int32_t)c.v0;
    return rc;
}
