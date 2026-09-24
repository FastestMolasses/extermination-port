/* em_player_equipment.c - the player's attached equipment nodes (see
 * em_player_equipment.h, docs/PLAYER_EQUIPMENT.md).
 *
 * Read from the byte-matched decomp C of 0018A6B0, 0018A8D0, 00188630,
 * 00188A50, 00188AC0, 00188B80, 00188DF0, 00188ED0, 0018A1F0, 00189D30 and
 * 0015D2F0 (the split listing was checked where the C leaves a register
 * implicit: 0018A6B0 passes the node and its +0x04 byte to 0018A8D0 and to
 * the flavour routines; 00188B80's clip-time test is "not below 12.0" then
 * "at most 51.0"). Every store names the original offset it writes. */
#include "game/em_player_equipment.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

#define ONE        UINT32_C(0x3F800000)
#define ZERO       UINT32_C(0x00000000)
#define MINUS3     UINT32_C(0xC0400000) /* -3.0 (00188630) */
#define QUARTER    UINT32_C(0x3E800000) /* 0.25 (0018A1F0) */
#define HALF       UINT32_C(0x3F000000) /* 0.5 (00188ED0) */
#define F3_6       UINT32_C(0x40666666) /* 3.6 (00188ED0) */
#define FIVE       UINT32_C(0x40A00000) /* 5.0 (00189D30) */
#define TEN        UINT32_C(0x41200000) /* 10.0 (00189D30 f12) */
#define TWELVE     UINT32_C(0x41400000) /* 12.0 (00188B80) */
#define F51        UINT32_C(0x424C0000) /* 51.0 (00188B80) */
#define F64        UINT32_C(0x42800000) /* 64.0 (00189D30) */
#define F128       UINT32_C(0x43000000) /* 128.0 (00189D30) */
#define HALF_PI    UINT32_C(0x3FC90FDB) /* 1.57079637 (00188630 mode 4) */

/* Scratchpad word indices (0x70003600 window, 0x700038A0 window). */
#define S3600 0x00u
#define S36A0 0x28u
#define S36D0 0x34u
#define S36E0 0x38u
#define S38A0 0x00u
#define S38B0 0x04u
#define S38C0 0x08u
#define S38D0 0x0Cu

/* Table rows by original address. */
#define T_A220 0x0024A220u
#define T_A2A0 0x0024A2A0u
#define T_A300 0x0024A300u
#define T_A440 0x0024A440u
#define T_A4A0 0x0024A4A0u

/* Player record offsets. */
#define P_STATE    0x004u  /* D_008102B4 */
#define P_SUB      0x005u  /* D_008102B5 */
#define P_POS      0x0B0u  /* D_00810360 */
#define P_ACTION   0x1F0u  /* D_008104A0 */
#define P_ACTION2  0x1F1u  /* D_008104A1 */
#define P_MODE     0x275u  /* D_00810525 */
#define P_MATRIX   0x2A0u  /* D_00810550 */
#define P_2F2      0x2F2u  /* D_008105A2 */
#define P_318      0x318u  /* D_008105C8 */

/* ---- faults --------------------------------------------------------------- */

static int fail(const EmPlayerEquipment *e, uint32_t address, int32_t code)
{
    if (e && e->fault && e->fault->code == EM_PLAYER_EQUIPMENT_FAULT_NONE) {
        e->fault->address = address;
        e->fault->code = code;
    }
    return -1;
}

#define CALL(addr, expr) do { if ((expr) < 0) return fail(e, (addr), EM_PLAYER_EQUIPMENT_FAULT_WORKER_FAILED); } while (0)
#define STEP(expr) do { if ((expr) < 0) return -1; } while (0)

static int latched(const EmPlayerEquipment *e)
{
    return !e || !e->fault || e->fault->code != EM_PLAYER_EQUIPMENT_FAULT_NONE;
}

/* ---- reach checks (before any write) -------------------------------------- */

#define NEED(ptr, addr) do { if (!(ptr)) return fail(e, (addr), EM_PLAYER_EQUIPMENT_FAULT_NULL_WORKER); } while (0)

static int need_vu(const EmPlayerEquipment *e)
{
    const EmPlayerEquipmentWorkers *w = e->workers;
    NEED(w->w_001026A0, 0x001026A0u);
    NEED(w->w_001026D0, 0x001026D0u);
    NEED(w->w_001028B8, 0x001028B8u);
    NEED(w->w_001028D0, 0x001028D0u);
    NEED(w->w_00102760, 0x00102760u);
    NEED(w->w_001029C0, 0x001029C0u);
    NEED(w->w_00102BB0, 0x00102BB0u);
    return 0;
}

static int need_common(const EmPlayerEquipment *e)
{
    NEED(e->workers, 0x0018A6B0u);
    NEED(e->world, 0x0018A6B0u);
    const EmPlayerEquipmentWorld *d = e->world;
    NEED(d->player, 0x008102B0u);
    NEED(d->player_bones, 0x008103C0u);
    NEED(d->d00275B40, 0x00275B40u);
    return 0;
}

