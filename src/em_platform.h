/* em_platform.h — cross-platform windowing + input for the Extermination
 * native port.
 *
 * Clean-room: implemented directly on each OS's native API (Cocoa on macOS,
 * Win32 on Windows, X11/Wayland on Linux). NO third-party libraries. The
 * implementation files live under src/platform/<os>/.
 *
 * The window owns a native surface that the graphics backend (em_gfx.h)
 * attaches to. The platform layer never talks to a GPU API directly.
 */
#ifndef EM_PLATFORM_H
#define EM_PLATFORM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EmWindow EmWindow;

typedef enum {
    EM_EVENT_NONE = 0,
    EM_EVENT_QUIT,        /* window closed / app asked to quit */
    EM_EVENT_RESIZE,      /* width/height carry the new client size (points) */
    EM_EVENT_KEY_DOWN,    /* key carries an EmKey code */
    EM_EVENT_KEY_UP
} EmEventType;

/* Platform-neutral key codes. Printable ASCII keys use their ASCII value;
 * non-printable keys use values >= 256 so they never collide. Extend as the
 * input map grows. */
typedef enum {
    EM_KEY_UNKNOWN = 0,
    EM_KEY_SPACE   = 32,
    EM_KEY_ESCAPE  = 256,
    EM_KEY_RETURN,
    EM_KEY_TAB,
    EM_KEY_LEFT,
    EM_KEY_RIGHT,
    EM_KEY_UP,
    EM_KEY_DOWN
} EmKey;

typedef struct {
    EmEventType type;
    int         key;            /* for KEY_DOWN / KEY_UP */
    int         width, height;  /* for RESIZE (client size, points) */
} EmEvent;

/* Create a window. Returns NULL on failure. */
EmWindow *em_window_create(const char *title, int width, int height);
void      em_window_destroy(EmWindow *win);

/* Drain one pending event into *out. Returns true if an event was produced,
 * false when the queue is empty for this iteration. Call in a loop each
 * frame:  while (em_window_poll(win, &ev)) { ... } */
bool em_window_poll(EmWindow *win, EmEvent *out);

/* Current drawable size in PIXELS (i.e. points * backing scale). The gfx
 * backend uses this to size its swapchain. */
void em_window_drawable_size(EmWindow *win, int *w, int *h);

/* Opaque native handle the gfx backend attaches to:
 *   macOS   -> NSView pointer (layer-backed; its layer is a CAMetalLayer)
 *   Windows -> HWND
 *   Linux   -> platform-specific struct ptr (Xlib Display+Window, or wl_surface)
 * The gfx backend for a platform knows how to interpret it. */
void *em_window_native_handle(EmWindow *win);

#ifdef __cplusplus
}
#endif

#endif /* EM_PLATFORM_H */
