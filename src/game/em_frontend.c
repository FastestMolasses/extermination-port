/* Host services for em_startup. Original control flow lives in em_startup.c;
 * this file adapts disc exports, native movie playback, and native storage.
 * It deliberately keeps startup assets out of the gameplay fixture loader.
 */
#include "game/em_memcard.h"
#include "game/em_frontend.h"
#include "game/em_start_at.h"
#include "game/em_area01_terminal_status.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_options_live.h"
#include "game/em_stream_live.h"
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
#include "game/em_movie_field.h"
#include "game/em_movie_sound.h"
#include "game/em_gs_texture.h"
#include "game/em_module_loader.h"
#include "game/em_status_background.h"
#include "game/em_render_context_live.h"
#include "game/em_pad_actuator.h"
#include "game/em_player.h"
#include "game/em_game_internal.h"
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
    EmMovieField movie_field;   /* 00203350 on the field clock (em_movie_field.h) */
    uint32_t movie_steps;       /* the movie iteration's steps, the arming one included */
    uint64_t movie_sound;       /* PCM frames the movie's fields sounded */
    uint64_t movie_sound_base[3]; /* em_movie_sound_counters at the movie's start */
    uint32_t movie_sound_first; /* the first sounding field's vblank (em_frame_vblanks), or UINT32_MAX */
    unsigned menu_ticks;
    unsigned menu_wait;  /* EM_STARTUP_MENU_WAIT (fixtures only) */
    unsigned seen_screens;
    unsigned captured;
    uint32_t load_serial;  /* the title's load screen (001AC070 state 5) */
    int load_drawn;        /* this tick's 00225AC0 call left a draw stream */
    int card_close;        /* the boot title's card binding closes after this tick's draw */
    int load_test;         /* EM_STARTUP_TEST=loadgame: the boot title's LOAD GAME */
    int start_at;          /* EM_START_AT=<name> (em_start_at.h): the same LOAD GAME on a start-point card */
    const char *capture_dir;
    const char *test;
    char error[512];
} f;

/* The EM_STARTUP_TEST fixtures that choose New Game on the title menu:
 * "newgame", "newgame-control" and "newgame-skip" (em_opening_control_test.c)
 * and "newgame-level" (the S13 level smoke, em_level_smoke_test.c). */
static int new_game_test(const char *test)
{
    return test && (strcmp(test, "newgame") == 0 || strcmp(test, "newgame-control") == 0 ||
           strcmp(test, "newgame-level") == 0 || strcmp(test, "newgame-skip") == 0);
}

/* The boot title's LOAD GAME is driven (the title load test, EM_START_AT)
 * and reported (block lines, the placement, control and the quit: the test,
 * and EM_START_AT under EM_START_AT_TEST=1). */
static int load_drive(void) { return f.load_test || f.start_at; }
static int load_report(void) { return f.load_test || (f.start_at && em_start_at_report()); }
static const char *load_prefix(void) { return f.load_test ? "startup test: loadgame" : "startup: EM_START_AT"; }

static uint32_t le32(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void load_test_block(const char *when);
static void load_pad_release(void);

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
 * docs/FIRST_LEVEL_EXIT.md), selector 2 \MOVIE\E006S1.PSS (AREA00's switch
 * movie: the area script host's op 0x0F for the owner 00825920 at route_a00
 * a00_08; docs/LEVEL3_BINDING.md). The port decodes the remux of each file
 * (tools/export_movie.py), selector 3 E007 and selector 4 E008S4 (below); the other four are not on the route
 * played so far and are not exported (a request for one fails).
 * Each movie's field-clock facts (em_movie_field.h; GS_EXACT.md 10.4): its
 * pictures (the fork's sends: 4,761 pairs in the New Game movie, 2,305 in
 * E001), the two held pictures (the stream's last coded picture and the
 * reference before it: P8 / P9 of E001's last GOP, B4 / P5 of E900's) and
 * the feed loop's end after the gate (fork New Game run d0: gate 289, end
 * 9907; E001 canon: 458 / 5189, d4: 457 / 5188; E006S1, decomp
 * build/fork_refs/probes_1020_e006s1/a00_08: 741 pairs, the last 2 sent
 * after the feed loop's end and a 127-vblank stall, gate 372, end 1976;
 * E008S4, selector 4, the AREA01 revisit's event movie (script 0x82AD10 at
 * a01r_01 rows 1069 -> 1070), decomp build/fork_refs/probes_1028_e008s4/
 * a01r_01: 315 pairs, the last 2 after the feed loop's end, a 129-vblank
 * stall, gate 1084, end 1839; the replay of the recorded beat equalled the
 * recording on all 1,077 rows; E007, selector 3, AREA00's exit movie (the
 * shaft door's script at a00_10 row 672), decomp build/fork_refs/
 * probes_1028_e007/a00_10: 691 pairs, the last 2 after the feed loop's
 * end, a 133-vblank stall, gate 686, end 1510; the replay equalled the
 * recording on all 1,095 rows). */
static const struct {
    const char *path;
    const char *name;
    EmMovieFieldConfig field;
} k_movies[] = {
    {"assets/startup/intro.mov", "E900.PSS", {4761, 2, 9618}},   /* 0: \MOVIE\E900.PSS */
    {"assets/movies/e001.mov", "E001.PSS", {2305, 2, 4731}},     /* 1: \MOVIE\E001.PSS */
    {"assets/movies/e006s1.mov", "E006S1.PSS", {741, 2, 1604}},  /* 2: \MOVIE\E006S1.PSS */
    {"assets/movies/e007.mov", "E007.PSS", {691, 2, 1510}},      /* 3: \MOVIE\E007.PSS */
    {"assets/movies/e008s4.mov", "E008S4.PSS", {315, 2, 755}},   /* 4: \MOVIE\E008S4.PSS */
};

