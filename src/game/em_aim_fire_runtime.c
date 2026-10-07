#include "game/em_aim_fire_runtime.h"
#include "game/em_collision_world.h"
#include "game/em_pad_actuator.h"
#include "game/em_aim_fire_binding.h"
#include "game/em_aim_fire_cable_live.h"
#include "game/em_aim_fire_flash.h"
#include "game/em_aim_fire_trail.h"
#include "game/em_bone_burst.h"
#include "game/em_effect_001F77B0.h"
#include "game/em_game_internal.h"
#include "game/em_random.h"
#include "game/em_sdk_math_original.h"
#include "game/em_shadow_live.h"
#include "game/em_aim_fire_leaves.h"
#include "game/em_area11_roger.h"
#include "game/em_aim_fire_render_live.h"
#include "game/em_aim_fire_world_live.h"
#include "game/em_area00_low.h"
#include "game/em_effects_live.h"
#include "game/em_equipment_live.h"
#include "game/em_render_context_live.h"
#include "game/em_area11_boxes.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_camera_live.h"
#include "game/em_owner_services_original.h"
#include "game/em_owner_draw_live.h"
#include "game/em_scene_bindings.h"
#include "game/em_startup_load_gaps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRY(x) do { if ((x)<0) return -1; } while (0)

/* Original BSS owners. The executable's phase word is not an area-reset
 * value. The cable worker writes each point before the strip drawer reads it. */
static uint32_t target_phase;             /* D_00275B00's first word only */
static uint32_t cable_points[12][4];       /* D_00821400..D_008214BF */
/* Scratchpad words no other owner holds, which the aim / fire routines write
 * before they read them in the same call (AIM_FIRE.md section 3): the laser
 * dot's 0x700038C0 / 0x700038D0 (001854E0, 00185760), the shot's hit and
 * normal 0x700038C0..0x700038EF (001861C0), and 0x700031E8, the shot's
 * flag (only 001861C0 and 00186A60 address it; each stores it before
 * reading it). */
static uint32_t scratch_38C0[16];          /* 0x700038C0..0x700038FF */
static uint32_t shot_flag;                 /* 0x700031E8 */
/* 0x70003600..0x7000363F: the sight sprite 001CD520 (0x70003600..1F), the
 * beam 001E2BA0 and the hit call 001B41F0 (0x70003600 / 0x70003610) write
 * them before reading them. 001CD520 loads the quadword 0x70003610 after
 * storing three of its words; the fourth (0x7000361C) is loaded but not
 * used (its transform takes the fourth row's factor from vf0.w;
 * PLAYER_EQUIPMENT.md section 5). AIM_FIRE.md section 7. */
static uint32_t scratch_3600[16];          /* 0x70003600..0x7000363F */
static uint32_t *shared_3600;
static uint32_t d275C3C;                   /* D_00275C3C (001F0190 / 001F0290) */

/* The records the composition allocates itself (001861C0's 001AFA90(1),
 * the impact marker): the bytes beyond the pool header the marker
 * 0018ABA0 writes before it reads them, +0x28 (its countdown halfword)
 * and +0xA0..+0xAF (its normal), with a written mask per byte (a read of a
 * byte nobody wrote since the allocation faults: 001AFA90 / 001AFC10 leave
 * them as the previous record had them). AIM_FIRE.md section 7. */
typedef struct {
    uint32_t generation;
    uint8_t used, bound;
    uint8_t b28[2], a0[16];
    uint8_t w28[2], wa0[16];
} AimRecord;

static struct {
    EmActorPool *pool;
    AimRecord rec[EM_ACTOR_POOL_CAPACITY];
    EmActor *pending[8];
    unsigned npending;
    int (*bind)(EmActor *);
    EmAimFireWorldLive world;
    EmAimFireRenderLive render;
    EmArea00HudVu vu;
    int vf23_valid;
    uint32_t self_word[EM_ACTOR_POOL_CAPACITY]; /* original pointer encodings */
    EmPoseRegion bss[14];
    uint32_t d28A56C;                    /* D_0028A490[0x37], read-only */
} R;

int em_aim_fire_runtime_scratch_3600_bind(uint32_t *words16)
{
    if (!words16 && shared_3600) memcpy(scratch_3600,shared_3600,sizeof scratch_3600);
    shared_3600=words16;
    R.bss[4]=(EmPoseRegion){0x70003600,sizeof scratch_3600,
                           (uint8_t *)(shared_3600 ? shared_3600 : scratch_3600),1};
    return 0;
}
const uint32_t *em_aim_fire_runtime_scratch_3600(void)
{ return shared_3600 ? shared_3600 : scratch_3600; }
uint32_t *em_aim_fire_runtime_scratch_38C0(void) { return scratch_38C0; }
EmAimFireWorldLive *em_aim_fire_runtime_world_state(void) { return &R.world; }
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
    FIELD(flags2,0x2E); FIELD(w30,0x30);
    /* 00185A10/00185E30 read the class-2 target's +34 halfword. The
     * original word overlaps +36, whose canonical owner is h36. */
    if ((p=span(address,size,base+0x34,2,&a->w34))) return p;
    FIELD(h36,0x36);
    FIELD(h52,0x52); FIELD(kind,0x54); FIELD(link,0x56); FIELD(w58,0x58); FIELD(w5C,0x5C);
    FIELD(f60,0x60); FIELD(f80,0x80); FIELD(w90,0x90); FIELD(h94,0x94); FIELD(h96,0x96);
    FIELD(b98,0x98); FIELD(b99,0x99); FIELD(table_index,0x9A);
    FIELD(b9C,0x9C); FIELD(b9D,0x9D); FIELD(b9E,0x9E); FIELD(pos,0xB0); FIELD(rot,0xC0);
