/* em_player_equipment.h - the player's attached equipment nodes (pool
 * callback 0018A6B0) and the camera-mode map 0015D2F0.
 * Docs: docs/PLAYER_EQUIPMENT.md.
 *
 * Hand translation of the byte-matched decomp C of:
 *   0018A6B0  node behaviour: lifecycle +0x04 (0 init, 1 run, 2 idle,
 *             3 free) and, while running, the flavour dispatch on +0x03
 *             followed by the +0x4C draw method
 *   0018A8D0  node init: clip id from (+0x03, +0x0D), model bind, bone slots
 *   00188630  flavour 0: the view vectors +0xA0/+0xB0/+0xC0, the +0x1F0
 *             point and the per-mode one-shot on +0x2E
 *   00188A50  flavour 1: dispatch on +0x0D (0, 0x10, 0x15)
 *   00188AC0  flavour 1 variant 0: bone copy and the equipment-change
 *             respawn (0015C310(player, 1)) on +0x05
 *   00188B80  flavour 1 variant 0x10: bone copy from player bone 19 or 4
 *   00188DF0  flavour 2: dispatch on +0x0D (0, 1, 2, 3, 0xC), default
 *             placement through 001C9610
 *   00188ED0  flavour 2 variant 0: bone copies and the lamp gate (00187780)
 *   0018A1F0  flavour 4: bone copy from player bone 14, the hit probes
 *             while +0x00 bit 0 is set, then 00189D30
 *   00189D30  flavour 4 effect lifecycle on +0x07 (001EFF10 spawn)
 *   0015D2F0  the camera-mode code of the player record (0, 1, 2, 3, 0x82)
 * and the three plain copies they call: copy_qw4 (00102958, 64 bytes),
 * 00102948 (16 bytes) and 001031E0 (12 bytes).
 *
 * The seven nodes: 0015C420 spawns 0018A880(4, 0) (player +0x18) and
 * 0015C310(player, 0) spawns (0, 0) (player +0x20), (1, 0), (1, 0x10) and
 * three flavour-2 nodes from D_00810CA4..CA7 (docs/ORIGINAL_FRAME_ORDER.md
 * Q5). Each tick rewrites one or more of its own bone world matrices from a
 * player bone and draws through +0x4C.
 *
 * Record: EmPlayerEquipmentNode holds exactly the node bytes these routines
 * read or write, each at its original offset. Anything else is reached only
 * through workers that receive the node.
 *
 * Workers: every other original callee is an explicit worker named by its
 * address, including the SDK VU0 routines (001026A0, 001026D0, 001028B8,
 * 001028D0, 00102760, 001029C0, 00102BB0), which take their inputs by value
 * (the originals load every input before they store) and return the result
 * that the translation stores at the original destination. Callees that
 * receive a pointer the translation cannot size (001EFEB0, 001EFF10,
 * 001F00A0, 001F4010) get a pointer into the owned storage.
 *
 * Fail-stop: every entry point checks the whole worker set and every data
 * view it can reach and returns -1 before its first write when one is
 * missing. A negative worker result, a NULL bone slot or effect record the
 * original would dereference, or an index outside a view latches a fault
 * and returns -1 at once (writes made before that point stay, in the
 * original order). A latched fault makes every later call return -1.
 *
 * Arithmetic: 00188B80's two clip-time compares are EE COP1 compares
 * (em_ee_float.h); nothing else here computes on floats.
 *
 * Oracle: tools/test_player_equipment_reference.py executes the original
 * instructions of every routine above from the user's pinned ELF over
 * synthetic records and over the seven live nodes of every route capture.
 *
 * stdint only, plus the EmOwnerBone type of em_owner_services_original.h. */
#ifndef EM_PLAYER_EQUIPMENT_H
#define EM_PLAYER_EQUIPMENT_H

#include <stdint.h>

#include "game/em_owner_services_original.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_PLAYER_EQUIPMENT_CALLBACK 0x0018A6B0u /* node +0x10 (0018A880) */
#define EM_PLAYER_EQUIPMENT_MAX_BONES EM_OWNER_SERVICES_MAX_BONES /* +0x110..+0x1EF */
#define EM_PLAYER_EQUIPMENT_PLAYER_BYTES 0x320u  /* D_008102B0 record */

