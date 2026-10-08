/* Native presentation loop for original main 001AAE40.
 *
 * The readable gs_readback_queue_run.c establishes the relevant order:
 * frame begin -> input unpack -> 001AEBE0 letterbox fade -> task dispatch
 * -> audio service -> 001AEE70 full-screen transition -> optional blocking
 * movie 00203350 -> movie-only frame close and second transition tick
 * -> vsync/present -> parity and main-frame count.
 *
 * Hardware packet/DMA/GS bookkeeping belongs to the native gfx backend.
 * Step C runs the original unpacker 001B5940 (em_pad_unpack) on a libpad
 * buffer built from the native pad; 001B5B70 is an actuator countdown,
 * not edge post-processing. Native consumers see canonical EM_PAD bits,
 * while original button words swap their high/low bytes. See em_frame.h
 * for the explicit boundary.
 *
 * A native movie pump presents incrementally while the ordinary engine
 * iteration is suspended. This preserves the blocking movie call's task,
 * fade, audio-service and main-counter behavior without blocking the OS
 * event loop. Its return completes the one pending main iteration.
 */
#include "game/em_frame.h"

#include "em_gamepad.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "em_input.h"
#include "game/em_task.h"

static struct {
    EmWindow    *win;
    EmGfx       *gfx;
    bool         quit;
    uint32_t     counter;     /* 0x70003B64 lifetime frame counter */
    uint32_t     parity;      /* 0x00810E80 frame index (0/1) */
    uint16_t     field;       /* 0x00810E88 field bit (em_frame_d810E88) */
    EmFrameInput input;       /* canonical view of pad_block */
    EmPadUnpack  pad_block;   /* 0x00810E70 block + 0x00810E40 analog bytes */
    EmScreenFade screen_fade;         /* 001AEBE0: letterbox bars */
    EmTransitionFade transition;     /* 001AEE70: full-screen effect */
    uint8_t      screen_request;      /* 0x70003B90 drawing gate */
    uint8_t      screen_suppress;     /* 0x008106C4 drawing gate */
    bool         movie_active;        /* 0x00821058 == 1 */
    bool         movie_suspended;     /* main iteration stopped at phase M */
    bool         suspended_draw_transition;
    EmFrameMoviePump movie_pump;
    void        *movie_user;
    EmFrameMessageService message;    /* step F 001FCA10 presenter */
    int (*step_i)(void *);            /* step I 001B5B70 (em_pad_actuator) */
    void        *step_i_context;
    int (*task_check)(void *);        /* after step E: a latched task fault */
    void        *task_check_context;
    int (*step_b)(void *, int32_t);   /* step B 001D1AE0 (em_render_context_live) */
    void        *step_b_context;
    int (*step_v)(void *);            /* step V 001D2300 (em_render_context_live) */
    int (*step_w)(void *, int32_t);   /* step W 001D2580 */
    void        *step_vw_context;
    EmFrameSoundService sound;        /* the field and step H 001FB100 */
    bool         pace_initialized;
    bool         uncapped;
    struct timespec next_deadline;
    /* EM_INPUT_TEST bookkeeping */
    bool         input_test;
    EmPadState   prev_pad;
    /* EM_MOVE_TEST: pad is script-driven; ignore real key events */
    bool         input_scripted;
} s_frame;

void em_frame_init(EmWindow *win, EmGfx *gfx)
{
    memset(&s_frame, 0, sizeof s_frame);
    s_frame.win     = win;
    s_frame.gfx     = gfx;
    s_frame.quit    = false;
    s_frame.counter = 0;
    s_frame.parity  = 0;
    s_frame.input   = (EmFrameInput){ 0x80, 0x80, 0x80, 0x80, 0, 0, 0 };
    /* pad_block stays zero, like the original .bss block. */
    em_screen_fade_init(&s_frame.screen_fade);
    em_transition_fade_init(&s_frame.transition);

    em_input_init();
    em_task_init();

    const char *env    = getenv("EM_INPUT_TEST");
    s_frame.input_test = env && env[0] == '1';
    env                    = getenv("EM_MOVE_TEST");
    s_frame.input_scripted = env && env[0] == '1';
    em_input_pad(&s_frame.prev_pad);
}

void em_frame_request_quit(void)        { s_frame.quit = true; }

void em_frame_fade_start(int dir, int speed)
{
    em_frame_fade_start_colour(dir, speed, EM_FADE_BLACK);
}

