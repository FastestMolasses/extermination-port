/* em_anim_runtime_rest.h - the remaining animation-runtime originals of the
 * first-level route census (lane L33-anim-runtime-rest,
 * docs/ANIM_RUNTIME_REST.md).
 *
 * Hand translations of these original functions (boot ELF SCUS-97112), read
 * from the original instructions (the decomp's build/asm), not from the
 * readable C alone:
 *   001C9E40  rotation matrix -> quaternion (asm-word file; decoded here)
 *   001C9D50  two-pose blend: 001C9E40 twice, quat_nlerp, quat_to_mat3, then
 *             the translation row lerped through the COP1 accumulator
 *             (byte-matched C)
 *   001C7900  matrix upload for a single bone: 001D88B0 into SPR
 *             0x70003400 / 0x70003440, then two DMA packets (B, and the
 *             matrix x view-projection plus the normalised rows x A)
 *             (NEARMISS C; the .s was followed)
 *   001CB2C0  attachment colour packet: the two quadwords at
 *             *(owner + 0x90) + 0x40 / + 0x50 behind a STCYCL / UNPACK header
 *             (NEARMISS C; the .s was followed)
 *   001CAAC0  depth key of a world point (VU0 projection by SPR 0x70003AC0,
 *             z in 12.4 fixed point, clamped) and its 001CB760 insertion
 *             (NEARMISS C; the .s was followed)
 *   001CACB0  indicator draw method: 001CABA0(owner, *(owner + 0x44))
 *             (byte-matched C)
 *   001CB5B0  anim_bone_array_setup: D_00275B40 = D_00275B48 + 0x110
 *             (byte-matched C)
 * and reuses, as leaves, the verified translations of
 *   001CA0A0 quat_nlerp / 001CA1C0 quat_to_mat3 (em_pose_host_workers.c).
 *
 * Shared state is shared by type with the module that already owns it: the
 * display-list channels and the SPR matrices 0x70003400 / 3440 / 3480 /
 * 3AC0 are the em_owner_services_original.h types (001C7420 writes the same
 * channel and the same scratch), and 0x70003760 (quat_to_mat3's products)
 * is a pointer to the same 11 words as EmPoseGlobals.spad3760.
 *
 * Arithmetic: every COP1 and VU0-macro instruction goes through
 * game/em_ee_float.h on bit patterns, under its real form (op, dest mask,
 * broadcast lane; VDIV (3,0) / (3,3)). No host float operation.
 *
 * Fail-stop. Each routine checks, before its first write (and before its
 * first worker call), every view, worker, channel room and mapped address it
 * will reach; otherwise it latches a fault and returns -1 with nothing
 * written. A worker that returns a negative value latches a fault; writes
 * already made at that point stay, in the original's order. Once a fault is
 * latched every later call returns -1 until the caller clears it.
 *
 * Oracle: tools/test_anim_runtime_rest_reference.py executes the original
 * instructions over the captured AREA11 RAM (the playable image and the
 * route beats) and compares every RAM byte, every scratchpad byte, every
 * worker call and every return value.
 *
 * stdint only; depends on em_pose_host_workers (the two quaternion leaves
 * and the EmPoseRegion type) and em_owner_services_original (types only). */
#ifndef EM_ANIM_RUNTIME_REST_H
#define EM_ANIM_RUNTIME_REST_H

#include <stdint.h>

#include "game/em_owner_services_original.h"
#include "game/em_pose_host_workers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 001CAAC0's page table argument: the address the original passes to
 * 001CB760 (built by an instruction immediate, 0x001CAB70). */
#define EM_ANIM_REST_DEPTH_TABLE 0x007635C0u
/* The bytes 001C7900 appends to its channel: a 6-quadword packet and a
 * 10-quadword packet. */
#define EM_ANIM_REST_7900_BYTES 0x100u
/* The bytes 001CB2C0 appends: one 4-quadword packet. */
#define EM_ANIM_REST_B2C0_BYTES 0x40u

/* Fault codes: numerically the EM_OWNER_FAULT_* codes. */
enum {
    EM_ANIM_REST_FAULT_NONE = 0,
    EM_ANIM_REST_FAULT_NULL = 1,        /* a reached worker or view is NULL */
    EM_ANIM_REST_FAULT_WORKER = 2,      /* a worker returned a negative value */
    EM_ANIM_REST_FAULT_BAD_INDEX = 4,   /* a channel index, channel room or EE address outside
                                         * the owned storage */
    EM_ANIM_REST_FAULT_UNMEASURED = 6   /* em_ee_float.h refused a form (unreachable: every
                                         * form used here is in the measured table) */
};

typedef struct {
    uint32_t address; /* original function, data or instruction address */
    int32_t code;     /* EM_ANIM_REST_FAULT_* */
} EmAnimRestFault;

/* Views of the storage the routines read or write. Each is required only by
 * the routine that reaches it. */
