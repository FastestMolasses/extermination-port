/* AREA11 Roger actor: the lifecycle-0 initialisation of the Roger controller
 * (overlay 0x008237E0, case +0x04 == 0), its face attachment and face
 * services, and the equipment node that rides on Roger (area11[9], callback
 * 001C5C90). Docs: docs/ROGER_ACTOR_ORIGINAL.md.
 *
 * Hand translation of these original functions (boot ELF SCUS-97112 and the
 * AREA11 overlay), read from the byte-matched decomp C where it exists and
 * from the instructions where it does not:
 *   008237E0  lifecycle 0 only (the overlay controller; em_roger.c translates
 *             lifecycles 1..3 and returns -1 for 0)
 *   001BA1C0  story-slot test D_00810758[a1] == 0xFF
 *   001B10B0  model bind by table index + bone slot allocation
 *   001AF780  bone-slot pop (D_00275BD0 stack, D_00275BCC count; asm words)
 *   001AF890  bone-slot push (clears the 0xD0-byte slot)
 *   001C6150  model bone count (model +0x08)
 *   001CA6E0  -> 001CA5E0 -> 001CA5F0(kind 0): +0x44 = model, +0x4C = 001CAA00
 *   001CA6F0  +0x98 = a1
 *   001BA8E0  first-tick face attach (NEARMISS C; the instructions were
 *             followed, and they differ from that C: see the doc)
 *   001CA700  face slot: lazy 001AF780, +0x94, slot +0x60, 001D0690(slot +0x70)
 *   001D0690  face record reset (slot +0x70 .. +0xA7)
 *   001D06D0  face slot +0x81 = a1
 *   001D8BF0  +0x02 bit 0x20 set / clear
 *   001BA580  face update (NEARMISS C; checked against the instructions)
 *   001D06E0  face talking byte (slot +0x80) and target clear (+0x90..+0xA7)
 *   001D0C70  tail call of 001D0720 (the face kernel, a worker here)
 *   001BA540  face release: 001CA770 when +0x56, then 001D8BF0(0)
 *   001CA770  +0x90 slot release (001AF890), +0x90 = 0, +0x94 = -1
 *   001C5C90  the equipment node (byte-matched C), with the SDK leaves
 *             00102958 (copy_qw4), 001026A0, 001028D0 and 00102760 executed
 *             as their VU0 macro operations through em_ee_float.h.
 * 001B1020 is NOT re-translated: it is em_owner_services_001B1020 (reached
 * here as the w_001B1020 worker).
 *
 * Verified by tools/test_roger_actor_original_reference.py, which executes
 * the original instructions (VU0 arithmetic through tools/ee_float_model.py)
 * over captured original RAM and compares every modelled byte and every
 * worker call.
 *
 * Only original bytes these functions read or write are modelled; each
 * field names its original offset. Everything else they reach is an explicit
 * worker named by original address. A reached NULL worker or data view, a
 * negative worker result, a worker result outside the original range, or an
 * address outside the supplied views latches a fault (fail-stop) before any
 * later write; a latched fault makes every later call return -1.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_ROGER_ACTOR_ORIGINAL_H
#define EM_ROGER_ACTOR_ORIGINAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_ROGER_ACTOR_CONTROLLER 0x008237E0u   /* Roger record +0x10 */
#define EM_ROGER_ACTOR_EQUIPMENT 0x001C5C90u    /* area11[9] record +0x10 */
#define EM_ROGER_ACTOR_DESCRIPTOR 0x00828BD0u   /* 008237E0 stores it at +0x30 */
#define EM_ROGER_ACTOR_BANK_INDEX 0x4A          /* 001B10B0 a2 from 008237E0 */
#define EM_ROGER_ACTOR_W58_INDEX 0x4D           /* +0x58 = word at 0028A5C4 = D_0028A490[0x4D] */
#define EM_ROGER_ACTOR_CLIP 8                   /* 001C63E0 a1 from 008237E0 */
#define EM_ROGER_ACTOR_POSE_BONE 2              /* 001CA6F0 a1 from 008237E0 */
#define EM_ROGER_ACTOR_DRAW_001CAA00 0x001CAA00u /* 001CA5F0 kind 0 (+0x4C) */
#define EM_ROGER_ACTOR_SLOT_BYTES 0xD0u         /* 001AF890 clears 13 quadwords */
#define EM_ROGER_ACTOR_SLOT_MIN_FREE 31         /* 001AF780 refuses below this */
#define EM_ROGER_ACTOR_ACTIVITY_COUNT 10        /* D_008106D4[0..9] */

