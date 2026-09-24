/* Script-host and door leaves of the first level (lane "script-door-fan",
 * census lanes L19-script-host and L18-door-original). Docs:
 * docs/SCRIPT_DOOR_FAN.md. Oracle: tools/test_script_door_fan_reference.py.
 *
 * Hand translations of these original functions (runtime addresses of the
 * pinned boot ELF, SHA-256 ee052236...e17a):
 *   001BA510  clear the 12 activity bytes D_008106D4..D_008106DF
 *             (C linked from the listing; read from the listing)
 *   001BAC00  script op14: spawn the actors of a 0x2C-byte entry list
 *             (byte-matched C)
 *   001BAD40  event handler of an op14-spawned actor (NEARMISS: read from the
 *             listing, including its jump table 0x26E170, which is read, not
 *             copied)
 *   001B1B30  visibility publish: +0x01 = 001B1630(x, y, z), then 001B1B70
 *             (byte-matched C)
 *   001BC240  door phase 4: advance the clip, commit the transition
 *             (byte-matched C)
 *   001BC290  door phase 5: advance the clip, restart it when D_008106B8 == 0
 *             (byte-matched C)
 *   001BBD60  door open-script sound: record word +0x18 = D_0024DB80 row
 *             (+0x56 >> 8) column (+0x2E) (byte-matched C)
 *   001B0080  room-entry camera seat on the camera object D_008101E0
 *             (byte-matched C; census row L18, listed there as verified but
 *             only a worker slot of em_script_host_workers existed)
 *
 * Every record byte a function reads or writes is a field named by its
 * original offset. Every original callee is one worker of EmSdfWorkers named
 * by its original address; `ctx` identifies the actor the call is about.
 * Original data owned elsewhere (the message request words, the camera
 * object bytes, the bone budget) is reached through the EmSdfWorld pointers.
 * A reached NULL worker, a NULL world pointer, a negative worker result, or
 * data outside a bounded image latches a fault (fail-stop): the function
 * returns -1 and the first fault stays in *fault. A latched fault makes every
 * later call return -1 without doing anything.
 *
 * Floats: these functions only move float bit patterns, except 001BAD40's
 * int-to-float conversion of the clip length (cvt.s.w) and 001B0080's adds;
 * both go through em_ee_float.h. stdint only; no port subsystem is used. */
#ifndef EM_SCRIPT_DOOR_FAN_H
#define EM_SCRIPT_DOOR_FAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SDF_D_008101E0 0x008101E0u /* camera object (0022EC30 argument) */
#define EM_SDF_D_0024DB80 0x0024DB80u /* door sound table, u16 pairs per row */
#define EM_SDF_D_0028A490 0x0028A490u /* bank table, one word per index */
#define EM_SDF_INDEX_0028A56C 0x37    /* D_0028A56C = D_0028A490[0x37] */
#define EM_SDF_INDEX_0028A59C 0x43    /* D_0028A59C = D_0028A490[0x43] */
#define EM_SDF_DEFAULT_HANDLER 0x001BB0E0u /* 001BAC00's default +0x10 */
#define EM_SDF_TAG_ALT_SPAWN 0x270E   /* 001BAC00: entry +4 selects 001C8140 */
#define EM_SDF_MSG_REQUEST 0x270D     /* 001BAD40: message request event */
#define EM_SDF_MSG_PUBLISH 0x270C     /* 001BAD40: publish +0x18 event */
#define EM_SDF_ENTRY_SIZE 0x2C        /* 001BAC00 entry stride */
#define EM_SDF_BONE_SLOTS 0x78        /* +0x110 .. +0x2F0 inside one record */

enum {
    EM_SDF_FAULT_NONE = 0,
    EM_SDF_FAULT_NULL = 1,          /* reached worker or data pointer is NULL */
    EM_SDF_FAULT_WORKER_FAILED = 2, /* worker returned a negative value */
    EM_SDF_FAULT_OUT_OF_IMAGE = 3,  /* an entry the original reads is outside the image */
    EM_SDF_FAULT_CAPACITY = 4       /* 001BAD40 bone count past the record's +0x110 array */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_SDF_FAULT_* */
} EmSdfFault;

/* Bytes of an original image (the AREA11 overlay arena or ELF data) that
 * 001BAC00 walks. `base` is the original address of bytes[0]. */
typedef struct {
    const uint8_t *bytes;
    uint32_t base, length;
} EmSdfImage;

/* The node bytes 001BAC00 writes into a node its spawn worker returns. The
 * 0x270E path writes only owner_24 and s2E. */
