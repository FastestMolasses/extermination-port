/* em_frame_kick.h - the main loop's steps V and W (original main 0x1AAE40,
 * docs/MAIN_LOOP_AND_GAP.md): 001D2300, the frame's DMA list and its kick,
 * and 001D2580, the field store. Docs: docs/RENDER_CONTEXT.md section 9.
 *
 * Hand translation of these original functions (boot ELF SCUS-97112):
 *   001D2300  (NEARMISS C; the .s was followed) the frame's main list at
 *             D_0028F700 + (context +0x9C << 14): a REF of the GS block
 *             D_00275674 (one quadword) at the channel-1 cursor (context
 *             +0x14) and 001D1F80(1, 0, 7); the two half-pixel offsets
 *             001015A8 / 00101810 on the slot's draw environment (GS block
 *             + 0x190 * slot + 0x40 / + 0xC0) from 0x70003B70 / 72 and
 *             1 - D_00810E88; 001D2110 (list cursor context +0x08 = the
 *             slot's list); REF of the slot's draw environment (+0x20, 0x19
 *             quadwords); REF of the clear packet, GS block +0x420 when
 *             render flag 3 is set (then 001D2830(3, 0)), else +0x3A0 (8
 *             quadwords each); then, when D_008106C4 == 0, 001E0DF0 under
 *             flag 4 clear and flag 0x20 set, and NEXT tags through
 *             channel 0, the chain page (context +0x00 / +0x04) and
 *             channel 1; when D_008106C4 != 0, channel 1 first, then
 *             channel 0 and the page; finally 001D21E0
 *   001D2110, 001D2130, 001D2160, 001D2180  the list-cursor helpers
 *             (byte-matched; each tag is byte +3 = the DMA id, word +4 =
 *             the address, halfword +0 = the quadword count)
 *   001D21E0  (NEARMISS C; the .s was followed) the closing NEXT tag to GS
 *             block + 0x10 at the list cursor, then the hardware kick of the
 *             list: its DMAC / VIF1 register edits, 0011B9E0, the syscall
 *             stub 0010BAA0 and 00101F08(channel 1, the list) are the
 *             renderer boundary, the worker w_kick (the port presents the
 *             frame with its own renderer)
 *   001015A8, 00101810  (SDK, asm-word units) XYOFFSET of a draw
 *             environment: from its SCISSOR (+0x30) width w and height h,
 *             x = (cx - (w + 1) / 2) * 16, y = (cy - (h + 1) / 2) * 16,
 *             plus 8 when the half-offset argument is nonzero; +0x20 =
 *             x | y << 32 (64-bit arithmetic)
 *   001D2580  (byte-matched) context word +0x98 = a0 (the field bit)
 *
 * Memory model: every load and store goes to its original address through
 * the host's `mem` callback (host bytes of an original range, writable when
 * asked). An address it does not map faults. Multi-byte values are
 * little-endian, as on the EE. Nothing else is read.
 *
 * Fail-stop: a NULL worker, a negative worker result or an unmapped address
 * latches the fault (original function and data address) and returns -1. */
#ifndef EM_FRAME_KICK_H
#define EM_FRAME_KICK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_FRAME_KICK_D_00275670 UINT32_C(0x00275670) /* word: the render context */
#define EM_FRAME_KICK_D_00275674 UINT32_C(0x00275674) /* word: the GS blocks */
#define EM_FRAME_KICK_D_0028F700 UINT32_C(0x0028F700) /* the packet arena */
#define EM_FRAME_KICK_D_00810E88 UINT32_C(0x00810E88) /* halfword: the field bit */
#define EM_FRAME_KICK_D_008106C4 UINT32_C(0x008106C4) /* byte: the status-frame request */
#define EM_FRAME_KICK_SPR_3B70   UINT32_C(0x70003B70) /* halfwords 3B70 / 3B72: the screen centre */

typedef struct {
    void *ctx;
    /* Host bytes of original [address, address + size), writable when
     * `write` != 0, or NULL (unmapped: a fault). */
    uint8_t *(*mem)(void *ctx, uint32_t address, uint32_t size, int write);
    int (*w_001D1F80)(void *ctx, int32_t a0, int32_t a1, int32_t a2);
    int (*w_001D2910)(void *ctx, int32_t a0, int32_t *ret);   /* v0 -> *ret */
    int (*w_001D2830)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001E0DF0)(void *ctx);
    /* 001D21E0's hardware tail: the DMA kick of the list at `chain`
     * (D_0028F700 + (context +0x9C << 14)). */
    int (*w_kick)(void *ctx, uint32_t chain);
} EmFrameKickWorkers;

typedef struct {
    uint32_t address; /* the original function executing */
    uint32_t data;    /* the data address or callee involved */
} EmFrameKickFault;

/* 001D2300(). 0, or -1 (fault latched). */
int em_frame_kick_001D2300(const EmFrameKickWorkers *w, EmFrameKickFault *fault);
/* 001D2580(a0). 0, or -1. */
int em_frame_kick_001D2580(const EmFrameKickWorkers *w, int32_t a0, EmFrameKickFault *fault);
/* The condition under which 001D2300 calls 001E0DF0 (D_008106C4 == 0,
 * 001D2910(4) == 0, 001D2910(0x20) != 0), evaluated by the same code the
 * translation runs: *calls = 1 or 0. 0, or -1. */
int em_frame_kick_calls_001E0DF0(const EmFrameKickWorkers *w, int *calls, EmFrameKickFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_FRAME_KICK_H */
