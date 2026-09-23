/* Original EE stream lanes (see em_stream_lanes_original.h and
 * docs/STREAM_LANES.md). Every function below is the original of the same
 * address; comments cite original addresses and offsets. Float constants
 * are the original bit patterns and every COP1 operation follows the EE
 * rules of docs/EE_FLOAT_MODEL.md (bit-exact, on raw binary32 words). */
#include "game/em_stream_lanes_original.h"

#include "game/em_ee_float.h"

#include <string.h>

#define F_16383 0x467FFC00u    /* 16383.0: fade numerator, 001FABF0 fade-0 step */
#define F_M16383 0xC67FFC00u   /* -16383.0: 001FAD70 fade-0 step */
#define F_TICKS 0x3D98EAD6u    /* 0.074666664 (001FA790) */
#define F_60 0x42700000u       /* 60.0 */
#define F_30 0x41F00000u       /* 30.0 */
#define F_2 0x40000000u        /* 2.0 (lane 0 only) */

/* D_0025DD30 music rows before D_0025E170 (the voice table). */
#define EM_STREAM_MUSIC_ROWS 68u

/* ------------------------------------------------------------------------
 * EE COP1 (docs/EE_FLOAT_MODEL.md section 2): every add.s / sub.s / mul.s /
 * div.s / cvt.s.w / neg.s / c.eq.s / c.lt.s / c.le.s below is the shared
 * integer-only model in game/em_ee_float.h (section 6), on raw binary32
 * words. No host float operation is performed in this module. */

/* Exponent field, for the integer soft-float leaves below (not COP1). */
static uint32_t fexp(uint32_t b) { return (b >> 23) & 0xFFu; }

/* ------------------------------------------------------------------------
 * 001278C0 (soft-float unpack) as used by 001281C0 / 00128250: exponent
 * field 0 is class 2 (zero; the fraction is ignored), 255 with fraction 0
 * is class 4 (Inf), 255 otherwise class 0/1 (NaN); else class 3 with
 * exponent e - 127 and fraction (f << 7) | 0x40000000. */

/* 001281C0 float_to_int: class 2 / NaN -> 0; Inf and exponent >= 31
 * saturate by sign; exponent < 0 -> 0; else fraction >> (30 - exponent),
 * negated when the sign is set. */
int32_t em_stream_lanes_001281C0(uint32_t b)
{
    uint32_t e = fexp(b), sign = b >> 31, frac;
    int32_t x;
    uint32_t v;
    if (e == 0)
        return 0;
    if (e == 0xFF) {
        if (b & 0x7FFFFFu)
            return 0;
        return sign ? (int32_t)0x80000000u : 0x7FFFFFFF;
    }
    x = (int32_t)e - 127;
    if (x < 0)
        return 0;
    if (x >= 31)
        return sign ? (int32_t)0x80000000u : 0x7FFFFFFF;
    frac = ((b & 0x7FFFFFu) << 7) | 0x40000000u;
    v = frac >> (30 - x);
    return sign ? (int32_t)(0u - v) : (int32_t)v;
}

/* 00128250 float -> unsigned (its a0 is not read): class 2 / NaN / sign set
 * -> 0; Inf -> 0xFFFFFFFF; exponent < 0 -> 0; >= 32 -> 0xFFFFFFFF; 31 ->
 * fraction << 1; else fraction >> (30 - exponent). */
uint32_t em_stream_lanes_00128250(uint32_t b)
{
    uint32_t e = fexp(b), frac;
    int32_t x;
    if (e == 0)
        return 0;
    if (e == 0xFF && (b & 0x7FFFFFu))
        return 0;
    if (b >> 31)
        return 0;
    if (e == 0xFF)
        return 0xFFFFFFFFu;
    x = (int32_t)e - 127;
    if (x < 0)
        return 0;
    if (x >= 32)
        return 0xFFFFFFFFu;
    frac = ((b & 0x7FFFFFu) << 7) | 0x40000000u;
    if (x == 31)
        return frac << 1;
    return frac >> (30 - x);
}

