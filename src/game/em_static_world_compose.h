/* em_static_world_compose.h - 001C1D00's whole call tree (the background
 * channel and the static-world grid pass) and 001D52E0, composed from the
 * verified translations over ONE set of original-address views
 * (docs/STATIC_WORLD.md, "Binding").
 *
 *   001C1D00  em_frh_001C1D00            (em_frame_render_heads)
 *   001E2260  em_rvr_001E2260            (em_render_verify_rest; key 0x1500)
 *   001E0CF0  em_rvr_001E0CF0            (em_render_verify_rest)
 *   001E0CC0, 001D2910, 001D2E00         (em_render_context)
 *   001E1E60, 001E1AD0 and their builders (em_static_world)
 *   001D5370, 001D52E0                   (em_render_context)
 *   001D4DA0, 001D4FB0, 001D4B20, 001C6120, 001026D0 (em_static_world)
 *   001D2D20  em_frh_001D2D20 into a native matrix, stored at its address
 *   001D1F80, 001D1FF0, 001D2040, 001D7080, 001D6BA0 (em_load_veil_particles)
 *   00128250  em_stream_lanes_00128250;  001281C0 em_player_float_to_int;
 *   0011DF78  em_sdk_math_original_0011DF78; 001026A0 em_effect_original_001026A0
 *
 * The composition adds no behaviour: each worker of one translation is the
 * translation of the other. What no translation covers is the host's
 * (EmSwcHostWorkers): 001D5BD0 (key list without 0x0B00; the verified
 * translation is em_area01_render_001D5BD0), the flag-0x23 sound branch of
 * 001E1E60 and 001E1AD0's 001E1760 / 001E17E0 (flag 0x22). The first level
 * reaches none of them; bind them to a fault.
 *
 * Views: every original byte the tree reads or writes must be covered. For
 * AREA11 that is the render context (0x811CC0..+0x2540), the channel packet
 * arena, D_00275670..D_0027569F, the area bytes D_00810700..702, the state
 * byte (D_008101D0), the view D_00810610, the bank word D_0028A5A0 and the
 * bank itself (tools/export_static_world.py), the skin/scratch words
 * 0x70003400..0x7000347F and 0x70003AC0..0x70003AFF, D_00817240..BF and the
 * .data block D_00253560..D_002535EF (exported with the bank). The veil
 * builders need the channel cursors, context +0x9C and D_00275674 as host
 * words (4-byte aligned host storage) and one view that holds every packet
 * the channels write.
 *
 * Fail-stop: the first fault of any translation is latched here (the
 * innermost: its module, original function, code and detail); every later
 * entry returns -1. */
#ifndef EM_STATIC_WORLD_COMPOSE_H
#define EM_STATIC_WORLD_COMPOSE_H

#include <stdint.h>

#include "game/em_static_world.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_SWC_MODULE_NONE = 0,
    EM_SWC_MODULE_STATIC_WORLD = 1, /* em_static_world (EM_SW_FAULT_*) */
    EM_SWC_MODULE_RENDER_CONTEXT = 2, /* em_render_context (EM_RC_FAULT_*) */
    EM_SWC_MODULE_FRAME_HEADS = 3,  /* em_frame_render_heads (EM_FRH_FAULT_*) */
    EM_SWC_MODULE_VERIFY_REST = 4,  /* em_render_verify_rest (EM_RVR_FAULT_*) */
    EM_SWC_MODULE_VEIL = 5,         /* em_load_veil_particles (EM_LVP_FAULT_*) */
    EM_SWC_MODULE_COMPOSE = 6       /* this file: a view or host worker is missing */
};

typedef struct {
    int32_t module;   /* EM_SWC_MODULE_* */
    uint32_t address; /* the original function that faulted */
    int32_t code;     /* that module's fault code */
    uint32_t detail;
} EmSwcFault;

/* The callees no translation covers (see above). Each returns >= 0; a
 * negative result faults. NULL faults when reached. */
typedef struct {
    void *ctx;
    int (*w_001D5BD0)(void *ctx);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001D73A0)(void *ctx, int32_t handle, int32_t *result);
    int (*w_001D72D0)(void *ctx, const uint32_t snd[4], int32_t *result);
    int (*w_001D75E0)(void *ctx, int32_t chan, const uint32_t snd[4], uint64_t a2, uint32_t a3,
                      uint32_t f12, uint32_t f13);
    int (*w_001CEFD0)(void *ctx, const uint32_t snd[4], const uint32_t vol[4]);
    int (*w_001E1760)(void *ctx, int32_t chan);
    int (*w_001E17E0)(void *ctx, int32_t chan);
} EmSwcHostWorkers;

typedef struct {
    const EmStaticWorldView *views;
    uint32_t view_count;
    /* Optional, one flag per view (NULL: every view writable). Nonzero marks
     * the view read-only for em_static_world (a store faults) and for
     * em_frame_render_heads (EmFrhView.writable = 0), as em_rcl marks its
     * external views. em_render_context and em_load_veil_particles have no
     * read-only notion (em_rcl's rc_views do not either). */
    const uint8_t *read_only;
    EmSwcHostWorkers host;
    EmStaticWorldTrace trace; /* test hook (every routine entry); NULL in the game */
    void *trace_ctx;
    EmSwcFault fault;         /* latched; cleared only by the caller */
} EmSwc;

/* 0 on success, -1 on a fault (or an earlier latched one). */
int em_swc_001C1D00(EmSwc *c, uint32_t state_address);
int em_swc_001E0CF0(EmSwc *c);
int em_swc_001D5370(EmSwc *c);
int em_swc_001D52E0(EmSwc *c);
/* 001E1E60(a0, chan) / 001E1AD0(a0, chan) alone (the unit oracle). */
int em_swc_001E1E60(EmSwc *c, uint32_t a0, int32_t chan, uint32_t *result);
int em_swc_001E1AD0(EmSwc *c, uint32_t a0, int32_t chan, uint32_t *result);
/* Run body with an em_static_world state wired as above (one builder alone:
 * the unit oracle, or a host that needs a single builder). A failure of
 * body latches the builder's fault here. Returns body's result. */
int em_swc_with_static_world(EmSwc *c, int (*body)(EmStaticWorld *sw, void *arg), void *arg);

#ifdef __cplusplus
}
#endif

#endif /* EM_STATIC_WORLD_COMPOSE_H */