static int need_00188630(const EmPlayerEquipment *e)
{
    const EmPlayerEquipmentWorkers *w = e->workers;
    const EmPlayerEquipmentWorld *d = e->world;
    STEP(need_vu(e));
    NEED(d->d00810CA4, 0x00810CA4u);
    NEED(d->table, 0x0024A220u);
    NEED(d->spad3600, 0x70003600u);
    NEED(w->w_001854E0, 0x001854E0u);
    NEED(w->w_00185760, 0x00185760u);
    NEED(w->w_001861C0, 0x001861C0u);
    NEED(w->w_001869A0, 0x001869A0u);
    NEED(w->w_00186A60, 0x00186A60u);
    NEED(w->w_001872C0, 0x001872C0u);
    NEED(w->w_00187CC0, 0x00187CC0u);
    NEED(w->w_001B61C0, 0x001B61C0u);
    NEED(w->w_001EFEB0, 0x001EFEB0u);
    NEED(w->w_001F4010, 0x001F4010u);
    return 0;
}

static int need_00188AC0(const EmPlayerEquipment *e)
{
    const EmPlayerEquipmentWorld *d = e->world;
    NEED(d->d00810CA4, 0x00810CA4u);
    NEED(d->d008106CC, 0x008106CCu);
    NEED(d->d008106C6, 0x008106C6u);
    NEED(e->workers->w_0015C310, 0x0015C310u);
    return 0;
}

static int need_00188A50(const EmPlayerEquipment *e)
{
    STEP(need_00188AC0(e));
    NEED(e->world->d00248B98, 0x00248B98u);
    NEED(e->world->d00248C78, 0x00248C78u);
    NEED(e->workers->w_00188C70, 0x00188C70u);
    return 0;
}

static int need_00188ED0(const EmPlayerEquipment *e)
{
    NEED(e->world->d008106C7, 0x008106C7u);
    NEED(e->world->spad38A0, 0x700038A0u);
    NEED(e->workers->w_001026A0, 0x001026A0u);
    NEED(e->workers->w_001B0070, 0x001B0070u);
    NEED(e->workers->w_00187780, 0x00187780u);
    return 0;
}

static int need_00188DF0(const EmPlayerEquipment *e)
{
    const EmPlayerEquipmentWorkers *w = e->workers;
    STEP(need_00188ED0(e));
    NEED(w->w_00189090, 0x00189090u);
    NEED(w->w_00189330, 0x00189330u);
    NEED(w->w_001899C0, 0x001899C0u);
    NEED(w->w_00189A20, 0x00189A20u);
    NEED(w->w_001C9610, 0x001C9610u);
    return 0;
}

static int need_00189D30(const EmPlayerEquipment *e)
{
    NEED(e->world->spad38A0, 0x700038A0u);
    NEED(e->workers->w_001EFF10, 0x001EFF10u);
    return 0;
}

static int need_0018A1F0(const EmPlayerEquipment *e)
{
    const EmPlayerEquipmentWorkers *w = e->workers;
    const EmPlayerEquipmentWorld *d = e->world;
    STEP(need_vu(e));
    STEP(need_00189D30(e));
    NEED(d->table, 0x0024A440u);
    NEED(d->hit, 0x700031B0u);
    NEED(w->w_001AA840, 0x001AA840u);
    NEED(w->w_0019B2C0, 0x0019B2C0u);
    NEED(w->w_00189EC0, 0x00189EC0u);
    NEED(w->w_face_record, 0x700031D0u);
    NEED(w->w_001F00A0, 0x001F00A0u);
    NEED(w->w_0018A180, 0x0018A180u);
    NEED(w->w_0019A570, 0x0019A570u);
    NEED(w->w_00189FE0, 0x00189FE0u);
    return 0;
}

static int need_0018A8D0(const EmPlayerEquipment *e)
{
    const EmPlayerEquipmentWorkers *w = e->workers;
    const EmPlayerEquipmentWorld *d = e->world;
    NEED(d->d008106C6, 0x008106C6u);
    NEED(d->d00275BCC, 0x00275BCCu);
    NEED(d->d0028A56C, 0x0028A56Cu);
    NEED(w->w_001C6120, 0x001C6120u);
    NEED(w->w_001CA6E0, 0x001CA6E0u);
    NEED(w->w_001C6150, 0x001C6150u);
    NEED(w->w_001AF780, 0x001AF780u);
    NEED(w->w_anim_bone_array_setup, 0x001CB5B0u);
    NEED(w->w_bone_init_default_1, 0x001C62C0u);
    return 0;
}

static int need_tick(const EmPlayerEquipment *e)
{
    const EmPlayerEquipmentWorld *d = e->world;
    STEP(need_0018A8D0(e));
    STEP(need_00188630(e));
    STEP(need_00188A50(e));
    STEP(need_00188DF0(e));
    STEP(need_0018A1F0(e));
    NEED(d->d008106C7, 0x008106C7u);
    NEED(d->d00810CA6, 0x00810CA6u);
    NEED(e->workers->w_001AFC10, 0x001AFC10u);
    NEED(e->workers->w_method, 0x0018A6B0u);
    return 0;
}

/* ---- storage access ------------------------------------------------------- */

static uint8_t pb(const EmPlayerEquipment *e, unsigned at) { return e->world->player[at]; }

static int16_t ph(const EmPlayerEquipment *e, unsigned at)
{
    uint16_t v;
    memcpy(&v, e->world->player + at, 2);
    return (int16_t)v;
}

static uint32_t pw(const EmPlayerEquipment *e, unsigned at)
{
    uint32_t v;
    memcpy(&v, e->world->player + at, 4);
    return v;
}

