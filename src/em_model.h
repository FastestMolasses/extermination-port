/* em_model.h — EMDL model loading for the Extermination native port.
 *
 * EMDL is OUR OWN interchange format, produced locally by the decomp repo's
 * tools/export_native.py from the user's own disc dump. The files are
 * disc-derived and live under assets/ (git-ignored, never redistributed).
 *
 * Layout (little-endian) — see export_native.py for the producer:
 *   char  magic[4]   "EMD1"
 *   u32   bone_count
 *   u32   vert_count
 *   u32   index_count
 *   u32   frame_count
 *   f32   fps
 *   u32   reserved[2]
 *   i32   parents[bone_count]
 *   vert  { f32 px,py,pz; f32 nx,ny,nz; u32 bone } x vert_count
 *   u32   indices[index_count]
 *   f32   palette[frame_count][bone_count][16]   (column-major world mats)
 *
 * Vertices are bone-local (the PS2 stores per-bone object-space packets);
 * posing = palette[frame][bone] * position, exactly the engine's skinning
 * model (a matrix palette uploaded to VU1 + per-bone vertex packets).
 */
#ifndef EM_MODEL_H
#define EM_MODEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t  bone_count;
    uint32_t  vert_count;
    uint32_t  index_count;
    uint32_t  frame_count;
    float     fps;
    int32_t  *parents;   /* bone_count */
    float    *verts;     /* vert_count * 7 floats (pos3, nrm3, bone-as-u32) */
    uint32_t *indices;   /* index_count */
    float    *palette;   /* frame_count * bone_count * 16 floats */
} EmModel;

/* Load an EMDL file. Returns 0 on success, nonzero on error (and prints the
 * reason to stderr). em_model_free releases all arrays. */
int  em_model_load(EmModel *m, const char *path);
void em_model_free(EmModel *m);

/* Write the bone palette for a (possibly fractional) frame time into
 * out[bone_count*16], linearly blending the two surrounding baked frames
 * (the engine itself interpolates between animation samples each tick).
 * time_frames wraps, so any monotonically growing value loops the clip. */
void em_model_palette_at(const EmModel *m, double time_frames, float *out);

#ifdef __cplusplus
}
#endif

#endif /* EM_MODEL_H */
