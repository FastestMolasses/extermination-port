/* em_gfx_metal.m — macOS and iOS graphics backend on Metal.
 *
 * iOS (TARGET_OS_IPHONE, docs/IOS.md) differs only where the window does:
 * the native handle is the CAMetalLayer itself, whose settings are applied
 * on the main thread (UIKit's layer; the frame loop runs on the game
 * thread), and the drawable size comes from em_window_drawable_size (the
 * UIKit layer sizes the drawable). Every resource already uses Shared or
 * Private storage, which both platforms support.
 *
 * Clean-room, no third-party libraries; only Apple system frameworks
 * (Metal, QuartzCore). Manual reference counting (no ARC). Per-frame
 * autoreleased objects (the drawable, command buffer, render-pass
 * descriptor) are bounded by an NSAutoreleasePool spanning begin/end_frame.
 *
 * Currently clear-and-present only — no custom shaders, so the offline Metal
 * shader toolchain is not required. When the translated PS2 draw pipeline
 * needs shaders they will be compiled at runtime from source via
 * -newLibraryWithSource:options:error: (no offline .metallib step).
 */
#include <TargetConditionals.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#if !TARGET_OS_IPHONE
#import <AppKit/AppKit.h>
#endif
#include "em_gfx.h"
#include "em_platform.h"
#include "em_math.h"
#include "game/em_lighting.h"
#include "game/em_object_unit.h"
#include "gfx/metal/em_fog_gs.h"
#include "gfx/metal/em_background_gs.h"
#include "gfx/metal/em_shadow_gs.h"
#include "gs/em_gs_world.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define EM_GFX_GS_SURFACE_MAX 8u

struct EmGfx {
#if TARGET_OS_IPHONE
    EmWindow                    *win;      /* the drawable size (UIKit layer) */
#else
    NSView                      *view;     /* layer-backed, layer is CAMetalLayer */
#endif
    CAMetalLayer                *layer;
    id<MTLDevice>                device;
    id<MTLCommandQueue>          queue;
    id<MTLRenderPipelineState>   testPipeline; /* lazily built for the test draw */
    id<MTLRenderPipelineState>   skinPipeline; /* lazily built for skinned draws */
    /* Opaque skinned draws: GS class 0, PRIM ABE 0 — blending OFF
     * (docs/LEVEL_MATERIALS.md). skinPipeline keeps standard alpha
     * blending for the port's translucent tint path (rgba[3] < 1). */
    id<MTLRenderPipelineState>   skinOpaquePipeline;
    id<MTLRenderPipelineState>   glowPipeline; /* additive (ONE/ONE) glow pass */
    id<MTLSamplerState>          repeatSampler; /* linear, REPEAT (PS2 tiling) */
    id<MTLDepthStencilState>     depthOn;      /* less-equal, write */
    id<MTLDepthStencilState>     depthOff;     /* always, no write */
    id<MTLDepthStencilState>     depthGlow;    /* less-equal, NO write (ZMSK=1) */
    id<MTLTexture>               depthTex;     /* sized to the drawable */
    /* per-frame */
    NSAutoreleasePool           *pool;
    id<CAMetalDrawable>          drawable;
    /* Render target of the frame: the drawable's texture, or in headless
     * runs (em_headless) an offscreen texture of the same size, so tests
     * and captures never need a visible window. */
    id<MTLTexture>               target;
    id<MTLTexture>               offscreen;
    bool                         headless;
    /* em_gfx_overlay_glyph_nearest: font quads queued now sample nearest */
    bool                         glyphNearest;
    id<MTLCommandBuffer>         cmd;
    id<MTLRenderCommandEncoder>  enc;       /* open from begin_frame to end_frame */
    /* headless capture (see em_gfx_request_capture) */
    char                         capturePath[1024];
    bool                         captureRequested;
    /* 2D overlay pass (em_gfx_overlay_rect / _arc): primitives queued
     * during the frame as ready-to-draw NDC vertices (float4 pos +
     * float4 color each — the kTestShaderSrc layout), flushed by
     * end_frame after all 3D draws. Budget = EM_GFX_OVERLAY_MAX quads
     * (6 verts each); rects take one quad, arcs one per segment.
     * overlayW/H is the SELECTED virtual canvas (em_gfx_overlay_canvas)
     * used for the queue-time NDC mapping; reset each begin_frame. */
    float                        overlayVerts[EM_GFX_OVERLAY_MAX * 6 * 8];
    uint32_t                     overlayVertCount;
    float                        overlayW, overlayH;
    /* Final screen effects: one ordered queue of reverse-subtract
     * (GS 0xA1) and additive (GS 0x68) rects. Separate PSOs preserve
     * destination alpha; mixed calls keep their order at the clamps. */
    float                        subVerts[EM_GFX_OVERLAY_SUB_MAX * 6 * 8];
    uint32_t                     subVertCount;
    bool                         subAdd[EM_GFX_OVERLAY_SUB_MAX];
    bool                         subBeforeText[EM_GFX_OVERLAY_SUB_MAX];
    id<MTLRenderPipelineState>   subPipeline;
    id<MTLRenderPipelineState>   addOverlayPipeline;
    /* Textured overlay: TWO texture slots (EM_GFX_OVERLAY_TEX_FONT /
     * _UI), each with its own quad queue (float4 NDC pos + float4 color
     * + float4 uv, uv.xy normalized at queue time) and its own
     * budget (EM_GFX_OVERLAY_MAX for glyphs, EM_GFX_DECOR_MAX for UI).
     * Flush order after the untextured
     * primitives: UI decor sprites first, font glyphs last — decor sits
     * over the scene dim and the diamond arcs, text over everything. */
    id<MTLTexture>               overlayTex[2];
    float                        overlayTexW[2], overlayTexH[2];
    id<MTLRenderPipelineState>   glyphPipeline;  /* textured overlay PSO  */
    id<MTLRenderPipelineState>   spriteAddPipeline, spriteSubPipeline, spriteOpaquePipeline;
    id<MTLSamplerState>          clampSampler;   /* linear, clamp-to-edge */
    float                        glyphVerts[EM_GFX_OVERLAY_MAX * 6 * 12];
    uint32_t                     glyphVertCount;
    float                        spriteVerts[EM_GFX_DECOR_MAX * 6 * 12];
    uint32_t                     spriteVertCount;
    uint8_t                      spriteBlend[EM_GFX_DECOR_MAX];
    /* Backdrop layer (em_gfx_overlay_backdrop / _backdrop_fill): the
     * animated UI background — UI-slot quads + an optional full-frame
     * solid fill, flushed FIRST in the overlay sequence (bottom layer,
     * under the untextured primitives). */
    float                        backdropVerts[EM_GFX_BACKDROP_MAX * 6 * 12];
    uint32_t                     backdropVertCount;
    bool                         backdropFillOn;
    float                        backdropFill[4];
    /* Per-frame forward spot (em_gfx_spot_light — em_gfx.h). Four
     * float4 rows bound as fragment buffer 2 of every skinned draw:
     *   [0] = pos.xyz, w = enable (0 = off, the begin_frame reset)
     *   [1] = dir.xyz (normalized), w = range
     *   [2] = (cos_inner, cos_outer, 0, 0)
     *   [3] = rgb, w unused
     * All-zero rows are the OFF state: the shader's spot term is gated
     * on row-0 w > 0, keeping no-spot frames bit-identical. */
    float                        spot[16];
    /* Per-draw character light rig (em_gfx_char_rig — em_gfx.h). Seven
     * float4 rows used to prepare integer colors at authored vertices:
     *   [0..2] = dir_i.xyz (world), w unused
     *   [3..5] = col_i.rgb (0..128 scale; caller applies actor RGB), w unused
     *   [6]    = amb.rgb (0..128), w = enable (0 = off)
     * All-zero = OFF: a normal-carrying mesh is then NOT drawn (the
     * original always resolves a room rig, 001D7B30 falls back to entry 0;
     * the former rig-less 0.30+0.70*N.L stand-in was invented, H18). */
    float                        rig[28];
    float                        face_rig[28]; /* separate original face draw */
    /* Per-frame distance fog (em_gfx_fog — em_gfx.h, em_fog_gs.h). Two
     * float4 rows bound as vertex buffer 6 and fragment buffer 4 of every
     * skinned draw:
     *   [0] = GS FOGCOL as [0,1] colour (channel / 255), w = enable
     *   [1] = (A, B, 0, 0), the 0021B920 VU fog coefficients
     * All-zero is the OFF state (the begin_frame reset): the shader's
     * fog blend is gated on row-0 w > 0, so fog-less scenes (office,
     * drawbridge) run the EXACT pre-fog arithmetic and stay
     * byte-identical. */
    float                        fog[8];
    /* Level background (em_gfx_background_* — em_gfx.h,
     * em_background_gs.h): the parsed asset (bgAsset.rgba points into
     * bgFile), its texture, and the pipeline of the 31 strips. */
    uint8_t                     *bgFile;
    EmBackgroundGsAsset          bgAsset;
    id<MTLTexture>               bgTexture;
    id<MTLRenderPipelineState>   bgPipeline;
    /* Player drop shadow (em_gfx_shadow_* — em_gfx.h, em_shadow_gs.h).
     * shadowAlphaPipeline: alpha-only colour write (001DA290 / 001DA310
     * state (2,9)); shadowSilPipeline: the 128x128 target (001D9EE0);
     * shadowRecvPipeline: v_skin + the GS receiver pixel pipeline with
     * framebuffer fetch (001D5C80). One target per silhouette of the
     * frame, each rendered by its own command buffer committed at once,
     * so it executes before the frame's buffer that samples it. */
    id<MTLRenderPipelineState>   shadowAlphaPipeline;
    id<MTLRenderPipelineState>   shadowSilPipeline;
    id<MTLRenderPipelineState>   shadowRecvPipeline;
    id<MTLTexture>               shadowTarget[EM_GFX_SHADOW_TARGET_MAX];
    uint32_t                     shadowTargets;     /* used this frame */
    int                          shadowCurrent;     /* last silhouette, -1 */
    id<MTLTexture>               shadowLast;        /* for the read hook */
    bool                         shadowRecvOpen;
    float                        shadowUV[16], shadowCam[16], shadowVP[16];
    uint32_t                     shadowWarned;      /* reasons printed */
    /* The chain page's GS primitives (em_gfx_gs_prims): the textured and
     * the untextured GS pixel paths (framebuffer fetch, RGB write only) and
     * the reasons already printed. Textures share the object table. */
    id<MTLRenderPipelineState>   gsTexPipeline;
    id<MTLRenderPipelineState>   gsFlatPipeline;
    uint32_t                     gsWarned;
    /* The GS frame (em_gfx_gs_frame): GS-memory surfaces, the GS pixel-path
     * pipeline that draws into them, the pipeline that shows one in the game
     * rectangle, and the reasons already printed. */
    struct EmGfxGsSurface {
        uint32_t fbp, fbw, height;
        id<MTLTexture> tex;
    }                            gsSurf[EM_GFX_GS_SURFACE_MAX];
    uint32_t                     gsSurfCount;
    id<MTLRenderPipelineState>   gsfPipeline;
    id<MTLRenderPipelineState>   gsfShowPipeline;
    id<MTLTexture>               gsfNoTexture;  /* bound for untextured draws */
    uint32_t                     gsfWarned;
    /* Object units (em_gfx_object_unit / em_gfx_object_texture — em_gfx.h):
     * the TEX0 (CLD cleared) -> texture table, the pipeline of the GS
     * class-0 pixel path, the CPU kernels' result storage and the reasons
     * already printed. */
    struct EmGfxObjectTex {
        uint64_t tex0;
        uint32_t width, height;
        id<MTLTexture> tex;
    }                            objTex[EM_GFX_OBJECT_TEX_MAX];
    uint32_t                     objTexCount;
    id<MTLRenderPipelineState>   objPipeline;
    EmObjectUnitResult           objResult;
    uint32_t                     objWarned;
    /* em_gfx_gs_opaque: the triangles in the object path's form */
    EmObjectUnitTriangle        *opaqueTri;
    uint32_t                     opaqueCap;
    /* The Original profile's GS frame (em_gfx_gs_world_* — em_gfx.h,
     * src/gs/em_gs_world.h): the CPU GS model, the reader of the original
     * state blocks, whether this frame's world draws are recorded and
     * whether end_frame presents the model's field, the field's texture,
     * the primitive buffer the object units' triangles convert into, and
     * the presentation choice (the placeholder only). */
    EmGsWorld                   *gsw;
    bool                         gswOn, gswFrame, gswShow;
    EmGfxGsRead                  gswRead;
    void                        *gswReadCtx;
    /* The field textures (three, so one is never written while the GPU may
     * still read it), the command buffer that read each last, and the slot
     * of this frame. A frame that shows a field is ended but not committed:
     * the model draws its field while the next frame is built (as the GS
     * draws the kicked list while the EE builds the next), and the frame is
     * committed, its field uploaded first, when the next kick or the next
     * frame without a field ends it (gsw_complete). */
    id<MTLTexture>               gswSlotTex[3];
    id<MTLCommandBuffer>         gswSlotCmd[3];
    int                          gswSlot, gswSlotNext;
    bool                         gswPending;
    id<MTLCommandBuffer>         gswPendCmd;
    id<CAMetalDrawable>          gswPendDrawable;
    int                          gswPendSlot;
    id<MTLBuffer>                gswPendShot;
    NSUInteger                   gswPendShotW, gswPendShotH, gswPendShotStride;
    char                         gswPendPath[1024];
    EmGfxGsPrim                 *gswPrims;
    uint32_t                     gswPrimCap;
    int                          gswPresentation;
    char                         gswWhy[192];
};

struct EmGfxMesh {
    id<MTLBuffer>  vbuf;
    id<MTLBuffer>  ibuf;       /* indices reordered: [opaque..., glow...] */
    uint32_t       index_count;
    uint32_t       opaque_count; /* leading non-glow indices */
    uint32_t       glow_count;   /* trailing EM_GFX_VERT_BILLBOARD indices */
    /* Embedded PS2 textures as a 2D array: every texture is TILED to fill
     * a common pow-2 slice, so REPEAT addressing reproduces the GS wrap
     * for any sub-size (all sizes are powers of two). scaleBuf holds one
     * float2 per texture: uv * scale maps native UVs into the slice. */
    id<MTLTexture> texArray;
    id<MTLBuffer>  scaleBuf;
    uint32_t       tex_count;
    uint32_t       flags;      /* EM_GFX_MESH_* */
    /* One GS draw-state code per texture slice (em_gfx.h EM_GFX_MESH_GSMAT
     * layout): the exported code for flagged meshes, the class-0 default
     * (kGsClass0Code) otherwise. The fragment shader reads its TEST_1
     * fields for the alpha test of opaque draws. */
    id<MTLBuffer>  matBuf;
    /* H18: set once this mesh has been reported for a rig-less opaque
     * draw, so every offending mesh is reported (once) instead of only
     * the first one of the session. */
    uint8_t        rig_warned;
};

/* The GS frame's routes (defined with em_gfx_gs_world_enable below). */
static int gsw_fail(EmGfx *g, const char *what);
static int gsw_background(EmGfx *g, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count);
static int gsw_opaque(EmGfx *g, const EmGfxGsPrim *prims, uint32_t count);
static int gsw_object(EmGfx *g, const EmGfxObjectUnit *unit);
static int gsw_page(EmGfx *g, const EmGfxGsPrim *prims, uint32_t count);
static int gsw_fogcol(EmGfx *g, const float rgb[3]);
static int gsw_alpha_clear(EmGfx *g);
static int gsw_box(EmGfx *g, const EmGfxShadowStrips *model, const float clip[16], uint32_t rgbaq);
static int gsw_silhouette(EmGfx *g, const float *verts, uint32_t vert_count, const uint32_t *indices,
                          uint32_t index_count, const float *nodes, uint32_t node_count, const float vp[16]);
static int gsw_receiver_begin(EmGfx *g, const float uv[16], const float camera[16]);
static int gsw_receiver(EmGfx *g, const EmGfxShadowStrips *object, uint32_t cls);
static int gsw_receiver_end(EmGfx *g);
static int gsw_list_frame(EmGfx *g, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count,
                          uint64_t display_frame, uint64_t display_scissor);
static void gsw_present(EmGfx *g);
static void gsw_write_field(EmGfx *g, const char *bmp_path);
static void gsw_complete(EmGfx *g);
static int gsw_slot(EmGfx *g);
static void write_bmp(const char *path, const uint8_t *bgra, uint32_t w, uint32_t h, uint32_t stride);

#define EM_DEPTH_FORMAT MTLPixelFormatDepth32Float

/* Minimal MSL shader, compiled at RUNTIME (newLibraryWithSource) — no offline
 * Metal toolchain / .metallib step required. Vertex buffer 0 holds, per vertex,
 * a float4 position followed by a float4 color (2 float4s/vertex). */
static NSString *const kTestShaderSrc =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct VOut { float4 pos [[position]]; float4 color; };\n"
"vertex VOut v_main(uint vid [[vertex_id]],\n"
"                   const device float4 *data [[buffer(0)]]) {\n"
"    VOut o;\n"
"    o.pos   = float4(data[vid*2].xy, 0.0, 1.0);\n"
"    o.color = data[vid*2 + 1];\n"
"    return o;\n"
"}\n"
"fragment float4 f_main(VOut in [[stage_in]]) { return in.color; }\n";

/* Textured-overlay shader (em_gfx_overlay_glyph) — runtime-compiled like
 * the others. Buffer 0 holds 3 float4s per vertex: pre-converted NDC
 * position, modulate color, uv (xy, normalized; z = 1 to sample nearest).
 * The fragment samples the registered overlay texture with the queue's
 * sampler (bilinear), or NEAREST where the vertex asks for it (the message
 * glyph strips: their packets set TEX1_1 = 0, em_gfx_overlay_glyph_nearest),
 * and modulates (TFX modulate). */
static NSString *const kGlyphShaderSrc =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct VOut { float4 pos [[position]]; float4 color; float2 uv; float nearest [[flat]]; };\n"
"vertex VOut v_glyph(uint vid [[vertex_id]],\n"
"                    const device float4 *data [[buffer(0)]]) {\n"
"    VOut o;\n"
"    o.pos   = float4(data[vid*3].xy, 0.0, 1.0);\n"
"    o.color = data[vid*3 + 1];\n"
"    o.uv    = data[vid*3 + 2].xy;\n"
"    o.nearest = data[vid*3 + 2].z;\n"
"    return o;\n"
"}\n"
"static float4 glyph_texel(VOut in, texture2d<float> tex, sampler smp) {\n"
"    constexpr sampler nearest_smp(filter::nearest, address::clamp_to_edge);\n"
"    return in.nearest > 0.5 ? tex.sample(nearest_smp, in.uv) : tex.sample(smp, in.uv);\n"
"}\n"
"fragment float4 f_glyph(VOut in [[stage_in]],\n"
"                        texture2d<float> tex [[texture(0)]],\n"
"                        sampler smp [[sampler(0)]]) {\n"
"    return glyph_texel(in, tex, smp) * in.color;\n"
"}\n"
"fragment float4 f_glyph_opaque(VOut in [[stage_in]],\n"
"                        texture2d<float> tex [[texture(0)]],\n"
"                        sampler smp [[sampler(0)]]) {\n"
"    float4 color = glyph_texel(in, tex, smp) * in.color;\n"
"    if (color.a <= 0.0) discard_fragment();\n"
"    return color;\n"
"}\n";

/* Background shader (em_gfx_background_prims): 2 float4s per vertex, the
 * NDC position of the kernel's GS 12.4 XY and the ST pair. Q is 1.0 (the
 * RGBAQ 001E1E60 sends), so ST interpolates affinely. The fragment is the
 * GS MODULATE with TCC 0: rgb = texel * RGBAQ / 128 (clamped like the GS
 * COLCLAMP), alpha = RGBAQ A. */