static void begin_movie(uint32_t serial, int new_game, unsigned selector)
{
    em_startup_audio_stop();
    em_sfx_stop_all();
    if (selector >= sizeof k_movies / sizeof k_movies[0] || !k_movies[selector].path) {
        fail("movie selector without an exported movie");
        return;
    }
    f.movie = em_movie_open(k_movies[selector].path);
    if (!f.movie) {
        char error[160];
        snprintf(error, sizeof error, "could not open %s (tools/export_movie.py, docs/STARTUP.md)",
                 k_movies[selector].path);
        fail(error);
        return;
    }
    EmMovieInfo info;
    if (em_movie_info(f.movie, &info) != 0) {
        fail(em_movie_error(f.movie));
        return;
    }
    if (info.pictures != k_movies[selector].field.pictures) {
        char error[200];
        snprintf(error, sizeof error, "%s has %u pictures, the original's %s %u (re-export it)",
                 k_movies[selector].path, info.pictures, k_movies[selector].name,
                 k_movies[selector].field.pictures);
        fail(error);
        return;
    }
    if (em_movie_field_begin(&f.movie_field, &k_movies[selector].field) != 0) {
        fail("the movie's field clock refused its configuration");
        return;
    }
    f.movie_serial = serial;
    f.movie_skip = 0;
    f.new_game_movie = new_game;
    f.movie_frames = 0;
    f.movie_picture = 0;
    f.movie_pts = 0;
    f.have_movie_frame = 0;
    f.movie_steps = 0;
    f.movie_sound = 0;
    f.movie_sound_first = UINT32_MAX;
    em_movie_sound_counters(&f.movie_sound_base[0], &f.movie_sound_base[1], &f.movie_sound_base[2]);
    f.texture_screen = -1;
    em_frame_set_movie_active(1);
    f.playing_selector = (int)selector;
    printf("startup: movie %u (%s)%s, D_00810E80 %u, the first wait's field %u\n", selector,
           k_movies[selector].name, new_game ? " for the game task" : "", (unsigned)em_frame_parity(),
           (unsigned)*em_frame_d810E88());
}

/* em_frame's movie vblank hook: 00206030 and the waits on this vblank,
 * then the field's movie sound (em_movie_sound.h). */
static void movie_vblank(void *unused, int field, uint64_t vblank)
{
    (void)unused;
    if (!f.movie || f.failed) return;
    em_movie_field_vblank(&f.movie_field, field);
    if (!f.movie_field.sound_field) return;
    int16_t pcm[2 * 801];
    const uint32_t frames = em_movie_sound_field_frames(vblank);
    const long got = em_movie_read_audio(f.movie, pcm, (long)frames);
    if (got < 0) {
        fail(em_movie_error(f.movie));
        return;
    }
    /* Past the PCM's end the SPU2 plays silence (none of the first
     * level's movies sounds that long: their PCM outlasts the pictures). */
    memset(pcm + 2 * got, 0, sizeof(int16_t) * 2u * (frames - (uint32_t)got));
    em_movie_sound_push(pcm, frames);
    if (f.movie_sound_first == UINT32_MAX) f.movie_sound_first = (uint32_t)vblank;
    f.movie_sound += (uint64_t)got;
}

static int movie_pump(void *unused)
{
    (void)unused;
    if (!f.movie) return 0;
    if (em_startup_audio_tick() != 0) fail("startup audio event queue overflow");
    ++f.movie_steps;
    EmMovieField *mf = &f.movie_field;
    /* The decoder thread's part of this vblank's period. */
    em_movie_field_decode(mf);
    /* The picture the display shows: each one in display order, decoded
     * when its field-0 half is sent. */
    while (!f.failed && mf->shown >= 0 && (!f.have_movie_frame || f.movie_picture < (uint64_t)mf->shown)) {
        EmMovieFrame frame;
        const int r = em_movie_next_picture(f.movie, &frame);
        if (r < 0) {
            fail(em_movie_error(f.movie));
        } else if (r == 0 || frame.picture_index != (f.have_movie_frame ? f.movie_picture + 1 : 0)) {
            fail("the movie's pictures ran out or came out of order");
        } else if (frame.stride != frame.width * 4 ||
                   !em_gfx_overlay_texture_set(em_frame_gfx(), EM_GFX_OVERLAY_TEX_UI, frame.pixels,
                                               frame.width, frame.height)) {
            fail("could not upload the decoded movie frame");
        } else {
            f.movie_width = frame.width;
            f.movie_height = frame.height;
            f.have_movie_frame = 1;
            f.movie_pts = frame.pts_seconds;
            f.movie_picture = frame.picture_index;
            ++f.movie_frames;
        }
    }
    if (f.have_movie_frame) {
        draw_texture(f.movie_width, f.movie_height);
        if (f.movie_pts >= 2.0 && f.playing_selector == 0) capture(8, "intro");
    }
    uint16_t held = em_frame_input()->held;
    /* EM_START_AT presses START on the boot's title movie only: a game
     * task's movie in the loaded game (the AREA01 revisit's E008S4) is the
     * player's to watch or skip. */
    if (!f.reinstalled && ((f.start_at && !f.new_game_movie) ||
                           (f.test && (strcmp(f.test, "skip") == 0 || new_game_test(f.test) || f.load_test))) &&
        f.movie_pts >= 2.0)
        held |= EM_PAD_START;
    /* EM_NEW_GAME=1: START is held from the movie's first picture, so the
     * original skip test below takes it at the first picture it accepts. */
    if (f.new_game_switch && f.new_game_movie)
        held |= EM_PAD_START;
    /* 002036E0's skip: the decoder's picture count D_002DF740 +8 >= 11
     * while the feed loop runs (em_movie_field_skip_ready). */
    const int skip_ready = em_movie_field_skip_ready(mf);
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
        /* The title's movie: 0x70003B90 is 0 (001AC070 clears it every
         * tick), the 0x8F0 buttons (em_startup_movie_input). */
        em_startup_movie_input(&f.flow, held, skip_ready);
    }
    const int playing = !f.failed && em_movie_field_main(mf, f.movie_skip);
    if (playing) return 1;
    uint64_t pushed = 0, overruns = 0, bad_rate = 0;
    em_movie_sound_counters(&pushed, &overruns, &bad_rate);
    /* The sound's line: the step (em_frame_vblanks) of the first sounding
     * field and this movie's ring overruns / wrong-rate pulls (the replay's
     * offline audio, tools/test_replay_movie_audio.py). */
    printf("startup: movie %s, %u displayed frames, last PTS %.3f; %u steps (gate at vblank %u, the play "
           "loop's end at %u, P field %d), %u no-picture vblanks, %u sounding fields (%llu PCM frames)\n",
           f.failed ? "failed" : f.movie_skip ? "skipped" : "completed", f.movie_frames, f.movie_pts,
           f.movie_steps, mf->gate_vblank, mf->end_vblank, (int)*em_frame_d810E88(), mf->no_picture,
           mf->sound_fields, (unsigned long long)f.movie_sound);
    printf("startup: movie sound: first sounding field at step %lld, %llu frames pushed, %llu overruns, "
           "%llu pulls at another rate\n", f.movie_sound_first == UINT32_MAX ? -1LL : (long long)f.movie_sound_first,
           (unsigned long long)(pushed - f.movie_sound_base[0]), (unsigned long long)(overruns - f.movie_sound_base[1]),
           (unsigned long long)(bad_rate - f.movie_sound_base[2]));
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
    /* The boot's card check (001AB9D0 state 3's 0022A460, not translated):
     * the host memory cards data/memcard/slot1 and slot2 (em_memcard, the
     * platform boundary of the card I/O) are made and both ports asked, as
     * the original's check leaves the card server. No fabricated PS2 card
     * record is written into the game state. */
    return em_memcard_boot_check();
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

