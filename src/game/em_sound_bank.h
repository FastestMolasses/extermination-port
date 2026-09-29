/* The sound-bank upload 001FB370 bound over the EE sound library and the
 * IOP backend (docs/IOP_STREAM.md "The sound-bank transfer",
 * docs/MODULE_LOADER.md section 1.9).
 *
 * 001FB370 / 001FB3E0 / 001FB910 are the verified translations of
 * em_startup_load_gaps_sound.c; this module gives them their workers:
 *
 *   00119450, 001194B8, 001195A8, 001199F0  em_ee_sound_lib over this
 *              module's D_0027C6C0 / D_002819C0 / D_0027F740 + 0x48, the
 *              IOP backend's queue 001157F0 and its status copy
 *              D_002817C0 + 0x1C0;
 *   0010F8F8 / 0010F968  the IOP heap RPCs (em_iop_stream's heap model);
 *   0010BC00 (sceSifSetDChain), 0010BAA0 (FlushCache): no host effect
 *              (the EE caches and the SIF0 chain are not modelled);
 *   0010BBE0 / 0010BBC0 (sceSifSetDma / sceSifDmaStat): em_iop_stream's SIF
 *              DMA at host speed, from the bank file's bytes.
 *
 * The upload's pace is the code's: 001FB3E0 moves one state per call, and
 * its acknowledgement waits for the IOP's count, which the per-field
 * exchange (em_iop_stream_field) delivers the field after the driver ran
 * command 0x20.
 *
 * The state the boot's and the title's bank uploads leave (the port runs
 * neither: 001AB7E0 step 1's common bank and the title module's bank) is
 * seeded from the title capture (slot 01): EM_SOUND_BANK_SEEDS in the .c,
 * checked against that capture by tools/test_sound_bank_reference.py.
 *
 * One instance per game (em_stream_live owns it next to its IOP backend).
 * Fail-stop: any fault latches and every later call returns -1. */
#ifndef EM_SOUND_BANK_H
#define EM_SOUND_BANK_H

#include <stdint.h>

#include "game/em_ee_sound_lib.h"
#include "game/em_iop_stream.h"
#include "game/em_startup_load_gaps.h"

#ifdef __cplusplus
extern "C" {
#endif

/* D_0027CCC0[voice] as 001195A8 reads it: the voice's +0x00 and +0x22. */
typedef int (*EmSoundBankVoice)(void *ctx, int32_t voice, uint16_t *state, uint16_t *handle);

typedef struct {
    EmSlgBankLoad load;         /* D_00282150 .. D_002821A7, D_00281D30 / D_00281D50,
                                 * D_00275B18 / D_00275B1C, D_00264890 */
    EmEeSoundLib lib;           /* D_0027C6C0, D_002819C0, D_0027F740 + 0x48 */
    EmIopStream *iop;
    EmSoundBankVoice voice;
    void *voice_ctx;
    const EmSlgBankFile *file;  /* the bank the running call reads (its EE bytes) */
    EmEeSoundLibFault lib_fault;
    uint32_t fault_address;
    int32_t fault_code;
    uint32_t calls, uploads;    /* 001FB370 calls; command 0x20s queued */
} EmSoundBank;

/* Binds the instance to its IOP backend and seeds the state the boot's
 * and title's uploads leave (EM_SOUND_BANK_SEEDS; also the backend's
 * transfer count). `base` is D_00264890 (5 words from the user's ELF:
 * tools/export_module_loader.py). 0, or -1. */
int em_sound_bank_init(EmSoundBank *bank, EmIopStream *iop, const int32_t base[5]);
/* The D_0027CCC0 view 001195A8 scans (NULL: a reached scan faults). */
void em_sound_bank_set_voice(EmSoundBank *bank, EmSoundBankVoice voice, void *ctx);

/* One 001FB370 call on the bank file at EE `address` (`bytes`, `size`: the
 * file as the loader delivered it). *result = 0 while working, else the
 * 0x40-aligned EE address past the bank's resident part. 0, or -1. */
int em_sound_bank_001FB370(EmSoundBank *bank, uint32_t address, const uint8_t *bytes,
                           uint32_t size, uint32_t *result);

/* 1 with the fault's original address and code once one latched, else 0. */
int em_sound_bank_failed(const EmSoundBank *bank, uint32_t *address, int32_t *code);

#ifdef __cplusplus
}
#endif

#endif /* EM_SOUND_BANK_H */
