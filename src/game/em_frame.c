/* em_frame.c — per-frame phase sequence (PS2 main loop func_001AAE40).
 *
 * PS2 step -> native step mapping (FINDINGS.md "ENGINE FRAME ANATOMY"):
 *
 *  A  clear vsync flag (0x00810E98)      NO-OP — the flag exists for the
 *                                        VBLANK ISR handshake; natively the
 *                                        blocking present IS the vsync wait,
 *                                        so there is no flag and no ISR.
 *  B  func_001D1AE0 frame begin          em_gfx_begin_frame() — the PS2
 *     (select per-frame CPU packet       selects the parity-indexed packet
 *     arena, ~0x95800 bytes/frame)       arena; natively the gfx backend
 *                                        acquires the swapchain image and
 *                                        opens the frame's command stream.
 *  C  func_001B57E0 input read/unpack    frame_input_read() — OS event pump
 *     (libpad RPC buffer 0x00810E40 ->   feeds em_input (the "unpacker");
 *     frame block 0x00810E60..7B)        snapshot lands in the EmFrameInput
 *                                        block, edges computed here.
 *  D  func_001AEBE0 screen-fade machine  frame_fade_tick() — the native
 *     (armed by func_001AEDE0(speed,     fade level machine (64-frame ramp
 *     dir); door commits use speed 4)    at the captured door speed 4); the
 *                                        level draws as a full-screen black
 *                                        overlay rect at close-out.
 *  E  func_001AB6A0 TASK DISPATCH        em_task_dispatch() — ALL game
 *                                        logic, exactly as on PS2.
 *  F  func_001FCA10 audio service        em_bgm_service() — em_audio is
 *                                        pull-model (the OS audio thread
 *                                        mixes), so the per-frame call is
 *                                        only the game-thread half of the
 *                                        BGM stream: retire swapped-out
 *                                        tracks the audio thread has
 *                                        acked. The PS2 pumped IOP RPC
 *                                        here.
 *  G  func_001AEE70 transition/          NO-OP HOOK — same family as D.
 *     brightness machine
 *  H  func_001FB100 audio service        folded into F — one native
 *                                        service call covers all three
 *                                        PS2 pumps (no RPC to drain).
 *  I  func_001B5B70 input post-process   folded into C — pressed/released
 *                                        edges are computed at snapshot
 *                                        time; nothing left to do here.
 *  J  func_00100A60 VIF1/GIF/VU1 sync    NO-OP — no DMA paths or vector
 *     + conditional VU0/VU1 macro block  units; GPU/CPU sync is owned by
 *                                        the gfx backend.
 *  K  func_001D7410 GS-VRAM readback     NO-OP — gated OFF (*(gp-0x7768))
 *                                        in every live sample on PS2 too.
 *  L  func_001AB590 DMA CHCR watchdog    NO-OP — guards against wedged
 *                                        D0/D1/D2 DMA channels; no DMA
 *                                        controller exists natively.
 *  M  conditional func_00203350 audio    folded into F — as H.
 *  N  func_001D1C10 frame-end render     NO-OP — closes the per-frame
 *     bookkeeping                        packet arena; natively the command
 *                                        stream is closed by end_frame (P).
 *  O  func_001AEE70 (second fade tick)   NO-OP HOOK — as G.
 *  P  poll 0x00810E98 until set (vsync   em_gfx_end_frame() — submit +
 *     wait; the VBLANK ISR func_001AB140 present; the blocking present is
 *     sets it and wakes the audio        the native vsync wait. No ISR, no
 *     thread)                            wakeup: the OS audio thread is
 *                                        driven by the audio device itself.
 *  Q  T0_COUNT = 0                       NO-OP — EE hardware timer reset.
 *  R  func_001AB4E0 display offset       NO-OP — CRTC display-area offset;
 *                                        the window system owns placement.
 *  S  PutDispEnv x2 (FIELD-indexed       NO-OP — the dispenv flip selects
 *     dispenv flip)                      which GS buffer the CRTC scans
 *                                        out; the swapchain does this.
 *  T  func_0010BAA0 GS register apply    NO-OP — GS privileged registers
 *                                        have no native counterpart.
 *  U  func_00100550 per-frame GS env     NO-OP — double-buffered GS context
 *     (0x00810EA0 + parity*0x28)         (scissor/frame/zbuf); the render
 *                                        pass set up in B covers it.
 *  V  func_001D2300 render frame-flip    NO-OP — swapchain bookkeeping.
 *     bookkeeping
 *  W  frame parity ^= 1 (0x00810E80);    kept — parity + lifetime counter
 *     counters++ (0x70003B64)            (em_frame_parity/em_frame_counter),
 *                                        the engine's double-buffer index.
 *
 * Debug instrumentation (port-side, not engine structure): EM_INPUT_TEST=1
 * prints one line per pad-state change from step C, exactly as the
 * pre-architecture shell did. EM_MOVE_TEST=1 (the game-side scripted
 * movement self-test, em_game.c) makes step C stop feeding REAL key events
 * into em_input, so stray keystrokes hitting the focused window can't
 * perturb the deterministic script; window-quit and Esc still work.
 */
#include "game/em_frame.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "em_input.h"
#include "game/em_bgm.h"
#include "game/em_task.h"

