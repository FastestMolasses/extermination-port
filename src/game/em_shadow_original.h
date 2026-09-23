/* Player drop shadow: original 001DA6A0 and the routines it calls
 * (001D98A0, 001DA080, 001DA290/001DA1E0, 001DA310, 001D9EE0, 001D5C80).
 * Docs: docs/SHADOW_ORIGINAL.md.
 *
 * Mechanism (every step executed as original instructions over the
 * first-control capture by tools/test_shadow_original_reference.py, whose
 * rebuilt DMA chain is byte-identical to the captured one):
 *   1. 0015C160 (player post-step) calls 001DA6A0(player) when
 *      D_008102B1 != 0, D_00810771 != 1 and player+0x214 == 0.
 *   2. Gate: kind = actor+0x96 (0 returns); the anchor is a node position;
 *      three probes (anchor, anchor-40y, anchor-0y or -5y) are clip-tested
 *      against ctx+0x2240 and a plane shared by all three rejects.
 *   3. 001D98A0 builds a top-down light view 6 units above the anchor
 *      (D_00817F20) and the world -> shadow-texture matrix ctx+0x24B0
 *      (u, v in [0,1] across +-8 world units, the w lane an alpha ramp
 *      71 - 2*depth in the float-mantissa integer encoding).
 *   4. 001DA290 clears the frame's destination alpha to 0 (colour kept).
 *   5. 001DA310 x2 draws the 16 x 40 x 16 box below the anchor into the
 *      destination alpha only: outward cube (model 0x14) alpha 128, then
 *      inward cube (model 0x15) alpha 1, depth test GEQUAL, no depth write.
 *      Alpha bit 7 then marks visible surfaces inside the box.
 *   6. 001D9EE0 renders the kind's shadow proxy mesh (D_0028A490[kind],
 *      skinned by the actor's own node palette) orthographically, 8 px per
 *      world unit, into a 128x128 PSMCT32 target at GS byte 0x258000 that
 *      its setup packet clears to (128,128,128,0); every drawn pixel is the
 *      flat RGBAQ (128,255,255,255).
 *   7. 001D5C80 redraws the level objects of the grid cells around the
 *      highest node with the 0023C200 kernel: per vertex RGBA (0,0,0,a),
 *      ST from ctx+0x24B0, texture = that target (MODULATE, RGBA, bilinear,
 *      clamp), alpha test A > 0, destination-alpha test (bit 7 set), depth
 *      GEQUAL without write, blend (Cs - Cd) * As / 128 + Cd, fogged.
 *
 * This module is the EE side: every matrix, colour, clip decision and the
 * receiver object list, bit-exact on the EE/VU0 arithmetic (binary32
 * results truncated toward zero; EE division rounded, as the point-light
 * and fog oracles established). The GS side is described by the constants
 * below and is the draw workers' job. It reads only the original record
 * bytes named by offset. A reached NULL worker or view, a negative worker
 * result, an out-of-range node or grid read, the receiver bound, or the
 * untranslated 0015BF90 route (em_shadow_original_route_0015C160) latches a
 * fault; the call returns -1 and every later call returns -1.
 *
 * The receiver data asset (em_shadow_receivers_*, below) is read with
 * stdio; the routine itself has no dependency on any port subsystem. */
#ifndef EM_SHADOW_ORIGINAL_H
#define EM_SHADOW_ORIGINAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SHADOW_ORIGINAL_001DA6A0 0x001DA6A0u
#define EM_SHADOW_ORIGINAL_ACTOR_BYTES 0x240u  /* reads up to +0x23C */
#define EM_SHADOW_ORIGINAL_NODE_BYTES 0xD0u    /* reads +0xC0..+0xCF */
#define EM_SHADOW_ORIGINAL_KIND_PLAYER 0x28    /* player+0x96 in every capture */

/* GS-side constants (packet bytes cited in docs/SHADOW_ORIGINAL.md). */
#define EM_SHADOW_TARGET_GS_BYTE 0x258000u     /* FRAME_1 FBP 0x12C, TEX0 TBP 0x2580 */
#define EM_SHADOW_TARGET_SIZE 128              /* FBW 2, SCISSOR 0..127, TW=TH=7 */
#define EM_SHADOW_TARGET_OFFSET 1984.0f        /* XYOFFSET 0x7C00 / 16 */
#define EM_SHADOW_TARGET_CLEAR_RGBA 0x00808080u /* D_00817E20 sprite RGBAQ */
#define EM_SHADOW_SILHOUETTE_RGBA 0xFFFFFF80u  /* 001D7080(0, -0x80, 0.0) */
#define EM_SHADOW_BOX_MODEL_FRONT 0x14         /* chunk27 library, outward cube */
#define EM_SHADOW_BOX_MODEL_BACK 0x15          /* chunk27 library, inward cube */
#define EM_SHADOW_RECEIVER_MAX 512             /* native bound; exceeding faults */

