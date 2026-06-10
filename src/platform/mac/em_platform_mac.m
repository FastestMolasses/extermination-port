/* em_platform_mac.m — macOS windowing on Cocoa (AppKit).
 *
 * Clean-room, no third-party libraries; only Apple system frameworks
 * (Cocoa, QuartzCore). Manual reference counting (no ARC) — the few
 * long-lived objects (NSApplication, NSWindow, the content view, the
 * delegate) are retained on create and released on destroy.
 *
 * The content view is layer-backed with a CAMetalLayer so the Metal gfx
 * backend can render into it without the platform layer touching Metal.
 */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include "em_platform.h"
#include <stdlib.h>

/* ---- content view: backed by a CAMetalLayer -------------------------- */
@interface EmContentView : NSView
@end
@implementation EmContentView
- (CALayer *)makeBackingLayer { return [CAMetalLayer layer]; }
- (BOOL)wantsUpdateLayer      { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped             { return YES; }
@end

/* ---- window delegate: tracks close requests -------------------------- */
@interface EmWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic) BOOL shouldClose;
@end
@implementation EmWindowDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender { (void)sender; self.shouldClose = YES; return NO; }
@end

struct EmWindow {
    NSWindow         *window;
    EmContentView    *view;
    EmWindowDelegate *delegate;
};

static int map_key(NSEvent *ev)
{
    /* macOS virtual key codes -> EmKey for the non-printable keys. */
    switch (ev.keyCode) {
        case 53:  return EM_KEY_ESCAPE;
        case 49:  return EM_KEY_SPACE;
        case 36:  return EM_KEY_RETURN;
        case 76:  return EM_KEY_RETURN;   /* keypad enter */
        case 48:  return EM_KEY_TAB;
        case 123: return EM_KEY_LEFT;
        case 124: return EM_KEY_RIGHT;
        case 126: return EM_KEY_UP;
        case 125: return EM_KEY_DOWN;
        default:  break;
    }
    /* Printable keys: per the EmKey contract they carry their ASCII value.
     * Use the layout-resolved character (ignoring modifiers) and fold
     * letters to lowercase so 'A' and 'a' are the same key to the game. */
    NSString *chars = ev.charactersIgnoringModifiers;
    if (chars.length > 0) {
        unichar c = [chars characterAtIndex:0];
        if (c >= 'A' && c <= 'Z') c = (unichar)(c - 'A' + 'a');
        if (c >= 32 && c < 127) return (int)c;
    }
    return EM_KEY_UNKNOWN;
}

EmWindow *em_window_create(const char *title, int width, int height)
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSRect frame = NSMakeRect(0, 0, width, height);
        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;
        NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:style
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        if (!window) return NULL;
        [window setTitle:[NSString stringWithUTF8String:(title ? title : "Extermination")]];

        EmContentView *view = [[EmContentView alloc] initWithFrame:frame];
        [view setWantsLayer:YES];   /* triggers makeBackingLayer -> CAMetalLayer */

        EmWindowDelegate *del = [[EmWindowDelegate alloc] init];

        [window setContentView:view];
        [window makeFirstResponder:view];
        [window setDelegate:del];
        [window center];
        [window setAcceptsMouseMovedEvents:YES];
        [window makeKeyAndOrderFront:nil];

        [NSApp activateIgnoringOtherApps:YES];
        [NSApp finishLaunching];

        EmWindow *w = (EmWindow *)calloc(1, sizeof(EmWindow));
        w->window   = [window retain];
        w->view     = [view retain];
        w->delegate = [del retain];
        return w;
    }
}

void em_window_destroy(EmWindow *w)
{
    if (!w) return;
    @autoreleasepool {
        [w->window setDelegate:nil];
        [w->window close];
        [w->delegate release];
        [w->view release];
        [w->window release];
    }
    free(w);
}

bool em_window_poll(EmWindow *w, EmEvent *out)
{
    if (!w || !out) return false;

    if (w->delegate.shouldClose) {
        w->delegate.shouldClose = NO;
        out->type = EM_EVENT_QUIT;
        return true;
    }

    for (;;) {
        @autoreleasepool {
            NSEvent *ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                             untilDate:[NSDate distantPast]
                                                inMode:NSDefaultRunLoopMode
                                               dequeue:YES];
            if (!ev) return false;

            EmEventType type = EM_EVENT_NONE;
            int key = 0;
            if (ev.type == NSEventTypeKeyDown && !ev.isARepeat) {
                type = EM_EVENT_KEY_DOWN;
                key  = map_key(ev);
            } else if (ev.type == NSEventTypeKeyUp) {
                type = EM_EVENT_KEY_UP;
                key  = map_key(ev);
            }

            [NSApp sendEvent:ev];

            if (type != EM_EVENT_NONE) {
                out->type = type;
                out->key  = key;
                return true;
            }
            /* not a key event — keep draining */
        }
    }
}

void em_window_drawable_size(EmWindow *w, int *outW, int *outH)
{
    if (!w) { if (outW) *outW = 0; if (outH) *outH = 0; return; }
    NSSize sz = w->view.bounds.size;
    CGFloat scale = w->window.backingScaleFactor;
    if (scale <= 0) scale = 1.0;
    if (outW) *outW = (int)(sz.width  * scale);
    if (outH) *outH = (int)(sz.height * scale);
}

void *em_window_native_handle(EmWindow *w)
{
    return w ? (void *)w->view : NULL;
}
