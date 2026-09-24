/* em_locomotion_display.c - the idle / walk states and the gait display (see
 * em_locomotion_display.h and docs/LOCOMOTION_DISPLAY.md).
 *
 * 00161020, 001612D0 and 0017B660 are NEARMISS in the decomp; they were
 * translated from the original instructions (build/asm), which differ from
 * the readable C in these places:
 *   - 00161020 +6 = 1 and +6 = 2: when D_0028A9A0, 001607D0 or 00160220 stop
 *     the state, the routine returns at once (00161278 is skipped); the C
 *     falls through to the shared tail. 001607D0 receives the record, not
 *     the state byte.
 *   - 001612D0: the same early returns (the epilogue 00161678); 001764E0 sees
 *     the caller's $s1 except after the +6 = 2 resume path, which leaves the
 *     0017B490 clip id in $s1 (00161490).
 *   - 0017B660: 0017B490 receives the record as its first argument (the C
 *     drops it); the cross-fade frame reloads +3C after the first 00179D20
 *     (0017B7D0), and D_00275B40 and +208 are reloaded on every node.
 * Every address in a comment is the original instruction translated there.
 * tools/test_locomotion_display_reference.py executes the original
 * instructions and compares every byte they write. */
#include "game/em_locomotion_display.h"
#include "game/em_ee_float.h"
#include "game/em_owner_services_original.h"

#include <stddef.h>
#include <string.h>

/* Float constants as the instructions build them. */
#define F_ZERO      UINT32_C(0x00000000)
#define F_ONE       UINT32_C(0x3F800000)
#define F_TWO       UINT32_C(0x40000000)
#define F_FOUR      UINT32_C(0x40800000)
#define F_SIX       UINT32_C(0x40C00000)
#define F_EIGHT     UINT32_C(0x41000000)
#define F_TWELVE    UINT32_C(0x41400000)
#define F_4096      UINT32_C(0x45800000)
#define F_0_75      UINT32_C(0x3F400000)
#define F_PI        UINT32_C(0x40490FDB)
#define F_M0_2      UINT32_C(0xBE4CCCCD)
#define F_M0_4      UINT32_C(0xBECCCCCD)
#define F_M0_8      UINT32_C(0xBF4CCCCD)

#define NODE_BYTES  0xD0u

static uint32_t w32(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void put32(EmPlayerLiveActor *a, unsigned at, uint32_t v) { em_live_set_u32(a, at, v); }
static uint8_t b8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void put8(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u8(a, at, (uint8_t)v); }
static int16_t h16(const EmPlayerLiveActor *a, unsigned at) { return (int16_t)em_live_u16(a, at); }
static void put16(EmPlayerLiveActor *a, unsigned at, int v) { em_live_set_u16(a, at, (uint16_t)v); }

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static int fault(EmLocoHost *h, uint32_t address)
{
    if (h && h->fault == 0) h->fault = address;
    return -1;
}

#define CALL(h, address, expr) do { if ((expr) < 0) return fault((h), (address)); } while (0)
#define STEP(expr) do { if ((expr) < 0) return -1; } while (0)

/* The host bytes of [address, address + len) in the pose host's regions:
 * the first region holding the whole range, NULL when none does or when a
 * write reaches a read-only region (em_pose_host_workers' rule). */
static uint8_t *emap(const EmPoseHost *p, uint32_t address, uint32_t len, int write)
{
    if (!p) return NULL;
    for (unsigned i = 0; i < p->region_count && i < EM_POSE_REGION_MAX; ++i) {
        const EmPoseRegion *r = &p->region[i];
        if (!r->bytes || len > r->size) continue;
        uint32_t off = address - r->address;
        if (address < r->address || off > r->size - len) continue;
        if (write && !r->writable) return NULL;
        return r->bytes + off;
    }
    return NULL;
}

static float fl(uint32_t bits) { return em_ee_float(bits); }

/* ---- binding check ------------------------------------------------------ */

int em_loco_bound(const EmLocoHost *h)
{
    if (!h) return 0;
    const EmLocoWorkers *w = &h->workers;
    if (!(w->actions && w->ladder && w->heading && w->row_request && w->request && w->arbiter &&
          w->clip_frames && w->probes && w->floor && w->clearance && w->fall_check && w->motor &&
          w->translate && w->use_scan && w->use_accepted && w->handoff && w->reentry &&
          w->foot_stop && w->sound && w->effect && w->wrap && w->mode))
        return 0;
    const EmLocoScene *s = &h->scene;
    if (!(s->d28A9A0 && s->d810E74 && s->spad3B76 && s->caller_s1)) return 0;
    const EmLocoDisplay *d = &h->display;
    if (!d->pose || !d->pose->globals || !d->rest || !d->d275B40) return 0;
    const EmPoseGlobals *g = d->pose->globals;
    if (!(g->spad3400 && g->spad3440 && g->spad3600 && g->spad3760 && g->spad3A20)) return 0;
    const EmAnimRest *r = d->rest;
    if (!r->workers.w_0011E748 || !r->world.spad34C0 || !r->world.spad34D0 || !r->world.spad34E0)
        return 0;
    if (r->world.spad3760 != g->spad3760) return 0;   /* one scratchpad */
    if (r->fault.code != 0) return 0;                  /* a latched 001C9D50 fault */
    return 1;
}

static int check(EmLocoHost *h, uint32_t entry)
{
    if (!h) return -1;
    return em_loco_bound(h) ? 0 : fault(h, entry);
}

/* ---- the private SDK leaves --------------------------------------------- */

/* 001026D0(dst, a, b): a's four rows are loaded first; then each row v of b
 * gives dst's row: ACC = a0 * v.x, ACC += a1 * v.y, ACC += a2 * v.z, then
 * row = ACC + a3 * v.w (all four lanes), stored before the next row of b is
 * loaded. dst may alias a or b. */
int em_loco_001026D0(uint32_t dst[16], const uint32_t a[16], const uint32_t b[16])
{
    uint32_t m[16];
    memcpy(m, a, sizeof m);
    for (unsigned row = 0; row < 4; ++row) {
        uint32_t v[4], acc[4] = {0, 0, 0, 0}, out[4] = {0, 0, 0, 0};
        memcpy(v, b + 4 * row, sizeof v);
        int st = em_vu_vec_bits(EM_VU_MULABC, 15, 0, m + 0, v, 0, NULL, acc);
        if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDABC, 15, 1, m + 4, v, 0, acc, acc);
        if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDABC, 15, 2, m + 8, v, 0, acc, acc);
        if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDBC, 15, 3, m + 12, v, 0, acc, out);
        if (st != EM_EE_FLOAT_OK) return -1;
        memcpy(dst + 4 * row, out, sizeof out);
    }
    return 0;
}

