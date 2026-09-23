/* em_player_weapon_states_a.h - the player's action machine and the first
 * three armed stances of the FLOOR closure (docs/PLAYER_WEAPON_STATES_A.md).
 *
 * Translations of the original routines, not models of them (all four
 * decomp functions are byte-matched C):
 *   001607D0  the action machine on +1F0: the entries of +5 = 0x1D..0x22
 *             (R1 / R2 held, the two pressed buttons) and the fire /
 *             reload / draw forwarding of the armed modes 0x31/0x32/0x34/
 *             0x35. Called by 00161020 / 001612D0 (idle / walk), 0016B790
 *             and the stance handlers 00170A60 .. 001723D0.
 *   0016FCF0  +4 = 1, +5 = 0x1D: the R1 stance   (0015B130 case 29)
 *   001703E0  +4 = 1, +5 = 0x1E: the R2 stance   (0015B130 case 30)
 *   001729A0  +4 = 1, +5 = 0x1F                   (0015B130 case 31)
 *
 * Every routine works on the raw 0x320-byte player record
 * (EmPlayerLiveActor) by its original offsets. Every original callee is an
 * explicit worker except two byte-matched leaves translated in place:
 * copy_qw4 (00102958, a 64-byte copy) and 001031E0 (a 3-word copy). A
 * routine checks, before its first write, that the scene and every worker
 * it can reach are bound, and faults (-1) otherwise. A worker that returns
 * a negative value is a fault too: the routine stops at once and returns
 * -1, leaving the writes made before the call as the original order leaves
 * them. Arithmetic and float compares go through em_ee_float.h (the EE
 * COP1 model, docs/EE_FLOAT_MODEL.md) on raw bit patterns.
 *
 * Oracle: tools/test_player_weapon_states_a_reference.py executes the
 * original instructions (COP1 through tools/ee_float_model.py) with every
 * callee hooked and compares all 0x320 record bytes, the globals, the
 * return value and the worker call sequence with its arguments, over
 * synthetic records and over the player record of every captured route
 * beat. */
#ifndef EM_PLAYER_WEAPON_STATES_A_H
#define EM_PLAYER_WEAPON_STATES_A_H

#include <stdint.h>

#include "game/em_player_floor.h"

/* The two clip-id tables of the stance tops (halfwords, read with a sign
 * extension): +5 = 0x1D / 0x1E index D_00248B88 by +275, any other +5
 * indexes D_00248C68. */
#define EM_PLAYER_WEAPON_CLIPS_1D1E UINT32_C(0x00248B88)
#define EM_PLAYER_WEAPON_CLIPS_OTHER UINT32_C(0x00248C68)

/* Globals these routines read or write outside the record. The binder owns
 * one instance and must hand the SAME instance to every worker whose
 * original reads or writes one of these words during the call (for example
 * 00185A10 / 00185E30 / 00199220 / 0017ABA0 and D_008106E0, 0017ABA0 and
 * D_00810CA4): the routines read them where the original loads them, after
 * the calls that come before. */
typedef struct EmPlayerWeaponScene {
    /* 001607D0: the pad configuration masks in the scratchpad (0x70003B74,
     * 3B76, 3B78, 3B7C, 3B7E: +5 = 0x1D is entered on 3B7C held, 0x1E on
     * 3B7E held), the held and pressed pad words D_00810E70 /
     * D_00810E74, and the fire mode D_00810C61. Read only. */
    uint16_t spad3B74, spad3B76, spad3B78, spad3B7C, spad3B7E;
    uint16_t d810E70;
    uint16_t d810E74;
    uint8_t d810C61;
    /* The stance tops. D_008106E0[0] is the lock target (an original
     * address, 0 = none): 001703E0 clears it on entry, 0016FCF0 / 001729A0
     * clear it in state 0 and store the 00185A10 / 00185E30 result.
     * D_00810CA4[0] is read by 0016FCF0 / 001703E0 state 2. 0x70003A20 is
     * the scratch word the 0x63 blend stores the atan2 result into. */
    uint32_t d8106E0;
    uint8_t d810CA4;
    uint32_t spad3A20;
} EmPlayerWeaponScene;

/* The original callees. Each returns 0, or a negative value on a fault.
 * `actor` is the record the original passes in $a0. */
