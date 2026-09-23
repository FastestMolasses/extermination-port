/* Original owner 001551B0: the AREA11 records area11[3..6] (model byte 6).
 *
 * What the code is (read off the instructions, not a label): a breakable,
 * stackable box. It never reads the player. It breaks when its +0x36
 * damage word is set (state 4), broadcasts a "support changed" byte (+0x0A)
 * to every listed box resting on something other than world ground
 * (+0x52 != 0, set by the INIT floor probe), and an alerted box re-probes
 * its four corners: still supported -> back to rest; otherwise it tips
 * 0.0524 rad per frame for 28 frames (or takes a random euler kick) and
 * falls under 0.052/frame gravity until the probe reports world ground,
 * then breaks without damage. A damage break of model 6/0x1E rebinds the
 * box to model 0x22/0x29 (the broken husk) and keeps drawing it.
 *
 * Every model, sound, effect, collision, allocation and draw operation is
 * an explicit host worker. The SDK matrix helpers the owner calls are
 * translated here (001029C0/001029E8/00102A60/00102B08/00102BB0/00102C58/
 * 001026A0/001026D0) and are exported for 00156620. */
#ifndef EM_CRATE_ORIGINAL_H
#define EM_CRATE_ORIGINAL_H

#include <stddef.h>
#include <stdint.h>

/* Actor fields 001551B0 reads or writes. Offsets are the original actor's. */
typedef struct {
    uint8_t status;          /* +0x00 */
    uint8_t model;           /* +0x03 model byte */
    uint8_t state;           /* +0x04 0 init,1 fall,2 break,3 free,4 rest */
    uint8_t fall_phase;      /* +0x05 */
    uint8_t break_phase;     /* +0x07 */
    uint8_t alarm;           /* +0x0A support-changed byte */
    uint8_t puid;            /* +0x9A */
    uint16_t placement;      /* +0x0E bit0 = owes its nest group a child */
    int16_t tilt;            /* +0x28 */
    int16_t timer;           /* +0x2A */
    int16_t health;          /* +0x34 */
    int16_t damage;          /* +0x36 */
    uint16_t raised;         /* +0x52 INIT floor probe != world ground */
    int16_t link;            /* +0x56 nest-group link */
    float position[4];       /* +0xB0 */
    float rotation[4];       /* +0xC0 */
    float world[16];         /* +0xD0; world[12..14] is +0x100..+0x108 */
    float rest[16];          /* +0x1F0 INIT snapshot of world */
    /* +0x230 step matrix. Its words 2/3 ARE +0x238/+0x23C, the rattle
     * wait/row ints of state 4: the original overlays them. */
    float step[16];
    float spin[3];           /* +0x2B8, +0x2BC, +0x2C0 */
    float velocity[3];       /* +0x2C4 (z), +0x2C8 (y), +0x2CC (x) */
    float probe[8];          /* +0x2D0..+0x2EC corner points, see below */
} EmCrateOriginal;

/* probe[] keeps the original order +0x2D0..+0x2EC. Corner i (0..3) is
 * x = probe[7-i], z = probe[3-i] (INIT 001553C8 layout). */

/* One live-list node (D_00275BC0 chain) for the state-4 broadcast. */
typedef struct {
    uint8_t model;          /* node +0x03 */
    uint16_t raised;        /* node +0x52 */
    uint8_t *alarm;         /* node +0x0A */
} EmCrateListEntry;

/* D_0024A850 / D_0024D820: groups[area][index] -> 0x2C-byte records,
 * little-endian, terminated by a record whose first halfword is -1. */
typedef struct {
    const int16_t *first_group;
    const uint8_t *const *const *groups;
    size_t area_count;
} EmCrateRegistry;

typedef struct {
    uint8_t area;              /* D_00810700 */
    uint8_t sub_area;          /* D_00810701 */
    /* 001AFD70 holds node->next in s0 when it calls the owner; INIT reads
     * that s0 uninitialised when +0x0E bit0 is set and link < 0. */
    int dispatch_has_next;
    const EmCrateListEntry *actors;   /* whole live list, may include self */
    size_t actor_count;
    const EmCrateRegistry *registry;  /* needed only when link >= 0 */
    /* D_002468B0 rows of 0x30 bytes: [row][column][B0,B4,B8]. */
    const float (*rattle)[4][3];
    size_t rattle_rows;
} EmCrateInput;

typedef struct {
    int32_t result;         /* 0019AB20 v0: 0, 2 (actor hull) or 4 (world) */
    int32_t actor;          /* D_700031D4 after the call: hit actor, 0 none */
} EmCrateProbe;

