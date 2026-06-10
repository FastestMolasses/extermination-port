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
#include <stdlib.h>
#include <stdio.h>

struct EmGfx {
    NSView                      *view;     /* layer-backed, layer is CAMetalLayer */
    CAMetalLayer                *layer;
    id<MTLDevice>                device;
    id<MTLCommandQueue>          queue;
    id<MTLRenderPipelineState>   testPipeline; /* lazily built for the test draw */
    id<MTLRenderPipelineState>   skinPipeline; /* lazily built for skinned draws */
    id<MTLRenderPipelineState>   glowPipeline; /* additive (ONE/ONE) glow pass */
    id<MTLSamplerState>          repeatSampler; /* linear, REPEAT (PS2 tiling) */
    id<MTLDepthStencilState>     depthOn;      /* less-equal, write */
    id<MTLDepthStencilState>     depthOff;     /* always, no write */
    id<MTLDepthStencilState>     depthGlow;    /* less-equal, NO write (ZMSK=1) */
    id<MTLTexture>               depthTex;     /* sized to the drawable */
    /* per-frame */
    NSAutoreleasePool           *pool;
    id<CAMetalDrawable>          drawable;
    id<MTLCommandBuffer>         cmd;
    id<MTLRenderCommandEncoder>  enc;       /* open from begin_frame to end_frame */
    /* headless capture (see em_gfx_request_capture) */
    char                         capturePath[1024];
    bool                         captureRequested;
    /* 2D overlay pass (em_gfx_overlay_rect): rects queued during the
     * frame as ready-to-draw NDC vertices (6 per rect, float4 pos +
     * float4 color each — the kTestShaderSrc layout), flushed by
     * end_frame after all 3D draws. */
    float                        overlayVerts[EM_GFX_OVERLAY_MAX * 6 * 8];
    uint32_t                     overlayRects;
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

/* Skinning shader — the translated PS2 vertex pipeline. Buffer 0 holds
 * 10-word vertex records (float pos[3], float normal[3], float uv[2],
 * uint bone, uint tex); buffer 1 is the bone-matrix palette (column-major,
 * same layout the game stages for VU1 dmem); buffer 2 the camera; buffer 3
 * one float2 UV scale per texture (native size / array-slice size — the
 * slices hold each texture TILED to a common pow-2 size so sampler REPEAT
 * reproduces GS wrap). This mirrors the VU1 soft-skinner: vertex positions
 * are bone-local, world = palette[bone] * pos. Fragment = texture sample
 * (per-triangle slice via [[flat]]) modulated by directional light;
 * untextured vertices (tex == ~0u) keep the flat grey. A per-draw mode word
 * (fragment buffer 0) selects shading: 0 = directional stand-in light from
 * the normal; bit 0 set = the "normal" slot is a baked RGB vertex color
 * (static level geometry ships its lighting prebaked) and the fragment is
 * texture * color, the GS modulate path; bit 1 set = the GLOW pass (drawn
 * additively): the fragment is the texture sample alone, no alpha-test
 * cutout (the GS glow draws never update alpha or Z).
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
"struct VOut { float4 pos [[position]]; float3 nrm; float2 uv;\n"
"              uint slice [[flat]]; };\n"
"vertex VOut v_skin(uint vid [[vertex_id]],\n"
"                   const device uint *vdata [[buffer(0)]],\n"
"                   const device float4x4 *palette [[buffer(1)]],\n"
"                   constant float4x4 &viewproj [[buffer(2)]],\n"
"                   const device float2 *tscale [[buffer(3)]],\n"
"                   constant uint &mode [[buffer(4)]]) {\n"
"    const device float *fw = (const device float *)vdata;\n"
"    float3 p = float3(fw[vid*10+0], fw[vid*10+1], fw[vid*10+2]);\n"
"    float3 n = float3(fw[vid*10+3], fw[vid*10+4], fw[vid*10+5]);\n"
"    float2 uv = float2(fw[vid*10+6], fw[vid*10+7]);\n"
"    uint  tex = vdata[vid*10+9];\n"
"    uint  bw  = vdata[vid*10+8];\n"
"    float4x4 M = palette[bw & 0x00FFFFFFu];\n"
"    VOut o;\n"
"    if (bw & 0x80000000u) {\n"
"        /* camera-facing glow quad: anchor + corner along camera right/up\n"
"         * (viewproj row r = float3(vp[0][r], vp[1][r], vp[2][r])). */\n"
"        float3 c = (M * float4(p, 1.0)).xyz;\n"
"        float3 right = normalize(float3(viewproj[0].x, viewproj[1].x,\n"
"                                        viewproj[2].x));\n"
"        float3 up    = normalize(float3(viewproj[0].y, viewproj[1].y,\n"
"                                        viewproj[2].y));\n"
"        o.pos = viewproj * float4(c + right * n.x + up * n.y, 1.0);\n"
"        o.nrm = float3(1.0);\n"
"    } else {\n"
"        o.pos = viewproj * (M * float4(p, 1.0));\n"
"        o.nrm = (mode & 1u) ? n : (M * float4(n, 0.0)).xyz;\n"
"    }\n"
"    o.slice = tex;\n"
"    o.uv = (tex == 0xFFFFFFFFu) ? float2(0.0) : uv * tscale[tex];\n"
"    return o;\n"
"}\n"
"fragment float4 f_skin(VOut in [[stage_in]],\n"
"                       texture2d_array<float> texs [[texture(0)]],\n"
"                       sampler smp [[sampler(0)]],\n"
"                       constant uint &mode [[buffer(0)]]) {\n"
"    float4 base = float4(0.55, 0.62, 0.70, 1.0);\n"
"    if (in.slice != 0xFFFFFFFFu) {\n"
"        base = texs.sample(smp, in.uv, in.slice);\n"
"        /* PS2 CLUT alpha is mostly binary (0 / 0x80): alpha-test the\n"
"         * cutout texels (grates, glass edges) so depth stays correct;\n"
"         * residual partial alpha goes through the blend stage. The glow\n"
"         * pass skips the test: additive draws never punch holes. */\n"
"        if (!(mode & 2u) && base.a < 0.5) discard_fragment();\n"
"    }\n"
"    if (mode & 2u) {\n"
"        /* additive glow: Cv = Cs + Cd (GS ALPHA FIX=0x80); the layer\n"
"         * tint is pre-multiplied into the texels by the exporter. */\n"
"        return float4(base.rgb, 1.0);\n"
"    }\n"
"    if (mode & 1u) {\n"
"        /* baked vertex color (GS modulate) */\n"
"        return float4(base.rgb * clamp(in.nrm, 0.0, 1.0), base.a);\n"
"    }\n"
"    float3 N = normalize(in.nrm);\n"
"    float3 L = normalize(float3(0.4, 0.8, 0.45));\n"
"    float  d = max(dot(N, L), 0.0);\n"
"    return float4(base.rgb * (0.30 + 0.70 * d), base.a);\n"
"}\n";

/* Compile MSL source at runtime and build a pipeline for the swapchain +
 * depth formats. Returns +1-retained PSO or nil (with the error printed). */
static id<MTLRenderPipelineState> build_pipeline(EmGfx *g, NSString *src,
                                                 NSString *vfn_name,
                                                 NSString *ffn_name,
                                                 bool additive)
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
    /* Standard alpha blending: most PS2 texels are fully opaque (alpha-test
     * in the shader handles cutouts), so this only softens the few
     * partial-alpha texels (window glass) without needing draw sorting.
     * `additive` builds the glow-pass variant instead: Cv = Cs + Cd, the
     * GS ALPHA (A=Cs, B=0, C=FIX 0x80, D=Cd) of the live glow draws; dest
     * alpha is left untouched (the GS never updates A on those draws). */
    pd.colorAttachments[0].blendingEnabled = YES;
    if (additive) {
        pd.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorOne;
        pd.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOne;
        pd.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorZero;
        pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    } else {
        pd.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
        pd.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
        pd.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorOne;
        pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
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
    /* NO so the drawable can be blit-read by the capture path. */
    layer.framebufferOnly = NO;
    return g;
}

void em_gfx_destroy(EmGfx *g)
{
    if (!g) return;
    [g->testPipeline release];
    [g->skinPipeline release];
    [g->glowPipeline release];
    [g->repeatSampler release];
    [g->depthOn release];
    [g->depthOff release];
    [g->depthGlow release];
    [g->depthTex release];
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

    /* keep the swapchain sized to the backing store */
    NSSize sz = g->view.bounds.size;
    CGFloat scale = g->view.window.backingScaleFactor;
    if (scale <= 0) scale = 1.0;
    g->layer.drawableSize = CGSizeMake(sz.width * scale, sz.height * scale);

    g->drawable = [[g->layer nextDrawable] retain];
    if (!g->drawable) { return; }
    g->cmd = [[g->queue commandBuffer] retain];

    ensure_depth_texture(g, g->drawable.texture.width,
                         g->drawable.texture.height);

    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture     = g->drawable.texture;
    rp.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor  = MTLClearColorMake(r, gr, b, a);
    rp.depthAttachment.texture     = g->depthTex;
    rp.depthAttachment.loadAction  = MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionDontCare;
    rp.depthAttachment.clearDepth  = 1.0;

    /* Keep the encoder open so draw calls can run between begin and end. */
    g->enc = [[g->cmd renderCommandEncoderWithDescriptor:rp] retain];
}

void em_gfx_draw_test_triangle(EmGfx *g)
{
    if (!g || !g->enc) return;
    if (!g->testPipeline) {
        g->testPipeline = build_pipeline(g, kTestShaderSrc, @"v_main",
                                         @"f_main", false); /* lazily, once */
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

EmGfxMesh *em_gfx_mesh_create(EmGfx *g, const float *verts,
                              uint32_t vert_count, const uint32_t *indices,
                              uint32_t index_count,
                              const EmGfxTexDesc *texs, uint32_t tex_count,
                              const uint8_t *texels, uint32_t flags)
{
    if (!g || !verts || !indices || !vert_count || !index_count) return NULL;
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

    if (!m->vbuf || !m->ibuf || !m->texArray || !m->scaleBuf) {
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
    free(m);
}

void em_gfx_draw_skinned(EmGfx *g, EmGfxMesh *m, const float *viewproj,
                         const float *palette, uint32_t bone_count)
{
    if (!g || !g->enc || !m || !viewproj || !palette || !bone_count) return;
    if (!g->skinPipeline) {
        g->skinPipeline = build_pipeline(g, kSkinShaderSrc,
                                         @"v_skin", @"f_skin", false);
        if (!g->skinPipeline) return;
    }
    if (m->glow_count && !g->glowPipeline) {
        g->glowPipeline = build_pipeline(g, kSkinShaderSrc,
                                         @"v_skin", @"f_skin", true);
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
    [g->enc setRenderPipelineState:g->skinPipeline];
    [g->enc setDepthStencilState:g->depthOn];
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
    uint32_t mode = m->flags;
    [g->enc setVertexBytes:&mode length:4 atIndex:4];
    [g->enc setFragmentBytes:&mode length:4 atIndex:0];
    [g->enc setFragmentTexture:m->texArray atIndex:0];
    [g->enc setFragmentSamplerState:g->repeatSampler atIndex:0];
    if (m->opaque_count)
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
        uint32_t glow_mode = mode | 2u;
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
}

/* Queue one overlay rect: convert the virtual-canvas box (640x448,
 * origin top-left, y down — em_gfx.h) to NDC on the CPU and append two
 * triangles in the kTestShaderSrc vertex layout (float4 pos + float4
 * color). The pass itself runs in end_frame. */
void em_gfx_overlay_rect(EmGfx *g, float x, float y, float w, float h,
                         const float rgba[4])
{
    if (!g || !rgba || g->overlayRects >= EM_GFX_OVERLAY_MAX) return;
    float x0 = x / EM_GFX_OVERLAY_W * 2.0f - 1.0f;
    float x1 = (x + w) / EM_GFX_OVERLAY_W * 2.0f - 1.0f;
    float y0 = 1.0f - y / EM_GFX_OVERLAY_H * 2.0f;
    float y1 = 1.0f - (y + h) / EM_GFX_OVERLAY_H * 2.0f;
    const float corners[6][2] = {
        { x0, y0 }, { x1, y0 }, { x0, y1 },   /* tri 1 */
        { x1, y0 }, { x1, y1 }, { x0, y1 },   /* tri 2 */
    };
    float *v = g->overlayVerts + (size_t)g->overlayRects * 6 * 8;
    for (int i = 0; i < 6; i++) {
        v[i * 8 + 0] = corners[i][0];
        v[i * 8 + 1] = corners[i][1];
        v[i * 8 + 2] = 0.0f;
        v[i * 8 + 3] = 1.0f;
        v[i * 8 + 4] = rgba[0];
        v[i * 8 + 5] = rgba[1];
        v[i * 8 + 6] = rgba[2];
        v[i * 8 + 7] = rgba[3];
    }
    g->overlayRects++;
}

/* Flush the queued overlay rects: one draw at the END of the open render
 * pass (post-3D, so the HUD composites over the scene), depth test OFF,
 * standard alpha blend. Reuses the position+color passthrough pipeline
 * (kTestShaderSrc, runtime-compiled) — overlay vertices are pre-converted
 * NDC, exactly that shader's input. The vertex data can exceed Metal's
 * 4 KB setVertexBytes ceiling (a HUD is ~40 rects = ~7.5 KB), so it goes
 * through a per-flush MTLBuffer; the command buffer retains it until the
 * GPU is done, so releasing right after the draw is safe. */
static void overlay_flush(EmGfx *g)
{
    uint32_t rects = g->overlayRects;
    g->overlayRects = 0;
    if (!rects || !g->enc) return;
    if (!g->testPipeline) {
        g->testPipeline = build_pipeline(g, kTestShaderSrc, @"v_main",
                                         @"f_main", false);
        if (!g->testPipeline) return;
    }
    ensure_depth_states(g);
    id<MTLBuffer> vbuf =
        [g->device newBufferWithBytes:g->overlayVerts
                               length:(NSUInteger)rects * 6 * 8 * sizeof(float)
                              options:MTLResourceStorageModeShared];
    if (!vbuf) return;
    [g->enc setRenderPipelineState:g->testPipeline];
    [g->enc setDepthStencilState:g->depthOff];
    [g->enc setCullMode:MTLCullModeNone];
    [g->enc setVertexBuffer:vbuf offset:0 atIndex:0];
    [g->enc drawPrimitives:MTLPrimitiveTypeTriangle
               vertexStart:0
               vertexCount:(NSUInteger)rects * 6];
    [vbuf release];   /* the in-flight command buffer keeps it alive */
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
    overlay_flush(g);   /* queued HUD rects draw last, over the 3D scene */
    if (g->enc) { [g->enc endEncoding]; [g->enc release]; g->enc = nil; }

    id<MTLBuffer> shot = nil;
    NSUInteger shot_w = 0, shot_h = 0, shot_stride = 0;
    if (g->captureRequested && g->drawable && g->cmd) {
        id<MTLTexture> tex = g->drawable.texture;
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

    if (g->drawable && g->cmd) {
        [g->cmd presentDrawable:g->drawable];
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
    [g->pool release];     g->pool = nil;
}
