#include "em_startup.h"
#include "../em_input.h"

#include <string.h>

/* Retain the original task/substate boundaries: they determine when input,
 * fades and post-decrement timers can affect the next presented frame.
 * Resource operations are native asynchronous boundaries, not fake delays. */
enum { FLOW_BOOT, FLOW_TITLE };

static void emit(EmStartup *s, EmStartupEventKind kind, int id, int value)
{
    EmStartupEvent event = { kind, 0, id, value };
    if (s->notify) s->notify(s->user, &event);
}

static void request(EmStartup *s, EmStartupEventKind kind, int id)
{
    EmStartupEvent event;
    ++s->next_serial;
    if (!s->next_serial) ++s->next_serial;
    s->pending_serial = s->next_serial;
    s->pending_kind = kind;
    s->pending_result = 0;
    event.kind = kind;
    event.serial = s->pending_serial;
    event.id = id;
    event.value = 0;
    if (s->notify) s->notify(s->user, &event);
}

static int poll(EmStartup *s)
{
    int result = s->pending_result;
    if (!s->pending_serial || result <= 0) return 0;
    s->pending_serial = 0;
    s->pending_result = 0;
    return result;
}

static void fade(EmStartup *s, EmStartupEventKind kind, int speed)
{
    emit(s, kind, 0, speed);
}

static void levels(EmStartup *s, int level)
{
    emit(s, EM_STARTUP_AUDIO_LEVEL, 0, level);
    emit(s, EM_STARTUP_AUDIO_LEVEL, 1, level);
}

static void cue(EmStartup *s, int id)
{
    emit(s, EM_STARTUP_CUE, id, 0x1000);
}

static void advance_major(EmStartup *s)
{
    ++s->major;
    s->sub = 0;
    s->aux = 0;
}

static void logo_countdown(EmStartup *s, EmStartupScreen screen)
{
    uint16_t old = s->timer;
    s->screen = screen;
    s->timer = (uint16_t)(old - 1);
    if (old == 0) {
        fade(s, EM_STARTUP_FADE_OUT, 4);
        ++s->sub;
    }
}

static void boot_tick(EmStartup *s, const EmStartupInput *in)
{
    switch (s->major) {
    case 0:                         /* 001AB7E0 initial banks */
        if (s->sub == 0) {
            s->sub = 1;
            request(s, EM_STARTUP_BOOT_RESOURCES, 0);
        } else if (poll(s)) {
            advance_major(s);
        }
        break;
    case 1:
        advance_major(s);
        /* The original deliberately falls through to first-screen init. */
        /* fall through */
    case 2:                         /* 001AB9D0 */
        switch (s->sub) {
        case 0:
            fade(s, EM_STARTUP_FADE_FULL, 0);
            s->sub = 1;
            break;
        case 1:
            s->sub = 2;
            request(s, EM_STARTUP_SCREEN_MODULE, 0x28);
            break;
        case 2:
            if (poll(s)) {
                fade(s, EM_STARTUP_FADE_IN, 4);
                s->sub = 3;
                s->screen = EM_STARTUP_SCREEN_CARD;
            }
            break;
        case 3:
            if (s->aux == 0) {
                s->aux = 1;
                request(s, EM_STARTUP_CARD_CHECK, 0);
            }
            if (s->aux == 1 && poll(s)) {
                fade(s, EM_STARTUP_FADE_OUT, 8);
                s->aux = 2;
            } else if (s->aux == 2 && in->fade_state == 2) {
                s->timer = 300;
                s->aux = 0;
                s->sub = 4;
                s->screen = EM_STARTUP_SCREEN_NONE;
                fade(s, EM_STARTUP_FADE_IN, 4);
            }
            break;
        case 4:
            logo_countdown(s, EM_STARTUP_SCREEN_LOGO_A);
            break;
        case 5:
            s->screen = EM_STARTUP_SCREEN_LOGO_A;
            if (in->fade_state == 2) advance_major(s);
            break;
        }
        break;
    case 3:                         /* 001ABC60 */
        switch (s->sub) {
        case 0:
            s->timer = 300;
            s->screen = EM_STARTUP_SCREEN_NONE;
            s->sub = 1;
            request(s, EM_STARTUP_SCREEN_MODULE, 0x29);
            break;
        case 1:
            if (poll(s)) {
                fade(s, EM_STARTUP_FADE_IN, 4);
                s->sub = 2;
            }
            break;
        case 2:
            logo_countdown(s, EM_STARTUP_SCREEN_LOGO_B);
            break;
        case 3:
            s->screen = EM_STARTUP_SCREEN_LOGO_B;
            if (in->fade_state == 2) {
                s->sub = 4;
                request(s, EM_STARTUP_RESOURCE_BANK, 0x1B);
                if (poll(s)) advance_major(s);
            }
            break;
        case 4:                     /* native async equivalent of blocking load */
            if (poll(s)) advance_major(s);
            break;
        }
        break;
    case 4:                         /* 001ABE10: module41 stays resident */
        switch (s->sub) {
        case 0:
            s->timer = 300;
            s->screen = EM_STARTUP_SCREEN_NONE;
            s->sub = 1;
            fade(s, EM_STARTUP_FADE_IN, 4);
            break;
        case 1:
            logo_countdown(s, EM_STARTUP_SCREEN_LOGO_C);
            break;
        case 2:
            s->screen = EM_STARTUP_SCREEN_LOGO_C;
            if (in->fade_state == 2) {
                s->sub = 3;
                request(s, EM_STARTUP_RESOURCE_BANK, 0x1C);
                if (poll(s)) advance_major(s);
            }
            break;
        case 3:
            if (poll(s)) advance_major(s);
            break;
        }
        break;
    case 5:
        levels(s, 0x3FFF);
        ++s->major;
        break;
    case 6:                         /* 001AB790 replaces slot0, clearing state */
        s->flow = FLOW_TITLE;
        s->major = s->sub = s->aux = 0;
        s->screen = EM_STARTUP_SCREEN_NONE;
        break;
    }
}

