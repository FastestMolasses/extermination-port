/* Shared owner model services: the generic original routines that the AREA11
 * owners (truck, crates, drums, fan, Roger, elevator, pickups) call for model
 * binding, bone setup, placement, publication, drawing and rumble.
 * Docs: docs/OWNER_SERVICES.md.
 *
 * Hand translation of these original functions (boot ELF SCUS-97112):
 *   001B0FD0  model bind: 001B0EA0, then bone_init_default_1, +0x04 += 1
 *   001B0EA0  model/bone allocation (NEARMISS C; checked against the .s)
 *   001B1020  model bind (clip variant): 001B0DC0, +0x04 += 1, bone init 1 or 2
 *   001B0DC0  model/bone allocation, clip variant (the a1 register reaches
 *             001C6120 as the model id; the decomp comment "arg1 unused" is
 *             wrong about that)
 *   001C62C0  bone_init_default_1 (asm .word leaf; decoded from its words)
 *   001C6150  model bone count (model +0x08), inlined in the two allocators
 *   001C6380  placement: build_trs_matrix(+0xD0) then 001C9610 over the bones
 *   build_trs_matrix, 001C9610 (NEARMISS; the .s was followed), and the SDK
 *             VU0 routines they reach: 001029C0, 001029E8, 00102A60, 00102B08,
 *             00102BB0, 00102C58, 00102918
 *   00102958  copy_qw4 (64-byte matrix copy)
 *   001B17A0  per-owner update tail: 001B1CE0 gate, +0x01 = 001B1630(pos),
 *             001B1B70 when visible
 *   001CAA00  default draw method (+0x4C): 001CA990 with the cull radius
 *   001CA990  draw: 001CA7B0 cull, 001D8C20(0), 001C7420(0x3F5, 0),
 *             001D1F80(0, 1, 0), 001CA940
 *   001C7420  bone palette upload: 001D89D0 into SPR 0x70003400/0x70003440,
 *             then the DMA CNT packets of node x VP and normalised node x
 *             light rows (NEARMISS C; the .s was followed)
 *   001B1E20  rumble dispatch by effect id (table D_0024D6F0)
 *   001B5B70  rumble countdown (frame step I)
 *
 * Verified by tools/test_owner_services_reference.py, which executes the
 * original instructions and compares every modelled byte, every packet byte
 * and every worker call; tests/owner_services_test.c pins the fail-stop
 * contract.
 *
 * Arithmetic: every EE COP1 and VU0-macro instruction goes through
 * game/em_ee_float.h (docs/EE_FLOAT_MODEL.md section 6) under its real form
 * (op, dest mask, broadcast lane; VDIV (3,0)); the module has no float
 * helpers of its own and performs no host float operation. A refused form
 * latches EM_OWNER_FAULT_UNMEASURED_FORM. Original float REGISTER values
 * (f12/f13/f14, the SDK angle) cross this API as raw uint32_t bits, so a
 * signalling NaN keeps every bit.
 *
 * Only original bytes these routines read or write are modelled; each field
 * names its original offset. Everything else the originals reach is an
 * explicit worker named by original address. A reached NULL worker or data
 * view, a negative worker result or an index outside the owned storage
 * latches a fault (fail-stop); every later call then returns -1.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_OWNER_SERVICES_ORIGINAL_H
#define EM_OWNER_SERVICES_ORIGINAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bone slots +0x110 .. +0x1EF. The original has no bound: slot 56 and up
 * would overwrite the +0x1F0 owner scratch, which is not modelled, so a
 * bone count above 56 faults (EM_OWNER_FAULT_BAD_INDEX). */
#define EM_OWNER_SERVICES_MAX_BONES 56

/* 001CA990's 001C7420 arguments. */
#define EM_OWNER_SERVICES_COLOR_VU_ADDRESS 0x3F5
#define EM_OWNER_SERVICES_DRAW_CHANNEL 0

/* Fault codes (numerically the EM_SCENE_FAULT_* codes of em_scene_state.h). */
enum {
    EM_OWNER_FAULT_NONE = 0,
    EM_OWNER_FAULT_NULL_WORKER = 1,   /* a reached worker or data view is NULL */
    EM_OWNER_FAULT_WORKER_FAILED = 2, /* a worker returned a negative value */
    EM_OWNER_FAULT_BAD_RESULT = 3,    /* a worker result outside the original range */
    EM_OWNER_FAULT_BAD_INDEX = 4,     /* an original index outside the owned storage */
    /* em_ee_float.h refused a float form (EM_EE_FLOAT_UNMEASURED / _NO_OPERAND).
     * Every form this module executes is in the measured table, so this is
     * unreachable unless the table changes. No EM_SCENE_FAULT_* has this
     * number (5 is EM_SCENE_FAULT_FREED_NEXT); the coordinator maps it. */
    EM_OWNER_FAULT_UNMEASURED_FORM = 6
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_OWNER_FAULT_* */
} EmOwnerServicesFault;

