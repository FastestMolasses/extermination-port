#include "game/em_player_face_host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t vertex_word(const float *vertex, unsigned index)
{
    uint32_t result;
    memcpy(&result, vertex + index, sizeof result);
    return result;
}

static int valid_body(const EmModel *model)
{
    if (!model || model->bone_count != 22 || !model->parents || model->flags ||
        !model->vert_count || model->vert_count > 100000 || !model->verts ||
        !model->index_count || model->index_count > 1000000 ||
        model->index_count % 3 || !model->indices || !model->tex_count ||
        model->tex_count > 1024 || !model->texs || !model->texel_bytes ||
        model->texel_bytes > 64u*1024u*1024u || !model->texels) return 0;
    for (uint32_t i = 0; i < model->vert_count; ++i) {
        const float *v = model->verts + (size_t)i * EM_MODEL_VERT_WORDS;
        const uint32_t bone = vertex_word(v, 8), texture = vertex_word(v, 9);
        if (bone & EM_GFX_VERT_FACE_LIGHT) return 0; /* already attached */
        if ((bone & EM_MODEL_VERT_BONE_MASK) >= 22 ||
            (texture != EM_MODEL_NO_TEX && texture >= model->tex_count)) return 0;
        for (unsigned j = 0; j < 8; ++j) if (!isfinite(v[j])) return 0;
    }
    for (uint32_t i = 0; i < model->index_count; ++i)
        if (model->indices[i] >= model->vert_count) return 0;
    for (uint32_t i = 0; i < model->tex_count; ++i) {
        const EmModelTex *texture = &model->texs[i];
        if (!texture->width || !texture->height ||
            (uint64_t)texture->offset + (uint64_t)texture->width * texture->height * 4 >
            model->texel_bytes) return 0;
    }
    return 1;
}

static void *copy_block(const void *source, size_t bytes)
{
    void *result = malloc(bytes);
    if (result) memcpy(result, source, bytes);
    return result;
}

static int copy_render_model(EmModel *out, const EmModel *source)
{
    out->bone_count = source->bone_count;
    out->vert_count = source->vert_count;
    out->index_count = source->index_count;
    out->tex_count = source->tex_count;
    out->flags = source->flags;
    out->texel_bytes = source->texel_bytes;
    out->parents = copy_block(source->parents, source->bone_count * sizeof *source->parents);
    out->verts = copy_block(source->verts, (size_t)source->vert_count * EM_MODEL_VERT_WORDS * sizeof(float));
    out->indices = copy_block(source->indices, source->index_count * sizeof *source->indices);
    out->texs = copy_block(source->texs, source->tex_count * sizeof *source->texs);
    out->texels = copy_block(source->texels, source->texel_bytes);
    return out->parents && out->verts && out->indices && out->texs && out->texels;
}

static int valid_face_asset(const char *path)
{
    EmModel face = {0};
    if (em_model_load(&face, path)) return 0;
    int valid = valid_body(&face) && face.frame_count == 1;
    for (uint32_t i = 0; valid && i < face.vert_count; ++i)
        valid = vertex_word(face.verts + (size_t)i * EM_MODEL_VERT_WORDS, 8) == 7;
    em_model_free(&face);
    return valid;
}

void em_player_face_host_free(EmPlayerFaceHost *host)
{
    if (!host) return;
    if (host->mesh) em_gfx_mesh_destroy(host->gfx, host->mesh);
    em_face_model_free(&host->face);
    em_model_free(&host->model);
    memset(host, 0, sizeof *host);
}

int em_player_face_host_load(EmPlayerFaceHost *host, EmGfx *gfx, const EmModel *ordinary,
                            const char *directory, EmFaceRandom random, void *context)
{
    if (!host || host->ready || host->mesh || host->model.verts ||
        !gfx || !directory || !random || !valid_body(ordinary)) return 0;
    EmPlayerFaceHost next = {0};
    next.gfx = gfx;
    next.random = random;
    next.random_context = context;
    char mesh[1024], morph[1024];
    int a = snprintf(mesh, sizeof mesh, "%s/opening/player_face.emdl", directory);
    int b = snprintf(morph, sizeof morph, "%s/opening/player_face.emfm", directory);
    if (a < 0 || (size_t)a >= sizeof mesh || b < 0 || (size_t)b >= sizeof morph ||
        !valid_face_asset(mesh) ||
        !copy_render_model(&next.model, ordinary) ||
        !em_face_model_attach(&next.face, &next.model, mesh, morph)) goto fail;
    next.mesh = em_gfx_mesh_create(gfx, next.model.verts, next.model.vert_count,
        next.model.indices, next.model.index_count, (const EmGfxTexDesc *)next.model.texs,
        next.model.tex_count, next.model.texels, next.model.flags);
    if (!next.mesh) goto fail;
    /* Resource preparation is not original face allocation. Stay detached
     * and blank until the caller reaches the original B81D0 event. */
    memset(&next.face.state, 0, sizeof next.face.state);
    next.ready = 1;
    *host = next;
    return 1;
fail:
    em_player_face_host_free(&next);
    return 0;
}

static int update_mesh(EmPlayerFaceHost *host)
{
    for (uint32_t i = 0; i < host->face.count; ++i) {
        size_t vertex = host->face.first + (size_t)i;
        em_opening_face_position(host->face.positions + vertex * 3,
            host->model.verts + vertex * EM_MODEL_VERT_WORDS,
            host->face.deltas + (size_t)i * 21, host->face.state.weight);
    }
    if (em_gfx_mesh_update_positions(host->gfx, host->mesh,
                                    host->face.positions, host->model.vert_count)) return 1;
    host->failed = 1;
    return 0;
}

int em_player_face_host_attach(EmPlayerFaceHost *host)
{
    if (!host || !host->ready || host->failed) return 0;
    if (host->attached) {
        em_opening_face_reset(&host->face.state);
        host->face.state.speed = 1;
    } else {
        /* Original allocator/free proof: fresh 208-byte pool blocks are
         * zero; AF890 clears them again. No captured weights are seeded. */
        em_opening_face_init(&host->face.state, 1);
    }
    host->attached = 1;
    return update_mesh(host);
}

void em_player_face_host_detach(EmPlayerFaceHost *host)
{
    if (!host) return;
    memset(&host->face.state, 0, sizeof host->face.state);
    host->attached = 0;
}

int em_player_face_host_talk(EmPlayerFaceHost *host, uint8_t talking)
{
    if (!host || !host->ready || !host->attached || host->failed) return 0;
    em_opening_face_talk(&host->face.state, talking);
    return 1;
}

int em_player_face_host_tick_before_body(EmPlayerFaceHost *host)
{
    if (!host || !host->ready || host->failed) return 0;
    if (!host->attached) return 1;
    em_opening_face_tick(&host->face.state, host->random, host->random_context);
    return update_mesh(host);
}

int em_player_face_host_record(const EmPlayerFaceHost *host, EmGfxMesh **mesh,
                              const EmModel **model)
{
    if (!host || !host->ready || host->failed || !mesh || !model) return -1;
    if (!host->attached) return 0;
    *mesh = host->mesh;
    *model = &host->model;
    return 1;
}

const EmOpeningFace *em_player_face_host_state(const EmPlayerFaceHost *host)
{
    return host && host->ready && host->attached && !host->failed ? &host->face.state : NULL;
}
