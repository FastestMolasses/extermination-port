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
    id<MTLDepthStencilState>     depthOn;      /* less-equal, write */
    id<MTLDepthStencilState>     depthOff;     /* always, no write */
    id<MTLTexture>               depthTex;     /* sized to the drawable */
    /* per-frame */
    NSAutoreleasePool           *pool;
    id<CAMetalDrawable>          drawable;
    id<MTLCommandBuffer>         cmd;
    id<MTLRenderCommandEncoder>  enc;       /* open from begin_frame to end_frame */
    /* headless capture (see em_gfx_request_capture) */
    char                         capturePath[1024];
    bool                         captureRequested;
};

struct EmGfxMesh {
    id<MTLBuffer> vbuf;
    id<MTLBuffer> ibuf;
    uint32_t      index_count;
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
 * 7-word vertex records (float pos[3], float normal[3], uint bone); buffer 1
 * is the bone-matrix palette (column-major, same layout the game stages for
 * VU1 dmem); buffer 2 the camera. This mirrors the VU1 soft-skinner: vertex
 * positions are bone-local, world = palette[bone] * pos. Flat directional
 * shading stands in for textures until GS material capture is wired up. */
static NSString *const kSkinShaderSrc =
@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct VOut { float4 pos [[position]]; float3 nrm; };\n"
"vertex VOut v_skin(uint vid [[vertex_id]],\n"
"                   const device uint *vdata [[buffer(0)]],\n"
"                   const device float4x4 *palette [[buffer(1)]],\n"
"                   constant float4x4 &viewproj [[buffer(2)]]) {\n"
"    const device float *fw = (const device float *)vdata;\n"
"    float3 p = float3(fw[vid*7+0], fw[vid*7+1], fw[vid*7+2]);\n"
"    float3 n = float3(fw[vid*7+3], fw[vid*7+4], fw[vid*7+5]);\n"
"    float4x4 M = palette[vdata[vid*7+6]];\n"
"    VOut o;\n"
"    o.pos = viewproj * (M * float4(p, 1.0));\n"
"    o.nrm = (M * float4(n, 0.0)).xyz;\n"
"    return o;\n"
"}\n"
"fragment float4 f_skin(VOut in [[stage_in]]) {\n"
"    float3 N = normalize(in.nrm);\n"
"    float3 L = normalize(float3(0.4, 0.8, 0.45));\n"
"    float  d = max(dot(N, L), 0.0);\n"
"    float3 c = float3(0.55, 0.62, 0.70) * (0.30 + 0.70 * d);\n"
"    return float4(c, 1.0);\n"
"}\n";

/* Compile MSL source at runtime and build a pipeline for the swapchain +
 * depth formats. Returns +1-retained PSO or nil (with the error printed). */
static id<MTLRenderPipelineState> build_pipeline(EmGfx *g, NSString *src,
                                                 NSString *vfn_name,
                                                 NSString *ffn_name)
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
    [g->depthOn release];
    [g->depthOff release];
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
        g->testPipeline = build_pipeline(g, kTestShaderSrc,
                                         @"v_main", @"f_main"); /* lazily, once */
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
                              uint32_t index_count)
{
    if (!g || !verts || !indices || !vert_count || !index_count) return NULL;
    EmGfxMesh *m = (EmGfxMesh *)calloc(1, sizeof(EmGfxMesh));
    m->vbuf = [g->device newBufferWithBytes:verts
                                     length:(NSUInteger)vert_count * 7 * 4
                                    options:MTLResourceStorageModeShared];
    m->ibuf = [g->device newBufferWithBytes:indices
                                     length:(NSUInteger)index_count * 4
                                    options:MTLResourceStorageModeShared];
    m->index_count = index_count;
    if (!m->vbuf || !m->ibuf) {
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
    free(m);
}

void em_gfx_draw_skinned(EmGfx *g, EmGfxMesh *m, const float *viewproj,
                         const float *palette, uint32_t bone_count)
{
    if (!g || !g->enc || !m || !viewproj || !palette || !bone_count) return;
    if (!g->skinPipeline) {
        g->skinPipeline = build_pipeline(g, kSkinShaderSrc,
                                         @"v_skin", @"f_skin");
        if (!g->skinPipeline) return;
    }
    ensure_depth_states(g);
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
    [g->enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                       indexCount:m->index_count
                        indexType:MTLIndexTypeUInt32
                      indexBuffer:m->ibuf
                indexBufferOffset:0];
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
