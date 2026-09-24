#include "game/em_camera.h"
#include "game/em_collision_world.h"
#include <stdio.h>
EmGameState g;
const float kLocoTierSpeed[4]={0};
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

static int retarget(const char *ramfile,const char *world,const char *scratch,float *out,int refusal)
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
    float hip[3],rot[3];
    for (int i=0;i<3;i++) {
        g.pos[i]=word(ram,p+0xA0+i*4);hip[i]=word(ram,p+0xB0+i*4);
        rot[i]=word(ram,c+0x30+i*4);
    }
    g.cam_dist_param=word(ram,c+0xC);
    g.cam.y_lo=word(ram,c+0x50);g.cam.y_hi=word(ram,c+0x54);
    g.cam.var_5c=word(ram,c+0x5C);g.cam.overhead_y=word(ram,c+0x60);
    g.cam.horiz_dist=word(ram,0x810690);
    int ok=refusal ? camera_interaction_retarget_distance_area11(
        &g.cam,hip,rot,-20.0f,word(ram,c+0x64)) :
        camera_interaction_retarget_area11(&g.cam,hip,rot,word(ram,c+0x64));
    for (int i=0;i<3;i++) {out[i]=g.cam.eye[i];out[3+i]=g.cam.tgt[i];}
    out[6]=g.cam.y_lo;out[7]=g.cam.y_hi;out[8]=g.cam.hit;
    out[9]=g.cam.probe_flags;out[10]=g.cam.ground_attr78;out[11]=g.cam.overhead_y;
    em_collision_world_unload();em_collision_free(&g.coll);free(ram);return ok;
}
int test_retarget(const char *ramfile,const char *world,const char *scratch,float *out)
{return retarget(ramfile,world,scratch,out,0);}
int test_refusal(const char *ramfile,const char *world,const char *scratch,float *out)
{return retarget(ramfile,world,scratch,out,1);}
