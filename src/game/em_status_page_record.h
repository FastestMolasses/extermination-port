/* em_status_page_record.h - the BATTERY page 002149F0 over the original
 * status records (docs/STATUS_PAGE_RECORD.md). Prefix em_spr_.
 *
 * Hand translation of these original routines (boot ELF SCUS-97112):
 *   002149F0  the ITEM > BATTERY sub-page (ITEM child, module 0x21): a
 *             nine-state machine on the page record's +5 byte (NEARMISS C
 *             99.97 %; this translation follows the .s, whose only
 *             difference from the C is a register swap in state 6)
 *   0020CD80  the "no target" cue thunk: 001FB9F0(2, 0x1000, 0x1000,
 *             0x1000) (the no-device path of state 1)
 *
 * The routine works on the original bytes, addressed by original offset:
 *   - the page record `page` (the 0xA0-byte UI block at D_00810130): +1..+6,
 *     +0x12 (charge at entry), +0x13 (message line), +0x17 (cursor), +0x18
 *     (row count), +0x19, +0x1A, +0x1B (acquired kind), +0x1C (s16),
 *     +0x1E (s16 item base 0x1B), +0x30 (owner record address, 32-bit),
 *     +0x3C (s16 unit timer) and the kind ring +0x50..;
 *   - the game-block bytes the pointers in EmSprRecords name (little-endian
 *     multi-byte fields, exactly as the original reads them: D_00810CB2 is
 *     written as a halfword and also read as its low byte);
 *   - the message words D_002821B0 / B4 / B8 and D_00282240;
 *   - the scratchpad mode byte 0x70003B8D;
 *   - the owner record (the panel, or what 00185420 returns) through the
 *     owner_read / owner_write workers: +3 (type byte), +0x34 (s16 cost)
 *     read, +0xA / +0xB written.
 * Every callee is an explicit worker named by its original address: the
 * page draws 0020A7A0 / 0020AE40 / 0020B210 / 0020B0D0 / 0020CCB0 /
 * 0020BBE0 / 0020BC50, the cues 0020CD40 / 0020CD60 / 0020CD80 / 0020CDA0,
 * the message line 001FCF10, the blend 00207D00, the unit sound 001FB9F0
 * and the device lookup 00185420. The workers receive exactly the
 * arguments the original passes.
 *
 * Fail-stop:
 *   - a NULL worker or record pointer, or a page shorter than 0xA0 bytes:
 *     -1 before anything is written or called;
 *   - a worker returning a negative value, a ring index outside the page
 *     (the original would read the neighbouring game block), or a zero owner
 *     address the original would dereference: -1 at that point; what was
 *     written and called before it stays, in the original order.
 * The optional EmSprFault names the original address of the failure.
 *
 * No float arithmetic. Oracle: tools/test_status_page_record_reference.py
 * executes the original 002149F0 / 0020CD80 over captured RAM (the status
 * hub, the panel confirmation, the panel ITEM root and every route beat) and
 * synthetic records, and compares every written byte and every worker call
 * in order; it also composes the page with the verified draw translations
 * (em_status_ui_leftovers, em_census_standins, em_render_verify_rest)
 * against the original draw routines. */
#ifndef EM_STATUS_PAGE_RECORD_H
#define EM_STATUS_PAGE_RECORD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SPR_PAGE_SIZE 0xA0u                        /* the UI block D_00810130 */
#define EM_SPR_PAGE_ADDRESS 0x00810130u               /* its original address */
#define EM_SPR_FRAME_TABLE 0x00265C50u                /* D_00265C50: 0020AE40 / 0020B0D0 */
#define EM_SPR_ROW_TABLE 0x00265CD0u                  /* D_00265CD0: 0020B210 / 0020BC50 */
#define EM_SPR_BACKGROUND_TEX0 UINT64_C(0x20043C859D422150) /* 0020A7A0 argument */
#define EM_SPR_LIST_GLYPH UINT64_C(0x20042D05A1322000)      /* 0020B210 / 0020BC50 argument */

enum {
    EM_SPR_FAULT_NONE = 0,
    EM_SPR_FAULT_NULL = 1,          /* a worker or record pointer is NULL */
    EM_SPR_FAULT_WORKER_FAILED = 2, /* a worker returned a negative value */
    EM_SPR_FAULT_BAD_INPUT = 3      /* ring index outside the page, zero owner */
};