/* ------------------------------------------------------------------------ */

static int fault(EmStreamLanes *L, uint32_t address, int32_t code)
{
    if (L->fault.code == EM_STREAM_FAULT_NONE) {
        L->fault.address = address;
        L->fault.code = code;
    }
    return -1;
}

static int latched(const EmStreamLanes *L) { return L->fault.code != EM_STREAM_FAULT_NONE; }

static int leave(EmStreamLanes *L, uint32_t callee, int result)
{
    return result < 0 ? fault(L, callee, EM_STREAM_FAULT_WORKER_FAILED) : 0;
}

static int lane_ok(EmStreamLanes *L, uint32_t fn, int32_t lane)
{
    return (lane >= 0 && lane < EM_STREAM_LANES) ? 0 : fault(L, fn, EM_STREAM_FAULT_BAD_INDEX);
}

static EmStreamLanesGlobals *globals(EmStreamLanes *L, uint32_t fn)
{
    if (!L->globals)
        fault(L, fn, EM_STREAM_FAULT_NULL_WORKER);
    return L->globals;
}

void em_stream_lanes_bind(EmStreamLanes *L, const EmStreamLanesData *data,
                          EmStreamLanesGlobals *g, const EmStreamLanesWorkers *w)
{
    L->data = data;
    L->globals = g;
    if (w)
        L->workers = *w;
    else
        memset(&L->workers, 0, sizeof L->workers);
    L->fault.address = 0;
    L->fault.code = EM_STREAM_FAULT_NONE;
}

/* 001157F0 through its worker. */
static int iop(EmStreamLanes *L, int32_t cmd, int32_t a1, int32_t a2, int32_t a3)
{
    if (!L->workers.w_001157F0)
        return fault(L, 0x001157F0u, EM_STREAM_FAULT_NULL_WORKER);
    return leave(L, 0x001157F0u, L->workers.w_001157F0(L->workers.ctx, cmd, a1, a2, a3));
}

/* 0011A608(mask, a1, a2): 001157F0(0x40, mask & 0xFFFFFF,
 * (mask >> 24) & 0xFFFFFF (dsrl), (a1 << 16) | a2). */
static int cmd_0011A608(EmStreamLanes *L, uint64_t mask, int32_t a1, int32_t a2)
{
    return iop(L, EM_STREAM_CMD_0011A608, (int32_t)(mask & 0xFFFFFFu),
               (int32_t)((mask >> 24) & 0xFFFFFFu), (int32_t)(((uint32_t)a1 << 16) | (uint32_t)a2));
}

/* 0011A6A0 / 0011A6E8(mask): 001157F0(0x42 / 0x43, lo24, hi24, 0). */
static int cmd_mask(EmStreamLanes *L, int32_t cmd, uint64_t mask)
{
    return iop(L, cmd, (int32_t)(mask & 0xFFFFFFu), (int32_t)((mask >> 24) & 0xFFFFFFu), 0);
}

int em_stream_lanes_00119828(EmStreamLanes *L, int32_t a0, int32_t a1, int32_t a2)
{
    if (latched(L))
        return -1;
    return iop(L, EM_STREAM_CMD_00119828, a0, a1, a2);
}

/* 0011A730(voice): word[voice] of D_00281880 when (unsigned)voice < 0x30. */
static int voice_status(EmStreamLanes *L, uint32_t fn, int32_t voice, int32_t *out)
{
    EmStreamLanesGlobals *g = globals(L, fn);
    *out = 0;
    if (!g)
        return -1;
    if ((uint32_t)voice >= EM_STREAM_VOICE_STATUS)
        return 0;
    if (!g->d281880)
        return fault(L, 0x00281880u, EM_STREAM_FAULT_NULL_WORKER);
    *out = g->d281880[voice];
    return 0;
}

/* ------------------------------------------------------------------------ */

/* 001FAAC0(lane): when active, 0011A6E8(+0x08); active = 0; cue = 0;
 * +0x03 = 0. */
