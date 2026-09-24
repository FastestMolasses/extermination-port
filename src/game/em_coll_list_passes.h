/* em_coll_list_passes.h - the actor list passes of the frame close-out
 * 001AAD00 (census lane L08; docs/COLL_LIST_PASSES.md).
 *
 * Hand translations of the original routines below. All are byte-matched
 * decomp C except 001A7870 (NEARMISS; its .s is the authority). Where the
 * byte-matched C declares a callee with fewer arguments than the call site
 * passes, the .s decides: 001A9F60 calls 001A9E00(player, entry) and
 * 001A9B10 calls 001A99E0(outer, inner) (the entry is still in the second
 * argument register at the call; both callees read it).
 *
 *   001A9D20          class-1 x class-2 pairs -> 001A9C40 (inner type 0,1,4..7)
 *   001A8DA0          class-1 x class-0xD pairs -> 001A8CE0 (type 3, +0xD 0)
 *   001A9F60(player)  class-2 entries (+2 & 0x1F == 2, active, type 0) ->
 *                     001A9E00(player, entry); gated off by 0x70003B8D and
 *                     D_0028A9A0
 *   001AA140          class-2 unordered pairs -> 001AA000(a, b, a+1F0, b+1F0)
 *   001A7870          class-2 capsule push-apart: pass 1 marks +0x50, pass 2
 *                     pushes the inner entry's +B0/+B8 out of the outer
 *                     entry's capsule (0011E748 sqrt, 00128350 + 001000C0 the
 *                     soft-float |len| < 0.001 test)
 *   001A8BE0(player)  player x class-0xD list -> 001A8660 (type 1),
 *                     001A8840 (type 3), 001A8970 (type 5); gated like 001A9F60
 *   001A8660(p, e)    the circle/height overlap of the player and one
 *                     type-1 entry: the entry's +0x34 behaviour, the
 *                     knock-back speed from D_0024A740 / D_0024A780, the
 *                     knock-back direction (00102760), 0x70003B86 = 0
 *   001A9000          class-0xD type-5 x class-4 -> 001A8F40 / 001A8E80
 *   001A97B0          class-0xD x class-2 -> 001A9360 / 001A96F0 / 001A9480
 *   001A9B10          class-2 type-0 x class-4 type-7 -> 001A99E0
 *   001AAD00's hooks  the nine calls above in the original order
 *                     (em_coll_list_passes_001AAD00_hooks); its list swap is
 *                     em_actor_class_lists_swap_001AAD00 (em_actor_collision)
 *
 * Records. The passes read and write the original record bytes by their
 * original offsets: pool actors (0x2F0), the player (0x320) and the records
 * their words point to (+0x30, +0x58, +0x110[]). Every access goes through
 * EmCollListMemory.bytes, which returns the host bytes of an original
 * address range in the original layout, or NULL (a fault). The list arrays
 * are read the same way: entry j of a list is the word at cursor + 4j.
 *
 * Globals. EmCollListGlobals holds every other original word the passes
 * read or write (the live list cursors/counts D_00275B80..D_00275BB8, the
 * scratchpad counters 0x70003B86/88 that the callees may shorten, the
 * gates, the scratch vector 0x700038A0). The passes re-read them where the
 * original re-reads memory, so a worker that changes one (as 001A9360 and
 * 001A99E0 zero 0x70003B88) changes the walk exactly as in the original.
 * D_00810354 (the player's +0xA4) is read through `bytes`.
 *
 * Workers. Every original callee without a translation here is a worker
 * that receives the whole EmCollListPasses. Each routine checks, before
 * its first write, that the memory and every worker it can reach are bound
 * and returns -1 otherwise; em_coll_list_passes_unported (and its
 * 001AA000 / 0021BD10 / behaviour variants) is the explicit fail-stop
 * binding for a callee that has no translation yet (it faults when
 * reached, and `fault` names the call site). A worker that returns a negative value, and a memory
 * range `bytes` cannot give, are faults: the routine returns -1 at once
 * and the writes made before stay, as the original order leaves them
 * (`fault` names the original address). A negative list count is a fault
 * (the original would walk far past the list).
 *
 * Arithmetic: every COP1 operation goes through em_ee_float.h; the
 * soft-float calls are em_sdk_soft_float's translations, 0011E748 is
 * em_sdk_math_original's.
 *
 * Oracle: tools/test_coll_list_passes_reference.py executes the original
 * instructions of every routine above over captured AREA11 RAM (the route
 * beats' lists, and synthetic lists over the captured pool records) and
 * compares every RAM and scratchpad byte, the globals and every worker call
 * (order and arguments). */
