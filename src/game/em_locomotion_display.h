/* em_locomotion_display.h - the player's idle and walk states and the gait
 * display they drive (census lane L12-locomotion-display,
 * docs/LOCOMOTION_DISPLAY.md).
 *
 * Translations of the original routines (boot ELF SCUS-97112), read from the
 * original instructions (the decomp's build/asm) where the decomp C is a
 * NEARMISS, and from the byte-matched C otherwise:
 *   00161020  +4 = 1, +5 = 0 idle state   (0015B130 state[0]; NEARMISS)
 *   001612D0  +4 = 1, +5 = 1 walk state   (0015B130 state[1]; NEARMISS)
 *   0017C030  the +1F0 gait-display / skid dispatcher (byte-matched)
 *   0017B660  anim_matrix_player: the gait-tier cross-fade (NEARMISS)
 *   0017B5C0  the walk entry clip with an eight-frame blend (byte-matched)
 *   0017B490  the clip-table selector (byte-matched)
 *   0017B460  D_00248AB0[a][b] (byte-matched)
 *   00179D20  the per-node local pose seed (byte-matched)
 *   00179FF0  the per-node world matrices under the record's TRS (byte-matched)
 *   00182D40  +1F0 == 0x17 (byte-matched)
 * and, privately, the two SDK VU0 leaves the display reaches that no module
 * exports: 001026D0 (4x4 product) and 00103230 (row xyz times a scalar).
 * The other leaves are the verified translations, called directly:
 *   001029C0 / 00102C58   em_owner_services_identity_001029C0 / _euler_00102C58
 *   001CA0A0 / 001CA1C0   em_pose_host_001CA0A0 / em_pose_host_001CA1C0
 *   001C94B0              em_pose_host_build_trs_matrix
 *   001C9D50              em_anim_rest_001C9D50 (em_anim_runtime_rest.c)
 *   00102958 copy_qw4     a 64-byte copy (inline)
 *
 * The state routines work on the raw 0x320-byte player record
 * (EmPlayerLiveActor) by its original offsets. The display reaches memory by
 * EE address (the D_00275B40 node-pointer array, the node records, the two
 * pose buffers D_00288D40 / D_00287F40 and the ELF tables D_00248AB0 (and
 * its rows), D_00248740, D_00248870) through the pose host's EmPoseRegion
 * list, first region holding the whole range wins, exactly as
 * em_pose_host_workers maps them. No original data is embedded here.
 *
 * Every original callee that is not translated here is a worker
 * (EmLocoWorkers). Fail-stop: every entry point first checks that every
 * worker, scene pointer and display view it can reach is bound (and that the
 * display's shared scratch words are one copy), and returns -1 before its
 * first write otherwise. A worker that returns a negative value, a region
 * miss and a refused em_ee_float form are faults too: the routine stops at
 * once and returns -1, leaving the writes made before that point, as the
 * original order leaves them. `fault` records the original address.
 *
 * Arithmetic: every COP1 and VU0 operation goes through em_ee_float.h on
 * raw bit patterns (docs/EE_FLOAT_MODEL.md).
 *
 * Oracle: tools/test_locomotion_display_reference.py executes the original
 * instructions of every routine above (callees that are workers here are
 * hooked, scripted and recorded; the display leaves run unhooked) over a
 * synthetic skeleton and over the captured AREA11 player (the playable image
 * and the route beats), and compares every RAM and scratchpad byte, every
 * record byte, every return value and the worker call sequence. */
#ifndef EM_LOCOMOTION_DISPLAY_H
#define EM_LOCOMOTION_DISPLAY_H

#include <stdint.h>

#include "game/em_anim_runtime_rest.h"
#include "game/em_player_floor.h"
#include "game/em_pose_host_workers.h"

