/* em_replay.c — input recording and playback for the video comparison.
 * See em_replay.h. Host tooling only: no original function stands behind
 * this file, and nothing here runs unless one of its variables is set.
 *
 * One row per main-loop step (em_frame_step), written when the step reads
 * its pad (the filter runs inside em_input_pad, called once per step by the
 * frame loop's step C). A step whose counter does not advance is a movie
 * step (the blocking movie suspends the main iteration, em_frame.c); every
 * other step is one game tick, the original's main-loop iteration.
 *
 * Sync phase of a tick, from the state the tick starts from (the state the
 * original shows at its main-loop top 0x001AAF28):
 *   T  title / front end: D_00810700 (area) == 0
 *   L  load: D_00275BD8 != 0 (001ADF50 / 001AD4E0 set it and wait for 0)
 *      or the slot-0 task's +9 == 5 (001AD250's load state, which also
 *      covers the load veil's tail after the busy byte clears)
 *   C  cutscene or menu: scratchpad 0x70003B8D != 0 (the world-frame
 *      selector the opening, the scripts and the status screen set)
 *   P  play: otherwise
 * A segment is a run of ticks with one phase. The PCSX2 side
 * (tools/video_compare/ps2.py) computes the same phases from the same
 * addresses. */
#include "game/em_replay.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_input.h"
#include "em_settings.h"
#include "game/em_bgm.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_replay_audio.h"
#include "game/em_scene_bindings.h"
#include "game/em_task.h"

#define REC_MAGIC "EMREC 1"
#define REC_COLS "step counter mv ph btn lx ly rx ry area sub entry bd8 s8d t9 tb x y z yaw seg off cap af"

typedef struct {
    uint16_t btn;
    uint8_t axis[4]; /* lx ly rx ry, raw bytes (0x80 = centre) */
} RecPad;

typedef struct {
    char phase;
    uint32_t start, len; /* into s.ticks */
} RecSeg;

static struct {
    int installed;
    FILE *out;          /* EM_INPUT_RECORD */
    /* playback (EM_INPUT_PLAY) */
    int playing;
    RecPad *ticks;
    uint32_t nticks, cap_ticks;
    RecSeg *segs;
    uint32_t nsegs, cap_segs;
    uint8_t *movie_skip;
    uint32_t nmovies, cap_movies;
    uint32_t movie_index;
    long max_overrun;
    int done;
    /* live segment tracking (record and playback) */
    char prev_phase;
    uint32_t cur_seg;  /* playback: recorded segment; record: live count - 1 */
    uint32_t off;
    int in_movie;
    uint32_t step;
    /* captures */
    const char *capture;
    uint32_t capture_every, captures;
    /* offline audio */
    FILE *wav;
    uint64_t audio_frames;
    float *mix;
    int16_t *pcm;
} s;

static char live_phase(const EmSceneState *st)
{
    const EmTask *t0 = em_task_slot(0);
    if (st->d810700 == 0) return 'T';
    if (st->d275BD8 || (t0 && t0->user[1] == 5)) return 'L';
    if (st->spad3B8D) return 'C';
    return 'P';
}