#ifndef EM_COLL_LIST_PASSES_H
#define EM_COLL_LIST_PASSES_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_sdk_math_original.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_COLL_LIST_PLAYER 0x008102B0u   /* D_008102B0: the player record */

typedef struct EmCollListPasses EmCollListPasses;

typedef struct EmCollListMemory {
    void *context;
    /* The host bytes of [address, address + size) in the original layout
     * (little-endian, writable), or NULL when the binder has none. */
    uint8_t *(*bytes)(void *context, uint32_t address, uint32_t size);
} EmCollListMemory;

typedef struct EmCollListGlobals {
    uint32_t d275BB0; int16_t d275BB8;   /* class-1 live list: cursor, count */
    uint32_t d275BA0; int16_t d275BA8;   /* class-0xD live list */
    uint32_t d275B90; int16_t d275B98;   /* class-2 / 0xA live list */
    uint32_t d275B80; int16_t d275B88;   /* class-4 live list */
    int16_t s3B86;                       /* 0x70003B86 */
    int16_t s3B88;                       /* 0x70003B88 */
    uint8_t s3B8D;                       /* 0x70003B8D (the frame selector) */
    int16_t d28A9A0;                     /* D_0028A9A0[0] */
    uint8_t d810700;                     /* D_00810700 (area) */
    uint8_t d810702;                     /* D_00810702 */
    uint8_t d81070A;                     /* D_0081070A (001A8660's table pick) */
    uint32_t s38A0[4];                   /* 0x700038A0..0x700038AC (raw bits) */
} EmCollListGlobals;

/* Read-only data from the user's ELF. D_0024A740 (and D_0024A780 =
 * D_0024A740 + 0x40): 001A8660 reads the float at +4d or +0x40 + 4d for the
 * entry's +0xD byte d; `d24A740` holds `d24A740_size` bytes from 0x24A740
 * (0x440 covers every d of both tables). A read beyond it faults. */
typedef struct EmCollListData {
    const uint8_t *d24A740;
    uint32_t d24A740_size;
} EmCollListData;

/* A worker over two records (original addresses). */
typedef int (*EmCollListPairWorker)(void *context, EmCollListPasses *passes, uint32_t a,
                                    uint32_t b);

