#include "game/em_face_model.h"
#include "em_gfx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void em_face_model_free(EmFaceModel *face)
{
    if (!face) return;
    free(face->deltas);free(face->positions);
    memset(face,0,sizeof *face);
}

static uint32_t vertex_word(const float *vertex, unsigned index)
{
    uint32_t value;
    memcpy(&value,vertex+index,sizeof value);
    return value;
}

int em_face_model_attach(EmFaceModel *actor, EmModel *body,
                         const char *mesh_path, const char *morph_path)
{
    EmModel face={0};
    if (!actor || !body || !mesh_path || !morph_path || actor->positions || actor->deltas) return 0;
    FILE *file=NULL;
    float *vertices=NULL;
    uint32_t *indices=NULL;
    EmModelTex *textures=NULL;
    uint8_t *texels=NULL;
    int success=0;
    if(em_model_load(&face,mesh_path)!=0) goto done;
    if(face.bone_count!=22 || face.frame_count!=1 || face.flags ||
       !face.vert_count || !face.index_count || !face.tex_count ||
       face.vert_count>10000 || body->vert_count>100000 ||
       face.index_count%3 || body->index_count%3) goto done;
    file=fopen(morph_path,"rb");
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
        /* 001C7420 collapses actor+94's basis. These original model47 assets
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
    actor->first=body->vert_count;actor->count=face.vert_count;
    free(body->verts);body->verts=vertices;vertices=NULL;
    free(body->indices);body->indices=indices;indices=NULL;
    free(body->texs);body->texs=textures;textures=NULL;
    free(body->texels);body->texels=texels;texels=NULL;
    body->vert_count=count;body->index_count=used;
    body->tex_count=texture_count;body->texel_bytes=texel_bytes;
    /* B81D0 selects speed1 for Dennis; BA8E0 selects1 for model47. */
    em_opening_face_init(&actor->state,1);
    success=1;
done:
    if(file) fclose(file);
    free(vertices);free(indices);free(textures);free(texels);
    em_model_free(&face);
    if (!success) em_face_model_free(actor);
    return success;
}

int em_face_model_tick(EmFaceModel *face, const EmModel *body, uint8_t *activity,
                       EmFaceRandom random, void *context)
{
    if (!face || !body || !activity || !random || !face->positions || !face->deltas ||
        face->first>body->vert_count || face->count!=body->vert_count-face->first) return 0;
    if (*activity==1 || *activity==2) {
        em_opening_face_talk(&face->state,(uint8_t)(*activity==1));
        *activity=0;
    }
    em_opening_face_tick(&face->state,random,context);
    for (uint32_t i=0;i<face->count;++i) {
        size_t vertex=(size_t)face->first+i;
        em_opening_face_position(face->positions+vertex*3,body->verts+vertex*10,
                                 face->deltas+(size_t)i*21,face->state.weight);
    }
    return 1;
}
