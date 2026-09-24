/* main_loop_and_gap_frame_audit_test.c - recording stubs for the live frame
 * loop em_frame.c, used by the report script
 * tools/test_main_loop_and_gap_frame_audit_reference.py
 * (docs/MAIN_LOOP_AND_GAP.md section 4). It is NOT part of the lane's make
 * target: it re-declares the modules em_frame.c calls, so it has to follow
 * em_frame.c whenever the coordinator changes it.
 *
 * The report script links src/game/em_frame.c with this file into a shared
 * library (no window, no GPU, no audio device) and calls mlg_audit_run.
 * Every module em_frame.c calls is replaced here by a stub that appends its
 * name to a log, so one em_frame_step shows the exact order in which the
 * live loop calls its steps. The report maps those names onto the original
 * steps of 0x1AAE40 and lists the deviations from the translation's order. No
 * behaviour of the stubbed modules is claimed; only the call order of
 * em_frame.c is measured.
 *
 * Scenarios (mlg_audit_run):
 *   0  an ordinary frame (D_00821058 != 1 in the original)
 *   1  the task dispatch arms the movie and the movie pump finishes at once
 *   2  the task dispatch arms the movie and the pump plays two more steps
 */
#include "em_gamepad.h"
#include "em_gfx.h"
#include "em_input.h"
#include "em_platform.h"
#include "game/em_bgm.h"
#include "game/em_fade.h"
#include "game/em_frame.h"
#include "game/em_opening_media.h"
#include "game/em_task.h"

#include <stdio.h>
#include <string.h>

static char s_log[8192];
static size_t s_len;
static int s_scenario;
static int s_movie_steps;

static void rec(const char *name)
{
    size_t n = strlen(name);
    if (s_len + n + 2 >= sizeof s_log) return;
    memcpy(s_log + s_len, name, n);
    s_len += n;
    s_log[s_len++] = ' ';
    s_log[s_len] = '\0';
}

/* ---- platform, gfx, input ------------------------------------------------ */

bool em_window_poll(EmWindow *win, EmEvent *out) { (void)win; (void)out; rec("poll"); return false; }
void em_gamepad_poll(void) { rec("gamepad"); }
void em_gfx_begin_frame(EmGfx *gfx, float r, float g, float b, float a)
{ (void)gfx; (void)r; (void)g; (void)b; (void)a; rec("begin"); }
void em_gfx_end_frame(EmGfx *gfx) { (void)gfx; rec("end"); }
void em_gfx_overlay_canvas(EmGfx *gfx, float w, float h) { (void)gfx; (void)w; (void)h; rec("overlay"); }
void em_gfx_overlay_rect_add(EmGfx *gfx, float x, float y, float w, float h, const float rgb[3])
{ (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)rgb; rec("overlay"); }
void em_gfx_overlay_rect_sub(EmGfx *gfx, float x, float y, float w, float h, const float rgb[3])
{ (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)rgb; rec("overlay"); }
void em_gfx_overlay_rect_sub_before_text(EmGfx *gfx, float x, float y, float w, float h,
                                         const float rgb[3])
{ (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)rgb; rec("overlay"); }
void em_input_init(void) {}
void em_input_handle_event(const EmEvent *ev) { (void)ev; }
void em_input_pad(EmPadState *out) { memset(out, 0, sizeof *out); rec("input_pad"); }
const char *em_pad_button_name(int bit_index) { (void)bit_index; return "?"; }
void em_pad_raw(const EmPadState *pad, uint8_t raw[8]) { (void)pad; memset(raw, 0, 8); rec("pad_raw"); }
uint16_t em_pad_swap(uint16_t mask) { return mask; }
int em_pad_unpack(EmPadUnpack *u, const uint8_t raw[8], unsigned port, int analog)
{ (void)u; (void)raw; (void)port; (void)analog; rec("unpack"); return 1; }

/* ---- game modules -------------------------------------------------------- */

void em_task_init(void) {}
void em_task_dispatch(void)
{
    rec("task_dispatch");
    if (s_scenario != 0) em_frame_set_movie_active(1);
}
void em_bgm_service(void) { rec("bgm_service"); }
void em_opening_media_render(EmGfx *gfx) { (void)gfx; rec("media_render"); }

void em_transition_fade_init(EmTransitionFade *f) { memset(f, 0, sizeof *f); }
void em_transition_fade_clear(EmTransitionFade *f, uint8_t c) { (void)f; (void)c; }
void em_transition_fade_full(EmTransitionFade *f, uint8_t c) { (void)f; (void)c; }
void em_transition_fade_out(EmTransitionFade *f, int16_t s, uint8_t c) { (void)f; (void)s; (void)c; }
void em_transition_fade_in(EmTransitionFade *f, int16_t s, uint8_t c) { (void)f; (void)s; (void)c; }
void em_transition_fade_flash(EmTransitionFade *f, int16_t s) { (void)f; (void)s; }
int em_transition_fade_tick(EmTransitionFade *f) { (void)f; rec("transition_fade_tick"); return 0; }
void em_screen_fade_init(EmScreenFade *f) { memset(f, 0, sizeof *f); }
void em_screen_fade_out(EmScreenFade *f, int16_t s) { (void)f; (void)s; }
void em_screen_fade_in(EmScreenFade *f, int16_t s) { (void)f; (void)s; }
int em_screen_fade_tick(EmScreenFade *f, uint8_t r, uint8_t s)
{ (void)f; (void)r; (void)s; rec("screen_fade_tick"); return 0; }

static int message_tick(void *ctx) { (void)ctx; rec("message_tick"); return 0; }
static void message_render(void *ctx, EmGfx *gfx) { (void)ctx; (void)gfx; rec("message_render"); }

static int movie_pump(void *user)
{
    (void)user;
    rec("movie_pump");
    if (s_movie_steps > 0) { s_movie_steps--; return 1; }
    return 0;
}

/* Run em_frame_step until one engine frame has completed (the counter
 * advanced) or 8 presentation steps passed; "step" separates presentation
 * steps and "counter+1" / "parity^1" mark the engine-frame bookkeeping.
 * Returns the log (valid until the next call). */
const char *mlg_audit_run(int scenario)
{
    static EmFrameMessageService service = { message_tick, message_render, NULL };
    s_scenario = scenario;
    s_movie_steps = scenario == 2 ? 2 : 0;
    em_frame_init(NULL, NULL);
    em_frame_set_message_service(&service);
    em_frame_set_movie_pump(movie_pump, NULL);
    s_len = 0;                   /* em_frame_init's own pad read is not a step */
    s_log[0] = '\0';
    uint32_t counter = em_frame_counter(), parity = em_frame_parity();
    for (int i = 0; i < 8 && em_frame_counter() == counter; i++) {
        rec("step");
        em_frame_step();
    }
    if (em_frame_parity() != parity) rec("parity^1");
    if (em_frame_counter() == counter + 1) rec("counter+1");
    return s_log;
}
