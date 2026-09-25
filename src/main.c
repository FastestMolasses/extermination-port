/* main.c — entry point for the Extermination native port.
 *
 * Cross-platform: talks only to the em_* contracts. No OS or GPU API, and
 * no game logic, appears here — exactly like the original's crt0, main()
 * only brings the machine up and enters the engine frame loop:
 *
 *   platform/gfx/audio/input init  ->  em_frame_init()
 *   register the boot task         ->  em_game_install()
 *   run frames forever (steps A..W) -> em_frame_run()
 *
 * The PS2 main (func_001AAE40) never returns; natively em_frame_run()
 * returns on quit so the teardown below can run.
 */
#include "em_audio.h"
#include "em_gfx.h"
#include "em_platform.h"
#include "game/em_frame.h"
#include "game/em_game.h"
#include "game/em_frontend.h"
#include "game/em_message_live.h"
#include "game/em_scene_bindings.h"
#include "game/em_startup_audio.h"
#include "game/em_stream_live.h"
#include "game/em_opening_runtime.h"
#include "game/em_level_smoke_test.h"
#include "game/em_opening_control_test.h"
#include "game/em_pad_actuator.h"

#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* EM_SCENE=<dir> — switch the loaded level scene without touching the game
 * task code. The game code's asset paths are compile-time relative
 * constants ("assets/scene", "assets/scene/office.emcl",
 * "assets/player.emdl"), so the switch happens at the filesystem level:
 * stage a shadow tree in a temp directory — a symlink for every entry of
 * ./assets plus the requested directory linked AS "scene" — and chdir into
 * it before anything opens a file. The scene directory supplies its level
 * parts as *.emdl and its collision world as "office.emcl" (the loader's
 * compile-time name; a per-scene name needs a src/game change).
 *
 * Default behavior is BYTE-IDENTICAL: with EM_SCENE unset (or naming the
 * default directory) this function does nothing at all.
 *
 * Relative paths in EM_CAPTURE / EM_AUDIO_FILE are absolutized
 * against the original cwd first, so their files keep landing where the
 * caller expects despite the chdir. POSIX-only (mac/linux); the Windows
 * backend does not exist yet — revisit alongside it. */
static void env_make_absolute(const char *name, const char *cwd)
{
    const char *v = getenv(name);
    if (!v || !v[0] || v[0] == '/')
        return;
    char abs[PATH_MAX];
    snprintf(abs, sizeof abs, "%s/%s", cwd, v);
    setenv(name, abs, 1);
}

static void scene_redirect(void)
{
    const char *scene = getenv("EM_SCENE");
    if (!scene || !scene[0])
        return;                                  /* default — no-op */

    char cwd[PATH_MAX], scene_abs[PATH_MAX], def_abs[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd))
        return;
    if (!realpath(scene, scene_abs)) {
        fprintf(stderr, "scene: EM_SCENE=%s does not resolve — keeping the "
                        "default scene\n", scene);
        return;
    }
    if (realpath("assets/scene", def_abs) && strcmp(def_abs, scene_abs) == 0)
        return;                                  /* the default dir — no-op */

    env_make_absolute("EM_CAPTURE", cwd);
    env_make_absolute("EM_AUDIO_FILE", cwd);
    env_make_absolute("EM_STARTUP_CAPTURE_DIR", cwd);

    char stage_tmpl[] = "/tmp/em_scene_XXXXXX";
    char *stage = mkdtemp(stage_tmpl);
    char sub[PATH_MAX];
    if (!stage) {
        fprintf(stderr, "scene: mkdtemp failed — keeping the default "
                        "scene\n");
        return;
    }
    snprintf(sub, sizeof sub, "%s/assets", stage);
    if (mkdir(sub, 0700) != 0) {
        fprintf(stderr, "scene: cannot stage %s — keeping the default "
                        "scene\n", sub);
        return;
    }

    DIR *dir = opendir("assets");                /* may not exist: fine */
    if (dir) {
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            if (de->d_name[0] == '.' || strcmp(de->d_name, "scene") == 0)
                continue;
            char from[PATH_MAX], to[PATH_MAX];
            snprintf(from, sizeof from, "%s/assets/%s", cwd, de->d_name);
            snprintf(to, sizeof to, "%s/%s", sub, de->d_name);
            (void)symlink(from, to);
        }
        closedir(dir);
    }
    char link[PATH_MAX];
    snprintf(link, sizeof link, "%s/scene", sub);
    if (symlink(scene_abs, link) != 0 || chdir(stage) != 0) {
        fprintf(stderr, "scene: staging failed — keeping the default "
                        "scene\n");
        return;
    }
    printf("scene: EM_SCENE=%s (staged at %s)\n", scene_abs, stage);
}

