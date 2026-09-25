#include "game/em_camera.h"
#include "game/em_camera_live.h"
#include "game/em_collision_world.h"
#include "game/em_frame.h"
#include "game/em_scene_bindings.h"
#include <stdio.h>
EmGameState g;
const float kLocoTierSpeed[4]={0};
/* The canonical scene bytes the live camera reads (the game keeps them in
 * em_scene_bindings.c; AREA11 here) and the transition fade substate. */
EmSceneState *em_scene_state(void)
{
    static EmSceneState state;
    state.d810700 = 0x0B;
    return &state;
}
const EmTransitionFade *em_frame_transition(void) { static EmTransitionFade fade; return &fade; }
/* Test-only host state. Unrelated gameplay entrypoints are discarded by the
 * linker; the actual public camera hook and collision walkers execute. */
static float word(const unsigned char *ram,unsigned address)
{float value;memcpy(&value,ram+address,4);return value;}
static uint32_t u32(const unsigned char *ram,unsigned address)
{uint32_t value;memcpy(&value,ram+address,4);return value;}

/* The captured scene's collision world (census L06b/L08): the probe's
 * 0019A910 / 0019B7D0 run over the original's own cell directory (found in
 * the capture by the disc directory's header and offset words, which the
 * re-transforms never change) and its published class-4 list
 * D_00275B7C / D_00275B84, pushed in the original order. */
static EmActor owners[0x80];
static int load_world(const unsigned char *ram,const char *world,const char *scratch)
{
    FILE *f=fopen(EM_COLLISION_WORLD_CELLS_PATH,"rb");
    if (!f) return 0;
    static unsigned char disc[0x8000];
    size_t size=fread(disc,1,sizeof disc,f);fclose(f);
    if (size<4 || size==sizeof disc) return 0;
    size_t head=4+4*(size_t)u32(disc,0);
    long found=-1;
    for (size_t at=0;at+size<=0x2000000;at+=4)
        if (!memcmp(ram+at,disc,head)) { if (found>=0) return 0; found=(long)at; }
    if (found<0 || !(f=fopen(scratch,"wb"))) return 0;
    fwrite(ram+found,1,size,f);fclose(f);
    if (em_collision_world_load(&g.coll,world,scratch,EM_COLLISION_WORLD_SDK_PATH)) return 0;
    const uint32_t list=u32(ram,0x275B7C);
    int16_t count;memcpy(&count,ram+0x275B84,2);
    if (count<0 || count>0x80) return 0;
    memset(owners,0,sizeof owners);
    for (int j=count-1;j>=0;--j) {           /* the oldest push first */
        const uint32_t a=u32(ram,list+4*(unsigned)j);
        EmActor *o=&owners[j];
        o->status=ram[a];o->cls=ram[a+2];o->model=ram[a+3];
        memcpy(&o->uid,ram+a+0xE,2);memcpy(&o->kind,ram+a+0x54,2);
        memcpy(o->pos,ram+a+0xB0,sizeof o->pos);
        o->self=o;
        if (em_collision_world_publish_001B1B70(o)!=1) return 0;
    }
    static EmSceneState scene;
    uint32_t fault=0;
    return em_collision_world_close_out_001AAD00(&scene,0,&fault)==0 && !fault;
}

/* The live camera's inputs (em_camera_live.h) from the capture: the player
 * record D_008102B0 with its +B0 (the bone-1 position the retarget's
 * prepass reads) and +C0 Euler. No stand-in, no timeline. */
static EmPlayerLiveActor player;
static int32_t carry31F0;
static const EmPlayerLiveActor *camera_player(void *ctx) { (void)ctx; return &player; }
static int camera_hip(void *ctx, float out[3])
{
    (void)ctx;
    for (int i = 0; i < 3; i++) out[i] = em_live_f32(&player, 0xB0 + 4u * (unsigned)i);
    return 1;
}
static int camera_euler(void *ctx, float out[3])
{
    (void)ctx;
    for (int i = 0; i < 3; i++) out[i] = em_live_f32(&player, 0xC0 + 4u * (unsigned)i);
    return 1;
}
static int camera_no_standin(void *ctx) { (void)ctx; return CAMERA_STANDIN_NONE; }
static int camera_no_timeline(void *ctx) { (void)ctx; return -1; }
static const EmCameraLiveHost camera_host = {NULL, camera_player, camera_hip, camera_euler, NULL, &carry31F0,
                                             camera_no_standin, camera_no_timeline};

/* The captured camera block and vector pool into the live camera, the
 * capture's own seed Euler cam+30 through 0018CBD0 (distance: sub 3 the
 * camera's +0x0C, the refusal's sub 5 -20), then 0018D7B0(5), 0018D7B0(1)
 * and cam+A0 = 0x78. `out` receives the resulting camera block (0xD0 bytes)
 * and the pool D_008105D0..D_008106A3 (0xD4 bytes). */
static int retarget(const char *ramfile,const char *world,const char *scratch,uint8_t *out,int refusal)
{
    unsigned char *ram=malloc(0x2000000);FILE *f=fopen(ramfile,"rb");
    if (!ram) return 0;
    if (!f) {free(ram);return 0;}
    if (fread(ram,1,0x2000000,f)!=0x2000000) {fclose(f);free(ram);return 0;}
    fclose(f);memset(&g,0,sizeof g);
    if (em_collision_load(&g.coll,world)) {free(ram);return 0;}
    if (!load_world(ram,world,scratch)) {
        em_collision_world_unload();em_collision_free(&g.coll);free(ram);return 0;
    }
    const unsigned c=0x8101E0,p=0x8102B0;
    memcpy(player.bytes,ram+p,sizeof player.bytes);
    float rot[3];
    for (int i=0;i<3;i++) { g.pos[i]=word(ram,p+0xA0+i*4); rot[i]=word(ram,c+0x30+i*4); }
    g.yaw=word(ram,p+0xC4);
    g.cam.zoom=480;
    int ok=em_camera_live_bind(&camera_host)==0;
    if (ok) {
        memcpy(em_camera_live_bytes(0x008101E0u,0xD0),ram+c,0xD0);
        memcpy(em_camera_live_bytes(0x008105D0u,0xD4),ram+0x8105D0,0xD4);
        em_camera_live_view_publish();
        ok=refusal ? camera_interaction_retarget_distance_area11(&g.cam,rot,-20.0f) :
                     camera_interaction_retarget_area11(&g.cam,rot);
        memcpy(out,em_camera_live_bytes(0x008101E0u,0xD0),0xD0);
        memcpy(out+0xD0,em_camera_live_bytes(0x008105D0u,0xD4),0xD4);
    }
    em_collision_world_unload();em_collision_free(&g.coll);free(ram);return ok;
}
int test_retarget(const char *ramfile,const char *world,const char *scratch,uint8_t *out)
{return retarget(ramfile,world,scratch,out,0);}
int test_refusal(const char *ramfile,const char *world,const char *scratch,uint8_t *out)
{return retarget(ramfile,world,scratch,out,1);}
