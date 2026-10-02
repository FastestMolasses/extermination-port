#ifndef EM_AREA11_EFFECT_RUNTIME_H
#define EM_AREA11_EFFECT_RUNTIME_H
/* em_area11_effect_runtime.h - the AREA11 flame, owner 008235F0
 * (docs/AREA11_EFFECT.md): em_area11_effect's controller over the flame's
 * pool record (its +0x00 / +0x04, the +0x1F0 block and the contact words
 * +0x30 / +0x34 live in the EmActor), with the owner's world matrix
 * (identity plus the placement, the exporter's checked form of 001029C0 /
 * 00102C58 / 00102918 over a zero rotation), its DRAW call
 * 001D04B0(+0xD0, 1, D_00828340, phase, seed) bound to em_effects_live's
 * 001CCF70 / 001CFA60 / 001CFBE0 (the flame's packets go into the chain
 * page, whose consumer runs the sprite program on them: docs/CHAIN_PAGE.md),
 * its loop sound 001FC3C0 / 001FC520 on em_sfx's looped positional service
 * (em_sfx_loop_service / em_sfx_loop_release over the record's +0xB0 and
 * +0x20C), and its 001B17A0 and 001AFC10 through the binder's hooks. */
#include <stdint.h>

#include "game/em_actor_pool.h"
#include "game/em_area11_effect.h"

#define EM_AREA11_EFFECT_DESCRIPTOR 0x00828340u   /* D_00828340 (overlay AREA11) */

/* The scene's `area11effect <config> [<texture>]` line: the exported
 * placement and descriptor (tools/export_area11_effect.py; the texture token
 * of an older manifest is not read: the page textures hold the flame's
 * TEX0). 1, or 0 (missing or malformed). */
int em_area11_effect_runtime_load(const char *directory, const char *config);
void em_area11_effect_runtime_clear(void);

/* One 008235F0 call over the flame's record. `record` is the record's
 * original address (its +0x14), `frame` / `ordinal` the scratchpad words
 * 0x70003B68 / 0x70003B8A 001FC3C0 reads; `publish` is 001B17A0(self) and
 * `free_record` 001AFC10(self) (after it the record is not written back).
 * 0, or -1 when a callee faulted (*fault_address names it: 001D04B0 when
 * em_effects_live latched a fault, 001B17A0, 001AFC10, or 0x8235F0 when
 * the export is not loaded). */
typedef struct {
    void *ctx;
    EmActor *actor;
    uint32_t record;
    int32_t frame;
    int16_t ordinal;
    int (*publish)(void *ctx);
    int (*free_record)(void *ctx);
} EmArea11EffectRuntimeCall;
int em_area11_effect_runtime_tick(const EmArea11EffectRuntimeCall *call, uint32_t *fault_address);

/* 00823580 over the flame's record (the +0x210 cooldown it writes) for the
 * contact pass 001A8660: em_area11_effect_contact. */
int em_area11_effect_runtime_contact(EmActor *actor, uint8_t target_flags,
                                     const EmArea11EffectContactWorkers *workers,
                                     uint8_t *target_reaction, uint32_t *fault_address);

#endif
