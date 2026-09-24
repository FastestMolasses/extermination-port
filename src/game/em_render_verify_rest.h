/* em_render_verify_rest.h - translations of the last census rows of lanes
 * L31 (background / weather / area load), L37 (SDK math leaves), L29b
 * (shadow GS) and the L20 missing row (docs/RENDER_VERIFY_REST.md).
 *
 * Translations of the original routines, not models of them:
 *   L31  001C1DC0  area render init: eight 001D2830 flag registrations,
 *                  then 001C1E70, 001C1E80, 001C1E90, 001C1EA0, 001C1F50
 *                  on the block D_008101D0
 *        001C1E70  thunk to 001D52E0 (level cell grid publish)
 *        001C1E80  thunk to 001D8FD0 (area fog)
 *        001C1E90  returns (empty)
 *        001C1F50  per-area render flags 0x20..0x23/0x25, the background
 *                  TEX0 (001E2260), the colour (001E2270(&D_00250F30)) and
 *                  the area 0x1500 extra tag (001E2280)
 *        001E2260  ctx+0x1D0 = the 64-bit tag
 *        001E2270  ctx+0x1C0..0x1CF = the 16 bytes at the argument
 *        001E2280  ctx+0x1E0 = the 64-bit tag (reached only for area
 *                  0x1500; not on the census route, translated because
 *                  001C1F50 can reach it)
 *        001E0CF0  per-frame background/weather list build: 001E0CC0,
 *                  then under flag 0x20: flag 0x21 -> ctx+0x1D8 =
 *                  001E1E60(ctx+0x180, 3); flag 0x22 -> ctx+0x1E8 =
 *                  001E1AD0(ctx+0x1E0, 3)
 *        001C22A0  model/skeleton bind of a library entity (table
 *        001C2360  D_0028A59C / D_0028A56C): 001C6120, 001CA5E0,
 *                  001C6150 -> +0xC, the D_00275BCC cap, the +0x110 bone
 *                  pointers from 001AF780, +9, 001CB5B0, 001C62C0
 *   L37  001027E0  VU0 rigid inverse of a 4x4 row-vector matrix
 *        00102850  VU0 vector / scalar (Q = 1 / s, then v * Q)
 *        001000E0  001274B0(a, b) <= 0 (the soft-float double compare)
 *   L20  001FCF10  001FCB90(0x10E, 0xCC, 5, 0)
 *   L29b 001D4B50  001D49D0(obj) then 001D4B10(obj)
 *        001DA1E0  the 0x80-byte CNT/DIRECT record at a channel cursor
 *        001DA290  001D1F80(a0, 2, 9) then 001DA1E0(a0, copy of
 *                  D_002531D0, a1) (the only caller of 001DA1E0; its
 *                  packet content, next to em_shadow_original's worker)
 *
 * Conventions (the house style of em_player_stage_workers.h):
 *   - Every original callee that is not translated here is an explicit
 *     worker. A routine checks, before its first write, that every worker
 *     it can reach is bound, and latches a fault (-1) otherwise; a worker
 *     returning a negative value is a fault too, and writes made before it
 *     stay, as the original order leaves them. Once a fault is latched in
 *     an EmRvrFault every later call that is given it returns -1.
 *   - Original records are byte views addressed by their original offsets.
 *     An access outside a view faults (EM_RVR_FAULT_BAD_INPUT) before the
 *     write that would need it.
 *   - Float arithmetic is em_ee_float.h (docs/EE_FLOAT_MODEL.md) on bit
 *     patterns; a VU0 form the model refuses faults
 *     (EM_RVR_FAULT_UNMEASURED) instead of guessing.
 *
 * Oracle: tools/test_render_verify_rest_reference.py executes the original
 * instructions of every routine above (COP1 and VU0 through
 * tools/ee_float_model.py) and compares every write and every worker call.
 * It also verifies, by per-function interception inside the original
 * 001DA6A0, the em_shadow_original.c translations of 001DA080, 001DA310
 * and 001D5C80 (census L29b). */
#ifndef EM_RENDER_VERIFY_REST_H
#define EM_RENDER_VERIFY_REST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_RVR_FAULT_NONE = 0,
    EM_RVR_FAULT_NULL_WORKER = 1,   /* a reachable worker or view is NULL */
    EM_RVR_FAULT_WORKER_FAILED = 2, /* a worker returned a negative value */
    EM_RVR_FAULT_BAD_INPUT = 3,     /* an access outside a record view */
    EM_RVR_FAULT_UNMEASURED = 4     /* em_ee_float.h refused a VU0 form */
};

typedef struct {
    uint32_t address; /* original function (or instruction) address */
    int32_t code;     /* EM_RVR_FAULT_* */
} EmRvrFault;

/* The render context D_00275670 points to, as its original bytes from +0.
 * The routines here touch +0x10.. (channel cursors), +0x1C0..+0x1D7,
 * +0x1E0..+0x1E7 and the words +0x1D8 / +0x1E8. */