static int lane_release(EmStreamLanes *L, int32_t lane)
{
    EmStreamLanesState *s = &L->state;
    if (lane_ok(L, 0x001FAAC0u, lane))
        return -1;
    if (s->active[lane] == 0)
        return 0;
    if (cmd_mask(L, EM_STREAM_CMD_0011A6E8, s->lane[lane].voice_mask))
        return -1;
    s->active[lane] = 0;
    s->cue[lane] = 0;
    s->lane[lane].load = 0;
    return 0;
}

int em_stream_lanes_001FAAC0(EmStreamLanes *L, int32_t lane)
{
    return latched(L) ? -1 : lane_release(L, lane);
}

static int fade_in(EmStreamLanes *L, int32_t lane, int32_t cue, int32_t fade, int32_t enable);

/* 001FA790(lane, cue). */
static int lane_start(EmStreamLanes *L, int32_t lane, int32_t cue)
{
    EmStreamLanesState *s = &L->state;
    EmStreamLanesGlobals *g;
    EmStreamLane *r;
    const EmStreamClip *row;
    uint32_t f, half, size;

    if (lane_ok(L, 0x001FA790u, lane))
        return -1;
    if (s->active[lane] != 0 || cue == 0)
        return 0;
    s->cue[lane] = cue;                      /* stored to D_00282178[lane] */
    if (!L->data)
        return fault(L, 0x001FA790u, EM_STREAM_FAULT_NULL_WORKER);
    if (!(g = globals(L, 0x001FA790u)))
        return -1;
    r = &s->lane[lane];
    if (lane != 0) {
        if (!L->data->voice || cue < 0 || (uint32_t)cue >= L->data->voice_count)
            return fault(L, 0x0025E170u, EM_STREAM_FAULT_BAD_INDEX);
        row = &L->data->voice[cue];
        r->base_sector = s->voice_sector + (uint32_t)row->sector;
        r->start_time = g->d810E90;
        f = em_ee_cvt_s_w_bits(em_ee_int_word(row->size >> 11)); /* arithmetic shift: sectors */
        f = em_ee_sub_bits(em_ee_mul_bits(F_60, em_ee_mul_bits(F_TICKS, f)), F_30);
    } else {
        /* The original reads D_0025DD30 + 16 * cue with no bound. The music
         * table has exactly EM_STREAM_MUSIC_ROWS rows and the voice table
         * D_0025E170 starts right after it (0x25DD30 + 16 * 68 = 0x25E170),
         * so a cue past the music rows is the voice row cue - 68. Anything
         * beyond both supplied tables faults. */
        if (!L->data->music || cue < 0)
            return fault(L, 0x0025DD30u, EM_STREAM_FAULT_BAD_INDEX);
        if ((uint32_t)cue < L->data->music_count)
            row = &L->data->music[cue];
        else if (L->data->music_count == EM_STREAM_MUSIC_ROWS && L->data->voice &&
                 (uint32_t)cue - EM_STREAM_MUSIC_ROWS < L->data->voice_count)
            row = &L->data->voice[(uint32_t)cue - EM_STREAM_MUSIC_ROWS];
        else
            return fault(L, 0x0025DD30u, EM_STREAM_FAULT_BAD_INDEX);
        s->music_clip = cue;                                 /* D_00275B2C */
        r->base_sector = s->music_sector + (uint32_t)row->sector;
        r->start_time = g->d810E90;
        f = em_ee_cvt_s_w_bits(em_ee_int_word(row->size >> 11));
        f = em_ee_div_bits(em_ee_mul_bits(F_TICKS, f), F_2);
        f = em_ee_sub_bits(em_ee_mul_bits(F_60, f), F_30);
    }
    r->duration = em_stream_lanes_00128250(f);               /* D_0028201C */
    r->loop = row->loop;                                     /* row +0xC */
    r->loop_sector = r->base_sector;
    r->end_sector = (uint32_t)((int32_t)((uint32_t)row->size + 0x7FFu) >> 11) + r->base_sector;
    r->state = 1;
    r->last_half = 1;
    r->half = 2;
    r->load = 1;
    r->read_addr = r->buffer;
    r->read_sector = r->base_sector;
    half = r->buffer_size >> 1;
    size = (uint32_t)row->size;
    if (half < size)
        r->read_count = (half + 0x7FFu) >> 11;                        /* srl */
    else
        r->read_count = (uint32_t)((int32_t)(size + 0x7FFu) >> 11);   /* sra */
    s->active[lane] = 1;
    return fade_in(L, lane, cue, 0, 0);
}

