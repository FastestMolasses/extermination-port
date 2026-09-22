#include "game/em_opening_actor.h"
#include "em_model.h"
#include "game/em_opening_face.h"
#include "game/em_random.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    EmModel model;
    EmGfxMesh *mesh;
    EmOpeningFace face;
    uint32_t face_first, face_count;
    float *deltas, *positions;
} OpeningActor;

static OpeningActor actors[EM_OPENING_ACTOR_COUNT];
static int ready;
static EmGfx *opening_gfx;
static int face_ticked;
static uint32_t face_tick;

void em_opening_actor_begin(void)
{
    face_ticked=0;
    for(unsigned i=0;i<2;++i) em_opening_face_init(&actors[i].face,1);
}

static uint32_t vertex_word(const float *vertex, unsigned index)
{
    uint32_t value;
    memcpy(&value,vertex+index,sizeof value);
    return value;
}

static int attach_face(OpeningActor *actor, const char *directory,
                       const char *name)
{
    EmModel face={0};
    EmModel *body=&actor->model;
    char path[1024];
    FILE *file=NULL;
    float *vertices=NULL;
    uint32_t *indices=NULL;
    EmModelTex *textures=NULL;
    uint8_t *texels=NULL;
    int success=0;
    int n=snprintf(path,sizeof path,"%s/opening/%s_face.emdl",directory,name);
    if(n<0 || (size_t)n>=sizeof path || em_model_load(&face,path)!=0) goto done;
    if(face.bone_count!=22 || face.frame_count!=1 || face.flags ||
       !face.vert_count || !face.index_count || !face.tex_count ||
       face.vert_count>10000 || body->vert_count>100000 ||
       face.index_count%3 || body->index_count%3) goto done;
    n=snprintf(path,sizeof path,"%s/opening/%s_face.emfm",directory,name);
    if(n<0 || (size_t)n>=sizeof path) goto done;
    file=fopen(path,"rb");
    uint32_t header[5];
    if(!file || fread(header,sizeof header,1,file)!=1 ||
       memcmp(header,"EMFM",4) || header[1]!=1 || header[2]!=face.vert_count ||
       header[3]!=7 || header[4]!=7) goto done;
    actor->deltas=malloc((size_t)face.vert_count*21*sizeof(float));
    if(!actor->deltas || fread(actor->deltas,21*sizeof(float),face.vert_count,file)!=face.vert_count ||
       fgetc(file)!=EOF) goto done;
    for(size_t i=0;i<(size_t)face.vert_count*21;++i)
        if(!isfinite(actor->deltas[i])) goto done;
    uint32_t count=body->vert_count+face.vert_count;
    uint32_t texture_count=body->tex_count+face.tex_count;
    if((uint64_t)body->texel_bytes+face.texel_bytes>UINT32_MAX) goto done;
    uint32_t texel_bytes=body->texel_bytes+face.texel_bytes;
    vertices=malloc((size_t)count*10*sizeof(float));
    indices=malloc(((size_t)body->index_count+face.index_count)*sizeof(uint32_t));
    textures=malloc((size_t)texture_count*sizeof *textures);
    texels=malloc(texel_bytes);
    actor->positions=malloc((size_t)count*3*sizeof(float));
    if(!vertices || !indices || !textures || !texels || !actor->positions) goto done;
    memcpy(vertices,body->verts,(size_t)body->vert_count*10*sizeof(float));
    memcpy(vertices+(size_t)body->vert_count*10,face.verts,(size_t)face.vert_count*10*sizeof(float));
    uint32_t used=0,removed=0;
    for(uint32_t i=0;i<body->index_count;i+=3) {
        unsigned head=0;
        for(unsigned k=0;k<3;++k)
            head+=vertex_word(body->verts+(size_t)body->indices[i+k]*10,8)==7;
        /* 001C7420 collapses actor+94's basis. The original opening assets
         * have no mixed head/body triangles; reject any that would require
         * retaining partially collapsed geometry. */
        if(head==3) {removed+=3;continue;}
        if(head) goto done;
        memcpy(indices+used,body->indices+i,3*sizeof(uint32_t));used+=3;
    }
    if(removed!=face.index_count) goto done;
    for(uint32_t i=0;i<face.index_count;++i) indices[used++]=body->vert_count+face.indices[i];
    for(uint32_t i=0;i<face.vert_count;++i) {
        float *v=vertices+((size_t)body->vert_count+i)*10;
        if(vertex_word(v,8)!=7 || vertex_word(v,9)>=face.tex_count) goto done;
        uint32_t face_bone=7|EM_GFX_VERT_FACE_LIGHT;
        memcpy(v+8,&face_bone,sizeof face_bone);
        uint32_t texture=vertex_word(v,9)+body->tex_count;
        memcpy(v+9,&texture,sizeof texture);
    }
    memcpy(textures,body->texs,(size_t)body->tex_count*sizeof *textures);
    memcpy(textures+body->tex_count,face.texs,(size_t)face.tex_count*sizeof *textures);
    for(uint32_t i=body->tex_count;i<texture_count;++i) textures[i].offset+=body->texel_bytes;
    memcpy(texels,body->texels,body->texel_bytes);
    memcpy(texels+body->texel_bytes,face.texels,face.texel_bytes);
    for(uint32_t i=0;i<count;++i)
        memcpy(actor->positions+(size_t)i*3,vertices+(size_t)i*10,3*sizeof(float));
    actor->face_first=body->vert_count;actor->face_count=face.vert_count;
    free(body->verts);body->verts=vertices;vertices=NULL;
    free(body->indices);body->indices=indices;indices=NULL;
    free(body->texs);body->texs=textures;textures=NULL;
    free(body->texels);body->texels=texels;texels=NULL;
    body->vert_count=count;body->index_count=used;
    body->tex_count=texture_count;body->texel_bytes=texel_bytes;
    /* B81D0 selects speed1 for Dennis; BA8E0 selects1 for model47. */
    em_opening_face_init(&actor->face,1);
    success=1;
done:
    if(file) fclose(file);
    free(vertices);free(indices);free(textures);free(texels);
    em_model_free(&face);
    return success;
}

