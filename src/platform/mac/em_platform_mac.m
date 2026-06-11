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
/* Consume ALL key events at the view. em_window_poll reads key events off
 * the queue itself; the [NSApp sendEvent:] forward exists only so Cocoa
 * housekeeping (menu key equivalents, window focus, IME bookkeeping) keeps
 * working. NSView's default -keyDown: forwards unhandled keys up the
 * responder chain, where NSWindow's "no responder" path calls NSBeep() —
 * i.e. the system alert sound on EVERY game key press/hold (repeats
 * included). Overriding both as no-ops terminates the chain here, for
 * mapped and unmapped keys alike, without touching the key-equivalent
 * path (Cmd-Q etc. are routed by -sendEvent: before -keyDown:). */
- (void)keyDown:(NSEvent *)event { (void)event; }
- (void)keyUp:(NSEvent *)event   { (void)event; }
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
    bool              altDown;   /* last seen Option/Alt state (flagsChanged) */
    bool              cmdDown;   /* last seen Command state (flagsChanged) */
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
        case 51:  return EM_KEY_BACKSPACE; /* "delete" — chars gives 0x7F,
                                            * outside the printable window */
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
            } else if (ev.type == NSEventTypeFlagsChanged) {
                /* Modifiers never arrive as keyDown/keyUp — synthesize the
                 * EM_KEY_ALT / EM_KEY_CMD transitions the contract promises
                 * (the input model's gait-hold tiers, em_platform.h).
                 * Edge-detect against the last seen state so the L/R keys
                 * and unrelated modifier churn don't emit duplicates; at
                 * most one transition per flagsChanged event (Option wins
                 * the tie; the other edge surfaces on the next event). */
                bool alt = (ev.modifierFlags & NSEventModifierFlagOption) != 0;
                bool cmd = (ev.modifierFlags & NSEventModifierFlagCommand) != 0;
                if (alt != w->altDown) {
                    w->altDown = alt;
                    type = alt ? EM_EVENT_KEY_DOWN : EM_EVENT_KEY_UP;
                    key  = EM_KEY_ALT;
                } else if (cmd != w->cmdDown) {
                    w->cmdDown = cmd;
                    type = cmd ? EM_EVENT_KEY_DOWN : EM_EVENT_KEY_UP;
                    key  = EM_KEY_CMD;
                }
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
