/* Canonical live views and adapters to existing verified original owners. */
#include "game/em_aim_fire_binding.h"
#include "game/em_aim_fire_tables.h"
#include "game/em_aim_fire_sdk_memory.h"
#include "game/em_equipment_live.h"
#include "game/em_player_equipment.h"
#include "game/em_scene_bindings.h"
#include "game/em_scene_state.h"
#include "game/em_weapon.h"
#include "game/em_ee_float.h"
#include "game/em_effect_original.h"
#include "game/em_owner_services_original.h"
#include "game/em_script_host_workers.h"
#include "game/em_player_recovery.h"
#include "game/em_coll_probe_original.h"
#include "game/em_stream_lanes_original.h"
#include "game/em_locomotion_display.h"
#include "game/em_random.h"
#include "game/em_sdk_vu0.h"
#include "game/em_sfx.h"
#include <stdio.h>
#include <string.h>

#define TRY(x) do { if ((x)<0) return -1; } while (0)
static struct {
    EmAimFireLive live;
    EmAimFireBindingConfig config;
    /* Original pose work banks: 0017A130 writes every used node through
     * 00179BC0 / 00179CA0 before reading it. The other two banks are already
     * canonical in EmPlayerRecordPose (287F40..289B40). */
    uint8_t pose_work[0x1C00];
    int configured;
} B;
static struct {
    int (*call)(void *,EmAimFireLive *,EmAimFireTargetCall *);
    void *(*map)(void *,uint32_t,size_t,int);
    void *context;
    int (*settle)(void *);
    void *settle_context;
} Extension;
static void *span(uint32_t a,size_t n,uint32_t base,size_t count,void *p)
{
    return p && a>=base && n<=count && (uint64_t)a+n<=(uint64_t)base+count
        ? (uint8_t *)p+(a-base) : NULL;
}
static void *map(void *context,uint32_t a,size_t n,int write)
{
    (void)context;
    EmSceneState *s=em_scene_state();
    void *p;
    if ((p=span(a,n,0x8106B0,EM_SCENE_REQ_SIZE,s ? s->req : NULL))) return p;
    if ((p=span(a,n,0x810C61,1,em_weapon_fire_mode_byte()))) return p;
    if ((p=span(a,n,0x810C62,1,em_weapon_mag_byte()))) return p;
    if ((p=span(a,n,0x810CB4,2,em_weapon_reserve_word()))) return p;
    if ((p=span(a,n,0x810D3C,1,em_weapon_flashlight_byte()))) return p;
    if (s && n<=UINT32_MAX && (p=em_scene_progress_at(s,a,(uint32_t)n))) return p;
    if (s && (p=span(a,n,0x810E70,2,&s->d810E70))) return p;
    if (s && (p=span(a,n,0x810E74,2,&s->d810E74))) return p;
    if (s && (p=span(a,n,0x70003B68,4,&s->spad3B68))) return p;
    if ((p=span(a,n,0x286340,sizeof B.pose_work,B.pose_work))) return p;
    if (n<=UINT32_MAX && (p=em_equipment_live_field(a,(uint32_t)n,write))) return p;
    if (n<=UINT32_MAX && em_aim_fire_tables_contains(a,(uint32_t)n)) {
        if (write) return NULL;
        const uint8_t *t=em_aim_fire_tables_bytes(a,(uint32_t)n);
        if (!t && em_aim_fire_tables_load()==0) t=em_aim_fire_tables_bytes(a,(uint32_t)n);
        return (void *)t;
    }
    return Extension.map ? Extension.map(Extension.context,a,n,write) : NULL;
}
void *em_aim_fire_binding_bytes(uint32_t a,size_t n,int write)
{ return B.configured ? em_aim_fire_live_map(&B.live,a,n,write) : NULL; }
static int load(uint32_t a,void *out,size_t n)
{
    const void *p=em_aim_fire_binding_bytes(a,n,0); if (!p) return -1;
    memcpy(out,p,n);return 0;
}
static int aim_fire_binding_call(void *context,EmAimFireTargetCall *f)
{
    (void)context;
    int memory_status=em_aim_fire_sdk_memory_call(&B.live,em_aim_fire_live_map,f);
    if (memory_status!=1) return memory_status;
    const uint32_t a=(uint32_t)f->a[0],b=(uint32_t)f->a[1],c=(uint32_t)f->a[2];
    const uint32_t fn=f->function;
    EmSdkMathContext *sdk=B.config.sdk;
    float out[16],x[16];uint32_t bits[16];int32_t result;
    switch(fn) {
    case 0x1607D0:
        if (a!=B.live.player_address || !B.config.action) return -1;
        TRY(B.config.action(B.config.action_context,B.config.player,&result));f->v0=(uint32_t)result;return 0;
    case 0x1C61D0:
        TRY(em_pose_host_001C61D0(B.config.pose,a,(int16_t)b,&result));f->v0=(uint32_t)result;return 0;
    case 0x1749A0: case 0x1749F0: case 0x1C6DA0: {
        uint8_t *record=em_aim_fire_binding_bytes(a,0x320,1);
        if (!record) return -1;
        if (fn==0x1749A0) TRY(em_pose_host_001749A0(B.config.pose,record,0x320,(int16_t)b,(int)c,em_ee_float(f->f[0]),&result));
        else if (fn==0x1749F0) TRY(em_pose_host_001749F0(B.config.pose,record,0x320,(int16_t)b,em_ee_float(f->f[0]),em_ee_float(f->f[1]),&result));
        else return em_pose_host_001C6DA0(B.config.pose,record,0x320);
        f->v0=(uint32_t)result;return 0;
    }
    case 0x11E2A8: case 0x11DE90: case 0x11E620: case 0x11E748: case 0x11DBB8:
        if (!sdk) return -1;
        if (fn==0x11E2A8) out[0]=em_sdk_math_original_float_0011E2A8(sdk,em_ee_float(f->f[0]));
        else if (fn==0x11DE90) out[0]=em_sdk_math_original_float_0011DE90(sdk,em_ee_float(f->f[0]));
        else if (fn==0x11E620) out[0]=em_sdk_math_original_float_0011E620(sdk,em_ee_float(f->f[0]),em_ee_float(f->f[1]));
        else if (fn==0x11DBB8) out[0]=em_sdk_math_original_float_0011DBB8(sdk,em_ee_float(f->f[0]));
        else out[0]=em_sdk_math_original_float_0011E748(sdk,em_ee_float(f->f[0]));
        if (sdk->fault) return -1;
        f->f0=em_ee_bits(out[0]);return 0;
    case 0x11DF78:f->f0=em_ee_bits(em_sdk_math_original_0011DF78(em_ee_float(f->f[0])));return 0;
    case 0x11E860:f->v0=(uint32_t)em_effect_original_0011E860((int32_t)a);return 0;
    case 0x1281C0:f->v0=(uint32_t)em_stream_lanes_001281C0(f->f[0]);return 0;
    case 0x122BB8:f->v0=em_random_next();return 0;
    case 0x1B0070:
        if (!em_scene_state()) return -1;
        f->v0=em_scene_req_u32(em_scene_state(),EM_SCENE_REQ_C8);return 0;
    case 0x15D2F0:
        TRY(em_player_equipment_0015D2F0(B.config.player->bytes,&result));
        f->v0=(uint32_t)result;return 0;
    case 0x1B1470:return em_player_recovery_wrap(f->f[0],&f->f0);
    case 0x1B1240: {
        if (!sdk) return -1;
        TRY(load(a,bits,12)); EmScriptHostWorkers h={0};
        h.world.sdk_tables=sdk->tables;h.world.sdk_world=&sdk->world;h.world.sdk_workers=&sdk->workers;
        return em_script_host_001B1240(&h,bits,f->f[0],f->f[1],&f->f0);
    }
    case 0x1B12B0:return em_script_host_approach(NULL,f->f[0],f->f[1],f->f[2],&f->f0);
    case 0x1FBD50:
        if (c) return -1;
        TRY(load(a+0xB0,x,12));f->v0=(uint32_t)em_sfx_play_at_track(b,x,em_ee_float(f->f[0]));return 0;
    case 0x1FB9F0:
        if (b!=0x1000 || c!=0x1000 || f->a[3]!=0x1000) return -1;
        em_sfx_play(a);return 0;
    case 0x11A070:return em_sfx_stop_track((int)a&0x7FFF,(a&0x8000)!=0);
    default:return Extension.call ? Extension.call(Extension.context,&B.live,f) : -1;
    }
}
int em_aim_fire_binding_configure(const EmAimFireBindingConfig *config)
{
    if (!config || !config->player || !config->pose || !config->sdk || !config->bone_array || !config->action) return -1;
    memset(&B,0,sizeof B);B.config=*config;
    B.live.context=&B;B.live.player=config->player;B.live.player_address=0x8102B0;
    B.live.pose=config->pose;B.live.regions=config->regions;B.live.region_count=config->region_count;
    B.live.map=map;B.live.call=aim_fire_binding_call;B.configured=1;return 0;
}
void em_aim_fire_binding_set_extension(int (*fn)(void *,EmAimFireLive *,EmAimFireTargetCall *),
                                       void *(*view)(void *,uint32_t,size_t,int),void *context)
{ Extension.call=fn;Extension.map=view;Extension.context=context; }
void em_aim_fire_binding_set_settle(int (*settle)(void *),void *context)
{ Extension.settle=settle;Extension.settle_context=context; }
EmAimFireLive *em_aim_fire_binding_host(void) { return B.configured ? &B.live : NULL; }
int em_aim_fire_binding_ready(void) { return B.configured; }
int em_aim_fire_binding_frame(EmAimFireTargetCall *frame)
{
    if (!B.configured || !frame) return -1;
    if (!B.live.depth) TRY(em_aim_fire_live_clear_fault(&B.live));
    int result=em_aim_fire_live_call(&B.live,frame);
    if (!B.live.depth && Extension.settle && Extension.settle(Extension.settle_context)<0 && result>=0) {
        if (!B.live.fault_function) B.live.fault_function=frame->function;
        result=-1;
    }
    if (result<0 && !B.live.depth)
        fprintf(stderr,"aim/fire: original %08X faulted at function %08X address %08X\n",
                frame->function,B.live.fault_function,B.live.fault_address);
    return result;
}
int em_aim_fire_binding_run(uint32_t entry,uint32_t actor)
{
    if (!B.configured) return -1;
    uint32_t previous=*B.config.bone_array;
    uint32_t bones=em_equipment_live_current_bones();
    if (bones) *B.config.bone_array=bones;
    EmAimFireTargetCall f={0};f.function=entry;f.a[0]=actor;f.na=1;
    int result=em_aim_fire_binding_frame(&f);
    /* D_00275B40 selects the running owner's bone table. Equipment's
     * temporary selection must not leak into the next player callback. */
    *B.config.bone_array=previous;
    return result;
}
