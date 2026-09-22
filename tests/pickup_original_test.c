/* Actual exported pickup programs and native owner/runtime adapter.
 * Model/GPU/player sampling are explicit deterministic fixture boundaries;
 * owner, sequencer, frame command and inventory code are the real modules. */
#define main pickup_light_fixture_main
#include "pickup_light_test.c"
#undef main

typedef struct {
    int status_calls, pending_status, turn_calls, camera_calls, sounds, publications;
    uint8_t kind, index;
} Host;

static int player_acquire(void *context) { (void)context; return 1; }
static int player_idle(void *context, float *palette) { (void)context; (void)palette; return 1; }
static int player_release(void *context) { (void)context; return 1; }
static int player_publish(void *context, const float *palette) { (void)context; (void)palette; return 1; }
static int frame_event(void *context, EmInteractionFrameEvent event)
{ (void)context; (void)event; return 1; }
static int retarget(void *context) { (void)context; return 1; }
static int turn(void *context, uint32_t source_id, const float position[3], float step)
{
    Host *host=context; (void)source_id; (void)position;
    assert(step == 0.1745329350233078f); /* Original record3E32B8C3. */
    return ++host->turn_calls >= 2;
}
static int camera(void *context, uint32_t source_id, const float position[3], EmScript *script)
{
    Host *host=context; (void)source_id; (void)position;
    if (!script->phase) { script->phase=1; ++host->camera_calls; return 0; }
    return ++host->camera_calls >= 3;
}
static int status(void *context, uint8_t kind, uint8_t index)
{
    Host *host=context;
    assert(!host->pending_status);
    ++host->status_calls; host->pending_status=1; host->kind=kind; host->index=index;
    return 1;
}
static int owner_event(void *context, uint32_t source_id, EmPickupOwnerEvent event, uint32_t argument)
{
    Host *host=context; (void)source_id;
    if (event==EM_PICKUP_OWNER_PUBLISH) ++host->publications;
    else if (event==EM_PICKUP_OWNER_TAKE_SOUND) { assert(argument==0x194); ++host->sounds; }
    else assert(event==EM_PICKUP_OWNER_AURA);
    return 1;
}

static void run(uint32_t callback, uint8_t subtype, uint16_t type, int short_program)
{
    Host host={0};
    EmGfx *gfx=(EmGfx *)(uintptr_t)1;
    em_pickup_reset();
    EmModel model={0}; EmModelClip clips[3]={0};
    for (unsigned i=0;i<3;++i) { clips[i].id=0x40+i; clips[i].frame_count=45; clips[i].fps=60; }
    float palette[22*16]={0};
    model.bone_count=22; model.clip_count=3; model.clips=clips; model.palette=palette;
    EmInteractionFrame frame={0}; EmInteractionRuntime interaction;
    EmInteractionRuntimeHooks runtime_hooks={&host,player_acquire,player_idle,player_release,
        player_publish,frame_event,retarget};
    assert(em_interaction_runtime_init(&interaction,&frame,&model,palette,&runtime_hooks));
    EmInteractionSceneOwner metadata={.source_id=0x828180,.role=EM_INTERACTION_PICKUP,
        .publication_rank=0,.callback=callback,.item_type=type,.uid=0xB01,
        .initial_status=1,.class_flags=callback==0x219550?0x84:0x87,.subtype=subtype,.selector=3,
        .position={211.6f,229.9f,227.2f}};
    assert(em_pickup_add(gfx,"scene",type,metadata.position,0,metadata.uid,"item",0)==0);
    char path[128]; snprintf(path,sizeof path,"assets/scene_snow/pickup_%08x.emsc",callback);
    EmPickupOriginalHooks hooks={&host,turn,camera,status,owner_event};
    EmPickupOriginalHooks missing=hooks; missing.camera=NULL;
    assert(!em_pickup_original_bind(&metadata,&interaction,path,&missing));
    assert(em_pickup_original_bind(&metadata,&interaction,path,&hooks)==1);
    assert(!em_pickup_original_bind(&metadata,&interaction,path,&hooks));
    EmPickupOwner *owner=em_pickup_original_owner(metadata.uid);
    assert(owner && em_pickup_original_active());
    EmFrameInput input={0}; input.pressed=EM_PAD_CROSS;
    em_pickup_update(metadata.position,0,&input,1);
    assert(owner->armed==0 && s.p[0].armed==0 && scan_slot==-1);
    assert(em_interaction_runtime_claim(&interaction,owner));
    owner->armed=4;
    uint8_t action=short_program?0x2D:0;
    assert(em_pickup_original_tick(229.9f,action,0,0,1)==1);
    assert(owner->phase==1 && s.p[0].program.script.phase==0 && frame.selector==3);
    int frames=0;
    while (!host.pending_status) {
        assert(++frames<100);
        assert(em_interaction_runtime_player_tick(&interaction,1)>=0);
        assert(em_pickup_original_tick(229.9f,action,0,frame.ready,1)==1);
    }
    assert(host.status_calls==1 && host.index==type);
    assert(host.kind==(subtype==0?1:subtype==1?2:3));
    assert(!em_pickup_taken(metadata.uid) && s.p[0].used && owner->lifecycle==1);
    assert(em_pickup_found_take()==-1);
    if (!subtype) assert(em_pickup_item_count(type)==1);
    else if (subtype==1) assert(em_pickup_maps()[type]==1 && !em_pickup_item_count(type));
    else assert(em_pickup_keys()[type]==1 && !em_pickup_item_count(type));
    if (!short_program) assert(host.turn_calls==2 && host.camera_calls==3 && frames>=45);
    EmScript saved=s.p[0].program.script;
    for (unsigned i=0;i<5;++i) {
        assert(em_interaction_runtime_player_tick(&interaction,0)==0);
        assert(em_pickup_original_tick(229.9f,action,0,frame.ready,0)==1);
        assert(memcmp(&saved,&s.p[0].program.script,sizeof saved)==0);
    }
    host.pending_status=0;
    assert(em_interaction_runtime_player_tick(&interaction,1)>=0);
    assert(em_pickup_original_tick(229.9f,action,0,frame.ready,1)==1);
    assert(frame.selector==0 && s.p[0].used);
    assert(owner->lifecycle==(callback==0x219550?3:2));
    assert(em_pickup_taken(metadata.uid)==(callback==0x219550));
    assert(host.sounds==(callback==0x219550));
    assert(em_interaction_runtime_player_tick(&interaction,1)>=0);
    assert(em_pickup_original_tick(229.9f,action,0,frame.ready,1)==1);
    assert(owner->freed && !s.p[0].used && em_pickup_taken(metadata.uid));
    assert(em_pickup_found_take()==-1 && !em_interaction_runtime_owner(&interaction));
    em_pickup_scene_clear(gfx);
    assert(em_pickup_original_bind(&metadata,&interaction,path,&hooks)==-2);
    assert(em_pickup_original_active());
    em_pickup_scene_clear(gfx);
}

