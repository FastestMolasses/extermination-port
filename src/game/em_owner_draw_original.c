/* World-owner draw workers and the AREA11 world model bank. See
 * em_owner_draw_original.h and docs/OWNER_DRAW.md. Every routine names the
 * original address it translates; tools/test_owner_draw_reference.py runs the
 * original instructions against it.
 *
 * Float arithmetic: every EE COP1 and VU0-macro instruction goes through
 * game/em_ee_float.h, named by its real form (op, dest, bc). No host float
 * operation is performed here. */
#include "game/em_owner_draw_original.h"

#include "game/em_ee_float.h"

#include <string.h>

typedef uint32_t u32;
typedef EmOwnerDraw S;

/* Dest masks (x = 8, y = 4, z = 2, w = 1). */
#define DX 8u
#define DXYZ 14u
#define DXYZW 15u

/* ======================================================================
 * Fault latch
 * ==================================================================== */

static int latched(const S *s) { return s->fault.code != EM_OWNER_DRAW_FAULT_NONE; }

static int fault(S *s, u32 address, int32_t code)
{
    if (!latched(s)) {
        s->fault.address = address;
        s->fault.code = code;
    }
    return -1;
}

static u32 get32(const uint8_t *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static void put32(uint8_t *p, u32 v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put16(uint8_t *p, u32 v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* ======================================================================
 * 001CA7B0
 * ==================================================================== */

/* One VU0 macro instruction; `*st` is sticky (see em_owner_services). */
static void vu(int *st, em_vu_op op, unsigned dest, int bc, const u32 fs[4], const u32 ft[4],
               const u32 acc[4], u32 dst[4])
{
    if (*st != EM_EE_FLOAT_OK) return;
    *st = em_vu_vec_bits(op, dest, bc, fs, ft, 0, acc, dst);
}

/* 00102738(p, v): (p.x * v.x + p.y * v.y) + p.z * v.z. The product is one
 * MUL xyz with the plane as fs (the form clamps fs only); lane w keeps v.w.
 * Then ADDBC x/1 and x/2 fold y and z into x. Returns lane x. */
static u32 inner_00102738(int *st, const u32 plane[4], const u32 v[4])
{
    u32 r[4];
    memcpy(r, v, sizeof r);
    vu(st, EM_VU_MUL, DXYZ, EM_VU_NO_BC, plane, r, NULL, r);
    vu(st, EM_VU_ADDBC, DX, 1, r, r, NULL, r);
    vu(st, EM_VU_ADDBC, DX, 2, r, r, NULL, r);
    return r[0];
}

int em_owner_draw_001CA7B0(S *s, const u32 position[4], u32 radius, int32_t *flags)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!position || !flags) return fault(s, 0x001CA7B0u, EM_OWNER_DRAW_FAULT_NULL_WORKER);
    if (!s->world.d00810610) return fault(s, 0x00810610u, EM_OWNER_DRAW_FAULT_NULL_WORKER);
    if (!s->world.ctx_2410) return fault(s, 0x00275670u, EM_OWNER_DRAW_FAULT_NULL_WORKER);

    /* 00102948 copies the whole quadword; the stack w is then 1.0. */
    u32 b[4] = {position[0], position[1], position[2], EM_EE_ONE};
    u32 m[16], a[4] = {0, 0, 0, 0}, acc[4] = {0, 0, 0, 0};
    int st = EM_EE_FLOAT_OK;
    memcpy(m, s->world.d00810610, sizeof m);
    /* 001026A0(a, D_00810610, b): ACC = row0 * b.x (MULABC xyzw/0),
     * += row1 * b.y, += row2 * b.z (MADDABC xyzw/1, /2), a = ACC + row3 * b.w
     * (MADDBC xyzw/3). */
    vu(&st, EM_VU_MULABC, DXYZW, 0, m, b, NULL, acc);
    vu(&st, EM_VU_MADDABC, DXYZW, 1, m + 4, b, acc, acc);
    vu(&st, EM_VU_MADDABC, DXYZW, 2, m + 8, b, acc, acc);
    vu(&st, EM_VU_MADDBC, DXYZW, 3, m + 12, b, acc, a);
    if (st != EM_EE_FLOAT_OK) return fault(s, 0x001026A0u, EM_OWNER_DRAW_FAULT_UNMEASURED_FORM);
    a[3] = 0;                                        /* the stack w store */

    const u32 neg = em_ee_neg_bits(radius);          /* NEG.S of f12 */
    int32_t result = 0;
    u32 f = a[2];                                    /* view z */
    if (em_ee_c_lt_bits(f, neg)) {
        *flags = -1;
        return 0;
    }
    if (em_ee_c_lt_bits(f, radius)) result |= 1;
    for (int k = 0; k < 4; ++k) {                    /* context +0x2410 + 0x10 k */
        f = inner_00102738(&st, s->world.ctx_2410 + 4 * k, a);
        if (st != EM_EE_FLOAT_OK) return fault(s, 0x00102738u, EM_OWNER_DRAW_FAULT_UNMEASURED_FORM);
        if (em_ee_c_lt_bits(f, neg)) {
            *flags = -1;
            return 0;
        }
        if (em_ee_c_lt_bits(f, radius)) result |= 2 << k;
    }
    *flags = result;
    return 0;
}

/* ======================================================================
 * 001CA940 and the kernel-submit chain
 * ==================================================================== */

/* Bytes one 001D37D0 / 001D3AD0 run appends after its skin-record REF. */
static u32 submit_bytes(int with_ref2) { return with_ref2 ? 0x40u : 0x30u; }

uint32_t em_owner_draw_001CA940_bytes(int32_t flags, int with_ref2)
{
    u32 one = 0x10u + submit_bytes(with_ref2);       /* REF 8 + the submit */
    return (flags != 0 && (flags & 1)) ? 2u * one : one;
}

/* Every view the chain reaches, the channel and the whole run of `bytes`
 * before the cursor, checked before anything is written. */
static int prepare(S *s, int32_t chan, u32 bytes, u32 fn)
{
    const EmOwnerDrawWorld *w = &s->world;
    if (!w->ctx_9C || !w->ctx_0C) return fault(s, 0x00275670u, EM_OWNER_DRAW_FAULT_NULL_WORKER);
    if (!w->d00275674) return fault(s, 0x00275674u, EM_OWNER_DRAW_FAULT_NULL_WORKER);
    if (!w->channel || !w->ctx_50) return fault(s, 0x00275670u, EM_OWNER_DRAW_FAULT_NULL_WORKER);
    if (chan < 0 || (u32)chan >= w->channel_count || (u32)chan >= w->ctx_50_count)
        return fault(s, fn, EM_OWNER_DRAW_FAULT_BAD_INDEX);
    EmOwnerServicesChannel *c = &w->channel[chan];
    if (!c->cursor || !c->end || c->cursor > c->end || (size_t)(c->end - c->cursor) < bytes)
        return fault(s, fn, EM_OWNER_DRAW_FAULT_BAD_INDEX);
    return 0;
}

/* A DMA tag at the cursor: byte +3 = id, word +4 = address, halfword +0 =
 * qwc; byte +2 and +8..+0xF are left as they are. The cursor advances 0x10. */
static void tag(EmOwnerServicesChannel *c, uint8_t id, u32 qwc, u32 address)
{
    uint8_t *p = c->cursor;
    p[3] = id;
    put32(p + 4, address);
    put16(p, qwc);
    c->cursor = p + 0x10;
}

/* vif_append_ref_tag (001D2090)(chan, target). */
static void append_ref_tag(S *s, int32_t chan, u32 target)
{
    EmOwnerServicesChannel *c = &s->world.channel[chan];
    tag(c, 0x30, 1u, *s->world.d00275674);          /* REF 1 qw to *D_00275674 */
    s->world.ctx_50[chan] = target;                 /* context +0x50 + 4 chan */
    tag(c, 0x50, 0u, target);                        /* CALL */
}

/* 001D37D0 (kernel 0023C750) and 001D3AD0 (kernel 002354A0). */
static void submit(S *s, int32_t chan, u32 kernel, u32 model, u32 w04)
{
    EmOwnerServicesChannel *c = &s->world.channel[chan];
    append_ref_tag(s, chan, kernel);
    if ((*s->world.ctx_0C & 1u) == 0)               /* 001D2910(0) == 0 */
        tag(c, 0x30, 2u, EM_OWNER_DRAW_D_002514B0);
    tag(c, 0x30, w04 & 0xFFFFu, model + 0x40u);      /* sh of the +0x04 word */
}

static void skin_ref(S *s, int32_t chan, u32 record)
{
    tag(&s->world.channel[chan], 0x30, 8u, record + (*s->world.ctx_9C << 7));
}

int em_owner_draw_001D38A0(S *s, int32_t chan, u32 model, u32 w04)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    int ref2 = s->world.ctx_0C ? (*s->world.ctx_0C & 1u) == 0 : 0;
    if (prepare(s, chan, 0x10u + submit_bytes(ref2), 0x001D38A0u) < 0) return -1;
    skin_ref(s, chan, EM_OWNER_DRAW_SKIN_RECORD_0);
    submit(s, chan, EM_OWNER_DRAW_KERNEL, model, w04);
    return 0;
}