void em_opening_actor_shutdown(EmGfx *gfx)
{
    for (unsigned i=0; i<EM_OPENING_ACTOR_COUNT; ++i) {
        if (actors[i].mesh) em_gfx_mesh_destroy(gfx,actors[i].mesh);
        actors[i].mesh=NULL;
        em_model_free(&actors[i].model);
        free(actors[i].deltas);free(actors[i].positions);
        memset(&actors[i],0,sizeof actors[i]);
    }
    ready=0;
    opening_gfx=NULL;
    face_ticked=0;
}

int em_opening_actor_init(EmGfx *gfx, const char *scene_dir)
{
    static const char *names[EM_OPENING_ACTOR_COUNT]={
        "player", "roger", "equipment_6b"
    };
    static const uint32_t bones[EM_OPENING_ACTOR_COUNT]={22,22,2};
    static const uint32_t clip_ids[EM_OPENING_ACTOR_COUNT]={1,2,0};
    em_opening_actor_shutdown(gfx);
    if (!gfx || !scene_dir) return 0;
    for (unsigned i=0; i<EM_OPENING_ACTOR_COUNT; ++i) {
        char path[1024];
        int length=snprintf(path,sizeof path,"%s/opening/%s.emdl",scene_dir,names[i]);
        if (length<0 || (size_t)length>=sizeof path) goto fail;
        EmModel *m=&actors[i].model;
        if (em_model_load(m,path)!=0) goto fail;
        /* These files have one frame per half source tick, plus the exporter
         * identity slot. Reject an ordinary locomotion asset accidentally
         * placed here: it would move these actors in a fabricated pose. */
        if (m->bone_count!=bones[i] || m->frame_count!=1291 ||
            m->fps!=60.0f || m->clip_count!=1 ||
            m->clips[0].id!=clip_ids[i] || m->clips[0].first_frame!=0 ||
            m->clips[0].frame_count!=m->frame_count || !m->tex_count ||
            !m->vert_count || !m->index_count) {
            fprintf(stderr,"opening: invalid original actor track %s\n",path);
            goto fail;
        }
        if(i<2 && !attach_face(&actors[i],scene_dir,names[i])) {
            fprintf(stderr,"opening: missing or invalid original face %s\n",names[i]);
            goto fail;
        }
        actors[i].mesh=em_gfx_mesh_create(gfx,m->verts,m->vert_count,
                           m->indices,m->index_count,
                           (const EmGfxTexDesc *)m->texs,m->tex_count,
                           m->texels,m->flags);
        if (!actors[i].mesh) goto fail;
    }
    ready=1;
    opening_gfx=gfx;
    return 1;
fail:
    em_opening_actor_shutdown(gfx);
    return 0;
}

static uint32_t face_random(void *context)
{
    (void)context;
    return em_random_next();
}

int em_opening_actor_tick(uint32_t half_tick, unsigned talk_mask)
{
    if(!ready || (!face_ticked && half_tick!=0)) return 0;
    if(face_ticked && half_tick==face_tick) return 1;
    if(face_ticked && (face_tick==UINT32_MAX || half_tick!=face_tick+1)) return 0;
    for(unsigned i=0;i<2;++i) {
        OpeningActor *a=&actors[i];
        uint8_t talk=(uint8_t)((talk_mask>>i)&1);
        if(a->face.talking!=talk) em_opening_face_talk(&a->face,talk);
        em_opening_face_tick(&a->face,face_random,NULL);
        for(uint32_t j=0;j<a->face_count;++j) {
            size_t vertex=(size_t)a->face_first+j;
            em_opening_face_position(a->positions+vertex*3,a->model.verts+vertex*10,
                                    a->deltas+(size_t)j*21,a->face.weight);
        }
        if(!em_gfx_mesh_update_positions(opening_gfx,a->mesh,a->positions,a->model.vert_count))
            return 0;
    }
    face_ticked=1;face_tick=half_tick;
    return 1;
}

int em_opening_actor_record(unsigned index, uint32_t half_tick,
                           EmGfxMesh **mesh, const float **palette,
                           uint32_t *bone_count)
{
    if (!ready || index>=EM_OPENING_ACTOR_COUNT || !mesh || !palette ||
        !bone_count) return 0;
    const OpeningActor *a=&actors[index];
    /* 001BB0E0 binds in phase0 and falls through into the first 0.5-rate
     * advance before evaluation. The captured player and Roger clocks are
     * likewise 0.5 ahead of the camera sample from the same original frame.
     * Clamp before adding, including callers' UINT32_MAX terminal query. */
    uint32_t last=a->model.frame_count-1;
    uint32_t frame=half_tick<last ? half_tick+1 : last;
    *mesh=a->mesh;
    *palette=a->model.palette+(size_t)frame*a->model.bone_count*16;
    *bone_count=a->model.bone_count;
    return 1;
}

void em_opening_actor_draw(EmGfx *gfx, const float *viewproj,
                          uint32_t half_tick)
{
    for (unsigned i=0; i<EM_OPENING_ACTOR_COUNT; ++i) {
        EmGfxMesh *mesh;
        const float *palette;
        uint32_t bones;
        if (em_opening_actor_record(i,half_tick,&mesh,&palette,&bones))
            em_gfx_draw_skinned(gfx,mesh,viewproj,palette,bones);
    }
}