static NSString *const kBackgroundShaderSrc =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct VOut { float4 pos [[position]];\n"
"              float2 st [[center_no_perspective]]; };\n"
"vertex VOut v_background(uint vid [[vertex_id]],\n"
"                         const device float4 *data [[buffer(0)]]) {\n"
"    VOut o;\n"
"    o.pos = float4(data[vid*2].xy, 0.0, 1.0);\n"
"    o.st  = data[vid*2 + 1].xy;\n"
"    return o;\n"
"}\n"
"fragment float4 f_background(VOut in [[stage_in]],\n"
"                             texture2d<float> tex [[texture(0)]],\n"
"                             sampler smp [[sampler(0)]],\n"
"                             constant float4 &rgbaq [[buffer(0)]]) {\n"
"    float3 c = min(tex.sample(smp, in.st).rgb * rgbaq.rgb, float3(1.0));\n"
"    return float4(c, rgbaq.a);\n"
"}\n";

/* Skinning shader — the translated PS2 vertex pipeline. Buffer 0 holds
 * 10-word vertex records (float pos[3], float normal[3], float uv[2],
 * uint bone, uint tex); buffer 1 is the bone-matrix palette (column-major,
 * same layout the game stages for VU1 dmem); buffer 2 the camera; buffer 3
 * one float2 UV scale per texture (native size / array-slice size — the
 * slices hold each texture TILED to a common pow-2 size so sampler REPEAT
 * reproduces GS wrap). This mirrors the VU1 soft-skinner: vertex positions
 * are bone-local, world = palette[bone] * pos. Fragment = texture sample
 * (per-triangle slice via [[flat]]) modulated by the vertex color;
 * untextured vertices (tex == ~0u) keep the flat grey. A per-draw mode word
 * (fragment buffer 0) selects shading: bit 2 = the original object-kernel
 * colors em_lighting prepared at the authored vertices (vertex buffer 5,
 * integer GS RGB interpolated screen-linearly); bit 0 set = the "normal"
 * slot is a baked RGB vertex color (static level geometry ships its
 * lighting prebaked) and the fragment is texture * color, the GS modulate
 * path; a normal-carrying draw without a rig is rejected before encoding
 * (no stand-in light exists); bit 1 set = the GLOW pass (drawn
 * additively): the fragment is the texture sample alone, no alpha-test
 * cutout (the GS glow draws never update alpha or Z). Fragment buffer 1 is
 * the per-draw RGBA tint (em_gfx_draw_skinned_tinted — the GS RGBAQ actor
 * color multiplier): every shading path's output is multiplied by it;
 * em_gfx_draw_skinned binds opaque white, the exact identity.
 *
 * Vertex bone word: low 24 bits = palette slot, bit 31 = BILLBOARD glow
 * vertex (EM_GFX_VERT_BILLBOARD). For those the position is the anchor
 * point (bone-local) and the "normal" slot the camera-plane corner offset;
 * camera right/up come from the view rows of viewproj (rows 0/1 of P*V are
 * the view rotation's rows up to a positive projection scale, so their
 * normalized xyz IS the camera basis in world space). */
static NSString *const kSkinShaderSrc =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
EM_FOG_GS_MSL
"struct VOut { float4 pos [[position]]; float3 nrm; float3 wpos;\n"
"              float3 light_rgb [[center_no_perspective]];\n"
"              float3 vcol [[center_no_perspective]];\n"
"              float fog_f [[center_no_perspective]];\n"
"              float2 uv; uint slice [[flat]]; };\n"
"vertex VOut v_skin(uint vid [[vertex_id]],\n"
"                   const device uint *vdata [[buffer(0)]],\n"
"                   const device float4x4 *palette [[buffer(1)]],\n"
"                   constant float4x4 &viewproj [[buffer(2)]],\n"
"                   const device float2 *tscale [[buffer(3)]],\n"
"                   constant uint &mode [[buffer(4)]],\n"
"                   const device uint4 *vertex_rgba [[buffer(5)]],\n"
"                   constant float4 *fog [[buffer(6)]]) {\n"
"    const device float *fw = (const device float *)vdata;\n"
"    float3 p = float3(fw[vid*10+0], fw[vid*10+1], fw[vid*10+2]);\n"
"    float3 n = float3(fw[vid*10+3], fw[vid*10+4], fw[vid*10+5]);\n"
"    float2 uv = float2(fw[vid*10+6], fw[vid*10+7]);\n"
"    uint  tex = vdata[vid*10+9];\n"
"    uint  bw  = vdata[vid*10+8];\n"
"    float4x4 M = palette[bw & 0x00FFFFFFu];\n"
"    VOut o;\n"
"    o.light_rgb = (mode & 4u) ? float3(vertex_rgba[vid].xyz & 255u) / 128.0\n"
"                              : float3(1.0);\n"
"    if (bw & 0x80000000u) {\n"
"        /* camera-facing glow quad: anchor + corner along camera right/up\n"
"         * (viewproj row r = float3(vp[0][r], vp[1][r], vp[2][r])). */\n"
"        float3 c = (M * float4(p, 1.0)).xyz;\n"
"        float3 right = normalize(float3(viewproj[0].x, viewproj[1].x,\n"
"                                        viewproj[2].x));\n"
"        float3 up    = normalize(float3(viewproj[0].y, viewproj[1].y,\n"
"                                        viewproj[2].y));\n"
"        /* o.pos arithmetic kept EXACTLY as the pre-spot shader — a\n"
"         * reworked expression shifts clip positions by 1 ulp and flips\n"
"         * rasterized edge pixels (breaks byte-identical captures). */\n"
"        o.pos = viewproj * float4(c + right * n.x + up * n.y, 1.0);\n"
"        o.nrm = float3(1.0);\n"
"        o.wpos = c + right * n.x + up * n.y;\n"
"    } else {\n"
"        o.pos = viewproj * (M * float4(p, 1.0));\n"
"        o.nrm = (mode & 1u) ? n : (M * float4(n, 0.0)).xyz;\n"
"        o.wpos = (M * float4(p, 1.0)).xyz;\n"
"    }\n"
"    /* Baked level colour (mode bit 0). The kernel writes it to RGBAQ\n"
"     * and the GS Gouraud-interpolates it (template PRIM IIP 1) linearly\n"
"     * in SCREEN space, like light_rgb: center_no_perspective (R25). */\n"
"    o.vcol = (mode & 1u) ? n : float3(1.0);\n"
"    /* GS fog F per vertex (em_fog_gs.h): the 0023C780 kernel computes\n"
"     * F = A + B * clip_w, clamps to [0,255] and keeps its integer part\n"
"     * in XYZF2; the GS interpolates F linearly in screen space, hence\n"
"     * center_no_perspective. 255 = unfogged when fog is off. */\n"
"    o.fog_f = (fog[0].w > 0.0)\n"
"        ? floor(clamp(fog[1].x + fog[1].y * o.pos.w, 0.0, 255.0)) : 255.0;\n"
"    o.slice = tex;\n"
"    o.uv = (tex == 0xFFFFFFFFu) ? float2(0.0) : uv * tscale[tex];\n"
"    return o;\n"
"}\n"
"/* Flashlight spot term (em_gfx_spot_light — port deviation, see\n"
" * em_gfx.h: the engine never lights geometry from the toggle). Rows:\n"
" * [0] pos + enable, [1] dir + range, [2] cone cosines, [3] rgb.\n"
" * LEVEL-ONLY: only the baked-vertex-color LEVEL path (mode bit 0)\n"
" * adds it — the projected disc on the walls/floor. Actor draws take\n"
" * only the original em_lighting colors (H18: the former N.L character\n"
" * wrap and its camera-fill exception were invented). */\n"
"static float3 spot_term(float3 wpos, constant float4 *spot) {\n"
"    if (spot[0].w <= 0.0) return float3(0.0);\n"
"    float3 toF = wpos - spot[0].xyz;\n"
"    float dist = max(length(toF), 1e-4);\n"
"    float3 L   = toF / dist;\n"
"    float cone = smoothstep(spot[2].y, spot[2].x, dot(L, spot[1].xyz));\n"
"    float att  = clamp(1.0 - dist / spot[1].w, 0.0, 1.0);\n"
"    att *= att;\n"
"    return spot[3].rgb * (cone * att);\n"
"}\n"
"/* Distance fog (em_gfx_fog, em_fog_gs.h — the per-area GS fog).\n"
" * Rows: [0] FOGCOL as [0,1] colour + enable, [1] (A, B) coefficients.\n"
" * f255 is the GS F value interpolated from the vertices (0 is pure\n"
" * FOGCOL). The GS rule is FOGCOL + ((C - FOGCOL) * F7 >> 15) on the\n"
" * texture function's 8-bit colour with the 8.7 weight F7 (em_fog_gs_blend7,\n"
" * em_fog_gs_weight7, measured: GS_EXACT.md 3.2 / 5.2), so F = 255 still\n"
" * leaves 1/256 of FOGCOL. c is the texel times the vertex colour / 128\n"
" * (GS MODULATE), so its 8-bit value is floor(c * 255): the GS's\n"
" * min(255, Ct * Cv >> 7) for integer texels and colours (0.001 absorbs\n"
" * float noise). The texel itself is still the Metal sampler's, and the\n"
" * colour is the 8-bit one (the GS's texture function uses the 8.7 colour,\n"
" * GS_EXACT.md 5.1). */\n"
"static float3 fog_apply(float3 c, float f255, constant float4 *fog) {\n"
"    if (fog[0].w <= 0.0) return c;\n"
"    uint3 ci = uint3(clamp(floor(c * 255.0 + 0.001), 0.0, 255.0));\n"
"    uint3 fc = uint3(round(clamp(fog[0].rgb, 0.0, 1.0) * 255.0));\n"
"    return float3(em_fog_gs_blend7(ci, em_fog_gs_weight7(f255), fc)) / 255.0;\n"
"}\n"
"/* GS TEST_1 alpha test (docs/LEVEL_MATERIALS.md). `a` is the filtered\n"
" * texel alpha as exported (GS alpha At stored as min(255, 2*At)). For\n"
" * the decoded draws As = At: the level runs TFX modulate with Af 128\n"
" * (As = At*128>>7) and actors TFX highlight with Af 0 (As = At + 0).\n"
" * The GS truncates the bilinear result to an integer; 128 is exported\n"
" * as 255. AFAIL is KEEP (the only value mesh creation accepts), so a\n"
" * failing fragment writes neither colour nor depth. */\n"
"static bool gs_alpha_test(float a, uint test) {\n"
"    if (!(test & 1u)) return true;\n"
"    float as = (a >= 1.0) ? 128.0 : floor(a * 127.5 + 0.001);\n"
"    float aref = float((test >> 4) & 0xFFu);\n"
"    switch ((test >> 1) & 7u) {\n"
"    case 0u: return false;\n"
"    case 1u: return true;\n"
"    case 2u: return as < aref;\n"
"    case 3u: return as <= aref;\n"
"    case 4u: return as == aref;\n"
"    case 5u: return as >= aref;\n"
"    case 6u: return as > aref;\n"
"    default: return as != aref;\n"
"    }\n"
"}\n"
"fragment float4 f_skin(VOut in [[stage_in]],\n"
"                       texture2d_array<float> texs [[texture(0)]],\n"
"                       sampler smp [[sampler(0)]],\n"
"                       constant uint &mode [[buffer(0)]],\n"
"                       constant float4 &tint [[buffer(1)]],\n"
"                       constant float4 *spot [[buffer(2)]],\n"
"                       constant float4 *fog  [[buffer(4)]],\n"
"                       const device uint *gsmat [[buffer(5)]]) {\n"
"    float4 base = float4(0.55, 0.62, 0.70, 1.0);\n"
"    if (in.slice != 0xFFFFFFFFu) {\n"
"        base = texs.sample(smp, in.uv, in.slice);\n"
"        /* Mode bit 3 = an opaque draw in the decoded GS class 0: the\n"
"         * per-slice TEST_1 alpha test (ATST GREATER, AREF 0: only\n"
"         * texels whose filtered alpha is 0 are dropped) with blending\n"
"         * off. Otherwise (the port's translucent tint path, not\n"
"         * decoded) the legacy cutout at alpha 0.5 applies. The glow\n"
"         * pass skips both: additive draws never punch holes. */\n"
"        if (mode & 8u) {\n"
"            if (!gs_alpha_test(base.a, gsmat[in.slice] & 0x3FFFu))\n"
"                discard_fragment();\n"
"        } else if (!(mode & 2u) && base.a < 0.5) {\n"
"            discard_fragment();\n"
"        }\n"
"    }\n"
"    if (mode & 2u) {\n"
"        /* additive glow: Cv = Cs + Cd (GS ALPHA FIX=0x80); the layer\n"
"         * tint is pre-multiplied into the texels by the exporter. The\n"
"         * per-draw tint still applies (additive blend ignores alpha). */\n"
"        return float4(base.rgb, 1.0) * tint;\n"
"    }\n"
"    /* The spot add stays behind the enable branch so spot-off frames\n"
"     * run the EXACT pre-spot arithmetic (byte-identical captures). */\n"
"    if (mode & 1u) {\n"
"        /* baked vertex color (GS modulate) + the forward spot. The\n"
"         * level kernel adds 65536.0 (LOI 00237218, ADDI.y 00237220,\n"
"         * ADDy 002373B0) and PACKED RGBAQ takes the low byte:\n"
"         * floor(128*c), so 1.0 is GS 128 (identity) and colors up to\n"
"         * 255/128 over-brighten like the GS. */\n"
"        float3 lit = clamp(in.vcol, 0.0, 255.0 / 128.0);\n"
"        if (spot[0].w > 0.0)\n"
"            lit += spot_term(in.wpos, spot);\n"
"        return float4(fog_apply(base.rgb * lit, in.fog_f, fog), base.a)\n"
"             * tint;\n"
"    }\n"
"    /* Original VU lighting produces integer colors at authored\n"
"     * vertices. GS Gouraud interpolation is linear in screen space;\n"
"     * normalizing an interpolated normal and lighting per fragment\n"
"     * changes the original face/body shading. */\n"
"    if (mode & 4u) {\n"
"        return float4(fog_apply(base.rgb * in.light_rgb, in.fog_f, fog), base.a)\n"
"             * tint;\n"
"    }\n"
"    /* draw_skinned never encodes a normal-carrying draw without its\n"
"     * em_lighting colors; there is no stand-in light to fall back on. */\n"
"    discard_fragment();\n"
"    return float4(0.0);\n"
"}\n"
"/* Shadow receiver vertex: the kicked GS words (em_gfx_shadow_receiver):\n"
" * v[3 i] the position from XYZF2 X / Y / Z (em_background_gs_ndc and\n"
" * object_depth, w = 1: the mapping the static world's triangles use, so a\n"
" * receiver meets the level surface it lies on at the same depth), v[3 i +\n"
" * 1] (S, T, Q), screen-linear as the GS interpolates them, and v[3 i + 2]\n"
" * the RGBAQ A and fog F. */\n"
"vertex VOut v_shadow_recv_gs(uint vid [[vertex_id]], const device float4 *v [[buffer(0)]]) {\n"
"    VOut o;\n"
"    o.pos = v[3 * vid];\n"
"    o.nrm = v[3 * vid + 1].xyz;\n"
"    o.wpos = float3(0.0);\n"
"    o.light_rgb = float3(v[3 * vid + 2].x / 128.0, v[3 * vid + 2].y / 128.0, 0.0);\n"
"    o.vcol = float3(1.0);\n"
"    o.fog_f = 255.0;\n"
"    o.uv = float2(0.0);\n"
"    o.slice = 0u;\n"
"    return o;\n"
"}\n"
"/* Shadow receiver pixel (em_gfx_shadow_receiver, em_shadow_gs.h\n"
" * em_shadow_gs_bilinear_alpha / em_shadow_gs_receiver_pixel): v_shadow_recv_gs\n"
" * carries in light_rgb the kernel's RGBAQ A and fog F / 128 (screen-linear\n"
" * interpolation) and in nrm the (S, T, Q) the pixel divides (the GS's STQ:\n"
" * u = S / Q, v = T / Q per pixel). dst is the frame pixel (framebuffer fetch). APPROXIMATION,\n"
" * not verified against a GS dump of a drawn shadow: A per pixel is\n"
" * floor(value * 128 + 0.001) of Metal's float interpolation (8-bit, where\n"
" * the GS's texture function uses the 8.7 A: GS_EXACT.md 5.1); F is the 8.7\n"
" * weight em_fog_gs_weight7 (GS_EXACT.md 5.2); the GS's own DDA stepping is\n"
" * not modelled and the epsilons are heuristics (docs/SHADOW_ORIGINAL.md,\n"
" * open items). */\n"
"fragment float4 f_shadow_receiver(VOut in [[stage_in]],\n"
"        float4 dst [[color(0)]],\n"
"        texture2d<float, access::read> sil [[texture(0)]],\n"
"        constant float4 *fog [[buffer(4)]]) {\n"
"    int uu = int(floor(in.nrm.x / in.nrm.z * 2048.0)) - 8;\n"
"    int vv = int(floor(in.nrm.y / in.nrm.z * 2048.0)) - 8;\n"
"    int fu = uu & 15, fv = vv & 15;\n"
"    int x0 = clamp(uu >> 4, 0, 127), x1 = clamp((uu >> 4) + 1, 0, 127);\n"
"    int y0 = clamp(vv >> 4, 0, 127), y1 = clamp((vv >> 4) + 1, 0, 127);\n"
"    uint a00 = uint(round(sil.read(uint2(x0, y0)).a * 255.0));\n"
"    uint a10 = uint(round(sil.read(uint2(x1, y0)).a * 255.0));\n"
"    uint a01 = uint(round(sil.read(uint2(x0, y1)).a * 255.0));\n"
"    uint a11 = uint(round(sil.read(uint2(x1, y1)).a * 255.0));\n"
"    uint at = (a00 * uint((16 - fu) * (16 - fv)) + a10 * uint(fu * (16 - fv))\n"
"             + a01 * uint((16 - fu) * fv) + a11 * uint(fu * fv)) >> 8;\n"
"    uint a = uint(floor(in.light_rgb.x * 128.0 + 0.001));\n"
"    uint f7 = em_fog_gs_weight7(in.light_rgb.y * 128.0);\n"
"    uint as = min((at * a) >> 7, 255u);\n"
"    if (as == 0u) discard_fragment();\n"
"    uint4 d = uint4(round(dst * 255.0));\n"
"    if ((d.a & 0x80u) == 0u) discard_fragment();\n"
"    int3 fc = int3(round(fog[0].rgb * 255.0));\n"
"    int3 cs = int3(em_fog_gs_blend7(uint3(0u), f7, uint3(fc)));\n"
"    int3 dc = int3(d.rgb);\n"
"    float3 prod = float3((cs - dc) * int(as));\n"
"    int3 c = clamp(int3(floor(prod / 128.0)) + dc, 0, 255);\n"
"    return float4(float3(c) / 255.0, float(as) / 255.0);\n"
"}\n";

/* Blend state selector for build_pipeline — the three GS ALPHA configs
 * the translated draws use (each comment gives the GS A/B/C/D form). */
typedef enum {
    EM_BLEND_ALPHA, /* standard alpha: src*a + dst*(1-a)                  */
    EM_BLEND_ADD,   /* additive (A=Cs B=0 C=FIX 0x80 D=Cd): src + dst     */
    EM_BLEND_RSUB,  /* reverse subtract (A=Cd B=Cs C=FIX 0x80 D=0):
                       max(0, dst - src) — the screen-fade sprite blend   */
    EM_BLEND_OPAQUE,
} EmBlendMode;

/* Compile MSL source at runtime and build a pipeline for the swapchain +
 * depth formats. Returns +1-retained PSO or nil (with the error printed). */
static id<MTLRenderPipelineState> build_pipeline(EmGfx *g, NSString *src,
                                                 NSString *vfn_name,
                                                 NSString *ffn_name,
                                                 EmBlendMode blend)
{
    NSError *err = nil;
    id<MTLLibrary> lib = [g->device newLibraryWithSource:src
                                                 options:nil
                                                   error:&err];
    if (!lib) {
        fprintf(stderr, "metal: runtime shader compile failed: %s\n",
                err ? err.localizedDescription.UTF8String : "(unknown)");
        return nil;
    }
    id<MTLFunction> vfn = [lib newFunctionWithName:vfn_name];
    id<MTLFunction> ffn = [lib newFunctionWithName:ffn_name];
    MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction   = vfn;
    pd.fragmentFunction  = ffn;
    pd.colorAttachments[0].pixelFormat = g->layer.pixelFormat;
    /* EM_BLEND_ALPHA — standard alpha blending: most PS2 texels are fully
     * opaque (alpha-test in the shader handles cutouts), so this only
     * softens the few partial-alpha texels (window glass) without needing
     * draw sorting.
     * EM_BLEND_ADD — the glow/beam variant: Cv = Cs + Cd, the GS ALPHA
     * (A=Cs, B=0, C=FIX 0x80, D=Cd) of the live glow draws; dest alpha is
     * left untouched (the GS never updates A on those draws).
     * EM_BLEND_RSUB — the screen-fade variant: Cv = max(0, Cd - Cs), the
     * GS ALPHA_2 0xA1/FIX 0x80 (A=Cd, B=Cs, C=FIX, D=0) of the fade
     * sprite; ReverseSubtract with ONE/ONE saturates at 0 on the unorm
     * target exactly like the GS clamp, and dest alpha is kept. */
    pd.colorAttachments[0].blendingEnabled = YES;
    switch (blend) {
    case EM_BLEND_OPAQUE:
        pd.colorAttachments[0].blendingEnabled = NO;
        break;
    case EM_BLEND_ADD:
        pd.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorOne;
        pd.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOne;
        pd.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorZero;
        pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
        break;
    case EM_BLEND_RSUB:
        pd.colorAttachments[0].rgbBlendOperation = MTLBlendOperationReverseSubtract;
        pd.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorOne;
        pd.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOne;
        pd.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorZero;
        pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
        break;
    case EM_BLEND_ALPHA:
        pd.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
        pd.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
        pd.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorOne;
        pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
        break;
    }
    pd.depthAttachmentPixelFormat      = EM_DEPTH_FORMAT;
    id<MTLRenderPipelineState> pso =
        [g->device newRenderPipelineStateWithDescriptor:pd error:&err];
    [pd release];
    if (!pso) {
        fprintf(stderr, "metal: pipeline build failed: %s\n",
                err ? err.localizedDescription.UTF8String : "(unknown)");
    }
    return pso; /* +1 retain count owned by caller */
}

static void ensure_depth_states(EmGfx *g)
{
    if (g->depthOn) return;
    MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
    dd.depthCompareFunction = MTLCompareFunctionLessEqual;
    dd.depthWriteEnabled    = YES;
    g->depthOn = [g->device newDepthStencilStateWithDescriptor:dd];
    dd.depthCompareFunction = MTLCompareFunctionAlways;
    dd.depthWriteEnabled    = NO;
    g->depthOff = [g->device newDepthStencilStateWithDescriptor:dd];
    /* glow pass: depth TEST on, depth WRITE off — the GS glow draws keep
     * ZTE=1/ZTST=GEQUAL with ZMSK=1, so geometry still occludes the glow
     * but the glow never occludes anything. */
    dd.depthCompareFunction = MTLCompareFunctionLessEqual;
    dd.depthWriteEnabled    = NO;
    g->depthGlow = [g->device newDepthStencilStateWithDescriptor:dd];
    [dd release];
}

/* (Re)allocate the depth texture to match the drawable. */
static void ensure_depth_texture(EmGfx *g, NSUInteger w, NSUInteger h)
{
    if (g->depthTex && g->depthTex.width == w && g->depthTex.height == h)
        return;
    [g->depthTex release];
    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:EM_DEPTH_FORMAT
                                     width:w
                                    height:h
                                 mipmapped:NO];
    td.usage       = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    g->depthTex = [g->device newTextureWithDescriptor:td];
}

EmGfx *em_gfx_create(EmWindow *win)
{
#if TARGET_OS_IPHONE
    CAMetalLayer *layer = (CAMetalLayer *)em_window_native_handle(win);
    if (![layer isKindOfClass:[CAMetalLayer class]]) return NULL;
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) return NULL;

    EmGfx *g = (EmGfx *)calloc(1, sizeof(EmGfx));
    g->win    = win;
    g->layer  = [layer retain];
    g->device = [dev retain];
    g->queue  = [[dev newCommandQueue] retain];

    /* The macOS settings below, applied on the main thread, which owns
     * UIKit's layer (this runs on the game thread). */
    void (^configure)(void) = ^{
        layer.device          = dev;
        layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
        layer.opaque          = YES;
        layer.framebufferOnly = NO;
    };
    if ([NSThread isMainThread]) configure();
    else dispatch_sync(dispatch_get_main_queue(), configure);
#else
    NSView *view = (NSView *)em_window_native_handle(win);
    if (!view) return NULL;
    CAMetalLayer *layer = (CAMetalLayer *)view.layer;
    if (![layer isKindOfClass:[CAMetalLayer class]]) return NULL;

    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) return NULL;

    EmGfx *g = (EmGfx *)calloc(1, sizeof(EmGfx));
    g->view   = [view retain];
    g->layer  = [layer retain];
    g->device = [dev retain];
    g->queue  = [[dev newCommandQueue] retain];

    layer.device          = dev;
    layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
    /* The frame's alpha channel is the GS destination alpha (the shadow
     * chain writes it; em_gfx_shadow_*); the PS2 display never shows it,
     * so neither may the window. */
    layer.opaque          = YES;
    /* NO so the drawable can be blit-read by the capture path. */
    layer.framebufferOnly = NO;
#endif
    g->headless = em_headless();
    g->shadowCurrent = -1;
    return g;
}

void em_gfx_destroy(EmGfx *g)
{
    if (!g) return;
    [g->testPipeline release];
    [g->skinPipeline release];
    [g->skinOpaquePipeline release];
    [g->glowPipeline release];
    [g->bgTexture release];
    [g->bgPipeline release];
    [g->shadowAlphaPipeline release];
    [g->shadowSilPipeline release];
    [g->shadowRecvPipeline release];
    for (unsigned i = 0; i < EM_GFX_SHADOW_TARGET_MAX; i++)
        [g->shadowTarget[i] release];
    [g->shadowLast release];
    [g->gsTexPipeline release];
    [g->gsFlatPipeline release];
    for (uint32_t i = 0; i < g->gsSurfCount; i++)
        [g->gsSurf[i].tex release];
    [g->gsfPipeline release];
    [g->gsfShowPipeline release];
    [g->gsfNoTexture release];
    for (uint32_t i = 0; i < g->objTexCount; i++)
        [g->objTex[i].tex release];
    [g->objPipeline release];
    free(g->opaqueTri);
    em_object_unit_result_free(&g->objResult);
    gsw_complete(g);
    em_gs_world_destroy(g->gsw);
    for (int i = 0; i < 3; i++) {
        [g->gswSlotTex[i] release];
        [g->gswSlotCmd[i] release];
    }
    free(g->gswPrims);
    free(g->bgFile);
    [g->glyphPipeline release];
    [g->spriteAddPipeline release];
    [g->spriteSubPipeline release];
    [g->spriteOpaquePipeline release];
    [g->subPipeline release];
    [g->addOverlayPipeline release];
    [g->overlayTex[0] release];
    [g->overlayTex[1] release];
    [g->clampSampler release];
    [g->repeatSampler release];
    [g->depthOn release];
    [g->depthOff release];
    [g->depthGlow release];
    [g->depthTex release];
    [g->offscreen release];
    [g->queue release];
    [g->device release];
    [g->layer release];
#if !TARGET_OS_IPHONE
    [g->view release];
#endif
    free(g);
}

void em_gfx_begin_frame(EmGfx *g, float r, float gr, float b, float a)
{
    if (!g) return;
    g->pool = [[NSAutoreleasePool alloc] init];
    g->overlayW    = EM_GFX_OVERLAY_W;  /* canvas resets to the default */
    g->glyphNearest = false;            /* font quads sample bilinear   */
    g->overlayH    = EM_GFX_OVERLAY_H;
    memset(g->spot, 0, sizeof(g->spot)); /* the forward spot is per-frame */
    memset(g->rig,  0, sizeof(g->rig));  /* character rig is per-frame   */
    memset(g->face_rig, 0, sizeof(g->face_rig));
    memset(g->fog,  0, sizeof(g->fog));  /* distance fog is per-frame    */
    g->shadowTargets  = 0;               /* shadow targets are per-frame  */
    g->shadowCurrent  = -1;
    g->shadowRecvOpen = false;
    g->gswFrame = false;                 /* the GS frame is per-frame too */
    g->gswShow = false;

    /* keep the swapchain sized to the backing store */
#if TARGET_OS_IPHONE
    /* The UIKit layer sizes the drawable on the main thread and publishes
     * the size; before its first layout a headless target gets 960x720. */
    int iw = 0, ih = 0;
    em_window_drawable_size(g->win, &iw, &ih);
    if (iw <= 0 || ih <= 0) { iw = 960; ih = 720; }
    NSUInteger pw = (NSUInteger)iw, ph = (NSUInteger)ih;
#else
    NSSize sz = g->view.bounds.size;
    CGFloat scale = g->view.window.backingScaleFactor;
    if (scale <= 0) scale = 1.0;
    NSUInteger pw = (NSUInteger)(sz.width * scale), ph = (NSUInteger)(sz.height * scale);
#endif
    if (g->headless) {
        if (!g->offscreen || g->offscreen.width != pw || g->offscreen.height != ph) {
            [g->offscreen release];
            MTLTextureDescriptor *td = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                             width:pw height:ph mipmapped:NO];
            td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            td.storageMode = MTLStorageModePrivate;
            g->offscreen = [g->device newTextureWithDescriptor:td];
        }
        g->target = g->offscreen;
    } else {
#if !TARGET_OS_IPHONE
        g->layer.drawableSize = CGSizeMake(sz.width * scale, sz.height * scale);
#endif
        g->drawable = [[g->layer nextDrawable] retain];
        g->target = g->drawable ? g->drawable.texture : nil;
    }
    if (!g->target) { return; }
    g->cmd = [[g->queue commandBuffer] retain];

    ensure_depth_texture(g, g->target.width, g->target.height);

    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture     = g->target;
    rp.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    /* The whole drawable clears to BLACK: anything outside the 4:3 game
     * frame below stays black (the letterbox/pillarbox bars). */
    rp.colorAttachments[0].clearColor  = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    rp.depthAttachment.texture     = g->depthTex;
    rp.depthAttachment.loadAction  = MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionDontCare;
    rp.depthAttachment.clearDepth  = 1.0;

    /* Keep the encoder open so draw calls can run between begin and end. */
    g->enc = [[g->cmd renderCommandEncoderWithDescriptor:rp] retain];

    /* GAME FRAME = the largest centered 4:3 rect of the drawable (the
     * PS2 renders a 512x448 frame displayed at 4:3; PCSX2 presents the
     * same letterboxed/pillarboxed 4:3 frame). Viewport + scissor apply
     * to EVERY draw of the frame — the engine projection's baked 4:3
     * aspect (em_mat4_perspective_gs), the s49-pinned UI projection and
     * the overlay's virtual canvases all map NDC onto this rect, so a
     * non-4:3 window letterboxes instead of stretching the image. */
    if (g->enc) {
        double dw = (double)g->target.width;
        double dh = (double)g->target.height;
        double vw = dw, vh = dh, vx = 0.0, vy = 0.0;
        if (dw * 3.0 >= dh * 4.0) {            /* wide: pillarbox */
            vw = dh * 4.0 / 3.0;  vx = (dw - vw) * 0.5;
        } else {                               /* tall: letterbox */
            vh = dw * 3.0 / 4.0;  vy = (dh - vh) * 0.5;
        }
        [g->enc setViewport:(MTLViewport){ vx, vy, vw, vh, 0.0, 1.0 }];
        [g->enc setScissorRect:(MTLScissorRect){
            (NSUInteger)vx, (NSUInteger)vy,
            (NSUInteger)vw, (NSUInteger)vh }];

        /* Fill the game frame with the requested clear color (the bars
         * keep the pass's black clear): one full-NDC quad through the
         * overlay pipeline, depth off — the in-frame equivalent of the
         * old whole-drawable clear. */
        if (!g->testPipeline)
            g->testPipeline = build_pipeline(g, kTestShaderSrc, @"v_main",
                                             @"f_main", EM_BLEND_ALPHA);
        if (g->testPipeline) {
            ensure_depth_states(g);
            float v[6 * 8];
            static const float corner[6][2] = {
                { -1.0f,  1.0f }, { 1.0f,  1.0f }, { -1.0f, -1.0f },
                {  1.0f,  1.0f }, { 1.0f, -1.0f }, { -1.0f, -1.0f },
            };
            for (int i = 0; i < 6; i++) {
                float *o = v + i * 8;
                o[0] = corner[i][0]; o[1] = corner[i][1];
                o[2] = 0.0f;         o[3] = 1.0f;
                o[4] = r; o[5] = gr; o[6] = b; o[7] = a;
            }
            [g->enc setRenderPipelineState:g->testPipeline];
            [g->enc setDepthStencilState:g->depthOff];
            [g->enc setCullMode:MTLCullModeNone];
            [g->enc setVertexBytes:v length:sizeof(v) atIndex:0];
            [g->enc drawPrimitives:MTLPrimitiveTypeTriangle
                       vertexStart:0
                       vertexCount:6];
        }
    }
}

