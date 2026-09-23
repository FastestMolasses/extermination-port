/* Original AREA11 truck set piece: overlay owners 00823FF0 (truck, placement
 * record 16, pool node #24) and 008251E0 (camera trigger, record 17, #25).
 *
 * Runtime addresses are overlay file offset + 0x823500 (the splat labels of
 * this overlay are 0x40 lower). Every constant below was read from those two
 * routines; tools/test_truck_original_reference.py executes the original
 * instructions and compares every field and every worker call.
 *
 * The truck is NOT armed by the trigger. The trigger only starts camera
 * script 0x8292C0 and, when the script ends, sets D_00810792 = 1. The truck
 * arms when the player's ground actor (player +0x214, D_008104C4) is nonzero,
 * player byte +0x0A (D_008102BA) is nonzero and that ground actor's byte
 * +0x0D is 9. Neither routine checks that the ground actor is the truck. */
#ifndef EM_TRUCK_ORIGINAL_H
#define EM_TRUCK_ORIGINAL_H

#include <stdint.h>

/* Original entry of camera script started by 008251E0 through 001BA1A0. */
#define EM_TRUCK_CAMERA_SCRIPT 0x8292C0u
/* 1EFD20 effect id used by every truck effect spawn. */
#define EM_TRUCK_EFFECT_ID 0x80000049u
/* 1FBD50 sound ids and radius (0x43960000). */
#define EM_TRUCK_SOUND_FALL_START 0x454u
#define EM_TRUCK_SOUND_FALL_END   0x455u

/* Truck owner fields, named by original actor offset. Matrices are the
 * original column-major 4x4 layout (translation at elements 12..14). */
typedef struct {
    uint8_t state;          /* +0x04: 0 init, 4 wedged, 1 falling, 2 rest, 3 free */
    uint8_t freed;          /* 1AFC10 returned the node to the pool */
    int16_t frame;          /* +0x28: fall beat counter (read signed) */
    float position[3];      /* +0xB0 */
    float rotation_x;       /* +0xC0: read only by the state-0 init */
    float matrix[16];       /* +0xD0 */
    float rest_matrix[16];  /* +0x1F0: copy of the placement matrix */
    float jitter_z;         /* +0x2DC */
    float jitter_x;         /* +0x2E0 */
    float rest_y;           /* +0x2E4: +0xB4 at init */
    float rest_rotation_x;  /* +0x2E8: +0xC0 at init (stored, never read) */
    int32_t shake;          /* +0x2EC: shake tick counter */
    /* Scratch D_700038A0..A8 as the last falling tick left it: the per-tick
     * velocity added to +0xB0 and (z only) to the carried player. */
    float velocity[3];
} EmTruckOriginal;

/* Camera trigger owner fields. */
typedef struct {
    uint8_t state;  /* +0x04: 0 init, 4 watching, 1 script running, 3 free */
    uint8_t armed;  /* +0x0B: set to 4 when the script starts */
    uint8_t freed;
} EmTruckTrigger;

/* Canonical original bytes the two owners read or write. Every pointer is
 * required except ground_kind, which is NULL exactly when D_008104C4 == 0.
 * The pointers name the canonical storage; the module keeps no copies. */
typedef struct {
    uint8_t *story;             /* D_00810792: 0, 1 after the camera script, 0xFF after the fall */
    const uint8_t *player_phase;/* D_008102B5 (player +0x05): trigger requires < 2 */
    const uint8_t *player_0a;   /* D_008102BA (player +0x0A): arm/rumble gate */
    const uint8_t *ground_kind; /* byte +0x0D of *D_008104C4 (player +0x214), NULL if none */
    float *player_a0;           /* D_00810350..58 (player +0xA0): trigger reads x,z; carry adds to z */
    const float *player_b0;     /* D_00810360..68 (player +0xB0): carry footprint x,z */
    int32_t *carry;             /* scratch 0x700031F0: set to 1 by the carry, never cleared here */
} EmTruckWorld;

/* Truck workers. Each returns 1 on success; any other value faults the tick. */
typedef struct {
    void *context;
    /* 001B0FD0: 1 while the model/bones are not bound (the tick then ends),
     * 0 once bound. Its own +4 increment is overwritten by the caller. */
    int (*model_bind)(void *, int *pending);
    /* 001C6380: placement TRS of +0xB0/+0xC0/+0x60 into +0xD0 and the bone slots. */
    int (*placement_matrix)(void *, float matrix[16]);
    /* 00102958(*D_00275B40 + 0x90, +0xD0): bone-0 pose matrix publish. */
    int (*pose)(void *, const float matrix[16]);
    /* 001A2370(actor, +0xD0): re-transform the hull, rebuild its AABB.
     * Translated: em_actor_collision_owner_hull (docs/ACTOR_COLLISION.md);
     * the captured uid-14 hull is the disc hull x the truck's +0xD0. */
    int (*hull)(void *, const float matrix[16]);
    /* Hull header of table *0x70003250 entry (+0xE >> 8): min xyz, max xyz,
     * as last rebuilt by 001A2370. Read before this tick's hull call.
     * Translated: em_actor_collision_owner_hull_bounds. */
    int (*hull_bounds)(void *, float bounds[6]);
    /* 001B1B70: em_actor_collision_owner_publish (class 4 -> 001B1D20). */
    int (*publish)(void *);
    int (*draw)(void *);                    /* virtual +0x4C */
    int (*rumble)(void *, int effect);      /* 001B1E20(effect, 0) */
    int (*effect)(void *, uint32_t id, const float position[4]); /* 001EFD20 */
    int (*sound)(void *, uint16_t id, float radius); /* 001FBD50(actor, id, 0, radius) */
    int (*free_owner)(void *);              /* 001AFC10 */
} EmTruckHooks;

typedef struct {
    void *context;
    int (*script_start)(void *, uint32_t entry); /* 001BA1A0(+0x1F0, entry) */
    int (*script_tick)(void *, int *done);       /* 001BA1F0(actor): done 0/1 */
    int (*free_owner)(void *);                   /* 001AFC10 */
} EmTruckTriggerHooks;

/* One pool-walk call of 00823FF0 / 008251E0. Return 1 allocated, 0 freed,
 * -1 fault (NULL argument, missing pointer or worker, worker failure). */
int em_truck_original_tick(EmTruckOriginal *, EmTruckWorld *, const EmTruckHooks *);
int em_truck_trigger_tick(EmTruckTrigger *, EmTruckWorld *, const EmTruckTriggerHooks *);

/* 008251E0 two-band X/Z union on player +0xA0 (x, z). */
int em_truck_trigger_bands(float x, float z);

/* SDK 00102B08 / 00102A60 (rotation about X / Z applied as R * src, using
 * the 001029E8 polynomial and VU truncation). dst may equal src. */
void em_truck_rotate_x(float dst[16], const float src[16], float angle);
void em_truck_rotate_z(float dst[16], const float src[16], float angle);

#endif