typedef struct EmPlayerWeaponWorkers {
    void *context;
    /* ---- 001607D0 ---- */
    /* 0016F5D0(p): the stance exit. */
    int (*w0016F5D0)(void *context, EmPlayerLiveActor *actor);
    /* 0017A8B0(p, a1), 0017A970(p, a1), 0017AAD0(p): *result is the return
     * value 001607D0 hands on (0017A8B0 / 0017A970) or tests (0017AAD0). */
    int (*w0017A8B0)(void *context, EmPlayerLiveActor *actor, int a1, int *result);
    int (*w0017A970)(void *context, EmPlayerLiveActor *actor, int a1, int *result);
    int (*w0017AAD0)(void *context, EmPlayerLiveActor *actor, int *result);
    /* 0017C370(p): lane player-stage-workers' em_player_0017C370 fits
     * through an adapter to its stage host. */
    int (*w0017C370)(void *context, EmPlayerLiveActor *actor);
    /* ---- the stance tops ---- */
    /* 0017B300(p, a1) and 0016F530(p, a1). */
    int (*w0017B300)(void *context, EmPlayerLiveActor *actor, int a1);
    int (*w0016F530)(void *context, EmPlayerLiveActor *actor, int a1);
    /* 001749A0(p, clip, flags, blend). The return value is not read. */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int flags, float blend);
    /* The halfword at table + 2 * index (table is one of the
     * EM_PLAYER_WEAPON_CLIPS_* addresses, index the byte +275). */
    int (*clip_id)(void *context, uint32_t table, unsigned index, int16_t *clip);
    /* anim_eval_skeleton (001C6DA0)(p) and anim_matrix_dispatch
     * (0017A130)(p). */
    int (*skeleton)(void *context, EmPlayerLiveActor *actor);
    int (*matrix)(void *context, EmPlayerLiveActor *actor);
    /* The 16 words at *(D_00275B40 + 4 * slot) + 0x90 (slot is always 4
     * here: the bone node whose matrix copy_qw4 copies into +2A0 and whose
     * +C0/+C4/+C8, words 12..14, the 0x64 blend copies into +2D0..+2D8),
     * as they are when the original loads them. */
    int (*bone)(void *context, unsigned slot, uint32_t words[16]);
    /* 0017ABA0(p): the aim steer. */
    int (*w0017ABA0)(void *context, EmPlayerLiveActor *actor);
    /* 00185A10(p, current) / 00185E30(p, current): *result is the returned
     * lock target, which the caller stores in D_008106E0[0]. */
    int (*w00185A10)(void *context, EmPlayerLiveActor *actor, uint32_t current, uint32_t *result);
    int (*w00185E30)(void *context, EmPlayerLiveActor *actor, uint32_t current, uint32_t *result);
    /* 00199220(p): the lock maintenance of the R2 stance (001703E0). */
    int (*w00199220)(void *context, EmPlayerLiveActor *actor);
    /* The per-weapon handlers the stance tops dispatch on +275 (0..5):
     * 00170A60(p, a1), 00171320(p), 00171670(p), 00171B00(p), 00171E90(p),
     * 001723D0(p); 00172860(p, rate) (001729A0 only); 0016F600(p) (state
     * 3, the holster). */
    int (*w00170A60)(void *context, EmPlayerLiveActor *actor, int a1);
    int (*w00171320)(void *context, EmPlayerLiveActor *actor);
    int (*w00171670)(void *context, EmPlayerLiveActor *actor);
    int (*w00171B00)(void *context, EmPlayerLiveActor *actor);
    int (*w00171E90)(void *context, EmPlayerLiveActor *actor);
    int (*w001723D0)(void *context, EmPlayerLiveActor *actor);
    int (*w00172860)(void *context, EmPlayerLiveActor *actor, float rate);
    int (*w0016F600)(void *context, EmPlayerLiveActor *actor);
    /* The words +C0 and +C8 of the object the record word +20 points to
     * (the same signature as EmPlayerStageCallees.link20). */
    int (*link20)(void *context, uint32_t word, uint32_t *c0, uint32_t *c8);
    /* SDK 0011E620 atan2(y, x), 001B1470(x) (angle wrap) and
     * 001B12B0(target, current, rate), raw bits in and out. */
    int (*atan2)(void *context, uint32_t y, uint32_t x, uint32_t *out);
    int (*wrap)(void *context, uint32_t x, uint32_t *out);
    int (*approach)(void *context, uint32_t target, uint32_t current, uint32_t rate,
                    uint32_t *out);
    /* 001FBD50(p, id, a2, radius). The return value is not read. */
    int (*sound)(void *context, EmPlayerLiveActor *actor, int id, int a2, float radius);
    /* 00174AC0(p, a1) (return value not read), 0017C440(p, a1), 0017C540(p),
     * 00178B90(p, a1). */
    int (*heading)(void *context, EmPlayerLiveActor *actor, int a1);
    int (*reentry)(void *context, EmPlayerLiveActor *actor, int a1);
    int (*handoff)(void *context, EmPlayerLiveActor *actor);
    int (*translate)(void *context, EmPlayerLiveActor *actor, int a1);
    /* The standing tail of 0016FCF0 / 001703E0: 001764E0(p) (the caller's
     * $s1 holds the record address at the call: 0016FD0C / 00170404),
     * 00175900(p, 1) (*result is its return value, not read here) and
     * 001796C0(p). */
    int (*probes)(void *context, EmPlayerLiveActor *actor);
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    int (*fall_check)(void *context, EmPlayerLiveActor *actor);
} EmPlayerWeaponWorkers;

/* The context of every entry point below. */
typedef struct EmPlayerWeaponStates {
    const EmPlayerWeaponWorkers *workers;
    EmPlayerWeaponScene *scene;
} EmPlayerWeaponStates;

/* 1 when the scene and every worker the routine can reach are bound. */
int em_player_weapon_001607D0_bound(const EmPlayerWeaponStates *states);
int em_player_weapon_0016FCF0_bound(const EmPlayerWeaponStates *states);
int em_player_weapon_001703E0_bound(const EmPlayerWeaponStates *states);
int em_player_weapon_001729A0_bound(const EmPlayerWeaponStates *states);

/* 001607D0(p). *result is its return value (0 or 1, or the value 0017A8B0 /
 * 0017A970 returned). Returns 0, or -1 on a fault (nothing written when a
 * worker or the scene is missing). */
int em_player_weapon_001607D0(void *states, EmPlayerLiveActor *actor, int *result);

/* The state callbacks (EmPlayerStateCallback); context is an
 * EmPlayerWeaponStates. Bind as EmPlayerStageWorkers.state[0x1D], [0x1E]
 * and [0x1F]. Each returns 0, or -1 on a fault. */
int em_player_weapon_state1D(void *states, EmPlayerLiveActor *actor);   /* 0016FCF0 */
int em_player_weapon_state1E(void *states, EmPlayerLiveActor *actor);   /* 001703E0 */
int em_player_weapon_state1F(void *states, EmPlayerLiveActor *actor);   /* 001729A0 */

#endif