/* 00103230(dst, src, s): the quadword src with x, y, z times s (the x lane
 * of the register s is moved into); w is carried over. */
int em_loco_00103230(uint32_t dst[4], const uint32_t src[4], uint32_t scale)
{
    uint32_t v[4], t[4] = {scale, 0, 0, 0};
    memcpy(v, src, sizeof v);
    if (em_vu_vec_bits(EM_VU_MULBC, 14, 0, v, t, 0, NULL, v) != EM_EE_FLOAT_OK) return -1;
    memcpy(dst, v, sizeof v);
    return 0;
}

/* ---- the verified leaves over bit arrays -------------------------------- */

static int identity(uint32_t m[16])
{
    float f[16];
    memcpy(f, m, sizeof f);
    int st = em_owner_services_identity_001029C0(f);
    memcpy(m, f, sizeof f);   /* the stores made before a refusal stay */
    return st == EM_EE_FLOAT_OK ? 0 : -1;
}

static int euler(uint32_t m[16], const uint32_t angles[3])
{
    float f[16], a[3];
    memcpy(f, m, sizeof f);
    memcpy(a, angles, sizeof a);
    int st = em_owner_services_euler_00102C58(f, f, a);
    memcpy(m, f, sizeof f);   /* the stores made before a refusal stay */
    return st == EM_EE_FLOAT_OK ? 0 : -1;
}

static void load16(uint32_t out[16], const uint8_t *p)
{
    for (unsigned k = 0; k < 16; ++k) out[k] = rd32(p + 4 * k);
}
static void store16(uint8_t *p, const uint32_t m[16])
{
    for (unsigned k = 0; k < 16; ++k) wr32(p + 4 * k, m[k]);
}

/* (float)(short) / 4096: the 4.12 scale (cvt, then a COP1 division). */
static uint32_t fixed_4_12(const uint8_t *p)
{
    int16_t h = (int16_t)(p[0] | p[1] << 8);
    return em_ee_div_bits(em_ee_cvt_s_w_bits(em_ee_int_word(h)), F_4096);
}

/* Scale rows 0, 1, 2 of the 16 words at m by s[0], s[1], s[2] (00103230
 * on each row in place). */
static int scale_rows(uint32_t m[16], const uint32_t s[3])
{
    for (unsigned row = 0; row < 3; ++row)
        if (em_loco_00103230(m + 4 * row, m + 4 * row, s[row]) < 0) return -1;
    return 0;
}

/* ---- the node-pointer array --------------------------------------------- */

/* The node record i of the array D_00275B40 points at (read now, as each
 * original iteration reloads the global), writable for NODE_BYTES. */
static uint8_t *node_at(EmLocoHost *h, int i, uint32_t address)
{
    const EmPoseHost *p = h->display.pose;
    uint32_t array = *h->display.d275B40;
    const uint8_t *slot = emap(p, array + 4u * (uint32_t)i, 4, 0);
    if (!slot) { fault(h, address); return NULL; }
    uint8_t *node = emap(p, rd32(slot), NODE_BYTES, 1);
    if (!node) fault(h, address);
    return node;
}

/* Every node the display will touch is mapped (writable) before the first
 * write: the record's +C nodes of the current array. */
static int nodes_mapped(EmLocoHost *h, const EmPlayerLiveActor *a, uint32_t address)
{
    unsigned n = b8(a, 0xC);
    for (unsigned i = 0; i < n; ++i)
        if (!node_at(h, (int)i, address)) return -1;
    return 0;
}

/* ---- 0017B460 / 0017B490 ------------------------------------------------ */

