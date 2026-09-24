/* em_startup_load_gaps_sound.c - the sound originals of census lane L34
 * (see em_startup_load_gaps.h, docs/STARTUP_LOAD_GAPS.md):
 *
 *   001FB100  per-frame output-mode commit + config copy (byte-matched)
 *   001FC6E0  the ten delayed cues (byte-matched)
 *   001FB370  the bank-load gate (byte-matched)
 *   001FB3E0  the bank upload state machine (NEARMISS; read against the
 *             split listing, jump table jtbl_0026EBF0)
 *   001FB910  the SIF DMA kick (byte-matched)
 *
 * Every original address a branch or store comes from is cited beside it. */
#include "game/em_startup_load_gaps.h"

#include <string.h>

#define CALL(expr) do { if ((expr) < 0) return -1; } while (0)

/* 32-bit wrapping add, as the EE addu/addiu computes it. */
static int32_t add32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }

/* ====================================================================== */
/* 001FC6E0 / 001FB100                                                    */
/* ====================================================================== */

/* 001FC6E0: for each of the ten records {delay, cue, a2, a3} at
 * D_00281F30: a nonzero delay counts down; at zero a cue other than -1 is
 * started with 001FB9F0(cue, 0x1000, a2, a3) (unless it is 0) and set to -1. */
int em_slg_001FC6E0(const EmSlgSoundWorkers *w, EmSlgSoundFrame *s)
{
    if (!w || !s || !w->w_001FB9F0) return -1;
    for (int i = 0; i < EM_SLG_CUES; i++) {
        int32_t *p = s->d281F30[i];
        if (p[0] != 0) {
            p[0] = add32(p[0], -1);
        } else if (p[1] != -1) {
            if (p[1] != 0) CALL(w->w_001FB9F0(w->ctx, p[1], 0x1000, p[2], p[3]));
            p[1] = -1;
        }
    }
    return 0;
}

int em_slg_001FB100(const EmSlgSoundWorkers *w, EmSlgSoundFrame *s)
{
    if (!w || !s || !w->w_001F9CF0 || !w->w_00119870 || !w->w_0011A608 || !w->w_001FB9F0)
        return -1;
    if (s->d821058 == 1) return 0;                                 /* the movie driver's frame */
    CALL(w->w_001F9CF0(w->ctx, s->d821058));
    if (s->d28215B != s->d81011C) {
        s->d28215B = s->d81011C;
        uint64_t m0 = (uint64_t)1 << (s->d281FD4 & 63);            /* 1LL << D_00281FD4 */
        uint64_t m1 = (uint64_t)1 << (s->d2820F4 & 63);            /* 1LL << D_002820F4 */
        if (s->d81011C == 0) {
            CALL(w->w_00119870(w->ctx, 0));
            CALL(w->w_0011A608(w->ctx, m0, 0x3FFF, 0));
            CALL(w->w_0011A608(w->ctx, m1, 0, 0x3FFF));
        } else {
            CALL(w->w_00119870(w->ctx, 1));
            CALL(w->w_0011A608(w->ctx, m0, 0x3000, 0x3000));
            CALL(w->w_0011A608(w->ctx, m1, 0x3000, 0x3000));
        }
    }
    memcpy(s->d281B70 + 0xC0, s->d281B70, 0xC0);                   /* block_copy(D_00281C30, D_00281B70, 0xC0) */
    return em_slg_001FC6E0(w, s);
}

/* ====================================================================== */
/* 001FB910                                                               */
/* ====================================================================== */

static int bank_bound(const EmSlgBankWorkers *w)
{
    return w && w->w_001195A8 && w->w_0010F8F8 && w->w_00119450 && w->w_001194B8 &&
           w->w_001199F0 && w->w_0010F968 && w->w_0010BC00 && w->w_0010BAA0 &&
           w->w_0010BBE0 && w->w_0010BBC0;
}

