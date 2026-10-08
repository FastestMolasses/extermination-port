/* Actual exported pickup programs and native owner/runtime adapter.
 * Model/GPU and the player stage are explicit deterministic fixture
 * boundaries (the stage: 0015B130's admission, 00183090 + 001C64F0 over the
 * record's +1F2 request, 0015B530's 00182DF0; executed against the original
 * by test_player_stage_workers_reference, in play by the level smoke's
 * battery and branch takes); owner, sequencer, frame command, the request
 * on the player record and inventory code are the real modules. */
#define main pickup_light_fixture_main
#include "pickup_light_test.c"
#undef main

typedef struct {
    int status_calls, pending_status, turn_calls, camera_calls, sounds, publications, draws;
    uint8_t kind, index;
    uint8_t record[0x320];      /* the player record D_008102B0 */
    int commit_stage, stages;   /* the stage that committed +1F2, the stage count */
    EmInteractionFrame *frame;
    EmInteractionRuntime *interaction;
} Host;

static uint8_t *player_record(void *context) { return ((Host *)context)->record; }

/* The original's first end flag of the grab clips 0x40 / 0x41 / 0x42
 * requested with blend 1.0 (the take programs' op0A sub 0, the clip 0015AE20
 * patched in by height): 46 stages after 00183090's commit stage (the
 * original 00183090 + 001C64F0 over the source bank,
 * tools/clip_clock_oracle.py). */
#define GRAB_END_STAGES 46

/* One player stage before the owner (the original task order): 0015B130's
 * prelude admits the player under the selector (3B8F = 1), 00183090
 * commits a changed +1F2, the advance raises +200's 0x1000 at the
 * original's stage, and once the selector clears 0015B530's 00182DF0
 * releases the player and the token ends. */
static void stage(Host *host)
{
    ++host->stages;
    if (!em_interaction_runtime_owner(host->interaction)) return;
    if (host->frame->selector && !host->frame->player_ready) { host->frame->player_ready=1; return; }
    uint16_t clip; memcpy(&clip,host->record+0x1F2,2);
    uint32_t flags=0;
    if (clip && host->commit_stage<0) { assert(clip>=0x40 && clip<=0x42); host->commit_stage=host->stages; }
    else if (host->commit_stage>=0 && host->stages-host->commit_stage>=GRAB_END_STAGES) flags=0x1000;
    memcpy(host->record+0x200,&flags,4);
    if (host->frame->player_ready && !host->frame->selector) {
        host->frame->player_ready=0;
        assert(em_interaction_runtime_release(host->interaction));
    }
}
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
    /* A visible owner's +0x4C (001CAA00 over its record): the host's. */
    else if (event==EM_PICKUP_OWNER_DRAW) { assert(host->draws<host->publications); ++host->draws; }
    else if (event==EM_PICKUP_OWNER_TAKE_SOUND) { assert(argument==0x194); ++host->sounds; }
    else assert(event==EM_PICKUP_OWNER_AURA);
    return 1;
}

static void run(uint32_t callback, uint8_t subtype, uint16_t type, int short_program)
{
    static Host host;
    memset(&host,0,sizeof host);
    host.commit_stage=-1;
    EmGfx *gfx=(EmGfx *)(uintptr_t)1;
    em_pickup_reset();
    EmInteractionFrame frame={0}; EmInteractionRuntime interaction;
    host.frame=&frame; host.interaction=&interaction;
    EmInteractionRuntimeHooks runtime_hooks={&host,frame_event,retarget,player_record};
    assert(em_interaction_runtime_init(&interaction,&frame,&runtime_hooks));
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
    assert(owner->armed==0);
    assert(em_interaction_runtime_claim(&interaction,owner));
    owner->armed=4;
    uint8_t action=short_program?0x2D:0;
    assert(em_pickup_original_tick(229.9f,action,0,0,1)==1);
    assert(owner->phase==1 && s.p[0].program.script.phase==0 && frame.selector==3);
    {   /* the live, visible, bound owner: no legacy instance draw */
        const float *unused_palette; EmGfxMesh *unused_mesh; uint32_t unused_bones;
        assert(s.p[0].used && host.draws==1 &&
               !em_pickup_draw(0,&unused_mesh,&unused_palette,&unused_bones));
    }
    int frames=0;
    while (!host.pending_status) {
        assert(++frames<100);
        stage(&host);
        assert(em_pickup_original_tick(229.9f,action,0,frame.ready,1)==1);
    }
    assert(host.status_calls==1 && host.index==type);
    assert(host.kind==(subtype==0?1:subtype==1?2:3));
    assert(!em_pickup_taken(metadata.uid) && s.p[0].used && owner->lifecycle==1);
    assert(!em_scene_state()->req[EM_SCENE_REQ_B0]); /* the request goes through the hook */
    if (!subtype) assert(em_pickup_item_count(type)==1);
    else if (subtype==1) assert(em_pickup_maps()[type]==1 && !em_pickup_item_count(type) &&
                                em_pickup_item_count(0x54+type)==1); /* CB8[t] is C64[0x54+t] */
    else assert(em_pickup_keys()[type]==1 && !em_pickup_item_count(type) &&
                em_pickup_item_count(0x5F+type)==1); /* CC3[t] is C64[0x5F+t] */
    if (!short_program) assert(host.turn_calls==2 && host.camera_calls==3 && frames>=45);
    {   /* The grab program's op0A sub 0 on the record: the clip 0015AE20
         * chose (the item at the player's 229.9, below its + 6: 0x42), blend 1.0,
         * rate 1.0; its sub 3 held the program until the end flag. The
         * no-grab program (action 0x2D) requests no clip. */
        uint16_t clip; uint32_t rate,blend,flags;
        memcpy(&clip,host.record+0x1F2,2); memcpy(&rate,host.record+0x1F4,4);
        memcpy(&blend,host.record+0x1F8,4); memcpy(&flags,host.record+0x200,4);
        if (short_program) assert(!clip && !rate && !blend && host.commit_stage<0);
        else {
            assert(clip==0x42 && rate==0x3F800000u && blend==0x3F800000u && (flags&0x1000));
            assert(host.stages-host.commit_stage>=GRAB_END_STAGES);
        }
    }
    EmScript saved=s.p[0].program.script;
    for (unsigned i=0;i<5;++i) {
        assert(em_pickup_original_tick(229.9f,action,0,frame.ready,0)==1);
        assert(memcmp(&saved,&s.p[0].program.script,sizeof saved)==0);
    }
    host.pending_status=0;
    stage(&host);
    assert(em_pickup_original_tick(229.9f,action,0,frame.ready,1)==1);
    assert(frame.selector==0 && s.p[0].used);
    assert(owner->lifecycle==(callback==0x219550?3:2));
    assert(em_pickup_taken(metadata.uid)==(callback==0x219550));
    assert(host.sounds==(callback==0x219550));
    stage(&host);
    assert(em_pickup_original_tick(229.9f,action,0,frame.ready,1)==1);
    assert(owner->freed && !s.p[0].used && em_pickup_taken(metadata.uid));
    /* Every publication found the owner visible (the fixture's 001B17A0
     * answers 1), so every one was followed by its +0x4C; the legacy
     * instance never draws a bound owner. */
    assert(host.publications>0 && host.draws==host.publications);
    assert(!em_scene_state()->req[EM_SCENE_REQ_B0] && !em_interaction_runtime_owner(&interaction));
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
