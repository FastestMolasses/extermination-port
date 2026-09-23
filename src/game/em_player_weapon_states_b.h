/* em_player_weapon_states_b.h - the player states +5 = 0x20, 0x21 and 0x22
 * (docs/PLAYER_WEAPON_STATES_B.md).
 *
 * Translations of the original routines, not models of them. Each works on
 * the raw 0x320-byte player record (EmPlayerLiveActor) by its original
 * offsets:
 *   00173000  +4 = 1, +5 = 0x20: the R2 aiming stance (0015B130 state[0x20])
 *   001735C0  +4 = 1, +5 = 0x21: the light three-hit melee combo
 *             (0015B130 state[0x21])
 *   00173E60  +4 = 1, +5 = 0x22: the heavy melee stab (0015B130 state[0x22])
 *   00173DD0  the heavy stab's in-swing yaw steer (private to 00173E60)
 *
 * Every other original callee is a worker (EmPlayerWeaponBWorkers). 00173000's
 * callees 0016F530, 0016F600, 00170A60, 00171320, 00171670, 00171B00,
 * 00171E90, 001723D0, 0017ABA0, 0017B300 and 00199220 are shared with the
 * other stance tops (0016FCF0, 001703E0, 001729A0), so they stay workers
 * here. A worker that is missing is a fault: each state callback checks the
 * workers that routine can call and returns -1 before its first write. A
 * worker that returns a negative value is a fault too: the routine stops at
 * once and returns -1, keeping the writes made before the call, as the
 * original order leaves them.
 *
 * Arithmetic: every COP1 operation goes through em_ee_float.h (the measured
 * EE model, docs/EE_FLOAT_MODEL.md), on raw bit patterns.
 *
 * Oracle: tools/test_player_weapon_states_b_reference.py executes the
 * original instructions of all four routines from the user's pinned ELF
 * (callees hooked and recorded) and compares all 0x320 actor bytes, the
 * linked target record, the globals and scratchpad words, and the worker
 * call sequence. */
#ifndef EM_PLAYER_WEAPON_STATES_B_H
#define EM_PLAYER_WEAPON_STATES_B_H

#include <stdint.h>

#include "game/em_player_floor.h"

/* The two aim-pose clip tables 00173000 reads through clip_id. */
#define EM_PLAYER_WEAPON_B_CLIPS_1D1E UINT32_C(0x00248B88)
#define EM_PLAYER_WEAPON_B_CLIPS_OTHER UINT32_C(0x00248C68)

/* The words these routines read or write outside the actor. Each pointer is
 * the binder's canonical storage for that original word, and the routines
 * read and write it where the original does (after any worker call before
 * that point). */
typedef struct EmPlayerWeaponBScene {
    uint32_t *d8106E0;           /* D_008106E0: 00173000 stores 0 on every call */
    const uint8_t *d810CA4;      /* D_00810CA4: the aim option (00173000 +6 = 2) */
    uint32_t *spad3A20;          /* 0x70003A20: 00173000 +6 = 0x63 (raw bits) */
    const uint16_t *pad_pressed; /* D_00810E74 (001735C0) */
    const uint16_t *spad3B78;    /* 0x70003B78: the button mask ANDed with it */
} EmPlayerWeaponBScene;

