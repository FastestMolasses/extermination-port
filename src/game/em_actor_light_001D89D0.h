/* The per-draw actor lighting driver 001D89D0 and everything it reaches,
 * translated bit for bit. Docs: docs/ACTOR_LIGHT_001D89D0.md.
 *
 * Hand translation of these original functions (boot ELF SCUS-97112):
 *   001D89D0  driver(owner, A, B, rgb): lighting-mode dispatch, the room rig,
 *             the slot composition, the colour multiply, the +0x40 glow add
 *             (NEARMISS C; the instructions were followed)
 *   001D8C30  modes 1, 3, 4, 5 and 6 (and every other mode it is given):
 *             clears / template copies and biased colour rows (NEARMISS C;
 *             the instructions and its seven-entry jump table were followed)
 *   001D8130  room rig load into the rig record D_00817BC0 (matched C)
 *   001D7B30  room rig lookup in D_00251C50 by area key (matched C)
 *   001D2910(8) = 001D2710(8): context +0x0C bit 8 (the 0x0F00 key)
 *   001D8340  slot directions from the rig angles, the camera fill (slot 0,
 *             owner +0x02 bit 0x20) and the point-light fold (matched C)
 *   001D8270  point-light fold gate: type byte and model radius (asm .word
 *             leaf, decoded from its words)
 *   001D8690  A = the three slot directions as columns plus slot 0's colour
 *             in the w lanes; B = the rig colours times rgb, ambient row
 *             biased by 8388608 (NEARMISS C; the instructions were followed)
 *   SDK VU0 routines: 001026A0 (row transform), 00102738 (xyz dot),
 *             00102760 (xyz normalise, w = 0), 00102798 (4x4 transpose),
 *             001028B8 (add), 001028D0 (subtract), 00102900 (scale),
 *             00102948 / 00102958 (quadword copies); 001029C0, 00102A60,
 *             00102B08 and 00102BB0 are em_owner_services' verified ones.
 *
 * Verified by tools/test_actor_light_001d89d0_reference.py: the original
 * instructions run over synthetic states and over every captured AREA11
 * owner draw (route beats 00..14); every A, B, rig record and D_00275688 byte
 * must be equal, and with this module bound as w_001D89D0 the native owner
 * draw chain must write the original's display-list unit.
 *
 * Arithmetic: every EE COP1 and VU0-macro instruction goes through
 * game/em_ee_float.h under its real form (op, dest mask, broadcast lane;
 * VDIV (3,0)). No host float operation is performed. Values cross the API as
 * raw uint32_t words, so every bit (signalling NaNs included) is kept.
 *
 * Only the original bytes these routines read or write are modelled. Each is
 * a view onto canonical storage owned by the caller; nothing is copied. A
 * reached NULL view, an owner node index outside the given slots or a refused
 * float form latches a fault and returns -1; every later call returns -1
 * until the caller clears the fault. The views a call will reach are checked
 * before its first store, so a view fault leaves every output untouched.
 *
 * Aliasing: the original re-reads memory between stores. The views are
 * assumed not to overlap (A, B, rgb, the rig record, the tables); the
 * original callers pass the scratchpad, the owner and D_00817BC0, which do
 * not overlap.
 *
 * stdint only; depends on em_owner_services_original (rotations, owner type)
 * and em_ee_float.h. */
#ifndef EM_ACTOR_LIGHT_001D89D0_H
#define EM_ACTOR_LIGHT_001D89D0_H

#include <stdint.h>

#include "game/em_owner_services_original.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_ACTOR_LIGHT_RIG_ADDRESS 0x00817BC0u   /* D_00817BC0: what D_00275688 receives */
#define EM_ACTOR_LIGHT_RIG_WORDS 76              /* D_00817BC0 +0x00..+0x12F */
#define EM_ACTOR_LIGHT_TABLE_ENTRIES 45          /* D_00251C50: 0x78-byte entries */
#define EM_ACTOR_LIGHT_TABLE_ENTRY_WORDS 30
#define EM_ACTOR_LIGHT_POINTS 32                 /* context +0x220: 0x80-byte entries */
#define EM_ACTOR_LIGHT_POINT_WORDS 32