static uint8_t axis_byte(float v)
{
    int b = 0x80 + (int)(v * 128.0f); /* em_pad_raw's conversion */
    return (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
}

/* Raw stick byte -> the float em_pad_raw maps back to the same byte. */
static float axis_float(uint8_t b) { return ((float)b - 128.0f) / 128.0f; }

static RecPad pad_of(const EmPadState *p)
{
    RecPad r = {p->buttons, {axis_byte(p->lx), axis_byte(p->ly), axis_byte(p->rx), axis_byte(p->ry)}};
    return r;
}

static void pad_set(EmPadState *p, const RecPad *r)
{
    p->buttons = r->btn;
    p->lx = axis_float(r->axis[0]);
    p->ly = axis_float(r->axis[1]);
    p->rx = axis_float(r->axis[2]);
    p->ry = axis_float(r->axis[3]);
}

/* ------------------------------------------------------------ the file */

static void *grow(void *p, uint32_t *cap, uint32_t need, size_t size)
{
    if (need <= *cap) return p;
    uint32_t n = *cap ? *cap * 2 : 1024;
    while (n < need) n *= 2;
    void *q = realloc(p, (size_t)n * size);
    if (!q) return NULL;
    *cap = n;
    return q;
}

static int load_recording(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "replay: cannot open %s\n", path);
        return -1;
    }
    char line[512];
    int magic = 0, cols = 0, movie_open = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') {
            if (!strncmp(line, "# " REC_MAGIC, strlen("# " REC_MAGIC))) magic = 1;
            if (!strncmp(line, "#cols ", 6)) cols = !strncmp(line + 6, REC_COLS, strlen(REC_COLS));
            continue;
        }
        unsigned step, counter, btn, lx, ly, rx, ry;
        int mv;
        char ph;
        if (sscanf(line, "%u %u %d %c %x %u %u %u %u", &step, &counter, &mv, &ph, &btn, &lx, &ly, &rx,
                   &ry) != 9 || lx > 255 || ly > 255 || rx > 255 || ry > 255 || btn > 0xFFFF) {
            fprintf(stderr, "replay: %s: unreadable row: %s", path, line);
            fclose(f);
            return -1;
        }
        RecPad pad = {(uint16_t)btn, {(uint8_t)lx, (uint8_t)ly, (uint8_t)rx, (uint8_t)ry}};
        if (mv) {
            if (!movie_open) {
                if (!(s.movie_skip = grow(s.movie_skip, &s.cap_movies, s.nmovies + 1, 1))) goto oom;
                s.movie_skip[s.nmovies++] = 0;
                movie_open = 1;
            }
            if (pad.btn & EM_PAD_START) s.movie_skip[s.nmovies - 1] = 1;
            continue;
        }
        movie_open = 0;
        if (!(s.ticks = grow(s.ticks, &s.cap_ticks, s.nticks + 1, sizeof *s.ticks))) goto oom;
        if (!s.nsegs || s.segs[s.nsegs - 1].phase != ph) {
            if (!(s.segs = grow(s.segs, &s.cap_segs, s.nsegs + 1, sizeof *s.segs))) goto oom;
            s.segs[s.nsegs++] = (RecSeg){ph, s.nticks, 0};
        }
        s.ticks[s.nticks++] = pad;
        s.segs[s.nsegs - 1].len++;
    }
    fclose(f);
    if (!magic || !cols || !s.nticks) {
        fprintf(stderr, "replay: %s is not an %s recording with columns \"%s\"\n", path, REC_MAGIC, REC_COLS);
        return -1;
    }
    printf("replay: %s: %u ticks in %u segments, %u movie(s)\n", path, s.nticks, s.nsegs, s.nmovies);
    return 0;
oom:
    fclose(f);
    fprintf(stderr, "replay: out of memory reading %s\n", path);
    return -1;
}

static void write_header(FILE *f)
{
    fprintf(f, "# " REC_MAGIC "\n");
    fprintf(f, "# source native-port\n");
    fprintf(f, "# mode %s\n", s.playing ? "playback-log" : "recording");
    fprintf(f, "# ps2_disc_drive_timing %d\n", em_settings()->ps2_disc_drive_timing);
    /* Launch switches that change the game's path: playback re-applies them. */
    static const char *const path_vars[] = {"EM_NEW_GAME", "EM_SKIP_STARTUP"};
    for (size_t i = 0; i < sizeof path_vars / sizeof path_vars[0]; ++i) {
        const char *v = getenv(path_vars[i]);
        if (v && v[0] && !strchr(v, ' ') && !strchr(v, '\n'))
            fprintf(f, "# env %s=%s\n", path_vars[i], v);
    }
    fprintf(f, "# tick_hz 59.94\n");
    if (s.wav) fprintf(f, "# audio_rate 48000\n");
    fprintf(f, "#cols " REC_COLS "\n");
}

/* ------------------------------------------------------- offline audio */

/* The pull at step n runs through field n, which every producer rendered
 * in this step's field hook: em_replay_audio.h. */
static uint64_t audio_target(uint64_t n) { return em_replay_audio_frames_through(n); }

static void le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { le16(p, (uint16_t)v); le16(p + 2, (uint16_t)(v >> 16)); }

