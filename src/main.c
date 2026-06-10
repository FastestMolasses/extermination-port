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

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define MODEL_PATH "assets/player.emdl"

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
                                  model.tex_count, model.texels);
        printf("loaded %s: %u bones, %u verts, %u tris, %u frames @ %.0f fps, "
               "%u textures\n",
               MODEL_PATH, model.bone_count, model.vert_count,
               model.index_count / 3, model.frame_count, model.fps,
               model.tex_count);
    } else {
        printf("no %s — showing the test triangle. Generate it with the "
               "decomp repo's tools/export_native.py\n", MODEL_PATH);
    }

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

        if (mesh) {
            /* Slow orbit camera around the character (game units: the
             * player stands ~15 units tall, recentred at the origin). */
            float ang = (float)(t * 0.5);
            float eye[3]    = { 28.0f * sinf(ang), 12.0f, 28.0f * cosf(ang) };
            float center[3] = { 0.0f, 7.0f, 0.0f };
            float up[3]     = { 0.0f, 1.0f, 0.0f };

            int dw, dh;
            em_window_drawable_size(win, &dw, &dh);
            float aspect = (dh > 0) ? (float)dw / (float)dh : 4.0f / 3.0f;

            float view[16], proj[16], viewproj[16];
            em_mat4_lookat(view, eye, center, up);
            em_mat4_perspective(proj, 50.0f * 3.14159265f / 180.0f, aspect,
                                0.5f, 500.0f);
            em_mat4_mul(viewproj, proj, view);

            em_model_palette_at(&model, t * model.fps, palette);
            em_gfx_draw_skinned(gfx, mesh, viewproj, palette,
                                model.bone_count);
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
    em_gfx_destroy(gfx);
    em_window_destroy(win);
    return 0;
}
