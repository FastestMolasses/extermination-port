/* em_platform_linux.c — Linux windowing (X11; Wayland later).
 *
 * Clean-room, no third-party libraries; only the platform's own client
 * libraries (Xlib/xcb, later libwayland) which ship with the OS. SKELETON —
 * not yet implemented. Plan:
 *   - XOpenDisplay, XCreateWindow, select Key/Structure events.
 *   - em_window_native_handle returns a small {Display*, Window} struct the
 *     Vulkan backend feeds to VK_KHR_xlib_surface.
 *   - em_window_poll translates XNextEvent (KeyPress/KeyRelease/ConfigureNotify
 *     /ClientMessage WM_DELETE_WINDOW) into EmEvent.
 */
#include "em_platform.h"
#include <stddef.h>

EmWindow *em_window_create(const char *title, int width, int height)
{
    (void)title; (void)width; (void)height;
    return NULL; /* TODO: X11 implementation */
}
void em_window_destroy(EmWindow *win) { (void)win; }
bool em_window_poll(EmWindow *win, EmEvent *out) { (void)win; (void)out; return false; }
void em_window_drawable_size(EmWindow *win, int *w, int *h)
{ (void)win; if (w) *w = 0; if (h) *h = 0; }
void *em_window_native_handle(EmWindow *win) { (void)win; return NULL; }