static void pvec(const EmPlayerEquipment *e, unsigned at, uint32_t out[4])
{
    memcpy(out, e->world->player + at, 16);
}

/* The world matrix (+0x90) of the player's bone slot `slot` (player +0x110
 * + 4 * slot). */
static int player_world(const EmPlayerEquipment *e, unsigned slot, uint32_t out[16])
{
    const EmPlayerEquipmentWorld *d = e->world;
    if (slot >= d->player_bone_count || !d->player_bones[slot])
        return fail(e, 0x008103C0u + 4u * slot, EM_PLAYER_EQUIPMENT_FAULT_BAD_INDEX);
    memcpy(out, d->player_bones[slot]->world, 64);
    return 0;
}

/* *D_00275B40 array entry i (a bone record). */
static int work_bone(const EmPlayerEquipment *e, unsigned i, EmOwnerBone **out)
{
    const EmPlayerEquipmentWorld *d = e->world;
    if (i >= d->d00275B40_count || !d->d00275B40[i])
        return fail(e, 0x00275B40u, EM_PLAYER_EQUIPMENT_FAULT_BAD_INDEX);
    *out = d->d00275B40[i];
    return 0;
}

static int work_world(const EmPlayerEquipment *e, unsigned i, uint32_t out[16])
{
    EmOwnerBone *b;
    STEP(work_bone(e, i, &b));
    memcpy(out, b->world, 64);
    return 0;
}

/* copy_qw4 (00102958): 64 bytes into bone i's world matrix. */
static int set_work_world(const EmPlayerEquipment *e, unsigned i, const uint32_t m[16])
{
    EmOwnerBone *b;
    STEP(work_bone(e, i, &b));
    memcpy(b->world, m, 64);
    return 0;
}

/* One 16-byte row of the ELF data window at original address `address`. */
static int row(const EmPlayerEquipment *e, uint32_t address, uint32_t out[4])
{
    const EmPlayerEquipmentWorld *d = e->world;
    uint32_t index = (address - EM_PLAYER_EQUIPMENT_TABLE_BASE) / 4u;
    if (address < EM_PLAYER_EQUIPMENT_TABLE_BASE || index + 4u > d->table_words)
        return fail(e, address, EM_PLAYER_EQUIPMENT_FAULT_BAD_INDEX);
    memcpy(out, d->table + index, 16);
    return 0;
}

static void spad_put(uint32_t *window, unsigned index, const uint32_t *v, unsigned words)
{
    memcpy(window + index, v, 4u * words);
}

/* ---- SDK worker calls ----------------------------------------------------- */

static int apply(const EmPlayerEquipment *e, const uint32_t m[16], const uint32_t v[4], uint32_t out[4])
{
    uint32_t r[4];
    CALL(0x001026A0u, e->workers->w_001026A0(e->workers->ctx, m, v, r));
    memcpy(out, r, 16);
    return 0;
}

/* ---- 00188630: flavour 0 -------------------------------------------------- */