typedef struct {
    uint8_t b03;          /* +0x03 = entry +2 */
    uint8_t b0D;          /* +0x0D = low byte of entry +4 */
    uint32_t handler_10;  /* +0x10: entry +0x28, or 001BB0E0 */
    uint32_t entry_20;    /* +0x20: the entry's original address */
    uint32_t owner_24;    /* +0x24 = owner +0x14 */
    int16_t s2E;          /* +0x2E: running index, or 0xF (0x270E) */
    float pos_B0[3];      /* +0xB0..+0xB8 = entry +0x10..+0x18 (bits copied) */
    float rot_C0[3];      /* +0xC0..+0xC8 = entry +0x1C..+0x24 (bits copied) */
} EmSdfSpawned;

/* The owner bytes of the op14 handler (handler arg0). */
typedef struct {
    uint32_t self_14; /* +0x14, read */
    int16_t s2E;      /* +0x2E, written 0 */
} EmSdfSpawnOwner;

/* Bytes of an op14-spawned actor that 001BAD40 reads or writes. */
typedef struct {
    uint8_t lifecycle; /* +0x04: read after 001C5C90; 3 on bone overflow */
    uint8_t b09;       /* +0x09: bone count kept */
    uint8_t b0C;       /* +0x0C: bone count from 001C6150 */
    uint32_t w18;      /* +0x18: published by event 0x270C */
    uint32_t bank_40;  /* +0x40 = D_0028A490[entry +6] */
    uint32_t w44;      /* +0x44: 001C6150's argument (001CA6E0 sets it) */
    uint32_t bones_110[EM_SDF_BONE_SLOTS]; /* +0x110: 001AF780 handles */
} EmSdfEventActor;

/* The data 001BAD40 reaches outside the actor. */
typedef struct {
    int32_t *d2821B0, *d2821B4, *d2821B8; /* message request mode/phase/line */
    uint32_t *d8106C0;                    /* published word */
    uint32_t *d810250;                    /* camera +0x70 track pointer */
    float *d810254, *d810258;             /* camera +0x74 cursor, +0x78 head */
    uint8_t *d8101E4;                     /* camera mode byte */
    int16_t *d81024E;                     /* camera +0x6E scene id */
    const int16_t *d275BCC;               /* bone budget (lh) */
    uint8_t *d8106B8;                     /* area-change request byte (001BC290) */
} EmSdfWorld;

/* Door record bytes 001BC240 / 001BC290 read or write. */
typedef struct {
    uint8_t b0B;         /* +0x0B: 001BC290 clears it (the armed bits) */
    int16_t anim_flags;  /* +0x1FE (script block +0x0E) */
} EmSdfDoorStep;

/* Camera object D_008101E0 bytes 001B0080 (arg0) reads or writes. Quads are
 * moved as bit patterns (00102948 is a 16-byte copy). */
typedef struct {
    float f0C;         /* +0x0C: read, the eye offset length */
    float eye_10[4];   /* +0x10..+0x1C */
    float tgt_20[4];   /* +0x20..+0x2C */
    float rot_30[4];   /* +0x30..+0x3C (+0x34 is wrapped) */
} EmSdfSeat;

/* Data 001B0080 reaches outside the camera object. */
typedef struct {
    const uint8_t *d810700, *d810702; /* area and room bytes */
    const float *d810350;             /* player +0xA0 quad */
    const float *spad3B50;            /* 0x70003B50 quad (the seat angles) */
    float *d8105D0, *d8105E0;         /* working eye / target quads */
    uint32_t *spad3400;               /* 0x70003400 matrix, 16 words */
    uint32_t *spad3600;               /* 0x70003600 vector, 4 words */
} EmSdfSeatWorld;

