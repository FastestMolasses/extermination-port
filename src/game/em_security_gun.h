/* The AREA11 security gun 0x825940, its power cable 0x827490 and the
 * flag-0x30 manager 0x823CE0 (census lane L24; docs/SECURITY_GUN.md, which
 * also holds the binding, and docs/SCRIPT_DOOR_FAN.md section 2 for the
 * translation notes). Oracle: tools/test_script_door_fan_reference.py.
 *
 * Identity (decomp build/workflows/verify-area11-husks.output.json, from the
 * code, the placement records and every AREA11 capture): 0x825940 is a
 * fixed wall/ceiling-mounted gun (a head bone 2 and a gun bone 3, a 60-unit
 * sight probe along the barrel, a lamp child 0x7A tinted by its state), and
 * 0x827490 is its cable, placed at the same point; shooting the cable
 * disables the gun (lifecycle 2) and sets the persistent taken bit +0x9A
 * (0x50). Earlier notes called them the "security gun" and "gun cable":
 * that label came from reading the cable's +0x03 byte 0x29 as library model
 * 0x29 (the crate's burst debris); the models are the per-area entries 8 and
 * 6 of D_0028A59C. 0x823CE0 starts the script 0x828C70 that sets D_00810788
 * (event flag 0x30) to 0xFF once that flag is non-zero; it is called the
 * flag-0x30 manager here after what its code does.
 *
 * Hand translations of the AREA11 overlay (id 9) functions at these runtime
 * addresses (splat names each 0x40 lower, because its vram base is the MWo3
 * header address; overlay file offset = runtime - 0x823500):
 *   0x00825940  security gun      (decomp func_overlay_AREA11_00825900.c)
 *   0x00827490  gun cable         (decomp func_overlay_AREA11_00827450.c)
 *   0x00823CE0  flag-0x30 manager (decomp func_overlay_AREA11_00823CA0.c)
 * They were first read from the listing; the decomp now holds byte-identical
 * C for all three (src/overlays/AREA11/, docs/AREA11_OVERLAY.md there),
 * which is their ground truth: the translations were re-read against it
 * line by line (chain step A11FIX) and agree.
 *
 * Scope of the gun translation. 0x825940 dispatches on +0x04. This module
 * translates lifecycle 0 (setup, including its 0x7A lamp child), 0x64
 * (dormant), 2 (disabled, both arms), 3 and every other value (free).
 * Lifecycles 4 (scan) and 1 (aim and fire), 0x825B74 and 0x826190, are NOT
 * translated here: they are entered only after the gun saw D_00810788
 * (event flag 0x30) == 0xFF, which only a return visit sets (an AREA17
 * script sets it to 1, then this manager's script sets 0xFF; every captured
 * AREA11 RAM image has the flag 0 and the gun in 0x64). Reaching them faults
 * (EM_GUN_FAULT_UNTRANSLATED at the lifecycle's entry) instead of running a
 * substitute. em_security_gun_rest.c holds a verified translation of them
 * (the decomp's C agrees) that the first level does not bind
 * (docs/SECURITY_GUN.md).
 *
 * Every record byte a function reads or writes is a field named by its
 * original offset. Every original callee is a worker named by its original
 * address; `ctx` identifies the actor. 001BA1C0 (a byte-matched leaf,
 * D_00810758[a1] == 0xFF) is translated inline and reads world->d810758.
 * A reached NULL worker or data pointer, or a negative worker result,
 * latches a fault (fail-stop) and the tick returns -1; a latched fault makes
 * every later tick return -1. Float arithmetic goes through em_ee_float.h on
 * bit patterns, as the COP1 instructions execute. stdint only. */
#ifndef EM_SECURITY_GUN_H
#define EM_SECURITY_GUN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_GUN 0x00825940u   /* placement callback -> +0x10 */
#define EM_GUN_CABLE 0x00827490u
#define EM_FLAG30_MANAGER 0x00823CE0u
#define EM_GUN_FLAG_30 0x30u          /* 001BA1C0 a1: D_00810788 */
#define EM_FLAG30_MANAGER_SCRIPT 0x00828C70u /* 0x823D8C: 001BA1A0 a1 */
#define EM_GUN_LAMP_CLASS 0x0C       /* 001AFA90 a0 for the 0x7A child */
#define EM_GUN_LAMP_MODEL 0x7A       /* child +0x0D */
#define EM_GUN_LAMP_HANDLER 0x001C5680u /* child +0x10 */
#define EM_GUN_CABLE_FX 0x80000045u /* 001EFE00 a0 */
#define EM_GUN_CABLE_HIT_SOUND 0x426
#define EM_GUN_CABLE_FALL_SOUND 0x427

