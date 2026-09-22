/* Behavioral fixtures from original startup call order and input boundaries.
 * Synthetic host services explicitly complete resources; no game assets, OS,
 * decoded audio or renderer are required. Build with em_startup.c + em_fade.c. */
#include "game/em_startup.h"
#include "game/em_fade.h"
#include "em_input.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    EmStartup startup;
    EmTransitionFade fade;
    EmStartupInput input;
    EmStartupEvent events[256];
    unsigned event_count;
    unsigned screen_ticks[8];
    unsigned logo_timer_expiries;
    int auto_resources;
} Fixture;

static void notify(void *user, const EmStartupEvent *event)
{
    Fixture *f = user;
    assert(f->event_count < sizeof f->events / sizeof f->events[0]);
    f->events[f->event_count++] = *event;
    switch (event->kind) {
    case EM_STARTUP_FADE_FULL:
        em_transition_fade_full(&f->fade, 0);
        break;
    case EM_STARTUP_FADE_IN:
        em_transition_fade_in(&f->fade, (int16_t)event->value, 0);
        break;
    case EM_STARTUP_FADE_OUT:
        em_transition_fade_out(&f->fade, (int16_t)event->value, 0);
        if (f->startup.screen >= EM_STARTUP_SCREEN_LOGO_A &&
            f->startup.screen <= EM_STARTUP_SCREEN_LOGO_C) {
            /* Callback occurs in tick301, after exactly300 preceding draws.
             * The 16-bit old-zero timer has wrapped, as in the original. */
            assert(f->screen_ticks[f->startup.screen] == 300);
            assert(f->startup.timer == 65535);
            ++f->logo_timer_expiries;
        }
        break;
    case EM_STARTUP_BOOT_RESOURCES:
    case EM_STARTUP_SCREEN_MODULE:
    case EM_STARTUP_RESOURCE_BANK:
    case EM_STARTUP_TITLE_RESOURCES:
        if (f->auto_resources)
            assert(em_startup_complete(&f->startup, event->serial, 1));
        break;
    default:
        break;
    }
}

static void init(Fixture *f, int auto_resources)
{
    memset(f, 0, sizeof *f);
    f->auto_resources = auto_resources;
    em_transition_fade_init(&f->fade);
    em_startup_init(&f->startup, notify, f);
}

static void tick(Fixture *f, uint16_t held, uint16_t pressed)
{
    f->input.held = held;
    f->input.pressed = pressed;
    f->input.fade_state = f->fade.substate;
    em_startup_tick(&f->startup, &f->input);
    ++f->screen_ticks[f->startup.screen];
    em_transition_fade_tick(&f->fade);
    f->input.fade_state = f->fade.substate;
}

static unsigned count_event(const Fixture *f, EmStartupEventKind kind, int id)
{
    unsigned count = 0;
    for (unsigned i = 0; i < f->event_count; ++i)
        if (f->events[i].kind == kind && f->events[i].id == id) ++count;
    return count;
}

static uint32_t pending(Fixture *f, EmStartupEventKind kind)
{
    for (int i = 0; i < 4000; ++i) {
        if (f->startup.pending_serial && f->startup.pending_kind == kind &&
            f->startup.pending_result == 0) return f->startup.pending_serial;
        tick(f, 0, 0);
    }
    assert(!"startup failed to reach requested service");
    return 0;
}

static void interactive(Fixture *f)
{
    for (int i = 0; i < 200; ++i) {
        if (em_startup_view(&f->startup, &f->input).interactive) return;
        tick(f, 0, 0);
    }
    assert(!"title did not become interactive");
}

static void reach_movie(Fixture *f)
{
    uint32_t serial = pending(f, EM_STARTUP_CARD_CHECK);
    assert(f->startup.screen == EM_STARTUP_SCREEN_CARD);
    for (int i = 0; i < 150; ++i) tick(f, EM_PAD_START, EM_PAD_START);
    assert(f->startup.pending_serial == serial); /* no fake card timeout */
    assert(em_startup_complete(&f->startup, serial, 1));
    pending(f, EM_STARTUP_MOVIE);
    assert(f->logo_timer_expiries == 3);
    assert(count_event(f, EM_STARTUP_SCREEN_MODULE, 0x28) == 1);
    assert(count_event(f, EM_STARTUP_SCREEN_MODULE, 0x29) == 1);
    assert(count_event(f, EM_STARTUP_RESOURCE_BANK, 0x1B) == 1);
    assert(count_event(f, EM_STARTUP_RESOURCE_BANK, 0x1C) == 1);
    assert(count_event(f, EM_STARTUP_MOVIE, 0) == 1);
    /* Logo calls themselves never react to pressed input. */
    assert(count_event(f, EM_STARTUP_MOVIE_SKIP, 0) == 0);
}