/* The ELF data window the flavour-0 and flavour-4 routines index:
 * D_0024A220 (sel rows), D_0024A2A0 / D_0024A300 (mode rows), D_0024A440
 * (probe rows 0..5, row 1 = D_0024A450) and D_0024A4A0. */
#define EM_PLAYER_EQUIPMENT_TABLE_BASE 0x0024A220u

enum {
    EM_PLAYER_EQUIPMENT_FAULT_NONE = 0,
    EM_PLAYER_EQUIPMENT_FAULT_NULL_WORKER = 1, /* a reached worker or data view is NULL */
    EM_PLAYER_EQUIPMENT_FAULT_WORKER_FAILED = 2,
    EM_PLAYER_EQUIPMENT_FAULT_BAD_RESULT = 3,  /* a worker result outside its original range */
    EM_PLAYER_EQUIPMENT_FAULT_BAD_INDEX = 4    /* index outside a view, or a NULL
                                                  slot/record the original dereferences */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_PLAYER_EQUIPMENT_FAULT_* */
} EmPlayerEquipmentFault;

/* The node record bytes the translated routines read or write. Vectors and
 * matrices are raw binary32 bit patterns. */
typedef struct EmPlayerEquipmentNode {
    uint8_t status;      /* +0x00: pool status; 0018A1F0 tests bit 0, 00189D30 tests 1/2, writes 2 */
    uint8_t drawn;       /* +0x01: 0018A6B0 writes 1; 00188AC0 writes 0 */
    uint8_t flavour;     /* +0x03: 0018A880 a0 */
    uint8_t state;       /* +0x04: lifecycle */
    uint8_t b05;         /* +0x05: 00188AC0 respawn step */
    uint8_t b07;         /* +0x07: 00189D30 effect step */
    uint8_t bones_held;  /* +0x09: 0018A8D0 = bone_count */
    uint8_t bone_count;  /* +0x0C: 0018A8D0 = 001C6150(model) */
    uint8_t variant;     /* +0x0D: 0018A880 a1 */
    struct EmPlayerEquipmentNode *self; /* +0x14: the record's own address (001AFA90) */
    uint8_t *effect;     /* +0x20: 00189D30's 001EFF10 record; its byte +4 is written */
    uint16_t h28;        /* +0x28: 0018A8D0 flavour 4 writes 0 */
    uint16_t h2E;        /* +0x2E: 00188630 one-shot pending; 0018A8D0 flavour 0 writes 0 */
    const void *model;   /* +0x44: written by the 001CA6E0 worker, read by 001C6150 */
    uint32_t method;     /* +0x4C: draw method address (written by the 001CA6E0 worker) */
    uint32_t vA0[4];     /* +0xA0 */
    uint32_t vB0[4];     /* +0xB0 */
    uint32_t vC0[4];     /* +0xC0 */
    uint32_t mD0[16];    /* +0xD0: 00188DF0's placement matrix */
    EmOwnerBone *bone[EM_PLAYER_EQUIPMENT_MAX_BONES]; /* +0x110: 001AF780 slots */
    uint32_t v1F0[4];    /* +0x1F0 */
    uint32_t w210;       /* +0x210 */
    uint32_t w214;       /* +0x214 */
} EmPlayerEquipmentNode;

/* The collision result the flavour-4 probe reads after 0019B2C0:
 * 0x700031B0 (point), *0x700031D0 (face record), *0x700031D4 (entity). */
typedef struct {
    uint32_t point[4];   /* 0x700031B0..0x700031BF */
    const void *record;  /* *(0x700031D0); NULL = 0 */
    const void *entity;  /* *(0x700031D4); NULL = 0 */
} EmPlayerEquipmentHitState;

/* Canonical storage views. Each is required only when a routine reaches it
 * (the entry points check the set they can reach). */