/* One skeleton node (a 001AF780 slot). Matrices are the original row
 * layout: four rows of four floats, row 3 the translation. */
typedef struct {
    float bind[16];    /* +0x00..+0x3F: bone_init_default_1 copies the model record */
    int16_t parent;    /* +0x64: -1 = root (001C9610 uses the owner's +0xD0) */
    float rot[3];      /* +0x70: 00102C58 angles (Z, then Y, then X) */
    float trans[3];    /* +0x7C */
    int16_t scale[3];  /* +0x88: 1/4096 fixed point (signed halfwords) */
    float world[16];   /* +0x90..+0xCF: 001C9610 output, 001C7420 input */
} EmOwnerBone;

/* Model skeleton record i at model + *(model + 0x0C) + i * 0x50. */
typedef struct {
    int16_t parent;    /* +0x04 (signed halfword) */
    float bind[16];    /* +0x10..+0x4F */
} EmOwnerSkeletonRecord;

/* The model bytes these routines read (the +0x44 handle's target). */
typedef struct {
    uint8_t bone_count;                     /* model +0x08 (001C6150) */
    float radius;                           /* model +0x20 (001CAA00) */
    const EmOwnerSkeletonRecord *skeleton;  /* model + *(model + 0x0C) */
    uint32_t skeleton_records;              /* native bound of `skeleton` */
} EmOwnerModel;

/* The owner record bytes these routines read or write. */
typedef struct {
    uint8_t drawn;          /* +0x01: 001B17A0 stores the 001B1630 result */
    uint8_t cls;            /* +0x02: class (low 5 bits) and flags 0xE0 */
    uint8_t kind;           /* +0x03 */
    uint8_t lifecycle;      /* +0x04: allocators write 3 over the bone cap; binds += 1 */
    uint8_t bones_held;     /* +0x09: allocated slot count */
    uint8_t bone_count;     /* +0x0C: 001C6150(model) */
    uint8_t model_id;       /* +0x0D: 001B0EA0's 001C6120 id; 001B17A0 bit 0 */
    uint16_t flags2;        /* +0x2E (read unsigned by 001B17A0) */
    uint32_t anim;          /* +0x40: 001B0DC0 stores D_0028A490[a2] */
    const EmOwnerModel *model; /* +0x44: bound by the 001CA6E0 worker; NULL = 0 */
    float scale[4];         /* +0x60: build_trs_matrix scale (x, y, z used) */
    uint32_t attachment;    /* +0x90: nonzero -> 001CAA00 calls 001CB3C0 */
    int16_t collapsed_bone; /* +0x94: 001C7420 keeps only this bone's translation */
    uint8_t pose_bone;      /* +0x98: 0xFF = cull at +0xB0, else at D_00275B40[b] +0xC0 */
    float pos[4];           /* +0xB0 */
    float rot[4];           /* +0xC0: build_trs_matrix X, Y, Z angles */
    float world[16];        /* +0xD0 */
    EmOwnerBone *bone[EM_OWNER_SERVICES_MAX_BONES]; /* +0x110: slot pointers */
} EmOwnerServicesOwner;

/* EE scratchpad matrices these routines write or read. */
typedef struct {
    float s3400[16]; /* 0x70003400: 001C9610 staging; 001D89D0 light matrix (A) */
    float s3440[16]; /* 0x70003440: 001D89D0 colour matrix (B); 001C7420 zeroes it */
    float s3480[16]; /* 0x70003480: 001C7420 normalised node rows (C) */
    float s3AC0[16]; /* 0x70003AC0: view-projection, read by 001C7420 */
} EmOwnerServicesScratch;

/* One display-list channel: the cursor word at D_00275670 + 0x10 + 4 * chan.
 * 001C7420 writes its packets at `cursor` and advances it. Bytes the
 * original leaves unwritten (DMA tag bytes +2 and +8..+0xF) are left as they
 * are. Writing past `end` faults. */
typedef struct {
    uint8_t *cursor;
    uint8_t *end;
} EmOwnerServicesChannel;

/* Canonical storage views. Each pointer is required only when a routine
 * reaches it; the module keeps no copies. */
