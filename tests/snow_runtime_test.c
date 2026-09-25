#include "game/em_snow_runtime.h"
#include "game/em_random.h"
#include "em_math.h"
#include "render_context_frame_stub.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static unsigned texture_loads, texture_clears, draw_calls, particles_drawn;
static unsigned guard_band_particles;
static int reject_texture;

int em_gfx_particle_texture_set(EmGfx *gfx, const uint8_t *rgba,
                                 uint32_t width, uint32_t height)
{
    (void)gfx;
    if (!rgba) {
        assert(!width && !height);
        ++texture_clears;
        return 1;
    }
    assert(width == 16 && height == 16);
    ++texture_loads;
    return !reject_texture;
}

void em_gfx_particles_draw(EmGfx *gfx, const EmGfxParticle *particles,
                            unsigned count)
{
    (void)gfx;
    assert(count <= 2160);
    ++draw_calls;
    particles_drawn += count;
    for (unsigned i = 0; i < count; ++i) {
        const EmGfxParticle *p = particles + i;
        for (unsigned lane = 0; lane < 4; ++lane)
            assert(isfinite(p->color[lane]) && p->color[lane] >= 0);
        assert(isfinite(p->depth) && p->depth >= 0 && p->depth <= 1);
        for (unsigned corner = 0; corner < 2; ++corner) {
            float x = p->corner[corner][0], y = p->corner[corner][1];
            assert(isfinite(x) && isfinite(y));
            /* GS sixteen-subpixel corners survive the native coordinate
             * conversion, allowing float roundoff on the Y /112 step. */
            assert(fabsf(x*4096 - roundf(x*4096)) < 0.01f);
            assert(fabsf(y*1792 - roundf(y*1792)) < 0.01f);
            assert(p->st[corner][0] == (float)corner);
            assert(p->st[corner][1] == (float)corner);
        }
        float center_x = (p->corner[0][0] + p->corner[1][0]) * 0.5f;
        float center_y = (p->corner[0][1] + p->corner[1][1]) * 0.5f;
        if (fabsf(center_x) > 1 || fabsf(center_y) > 1)
            ++guard_band_particles;
    }
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
    const float eye[3] = {250, 245, 225};
    const float forward[3] = {0, -0.6f, -0.8f};
    const float up[3] = {0, 1, 0};
    float view[16];
    fixture_view(view, eye, forward, up);
    fixture_frame_head(view, 480);
    em_snow_runtime_clear(NULL);
    em_snow_runtime_tick(eye, 0);
    em_snow_runtime_draw(NULL, view, 480);
    assert(!draw_calls);
    assert(em_snow_runtime_load(NULL, "assets/scene_snow", "snow.emsn",
                                "snow.emtx", 0x10));
    for (unsigned frame = 0; frame < 240; ++frame) {
        em_snow_runtime_tick(eye, 0);
        em_snow_runtime_draw(NULL, view, 480);
    }
    assert(draw_calls == 240 && particles_drawn > 0 && guard_band_particles > 0);
    unsigned previous_draws = draw_calls;
    assert(!em_snow_runtime_load(NULL, "assets/scene_snow", "snow.emsn",
                                 "snow.emtx", 0x20));
    em_snow_runtime_tick(eye, 0);
    em_snow_runtime_draw(NULL, view, 480);
    assert(draw_calls == previous_draws);
    assert(!em_snow_runtime_load(NULL, "assets/scene_snow", "absent.emsn",
                                 "snow.emtx", 0x10));
    assert(!em_snow_runtime_load(NULL, "assets/scene_snow", "snow.emsn",
                                 "absent.emtx", 0x10));
    reject_texture = 1;
    assert(!em_snow_runtime_load(NULL, "assets/scene_snow", "snow.emsn",
                                 "snow.emtx", 0x10));
    em_snow_runtime_tick(eye, 0);
    em_snow_runtime_draw(NULL, view, 480);
    assert(draw_calls == previous_draws);
    reject_texture = 0;
    assert(em_snow_runtime_load(NULL, "assets/scene_snow", "snow.emsn",
                                "snow.emtx", 0x10));
    em_snow_runtime_clear(NULL);
    em_snow_runtime_tick(eye, 0);
    em_snow_runtime_draw(NULL, view, 480);
    assert(draw_calls == previous_draws);
    assert(texture_loads == 3 && texture_clears == 8);
    printf("Snow runtime: 240 real-asset ticks, %u projected particles, "
           "%u guard-band centers, load/reset/failure paths PASS\n",
           particles_drawn, guard_band_particles);
    return 0;
}
