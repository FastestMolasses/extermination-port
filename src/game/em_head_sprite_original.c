/* Head sprite effect: 001E2560 and its helpers (see em_head_sprite_original.h
 * and docs/HEAD_SPRITE_ORIGINAL.md). Addresses in comments are original
 * runtime addresses; constants are the original bit patterns. */
#include "game/em_head_sprite_original.h"

#include <string.h>

#define HS_RAMP_STEP 0x3CA3D70Au   /* 0.02f: 001E2560 ramp step (EE add) */
#define HS_RAMP_END 0x3FC00000u    /* 1.5f: compare bound and clamp store */
#define HS_RAND_SCALE 0x4F000000u  /* 2^31: the RNG divisor (an immediate) */
#define HS_WAIT_MOD 40             /* 0x28: rand % 40 */
#define HS_WAIT_BASE 60            /* 0x3C: + 60 */
#define HS_ONE 0x3F800000u         /* 1.0f: 001E23A0 +0xAC, 001CFA60 +0x48 */
#define HS_XF50 0x358637BDu        /* 001CFA60 +0x50 */
#define HS_END_FIELD 0x004F35C0u   /* D_004F35C0: D_00810E80[0] == 0 */
#define HS_END_OTHER 0x005635C0u   /* D_005635C0: otherwise */
#define HS_ROWS 0x00251260u        /* D_00251260 */
#define HS_TABLE_2535F0 0x002535F0u

/* ------------------------------------------------------------ EE model ---
 * docs/EE_FLOAT_MODEL.md section 2 (tools/ee_float_model.py): DAZ operands,
 * add/sub pre-trim + truncation, RN division, FTZ, saturation to +-MAX. */
#define EE_SIGN 0x80000000u
#define EE_MAX 0x7F7FFFFFu

static unsigned ee_exp(uint32_t b) { return (b >> 23) & 0xFFu; }
static int ee_is_nan(uint32_t b) { return ee_exp(b) == 0xFFu && (b & 0x7FFFFFu) != 0; }
static int ee_is_inf(uint32_t b) { return (b & 0x7FFFFFFFu) == 0x7F800000u; }
static uint32_t ee_daz(uint32_t b) { return ee_exp(b) == 0 ? (b & EE_SIGN) : b; }