void em_gfx_draw_test_triangle(EmGfx *g)
{
    if (!g || !g->enc) return;
    if (!g->testPipeline) {       /* lazily, once */
        g->testPipeline = build_pipeline(g, kTestShaderSrc, @"v_main",
                                         @"f_main", EM_BLEND_ALPHA);
        if (!g->testPipeline) return;             /* compile failed; skip */
    }
    ensure_depth_states(g);
    [g->enc setDepthStencilState:g->depthOff];
    /* 3 vertices: each is a float4 position (clip-space xy used) + float4 color. */
    static const float verts[] = {
         0.0f,  0.6f, 0.0f, 1.0f,   1.0f, 0.2f, 0.2f, 1.0f,  /* top    - red   */
        -0.6f, -0.5f, 0.0f, 1.0f,   0.2f, 1.0f, 0.3f, 1.0f,  /* left   - green */
         0.6f, -0.5f, 0.0f, 1.0f,   0.3f, 0.4f, 1.0f, 1.0f,  /* right  - blue  */
    };
    [g->enc setRenderPipelineState:g->testPipeline];
    [g->enc setVertexBytes:verts length:sizeof(verts) atIndex:0];
    [g->enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

/* GS class-0 draw state (docs/LEVEL_MATERIALS.md) for meshes without
 * exported codes: TEST_1 bits 0..13 of 0x5000D (ATE 1, ATST GREATER,
 * AREF 0, AFAIL KEEP), PRIM ABE 0, ALPHA 0xA8, TCC 1, TEX1 MMAG/MMIN
 * LINEAR, CLAMP REPEAT — the env packet 001D0F20 builds at
 * arena+0xBA0+0x5A0 (D_00815360) that every textured opaque object-kernel
 * draw in the captures uses. TFX is left 0 here: the backend does not
 * read it (actor records carry TFX 2 with Af 0, which shades like
 * modulate). */
static const uint32_t kGsClass0Code =
    0x000Du | (0xA8u << 15) | (1u << 23) | (1u << 26) | (1u << 27);

/* Accept only the GS state this backend reproduces exactly as decoded:
 * alpha fail = KEEP, no blending, RGBA texture, modulate (TFX 0, level
 * Af 128) or highlight (TFX 2, Af 0 — the exporter checks every record),
 * MMAG and MMIN LINEAR (the code carries no MXL, so mipmapped MMIN values
 * are refused too) and REPEAT. Anything else is refused rather than
 * approximated. */
static const char *gsmat_unsupported(uint32_t c)
{
    uint32_t test = EM_GFX_GSMAT_TEST(c);
    if (EM_GFX_GS_TEST_ATE(test) && EM_GFX_GS_TEST_AFAIL(test) != 0)
        return "alpha-test AFAIL other than KEEP";
    if (EM_GFX_GSMAT_ABE(c)) return "alpha blending (PRIM ABE 1)";
    if (!EM_GFX_GSMAT_TCC(c)) return "RGB-only texture (TCC 0)";
    if (EM_GFX_GSMAT_TFX(c) != 0 && EM_GFX_GSMAT_TFX(c) != 2)
        return "texture function DECAL/HIGHLIGHT2";
    if (!EM_GFX_GSMAT_MMAG(c) || EM_GFX_GSMAT_MMIN(c) != 1)
        return "a texture filter other than LINEAR/LINEAR";
    if (EM_GFX_GSMAT_WRAP(c) != 0) return "CLAMP/REGION wrap mode";
    return NULL;
}

EmGfxMesh *em_gfx_mesh_create(EmGfx *g, const float *verts,
                              uint32_t vert_count, const uint32_t *indices,
                              uint32_t index_count,
                              const EmGfxTexDesc *texs, uint32_t tex_count,
                              const uint8_t *texels, uint32_t flags)
{
    if (!g || !verts || !indices || !vert_count || !index_count) return NULL;
    if ((flags & EM_GFX_MESH_GSMAT) && (!texs || !tex_count)) {
        fprintf(stderr, "gfx: GS material codes flagged on a mesh without "
                "textures — mesh rejected\n");
        return NULL;
    }
    for (uint32_t i = 0; (flags & EM_GFX_MESH_GSMAT) && i < tex_count; i++) {
        const char *why = gsmat_unsupported(texs[i].reserved);
        if (why) {
            fprintf(stderr, "gfx: texture %u GS code %08X uses %s, which "
                    "the Metal backend does not implement — mesh rejected\n",
                    i, texs[i].reserved, why);
            return NULL;
        }
    }
    EmGfxMesh *m = (EmGfxMesh *)calloc(1, sizeof(EmGfxMesh));
    m->flags = flags;
    m->vbuf = [g->device newBufferWithBytes:verts
                                     length:(NSUInteger)vert_count * 10 * 4
                                    options:MTLResourceStorageModeShared];

    /* Partition triangles: EM_GFX_VERT_BILLBOARD (additive glow) triangles
     * move to the END of the index buffer so draw_skinned can issue them
     * as a second, additive, no-depth-write pass after the opaque set.
     * Within each class the original order is kept. A triangle is a glow
     * triangle iff its first vertex carries the flag (the exporter flags
     * whole parts uniformly). */
    const uint32_t *vw = (const uint32_t *)verts;
    uint32_t *sorted = (uint32_t *)malloc((size_t)index_count * 4);
    uint32_t  n_glow = 0;
    for (uint32_t i = 0; i + 2 < index_count; i += 3)
        if (vw[(size_t)indices[i] * 10 + 8] & EM_GFX_VERT_BILLBOARD)
            n_glow += 3;
    uint32_t op = 0, gl = index_count - n_glow;
    for (uint32_t i = 0; i + 2 < index_count; i += 3) {
        bool glow = vw[(size_t)indices[i] * 10 + 8] & EM_GFX_VERT_BILLBOARD;
        uint32_t *dst = sorted + (glow ? gl : op);
        dst[0] = indices[i]; dst[1] = indices[i + 1]; dst[2] = indices[i + 2];
        if (glow) gl += 3; else op += 3;
    }
    for (uint32_t i = index_count - (index_count % 3); i < index_count; i++)
        sorted[op++] = indices[i];   /* non-triple tail (none in practice) */
    m->ibuf = [g->device newBufferWithBytes:sorted
                                     length:(NSUInteger)index_count * 4
                                    options:MTLResourceStorageModeShared];
    free(sorted);
    m->index_count  = index_count;
    m->glow_count   = n_glow;
    m->opaque_count = index_count - n_glow;

    /* Texture array: common slice size = max texture dims (all pow-2 in
     * the PS2 data); each texture is tiled across its slice so REPEAT
     * sampling wraps at the native period. With no textures, a 1x1 white
     * slice keeps the pipeline's bindings valid. */
    uint32_t sw = 1, sh = 1;
    for (uint32_t i = 0; i < tex_count; i++) {
        if (texs[i].width  > sw) sw = texs[i].width;
        if (texs[i].height > sh) sh = texs[i].height;
    }
    uint32_t slices = tex_count ? tex_count : 1;
    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:sw
                                    height:sh
                                 mipmapped:NO];
    td.textureType = MTLTextureType2DArray;
    td.arrayLength = slices;
    m->texArray = [g->device newTextureWithDescriptor:td];

    float   *scales = (float *)malloc((size_t)slices * 2 * sizeof(float));
    uint8_t *slice  = (uint8_t *)malloc((size_t)sw * sh * 4);
    for (uint32_t i = 0; i < slices; i++) {
        uint32_t w = 1, h = 1;
        const uint8_t *src = (const uint8_t *)"\xff\xff\xff\xff";
        if (i < tex_count && texels) {
            w = texs[i].width;
            h = texs[i].height;
            src = texels + texs[i].offset;
        }
        for (uint32_t y = 0; y < sh; y++) {
            const uint8_t *srow = src + (size_t)(y % h) * w * 4;
            uint8_t *drow = slice + (size_t)y * sw * 4;
            for (uint32_t x = 0; x < sw; x++)
                memcpy(drow + x * 4, srow + (x % w) * 4, 4);
        }
        [m->texArray replaceRegion:MTLRegionMake2D(0, 0, sw, sh)
                       mipmapLevel:0
                             slice:i
                         withBytes:slice
                       bytesPerRow:(NSUInteger)sw * 4
                     bytesPerImage:(NSUInteger)sw * sh * 4];
        scales[i * 2 + 0] = (float)w / (float)sw;
        scales[i * 2 + 1] = (float)h / (float)sh;
    }
    free(slice);
    m->scaleBuf = [g->device newBufferWithBytes:scales
                                         length:(NSUInteger)slices * 8
                                        options:MTLResourceStorageModeShared];
    free(scales);
    m->tex_count = tex_count;

    uint32_t *codes = (uint32_t *)malloc((size_t)slices * sizeof(uint32_t));
    if (codes) {
        for (uint32_t i = 0; i < slices; i++)
            codes[i] = ((flags & EM_GFX_MESH_GSMAT) && i < tex_count)
                     ? texs[i].reserved : kGsClass0Code;
        m->matBuf = [g->device newBufferWithBytes:codes
                                           length:(NSUInteger)slices * 4
                                          options:MTLResourceStorageModeShared];
        free(codes);
    }

    if (!m->vbuf || !m->ibuf || !m->texArray || !m->scaleBuf || !m->matBuf) {
        em_gfx_mesh_destroy(g, m);
        return NULL;
    }
    return m;
}

void em_gfx_mesh_destroy(EmGfx *g, EmGfxMesh *m)
{
    (void)g;
    if (!m) return;
    [m->vbuf release];
    [m->ibuf release];
    [m->texArray release];
    [m->scaleBuf release];
    [m->matBuf release];
    free(m);
}

int em_gfx_mesh_update_positions(EmGfx *g, EmGfxMesh *m,
                                 const float *positions, uint32_t count)
{
    if (!g || !m || !positions || !count ||
        [m->vbuf length]!=(NSUInteger)count*10*sizeof(float)) return 0;
    /* A fresh buffer preserves positions already referenced by an encoded
     * or executing command buffer. Metal's command buffer retains resources
     * used by its encoders; replacing our ownership does not race that draw. */
    id<MTLBuffer> next=[g->device newBufferWithBytes:[m->vbuf contents]
                                    length:[m->vbuf length]
                                   options:MTLResourceStorageModeShared];
    if (!next) return 0;
    float *vertices=(float *)[next contents];
    for (uint32_t i=0;i<count;++i)
        memcpy(vertices+(size_t)i*10,positions+(size_t)i*3,3*sizeof(float));
    [m->vbuf release];
    m->vbuf=next;
    return 1;
}

/* Wrapper: a skinned draw with no color modulation is a tinted draw with
 * opaque white — the multiply-by-1.0 identity, so output stays
 * bit-identical to the pre-tint pipeline. */
void em_gfx_draw_skinned(EmGfx *g, EmGfxMesh *m, const float *viewproj,
                         const float *palette, uint32_t bone_count)
{
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    em_gfx_draw_skinned_tinted(g, m, viewproj, palette, bone_count, white);
}

/* Tinted skinned draw (em_gfx.h — the GS RGBAQ per-draw modulate). The
 * tint rides as ONE extra setFragmentBytes (fragment buffer 1).
 * THRESHOLD RULE: rgba[3] >= 1.0 → opaque draw in the decoded GS class 0
 * (docs/LEVEL_MATERIALS.md): blending OFF (PRIM ABE 0), depth write ON,
 * and the per-slice TEST_1 alpha test (only texels whose filtered alpha
 * is 0 are dropped). rgba[3] < 1.0 → translucent draw (port path, not
 * decoded): standard alpha blending, depth WRITE off (ZMSK=1, test kept
 * on) so a fading gib never occludes what shows through it, and the
 * legacy alpha 0.5 cutout, so cutout holes stay holes while fading. */
static id<MTLBuffer> vertex_lighting_buffer(EmGfx *g, EmGfxMesh *mesh,
                                           const float *palette,
                                           uint32_t bone_count)
{
    unsigned rig_count = g->face_rig[27] > 0.0f ? 2 : 1;
    EmLightingMatrices *matrices = malloc((size_t)bone_count * rig_count * sizeof *matrices);
    if (!matrices) return nil;
    for (unsigned rig_index = 0; rig_index < rig_count; ++rig_index) {
        const float *rig = rig_index ? g->face_rig : g->rig;
        for (uint32_t bone = 0; bone < bone_count; ++bone) {
            if (!em_lighting_matrices(&matrices[(size_t)rig_index*bone_count+bone],
                                      palette + (size_t)bone*16, rig, rig+12, rig+24)) {
                free(matrices);
                return nil;
            }
        }
    }
    NSUInteger count = [mesh->vbuf length] / 40;
    id<MTLBuffer> buffer = [g->device newBufferWithLength:count*16
                                               options:MTLResourceStorageModeShared];
    if (!buffer) {
        free(matrices);
        return nil;
    }
    const float *vertices = [mesh->vbuf contents];
    uint32_t *colors = [buffer contents];
    for (NSUInteger vertex = 0; vertex < count; ++vertex) {
        uint32_t bone_word;
        memcpy(&bone_word, vertices + vertex*10+8, sizeof bone_word);
        uint32_t bone = bone_word & 0x00ffffffu;
        int face = (bone_word & EM_GFX_VERT_FACE_LIGHT) != 0;
        if (bone >= bone_count || (face && rig_count < 2)) {
            [buffer release];
            free(matrices);
            return nil;
        }
        em_lighting_vertex(colors + vertex*4, vertices + vertex*10+3,
                           &matrices[(size_t)face*bone_count+bone]);
    }
    free(matrices);
    return buffer;
}

static void draw_skinned(EmGfx *g, EmGfxMesh *m, const float *viewproj,
                         const float *palette, uint32_t bone_count,
                         const float rgba[4])
{
    if (!g || !g->enc || !m || !viewproj || !palette || !bone_count || !rgba)
        return;
    /* Opaque (tint alpha >= 1, non-additive) draws are the decoded GS
     * class 0: blending off (PRIM ABE 0) and the per-slice TEST_1 alpha
     * test (mode bit 3). The translucent tint path keeps the alpha
     * pipeline and the legacy cutout (port path, not decoded). */
    bool class0 = rgba[3] >= 1.0f;
    if (!class0 && !g->skinPipeline) {
        g->skinPipeline = build_pipeline(g, kSkinShaderSrc,
                                         @"v_skin", @"f_skin",
                                         EM_BLEND_ALPHA);
        if (!g->skinPipeline) return;
    }
    if (class0 && !g->skinOpaquePipeline) {
        g->skinOpaquePipeline = build_pipeline(g, kSkinShaderSrc,
                                               @"v_skin", @"f_skin",
                                               EM_BLEND_OPAQUE);
        if (!g->skinOpaquePipeline) return;
    }
    if (m->glow_count && !g->glowPipeline) {
        g->glowPipeline = build_pipeline(g, kSkinShaderSrc,
                                         @"v_skin", @"f_skin",
                                         EM_BLEND_ADD);
        if (!g->glowPipeline) return;
    }
    ensure_depth_states(g);
    if (!g->repeatSampler) {
        MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
        sd.minFilter = MTLSamplerMinMagFilterLinear;
        sd.magFilter = MTLSamplerMinMagFilterLinear;
        sd.sAddressMode = MTLSamplerAddressModeRepeat;
        sd.tAddressMode = MTLSamplerAddressModeRepeat;
        g->repeatSampler = [g->device newSamplerStateWithDescriptor:sd];
        [sd release];
    }
    [g->enc setRenderPipelineState:(class0 ? g->skinOpaquePipeline
                                    : g->skinPipeline)];
    /* Threshold rule (see the comment above): a tint alpha below 1.0
     * selects the translucent state — depth test on, write off. */
    bool translucent = rgba[3] < 1.0f;
    [g->enc setDepthStencilState:(translucent ? g->depthGlow : g->depthOn)];
    /* Strip winding from the PS2 data is not normalised yet — draw
     * double-sided until the translated GS context supplies cull state. */
    [g->enc setCullMode:MTLCullModeNone];
    [g->enc setVertexBuffer:m->vbuf offset:0 atIndex:0];
    /* The palette is small (21 bones * 64B); inline constants keep this a
     * one-call update, like the game's per-frame VU1 dmem upload. */
    [g->enc setVertexBytes:palette length:(NSUInteger)bone_count * 64
                   atIndex:1];
    [g->enc setVertexBytes:viewproj length:64 atIndex:2];
    [g->enc setVertexBuffer:m->scaleBuf offset:0 atIndex:3];
    uint32_t mode = (m->flags & EM_GFX_MESH_VCOLOR) | (class0 ? 8u : 0u);
    id<MTLBuffer> vertex_colors = nil;
    bool opaque = m->opaque_count != 0;
    if (opaque && !(mode & 3u)) {
        if (!(g->rig[27] > 0.0f)) {
            /* H18: the original lights every actor draw with a room rig
             * (001D89D0 mode 0; 001D7B30 never fails). A missing rig is a
             * caller contract fault, not a cue for an invented light:
             * the opaque set is not drawn (the additive glow set, which
             * never takes light, still is). Reported once PER MESH: a
             * session-wide flag let the first offender (the status-menu
             * backplate, drawn before any em_gfx_char_rig) hide later
             * contract violations. */
            if (!m->rig_warned) {
                fprintf(stderr, "gfx: normal-carrying mesh %p drawn without "
                        "a light rig — opaque draw rejected "
                        "(em_gfx_char_rig)\n", (void *)m);
                m->rig_warned = 1;
            }
            opaque = false;
        } else {
            vertex_colors = vertex_lighting_buffer(g, m, palette, bone_count);
            if (!vertex_colors) {
                fprintf(stderr, "gfx: failed to prepare original vertex lighting\n");
                return;
            }
            mode |= 4u;
        }
    }
    /* Without em_lighting colors the shader never reads buffer 5; binding
     * the vertex buffer keeps the indexed range valid. */
    [g->enc setVertexBuffer:(vertex_colors ? vertex_colors : m->vbuf)
                     offset:0 atIndex:5];
    [g->enc setVertexBytes:&mode length:4 atIndex:4];
    [g->enc setVertexBytes:g->fog length:sizeof(g->fog) atIndex:6];
    [g->enc setFragmentBytes:&mode length:4 atIndex:0];
    [g->enc setFragmentBytes:rgba length:16 atIndex:1];
    [g->enc setFragmentBytes:g->spot length:sizeof(g->spot) atIndex:2];
    [g->enc setFragmentBytes:g->fog length:sizeof(g->fog) atIndex:4];
    [g->enc setFragmentBuffer:m->matBuf offset:0 atIndex:5];
    [g->enc setFragmentTexture:m->texArray atIndex:0];
    [g->enc setFragmentSamplerState:g->repeatSampler atIndex:0];
    if (opaque)
        [g->enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                           indexCount:m->opaque_count
                            indexType:MTLIndexTypeUInt32
                          indexBuffer:m->ibuf
                    indexBufferOffset:0];

    /* Glow pass: the trailing EM_GFX_VERT_BILLBOARD triangles, additively
     * blended (Cv = Cs + Cd), depth test on / depth write off — the
     * translation of the engine's aura draws (GS ALPHA FIX=0x80, ZMSK=1).
     * Mode bit 1 tells the fragment shader to skip the alpha-test cutout
     * and emit the texture sample alone. */
    if (m->glow_count && g->glowPipeline) {
        uint32_t glow_mode = (mode & ~8u) | 2u;
        [g->enc setRenderPipelineState:g->glowPipeline];
        [g->enc setDepthStencilState:g->depthGlow];
        [g->enc setVertexBytes:&glow_mode length:4 atIndex:4];
        [g->enc setFragmentBytes:&glow_mode length:4 atIndex:0];
        [g->enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                           indexCount:m->glow_count
                            indexType:MTLIndexTypeUInt32
                          indexBuffer:m->ibuf
                    indexBufferOffset:(NSUInteger)m->opaque_count * 4];
    }
    [vertex_colors release];
}

void em_gfx_draw_skinned_tinted(EmGfx *g, EmGfxMesh *m, const float *viewproj,
                                const float *palette, uint32_t bone_count,
                                const float rgba[4])
{
    /* A GS world frame takes GS draws only: a GPU mesh in it is a fault. */
    if (g && g->gswFrame) { gsw_fail(g, "a skinned mesh in a GS world frame"); return; }
    draw_skinned(g,m,viewproj,palette,bone_count,rgba);
}

/* Select the virtual canvas for subsequent overlay queueing (em_gfx.h —
 * the status screen lays out on 512x448, everything else on the 640x448
 * default; begin_frame resets to the default). */
void em_gfx_overlay_canvas(EmGfx *g, float w, float h)
{
    if (!g || w <= 0.0f || h <= 0.0f) return;
    g->overlayW = w;
    g->overlayH = h;
}

/* Append one overlay vertex to `verts`/`count`: virtual-canvas point ->
 * NDC on the CPU, in the kTestShaderSrc layout (float4 pos + float4
 * color). Capacity is checked by the callers (whole primitives are
 * dropped, never split). */
static void overlay_push_to(EmGfx *g, float *verts, uint32_t *count,
                            float x, float y, const float rgba[4])
{
    float *v = verts + (size_t)*count * 8;
    v[0] = x / g->overlayW * 2.0f - 1.0f;
    v[1] = 1.0f - y / g->overlayH * 2.0f;
    v[2] = 0.0f;
    v[3] = 1.0f;
    v[4] = rgba[0];
    v[5] = rgba[1];
    v[6] = rgba[2];
    v[7] = rgba[3];
    (*count)++;
}

/* overlay_push_to into the standard (alpha-blended) overlay queue. */
static void overlay_push(EmGfx *g, float x, float y, const float rgba[4])
{
    overlay_push_to(g, g->overlayVerts, &g->overlayVertCount, x, y, rgba);
}

/* Queue one overlay rect: two triangles through overlay_push. The pass
 * itself runs in end_frame. */
void em_gfx_overlay_rect(EmGfx *g, float x, float y, float w, float h,
                         const float rgba[4])
{
    if (!g || !rgba ||
        g->overlayVertCount + 6 > EM_GFX_OVERLAY_MAX * 6)
        return;
    overlay_push(g, x,     y,     rgba);   /* tri 1 */
    overlay_push(g, x + w, y,     rgba);
    overlay_push(g, x,     y + h, rgba);
    overlay_push(g, x + w, y,     rgba);   /* tri 2 */
    overlay_push(g, x + w, y + h, rgba);
    overlay_push(g, x,     y + h, rgba);
}

/* Queue one REVERSE-SUBTRACT rect (em_gfx.h — the screen fade's GS
 * ALPHA_2 0xA1/FIX 0x80 blend, out = max(0, dst - rgb)): same NDC
 * vertex path as em_gfx_overlay_rect into the dedicated sub queue
 * (own PSO, flushed last). The color's alpha slot is set to 1 but the
 * blend never reads it (factors ONE/ONE on RGB, dst alpha kept). */
static void overlay_rect_effect(EmGfx *g, float x, float y, float w, float h,
                                 const float rgb[3], bool add, bool before_text)
{
    if (!g || !rgb ||
        g->subVertCount + 6 > EM_GFX_OVERLAY_SUB_MAX * 6)
        return;
    const float rgba[4] = { rgb[0], rgb[1], rgb[2], 1.0f };
    g->subAdd[g->subVertCount / 6] = add;
    g->subBeforeText[g->subVertCount / 6] = before_text;
    float *verts = g->subVerts;
    uint32_t *n  = &g->subVertCount;
    overlay_push_to(g, verts, n, x,     y,     rgba);   /* tri 1 */
    overlay_push_to(g, verts, n, x + w, y,     rgba);
    overlay_push_to(g, verts, n, x,     y + h, rgba);
    overlay_push_to(g, verts, n, x + w, y,     rgba);   /* tri 2 */
    overlay_push_to(g, verts, n, x + w, y + h, rgba);
    overlay_push_to(g, verts, n, x,     y + h, rgba);
}

void em_gfx_overlay_rect_sub(EmGfx *g, float x, float y, float w, float h,
                             const float rgb[3])
{
    overlay_rect_effect(g, x, y, w, h, rgb, false, false);
}

void em_gfx_overlay_rect_sub_before_text(EmGfx *g, float x, float y,
                                        float w, float h, const float rgb[3])
{
    overlay_rect_effect(g, x, y, w, h, rgb, false, true);
}

void em_gfx_overlay_rect_add(EmGfx *g, float x, float y, float w, float h,
                             const float rgb[3])
{
    overlay_rect_effect(g, x, y, w, h, rgb, true, false);
}

/* Queue one annular-arc segment (em_gfx.h — the translation of the
 * engine's 0x60-block arc primitive func_002082B0): CPU-triangulated fan
 * of quads, <= 6 degrees each, through the same overlay vertex path.
 * Angle 0 = up, clockwise on the y-down canvas; the 4 colors interpolate
 * radially (inner->outer) and angularly (start->end) per vertex. */
void em_gfx_overlay_arc4(EmGfx *g, float cx, float cy,
                         float r_in, float r_out, float a0, float a1,
                         const float rgba_is[4], const float rgba_os[4],
                         const float rgba_ie[4], const float rgba_oe[4])
{
    if (!g || !rgba_is || !rgba_os || !rgba_ie || !rgba_oe) return;
    if (a1 <= a0 || r_out <= r_in || r_in < 0.0f) return;
    uint32_t segs = (uint32_t)ceilf((a1 - a0) / 6.0f);
    if (segs < 1) segs = 1;
    if (g->overlayVertCount + segs * 6 > EM_GFX_OVERLAY_MAX * 6)
        return;                       /* over budget: drop whole arc */
    const float deg2rad = 0.01745329252f;
    for (uint32_t i = 0; i < segs; i++) {
        float t0 = (float)i / (float)segs;
        float t1 = (float)(i + 1) / (float)segs;
        float r0 = (a0 + (a1 - a0) * t0) * deg2rad;
        float r1 = (a0 + (a1 - a0) * t1) * deg2rad;
        /* canvas direction for angle a: (sin a, -cos a) — 0 = up, cw */
        float s0 = sinf(r0), c0 = cosf(r0);
        float s1 = sinf(r1), c1 = cosf(r1);
        float ci0[4], co0[4], ci1[4], co1[4];
        for (int k = 0; k < 4; k++) {
            ci0[k] = rgba_is[k] + (rgba_ie[k] - rgba_is[k]) * t0;
            co0[k] = rgba_os[k] + (rgba_oe[k] - rgba_os[k]) * t0;
            ci1[k] = rgba_is[k] + (rgba_ie[k] - rgba_is[k]) * t1;
            co1[k] = rgba_os[k] + (rgba_oe[k] - rgba_os[k]) * t1;
        }
        float i0x = cx + r_in  * s0, i0y = cy - r_in  * c0;
        float o0x = cx + r_out * s0, o0y = cy - r_out * c0;
        float i1x = cx + r_in  * s1, i1y = cy - r_in  * c1;
        float o1x = cx + r_out * s1, o1y = cy - r_out * c1;
        overlay_push(g, i0x, i0y, ci0);   /* tri 1: i0 o0 i1 */
        overlay_push(g, o0x, o0y, co0);
        overlay_push(g, i1x, i1y, ci1);
        overlay_push(g, o0x, o0y, co0);   /* tri 2: o0 o1 i1 */
        overlay_push(g, o1x, o1y, co1);
        overlay_push(g, i1x, i1y, ci1);
    }
}

/* Register one overlay texture slot (em_gfx.h — slot 0 = the UI font
 * sheet, slot 1 = the status-screen decor sheet). RGBA8 rows top-down,
 * copied into a GPU texture. */
int em_gfx_overlay_texture_set(EmGfx *g, int slot, const uint8_t *rgba,
                               uint32_t w, uint32_t h)
{
    if (!g || !g->device || !rgba || !w || !h || slot < 0 || slot > 1)
        return 0;
    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:w
                                    height:h
                                 mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> tex = [g->device newTextureWithDescriptor:td];
    if (!tex) return 0;
    [tex replaceRegion:MTLRegionMake2D(0, 0, w, h)
           mipmapLevel:0
             withBytes:rgba
           bytesPerRow:(NSUInteger)w * 4];
    [g->overlayTex[slot] release];
    g->overlayTex[slot]  = tex;    /* +1 from newTextureWithDescriptor */
    g->overlayTexW[slot] = (float)w;
    g->overlayTexH[slot] = (float)h;
    return 1;
}

/* Append one textured-overlay vertex (NDC pos + color + normalized uv —
 * the kGlyphShaderSrc layout) to `verts`. Capacity checked by the
 * caller; texel UVs normalize against the given slot's texture. */
static void texquad_push(EmGfx *g, float *verts, uint32_t *count, int slot,
                         float x, float y, float u, float v,
                         const float rgba[4])
{
    float *o = verts + (size_t)*count * 12;
    o[0]  = x / g->overlayW * 2.0f - 1.0f;
    o[1]  = 1.0f - y / g->overlayH * 2.0f;
    o[2]  = 0.0f;
    o[3]  = 1.0f;
    o[4]  = rgba[0];
    o[5]  = rgba[1];
    o[6]  = rgba[2];
    o[7]  = rgba[3];
    o[8]  = u / g->overlayTexW[slot];
    o[9]  = v / g->overlayTexH[slot];
    /* 1: sample nearest (em_gfx_overlay_glyph_nearest, font quads only). */
    o[10] = (slot == EM_GFX_OVERLAY_TEX_FONT && g->glyphNearest) ? 1.0f : 0.0f;
    o[11] = 1.0f;
    (*count)++;
}

/* Queue one textured overlay quad into a slot's queue: canvas-space rect
 * sampling the slot's texture at texel UVs (u0,v0)-(u1,v1). `cap` is the
 * queue's quad budget (EM_GFX_OVERLAY_MAX, EM_GFX_DECOR_MAX, or EM_GFX_BACKDROP_MAX for
 * the backdrop queue). */
static void texquad_queue(EmGfx *g, float *verts, uint32_t *count, int slot,
                          uint32_t cap,
                          float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1,
                          const float rgba[4])
{
    if (!g || !rgba || !g->overlayTex[slot] ||
        *count + 6 > cap * 6)
        return;
    texquad_push(g, verts, count, slot, x,     y,     u0, v0, rgba);
    texquad_push(g, verts, count, slot, x + w, y,     u1, v0, rgba);
    texquad_push(g, verts, count, slot, x,     y + h, u0, v1, rgba);
    texquad_push(g, verts, count, slot, x + w, y,     u1, v0, rgba);
    texquad_push(g, verts, count, slot, x + w, y + h, u1, v1, rgba);
    texquad_push(g, verts, count, slot, x,     y + h, u0, v1, rgba);
}

/* Queue one font-slot quad (em_gfx.h). */
void em_gfx_overlay_glyph(EmGfx *g, float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1,
                          const float rgba[4])
{
    if (!g) return;
    texquad_queue(g, g->glyphVerts, &g->glyphVertCount,
                  EM_GFX_OVERLAY_TEX_FONT, EM_GFX_OVERLAY_MAX,
                  x, y, w, h, u0, v0, u1, v1, rgba);
}

void em_gfx_overlay_glyph_nearest(EmGfx *g, int on)
{
    if (g) g->glyphNearest = on != 0;
}

/* Original subtitle markup offsets only the glyph's top edge. */
void em_gfx_overlay_glyph_skew(EmGfx *g, float x, float y, float w, float h,
                              float skew,
                              float u0, float v0, float u1, float v1,
                              const float rgba[4])
{
    if (!g || !rgba || !g->overlayTex[EM_GFX_OVERLAY_TEX_FONT] ||
        g->glyphVertCount + 6 > EM_GFX_OVERLAY_MAX * 6) return;
    float *v = g->glyphVerts;
    uint32_t *n = &g->glyphVertCount;
    int slot = EM_GFX_OVERLAY_TEX_FONT;
    texquad_push(g, v, n, slot, x + skew,     y,     u0, v0, rgba);
    texquad_push(g, v, n, slot, x + w + skew, y,     u1, v0, rgba);
    texquad_push(g, v, n, slot, x,            y + h, u0, v1, rgba);
    texquad_push(g, v, n, slot, x + w + skew, y,     u1, v0, rgba);
    texquad_push(g, v, n, slot, x + w,        y + h, u1, v1, rgba);
    texquad_push(g, v, n, slot, x,            y + h, u0, v1, rgba);
}

/* Queue one UI-decor-slot quad (em_gfx.h). */
void em_gfx_overlay_sprite(EmGfx *g, float x, float y, float w, float h,
                           float u0, float v0, float u1, float v1,
                           const float rgba[4])
{
    em_gfx_overlay_sprite_blend(g,x,y,w,h,u0,v0,u1,v1,rgba,EM_GFX_UI_ALPHA);
}

void em_gfx_overlay_sprite_blend(EmGfx *g,float x,float y,float w,float h,
    float u0,float v0,float u1,float v1,const float rgba[4],EmGfxOverlayBlend blend)
{
    if (!g || blend<EM_GFX_UI_ALPHA || blend>EM_GFX_UI_OPAQUE) return;
    uint32_t start=g->spriteVertCount;
    texquad_queue(g, g->spriteVerts, &g->spriteVertCount,
                  EM_GFX_OVERLAY_TEX_UI, EM_GFX_DECOR_MAX,
                  x, y, w, h, u0, v0, u1, v1, rgba);
    if (g->spriteVertCount!=start) g->spriteBlend[start/6]=(uint8_t)blend;
}

int em_gfx_overlay_triangle(EmGfx *g, const float xy[3][2], const float rgba[3][4],
                            float u, float v, EmGfxOverlayBlend blend)
{
    if (!g || !xy || !rgba || blend < EM_GFX_UI_ALPHA || blend > EM_GFX_UI_OPAQUE ||
        !g->overlayTex[EM_GFX_OVERLAY_TEX_UI] ||
        g->spriteVertCount + 6 > EM_GFX_DECOR_MAX * 6)
        return 0;
    uint32_t start = g->spriteVertCount;
    for (unsigned vertex = 0; vertex < 3; ++vertex)
        texquad_push(g, g->spriteVerts, &g->spriteVertCount, EM_GFX_OVERLAY_TEX_UI,
                     xy[vertex][0], xy[vertex][1], u, v, rgba[vertex]);
    /* The decor queue uses six-vertex records. A zero-area second triangle
     * preserves that grouping and ordering without rasterizing more pixels. */
    for (unsigned vertex = 0; vertex < 3; ++vertex)
        texquad_push(g, g->spriteVerts, &g->spriteVertCount, EM_GFX_OVERLAY_TEX_UI,
                     xy[0][0], xy[0][1], u, v, rgba[0]);
    g->spriteBlend[start / 6] = (uint8_t)blend;
    return 1;
}

/* Queue one BACKDROP quad — UI slot, bottom layer (em_gfx.h). */
void em_gfx_overlay_backdrop(EmGfx *g, float x, float y, float w, float h,
                             float u0, float v0, float u1, float v1,
                             const float rgba[4])
{
    if (!g) return;
    texquad_queue(g, g->backdropVerts, &g->backdropVertCount,
                  EM_GFX_OVERLAY_TEX_UI, EM_GFX_BACKDROP_MAX,
                  x, y, w, h, u0, v0, u1, v1, rgba);
}

/* Queue the full-frame backdrop fill (em_gfx.h). */
void em_gfx_overlay_backdrop_fill(EmGfx *g, const float rgba[4])
{
    if (!g || !rgba) return;
    g->backdropFillOn = true;
    memcpy(g->backdropFill, rgba, sizeof(g->backdropFill));
}

/* Flat-color arc (em_gfx.h). */
void em_gfx_overlay_arc(EmGfx *g, float cx, float cy,
                        float r_in, float r_out, float a0, float a1,
                        const float rgba[4])
{
    em_gfx_overlay_arc4(g, cx, cy, r_in, r_out, a0, a1,
                        rgba, rgba, rgba, rgba);
}


/* Set this frame's forward spot (em_gfx.h "Forward spot term" — a port
 * stand-in). Stored as the fragment-buffer rows the skinned shader
 * consumes; begin_frame resets the enable to 0. Call it BEFORE the
 * frame's skinned draws — the rows bind per draw. */
void em_gfx_spot_light(EmGfx *g, const float pos[3], const float dir[3],
                       const float rgb[3], float range,
                       float cos_inner, float cos_outer)
{
    if (!g || !pos || !dir || !rgb || range <= 0.0f) return;
    g->spot[0]  = pos[0]; g->spot[1]  = pos[1]; g->spot[2]  = pos[2];
    g->spot[3]  = 1.0f;                              /* enable */
    g->spot[4]  = dir[0]; g->spot[5]  = dir[1]; g->spot[6]  = dir[2];
    g->spot[7]  = range;
    g->spot[8]  = cos_inner; g->spot[9] = cos_outer;
    g->spot[10] = 0.0f; g->spot[11] = 0.0f;
    g->spot[12] = rgb[0]; g->spot[13] = rgb[1]; g->spot[14] = rgb[2];
    g->spot[15] = 0.0f;
}

/* Set this frame's distance fog (em_gfx.h "distance fog" — the engine's
 * per-area GS fog record). Stored as the two rows the skinned shader
 * consumes (vertex buffer 6, fragment buffer 4); begin_frame resets the
 * enable to 0, so a scene with no fog record never enables it.
 * `rgb` is the record's fog colour as written to GS FOGCOL by 0021BA80,
 * i.e. 0..255 framebuffer units (NOT the 0..128 modulate scale), so it is
 * stored as channel / 255. near_z/far_z are the record's near/far; the
 * stored coefficients are 0021B920's translation (em_fog_gs.h calls
 * em_packet_chain_0021B920), and the
 * vertex shader evaluates F = A + B * clip_w like the 0023C780 kernel.
 * Applies to the LEVEL and CHARACTER paths; additive glow draws are
 * unaffected. */
void em_gfx_fog(EmGfx *g, float near_z, float far_z, const float rgb[3])
{
    if (!g || !rgb) return;
    float coef[2];
    if (em_fog_gs_coefficients(near_z, far_z, coef) != 0) {
        /* 0021B920's translation faulted: nothing to draw the fog with. */
        fprintf(stderr, "gfx: fog coefficients (0021B920) faulted\n");
        abort();
    }
    g->fog[0] = em_fog_gs_color_unit(rgb[0]);
    g->fog[1] = em_fog_gs_color_unit(rgb[1]);
    g->fog[2] = em_fog_gs_color_unit(rgb[2]);
    g->fog[3] = 1.0f;                                /* enable */
    g->fog[4] = coef[0]; g->fog[5] = coef[1];
    g->fog[6] = 0.0f;    g->fog[7] = 0.0f;
}

void em_gfx_fog_coefficients(EmGfx *g, const float coef[2], const float rgb[3])
{
    if (!g || !coef || !rgb) return;
    if (g->gswFrame) (void)gsw_fogcol(g, rgb);
    g->fog[0] = em_fog_gs_color_unit(rgb[0]);
    g->fog[1] = em_fog_gs_color_unit(rgb[1]);
    g->fog[2] = em_fog_gs_color_unit(rgb[2]);
    g->fog[3] = 1.0f;                                /* enable */
    g->fog[4] = coef[0]; g->fog[5] = coef[1];
    g->fog[6] = 0.0f;    g->fog[7] = 0.0f;
}

/* Disable distance fog for subsequent draws. Zeroing the enable makes the
 * shader skip the blend entirely, so fog-off frames run the exact pre-fog
 * arithmetic. */
void em_gfx_fog_off(EmGfx *g)
{
    if (!g) return;
    memset(g->fog, 0, sizeof(g->fog));
}

/* --- Level background (em_gfx.h; em_background_gs.h has the original) -- */

void em_gfx_background_unload(EmGfx *g)
{
    if (!g) return;
    [g->bgTexture release];
    g->bgTexture = nil;
    free(g->bgFile);
    g->bgFile = NULL;
    memset(&g->bgAsset, 0, sizeof g->bgAsset);
}

int em_gfx_background_ready(EmGfx *g)
{
    return g && g->bgTexture ? 1 : 0;
}

int em_gfx_background_state(EmGfx *g, uint64_t *tex0, uint32_t *rgbaq)
{
    if (!g || !g->bgTexture || !tex0 || !rgbaq) return 0;
    *tex0 = g->bgAsset.tex0;
    *rgbaq = g->bgAsset.rgbaq;
    return 1;
}

int em_gfx_background_load(EmGfx *g, const char *path)
{
    if (!g || !g->device || !path) return -1;
    em_gfx_background_unload(g);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "background: %s: cannot open\n", path);
        return -1;
    }
    uint8_t *buf = NULL;
    long len = -1;
    if (fseek(f, 0, SEEK_END) == 0) len = ftell(f);
    if (len > 0 && len <= (long)(EM_BACKGROUND_GS_HEADER + 1024u * 1024u * 4u)
        && fseek(f, 0, SEEK_SET) == 0) {
        buf = (uint8_t *)malloc((size_t)len);
        if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
            free(buf);
            buf = NULL;
        }
    }
    fclose(f);
    EmBackgroundGsAsset asset;
    int parsed = buf ? em_background_gs_parse(buf, (size_t)len, &asset) : -1;
    if (parsed != 0) {
        fprintf(stderr, "background: %s: not a version-2 EMBG asset (%d)\n",
                path, parsed);
        free(buf);
        return -2;
    }
    const char *why = em_background_gs_unsupported(&asset);
    if (why) {
        fprintf(stderr, "background: %s: GS state not reproduced: %s\n",
                path, why);
        free(buf);
        return -3;
    }
    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:asset.tex_w
                                    height:asset.tex_h
                                 mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> tex = [g->device newTextureWithDescriptor:td];
    if (!tex) {
        free(buf);
        return -4;
    }
    [tex replaceRegion:MTLRegionMake2D(0, 0, asset.tex_w, asset.tex_h)
           mipmapLevel:0
             withBytes:asset.rgba
           bytesPerRow:(NSUInteger)asset.tex_w * 4];
    g->bgFile = buf;
    g->bgAsset = asset;
    g->bgTexture = tex;          /* +1 from newTextureWithDescriptor */
    return 0;
}

