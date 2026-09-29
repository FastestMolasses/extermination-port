#ifndef EM_AREA11_EFFECT_RUNTIME_H
#define EM_AREA11_EFFECT_RUNTIME_H
/* em_area11_effect_runtime.h - the AREA11 flame, owner 008235F0
 * (docs/AREA11_EFFECT.md): em_area11_effect's controller with the owner's
 * world matrix (identity plus the placement, the exporter's checked form of
 * 001029C0 / 00102C58 / 00102918 over a zero rotation), and its DRAW call
 * 001D04B0(+0xD0, 1, D_00828340, phase, seed) bound to em_effects_live's
 * 001CCF70 / 001CFA60 / 001CFBE0: the flame's packets go into the chain
 * page, whose consumer runs the sprite program on them (docs/CHAIN_PAGE.md).
 * The sound service and the contact reactions stay unbound (the doc). */
#include <stdint.h>

#include "game/em_area11_effect.h"

#define EM_AREA11_EFFECT_DESCRIPTOR 0x00828340u   /* D_00828340 (overlay AREA11) */

/* The scene's `area11effect <config> [<texture>]` line: the exported
 * placement and descriptor (tools/export_area11_effect.py; the texture token
 * of an older manifest is not read: the page textures hold the flame's
 * TEX0). 1, or 0 (missing or malformed). */
int em_area11_effect_runtime_load(const char *directory, const char *config);
void em_area11_effect_runtime_clear(void);
/* One 008235F0 call. 0, or -1 when its 001D04B0 faulted (em_effects_live
 * latched it; the caller fail-stops). */
int em_area11_effect_runtime_tick(void);
const EmArea11Effect *em_area11_effect_runtime_state(void);

#endif
