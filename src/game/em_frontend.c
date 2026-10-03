/* Host services for em_startup. Original control flow lives in em_startup.c;
 * this file adapts disc exports, native movie playback, and native storage.
 * It deliberately keeps startup assets out of the gameplay fixture loader.
 */
#include "game/em_frontend.h"
#include "game/em_startup.h"
#include "game/em_startup_audio.h"
#include "game/em_frame.h"
#include "game/em_level_smoke_test.h"
#include "game/em_game.h"
#include "game/em_hud.h"
#include "game/em_task.h"
#include "game/em_scene_bindings.h"
#include "game/em_sfx.h"
#include "game/em_bgm.h"
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
    int movie_skip, new_game_movie; /* new_game_movie: a game-task movie (D_00821058 = 1) */
    int movie_selector; /* D_00275C78 as the game stored it (001AD360 step 1, op0F); -1 = none */
    int playing_selector; /* the selector of the movie open (f.movie) */
    int installed;      /* em_frontend_install ran (the movie pump is registered) */
    int new_game_switch; /* EM_NEW_GAME=1: START held in the first game-task movie (cleared when it ends) */
    int reinstalled;     /* the title flow again after a death (001ADF00's 001AB790(001AC070)) */
    int interactive;     /* the last tick's view: the menu takes input */
    int title_audio;     /* the title's sounds are loaded (the boot resources) */
    int switch_boot;     /* EM_NEW_GAME=1 booted without the title */
    unsigned cursor;
    uint32_t attract_serial;
    unsigned movie_frames;
    uint64_t movie_picture;
    double movie_pts;
    unsigned movie_width, movie_height;
    int have_movie_frame;
    unsigned menu_ticks;
    unsigned menu_wait;  /* EM_STARTUP_MENU_WAIT (fixtures only) */
    unsigned seen_screens;
    unsigned captured;
    const char *capture_dir;
    const char *test;
    char error[512];
} f;

/* The EM_STARTUP_TEST fixtures that choose New Game on the title menu:
 * "newgame", "newgame-control" and "newgame-skip" (em_opening_control_test.c)
 * and "newgame-level" (the S13 level smoke, em_level_smoke_test.c). */
static int new_game_test(const char *test)
{
    return strcmp(test, "newgame") == 0 || strcmp(test, "newgame-control") == 0 ||
           strcmp(test, "newgame-level") == 0 || strcmp(test, "newgame-skip") == 0;
}

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

/* The game's movie table: 00203350 -> 002034C0 opens D_00821010[D_00275C78]
 * ({lsn, size}, which the boot's 002032C0 fills by looking up each name of
 * the ELF's D_00264FB0 table on the disc). Selector 0 is \MOVIE\E900.PSS
 * (the intro, the title's attract movie and the New Game's 001AD360 step 1),
 * selector 1 \MOVIE\E001.PSS (the departure movie of Roger's script
 * 0x828A10, op0F; the exit capture's D_00821010[1] is E001.PSS's extent,
 * docs/FIRST_LEVEL_EXIT.md). The port plays the remux of each file
 * (tools/export_movie.py); the other seven selectors are not on the first
 * level's path and are not exported (a request for one fails). */
static const char *const k_movie_paths[] = {
    "assets/startup/intro.mov",   /* 0: \MOVIE\E900.PSS */
    "assets/movies/e001.mov",     /* 1: \MOVIE\E001.PSS */
};