/* The boot's title has no world, so no AREA11 binding of the card screen
 * (em_area11_interaction_host_options, which serves the title after a
 * death). Its LOAD GAME runs the same translation (em_options_live, the
 * card screen 00225AC0(0) and 00227300 under it) on a binding of its own
 * (docs/SAVE_GAME.md section 6.4):
 *   - the GS memory: the status pages' file (em_gs_texture) cleared, since
 *     the boot's GS holds only the title's own uploads, which the file does
 *     not hold; the card screen's module 0x2A load (00225AC0 m[1] 2,
 *     001FF080(0, 0x2A)) runs the live loader's own steps, and its one chunk
 *     is applied as that module's step (the same chain check as the status
 *     runtime's, docs/MODULE_LOADER.md finding 1). A draw that needs a block
 *     the upload did not write is refused: the screen faults.
 *   - the workers as the AREA11 binding's: the cues 0 / 1 / 4 (0020CD40 /
 *     60 / A0: the system sound set, not exported: silent, reported once
 *     per cue while em_sfx_cue_state is 0, as status_sound),
 *     the colour fades, 001FBC50 / 001FABB0, 001D2830, 001B61C0; 00200970(1)
 *     is not reached on the title (00225AC0 calls it only when 0x70003B90 is
 *     2, the game task's value; the title clears it every tick) and faults.
 * Opened at the load entry, closed after the screen's last draw (the loader
 * chain released before any game task can bind its own). The card code's
 * buffers are not the binding's: em_options_live holds them once per
 * process, so a close and a new open keep them, as the original's .bss. */
static struct {
    EmGsTexture *gs;
    EmOptionsLive *options;
    int hooked;
} s_card;

static uint32_t card_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* 001FF3F0 state 3's send of module 0x2A's chunk (00200830 of D_00275C74). */
static int card_chain(void *context, uint32_t chain, const uint8_t *bytes, uint32_t size)
{
    (void)context;
    EmModuleLoader *ml = em_module_loader_live();
    const EmTask *record = em_module_loader_record(ml);
    const EmStatusSceneLoader *ld = em_module_loader_state(ml);
    if (!bytes || !record || !ld || record->user[0] != 0 || record->user[6] != 0x2A || chain != ld->d275C74)
        return -1;
    const uint8_t *h = ld->header;
    if (card_le32(h) != 0x2A || (h[0x0E] | h[0x0F] << 8) != 1 || card_le32(h + 0x10) != 0 ||
        size < card_le32(h + 0x24))
        return -1;
    return em_gs_texture_apply(s_card.gs, 0x2A) ? 0 : -1;
}

static int card_sound(void *context, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    (void)context;
    if ((id != 0 && id != 1 && id != 4) || a1 != 0x1000 || a2 != 0x1000 || a3 != 0x1000) return -1;
    /* As the AREA11 binding's status_sound: reported only while the cue
     * has no exported sample (em_sfx_cue_state 0), once per cue. */
    static unsigned reported;
    if (!em_sfx_cue_state((unsigned)id) && !(reported & (1u << id))) {
        reported |= 1u << id;
        fprintf(stderr, "startup: the card screen's cue 0x%X has no exported sample "
                        "(system sound set, WP-14); silent\n", (unsigned)id);
    }
    em_sfx_play((unsigned)id);
    return 0;
}

static int card_module_load(void *context, uint32_t module)
{
    (void)context;
    return module == 0x2A && em_module_loader_request_001FF080(em_module_loader_live(), 0, 0x2A) == 0;
}

