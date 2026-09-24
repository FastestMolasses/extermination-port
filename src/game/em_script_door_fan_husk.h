/* AREA11 overlay owners of census lane L24-fan-husk (lane "script-door-fan"):
 * the husk creature 0x825940, its partner 0x827490 and the second cinematic
 * manager 0x823CE0. Docs: docs/SCRIPT_DOOR_FAN.md. Oracle:
 * tools/test_script_door_fan_reference.py.
 *
 * Hand translations of the AREA11 overlay (id 9) functions at these runtime
 * addresses (the splat listing names each 0x40 lower, because its vram base
 * is the MWo3 header address; overlay file offset = runtime - 0x823500):
 *   0x00825940  husk creature   (listing func_overlay_AREA11_00825900)
 *   0x00827490  husk partner    (listing func_overlay_AREA11_00827450)
 *   0x00823CE0  manager r11     (listing func_overlay_AREA11_00823CA0)
 * No decomp C exists for any of them; they were read from the listing.
 *
 * Scope of the creature translation. 0x825940 dispatches on +0x04. This
 * module translates lifecycle 0 (setup, including its 0x7A child), 0x64
 * (dormant wait), 2 (both arms), 3 and every other value (free). Lifecycles
 * 1 and 4 (0x826190 and 0x825B74, about 1,150 instructions of the active
 * creature) are NOT translated: they are entered only after the creature saw
 * D_00810788 (event flag 0x30) == 0xFF, which no AREA11 first-visit script
 * sets (FIRST_LEVEL_AUDIT W17/INV-08; every captured AREA11 RAM image has
 * the flag 0 and the creature in 0x64). Reaching them faults
 * (EM_HUSK_FAULT_UNTRANSLATED at the lifecycle's entry) instead of running a
 * substitute.
 *
 * Every record byte a function reads or writes is a field named by its
 * original offset. Every original callee is a worker named by its original
 * address; `ctx` identifies the actor. 001BA1C0 (a byte-matched leaf,
 * D_00810758[a1] == 0xFF) is translated inline and reads world->d810758.
 * A reached NULL worker or data pointer, or a negative worker result,
 * latches a fault (fail-stop) and the tick returns -1; a latched fault makes
 * every later tick return -1. Float arithmetic goes through em_ee_float.h on
 * bit patterns, as the COP1 instructions execute. stdint only. */
#ifndef EM_SCRIPT_DOOR_FAN_HUSK_H
#define EM_SCRIPT_DOOR_FAN_HUSK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_HUSK_CREATURE 0x00825940u   /* placement callback -> +0x10 */
#define EM_HUSK_PARTNER 0x00827490u
#define EM_HUSK_MANAGER 0x00823CE0u
#define EM_HUSK_FLAG_30 0x30u          /* 001BA1C0 a1: D_00810788 */
#define EM_HUSK_MANAGER_SCRIPT 0x00828C70u /* 0x823D8C: 001BA1A0 a1 */
#define EM_HUSK_CHILD_CLASS 0x0C       /* 001AFA90 a0 for the 0x7A child */
#define EM_HUSK_CHILD_MODEL 0x7A       /* child +0x0D */
#define EM_HUSK_CHILD_HANDLER 0x001C5680u /* child +0x10 */
#define EM_HUSK_PARTNER_FX 0x80000045u /* 001EFE00 a0 */
#define EM_HUSK_PARTNER_HIT_SOUND 0x426
#define EM_HUSK_PARTNER_FALL_SOUND 0x427

enum {
    EM_HUSK_FAULT_NONE = 0,
    EM_HUSK_FAULT_NULL = 1,          /* reached worker or data pointer is NULL */
    EM_HUSK_FAULT_WORKER_FAILED = 2, /* worker returned a negative value */
    EM_HUSK_FAULT_UNTRANSLATED = 3,  /* creature lifecycle 1 or 4 */
    EM_HUSK_FAULT_FREED = 4          /* ticked after the 001AFC10 free */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_HUSK_FAULT_* */
} EmHuskFault;

/* The 0x7A child node bytes the creature writes (and reads, lifecycle 2). */
typedef struct {
    uint8_t b03;          /* +0x03 = 0 */
    uint8_t b0D;          /* +0x0D = 0x7A */
    uint16_t h0E;         /* +0x0E = 0xFFFF */
    uint32_t handler_10;  /* +0x10 = 001C5680 */
    uint16_t h2E;         /* +0x2E = 0 */
    uint16_t h54, h56;    /* +0x54, +0x56 = 0 */
    uint8_t b9A;          /* +0x9A = 0 */
    float fA0[4];         /* +0xA0..+0xAC */
    uint32_t pos_B0[4];   /* +0xB0..+0xBC: quad copy (00102948) of the creature's */
    uint32_t rot_C0[4];   /* +0xC0..+0xCC: quad copy of the creature's */
    uint32_t bone3_11C;   /* +0x11C: bone slot 3 (read in lifecycle 2) */
} EmHuskChild;