int em_player_equipment_00188630(const EmPlayerEquipment *e, EmPEN *act)
{
    if (latched(e)) return -1;
    STEP(need_common(e));
    STEP(need_00188630(e));
    const EmPlayerEquipmentWorkers *w = e->workers;
    const EmPlayerEquipmentWorld *d = e->world;
    uint32_t *s = d->spad3600;
    uint32_t m[16], v[4], a[4], b[4];

    unsigned mode = pb(e, P_MODE);                     /* D_00810525 */
    STEP(player_world(e, 4, m));                         /* D_008103D0[0] + 0x90 */
    STEP(set_work_world(e, 0, m));                       /* copy_qw4 into *D_00275B40 + 0x90 */

    unsigned sel;
    if (mode == 0 && *d->d00810CA4 == 0) sel = 7;
    else if (mode == 0 && *d->d00810CA4 == 2) sel = 6;
    else sel = mode;
    unsigned off = sel * 16u;

    uint32_t r[4];
    STEP(row(e, T_A220 + off, r));                       /* D_0024A224[sel * 4] = word 1 */
    s[S3600 + 0] = MINUS3;
    s[S3600 + 1] = r[1];
    s[S3600 + 2] = ZERO;
    s[S3600 + 3] = ONE;
    uint32_t pm[16];
    memcpy(pm, d->player + P_MATRIX, 64);                /* D_00810550 */
    memcpy(v, s + S3600, 16);
    STEP(apply(e, pm, v, act->vA0));                     /* +0xA0 */
    memcpy(pm, d->player + P_MATRIX, 64);
    STEP(row(e, T_A220 + off, v));
    STEP(apply(e, pm, v, act->vB0));                     /* +0xB0 */
    memcpy(a, act->vB0, 16);
    memcpy(b, act->vA0, 16);
    CALL(0x001028D0u, w->w_001028D0(w->ctx, a, b, r));
    memcpy(act->vC0, r, 16);                             /* +0xC0 = +0xB0 - +0xA0 */
    memcpy(a, act->vC0, 16);
    CALL(0x00102760u, w->w_00102760(w->ctx, a, r));
    memcpy(act->vC0, r, 16);                             /* normalised in place */

    unsigned moff = mode * 16u;
    STEP(work_world(e, 0, m));
    STEP(row(e, T_A2A0 + moff, v));
    STEP(apply(e, m, v, act->v1F0));                     /* ent = +0x1F0 */
    act->w210 = 0;                                       /* ent + 0x20 */

    unsigned k = pb(e, P_ACTION);                        /* D_008104A0 */
    if (k == 0x32 || k == 0x35) {
        if (pb(e, P_ACTION2) == 1) {                     /* D_008104A1 */
            if (pb(e, P_318) == 0) CALL(0x001854E0u, w->w_001854E0(w->ctx, act));
            else CALL(0x00185760u, w->w_00185760(w->ctx, act));
        }
    } else if (k == 0x31 || k == 0x34) {
        if (pb(e, P_ACTION2) == 1) {
            if (pb(e, P_2F2) != 0) {                     /* D_008105A2 */
                if (pb(e, P_318) == 0) CALL(0x00185760u, w->w_00185760(w->ctx, act));
                else CALL(0x001854E0u, w->w_001854E0(w->ctx, act));
            }
        }
    }

    switch (pb(e, P_MODE)) {                             /* re-read after the hooks */
    case 0:
        if (act->h2E != 0) {
            act->h2E = 0;
            CALL(0x001861C0u, w->w_001861C0(w->ctx, act));
            CALL(0x00187CC0u, w->w_00187CC0(w->ctx, act));
            STEP(work_world(e, 0, m));
            spad_put(s, S36A0, m, 16);                    /* copy_qw4(0x700036A0, ...) */
            STEP(work_world(e, 0, m));
            STEP(row(e, T_A300 + moff, v));
            STEP(apply(e, m, v, r));
            spad_put(s, S36D0, r, 4);                     /* 0x700036D0 */
            CALL(0x001F4010u, w->w_001F4010(w->ctx, 3, s + S36A0));
        }
        break;
    case 1:
    case 2:
        if (act->h2E != 0) {
            act->h2E = 0;
            CALL(0x001869A0u, w->w_001869A0(w->ctx, act));
            CALL(0x001B61C0u, w->w_001B61C0(w->ctx, 0, 0xE8, 0xF, 1));
        }
        break;
    case 3:
        if (act->h2E != 0) {
            act->h2E = 0;
            CALL(0x00186A60u, w->w_00186A60(w->ctx, act));
            CALL(0x00187CC0u, w->w_00187CC0(w->ctx, act));
            CALL(0x001B61C0u, w->w_001B61C0(w->ctx, 0, 0xD8, 0xC, 1));
        }
        break;
    case 4:
        if (act->h2E == 1) {
            uint32_t x[16], y[16];
            STEP(work_world(e, 0, m));
            spad_put(s, S36A0, m, 16);                    /* copy_qw4(0x700036A0, ...) */
            CALL(0x001029C0u, w->w_001029C0(w->ctx, x));
            spad_put(s, S36E0, x, 16);                    /* 0x700036E0 = identity */
            memcpy(x, s + S36E0, 64);
            CALL(0x00102BB0u, w->w_00102BB0(w->ctx, x, HALF_PI, y));
            spad_put(s, S36E0, y, 16);
            memcpy(x, s + S36A0, 64);
            memcpy(y, s + S36E0, 64);
            CALL(0x001026D0u, w->w_001026D0(w->ctx, x, y, m));
            spad_put(s, S36A0, m, 16);                    /* 0x700036A0 = 36A0 x 36E0 */
            spad_put(s, S36D0, act->vB0, 3);              /* 001031E0(0x700036D0, +0xB0) */
            CALL(0x001EFEB0u, w->w_001EFEB0(w->ctx, 0x80000039u, s + S36A0));
            CALL(0x001B61C0u, w->w_001B61C0(w->ctx, 0, 0x65, 5, 1));
        }
        act->h2E = 0;
        break;
    case 5:
        if (act->h2E != 0) {
            act->h2E = 0;
            CALL(0x001872C0u, w->w_001872C0(w->ctx, act));
            CALL(0x001B61C0u, w->w_001B61C0(w->ctx, 1, 0xF8, 0x12, 1));
        }
        break;
    default:
        break;
    }
    return 0;
}

/* ---- 00188AC0 / 00188B80 / 00188A50: flavour 1 ----------------------------- */

static int body_00188AC0(const EmPlayerEquipment *e, EmPEN *node)
{
    const EmPlayerEquipmentWorld *d = e->world;
    uint32_t m[16];
    if (*d->d00810CA4 == 2 || *d->d00810CA4 == 0) {
        node->drawn = 0;                                  /* +0x01 */
    } else {
        STEP(player_world(e, 4, m));
        STEP(set_work_world(e, 0, m));
    }
    uint8_t st = node->b05;                               /* +0x05 */
    switch (st) {
    case 0:
        if (*d->d008106CC != 0) node->b05 = (uint8_t)(st + 1);
        break;
    case 1:
        *d->d008106C6 = 0;
        CALL(0x0015C310u, e->workers->w_0015C310(e->workers->ctx, 1));
        *d->d008106CC = 0;
        node->b05 = 0;
        break;
    default:
        break;
    }
    return 0;
}

int em_player_equipment_00188AC0(const EmPlayerEquipment *e, EmPEN *node)
{
    if (latched(e)) return -1;
    STEP(need_common(e));
    STEP(need_00188AC0(e));
    return body_00188AC0(e, node);
}