int em_slg_001FB910(const EmSlgBankWorkers *w, EmSlgBankLoad *s, uint32_t src,
                    int32_t dst, int32_t size, int32_t *ret)
{
    if (!bank_bound(w) || !s || !ret) return -1;
    if (s->d28215C != 1) {
        if (s->d28215C != 0) { *ret = 0; return 0; }               /* 001FB944 */
        s->d28215C = 1;
        CALL(w->w_0010BC00(w->ctx));
        uint32_t desc[4] = { src, (uint32_t)dst, (uint32_t)size, 0 };
        CALL(w->w_0010BAA0(w->ctx, 0));
        int32_t id;
        CALL(w->w_0010BBE0(w->ctx, desc, 1, &id));
        s->d2821A0 = id;
    }
    if (s->d2821A0 == 0) { *ret = -1; return 0; }                  /* 001FB98C */
    int32_t st;
    CALL(w->w_0010BBC0(w->ctx, s->d2821A0, &st));
    if (st >= 0) { *ret = 1; return 0; }                           /* still transferring */
    CALL(w->w_0010BAA0(w->ctx, 0));
    *ret = 0;
    return 0;
}

/* ====================================================================== */
/* 001FB3E0 / 001FB370                                                    */
/* ====================================================================== */

/* A word of the bank file at EE address `at`; outside the file faults. */
static int file_word(const EmSlgBankFile *f, uint32_t at, int32_t *v)
{
    uint32_t off = at - f->address;
    if (at < f->address || off > f->size || f->size - off < 4) return -1;
    const uint8_t *p = f->bytes + off;
    *v = (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
                   (uint32_t)p[3] << 24);
    return 0;
}

/* D_00281D50 + bucket * 0x50 + index * 4, as a word index; outside
 * D_00281D50..D_00281F2F faults. */
static int handle_at(const EmSlgBankLoad *s, int32_t index, int32_t **out)
{
    int64_t at = (int64_t)s->d282190 * 20 + index;
    if (at < 0 || at >= EM_SLG_BANK_HANDLE_WORDS) return -1;
    *out = (int32_t *)&s->d281D50[at];
    return 0;
}

static int count_at(EmSlgBankLoad *s, int32_t **out)
{
    if (s->d282190 < 0 || s->d282190 >= EM_SLG_BANK_COUNTS) return -1;
    *out = &s->d281D30[s->d282190];
    return 0;
}

