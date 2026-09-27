/* AREA01 render lane, module vif: the per-frame pass over the record table
 * *D_0028A5A4 and the three routines it calls (census a01 delta, subsystem
 * render_vif). Docs: docs/AREA01_RENDER.md.
 *
 * Hand translation of the original functions (boot ELF SCUS-97112; the
 * decomp C where it is byte-matched, the original instructions where the C
 * is a NEARMISS or asm):
 *   001D5BD0  the table pass: for each 0x860-byte record (count = the
 *             table's first word, records from +0x10) it calls 001D5A70;
 *             result 0 -> 001D4FC0(rec); 0xFF -> nothing; any other ->
 *             001D4FC0(rec) then 001D5170(rec)             (byte-matched C)
 *   001D5A70  clip test of the three points at a1 + 0x40, + 0x80, + 0xC0
 *             (w taken as 1.0) through the matrix 001CD370(0): 0xFF when
 *             the three flag sets share a bit, 0 when all are empty, else 1
 *             (asm words)
 *   001D4FC0  channel-3 VIF chain: a 0xE-quadword tag with the 0xE0 bytes
 *             of D_002513D0 copied behind it, 001D2090(3, D_00237450), a
 *             0x30 tag to D_00816A40 + (context +0x9C << 7), 001D1F80(3,
 *             2, 1), one 0x30 tag per 0x1F8-vertex chunk of rec +0 over the
 *             data at rec + 0x40, a 0x60 end tag, then 001CAAC0 with
 *             (rec +0x34, +0x38, +0x3C, 1.0), the first tag's address and
 *             0x60 (NEARMISS C; the instructions were followed)
 *   001D5170  the same chain with 001D4750(3) and D_00237720 in place of
 *             the copied block and D_00237450 (NEARMISS C; instructions)
 * and, inline, the leaves 001CD370 and 00102948.
 *
 * Every other callee is a worker named by its original address. Integer
 * arguments are the original register values. A reached NULL worker faults
 * before the entry writes anything (each entry checks every worker it and
 * its nested translations can reach); a negative worker result faults at
 * once (the entry returns -1 and calls no further worker). The reference
 * test checks both at every entry; docs/AREA01_RENDER.md section 1 lists
 * exactly what it checks. */
#ifndef EM_AREA01_RENDER_VIF_H
#define EM_AREA01_RENDER_VIF_H

#include <stdint.h>

#include "game/em_area01_render_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_A01R_D_0028A5A4 0x0028A5A4u /* the record table address word */
#define EM_A01R_D_002513D0 0x002513D0u /* 001D4FC0's copied 0xE0-byte block */
#define EM_A01R_D_00237450 0x00237450u /* 001D4FC0's 001D2090 source */
#define EM_A01R_D_00237720 0x00237720u /* 001D5170's 001D2090 source */
#define EM_A01R_D_00816A40 0x00816A40u /* 0x80-byte slots indexed by context +0x9C */

typedef struct {
    void *ctx;
    /* 00121870(dst, src, n, a3): block copy (001D4FC0 passes the context
     * address as a3). */
    int (*w_00121870)(void *ctx, uint32_t dst, uint32_t src, int32_t n, uint32_t a3);
    /* 001D2090(chan, src): REF tag append. */
    int (*w_001D2090)(void *ctx, int32_t chan, uint32_t src);
    /* 001D4750(chan): the constant UNPACK block. */
    int (*w_001D4750)(void *ctx, int32_t chan);
    /* 001D1F80(chan, a1, a2, a3): REF tag (a3 is the slot address the
     * caller last computed). */
    int (*w_001D1F80)(void *ctx, int32_t chan, int32_t a1, int32_t a2, uint32_t a3);
    /* 001CAAC0(&v, packet, a2, a3): v is the caller's stack quadword (four
     * words), packet the first tag's address, a2 = 0x60, a3 the context. */
    int (*w_001CAAC0)(void *ctx, const uint32_t v[4], uint32_t packet, int32_t a2, uint32_t a3);
} EmArea01RenderVifWorkers;

typedef struct {
    EmArea01RenderCore core;
    EmArea01RenderVifWorkers workers;
} EmArea01RenderVif;

/* 0 on success, -1 on a fault. */
int em_area01_render_001D5BD0(EmArea01RenderVif *s);
/* *result = the original's v0 (0, 1 or 0xFF); result may be NULL. a0 is
 * not read (the original overwrites it before use). */
int em_area01_render_001D5A70(EmArea01RenderVif *s, uint32_t a0, uint32_t a1, uint32_t *result);
int em_area01_render_001D4FC0(EmArea01RenderVif *s, uint32_t rec);
int em_area01_render_001D5170(EmArea01RenderVif *s, uint32_t rec);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA01_RENDER_VIF_H */