typedef struct EmPlayerWeaponBWorkers {
    void *context;
    EmPlayerWeaponBScene *scene;
    /* 001749A0(p, clip, force, blend); blend is the raw bit pattern the
     * original passes in $f12 (a literal, or the float at +1FC). */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int force,
                   uint32_t blend);
    /* 001FBD50(p, id, 0, 300.0): *handle is its return value; the melee
     * routines keep its low byte at +302. */
    int (*sound)(void *context, EmPlayerLiveActor *actor, int id, int *handle);
    /* 0011A070(handle): handle is the +302 byte (0..255). */
    int (*stop_sound)(void *context, int handle);
    /* 00174AC0(p, arg): *result is its return value. */
    int (*heading)(void *context, EmPlayerLiveActor *actor, int arg, int *result);
    /* 001B12B0(target, current, rate) and 001B1470(x), raw bits. */
    int (*approach)(void *context, uint32_t target, uint32_t current, uint32_t rate,
                    uint32_t *out);
    int (*wrap)(void *context, uint32_t x, uint32_t *out);
    /* 0011E620(y, x): atan2f, raw bits. */
    int (*atan2)(void *context, uint32_t y, uint32_t x, uint32_t *out);
    /* 00178B90(p, arg): the translation. */
    int (*translate)(void *context, EmPlayerLiveActor *actor, int arg);
    /* 001764E0(p): the wall probes. 001735C0 and 00173E60 leave $s1 as their
     * caller (0015B130) left it; 001764E0 tests ($s1 & 4) (PLAYER_FLOOR.md
     * P16), so the binder passes the stage's inherited $s1. */
    int (*probes)(void *context, EmPlayerLiveActor *actor);
    /* 00175900(p, search): *result is its return value. */
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    /* 001796C0(p): the fall check. */
    int (*fall_check)(void *context, EmPlayerLiveActor *actor);
    /* 0017C440(p, arg) and 0017C540(p): the walk re-entry and hand-off. */
    int (*reentry)(void *context, EmPlayerLiveActor *actor, int arg);
    int (*handoff)(void *context, EmPlayerLiveActor *actor);
    /* The melee target record the word at +18 addresses (D_008102C8 is the
     * same word): *record is its raw image, at least 0x38 bytes (the routines
     * write +0 and +36, and read +A). Called each time the original loads
     * +18. */
    int (*link18)(void *context, uint32_t word, uint8_t **record);
    /* The record the word at +20 addresses: its +C0 and +C8 words. */
    int (*link20)(void *context, uint32_t word, uint32_t *c0, uint32_t *c8);
    /* The 16 words at *(D_00275B40 + 4 * slot) + 0x90 (slot is always 4: the
     * bone node whose matrix copy_qw4 copies into +2A0 and whose +C0/+C4/+C8,
     * words 12..14, the 0x64 blend copies into +2D0..+2D8), as they are when
     * the original loads them. The same signature as
     * EmPlayerWeaponWorkers.bone (em_player_weapon_states_a.h). */
    int (*bone)(void *context, unsigned slot, uint32_t words[16]);
    /* The halfword at table + 2 * index: table is D_00248B88 (+5 0x1D /
     * 0x1E) or D_00248C68 (any other +5), index the byte +275. The same
     * signature as EmPlayerWeaponWorkers.clip_id. */
    int (*clip_id)(void *context, uint32_t table, unsigned index, int16_t *clip);
    /* anim_eval_skeleton (001C6DA0) and anim_matrix_dispatch (0017A130). */
    int (*skeleton)(void *context, EmPlayerLiveActor *actor);
    int (*matrix)(void *context, EmPlayerLiveActor *actor);
    /* 00173000's stance callees (shared with 0016FCF0 / 001703E0 / 001729A0).
     * At these calls $s1 holds the record address (00173000 keeps p there). */
    int (*reload)(void *context, EmPlayerLiveActor *actor, int arg);       /* 0017B300(p, 0) */
    int (*draw)(void *context, EmPlayerLiveActor *actor, int arg);         /* 0016F530(p, 0) */
    int (*reload_wait)(void *context, EmPlayerLiveActor *actor);           /* 0016F600(p) */
    int (*pose)(void *context, EmPlayerLiveActor *actor);                  /* 0017ABA0(p) */
    int (*acquire)(void *context, EmPlayerLiveActor *actor);               /* 00199220(p) */
    /* The fire sub-machines by +275 (jtbl_0026D6C0): 0 -> 00170A60(p, 1),
     * 1 -> 00171320, 2 -> 00171670, 3 -> 00171B00, 4 -> 00171E90,
     * 5 -> 001723D0 (each (p)). */
    int (*fire_00170A60)(void *context, EmPlayerLiveActor *actor, int arg);
    int (*fire_00171320)(void *context, EmPlayerLiveActor *actor);
    int (*fire_00171670)(void *context, EmPlayerLiveActor *actor);
    int (*fire_00171B00)(void *context, EmPlayerLiveActor *actor);
    int (*fire_00171E90)(void *context, EmPlayerLiveActor *actor);
    int (*fire_001723D0)(void *context, EmPlayerLiveActor *actor);
} EmPlayerWeaponBWorkers;

/* 1 when every worker (and scene word) the routine can reach is bound. */
int em_player_weapon_b_bound_state20(const EmPlayerWeaponBWorkers *workers);
int em_player_weapon_b_bound_state21(const EmPlayerWeaponBWorkers *workers);
int em_player_weapon_b_bound_state22(const EmPlayerWeaponBWorkers *workers);

/* The state callbacks (EmPlayerStateCallback): context is a
 * const EmPlayerWeaponBWorkers *. Bind as EmPlayerStageWorkers.state[0x20],
 * state[0x21] and state[0x22]. Each returns 0, or -1 on a fault. */
int em_player_weapon_b_state20(void *workers, EmPlayerLiveActor *actor);   /* 00173000 */
int em_player_weapon_b_state21(void *workers, EmPlayerLiveActor *actor);   /* 001735C0 */
int em_player_weapon_b_state22(void *workers, EmPlayerLiveActor *actor);   /* 00173E60 */

/* 00173DD0(p), exported for the oracle. It checks nothing itself: the
 * caller has checked heading and approach. */
int em_player_weapon_b_00173DD0(const EmPlayerWeaponBWorkers *workers, EmPlayerLiveActor *actor);

#endif
