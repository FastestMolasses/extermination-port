/* The sound-bank upload 001FB370 bound over the EE sound library and the
 * IOP backend. See em_sound_bank.h and docs/IOP_STREAM.md "The sound-bank
 * transfer". */
#include "game/em_sound_bank.h"

#include <string.h>

/* The state the boot's and the title's bank uploads leave, which the port
 * does not run (001AB7E0 step 1's common bank: handles 0..2 in bucket 1;
 * the title module's bank: handle 3 in bucket 3). Every value is the title
 * capture's (startup-reference save state slot 01, taken at the title
 * before New Game) and holds until the New Game area load: route capture
 * 00 shows the same entries plus the area bank's. They are addresses and
 * counters, not disc data. tools/test_sound_bank_reference.py checks each
 * against the capture. */
static const struct {
    uint32_t handles[4][3];      /* D_0027C6C0[0..3]: {in use, SShd header, SPU >> 3} */
    int32_t counts[8];           /* D_00281D30 */
    int32_t bucket1[3];          /* D_00281D50 bucket 1 (words 20..22) */
    int32_t bucket3;             /* D_00281D50 bucket 3 (word 60) */
    uint8_t volume[6];           /* D_002819C0[0..5] */
    int32_t count;               /* D_0027F740 + 0x48 = D_002817C0 + 0x1C0 */
    int8_t d282150, d282151, d282159, d28215C;
    int32_t d282190, d282194, d282198;
    uint32_t d28219C;
    int32_t d2821A0;
    uint32_t d2821A4;
    int32_t d275B18, d275B1C;
} EM_SOUND_BANK_SEEDS = {
    {{1u, 0x00B2C860u, 0x2A08u}, {1u, 0x00B2F7D0u, 0xC8A0u}, {1u, 0x00B30CB0u, 0x1AA40u},
     {1u, 0x01800030u, 0x24400u}},
    {0, 3, 0, 1, 0, 0, 0, 0},
    {0, 1, 2},
    3,
    {0x64, 1, 0x64, 1, 0x64, 1},
    5,
    0, 0, 1, 1,
    3, 0xDE800, 0x12C770,
    0x0180AD00u,
    0x530D1702,
    0x01800030u,
    0x4D, 0,
};

static int bank_fail(EmSoundBank *b, uint32_t address, int32_t code)
{
    if (b->fault_code == 0) {
        b->fault_address = address;
        b->fault_code = code;
    }
    return -1;
}

static int lib_failed(EmSoundBank *b)
{
    return bank_fail(b, b->lib_fault.address, b->lib_fault.code);
}

/* ---- the library's workers ---- */

static int lw_001157F0(void *ctx, int32_t cmd, int32_t a1, int32_t a2, int32_t a3)
{
    EmSoundBank *b = ctx;
    (void)em_iop_stream_001157F0(b->iop, cmd, a1, a2, a3); /* a full queue drops it */
    if (em_iop_stream_fault(b->iop)->code)
        return -1;
    if (cmd == 0x20)
        b->uploads++;
    return 0;
}

static int lw_ack(void *ctx, int32_t *value)
{
    EmSoundBank *b = ctx;
    *value = em_iop_stream_ee_status(b->iop)[112];
    return 0;
}

static const uint8_t *file_at(const EmSoundBank *b, uint32_t address, uint32_t size)
{
    const EmSlgBankFile *f = b->file;
    if (!f || !f->bytes || address < f->address || address - f->address > f->size ||
        size > f->size - (address - f->address))
        return NULL;
    return f->bytes + (address - f->address);
}

static int lw_word(void *ctx, uint32_t address, uint32_t *value)
{
    const uint8_t *p = file_at(ctx, address, 4);
    if (!p)
        return -1;
    *value = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    return 0;
}

static int lw_voice(void *ctx, int32_t voice, uint16_t *state, uint16_t *handle)
{
    EmSoundBank *b = ctx;
    if (!b->voice)
        return -1;
    return b->voice(b->voice_ctx, voice, state, handle);
}

static EmEeSoundLibWorkers lib_workers(EmSoundBank *b)
{
    return (EmEeSoundLibWorkers){b, lw_001157F0, lw_ack, lw_word, lw_voice};
}

/* ---- 001FB3E0's workers ---- */

static int w_001195A8(void *ctx, int32_t handle)
{
    EmSoundBank *b = ctx;
    EmEeSoundLibWorkers w = lib_workers(b);
    int32_t ret;
    return em_ee_sound_lib_001195A8(&b->lib, &w, (uint32_t)handle, &ret, &b->lib_fault) < 0
               ? lib_failed(b) : 0;
}

static int w_0010F8F8(void *ctx, int32_t size, int32_t *ret)
{
    EmSoundBank *b = ctx;
    uint32_t block;
    if (em_iop_stream_0010F8F8(b->iop, size, &block) < 0)
        return bank_fail(b, 0x0010F8F8u, 2);
    *ret = (int32_t)block;
    return 0;
}

static int w_00119450(void *ctx, int32_t a0, int32_t *ret)
{
    EmSoundBank *b = ctx;
    EmEeSoundLibWorkers w = lib_workers(b);
    return em_ee_sound_lib_00119450(&b->lib, &w, a0, ret, &b->lib_fault) < 0 ? lib_failed(b) : 0;
}

static int w_001194B8(void *ctx, int32_t a0, uint32_t a1, int32_t a2, int32_t *ret)
{
    EmSoundBank *b = ctx;
    EmEeSoundLibWorkers w = lib_workers(b);
    return em_ee_sound_lib_001194B8(&b->lib, &w, a0, a1, a2, ret, &b->lib_fault) < 0
               ? lib_failed(b) : 0;
}