/* Bone slots +0x110 .. +0x1EF (56 words). The original has no bound; a
 * count past 56 would run into +0x1F0, which is not modelled, so it faults
 * (EM_ROGER_ACTOR_FAULT_BAD_INDEX). */
#define EM_ROGER_ACTOR_MAX_BONES 56

/* Fault codes (numerically the EM_SCENE_FAULT_* codes of em_scene_state.h). */
enum {
    EM_ROGER_ACTOR_FAULT_NONE = 0,
    EM_ROGER_ACTOR_FAULT_NULL_WORKER = 1,   /* reached worker or data view is NULL */
    EM_ROGER_ACTOR_FAULT_WORKER_FAILED = 2, /* worker returned a negative value */
    EM_ROGER_ACTOR_FAULT_BAD_RESULT = 3,    /* worker result outside the original range,
                                               a parent view that is not +0x18, or a
                                               draw method (+0x4C) other than 001CAA00 */
    EM_ROGER_ACTOR_FAULT_BAD_INDEX = 4,     /* address or index outside the views,
                                               or a call in the wrong lifecycle */
    /* em_ee_float.h refused a VU0 form. Every form executed here is in the
     * measured table, so this is unreachable unless the table changes. */
    EM_ROGER_ACTOR_FAULT_UNMEASURED_FORM = 6
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_ROGER_ACTOR_FAULT_* */
} EmRogerActorFault;

/* The record bytes these functions read or write (a 0x2F0-byte pool record;
 * both Roger and the equipment node). Floats are carried as raw bits. */
typedef struct EmRogerActorRecord {
    uint32_t address;    /* native: the record's original address */
    uint8_t status;      /* +0x00: 008237E0 lifecycle 0 writes 1 */
    uint8_t drawn;       /* +0x01: 001C5C90 writes 1; read from the parent */
    uint8_t cls;         /* +0x02: 001D8BF0 sets / clears bit 0x20 */
    uint8_t lifecycle;   /* +0x04 */
    uint8_t bones_held;  /* +0x09: 001B10B0 writes the bone count */
    uint8_t bone_count;  /* +0x0C: 001C6150(model) */
    uint8_t kind;        /* +0x0D: model kind (Roger 0x47, equipment 0x6B) */
    uint32_t w14;        /* +0x14: the record's self word (001F0120 reads it) */
    uint32_t parent;     /* +0x18: equipment: the Roger record's address */
    uint32_t descriptor; /* +0x30: 008237E0 writes 0x00828BD0 */
    uint32_t anim;       /* +0x40: D_0028A490[a2] (001B10B0 with a2 != -1) */
    uint32_t model;      /* +0x44: 001CA5E0 writes the model word */
    uint32_t draw;       /* +0x4C: 001CA5F0 writes the draw method address */
    int16_t face_active; /* +0x56: 001BA8E0 writes 1 (face attached) or 0 */
    uint32_t w58;        /* +0x58: 008237E0 writes D_0028A490[0x4D] */
    uint32_t face;       /* +0x90: the face slot address (001CA700), 0 = none */
    int16_t face_bone;   /* +0x94: 001CA700 a2 (7 for Roger); 001CA770 writes -1 */
    int16_t shadow_kind; /* +0x96: 001BA8E0 writes it; 001DA6A0 reads it */
    uint8_t pose_bone;   /* +0x98: 001CA6F0 */
    uint32_t fA0[4];     /* +0xA0..+0xAC: 001C5C90 writes va (w = 1.0) */
    uint32_t fB0[4];     /* +0xB0..+0xBC: 001C5C90 writes vb (w = 1.0) */
    uint32_t fC0[4];     /* +0xC0..+0xCC: 001C5C90 writes normalize(vb - va) */
    uint32_t bone[EM_ROGER_ACTOR_MAX_BONES]; /* +0x110: slot addresses */
} EmRogerActorRecord;

