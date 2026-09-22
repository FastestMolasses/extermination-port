/* State and scheduler boundary checks for original 001AEBE0/001AEE70.
 * The gfx/platform shims record effects while using the real frame,
 * task and input modules. No window, GPU, assets or timing waits. */
#include "game/em_fade.h"
#include "game/em_frame.h"
#include "game/em_task.h"
#include "em_input.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int task_calls, audio_calls, movie_calls, presents;
static int arm_movie;
static int frame_draws;
static int expected_frame;
static EmEvent event;
static int event_pending;
static struct { int add, before_text; float y, h, level; } draws[8];

bool em_window_poll(EmWindow *window, EmEvent *out)
{
    (void)window;
    if (!event_pending) return false;
    *out = event;
    event_pending = 0;
    return true;
}
void em_gamepad_poll(void) {}
void em_bgm_service(void) { ++audio_calls; }
void em_opening_media_render(EmGfx *gfx) { (void)gfx; }
void em_gfx_begin_frame(EmGfx *gfx, float r, float g, float b, float a)
{
    (void)gfx; (void)r; (void)g; (void)b; (void)a;
    frame_draws = 0;
}
void em_gfx_end_frame(EmGfx *gfx) { (void)gfx; ++presents; }
void em_gfx_overlay_canvas(EmGfx *gfx, float w, float h)
{
    (void)gfx;
    assert(w == EM_GFX_OVERLAY_W && h == EM_GFX_OVERLAY_H);
}
static void record_draw(int add, float x, float y, float w, float h,
                        const float rgb[3])
{
    assert(frame_draws < 8);
    assert(x == 0 && w == EM_GFX_OVERLAY_W);
    assert(rgb[0] == rgb[1] && rgb[1] == rgb[2]);
    draws[frame_draws].add = add;
    draws[frame_draws].before_text = 0;
    draws[frame_draws].y = y;
    draws[frame_draws].h = h;
    draws[frame_draws++].level = rgb[0];
}
void em_gfx_overlay_rect_sub(EmGfx *gfx, float x, float y, float w, float h,
                             const float rgb[3])
{ (void)gfx; record_draw(0, x, y, w, h, rgb); }
void em_gfx_overlay_rect_sub_before_text(EmGfx *gfx, float x, float y,
                                        float w, float h, const float rgb[3])
{
    (void)gfx;
    record_draw(0, x, y, w, h, rgb);
    draws[frame_draws-1].before_text = 1;
}
void em_gfx_overlay_rect_add(EmGfx *gfx, float x, float y, float w, float h,
                             const float rgb[3])
{ (void)gfx; record_draw(1, x, y, w, h, rgb); }

static void test_transition(void)
{
    EmTransitionFade f;
    em_transition_fade_init(&f);
    assert(f.level == 0 && f.substate == 0 && f.step == 0);
    assert(em_transition_fade_tick(&f) == 0);

    em_transition_fade_out(&f, 4, EM_FADE_BLACK);
    for (int tick = 1; tick <= 63; ++tick) {
        assert(em_transition_fade_tick(&f));
        assert(f.level == 4 * tick && f.mode == 3 && f.substate == 3);
    }
    assert(em_transition_fade_tick(&f));
    assert(f.level == 255 && f.mode == 1 && f.substate == 2);
    em_transition_fade_in(&f, 4, EM_FADE_WHITE);
    assert(f.level == 255 && f.colour == EM_FADE_WHITE);
    for (int tick = 1; tick <= 63; ++tick) {
        assert(em_transition_fade_tick(&f));
        assert(f.level == 255 - 4 * tick && f.substate == 1);
    }
    assert(em_transition_fade_tick(&f)); /* draws on the settling tick */
    assert(f.level == 0 && f.mode == 0 && f.substate == 0);
    assert(!em_transition_fade_tick(&f));

    /* Exact zero is still active; this catches the previous <=0 port
     * approximation. Mid-ramp direction reversal preserves level. */
    em_transition_fade_out(&f, 4, EM_FADE_BLACK);
    em_transition_fade_tick(&f);
    em_transition_fade_in(&f, 4, EM_FADE_BLACK);
    assert(f.level == 4);
    em_transition_fade_tick(&f);
    assert(f.level == 0 && f.substate == 1 && f.mode == 2);
    em_transition_fade_tick(&f);
    assert(f.level == 0 && f.substate == 0 && f.mode == 0);

    /* Force operations preserve step; flashing holds level for three
     * selector ticks (4->5->6->2), then resumes the fade-in. */
    em_transition_fade_full(&f, EM_FADE_WHITE);
    assert(f.level == 255 && f.step == 4 && f.colour == 1);
    em_transition_fade_flash(&f, 8);
    assert(f.colour == 0);
    const int modes[] = {5, 6, 2};
    for (int i = 0; i < 3; ++i) {
        em_transition_fade_tick(&f);
        assert(f.mode == modes[i] && f.level == 255);
    }
    em_transition_fade_tick(&f);
    assert(f.level == 247);

    /* Mode/substate mismatches are meaningful original handshake
     * cases. Their transition does not perform the next arm's tick. */
    f.mode = 0; f.substate = 3; f.level = 100; f.colour = 1;
    assert(!em_transition_fade_tick(&f));
    assert(f.mode == 3 && f.level == 100 && f.step == 4 && f.colour == 0);
    f.mode = 1; f.substate = 1;
    assert(em_transition_fade_tick(&f));
    assert(f.mode == 2 && f.level == 100);
    f.substate = 2;
    em_transition_fade_tick(&f);
    assert(f.mode == 3 && f.level == 100 && f.substate == 2);
    f.substate = 0;
    em_transition_fade_tick(&f);
    assert(f.mode == 2 && f.level == 100 && f.substate == 0);
    em_transition_fade_clear(&f, 1);
    assert(f.level == 0 && f.step == 4 && f.colour == 1);
}

