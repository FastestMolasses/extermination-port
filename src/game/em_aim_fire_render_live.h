#ifndef EM_AIM_FIRE_RENDER_LIVE_H
#define EM_AIM_FIRE_RENDER_LIVE_H
#include <stddef.h>
#include "game/em_aim_fire_target.h"
#include "game/em_area00_hud.h"

/* Composition of the verified sprite, beam, reticle and packet-chain owners.
 * Context, matrices and packets come from em_render_context_live. map supplies
 * only external inputs: actors, scratch 70003600..3F, original tables and the
 * caller's native stack-temporary view. No storage is duplicated here.
 *
 * vf23_valid certifies the ambient fog register, not the context fog block.
 * The caller must clear it when an intervening untracked original may change
 * vf23. A completed beam or a sprite that reached packet allocation publishes
 * its actual fog load. A reticle requires a certified input. Other VU fields
 * are forwarded where the existing owner exposes them; the existing sprite
 * and external SDK owners do not publish every VU clobber. */
typedef struct {
    void *context;
    void *(*map)(void *, uint32_t address, size_t size, int write);
    int (*call)(void *, EmAimFireTargetCall *);
    EmArea00HudVu *vu;
    int *vf23_valid;
    uint32_t fault_function, fault_address;
} EmAimFireRenderLive;

/* Handles 001CD520, 001E2BA0, 001DD170 and the reticle packet roots.
 * Also exposes matrix lookup 001CD370 and chain workers 001CB5F0 / 001CB6B0 /
 * 001CB900; these raw helpers do not require a VU pointer or external map.
 * Returns 0 on success; -1 latches a fault. Unsupported callees are errors.
 * call must preserve the shared RNG and implement the existing SDK/text
 * owners; this adapter handles all reached packet-chain calls itself.
 * The mapped stack must cover entry sp-0x140..sp for the nested reticle,
 * sp-0xE0..sp for a beam, in addition to its pointer arguments. */
int em_aim_fire_render_live_call(EmAimFireRenderLive *, EmAimFireTargetCall *);
#endif