static void movie_tick(EmStartup *s, const EmStartupInput *in)
{
    switch (s->sub) {
    case 0:                         /* 001AC3B0 audio/stream teardown */
        emit(s, EM_STARTUP_AUDIO_STOP, 0, 0);
        s->sub = 1;
        break;
    case 1:
        s->sub = 2;
        s->movie_skip_sent = 0;
        s->screen = EM_STARTUP_SCREEN_MOVIE;
        /* Host service must wait for preceding audio/disc work to drain,
         * equivalent to D_00282157==0, then run E900 and report completion. */
        request(s, EM_STARTUP_MOVIE, 0);
        break;
    case 2:
        if (poll(s)) {
            s->screen = EM_STARTUP_SCREEN_NONE;
            s->sub = 3;
        } else {
            em_startup_movie_input(s, in->held, in->movie_skip_ready);
        }
        break;
    case 3:
        s->major = 2;
        s->sub = s->aux = 0;
        break;
    }
}

static void title_draw(EmStartup *s)
{
    s->screen = EM_STARTUP_SCREEN_TITLE;
    if (s->aux == 0) {              /* 001AC7F0 first draw, not first idle frame */
        s->aux = 1;
        cue(s, 0x5DC);
    }
}

static void menu_tick(EmStartup *s, const EmStartupInput *in)
{
    switch (s->sub) {
    case 0:
        emit(s, EM_STARTUP_EFFECT_LEVEL, 0, 0x5998);
        emit(s, EM_STARTUP_EFFECT_LEVEL, 1, 0x5998);
        s->timer = 1200;
        s->cursor = 0;              /* cold-title context; death handled elsewhere */
        s->sub = 1;
        s->aux = 0;
        request(s, EM_STARTUP_SCREEN_MODULE, 1);
        break;
    case 1:
        if (s->aux == 0 && poll(s)) {
            s->aux = 1;
            request(s, EM_STARTUP_TITLE_RESOURCES, 0);
        }
        if (s->aux == 1 && poll(s)) {
            fade(s, EM_STARTUP_FADE_IN, 4);
            s->sub = 2;
            s->aux = 0;
        }
        break;
    case 2:
        title_draw(s);
        if (in->fade_state != 0) break;
        if (in->held == 0) {
            uint16_t old = s->timer;
            s->timer = (uint16_t)(old - 1);
            if (old == 0) {
                fade(s, EM_STARTUP_FADE_OUT, 4);
                s->sub = 4;
            }
        } else {
            if (in->pressed & (EM_PAD_START | EM_PAD_CROSS)) {
                fade(s, EM_STARTUP_FADE_OUT, 4);
                s->sub = 3;
                cue(s, 0x5DD + (int)s->cursor);
            } else if (in->pressed & EM_PAD_DOWN) {
                if (s->cursor < 2) {
                    ++s->cursor;
                    cue(s, 5);
                }
            } else if (in->pressed & EM_PAD_UP) {
                if (s->cursor > 0) {
                    --s->cursor;
                    cue(s, 5);
                }
            }
            s->timer = 1200;
        }
        break;
    case 3:
        title_draw(s);
        if (in->fade_state == 2) {
            s->major = 4 + s->cursor;
            s->sub = s->aux = 0;
        }
        break;
    case 4:
        title_draw(s);
        if (in->fade_state == 2) {
            if (s->cycle_mode == 1) {
                s->major = 3;
                s->cycle_mode = 3;
            } else {
                s->major = 1;
                s->cycle_mode = 1;
            }
            s->sub = s->aux = 0;
        } else if (in->held != 0) {
            fade(s, EM_STARTUP_FADE_IN, 4);
            s->sub = 2;
            s->aux = 0;
            s->timer = 1200;
        }
        break;
    }
}