int em_stream_lanes_001FA790(EmStreamLanes *L, int32_t lane, int32_t cue)
{
    return latched(L) ? -1 : lane_start(L, lane, cue);
}

/* 001FABF0(lane, cue, fade, enable). With enable: only for an inactive lane,
 * +0x58 = 0, 001FA790(lane, cue), 0011A608(+0x08, 0, 0). Then (or, without
 * enable, for an active lane) +0x5C = 0 and +0x54 = 16383 / fade (16383.0
 * for fade 0). */
static int fade_in(EmStreamLanes *L, int32_t lane, int32_t cue, int32_t fade, int32_t enable)
{
    EmStreamLanesState *s = &L->state;
    EmStreamLane *r;
    if (lane_ok(L, 0x001FABF0u, lane))
        return -1;
    r = &s->lane[lane];
    if (enable != 0) {
        if (s->active[lane] != 0)
            return 0;
        r->volume = 0;
        if (lane_start(L, lane, cue))
            return -1;
        if (cmd_0011A608(L, r->voice_mask, 0, 0))
            return -1;
    } else if (s->active[lane] == 0) {
        return 0;
    }
    r->release = 0;
    r->fade_step = fade != 0 ? em_ee_div_bits(F_16383, em_ee_cvt_s_w_bits(em_ee_int_word(fade))) : F_16383;
    return 0;
}

int em_stream_lanes_001FABF0(EmStreamLanes *L, int32_t lane, int32_t cue, int32_t fade, int32_t enable)
{
    return latched(L) ? -1 : fade_in(L, lane, cue, fade, enable);
}

/* 001FAD70(lane, fade, release): for an active lane, +0x5C = (s8)release and
 * +0x54 = -(16383 / fade) (-16383.0 for fade 0). */
int em_stream_lanes_001FAD70(EmStreamLanes *L, int32_t lane, int32_t fade, int32_t release)
{
    EmStreamLane *r;
    if (latched(L) || lane_ok(L, 0x001FAD70u, lane))
        return -1;
    if (L->state.active[lane] == 0)
        return 0;
    r = &L->state.lane[lane];
    r->release = (int8_t)release;
    r->fade_step = fade != 0 ? em_ee_neg_bits(em_ee_div_bits(F_16383, em_ee_cvt_s_w_bits(em_ee_int_word(fade)))) : F_M16383;
    return 0;
}

/* 001FAB50: 001FAAC0(0); D_008106F4 = 0. */
static int music_release(EmStreamLanes *L)
{
    EmStreamLanesGlobals *g;
    if (lane_release(L, 0))
        return -1;
    if (!(g = globals(L, 0x001FAB50u)))
        return -1;
    g->d8106F4 = 0;
    return 0;
}

int em_stream_lanes_001FAB50(EmStreamLanes *L) { return latched(L) ? -1 : music_release(L); }

/* 001FAB80: 001FAAC0(1); 001FAAC0(2); D_008106F5 = 0. */
static int voice_release(EmStreamLanes *L)
{
    EmStreamLanesGlobals *g;
    if (lane_release(L, 1) || lane_release(L, 2))
        return -1;
    if (!(g = globals(L, 0x001FAB80u)))
        return -1;
    g->d8106F5 = 0;
    return 0;
}

int em_stream_lanes_001FAB80(EmStreamLanes *L) { return latched(L) ? -1 : voice_release(L); }

