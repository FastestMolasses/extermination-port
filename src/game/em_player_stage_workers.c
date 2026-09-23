/* em_player_stage_workers.c - the player stage workers (see
 * em_player_stage_workers.h, docs/PLAYER_STAGE_WORKERS.md).
 *
 * Read from the original instructions (the decomp C where it is byte-matched,
 * the split listing for the asm-word units 0021BB00, 0021BC40, 0021C350,
 * 0021D6C0, 0017C370, 0015D000, 0015B530, 001B1470, 001278C0 and the
 * NEARMISS anim_advance_time). Every original address a branch or store
 * comes from is cited beside it. Float arithmetic and compares go through
 * em_ee_float.h on bit patterns, as the COP1 instructions execute. */
#include "game/em_player_stage_workers.h"
#include "game/em_ee_float.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZERO   UINT32_C(0x00000000)
#define ONE    UINT32_C(0x3F800000)
#define TWO    UINT32_C(0x40000000)
#define THREE  UINT32_C(0x40400000)
#define FIVE   UINT32_C(0x40A00000)
#define EIGHT  UINT32_C(0x41000000)
#define TEN    UINT32_C(0x41200000)
#define F35    UINT32_C(0x420C0000)
#define F60    UINT32_C(0x42700000)
#define F100   UINT32_C(0x42C80000)
#define F300   300.0f
#define PI     UINT32_C(0x40490FDB)
#define NEG_PI UINT32_C(0xC0490FDB)
#define TWO_PI UINT32_C(0x40C90FDB)
#define HALF_PI UINT32_C(0x3FC90FDB)

#define CALL(expr) do { if ((expr) < 0) return -1; } while (0)

/* ---- record access ------------------------------------------------------ */

