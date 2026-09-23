/* Host services for em_startup. Original control flow lives in em_startup.c;
 * this file adapts disc exports, native movie playback, and native storage.
 * It deliberately keeps startup assets out of the gameplay fixture loader.
 */
#include "game/em_frontend.h"
#include "game/em_startup.h"
#include "game/em_startup_audio.h"
#include "game/em_frame.h"
#include "game/em_game.h"
#include "game/em_hud.h"
#include "game/em_task.h"
#include "game/em_bgm.h"
#include "game/em_scene_bindings.h"
#include "game/em_sfx.h"
#include "em_input.h"
#include "em_movie.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { SCREEN_W = 512, SCREEN_H = 448, SCREEN_BYTES = SCREEN_W * SCREEN_H * 4 };
static struct {
    EmStartup flow;
    unsigned char *screens[6];
    int texture_screen, previous_screen, failed;
    EmMovie *movie;
    uint32_t movie_serial;
    int movie_skip, new_game_movie;
    int movie_selector; /* D_00275C78 as 001AD360 step 1 stored it; -1 = none */
    int installed;      /* em_frontend_install ran (the movie pump is registered) */
    uint32_t attract_serial;
    unsigned movie_frames;
    uint64_t movie_picture;
    double movie_pts;
    unsigned movie_width, movie_height;
    int have_movie_frame;
    unsigned menu_ticks;
    unsigned seen_screens;
    unsigned captured;
    const char *capture_dir;
    const char *test;
    char error[512];
} f;