typedef struct {
    uint8_t *bytes;
    uint32_t size;
} EmRvrRenderContext;

/* A window of EE memory holding display-list records: `bytes` is the
 * content of original addresses base .. base + size - 1. */
typedef struct {
    uint8_t *bytes;
    uint32_t base;
    uint32_t size;
} EmRvrMemory;

/* ======================================================================
 * L31: area render init (001C1DC0 family) and the background build
 * ====================================================================== */

#define EM_RVR_BLOCK_008101D0 0x008101D0u  /* the block 001C1DC0 passes */
#define EM_RVR_COLOUR_00250F30 0x00250F30u /* 001C1F50's 001E2270 source */

typedef struct {
    void *ctx;
    /* 001D2830(flag, value): render flag registration (census L32). */
    int (*w_001D2830)(void *ctx, int32_t flag, int32_t value);
    /* 001E2260(tag): bind to em_rvr_001E2260 over the render context. */
    int (*w_001E2260)(void *ctx, uint64_t tag);
    /* 001E2270(address): bind to em_rvr_001E2270 with the 16 bytes at
     * `address` (EM_RVR_COLOUR_00250F30, boot ELF data). */
    int (*w_001E2270)(void *ctx, uint32_t address);
    /* 001E2280(tag): area 0x1500 only (never reached in AREA11); bind to
     * em_rvr_001E2280 over the render context. */
    int (*w_001E2280)(void *ctx, uint64_t tag);
    /* 001D52E0(): level cell grid publish (census L30). */
    int (*w_001D52E0)(void *ctx);
    /* 001D8FD0(): area fog (em_fog_gs). */
    int (*w_001D8FD0)(void *ctx);
    /* 001C1EA0(block): the weather spawn (live:
     * em_area11_spawn_weather_001C1EA0). */
    int (*w_001C1EA0)(void *ctx, uint32_t block);
} EmRvrAreaWorkers;

/* d810700 points at the two bytes D_00810700 (area), D_00810701 (sub);
 * every key read re-reads them, as the original does after its calls. */
int em_rvr_001C1F50(const uint8_t d810700[2], const EmRvrAreaWorkers *w, EmRvrFault *fault);
int em_rvr_001C1E70(const EmRvrAreaWorkers *w, EmRvrFault *fault);
int em_rvr_001C1E80(const EmRvrAreaWorkers *w, EmRvrFault *fault);
int em_rvr_001C1E90(EmRvrFault *fault);
int em_rvr_001C1DC0(const uint8_t d810700[2], const EmRvrAreaWorkers *w, EmRvrFault *fault);

/* 001E2260 / 001E2270 / 001E2280: stores into the render context. */
int em_rvr_001E2260(EmRvrRenderContext *rc, uint64_t tag, EmRvrFault *fault);
int em_rvr_001E2270(EmRvrRenderContext *rc, const uint8_t src[16], EmRvrFault *fault);
int em_rvr_001E2280(EmRvrRenderContext *rc, uint64_t tag, EmRvrFault *fault);

typedef struct {
    void *ctx;
    /* 001E0CC0() (census L30). */
    int (*w_001E0CC0)(void *ctx);
    /* 001D2910(flag): *result = its return (the flag's state). */
    int (*w_001D2910)(void *ctx, int32_t flag, int32_t *result);
    /* 001E1E60(ctx + offset, channel) / 001E1AD0(ctx + offset, channel):
     * *result = the returned word the routine stores. */
    int (*w_001E1E60)(void *ctx, uint32_t ctx_offset, int32_t channel, uint32_t *result);
    int (*w_001E1AD0)(void *ctx, uint32_t ctx_offset, int32_t channel, uint32_t *result);
} EmRvrBackgroundWorkers;

int em_rvr_001E0CF0(EmRvrRenderContext *rc, const EmRvrBackgroundWorkers *w, EmRvrFault *fault);

/* 001C22A0 / 001C2360. `self` is the entity record (its original bytes
 * from +0, `size` readable/writable), `self_address` its original address
 * (what the original passes to 001CA5E0 / 001C62C0). */
typedef struct {
    void *ctx;
    /* 001C6120(bank, code): *result = the model record's address. */
    int (*w_001C6120)(void *ctx, uint32_t bank, uint32_t code, uint32_t *result);
    /* 001CA5E0(self, model, mode). */
    int (*w_001CA5E0)(void *ctx, uint8_t *self, uint32_t self_address, uint32_t model, int32_t mode);
    /* 001C6150(value): *result = its return word (the routine stores the
     * low byte). */
    int (*w_001C6150)(void *ctx, uint32_t value, uint32_t *result);
    /* 001AF780(): *result = the bone record's address. */
    int (*w_001AF780)(void *ctx, uint32_t *result);
    /* 001CB5B0 anim_bone_array_setup(count). */
    int (*w_001CB5B0)(void *ctx, int32_t count);
    /* 001C62C0 bone_init_default_1(self). */
    int (*w_001C62C0)(void *ctx, uint8_t *self, uint32_t self_address);
} EmRvrModelWorkers;

