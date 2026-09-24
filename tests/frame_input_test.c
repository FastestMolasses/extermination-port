/* Step-C wiring of em_frame: native pad -> libpad buffer -> em_pad_unpack
 * (001B5940, instruction-checked by tools/test_input_block_reference.py)
 * -> canonical EmFrameInput and the original-layout block. Also pins the
 * frame clear colour to 001AB370's black. No window, GPU or assets.
 * Build: cc -Isrc tests/frame_input_test.c src/game/em_frame.c
 *        src/game/em_fade.c src/game/em_task.c src/em_input.c */
#include "game/em_frame.h"
#include "game/em_task.h"
#include "em_input.h"

#include <assert.h>
#include <stdio.h>

static float clear_rgba[4] = {-1, -1, -1, -1};

bool em_window_poll(EmWindow *window, EmEvent *out)
{
    (void)window; (void)out;
    return false;
}
void em_gamepad_poll(void) {}
void em_bgm_service(void) {}
void em_gfx_begin_frame(EmGfx *gfx, float r, float g, float b, float a)
{
    (void)gfx;
    clear_rgba[0] = r; clear_rgba[1] = g; clear_rgba[2] = b; clear_rgba[3] = a;
}
void em_gfx_end_frame(EmGfx *gfx) { (void)gfx; }
void em_gfx_overlay_canvas(EmGfx *gfx, float w, float h) { (void)gfx; (void)w; (void)h; }
static void rect(EmGfx *gfx, float x, float y, float w, float h, const float rgb[3])
{ (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)rgb; }
void em_gfx_overlay_rect_sub(EmGfx *gfx, float x, float y, float w, float h,
                             const float rgb[3]) { rect(gfx, x, y, w, h, rgb); }
void em_gfx_overlay_rect_sub_before_text(EmGfx *gfx, float x, float y,
                                        float w, float h, const float rgb[3])
{ rect(gfx, x, y, w, h, rgb); }
void em_gfx_overlay_rect_add(EmGfx *gfx, float x, float y, float w, float h,
                             const float rgb[3]) { rect(gfx, x, y, w, h, rgb); }

static const EmFrameInput *step(uint16_t buttons, float lx, float ly)
{
    EmPadState pad = { buttons, lx, ly, 0.0f, 0.0f };
    em_input_set_gamepad(&pad);
    assert(em_frame_step());
    return em_frame_input();
}

int main(void)
{
    em_frame_init(NULL, NULL);
    const EmFrameInput *in = step(0, 0.0f, 0.0f);
    assert(clear_rgba[0] == 0 && clear_rgba[1] == 0 && clear_rgba[2] == 0 &&
           clear_rgba[3] == 1);
    assert(in->lx == 0x80 && in->ly == 0x80 && in->held == 0 &&
           em_frame_pad_block()->gait == 0);

    /* D-pad UP with a centred stick: 001B5E20 stick bytes, gait 3. */
    in = step(EM_PAD_UP | EM_PAD_START, 0.0f, 0.0f);
    assert(in->lx == 0x80 && in->ly == 0x00);
    assert(in->held == (EM_PAD_UP | EM_PAD_START) && in->pressed == in->held);
    assert(em_frame_pad_block()->held == 0x1800 && em_frame_pad_block()->gait == 3);
    /* S11a: the scene coordinator's D_00810E74/E70/E50 are the same words,
     * unswapped (START 0x0800, UP 0x1000), and E50 is 001B5F40's 4. */
    EmSceneState scene = {0};
    em_frame_scene_input(&scene);
    assert(scene.d810E74 == 0x1800 && scene.d810E70 == 0x1800 && scene.d810E50 == 4);

    /* Full right stick replaces the held D-pad bits (001B5D70) and is
     * quantized (001B5C90); the gait comes from the raw bytes. */
    in = step(EM_PAD_UP, 1.0f, 0.0f);
    assert(in->held == EM_PAD_RIGHT && in->pressed == EM_PAD_RIGHT);
    assert(in->released == (EM_PAD_UP | EM_PAD_START));
    assert(in->lx == 0xFC && in->ly == 0x80 && em_frame_pad_block()->gait == 3);

    /* Walk-band stick: no D-pad bits, quantized bytes. */
    in = step(0, -0.5f, 0.0f);
    assert(in->held == 0 && in->lx == 0x40 && em_frame_pad_block()->gait == 1);

    /* Held D-pad DOWN auto-repeat word 0x810E78: edge, 31 quiet frames,
     * the 32nd repeats, then every 10th. */
    unsigned repeats = 0;
    for (int frame = 0; frame < 52; ++frame) {
        step(EM_PAD_DOWN, 0.0f, 0.0f);
        if (em_frame_pad_block()->repeat & 0x4000) {
            assert(frame == 0 || frame == 32 || frame == 42);
            ++repeats;
        }
    }
    assert(repeats == 3);
    em_input_set_gamepad(NULL);
    printf("frame_input_test: PASS (001B5940 wiring, canonical view, repeat, clear)\n");
    return 0;
}
