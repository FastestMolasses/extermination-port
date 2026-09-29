/* The EE sound library's bank-handle table and its transfer commands: the
 * part of the SDK sound library (the lowmem code around 00119400) that the
 * sound-bank upload 001FB3E0 calls. Docs: docs/IOP_STREAM.md "The
 * sound-bank transfer".
 *
 *   00119400  queue a transfer command through 001157F0, counting it in
 *             D_0027F740 + 0x48
 *   001193A8  00119400(0x20, a0, a1, a2): the IOP-to-SPU upload command
 *   00119528  claim the first free bank handle of D_0027C6C0 for an SShd
 *             header
 *   001194B8  00119528, then 001193A8 of the header's size
 *   00119450  the upload acknowledgement: D_002817C0 + 0x1C0 (the IOP's
 *             count, delivered by the per-field exchange) against the count
 *   001195A8  release a bank handle no D_0027CCC0 voice plays
 *   001199F0  a handle's pending bank volume in D_002819C0
 *
 * Hand translations of the original instructions (the decomp's C for
 * 00119450 / 001194B8 / 00119528 / 001195A8 / 001193A8 is byte-matched ee-gcc,
 * 00119400 / 001199F0 mwcc), checked by tools/test_sound_bank_reference.py,
 * which executes the originals over the captured title RAM and compares
 * every byte they write and every command they queue.
 *
 * Fail-stop: a NULL or failing worker latches a fault and the call returns
 * -1; so does 00119450(1), the blocking form (a spin on the IOP's count that
 * no translated caller reaches). No dependency beyond the workers. */
#ifndef EM_EE_SOUND_LIB_H
#define EM_EE_SOUND_LIB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_EE_SOUND_HANDLES 0x80   /* D_0027C6C0: 0x600 bytes of 12-byte entries */
#define EM_EE_SOUND_VOICES 48      /* D_0027CCC0: 48 records of 0x6A bytes */

typedef struct {
    /* D_0027C6C0[h] = {+0 in use, +4 the SShd header's EE address, +8 the
     * SPU address >> 3} */
    uint32_t d27C6C0[EM_EE_SOUND_HANDLES][3];
    /* D_002819C0[2h] = the handle's bank volume, [2h + 1] = 1 while a new
     * one waits for the sequencer (00116DB8 takes it) */
    uint8_t d2819C0[2 * EM_EE_SOUND_HANDLES];
    int32_t d27F788;   /* D_0027F740 + 0x48: the transfer commands queued */
} EmEeSoundLib;

typedef struct {
    uint32_t address; /* original function address */
    int32_t code;     /* 1 NULL worker, 2 worker failed, 4 bad index */
} EmEeSoundLibFault;

typedef struct {
    void *ctx;
    /* 001157F0(cmd, a1, a2, a3): queue one command for the next exchange
     * (its result is ignored by 00119400, as in the original). */
    int (*w_001157F0)(void *ctx, int32_t cmd, int32_t a1, int32_t a2, int32_t a3);
    /* D_002817C0 + 0x1C0 as the last exchange delivered it. */
    int (*ack_2819_80)(void *ctx, int32_t *value);
    /* One EE word at `address` (the SShd header 00119528 / 001194B8 read). */
    int (*ee_word)(void *ctx, uint32_t address, uint32_t *value);
    /* D_0027CCC0[voice]: its +0x00 and +0x22 halfwords (001195A8's scan). */
    int (*voice)(void *ctx, int32_t voice, uint16_t *state, uint16_t *handle);
} EmEeSoundLibWorkers;

/* Each returns 0 with the original's return value in *ret (where it has
 * one), or -1 with *fault set. */
int em_ee_sound_lib_00119400(EmEeSoundLib *lib, const EmEeSoundLibWorkers *w, int32_t a0,
                             int32_t a1, int32_t a2, int32_t a3, EmEeSoundLibFault *fault);
int em_ee_sound_lib_001193A8(EmEeSoundLib *lib, const EmEeSoundLibWorkers *w, int32_t a0,
                             int32_t a1, int32_t a2, int32_t *ret, EmEeSoundLibFault *fault);
int em_ee_sound_lib_00119528(EmEeSoundLib *lib, const EmEeSoundLibWorkers *w, uint32_t header,
                             uint32_t spu, int32_t *ret, EmEeSoundLibFault *fault);
int em_ee_sound_lib_001194B8(EmEeSoundLib *lib, const EmEeSoundLibWorkers *w, int32_t iop,
                             uint32_t header, int32_t spu, int32_t *ret, EmEeSoundLibFault *fault);
int em_ee_sound_lib_00119450(EmEeSoundLib *lib, const EmEeSoundLibWorkers *w, int32_t a0,
                             int32_t *ret, EmEeSoundLibFault *fault);
int em_ee_sound_lib_001195A8(EmEeSoundLib *lib, const EmEeSoundLibWorkers *w, uint32_t handle,
                             int32_t *ret, EmEeSoundLibFault *fault);
int em_ee_sound_lib_001199F0(EmEeSoundLib *lib, int32_t handle, int32_t volume, int32_t *ret,
                             EmEeSoundLibFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_EE_SOUND_LIB_H */