/* EE addresses of the data the routines read or write through the regions. */
#define EM_LOCO_D_00248AB0 0x00248AB0u /* 7 row pointers (0017B460) */
#define EM_LOCO_D_00248740 0x00248740u /* 0017B5C0: entry frame offset by +235 */
#define EM_LOCO_D_00248870 0x00248870u /* 001612D0: resume speed by +25C */
#define EM_LOCO_D_00287F40 0x00287F40u /* 0017B660: the new-tier pose, 0x40 per node */
#define EM_LOCO_D_00288D40 0x00288D40u /* 0017B660: the old-tier pose, 0x40 per node */
/* The two pose buffers are 0xE00 bytes apart: 56 nodes each. A record with
 * more nodes would make the original's copies overlap; that is refused. */
#define EM_LOCO_POSE_NODES_MAX 56u

/* The original callees that are not translated here. Each returns 0 (or its
 * result through the out pointer), or a negative value on a fault. */
typedef struct EmLocoWorkers {
    void *context;
    /* 001607D0(p): the weapon / action states; nonzero ends the callback. */
    int (*actions)(void *context, EmPlayerLiveActor *actor, int *result);
    /* 00160220(p): the ladder entry test; nonzero ends the callback. */
    int (*ladder)(void *context, EmPlayerLiveActor *actor, int *result);
    /* 00174AC0(p, arg): the turn toward the stick; *result is its return. */
    int (*heading)(void *context, EmPlayerLiveActor *actor, int arg, int *result);
    /* 00174A50(p, blend): the idle row request. */
    int (*row_request)(void *context, EmPlayerLiveActor *actor, float blend);
    /* 001749A0(p, clip, flags, blend). The return value is not read. */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int flags, float blend);
    /* anim_clip_arbiter 001749F0(p, clip, blend, frame). */
    int (*arbiter)(void *context, EmPlayerLiveActor *actor, int clip, float blend, float frame);
    /* 001C61D0(word +40, clip): the clip's frame count. */
    int (*clip_frames)(void *context, uint32_t bank, int clip, int32_t *frames);
    /* 001764E0(p): the radial wall probes. `s1` is the value of the
     * caller's $s1 at the call, which 001764E0 tests (& 4) without setting
     * it (em_player_floor.h EmPlayerProbeScene.inherited_s1): 00161020
     * leaves the stage's value there; 001612D0 does too, except after its
     * +6 = 2 resume path, which leaves the 0017B490 clip id. */
    int (*probes)(void *context, EmPlayerLiveActor *actor, uint32_t s1);
    /* 00175900(p, search): the floor service; *result is its return. */
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    /* 001756E0(p): the clearance release; *result is its return (unread). */
    int (*clearance)(void *context, EmPlayerLiveActor *actor, int *result);
    /* 001796C0(p): the fall check. */
    int (*fall_check)(void *context, EmPlayerLiveActor *actor);
    /* 0017BC40(p): the speed motor. */
    int (*motor)(void *context, EmPlayerLiveActor *actor);
    /* 00178B90(p, arg): the translation along +C4 by +38. */
    int (*translate)(void *context, EmPlayerLiveActor *actor, int arg);
    /* 00184BA0(p, arg): the Use scan; *result nonzero = a target took it. */
    int (*use_scan)(void *context, EmPlayerLiveActor *actor, int arg, int *result);
    /* 001798D0(p): the accepted-Use reset. */
    int (*use_accepted)(void *context, EmPlayerLiveActor *actor);
    /* 0017C540(p): the walk hand-off; 0017C440(p, arg): the re-entry. */
    int (*handoff)(void *context, EmPlayerLiveActor *actor);
    int (*reentry)(void *context, EmPlayerLiveActor *actor, int arg);
    /* 0017B910(p): the foot-stop entry (0017C030 +1F0 = 3, +25C != 3). */
    int (*foot_stop)(void *context, EmPlayerLiveActor *actor);
    /* 001FB9F0(id, a1, a2, a3): the sound start. The return is not read. */
    int (*sound)(void *context, int id, int a1, int a2, int a3);
    /* 001EFD90(id, p + B0, p + C0): the surface effect at the record's
     * position (+B0..) and rotation (+C0..). */
    int (*effect)(void *context, uint32_t id, EmPlayerLiveActor *actor);
    /* 001B1470(x): the angle wrap, raw bits in and out. */
    int (*wrap)(void *context, uint32_t x, uint32_t *out);
    /* 001B0070(): the global mode word 0017B490 tests (& 4). */
    int (*mode)(void *context, int32_t *value);
} EmLocoWorkers;

