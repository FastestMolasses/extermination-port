/* em_sdk_display_original.h - the boot ELF's SDK display-environment pair,
 * translated from the original instructions (docs/GS_EXACT.md section 11):
 *
 *   001002E0  build one display environment (5 dwords: PMODE, SMODE2,
 *             DISPFB, DISPLAY, BGCOLOR) for a psm / width / height and a
 *             picture offset x, y           -> em_sdk_001002E0
 *   00100550  put a display environment into the GS privileged registers
 *                                           -> em_sdk_00100550
 *   00100268  returns &D_00241010, the SDK's GS parameter halfwords
 *             (interlace, video mode, field/frame, GS revision): the caller
 *             passes those 8 bytes as `mode`.
 *
 * Read from the original instructions (001002E0 is a readable NEARMISS in
 * the decomp, its split listing gives the order and the 64-bit forms;
 * 00100550 is byte-matched C). Oracle: tools/test_display_env_reference.py
 * executes both originals from the user's pinned ELF and compares every
 * written byte and every GS register store.
 *
 * Fail-stop: a function returns 0, or -1 where the original would trap or
 * call a routine the port does not run, with the writes made before that
 * point kept in the original order:
 *   - 001002E0's integer division (width + 0x9FF) / width traps on a zero
 *     width (its divide-by-zero break); -1 after the first three dwords;
 *   - 001002E0 with a video mode other than 2 (NTSC) or 3 (PAL) calls the
 *     SDK's message printer 00122B58 (not run): -1 after the first three
 *     dwords, before the BGCOLOR dword;
 *   - a NULL environment, mode or store worker: -1 before any write. */
#ifndef EM_SDK_DISPLAY_ORIGINAL_H
#define EM_SDK_DISPLAY_ORIGINAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SDK_DISPENV_SIZE 0x28u        /* five dwords */
#define EM_SDK_GS_MODE_SIZE 8u           /* D_00241010: four halfwords */

/* The GS privileged registers 00100550 stores to. */
#define EM_SDK_GS_PMODE    UINT32_C(0x12000000)
#define EM_SDK_GS_SMODE2   UINT32_C(0x12000020)
#define EM_SDK_GS_DISPFB1  UINT32_C(0x12000070)
#define EM_SDK_GS_DISPLAY1 UINT32_C(0x12000080)
#define EM_SDK_GS_DISPFB2  UINT32_C(0x12000090)
#define EM_SDK_GS_DISPLAY2 UINT32_C(0x120000A0)
#define EM_SDK_GS_EXTDATA  UINT32_C(0x120000C0)
#define EM_SDK_GS_BGCOLOR  UINT32_C(0x120000E0)

/* 001002E0(env, psm, width, height, x, y): the five arguments are taken as
 * the original takes them (each sign-extended from its low halfword).
 * `mode` = the 8 bytes of D_00241010 (00100268's result). */
int em_sdk_001002E0(const uint8_t mode[EM_SDK_GS_MODE_SIZE], uint8_t env[EM_SDK_DISPENV_SIZE],
                    int32_t psm, int32_t width, int32_t height, int32_t x, int32_t y);

/* 00100550(env): the 64-bit stores to the GS privileged registers, in the
 * original order, through `store` (address, value; a negative return is a
 * fault and stops the routine). */
typedef int (*EmSdkGsStore)(void *ctx, uint32_t address, uint64_t value);
int em_sdk_00100550(const uint8_t mode[EM_SDK_GS_MODE_SIZE], const uint8_t env[EM_SDK_DISPENV_SIZE],
                    EmSdkGsStore store, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
