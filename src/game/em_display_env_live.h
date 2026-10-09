/* em_display_env_live - main-loop steps R and U over the one storage of
 * the SDK's two display environments (docs/GS_EXACT.md section 11).
 *
 * Storage owned here: D_00810EA0 .. D_00810EEF, the two display
 * environments at the head of the double-buffer descriptor 001AB370 builds
 * (00101898(&D_00810EA0, ...)): five dwords each (PMODE, SMODE2, DISPFB,
 * DISPLAY, BGCOLOR), 0x28 bytes apart. Step R rewrites both whole every
 * iteration before step U reads one, so no boot value of them is ever
 * read. The rest of that descriptor (from D_00810EF0: its GIF tags, draw
 * environments and clears) is not owned here.
 *
 * Step R (0x1AB020): 001AB4E0(0x70003B94, 0x70003B96), the translation
 * em_slg_001AB4E0 (em_startup_load_gaps; one owner) with its worker
 * 001002E0 = em_sdk_001002E0 over D_00241010 (the render context's copy,
 * em_rcl_bytes). The screen offset is the scene state's spad3B94 /
 * spad3B96 (the options' SCREEN ADJUST, 00201F70).
 * Step U (0x1AB0EC): 00100550(D_00810EA0 + 40 * D_00810E80), the
 * translation em_sdk_00100550, whose GS privileged register stores go to
 * the presenter (em_gfx_gs_display_store).
 *
 * Fail-stop: a fault of either step is reported once and returns -1 (the
 * frame quits). */
#ifndef EM_DISPLAY_ENV_LIVE_H
#define EM_DISPLAY_ENV_LIVE_H

#include <stdint.h>

#include "em_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_DISPLAY_ENV_ADDRESS UINT32_C(0x00810EA0)
#define EM_DISPLAY_ENV_SIZE 0x50u

/* The presenter and the two scratchpad halfwords (0x70003B94, 0x70003B96)
 * step R reads. 0, or -1 (a NULL argument). */
int em_display_env_live_install(EmGfx *gfx, const int16_t *spad3B94, const int16_t *spad3B96);
/* em_frame_set_step_ru's steps (the context is unused). 0, or -1. */
int em_display_env_live_step_r(void *context);
int em_display_env_live_step_u(void *context, int32_t buffer);
/* D_00810EA0 .. D_00810EEF as step R last wrote them. */
const uint8_t *em_display_env_live_bytes(void);

#ifdef __cplusplus
}
#endif

#endif