static struct {
    EmWindow    *win;
    EmGfx       *gfx;
    bool         quit;
    uint32_t     counter;     /* 0x70003B64 lifetime frame counter */
    uint32_t     parity;      /* 0x00810E80 frame index (0/1) */
    EmFrameInput input;       /* 0x00810E60 frame input block */
    uint16_t     prev_held;   /* previous frame's buttons, for edges */
    /* screen-fade machine (step D, func_001AEDE0) */
    float        fade_level;  /* 0 = clear .. 1 = full black */
    int          fade_dir;    /* +1 fading out, -1 fading in, 0 settled */
    int          fade_speed;  /* engine units: level steps speed/256 */
    /* EM_INPUT_TEST bookkeeping */
    bool         input_test;
    EmPadState   prev_pad;
    /* EM_MOVE_TEST: pad is script-driven; ignore real key events */
    bool         input_scripted;
} s_frame;

void em_frame_init(EmWindow *win, EmGfx *gfx)
{
    s_frame.win     = win;
    s_frame.gfx     = gfx;
    s_frame.quit    = false;
    s_frame.counter = 0;
    s_frame.parity  = 0;
    s_frame.input   = (EmFrameInput){ 0x80, 0x80, 0x80, 0x80, 0, 0, 0 };
    s_frame.prev_held = 0;

    em_input_init();
    em_task_init();

    const char *env    = getenv("EM_INPUT_TEST");
    s_frame.input_test = env && env[0] == '1';
    env                    = getenv("EM_MOVE_TEST");
    s_frame.input_scripted = env && env[0] == '1';
    em_input_pad(&s_frame.prev_pad);
}

void em_frame_request_quit(void)        { s_frame.quit = true; }

/* func_001AEDE0(speed, dir) — arm the screen fade (see em_frame.h). */
void em_frame_fade_start(int dir, int speed)
{
    s_frame.fade_dir   = (dir > 0) ? 1 : -1;
    s_frame.fade_speed = speed;
}

float em_frame_fade_level(void)  { return s_frame.fade_level; }
int   em_frame_fade_active(void) { return s_frame.fade_dir != 0; }

/* Step D — one tick of the fade machine: the level ramps speed/256 per
 * frame toward the armed end (speed 4 = the captured 64-frame door
 * fade), then the machine settles. */
static void frame_fade_tick(void)
{
    if (!s_frame.fade_dir) return;
    s_frame.fade_level += (float)s_frame.fade_dir *
                          (float)s_frame.fade_speed / 256.0f;
    if (s_frame.fade_level >= 1.0f) {
        s_frame.fade_level = 1.0f;
        if (s_frame.fade_dir > 0) s_frame.fade_dir = 0;
    }
    if (s_frame.fade_level <= 0.0f) {
        s_frame.fade_level = 0.0f;
        if (s_frame.fade_dir < 0) s_frame.fade_dir = 0;
    }
}
const EmFrameInput *em_frame_input(void){ return &s_frame.input; }
EmWindow *em_frame_window(void)         { return s_frame.win; }
EmGfx    *em_frame_gfx(void)            { return s_frame.gfx; }
uint32_t  em_frame_counter(void)        { return s_frame.counter; }
uint32_t  em_frame_parity(void)         { return s_frame.parity; }

/* float [-1,1] -> raw 0x80-centered stick byte (0x00 / 0x80 / 0xFF). */
static uint8_t stick_byte(float axis)
{
    int v = 0x80 + (int)(axis * 128.0f);
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
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

/* Step C: pump platform events into em_input (and the quit logic), then
 * unpack the pad snapshot into the frame input block. Step I's edge
 * post-processing is folded in here. */
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

    EmFrameInput *in = &s_frame.input;
    in->lx       = stick_byte(pad.lx);
    in->ly       = stick_byte(pad.ly);
    in->rx       = stick_byte(pad.rx);
    in->ry       = stick_byte(pad.ry);
    in->held     = pad.buttons;
    in->pressed  = (uint16_t)(pad.buttons & ~s_frame.prev_held);
    in->released = (uint16_t)(s_frame.prev_held & ~pad.buttons);
    s_frame.prev_held = pad.buttons;
}

void em_frame_run(void)
{
    while (!s_frame.quit) {
        /* A: clear vsync flag — no-op (see mapping table). */

        /* B: frame begin — acquire swapchain image + clear. */
        em_gfx_begin_frame(s_frame.gfx, 0.08f, 0.09f, 0.12f, 1.0f);

        /* C (+I): input read/unpack into the frame input block. */
        frame_input_read();

        /* D: screen-fade machine (func_001AEDE0 tick) — armed by the
         * door-transit commit (em_door.c); the level is DRAWN as the
         * close-out's full-screen overlay rect (em_game.c). */
        frame_fade_tick();

        /* E: TASK DISPATCH — all game logic. */
        em_task_dispatch();

        /* F: audio service — the game-thread half of the pull-model BGM
         * stream (em_bgm.c); the PS2's H and M pumps are folded in. */
        em_bgm_service();

        /* G, H: transition machine / audio service — no-op hook /
         * folded into F. */

        /* J, K, L, M: VU sync / GS readback / DMA watchdog / audio —
         * no-op + folded into F (PS2 hardware paths; see mapping table). */

        /* N, O: frame-end render bookkeeping / second fade tick — no-op. */

        /* P..V: vsync wait, timer reset, dispenv flip, GS env apply,
         * frame-flip bookkeeping — submit + blocking present. */
        em_gfx_end_frame(s_frame.gfx);

        /* W: frame parity flip + lifetime counter. */
        s_frame.parity ^= 1u;
        s_frame.counter++;
    }
}