/* Canonical storage views. Each is required only on the path that reads or
 * writes it; the module keeps no copies. */
typedef struct EmRogerActorWorld {
    int16_t *d00275BCC;          /* free bone-slot count (signed halfword) */
    uint32_t *d00275BD0;         /* slot-stack cursor (an address word) */
    uint32_t *slot_stack;        /* the words the cursor walks ... */
    uint32_t slot_stack_base;    /* ... starting at this original address */
    uint32_t slot_stack_words;
    uint8_t *slots;              /* the bytes of the 0xD0-byte slots ... */
    uint32_t slots_base;         /* ... starting at this original address */
    uint32_t slots_size;
    const uint32_t *d0028A490;   /* resource table (words) */
    uint32_t d0028A490_count;
    const uint8_t *d00810758;    /* 001BA1C0 reads entry 0 */
    const uint8_t *d00810788;    /* 001BA8E0 gate byte */
    const uint8_t *d00810700;    /* 001BA580 area byte (category 6 only) */
    uint8_t *d008106D4;          /* activity bytes, EM_ROGER_ACTOR_ACTIVITY_COUNT */
    const uint32_t *d00275B40;   /* the words *D_00275B40 points at (the
                                    current record's +0x110, 001CB590) */
    uint32_t d00275B40_count;
    uint32_t *spad3600;          /* 0x70003600..0x7000360C (4 words) */
    /* Original bytes at a resource address (the model record 001C6150
     * reads). Returns a pointer to `size` readable bytes, or NULL. */
    const uint8_t *(*resource)(void *ctx, uint32_t address, uint32_t size);
    void *resource_ctx;
} EmRogerActorWorld;

/* Workers, one per original callee outside the translation. Each returns
 * >= 0 on success; a negative result faults. `ctx` is the adapter's. */
typedef struct {
    void *ctx;
    /* bone_init_default_2 (001C63E0)(actor, clip): 008237E0 passes 8. */
    int (*w_001C63E0)(void *ctx, EmRogerActorRecord *actor, int32_t clip);
    /* anim_bone_array_setup (001CB5B0)(count): D_00275B40 = D_00275B48 + 0x110. */
    int (*w_001CB5B0)(void *ctx, uint8_t count);
    /* 001F0120(owner, key) (em_head_sprite_original_spawn_001F0120 with
     * owner14 = actor +0x14). The original result is not used. */
    int (*w_001F0120)(void *ctx, uint32_t owner14, int32_t key);
    /* 001DA6A0(actor): drop shadow (em_shadow_original_001DA6A0). */
    int (*w_001DA6A0)(void *ctx, EmRogerActorRecord *actor);
    /* 001BA7F0(actor): the category-6 alternative when D_00810700 == 0xD
     * (not reached by the AREA11 owners). */
    int (*w_001BA7F0)(void *ctx, EmRogerActorRecord *actor);
    /* 001D0720(actor): the face kernel on the slot at actor +0x90 (reached
     * through the 001D0C70 tail call). */
    int (*w_001D0720)(void *ctx, EmRogerActorRecord *actor);
    /* 001B1020(actor, a1, a2, a3) (em_owner_services_001B1020 as it is,
     * including its own +0x04 stores). *result = its return value, 0 or 1;
     * 001C5C90 does not use it and stores +0x04 = 1 afterwards (001C5D0C). */
    int (*w_001B1020)(void *ctx, EmRogerActorRecord *actor, uint32_t a1, int32_t a2,
                      int32_t a3, int32_t *result);
    /* jalr *(actor +0x4C)(actor) with +0x4C == 001CAA00 (the only method
     * this worker stands for; any other +0x4C faults BAD_RESULT first). */
    int (*w_draw)(void *ctx, EmRogerActorRecord *actor);
    /* 001AFC10(actor): pool free. */
    int (*w_001AFC10)(void *ctx, EmRogerActorRecord *actor);
} EmRogerActorWorkers;