typedef struct EmSprFault {
    uint32_t address; /* the original function (worker) or 0x002149F0 */
    int32_t code;     /* EM_SPR_FAULT_* */
} EmSprFault;

/* The original records the page reads and writes, as pointers into the
 * caller's single storage of those bytes. */
typedef struct EmSprRecords {
    uint8_t *page;          /* D_00810130, at least EM_SPR_PAGE_SIZE bytes */
    size_t page_size;
    uint8_t *d8106B0;       /* the pending status request (read, cleared, set 1) */
    const uint8_t *d8106B1; /* the request kind / item index */
    uint8_t *d8106C5;       /* the status exit request (written 0xFF) */
    const uint8_t *d8106D0; /* the requesting owner's record address, 4 bytes LE */
    const uint8_t *d810C7F; /* owned counts of items 0x1B / 0x1C / 0x1D (3 bytes) */
    uint8_t *d810CB2;       /* the charge in half-units, 2 bytes LE */
    uint8_t *d810CB7;       /* the capacity */
    const uint8_t *d810E74; /* this frame's button / event bits, 2 bytes LE */
    int32_t *d2821B0;       /* message request block: kind */
    int32_t *d2821B4;       /* message request block: phase */
    int32_t *d2821B8;       /* message request block: line */
    int32_t *d282240;       /* message group */
    uint8_t *spad3B8D;      /* scratchpad 0x70003B8D, the mode byte */
} EmSprRecords;

/* Each worker returns 0 (or any non-negative value) on success and a
 * negative value on a fault. `page` is records->page. */
typedef struct EmSprWorkers {
    void *context;
    /* 0020A7A0(tex0): the status background */
    int (*background)(void *context, uint64_t tex0);
    /* 0020AE40(page, table, flags): the sub-page frame */
    int (*frame)(void *context, uint8_t *page, uint32_t table, int32_t flags);
    /* 0020B210(page, table, glyph, flags): the list; *result = its return */
    int (*list)(void *context, uint8_t *page, uint32_t table, uint64_t glyph, int32_t flags,
                int32_t *result);
    /* 0020B0D0(page, table): the arrows */
    int (*arrows)(void *context, uint8_t *page, uint32_t table);
    /* 0020BBE0(page, n): refills the five visible rows +0x90 from the
     * ring +0x50 (n = page +0x1A, the wrap event 0020B210 raised) */
    int (*list_refill)(void *context, uint8_t *page, int32_t n);
    /* 0020BC50(page, table, glyph, flags): the list scroll animation of
     * state 2; *result = its return */
    int (*list_scroll)(void *context, uint8_t *page, uint32_t table, uint64_t glyph, int32_t flags,
                       int32_t *result);
    /* 0020CCB0(page): the Yes/No marker */
    int (*marker)(void *context, uint8_t *page);
    /* the UI cue thunks 0020CD40 (accept), 0020CD60 (back), 0020CD80 (no
     * target, em_spr_0020CD80 below), 0020CDA0 (cursor) */
    int (*cue_accept)(void *context);
    int (*cue_back)(void *context);
    int (*cue_refuse)(void *context);
    int (*cue_cursor)(void *context);
    /* 001FCF10(): the message-bank group-5 line of the confirmation */
    int (*message_line)(void *context);
    /* 00207D00(slot, mode) */
    int (*blend)(void *context, int32_t slot, int32_t mode);
    /* 001FB9F0(id, a1, a2, a3) */
    int (*sound)(void *context, int32_t id, int32_t a1, int32_t a2, int32_t a3);
    /* 00185420(item): *owner = the record address it returns (0 = none) */
    int (*find_device)(void *context, int32_t item, uint32_t *owner);
    /* the owner record at original address `owner`: size 1 reads the
     * unsigned byte at `offset`, size 2 the signed halfword */
    int (*owner_read)(void *context, uint32_t owner, uint32_t offset, uint32_t size,
                      int32_t *value);
    /* the byte at owner + offset = value */
    int (*owner_write)(void *context, uint32_t owner, uint32_t offset, uint8_t value);
} EmSprWorkers;

/* One 002149F0(page) call. 0, or -1 on a fault (see above). */
int em_spr_002149F0(const EmSprWorkers *w, const EmSprRecords *r, EmSprFault *fault);

/* 0020CD80(): w->sound(2, 0x1000, 0x1000, 0x1000). 0, or -1 when sound is
 * NULL or fails. */
int em_spr_0020CD80(const EmSprWorkers *w);

#ifdef __cplusplus
}
#endif

#endif
