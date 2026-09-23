/* Canonical storage for the scene coordinator (WP-3, SCENE_COORDINATOR_DESIGN.md
 * section 3.2). Every original byte the coordinator cores read or write that
 * the coordinator OWNS exists exactly once, in EmSceneState. The static
 * instance lives in em_scene_bindings.c; every other module reaches it through
 * em_scene_state() and keeps no copy.
 *
 * Not in this struct, on purpose:
 * - The task bytes +8..+0x1F stay in EmTask.user (em_task.h), where original
 *   record offset +k is user[k - 8]. The cores receive that `user` pointer;
 *   em_scene_task_byte()/em_scene_task_u16() below name the offsets.
 * - D_0028A9A0 (transition substate, em_frame_transition()->substate) and
 *   D_00282157 (audio busy) are owned elsewhere and read through the
 *   r_0028A9A0/r_00282157 readers in em_scene_workers.h.
 *
 * Widths follow the original loads/stores (decomp C and splat .s): the request
 * block, area bytes, flags and scratchpad bytes are u8; D_00810E70/E74 are the
 * u16 button words (001AE7E0 reads E74 with lhu); D_00810E50 is u8 (lbu in
 * 001AE7E0); D_00810750 and spad 3B68 are s32 (001AE5E0/001AE6B0 add 1);
 * spad 3B84 is a u16 (001AE6B0, 001AFCF0 sh); spad 3B8A is the 16-bit walk
 * count (001AFD70 sh); spad 3258 and 31F4 are words (001AFCF0, 001AFCA0 sw).
 * Multi-byte values are native integers holding the original value; nothing
 * here is a binary overlay of PS2 memory.
 *
 * POD, stdint only, no dependency on any port subsystem.
 */
#ifndef EM_SCENE_STATE_H
#define EM_SCENE_STATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------ request block */

/* D_008106B0..D_008106F7: the 0x48 bytes 001AFCF0 memsets (func_00121A28(
 * D_008106B0, 0, 0x48)). Index = original address - 0x008106B0. Only the
 * bytes the coordinator or its named writers use get a name; the rest are
 * still stored (and cleared) so the memset stays exact. */
#define EM_SCENE_REQ_BASE 0x008106B0u
#define EM_SCENE_REQ_SIZE 0x48u

typedef enum {
    /* Comments state only what the cited original code does with the byte;
     * writer sets are in the design, section 3.2. */
    EM_SCENE_REQ_B0 = 0x00, /* D_008106B0: nonzero -> 001AE7E0 returns 2 */
    EM_SCENE_REQ_B1 = 0x01, /* D_008106B1 */
    EM_SCENE_REQ_B3 = 0x03, /* D_008106B3: nonzero -> 001AE7E0 returns 0 before the 0x800/0x10 test */
    EM_SCENE_REQ_B5 = 0x05, /* D_008106B5: 001AD010 copies it to D_00810700 */
    EM_SCENE_REQ_B6 = 0x06, /* D_008106B6: 001AD010: 0xFF -> D_00810701 = D_00810730[B5] & 0x7F */
    EM_SCENE_REQ_B7 = 0x07, /* D_008106B7: 001AD010 copies it to D_00810702 */
    EM_SCENE_REQ_B8 = 0x08, /* D_008106B8: 001AE7E0 -> 0; 001AD010 arm B8 == 2 */
    EM_SCENE_REQ_B9 = 0x09, /* D_008106B9: 001AE7E0 -> 0; 0x1AE040 state 1 -> 001AD140 */
    EM_SCENE_REQ_C4 = 0x14, /* D_008106C4: 0x1AE040 writes 2 (r==1), 1 (r==2), 0 (states 2, 5) */
    EM_SCENE_REQ_C5 = 0x15, /* D_008106C5: nonzero -> 001AE7E0 returns 2 */
    EM_SCENE_REQ_C6 = 0x16, /* D_008106C6 */
    EM_SCENE_REQ_C7 = 0x17, /* D_008106C7 */
    EM_SCENE_REQ_C8 = 0x18, /* D_008106C8..CB: the area flag word (lw/sw), written by
                             * 001B0250 from the spawn record +0x1C (S12a), returned
                             * by 001B0070; 001C1EA0 selects the weather effect by it */
    EM_SCENE_REQ_CE = 0x1E, /* D_008106CE: nonzero -> 001AE7E0 returns 3; state 6 tests == 2 */
    EM_SCENE_REQ_CF = 0x1F, /* D_008106CF: argument of 001FF030/001FEFE0 in 0x1AE040 state 6 */
    EM_SCENE_REQ_D5 = 0x25, /* D_008106D5 */
    EM_SCENE_REQ_EF = 0x3F, /* D_008106EF: 0x1AE040 state 3 writes 0x46 */
    EM_SCENE_REQ_F3 = 0x43, /* D_008106F3 */
    EM_SCENE_REQ_F5 = 0x45  /* D_008106F5 */
} EmSceneReqByte;