/* table_word = D_0028A59C[0] (001C22A0) or D_0028A56C[0] (001C2360);
 * d275BCC = the signed halfword D_00275BCC. *result = the return (0, or 1
 * when +0xC exceeds the cap). */
int em_rvr_001C22A0(uint8_t *self, uint32_t size, uint32_t self_address, uint32_t table_word,
                    int16_t d275BCC, const EmRvrModelWorkers *w, int32_t *result, EmRvrFault *fault);
int em_rvr_001C2360(uint8_t *self, uint32_t size, uint32_t self_address, uint32_t table_word,
                    int16_t d275BCC, const EmRvrModelWorkers *w, int32_t *result, EmRvrFault *fault);

/* ======================================================================
 * L37: SDK math leaves (words are binary32 bit patterns)
 * ====================================================================== */

/* 001027E0(out, in): out rows 0..2 = the transposed 3x3 of in (w lanes 0),
 * out row 3 = (-(in row 3 x the transpose), in[15]). out may alias in.
 * 0, or -1 (EM_RVR_FAULT_UNMEASURED at the refusing instruction). */
int em_rvr_001027E0(uint32_t out[16], const uint32_t in[16], EmRvrFault *fault);

/* 00102850(out, v, s): Q = 1.0 / s (VDIV, form (3,0)), out = v * Q on all
 * four lanes. out may alias v. */
int em_rvr_00102850(uint32_t out[4], const uint32_t v[4], uint32_t s, EmRvrFault *fault);

/* 001000E0(a, b): 1 when em_sdk_soft_float_001274B0(a, b) <= 0, else 0.
 * a and b are the full 64-bit argument registers (the caller 0017C580
 * passes the 00128350 double unchanged). */
int32_t em_rvr_001000E0(uint64_t a, uint64_t b);

/* ======================================================================
 * L20: 001FCF10
 * ====================================================================== */

typedef struct {
    void *ctx;
    /* 001FCB90(a0, a1, a2, a3) (census L20 stand-in row). */
    int (*w_001FCB90)(void *ctx, int32_t a0, int32_t a1, int32_t a2, int32_t a3);
} EmRvrMessageWorkers;

int em_rvr_001FCF10(const EmRvrMessageWorkers *w, EmRvrFault *fault);

/* ======================================================================
 * L29b: 001D4B50, 001DA1E0 (+ its caller 001DA290)
 * ====================================================================== */

typedef struct {
    void *ctx;
    /* 001D49D0(obj) and 001D4B10(obj) (census: boundary, VIF packets of
     * the 0023E8A0 clip pass). obj = the object's original address. */
    int (*w_001D49D0)(void *ctx, uint32_t obj);
    int (*w_001D4B10)(void *ctx, uint32_t obj);
} EmRvrClipWorkers;

int em_rvr_001D4B50(const EmRvrClipWorkers *w, uint32_t obj, EmRvrFault *fault);

/* 001DA1E0(channel, payload, word): the record at the channel's cursor
 * (ctx + 0x10 + 4 * channel), which must lie in `mem`:
 *   +0 half 7, +3 byte 0x10, +4 word 0 (bytes +2 and +8..+0xF keep their
 *   value), +0x10..+0x1B zero, +0x1C 0x50000006, +0x20 dword
 *   0x5022400000008001, +0x28 dword 0x44441, +0x30..+0x3B zero, +0x3C
 *   `word`, +0x40..+0x7F the 64 payload bytes; the cursor advances by
 *   0x80 and *body = record + 0x10. */
int em_rvr_001DA1E0(EmRvrRenderContext *rc, EmRvrMemory *mem, int32_t channel,
                    const uint8_t payload[64], uint32_t word, uint32_t *body, EmRvrFault *fault);

typedef struct {
    void *ctx;
    /* 001D1F80(channel, a, b): state block push (em_load_veil_particles /
     * em_owner_services_original). */
    int (*w_001D1F80)(void *ctx, int32_t channel, int32_t a, int32_t b);
} EmRvrStateWorkers;

/* 001DA290(a0, a1): 001D1F80(a0, 2, 9) - the listing passes its own a0
 * through (the decomp C of this asm-linked unit says 0; 001DA6A0 passes 0,
 * so the two agree on the shadow route) - then 001DA1E0(a0, copy of the
 * template, a1). template = the 64 bytes of D_002531D0 (boot ELF data,
 * supplied by the binder). */
int em_rvr_001DA290(EmRvrRenderContext *rc, EmRvrMemory *mem, const EmRvrStateWorkers *w,
                    const uint8_t template_2531D0[64], int32_t a0, uint32_t a1, EmRvrFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_RENDER_VERIFY_REST_H */
