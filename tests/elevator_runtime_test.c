#include "game/em_elevator_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    EmModel *model;
    EmInteractionFrame frame;
    EmInteractionRuntime interaction;
    EmElevatorRuntime elevator;
    float palette[22*16], player[3], eye[3], target[3];
    int tick, acquired, released, palettes, idles, actors, poses, hulls, hull_tick;
    int entered, left, camera_sets, camera_publishes, chases;
    int message_started, message_complete, message_polls;
    int sound_tick, motion_tick, last_pose_tick, fail_camera;
    int clip_commit_tick, clip_end_tick, completed_tick;
    float carried_height;
} Fixture;

static uint32_t bits(float value)
{ uint32_t word;memcpy(&word,&value,sizeof word);return word; }

/* These are explicit host boundaries: the source-channel acquisition/release
 * worker and refusal message presenter have their own original-reference
 * tests. The script, frame, clip clock and elevator motion below are real. */
static int acquire(void *context)
{ Fixture *f=context; ++f->acquired; return 1; }
static int idle(void *context, float *palette)
{
    Fixture *f=context; ++f->idles;
    int clip=em_model_clip_index(f->model,0); assert(clip>=0);
    em_model_palette_at(f->model,(unsigned)clip,0,palette); return 1;
}
static int publish(void *context, const float *palette)
{
    Fixture *f=context; ++f->palettes;
    if (f->interaction.animation.active) {
        EmInteractionAnimation *a=&f->interaction.animation;
        int index=em_model_clip_index(f->model,0x47); assert(index>=0);
        const float *expected=f->model->palette+
            (f->model->clips[index].first_frame+a->frame)*22*16;
        assert(!memcmp(palette,expected,sizeof f->palette));
    }
    return 1;
}
static int release(void *context)
{ Fixture *f=context; ++f->released; return 1; }
static int event(void *context, EmInteractionFrameEvent value)
{
    Fixture *f=context;
    if (value==EM_INTERACTION_BARS_ENTER) ++f->entered;
    if (value==EM_INTERACTION_BARS_LEAVE) ++f->left;
    return 1;
}
static int align_player(void *context, const float position[3])
{ Fixture *f=context; memcpy(f->player,position,sizeof f->player); return 1; }
static int face_player(void *context, float yaw)
{ (void)context; assert(isfinite(yaw)); return 1; }
static int camera_set(void *context, const float eye[3], const float target[3])
{
    Fixture *f=context; if(f->fail_camera)return 0;
    ++f->camera_sets; memcpy(f->eye,eye,sizeof f->eye);
    memcpy(f->target,target,sizeof f->target); return 1;
}
static int camera_publish(void *context)
{ Fixture *f=context; ++f->camera_publishes; return 1; }
static int camera_chase(void *context)
{ Fixture *f=context; ++f->chases; return 1; }
static int message_start(void *context, uint32_t token, uint32_t delay)
{
    Fixture *f=context; assert(token==0x8000001a && delay==0);
    ++f->message_started; return 1;
}
static int message_done(void *context)
{ Fixture *f=context; ++f->message_polls; return f->message_complete; }
static void sound(void *context, unsigned cue, float radius)
{
    Fixture *f=context; assert(radius==300);
    if(cue==0x19a) {assert(f->sound_tick<0);f->sound_tick=f->tick;}
    else {
        assert(cue==(f->elevator.owner.lower?0x452u:0x453u));
        assert(f->motion_tick<0);f->motion_tick=f->tick;
    }
}
static void pose(void *context, float height)
{
    Fixture *f=context; ++f->poses; f->last_pose_tick=f->tick;
    assert(height==f->elevator.owner.height);
    if(f->poses==150) f->carried_height=height;
}
static void indicator(void *context)
{ (void)context; }
static void actor(void *context)
{ Fixture *f=context; ++f->actors; }
/* 001A2370 follows only the completion's 001C6380 (0x827E48/0x827E54),
 * never a carry rebuild. */
static void hull(void *context)
{
    Fixture *f=context; ++f->hulls; f->hull_tick=f->tick;
    assert(f->last_pose_tick==f->tick);
}

static void setup(Fixture *f, EmModel *model, const char *path, int lower)
{
    memset(f,0,sizeof *f); f->model=model;
    f->sound_tick=f->motion_tick=f->last_pose_tick=-1;
    f->clip_commit_tick=f->clip_end_tick=f->completed_tick=-1;
    EmInteractionRuntimeHooks shared={f,acquire,idle,release,publish,event,NULL};
    assert(em_interaction_runtime_init(&f->interaction,&f->frame,model,f->palette,&shared));
    EmElevatorRuntimeHooks hooks={f,align_player,face_player,camera_set,
        camera_publish,camera_chase,message_start,message_done,sound,pose,indicator,actor,hull};
    assert(em_elevator_runtime_load(&f->elevator,path,lower,&f->interaction,
        f->player+1,f->target+1,&hooks));
}

static void step(Fixture *f, int powered)
{
    /* Actual task order: player before pooled owner. */
    assert(em_interaction_runtime_player_tick(&f->interaction,1)>=0);
    if(f->interaction.animation.active && !f->interaction.animation.pending) {
        if(f->clip_commit_tick<0) f->clip_commit_tick=f->tick;
        if(f->clip_end_tick<0 && em_interaction_runtime_animation_done(
            &f->interaction,&f->elevator)==1) f->clip_end_tick=f->tick;
    }
    assert(em_elevator_runtime_tick(&f->elevator,powered,1)==0);
    if(f->entered && !f->elevator.owner.phase && f->completed_tick<0)
        f->completed_tick=f->tick;
    ++f->tick;
}

