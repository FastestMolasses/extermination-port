/* The EE sound library's bank-handle table and transfer commands. See
 * em_ee_sound_lib.h and docs/IOP_STREAM.md "The sound-bank transfer".
 * Each function says in words what the original's instructions do; the
 * original address of every store is the function's own. */
#include "game/em_ee_sound_lib.h"

#include <string.h>

#define SSHD_MAGIC 0x64685353u   /* "SShd" at the header's +0x0C */

static int fail(EmEeSoundLibFault *fault, uint32_t address, int32_t code)
{
    if (fault && fault->code == 0) {
        fault->address = address;
        fault->code = code;
    }
    return -1;
}

static int latched(const EmEeSoundLibFault *fault) { return fault && fault->code != 0; }

/* 00119400(a0, a1, a2, a3): D_0027F740 + 0x48 += 1, then (a tail call)
 * 001157F0(a0, count << 8 | (a1 >> 16 & 0xFF), a1 << 16 | (a2 >> 8 & 0xFFFF),
 * a2 << 24 | (a3 & 0xFFFFFF)), the count re-read after the store; shifts
 * are logical. Its value is 001157F0's, which no translated caller reads. */
int em_ee_sound_lib_00119400(EmEeSoundLib *lib, const EmEeSoundLibWorkers *w, int32_t a0,
                             int32_t a1, int32_t a2, int32_t a3, EmEeSoundLibFault *fault)
{
    if (latched(fault))
        return -1;
    if (!lib || !w || !w->w_001157F0)
        return fail(fault, 0x001157F0u, 1);
    lib->d27F788 = (int32_t)((uint32_t)lib->d27F788 + 1u);
    const uint32_t u1 = (uint32_t)a1, u2 = (uint32_t)a2, u3 = (uint32_t)a3;
    const uint32_t word1 = (uint32_t)lib->d27F788 << 8 | (u1 >> 16 & 0xFFu);
    const uint32_t word2 = u1 << 16 | (u2 >> 8 & 0xFFFFu);
    const uint32_t word3 = u2 << 24 | (u3 & 0xFFFFFFu);
    if (w->w_001157F0(w->ctx, a0, (int32_t)word1, (int32_t)word2, (int32_t)word3) < 0)
        return fail(fault, 0x001157F0u, 2);
    return 0;
}

/* 001193A8(a0, a1, a2): 00119400(0x20, a0, a1, a2), returns 0. */
int em_ee_sound_lib_001193A8(EmEeSoundLib *lib, const EmEeSoundLibWorkers *w, int32_t a0,
                             int32_t a1, int32_t a2, int32_t *ret, EmEeSoundLibFault *fault)
{
    if (em_ee_sound_lib_00119400(lib, w, 0x20, a0, a1, a2, fault) < 0)
        return -1;
    if (ret)
        *ret = 0;
    return 0;
}

/* 00119528(header, spu): -1 unless the word at header + 0x0C is "SShd";
 * else the first of entries 0..0x7E of D_0027C6C0 whose +0 is 0 becomes
 * {1, header, spu >> 3} (logical shift) and its index is returned; -1 when
 * all 127 are in use (entry 0x7F is never taken). */
int em_ee_sound_lib_00119528(EmEeSoundLib *lib, const EmEeSoundLibWorkers *w, uint32_t header,
                             uint32_t spu, int32_t *ret, EmEeSoundLibFault *fault)
{
    uint32_t magic;
    if (latched(fault))
        return -1;
    if (!lib || !ret || !w || !w->ee_word)
        return fail(fault, 0x00119528u, 1);
    if (w->ee_word(w->ctx, header + 0xCu, &magic) < 0)
        return fail(fault, header + 0xCu, 4);
    *ret = -1;
    if (magic != SSHD_MAGIC)
        return 0;
    for (int32_t i = 0; i < 0x7F; i++)
        if (lib->d27C6C0[i][0] == 0) {
            lib->d27C6C0[i][0] = 1;
            lib->d27C6C0[i][1] = header;
            lib->d27C6C0[i][2] = spu >> 3;
            *ret = i;
            break;
        }
    return 0;
}

/* 001194B8(iop, header, spu): r = 00119528(header, spu); when r is not -1,
 * 001193A8(iop, spu, the header's word +4) (the size to upload). Returns r. */
