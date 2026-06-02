/* em_platform_win.c — Windows windowing (Win32).
 *
 * Clean-room, no third-party libraries; only the Win32 API in the platform
 * SDK. SKELETON — not yet implemented. Plan:
 *   - RegisterClassEx + CreateWindowEx; a WndProc that posts WM_CLOSE,
 *     WM_SIZE, WM_KEYDOWN/UP into an EmEvent ring.
 *   - em_window_native_handle returns the HWND for the D3D12 swapchain.
 *   - em_window_poll drains PeekMessage/TranslateMessage/DispatchMessage.
 * Built with the MSVC/clang-cl toolchain (separate from the Make build).
 */
#include "em_platform.h"
#include <stddef.h>

EmWindow *em_window_create(const char *title, int width, int height)
{
    (void)title; (void)width; (void)height;
    return NULL; /* TODO: Win32 implementation */
}
void em_window_destroy(EmWindow *win) { (void)win; }
bool em_window_poll(EmWindow *win, EmEvent *out) { (void)win; (void)out; return false; }
void em_window_drawable_size(EmWindow *win, int *w, int *h)
{ (void)win; if (w) *w = 0; if (h) *h = 0; }
void *em_window_native_handle(EmWindow *win) { (void)win; return NULL; }