int em_loco_0017B460(const EmPoseHost *p, int a, int b, int16_t *value)
{
    const uint8_t *row = emap(p, EM_LOCO_D_00248AB0 + ((uint32_t)a << 2), 4, 0);   /* 0017B470 */
    if (!row || !value) return -1;
    const uint8_t *cell = emap(p, rd32(row) + ((uint32_t)b << 1), 2, 0);           /* 0017B480 */
    if (!cell) return -1;
    *value = (int16_t)(cell[0] | cell[1] << 8);
    return 0;
}

int em_loco_0017B490(EmLocoHost *h, const EmPlayerLiveActor *a, int cmd, int idx, int tbl,
                     int16_t *clip)
{
    if (!h || !a || !clip || !h->workers.mode || !h->display.pose) return fault(h, 0x0017B490u);
    int32_t mode = 0;
    CALL(h, 0x001B0070u, h->workers.mode(h->workers.context, &mode));       /* 0017B4B8 */
    int flag = mode & 4;
    if ((unsigned)cmd >= 7) return fault(h, 0x0017B4C4u);  /* no case: $s0 would be returned */
    int gate = b8(a, 0x236) == 0 && !(b8(a, 0x235) & 1) && flag != 0;
    int arg;
    if (cmd == 1 || cmd == 6) {                                            /* 0017B53C */
        arg = gate ? tbl + 0x10 : (int)((uint32_t)tbl + ((uint32_t)idx << 2));
    } else {                                                               /* 0017B4E8 */
        arg = gate ? 4 : idx;
    }
    if (em_loco_0017B460(h->display.pose, cmd, arg, clip) < 0) return fault(h, 0x0017B460u);
    return 0;
}

/* ---- 00182D40 ----------------------------------------------------------- */

int em_loco_00182D40(const EmPlayerLiveActor *a)
{
    return b8(a, 0x1F0) == 0x17 ? 1 : 0;
}

/* ---- 00179D20: the local pose seed --------------------------------------- */

static int pose_seed(EmLocoHost *h, EmPlayerLiveActor *a)
{
    EmPoseGlobals *g = h->display.pose->globals;
    uint8_t *node = node_at(h, 0, 0x00179D40u);
    if (!node) return -1;
    uint32_t m[16], angles[3], s[3];
    load16(m, node + 0x90);
    int st = identity(m);                                                  /* 00179D44 */
    store16(node + 0x90, m);
    if (st < 0) return fault(h, 0x001029C0u);
    for (unsigned k = 0; k < 3; ++k) angles[k] = rd32(node + 0x70 + 4 * k);
    st = euler(m, angles);                                                 /* 00179D54 */
    store16(node + 0x90, m);
    if (st < 0) return fault(h, 0x00102C58u);
    wr32(node + 0xC0, rd32(node + 0x7C));                                  /* 00179D70 */
    wr32(node + 0xC4, rd32(node + 0x80));
    wr32(node + 0xC8, rd32(node + 0x84));
    for (unsigned row = 0; row < 3; ++row) {                               /* 00179D84.. */
        uint32_t v[4];
        for (unsigned k = 0; k < 4; ++k) v[k] = rd32(node + 0x90 + 16 * row + 4 * k);
        CALL(h, 0x00103230u, em_loco_00103230(v, v, fixed_4_12(node + 0x88 + 2 * row)));
        for (unsigned k = 0; k < 4; ++k) wr32(node + 0x90 + 16 * row + 4 * k, v[k]);
    }
    for (int i = 1; i < (int)b8(a, 0xC); ++i) {                            /* 00179FC0 */
        uint8_t *b = node_at(h, i, 0x00179E1Cu);
        if (!b) return -1;
        CALL(h, 0x001029C0u, identity(g->spad3400));                       /* 00179E2C */
        uint32_t qa[4], qb[4], translation[3];
        for (unsigned k = 0; k < 4; ++k) { qa[k] = rd32(b + 0x30 + 4 * k); qb[k] = rd32(b + 0x40 + 4 * k); }
        em_pose_host_001CA0A0(g->spad3600, qa, qb, rd32(b + 0x50));        /* 00179E44 */
        for (unsigned k = 0; k < 3; ++k) translation[k] = rd32(b + 4 * k);
        em_pose_host_001CA1C0(g->spad3400, g->spad3600, translation, g->spad3760); /* 00179E5C */
        for (unsigned k = 0; k < 3; ++k) s[k] = rd32(b + 0x18 + 4 * k);
        CALL(h, 0x00103230u, scale_rows(g->spad3400, s));                  /* 00179E74.. */
        CALL(h, 0x001029C0u, identity(g->spad3440));                       /* 00179EB0 */
        for (unsigned k = 0; k < 3; ++k) angles[k] = rd32(b + 0x70 + 4 * k);
        CALL(h, 0x00102C58u, euler(g->spad3440, angles));                  /* 00179EC8 */
        g->spad3440[12] = rd32(b + 0x7C);                                  /* 00179EE4 */
        g->spad3440[13] = rd32(b + 0x80);
        g->spad3440[14] = rd32(b + 0x84);
        for (unsigned k = 0; k < 3; ++k) s[k] = fixed_4_12(b + 0x88 + 2 * k);
        CALL(h, 0x00103230u, scale_rows(g->spad3440, s));                  /* 00179F28.. */
        uint32_t out[16];
        CALL(h, 0x001026D0u, em_loco_001026D0(out, g->spad3400, g->spad3440)); /* 00179FB0 */
        store16(b + 0x90, out);
    }
    return 0;
}

