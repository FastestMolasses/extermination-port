#ifndef EM_AREA11_EFFECT_H
#define EM_AREA11_EFFECT_H

#include <stdint.h>

/* The AREA11 flame 008235F0 and its class-0xD contact behaviour 00823580
 * (overlay AREA11; byte-identical decomp C in the sibling decomp,
 * src/overlays/AREA11/func_overlay_AREA11_008235B0.c and
 * overlay_AREA11_func_00823540.c; docs/AREA11_EFFECT.md). The owner keeps
 * these fields in its pool record: state +0x04, flags +0x00, the block
 * +0x1F0 (half extents +0x1F0..+0x1F8, phase +0x204, seed +0x208, sound
 * handle +0x20C, contact cooldown +0x210), and the contact words +0x30 /
 * +0x34. `record` is the record's +0x14 word (its own address), read by
 * state 0. */
typedef struct EmArea11Effect {
    uint8_t state, flags;
    float phase, seed;
    int32_t sound_handle, contact_cooldown;
    float half_extent[3];
    uint32_t record;   /* +0x14 (read) */
    uint32_t w30;      /* +0x30: state 0 stores *(+0x14) + 0x1F0, the block above */
    uint32_t w34;      /* +0x34: state 0 stores the contact behaviour 0x823580 */
} EmArea11Effect;

#define EM_AREA11_EFFECT_CALLBACK 0x008235F0u
#define EM_AREA11_EFFECT_CONTACT 0x00823580u   /* the +0x34 behaviour */
#define EM_AREA11_EFFECT_SOUND_ID 0x413         /* 001FC3C0's a2 */
#define EM_AREA11_EFFECT_CONTACT_EFFECT 0x80000027u /* 001EFE00's a0 in 00823580 */

typedef enum EmArea11EffectCall {
    EM_AREA11_EFFECT_MATRIX,
    EM_AREA11_EFFECT_DRAW,
    EM_AREA11_EFFECT_SOUND,
    EM_AREA11_EFFECT_PUBLISH,
    EM_AREA11_EFFECT_STOP_SOUND,
    EM_AREA11_EFFECT_FREE
} EmArea11EffectCall;

/* Callbacks retain the original call order: MATRIX is 001029C0 /
 * 00102C58 / 00102918 over +0xD0, DRAW 001D04B0(+0xD0, 1, D_00828340,
 * phase, seed) (it sees the phase before its increment), SOUND
 * 001FC3C0(self, +0x20C, 0x413, 100, 4096) (it may change sound_handle),
 * PUBLISH 001B17A0(self), STOP_SOUND 001FC520(+0x20C) and FREE
 * 001AFC10(self). */
typedef void (*EmArea11EffectCallback)(void *context, EmArea11EffectCall call,
                                      EmArea11Effect *effect);
typedef uint32_t (*EmArea11EffectRandom)(void *context);
void em_area11_effect_tick(EmArea11Effect *effect,
                           EmArea11EffectRandom random,
                           EmArea11EffectCallback callback, void *context);

/* 00823580(self, obj): the +0x34 behaviour 001A8660 calls when the player
 * `obj` touches the flame. When bit 1 of obj[0] (`target_flags`) is clear
 * and 0021BB00(D_008102B0) returns 0, it calls 001EFE00(0x80000027, obj),
 * then stores obj[0x0F] = 0xC and self +0x210 = 60. The callees are
 * workers in that order (0021BB00 only when the bit is clear). 0, or -1
 * when a worker is missing or failed (*fault_address names it; nothing is
 * written after a failed callee). */
typedef struct {
    void *ctx;
    int (*w_0021BB00)(void *ctx, int32_t *result);   /* 0021BB00(D_008102B0) */
    int (*w_001EFE00)(void *ctx, uint32_t id);       /* 001EFE00(id, obj) */
} EmArea11EffectContactWorkers;
int em_area11_effect_contact(EmArea11Effect *effect, uint8_t target_flags,
                             const EmArea11EffectContactWorkers *workers,
                             uint8_t *target_reaction, uint32_t *fault_address);
#endif
