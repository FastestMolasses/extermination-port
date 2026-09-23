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
    EM_SCENE_REQ_CE = 0x1E, /* D_008106CE: nonzero -> 001AE7E0 returns 3; state 6 tests == 2 */
    EM_SCENE_REQ_CF = 0x1F, /* D_008106CF: argument of 001FF030/001FEFE0 in 0x1AE040 state 6 */
    EM_SCENE_REQ_D5 = 0x25, /* D_008106D5 */
    EM_SCENE_REQ_EF = 0x3F, /* D_008106EF: 0x1AE040 state 3 writes 0x46 */
    EM_SCENE_REQ_F3 = 0x43, /* D_008106F3 */
    EM_SCENE_REQ_F5 = 0x45  /* D_008106F5 */
} EmSceneReqByte;

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