int em_gfx_background_prims_env(EmGfx *g, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count)
{
    if (g && g->gswFrame) return prims ? gsw_background(g, prims, envs, count) : gsw_fail(g, "no primitives");
    return em_gfx_background_prims(g, prims, count);
}

int em_gfx_background_prims(EmGfx *g, const EmGfxGsPrim *prims, uint32_t count)
{
    if (!g || !prims) return -1;
    if (!g->bgTexture) {
        fprintf(stderr, "background: the channel-3 list drew %u triangle(s) with no background asset loaded\n",
                (unsigned)count);
        return -1;
    }
    /* Every primitive must be one the asset's GS state describes: a
     * triangle of the kicked strips (PRIM as the asset's, the list's TEX0,
     * CLAMP_1, TEX1_1 and TEST_1 equal to the asset's, which
     * em_background_gs_unsupported accepted), the RGBAQ colour the list
     * sent and its Q 1.0 (the ST interpolate affinely). */
    const EmBackgroundGsAsset *a = &g->bgAsset;
    const uint32_t need = EM_GFX_GS_TEX0 | EM_GFX_GS_CLAMP | EM_GFX_GS_TEX1 | EM_GFX_GS_TEST;
    for (uint32_t i = 0; i < count; ++i) {
        const EmGfxGsPrim *p = &prims[i];
        const char *why = NULL;
        if (p->count != 3u || p->prim != a->prim) why = "not a triangle of the asset's PRIM";
        else if ((p->set & need) != need) why = "a drawing register the list never wrote";
        else if (p->tex0 != a->tex0 || p->clamp != a->clamp1 || p->tex1 != a->tex1 || p->test != a->test1)
            why = "TEX0 / CLAMP_1 / TEX1_1 / TEST_1 other than the asset's";
        for (uint32_t k = 0; !why && k < 3u; ++k) {
            const EmGfxGsVertex *v = &p->v[k];
            const uint32_t rgba = (uint32_t)v->rgba[0] | (uint32_t)v->rgba[1] << 8 | (uint32_t)v->rgba[2] << 16 |
                                  (uint32_t)v->rgba[3] << 24;
            if (rgba != a->rgbaq || v->q != 0x3F800000u) why = "an RGBAQ other than the asset's, or Q != 1.0";
        }
        if (why) {
            fprintf(stderr, "background: primitive %u refused: %s\n", (unsigned)i, why);
            return -1;
        }
    }
    if (!count || !g->enc) return 0;
    if (!g->bgPipeline)
        g->bgPipeline = build_pipeline(g, kBackgroundShaderSrc,
            @"v_background", @"f_background", EM_BLEND_OPAQUE);
    if (!g->bgPipeline) return -1;
    if (!g->clampSampler) {
        /* CLAMP_1 WMS/WMT CLAMP, TEX1 MMAG/MMIN LINEAR (the unsupported
         * check refused every other state). */
        MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
        sd.minFilter = MTLSamplerMinMagFilterLinear;
        sd.magFilter = MTLSamplerMinMagFilterLinear;
        sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        g->clampSampler = [g->device newSamplerStateWithDescriptor:sd];
        [sd release];
        if (!g->clampSampler) return -1;
    }
    ensure_depth_states(g);
    /* GS XY 12.4 onto NDC by the GS pixel-footprint convention of the
     * port's world projection (em_background_gs_ndc), ST the register words. */
    const size_t bytes = (size_t)count * 3u * 8u * sizeof(float);
    float *verts = malloc(bytes);
    if (!verts) return -1;
    for (uint32_t i = 0; i < count; ++i)
        for (uint32_t k = 0; k < 3u; ++k) {
            const EmGfxGsVertex *v = &prims[i].v[k];
            float *o = verts + (3u * i + k) * 8u;
            const uint16_t xy[2] = {v->x, v->y};
            em_background_gs_ndc(xy, o);
            o[2] = 0.0f;
            o[3] = 1.0f;
            memcpy(&o[4], &v->s, 4);
            memcpy(&o[5], &v->t, 4);
            o[6] = 0.0f;
            o[7] = 0.0f;
        }
    id<MTLBuffer> buffer = [g->device newBufferWithBytes:verts length:bytes
                                                 options:MTLResourceStorageModeShared];
    free(verts);
    if (!buffer) return -1;
    const uint32_t q = a->rgbaq;
    const float rgbaq[4] = {
        (float)(q & 0xffu) / 128.0f, (float)(q >> 8 & 0xffu) / 128.0f,
        (float)(q >> 16 & 0xffu) / 128.0f, (float)(q >> 24 & 0xffu) / 128.0f,
    };
    [g->enc setRenderPipelineState:g->bgPipeline];
    [g->enc setDepthStencilState:g->depthOff];
    [g->enc setCullMode:MTLCullModeNone];
    [g->enc setVertexBuffer:buffer offset:0 atIndex:0];
    [g->enc setFragmentTexture:g->bgTexture atIndex:0];
    [g->enc setFragmentSamplerState:g->clampSampler atIndex:0];
    [g->enc setFragmentBytes:rgbaq length:sizeof rgbaq atIndex:0];
    [g->enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:(NSUInteger)count * 3u];
    [buffer release];
    return 0;
}

/* Set the character light rig consumed by subsequent skinned draws
 * (em_gfx.h "Character light rig" — the engine's per-actor VU1 light
 * matrix). Stored as the seven rows vertex_lighting_buffer feeds to
 * em_lighting; NULL (or begin_frame) zeroes the enable, after which a
 * normal-carrying draw is rejected (no stand-in light). The rows bind per draw —
 * em_game sets a fresh rig before each actor draw of the close-out
 * flush, exactly like the engine rebuilding the matrix per actor. */
void em_gfx_char_rig(EmGfx *g, const EmGfxCharRig *rig)
{
    if (!g) return;
    memset(g->face_rig, 0, sizeof(g->face_rig));
    if (!rig) {
        memset(g->rig, 0, sizeof(g->rig));
        return;
    }
    memcpy(&g->rig[0],  rig->dir, sizeof(rig->dir));
    memcpy(&g->rig[12], rig->col, sizeof(rig->col));
    memcpy(&g->rig[24], rig->amb, sizeof(rig->amb));
    g->rig[27] = 1.0f;                               /* enable */
}

void em_gfx_char_face_rig(EmGfx *g, const EmGfxCharRig *rig)
{
    if (!g) return;
    memset(g->face_rig, 0, sizeof(g->face_rig));
    if (!rig) return;
    memcpy(g->face_rig, rig->dir, sizeof(rig->dir));
    memcpy(g->face_rig+12, rig->col, sizeof(rig->col));
    memcpy(g->face_rig+24, rig->amb, sizeof(rig->amb));
    g->face_rig[27] = 1.0f;
}


/* Flush the queued overlay primitives (rects + arcs): one draw at the
 * END of the open render pass (post-3D, so the HUD composites over the
 * scene), depth test OFF, standard alpha blend. Reuses the
 * position+color passthrough pipeline (kTestShaderSrc, runtime-compiled)
 * — overlay vertices are pre-converted NDC, exactly that shader's input.
 * The vertex data can exceed Metal's 4 KB setVertexBytes ceiling (the
 * status screen is hundreds of quads), so it goes through a per-flush
 * MTLBuffer; the command buffer retains it until the GPU is done, so
 * releasing right after the draw is safe. */
static void overlay_flush(EmGfx *g)
{
    uint32_t verts = g->overlayVertCount;
    g->overlayVertCount = 0;
    if (!verts || !g->enc) return;
    if (!g->testPipeline) {
        g->testPipeline = build_pipeline(g, kTestShaderSrc, @"v_main",
                                         @"f_main", EM_BLEND_ALPHA);
        if (!g->testPipeline) return;
    }
    ensure_depth_states(g);
    id<MTLBuffer> vbuf =
        [g->device newBufferWithBytes:g->overlayVerts
                               length:(NSUInteger)verts * 8 * sizeof(float)
                              options:MTLResourceStorageModeShared];
    if (!vbuf) return;
    [g->enc setRenderPipelineState:g->testPipeline];
    [g->enc setDepthStencilState:g->depthOff];
    [g->enc setCullMode:MTLCullModeNone];
    [g->enc setVertexBuffer:vbuf offset:0 atIndex:0];
    [g->enc drawPrimitives:MTLPrimitiveTypeTriangle
               vertexStart:0
               vertexCount:(NSUInteger)verts];
    [vbuf release];   /* the in-flight command buffer keeps it alive */
}

/* Letterbox effects precede text; full-screen effects follow it. Within
 * either layer preserve mixed subtract/add order at the color clamps. */
static void overlay_sub_flush(EmGfx *g, bool before_text)
{
    uint32_t verts = g->subVertCount;
    if (!before_text) g->subVertCount = 0;
    if (!verts || !g->enc) return;
    ensure_depth_states(g);
    /* Small queue (EM_GFX_OVERLAY_SUB_MAX = 16 quads = 3 KB) — fits
     * under Metal's 4 KB setVertexBytes ceiling, no MTLBuffer needed. */
    [g->enc setDepthStencilState:g->depthOff];
    [g->enc setCullMode:MTLCullModeNone];
    [g->enc setVertexBytes:g->subVerts
                    length:(NSUInteger)verts * 8 * sizeof(float)
                   atIndex:0];
    /* Preserve original packet order for mixed black/white effects:
     * subtract then add is different from add then subtract at clamps. */
    for (uint32_t start = 0; start < verts;) {
        if (g->subBeforeText[start / 6] != before_text) {
            start += 6;
            continue;
        }
        bool add = g->subAdd[start / 6];
        uint32_t end = start + 6;
        while (end < verts && g->subAdd[end / 6] == add &&
               g->subBeforeText[end / 6] == before_text) end += 6;
        id<MTLRenderPipelineState> *pipeline =
            add ? &g->addOverlayPipeline : &g->subPipeline;
        if (!*pipeline)
            *pipeline = build_pipeline(g, kTestShaderSrc, @"v_main",
                                        @"f_main", add ? EM_BLEND_ADD : EM_BLEND_RSUB);
        if (!*pipeline) return;
        [g->enc setRenderPipelineState:*pipeline];
        [g->enc drawPrimitives:MTLPrimitiveTypeTriangle
                   vertexStart:(NSUInteger)start
                   vertexCount:(NSUInteger)(end - start)];
        start = end;
    }
}

/* Flush one slot's queued TEXTURED overlay quads: one draw after
 * overlay_flush — same pass position (post-3D, depth off, standard
 * alpha blend). Linear clamp sampling = the GS UI-sprite state (TEX1
 * MMAG/MMIN=1). Called for the UI-decor slot first, the font slot last
 * (decor under text — see em_gfx.h). */
static void texquad_flush(EmGfx *g, int slot, float *verts_data,
                          uint32_t *count,const uint8_t *blend_modes)
{
    uint32_t verts = *count;
    *count = 0;
    if (!verts || !g->enc || !g->overlayTex[slot]) return;
    if (!g->glyphPipeline) {
        g->glyphPipeline = build_pipeline(g, kGlyphShaderSrc, @"v_glyph",
                                          @"f_glyph", EM_BLEND_ALPHA);
        if (!g->glyphPipeline) return;
    }
    if (!g->clampSampler) {
        MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
        sd.minFilter    = MTLSamplerMinMagFilterLinear;
        sd.magFilter    = MTLSamplerMinMagFilterLinear;
        sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        g->clampSampler = [g->device newSamplerStateWithDescriptor:sd];
        [sd release];
        if (!g->clampSampler) return;
    }
    ensure_depth_states(g);
    id<MTLBuffer> vbuf =
        [g->device newBufferWithBytes:verts_data
                               length:(NSUInteger)verts * 12 * sizeof(float)
                              options:MTLResourceStorageModeShared];
    if (!vbuf) return;
    [g->enc setDepthStencilState:g->depthOff];
    [g->enc setCullMode:MTLCullModeNone];
    [g->enc setVertexBuffer:vbuf offset:0 atIndex:0];
    [g->enc setFragmentTexture:g->overlayTex[slot] atIndex:0];
    [g->enc setFragmentSamplerState:g->clampSampler atIndex:0];
    for (uint32_t start=0;start<verts;) {
        unsigned mode=blend_modes ? blend_modes[start/6] : EM_GFX_UI_ALPHA;
        uint32_t end=start+6;
        while (end<verts && (!blend_modes || blend_modes[end/6]==mode)) end+=6;
        id<MTLRenderPipelineState> *pipeline=&g->glyphPipeline;
        EmBlendMode blend=EM_BLEND_ALPHA;
        NSString *fragment=@"f_glyph";
        if (mode==EM_GFX_UI_ADD) {pipeline=&g->spriteAddPipeline;blend=EM_BLEND_ADD;}
        else if (mode==EM_GFX_UI_SUBTRACT) {
            pipeline=&g->spriteSubPipeline;blend=EM_BLEND_RSUB;fragment=@"f_glyph_opaque";
        }
        else if (mode==EM_GFX_UI_OPAQUE) {
            pipeline=&g->spriteOpaquePipeline;blend=EM_BLEND_OPAQUE;fragment=@"f_glyph_opaque";
        }
        if (!*pipeline) *pipeline=build_pipeline(g,kGlyphShaderSrc,@"v_glyph",fragment,blend);
        if (!*pipeline) break;
        [g->enc setRenderPipelineState:*pipeline];
        [g->enc drawPrimitives:MTLPrimitiveTypeTriangle
                   vertexStart:(NSUInteger)start
                   vertexCount:(NSUInteger)(end-start)];
        start=end;
    }
    [vbuf release];   /* the in-flight command buffer keeps it alive */
}

/* Flush the backdrop layer — the BOTTOM of the overlay sequence (the
 * animated UI background under the panels, em_gfx.h): the optional
 * full-frame solid fill (the black frame; one quad through
 * the untextured overlay pipeline, NDC-direct so it covers the whole
 * drawable regardless of the selected canvas) and then the queued
 * UI-slot backdrop quads (texquad_flush — same pipeline/state as the
 * decor sprites). Runs before overlay_flush. */
static void backdrop_flush(EmGfx *g)
{
    bool fill = g->backdropFillOn;
    g->backdropFillOn = false;
    if (fill && g->enc) {
        if (!g->testPipeline)
            g->testPipeline = build_pipeline(g, kTestShaderSrc, @"v_main",
                                             @"f_main", EM_BLEND_ALPHA);
        if (g->testPipeline) {
            ensure_depth_states(g);
            /* full-frame quad, pre-converted NDC (kTestShaderSrc layout:
             * float4 pos + float4 color per vertex) */
            float v[6 * 8];
            static const float corner[6][2] = {
                { -1.0f,  1.0f }, { 1.0f,  1.0f }, { -1.0f, -1.0f },
                {  1.0f,  1.0f }, { 1.0f, -1.0f }, { -1.0f, -1.0f },
            };
            for (int i = 0; i < 6; i++) {
                float *o = v + i * 8;
                o[0] = corner[i][0];
                o[1] = corner[i][1];
                o[2] = 0.0f;
                o[3] = 1.0f;
                memcpy(o + 4, g->backdropFill, 4 * sizeof(float));
            }
            [g->enc setRenderPipelineState:g->testPipeline];
            [g->enc setDepthStencilState:g->depthOff];
            [g->enc setCullMode:MTLCullModeNone];
            [g->enc setVertexBytes:v length:sizeof(v) atIndex:0];
            [g->enc drawPrimitives:MTLPrimitiveTypeTriangle
                       vertexStart:0
                       vertexCount:6];
        }
    }
    texquad_flush(g, EM_GFX_OVERLAY_TEX_UI,
                  g->backdropVerts, &g->backdropVertCount,NULL);
}

/* The ordered 2D layer (em_gfx.h): the backdrop drawn now, between 3D
 * draws; backdrop_flush empties its queues, so end_frame's call finds
 * nothing left. */
void em_gfx_overlay_backdrop_flush(EmGfx *g)
{
    if (!g || !g->enc) return;
    backdrop_flush(g);
}

/* The ordered 2D layer so far (em_gfx.h): the backdrop, then the decor
 * queue with its per-record blends; end_frame then finds them empty. */
void em_gfx_overlay_decor_flush(EmGfx *g)
{
    if (!g || !g->enc) return;
    backdrop_flush(g);
    texquad_flush(g, EM_GFX_OVERLAY_TEX_UI, g->spriteVerts, &g->spriteVertCount, g->spriteBlend);
}

/* The game frame rect begin_frame set (viewport and scissor), in drawable
 * pixels. */
static void game_frame_rect(EmGfx *g, double *vx, double *vy, double *vw, double *vh)
{
    double dw = (double)g->target.width, dh = (double)g->target.height;
    *vw = dw; *vh = dh; *vx = 0.0; *vy = 0.0;
    if (dw * 3.0 >= dh * 4.0) { *vw = dh * 4.0 / 3.0; *vx = (dw - *vw) * 0.5; }
    else                      { *vh = dw * 3.0 / 4.0; *vy = (dh - *vh) * 0.5; }
}

void em_gfx_draw_scissor(EmGfx *g, const float rect[4])
{
    if (!g || !g->enc || !g->target) return;
    double vx, vy, vw, vh;
    game_frame_rect(g, &vx, &vy, &vw, &vh);
    if (!rect || g->overlayW <= 0.0f || g->overlayH <= 0.0f) { /* begin_frame's rect */
        [g->enc setScissorRect:(MTLScissorRect){(NSUInteger)vx, (NSUInteger)vy, (NSUInteger)vw,
                                                (NSUInteger)vh}];
        return;
    }
    double x0 = vx, y0 = vy, x1 = vx + vw, y1 = vy + vh;
    {
        const double sx = vw / g->overlayW, sy = vh / g->overlayH;
        double rx0 = vx + rect[0] * sx, ry0 = vy + rect[1] * sy;
        double rx1 = vx + rect[2] * sx, ry1 = vy + rect[3] * sy;
        x0 = rx0 > x0 ? rx0 : x0; y0 = ry0 > y0 ? ry0 : y0;
        x1 = rx1 < x1 ? rx1 : x1; y1 = ry1 < y1 ? ry1 : y1;
    }
    NSUInteger ix0 = (NSUInteger)(x0 + 0.5), iy0 = (NSUInteger)(y0 + 0.5);
    NSUInteger ix1 = x1 > x0 ? (NSUInteger)(x1 + 0.5) : ix0, iy1 = y1 > y0 ? (NSUInteger)(y1 + 0.5) : iy0;
    /* Metal refuses an empty rect; the callers skip their draws for an
     * empty window, so this only keeps the encoder valid. */
    if (ix1 <= ix0) ix1 = ix0 + 1;
    if (iy1 <= iy0) iy1 = iy0 + 1;
    if (ix1 > g->target.width) { ix1 = g->target.width; if (ix0 >= ix1) ix0 = ix1 - 1; }
    if (iy1 > g->target.height) { iy1 = g->target.height; if (iy0 >= iy1) iy0 = iy1 - 1; }
    [g->enc setScissorRect:(MTLScissorRect){ix0, iy0, ix1 - ix0, iy1 - iy0}];
}

/* --- Player drop shadow (em_gfx.h, em_shadow_gs.h) ----------------------- */

/* Shadow shaders. v_shadow_world: world position through the frame's
 * viewproj (the box faces). v_shadow_ndc: a position already in clip
 * space (the 001DA290 strip, the silhouette's GS 12.4 vertices).
 * f_shadow_alpha writes only the source alpha (the pipeline masks RGB):
 * ALPHA 0xA9 with FIX 0x80 keeps Cd and AFAIL FB_ONLY writes As.
 * f_shadow_silhouette is the flat RGBAQ 0xFFFFFF80. */
static NSString *const kShadowShaderSrc =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct VOut { float4 pos [[position]]; };\n"
"vertex VOut v_shadow_world(uint vid [[vertex_id]],\n"
"                           const device float4 *p [[buffer(0)]],\n"
"                           constant float4x4 &viewproj [[buffer(1)]]) {\n"
"    VOut o; o.pos = viewproj * float4(p[vid].xyz, 1.0); return o;\n"
"}\n"
"vertex VOut v_shadow_ndc(uint vid [[vertex_id]],\n"
"                         const device float4 *p [[buffer(0)]]) {\n"
"    VOut o; o.pos = p[vid]; return o;\n"
"}\n"
"fragment float4 f_shadow_alpha(VOut in [[stage_in]],\n"
"                               constant float &alpha [[buffer(0)]]) {\n"
"    return float4(0.0, 0.0, 0.0, alpha);\n"
"}\n"
"fragment float4 f_shadow_silhouette(VOut in [[stage_in]]) {\n"
"    return float4(128.0 / 255.0, 1.0, 1.0, 1.0);\n"
"}\n";

enum {
    SHADOW_WARN_FRAME = 1u, SHADOW_WARN_FETCH = 2u, SHADOW_WARN_INPUT = 4u,
    SHADOW_WARN_STALE = 8u, SHADOW_WARN_CLIP = 16u, SHADOW_WARN_TARGETS = 32u,
    SHADOW_WARN_FOG = 64u, SHADOW_WARN_ORDER = 128u, SHADOW_WARN_GPU = 256u,
};

static int shadow_fail(EmGfx *g, uint32_t why, const char *what)
{
    if (g && !(g->shadowWarned & why)) {
        g->shadowWarned |= why;
        fprintf(stderr, "gfx: shadow: %s — not drawn\n", what);
    }
    return -1;
}

/* A pipeline for the shadow passes: colour format `fmt`, depth attachment
 * when `depth`, colour write mask `mask`, blending off. +1 retained. */
static id<MTLRenderPipelineState> shadow_pipeline(EmGfx *g, NSString *src,
    NSString *vfn, NSString *ffn, MTLPixelFormat fmt, bool depth,
    MTLColorWriteMask mask)
{
    NSError *err = nil;
    id<MTLLibrary> lib = [g->device newLibraryWithSource:src options:nil
                                                   error:&err];
    if (!lib) {
        fprintf(stderr, "metal: shadow shader compile failed: %s\n",
                err ? err.localizedDescription.UTF8String : "(unknown)");
        return nil;
    }
    id<MTLFunction> v = [lib newFunctionWithName:vfn];
    id<MTLFunction> f = [lib newFunctionWithName:ffn];
    MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = v;
    pd.fragmentFunction = f;
    pd.colorAttachments[0].pixelFormat = fmt;
    pd.colorAttachments[0].blendingEnabled = NO;
    pd.colorAttachments[0].writeMask = mask;
    if (depth) pd.depthAttachmentPixelFormat = EM_DEPTH_FORMAT;
    id<MTLRenderPipelineState> pso =
        [g->device newRenderPipelineStateWithDescriptor:pd error:&err];
    [pd release];
    [v release];
    [f release];
    [lib release];
    if (!pso)
        fprintf(stderr, "metal: shadow pipeline build failed: %s\n",
                err ? err.localizedDescription.UTF8String : "(unknown)");
    return pso;
}

static bool shadow_alpha_ready(EmGfx *g)
{
    if (!g->shadowAlphaPipeline)
        g->shadowAlphaPipeline = shadow_pipeline(g, kShadowShaderSrc,
            @"v_shadow_world", @"f_shadow_alpha", g->layer.pixelFormat, true,
            MTLColorWriteMaskAlpha);
    ensure_depth_states(g);
    return g->shadowAlphaPipeline != nil;
}

int em_gfx_shadow_alpha_clear(EmGfx *g)
{
    if (g && g->gswFrame) return gsw_alpha_clear(g);
    if (!g || !g->enc) return shadow_fail(g, SHADOW_WARN_FRAME, "outside a frame");
    if (!shadow_alpha_ready(g))
        return shadow_fail(g, SHADOW_WARN_GPU, "alpha pipeline unavailable");
    /* The 001DA290 strip covers the whole field: the 4:3 game frame. */
    static const float quad[6][4] = {
        { -1.0f,  1.0f, 0.0f, 1.0f }, { 1.0f,  1.0f, 0.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f,  1.0f, 0.0f, 1.0f },
        {  1.0f, -1.0f, 0.0f, 1.0f }, { -1.0f, -1.0f, 0.0f, 1.0f },
    };
    static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0,
                                     0, 0, 1, 0, 0, 0, 0, 1 };
    const float alpha = 0.0f;                    /* RGBAQ A of the strip */
    [g->enc setRenderPipelineState:g->shadowAlphaPipeline];
    [g->enc setDepthStencilState:g->depthOff];   /* Z 0xFFFFFFFF GEQUAL */
    [g->enc setCullMode:MTLCullModeNone];
    [g->enc setVertexBytes:quad length:sizeof quad atIndex:0];
    [g->enc setVertexBytes:ident length:sizeof ident atIndex:1];
    [g->enc setFragmentBytes:&alpha length:4 atIndex:0];
    [g->enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    return 0;
}

static float object_depth(uint32_t z);

/* A kicked vertex's position as the static world's and the object units'
 * triangles take it (em_gfx_gs_opaque, em_gfx_object_unit): GS X / Y (12.4)
 * through em_background_gs_ndc, Z (24 bits) through object_depth, w = 1.
 * The shadow passes position their kicked words the same way, so the box
 * and the receivers meet the level's depth exactly as the GS compares them
 * (a receiver lies on the level surface it shades: same words, same
 * depth). */
static void shadow_gs_position(uint32_t x16, uint32_t y16, uint32_t z24, float out[4])
{
    const uint16_t xy[2] = { (uint16_t)x16, (uint16_t)y16 };
    em_background_gs_ndc(xy, out);
    out[2] = object_depth(z24 & 0xFFFFFFu);
    out[3] = 1.0f;
}

/* The clip kernels on the CPU: one result buffer per call. */
static EmVu1ClipResult *shadow_clip_result(void)
{
    return malloc(sizeof(EmVu1ClipResult));
}

/* Run clip kernel `kernel` (em_vu1_shadow_clip.h) on one batch and append
 * the triangles it kicks, positioned from their GS words
 * (shadow_gs_position), with their ST, A and F. Returns the vertex count
 * appended, or -1 (the kernel faulted, a data word names a matrix other
 * than dmem 0, an undecodable packet, more than `cap` vertices). */
static int shadow_clip_batch(int kernel, const float cam[16], const float *st,
                             const float k1021[4], const float (*qw3)[4],
                             EmVu1Qword *dmem, EmVu1ClipResult *res,
                             EmShadowGsClipVertex *gv, uint32_t cap,
                             float (*pos)[4])
{
    if (em_shadow_gs_clip_dmem(kernel, cam, st, k1021, qw3, EM_SHADOW_GS_CLIP_TOP, dmem))
        return -1;
    if (em_vu1_shadow_clip_run(kernel, dmem, EM_SHADOW_GS_CLIP_TOP, res)) return -1;
    const int n = em_shadow_gs_clip_vertices(kernel, res, gv, cap);
    if (n < 0 || n % 3) return -1;
    /* A triangle whose three kicked vertices share one GS X / Y (the
     * kernel's collapse of a triangle wholly outside a screen plane onto
     * (2048, 2048)) has no area: the GS draws no pixel of it, so it is not
     * unprojected or drawn. */
    int kept = 0;
    for (int t = 0; t < n; t += 3) {
        if (gv[t].x == gv[t + 1].x && gv[t].x == gv[t + 2].x &&
            gv[t].y == gv[t + 1].y && gv[t].y == gv[t + 2].y)
            continue;
        for (int c = 0; c < 3; ++c) {
            const EmShadowGsClipVertex v = gv[t + c];
            /* x / y hold the 12.4 words / 16: exact */
            shadow_gs_position((uint32_t)(v.x * 16.0f), (uint32_t)(v.y * 16.0f), v.z, pos[kept]);
            gv[kept++] = v;
        }
    }
    return kept;
}