void em_frame_fade_start_colour(int dir, int speed, uint8_t colour)
{
    if (dir > 0)
        em_transition_fade_out(&s_frame.transition, (int16_t)speed, colour);
    else
        em_transition_fade_in(&s_frame.transition, (int16_t)speed, colour);
}

void em_frame_fade_clear(uint8_t colour)
{
    em_transition_fade_clear(&s_frame.transition, colour);
}

void em_frame_fade_full(uint8_t colour)
{
    em_transition_fade_full(&s_frame.transition, colour);
}

void em_frame_fade_flash(int speed)
{
    em_transition_fade_flash(&s_frame.transition, (int16_t)speed);
}

const EmTransitionFade *em_frame_transition(void) { return &s_frame.transition; }
float em_frame_fade_level(void) { return s_frame.transition.level / 255.0f; }
int em_frame_fade_active(void)
{
    return s_frame.transition.substate == 1 || s_frame.transition.substate == 3;
}

void em_frame_set_movie_active(int active) { s_frame.movie_active = active != 0; }
int em_frame_movie_active(void) { return s_frame.movie_active; }

void em_frame_set_movie_pump(EmFrameMoviePump pump, void *user)
{
    s_frame.movie_pump = pump;
    s_frame.movie_user = user;
}

void em_frame_set_task_check(int (*check)(void *context), void *context)
{
    s_frame.task_check = check;
    s_frame.task_check_context = context;
}

void em_frame_set_step_i(int (*service)(void *context), void *context)
{
    s_frame.step_i = service;
    s_frame.step_i_context = context;
}

void em_frame_set_sound_service(const EmFrameSoundService *service)
{
    if (service)
        s_frame.sound = *service;
    else
        memset(&s_frame.sound, 0, sizeof s_frame.sound);
}

void em_frame_set_message_service(const EmFrameMessageService *service)
{
    if (service)
        s_frame.message = *service;
    else
        memset(&s_frame.message, 0, sizeof s_frame.message);
}

void em_frame_screen_fade_start(int dir, int speed)
{
    if (dir > 0)
        em_screen_fade_out(&s_frame.screen_fade, (int16_t)speed);
    else
        em_screen_fade_in(&s_frame.screen_fade, (int16_t)speed);
}

void em_frame_screen_fade_gate(uint8_t request, uint8_t suppress)
{
    s_frame.screen_request = request;
    s_frame.screen_suppress = suppress;
}

const EmScreenFade *em_frame_screen_fade(void) { return &s_frame.screen_fade; }