/* Words the states read outside the record. */
typedef struct EmLocoScene {
    const int16_t *d28A9A0;      /* D_0028A9A0 (00161020 +6 = 1 ends early when nonzero) */
    const uint16_t *d810E74;     /* D_00810E74: the Use button mask */
    const uint16_t *spad3B76;    /* 0x70003B76: the pad's pressed-this-frame bits */
    /* The caller's $s1 (0015B130 hands it down; em_player_floor.h
     * EmPlayerProbeScene.inherited_s1 is the same value). */
    const uint8_t *caller_s1;
} EmLocoScene;

/* The display's memory and shared scratch. */
typedef struct EmLocoDisplay {
    /* The regions (node records, the pose buffers, the ELF tables) and the
     * scratchpad words spad3400 / spad3440 / spad3600 / spad3760 / spad3A20
     * (the same storage as the pose host's). */
    EmPoseHost *pose;
    /* 001C9D50's context; its world.spad3760 must be pose->globals->spad3760
     * and its sqrt worker bound (checked before any write). */
    EmAnimRest *rest;
    /* D_00275B40: the EE address of the node-pointer array
     * (anim_bone_array_setup leaves the record's +110 there). */
    const uint32_t *d275B40;
} EmLocoDisplay;

typedef struct EmLocoHost {
    EmLocoWorkers workers;
    EmLocoScene scene;
    EmLocoDisplay display;
    uint32_t fault;   /* the original address of the first fault (0 = none) */
} EmLocoHost;

/* 1 when every worker, scene pointer and display view is bound and the
 * shared scratch is one copy. */
int em_loco_bound(const EmLocoHost *host);

/* ---- State callbacks (EmPlayerStateCallback; context = EmLocoHost) ------
 * Bind as EmPlayerStageWorkers.state[0] and state[1]. 0, or -1 on a fault. */
int em_loco_00161020(void *host, EmPlayerLiveActor *actor);   /* idle */
int em_loco_001612D0(void *host, EmPlayerLiveActor *actor);   /* walk */

/* ---- The display and its helpers ---------------------------------------- */
int em_loco_0017C030(EmLocoHost *host, EmPlayerLiveActor *actor);
int em_loco_0017B660(EmLocoHost *host, EmPlayerLiveActor *actor);
int em_loco_0017B5C0(EmLocoHost *host, EmPlayerLiveActor *actor);
int em_loco_00179D20(EmLocoHost *host, EmPlayerLiveActor *actor);
int em_loco_00179FF0(EmLocoHost *host, EmPlayerLiveActor *actor);
/* 0017B490(p, cmd, idx, tbl): *clip is the sign-extended halfword. cmd >= 7
 * is a fault (the original returns its caller's $s0 there). */
int em_loco_0017B490(EmLocoHost *host, const EmPlayerLiveActor *actor, int cmd, int idx, int tbl,
                     int16_t *clip);
/* 0017B460(a, b) = D_00248AB0[a][b]: the halfword at *(0x248AB0 + 4a) + 2b,
 * read through `pose`'s regions. */
int em_loco_0017B460(const EmPoseHost *pose, int a, int b, int16_t *value);
/* 00182D40(p): 1 when +1F0 == 0x17, else 0. */
int em_loco_00182D40(const EmPlayerLiveActor *actor);

/* The private SDK leaves, exported for the oracle (bit patterns). Each
 * returns 0 or -1 when em_ee_float refuses a form. */
int em_loco_001026D0(uint32_t dst[16], const uint32_t a[16], const uint32_t b[16]);
int em_loco_00103230(uint32_t dst[4], const uint32_t src[4], uint32_t scale);

#endif