enum {
    EM_GUN_FAULT_NONE = 0,
    EM_GUN_FAULT_NULL = 1,          /* reached worker or data pointer is NULL */
    EM_GUN_FAULT_WORKER_FAILED = 2, /* worker returned a negative value */
    EM_GUN_FAULT_UNTRANSLATED = 3,  /* gun lifecycle 1 or 4 */
    EM_GUN_FAULT_FREED = 4          /* ticked after the 001AFC10 free */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_GUN_FAULT_* */
} EmGunFault;

/* The 0x7A child node bytes the gun writes (and reads, lifecycle 2). */
typedef struct {
    uint8_t b03;          /* +0x03 = 0 */
    uint8_t b0D;          /* +0x0D = 0x7A */
    uint16_t h0E;         /* +0x0E = 0xFFFF */
    uint32_t handler_10;  /* +0x10 = 001C5680 */
    uint16_t h2E;         /* +0x2E = 0 */
    uint16_t h54, h56;    /* +0x54, +0x56 = 0 */
    uint8_t b9A;          /* +0x9A = 0 */
    float fA0[4];         /* +0xA0..+0xAC */
    uint32_t pos_B0[4];   /* +0xB0..+0xBC: quad copy (00102948) of the gun's */
    uint32_t rot_C0[4];   /* +0xC0..+0xCC: quad copy of the gun's */
    uint32_t bone3_11C;   /* +0x11C: bone slot 3 (read in lifecycle 2) */
} EmGunLamp;

/* The gun record bytes 0x825940 reads or writes. */
typedef struct {
    uint8_t b00;          /* +0x00 = 1 at setup */
    uint8_t lifecycle;    /* +0x04 */
    int16_t timer_28;     /* +0x28 = 300 + (300 * (rand >> 16)) >> 15 */
    uint32_t pos_B0[4];   /* +0xB0..+0xBC (read: copied into the child) */
    uint32_t rot_C0[4];   /* +0xC0..+0xCC (read: copied into the child) */
    uint32_t bone3_11C;   /* +0x11C: bone slot 3 (read in lifecycle 2) */
    float f1F4, f1F8, f1FC; /* +0x1F4 swing rate, +0x1F8 step, +0x1FC angle */
    uint32_t w200, w204, w208, w20C; /* +0x200..+0x20C */
    float f210, f214, f218;          /* +0x210..+0x218 */
    int32_t w21C;         /* +0x21C: countdown (the cable sets 0x5A) */
    uint32_t child_220;   /* +0x220: the child's original address, or 0 */
    int32_t w224;         /* +0x224 */
    uint8_t freed;        /* native: 1 after the 001AFC10 worker ran */
} EmGun;

/* The cable record bytes 0x827490 reads or writes. */
typedef struct {
    uint8_t b00;          /* +0x00: 1 at setup, 2 when hit */
    uint8_t lifecycle;    /* +0x04 */
    int16_t timer_28;     /* +0x28: fall counter */
    int16_t h34;          /* +0x34 = 1 at setup */
    int16_t hit_36;       /* +0x36: read; non-zero = shot */
    uint8_t item_9A;      /* +0x9A: persistent taken-bit id (read) */
    uint8_t freed;
} EmGunCable;

/* The record at cable +0x18 (the gun) as the cable writes it. */
typedef struct {
    uint8_t lifecycle;    /* +0x04 = 2 */
    int32_t w21C;         /* +0x21C = 0x5A */
} EmGunLinked;

/* The manager record bytes 0x823CE0 reads or writes. */
typedef struct {
    uint8_t b00;          /* +0x00 = 1 */
    uint8_t lifecycle;    /* +0x04 */
    uint8_t phase;        /* +0x05 */
    int16_t h28;          /* +0x28 = 0 at the script start */
    uint16_t h2E;         /* +0x2E = 0xFFFF at the end */
    uint8_t freed;
} EmFlag30Manager;