static void test_screen(void)
{
    EmScreenFade f;
    em_screen_fade_init(&f);
    em_screen_fade_in(&f, 8); /* clear destination: true no-op */
    assert(f.state == 0 && f.level == 0 && f.step == 0);
    em_screen_fade_out(&f, 255);
    assert(em_screen_fade_tick(&f, 0, 0));
    assert(f.state == 1 && f.level == 255);
    em_screen_fade_out(&f, 8); /* full destination: true no-op */
    assert(f.state == 1 && f.step == 255);
    em_screen_fade_in(&f, 255);
    assert(!em_screen_fade_tick(&f, 2, 1)); /* gate drawing only */
    assert(f.level == 0 && f.state == 2);
    assert(em_screen_fade_tick(&f, 1, 1));
    assert(f.level == 0 && f.state == 0);
    assert(!em_screen_fade_tick(&f, 0, 0));
    em_screen_fade_out(&f, 4);
    em_screen_fade_tick(&f, 0, 0);
    assert(f.level == 4);
    em_screen_fade_in(&f, 8); /* unlike transition, resets to full */
    assert(f.level == 255 && f.state == 2 && f.step == 8);
}

static void frame_task(void)
{
    assert(em_frame_counter() == (unsigned)expected_frame);
    ++task_calls;
    if (task_calls == 1) {
        assert(em_frame_transition()->level == 0);
        em_frame_fade_start_colour(1, 4, EM_FADE_WHITE);
        if (arm_movie) em_frame_set_movie_active(1);
    }
}

static int movie_pump(void *user)
{
    assert(user == &movie_calls);
    ++movie_calls;
    assert(task_calls == 1 && audio_calls == 1);
    assert(em_frame_counter() == 0 && em_frame_parity() == 0);
    assert(em_frame_transition()->level == 4); /* G ran just once */
    if (movie_calls == 2)
        assert(em_frame_input()->pressed == EM_PAD_START);
    return movie_calls < 3;
}

static void reset_frame(void)
{
    task_calls = audio_calls = movie_calls = presents = frame_draws = 0;
    arm_movie = expected_frame = event_pending = 0;
    em_frame_init(NULL, NULL);
}

static void test_frame_phases(void)
{
    reset_frame();
    em_task_register(0, frame_task);
    assert(em_frame_step());
    assert(task_calls == 1 && audio_calls == 1 && presents == 1);
    assert(em_frame_transition()->level == 4); /* same-frame arm tick */
    assert(frame_draws == 1 && draws[0].add == 1);
    assert(!draws[0].before_text);
    assert(fabsf(draws[0].level - 4.0f / 255.0f) < 0.000001f);
    assert(em_frame_counter() == 1 && em_frame_parity() == 1);

    /* Re-init clears all fade/movie/quit state, not just input/count. */
    em_frame_request_quit();
    reset_frame();
    assert(em_frame_transition()->level == 0 && !em_frame_fade_active());
    assert(em_frame_counter() == 0 && em_frame_parity() == 0);
    em_frame_screen_fade_start(1, 4);
    assert(em_frame_step());
    assert(frame_draws == 2 && draws[0].h == 64 && draws[1].h == 64);
    assert(draws[0].before_text && draws[1].before_text);
    assert(draws[0].y == 0 && draws[1].y == 384);
    em_frame_screen_fade_gate(2, 1);
    em_frame_step();
    assert(frame_draws == 0 && em_frame_screen_fade()->level == 8);

    /* A movie spans three presentation steps but just one original
     * engine iteration; input still samples every presentation step. */
    reset_frame();
    arm_movie = 1;
    em_frame_set_movie_pump(movie_pump, &movie_calls);
    em_task_register(0, frame_task);
    em_frame_step();
    assert(movie_calls == 1 && presents == 1 && frame_draws == 0);
    event = (EmEvent){.type = EM_EVENT_KEY_DOWN, .key = EM_KEY_RETURN};
    event_pending = 1;
    em_frame_step();
    assert(movie_calls == 2 && presents == 2 && frame_draws == 0);
    em_frame_step();
    assert(movie_calls == 3 && presents == 3);
    assert(task_calls == 1 && audio_calls == 1);
    assert(em_frame_counter() == 1 && em_frame_parity() == 1);
    assert(em_frame_transition()->level == 8); /* completion tick O */
    assert(frame_draws == 1 && draws[0].add == 1);
    expected_frame = 1;
    em_frame_step();
    assert(task_calls == 2 && audio_calls == 2 && movie_calls == 3);
    assert(em_frame_transition()->level == 12);
    assert(em_frame_counter() == 2 && em_frame_parity() == 0);
    em_frame_request_quit();
    assert(!em_frame_step() && presents == 4);
}

int main(void)
{
    test_transition();
    test_screen();
    test_frame_phases();
    puts("fade_test: PASS (transition, letterbox, phase order, blocking movie)");
    return 0;
}
