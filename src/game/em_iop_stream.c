/* IOP stream backend (see em_iop_stream.h and docs/IOP_STREAM.md).
 *
 * Section 1 translates original EE functions; section 2 translates the
 * stream part of the IOP sound driver SNDN2DRV.IRX (addresses are the
 * module's .text / .bss offsets); sections 3..5 are the stated hardware,
 * drive and timing models. Comments describe what the original does in
 * words; no instruction stream is reproduced. */
#include "game/em_iop_stream.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- timing model (docs/IOP_STREAM.md "Timing") --------------------------
 * Time is counted in NTSC half-lines. A field is 262.5 H-lines = 525
 * half-lines. The driver's hard timer (command 0x1E: compare 0x3C00 / 0xF0 =
 * 64 on an H-line source, repeat) wakes its thread every 64 H-lines = 128
 * half-lines. The SPU2 renders 48000 samples per second against
 * 4500000 / 286 H-lines per second: 1144/375 samples per H-line, i.e.
 * 572/375 per half-line (800.8 per field). */
#define HALF_LINES_PER_FIELD 525u
#define HALF_LINES_PER_TICK 128u
#define SAMPLES_NUM 572u
#define SAMPLES_DEN 375u

/* Captured: all 27 images hold D_00275B50 = 0x85B00 with D_00275B28 /
 * 24 / 20 = 0xADC00 / 0xBDD00 / 0xCDE00, i.e. four consecutive IOP heap
 * blocks of the 0x100-rounded request sizes. The first free block is an IOP
 * kernel fact (what the six modules left free), not derivable here. */
#define IOP_HEAP_FIRST 0x85B00u
#define IOP_HEAP_UNIT 0x100u

struct EmIopStream {
    uint8_t iop_ram[EM_IOP_RAM_SIZE];
    uint8_t spu_ram[EM_SPU_RAM_SIZE];
    EmIopDriver drv;
    EmIopLibsd sd;
    EmIopStreamForward forward;
    void *forward_ctx;
    EmIopStreamTap tap;
    void *tap_ctx;
    EmIopSpuVoice spu[EM_IOP_VOICES];
    uint16_t evol[2][2];
    uint8_t trans_busy;
    uint64_t trans_tick;
    /* time */
    uint64_t half_lines, ticks, samples;
    /* EE side */
    uint32_t ee_queue[EM_EE_QUEUE][4];
    uint32_t ee_count;
    int32_t ee_status[EM_IOP_STATUS_WORDS];
    uint32_t heap_next;
    EmIopVoiceTable voice_table;
    /* disc */
    const EmIopStreamDisc *disc;
    uint32_t disc_latency;
    struct { uint8_t busy; uint32_t polls, sector, count, addr; } read;
    /* mixer ring */
    float pcm[EM_IOP_PCM_FRAMES * 2];
    _Atomic uint64_t pcm_write, pcm_read;
    _Atomic uint64_t overruns, underruns, bad_rate;
    uint64_t digest;
    EmIopStreamFault fault;
};

/* ------------------------------------------------------------------------ */

static int fault(EmIopStream *s, uint32_t address, int32_t code)
{
    if (s->fault.code == EM_IOP_FAULT_NONE) {
        s->fault.address = address;
        s->fault.code = code;
    }
    return -1;
}

static int latched(const EmIopStream *s) { return s->fault.code != EM_IOP_FAULT_NONE; }

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* libsd voice register: ((voice % 24) << 1) | (voice / 24), the core in bit 0. */
static uint16_t voice_reg(int32_t v) { return (uint16_t)(((v % 24) << 1) | (v / 24)); }

/* ======================================================================== *
 * 3. SPU2 voice model (hardware; stated semantics, docs/IOP_STREAM.md)      *
 * ======================================================================== */