#undef FIELD
    {
        AimRecord *r=&R.rec[index];
        if (r->used && r->generation==a->generation) {
            uint8_t *bytes=NULL,*mask=NULL;
            if ((p=span(address,size,base+0x28,2,r->b28))) { bytes=p; mask=r->w28+(address-base-0x28); }
            else if ((p=span(address,size,base+0xA0,16,r->a0))) { bytes=p; mask=r->wa0+(address-base-0xA0); }
            if (bytes) {
                if (write) { memset(mask,1,size); return bytes; }
                for (size_t k=0;k<size;++k) if (!mask[k]) return NULL;
                return bytes;
            }
        }
    }
    /* The matrix belongs to the bound world owner, not the pool header. */
    return span(address,size,base+0xD0,64,em_area11_boxes_owner_world(a));
}
/* The original address of a grid node (*0x700031D0 after a grid hit) and
 * its record bytes: the area data the loader delivered (em_scene_bindings). */
static uint32_t grid_address(void *context,const EmCollProbeGrid *grid,uint32_t node)
{
    (void)context;(void)grid;
    return em_scene_bindings_grid_node_address(node);
}
static const uint8_t *grid_bytes(void *context,uint32_t address,uint32_t size)
{
    (void)context;
    return em_scene_bindings_grid_node_bytes(address,size);
}
static void *render_map(void *context,uint32_t a,size_t n,int write)
{ (void)context;return em_aim_fire_binding_bytes(a,n,write); }
static int render_forward(void *context,EmAimFireTargetCall *f)
{ (void)context;return em_aim_fire_binding_frame(f); }
/* Read-only words the muzzle node's 001F5040 loads: D_0028A56C (the
 * library's table word, D_0028A490[0x37]: the Roger export) and the free
 * slot count D_00275BCC (the one stack's, em_area11_boxes). */
static void *library_globals(uint32_t a,size_t n)
{
    static uint32_t d28A56C;
    if (a==0x28A56Cu && n==4) {
        if (em_area11_roger_table_word(0x28A56Cu,&d28A56C)<0) return NULL;
        return &d28A56C;
    }
    if (a==0x275BCCu && n==2) {
        const EmRogerActorWorld *slots=em_area11_boxes_slot_world();
        return slots && slots->d00275BCC ? (void *)slots->d00275BCC : NULL;
    }
    return NULL;
}
static void *external_map(void *context,uint32_t a,size_t n,int write)
{
    (void)context;
    if (!n || n>UINT32_MAX || (uint64_t)a+n>UINT64_C(0x100000000)) return NULL;
    void *p=em_aim_fire_flash_field(a,n,write);
    if (!p) p=em_aim_fire_trail_field(a,n,write);
    if (!p) p=em_bone_burst_field(a,n,write);
    if (!p) {
        EmEffectsLiveNodeRegion particles[3];
        size_t np=em_effects_live_particle_regions(particles,3);
        for (size_t k=0;!p && k<np && k<3;++k)
            p=span(a,n,particles[k].address,particles[k].size,particles[k].bytes);
    }
    if (!p) p=write ? em_rcl_bytes_mut(a,(uint32_t)n) : (void *)em_rcl_bytes(a,(uint32_t)n);
    if (!p && !write) p=library_globals(a,n);
    if (!p) p=em_effects_live_node_field(a,n,write);
    if (!p) p=em_aim_fire_world_live_map(&R.world,a,n,write);
    if (!p) p=pool_field(a,n,write);
    if (!p && !write) p=(void *)em_scene_bindings_target_model_bytes(a,(uint32_t)n);
    if (!p && !write) p=(void *)em_effects_live_window(a,(uint32_t)n);
    if (!p && !write) p=(void *)em_scene_bindings_grid_node_bytes(a,(uint32_t)n);
    /* D_008105D0, the camera's eye (00187780's view attenuation reads it):
     * the live camera's own word, read only. */
    if (!p && !write && a>=0x008105D0u && (uint64_t)a+n<=0x008105E0u)
        p=em_camera_live_bytes(a,(uint32_t)n);
    return p;
}
static int append(EmPoseRegion *out,unsigned capacity,unsigned *count,
                  uint32_t address,uint32_t size,void *bytes,int write)
{
    if (!bytes) return 0;
    if (*count>=capacity) {
        fprintf(stderr,"aim/fire runtime: view list full at %08X\n",(unsigned)address);
        return -1;
    }
    out[(*count)++]=(EmPoseRegion){address,size,bytes,write};return 0;
}
static int enumerate(void *context,EmPoseRegion *out,unsigned capacity,unsigned *count)
{
    (void)context;
    const EmCollisionWorldOwners *owners=em_collision_world_owners();
    if (!R.pool || !count) return -1;
    {
        /* The particle records D_007709C0 and D_00275C40 / D_00275C44 (the
         * shell casing's 001F4010 seeds one, the barrel's sweep moves and
         * draws it: em_effects_live's storage). */
        EmEffectsLiveNodeRegion particles[3];
        size_t np=em_effects_live_particle_regions(particles,3);
        if (np>3) return -1;
        for (size_t k=0;k<np;++k)
            TRY(append(out,capacity,count,particles[k].address,(uint32_t)particles[k].size,particles[k].bytes,1));
    }
    {
        /* The render context's storage (the muzzle node's lines read the
         * context, the clip matrix 001CD370 names and the scratchpad
         * view-projection; 001CB5F0 / 001CB6B0 / 001CB900 write the arena). */
        uint32_t address[32],size[32];uint8_t *bytes[32];int writable[32];
        unsigned n=em_rcl_views(address,size,bytes,writable,32);
        if (n>32) return -1;
        for (unsigned k=0;k<n;++k) TRY(append(out,capacity,count,address[k],size[k],bytes[k],writable[k]));
    }
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
        if (em_aim_fire_flash_owns(a)) {
            EmPoseRegion flash[16];
            size_t flash_count=em_aim_fire_flash_regions(address,flash,16);
            if (flash_count>16) return -1;
            for (size_t j=0;j<flash_count;++j)
                TRY(append(out,capacity,count,flash[j].address,flash[j].size,flash[j].bytes,flash[j].writable));
            /* The pool header, +0x14 and the EmActor fields 001F5040 /
             * 00187CC0 address: +0x60..+0x8F, +0xB0, +0xC0. */
            static const uint32_t spans[][2]={{0,0x14},{0x14,4},{0x2E,2},{0x60,0x20},{0x80,16},
                {0xB0,16},{0xC0,16}};
            for (unsigned j=0;j<sizeof spans/sizeof spans[0];++j) {
                uint32_t at=address+spans[j][0],nn=spans[j][1];
                TRY(append(out,capacity,count,at,nn,pool_field(at,nn,0),spans[j][0]!=0x14));
            }
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
            /* The knife's trail node and a debris node: their +0x110 words
             * and their slots. */
            EmPoseRegion trail[57];
            size_t trail_count=em_aim_fire_trail_regions(address,trail,57);
            if (trail_count>57) return -1;
            for (size_t j=0;j<trail_count;++j)
                TRY(append(out,capacity,count,trail[j].address,trail[j].size,trail[j].bytes,trail[j].writable));
            /* The bone-burst node (0022BBC0): its +0x110 words and slots. */
            EmPoseRegion burst[6];
            size_t burst_count=em_bone_burst_regions(address,burst,6);
            if (burst_count>6) return -1;
            for (size_t j=0;j<burst_count;++j)
                TRY(append(out,capacity,count,burst[j].address,burst[j].size,burst[j].bytes,burst[j].writable));
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
        static const uint32_t spans[][2]={{0,0x14},{0x14,4},{0x2E,2},{0x34,2},{0x36,2},
            {0xB0,16},{0xC0,16},{0xD0,64},{0x28,2},{0xA0,16}};
        for (unsigned j=0;j<sizeof spans/sizeof spans[0];++j) {
            uint32_t at=address+spans[j][0],n=spans[j][1];
            p=pool_field(at,n,0);
            TRY(append(out,capacity,count,at,n,p,spans[j][0]!=0x14));
        }
    }
    return 0;
}
/* AREA01's canonical private slot words or shared model projection, and
 * the one existing bone arena. Only the current target is projected: shared
 * providers may reuse their serialization buffer on the next actor query. */