typedef struct {
    const uint32_t *d0028A59C;      /* 001B0EA0: bank pointer word for 001C6120 */
    const uint32_t *d0028A56C;      /* 001B0DC0: bank pointer word for 001C6120 */
    const uint32_t *d0028A490;      /* 001B0DC0: +0x40 table (word entries) */
    uint32_t d0028A490_count;
    const int16_t *d00275BCC;       /* free bone slot count (signed halfword), the bone cap */
    const uint8_t *d00810CA5;       /* 001B17A0 mode byte (== 6 runs the 001B1CE0 gate) */
    EmOwnerBone *const *d00275B40;  /* 001CAA00: the current bone work array */
    uint32_t d00275B40_count;
    EmOwnerServicesScratch *scratch;
    EmOwnerServicesChannel *channel; /* indexed by the 001C7420 channel argument */
    uint32_t channel_count;
    const uint8_t *d0024D6F0;       /* 001B1E20: 4-byte records {big, small, dur, pad} */
    uint32_t d0024D6F0_count;       /* records */
    const uint8_t *d00810E56;       /* 001B5B70 gate (pad block D_00810E40 +0x16) */
    int16_t *d00810E68;             /* 001B5B70 timer (pad block +0x28) */
} EmOwnerServicesWorld;

/* Workers. Each returns >= 0 on success; a negative result faults. */
typedef struct {
    void *ctx;
    /* 001C6120(bank, id): model table lookup (a1 is the full register value;
     * the original masks it with 0x7FFF). */
    int (*w_001C6120)(void *ctx, uint32_t bank, uint32_t id, uint32_t *handle);
    /* 001CA6E0(owner, handle) = 001CA5E0(owner, handle, 0): binds the model;
     * the worker sets owner->model (+0x44) and whatever else 001CA5E0 writes. */
    int (*w_001CA6E0)(void *ctx, EmOwnerServicesOwner *owner, uint32_t handle);
    /* 001AF780: pop a bone slot (D_00275BD0 stack, D_00275BCC -= 1). The
     * original returns 0 while fewer than 31 slots are free: *slot = NULL. */
    int (*w_001AF780)(void *ctx, EmOwnerBone **slot);
    /* anim_bone_array_setup (001CB5B0)(count): D_00275B40 = D_00275B48 + 0x110. */
    int (*w_anim_bone_array_setup)(void *ctx, uint8_t count);
    /* bone_init_default_2 (001C63E0)(owner, (int16)a3), reached by 001B1020. */
    int (*w_bone_init_default_2)(void *ctx, EmOwnerServicesOwner *owner, int16_t clip);
    /* 001B1CE0(owner): D_00275B54 push of +0x14 while D_00275B58 < 0x40. */
    int (*w_001B1CE0)(void *ctx, EmOwnerServicesOwner *owner);
    /* 001B1630(f12, f13, f14): camera cone/range gate; the u8 result. The
     * arguments are the raw register bits of +0xB0/+0xB4/+0xB8. */
    int (*w_001B1630)(void *ctx, uint32_t x, uint32_t y, uint32_t z, uint8_t *visible);
    /* 001B1B70(owner): class-list publication. */
    int (*w_001B1B70)(void *ctx, EmOwnerServicesOwner *owner);
    /* 001CA7B0(position, f12 = radius): clip flags 0..0x1F, or -1 culled.
     * `radius` is the raw f12 bits. */
    int (*w_001CA7B0)(void *ctx, const float position[3], uint32_t radius, int32_t *flags);
    /* 001D8C20(mode): context +0x246C = mode (lighting mode). */
    int (*w_001D8C20)(void *ctx, int32_t mode);
    /* 001D89D0(owner, 0x70003400, 0x70003440, owner + 0x80): writes the light
     * matrix (A) and the colour matrix (B). */
    int (*w_001D89D0)(void *ctx, const EmOwnerServicesOwner *owner, float a[16], float b[16]);
    /* 001D1F80(a0, a1, a2): display-list call packet. */
    int (*w_001D1F80)(void *ctx, int32_t a0, int32_t a1, int32_t a2);
    /* 001CA940(flags, model): mesh kernel (001D3C30 when flags & 1, else 001D38F0). */
    int (*w_001CA940)(void *ctx, int32_t flags, const EmOwnerModel *model);
    /* 001CB3C0(owner): the +0x90 attachment draw. */
    int (*w_001CB3C0)(void *ctx, EmOwnerServicesOwner *owner);
    /* 001B61C0(big, small, duration, force): pad actuator request. */
    int (*w_001B61C0)(void *ctx, uint8_t big, uint8_t small, int64_t duration, int32_t force);
    /* 001B6250(&D_00810E40): pad actuator stop. */
    int (*w_001B6250)(void *ctx);
} EmOwnerServicesWorkers;

typedef struct {
    EmOwnerServicesWorld world;
    EmOwnerServicesWorkers workers;
    EmOwnerServicesFault fault; /* latched; cleared only by the caller */
} EmOwnerServices;

/* ---- model binding and bones. Results are the original return values;
 * -1 means a fault was latched (or one was already latched). ---- */