int em_owner_draw_001D3BA0(S *s, int32_t chan, u32 model, u32 w04)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    int ref2 = s->world.ctx_0C ? (*s->world.ctx_0C & 1u) == 0 : 0;
    if (prepare(s, chan, 2u * (0x10u + submit_bytes(ref2)), 0x001D3BA0u) < 0) return -1;
    skin_ref(s, chan, EM_OWNER_DRAW_SKIN_RECORD_0);          /* 001D38A0 */
    submit(s, chan, EM_OWNER_DRAW_KERNEL, model, w04);        /* its 001D37D0 */
    skin_ref(s, chan, EM_OWNER_DRAW_SKIN_RECORD_1);
    submit(s, chan, EM_OWNER_DRAW_CLIP_KERNEL, model, w04);   /* 001D3AD0 */
    return 0;
}

int em_owner_draw_001CA940_at(S *s, int32_t flags, u32 model, u32 w04)
{
    /* flags != 0 and bit 0 set: 001D3C30 -> 001D3BA0(0, model); otherwise
     * 001D38F0 -> 001D38A0(0, model). */
    if (flags != 0 && (flags & 1)) return em_owner_draw_001D3BA0(s, 0, model, w04);
    return em_owner_draw_001D38A0(s, 0, model, w04);
}

