/* em_model.h — EMDL model loading for the Extermination native port.
 *
 * EMDL is OUR OWN interchange format, produced locally by the decomp repo's
 * tools/export_native.py from the user's own disc dump. The files are
 * disc-derived and live under assets/ (git-ignored, never redistributed).
 *
 * Layout (little-endian) — see export_native.py for the producer:
 *   char  magic[4]   "EMD3"  ("EMD2" = the same file without clip_count
 *                            and the clip table; still loadable — the
 *                            loader synthesizes one whole-range clip)
 *   u32   bone_count
 *   u32   vert_count
 *   u32   index_count
 *   u32   frame_count   (total baked frames across ALL clips)
 *   f32   fps
 *   u32   tex_count
 *   u32   flags        (bit 0: the "normal" slot carries a baked vertex
 *                       COLOR, not a normal — static level geometry ships
 *                       its lighting prebaked; see tools/export_level.py)
 *   u32   clip_count    (EMD3 only, >= 1)
 *   i32   parents[bone_count]
 *   tex   { u32 width, height, byte_offset, reserved } x tex_count
 *   clip  { u32 id, first_frame, frame_count; f32 fps } x clip_count
 *         (EMD3 only; id = source container index in the disc's id 0x74
 *         animation library, frames index the shared palette blob)
 *   vert  { f32 px,py,pz; f32 nx,ny,nz; f32 u,v; u32 bone; u32 tex }
 *         x vert_count       (tex 0xFFFFFFFF = untextured)
 *   u32   indices[index_count]
 *   f32   palette[frame_count][bone_count][16]   (column-major world mats)
 *   u8    texels[]   RGBA8 rows top-down, per texture at byte_offset
 *
 * Vertices are bone-local (the PS2 stores per-bone object-space packets);
 * posing = palette[frame][bone] * position, exactly the engine's skinning
 * model (a matrix palette uploaded to VU1 + per-bone vertex packets).
 * UVs are normalized with REPEAT addressing (the PS2 data tiles: values
 * run past 1). The texel blob is the runtime-built PS2 texture content
 * (PSMT4/PSMT8 indices + CLUTs resolved by the exporter from a GS dump).
 */
#ifndef EM_MODEL_H
#define EM_MODEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One embedded texture: RGBA8 rows top-down at `offset` into texels[]. */
typedef struct {
    uint32_t width, height, offset, reserved;
} EmModelTex;

/* One named clip: a frame range into the shared palette blob. `id` is the
 * source container index in the disc's id 0x74 animation library (player:
 * 346 = idle/look-around, 2 = walk, 3 = run — identified by stride scan,
 * see export_native.py). Locomotion clips are baked IN PLACE (the
 * exporter strips the root's XZ travel); the game re-applies movement. */
typedef struct {
    uint32_t id, first_frame, frame_count;
    float    fps;
} EmModelClip;

#define EM_MODEL_VERT_WORDS 10u  /* pos3, nrm3, uv2, bone, tex */
#define EM_MODEL_NO_TEX     0xFFFFFFFFu
#define EM_MODEL_FLAG_VCOLOR 1u  /* nrm slot = baked vertex color */

typedef struct {
    uint32_t    bone_count;
    uint32_t    vert_count;
    uint32_t    index_count;
    uint32_t    frame_count;
    float       fps;
    uint32_t    tex_count;
    uint32_t    flags;
    uint32_t    clip_count;
    int32_t    *parents;   /* bone_count */
    EmModelTex *texs;      /* tex_count */
    EmModelClip *clips;    /* clip_count (>= 1; EMD2 gets one synthetic
                              clip spanning every frame, id 0) */
    float      *verts;     /* vert_count * EM_MODEL_VERT_WORDS 32-bit words */
    uint32_t   *indices;   /* index_count */
    float      *palette;   /* frame_count * bone_count * 16 floats */
    uint8_t    *texels;    /* RGBA8 blob, texel_bytes long */
    uint32_t    texel_bytes;
} EmModel;

/* Load an EMDL file. Returns 0 on success, nonzero on error (and prints the
 * reason to stderr). em_model_free releases all arrays. */
int  em_model_load(EmModel *m, const char *path);
void em_model_free(EmModel *m);

/* Write the bone palette of clip index `clip` for a (possibly fractional)
 * frame time into out[bone_count*16], linearly blending the two
 * surrounding baked frames (the engine itself interpolates between
 * animation samples each tick). time_frames is CLIP-RELATIVE and wraps,
 * so any monotonically growing value loops the clip; the last frame
 * blends back into the first. An out-of-range clip index uses clip 0. */
void em_model_palette_at(const EmModel *m, uint32_t clip,
                         double time_frames, float *out);

/* Clip index for a library clip id; -1 if this model doesn't carry it. */
int em_model_clip_index(const EmModel *m, uint32_t id);

#ifdef __cplusplus
}
#endif

#endif /* EM_MODEL_H */