static int target_regions(void *context,uint32_t node,EmPoseRegion *out,
                          unsigned capacity,unsigned *count)
{
    (void)context;
    if(!R.pool || !count || node<EM_ACTOR_POOL_BASE)return 0;
    uint32_t delta=node-EM_ACTOR_POOL_BASE;
    if(delta%EM_ACTOR_RECORD_SIZE || delta/EM_ACTOR_RECORD_SIZE>=EM_ACTOR_POOL_CAPACITY)return 0;
    const EmActor *a=&R.pool->records[delta/EM_ACTOR_RECORD_SIZE];
    if(!a->allocated || a->self!=a || !a->bones || a->bones>56u)return 0;
    const uint8_t *words=em_scene_bindings_target_model_bytes(node+0x110u,4u*a->bones);
    if(!words)return 0;
    TRY(append(out,capacity,count,node+0x110u,4u*a->bones,(void *)words,0));
    uint32_t size=EM_SLG_BONE_SLOTS*EM_SLG_BONE_SLOT_SIZE;
    const uint8_t *slots=em_scene_bindings_target_model_bytes(EM_SLG_BONE_RECORDS,size);
    TRY(append(out,capacity,count,EM_SLG_BONE_RECORDS,size,(void *)slots,0));
    return 0;
}
static int written(void *context,uint32_t address,size_t size)
{ (void)context;return em_effects_live_node_written(address,size); }
static int external_call(void *context,EmAimFireLive *live,EmAimFireTargetCall *f)
{
    (void)context;
    {
        const int flash=em_aim_fire_flash_call(f);
        if (flash<0) return -1;
        if (flash>0) return 0;
        const int trail=em_aim_fire_trail_call(f);
        if (trail<0) return -1;
        if (trail>0) return 0;
        const int burst=em_bone_burst_call(f);
        if (burst<0) return -1;
        if (burst>0) return 0;
    }
    switch (f->function) {
    case 0x1CA7B0: {
        /* 001F3E30's cull of its matrix row 3 (em_owner_draw_live's). */
        const void *p=em_aim_fire_live_map(live,(uint32_t)f->a[0]&~15u,16,0);
        if (!p) return -1;
        uint32_t position[4];int32_t flags=0;memcpy(position,p,16);
        TRY(em_owner_draw_live_001CA7B0(position,f->f[0],&flags));
        f->v0=(uint64_t)(int64_t)flags;return 0;
    }
    case 0x1C7900: {
        /* 001F3E30's 001C7900(m, token, 0x3F5, 0): the colour and node CNTs
         * on channel 0 (em_owner_draw_live). */
        const void *m=em_aim_fire_live_map(live,(uint32_t)f->a[0],64,0);
        const void *t=em_aim_fire_live_map(live,(uint32_t)f->a[1],16,0);
        if (!m || !t) return -1;
        uint32_t matrix[16];uint8_t token[16];memcpy(matrix,m,64);memcpy(token,t,16);
        return em_owner_draw_live_001C7900(matrix,(uint32_t)f->a[1],token,(int32_t)f->a[2],(int32_t)f->a[3]);
    }
    case 0x1AA7A0: {
        /* 001AA7A0(a0, e) (001AA840's): the node *D_00275B40's +0xC0..+0xC8
         * against the entry's +0xB0..+0xB8 (em_aim_fire_leaves); a reach
         * stores the entry's +0x36 = 1 (the cable's hit). */
        const uint8_t *table=em_aim_fire_live_map(live,0x275B40u,4,0);
        if (!table) return -1;
        uint32_t node;memcpy(&node,table,4);
        const uint8_t *table0=em_aim_fire_live_map(live,node,4,0);
        if (!table0) return -1;
        uint32_t bone;memcpy(&bone,table0,4);
        const uint8_t *c0=em_aim_fire_live_map(live,bone+0xC0u,12,0);
        const uint8_t *b0=em_aim_fire_live_map(live,(uint32_t)f->a[1]+0xB0u,12,0);
        if (!c0 || !b0) return -1;
        uint32_t n[3],e[3];int hit=0;memcpy(n,c0,12);memcpy(e,b0,12);
        f->v0=em_aim_fire_001AA7A0(n,e,&hit);
        if (hit) {
            uint8_t *h36=em_aim_fire_live_map(live,(uint32_t)f->a[1]+0x36u,2,1);
            if (!h36) return -1;
            h36[0]=1;h36[1]=0;
        }
        return 0;
    }
    case 0x1CA940:
        /* 001F3E30's 001CA940(flags, model): a library model's REFs, the
         * unit kept with the frame's units (em_owner_draw_live). */
        return em_owner_draw_live_001CA940_library((int32_t)f->a[0],(uint32_t)f->a[1]);
    case 0x1CD520: case 0x1E2BA0: case 0x1DD170: case 0x1DD2F0: case 0x1DD600:
        /* Untracked render owners run between actor calls. Their last vf23
         * write is not exposed yet, so an ambient reticle input is unknown. */
        R.vf23_valid=0;
        return em_aim_fire_render_live_call(&R.render,f);
    case 0x1CD370: case 0x1CB5F0: case 0x1CB6B0: case 0x1CB900:
        return em_aim_fire_render_live_call(&R.render,f);
    case 0x102870: {
        /* The beam's (001E2BA0) quadword divide: em_area00_low's translation
         * over the two quadwords it addresses (low four bits ignored). */
        uint32_t to=(uint32_t)f->a[0]&~15u,from=(uint32_t)f->a[1]&~15u;
        uint8_t *src=em_aim_fire_live_map(live,from,16,0),*dst=em_aim_fire_live_map(live,to,16,1);
        if (!src || !dst) return -1;
        EmArea00LowRegion region[2]={{from,16,src},{to,16,dst}};
        EmArea00Low low;memset(&low,0,sizeof low);low.regions=region;low.region_count=2;
        return em_area00_low_00102870(&low,(uint32_t)f->a[0],(uint32_t)f->a[1],f->f[0])<0 ? -1 : 0;
    }
    case 0x102990: {
        /* The knife trail's colour words (001F15F0's two 00102990 calls into
         * its packet block): em_area00_low's translation, VFTOI0 of the
         * quadword, over the two quadwords it addresses (low four bits
         * ignored). */
        uint32_t to=(uint32_t)f->a[0]&~15u,from=(uint32_t)f->a[1]&~15u;
        uint8_t *src=em_aim_fire_live_map(live,from,16,0),*dst=em_aim_fire_live_map(live,to,16,1);
        if (!src || !dst) return -1;
        EmArea00LowRegion region[2]={{from,16,src},{to,16,dst}};
        EmArea00Low low;memset(&low,0,sizeof low);low.regions=region;low.region_count=2;
        return em_area00_low_00102990(&low,(uint32_t)f->a[0],(uint32_t)f->a[1])<0 ? -1 : 0;
    }
    case 0x1EFEB0: case 0x1CE860:return em_aim_fire_cable_live_call(live,f);
    case 0x21AAC0: case 0x21A500: {
        int status=em_aim_fire_cable_live_tick(live,(uint32_t)f->a[0],f->function);
        if (status<0) return -1;
        f->v0=(uint32_t)status;return 0;
    }
    case 0x1AFA90: {
        /* 001861C0's 001AFA90(1): a pool record of the given class (the
         * byte), bound to its behaviour by its +0x10 when the root call
         * returns (settle). A refused allocation answers 0, as the
         * original. */
        EmSceneState *scene=em_scene_state();
        if (!R.pool || !scene) return -1;
        if (R.npending==sizeof R.pending/sizeof R.pending[0]) return -1;
        EmActor *a=em_actor_pool_alloc_001AFA90(R.pool,scene,(uint8_t)f->a[0]);
        if (!a) { if (em_scene_faulted(scene)) return -1; f->v0=0; return 0; }
        unsigned i=(unsigned)(a-R.pool->records);
        memset(&R.rec[i],0,sizeof R.rec[i]);
        R.rec[i].generation=a->generation;R.rec[i].used=1;
        /* Class 0xC is 001F4F40's muzzle node (its only 001AFA90 here):
         * its bytes are em_aim_fire_flash's. */
        if ((uint8_t)f->a[0]==0xC) {
            R.rec[i].used=0;
            if (em_aim_fire_flash_claim(a)<0) return -1;
        }
        R.pending[R.npending++]=a;
        f->v0=em_actor_pool_address(R.pool,a);return 0;
    }
    case 0x1B17A0: {
        /* 001B17A0(node) of a record this composition allocated (the
         * marker, the muzzle node): the interaction host's byte-matched
         * translation over the record's
         * +0x02 / +0x03 / +0x0D / +0x2E / +0xB0..+0xB8; it stores +0x01. */
        EmActor *a=pool_actor((uint32_t)f->a[0]);
        if (!a || !a->allocated) return -1;
        unsigned i=(unsigned)(a-R.pool->records);
        if ((!R.rec[i].used && !em_aim_fire_flash_owns(a)) || R.rec[i].generation!=a->generation) return -1;
        EmOwnerServicesOwner o;memset(&o,0,sizeof o);
        o.cls=a->cls;o.kind=a->model;o.model_id=a->param;o.flags2=a->flags2;
        memcpy(o.pos,a->pos,sizeof o.pos);
        int drawn=em_area11_interaction_host_offer_001B17A0(a,&o);
        if (drawn<0) return -1;
        f->v0=(uint32_t)a->drawn;return 0;
    }
    case 0x1EFD20: {
        float position[4];
        const void *p=em_aim_fire_live_map(live,(uint32_t)f->a[1],16,0);
        if (!p) return -1;
        memcpy(position,p,16);
        return em_effects_live_001EFD20((uint32_t)f->a[0],position);
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
        if ((uint32_t)f->a[2]==0x002487E0u) {
            /* The lamp's flare (00187690): its source is the writable .data
             * block D_002487E0..D_0024886F (the player record pose's span
             * of the locomotion tables, its one owner), copied into the
             * packet (copy 1). */
            const uint8_t *flare=em_aim_fire_live_map(live,0x002487E0u,0x90,0);
            if (!flare) return -1;
            return em_effects_live_001CFBE0_bytes((int32_t)f->a[0],(int32_t)f->a[1],0x002487E0u,flare,
                                                  block,(int32_t)f->a[4]);
        }
        return em_effects_live_001CFBE0((int32_t)f->a[0],(int32_t)f->a[1],(uint32_t)f->a[2],block,(int32_t)f->a[4]);
    }
    case 0x21B9A0:
        /* 0021B9A0(mode, f12, f13): the render context's parameter rows
         * (em_render_context_live, its one owner). */
        return em_rcl_0021B9A0((int32_t)f->a[0],f->f[0],f->f[1]);
    case 0x1B61C0:
        /* 001B61C0(big, small, duration, force): the pad actuator request
         * (0018A180's knife hit: (0, 0xD0, 0x0A, 1)), through its one owner
         * em_pad_actuator (em_player_rumble_001B61C0 over the block). */
        if (em_pad_actuator_001B61C0((uint8_t)f->a[0],(uint8_t)f->a[1],(int)(int32_t)f->a[2],
                                     (int)(int32_t)f->a[3])<0) return -1;
        return 0;   /* its v0 is unused by 0018A180, its one caller here */
    case 0x1D9530:
        /* The lamp's cone shells: 00187780 calls it only when D_008106C8
         * lacks 0x20000000, which AREA11 sets in every capture (decomp
         * CURIOSITIES "The gun light draws"). Not composed. */
        fprintf(stderr,"aim/fire: 001D9530 (the lamp's cone shells) is not composed: D_008106C8 lacks 0x20000000\n");
        return -1;
    default:return em_aim_fire_world_live_call(&R.world,live,f);
    }
}
/* The root call returned: bind the records it allocated by their +0x10. */
static int settle(void *context)
{
    (void)context;
    int rc=0;
    for (unsigned k=0;k<R.npending;++k) {
        EmActor *a=R.pending[k];
        unsigned i=(unsigned)(a-R.pool->records);
        if (!a->allocated || R.rec[i].generation!=a->generation) continue;   /* freed again */
        const int flash=em_aim_fire_flash_owns(a);
        if (a->callback!=(flash ? EM_AIM_FIRE_FLASH_CALLBACK : 0x0018ABA0u) || !R.bind || R.bind(a)<0) {
            rc=-1;continue;
        }
        R.rec[i].bound=1;
    }
    R.npending=0;
    return rc;
}
void em_aim_fire_runtime_set_bind(int (*bind)(EmActor *)) { R.bind=bind; }
int em_aim_fire_runtime_h28(const EmActor *actor,uint16_t *value)
{
    if (!R.pool || !actor || !value || actor<R.pool->records || actor>=R.pool->records+EM_ACTOR_POOL_CAPACITY)
        return 0;
    if (em_aim_fire_flash_h28(actor,value)) return 1;
    const AimRecord *r=&R.rec[actor-R.pool->records];
    if (!r->used || r->generation!=actor->generation || !r->w28[0] || !r->w28[1]) return 0;
    *value=(uint16_t)(r->b28[0]|r->b28[1]<<8);return 1;
}
int em_aim_fire_runtime_tick(EmActor *actor)
{
    if (!R.pool || !actor) return -1;
    unsigned i=(unsigned)(actor-R.pool->records);
    if (i>=EM_ACTOR_POOL_CAPACITY || R.rec[i].generation!=actor->generation) return -1;
    const uint32_t address=em_actor_pool_address(R.pool,actor);
    if (actor->callback==EM_AIM_FIRE_FLASH_CALLBACK && em_aim_fire_flash_owns(actor)) {
        /* 001F5040 with D_00275B40 = its own +0x110, as the walk's 001CB590
         * sets it before the callback. */
        em_aim_fire_flash_set_current(actor);
        const int rc=em_aim_fire_binding_run_bones(actor->callback,address,address+0x110u);
        em_aim_fire_flash_set_current(NULL);
        return rc<0 ? -1 : 1;
    }
    if (!R.rec[i].used || actor->callback!=0x0018ABA0u) return -1;
    EmAimFireTargetCall frame={0};
    frame.function=actor->callback;frame.a[0]=address;frame.na=1;
    return em_aim_fire_binding_frame(&frame)<0 ? -1 : 1;
}
/* The close-out passes' view of this composition's own records (the
 * impact markers on the class-1 list; 001A9D20 reads their +0 and
 * +0xB0..+0xB8): the pool header and the named fields, as pool_field. */