static int card_restore(void *context)
{
    (void)context;
    fprintf(stderr, "startup: 00200970(1) on the boot's title (0x70003B90 is 0 there)\n");
    return 0;
}

static int card_fade(void *context, int out, int32_t a0, int32_t a1)
{
    (void)context;
    em_frame_fade_start_colour(out ? 1 : -1, (int16_t)a0, (uint8_t)a1);
    return 1;
}

static int card_stop_sounds(void *context)
{
    (void)context;
    return em_scene_bindings_001FBC50() == 0;
}

static int card_stop_streams(void *context)
{
    (void)context;
    return em_scene_bindings_001FABB0() == 0;
}

static int card_draw_context(void *context, int32_t a0, int32_t a1)
{
    (void)context;
    return em_rcl_001D2830(a0, a1) >= 0;
}

static int card_rumble(void *context, int32_t big, int32_t small, int32_t duration, int32_t force)
{
    (void)context;
    return em_pad_actuator_001B61C0((uint8_t)big, (uint8_t)small, duration, force) == 0;
}

static void card_close(void)
{
    if (s_card.hooked) em_module_loader_set_chain_hook(em_module_loader_live(), NULL, NULL);
    s_card.hooked = 0;
    em_options_live_free(s_card.options);
    em_gs_texture_free(s_card.gs);
    s_card.options = NULL;
    s_card.gs = NULL;
}

static int card_open(void)
{
    static const EmOptionsHost host = {NULL, card_sound, card_module_load, card_restore, card_fade,
                                       card_stop_sounds, card_stop_streams, card_draw_context, card_rumble};
    EmModuleLoader *ml = em_module_loader_live();
    if (!ml) return 0;
    if (!s_card.options) {
        s_card.gs = em_gs_texture_load("assets/status_pages/status_pages.emsp");
        if (!s_card.gs) {
            fprintf(stderr, "startup: assets/status_pages/status_pages.emsp is missing "
                            "(python3 tools/export_status_pages.py)\n");
            return 0;
        }
        em_gs_texture_clear(s_card.gs);
        /* 0020A7A0's sine 0011E2A8 reads the SDK tables of the user's ELF. */
        if (!em_status_background_load_sdk("assets/sdk_math_tables.emsm") ||
            !(s_card.options = em_options_live_create(s_card.gs, &host))) {
            card_close();
            return 0;
        }
    }
    em_module_loader_set_chain_hook(ml, card_chain, NULL);
    s_card.hooked = 1;
    return 1;
}

/* The card screen's binding of this title: the AREA11 one after a death,
 * the boot title's own before any game. */
static EmOptionsLive *load_options(void)
{
    return f.reinstalled ? em_area11_interaction_host_options() : s_card.options;
}

/* The title's load screen (docs/OPTIONS.md section 4.3): 001AC070 state
 * 2's load verdict runs 00225A00 (the card record D_00810040 cleared) and
 * sets D_00275BE0 = 1; state 5 then calls 00225AC0(0) every tick through
 * the options binding the AREA11 interaction host keeps (em_options_live,
 * the card screen's module 0x2A in the status pages' GS memory, the card
 * I/O em_memcard): 1 (the screen's exit) goes back to the menu, 2 (a game
 * loaded: 00227300's LOAD GAME read the slot into D_00810700, the whole
 * block one storage) runs 001AF150 and hands off to 001AC070 state 4 with
 * id 1, the game task for a loaded game (docs/SAVE_GAME.md section 6.3).
 * After a death the binding is the AREA11 host's; on the boot's title it is
 * the title's own (s_card above, section 6.4). The first level's recordings
 * reach the load screen from the title after a death (decomp CAPTURES_C10.md
 * dmg_05); no recording of the boot title's load screen exists. */
static void load_begin(uint32_t serial)
{
    f.load_serial = 0;
    if (!f.reinstalled && !card_open()) {
        fail("the boot title's load screen 00225AC0 could not be bound");
        return;
    }
    if (!load_options()) {
        fail("the load screen 00225AC0 has no options binding");
        return;
    }
    if (em_area01_terminal_reset(em_scene_state()) != 0) {
        fail("00225A00 (the card record's clear)");
        return;
    }
    em_scene_state()->d275BE0 = 1;
    f.load_serial = serial;
}