typedef struct EmSdfWorkers {
    void *ctx;
    /* Data readers. */
    int (*r_0028A490)(void *ctx, int32_t index, uint32_t *value); /* D_0028A490[index] */
    int (*r_track_head)(void *ctx, uint32_t address, float *value); /* *(float *)address */
    int (*r_0024DB80)(void *ctx, uint32_t address, uint16_t *value); /* ELF halfword */

    /* 001BAC00 */
    int (*w_001AFA90)(void *ctx, uint8_t type, uint32_t *node, EmSdfSpawned **view);
    int (*w_001C8140)(void *ctx, uint32_t bank, int16_t index, uint32_t callback,
                      uint32_t *node, EmSdfSpawned **view);

    /* 001BAD40 (the actor is `ctx`; the view is passed where the callee
     * reads or writes bytes 001BAD40 reads afterwards). */
    int (*w_001CA6E0)(void *ctx, EmSdfEventActor *obj, uint32_t bank);
    int (*w_001C6120)(void *ctx, uint32_t table, int32_t index, uint32_t *result);
    int (*w_0022EC30)(void *ctx, uint32_t camera);
    int (*w_001C5C90)(void *ctx, EmSdfEventActor *obj);
    int (*w_001C6150)(void *ctx, uint32_t model, int32_t *result);
    int (*w_001AF780)(void *ctx, uint32_t *handle);
    int (*w_001BA8E0)(void *ctx, EmSdfEventActor *obj, int16_t type);
    int (*w_001D8BF0)(void *ctx, EmSdfEventActor *obj, int32_t a1);
    int (*w_001CA6F0)(void *ctx, EmSdfEventActor *obj, uint8_t mode);
    int (*w_001CB5B0)(void *ctx, uint8_t count);
    int (*w_001C63E0)(void *ctx, EmSdfEventActor *obj, int16_t clip);
    int (*w_001C61D0)(void *ctx, uint32_t bank, int16_t clip, int32_t *result);

    /* anim_clip_init 001C67E0(actor, clip, f12, f13): 001BAD40, 001BC290. */
    int (*w_001C67E0)(void *ctx, int16_t clip, float start, float length);

    /* 001B1B30 */
    int (*w_001B1630)(void *ctx, float x, float y, float z, int32_t *result);
    int (*w_001B1B70)(void *ctx);

    /* 001BC240 / 001BC290: anim_advance_time 001C64F0(actor, step) returns
     * the animation flags (a short); 001BC150 is the transition commit. */
    int (*w_001C64F0)(void *ctx, float step, int16_t *flags);
    int (*w_001BC150)(void *ctx);

    /* 001B0080: 001B1470 (wrap to (-pi, pi]) and the VU0 matrix leaves
     * 001029C0(m) (identity), 00102C58(dst, src, angles) (Euler rotation;
     * dst == src here) and 001026A0(out, m, v) (matrix x vector, four
     * lanes written). */
    int (*w_001B1470)(void *ctx, float angle, float *result);
    int (*w_001029C0)(void *ctx, uint32_t m[16]);
    int (*w_00102C58)(void *ctx, uint32_t dst[16], const uint32_t src[16], const float angles[4]);
    int (*w_001026A0)(void *ctx, float out[4], const uint32_t m[16], const uint32_t v[4]);
} EmSdfWorkers;

/* 001BA510: D_008106B0 +0x24 .. +0x2F = 0, in ascending order. Returns 0,
 * or -1 (fault at 0x008106D4) when `activity` is NULL. */
int em_sdf_001BA510(uint8_t activity[12], EmSdfFault *fault);

/* 001BAC00 (ftab_0024D880[0x14]): walks the entries at record +0x14 until a
 * short -1 at an entry's +0, starting with the first entry unconditionally,
 * and finally sets owner +0x2E = 0. Returns the original result 1, or -1. */
int em_sdf_001BAC00(EmSdfSpawnOwner *owner, const uint8_t *record, const EmSdfImage *image,
                    const EmSdfWorkers *w, EmSdfFault *fault);

/* 001BAD40(obj, entry): the original result (0 or 1), or -1. `entry` points at
 * the 0x2C bytes of the actor's entry (its +0x20 address). */
int em_sdf_001BAD40(EmSdfEventActor *obj, const uint8_t *entry, const EmSdfWorld *world,
                    const EmSdfWorkers *w, EmSdfFault *fault);

/* 001B1B30(actor, x, y, z): *visible = byte of 001B1630(x, y, z); 001B1B70
 * when it is nonzero. Returns the +0x01 byte, or -1. */
int em_sdf_001B1B30(uint8_t *visible, float x, float y, float z, const EmSdfWorkers *w,
                    EmSdfFault *fault);

/* 001BC240(door, block): returns 0, or -1. */
int em_sdf_001BC240(EmSdfDoorStep *door, const EmSdfWorkers *w, EmSdfFault *fault);

/* 001BC290(door, block): the original result (1 restarted, 0 waiting), or -1. */
int em_sdf_001BC290(EmSdfDoorStep *door, const EmSdfWorld *world, const EmSdfWorkers *w,
                    EmSdfFault *fault);

/* 001BBD60(door, record): *record_18 = D_0024DB80 halfword at
 * 0x24DB80 + ((link_56 & 0xFF00) >> 8) * 4 + side_2E * 2, zero-extended.
 * `link_56` is the door's +0x56 short, `side_2E` its +0x2E halfword.
 * Returns 0, or -1. */
int em_sdf_001BBD60(int16_t link_56, uint16_t side_2E, uint32_t *record_18,
                    const EmSdfWorkers *w, EmSdfFault *fault);

/* 001B0080(camera, a1): in area 1 room 4 the fixed seat, otherwise the
 * player-relative seat. Returns 0, or -1. */
int em_sdf_001B0080(EmSdfSeat *camera, float a1, const EmSdfSeatWorld *world,
                    const EmSdfWorkers *w, EmSdfFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_SCRIPT_DOOR_FAN_H */
