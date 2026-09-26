/* em_skin_arena_init.h - skin_arena_init (001D2E20), the first callee of the
 * render reset 001D19E0: the 14 skin records at D_00816440 .. D_0081723F
 * get their templates from the boot ELF's .data. The decomp repo's
 * src/skin_arena_init.c is byte-identical C (mwcc 2.3.3).
 *
 * Each record is 0x100 bytes, two 0x80-byte halves (the frame's half is
 * context +0x9C); both halves receive the same 0x80-byte source block. The
 * pairs (destination <- source) are fixed by the original's cursor order:
 * D_00816440 <- D_002514D0, D_00816540 <- D_00251650, D_00816640 <-
 * D_00251550, D_00816740 <- D_002515D0, and D_00816840 .. D_00817140 <-
 * D_002516D0 .. D_00251B50 in order. Every block copies 0x80 bytes
 * (block_copy). 001D30A0 later rewrites qwords 5..7 of each half every
 * frame (the fog row and the guard rows); qwords 0..4 (the VIF codes and the
 * GIF tags VU1 dmem 1017..1020 receive) keep these templates.
 *
 * Checked by tools/test_object_unit_reference.py: in every AREA11 capture
 * the records' qwords 0..4 equal this copy of the user's ELF data.
 * Header-only; no dependency. */
#ifndef EM_SKIN_ARENA_INIT_H
#define EM_SKIN_ARENA_INIT_H

#include <stdint.h>
#include <string.h>

#define EM_SKIN_ARENA_RECORDS 0x00816440u   /* D_00816440: 14 x 0x100 bytes */
#define EM_SKIN_ARENA_SOURCE  0x002514D0u   /* D_002514D0: 14 x 0x80 bytes */
#define EM_SKIN_ARENA_COUNT 14u

/* Record k (at EM_SKIN_ARENA_RECORDS + 0x100 * k) receives source block
 * em_skin_arena_source[k] (at EM_SKIN_ARENA_SOURCE + 0x80 * that). */
static const uint8_t em_skin_arena_source[EM_SKIN_ARENA_COUNT] = {0, 3, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};

/* skin_arena_init(): `records` = host bytes of D_00816440 .. + 0xE00,
 * `source` = host bytes of D_002514D0 .. + 0x700. */
static inline void em_skin_arena_init_001D2E20(uint8_t *records, const uint8_t *source)
{
    for (unsigned half = 0; half < 2u; ++half)
        for (unsigned k = 0; k < EM_SKIN_ARENA_COUNT; ++k)
            memcpy(records + 0x100u * k + 0x80u * half, source + 0x80u * em_skin_arena_source[k], 0x80u);
}

#endif /* EM_SKIN_ARENA_INIT_H */