static int body_00188B80(const EmPlayerEquipment *e)
{
    const EmPlayerEquipmentWorld *d = e->world;
    uint32_t m[16];
    int16_t clip = ph(e, 0x20C);
    uint32_t t = pw(e, 0x3C);
    if (pb(e, P_STATE) == 1 && pb(e, P_MODE) == 0 && pb(e, P_ACTION) == 0x33 &&
        (clip == *d->d00248B98 || clip == *d->d00248C78) &&
        !em_ee_c_lt_bits(t, TWELVE) && em_ee_c_le_bits(t, F51)) {
        STEP(player_world(e, 19, m));                      /* +0x15C */
    } else {
        STEP(player_world(e, 4, m));                       /* +0x120 */
    }
    return set_work_world(e, 0, m);
}

int em_player_equipment_00188B80(const EmPlayerEquipment *e)
{
    if (latched(e)) return -1;
    STEP(need_common(e));
    NEED(e->world->d00248B98, 0x00248B98u);
    NEED(e->world->d00248C78, 0x00248C78u);
    return body_00188B80(e);
}

static int body_00188A50(const EmPlayerEquipment *e, EmPEN *node)
{
    switch (node->variant) {                              /* +0x0D */
    case 0:
        return body_00188AC0(e, node);
    case 0x10:
        return body_00188B80(e);
    case 0x15:
        CALL(0x00188C70u, e->workers->w_00188C70(e->workers->ctx, node));
        return 0;
    default:
        return 0;
    }
}

int em_player_equipment_00188A50(const EmPlayerEquipment *e, EmPEN *node)
{
    if (latched(e)) return -1;
    STEP(need_common(e));
    STEP(need_00188A50(e));
    return body_00188A50(e, node);
}

/* ---- 00188ED0 / 00188DF0: flavour 2 ---------------------------------------- */

static int body_00188ED0(const EmPlayerEquipment *e, EmPEN *node)
{
    const EmPlayerEquipmentWorld *d = e->world;
    const EmPlayerEquipmentWorkers *w = e->workers;
    uint32_t m[16];
    for (unsigned i = 0; i < node->bone_count; i++) {     /* +0x0C re-read per pass */
        STEP(player_world(e, 4, m));
        STEP(set_work_world(e, i, m));
    }
    if (*d->d008106C7 == 0)
        return 0;
    uint8_t st = pb(e, P_STATE), sub = pb(e, P_SUB);
    int setup = (st == 1 && (sub == 0x1D || (unsigned)(sub - 0x1E) <= 2u)) ||
                (st == 2 && (sub == 0x17 || sub == 0x18));
    if (!setup) {
        if (*d->d008106C7 != 0) *d->d008106C7 = 0;
        return 0;
    }
    uint32_t *s = d->spad38A0;
    s[S38C0 + 0] = F3_6;
    s[S38C0 + 1] = HALF;
    s[S38C0 + 2] = ZERO;
    s[S38C0 + 3] = ONE;
    uint32_t v[4];
    STEP(work_world(e, 0, m));
    memcpy(v, s + S38C0, 16);
    STEP(apply(e, m, v, node->vB0));                       /* +0xB0 */
    int32_t mode = (pb(e, P_ACTION) == 0x31 || pb(e, P_ACTION) == 0x34) ? 0 : 1;
    uint32_t flags;
    CALL(0x001B0070u, w->w_001B0070(w->ctx, &flags));
    CALL(0x00187780u, w->w_00187780(w->ctx, node, (flags & 0x80u) ? 0 : 1, mode));
    return 0;
}

int em_player_equipment_00188ED0(const EmPlayerEquipment *e, EmPEN *node)
{
    if (latched(e)) return -1;
    STEP(need_common(e));
    STEP(need_00188ED0(e));
    return body_00188ED0(e, node);
}

static int body_00188DF0(const EmPlayerEquipment *e, EmPEN *node)
{
    const EmPlayerEquipmentWorkers *w = e->workers;
    const EmPlayerEquipmentWorld *d = e->world;
    uint32_t m[16];
    switch (node->variant) {                               /* +0x0D */
    case 0:
        return body_00188ED0(e, node);
    case 1:
        CALL(0x00189090u, w->w_00189090(w->ctx, node));
        return 0;
    case 2:
        CALL(0x00189330u, w->w_00189330(w->ctx, node));
        return 0;
    case 3:
        CALL(0x001899C0u, w->w_001899C0(w->ctx, node));
        return 0;
    case 0xC:
        CALL(0x00189A20u, w->w_00189A20(w->ctx, node));
        return 0;
    default:
        STEP(player_world(e, 4, m));
        memcpy(node->mD0, m, 64);                          /* copy_qw4(+0xD0, ...) */
        memcpy(m, node->mD0, 64);
        CALL(0x001C9610u, w->w_001C9610(w->ctx, d->d00275B40, d->d00275B40_count, node->bone_count, m));
        return 0;
    }
}

int em_player_equipment_00188DF0(const EmPlayerEquipment *e, EmPEN *node)
{
    if (latched(e)) return -1;
    STEP(need_common(e));
    STEP(need_00188DF0(e));
    return body_00188DF0(e, node);
}

/* ---- 00189D30 / 0018A1F0: flavour 4 ---------------------------------------- */