int em_ee_sound_lib_001194B8(EmEeSoundLib *lib, const EmEeSoundLibWorkers *w, int32_t iop,
                             uint32_t header, int32_t spu, int32_t *ret, EmEeSoundLibFault *fault)
{
    int32_t r, ignored;
    uint32_t size;
    if (!ret)
        return fail(fault, 0x001194B8u, 1);
    if (em_ee_sound_lib_00119528(lib, w, header, (uint32_t)spu, &r, fault) < 0)
        return -1;
    if (r != -1) {
        if (w->ee_word(w->ctx, header + 4u, &size) < 0)
            return fail(fault, header + 4u, 4);
        if (em_ee_sound_lib_001193A8(lib, w, iop, spu, (int32_t)size, &ignored, fault) < 0)
            return -1;
    }
    *ret = r;
    return 0;
}

/* 00119450(a0): a0 == 0: 1 when D_002817C0 + 0x1C0 equals D_0027F740 + 0x48,
 * else 0; a0 == 1 spins until they are equal (the blocking form; nothing
 * the port translates calls it, so it faults); any other a0: -1. */
int em_ee_sound_lib_00119450(EmEeSoundLib *lib, const EmEeSoundLibWorkers *w, int32_t a0,
                             int32_t *ret, EmEeSoundLibFault *fault)
{
    int32_t ack;
    if (latched(fault))
        return -1;
    if (!lib || !ret)
        return fail(fault, 0x00119450u, 1);
    if (a0 == 1)
        return fail(fault, 0x00119450u, 1);
    if (a0 != 0) {
        *ret = -1;
        return 0;
    }
    if (!w || !w->ack_2819_80)
        return fail(fault, 0x00119450u, 1);
    if (w->ack_2819_80(w->ctx, &ack) < 0)
        return fail(fault, 0x00119450u, 2);
    *ret = ack == lib->d27F788;
    return 0;
}

/* 001195A8(h): -1 unless h < 0x80 (unsigned) and D_0027C6C0[h].+0 == 1;
 * -1 when a D_0027CCC0 voice with +0x00 == 1 has +0x22 == h (it still
 * plays the bank); otherwise the 12-byte entry is cleared (00121A28) and it
 * returns 0. */
int em_ee_sound_lib_001195A8(EmEeSoundLib *lib, const EmEeSoundLibWorkers *w, uint32_t handle,
                             int32_t *ret, EmEeSoundLibFault *fault)
{
    if (latched(fault))
        return -1;
    if (!lib || !ret)
        return fail(fault, 0x001195A8u, 1);
    *ret = -1;
    if (handle >= EM_EE_SOUND_HANDLES || lib->d27C6C0[handle][0] != 1)
        return 0;
    if (!w || !w->voice)
        return fail(fault, 0x0027CCC0u, 1);
    for (int32_t v = 0; v < EM_EE_SOUND_VOICES; v++) {
        uint16_t state, owner;
        if (w->voice(w->ctx, v, &state, &owner) < 0)
            return fail(fault, 0x0027CCC0u + 0x6Au * (uint32_t)v, 2);
        if (state == 1 && owner == handle)
            return 0;
    }
    memset(lib->d27C6C0[handle], 0, sizeof lib->d27C6C0[handle]);
    *ret = 0;
    return 0;
}

/* 001199F0(h, v): -1 unless h < 0x80 (unsigned) and 0 <= v < 0x80; else
 * returns the signed byte D_002819C0[2h] and stores v there and 1 at
 * D_002819C0[2h + 1]. */
int em_ee_sound_lib_001199F0(EmEeSoundLib *lib, int32_t handle, int32_t volume, int32_t *ret,
                             EmEeSoundLibFault *fault)
{
    if (latched(fault))
        return -1;
    if (!lib || !ret)
        return fail(fault, 0x001199F0u, 1);
    *ret = -1;
    if ((uint32_t)handle >= EM_EE_SOUND_HANDLES || volume < 0 || volume >= 0x80)
        return 0;
    *ret = (int8_t)lib->d2819C0[2 * handle];
    lib->d2819C0[2 * handle] = (uint8_t)volume;
    lib->d2819C0[2 * handle + 1] = 1;
    return 0;
}
