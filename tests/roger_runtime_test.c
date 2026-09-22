#include "game/em_roger_runtime.h"
#include "em_math.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

struct EmGfx { int unused; };
struct EmGfxMesh { uint32_t vertices; };
static struct EmGfxMesh mesh;
static unsigned creates,destroys,updates;
EmGfxMesh *em_gfx_mesh_create(EmGfx *gfx,const float *v,uint32_t vc,const uint32_t *idx,
    uint32_t ic,const EmGfxTexDesc *tex,uint32_t tc,const uint8_t *pixels,uint32_t flags)
{
    assert(gfx && v && idx && tex && pixels && !flags && vc>2808 && ic==3380*3 && tc>68);
    mesh.vertices=vc;++creates;return &mesh;
}
void em_gfx_mesh_destroy(EmGfx *gfx,EmGfxMesh *m)
{ assert(gfx && m==&mesh);++destroys; }
int em_gfx_mesh_update_positions(EmGfx *gfx,EmGfxMesh *m,const float *p,uint32_t count)
{
    assert(gfx && m==&mesh && count==mesh.vertices && p);
    for (uint32_t i=0;i<count*3;++i) assert(isfinite(p[i]));
    ++updates;return 1;
}

typedef struct {unsigned random,publish,event,commands[6];int visible,unsupported;} Workers;
static uint32_t random_word(void *context)
{ Workers *w=context;return ++w->random*UINT32_C(0x94720123); }
static int matrix(void *context,const float position[3],const float angles[3],float out[16])
{
    (void)context;assert(isfinite(angles[1]));em_mat4_identity(out);
    memcpy(out+12,position,12);return 1; /* controlled placement boundary */
}
static int publish(void *context,EmRogerRuntime *r)
{ Workers *w=context;assert(r->owner.class_flags==0xAA);++w->publish;return w->visible; }
static int event(void *context,EmRogerRuntime *r,EmRogerEvent type,unsigned argument)
{ Workers *w=context;(void)r;(void)type;(void)argument;++w->event;return 1; }
static EmScriptCommandResult command(Workers *w,unsigned kind,const unsigned char *record)
{
    unsigned op=em_script_u32(record,0)&0xFFF;
    static const unsigned routed[5]={7,0,10,11,21};
    if (kind<5) assert(op==routed[kind]);
    else assert(op!=7 && op!=0 && op!=10 && op!=11 && op!=21);
    ++w->commands[kind];
    return w->unsupported ? EM_SCRIPT_UNSUPPORTED : EM_SCRIPT_ADVANCE;
}
#define WORKER(name,kind) static EmScriptCommandResult name(void *c,EmRogerRuntime *r,EmScript *s,const unsigned char *b) \
{(void)r;(void)s;return command(c,kind,b);}
WORKER(frame,0) WORKER(camera,1) WORKER(player,2) WORKER(actor,3) WORKER(message,4) WORKER(scene_command,5)