/* 001B0FD0(owner): 1 when 001B0EA0 refused (+0x04 = 3), else 0 (+0x04 += 1). */
int em_owner_services_001B0FD0(EmOwnerServices *s, EmOwnerServicesOwner *o);
/* 001B0EA0(owner): 1 over the bone cap (+0x04 = 3), else 0. */
int em_owner_services_001B0EA0(EmOwnerServices *s, EmOwnerServicesOwner *o);
/* 001B1020(owner, a1, a2, a3): 1 when 001B0DC0 refused, else 0. */
int em_owner_services_001B1020(EmOwnerServices *s, EmOwnerServicesOwner *o,
                               uint32_t a1, int32_t a2, int32_t a3);
/* 001B0DC0(owner, a1, a2): 1 over the bone cap (+0x04 = 3), else 0. */
int em_owner_services_001B0DC0(EmOwnerServices *s, EmOwnerServicesOwner *o,
                               uint32_t a1, int32_t a2);
/* bone_init_default_1 (001C62C0). Returns 0. */
int em_owner_services_001C62C0(EmOwnerServices *s, EmOwnerServicesOwner *o);

/* ---- placement ---- */

/* 001C6380(owner): +0xD0 = TRS(+0xB0, +0xC0, +0x60); bones' +0x90 through
 * 001C9610 (writes SPR 0x70003400 when the owner has bones). Returns 0.
 * A refused float form faults at build_trs_matrix (0x001C94B0). */
int em_owner_services_001C6380(EmOwnerServices *s, EmOwnerServicesOwner *o);
/* 001C9610(bones, count, root). Returns 0. */
int em_owner_services_001C9610(EmOwnerServices *s, EmOwnerBone *const *bones, int32_t count,
                               const float root[16]);
/* 00102958 copy_qw4(dst, src): raw 64-byte copy (four quadwords). */
void em_owner_services_copy_qw4_00102958(float dst[16], const float src[16]);

/* SDK VU0 routines (bit-exact through em_ee_float.h; dst may equal src).
 * Each returns the em_ee_float status: EM_EE_FLOAT_OK (0), or nonzero when a
 * form is refused (a fault for the caller). A rotate stores nothing on a
 * refusal; a composite (euler, build_trs_matrix) stops at the refused call,
 * after the stores of the calls before it, as the original would have.
 * `angle` is the raw f12 bits. */
int em_owner_services_identity_001029C0(float m[16]);
int em_owner_services_rotate_x_00102B08(float dst[16], const float src[16], uint32_t angle);
int em_owner_services_rotate_y_00102BB0(float dst[16], const float src[16], uint32_t angle);
int em_owner_services_rotate_z_00102A60(float dst[16], const float src[16], uint32_t angle);
/* 00102C58(dst, src, angles): Z, then Y (on dst), then X (on dst). */
int em_owner_services_euler_00102C58(float dst[16], const float src[16], const float angles[3]);
/* 00102918(dst, src, v): rows 0..2 copied, row 3 xyz = src row 3 + v. */
int em_owner_services_translate_00102918(float dst[16], const float src[16], const float v[3]);
/* build_trs_matrix (0x001C94B0)(m, pos, rot, scale). */
int em_owner_services_build_trs_matrix(float m[16], const float pos[3], const float rot[3],
                                       const float scale[3]);

/* ---- publication ---- */

/* 001B17A0(owner): returns +0x01 (0 or the 001B1630 byte). */
int em_owner_services_001B17A0(EmOwnerServices *s, EmOwnerServicesOwner *o);

/* ---- draw ---- */

/* 001CAA00(owner): the default +0x4C draw method. Returns 0. */
int em_owner_services_001CAA00(EmOwnerServices *s, EmOwnerServicesOwner *o);
/* 001CA990(owner, position, f12 = radius bits). Returns 0. */
int em_owner_services_001CA990(EmOwnerServices *s, EmOwnerServicesOwner *o,
                               const float position[3], uint32_t radius);
/* 001C7420(owner, vuaddr, chan). Returns 0 and the channel cursor on entry
 * (the original return value) in *first, or -1. */
int em_owner_services_001C7420(EmOwnerServices *s, const EmOwnerServicesOwner *o,
                               int32_t vuaddr, int32_t chan, uint8_t **first);

/* ---- rumble ---- */

/* 001B1E20(effect, duration). Returns 0. */
int em_owner_services_001B1E20(EmOwnerServices *s, int32_t effect, int64_t duration);
/* 001B5B70(). Returns 0. */
int em_owner_services_001B5B70(EmOwnerServices *s);

#ifdef __cplusplus
}
#endif

#endif /* EM_OWNER_SERVICES_ORIGINAL_H */
