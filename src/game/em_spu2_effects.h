#ifndef EM_SPU2_EFFECTS_H
#define EM_SPU2_EFFECTS_H

#include "em_spu2_reverb.h"
#include <stddef.h>
#include <stdint.h>

enum { EM_SPU2_EFFECT_MODES = 10, EM_SPU2_EFFECT_FILE_BYTES = 2576 };

typedef struct {
    uint32_t start, end; /* inclusive normalized signed-16 sample positions */
    EmSpu2ReverbParams params;
} EmSpu2Effect;

typedef struct {
    uint32_t initial_mode[2];
    uint8_t driver_sha256[32], capture_sha256[32];
    EmSpu2Effect core[2][EM_SPU2_EFFECT_MODES];
} EmSpu2Effects;

/*
 * EMRV1 is an ignored local asset from tools/export_spu2_effects.py:
 *   0: "EMRV"; 4: version=1; 8: header bytes=96; 12: record bytes=124;
 *   16: count=20; 20/24: initial core modes; 28: payload CRC32;
 *   32: SHA256 of SNDN2DRV.IRX; 64: SHA256 of captured original IOP RAM.
 * Twenty records follow, each four LE u32s (core, mode, start, end), then
 * 22 LE i32 addresses and ten LE i16 coefficients in Params field order.
 * All address values are normalized sample positions, not MMIO values.
 *
 * The loader pins the original SCUS-97112 driver digest, checks schema,
 * duplicates, complete coverage, address bounds, and payload CRC. It changes
 * *out only on success. The capture digest identifies the local source;
 * it does not by itself prove a particular game state or sound output.
 */
int em_spu2_effects_read(EmSpu2Effects *out, const void *data, size_t size);
int em_spu2_effects_load(EmSpu2Effects *out, const char *path);

/* Apply one exported preset and inclusive effect area to an existing RAM
 * binding. The caller owns write-enable, EVOL, routing, RAM clearing, and
 * command timing; this function changes none of those. */
int em_spu2_effects_apply(const EmSpu2Effects *effects, unsigned core,
                          unsigned mode, EmSpu2Reverb *reverb);

#endif