static int w_001199F0(void *ctx, int32_t handle, int32_t a1)
{
    EmSoundBank *b = ctx;
    int32_t ret;
    return em_ee_sound_lib_001199F0(&b->lib, handle, a1, &ret, &b->lib_fault) < 0 ? lib_failed(b)
                                                                                   : 0;
}

static int w_0010F968(void *ctx, int32_t block)
{
    EmSoundBank *b = ctx;
    return em_iop_stream_0010F968(b->iop, (uint32_t)block) < 0 ? bank_fail(b, 0x0010F968u, 2) : 0;
}

/* sceSifSetDChain (syscall 0x78) and FlushCache(0) (syscall 0x64): the SIF0
 * receive chain and the EE data cache are not host state. */
static int w_0010BC00(void *ctx)
{
    (void)ctx;
    return 0;
}

static int w_0010BAA0(void *ctx, int32_t a0)
{
    (void)ctx;
    (void)a0;
    return 0;
}

static int w_0010BBE0(void *ctx, const uint32_t desc[4], int32_t count, int32_t *ret)
{
    EmSoundBank *b = ctx;
    const uint8_t *src;
    if (count != 1 || desc[3] != 0 || !(src = file_at(b, desc[0], desc[2])))
        return bank_fail(b, 0x0010BBE0u, 4);
    return em_iop_stream_sif_set_dma(b->iop, src, desc[1], desc[2], ret) < 0
               ? bank_fail(b, 0x0010BBE0u, 2) : 0;
}

static int w_0010BBC0(void *ctx, int32_t id, int32_t *ret)
{
    EmSoundBank *b = ctx;
    return em_iop_stream_sif_dma_stat(b->iop, id, ret) < 0 ? bank_fail(b, 0x0010BBC0u, 2) : 0;
}

/* ---- the binding ---- */

int em_sound_bank_init(EmSoundBank *b, EmIopStream *iop, const int32_t base[5])
{
    if (!b || !iop || !base)
        return -1;
    memset(b, 0, sizeof *b);
    b->iop = iop;
    EmSlgBankLoad *s = &b->load;
    s->d282150 = EM_SOUND_BANK_SEEDS.d282150;
    s->d282151 = EM_SOUND_BANK_SEEDS.d282151;
    s->d282159 = EM_SOUND_BANK_SEEDS.d282159;
    s->d28215C = EM_SOUND_BANK_SEEDS.d28215C;
    s->d282190 = EM_SOUND_BANK_SEEDS.d282190;
    s->d282194 = EM_SOUND_BANK_SEEDS.d282194;
    s->d282198 = EM_SOUND_BANK_SEEDS.d282198;
    s->d28219C = EM_SOUND_BANK_SEEDS.d28219C;
    s->d2821A0 = EM_SOUND_BANK_SEEDS.d2821A0;
    s->d2821A4 = EM_SOUND_BANK_SEEDS.d2821A4;
    s->d275B18 = EM_SOUND_BANK_SEEDS.d275B18;
    s->d275B1C = EM_SOUND_BANK_SEEDS.d275B1C;
    memcpy(s->d281D30, EM_SOUND_BANK_SEEDS.counts, sizeof s->d281D30);
    memcpy(&s->d281D50[20], EM_SOUND_BANK_SEEDS.bucket1, sizeof EM_SOUND_BANK_SEEDS.bucket1);
    s->d281D50[60] = EM_SOUND_BANK_SEEDS.bucket3;
    memcpy(s->base, base, sizeof s->base);
    memcpy(b->lib.d27C6C0, EM_SOUND_BANK_SEEDS.handles, sizeof EM_SOUND_BANK_SEEDS.handles);
    memcpy(b->lib.d2819C0, EM_SOUND_BANK_SEEDS.volume, sizeof EM_SOUND_BANK_SEEDS.volume);
    b->lib.d27F788 = EM_SOUND_BANK_SEEDS.count;
    em_iop_stream_seed_transfer_count(iop, (uint32_t)EM_SOUND_BANK_SEEDS.count);
    return 0;
}

void em_sound_bank_set_voice(EmSoundBank *b, EmSoundBankVoice voice, void *ctx)
{
    if (!b)
        return;
    b->voice = voice;
    b->voice_ctx = ctx;
}

int em_sound_bank_001FB370(EmSoundBank *b, uint32_t address, const uint8_t *bytes, uint32_t size,
                           uint32_t *result)
{
    if (!b || !result)
        return -1;
    if (b->fault_code)
        return -1;
    if (!b->iop || !bytes)
        return bank_fail(b, 0x001FB370u, 1);
    const EmSlgBankFile file = {address, bytes, size};
    const EmSlgBankWorkers w = {b,          w_001195A8, w_0010F8F8, w_00119450,
                                w_001194B8, w_001199F0, w_0010F968, w_0010BC00,
                                w_0010BAA0, w_0010BBE0, w_0010BBC0};
    b->file = &file;
    b->calls++;
    int rc = em_slg_001FB370(&w, &b->load, &file, result);
    b->file = NULL;
    if (rc < 0)
        return bank_fail(b, 0x001FB370u, 2);
    return 0;
}

int em_sound_bank_failed(const EmSoundBank *b, uint32_t *address, int32_t *code)
{
    if (!b || !b->fault_code)
        return 0;
    if (address)
        *address = b->fault_address;
    if (code)
        *code = b->fault_code;
    return 1;
}