/* Canonical storage views. Each is required only when a call reaches it. */
typedef struct {
    uint32_t *d00275688;          /* the rig pointer word; the rig path stores 0x00817BC0 */
    uint32_t *d00817BC0;          /* the rig record, EM_ACTOR_LIGHT_RIG_WORDS words */
    const int32_t *ctx_246C;      /* *(D_00275670) + 0x246C: lighting mode (001D8C20) */
    const uint32_t *ctx_000C;     /* context +0x0C: 001D2710 bit word */
    const uint32_t *ctx_0220;     /* context +0x220: 32 x 32 words of point lights */
    const uint32_t *ctx_2380;     /* context +0x2380: 16 words, modes 4..6 */
    const uint8_t *d00810700;     /* two bytes: D_00810700 (area), D_00810701 (sub) */
    const uint32_t *d00251C50;    /* 45 x 30 words: room rigs, word 0 = key */
    const uint32_t *d00253170;    /* 4 words: the fold's first accumulator seed */
    const uint32_t *d00810610;    /* 16 words: the view matrix (camera fill only) */
} EmActorLightWorld;

/* The owner bytes 001D89D0 and its callees read (the a0 record). */
typedef struct {
    uint8_t cls;                   /* +0x02: bit 0x20 camera fill, bit 0x40 glow add */
    uint8_t kind;                  /* +0x03: 001D8270 type byte */
    uint8_t pose_bone;             /* +0x98: 0xFF = light point +0xB0, else node +0xC0 */
    uint32_t rgb[4];               /* +0x80..+0x8F: read by the glow add only */
    uint32_t pos[4];               /* +0xB0..+0xBF */
    const uint32_t *model_radius;  /* [+0x44] + 0x20; NULL when +0x44 is 0 */
    /* +0x110 slot k's node + 0xC0: 4 words, or NULL for a slot pointer that
     * is 0 (a fault only if the fold reads it). */
    const uint32_t *const *node_c0;
    uint32_t node_count;           /* native bound of node_c0 */
} EmActorLightOwner;

typedef struct {
    EmActorLightWorld world;
    EmOwnerServicesFault fault;    /* latched; cleared only by the caller */
} EmActorLight;

/* 001D89D0(owner, a, b, arg3): a and b are 16 words each (the callers pass
 * SPR 0x70003400 / 0x70003440), arg3 is 4 words (callers pass owner + 0x80).
 * a and b are read-modify-written in place: modes 3..6 leave rows of them
 * untouched. Returns 0, or -1 (fault latched). */
int em_actor_light_001D89D0(EmActorLight *s, const EmActorLightOwner *o, uint32_t a[16],
                            uint32_t b[16], const uint32_t arg3[4]);

/* 001D8C30(mode, a, b, in). Returns 0, or -1. */
int em_actor_light_001D8C30(EmActorLight *s, int32_t mode, uint32_t a[16], uint32_t b[16],
                            const uint32_t in[4]);

/* 001D8270(owner): *result = 0 or 1. Returns 0, or -1 (model view missing
 * for a type that reaches the radius compare). */
int em_actor_light_001D8270(EmActorLight *s, const EmActorLightOwner *o, int32_t *result);

/* 001D7B30(): *entry = the D_00251C50 entry index returned (0 on no match). */
int em_actor_light_001D7B30(EmActorLight *s, uint32_t *entry);

/* 001D8130(): D_00275688 = D_00817BC0, then the room rig load. */
int em_actor_light_001D8130(EmActorLight *s);

/* 001D8340(owner, flag, point): flag = owner +0x02 & 0x20; point is the
 * light point (4 words; NULL = address 0xC0, a fault if the fold reads it). */
int em_actor_light_001D8340(EmActorLight *s, const EmActorLightOwner *o, uint32_t flag,
                            const uint32_t point[4]);

/* 001D8690(a, b, rgb). */
int em_actor_light_001D8690(EmActorLight *s, uint32_t a[16], uint32_t b[16], const uint32_t rgb[4]);

/* ---- binding as EmOwnerServicesWorkers.w_001D89D0 ----
 * EmOwnerServicesOwner carries +0x02, +0x03, +0x98, +0xB0, +0x44 (radius)
 * and the +0x110 slots (node +0xC0 = bone->world row 3), but not the +0x80
 * colour words: `w_owner_rgb` supplies them. The adapter runs
 * em_actor_light_001D89D0(owner, a, b, owner + 0x80) on `light`. A fault is
 * latched in light->fault and returned as -1 (owner services then latches
 * EM_OWNER_FAULT_WORKER_FAILED at 0x001D89D0). */
typedef struct {
    EmActorLight *light;
    void *ctx;
    /* owner +0x80..+0x8F as raw words; a negative result is a fault. */
    int (*w_owner_rgb)(void *ctx, const EmOwnerServicesOwner *owner, uint32_t rgb[4]);
} EmActorLightBinding;

int em_actor_light_w_001D89D0(void *binding, const EmOwnerServicesOwner *owner, float a[16],
                              float b[16]);

#ifdef __cplusplus
}
#endif

#endif /* EM_ACTOR_LIGHT_001D89D0_H */