static int body_00189D30(const EmPlayerEquipment *e, EmPEN *node)
{
    const EmPlayerEquipmentWorkers *w = e->workers;
    uint32_t *s = e->world->spad38A0;
    uint8_t state = node->b07;                            /* +0x07 */
    switch (state) {
    case 0:
        if (node->status == 1) {
            node->b07 = (uint8_t)(state + 1);
            s[S38A0 + 0] = ZERO;
            s[S38A0 + 1] = ZERO;
            s[S38A0 + 2] = ZERO;
            s[S38B0 + 0] = ZERO;
            s[S38D0 + 0] = ZERO;
            s[S38D0 + 1] = ZERO;
            s[S38D0 + 2] = ZERO;
            s[S38D0 + 3] = ZERO;
            s[S38A0 + 3] = ONE;
            s[S38B0 + 1] = FIVE;
            s[S38B0 + 2] = ZERO;
            s[S38B0 + 3] = ONE;
            s[S38C0 + 0] = F64;
            s[S38C0 + 1] = F64;
            s[S38C0 + 2] = F64;
            s[S38C0 + 3] = F128;
            /* *(+0x14) is the record itself; its +0x110 slot 0 + 0x90. */
            EmPEN *self = node->self;
            if (!self || !self->bone[0])
                return fail(e, 0x00189D30u, EM_PLAYER_EQUIPMENT_FAULT_BAD_INDEX);
            uint8_t *effect = NULL;
            CALL(0x001EFF10u, w->w_001EFF10(w->ctx, 0x8000000Du, self->bone[0], s + S38A0, s + S38B0,
                                            s + S38C0, s + S38D0, TEN, &effect));
            node->effect = effect;                        /* +0x20 */
        }
        break;
    case 1:
        if (node->status == 2) {
            if (!node->effect) return fail(e, 0x00189D30u, EM_PLAYER_EQUIPMENT_FAULT_BAD_INDEX);
            node->effect[4] = 2;
            node->b07 = (uint8_t)(node->b07 - 1);
        } else if (pb(e, P_ACTION) != 0x36 && pb(e, P_ACTION) != 0x37) {
            if (!node->effect) return fail(e, 0x00189D30u, EM_PLAYER_EQUIPMENT_FAULT_BAD_INDEX);
            node->effect[4] = 2;
            node->status = 2;
            node->b07 = (uint8_t)(node->b07 - 1);
        }
        break;
    default:
        break;
    }
    return 0;
}

int em_player_equipment_00189D30(const EmPlayerEquipment *e, EmPEN *node)
{
    if (latched(e)) return -1;
    STEP(need_common(e));
    STEP(need_00189D30(e));
    return body_00189D30(e, node);
}

/* The hit block of 0018A1F0 (both arms run the same body). */
static int fire_hit(const EmPlayerEquipment *e, EmPEN *node)
{
    const EmPlayerEquipmentWorkers *w = e->workers;
    const EmPlayerEquipmentHitState *h = e->world->hit;
    uint32_t *s = e->world->spad38A0;
    uint16_t kind;
    uint32_t normal[3];
    spad_put(s, S38C0, h->point, 3);                      /* 001031E0(0x700038C0, 0x700031B0) */
    CALL(0x700031D0u, w->w_face_record(w->ctx, h->record, &kind, normal));
    s[S38B0 + 0] = normal[0];                             /* record +0x24 */
    s[S38B0 + 1] = normal[1];                             /* record +0x28 */
    s[S38B0 + 2] = normal[2];                             /* record +0x2C */
    s[S38B0 + 3] = ONE;
    spad_put(s, S38A0, h->point, 4);                      /* 00102948(0x700038A0, 0x700031B0) */
    CALL(0x001F00A0u, w->w_001F00A0(w->ctx, 0x80000003u, s + S38A0, s + S38B0, 0));
    CALL(0x0018A180u, w->w_0018A180(w->ctx, node));
    return 0;
}

static int face_is_2000(const EmPlayerEquipment *e, int *yes)
{
    const EmPlayerEquipmentWorkers *w = e->workers;
    const EmPlayerEquipmentHitState *h = e->world->hit;
    uint16_t kind;
    uint32_t normal[3];
    if (!h->record) return fail(e, 0x700031D0u, EM_PLAYER_EQUIPMENT_FAULT_BAD_INDEX);
    CALL(0x700031D0u, w->w_face_record(w->ctx, h->record, &kind, normal));
    *yes = (kind & 0xFF00u) == 0x2000u;                   /* record +0x1A */
    return 0;
}

/* 0019A570(from, to, 7, 0x20); on a result of 1 or 2, 00189FE0(node,
 * report_a, report_b). The report vectors are read from their storage after
 * the query, as the original passes their addresses. */
static int probe(const EmPlayerEquipment *e, EmPEN *node, const uint32_t *from_at, const uint32_t *to_at,
                 const uint32_t *report_a, const uint32_t *report_b)
{
    const EmPlayerEquipmentWorkers *w = e->workers;
    uint32_t from[4], to[4];
    int32_t n;
    memcpy(from, from_at, 16);
    memcpy(to, to_at, 16);
    CALL(0x0019A570u, w->w_0019A570(w->ctx, from, to, 7, 0x20, &n));
    if (n != 0 && (uint32_t)(n - 1) < 2u) {
        uint32_t a[4], b[4];
        memcpy(a, report_a, 16);
        memcpy(b, report_b, 16);
        CALL(0x00189FE0u, w->w_00189FE0(w->ctx, node, a, b));
    }
    return 0;
}