int em_loco_00179D20(EmLocoHost *h, EmPlayerLiveActor *a)
{
    STEP(check(h, 0x00179D20u));
    if (!a) return fault(h, 0x00179D20u);
    STEP(nodes_mapped(h, a, 0x00179D20u));
    return pose_seed(h, a);
}

/* ---- 00179FF0: the world matrices ---------------------------------------- */

static int world_matrices(EmLocoHost *h, EmPlayerLiveActor *a)
{
    uint32_t out[16], pos[3], rot[3], scale[3];
    for (unsigned k = 0; k < 16; ++k) out[k] = w32(a, 0xD0 + 4 * k);
    for (unsigned k = 0; k < 3; ++k) {
        pos[k] = w32(a, 0xB0 + 4 * k);
        rot[k] = w32(a, 0xC0 + 4 * k);
        scale[k] = w32(a, 0x60 + 4 * k);
    }
    int st = em_pose_host_build_trs_matrix(h->display.pose, out, pos, rot, scale); /* 0017A014 */
    for (unsigned k = 0; k < 16; ++k) put32(a, 0xD0 + 4 * k, out[k]);
    if (st < 0) return fault(h, 0x001C94B0u);
    for (int i = 0; i < (int)b8(a, 0xC); ++i) {                            /* 0017A080 */
        uint8_t *bone = node_at(h, i, 0x0017A028u);
        if (!bone) return -1;
        int16_t parent = (int16_t)(bone[0x64] | bone[0x65] << 8);           /* 0017A038 */
        uint32_t local[16], parent_m[16], result[16];
        load16(local, bone + 0x90);
        if (parent != -1) {                                                /* 0017A044 */
            uint32_t array = *h->display.d275B40;
            const uint8_t *slot = emap(h->display.pose, array + ((uint32_t)(int32_t)parent << 2), 4, 0);
            if (!slot) return fault(h, 0x0017A04Cu);
            const uint8_t *pnode = emap(h->display.pose, rd32(slot) + 0x90, 64, 0);
            if (!pnode) return fault(h, 0x0017A058u);
            load16(parent_m, pnode);
        } else {                                                           /* 0017A06C */
            for (unsigned k = 0; k < 16; ++k) parent_m[k] = w32(a, 0xD0 + 4 * k);
        }
        CALL(h, 0x001026D0u, em_loco_001026D0(result, parent_m, local));
        store16(bone + 0x90, result);
    }
    put8(a, 0x303, 1);                                                     /* 0017A094 */
    return 0;
}

int em_loco_00179FF0(EmLocoHost *h, EmPlayerLiveActor *a)
{
    STEP(check(h, 0x00179FF0u));
    if (!a) return fault(h, 0x00179FF0u);
    STEP(nodes_mapped(h, a, 0x00179FF0u));
    return world_matrices(h, a);
}

/* ---- 0017B660 anim_matrix_player ----------------------------------------- */

static int clip_frames(EmLocoHost *h, const EmPlayerLiveActor *a, int clip, uint32_t *value)
{
    int32_t frames = 0;
    CALL(h, 0x001C61D0u, h->workers.clip_frames(h->workers.context, w32(a, 0x40), clip, &frames));
    *value = em_ee_cvt_s_w_bits(em_ee_int_word(frames));
    return 0;
}

static int arbiter(EmLocoHost *h, EmPlayerLiveActor *a, int clip, uint32_t blend, uint32_t frame)
{
    CALL(h, 0x001749F0u, h->workers.arbiter(h->workers.context, a, clip, fl(blend), fl(frame)));
    return 0;
}

/* The 64-byte copies of the current node matrices into a pose buffer
 * (copy_qw4 00102958 per node). */
static int copy_pose(EmLocoHost *h, const EmPlayerLiveActor *a, uint32_t buffer, uint32_t address)
{
    for (int i = 0; i < (int)b8(a, 0xC); ++i) {
        uint8_t *node = node_at(h, i, address);
        if (!node) return -1;
        uint8_t *dst = emap(h->display.pose, buffer + 0x40u * (uint32_t)i, 64, 1);
        if (!dst) return fault(h, address);
        memmove(dst, node + 0x90, 64);
    }
    return 0;
}

