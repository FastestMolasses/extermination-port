#include "game/em_area01_live.h"
#include "game/em_render_verify_rest.h"
#include "game/em_sdk_soft_float.h"
#include "game/em_player_closure_live.h"
#include "game/em_pose_host_workers.h"
#include "game/em_message_live.h"
#include "game/em_area01_scene_view.h"
#include "game/em_area01_script_workers.h"
#include "game/em_area01_transition_services.h"
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
#include "game/em_render_verify_rest.h"
#include "game/em_anim_runtime_rest.h"
#include "game/em_shadow_live.h"
#include "game/em_area11_bindings.h"
#include "game/em_area11_roger.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_aim_fire_binding.h"
#include "game/em_aim_fire_world_live.h"
#include "game/em_aim_fire_runtime.h"
#include "game/em_area00_fx.h"
#include "game/em_aim_fire_flash.h"
#include "game/em_aim_fire_sdk_memory.h"
#include "game/em_camera_live.h"
#include "game/em_collision_world.h"
#include "game/em_actor_collision.h"
#include "game/em_player_floor.h"
#include "game/em_effects_live.h"
#include "game/em_area01_effects_services.h"
#include "game/em_pickup.h"
#include "game/em_pickup_motion.h"
#include "game/em_security_gun_rest.h"
#include "game/em_security_gun.h"
#include "game/em_player.h"
#include "game/em_game_internal.h"
#include "game/em_owner_draw_live.h"
#include "game/em_random.h"
#include "game/em_render_context_live.h"
#include "game/em_script_host_workers.h"
#include "game/em_script_door_fan.h"
#include "game/em_player_recovery.h"
#include "game/em_player_stage_workers.h"
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
/* D_00822CF0..D_008234C0: the boot BSS block only 0022DCD0 uses (decomp
 * NEARMISS C; no other decomp function names an address in it): its
 * 64-byte matrices at 0x00822CF0, the colour rows at 0x008230F0, eleven
 * word tables up to 0x008233F0 + 64, and the 0x90-byte request record at
 * 0x00823430 that 001D0400 rewrites. Boot BSS starts zero and is never
 * reset by an area change, so it is one process-lifetime block. */
static EmPlayerFloorTable s_column;
#define D822CF0_BASE 0x00822CF0u
#define D822CF0_SIZE 0x7D0u
static uint8_t s_d822CF0[D822CF0_SIZE];
/* D_002668A0: the boot data block 0021A180 (AREA06's strip owner 00219870)
 * hands 001CFBE0 as its 0x90-byte source, after storing its +0x08 length
 * (D_002668A8) and +0x80 (D_00266920 = 0x40). Boot data: the ELF's bytes
 * (export_area00_runtime_tables.py MID_WINDOWS) at the first access, then
 * one process-lifetime block, as the original's .data is. */
#define D2668A0_BASE 0x002668A0u
#define D2668A0_SIZE 0x90u
static uint8_t s_d2668A0[D2668A0_SIZE];
static int s_d2668A0_ready;
static uint8_t *d2668A0(void)
{
    if(!s_d2668A0_ready) {
        const uint8_t *elf=em_effects_live_window(D2668A0_BASE,D2668A0_SIZE);
        if(!elf)return NULL;
        memcpy(s_d2668A0,elf,D2668A0_SIZE);s_d2668A0_ready=1;
    }
    return s_d2668A0;
}
/* D_0081F970..D_0081FE6F: the boot BSS of the infection node 001F8350
 * (em_area06_port, effect 0x80000048) only: its 64 ages D_0081F970 and its
 * 64-slot ring D_0081FA70 (no other decomp function names an address in
 * it). Boot BSS starts zero and an area change never resets it: one
 * process-lifetime block. */
#define D81F970_BASE 0x0081F970u
#define D81F970_SIZE 0x500u
static uint8_t s_d81F970[D81F970_SIZE];
/* D_00275CA8: the word AREA13's [45] 0x827150 publishes its block in each
 * frame and its step functions read (only AREA13's overlay names it). Boot
 * BSS, zero until then and never reset. */
