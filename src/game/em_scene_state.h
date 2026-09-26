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
 * mirror that has not been migrated yet (for example g.opening_complete =
 * D_00810811, em_weapon's
 * D_00810C61/C62/CB4). em_scene_progress_at() refuses
 * a reserved byte (NULL), so nothing can read or write a second copy through
 * it.
 *
 * Migrated ranges (step that migrated them; original readers and writers):
 *   D_00810707           HK    0015CF90's copy of the player's infected
 *                              latch +0x234 (every player stage;
 *                              em_player_0015BCF0), 0021C270 (=1) and
 *                              0021E830 (=2) through the stage globals'
 *                              and EmPlayerMajor2Scene's pointer; read
 *                              by 001B07C0 into +0x234. Before HK the
 *                              port kept no copy (001B07C0 read the live
 *                              +0x234, g.pd_infected).
 *   D_00810758           L22   event 0 (D_00810758[0]): Roger 008237E0's
 *                              001BA1C0(Roger, 0) in its lifecycle 0 (0xFF
 *                              keeps him out), set to 1 by the encounter
 *                              script 0x8283D0's op06 sub 0; no port mirror.
 *   D_00810788           S10b  001B65C0 prime-pass mode (tested == 0xFF),
 *                              001B6660 case 6 via D_00810700[0x88]; no port
 *                              mirror existed.
 *   D_0081078B           L13   event 0x33 (D_00810758[0x33]): the walking
 *                              camera's 00191210 (area 0x10 room 0) keeps
 *                              its eye clamp while it is not 0xFF; no port
 *                              mirror, no AREA11 writer.
 *   D_0081078F           L22   event 0x37: 001B81D0 (the scripted player
 *                              face attach) takes resource row 0x18 when it
 *                              is 1; no port writer (0 on the route).
 *   D_00810791           L22   event 0x39: the opening script 0x828FC0's
 *                              op06 sub 0 (1) and its op07 sub 5 (0xFF)
 *                              (em_opening_runtime.c), read by Roger
 *                              008237E0 (1 suppresses him); migrated from
 *                              g.opening_event_39.
 *   D_00810792           HK    event 0x3A (D_00810758[0x3A]): the truck
 *                              trigger 008251E0 stores 1 when its camera
 *                              script ends, the truck 00823FF0 stores 0xFF
 *                              after the fall; both read it (00823FF0 and
 *                              008251E0 are em_truck_original, not bound:
 *                              WP-12 hands it this byte). No port mirror.
 *   D_00810793           HK    event 0x3B (D_00810758[0x3B]): read by the
 *                              director 008253F0 (001BA1C0 in its state 0)
 *                              and Roger 008237E0; written by the area
 *                              script's op 6 (001BA080 sub 0/1 = 1 /
 *                              0xFF). Not bound (WP-9/WP-10); no port
 *                              mirror (EmRogerStory.alternate is the
 *                              unbound Roger translation's value view).
 *   D_00810794           S12a  event 0x3C (D_00810758[0x3C]), read by the
 *                              record-13 manager 008257A0 through 001BA1C0;
 *                              no port mirror, no port writer.
 *   D_008107D8           L22   counter 0 (D_008107D8[0]): Roger's story
 *                              progress (bit 0 after the encounter,
 *                              0x823AB0; bit 0x80 starts his departure),
 *                              also written by 001B82D0 sub 6; no port
 *                              mirror.
 *   D_00810813           HK    counter 0x3B (D_008107D8[0x3B]), the
 *                              director's beat step: 008253F0's beat
 *                              completions store 0x10/0x20/0xFF (live
 *                              today through the legacy director stand-in
 *                              em_director.c until WP-10 binds
 *                              em_director_original), Roger 008237E0
 *                              stores 0x11 (0x823A04) and the area
 *                              script writes it (op 6, 001BA080 sub
 *                              3/5/6; 001B82D0 sub 6); migrated from g.cine_step
 *                              (whose per-area-build reset had no
 *                              original writer: only 001AF2C0's memset
 *                              clears it).
 *   D_00810803           L13   counter 0x2B (D_008107D8[0x2B]): the walking
 *                              camera's 00195130 area-0 arm gate; no port
 *                              mirror, no AREA11 writer.
 *   D_0081083A           WP-4  the AREA11 elevator's floor byte: 00827B10
 *                              state 0 reads it (190/230 heights), its
 *                              completion toggles it when powered; no
 *                              port mirror existed (EmElevator.lower is
 *                              the owner's view, loaded and stored around
 *                              each owner callback).
 *   D_0081083C           L01   the player's grab-slot bits: 0012E0B0 claims a
 *                              free bit (0..3 behind the player, 4..7 in
 *                              front), 0012E070 releases one; read by
 *                              0021C440 on every player stage (nonzero
 *                              enters the +4 = 2 +5 = 0xB reaction), by
 *                              0021F330 and by 00182BF0. No port mirror and
 *                              no port writer (the latching enemies'
 *                              0012E0B0 / 0012E070 are not ported, and no
 *                              AREA11 owner calls them: 0 in every route
 *                              capture); the player stage's worker view
 *                              loads it before every stage
 *                              (em_player_stage_live.c).
 *   D_0081084C           WP-4  D_00810841[0x0B], AREA11's power byte:
 *                              001580C0 sets bit (1 << panel +0x2E) = 0x80,
 *                              00159210 state 0 and 00827B10 test it;
 *                              migrated from g.terminal_powered. Other
 *                              areas' D_00810841 bytes stay reserved.
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
 *   D_00810C60, D_00810C63..D_00810CA3, D_00810CA8..D_00810CB3,
 *   D_00810CB6..D_00810D1F
 *                        WP-6  the item block 001C40B0 and 001C4720/4760 write
 *                              (the equipment status C60, the pack count C63,
 *                              the item counts D_00810C64[t], the meters
 *                              CA8..CB0, the battery charge CB2 (s16) and
 *                              capacity CB7, the map bytes D_00810CB8[t] and
 *                              key bytes D_00810CC3[t], which overlap the
 *                              counts exactly as in the original); migrated
 *                              from em_pickup's separate count/map/key/meter
 *                              mirrors. HK moved the last key-byte mirror
 *                              (g.opening_key_item_zero, the opening's
 *                              001C4760(0, 1)) here: 001C4760 has one
 *                              translation, em_director_original_001C4760,
 *                              bound over this region by
 *                              em_director_original_001C4760_scene. D_00810CB6 is
 *                              read by 0015BA50's busy test through the
 *                              stage scene's pointer. D_00810C61 (the fire mode), D_00810C62
 *                              (the loaded magazine) and D_00810CB4 (the
 *                              reserve) stay em_weapon's (w.fire_mode, w.mag,
 *                              w.reserve) and are reserved here.
 *   D_00810D38..D_00810D3B
 *                        WP-5  the current-BGM word (sw/lw): 001ADF00 and
 *                              001AD740 store 0, 001FB0B0 stores its cue
 *                              (no AREA11 or boot caller: only other areas'
 *                              overlays jal it), 001FAE70 and 001FAFD0 read
 *                              it. No port mirror existed.
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
        {0x00810707u, 0x00810708u}, /* 0015CF90's infected-latch copy (HK) */
        {0x00810758u, 0x00810759u}, /* event 0: Roger's 001BA1C0, 0x8283D0's 06/0 (L22) */
        {0x00810788u, 0x00810789u},
        {0x0081078Bu, 0x0081078Cu}, /* event 0x33: 00191210's gate (L13) */
        {0x0081078Fu, 0x00810790u}, /* event 0x37: 001B81D0's face gate (L22) */
        {0x00810791u, 0x00810795u}, /* event 0x39 (L22), events 0x3A, 0x3B (HK), 0x3C (S12a) */
        {0x008107D8u, 0x008107D9u}, /* counter 0: Roger's story progress (L22) */
        {0x00810803u, 0x00810804u}, /* counter 0x2B: 00195130's area-0 gate (L13) */
        {0x00810813u, 0x00810814u}, /* counter 0x3B, the director step (HK) */
        {0x0081083Au, 0x0081083Bu}, /* AREA11 elevator floor (WP-4) */
        {0x0081083Cu, 0x0081083Du}, /* the player's grab-slot bits (L01) */
        {0x0081084Cu, 0x0081084Du}, /* D_00810841[0x0B], AREA11 power (WP-4) */
        {0x00810860u, 0x00810B60u}, /* taken bits, then the first-visit bits */
        {0x00810C60u, 0x00810C61u}, /* equipment status C60 (WP-6) */
        {0x00810C63u, 0x00810CB4u}, /* item block and CA4..CA7 (S10b) */
        {0x00810CB6u, 0x00810D20u}, /* item block, em_pickup (WP-6) */
        {0x00810D38u, 0x00810D3Cu}, /* current-BGM word (WP-5) */
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
    uint8_t d8101E4; /* 0x1AE040 state 4's store (0x1AE0BC): the frame core's view of
                      * the camera block's +0x04, stored to its one storage
                      * (g.cam.top_mode) at the next worker boundary, 0018D7B0
                      * (em_scene_bindings.c); every reader reads g.cam.top_mode */

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