static int ee_bitlen(uint64_t v)
{
    int n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

/* sign * mag * 2^exp to binary32 (mag > 0): truncation or nearest-even
 * (`below` marks a nonzero remainder under mag's last bit); flush below the
 * smallest normal to a signed zero, saturate above the largest finite. */
static uint32_t ee_pack(uint32_t sign, uint64_t mag, int exp, int nearest, int below)
{
    int shift = ee_bitlen(mag) - 24;
    uint64_t kept;
    if (shift > 0) {
        uint64_t rest = mag & ((UINT64_C(1) << shift) - 1);
        kept = mag >> shift;
        if (nearest) {
            uint64_t half = UINT64_C(1) << (shift - 1);
            if (rest > half || (rest == half && (below || (kept & 1))))
                kept++;
        }
    } else {
        kept = mag << -shift;
    }
    exp += shift;
    if (kept >> 24) {
        kept >>= 1;
        exp++;
    }
    int biased = exp + 150;
    if (biased <= 0)
        return sign << 31;
    if (biased >= 0xFF)
        return (sign << 31) | EE_MAX;
    return (sign << 31) | ((uint32_t)biased << 23) | (uint32_t)(kept & 0x7FFFFFu);
}

static uint32_t ee_trim(uint32_t b, unsigned distance)
{
    if (distance >= 25)
        return b & EE_SIGN;
    return b & (UINT32_MAX << (distance - 1));
}

static uint32_t ee_exact_sum(uint32_t a, uint32_t b)
{
    uint32_t ma = ee_exp(a) ? ((a & 0x7FFFFFu) | 0x800000u) : 0;
    uint32_t mb = ee_exp(b) ? ((b & 0x7FFFFFu) | 0x800000u) : 0;
    if (!ma && !mb)
        return ((a & b) & EE_SIGN) ? EE_SIGN : 0;
    if (!ma)
        return b;
    if (!mb)
        return a;
    int ea = (int)ee_exp(a) - 150, eb = (int)ee_exp(b) - 150;
    int base = ea < eb ? ea : eb;
    /* After the pre-trim the exponent distance is at most 24 here. */
    int64_t va = (int64_t)((uint64_t)ma << (ea - base));
    int64_t vb = (int64_t)((uint64_t)mb << (eb - base));
    int64_t total = ((a & EE_SIGN) ? -va : va) + ((b & EE_SIGN) ? -vb : vb);
    if (total == 0)
        return 0;
    return ee_pack(total < 0, (uint64_t)(total < 0 ? -total : total), base, 0, 0);
}

uint32_t em_head_sprite_ee_add(uint32_t a, uint32_t b)
{
    a = ee_daz(a);
    b = ee_daz(b);
    if (ee_exp(a) == 0xFFu || ee_exp(b) == 0xFFu) {
        if (ee_is_nan(a) || ee_is_nan(b))
            return EE_MAX;
        if (ee_is_inf(a) && ee_is_inf(b))
            return ((a ^ b) & EE_SIGN) ? EE_MAX : ((a & EE_SIGN) | EE_MAX);
        return ((ee_is_inf(a) ? a : b) & EE_SIGN) | EE_MAX;
    }
    int d = (int)ee_exp(a) - (int)ee_exp(b);
    if (d > 0)
        b = ee_trim(b, (unsigned)d);
    else if (d < 0)
        a = ee_trim(a, (unsigned)-d);
    return ee_exact_sum(a, b);
}

static int64_t ee_compare_key(uint32_t b)
{
    b = ee_daz(b);
    if (ee_exp(b) == 0xFFu)
        b = (b & EE_SIGN) | EE_MAX;
    return (b & EE_SIGN) ? -(int64_t)(b & 0x7FFFFFFFu) : (int64_t)b;
}

int em_head_sprite_ee_c_le(uint32_t a, uint32_t b)
{
    return ee_compare_key(a) <= ee_compare_key(b);
}

uint32_t em_head_sprite_ee_cvt_s_w(int32_t value)
{
    if (value == 0)
        return 0;
    uint32_t sign = value < 0;
    uint64_t mag = sign ? (uint64_t)(-(int64_t)value) : (uint64_t)value;
    return ee_pack(sign, mag, 0, 0, 0);
}

uint32_t em_head_sprite_ee_div(uint32_t a, uint32_t b)
{
    a = ee_daz(a);
    b = ee_daz(b);
    uint32_t sign = (a ^ b) >> 31;
    if (ee_exp(b) == 0)
        return (sign << 31) | EE_MAX;
    if (ee_exp(a) == 0xFFu || ee_exp(b) == 0xFFu) {
        if (ee_is_nan(a) || ee_is_nan(b) || (ee_is_inf(a) && ee_is_inf(b)))
            return EE_MAX;
        return ee_is_inf(a) ? ((sign << 31) | EE_MAX) : (sign << 31);
    }
    if (ee_exp(a) == 0)
        return sign << 31;
    uint64_t ma = (a & 0x7FFFFFu) | 0x800000u, mb = (b & 0x7FFFFFu) | 0x800000u;
    int ea = (int)ee_exp(a) - 150, eb = (int)ee_exp(b) - 150;
    /* 26 extra quotient bits: >= 2 bits below the kept 24 plus the sticky
     * remainder, which is all nearest-even needs. */
    uint64_t num = ma << 26;
    return ee_pack(sign, num / mb, ea - eb - 26, 1, (num % mb) != 0);
}

/* --------------------------------------------------------------- helpers */

static int hs_fault(EmHeadSpriteOriginalFault *fault, uint32_t address, int32_t code)
{
    fault->address = address;
    fault->code = code;
    return -1;
}

static int hs_leave(EmHeadSpriteOriginalFault *fault, uint32_t callee, int result)
{
    return result < 0 ? hs_fault(fault, callee, EM_HEAD_SPRITE_FAULT_WORKER_FAILED) : 0;
}

#define HS_NEED(w, name, address)                                                  \
    do {                                                                           \
        if (!(w) || !(w)->name)                                                    \
            return hs_fault(fault, (address), EM_HEAD_SPRITE_FAULT_NULL_WORKER);   \
    } while (0)

static uint32_t hs_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

static float hs_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

static uint32_t hs_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void hs_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* ------------------------------------------------------------ tables */

int em_head_sprite_original_load_tables(const uint8_t *elf, size_t size,
                                        EmHeadSpriteOriginalTables *out)
{
    static const uint8_t magic[4] = {0x7F, 'E', 'L', 'F'};
    if (!elf || !out || size != 1532624u || memcmp(elf, magic, 4) != 0)
        return -1;
#define HS_FILE(address) ((size_t)(address) - 0x100000u + 0x300u)
    memcpy(out->d2535F0, elf + HS_FILE(HS_TABLE_2535F0), sizeof out->d2535F0);
    memcpy(out->d253670, elf + HS_FILE(EM_HEAD_SPRITE_ORIGINAL_SOURCE), sizeof out->d253670);
    memcpy(out->d251260, elf + HS_FILE(HS_ROWS), sizeof out->d251260);
#undef HS_FILE
    return 0;
}

/* ------------------------------------------------------------ 001E2290 */

int em_head_sprite_original_001E2290(int32_t key)
{
    switch (key) {
    case 0x3B: case 0x3D: case 0x3E: case 0x3F: case 0x40:
    case 0x47: case 0x48: case 0x49:
    case 0x4E: case 0x4F: case 0x50: case 0x51:
    case 0x54: case 0x55:
    case 0x58: case 0x59: case 0x5A:
    case 0x61: case 0x68: case 0x6A:
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------ 001E23A0 */

int em_head_sprite_original_001E23A0(EmHeadSpriteOriginal *record,
                                     const EmHeadSpriteOriginalTables *tables,
                                     EmHeadSpriteOriginalFault *fault)
{
    if (fault->code)
        return -1;
    int idx;
    switch (record->key) { /* key byte +0x0D */
    case 0x3B: case 0x3D: case 0x3E: case 0x3F: case 0x40: idx = 0; break;
    case 0x47: case 0x48: case 0x49: idx = 1; break;
    case 0x4E: case 0x4F: case 0x50: case 0x51: idx = 2; break;
    case 0x54: case 0x55: idx = 3; break;
    case 0x58: case 0x59: case 0x5A: idx = 4; break;
    case 0x61: idx = 5; break;
    case 0x68: idx = 6; break;
    case 0x6A: idx = 7; break;
    default: return 1;
    }
    if (!tables)
        return hs_fault(fault, HS_TABLE_2535F0, EM_HEAD_SPRITE_FAULT_NULL_WORKER);
    const uint8_t *e = tables->d2535F0[idx];
    record->local[0] = hs_float(hs_le32(e + 4)); /* float moves: raw bits kept */
    record->local[1] = hs_float(hs_le32(e + 8));
    record->local[2] = hs_float(hs_le32(e + 12));
    record->local[3] = hs_float(HS_ONE);         /* +0xAC = 1.0 (integer store) */
    record->bone = (int32_t)hs_le32(e);          /* entry word -> +0x28 */
    return 0;
}

/* ------------------------------------------------------------ 001F0120 */

int em_head_sprite_original_spawn_001F0120(uint32_t owner14, int32_t key,
                                           const EmHeadSpriteOriginalWorkers *w,
                                           EmHeadSpriteOriginal **record,
                                           EmHeadSpriteOriginalFault *fault)
{
    if (fault->code)
        return -1;
    *record = NULL;
    if (!em_head_sprite_original_001E2290(key))
        return 0;
    HS_NEED(w, w_001EF9D0, 0x001EF9D0u);
    EmHeadSpriteOriginal *e = NULL;
    if (hs_leave(fault, 0x001EF9D0u,
                 w->w_001EF9D0(w->ctx, EM_HEAD_SPRITE_ORIGINAL_HANDLE, 0, 1.0f, &e)) < 0)
        return -1;
    if (e) {
        e->key = (uint8_t)key; /* +0x0D = key (low byte) */
        e->owner = owner14;    /* +0x24 = owner +0x14 */
    }
    *record = e;
    return 0;
}

/* ------------------------------------------------------------ 001CFA60 */

int em_head_sprite_original_001CFA60(EmHeadSpriteOriginalXf *xf, const float src[16],
                                     uint32_t f12, uint32_t f13,
                                     const EmHeadSpriteOriginalWorkers *w,
                                     EmHeadSpriteOriginalFault *fault)
{
    if (fault->code)
        return -1;
    xf->w44 = f12;
    xf->w4C = f13;
    xf->w48 = HS_ONE;
    xf->w50 = HS_XF50;
    xf->w54 = 0;
    HS_NEED(w, w_001CD370, 0x001CD370u);
    uint32_t address = 0;
    const uint8_t *bytes = NULL;
    if (hs_leave(fault, 0x001CD370u, w->w_001CD370(w->ctx, 0, &address, &bytes)) < 0)
        return -1;
    xf->m40 = address;
    xf->m40_bytes = bytes;
    memcpy(xf->q, src, 64); /* copied as four quadwords */
    return 0;
}

/* ------------------------------------------------------------ 001CFBE0 */

static int hs_open(const EmHeadSpriteOriginalWorkers *w, int32_t id, int32_t count, uint8_t **out,
                   EmHeadSpriteOriginalFault *fault)
{
    HS_NEED(w, w_001CB5F0, 0x001CB5F0u);
    *out = NULL;
    if (hs_leave(fault, 0x001CB5F0u, w->w_001CB5F0(w->ctx, EM_HEAD_SPRITE_ORIGINAL_CHAIN, id, count, out)) < 0)
        return -1;
    if (!*out)
        return hs_fault(fault, 0x001CB5F0u, EM_HEAD_SPRITE_FAULT_NULL_WORKER);
    return 0;
}

int em_head_sprite_original_001CFBE0(int32_t id, uint32_t kind,
                                     const EmHeadSpriteOriginalSource *st,
                                     const EmHeadSpriteOriginalXf *xf, int32_t copy,
                                     const EmHeadSpriteOriginalWorld *world,
                                     const EmHeadSpriteOriginalWorkers *w,
                                     EmHeadSpriteOriginalFault *fault)
{
    if (fault->code)
        return -1;
    if (!world)
        return hs_fault(fault, 0x00810E80u, EM_HEAD_SPRITE_FAULT_NULL_WORKER);
    /* Free-space guard: signed (end - cursor) >= 0x8000. */
    uint32_t end = world->d810E80 == 0 ? HS_END_FIELD : HS_END_OTHER;
    if ((int32_t)(end - world->cursor) < 0x8000)
        return 0;
    if (!st || !st->bytes)
        return hs_fault(fault, st ? st->address : 0x001CFBE0u, EM_HEAD_SPRITE_FAULT_NULL_WORKER);

    int32_t mode = (int32_t)hs_le32(st->bytes + 0x8C);
    int idx = -1;
    uint32_t tbl = 0;
    if (mode == 1) { /* jtbl_0026E390 */
        switch (kind) {
        case 0: idx = 0; tbl = 0x00230800u; break;
        case 1: idx = 2; tbl = 0x00231770u; break;
        case 6: idx = 2; tbl = 0x0023D930u; break;
        case 2: idx = 4; tbl = 0x00232540u; break;
        case 3: idx = 2; tbl = 0x00233800u; break;
        case 5: idx = 6; tbl = 0x00231770u; break;
        case 4: idx = 7; tbl = 0x00230800u; break;
        default: break;
        }
    } else if (mode >= 2 && mode <= 4) { /* jtbl_0026E370 */
        switch (kind) {
        case 0: idx = 1; tbl = 0x00230800u; break;
        case 1: idx = 3; tbl = 0x00231770u; break;
        case 6: idx = 3; tbl = 0x0023D930u; break;
        case 2: idx = 5; tbl = 0x00232540u; break;
        case 3: idx = 3; tbl = 0x00233800u; break;
        case 5: idx = 6; tbl = 0x00231770u; break;
        case 4: idx = 7; tbl = 0x00230800u; break;
        default: break;
        }
    }
    if (idx < 0) /* the original then uses whatever s0/s1 held */
        return hs_fault(fault, 0x001CFBE0u, EM_HEAD_SPRITE_FAULT_UNDEFINED);
    if (!world->tables)
        return hs_fault(fault, HS_ROWS, EM_HEAD_SPRITE_FAULT_NULL_WORKER);
    if (!world->scratch_3A40)
        return hs_fault(fault, 0x70003A40u, EM_HEAD_SPRITE_FAULT_NULL_WORKER);
    if (!world->scratch_3AC0)
        return hs_fault(fault, 0x70003AC0u, EM_HEAD_SPRITE_FAULT_NULL_WORKER);
    if (!world->ctx_A0)
        return hs_fault(fault, 0x00275670u, EM_HEAD_SPRITE_FAULT_NULL_WORKER);
    if (!xf->m40_bytes)
        return hs_fault(fault, xf->m40, EM_HEAD_SPRITE_FAULT_NULL_WORKER);

    /* Packet 1: 7 quadwords. */
    uint8_t *r;
    if (hs_open(w, id, 7, &r, fault) < 0)
        return -1;
    memset(r, 0, 16);
    hs_put32(r + 0xC, 0x6C050059u);
    hs_put32(r + 0x10, xf->w44);
    hs_put32(r + 0x14, xf->w48);
    hs_put32(r + 0x18, xf->w50);
    hs_put32(r + 0x1C, xf->w4C);
    memcpy(r + 0x20, xf->q, 64);
    memset(r + 0x60, 0, 16);
    hs_put32(r + 0x60, 0x14000000u);
    hs_put32(r + 0x64, 0x11000000u);

    /* Packet 2: nine quadwords of `st`, copied or referenced. */
    if (copy != 0) {
        uint8_t *d;
        if (hs_open(w, id, 9, &d, fault) < 0)
            return -1;
        memcpy(d, st->bytes, 9 * 16);
    } else {
        HS_NEED(w, w_001CB6B0, 0x001CB6B0u);
        if (hs_leave(fault, 0x001CB6B0u,
                     w->w_001CB6B0(w->ctx, EM_HEAD_SPRITE_ORIGINAL_CHAIN, id, 9, st->address)) < 0)
            return -1;
    }

    /* Packet 3: GIFtag only. */
    if (hs_open(w, id, 1, &r, fault) < 0)
        return -1;
    memset(r, 0, 16);
    hs_put32(r + 0xC, 0x6C090050u);

    /* Packet 4: 16 quadwords. */
    if (hs_open(w, id, 0x10, &r, fault) < 0)
        return -1;
    memset(r, 0, 16);
    hs_put32(r + 0x8, 0x01000101u);
    hs_put32(r + 0xC, 0x6C0F006Eu);
    memcpy(r + 0x10, world->scratch_3A40, 64);
    memcpy(r + 0x50, xf->m40_bytes, 64);
    memcpy(r + 0x90, world->scratch_3AC0, 64);
    memcpy(r + 0xD0, world->ctx_A0, 16);
    hs_put32(r + 0xE0, 0);
    hs_put32(r + 0xE4, 0);
    hs_put32(r + 0xE8, xf->w54);
    hs_put32(r + 0xEC, 0);
    memcpy(r + 0xF0, world->tables->d251260[idx], 16);

    HS_NEED(w, w_001CB760, 0x001CB760u);
    if (hs_leave(fault, 0x001CB760u,
                 w->w_001CB760(w->ctx, EM_HEAD_SPRITE_ORIGINAL_CHAIN, id, tbl,
                               HS_ROWS + (uint32_t)idx * 0x10u)) < 0)
        return -1;
    HS_NEED(w, w_001CB900, 0x001CB900u);
    if (hs_leave(fault, 0x001CB900u, w->w_001CB900(w->ctx, EM_HEAD_SPRITE_ORIGINAL_CHAIN, id, mode)) < 0)
        return -1;
    return 1;
}

/* ------------------------------------------------------------ 001E2560 */

static int hs_rand_wait(const EmHeadSpriteOriginalWorkers *w, int32_t *timer,
                        EmHeadSpriteOriginalFault *fault)
{
    HS_NEED(w, w_00122BB8, 0x00122BB8u);
    int32_t value = 0;
    if (hs_leave(fault, 0x00122BB8u, w->w_00122BB8(w->ctx, &value)) < 0)
        return -1;
    *timer = value % HS_WAIT_MOD + HS_WAIT_BASE; /* signed remainder, + 60 */
    return 0;
}

static int hs_ramp_emit(EmHeadSpriteOriginal *e, const EmHeadSpriteOriginalOwner *owner,
                        const EmHeadSpriteOriginalWorld *world,
                        const EmHeadSpriteOriginalWorkers *w, EmHeadSpriteOriginalFault *fault)
{
    /* matrix = *(owner + 0x110 + bone * 4) + 0x90 */
    if (!owner->slots)
        return hs_fault(fault, e->owner + 0x110u, EM_HEAD_SPRITE_FAULT_NULL_WORKER);
    if (e->bone < 0 || (uint32_t)e->bone >= owner->slot_count)
        return hs_fault(fault, e->owner + 0x110u, EM_HEAD_SPRITE_FAULT_BAD_INDEX);
    uint32_t bone_matrix = owner->slots[e->bone] + 0x90u;

    HS_NEED(w, w_001026A0, 0x001026A0u);
    if (hs_leave(fault, 0x001026A0u, w->w_001026A0(w->ctx, e->pos, bone_matrix, e->local)) < 0)
        return -1;
    HS_NEED(w, w_001029C0, 0x001029C0u);
    if (hs_leave(fault, 0x001029C0u, w->w_001029C0(w->ctx, e->matrix)) < 0)
        return -1;
    if (!owner->rot)
        return hs_fault(fault, e->owner + 0xC0u, EM_HEAD_SPRITE_FAULT_NULL_WORKER);
    HS_NEED(w, w_00102C58, 0x00102C58u);
    if (hs_leave(fault, 0x00102C58u, w->w_00102C58(w->ctx, e->matrix, owner->rot)) < 0)
        return -1;
    HS_NEED(w, w_00102918, 0x00102918u);
    if (hs_leave(fault, 0x00102918u, w->w_00102918(w->ctx, e->matrix, e->matrix, e->pos)) < 0)
        return -1;
    HS_NEED(w, w_001CCF70, 0x001CCF70u);
    int32_t handle = 0;
    if (hs_leave(fault, 0x001CCF70u, w->w_001CCF70(w->ctx, e->pos, &handle)) < 0)
        return -1;

    EmHeadSpriteOriginalXf xf; /* sp+0x40 */
    memset(&xf, 0, sizeof xf);
    if (em_head_sprite_original_001CFA60(&xf, e->matrix, hs_bits(e->ramp), hs_bits(e->scalar), w,
                                         fault) < 0)
        return -1;
    if (!world || !world->tables)
        return hs_fault(fault, EM_HEAD_SPRITE_ORIGINAL_SOURCE, EM_HEAD_SPRITE_FAULT_NULL_WORKER);
    EmHeadSpriteOriginalSource st = {EM_HEAD_SPRITE_ORIGINAL_SOURCE, world->tables->d253670};
    return em_head_sprite_original_001CFBE0(handle, EM_HEAD_SPRITE_ORIGINAL_KIND, &st, &xf, 0, world,
                                            w, fault) < 0 ? -1 : 0;
}

int em_head_sprite_original_tick(EmHeadSpriteOriginal *e,
                                 const EmHeadSpriteOriginalOwner *owner,
                                 const EmHeadSpriteOriginalWorld *world,
                                 const EmHeadSpriteOriginalWorkers *w,
                                 EmHeadSpriteOriginalFault *fault)
{
    if (fault->code)
        return -1;
    if (e->freed)
        return hs_fault(fault, EM_HEAD_SPRITE_ORIGINAL_CALLBACK, EM_HEAD_SPRITE_FAULT_BAD_INDEX);

    switch (e->lifecycle) {
    case 0: {
        if (hs_rand_wait(w, &e->timer, fault) < 0)
            return -1;
        e->b0C = 0;
        e->b09 = 0;
        e->lifecycle = 1;
        int r = em_head_sprite_original_001E23A0(e, world ? world->tables : NULL, fault);
        if (r < 0)
            return -1;
        if (r != 0) {
            e->lifecycle = 3;
            return 1;
        }
        if (!world) /* 001B0070 returns D_008106C8 */
            return hs_fault(fault, 0x008106C8u, EM_HEAD_SPRITE_FAULT_NULL_WORKER);
        if (world->d8106C8 & 8)
            e->lifecycle = 3;
        return 1;
    }
    case 1: {
        if (!owner)
            return hs_fault(fault, e->owner, EM_HEAD_SPRITE_FAULT_NULL_WORKER);
        if (owner->address != e->owner)
            return hs_fault(fault, e->owner, EM_HEAD_SPRITE_FAULT_BAD_RESULT);
        if (!(owner->b02 & 0x1F)) {
            if (em_head_sprite_ee_c_le(hs_bits(owner->f220), 0)) { /* f220 <= +0, EE compare */
                e->lifecycle = 3;
                return 1;
            }
        } else if (owner->b04 >= 2) {
            e->lifecycle = 3;
            return 1;
        }
        if (owner->b01 == 0)
            return 1;
        switch (e->sub) {
        case 0:
            e->timer = (int32_t)((uint32_t)e->timer - 1u);
            if (e->timer < 0) {
                e->sub = (uint8_t)(e->sub + 1);
                e->ramp = hs_float(0); /* integer zero store */
                HS_NEED(w, w_00122BB8, 0x00122BB8u);
                int32_t value = 0;
                if (hs_leave(fault, 0x00122BB8u, w->w_00122BB8(w->ctx, &value)) < 0)
                    return -1;
                /* int -> float (truncated), then divided by 2^31 (nearest) */
                e->scalar = hs_float(em_head_sprite_ee_div(em_head_sprite_ee_cvt_s_w(value),
                                                           HS_RAND_SCALE));
            }
            return 1;
        case 1: {
            uint32_t ramp = em_head_sprite_ee_add(hs_bits(e->ramp), HS_RAMP_STEP);
            e->ramp = hs_float(ramp);
            if (!em_head_sprite_ee_c_le(ramp, HS_RAMP_END)) {
                e->ramp = hs_float(HS_RAMP_END); /* 1.5 stored as an integer word */
                if (hs_rand_wait(w, &e->timer, fault) < 0)
                    return -1;
                e->sub = 0;
            }
            return hs_ramp_emit(e, owner, world, w, fault) < 0 ? -1 : 1;
        }
        default:
            return 1;
        }
    }
    case 2:
    case 3:
        HS_NEED(w, w_001AFC10, 0x001AFC10u);
        if (hs_leave(fault, 0x001AFC10u, w->w_001AFC10(w->ctx, e->self)) < 0)
            return -1;
        e->freed = 1;
        return 0;
    default:
        return 1;
    }
}