static int matrix_player(EmLocoHost *h, EmPlayerLiveActor *a)
{
    int16_t clip = 0;
    if (b8(a, 0x1F1) == 0) {                                               /* 0017B690 */
        STEP(em_loco_0017B490(h, a, 1, b8(a, 0x235), b8(a, 0x25C), &clip));
        int16_t current = h16(a, 0x20C);                                    /* 0017B6A8 */
        if (clip == current) return 0;
        uint32_t old_frames = 0, new_frames = 0;
        STEP(clip_frames(h, a, current, &old_frames));                     /* 0017B6BC */
        STEP(clip_frames(h, a, clip, &new_frames));                        /* 0017B6D0 */
        uint32_t t = em_ee_div_bits(em_ee_sub_bits(old_frames, w32(a, 0x3C)), old_frames);
        return arbiter(h, a, clip, F_ZERO, em_ee_mul_bits(new_frames, t)); /* 0017B700 */
    }
    unsigned n = b8(a, 0xC);
    if (n > EM_LOCO_POSE_NODES_MAX) return fault(h, 0x0017B790u);
    STEP(nodes_mapped(h, a, 0x0017B660u));
    if (!emap(h->display.pose, EM_LOCO_D_00288D40, 0x40u * n, 1) ||
        !emap(h->display.pose, EM_LOCO_D_00287F40, 0x40u * n, 1))
        return fault(h, 0x0017B790u);
    int tier = b8(a, 0x25C) + (b8(a, 0x1F1) == 1 ? 1 : -1);                 /* 0017B728 / 0017B744 */
    STEP(em_loco_0017B490(h, a, 1, b8(a, 0x235), tier, &clip));
    int16_t current = h16(a, 0x20C);                                        /* 0017B750 */
    uint32_t clock = w32(a, 0x3C);                                         /* 0017B758 */
    uint32_t old_frames = 0, new_frames = 0;
    STEP(clip_frames(h, a, current, &old_frames));                         /* 0017B75C */
    STEP(clip_frames(h, a, clip, &new_frames));                            /* 0017B770 */
    STEP(pose_seed(h, a));                                                 /* 0017B780 */
    STEP(copy_pose(h, a, EM_LOCO_D_00288D40, 0x0017B7ACu));
    uint32_t t = em_ee_div_bits(em_ee_sub_bits(old_frames, w32(a, 0x3C)), old_frames); /* 0017B7D0 */
    STEP(arbiter(h, a, clip, F_ZERO, em_ee_mul_bits(new_frames, t)));      /* 0017B7F0 */
    STEP(pose_seed(h, a));                                                 /* 0017B7F8 */
    STEP(copy_pose(h, a, EM_LOCO_D_00287F40, 0x0017B824u));
    for (int i = 0; i < (int)b8(a, 0xC); ++i) {                            /* 0017B890 */
        uint8_t *node = node_at(h, i, 0x0017B860u);
        if (!node) return -1;
        const uint8_t *old_pose = emap(h->display.pose, EM_LOCO_D_00288D40 + 0x40u * (uint32_t)i, 64, 0);
        const uint8_t *new_pose = emap(h->display.pose, EM_LOCO_D_00287F40 + 0x40u * (uint32_t)i, 64, 0);
        if (!old_pose || !new_pose) return fault(h, 0x0017B878u);
        uint32_t out[16], pa[16], pb[16];
        load16(out, node + 0x90);
        load16(pa, old_pose);
        load16(pb, new_pose);
        int st = em_anim_rest_001C9D50(h->display.rest, out, pa, pb, w32(a, 0x208)); /* 0017B878 */
        store16(node + 0x90, out);
        if (st < 0) return fault(h, 0x001C9D50u);
    }
    STEP(world_matrices(h, a));                                            /* 0017B8A0 */
    uint32_t back = em_ee_sub_bits(old_frames, clock);                     /* 0017B8C4 */
    if (em_ee_c_lt_bits(w32(a, 0x208), F_ONE))                             /* 0017B8B8 */
        STEP(arbiter(h, a, current, F_ZERO, back));                        /* 0017B8D0 */
    return 0;
}

int em_loco_0017B660(EmLocoHost *h, EmPlayerLiveActor *a)
{
    STEP(check(h, 0x0017B660u));
    if (!a) return fault(h, 0x0017B660u);
    return matrix_player(h, a);
}

/* ---- 0017B5C0 ----------------------------------------------------------- */

static int entry_blend(EmLocoHost *h, EmPlayerLiveActor *a)
{
    int16_t clip = 0;
    STEP(em_loco_0017B490(h, a, 1, b8(a, 0x235), 1, &clip));              /* 0017B5DC */
    uint32_t frames = 0;
    STEP(clip_frames(h, a, clip, &frames));                                /* 0017B5F0 */
    uint32_t *s3A20 = h->display.pose->globals->spad3A20;
    *s3A20 = frames;                                                       /* 0017B614 */
    const uint8_t *base = emap(h->display.pose, EM_LOCO_D_00248740 + 4u * b8(a, 0x235), 4, 0);
    if (!base) return fault(h, 0x0017B634u);
    uint32_t frame = em_ee_sub_bits(*s3A20, rd32(base));                   /* 0017B63C */
    return arbiter(h, a, clip, F_EIGHT, frame);                            /* 0017B638 */
}

int em_loco_0017B5C0(EmLocoHost *h, EmPlayerLiveActor *a)
{
    STEP(check(h, 0x0017B5C0u));
    if (!a) return fault(h, 0x0017B5C0u);
    return entry_blend(h, a);
}

/* ---- 0017C030 ----------------------------------------------------------- */

