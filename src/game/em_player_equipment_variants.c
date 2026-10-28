/* em_player_equipment_variants.c - the equipment nodes' variant routines
 * (see em_player_equipment_variants.h, docs/PLAYER_EQUIPMENT.md 4.7).
 *
 * Read from the byte-matched decomp C of 00188C70, 00189090, 00189330,
 * 001899C0 (with its unit's 00189630) and 00189730, 00189A20; the float
 * constants are the words the original materializes. Every store names
 * the original offset it writes, in the original order. */
#include "game/em_player_equipment_variants.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

#define K_ZERO    UINT32_C(0x00000000)
#define K_ONE     UINT32_C(0x3F800000) /* 1.0 */
#define K_TWO     UINT32_C(0x40000000) /* 2.0 */
#define K_SEVEN   UINT32_C(0x40E00000) /* 7.0 */
#define K_EIGHT   UINT32_C(0x41000000) /* 8.0 */
#define K_ELEVEN  UINT32_C(0x41300000) /* 11.0 */
#define K_TWELVE  UINT32_C(0x41400000) /* 12.0 */
#define K_19      UINT32_C(0x41980000) /* 19.0 */
#define K_20      UINT32_C(0x41A00000) /* 20.0 */
#define K_21      UINT32_C(0x41A80000) /* 21.0 */
#define K_27      UINT32_C(0x41D80000) /* 27.0 */
#define K_39      UINT32_C(0x421C0000) /* 39.0 */
#define K_50      UINT32_C(0x42480000) /* 50.0 */
#define K_52      UINT32_C(0x42500000) /* 52.0 */
#define K_300     UINT32_C(0x43960000) /* 300.0 */
#define K_TENTH   UINT32_C(0x3DCCCCCD) /* 0.1 */
#define K_RAMP    UINT32_C(0x3E212A30) /* 0.1573875 (00189A20) */
#define K_DIP     UINT32_C(0xBF214BDD) /* -0.63006383 (00189730) */
#define K_DIP_STEP UINT32_C(0xBDA14BDD) /* -0.07875798 (00189730) */
#define K_ROLL    UINT32_C(0x3D567750) /* 00189630's +0x38 word */

/* Scratchpad word indices. */
#define S36A0 0x28u /* 0x700036A0 in the 0x70003600 window */
#define S36D0 0x34u /* 0x700036D0 */

/* ELF data rows by original address (inside the table window). */
#define T_A310 0x0024A310u
#define T_A330 0x0024A330u
#define T_A340 0x0024A340u
#define T_A360 0x0024A360u
#define T_A3B0 0x0024A3B0u
#define T_A410 0x0024A410u

/* Player record offsets. */
#define P_STATE  0x004u
#define P_CLOCK  0x03Cu  /* the clip time (float) */
#define P_ACTION 0x1F0u
#define P_FLAGS  0x200u
#define P_CLIP   0x20Cu
#define P_274    0x274u
#define P_MODE   0x275u

typedef EmPlayerEquipmentVariants Ev;
typedef EmPlayerEquipmentNode Node;
typedef EmPlayerEquipmentVariantBytes Xb;

/* ---- faults --------------------------------------------------------------- */

static int fail(const Ev *e, uint32_t address, int32_t code)
{
    if (e && e->fault && e->fault->code == EM_PLAYER_EQUIPMENT_FAULT_NONE) {
        e->fault->address = address;
        e->fault->code = code;
    }
    return -1;
}

#define CALL(addr, expr) do { if ((expr) < 0) return fail(e, (addr), EM_PLAYER_EQUIPMENT_FAULT_WORKER_FAILED); } while (0)
#define STEP(expr) do { if ((expr) < 0) return -1; } while (0)
#define NEED(ptr, addr) do { if (!(ptr)) return fail(e, (addr), EM_PLAYER_EQUIPMENT_FAULT_NULL_WORKER); } while (0)

static int latched(const Ev *e)
{
    return !e || !e->fault || e->fault->code != EM_PLAYER_EQUIPMENT_FAULT_NONE;
}

/* ---- views ---------------------------------------------------------------- */

static uint8_t pb(const Ev *e, uint32_t at) { return e->world->player[at]; }

static int16_t ph(const Ev *e, uint32_t at)
{
    const uint8_t *p = e->world->player + at;
    return (int16_t)(uint16_t)(p[0] | p[1] << 8);
}