static void title_tick(EmStartup *s, const EmStartupInput *in)
{
    int result;
    switch (s->major) {
    case 0:                         /* 001AC070 init, from-death flag is zero */
        fade(s, EM_STARTUP_FADE_FULL, 0);
        s->attract_cycle = 0;
        s->cycle_mode = 1;
        s->major = 1;
        break;
    case 1:
        movie_tick(s, in);
        break;
    case 2:
        menu_tick(s, in);
        break;
    case 3:                         /* 001AC070 state 3: anim_frame_top_a */
        if (s->sub == 0) {
            s->sub = 1;
            s->screen = EM_STARTUP_SCREEN_EXTERNAL;
            request(s, EM_STARTUP_ATTRACT, (int)s->attract_cycle);
        }
        /* anim_frame_top_a can return 2 on its first call (state 0 input
         * gate), so a synchronous completion is honoured in this tick. */
        if (poll(s)) {
            s->attract_cycle = (s->attract_cycle + 1) % 3;
            emit(s, EM_STARTUP_AUDIO_STOP, 0, 0);
            fade(s, EM_STARTUP_FADE_FULL, 0);
            s->major = 2;
            s->sub = s->aux = 0;
            s->screen = EM_STARTUP_SCREEN_NONE;
        }
        break;
    case 4:
        s->handed_off = 1;
        s->screen = EM_STARTUP_SCREEN_EXTERNAL;
        emit(s, EM_STARTUP_NEW_GAME, s->aux == 2, 0);
        break;
    case 5:
        if (s->sub == 0) {
            s->sub = 1;
            s->screen = EM_STARTUP_SCREEN_EXTERNAL;
            request(s, EM_STARTUP_LOAD_GAME, 0);
        } else if ((result = poll(s)) != 0) {
            s->major = result == 2 ? 4 : 2;
            s->aux = result == 2 ? 2 : 0;
            s->sub = 0;
            s->screen = EM_STARTUP_SCREEN_NONE;
        }
        break;
    case 6:
        if (s->sub == 0) {
            s->sub = 1;
            s->screen = EM_STARTUP_SCREEN_EXTERNAL;
            request(s, EM_STARTUP_OPTIONS, 0);
        } else if (poll(s)) {
            s->major = 2;
            s->sub = s->aux = 0;
            s->screen = EM_STARTUP_SCREEN_NONE;
        }
        break;
    }
}

void em_startup_init(EmStartup *s, EmStartupNotify notify, void *user)
{
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->notify = notify;
    s->user = user;
}

int em_startup_complete(EmStartup *s, uint32_t serial, int result)
{
    if (!s || serial == 0 || serial != s->pending_serial) return 0;
    s->pending_result = result;
    s->failed = result < 0;
    return 1;
}

void em_startup_tick(EmStartup *s, const EmStartupInput *in)
{
    if (!s || !in || s->failed || s->handed_off) return;
    if (s->flow == FLOW_BOOT) boot_tick(s, in);
    else title_tick(s, in);
}

void em_startup_movie_input(EmStartup *s, uint16_t held, int movie_skip_ready)
{
    if (!s || s->failed || s->handed_off || s->movie_skip_sent ||
        s->flow != FLOW_TITLE || s->major != 1 || s->sub != 2 ||
        !s->pending_serial || s->pending_kind != EM_STARTUP_MOVIE ||
        s->pending_result != 0 || !movie_skip_ready) return;
    if (held & (EM_PAD_START | EM_PAD_TRIANGLE | EM_PAD_CIRCLE |
                EM_PAD_CROSS | EM_PAD_SQUARE)) {
        s->movie_skip_sent = 1;
        emit(s, EM_STARTUP_MOVIE_SKIP, 0, 0);
    }
}

EmStartupView em_startup_view(const EmStartup *s, const EmStartupInput *in)
{
    EmStartupView view;
    memset(&view, 0, sizeof view);
    if (!s) return view;
    view.screen = s->screen;
    view.cursor = s->cursor;
    view.timer = s->timer;
    view.interactive = in && !s->failed && !s->handed_off &&
                       s->flow == FLOW_TITLE && s->major == 2 && s->sub == 2 &&
                       in->fade_state == 0;
    view.handed_off = s->handed_off;
    view.failed = s->failed;
    view.pending_serial = s->pending_serial;
    view.pending_kind = s->pending_kind;
    return view;
}