static int display(EmLocoHost *h, EmPlayerLiveActor *a)
{
    const EmLocoWorkers *w = &h->workers;
    int16_t clip = 0;
    switch (b8(a, 0x1F0)) {
    case 1:                                                                /* 0017C06C */
        if (b8(a, 0x25C) != 0) return matrix_player(h, a);
        return 0;
    case 3:                                                                /* 0017C088 */
        if (b8(a, 0x25C) == 3) {
            STEP(em_loco_0017B490(h, a, 6, b8(a, 0x235), 3, &clip));
            uint32_t frames = 0;
            STEP(clip_frames(h, a, clip, &frames));                        /* 0017C0B0 */
            uint32_t *s3A20 = h->display.pose->globals->spad3A20;
            *s3A20 = frames;                                               /* 0017C0D0 */
            STEP(arbiter(h, a, clip, F_SIX, em_ee_sub_bits(*s3A20, F_SIX))); /* 0017C0E0 */
            put8(a, 0x1F0, 4);                                             /* 0017C0F0 */
        } else {
            CALL(h, 0x0017B910u, w->foot_stop(w->context, a));             /* 0017C0F4 */
        }
        put32(a, 0x38, 0);                                                 /* 0017C100 */
        return 0;
    case 4:                                                                /* 0017C104 */
        if (b8(a, 0x23F) >= 2) {
            put8(a, 6, 0x63);                                              /* 0017C120 */
            CALL(h, 0x0017C440u, w->reentry(w->context, a, 1));
        } else if (w32(a, 0x200) & 0x1000) {
            put8(a, 0x1F0, 0);                                             /* 0017C13C */
            put8(a, 0x25C, 0);
            put8(a, 0x25E, 0x83);
        }
        return 0;
    case 5:                                                                /* 0017C150 */
        if (b8(a, 0x25C) == 1) {
            if (em_ee_c_lt_bits(w32(a, 0x268), F_ONE)) {                   /* 0017C170 */
                put8(a, 0x1F0, 0);
                put8(a, 0x25C, 0);
                put8(a, 0x25E, 0x81);
                return 0;
            }
            put32(a, 0xB0, em_ee_add_bits(w32(a, 0xB0), w32(a, 0x260)));  /* 0017C1A0 */
            put32(a, 0xB8, em_ee_add_bits(w32(a, 0xB8), w32(a, 0x264)));  /* 0017C1B0 */
            put32(a, 0x268, em_ee_sub_bits(w32(a, 0x268), F_ONE));        /* 0017C1BC */
            put32(a, 0x204, F_TWO);                                        /* 0017C1C8 */
            return 0;
        }
        if (em_ee_c_lt_bits(w32(a, 0x268), F_ONE)) {                       /* 0017C1DC */
            if (w32(a, 0x200) & 0x1000) {
                put8(a, 0x1F0, 0);
                put8(a, 0x25C, 0);
                put8(a, 0x25E, 0x82);
            }
            return 0;
        }
        put32(a, 0xB0, em_ee_add_bits(w32(a, 0xB0), w32(a, 0x260)));      /* 0017C218 */
        put32(a, 0xB8, em_ee_add_bits(w32(a, 0xB8), w32(a, 0x264)));      /* 0017C228 */
        put32(a, 0x268, em_ee_sub_bits(w32(a, 0x268), F_ONE));            /* 0017C234 */
        return 0;
    case 7:                                                                /* 0017C240 */
        STEP(em_loco_0017B490(h, a, b8(a, 0x1F1) == 3 ? 2 : 4, b8(a, 0x235), 0, &clip));
        CALL(h, 0x001749A0u, w->request(w->context, a, clip, 0, fl(F_FOUR)));   /* 0017C290 */
        put8(a, 0x1F0, 6);                                                 /* 0017C2A4 */
        CALL(h, 0x001FB9F0u, w->sound(w->context, 0x137, 0x1000, 0x1000, 0x1000)); /* 0017C2AC */
        return 0;
    case 6:                                                                /* 0017C2BC */
        if (!(w32(a, 0x200) & 0x1000)) {
            put32(a, 0x204, F_0_75);                                       /* 0017C34C */
            return 0;
        }
        STEP(em_loco_0017B490(h, a, b8(a, 0x1F1) == 3 ? 3 : 5, b8(a, 0x235), 0, &clip));
        CALL(h, 0x001749A0u, w->request(w->context, a, clip, 1, fl(F_ZERO)));   /* 0017C314 */
        put8(a, 0x1F0, 0);                                                 /* 0017C31C */
        put8(a, 0x25C, 0);
        put32(a, 0x38, 0);
        {
            uint32_t turned = 0;
            CALL(h, 0x001B1470u, w->wrap(w->context, em_ee_add_bits(F_PI, w32(a, 0xC4)), &turned));
            put32(a, 0xC4, turned);                                        /* 0017C344 */
        }
        return 0;
    default:                                                               /* 0, 2 and >= 8 */
        return 0;
    }
}

int em_loco_0017C030(EmLocoHost *h, EmPlayerLiveActor *a)
{
    STEP(check(h, 0x0017C030u));
    if (!a) return fault(h, 0x0017C030u);
    return display(h, a);
}

/* ---- the shared calls of the two states ---------------------------------- */

static int test_call(EmLocoHost *h, uint32_t address,
                     int (*fn)(void *, EmPlayerLiveActor *, int *), EmPlayerLiveActor *a,
                     int *result)
{
    *result = 0;
    CALL(h, address, fn(h->workers.context, a, result));
    return 0;
}

/* The tail after the state switch: 001764E0 (with `s1`), then +B4 +=
 * -0.2 (idle, 00161298) or, for the walk, -0.8 when +23B == 0x35 and -0.4
 * otherwise (00161610: +23B is read after 001764E0 returns), then
 * 00175900(p, 1), 001756E0, 001796C0. */