/* 001FA570: 00121A28(D_00281CF0, 0xFF, 0x40) (every slot -1);
 * D_00275B34 = 0; D_00275B30 = 0. */
static void ring_reset(EmStreamLanesState *s)
{
    memset(s->ring, 0xFF, sizeof s->ring);
    s->ring_tail = 0;
    s->ring_head = 0;
}

int em_stream_lanes_001FA570(EmStreamLanes *L)
{
    if (latched(L))
        return -1;
    ring_reset(&L->state);
    return 0;
}

/* 001FABB0: 001FA570; 001FAB50; 001FAB80; D_00282157 = 0. */
static int stop_all(EmStreamLanes *L)
{
    ring_reset(&L->state);
    if (music_release(L) || voice_release(L))
        return -1;
    L->state.read_phase = 0;
    return 0;
}

int em_stream_lanes_001FABB0(EmStreamLanes *L) { return latched(L) ? -1 : stop_all(L); }

/* 001FD470(mask): bit 0 -> 001FBC50(); bit 1 -> 001FABB0(). */
int em_stream_lanes_001FD470(EmStreamLanes *L, int32_t mask)
{
    if (latched(L))
        return -1;
    if (mask & 1) {
        if (!L->workers.w_001FBC50)
            return fault(L, 0x001FBC50u, EM_STREAM_FAULT_NULL_WORKER);
        if (leave(L, 0x001FBC50u, L->workers.w_001FBC50(L->workers.ctx)))
            return -1;
    }
    if (mask & 2)
        return stop_all(L);
    return 0;
}

/* 001FAE70(a0) (byte-matched C). */
static int music_select(EmStreamLanes *L, int32_t a0)
{
    EmStreamLanesState *s = &L->state;
    EmStreamLanesGlobals *g;
    int32_t s0, s2, rng;

    if (!L->workers.w_001FC280)
        return fault(L, 0x001FC280u, EM_STREAM_FAULT_NULL_WORKER);
    if (leave(L, 0x001FC280u, L->workers.w_001FC280(L->workers.ctx)))
        return -1;
    if (!(g = globals(L, 0x001FAE70u)))
        return -1;
    s0 = (int32_t)((uint32_t)g->d8106C8 & 0xFF00u) >> 8;
    if (g->d810D38 != 0) {
        s0 &= 0x80;
        s0 |= g->d810D38;
    }
    if (g->d810700 != 0x15 && g->d810D38 != 0xB && g->d810D38 != 0xC && g->d810D38 != 0x17 &&
        g->d8104E4 == 1) {
        if (s->cue[0] != 0x18) {
            if (lane_release(L, 0))
                return -1;
            return fade_in(L, 0, 0x18, 0x40, 1);
        }
        return 0;
    }
    if (!L->workers.w_00122BB8)
        return fault(L, 0x00122BB8u, EM_STREAM_FAULT_NULL_WORKER);
    rng = 0;
    if (leave(L, 0x00122BB8u, L->workers.w_00122BB8(L->workers.ctx, &rng)))
        return -1;
    s2 = (rng >> 16) & 0x7F;
    if (a0 != 0) {
        if (music_release(L))
            return -1;
        s0 &= 0x7F;
        if (s0 != 0)
            return fade_in(L, 0, s0, s2 + 0x10E, 1);
        return 0;
    }
    s0 &= 0x7F;
    if (s0 == 0)
        return music_release(L);
    if (s->active[0] == 0 || s->cue[0] != s0) {
        if (music_release(L))
            return -1;
        return fade_in(L, 0, s0, s2 + 0x10E, 1);
    }
    return 0;
}

int em_stream_lanes_001FAE70(EmStreamLanes *L, int32_t a0) { return latched(L) ? -1 : music_select(L, a0); }