static int body_0018A1F0(const EmPlayerEquipment *e, EmPEN *node)
{
    const EmPlayerEquipmentWorkers *w = e->workers;
    const EmPlayerEquipmentWorld *d = e->world;
    uint32_t *s = d->spad38A0;
    uint32_t m[16], v[4], a[4], b[4], r[4];

    STEP(player_world(e, 14, m));                         /* D_008103F8[0] + 0x90 */
    STEP(set_work_world(e, 0, m));

    if (node->status & 1) {
        CALL(0x001AA840u, w->w_001AA840(w->ctx, node));
        s[S38A0 + 0] = QUARTER;
        s[S38A0 + 1] = ONE;
        s[S38A0 + 2] = ZERO;
        s[S38A0 + 3] = ONE;
        STEP(work_world(e, 0, m));
        memcpy(v, s + S38A0, 16);
        STEP(apply(e, m, v, r));
        spad_put(s, S38A0, r, 4);
        int32_t found;
        pvec(e, P_POS, a);
        memcpy(b, s + S38A0, 16);
        CALL(0x0019B2C0u, w->w_0019B2C0(w->ctx, a, b, 6, &found));
        if (found != 0) {
            const EmPlayerEquipmentHitState *h = d->hit;
            int yes;
            if (h->entity) {
                int32_t lock;
                CALL(0x00189EC0u, w->w_00189EC0(w->ctx, h->entity, &lock));
                if (lock == 0) {
                    STEP(face_is_2000(e, &yes));
                    if (yes) STEP(fire_hit(e, node));
                }
            } else {
                STEP(face_is_2000(e, &yes));
                if (yes) STEP(fire_hit(e, node));
            }
        }
    }

    if (node->status & 1) {                                /* re-read: 0018A180 writes +0x00 */
        uint32_t sp30[4], sp40[4], sp50[4];
        STEP(work_world(e, 0, m));
        STEP(row(e, T_A440, v));
        STEP(apply(e, m, v, sp30));
        STEP(work_world(e, 0, m));
        STEP(row(e, T_A440 + 0x10u, v));                   /* D_0024A450 */
        STEP(apply(e, m, v, sp40));
        STEP(probe(e, node, sp30, sp40, sp40, sp30));
        for (unsigned i = 0; i < 2; i++) {
            STEP(work_world(e, 0, m));
            STEP(row(e, T_A440 + (i + 2u) * 0x10u, v));
            STEP(apply(e, m, v, sp50));
            CALL(0x001028B8u, w->w_001028B8(w->ctx, sp50, sp30, r));
            spad_put(s, S38A0, r, 4);
            CALL(0x001028B8u, w->w_001028B8(w->ctx, sp50, sp40, r));
            spad_put(s, S38B0, r, 4);
            STEP(probe(e, node, s + S38A0, s + S38B0, s + S38B0, s + S38A0));
        }
        for (unsigned i = 0; i < 2; i++) {
            STEP(work_world(e, 0, m));
            STEP(row(e, T_A440 + (i + 4u) * 0x10u, v));
            STEP(apply(e, m, v, sp50));
            CALL(0x001028B8u, w->w_001028B8(w->ctx, sp50, sp30, r));
            spad_put(s, S38C0, r, 4);
            CALL(0x001028B8u, w->w_001028B8(w->ctx, sp50, sp40, r));
            spad_put(s, S38D0, r, 4);
            STEP(probe(e, node, s + S38C0, s + S38D0, s + S38D0, s + S38C0));
        }
        STEP(work_world(e, 0, m));
        STEP(row(e, T_A4A0, v));
        STEP(apply(e, m, v, r));
        spad_put(s, S38A0, r, 4);
        pvec(e, P_POS, r);
        spad_put(s, S38B0, r, 4);                          /* 00102948(0x700038B0, D_00810360) */
        s[S38B0 + 1] = s[S38A0 + 1];                       /* 0x700038B4 = 0x700038A4 */
        STEP(probe(e, node, s + S38B0, s + S38A0, s + S38A0, s + S38B0));
    }

    return body_00189D30(e, node);
}

int em_player_equipment_0018A1F0(const EmPlayerEquipment *e, EmPEN *node)
{
    if (latched(e)) return -1;
    STEP(need_common(e));
    STEP(need_0018A1F0(e));
    return body_0018A1F0(e, node);
}

/* ---- 0018A8D0: init -------------------------------------------------------- */