enum {
    EM_SHADOW_FAULT_NONE = 0,
    EM_SHADOW_FAULT_NULL_WORKER = 1,   /* reached worker or data view is NULL */
    EM_SHADOW_FAULT_WORKER_FAILED = 2, /* worker returned a negative value */
    EM_SHADOW_FAULT_BAD_INPUT = 3,     /* node/grid read outside the views */
    EM_SHADOW_FAULT_OVERFLOW = 4,      /* receiver list over the native bound */
    EM_SHADOW_FAULT_UNTRANSLATED = 5   /* 0015BF90 route (player+0x214 != 0);
                                          raised by em_shadow_original_route_0015C160 */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_SHADOW_FAULT_* */
} EmShadowOriginalFault;

/* 0015C160's routing of the post-step, checked against 0015C160 executed
 * as original instructions with the three gate bytes patched:
 *   d8102B1 == 0             -> 0: nothing (no 001CB590, no +0x4C draw).
 *   d810771 == 1             -> 0: no shadow call; the +0x4C draw follows.
 *   player_214 == 0          -> 1: call em_shadow_original_001DA6A0 with the
 *                               player (0015C160 passes D_00275B44, which the
 *                               001CB590(player) just before it set to the
 *                               player), then the +0x4C draw.
 *   player_214 != 0          -> 0015BF90 (a floor-raycast variant, never
 *                               taken in any capture) is not translated: it
 *                               latches EM_SHADOW_FAULT_UNTRANSLATED at
 *                               0x0015BF90 and returns -1.
 * A fault already latched returns -1. */
int em_shadow_original_route_0015C160(uint8_t d8102B1, uint8_t d810771,
                                      uint32_t player_214, EmShadowOriginalFault *fault);

/* Render-context and global views the routine reads, in original bytes'
 * float order (row-vector convention, rows contiguous). */
typedef struct {
    float clip_2240[16];   /* ctx+0x2240 = 001CD370(0) */
    float proj_2340[16];   /* ctx+0x2340 P */
    float view_2380[16];   /* ctx+0x2380 V */
    float camera_3AC0[16]; /* D_70003AC0 at the call (== ctx+0x23C0 in
                              every capture with a scratchpad dump) */
    float view_810610[16]; /* D_00810610 */
    float zoom_2468;       /* ctx+0x2468 */
    uint8_t area_700;      /* D_00810700 */
    uint8_t sub_701;       /* D_00810701 */
    const int32_t *grid_140; /* ctx+0x140 level cell grid, 4 ids per cell */
    uint32_t grid_words;     /* readable int32 count behind grid_140 */
    int32_t stride_148;      /* ctx+0x148 */
    float cell_x_150, cell_z_154, origin_x_158, origin_z_15C;
} EmShadowOriginalScene;

/* D_00817FF0: the light direction of the previous call. It only feeds
 * D_00817FC0 (and so 001DA080's second pick) before being replaced. Zero at
 * boot (BSS). */
typedef struct {
    float d817FF0[4];
} EmShadowOriginalState;

/* One 001DA310 box pass (model 0x14 then 0x15). */
typedef struct {
    int32_t model;          /* 001C6120(*D_0028A56C, model) */
    float world[16];        /* sp+0x50: scale(0.1*size, 4, 0.1*size), +(0,-20,0),
                               rotation(0,0,0), +anchor */
    float camera[16];       /* dmem 0..3: world x D_70003AC0 */
    float normal[16];       /* dmem 4..7: world x 0 (D_70003400 zeroed) */
    float color_row[4];     /* D_70003470 = rgba + 8388608 (dmem 0x3F8) */
    uint32_t rgbaq;         /* 001D7080 A+D RGBAQ low word, A<<24|B<<16|G<<8|R */
    float clip_pass[16];    /* D_70003AC0 for the 001D4C20 pass: (world x V) x P */
} EmShadowOriginalBox;