/* ------------------------------------------------------- game progress (D2)
 *
 * Lead decision D2 (SCENE_COORDINATOR_DESIGN.md 10.3): the 0x640-byte block
 * D_00810700..D_00810D3F that 001AF2C0 resets (func_00121A28(D_00810700, 0,
 * 0x640), src/func_001AF2C0.c) has ONE canonical owner, the EmProgress region
 * below, addressed by original address. A port mirror of one of these bytes
 * becomes an accessor over this region in the step that first touches it; no
 * step adds a second copy.
 *
 * The region is only as canonical as its migrated ranges. Every other byte of
 * it is RESERVED: it is still owned by a named EmSceneState field (the area
 * bytes D_00810700..702, D_00810730[] and the D_00810750 counter) or by a port
 * mirror that has not been migrated yet (for example g.opening_event_39 =
 * D_00810791, g.opening_complete = D_00810811, g.cine_step = D_00810813,
 * g.terminal_powered = D_0081084C bit 7, the em_pickup item counts from
 * D_00810C64, the magazine and battery bytes). em_scene_progress_at() refuses
 * a reserved byte (NULL), so nothing can read or write a second copy through
 * it.
 *
 * Migrated ranges (step that migrated them; original readers and writers):
 *   D_00810788           S10b  001B65C0 prime-pass mode (tested == 0xFF),
 *                              001B6660 case 6 via D_00810700[0x88]; no port
 *                              mirror existed.
 *   D_00810794           S12a  event 0x3C (D_00810758[0x3C]), read by the
 *                              record-13 manager 008257A0 through 001BA1C0;
 *                              no port mirror, no port writer.
 *   D_00810860..D_00810B3F
 *                        S10b  per-area taken bits, u32[8] per area
 *                              (001B11E0 test, 001B1190 set, 001B64F0 clear);
 *                              migrated from em_pickup's taken[] mirror.
 *   D_00810B40..D_00810B5F
 *                        S10b  first-visit bits (001B65C0); no port mirror.
 *   D_00810CA4..D_00810CA7
 *                        S10b  equipment bytes read by 0015C310 (player
 *                              attachment spawn): CA4/CA6 migrated from
 *                              em_pickup's primary/secondary mirror; CA5/CA7
 *                              had no port storage.
 */
#define EM_SCENE_PROGRESS_BASE 0x00810700u
#define EM_SCENE_PROGRESS_SIZE 0x640u

typedef struct {
    uint8_t bytes[EM_SCENE_PROGRESS_SIZE]; /* index = original address - 0x00810700 */
} EmProgress;

/* 1 when every byte of [address, address + size) is in a migrated range. */
static inline int em_scene_progress_canonical(uint32_t address, uint32_t size)
{
    static const struct {
        uint32_t first, end;
    } migrated[] = {
        {0x00810788u, 0x00810789u},
        {0x00810794u, 0x00810795u},
        {0x00810860u, 0x00810B60u}, /* taken bits, then the first-visit bits */
        {0x00810CA4u, 0x00810CA8u},
    };
    if (size == 0 || address + size < address)
        return 0;
    for (size_t i = 0; i < sizeof migrated / sizeof migrated[0]; ++i)
        if (address >= migrated[i].first && address + size <= migrated[i].end)
            return 1;
    return 0;
}