/* Audio smoke test (EM_AUDIO_TEST=1): synthesize a quiet 440 Hz sine from the
 * audio thread and count delivered frames. Per the em_audio.h contract the
 * callback runs on the OS audio thread, so the counter the main thread reads
 * at exit is atomic; the phase is touched only by the audio thread. */
#define AUDIO_TEST_RATE 48000

#include <stdint.h>

typedef struct {
    double      phase;   /* audio thread only */
    atomic_long frames;  /* written by audio thread, read at exit */
} AudioTest;

static void audio_test_cb(void *user, float *out, int frames)
{
    AudioTest *at = user;
    const double step =
        2.0 * 3.14159265358979323846 * 440.0 / (double)AUDIO_TEST_RATE;
    for (int i = 0; i < frames; i++) {
        float s = 0.1f * (float)sin(at->phase);
        at->phase += step;
        if (at->phase > 2.0 * 3.14159265358979323846)
            at->phase -= 2.0 * 3.14159265358979323846;
        out[i * 2 + 0] = s;
        out[i * 2 + 1] = s;
    }
    atomic_fetch_add_explicit(&at->frames, (long)frames,
                              memory_order_relaxed);
}

/* WAV playback test (EM_AUDIO_TEST=2, EM_AUDIO_FILE=path.wav): stream a
 * PCM16 WAV (mono or stereo, any rate) through em_audio — the first real
 * audio data down the pull path (decoded PS2 streams/SFX from the decomp
 * repo's tools/audio_export.py). The whole file is loaded up front so the
 * audio thread only reads memory (real-time safe). When the file has fully
 * played (plus a short tail) the callback requests the frame loop to quit —
 * a one-way bool store polled once per frame, same as the ESC path — so the
 * mode is scriptable: run, play, exit, print frame counts. */
typedef struct {
    const int16_t *pcm;       /* interleaved s16, audio thread reads only  */
    long           nframes;   /* total WAV frames                          */
    int            channels;  /* 1 or 2                                    */
    int            rate;      /* WAV sample rate (device opens at this)    */
    long           pos;       /* audio thread only                         */
    int            tail;      /* audio thread only: silent frames appended */
    atomic_long    delivered; /* device frames delivered, read at exit     */
    atomic_long    played;    /* WAV frames consumed, read at exit         */
} AudioWavTest;

static void audio_wav_cb(void *user, float *out, int frames)
{
    AudioWavTest *wt = user;
    int written = 0;
    while (written < frames && wt->pos < wt->nframes) {
        const int16_t *src = wt->pcm + wt->pos * wt->channels;
        float l = (float)src[0] / 32768.0f;
        float r = (wt->channels == 2) ? (float)src[1] / 32768.0f : l;
        out[written * 2 + 0] = l;
        out[written * 2 + 1] = r;
        written++;
        wt->pos++;
    }
    atomic_fetch_add_explicit(&wt->played, (long)written,
                              memory_order_relaxed);
    for (int i = written; i < frames; i++) {
        out[i * 2 + 0] = 0.0f;
        out[i * 2 + 1] = 0.0f;
    }
    if (wt->pos >= wt->nframes) {
        wt->tail += frames - written;
        if (wt->tail >= wt->rate / 4)   /* ~250 ms of silence, then quit */
            em_frame_request_quit();
    }
    atomic_fetch_add_explicit(&wt->delivered, (long)frames,
                              memory_order_relaxed);
}

/* Minimal RIFF/WAVE reader: PCM16 only, mono/stereo, zero dependencies.
 * Returns 0 on success and fills *wt (pcm is malloc'd, caller frees).
 * Little-endian host assumed (every port target is). */
