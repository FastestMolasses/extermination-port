#include "game/em_area11_effect_runtime.h"
#include "game/em_random.h"
#include "game/em_effect_color.h"
#include "em_math.h"

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

int main(void)
{
    EmGfx *gfx = (EmGfx *)(uintptr_t)1;
    const char *directory = "assets/scene_snow";
    assert(!em_area11_effect_runtime_load(gfx, directory, "missing.emef", "area11_effect.emtx"));
    assert(!em_area11_effect_runtime_state() && !texture_live);
    em_random_seed(0x1278);
    assert(em_area11_effect_runtime_load(gfx, directory, "area11_effect.emef", "area11_effect.emtx"));
    const EmArea11Effect *owner = em_area11_effect_runtime_state();
    assert(owner && owner->state == 0 && texture_live);
    /* Exercise the complete loaded effect from a view facing its authored
     * location. This is plumbing coverage, not an invented gameplay camera. */
    float eye[3] = {452.3f, 280.0f, 240.0f};
    float forward[3] = {0,0,1}, up[3] = {0,1,0}, view[16];
    em_mat4_lookat_gs(view, eye, forward, up);
    float expected_phase = 0.0f;
    for (unsigned tick = 0; tick < 400; ++tick) {
        em_area11_effect_runtime_tick();
        expected_phase = em_effect_float32((double)expected_phase + 0.025f);
        if (expected_phase >= 2) expected_phase = em_effect_float32((double)expected_phase - 1);
        assert(owner->state == 1 && owner->phase == expected_phase && owner->flags == 1);
        assert(owner->sound_handle == -1 && owner->contact_cooldown == 0);
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