static void reach_title(Fixture *f)
{
    reach_movie(f);
    assert(em_startup_complete(&f->startup, f->startup.pending_serial, 1));
    interactive(f);
    assert(count_event(f, EM_STARTUP_SCREEN_MODULE, 1) == 1);
    assert(count_event(f, EM_STARTUP_CUE, 0x5DC) == 1);
    assert(f->startup.cursor == 0);
    assert(f->startup.timer == 1200);
}

static void test_missing_resources(void)
{
    Fixture f;
    init(&f, 0);
    tick(&f, 0, 0);
    uint32_t serial = f.startup.pending_serial;
    assert(serial != 0);
    assert(!em_startup_complete(&f.startup, serial + 1, 1));
    for (int i = 0; i < 4000; ++i) tick(&f, EM_PAD_START, EM_PAD_START);
    assert(f.event_count == 1);
    assert(f.startup.pending_serial == serial);
    assert(em_startup_complete(&f.startup, serial, -1));
    tick(&f, 0, 0);
    assert(em_startup_view(&f.startup, &f.input).failed);
    assert(em_startup_complete(&f.startup, serial, 1));
    tick(&f, 0, 0);
    assert(!em_startup_view(&f.startup, &f.input).failed);
    assert(!em_startup_complete(&f.startup, serial, 1)); /* already consumed */
}

static void test_movie_and_title_controls(void)
{
    Fixture f;
    init(&f, 1);
    reach_movie(&f);
    uint32_t serial = f.startup.pending_serial;
    unsigned major = f.startup.major, sub = f.startup.sub;
    EmTransitionFade saved_fade = f.fade;
    em_startup_movie_input(&f.startup, EM_PAD_START, 0);
    em_startup_movie_input(&f.startup, EM_PAD_UP | EM_PAD_R1, 1);
    assert(count_event(&f, EM_STARTUP_MOVIE_SKIP, 0) == 0);
    em_startup_movie_input(&f.startup, EM_PAD_CIRCLE, 1);
    em_startup_movie_input(&f.startup, EM_PAD_START, 1);
    assert(count_event(&f, EM_STARTUP_MOVIE_SKIP, 0) == 1);
    assert(f.startup.pending_serial == serial); /* skip still must drain */
    assert(f.startup.major == major && f.startup.sub == sub);
    assert(memcmp(&f.fade, &saved_fade, sizeof saved_fade) == 0);
    assert(em_startup_complete(&f.startup, serial, 1));
    for (int i = 0; i < 100; ++i) {
        tick(&f, 0, 0);
        if (f.startup.screen == EM_STARTUP_SCREEN_TITLE) break;
    }
    assert(f.fade.substate == 1);
    tick(&f, EM_PAD_START | EM_PAD_DOWN, EM_PAD_START | EM_PAD_DOWN);
    assert(f.startup.cursor == 0);
    assert(count_event(&f, EM_STARTUP_CUE, 0x5DD) == 0); /* fade input gate */
    interactive(&f);
    tick(&f, EM_PAD_UP, EM_PAD_UP);
    assert(f.startup.cursor == 0);
    tick(&f, EM_PAD_DOWN, EM_PAD_DOWN);
    assert(f.startup.cursor == 1);
    tick(&f, EM_PAD_DOWN, 0); /* held alone does not move */
    assert(f.startup.cursor == 1 && f.startup.timer == 1200);
    tick(&f, EM_PAD_DOWN, EM_PAD_DOWN);
    tick(&f, EM_PAD_DOWN, EM_PAD_DOWN);
    assert(f.startup.cursor == 2);
    tick(&f, EM_PAD_UP | EM_PAD_DOWN, EM_PAD_UP | EM_PAD_DOWN);
    assert(f.startup.cursor == 2); /* DOWN priority, clamped */
    tick(&f, EM_PAD_UP, EM_PAD_UP);
    tick(&f, EM_PAD_UP, EM_PAD_UP);
    assert(f.startup.cursor == 0);
    tick(&f, EM_PAD_START | EM_PAD_DOWN, EM_PAD_START | EM_PAD_DOWN);
    assert(f.startup.cursor == 0); /* confirm priority */
    assert(count_event(&f, EM_STARTUP_CUE, 0x5DD) == 1);
    assert(!f.startup.handed_off);
    for (int i = 0; i < 70 && !f.startup.handed_off; ++i) tick(&f, 0, 0);
    assert(f.startup.handed_off);
    assert(count_event(&f, EM_STARTUP_NEW_GAME, 0) == 1);
    tick(&f, EM_PAD_START, EM_PAD_START);
    assert(count_event(&f, EM_STARTUP_NEW_GAME, 0) == 1);
}

