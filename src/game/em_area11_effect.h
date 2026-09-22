#ifndef EM_AREA11_EFFECT_H
#define EM_AREA11_EFFECT_H

#include <stdint.h>

/* AREA11 runtime callback008235F0. The original owner stores these fields
 * at actor+4/+0 and work+14/+18/+1C/+20; work begins at actor+1F0. */
typedef struct EmArea11Effect {
    uint8_t state, flags;
    float phase, seed;
    int32_t sound_handle, contact_cooldown;
    float half_extent[3];
} EmArea11Effect;

typedef enum EmArea11EffectCall {
    EM_AREA11_EFFECT_MATRIX,
    EM_AREA11_EFFECT_DRAW,
    EM_AREA11_EFFECT_SOUND,
    EM_AREA11_EFFECT_PUBLISH,
    EM_AREA11_EFFECT_STOP_SOUND,
    EM_AREA11_EFFECT_FREE
} EmArea11EffectCall;

/* Callbacks retain the original call order. DRAW sees phase before its
 * increment; SOUND can change sound_handle. The host owns resource/matrix,
 * audio-handle and contact-system bindings, not this controller. */
typedef void (*EmArea11EffectCallback)(void *context, EmArea11EffectCall call,
                                      EmArea11Effect *effect);
typedef uint32_t (*EmArea11EffectRandom)(void *context);
void em_area11_effect_tick(EmArea11Effect *effect,
                           EmArea11EffectRandom random,
                           EmArea11EffectCallback callback, void *context);

/* 00823580's contact callback: target_flags is target+0; blocked is the
 * original0021BB00 player-state predicate. On acceptance, request effect
 * 80000027 and write target+0F=12. Collision candidate selection and the
 * attached effect's behavior are separate original routines. */
int em_area11_effect_contact(EmArea11Effect *effect, uint8_t target_flags,
                             int blocked, uint8_t *target_reaction);
#endif