int em_owner_draw_001CA940(S *s, int32_t flags, const EmOwnerModel *model)
{
    if (!s) return -1;
    if (latched(s)) return -1;
    if (!s->world.models) return fault(s, 0x0028A59Cu, EM_OWNER_DRAW_FAULT_NULL_WORKER);
    const EmWorldModel *m = em_world_models_of(s->world.models, model);
    if (!m) return fault(s, 0x001CA940u, EM_OWNER_DRAW_FAULT_BAD_INDEX);
    return em_owner_draw_001CA940_at(s, flags, m->address, m->w04);
}

/* ======================================================================
 * The world model bank
 * ==================================================================== */

#define HEADER_BYTES 0x20u
static const u32 BLOCK_HEAD[4] = {0u, 0u, 0x01000404u, 0x6C808000u}; /* STCYCL 4,4; UNPACK V4-32 128 */
#define BLOCK_MSCAL 0x14000000u
#define BLOCK_MSCNT 0x17000000u

static int model_ok(const uint8_t *m, u32 room, u32 *size)
{
    if (room < 0x40u) return 0;
    u32 blocks = get32(m), w04 = get32(m + 4), bones = get32(m + 8), skel = get32(m + 0xC);
    if (blocks == 0 || blocks > 0x10000u || w04 != blocks * EM_WORLD_MODELS_BLOCK_QWORDS) return 0;
    if (bones > 0xFFu || skel != 0x40u + w04 * 16u) return 0;
    u32 total = skel + 0x50u * bones;
    if (total > room) return 0;
    for (u32 b = 0; b < blocks; ++b) {
        const uint8_t *blk = m + 0x40u + b * EM_WORLD_MODELS_BLOCK_QWORDS * 16u;
        for (int k = 0; k < 4; ++k)
            if (get32(blk + 4 * k) != BLOCK_HEAD[k]) return 0;
        const uint8_t *tail = blk + (EM_WORLD_MODELS_BLOCK_QWORDS - 1u) * 16u;
        if (get32(tail) != (b ? BLOCK_MSCNT : BLOCK_MSCAL) || get32(tail + 4) || get32(tail + 8) ||
            get32(tail + 12))
            return 0;
    }
    *size = total;
    return 1;
}