static void wav_header(FILE *f, uint64_t frames)
{
    uint8_t h[44];
    uint32_t data = (uint32_t)(frames * 4u > 0xFFFFFFF0u ? 0xFFFFFFF0u : frames * 4u);
    memcpy(h, "RIFF", 4);
    le32(h + 4, 36 + data);
    memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16);
    le16(h + 20, 1);        /* PCM */
    le16(h + 22, 2);        /* stereo */
    le32(h + 24, 48000);
    le32(h + 28, 48000 * 4);
    le16(h + 32, 4);
    le16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    le32(h + 40, data);
    fwrite(h, 1, sizeof h, f);
}

static void audio_step(void)
{
    uint64_t want = audio_target(s.step) - s.audio_frames;
    while (want) {
        int n = want > 1024 ? 1024 : (int)want;
        em_bgm_render_offline(s.mix, n);
        for (int i = 0; i < 2 * n; ++i) {
            float v = s.mix[i] * 32767.0f;
            s.pcm[i] = (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : lrintf(v));
        }
        fwrite(s.pcm, 4, (size_t)n, s.wav);
        s.audio_frames += (uint64_t)n;
        want -= (uint64_t)n;
    }
}

static void finish(void)
{
    if (s.wav) {
        fflush(s.wav);
        if (fseek(s.wav, 0, SEEK_SET) == 0) wav_header(s.wav, s.audio_frames);
        fclose(s.wav);
        s.wav = NULL;
    }
    if (s.out) {
        fclose(s.out);
        s.out = NULL;
    }
}

/* --------------------------------------------------------- the filter */

/* The recorded segment a live phase change leads to: the next recorded
 * segment when its phase matches; else one up to 4 ahead when everything
 * between is short (at most SHORT_SEG ticks: phase flickers the live run
 * does not have); else -1: the change is a flicker of the live run, and
 * the current segment's timeline simply continues. */
enum { SHORT_SEG = 8 };

static int next_segment(char phase, int first)
{
    if (first) {
        for (uint32_t j = 0; j < s.nsegs && j < 4; ++j)
            if (s.segs[j].phase == phase) return (int)j;
        return -1;
    }
    for (uint32_t j = s.cur_seg + 1; j < s.nsegs && j <= s.cur_seg + 4; ++j) {
        if (s.segs[j].phase == phase) return (int)j;
        if (s.segs[j].len > SHORT_SEG) break;
    }
    return -1;
}

static void stop(const char *why)
{
    if (!s.done) {
        s.done = 1;
        printf("replay: %s at step %u (counter %u)\n", why, s.step, em_frame_counter());
        em_frame_request_quit();
    }
}