/* The child fields 001551B0 state 2 copies from a nest record. */
typedef struct {
    uint8_t class_id;       /* record +0x04, 001AFA90 argument */
    uint8_t puid;           /* -> +0x9A */
    uint8_t model;          /* -> +0x03 */
    uint16_t model_high;    /* -> +0x2E */
    uint8_t param;          /* -> +0x0D */
    uint8_t class_two;      /* (s16)record+4 & ~0xE0 == 2 */
    uint8_t sub_area;       /* -> +0x9D when class_two */
    uint8_t condition;      /* -> +0x9E when class_two */
    uint16_t placement;     /* -> +0x0E otherwise */
    int16_t kind, link;     /* -> +0x54, +0x56 */
    float position[3];      /* -> +0xB0.. parent + record +0x10.. */
    float rotation[3];      /* -> +0xC0.. record +0x1C.. */
    uint32_t behavior;      /* -> +0x10 record +0x28 */
} EmCrateChild;

/* Every worker returns >=0 on success and <0 to fault the owner. */
typedef struct {
    void *context;
    int (*allocate_model)(void *);          /* 001B0EA0: 1 not ready, 0 ready */
    int (*bone_init)(void *);               /* bone_init_default_1(self) */
    int (*publish)(void *);                 /* 001B1B70(self): em_actor_collision_owner_publish */
    int (*place)(void *, float world[16]);  /* 001C6380(self) */
    /* 0019AB20(self, from, {?,dy,?}, mode). position is the owner's +0xB0:
     * mode bit31 lets the worker snap position[1]. Translated:
     * em_actor_collision_owner_probe (docs/ACTOR_COLLISION.md). */
    int (*probe)(void *, float position[4], const float from[3], float dy,
                 uint32_t mode, EmCrateProbe *);
    int (*random)(void *, uint32_t *);      /* 00122BB8 */
    int (*sound)(void *, uint16_t id);      /* 001FC580(self, id) */
    int (*sound3d)(void *, uint16_t id, int32_t mode, float radius); /* 001FBD50 */
    int (*effect)(void *, uint32_t id, const float position[4],
                  const float rotation[4]); /* 001EFD90(?, pos, rot) */
    int (*taken)(void *, uint8_t puid);     /* 001B11E0: 0 not taken */
    int (*spawn)(void *, const EmCrateChild *); /* 001AFA90+copy: 1 allocated */
    int (*rebind)(void *, uint16_t model);  /* 001C6120(D_0028A56C,id)->001CA6E0 */
    int (*bone_matrix)(void *, const float world[16]); /* *D_00275B40 + 0x90 */
    int (*draw)(void *);                    /* actor +0x4C */
    int (*set_taken)(void *, uint8_t puid); /* 001B1190 */
    int (*free)(void *);                    /* 001AFC10 */
} EmCrateOriginalHooks;

enum { EM_CRATE_FAULT = -1, EM_CRATE_FREED = 0, EM_CRATE_ALIVE = 1 };

/* One 001551B0 call. Faults (-1) before any write when a worker is
 * missing, and on a worker fault, a missing registry/rattle view or a
 * registry/rattle index outside the view (the original would read
 * unrelated memory there). */
int em_crate_original_tick(EmCrateOriginal *, const EmCrateInput *,
                           const EmCrateOriginalHooks *);

/* The +0x238/+0x23C ints overlaid on step[2]/step[3]. */
int32_t em_crate_original_rattle_wait(const EmCrateOriginal *);
int32_t em_crate_original_rattle_row(const EmCrateOriginal *);
void em_crate_original_set_rattle(EmCrateOriginal *, int32_t wait, int32_t row);

/* Translated SDK helpers (EE truncating float semantics). */
float em_crate_sdk_add(float, float);
float em_crate_sdk_sub(float, float);
float em_crate_sdk_mul(float, float);
float em_crate_sdk_int_to_float(int32_t);          /* cvt.s.w */
int32_t em_crate_sdk_float_to_int(float);          /* float_to_int */
float em_crate_sdk_wrap(float);                    /* 001B1470 */
void em_crate_sdk_identity(float m[16]);           /* 001029C0 */
/* axis 0 = 00102B08 (X), 1 = 00102BB0 (Y), 2 = 00102A60 (Z). */
void em_crate_sdk_rotate(float out[16], const float in[16], float angle, int axis);
void em_crate_sdk_euler(float m[16], const float angles[3]); /* 00102C58 */
void em_crate_sdk_multiply(float out[16], const float left[16],
                           const float right[16]);  /* 001026D0(out,left,right) */
void em_crate_sdk_apply(float out[4], const float m[16], const float v[4]); /* 001026A0 */
void em_crate_sdk_translate(float out[16], const float in[16], const float v[3]); /* 00102918 */
float em_crate_sdk_dot3(const float a[4], const float b[4]); /* 00102738 */

#endif