static int wav_load_pcm16(const char *path, AudioWavTest *wt)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "wav: cannot open %s\n", path);
        return -1;
    }
    unsigned char hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) != 0 ||
        memcmp(hdr + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "wav: %s is not a RIFF/WAVE file\n", path);
        fclose(f);
        return -1;
    }
    uint16_t fmt = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    int16_t *pcm = NULL;
    uint32_t data_size = 0;
    unsigned char ch[8];
    while (fread(ch, 1, 8, f) == 8) {
        uint32_t size = (uint32_t)ch[4] | (uint32_t)ch[5] << 8 |
                        (uint32_t)ch[6] << 16 | (uint32_t)ch[7] << 24;
        if (memcmp(ch, "fmt ", 4) == 0 && size >= 16) {
            unsigned char fc[16];
            if (fread(fc, 1, 16, f) != 16)
                break;
            fmt      = (uint16_t)(fc[0] | fc[1] << 8);
            channels = (uint16_t)(fc[2] | fc[3] << 8);
            rate     = (uint32_t)fc[4] | (uint32_t)fc[5] << 8 |
                       (uint32_t)fc[6] << 16 | (uint32_t)fc[7] << 24;
            bits     = (uint16_t)(fc[14] | fc[15] << 8);
            if (fseek(f, (long)(size - 16 + (size & 1)), SEEK_CUR) != 0)
                break;
        } else if (memcmp(ch, "data", 4) == 0) {
            pcm = malloc(size);
            if (!pcm || fread(pcm, 1, size, f) != size) {
                free(pcm);
                pcm = NULL;
                break;
            }
            data_size = size;
            break;                      /* fmt always precedes data */
        } else {
            if (fseek(f, (long)(size + (size & 1)), SEEK_CUR) != 0)
                break;
        }
    }
    fclose(f);
    if (fmt != 1 || bits != 16 || (channels != 1 && channels != 2) ||
        rate == 0 || !pcm) {
        fprintf(stderr, "wav: %s unsupported (need PCM16 mono/stereo; "
                        "got fmt=%u bits=%u ch=%u rate=%u data=%u)\n",
                path, fmt, bits, channels, rate, data_size);
        free(pcm);
        return -1;
    }
    wt->pcm      = pcm;
    wt->nframes  = (long)(data_size / (channels * 2u));
    wt->channels = channels;
    wt->rate     = (int)rate;
    return 0;
}

/* The stream lanes' hooks for the frame loop and the message service
 * (em_stream_live; the message workers return 1 ok, 0 fault). */
static int stream_field(void *context) { (void)context; return em_stream_live_field(); }
static int stream_step_h(void *context) { (void)context; return em_stream_live_step_h(); }
static int stream_voice_push(void *context, int32_t cue)
{ (void)context; return em_stream_live_001FA5A0(cue) == 0; }
static int stream_stop_lane(void *context, int lane)
{ (void)context; return em_stream_live_001FAAC0(lane) == 0; }
static int8_t stream_active(void *context, int lane) { (void)context; return em_stream_live_active(lane); }