/* 001FB0B0(cue): D_00810D38 = cue; 001FAE70(1). */
int em_stream_lanes_001FB0B0(EmStreamLanes *L, int32_t cue)
{
    EmStreamLanesGlobals *g;
    if (latched(L))
        return -1;
    if (!(g = globals(L, 0x001FB0B0u)))
        return -1;
    g->d810D38 = cue;
    return music_select(L, 1);
}

/* 001FA5F0: pop the ring slot at D_00275B34 into the first idle voice lane
 * (1, then 2); a consumed slot becomes -1 and the pop index advances. */
static int ring_pop(EmStreamLanes *L)
{
    EmStreamLanesState *s = &L->state;
    int32_t v;
    if (s->ring_tail < 0 || s->ring_tail >= EM_STREAM_RING)
        return fault(L, 0x00275B34u, EM_STREAM_FAULT_BAD_INDEX);
    v = s->ring[s->ring_tail];
    if (v == -1)
        return 0;
    if (s->active[1] == 0) {
        if (lane_start(L, 1, v))
            return -1;
        v = -1;
    } else if (s->active[2] == 0) {
        if (lane_start(L, 2, v))
            return -1;
        v = -1;
    }
    if (v == -1) {
        s->ring[s->ring_tail] = v;
        s->ring_tail = (int8_t)((s->ring_tail + 1) & 0xF);
    }
    return 0;
}

int em_stream_lanes_001FA5F0(EmStreamLanes *L) { return latched(L) ? -1 : ring_pop(L); }

/* 001FA0D0: the disc read sequencer over D_00282157 (phase) and D_00282158
 * (lane). */
static int read_step(EmStreamLanes *L)
{
    EmStreamLanesState *s = &L->state;
    const EmStreamLanesWorkers *w = &L->workers;
    int32_t idx, result;
    uint8_t mode[3];

    switch (s->read_phase) {
    case 0:
        idx = s->read_lane;
        while (idx < 3) {
            if (idx < 0)
                return fault(L, 0x001FA0D0u, EM_STREAM_FAULT_BAD_INDEX);
            if (s->lane[idx].load == 1) {
                s->read_phase = (int8_t)(s->read_phase + 1);
                s->read_lane = (int8_t)idx;
                break;
            }
            idx = (int8_t)(idx + 1);
        }
        if (s->read_phase == 0)
            s->read_lane = 0;
        return 0;
    case 1:
        idx = s->read_lane;
        if (idx < 0 || idx >= 3)
            return fault(L, 0x001FA0D0u, EM_STREAM_FAULT_BAD_INDEX);
        if (s->lane[idx].load != 1) {
            idx = (int8_t)(idx + 1);
            s->read_phase = 0;
            s->read_lane = (int8_t)(idx < 3 ? idx : 0);
            return 0;
        }
        if (!w->w_00113280)
            return fault(L, 0x00113280u, EM_STREAM_FAULT_NULL_WORKER);
        result = 0;
        if (leave(L, 0x00113280u, w->w_00113280(w->ctx, 1, &result)))
            return -1;
        if (result != 2)
            return 0;
        mode[0] = mode[1] = mode[2] = 0;
        if (!w->w_00112610)
            return fault(L, 0x00112610u, EM_STREAM_FAULT_NULL_WORKER);
        result = 0;
        if (leave(L, 0x00112610u,
                  w->w_00112610(w->ctx, s->lane[idx].read_sector, s->lane[idx].read_count,
                                s->lane[idx].read_addr, mode, &result)))
            return -1;
        if (result != 0)
            s->read_phase = (int8_t)(s->read_phase + 1);
        return 0;
    case 2:
        idx = s->read_lane;
        if (idx < 0 || idx >= 3)
            return fault(L, 0x001FA0D0u, EM_STREAM_FAULT_BAD_INDEX);
        if (s->lane[idx].load != 1) {
            if (!w->w_00113478)
                return fault(L, 0x00113478u, EM_STREAM_FAULT_NULL_WORKER);
            if (leave(L, 0x00113478u, w->w_00113478(w->ctx, 1)))
                return -1;
            s->read_phase = 0;
            idx = (int8_t)(idx + 1);
            s->read_lane = (int8_t)(idx < 3 ? idx : 0);
            return 0;
        }
        if (!w->w_00112D18)
            return fault(L, 0x00112D18u, EM_STREAM_FAULT_NULL_WORKER);
        result = 0;
        if (leave(L, 0x00112D18u, w->w_00112D18(w->ctx, 1, &result)))
            return -1;
        if (result == 0) {
            s->read_phase = 0;
            s->lane[idx].load = 2;
            idx = (int8_t)(idx + 1);
            s->read_lane = (int8_t)(idx < 3 ? idx : 0);
        }
        return 0;
    default:
        return 0;
    }
}

