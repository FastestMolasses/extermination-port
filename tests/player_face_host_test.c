#include "game/em_player_face_host.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct EmGfx { int unused; };
struct EmGfxMesh { uint32_t vertices; };
static unsigned created, destroyed, updated, calls;
static int fail_create, fail_update;

EmGfxMesh *em_gfx_mesh_create(EmGfx *gfx, const float *v, uint32_t vertices,
    const uint32_t *indices, uint32_t count, const EmGfxTexDesc *textures,
    uint32_t texture_count, const uint8_t *pixels, uint32_t flags)
{
    assert(gfx && v && indices && count && textures && texture_count && pixels && !flags);
    for (uint32_t i = 0; i < count; ++i) assert(indices[i] < vertices);
    if (fail_create) return NULL;
    EmGfxMesh *mesh = malloc(sizeof *mesh); assert(mesh);
    mesh->vertices = vertices;
    ++created;
    return mesh;
}
void em_gfx_mesh_destroy(EmGfx *gfx, EmGfxMesh *mesh)
{ assert(gfx && mesh); free(mesh); ++destroyed; }
int em_gfx_mesh_update_positions(EmGfx *gfx, EmGfxMesh *mesh, const float *positions, uint32_t count)
{
    assert(gfx && mesh && positions && count == mesh->vertices);
    for (uint32_t i = 0; i < count * 3; ++i) assert(isfinite(positions[i]));
    ++updated;
    return !fail_update;
}
static uint32_t random_word(void *context)
{
    assert(context == &calls);
    /* Original 122BB8 returns 31 bits, even though its seed retains 32. */
    return (++calls * UINT32_C(0x94720123)) & UINT32_C(0x7FFFFFFF);
}
static uint64_t hash_block(uint64_t hash, const void *data, size_t count)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < count; ++i) hash = (hash ^ p[i]) * UINT64_C(1099511628211);
    return hash;
}
static uint64_t model_hash(const EmModel *m)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = hash_block(hash, m, sizeof *m);
    hash = hash_block(hash, m->parents, m->bone_count * sizeof *m->parents);
    hash = hash_block(hash, m->verts, (size_t)m->vert_count * 40);
    hash = hash_block(hash, m->indices, m->index_count * sizeof *m->indices);
    hash = hash_block(hash, m->texs, m->tex_count * sizeof *m->texs);
    hash = hash_block(hash, m->texels, m->texel_bytes);
    hash = hash_block(hash, m->clips, m->clip_count * sizeof *m->clips);
    return hash_block(hash, m->palette, (size_t)m->frame_count * m->bone_count * 64);
}
static void record(FILE *file, const EmPlayerFaceHost *host, unsigned frame)
{
    uint32_t header[3] = {frame, host->attached, calls};
    assert(fwrite(header, sizeof header, 1, file) == 1);
    assert(fwrite(&host->face.state, sizeof host->face.state, 1, file) == 1);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    EmModel body = {0};
    assert(!em_model_load(&body, "assets/player.emdl"));
    uint64_t original = model_hash(&body);
    EmGfx gfx = {0};
    EmPlayerFaceHost host = {0};
    for (unsigned i = 0; i < 7; ++i) {
        char path[1024];
        snprintf(path, sizeof path, "%s/bad_%u", argv[1], i);
        assert(!em_player_face_host_load(&host, &gfx, &body, path, random_word, &calls));
        assert(!host.ready && !host.mesh && !host.model.verts && !calls);
        assert(model_hash(&body) == original);
    }
    uint32_t saved_index = body.indices[0];
    body.indices[0] = body.vert_count;
    uint64_t invalid_body = model_hash(&body);
    assert(!em_player_face_host_load(&host, &gfx, &body, "assets/scene_snow", random_word, &calls));
    assert(model_hash(&body) == invalid_body);
    body.indices[0] = saved_index;
    uint32_t head_vertex = UINT32_MAX, body_triangle = UINT32_MAX;
    for (uint32_t i = 0; i < body.vert_count; ++i) {
        uint32_t bone; memcpy(&bone, body.verts + (size_t)i * 10 + 8, 4);
        if (bone == 7) { head_vertex = i; break; }
    }
    for (uint32_t i = 0; i < body.index_count; i += 3) {
        int non_head = 1;
        for (unsigned j = 0; j < 3; ++j) {
            uint32_t bone; memcpy(&bone, body.verts + (size_t)body.indices[i+j] * 10 + 8, 4);
            if (bone == 7) non_head = 0;
        }
        if (non_head) { body_triangle = i; break; }
    }
    assert(head_vertex != UINT32_MAX && body_triangle != UINT32_MAX);
    saved_index = body.indices[body_triangle];
    body.indices[body_triangle] = head_vertex;
    invalid_body = model_hash(&body);
    assert(!em_player_face_host_load(&host, &gfx, &body, "assets/scene_snow", random_word, &calls));
    assert(model_hash(&body) == invalid_body);
    body.indices[body_triangle] = saved_index;
    fail_create = 1;
    assert(!em_player_face_host_load(&host, &gfx, &body, "assets/scene_snow", random_word, &calls));
    fail_create = 0;
    assert(!host.ready && !host.model.verts && model_hash(&body) == original);
    assert(em_player_face_host_load(&host, &gfx, &body, "assets/scene_snow", random_word, &calls));
    assert(!host.attached && !calls && !host.model.palette && !host.model.clips);
    assert(host.model.parents != body.parents && host.model.verts != body.verts);
    assert(host.model.indices != body.indices && host.model.texs != body.texs && host.model.texels != body.texels);
    assert(!memcmp(host.model.verts, body.verts, (size_t)body.vert_count * 40));
    assert(!memcmp(host.model.texs, body.texs, body.tex_count * sizeof *body.texs));
    assert(!memcmp(host.model.texels, body.texels, body.texel_bytes));
    for (uint32_t i = host.face.first; i < host.model.vert_count; ++i) {
        uint32_t bone;
        memcpy(&bone, host.model.verts + (size_t)i * 10 + 8, 4);
        assert(bone == (7 | EM_GFX_VERT_FACE_LIGHT));
    }
    EmGfxMesh *mesh = NULL; const EmModel *draw_model = NULL;
    assert(em_player_face_host_record(&host, &mesh, &draw_model) == 0);
    assert(em_player_face_host_tick_before_body(&host) && !calls && !updated);
    assert(!em_player_face_host_talk(&host, 1));
    assert(em_player_face_host_attach(&host));
    assert(em_player_face_host_record(&host, &mesh, &draw_model) == 1);
    assert(mesh == host.mesh && draw_model == &host.model);
    char output[1024]; snprintf(output, sizeof output, "%s/states.bin", argv[1]);
    FILE *file = fopen(output, "wb"); assert(file);
    record(file, &host, UINT32_MAX);
    for (unsigned frame = 0; frame < 400; ++frame) {
        if (frame == 5 || frame == 80 || frame == 150 || frame == 300)
            assert(em_player_face_host_talk(&host, 1));
        if (frame == 50 || frame == 125 || frame == 200 || frame == 350)
            assert(em_player_face_host_talk(&host, 0));
        if (frame == 128) {
            float weights[8];
            memcpy(weights, host.face.state.weight, sizeof weights);
            int blink = host.face.state.blink_wait, expression = host.face.state.expression_wait;
            assert(em_player_face_host_attach(&host));
            assert(!memcmp(weights, host.face.state.weight, sizeof weights));
            assert(host.face.state.blink_wait == blink && host.face.state.expression_wait == expression);
        }
        if (frame == 250) em_player_face_host_detach(&host);
        if (frame == 280) assert(em_player_face_host_attach(&host));
        assert(em_player_face_host_tick_before_body(&host));
        record(file, &host, frame);
    }
    fclose(file);
    assert(model_hash(&body) == original);
    em_model_free(&body); /* alternate materials and geometry have no borrowed storage */
    /* GPU update failure must remain visible to the required draw path. */
    fail_update = 1;
    assert(!em_player_face_host_tick_before_body(&host) && host.failed);
    assert(em_player_face_host_record(&host, &mesh, &draw_model) == -1);
    assert(!em_player_face_host_attach(&host));
    em_player_face_host_detach(&host);
    assert(em_player_face_host_record(&host, &mesh, &draw_model) == -1);
    em_player_face_host_free(&host);
    assert(created == destroyed && !host.ready && !host.mesh && !host.model.verts);
    printf("PASS Dennis face host: isolated geometry/materials, 400 callbacks, 7 malformed assets, %u RNG calls\n", calls);
    return 0;
}