#define D275CA8_BASE 0x00275CA8u
static uint32_t s_d275CA8;
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
    /* 0019BC40's column table as the creature family's 001B30E0 reads it
     * (the count 0x700031E0, the halfword flags D_70003170, the heights
     * D_700030F0, the slopes D_00282250; 16 entries, the table's limit),
     * read-only here: the worker 0019BC40 below is its writer. */
    if(overlaps(a,n,0x700031E0u,4))
        return !write && contains(a,n,0x700031E0u,4) ? (uint8_t *)&s_column.count : NULL;
    if(overlaps(a,n,0x70003170u,32))
        return !write && contains(a,n,0x70003170u,32) ? (uint8_t *)s_column.flags+(a-0x70003170u) : NULL;
    if(overlaps(a,n,0x700030F0u,64))
        return !write && contains(a,n,0x700030F0u,64) ? (uint8_t *)s_column.height+(a-0x700030F0u) : NULL;
    if(overlaps(a,n,0x00282250u,64))
        return !write && contains(a,n,0x00282250u,64) ? (uint8_t *)s_column.aux+(a-0x00282250u) : NULL;
    if(overlaps(a,n,0x70003B64u,4))
        return !write && contains(a,n,0x70003B64u,4)
            ? (uint8_t *)(void *)em_frame_counter_storage()+((size_t)a-0x70003B64u) : NULL;
    if(overlaps(a,n,EM_ACTOR_POOL_BASE,EM_ACTOR_RECORD_SIZE*EM_ACTOR_POOL_CAPACITY)) {
        /* A muzzle node's +0x28 / +0xD0..+0x10F / +0x230.. are
         * em_aim_fire_flash's (their one home; a read needs a prior write). */
        if((p=em_aim_fire_flash_field(a,n,write)))return p;
        return em_area01_actor_view_bytes(&l->actors,a,n,write);
    }
    if(overlaps(a,n,EM_AREA01_PLAYER_BASE,EM_PLAYER_ACTOR_SIZE))
        return em_area01_player_view_bytes(&l->player,a,n,write);
    if(overlaps(a,n,LOCAL_BASE,sizeof l->locals))
        return contains(a,n,LOCAL_BASE,sizeof l->locals) ? l->locals+a-LOCAL_BASE : NULL;
    if(overlaps(a,n,0x70003000u,sizeof l->scratch_3000))
        return em_area01_live_matrix_3000(l,a,n);
    if(overlaps(a,n,0x700034C0u,sizeof l->scratch_34C0))
        return l->active && contains(a,n,0x700034C0u,sizeof l->scratch_34C0)
            ? (uint8_t *)l->scratch_34C0+a-0x700034C0u : NULL;
    if(overlaps(a,n,0x70003A24u,sizeof l->scratch_3A24))
        return l->active && contains(a,n,0x70003A24u,sizeof l->scratch_3A24)
            ? (uint8_t *)l->scratch_3A24+a-0x70003A24u : NULL;
    if(overlaps(a,n,0x700036E0u,sizeof l->scratch_36E0))
        return l->active && contains(a,n,0x700036E0u,sizeof l->scratch_36E0)
            ? (uint8_t *)l->scratch_36E0+a-0x700036E0u : NULL;
    if(overlaps(a,n,0x002821B0u,(uint32_t)sizeof(EmMessageBlock))) {
        /* D_002821B0: the live message service's request block, the one
         * storage its original writers use (the door program's op09
         * 001BBAE0 stores +0..+0xC and polls +4, as on the first level). */
        EmMessageBlock *block=em_message_live_block();
        return block && contains(a,n,0x002821B0u,(uint32_t)sizeof *block)
            ? (uint8_t *)(void *)block+a-0x002821B0u : NULL;
    }
    if(overlaps(a,n,0x00281F30u,(uint32_t)sizeof(int32_t[EM_SLG_CUES][4]))) {
        /* D_00281F30: the ten cue records {delay, cue, gain L, gain R} of
         * em_stream_live (001FB100's storage; step H's 001FC6E0 plays from
         * them). 001FC580 (em_area00_low, the creatures' and boxes' cue
         * request) reads and writes the record its id selects. */
        int32_t (*cues)[4]=em_stream_live_d281F30();
        return cues && contains(a,n,0x00281F30u,(uint32_t)sizeof(int32_t[EM_SLG_CUES][4]))
            ? (uint8_t *)cues+(a-0x00281F30u) : NULL;
    }
    if(overlaps(a,n,0x00282154u,3)) {
        /* D_00282154..56: the stream lanes' active bytes (em_stream_live's,
         * read only here: switch [31]'s 0x823980 waits on lane 0's before
         * its 001FAE70(0)). */
        const int8_t *active=em_stream_live_active_bytes();
        return !write && active && contains(a,n,0x00282154u,3)
            ? (uint8_t *)(uintptr_t)active+(a-0x00282154u) : NULL;
    }
    if(overlaps(a,n,0x008112C0u,0xA00u)) {
        /* D_008112C0..D_00811CBF: the clipper's stage (em_shadow_live's). */
        uint8_t *stage=em_shadow_live_stage_bytes();
        return stage && contains(a,n,0x008112C0u,0xA00u) ? stage+(a-0x008112C0u) : NULL;
    }
    if(overlaps(a,n,D275CA8_BASE,4))
        return contains(a,n,D275CA8_BASE,4) ? (uint8_t *)&s_d275CA8+(a-D275CA8_BASE) : NULL;
    if(overlaps(a,n,D822CF0_BASE,D822CF0_SIZE))
        return contains(a,n,D822CF0_BASE,D822CF0_SIZE) ? s_d822CF0+(a-D822CF0_BASE) : NULL;
    if(overlaps(a,n,D81F970_BASE,D81F970_SIZE))
        return contains(a,n,D81F970_BASE,D81F970_SIZE) ? s_d81F970+(a-D81F970_BASE) : NULL;
    if(overlaps(a,n,D2668A0_BASE,D2668A0_SIZE))
        return contains(a,n,D2668A0_BASE,D2668A0_SIZE) && d2668A0() ? s_d2668A0+(a-D2668A0_BASE) : NULL;
    /* D_00275CA0: the car's side AREA03's lift scripts toggle (0x826860, a
     * boot word the owners 0x825430 / 0x8256E0 read; its ride callback
     * 0x826270). */
    if(overlaps(a,n,0x00275CA0u,8)) {
        static uint32_t d275CA0[2];   /* and D_00275CA4, the ride's second word */
        return contains(a,n,0x00275CA0u,8) ? (uint8_t *)d275CA0+a-0x00275CA0u : NULL;
    }
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
    if(overlaps(a,n,0x0028A9A0u,8)) {
        /* D_0028A9A0 (D_0028A8E0 + 0xC0): 001AEE70's transition fields,
         * held by their one owner em_frame_transition(); read-only here
         * (00184BA0's Use-scan gate, 00131B10's colour byte +2). */
        const EmTransitionFade *fade=em_frame_transition();
        return !write && fade && contains(a,n,0x0028A9A0u,8)
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
    {
        /* The particle records D_007709C0 and D_00275C40 / D_00275C44:
         * em_effects_live's storage (0x8237D0's 001F4010 seeds a slot). */
        EmEffectsLiveNodeRegion particles[3];
        const size_t np=em_effects_live_particle_regions(particles,3);
        for(size_t k=0;k<np && k<3;++k)
            if(overlaps(a,n,particles[k].address,(uint32_t)particles[k].size))
                return contains(a,n,particles[k].address,(uint32_t)particles[k].size)
                    ? (uint8_t *)particles[k].bytes+a-particles[k].address : NULL;
    }
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
    if((p=em_effects_live_window_mutable(a,n)))return p;
    if((p=em_sfx_track_words(a,n)))return p;
    /* Words only AREA19's code reaches, behind every shared storage:
     * 0x70003910..1F, 0x827DD0's hit direction (its trackers); D_00275C10 /
     * 14, 001E7D20's once-a-frame step. */
    if(contains(a,n,0x70003910u,16)) {
        static uint8_t s3910[16];
        return s3910+a-0x70003910u;
    }
    if(contains(a,n,0x00275C10u,8)) {
        static uint8_t d275C10[8];
        return d275C10+a-0x00275C10u;
    }
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
/* The collision view's file: the loader's current mapping, re-resolved on
 * each access (a sub-area bank delivery splits the region holding it). */
static const uint8_t *collision_file_rest(void *ctx,uint32_t a,uint32_t *n)
{ return em_module_loader_memory_rest(((EmArea01Live *)ctx)->host.loader,a,n); }
static uint32_t random_value(void *ctx) { (void)ctx;return em_random_next(); }
static int hull_record(void *ctx,const EmActor *body,uint8_t out[EM_ACTOR_RECORD_SIZE])
{
    EmArea01Live *l=ctx;
    uint32_t at=em_actor_pool_address(l->host.pool,body);
    if(!at || actor(l,at)!=body)return -1;
    memset(out,0,EM_ACTOR_RECORD_SIZE);
    for(unsigned i=0;i<3;++i) {
        unsigned offset=i==0 ? 9u : i==1 ? 0x58u : 0x110u;
        unsigned size=i==0 ? 1u : i==1 ? 4u : 4u*out[9];
        if(out[9]>EM_OWNER_SERVICES_MAX_BONES)return -1;
        if(!size)continue;
        if(l->actors.active) {
            const uint8_t *p=em_area01_actor_view_bytes(&l->actors,at+offset,size,0);
            if(!p)return -1;
            memcpy(out+offset,p,size);
        } else if(em_area01_actor_view_snapshot(&l->actors,at+offset,size,out+offset)<0)return -1;
    }
    return 0;
}
static const uint8_t *hull_slot(void *ctx,uint32_t address,uint32_t size)
{ return em_area01_model_slot_bytes(&((EmArea01Live *)ctx)->model,address,size); }
/* --- AREA01 revisit: the player's hull for 0019AA80 / 001A7280 (the
 * collision view's player_chain). D_008102B0's +0x00, +0x09, +0x58, +0x5C
 * and +0x110 slot words are read through the player view into a call-local
 * record; em_area01_hull_chain borrows its +0x58 chain from the delivered
 * resources and copies the slots' +0x90 matrices. */
static int player_hull_record(void *ctx,const EmActor *body,uint8_t out[EM_ACTOR_RECORD_SIZE])
{
    (void)body;
    memset(out,0,EM_ACTOR_RECORD_SIZE);
    const uint8_t *p=bytes(ctx,EM_AREA01_PLAYER_BASE,0x60,0);
    if(!p)return -1;
    memcpy(out,p,0x60);
    if(out[9]>EM_OWNER_SERVICES_MAX_BONES)return -1;
    if(out[9]) {
        if(!(p=bytes(ctx,EM_AREA01_PLAYER_BASE+0x110u,4u*out[9],0)))return -1;
        memcpy(out+0x110,p,4u*out[9]);
    }
    return 0;
}
static const uint8_t *player_hull_slot(void *ctx,uint32_t address,uint32_t size)
{ return bytes(ctx,address,size,0); }
static const uint8_t *player_hull_rest(void *ctx,uint32_t address,uint32_t *size)
{ (void)ctx;return em_aim_fire_world_live_player_hull(address,size); }
static EmArea01HullView s_player_hull;
static int player_chain(void *ctx,EmActor *player,EmCollHullChain *out)
{
    uint8_t record[EM_ACTOR_RECORD_SIZE];
    if(!player || player_hull_record(ctx,NULL,record)<0)return -1;
    player->status=record[0];
    memcpy(&player->w58,record+0x58,4);memcpy(&player->w5C,record+0x5C,4);
    if(!out)return 0;
    s_player_hull.host=(EmArea01HullHost){ctx,player_hull_record,player_hull_rest,player_hull_slot};
    return em_area01_hull_chain(&s_player_hull,player,out);
}
int em_area01_live_hull_chain(void *ctx,const EmActor *body,EmCollHullChain *out)
{
    EmArea01Live *l=ctx;
    if(!l || !l->bound || l->fault)return -1;
    return em_area01_hull_chain(&l->hulls,body,out);
}
const uint8_t *em_area01_live_target_model_bytes(EmArea01Live *l,uint32_t address,uint32_t size)
{
    if(!l || !l->bound || l->fault)return NULL;
    if(address>=EM_ACTOR_POOL_BASE && address-EM_ACTOR_POOL_BASE<
       EM_ACTOR_POOL_CAPACITY*EM_ACTOR_RECORD_SIZE)
        return em_area01_actor_view_slot_words(&l->actors,address,size);
    return em_area01_model_slot_bytes(&l->model,address,size);
}
static int model_external(void *ctx,uint32_t fn,uint32_t node,uint32_t arg)
{
    EmArea01Live *l=ctx;
    if(fn==0x001F0120u)return em_area11_bindings_spawn_001F0120(node,(uint8_t)arg);
    if(fn==0x001DA6A0u || fn==0x001CAA00u)return em_area01_model_draw_call(&l->draw,fn,node);
    if(fn==0x001AFC10u) {
        EmActor *a=actor(l,node);
        return a ? em_actor_pool_free_001AFC10(l->host.pool,l->host.scene,a) : -1;
    }
    if(fn==0x001BA7F0u) {
        /* 001BA580's branch for the scene body 0x61 in AREA13: the runtime's
         * translation over the actor view this call had suspended. */
        const uint32_t parent=l->runtime.sp ? l->runtime.sp : LOCAL_BASE+sizeof l->locals-16;
        EmArea01Call c={.function=fn,.sp=parent-0x100u,.a={node},.na=1};
        if(parent<LOCAL_BASE+0x200u)return -1;
        em_camera_live_adopt_view();
        if(em_area01_actor_view_begin(&l->actors)<0 || player_begin(l)<0 ||
           em_area01_collision_view_begin(&l->collision)<0)return -1;
        int rc=em_area01_runtime_call(&l->runtime,&c);
        if(em_area01_collision_view_commit(&l->collision)<0 || em_area01_player_view_commit(&l->player)<0 ||
           em_area01_actor_view_commit(&l->actors)<0)return -1;
        em_camera_live_view_publish();
        return rc;
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
    case 0x001C64F0u: case 0x001C2360u: case 0x001C22A0u: case 0x001C5C90u:
    case 0x001C6960u: /* AREA01 revisit: 0x823CD0 states 4 / 5 */
    case 0x001C6910u: /* 001383C0's (AREA08, AREA07) */
    case 0x001C63D0u: /* 001BE6C0 (AREA01's way back, a01v) */
    case 0x001C9940u: /* 001C6910's tail (em_level12_port) */
    case 0x001CA5F0u: /* AREA15's 0x826850 */
        return 1;
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
/* D_0081083C, the player's grab-slot bits (the canonical progress byte):
 * 0012E070 / 0012E0B0 (the bugs' grab and release) read and write it
 * through em_area00_low's `grab_bits` view. */
static uint8_t *low_grab_bits(EmArea01Live *l)
{ return em_area01_live_bytes(l,0x0081083Cu,1,1); }
static int low_call(void *ctx,EmArea00LowCall *in)
{
    EmArea01Call c={.function=in->fn,.sp=in->sp,.na=in->na,.nf=in->nf};
    memcpy(c.a,in->a,sizeof c.a);memcpy(c.f,in->f,sizeof c.f);
    int rc=nested(ctx,&c);in->v0=c.v0;in->f0=c.f0;return rc;
}
/* 001F02C0(point, id; f12): em_area00_fx's translation (the one; the aim /
 * fire world binds it too) over this composition's bytes: the point is
 * copied to the frame's +0xE0, which is +0xB0 of the frame's +0x30 block,
 * then 001FBD50(block, id, 0; f12) comes back through the live worker (the
 * AREA02 car's sounds, 0x8242F0). */
static int fx_nested(void *ctx,uint32_t target,uint32_t sp,const EmArea00FxRegs *in,uint64_t *v0,uint32_t *f0)
{
    EmArea01Call c={.function=target,.sp=sp};
    for(unsigned i=4;i<12;++i)if(in->imask&(1u<<i))c.na=i-3u;
    for(unsigned i=0;i<c.na;++i)c.a[i]=in->r[4+i];
    for(unsigned i=12;i<20;++i)if(in->fmask&(1u<<i))c.nf=i-11u;
    for(unsigned i=0;i<c.nf;++i)c.f[i]=in->f[12+i];
    int rc=nested(ctx,&c);
    if(rc>=0){ if(v0)*v0=c.v0; if(f0)*f0=c.f0; }
    return rc;
}
static int fx_001F02C0(EmArea01Live *l,EmArea01Call *c)
{
    if(c->na<2 || c->nf<1)return worker_result(l,-1,c->function);
    EmArea00Fx s;memset(&s,0,sizeof s);
    s.core.world.view=bytes;s.core.world.view_ctx=l;
    s.call=fx_nested;s.ctx=l;s.sp=c->sp;
    int rc=em_area00_fx_001F02C0(&s,c->a[0],c->a[1],c->f[0]);
    if(rc<0 || s.core.fault.code)
        return worker_result(l,-1,s.core.fault.address ? s.core.fault.address : c->function);
    return worker_result(l,0,0);
}
/* 0012D580(a0, a1, a2): 00128C10's sub-state machine at a0 +7 (reached
 * through em_area01_exita's 00128C10 state 8). */
static int low_0012D580(EmArea01Live *l,EmArea01Call *c)
{
    if(c->na<3)return worker_result(l,-1,c->function);
    EmArea00Low low;memset(&low,0,sizeof low);
    low.call=low_call;low.ctx=l;low.sp=c->sp;low.view=low_view;low.view_ctx=l;
    low.grab_bits=low_grab_bits(l);
    int rc=em_area00_low_0012D580(&low,(uint32_t)c->a[0],(uint32_t)c->a[1],c->a[2]);
    return worker_result(l,rc,rc<0 ? (low.fault_address ? low.fault_address : c->function) : 0);
}
/* 001288D0(a0, a1): the creature glow (byte-matched decomp C), which
 * 00128C10 / 0012A5D0 call when their block a1's signed byte +0xFA is
 * non-zero: scratch 0x700038A0 = (0, -1.5, 0, 1) through the current bone
 * row's +0x90, the colour words at 0x700038B0 by a1 +0xE1, then 001F4A00
 * (the tail call to 001F4BF0, flame services). */
static int low_001288D0(EmArea01Live *l,EmArea01Call *c)
{
    if(c->na<2)return worker_result(l,-1,c->function);
    EmArea00Low low;memset(&low,0,sizeof low);
    low.call=low_call;low.ctx=l;low.sp=c->sp;low.view=low_view;low.view_ctx=l;
    low.grab_bits=low_grab_bits(l);
    int rc=em_area00_low_001288D0(&low,(uint32_t)c->a[0],(uint32_t)c->a[1]);
    return worker_result(l,rc,rc<0 ? (low.fault_address ? low.fault_address : c->function) : 0);
}
/* The sub-state machines the bug owner 0012A5D0 (em_area01_exita, byte-
 * matched decomp C) runs by its +5 (states 2..7 and 9: 0012B410 NEARMISS,
 * 0012B970 / 0012BE20 / 0012C490 jump tables, 0012CAA0 byte-matched,
 * 0012D240 NEARMISS, 0012D850 word assembly), and the reaction / release
 * routines 0012A5D0's callees reach (00129FC0 byte-matched jump table,
 * 0012E070 C linked from asm): em_area00_low's translations
 * (test_area00_low_reference.py against the original instructions), over
 * this composition's views; their own callees come back through it. First
 * reached natively by AREA00 a00_09's crate children. */
static int low_entry(EmArea01Live *l,EmArea01Call *c)
{
    EmArea00Low low;memset(&low,0,sizeof low);
    low.call=low_call;low.ctx=l;low.sp=c->sp;low.view=low_view;low.view_ctx=l;
    low.grab_bits=low_grab_bits(l);
    const uint32_t a0=(uint32_t)c->a[0],a1=(uint32_t)c->a[1];
    int rc;
    switch(c->function) {
    case 0x0012B410u: rc=c->na<2 ? -1 : em_area00_low_0012B410(&low,a0,a1);break;
    case 0x0012B970u: rc=c->na<2 ? -1 : em_area00_low_0012B970(&low,a0,a1);break;
    case 0x0012BE20u: rc=c->na<2 ? -1 : em_area00_low_0012BE20(&low,a0,a1);break;
    case 0x0012C490u: rc=c->na<2 ? -1 : em_area00_low_0012C490(&low,a0,a1);break;
    case 0x0012CAA0u: rc=c->na<2 ? -1 : em_area00_low_0012CAA0(&low,a0,a1);break;
    case 0x0012D240u: rc=c->na<2 ? -1 : em_area00_low_0012D240(&low,a0,a1);break;
    case 0x0012D850u: rc=c->na<2 ? -1 : em_area00_low_0012D850(&low,a0,a1);break;
    case 0x00129FC0u: rc=c->na<2 ? -1 : em_area00_low_00129FC0(&low,a0,a1);break;
    case 0x0012E070u: rc=c->na<1 ? -1 : em_area00_low_0012E070(&low,a0);break;
    /* 0012DE90(event block): the AREA01 revisit's 0x824D50 (byte-matched C). */
    case 0x0012DE90u: rc=c->na<1 ? -1 : em_area00_low_0012DE90(&low,a0);break;
    /* 00102870(dst, v; f12): the AREA01 revisit beam's (001E2BA0) quadword
     * divide, em_area00_low's translation (as em_aim_fire_runtime's). */
    case 0x00102870u: rc=c->na<2 || c->nf<1 ? -1 : em_area00_low_00102870(&low,a0,a1,c->f[0]);break;
    /* 00128830(a0; f12, f13, f14): the AREA01 revisit creatures' (0x824340)
     * hop, em_area00_low's translation (byte-matched decomp C). */
    case 0x00128830u: rc=c->na<1 || c->nf<3 ? -1 : em_area00_low_00128830(&low,a0,c->f[0],c->f[1],c->f[2]);break;
    /* 001B5360(node): the 0012A5D0 bugs' walk step from 0012D940's state 1
     * (AREA22's corridor bugs, +5 = 10 / 11), em_area00_low's translation. */
    case 0x001B5360u: rc=c->na<1 ? -1 : em_area00_low_001B5360(&low,a0);break;
    /* 001FC580(node, id): the AREA19 drums' (00156620) landing sound. */
    case 0x001FC580u: rc=c->na<2 ? -1 : em_area00_low_001FC580(&low,a0,(int32_t)a1);break;
    default: rc=-1;break;
    }
    if(rc<0)
        fprintf(stderr,"em_area01: em_area00_low %08X (entry %08X) fault code %d at %08X\n",
                (unsigned)c->function,(unsigned)low.fault_function,(int)low.fault,(unsigned)low.fault_address);
    return worker_result(l,rc,rc<0 ? (low.fault_address ? low.fault_address : c->function) : 0);
}
static int low_entry_handles(uint32_t fn)
{
    switch(fn) {
    case 0x0012B410u: case 0x0012B970u: case 0x0012BE20u: case 0x0012C490u: case 0x0012CAA0u:
    case 0x0012D240u: case 0x0012D850u: case 0x00129FC0u: case 0x0012E070u:
    case 0x0012DE90u: case 0x00102870u: case 0x00128830u: case 0x001B5360u: case 0x001FC580u:
        return 1;
    default:
        return 0;
    }
}
/* 001027E0(dst, src) (asm-word leaf of the SDK, the rigid inverse:
 * transposed rotation rows with w = 0, translation -(R^T t), w kept), from
 * the bug owners' 0012ADC0 (em_area01_exita; scratch 0x700036E0 from
 * 0x700036A0): em_render_verify_rest's translation (em_rvr_001027E0,
 * test_render_verify_rest_reference.py against the original instructions).
 * The four source rows (low four address bits ignored) are all read before
 * the first store, so dst may equal src; a partial overlap is refused. */
static int sdk_001027E0(EmArea01Live *l,EmArea01Call *c)
{
    if(c->na<2)return worker_result(l,-1,c->function);
    const uint32_t a=(uint32_t)c->a[0]&~15u,b=(uint32_t)c->a[1]&~15u;
    if(a!=b && overlaps(a,64,b,64))return worker_result(l,-1,c->function);
    uint32_t in[16],out[16];
    const uint8_t *p=bytes(l,b,64,0);
    if(!p)return worker_result(l,-1,b);
    memcpy(in,p,64);
    EmRvrFault fault;memset(&fault,0,sizeof fault);
    if(em_rvr_001027E0(out,in,&fault)<0)return worker_result(l,-1,c->function);
    uint8_t *q=bytes(l,a,64,1);
    if(!q)return worker_result(l,-1,a);
    memcpy(q,out,64);
    return 0;
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
    int rc=c->function==0x001F9180u ? em_shadow_live_owner_001F9180(q[0],q[1],q[2],c->f[0])
                                    : em_shadow_live_owner_001F9100(q[0],q[1],q[2],c->f[0]);
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
/* 001C9E40(q, m), the rotation's quaternion: em_anim_rest's translation
 * (the aim/fire binding's, live through 0017B660), its 0011E748 the SDK
 * square root. The AREA01 revisit reaches it from 001F2F90 (0x8237D0's
 * debris slot through 001F4010). */
static int rest_001C9E40(EmArea01Live *l,EmArea01Call *c)
{
    if(c->na<2 || c->nf)return worker_result(l,-1,c->function);
    const uint32_t qa=(uint32_t)c->a[0],ma=(uint32_t)c->a[1];
    if(overlaps(qa,16,ma,64))return worker_result(l,-1,c->function);
    uint32_t m[16],q[4];
    const uint8_t *pm=bytes(l,ma,64,0);
    if(!pm)return worker_result(l,-1,ma);
    memcpy(m,pm,64);
    EmAnimRest r;memset(&r,0,sizeof r);
    r.workers.sqrt_ctx=em_collision_world_sdk();
    r.workers.w_0011E748=em_anim_rest_sqrt_0011E748;
    int rc=em_anim_rest_001C9E40(&r,q,m);
    if(rc<0)return worker_result(l,rc,r.fault.address ? r.fault.address : c->function);
    uint8_t *pq=bytes(l,qa,16,1);
    if(!pq)return worker_result(l,-1,qa);
    memcpy(pq,q,16);
    return 0;
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
/* 00121870 block_copy(dst, src, n) (the C library forward copy; decomp
 * assembly), as em_static_world's translation orders it: with n >= 0x20 and
 * both addresses 16-aligned, pairs of quadwords while n >= 0x20 (tested
 * after each pair), then doublewords while n >= 8; then bytes. Each piece is
 * loaded before it is stored, so a forward overlap behaves as the original's.
 * a3 is not read; v0 is not set (no caller reads it). 001D0400 (via
 * 0022DCD0 in AREA00) is the caller here. */
static int block_copy_00121870(EmArea01Live *l,EmArea01Call *c)
{
    if(c->na<3)return worker_result(l,-1,c->function);
    uint32_t dst=(uint32_t)c->a[0],src=(uint32_t)c->a[1],n=(uint32_t)c->a[2];
    uint8_t piece[16];
#define PIECE(size) do { \
    const uint8_t *from_=bytes(l,src,(size),0); \
    if(!from_)return worker_result(l,-1,src); \
    memcpy(piece,from_,(size)); \
    uint8_t *to_=bytes(l,dst,(size),1); \
    if(!to_)return worker_result(l,-1,dst); \
    memcpy(to_,piece,(size)); \
    src+=(size);dst+=(size); \
} while(0)
    if(n>=0x20u && ((src|dst)&15u)==0) {
        do { PIECE(16u);PIECE(16u);n-=0x20u; } while(n>=0x20u);
        while(n>=8u) { PIECE(8u);n-=8u; }
    }
    while(n--!=0u)PIECE(1u);
#undef PIECE
    return 0;
}
/* --- AREA01 revisit: 001F4F40(kind) (decomp C; em_area00_fx's translation)
 * for 0x826010's 001B0CD0. Its 001AFA90(0xC) allocates in this pool; the
 * record is then adopted by em_aim_fire_runtime, so the muzzle node's
 * bytes are em_aim_fire_flash's and its 001F5040 is bound there when the
 * +0x10 store commits (area01_rebind). */
static int nested(void *ctx,EmArea01Call *c);
static uint8_t *muzzle_view(void *ctx,uint32_t a,uint32_t n,int write)
{ return em_area01_live_bytes(ctx,a,n,write); }
static int muzzle_worker(void *ctx,uint32_t fn,uint32_t sp,const EmArea00FxRegs *in,uint64_t *v0,uint32_t *f0)
{
    EmArea01Live *l=ctx;
    if(fn!=0x001AFA90u || in->imask!=(1u<<4) || in->fmask)return -1;
    EmArea01Call c={.function=fn,.sp=sp,.a={in->r[4]},.na=1};
    if(nested(l,&c)<0)return -1;
    *v0=c.v0;*f0=c.f0;
    if(!c.v0)return 0;
    EmActor *a=actor(l,(uint32_t)c.v0);
    if(!a || em_area01_live_suspend(l)<0)return -1;
    int rc=em_aim_fire_runtime_adopt_flash(a);
    if(em_area01_live_resume(l)<0)return -1;
    return rc;
}
/* A class-0xC record whose +0x10 an AREA01 spawner set to 001F5040 without
 * 001F4F40 (0x8287C0's child, 0x8237D0's node): adopted at its rebind, its
 * +0xD0..+0x10F (stored by the spawner before the +0x10 store, held by the
 * actor view's image) seeded into em_aim_fire_flash. */
int em_area01_live_adopt_flash(EmArea01Live *l,EmActor *a)
{
    if(!l || !l->bound || !a)return -1;
    const uint32_t node=em_actor_pool_address(l->host.pool,a);
    const uint8_t *image=em_area01_actor_view_image(&l->actors,node);
    uint8_t d0[64];
    if(!image)return -1;
    memcpy(d0,image+0xD0,64);
    if(em_aim_fire_runtime_adopt_flash(a)<0)return -1;
    return em_aim_fire_flash_seed_d0(a,d0);
}
static int muzzle_001F4F40(EmArea01Live *l,EmArea01Call *c)
{
    if(c->na<1 || c->nf)return worker_result(l,-1,c->function);
    EmArea00Fx x;memset(&x,0,sizeof x);
    x.core.world.view=muzzle_view;x.core.world.view_ctx=l;x.call=muzzle_worker;x.ctx=l;x.sp=c->sp;
    int rc=em_area00_fx_001F4F40(&x,c->a[0],&c->v0);
    if(rc<0)return worker_result(l,rc,x.core.fault.detail ? x.core.fault.detail : x.core.fault.address);
    if(!c->v0)return 0;
    /* A reused record keeps its +0x10 (001AFA90 leaves it, 001AFC10 too):
     * when it already held 001F5040 the store commits no change and no
     * rebind runs, so the adopted node is bound here once committed. */
    EmActor *a=actor(l,(uint32_t)c->v0);
    if(!a || em_area01_live_suspend(l)<0)return -1;
    rc=a->behavior ? 0 : em_aim_fire_runtime_bind_flash(a);
    if(em_area01_live_resume(l)<0)return -1;
    return worker_result(l,rc,c->function);
}
/* --- 001F2F90 / 001F3340 (001F4010's callees: a debris slot's seed and
 * its first step, em_area00_fx_debris, their one translation; the aim/fire
 * world binds them for the shell casing) over the live bytes; every callee
 * re-enters the runtime. 001F3340 hands 0019A570 its s4, which 0019A570
 * reads only on a path the segment walker faults on (as the aim/fire
 * world), so s4 is not forwarded. */
static int debris_worker(void *ctx,uint32_t fn,uint32_t sp,const EmArea00FxRegs *r,uint64_t *v0,uint32_t *f0)
{
    uint32_t imask=r->imask;
    if(fn==0x0019A570u)imask&=~(1u<<20);
    if((imask&~(0xFFu<<4)) || (r->fmask&~(0xFFu<<12)))return -1;
    EmArea01Call c={.function=fn,.sp=sp};
    for(unsigned i=0;i<8;++i)if(imask&(1u<<(i+4))) {c.a[i]=r->r[i+4];c.na=i+1;}
    for(unsigned i=0;i<8;++i)if(r->fmask&(1u<<(i+12))) {c.f[i]=r->f[i+12];c.nf=i+1;}
    if(nested(ctx,&c)<0)return -1;
    *v0=c.v0;*f0=c.f0;
    return 0;
}
static int debris_001F2F90(EmArea01Live *l,EmArea01Call *c)
{
    if(c->na<(c->function==0x001F2F90u ? 4u : 3u) || c->nf)return worker_result(l,-1,c->function);
    EmArea00Fx x;memset(&x,0,sizeof x);
    x.core.world.view=muzzle_view;x.core.world.view_ctx=l;x.call=debris_worker;x.ctx=l;x.sp=c->sp;
    int rc=c->function==0x001F2F90u ? em_area00_fx_001F2F90(&x,c->a[0],c->a[1],c->a[2],c->a[3])
                                    : em_area00_fx_001F3340(&x,c->a[0],c->a[1],c->a[2]);
    if(rc<0 || x.core.fault.code)
        return worker_result(l,-1,x.core.fault.detail ? x.core.fault.detail : x.core.fault.address);
    return 0;
}
static int worker(void *ctx,EmArea01Call *c)
{
    EmArea01Live *l=ctx;
    const unsigned area=l->runtime.area;
    /* The soft-float double and its compare (em_sdk_soft_float's 00128350,
     * oracle-checked; em_render_verify_rest's 001000E0, byte-matched C):
     * AREA04's creature family (em_level8_port's hooks). */
    if(c->function==0x00128350u && c->nf==1 && !c->na) {
        c->v0=em_sdk_soft_float_00128350(c->f[0]);return 0;
    }
    if(c->function==0x001000E0u && c->na==2 && !c->nf) {
        c->v0=(uint64_t)(int64_t)em_rvr_001000E0(c->a[0],c->a[1]);return 0;
    }
    if((area==0 || area==2) && c->function>=0x00823500u) {
        if(em_area01_script_worker_handles_area(c->function,area)) {
            const EmArea01RuntimeHost host={l,bytes,nested};uint32_t fault=0;
            int rc=em_area01_script_worker_call_area(&host,c,&fault,area);
            return worker_result(l,rc,fault);
        }
        return l->host.worker ? l->host.worker(l->host.ctx,c) : worker_result(l,-1,c->function);
    }
    if(em_area01_transition_handles(c->function)) {
        const EmArea01RuntimeHost host={l,bytes,nested};uint32_t fault=0;
        int rc=em_area01_transition_call(&host,c,&fault);
        return worker_result(l,rc,fault);
    }
    if(c->function==0x001F4F40u)return muzzle_001F4F40(l,c);
    if(c->function==0x001F2F90u || c->function==0x001F3340u)return debris_001F2F90(l,c);
    if(c->function==0x00121870u)return block_copy_00121870(l,c);
    if(c->function==0x001B0C60u) {
        if(c->na!=3 || c->nf)return worker_result(l,-1,c->function);
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=em_scene_request_area_change_001B0C60((int32_t)c->a[0],(int32_t)c->a[1],(int32_t)c->a[2]);
        if(em_area01_live_resume(l)<0)return -1;
        return worker_result(l,rc,c->function);
    }
    if(em_area01_effects_draw_handles(c->function)) {
        const EmArea01RuntimeHost host={l,bytes,nested};uint32_t fault=0;
        EmArea01EffectsDraw draw;
        if(em_area01_effects_draw_prepare(&host,c,&draw,&fault)<0)return worker_result(l,-1,fault);
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=em_area01_effects_draw_invoke(&draw,c);
        uint32_t owner_fault=em_effects_live_fault();
        if(em_area01_live_resume(l)<0)return -1;
        if(em_area01_effects_draw_finish(&host,&draw,&fault)<0)return worker_result(l,-1,fault);
        return worker_result(l,rc,owner_fault ? owner_fault : c->function);
    }
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
        /* 001C6120(bank, id) (byte-matched src/func_001C6120.c). Roger's
         * library keeps its exported table; any other bank (AREA00's
         * arrival owner through 001C6160) is em_pose_host_001C6120 (the
         * oracle-checked translation, test-pose-host-workers-reference)
         * over the one directory word, read from the composition's views. */
        const uint32_t *library=em_area11_roger_library_word();uint32_t result=0;
        if(c->na!=2 || c->nf)return worker_result(l,-1,c->function);
        const uint32_t bank=(uint32_t)c->a[0];
        if(!library || bank!=*library) {
            const uint32_t at=bank+4u+4u*((uint32_t)c->a[1]&0x7FFFu);
            uint8_t *entry=bytes(l,at,4,0);
            if(!entry)return worker_result(l,-1,at);
            EmPoseHost directory;memset(&directory,0,sizeof directory);
            directory.region[directory.region_count++]=(EmPoseRegion){at,4,entry,0};
            if(em_pose_host_001C6120(&directory,bank,(int)(int32_t)c->a[1],&result)<0)
                return worker_result(l,-1,c->function);
            c->v0=(uint64_t)(int64_t)(int32_t)result;
            return 0;
        }
        int rc=em_area11_roger_001C6120(bank,(int32_t)c->a[1],&result);
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
    if(c->function==0x001CFB50u) {
        /* 001CFB50(block, index, matrix; f12..f16): the transform block
         * from the 64-byte matrix (em_effect_kinds' translation through
         * em_effects_live). 0022DCD0 (AREA00) passes its stack block and a
         * matrix of the 0x00822CF0 table. */
        if(c->na<3 || c->nf<5)return worker_result(l,-1,c->function);
        const uint8_t *m=bytes(l,(uint32_t)c->a[2],64,0);
        if(!m)return worker_result(l,-1,(uint32_t)c->a[2]);
        uint8_t *view=bytes(l,(uint32_t)c->a[0],0x60,1);
        if(!view)return worker_result(l,-1,(uint32_t)c->a[0]);
        float matrix[16];uint8_t block[0x60];
        memcpy(matrix,m,64);memcpy(block,view,0x60);
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=em_effects_live_001CFB50(block,(int32_t)c->a[1],matrix,c->f);
        if(em_area01_live_resume(l)<0)return -1;
        if(rc<0)return worker_result(l,rc,em_effects_live_fault());
        if(!(view=bytes(l,(uint32_t)c->a[0],0x60,1)))return worker_result(l,-1,(uint32_t)c->a[0]);
        memcpy(view,block,0x60);
        return 0;
    }
    if(c->function==0x001D04B0u) {
        /* 001D04B0(matrix, kind, source; f12, f13): the particle draw
         * (em_effects_live's translation, as the spat hazard's) over the
         * matrix as it stands and the 0x90-byte source the caller names
         * (AREA06's 0x8240E0 hands one of its overlay's). */
        if(c->na<3 || c->nf<2)return worker_result(l,-1,c->function);
        const uint8_t *m=bytes(l,(uint32_t)c->a[0],64,0);
        if(!m)return worker_result(l,-1,(uint32_t)c->a[0]);
        const uint8_t *src=bytes(l,(uint32_t)c->a[2],0x90,0);
        if(!src)return worker_result(l,-1,(uint32_t)c->a[2]);
        float matrix[16];uint8_t source[0x90];
        memcpy(matrix,m,64);memcpy(source,src,0x90);
        if(em_area01_live_suspend(l)<0)return -1;
        /* the first boss's spores name an ELF block (D_0025D920) */
        const uint32_t from=(uint32_t)c->a[2];
        int rc=from>=0x00100000u && from<0x00275B00u
            ? em_effects_live_001D04B0_elf(matrix,(int32_t)c->a[1],from,c->f[0],c->f[1])
            : em_effects_live_001D04B0(matrix,(int32_t)c->a[1],from,source,c->f[0],c->f[1]);
        if(em_area01_live_resume(l)<0)return -1;
        return worker_result(l,rc,rc<0 ? em_effects_live_fault() : 0);
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
       c->function==0x001B1EA0u || c->function==0x001C4760u || c->function==0x00157F60u) {
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
    if(c->function==0x00183EF0u || c->function==0x00184BA0u || c->function==0x00185420u)
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
    if(c->function==0x001CD370u && c->na==1) {
        /* 001CD370(i): D_00275670 + i * 0x40 + 0x2240, the clip matrices. */
        const uint8_t *context=bytes(l,0x00275670u,4,0);
        uint32_t base=0;
        if(!context)return worker_result(l,-1,c->function);
        memcpy(&base,context,4);
        c->v0=(uint64_t)(int64_t)(int32_t)(base+(uint32_t)c->a[0]*0x40u+0x2240u);
        return worker_result(l,0,0);
    }
    if(c->function==0x001CF470u) {
        /* 001CF470(D_008112C0, m): the level ports' 001CDDC0 clip. */
        const uint8_t *m=c->na==2 && (uint32_t)c->a[0]==0x008112C0u ? bytes(l,(uint32_t)c->a[1],64,0) : NULL;
        if(!m)return worker_result(l,-1,c->function);
        uint32_t rows[16];int32_t count=0;
        memcpy(rows,m,sizeof rows);
        const int rc=em_shadow_live_001CF470(rows,&count);
        c->v0=(uint64_t)(int64_t)count;
        return worker_result(l,rc,rc<0 ? c->function : 0);
    }
    if(c->function==0x001FBD50u || c->function==0x001FBF50u || c->function==0x001B1380u ||
       c->function==0x001FC3C0u || c->function==0x001FC520u) {
        const EmArea01RuntimeHost host={l,bytes,nested};
        return em_area01_audio_services_call(&host,em_collision_world_sdk(),
                                              em_stream_live_output_mode(),c);
    }
    if(em_area01_script_worker_handles_area(c->function,area)) {
        const EmArea01RuntimeHost host={l,bytes,nested};uint32_t fault=0;
        int rc=em_area01_script_worker_call_area(&host,c,&fault,area);
        return worker_result(l,rc,fault);
    }
    if(c->function==0x001F02C0u)return fx_001F02C0(l,c);
    if(c->function==0x001F0460u) {
        /* 001F0460(n, src): a ring decal slot (AREA19's 0x827B60 falling
         * pieces), em_effects_live's translation. */
        const uint8_t *q=c->na>=2 ? bytes(l,(uint32_t)c->a[1]&~15u,64,0) : NULL;
        if(!q)return worker_result(l,-1,c->function);
        float m[16];
        memcpy(m,q,sizeof m);
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=em_effects_live_001F0460((int32_t)c->a[0],m);
        if(em_area01_live_resume(l)<0)return -1;
        return worker_result(l,rc,c->function);
    }
    if(c->function==0x001F6B30u) {
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=em_effects_live_001F6B30(em_scene_state());
        if(em_area01_live_resume(l)<0)return -1;
        return worker_result(l,rc,c->function);
    }
    if(c->function==0x0012D580u)return low_0012D580(l,c);
    if(c->function==0x001288D0u)return low_001288D0(l,c);
    if(low_entry_handles(c->function))return low_entry(l,c);
    if(c->function==0x001027E0u)return sdk_001027E0(l,c);
    if(c->function==0x001F9100u || c->function==0x001F9180u)return owner_001F9100(l,c);
    if(c->function==0x001C9D50u)return rest_001C9D50(l,c);
    if(c->function==0x001C9E40u)return rest_001C9E40(l,c);
    if(em_area01_door_handles(c->function))return em_area01_door_call(&l->door,c);
    if(em_area01_pickup_handles(c->function))return em_area01_pickup_call(&l->pickups,c);
    if(c->function==0x001EFD90u || c->function==0x001EFD20u || c->function==0x001EF9D0u ||
       c->function==0x001EFEB0u) {
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
    if(c->function==0x001AF800u && c->na>=1) {
        /* 001AF800 on a typed shared owner (an AREA00 record bound through
         * the boxes' owner views: [60] 0x8266A0's model switch at a00_10):
         * its +0x110 words are the owner's own slots, read-only in the
         * record view, so the release is the one 001AF800 over them, the
         * pool's (em_area11_boxes_001AF800: em_roger_actor_001AF800 over
         * the canonical slot stack), with the views published around it. */
        EmActor *a=actor(l,c->a[0]);
        if(a && em_area11_boxes_owner_view(a)) {
            if(em_area01_actor_view_touch(&l->actors,c->a[0])<0 || em_area01_live_suspend(l)<0)return -1;
            EmActorPool *pool=l->host.pool;
            int rc=pool && pool->w_001AF800 ? pool->w_001AF800(pool->worker_ctx,a) : -1;
            if(em_area01_live_resume(l)<0)return -1;
            c->v0=0;return rc<0 ? -1 : 0;
        }
    }
    if((c->function==0x001CA5E0u && c->na>=3) || (c->function==0x001CA6E0u && c->na>=2)) {
        /* 001CA5E0 on a typed shared owner (AREA02's 001C4AF0 re-installs
         * the owner's model with kind 1): +0x44 and +0x4C are the owner's
         * canonical view, read-only in the record view, so the store is the
         * boxes' 001CA5E0 over it, with the views published around it.
         * 001CA6E0(owner, handle) is 001CA5E0(owner, handle, 0) (decomp
         * func_001CA6E0.c): AREA04's [47] 0x823EE0 rebinds its model so in
         * its state 2 (a04_00). */
        EmActor *a=actor(l,c->a[0]);
        if(a && em_area11_boxes_owner_view(a)) {
            if(em_area01_actor_view_touch(&l->actors,c->a[0])<0 || em_area01_live_suspend(l)<0)return -1;
            int rc=em_area11_boxes_owner_001CA5E0(a,c->a[1],c->function==0x001CA6E0u ? 0u : c->a[2]);
            if(em_area01_live_resume(l)<0)return -1;
            c->v0=0;return rc<0 ? -1 : 0;
        }
    }
    if(c->function==0x001CA5F0u && c->na>=2) {
        /* 001CA5F0(owner, kind) on a typed shared owner (AREA15's 0x826850,
         * state 0): +0x4C, as above. */
        EmActor *a=actor(l,c->a[0]);
        if(a && em_area11_boxes_owner_view(a)) {
            if(em_area01_actor_view_touch(&l->actors,c->a[0])<0 || em_area01_live_suspend(l)<0)return -1;
            int rc=em_area11_boxes_owner_001CA5F0(a,c->a[1]);
            if(em_area01_live_resume(l)<0)return -1;
            c->v0=0;return rc<0 ? -1 : 0;
        }
    }
    if(model_function(c->function)) {
        /* Model worker brackets actor views itself; the player and camera
         * must also publish before its native head/shadow callbacks. */
        if(em_area01_collision_view_commit(&l->collision)<0 || em_area01_player_view_commit(&l->player)<0)return -1;
        em_camera_live_view_publish();
        uint32_t result;
        /* 001C9940 takes the record's +0x110 and +0xD0. */
        const uint32_t node=c->function==0x001C9940u ? (uint32_t)c->a[0]-0x110u : (uint32_t)c->a[0];
        if(c->function==0x001C9940u && (uint32_t)c->a[2]!=node+0xD0u)return -1;
        int rc=em_area01_model_call(&l->model,c->function,node,c->a[1],c->a[2],c->a[3],
                                     em_ee_float(c->f[0]),em_ee_float(c->f[1]),&result);
        em_camera_live_adopt_view();
        if(player_begin(l)<0 || em_area01_collision_view_begin(&l->collision)<0)return -1;
        c->v0=(uint64_t)(int64_t)(int32_t)result;return rc;
    }
    if(c->function==0x001CAA00u || c->function==0x001CACB0u ||
       c->function==0x001CB360u || c->function==0x001DA6A0u || c->function==0x001CAF60u) {
        if(c->na<1 || em_area01_collision_view_commit(&l->collision)<0 || em_area01_player_view_commit(&l->player)<0)return -1;
        l->draw.current=*l->host.current_actor;l->draw.sp=c->sp;
        em_camera_live_view_publish();
        int rc=em_area01_model_draw_call(&l->draw,c->function,(uint32_t)c->a[0]);
        em_camera_live_adopt_view();
        if(player_begin(l)<0 || em_area01_collision_view_begin(&l->collision)<0)return -1;
        c->v0=0;return rc;
    }
    if(c->function==0x001CA7B0u) {
        /* 001CA7B0(position, f12): em_owner_draw_live's cull flags over the
         * position as it stands (AREA13's [45] 0x827150 and [11]'s). */
        if(c->na<1 || c->nf<1)return -1;
        const uint8_t *p=bytes(l,(uint32_t)c->a[0],16,0);
        if(!p)return worker_result(l,-1,(uint32_t)c->a[0]);
        uint32_t position[4];int32_t flags=0;memcpy(position,p,16);
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=em_owner_draw_live_001CA7B0(position,c->f[0],&flags);
        if(em_area01_live_resume(l)<0)return -1;
        c->v0=(uint64_t)(int64_t)flags;
        return worker_result(l,rc,rc<0 ? 0x001CA7B0u : 0);
    }
    if(c->function==0x001C7900u) {
        /* 001C7900(m, token, 0x3F5, 0): the colour and node CNTs on channel
         * 0 (em_owner_draw_live), as 001F3E30's (AREA13's [45] markers). */
        if(c->na<4)return -1;
        const uint8_t *m=bytes(l,(uint32_t)c->a[0],64,0),*t=bytes(l,(uint32_t)c->a[1],16,0);
        if(!m || !t)return worker_result(l,-1,(uint32_t)c->a[0]);
        uint32_t matrix[16];uint8_t token[16];memcpy(matrix,m,64);memcpy(token,t,16);
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=em_owner_draw_live_001C7900(matrix,(uint32_t)c->a[1],token,(int32_t)c->a[2],(int32_t)c->a[3]);
        if(em_area01_live_resume(l)<0)return -1;
        return worker_result(l,rc,rc<0 ? 0x001C7900u : 0);
    }
    if(c->function==0x001CA940u) {
        /* 001CA940(flags, model): the model's REFs, the unit kept with the
         * frame's units (em_owner_draw_live), a world-bank model here. */
        if(c->na<2)return -1;
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=em_owner_draw_live_001CA940_library((int32_t)c->a[0],(uint32_t)c->a[1]);
        if(em_area01_live_resume(l)<0)return -1;
        return worker_result(l,rc,rc<0 ? 0x001CA940u : 0);
    }
    EmSdkMathContext *sdk=em_collision_world_sdk();float out;
    switch(c->function) {
    case 0x00121A28u: {
        /* 00121A28, the C runtime's memset(dst, value, count) (the AREA02
         * owner 0x823930's state-0 clear of its +0x2B0 block): over the
         * canonical bytes of dst; v0 = dst. */
        const uint32_t n=(uint32_t)c->a[2];
        if(c->na!=3 || c->nf || n>0x10000u)return -1;
        uint8_t *p=n ? bytes(l,(uint32_t)c->a[0],n,1) : NULL;
        if(n && !p)return worker_result(l,-1,(uint32_t)c->a[0]);
        if(n)memset(p,(int)(c->a[1]&0xFFu),n);
        c->v0=c->a[0];return 0;
    }
    case 0x001798D0u: {
        /* 001798D0(D_008102B0): the area script's player reset (0x82B0D0's
         * record handler at a01r_02): em_player_use_001798D0 over the live
         * record, a native worker, with the views published around it. */
        if(c->na!=1 || c->nf || c->a[0]!=EM_AREA01_PLAYER_BASE || !l->player.active)return -1;
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=em_player_closure_live_001798D0(l->player.actor);
        if(em_area01_live_resume(l)<0)return -1;
        c->v0=0;return worker_result(l,rc,c->function);
    }
    case 0x0021BB00u:
        if(c->na!=1 || c->nf || c->a[0]!=EM_AREA01_PLAYER_BASE || !l->player.active)return -1;
        c->v0=(uint64_t)(int64_t)em_player_0021BB00(l->player.actor);
        return 0;
    case 0x00182F90u: {
        if(c->na!=2 || c->nf || c->a[0]!=0x008102B0u)return -1;
        const uint8_t *point=bytes(l,(uint32_t)c->a[1],12,0);
        if(!point)return -1;
        float position[3];memcpy(position,point,sizeof position);
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=player_pose_align(position) ? 0 : -1;
        if(em_area01_live_resume(l)<0)return -1;
        return rc;
    }
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
    case 0x001B1B70u: case 0x001B1DE0u: case 0x001B1D20u: {
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
    case 0x001AEDB0u: case 0x001AEDE0u: case 0x001AEE10u: case 0x001B1E20u:
    case 0x001B0250u: case 0x0021B9A0u: case 0x001D2830u:
    case 0x001DD980u: case 0x001D25F0u: case 0x001FB9F0u:
    case 0x001FAD70u: case 0x001FA790u: case 0x001FABB0u:
    case 0x001FAE70u: case 0x001FBC50u: {
        /* The byte views have the last original writes. Publish before
         * existing native frame, camera, render and sound services run. */
        if((c->function==0x001AEDE0u || c->function==0x001AEE10u ||
            c->function==0x001B1E20u || c->function==0x001D2830u ||
            c->function==0x001DD980u) && c->na<2)return -1;
        if(c->function==0x0021B9A0u && (c->na<1 || c->nf<2))return -1;
        if(c->function==0x001D25F0u && c->nf<1)return -1;
        if(c->function==0x001DD980u && (c->a[0]!=0x008105D0u || c->a[1]!=0x008105E0u))return -1;
        if(c->function==0x001FB9F0u && (c->na<4 || c->a[1]!=0x1000u))return -1;
        if(c->function==0x001FAD70u && c->na<3)return -1;
        if(c->function==0x001FA790u && c->na<2)return -1;
        if(c->function==0x001FAE70u && c->na<1)return -1;
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=0;
        switch(c->function) {
        case 0x001AEDE0u: case 0x001AEE10u:
            em_frame_fade_start_colour(c->function==0x001AEDE0u ? 1 : -1,
                                      (int16_t)c->a[0],(uint8_t)c->a[1]);break;
        case 0x001AEDB0u:em_frame_fade_full((uint8_t)c->a[0]);break; /* [3]'s scene (AREA13) */
        case 0x001B1E20u:rc=em_pad_actuator_001B1E20((int32_t)c->a[0],(int64_t)c->a[1]);break;
        case 0x001B0250u:rc=em_scene_bindings_001B0250();break;
        case 0x0021B9A0u:rc=em_rcl_0021B9A0((int32_t)c->a[0],c->f[0],c->f[1]);break;
        case 0x001D2830u:rc=em_rcl_001D2830((int32_t)c->a[0],(int32_t)c->a[1]);break;
        case 0x001DD980u:rc=em_area11_interaction_host_camera_publish()==1 ? 0 : -1;break;
        case 0x001D25F0u:rc=em_rcl_001D25F0(c->f[0]);break;
        case 0x001FB9F0u:
            c->v0=(uint64_t)(int64_t)em_sfx_submit_001FB9F0_track((unsigned)c->a[0],
                                                               (int32_t)c->a[2],(int32_t)c->a[3]);break;
        case 0x001FAD70u:rc=em_scene_bindings_001FAD70((int32_t)c->a[0],(int32_t)c->a[1],(int32_t)c->a[2]);break;
        case 0x001FA790u:rc=em_scene_bindings_001FA790(NULL,(int32_t)c->a[0],(int32_t)c->a[1]) ? 0 : -1;break;
        case 0x001FABB0u:rc=em_scene_bindings_001FABB0();break;
        case 0x001FAE70u:rc=em_scene_bindings_001FAE70((int32_t)c->a[0]);break;
        case 0x001FBC50u:rc=em_scene_bindings_001FBC50();break;
        }
        if(em_area01_live_resume(l)<0)return -1;
        return rc;
    }
    case 0x0019AA80u: /* the AREA01 revisit's 0x8282F0 probe */
    case 0x0019A440u: /* 00133CD0's rays */
    case 0x0019A570u: case 0x0019A910u: case 0x0019B6C0u: case 0x0019AB20u:
    case 0x001A0B10u: case 0x0019D330u: case 0x001A7280u:
    case 0x0019F1A0u: case 0x0019ED80u: case 0x001A4030u: case 0x001A50A0u:
    case 0x001A5C30u: case 0x0019AD00u: case 0x0019AFE0u: case 0x0019B2C0u: {
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
    case 0x001AEBA0u:
        /* 001AEBA0(step) (byte-matched C): the screen fade (the lift [51]'s
         * sub-state 5, 001BD560, before its area change). */
        if(c->na<1)return -1;
        em_frame_screen_fade_start(-1,(int16_t)c->a[0]);
        return 0;
    case 0x0018D7B0u: {
        /* 0018D7B0(D_008101E0, style): the camera's solve (em_camera_live),
         * 001BC560's (the lift's op09). The views published around it. */
        if(c->na<2 || (uint32_t)c->a[0]!=0x008101E0u)return -1;
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=em_camera_live_solve((int)(int32_t)c->a[1]);
        if(em_area01_live_resume(l)<0)return -1;
        c->v0=(uint64_t)(int64_t)(rc<0 ? 0 : rc);return rc<0 ? -1 : 0;
    }
    case 0x0019BC40u: {
        /* 0019BC40(pos): the column table over the world (em_actor_collision,
         * NEARMISS logic recovered; ACTOR_COLLISION.md), for AREA04's
         * creature family. Committed actors, as 0019AD00 above. */
        if(c->na<1 || !em_collision_world_column_player())return -1;
        float pos[3];
        const uint8_t *p=bytes(l,(uint32_t)c->a[0],12,0);
        if(!p)return -1;
        memcpy(pos,p,12);
        if(em_area01_live_suspend(l)<0)return -1;
        int rc=em_actor_collision_player_column(em_collision_world_column_player(),pos,&s_column);
        if(em_area01_live_resume(l)<0)return -1;
        return rc<0 ? -1 : 0;
    }
    case 0x001B2E50u: {
        /* 001B2E50(a0, out) (byte-matched C): the first height of the column
         * table at or above a0's y, stored when its flag bit 0 is set; past
         * the table's end, the last one but one when the last has the bit
         * clear and that one has it set. */
        if(c->na<2)return -1;
        const uint8_t *q=bytes(l,(uint32_t)c->a[0]+4u,4,0);
        uint8_t *to=bytes(l,(uint32_t)c->a[1],4,1);
        if(!q || !to)return -1;
        uint32_t y;memcpy(&y,q,4);
        int32_t result=0,i=0;
        const int32_t count=s_column.count;
        if(count<0 || count>16)return -1;
        for(;i<count;++i) {
            uint32_t h;memcpy(&h,&s_column.height[i],4);
            if(!em_ee_c_lt_bits(h,y)) {
                if(s_column.flags[i]&1) { memcpy(to,&h,4);result=1; }
                c->v0=(uint64_t)(int64_t)result;return 0;
            }
        }
        const int32_t last=i-1;
        if(count && !(s_column.flags[last]&1) && last && (s_column.flags[last-1]&1)) {
            memcpy(to,&s_column.height[last-1],4);result=1;
        }
        c->v0=(uint64_t)(int64_t)result;return 0;
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
    case 0x00119828u: {
        /* 00119828(ch, l, r), the stream lanes' IOP command 0x16 (the
         * revisit's 001B6AE0; the scene bindings' em_stream_live binding,
         * as the AREA11 script host uses it). */
        /* Neither service touches the actor pool or the player, so the
         * live segment stays open: 001B6AE0 holds its view of the owner's
         * +0x1F0 block across these calls (a suspend would re-image the
         * record and drop its phase byte). */
        if(c->na<3)return -1;
        int rc=em_scene_bindings_00119828(NULL,(int32_t)c->a[0],(int32_t)c->a[1],(int32_t)c->a[2]);
        c->v0=0;return rc<0 ? -1 : 0;
    }
    case 0x001FD4C0u: {
        /* 001FD4C0(line), the message service's stream request (the AREA01
         * revisit's 001B6AE0 in script 0x82AD90; em_message_live, as the
         * AREA11 script host binds it). v0: 1 when the area has the line. */
        if(c->na<1)return -1;   /* no suspend: as 00119828 above */
        int rc=em_message_live_stream_request((int32_t)c->a[0]);
        if(rc<0)return -1;
        c->v0=(uint64_t)(int64_t)rc;return 0;
    }
    case 0x001C47A0u: {
        /* AREA01 revisit (0x825F00 gives item 0x20): 001C47A0(type)
         * (byte-matched decomp C; a1 reaches 001C40B0 untouched) runs
         * 001C40B0(type, a1), then D_008106B0 = 1, D_008106B1 = type; v0 0. */
        if(c->na<2 || em_area01_live_suspend(l)<0)return -1;
        int rc=em_pickup_inventory_001C40B0((int32_t)c->a[0],(int32_t)c->a[1]);
        if(em_area01_live_resume(l)<0 || rc<0)return -1;
        uint8_t *request=bytes(l,0x008106B0u,2,1);
        if(!request)return -1;
        request[0]=1;request[1]=(uint8_t)c->a[0];
        c->v0=0;return 0;
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
    case 0x00128250u: /* the soft-float float -> unsigned (the burst emitter's colour lanes) */
        if(c->nf<1)return -1;
        c->v0=(uint64_t)(int64_t)(int32_t)em_stream_lanes_00128250(c->f[0]);return 0;
    case 0x001B1470u:
        return c->nf<1 ? -1 : em_player_recovery_wrap(c->f[0],&c->f0);
    case 0x0011DF78u:
        if(c->nf<1)return -1;
        c->f0=em_ee_bits(em_sdk_math_original_0011DF78(em_ee_float(c->f[0])));return 0;
    case 0x0011E080u:
        if(c->nf<1)return -1;
        c->v0=(uint64_t)(int64_t)em_sdk_math_original_0011E080(em_ee_float(c->f[0]));return 0;
    case 0x0011DF98u:
        if(c->nf<1)return -1;
        c->f0=em_ee_bits(em_sdk_math_original_0011DF98(em_ee_float(c->f[0])));return 0;
    case 0x001F6640u: case 0x001F66F0u:
        if(c->na<1 || em_area01_live_suspend(l)<0)return -1;
        {
            int rc=em_effects_live_room_list(em_scene_state(),c->function,(uint32_t)c->a[0]);
            if(em_area01_live_resume(l)<0)return -1;
            return rc;
        }
    case 0x0011E2A8u: case 0x0011DE90u: case 0x0011E398u:
    case 0x0011DBB8u: case 0x0011E620u: case 0x0011E748u: case 0x0011CB90u:
        if(!sdk || c->nf<(c->function==0x0011E620u ? 2u : 1u))return -1;
        if(c->function==0x0011E2A8u)out=em_sdk_math_original_float_0011E2A8(sdk,em_ee_float(c->f[0]));
        else if(c->function==0x0011DE90u)out=em_sdk_math_original_float_0011DE90(sdk,em_ee_float(c->f[0]));
        else if(c->function==0x0011E398u)out=em_sdk_math_original_float_0011E398(sdk,em_ee_float(c->f[0]));
        else if(c->function==0x0011DBB8u)out=em_sdk_math_original_float_0011DBB8(sdk,em_ee_float(c->f[0]));
        else if(c->function==0x0011E748u)out=em_sdk_math_original_float_0011E748(sdk,em_ee_float(c->f[0]));
        else if(c->function==0x0011CB90u)out=em_sdk_math_original_0011CB90(em_ee_float(c->f[0]));
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
int em_area01_live_00199C50(EmArea01Live *l)
{
    if(!l || !l->bound)return -1;
    return em_area01_collision_view_00199C50(&l->collision)<0 ? fail(l,l->collision.fault_address) : 0;
}
int em_area01_live_bind(EmArea01Live *l,const EmArea01LiveHost *h)
{
    if(!l || !h || !h->pool || !h->scene || !h->area || !h->loader || !h->current_actor ||
       !h->resource_epoch || !h->rebind)
        return -1;
    const unsigned area=h->scene->d810700;
    if(!em_area01_state_supported(area) || h->area->area!=area)return -1;
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
    if(em_area01_model_bind(&l->model,&l->actors,&s)<0 || em_area01_runtime_bind_area(&l->runtime,&runtime,area)<0)
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
    collision.file_rest=collision_file_rest;
    collision.player_chain=player_chain;
    const uint8_t *count=em_module_loader_memory(h->loader,collision.file.d28A5A8,2);
    if(!count)return fail(l,0x0028A5A8u);
    memcpy(&collision.file.d28A5A8_value,count,2);
    if(em_area01_collision_view_bind(&l->collision,&collision)<0)return fail(l,l->collision.fault_address);
    const EmArea01RuntimeHost script={l,bytes,script_worker};
    if(em_area01_script_bind_area(&l->scripts,&script,h->loader,NULL,NULL,area)<0)return fail(l,0x001BA1A0u);
    em_area01_script_bank(&l->scripts,l,script_bank);
    l->hulls.host=(EmArea01HullHost){l,hull_record,resource_rest,hull_slot};
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
/* 00185420(item) for the status pages (the BATTERY page's device): the
 * record address in *owner, 0 for none. */
int em_area01_live_find_device(EmArea01Live *l,EmPlayerLiveActor *p,int32_t item,uint32_t *owner)
{
    if(!owner)return -1;
    EmArea01Call c={.function=0x00185420u,.a={(uint64_t)(int64_t)item},.na=1};
    int rc=em_area01_live_player_call(l,p,&c);
    if(rc>=0)*owner=(uint32_t)c.v0;
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
