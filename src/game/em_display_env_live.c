/* em_display_env_live.c - see em_display_env_live.h. */
#include "game/em_display_env_live.h"

#include <stdio.h>

#include "game/em_render_context_live.h"
#include "game/em_sdk_display_original.h"
#include "game/em_startup_load_gaps.h"

#define D_00241010 UINT32_C(0x00241010)

static struct {
    EmGfx *gfx;
    const int16_t *spad3B94, *spad3B96;
    uint8_t env[2][EM_SLG_DISPENV_SIZE];   /* D_00810EA0, D_00810EC8 */
    int faulted;
} s_disp;

static int fault(const char *what)
{
    if (!s_disp.faulted) fprintf(stderr, "display env: %s\n", what);
    s_disp.faulted = 1;
    return -1;
}

int em_display_env_live_install(EmGfx *gfx, const int16_t *spad3B94, const int16_t *spad3B96)
{
    if (!gfx || !spad3B94 || !spad3B96) return -1;
    s_disp.gfx = gfx;
    s_disp.spad3B94 = spad3B94;
    s_disp.spad3B96 = spad3B96;
    s_disp.faulted = 0;
    return 0;
}

/* 001AB4E0's worker 001002E0, with 00100268's &D_00241010. */
static int w_001002E0(void *ctx, uint8_t *env, int32_t psm, int32_t width, int32_t height, int32_t dx,
                      int32_t dy)
{
    (void)ctx;
    const uint8_t *mode = em_rcl_bytes(D_00241010, EM_SDK_GS_MODE_SIZE);
    if (!mode) return fault("001002E0: the render context holds no D_00241010");
    if (em_sdk_001002E0(mode, env, psm, width, height, dx, dy) < 0)
        return fault("001002E0 trapped or reached its message printer 00122B58");
    return 0;
}

int em_display_env_live_step_r(void *context)
{
    (void)context;
    if (s_disp.faulted) return -1;
    if (!s_disp.gfx) return fault("step R before the install");
    const EmSlgDisplayWorkers w = {NULL, w_001002E0};
    if (em_slg_001AB4E0(&w, s_disp.env, *s_disp.spad3B94, *s_disp.spad3B96) < 0)
        return fault("001AB4E0 faulted");
    return 0;
}

static int store(void *ctx, uint32_t address, uint64_t value)
{
    (void)ctx;
    return em_gfx_gs_display_store(s_disp.gfx, address, value);
}

int em_display_env_live_step_u(void *context, int32_t buffer)
{
    (void)context;
    if (s_disp.faulted) return -1;
    if (!s_disp.gfx) return fault("step U before the install");
    /* The call at 0x1AB0EC: D_00810EA0 + 40 * D_00810E80 (a halfword). */
    const int32_t index = (int16_t)(uint16_t)buffer;
    if (index != 0 && index != 1) return fault("step U: D_00810E80 selects no display environment");
    const uint8_t *mode = em_rcl_bytes(D_00241010, EM_SDK_GS_MODE_SIZE);
    if (!mode) return fault("00100550: the render context holds no D_00241010");
    if (em_sdk_00100550(mode, s_disp.env[index], store, NULL) < 0)
        return fault("00100550: the presenter refused a display register");
    return 0;
}

const uint8_t *em_display_env_live_bytes(void) { return &s_disp.env[0][0]; }
