/* main.c — entry point for the Extermination native port shell.
 *
 * Cross-platform: talks only to em_platform.h + em_gfx.h. No OS or GPU API
 * appears here.
 *
 * Current milestone: render the PLAYER CHARACTER through the translated PS2
 * skinning pipeline — per-bone object-space vertices (decoded from the
 * user's own disc dump by the decomp repo's tools/export_native.py into
 * assets/player.emdl) posed by a baked bone-matrix palette and drawn with a
 * runtime-compiled shader. If no asset file is present the shell falls back
 * to the gradient test triangle, so the repo still runs standalone.
 */
#include "em_platform.h"
#include "em_gfx.h"
#include "em_math.h"
#include "em_model.h"
#include "em_audio.h"
#include "em_input.h"

#include <dirent.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODEL_PATH "assets/player.emdl"

/* Scene: a small list of static EMDL parts (level geometry exported in
 * WORLD space by the decomp repo's tools/export_level.py) loaded from
 * assets/scene EMDL files in alphabetical order. When present, the player is
 * placed at its known world position in the room and the camera orbits
 * there; without a scene the original single-model behavior is kept. */
#define SCENE_DIR   "assets/scene"
#define SCENE_MAX   16

/* Player world placement in the office room (chunk06.n1 level): the live
 * GS-dump capture has the character standing at ~(107.4, 0, -184); the
 * level floor there is y = 0 and the player EMDL is recentred at the
 * origin with its feet at y ~= 0. */
static const float kPlayerPos[3] = { 107.4f, 0.0f, -184.0f };

typedef struct {
    EmModel    model;
    EmGfxMesh *mesh;
} SceneItem;

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Load assets/scene EMDL files (alphabetical). Returns the number loaded. */
static int scene_load(EmGfx *gfx, SceneItem *items, int max_items)
{
    DIR *dir = opendir(SCENE_DIR);
    if (!dir) return 0;

    char *names[SCENE_MAX];
    int   n_names = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL && n_names < max_items) {
        const char *dot = strrchr(de->d_name, '.');
        if (!dot || strcmp(dot, ".emdl") != 0) continue;
        names[n_names] = malloc(strlen(de->d_name) + 1);
        if (!names[n_names]) break;
        strcpy(names[n_names], de->d_name);
        n_names++;
    }
    closedir(dir);
    qsort(names, n_names, sizeof(names[0]), cmp_str);

    int n = 0;
    for (int i = 0; i < n_names; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", SCENE_DIR, names[i]);
        free(names[i]);
        SceneItem *it = &items[n];
        if (em_model_load(&it->model, path) != 0) continue;
        it->mesh = em_gfx_mesh_create(gfx, it->model.verts,
                                      it->model.vert_count, it->model.indices,
                                      it->model.index_count,
                                      (const EmGfxTexDesc *)it->model.texs,
                                      it->model.tex_count, it->model.texels,
                                      it->model.flags);
        if (!it->mesh) {
            em_model_free(&it->model);
            continue;
        }
        printf("scene: %s — %u verts, %u tris, %u textures\n", path,
               it->model.vert_count, it->model.index_count / 3,
               it->model.tex_count);
        n++;
    }
    return n;
}

/* Audio smoke test (EM_AUDIO_TEST=1): synthesize a quiet 440 Hz sine from the
 * audio thread and count delivered frames. Per the em_audio.h contract the
 * callback runs on the OS audio thread, so the counter the main thread reads
 * at exit is atomic; the phase is touched only by the audio thread. */
#define AUDIO_TEST_RATE 48000

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

/* Pad debug print (EM_INPUT_TEST=1): one compact line per state change. */
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

