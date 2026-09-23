/* em_gfx_metal.m — macOS graphics backend on Metal.
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
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>
#include "em_gfx.h"
#include "em_platform.h"
#include "game/em_lighting.h"
#include "gfx/metal/em_fog_gs.h"
#include "gfx/metal/em_background_gs.h"
#include "gfx/metal/em_shadow_gs.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct EmGfx {
    NSView                      *view;     /* layer-backed, layer is CAMetalLayer */
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
    /* World-space beam pass (em_gfx_beam / em_gfx_beam_dot): primitives
     * queued during the frame as raw records; the camera-plane extrusion
     * needs the camera, so vertices are built at flush time from the
     * viewproj of the frame's LAST skinned draw. */
    struct EmGfxBeamRec {
        float a[3], b[3];   /* segment ends (dot: a == b == anchor)     */
        float w;            /* width (dot: the square's size)           */
        float ca[4], cb[4]; /* per-end colors (dot: ca only)            */
        int   dot;          /* 1 = camera-facing square at a            */
        int   tex;          /* beam-texture slot, -1 = untextured       */
        float roll;         /* axial quads: rotation of the width
                             * vector around the a->b axis, RADIANS
                             * (em_gfx_beam_tex_roll — the muzzle-flash
                             * star's engine rotation lerp). 0 keeps
                             * the exact pre-roll camera-plane math.   */
    }                            beams[EM_GFX_BEAM_MAX];
    uint32_t                     beamCount;
    /* World-space TEXTURED TRIANGLES in the same additive beam pass
     * (em_gfx_beam_tris_tex — the flashlight cone mesh): raw world
     * vertices, drawn additively with depth test on / write off after
     * the beam sprites. */
    struct EmGfxBeamTriRec {
        float p[9];         /* 3 world positions                        */
        float uv[6];        /* 3 uv pairs                               */
        float c[4];         /* modulate color                           */
        int   tex;          /* beam-texture slot (>= 0)                 */
    }                            beamTris[EM_GFX_BEAM_TRI_MAX];
    uint32_t                     beamTriCount;
    float                        lastViewProj[16]; /* column-major P*V  */
    bool                         hasViewProj;      /* a 3D draw ran     */
    bool                         everViewProj;     /* any draw EVER ran:
                                                    * em_gfx_last_viewproj
                                                    * gate (lastViewProj
                                                    * persists across
                                                    * frames, hasViewProj
                                                    * is per-frame)      */
    id<MTLRenderPipelineState>   beamPipeline;     /* additive, depth-on */
    /* Textured beam sprites (em_gfx_beam_tex / _dot_tex — em_gfx.h):
     * the laser-dot sprite + the muzzle-flash sheets, drawn through the
     * additive beam pass sampling these registered slots. */
    id<MTLTexture>               beamTex[EM_GFX_BEAM_TEX_MAX];
    id<MTLRenderPipelineState>   beamTexPipeline;  /* additive, textured */
    id<MTLTexture>               particleTexture[EM_GFX_PARTICLE_TEX_MAX];
    id<MTLRenderPipelineState>   particlePipeline;
    /* Last skinned draw's leading bone matrices (em_gfx_last_skinned_bone
     * — the native bone-publish; the chain draws the player LAST, so this
     * is the player palette between frames). COPIED at draw time: palette
     * buffers may be freed by scene switches. */
    float                        lastBones[EM_GFX_TRACK_BONES * 16];
    uint32_t                     lastBoneCount;    /* bones recorded     */
    /* Per-frame flashlight spot (em_gfx_spot_light — em_gfx.h). Four
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
 * position, modulate color, uv (xy, normalized). The fragment samples the
 * registered overlay texture BILINEAR and modulates — the GS state of the
 * engine's font-strip sprites (TEX1 MMAG/MMIN=1, TFX modulate). */
static NSString *const kGlyphShaderSrc =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct VOut { float4 pos [[position]]; float4 color; float2 uv; };\n"
"vertex VOut v_glyph(uint vid [[vertex_id]],\n"
"                    const device float4 *data [[buffer(0)]]) {\n"
"    VOut o;\n"
"    o.pos   = float4(data[vid*3].xy, 0.0, 1.0);\n"
"    o.color = data[vid*3 + 1];\n"
"    o.uv    = data[vid*3 + 2].xy;\n"
"    return o;\n"
"}\n"
"fragment float4 f_glyph(VOut in [[stage_in]],\n"
"                        texture2d<float> tex [[texture(0)]],\n"
"                        sampler smp [[sampler(0)]]) {\n"
"    return tex.sample(smp, in.uv) * in.color;\n"
"}\n"
"fragment float4 f_glyph_opaque(VOut in [[stage_in]],\n"
"                        texture2d<float> tex [[texture(0)]],\n"
"                        sampler smp [[sampler(0)]]) {\n"
"    float4 color = tex.sample(smp, in.uv) * in.color;\n"
"    if (color.a <= 0.0) discard_fragment();\n"
"    return color;\n"
"}\n";

/* Beam shader — world-space position + color through the camera, compiled
 * at runtime like the others. Buffer 0 holds float4 world position +
 * float4 color per vertex; buffer 1 the column-major viewproj. Going
 * through viewproj (not pre-projected NDC) keeps real depth, so the
 * laser is occluded by geometry exactly like the GS LINE pass (ZTE=1,
 * ZMSK=1 — depth test on, write off, bound at draw time). */
static NSString *const kBeamShaderSrc =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct VOut { float4 pos [[position]]; float4 color; };\n"
"vertex VOut v_beam(uint vid [[vertex_id]],\n"
"                   const device float4 *data [[buffer(0)]],\n"
"                   constant float4x4 &viewproj [[buffer(1)]]) {\n"
"    VOut o;\n"
"    o.pos   = viewproj * float4(data[vid*2].xyz, 1.0);\n"
"    o.color = data[vid*2 + 1];\n"
"    return o;\n"
"}\n"
"fragment float4 f_beam(VOut in [[stage_in]]) { return in.color; }\n";