static void filter(EmPadState *pad, void *user)
{
    (void)user;
    EmSceneState *st = em_scene_state();
    const int movie = em_frame_movie_active() != 0;
    const char phase = live_phase(st);
    int cap = -1;
    const uint64_t af = s.audio_frames;
    if (s.wav) audio_step();

    if (movie) {
        if (!s.in_movie) s.movie_index++;
        s.in_movie = 1;
        if (s.playing) {
            int skip = s.movie_index > s.nmovies || s.movie_skip[s.movie_index - 1];
            RecPad r = {skip ? (uint16_t)EM_PAD_START : 0, {0x80, 0x80, 0x80, 0x80}};
            pad_set(pad, &r);
        }
    } else {
        s.in_movie = 0;
        if (s.playing && !s.done) {
            int j = -1;
            if (!s.prev_phase || phase != s.prev_phase)
                j = phase == s.segs[s.cur_seg].phase && s.prev_phase ? -1 : next_segment(phase, !s.prev_phase);
            if (!s.prev_phase && j < 0) {
                fprintf(stderr, "replay: desync: the run starts in phase %c, the recording does not\n", phase);
                stop("desync");
                j = 0;
            }
            if (j >= 0) {
                if (s.prev_phase && (uint32_t)j > s.cur_seg + 1)
                    printf("replay: skipped short recorded segment(s) %u..%u at step %u\n", s.cur_seg + 1,
                           (unsigned)j - 1, s.step);
                s.cur_seg = (uint32_t)j;
                s.off = 0;
            } else if (s.prev_phase) {
                s.off++;
            }
            const RecSeg *g0 = &s.segs[s.cur_seg];
            const uint32_t i = s.off < g0->len ? s.off : g0->len - 1;
            pad_set(pad, &s.ticks[g0->start + i]);
            if ((long)s.off - (long)g0->len > s.max_overrun) stop("desync: segment overran the recording");
            if (s.cur_seg + 1 == s.nsegs && s.off + 1 >= g0->len) stop("recording used up");
            if (s.capture && phase != 'T' && s.off % s.capture_every == 0) {
                char path[1024];
                if (strstr(s.capture, "%u"))
                    snprintf(path, sizeof path, s.capture, s.captures);
                else
                    snprintf(path, sizeof path, "%s", s.capture);
                em_gfx_request_capture(em_frame_gfx(), path);
                cap = (int)s.captures++;
            }
        } else if (!s.playing) {
            if (!s.prev_phase || phase != s.prev_phase) {
                s.cur_seg = s.prev_phase ? s.cur_seg + 1 : 0;
                s.off = 0;
            } else {
                s.off++;
            }
        }
        s.prev_phase = phase;
    }

    if (s.out) {
        const RecPad r = pad_of(pad);
        const EmTask *t0 = em_task_slot(0);
        fprintf(s.out, "%u %u %d %c 0x%04x %u %u %u %u %u %u %u %u %u %u %u %.9g %.9g %.9g %.9g %d %d %d %lld\n",
                s.step, em_frame_counter(), movie, movie ? 'M' : phase, r.btn, r.axis[0], r.axis[1], r.axis[2],
                r.axis[3], st->d810700, st->d810701, st->d810702, st->d275BD8, st->spad3B8D,
                t0 ? t0->user[1] : 0, t0 ? t0->user[3] : 0, (double)g.pos[0], (double)g.pos[1],
                (double)g.pos[2], (double)g.yaw, movie ? -1 : (int)s.cur_seg, movie ? -1 : (int)s.off, cap,
                s.wav ? (long long)af : -1LL);
    }
    s.step++;
}

int em_replay_install(void)
{
    const char *rec = getenv("EM_INPUT_RECORD"), *play = getenv("EM_INPUT_PLAY");
    const char *audio = getenv("EM_REPLAY_AUDIO");
    if ((!rec || !rec[0]) && (!play || !play[0])) return 0;
    memset(&s, 0, sizeof s);
    s.capture_every = 1;
    s.max_overrun = 1800;
    if (play && play[0]) {
        if (load_recording(play) != 0) return -1;
        s.playing = 1;
        const char *cap = getenv("EM_REPLAY_CAPTURE");
        const char *every = getenv("EM_REPLAY_CAPTURE_EVERY");
        const char *over = getenv("EM_REPLAY_MAX_OVERRUN");
        s.capture = cap && cap[0] ? cap : NULL;
        if (every && atoi(every) > 0) s.capture_every = (uint32_t)atoi(every);
        if (over && atol(over) >= 0) s.max_overrun = atol(over);
        if (audio && audio[0]) {
            s.wav = fopen(audio, "wb");
            s.mix = malloc(sizeof(float) * 2 * 1024);
            s.pcm = malloc(sizeof(int16_t) * 2 * 1024);
            if (!s.wav || !s.mix || !s.pcm) {
                fprintf(stderr, "replay: cannot write %s\n", audio);
                return -1;
            }
            wav_header(s.wav, 0);
            em_bgm_set_offline();
        }
    }
    if (rec && rec[0]) {
        s.out = fopen(rec, "w");
        if (!s.out) {
            fprintf(stderr, "replay: cannot write %s\n", rec);
            return -1;
        }
        setvbuf(s.out, NULL, _IOLBF, 1 << 16); /* a crash keeps every finished row */
        write_header(s.out);
    }
    atexit(finish);
    em_input_set_filter(filter, NULL);
    s.installed = 1;
    printf("replay: %s%s%s\n", s.playing ? "playing " : "", s.playing ? play : "",
           s.out ? (s.playing ? " (log written)" : "recording") : "");
    return 0;
}