/* The creature record bytes 0x825940 reads or writes. */
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
    int32_t w21C;         /* +0x21C: countdown (the partner sets 0x5A) */
    uint32_t child_220;   /* +0x220: the child's original address, or 0 */
    int32_t w224;         /* +0x224 */
    uint8_t freed;        /* native: 1 after the 001AFC10 worker ran */
} EmHuskCreature;

/* The partner record bytes 0x827490 reads or writes. */
typedef struct {
    uint8_t b00;          /* +0x00: 1 at setup, 2 when hit */
    uint8_t lifecycle;    /* +0x04 */
    int16_t timer_28;     /* +0x28: fall counter */
    int16_t h34;          /* +0x34 = 1 at setup */
    int16_t hit_36;       /* +0x36: read; non-zero = shot */
    uint8_t item_9A;      /* +0x9A: persistent taken-bit id (read) */
    uint8_t freed;
} EmHuskPartner;

/* The record at partner +0x18 (the creature) as the partner writes it. */
typedef struct {
    uint8_t lifecycle;    /* +0x04 = 2 */
    int32_t w21C;         /* +0x21C = 0x5A */
} EmHuskLinked;

/* The manager record bytes 0x823CE0 reads or writes. */
typedef struct {
    uint8_t b00;          /* +0x00 = 1 */
    uint8_t lifecycle;    /* +0x04 */
    uint8_t phase;        /* +0x05 */
    int16_t h28;          /* +0x28 = 0 at the script start */
    uint16_t h2E;         /* +0x2E = 0xFFFF at the end */
    uint8_t freed;
} EmHuskManager;

/* Data the three owners reach outside their records. */
typedef struct {
    const uint8_t *d810758; /* D_00810758[256] event flags (001BA1C0, D_00810788) */
    uint32_t *d8106C8;      /* manager: &= 0xF1FFFF8F, |= 0x40 */
    uint8_t *d810808;       /* manager: = 0xFF at the end */
    float *spad3A20;        /* creature lifecycle 2: scratch float 0x70003A20 */
} EmHuskWorld;

typedef struct EmHuskWorkers {
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

    /* Creature. */
    int (*w_00122BB8)(void *ctx, int32_t *value);          /* game rand */
    int (*w_0011E2A8)(void *ctx, float x, float *result);  /* SDK sinf */
    /* *(u32 *)(D_00275B40 + 4 * index): the current bone work array slot. */
    int (*r_00275B40)(void *ctx, uint32_t index, uint32_t *bone);
    /* *(float *)(bone + offset) = value (a bone record the slot names). */
    int (*s_bone_f32)(void *ctx, uint32_t bone, uint32_t offset, float value);
    int (*w_001A2370)(void *ctx, uint32_t matrix);          /* hull re-transform */
    int (*w_001AFA90)(void *ctx, uint8_t cls, uint32_t *node, EmHuskChild **view);
    /* The node at creature +0x220 (lifecycle 2 writes and reads it); a NULL
     * view faults. */
    int (*r_child_220)(void *ctx, uint32_t child, EmHuskChild **view);
    int (*w_00102958)(void *ctx, uint32_t dst, uint32_t src); /* matrix copy */

    /* Partner. */
    int (*w_001B11E0)(void *ctx, uint8_t id, int32_t *result); /* taken bit test */
    int (*w_001B1190)(void *ctx, uint8_t id);                  /* taken bit set */
    int (*w_001EFE00)(void *ctx, uint32_t fx, int32_t *result); /* FX spawn at actor */
    int (*w_001FBD50)(void *ctx, int32_t cue, int32_t a2, float range);
    /* The record at partner +0x18; NULL faults when the partner writes it. */
    int (*r_link_18)(void *ctx, EmHuskLinked **linked);

    /* Manager. */
    int (*w_001BA1A0)(void *ctx, uint32_t entry);          /* script start (+0x1F0) */
    int (*w_001BA1F0)(void *ctx, int32_t *result);         /* script poll */
    int (*w_001D2830)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001C1DC0)(void *ctx);
    int (*w_001FABB0)(void *ctx);
    int (*w_001AEE10)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001C4760)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001FAE70)(void *ctx, int32_t a0);
} EmHuskWorkers;

/* One call of 0x825940. Returns 1 while allocated, 0 after the free, -1 on a
 * fault. */
int em_husk_creature_tick(EmHuskCreature *husk, const EmHuskWorld *world,
                          const EmHuskWorkers *w, EmHuskFault *fault);

/* One call of 0x827490. Returns 1 while allocated, 0 after the free, -1. */
int em_husk_partner_tick(EmHuskPartner *partner, const EmHuskWorkers *w, EmHuskFault *fault);

/* One call of 0x823CE0. Returns 1 while allocated, 0 after the free, -1. */
int em_husk_manager_tick(EmHuskManager *manager, const EmHuskWorld *world,
                         const EmHuskWorkers *w, EmHuskFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_SCRIPT_DOOR_FAN_HUSK_H */
