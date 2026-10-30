/* Display-free Linux platform for CPU proofs. The visible X11 runtime is
 * deliberately unavailable until its presenter and input are implemented. */
#include "em_platform.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

struct EmWindow { int width, height; };

EmWindow *em_window_create(const char *title, int width, int height)
{
    (void)title;
    if (!em_headless()) {
        fprintf(stderr, "Linux: X11 runtime is not implemented; headless proofs require EM_HEADLESS=1\n");
        return NULL;
    }
    if (width <= 0 || height <= 0) return NULL;
    EmWindow *win = calloc(1, sizeof *win);
    if (win) { win->width = width; win->height = height; }
    return win;
}
void em_window_destroy(EmWindow *win) { free(win); }
bool em_window_poll(EmWindow *win, EmEvent *out) { (void)win; (void)out; return false; }
void em_window_drawable_size(EmWindow *win, int *w, int *h)
{ if (w) *w = win ? win->width : 0; if (h) *h = win ? win->height : 0; }
void *em_window_native_handle(EmWindow *win) { (void)win; return NULL; }