static void load_frame(void)
{
    EmOptionsLive *options = load_options();
    EmSceneState *scene = em_scene_state();
    EmTask *task = em_task_current();      /* 001AC070 in slot 0 */
    if (!f.load_serial || !options || !task || task != em_task_slot(0)) {
        fail("001AC070 state 5 without its load screen");
        return;
    }
    EmOptionsFrame frame = {
        .settings = scene->d810118,
        .task_address = 0x0028A750u,        /* slot 0: 001AC070's record */
        .task = task->user,
        .mc = scene->d810040,
        .progress = scene->progress.bytes,
        .message = em_message_live_block(),
        .busy = &scene->d275BD8,
        .masks = scene->spad3B74,
        .offset = &scene->spad3B94,
        .read_phase = em_stream_live_read_phase(),
        .fade = em_frame_transition()->substate,
        .held = scene->d810E70,
        .pressed = scene->d810E74,
        .repeat = em_frame_pad_block()->repeat,
        .pad_mode = em_frame_d810E6A(),
        .pad_phase = scene->d810E50,
        .spad3B90 = scene->spad3B90,
        .spad3B93 = scene->spad3B93,
    };
    uint32_t r = 0;
    if (em_options_live_00225AC0(options, &frame, 0, &r) < 0) {
        fail("00225AC0 (the title's load screen) faulted");
        return;
    }
    f.load_drawn = 1;
    if (r == 2 && em_options_live_001AF150(options, &frame) < 0) {
        /* 001AC070 state 5's r == 2: 001AF150 (the settings, the button
         * masks and the screen offset back from the block just read), then
         * D_00275BE0 = 1 (already 1 since state 2) and state 4. */
        fail("001AF150 (the settings from the loaded block) faulted");
        return;
    }
    if (r != 0) {
        const uint32_t serial = f.load_serial;
        f.load_serial = 0;
        /* The boot title's binding goes after this tick's draw; 00225AC0
         * loads module 0x2A again at the next entry. */
        if (!f.reinstalled) f.card_close = 1;
        em_startup_complete(&f.flow, serial, (int)r);
    }
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
        if (!ready) fail("the host memory cards data/memcard/slot1, slot2 are unavailable");
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
         * task; id 1 is a loaded game (D_00275BE0 = 1). The intro movie of
         * a New Game is requested later by the chain itself, at 001AD360
         * step 1 (em_frontend_movie_request). */
        if (event->id != 0 && load_report()) load_test_block("read");
        card_close();
        if (event->id != 0) {
            /* A loaded game (state 5's 00225AC0(0) returned 2): the same
             * 001AB790(001ACEC0) with D_00275BE0 = 1, whose chain enters the
             * block's area: after a death em_game_reinstall_loaded_001AC070,
             * on the boot's title em_game_install_loaded_001AC070 (the game
             * state as at the boot's New Game, the block the slot's). */
            if ((f.reinstalled ? em_game_reinstall_loaded_001AC070() : em_game_install_loaded_001AC070()) != 0)
                fail("001AC070 state 4: the game task for a loaded game");
            else if (load_drive())
                printf("%s: the loaded game's handoff at engine frame %u, area %02X %02X %02X\n", load_prefix(),
                       em_frame_counter(), em_scene_state()->d810700, em_scene_state()->d810701,
                       em_scene_state()->d810702);
            if (load_report()) load_test_block("handoff");
            /* EM_START_AT: the pad is the player's from the handoff on, in
             * a report run (EM_START_AT_TEST=1) as in a developer's run;
             * the report run's handback probe (em_frontend_load_test_
             * before_frame, phase 5) checks it. */
            if (f.start_at) load_pad_release();
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
        load_begin(event->serial);
        return;
    case EM_STARTUP_LOAD_GAME_FRAME:
        load_frame();
        return;
    case EM_STARTUP_OPTIONS:
        fprintf(stderr, "startup: service %d awaits native translation\n", event->kind);
        /* Preserve pending state rather than manufacturing an outcome. */
        return;
    }
    em_startup_complete(&f.flow, event->serial, ready ? 1 : -1);
}

/* EM_STARTUP_TEST=loadgame (test input only; tools/test_title_load_game.py)
 * and EM_START_AT=<name> (em_start_at.h; the same presses):
 * the boot's title, LOAD GAME (load_test's menu presses), then on the card
 * screen the policy of tools/test_card_screen_reference.py (slot 0 on port 0,
 * Yes; the level smoke's DS_LOAD_SLOT) once the screen is up with the fade
 * idle and 60 ticks: each press is the card record's game word as pad
 * buttons (em_pad_swap), held two ticks, then the policy waits until the
 * record D_00810040 changes (30 ticks at most, 40 presses at most). After
 * the handoff the world frames (em_frontend_load_test_before_frame) wait
 * for the placement and control in the loaded area, then 60 ticks. */
static struct {
    int phase;          /* 0 the title, 1 the screen up, 2 pressing, 3 the game */
    int ticks, hold, tries, presses;
    uint8_t mc[0x18];
    uint32_t placed_counter, control_counter;
    int handback;       /* EM_START_AT_TEST: the handback probe saw its button */
} lt;

/* EM_START_AT_TEST's handback probe button (L3: no use in a first-level
 * area's play, and the run quits 60 ticks after control). */
#define HANDBACK_PROBE EM_PAD_L3

/* The whole progress block D_00810700..D_00810D3F as hex, for the test's
 * byte-for-byte comparison with the slot's EX_DATA.00. */
static void load_test_block(const char *when)
{
    const uint8_t *b = em_scene_progress_at(em_scene_state(), 0x00810700u, 0x640u);
    if (!b) {
        fprintf(stderr, "%s: FAIL: the progress block is not one canonical storage\n", load_prefix());
        fail("the progress block is not one canonical storage");
        return;
    }
    printf("%s: block %s ", load_prefix(), when);
    for (unsigned i = 0; i < 0x640u; i++) printf("%02X", b[i]);
    printf("\n");
}

/* The driven pad: the outer input layer (em_input_set_filter) replaces
 * the whole pad, the keyboard's and a physical controller's, with these
 * buttons and centred sticks while the drive holds it. */
static struct {
    int on;
    uint16_t buttons;
} drive;

static void load_pad_filter(EmPadState *pad, void *user)
{
    (void)user;
    const EmPadState driven = {drive.buttons, 0, 0, 0, 0};
    *pad = driven;
}

static void load_test_pad(uint16_t buttons)
{
    drive.buttons = buttons;
    if (!drive.on) {
        em_input_set_filter(load_pad_filter, NULL);
        drive.on = 1;
    }
}

static void load_pad_release(void)
{
    if (drive.on) em_input_set_filter(NULL, NULL);
    drive.on = 0;
    drive.buttons = 0;
}

static void load_test_fail(const char *why)
{
    fprintf(stderr, "%s: FAIL at engine frame %u: %s\n", load_prefix(), em_frame_counter(), why);
    fail(why);
}