int main(void)
{
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

    /* Optional character asset (disc-derived, generated locally). */
    EmModel    model;
    EmGfxMesh *mesh = NULL;
    float      palette[1024 * 16]; /* bone_count <= 1024 enforced by loader */
    if (em_model_load(&model, MODEL_PATH) == 0) {
        mesh = em_gfx_mesh_create(gfx, model.verts, model.vert_count,
                                  model.indices, model.index_count,
                                  (const EmGfxTexDesc *)model.texs,
                                  model.tex_count, model.texels, model.flags);
        printf("loaded %s: %u bones, %u verts, %u tris, %u frames @ %.0f fps, "
               "%u textures\n",
               MODEL_PATH, model.bone_count, model.vert_count,
               model.index_count / 3, model.frame_count, model.fps,
               model.tex_count);
    } else {
        printf("no %s — showing the test triangle. Generate it with the "
               "decomp repo's tools/export_native.py\n", MODEL_PATH);
    }

    /* Optional scene (level parts, world-space). */
    SceneItem scene[SCENE_MAX];
    int n_scene = scene_load(gfx, scene, SCENE_MAX);

    /* Headless verification: EM_CAPTURE=<path.bmp> renders ~1s, captures a
     * frame to the given BMP, and exits. Used for screenshot regression. */
    const char *capture_path = getenv("EM_CAPTURE");
    int frame_no = 0;

    /* Audio smoke test: EM_AUDIO_TEST=1 opens the output device with the
     * sine callback above. When unset, no audio object exists at all. */
    AudioTest audio_test = { 0.0, 0 };
    EmAudio  *audio = NULL;
    const char *audio_env = getenv("EM_AUDIO_TEST");
    if (audio_env && audio_env[0] == '1') {
        audio = em_audio_create(AUDIO_TEST_RATE, audio_test_cb, &audio_test);
        if (!audio)
            fprintf(stderr, "warning: EM_AUDIO_TEST set but audio device "
                            "creation failed\n");
    }

    /* Pad model: always fed (cheap, side-effect-free); printed per change
     * only when EM_INPUT_TEST=1 so default output is untouched. */
    em_input_init();
    const char *input_env = getenv("EM_INPUT_TEST");
    bool input_test = input_env && input_env[0] == '1';
    EmPadState prev_pad;
    em_input_pad(&prev_pad);

    bool running = true;
    double t = 0.0;
    while (running) {
        EmEvent ev;
        while (em_window_poll(win, &ev)) {
            em_input_handle_event(&ev);
            switch (ev.type) {
                case EM_EVENT_QUIT:
                    running = false;
                    break;
                case EM_EVENT_KEY_DOWN:
                    if (ev.key == EM_KEY_ESCAPE) running = false;
                    break;
                default:
                    break;
            }
        }

        if (input_test) {
            EmPadState pad;
            em_input_pad(&pad);
            if (pad_changed(&pad, &prev_pad)) {
                input_test_print(&pad);
                prev_pad = pad;
            }
        }

        em_gfx_begin_frame(gfx, 0.08f, 0.09f, 0.12f, 1.0f);

        if (mesh || n_scene) {
            /* Slow orbit camera. Without a scene it circles the character
             * at the origin (the original single-model framing); with one
             * it circles the character's world position inside the room. */
            float ang = (float)(t * 0.5);
            float cx = 0.0f, cy = 7.0f, cz = 0.0f;
            float radius = 28.0f, eye_h = 12.0f, far_clip = 500.0f;
            if (n_scene) {
                /* The character stands near the room's +X wall; phase the
                 * orbit so the camera starts inside the open part of the
                 * room (towards -X/+Z) instead of inside that wall. */
                ang += 4.82f;
                cx = kPlayerPos[0];
                cy = kPlayerPos[1] + 7.0f;
                cz = kPlayerPos[2];
                radius = 20.0f;
                eye_h  = cy + 8.0f;
                far_clip = 800.0f;
            }
            float eye[3]    = { cx + radius * sinf(ang), eye_h,
                                cz + radius * cosf(ang) };
            float center[3] = { cx, cy, cz };
            float up[3]     = { 0.0f, 1.0f, 0.0f };

            int dw, dh;
            em_window_drawable_size(win, &dw, &dh);
            float aspect = (dh > 0) ? (float)dw / (float)dh : 4.0f / 3.0f;

            float view[16], proj[16], viewproj[16];
            em_mat4_lookat(view, eye, center, up);
            em_mat4_perspective(proj, 50.0f * 3.14159265f / 180.0f, aspect,
                                0.5f, far_clip);
            em_mat4_mul(viewproj, proj, view);

            for (int i = 0; i < n_scene; i++) {
                em_model_palette_at(&scene[i].model, 0.0, palette);
                em_gfx_draw_skinned(gfx, scene[i].mesh, viewproj, palette,
                                    scene[i].model.bone_count);
            }
            if (mesh) {
                em_model_palette_at(&model, t * model.fps, palette);
                if (n_scene) {
                    /* Place the recentred character at its world spot by
                     * offsetting every palette matrix translation. */
                    for (uint32_t b = 0; b < model.bone_count; b++) {
                        palette[b * 16 + 12] += kPlayerPos[0];
                        palette[b * 16 + 13] += kPlayerPos[1];
                        palette[b * 16 + 14] += kPlayerPos[2];
                    }
                }
                em_gfx_draw_skinned(gfx, mesh, viewproj, palette,
                                    model.bone_count);
            }
        } else {
            em_gfx_draw_test_triangle(gfx);
        }

        if (capture_path && frame_no == 60)
            em_gfx_request_capture(gfx, capture_path);

        em_gfx_end_frame(gfx);
        t += 1.0 / 60.0;
        if (capture_path && ++frame_no > 61) running = false;
    }

    if (audio) {
        em_audio_destroy(audio); /* blocks: no callback after this */
        printf("audio test: %ld frames delivered\n",
               atomic_load_explicit(&audio_test.frames,
                                    memory_order_relaxed));
    }
    if (mesh) {
        em_gfx_mesh_destroy(gfx, mesh);
        em_model_free(&model);
    }
    for (int i = 0; i < n_scene; i++) {
        em_gfx_mesh_destroy(gfx, scene[i].mesh);
        em_model_free(&scene[i].model);
    }
    em_gfx_destroy(gfx);
    em_window_destroy(win);
    return 0;
}