typedef struct EmCollListWorkers {
    void *context;
    EmCollListPairWorker w_001A8840;     /* 001A8BE0 type 3: (player, entry) */
    EmCollListPairWorker w_001A8970;     /* 001A8BE0 type 5: (player, entry) */
    EmCollListPairWorker w_001A8CE0;     /* 001A8DA0: (outer, inner) */
    EmCollListPairWorker w_001A8E80;     /* 001A9000: (outer, inner) */
    EmCollListPairWorker w_001A8F40;     /* 001A9000: (outer, inner) */
    EmCollListPairWorker w_001A9360;     /* 001A97B0, outer type 5 */
    EmCollListPairWorker w_001A96F0;     /* 001A97B0, outer type 3 */
    EmCollListPairWorker w_001A9480;     /* 001A97B0, outer type 6 */
    EmCollListPairWorker w_001A99E0;     /* 001A9B10: (outer, inner) */
    EmCollListPairWorker w_001A9C40;     /* 001A9D20: (outer, inner) */
    EmCollListPairWorker w_001A9E00;     /* 001A9F60: (player, entry) */
    /* 001AA140: 001AA000(a, b, a + 0x1F0, b + 0x1F0). */
    int (*w_001AA000)(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b,
                      uint32_t a1f0, uint32_t b1f0);
    /* 001A8660: 0021BD10() (its v0 in *result). */
    int (*w_0021BD10)(void *context, EmCollListPasses *passes, int *result);
    /* 001A8660: the call through the entry's +0x34 word `fn`:
     * fn(entry, player, player + 0xB0). */
    int (*behaviour)(void *context, EmCollListPasses *passes, uint32_t fn, uint32_t entry,
                     uint32_t player, uint32_t player_b0);
    /* 001A8660: 00102760(out, in): xyz normalized, w = 0 (bind
     * em_coll_list_passes_normalize). */
    int (*normalize)(void *context, float out[4], const float in[4]);
} EmCollListWorkers;

struct EmCollListPasses {
    EmCollListMemory memory;
    EmCollListGlobals *globals;
    const EmCollListData *data;
    const EmSdkMathContext *math;        /* 0011E748 (001A8660, 001A7870) */
    EmCollListWorkers workers;
    uint32_t fault;                      /* 0, or the original address of the first fault */
};

/* ---- The passes: 0, or -1 on a fault (see the header comment) ----------- */
int em_coll_list_001A9D20(EmCollListPasses *p);
int em_coll_list_001A8DA0(EmCollListPasses *p);
int em_coll_list_001A9F60(EmCollListPasses *p, uint32_t player);
int em_coll_list_001AA140(EmCollListPasses *p);
int em_coll_list_001A7870(EmCollListPasses *p);
int em_coll_list_001A8BE0(EmCollListPasses *p, uint32_t player);
int em_coll_list_001A8660(EmCollListPasses *p, uint32_t player, uint32_t entry);
int em_coll_list_001A9000(EmCollListPasses *p);
int em_coll_list_001A97B0(EmCollListPasses *p);
int em_coll_list_001A9B10(EmCollListPasses *p);

/* 001AAD00's nine hooks in the original order: 001A9D20, 001A8DA0,
 * 001A9F60(player), 001AA140, 001A7870, 001A8BE0(player), 001A9000,
 * 001A97B0, 001A9B10. Stops at the first fault. The list swap that follows
 * them in 001AAD00 is not part of this call. */
int em_coll_list_passes_001AAD00_hooks(EmCollListPasses *p, uint32_t player);

/* 1 when the memory, the data, the SDK context and every worker the nine
 * hooks can reach are bound (the globals are checked at each call). */
int em_coll_list_passes_bound(const EmCollListPasses *p);

/* ---- Worker adapters ------------------------------------------------------- */

/* The explicit bindings for a callee that has no translation yet: each
 * returns -1 without touching passes->fault, so the calling pass records
 * the original address of the call site that reached it (for example
 * 0x1A8C94 for 001A8BE0's 001A8840 call). One per worker signature: the
 * pair workers, 001AA000, 0021BD10 and the +0x34 behaviour. */
int em_coll_list_passes_unported(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b);
int em_coll_list_passes_unported_001AA000(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b,
                                          uint32_t a1f0, uint32_t b1f0);
int em_coll_list_passes_unported_0021BD10(void *context, EmCollListPasses *passes, int *result);
int em_coll_list_passes_unported_behaviour(void *context, EmCollListPasses *passes, uint32_t fn,
                                           uint32_t entry, uint32_t player, uint32_t player_b0);
/* EmCollListWorkers.normalize over em_effect_original_00102760 (the
 * verified translation of the SDK routine); `context` is unused. */
int em_coll_list_passes_normalize(void *context, float out[4], const float in[4]);

#ifdef __cplusplus
}
#endif

#endif /* EM_COLL_LIST_PASSES_H */