static void load_test_after_frame(void)
{
    unsigned major = 0, sub = 0;
    em_frontend_title_state(&major, &sub);
    const uint8_t *mc = em_scene_state()->d810040;
    if (lt.phase == 0) {
        if (major == 5 && mc[0] == 1 && em_frame_transition()->substate == 0) {
            lt.phase = 1;
            lt.ticks = 0;
            printf("%s: the card screen up at engine frame %u (card %u %u %u)\n", load_prefix(),
                   em_frame_counter(), mc[0], mc[1], mc[0x15]);
        } else if (++lt.ticks > 4000) {
            load_test_fail("the boot title's load screen did not come up");
        }
        return;
    }
    if (lt.phase == 1) {
        load_test_pad(0);
        if (++lt.ticks >= 60) {
            lt.phase = 2;
            lt.ticks = 0;
        }
        return;
    }
    if (lt.phase != 2) return;
    if (major != 5) {
        if (!lt.presses) {
            load_test_fail("the load screen ended before a press");
            return;
        }
        load_test_pad(0);
        lt.phase = 3;
        lt.ticks = 0;
        printf("%s: the load screen ends at engine frame %u (title state %u, card %u %u %u %u) after %d "
               "presses\n", load_prefix(), em_frame_counter(), major, mc[0], mc[1], mc[0x15], mc[0x16], lt.presses);
        return;
    }
    if (lt.hold > 0 && lt.hold < 3) {
        if (++lt.hold == 3) load_test_pad(0);
        return;
    }
    if (lt.hold == 3) {
        load_test_pad(0);
        if (memcmp(lt.mc, mc, sizeof lt.mc) == 0 && ++lt.ticks <= 30) return;
        lt.hold = lt.ticks = 0;
        return;
    }
    const uint8_t screen = mc[1], step = mc[2], st3 = mc[3], yes = mc[9], cur = mc[0xA];
    uint16_t word = 0;
    if (screen == 2) {
        if (step == 0) word = cur > 0 ? 0x1000 : 0x40;
        else if (step == 1 && st3 == 1) word = yes == 0 ? 0x8000 : 0x40;
    } else if (screen == 4) {
        if (step == 0) word = cur > 0 ? 0x1000 : 0x40;
        else if (step == 1 && st3 == 1) word = 0x40;
    }
    load_test_pad(0);
    if (!word) {
        if (++lt.ticks > 2000) load_test_fail("the load screen's load did not end");
        return;
    }
    if (++lt.tries > 40) {
        load_test_fail("the load screen's presses had no effect");
        return;
    }
    printf("%s: press %04X at engine frame %u (card %u %u %u %u %u %u)\n", load_prefix(), word,
           em_frame_counter(), mc[0], screen, step, st3, yes, cur);
    memcpy(lt.mc, mc, sizeof lt.mc);
    ++lt.presses;
    load_test_pad(em_pad_swap(word));
    lt.hold = 1;
    lt.ticks = 0;
}

static uint8_t load_test_task_byte(unsigned offset)
{
    EmTask *task = (EmTask *)em_task_slot(0);
    const uint8_t *b = task ? em_scene_task_byte(task->user, offset) : NULL;
    return b ? *b : 0xFF;
}

static float load_test_f32(const uint8_t *p)
{
    float v;
    memcpy(&v, p, 4);
    return v;
}

/* EM_START_AT without EM_START_AT_TEST: the run is the player's after the
 * handoff; the placement and control are only reported, and the frames are
 * paced again at control (or when it does not come). */
static void start_at_give_up(const char *why)
{
    fprintf(stderr, "%s: %s at engine frame %u; the run goes on paced\n", load_prefix(), why, em_frame_counter());
    em_frame_set_unpaced(0);
    lt.phase = 6;
}

/* EM_START_AT_PAD's clock steps once per frame: in a world frame at its
 * head (below), in a status frame (frame-machine state 3, the world frozen:
 * no world-frame head runs) at its close-out, so a recorded press on the
 * status screen (Triangle closing the revisit's item page) is played. */
static int start_at_pad_stepped;

void em_frontend_start_at_after_frame(void)
{
    if (f.start_at && lt.phase == 6 && !start_at_pad_stepped)
        (void)em_start_at_pad_frame(em_frame_counter(), NULL, g.cam.fwd);
    start_at_pad_stepped = 0;
}