int em_world_models_parse(EmWorldModels *bank, const uint8_t *data, size_t size)
{
    if (!bank) return -1;
    memset(bank, 0, sizeof *bank);
    if (!data || size < HEADER_BYTES) return -1;
    u32 table = get32(data + 8), span = get32(data + 0xC), count = get32(data + 0x10);
    if (get32(data) != EM_WORLD_MODELS_MAGIC || get32(data + 4) != EM_WORLD_MODELS_VERSION ||
        get32(data + 0x14) || get32(data + 0x18) || get32(data + 0x1C))
        return -1;
    if ((size_t)span != size - HEADER_BYTES || span < 4u) return -1;
    const uint8_t *t = data + HEADER_BYTES;
    if (get32(t) != count || count == 0 || count > EM_WORLD_MODELS_MAX || 4u + 4u * count > span)
        return -1;
    EmWorldModels *out = bank;
    for (u32 i = 0; i < count; ++i) {
        u32 off = get32(t + 4u + 4u * i) & ~3u;      /* arithmetic >> 2 << 2 */
        EmWorldModel *m = &out->models[i];
        m->id = i;
        if (off < 4u + 4u * count || off >= span) goto bad;
        const uint8_t *p = t + off;
        u32 bytes;
        if (!model_ok(p, span - off, &bytes)) goto bad;
        u32 bones = get32(p + 8);
        if (out->record_count + bones > EM_WORLD_MODELS_MAX_RECORDS) goto bad;
        m->address = table + off;
        m->blocks = get32(p);
        m->w04 = get32(p + 4);
        m->size = bytes;
        m->bytes = p;
        m->model.bone_count = (uint8_t)bones;
        u32 radius = get32(p + 0x20);
        memcpy(&m->model.radius, &radius, sizeof radius);
        m->model.skeleton = &out->records[out->record_count];
        m->model.skeleton_records = bones;
        const uint8_t *r = p + get32(p + 0xC);
        for (u32 k = 0; k < bones; ++k, r += 0x50) {
            EmOwnerSkeletonRecord *rec = &out->records[out->record_count++];
            rec->parent = (int16_t)(uint16_t)(r[4] | r[5] << 8);
            for (int j = 0; j < 16; ++j) {
                u32 word = get32(r + 0x10 + 4 * j);
                memcpy(&rec->bind[j], &word, sizeof word);
            }
        }
    }
    out->table_address = table;
    out->count = count;
    out->model_count = count;
    out->span = t;
    out->span_size = span;
    return 0;
bad:
    memset(bank, 0, sizeof *bank);
    return -1;
}

const EmWorldModel *em_world_models_at(const EmWorldModels *bank, u32 address)
{
    if (!bank) return NULL;
    for (u32 i = 0; i < bank->model_count; ++i)
        if (bank->models[i].address == address) return &bank->models[i];
    return NULL;
}

const EmWorldModel *em_world_models_of(const EmWorldModels *bank, const EmOwnerModel *model)
{
    if (!bank || !model) return NULL;
    for (u32 i = 0; i < bank->model_count; ++i)
        if (&bank->models[i].model == model) return &bank->models[i];
    return NULL;
}

int em_world_models_001C6120(const EmWorldModels *bank, u32 bank_word, u32 id, u32 *handle)
{
    if (!bank || !handle || !bank->span || bank_word != bank->table_address) return -1;
    u32 index = id & 0x7FFFu;                        /* & 0xFFFF, then & ~0x8000 */
    if (index >= bank->count) return -1;             /* would read past the table words */
    u32 address = bank_word + (get32(bank->span + 4u + 4u * index) & ~3u);
    if (!em_world_models_at(bank, address)) return -1;
    *handle = address;
    return 0;
}