int em_gfx_shadow_box(EmGfx *g, const EmGfxShadowStrips *model,
                      const float world[16], const float clip[16],
                      uint32_t rgbaq, const float viewproj[16])
{
    if (g && g->gswFrame) {
        if (!model || !model->qw3 || !model->vertex_count || model->vertex_count % EM_GFX_SHADOW_BATCH || !clip)
            return gsw_fail(g, "box input missing");
        return gsw_box(g, model, clip, rgbaq);
    }
    if (!g || !g->enc) return shadow_fail(g, SHADOW_WARN_FRAME, "outside a frame");
    if (!model || !model->qw3 || !model->vertex_count ||
        model->vertex_count % EM_GFX_SHADOW_BATCH || !world || !clip || !viewproj)
        return shadow_fail(g, SHADOW_WARN_INPUT, "box input missing");
    if (!shadow_alpha_ready(g))
        return shadow_fail(g, SHADOW_WARN_GPU, "alpha pipeline unavailable");
    /* The fog row only reaches the F of the kicked vertices, which PRIM
     * 0x044 / 0x043 (FGE 0) never uses. */
    float k1021[4] = { 255.0f, 2048.0f, 0.0f, 0.0f }, k1022[4], k1023[4];
    em_shadow_gs_level_rows(k1022, k1023);
    const uint32_t n = model->vertex_count;
    const uint32_t batches = n / EM_GFX_SHADOW_BATCH;
    const uint32_t clip_cap = 30u * 27u;          /* per batch */
    EmShadowGsVertex *out = malloc(sizeof *out * n);
    float (*tri)[4] = malloc(sizeof *tri * (3 * n + clip_cap * batches));
    EmVu1Qword *dmem = malloc(sizeof *dmem * EM_VU1_DMEM_QWORDS);
    EmVu1ClipResult *res = shadow_clip_result();
    EmShadowGsClipVertex *gv = malloc(sizeof *gv * clip_cap);
    float (*cp)[4] = malloc(sizeof *cp * clip_cap);
    if (!out || !tri || !dmem || !res || !gv || !cp) {
        free(out); free(tri); free(dmem); free(res); free(gv); free(cp);
        return shadow_fail(g, SHADOW_WARN_INPUT, "out of memory");
    }
    const float (*qw3)[4] = (const float (*)[4])model->qw3;
    uint32_t count = 0;
    int bad = 0;
    (void)world;   /* the kicked words carry the positions (shadow_gs_position) */
    for (uint32_t b = 0; b < n && !bad; b += EM_GFX_SHADOW_BATCH) {
        if (em_shadow_gs_level_batch(clip, k1021, k1022, k1023, qw3 + b,
                                     EM_GFX_SHADOW_BATCH, out + b))
            bad = 1;                     /* EM_SHADOW_GS_ADC_STALE */
        for (uint32_t i = b + 2; i < b + EM_GFX_SHADOW_BATCH && !bad; ++i) {
            /* 00237180 kicks only vertices without ADC; the triangles it
             * leaves for CLIP are 00239C90's (below). */
            if (out[i].why) continue;
            for (unsigned k = 0; k < 3; ++k) {
                const EmShadowGsVertex *v = &out[i - 2 + k];
                shadow_gs_position((uint32_t)v->w[0] & 0xFFFFu, (uint32_t)v->w[1] & 0xFFFFu,
                                   (uint32_t)v->w[2] >> 4, tri[count]);
                ++count;
            }
        }
    }
    free(out);
    /* 001DA310 runs 00239C90 after the box, over the same batches: its
     * triangles, positioned from their kicked words. */
    for (uint32_t b = 0; b < n && !bad; b += EM_GFX_SHADOW_BATCH) {
        const int v = shadow_clip_batch(EM_VU1_CLIP_BOX, clip, NULL, k1021, qw3 + b,
                                        dmem, res, gv, clip_cap, cp);
        if (v < 0) { bad = 2; break; }
        for (int k = 0; k < v; ++k) memcpy(tri[count++], cp[k], sizeof cp[k]);
    }
    free(dmem); free(res); free(gv); free(cp);
    if (bad) {
        free(tri);
        return shadow_fail(g, bad == 1 ? SHADOW_WARN_STALE : SHADOW_WARN_CLIP,
                           bad == 1 ? "box strip starts without ADC (kernel "
                                      "state not modelled)"
                                    : "box clip kernel 00239C90 fault (an FTOI "
                                      "outside int32, a data word naming "
                                      "another matrix, a singular camera)");
    }
    if (count) {
        id<MTLBuffer> vb = [g->device newBufferWithBytes:tri
                                                  length:sizeof *tri * count
                                                 options:MTLResourceStorageModeShared];
        const float alpha = (float)(rgbaq >> 24) / 255.0f;
        [g->enc setRenderPipelineState:g->shadowAlphaPipeline];
        [g->enc setDepthStencilState:g->depthGlow];  /* GEQUAL, ZMSK 1 */
        [g->enc setCullMode:MTLCullModeNone];
        [g->enc setVertexBuffer:vb offset:0 atIndex:0];
        static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0,
                                         0, 0, 1, 0, 0, 0, 0, 1 };
        [g->enc setVertexBytes:ident length:64 atIndex:1];   /* positions are NDC */
        [g->enc setFragmentBytes:&alpha length:4 atIndex:0];
        [g->enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0
                   vertexCount:count];
        [vb release];
    }
    free(tri);
    return 0;
}

int em_gfx_shadow_silhouette(EmGfx *g, const float *verts, uint32_t vert_count,
                             const uint32_t *indices, uint32_t index_count,
                             const float *nodes, uint32_t node_count,
                             const float vp[16])
{
    if (g && g->gswFrame) {
        if (!verts || !vert_count || !indices || !index_count || index_count % 3 || !nodes || !node_count || !vp)
            return gsw_fail(g, "silhouette input missing");
        return gsw_silhouette(g, verts, vert_count, indices, index_count, nodes, node_count, vp);
    }
    if (!g || !g->enc) return shadow_fail(g, SHADOW_WARN_FRAME, "outside a frame");
    if (!verts || !vert_count || !indices || !index_count || index_count % 3 ||
        !nodes || !node_count || !vp)
        return shadow_fail(g, SHADOW_WARN_INPUT, "silhouette input missing");
    if (g->shadowTargets >= EM_GFX_SHADOW_TARGET_MAX)
        return shadow_fail(g, SHADOW_WARN_TARGETS, "more silhouettes than "
                           "EM_GFX_SHADOW_TARGET_MAX in one frame");
    if (!g->shadowSilPipeline)
        g->shadowSilPipeline = shadow_pipeline(g, kShadowShaderSrc,
            @"v_shadow_ndc", @"f_shadow_silhouette", MTLPixelFormatRGBA8Unorm,
            false, MTLColorWriteMaskAll);
    if (!g->shadowSilPipeline)
        return shadow_fail(g, SHADOW_WARN_GPU, "silhouette pipeline unavailable");
    const uint32_t slot = g->shadowTargets;
    if (!g->shadowTarget[slot]) {
        MTLTextureDescriptor *td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:EM_SHADOW_GS_TARGET_SIZE
                                        height:EM_SHADOW_GS_TARGET_SIZE
                                     mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModePrivate;
        g->shadowTarget[slot] = [g->device newTextureWithDescriptor:td];
        if (!g->shadowTarget[slot])
            return shadow_fail(g, SHADOW_WARN_GPU, "target allocation failed");
    }
    /* 001C7420 + kernel 0023C750 on the CPU, bit for bit: bone = node x vp,
     * c = p x bone, XYZ2 = ftoi4(c.xyz / c.w); ADC for the guard band. */
    float k1021[4] = { 255.0f, 2048.0f, 0.0f, 0.0f }, k1022[4], k1023[4];
    em_shadow_gs_object_rows(k1022, k1023);
    float *bones = malloc(sizeof(float) * 16 * node_count);
    float (*pos)[4] = malloc(sizeof *pos * vert_count);
    uint8_t *clipped = malloc(vert_count);
    float (*tri)[4] = malloc(sizeof *tri * index_count);
    if (!bones || !pos || !clipped || !tri) {
        free(bones); free(pos); free(clipped); free(tri);
        return shadow_fail(g, SHADOW_WARN_INPUT, "out of memory");
    }
    for (uint32_t b = 0; b < node_count; ++b)
        em_shadow_gs_bone(nodes + 16 * b, vp, bones + 16 * b);
    int bad = 0;
    for (uint32_t i = 0; i < vert_count && !bad; ++i) {
        const float *rec = verts + (size_t)i * 10;
        uint32_t bone;
        memcpy(&bone, rec + 8, 4);
        bone &= EM_GFX_VERT_BONE_MASK;
        if (bone >= node_count) { bad = 1; break; }
        float q3[1][4] = { { rec[0], rec[1], rec[2], 0.0f } };
        const float *bp = bones + 16 * bone;
        EmShadowGsVertex v;
        em_shadow_gs_object_batch(&bp, k1021, k1022, k1023,
                                  (const float (*)[4])q3, 1, &v);
        clipped[i] = (v.why & EM_SHADOW_GS_ADC_CLIP) ? 1u : 0u;
        float ndc[2];
        em_shadow_gs_target_ndc((float)(v.w[0] & 0xFFFF) / 16.0f,
                                (float)(v.w[1] & 0xFFFF) / 16.0f, ndc);
        pos[i][0] = ndc[0]; pos[i][1] = ndc[1]; pos[i][2] = 0.0f; pos[i][3] = 1.0f;
    }
    uint32_t count = 0;
    for (uint32_t t = 0; t + 2 < index_count && !bad; t += 3) {
        const uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
        if (a >= vert_count || b >= vert_count || c >= vert_count) { bad = 1; break; }
        if (clipped[a] || clipped[b] || clipped[c]) continue;
        memcpy(tri[count++], pos[a], 16);
        memcpy(tri[count++], pos[b], 16);
        memcpy(tri[count++], pos[c], 16);
    }
    free(bones); free(pos); free(clipped);
    if (bad) {
        free(tri);
        return shadow_fail(g, SHADOW_WARN_INPUT, "silhouette vertex/node out of range");
    }
    id<MTLCommandBuffer> cb = [g->queue commandBuffer];
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = g->shadowTarget[slot];
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    /* The D_00817E20 sprite: RGBAQ (128,128,128,0) over pixels 0..127. */
    rp.colorAttachments[0].clearColor =
        MTLClearColorMake(128.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0, 0.0);
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    [enc setViewport:(MTLViewport){ 0.0, 0.0, EM_SHADOW_GS_TARGET_SIZE,
                                    EM_SHADOW_GS_TARGET_SIZE, 0.0, 1.0 }];
    if (count) {
        id<MTLBuffer> vb = [g->device newBufferWithBytes:tri
                                                  length:sizeof *tri * count
                                                 options:MTLResourceStorageModeShared];
        [enc setRenderPipelineState:g->shadowSilPipeline];
        [enc setCullMode:MTLCullModeNone];          /* 0023C750 never culls */
        [enc setVertexBuffer:vb offset:0 atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:count];
        [vb release];
    }
    [enc endEncoding];
    /* Committed now: it runs before the frame's command buffer (committed
     * by end_frame), whose receivers sample the target. */
    [cb commit];
    free(tri);
    g->shadowCurrent = (int)slot;
    g->shadowTargets++;
    [g->shadowLast release];
    g->shadowLast = [g->shadowTarget[slot] retain];
    return 0;
}

int em_gfx_shadow_receiver_begin(EmGfx *g, const float uv[16],
                                 const float camera[16],
                                 const float viewproj[16])
{
    if (g && g->gswFrame) {
        if (!uv || !camera) return gsw_fail(g, "receiver input missing");
        if (!(g->fog[3] > 0.0f)) return gsw_fail(g, "receivers without the frame's fog");
        return gsw_receiver_begin(g, uv, camera);
    }
    if (!g || !g->enc) return shadow_fail(g, SHADOW_WARN_FRAME, "outside a frame");
    if (!uv || !camera || !viewproj)
        return shadow_fail(g, SHADOW_WARN_INPUT, "receiver input missing");
    if (g->shadowCurrent < 0)
        return shadow_fail(g, SHADOW_WARN_ORDER, "receivers before a silhouette");
    if (!(g->fog[3] > 0.0f))
        return shadow_fail(g, SHADOW_WARN_FOG, "receivers without the frame's "
                           "fog (em_gfx_fog)");
    if (![g->device supportsFamily:MTLGPUFamilyApple1])
        return shadow_fail(g, SHADOW_WARN_FETCH, "the GPU has no framebuffer "
                           "fetch (destination-alpha test and GS blend)");
    if (!g->shadowRecvPipeline)
        g->shadowRecvPipeline = shadow_pipeline(g, kSkinShaderSrc, @"v_shadow_recv_gs",
            @"f_shadow_receiver", g->layer.pixelFormat, true, MTLColorWriteMaskAll);
    if (!g->shadowRecvPipeline)
        return shadow_fail(g, SHADOW_WARN_GPU, "receiver pipeline unavailable");
    ensure_depth_states(g);
    memcpy(g->shadowUV, uv, 64);
    memcpy(g->shadowCam, camera, 64);
    memcpy(g->shadowVP, viewproj, 64);
    g->shadowRecvOpen = true;
    return 0;
}

int em_gfx_shadow_receiver(EmGfx *g, const EmGfxShadowStrips *object,
                           uint32_t cls)
{
    if (g && g->gswFrame) {
        if (!g->shadowRecvOpen) return gsw_fail(g, "receiver outside begin/end");
        if (!object || !object->qw3 || !object->vertex_count || object->vertex_count % EM_GFX_SHADOW_BATCH ||
            cls > 2u)
            return gsw_fail(g, "receiver strips missing");
        return gsw_receiver(g, object, cls);
    }
    if (!g || !g->enc) return shadow_fail(g, SHADOW_WARN_FRAME, "outside a frame");
    if (!g->shadowRecvOpen)
        return shadow_fail(g, SHADOW_WARN_ORDER, "receiver outside begin/end");
    if (!object || !object->qw3 || !object->vertex_count ||
        object->vertex_count % EM_GFX_SHADOW_BATCH || cls > 2u)
        return shadow_fail(g, SHADOW_WARN_INPUT, "receiver strips missing");
    /* The template fog row: (255, 2048, A, B) from the frame's em_gfx_fog;
     * guard rows as the level template's. */
    float k1021[4] = { 255.0f, 2048.0f, g->fog[4], g->fog[5] }, k1022[4], k1023[4];
    em_shadow_gs_level_rows(k1022, k1023);
    const uint32_t n = object->vertex_count;
    EmShadowGsReceiverVertex *out = malloc(sizeof *out * n);
    /* per drawn vertex: position (NDC, depth, 1), (S, T, Q, 0), (A, F, 0, 0) */
    float (*rec)[12] = malloc(sizeof *rec * 3 * n);
    if (!out || !rec) {
        free(out); free(rec);
        return shadow_fail(g, SHADOW_WARN_INPUT, "out of memory");
    }
    const float (*qw3)[4] = (const float (*)[4])object->qw3;
    uint32_t count = 0;
    int clip = 0;
    for (uint32_t b = 0; b < n; b += EM_GFX_SHADOW_BATCH) {
        em_shadow_gs_receiver_batch(g->shadowCam, g->shadowUV, k1021, k1022,
                                    k1023, qw3 + b, EM_GFX_SHADOW_BATCH, out + b);
        for (uint32_t i = b + 2; i < b + EM_GFX_SHADOW_BATCH; ++i) {
            /* 0023C200 kicks the triangle only when its last vertex has no
             * ADC (data flag or guard-band CLIP). A class-2 object's
             * 0023E8A0 re-pass (below) draws the CLIP ones; classes 0
             * and 1 have no re-pass, so nothing draws them. */
            const uint32_t why = out[i].xyzf.why;
            if (why) continue;
            for (unsigned k = 0; k < 3; ++k) {
                const EmShadowGsReceiverVertex *v = &out[i - 2 + k];
                float *r = rec[count++];
                shadow_gs_position((uint32_t)v->xyzf.w[0] & 0xFFFFu, (uint32_t)v->xyzf.w[1] & 0xFFFFu,
                                   (uint32_t)v->xyzf.w[2] >> 4, r);
                r[4] = v->s; r[5] = v->t; r[6] = v->q; r[7] = 0.0f;
                r[8] = (float)v->a;
                r[9] = (float)((uint32_t)(v->xyzf.w[3] >> 4) & 0xFFu);
                r[10] = r[11] = 0.0f;
            }
        }
    }
    free(out);
    /* Class 2: 001D5C80 runs 0023E8A0 over the same batches after the whole
     * object (001D4FB0, 001D1F80(0,2,6), 001D4B50, 001D4CD0). Its
     * triangles come after the object's own, as on the GS. */
    if (cls == 2u) {
        const uint32_t clip_cap = 30u * 27u;
        EmVu1Qword *dmem = malloc(sizeof *dmem * EM_VU1_DMEM_QWORDS);
        EmVu1ClipResult *res = shadow_clip_result();
        EmShadowGsClipVertex *gv = malloc(sizeof *gv * clip_cap);
        float (*cp)[4] = malloc(sizeof *cp * clip_cap);
        float (*rec2)[12] = dmem && res && gv && cp
            ? realloc(rec, sizeof *rec * (3 * n + clip_cap * (n / EM_GFX_SHADOW_BATCH)))
            : NULL;
        if (rec2) rec = rec2;
        else clip = 2;
        for (uint32_t b = 0; b < n && !clip; b += EM_GFX_SHADOW_BATCH) {
            const int v = shadow_clip_batch(EM_VU1_CLIP_RECEIVER, g->shadowCam,
                                            g->shadowUV, k1021, qw3 + b, dmem,
                                            res, gv, clip_cap, cp);
            if (v < 0) { clip = 1; break; }
            for (int k = 0; k < v; ++k) {
                float *r = rec[count++];
                memcpy(r, cp[k], sizeof cp[k]);
                r[4] = gv[k].s; r[5] = gv[k].t; r[6] = gv[k].q; r[7] = 0.0f;
                r[8] = (float)gv[k].a;
                r[9] = (float)gv[k].f;
                r[10] = r[11] = 0.0f;
            }
        }
        free(dmem); free(res); free(gv); free(cp);
    }
    if (clip) {
        free(rec);
        return shadow_fail(g, clip == 2 ? SHADOW_WARN_INPUT : SHADOW_WARN_CLIP,
                           clip == 2 ? "out of memory"
                                     : "receiver clip kernel 0023E8A0 fault (an "
                                       "FTOI outside int32, a data word naming "
                                       "another matrix)");
    }
    if (count) {
        id<MTLBuffer> vb = [g->device newBufferWithBytes:rec
                                                  length:sizeof *rec * count
                                                 options:MTLResourceStorageModeShared];
        [g->enc setRenderPipelineState:g->shadowRecvPipeline];
        [g->enc setDepthStencilState:g->depthGlow];   /* GEQUAL, ZMSK 1 */
        [g->enc setCullMode:MTLCullModeNone];         /* 0023C200 never culls */
        [g->enc setVertexBuffer:vb offset:0 atIndex:0];
        [g->enc setFragmentTexture:g->shadowTarget[g->shadowCurrent] atIndex:0];
        [g->enc setFragmentBytes:g->fog length:sizeof(g->fog) atIndex:4];
        [g->enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:count];
        [vb release];
    }
    free(rec);
    return 0;
}

int em_gfx_shadow_receiver_end(EmGfx *g)
{
    if (g && g->gswFrame) {
        if (!g->shadowRecvOpen) return gsw_fail(g, "receiver end without begin");
        return gsw_receiver_end(g);
    }
    if (!g || !g->enc) return shadow_fail(g, SHADOW_WARN_FRAME, "outside a frame");
    if (!g->shadowRecvOpen)
        return shadow_fail(g, SHADOW_WARN_ORDER, "receiver end without begin");
    g->shadowRecvOpen = false;
    return 0;
}

int em_gfx_shadow_target_read(EmGfx *g, uint8_t *rgba)
{
    /* The GS frame: the target in GS memory (FBP 0x12C, 128 x 128). */
    if (g && g->gswOn && rgba)
        return em_gs_world_read(g->gsw, (uint32_t)EM_SHADOW_GS_FRAME_TARGET & 0x1FFu,
                                (uint32_t)(EM_SHADOW_GS_FRAME_TARGET >> 16) & 0x3Fu, EM_SHADOW_GS_TARGET_SIZE,
                                EM_SHADOW_GS_TARGET_SIZE, rgba);
    if (!g || !rgba || !g->shadowLast) return -1;
    @autoreleasepool {   /* called outside begin/end_frame */
    const NSUInteger row = EM_SHADOW_GS_TARGET_SIZE * 4;
    id<MTLBuffer> buf = [g->device newBufferWithLength:row * EM_SHADOW_GS_TARGET_SIZE
                                               options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [g->queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:g->shadowLast sourceSlice:0 sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(EM_SHADOW_GS_TARGET_SIZE,
                                      EM_SHADOW_GS_TARGET_SIZE, 1)
                 toBuffer:buf destinationOffset:0
    destinationBytesPerRow:row
  destinationBytesPerImage:row * EM_SHADOW_GS_TARGET_SIZE];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    memcpy(rgba, buf.contents, row * EM_SHADOW_GS_TARGET_SIZE);
    [buf release];
    }
    return 0;
}

/* --- Object units (em_gfx_object_unit — em_gfx.h "Object units") ------- */

/* The GS pixel path of the object kernel's class-0 state. Vertices arrive
 * in NDC with w = 1, so every attribute is screen-linear, as the GS
 * interpolates RGBA, F and S, T, Q; the texture coordinate is divided per
 * pixel (STQ). Texels are the raw CLUT entries (RGBA8Uint, GS alpha
 * 0..0xFF), read with the GS bilinear rule (sample point U - 0.5 on the
 * 1/16 grid, 4-bit weights, as f_shadow_receiver) and REPEAT wrap (CLAMP
 * 0, power-of-two sizes). k = (width, height, FOGCOL r | g << 8 | b << 16,
 * fog enabled | MODULATE << 1: the TEX0's TFX, MODULATE or HIGHLIGHT).
 * APPROXIMATION, not verified against a GS dump: the
 * per-pixel values come from Metal's float interpolation, floored with a
 * 0.001 epsilon; the GS DDA stepping is not modelled. */
static NSString *const kObjectShaderSrc =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
EM_FOG_GS_MSL
"struct OVOut { float4 pos [[position]];\n"
"               float4 rgba [[center_no_perspective]];\n"
"               float4 stqf [[center_no_perspective]]; };\n"
"vertex OVOut v_object(uint vid [[vertex_id]], const device float4 *v [[buffer(0)]]) {\n"
"    OVOut o; o.pos = v[3 * vid]; o.rgba = v[3 * vid + 1]; o.stqf = v[3 * vid + 2]; return o;\n"
"}\n"
"fragment float4 f_object(OVOut in [[stage_in]],\n"
"                         texture2d<uint, access::read> tex [[texture(0)]],\n"
"                         constant uint4 &k [[buffer(0)]]) {\n"
"    int w = int(k.x), h = int(k.y);\n"
"    float u = in.stqf.x / in.stqf.z, v = in.stqf.y / in.stqf.z;\n"
"    int uu = int(floor(u * float(w) * 16.0)) - 8;\n"
"    int vv = int(floor(v * float(h) * 16.0)) - 8;\n"
"    int fu = uu & 15, fv = vv & 15;\n"
"    int x0 = (uu >> 4) & (w - 1), x1 = ((uu >> 4) + 1) & (w - 1);\n"
"    int y0 = (vv >> 4) & (h - 1), y1 = ((vv >> 4) + 1) & (h - 1);\n"
"    uint4 t = (tex.read(uint2(x0, y0)) * uint((16 - fu) * (16 - fv)) +\n"
"               tex.read(uint2(x1, y0)) * uint(fu * (16 - fv)) +\n"
"               tex.read(uint2(x0, y1)) * uint((16 - fu) * fv) +\n"
"               tex.read(uint2(x1, y1)) * uint(fu * fv)) >> 8;\n"
"    uint4 cf = uint4(clamp(floor(in.rgba + 0.001), 0.0, 255.0));\n"
"    /* TFX MODULATE or HIGHLIGHT, TCC 1 (COLCLAMP 1). */\n"
"    uint3 m = min((t.rgb * cf.rgb) >> 7, uint3(255));\n"
"    uint3 c; uint a;\n"
"    if ((k.w & 2u) != 0u) { c = m; a = min((t.a * cf.a) >> 7, 255u); }\n"
"    else { c = min(m + cf.a, uint3(255)); a = min(t.a + cf.a, 255u); }\n"
"    /* TEST_1: ATE, ATST GREATER, AREF 0, AFAIL KEEP. */\n"
"    if (a == 0u) discard_fragment();\n"
"    if ((k.w & 1u) != 0u) {\n"
"        /* The 8.7 fog weight of the screen-linear F (em_fog_gs.h). */\n"
"        uint3 fc = uint3(k.z & 255u, (k.z >> 8) & 255u, (k.z >> 16) & 255u);\n"
"        c = em_fog_gs_blend7(c, em_fog_gs_weight7(in.stqf.w), fc);\n"
"    }\n"
"    return float4(float3(c) / 255.0, float(a) / 255.0);\n"
"}\n";

enum {
    OBJ_WARN_FRAME = 1u, OBJ_WARN_UNIT = 2u, OBJ_WARN_TEXTURE = 4u, OBJ_WARN_FOG = 8u,
    OBJ_WARN_GPU = 16u, OBJ_WARN_INPUT = 32u,
};

static int object_fail(EmGfx *g, uint32_t why, const char *what, const char *detail)
{
    if (g && !(g->objWarned & why)) {
        g->objWarned |= why;
        fprintf(stderr, "gfx: object unit: %s%s%s — not drawn\n", what, detail ? ": " : "",
                detail ? detail : "");
    }
    return -1;
}

int em_gfx_world_textures_reset(EmGfx *g)
{
    if (!g) return -1;
    for (uint32_t i = 0; i < g->objTexCount; ++i) [g->objTex[i].tex release];
    memset(g->objTex, 0, sizeof g->objTex);
    g->objTexCount = 0;
    return 0;
}

static const struct EmGfxObjectTex *object_texture(const EmGfx *g, uint64_t tex0)
{
    const uint64_t key = tex0 & ~(UINT64_C(7) << 61);
    for (uint32_t i = 0; i < g->objTexCount; i++)
        if (g->objTex[i].tex0 == key) return &g->objTex[i];
    return NULL;
}

int em_gfx_object_texture(EmGfx *g, uint64_t tex0, const uint8_t *rgba, uint32_t width,
                          uint32_t height)
{
    if (!g || !rgba) return -1;
    const uint64_t key = tex0 & ~(UINT64_C(7) << 61);
    const uint32_t tw = (uint32_t)(key >> 26) & 15u, th = (uint32_t)(key >> 30) & 15u;
    const uint32_t tcc = (uint32_t)(key >> 34) & 1u, tfx = (uint32_t)(key >> 35) & 3u;
    /* The shader implements TFX MODULATE and HIGHLIGHT with TCC 1 and
     * power-of-two REPEAT; anything else is refused, not approximated. */
    if (tw > 10u || th > 10u || width != (1u << tw) || height != (1u << th) || tcc != 1u ||
        (tfx != 0u && tfx != 2u))
        return object_fail(g, OBJ_WARN_INPUT, "texture registration",
                           "not a MODULATE / HIGHLIGHT TCC 1 TEX0 of its size");
    struct EmGfxObjectTex *slot = (struct EmGfxObjectTex *)object_texture(g, key);
    if (!slot) {
        if (g->objTexCount >= EM_GFX_OBJECT_TEX_MAX)
            return object_fail(g, OBJ_WARN_INPUT, "texture registration", "EM_GFX_OBJECT_TEX_MAX reached");
        slot = &g->objTex[g->objTexCount++];
        memset(slot, 0, sizeof *slot);
        slot->tex0 = key;
    }
    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Uint
                                     width:width height:height mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> t = [g->device newTextureWithDescriptor:td];
    if (!t) return object_fail(g, OBJ_WARN_GPU, "texture registration", "allocation failed");
    [t replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0
           withBytes:rgba bytesPerRow:4u * width];
    [slot->tex release];
    slot->tex = t;
    slot->width = width;
    slot->height = height;
    return 0;
}

/* GS depth -> the port's depth for the same view depth. The engine's
 * projection gives Z = bz + az / w (em_math.h: az = 0.1 * (2^24 - 1),
 * bz = 1 - az / 16711680), the port's em_mat4_perspective_gs gives
 * d = F / (F - N) - N F / ((F - N) w) with N = 0.1, F = 16711680; with
 * 1 / w = (Z - bz) / az and N / az = 1 / (2^24 - 1):
 * d = F / (F - N) * (1 - (Z - bz) / (2^24 - 1)). Z is the kicked 24-bit
 * value, so the depth order among object triangles is the GS's; against
 * the level, which the port projects itself, it is the same function of
 * the view depth. */
static float bits_f(uint32_t b)
{
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}

static float object_depth(uint32_t z)
{
    const double bz = (double)bits_f(0x3F664CB3u);  /* em_math.h: the P z-row literal */
    const double far_ = (double)EM_GS_FAR, near_ = (double)EM_GS_NEAR;
    return (float)(far_ / (far_ - near_) * (1.0 - ((double)z - bz) / 16777215.0));
}

/* The triangles through the class-0 pixel path (em_gfx_object_unit's and
 * em_gfx_gs_opaque's): one draw per run of triangles with the same TEX0, in
 * kick order. */
static int object_draw(EmGfx *g, const EmObjectUnitTriangle *tri, uint32_t count)
{
    const bool fog = g->fog[3] > 0.0f;
    const struct EmGfxObjectTex *seen = NULL;
    uint64_t seen_tex0 = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!seen || tri[i].tex0 != seen_tex0) {
            seen = object_texture(g, tri[i].tex0);
            seen_tex0 = tri[i].tex0;
        }
        if (!seen)
            return object_fail(g, OBJ_WARN_TEXTURE, "a TEX0 without a registered texture",
                               "run tools/export_object_textures.py");
        for (unsigned c = 0; c < 3u && !fog; c++)
            if (tri[i].v[c].f != 255u)
                return object_fail(g, OBJ_WARN_FOG, "fogged vertices without the frame's FOGCOL",
                                   "em_gfx_fog_coefficients first");
    }
    if (!count) return 0;
    if (!g->objPipeline)
        g->objPipeline = build_pipeline(g, kObjectShaderSrc, @"v_object", @"f_object", EM_BLEND_OPAQUE);
    if (!g->objPipeline) return object_fail(g, OBJ_WARN_GPU, "pipeline unavailable", NULL);
    ensure_depth_states(g);
    float *v = malloc(sizeof(float) * 36u * count);
    if (!v) return object_fail(g, OBJ_WARN_INPUT, "out of memory", NULL);
    for (uint32_t i = 0; i < count; i++) {
        for (unsigned c = 0; c < 3u; c++) {
            const EmObjectUnitVertex *s = &tri[i].v[c];
            float *o = v + (size_t)(3u * i + c) * 12u;
            const uint16_t xy[2] = { s->x, s->y };
            em_background_gs_ndc(xy, o);
            o[2] = object_depth(s->z);
            o[3] = 1.0f;
            for (unsigned k = 0; k < 4u; k++) o[4 + k] = (float)s->rgba[k];
            o[8] = bits_f(s->s);
            o[9] = bits_f(s->t);
            o[10] = bits_f(s->q);
            o[11] = (float)s->f;
        }
    }
    id<MTLBuffer> vb = [g->device newBufferWithBytes:v length:sizeof(float) * 36u * count
                                             options:MTLResourceStorageModeShared];
    free(v);
    const uint32_t fogcol = fog
        ? (uint32_t)lroundf(g->fog[0] * 255.0f) | (uint32_t)lroundf(g->fog[1] * 255.0f) << 8 |
          (uint32_t)lroundf(g->fog[2] * 255.0f) << 16
        : 0u;
    [g->enc setRenderPipelineState:g->objPipeline];
    [g->enc setDepthStencilState:g->depthOn];        /* ZTST GEQUAL, ZMSK 0 */
    [g->enc setCullMode:MTLCullModeNone];            /* the GS draws both windings */
    [g->enc setVertexBuffer:vb offset:0 atIndex:0];
    for (uint32_t i = 0; i < count;) {
        const struct EmGfxObjectTex *t = object_texture(g, tri[i].tex0);
        uint32_t j = i + 1u;
        while (j < count && ((tri[j].tex0 ^ tri[i].tex0) & ~(UINT64_C(7) << 61)) == 0) j++;
        const uint32_t modulate = ((t->tex0 >> 35) & 3u) == 0u ? 1u : 0u;
        const uint32_t k[4] = { t->width, t->height, fogcol, (fog ? 1u : 0u) | modulate << 1 };
        [g->enc setFragmentTexture:t->tex atIndex:0];
        [g->enc setFragmentBytes:k length:sizeof k atIndex:0];
        [g->enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:3u * i vertexCount:3u * (j - i)];
        i = j;
    }
    [vb release];
    return 0;
}