static void test_timeout_boundary_and_attract(void)
{
    Fixture f;
    init(&f, 1);
    reach_title(&f);
    for (int i = 0; i < 1200; ++i) tick(&f, 0, 0);
    assert(f.startup.timer == 0 && f.fade.substate == 0);
    tick(&f, 0, 0);
    assert(f.startup.timer == 65535 && f.fade.substate == 3);
    tick(&f, EM_PAD_L1, 0); /* any held button cancels before black */
    assert(f.startup.timer == 1200 && f.fade.substate == 1);
    interactive(&f);
    for (int i = 0; i < 1201; ++i) tick(&f, 0, 0);
    while (f.fade.substate != 2) tick(&f, 0, 0);
    tick(&f, EM_PAD_START, EM_PAD_START); /* black wins over late cancellation */
    uint32_t serial = pending(&f, EM_STARTUP_ATTRACT);
    assert(count_event(&f, EM_STARTUP_ATTRACT, 0) == 1);
    assert(em_startup_complete(&f.startup, serial, 1));
    interactive(&f);
    assert(f.startup.attract_cycle == 1);
    for (int i = 0; i < 1201; ++i) tick(&f, 0, 0);
    pending(&f, EM_STARTUP_MOVIE);
    assert(count_event(&f, EM_STARTUP_MOVIE, 0) == 2); /* alternating idle cycle */
}

static void test_external_menus(void)
{
    Fixture f;
    init(&f, 1);
    reach_title(&f);
    tick(&f, EM_PAD_DOWN, EM_PAD_DOWN);
    tick(&f, EM_PAD_CROSS, EM_PAD_CROSS);
    uint32_t serial = pending(&f, EM_STARTUP_LOAD_GAME);
    assert(count_event(&f, EM_STARTUP_CUE, 0x5DE) == 1);
    assert(em_startup_complete(&f.startup, serial, 1)); /* cancel */
    interactive(&f);
    assert(f.startup.cursor == 0);
    tick(&f, EM_PAD_DOWN, EM_PAD_DOWN);
    tick(&f, EM_PAD_DOWN, EM_PAD_DOWN);
    tick(&f, EM_PAD_START, EM_PAD_START);
    serial = pending(&f, EM_STARTUP_OPTIONS);
    assert(count_event(&f, EM_STARTUP_CUE, 0x5DF) == 1);
    assert(em_startup_complete(&f.startup, serial, 1));
    interactive(&f);
    tick(&f, EM_PAD_DOWN, EM_PAD_DOWN);
    tick(&f, EM_PAD_START, EM_PAD_START);
    serial = pending(&f, EM_STARTUP_LOAD_GAME);
    assert(em_startup_complete(&f.startup, serial, 2)); /* loaded */
    tick(&f, 0, 0);
    tick(&f, 0, 0);
    assert(f.startup.handed_off);
    assert(count_event(&f, EM_STARTUP_NEW_GAME, 1) == 1);
}

int main(void)
{
    test_missing_resources();
    test_movie_and_title_controls();
    test_timeout_boundary_and_attract();
    test_external_menus();
    puts("startup: PASS (resources, logo timing, movie skip, title, idle, handoff)");
    return 0;
}
