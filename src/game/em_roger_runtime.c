#include "game/em_roger_runtime.h"
#pragma STDC FP_CONTRACT OFF
#include "em_math.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int path(char *out,size_t size,const char *directory,const char *name)
{
    int count=snprintf(out,size,"%s/%s",directory,name);
    return count>0 && (size_t)count<size;
}

int em_roger_runtime_set_placement(EmRogerRuntime *r,const float position[3],const float angles[3])
{
    if (!r || !position || !angles) return 0;
    for (unsigned i=0;i<3;++i)
        if (!isfinite(position[i]) || !isfinite(angles[i])) return 0;
    memcpy(r->position,position,sizeof r->position);
    memcpy(r->angles,angles,sizeof r->angles);
    r->owner.yaw=angles[1];
    return 1;
}

static int build_pose(EmRogerRuntime *r)
{
    float local[22*16];
    r->angles[1]=r->owner.yaw;
    if (!r->hooks.owner_matrix(r->hooks.context,r->position,r->angles,r->owner_matrix) ||
        !em_player_pose_palette(&r->pose,local,22)) return 0;
    /* Captured comparisons establish a tolerance for this finite host
     * multiplication, not byte-identical VU matrix results. */
    for (unsigned i=0;i<22;++i) {
        em_mat4_mul(r->palette+i*16,r->owner_matrix,local+i*16);
        for (unsigned k=0;k<16;++k) if (!isfinite(r->palette[i*16+k])) return 0;
    }
    return 1;
}

int em_roger_runtime_animation(EmRogerRuntime *r,uint16_t bank,uint16_t clip,
                                unsigned blend,float source_frame)
{
    if (!r || !r->ready || r->failed || (bank!=0x4A && bank!=0x96)) return 0;
    const EmPoseBank *previous=r->pose.bank;
    r->pose.bank=bank==0x4A ? &r->assets.animation : &r->encounter;
    if (!em_player_pose_select(&r->pose,clip,source_frame,blend,1)) {
        r->pose.bank=previous;
        return 0;
    }
    r->bank=bank;
    return 1;
}

static unsigned char *resolve(void *context,uint32_t address)
{
    EmRogerRuntime *r=context;
    return em_script_image_read(&r->assets.programs,address,64);
}

static EmScriptCommandResult execute(void *context,EmScript *script,unsigned char *record)
{
    EmRogerRuntime *r=context;
    EmRogerCommand worker;
    unsigned sub=em_script_u32(record,8);
    switch (em_script_u32(record,0)&0xFFF) {
    case 1:
        /* 1B94F0/sub0 and10 copy raw owner position/angles. In the real
         * encounter sub10 first zeros both, then restores authored placement. */
        if (sub==0 || sub==10) {
            float position[3],angles[3];
            for (unsigned i=0;i<3;++i) {
                position[i]=em_script_f32(record,(sub==0?0x30:0x20)+4*i);
                angles[i]=sub==0 ? r->angles[i] : em_script_f32(record,0x30+4*i);
            }
            return em_roger_runtime_set_placement(r,position,angles) ? EM_SCRIPT_ADVANCE : EM_SCRIPT_UNSUPPORTED;
        }
        worker=r->hooks.scene;break;
    case 0: worker=r->hooks.camera;break;
    case 7: worker=r->hooks.frame;break;
    case 10: worker=r->hooks.player;break;
    case 11:
        /* 1B8020/sub4 switches bank, initializes at source0/blend0 and
         * clears the script's animation-result halfword. */
        if (sub==4) {
            if (!em_roger_runtime_animation(r,(uint16_t)em_script_u32(record,0x1C),
                                             (uint16_t)em_script_u32(record,0x14),0,0))
                return EM_SCRIPT_UNSUPPORTED;
            r->owner.animation_result=0;
            return EM_SCRIPT_ADVANCE;
        }
        worker=r->hooks.actor;break;
    case 21: worker=r->hooks.message;break;
    default: worker=r->hooks.scene;break;
    }
    return worker ? worker(r->hooks.context,r,script,record) : EM_SCRIPT_UNSUPPORTED;
}

static int start_script(void *context,uint32_t address)
{
    EmRogerRuntime *r=context;
    if (address!=0x8283D0 && address!=0x828810 && address!=0x828990 && address!=0x828A10)
        return 0;
    em_script_start(&r->script,address);
    return 1;
}

static int tick_script(void *context)
{
    EmRogerRuntime *r=context;
    EmScriptResult result=em_script_tick(&r->script,resolve,execute,r);
    if (result==EM_SCRIPT_YIELDED) return 0;
    /* BA1F0 returns1 for STOP and3 for abort/skip. The Roger caller tests
     * nonzero, so either takes its completion branch. Unsupported handlers
     * are the separate native fault result and must never enter it. */
    if (result==EM_SCRIPT_FINISHED || result==EM_SCRIPT_ABORTED) return 1;
    return -1;
}

static int trigger(void *context)
{
    EmRogerRuntime *r=context;
    return em_roger_trigger(r->math,r->player,r->assets.trigger);
}

static int animation_init(void *context,uint16_t clip,float blend,float start)
{
    EmRogerRuntime *r=context;
    if (!isfinite(blend) || blend<0 || blend>65535) return 0;
    return em_roger_runtime_animation(r,r->bank,clip,(unsigned)blend,start);
}

static int animation_tick(void *context,float rate,uint16_t *result)
{
    EmRogerRuntime *r=context;
    if (!em_player_pose_advance(&r->pose,rate,0)) return 0;
    *result=(uint16_t)r->pose.flags;
    return 1;
}

