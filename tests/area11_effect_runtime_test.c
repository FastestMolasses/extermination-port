#include <math.h>
#include "game/em_area11_effect_runtime.h"
#include "game/em_random.h"
#include "game/em_effect_color.h"
#include "em_math.h"
#include "render_context_frame_stub.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned texture_live, texture_sets, submissions, draws;

int em_gfx_particle_texture_set_slot(EmGfx *gfx, unsigned slot,
                                      const uint8_t *rgba,
                                      uint32_t width, uint32_t height)
{
    assert(gfx && slot == 1);
    if (!rgba) {
        assert(!width && !height);
        texture_live = 0;
    } else {
        assert(width == 64 && height == 64);
        texture_live = 1;
        ++texture_sets;
    }
    return 1;
}

void em_gfx_particles_draw_slot(EmGfx *gfx, unsigned slot,
                                 const EmGfxParticle *particles, unsigned count)
{
    assert(gfx && slot == 1 && texture_live && count <= 80);
    for (unsigned i = 0; i < count; ++i) {
        const EmGfxParticle *p = &particles[i];
        assert(isfinite(p->depth));
        for (unsigned c = 0; c < 2; ++c)
            for (unsigned axis = 0; axis < 2; ++axis) {
                assert(isfinite(p->corner[c][axis]));
                assert(p->st[c][axis] == (float)c);
            }
        for (unsigned c = 0; c < 4; ++c)
            assert(isfinite(p->color[c]) && p->color[c] >= 0.0f && p->color[c] <= 2.0f);
    }
    submissions += count;
    ++draws;
}

/* The fixture's input view (plumbing only, not a game camera): the
 * renderer's convention (Y up, negative Z forward, column-major) of a
 * look-at from `pos` along `fwd` with the GS up vector `up_gs`, the same
 * layout em_cs_view_to_native gives the original 00102CD0 matrix. */
static void fixture_view(float *m, const float *pos, const float *fwd, const float *up_gs)
{
    float sx = fwd[1] * up_gs[2] - fwd[2] * up_gs[1];
    float sy = fwd[2] * up_gs[0] - fwd[0] * up_gs[2];
    float sz = fwd[0] * up_gs[1] - fwd[1] * up_gs[0];
    float sl = sqrtf(sx * sx + sy * sy + sz * sz);
    sx /= sl; sy /= sl; sz /= sl;
    float ux = sy * fwd[2] - sz * fwd[1];
    float uy = sz * fwd[0] - sx * fwd[2];
    float uz = sx * fwd[1] - sy * fwd[0];
    m[0] = -sx;     m[4] = -sy;     m[8]  = -sz;
    m[1] = -ux;     m[5] = -uy;     m[9]  = -uz;
    m[2] = -fwd[0]; m[6] = -fwd[1]; m[10] = -fwd[2];
    m[3] = 0.0f;    m[7] = 0.0f;    m[11] = 0.0f;
    m[12] = sx * pos[0] + sy * pos[1] + sz * pos[2];
    m[13] = ux * pos[0] + uy * pos[1] + uz * pos[2];
    m[14] = fwd[0] * pos[0] + fwd[1] * pos[1] + fwd[2] * pos[2];
    m[15] = 1.0f;
}

int main(void)
{
    EmGfx *gfx = (EmGfx *)(uintptr_t)1;
    const char *directory = "assets/scene_snow";
    assert(!em_area11_effect_runtime_load(gfx, directory, "missing.emef", "area11_effect.emtx"));
    assert(!em_area11_effect_runtime_state() && !texture_live);
    em_random_seed(0x1278);
    fixture_fog_area(-209.0f, 304.0f);   /* AREA11's area fog (001D8FD0): the owner's draw reads +0xA0 */
    assert(em_area11_effect_runtime_load(gfx, directory, "area11_effect.emef", "area11_effect.emtx"));
    const EmArea11Effect *owner = em_area11_effect_runtime_state();
    assert(owner && owner->state == 0 && texture_live);
    /* Exercise the complete loaded effect from a view facing its authored
     * location. This is plumbing coverage, not an invented gameplay camera. */
    float eye[3] = {452.3f, 280.0f, 240.0f};
    float forward[3] = {0,0,1}, up[3] = {0,1,0}, view[16];
    fixture_view(view, eye, forward, up);
    float expected_phase = 0.0f;
    for (unsigned tick = 0; tick < 400; ++tick) {
        em_area11_effect_runtime_tick();
        expected_phase = em_effect_float32((double)expected_phase + 0.025f);
        if (expected_phase >= 2) expected_phase = em_effect_float32((double)expected_phase - 1);
        assert(owner->state == 1 && owner->phase == expected_phase && owner->flags == 1);
        assert(owner->sound_handle == -1 && owner->contact_cooldown == 0);
        fixture_frame_head(view, 480.0f);
        em_area11_effect_runtime_draw(gfx, view, 480.0f);
    }
    assert(submissions > 20000 && draws > 300 && texture_sets == 1);
    em_area11_effect_runtime_clear(gfx);
    assert(!em_area11_effect_runtime_state() && !texture_live);
    unsigned previous = submissions;
    em_area11_effect_runtime_tick();
    em_area11_effect_runtime_draw(gfx, view, 480);
    assert(submissions == previous);
    /* Re-entry reconstructs original initializer state, not a captured phase. */
    assert(em_area11_effect_runtime_load(gfx, directory, "area11_effect.emef", "area11_effect.emtx"));
    owner = em_area11_effect_runtime_state();
    assert(owner->state == 0 && owner->phase == 0);
    em_area11_effect_runtime_tick();
    assert(owner->state == 1 && owner->phase == 0.025f);
    em_area11_effect_runtime_clear(gfx);
    printf("AREA11 effect runtime PASS:400 ticks,%u submissions,re-entry,cleanup\n", submissions);
    return 0;
}