static int tail(EmLocoHost *h, EmPlayerLiveActor *a, uint32_t s1, int walk)
{
    const EmLocoWorkers *w = &h->workers;
    int ignored = 0;
    CALL(h, 0x001764E0u, w->probes(w->context, a, s1));
    uint32_t drop = !walk ? F_M0_2 : b8(a, 0x23B) == 0x35 ? F_M0_8 : F_M0_4;
    put32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), drop));
    CALL(h, 0x00175900u, w->floor(w->context, a, 1, &ignored));
    CALL(h, 0x001756E0u, w->clearance(w->context, a, &ignored));
    CALL(h, 0x001796C0u, w->fall_check(w->context, a));
    return 0;
}

/* ---- 00161020: the idle state -------------------------------------------- */

static int idle(EmLocoHost *h, EmPlayerLiveActor *a)
{
    const EmLocoWorkers *w = &h->workers;
    uint32_t s1 = *h->scene.caller_s1;
    int result = 0;
    switch (b8(a, 6)) {
    case 0:                                                                /* 00161074 */
        put8(a, 6, 1);
        put8(a, 7, 0);
        put32(a, 0x38, 0);
        put8(a, 0x25C, 0);                                                 /* 0016108C */
        CALL(h, 0x00174A50u, w->row_request(w->context, a, fl(F_TWELVE)));
        put16(a, 0x28, 0x12C);                                             /* 00161098 */
        break;
    case 1:                                                                /* 0016109C */
        if (*h->scene.d28A9A0 != 0) return 0;
        STEP(test_call(h, 0x001607D0u, w->actions, a, &result));
        if (result != 0) return 0;
        STEP(test_call(h, 0x00160220u, w->ladder, a, &result));
        if (result != 0) return 0;
        result = 0;
        CALL(h, 0x00174AC0u, w->heading(w->context, a, 0, &result));       /* 001610D0 */
        if (result != 0) {
            put8(a, 6, b8(a, 6) + 1);                                      /* 001610F0 */
            STEP(entry_blend(h, a));                                       /* 001610EC */
            break;
        }
        if (b8(a, 7) == 1) {                                               /* 00161170 */
            if (w32(a, 0x200) & 0x1000) {
                put8(a, 7, 0);
                put16(a, 0x28, 0x12C);
                CALL(h, 0x00174A50u, w->row_request(w->context, a, fl(F_EIGHT)));
            }
        } else if (b8(a, 7) == 0) {                                        /* 0016111C */
            if (b8(a, 0x236) != 0 || (b8(a, 0x235) & 1)) break;
            int16_t count = h16(a, 0x28);                                   /* 00161138 */
            put16(a, 0x28, count - 1);                                     /* 00161144 */
            if (count != 0) break;
            put8(a, 7, b8(a, 7) + 1);                                      /* 00161164 */
            CALL(h, 0x001749A0u, w->request(w->context, a, 0x15D, 1, fl(F_EIGHT)));
        }
        break;
    case 2:                                                                /* 001611A4 */
        STEP(test_call(h, 0x001607D0u, w->actions, a, &result));
        if (result != 0) return 0;
        STEP(test_call(h, 0x00160220u, w->ladder, a, &result));
        if (result != 0) return 0;
        result = 0;
        CALL(h, 0x00174AC0u, w->heading(w->context, a, 1, &result));       /* 001611C8 */
        if (w32(a, 0x200) & 0x8000) break;                                 /* 001611D4 */
        if (em_ee_c_eq_bits(w32(a, 0x240), F_ZERO)) {                      /* 001611EC */
            put8(a, 6, 0x63);
        } else if (b8(a, 0x25D) == 0) {                                   /* 00161208 */
            put8(a, 5, 1);
            put8(a, 6, 0);
            put8(a, 0x1F0, 1);
            put8(a, 0x1F1, 1);
        } else {
            put32(a, 0x204, 0);                                            /* 00161230 */
        }
        break;
    case 0x63:                                                             /* 00161238 */
        put8(a, 6, 0x64);
        CALL(h, 0x00174A50u, w->row_request(w->context, a, fl(F_EIGHT)));
        break;
    case 0x64:                                                             /* 00161254 */
        if (w32(a, 0x200) & 0x8000) break;
        put8(a, 6, 1);
        put8(a, 7, 0);
        put16(a, 0x28, 0x12C);
        break;
    default:
        break;
    }
    return tail(h, a, s1, 0);                                              /* 0016127C */
}

int em_loco_00161020(void *host, EmPlayerLiveActor *a)
{
    EmLocoHost *h = host;
    STEP(check(h, 0x00161020u));
    if (!a) return fault(h, 0x00161020u);
    return idle(h, a);
}

/* ---- 001612D0: the walk state -------------------------------------------- */

/* 0017BC40, 0017C030, 00178B90(p, 0): the gait step of +6 = 1 and 2. */
static int gait_step(EmLocoHost *h, EmPlayerLiveActor *a)
{
    const EmLocoWorkers *w = &h->workers;
    CALL(h, 0x0017BC40u, w->motor(w->context, a));
    STEP(display(h, a));
    CALL(h, 0x00178B90u, w->translate(w->context, a, 0));
    return 0;
}