/* Data the three owners reach outside their records. */
typedef struct {
    const uint8_t *d810758; /* D_00810758[256] event flags (001BA1C0, D_00810788) */
    uint32_t *d8106C8;      /* manager: &= 0xF1FFFF8F, |= 0x40 */
    uint8_t *d810808;       /* manager: = 0xFF at the end */
    float *spad3A20;        /* gun lifecycle 2: scratch float 0x70003A20 */
} EmGunWorld;

typedef struct EmGunWorkers {
    void *ctx;
    /* Shared tail callees. */
    int (*w_001C6380)(void *ctx);            /* TRS world matrix of the actor */
    int (*w_001B17A0)(void *ctx);            /* publication; byte result ignored */
    int (*w_draw_4C)(void *ctx);             /* jalr *(actor + 0x4C)(actor) */
    int (*w_001AFC10)(void *ctx);            /* pool free */
    /* 001B0FD0(actor) minus its +0x04 byte writes, which the module owns
     * (the em_fan_original convention): the model/bone setup of 001B0EA0 and,
     * when that returns 0, bone_init_default_1. *result is 001B0EA0's result:
     * 0 (module: +0x04 += 1) or 1 (bone cap exceeded; module: +0x04 = 3, the
     * 001B0EA0 store). Any other result faults. */
    int (*w_001B0FD0)(void *ctx, int32_t *result);

    /* Gun. */
    int (*w_00122BB8)(void *ctx, int32_t *value);          /* game rand */
    int (*w_0011E2A8)(void *ctx, float x, float *result);  /* SDK sinf */
    /* *(u32 *)(D_00275B40 + 4 * index): the current bone work array slot. */
    int (*r_00275B40)(void *ctx, uint32_t index, uint32_t *bone);
    /* *(float *)(bone + offset) = value (a bone record the slot names). */
    int (*s_bone_f32)(void *ctx, uint32_t bone, uint32_t offset, float value);
    int (*w_001A2370)(void *ctx, uint32_t matrix);          /* hull re-transform */
    int (*w_001AFA90)(void *ctx, uint8_t cls, uint32_t *node, EmGunLamp **view);
    /* The node at gun +0x220 (lifecycle 2 writes and reads it); a NULL
     * view faults. */
    int (*r_child_220)(void *ctx, uint32_t child, EmGunLamp **view);
    int (*w_00102958)(void *ctx, uint32_t dst, uint32_t src); /* matrix copy */

    /* Cable. */
    int (*w_001B11E0)(void *ctx, uint8_t id, int32_t *result); /* taken bit test */
    int (*w_001B1190)(void *ctx, uint8_t id);                  /* taken bit set */
    int (*w_001EFE00)(void *ctx, uint32_t fx, int32_t *result); /* FX spawn at actor */
    int (*w_001FBD50)(void *ctx, int32_t cue, int32_t a2, float range);
    /* The record at cable +0x18; NULL faults when the cable writes it. */
    int (*r_link_18)(void *ctx, EmGunLinked **linked);

    /* Manager. */
    int (*w_001BA1A0)(void *ctx, uint32_t entry);          /* script start (+0x1F0) */
    int (*w_001BA1F0)(void *ctx, int32_t *result);         /* script poll */
    int (*w_001D2830)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001C1DC0)(void *ctx);
    int (*w_001FABB0)(void *ctx);
    int (*w_001AEE10)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001C4760)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001FAE70)(void *ctx, int32_t a0);
} EmGunWorkers;

/* One call of 0x825940. Returns 1 while allocated, 0 after the free, -1 on a
 * fault. */
int em_gun_tick(EmGun *gun, const EmGunWorld *world, const EmGunWorkers *w, EmGunFault *fault);

/* One call of 0x827490. Returns 1 while allocated, 0 after the free, -1. */
int em_gun_cable_tick(EmGunCable *cable, const EmGunWorkers *w, EmGunFault *fault);

/* One call of 0x823CE0. Returns 1 while allocated, 0 after the free, -1. */
int em_flag30_manager_tick(EmFlag30Manager *manager, const EmGunWorld *world, const EmGunWorkers *w,
                           EmGunFault *fault);
/* Shared original story-flag leaf. The caller validates the indexed byte
 * belongs to its canonical progress storage before passing this view. */
int em_gun_flag_done_001BA1C0(const EmGunWorld *,unsigned index,EmGunFault *,int *done);

#ifdef __cplusplus
}
#endif

#endif /* EM_SECURITY_GUN_H */