static uint32_t le32(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void fail(const char *message)
{
    if (f.failed) return;
    f.failed = 1;
    snprintf(f.error, sizeof f.error, "%s", message);
    fprintf(stderr, "startup: %s\n", f.error);
    em_frame_request_quit();
}

static int load_screen(unsigned index, const char *name)
{
    char path[256], error[320];
    snprintf(path, sizeof path, "assets/startup/%s.emui", name);
    FILE *in = fopen(path, "rb");
    unsigned char header[36];
    if (!in) {
        snprintf(error, sizeof error, "missing %s; run the decomp's tools/export_startup.py", path);
        fail(error);
        return 0;
    }
    int valid = fread(header, 1, sizeof header, in) == sizeof header &&
        !memcmp(header, "EMUI", 4) && le32(header + 4) == 1 &&
        le32(header + 8) == SCREEN_W && le32(header + 12) == SCREEN_H &&
        le32(header + 16) == 1;
    if (valid) {
        static const unsigned char sprite[16] =
            { 0,0, 0,0, 0,2, 192,1, 0,0, 0,0, 0,2, 192,1 };
        valid = !memcmp(header + 20, sprite, sizeof sprite);
    }
    unsigned char *pixels = valid ? malloc(SCREEN_BYTES) : NULL;
    valid = pixels && fread(pixels, 1, SCREEN_BYTES, in) == SCREEN_BYTES && fgetc(in) == EOF;
    fclose(in);
    if (!valid) {
        free(pixels);
        snprintf(error, sizeof error, "invalid composed screen %s", path);
        fail(error);
        return 0;
    }
    f.screens[index] = pixels;
    return 1;
}

static void capture(unsigned bit, const char *name)
{
    if (!f.capture_dir || (f.captured & bit)) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.bmp", f.capture_dir, name);
    em_gfx_request_capture(em_frame_gfx(), path);
    f.captured |= bit;
}

static void draw_texture(unsigned w, unsigned h)
{
    const float white[4] = {1, 1, 1, 1};
    EmGfx *gfx = em_frame_gfx();
    em_gfx_overlay_canvas(gfx, SCREEN_W, SCREEN_H);
    em_gfx_overlay_sprite(gfx, 0, 0, SCREEN_W, SCREEN_H, 0, 0, w, h, white);
    em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
}

static void begin_movie(uint32_t serial, int new_game)
{
    em_startup_audio_stop();
    em_sfx_stop_all();
    em_bgm_stop(0);
    f.movie = em_movie_open("assets/startup/intro.mov");
    if (!f.movie) {
        fail("could not allocate the intro movie player");
        return;
    }
    f.movie_serial = serial;
    f.movie_skip = 0;
    f.new_game_movie = new_game;
    f.movie_frames = 0;
    f.movie_picture = 0;
    f.movie_pts = 0;
    f.have_movie_frame = 0;
    f.texture_screen = -1;
    em_frame_set_movie_active(1);
    printf("startup: movie 0 (E900.PSS)%s\n", new_game ? " for NEW GAME" : "");
}

static int movie_pump(void *unused)
{
    (void)unused;
    if (!f.movie) return 0;
    if (em_startup_audio_tick() != 0) fail("startup audio event queue overflow");
    EmMovieFrame frame;
    int updated = em_movie_update(f.movie, &frame);
    if (updated < 0) {
        fail(em_movie_error(f.movie));
    } else if (updated) {
        if (frame.stride != frame.width * 4 ||
            !em_gfx_overlay_texture_set(em_frame_gfx(), EM_GFX_OVERLAY_TEX_UI,
                                       frame.pixels, frame.width, frame.height)) {
            fail("could not upload the decoded movie frame");
        }
        f.movie_width = frame.width;
        f.movie_height = frame.height;
        f.have_movie_frame = 1;
        f.movie_pts = frame.pts_seconds;
        f.movie_picture = frame.picture_index;
        ++f.movie_frames;
    }
    if (f.have_movie_frame) {
        draw_texture(f.movie_width, f.movie_height);
        if (f.movie_pts >= 2.0) capture(8, "intro");
    }
    /* Original stream +8 is a completed-picture index, not game ticks.
     * PTS-derived indices preserve the gate even if presentation drops frames. */
    uint16_t held = em_frame_input()->held;
    if (f.test && (strcmp(f.test, "skip") == 0 || (strcmp(f.test, "newgame") == 0 || strcmp(f.test, "newgame-control") == 0)) &&
        f.movie_pts >= 2.0)
        held |= EM_PAD_START;
    int skip_ready = f.have_movie_frame && f.movie_picture >= 11;
    if (f.new_game_movie) {
        /* 002036E0 skips on held & (spad 0x70003B90 ? 0x800 : 0x8F0). The
         * game task 001ACEC0 writes 0x70003B90 = 2 on every tick before
         * 001AD360 requests this movie, so only START skips; the 0x8F0 arm
         * is unreachable from the game task and is refused. */
        if (em_scene_state()->spad3B90 == 0) fail("game-task movie with spad 3B90 == 0");
        if (skip_ready && (held & EM_PAD_START)) f.movie_skip = 1;
    } else {
        em_startup_movie_input(&f.flow, held, skip_ready);
    }
    if (!f.failed && !f.movie_skip && !em_movie_finished(f.movie)) return 1;
    printf("startup: movie %s, %u displayed frames, last PTS %.3f\n",
           f.failed ? "failed" : f.movie_skip ? "skipped" : "completed",
           f.movie_frames, f.movie_pts);
    em_movie_close(f.movie);
    f.movie = NULL;
    f.texture_screen = -1;
    if (!f.new_game_movie) {
        em_startup_complete(&f.flow, f.movie_serial, f.failed ? -1 : 1);
    }
    /* A game-task movie (001AD360 step 1) simply returns: the blocking call
     * 00203350 ends and the main loop resumes; the task chain goes on to
     * 001AD360 step 2 (S12a). 001AD250's 001AEDB0 after step 5 and the
     * 001ADF50 load follow in the chain (em_scene_bindings.c); the loading
     * veil's particles (0021B1B0/0021B500) are not drawn, so the screen
     * stays black there (WP-17 residue). */
    return 0;
}

static int native_storage_ready(void)
{
    /* Native storage replaces the removable PS2 memory-card device.
     * No fabricated PS2 card record is written into the game state. */
    if (mkdir("data", 0700) && errno != EEXIST) return 0;
    if (mkdir("data/save", 0700) && errno != EEXIST) return 0;
    struct stat st;
    return stat("data/save", &st) == 0 && S_ISDIR(st.st_mode);
}

/* anim_frame_top_a states 0/1/3/4 return 2 while D_00810E70 & 0x9F0
 * (EM_STARTUP_ATTRACT_EXIT); 001AC070 state 3 then tears down and
 * re-enters the title. The demo's other exits (0xE10 demo frames,
 * 001ACE70, death/pause at fade 2) need the demo itself. */
static void attract_abort(void)
{
    if (!f.attract_serial ||
        !(em_frame_input()->held & EM_STARTUP_ATTRACT_EXIT)) return;
    uint32_t serial = f.attract_serial;
    f.attract_serial = 0;
    em_startup_complete(&f.flow, serial, 2);
}

static void notify(void *unused, const EmStartupEvent *event)
{
    (void)unused;
    int ready = 1;
    switch (event->kind) {
    case EM_STARTUP_BOOT_RESOURCES: {
        const char *names[] = {"logo_0", "logo_1", "logo_2", "title_0", "title_1", "title_2"};
        for (unsigned i = 0; i < 6 && ready; ++i) ready = load_screen(i, names[i]);
        if (ready && em_startup_audio_init("assets/startup_audio/startup_audio.txt") != 0) {
            fail("could not load the original startup sound events");
            ready = 0;
        }
        break;
    }
    case EM_STARTUP_SCREEN_MODULE:
    case EM_STARTUP_RESOURCE_BANK:
    case EM_STARTUP_TITLE_RESOURCES:
        /* The synchronous native export/preload has already resolved these
         * module/bank resources. The FSM still observes its original wait. */
        ready = f.screens[0] != NULL && f.screens[5] != NULL;
        break;
    case EM_STARTUP_CARD_CHECK:
        ready = native_storage_ready();
        if (!ready) fail("native save directory data/save is unavailable");
        break;
    case EM_STARTUP_MOVIE:
        begin_movie(event->serial, 0);
        return;
    case EM_STARTUP_MOVIE_SKIP:
        f.movie_skip = 1;
        return;
    case EM_STARTUP_FADE_FULL:
        em_frame_fade_full(EM_FADE_BLACK);
        return;
    case EM_STARTUP_FADE_IN:
        em_frame_fade_start(-1, event->value);
        return;
    case EM_STARTUP_FADE_OUT:
        em_frame_fade_start(1, event->value);
        return;
    case EM_STARTUP_AUDIO_STOP:
        em_startup_audio_stop();
        em_sfx_stop_all();
        em_bgm_stop(0);
        return;
    case EM_STARTUP_AUDIO_LEVEL:
        /* PS2 master hardware gain is adapted by the native audio service. */
        return;
    case EM_STARTUP_EFFECT_LEVEL:
        /* Separate PS2 effect-return control (00119828); not master gain. */
        return;
    case EM_STARTUP_CUE:
        if (em_startup_audio_play((unsigned)event->id) != 0)
            fail("missing or unavailable startup sound cue");
        return;
    case EM_STARTUP_NEW_GAME:
        /* 001AC070 state 4: 001AB790(001ACEC0) replaces slot 0 with the game
         * task; id 1 (a loaded game, D_00275BE0 = 1) needs the unported
         * load-game service. The intro movie is requested later by the
         * chain itself, at 001AD360 step 1 (em_frontend_movie_request). */
        if (event->id != 0) {
            fail("NEW_GAME handoff for a loaded game (load-game service not ported)");
            return;
        }
        em_game_install_new();
        return;
    case EM_STARTUP_ATTRACT:
        /* anim_frame_top_a (the attract demo) is not ported (WP-17
         * residue): the screen stays black and only its button exit is
         * served, see attract_abort. */
        f.attract_serial = event->serial;
        attract_abort();
        return;
    case EM_STARTUP_LOAD_GAME:
    case EM_STARTUP_OPTIONS:
        fprintf(stderr, "startup: service %d awaits native translation\n", event->kind);
        /* Preserve pending state rather than manufacturing an outcome. */
        return;
    }
    em_startup_complete(&f.flow, event->serial, ready ? 1 : -1);
}

static void startup_task(void)
{
    if (em_startup_audio_tick() != 0) fail("startup audio event queue overflow");
    const EmFrameInput *pad = em_frame_input();
    EmStartupInput input = {pad->held, pad->pressed,
                            em_frame_transition()->substate, 0};
    attract_abort();
    int expected_cursor = -1;
    if (f.test && em_startup_view(&f.flow, &input).interactive) {
        /* End-to-end fixture: exercise the rendered menu through the same
         * state-machine input boundary as the frame's unpacked pad. */
        input.held = input.pressed = 0;
        switch (f.menu_ticks) {
        case 2: input.held = input.pressed = EM_PAD_DOWN; expected_cursor = 1; break;
        case 4: input.held = input.pressed = EM_PAD_DOWN; expected_cursor = 2; break;
        case 6: input.held = input.pressed = EM_PAD_DOWN; expected_cursor = 2; break;
        case 8: input.held = input.pressed = EM_PAD_UP; expected_cursor = 1; break;
        case 10: input.held = input.pressed = EM_PAD_UP; expected_cursor = 0; break;
        case 12: input.held = input.pressed = EM_PAD_UP; expected_cursor = 0; break;
        case 14:
            if ((strcmp(f.test, "newgame") == 0 || strcmp(f.test, "newgame-control") == 0))
                input.held = input.pressed = EM_PAD_CROSS;
            break;
        }
    }
    em_startup_tick(&f.flow, &input);
    EmStartupView view = em_startup_view(&f.flow, &input);
    f.seen_screens |= 1u << view.screen;
    if (expected_cursor >= 0 && view.cursor != (unsigned)expected_cursor)
        fail("startup test: menu navigation/clamp mismatch");
    if ((int)view.screen != f.previous_screen) {
        printf("startup: screen %d, engine frame %u\n", view.screen, em_frame_counter());
        fflush(stdout);
        f.previous_screen = view.screen;
    }
    EmGfx *gfx = em_frame_gfx();
    const float black[4] = {0, 0, 0, 1};
    em_gfx_overlay_rect(gfx, 0, 0, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H, black);
    int screen = -1;
    if (view.screen >= EM_STARTUP_SCREEN_LOGO_A && view.screen <= EM_STARTUP_SCREEN_LOGO_C)
        screen = view.screen - EM_STARTUP_SCREEN_LOGO_A;
    else if (view.screen == EM_STARTUP_SCREEN_TITLE)
        screen = 3 + (int)view.cursor;
    if (screen >= 0 && f.screens[screen]) {
        if (f.texture_screen != screen) {
            if (!em_gfx_overlay_texture_set(gfx, EM_GFX_OVERLAY_TEX_UI,
                                           f.screens[screen], SCREEN_W, SCREEN_H))
                fail("could not upload the startup screen");
            f.texture_screen = screen;
        }
        draw_texture(SCREEN_W, SCREEN_H);
        if (em_frame_transition()->substate == 0) {
            const char *names[] = {"warning", "sony", "deep_space", "title_0", "title_1", "title_2"};
            capture(1u << (screen < 3 ? screen : screen + 1), names[screen]);
        }
    }
    if (view.interactive && ++f.menu_ticks == 30 && f.test) {
        unsigned required = (1u << EM_STARTUP_SCREEN_LOGO_A) |
            (1u << EM_STARTUP_SCREEN_LOGO_B) | (1u << EM_STARTUP_SCREEN_LOGO_C) |
            (1u << EM_STARTUP_SCREEN_MOVIE) | (1u << EM_STARTUP_SCREEN_TITLE);
        if ((f.seen_screens & required) != required || !f.movie_frames)
            fail("startup test: did not present every required startup stage");
        if (!f.failed) printf("startup test: PASS (logos, movie, title navigation/clamps)\n");
        em_frame_request_quit();
    }
}

int em_frontend_movie_select(uint8_t selector)
{
    f.movie_selector = selector;
    return 0;
}

int em_frontend_movie_request(uint8_t value)
{
    if (!f.installed) {
        /* EM_SKIP_STARTUP: no frontend, so no movie pump for step M. */
        fprintf(stderr, "startup: movie request without the native frontend (EM_SKIP_STARTUP)\n");
        em_frame_request_quit();
        return -1;
    }
    if (value != 1 || f.movie_selector != 0 || f.movie) {
        char error[128];
        snprintf(error, sizeof error, "movie request D_00821058=%u with selector %d is not exported",
                 (unsigned)value, f.movie_selector);
        fail(error);
        return -1;
    }
    begin_movie(0, 1);
    return f.failed ? -1 : 0;
}

void em_frontend_install(void)
{
    memset(&f, 0, sizeof f);
    f.texture_screen = f.previous_screen = -1;
    f.movie_selector = -1;
    f.installed = 1;
    f.capture_dir = getenv("EM_STARTUP_CAPTURE_DIR");
    f.test = getenv("EM_STARTUP_TEST");
    em_startup_init(&f.flow, notify, NULL);
    em_frame_set_movie_pump(movie_pump, NULL);
    em_task_register(0, startup_task);
}

void em_frontend_shutdown(void)
{
    em_startup_audio_stop();
    em_frame_set_movie_pump(NULL, NULL);
    if (f.movie) em_movie_close(f.movie);
    f.movie = NULL;
    for (unsigned i = 0; i < 6; ++i) {
        free(f.screens[i]);
        f.screens[i] = NULL;
    }
}

int em_frontend_failed(void) { return f.failed; }