typedef struct {
    int32_t id;          /* grid id (> 0) */
    uint8_t cls;         /* 0: inside 1024x448 frustum, 1: inside 4096 guard
                            band, 2: outside it (001D4FB0 then 001D4B50) */
    uint32_t clip_lo;    /* corners 0..3 CLIP bits against the screen matrix */
    uint32_t clip_hi;    /* corners 4..7 */
} EmShadowOriginalReceiver;

typedef struct {
    int32_t drawn;            /* 0: early return (kind 0 / clip reject) */
    int32_t kind;             /* actor+0x96 */
    float anchor[4];          /* sp+0x40, w = 1 */
    float probe[3][4];
    uint32_t probe_clip[3];   /* vclipw.xyz flags per probe */
    int32_t variant;          /* 1 when kind 0x28 and actor+0x23C != 0 */
    float spread;             /* the shipped min/min spread (always 0) */
    float box_size;           /* f20: 16 unless spread > 7 */
    float unit;               /* f21: 128 / 16 = 8 */
    /* 001D98A0 */
    float light_817F70[4], right_817F80[4], up_817F90[4];
    float cross_817FC0[4];    /* up x (previous D_00817FF0 x up) */
    float eye_817F60[4];      /* -(anchor - 6 * light) */
    float view_817F20[16];    /* D_00817F20 */
    float uv_24B0[16];        /* ctx+0x24B0 */
    int32_t alpha_matrix;     /* 0: none, 1: D_0026E550 (71), 2: D_0026E590 (101),
                                 3: D_0026E5D0 (51, variant) */
    /* 001DA080 */
    int32_t near_index, far_index;
    float near_817FB0[4], far_817FA0[4];
    /* 001DA310 x2 */
    EmShadowOriginalBox box[2];
    /* 001D9EE0 */
    float silhouette_vp[16];  /* D_70003AC0 during the proxy draw */
    /* 001D5C80 */
    int32_t cell_cx, cell_cz, cell_x0, cell_x1, cell_z0, cell_z1;
    float screen_3400[16], guard_3440[16];
    uint32_t receiver_count;
    EmShadowOriginalReceiver receiver[EM_SHADOW_RECEIVER_MAX];
} EmShadowOriginalPlan;

/* Workers, called in the original order. All return >= 0 on success. */
typedef struct {
    void *ctx;
    /* 001C6120(D_0028A5A0, id): the level object's AABB, obj+0x14..+0x1C
     * (min x,y,z) and obj+0x24..+0x2C (max). */
    int (*w_object_bounds)(void *ctx, int32_t id, float bmin[3], float bmax[3]);
    /* 001DA290: full-frame sprite (1792..2304, 1936..2160, Z 0xFFFFFF) that
     * writes destination alpha 0 and keeps colour (state block (2,9)). */
    int (*w_alpha_clear)(void *ctx);
    /* 001DA310: one box pass into destination alpha (see the header). */
    int (*w_box)(void *ctx, const EmShadowOriginalBox *box);
    /* 001D9EE0: clear the 128x128 target, draw the kind's proxy mesh
     * D_0028A490[kind] skinned by the actor's world-space node matrices
     * (node+0x90 of each actor+0x110 node, the same bone matrices the
     * actor's own draw uses) times `vp` (001C7420 uploads node+0x90 x vp;
     * the oracle checks every bone). 001D9EE0 saves the actor qword +0x80,
     * zeroes +0x80/+0x84/+0x88 (+0x8C kept) for the 001C7420 call and
     * restores it afterwards (0x1D9FFC..0x1DA024), so any per-actor offset
     * held there is NOT applied to the silhouette. Screen = clip + 2048,
     * target pixel = screen - 1984; flat EM_SHADOW_SILHOUETTE_RGBA, no
     * blend. */
    int (*w_silhouette)(void *ctx, int32_t kind, const float vp[16]);
    /* 001D4CD0: bind the receiver pass (kernel 0023C200, `uv` to dmem 8,
     * the target as MODULATE/RGBA texture, state block (2,6)). */
    int (*w_receiver_begin)(void *ctx, const float uv[16]);
    /* 001D4FB0 (+ 001D1F80(0,2,6), 001D4B50, 001D4CD0 when cls == 2). */
    int (*w_receiver)(void *ctx, const EmShadowOriginalReceiver *object);
    /* 001D1FF0(0, 1): restore the level clamp block. */
    int (*w_receiver_end)(void *ctx);
} EmShadowOriginalWorkers;