int em_gfx_object_unit(EmGfx *g, const EmGfxObjectUnit *unit)
{
    if (g && g->gswFrame) return unit ? gsw_object(g, unit) : gsw_fail(g, "no unit");
    if (!g || !g->enc) return object_fail(g, OBJ_WARN_FRAME, "outside a frame", NULL);
    if (!unit) return object_fail(g, OBJ_WARN_INPUT, "no unit", NULL);
    if (unit->gs_class != 0u)
        return object_fail(g, OBJ_WARN_INPUT, "a class-2 unit (it is drawn on the chain page)", NULL);
    if (em_object_unit_run(unit, &g->objResult))
        return object_fail(g, OBJ_WARN_UNIT, "the VU1 programs refused the unit", g->objResult.why);
    return object_draw(g, g->objResult.tri, g->objResult.count);
}

/* em_gfx_gs_opaque (em_gfx.h): the static world's triangles. The state each
 * one carries must be exactly the class-0 set the object path reproduces. */
int em_gfx_gs_opaque(EmGfx *g, const EmGfxGsPrim *prims, uint32_t count)
{
    if (g && g->gswFrame) return prims || !count ? gsw_opaque(g, prims, count) : gsw_fail(g, "no primitives");
    if (!g || !g->enc) return object_fail(g, OBJ_WARN_FRAME, "outside a frame", NULL);
    if (!prims && count) return object_fail(g, OBJ_WARN_INPUT, "no primitives", NULL);
    const uint32_t need = EM_GFX_GS_TEX0 | EM_GFX_GS_TEX1 | EM_GFX_GS_TEST | EM_GFX_GS_CLAMP | EM_GFX_GS_COLCLAMP;
    for (uint32_t i = 0; i < count; i++) {
        const EmGfxGsPrim *p = &prims[i];
        const uint32_t type = p->prim & 7u;
        /* IIP 0x08, TME 0x10, FGE 0x20 set; ABE 0x40, AA1 0x80, FST 0x100,
         * CTXT 0x200, FIX 0x400 clear */
        if (p->count != 3u || (type != 3u && type != 4u) || (p->prim & 0x7F8u) != 0x038u ||
            (p->set & need) != need || p->test != 0x5000Du || p->tex1 != 0x60u || p->clamp != 0u ||
            p->colclamp != 1u)
            return object_fail(g, OBJ_WARN_INPUT, "em_gfx_gs_opaque",
                               "a primitive outside the class-0 opaque triangle state");
    }
    if (count > g->opaqueCap) {
        EmObjectUnitTriangle *grown = realloc(g->opaqueTri, sizeof *grown * count);
        if (!grown) return object_fail(g, OBJ_WARN_INPUT, "out of memory", NULL);
        g->opaqueTri = grown;
        g->opaqueCap = count;
    }
    for (uint32_t i = 0; i < count; i++) {
        EmObjectUnitTriangle *t = &g->opaqueTri[i];
        memset(t, 0, sizeof *t);
        t->tex0 = prims[i].tex0;
        for (unsigned c = 0; c < 3u; c++) {
            const EmGfxGsVertex *s = &prims[i].v[c];
            EmObjectUnitVertex *d = &t->v[c];
            d->x = s->x;
            d->y = s->y;
            d->z = s->z;
            d->f = s->f;
            memcpy(d->rgba, s->rgba, 4);
            d->s = s->s;
            d->t = s->t;
            d->q = s->q;
        }
    }
    return object_draw(g, g->opaqueTri, count);
}

/* --- The chain page: GS primitives (em_gfx_gs_prims — em_gfx.h) ---------- */

/* The GS pixel path of the chain page's primitives (docs/CHAIN_PAGE.md
 * section 5). Vertices arrive in NDC with w = 1 (X / Y through
 * em_background_gs_ndc, Z through object_depth), so RGBA, F and S, T, Q are
 * screen-linear, as the GS interpolates them; the texture coordinate is
 * divided per pixel (STQ). Texels are the raw CLUT entries, read with the GS
 * bilinear rule (sample point U - 0.5 on the 1/16 grid, 4-bit weights) and
 * REPEAT (CLAMP_1 0). TFX MODULATE with TCC 1: Cf = Ct * Cv >> 7, Af = At *
 * Av >> 7 (untextured: Cv, Av); MODULATE with TCC 0 (an RGB texture: AREA01's
 * floor fields, 001E9E60): the same Cf, Af = Av (the GS's texture function
 * table); HIGHLIGHT (the class-2 object units of
 * 001CABA0): Cf = (Ct * Cv >> 7) + Av, Af = At + Av, each clamped to 255
 * (the object-unit shader's rule); fog (FGE): FOGCOL + ((C - FOGCOL) * F >> 8)
 * (em_fog_gs_blend, the measured rule: GS_EXACT.md 5.2);
 * the alpha test NEVER with AFAIL RGB_ONLY writes RGB only (the pipeline
 * masks alpha, the depth state writes no Z; depth GEQUAL); the blend
 * ((A - B) * C >> 7) + D with A, B, D each Cs, Cd or 0 and C As or FIX,
 * COLCLAMP 1, on the frame pixel (framebuffer fetch).
 * k[0] = (width, height, FOGCOL r | g << 8 | b << 16, FGE);
 * k[1] = (A, B, C, D selectors as ALPHA_1 packs them, FIX, TFX (0 MODULATE, 2
 * HIGHLIGHT), 1 when TCC is 0).
 * APPROXIMATION, as the object units': the per-pixel values come from
 * Metal's float interpolation, floored with a 0.001 epsilon; the GS DDA
 * stepping (and the GS line rule) is not modelled and no GS dump of a drawn
 * frame checks it. */
static NSString *const kGsPrimShaderSrc =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
EM_FOG_GS_MSL
"struct GVOut { float4 pos [[position]];\n"
"               float4 rgba [[center_no_perspective]];\n"
"               float4 stqf [[center_no_perspective]]; };\n"
"vertex GVOut v_gs(uint vid [[vertex_id]], const device float4 *v [[buffer(0)]]) {\n"
"    GVOut o; o.pos = v[3 * vid]; o.rgba = v[3 * vid + 1]; o.stqf = v[3 * vid + 2]; return o;\n"
"}\n"
"static float4 gs_out(uint3 c, uint a, float4 dst, constant uint4 *k) {\n"
"    int3 cs = int3(c), cd = int3(round(dst.rgb * 255.0));\n"
"    uint sel = k[1].x;\n"
"    int3 va = (sel & 3u) == 0u ? cs : (sel & 3u) == 1u ? cd : int3(0);\n"
"    int3 vb = ((sel >> 2) & 3u) == 0u ? cs : ((sel >> 2) & 3u) == 1u ? cd : int3(0);\n"
"    int vc = ((sel >> 4) & 3u) == 0u ? int(a) : int(k[1].y);\n"
"    int3 vd = ((sel >> 6) & 3u) == 0u ? cs : ((sel >> 6) & 3u) == 1u ? cd : int3(0);\n"
"    float3 prod = float3((va - vb) * vc);\n"
"    int3 o = clamp(int3(floor(prod / 128.0)) + vd, 0, 255);\n"
"    return float4(float3(o) / 255.0, dst.a);\n"
"}\n"
"static uint3 gs_fog(uint3 c, float fv, constant uint4 *k) {\n"
"    if (k[0].w == 0u) return c;\n"
"    /* The 8.7 fog weight of the screen-linear F (em_fog_gs.h). */\n"
"    uint3 fc = uint3(k[0].z & 255u, (k[0].z >> 8) & 255u, (k[0].z >> 16) & 255u);\n"
"    return em_fog_gs_blend7(c, em_fog_gs_weight7(fv), fc);\n"
"}\n"
"fragment float4 f_gs_tex(GVOut in [[stage_in]], float4 dst [[color(0)]],\n"
"                         texture2d<uint, access::read> tex [[texture(0)]],\n"
"                         constant uint4 *k [[buffer(0)]]) {\n"
"    int w = int(k[0].x), h = int(k[0].y);\n"
"    float u = in.stqf.x / in.stqf.z, v = in.stqf.y / in.stqf.z;\n"
"    int uu = int(floor(u * float(w) * 16.0)) - 8;\n"
"    int vv = int(floor(v * float(h) * 16.0)) - 8;\n"
"    int fu = uu & 15, fv = vv & 15;\n"
"    int x0 = (uu >> 4) & (w - 1), x1 = ((uu >> 4) + 1) & (w - 1);\n"
"    int y0 = (vv >> 4) & (h - 1), y1 = ((vv >> 4) + 1) & (h - 1);\n"
"    uint4 t = (tex.read(uint2(x0, y0)) * uint((16 - fu) * (16 - fv)) +\n"
"               tex.read(uint2(x1, y0)) * uint(fu * (16 - fv)) +\n"
"               tex.read(uint2(x0, y1)) * uint((16 - fu) * fv) +\n"
"               tex.read(uint2(x1, y1)) * uint(fu * fv)) >> 8;\n"
"    uint4 cv = uint4(clamp(floor(in.rgba + 0.001), 0.0, 255.0));\n"
"    uint3 c = min((t.rgb * cv.rgb) >> 7, uint3(255));\n"
"    uint a = min((t.a * cv.a) >> 7, 255u);\n"
"    /* TFX HIGHLIGHT (k[1].z == 2): Cf = Ct * Cv >> 7 + Av, Af = At + Av. */\n"
"    if (k[1].z == 2u) { c = min(c + cv.a, uint3(255)); a = min(t.a + cv.a, 255u); }\n"
"    /* TCC 0 (RGB texture; MODULATE only): Af = Av. */\n"
"    if (k[1].w != 0u) a = cv.a;\n"
"    return gs_out(gs_fog(c, in.stqf.w, k), a, dst, k);\n"
"}\n"
"fragment float4 f_gs_flat(GVOut in [[stage_in]], float4 dst [[color(0)]],\n"
"                          constant uint4 *k [[buffer(0)]]) {\n"
"    uint4 cv = uint4(clamp(floor(in.rgba + 0.001), 0.0, 255.0));\n"
"    return gs_out(gs_fog(cv.rgb, in.stqf.w, k), cv.a, dst, k);\n"
"}\n";

enum {
    GS_WARN_FRAME = 1u, GS_WARN_STATE = 2u, GS_WARN_TEXTURE = 4u, GS_WARN_FOG = 8u,
    GS_WARN_GPU = 16u, GS_WARN_INPUT = 32u, GS_WARN_FETCH = 64u,
};

static int gs_fail(EmGfx *g, uint32_t why, const char *what, uint64_t detail)
{
    if (g && !(g->gsWarned & why)) {
        g->gsWarned |= why;
        fprintf(stderr, "gfx: chain page primitive: %s (%#llx) — not drawn\n", what,
                (unsigned long long)detail);
    }
    return -1;
}

int em_gfx_gs_texture(EmGfx *g, uint64_t tex0, const uint8_t *rgba, uint32_t width, uint32_t height)
{
    if (!g || !rgba) return -1;
    const uint64_t key = tex0 & ~(UINT64_C(7) << 61);
    const uint32_t tw = (uint32_t)(key >> 26) & 15u, th = (uint32_t)(key >> 30) & 15u;
    if (tw > 10u || th > 10u || width != (1u << tw) || height != (1u << th))
        return gs_fail(g, GS_WARN_INPUT, "texture registration: not 2^TW x 2^TH", key);
    struct EmGfxObjectTex *slot = (struct EmGfxObjectTex *)object_texture(g, key);
    if (!slot) {
        if (g->objTexCount >= EM_GFX_OBJECT_TEX_MAX)
            return gs_fail(g, GS_WARN_INPUT, "texture registration: table full", key);
        slot = &g->objTex[g->objTexCount++];
        memset(slot, 0, sizeof *slot);
        slot->tex0 = key;
    }
    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Uint
                                     width:width height:height mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> t = [g->device newTextureWithDescriptor:td];
    if (!t) return gs_fail(g, GS_WARN_GPU, "texture allocation failed", key);
    [t replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0
           withBytes:rgba bytesPerRow:4u * width];
    [slot->tex release];
    slot->tex = t;
    slot->width = width;
    slot->height = height;
    return 0;
}

/* The state one primitive needs, checked against what the pixel path
 * implements. Returns NULL (drawable) or the refusal. */
static const char *gs_refusal(const EmGfxGsPrim *p, const struct EmGfxObjectTex **tex)
{
    const uint32_t type = p->prim & 7u, iip = (p->prim >> 3) & 1u, tme = (p->prim >> 4) & 1u;
    const uint32_t abe = (p->prim >> 6) & 1u;
    *tex = NULL;
    if (p->prim & 0x780u) return "PRIM AA1 / FST / CTXT / FIX";
    if (!abe) return "PRIM without ABE";
    if (type == 1u || type == 2u) {
        /* A LINE list of two vertices (the aim beam 001E2BA0's PRIM 0x69) is
         * one segment, as a two-vertex line strip: the GS assembles a list
         * every two vertices and a strip from its last two (GS_EXACT.md
         * 3.0), and its measured line rule covers both (decomp
         * GS_CONFORMANCE.md 5.2 / 5.3; CHAIN_PAGE.md section 5). */
        if (p->count != 2u || tme || !iip) return "a line other than Gouraud untextured";
    } else if (type == 3u || type == 4u || type == 5u) {
        /* Type 3: the class-2 object units' clip pass (PRIM 0x07B). An
         * untextured Gouraud strip is the knife trail's (001F15F0's GIF tag
         * PRIM 0x4C), drawn by the flat path (Cf = Cv, Af = Av). */
        if (p->count != 3u || !iip) return "a triangle other than Gouraud";
    } else if (type == 6u) {
        if (p->count != 2u || !tme) return "an untextured sprite";
    } else {
        return "a PRIM type the page does not draw";
    }
    const uint32_t need = EM_GFX_GS_ALPHA | EM_GFX_GS_TEST | EM_GFX_GS_COLCLAMP |
        (tme ? EM_GFX_GS_TEX0 | EM_GFX_GS_TEX1 | EM_GFX_GS_CLAMP : 0u);
    if ((p->set & need) != need) return "a drawing state the page did not set";
    if (p->test != 0x53001u) return "TEST_1 other than 0x53001";
    if (p->colclamp != 1u) return "COLCLAMP other than 1";
    const uint32_t a = (uint32_t)p->alpha & 0xFFu;
    if ((p->alpha & ~(UINT64_C(0xFF) << 32 | 0xFFu)) || ((a >> 4) & 3u) == 1u || ((a >> 4) & 3u) == 3u ||
        (a & 3u) == 3u || ((a >> 2) & 3u) == 3u || ((a >> 6) & 3u) == 3u)
        return "an ALPHA_1 form the pixel path does not implement";
    if (tme) {
        const uint64_t t0 = p->tex0 & ~(UINT64_C(7) << 61);
        const uint32_t psm = (uint32_t)(t0 >> 20) & 0x3Fu, tcc = (uint32_t)(t0 >> 34) & 1u;
        const uint32_t tfx = (uint32_t)(t0 >> 35) & 3u, cpsm = (uint32_t)(t0 >> 51) & 0xFu;
        const uint32_t csm = (uint32_t)(t0 >> 55) & 1u;
        /* HIGHLIGHT (TFX 2): the class-2 object units' textures; TCC 0
         * only with MODULATE (the floor fields' RGB textures). */
        if ((psm != 0x13u && psm != 0x14u) || cpsm || csm || (tcc != 1u && tfx != 0u) ||
            (tfx != 0u && tfx != 2u))
            return "a TEX0 other than a CT32 CLUT texture with TCC 1 MODULATE / HIGHLIGHT or TCC 0 MODULATE";
        if (p->tex1 != 0x60u) return "TEX1_1 other than 0x60";
        if (p->clamp != 0u) return "CLAMP_1 other than REPEAT";
    }
    return NULL;
}

static void gs_put(float *o, uint16_t x, uint16_t y, uint32_t z, const uint8_t rgba[4], float s, float t,
                   float q, float f)
{
    const uint16_t xy[2] = { x, y };
    em_background_gs_ndc(xy, o);
    o[2] = object_depth(z);
    o[3] = 1.0f;
    for (unsigned k = 0; k < 4u; k++) o[4 + k] = (float)rgba[k];
    o[8] = s;
    o[9] = t;
    o[10] = q;
    o[11] = f;
}

int em_gfx_gs_prims(EmGfx *g, const EmGfxGsPrim *prims, uint32_t count)
{
    if (g && g->gswFrame) return prims || !count ? gsw_page(g, prims, count) : gsw_fail(g, "no primitives");
    if (!g || !g->enc) return gs_fail(g, GS_WARN_FRAME, "outside a frame", 0);
    if (!count) return 0;
    if (!prims) return gs_fail(g, GS_WARN_INPUT, "no primitives", 0);
    const bool fog = g->fog[3] > 0.0f;
    const struct EmGfxObjectTex **texs = calloc(count, sizeof *texs);
    if (!texs) return gs_fail(g, GS_WARN_INPUT, "out of memory", count);
    for (uint32_t i = 0; i < count; i++) {
        const char *why = gs_refusal(&prims[i], &texs[i]);
        if (why) { free(texs); return gs_fail(g, GS_WARN_STATE, why, prims[i].prim); }
        if ((prims[i].prim >> 4) & 1u) {
            texs[i] = object_texture(g, prims[i].tex0);
            if (!texs[i]) {
                free(texs);
                return gs_fail(g, GS_WARN_TEXTURE, "a TEX0 without a registered texture "
                               "(run tools/export_page_textures.py)", prims[i].tex0);
            }
        }
        if (((prims[i].prim >> 5) & 1u) && !fog) {
            free(texs);
            return gs_fail(g, GS_WARN_FOG, "fogged primitive without the frame's FOGCOL", prims[i].prim);
        }
    }
    if (![g->device supportsFamily:MTLGPUFamilyApple1]) {
        free(texs);
        return gs_fail(g, GS_WARN_FETCH, "the GPU has no framebuffer fetch (the GS blend)", 0);
    }
    const MTLColorWriteMask rgb = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
    if (!g->gsTexPipeline)
        g->gsTexPipeline = shadow_pipeline(g, kGsPrimShaderSrc, @"v_gs", @"f_gs_tex", g->layer.pixelFormat, true, rgb);
    if (!g->gsFlatPipeline)
        g->gsFlatPipeline = shadow_pipeline(g, kGsPrimShaderSrc, @"v_gs", @"f_gs_flat", g->layer.pixelFormat, true, rgb);
    if (!g->gsTexPipeline || !g->gsFlatPipeline) {
        free(texs);
        return gs_fail(g, GS_WARN_GPU, "pipeline unavailable", 0);
    }
    ensure_depth_states(g);
    /* Every primitive becomes 6 vertices at most (a sprite's two
     * triangles), 12 floats each. */
    float *v = malloc(sizeof(float) * 72u * count);
    uint32_t *first = malloc(sizeof(uint32_t) * 2u * count);
    if (!v || !first) {
        free(v); free(first); free(texs);
        return gs_fail(g, GS_WARN_INPUT, "out of memory", count);
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        const EmGfxGsPrim *p = &prims[i];
        const uint32_t type = p->prim & 7u;
        first[2u * i] = n;
        if (type == 6u) {
            /* The GS sprite: Z, F and RGBA of the second vertex; S / Q, T / Q
             * at each corner, affine across the rectangle. */
            const EmGfxGsVertex *a = &p->v[0], *b = &p->v[1];
            const float ua = bits_f(a->s) / bits_f(a->q), va = bits_f(a->t) / bits_f(a->q);
            const float ub = bits_f(b->s) / bits_f(b->q), vb = bits_f(b->t) / bits_f(b->q);
            const uint16_t xs[2] = { a->x, b->x }, ys[2] = { a->y, b->y };
            const float us[2] = { ua, ub }, vs[2] = { va, vb };
            static const unsigned corner[6][2] = { {0, 0}, {1, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 1} };
            for (unsigned c = 0; c < 6u; c++)
                gs_put(v + (size_t)(n + c) * 12u, xs[corner[c][0]], ys[corner[c][1]], b->z, b->rgba,
                       us[corner[c][0]], vs[corner[c][1]], 1.0f, (float)b->f);
            n += 6u;
        } else {
            for (uint32_t c = 0; c < p->count; c++) {
                const EmGfxGsVertex *s = &p->v[c];
                gs_put(v + (size_t)(n + c) * 12u, s->x, s->y, s->z, s->rgba, bits_f(s->s), bits_f(s->t),
                       bits_f(s->q), (float)s->f);
            }
            n += p->count;
        }
        first[2u * i + 1u] = n - first[2u * i];
    }
    id<MTLBuffer> vb = [g->device newBufferWithBytes:v length:sizeof(float) * 12u * n
                                             options:MTLResourceStorageModeShared];
    free(v);
    const uint32_t fogcol = fog
        ? (uint32_t)lroundf(g->fog[0] * 255.0f) | (uint32_t)lroundf(g->fog[1] * 255.0f) << 8 |
          (uint32_t)lroundf(g->fog[2] * 255.0f) << 16
        : 0u;
    [g->enc setDepthStencilState:g->depthGlow];      /* ZTST GEQUAL; AFAIL RGB_ONLY: no Z */
    [g->enc setCullMode:MTLCullModeNone];
    [g->enc setVertexBuffer:vb offset:0 atIndex:0];
    /* One draw per primitive, in GS order: each blends over the frame the
     * ones before it left (framebuffer fetch). */
    for (uint32_t i = 0; i < count; i++) {
        const EmGfxGsPrim *p = &prims[i];
        const uint32_t tme = (p->prim >> 4) & 1u;
        const uint32_t k[8] = { tme ? texs[i]->width : 1u, tme ? texs[i]->height : 1u, fogcol,
                                (p->prim >> 5) & 1u, (uint32_t)p->alpha & 0xFFu, (uint32_t)(p->alpha >> 32) & 0xFFu,
                                tme ? (uint32_t)(p->tex0 >> 35) & 3u : 0u,
                                tme && !((p->tex0 >> 34) & 1u) ? 1u : 0u };
        [g->enc setRenderPipelineState:tme ? g->gsTexPipeline : g->gsFlatPipeline];
        if (tme) [g->enc setFragmentTexture:texs[i]->tex atIndex:0];
        [g->enc setFragmentBytes:k length:sizeof k atIndex:0];
        [g->enc drawPrimitives:((p->prim & 7u) == 1u || (p->prim & 7u) == 2u) ? MTLPrimitiveTypeLine
                                                                              : MTLPrimitiveTypeTriangle
                   vertexStart:first[2u * i] vertexCount:first[2u * i + 1u]];
    }
    [vb release];
    free(first);
    free(texs);
    return 0;
}

/* --- The GS frame (em_gfx_gs_frame — em_gfx.h) ---------------------------- */

/* The GS pixel path of a frame drawn wholly by its GS list, at the GS's own
 * resolution into GS-memory surfaces (em_gfx.h "The GS frame"). Positions
 * arrive in NDC of the target surface with the window coordinate w mapped to
 * w + 0.5, so each Metal pixel centre is a GS integer sample point. RGBA,
 * S / T / Q and U / V are screen-linear (the GS interpolates them so); a
 * sprite's U / V (or S / Q, T / Q) are evaluated at the sample point from its
 * two corners (spr0: window X0, Y0, X1, Y1 and spr1: the corners' texel
 * coordinates, all in 1/16). Texels are the surface's bytes, read with the
 * GS rules (nearest at floor(U), or bilinear at U - 0.5 with 4-bit weights),
 * each coordinate wrapped by CLAMP_1. Then TFX / TCC, the alpha test (ATST,
 * AREF, AFAIL), the blend ((A - B) * C >> 7) + D, COLCLAMP, FBA and FBMSK
 * on the surface pixel (framebuffer fetch).
 * k[0] = (flags, 2^TW, 2^TH, 0); k[1] = (AREF, ALPHA selectors, FIX, FBMSK);
 * k[2] = (WMS | WMT << 2, MINU | MAXU << 16, MINV | MAXV << 16, 0).
 * flags: 1 TME, 2 FST, 4 sprite, 8 ABE, 16 bilinear, 32 TCC, TFX << 6,
 * 256 ATE, ATST << 9, AFAIL << 12, 16384 FBA, 32768 COLCLAMP, 131072 line
 * (spr0 / spr1 carry its two vertex colours: a pixel Metal's line rule
 * places past an end takes no colour beyond the ends', as the GS line
 * steps from one vertex to the other).
 * APPROXIMATION: coverage is Metal's (its triangle and line rules) and the
 * interpolated values are Metal's float interpolation, floored with a 0.001
 * epsilon; the GS DDA is not modelled. */
static NSString *const kGsFrameShaderSrc =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct FV { float4 pos; float4 rgba; float4 stq; float4 spr0; float4 spr1; };\n"
"struct FOut { float4 pos [[position]];\n"
"              float4 rgba [[center_no_perspective]];\n"
"              float4 stq [[center_no_perspective]];\n"
"              float4 spr0 [[flat]]; float4 spr1 [[flat]]; };\n"
"vertex FOut v_gsf(uint vid [[vertex_id]], const device FV *v [[buffer(0)]]) {\n"
"    FOut o; o.pos = v[vid].pos; o.rgba = v[vid].rgba; o.stq = v[vid].stq;\n"
"    o.spr0 = v[vid].spr0; o.spr1 = v[vid].spr1; return o;\n"
"}\n"
"static int wrapc(int x, uint mode, int size, int lo, int hi) {\n"
"    if (mode == 0u) return x & (size - 1);\n"
"    if (mode == 1u) return clamp(x, 0, size - 1);\n"
"    if (mode == 2u) return clamp(x, lo, hi);\n"
"    return (x & lo) | hi;\n"
"}\n"
"static uint4 texel(texture2d<float, access::read> t, int x, int y) {\n"
"    return uint4(round(t.read(uint2(uint(x), uint(y))) * 255.0));\n"
"}\n"
"fragment float4 f_gsf(FOut in [[stage_in]], float4 dstf [[color(0)]],\n"
"                      texture2d<float, access::read> tex [[texture(0)]],\n"
"                      constant uint4 *k [[buffer(0)]]) {\n"
"    uint fl = k[0].x;\n"
"    uint4 dst = uint4(round(dstf * 255.0));\n"
"    float4 rgba = in.rgba;\n"
"    if ((fl & 131072u) != 0u) rgba = clamp(rgba, min(in.spr0, in.spr1), max(in.spr0, in.spr1));\n"
"    uint4 cv = uint4(clamp(floor(rgba + 0.001), 0.0, 255.0));\n"
"    uint3 c = cv.rgb; uint a = cv.a;\n"
"    if ((fl & 1u) != 0u) {\n"
"        int tw = int(k[0].y), th = int(k[0].z);\n"
"        float u16, v16;\n"
"        if ((fl & 4u) != 0u) {\n"
"            float px = (in.pos.x - 0.5) * 16.0, py = (in.pos.y - 0.5) * 16.0;\n"
"            u16 = in.spr1.x + (px - in.spr0.x) * (in.spr1.z - in.spr1.x) / (in.spr0.z - in.spr0.x);\n"
"            v16 = in.spr1.y + (py - in.spr0.y) * (in.spr1.w - in.spr1.y) / (in.spr0.w - in.spr0.y);\n"
"        } else if ((fl & 2u) != 0u) {\n"
"            u16 = in.stq.x; v16 = in.stq.y;\n"
"        } else {\n"
"            u16 = in.stq.x / in.stq.z * float(tw) * 16.0;\n"
"            v16 = in.stq.y / in.stq.z * float(th) * 16.0;\n"
"        }\n"
"        uint wms = k[2].x & 3u, wmt = (k[2].x >> 2) & 3u;\n"
"        int minu = int(k[2].y & 0xFFFFu), maxu = int(k[2].y >> 16);\n"
"        int minv = int(k[2].z & 0xFFFFu), maxv = int(k[2].z >> 16);\n"
"        uint4 t;\n"
"        if ((fl & 16u) != 0u) {\n"
"            int uu = int(floor(u16)) - 8, vv = int(floor(v16)) - 8;\n"
"            int fu = uu & 15, fv = vv & 15;\n"
"            int x0 = wrapc(uu >> 4, wms, tw, minu, maxu), x1 = wrapc((uu >> 4) + 1, wms, tw, minu, maxu);\n"
"            int y0 = wrapc(vv >> 4, wmt, th, minv, maxv), y1 = wrapc((vv >> 4) + 1, wmt, th, minv, maxv);\n"
"            t = (texel(tex, x0, y0) * uint((16 - fu) * (16 - fv)) + texel(tex, x1, y0) * uint(fu * (16 - fv)) +\n"
"                 texel(tex, x0, y1) * uint((16 - fu) * fv) + texel(tex, x1, y1) * uint(fu * fv)) >> 8;\n"
"        } else {\n"
"            t = texel(tex, wrapc(int(floor(u16)) >> 4, wms, tw, minu, maxu),\n"
"                      wrapc(int(floor(v16)) >> 4, wmt, th, minv, maxv));\n"
"        }\n"
"        uint tfx = (fl >> 6) & 3u; bool tcc = (fl & 32u) != 0u;\n"
"        if (tfx == 1u) { c = t.rgb; a = tcc ? t.a : a; }\n"
"        else {\n"
"            uint3 m = min((t.rgb * c) >> 7, uint3(255));\n"
"            if (tfx == 0u) { c = m; a = tcc ? min((t.a * a) >> 7, 255u) : a; }\n"
"            else { c = min(m + a, uint3(255)); a = tcc ? (tfx == 2u ? min(t.a + a, 255u) : t.a) : a; }\n"
"        }\n"
"    }\n"
"    bool pass = true;\n"
"    if ((fl & 256u) != 0u) {\n"
"        uint atst = (fl >> 9) & 7u, aref = k[1].x;\n"
"        pass = atst == 1u || (atst == 2u && a < aref) || (atst == 3u && a <= aref) ||\n"
"               (atst == 4u && a == aref) || (atst == 5u && a >= aref) || (atst == 6u && a > aref) ||\n"
"               (atst == 7u && a != aref);\n"
"    }\n"
"    uint afail = (fl >> 12) & 3u;\n"
"    bool wrgb = pass || afail == 1u || afail == 3u, wa = pass || afail == 1u;\n"
"    if (!wrgb) return dstf;\n"
"    uint3 o = c;\n"
"    if ((fl & 8u) != 0u) {\n"
"        int3 cs = int3(c), cd = int3(dst.rgb);\n"
"        uint sel = k[1].y;\n"
"        int3 va = (sel & 3u) == 0u ? cs : (sel & 3u) == 1u ? cd : int3(0);\n"
"        int3 vb = ((sel >> 2) & 3u) == 0u ? cs : ((sel >> 2) & 3u) == 1u ? cd : int3(0);\n"
"        int vc = ((sel >> 4) & 3u) == 0u ? int(a) : ((sel >> 4) & 3u) == 1u ? int(dst.a) : int(k[1].z);\n"
"        int3 vd = ((sel >> 6) & 3u) == 0u ? cs : ((sel >> 6) & 3u) == 1u ? cd : int3(0);\n"
"        int3 r = (((va - vb) * vc) >> 7) + vd;\n"
"        o = (fl & 32768u) != 0u ? uint3(clamp(r, 0, 255)) : uint3(r) & uint3(255u);\n"
"    }\n"
"    uint oa = (a | ((fl & 16384u) != 0u ? 128u : 0u)) & 255u;\n"
"    uint4 outv = uint4(o, wa ? oa : dst.a);\n"
"    uint msk = k[1].w;\n"
"    uint4 m4 = uint4(msk & 255u, (msk >> 8) & 255u, (msk >> 16) & 255u, msk >> 24);\n"
"    outv = (outv & ~m4) | (dst & m4);\n"
"    return float4(outv) / 255.0;\n"
"}\n"
"struct SOut { float4 pos [[position]]; float2 tc [[center_no_perspective]]; };\n"
"vertex SOut v_gsshow(uint vid [[vertex_id]], const device float4 *v [[buffer(0)]]) {\n"
"    SOut o; o.pos = float4(v[vid].xy, 0.0, 1.0); o.tc = v[vid].zw; return o;\n"
"}\n"
"fragment float4 f_gsshow(SOut in [[stage_in]], texture2d<float, access::read> tex [[texture(0)]],\n"
"                         constant uint4 *k [[buffer(0)]]) {\n"
"    float2 size = float2(k[0].xy);\n"
"    uint2 p = uint2(min(floor(in.tc * size), size - 1.0));\n"
"    return float4(tex.read(p).rgb, 1.0);\n"
"}\n";