static void run(EmModel *model, const char *path, int powered, int lower)
{
    Fixture f; setup(&f,model,path,lower);
    int competitor=0;
    assert(em_interaction_runtime_claim(&f.interaction,&competitor));
    assert(!em_elevator_runtime_arm(&f.elevator) && !f.elevator.owner.armed);
    /* End the explicit competing-host fixture without performing a script. */
    f.interaction.owner=NULL;f.frame.selector=0;
    assert(em_elevator_runtime_arm(&f.elevator));
    assert(!em_elevator_runtime_arm(&f.elevator));
    assert(!em_elevator_runtime_free(&f.elevator));
    step(&f,powered);
    assert(f.acquired==1 && f.elevator.owner.phase==1 && !f.entered);
    assert(f.elevator.program.script.pc==(powered?EM_ELEVATOR_POWERED_SCRIPT:EM_ELEVATOR_REFUSAL_SCRIPT));
    while(f.tick<650 && !f.released) {
        if(f.tick==40) {
            EmElevator before=f.elevator.owner;
            EmElevatorMotion motion=f.elevator.motion;
            EmScript script=f.elevator.program.script;
            EmInteractionAnimation animation=f.interaction.animation;
            int palettes=f.palettes,actors=f.actors;
            for(int paused=0;paused<150;paused++) {
                assert(em_interaction_runtime_player_tick(&f.interaction,0)==0);
                assert(em_elevator_runtime_tick(&f.elevator,powered,0)==0);
            }
            assert(!memcmp(&before,&f.elevator.owner,sizeof before));
            assert(!memcmp(&motion,&f.elevator.motion,sizeof motion));
            assert(!memcmp(&script,&f.elevator.program.script,sizeof script));
            assert(!memcmp(&animation,&f.interaction.animation,sizeof animation));
            assert(palettes==f.palettes && actors==f.actors);
        }
        /* Refusal duration is deliberately supplied by the host boundary;
         * 400 polls proves that no adapter timeout silently releases it. */
        if(!powered && f.message_polls==400) f.message_complete=1;
        step(&f,powered);
        if(f.completed_tick<0) assert(f.elevator.owner.lower==lower);
    }
    assert(f.tick<650 && f.entered==1 && f.left==1 && f.released==1);
    assert(f.completed_tick==f.tick-2 && !em_interaction_runtime_owner(&f.interaction));
    assert(!f.elevator.owner.phase && !f.elevator.owner.armed);
    if(powered) {
        assert(f.sound_tick==120);
        assert(f.clip_end_tick-f.clip_commit_tick==201);
        assert(f.motion_tick==f.clip_end_tick+1);
        assert(f.completed_tick-f.motion_tick==153);
        assert(f.elevator.motion.ticks==150 && f.poses==151);
        assert(f.hulls==1 && f.hull_tick==f.completed_tick);
        assert(f.elevator.owner.lower!=lower);
        assert(f.elevator.owner.height==(lower?230:190));
        /* Original00828050's 150 add.s steps with the EE guard-bit add:
         * route 04_elevator_ride carries the player from 230 to 190.00061
         * (f394..f543); up, 229.99939. Only the owner's final callback
         * snaps its own Y to the exact landing. Do not snap both. */
        uint32_t carried=lower?0x4365ffd8u:0x433e0028u;
        assert(bits(f.player[1])==carried && bits(f.carried_height)==carried);
        assert(f.target[1]==(lower?245:205));
        assert(f.camera_sets==2 && f.camera_publishes==4 && !f.chases);
        assert(!f.message_started && f.elevator.owner.indicator_level==128);
    } else {
        assert(f.sound_tick<0 && f.motion_tick<0 && f.clip_commit_tick<0);
        assert(f.message_started==1 && f.message_polls==401 && f.chases==1);
        assert(f.elevator.owner.lower==lower && f.elevator.owner.sound_timer==300);
        assert(!f.poses && !f.hulls && !f.camera_sets && !f.camera_publishes);
    }
    printf("Elevator %s lower%d: commit%d end%d carry%d complete%d release%d PASS\n",
        powered?"powered":"refusal",lower,f.clip_commit_tick,f.clip_end_tick,
        f.motion_tick,f.completed_tick,f.tick-1);
    assert(em_elevator_runtime_free(&f.elevator));
}

static void fail_camera(EmModel *model, const char *path)
{
    Fixture f;setup(&f,model,path,0);f.fail_camera=1;
    assert(em_elevator_runtime_arm(&f.elevator));
    int failed=0;
    for(f.tick=0;f.tick<20;f.tick++) {
        assert(em_interaction_runtime_player_tick(&f.interaction,1)>=0);
        if(em_elevator_runtime_tick(&f.elevator,1,1)<0) {failed=1;break;}
    }
    assert(failed && f.elevator.failed && f.elevator.program.failed);
    assert(f.elevator.owner.phase==1 && f.elevator.owner.armed==4 && !f.elevator.owner.lower);
    assert(em_interaction_runtime_owner(&f.interaction)==&f.elevator);
    assert(em_interaction_runtime_camera_owned(&f.interaction));
    assert(!em_elevator_runtime_free(&f.elevator) && !f.released);
    assert(em_elevator_runtime_tick(&f.elevator,1,1)==-1);
    /* Explicit whole-scene teardown, not a successful game release. */
    memset(&f.interaction,0,sizeof f.interaction);
    assert(em_elevator_runtime_free(&f.elevator));
}

int main(int argc, char **argv)
{
    assert(argc==3);EmModel model={0};assert(em_model_load(&model,argv[2])==0);
    run(&model,argv[1],0,0);run(&model,argv[1],1,0);run(&model,argv[1],1,1);
    fail_camera(&model,argv[1]);em_model_free(&model);
    puts("Elevator shared runtime: actual assets, full clip, task order, freeze and fault retention PASS");
    return 0;
}
