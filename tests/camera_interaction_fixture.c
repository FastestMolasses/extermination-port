#include "game/em_camera.h"
#include <stdio.h>
EmGameState g;
const float kLocoTierSpeed[4]={0};
/* Test-only host state. Unrelated gameplay entrypoints are discarded by the
 * linker; the actual public camera hook and collision walker execute. */
static float word(const unsigned char *ram,unsigned address)
{float value;memcpy(&value,ram+address,4);return value;}
static int retarget(const char *ramfile,const char *world,float *out,int refusal)
{
    unsigned char *ram=malloc(0x2000000);FILE *f=fopen(ramfile,"rb");
    if (!ram) return 0;
    if (!f) {free(ram);return 0;}
    if (fread(ram,1,0x2000000,f)!=0x2000000) {fclose(f);free(ram);return 0;}
    fclose(f);memset(&g,0,sizeof g);
    if (em_collision_load(&g.coll,world)) {free(ram);return 0;}
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
    em_collision_free(&g.coll);free(ram);return ok;
}
int test_retarget(const char *ramfile,const char *world,float *out)
{return retarget(ramfile,world,out,0);}
int test_refusal(const char *ramfile,const char *world,float *out)
{return retarget(ramfile,world,out,1);}
