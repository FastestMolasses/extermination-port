/* em_model.c — EMDL loader. Plain C stdio, no dependencies. */
#include "em_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *read_block(FILE *f, size_t bytes, const char *what)
{
    void *p = malloc(bytes ? bytes : 1);
    if (!p || fread(p, 1, bytes, f) != bytes) {
        fprintf(stderr, "emdl: short read on %s (%zu bytes)\n", what, bytes);
        free(p);
        return NULL;
    }
    return p;
}

int em_model_load(EmModel *m, const char *path)
{
    memset(m, 0, sizeof(*m));
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "emdl: cannot open %s\n", path);
        return 1;
    }

    struct {
        char     magic[4];
        uint32_t bone_count, vert_count, index_count, frame_count;
        float    fps;
        uint32_t tex_count, reserved;
    } hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 ||
        memcmp(hdr.magic, "EMD2", 4) != 0) {
        fprintf(stderr, "emdl: %s is not an EMD2 file (re-export with "
                        "tools/export_native.py)\n", path);
        fclose(f);
        return 1;
    }
    /* sanity bounds so a corrupt file can't drive huge allocations */
    if (hdr.bone_count > 1024 || hdr.vert_count > (1u << 22) ||
        hdr.index_count > (1u << 24) || hdr.frame_count > (1u << 16) ||
        hdr.tex_count > 4096) {
        fprintf(stderr, "emdl: %s header out of bounds\n", path);
        fclose(f);
        return 1;
    }

    m->bone_count  = hdr.bone_count;
    m->vert_count  = hdr.vert_count;
    m->index_count = hdr.index_count;
    m->frame_count = hdr.frame_count;
    m->fps         = hdr.fps;
    m->tex_count   = hdr.tex_count;

    m->parents = read_block(f, (size_t)hdr.bone_count * 4, "parents");
    m->texs    = read_block(f, (size_t)hdr.tex_count * 16, "textures");
    m->verts   = read_block(f, (size_t)hdr.vert_count *
                            EM_MODEL_VERT_WORDS * 4, "vertices");
    m->indices = read_block(f, (size_t)hdr.index_count * 4, "indices");
    m->palette = read_block(f, (size_t)hdr.frame_count * hdr.bone_count * 64,
                            "palette");

    /* texel blob runs to EOF */
    long blob_at = ftell(f);
    if (blob_at >= 0 && fseek(f, 0, SEEK_END) == 0) {
        long end = ftell(f);
        if (end > blob_at && fseek(f, blob_at, SEEK_SET) == 0) {
            m->texel_bytes = (uint32_t)(end - blob_at);
            m->texels = read_block(f, m->texel_bytes, "texels");
        }
    }
    fclose(f);

    if (!m->parents || !m->texs || !m->verts || !m->indices || !m->palette ||
        (m->tex_count && !m->texels)) {
        em_model_free(m);
        return 1;
    }
    /* each texture must fit the blob */
    for (uint32_t i = 0; i < m->tex_count; i++) {
        const EmModelTex *t = &m->texs[i];
        if (!t->width || !t->height ||
            (uint64_t)t->offset + (uint64_t)t->width * t->height * 4 >
                m->texel_bytes) {
            fprintf(stderr, "emdl: %s texture %u out of bounds\n", path, i);
            em_model_free(m);
            return 1;
        }
    }
    return 0;
}

void em_model_free(EmModel *m)
{
    free(m->parents);
    free(m->texs);
    free(m->verts);
    free(m->indices);
    free(m->palette);
    free(m->texels);
    memset(m, 0, sizeof(*m));
}

void em_model_palette_at(const EmModel *m, double time_frames, float *out)
{
    uint32_t n = m->bone_count * 16;
    if (m->frame_count <= 1) {
        memcpy(out, m->palette, n * sizeof(float));
        return;
    }
    double wrapped = fmod(time_frames, (double)m->frame_count);
    if (wrapped < 0) wrapped += m->frame_count;
    uint32_t f0 = (uint32_t)wrapped;
    uint32_t f1 = (f0 + 1) % m->frame_count;
    float t = (float)(wrapped - f0);

    const float *a = m->palette + (size_t)f0 * n;
    const float *b = m->palette + (size_t)f1 * n;
    for (uint32_t i = 0; i < n; i++)
        out[i] = a[i] + (b[i] - a[i]) * t;
}