/* Textured-beam shader — the beam shader with a UV channel sampling one
 * registered beam-texture slot (the FX sprite sheets): 3 float4s per
 * vertex (world pos, modulate color, uv.xy). The fragment is sample *
 * color, drawn additively (GS ALPHA Cv = Cs + Cd — the original flash/
 * dot sprite state), so the texture's own falloff shapes the glow. */
static NSString *const kBeamTexShaderSrc =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct VOut { float4 pos [[position]]; float4 color; float2 uv; };\n"
"vertex VOut v_beamtex(uint vid [[vertex_id]],\n"
"                      const device float4 *data [[buffer(0)]],\n"
"                      constant float4x4 &viewproj [[buffer(1)]]) {\n"
"    VOut o;\n"
"    o.pos   = viewproj * float4(data[vid*3].xyz, 1.0);\n"
"    o.color = data[vid*3 + 1];\n"
"    o.uv    = data[vid*3 + 2].xy;\n"
"    return o;\n"
"}\n"
"fragment float4 f_beamtex(VOut in [[stage_in]],\n"
"                          texture2d<float> tex [[texture(0)]],\n"
"                          sampler smp [[sampler(0)]]) {\n"
"    return tex.sample(smp, in.uv) * in.color;\n"
"}\n";

/* Background shader (em_gfx_background_draw): 2 float4s per vertex, the
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

static NSString *const kParticleShaderSrc =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct VOut { float4 pos [[position]]; float4 color; float2 uv; };\n"
"vertex VOut v_particle(uint vid [[vertex_id]],\n"
"                         const device float4 *data [[buffer(0)]]) {\n"
"    VOut out;\n"
"    out.pos = data[vid*3];\n"
"    out.color = data[vid*3+1];\n"
"    out.uv = data[vid*3+2].xy;\n"
"    return out;\n"
"}\n"
"fragment float4 f_particle(VOut in [[stage_in]],\n"
"                          texture2d<float> tex [[texture(0)]],\n"
"                          sampler smp [[sampler(0)]]) {\n"
"    return tex.sample(smp, in.uv) * in.color;\n"
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
" * f255 is the GS F value interpolated from the vertices (255 keeps the\n"
" * colour, 0 is pure FOGCOL). The GS blends toward FOGCOL by F / 255\n"
" * in 8-bit arithmetic; this is the float mix without that\n"
" * quantization. */\n"
"static float3 fog_apply(float3 c, float f255, constant float4 *fog) {\n"
"    if (fog[0].w <= 0.0) return c;\n"
"    return mix(fog[0].rgb, c, f255 * (1.0 / 255.0));\n"
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
"        /* baked vertex color (GS modulate) + the flashlight spot. The\n"
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
"/* Shadow receiver pixel (em_gfx_shadow_receiver, em_shadow_gs.h\n"
" * em_shadow_gs_bilinear_alpha / em_shadow_gs_receiver_pixel): v_skin runs\n"
" * with mode 4, so light_rgb carries the kernel's RGBAQ A and fog F / 128\n"
" * (screen-linear interpolation) and uv the 0023C200 (u, v) (perspective,\n"
" * S/Q and T/Q). dst is the frame pixel (framebuffer fetch). APPROXIMATION,\n"
" * not verified against a GS dump of a drawn shadow: A and F per pixel are\n"
" * floor(value * 128 + 0.001) of Metal's float interpolation; the GS's own\n"
" * Gouraud/DDA stepping of A and F is not modelled and the 0.001 epsilon\n"
" * is a heuristic (docs/SHADOW_ORIGINAL.md, open items). */\n"
"fragment float4 f_shadow_receiver(VOut in [[stage_in]],\n"
"        float4 dst [[color(0)]],\n"
"        texture2d<float, access::read> sil [[texture(0)]],\n"
"        constant float4 *fog [[buffer(4)]]) {\n"
"    int uu = int(floor(in.uv.x * 2048.0)) - 8;\n"
"    int vv = int(floor(in.uv.y * 2048.0)) - 8;\n"
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
"    uint f = uint(floor(in.light_rgb.y * 128.0 + 0.001));\n"
"    uint as = min((at * a) >> 7, 255u);\n"
"    if (as == 0u) discard_fragment();\n"
"    uint4 d = uint4(round(dst * 255.0));\n"
"    if ((d.a & 0x80u) == 0u) discard_fragment();\n"
"    int3 fc = int3(round(fog[0].rgb * 255.0));\n"
"    int3 cs = int3((uint3(255u - f) * uint3(fc)) >> 8);\n"
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
    [g->beamPipeline release];
    [g->beamTexPipeline release];
    for (unsigned i = 0; i < EM_GFX_PARTICLE_TEX_MAX; ++i)
        [g->particleTexture[i] release];
    [g->particlePipeline release];
    [g->bgTexture release];
    [g->bgPipeline release];
    [g->shadowAlphaPipeline release];
    [g->shadowSilPipeline release];
    [g->shadowRecvPipeline release];
    for (unsigned i = 0; i < EM_GFX_SHADOW_TARGET_MAX; i++)
        [g->shadowTarget[i] release];
    [g->shadowLast release];
    free(g->bgFile);
    for (int i = 0; i < EM_GFX_BEAM_TEX_MAX; i++)
        [g->beamTex[i] release];
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
    [g->view release];
    free(g);
}