static int publication(void *context)
{
    EmRogerRuntime *r=context;
    r->angles[1]=r->owner.yaw;
    return r->hooks.publish(r->hooks.context,r);
}

static int event(void *context,EmRogerEvent type,unsigned argument)
{
    EmRogerRuntime *r=context;
    switch (type) {
    case EM_ROGER_RESTORE_DEFAULT_BANK:
        if (argument!=0x4A) return 0;
        r->bank=0x4A;return 1;
    case EM_ROGER_FACE_UPDATE:
        if (argument!=0x47 || !em_face_model_tick(&r->face,&r->assets.model,r->face_activity,
                                                r->hooks.random,r->hooks.context)) return 0;
        return em_gfx_mesh_update_positions(r->gfx,r->mesh,r->face.positions,r->assets.model.vert_count);
    case EM_ROGER_BUILD_POSE: return build_pose(r);
    case EM_ROGER_DRAW: r->draw_pending=argument!=0;return 1;
    case EM_ROGER_FREE:
        if (r->hooks.event(r->hooks.context,r,type,argument)!=1) return 0;
        r->owner.status=0;r->owner.class_flags=0;r->draw_pending=0;
        return 1;
    default: return r->hooks.event(r->hooks.context,r,type,argument);
    }
}

void em_roger_runtime_free(EmRogerRuntime *r)
{
    if (!r) return;
    if (r->mesh) em_gfx_mesh_destroy(r->gfx,r->mesh);
    em_face_model_free(&r->face);
    em_pose_bank_free(&r->encounter);
    em_roger_assets_free(&r->assets);
    memset(r,0,sizeof *r);
}

int em_roger_runtime_load(EmRogerRuntime *r,EmGfx *gfx,const char *directory,
    const EmInteractionSceneOwner *source,const EmInteractionMath *math,EmRogerStory *story,
    uint8_t *activity,const EmRogerRuntimeHooks *hooks)
{
    if (!r || !gfx || !directory || !source || !math || !story || !activity || !hooks ||
        !hooks->owner_matrix || !hooks->publish || !hooks->event || !hooks->random ||
        !hooks->frame || !hooks->camera || !hooks->player || !hooks->actor ||
        !hooks->message || !hooks->scene || source->source_id!=0x82A500 ||
        source->role!=EM_INTERACTION_ROGER || source->callback!=0x8237E0 ||
        source->initial_status!=1 || source->class_flags!=0xAA || source->subtype!=1 ||
        source->selector!=0 || source->descriptor[0]!=10 || source->descriptor[1]!=20) return 0;
    memset(r,0,sizeof *r);
    r->gfx=gfx;r->hooks=*hooks;r->story=story;r->face_activity=activity;r->math=math;
    char name[1024],morph[1024];
    if (!path(name,sizeof name,directory,"roger") || !em_roger_assets_load(&r->assets,name) ||
        !path(name,sizeof name,directory,"roger/encounter_roger.empc") ||
        !em_pose_bank_load(&r->encounter,name) || r->encounter.bone_count!=21 ||
        r->encounter.clip_count!=1 || r->encounter.clips[0].id!=2 ||
        r->encounter.clips[0].duration!=691 || r->encounter.clips[0].next_clip!=-2 ||
        r->encounter.clips[0].blend ||
        !path(name,sizeof name,directory,"opening/roger_face.emdl") ||
        !path(morph,sizeof morph,directory,"opening/roger_face.emfm") ||
        !em_face_model_attach(&r->face,&r->assets.model,name,morph) ||
        !em_player_pose_init(&r->pose,&r->assets.animation,8,0) ||
        !em_roger_runtime_set_placement(r,source->position,source->angles)) goto fail;
    for (unsigned i=0;i<21;++i)
        if (r->encounter.parents[i]!=r->assets.animation.parents[i]) goto fail;
    r->owner.status=source->initial_status;r->owner.class_flags=source->class_flags;
    r->owner.lifecycle=1;r->owner.model_kind=0x47;
    memcpy(r->descriptor,source->descriptor,sizeof r->descriptor);
    r->bank=0x4A;
    EmModel *m=&r->assets.model;
    r->mesh=em_gfx_mesh_create(gfx,m->verts,m->vert_count,m->indices,m->index_count,
                                (const EmGfxTexDesc *)m->texs,m->tex_count,m->texels,m->flags);
    if (!r->mesh || !build_pose(r)) goto fail;
    r->ready=1;
    return 1;
fail:
    em_roger_runtime_free(r);
    return 0;
}

int em_roger_runtime_tick(EmRogerRuntime *r,const float player[3],int ordinary)
{
    if (!r || !r->ready || r->failed || !player) return -1;
    if (!ordinary) return 0;
    for (unsigned i=0;i<3;++i) if (!isfinite(player[i])) {
        r->failed=1;
        return -1;
    }
    memcpy(r->player,player,sizeof r->player);
    r->draw_pending=0;
    EmRogerHooks hooks={r,start_script,tick_script,trigger,animation_init,
                       animation_tick,publication,event};
    int result=em_roger_tick(&r->owner,r->story,&hooks);
    if (result<0) r->failed=1;
    return result;
}

int em_roger_runtime_record(const EmRogerRuntime *r,EmGfxMesh **mesh,
                            const float **palette,uint32_t *bones)
{
    if (!r || !r->ready || r->failed || !mesh || !palette || !bones) return -1;
    if (!r->draw_pending || r->owner.freed) return 0;
    *mesh=r->mesh;*palette=r->palette;*bones=22;
    return 1;
}

const EmModel *em_roger_runtime_model(const EmRogerRuntime *r)
{ return r && r->ready && !r->failed ? &r->assets.model : NULL; }