static uint32_t pw(const Ev *e, uint32_t at)
{
    const uint8_t *p = e->world->player + at;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* *(D_00275B40 + 4 i): a NULL slot or an index past the array faults. */
static int bone(const Ev *e, uint32_t i, EmOwnerBone **out)
{
    const EmPlayerEquipmentVariantWorld *d = e->world;
    if (i >= d->bones_count || !d->bones[i])
        return fail(e, 0x00275B40u, EM_PLAYER_EQUIPMENT_FAULT_BAD_INDEX);
    *out = d->bones[i];
    return 0;
}

/* `n` words of the ELF data at `address` (inside the table window). */
static int table(const Ev *e, uint32_t address, uint32_t n, uint32_t *out)
{
    const EmPlayerEquipmentVariantWorld *d = e->world;
    if (address < EM_PLAYER_EQUIPMENT_TABLE_BASE || (address & 3u))
        return fail(e, address, EM_PLAYER_EQUIPMENT_FAULT_BAD_INDEX);
    const uint32_t at = (address - EM_PLAYER_EQUIPMENT_TABLE_BASE) / 4u;
    if (at > d->table_words || n > d->table_words - at)
        return fail(e, address, EM_PLAYER_EQUIPMENT_FAULT_BAD_INDEX);
    memcpy(out, d->table + at, 4u * n);
    return 0;
}

static void world_words(const EmOwnerBone *b, uint32_t m[16]) { memcpy(m, b->world, 64); }

/* v x M with M = the bone's world matrix: 001026A0 by value. */
static int transform(const Ev *e, const EmOwnerBone *b, const uint32_t v[4], uint32_t out[4])
{
    uint32_t m[16], in[4];
    world_words(b, m);
    memcpy(in, v, 16);
    CALL(0x001026A0u, e->workers->w_001026A0(e->workers->ctx, m, in, out));
    return 0;
}

/* The bone's world row 3 x, y, z += the 0x700038A0 vector, each an EE
 * add (three loads of the slot, three stores). */
static void add_row3(EmOwnerBone *b, const uint32_t *s38A0)
{
    for (unsigned k = 0; k < 3; ++k) {
        uint32_t w;
        memcpy(&w, &b->world[12 + k], 4);
        w = em_ee_add_bits(w, s38A0[k]);
        memcpy(&b->world[12 + k], &w, 4);
    }
}

/* copy_qw4(+0x90 of every work bone 0 .. +0x0C - 1, *(D_008103D0) + 0x90). */
static int copy_player_bone4(const Ev *e, const Node *node)
{
    for (uint32_t i = 0; i < node->bone_count; ++i) {
        EmOwnerBone *b;
        STEP(bone(e, i, &b));
        memcpy(b->world, e->world->player_bone4->world, 64);
    }
    return 0;
}

/* copy_qw4(+0xD0, *(D_008103D0) + 0x90), then 001C9610(D_00275B40, +0x0C,
 * +0xD0). */
static int place(const Ev *e, Node *node)
{
    const EmPlayerEquipmentVariantWorld *d = e->world;
    memcpy(node->mD0, d->player_bone4->world, 64);
    uint32_t m[16];
    memcpy(m, node->mD0, 64);
    CALL(0x001C9610u, e->workers->w_001C9610(e->workers->ctx, d->bones, d->bones_count,
                                             (int32_t)node->bone_count, m));
    return 0;
}

/* copy_qw4(0x700036A0, bone + 0x90), 0x700036D0 = row x bone world, then
 * 001F4010(index, 0x700036A0). */
static int seed(const Ev *e, const EmOwnerBone *b, uint32_t row_address, int32_t index)
{
    uint32_t *s = e->world->spad3600;
    uint32_t row[4], out[4];
    world_words(b, s + S36A0);
    STEP(table(e, row_address, 4, row));
    STEP(transform(e, b, row, out));
    memcpy(s + S36D0, out, 16);
    CALL(0x001F4010u, e->workers->w_001F4010(e->workers->ctx, index, s + S36A0));
    return 0;
}

static int need_common(const Ev *e)
{
    NEED(e->workers, 0x00188DF0u);
    NEED(e->world, 0x00188DF0u);
    const EmPlayerEquipmentVariantWorld *d = e->world;
    NEED(d->player, 0x008102B0u);
    NEED(d->player_bone4, 0x008103D0u);
    NEED(d->bones, 0x00275B40u);
    return 0;
}

/* ---- 00188C70: flavour 1 variant 0x15 ---------------------------------------- */

int em_player_equipment_00188C70(const Ev *e, Node *node)
{
    if (latched(e)) return -1;
    STEP(need_common(e));
    const EmPlayerEquipmentVariantWorld *d = e->world;
    const EmPlayerEquipmentVariantWorkers *w = e->workers;
    NEED(d->d008106CC, 0x008106CCu);
    NEED(d->d00248B98, 0x00248BA0u);
    NEED(d->d00248C78, 0x00248C80u);
    NEED(d->player_bone19, 0x0081040Cu);
    NEED(d->table, 0x0024A340u);
    NEED(w->w_001026A0, 0x001026A0u);
    NEED(w->w_001F4010, 0x001F4010u);
    if (*d->d008106CC != 0) {
        node->state = 3;                                   /* +0x04 */
        return 0;
    }
    EmOwnerBone *b0;
    if (pb(e, P_STATE) == 1 && pb(e, 0x275) == 4 && pb(e, P_ACTION) == 0x33 &&
        (ph(e, P_CLIP) == d->d00248B98[4] || ph(e, P_CLIP) == d->d00248C78[4])) {
        const uint32_t f = pw(e, P_CLOCK);
        if (em_ee_c_le_bits(f, K_50)) {
            if (!em_ee_c_lt_bits(f, K_39)) {
                if (em_ee_c_eq_bits(f, K_39)) {
                    uint32_t m[16];
                    STEP(bone(e, 0, &b0));
                    world_words(b0, m);              /* *D_00275B40 + 0x90 itself */
                    CALL(0x001F4010u, w->w_001F4010(w->ctx, 9, m));
                }
                return 0;
            }
            if (!em_ee_c_le_bits(f, K_27)) {
                node->drawn = 0;                           /* +0x01 */
            } else if (!em_ee_c_lt_bits(f, K_EIGHT)) {
                STEP(bone(e, 0, &b0));
                memcpy(b0->world, d->player_bone19->world, 64);
                return 0;
            }
        }
    }
    STEP(bone(e, 0, &b0));
    memcpy(b0->world, d->player_bone4->world, 64);
    uint32_t row[4], out[4];
    STEP(table(e, T_A340, 4, row));
    STEP(transform(e, b0, row, out));
    memcpy(&b0->world[12], out, 16);                       /* +0xC0..+0xCC */
    return 0;
}

/* ---- 00189090: flavour 2 variant 1 ------------------------------------------- */

int em_player_equipment_00189090(const Ev *e, Node *node, Xb *x)
{
    if (latched(e)) return -1;
    STEP(need_common(e));
    const EmPlayerEquipmentVariantWorld *d = e->world;
    const EmPlayerEquipmentVariantWorkers *w = e->workers;
    NEED(x, 0x00189090u);
    NEED(d->d00810CA8, 0x00810CA8u);
    NEED(d->table, 0x0024A330u);
    NEED(d->spad3600, 0x700036A0u);
    NEED(d->spad38A0, 0x700038A0u);
    NEED(w->w_001026A0, 0x001026A0u);
    NEED(w->w_001F4010, 0x001F4010u);
    NEED(w->w_001FBD50, 0x001FBD50u);
    STEP(copy_player_bone4(e, node));
    if (pb(e, P_STATE) != 1) node->b05 = 0;
    EmOwnerBone *b0, *b1;
    switch (node->b05) {
    case 0:
        if (pb(e, P_MODE) == 3 && pb(e, P_274) != 0 && (d->d00810CA8[0] | d->d00810CA8[1]) != 0)
            node->b05 = (uint8_t)(node->b05 + 1);
        break;
    case 1:
        if (em_ee_c_le_bits(pw(e, P_CLOCK), K_21)) {
            node->b05 = (uint8_t)(node->b05 + 1);
            CALL(0x001FBD50u, w->w_001FBD50(w->ctx, 0x5DF, 0, K_300));
            x->v70[0] = K_ZERO;                            /* +0x70 */
            x->v70[1] = 0;                                 /* +0x74 */
            x->v70[2] = 0;                                 /* +0x78 */
            x->v70[3] = 0;                                 /* +0x7C */
            node->h28 = 0;                                 /* +0x28 */
        }
        break;
    case 2:
        if (em_ee_c_le_bits(pw(e, P_CLOCK), K_TWELVE)) {
            node->b05 = (uint8_t)(node->b05 + 1);
            STEP(bone(e, 0, &b0));
            STEP(seed(e, b0, T_A330, 7));
        }
        /* fall through */
    case 3: {
        if (em_ee_c_le_bits(pw(e, P_CLOCK), K_TWO)) {
            node->b05 = 0;
            break;
        }
        uint32_t step, out[4];
        STEP(table(e, T_A360 + 4u * (uint32_t)(int32_t)(int16_t)node->h28, 1, &step));
        x->v70[0] = em_ee_add_bits(x->v70[0], step);       /* +0x70 */
        STEP(bone(e, 0, &b0));
        STEP(transform(e, b0, x->v70, out));
        memcpy(d->spad38A0, out, 16);                      /* 0x700038A0 */
        STEP(bone(e, 1, &b1));
        add_row3(b1, d->spad38A0);                         /* +0xC0 / +0xC4 / +0xC8 */
        node->h28 = (uint16_t)(node->h28 + 1);
        break;
    }
    default:
        break;
    }
    return 0;
}

/* ---- 00189330: flavour 2 variant 2 ------------------------------------------- */

/* 0x700038A0 = +0x70 x bone 0's world; bone 1's +0xC0 quadword +=
 * 0x700038A0 through 001028B8. */
static int push(const Ev *e, Xb *x)
{
    const EmPlayerEquipmentVariantWorld *d = e->world;
    EmOwnerBone *b0, *b1;
    uint32_t out[4], row[4], sum[4];
    STEP(bone(e, 0, &b0));
    STEP(transform(e, b0, x->v70, out));
    memcpy(d->spad38A0, out, 16);
    STEP(bone(e, 1, &b1));
    memcpy(row, &b1->world[12], 16);
    memcpy(out, d->spad38A0, 16);
    CALL(0x001028B8u, e->workers->w_001028B8(e->workers->ctx, row, out, sum));
    memcpy(&b1->world[12], sum, 16);
    return 0;
}

int em_player_equipment_00189330(const Ev *e, Node *node, Xb *x)
{
    if (latched(e)) return -1;
    STEP(need_common(e));
    const EmPlayerEquipmentVariantWorld *d = e->world;
    const EmPlayerEquipmentVariantWorkers *w = e->workers;
    NEED(x, 0x00189330u);
    NEED(d->d00248B98, 0x00248B9Au);
    NEED(d->d00248C78, 0x00248C7Au);
    NEED(d->table, 0x0024A310u);
    NEED(d->spad3600, 0x700036A0u);
    NEED(d->spad38A0, 0x700038A0u);
    NEED(w->w_001026A0, 0x001026A0u);
    NEED(w->w_001028B8, 0x001028B8u);
    NEED(w->w_001F4010, 0x001F4010u);
    STEP(copy_player_bone4(e, node));
    if (pb(e, P_STATE) != 1) node->b05 = 0;
    const uint8_t st = node->b05;
    EmOwnerBone *b0;
    switch (st) {
    case 0:
        if (pb(e, P_MODE) == 1 && pb(e, P_ACTION) == 0x33) {
            node->b05 = (uint8_t)(node->b05 + 1);
            memset(x->v70, 0, sizeof x->v70);              /* +0x70..+0x7C */
        }
        break;
    case 1:
        if (ph(e, P_CLIP) == d->d00248B98[1] || ph(e, P_CLIP) == d->d00248C78[1])
            node->b05 = (uint8_t)(node->b05 + 1);
        break;
    case 2:
        if (em_ee_c_le_bits(pw(e, P_CLOCK), K_52)) {
            node->b05 = (uint8_t)(st + 1);
            STEP(bone(e, 0, &b0));
            STEP(seed(e, b0, T_A310, 8));
        }
        x->v70[0] = em_ee_add_bits(x->v70[0], K_TENTH);    /* +0x70 */
        STEP(push(e, x));
        break;
    case 3:
        if (em_ee_c_le_bits(pw(e, P_CLOCK), K_SEVEN)) node->b05 = (uint8_t)(st + 1);
        STEP(push(e, x));
        break;
    case 4:
        if (em_ee_c_le_bits(pw(e, P_CLOCK), K_ONE)) {
            node->b05 = (uint8_t)(st + 1);
        } else {
            x->v70[0] = em_ee_sub_bits(x->v70[0], K_TENTH);
            STEP(push(e, x));
        }
        break;
    case 5:
        if (pb(e, P_ACTION) != 0x33) node->b05 = 0;
        break;
    default:
        break;
    }
    return 0;
}

/* ---- 001899C0: flavour 2 variant 3 (00189630, 00189730) ---------------------- */

/* 00189630(self, D_008102B0): the +0x06 machine on work bone 2's +0x70. */
static int body_00189630(const Ev *e, Node *node, Xb *x)
{
    EmOwnerBone *b2;
    if (pb(e, P_STATE) != 1) {
        x->b06 = 0;
        STEP(bone(e, 2, &b2));
        memset(&b2->rot[0], 0, 4);                         /* +0x70 = 0 */
    }
    const uint8_t st = x->b06;
    switch (st) {
    case 0:
        if (pb(e, P_MODE) == 2 && pb(e, P_274) != 0) x->b06 = (uint8_t)(st + 1);
        break;
    case 1:
        if (em_ee_c_le_bits(pw(e, P_CLOCK), K_20)) {
            x->b06 = (uint8_t)(st + 1);
            node->h28 = 0x14;                              /* +0x28 */
            x->w38 = K_ROLL;                               /* +0x38 */
        }
        break;
    case 2: {
        const int16_t t = (int16_t)node->h28;
        node->h28 = (uint16_t)(t - 1);
        STEP(bone(e, 2, &b2));
        if (t == 0) {
            x->b06 = 0;
            memset(&b2->rot[0], 0, 4);
        } else {
            uint32_t v;
            memcpy(&v, &b2->rot[0], 4);
            v = em_ee_add_bits(v, x->w38);
            memcpy(&b2->rot[0], &v, 4);
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

/* 00189730(self, D_008102B0): the +0x05 machine on work bone 1's +0x74
 * and the 001F4010(8) seeds from work bone 2. */
static int body_00189730(const Ev *e, Node *node, Xb *x)
{
    const EmPlayerEquipmentVariantWorld *d = e->world;
    const EmPlayerEquipmentVariantWorkers *w = e->workers;
    EmOwnerBone *b1, *b2;
    if (pb(e, P_STATE) != 1) {
        node->b05 = 0;
        STEP(bone(e, 1, &b1));
        memset(&b1->rot[1], 0, 4);                         /* +0x74 = 0 */
    }
    const uint8_t st = node->b05;
    switch (st) {
    case 0:
        if (pb(e, P_MODE) == 2 && pb(e, P_ACTION) == 0x33) node->b05 = (uint8_t)(node->b05 + 1);
        break;
    case 1:
        if (ph(e, P_CLIP) == d->d00248B98[2] || ph(e, P_CLIP) == d->d00248C78[2]) {
            node->b05 = (uint8_t)(node->b05 + 1);
            node->h28 = 8;                                 /* +0x28 */
            x->w38 = K_DIP_STEP;                           /* +0x38 */
        }
        break;
    case 2: {
        const int16_t n = (int16_t)node->h28;
        node->h28 = (uint16_t)(n - 1);
        STEP(bone(e, 1, &b1));
        if (n == 0) {
            node->b05 = (uint8_t)(node->b05 + 1);
            const uint32_t dip = K_DIP;
            memcpy(&b1->rot[1], &dip, 4);                  /* +0x74 */
            node->h28 = 0;
            int32_t r;
            CALL(0x00122BB8u, w->w_00122BB8(w->ctx, &r));
            x->h2A = (int16_t)(r & 1);                     /* +0x2A */
        } else {
            uint32_t v;
            memcpy(&v, &b1->rot[1], 4);
            v = em_ee_add_bits(v, x->w38);
            memcpy(&b1->rot[1], &v, 4);
        }
        break;
    }
    case 3:
        if ((int16_t)node->h28 >= 6) {
            node->b05 = (uint8_t)(st + 1);
        } else if ((*d->spad3B68 & 1) == 0) {
            node->h28 = (uint16_t)(node->h28 + 1);
            /* D_0024A410[+0x2A][+0x28]: rows of six words */
            const int32_t k = (int32_t)x->h2A * 6 + (int16_t)node->h28;
            uint32_t n;
            STEP(table(e, T_A410 + 4u * (uint32_t)k, 1, &n));
            STEP(bone(e, 2, &b2));
            STEP(seed(e, b2, T_A3B0 + 16u * n, 8));
        }
        break;
    case 4:
        if (em_ee_c_le_bits(pw(e, P_CLOCK), K_ELEVEN)) {
            node->b05 = (uint8_t)(st + 1);
            node->h28 = 8;
        }
        break;
    case 5: {
        const int16_t n = (int16_t)node->h28;
        node->h28 = (uint16_t)(n - 1);
        STEP(bone(e, 1, &b1));
        if (n == 0) {
            node->b05 = (uint8_t)(node->b05 + 1);
            memset(&b1->rot[1], 0, 4);
        } else {
            uint32_t v;
            memcpy(&v, &b1->rot[1], 4);
            v = em_ee_sub_bits(v, x->w38);
            memcpy(&b1->rot[1], &v, 4);
        }
        break;
    }
    case 6:
        if (pb(e, P_ACTION) != 0x33) node->b05 = 0;
        break;
    default:
        break;
    }
    return 0;
}

int em_player_equipment_001899C0(const Ev *e, Node *node, Xb *x)
{
    if (latched(e)) return -1;
    STEP(need_common(e));
    const EmPlayerEquipmentVariantWorld *d = e->world;
    const EmPlayerEquipmentVariantWorkers *w = e->workers;
    NEED(x, 0x001899C0u);
    NEED(d->d00248B98, 0x00248B9Cu);
    NEED(d->d00248C78, 0x00248C7Cu);
    NEED(d->table, 0x0024A410u);
    NEED(d->spad3600, 0x700036A0u);
    NEED(d->spad3B68, 0x70003B68u);
    NEED(w->w_001026A0, 0x001026A0u);
    NEED(w->w_001F4010, 0x001F4010u);
    NEED(w->w_00122BB8, 0x00122BB8u);
    NEED(w->w_001C9610, 0x001C9610u);
    STEP(body_00189630(e, node, x));
    STEP(body_00189730(e, node, x));
    return place(e, node);
}

/* ---- 00189A20: flavour 2 variant 0xC ----------------------------------------- */

/* 0x700038A0 = +0x70 x bone 0's world; bone 1's world row 3 x, y, z +=
 * it. */
static int nudge(const Ev *e, Xb *x)
{
    const EmPlayerEquipmentVariantWorld *d = e->world;
    EmOwnerBone *b0, *b1;
    uint32_t out[4];
    STEP(bone(e, 0, &b0));
    STEP(transform(e, b0, x->v70, out));
    memcpy(d->spad38A0, out, 16);
    STEP(bone(e, 1, &b1));
    add_row3(b1, d->spad38A0);
    return 0;
}

int em_player_equipment_00189A20(const Ev *e, Node *node, Xb *x)
{
    if (latched(e)) return -1;
    STEP(need_common(e));
    const EmPlayerEquipmentVariantWorld *d = e->world;
    const EmPlayerEquipmentVariantWorkers *w = e->workers;
    NEED(x, 0x00189A20u);
    NEED(d->spad38A0, 0x700038A0u);
    NEED(w->w_001026A0, 0x001026A0u);
    NEED(w->w_001C9610, 0x001C9610u);
    STEP(place(e, node));
    if (pb(e, P_STATE) != 1) node->b05 = 0;                /* D_008102B4 */
    const uint8_t st = node->b05;
    switch (st) {
    case 0:
        if (pb(e, P_MODE) == 5 && pb(e, P_ACTION) == 0x33) {
            node->b05 = (uint8_t)(node->b05 + 1);
            node->h28 = 8;
            memset(x->v70, 0, sizeof x->v70);
        }
        break;
    case 1:
    case 3: {
        const int16_t t = (int16_t)node->h28;
        node->h28 = (uint16_t)(t - 1);
        if (t == 0) {
            node->b05 = (uint8_t)(node->b05 + 1);
        } else {
            x->v70[0] = st == 1 ? em_ee_add_bits(x->v70[0], K_RAMP) : em_ee_sub_bits(x->v70[0], K_RAMP);
            STEP(nudge(e, x));
        }
        break;
    }
    case 2:
        if (em_ee_c_le_bits(pw(e, P_CLOCK), K_19)) {
            node->b05 = (uint8_t)(st + 1);
            node->h28 = 8;
        }
        STEP(nudge(e, x));
        break;
    case 4:
        if (pw(e, P_FLAGS) & 0x1000u) node->b05 = (uint8_t)(st + 1);
        break;
    case 5:
        if (pb(e, P_ACTION) != 0x33) node->b05 = 0;
        break;
    default:
        break;
    }
    return 0;
}