static void frame_transition_draw(void)
{
    float level = em_frame_fade_level();
    float rgb[3] = {level, level, level};
    em_gfx_overlay_canvas(s_frame.gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
    if (s_frame.transition.colour == EM_FADE_BLACK)
        em_gfx_overlay_rect_sub(s_frame.gfx, 0, 0, EM_GFX_OVERLAY_W,
                                EM_GFX_OVERLAY_H, rgb);
    else
        em_gfx_overlay_rect_add(s_frame.gfx, 0, 0, EM_GFX_OVERLAY_W,
                                EM_GFX_OVERLAY_H, rgb);
}

static void frame_screen_fade_draw(void)
{
    /* 001AE900's two rectangles cover the first/last 32 of 224 field
     * lines, hence 64 of the native 448-line canvas. */
    const float bar_height = EM_GFX_OVERLAY_H / 7.0f;
    float level = s_frame.screen_fade.level / 255.0f;
    float rgb[3] = {level, level, level};
    em_gfx_overlay_canvas(s_frame.gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
    em_gfx_overlay_rect_sub_before_text(s_frame.gfx, 0, 0, EM_GFX_OVERLAY_W, bar_height, rgb);
    em_gfx_overlay_rect_sub_before_text(s_frame.gfx, 0, EM_GFX_OVERLAY_H - bar_height,
                            EM_GFX_OVERLAY_W, bar_height, rgb);
}
const EmFrameInput *em_frame_input(void){ return &s_frame.input; }
const EmPadUnpack *em_frame_pad_block(void) { return &s_frame.pad_block; }

/* See em_frame.h: the original-layout words need no remapping; the native
 * EM_PAD view (s_frame.input) is the swapped one. */
void em_frame_scene_input(EmSceneState *scene)
{
    if (!scene)
        return;
    scene->d810E74 = s_frame.pad_block.pressed;
    scene->d810E70 = s_frame.pad_block.held;
    scene->d810E50 = 4;
}
EmWindow *em_frame_window(void)         { return s_frame.win; }
EmGfx    *em_frame_gfx(void)            { return s_frame.gfx; }
uint32_t  em_frame_counter(void)        { return s_frame.counter; }
const uint32_t *em_frame_counter_storage(void) { return &s_frame.counter; }
uint32_t  em_frame_parity(void)         { return s_frame.parity; }

/* The host's frame pacing (frame_pace_ntsc): the pacing flags and the
 * CLOCK_MONOTONIC deadline. Host state only (no original counterpart);
 * tools/test_new_game_switch.py leaves these bytes out of its comparison. */
const void *em_frame_host_pacing(size_t *size)
{
    *size = (size_t)((const char *)(&s_frame.next_deadline + 1) - (const char *)&s_frame.pace_initialized);
    return &s_frame.pace_initialized;
}
/* The low two bytes of the (little-endian) parity word are the halfword
 * D_00810E80 (0 or 1). */
uint8_t  *em_frame_d810E80(void)        { return (uint8_t *)&s_frame.parity; }
uint8_t  *em_frame_d810E88(void)        { return (uint8_t *)&s_frame.field; }

void em_frame_set_step_vw(int (*step_v)(void *context), int (*step_w)(void *context, int32_t field),
                          void *context)
{
    s_frame.step_v = step_v;
    s_frame.step_w = step_w;
    s_frame.step_vw_context = context;
}

void em_frame_set_step_b(int (*service)(void *context, int32_t index), void *context)
{
    s_frame.step_b = service;
    s_frame.step_b_context = context;
}

/* EM_INPUT_TEST=1: one compact line per pad-state change. */
static void input_test_print(const EmPadState *pad)
{
    printf("pad 0x%04x [", pad->buttons);
    const char *sep = "";
    for (int b = 0; b < 16; b++) {
        if (pad->buttons & (1u << b)) {
            printf("%s%s", sep, em_pad_button_name(b));
            sep = " ";
        }
    }
    printf("] L(%+.1f,%+.1f) R(%+.1f,%+.1f)\n",
           pad->lx, pad->ly, pad->rx, pad->ry);
    fflush(stdout);
}

static bool pad_changed(const EmPadState *a, const EmPadState *b)
{
    return a->buttons != b->buttons ||
           a->lx != b->lx || a->ly != b->ly ||
           a->rx != b->rx || a->ry != b->ry;
}

/* Step C: pump platform events, then 001B57E0 -> 001B5F40 -> 001B5940.
 * The native pad is a connected DualShock in the stable libpad state, so
 * 001B5F40 always takes its analog call (state 6: a2 = 1) on port 0. A
 * native read never fails, so 001B57E0's failure clear is not reachable. */
static void frame_input_read(void)
{
    EmEvent ev;
    while (em_window_poll(s_frame.win, &ev)) {
        if (!s_frame.input_scripted) em_input_handle_event(&ev);
        switch (ev.type) {
            case EM_EVENT_QUIT:
                s_frame.quit = true;
                break;
            case EM_EVENT_KEY_DOWN:
                if (ev.key == EM_KEY_ESCAPE) s_frame.quit = true;
                break;
            default:
                break;
        }
    }

    EmPadState pad;
    em_input_pad(&pad);

    if (s_frame.input_test && pad_changed(&pad, &s_frame.prev_pad)) {
        input_test_print(&pad);
        s_frame.prev_pad = pad;
    }

    uint8_t raw[8];
    EmPadUnpack *block = &s_frame.pad_block;
    em_pad_raw(&pad, raw);
    (void)em_pad_unpack(block, raw, 0, 1);

    EmFrameInput *in = &s_frame.input;
    in->lx       = block->lx;
    in->ly       = block->ly;
    in->rx       = block->rx;
    in->ry       = block->ry;
    in->held     = em_pad_swap(block->held);
    in->pressed  = em_pad_swap(block->pressed);
    in->released = em_pad_swap((uint16_t)(block->prev_held & ~block->held));
}

/* NTSC frame pacing (~59.94 Hz = 60/1.001). See the call site in
 * em_frame_run. Monotonic absolute deadlines via clock_gettime. */
static void frame_pace_ntsc(void)
{
    const long period_ns = 16683350L;
    struct timespec *next = &s_frame.next_deadline;
    if (!s_frame.pace_initialized) {
        const char *e = getenv("EM_UNCAPPED");
        s_frame.uncapped = (e && e[0] == '1');
        s_frame.pace_initialized = true;
        clock_gettime(CLOCK_MONOTONIC, next);
    }
    if (s_frame.uncapped) return;
    next->tv_nsec += period_ns;
    while (next->tv_nsec >= 1000000000L) { next->tv_nsec -= 1000000000L; next->tv_sec++; }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > next->tv_sec ||
        (now.tv_sec == next->tv_sec && now.tv_nsec >= next->tv_nsec)) {
        *next = now; /* behind schedule: reset deadline without a catch-up burst */
        return;
    }
    struct timespec rem = { next->tv_sec - now.tv_sec, next->tv_nsec - now.tv_nsec };
    if (rem.tv_nsec < 0) { rem.tv_nsec += 1000000000L; rem.tv_sec--; }
    nanosleep(&rem, NULL);
}

int em_frame_step(void)
{
    if (s_frame.quit) return 0;
    /* One NTSC field per step, movie steps included: the vblank handler's
     * D_00810E90 and the IOP's field (em_stream_live_field). */
    if (s_frame.sound.field && s_frame.sound.field(s_frame.sound.context) < 0) {
        s_frame.quit = true;
        return 0;
    }

    /* B/C: native frame begin and the original input-unpack phase. */
    /* 001AB370 sets both sceGsDBuffDc clear colours (0x00811020/0x00811190)
     * to RGBA 0,0,0,0x80: the original draw buffer clears to black. */
    em_gfx_begin_frame(s_frame.gfx, 0.0f, 0.0f, 0.0f, 1.0f);
    /* B: 001D1AE0(D_00810E80), the render context's buffer set-up; the
     * blocking movie holds the original iteration, so not while it plays. */
    if (!s_frame.movie_suspended && s_frame.step_b &&
        s_frame.step_b(s_frame.step_b_context, (int32_t)(int16_t)s_frame.parity) < 0)
        s_frame.quit = true;
    em_gamepad_poll();
    frame_input_read();

    if (s_frame.movie_suspended) goto movie_phase;

    /* D: 001AEBE0 updates the separate letterbox effect before tasks. */
    if (em_screen_fade_tick(&s_frame.screen_fade, s_frame.screen_request,
                            s_frame.screen_suppress))
        frame_screen_fade_draw();

    /* E/F/G: task requests affect the transition's SAME-frame tick. */
    em_task_dispatch();
    /* A task whose worker latched a fault (the screen-module loader's
     * slot-2 task) stops the game here: fail-stop, never a hung wait. */
    if (s_frame.task_check && s_frame.task_check(s_frame.task_check_context) < 0) {
        s_frame.quit = true;
        return 0;
    }
    /* F: 001FCA10, the message service, after every script worker. */
    if (s_frame.message.tick && s_frame.message.tick(s_frame.message.context) < 0)
        s_frame.quit = true;
    /* Original message presentation overlays the already queued bars,
     * then the full-screen transition composites over the whole image. */
    if (s_frame.message.render)
        s_frame.message.render(s_frame.message.context, s_frame.gfx);
    s_frame.suspended_draw_transition =
        em_transition_fade_tick(&s_frame.transition) != 0;
    /* H: 001FB100 unless D_00821058 == 1 (a movie armed this frame): the
     * stream lanes' service 001F9CF0, the D_0028215B output-mode commit,
     * the D_00281B70 copy and 001FC6E0's delayed cues (em_stream_live_step_h;
     * docs/STREAM_LANES.md "Binding"). */
    if (!s_frame.movie_active && s_frame.sound.step_h &&
        s_frame.sound.step_h(s_frame.sound.context) < 0)
        s_frame.quit = true;
    /* I: 001B5B70, the rumble countdown. */
    if (s_frame.step_i && s_frame.step_i(s_frame.step_i_context) < 0)
        s_frame.quit = true;

    /* 00203350 blocks the original main iteration at M while playing.
     * Native presentation is incremental, so preserve that suspension
     * across calls: input/media/presentation run but tasks, fades and
     * the main iteration counter do not advance during playback. */
    if (s_frame.movie_active) s_frame.movie_suspended = true;
movie_phase:
    if (s_frame.movie_suspended) {
        if (s_frame.movie_active && s_frame.movie_pump)
            s_frame.movie_active = s_frame.movie_pump(s_frame.movie_user) != 0;
        if (s_frame.movie_active && !s_frame.quit) {
            em_gfx_end_frame(s_frame.gfx);
            return 1;
        }
        /* N/O occur once after the blocking movie function returns. */
        s_frame.movie_suspended = false;
        s_frame.suspended_draw_transition |=
            em_transition_fade_tick(&s_frame.transition) != 0;
    }
    if (s_frame.suspended_draw_transition)
        frame_transition_draw();

    /* P: the vblank the original loop waits for. Its handler stores the
     * field; one field per iteration, in the measured phase (field ==
     * D_00810E80 at step V, em_frame.h em_frame_d810E88). */
    s_frame.field = (uint16_t)(s_frame.parity & 1u);
    /* V: 001D2300 builds the frame's list; its kick is the presentation. */
    if (s_frame.step_v && s_frame.step_v(s_frame.step_vw_context) < 0)
        s_frame.quit = true;
    em_gfx_end_frame(s_frame.gfx);
    /* W: the field is read first, then D_00810E80 flips, then 001D2580. */
    const int32_t field = (int16_t)s_frame.field;
    s_frame.parity ^= 1u;
    if (s_frame.step_w && s_frame.step_w(s_frame.step_vw_context, field) < 0)
        s_frame.quit = true;
    s_frame.counter++;
    return !s_frame.quit;
}

/* EM_FRAME_TIMING=<file>: a diagnostic that writes one line per step of
 * em_frame_run ("step wall_ns cpu_ns": the step's wall time and the main
 * thread's CPU time, the pacing sleep excluded) and prints a summary
 * against the 16.683 ms NTSC period at exit. It changes nothing the game
 * computes. */
static int64_t timing_ns(clockid_t clock)
{
    struct timespec t;
    clock_gettime(clock, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

static int timing_cmp(const void *a, const void *b)
{
    const int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

static void timing_summary(const char *what, int64_t *v, size_t n)
{
    if (!n) return;
    int64_t sum = 0;
    size_t over = 0;
    for (size_t i = 0; i < n; ++i) {
        sum += v[i];
        over += v[i] > 16683350;
    }
    qsort(v, n, sizeof *v, timing_cmp);
    fprintf(stderr, "frame timing: %s mean %.2f ms, p50 %.2f, p95 %.2f, p99 %.2f, max %.2f; %zu of %zu steps over the 16.68 ms period\n",
            what, (double)sum / (double)n / 1e6, (double)v[n / 2] / 1e6, (double)v[n * 95 / 100] / 1e6,
            (double)v[n * 99 / 100] / 1e6, (double)v[n - 1] / 1e6, over, n);
}

void em_frame_run(void)
{
    const char *timing = getenv("EM_FRAME_TIMING");
    FILE *tf = (timing && timing[0]) ? fopen(timing, "w") : NULL;
    int64_t *wall = NULL, *cpu = NULL;
    size_t n = 0, cap = 0;
    while (!s_frame.quit) {
        const int64_t w0 = tf ? timing_ns(CLOCK_MONOTONIC) : 0, c0 = tf ? timing_ns(CLOCK_THREAD_CPUTIME_ID) : 0;
        em_frame_step();
        if (tf) {
            const int64_t dw = timing_ns(CLOCK_MONOTONIC) - w0, dc = timing_ns(CLOCK_THREAD_CPUTIME_ID) - c0;
            fprintf(tf, "%zu %lld %lld\n", n, (long long)dw, (long long)dc);
            if (n == cap) {
                const size_t grown = cap ? 2 * cap : 4096;
                int64_t *nw = realloc(wall, grown * sizeof *wall);
                if (nw) wall = nw;
                int64_t *nc = nw ? realloc(cpu, grown * sizeof *cpu) : NULL;
                if (nc) cpu = nc;
                if (nw && nc) cap = grown;
            }
            if (n < cap) {
                wall[n] = dw;
                cpu[n] = dc;
                n++;
            }
        }
        /* One logic tick per NTSC vblank, independent of display rate.
         * Sleeping belongs only to the interactive loop, not the
         * deterministic single-frame interface used by headless tests. */
        frame_pace_ntsc();
    }
    if (tf) {
        fclose(tf);
        timing_summary("wall", wall, n);
        timing_summary("main-thread cpu", cpu, n);
    }
    free(wall);
    free(cpu);
}