typedef struct {
    EmRogerActorWorld world;
    EmRogerActorWorkers workers;
    EmRogerActorFault fault; /* latched; cleared only by the caller */
} EmRogerActor;

/* Every function returns the original return value (0 for a void original;
 * em_roger_actor_001C5C90 excepted, see its comment below) or -1 when a
 * fault was latched (or one was already latched). */

/* 008237E0 with +0x04 == 0 (the caller dispatches +0x04 == 1..3 to
 * em_roger_tick). Any other +0x04 faults (BAD_INDEX). */
int em_roger_actor_008237E0_init(EmRogerActor *s, EmRogerActorRecord *roger);

/* 001BA1C0(actor, a1): 1 when D_00810758[a1] == 0xFF. */
int em_roger_actor_001BA1C0(EmRogerActor *s, uint32_t a1);
/* 001B10B0(actor, a1, a2): 1 over the bone cap (+0x04 = 3), else 0. */
int em_roger_actor_001B10B0(EmRogerActor *s, EmRogerActorRecord *a, uint32_t a1, int32_t a2);
/* 001AF780(): *slot = the popped slot address, or 0 when fewer than 31 are free. */
int em_roger_actor_001AF780(EmRogerActor *s, uint32_t *slot);
/* 001AF890(slot): clears the slot's 0xD0 bytes and pushes it back. */
int em_roger_actor_001AF890(EmRogerActor *s, uint32_t slot);
/* 001CA6E0(actor, model). */
int em_roger_actor_001CA6E0(EmRogerActor *s, EmRogerActorRecord *a, uint32_t model);
/* 001BA8E0(actor, kind). */
int em_roger_actor_001BA8E0(EmRogerActor *s, EmRogerActorRecord *a, uint32_t kind);
/* 001CA700(actor, resource, a2): 1 attached, 0 when no slot was free. */
int em_roger_actor_001CA700(EmRogerActor *s, EmRogerActorRecord *a, uint32_t resource,
                            int32_t a2);
/* 001D0690(record): `record` is a slot address + 0x70. */
int em_roger_actor_001D0690(EmRogerActor *s, uint32_t record);
/* 001D06D0(actor, a1) and 001D06E0(actor, a1). */
int em_roger_actor_001D06D0(EmRogerActor *s, EmRogerActorRecord *a, uint32_t a1);
int em_roger_actor_001D06E0(EmRogerActor *s, EmRogerActorRecord *a, uint32_t a1);
/* 001D8BF0(actor, a1). */
int em_roger_actor_001D8BF0(EmRogerActor *s, EmRogerActorRecord *a, int32_t a1);
/* 001BA580(actor, kind): the face update (em_roger.c EM_ROGER_FACE_UPDATE). */
int em_roger_actor_001BA580(EmRogerActor *s, EmRogerActorRecord *a, uint32_t kind);
/* 001BA540(actor): the face release (em_roger.c EM_ROGER_RELEASE_FACE). */
int em_roger_actor_001BA540(EmRogerActor *s, EmRogerActorRecord *a);
/* 001CA770(actor). */
int em_roger_actor_001CA770(EmRogerActor *s, EmRogerActorRecord *a);
/* 001C5C90(actor): one tick of the equipment node. `parent` is the record
 * at actor +0x18 (its address must equal actor->parent). Returns 1 while
 * the record is allocated, 0 after the 001AFC10 worker ran. */
int em_roger_actor_001C5C90(EmRogerActor *s, EmRogerActorRecord *actor,
                            const EmRogerActorRecord *parent);

#ifdef __cplusplus
}
#endif

#endif /* EM_ROGER_ACTOR_ORIGINAL_H */
