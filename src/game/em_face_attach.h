/* em_face_attach.h - the +0x90 attachment draw (a face unit) of the owners
 * whose draw method is 001CAA00: Roger's face, and the player's while a
 * script holds his face slot. Docs: docs/FACE_ATTACH.md.
 *
 * Hand translation of these original functions (boot ELF SCUS-97112), read
 * from the original instructions (the decomp's build/asm), not from the
 * readable C alone:
 *   001CB3C0  the attachment draw (byte-matched C): a local 4x4 identity
 *             (001029C0) whose row 3 xyz are D_00250FB0 / D_00250FB4 /
 *             D_00250FB8, times the owner's node D_00275B40[+0x94] world
 *             matrix (001026D0), uploaded by 001C7900(local, owner + 0x80,
 *             0x3F5, 0); then 001CB2C0(owner, 0x3F3, 0), 001D1F80(0, 1, 0)
 *             and 001D3F50(*(*(owner + 0x90) + 0x60))
 *   001D3F50  tail thunk: 001D3E40(0, a0)
 *   001D3E40  (NEARMISS C; the .s was followed) vif_append_ref_tag(chan,
 *             0x0023C480); REF 8 qw to the skin record D_00816440 +
 *             (context +0x9C) * 0x80; REF 2 qw to D_002514B0 only while
 *             001D2910(0) is 0; REF (face +0x04 low halfword) qw to face +
 *             0x40
 * and binds, without a new translation, the verified ones it reaches:
 *   vif_append_ref_tag (001D2090) and the tag writer
 *                       em_owner_draw_vif_append_ref_tag / em_owner_draw_tag
 *   001D2910(0)         a worker (the live render context's
 *                       em_render_context_001D2910, em_rcl_001D2910)
 *   001029C0            em_owner_services_identity_001029C0
 *   001026D0            em_sdk_vu0_001026D0
 *   001C7900, 001CB2C0  em_anim_rest_001C7900 / _001CB2C0
 *   001D88B0            em_frh_001D88B0 (em_frame_render_heads), through
 *                       em_face_attach_w_001D88B0 below; its workers are
 *                       em_actor_light_001D8130 / _001D8340 / _001D8690,
 *                       and its modes 1, 3..6 em_frh_001D8C30
 *   001D1F80(0, 1, 0)   a worker (the render context's veil module,
 *                       em_load_veil_particles_001D1F80)
 *
 * Storage. Nothing is copied. The EE bytes the routines read by address
 * (the owner's +0x80..+0x97, the attachment slot's +0x40..+0x63, the face
 * resource's +0x04) come through the EmAnimRest's region table, the same
 * table 001CB2C0 reads through. The display-list channel is the one
 * EmOwnerServicesChannel array that 001C7420, 001CA940 and 001C7900 write
 * (EmAnimRestWorld.channel must equal EmOwnerDrawWorld.channel). The
 * context words (+0x0C, +0x9C, +0x50, D_00275674) are EmOwnerDrawWorld's;
 * the lighting storage (the rig record D_00817BC0, D_00275688, context
 * +0x246C / +0x2380) is EmActorLightWorld's.
 *
 * Arithmetic: every VU0-macro instruction goes through em_ee_float.h (in the
 * reused translations); 001CB3C0 itself only moves raw words.
 *
 * Fail-stop. Every view, region, index and the channel room for the whole
 * face unit are checked before the first write; a failure latches a fault
 * and returns -1 with nothing written. A callee that faults later latches a
 * fault here too (its own module keeps its own fault); the writes already
 * made stay, in the original's order. Once latched, every call returns -1
 * until the caller clears it.
 *
 * Oracle: tools/test_face_attach_reference.py executes the original
 * instructions over the captured AREA11 RAM and compares every byte written,
 * every call with its arguments, and a fixed storage subset (this path's
 * write set: the display-list window, context +0x10 / +0x50, SPR
 * 0x3400..0x34BF, the rig record, D_00275688, the stack matrix) at every
 * callee entry; the whole RAM and scratchpad are compared after the call; the
 * unit then runs through em_object_unit (the face-morph path) against the
 * original VU1 face program.
 *
 * stdint only. */
#ifndef EM_FACE_ATTACH_H
#define EM_FACE_ATTACH_H

#include <stdint.h>

#include "game/em_actor_light_001D89D0.h"
#include "game/em_anim_runtime_rest.h"
#include "game/em_owner_draw_original.h"
#include "game/em_owner_services_original.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_FACE_ATTACH_KERNEL UINT32_C(0x0023C480)   /* 001D3E40's CALL target */
#define EM_FACE_ATTACH_SKIN_RECORD UINT32_C(0x00816440)
#define EM_FACE_ATTACH_D_002514B0 UINT32_C(0x002514B0)
#define EM_FACE_ATTACH_COLOR_VU 0x3F5                 /* 001C7900 a2 */
#define EM_FACE_ATTACH_WEIGHTS_VU 0x3F3               /* 001CB2C0 a1 */
/* Bytes one 001CB3C0 appends to channel 0: 001C7900 0x100, 001CB2C0 0x40,
 * 001D1F80(0, 1, 0) one REF (0x10), 001D3E40 0x40 (0x50 while the
 * D_002514B0 REF is emitted). */