typedef struct {
    const uint8_t *player;              /* D_008102B0: the 0x320-byte player record image */
    EmOwnerBone *const *player_bones;   /* player +0x110 slots (4 = +0x120, 14 = +0x148, 19 = +0x15C) */
    uint32_t player_bone_count;
    EmOwnerBone *const *d00275B40;      /* the current bone work array (*D_00275B40) */
    uint32_t d00275B40_count;
    uint8_t *d008106C6;
    uint8_t *d008106C7;
    uint8_t *d008106CC;
    const uint8_t *d00810CA4;
    const uint8_t *d00810CA6;
    const int16_t *d00275BCC;           /* free bone slot count (signed halfword) */
    const uint32_t *d0028A56C;          /* bank word handed to 001C6120 */
    const int16_t *d00248B98;           /* first halfword of each table */
    const int16_t *d00248C78;
    const uint32_t *table;              /* words from EM_PLAYER_EQUIPMENT_TABLE_BASE */
    uint32_t table_words;
    uint32_t *spad3600;                 /* 0x70003600..0x7000371F (0x48 words) */
    uint32_t *spad38A0;                 /* 0x700038A0..0x700038DF (0x10 words) */
    const EmPlayerEquipmentHitState *hit;
} EmPlayerEquipmentWorld;

#define EM_PLAYER_EQUIPMENT_SPAD3600_WORDS 0x48u
#define EM_PLAYER_EQUIPMENT_SPAD38A0_WORDS 0x10u

typedef EmPlayerEquipmentNode EmPEN;

/* Workers. Each returns >= 0 on success; a negative result faults. */
typedef struct {
    void *ctx;
    /* SDK VU0 routines (by value; the translation stores `out`). */
    int (*w_001026A0)(void *ctx, const uint32_t m[16], const uint32_t v[4], uint32_t out[4]); /* v x M */
    int (*w_001026D0)(void *ctx, const uint32_t a[16], const uint32_t b[16], uint32_t out[16]);
    int (*w_001028B8)(void *ctx, const uint32_t a[4], const uint32_t b[4], uint32_t out[4]);  /* a + b */
    int (*w_001028D0)(void *ctx, const uint32_t a[4], const uint32_t b[4], uint32_t out[4]);  /* a - b */
    int (*w_00102760)(void *ctx, const uint32_t v[4], uint32_t out[4]);                       /* normalise */
    int (*w_001029C0)(void *ctx, uint32_t out[16]);                                           /* identity */
    int (*w_00102BB0)(void *ctx, const uint32_t m[16], uint32_t angle, uint32_t out[16]);
    /* 0018A6B0 */
    int (*w_001AFC10)(void *ctx, EmPEN *node);                   /* pool free */
    int (*w_method)(void *ctx, EmPEN *node, uint32_t method);     /* jalr *(node + 0x4C) */
    /* 0018A8D0 */
    int (*w_001C6120)(void *ctx, uint32_t bank, uint32_t id, uint32_t *handle);
    int (*w_001CA6E0)(void *ctx, EmPEN *node, uint32_t handle);  /* sets +0x44, +0x4C, ... */
    int (*w_001C6150)(void *ctx, const void *model, uint8_t *count);
    int (*w_001AF780)(void *ctx, EmOwnerBone **slot);            /* NULL = the original's 0 */
    int (*w_anim_bone_array_setup)(void *ctx, uint8_t count);    /* 001CB5B0 */
    int (*w_bone_init_default_1)(void *ctx, EmPEN *node);       /* 001C62C0 */
    /* 00188630 */
    int (*w_001854E0)(void *ctx, EmPEN *node);
    int (*w_00185760)(void *ctx, EmPEN *node);
    int (*w_001861C0)(void *ctx, EmPEN *node);
    int (*w_001869A0)(void *ctx, EmPEN *node);
    int (*w_00186A60)(void *ctx, EmPEN *node);
    int (*w_001872C0)(void *ctx, EmPEN *node);
    int (*w_00187CC0)(void *ctx, EmPEN *node);
    int (*w_001B61C0)(void *ctx, int32_t a, int32_t b, int32_t c, int32_t d); /* pad vibration */
    int (*w_001EFEB0)(void *ctx, uint32_t id, const uint32_t *matrix); /* matrix = spad 0x700036A0 */
    int (*w_001F4010)(void *ctx, int32_t index, const uint32_t *at);    /* at = spad 0x700036A0 */
    /* 00188A50 / 00188AC0 */
    int (*w_00188C70)(void *ctx, EmPEN *node);
    int (*w_0015C310)(void *ctx, int32_t arg1);                  /* 0015C310(D_008102B0, arg1) */
    /* 00188DF0 / 00188ED0 */
    int (*w_00189090)(void *ctx, EmPEN *node);
    int (*w_00189330)(void *ctx, EmPEN *node);
    int (*w_001899C0)(void *ctx, EmPEN *node);
    int (*w_00189A20)(void *ctx, EmPEN *node);
    int (*w_001C9610)(void *ctx, EmOwnerBone *const *bones, uint32_t bones_count, int32_t count,
                      const uint32_t world[16]);                 /* 001C9610(D_00275B40, n, +0xD0) */
    int (*w_001B0070)(void *ctx, uint32_t *value);               /* D_008106C8 */
    int (*w_00187780)(void *ctx, EmPEN *node, int32_t a1, int32_t a2);
    /* 0018A1F0 / 00189D30 */
    int (*w_001AA840)(void *ctx, EmPEN *node);
    int (*w_0019B2C0)(void *ctx, const uint32_t a[4], const uint32_t b[4], int32_t mask, int32_t *result);
    int (*w_00189EC0)(void *ctx, const void *entity, int32_t *result);
    int (*w_face_record)(void *ctx, const void *record, uint16_t *h1A, uint32_t normal[3]); /* +0x1A, +0x24.. */
    int (*w_001F00A0)(void *ctx, uint32_t id, const uint32_t *a, const uint32_t *b, int32_t a3);
    int (*w_0018A180)(void *ctx, EmPEN *node);
    int (*w_0019A570)(void *ctx, const uint32_t from[4], const uint32_t to[4], int32_t mask, int32_t id,
                      int32_t *result);
    int (*w_00189FE0)(void *ctx, EmPEN *node, const uint32_t a[4], const uint32_t b[4]);
    int (*w_001EFF10)(void *ctx, uint32_t id, const EmOwnerBone *bone, const uint32_t *s38A0,
                      const uint32_t *s38B0, const uint32_t *s38C0, const uint32_t *s38D0, uint32_t f12,
                      uint8_t **effect);                          /* NULL = the original's 0 */
} EmPlayerEquipmentWorkers;