static const int32_t ADPCM_COEF[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

/* Decode the 16-byte block at p->nax. The rules are the port's existing
 * decoder's (tools/audio_export.py decode_adpcm, used for every exported
 * clip): filter > 4 decodes as 0, shift > 12 as 12, prediction >> 6
 * without rounding, clamp to s16. A loop-start flag (bit 2) sets LSA. */
static void spu_load_block(EmIopStream *s, EmIopSpuVoice *p)
{
    const uint8_t *b = s->spu_ram + (p->nax & (EM_SPU_RAM_SIZE - 16));
    int shift = b[0] & 0xF, filter = b[0] >> 4, i;
    if (filter > 4)
        filter = 0;
    if (shift > 12)
        shift = 12;
    p->flags = b[1];
    if (p->flags & 4)
        p->lsa = p->nax;
    for (i = 0; i < 28; i++) {
        int nib = (b[2 + (i >> 1)] >> ((i & 1) * 4)) & 0xF;
        int32_t x;
        if (nib > 7)
            nib -= 16;
        x = nib * (1 << (12 - shift)) +
            ((p->s1 * ADPCM_COEF[filter][0] + p->s2 * ADPCM_COEF[filter][1]) >> 6);
        if (x < -32768)
            x = -32768;
        if (x > 32767)
            x = 32767;
        p->s2 = p->s1;
        p->s1 = x;
        p->block[i] = (int16_t)x;
    }
}

static void spu_key_on(EmIopStream *s, EmIopSpuVoice *p)
{
    p->nax = p->lsa = p->ssa & ~15u;
    p->pos = 0;
    p->counter = 0;
    p->s1 = p->s2 = 0;
    em_sfx_envelope_key_on(&p->env, p->adsr1, p->adsr2);
    p->on = 1;
    spu_load_block(s, p);
}

/* End of a block: loop-end (bit 0) jumps to LSA, and without repeat (bit 1)
 * the voice stops with ENVX 0; otherwise the next 16 bytes. */
static void spu_block_end(EmIopStream *s, EmIopSpuVoice *p)
{
    if (p->flags & 1) {
        p->nax = p->lsa;
        if (!(p->flags & 2)) {
            p->env.level = 0;
            p->env.phase = EM_SFX_ENV_OFF;
            p->on = 0;
            return;
        }
    } else {
        p->nax = (p->nax + 16) & (EM_SPU_RAM_SIZE - 1);
    }
    p->pos = 0;
    spu_load_block(s, p);
}

/* One 48 kHz output sample of every keyed voice (no interpolation: the
 * streams run at pitch 0x1000; SPU2 Gaussian interpolation is not modelled,
 * as for the SFX voices). */
static void spu_sample(EmIopStream *s, float *l, float *r)
{
    int v;
    *l = *r = 0.0f;
    for (v = 0; v < EM_IOP_VOICES; v++) {
        EmIopSpuVoice *p = &s->spu[v];
        uint32_t step;
        float x;
        if (!p->on)
            continue;
        if (p->env.phase == EM_SFX_ENV_OFF) {
            p->on = 0;
            continue;
        }
        if (s->tap)
            s->tap(s->tap_ctx, v, p->block[p->pos]);
        x = (float)p->block[p->pos] / 32768.0f * ((float)p->env.level / 32768.0f);
        *l += x * em_sfx_volume_gain(p->vol[0]);
        *r += x * em_sfx_volume_gain(p->vol[1]);
        em_sfx_envelope_step(&p->env);
        step = p->pitch > 0x3FFF ? 0x3FFFu : p->pitch;
        p->counter += step;
        while (p->counter >= 0x1000u && p->on) {
            p->counter -= 0x1000u;
            if (++p->pos == 28)
                spu_block_end(s, p);
        }
    }
}

/* The ring the audio thread drains; every frame also feeds the digest. */
static void pcm_push(EmIopStream *s, float l, float r)
{
    uint64_t w = atomic_load_explicit(&s->pcm_write, memory_order_relaxed);
    uint64_t rd = atomic_load_explicit(&s->pcm_read, memory_order_acquire);
    uint32_t bits[2];
    int i, k;
    memcpy(&bits[0], &l, 4);
    memcpy(&bits[1], &r, 4);
    for (k = 0; k < 2; k++)
        for (i = 0; i < 4; i++) {
            s->digest ^= (bits[k] >> (8 * i)) & 0xFFu;
            s->digest *= 0x100000001B3ull;
        }
    if (w - rd >= EM_IOP_PCM_FRAMES) {
        atomic_fetch_add_explicit(&s->overruns, 1, memory_order_relaxed);
        return;
    }
    s->pcm[(w % EM_IOP_PCM_FRAMES) * 2] = l;
    s->pcm[(w % EM_IOP_PCM_FRAMES) * 2 + 1] = r;
    atomic_store_explicit(&s->pcm_write, w + 1, memory_order_release);
}

static void spu_render_to(EmIopStream *s, uint64_t half_line)
{
    const uint64_t target = half_line * SAMPLES_NUM / SAMPLES_DEN;
    while (s->samples < target) {
        float l, r;
        spu_sample(s, &l, &r);
        pcm_push(s, l, r);
        s->samples++;
    }
}

/* ---- the libsd calls on the model ---------------------------------------- */

static EmIopSpuVoice *reg_voice(EmIopStream *s, uint16_t reg)
{
    int core = reg & 1, v = (reg >> 1) & 0x1F;
    if (v >= 24) {
        fault(s, EM_IOP_DRV(0x314C), EM_IOP_FAULT_BAD_INDEX);
        return NULL;
    }
    return &s->spu[core * 24 + v];
}

static void sd_set_param(void *ctx, uint16_t reg, uint16_t value)
{
    EmIopStream *s = ctx;
    EmIopSpuVoice *p;
    if (reg & 0x80) {                     /* core parameters */
        if ((reg & 0xFF7E) == 0x0B00)
            s->evol[reg & 1][0] = value;  /* EVOLL */
        else if ((reg & 0xFF7E) == 0x0C00)
            s->evol[reg & 1][1] = value;  /* EVOLR */
        else
            fault(s, EM_IOP_DRV(0x314C), EM_IOP_FAULT_UNSUPPORTED);
        return;
    }
    if (!(p = reg_voice(s, reg)))
        return;
    switch (reg & 0xFF00) {
    case 0x0000: p->vol[0] = value; break;
    case 0x0100: p->vol[1] = value; break;
    case 0x0200: p->pitch = value; break;
    case 0x0300: p->adsr1 = value; break;
    case 0x0400: p->adsr2 = value; break;
    default: fault(s, EM_IOP_DRV(0x314C), EM_IOP_FAULT_UNSUPPORTED); break;
    }
}

static uint16_t sd_get_param(void *ctx, uint16_t reg)
{
    EmIopStream *s = ctx;
    EmIopSpuVoice *p;
    if ((reg & 0xFF80) != 0x0500 || !(p = reg_voice(s, reg))) {
        fault(s, EM_IOP_DRV(0x3154), EM_IOP_FAULT_UNSUPPORTED);
        return 0;
    }
    return (uint16_t)(p->env.level & 0xFFFF);  /* ENVX */
}

static void sd_set_switch(void *ctx, uint16_t reg, uint32_t value)
{
    EmIopStream *s = ctx;
    int core = reg & 1, v;
    if ((reg & 0xFFFE) != 0x1500 && (reg & 0xFFFE) != 0x1600) {
        fault(s, EM_IOP_DRV(0x315C), EM_IOP_FAULT_UNSUPPORTED);
        return;
    }
    for (v = 0; v < 24; v++) {
        EmIopSpuVoice *p = &s->spu[core * 24 + v];
        if (!(value & (1u << v)))
            continue;
        if ((reg & 0xFFFE) == 0x1500)
            spu_key_on(s, p);
        else if (p->on)
            em_sfx_envelope_key_off(&p->env);
    }
}

static void sd_set_addr(void *ctx, uint16_t reg, uint32_t value)
{
    EmIopStream *s = ctx;
    EmIopSpuVoice *p;
    if ((reg & 0xFFC0) != 0x2040 || !(p = reg_voice(s, reg))) {
        fault(s, EM_IOP_DRV(0x3164), EM_IOP_FAULT_UNSUPPORTED);
        return;
    }
    p->ssa = value;
}

/* NAX: the block being played, plus the data halfword of the current sample
 * (captured NAX values are block + 2..0xE). */
static uint32_t sd_get_addr(void *ctx, uint16_t reg)
{
    EmIopStream *s = ctx;
    EmIopSpuVoice *p;
    if ((reg & 0xFFC0) != 0x2240 || !(p = reg_voice(s, reg))) {
        fault(s, EM_IOP_DRV(0x316C), EM_IOP_FAULT_UNSUPPORTED);
        return 0;
    }
    return p->nax + 2u + 2u * (uint32_t)(p->pos >> 2);
}

/* The transfer lands in SPU RAM when issued and counts as complete from the
 * next driver tick on (a 0x2000-byte SPU2 DMA is far shorter than a tick). */
static void sd_voice_trans(void *ctx, int16_t channel, uint16_t mode, const uint8_t *data,
                           uint32_t spu_addr, uint32_t size)
{
    EmIopStream *s = ctx;
    if (channel != 1 || mode != 0 || spu_addr > EM_SPU_RAM_SIZE || size > EM_SPU_RAM_SIZE - spu_addr) {
        fault(s, EM_IOP_DRV(0x317C), EM_IOP_FAULT_BAD_INDEX);
        return;
    }
    memcpy(s->spu_ram + spu_addr, data, size);
    s->trans_busy = 1;
    s->trans_tick = s->ticks;
}

static int32_t sd_voice_trans_status(void *ctx, int16_t channel, int16_t flag)
{
    EmIopStream *s = ctx;
    (void)channel;
    (void)flag;
    if (s->trans_busy && s->ticks > s->trans_tick)
        s->trans_busy = 0;
    return s->trans_busy ? 0 : 1;
}

static void default_libsd(EmIopStream *s)
{
    s->sd.ctx = s;
    s->sd.set_param = sd_set_param;
    s->sd.get_param = sd_get_param;
    s->sd.set_switch = sd_set_switch;
    s->sd.set_addr = sd_set_addr;
    s->sd.get_addr = sd_get_addr;
    s->sd.voice_trans = sd_voice_trans;
    s->sd.voice_trans_status = sd_voice_trans_status;
}

/* ======================================================================== *
 * lifetime                                                                  *
 * ======================================================================== */

EmIopStream *em_iop_stream_create(void)
{
    EmIopStream *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    default_libsd(s);
    s->heap_next = IOP_HEAP_FIRST;
    s->digest = 0xCBF29CE484222325ull;
    atomic_init(&s->pcm_write, 0);
    atomic_init(&s->pcm_read, 0);
    atomic_init(&s->overruns, 0);
    atomic_init(&s->underruns, 0);
    atomic_init(&s->bad_rate, 0);
    return s;
}

void em_iop_stream_destroy(EmIopStream *s) { free(s); }

const EmIopStreamFault *em_iop_stream_fault(const EmIopStream *s) { return &s->fault; }

void em_iop_stream_set_libsd(EmIopStream *s, const EmIopLibsd *sd)
{
    if (sd)
        s->sd = *sd;
    else
        default_libsd(s);
}

void em_iop_stream_set_forward(EmIopStream *s, EmIopStreamForward fn, void *ctx)
{
    s->forward = fn;
    s->forward_ctx = ctx;
}

EmIopDriver *em_iop_stream_driver(EmIopStream *s) { return &s->drv; }
uint8_t *em_iop_stream_iop_ram(EmIopStream *s) { return s->iop_ram; }
uint8_t *em_iop_stream_spu_ram(EmIopStream *s) { return s->spu_ram; }

EmIopSpuVoice *em_iop_stream_spu_voice(EmIopStream *s, int voice)
{
    return voice >= 0 && voice < EM_IOP_VOICES ? &s->spu[voice] : NULL;
}

uint16_t em_iop_stream_effect_volume(const EmIopStream *s, int core, int side)
{
    return s->evol[core & 1][side & 1];
}

void em_iop_stream_clock(const EmIopStream *s, uint64_t *half_lines, uint64_t *ticks, uint64_t *samples)
{
    if (half_lines)
        *half_lines = s->half_lines;
    if (ticks)
        *ticks = s->ticks;
    if (samples)
        *samples = s->samples;
}

uint64_t em_iop_stream_pcm_digest(const EmIopStream *s) { return s->digest; }

void em_iop_stream_set_tap(EmIopStream *s, EmIopStreamTap fn, void *ctx)
{
    s->tap = fn;
    s->tap_ctx = ctx;
}

/* ======================================================================== *
 * the exported disc (EMST, tools/export_streams.py)                         *
 * ======================================================================== */

#define EMST_HEADER 0xA8u

int em_iop_stream_disc_load(EmIopStreamDisc *disc, const char *path)
{
    FILE *f;
    long size;
    uint8_t *b;
    uint32_t rows, i, table;
    memset(disc, 0, sizeof *disc);
    if (!(f = fopen(path, "rb"))) {
        fprintf(stderr, "iop_stream: cannot open %s (run tools/export_streams.py)\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < (long)EMST_HEADER || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        fprintf(stderr, "iop_stream: %s is not an EMST file\n", path);
        return -1;
    }
    if (!(b = malloc((size_t)size)) || fread(b, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(b);
        fprintf(stderr, "iop_stream: cannot read %s\n", path);
        return -1;
    }
    fclose(f);
    if (memcmp(b, "EMST", 4) || rd32(b + 4) != 1 || rd32(b + 0x18) != 68 || rd32(b + 0x1C) != 179 ||
        rd32(b + 0x24) != 0)
        goto bad;
    disc->blob = b;
    disc->size = (size_t)size;
    for (i = 0; i < 2; i++) {
        disc->lsn[i] = rd32(b + 0x08 + 4 * i);
        disc->sectors[i] = rd32(b + 0x10 + 4 * i);
        memcpy(disc->search[i], b + 0x28 + 32 * i, 32);
        memcpy(disc->path[i], b + 0x68 + 32 * i, 32);
        disc->search[i][31] = disc->path[i][31] = 0;
    }
    disc->music_rows = 68;
    disc->voice_rows = 179;
    rows = 68 + 179;
    disc->extents = rd32(b + 0x20);
    table = EMST_HEADER + rows * 16u;
    if ((size_t)table + (size_t)disc->extents * 16u > (size_t)size)
        goto bad;
    for (i = 0; i < rows; i++) {
        disc->rows[i].sector = (int32_t)rd32(b + EMST_HEADER + 16 * i);
        disc->rows[i].unread = (int32_t)rd32(b + EMST_HEADER + 16 * i + 4);
        disc->rows[i].size = (int32_t)rd32(b + EMST_HEADER + 16 * i + 8);
        disc->rows[i].loop = (int32_t)rd32(b + EMST_HEADER + 16 * i + 12);
    }
    disc->extent = (const EmIopStreamExtent *)(const void *)(b + table);
    for (i = 0; i < disc->extents; i++) {
        const EmIopStreamExtent *e = &disc->extent[i];
        if (e->file > 1 || e->lsn < disc->lsn[e->file] ||
            e->lsn - disc->lsn[e->file] + (uint64_t)e->sectors > disc->sectors[e->file] ||
            e->offset > (size_t)size || (uint64_t)e->sectors * 2048u > (size_t)size - e->offset)
            goto bad;
    }
    return 0;
bad:
    fprintf(stderr, "iop_stream: %s is not a valid EMST 1 file\n", path);
    free(b);
    memset(disc, 0, sizeof *disc);
    return -1;
}

void em_iop_stream_disc_free(EmIopStreamDisc *disc)
{
    free(disc->blob);
    memset(disc, 0, sizeof *disc);
}

void em_iop_stream_lanes_data(const EmIopStreamDisc *disc, EmStreamLanesData *out)
{
    out->music = disc->rows;
    out->music_count = disc->music_rows;
    out->voice = disc->rows + disc->music_rows;
    out->voice_count = disc->voice_rows;
}

void em_iop_stream_attach_disc(EmIopStream *s, const EmIopStreamDisc *disc) { s->disc = disc; }
void em_iop_stream_set_disc_latency(EmIopStream *s, uint32_t polls) { s->disc_latency = polls; }

/* The exported bytes of one sector, or NULL. */
static const uint8_t *disc_sector(const EmIopStreamDisc *d, uint32_t lsn)
{
    uint32_t i;
    for (i = 0; i < d->extents; i++) {
        const EmIopStreamExtent *e = &d->extent[i];
        if (lsn >= e->lsn && lsn - e->lsn < e->sectors)
            return d->blob + e->offset + (size_t)(lsn - e->lsn) * 2048u;
    }
    return NULL;
}

/* ======================================================================== *
 * 1. EE side                                                                *
 * ======================================================================== */

/* 0010F8F8 through the IOP heap model: the next free block, request rounded
 * up to the 0x100 unit the captured blocks show. */
static uint32_t iop_heap_alloc(EmIopStream *s, int32_t size)
{
    uint32_t block = s->heap_next;
    uint32_t bytes = ((uint32_t)size + IOP_HEAP_UNIT - 1) & ~(IOP_HEAP_UNIT - 1);
    if (size <= 0 || bytes > EM_IOP_RAM_SIZE - block) {
        fault(s, 0x0010F8F8u, EM_IOP_FAULT_BAD_INDEX);
        return 0;
    }
    s->heap_next = block + bytes;
    return block;
}

/* 001FA6A0(size): 0010F8F8(size + 0x10), rounded up to 16 when it is not a
 * multiple of 16. */
uint32_t em_iop_stream_001FA6A0(EmIopStream *s, int32_t size)
{
    uint32_t v;
    if (latched(s))
        return 0;
    v = iop_heap_alloc(s, size + 0x10);
    if (v & 0xFu)
        v = (v & ~0xFu) + 0x10u;
    return v;
}

/* sub_cdrom0_IRX_SNDN2DRV_IRX_1 ends with five 001FA6A0 calls: 0x28000 into
 * D_00275B50, then 0x10000 into D_00275B28 (also stored to D_00275B4C),
 * D_00275B24 and D_00275B20. */
int em_iop_stream_boot_buffers(EmIopStream *s, uint32_t out[5])
{
    if (latched(s))
        return -1;
    out[0] = em_iop_stream_001FA6A0(s, 0x28000);
    out[1] = em_iop_stream_001FA6A0(s, 0x10000);
    out[2] = out[1];
    out[3] = em_iop_stream_001FA6A0(s, 0x10000);
    out[4] = em_iop_stream_001FA6A0(s, 0x10000);
    return latched(s) ? -1 : 0;
}

/* 00111C28(fp, name) over the exported directory: the start sector of the
 * entry whose disc path equals name. */
static int search_file(const EmIopStream *s, const char *name, uint32_t *lsn)
{
    int i;
    for (i = 0; i < 2; i++)
        if (!strcmp(s->disc->path[i], name)) {
            *lsn = s->disc->lsn[i];
            return 1;
        }
    return 0;
}

/* sub_O_STREAM_MUSIC_DAT_1: 00113280(0), then 00111C28 on D_0026EBB0 (with
 * 00113280(0) before every retry) -> D_00282188 = the file's start sector;
 * the same for D_0026EBD0 -> D_0028218C. The original retries forever; a
 * name the export does not hold faults here instead. */
int em_iop_stream_sub_O_STREAM_MUSIC_DAT_1(EmIopStream *s, uint32_t *d282188, uint32_t *d28218C)
{
    uint32_t lsn;
    if (latched(s))
        return -1;
    if (!s->disc)
        return fault(s, 0x001FA6E0u, EM_IOP_FAULT_NULL_WORKER);
    if (!search_file(s, s->disc->search[0], &lsn))
        return fault(s, 0x00111C28u, EM_IOP_FAULT_NOT_EXPORTED);
    *d282188 = lsn;
    if (!search_file(s, s->disc->search[1], &lsn))
        return fault(s, 0x00111C28u, EM_IOP_FAULT_NOT_EXPORTED);
    *d28218C = lsn;
    return 0;
}

void em_iop_stream_set_voice_table(EmIopStream *s, const EmIopVoiceTable *table)
{
    if (table)
        s->voice_table = *table;
    else
        memset(&s->voice_table, 0, sizeof s->voice_table);
}

int em_iop_stream_001157F0(EmIopStream *s, int32_t cmd, int32_t a1, int32_t a2, int32_t a3)
{
    uint32_t *e;
    if (latched(s))
        return -1;
    if (s->ee_count >= EM_EE_QUEUE)
        return -1;
    e = s->ee_queue[s->ee_count++];
    e[0] = (uint32_t)cmd;
    e[1] = (uint32_t)a1;
    e[2] = (uint32_t)a2;
    e[3] = (uint32_t)a3;
    return (int)s->ee_count;
}

/* 0011A2B0(a0), from its .s. a0 1 scans voices 0..23, 0 scans 0..47, 2 scans
 * 24..47; anything else returns -1. The first record with +0x00 == 0 and
 * +0x1A != 3 is claimed (+0x00 = 1, +0x1A = 3). Otherwise the lowest +0x0A
 * (unsigned, first wins) among records with +0x1A != 3 is taken over: the
 * original then sends 001157F0(3, i, 0, 0) with i = the scan's end index (the
 * loop counter left in a1; the NEARMISS C passes 0), ORs into D_0027F740+0x28
 * the scan mask shifted once per visited record (1 << (end - start), not the
 * chosen voice's bit; the C agrees), clears the record and sets +0x06,
 * +0x22, +0x24, +0x26 = 0xFFFF, +0x4E = 0x78, +0x00 = 1, +0x1A = 3. */
int em_iop_stream_0011A2B0(EmIopStream *s, int32_t a0, int32_t *voice)
{
    uint8_t *t = s->voice_table.d27CCC0, *e;
    int32_t start, limit, i, best = -1;
    uint32_t best_value = 0xFFFFFFFFu;
    uint64_t mask = 1;
    if (latched(s))
        return -1;
    if (!t || !s->voice_table.d27F768)
        return fault(s, 0x0011A2B0u, EM_IOP_FAULT_NULL_WORKER);
    if (a0 == 1) {
        start = 0;
        limit = 0x18;
    } else if (a0 == 0) {
        start = 0;
        limit = 0x30;
    } else if (a0 == 2) {
        start = 0x18;
        limit = 0x30;
    } else {
        *voice = -1;
        return 0;
    }
    for (i = start; i < limit; i++, mask <<= 1) {
        e = t + i * 0x6A;
        if (rd16(e) == 0 && rd16(e + 0x1A) != 3) {
            wr16(e, 1);
            wr16(e + 0x1A, 3);
            *voice = i;
            return 0;
        }
        if (rd16(e + 0x0A) < best_value && rd16(e + 0x1A) != 3) {
            best_value = rd16(e + 0x0A);
            best = i;
        }
    }
    if (best == -1) {
        *voice = -1;
        return 0;
    }
    em_iop_stream_001157F0(s, 3, i, 0, 0);          /* queue-full result ignored */
    *s->voice_table.d27F768 |= mask;
    e = t + best * 0x6A;
    memset(e, 0, 0x6A);                            /* 00121A28 */
    wr16(e + 0x06, 0xFFFF);
    wr16(e + 0x26, 0xFFFF);
    wr16(e + 0x24, 0xFFFF);
    wr16(e + 0x22, 0xFFFF);
    wr16(e + 0x4E, 0x78);
    wr16(e + 0x00, 1);
    wr16(e + 0x1A, 3);
    *voice = best;
    return 0;
}

const int32_t *em_iop_stream_ee_status(const EmIopStream *s) { return s->ee_status; }

const uint32_t (*em_iop_stream_ee_queue(const EmIopStream *s, uint32_t *count))[4]
{
    *count = s->ee_count;
    return (const uint32_t (*)[4])s->ee_queue;
}

uint32_t em_iop_stream_heap_next(const EmIopStream *s) { return s->heap_next; }
void em_iop_stream_set_heap_next(EmIopStream *s, uint32_t next) { s->heap_next = next; }

/* ---- 4. the sector reader (libcdvd contract as the lanes use it) -------- */

/* A read the EE left in flight (001FABB0 resets D_00282157 without the
 * 00113478 break) is not lost: the drive finishes it on its own and its
 * sectors land in IOP RAM. With no latency left (the default drive model)
 * it has finished by the drive's next query; a read that still has polls
 * to wait for is outside the model (the caller faults). */
static void read_land(EmIopStream *s)
{
    uint32_t i;
    for (i = 0; i < s->read.count; i++)
        memcpy(s->iop_ram + s->read.addr + 2048u * i, disc_sector(s->disc, s->read.sector + i), 2048);
    s->read.busy = 0;
}

/* 00113280(mode): 2 = the drive is ready (the value 001FA0D0 waits for). */
int em_iop_stream_00113280(EmIopStream *s, int32_t mode, int32_t *result)
{
    (void)mode;
    if (latched(s))
        return -1;
    if (s->read.busy) {
        if (s->read.polls)
            return fault(s, 0x00113280u, EM_IOP_FAULT_UNSUPPORTED);
        read_land(s);
    }
    *result = 2;
    return 0;
}

/* 00112610(sector, count, IOP address, mode): accept a read of whole sectors
 * into IOP RAM (the lane buffers are IOP heap blocks). Every sector must be
 * exported; the data land when 00112D18 reports completion. */
int em_iop_stream_00112610(EmIopStream *s, uint32_t sector, uint32_t count, uint32_t addr,
                           const uint8_t mode[3], int32_t *result)
{
    uint32_t i;
    (void)mode;
    if (latched(s))
        return -1;
    if (!s->disc)
        return fault(s, 0x00112610u, EM_IOP_FAULT_NULL_WORKER);
    if (s->read.busy)
        return fault(s, 0x00112610u, EM_IOP_FAULT_UNSUPPORTED);
    if (addr > EM_IOP_RAM_SIZE || (uint64_t)count * 2048u > EM_IOP_RAM_SIZE - addr)
        return fault(s, 0x00112610u, EM_IOP_FAULT_BAD_INDEX);
    for (i = 0; i < count; i++)
        if (!disc_sector(s->disc, sector + i))
            return fault(s, 0x00112610u, EM_IOP_FAULT_NOT_EXPORTED);
    s->read.busy = 1;
    s->read.polls = s->disc_latency;
    s->read.sector = sector;
    s->read.count = count;
    s->read.addr = addr;
    *result = 1;
    return 0;
}

/* 00112D18(1): 1 while the read is in flight, 0 once done (or idle). */
int em_iop_stream_00112D18(EmIopStream *s, int32_t mode, int32_t *result)
{
    (void)mode;
    if (latched(s))
        return -1;
    if (s->read.busy && s->read.polls) {
        s->read.polls--;
        *result = 1;
        return 0;
    }
    if (s->read.busy)
        read_land(s);
    *result = 0;
    return 0;
}

/* 00113478(1): the read is abandoned (its data never land). */
int em_iop_stream_00113478(EmIopStream *s, int32_t mode)
{
    (void)mode;
    if (latched(s))
        return -1;
    s->read.busy = 0;
    return 0;
}

/* ---- the lanes' worker adapters (ctx: first member is the stream) -------- */

static EmIopStream *lane_ctx(void *ctx) { return *(EmIopStream **)ctx; }

static int lw_001157F0(void *ctx, int32_t cmd, int32_t a1, int32_t a2, int32_t a3)
{
    EmIopStream *s = lane_ctx(ctx);
    int r = em_iop_stream_001157F0(s, cmd, a1, a2, a3);
    return latched(s) ? -1 : (r < 0 ? 0 : r);
}
static int lw_00113280(void *ctx, int32_t a0, int32_t *r) { return em_iop_stream_00113280(lane_ctx(ctx), a0, r); }
static int lw_00112610(void *ctx, uint32_t sector, uint32_t count, uint32_t addr, const uint8_t mode[3],
                       int32_t *r)
{
    return em_iop_stream_00112610(lane_ctx(ctx), sector, count, addr, mode, r);
}
static int lw_00112D18(void *ctx, int32_t a0, int32_t *r) { return em_iop_stream_00112D18(lane_ctx(ctx), a0, r); }
static int lw_00113478(void *ctx, int32_t a0) { return em_iop_stream_00113478(lane_ctx(ctx), a0); }
static int lw_0011A2B0(void *ctx, int32_t a0, int32_t *v) { return em_iop_stream_0011A2B0(lane_ctx(ctx), a0, v); }

void em_iop_stream_lane_workers(EmIopStream *s, EmStreamLanesWorkers *w)
{
    (void)s;
    w->w_001157F0 = lw_001157F0;
    w->w_00113280 = lw_00113280;
    w->w_00112610 = lw_00112610;
    w->w_00112D18 = lw_00112D18;
    w->w_00113478 = lw_00113478;
    w->w_0011A2B0 = lw_0011A2B0;
}

/* ======================================================================== *
 * 2. SNDN2DRV.IRX stream subset                                             *
 * ======================================================================== */

/* 0x2CF4: push {a0..a3} unless 0x60 entries are waiting. A full queue drops
 * the entry (the module returns -1 there, which no caller reads); this
 * function's -1 is reserved for faults. */
int em_iop_stream_drv_push(EmIopStream *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    EmIopDriver *d = &s->drv;
    uint32_t *e;
    if (latched(s))
        return -1;
    if (d->queue_write - d->queue_read >= EM_IOP_QUEUE)
        return 0;
    if (d->queue_write < 0)
        return fault(s, EM_IOP_DRV(0x2CF4), EM_IOP_FAULT_BAD_INDEX);
    e = d->queue[d->queue_write % EM_IOP_QUEUE];
    d->queue_write++;
    e[0] = a0;
    e[1] = a1;
    e[2] = a2;
    e[3] = a3;
    return 0;
}

static EmIopStreamVoice *drv_voice(EmIopStream *s, uint32_t fn, int64_t v)
{
    if (v < 0 || v >= EM_IOP_VOICES) {
        fault(s, EM_IOP_DRV(fn), EM_IOP_FAULT_BAD_INDEX);
        return NULL;
    }
    return &s->drv.voice[v];
}

/* 0x634 for the stream commands. Commands outside the subset go to the
 * forward sink (the SFX driver's commands) at this same point. */
int em_iop_stream_drv_command(EmIopStream *s, const uint32_t c[4])
{
    EmIopDriver *d = &s->drv;
    EmIopStreamVoice *p;
    int32_t core, v, vv;
    uint32_t mask, bit;
    if (latched(s))
        return -1;
    switch (c[0]) {
    case 0x16:  /* effect return volume: EVOLL / EVOLR of core c[1] */
        s->sd.set_param(s->sd.ctx, (uint16_t)((c[1] & 0xFFFF) | 0xB80), (uint16_t)c[2]);
        s->sd.set_param(s->sd.ctx, (uint16_t)((c[1] & 0xFFFF) | 0xC80), (uint16_t)c[3]);
        break;
    case 0x3C:  /* clear .bss 0x8680 (0x200), the voice table (0x9C0), the queue (0x600) */
        memset(d->records, 0, sizeof d->records);
        memset(d->voice, 0, sizeof d->voice);
        memset(d->queue, 0, sizeof d->queue);
        break;
    case 0x3D:
        break;
    case 0x3E: {  /* one voice's stream configuration (0011A4E8's packing) */
        uint32_t spu, size, iop;
        v = (int32_t)(c[1] >> 24);
        if (!(p = drv_voice(s, 0x1720, v)))
            return -1;
        spu = (c[1] & 0xFF) << 16 | c[2] >> 16;
        size = (c[2] & 0xFFFF) << 8 | c[3] >> 24;
        iop = c[3] & 0xFFFFFF;
        p->stride = c[1] & 0xFF0000;
        p->iop_addr = iop;
        p->iop_size = size;
        p->spu_addr = spu;
        p->spu_size = c[1] & 0xFF00;
        s->sd.set_addr(s->sd.ctx, (uint16_t)(voice_reg(v) | 0x2040), spu);   /* SSA */
        s->sd.set_param(s->sd.ctx, (uint16_t)(voice_reg(v) | 0x300), 0x8080); /* ADSR1 */
        s->sd.set_param(s->sd.ctx, (uint16_t)(voice_reg(v) | 0x400), 0x808A); /* ADSR2 */
        break;
    }
    case 0x3F:
        if (!(p = drv_voice(s, 0x1A08, (int32_t)c[1])))
            return -1;
        memset(p, 0, sizeof *p);
        break;
    case 0x40:  /* volume words: VOLL = c[3] >> 16, VOLR = c[3] & 0xFFFF */
    case 0x41:  /* rate (Hz): PITCH = (rate << 12) / 48000 */
    case 0x42:  /* queue a first-half transfer per voice, then the key-on entry */
        for (core = 0; core < 2; core++) {
            mask = core ? c[2] : c[1];
            for (vv = 0, bit = 1; vv < 24; vv++, bit <<= 1) {
                if (!(mask & bit))
                    continue;
                v = core * 24 + vv;
                p = &d->voice[v];
                if (c[0] == 0x40) {
                    p->vol_left = (c[3] >> 16) & 0xFFFF;
                    p->vol_right = c[3] & 0xFFFF;
                    s->sd.set_param(s->sd.ctx, (uint16_t)(vv << 1 | core), (uint16_t)p->vol_left);
                    s->sd.set_param(s->sd.ctx, (uint16_t)((vv << 1 | core) | 0x100), (uint16_t)p->vol_right);
                } else if (c[0] == 0x41) {
                    /* signed division by 48000 as the module computes it */
                    int32_t x = (int32_t)(c[3] << 12);
                    int32_t hi = (int32_t)(((int64_t)x * 0x057619F1) >> 32);
                    int32_t q = (hi >> 10) - (x >> 31);
                    p->rate = c[3];
                    s->sd.set_param(s->sd.ctx, (uint16_t)((vv << 1 | core) | 0x200), (uint16_t)q);
                } else {
                    em_iop_stream_drv_push(s, (uint32_t)v << 26 | (p->stride & 0xFF0000) | 0x2000000u | 0x1000u,
                                           p->iop_addr, p->spu_addr, (uint32_t)((int32_t)p->spu_size / 2));
                }
                if (latched(s))
                    return -1;
            }
        }
        if (c[0] == 0x42)
            em_iop_stream_drv_push(s, 0x1010, c[1], c[2], 0);
        break;
    case 0x43:  /* the key-off entry */
        em_iop_stream_drv_push(s, 0x1011, c[1], c[2], 0);
        break;
    default:
        if (!s->forward)
            return fault(s, EM_IOP_DRV(0x634), EM_IOP_FAULT_UNSUPPORTED);
        s->forward(s->forward_ctx, c);
        break;
    }
    return latched(s) ? -1 : 0;
}

/* 0x12C with function 0x64: append the received bytes to the 0x1000-byte
 * command ring at the write count (split at the ring end); the reply is the
 * status block. */
int em_iop_stream_drv_rpc(EmIopStream *s, const uint32_t *words, uint32_t bytes)
{
    EmIopDriver *d = &s->drv;
    uint8_t *ring = (uint8_t *)d->ring;
    int32_t size = (int32_t)bytes, off, room;
    if (latched(s))
        return -1;
    if (size > 0) {
        if (size > 0x1000)
            return fault(s, EM_IOP_DRV(0x12C), EM_IOP_FAULT_BAD_INDEX);
        off = (int32_t)(d->ring_write & 0xFFF);
        room = 0x1000 - off;
        if (room < size) {
            memcpy(ring + off, words, (size_t)room);
            memcpy(ring, (const uint8_t *)words + (room / 4) * 4, (size_t)(size - room));
        } else {
            memcpy(ring + off, words, (size_t)size);
        }
        d->ring_write += (uint32_t)size;
    }
    return 0;
}

/* 0x328: run every ring command from the read count to the write count,
 * then snapshot the status block: per voice the cursor word and ENVX & 0x7FFF,
 * then the +0xC words of the sixteen .bss 0x8680 records. */
int em_iop_stream_drv_tick_commands(EmIopStream *s)
{
    EmIopDriver *d = &s->drv;
    uint32_t p;
    int v;
    if (latched(s))
        return -1;
    for (p = d->ring_read; p < d->ring_write; p += 16)
        if (em_iop_stream_drv_command(s, d->ring[(p & 0xFFF) / 16]))
            return -1;
    d->ring_read = d->ring_write;
    for (v = 0; v < EM_IOP_VOICES; v++) {
        d->status[48 + v] = d->voice[v].cursor;
        d->status[v] = (uint32_t)(s->sd.get_param(s->sd.ctx, (uint16_t)(voice_reg(v) | 0x500)) & 0x7FFF);
    }
    for (v = 0; v < 16; v++)
        d->status[96 + v] = d->records[(0xC + 32 * v) / 4];
    return latched(s) ? -1 : 0;
}

/* 0x20C8: voices 47 down to 0 with active bit 0: read NAX; NAX inside
 * (start, start + half] marks the first SPU half (target the second), past
 * start + half marks the second (target the first). A changed half queues a
 * transfer of the next half-size chunk at the cursor into the target. */
int em_iop_stream_drv_scan(EmIopStream *s)
{
    int32_t v;
    if (latched(s))
        return -1;
    for (v = EM_IOP_VOICES - 1; v >= 0; v--) {
        EmIopStreamVoice *p = &s->drv.voice[v];
        int32_t half, target;
        if (!(p->active & 1))
            continue;
        p->nax = s->sd.get_addr(s->sd.ctx, (uint16_t)(voice_reg(v) | 0x2240));
        half = (int32_t)p->spu_size / 2;
        target = (int32_t)p->spu_addr;
        if ((int32_t)p->spu_addr < (int32_t)p->nax &&
            !((int32_t)p->spu_addr + half < (int32_t)p->nax)) {
            p->half = 0x1000000;
            target += half;
        }
        if ((int32_t)p->spu_addr + half < (int32_t)p->nax)
            p->half = 0x2000000;
        if (p->sent != p->half) {
            em_iop_stream_drv_push(s, (uint32_t)v << 26 | p->half | (p->stride & 0xFF0000) | 0x1000u,
                                   p->iop_addr + p->cursor, (uint32_t)target, (uint32_t)half);
            p->sent = p->half;
        }
        if (latched(s))
            return -1;
    }
    return 0;
}

/* 0x23B8: only while channel 1's transfer status is 1. First the finished
 * transfer's voice advances its cursor by half * (stride >> 16) modulo the
 * IOP size; then one queue entry runs: 0x1000 a transfer (copy the voice's
 * chunks into the staging buffer, 0x800 >> stride-bytes from every 0x800,
 * set the loop flags of its first and last block, transfer to SPU RAM, mark
 * pending), 0x1010 key-on (KON switches; active |= 1, sent = half = 0), 0x1011
 * key-off (active = cursor = sent = half = 0; KOFF switches). The entry is
 * then cleared and the read count advances. */
int em_iop_stream_drv_consume(EmIopStream *s)
{
    EmIopDriver *d = &s->drv;
    uint32_t *e;
    int32_t v;
    if (latched(s))
        return -1;
    if (s->sd.voice_trans_status(s->sd.ctx, 1, 0) != 1)
        return 0;
    if (d->pending != 0) {
        EmIopStreamVoice *p;
        int32_t value, size;
        d->pending &= 0xFF;
        if (!(p = drv_voice(s, 0x2400, (int32_t)d->pending)))
            return -1;
        value = (int32_t)(p->cursor + (uint32_t)(((int32_t)p->spu_size / 2) * ((int32_t)p->stride >> 16)));
        size = (int32_t)p->iop_size;
        if (size == 0 || (size == -1 && value == INT32_MIN))
            return fault(s, EM_IOP_DRV(0x24C4), EM_IOP_FAULT_DIVIDE);
        p->cursor = (uint32_t)(value % size);
        d->pending = 0;
    }
    if (!(d->queue_read < d->queue_write))
        return 0;
    if (d->queue_read < 0)
        return fault(s, EM_IOP_DRV(0x2514), EM_IOP_FAULT_BAD_INDEX);
    e = d->queue[d->queue_read % EM_IOP_QUEUE];
    switch (e[0] & 0xFFFF) {
    case 0x1000: {
        int32_t stride = (int32_t)((e[0] & 0xFF0000) >> 16), chunk, count, i, size = (int32_t)e[3];
        uint32_t src = e[1];
        if (stride == 0 || (chunk = 0x800 / stride) == 0)
            return fault(s, EM_IOP_DRV(0x25FC), EM_IOP_FAULT_DIVIDE);
        count = size / chunk;
        if (size < 0x10 || size > EM_IOP_STAGING || (int64_t)count * chunk > EM_IOP_STAGING)
            return fault(s, EM_IOP_DRV(0x2698), EM_IOP_FAULT_BAD_INDEX);
        for (i = 0; i < count; i++, src += 0x800) {
            if (src > EM_IOP_RAM_SIZE || (uint32_t)chunk > EM_IOP_RAM_SIZE - src)
                return fault(s, EM_IOP_DRV(0x26F4), EM_IOP_FAULT_BAD_INDEX);
            memcpy(d->staging + i * chunk, s->iop_ram + src, (size_t)chunk);
        }
        if ((e[0] & 0x3000000) == 0x1000000) {
            d->staging[1] = 2;
            d->staging[1 + size - 0x10] = 3;
        } else if ((e[0] & 0x3000000) == 0x2000000) {
            d->staging[1] = 6;
            d->staging[1 + size - 0x10] = 2;
        } else {
            break;
        }
        s->sd.voice_trans(s->sd.ctx, 1, 0, d->staging, e[2], e[3]);
        d->pending = (uint32_t)((int32_t)e[0] >> 26) | 0x10000000u;
        break;
    }
    case 0x1010:
        s->sd.set_switch(s->sd.ctx, 0x1500, e[1]);
        s->sd.set_switch(s->sd.ctx, 0x1501, e[2]);
        for (v = 0; v < EM_IOP_VOICES; v++)
            if (e[1 + v / 24] & (1u << (v % 24))) {
                d->voice[v].active |= 1;
                d->voice[v].sent = 0;
                d->voice[v].half = 0;
            }
        break;
    case 0x1011:
        for (v = 0; v < EM_IOP_VOICES; v++)
            if (e[1 + v / 24] & (1u << (v % 24))) {
                d->voice[v].active = 0;
                d->voice[v].cursor = 0;
                d->voice[v].sent = 0;
                d->voice[v].half = 0;
            }
        s->sd.set_switch(s->sd.ctx, 0x1600, e[1]);
        s->sd.set_switch(s->sd.ctx, 0x1601, e[2]);
        break;
    default:
        break;
    }
    if (latched(s))
        return -1;
    memset(e, 0, 16);
    d->queue_read++;
    return 0;
}

/* ======================================================================== *
 * 5. time                                                                   *
 * ======================================================================== */

/* One driver thread wake-up: 0x328, 0x20C8, 0x23B8 in that order. */
static int driver_tick(EmIopStream *s)
{
    s->ticks++;
    if (em_iop_stream_drv_tick_commands(s) || em_iop_stream_drv_scan(s))
        return -1;
    return em_iop_stream_drv_consume(s);
}

int em_iop_stream_field(EmIopStream *s)
{
    uint64_t end, h;
    if (latched(s))
        return -1;
    /* 001152D8's per-field RPC 0x64: the queued commands go into the driver's
     * ring; the reply (0x200 bytes into D_002817C0) is the driver's status
     * block as its last tick wrote it. */
    if (em_iop_stream_drv_rpc(s, &s->ee_queue[0][0], s->ee_count * 16u))
        return -1;
    s->ee_count = 0;
    memcpy(s->ee_status, s->drv.status, sizeof s->ee_status);
    end = s->half_lines + HALF_LINES_PER_FIELD;
    for (h = (s->half_lines / HALF_LINES_PER_TICK + 1) * HALF_LINES_PER_TICK; h <= end;
         h += HALF_LINES_PER_TICK) {
        spu_render_to(s, h);
        if (driver_tick(s))
            return -1;
    }
    spu_render_to(s, end);
    s->half_lines = end;
    return latched(s) ? -1 : 0;
}

/* ======================================================================== *
 * mixer (audio thread)                                                      *
 * ======================================================================== */

void em_iop_stream_mix(EmIopStream *s, float *out, int frames, int device_rate)
{
    uint64_t r, w, n, i;
    if (!s || frames <= 0)
        return;
    if (device_rate != 48000) {
        atomic_fetch_add_explicit(&s->bad_rate, 1, memory_order_relaxed);
        return;
    }
    r = atomic_load_explicit(&s->pcm_read, memory_order_relaxed);
    w = atomic_load_explicit(&s->pcm_write, memory_order_acquire);
    n = w - r < (uint64_t)frames ? w - r : (uint64_t)frames;
    for (i = 0; i < n; i++) {
        out[2 * i] += s->pcm[((r + i) % EM_IOP_PCM_FRAMES) * 2];
        out[2 * i + 1] += s->pcm[((r + i) % EM_IOP_PCM_FRAMES) * 2 + 1];
    }
    atomic_store_explicit(&s->pcm_read, r + n, memory_order_release);
    if (n < (uint64_t)frames)
        atomic_fetch_add_explicit(&s->underruns, (uint64_t)frames - n, memory_order_relaxed);
}

void em_iop_stream_mix_counters(const EmIopStream *s, uint64_t *overruns, uint64_t *underruns,
                                uint64_t *bad_rate)
{
    EmIopStream *m = (EmIopStream *)s;
    if (overruns)
        *overruns = atomic_load_explicit(&m->overruns, memory_order_relaxed);
    if (underruns)
        *underruns = atomic_load_explicit(&m->underruns, memory_order_relaxed);
    if (bad_rate)
        *bad_rate = atomic_load_explicit(&m->bad_rate, memory_order_relaxed);
}