/* ------------------------------------------------------------------ faults */

typedef enum {
    EM_SCENE_FAULT_NONE = 0,
    EM_SCENE_FAULT_NULL_WORKER = 1,   /* a reached worker/reader is NULL */
    EM_SCENE_FAULT_WORKER_FAILED = 2, /* a worker returned a negative value */
    EM_SCENE_FAULT_BAD_RESULT = 3,    /* a worker result outside the original range */
    EM_SCENE_FAULT_BAD_INDEX = 4,     /* an original index outside the owned storage */
    EM_SCENE_FAULT_FREED_NEXT = 5     /* 001AFD70 walk reached a freed next node (S4) */
} EmSceneFaultCode;

/* Latched fail-stop record: `address` is the original function (or data)
 * address the fault is attributed to. The first fault wins; afterwards the
 * task does nothing (design section 3.1 "Return convention"). */
typedef struct {
    uint32_t address;
    int32_t code; /* EmSceneFaultCode */
} EmSceneFault;

/* -------------------------------------------------------------- the state */

typedef struct {
    /* Request block D_008106B0..F7 (see EmSceneReqByte). */
    uint8_t req[EM_SCENE_REQ_SIZE];

    /* D_00810700/701/702 (written by 001AD010 and 001AD360) and
     * D_00810730[0x20] (0x00810730..0x0081074F; D_00810750 follows). */
    uint8_t d810700, d810701, d810702;
    uint8_t d810730[0x20];

    /* Frame counters incremented by 001AE5E0/001AE6B0. */
    int32_t d810750;
    int32_t spad3B68;

    /* Scratchpad 0x70003Bxx / 0x700032xx / 0x700031xx. */
    uint16_t spad3B84; /* incremented by 001AE6B0 while 3B92 != 0 */
    uint16_t spad3B8A; /* 001AFD70 visited-node count */
    uint8_t spad3B8C;
    uint8_t spad3B8D;  /* world-frame selector: 0 = 001AE5E0, else 001AE6B0 */
    uint8_t spad3B8E;
    uint8_t spad3B8F;
    uint8_t spad3B90;  /* 001ACEC0 writes 2 every tick */
    uint8_t spad3B91;  /* 001AE6B0: 1 -> 2 when D_0028A9A0 == 0 and E74 & 0x900 */
    uint8_t spad3B92;  /* gates the 3B84 increment in 001AE6B0 */
    uint8_t spad3B93;  /* 001AD010 routes to +9=3 when nonzero */
    uint32_t spad3258; /* cleared by 001AFCF0 */
    uint32_t spad31F4; /* cleared by 001AFCA0 */

    /* Flags. */
    uint8_t d275BD8; /* 001ADF50/001AD4E0 set 1 and wait for 0; 0x1AE040 states 1, 6 test it */
    uint8_t d275BDC; /* set to 1 by 001ADF00 */
    uint8_t d275BE0; /* 001ACEC0 +8=0 branch; set to 1 by 0x1AE040 state 2 r==2 */
    uint8_t d8101E4; /* cleared by 0x1AE040 state 4 */

    /* Input in the ORIGINAL layout (step C, 001B57E0): the original halfword
     * values, not a remapped native mask. Design 3.2 pad map: START 0x800,
     * TRIANGLE 0x10, SELECT 0x100, CROSS 0x40. */
    uint16_t d810E74; /* pressed edge word */
    uint16_t d810E70; /* held word */
    uint8_t d810E50;  /* 001AE7E0: != 4 returns 1 (same arm as E74 & 0x100) */

    EmSceneFault fault;

    /* D_00810700..D_00810D3F (D2); reach it only through the accessors below. */
    EmProgress progress;

    /* Scratchpad 0x70003B40..0x70003B5C: 001B07C0 copies the placed player's
     * +0xB0..+0xCC here (S12a). Read later by the door cut 0018CBD0 (3B50). */
    float spad3B40[8];
} EmSceneState;