int em_stream_lanes_001FA0D0(EmStreamLanes *L) { return latched(L) ? -1 : read_step(L); }

/* 001FA330: per active lane with a nonzero +0x54, step +0x58 toward the cap
 * (0x3FFF; 0x3000 for lane 0 while D_0028215B != 0) or down to 0 (then
 * 001FAAC0 when +0x5C != 0), and send the volume words. */
static int volume_step(EmStreamLanes *L)
{
    EmStreamLanesState *s = &L->state;
    int32_t i, cap, a, b;
    for (i = 0; i < EM_STREAM_LANES; i++) {
        EmStreamLane *r = &s->lane[i];
        cap = i != 0 ? 0x3FFF : (s->mono != 0 ? 0x3000 : 0x3FFF);
        if (s->active[i] == 0)
            continue;
        if (em_ee_c_eq_bits(r->fade_step, 0))
            continue;
        r->volume = em_ee_add_bits(r->volume, r->fade_step);
        if (!em_ee_c_le_bits(r->fade_step, 0)) {
            uint32_t capf = em_ee_cvt_s_w_bits(em_ee_int_word(cap));
            if (!em_ee_c_lt_bits(r->volume, capf)) {
                r->fade_step = 0;
                r->volume = capf;
            }
        } else if (em_ee_c_le_bits(r->volume, 0)) {
            r->fade_step = 0;
            r->volume = 0;
            if (r->release != 0 && lane_release(L, i))
                return -1;
        }
        if (i != 0) {
            a = em_stream_lanes_001281C0(r->volume);
            b = em_stream_lanes_001281C0(r->volume);
            if (cmd_0011A608(L, r->voice_mask, a, b))
                return -1;
        } else if (s->mono == 0) {
            b = em_stream_lanes_001281C0(r->volume);
            if (cmd_0011A608(L, (uint64_t)1 << ((uint32_t)r->voice & 63u), 0, b))
                return -1;
            a = em_stream_lanes_001281C0(r->volume);
            if (cmd_0011A608(L, (uint64_t)1 << ((uint32_t)s->voice_right & 63u), a, 0))
                return -1;
        } else {
            a = em_stream_lanes_001281C0(r->volume);
            b = em_stream_lanes_001281C0(r->volume);
            if (cmd_0011A608(L, (uint64_t)1 << ((uint32_t)r->voice & 63u), a, b))
                return -1;
            a = em_stream_lanes_001281C0(r->volume);
            b = em_stream_lanes_001281C0(r->volume);
            if (cmd_0011A608(L, (uint64_t)1 << ((uint32_t)s->voice_right & 63u), a, b))
                return -1;
        }
    }
    return 0;
}

int em_stream_lanes_001FA330(EmStreamLanes *L) { return latched(L) ? -1 : volume_step(L); }

/* 001F9CF0: per lane the timer end, the half-buffer switch 0/1/2 and the
 * hold bytes (D_008106F4 for lane 0, D_008106F5 for 1/2); then 001FA0D0,
 * 001FA5F0, 001FA330; then 0011A6E8 for every inactive lane whose voice
 * status is nonzero. */