void em_frontend_load_test_before_frame(void)
{
    start_at_pad_stepped = 1;
    /* EM_START_AT_PATH follows the path only while the player has control
     * (0x70003B8D clear): a scripted stretch moves the player itself, so
     * the clock does not wait on the recorded position there. */
    const int path_control = f.start_at && lt.phase == 6 && ((const EmSceneState *)em_scene_state())->spad3B8D == 0;
    if (f.start_at && lt.phase == 6 &&
        em_start_at_pad_frame(em_frame_counter(), path_control ? g.pos : NULL, g.cam.fwd) == 0 &&
        em_start_at_pad_progress(em_frame_counter())) {
        /* EM_START_AT_PAD's progress line every 300 frames: where the player
         * is and the AREA01 revisit's story bytes (D_008107DF event stage,
         * D_008107E0 bridge counter, D_0081075F, D_00810760). */
        EmSceneState *sc = (EmSceneState *)em_scene_state();
        const uint8_t *df = em_scene_progress_at(sc, 0x008107DFu, 2);
        const uint8_t *e5f = em_scene_progress_at(sc, 0x0081075Fu, 2);
        printf("%s: pad frame %d at (%.2f,%.2f,%.2f) area %02X %02X 3B8D %u DF %02X E0 %02X 75F %02X 760 %02X\n",
               load_prefix(), em_start_at_pad_clock(), g.pos[0], g.pos[1], g.pos[2], sc->d810700,
               sc->d810701, sc->spad3B8D, df ? df[0] : 0xEEu, df ? df[1] : 0xEEu, e5f ? e5f[0] : 0xEEu,
               e5f ? e5f[1] : 0xEEu);
        fflush(stdout);
    }
    if (!load_drive() || f.failed || lt.phase < 3 || lt.phase > 5) return;
    const EmSceneState *sc = em_scene_state();
    const EmPlayerLiveActor *a = player_states_actor();
    const int report = load_report();
    /* The title load test keeps its pad driven (and zero) to the end;
     * EM_START_AT released it at the handoff. */
    if (report && !f.start_at) load_test_pad(0);
    if (lt.phase == 3) {
        if (sc->d275BE0 == 0 && load_test_task_byte(EM_SCENE_TASK_08) == 3 &&
            load_test_task_byte(EM_SCENE_TASK_09) == 1 && a) {
            /* 001B07C0(0) with D_00275BE0 == 1 placed the player from the
             * load pose D_00810710 / 720 and 001B0460 cleared it. */
            const uint8_t *pose = em_scene_progress_at((EmSceneState *)sc, 0x00810710u, 0x20);
            lt.placed_counter = em_frame_counter();
            lt.phase = 4;
            printf("%s: placed at engine frame %u in area %02X %02X %02X at (%.3f,%.5f,%.3f) "
                   "heading %.5f, the load pose (%.3f,%.5f,%.3f) heading %.5f, health %.1f\n",
                   load_prefix(), lt.placed_counter, sc->d810700, sc->d810701, sc->d810702, g.pos[0], g.pos[1], g.pos[2], g.yaw,
                   pose ? load_test_f32(pose) : -1.0f,
                   pose ? load_test_f32(pose + 4) : -1.0f, pose ? load_test_f32(pose + 8) : -1.0f,
                   pose ? load_test_f32(pose + 0x14) : -1.0f, sc->d810858);
            if (report) load_test_block("placed");
        } else if (++lt.ticks > 6000) {
            if (report) load_test_fail("no placement in the loaded area");
            else start_at_give_up("no placement in the loaded area");
        }
        return;
    }
    if (lt.phase == 4) {
        if (sc->spad3B8D == 0 && !player_pose_owned() && !em_game_player_interact_busy() &&
            load_test_task_byte(EM_SCENE_TASK_0B) == 1 && a && em_live_u8(a, 4) == 1) {
            lt.control_counter = em_frame_counter();
            lt.phase = 5;
            lt.ticks = 0;
            printf("%s: control in the loaded area %02X at engine frame %u (%.3f,%.5f,%.3f) heading %.5f "
                   "health %.1f\n", load_prefix(), sc->d810700, lt.control_counter,
                   load_test_f32(a->bytes + 0xA0), load_test_f32(a->bytes + 0xA4), load_test_f32(a->bytes + 0xA8),
                   load_test_f32(a->bytes + 0xC4), sc->d810858);
            if (report) load_test_block("control");
            if (f.start_at) em_frame_set_unpaced(0);
            if (f.start_at && !report) {
                lt.phase = 6;
                em_start_at_pad_control(lt.control_counter);
            }
        } else if (++lt.ticks > 6000) {
            if (report) load_test_fail("no control in the loaded area");
            else start_at_give_up("no control in the loaded area");
        }
        return;
    }
    if (lt.phase == 5 && f.start_at && report && !lt.handback) {
        /* EM_START_AT_TEST's handback probe: from control the probe holds
         * HANDBACK_PROBE through the test buttons (em_input_set_test_
         * buttons), ORed into the pad before the outer input layer; a
         * driven pad (em_input_set_filter) would erase it. It must reach
         * the unpacked held mask D_00810E70 (em_frame_input) within 10
         * ticks: the pad is the player's. */
        if (lt.ticks == 1) em_input_set_test_buttons(HANDBACK_PROBE);
        if (lt.ticks >= 1 && (em_frame_input()->held & HANDBACK_PROBE)) {
            em_input_set_test_buttons(0);
            lt.handback = 1;
            printf("%s: the pad is the player's after the handoff: a held %04X reached D_00810E70 at engine "
                   "frame %u\n", load_prefix(), (unsigned)HANDBACK_PROBE, em_frame_counter());
        } else if (lt.ticks > 10) {
            em_input_set_test_buttons(0);
            load_test_fail("the pad is not the player's after the handoff (a held button did not reach "
                           "D_00810E70: the driven pad is still installed)");
            return;
        }
    }
    if (lt.phase == 5 && ++lt.ticks >= 60) {
        if (f.start_at && report && !lt.handback) {
            load_test_fail("no handback probe");
            return;
        }
        lt.phase = 6;
        printf("%s: PASS (the boot title's LOAD GAME, area %02X, control at engine frame %u)\n", load_prefix(),
               sc->d810700, lt.control_counter);
        em_frame_request_quit();
    }
}