/* ---------------------------------------------------------------- accessors */

static inline uint8_t em_scene_req_get(const EmSceneState *s, EmSceneReqByte b)
{
    return s->req[b];
}

static inline void em_scene_req_set(EmSceneState *s, EmSceneReqByte b, uint8_t value)
{
    s->req[b] = value;
}

/* The request block's little-endian word at index `b` (b..b+3; lw/sw in the
 * original), e.g. EM_SCENE_REQ_C8. */
static inline uint32_t em_scene_req_u32(const EmSceneState *s, EmSceneReqByte b)
{
    return (uint32_t)s->req[b] | (uint32_t)s->req[b + 1] << 8 | (uint32_t)s->req[b + 2] << 16 |
           (uint32_t)s->req[b + 3] << 24;
}

static inline void em_scene_req_set_u32(EmSceneState *s, EmSceneReqByte b, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        s->req[b + i] = (uint8_t)(value >> (8 * i));
}

/* Byte of the request block by ORIGINAL address; NULL outside
 * D_008106B0..D_008106F7 (the caller faults with EM_SCENE_FAULT_BAD_INDEX). */
static inline uint8_t *em_scene_req_at(EmSceneState *s, uint32_t original_address)
{
    if (original_address < EM_SCENE_REQ_BASE ||
        original_address >= EM_SCENE_REQ_BASE + EM_SCENE_REQ_SIZE)
        return NULL;
    return &s->req[original_address - EM_SCENE_REQ_BASE];
}

/* D_00810730[index]. 001AD010 indexes it with B5 and 001AD360 with
 * D_00810700; an index >= 0x20 would address D_00810750.. in the original,
 * which this storage does not alias: NULL, and the caller faults. */
static inline uint8_t *em_scene_d810730_at(EmSceneState *s, unsigned index)
{
    return index < sizeof s->d810730 ? &s->d810730[index] : NULL;
}

/* Latch a fault (first one wins). Always returns -1 so a core can
 * `return em_scene_fault(...)`. */
static inline int em_scene_fault(EmSceneState *s, uint32_t address, EmSceneFaultCode code)
{
    if (s->fault.code == EM_SCENE_FAULT_NONE) {
        s->fault.address = address;
        s->fault.code = (int32_t)code;
    }
    return -1;
}

static inline int em_scene_faulted(const EmSceneState *s)
{
    return s->fault.code != EM_SCENE_FAULT_NONE;
}

/* -------------------------------------------------- progress accessors (D2) */

/* The `size` canonical bytes at ORIGINAL address `address`, or NULL when any
 * of them is reserved (see EmProgress). Multi-byte values keep the EE
 * little-endian byte order. */
static inline uint8_t *em_scene_progress_at(EmSceneState *s, uint32_t address, uint32_t size)
{
    if (!s || !em_scene_progress_canonical(address, size))
        return NULL;
    return &s->progress.bytes[address - EM_SCENE_PROGRESS_BASE];
}

/* The byte view D_00810758..D_00810B5F the state-0 spawners read and write
 * (em_actor_roster.h EmActorRosterProgress has exactly this layout). Of it,
 * only D_00810788 and D_00810860..D_00810B5F are canonical: a caller must
 * refuse any roster record whose 001B6660 condition reads another byte (ids
 * 2..6 read D_00810758[i], D_008107D8[i] or D_00810778). */
#define EM_SCENE_PROGRESS_SPAWN_VIEW 0x00810758u
#define EM_SCENE_PROGRESS_SPAWN_VIEW_END 0x00810B60u
static inline uint8_t *em_scene_progress_spawn_view(EmSceneState *s)
{
    return &s->progress.bytes[EM_SCENE_PROGRESS_SPAWN_VIEW - EM_SCENE_PROGRESS_BASE];
}