int main(void)
{
    EmInteractionScene scene;
    assert(em_interaction_scene_load(&scene,"assets/scene_snow/interaction.emis"));
    EmInteractionSceneOwner *source=em_interaction_scene_role(&scene,EM_INTERACTION_ROGER);
    assert(source);
    EmRogerRuntime r={0};EmGfx gfx={0};Workers w={0};EmRogerStory story={0};uint8_t activity=0;
    EmRogerRuntimeHooks h={&w,matrix,publish,event,frame,camera,player,actor,message,scene_command,random_word};
    EmRogerRuntimeHooks missing=h;missing.frame=NULL;
    assert(!em_roger_runtime_load(&r,&gfx,"assets/scene_snow",source,&scene.math,&story,&activity,&missing));
    assert(em_roger_runtime_load(&r,&gfx,"assets/scene_snow",source,&scene.math,&story,&activity,&h));
    assert(em_interaction_scene_bind(&scene,source->source_id,&r,&r.owner.status,
                                     &r.owner.class_flags,&r.owner.armed));
    assert(source->native_owner==&r && source->live_armed==&r.owner.armed);
    float outside[3]={250,300,210};
    EmGfxMesh *draw;const float *palette;uint32_t bones;
    assert(em_roger_runtime_record(&r,&draw,&palette,&bones)==0);
    assert(em_roger_runtime_tick(&r,outside,0)==0 && !w.random && !w.publish);
    story.suppressed=1;
    assert(em_roger_runtime_tick(&r,outside,1)==1 && !w.random && !w.publish);
    story.suppressed=0;
    assert(em_roger_runtime_tick(&r,outside,1)==1 && w.random==2 && w.publish==1);
    assert(r.owner.rendered==1 && r.pose.playback.remaining==179);
    assert(em_roger_runtime_record(&r,&draw,&palette,&bones)==1 && bones==22 && draw==&mesh);
    /* Forced visible initial draw did not alter publication visibility. */
    assert(w.visible==0);
    assert(palette[12]==source->position[0] && palette[13]==source->position[1]);
    activity=1;
    assert(em_roger_runtime_tick(&r,outside,1)==1 && activity==0 && r.face.state.talking==1);
    activity=2;
    assert(em_roger_runtime_tick(&r,outside,1)==1 && activity==0 && !r.face.state.talking);
    activity=3;
    assert(em_roger_runtime_tick(&r,outside,1)==1 && activity==3);
    float changed[3]={310,275,180},angles[3]={0,1,0};
    assert(em_roger_runtime_set_placement(&r,changed,angles));
    assert(em_roger_runtime_tick(&r,outside,1)==1 && r.owner.yaw==1);
    assert(em_roger_runtime_record(&r,&draw,&palette,&bones)==1 && palette[12]==310);
    assert(!em_roger_runtime_animation(&r,0x98,2,0,0)); /* unavailable bank isn't substituted */
    story.progress=1;r.owner.armed=4;
    assert(em_roger_runtime_tick(&r,outside,1)==1 && r.script.pc==0x828810);
    for (unsigned i=0;r.owner.phase && i<20;++i) assert(em_roger_runtime_tick(&r,outside,1)==1);
    assert(!r.owner.phase && !r.owner.armed && r.pose.transition.active);
    assert(w.commands[0]==2 && w.commands[2]==1 && w.commands[4]==1 && w.commands[5]==1);
    assert(em_roger_runtime_record(&r,&draw,&palette,&bones)==0); /* ordinary culled Roger */
    r.owner.lifecycle=3;
    assert(em_roger_runtime_tick(&r,outside,1)==0 && r.owner.freed && !r.owner.status);
    em_roger_runtime_free(&r);assert(!r.ready && !r.mesh);
    memset(&w,0,sizeof w);memset(&story,0,sizeof story);
    assert(em_roger_runtime_load(&r,&gfx,"assets/scene_snow",source,&scene.math,&story,&activity,&h));
    float inside[3]={340,300,190};
    assert(em_roger_runtime_tick(&r,inside,1)==1 && r.owner.phase==1 && r.script.pc==0x8283D0);
    w.unsupported=1;
    assert(em_roger_runtime_tick(&r,inside,1)==-1 && r.failed && !story.progress);
    assert(em_roger_runtime_record(&r,&draw,&palette,&bones)==-1);
    em_roger_runtime_free(&r);
    memset(&w,0,sizeof w);memset(&story,0,sizeof story);
    assert(em_roger_runtime_load(&r,&gfx,"assets/scene_snow",source,&scene.math,&story,&activity,&h));
    assert(em_roger_runtime_tick(&r,inside,1)==1);
    /* Real program sequencing with controlled external workers: original
     * op01/sub10 zeroes owner placement before its bank96/Roger2 bind. */
    for (unsigned i=0;r.script.pc<0x8285D0 && i<20;++i)
        assert(em_roger_runtime_tick(&r,inside,1)==1);
    assert(r.bank==0x96 && r.pose.bank==&r.encounter && r.pose.playback.remaining==690.5f);
    assert(!r.position[0] && !r.position[1] && !r.position[2] && !r.owner.yaw);
    assert(r.palette[16+12]>300); /* authored world root survives original zero owner */
    for (unsigned i=0;r.owner.phase && i<30;++i) assert(em_roger_runtime_tick(&r,inside,1)==1);
    assert(story.progress==1 && r.bank==0x4A && r.pose.bank==&r.assets.animation);
    assert(r.position[0]==327.70001220703125f && r.position[1]==290 && r.position[2]==197.5f);
    assert(r.owner.yaw==2.844886541366577f);
    em_roger_runtime_free(&r);
    assert(creates==3 && destroys==3 && updates>5);
    puts("Roger runtime: actual model/face/raw pose + canonical binding + four service routes + explicit missing-worker failure PASS");
    return 0;
}