static int walk(EmLocoHost *h, EmPlayerLiveActor *a)
{
    const EmLocoWorkers *w = &h->workers;
    uint32_t s1 = *h->scene.caller_s1;
    int result = 0;
    uint8_t state = b8(a, 6);
    if (state == 0x63) {                                                   /* 001615D8 */
        CALL(h, 0x00174AC0u, w->heading(w->context, a, 1, &result));
        CALL(h, 0x00178B90u, w->translate(w->context, a, 0));              /* 001615E4 */
        if (!(w32(a, 0x200) & 0x8000))
            CALL(h, 0x0017C540u, w->handoff(w->context, a));               /* 001615FC */
    } else if (state == 0 || state == 1) {
        if (state == 0) {                                                  /* 0016131C */
            put8(a, 6, 1);
            put8(a, 7, 0);
            put32(a, 0x2EC, 0);
        }
        STEP(test_call(h, 0x001607D0u, w->actions, a, &result));           /* 0016132C */
        if (result != 0) return 0;
        STEP(test_call(h, 0x00160220u, w->ladder, a, &result));
        if (result != 0) return 0;
        CALL(h, 0x00174AC0u, w->heading(w->context, a, 1, &result));       /* 00161350 */
        STEP(gait_step(h, a));
        uint8_t stage = b8(a, 0x1F0);                                      /* 00161374 */
        if (stage == 6 || stage == 7) {
            put8(a, 6, b8(a, 6) + 1);
            put16(a, 0x28, 0);
        } else if (stage == 0) {                                           /* 001613A4 */
            put8(a, 5, 0);
            put8(a, 6, 0);
        }
    } else if (state == 2) {                                               /* 001613B8 */
        STEP(test_call(h, 0x001607D0u, w->actions, a, &result));
        if (result != 0) return 0;
        if (*h->scene.d810E74 & *h->scene.spad3B76) {                       /* 001613D8 */
            int taken = 0;
            CALL(h, 0x00184BA0u, w->use_scan(w->context, a, 1, &taken));
            if (taken != 0) {
                CALL(h, 0x001798D0u, w->use_accepted(w->context, a));      /* 001613F4 */
                put8(a, 5, 0x25);
                put8(a, 6, 0);
                return 0;
            }
        }
        CALL(h, 0x00174AC0u, w->heading(w->context, a, 1, &result));       /* 00161410 */
        uint8_t stage = b8(a, 0x1F0);
        if (stage != 6 && stage != 7) {
            if (!em_ee_c_eq_bits(w32(a, 0x240), F_ZERO)) {                 /* 00161440 */
                put8(a, 0x25C, b8(a, 0x23F) - 1);                          /* 00161464 */
                uint8_t tier = b8(a, 0x25C);
                const uint8_t *speed = emap(h->display.pose, EM_LOCO_D_00248870 + 4u * tier, 4, 0);
                if (!speed) return fault(h, 0x00161474u);
                put32(a, 0x38, rd32(speed));                               /* 00161478 */
                int16_t clip = 0;
                STEP(em_loco_0017B490(h, a, 1, b8(a, 0x235), b8(a, 0x25C), &clip)); /* 00161484 */
                s1 = (uint32_t)(int32_t)clip;                              /* 00161490 */
                if (b8(a, 0x1F1) == 3) {
                    CALL(h, 0x001749A0u, w->request(w->context, a, clip, 0, fl(F_ZERO)));
                } else {
                    uint32_t frames = 0;
                    STEP(clip_frames(h, a, clip, &frames));                /* 001614C0 */
                    uint32_t *s3A20 = h->display.pose->globals->spad3A20;
                    *s3A20 = frames;                                       /* 001614E0 */
                    uint32_t half = em_ee_div_bits(*s3A20, F_TWO);         /* 001614F4 */
                    STEP(arbiter(h, a, clip, F_ZERO, half));               /* 00161500 */
                }
                put8(a, 6, b8(a, 6) - 1);                                  /* 00161514 */
                put8(a, 0x1F0, 1);
                put8(a, 0x1F1, 1);
            } else {                                                       /* 00161524 */
                put8(a, 5, 0);
                put8(a, 6, 0);
                put8(a, 0x1F0, 0);
            }
        } else {                                                           /* 00161534 */
            if (!(h16(a, 0x28) & 7)) {
                uint8_t surface = b8(a, 0x23A);
                if (surface == 6 || surface == 5) {
                    CALL(h, 0x001EFD90u, w->effect(w->context, UINT32_C(0x80000033), a));
                } else if (b8(a, 0x23C) == 0 && b8(a, 0x23D) == 0) {
                    CALL(h, 0x001EFD90u, w->effect(w->context, UINT32_C(0x80000012), a));
                }
            }
            put16(a, 0x28, h16(a, 0x28) + 1);                              /* 001615B0 */
        }
        STEP(gait_step(h, a));                                             /* 001615B4 */
    }
    /* 00161608: the tail (any other +6 comes straight here). */
    return tail(h, a, s1, 1);
}

int em_loco_001612D0(void *host, EmPlayerLiveActor *a)
{
    EmLocoHost *h = host;
    STEP(check(h, 0x001612D0u));
    if (!a) return fault(h, 0x001612D0u);
    return walk(h, a);
}