int em_slg_001FB3E0(const EmSlgBankWorkers *w, EmSlgBankLoad *s, const EmSlgBankFile *f,
                    uint32_t *ret)
{
    if (!bank_bound(w) || !s || !f || !f->bytes || !ret) return -1;
    const uint32_t a0 = f->address;
    const uint32_t q = s->d2821A4;                                 /* the entry cursor, read once */
    int32_t v, r, *count, *slot;
    *ret = 0;
    switch (s->d282151) {                                          /* jtbl_0026EBF0, 7 cases */
    case 0: {
        int32_t entries, offset;
        CALL(file_word(f, a0 + 8, &entries));
        CALL(file_word(f, a0 + 0x18, &offset));
        s->d282151 = (int8_t)(s->d282151 + 1);
        s->d282159 = 0;
        s->d282190 = 0x63;
        s->d2821A4 = a0 + 0x20 + (uint32_t)entries * 16;
        s->d28219C = a0 + (uint32_t)offset;
        break;
    }
    case 1: {
        int32_t bucket, size;
        CALL(file_word(f, q + 8, &bucket));
        if (bucket != s->d282190) {
            s->d282190 = bucket;
            CALL(count_at(s, &count));
            for (int32_t i = 0; i < *count; i++) {
                CALL(handle_at(s, i, &slot));
                CALL(w->w_001195A8(w->ctx, *slot));
                CALL(count_at(s, &count));                         /* re-read every pass */
            }
            *count = 0;
            if (bucket < 0 || bucket >= EM_SLG_BANK_BUCKETS) return -1;   /* the 5-word D_00264890 */
            s->d282198 = s->base[bucket];
        }
        CALL(file_word(f, q, &size));
        CALL(w->w_0010F8F8(w->ctx, add32(size, 0x10), &v));
        s->d282194 = v;
        s->d282151 = (int8_t)(s->d282151 + 1);
        s->d28215C = 0;
        break;
    }
    case 2: {
        int32_t size;
        CALL(file_word(f, q, &size));
        CALL(em_slg_001FB910(w, s, s->d28219C, s->d282194 & ~0xF, size, &r));
        if (r == 0) s->d282151 = (int8_t)(s->d282151 + 1);
        break;
    }
    case 3:
        if (s->d275B1C != 0) {
            s->d282151 = 4;
        } else {
            CALL(w->w_00119450(w->ctx, 0, &r));
            if (r != 0) s->d282151 = (int8_t)(s->d282151 + 1);
        }
        break;
    case 4: {
        int32_t dest;
        if (s->d282198 & 0x3F) s->d282198 = add32(s->d282198, 0x40) & ~0x3F;
        CALL(file_word(f, q + 4, &dest));
        CALL(w->w_001194B8(w->ctx, s->d282194 & ~0xF, a0 + (uint32_t)dest, s->d282198, &r));
        CALL(count_at(s, &count));
        CALL(handle_at(s, *count, &slot));
        *slot = r;
        if (*slot == -1) break;
        s->d275B18 = 0x50;
        s->d282151 = (int8_t)(s->d282151 + 1);
    }
        /* fall through */
    case 5:
        if (s->d275B1C != 0) {
            if (s->d275B18 != 0) {
                s->d275B18 = add32(s->d275B18, -1);
            } else {
                s->d282151 = (int8_t)(s->d282151 + 1);
                CALL(w->w_00119450(w->ctx, 0, &r));
                if (r == 1) s->d275B1C = 0;
            }
        } else {
            CALL(w->w_00119450(w->ctx, 0, &r));
            if (r == 1) {
                s->d282151 = (int8_t)(s->d282151 + 1);
            } else if (r < 0) {
                s->d282151 = 4;
                CALL(count_at(s, &count));
                CALL(handle_at(s, *count, &slot));
                CALL(w->w_001195A8(w->ctx, *slot));
            } else if (s->d275B18 != 0) {
                s->d275B18 = add32(s->d275B18, -1);
            } else {
                s->d275B1C = 1;
                s->d282151 = (int8_t)(s->d282151 + 1);
            }
        }
        break;
    case 6: {
        int32_t size;
        s->d282151 = 1;
        CALL(count_at(s, &count));
        CALL(handle_at(s, *count, &slot));
        CALL(w->w_001199F0(w->ctx, *slot, 0x64));
        CALL(w->w_0010F968(w->ctx, s->d282194));
        CALL(file_word(f, q, &size));
        s->d282198 = add32(s->d282198, size);
        s->d28219C += (uint32_t)size;
        CALL(count_at(s, &count));
        *count = add32(*count, 1);
        s->d2821A4 = q + 0x10;
        s->d282159 = (int8_t)(s->d282159 + 1);
        break;
    }
    default:
        break;
    }
    int32_t total, payload;                                        /* 001FB8A4: the epilogue */
    CALL(file_word(f, a0 + 0xC, &total));
    if ((uint32_t)(int32_t)s->d282159 < (uint32_t)total) return 0;
    CALL(file_word(f, a0 + 0x10, &payload));
    s->d282150 = 0;
    s->d282151 = 0;
    *ret = (a0 + (uint32_t)payload + 0x40) & ~(uint32_t)0x3F;
    return 0;
}

int em_slg_001FB370(const EmSlgBankWorkers *w, EmSlgBankLoad *s, const EmSlgBankFile *f,
                    uint32_t *ret)
{
    if (!bank_bound(w) || !s || !f || !ret) return -1;
    switch (s->d282150) {
    case 0:
        s->d282150 = (int8_t)(s->d282150 + 1);
        /* fall through */
    case 1:
        return em_slg_001FB3E0(w, s, f, ret);
    default:
        s->d282150 = 0;
        s->d282151 = 0;
        *ret = 0;
        return 0;
    }
}