static uint8_t b(const EmPlayerLiveActor *a, unsigned at) { return a->bytes[at]; }
static void setb(EmPlayerLiveActor *a, unsigned at, uint8_t v) { a->bytes[at] = v; }
static uint32_t w(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void setw(EmPlayerLiveActor *a, unsigned at, uint32_t v) { em_live_set_u32(a, at, v); }
static int16_t h(const EmPlayerLiveActor *a, unsigned at) { return (int16_t)em_live_u16(a, at); }
static void seth(EmPlayerLiveActor *a, unsigned at, int v) { em_live_set_u16(a, at, (uint16_t)v); }

/* c.eq.s x, 0 / c.le.s / c.lt.s on the record's words. */
static int is_zero(const EmPlayerLiveActor *a, unsigned at) { return em_ee_c_eq_bits(w(a, at), ZERO); }
static int le(uint32_t x, uint32_t y) { return em_ee_c_le_bits(x, y); }

/* ---- D_00248C98 --------------------------------------------------------- */

int em_player_clip_rates_parse(EmPlayerClipRates *out, const uint8_t *data, size_t size)
{
    if (!out || !data || size < 12 || memcmp(data, "EMCR", 4) != 0) return -1;
    uint32_t version, count;
    memcpy(&version, data + 4, 4);
    memcpy(&count, data + 8, 4);
    if (version != 1 || count != EM_PLAYER_CLIP_RATE_ROWS || size != 12 + (size_t)count * 4)
        return -1;
    out->count = count;
    memcpy(out->rate, data + 12, (size_t)count * 4);
    return 0;
}

int em_player_clip_rates_load(EmPlayerClipRates *out, const char *path)
{
    if (!out || !path) return -1;
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    uint8_t buffer[12 + EM_PLAYER_CLIP_RATE_ROWS * 4 + 1];
    size_t size = fread(buffer, 1, sizeof buffer, file);
    fclose(file);
    return em_player_clip_rates_parse(out, buffer, size);
}

/* 0015BA50: D_00248C98[*(short *)(p + 0x20C) * 3]. The original reads any
 * index; a clip outside the 459 rows is not data this table defines, so it
 * faults. */
int em_player_stage_clip_rate(void *host, int clip, float *rate)
{
    const EmPlayerStageHost *hs = host;
    if (!hs || !hs->rates || !rate || hs->rates->count != EM_PLAYER_CLIP_RATE_ROWS) return -1;
    if (clip < 0 || clip >= EM_PLAYER_CLIP_RATE_ROWS) return -1;
    *rate = hs->rates->rate[clip];
    return 0;
}

/* ---- leaf predicates ------------------------------------------------------ */

/* 0021BB00: 1 when +1F0 is a mode the reaction must leave alone. */
int em_player_0021BB00(const EmPlayerLiveActor *a)
{
    unsigned m = b(a, 0x1F0);
    if (m == 8) return 1;                                   /* 0021BB08 */
    if (m - 9 < 2) return 1;                                /* 0021BB18: 9, 0xA */
    if (m == 0x14) return 1;                                /* 0021BB24 */
    if (m == 0x2C && b(a, 0xD) != 2) return 1;              /* 0021BB30 / 0021BB40 */
    if (m == 0x2E) return 1;                                /* 0021BB4C */
    if (m - 0x11 < 3 || m - 0x15 < 2 || m - 0x18 < 2 || m - 0x1B < 2 ||
        m - 0x1E < 3 || m == 0x28 || m - 0x23 < 4) return 1;   /* 0021BB5C..0021BBB8 */
    if (m != 0xF) return 0;                                 /* 0021BBD0 */
    if (b(a, 4) != 1) return 1;                             /* 0021BBE0 */
    if (b(a, 5) != 8) return 1;                             /* 0021BBF0 */
    unsigned phase = b(a, 6);
    if (phase == 2 || phase - 4 < 2) return 0;              /* 0021BC00 / 0021BC10 */
    return 1;
}

/* 0021BC40: 1 while +1F0 is a mode that ignores pending damage. */
int em_player_0021BC40(const EmPlayerLiveActor *a)
{
    unsigned m = b(a, 0x1F0);
    if (m == 0xB || m - 0xC < 2) return 1;                  /* 0021BC48 / 0021BC58 */
    if (m == 0x2C) return b(a, 0xD) == 2;                   /* 0021BC64..0021BC74, then no match */
    return m == 0x10 || m == 0x17 || m == 0x1A || m == 0x1D || m == 0x2A ||
           m - 0x21 < 2 || m - 0x2F < 2 || m == 0x39 || m == 0x2D;   /* 0021BC80..0021BCE8 */
}

/* 0021D640 (byte-matched). */
int em_player_0021D640(const EmPlayerLiveActor *a)
{
    unsigned state = b(a, 0x1F0);
    if (state == 0x27 || state - 0x34 < 2) return 1;
    if (b(a, 4) == 1) {
        unsigned b5 = b(a, 5);
        if ((b5 == 0x1F || b5 == 0x20) && state == 0x33) return 1;
    }
    return 0;
}

/* 0021C3F0 (byte-matched): 0 only in area 8/2 with D_00810770 == 0xFF. */
int em_player_0021C3F0(const EmPlayerStageScene *stage, const EmPlayerStageGlobals *g)
{
    if (stage->area != 8) return 1;
    if (g->d810701 != 2) return 1;
    if (g->d810770 != 0xFF) return 1;
    return 0;
}

/* 001B1470: subtract 2pi until <= pi, then add 2pi while <= -pi. */
uint32_t em_player_001B1470(uint32_t x)
{
    if (!le(x, PI)) {                                        /* 001B1488 */
        do x = em_ee_sub_bits(x, TWO_PI);                    /* 001B149C */
        while (!le(x, PI));
    }
    while (le(x, NEG_PI)) x = em_ee_add_bits(x, TWO_PI);     /* 001B14C8 / 001B14E4 */
    return x;
}

/* 001281C0 float_to_int over 001278C0's unpack: exponent 0 is class 2 (0),
 * exponent 255 is 4 (infinity) or 0/1 (NaN), else 3 with the fraction
 * (m << 7) | 1 << 30 and exponent e - 127. */
int32_t em_player_float_to_int(uint32_t v)
{
    uint32_t exp = v >> 23 & 0xFF, frac = v & 0x7FFFFF, sign = v >> 31;
    if (exp == 0) return 0;                                  /* class 2: 001281E0 */
    if (exp == 0xFF) {
        if (frac != 0) return 0;                             /* classes 0/1: 001281E8 */
        return sign ? INT32_MIN : INT32_MAX;                 /* class 4: 00128210 */
    }
    int e = (int)exp - 0x7F;
    if (e < 0) return 0;                                     /* 00128200 */
    if (e >= 31) return sign ? INT32_MIN : INT32_MAX;        /* 00128208 -> 00128210 */
    uint32_t r = ((frac << 7) | UINT32_C(0x40000000)) >> (30 - e);  /* 00128234 */
    return sign ? (int32_t)(0u - r) : (int32_t)r;            /* 0012823C movn */
}

/* ---- worker readiness (fail-stop before the first write) -------------- */

static const EmPlayerStageCallees *ready(const EmPlayerStageHost *hs)
{
    return hs && hs->stage && hs->globals ? &hs->callees : NULL;
}

static int reaction_callees(const EmPlayerStageCallees *c)
{
    return c && c->sound && c->cue && c->w001EFE00 && c->w001F00A0 && c->w001F0060 &&
           c->atan2 && c->link20 && c->clip_lookup && c->request;
}

/* ---- 0021C440's callees --------------------------------------------------- */

/* 0021C350: apply the pending front damage +224. */
static void apply_224(EmPlayerLiveActor *a)
{
    uint32_t damage = w(a, 0x224);
    if (em_ee_c_eq_bits(damage, ZERO)) return;               /* 0021C364 */
    setw(a, 0x220, em_ee_sub_bits(w(a, 0x220), damage));    /* 0021C37C */
    setw(a, 0x224, ZERO);
    if (le(w(a, 0x220), F35)) {                              /* 0021C38C */
        setb(a, 0x235, b(a, 0x235) & 0xFE);
        setb(a, 0x235, b(a, 0x235) | 1);
    }
    if (le(w(a, 0x220), ZERO)) {                             /* 0021C3C0 */
        setw(a, 0x220, ZERO);
        setb(a, 0, 2);
    }
}

int em_player_0021C350(void *host, EmPlayerLiveActor *a)
{
    (void)host;
    if (!a) return -1;
    apply_224(a);
    return 0;
}

/* 0021D4E0: 001F0060(+234 ? 0x80000062 : 0x80000061, 0). */
static int effect_0021D4E0(const EmPlayerStageCallees *c, EmPlayerLiveActor *a)
{
    uint32_t id = b(a, 0x234) != 0 ? UINT32_C(0x80000062) : UINT32_C(0x80000061);
    return c->w001F0060(c->context, id, 0);
}

int em_player_0021D4E0(void *host, EmPlayerLiveActor *a)
{
    const EmPlayerStageCallees *c = ready(host);
    if (!c || !c->w001F0060 || !a) return -1;
    return effect_0021D4E0(c, a) < 0 ? -1 : 0;
}

/* 0021C270: fold the pending infection +22C into +228. */
static int infect_22C(EmPlayerStageHost *hs, EmPlayerLiveActor *a)
{
    const EmPlayerStageCallees *c = &hs->callees;
    uint32_t dt = w(a, 0x22C);
    if (em_ee_c_eq_bits(dt, ZERO)) return 0;                 /* 0021C28C */
    setw(a, 0x228, em_ee_add_bits(w(a, 0x228), dt));        /* 0021C2A4 */
    setw(a, 0x22C, ZERO);
    if (!em_ee_c_lt_bits(w(a, 0x228), F100)) {               /* 0021C2B4 */
        setw(a, 0x228, F100);
        if (!le(w(a, 0x220), F60)) setw(a, 0x220, F60);      /* 0021C2D8 */
        if (b(a, 0x234) == 0) {                              /* 0021C2F0 */
            setb(a, 0x234, 1);
            hs->globals->d810707 = 1;
            hs->stage->d8106F1 = 1;
        }
    }
    CALL(effect_0021D4E0(c, a));                             /* 0021C314 */
    return c->sound(c->context, a, 0x149, 0, F300) < 0 ? -1 : 0;   /* 0021C32C */
}

int em_player_0021C270(void *host, EmPlayerLiveActor *a)
{
    const EmPlayerStageCallees *c = ready(host);
    if (!c || !c->w001F0060 || !c->sound || !a) return -1;
    return infect_22C(host, a);
}

/* 0021C200: the infected death cue. */
static int death_cue(EmPlayerStageHost *hs, EmPlayerLiveActor *a)
{
    const EmPlayerStageCallees *c = &hs->callees;
    if (!le(w(a, 0x220), ZERO)) return 0;
    if (hs->stage->d8106F1 == 0) return 0;
    CALL(c->sound(c->context, a, 0x14D, 0, F300));
    return c->w001EFE00(c->context, UINT32_C(0x80000048), a) < 0 ? -1 : 0;
}

int em_player_0021C200(void *host, EmPlayerLiveActor *a)
{
    const EmPlayerStageCallees *c = ready(host);
    if (!c || !c->sound || !c->w001EFE00 || !a) return -1;
    return death_cue(host, a);
}

/* The facing written by 0021D6C0 and the three 0021C440 paths:
 * 0x70003A20 = atan2(-o.C8, o.C0) for o = *(p+20), +C4 = 001B1470(pi/2 +
 * that), and the +27C/+278 copy under +1F0 0x33. */
static int face_link(EmPlayerStageHost *hs, EmPlayerLiveActor *a)
{
    const EmPlayerStageCallees *c = &hs->callees;
    uint32_t c0, c8;
    CALL(c->link20(c->context, w(a, 0x20), &c0, &c8));
    float angle = c->atan2(c->context, em_ee_float(em_ee_neg_bits(c8)), em_ee_float(c0));
    hs->globals->spad3A20 = em_ee_bits(angle);
    setw(a, 0xC4, em_player_001B1470(em_ee_add_bits(HALF_PI, hs->globals->spad3A20)));
    return 0;
}

static void copy_2E0(EmPlayerLiveActor *a)
{
    if (b(a, 0x1F0) != 0x33) return;
    setw(a, 0x27C, w(a, 0x2E0));
    setw(a, 0x278, w(a, 0x2E4));
}

/* 0021D6C0: the climb-hold reaction (+5 = 0xA, mode 0x46). */
static int hold_reaction(EmPlayerStageHost *hs, EmPlayerLiveActor *a, int *result)
{
    *result = 0;
    if (is_zero(a, 0x224) && is_zero(a, 0x22C) && !(b(a, 0xF) & 2)) return 0;  /* 0021D6DC..0021D700 */
    if (!is_zero(a, 0x22C)) {                                /* 0021D71C */
        CALL(infect_22C(hs, a));
        setb(a, 0x1F1, 1);
    }
    if (!is_zero(a, 0x224)) {                                /* 0021D748 */
        apply_224(a);
        setb(a, 0x1F1, 0);
    }
    CALL(face_link(hs, a));                                  /* 0021D75C..0021D794 */
    setw(a, 0xC0, ZERO);                                     /* 0021D798 */
    copy_2E0(a);                                             /* 0021D79C */
    setb(a, 4, 2);
    setb(a, 5, 0xA);
    setb(a, 6, 0);
    setb(a, 0x1F0, 0x46);
    *result = 1;
    return 0;
}

int em_player_0021D6C0(void *host, EmPlayerLiveActor *a, int *result)
{
    const EmPlayerStageCallees *c = ready(host);
    if (!c || !a || !result || !c->w001F0060 || !c->sound || !c->link20 || !c->atan2) return -1;
    return hold_reaction(host, a, result);
}

/* 0017C370: the reaction clip and motion reset. */
static int reaction_reset(EmPlayerStageHost *hs, EmPlayerLiveActor *a)
{
    const EmPlayerStageCallees *c = &hs->callees;
    uint8_t mode = b(a, 0x1F0);
    if (mode == 6 || mode == 7) {                            /* 0017C388 / 0017C394 */
        int16_t clip;
        int row = b(a, 0x1F1) == 3 ? 2 : 4;                  /* 0017C3A4 */
        CALL(c->clip_lookup(c->context, a, row, b(a, 0x235), 0, &clip));
        CALL(c->request(c->context, a, clip, 1, 0.0f));      /* 0017C3E8 */
        setw(a, 0xC4, em_player_001B1470(em_ee_add_bits(PI, w(a, 0xC4))));  /* 0017C404 */
        setw(a, 0x1FC, EIGHT);
    }
    setw(a, 0x38, ZERO);                                     /* 0017C414 */
    setw(a, 0x240, ZERO);
    setb(a, 0x25C, 0);
    setb(a, 0x25E, 0);
    setw(a, 0x260, ZERO);
    setw(a, 0x264, ZERO);
    setw(a, 0x268, ZERO);
    return 0;
}

int em_player_0017C370(void *host, EmPlayerLiveActor *a)
{
    const EmPlayerStageCallees *c = ready(host);
    if (!c || !a || !c->clip_lookup || !c->request) return -1;
    return reaction_reset(host, a);
}

/* ---- 0021C440 --------------------------------------------------------------- */

static void enter(EmPlayerLiveActor *a, uint8_t state, uint8_t mode)
{
    setb(a, 4, 2);
    setb(a, 5, state);
    setb(a, 6, 0);
    setb(a, 0x1F0, mode);
}

/* The dead exits every path shares: +234 1 -> 2/3 mode 0x3F, else 2/1 0x40. */
static void enter_dead(EmPlayerLiveActor *a)
{
    if (b(a, 0x234) == 1) enter(a, 3, 0x3F);
    else enter(a, 1, 0x40);
}

/* hit_a / hit_b after their damage and effect: 0021CBCC.. / 0021CDA4.. */
static int hit_exit(EmPlayerStageHost *hs, EmPlayerLiveActor *a, uint8_t variant)
{
    if (le(w(a, 0x220), ZERO)) {
        enter_dead(a);
        return 0;
    }
    uint8_t state = b(a, 5);
    if (state == 0x1D || state == 0x1E) {
        CALL(face_link(hs, a));
        copy_2E0(a);
        setb(a, 5, 0x17);
    } else {
        setb(a, 5, 0);
    }
    setb(a, 4, 2);
    setb(a, 6, 0);
    setb(a, 0x1F0, 0x3E);
    setb(a, 0x1F1, variant);
    return 0;
}

/* The shared state gate of hit_a / hit_b (0021CAD4.. / 0021CD0C..). */
static int hit_gate(const EmPlayerLiveActor *a)
{
    if (b(a, 4) == 1 && b(a, 5) == 0) return 1;
    unsigned state = b(a, 5);
    if (state == 1 || state - 0x21 < 2) return 1;
    if ((state == 0x1D || state == 0x1E) && b(a, 0x1F1) == 1) return 1;
    return 0;
}

static int reaction_body(EmPlayerStageHost *hs, EmPlayerLiveActor *a, int *s)
{
    const EmPlayerStageCallees *c = &hs->callees;
    uint8_t st = b(a, 0xF);
    switch (st) {
    case 1:                                                  /* 0021C594 */
        CALL(infect_22C(hs, a));
        enter(a, 0xC, 0x3E);
        setb(a, 0x1F1, 1);
        *s = 0x81;
        setb(a, 0xF, b(a, 0xF) | 0x80);
        return 0;
    case 2: {                                                /* 0021C5E0 */
        unsigned mode = b(a, 0x1F0);
        if ((mode == 0x2C && b(a, 0xD) == 2) || mode == 0x10 || mode == 0x17 || mode == 0x1A ||
            mode == 0x1D || mode == 0x2A || mode - 0x21 < 2 || mode - 0x2F < 2 || mode == 0x39) {
            *s = 1;
        } else if (em_player_0021D640(a)) {                  /* 0021C67C */
            int held;
            CALL(hold_reaction(hs, a, &held));
            if (held) *s = 1;
        } else {
            apply_224(a);                                    /* 0021C6A8 */
            enter(a, 0x10, 0x3E);
            *s = 0x81;
        }
        setb(a, 0xF, b(a, 0xF) | 0x80);
        return 0;
    }
    case 3:                                                  /* 0021C6E4 */
        CALL(infect_22C(hs, a));
        enter(a, 0xF, 0x3E);
        setb(a, 0x1F1, 1);
        *s = 0x81;
        setb(a, 0xF, b(a, 0xF) | 0x80);
        return 0;
    case 5:                                                  /* 0021C730 */
        setw(a, 0x220, ZERO);
        enter(a, 0xF, 0x3E);
        setb(a, 0x1F1, 1);
        *s = 0x81;
        setb(a, 0xF, b(a, 0xF) | 0x80);
        return 0;
    case 4:                                                  /* 0021C770 */
        CALL(infect_22C(hs, a));
        *s = 0x80;
        setb(a, 0xF, b(a, 0xF) | 0x80);
        return 0;
    case 6:                                                  /* 0021C798 */
        apply_224(a);
        if (le(w(a, 0x220), ZERO)) enter_dead(a);
        else enter(a, 0x11, 0x3E);
        *s = 0x81;
        setb(a, 0xF, b(a, 0xF) | 0x80);
        return 0;
    case 7:                                                  /* 0021C844 */
        if (b(a, 0x1F0) == 0x17) {
            setb(a, 4, 2);
            setb(a, 5, 4);
            setb(a, 6, 0);
            setw(a, 0x224, w(a, 0x220));
            setb(a, 0x302, 0);
            CALL(c->sound(c->context, a, 0x159, 0, F300));   /* 0021C884 */
            *s = 1;
        } else {
            enter(a, 0x12, 0x3E);
            *s = 0x81;
            setw(a, 0x220, ZERO);
        }
        setb(a, 0, 2);
        setb(a, 0xF, b(a, 0xF) | 0x80);
        return 0;
    case 8:                                                  /* 0021C8D0 */
        CALL(infect_22C(hs, a));
        apply_224(a);
        *s = 0x80;
        setb(a, 0xF, b(a, 0xF) | 0x80);
        return 0;
    case 9:                                                  /* 0021C900 */
        apply_224(a);
        *s = 0x80;
        setb(a, 0xF, b(a, 0xF) | 0x80);
        return 0;
    case 0xA:                                                /* 0021C928 */
    case 0xB:                                                /* 0021C964 */
        setw(a, 0x220, ZERO);
        enter(a, st == 0xA ? 0x13 : 0x14, 0x3E);
        *s = 0x81;
        setb(a, 0xF, b(a, 0xF) | 0x80);
        return 0;
    default:
        break;
    }

    /* 0021C99C: no hit code. */
    unsigned mode = b(a, 0x1F0);
    if (mode == 0x3B) return 0;
    if (mode == 0x3C) {                                      /* 0021C9B0 */
        if (b(a, 0xD) == 0 && !is_zero(a, 0x224)) {          /* 0021C9BC / 0021C9D8 */
            apply_224(a);
            CALL(c->cue(c->context, 0, 0xC0, 5, 1));         /* 0021C9F4 */
            CALL(c->sound(c->context, a, 0x152, 0, F300));   /* 0021CA0C */
        }
        return 0;
    }
    if (em_player_0021D640(a)) {                             /* 0021CA38 */
        int held;
        CALL(hold_reaction(hs, a, &held));
        if (held) *s = 1;
        return 0;
    }
    if (hs->globals->d81083C != 0) {                         /* 0021CA60 */
        enter(a, 0xB, 0x3B);
        *s = 0x81;
        setb(a, 0x1F1, 1);
        return 0;
    }
    /* hit_a: the infected player on surface 0x5B / 6 (0021CA9C). */
    if (b(a, 0x234) == 1 && b(a, 0) == 1 && (b(a, 0x23A) == 0x5B || b(a, 0x23A) == 6) &&
        hit_gate(a)) {
        setb(a, 0, b(a, 0) | 2);                             /* 0021CB44 */
        if (b(a, 0x23A) == 6) setw(a, 0x224, b(a, 0x31E) == 0 ? THREE : FIVE);
        else setw(a, 0x224, THREE);
        apply_224(a);                                        /* 0021CB8C */
        uint8_t *record = NULL;
        CALL(c->w001F00A0(c->context, UINT32_C(0x8000001B), a, 0, &record));  /* 0021CBA4 */
        if (record) {                                        /* 0021CBB0: copy_qw4 + the +104 word */
            memcpy(record + 0xD0, a->bytes + 0xD0, 0x40);
            memcpy(record + 0x104, a->bytes + 0x250, 4);
        }
        CALL(hit_exit(hs, a, 2));
        *s = 0x81;
        return 0;
    }
    /* hit_b: surface mode 0xA outside area 8/2 with D_00810770 0xFF (0021CCDC). */
    if (em_player_0021C3F0(hs->stage, hs->globals) && b(a, 0) == 1 && b(a, 0x23B) == 0xA &&
        hit_gate(a)) {
        setb(a, 0, b(a, 0) | 2);                             /* 0021CD78 */
        setw(a, 0x224, EIGHT);
        apply_224(a);
        CALL(c->w001EFE00(c->context, UINT32_C(0x80000044), a));   /* 0021CD9C */
        CALL(hit_exit(hs, a, 3));
        *s = 0x81;
        return 0;
    }
    /* try_c: pending damage +224 / infection +22C (0021CEB8). */
    if (is_zero(a, 0x224) && is_zero(a, 0x22C)) return 0;
    if (em_player_0021BC40(a)) return 0;                     /* 0021CEEC */
    if (!is_zero(a, 0x224)) {                                /* 0021CF10 */
        apply_224(a);
        if (b(a, 0xF) == 0xC) {
            setb(a, 0x1F1, 4);
            setb(a, 0xF, 0);
        } else {
            setb(a, 0x1F1, 0);
        }
    }
    if (!is_zero(a, 0x22C)) {                                /* 0021CF58 */
        CALL(infect_22C(hs, a));
        setb(a, 0x1F1, 1);
    }
    if (le(w(a, 0x220), ZERO)) {                             /* 0021CF7C */
        if (b(a, 0xF) == 0x63 || b(a, 0x234) == 1) {
            CALL(death_cue(hs, a));                          /* 0021CFB0 */
            enter(a, 3, 0x3F);
        } else {
            enter(a, 1, 0x40);
        }
    } else {
        if (b(a, 4) == 1 && (b(a, 5) == 0x1D || b(a, 5) == 0x1E)) {   /* 0021CFF4 */
            CALL(face_link(hs, a));
            copy_2E0(a);
            /* 0021D07C: +228 >= 100 with D_008106F1 set picks 0x18. */
            int infected = !em_ee_c_lt_bits(w(a, 0x228), F100) && hs->stage->d8106F1 != 0;
            setb(a, 5, infected ? 0x18 : 0x17);
        } else {
            int infected = !em_ee_c_lt_bits(w(a, 0x228), F100) && hs->stage->d8106F1 != 0;
            setb(a, 5, infected ? 2 : 0);                    /* 0021D0C4 */
        }
        setb(a, 4, 2);
        setb(a, 6, 0);
        setb(a, 0x1F0, 0x3E);
    }
    *s = 0x81;
    return 0;
}

int em_player_stage_reaction(void *host, EmPlayerLiveActor *a, int *result)
{
    EmPlayerStageHost *hs = host;
    const EmPlayerStageCallees *c = ready(hs);
    if (!c || !reaction_callees(c) || !a || !result) return -1;
    *result = 0;
    if (le(w(a, 0x220), ZERO)) {                             /* 0021C45C */
        if (!is_zero(a, 0x224)) { setw(a, 0x224, ZERO); setb(a, 0, 2); }
        if (!is_zero(a, 0x22C)) { setw(a, 0x22C, ZERO); setb(a, 0, 2); }
        return 0;
    }
    if (b(a, 0x234) == 1 && !is_zero(a, 0x22C)) {            /* 0021C4C8 */
        setw(a, 0x22C, ZERO);
        if (b(a, 0xF) == 0 && is_zero(a, 0x224)) {
            setb(a, 0, 1);
            return 0;
        }
    }
    if (em_player_0021BB00(a)) {                             /* 0021C518 */
        if (b(a, 0xF) != 7) {
            setw(a, 0x224, ZERO);
            setw(a, 0x22C, ZERO);
            if (b(a, 0) & 2) setb(a, 0, 1);
            if (b(a, 0xF) == 2) setb(a, 0xF, 0);
        }
        return 0;
    }
    if (b(a, 0xF) & 0x80) return 0;                          /* 0021C57C */
    int s = 0;
    CALL(reaction_body(hs, a, &s));
    /* 0021D11C: the tail. */
    if (le(w(a, 0x220), F35)) {
        setb(a, 0x235, b(a, 0x235) & 0xFE);
        setb(a, 0x235, b(a, 0x235) | 1);
    }
    if (s & 0x80) CALL(reaction_reset(hs, a));               /* 0021D160 */
    if (s & 1) {
        setb(a, 0x2F2, 0);
        setb(a, 0x274, 0);
        seth(a, 0x276, 0);
        *result = 1;
    }
    return 0;
}

/* ---- 0015D100 / 0015D000 ------------------------------------------------- */

static void low_health_latch(EmPlayerLiveActor *a)
{
    setb(a, 0x235, b(a, 0x235) & 0xFE);
    setb(a, 0x235, b(a, 0x235) | 1);
}

int em_player_stage_drain(void *host, EmPlayerLiveActor *a)
{
    EmPlayerStageHost *hs = host;
    const EmPlayerStageCallees *c = ready(hs);
    if (!c || !c->w001F0060 || !c->w0015C9D0 || !a) return -1;
    int flag = hs->globals->d8106C8 & 4;                     /* 0015D110: 001B0070() & 4 */
    if (em_player_0021BB00(a)) return 0;                     /* 0015D11C */
    if (b(a, 0x234) == 0) {
        /* The hazard-room arm: area flag bit 2, D_008106C8 & 0x60, D_00810C7E 0. */
        if (flag != 4) return 0;                             /* 0015D13C */
        if (!(hs->globals->d8106C8 & 0x60)) return 0;        /* 0015D14C */
        if (hs->globals->d810C7E != 0) return 0;             /* 0015D160 */
        seth(a, 0x300, h(a, 0x300) + 1);
        if (h(a, 0x300) < 0x168) return 0;                   /* 0015D178 */
        seth(a, 0x300, 0);
        uint32_t health = w(a, 0x220);
        if (le(health, ONE)) {                               /* 0015D198 */
            setb(a, 0, 2);
            setw(a, 0x224, ONE);
            seth(a, 0x300, -0x8000);
            return 0;
        }
        health = em_ee_sub_bits(health, ONE);                /* 0015D1A4 */
        setw(a, 0x220, health);
        if (!le(health, F35) || (b(a, 0x235) & 1)) return 0; /* 0015D1D0 / 0015D1E8 */
        low_health_latch(a);
        return c->w0015C9D0(c->context, a) < 0 ? -1 : 0;     /* 0015D204 */
    }
    /* Infected: every 240 ticks, 2.0 of health and the effect 0x80000063. */
    seth(a, 0x2FC, h(a, 0x2FC) + 1);
    if (h(a, 0x2FC) < 0xF0) return 0;                        /* 0015D224 */
    seth(a, 0x2FC, 0);
    uint32_t health = w(a, 0x220);
    if (le(health, TWO)) {                                   /* 0015D244 */
        setb(a, 0, 2);
        setw(a, 0x224, TWO);
        setb(a, 0xF, 0x63);
        return 0;
    }
    setw(a, 0x220, em_ee_sub_bits(health, TWO));             /* 0015D250 / 0015D280 */
    CALL(c->w001F0060(c->context, UINT32_C(0x80000063), 0)); /* 0015D27C */
    if (!le(w(a, 0x220), F35) || (b(a, 0x235) & 1)) return 0;
    low_health_latch(a);
    return c->w0015C9D0(c->context, a) < 0 ? -1 : 0;         /* 0015D2C8 */
}

/* 0015D000: the heartbeat rumble. */
int em_player_stage_heartbeat(void *host, EmPlayerLiveActor *a)
{
    const EmPlayerStageCallees *c = ready(host);
    if (!c || !c->cue || !a) return -1;
    uint32_t health = w(a, 0x220);
    if (em_ee_c_eq_bits(health, ZERO)) return 0;             /* 0015D01C */
    int small, period;
    if (le(health, TEN)) { small = 0xE0; period = 0x3D; }    /* 0015D030 */
    else if (le(health, F35)) { small = 0xD0; period = 0x79; }   /* 0015D090 */
    else { seth(a, 0x210, 0); return 0; }                    /* 0015D0E4 */
    if (h(a, 0x210) == 0) CALL(c->cue(c->context, 0, small, 4, 0));   /* 0015D058 / 0015D0B8 */
    seth(a, 0x210, h(a, 0x210) + 1);
    if (h(a, 0x210) >= period) seth(a, 0x210, 0);            /* 0015D070 / 0015D0D0 */
    return 0;
}

/* ---- 00182B30 / 00182D70 / 00174A50 -------------------------------------- */

int em_player_stage_scripted_check(void *host, EmPlayerLiveActor *a, int *result)
{
    EmPlayerStageHost *hs = host;
    if (!ready(hs) || !a || !result) return -1;
    if (b(a, 4) == 5 && b(a, 5) == 1) { *result = 0; return 0; }   /* 00182B44 / 00182B54 */
    unsigned mode = b(a, 0x1F0);
    *result = le(w(a, 0x220), ZERO) || b(a, 0x25F) != 0 || em_player_0021BB00(a) ||
              hs->stage->d8106F1 != 0 || mode == 0x3C || mode == 0x3D;   /* 00182B70..00182BC0 */
    return 0;
}

int em_player_stage_scripted_notify(void *host, EmPlayerLiveActor *a)
{
    EmPlayerStageHost *hs = host;
    const EmPlayerStageCallees *c = ready(hs);
    if (!c || !c->link1C || !a) return -1;
    if (hs->stage->spad3B8F == 0) hs->stage->spad3B8F = 1;   /* 00182D78 */
    setw(a, 0x224, ZERO);
    setw(a, 0x22C, ZERO);
    setb(a, 0xF, 0);
    seth(a, 0x20E, 0);
    setb(a, 0, 1);
    uint32_t link = w(a, 0x1C);
    if (link != 0) CALL(c->link1C(c->context, link, 2));     /* 00182DB4 */
    em_live_set_u16(a, 0x1F2, em_live_u16(a, 0x20C));       /* 00182DC4 */
    setw(a, 0x1F8, ZERO);
    setb(a, 0x2F3, 0);
    setw(a, 0x1F4, ONE);
    setb(a, 0x23F, 0);
    setw(a, 0x38, ZERO);
    setw(a, 0x240, ZERO);
    setw(a, 0x24C, UINT32_C(0xFFFFFFFF));
    seth(a, 0x276, 0);
    return 0;
}

int em_player_stage_row_request(void *host, EmPlayerLiveActor *a, float blend)
{
    const EmPlayerStageCallees *c = ready(host);
    if (!c || !c->clip_lookup || !c->request || !a) return -1;
    int16_t clip;
    CALL(c->clip_lookup(c->context, a, 0, b(a, 0x235), 0, &clip));   /* 00174A70 */
    return c->request(c->context, a, clip, 0, blend) < 0 ? -1 : 0;  /* 00174A88 */
}

/* ---- 00183090 -------------------------------------------------------------- */

int em_player_stage_commit(void *host, EmPlayerLiveActor *a, int *result)
{
    EmPlayerStageHost *hs = host;
    const EmPlayerStageCallees *c = ready(hs);
    if (!c || !c->w001D0C70 || !c->bone_init || !c->clip_init || !a || !result) return -1;
    if (hs->stage->spad3B8F == 2) CALL(c->w001D0C70(c->context));   /* 001830A8 */
    uint8_t mode = b(a, 0x2F3);
    if (mode != 0) {                                         /* 001830BC */
        if (mode == 1 || mode == 3) {                        /* 001830C8 / 001830F0 */
            CALL(c->bone_init(c->context, a, h(a, 0x1F2)));
            setw(a, 0x200, ZERO);
            setb(a, 0x2F3, (uint8_t)(mode + 1));
        }
        *result = 1;
        return 0;
    }
    int16_t requested = h(a, 0x1F2);
    if (requested == h(a, 0x20C)) { *result = 1; return 0; } /* 00183120 */
    seth(a, 0x20C, requested);
    CALL(c->clip_init(c->context, a, h(a, 0x20C), em_ee_float(w(a, 0x1F8)), 0.0f));  /* 00183140 */
    setw(a, 0x200, ZERO);
    *result = 0;
    return 0;
}

/* ---- 001C64F0 anim_advance_time ------------------------------------------ */

int em_player_stage_anim_advance(void *host, EmPlayerLiveActor *a, float step_in, uint32_t *out)
{
    const EmPlayerStageCallees *c = ready(host);
    if (!c || !c->clip_resolve || !c->skeleton_frame || !c->w001C8710 || !c->w001C87C0 ||
        !c->sample_bones || !a || !out)
        return -1;
    uint32_t t = em_ee_bits(step_in);
    int16_t flags = 0;
    while (!le(t, ZERO)) {                                   /* 001C6524 / 001C6798 */
        uint32_t step = le(t, ONE) ? t : ONE;                /* 001C6548 bc1tl */
        EmPlayerClipHeader hd;
        CALL(c->clip_resolve(c->context, w(a, 0x40), h(a, 0x2C), &hd));   /* 001C6558 */
        t = em_ee_sub_bits(t, step);                         /* 001C655C */
        if (hd.events != 0 && !(h(a, 0x2C) & 0x8000)) {      /* 001C6568 / 001C6578 */
            for (int i = 0; i < hd.event_count; ++i) {       /* 001C65C8 */
                if (!hd.event_pairs) return -1;
                int16_t now = (int16_t)em_player_float_to_int(w(a, 0x3C));   /* 001C6594 */
                if (hd.event_pairs[2 * i] == now) {
                    flags = (int16_t)(uint16_t)((uint16_t)flags | (uint16_t)hd.event_pairs[2 * i + 1]);
                    break;
                }
            }
        }
        uint32_t clock = w(a, 0x3C);
        uint32_t less = em_ee_sub_bits(clock, step);         /* 001C65F4 (delay slot) */
        if (le(clock, ONE)) {                                /* 001C65E8 */
            int16_t id = h(a, 0x2C);
            if (id & 0x8000) {                               /* 001C65FC */
                seth(a, 0x2C, id & 0x7FFF);
                setw(a, 0x3C, em_ee_cvt_s_w_bits(hd.frames));   /* 001C6610..001C6640 */
                int16_t hold;
                CALL(c->skeleton_frame(c->context, w(a, 0x110), &hold));   /* +8E */
                uint32_t held = em_ee_cvt_s_w_bits((uint32_t)(int32_t)hold);
                setw(a, 0x3C, em_ee_sub_bits(w(a, 0x3C), held));   /* 001C6660 */
                CALL(c->w001C8710(c->context, a, b(a, 0xC), em_ee_float(held)));   /* 001C6678 */
            } else if (hd.next == -2) {                      /* 001C6690 */
                flags = (int16_t)(flags | 0x1000);
            } else if (hd.next == -1) {                      /* 001C669C */
                flags = (int16_t)(flags | 0x3000);
                setw(a, 0x3C, em_ee_cvt_s_w_bits(hd.frames));   /* 001C66F8..001C6734 */
                CALL(c->w001C8710(c->context, a, b(a, 0xC), 0.0f));   /* 001C6740 */
            } else {
                seth(a, 0x2C, hd.next | 0x8000);             /* 001C66A8 */
                flags = (int16_t)(flags | 0x4000);
                setw(a, 0x3C, em_ee_cvt_s_w_bits((uint32_t)(int32_t)hd.start));   /* 001C66C4 */
                EmPlayerClipHeader follow;
                CALL(c->clip_resolve(c->context, w(a, 0x40), h(a, 0x2C), &follow));   /* 001C66D4 */
                CALL(c->sample_bones(c->context, a, b(a, 0xC), 0.0f, em_ee_float(w(a, 0x3C))));
            }
        } else {
            setw(a, 0x3C, less);                             /* 001C6760 */
            CALL(c->w001C87C0(c->context, a, b(a, 0xC), em_ee_float(step)));   /* 001C6768 */
            if (h(a, 0x2C) & 0x8000) flags = (int16_t)(uint16_t)((uint16_t)flags | 0x8000u);   /* 001C6780 */
        }
    }
    *out = (uint32_t)(int32_t)flags;                         /* 0015BA50: sw v0, +200 */
    return 0;
}

/* ---- 0011A070 ------------------------------------------------------------------ */

/* 0011A070(arg): track = arg & 0x7FFF, hard = arg & 0x8000; the body is
 * em_sfx_driver_stop (em_sfx_bank.c), reached through the sound_stop worker.
 * 0015BCF0 passes the sign-extended +31B byte. */
int em_player_stage_stop_sound(void *host, int handle)
{
    const EmPlayerStageCallees *c = ready(host);
    if (!c || !c->sound_stop) return -1;
    return c->sound_stop(c->context, handle & 0x7FFF, (handle & 0x8000) != 0) < 0 ? -1 : 0;
}

/* ---- binding --------------------------------------------------------------------- */

void em_player_stage_workers_bind(EmPlayerStageWorkers *wk, EmPlayerStageHost *host)
{
    if (!wk) return;
    wk->context = host;
    wk->clip_rate = em_player_stage_clip_rate;
    wk->advance = em_player_stage_anim_advance;
    wk->commit = em_player_stage_commit;
    wk->reaction = em_player_stage_reaction;
    wk->drain = em_player_stage_drain;
    wk->heartbeat = em_player_stage_heartbeat;
    wk->scripted_check = em_player_stage_scripted_check;
    wk->scripted_notify = em_player_stage_scripted_notify;
    wk->row_request = em_player_stage_row_request;
    wk->stop_sound = em_player_stage_stop_sound;
}

/* ---- 0015B530 ------------------------------------------------------------------- */

int em_player_stage_0015B530(void *context, EmPlayerLiveActor *a)
{
    const EmPlayerStageMajor4 *m = context;
    if (!m || !m->stage || !a) return -1;
    for (int i = 0; i < EM_PLAYER_MAJOR4_COUNT; ++i)
        if (!m->routine[i]) return -1;
    int which;
    if (m->stage->spad3B8D == 0) {                           /* 0015B540 */
        which = EM_PLAYER_MAJOR4_00182DF0;
    } else {
        switch (b(a, 5)) {                                   /* 0015B558.. */
        case 0xC: which = EM_PLAYER_MAJOR4_001838B0; break;
        case 8: which = EM_PLAYER_MAJOR4_00163B40; break;
        case 5: which = EM_PLAYER_MAJOR4_00162DB0; break;
        case 1: which = EM_PLAYER_MAJOR4_001837B0; break;
        case 0x17: which = EM_PLAYER_MAJOR4_00183910; break;
        case 0: which = EM_PLAYER_MAJOR4_001837A0; break;
        default: return 0;
        }
    }
    return m->routine[which](m->routine_context[which], a) < 0 ? -1 : 0;
}