int main(void)
{
    /* Whole lines on stdout: the test drivers read stdout and stderr
     * through one pipe, and a block-buffered flush in mid-line would split
     * a stderr report line. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    scene_redirect();   /* EM_SCENE — must precede every file open */

    EmWindow *win = em_window_create("Extermination (native port)", 960, 720);
    if (!win) {
        fprintf(stderr, "fatal: could not create window\n");
        return 1;
    }
    EmGfx *gfx = em_gfx_create(win);
    if (!gfx) {
        fprintf(stderr, "fatal: could not create graphics device\n");
        em_window_destroy(win);
        return 1;
    }

    /* Audio tests: EM_AUDIO_TEST=1 opens the output device with the sine
     * callback above; EM_AUDIO_TEST=2 streams the PCM16 WAV named by
     * EM_AUDIO_FILE and quits when it has played. When unset, no audio
     * object exists at all. */
    AudioTest    audio_test = { 0.0, 0 };
    AudioWavTest audio_wav  = { NULL, 0, 0, 0, 0, 0, 0, 0 };
    EmAudio  *audio = NULL;
    int       audio_mode = 0;
    const char *audio_env = getenv("EM_AUDIO_TEST");
    if (audio_env && audio_env[0] == '1') {
        audio_mode = 1;
        audio = em_audio_create(AUDIO_TEST_RATE, audio_test_cb, &audio_test);
        if (!audio)
            fprintf(stderr, "warning: EM_AUDIO_TEST set but audio device "
                            "creation failed\n");
    } else if (audio_env && audio_env[0] == '2') {
        const char *wav_path = getenv("EM_AUDIO_FILE");
        if (!wav_path) {
            fprintf(stderr, "warning: EM_AUDIO_TEST=2 needs EM_AUDIO_FILE="
                            "path.wav\n");
        } else if (wav_load_pcm16(wav_path, &audio_wav) == 0) {
            audio_mode = 2;
            printf("audio test 2: %s: %ld frames @ %d Hz, %d ch (%.1f s)\n",
                   wav_path, audio_wav.nframes, audio_wav.rate,
                   audio_wav.channels,
                   (double)audio_wav.nframes / audio_wav.rate);
            audio = em_audio_create(audio_wav.rate, audio_wav_cb,
                                    &audio_wav);
            if (!audio)
                fprintf(stderr, "warning: EM_AUDIO_TEST=2 set but audio "
                                "device creation failed\n");
        }
    }

    /* Engine bring-up: frame loop env + input + task table, then the boot
     * task into slot 0 (the engine init's func_001AB740(0, boot)). */
    em_frame_init(win, gfx);
    /* 001AAE40's start-up: the SNDN2DRV.IRX bring-up's IOP buffers and the
     * stream files' sectors, then 001F9820 (the stream lanes' initial
     * state; em_stream_live, WP-8b). Without the stream export the game
     * cannot start (fail-stop, never silence). The field (D_00810E90 and
     * the IOP's field work) runs at the top of every frame, the lane
     * service 001F9CF0 at step H. */
    if (em_stream_live_boot("assets/streams/streams.emst") != 0) {
        em_stream_live_shutdown();
        em_gfx_destroy(gfx);
        em_window_destroy(win);
        return 1;
    }
    static const EmFrameSoundService sound = {stream_field, stream_step_h, NULL};
    em_frame_set_sound_service(&sound);
    /* Main-loop step F: the message service 001FCA10 (WP-8), with the
     * stream workers 001FD470 / 001FA790 / 001FA5A0 / 001FAAC0 and the
     * voice lanes' active bytes its records reach. */
    em_message_live_install("assets/message/message_data.emmd");
    static const EmMessageLiveStreams streams = {NULL, em_scene_bindings_001FD470,
                                                 em_scene_bindings_001FA790, stream_voice_push,
                                                 stream_stop_lane, stream_active};
    em_message_live_set_streams(&streams);
    /* Main-loop step I: 001B5B70, the rumble countdown over the pad block
     * D_00810E40 (em_pad_actuator). */
    em_pad_actuator_reset();
    em_frame_set_step_i(em_pad_actuator_step_i, NULL);
    /* No seed here: the original main never seeds the SDK RNG; its state
     * starts at the ELF's initialized value 1 (em_random.c). */
    const char *skip_startup = getenv("EM_SKIP_STARTUP");
    if (skip_startup && strcmp(skip_startup, "1") == 0)
        em_game_install();  /* explicit gameplay/debug fixture */
    else
        em_frontend_install();

    em_frame_run();

    em_frontend_shutdown();
    em_game_shutdown();         /* em_bgm_shutdown: the device no longer mixes */
    em_frame_set_sound_service(NULL);
    int stream_failed = em_stream_live_failed();
    em_stream_live_shutdown();
    int message_failed = em_message_live_fault() != NULL;
    em_message_live_shutdown();
    em_startup_audio_shutdown();  /* shared device has stopped its callback */
    if (audio) {
        em_audio_destroy(audio); /* blocks: no callback after this */
        if (audio_mode == 1)
            printf("audio test: %ld frames delivered\n",
                   atomic_load_explicit(&audio_test.frames,
                                        memory_order_relaxed));
        else if (audio_mode == 2)
            printf("audio test 2: %ld/%ld wav frames played, "
                   "%ld device frames delivered\n",
                   atomic_load_explicit(&audio_wav.played,
                                        memory_order_relaxed),
                   audio_wav.nframes,
                   atomic_load_explicit(&audio_wav.delivered,
                                        memory_order_relaxed));
    }
    free((void *)audio_wav.pcm);
    em_gfx_destroy(gfx);
    em_window_destroy(win);
    return em_frontend_failed() || em_opening_runtime_failed() || message_failed || stream_failed ||
           em_opening_control_test_failed() || em_level_smoke_test_failed() ? 1 : 0;
}