#define EM_FACE_ATTACH_UNIT_BYTES 0x190u
#define EM_FACE_ATTACH_UNIT_BYTES_REF2 0x1A0u

/* Fault codes: numerically the EM_OWNER_FAULT_* codes. */
enum {
    EM_FACE_ATTACH_FAULT_NONE = 0,
    EM_FACE_ATTACH_FAULT_NULL = 1,       /* a reached view or worker is NULL */
    EM_FACE_ATTACH_FAULT_WORKER = 2,     /* a callee or worker failed */
    EM_FACE_ATTACH_FAULT_BAD_INDEX = 4   /* an unmapped EE address, a node index outside the
                                          * slots, a channel index, or short channel room */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_FACE_ATTACH_FAULT_* */
} EmFaceAttachFault;

typedef struct {
    /* 001C7900 / 001CB2C0 and the EE-address reads (its region table). Its
     * workers.w_001D88B0 must be em_face_attach_w_001D88B0 with
     * workers.ctx = this EmFaceAttach. */
    EmAnimRest *anim;
    /* 001D3E40's views: channel (== anim->world.channel), ctx_9C,
     * d00275674, ctx_50. `models` and ctx_0C are not read (001D2910 is the
     * worker's). */
    const EmOwnerDrawWorld *draw;
    /* 001D88B0's storage and its 001D8130 / 001D8340 / 001D8690. */
    EmActorLight *light;
    /* *D_00275670: the context's original address (001D88B0 reaches context
     * +0x246C and +0x2380 through it; the words themselves are the light
     * world's views). */
    uint32_t context_address;
    /* D_00250FB0, D_00250FB4, D_00250FB8: the attachment's offset (3 words,
     * .data of the boot ELF). */
    const uint32_t *d00250FB0;
    /* D_00275B40: the draw walk's current node array (the owner's +0x110
     * slots, published by 001CB590); node +0x90 is EmOwnerBone.world. */
    EmOwnerBone *const *d00275B40;
    uint32_t d00275B40_count;
} EmFaceAttachWorld;

typedef struct {
    void *ctx;
    /* 001D1F80(a0, a1, a2): the GS state REF; it appends at channel 0 and
     * must leave the channel's host cursor advanced past what it wrote. */
    int (*w_001D1F80)(void *ctx, int32_t a0, int32_t a1, int32_t a2);
    /* 001D2910(a0): *result = its return value (001D3E40 passes 0: the
     * context's +0x0C bit 0). It is also queried, untraced, before the
     * first write to size the unit (a read with no side effect). */
    int (*w_001D2910)(void *ctx, int32_t a0, uint32_t *result);
    /* Optional (tests): called at the entry of every callee these routines
     * reach through a boundary, before the call. `args` are the original
     * argument registers a0..a3 (and t0 in args[4] for 001D8340), except
     * that a pointer to 001CB3C0's stack matrix is passed as 0; `frame` is
     * that matrix as it is at the call (16 words). */
    void (*trace)(void *ctx, uint32_t callee, const uint32_t args[5], const uint32_t frame[16]);
} EmFaceAttachWorkers;

typedef struct {
    EmFaceAttachWorld world;
    EmFaceAttachWorkers workers;
    EmFaceAttachFault fault; /* latched; cleared only by the caller */
    /* internal: 001CB3C0's stack matrix while a call is running (trace) */
    const uint32_t *frame;
} EmFaceAttach;

/* 001CB3C0(owner): owner is the EE address of the record (its +0x80..+0x97
 * must be mapped). 0, or -1 (fault latched). */
int em_face_attach_001CB3C0(EmFaceAttach *s, uint32_t owner);

/* 001D3F50(face) = 001D3E40(0, face). */
int em_face_attach_001D3F50(EmFaceAttach *s, uint32_t face);

/* 001D3E40(chan, face): face is the EE address of the face resource (its
 * +0x04 word must be mapped). Verified for chan 0 only (the one channel
 * 001D3F50 passes, and the only one any case runs); other channels follow
 * the same code with context +0x50 + 4 chan and channel[chan]. */
int em_face_attach_001D3E40(EmFaceAttach *s, int32_t chan, uint32_t face);

/* The EmAnimRestWorkers.w_001D88B0 adapter (ctx = the EmFaceAttach): runs
 * em_frh_001D88B0(position, 0x70003400, 0x70003440, token) over views of
 * the light world, `a`, `b` and the token's 16 mapped bytes; its 001D8130 /
 * 001D8340 / 001D8690 are em_actor_light's. 001D8340 is reached with owner
 * 0 only (the fold is never run, so the position is never read). */
int em_face_attach_w_001D88B0(void *ctx, const uint32_t position[4], float a[16], float b[16],
                              uint32_t token);

#ifdef __cplusplus
}
#endif

#endif /* EM_FACE_ATTACH_H */