/* 001AF2C0's effect on the region: the 0x640-byte memset, then its stores
 * that land on migrated bytes (CA4 = 0xFF, CA5 = 5, CA6 = 0, CA7 = 7;
 * src/func_001AF2C0.c). Its other stores go to their mirrors (em_pickup_reset,
 * game_state_new_game). The named area bytes and D_00810750 are outside this
 * reset: the port has no w_001AD230 yet (S12a), and the legacy load writes the
 * 001AD360 area bytes after it. */
static inline void em_scene_progress_reset_001AF2C0(EmSceneState *s)
{
    for (size_t i = 0; i < EM_SCENE_PROGRESS_SIZE; ++i)
        s->progress.bytes[i] = 0;
    s->progress.bytes[0x00810CA4u - EM_SCENE_PROGRESS_BASE] = 0xFF;
    s->progress.bytes[0x00810CA5u - EM_SCENE_PROGRESS_BASE] = 5;
    s->progress.bytes[0x00810CA6u - EM_SCENE_PROGRESS_BASE] = 0;
    s->progress.bytes[0x00810CA7u - EM_SCENE_PROGRESS_BASE] = 7;
}

/* ----------------------------------------------------------- task bytes */

/* Original task-record offsets used by the coordinator (record at
 * *(void **)0x70003B6C). They live in EmTask.user; offset +k is user[k-8]. */
enum {
    EM_SCENE_TASK_08 = 0x08, /* 001ACEC0 state */
    EM_SCENE_TASK_09 = 0x09, /* 001AD250 state */
    EM_SCENE_TASK_0A = 0x0A, /* 001ADF50/001AD360/001AD4E0 step */
    EM_SCENE_TASK_0B = 0x0B, /* 0x1AE040 state 0..6 */
    EM_SCENE_TASK_0C = 0x0C, /* 0x1AE040 sub-step */
    EM_SCENE_TASK_0D = 0x0D, /* cleared by state 1 r==1 and state 2 r==2 */
    EM_SCENE_TASK_10 = 0x10, /* cleared by 001AD360 step 3 */
    EM_SCENE_TASK_11 = 0x11, /* cleared on status/pause entry */
    EM_SCENE_TASK_18 = 0x18, /* u16: 001AD4E0 countdown, cleared by 001AD360 */
    EM_SCENE_TASK_FIRST = 0x08,
    EM_SCENE_TASK_END = 0x20 /* EM_TASK_USER_BYTES == 24 */
};

/* Pointer to the task byte at ORIGINAL record offset `original_offset`, or
 * NULL when the offset is outside +8..+0x1F. */
static inline uint8_t *em_scene_task_byte(uint8_t *user, unsigned original_offset)
{
    if (!user || original_offset < EM_SCENE_TASK_FIRST || original_offset >= EM_SCENE_TASK_END)
        return NULL;
    return &user[original_offset - EM_SCENE_TASK_FIRST];
}

/* The halfword at an even ORIGINAL record offset, little-endian as on the
 * EE (e.g. +0x18). Returns 0 and leaves *ok = 0 when out of range. */
static inline uint16_t em_scene_task_u16(const uint8_t *user, unsigned original_offset, int *ok)
{
    int valid = user && !(original_offset & 1u) && original_offset >= EM_SCENE_TASK_FIRST &&
                original_offset + 2 <= EM_SCENE_TASK_END;
    if (ok)
        *ok = valid;
    if (!valid)
        return 0;
    const uint8_t *p = &user[original_offset - EM_SCENE_TASK_FIRST];
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static inline int em_scene_task_set_u16(uint8_t *user, unsigned original_offset, uint16_t value)
{
    if (!user || (original_offset & 1u) || original_offset < EM_SCENE_TASK_FIRST ||
        original_offset + 2 > EM_SCENE_TASK_END)
        return -1;
    uint8_t *p = &user[original_offset - EM_SCENE_TASK_FIRST];
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* EM_SCENE_STATE_H */
