#include "game/em_interaction_scene.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void save(const char *path,const unsigned char *bytes,size_t count)
{
    FILE *file=fopen(path,"wb");assert(file);
    assert(fwrite(bytes,1,count,file)==count);assert(fclose(file)==0);
}

static int eligible(void *context,const EmInteractionCandidate *candidate,float *score)
{
    const EmInteractionSceneOwner *owner=candidate->owner;
    assert(owner->native_owner==context);
    *score=1;
    return 1;
}

int main(int argc,char **argv)
{
    assert(argc==2);
    EmInteractionScene scene;
    assert(em_interaction_scene_load(&scene,argv[1]));
    assert(scene.count==11);
    EmInteractionSceneOwner *pickup=em_interaction_scene_pickup(&scene,0x0b01);
    EmInteractionSceneOwner *panel=em_interaction_scene_role(&scene,EM_INTERACTION_PANEL);
    EmInteractionSceneOwner *elevator=em_interaction_scene_role(&scene,EM_INTERACTION_ELEVATOR);
    assert(pickup && panel && elevator);
    assert(em_interaction_scene_role(&scene,EM_INTERACTION_DOOR));
    assert(em_interaction_scene_role(&scene,EM_INTERACTION_ROGER));
    assert(!em_interaction_scene_role(&scene,EM_INTERACTION_PICKUP));
    uint8_t status[32],flags[32],armed[32]={0};
    int context;
    const float position[3]={0,0,10},anchor[3]={0,0,0},forward[3]={0,0,1};
    assert(em_interaction_scene_offer(&scene,pickup->source_id,position,anchor,forward)==-1);
    assert(strstr(scene.error,"no live controller binding"));
    for (size_t i=0;i<scene.count;++i) {
        EmInteractionSceneOwner *owner=&scene.owners[i];
        status[i]=owner->initial_status;flags[i]=owner->class_flags;
        assert(em_interaction_scene_bind(&scene,owner->source_id,&context,&status[i],&flags[i],&armed[i]));
        assert(em_interaction_scene_offer(&scene,owner->source_id,position,anchor,forward)==1);
    }
    em_interaction_scene_publish(&scene);
    assert(scene.list.active_count==11 && !scene.list.pending_count);
    /* Mutation AFTER publication proves canonical metadata is refreshed. */
    status[10]=2;
    EmInteractionScanState state={0};size_t winner;
    assert(em_interaction_scene_scan(&scene,&state,eligible,&context,&winner)==1);
    assert(winner==1 && armed[9]==4 && !armed[10]);
    scene.owners[9].live_armed=NULL;
    state.score=7;
    assert(em_interaction_scene_scan(&scene,&state,NULL,NULL,&winner)==0 && state.score==7);
    state.selector=0;
    assert(em_interaction_scene_scan(&scene,&state,eligible,&context,&winner)==-1);

    FILE *file=fopen(argv[1],"rb");assert(file);
    unsigned char bytes[4096];size_t size=fread(bytes,1,sizeof bytes,file);fclose(file);
    assert(size==976);
    const char *path="build/interaction_scan_reference/invalid.emis";
    for (size_t cut=0;cut<size;++cut) {
        save(path,bytes,cut);
        assert(!em_interaction_scene_load(&scene,path) && !scene.count && scene.error[0]);
    }
    unsigned char changed[4097];
    const size_t malformed_offsets[]={0,4,8,12,16,96+4,96+23,96+25,96+26,96+28};
    for (unsigned i=0;i<sizeof malformed_offsets/sizeof malformed_offsets[0];++i) {
        memcpy(changed,bytes,size);changed[malformed_offsets[i]]=0xff;
        save(path,changed,size);assert(!em_interaction_scene_load(&scene,path));
    }
    memcpy(changed,bytes,size);memcpy(changed+96+80,changed+96,4);
    save(path,changed,size);assert(!em_interaction_scene_load(&scene,path));
    memcpy(changed,bytes,size);changed[size]=0;
    save(path,changed,size+1);assert(!em_interaction_scene_load(&scene,path));
    assert(em_interaction_scene_load(&scene,argv[1]));
    assert(!scene.list.active_count && !scene.owners[0].native_owner);
    puts("EMIS: 11 real owners, canonical live refresh, all truncations/malformed data, gated/re-entry bindings PASS");
    return 0;
}
