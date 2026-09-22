#include "game/em_roger_assets.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void em_roger_assets_free(EmRogerAssets *a)
{
    if (!a) return;
    em_model_free(&a->model);
    em_pose_bank_free(&a->animation);
    em_script_image_free(&a->programs);
    memset(a,0,sizeof *a);
}

static int path(char *buffer,size_t capacity,const char *directory,const char *name)
{
    int count=snprintf(buffer,capacity,"%s/%s",directory,name);
    return count>0 && (size_t)count<capacity;
}

int em_roger_assets_load(EmRogerAssets *a,const char *directory)
{
    if (!a || !directory) return 0;
    memset(a,0,sizeof *a);
    char filename[1024];
    if (!path(filename,sizeof filename,directory,"roger.emdl") ||
        em_model_load(&a->model,filename)!=0 || a->model.bone_count!=22 ||
        a->model.clip_count!=1 || a->model.clips[0].id!=8 ||
        a->model.clips[0].frame_count!=1) goto fail;
    if (!path(filename,sizeof filename,directory,"channels.empc") ||
        !em_pose_bank_load(&a->animation,filename) || a->animation.bone_count!=21 ||
        a->animation.clip_count!=9) goto fail;
    static const uint16_t durations[9]={240,300,240,360,300,60,60,180,180};
    for (unsigned i=0;i<9;++i)
        if (a->animation.clips[i].id!=i || a->animation.clips[i].duration!=durations[i] ||
            a->animation.clips[i].next_clip!=-1 || a->animation.clips[i].blend) goto fail;
    if (!path(filename,sizeof filename,directory,"programs.emsc") ||
        !em_script_image_load(&a->programs,filename) || a->programs.base!=0x8283D0 ||
        a->programs.entry!=0x8283D0 || a->programs.length!=0x800) goto fail;
    static const unsigned starts[4]={0,0x440,0x5C0,0x640};
    static const unsigned counts[4]={17,6,2,7};
    for (unsigned i=0;i<4;++i) {
        const unsigned char *record=a->programs.bytes+starts[i]+64*(counts[i]-1);
        if (!(em_script_u32(record,0)&0x80000000)) goto fail;
    }
    if (!path(filename,sizeof filename,directory,"trigger.empg")) goto fail;
    FILE *file=fopen(filename,"rb");
    if (!file) goto fail;
    unsigned char header[16];
    int valid=fread(header,1,16,file)==16 && !memcmp(header,"EMPG",4) &&
        em_script_u32(header,4)==1 && em_script_u32(header,8)==0 && em_script_u32(header,12)==4 &&
        fread(a->trigger,1,sizeof a->trigger,file)==sizeof a->trigger && fgetc(file)==EOF;
    fclose(file);
    if (!valid) goto fail;
    for (unsigned i=0;i<4;++i)
        for (unsigned j=0;j<4;++j)
            if (!isfinite(a->trigger[i][j])) goto fail;
    return 1;
fail:
    em_roger_assets_free(a);
    return 0;
}