/* 001DA6A0(actor) after 001CB590(actor) published actor+0x110 as the node
 * table. `actor` = the actor record bytes (>= EM_SHADOW_ORIGINAL_ACTOR_BYTES);
 * nodes[i] = the record *(actor+0x110+4i) points to (>= NODE_BYTES each,
 * NULL when not provided). `plan` receives every computed value (it is
 * written even on an early return). Returns 1 when drawn, 0 on the
 * original early return, -1 on a fault. */
int em_shadow_original_001DA6A0(const uint8_t *actor, const uint8_t *const *nodes,
                                uint32_t node_slots, const EmShadowOriginalScene *scene,
                                EmShadowOriginalState *state, EmShadowOriginalPlan *plan,
                                const EmShadowOriginalWorkers *w,
                                EmShadowOriginalFault *fault);

/* The receiver kernel's per-vertex slice (0023C338..0023C350, 0023C370,
 * 0023C398): out = uv x (p, 1) with VU truncation, alpha = the w lane
 * clamped to [8388608, 8388863] minus 8388608. The kernel's ST = out.xy * Q
 * with Q = 1 / clip w, and out.z is 1 for every original uv matrix. */
void em_shadow_original_receiver_vertex(const float uv[16], const float p[3],
                                        float out[4], uint32_t *alpha);

/* 0023C200's RGBAQ word for a receiver vertex: R = G = B = 0. */
static inline uint32_t em_shadow_original_receiver_rgba(uint32_t alpha)
{
    return alpha << 24;
}

/* --- the receivers' original data (asset) -------------------------------
 * `assets/scene_snow/shadow_receivers.emsr`, written from the user's disc
 * by ../Extermination/tools/export_shadow_receivers.py (checked there
 * against captured RAM): the level cell grid 001D52E0 publishes (object 0
 * of the bank *D_0028A5A0: ctx+0x144 rows, +0x148 stride, +0x150..+0x164,
 * ctx+0x140 = the ids), every bank object's AABB (obj+0x14 / +0x24) and the
 * 32-vertex batches its +0x40 VIF data unpacks, and the chunk27 library box
 * models 0x14 / 0x15 in the same form. */
typedef struct {
    int32_t id;
    uint32_t batches;         /* 32 vertices each */
    float bmin[3], bmax[3];   /* obj+0x14..+0x1C, obj+0x24..+0x2C */
    const float *qw3;         /* batches x 32 x 4: qword 3 of each vertex
                                 (x, y, z, data word), EmGfxShadowStrips.qw3 */
    const uint32_t *qwords;   /* batches x 128 x 4 words: the four qwords of
                                 each vertex as UNPACK writes them */
} EmShadowReceiverObject;

typedef struct {
    int32_t rows_144, stride_148;
    float f150[6];            /* ctx+0x150..+0x164 */
    const int32_t *grid;      /* ctx+0x140: rows x stride cells x 4 ids */
    uint32_t grid_words;
    uint32_t slots;           /* bank word 0: object ids 1..slots-1 */
    EmShadowReceiverObject *object;   /* [slots], by id (object[0] unused) */
    EmShadowReceiverObject box[2];    /* library models 0x14, 0x15 */
    void *blob;
} EmShadowReceivers;

/* Load / free the asset. Returns 0, or -1 (missing file, wrong magic or
 * version, a size or count that does not match the file) with *r zeroed. */
int em_shadow_receivers_load(EmShadowReceivers *r, const char *path);
void em_shadow_receivers_free(EmShadowReceivers *r);

/* The grid fields of an EmShadowOriginalScene (grid_140, grid_words,
 * stride_148, cell_x_150 .. origin_z_15C), as 001D52E0 publishes them. */
void em_shadow_receivers_scene(const EmShadowReceivers *r, EmShadowOriginalScene *scene);

/* 001C6120(*D_0028A5A0, id) (id & 0xFFFF & ~0x8000): the object, or NULL
 * when the id has no record. */
const EmShadowReceiverObject *em_shadow_receivers_object(const EmShadowReceivers *r,
                                                         int32_t id);

/* w_object_bounds over the asset (ctx = the EmShadowReceivers): 0, or -1
 * for an id without a record. */
int em_shadow_receivers_bounds(void *ctx, int32_t id, float bmin[3], float bmax[3]);

/* The box model of a w_box call (model 0x14 or 0x15), or NULL. */
const EmShadowReceiverObject *em_shadow_receivers_box(const EmShadowReceivers *r,
                                                      int32_t model);

#ifdef __cplusplus
}
#endif

#endif /* EM_SHADOW_ORIGINAL_H */