static uint8_t *collision_records(void *context,uint32_t address,uint32_t size)
{
    (void)context;
    if (!R.pool || address<EM_ACTOR_POOL_BASE) return NULL;
    unsigned i=(address-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE;
    if (i>=EM_ACTOR_POOL_CAPACITY) return NULL;
    const EmActor *a=&R.pool->records[i];
    if (!a->allocated || !R.rec[i].used || R.rec[i].generation!=a->generation) return NULL;
    return pool_field(address,size,0);
}
void em_aim_fire_runtime_attach(EmActorPool *pool)
{
    memset(&R,0,sizeof R);R.pool=pool;
    R.world.enumerate=enumerate;
    R.world.target_regions=target_regions;
    R.world.grid_address=grid_address;
    R.world.grid_bytes=grid_bytes;
    R.world.written=written;
    R.bss[0]=(EmPoseRegion){0x275B00,sizeof target_phase,(uint8_t *)&target_phase,1};
    R.bss[1]=(EmPoseRegion){0x821400,sizeof cable_points,(uint8_t *)cable_points,1};
    R.bss[2]=(EmPoseRegion){0x700038C0,sizeof scratch_38C0,(uint8_t *)scratch_38C0,1};
    R.bss[3]=(EmPoseRegion){0x700031E8,sizeof shot_flag,(uint8_t *)&shot_flag,1};
    R.bss[4]=(EmPoseRegion){0x70003600,sizeof scratch_3600,
                           (uint8_t *)(shared_3600 ? shared_3600 : scratch_3600),1};
    /* D_00275C3C: the count 001F0190 keeps of its 0021B9A0 calls, which
     * 001F0290 tests (the bone burst's fog bracket; no other reader). */
    R.bss[5]=(EmPoseRegion){0x275C3C,sizeof d275C3C,(uint8_t *)&d275C3C,1};
    R.world.regions=R.bss;R.world.region_count=6;
    R.render.map=render_map;R.render.call=render_forward;
    R.render.vu=&R.vu;R.render.vf23_valid=&R.vf23_valid;
    em_aim_fire_binding_set_extension(external_call,external_map,NULL);
    em_aim_fire_binding_set_settle(settle,NULL);
    em_collision_world_bind_records(collision_records,NULL);
}
/* ---- 001F77B0, the effect 0x80000043's node (the death decal;
 * em_effect_001F77B0, docs/DAMAGE.md section 4) ---- */
static int dd_rand(void *c,int32_t *v) { (void)c;*v=(int32_t)em_random_next();return 0; }
static int dd_sincos(uint32_t x,uint32_t *r,int cosine)
{
    EmSdkMathContext *sdk=em_collision_world_sdk();
    float f=0.0f,in;memcpy(&in,&x,4);
    if (!sdk) return -1;
    const int rc=cosine ? em_sdk_math_original_w_0011DE90(sdk,in,&f) : em_sdk_math_original_w_0011E2A8(sdk,in,&f);
    if (rc<0 || sdk->fault) return -1;
    memcpy(r,&f,4);return 0;
}
static int dd_sin(void *c,uint32_t x,uint32_t *r) { (void)c;return dd_sincos(x,r,0); }
static int dd_cos(void *c,uint32_t x,uint32_t *r) { (void)c;return dd_sincos(x,r,1); }
static int dd_decal(void *c,int32_t tag,const uint32_t q[16],uint64_t tex0,uint32_t rgba)
{ (void)c;return em_shadow_live_effect_001CE300(tag,q,tex0,rgba); }
static int dd_free(void *c)
{
    EmActor *a=c;EmSceneState *scene=em_scene_state();
    if (!a || !a->allocated || !scene) return -1;
    return em_actor_pool_free_001AFC10(R.pool,scene,a)<0 ? -1 : 0;
}
/* D_00810360..68: the player record's +0xB0, which 0015BCF0's tail leaves
 * as the bone-1 node's +0xC0 (the pose host's published hip; the record
 * the port keeps holds the stage's +0xB0 instead). No published pose:
 * the read faults. */
static int dd_listener(void *c,uint32_t out[3])
{
    (void)c;float hip[3];
    if (!player_pose_hip(hip)) return -1;
    memcpy(out,hip,12);return 0;
}
static int death_decal_tick(uint32_t node)
{
    EmActor *a=pool_actor(node);
    if (!a || !a->allocated) return -1;
    const uint32_t generation=a->generation;
    uint32_t *s3A20=em_aim_fire_binding_bytes(0x70003A20u,4,1),*s3A24=em_aim_fire_binding_bytes(0x70003A24u,4,1);
    if (!s3A20 || !s3A24) return -1;
    uint32_t b0[4],c0[4];memcpy(b0,a->pos,16);memcpy(c0,a->rot,16);
    const EmEffect001F77B0Node n={&a->u04[0],a->param,b0,c0,a->scratch,s3A20,s3A24};
    const EmEffect001F77B0Workers w={a,dd_rand,dd_sin,dd_cos,dd_decal,dd_free,dd_listener};
    uint32_t fault=0;
    if (em_effect_001F77B0(&n,&w,&fault)<0) {
        fprintf(stderr,"001F77B0: node %08X faulted at %08X\n",(unsigned)node,(unsigned)fault);
        return -1;
    }
    return a->allocated && a->generation==generation ? 1 : 0;
}
static int other_tick(void *context,uint32_t node,uint32_t callback)
{
    (void)context;
    if (callback==EM_EFFECT_001F77B0_CALLBACK) return death_decal_tick(node);
    if (callback==EM_AIM_FIRE_TRAIL_CALLBACK || callback==EM_AIM_FIRE_DEBRIS_CALLBACK) {
        /* The knife's trail node 001F18C0 and a box's debris node 001F2BA0
         * (em_area00_fx through the composition), with D_00275B40 = its
         * +0x110 (the walk's 001CB590); 1 while its record stays allocated,
         * 0 once it freed itself. */
        EmActor *a=pool_actor(node);
        if (!a) return -1;
        const uint32_t generation=a->generation;
        TRY(em_aim_fire_trail_set_current(a));
        const int rc=em_aim_fire_binding_run_bones(callback,node,node+0x110u);
        em_aim_fire_trail_set_current(NULL);
        if (rc<0) return -1;
        return a->allocated && a->generation==generation ? 1 : 0;
    }
    if (callback==EM_BONE_BURST_CALLBACK) {
        /* The bone-burst node 0022BBC0 (em_area01_ui through the
         * composition), with D_00275B40 = its +0x110 (the walk's
         * 001CB590); 1 while its record stays allocated, 0 once freed. */
        EmActor *a=pool_actor(node);
        if (!a) return -1;
        const uint32_t generation=a->generation;
        TRY(em_bone_burst_set_current(a));
        const int rc=em_aim_fire_binding_run_bones(callback,node,node+0x110u);
        em_bone_burst_set_current(NULL);
        if (rc<0) return -1;
        return a->allocated && a->generation==generation ? 1 : 0;
    }
    if (callback!=0x21AAC0 && callback!=0x21A500) return -1;
    EmAimFireTargetCall frame={0};frame.function=callback;frame.a[0]=node;frame.na=1;
    TRY(em_aim_fire_binding_frame(&frame));return (int32_t)frame.v0;
}
/* The read-only words and ELF windows the region-only owners (the muzzle
 * node's 001F5040 / 001F4F90 and the shell casing's 001F2F90 / 001F3340 /
 * 001F3620 / 001F3E30, em_area00_fx) load, as views: D_0025A350's block
 * (the debris rows and gravity, 001D80E0's colour D_0025AD70), D_0026EA80 /
 * D_0026EAC0 (the lines' colours and indices; assets/effect_tables.emet),
 * the bone-burst node 0022BBC0's timeline tables and source blocks
 * 0x267310..0x268B3F (em_bone_burst; docs/DAMAGE.md),
 * D_0028A56C (the Roger export's table word) and D_00275BCC (the one slot
 * stack's free count). */
static int add_windows(void)
{
    static const uint32_t windows[][2]={{0x25A350,0x34B0},{0x26EA80,0x64},{0x267310,0x1830}};
    unsigned n=6;
    for (unsigned k=0;k<3;++k) {
        const uint8_t *p=em_effects_live_window(windows[k][0],windows[k][1]);
        if (!p) return -1;
        R.bss[n++]=(EmPoseRegion){windows[k][0],windows[k][1],(uint8_t *)(uintptr_t)p,0};
    }
    /* D_00275C04: 001CD070's store of float_to_int(the clip w) (the bone
     * burst's ring test), the word 001CCF70 stores too: em_effects_live's
     * one copy. */
    int32_t *d275C04=em_effects_live_d275C04();
    if (!d275C04) return -1;
    R.bss[n++]=(EmPoseRegion){0x275C04,4,(uint8_t *)d275C04,1};
    if (em_area11_roger_table_word(0x28A56Cu,&R.d28A56C)<0) return -1;
    R.bss[n++]=(EmPoseRegion){0x28A56C,4,(uint8_t *)&R.d28A56C,0};
    const EmRogerActorWorld *slots=em_area11_boxes_slot_world();
    if (!slots || !slots->d00275BCC) return -1;
    R.bss[n++]=(EmPoseRegion){0x275BCC,2,(uint8_t *)slots->d00275BCC,0};
    R.world.region_count=n;
    return 0;
}
/* The barrel's particle sweep 001F40C0: 001F3620 / 001F3E30 through the
 * composition (em_aim_fire_world_live: em_area00_fx_debris). */
static int particle_call(void *context,uint32_t function,const uint32_t a[5])
{
    (void)context;
    EmAimFireTargetCall frame={0};frame.function=function;
    frame.na=function==0x1F3620u ? 2 : 5;
    for (unsigned k=0;k<frame.na;++k) frame.a[k]=(uint64_t)(int64_t)(int32_t)a[k];
    return em_aim_fire_binding_frame(&frame)<0 ? -1 : 0;
}
int em_aim_fire_runtime_effects_attach(void)
{
    if (add_windows()<0) return -1;
    if (em_effects_live_set_particle_call(particle_call,NULL)<0) return -1;
    return em_effects_live_set_other_tick(other_tick,NULL);
}
int em_aim_fire_runtime_001F4010(int32_t index,const uint32_t *at)
{
    /* 00188630's 001F4010(3, 0x700036A0): the equipment wrote the frame it
     * passes (0x700036A0..0x700036DF) into its own copy; the scratchpad is
     * one memory, so the copy goes to the composition's 0x700036A0 before
     * the call (the player closure's view of it). */
    uint8_t *spad=em_aim_fire_binding_bytes(0x700036A0u,64,1);
    if (!spad || !at) return -1;
    memcpy(spad,at,64);
    EmAimFireTargetCall frame={0};frame.function=0x1F4010u;frame.na=2;
    frame.a[0]=(uint64_t)(int64_t)index;frame.a[1]=0x700036A0u;
    return em_aim_fire_binding_frame(&frame)<0 ? -1 : 0;
}

/* ---- the knife's (flavour 4) callees (em_equipment_live_set_world) ---- */

/* The node's staged scratch 0x700038A0..0x700038DF to and from the
 * scratchpad (four quadwords, each its own owner's). */
static int spad_move(uint32_t *spad,int to_scratch)
{
    for (unsigned k=0;k<4;++k) {
        uint8_t *p=em_aim_fire_binding_bytes(0x700038A0u+16u*k,16,to_scratch);
        if (!p) return -1;
        if (to_scratch) memcpy(p,spad+4*k,16);
        else memcpy(spad+4*k,p,16);
    }
    return 0;
}
int em_aim_fire_runtime_sprite_borrow(EmAimFireTargetCall *c,void *context,
    void *(*map)(void *,uint32_t,size_t,int),uint32_t *fault_address)
{
    if(!c || c->function!=0x001CD520u || c->na!=5 || c->nf!=3 || !map ||
       !R.render.vu || !R.render.vf23_valid || R.render.fault_function) {
        if(fault_address)*fault_address=R.render.fault_address ? R.render.fault_address : 0x001CD520u;
        return -1;
    }
    EmAimFireRenderLive view=R.render;
    view.context=context;view.map=map;view.call=NULL;
    /* Same ambient-register invalidation as the existing native sprite root. */
    R.vf23_valid=0;
    int rc=em_aim_fire_render_live_call(&view,c);
    if(rc<0) {
        R.render.fault_function=view.fault_function;
        R.render.fault_address=view.fault_address;
        if(fault_address)*fault_address=view.fault_address ? view.fault_address : view.fault_function;
    }
    return rc;
}
int em_aim_fire_runtime_world_call(uint32_t fn,const uint32_t *a,unsigned na,uint32_t f12,unsigned nf,
                                   uint32_t *spad,uint32_t *v0)
{
    if (na>6 || nf>1 || !spad) return -1;
    TRY(spad_move(spad,1));
    EmAimFireTargetCall frame={0};
    frame.function=fn;frame.na=na;frame.nf=nf;
    for (unsigned k=0;k<na;++k) frame.a[k]=(uint64_t)(int64_t)(int32_t)a[k];
    frame.f[0]=f12;
    const int rc=em_aim_fire_binding_call_bones(&frame,em_equipment_live_current_bones());
    if (spad_move(spad,0)<0) return -1;
    if (rc<0) return -1;
    if (v0) *v0=(uint32_t)frame.v0;
    return 0;
}
int em_aim_fire_runtime_world_read(uint32_t address,void *out,uint32_t size)
{
    const void *p=em_aim_fire_binding_bytes(address,size,0);
    if (!p) return -1;
    memcpy(out,p,size);return 0;
}
const uint8_t *em_aim_fire_runtime_world_bytes(uint32_t address,uint32_t size)
{
    return em_scene_bindings_grid_node_bytes(address,size);
}
/* Two quadwords near the top of the composition's temporaries: call depth
 * d carves its frames below LOCAL_BASE + 0x400 * d, so 0x7F004F00..1F is
 * touched only by a depth-16 frame (the deepest the composition allows);
 * a call chain that deep would overwrite them. The original passes its own stack
 * locals there (0018A1F0's frame). */
int em_aim_fire_runtime_world_temp(const uint32_t words[4],uint32_t *address)
{
    static unsigned next;
    const uint32_t at=0x7F004F00u+16u*(next++&1u);
    uint8_t *p=em_aim_fire_binding_bytes(at,16,1);
    if (!p) return -1;
    memcpy(p,words,16);*address=at;return 0;
}