static void begin_movie(uint32_t serial, int new_game, unsigned selector)
{
    em_startup_audio_stop();
    em_sfx_stop_all();
    if (selector >= sizeof k_movie_paths / sizeof k_movie_paths[0]) {
        fail("movie selector without an exported movie");
        return;
    }
    f.movie = em_movie_open(k_movie_paths[selector]);
    if (!f.movie) {
        char error[160];
        snprintf(error, sizeof error, "could not open %s (tools/export_movie.py, docs/STARTUP.md)",
                 k_movie_paths[selector]);
        fail(error);
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
    f.playing_selector = (int)selector;
    printf("startup: movie %u (%s)%s\n", selector, selector ? "E001.PSS" : "E900.PSS",
           new_game ? " for the game task" : "");
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
        if (f.movie_pts >= 2.0 && f.playing_selector == 0) capture(8, "intro");
    }
    /* Original stream +8 is a completed-picture index, not game ticks.
     * PTS-derived indices preserve the gate even if presentation drops frames. */
    uint16_t held = em_frame_input()->held;
    if (f.test && !f.reinstalled && (strcmp(f.test, "skip") == 0 || new_game_test(f.test)) &&
        f.movie_pts >= 2.0)
        held |= EM_PAD_START;
    /* EM_NEW_GAME=1: START is held from the movie's first picture, so the
     * original skip test below takes it at the first picture it accepts. */
    if (f.new_game_switch && f.new_game_movie)
        held |= EM_PAD_START;
    int skip_ready = f.have_movie_frame && f.movie_picture >= 11;
    if (f.new_game_movie) {
        /* 002036E0 skips on held & (spad 0x70003B90 ? 0x800 : 0x8F0). The
         * game task 001ACEC0 writes 0x70003B90 = 2 on every tick before
         * 001AD360 requests this movie, so only START skips; the 0x8F0 arm
         * is unreachable from the game task and is refused. The departure
         * movie (selector 1, op0F of 0x828A10) is requested from the same
         * task's world frame: 0x70003B90 is 2 there too (the exit capture's
         * movie frame, decomp build/c10/exit/exit_01_movie_arrival row 6). */
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
    /* EM_NEW_GAME=1 holds START for its own New Game's intro movie only:
     * any later 001AD360 step 1 request plays and skips by the player's
     * pad alone, as on the title route. */
    if (f.new_game_movie)
        f.new_game_switch = 0;
    /* A game-task movie (001AD360 step 1, or op0F's) simply returns: the
     * blocking call 00203350 ends (D_00821058 = 0) and the main loop
     * resumes; the task chain goes on to 001AD360 step 2 (S12a), or op0F's
     * phase 3 sees D_00821058 = 0 on the next world frame. 001AD250's 001AEDB0 after step 5 and the
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

/* The boot resources the title reads: the composed screens and the title
 * sounds (the boot's 001AB7E0 bank / screen loads, native preloads). */
static int boot_resources(void)
{
    int ready = 1;
    const char *names[] = {"logo_0", "logo_1", "logo_2", "title_0", "title_1", "title_2"};
    for (unsigned i = 0; i < 6 && ready; ++i)
        ready = f.screens[i] != NULL || load_screen(i, names[i]);
    if (ready && f.title_audio) return 1;
    if (ready && f.switch_boot) {
        /* EM_NEW_GAME=1 opened the audio device without the title's sounds,
         * which open it themselves: the title after a death cannot play. */
        fail("EM_NEW_GAME=1: the title after a death needs the boot's title sounds");
        return 0;
    }
    if (ready && em_startup_audio_init("assets/startup_audio/startup_audio.txt") != 0) {
        fail("could not load the original startup sound events");
        ready = 0;
    }
    f.title_audio = ready;
    return ready;
}

static void notify(void *unused, const EmStartupEvent *event)
{
    (void)unused;
    int ready = 1;
    switch (event->kind) {
    case EM_STARTUP_BOOT_RESOURCES:
        ready = boot_resources();
        break;
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
        begin_movie(event->serial, 0, 0);
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
        /* 001AC3B0 state 0: 001FBC50 (em_sfx_stop_all and its two 00119828
         * calls) and 001FABB0 on the stream lanes. */
        em_startup_audio_stop();
        if (em_scene_bindings_001FBC50() < 0 || em_scene_bindings_001FABB0() < 0)
            fail("001FBC50 / 001FABB0 (the stream lanes)");
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
        /* After a death the same handoff replaces this task with the game
         * task again (001AB790(001ACEC0)); the cold boot installs it. */
        if (f.reinstalled) {
            if (em_game_reinstall_new_001AC070() != 0) fail("001AC070 state 4: the game task");
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
                            em_frame_transition()->substate, 0, em_scene_state()->d275BDC};
    attract_abort();
    int expected_cursor = -1;
    /* The fixtures drive the boot's title only; after a death the player's
     * pad (the level smoke's driver) drives it. */
    if (f.test && !f.reinstalled && em_startup_view(&f.flow, &input).interactive) {
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
        }
        /* EM_STARTUP_MENU_WAIT=N (0..15) holds the title N more idle ticks
         * before the CROSS (tools/test_new_game_switch.py: title routes of
         * both field parities and dwells; below the 30-tick menu test). */
        if (f.menu_ticks == 14 + f.menu_wait && new_game_test(f.test))
            input.held = input.pressed = EM_PAD_CROSS;
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
    f.interactive = view.interactive;
    f.cursor = view.cursor;
    /* The level smoke drives the title after a death with the pad (its
     * after-frame hook, as at every world frame's close-out). */
    if (f.reinstalled) em_level_smoke_test_after_frame();
    if (view.interactive && !f.reinstalled && ++f.menu_ticks == 30 && f.test) {
        unsigned required = (1u << EM_STARTUP_SCREEN_LOGO_A) |
            (1u << EM_STARTUP_SCREEN_LOGO_B) | (1u << EM_STARTUP_SCREEN_LOGO_C) |
            (1u << EM_STARTUP_SCREEN_MOVIE) | (1u << EM_STARTUP_SCREEN_TITLE);
        if ((f.seen_screens & required) != required || !f.movie_frames)
            fail("startup test: did not present every required startup stage");
        if (!f.failed) printf("startup test: PASS (logos, movie, title navigation/clamps)\n");
        em_frame_request_quit();
    }
}

/* 001ADF00's 001AB790(001AC070): the title flow again, replacing the game
 * task in slot 0 (em_task_replace_current, 001AB790's clear of the task
 * record), from 001AC070 state 0, whose D_00275BDC (now 1) opens the menu
 * with the cursor on its second entry. Under EM_NEW_GAME=1 the boot loaded
 * no title screens: they and the title sounds load here. 0, or -1. */
int em_frontend_install_001AC070(void)
{
    if (!f.installed) {
        fprintf(stderr, "startup: 001AC070 without the native frontend (EM_SKIP_STARTUP)\n");
        return -1;
    }
    if (!boot_resources()) return -1;
    em_startup_reinstall_001AC070(&f.flow);
    f.reinstalled = 1;
    f.texture_screen = -1;
    f.previous_screen = -1;
    f.attract_serial = 0;
    if (!em_task_replace_current(startup_task)) return -1;
    printf("startup: 001AC070 again (D_00275BDC = %u), engine frame %u\n",
           (unsigned)em_scene_state()->d275BDC, em_frame_counter());
    return 0;
}

int em_frontend_movie_select(uint8_t selector)
{
    f.movie_selector = selector;
    return 0;
}

int em_frontend_movie_selector(void) { return f.movie_selector; }

int em_frontend_movie_request(uint8_t value)
{
    if (!f.installed) {
        /* EM_SKIP_STARTUP: no frontend, so no movie pump for step M. */
        fprintf(stderr, "startup: movie request without the native frontend (EM_SKIP_STARTUP)\n");
        em_frame_request_quit();
        return -1;
    }
    if (value != 1 || f.movie_selector < 0 ||
        (size_t)f.movie_selector >= sizeof k_movie_paths / sizeof k_movie_paths[0] || f.movie) {
        char error[128];
        snprintf(error, sizeof error, "movie request D_00821058=%u with selector %d is not exported",
                 (unsigned)value, f.movie_selector);
        fail(error);
        return -1;
    }
    begin_movie(0, 1, (unsigned)f.movie_selector);
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
    const char *wait = f.test ? getenv("EM_STARTUP_MENU_WAIT") : NULL;
    f.menu_wait = wait ? (unsigned)strtoul(wait, NULL, 10) : 0;
    if (f.menu_wait > 15)
        f.menu_wait = 15;
    em_startup_init(&f.flow, notify, NULL);
    em_frame_set_movie_pump(movie_pump, NULL);
    em_task_register(0, startup_task);
}

/* EM_NEW_GAME=1 (em_new_game_switch.h): the frontend's host services only
 * (the movie player for 001AD360 step 1), no startup task, and the title's
 * New Game handoff at once: 001AC070 state 4 with D_00275BE0 = 0, the same
 * em_game_install_new the EM_STARTUP_NEW_GAME event calls. The startup
 * flow's screens, sound sequencer and card check never run; what the
 * game state at the opening's first frame owes them is checked by
 * tools/test_new_game_switch.py. */
void em_frontend_install_new_game(void)
{
    memset(&f, 0, sizeof f);
    f.texture_screen = f.previous_screen = -1;
    f.movie_selector = -1;
    f.installed = 1;
    f.new_game_switch = 1;
    f.switch_boot = 1;
    /* The shared audio device opens at the boot, as the frontend's boot
     * resources open it (em_startup_audio_init); the title's own sounds are
     * not loaded, since nothing after New Game plays them. */
    if (em_bgm_device_ensure(48000) != 0) {
        fail("could not open the audio device");
        return;
    }
    em_frame_set_movie_pump(movie_pump, NULL);
    printf("startup: EM_NEW_GAME=1: no frontend; New Game (001AC070 state 4)\n");
    em_game_install_new();
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

int em_frontend_title_menu(unsigned *cursor)
{
    if (cursor) *cursor = f.cursor;
    return f.installed && f.interactive && em_task_slot(0) && em_task_slot(0)->fn == startup_task;
}

const void *em_frontend_host_state(size_t *size)
{
    *size = sizeof f;
    return &f;
}

void em_frontend_service_state(int32_t out[4])
{
    out[0] = f.installed;
    out[1] = f.movie_selector;
    out[2] = f.movie != NULL;
    out[3] = f.failed;
}