enum {
    GSF_WARN_FRAME = 1u, GSF_WARN_STATE = 2u, GSF_WARN_SURFACE = 4u, GSF_WARN_GPU = 8u,
    GSF_WARN_INPUT = 16u,
};

static int gsf_fail(EmGfx *g, uint32_t why, const char *what, uint64_t detail)
{
    if (g && !(g->gsfWarned & why)) {
        g->gsfWarned |= why;
        fprintf(stderr, "gfx: GS frame: %s (%#llx) — not drawn\n", what, (unsigned long long)detail);
    }
    return -1;
}

static struct EmGfxGsSurface *gsf_find(EmGfx *g, uint32_t fbp, uint32_t fbw)
{
    for (uint32_t i = 0; i < g->gsSurfCount; i++)
        if (g->gsSurf[i].fbp == fbp && g->gsSurf[i].fbw == fbw) return &g->gsSurf[i];
    return NULL;
}

/* The surface a FRAME_1 draws into, created (zeroed) at its first use with
 * the height its SCISSOR reaches. NULL on a refusal. */
static struct EmGfxGsSurface *gsf_target(EmGfx *g, id<MTLCommandBuffer> cb, uint64_t frame, uint64_t scissor,
                                         const char **why)
{
    const uint32_t fbp = (uint32_t)frame & 0x1FFu, fbw = (uint32_t)(frame >> 16) & 0x3Fu;
    const uint32_t psm = (uint32_t)(frame >> 24) & 0x3Fu;
    const uint32_t sx1 = (uint32_t)(scissor >> 16) & 0x7FFu, sy1 = (uint32_t)(scissor >> 48) & 0x7FFu;
    if (psm != 0u) { *why = "a FRAME_1 other than PSMCT32"; return NULL; }
    if (fbw == 0u || sx1 >= 64u * fbw) { *why = "a SCISSOR_1 wider than FRAME_1's buffer"; return NULL; }
    struct EmGfxGsSurface *s = gsf_find(g, fbp, fbw);
    if (s) {
        if (sy1 >= s->height) { *why = "a SCISSOR_1 below the surface's first height"; return NULL; }
        return s;
    }
    if (g->gsSurfCount >= EM_GFX_GS_SURFACE_MAX) { *why = "too many GS surfaces"; return NULL; }
    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64u * fbw height:sy1 + 1u mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [g->device newTextureWithDescriptor:td];
    if (!t) { *why = "surface allocation failed"; return NULL; }
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = t;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    [enc endEncoding];
    s = &g->gsSurf[g->gsSurfCount++];
    s->fbp = fbp;
    s->fbw = fbw;
    s->height = sy1 + 1u;
    s->tex = t;
    return s;
}

/* One primitive's draw state: its uniform, target, texture and scissor. */
typedef struct {
    uint32_t k[12];
    struct EmGfxGsSurface *target, *texture;
    uint32_t sc[4];          /* scissor x, y, w, h in the target */
    uint32_t line;           /* 1: a line (a separate Metal primitive type) */
} GsfState;

static const char *gsf_state(EmGfx *g, id<MTLCommandBuffer> cb, const EmGfxGsPrim *p, const EmGfxGsEnv *e,
                             GsfState *st)
{
    const uint32_t type = p->prim & 7u, iip = (p->prim >> 3) & 1u, tme = (p->prim >> 4) & 1u;
    const uint32_t fge = (p->prim >> 5) & 1u, abe = (p->prim >> 6) & 1u, fst = (p->prim >> 8) & 1u;
    (void)iip;
    memset(st, 0, sizeof *st);
    if (p->prim & 0x680u) return "PRIM AA1 / CTXT / FIX";
    if (fge) return "a fogged primitive";
    if (type == 0u || type == 7u) return "a point or an unknown PRIM type";
    if ((type == 1u || type == 2u) ? p->count != 2u : type == 6u ? p->count != 2u : p->count != 3u)
        return "a vertex count the PRIM type does not draw";
    const uint32_t need_env = EM_GFX_GS_ENV_FRAME | EM_GFX_GS_ENV_XYOFFSET | EM_GFX_GS_ENV_SCISSOR |
                              EM_GFX_GS_ENV_PRMODECONT;
    if ((e->set & need_env) != need_env) return "an environment the list did not set";
    if (!(e->prmodecont & 1u)) return "PRMODECONT 0 (PRMODE attributes)";
    if ((e->set & EM_GFX_GS_ENV_DTHE) && (e->dthe & 1u)) return "dithering";
    if ((e->set & EM_GFX_GS_ENV_PABE) && (e->pabe & 1u)) return "PABE";
    if ((e->set & EM_GFX_GS_ENV_SCANMSK) && (e->scanmsk & 3u)) return "SCANMSK";
    const uint32_t need = EM_GFX_GS_TEST | EM_GFX_GS_COLCLAMP | (abe ? EM_GFX_GS_ALPHA : 0u) |
                          (tme ? EM_GFX_GS_TEX0 | EM_GFX_GS_TEX1 | EM_GFX_GS_CLAMP : 0u);
    if ((p->set & need) != need) return "a drawing state the list did not set";
    const uint64_t test = p->test;
    if ((test >> 14) & 1u) return "destination alpha test";
    if (((test >> 16) & 1u) && ((test >> 17) & 3u) != 1u) return "a Z test other than ALWAYS";
    const char *why = NULL;
    st->target = gsf_target(g, cb, e->frame, e->scissor, &why);
    if (!st->target) return why;
    uint32_t flags = 0;
    if (tme) {
        const uint64_t t0 = p->tex0;
        const uint32_t tbp = (uint32_t)t0 & 0x3FFFu, tbw = (uint32_t)(t0 >> 14) & 0x3Fu;
        const uint32_t psm = (uint32_t)(t0 >> 20) & 0x3Fu, tw = (uint32_t)(t0 >> 26) & 15u;
        const uint32_t th = (uint32_t)(t0 >> 30) & 15u, tcc = (uint32_t)(t0 >> 34) & 1u;
        const uint32_t tfx = (uint32_t)(t0 >> 35) & 3u;
        if (psm != 0u) return "a TEX0 other than PSMCT32";
        if ((tbp & 31u) || tw > 10u || th > 10u) return "a TEX0 that is not a whole frame-buffer page";
        st->texture = gsf_find(g, tbp >> 5, tbw);
        if (!st->texture) return "a TEX0 that names no drawn surface";
        if (st->texture == st->target) return "a texture read of the surface it draws into";
        const uint64_t t1 = p->tex1;
        const uint32_t mmag = (uint32_t)(t1 >> 5) & 1u, mmin = (uint32_t)(t1 >> 6) & 7u;
        if ((t1 & 1u) || ((t1 >> 2) & 7u) || mmin > 1u || mmag != mmin)
            return "a TEX1_1 other than MMAG == MMIN nearest / bilinear without mipmaps";
        const uint64_t cl = p->clamp;
        const uint32_t wms = (uint32_t)cl & 3u, wmt = (uint32_t)(cl >> 2) & 3u;
        const uint32_t minu = (uint32_t)(cl >> 4) & 0x3FFu, maxu = (uint32_t)(cl >> 14) & 0x3FFu;
        const uint32_t minv = (uint32_t)(cl >> 24) & 0x3FFu, maxv = (uint32_t)(cl >> 34) & 0x3FFu;
        /* The texels a coordinate can reach must lie in the surface. */
        const uint32_t reach_u = wms == 2u ? maxu : wms == 3u ? (minu | maxu) : (1u << tw) - 1u;
        const uint32_t reach_v = wmt == 2u ? maxv : wmt == 3u ? (minv | maxv) : (1u << th) - 1u;
        if (reach_u >= 64u * st->texture->fbw || reach_v >= st->texture->height ||
            (wms == 2u && minu > maxu) || (wmt == 2u && minv > maxv))
            return "a CLAMP_1 whose texels are outside the surface";
        flags |= 1u | (fst ? 2u : 0u) | (mmag ? 16u : 0u) | (tcc ? 32u : 0u) | (tfx << 6);
        st->k[1] = 1u << tw;
        st->k[2] = 1u << th;
        st->k[8] = wms | wmt << 2;
        st->k[9] = minu | maxu << 16;
        st->k[10] = minv | maxv << 16;
    }
    if (type == 6u) flags |= 4u;
    if (type == 1u || type == 2u) flags |= 131072u;
    if (abe) {
        const uint32_t a = (uint32_t)p->alpha & 0xFFu;
        if ((a & 3u) == 3u || ((a >> 2) & 3u) == 3u || ((a >> 4) & 3u) == 3u || ((a >> 6) & 3u) == 3u)
            return "a reserved ALPHA_1 selector";
        flags |= 8u;
        st->k[5] = a;
        st->k[6] = (uint32_t)(p->alpha >> 32) & 0xFFu;
    }
    if (test & 1u) flags |= 256u | (uint32_t)((test >> 1) & 7u) << 9 | (uint32_t)((test >> 12) & 3u) << 12;
    st->k[4] = (uint32_t)(test >> 4) & 0xFFu;
    if ((e->set & EM_GFX_GS_ENV_FBA) && (e->fba & 1u)) flags |= 16384u;
    if (p->colclamp & 1u) flags |= 32768u;
    st->k[7] = (uint32_t)(e->frame >> 32);
    st->k[0] = flags;
    const uint32_t sx0 = (uint32_t)e->scissor & 0x7FFu, sx1 = (uint32_t)(e->scissor >> 16) & 0x7FFu;
    const uint32_t sy0 = (uint32_t)(e->scissor >> 32) & 0x7FFu, sy1 = (uint32_t)(e->scissor >> 48) & 0x7FFu;
    if (sx0 > sx1 || sy0 > sy1) return "an empty SCISSOR_1";
    st->sc[0] = sx0; st->sc[1] = sy0; st->sc[2] = sx1 - sx0 + 1u; st->sc[3] = sy1 - sy0 + 1u;
    st->line = type == 1u || type == 2u;
    return NULL;
}

/* One vertex: position in the target's NDC, colour, texture coordinates. */
static void gsf_put(float *o, const EmGfxGsVertex *v, const EmGfxGsEnv *e, const struct EmGfxGsSurface *t,
                    const uint8_t rgba[4], uint32_t fst)
{
    const float wx = ((float)v->x - (float)(e->xyoffset & 0xFFFFu)) / 16.0f;
    const float wy = ((float)v->y - (float)((e->xyoffset >> 32) & 0xFFFFu)) / 16.0f;
    memset(o, 0, 20 * sizeof(float));
    o[0] = (wx + 0.5f) / (float)(64u * t->fbw) * 2.0f - 1.0f;
    o[1] = 1.0f - (wy + 0.5f) / (float)t->height * 2.0f;
    o[3] = 1.0f;
    for (unsigned c = 0; c < 4u; c++) o[4 + c] = (float)rgba[c];
    if (fst) {
        o[8] = (float)v->u;
        o[9] = (float)v->v;
    } else {
        o[8] = bits_f(v->s);
        o[9] = bits_f(v->t);
        o[10] = bits_f(v->q);
    }
}

int em_gfx_gs_frame(EmGfx *g, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count,
                    uint64_t display_frame, uint64_t display_scissor)
{
    /* The GS frame: the list frame runs through the CPU GS model. */
    if (g && g->gswOn) {
        if (!g->enc) return gsw_fail(g, "a list frame outside a frame");
        if (count && (!prims || !envs)) return gsw_fail(g, "a list frame without its primitives");
        return gsw_list_frame(g, prims, envs, count, display_frame, display_scissor);
    }
    if (!g || !g->enc) return gsf_fail(g, GSF_WARN_FRAME, "outside a frame", 0);
    if (count && (!prims || !envs)) return gsf_fail(g, GSF_WARN_INPUT, "no primitives", 0);
    if (![g->device supportsFamily:MTLGPUFamilyApple1])
        return gsf_fail(g, GSF_WARN_GPU, "the GPU has no framebuffer fetch (the GS blend)", 0);
    if (!g->gsfPipeline)
        g->gsfPipeline = shadow_pipeline(g, kGsFrameShaderSrc, @"v_gsf", @"f_gsf", MTLPixelFormatRGBA8Unorm,
                                         false, MTLColorWriteMaskAll);
    if (!g->gsfShowPipeline)
        g->gsfShowPipeline = shadow_pipeline(g, kGsFrameShaderSrc, @"v_gsshow", @"f_gsshow",
                                             g->layer.pixelFormat, true, MTLColorWriteMaskAll);
    if (!g->gsfNoTexture) {
        MTLTextureDescriptor *td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        g->gsfNoTexture = [g->device newTextureWithDescriptor:td];
    }
    if (!g->gsfPipeline || !g->gsfShowPipeline || !g->gsfNoTexture)
        return gsf_fail(g, GSF_WARN_GPU, "pipeline unavailable", 0);
    GsfState *st = calloc(count ? count : 1u, sizeof *st);
    float *v = malloc(sizeof(float) * 20u * 6u * (count ? count : 1u));
    uint32_t *first = malloc(sizeof(uint32_t) * 2u * (count ? count : 1u));
    if (!st || !v || !first) {
        free(st); free(v); free(first);
        return gsf_fail(g, GSF_WARN_INPUT, "out of memory", count);
    }
    id<MTLCommandBuffer> cb = [g->queue commandBuffer];
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        const EmGfxGsPrim *p = &prims[i];
        const char *why = gsf_state(g, cb, p, &envs[i], &st[i]);
        if (why) {
            [cb commit];
            free(st); free(v); free(first);
            return gsf_fail(g, GSF_WARN_STATE, why, p->prim);
        }
        const uint32_t type = p->prim & 7u, iip = (p->prim >> 3) & 1u, fst = (p->prim >> 8) & 1u;
        const EmGfxGsVertex *last = &p->v[p->count - 1u];
        first[2u * i] = n;
        if (type == 6u) {
            /* The GS sprite: colour and Z of the second vertex; the corners'
             * texel coordinates in 1/16 for the per-pixel evaluation. */
            const EmGfxGsVertex *a = &p->v[0], *b = &p->v[1];
            const EmGfxGsEnv *e = &envs[i];
            const float ox = (float)(e->xyoffset & 0xFFFFu), oy = (float)((e->xyoffset >> 32) & 0xFFFFu);
            float spr1[4] = { 0, 0, 0, 0 };
            if ((p->prim >> 4) & 1u) {
                if (fst) {
                    spr1[0] = (float)a->u; spr1[1] = (float)a->v; spr1[2] = (float)b->u; spr1[3] = (float)b->v;
                } else {
                    const float tw = (float)st[i].k[1], th = (float)st[i].k[2];
                    spr1[0] = bits_f(a->s) / bits_f(a->q) * tw * 16.0f;
                    spr1[1] = bits_f(a->t) / bits_f(a->q) * th * 16.0f;
                    spr1[2] = bits_f(b->s) / bits_f(b->q) * tw * 16.0f;
                    spr1[3] = bits_f(b->t) / bits_f(b->q) * th * 16.0f;
                }
            }
            const float spr0[4] = { (float)a->x - ox, (float)a->y - oy, (float)b->x - ox, (float)b->y - oy };
            if (spr0[0] == spr0[2] || spr0[1] == spr0[3]) {
                first[2u * i + 1u] = 0;     /* an empty sprite draws nothing */
                continue;
            }
            static const unsigned corner[6][2] = { {0, 0}, {1, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 1} };
            for (unsigned c = 0; c < 6u; c++) {
                EmGfxGsVertex q = *b;
                q.x = corner[c][0] ? b->x : a->x;
                q.y = corner[c][1] ? b->y : a->y;
                float *o = v + (size_t)(n + c) * 20u;
                gsf_put(o, &q, e, st[i].target, b->rgba, 1u);
                memcpy(o + 12, spr0, sizeof spr0);
                memcpy(o + 16, spr1, sizeof spr1);
            }
            n += 6u;
        } else {
            for (uint32_t c = 0; c < p->count; c++) {
                float *o = v + (size_t)(n + c) * 20u;
                gsf_put(o, &p->v[c], &envs[i], st[i].target, iip ? p->v[c].rgba : last->rgba, fst);
                if (st[i].line)
                    for (unsigned q = 0; q < 4u; q++) {
                        o[12 + q] = (float)(iip ? p->v[0].rgba[q] : last->rgba[q]);
                        o[16 + q] = (float)last->rgba[q];
                    }
            }
            n += p->count;
        }
        first[2u * i + 1u] = n - first[2u * i];
    }
    id<MTLBuffer> vb = n ? [g->device newBufferWithBytes:v length:sizeof(float) * 20u * n
                                                 options:MTLResourceStorageModeShared]
                         : nil;
    free(v);
    /* Runs of primitives with one state become one draw (the Apple GPU keeps
     * framebuffer-fetch order within a draw); a new target starts a pass. */
    id<MTLRenderCommandEncoder> enc = nil;
    struct EmGfxGsSurface *cur = NULL;
    for (uint32_t i = 0; i < count;) {
        uint32_t j = i + 1u;
        while (j < count && st[j].target == st[i].target && st[j].texture == st[i].texture &&
               st[j].line == st[i].line && memcmp(st[j].k, st[i].k, sizeof st[i].k) == 0 &&
               memcmp(st[j].sc, st[i].sc, sizeof st[i].sc) == 0 &&
               first[2u * j] == first[2u * (j - 1u)] + first[2u * (j - 1u) + 1u])
            j++;
        uint32_t verts = 0;
        for (uint32_t m = i; m < j; m++) verts += first[2u * m + 1u];
        if (verts) {
            if (st[i].target != cur) {
                if (enc) [enc endEncoding];
                MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
                rp.colorAttachments[0].texture = st[i].target->tex;
                rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
                rp.colorAttachments[0].storeAction = MTLStoreActionStore;
                enc = [cb renderCommandEncoderWithDescriptor:rp];
                cur = st[i].target;
                [enc setRenderPipelineState:g->gsfPipeline];
                [enc setCullMode:MTLCullModeNone];
                [enc setViewport:(MTLViewport){ 0.0, 0.0, 64.0 * cur->fbw, cur->height, 0.0, 1.0 }];
                [enc setVertexBuffer:vb offset:0 atIndex:0];
            }
            const uint32_t w = cur->fbw * 64u, h = cur->height;
            const uint32_t x0 = st[i].sc[0] < w ? st[i].sc[0] : w, y0 = st[i].sc[1] < h ? st[i].sc[1] : h;
            const uint32_t sw = st[i].sc[2] < w - x0 ? st[i].sc[2] : w - x0;
            const uint32_t sh = st[i].sc[3] < h - y0 ? st[i].sc[3] : h - y0;
            if (sw && sh) {
                [enc setScissorRect:(MTLScissorRect){ x0, y0, sw, sh }];
                const uint32_t k[16] = { st[i].k[0], st[i].k[1], st[i].k[2], 0, st[i].k[4], st[i].k[5],
                                         st[i].k[6], st[i].k[7], st[i].k[8], st[i].k[9], st[i].k[10], 0,
                                         0, 0, 0, 0 };
                [enc setFragmentBytes:k length:sizeof k atIndex:0];
                [enc setFragmentTexture:st[i].texture ? st[i].texture->tex : g->gsfNoTexture atIndex:0];
                [enc drawPrimitives:st[i].line ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle
                        vertexStart:first[2u * i] vertexCount:verts];
            }
        }
        i = j;
    }
    if (enc) [enc endEncoding];
    /* Committed now: it runs before the frame's command buffer (committed by
     * end_frame), whose game rectangle shows the displayed surface. */
    [cb commit];
    [vb release];
    free(st);
    free(first);
    /* Show the displayed surface over the whole game rectangle. */
    const uint32_t dfbp = (uint32_t)display_frame & 0x1FFu, dfbw = (uint32_t)(display_frame >> 16) & 0x3Fu;
    const uint32_t dh = (uint32_t)(display_scissor >> 48) & 0x7FFu;
    struct EmGfxGsSurface *d = gsf_find(g, dfbp, dfbw);
    if (!d || dh + 1u > d->height)
        return gsf_fail(g, GSF_WARN_SURFACE, "the displayed frame buffer was not drawn", display_frame);
    ensure_depth_states(g);
    static const float quad[6][4] = {
        { -1.0f,  1.0f, 0.0f, 0.0f }, { 1.0f,  1.0f, 1.0f, 0.0f }, { -1.0f, -1.0f, 0.0f, 1.0f },
        {  1.0f,  1.0f, 1.0f, 0.0f }, { 1.0f, -1.0f, 1.0f, 1.0f }, { -1.0f, -1.0f, 0.0f, 1.0f },
    };
    const uint32_t k[4] = { 64u * d->fbw, dh + 1u, 0u, 0u };
    [g->enc setRenderPipelineState:g->gsfShowPipeline];
    [g->enc setDepthStencilState:g->depthOff];
    [g->enc setCullMode:MTLCullModeNone];
    [g->enc setVertexBytes:quad length:sizeof quad atIndex:0];
    [g->enc setFragmentTexture:d->tex atIndex:0];
    [g->enc setFragmentBytes:k length:sizeof k atIndex:0];
    [g->enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    return 0;
}

