/* World TEX0 bindings shared by object units and the chain page.
 * Catalogs contain pixels reconstructed from original disc uploads;
 * docs/LEVEL2_TEXTURES.md records their delivery and verification. */
#ifndef EM_WORLD_TEXTURES_LIVE_H
#define EM_WORLD_TEXTURES_LIVE_H

#include "em_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call at completed area-resource delivery, before either world draw.
 * Every call invalidates the device cache, including a same-area reload.
 * Supported deliveries are AREA11/0 and AREA01/0; others fail closed.
 * AREA11/0 is selected initially to preserve the first-level entry. */
int em_world_textures_live_bind(unsigned area, unsigned subarea);

/* Validate the selected catalogs, clear the previous world's TEX0 table,
 * and copy pixels to the device. Subsequent calls on the same device are
 * free until bind. Failure prevents drawing; no fallback catalog exists. */
int em_world_textures_live_ensure(EmGfx *gfx);

#ifdef __cplusplus
}
#endif

#endif