typedef struct {
    const EmPlayerEquipmentWorkers *workers;
    const EmPlayerEquipmentWorld *world;
    EmPlayerEquipmentFault *fault;
} EmPlayerEquipment;

/* 0018A6B0(node): the pool behaviour. 0, or -1 on a fault. */
int em_player_equipment_tick(const EmPlayerEquipment *e, EmPEN *node);
/* 0018A8D0(node, id): *result is its return (0 bound, 1 refused). */
int em_player_equipment_0018A8D0(const EmPlayerEquipment *e, EmPEN *node, int32_t id, int32_t *result);
int em_player_equipment_00188630(const EmPlayerEquipment *e, EmPEN *node);
int em_player_equipment_00188A50(const EmPlayerEquipment *e, EmPEN *node);
int em_player_equipment_00188AC0(const EmPlayerEquipment *e, EmPEN *node);
int em_player_equipment_00188B80(const EmPlayerEquipment *e);
int em_player_equipment_00188DF0(const EmPlayerEquipment *e, EmPEN *node);
int em_player_equipment_00188ED0(const EmPlayerEquipment *e, EmPEN *node);
int em_player_equipment_0018A1F0(const EmPlayerEquipment *e, EmPEN *node);
int em_player_equipment_00189D30(const EmPlayerEquipment *e, EmPEN *node);

/* 0015D2F0(): *result is the code (0, 1, 2, 3 or 0x82) of the 0x320-byte
 * player record image (+0x04, +0x05, +0x1F1, +0x318). -1 when a pointer is
 * NULL. */
int em_player_equipment_0015D2F0(const uint8_t *player, int32_t *result);

#ifdef __cplusplus
}
#endif

#endif /* EM_PLAYER_EQUIPMENT_H */