static void startup_task(void)
{
    if (em_startup_audio_tick() != 0) fail("startup audio event queue overflow");
    if (f.reinstalled && em_level_smoke_test_active()) {
        /* The level smoke's title rows (tools/level_smoke_damage.py): the
         * state after the previous frame (its fade step and the loader task
         * in slot 2 ran): 001AC070's state +8, 001AC480's sub-state +9 (the
         * menu's), the fade D_0028A9A0, the busy byte D_00275BD8 and the card
         * record D_00810040 +0, +1, +0x14, +0x15, +0x16. */
        const EmSceneState *scene = em_scene_state();
        printf("startup: title row counter %u state %u sub %u fade %d busy %u card %u %u %u %u %u\n",
               em_frame_counter(), f.flow.major, f.flow.major == 2 ? f.flow.sub : 0,
               (int)em_frame_transition()->substate, (unsigned)scene->d275BD8, scene->d810040[0],
               scene->d810040[1], scene->d810040[0x14], scene->d810040[0x15], scene->d810040[0x16]);
    }
    /* 001AC070 (and the boot's flow before it) clears the scratchpad byte
     * 0x70003B90 on entry every tick; the game task 001ACEC0 writes 2. */
    em_scene_state()->spad3B90 = 0;
    /* D_00810E74 / E70 / E50 as this frame's step C left them (the title's
     * load screen reads them), as at the start of every game-task tick. */
    em_frame_scene_input(em_scene_state());
    f.load_drawn = 0;
    const EmFrameInput *pad = em_frame_input();
    EmStartupInput input = {pad->held, pad->pressed,
                            em_frame_transition()->substate, 0, em_scene_state()->d275BDC};
    attract_abort();
    int expected_cursor = -1;
    /* The fixtures drive the boot's title only; after a death the player's
     * pad (the level smoke's driver) drives it. */
    if ((f.test || f.start_at) && !f.reinstalled && em_startup_view(&f.flow, &input).interactive) {
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
        /* The CROSS at menu tick 15: the New Game movie's iteration then
         * has D_00810E80 = 1, as in the canonical fork run of the original
         * (decomp build/fork_refs/probes_1016/newgame/d0), so the field
         * relation after it is the reference recordings' (GS_EXACT.md
         * 10.4: after a movie the relation follows the parity it was
         * entered on). A player's press decides it in real play; both
         * outcomes are original. EM_STARTUP_MENU_WAIT=N (0..14) holds the
         * title N more idle ticks before the CROSS
         * (tools/test_new_game_switch.py: title routes of both field
         * parities and dwells; below the 30-tick menu test). */
        if (f.menu_ticks == 15 + f.menu_wait && new_game_test(f.test))
            input.held = input.pressed = EM_PAD_CROSS;
        /* EM_STARTUP_TEST=loadgame: Down to LOAD GAME at tick 13, its
         * CROSS at tick 15 (the boot title's load entry, state 5). */
        if (load_drive() && f.menu_ticks == 13) {
            input.held = input.pressed = EM_PAD_DOWN;
            expected_cursor = 1;
        }
        if (load_drive() && f.menu_ticks == 15)
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
    /* The load screen's draw stream of this tick's 00225AC0 call. */
    if (f.load_drawn) {
        if (em_options_live_render(load_options(), gfx) != 1)
            fail("the load screen's draw stream");
        const uint8_t *card = em_scene_state()->d810040;
        if (card[0] == 1 && card[0x15] == 1 && em_frame_transition()->substate == 0)
            capture(1u << 10, "load_screen");   /* the slot choice, idle */
    }
    if (f.card_close) {
        f.card_close = 0;
        card_close();
    }
    if (load_drive()) load_test_after_frame();
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
        (size_t)f.movie_selector >= sizeof k_movies / sizeof k_movies[0] ||
        !k_movies[f.movie_selector].path || f.movie) {
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
    f.load_test = f.test && strcmp(f.test, "loadgame") == 0;
    /* Test input only (EM_STARTUP_TEST runs): the host card folder for the
     * whole run, set before the boot's card check (the level smoke's
     * dmg_load_game loads a copy of a start-point card). */
    const char *card = f.test ? getenv("EM_TEST_MEMCARD_ROOT") : NULL;
    if (card && card[0])
        em_memcard_set_root(card);
    /* EM_START_AT=<name> (em_start_at.h, checked at launch): the start-point
     * card, read-only, for the whole run; the boot runs unpaced to the
     * loaded area's control. */
    if (em_start_at_name()) {
        f.start_at = 1;
        em_memcard_set_root(em_start_at_card_root());
        em_memcard_set_read_only(1);
        em_frame_set_unpaced(1);
    }
    const char *wait = f.test ? getenv("EM_STARTUP_MENU_WAIT") : NULL;
    f.menu_wait = wait ? (unsigned)strtoul(wait, NULL, 10) : 0;
    if (f.menu_wait > 14)
        f.menu_wait = 14;
    em_startup_init(&f.flow, notify, NULL);
    em_frame_set_movie_pump(movie_pump, NULL);
    em_frame_set_movie_vblank(movie_vblank, NULL);
    em_task_register(0, startup_task);
}

/* EM_NEW_GAME=1 (em_new_game_switch.h): the frontend's host services only
 * (the movie player for 001AD360 step 1), no startup task, and the title's
 * New Game handoff at once: 001AC070 state 4 with D_00275BE0 = 0, the same
 * em_game_install_new the EM_STARTUP_NEW_GAME event calls. The startup
 * flow's screens and sound sequencer never run; the boot's card check
 * does (the card server's state it leaves, em_memcard_boot_check, is game
 * state the options' load row reads). What the game state at the opening's
 * first frame owes the title is checked by tools/test_new_game_switch.py. */
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
    if (!native_storage_ready()) {
        fail("the host memory cards data/memcard/slot1, slot2 are unavailable");
        return;
    }
    em_frame_set_movie_pump(movie_pump, NULL);
    em_frame_set_movie_vblank(movie_vblank, NULL);
    printf("startup: EM_NEW_GAME=1: no frontend; New Game (001AC070 state 4)\n");
    em_game_install_new();
}

void em_frontend_shutdown(void)
{
    card_close();
    em_startup_audio_stop();
    em_frame_set_movie_pump(NULL, NULL);
    em_frame_set_movie_vblank(NULL, NULL);
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

void em_frontend_title_state(unsigned *state, unsigned *sub)
{
    const int held = f.installed && em_task_slot(0) && em_task_slot(0)->fn == startup_task;
    if (state) *state = held ? f.flow.major : 0;
    if (sub) *sub = held && f.flow.major == 2 ? f.flow.sub : 0;
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
