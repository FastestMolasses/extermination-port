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

struct EmGfx {
    NSView                 *view;     /* layer-backed, layer is CAMetalLayer */
    CAMetalLayer           *layer;
    id<MTLDevice>           device;
    id<MTLCommandQueue>     queue;
    /* per-frame */
    NSAutoreleasePool      *pool;
    id<CAMetalDrawable>     drawable;
    id<MTLCommandBuffer>    cmd;
};

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
    layer.framebufferOnly = YES;
    return g;
}

void em_gfx_destroy(EmGfx *g)
{
    if (!g) return;
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

    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture     = g->drawable.texture;
    rp.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor  = MTLClearColorMake(r, gr, b, a);

    id<MTLRenderCommandEncoder> enc =
        [g->cmd renderCommandEncoderWithDescriptor:rp];
    /* clear-only for now: open and immediately close the pass */
    [enc endEncoding];
}

void em_gfx_end_frame(EmGfx *g)
{
    if (!g) return;
    if (g->drawable && g->cmd) {
        [g->cmd presentDrawable:g->drawable];
        [g->cmd commit];
    }
    [g->cmd release];      g->cmd = nil;
    [g->drawable release]; g->drawable = nil;
    [g->pool release];     g->pool = nil;
}