void em_gfx_begin_frame(EmGfx *g, float r, float gr, float b, float a)
{
    if (!g) return;
    g->pool = [[NSAutoreleasePool alloc] init];
    g->beamCount    = 0;      /* world-space beams are per-frame */
    g->beamTriCount = 0;
    g->hasViewProj = false;   /* set again by the frame's 3D draws */
    g->overlayW    = EM_GFX_OVERLAY_W;  /* canvas resets to the default */
    g->overlayH    = EM_GFX_OVERLAY_H;
    memset(g->spot, 0, sizeof(g->spot)); /* flashlight spot is per-frame */
    memset(g->rig,  0, sizeof(g->rig));  /* character rig is per-frame   */
    memset(g->face_rig, 0, sizeof(g->face_rig));
    memset(g->fog,  0, sizeof(g->fog));  /* distance fog is per-frame    */
    g->shadowTargets  = 0;               /* shadow targets are per-frame  */
    g->shadowCurrent  = -1;
    g->shadowRecvOpen = false;

    /* keep the swapchain sized to the backing store */
    NSSize sz = g->view.bounds.size;
    CGFloat scale = g->view.window.backingScaleFactor;
    if (scale <= 0) scale = 1.0;
    NSUInteger pw = (NSUInteger)(sz.width * scale), ph = (NSUInteger)(sz.height * scale);
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
        g->layer.drawableSize = CGSizeMake(sz.width * scale, sz.height * scale);
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
                         const float rgba[4], bool additive)
{
    if (!g || !g->enc || !m || !viewproj || !palette || !bone_count || !rgba)
        return;
    /* Remember the frame's camera for the beam flush (world-space pass). */
    memcpy(g->lastViewProj, viewproj, sizeof(g->lastViewProj));
    g->hasViewProj  = true;
    g->everViewProj = true;
    /* Record the leading bone matrices — the native bone-publish for
     * equipment consumers (em_gfx.h "last skinned palette"). */
    if (!additive) {
        g->lastBoneCount = bone_count < EM_GFX_TRACK_BONES ? bone_count
                                                           : EM_GFX_TRACK_BONES;
        memcpy(g->lastBones, palette,
               (size_t)g->lastBoneCount * 16 * sizeof(float));
    }
    /* Opaque (tint alpha >= 1, non-additive) draws are the decoded GS
     * class 0: blending off (PRIM ABE 0) and the per-slice TEST_1 alpha
     * test (mode bit 3). The translucent tint path keeps the alpha
     * pipeline and the legacy cutout (port path, not decoded). */
    bool class0 = !additive && rgba[3] >= 1.0f;
    if (!additive && !class0 && !g->skinPipeline) {
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
    if ((additive || m->glow_count) && !g->glowPipeline) {
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
    [g->enc setRenderPipelineState:(additive ? g->glowPipeline
                                    : class0 ? g->skinOpaquePipeline
                                    : g->skinPipeline)];
    /* Threshold rule (see the comment above): a tint alpha below 1.0
     * selects the translucent state — depth test on, write off. */
    bool translucent = additive || rgba[3] < 1.0f;
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
    uint32_t mode = (m->flags & EM_GFX_MESH_VCOLOR) | (additive ? 2u : 0u)
                  | (class0 ? 8u : 0u);
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
    draw_skinned(g,m,viewproj,palette,bone_count,rgba,false);
}

void em_gfx_draw_skinned_additive(EmGfx *g, EmGfxMesh *m, const float *viewproj,
                                  const float *palette, uint32_t bone_count,
                                  const float rgba[4])
{
    draw_skinned(g,m,viewproj,palette,bone_count,rgba,true);
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
    o[10] = 0.0f;
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

/* Queue one world-space beam segment (em_gfx.h). Stored raw; vertices are
 * built at flush time because the camera-plane extrusion needs the
 * frame's camera. */
void em_gfx_beam(EmGfx *g, const float a[3], const float b[3], float width,
                 const float rgba_a[4], const float rgba_b[4])
{
    if (!g || !a || !b || !rgba_a || !rgba_b ||
        g->beamCount >= EM_GFX_BEAM_MAX)
        return;
    struct EmGfxBeamRec *r = &g->beams[g->beamCount++];
    memcpy(r->a,  a,      sizeof(r->a));
    memcpy(r->b,  b,      sizeof(r->b));
    memcpy(r->ca, rgba_a, sizeof(r->ca));
    memcpy(r->cb, rgba_b, sizeof(r->cb));
    r->w    = width;
    r->dot  = 0;
    r->tex  = -1;
    r->roll = 0.0f;
}

/* Copy one recorded bone matrix of the last skinned draw (em_gfx.h —
 * the native bone-publish; the gameplay chain draws the player LAST). */
int em_gfx_last_skinned_bone(EmGfx *g, uint32_t bone, float out16[16])
{
    if (!g || !out16 || bone >= g->lastBoneCount) return 0;
    memcpy(out16, g->lastBones + (size_t)bone * 16, 16 * sizeof(float));
    return 1;
}

/* Copy the last skinned draw's P*V (em_gfx.h — the camera-matrix
 * publish for em_weapon's screen-space acquisition cone). */
int em_gfx_last_viewproj(EmGfx *g, float out16[16])
{
    if (!g || !out16 || !g->everViewProj) return 0;
    memcpy(out16, g->lastViewProj, sizeof(g->lastViewProj));
    return 1;
}

/* Queue one camera-facing square glow (em_gfx.h). */
void em_gfx_beam_dot(EmGfx *g, const float p[3], float size,
                     const float rgba[4])
{
    if (!g || !p || !rgba || g->beamCount >= EM_GFX_BEAM_MAX) return;
    struct EmGfxBeamRec *r = &g->beams[g->beamCount++];
    memcpy(r->a,  p,    sizeof(r->a));
    memcpy(r->b,  p,    sizeof(r->b));
    memcpy(r->ca, rgba, sizeof(r->ca));
    memcpy(r->cb, rgba, sizeof(r->cb));
    r->w    = size;
    r->dot  = 1;
    r->tex  = -1;
    r->roll = 0.0f;
}

/* Register one beam texture slot (em_gfx.h — the FX sprite sheets). */
int em_gfx_beam_texture_set(EmGfx *g, int slot, const uint8_t *rgba,
                            uint32_t w, uint32_t h)
{
    if (!g || !g->device || !rgba || !w || !h ||
        slot < 0 || slot >= EM_GFX_BEAM_TEX_MAX)
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
    [g->beamTex[slot] release];
    g->beamTex[slot] = tex;        /* +1 from newTextureWithDescriptor */
    return 1;
}

/* Queue one TEXTURED axial-billboard beam quad (em_gfx.h): u runs
 * a -> b, v across. No-op without a registered slot texture (the
 * caller keeps its untextured fallback). */
void em_gfx_beam_tex(EmGfx *g, int slot, const float a[3], const float b[3],
                     float width, const float rgba[4])
{
    if (!g || !a || !b || !rgba || slot < 0 ||
        slot >= EM_GFX_BEAM_TEX_MAX || !g->beamTex[slot] ||
        g->beamCount >= EM_GFX_BEAM_MAX)
        return;
    struct EmGfxBeamRec *r = &g->beams[g->beamCount++];
    memcpy(r->a,  a,    sizeof(r->a));
    memcpy(r->b,  b,    sizeof(r->b));
    memcpy(r->ca, rgba, sizeof(r->ca));
    memcpy(r->cb, rgba, sizeof(r->cb));
    r->w    = width;
    r->dot  = 0;
    r->tex  = slot;
    r->roll = 0.0f;
}

/* em_gfx_beam_tex with an axis ROLL (em_gfx.h): the quad's width vector
 * is rotated `roll` radians around the a->b axis before the camera-
 * plane extrusion — the muzzle-flash star's engine rotation lerp
 * (func_001F5040 tick >= 4) made visible on the axial billboard. */
void em_gfx_beam_tex_roll(EmGfx *g, int slot, const float a[3],
                          const float b[3], float width, float roll,
                          const float rgba[4])
{
    uint32_t before = g ? g->beamCount : 0;
    em_gfx_beam_tex(g, slot, a, b, width, rgba);
    if (g && g->beamCount == before + 1)
        g->beams[before].roll = roll;
}

/* Queue one TEXTURED camera-facing square sprite (em_gfx.h). */
void em_gfx_beam_dot_tex(EmGfx *g, int slot, const float p[3], float size,
                         const float rgba[4])
{
    if (!g || !p || !rgba || slot < 0 ||
        slot >= EM_GFX_BEAM_TEX_MAX || !g->beamTex[slot] ||
        g->beamCount >= EM_GFX_BEAM_MAX)
        return;
    struct EmGfxBeamRec *r = &g->beams[g->beamCount++];
    memcpy(r->a,  p,    sizeof(r->a));
    memcpy(r->b,  p,    sizeof(r->b));
    memcpy(r->ca, rgba, sizeof(r->ca));
    memcpy(r->cb, rgba, sizeof(r->cb));
    r->w    = size;
    r->dot  = 1;
    r->tex  = slot;
    r->roll = 0.0f;
}

/* Queue one world-space TEXTURED TRIANGLE in the additive beam pass
 * (em_gfx.h — the flashlight cone mesh's drawer): three world vertices
 * with uvs, sampling beam slot `slot`, modulated by `rgba`. Same flush,
 * blend (additive) and depth state (test on / write off) as the beam
 * sprites; own EM_GFX_BEAM_TRI_MAX budget, overflow dropped. */
void em_gfx_beam_tri_tex(EmGfx *g, int slot, const float p[9],
                         const float uv[6], const float rgba[4])
{
    if (!g || !p || !uv || !rgba || slot < 0 ||
        slot >= EM_GFX_BEAM_TEX_MAX || !g->beamTex[slot] ||
        g->beamTriCount >= EM_GFX_BEAM_TRI_MAX)
        return;
    struct EmGfxBeamTriRec *t = &g->beamTris[g->beamTriCount++];
    memcpy(t->p,  p,    sizeof(t->p));
    memcpy(t->uv, uv,   sizeof(t->uv));
    memcpy(t->c,  rgba, sizeof(t->c));
    t->tex = slot;
}

int em_gfx_particle_texture_set_slot(EmGfx *g, unsigned slot,
                                      const uint8_t *rgba,
                                      uint32_t width, uint32_t height)
{
    if (!g || slot >= EM_GFX_PARTICLE_TEX_MAX) return 0;
    if (!rgba && !width && !height) {
        [g->particleTexture[slot] release];
        g->particleTexture[slot] = nil;
        return 1;
    }
    if (!rgba || !width || !height || width > 256 || height > 256) return 0;
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:width height:height mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [g->device newTextureWithDescriptor:descriptor];
    if (!texture) return 0;
    [texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
              mipmapLevel:0 withBytes:rgba bytesPerRow:width * 4];
    [g->particleTexture[slot] release];
    g->particleTexture[slot] = texture;
    return 1;
}

void em_gfx_particles_draw_slot(EmGfx *g, unsigned slot,
                                 const EmGfxParticle *particles, unsigned count)
{
    if (!g || !g->enc || slot >= EM_GFX_PARTICLE_TEX_MAX ||
        !g->particleTexture[slot] || !particles || !count || count > 4096)
        return;
    if (!g->particlePipeline)
        g->particlePipeline = build_pipeline(g, kParticleShaderSrc,
            @"v_particle", @"f_particle", EM_BLEND_ADD);
    if (!g->particlePipeline) return;
    if (!g->clampSampler) {
        MTLSamplerDescriptor *descriptor = [[MTLSamplerDescriptor alloc] init];
        descriptor.minFilter = MTLSamplerMinMagFilterLinear;
        descriptor.magFilter = MTLSamplerMinMagFilterLinear;
        descriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
        descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
        g->clampSampler = [g->device newSamplerStateWithDescriptor:descriptor];
        [descriptor release];
    }
    ensure_depth_states(g);
    float *vertices = malloc((size_t)count * 6 * 12 * sizeof(float));
    if (!vertices) return;
    const unsigned corners[6] = {0, 1, 2, 1, 3, 2};
    for (unsigned i = 0; i < count; ++i) {
        const EmGfxParticle *particle = &particles[i];
        for (unsigned vertex = 0; vertex < 6; ++vertex) {
            unsigned corner = corners[vertex];
            unsigned x = corner & 1;
            unsigned y = (corner >> 1) & 1;
            float *out = vertices + (i * 6 + vertex) * 12;
            out[0] = particle->corner[x][0];
            out[1] = particle->corner[y][1];
            out[2] = particle->depth;
            out[3] = 1.0f;
            memcpy(out + 4, particle->color, 4 * sizeof(float));
            out[8] = particle->st[x][0];
            out[9] = particle->st[y][1];
            out[10] = out[11] = 0.0f;
        }
    }
    id<MTLBuffer> buffer = [g->device newBufferWithBytes:vertices
        length:(NSUInteger)count * 6 * 12 * sizeof(float)
        options:MTLResourceStorageModeShared];
    free(vertices);
    if (!buffer) return;
    [g->enc setRenderPipelineState:g->particlePipeline];
    [g->enc setDepthStencilState:g->depthGlow];
    [g->enc setCullMode:MTLCullModeNone];
    [g->enc setVertexBuffer:buffer offset:0 atIndex:0];
    [g->enc setFragmentTexture:g->particleTexture[slot] atIndex:0];
    [g->enc setFragmentSamplerState:g->clampSampler atIndex:0];
    [g->enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:count * 6];
    [buffer release];
}

int em_gfx_particle_texture_set(EmGfx *g, const uint8_t *rgba,
                                 uint32_t width, uint32_t height)
{
    return em_gfx_particle_texture_set_slot(g, 0, rgba, width, height);
}

void em_gfx_particles_draw(EmGfx *g, const EmGfxParticle *particles,
                            unsigned count)
{
    em_gfx_particles_draw_slot(g, 0, particles, count);
}

/* Set this frame's flashlight spot (em_gfx.h "Flashlight spot light" —
 * the documented port deviation). Stored as the fragment-buffer rows the
 * skinned shader consumes; begin_frame resets the enable to 0. Call it
 * BEFORE the frame's skinned draws (em_weapon_render runs at the head of
 * the render chain build) — the rows bind per draw. */
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
 * stored coefficients are exactly 0021B920's (em_fog_gs.h), and the
 * vertex shader evaluates F = A + B * clip_w like the 0023C780 kernel.
 * Applies to the LEVEL and CHARACTER paths; additive glow draws are
 * unaffected. */
void em_gfx_fog(EmGfx *g, float near_z, float far_z, const float rgb[3])
{
    if (!g || !rgb) return;
    float coef[2];
    em_fog_gs_coefficients(near_z, far_z, coef);
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

void em_gfx_background_draw(EmGfx *g, const float view[16], float zoom_s)
{
    if (!g || !g->enc || !g->bgTexture || !view) return;
    if (!g->bgPipeline)
        g->bgPipeline = build_pipeline(g, kBackgroundShaderSrc,
            @"v_background", @"f_background", EM_BLEND_OPAQUE);
    if (!g->bgPipeline) return;
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
        if (!g->clampSampler) return;
    }
    ensure_depth_states(g);

    /* 001E1E60: view copy -> D_00253570; kernel 0x0023C990: the grid. */
    float original[16], m[16];
    em_background_gs_original_view(view, original);
    em_background_gs_matrix(original, m);
    static EmBackgroundGsVertex grid[EM_BACKGROUND_GS_GRID]
                                    [EM_BACKGROUND_GS_GRID];
    em_background_gs_grid(&g->bgAsset, m, zoom_s, grid);

    /* One kick per row pair: (r,c), (r+1,c) for c = 0..31. GS XY 12.4
     * maps onto NDC by the GS pixel-footprint convention of the port's
     * world projection (em_background_gs_ndc: GS X 2048 = NDC 0, 256 and
     * 112 field pixels per NDC unit). */
    enum { STRIP = EM_BACKGROUND_GS_GRID * 2,
           STRIPS = EM_BACKGROUND_GS_GRID - 1 };
    static float verts[STRIPS * STRIP * 8];
    unsigned n = 0;
    for (unsigned r = 0; r < STRIPS; ++r)
        for (unsigned c = 0; c < EM_BACKGROUND_GS_GRID; ++c)
            for (unsigned k = 0; k < 2; ++k) {
                const EmBackgroundGsVertex *v = &grid[r + k][c];
                float *o = verts + (n++) * 8;
                em_background_gs_ndc(v->xy, o);
                o[2] = 0.0f;
                o[3] = 1.0f;
                o[4] = v->st[0];
                o[5] = v->st[1];
                o[6] = 0.0f;
                o[7] = 0.0f;
            }
    id<MTLBuffer> buffer = [g->device newBufferWithBytes:verts
        length:sizeof verts options:MTLResourceStorageModeShared];
    if (!buffer) return;
    const uint32_t q = g->bgAsset.rgbaq;
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
    for (unsigned r = 0; r < STRIPS; ++r)
        [g->enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                   vertexStart:r * STRIP
                   vertexCount:STRIP];
    [buffer release];
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

static void beam_norm3(float v[3])
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
    else           { v[0] = 1.0f; v[1] = 0.0f; v[2] = 0.0f; }
}

/* World-space quad corners of one beam record — a-side0 a-side1 b-side0
 * b-side1 (the arithmetic of the original untextured-only flush, kept
 * expression-for-expression so untextured frames stay byte-identical). */
static void beam_corners(const struct EmGfxBeamRec *r, const float right[3],
                         const float up[3], const float fwd[3],
                         float c0[3], float c1[3], float c2[3], float c3[3])
{
    float h = r->w * 0.5f;
    if (r->dot) {
        for (int k = 0; k < 3; k++) {
            c0[k] = r->a[k] - right[k] * h - up[k] * h;
            c1[k] = r->a[k] + right[k] * h - up[k] * h;
            c2[k] = r->a[k] - right[k] * h + up[k] * h;
            c3[k] = r->a[k] + right[k] * h + up[k] * h;
        }
    } else {
        float axis[3] = { r->b[0] - r->a[0], r->b[1] - r->a[1],
                          r->b[2] - r->a[2] };
        float side[3] = { axis[1] * fwd[2] - axis[2] * fwd[1],
                          axis[2] * fwd[0] - axis[0] * fwd[2],
                          axis[0] * fwd[1] - axis[1] * fwd[0] };
        float sl = sqrtf(side[0] * side[0] + side[1] * side[1] +
                         side[2] * side[2]);
        if (sl > 1e-6f) {
            side[0] *= h / sl; side[1] *= h / sl; side[2] *= h / sl;
        } else {               /* segment along the view axis: use right */
            side[0] = right[0] * h;
            side[1] = right[1] * h;
            side[2] = right[2] * h;
        }
        if (r->roll != 0.0f) {
            /* Rodrigues rotation of the width vector around the unit
             * a->b axis (em_gfx_beam_tex_roll — the muzzle-flash
             * star's engine rotation lerp, func_001F5040 tick >= 4).
             * roll == 0 keeps the exact pre-roll arithmetic above. */
            float ax[3] = { axis[0], axis[1], axis[2] };
            beam_norm3(ax);
            float c = cosf(r->roll), s = sinf(r->roll);
            float axs[3] = { ax[1] * side[2] - ax[2] * side[1],
                             ax[2] * side[0] - ax[0] * side[2],
                             ax[0] * side[1] - ax[1] * side[0] };
            float ad = ax[0] * side[0] + ax[1] * side[1] +
                       ax[2] * side[2];
            for (int k = 0; k < 3; k++)
                side[k] = side[k] * c + axs[k] * s +
                          ax[k] * ad * (1.0f - c);
        }
        for (int k = 0; k < 3; k++) {
            c0[k] = r->a[k] - side[k];
            c1[k] = r->a[k] + side[k];
            c2[k] = r->b[k] - side[k];
            c3[k] = r->b[k] + side[k];
        }
    }
}

/* Flush the queued world-space beams: additive draws inside the open
 * render pass, AFTER the 3D scene (em_weapon queues during close-out) and
 * BEFORE the overlay pass — the untextured records in one draw (exactly
 * the pre-texture flush), then the textured sprites grouped by slot
 * (em_gfx_beam_tex / _dot_tex). Camera basis comes from the rows of the
 * frame's last viewproj (column-major m[col*4+row]): row r of P*V is the
 * view rotation's r-row scaled by a positive projection factor, so the
 * normalized xyz of rows 0/1/3 are camera right / up / forward in world
 * space (row 3 = the w row = view z, because w_clip = z_view). Each
 * segment becomes a quad extruded half a width to each side along
 * normalize(cross(axis, camera_fwd)) — the axial billboard; dots become
 * camera-plane squares on right/up. Depth state: TEST on, WRITE off
 * (depthGlow) — the GS laser draws keep ZTE=1 with ZMSK=1. */
static void beam_flush(EmGfx *g)
{
    uint32_t count    = g->beamCount;
    uint32_t triCount = g->beamTriCount;
    g->beamCount    = 0;
    g->beamTriCount = 0;
    if ((!count && !triCount) || !g->enc) return;
    if (!g->hasViewProj) return;   /* no camera this frame: drop */
    if (!g->beamPipeline) {
        g->beamPipeline = build_pipeline(g, kBeamShaderSrc, @"v_beam",
                                         @"f_beam", EM_BLEND_ADD);
        if (!g->beamPipeline) return;
    }
    ensure_depth_states(g);

    const float *m = g->lastViewProj;
    float right[3] = { m[0], m[4], m[8]  };
    float up[3]    = { m[1], m[5], m[9]  };
    float fwd[3]   = { m[3], m[7], m[11] };
    beam_norm3(right);
    beam_norm3(up);
    beam_norm3(fwd);

    /* Pass 1: the UNTEXTURED records — one draw, exactly the pre-texture
     * flush (a frame with no textured records issues identical GPU work,
     * keeping pre-texture captures byte-identical). 6 verts per
     * primitive, float4 pos + float4 color each. */
    float *verts = (float *)malloc((size_t)count * 6 * 8 * sizeof(float));
    if (!verts) return;
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        const struct EmGfxBeamRec *r = &g->beams[i];
        if (r->tex >= 0) continue;
        float c0[3], c1[3], c2[3], c3[3];
        beam_corners(r, right, up, fwd, c0, c1, c2, c3);
        const float *quad[6][2] = {
            { c0, r->ca }, { c1, r->ca }, { c2, r->cb },   /* tri 1 */
            { c1, r->ca }, { c3, r->cb }, { c2, r->cb },   /* tri 2 */
        };
        for (int v = 0; v < 6; v++) {
            float *o = verts + (size_t)(n + v) * 8;
            o[0] = quad[v][0][0];
            o[1] = quad[v][0][1];
            o[2] = quad[v][0][2];
            o[3] = 1.0f;
            memcpy(o + 4, quad[v][1], 4 * sizeof(float));
        }
        n += 6;
    }
    if (n) {
        /* Can exceed the 4 KB setVertexBytes ceiling (64 beams = 12 KB),
         * so a per-flush buffer; the in-flight command buffer keeps it
         * alive. */
        id<MTLBuffer> vbuf =
            [g->device newBufferWithBytes:verts
                                   length:(NSUInteger)n * 8 * sizeof(float)
                                  options:MTLResourceStorageModeShared];
        if (vbuf) {
            [g->enc setRenderPipelineState:g->beamPipeline];
            [g->enc setDepthStencilState:g->depthGlow];
            [g->enc setCullMode:MTLCullModeNone];
            [g->enc setVertexBuffer:vbuf offset:0 atIndex:0];
            [g->enc setVertexBytes:g->lastViewProj length:64 atIndex:1];
            [g->enc drawPrimitives:MTLPrimitiveTypeTriangle
                       vertexStart:0
                       vertexCount:n];
            [vbuf release];
        }
    }
    free(verts);

    /* Pass 2: the TEXTURED sprites (em_gfx_beam_tex / _dot_tex), grouped
     * by slot — same additive blend and depth state, one draw per slot
     * sampling its registered sheet. 3 float4s per vertex (world pos,
     * color, uv): u runs a -> b on axial quads, dots take the full
     * sprite; v = 0 at the texture's TOP row (rows are uploaded
     * top-down), which is +up in the camera plane. */
    for (int slot = 0; slot < EM_GFX_BEAM_TEX_MAX; slot++) {
        uint32_t nt = 0, ntri = 0;
        for (uint32_t i = 0; i < count; i++)
            if (g->beams[i].tex == slot) nt++;
        for (uint32_t i = 0; i < triCount; i++)
            if (g->beamTris[i].tex == slot) ntri++;
        if ((!nt && !ntri) || !g->beamTex[slot]) continue;
        if (!g->beamTexPipeline) {
            g->beamTexPipeline = build_pipeline(g, kBeamTexShaderSrc,
                                                @"v_beamtex", @"f_beamtex",
                                                EM_BLEND_ADD);
            if (!g->beamTexPipeline) return;
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
        float *tv = (float *)malloc(((size_t)nt * 6 + (size_t)ntri * 3) *
                                    12 * sizeof(float));
        if (!tv) return;
        uint32_t tn = 0;
        for (uint32_t i = 0; i < count; i++) {
            const struct EmGfxBeamRec *r = &g->beams[i];
            if (r->tex != slot) continue;
            float c0[3], c1[3], c2[3], c3[3];
            beam_corners(r, right, up, fwd, c0, c1, c2, c3);
            /* Per-corner UVs: axial quads run u 0->1 from the a end
             * (c0/c1) to the b end (c2/c3), v 0->1 across; dots take
             * the full sprite with v = 0 on the +up corners (c2/c3 —
             * texel rows are uploaded top-down). */
            static const float uv_axial[4][2] =
                { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } };
            static const float uv_dot[4][2] =
                { { 0, 1 }, { 1, 1 }, { 0, 0 }, { 1, 0 } };
            const float (*uvs)[2] = r->dot ? uv_dot : uv_axial;
            const struct { const float *p; const float *c; const float *uv; }
            quad[6] = {
                { c0, r->ca, uvs[0] }, { c1, r->ca, uvs[1] },   /* tri 1 */
                { c2, r->cb, uvs[2] },
                { c1, r->ca, uvs[1] }, { c3, r->cb, uvs[3] },   /* tri 2 */
                { c2, r->cb, uvs[2] },
            };
            for (int v = 0; v < 6; v++) {
                float *o = tv + (size_t)(tn + v) * 12;
                o[0] = quad[v].p[0];
                o[1] = quad[v].p[1];
                o[2] = quad[v].p[2];
                o[3] = 1.0f;
                memcpy(o + 4, quad[v].c, 4 * sizeof(float));
                o[8]  = quad[v].uv[0];
                o[9]  = quad[v].uv[1];
                o[10] = 0.0f;
                o[11] = 1.0f;
            }
            tn += 6;
        }
        /* The queued world-space TRIANGLES of this slot (the flashlight
         * cone mesh — em_gfx_beam_tris_tex): raw vertices, no camera
         * extrusion, same additive draw. */
        for (uint32_t i = 0; i < triCount; i++) {
            const struct EmGfxBeamTriRec *t = &g->beamTris[i];
            if (t->tex != slot) continue;
            for (int v = 0; v < 3; v++) {
                float *o = tv + (size_t)(tn + v) * 12;
                o[0] = t->p[v * 3 + 0];
                o[1] = t->p[v * 3 + 1];
                o[2] = t->p[v * 3 + 2];
                o[3] = 1.0f;
                memcpy(o + 4, t->c, 4 * sizeof(float));
                o[8]  = t->uv[v * 2 + 0];
                o[9]  = t->uv[v * 2 + 1];
                o[10] = 0.0f;
                o[11] = 1.0f;
            }
            tn += 3;
        }
        id<MTLBuffer> vbuf =
            [g->device newBufferWithBytes:tv
                                   length:(NSUInteger)tn * 12 * sizeof(float)
                                  options:MTLResourceStorageModeShared];
        free(tv);
        if (!vbuf) return;
        [g->enc setRenderPipelineState:g->beamTexPipeline];
        [g->enc setDepthStencilState:g->depthGlow];
        [g->enc setCullMode:MTLCullModeNone];
        [g->enc setVertexBuffer:vbuf offset:0 atIndex:0];
        [g->enc setVertexBytes:g->lastViewProj length:64 atIndex:1];
        [g->enc setFragmentTexture:g->beamTex[slot] atIndex:0];
        [g->enc setFragmentSamplerState:g->clampSampler atIndex:0];
        [g->enc drawPrimitives:MTLPrimitiveTypeTriangle
                   vertexStart:0
                   vertexCount:tn];
        [vbuf release];
    }
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
 * full-frame solid fill (the UI-camera-scene stand-in; one quad through
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

int em_gfx_shadow_box(EmGfx *g, const EmGfxShadowStrips *model,
                      const float world[16], const float clip[16],
                      uint32_t rgbaq, const float viewproj[16])
{
    if (!g || !g->enc) return shadow_fail(g, SHADOW_WARN_FRAME, "outside a frame");
    if (!model || !model->qw3 || !model->vertex_count ||
        model->vertex_count % EM_GFX_SHADOW_BATCH || !world || !clip || !viewproj)
        return shadow_fail(g, SHADOW_WARN_INPUT, "box input missing");
    if (!shadow_alpha_ready(g))
        return shadow_fail(g, SHADOW_WARN_GPU, "alpha pipeline unavailable");
    float k1021[4] = { 255.0f, 2048.0f, 0.0f, 0.0f }, k1022[4], k1023[4];
    em_shadow_gs_level_rows(k1022, k1023);
    const uint32_t n = model->vertex_count;
    EmShadowGsVertex *out = malloc(sizeof *out * n);
    float (*tri)[4] = malloc(sizeof *tri * 3 * n);
    if (!out || !tri) {
        free(out); free(tri);
        return shadow_fail(g, SHADOW_WARN_INPUT, "out of memory");
    }
    const float (*qw3)[4] = (const float (*)[4])model->qw3;
    uint32_t count = 0;
    int bad = 0;
    for (uint32_t b = 0; b < n && !bad; b += EM_GFX_SHADOW_BATCH) {
        if (em_shadow_gs_level_batch(clip, k1021, k1022, k1023, qw3 + b,
                                     EM_GFX_SHADOW_BATCH, out + b))
            bad = 1;                     /* EM_SHADOW_GS_ADC_STALE */
        for (uint32_t i = b + 2; i < b + EM_GFX_SHADOW_BATCH && !bad; ++i) {
            const uint32_t why = out[i].why;
            /* 00239C90 (001DA310 runs it for every box) draws exactly
             * these; its clipping is not translated. */
            if (em_shadow_gs_needs_clip(why, i - b)) { bad = 2; break; }
            /* 00237180 kicks only vertices without ADC. */
            if (why) continue;
            for (unsigned k = 0; k < 3; ++k) {
                const float *p = qw3[i - 2 + k];
                for (unsigned l = 0; l < 3; ++l)
                    tri[count][l] = p[0] * world[l] + p[1] * world[4 + l] +
                                    p[2] * world[8 + l] + world[12 + l];
                tri[count][3] = 1.0f;
                ++count;
            }
        }
    }
    free(out);
    if (bad) {
        free(tri);
        return shadow_fail(g, bad == 1 ? SHADOW_WARN_STALE : SHADOW_WARN_CLIP,
                           bad == 1 ? "box strip starts without ADC (kernel "
                                      "state not modelled)"
                                    : "box triangle for clip kernel 00239C90 "
                                      "(not translated)");
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
        [g->enc setVertexBytes:viewproj length:64 atIndex:1];
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
        g->shadowRecvPipeline = shadow_pipeline(g, kSkinShaderSrc, @"v_skin",
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
    float *rec = malloc(sizeof(float) * 10 * 3 * n);
    uint32_t (*rgba)[4] = malloc(sizeof *rgba * 3 * n);
    if (!out || !rec || !rgba) {
        free(out); free(rec); free(rgba);
        return shadow_fail(g, SHADOW_WARN_INPUT, "out of memory");
    }
    const float (*qw3)[4] = (const float (*)[4])object->qw3;
    uint32_t count = 0;
    int clip = 0;
    for (uint32_t b = 0; b < n && !clip; b += EM_GFX_SHADOW_BATCH) {
        em_shadow_gs_receiver_batch(g->shadowCam, g->shadowUV, k1021, k1022,
                                    k1023, qw3 + b, EM_GFX_SHADOW_BATCH, out + b);
        for (uint32_t i = b + 2; i < b + EM_GFX_SHADOW_BATCH; ++i) {
            /* 0023C200 kicks the triangle only when its last vertex has no
             * ADC (data flag or guard-band CLIP). A class-2 object's
             * 0023E8A0 re-pass draws the CLIP ones (not translated: fault
             * before anything of the object is drawn); classes 0 and 1
             * have no re-pass, so nothing draws them. */
            const uint32_t why = out[i].xyzf.why;
            if (cls == 2u && em_shadow_gs_needs_clip(why, i - b)) {
                clip = 1;
                break;
            }
            if (why) continue;
            for (unsigned k = 0; k < 3; ++k) {
                const uint32_t j = i - 2 + k;
                float *r = rec + (size_t)count * 10;
                uint32_t zero = 0, notex = 0;
                /* The kernel's (u, v) = (S, T) / Q: the pixel samples at
                 * S/Q, T/Q; Metal's perspective interpolation of (u, v)
                 * is that division. */
                const float u = out[j].s / out[j].q, v = out[j].t / out[j].q;
                r[0] = qw3[j][0]; r[1] = qw3[j][1]; r[2] = qw3[j][2];
                r[3] = r[4] = r[5] = 0.0f;
                r[6] = u; r[7] = v;
                memcpy(r + 8, &zero, 4);
                memcpy(r + 9, &notex, 4);
                rgba[count][0] = out[j].a;
                rgba[count][1] = (uint32_t)(out[j].xyzf.w[3] >> 4) & 0xFFu;
                rgba[count][2] = rgba[count][3] = 0;
                ++count;
            }
        }
    }
    free(out);
    if (clip) {
        free(rec); free(rgba);
        return shadow_fail(g, SHADOW_WARN_CLIP, "receiver triangle for clip "
                           "kernel 0023E8A0 (not translated)");
    }
    if (count) {
        static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0,
                                         0, 0, 1, 0, 0, 0, 0, 1 };
        static const float scale[2] = { 1.0f, 1.0f };
        const uint32_t mode = 4u;          /* light_rgb = (A, F) / 128 */
        const float nofog[8] = { 0 };      /* v_skin's own fog_f unused */
        id<MTLBuffer> vb = [g->device newBufferWithBytes:rec
                                                  length:sizeof(float) * 10 * count
                                                 options:MTLResourceStorageModeShared];
        id<MTLBuffer> cb = [g->device newBufferWithBytes:rgba
                                                  length:sizeof *rgba * count
                                                 options:MTLResourceStorageModeShared];
        [g->enc setRenderPipelineState:g->shadowRecvPipeline];
        [g->enc setDepthStencilState:g->depthGlow];   /* GEQUAL, ZMSK 1 */
        [g->enc setCullMode:MTLCullModeNone];         /* 0023C200 never culls */
        [g->enc setVertexBuffer:vb offset:0 atIndex:0];
        [g->enc setVertexBytes:ident length:64 atIndex:1];
        [g->enc setVertexBytes:g->shadowVP length:64 atIndex:2];
        [g->enc setVertexBytes:scale length:8 atIndex:3];
        [g->enc setVertexBytes:&mode length:4 atIndex:4];
        [g->enc setVertexBuffer:cb offset:0 atIndex:5];
        [g->enc setVertexBytes:nofog length:sizeof nofog atIndex:6];
        [g->enc setFragmentTexture:g->shadowTarget[g->shadowCurrent] atIndex:0];
        [g->enc setFragmentBytes:g->fog length:sizeof(g->fog) atIndex:4];
        [g->enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:count];
        [vb release];
        [cb release];
    }
    free(rec);
    free(rgba);
    return 0;
}

int em_gfx_shadow_receiver_end(EmGfx *g)
{
    if (!g || !g->enc) return shadow_fail(g, SHADOW_WARN_FRAME, "outside a frame");
    if (!g->shadowRecvOpen)
        return shadow_fail(g, SHADOW_WARN_ORDER, "receiver end without begin");
    g->shadowRecvOpen = false;
    return 0;
}

int em_gfx_shadow_target_read(EmGfx *g, uint8_t *rgba)
{
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
    beam_flush(g);      /* world-space beams: after 3D, under the overlay */
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

    if (g->target && g->cmd) {
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