typedef struct {
    /* EE address ranges backed by host bytes, for the EE-address reads:
     * 001CB2C0 the owner word +0x90 and the attachment quadwords, 001CACB0
     * the owner word +0x44, 001CAAC0 the position quadword. Read-only use. */
    EmPoseRegion region[EM_POSE_REGION_MAX];
    unsigned region_count;
    /* The display-list channels: cursor word D_00275670 + 0x10 + 4 * chan.
     * The same array (and the same cursors) as EmOwnerServicesWorld.channel. */
    EmOwnerServicesChannel *channel;
    uint32_t channel_count;
    /* SPR 0x70003400 (A), 0x70003440 (B), 0x70003480..0x700034BF (the
     * normalised rows and the translation row), 0x70003AC0 (the
     * view-projection). The same storage as EmOwnerServicesWorld.scratch. */
    EmOwnerServicesScratch *scratch;
    /* SPR 0x700034C0 / 0x700034D0 / 0x700034E0 (4 words each): 001C9D50's
     * two quaternions and their nlerp. */
    uint32_t *spad34C0;
    uint32_t *spad34D0;
    uint32_t *spad34E0;
    /* SPR 0x70003760..0x7000378B (11 words): quat_to_mat3's products; the
     * same words as EmPoseGlobals.spad3760. */
    uint32_t *spad3760;
    /* D_00275B40 (written) and D_00275B48 (read) by 001CB5B0. */
    uint32_t *d275B40;
    const uint32_t *d275B48;
} EmAnimRestWorld;

/* Workers. Each returns >= 0 on success; a negative result faults. */
typedef struct {
    /* SDK 0011E748 sqrtf(f12): the raw result bits. Bind
     * em_anim_rest_sqrt_0011E748 with sqrt_ctx = an EmSdkMathContext. */
    void *sqrt_ctx;
    int (*w_0011E748)(void *sqrt_ctx, uint32_t x, uint32_t *result);
    /* The context of the three workers below. */
    void *ctx;
    /* 001D88B0(position, 0x70003400, 0x70003440, token): the lighting
     * matrices A and B. `position` is the caller's matrix row 3 (m + 12).
     * `token` is the raw a1 register 001C7900 received (001CB3C0 passes
     * owner + 0x80). If the worker appends to a display-list channel it
     * must advance that channel's `cursor`: 001C7900 re-reads the cursor
     * after the call. */
    int (*w_001D88B0)(void *ctx, const uint32_t position[4], float a[16], float b[16],
                      uint32_t token);
    /* 001CB760(table, key, payload): the depth-bucket insertion. `payload`
     * is the raw a1 register 001CAAC0 received (the original masks it to
     * 28 bits inside 001CB760). */
    int (*w_001CB760)(void *ctx, uint32_t table, int32_t key, uint32_t payload);
    /* 001CABA0(owner, model): the indicator draw (a GS packet builder). */
    int (*w_001CABA0)(void *ctx, uint32_t owner, uint32_t model);
} EmAnimRestWorkers;

typedef struct {
    EmAnimRestWorld world;
    EmAnimRestWorkers workers;
    EmAnimRestFault fault; /* latched; cleared only by the caller */
} EmAnimRest;

/* ---- routines. Results are 0 (or the original return value through an
 * out pointer); -1 means a fault was latched (or one already was). ---- */

/* 001C9E40(q, m): q = the quaternion (x, y, z, w) of the rotation part of
 * m (16 words, row-major, raw bits). */
int em_anim_rest_001C9E40(EmAnimRest *r, uint32_t q[4], const uint32_t m[16]);

/* 001C9D50(out, a, b, blend): out = the rotation of nlerp(quat(a),
 * quat(b), blend) with out row 3 xyz = a*(1 - blend) + b*blend and
 * out[15] = 1.0 (the other words as quat_to_mat3 leaves them). `blend` is
 * the raw f12 bits. Scratchpad: 0x700034C0/D0/E0 and 0x70003760.. */
int em_anim_rest_001C9D50(EmAnimRest *r, uint32_t out[16], const uint32_t a[16],
                          const uint32_t b[16], uint32_t blend);

/* 001C7900(m, token, vuaddr, chan). *first = the channel cursor as it was
 * on entry (the original's return value). */
int em_anim_rest_001C7900(EmAnimRest *r, const uint32_t m[16], uint32_t token, int32_t vuaddr,
                          int32_t chan, uint8_t **first);

/* 001CB2C0(owner, vuaddr, chan): owner is the EE address of the record. */
int em_anim_rest_001CB2C0(EmAnimRest *r, uint32_t owner, int32_t vuaddr, int32_t chan);

/* 001CAAC0(position, payload): position is the EE address of the world
 * point (a quadword read: the low four address bits are ignored, as the
 * original's 00102948 does). *key = the original's return value (the depth
 * key passed to 001CB760). */
int em_anim_rest_001CAAC0(EmAnimRest *r, uint32_t position, uint32_t payload, int32_t *key);

/* 001CACB0(owner): owner is the EE address of the record. */
int em_anim_rest_001CACB0(EmAnimRest *r, uint32_t owner);

/* 001CB5B0 anim_bone_array_setup(): *d275B40 = *d275B48 + 0x110 (the
 * callers' argument is not read). */
int em_anim_rest_001CB5B0(EmAnimRest *r);

/* ---- worker adapters ---- */

/* EmAnimRestWorkers.w_0011E748 over em_sdk_math_original_float_0011E748;
 * ctx = an EmSdkMathContext (docs/SDK_MATH_ORIGINAL.md section 7). A
 * recorded SDK fault (for example a negative argument with the soft-float
 * workers unbound) returns -1. */
int em_anim_rest_sqrt_0011E748(void *sdk_math_context, uint32_t x, uint32_t *result);

#ifdef __cplusplus
}
#endif

#endif /* EM_ANIM_RUNTIME_REST_H */