int em_gfx_gs_surface_read(EmGfx *g, uint32_t fbp, uint32_t fbw, uint32_t height, uint8_t *rgba)
{
    if (!g || !rgba) return -1;
    if (g->gswOn) return em_gs_world_read(g->gsw, fbp, fbw, 64u * fbw, height, rgba);
    struct EmGfxGsSurface *s = gsf_find(g, fbp, fbw);
    if (!s || height > s->height || !height) return -1;
    @autoreleasepool {   /* called outside begin/end_frame */
    const NSUInteger row = 64u * fbw * 4u;
    id<MTLBuffer> buf = [g->device newBufferWithLength:row * height options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [g->queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:s->tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(64u * fbw, height, 1) toBuffer:buf destinationOffset:0
      destinationBytesPerRow:row destinationBytesPerImage:row * height];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    memcpy(rgba, buf.contents, row * height);
    [buf release];
    }
    return 0;
}

/* --- The Original profile's GS frame (em_gfx_gs_world_* — em_gfx.h) ------
 * A world frame's GS draws are recorded into the CPU GS model
 * (src/gs/em_gs_world.h) in the order they arrive, with the original state
 * blocks their lists REF read by address (gswRead); the kick runs the frame
 * and end_frame shows the field. The register values that are not in a
 * block the game owns are the ones em_shadow_gs.h documents (checked against
 * the captured chains by tools/test_shadow_original_reference.py). */

#define GSW_CLASS0_SET   0x00815360u   /* 001D1F80(0, 1, 0): set 1, class 0 */
#define GSW_SHADOW_2_9   0x00815E10u   /* 001D1F80(a0, 2, 9): 001DA290 / 001DA310 */
#define GSW_SILHOUETTE   0x00814DC0u   /* 001D1F80(., 0, 0): 001D9EE0's set */
#define GSW_SHADOW_2_6   0x00815C60u   /* 001D1F80(., 2, 6): 001D4CD0 */
#define GSW_CLAMP_BLOCK  0x008146C0u   /* 001D1FF0(., 0): CLAMP 5; (., 1): CLAMP 0 */
#define GSW_ALPHA_ROWS   0x002531D0u   /* D_002531D0: 001DA290's four strip rows */

static int gsw_fail(EmGfx *g, const char *what)
{
    if (!g->gswWhy[0]) {
        snprintf(g->gswWhy, sizeof g->gswWhy, "%s", what);
        fprintf(stderr, "gfx: GS frame: %s\n", what);
    }
    return -1;
}

/* The model's latched fault, if any, as the backend's. */
static int gsw_check(EmGfx *g)
{
    const char *why = em_gs_world_fault(g->gsw);
    return why ? gsw_fail(g, why) : 0;
}

/* A state block the list REFs: `qwords` at `address` (its FLUSH / DIRECT
 * codes and the GIF data), recorded at this point of the frame. */
static int gsw_block(EmGfx *g, uint32_t address, uint32_t qwords)
{
    const uint8_t *p = g->gswRead ? g->gswRead(g->gswReadCtx, address, qwords * 16u) : NULL;
    if (!p) return gsw_fail(g, "a state block the game's memory does not map");
    uint8_t gif[0x200];
    size_t used = 0;
    if (qwords * 16u > sizeof gif || em_gs_vif_direct(p, qwords * 16u, 0, gif, sizeof gif, &used) < 0 || !used)
        return gsw_fail(g, "a state block that is not VIF DIRECT GIF data");
    em_gs_world_gif(g->gsw, gif, used);
    return gsw_check(g);
}

static EmGfxGsPrim *gsw_prims(EmGfx *g, uint32_t count)
{
    if (count > g->gswPrimCap) {
        EmGfxGsPrim *n = realloc(g->gswPrims, sizeof *n * count);
        if (!n) return NULL;
        g->gswPrims = n;
        g->gswPrimCap = count;
    }
    return g->gswPrims;
}

static uint32_t gsw_fbits(float f)
{
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return b;
}

/* One kicked XYZF2 vertex of the shadow kernels (PACKED words: X, Y, Z << 4,
 * F << 4) with the vertex's RGBAQ / ST. */
static void gsw_vertex_xyzf(EmGfxGsVertex *o, const int32_t w[4], const uint8_t rgba[4], uint32_t q,
                            uint32_t s, uint32_t t)
{
    memset(o, 0, sizeof *o);
    o->x = (uint16_t)((uint32_t)w[0] & 0xFFFFu);
    o->y = (uint16_t)((uint32_t)w[1] & 0xFFFFu);
    o->z = ((uint32_t)w[2] >> 4) & 0xFFFFFFu;
    o->f = (uint8_t)(((uint32_t)w[3] >> 4) & 0xFFu);
    o->has_f = 1;
    memcpy(o->rgba, rgba, 4);
    o->q = q;
    o->s = s;
    o->t = t;
}

int em_gfx_gs_world_enable(EmGfx *g, int on, EmGfxGsRead read, void *read_ctx)
{
    if (!g) return -1;
    if (!on) {
        g->gswOn = false;
        return 0;
    }
    if (!read) return -1;
    if (!g->gsw && !(g->gsw = em_gs_world_create())) return -1;
    g->gswRead = read;
    g->gswReadCtx = read_ctx;
    g->gswOn = true;
    return 0;
}

int em_gfx_gs_world_enabled(EmGfx *g) { return g && g->gswOn; }

int em_gfx_gs_memory_load(EmGfx *g, const char *path)
{
    if (!g || !g->gsw) return -1;
    if (em_gs_world_memory_load(g->gsw, path) < 0) return gsw_check(g);
    return 0;
}

int em_gfx_gs_upload(EmGfx *g, const uint8_t *chain, size_t bytes)
{
    if (!g || !g->gswOn) return 0;
    gsw_complete(g);   /* the frame before is shown before memory changes */
    if (em_gs_world_upload_chain(g->gsw, chain, bytes) < 0) return gsw_check(g);
    return 0;
}

int em_gfx_gs_world_frame(EmGfx *g)
{
    if (!g || !g->gswOn) return 0;
    if (g->gswWhy[0]) return -1;
    if (!em_gs_world_memory_loaded(g->gsw)) return gsw_fail(g, "a world frame before the GS memory image");
    em_gs_world_begin(g->gsw);
    g->gswFrame = true;
    return 1;
}

void em_gfx_gs_world_drop(EmGfx *g)
{
    if (g) g->gswFrame = false;   /* em_gs_world_begin drops the body */
}

int em_gfx_gs_world_kick(EmGfx *g, const void *env, size_t env_bytes, const void *clear, size_t clear_bytes)
{
    if (!g || !g->gswFrame) return 0;
    g->gswFrame = false;
    /* the frame before ends first: its field drawn, uploaded, committed */
    gsw_complete(g);
    if (g->gswWhy[0]) return -1;
    if (gsw_slot(g) < 0) return -1;
    if (em_gs_world_kick(g->gsw, env, env_bytes, clear, clear_bytes) < 0) return gsw_check(g);
    g->gswShow = true;
    return 0;
}

const char *em_gfx_gs_world_fault(EmGfx *g) { return g && g->gswWhy[0] ? g->gswWhy : NULL; }

int em_gfx_gs_field_read(EmGfx *g, uint8_t *rgba, uint64_t *frame)
{
    uint32_t w, h;
    if (!g || !rgba || !g->gsw) return -1;
    gsw_complete(g);
    const uint8_t *f = em_gs_world_field(g->gsw, &w, &h, frame);
    if (!f || w != EM_GS_WORLD_FIELD_W || h != EM_GS_WORLD_FIELD_H) return -1;
    memcpy(rgba, f, (size_t)w * h * 4u);
    return 0;
}

int em_gfx_gs_world_cost(EmGfx *g, EmGfxGsCost *out)
{
    if (!g || !g->gsw || !out) return -1;
    EmGsWorldStats st;
    em_gs_world_stats(g->gsw, &st);
    out->wall_ns = st.ns;
    out->cpu_max_ns = st.cpu_max_ns;
    out->cpu_sum_ns = st.cpu_sum_ns;
    out->pixels = st.pixels;
    out->runs = st.runs;
    out->workers = em_gs_world_workers(g->gsw);
    return 0;
}

void em_gfx_field_presentation(EmGfx *g, EmGfxFieldPresentation mode)
{
    if (g) g->gswPresentation = (int)mode;
}

/* em_gfx_background_prims_env: the channel-3 list's triangles with its
 * environment (ZBUF_1 ZMSK 1) and its own state writes. */
static int gsw_background(EmGfx *g, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count)
{
    em_gs_world_prims(g->gsw, prims, envs, count);
    return gsw_check(g);
}

/* em_gfx_gs_opaque: the static world's run, in the class-0 set its REF names. */
static int gsw_opaque(EmGfx *g, const EmGfxGsPrim *prims, uint32_t count)
{
    if (gsw_block(g, GSW_CLASS0_SET, 9) < 0) return -1;
    em_gs_world_prims(g->gsw, prims, NULL, count);
    return gsw_check(g);
}

/* em_gfx_object_unit: the unit's programs (the same CPU translations), then
 * the triangles they kick in the class-0 set its REF names: pass 0 with the
 * template's PRIM, the clip pass with its own (prim - 1). */
static int gsw_object(EmGfx *g, const EmGfxObjectUnit *unit)
{
    if (unit->gs_class != 0u) return gsw_fail(g, "a class-2 object unit outside the chain page");
    if (em_object_unit_run(unit, &g->objResult)) return gsw_fail(g, "the VU1 programs refused an object unit");
    const EmObjectUnitResult *r = &g->objResult;
    EmGfxGsPrim *p = gsw_prims(g, r->count);
    if (r->count && !p) return gsw_fail(g, "out of memory");
    for (uint32_t i = 0; i < r->count; i++) {
        const EmObjectUnitTriangle *t = &r->tri[i];
        memset(&p[i], 0, sizeof p[i]);
        p[i].prim = t->pass ? r->prim - 1u : r->prim;
        p[i].set = EM_GFX_GS_TEX0;
        p[i].tex0 = t->tex0;
        p[i].count = 3;
        for (unsigned c = 0; c < 3u; c++) {
            const EmObjectUnitVertex *s = &t->v[c];
            EmGfxGsVertex *d = &p[i].v[c];
            d->x = s->x; d->y = s->y; d->z = s->z; d->f = s->f; d->has_f = 1;
            memcpy(d->rgba, s->rgba, 4);
            d->q = s->q; d->s = s->s; d->t = s->t;
        }
    }
    if (gsw_block(g, GSW_CLASS0_SET, 9) < 0) return -1;
    em_gs_world_prims(g->gsw, p, NULL, r->count);
    return gsw_check(g);
}

/* em_gfx_gs_prims: the chain page's primitives, with the state they carry. */
static int gsw_page(EmGfx *g, const EmGfxGsPrim *prims, uint32_t count)
{
    em_gs_world_prims(g->gsw, prims, NULL, count);
    return gsw_check(g);
}

/* The frame's FOGCOL (context +0xB0, which 001D1C50 copies to the GS block):
 * integers 0..255. */
static int gsw_fogcol(EmGfx *g, const float rgb[3])
{
    uint64_t col = 0;
    for (unsigned k = 0; k < 3u; k++) {
        if (!(rgb[k] >= 0.0f && rgb[k] <= 255.0f) || rgb[k] != floorf(rgb[k]))
            return gsw_fail(g, "a FOGCOL that is not 8-bit integers");
        col |= (uint64_t)(uint32_t)rgb[k] << (8u * k);
    }
    em_gs_world_write(g->gsw, EM_GS_FOGCOL, col);
    return gsw_check(g);
}

/* 001DA290: state (2,9), then 001DA1E0's DIRECT strip: the GIF tag (PRE,
 * PRIM 0x044, NLOOP 1, REGS RGBAQ + XYZF2 x 4), RGBAQ (0, 0, 0, a1 = 0) and
 * the four rows of D_002531D0. */
static int gsw_alpha_clear(EmGfx *g)
{
    if (gsw_block(g, GSW_SHADOW_2_9, 9) < 0) return -1;
    const uint8_t *rows = g->gswRead(g->gswReadCtx, GSW_ALPHA_ROWS, 64);
    if (!rows) return gsw_fail(g, "D_002531D0 is not mapped (re-export effect_tables.emet)");
    uint8_t gif[96];
    const uint64_t tag_lo = UINT64_C(0x8001) | (UINT64_C(0x50224000) << 32), tag_hi = 0x44441u;
    memcpy(gif, &tag_lo, 8);
    memcpy(gif + 8, &tag_hi, 8);
    memset(gif + 16, 0, 16);                   /* RGBAQ: R, G, B, A = 0 */
    memcpy(gif + 32, rows, 64);
    em_gs_world_gif(g->gsw, gif, sizeof gif);
    return gsw_check(g);
}

/* 001DA310: one box through 00237180 (its kicked triangles) and 00239C90 (its
 * clipped triangles), flat with the call's RGBAQ, in state (2,9). */
static int gsw_box(EmGfx *g, const EmGfxShadowStrips *model, const float clip[16], uint32_t rgbaq)
{
    float k1021[4] = { 255.0f, 2048.0f, 0.0f, 0.0f }, k1022[4], k1023[4];
    em_shadow_gs_level_rows(k1022, k1023);
    const uint32_t n = model->vertex_count, clip_cap = 30u * 27u;
    const uint8_t rgba[4] = { (uint8_t)rgbaq, (uint8_t)(rgbaq >> 8), (uint8_t)(rgbaq >> 16), (uint8_t)(rgbaq >> 24) };
    EmShadowGsVertex *out = malloc(sizeof *out * n);
    EmVu1Qword *dmem = malloc(sizeof *dmem * EM_VU1_DMEM_QWORDS);
    EmVu1ClipResult *res = shadow_clip_result();
    EmShadowGsClipVertex *gv = malloc(sizeof *gv * clip_cap);
    float (*cp)[4] = malloc(sizeof *cp * clip_cap);
    EmGfxGsPrim *p = gsw_prims(g, n + clip_cap * (n / EM_GFX_SHADOW_BATCH));
    int bad = !out || !dmem || !res || !gv || !cp || !p ? 3 : 0;
    const float (*qw3)[4] = (const float (*)[4])model->qw3;
    uint32_t count = 0;
    for (uint32_t b = 0; b < n && !bad; b += EM_GFX_SHADOW_BATCH) {
        if (em_shadow_gs_level_batch(clip, k1021, k1022, k1023, qw3 + b, EM_GFX_SHADOW_BATCH, out + b)) bad = 1;
        for (uint32_t i = b + 2; i < b + EM_GFX_SHADOW_BATCH && !bad; ++i) {
            if (out[i].why) continue;
            EmGfxGsPrim *t = &p[count++];
            memset(t, 0, sizeof *t);
            t->prim = 0x044u;
            t->count = 3;
            for (unsigned k = 0; k < 3; ++k)
                gsw_vertex_xyzf(&t->v[k], out[i - 2 + k].w, rgba, 0x3F800000u, 0, 0);
        }
    }
    for (uint32_t b = 0; b < n && !bad; b += EM_GFX_SHADOW_BATCH) {
        if (em_shadow_gs_clip_dmem(EM_VU1_CLIP_BOX, clip, NULL, k1021, qw3 + b, EM_SHADOW_GS_CLIP_TOP, dmem) ||
            em_vu1_shadow_clip_run(EM_VU1_CLIP_BOX, dmem, EM_SHADOW_GS_CLIP_TOP, res)) {
            bad = 2;
            break;
        }
        const int v = em_shadow_gs_clip_vertices(EM_VU1_CLIP_BOX, res, gv, clip_cap);
        if (v < 0 || v % 3) { bad = 2; break; }
        for (int k = 0; k < v; k += 3) {
            EmGfxGsPrim *t = &p[count++];
            memset(t, 0, sizeof *t);
            t->prim = 0x043u;
            t->count = 3;
            for (unsigned c = 0; c < 3; ++c) {
                const EmShadowGsClipVertex *s = &gv[k + c];
                const int32_t w[4] = { (int32_t)(s->x * 16.0f), (int32_t)(s->y * 16.0f), (int32_t)(s->z << 4),
                                       (int32_t)(s->f << 4) };
                gsw_vertex_xyzf(&t->v[c], w, rgba, 0x3F800000u, 0, 0);
            }
        }
    }
    free(out); free(dmem); free(res); free(gv); free(cp);
    if (bad) return gsw_fail(g, bad == 3 ? "out of memory" : bad == 1 ? "box strip starts without ADC"
                                                                      : "box clip kernel 00239C90 fault");
    em_gs_world_prims(g->gsw, p, NULL, count);
    return gsw_check(g);
}

/* 001D9EE0: the target packet D_00817E20 (its draw environment and the
 * clear sprite: the values em_shadow_gs.h pins), the set D_00814DC0, the
 * A+D RGBAQ 0xFFFFFF80 and the proxy mesh's triangles through the object
 * kernel's position path (flat, PRIM 0x004; a triangle with an ADC vertex is
 * not kicked). The receivers sample the target from GS memory. */
static int gsw_silhouette(EmGfx *g, const float *verts, uint32_t vert_count, const uint32_t *indices,
                          uint32_t index_count, const float *nodes, uint32_t node_count, const float vp[16])
{
    em_gs_world_write(g->gsw, EM_GS_FRAME_1, EM_SHADOW_GS_FRAME_TARGET);
    /* the target is drawn from here (its readers so far finish first) */
    em_gs_world_drawn_buffer(g->gsw, EM_SHADOW_GS_FRAME_TARGET, EM_SHADOW_GS_TARGET_SIZE);
    em_gs_world_write(g->gsw, EM_GS_ZBUF_1, EM_SHADOW_GS_ZBUF_TARGET);
    em_gs_world_write(g->gsw, EM_GS_XYOFFSET_1, EM_SHADOW_GS_XYOFFSET_TARGET);
    em_gs_world_write(g->gsw, EM_GS_SCISSOR_1, EM_SHADOW_GS_SCISSOR_TARGET);
    em_gs_world_write(g->gsw, EM_GS_PRMODECONT, EM_SHADOW_GS_PRMODECONT_TARGET);
    em_gs_world_write(g->gsw, EM_GS_COLCLAMP, EM_SHADOW_GS_COLCLAMP_TARGET);
    em_gs_world_write(g->gsw, EM_GS_DTHE, EM_SHADOW_GS_DTHE_TARGET);
    em_gs_world_write(g->gsw, EM_GS_TEST_1, EM_SHADOW_GS_TARGET_CLEAR_TEST);
    em_gs_world_write(g->gsw, EM_GS_PRIM, EM_SHADOW_GS_TARGET_CLEAR_PRIM);
    em_gs_world_write(g->gsw, EM_GS_RGBAQ, EM_SHADOW_GS_TARGET_CLEAR_RGBAQ);
    em_gs_world_write(g->gsw, EM_GS_XYZ2, EM_SHADOW_GS_TARGET_CLEAR_XYZ0);
    em_gs_world_write(g->gsw, EM_GS_XYZ2, EM_SHADOW_GS_TARGET_CLEAR_XYZ1);
    em_gs_world_write(g->gsw, EM_GS_TEST_1, EM_SHADOW_GS_TARGET_CLEAR_TEST);
    if (gsw_check(g) < 0 || gsw_block(g, GSW_SILHOUETTE, 9) < 0) return -1;
    float k1021[4] = { 255.0f, 2048.0f, 0.0f, 0.0f }, k1022[4], k1023[4];
    em_shadow_gs_object_rows(k1022, k1023);
    float *bones = malloc(sizeof(float) * 16 * node_count);
    EmShadowGsVertex *sv = malloc(sizeof *sv * vert_count);
    EmGfxGsPrim *p = gsw_prims(g, index_count / 3u);
    if (!bones || !sv || !p) {
        free(bones); free(sv);
        return gsw_fail(g, "out of memory");
    }
    for (uint32_t b = 0; b < node_count; ++b) em_shadow_gs_bone(nodes + 16 * b, vp, bones + 16 * b);
    int bad = 0;
    for (uint32_t i = 0; i < vert_count && !bad; ++i) {
        const float *rec = verts + (size_t)i * 10;
        uint32_t bone;
        memcpy(&bone, rec + 8, 4);
        bone &= EM_GFX_VERT_BONE_MASK;
        if (bone >= node_count) { bad = 1; break; }
        float q3[1][4] = { { rec[0], rec[1], rec[2], 0.0f } };
        const float *bp = bones + 16 * bone;
        em_shadow_gs_object_batch(&bp, k1021, k1022, k1023, (const float (*)[4])q3, 1, &sv[i]);
    }
    const uint8_t rgba[4] = { 0x80, 0xFF, 0xFF, 0xFF };          /* A+D RGBAQ 0xFFFFFF80, Q 0 */
    uint32_t count = 0;
    for (uint32_t t = 0; t + 2 < index_count && !bad; t += 3) {
        const uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
        if (a >= vert_count || b >= vert_count || c >= vert_count) { bad = 1; break; }
        if ((sv[a].why | sv[b].why | sv[c].why) & EM_SHADOW_GS_ADC_CLIP) continue;
        EmGfxGsPrim *o = &p[count++];
        memset(o, 0, sizeof *o);
        o->prim = 0x004u;
        o->count = 3;
        const uint32_t idx[3] = { a, b, c };
        for (unsigned k = 0; k < 3; ++k) {
            EmGfxGsVertex *d = &o->v[k];
            memset(d, 0, sizeof *d);
            d->x = (uint16_t)((uint32_t)sv[idx[k]].w[0] & 0xFFFFu);
            d->y = (uint16_t)((uint32_t)sv[idx[k]].w[1] & 0xFFFFu);
            d->z = (uint32_t)sv[idx[k]].w[2];                     /* XYZ2: the whole Z word */
            memcpy(d->rgba, rgba, 4);
        }
    }
    free(bones); free(sv);
    if (bad) return gsw_fail(g, "silhouette vertex/node out of range");
    em_gs_world_prims(g->gsw, p, NULL, count);
    return gsw_check(g);
}

/* 001D4CD0: the frame's draw environment again, set (2,6), CLAMP block 0
 * (CLAMP 5) and the target's TEX0. */
static int gsw_receiver_begin(EmGfx *g, const float uv[16], const float camera[16])
{
    memcpy(g->shadowUV, uv, 64);
    memcpy(g->shadowCam, camera, 64);
    g->shadowRecvOpen = true;
    em_gs_world_env_again(g->gsw);
    if (gsw_check(g) < 0 || gsw_block(g, GSW_SHADOW_2_6, 9) < 0 || gsw_block(g, GSW_CLAMP_BLOCK, 4) < 0)
        return -1;
    em_gs_world_write(g->gsw, EM_GS_TEX0_1, EM_SHADOW_GS_TEX0_RECEIVER);
    return gsw_check(g);
}

/* 001D4FB0: one receiver through 0023C200 (PRIM 0x07C; ST, RGBAQ (0, 0, 0, a)
 * with the ST's Q, XYZF2) and, for class 2, 0023E8A0's clipped triangles
 * (PRIM 0x07B) after the object's own. */
static int gsw_receiver(EmGfx *g, const EmGfxShadowStrips *object, uint32_t cls)
{
    float k1021[4] = { 255.0f, 2048.0f, g->fog[4], g->fog[5] }, k1022[4], k1023[4];
    em_shadow_gs_level_rows(k1022, k1023);
    const uint32_t n = object->vertex_count, clip_cap = 30u * 27u;
    EmShadowGsReceiverVertex *out = malloc(sizeof *out * n);
    EmGfxGsPrim *p = gsw_prims(g, n + (cls == 2u ? clip_cap * (n / EM_GFX_SHADOW_BATCH) : 0u));
    if (!out || !p) {
        free(out);
        return gsw_fail(g, "out of memory");
    }
    const float (*qw3)[4] = (const float (*)[4])object->qw3;
    uint32_t count = 0;
    for (uint32_t b = 0; b < n; b += EM_GFX_SHADOW_BATCH) {
        em_shadow_gs_receiver_batch(g->shadowCam, g->shadowUV, k1021, k1022, k1023, qw3 + b,
                                    EM_GFX_SHADOW_BATCH, out + b);
        for (uint32_t i = b + 2; i < b + EM_GFX_SHADOW_BATCH; ++i) {
            if (out[i].xyzf.why) continue;
            EmGfxGsPrim *t = &p[count++];
            memset(t, 0, sizeof *t);
            t->prim = 0x07Cu;
            t->count = 3;
            for (unsigned k = 0; k < 3; ++k) {
                const EmShadowGsReceiverVertex *v = &out[i - 2 + k];
                const uint8_t rgba[4] = { 0, 0, 0, (uint8_t)v->a };
                gsw_vertex_xyzf(&t->v[k], v->xyzf.w, rgba, gsw_fbits(v->q), gsw_fbits(v->s), gsw_fbits(v->t));
            }
        }
    }
    free(out);
    int clip = 0;
    if (cls == 2u) {
        EmVu1Qword *dmem = malloc(sizeof *dmem * EM_VU1_DMEM_QWORDS);
        EmVu1ClipResult *res = shadow_clip_result();
        EmShadowGsClipVertex *gv = malloc(sizeof *gv * clip_cap);
        if (!dmem || !res || !gv) clip = 2;
        for (uint32_t b = 0; b < n && !clip; b += EM_GFX_SHADOW_BATCH) {
            if (em_shadow_gs_clip_dmem(EM_VU1_CLIP_RECEIVER, g->shadowCam, g->shadowUV, k1021, qw3 + b,
                                       EM_SHADOW_GS_CLIP_TOP, dmem) ||
                em_vu1_shadow_clip_run(EM_VU1_CLIP_RECEIVER, dmem, EM_SHADOW_GS_CLIP_TOP, res)) {
                clip = 1;
                break;
            }
            const int v = em_shadow_gs_clip_vertices(EM_VU1_CLIP_RECEIVER, res, gv, clip_cap);
            if (v < 0 || v % 3) { clip = 1; break; }
            for (int k = 0; k < v; k += 3) {
                EmGfxGsPrim *t = &p[count++];
                memset(t, 0, sizeof *t);
                t->prim = 0x07Bu;
                t->count = 3;
                for (unsigned c = 0; c < 3; ++c) {
                    const EmShadowGsClipVertex *s = &gv[k + c];
                    const int32_t w[4] = { (int32_t)(s->x * 16.0f), (int32_t)(s->y * 16.0f), (int32_t)(s->z << 4),
                                           (int32_t)(s->f << 4) };
                    const uint8_t rgba[4] = { 0, 0, 0, (uint8_t)s->a };
                    gsw_vertex_xyzf(&t->v[c], w, rgba, gsw_fbits(s->q), gsw_fbits(s->s), gsw_fbits(s->t));
                }
            }
        }
        free(dmem); free(res); free(gv);
    }
    if (clip) return gsw_fail(g, clip == 2 ? "out of memory" : "receiver clip kernel 0023E8A0 fault");
    em_gs_world_prims(g->gsw, p, NULL, count);
    return gsw_check(g);
}

/* 001D1FF0(0, 1): CLAMP block 1 (CLAMP 0). */
static int gsw_receiver_end(EmGfx *g)
{
    g->shadowRecvOpen = false;
    return gsw_block(g, GSW_CLAMP_BLOCK + 0x40u, 4);
}

/* A list frame (the load veil) through the model. */
static int gsw_list_frame(EmGfx *g, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count,
                          uint64_t display_frame, uint64_t display_scissor)
{
    gsw_complete(g);
    if (g->gswWhy[0]) return -1;
    if (gsw_slot(g) < 0) return -1;
    if (em_gs_world_list_frame(g->gsw, prims, envs, count, display_frame, display_scissor) < 0) return gsw_check(g);
    g->gswShow = true;
    return 0;
}

/* This frame's field texture: the next of the three, once the GPU no longer
 * reads it (its last command buffer has completed). */
static int gsw_slot(EmGfx *g)
{
    const int k = g->gswSlotNext;
    g->gswSlotNext = (k + 1) % 3;
    if (g->gswSlotCmd[k]) {
        [g->gswSlotCmd[k] waitUntilCompleted];
        [g->gswSlotCmd[k] release];
        g->gswSlotCmd[k] = nil;
    }
    if (!g->gswSlotTex[k]) {
        MTLTextureDescriptor *td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:EM_GS_WORLD_FIELD_W
                                        height:EM_GS_WORLD_FIELD_H mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        g->gswSlotTex[k] = [g->device newTextureWithDescriptor:td];
        if (!g->gswSlotTex[k]) return gsw_fail(g, "field texture allocation failed");
    }
    g->gswSlot = k;
    return 0;
}

/* The pending frame (a field frame already ended): wait for its field, put
 * it in the frame's texture, commit (present) the frame, and write its
 * capture. */
static void gsw_complete(EmGfx *g)
{
    if (!g || !g->gswPending) return;
    g->gswPending = false;
    @autoreleasepool {
    const int ok = em_gs_world_wait(g->gsw) == 0;
    if (!ok) (void)gsw_check(g);
    uint32_t w = 0, h = 0;
    const uint8_t *f = ok ? em_gs_world_field(g->gsw, &w, &h, NULL) : NULL;
    if (f && w == EM_GS_WORLD_FIELD_W && h <= EM_GS_WORLD_FIELD_H)
        [g->gswSlotTex[g->gswPendSlot] replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:f
                                         bytesPerRow:4u * w];
    else if (ok)
        gsw_fail(g, "the drawn field is not a 512-wide field of at most 224 rows");
    if (g->gswPendDrawable) [g->gswPendCmd presentDrawable:g->gswPendDrawable];
    [g->gswPendCmd commit];
    [g->gswSlotCmd[g->gswPendSlot] release];
    g->gswSlotCmd[g->gswPendSlot] = [g->gswPendCmd retain];
    if (g->gswPendShot) {
        [g->gswPendCmd waitUntilCompleted];
        write_bmp(g->gswPendPath, (const uint8_t *)g->gswPendShot.contents, (uint32_t)g->gswPendShotW,
                  (uint32_t)g->gswPendShotH, (uint32_t)g->gswPendShotStride);
        if (f) gsw_write_field(g, g->gswPendPath);
        [g->gswPendShot release];
        g->gswPendShot = nil;
    }
    [g->gswPendCmd release];
    g->gswPendCmd = nil;
    [g->gswPendDrawable release];
    g->gswPendDrawable = nil;
    }
}

static void gsw_write_field(EmGfx *g, const char *bmp_path)
{
    uint32_t w, h;
    uint64_t frame = 0;
    const uint8_t *f = em_gs_world_field(g->gsw, &w, &h, &frame);
    char path[1100];
    snprintf(path, sizeof path, "%s.gsfield", bmp_path);
    FILE *o = f ? fopen(path, "wb") : NULL;
    if (!o) {
        fprintf(stderr, "capture: no GS field written for %s\n", bmp_path);
        return;
    }
    fwrite(f, 1, (size_t)w * h * 4u, o);
    fclose(o);
    fprintf(stderr, "capture: wrote %s (%ux%u GS field, FRAME_1 %016llx, XYOFFSET_1 %016llx)\n", path, w, h,
            (unsigned long long)frame, (unsigned long long)em_gs_world_field_xyoffset(g->gsw));
}

/* The field into the game rectangle (EM_GFX_FIELD_SPREAD, the placeholder:
 * nearest, its rows spread over the rectangle's height), under the overlay
 * pass. */
static void gsw_present(EmGfx *g)
{
    if (g->gswFrame) {
        g->gswFrame = false;
        gsw_fail(g, "a world frame was recorded but never kicked");
    }
    if (!g->gswShow || !g->enc || !g->gswSlotTex[g->gswSlot]) return;
    if (!g->gsfShowPipeline)
        g->gsfShowPipeline = shadow_pipeline(g, kGsFrameShaderSrc, @"v_gsshow", @"f_gsshow",
                                             g->layer.pixelFormat, true, MTLColorWriteMaskAll);
    if (!g->gsfShowPipeline) { gsw_fail(g, "the field pipeline is unavailable"); return; }
    ensure_depth_states(g);
    static const float quad[6][4] = {
        { -1.0f,  1.0f, 0.0f, 0.0f }, { 1.0f,  1.0f, 1.0f, 0.0f }, { -1.0f, -1.0f, 0.0f, 1.0f },
        {  1.0f,  1.0f, 1.0f, 0.0f }, { 1.0f, -1.0f, 1.0f, 1.0f }, { -1.0f, -1.0f, 0.0f, 1.0f },
    };
    /* EM_GFX_FIELD_SPREAD (the placeholder): the field's rows spread over
     * the game rectangle, nearest; its texture is filled before the frame
     * is committed (gsw_complete). */
    const uint32_t k[4] = { EM_GS_WORLD_FIELD_W, EM_GS_WORLD_FIELD_H, 0u, 0u };
    [g->enc setRenderPipelineState:g->gsfShowPipeline];
    [g->enc setDepthStencilState:g->depthOff];
    [g->enc setCullMode:MTLCullModeNone];
    [g->enc setVertexBytes:quad length:sizeof quad atIndex:0];
    [g->enc setFragmentTexture:g->gswSlotTex[g->gswSlot] atIndex:0];
    [g->enc setFragmentBytes:k length:sizeof k atIndex:0];
    [g->enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
}

void em_gfx_request_capture(EmGfx *g, const char *path)
{
    if (!g || !path) return;
    snprintf(g->capturePath, sizeof(g->capturePath), "%s", path);
    g->captureRequested = true;
}

/* Write BGRA8 pixels as a bottom-up 24-bit BMP (plain stdio, no deps). */
static void write_bmp(const char *path, const uint8_t *bgra,
                      uint32_t w, uint32_t h, uint32_t stride)
{
    uint32_t row_bytes = (w * 3 + 3) & ~3u;
    uint32_t image_size = row_bytes * h;
    uint32_t file_size = 54 + image_size;
    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2,  &file_size, 4);
    uint32_t off = 54;          memcpy(hdr + 10, &off, 4);
    uint32_t bisize = 40;       memcpy(hdr + 14, &bisize, 4);
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    uint16_t planes = 1, bpp = 24;
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &image_size, 4);

    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "capture: cannot write %s\n", path); return; }
    fwrite(hdr, 1, 54, f);
    uint8_t *row = (uint8_t *)calloc(1, row_bytes);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *src = bgra + (size_t)(h - 1 - y) * stride;
        for (uint32_t x = 0; x < w; x++) {
            row[x * 3 + 0] = src[x * 4 + 0];   /* B */
            row[x * 3 + 1] = src[x * 4 + 1];   /* G */
            row[x * 3 + 2] = src[x * 4 + 2];   /* R */
        }
        fwrite(row, 1, row_bytes, f);
    }
    free(row);
    fclose(f);
    fprintf(stderr, "capture: wrote %s (%ux%u)\n", path, w, h);
}

void em_gfx_end_frame(EmGfx *g)
{
    if (!g) return;
    gsw_present(g);     /* the GS frame's field, under the overlay pass */
    overlay_sub_flush(g, true); /* letterbox subtracts scene beneath text */
    backdrop_flush(g);  /* UI background: bottom of the overlay sequence */
    overlay_flush(g);   /* queued HUD rects draw last, over the 3D scene */
    texquad_flush(g, EM_GFX_OVERLAY_TEX_UI,     /* decor over the rects  */
                  g->spriteVerts, &g->spriteVertCount,g->spriteBlend);
    texquad_flush(g, EM_GFX_OVERLAY_TEX_FONT,   /* text over everything  */
                  g->glyphVerts, &g->glyphVertCount,NULL);
    overlay_sub_flush(g, false); /* screen fade darkens the whole frame */
    if (g->enc) { [g->enc endEncoding]; [g->enc release]; g->enc = nil; }

    id<MTLBuffer> shot = nil;
    NSUInteger shot_w = 0, shot_h = 0, shot_stride = 0;
    if (g->captureRequested && g->target && g->cmd) {
        id<MTLTexture> tex = g->target;
        shot_w = tex.width; shot_h = tex.height;
        shot_stride = shot_w * 4;
        shot = [g->device newBufferWithLength:shot_stride * shot_h
                                      options:MTLResourceStorageModeShared];
        id<MTLBlitCommandEncoder> blit = [g->cmd blitCommandEncoder];
        [blit copyFromTexture:tex
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(shot_w, shot_h, 1)
                     toBuffer:shot
            destinationOffset:0
       destinationBytesPerRow:shot_stride
     destinationBytesPerImage:shot_stride * shot_h];
        [blit endEncoding];
    }

    if (g->target && g->cmd && g->gswShow) {
        /* A field frame waits for its field (gsw_complete): the next kick, or
         * the next frame without a field, commits it. A capture of it also
         * writes the field itself, <path>.gsfield (512 x 224 x 4 bytes, R, G,
         * B, A as GS memory holds them), for tools/test_fb2_pixels.py. */
        g->gswPending = true;
        g->gswPendCmd = [g->cmd retain];
        g->gswPendDrawable = [g->drawable retain];
        g->gswPendSlot = g->gswSlot;
        if (shot) {
            g->gswPendShot = [shot retain];
            g->gswPendShotW = shot_w;
            g->gswPendShotH = shot_h;
            g->gswPendShotStride = shot_stride;
            snprintf(g->gswPendPath, sizeof g->gswPendPath, "%s", g->capturePath);
            g->captureRequested = false;
        }
    } else if (g->target && g->cmd) {
        gsw_complete(g);   /* a field frame before this one is shown first */
        if (g->drawable) [g->cmd presentDrawable:g->drawable];
        [g->cmd commit];
        if (shot) {
            [g->cmd waitUntilCompleted];
            write_bmp(g->capturePath, (const uint8_t *)shot.contents,
                      (uint32_t)shot_w, (uint32_t)shot_h,
                      (uint32_t)shot_stride);
            g->captureRequested = false;
        }
    }
    [shot release];
    [g->cmd release];      g->cmd = nil;
    [g->drawable release]; g->drawable = nil;
    g->target = nil;
    [g->pool release];     g->pool = nil;
}