static int body_0018A8D0(const EmPlayerEquipment *e, EmPEN *p, int32_t id, int32_t *result)
{
    const EmPlayerEquipmentWorkers *w = e->workers;
    const EmPlayerEquipmentWorld *d = e->world;
    switch (p->flavour) {                                  /* +0x03 */
    case 0:
        id = 0x2F;
        p->h2E = 0;
        p->w210 = 0;                                       /* q + 0x20 */
        p->w214 = 0;                                       /* q + 0x24 */
        break;
    case 1:
        switch (p->variant) {
        case 0: id = 0x30; break;
        case 0x10: id = 0x40; break;
        case 0x15: id = 0x6D; break;
        default: break;
        }
        break;
    case 2:
        switch (p->variant) {
        case 0: id = 0x32; break;
        case 1: id = 0x33; break;
        case 2: id = 0x34; break;
        case 3: id = 0x35; break;
        case 4: id = 0x36; break;
        case 5: id = 0x31; break;
        case 6: id = 0x37; break;
        case 7: id = 0x38; break;
        case 8: id = 0x39; *d->d008106C6 = 1; break;
        case 9: id = 0x3A; *d->d008106C6 = 2; break;
        case 10: id = 0x3B; *d->d008106C6 = 3; break;
        case 11: id = 0x3C; *d->d008106C6 = 4; break;
        case 12: id = 0x3D; *d->d008106C6 = 5; break;
        default: break;
        }
        break;
    case 4:
        id = 0x6A;
        p->h28 = 0;
        break;
    default:
        *result = 1;
        return 0;
    }

    uint32_t handle;
    CALL(0x001C6120u, w->w_001C6120(w->ctx, *d->d0028A56C, (uint32_t)id, &handle));
    CALL(0x001CA6E0u, w->w_001CA6E0(w->ctx, p, handle));
    uint8_t count;
    CALL(0x001C6150u, w->w_001C6150(w->ctx, p->model, &count));
    p->bone_count = count;                                 /* +0x0C */
    if (*d->d00275BCC < (int)p->bone_count) {
        *result = 1;
        return 0;
    }
    for (unsigned i = 0; i < p->bone_count; i++) {
        EmOwnerBone *slot = NULL;
        if (i >= EM_PLAYER_EQUIPMENT_MAX_BONES)
            return fail(e, 0x0018A8D0u, EM_PLAYER_EQUIPMENT_FAULT_BAD_INDEX);
        CALL(0x001AF780u, w->w_001AF780(w->ctx, &slot));
        p->bone[i] = slot;                                 /* +0x110 + 4 * i */
    }
    p->bones_held = p->bone_count;                         /* +0x09 */
    CALL(0x001CB5B0u, w->w_anim_bone_array_setup(w->ctx, p->bone_count));
    CALL(0x001C62C0u, w->w_bone_init_default_1(w->ctx, p));
    *result = 0;
    return 0;
}

int em_player_equipment_0018A8D0(const EmPlayerEquipment *e, EmPEN *node, int32_t id, int32_t *result)
{
    if (latched(e)) return -1;
    if (!result) return fail(e, 0x0018A8D0u, EM_PLAYER_EQUIPMENT_FAULT_NULL_WORKER);
    STEP(need_common(e));
    STEP(need_0018A8D0(e));
    return body_0018A8D0(e, node, id, result);
}

/* ---- 0018A6B0: the node behaviour ------------------------------------------ */

int em_player_equipment_tick(const EmPlayerEquipment *e, EmPEN *node)
{
    if (latched(e)) return -1;
    if (!node) return fail(e, 0x0018A6B0u, EM_PLAYER_EQUIPMENT_FAULT_NULL_WORKER);
    STEP(need_common(e));
    STEP(need_tick(e));
    const EmPlayerEquipmentWorkers *w = e->workers;
    const EmPlayerEquipmentWorld *d = e->world;
    uint8_t st = node->state;                              /* +0x04 */
    switch (st) {
    case 0: {
        int32_t refused;
        STEP(body_0018A8D0(e, node, st, &refused));        /* a1 = the +0x04 byte (0) */
        if (refused == 0) node->state = 1;
        break;
    }
    case 1:
        if (pb(e, 0x001) != 0 || pb(e, P_ACTION) == 0x33) /* D_008102B1, D_008104A0 */
            node->drawn = 1;
        switch (node->flavour) {
        case 0:
            STEP(em_player_equipment_00188630(e, node));
            break;
        case 1:
            STEP(body_00188A50(e, node));
            break;
        case 2:
            if (*d->d008106CC != 0) {
                if (node->variant == 0) {
                    if (*d->d00810CA6 != 0 && *d->d008106C7 != 0)
                        *d->d008106C7 = 0;
                }
                node->state = 3;
            } else {
                STEP(body_00188DF0(e, node));
            }
            break;
        case 4:
            STEP(body_0018A1F0(e, node));
            break;
        default:
            break;
        }
        /* D_008106C6, D_008104E0 (player +0x230, a word), D_008104A1. */
        if ((*d->d008106C6 == 0 || pw(e, 0x230) != 0xCu || pb(e, P_ACTION2) != 1) && node->drawn != 0)
            CALL(0x0018A6B0u, w->w_method(w->ctx, node, node->method));
        break;
    case 2:
        break;
    case 3:
        CALL(0x001AFC10u, w->w_001AFC10(w->ctx, node));
        break;
    default:
        break;
    }
    return 0;
}

/* ---- 0015D2F0 -------------------------------------------------------------- */

int em_player_equipment_0015D2F0(const uint8_t *p, int32_t *result)
{
    if (!p || !result) return -1;
    int32_t r = 0;
    if (p[4] == 1) {                                        /* D_008102B4 */
        uint8_t v = p[5];
        if (v == 29 || v == 31) {
            if (p[0x1F1] == 1) r = p[0x318] == 1 ? 2 : p[0x318] == 3 ? 0 : 1;
            else r = p[0x318] == 2 ? 1 : 0;
        } else if (v == 30 || v == 32) {
            if (p[0x1F1] == 1) r = p[0x318] == 1 ? 1 : p[0x318] == 3 ? 0 : 2;
            else r = p[0x318] == 2 ? 2 : 0;
        } else if (v == 35) {
            r = p[0x1F1] == 1 ? 0x82 : 0;
        } else {
            r = v == 25 ? 3 : 0;
        }
    }
    *result = r;
    return 0;
}