static void program_validation(void)
{
    const char *path="build/pickup_owner_reference/truncated.emsc";
    unsigned char bytes[660];
    FILE *file=fopen("assets/scene_snow/pickup_00219550.emsc","rb");
    assert(file && fread(bytes,1,sizeof bytes,file)==sizeof bytes);
    fclose(file);
    EmPickupProgramHooks hooks={NULL,original_frame,original_turn,original_camera,
        original_animation,original_animation_done,original_take};
    EmPickupProgram program={0};
    for (size_t length=0;length<sizeof bytes;++length) {
        file=fopen(path,"wb");assert(file);
        assert(fwrite(bytes,1,length,file)==length);fclose(file);
        assert(!em_pickup_program_load(&program,path,0x219550,&hooks));
        assert(!program.image.bytes);
    }
    const unsigned offsets[]={0,4,8,12,16,20,20+64,20+64+0x24,20+128+0xC,
        20+128+0x14,20+5*64+4,20+6*64};
    for (unsigned i=0;i<sizeof offsets/sizeof offsets[0];++i) {
        bytes[offsets[i]]^=1;
        file=fopen(path,"wb");assert(file);
        assert(fwrite(bytes,1,sizeof bytes,file)==sizeof bytes);fclose(file);
        assert(!em_pickup_program_load(&program,path,0x219550,&hooks));
        assert(!program.image.bytes);
        bytes[offsets[i]]^=1;
    }
    remove(path);
}

int main(void)
{
    program_validation();
    uint8_t status_byte=1,primary=0,secondary=1;
    em_pickup_reset(); em_pickup_equipment_read(&status_byte,&primary,&secondary);
    assert(status_byte==0 && primary==255 && secondary==0);
    em_pickup_equipment_write(2,7,19);
    em_pickup_equipment_read(&status_byte,&primary,&secondary);
    assert(status_byte==2 && primary==7 && secondary==19);
    run(0x219550,0,0x1B,1);
    run(0x15AFA0,1,8,1);
    run(0x219550,2,0x32,1);
    run(0x219550,0,0x1B,0);
    run(0x15AFA0,1,8,0);
    puts("Original pickup binding/program/status-pause/reentry PASS");
    return 0;
}