static int service(EmStreamLanes *L)
{
    EmStreamLanesState *s = &L->state;
    EmStreamLanesGlobals *g = globals(L, 0x001F9CF0u);
    int32_t i, v0;
    uint32_t v1, a0, a1, last;
    if (!g)
        return -1;
    for (i = 0; i < EM_STREAM_LANES; i++) {
        EmStreamLane *r = &s->lane[i];
        uint8_t *hold = i != 0 ? &g->d8106F5 : &g->d8106F4;
        if (s->active[i] == 0)
            continue;
        if (s->active[i] == 2 && r->loop == 0) {
            last = r->start_time;
            if (g->d810E90 < last) {
                a1 = g->d810E90;
                r->remaining = (int32_t)(r->duration - ((0xFFFFFFFFu - last) + g->d810E90));
            } else {
                a1 = g->d810E90 - last;
                r->remaining = (int32_t)(r->duration - a1);
            }
            if (!(a1 < r->duration)) {
                if (lane_release(L, i))
                    return -1;
                continue;
            }
        }
        if (r->state == 2) {
            if (*hold != 0)
                continue;
            r->state = 0;
            s->active[i] = (int8_t)(s->active[i] + 1);
            r->start_time = g->d810E90;
            if (cmd_mask(L, EM_STREAM_CMD_0011A6A0, r->voice_mask))
                return -1;
            continue;
        }
        if (r->state != 1) {
            if (r->state != 0)
                continue;
            if (voice_status(L, 0x001F9CF0u, r->voice, &v0))
                return -1;
            if (v0 == 0)
                continue;
            if ((uint32_t)v0 < (r->buffer_size >> 1)) {
                r->half = 1;
                r->read_addr = r->buffer + (r->buffer_size >> 1);
            } else {
                r->half = 2;
                r->read_addr = r->buffer;
            }
            if (r->last_half == r->half)
                continue;
            r->state = 1;
            r->load = 1;
            v1 = r->end_sector - r->read_sector;
            a0 = ((r->buffer_size >> 1) + 0x7FFu) >> 11;
            r->read_count = a0 < v1 ? a0 : v1;
        }
        /* state 1 (and the state-0 fall-through) */
        if (r->load != 2)
            continue;
        r->read_addr += r->read_count << 11;
        a1 = r->half == 1 ? r->buffer + r->buffer_size : r->buffer + (r->buffer_size >> 1);
        if (r->read_addr != a1) {
            r->load = 1;
            r->read_sector = r->loop_sector;
            v1 = r->end_sector - r->loop_sector;
            a0 = a1 - r->read_addr;
            /* sltu (v1 << 11), a0: the smaller of the two (the NEARMISS C
             * has these two arms swapped; the .s is followed). */
            r->read_count = (v1 << 11) < a0 ? v1 : (a0 + 0x7FFu) >> 11;
            continue;
        }
        r->state = 0;
        r->load = 0;
        r->last_half = r->half;
        r->read_sector += r->read_count;
        if (!(r->read_sector < r->end_sector))
            r->read_sector = r->loop_sector + (r->read_sector - r->end_sector);
        if (s->active[i] != 1)
            continue;
        if (*hold != 0) {
            r->state = 2;
            *hold = 1;
            continue;
        }
        s->active[i] = (int8_t)(s->active[i] + 1);
        r->start_time = g->d810E90;
        if (cmd_mask(L, EM_STREAM_CMD_0011A6A0, r->voice_mask))
            return -1;
    }
    if (read_step(L) || ring_pop(L) || volume_step(L))
        return -1;
    for (i = 0; i < EM_STREAM_LANES; i++) {
        if (s->active[i] != 0)
            continue;
        if (voice_status(L, 0x001F9CF0u, s->lane[i].voice, &v0))
            return -1;
        if (v0 != 0) {
            if (cmd_mask(L, EM_STREAM_CMD_0011A6E8, s->lane[i].voice_mask))
                return -1;
            s->cue[i] = 0;
            s->lane[i].load = 0;
        }
    }
    return 0;
}

int em_stream_lanes_001F9CF0(EmStreamLanes *L) { return latched(L) ? -1 : service(L); }
